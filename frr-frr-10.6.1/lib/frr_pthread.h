// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Utilities and interfaces for managing POSIX threads within FRR.
 * Copyright (C) 2017  Cumulus Networks, Inc.
 */

/* ==================== 文件导读 ====================
 * 本头文件声明 FRR 的 POSIX 线程管理接口（frr_pthread）。FRR 采用“每线程
 * 一个事件循环”的模型：主线程与每个工作线程各自拥有一个 event_loop，线程
 * 之间通过各自 master 投递事件来通信，从而避免共享数据竞争。
 *
 * 核心对象：
 *   - struct frr_pthread：把一个 POSIX 线程、它的 RCU 注册信息、它专属的
 *     event_loop（master）以及启动 / 就绪同步用的条件变量打包在一起。
 *   - struct frr_pthread_attr：创建线程时可自定义的 start / stop 回调。
 *
 * 典型用法：
 *   1. frr_pthread_init() 初始化模块；
 *   2. frr_pthread_new() 创建对象（含默认事件循环）；
 *   3. frr_pthread_run() 真正启动底层 pthread；
 *   4. 用 frr_pthread_wait_running / frr_pthread_notify_running 做启动握手；
 *   5. 通过 fpt->master 向该线程投递事件；
 *   6. frr_pthread_stop() / frr_pthread_destroy() 收尾。
 *
 * 文件后半部分是互斥锁自动加解锁宏（frr_with_mutex 等），利用 cleanup 属性
 * 在作用域结束时自动解锁，防止忘记释放锁。
 * ================================================ */

#ifndef _FRR_PTHREAD_H
#define _FRR_PTHREAD_H

#include <pthread.h>
#include "frratomic.h"
#include "memory.h"
#include "frrcu.h"
#include "frrevent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 写入操作系统的线程名缓冲区最大长度（含结尾符 \0），共 16 字节。 */
#define OS_THREAD_NAMELEN 16

/* 前向声明：frr_pthread 与 frr_pthread_attr 的完整定义见下。 */
struct frr_pthread;
struct frr_pthread_attr;

/* 创建 / 销毁 frr_pthread 时可自定义的属性回调：
 *   start：线程入口函数（返回值作为线程结果）
 *   stop：请求线程停止，可把结果回填到 result
 */
struct frr_pthread_attr {
	void *(*start)(void *);
	int (*stop)(struct frr_pthread *, void **);
};

/* frr_pthread 描述一个 FRR 管理的 POSIX 线程：它把 pthread、rcu 线程、
 * 该线程专属的 event_loop（master）以及同步用的条件变量打包在一起。
 * 字段要点：
 *   mtx：保护本结构的互斥锁（部分字段需持锁访问）。
 *   thread：底层 pthread 标识。
 *   rcu_thread：该线程的 RCU 注册信息。
 *   master：本线程运行的事件循环；向它投递任务即在该线程执行。
 *   attr：调用方指定的 start / stop 等属性。
 *   startup_cond / startup_cond_mtx / started：新线程启动早期的串行化，
 *     避免与启动方线程竞争。
 *   running_cond / running_cond_mtx / running：就绪通知，配合
 *     frr_pthread_wait_running 与 frr_pthread_notify_running 使用。
 *   data：线程私有数据（可用于传参）。
 *   name：人类可读线程名；os_name：写入操作系统的线程名（含结尾符 16 字节）。
 */
struct frr_pthread {

	/*
	 * Mutex protecting this structure. Must be taken for reading some
	 * fields, denoted by a 'Requires: mtx'.
	 */
	pthread_mutex_t mtx;

	/* pthread id */
	pthread_t thread;

	struct rcu_thread *rcu_thread;

	/* thread master for this pthread's thread.c event loop */
	struct event_loop *master;

	/* caller-specified data; start & stop funcs, name, id */
	struct frr_pthread_attr attr;

	/*
	 * Startup serialization: newly-started pthreads wait at a point
	 * very early in life so that there isn't a race with the
	 * starting pthread. The OS 'start' apis don't make any guarantees
	 * about which pthread runs first - the existing pthread that has
	 * called the 'start' api, or the new pthread that is just starting.
	 */
	pthread_cond_t startup_cond;
	pthread_mutex_t startup_cond_mtx;
	atomic_bool started;

	/*
	 * Notification mechanism for allowing pthreads to notify their parents
	 * when they are ready to do work. This mechanism has two associated
	 * functions:
	 *
	 * - frr_pthread_wait_running()
	 *   This function should be called by the spawning thread after
	 *   frr_pthread_run(). It safely waits until the spawned thread
	 *   indicates that is ready to do work by posting to the condition
	 *   variable.
	 *
	 * - frr_pthread_notify_running()
	 *   This function should be called by the spawned thread when it is
	 *   ready to do work. It will wake up any threads waiting on the
	 *   previously described condition.
	 */
	pthread_cond_t *running_cond;
	pthread_mutex_t *running_cond_mtx;
	atomic_bool running;

	/*
	 * Fake thread-specific storage. No constraints on usage. Helpful when
	 * creating reentrant pthread implementations. Can be used to pass
	 * argument to pthread entry function.
	 *
	 * Requires: mtx
	 */
	void *data;

	/*
	 * Human-readable thread name.
	 *
	 * Requires: mtx
	 */
	char *name;

	/* Used in pthread_set_name max 16 characters */
	char os_name[OS_THREAD_NAMELEN];
};

/* frr_pthread_attr_default 是默认属性（默认 start / stop 为 NULL）。 */
extern const struct frr_pthread_attr frr_pthread_attr_default;

/* 初始化 frr_pthread 模块，使用其它任何函数前必须先调用。 */

/*
 * Initializes this module.
 *
 * Must be called before using any of the other functions.
 */
void frr_pthread_init(void);

/* 反初始化本模块：销毁所有已注册的 frr_pthread 及内部数据结构。之后可再次
 * 调用 frr_pthread_init 重新初始化。
 */

/*
 * Uninitializes this module.
 *
 * Destroys all registered frr_pthread's and internal data structures.
 *
 * It is safe to call frr_pthread_init() after this function to reinitialize
 * the module.
 */
void frr_pthread_finish(void);

/* 按给定属性创建一个 frr_pthread。attr 为 NULL 时用默认属性（运行一个基础
 * 事件循环，名字为 "Anonymous"）。成功返回对象，失败返回 NULL。
 *   attr：线程属性；name：人类可读名；os_name：写入系统的线程名（含结尾符）。
 */

/*
 * Creates a new frr_pthread with the given attributes.
 *
 * The 'attr' argument should be filled out with the desired attributes,
 * including ID, start and stop functions and the desired name. Alternatively,
 * if attr is NULL, the default attributes will be used. The pthread will be
 * set up to run a basic threadmaster loop and the name will be "Anonymous".
 * Scheduling tasks onto the threadmaster in the 'master' field of the returned
 * frr_pthread will cause them to run on that pthread.
 *
 * @param attr - the thread attributes
 * @param name - Human-readable name
 * @param os_name - 16 characters (including '\0') thread name to set in os,
 * @return the created frr_pthread upon success, or NULL upon failure
 */
struct frr_pthread *frr_pthread_new(const struct frr_pthread_attr *attr,
				    const char *name, const char *os_name);

/* 按 fpt->os_name 修改操作系统层面的线程名。成功返回 0，否则返回非零错误码。 */

/*
 * Changes the name of the frr_pthread as reported by the operating
 * system.
 *
 * @param fpt - the frr_pthread to operate on
 * @return -  on success returns 0 otherwise nonzero error number.
 */
int frr_pthread_set_name(struct frr_pthread *fpt);

/* 销毁一个 frr_pthread。调用前必须确保其关联的 pthread 已终止。 */

/*
 * Destroys an frr_pthread.
 *
 * Assumes that the associated pthread, if any, has already terminated.
 *
 * @param fpt - the frr_pthread to destroy
 */
void frr_pthread_destroy(struct frr_pthread *fpt);

/* 创建并绑定一个 POSIX 线程到该 frr_pthread，是 pthread_create 的封装。
 * fpt 会作为线程入口参数；若需附加数据可放到 fpt->data。
 * 返回值含义同 pthread_create：0 表示线程已成功运行；小于 0（非零）表示失败，
 * 此时 fpt 仍是干净实例，可再次调用 frr_pthread_run。
 */

/*
 * Creates a new pthread and binds it to a frr_pthread.
 *
 * This function is a wrapper for pthread_create. The first parameter is the
 * frr_pthread to bind the created pthread to. All subsequent arguments are
 * passed unmodified to pthread_create(). The frr_pthread * provided will be
 * used as the argument to the pthread entry function. If it is necessary to
 * pass additional data, the 'data' field in the frr_pthread may be used.
 *
 * This function returns the same code as pthread_create(). If the value is
 * zero, the provided frr_pthread is bound to a running POSIX thread. If the
 * value is less than zero, the provided frr_pthread is guaranteed to be a
 * clean instance that may be susbsequently passed to frr_pthread_run().
 *
 * @param fpt - frr_pthread * to run
 * @param attr - see pthread_create(3)
 *
 * @return see pthread_create(3)
 */
int frr_pthread_run(struct frr_pthread *fpt, const pthread_attr_t *attr);

/* 等待目标线程完成初始化、进入就绪状态（阻塞在 running 条件变量上）。线程
 * 代码若使用启动同步机制，调用方在使用该线程功能前应先调用本函数。
 */

/*
 * Waits until the specified pthread has finished setting up and is ready to
 * begin work.
 *
 * If the pthread's code makes use of the startup synchronization mechanism,
 * this function should be called before attempting to use the functionality
 * exposed by the pthread. It waits until the 'running' condition is satisfied
 * (see struct definition of frr_pthread).
 *
 * @param fpt - the frr_pthread * to wait on
 */
void frr_pthread_wait_running(struct frr_pthread *fpt);

/* 通知其它线程：本线程已完成初始化、可以开始工作；会唤醒阻塞在
 * frr_pthread_wait_running 中的线程。
 */

/*
 * Notifies other pthreads that the calling thread has finished setting up and
 * is ready to begin work.
 *
 * This will allow any other pthreads waiting in 'frr_pthread_wait_running' to
 * proceed.
 *
 * @param fpt - the frr_pthread * that has finished setting up
 */
void frr_pthread_notify_running(struct frr_pthread *fpt);

/* 请求停止一个 frr_pthread，并把线程结果写入 *result（不需结果可传 NULL）。 */

/*
 * Stops a frr_pthread with a result.
 *
 * @param fpt - frr_pthread * to stop
 * @param result - where to store the thread's result, if any. May be NULL if a
 * result is not needed.
 */
int frr_pthread_stop(struct frr_pthread *fpt, void **result);

/* 停止所有已注册的 frr_pthread。 */
/* Stops all frr_pthread's. */
void frr_pthread_stop_all(void);

/* 在没有 frr_pthread 对象的情况下，为一个“非受控”启动的线程登记名字，
 * 使其 rcu / name 机制仍能正常工作。返回 0 表示成功。
 */
int frr_pthread_non_controlled_startup(pthread_t thread, const char *name,
				       const char *os_name);

/* 互斥锁自动加锁 / 解锁工具：利用 cleanup 属性在作用域结束时自动解锁，
 * 避免忘了解锁。下面提供两种写法。
 */
/* mutex auto-lock/unlock */

/* 写法一（适合短代码块，可同时锁多个互斥量；可用 break / return 提前退出
 * 该块）：frr_with_mutex(&mtx, &mtx2) { ... }
 * 相关宏：_frr_with_mutex 负责加锁并注册 cleanup 解锁；_frr_with_mutex_once
 * 与 frr_with_mutex 构造一个只执行一次的作用域。
 */
/* variant 1:
 * (for short blocks, multiple mutexes supported)
 * break & return can be used for aborting the block
 *
 * frr_with_mutex(&mtx, &mtx2) {
 *    if (error)
 *       break;
 *    ...
 * }
 */
#define _frr_with_mutex(mutex)                                                 \
	*NAMECTR(_mtx_) __attribute__((                                        \
		unused, cleanup(_frr_mtx_unlock))) = _frr_mtx_lock(mutex),     \
	/* end */

#define _frr_with_mutex_once(_once, ...)                                                           \
	for (pthread_mutex_t MACRO_REPEAT(_frr_with_mutex, ##__VA_ARGS__)*_once = NULL;           \
	     _once == NULL; _once = (void *)1)                                                     \
	/* end */

/* 对外的多互斥量自动加解锁宏，展开为 _frr_with_mutex_once。 */
#define frr_with_mutex(...) _frr_with_mutex_once(NAMECTR(_once_), __VA_ARGS__)

/* 写法二（适合长代码块，不增加额外缩进）：frr_mutex_lock_autounlock(&mtx);
 * 之后作用域结束时自动解锁。frr_mutex_lock_autounlock 宏声明一个带 cleanup
 * 属性的互斥量指针。
 */
/* variant 2:
 * (more suitable for long blocks, no extra indentation)
 *
 * frr_mutex_lock_autounlock(&mtx);
 * ...
 */
#define frr_mutex_lock_autounlock(mutex)                                       \
	pthread_mutex_t *NAMECTR(_mtx_)                                        \
		__attribute__((unused, cleanup(_frr_mtx_unlock))) =            \
				    _frr_mtx_lock(mutex)                       \
	/* end */

/* 加锁并返回互斥量指针（供 cleanup 回调随后解锁用）。 */
static inline pthread_mutex_t *_frr_mtx_lock(pthread_mutex_t *mutex)
{
	pthread_mutex_lock(mutex);
	return mutex;
}

/* cleanup 回调：解锁并置空指针（指针已空则直接返回）。 */
static inline void _frr_mtx_unlock(pthread_mutex_t **mutex)
{
	if (!*mutex)
		return;
	pthread_mutex_unlock(*mutex);
	*mutex = NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* _FRR_PTHREAD_H */
