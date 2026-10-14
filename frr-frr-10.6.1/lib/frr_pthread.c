// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Utilities and interfaces for managing POSIX threads within FRR.
 * Copyright (C) 2017  Cumulus Networks, Inc.
 */

/* ==================== 文件导读 ====================
 * 本文件实现 FRR 的 POSIX 线程管理（frr_pthread，接口见 frr_pthread.h）。
 * 设计要点：
 *   - FRR 采用“每线程一个事件循环”模型。frr_pthread_new 会为该线程创建
 *     一个独立的 event_loop（master），之后往该 master 投递的事件都在该
 *     线程执行，从而把并发限制在线程边界内，减少锁竞争。
 *   - 所有 frr_pthread 对象登记在全局链表 frr_pthread_list 中，用一个全局
 *     互斥量 frr_pthread_list_mtx 保护。
 *   - 线程启动时通过 startup_cond 做“父线程放行、子线程等待”的握手；
 *     启动后通过 running_cond 通知“已就绪可工作”。
 *   - rcu_thread_start 在子线程入口里注册 RCU 读侧，配合 FRR 的 RCU 机制。
 *   - 启动前会屏蔽继承来的信号，避免把信号递交给工作线程。
 * ================================================ */

#include <zebra.h>

#include <signal.h>

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <sched.h>

#include "frr_pthread.h"
#include "memory.h"
#include "linklist.h"
#include "zlog.h"
#include "libfrr.h"
#include "libfrr_trace.h"
#include "sigevent.h"

/* 为 frr_pthread 对象与线程同步原语（mutex / cond）分别登记内存类型，
 * 便于内存统计与排查。
 */
DEFINE_MTYPE_STATIC(LIB, FRR_PTHREAD, "FRR POSIX Thread");
DEFINE_MTYPE_STATIC(LIB, PTHREAD_PRIM, "POSIX sync primitives");

/* 默认的线程入口与停止例程（当属性未指定 start / stop 时使用）。 */
/* default frr_pthread start/stop routine prototypes */
static void *fpt_run(void *arg);
static int fpt_halt(struct frr_pthread *fpt, void **res);

/* misc sigs */
static void frr_pthread_destroy_nolock(struct frr_pthread *fpt);

/* 默认属性对象：入口为 fpt_run，停止为 fpt_halt。 */
/* default frr_pthread attributes */
const struct frr_pthread_attr frr_pthread_attr_default = {
	.start = fpt_run,
	.stop = fpt_halt,
};

/* 记录所有已创建的 frr_pthread，保护它的是 frr_pthread_list_mtx。 */
/* list to keep track of all frr_pthreads */
static pthread_mutex_t frr_pthread_list_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct list *frr_pthread_list;

/* ------------------------------------------------------------------------ */

/* 初始化模块：创建全局 frr_pthread 链表。必须在其它接口之前调用。 */
void frr_pthread_init(void)
{
	frr_with_mutex (&frr_pthread_list_mtx) {
		frr_pthread_list = list_new();
	}
}

/* 反初始化：先停止所有线程，再逐个从链表摘除并销毁，最后删除链表。之后
 * 可再次调用 frr_pthread_init 重新使用本模块。
 */
void frr_pthread_finish(void)
{
	frr_pthread_stop_all();

	frr_with_mutex (&frr_pthread_list_mtx) {
		struct listnode *n, *nn;
		struct frr_pthread *fpt;

		for (ALL_LIST_ELEMENTS(frr_pthread_list, n, nn, fpt)) {
			listnode_delete(frr_pthread_list, fpt);
			frr_pthread_destroy_nolock(fpt);
		}

		list_delete(&frr_pthread_list);
	}
}

/* 创建一个 frr_pthread：分配对象、初始化互斥量与条件变量、创建该线程专属
 * 的事件循环、复制属性与名字，并把对象登记进全局链表。
 *   attr 为 NULL 时用默认属性；name 为 NULL 时用 "Anonymous thread"；
 *   os_name 为 NULL 时沿用 name（两者都截断到 OS_THREAD_NAMELEN）。
 * 返回创建好的对象。注意此时线程尚未启动，需再调用 frr_pthread_run。
 */
struct frr_pthread *frr_pthread_new(const struct frr_pthread_attr *attr,
				    const char *name, const char *os_name)
{
	struct frr_pthread *fpt = NULL;

	attr = attr ? attr : &frr_pthread_attr_default;

	fpt = XCALLOC(MTYPE_FRR_PTHREAD, sizeof(struct frr_pthread));
	/* initialize mutex */
	pthread_mutex_init(&fpt->mtx, NULL);
	/* create new thread master */
	fpt->master = event_master_create(name);
	/* set attributes */
	fpt->attr = *attr;
	name = (name ? name : "Anonymous thread");
	fpt->name = XSTRDUP(MTYPE_FRR_PTHREAD, name);
	if (os_name)
		strlcpy(fpt->os_name, os_name, OS_THREAD_NAMELEN);
	else
		strlcpy(fpt->os_name, name, OS_THREAD_NAMELEN);
	/* initialize startup synchronization primitives */
	fpt->running_cond_mtx = XCALLOC(
		MTYPE_PTHREAD_PRIM, sizeof(pthread_mutex_t));
	fpt->running_cond = XCALLOC(MTYPE_PTHREAD_PRIM,
				    sizeof(pthread_cond_t));

	pthread_mutex_init(fpt->running_cond_mtx, NULL);
	pthread_cond_init(fpt->running_cond, NULL);

	pthread_mutex_init(&fpt->startup_cond_mtx, NULL);
	pthread_cond_init(&fpt->startup_cond, NULL);
	fpt->started = false;

	frr_with_mutex (&frr_pthread_list_mtx) {
		listnode_add(frr_pthread_list, fpt);
	}

	return fpt;
}

/* 真正释放一个 frr_pthread 的全部资源（不涉及全局链表的加锁），供持有
 * frr_pthread_list_mtx 的调用者使用。会销毁事件循环、各互斥量 / 条件变量
 * 并释放内存。调用前应确保线程已停止。
 */
static void frr_pthread_destroy_nolock(struct frr_pthread *fpt)
{
	event_master_free(fpt->master);
	pthread_mutex_destroy(&fpt->mtx);
	pthread_mutex_destroy(fpt->running_cond_mtx);
	pthread_cond_destroy(fpt->running_cond);
	pthread_mutex_destroy(&fpt->startup_cond_mtx);
	pthread_cond_destroy(&fpt->startup_cond);
	XFREE(MTYPE_FRR_PTHREAD, fpt->name);
	XFREE(MTYPE_PTHREAD_PRIM, fpt->running_cond_mtx);
	XFREE(MTYPE_PTHREAD_PRIM, fpt->running_cond);
	XFREE(MTYPE_FRR_PTHREAD, fpt);
}

/* 销毁一个 frr_pthread：先从全局链表摘除（加锁），再调用 nolock 版本释放
 * 资源。调用前必须确保其 pthread 已终止。
 */
void frr_pthread_destroy(struct frr_pthread *fpt)
{
	frr_with_mutex (&frr_pthread_list_mtx) {
		listnode_delete(frr_pthread_list, fpt);
	}

	frr_pthread_destroy_nolock(fpt);
}

/* 设置操作系统层面的线程名，按平台选用 pthread_setname_np 或
 * pthread_set_name_np。返回底层调用的错误码（0 为成功）。
 */
int frr_pthread_set_name(struct frr_pthread *fpt)
{
	int ret = 0;

#ifdef HAVE_PTHREAD_SETNAME_NP
# ifdef GNU_LINUX
	ret = pthread_setname_np(fpt->thread, fpt->os_name);
# elif defined(__NetBSD__)
	ret = pthread_setname_np(fpt->thread, fpt->os_name, NULL);
# endif
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
	pthread_set_name_np(fpt->thread, fpt->os_name);
#endif

	return ret;
}

/* 子线程入口处等待父线程放行：在 startup_cond 上循环等待 started 变为真。 */
/* New pthread waits before running */
static void frr_pthread_wait_startup(struct frr_pthread *fpt)
{
	frr_with_mutex (&fpt->startup_cond_mtx) {
		while (!fpt->started)
			pthread_cond_wait(&fpt->startup_cond,
					  &fpt->startup_cond_mtx);
	}
}

/* 父线程放行子线程：置 started 为真并 signal startup_cond。 */
/* Parent pthread allows new pthread to start running */
static void frr_pthread_notify_startup(struct frr_pthread *fpt)
{
	frr_with_mutex (&fpt->startup_cond_mtx) {
		fpt->started = true;
		pthread_cond_signal(&fpt->startup_cond);
	}
}

/* 底层线程入口：先等待父线程放行，再在当前线程注册 RCU 读侧，最后调用
 * 用户提供的 start 回调并返回其结果。
 */
static void *frr_pthread_inner(void *arg)
{
	struct frr_pthread *fpt = arg;

	/* The new pthead waits until the parent allows it to continue. */
	frr_pthread_wait_startup(fpt);

	rcu_thread_start(fpt->rcu_thread);

	return fpt->attr.start(fpt);
}

/* 启动底层 pthread 并绑定到该 frr_pthread。步骤：断言确保已在 fork 之后；
 * 先屏蔽主线程专用信号（子线程会继承掩码，从而不接收这些信号），调用
 * pthread_create，随后恢复调用者的信号掩码，再放行子线程（notify_startup）。
 * 若 pthread_create 失败（ret 小于 0），按 pthread_create(3) 语义 fpt->thread
 * 内容未定义，这里把它清零并撤销 rcu_thread。返回 pthread_create 的错误码。
 */
int frr_pthread_run(struct frr_pthread *fpt, const pthread_attr_t *attr)
{
	int ret;
	sigset_t oldsigs, blocksigs;

	assert(frr_is_after_fork || !"trying to start thread before fork()");

	sigemptyset(&blocksigs);
	frr_sigset_add_mainonly(&blocksigs);
	/* new thread inherits mask */
	pthread_sigmask(SIG_BLOCK, &blocksigs, &oldsigs);

	frrtrace(1, frr_libfrr, frr_pthread_run, fpt->name);

	fpt->rcu_thread = rcu_thread_prepare();
	ret = pthread_create(&fpt->thread, attr, frr_pthread_inner, fpt);

	/* Restore caller's signals */
	pthread_sigmask(SIG_SETMASK, &oldsigs, NULL);

	/* Allow new child pthread to start */
	frr_pthread_notify_startup(fpt);

	/*
	 * Per pthread_create(3), the contents of fpt->thread are undefined if
	 * pthread_create() did not succeed. Reset this value to zero.
	 */
	if (ret < 0) {
		rcu_thread_unprepare(fpt->rcu_thread);
		memset(&fpt->thread, 0x00, sizeof(fpt->thread));
	}

	return ret;
}

/* 阻塞等待目标线程就绪（running 变为真）。通常由创建方在 run 之后调用。 */
void frr_pthread_wait_running(struct frr_pthread *fpt)
{
	frr_with_mutex (fpt->running_cond_mtx) {
		while (!fpt->running)
			pthread_cond_wait(fpt->running_cond,
					  fpt->running_cond_mtx);
	}
}

/* 由子线程在完成初始化后调用：置 running 为真并 signal，唤醒等待者。 */
void frr_pthread_notify_running(struct frr_pthread *fpt)
{
	frr_with_mutex (fpt->running_cond_mtx) {
		fpt->running = true;
		pthread_cond_signal(fpt->running_cond);
	}
}

/* 请求停止线程：调用 attr.stop 回调（默认 fpt_halt），随后把 fpt->thread
 * 清零，返回回调的返回值。result 用于接收线程结果。
 */
int frr_pthread_stop(struct frr_pthread *fpt, void **result)
{
	frrtrace(1, frr_libfrr, frr_pthread_stop, fpt->name);

	int ret = (*fpt->attr.stop)(fpt, result);
	memset(&fpt->thread, 0x00, sizeof(fpt->thread));
	return ret;
}

/* 遍历全局链表，停止所有 running 为真的 frr_pthread（持全局锁调用 stop）。 */
void frr_pthread_stop_all(void)
{
	frr_with_mutex (&frr_pthread_list_mtx) {
		struct listnode *n;
		struct frr_pthread *fpt;
		for (ALL_LIST_ELEMENTS_RO(frr_pthread_list, n, fpt)) {
			if (atomic_load_explicit(&fpt->running,
						 memory_order_relaxed))
				frr_pthread_stop(fpt, NULL);
		}
	}
}

/* 非受控线程的“伪”入口：仅把 running 置真，不真正跑事件循环。 */
static void *frr_pthread_attr_non_controlled_start(void *arg)
{
	struct frr_pthread *fpt = arg;

	fpt->running = true;

	return NULL;
}

/* 为一个由外部库启动、并非 FRR 自建的 pthread 建立 frr_pthread 上下文，
 * 使其能接入 FRR 的日志 / RCU 机制。会新建 rcu_thread 并立即 start，用伪
 * 入口构造 fpt，随后直接在当前线程调用 frr_pthread_inner。成功返回 0，
 * 失败返回 -1。
 */
/* Create a FRR pthread context from a non FRR pthread initialized from an
 * external library in order to allow logging */
int frr_pthread_non_controlled_startup(pthread_t thread, const char *name,
				       const char *os_name)
{
	struct rcu_thread *rcu_thread = rcu_thread_new(NULL);

	rcu_thread_start(rcu_thread);

	struct frr_pthread_attr attr = {
		.start = frr_pthread_attr_non_controlled_start,
		.stop = frr_pthread_attr_default.stop,
	};
	struct frr_pthread *fpt;

	fpt = frr_pthread_new(&attr, name, os_name);
	if (!fpt)
		return -1;

	fpt->thread = thread;
	fpt->rcu_thread = rcu_thread;
	fpt->started = true;

	frr_pthread_inner(fpt);

	return 0;
}

/* 默认事件循环：当 frr_pthread 未自定义 start 时使用。线程入口 fpt_run 会
 * 运行一个基础事件循环，直到被 fpt_halt 投递的“毒丸”事件结束。
 */
/*
 * ----------------------------------------------------------------------------
 * Default Event Loop
 * ----------------------------------------------------------------------------
 */

/* 空任务：仅用于把 sleeper 管道的读端作为 io 源挂进事件循环，防止循环因
 * 无任何事件而空转 / 退出。
 */
/* dummy task for sleeper pipe */
static void fpt_dummy(struct event *event)
{
}

/* 毒丸任务：把 running 置假，使主循环条件不再成立从而退出。 */
/* poison pill task to end event loop */
static void fpt_finish(struct event *event)
{
	struct frr_pthread *fpt = EVENT_ARG(event);

	atomic_store_explicit(&fpt->running, false, memory_order_relaxed);
}

/* 默认停止函数：向目标线程的 master 投递毒丸事件，然后 pthread_join 等它
 * 退出，结果写入 res。由其它线程调用。
 */
/* stop function, called from other threads to halt this one */
static int fpt_halt(struct frr_pthread *fpt, void **res)
{
	event_add_event(fpt->master, &fpt_finish, fpt, 0, NULL);
	pthread_join(fpt->thread, res);

	return 0;
}

/* 默认线程入口兼主事件循环。启动时：把 master 的归属线程设为当前 pthread；
 * 初始化线程局部日志缓冲；创建 sleeper 管道并作为读事件挂入 master（防止
 * poll 无源可等）；设置本循环不处理信号；设置 OS 线程名；通知 running。
 * 随后循环：只要 running 为真就 pthread_testcancel 并取出 / 执行一个事件。
 * 退出后关闭管道、清理线程局部日志缓冲。注意：循环退出不执行任何清理回调。
 */
/*
 * Entry pthread function & main event loop.
 *
 * Upon thread start the following actions occur:
 *
 * - frr_pthread's owner field is set to pthread ID.
 * - All signals are blocked (except for unblockable signals).
 * - Pthread's threadmaster is set to never handle pending signals
 * - Poker pipe for poll() is created and queued as I/O source
 * - The frr_pthread->running_cond condition variable is signalled to indicate
 *   that the previous actions have completed. It is not safe to assume any of
 *   the above have occurred before receiving this signal.
 *
 * After initialization is completed, the event loop begins running. Each tick,
 * the following actions are performed before running the usual event system
 * tick function:
 *
 * - Verify that the running boolean is set
 * - Verify that there are no pending cancellation requests
 * - Verify that there are tasks scheduled
 *
 * So long as the conditions are met, the event loop tick is run and the
 * returned task is executed.
 *
 * If any of these conditions are not met, the event loop exits, closes the
 * pipes and dies without running any cleanup functions.
 */
static void *fpt_run(void *arg)
{
	struct frr_pthread *fpt = arg;
	frr_event_loop_set_pthread_owner(fpt->master, pthread_self());

	zlog_tls_buffer_init();

	int sleeper[2];
	pipe(sleeper);
	event_add_read(fpt->master, &fpt_dummy, NULL, sleeper[0], NULL);

	frr_event_loop_set_handle_sigs(fpt->master, false);

	frr_pthread_set_name(fpt);

	frr_pthread_notify_running(fpt);

	struct event task;
	while (atomic_load_explicit(&fpt->running, memory_order_relaxed)) {
		pthread_testcancel();
		if (event_fetch(fpt->master, &task)) {
			event_call(&task);
		}
	}

	close(sleeper[1]);
	close(sleeper[0]);

	zlog_tls_buffer_fini();

	return NULL;
}
