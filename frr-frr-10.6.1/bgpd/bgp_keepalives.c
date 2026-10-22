// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Keepalives.
 * Implements a producer thread to generate BGP keepalives for peers.
 * Copyright (C) 2017 Cumulus Networks, Inc.
 * Quentin Young
 */

/* ============================================================================
 * 【文件导读】bgp_keepalives.c —— 统一的 keepalive 生产者线程
 * ============================================================================
 * 【解决什么问题】每个 Established 邻居都要周期性发 KEEPALIVE 保活。若每个邻居
 * 各自在主线程挂一个定时器，邻居一多就会产生成千上万个定时器、主线程被频繁
 * 唤醒，开销大且不易管理。本文件改用**一个独立线程 + 一张哈希表**集中调度：
 *   peerhash : key 是 peer 指针、value 是 struct pkat(记录该邻居上次发 keepalive
 *              的绝对时间)；只有需要发 keepalive 的邻居才在表里。
 *   线程主循环(bgp_keepalives_start) : 遍历整张表算出“最近一个到期时刻”，然后
 *              用条件变量睡到那个时刻(既非忙等、也非每邻居一个定时器)；被
 *              on / off 唤醒时立即重算。
 * 【为什么这样设计】(1) 等待点数量与邻居数解耦，全局只需 1 个等待点；(2) 所有
 * keepalive 的发送都串行在同一线程里，天然避免并发发送；(3) 用 hash 增删成员，
 * 复杂度 O(1)。
 * 【线程安全】主线程通过 bgp_keepalives_on / off 增删成员，都会先拿 peerhash_mtx；
 * 线程循环也持锁遍历。条件变量用**单调时钟**(CLOCK_MONOTONIC)，避免系统时间跳变
 * 导致睡眠时长算错。注意本线程不使用 FRR 常规的 fpt_run，而是自建循环。
 * 【关键数据结构】struct pkat{ peer, last }: 邻居 + 上次发送时刻。
 * 【阅读建议】pkat_new / pkat_del(条目生命周期) → peer_process(单邻居是否到期，
 * 顺带算出线程该睡多久) → bgp_keepalives_start(主循环) → on / off / stop。
 * ============================================================================
 */

/* clang-format off */
#include <zebra.h>
#include <pthread.h>		// for pthread_mutex_lock, pthread_mutex_unlock

#include "frr_pthread.h"        // for frr_pthread
#include "hash.h"		// for hash, hash_clean, hash_create_size...
#include "log.h"		// for zlog_debug
#include "memory.h"		// for MTYPE_TMP, XFREE, XCALLOC, XMALLOC
#include "monotime.h"		// for monotime, monotime_since

#include "bgpd/bgpd.h"          // for peer, PEER_EVENT_KEEPALIVES_ON, peer...
#include "bgpd/bgp_debug.h"	// for bgp_debug_neighbor_events
#include "bgpd/bgp_packet.h"	// for bgp_keepalive_send
#include "bgpd/bgp_keepalives.h"
/* clang-format on */

/* 【本文件私有的内存标签】BGP_PKAT(单条 pkat 条目)、BGP_COND / BGP_MUTEX(
 * 条件变量与互斥锁实体)。用 DEFINE_MTYPE_STATIC 定义，仅本文件可见。 */
DEFINE_MTYPE_STATIC(BGPD, BGP_PKAT, "Peer KeepAlive Timer");
DEFINE_MTYPE_STATIC(BGPD, BGP_COND, "BGP Peer pthread Conditional");
DEFINE_MTYPE_STATIC(BGPD, BGP_MUTEX, "BGP Peer pthread Mutex");

/*
 * Peer KeepAlive Timer.
 * Associates a peer with the time of its last keepalive.
 */
/* 【pkat】把一个 peer 与它上次发送 keepalive 的(单调)绝对时间绑在一起，作为
 * peerhash 的 value；key 直接取 peer 指针。 */
struct pkat {
	/* the peer to send keepalives to */
	struct peer *peer;
	/* absolute time of last keepalive sent */
	struct timeval last;
};

/* List of peers we are sending keepalives for, and associated mutex. */
/* 【共享状态】(三者均受 peerhash_mtx 保护)
 *   peerhash_mtx  : 保护整张表与线程运行状态。
 *   peerhash_cond : 条件变量，on / off / stop 用它唤醒睡眠中的线程。
 *   peerhash      : 邻居集合，key=peer 指针，value=struct pkat。 */
static pthread_mutex_t *peerhash_mtx;
static pthread_cond_t *peerhash_cond;
static struct hash *peerhash;

/* 【新建条目】为 peer 分配 pkat 并记录“此刻”为上次发送时刻(单调时钟)。
 * 注意此处**不加** peer 引用计数——引用由调用方 bgp_keepalives_on 负责 peer_lock。 */
static struct pkat *pkat_new(struct peer *peer)
{
	struct pkat *pkat = XMALLOC(MTYPE_BGP_PKAT, sizeof(struct pkat));
	pkat->peer = peer;
	monotime(&pkat->last);
	return pkat;
}

/* 【释放条目】pkat 作为 hash 的 value 析构回调(hash_clean / hash_release 时调用)；
 * 只释放 pkat 自身，不碰 peer 引用(解除引用由调用方 peer_unlock)。 */
static void pkat_del(void *pkat)
{
	XFREE(MTYPE_BGP_PKAT, pkat);
}


/*
 * Callback for hash_iterate. Determines if a peer needs a keepalive and if so,
 * generates and sends it.
 *
 * For any given peer, if the elapsed time since its last keepalive exceeds its
 * configured keepalive timer, a keepalive is sent to the peer and its
 * last-sent time is reset. Additionally, If the elapsed time does not exceed
 * the configured keepalive timer, but the time until the next keepalive is due
 * is within a hardcoded tolerance, a keepalive is sent as if the configured
 * timer was exceeded. Doing this helps alleviate nanosecond sleeps between
 * ticks by grouping together peers who are due for keepalives at roughly the
 * same time. This tolerance value is arbitrarily chosen to be 100ms.
 *
 * In addition, this function calculates the maximum amount of time that the
 * keepalive thread can sleep before another tick needs to take place. This is
 * equivalent to shortest time until a keepalive is due for any one peer.
 *
 * @return maximum time to wait until next update (0 if infinity)
 */
/* 【单邻居判定 + 全局下一睡点】本函数是线程循环的核心回调(hash_iterate 会对每个
 * pkat 调用一次)。逻辑：
 *   elapsed = 距上次发送的时间；diff = v_keepalive - elapsed。
 *   - 若 elapsed >= v_keepalive，或 diff 已小于 100ms 容差，就立即发一个 KEEPALIVE
 *     并重置 last / elapsed；用 100ms 容差把“本来差一点点到期”的邻居一起带上，
 *     免得线程为它们再来一次极短的睡眠(见上方英文注释)。
 *   - 每次把本邻居的 diff 与 arg(指向 next_update)取较小者，供主循环决定睡多久；
 *     某邻居 v_keepalive==0(即不发 keepalive)时直接跳过。
 * 注意 elapsed / ka / diff 被声明为 static —— 复用缓冲、减少反复构造；本线程串行，
 * 不存在重入问题。 */
static void peer_process(struct hash_bucket *hb, void *arg)
{
	struct pkat *pkat = hb->data;

	struct timeval *next_update = arg;

	static struct timeval elapsed;  // elapsed time since keepalive
	static struct timeval ka = {0}; // peer->v_keepalive as a timeval
	static struct timeval diff;     // ka - elapsed

	static const struct timeval tolerance = {0, 100000};

	uint32_t v_ka = atomic_load_explicit(&pkat->peer->v_keepalive,
					     memory_order_relaxed);

	/* 0 keepalive timer means no keepalives */
	if (v_ka == 0)
		return;

	/* calculate elapsed time since last keepalive */
	monotime_since(&pkat->last, &elapsed);

	/* calculate difference between elapsed time and configured time */
	ka.tv_sec = v_ka;
	timersub(&ka, &elapsed, &diff);

	int send_keepalive =
		elapsed.tv_sec >= ka.tv_sec || timercmp(&diff, &tolerance, <);

	if (send_keepalive) {
		if (bgp_debug_keepalive(pkat->peer))
			zlog_debug("%s [FSM] Timer (keepalive timer expire)",
				   pkat->peer->host);

		bgp_keepalive_send(pkat->peer->connection);
		monotime(&pkat->last);
		memset(&elapsed, 0, sizeof(elapsed));
		diff = ka;
	}

	/* if calculated next update for this peer < current delay, use it */
	if (next_update->tv_sec < 0 || timercmp(&diff, next_update, <))
		*next_update = diff;
}

/* 【比较回调】判定两个 pkat 是否属于同一邻居：比较 peer 指针。 */
static bool peer_hash_cmp(const void *f, const void *s)
{
	const struct pkat *p1 = f;
	const struct pkat *p2 = s;

	return p1->peer == p2->peer;
}

/* 【哈希键】直接拿 peer 指针当键(指针唯一标识一个邻居)。 */
static unsigned int peer_hash_key(const void *arg)
{
	const struct pkat *pkat = arg;
	return (uintptr_t)pkat->peer;
}

/* Cleanup handler / deinitializer. */
/* 【线程退出清理】作为 pthread_cleanup_push 的回调，在线程结束(正常退出或被取消)
 * 时执行：清空并释放 peerhash、解锁并销毁互斥锁与条件变量、释放它们的内存。 */
static void bgp_keepalives_finish(void *arg)
{
	hash_clean_and_free(&peerhash, pkat_del);

	pthread_mutex_unlock(peerhash_mtx);
	pthread_mutex_destroy(peerhash_mtx);
	pthread_cond_destroy(peerhash_cond);

	XFREE(MTYPE_BGP_MUTEX, peerhash_mtx);
	XFREE(MTYPE_BGP_COND, peerhash_cond);
}

/*
 * Entry function for peer keepalive generation pthread.
 */
/* 【线程主体】keepalives 线程的入口(由 frr_pthread 框架启动，见 bgp_main.c)：
 *   1) 解锁本线程的 RCU 读锁——本线程不走常规 event loop，需手动解锁，RCU 后台
 *      线程才能运行(见下方英文注释)；
 *   2) 初始化互斥锁与条件变量(条件变量绑定 CLOCK_MONOTONIC)、创建 peerhash；
 *   3) 注册 pthread 清理 handler(线程退出时释放资源)；
 *   4) 通知框架“我已启动”，随后进入主循环：表非空就睡到下一个最近到期时刻；
 *      表为空就阻塞等到有邻居被登记；醒来后遍历表逐个 peer_process，算出新的
 *      下一睡点 next_update_ts。 */
void *bgp_keepalives_start(void *arg)
{
	struct frr_pthread *fpt = arg;
	frr_event_loop_set_pthread_owner(fpt->master, pthread_self());

	struct timeval currtime = {0, 0};
	struct timeval aftertime = {0, 0};
	struct timeval next_update = {0, 0};
	struct timespec next_update_ts = {0, 0};

	/*
	 * The RCU mechanism for each pthread is initialized in a "locked"
	 * state. That's ok for pthreads using the frr_pthread,
	 * event_fetch event loop, because that event loop unlocks regularly.
	 * For foreign pthreads, the lock needs to be unlocked so that the
	 * background rcu pthread can run.
	 */
	rcu_read_unlock();

	peerhash_mtx = XCALLOC(MTYPE_BGP_MUTEX, sizeof(pthread_mutex_t));
	peerhash_cond = XCALLOC(MTYPE_BGP_COND, sizeof(pthread_cond_t));

	/* initialize mutex */
	pthread_mutex_init(peerhash_mtx, NULL);

	/* use monotonic clock with condition variable */
	pthread_condattr_t attrs;
	pthread_condattr_init(&attrs);
	pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC);
	pthread_cond_init(peerhash_cond, &attrs);
	pthread_condattr_destroy(&attrs);

	/*
	 * We are not using normal FRR pthread mechanics and are
	 * not using fpt_run
	 */
	frr_pthread_set_name(fpt);

	/* initialize peer hashtable */
	peerhash = hash_create_size(2048, peer_hash_key, peer_hash_cmp, NULL);
	pthread_mutex_lock(peerhash_mtx);

	/* register cleanup handler */
	pthread_cleanup_push(&bgp_keepalives_finish, NULL);

	/* notify anybody waiting on us that we are done starting up */
	frr_pthread_notify_running(fpt);

	while (atomic_load_explicit(&fpt->running, memory_order_relaxed)) {
		/* 【等待分支】表里有邻居就定时睡到下一到期点(timedwait)；表为空就
		 * 一直等到有人登记或被要求退出，避免空转。 */
		if (peerhash->count > 0)
			pthread_cond_timedwait(peerhash_cond, peerhash_mtx,
					       &next_update_ts);
		else
			while (peerhash->count == 0
			       && atomic_load_explicit(&fpt->running,
						       memory_order_relaxed))
				pthread_cond_wait(peerhash_cond, peerhash_mtx);

		monotime(&currtime);

		/* 【哨兵】tv_sec = -1 表示“尚无候选”；peer_process 里只要
		 * next_update->tv_sec < 0 就会用本邻居的 diff 覆盖它，从而收敛到
		 * 全局最早到期时间。 */
		next_update.tv_sec = -1;

		hash_iterate(peerhash, peer_process, &next_update);
		if (next_update.tv_sec == -1)
			memset(&next_update, 0, sizeof(next_update));

		monotime_since(&currtime, &aftertime);

		timeradd(&currtime, &next_update, &next_update);
		TIMEVAL_TO_TIMESPEC(&next_update, &next_update_ts);
	}

	/* clean up */
	pthread_cleanup_pop(1);

	return NULL;
}

/* --- thread external functions ------------------------------------------- */
/* 【对外接口区】以下三个函数供主线程调用：登记 / 注销邻居、停止线程。都通过
 * peerhash_mtx 与线程循环互斥；frr_with_mutex 是 FRR 的作用域加解锁宏。 */

/* 【登记邻居】把 connection->peer 加入 keepalive 集合(邻居进入 Established 时调用)。
 * 已是成员(thread_flags 置位)则直接返回。用栈上 holder 只填 peer 做快速查表；
 * 未命中才新建 pkat 入表并对 peer 加引用。最后无条件唤醒线程令其重算睡点。 */
void bgp_keepalives_on(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;

	struct frr_pthread *fpt = bgp_pth_ka;
	assert(fpt->running);

	/* placeholder bucket data to use for fast key lookups */
	static struct pkat holder = {0};

	/*
	 * We need to ensure that bgp_keepalives_init was called first
	 */
	assert(peerhash_mtx);

	frr_with_mutex (peerhash_mtx) {
		if (CHECK_FLAG(peer->thread_flags, PEER_THREAD_KEEPALIVES_ON))
			return;

		holder.peer = peer;
		if (!hash_lookup(peerhash, &holder)) {
			struct pkat *pkat = pkat_new(peer);
			(void)hash_get(peerhash, pkat, hash_alloc_intern);
			peer_lock(peer);
		}
		SET_FLAG(peer->thread_flags, PEER_THREAD_KEEPALIVES_ON);
		/* Force the keepalive thread to wake up */
		pthread_cond_signal(peerhash_cond);
	}
}

/* 【注销邻居】把 connection->peer 移出集合(邻居离开 Established / 断开时调用)。
 * 非成员则直接返回。用 holder 查表后 hash_release 摘除，释放 pkat 并对 peer 解引用；
 * 清掉标志位使其之后可重新登记。会清空表项但**不**唤醒线程(线程下次遍历时自然反映)。 */
void bgp_keepalives_off(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;

	struct frr_pthread *fpt = bgp_pth_ka;
	assert(fpt->running);

	/* placeholder bucket data to use for fast key lookups */
	static struct pkat holder = {0};

	/*
	 * We need to ensure that bgp_keepalives_init was called first
	 */
	assert(peerhash_mtx);

	frr_with_mutex (peerhash_mtx) {
		if (!CHECK_FLAG(peer->thread_flags, PEER_THREAD_KEEPALIVES_ON))
			return;

		holder.peer = peer;
		struct pkat *res = hash_release(peerhash, &holder);
		if (res) {
			pkat_del(res);
			peer_unlock(peer);
		}
		UNSET_FLAG(peer->thread_flags, PEER_THREAD_KEEPALIVES_ON);
	}
}

/* 【停止线程】置 fpt->running=false 并唤醒条件变量，使线程跳出主循环；随后 join
 * 等待其真正结束。返回 0。 */
int bgp_keepalives_stop(struct frr_pthread *fpt, void **result)
{
	assert(fpt->running);

	frr_with_mutex (peerhash_mtx) {
		atomic_store_explicit(&fpt->running, false,
				      memory_order_relaxed);
		pthread_cond_signal(peerhash_cond);
	}

	pthread_join(fpt->thread, result);
	return 0;
}
