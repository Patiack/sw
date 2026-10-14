// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2017-19  David Lamparter, for NetDEF, Inc.
 */

/* 【文件导读】frrcu.c —— FRR 用户态 RCU 的具体实现（基于 epoch 纪元）
 *
 * 职责：
 *   实现 frrcu.h 声明的整套 RCU 机制。核心思路是"纪元（epoch）"计数：
 *   全局 rcu_seq 表示当前纪元；线程在 rcu_read_lock() 时"钉住"某个纪元，表示
 *   它可能正在访问该纪元的数据。回收线程 rcu_main 会遍历所有线程，等它们都离开
 *   旧纪元之后，才真正执行延迟释放动作（即宽限期结束）。
 *
 * 核心数据结构：
 *   - struct rcu_thread：每线程 RCU 状态，含 seqlock 纪元值与嵌套深度 depth。
 *   - struct rcu_next：一次"纪元推进（bump）"在队列上留下的占位项。
 *   - rcu_heads：待处理动作队列（rcu_head 原子链表）。
 *   - rcu_threads：所有线程的 rcu_thread 链表。
 *   - 全局 rcu_seq（当前纪元）、rcu_dirty（有动作待处理时的纪元标记）。
 *
 * 与其他模块的关系：
 *   - seqlock.h：提供纪元值的等待 / 递增原语，是宽限期判定的基础。
 *   - atomlist.h：提供无锁队列，承载 rcu_heads / rcu_threads。
 *   - sigevent.h：RCU 线程屏蔽信号时用到。
 *   - memory.h / MTYPE：延迟释放时按类型做内存统计。
 *
 * 阅读顺序：
 *   1) 先看顶部 implementation notes，了解 epoch 模型。
 *   2) 看全局变量与 struct rcu_thread，理解状态载体。
 *   3) 读 rcu_read_lock / rcu_read_unlock，掌握读侧如何钉住纪元。
 *   4) 读 rcu_bump / rcu_main，理解宽限期如何被推进与消费。
 *   5) 最后看 rcu_shutdown 与 rcu_close 等收尾路径。
 *
 * 易踩的坑：
 *   - rcu_bump 必须在持有读锁时调用，否则 rcu_main 可能在链表"中断点"上遍历出错。
 *   - rcu_heads 队列必须始终保持至少 1 个元素，绝不能清空，否则 last 指针失效。
 *   - 主线程与 RCU 回收线程各有预置的 rcu_thread（main / rcu），不要当作普通线程。
 *   - rcu_dirty 只用于判断"是否需要 bump"；竞争下多 bump 无害、少 bump 会延迟回收。
 */

/* 顶部 implementation notes 要点：rcu_seq 是全局纪元计数；线程在 read_lock 时持有
 * 某个纪元；rcu_thread 结构体本身也用 RCU 方式释放；rcu_head 借助 ATOMLIST 的
 * "last 指针"特性避免了 ABA / use-after-free，但前提是队列始终非空。 */
/* implementation notes:  this is an epoch-based RCU implementation.  rcu_seq
 * (global variable) counts the current epoch.  Threads hold a specific epoch
 * in rcu_read_lock().  This is the oldest epoch a thread might be accessing
 * data from.
 *
 * The rcu_seq global is only pushed forward on rcu_read_lock() and
 * rcu_read_unlock() calls.  This makes things a tad more efficient since
 * those are the only places it matters:
 * - on rcu_read_lock, we don't want to hold an old epoch pointlessly
 * - on rcu_read_unlock, we want to make sure we're not stuck on an old epoch
 *   when heading into a long idle period where no thread holds RCU
 *
 * rcu_thread structures themselves are RCU-free'd.
 *
 * rcu_head structures are the most iffy;  normally for an ATOMLIST we would
 * need to make sure we use rcu_free or pthread_rwlock to deallocate old items
 * to prevent ABA or use-after-free problems.  However, our ATOMLIST code
 * guarantees that if the list remains non-empty in all cases, we only need
 * the "last" pointer to do an "add_tail()", i.e. we can't run into ABA/UAF
 * issues - but we do need to keep at least 1 item on the list.
 *
 * (Search the atomlist code for all uses of "last")
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* 引入依赖：frrcu.h 的接口、seqlock 的纪元原语、atomlist 的无锁链表、sigevent 的信号屏蔽。 */
#include "frrcu.h"
#include "seqlock.h"
#include "atomlist.h"
#include "sigevent.h"

/* 定义两个本文件私有的内存类型（memtype），供延迟释放时做带名字的内存统计：
 *   MTYPE_RCU_THREAD —— rcu_thread 结构体的分配类型；
 *   MTYPE_RCU_NEXT   —— struct rcu_next 占位项的分配类型。 */
DEFINE_MTYPE_STATIC(LIB, RCU_THREAD,    "RCU thread");
DEFINE_MTYPE_STATIC(LIB, RCU_NEXT,      "RCU sequence barrier");

/* 实例化 rcu_heads 原子链表：元素是 struct rcu_head，挂链字段是 head。
 * 队列里存放所有待处理的 RCU 动作，由 rcu_main 线程消费。 */
DECLARE_ATOMLIST(rcu_heads, struct rcu_head, head);

/* struct rcu_thread：描述一个线程的 RCU 状态，是宽限期判定的最小单位。
 *   head     —— 挂入 rcu_threads 链表的节点；
 *   rcu_head —— 用于把本结构体自身也做 RCU 延迟释放；
 *   rcu      —— seqlock，记录本线程当前钉住的纪元值；
 *   depth    —— 读锁嵌套深度，仅由本线程访问（非原子）。 */
PREDECL_ATOMLIST(rcu_threads);
struct rcu_thread {
	struct rcu_threads_item head;

	struct rcu_head rcu_head;

	struct seqlock rcu;

	/* only accessed by thread itself, not atomic */
	unsigned depth;
};
/* 实例化 rcu_threads 原子链表：元素是 struct rcu_thread，挂链字段是 head。
 * 全局所有活动线程的 RCU 状态都挂在这条链上，供回收线程遍历等待。 */
DECLARE_ATOMLIST(rcu_threads, struct rcu_thread, head);

/* 三个预置的动作常量，多处共享同一实例，避免重复构造：
 *   rcua_next  —— 纪元推进占位动作（队列同步点）；
 *   rcua_end   —— 收尾动作，被 rcu_main 看到后结束回收循环；
 *   rcua_close —— 关闭 fd 的动作，供 rcu_close() 使用。 */
static const struct rcu_action rcua_next  = { .type = RCUA_NEXT };
static const struct rcu_action rcua_end   = { .type = RCUA_END };
static const struct rcu_action rcua_close = { .type = RCUA_CLOSE };

/* struct rcu_next：一次纪元推进（rcu_bump）在队列上放置两项。
 *   head_next —— RCUA_NEXT 占位项，标记纪元边界；
 *   head_free —— RCUA_FREE 项，稍后回收本 rcu_next 结构体自身。
 * 先入队 head_next 再入队 head_free，保证队列不会出现"被清空"的危险窗口。 */
struct rcu_next {
	struct rcu_head head_free;
	struct rcu_head head_next;
};

/* rcu_free_internal(mtype, ptr, field)：本文件内部使用的"立即入队"版释放宏。
 * 与 frrcu.h 里的 rcu_free 不同，这里不做 NULL 判断，且直接操作头部指针并入队，
 * 主要用于 rcu_next 占位项与 rcu_thread 结构体自身的延迟释放。 */
#define rcu_free_internal(mtype, ptr, field)                                   \
	do {                                                                   \
		typeof(ptr) _ptr = (ptr);                                      \
		struct rcu_head *_rcu_head = &_ptr->field;                     \
		static const struct rcu_action _rcu_action = {                 \
			.type = RCUA_FREE,                                     \
			.u.free = {                                            \
				.mt = mtype,                                   \
				.offset = offsetof(typeof(*_ptr), field),      \
			},                                                     \
		};                                                             \
		_rcu_head->action = &_rcu_action;                              \
		rcu_heads_add_tail(&rcu_heads, _rcu_head);                     \
	} while (0)

/* rcu_seq：全局主 RCU 序号（纪元）。它是宽限期判定的核心，只在 read_lock / read_unlock
 * 时被推进一步。rcu_dirty：每当有动作加入 RCU 队列时被设为当时的 rcu_seq；读侧
 * 拿它与 rcu_seq 比较，若相等说明"有脏数据待推进"，于是触发一次 bump。 */
/* primary global RCU position */
static struct seqlock rcu_seq;
/* this is set to rcu_seq whenever something is added on the RCU queue.
 * rcu_read_lock() and rcu_read_unlock() will then bump rcu_seq up one step.
 */
static _Atomic seqlock_val_t rcu_dirty;

/* 两条全局链表：rcu_threads 挂所有线程的 RCU 状态，rcu_heads 挂所有待处理的 RCU 动作。 */
static struct rcu_threads_head rcu_threads;
static struct rcu_heads_head rcu_heads;

/* 主线程与 RCU 回收线程各自有一个预置的 rcu_thread：
 *   rcu_thread_main —— 主线程并非通过 pthread_create 创建，故需要它一一对应；
 *   rcu_thread_rcu  —— 回收线程的占位状态，不加入 rcu_threads 链表，仅为让回收线程
 *                      自身也能安全调用 read_lock / read_unlock。 */
/* main thread & RCU sweeper have pre-setup rcu_thread structures.  The
 * reasons are different:
 *
 * - rcu_thread_main is there because the main thread isn't started like
 *   other threads, it's implicitly created when the program is started.  So
 *   rcu_thread_main matches up implicitly.
 *
 * - rcu_thread_rcu isn't actually put on the rcu_threads list (makes no
 *   sense really), it only exists so we can call RCU-using functions from
 *   the RCU thread without special handling in rcu_read_lock/unlock.
 */
static struct rcu_thread rcu_thread_main;
static struct rcu_thread rcu_thread_rcu;

/* 全局运行态变量：
 *   rcu_pthread    —— 回收线程的 pthread 句柄；
 *   rcu_thread_key —— TLS 键，用来按线程取回各自的 rcu_thread；
 *   rcu_active     —— RCU 是否已启动；未启动时释放动作会同步直接执行。 */
static pthread_t rcu_pthread;
static pthread_key_t rcu_thread_key;
static bool rcu_active;

/* 前置声明：rcu_start 启动回收线程；rcu_bump 推进一次纪元。 */
static void rcu_start(void);
static void rcu_bump(void);

/*
 * preinitialization for main thread
 */
/* rcu_thread_end：线程退出时的析构回调（由 pthread_key 的 destructor 触发）。 */
static void rcu_thread_end(void *rcu_thread);

/* rcu_preinit：以 constructor 属性注册，程序启动时自动执行，完成主线程的 RCU 初始化。 */
static void rcu_preinit(void) __attribute__((constructor));
static void rcu_preinit(void)
{
	struct rcu_thread *rt;

	rt = &rcu_thread_main;
	rt->depth = 1;
	seqlock_init(&rt->rcu);
	seqlock_acquire_val(&rt->rcu, SEQLOCK_STARTVAL);

	pthread_key_create(&rcu_thread_key, rcu_thread_end);
	pthread_setspecific(rcu_thread_key, rt);

	rcu_threads_add_tail(&rcu_threads, rt);

	/* RCU sweeper's rcu_thread is a dummy, NOT added to rcu_threads */
	rt = &rcu_thread_rcu;
	rt->depth = 1;

	seqlock_init(&rcu_seq);
	seqlock_acquire_val(&rcu_seq, SEQLOCK_STARTVAL);
}

/* rcu_self：取回当前线程的 rcu_thread（通过 TLS 键）。 */
static struct rcu_thread *rcu_self(void)
{
	return (struct rcu_thread *)pthread_getspecific(rcu_thread_key);
}

/* rcu_thread_new：为一个新线程分配并初始化 rcu_thread。
 * arg 为父线程的 rcu_thread 指针（可为 NULL）。新线程初始深度为 1，并继承父线程
 * 的纪元——这样才能用 RCU 安全地把数据通过 arg 传进新线程。 */
struct rcu_thread *rcu_thread_new(void *arg)
{
	struct rcu_thread *rt, *cur = arg;

	/* new thread always starts with rcu_read_lock held at depth 1, and
	 * holding the same epoch as the parent (this makes it possible to
	 * use RCU for things passed into the thread through its arg)
	 */
	rt = XCALLOC(MTYPE_RCU_THREAD, sizeof(*rt));
	rt->depth = 1;

	seqlock_init(&rt->rcu);
	if (cur)
		seqlock_acquire(&rt->rcu, &cur->rcu);

	rcu_threads_add_tail(&rcu_threads, rt);

	return rt;
}

/* rcu_thread_prepare：非主线程在创建前调用。要求调用者已持有读锁；若 RCU 尚未启动
 * 则先启动回收线程；随后基于当前线程的状态为新线程预建 rcu_thread。 */
/*
 * thread management (for the non-main thread)
 */
struct rcu_thread *rcu_thread_prepare(void)
{
	struct rcu_thread *cur;

	rcu_assert_read_locked();

	if (!rcu_active)
		rcu_start();

	cur = rcu_self();
	assert(cur->depth);

	return rcu_thread_new(cur);
}

/* rcu_thread_start：在新线程内调用，把预建的 rcu_thread 绑定到该线程的 TLS 上。 */
void rcu_thread_start(struct rcu_thread *rt)
{
	pthread_setspecific(rcu_thread_key, rt);
}

/* rcu_thread_unprepare：线程退出 / 创建失败时的清理。把本线程钉到当前纪元、推进一次
 * 纪元，然后（非主线程时）用 RCU 方式延迟释放 rcu_thread 自身，最后从链表摘除并释放纪元。 */
void rcu_thread_unprepare(struct rcu_thread *rt)
{
	if (rt == &rcu_thread_rcu)
		return;

	rt->depth = 1;
	seqlock_acquire(&rt->rcu, &rcu_seq);

	rcu_bump();
	if (rt != &rcu_thread_main)
		/* this free() happens after seqlock_release() below */
		rcu_free_internal(MTYPE_RCU_THREAD, rt, rcu_head);

	rcu_threads_del(&rcu_threads, rt);
	seqlock_release(&rt->rcu);
}

/* rcu_thread_end：pthread_key 析构函数，线程结束时被调用，转调 unprepare 做清理。 */
static void rcu_thread_end(void *rtvoid)
{
	struct rcu_thread *rt = rtvoid;
	rcu_thread_unprepare(rt);
}

/* 主体 RCU 控制部分。
 * rcu_bump：推进一次纪元。它向 rcu_heads 依次入队一个 RCUA_NEXT 占位项和一个
 * RCUA_FREE 项（回收占位结构体），随后 seqlock_bump 推高 rcu_seq，唤醒回收线程。
 * 之所以额外插入 RCUA_FREE，是为了保证队列后段仍有元素，避免把队列清空而破坏
 * ATOMLIST 的 last 指针。本函数必须在持有读锁时调用。 */
/*
 * main RCU control aspects
 */

static void rcu_bump(void)
{
	struct rcu_next *rn;

	rn = XMALLOC(MTYPE_RCU_NEXT, sizeof(*rn));

	/* note: each RCUA_NEXT item corresponds to exactly one seqno bump.
	 * This means we don't need to communicate which seqno is which
	 * RCUA_NEXT, since we really don't care.
	 */

	/*
	 * Important race condition:  while rcu_heads_add_tail is executing,
	 * there is an intermediate point where the rcu_heads "last" pointer
	 * already points to rn->head_next, but rn->head_next isn't added to
	 * the list yet.  That means any other "add_tail" calls append to this
	 * item, which isn't fully on the list yet.  Freeze this thread at
	 * that point and look at another thread doing a rcu_bump.  It adds
	 * these two items and then does a seqlock_bump.  But the rcu_heads
	 * list is still "interrupted" and there's no RCUA_NEXT on the list
	 * yet (from either the frozen thread or the second thread).  So
	 * rcu_main() might actually hit the end of the list at the
	 * "interrupt".
	 *
	 * This situation is prevented by requiring that rcu_read_lock is held
	 * for any calls to rcu_bump, since if we're holding the current RCU
	 * epoch, that means rcu_main can't be chewing on rcu_heads and hit
	 * that interruption point.  Only by the time the thread has continued
	 * to rcu_read_unlock() - and therefore completed the add_tail - the
	 * RCU sweeper gobbles up the epoch and can be sure to find at least
	 * the RCUA_NEXT and RCUA_FREE items on rcu_heads.
	 */
	rn->head_next.action = &rcua_next;
	rcu_heads_add_tail(&rcu_heads, &rn->head_next);

	/* free rn that we allocated above.
	 *
	 * This is INTENTIONALLY not built into the RCUA_NEXT action.  This
	 * ensures that after the action above is popped off the queue, there
	 * is still at least 1 item on the RCU queue.  This means we never
	 * delete the last item, which is extremely important since it keeps
	 * the atomlist ->last pointer alive and well.
	 *
	 * If we were to "run dry" on the RCU queue, add_tail may run into the
	 * "last item is being deleted - start over" case, and then we may end
	 * up accessing old RCU queue items that are already free'd.
	 */
	rcu_free_internal(MTYPE_RCU_NEXT, rn, head_free);

	/* Only allow the RCU sweeper to run after these 2 items are queued.
	 *
	 * If another thread enqueues some RCU action in the intermediate
	 * window here, nothing bad happens - the queued action is associated
	 * with a larger seq# than strictly necessary.  Thus, it might get
	 * executed a bit later, but that's not a problem.
	 *
	 * If another thread acquires the read lock in this window, it holds
	 * the previous epoch, but its RCU queue actions will be in the next
	 * epoch.  This isn't a problem either, just a tad inefficient.
	 */
	seqlock_bump(&rcu_seq);
}

/* rcu_bump_maybe：条件推进。若 rcu_dirty 与当前 rcu_seq 相等，说明队列里有待推进的
 * 动作，就调用 rcu_bump()。存在竞争时多 bump 无害，少 bump 只会让回收稍有延迟。 */
static void rcu_bump_maybe(void)
{
	seqlock_val_t dirty;

	dirty = atomic_load_explicit(&rcu_dirty, memory_order_relaxed);
	/* no problem if we race here and multiple threads bump rcu_seq;
	 * bumping too much causes no issues while not bumping enough will
	 * result in delayed cleanup
	 */
	if (dirty == seqlock_cur(&rcu_seq))
		rcu_bump();
}

/* rcu_read_lock：进入读侧临界区。深度自增，只有最外层（depth 由 0 变 1）才真正
 * 钉住当前纪元：先 acquire 一次（保证 bump 时持有旧纪元），必要时 bump，再重新
 * acquire 到最新纪元。嵌套调用直接返回。 */
void rcu_read_lock(void)
{
	struct rcu_thread *rt = rcu_self();

	assert(rt);
	if (rt->depth++ > 0)
		return;

	seqlock_acquire(&rt->rcu, &rcu_seq);
	/* need to hold RCU for bump ... */
	rcu_bump_maybe();
	/* ... but no point in holding the old epoch if we just bumped */
	seqlock_acquire(&rt->rcu, &rcu_seq);
}

/* rcu_read_unlock：退出读侧临界区。深度自减，只有最外层退出时才真正释放纪元；
 * 释放前顺手 bump_maybe，避免线程在长时间空闲时还卡在旧纪元上。 */
void rcu_read_unlock(void)
{
	struct rcu_thread *rt = rcu_self();

	assert(rt && rt->depth);
	if (--rt->depth > 0)
		return;
	rcu_bump_maybe();
	seqlock_release(&rt->rcu);
}

/* rcu_assert_read_locked：断言当前线程处于读锁定状态（深度非零且持有纪元）。 */
void rcu_assert_read_locked(void)
{
	struct rcu_thread *rt = rcu_self();
	assert(rt && rt->depth && seqlock_held(&rt->rcu));
}

/* rcu_assert_read_unlocked：断言当前线程未处于读锁定状态（深度为零且未持有纪元）。 */
void rcu_assert_read_unlocked(void)
{
	struct rcu_thread *rt = rcu_self();
	assert(rt && !rt->depth && !seqlock_held(&rt->rcu));
}

/* RCU 资源回收线程部分。
 * rcu_main 是回收线程主循环；rcu_start 负责创建它；rcu_do 执行单个动作。 */
/*
 * RCU resource-release thread
 */

static void *rcu_main(void *arg);

/* rcu_start：启动 RCU 回收线程（仅一次）。启动前屏蔽本线程的各种信号，使新线程继承
 * 一个"什么信号都不处理"的掩码，避免回收线程在遍历 RCU 数据时被信号处理打扰。 */
static void rcu_start(void)
{
	/* ensure we never handle signals on the RCU thread by blocking
	 * everything here (new thread inherits signal mask)
	 */
	sigset_t oldsigs, blocksigs;

	/* technically, the RCU thread is very poorly suited to run even just a
	 * crashlog handler, since zlog_sigsafe() could deadlock on transiently
	 * invalid (due to RCU) logging data structures
	 *
	 * but given that when we try to write a crashlog, we're already in
	 * b0rked territory anyway - give the crashlog handler a chance.
	 *
	 * (also cf. the SIGALRM usage in writing crashlogs to avoid hung
	 * processes on any kind of deadlock in crash handlers)
	 */
	sigemptyset(&blocksigs);
	frr_sigset_add_mainonly(&blocksigs);
	/* new thread inherits mask */
	pthread_sigmask(SIG_BLOCK, &blocksigs, &oldsigs);

	rcu_active = true;

	assert(!pthread_create(&rcu_pthread, NULL, rcu_main, NULL));

	pthread_sigmask(SIG_SETMASK, &oldsigs, NULL);

#ifdef HAVE_PTHREAD_SETNAME_NP
# ifdef GNU_LINUX
	pthread_setname_np(rcu_pthread, "RCU sweeper");
# elif defined(__NetBSD__)
	pthread_setname_np(rcu_pthread, "RCU sweeper", NULL);
# endif
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
	pthread_set_name_np(rcu_pthread, "RCU sweeper");
#endif
}

/* rcu_do：执行单个 RCU 动作。按动作类型分派：FREE 时用 offset 反推对象地址后释放
 * （有 memtype 走 qfree，否则走裸 free）；CLOSE 时关闭 fd；CALL 时用 offset 反推对象
 * 地址并调用回调。内部类型（INVALID / NEXT / END）理论上不该到这里，命中即断言失败。 */
static void rcu_do(struct rcu_head *rh)
{
	struct rcu_head_close *rhc;
	void *p;

	switch (rh->action->type) {
	case RCUA_FREE:
		p = (char *)rh - rh->action->u.free.offset;
		if (rh->action->u.free.mt)
			qfree(rh->action->u.free.mt, p);
		else
			free(p);
		break;
	case RCUA_CLOSE:
		rhc = container_of(rh, struct rcu_head_close,
				   rcu_head);
		close(rhc->fd);
		break;
	case RCUA_CALL:
		p = (char *)rh - rh->action->u.call.offset;
		rh->action->u.call.fptr(p);
		break;

	case RCUA_INVALID:
	case RCUA_NEXT:
	case RCUA_END:
	default:
		assert(0);
	}
}

/* rcu_watchdog：看门狗钩子。当前为占位实现（#if 0），未来计划打印"拖住 RCU 的线程"
 * 的回溯栈。 */
static void rcu_watchdog(struct rcu_thread *rt)
{
#if 0
	/* future work: print a backtrace for the thread that's holding up
	 * RCU.  The only (good) way of doing that is to send a signal to the
	 * other thread, save away the backtrace in the signal handler, and
	 * block here until the signal is done processing.
	 *
	 * Just haven't implemented that yet.
	 */
	fprintf(stderr, "RCU watchdog %p\n", rt);
#endif
}

/* rcu_main：RCU 回收线程主循环。每轮先等待 rcu_seq 推进，再逐一等待每个线程离开
 * 当前纪元（带超时，超时则调用看门狗）；随后从 rcu_heads 弹出动作并执行，遇到
 * RCUA_NEXT 表示本轮纪元结束就停下，遇到 RCUA_END 则整体结束。 */
static void *rcu_main(void *arg)
{
	struct rcu_thread *rt;
	struct rcu_head *rh = NULL;
	bool end = false;
	struct timespec maxwait;

	seqlock_val_t rcuval = SEQLOCK_STARTVAL;

	pthread_setspecific(rcu_thread_key, &rcu_thread_rcu);

	while (!end) {
		seqlock_wait(&rcu_seq, rcuval);

		/* RCU watchdog timeout, TODO: configurable value */
		clock_gettime(CLOCK_MONOTONIC, &maxwait);
		maxwait.tv_nsec += 100 * 1000 * 1000;
		if (maxwait.tv_nsec >= 1000000000) {
			maxwait.tv_sec++;
			maxwait.tv_nsec -= 1000000000;
		}

		frr_each (rcu_threads, &rcu_threads, rt)
			if (!seqlock_timedwait(&rt->rcu, rcuval, &maxwait)) {
				rcu_watchdog(rt);
				seqlock_wait(&rt->rcu, rcuval);
			}

		while ((rh = rcu_heads_pop(&rcu_heads))) {
			if (rh->action->type == RCUA_NEXT)
				break;
			else if (rh->action->type == RCUA_END)
				end = true;
			else
				rcu_do(rh);
		}

		rcuval += SEQLOCK_INCR;
	}

	/* rcu_shutdown can only be called singlethreaded, and it does a
	 * pthread_join, so it should be impossible that anything ended up
	 * on the queue after RCUA_END
	 */
#if 1
	assert(!rcu_heads_first(&rcu_heads));
#else
	while ((rh = rcu_heads_pop(&rcu_heads)))
		if (rh->action->type >= RCUA_FREE)
			rcu_do(rh);
#endif
	return NULL;
}

/* rcu_shutdown：全局关闭 RCU，必须在单线程状态下调用。向队列塞入 RCUA_END 结束动作，
 * 释放本线程纪元，然后 join 回收线程；join 成功后把纪元重置回起始值，以便后续复用。 */
void rcu_shutdown(void)
{
	static struct rcu_head rcu_head_end;
	struct rcu_thread *rt = rcu_self();
	void *retval;

	if (!rcu_active)
		return;

	rcu_assert_read_locked();
	assert(rcu_threads_count(&rcu_threads) == 1);

	rcu_enqueue(&rcu_head_end, &rcua_end);

	rt->depth = 0;
	seqlock_release(&rt->rcu);
	seqlock_release(&rcu_seq);
	rcu_active = false;

	/* clearing rcu_active is before pthread_join in case we hang in
	 * pthread_join & get a SIGTERM or something - in that case, just
	 * ignore the maybe-still-running RCU thread
	 */
	if (pthread_join(rcu_pthread, &retval) == 0) {
		seqlock_acquire_val(&rcu_seq, SEQLOCK_STARTVAL);
		seqlock_acquire_val(&rt->rcu, SEQLOCK_STARTVAL);
		rt->depth = 1;
	}
}

/* RCU 的释放相关对外函数：rcu_enqueue 把动作入队；rcu_close 延迟关闭 fd。 */
/*
 * RCU'd free functions
 */

/* rcu_enqueue：把一个 RCU 动作挂入清理队列。要求持有读锁。若 RCU 尚未启动（单线程
 * 阶段），则同步直接执行该动作；否则入队并更新 rcu_dirty 以触发下一次 bump。 */
void rcu_enqueue(struct rcu_head *rh, const struct rcu_action *action)
{
	/* refer to rcu_bump() for why we need to hold RCU when adding items
	 * to rcu_heads
	 */
	rcu_assert_read_locked();

	rh->action = action;

	if (!rcu_active) {
		rcu_do(rh);
		return;
	}
	rcu_heads_add_tail(&rcu_heads, rh);
	atomic_store_explicit(&rcu_dirty, seqlock_cur(&rcu_seq),
			      memory_order_relaxed);
}

/* rcu_close：延迟关闭文件描述符。记录 fd 后以 RCUA_CLOSE 动作入队，宽限期结束后
 * 由回收线程真正执行 close()。 */
void rcu_close(struct rcu_head_close *rhc, int fd)
{
	rhc->fd = fd;
	rcu_enqueue(&rhc->rcu_head, &rcua_close);
}
