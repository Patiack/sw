// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2017-19  David Lamparter, for NetDEF, Inc.
 */

/* 【文件导读】frrcu.h —— FRR 用户态 RCU（Read-Copy-Update）读侧 / 写侧 API
 *
 * 职责：
 *   本头文件定义 FRR 在用户态自行实现的一套轻量 RCU 接口。核心思想是"延迟释放"：
 *   当某块资源（内存对象、文件描述符）要被"删除"时并不立即 free，而是把它挂到一
 *   条待清理队列上；等所有可能仍持有旧指针的读者线程都退出临界区（即宽限期结束）
 *   之后才真正释放，从而避免读者访问到已经被回收的内存（use-after-free）。
 *
 * 核心数据结构：
 *   - struct rcu_thread：每线程的 RCU 状态（不透明，在 frrcu.c 中定义）。
 *   - enum rcu_action_type：RCU 延迟动作的类型枚举。
 *   - struct rcu_action：描述一个延迟动作（释放哪块内存、或调用哪个函数）。
 *   - struct rcu_head：内嵌进目标结构体的"挂链节点"，把对象接入清理队列。
 *   - struct rcu_head_close：内嵌 rcu_head，用于延迟关闭文件描述符。
 *
 * 与其他模块的关系：
 *   - 依赖 atomlist.h 提供的原子链表（可无锁地读写链表指针）。
 *   - 依赖 memory.h 的 memtype 机制来做带类型统计的释放。
 *   - 被 lib 内大量数据结构（哈希表、路由表等）使用，以安全地进行无锁读。
 *
 * 阅读顺序：
 *   1) 先读文件顶部英文注释里的 RCU 简介（quick RCU primer）。
 *   2) 再看 enum 与 struct 定义，理解数据模型。
 *   3) 重点看 rcu_free 与 rcu_call 两个宏，它们是最常用的对外接口。
 *   4) 最后配合 frrcu.c 理解读侧 / 写侧函数与宽限期推进机制。
 *
 * 易踩的坑：
 *   - rcu_read_lock() 只是把当前线程标记为"持有当前序号"，可嵌套计数。
 *   - 即便处于 rcu_read_lock() 内，也不能访问已经 rcu_free() 的对象（这是设计
 *     取舍：单线程运行时可以把整套 RCU 退化成空操作）。
 *   - 没有内核式的 rcu_synchronize()；真需要异步回调请用 rcu_call()，但它运行在
 *     并会阻塞 RCU 线程，务必少用。
 *   - 没有 rcu_dereference() / rcu_assign_pointer()，请改用 atomic_ 系列或 ATOM 链表。
 */

#ifndef _FRRCU_H
#define _FRRCU_H

#include <assert.h>

#include "memory.h"
#include "atomlist.h"

/* 以 C 链接方式导出接口：本文件可能被 C++ 代码包含，extern "C" 防止名字改编。 */
#ifdef __cplusplus
extern "C" {
#endif

/* 下面是英文的 RCU 速览：存在一个全局序号计数器；每次线程执行 rcu_read_lock()
 * 就被记为持有当前序号；当资源被 RCU 回收时，全局序号递增并把该条目入队，等到
 * 所有线程的序号都更新到更晚的值（或不再持有序号）之后才真正执行清理。
 * 因此延迟清理保证了对象不会在别的线程仍可能持有（过期的）引用时消失。 */

/* quick RCU primer:
 *   There's a global sequence counter.  Whenever a thread does a
 *   rcu_read_lock(), it is marked as holding the current sequence counter.
 *   When something is cleaned with RCU, the global sequence counter is
 *   increased and the item is queued for cleanup - *after* all threads are
 *   at a more recent sequence counter (or no sequence counter / unheld).
 *
 *   So, by delaying resource cleanup, RCU ensures that things don't go away
 *   while another thread may hold a (stale) reference.
 *
 *   Note that even if a thread is in rcu_read_lock(), it is invalid for that
 *   thread to access bits after rcu_free() & co on them.  This is a design
 *   choice to allow no-op'ing out the entire RCU mechanism if we're running
 *   singlethreaded.  (Also allows some optimization on the counter bumping.)
 *
 * differences from Linux Kernel RCU:
 *   - there's no rcu_synchronize(), if you really need to defer something
 *     use rcu_call() (and double check it's really necessary)
 *   - rcu_dereference() and rcu_assign_pointer() don't exist, use atomic_*
 *     instead (ATOM* list structures do the right thing)
 */

/* struct rcu_thread：不透明（opaque）结构体，表示一个线程的 RCU 状态。
 * 具体字段只在 frrcu.c 中定义，外部只能通过指针传递，不能直接访问内部成员。 */
/* opaque */
struct rcu_thread;

/* rcu_thread_new：为当前线程创建并初始化 RCU 线程信息。
 * arg 一般是该线程要运行的入口参数；返回值必须交给该线程的 rcu_thread_start()。 */
/* sets up rcu thread info
 *
 * return value must be passed into the thread's call to rcu_thread_start()
 */
extern struct rcu_thread *rcu_thread_new(void *arg);

/* rcu_thread_prepare：在创建新线程之前调用，预先为新线程准备 RCU 线程信息，
 * 保证线程启动阶段可能持有的 RCU 引用不会因资源被回收而失效。
 * 返回值同样要传给新线程的 rcu_thread_start()。 */
/* called before new thread creation, sets up rcu thread info for new thread
 * before it actually exits.  This ensures possible RCU references are held
 * for thread startup.
 *
 * return value must be passed into the new thread's call to rcu_thread_start()
 */
extern struct rcu_thread *rcu_thread_prepare(void);

/* rcu_thread_unprepare：当 pthread_create() 失败时的清理函数，
 * 用于回收 rcu_thread_prepare() 预先分配的资源。 */
/* cleanup in case pthread_create() fails */
extern void rcu_thread_unprepare(struct rcu_thread *rcu_thread);

/* rcu_thread_start：在新线程内部尽早调用，入参为上面 prepare / new 的返回值。
 * 注意：新线程初始即处于 RCU 持有状态（嵌套深度为 1）。 */
/* called early in the new thread, with the return value from the above.
 * NB: new thread is initially in RCU-held state! (at depth 1)
 *
 * TBD: maybe inherit RCU state from rcu_thread_prepare()?
 */
extern void rcu_thread_start(struct rcu_thread *rcu_thread);

/* thread exit is handled through pthread_key_create's destructor function */

/* rcu_shutdown：全局 RCU 关闭，必须在只剩 1 个活动线程时调用。
 * 它会等待尚未完成的 RCU 动作做完，并等待 RCU 线程退出，主要用于干净退出、避免内存泄漏。 */
/* global RCU shutdown - must be called with only 1 active thread left.  waits
 * until remaining RCU actions are done & RCU thread has exited.
 *
 * This is mostly here to get a clean exit without memleaks.
 */
extern void rcu_shutdown(void);

/* rcu_read_lock / rcu_read_unlock：进入 / 退出 RCU 读侧临界区。
 * 采用计数器实现，因此可以嵌套调用，且必须成对出现。 */
/* enter / exit RCU-held state.  counter-based, so can be called nested. */
extern void rcu_read_lock(void);
extern void rcu_read_unlock(void);

/* rcu_assert_read_locked / rcu_assert_read_unlocked：调试与安全检查断言，
 * 分别断言"当前处于读侧锁定状态"和"当前未处于读侧锁定状态"。 */
/* for debugging / safety checks */
extern void rcu_assert_read_locked(void);
extern void rcu_assert_read_unlocked(void);

/* enum rcu_action_type：RCU 延迟动作的类型。
 *   RCUA_INVALID 无效值（占位 / 初值）；
 *   RCUA_NEXT、RCUA_END 由 RCU 内部使用，不应出现在外部代码里；
 *   RCUA_FREE 释放对象内存；RCUA_CLOSE 关闭文件描述符；RCUA_CALL 调用回调函数。 */
enum rcu_action_type {
	RCUA_INVALID = 0,
	/* used internally by the RCU code, shouldn't ever show up outside */
	RCUA_NEXT,
	RCUA_END,
	/* normal RCU actions, for outside use */
	RCUA_FREE,
	RCUA_CLOSE,
	RCUA_CALL,
};

/* struct rcu_action：描述一个被延迟执行的 RCU 动作。
 * 因为 rcu_head 会内嵌到可能有大量副本的结构体中，所以 rcu_head 被刻意压缩到最小
 * （只有原子链表指针加一个指向本结构体的指针），真正的动作描述放在这里。
 * type 指明动作类型；联合体 u 复用存储：free 分支记录 memtype 与字段偏移量，
 * call 分支记录回调函数指针与字段偏移量。 */
/* since rcu_head is intended to be embedded into structs which may exist
 * with lots of copies, rcu_head is shrunk down to its absolute minimum -
 * the atomlist pointer + a pointer to this action struct.
 */
struct rcu_action {
	enum rcu_action_type type;

	union {
		struct {
			struct memtype *mt;
			ptrdiff_t offset;
		} free;

		struct {
			void (*fptr)(void *arg);
			ptrdiff_t offset;
		} call;
	} u;
};

/* RCU cleanup function queue item */
/* struct rcu_head：RCU 清理队列中的节点。
 * 通过 PREDECL_ATOMLIST 声明原子链表类型 rcu_heads；结构体里 head 是链表挂链节点，
 * action 指向对应的 rcu_action（描述"这个对象将来该怎么被回收"）。 */
PREDECL_ATOMLIST(rcu_heads);
struct rcu_head {
	struct rcu_heads_item head;
	const struct rcu_action *action;
};

/* struct rcu_head_close：用于"延迟关闭文件描述符"的特殊 RCU 头，
 * 在 rcu_head 基础上额外携带一个待关闭的 fd。 */
/* special RCU head for delayed fd-close */
struct rcu_head_close {
	struct rcu_head rcu_head;
	int fd;
};

/* rcu_enqueue：把一个 RCU 动作入队，真正挂上清理队列。
 * 一般不要直接调用，请使用下面的 rcu_free / rcu_call 宏来构造 rcu_action。 */
/* enqueue RCU action - use the macros below to get the rcu_action set up */
extern void rcu_enqueue(struct rcu_head *head, const struct rcu_action *action);

/* 下面是 RCU 的 free() 与文件 close() 语义说明：被释放的内存 / 被关闭的 fd
 * 会"立刻"对调用线程不可用，但对其他线程仍保持可用，直到它们也进入 RCU 已释放状态。 */
/* RCU free() and file close() operations.
 *
 * freed memory / closed fds become _immediately_ unavailable to the calling
 * thread, but will remain available for other threads until they have passed
 * into RCU-released state.
 */

/* rcu_free(mtype, ptr, field)：延迟释放 ptr 所指向对象的内存。
 * 通过 offsetof 记录 field 在对象中的偏移，回收时据此反推出对象地址；
 * mtype 传 NULL 时走非 MTYPE 的普通 free。ptr 为 NULL 时直接跳过。 */
/* may be called with NULL mt to do non-MTYPE free() */
#define rcu_free(mtype, ptr, field)                                            \
	do {                                                                   \
		typeof(ptr) _ptr = (ptr);                                      \
		if (!_ptr)                                                     \
			break;                                                 \
		struct rcu_head *_rcu_head = &_ptr->field;                     \
		static const struct rcu_action _rcu_action = {                 \
			.type = RCUA_FREE,                                     \
			.u.free = {                                            \
				.mt = mtype,                                   \
				.offset = offsetof(typeof(*_ptr), field),      \
			},                                                     \
		};                                                             \
		rcu_enqueue(_rcu_head, &_rcu_action);                          \
	} while (0)

/* rcu_call(func, ptr, field)：延迟调用函数 func(ptr)。
 * 该回调运行在（并会阻塞）RCU 线程上，请谨慎、少量使用。
 * 宏内的 _fptype 赋值是一次编译期类型检查，确保 func 的参数类型与 ptr 匹配。 */
/* use this sparingly, it runs on (and blocks) the RCU thread */
#define rcu_call(func, ptr, field)                                             \
	do {                                                                   \
		typeof(ptr) _ptr = (ptr);                                      \
		void (*_fptype)(typeof(ptr));                                  \
		struct rcu_head *_rcu_head = &_ptr->field;                     \
		static const struct rcu_action _rcu_action = {                 \
			.type = RCUA_CALL,                                     \
			.u.call = {                                            \
				.fptr = (void *)func,                          \
				.offset = offsetof(typeof(*_ptr), field),      \
			},                                                     \
		};                                                             \
		(void)(_fptype = func);                                        \
		rcu_enqueue(_rcu_head, &_rcu_action);                          \
	} while (0)

/* rcu_close：延迟关闭文件描述符 fd。被关闭的 fd 立即对当前线程不可用，
 * 但要等其他线程都离开 RCU 持有状态后，才真正在系统层完成 close。 */
extern void rcu_close(struct rcu_head_close *head, int fd);

/* 结束 C 链接导出：与文件上方的 extern "C" 成对出现。 */
#ifdef __cplusplus
}
#endif

/* 头文件保护结束：与文件开头的 #ifndef _FRRCU_H 配对。 */
#endif /* _FRRCU_H */
