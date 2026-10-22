// SPDX-License-Identifier: GPL-2.0-or-later
/* Event management routine header.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/* ==================== 文件导读 ====================
 * 本头文件声明 FRR 事件子系统（旧名 thread，现已改名 event）对外暴露的
 * 全部类型、宏与函数原型。事件子系统是 FRR 所有守护进程的心脏：每个守护
 * 进程都拥有一个（或多个）event_loop（事件主循环，旧名 thread_master），
 * 它负责把“定时器到期”“文件描述符可读 / 可写”“内核信号”“其它线程投递
 * 的事件”统一排队、按优先级调度，并逐一回调注册的函数。
 *
 * 关键概念：
 *   - event_loop / event_master：一个事件循环实例，内部持有定时器堆和
 *     fd 事件集合；主线程通常通过 event_master_create 创建。
 *   - struct event：一次被调度的“事件”，可能是定时器、可读 / 可写 fd
 *     监听，或一次性投递的事件；它带有发起点的 xref 便于调试追踪。
 *   - 定时器（EVENT_TIMER）与 fd 事件（EVENT_READ / EVENT_WRITE）在同一
 *     循环内竞争执行，定时器依靠最小堆按到期时间排序。
 *   - 线程与主循环：多线程场景下通过 frr_pthread 创建线程并绑定各自的
 *     event_loop，跨线程投递使用 event_cancel_async 等异步接口。
 *
 * 本文件里 event_add_* / event_execute 是宏而非函数：它们会自动生成一个
 * static 的 xref 记录调用点（文件 / 行号 / 函数名），再转发到 _event_*
 * 实体函数，因此实际入口看带下划线前缀的 _event_add_* 原型。
 * ================================================ */

#ifndef _ZEBRA_THREAD_H
#define _ZEBRA_THREAD_H

#if defined(USE_EPOLL) && defined(HAVE_EPOLL_PWAIT)
#define EPOLL_ENABLED 1
#else
#define EPOLL_ENABLED 0
#endif

#include <signal.h>
#include <zebra.h>
#include <pthread.h>
#include <poll.h>
#if EPOLL_ENABLED
#include <sys/epoll.h>
#endif
#include "monotime.h"
#include "frratomic.h"
#include "typesafe.h"
#include "xref.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 若一次事件回调消耗的 CPU 时间超过该微秒数（5 秒），则认为该回调“霸占”
 * 了事件循环，会记录告警（见 event_consumed_time）。用于排查卡顿。
 */
#define CONSUMED_TIME_CHECK 5000000

/* cputime_enabled：是否启用 cpu 时间统计；cputime_threshold 是记录 cpu
 * 告警的阈值（微秒）。
 */
extern bool cputime_enabled;
extern unsigned long cputime_threshold;
/* capturing wallclock time is always enabled since it is fast (reading
 * hardware TSC w/o syscalls)
 */
extern unsigned long walltime_threshold;

/* 事件回调的资源使用统计：cpu 为 CPU 时间，real 为墙钟时间。 */
struct rusage_t {
	struct timespec cpu;
	struct timeval real;
};
#define RUSAGE_T struct rusage_t

/* 取当前进程的资源使用量，填充到 RUSAGE_T 指针里（封装 getrusage）。 */
#define GETRUSAGE(X) event_getrusage(X)

/* 用 typesafe 框架声明事件链表与定时器堆两类容器（实体在 event.c 展开）。
 * event_list 串起同优先级的就绪 / 待处理事件；event_timer_list 是按到期
 * 时间排序的最小堆。
 */
PREDECL_LIST(event_list);
PREDECL_HEAP(event_timer_list);

/* 事件调度发起点记录：配合 xref 机制保存“是哪个函数、到哪个目的地、
 * 什么类型”的调度，用于调试和 cpu 统计归类。
 */
struct xref_eventsched {
	struct xref xref;

	const char *funcname;
	const char *dest;
	uint32_t event_type;
};

/* 以 funcname 为键的哈希容器，用于按回调函数聚合 cpu 使用统计。 */
PREDECL_HASH(cpu_records);

/* Event types. */
/* 事件类型枚举。注意 struct event 里同时保存 type 与 add_type：类型会随
 * 事件在系统中流转而变化（如定时器到期后转为 READY），add_type 保留创建
 * 时的原始类型。
 *   EVENT_READ  / EVENT_WRITE：fd 可读 / 可写监听
 *   EVENT_TIMER：定时器
 *   EVENT_EVENT：一次性投递的事件
 *   EVENT_READY：已到期、等待被执行的“就绪”事件
 *   EVENT_UNUSED：空闲（已从循环摘除）
 *   EVENT_EXECUTE：event_execute 直接同步执行的事件
 */
enum event_types {
	EVENT_READ,
	EVENT_WRITE,
	EVENT_TIMER,
	EVENT_EVENT,
	EVENT_READY,
	EVENT_UNUSED,
	EVENT_EXECUTE,
};

/* Event itself. */
/* struct event 是事件系统的基本单位。字段要点：
 *   type / add_type：当前类型 / 创建时类型（见 enum event_types 说明）。
 *   eventitem / timeritem：分别挂入 event_list 链表与定时器堆的节点。
 *   ref：外部引用指针（例如调用方保存的句柄），取消时会被清空。
 *   master：所属事件循环。
 *   func / arg：回调函数及其参数。
 *   u.val / u.fd / u.sands：第二参数，按类型解释为整数值 / fd / 剩余时间。
 *   real：事件创建或最后一次调度时的墙钟时间。
 *   hist：指向 cpu 统计记录的缓存指针。
 *   yield：本事件允许的让出时间（微秒）。
 *   tardy_threshold：迟到告警阈值（微秒），非定时器为 0。
 *   xref：发起点记录。
 *   mtx：保护本事件的互斥锁，供多线程访问使用。
 */
struct event {
	enum event_types type;	   /* event type as it moves through the event system*/
	enum event_types add_type; /* event type as it was created */
	struct event_list_item eventitem;
	struct event_timer_list_item timeritem;
	struct event **ref;	      /* external reference (if given) */
	struct event_loop *master;    /* pointer to the struct event_loop */
	void (*func)(struct event *e); /* event function */
	void *arg;		      /* event argument */
	union {
		int val;	      /* second argument of the event. */
		int fd;		      /* file descriptor in case of r/w */
		struct timeval sands; /* rest of time sands value. */
	} u;
	struct timeval real;
	struct cpu_event_history *hist;	    /* cache pointer to cpu_history */
	unsigned long yield;		    /* yield time in microseconds */
	/* lateness warning threshold, usec.  0 if it's not a timer. */
	unsigned long tardy_threshold;
	const struct xref_eventsched *xref; /* origin location */
	pthread_mutex_t mtx;		    /* mutex for thread.c functions */
};

/* 定时器迟到：实际执行时刻相较到期时刻晚了多少。为防刷屏，告警做了限速，
 * TARDY_WARNING_INTERVAL 是两次告警的最小间隔（10 秒）；TARDY_DEFAULT_THRESHOLD
 * 是默认迟到阈值（4 秒），超过即打印告警。
 */
/* rate limit late timer warnings */
#define TARDY_WARNING_INTERVAL 10 * TIMER_SECOND_MICRO
/* default threshold for late timer warning */
#define TARDY_DEFAULT_THRESHOLD 4 * TIMER_SECOND_MICRO

#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pTH"(struct event *)
#endif

/* 按回调函数聚合的 cpu / 墙钟使用统计。_clear_begin 与 _clear_end 之间的字段
 * 会被 "clear event cpu" 命令整体清零（利用零长数组成员标出区间）。funcname
 * 为该记录对应的回调函数名。
 */
struct cpu_event_history {
	struct cpu_records_item item;

	void (*func)(struct event *e);

	/* fields between the pair of these two are nulled on "clear event cpu" */
	char _clear_begin[0];

	atomic_size_t total_cpu_warn;
	atomic_size_t total_wall_warn;
	atomic_size_t total_starv_warn;
	atomic_size_t total_calls;
	atomic_size_t total_active;
	struct time_stats {
		atomic_size_t total, max;
	} real;
	struct time_stats cpu;
	atomic_uint_fast32_t types;

	/* end of cleared region */
	char _clear_end[0];

	const char *funcname;
};

/* Struct timeval's tv_usec one second value.  */
#define TIMER_SECOND_MICRO 1000000L

/* 返回 a 相对 b 经过的微秒数（a 较新时为正）。 */
static inline unsigned long timeval_elapsed(struct timeval a, struct timeval b)
{
	return (((a.tv_sec - b.tv_sec) * TIMER_SECOND_MICRO)
		+ (a.tv_usec - b.tv_usec));
}

/* 事件主动让出 CPU 的时间片粒度，默认 10ms。 */
/* Event yield time.  */
#define EVENT_YIELD_TIME_SLOT 10 * 1000L /* 10ms */

/* event_timer_to_hhmmss 输出缓冲区所需的最小长度。 */
#define EVENT_TIMER_STRLEN 12

/* Macros. */
/* 访问 struct event 各字段的便捷宏：参数 / fd / 第二参数值。 */
#define EVENT_ARG(X) ((X)->arg)
#define EVENT_FD(X) ((X)->u.fd)
#define EVENT_VAL(X) ((X)->u.val)

/*
 * Macro wrappers to generate xrefs for all thread add calls.  Includes
 * file/line/function info for debugging/tracing.
 */
#include "lib/xref.h"

/* _xref_t_a 是 event_add_* 宏的公共实现骨架：使用 GCC 语句表达式先声明一个
 * static 的 xref_eventsched 常量（记录函数名、目的地、事件类型），在编译期
 * 用 XREF_LINK 注册调用点，然后调用对应的 _event_add_<addfn> 实体函数。
 * 参数 m=master，f=回调，a=arg，v=值 / fd，t=外部引用。
 */
#define _xref_t_a(addfn, type, m, f, a, v, t)                                  \
	({                                                                     \
		static const struct xref_eventsched _xref __attribute__(       \
			(used)) = {                                            \
			.xref = XREF_INIT(XREFT_EVENTSCHED, NULL, __func__),   \
			.funcname = #f,                                        \
			.dest = #t,                                            \
			.event_type = EVENT_##type,                            \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		_event_add_##addfn(&_xref, m, f, a, v, t);                     \
	}) /* end */

/* 以下 event_add_* 宏分别注册 fd 读 / 写、定时器（秒 / 毫秒 / timeval）与
 * 一次性事件；均展开为 _xref_t_a 调用，自动生成 xref 后转发到 _event_add_*。
 */
#define event_add_read(m, f, a, v, t) _xref_t_a(read_write, READ, m, f, a, v, t)
#define event_add_write(m, f, a, v, t)                                         \
	_xref_t_a(read_write, WRITE, m, f, a, v, t)
#define event_add_timer(m, f, a, v, t) _xref_t_a(timer, TIMER, m, f, a, v, t)
#define event_add_timer_msec(m, f, a, v, t)                                    \
	_xref_t_a(timer_msec, TIMER, m, f, a, v, t)
#define event_add_timer_tv(m, f, a, v, t)                                      \
	_xref_t_a(timer_tv, TIMER, m, f, a, v, t)
#define event_add_event(m, f, a, v, t) _xref_t_a(event, EVENT, m, f, a, v, t)

/* event_execute 与 event_add_* 类似生成 xref，但调用 _event_execute 同步
 * 立即执行回调（而非排入循环等待）。参数 p 为事件内存存放位置。
 */

#define event_execute(m, f, a, v, p)                                           \
	({                                                                     \
		static const struct xref_eventsched _xref __attribute__(       \
			(used)) = {                                            \
			.xref = XREF_INIT(XREFT_EVENTSCHED, NULL, __func__),   \
			.funcname = #f,                                        \
			.dest = NULL,                                          \
			.event_type = EVENT_EXECUTE,                           \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		_event_execute(&_xref, m, f, a, v, p);                         \
	}) /* end */

/* Prototypes. */
/* 创建一个事件主循环（event_master）；失败返回 NULL。 */
extern struct event_loop *event_master_create(const char *name);
/* 修改事件循环的名字（用于日志 / cpu 统计显示）。 */
void event_master_set_name(struct event_loop *master, const char *name);
/* 销毁事件循环并释放其持有的所有事件。 */
extern void event_master_free(struct event_loop *m);

/* 以下 _event_add_* / _event_execute 是事件注册的实体函数，通常不直接
 * 调用，而由本头文件里的 event_add_* 宏封装（宏会自动带上 xref）。
 *   read_write：注册 fd 可读 / 可写事件
 *   timer / timer_msec / timer_tv：注册定时器（单位秒 / 毫秒 / timeval）
 *   event：投递一次性事件
 *   execute：同步立即执行一次回调
 */
extern void _event_add_read_write(const struct xref_eventsched *xref,
				  struct event_loop *master,
				  void (*fn)(struct event *), void *arg, int fd,
				  struct event **tref);

/* 注册一个定时器，t 为相对当前时间的秒数。 */
extern void _event_add_timer(const struct xref_eventsched *xref,
			     struct event_loop *master,
			     void (*fn)(struct event *), void *arg, long t,
			     struct event **tref);

/* 注册一个定时器，t 为相对当前时间的毫秒数。 */
extern void _event_add_timer_msec(const struct xref_eventsched *xref,
				  struct event_loop *master,
				  void (*fn)(struct event *), void *arg, long t,
				  struct event **tref);

/* 注册一个定时器，tv 指向相对当前的超时 timeval。 */
extern void _event_add_timer_tv(const struct xref_eventsched *xref,
				struct event_loop *master,
				void (*fn)(struct event *), void *arg,
				struct timeval *tv, struct event **tref);

/* 投递一次性事件，val 为回调可取的整数值。 */
extern void _event_add_event(const struct xref_eventsched *xref,
			     struct event_loop *master,
			     void (*fn)(struct event *), void *arg, int val,
			     struct event **tref);

/* 同步立即执行一次回调（不进入循环排队）。 */
extern void _event_execute(const struct xref_eventsched *xref,
			   struct event_loop *master,
			   void (*fn)(struct event *), void *arg, int val,
			   struct event **eref);

/* 取消一个事件（可传 NULL 或指向 NULL 的指针，安全）；清空 *event。 */
extern void event_cancel(struct event **event);
/* 从其它线程异步取消事件：实际取消在目标主循环线程内执行，避免数据竞争。 */
extern void event_cancel_async(struct event_loop *m, struct event **eptr,
			       void *data);
/* 取消就绪队列中所有 arg 匹配的事件。 */
/* Cancel ready tasks with an arg matching 'arg' */
extern void event_cancel_event_ready(struct event_loop *m, void *arg);
/* 取消所有 arg 匹配的事件，含定时器与 io。 */
/* Cancel all tasks with an arg matching 'arg', including timers and io */
extern void event_cancel_event(struct event_loop *m, void *arg);
/* 从主循环取回刚到期 / 就绪的事件。 */
extern struct event *event_fetch(struct event_loop *m, struct event *event);
/* 执行一个事件回调。 */
extern void event_call(struct event *event);
/* 定时器剩余时间：秒 / timeval / 毫秒。 */
extern unsigned long event_timer_remain_second(struct event *event);
extern struct timeval event_timer_remain(struct event *event);
extern unsigned long event_timer_remain_msec(struct event *event);
/* 判断事件是否已运行过久、应当让出 CPU（返回真表示应让出）。 */
extern int event_should_yield(struct event *event);
/* set yield time for thread */
/* 设置事件的让出时间（微秒）。 */
extern void event_set_yield_time(struct event *event, unsigned long ytime);

/* Internal libfrr exports */
/* 取资源使用量；初始化 "show event cpu" 等命令。 */
extern void event_getrusage(RUSAGE_T *r);
extern void event_cmd_init(void);

/* 计算两次采样之间的墙钟耗时（微秒），并通过 cpu_time_elapsed 返回 CPU 耗时。 */
/* Returns elapsed real (wall clock) time. */
extern unsigned long event_consumed_time(RUSAGE_T *after, RUSAGE_T *before,
					 unsigned long *cpu_time_elapsed);

/* 仅供日志函数使用！thread_current 为线程局部键，event_timer_to_hhmmss 把
 * 定时器剩余时间格式化成 hh:mm:ss 字符串到 buf。
 */
/* only for use in logging functions! */
extern pthread_key_t thread_current;
extern char *event_timer_to_hhmmss(char *buf, int buf_size,
				   struct event *t_timer);

/* 若事件句柄非空则视为仍在调度中（返回 true）。 */
static inline bool event_is_scheduled(struct event *event)
{
	if (event)
		return true;

	return false;
}

/* 打印信号掩码内容，调试用。 */
/* Debug signal mask */
void debug_signals(const sigset_t *sigs);

/* 设置定时器迟到告警阈值（微秒）。默认 4 秒，0 表示关闭。非定时器勿调用。 */
/* getting called more than given microseconds late will print a warning.
 * Default if not called: 4s.  Don't call this on non-timers.
 */
static inline void event_set_tardy_threshold(struct event *event, unsigned long thres)
{
	event->tardy_threshold = thres;
}

/* 关闭该定时器的迟到告警（阈值置 0）。 */
static inline void event_ignore_late_timer(struct event *event)
{
	event->tardy_threshold = 0;
}

/* Accessors for event loop pthread */
/* 读取 / 设置事件循环的归属线程（哪个 pthread 拥有并运行该循环）。 */
pthread_t frr_event_loop_get_pthread_owner(struct event_loop *loop);
void frr_event_loop_set_pthread_owner(struct event_loop *loop, pthread_t pth);

/* Control whether 'loop' is the signal-handler for a process */
/* 设置该事件循环是否负责处理进程信号。 */
void frr_event_loop_set_handle_sigs(struct event_loop *loop, bool handle_p);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_THREAD_H */
