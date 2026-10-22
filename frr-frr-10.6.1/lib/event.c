// SPDX-License-Identifier: GPL-2.0-or-later
/* Thread management routine
 * Copyright (C) 1998, 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

/* 取消下面这行的注释即可打开事件子系统的编译期调试输出。 */
/* #define DEBUG */

/* ==================== 文件导读 ====================
 * 本文件是 FRR 事件子系统（旧名 thread 子系统）的核心实现，是几乎所有
 * 守护进程的“心脏”。它实现了事件主循环 event_loop（旧名 thread_master），
 * 把定时器、文件描述符可读 / 可写、一次性事件、跨线程投递的事件统一排队
 * 调度，并逐一回调注册的处理函数。
 *
 * 关键数据结构：
 *   - struct event_loop：一个事件循环实例。持有 read / write 两个 fd 事件
 *     槽数组、定时器最小堆 timer、以及 event / ready / unuse 三条链表；
 *     还持有跨线程取消请求链表 cancel_req、cpu 统计 cpu_records、唤醒用
 *     的 io_pipe 与归属线程 owner 等。
 *   - struct event：一次被调度的具体事件（见 frrevent.h）。
 *   - struct fd_handler：fd 监听后端，EPOLL_ENABLED 时用 epoll，否则用
 *     poll；分别维护 fd 集合 / pollfd 数组。
 *
 * 主循环怎么跑：
 *   1. 每轮先取出已到期的定时器，转入 ready 队列；
 *   2. 调用后端（epoll_wait / poll）等待 fd 就绪，把就绪 fd 对应的事件也
 *      转入 ready 队列；
 *   3. 按链表中顺序执行 ready 队列里的事件回调；
 *   4. 处理跨线程取消请求与唤醒管道。
 *
 * 线程与主循环的关系：
 *   - 每个 event_loop 归属于某个 pthread（owner）；event_master_create
 *     在主线程创建，工作线程通过 frr_pthread 创建自己的循环。
 *   - 其它线程往某循环投递 / 取消事件时，若目标不是本线程，会走
 *     cancel_req 异步队列并用 io_pipe 唤醒目标循环（见 event_cancel_async）。
 * ================================================ */

#include <zebra.h>

#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "frrevent.h"
#include "memory.h"
#include "frrcu.h"
#include "log.h"
#include "hash.h"
#include "command.h"
#include "sigevent.h"
#include "network.h"
#include "jhash.h"
#include "frratomic.h"
#include "frr_pthread.h"
#include "lib_errors.h"
#include "libfrr_trace.h"
#include "libfrr.h"

/* 为事件对象、事件循环、poll 信息、统计记录分别登记内存类型，便于统计排查。 */
DEFINE_MTYPE_STATIC(LIB, THREAD, "Thread");
DEFINE_MTYPE_STATIC(LIB, EVENT_MASTER, "Thread master");
DEFINE_MTYPE_STATIC(LIB, EVENT_POLL, "Thread Poll Info");
DEFINE_MTYPE_STATIC(LIB, EVENT_STATS, "Thread stats");

#if EPOLL_ENABLED

PREDECL_HASH(epoll_event_hash);
PREDECL_DLIST(epoll_revent_list);

/* epoll 版：每个被监听的 fd 对应一个 frr_epoll_event，既挂进哈希表（按 fd
 * 查找）又挂进“常规文件”链表。ev 是原始 epoll_event，flags 见
 * FRR_EV_FD_IS_REGULAR。
 */
struct frr_epoll_event {
	struct epoll_event ev;
	int flags;
	struct epoll_event_hash_item hlink;
	struct epoll_revent_list_item rlink;
};

/* Flags values */
/* 标志位：该 fd 是常规文件（epoll 不能监听，需走常规事件链表）。 */
#define FRR_EV_FD_IS_REGULAR 0x01

/* epoll 后端的 fd 处理器：epoll_fd 为 epoll 实例；epoll_event_hash 按 fd
 * 登记被监听事件；revents 是 epoll_wait 的结果缓冲区；epoll_revents_list
 * 存放无法加入 epoll 的常规文件事件；fd_poll_counter 统计每个 fd 的事件数。
 */
struct fd_handler {
	/* The epoll set file descriptor */
	int epoll_fd;

	/* A hash table in which monitored I/O file descriptors and events
	 * are registered
	 */
	struct epoll_event_hash_head epoll_event_hash;

	/* Maximum size of .revents array */
	int eventsize;

	/* The buffer which stores the results of epoll_wait */
	struct epoll_event *revents;

	/* Vtysh might redirect stdin/stdout to regular files. However,
	 * regular files can't be added into epoll set and need special
	 * treatment. I/O events from/to regular files will be directly
	 * added to the regular events list, but not into the epoll set,
	 * sidestepping epoll_wait.
	 */
	struct epoll_revent_list_head epoll_revents_list;

	unsigned long *fd_poll_counter;
};
#else
/* poll 后端的 fd 处理器：pfds 是待监听的 pollfd 数组（容量 pfdsize，已用
 * pfdcount），copy 是轮询前对 pfds 的临时拷贝（容量同 pfdsize，已用
 * copycount）。
 */
struct fd_handler {
	/* number of pfd that fit in the allocated space of pfds. This is a
	 * constant and is the same for both pfds and copy.
	 */
	nfds_t pfdsize;

	/* file descriptors to monitor for i/o */
	struct pollfd *pfds;
	/* number of pollfds stored in pfds */
	nfds_t pfdcount;

	/* chunk used for temp copy of pollfds */
	struct pollfd *copy;
	/* number of pollfds stored in copy */
	nfds_t copycount;
};
#endif

/* Master of the theads. */
/* 事件主循环（event_master）。字段要点：
 *   name：循环名（日志 / 统计用）。
 *   read、write：按 fd 索引的读 / 写事件指针数组（同一 fd 单事件）。
 *   timer：定时器最小堆，按到期时间排序。
 *   event、ready、unuse：待处理、就绪、空闲三条事件链表。
 *   cancel_req：来自其它线程的取消请求；canceled 与 cancel_cond 配合。
 *   cpu_records：按回调函数聚合的 cpu / 墙钟统计哈希表。
 *   io_pipe：自唤醒管道，向写端写字节即可唤醒阻塞中的 poll / epoll。
 *   fd_limit：fd 上限；handler：fd 监听后端；selectpoll_timeout：超时；
 *     spin：忙等标记。
 *   handle_signals：本循环是否处理进程信号。
 *   mtx：保护本结构的互斥锁；owner：归属线程（pthread）。
 *   ready_run_loop：主循环是否已进入就绪运行态。
 *   last_getrusage / last_tardy_warning：统计与迟到告警限速的时间记录。
 */
struct event_loop {
	char *name;

	struct event **read;
	struct event **write;
	struct event_timer_list_head timer;
	struct event_list_head event, ready, unuse;
	struct list *cancel_req;
	bool canceled;
	pthread_cond_t cancel_cond;
	struct cpu_records_head cpu_records[1];
	int io_pipe[2];
	int fd_limit;
	struct fd_handler handler;
	long selectpoll_timeout;
	bool spin;
	bool handle_signals;
	pthread_mutex_t mtx;
	pthread_t owner;

#if !EPOLL_ENABLED
	nfds_t last_read;
#endif

	bool ready_run_loop;
	RUSAGE_T last_getrusage;
	struct timeval last_tardy_warning;
};

#if EPOLL_ENABLED
DEFINE_MTYPE_STATIC(LIB, EVENT_EPOLL, "Thread epoll events");

/* 比较两个 epoll 事件所属 fd，供哈希表判等用。 */
static int epoll_event_hash_cmp(const struct frr_epoll_event *f,
				const struct frr_epoll_event *s)
{
	return f->ev.data.fd - s->ev.data.fd;
}

/* 以 fd 计算 epoll 事件哈希键（jhash 加固定种子）。 */
static uint32_t epoll_event_hash_key(const struct frr_epoll_event *e)
{
	uint32_t val, initval = 0xd4297c53;

	val = e->ev.data.fd;
	return jhash_1word(val, initval);
}

/* 展开 epoll 事件哈希表实现（键为 fd）。 */
/* Hash of epoll events (by fd) */
DECLARE_HASH(epoll_event_hash, struct frr_epoll_event, hlink, epoll_event_hash_cmp,
	     epoll_event_hash_key);

/* 展开“常规文件”事件链表实现（这些对象不加入 epoll 集）。 */
/* List of "regular" epoll objects, that are not added to the epoll set. */
DECLARE_DLIST(epoll_revent_list, struct frr_epoll_event, rlink);

#endif

/* 展开 event_list（struct event 的 eventitem 成员）链表实现。 */
DECLARE_LIST(event_list, struct event, eventitem);

/* 一条跨线程取消请求：flags 见 EVENT_CANCEL_FLAG_READY；event 为待取消事件；
 * eventobj 为附带数据；threadref 为调用方保存的事件句柄指针。
 */
struct cancel_req {
	int flags;
	struct event *event;
	void *eventobj;
	struct event **threadref;
};

/* 取消请求标志：目标事件当前位于 ready 队列。 */
/* Flags for task cancellation */
#define EVENT_CANCEL_FLAG_READY 0x01

/* 定时器最小堆比较函数：按到期时间（先 tv_sec 后 tv_usec）升序。 */
static int event_timer_cmp(const struct event *a, const struct event *b)
{
	if (a->u.sands.tv_sec < b->u.sands.tv_sec)
		return -1;
	if (a->u.sands.tv_sec > b->u.sands.tv_sec)
		return 1;
	if (a->u.sands.tv_usec < b->u.sands.tv_usec)
		return -1;
	if (a->u.sands.tv_usec > b->u.sands.tv_usec)
		return 1;
	return 0;
}

/* 展开 event_timer_list 最小堆实现（比较函数见上）。 */
DECLARE_HEAP(event_timer_list, struct event, timeritem, event_timer_cmp);

/* 唤醒主循环：向 io_pipe 写端写一个字节，使 poll / epoll_wait 立即返回。 */
#define AWAKEN(m)                                                              \
	do {                                                                   \
		const unsigned char wakebyte = 0x01;                           \
		write(m->io_pipe[1], &wakebyte, 1);                            \
	} while (0)

/* control variable for initializer */
/* init_once 保证本文件的初始化逻辑只执行一次；thread_current 为线程局部键。 */
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
pthread_key_t thread_current;

/* 所有事件循环组成全局链表 masters，保护它的是 masters_mtx。 */
static pthread_mutex_t masters_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct list *masters;

/* 内部函数：释放一个事件及其关联资源（前置声明）。 */
static void thread_free(struct event_loop *master, struct event *event);

/* cpu / 墙钟时间统计的开关与阈值（详见 frrevent.h 中同名 extern 说明）。 */
bool cputime_enabled = true;
unsigned long cputime_threshold = CONSUMED_TIME_CHECK;
unsigned long walltime_threshold = CONSUMED_TIME_CHECK;

/* CLI 相关代码区：下面 include 的 clippy 文件提供 "show event cpu" 等命令。 */
/* CLI start ---------------------------------------------------------------- */
#include "lib/event_clippy.c"

/* cpu 统计哈希表以回调函数地址为键。 */
static uint32_t cpu_record_hash_key(const struct cpu_event_history *a)
{
	int size = sizeof(a->func);

	return jhash(&a->func, size, 0);
}

/* cpu 统计哈希表的判等函数：比较回调函数地址。 */
static int cpu_record_hash_cmp(const struct cpu_event_history *a,
			       const struct cpu_event_history *b)
{
	return numcmp((uintptr_t)a->func, (uintptr_t)b->func);
}

/* 展开 cpu_records 哈希表实现。 */
DECLARE_HASH(cpu_records, struct cpu_event_history, item, cpu_record_hash_cmp,
	     cpu_record_hash_key);

/* 按回调函数在 loop 的统计表里查找记录，不存在则新建并挂入；funcname 用于显示。 */
static struct cpu_event_history *cpu_records_get(struct event_loop *loop,
						 void (*func)(struct event *e),
						 const char *funcname)
{
	struct cpu_event_history ref = { .func = func }, *res;

	res = cpu_records_find(loop->cpu_records, &ref);
	if (!res) {
		res = XCALLOC(MTYPE_EVENT_STATS, sizeof(*res));
		res->func = func;
		res->funcname = funcname;
		cpu_records_add(loop->cpu_records, res);
	}
	return res;
}

/* 清零一条统计记录的“可清除区间”（由零长数组标出的字段范围）。 */
static void cpu_records_clear(struct cpu_event_history *p)
{
	memset(p->_clear_begin, 0, p->_clear_end - p->_clear_begin);
}

/* 释放一条统计记录。 */
static void cpu_records_free(struct cpu_event_history **p)
{
	XFREE(MTYPE_EVENT_STATS, *p);
}

/* 把一条 cpu 统计记录格式化输出：先是各计数与均值 / 峰值，再以 R / W / T /
 * E / X 字母标出该回调涉及的事件类型，最后是函数名。
 */
static void vty_out_cpu_event_history(struct vty *vty,
				      struct cpu_event_history *a)
{
	vty_out(vty,
		"%5zu %10zu.%03zu %9zu %8zu %9zu %8zu %9zu %9zu %9zu %10zu",
		a->total_active, a->cpu.total / 1000, a->cpu.total % 1000,
		a->total_calls, (a->cpu.total / a->total_calls), a->cpu.max,
		(a->real.total / a->total_calls), a->real.max,
		a->total_cpu_warn, a->total_wall_warn, a->total_starv_warn);
	vty_out(vty, "  %c%c%c%c%c  %s\n",
		a->types & (1 << EVENT_READ) ? 'R' : ' ',
		a->types & (1 << EVENT_WRITE) ? 'W' : ' ',
		a->types & (1 << EVENT_TIMER) ? 'T' : ' ',
		a->types & (1 << EVENT_EVENT) ? 'E' : ' ',
		a->types & (1 << EVENT_EXECUTE) ? 'X' : ' ', a->funcname);
}

/* 打印一条统计记录（按 filter 过滤类型），并把它的各计数累加到 totals，
 * 以便最后打印总计行。先用原子读把记录快照到局部 copy，避免并发竞争。
 */
static void cpu_record_print_one(struct vty *vty, uint8_t filter,
				 struct cpu_event_history *totals,
				 const struct cpu_event_history *a)
{
	struct cpu_event_history copy;

	copy.total_active =
		atomic_load_explicit(&a->total_active, memory_order_seq_cst);
	copy.total_calls =
		atomic_load_explicit(&a->total_calls, memory_order_seq_cst);
	copy.total_cpu_warn =
		atomic_load_explicit(&a->total_cpu_warn, memory_order_seq_cst);
	copy.total_wall_warn =
		atomic_load_explicit(&a->total_wall_warn, memory_order_seq_cst);
	copy.total_starv_warn = atomic_load_explicit(&a->total_starv_warn,
						     memory_order_seq_cst);
	copy.cpu.total =
		atomic_load_explicit(&a->cpu.total, memory_order_seq_cst);
	copy.cpu.max = atomic_load_explicit(&a->cpu.max, memory_order_seq_cst);
	copy.real.total =
		atomic_load_explicit(&a->real.total, memory_order_seq_cst);
	copy.real.max =
		atomic_load_explicit(&a->real.max, memory_order_seq_cst);
	copy.types = atomic_load_explicit(&a->types, memory_order_seq_cst);
	copy.funcname = a->funcname;

	if (!(copy.types & filter))
		return;

	vty_out_cpu_event_history(vty, &copy);
	totals->total_active += copy.total_active;
	totals->total_calls += copy.total_calls;
	totals->total_cpu_warn += copy.total_cpu_warn;
	totals->total_wall_warn += copy.total_wall_warn;
	totals->total_starv_warn += copy.total_starv_warn;
	totals->real.total += copy.real.total;
	if (totals->real.max < copy.real.max)
		totals->real.max = copy.real.max;
	totals->cpu.total += copy.cpu.total;
	if (totals->cpu.max < copy.cpu.max)
		totals->cpu.max = copy.cpu.max;
}

/* 打印所有事件循环的 cpu / 墙钟统计：先按 pthread 逐个输出其统计表，再输出
 * 汇总行。若 cputime 统计被关闭会先给出提示（墙钟统计始终有效）。
 */
static void cpu_record_print(struct vty *vty, uint8_t filter)
{
	struct cpu_event_history tmp;
	struct event_loop *m;
	struct listnode *ln;

	if (!cputime_enabled)
		vty_out(vty,
			"\n"
			"Collecting CPU time statistics is currently disabled.  Following statistics\n"
			"will be zero or may display data from when collection was enabled.  Use the\n"
			"  \"service cputime-stats\"  command to start collecting data.\n"
			"\nCounters and wallclock times are always maintained and should be accurate.\n");

	memset(&tmp, 0, sizeof(tmp));
	tmp.funcname = "TOTAL";
	tmp.types = filter;

	frr_with_mutex (&masters_mtx) {
		for (ALL_LIST_ELEMENTS_RO(masters, ln, m)) {
			const char *name = m->name ? m->name : "main";
			char underline[strlen(name) + 1];

			memset(underline, '-', sizeof(underline));
			underline[sizeof(underline) - 1] = '\0';

			vty_out(vty, "\n");
			vty_out(vty, "Showing statistics for pthread %s\n",
				name);
			vty_out(vty, "-------------------------------%s\n",
				underline);
			vty_out(vty, "%30s %18s %18s\n", "",
				"CPU (user+system):", "Real (wall-clock):");
			vty_out(vty,
				"Active   Runtime(ms)   Invoked Avg uSec Max uSecs");
			vty_out(vty, " Avg uSec Max uSecs");
			vty_out(vty,
				"  CPU_Warn Wall_Warn Starv_Warn   Type  Event\n");

			if (cpu_records_count(m->cpu_records)) {
				struct cpu_event_history *rec;

				frr_each (cpu_records, m->cpu_records, rec)
					cpu_record_print_one(vty, filter, &tmp,
							     rec);
			} else
				vty_out(vty, "No data to display yet.\n");

			vty_out(vty, "\n");
		}
	}

	vty_out(vty, "\n");
	vty_out(vty, "Total Event statistics\n");
	vty_out(vty, "-------------------------\n");
	vty_out(vty, "%30s %18s %18s\n", "",
		"CPU (user+system):", "Real (wall-clock):");
	vty_out(vty, "Active   Runtime(ms)   Invoked Avg uSec Max uSecs");
	vty_out(vty, " Avg uSec Max uSecs  CPU_Warn Wall_Warn Starv_Warn");
	vty_out(vty, "   Type  Event\n");

	if (tmp.total_calls > 0)
		vty_out_cpu_event_history(vty, &tmp);
}

/* 按 filter 清零各循环统计表中匹配记录的可清除字段。注意这里只清零不释放
 * 内存，因为其中一些记录可能正在被使用（如当前正在运行的记录）。
 */
static void cpu_record_clear(uint8_t filter)
{
	struct event_loop *m;
	struct listnode *ln;

	frr_with_mutex (&masters_mtx) {
		for (ALL_LIST_ELEMENTS_RO(masters, ln, m)) {
			frr_with_mutex (&m->mtx) {
				struct cpu_event_history *item;

				/* it isn't possible to free the memory here
				 * because some of these will be in use (e.g.
				 * the one we're currently running in!)
				 */
				frr_each (cpu_records, m->cpu_records, item) {
					if (item->types & filter)
						cpu_records_clear(item);
				}
			}
		}
	}
}

/* 把命令里的过滤字符串（R / W / T / E / X 等字母）解析成一个位掩码，用于
 * 选择要显示 / 清理的事件类型。未知字符忽略。返回掩码。
 */
static uint8_t parse_filter(const char *filterstr)
{
	int i = 0;
	int filter = 0;

	while (filterstr[i] != '\0') {
		switch (filterstr[i]) {
		case 'r':
		case 'R':
			filter |= (1 << EVENT_READ);
			break;
		case 'w':
		case 'W':
			filter |= (1 << EVENT_WRITE);
			break;
		case 't':
		case 'T':
			filter |= (1 << EVENT_TIMER);
			break;
		case 'e':
		case 'E':
			filter |= (1 << EVENT_EVENT);
			break;
		case 'x':
		case 'X':
			filter |= (1 << EVENT_EXECUTE);
			break;
		default:
			break;
		}
		++i;
	}
	return filter;
}

/* 命令 "show event cpu [FILTER]"：按可选过滤器显示各线程的事件 cpu 统计。 */
DEFUN_NOSH (show_event_cpu,
            show_event_cpu_cmd,
            "show event cpu [FILTER]",
            SHOW_STR
            "Event information\n"
            "Event CPU usage\n"
            "Display filter (rwtexb)\n")
{
	uint8_t filter = (uint8_t)-1U;
	int idx = 0;

	if (argv_find(argv, argc, "FILTER", &idx)) {
		filter = parse_filter(argv[idx]->arg);
		if (!filter) {
			vty_out(vty,
				"Invalid filter \"%s\" specified; must contain at leastone of 'RWTEXB'\n",
				argv[idx]->arg);
			return CMD_WARNING;
		}
	}

	cpu_record_print(vty, filter);
	return CMD_SUCCESS;
}

/* 命令 "[no] service cputime-stats"：开关 cpu 时间统计（cputime_enabled）。 */
DEFPY (service_cputime_stats,
       service_cputime_stats_cmd,
       "[no] service cputime-stats",
       NO_STR
       "Set up miscellaneous service\n"
       "Collect CPU usage statistics\n")
{
	cputime_enabled = !no;
	return CMD_SUCCESS;
}

/* 命令 "[no] service cputime-warning <毫秒>"：设置 cpu 超时告警阈值；no 时清零。 */
DEFPY (service_cputime_warning,
       service_cputime_warning_cmd,
       "[no] service cputime-warning ![(1-4294967295)]",
       NO_STR
       "Set up miscellaneous service\n"
       "Warn for tasks exceeding CPU usage threshold\n"
       "Warning threshold in milliseconds\n")
{
	if (no)
		cputime_threshold = 0;
	else
		cputime_threshold = cputime_warning * 1000;
	return CMD_SUCCESS;
}

/* 命令 "[no] service walltime-warning <毫秒>"：设置墙钟超时告警阈值；no 时清零。 */
DEFPY (service_walltime_warning,
       service_walltime_warning_cmd,
       "[no] service walltime-warning ![(1-4294967295)]",
       NO_STR
       "Set up miscellaneous service\n"
       "Warn for tasks exceeding total wallclock threshold\n"
       "Warning threshold in milliseconds\n")
{
	if (no)
		walltime_threshold = 0;
	else
		walltime_threshold = walltime_warning * 1000;
	return CMD_SUCCESS;
}

#if EPOLL_ENABLED
/* "show event poll" 辅助迭代器的参数：vty 与目标事件循环。 */
struct show_event_poll_helper_iter_arg_t {
	struct vty *vty;
	struct event_loop *master;
};

/* 打印一个 epoll 事件的 fd、监听事件位与对应的读 / 写回调函数名（缺省显示
 * ERROR），用于 "show event poll" 调试。
 */
static void show_epoll_event_helper(struct vty *vty, const struct frr_epoll_event *ev,
				    struct event_loop *m)
{
	struct event *thread;

	vty_out(vty, "\t fd:%6d events:%2d\t\t", ev->ev.data.fd, ev->ev.events);

	if (ev->ev.events & EPOLLIN) {
		thread = m->read[ev->ev.data.fd];

		if (!thread)
			vty_out(vty, "ERROR ");
		else
			vty_out(vty, "%s ", thread->xref->funcname);
	} else
		vty_out(vty, " ");

	if (ev->ev.events & EPOLLOUT) {
		thread = m->write[ev->ev.data.fd];

		if (!thread)
			vty_out(vty, "ERROR\n");
		else
			vty_out(vty, "%s\n", thread->xref->funcname);
	} else
		vty_out(vty, "\n");
}

/* epoll 版：打印某事件循环的 fd 监听概况——每个 fd 的事件计数、注册的事件
 * 总数（哈希 + 常规链表）与 fd 上限，再逐个列出 epoll 事件。
 */
static void show_event_poll_helper(struct vty *vty, struct event_loop *m)
{
	const char *name = m->name ? m->name : "main";
	char underline[strlen(name) + 1];
	const struct frr_epoll_event *ev;

	memset(underline, '-', sizeof(underline));
	underline[sizeof(underline) - 1] = '\0';

	vty_out(vty, "\nShowing epoll FD's count for %s\n", name);
	vty_out(vty, "----------------------%s\n", underline);
	for (int i = 0; i < m->handler.eventsize; i++) {
		if (m->handler.fd_poll_counter[i] > 0)
			vty_out(vty, "\tfd: %d, event count: %lu\n", i,
				m->handler.fd_poll_counter[i]);
	}

	vty_out(vty, "\nShowing epoll FD's for %s\n", name);
	vty_out(vty, "----------------------%s\n", underline);
	vty_out(vty, "Count: %u/%d\n",
		(uint32_t)(epoll_revent_list_count(&m->handler.epoll_revents_list) +
			   epoll_event_hash_count(&m->handler.epoll_event_hash)),
		m->fd_limit);

	frr_each (epoll_event_hash_const, &m->handler.epoll_event_hash, ev)
		show_epoll_event_helper(vty, ev, m);
}
#else
/* poll 版：打印某事件循环监听的 pollfd 数组（fd、监听事件位、就绪事件位与
 * 对应读写回调名）以及已监听个数 / fd 上限。
 */
static void show_event_poll_helper(struct vty *vty, struct event_loop *m)
{
	const char *name = m->name ? m->name : "main";
	char underline[strlen(name) + 1];
	struct event *event;
	uint32_t i;

	memset(underline, '-', sizeof(underline));
	underline[sizeof(underline) - 1] = '\0';

	vty_out(vty, "\nShowing poll FD's for %s\n", name);
	vty_out(vty, "----------------------%s\n", underline);
	vty_out(vty, "Count: %u/%d\n", (uint32_t)m->handler.pfdcount,
		m->fd_limit);
	for (i = 0; i < m->handler.pfdcount; i++) {
		vty_out(vty, "\t%6d fd:%6d events:%2d revents:%2d\t\t", i,
			m->handler.pfds[i].fd, m->handler.pfds[i].events,
			m->handler.pfds[i].revents);

		if (m->handler.pfds[i].events & POLLIN) {
			event = m->read[m->handler.pfds[i].fd];

			if (!event)
				vty_out(vty, "ERROR ");
			else
				vty_out(vty, "%s ", event->xref->funcname);
		} else
			vty_out(vty, " ");

		if (m->handler.pfds[i].events & POLLOUT) {
			event = m->write[m->handler.pfds[i].fd];

			if (!event)
				vty_out(vty, "ERROR\n");
			else
				vty_out(vty, "%s\n", event->xref->funcname);
		} else
			vty_out(vty, "\n");
	}
}
#endif

/* 命令 "show event poll"：遍历所有事件循环，输出各自的 fd 监听详情。 */
DEFUN_NOSH (show_event_poll,
            show_event_poll_cmd,
            "show event poll",
            SHOW_STR
            "Event information\n"
            "Event Poll Information\n")
{
	struct listnode *node;
	struct event_loop *m;

	frr_with_mutex (&masters_mtx) {
		for (ALL_LIST_ELEMENTS_RO(masters, node, m)) {
			pthread_mutex_lock(&m->mtx);
			show_event_poll_helper(vty, m);
			pthread_mutex_unlock(&m->mtx);
		}
	}

	return CMD_SUCCESS;
}

/* 命令 "clear event cpu [FILTER]"：按过滤器清零各线程的事件 cpu 统计。 */
DEFUN (clear_event_cpu,
       clear_event_cpu_cmd,
       "clear event cpu [FILTER]",
       "Clear stored data in all pthreads\n"
       "Event information\n"
       "Event CPU usage\n"
       "Display filter (rwtexb)\n")
{
	uint8_t filter = (uint8_t)-1U;
	int idx = 0;

	if (argv_find(argv, argc, "FILTER", &idx)) {
		filter = parse_filter(argv[idx]->arg);
		if (!filter) {
			vty_out(vty,
				"Invalid filter \"%s\" specified; must contain at leastone of 'RWTEXB'\n",
				argv[idx]->arg);
			return CMD_WARNING;
		}
	}

	cpu_record_clear(filter);
	return CMD_SUCCESS;
}

/* 打印某个事件循环当前挂着的所有定时器（函数名 + 剩余时间）。 */
static void show_event_timers_helper(struct vty *vty, struct event_loop *m)
{
	const char *name = m->name ? m->name : "main";
	char underline[strlen(name) + 1];
	struct event *event;

	memset(underline, '-', sizeof(underline));
	underline[sizeof(underline) - 1] = '\0';

	vty_out(vty, "\nShowing timers for %s\n", name);
	vty_out(vty, "-------------------%s\n", underline);

	frr_each (event_timer_list, &m->timer, event) {
		vty_out(vty, "  %-50s%pTH\n", event->hist->funcname, event);
	}
}

/* 命令 "show event timers"：显示所有事件循环中的定时器及其剩余时间。 */
DEFPY_NOSH (show_event_timers,
            show_event_timers_cmd,
            "show event timers",
            SHOW_STR
            "Event information\n"
            "Show all timers and how long they have in the system\n")
{
	struct listnode *node;
	struct event_loop *m;

	frr_with_mutex (&masters_mtx) {
		for (ALL_LIST_ELEMENTS_RO(masters, node, m)) {
			pthread_mutex_lock(&m->mtx);
			show_event_timers_helper(vty, m);
			pthread_mutex_unlock(&m->mtx);
		}
	}

	return CMD_SUCCESS;
}

/* 注册事件子系统相关的 CLI 命令（在守护进程启动时调用）。 */
void event_cmd_init(void)
{
	install_element(VIEW_NODE, &show_event_cpu_cmd);
	install_element(VIEW_NODE, &show_event_poll_cmd);
	install_element(ENABLE_NODE, &clear_event_cpu_cmd);

	install_element(CONFIG_NODE, &service_cputime_stats_cmd);
	install_element(CONFIG_NODE, &service_cputime_warning_cmd);
	install_element(CONFIG_NODE, &service_walltime_warning_cmd);

	install_element(VIEW_NODE, &show_event_timers_cmd);
}
/* CLI end ------------------------------------------------------------------ */


/* 释放一条取消请求对象（作为链表删除回调使用）。 */
static void cancelreq_del(void *cr)
{
	XFREE(MTYPE_TMP, cr);
}

/* initializer, only ever called once */
/* 一次性初始化：创建线程局部键 thread_current。由 pthread_once 保证只跑一次。 */
static void initializer(void)
{
	pthread_key_create(&thread_current, NULL);
}

#if EPOLL_ENABLED
/* 分配 / 释放 epoll 包装结构 frr_epoll_event */
/* Alloc, free epoll wrapper structs */
static struct frr_epoll_event *frr_epoll_event_new(int fd, uint32_t events)
{
	struct frr_epoll_event *ev = XCALLOC(MTYPE_EVENT_EPOLL,
					     sizeof(struct frr_epoll_event));
	ev->ev.data.fd = fd;
	ev->ev.events = events;
	return ev;
}

/* 释放一个 frr_epoll_event。 */
static void frr_epoll_event_del(struct frr_epoll_event **ev)
{
	XFREE(MTYPE_EVENT_EPOLL, *ev);
}

/* 取 fd 的 stat 信息；若 fstat 因 EBADF 失败则认为 fd 已关闭，通过 fd_closed
 * 报告（其它 errno 仅打调试日志）。
 */
static void get_fd_stat(int fd, struct stat *fd_stat, bool *fd_closed)
{
	assert(fd_stat != NULL);
	if (fstat(fd, fd_stat) == -1) {
		/* fd is probably already closed */
		if (errno == EBADF) {
			if (fd_closed != NULL)
				*fd_closed = true;
			return;
		}
		zlog_debug("[!] In %s, fstat failed unexpectedly, fd: %d, errno: %d)",
			   __func__, fd, errno);
	}
	if (fd_closed != NULL)
		*fd_closed = false;
}
#endif

/* fd_limit 的硬上限：超过则截断，避免为超大 fd 表分配过多内存。 */
#define STUPIDLY_LARGE_FD_SIZE 100000

/* 创建一个事件主循环（event_master）。要点：
 *   - 通过 pthread_once 做一次性初始化（创建 thread_current 键）；
 *   - 初始化互斥量 / 取消条件变量，复制名字；
 *   - 确定 fd 上限：优先配置值，否则 ulimit，并截断到 STUPIDLY_LARGE_FD_SIZE；
 *   - 分配 read / write 两个按 fd 索引的事件指针数组；
 *   - 初始化 cpu 统计哈希表、event / ready / unuse 链表与定时器堆；
 *   - spin、handle_signals 置真，设定迟到告警基准时间，记录 owner；
 *   - 创建唤醒管道 io_pipe（非阻塞）；
 *   - 按后端初始化 fd_handler（epoll 版创建 epoll 实例并把管道读端加入；
 *     poll 版分配 pfds 与 copy 数组）；
 *   - 最后把本循环加入全局 masters 链表。返回新建的事件循环。
 */
struct event_loop *event_master_create(const char *name)
{
	struct event_loop *rv;
	struct rlimit limit;
#if EPOLL_ENABLED
	struct epoll_event pipe_read_ev;
#endif

	pthread_once(&init_once, &initializer);

	rv = XCALLOC(MTYPE_EVENT_MASTER, sizeof(struct event_loop));

	/* Initialize master mutex */
	pthread_mutex_init(&rv->mtx, NULL);
	pthread_cond_init(&rv->cancel_cond, NULL);

	/* Set name */
	name = name ? name : "default";
	rv->name = XSTRDUP(MTYPE_EVENT_MASTER, name);

	/* Initialize I/O task data structures */

	/* Use configured limit if present, ulimit otherwise. */
	rv->fd_limit = frr_get_fd_limit();
	if (rv->fd_limit == 0) {
		getrlimit(RLIMIT_NOFILE, &limit);
		rv->fd_limit = (int)limit.rlim_cur;
	}

	if (rv->fd_limit > STUPIDLY_LARGE_FD_SIZE) {
		if (frr_is_daemon())
			zlog_warn("FD Limit set: %u is stupidly large.  Is this what you intended?  Consider using --limit-fds also limiting size to %u",
				  rv->fd_limit, STUPIDLY_LARGE_FD_SIZE);

		rv->fd_limit = STUPIDLY_LARGE_FD_SIZE;
	}

	rv->read = XCALLOC(MTYPE_EVENT_POLL,
			   sizeof(struct event *) * rv->fd_limit);

	rv->write = XCALLOC(MTYPE_EVENT_POLL,
			    sizeof(struct event *) * rv->fd_limit);

	cpu_records_init(rv->cpu_records);

	event_list_init(&rv->event);
	event_list_init(&rv->ready);
	event_list_init(&rv->unuse);
	event_timer_list_init(&rv->timer);

	/* Initialize event_fetch() settings */
	rv->spin = true;
	rv->handle_signals = true;

	/* tardy event warnings */
	monotime(&rv->last_tardy_warning);
	rv->last_tardy_warning.tv_sec -= (TARDY_WARNING_INTERVAL + TIMER_SECOND_MICRO - 1) /
					 TIMER_SECOND_MICRO;

	/* Set pthread owner, should be updated by actual owner */
	rv->owner = pthread_self();
	rv->cancel_req = list_new();
	rv->cancel_req->del = cancelreq_del;
	rv->canceled = true;

	/* Initialize pipe poker */
	pipe(rv->io_pipe);
	set_nonblocking(rv->io_pipe[0]);
	set_nonblocking(rv->io_pipe[1]);

#if EPOLL_ENABLED
	/* Initialize data structures for epoll */
	rv->handler.epoll_fd = epoll_create1(0);
	epoll_event_hash_init(&rv->handler.epoll_event_hash);
	epoll_revent_list_init(&rv->handler.epoll_revents_list);
	rv->handler.eventsize = rv->fd_limit;
	rv->handler.revents = XCALLOC(MTYPE_EVENT_MASTER,
				      sizeof(struct epoll_event) * rv->handler.eventsize);
	memset(&pipe_read_ev, 0, sizeof(pipe_read_ev));
	pipe_read_ev.data.fd = rv->io_pipe[0];
	pipe_read_ev.events = EPOLLIN;
	if (epoll_ctl(rv->handler.epoll_fd, EPOLL_CTL_ADD, rv->io_pipe[0],
		      &pipe_read_ev) == -1) {
		flog_err(EC_LIB_NO_THREAD,
			 "Attempting to call epoll_ctl to add io_pipe[0] but failed, fd: %d!",
			 rv->io_pipe[0]);
		exit(1);
	}
	rv->handler.fd_poll_counter =
		XCALLOC(MTYPE_EVENT_MASTER,
			sizeof(unsigned long) * rv->handler.eventsize);
#else
	/* Initialize data structures for poll() */
	rv->handler.pfdsize = rv->fd_limit;
	rv->handler.pfdcount = 0;
	rv->handler.pfds = XCALLOC(MTYPE_EVENT_MASTER,
				   sizeof(struct pollfd) * rv->handler.pfdsize);
	rv->handler.copy = XCALLOC(MTYPE_EVENT_MASTER,
				   sizeof(struct pollfd) * rv->handler.pfdsize);
#endif

	/* add to list of loops */
	frr_with_mutex (&masters_mtx) {
		if (!masters)
			masters = list_new();

		listnode_add(masters, rv);
	}

	return rv;
}

/* 修改事件循环的名字（加锁替换 name 字符串）。 */
void event_master_set_name(struct event_loop *master, const char *name)
{
	frr_with_mutex (&master->mtx) {
		XFREE(MTYPE_EVENT_MASTER, master->name);
		master->name = XSTRDUP(MTYPE_EVENT_MASTER, name);
	}
}

/* unuse（空闲事件缓存）链表的最大保留深度：复用已分配事件对象，减少频繁的
 * malloc / free。
 */
#define EVENT_UNUSED_DEPTH 10

/* Move thread to unuse list. */
/* 把一个事件放回 unuse 空闲缓存链表以便复用；超过 EVENT_UNUSED_DEPTH 则真正
 * 释放。会先把事件清零（保留 type=EVENT_UNUSED 与原互斥量），并递减其活跃
 * 计数 total_active。
 */
static void thread_add_unuse(struct event_loop *m, struct event *event)
{
	pthread_mutex_t mtxc = event->mtx;

	assert(m != NULL && event != NULL);

	event->hist->total_active--;
	memset(event, 0, sizeof(struct event));
	event->type = EVENT_UNUSED;

	/* Restore the event mutex context. */
	event->mtx = mtxc;

	if (event_list_count(&m->unuse) < EVENT_UNUSED_DEPTH) {
		event_list_add_tail(&m->unuse, event);
		return;
	}

	thread_free(m, event);
}

/* Free all unused thread. */
/* 释放一条事件链表里的全部事件。 */
static void thread_list_free(struct event_loop *m, struct event_list_head *list)
{
	struct event *t;

	while ((t = event_list_pop(list)))
		thread_free(m, t);
}

/* 释放 fd 事件数组（read / write）里的全部事件并释放数组本身。 */
static void thread_array_free(struct event_loop *m, struct event **thread_array)
{
	struct event *t;
	int index;

	for (index = 0; index < m->fd_limit; ++index) {
		t = thread_array[index];
		if (t) {
			thread_array[index] = NULL;
			thread_free(m, t);
		}
	}
	XFREE(MTYPE_EVENT_POLL, thread_array);
}

/* Stop thread scheduler. */
/* 销毁事件循环：从全局 masters 摘除；释放 read / write 数组、定时器堆、三条
 * 事件链表中的所有事件；销毁互斥量 / 条件变量；关闭唤醒管道；释放取消请求
 * 链表与 cpu 统计；按后端释放 fd_handler（epoll 释放实例与哈希 / 链表，
 * poll 释放 pfds / copy）；最后释放循环结构。
 */
void event_master_free(struct event_loop *m)
{
	struct cpu_event_history *record;
	struct event *t;

	frr_with_mutex (&masters_mtx) {
		listnode_delete(masters, m);
		if (masters->count == 0)
			list_delete(&masters);
	}

	thread_array_free(m, m->read);
	thread_array_free(m, m->write);
	while ((t = event_timer_list_pop(&m->timer)))
		thread_free(m, t);
	thread_list_free(m, &m->event);
	thread_list_free(m, &m->ready);
	thread_list_free(m, &m->unuse);
	pthread_mutex_destroy(&m->mtx);
	pthread_cond_destroy(&m->cancel_cond);
	close(m->io_pipe[0]);
	close(m->io_pipe[1]);
	list_delete(&m->cancel_req);
	m->cancel_req = NULL;

	while ((record = cpu_records_pop(m->cpu_records)))
		cpu_records_free(&record);
	cpu_records_fini(m->cpu_records);

	XFREE(MTYPE_EVENT_MASTER, m->name);
#if EPOLL_ENABLED
	struct frr_epoll_event *ev;
	uint32_t idx = 0;

	close(m->handler.epoll_fd);

	/* Free any remaining epoll objects */
	while ((ev = epoll_event_hash_pop_all(&(m->handler.epoll_event_hash), &idx)) != NULL) {
		/* Delete from "regular file" list if regular fd */
		if (CHECK_FLAG(ev->flags, FRR_EV_FD_IS_REGULAR))
			epoll_revent_list_del(&(m->handler.epoll_revents_list), ev);
		frr_epoll_event_del(&ev);
	}

	epoll_revent_list_fini(&(m->handler.epoll_revents_list));
	epoll_event_hash_fini(&(m->handler.epoll_event_hash));

	XFREE(MTYPE_EVENT_MASTER, m->handler.revents);
	XFREE(MTYPE_EVENT_MASTER, m->handler.fd_poll_counter);
#else
	XFREE(MTYPE_EVENT_MASTER, m->handler.pfds);
	XFREE(MTYPE_EVENT_MASTER, m->handler.copy);
#endif
	XFREE(MTYPE_EVENT_MASTER, m);
}

/* Return remain time in milliseconds. */
/* 返回定时器剩余毫秒数；对非定时器 / 未调度返回 0，剩余为负也归 0。 */
unsigned long event_timer_remain_msec(struct event *event)
{
	int64_t remain;

	if (!event_is_scheduled(event))
		return 0;

	frr_with_mutex (&event->mtx) {
		remain = monotime_until(&event->u.sands, NULL) / 1000LL;
	}

	return remain < 0 ? 0 : remain;
}

/* Return remain time in seconds. */
/* 返回定时器剩余秒数（由毫秒换算）。 */
unsigned long event_timer_remain_second(struct event *event)
{
	return event_timer_remain_msec(event) / 1000LL;
}

/* 返回定时器剩余时间（timeval）。注意这里不判空，传入非定时器需小心。 */
struct timeval event_timer_remain(struct event *event)
{
	struct timeval remain;

	frr_with_mutex (&event->mtx) {
		monotime_until(&event->u.sands, &remain);
	}
	return remain;
}

/* 把秒数格式化成 hh:mm:ss 写入 buf。返回非 0 表示未按 8 字节写出（异常），
 * 正常返回 0。要求 buf_size 至少 8。
 */
static int time_hhmmss(char *buf, int buf_size, long sec)
{
	long hh;
	long mm;
	int wr;

	assert(buf_size >= 8);

	hh = sec / 3600;
	sec %= 3600;
	mm = sec / 60;
	sec %= 60;

	wr = snprintf(buf, buf_size, "%02ld:%02ld:%02ld", hh, mm, sec);

	return wr != 8;
}

/* 把定时器剩余时间格式化成 hh:mm:ss 到 buf；传入 NULL 时输出 "--:--:--"。
 * 返回 buf 以便链式使用。
 */
char *event_timer_to_hhmmss(char *buf, int buf_size, struct event *t_timer)
{
	if (t_timer)
		time_hhmmss(buf, buf_size, event_timer_remain_second(t_timer));
	else
		snprintf(buf, buf_size, "--:--:--");

	return buf;
}

/* Get new thread.  */
/* 从 unuse 缓存取一个事件（没有则新建），填入类型、master、arg、回调、yield、
 * 迟到阈值等字段，并在回调或 funcname 变化时更新其 cpu 统计记录，递增活跃
 * 计数。返回初始化好的事件对象。注意这里不设置 ref。
 */
static struct event *event_get(struct event_loop *m, uint8_t type,
				void (*func)(struct event *), void *arg,
				const struct xref_eventsched *xref)
{
	struct event *event = event_list_pop(&m->unuse);

	if (!event) {
		event = XCALLOC(MTYPE_THREAD, sizeof(struct event));
		/* mutex only needs to be initialized at struct creation. */
		pthread_mutex_init(&event->mtx, NULL);
	}

	event->type = type;
	event->add_type = type;
	event->master = m;
	event->arg = arg;
	event->yield = EVENT_YIELD_TIME_SLOT; /* default */
	event->tardy_threshold = 0;
	/* event->ref is zeroed either by XCALLOC above or by memset before
	 * being put on the "unuse" list by thread_add_unuse().
	 * Setting it here again makes coverity complain about a missing
	 * lock :(
	 */
	/* event->ref = NULL; */

	/*
	 * So if the passed in funcname is not what we have
	 * stored that means the thread->hist needs to be
	 * updated.  We keep the last one around in unused
	 * under the assumption that we are probably
	 * going to immediately allocate the same
	 * type of thread.
	 * This hopefully saves us some serious
	 * hash_get lookups.
	 */
	if ((event->xref && event->xref->funcname != xref->funcname) || event->func != func)
		event->hist = cpu_records_get(m, func, xref->funcname);

	event->hist->total_active++;
	event->func = func;
	event->xref = xref;

	return event;
}

/* 真正释放一个事件对象：销毁其互斥量并释放内存。 */
static void thread_free(struct event_loop *master, struct event *event)
{
	/* Free allocated resources. */
	pthread_mutex_destroy(&event->mtx);
	XFREE(MTYPE_THREAD, event);
}

/* 后端轮询：调用 epoll_wait / ppoll / poll 等待 fd 就绪或超时，返回就绪的事件
 * 数。要点：
 *   - 进入前先释放 RCU 读锁、刷新线程局部日志缓冲；
 *   - 处理信号竞争：主线程先屏蔽关键信号，若已有待处理信号则直接返回；
 *   - poll 后端把唤醒管道 io_pipe[0] 临时作为最后一个 pollfd；
 *   - 返回后清空管道、按需修正就绪计数，再重新获取 RCU 读锁。
 * timer_wait 为等待超时（NULL 时由 selectpoll_timeout 决定或无限阻塞）；
 * *eintr_p 在因信号中断时置真。
 */
static int fd_poll(struct event_loop *m, const struct timeval *timer_wait,
		   bool *eintr_p)
{
	sigset_t origsigs;
	unsigned char trash[64];
#if !EPOLL_ENABLED
	nfds_t count = m->handler.copycount;
#endif

	/* number of file descriptors with events */
	int num;

	zlog_tls_buffer_flush();
	rcu_read_unlock();
	rcu_assert_read_unlocked();

#if !EPOLL_ENABLED
	/* add poll pipe poker */
	assert(count + 1 < m->handler.pfdsize);
	m->handler.copy[count].fd = m->io_pipe[0];
	m->handler.copy[count].events = POLLIN;
	m->handler.copy[count].revents = 0x00;
#endif

	/* We need to deal with a signal-handling race here: we
	 * don't want to miss a crucial signal, such as SIGTERM or SIGINT,
	 * that may arrive just before we enter poll(). We will block the
	 * key signals, then check whether any have arrived - if so, we return
	 * before calling poll(). If not, we'll re-enable the signals
	 * in the ppoll() call.
	 */

	sigemptyset(&origsigs);
	if (m->handle_signals) {
		/* Main pthread that handles the app signals */
		if (frr_sigevent_check(&origsigs)) {
			/* Signal to process - restore signal mask and return */
			pthread_sigmask(SIG_SETMASK, &origsigs, NULL);
			num = -1;
			*eintr_p = true;
			goto done;
		}
	} else {
		/* Don't make any changes for the non-main pthreads */
		pthread_sigmask(SIG_SETMASK, NULL, &origsigs);
	}

	/*
	 * Timeout computation. We use apis that take two different timeout
	 * forms. Some apis take a timeout scalar in milliseconds, with
	 * special meaning for the values '0' and '-1'. Some apis take a
	 * timespec with resolution in nanoseconds, and with a special
	 * meaning for NULL.
	 */
#if !EPOLL_ENABLED && defined(HAVE_PPOLL)
	/* Support for timeout via timespec */
	struct timespec ts, *tsp;

	if (timer_wait != NULL) {
		ts.tv_sec = timer_wait->tv_sec;
		ts.tv_nsec = timer_wait->tv_usec * 1000; /* microseconds to nanoseconds */
		tsp = &ts;
	} else {
		tsp = NULL; /* block indefinitely, because there is no timer to wait for */
	}
#else
	/*
	 * If timer_wait is null here, that means poll() should block
	 * indefinitely, unless the event_master has overridden it by setting
	 * ->selectpoll_timeout.
	 *
	 * If the value is positive, it specifies the maximum number of
	 * milliseconds to wait. If the timeout is zero, it specifies that
	 * we should never wait and return immediately even if no
	 * event is detected. If the value is -1, the call blocks indefinitely.
	 */
	int timeout = -1;

	if (timer_wait != NULL && m->selectpoll_timeout == 0) {
		/* Convert to millisecds */
		timeout = (timer_wait->tv_sec * 1000) + (timer_wait->tv_usec / 1000);
		/* Round up if there are only fractional usecs */
		if (timeout == 0 && timer_wait->tv_usec != 0)
			timeout = 1;
	} else if (m->selectpoll_timeout > 0) {
		/* use the user's timeout */
		timeout = m->selectpoll_timeout;
	} else if (m->selectpoll_timeout < 0) {
		/* effect a poll (return immediately) */
		timeout = 0;
	}
#endif /* timeout computation */

#if defined(USE_EPOLL) && defined(HAVE_EPOLL_PWAIT)
	num = epoll_pwait(m->handler.epoll_fd, m->handler.revents, m->handler.eventsize,
			  timeout, &origsigs);
	pthread_sigmask(SIG_SETMASK, &origsigs, NULL);
#elif defined(HAVE_PPOLL)
	num = ppoll(m->handler.copy, count + 1, tsp, &origsigs);
	pthread_sigmask(SIG_SETMASK, &origsigs, NULL);
#else
	/* Not ideal - there is a race after we restore the signal mask */
	pthread_sigmask(SIG_SETMASK, &origsigs, NULL);
	num = poll(m->handler.copy, count + 1, timeout);
#endif

done:

	if (num < 0 && errno == EINTR)
		*eintr_p = true;

	/* Drain the pipe */
	while (read(m->io_pipe[0], &trash, sizeof(trash)) > 0)
		;

		/* When poll() is used, we need to remove the io_pipe[0]
		 * from m->handler.copy and decreate "num" as fast as
		 * possible. Otherwise, when current thread is awakened,
		 * even if there is no ready I/O task, thread_process_io
		 * will still iterate over m->handler.copy until io_pipe[0]
		 * is find, which is inefficient.
		 *
		 * When epoll-APIs are used, we can postpone handling of
		 * io_pipe[0] in thread_process_io. In the case mentioned
		 * above, thread_process_io just need to iterate over a
		 * single-element m->handler.revents, which is much faster
		 * than poll() case (thanks to epoll_wait's behavior). Of
		 * course, removing io_pipe[0] from m->handler.revents is
		 * still a feasible choice. However, it is not as easy as
		 * removing the last element of m->handler.copy before, since
		 * we don't know where io_pipe[0] is located in m->handler.revents
		 * now. Only by traversing through m->handler.revents can we
		 * find io_pipe[0] and remove it. So, why don't we just postpone
		 * this traverse to thread_process_io to avoid an additional
		 * traverse?
		 */
#if !EPOLL_ENABLED
	if (num > 0 && m->handler.copy[count].revents != 0)
		num--;
#endif

	rcu_read_lock();

	return num;
}

#if EPOLL_ENABLED
/* epoll 平台上注册读 / 写事件的辅助函数（调用前须持有循环锁）。会把 fd 加入
 * epoll 集合并登记到哈希表；常规文件不能加入 epoll，改登记到常规事件链表。
 */
/*
 * Helper for event add read/write on epoll platforms; we expect the loop's lock
 * to be held.
 */
static void add_epoll_rw_helper(struct event_loop *m, int fd, int dir)
{
	struct frr_epoll_event set_ev = {};
	struct frr_epoll_event *hash_ev, *tmp_ev;
	struct stat fd_stat = {};
	bool fd_closed;
	int ret;
	bool is_regular = false;

	set_ev.ev.data.fd = fd;
	set_ev.ev.events = (dir == EVENT_READ ? EPOLLIN : EPOLLOUT);

	get_fd_stat(fd, &fd_stat, &fd_closed);
	if (S_ISREG(fd_stat.st_mode))
		is_regular = true;

	hash_ev = epoll_event_hash_find(&m->handler.epoll_event_hash, &set_ev);

	if (hash_ev) {
		/* Existing fd */

		/* Union epoll IN/OUT events */
		set_ev.ev.events |= hash_ev->ev.events;

		if (!is_regular) {
			if (epoll_ctl(m->handler.epoll_fd, EPOLL_CTL_MOD, fd,
				      &(set_ev.ev)) == -1) {
				/* Not regular file, modify the entry in the epoll set */
				if (errno == ENOENT) {
					/* The fd is already closed and removed from epoll
					 * set, but still in the hash table
					 * (fd is a zombie): reset .events of new set_ev
					 */
					set_ev.ev.events =
						(dir == EVENT_READ ? EPOLLIN : EPOLLOUT);
					if (epoll_ctl(m->handler.epoll_fd, EPOLL_CTL_ADD,
						      fd, &(set_ev.ev)) == -1) {
						/* Not regular file, add into the epoll set */
						zlog_debug("%s: EPOLL_CTL_MOD and EPOLL_CTL_ADD error, errno: %d",
							   __func__, errno);
						zlog_debug("[!] loop: %s | fd: %d",
							   m->name ? m->name : "", fd);
					}
				} else {
					zlog_debug("%s: EPOLL_CTL_MOD error, errno: %d, loop: %s, fd: %d",
						   __func__, errno, m->name ? m->name : "",
						   fd);
				}
			}
		}

		/* Modify existing hash element */
		hash_ev->ev.events = set_ev.ev.events;

	} else {
		/* New fd */
		if (!is_regular) {
			/* Not regular file, add into the epoll set */
			ret = epoll_ctl(m->handler.epoll_fd, EPOLL_CTL_ADD, fd,
					&set_ev.ev);
			if (ret == -1) {
				zlog_debug("%s: EPOLL_CTL_ADD error, errno: %d",
					   __func__, errno);
				zlog_debug("[!] loop: %s | fd: %d",
					   m->name ? m->name : "", fd);
			}
		}

		/* Add hash element */
		hash_ev = frr_epoll_event_new(fd, set_ev.ev.events);
		if (is_regular) {
			SET_FLAG(hash_ev->flags, FRR_EV_FD_IS_REGULAR);
			epoll_revent_list_add_tail(&m->handler.epoll_revents_list,
						   hash_ev);
		}

		tmp_ev = epoll_event_hash_add(&m->handler.epoll_event_hash, hash_ev);

		/* We just looked the item up, and the loop object is locked:
		 * don't expect to find it in the hash now
		 */
		assert(tmp_ev == NULL);
	}
}
#endif /* EPOLL */

/* 注册 / 更新一个 fd 的读或写事件（dir 由 xref->event_type 指定 READ / WRITE）。
 * 若 t_ptr 指向的事件已存在则不重复注册。epoll 后端调用 add_epoll_rw_helper；
 * poll 后端复用已有 pollfd 槽或追加新槽。随后用 event_get 取事件、填入 fd 并
 * 存入 m->read / m->write 数组，回填 *t_ptr 与 event->ref，最后 AWAKEN 唤醒循环。
 */
/* Add new read thread. */
void _event_add_read_write(const struct xref_eventsched *xref,
			   struct event_loop *m, void (*func)(struct event *),
			   void *arg, int fd, struct event **t_ptr)
{
	int dir = xref->event_type;
	struct event *event = NULL;
	struct event **thread_array;

	if (dir == EVENT_READ)
		frrtrace(9, frr_libfrr, schedule_read, m->name,
			 xref->funcname, xref->xref.file, xref->xref.line,
			 t_ptr, fd, 0, arg, 0);
	else
		frrtrace(9, frr_libfrr, schedule_write, m->name,
			 xref->funcname, xref->xref.file, xref->xref.line,
			 t_ptr, fd, 0, arg, 0);

	assert(fd >= 0);
	if (fd >= m->fd_limit)
		assert(!"Number of FD's open is greater than FRR currently configured to handle, aborting");

	frr_with_mutex (&m->mtx) {
		/* Thread is already scheduled; don't reschedule */
		if (t_ptr && *t_ptr)
			break;

#if EPOLL_ENABLED
		add_epoll_rw_helper(m, fd, dir);
#else
		/* default to a new pollfd */
		nfds_t queuepos = m->handler.pfdcount;

		/*
		 * if we already have a pollfd for our file descriptor, find and
		 * use it
		 */
		for (nfds_t i = 0; i < m->handler.pfdcount; i++) {
			if (m->handler.pfds[i].fd == fd) {
				queuepos = i;
				break;
			}
			/*
			 * We are setting the fd = -1 for the
			 * case when a read/write event is going
			 * away.  if we find a -1 we can stuff it
			 * into that spot, so note it
			 */
			if (m->handler.pfds[i].fd == -1 && queuepos == m->handler.pfdcount)
				queuepos = i;
		}

		/* make sure we have room for this fd + pipe poker fd */
		assert(queuepos + 1 < m->handler.pfdsize);

		m->handler.pfds[queuepos].fd = fd;
		m->handler.pfds[queuepos].events |=
			(dir == EVENT_READ ? POLLIN : POLLOUT);

		if (queuepos == m->handler.pfdcount)
			m->handler.pfdcount++;
#endif

		if (dir == EVENT_READ)
			thread_array = m->read;
		else
			thread_array = m->write;

#ifdef DEV_BUILD
		/*
		 * What happens if we have a thread already
		 * created for this event?
		 */
		if (thread_array[fd])
			assert(!"Thread already scheduled for file descriptor");
#endif

		event = event_get(m, dir, func, arg, xref);

		if (event) {
			frr_with_mutex (&event->mtx) {
				event->u.fd = fd;
				thread_array[event->u.fd] = event;
			}

			if (t_ptr) {
				*t_ptr = event;
				event->ref = t_ptr;
			}
		}

		AWAKEN(m);
	}
}

/* 注册定时器（timeval 相对时间）的内部实现：计算绝对到期时刻（当前时间 +
 * 相对值），在锁内取事件并加入定时器堆，若成为最早到期者则 AWAKEN 让循环
 * 重新计算等待超时；相对时间超过一年会告警。
 */
static void _event_add_timer_timeval(const struct xref_eventsched *xref,
				     struct event_loop *m,
				     void (*func)(struct event *), void *arg,
				     struct timeval *time_relative,
				     struct event **t_ptr)
{
	struct event *event;
	struct timeval t;

	assert(m != NULL);

	assert(time_relative);

	frrtrace(9, frr_libfrr, schedule_timer, m->name,
		 xref->funcname, xref->xref.file, xref->xref.line,
		 t_ptr, 0, 0, arg, (long)time_relative->tv_sec);

	/* Compute expiration/deadline time. */
	monotime(&t);
	timeradd(&t, time_relative, &t);

	frr_with_mutex (&m->mtx) {
		if (t_ptr && *t_ptr)
			/* thread is already scheduled; don't reschedule */
			return;

		event = event_get(m, EVENT_TIMER, func, arg, xref);
		/* default lateness warning: 4s */
		event->tardy_threshold = TARDY_DEFAULT_THRESHOLD;

		frr_with_mutex (&event->mtx) {
			event->u.sands = t;
			event_timer_list_add(&m->timer, event);
			if (t_ptr) {
				*t_ptr = event;
				event->ref = t_ptr;
			}
		}

		/* The timer list is sorted - if this new timer
		 * might change the time we'll wait for, give the pthread
		 * a chance to re-compute.
		 */
		if (event_timer_list_first(&m->timer) == event)
			AWAKEN(m);
	}
/* 一年的秒数，用于对过长的定时器给出告警。 */
#define ONEYEAR2SEC (60 * 60 * 24 * 365)
	if (time_relative->tv_sec > ONEYEAR2SEC)
		flog_err(EC_LIB_TIMER_TOO_LONG,
			 "Timer: %pTHD is created with an expiration that is greater than 1 year",
			 event);
}


/* Add timer event thread. */
/* 注册一个相对定时器（单位秒），转成 timeval 后交给内部实现。 */
void _event_add_timer(const struct xref_eventsched *xref, struct event_loop *m,
		      void (*func)(struct event *), void *arg, long timer,
		      struct event **t_ptr)
{
	struct timeval trel;

	assert(m != NULL);

	trel.tv_sec = timer;
	trel.tv_usec = 0;

	_event_add_timer_timeval(xref, m, func, arg, &trel, t_ptr);
}

/* Add timer event thread with "millisecond" resolution */
/* 注册一个相对定时器（单位毫秒），换算后交给内部实现。 */
void _event_add_timer_msec(const struct xref_eventsched *xref,
			   struct event_loop *m, void (*func)(struct event *),
			   void *arg, long timer, struct event **t_ptr)
{
	struct timeval trel;

	assert(m != NULL);

	trel.tv_sec = timer / 1000;
	trel.tv_usec = 1000 * (timer % 1000);

	_event_add_timer_timeval(xref, m, func, arg, &trel, t_ptr);
}

/* Add timer event thread with "timeval" resolution */
/* 注册一个相对定时器（直接给定 timeval）。 */
void _event_add_timer_tv(const struct xref_eventsched *xref,
			 struct event_loop *m, void (*func)(struct event *),
			 void *arg, struct timeval *tv, struct event **t_ptr)
{
	_event_add_timer_timeval(xref, m, func, arg, tv, t_ptr);
}

/* Add simple event thread. */
/* 注册一个一次性事件（EVENT_EVENT），val 由回调从事件里取出。若 t_ptr 已存在
 * 则不重复注册；成功后回填句柄与 ref 并 AWAKEN。
 */
void _event_add_event(const struct xref_eventsched *xref, struct event_loop *m,
		      void (*func)(struct event *), void *arg, int val,
		      struct event **t_ptr)
{
	struct event *event = NULL;

	frrtrace(9, frr_libfrr, schedule_event, m->name,
		 xref->funcname, xref->xref.file, xref->xref.line,
		 t_ptr, 0, val, arg, 0);

	assert(m != NULL);

	frr_with_mutex (&m->mtx) {
		if (t_ptr && *t_ptr)
			/* thread is already scheduled; don't reschedule */
			break;

		event = event_get(m, EVENT_EVENT, func, arg, xref);
		frr_with_mutex (&event->mtx) {
			event->u.val = val;
			event_list_add_tail(&m->event, event);
		}

		if (t_ptr) {
			*t_ptr = event;
			event->ref = t_ptr;
		}

		AWAKEN(m);
	}
}

/* 事件取消相关实现区。 */
/* Thread cancellation ------------------------------------------------------ */

/* epoll 版：清除某 fd 上指定的监听事件位（state 为 EPOLLIN / EPOLLOUT 或其相或）。
 * 若清除后不再监听任何事件，则把 fd 从 epoll 集合 / 哈希表（常规文件从链表）
 * 中彻底移除；否则更新 epoll 与哈希表中的事件位。参数 idx_hint 在 epoll 版未用。
 */
/**
 * NOT's out the .events field of pollfd corresponding to the given file
 * descriptor. The event to be NOT'd is passed in the 'state' parameter.
 *
 * This needs to happen for both copies of pollfd's. See 'event_fetch'
 * implementation for details.
 *
 * @param master
 * @param fd
 * @param state the event to cancel. One or more (OR'd together) of the
 * following:
 *   - POLLIN/EPOLLIN
 *   - POLLOUT/EPOLLOUT
 */
#if EPOLL_ENABLED
static void event_cancel_rw(struct event_loop *master, int fd, short state,
			    int idx_hint)
{
	struct frr_epoll_event set_ev = {};
	struct frr_epoll_event *hash_ev;
	struct stat fd_stat = {};
	bool fd_closed = false;
	bool is_regular = false;

	get_fd_stat(fd, &fd_stat, &fd_closed);

	set_ev.ev.data.fd = fd;
	hash_ev = epoll_event_hash_find(&(master->handler.epoll_event_hash), &set_ev);
	if (!hash_ev) {
		zlog_debug("[!] Received cancellation request for nonexistent rw job");
		zlog_debug("[!] loop: %s | fd: %d",
			   master->name ? master->name : "", fd);
		return;
	}

	is_regular = CHECK_FLAG(hash_ev->flags, FRR_EV_FD_IS_REGULAR);

	/* NOT to unset specified event bit. */
	set_ev.ev.events = hash_ev->ev.events &= ~(state);

	if (set_ev.ev.events == 0) {
		/* All events are canceled, unregister the fd */
		if (is_regular) {
			/* Remove from list */
			epoll_revent_list_del(&(master->handler.epoll_revents_list),
					      hash_ev);
		} else if (epoll_ctl(master->handler.epoll_fd, EPOLL_CTL_DEL, fd,
				     NULL) == -1) {
			/* Not regular file, remove the fd from the epoll set */
			if (errno != ENOENT && errno != EBADF) {
				zlog_debug("%s: EPOLL_CTL_DEL error, errno: %d",
					   __func__, errno);
				zlog_debug("[!] loop: %s | fd: %d",
					   master->name ? master->name : "", fd);
			}
		}

		/* Remove fd from hash table */
		epoll_event_hash_del(&(master->handler.epoll_event_hash), hash_ev);
		frr_epoll_event_del(&hash_ev);
	} else {
		/* Not all events are canceled */
		if (!is_regular) {
			if (epoll_ctl(master->handler.epoll_fd, EPOLL_CTL_MOD, fd,
				      &set_ev.ev) == -1) {
				/* Not regular file, update the fd's events
				 * from the epoll set
				 */
				zlog_debug("%s: EPOLL_CTL_MOD error, errno: %d", __func__, errno);
				zlog_debug("[!] loop: %s | fd: %d",
					   master->name ? master->name : "", fd);
			}
		}

		/* update the fd's events in the hash table. */
		hash_ev->ev.events = set_ev.ev.events;
	}
}
#else
/* poll 版：清除某 pollfd 上指定的事件位（state 或上 POLLHUP）。idx_hint >= 0
 * 时直接用该下标，否则在 pfds 中查找 fd。若某 pollfd 事件全被清除则从数组中
 * 移除（memmove 压缩）；对 copy 数组做同样处理（若其中也存在该 fd）。
 */
static void event_cancel_rw(struct event_loop *master, int fd, short state,
			    int idx_hint)
{
	bool found = false;

	/* find the index of corresponding pollfd */
	nfds_t i;

	/* Cancel POLLHUP too just in case some bozo set it */
	state |= POLLHUP;

	/* Some callers know the index of the pfd already */
	if (idx_hint >= 0) {
		i = idx_hint;
		found = true;
	} else {
		/* Have to look for the fd in the pfd array */
		for (i = 0; i < master->handler.pfdcount; i++)
			if (master->handler.pfds[i].fd == fd) {
				found = true;
				break;
			}
	}

	if (!found) {
		zlog_debug(
			"[!] Received cancellation request for nonexistent rw job");
		zlog_debug("[!] loop: %s | fd: %d",
			   master->name ? master->name : "", fd);
		return;
	}

	/* NOT out event. */
	master->handler.pfds[i].events &= ~(state);

	/* If all events are canceled, delete / resize the pollfd array. */
	if (master->handler.pfds[i].events == 0) {
		memmove(master->handler.pfds + i, master->handler.pfds + i + 1,
			(master->handler.pfdcount - i - 1)
				* sizeof(struct pollfd));
		master->handler.pfdcount--;
		master->handler.pfds[master->handler.pfdcount].fd = 0;
		master->handler.pfds[master->handler.pfdcount].events = 0;
	}

	/*
	 * If we have the same pollfd in the copy, perform the same operations,
	 * otherwise return.
	 */
	if (i >= master->handler.copycount)
		return;

	master->handler.copy[i].events &= ~(state);

	if (master->handler.copy[i].events == 0) {
		memmove(master->handler.copy + i, master->handler.copy + i + 1,
			(master->handler.copycount - i - 1)
				* sizeof(struct pollfd));
		master->handler.copycount--;
		master->handler.copy[master->handler.copycount].fd = 0;
		master->handler.copy[master->handler.copycount].events = 0;
	}
}
#endif

#if EPOLL_ENABLED
/* epoll 取消辅助：判断某 epoll 事件对应的读 / 写回调的 arg 是否等于给定的 arg。 */
/*
 * Helper for epoll task cancellation; check for match with cancelled 'arg' value.
 */
static bool epoll_cancel_arg_helper(struct frr_epoll_event *ev, struct event_loop *m,
				    void *arg)
{
	struct event *t;
	int fd;
	bool ret = false;

	fd = ev->ev.data.fd;
	if (fd == m->io_pipe[0] || fd == m->io_pipe[1])
		return ret;

	if (ev->ev.events & EPOLLIN)
		t = m->read[fd];
	else
		t = m->write[fd];

	if (t && t->arg == arg) {
		/* Found a match to cancel: clean up fd arrays */
		event_cancel_rw(m, fd, ev->ev.events, -1);

		/* Clean up thread arrays */
		m->read[fd] = NULL;
		m->write[fd] = NULL;

		/* Clear caller's ref */
		if (t->ref)
			*t->ref = NULL;

		thread_add_unuse(m, t);
		ret = true;
	}

	return ret;
}
#endif

/* 按 arg 取消：遍历各任务容器（event、ready、io、timer），把 arg 匹配的事件从
 * 相应容器摘除、清空调用方句柄并放回 unuse 缓存。若 cr->flags 带
 * EVENT_CANCEL_FLAG_READY，则只处理 event / ready 两条链表后返回。
 */
/*
 * Process task cancellation given a task argument: iterate through the
 * various lists of tasks, looking for any that match the argument.
 */
static void cancel_arg_helper(struct event_loop *master,
			      const struct cancel_req *cr)
{
	struct event *t;
#if EPOLL_ENABLED
	struct frr_epoll_event *ev;
#else
	nfds_t i;
	int fd;
	struct pollfd *pfd;
#endif

	/* We're only processing arg-based cancellations here. */
	if (cr->eventobj == NULL)
		return;

	/* First process the ready lists. */
	frr_each_safe (event_list, &master->event, t) {
		if (t->arg != cr->eventobj)
			continue;
		event_list_del(&master->event, t);
		if (t->ref)
			*t->ref = NULL;
		thread_add_unuse(master, t);
	}

	frr_each_safe (event_list, &master->ready, t) {
		if (t->arg != cr->eventobj)
			continue;
		event_list_del(&master->ready, t);
		if (t->ref)
			*t->ref = NULL;
		thread_add_unuse(master, t);
	}

	/* If requested, stop here and ignore io and timers */
	if (CHECK_FLAG(cr->flags, EVENT_CANCEL_FLAG_READY))
		return;

	/* Check the io tasks */
#if EPOLL_ENABLED
	frr_each_safe(epoll_event_hash, &(master->handler.epoll_event_hash), ev) {
		epoll_cancel_arg_helper(ev, master, cr->eventobj);
	}
#else
	for (i = 0; i < master->handler.pfdcount;) {
		pfd = master->handler.pfds + i;

		/*
		 * Skip this spot, nothing here to see
		 */
		if (pfd->fd == -1) {
			i++;
			continue;
		}

		if (pfd->events & POLLIN)
			t = master->read[pfd->fd];
		else
			t = master->write[pfd->fd];

		if (t && t->arg == cr->eventobj) {
			fd = pfd->fd;

			/* Found a match to cancel: clean up fd arrays */
			event_cancel_rw(master, pfd->fd, pfd->events, i);

			/* Clean up thread arrays */
			master->read[fd] = NULL;
			master->write[fd] = NULL;

			/* Clear caller's ref */
			if (t->ref)
				*t->ref = NULL;

			thread_add_unuse(master, t);

			/* Don't increment 'i' since the cancellation will have
			 * removed the entry from the pfd array
			 */
		} else
			i++;
	}
#endif

	/* Check the timer tasks */
	t = event_timer_list_first(&master->timer);
	while (t) {
		struct event *t_next;

		t_next = event_timer_list_next(&master->timer, t);

		if (t->arg == cr->eventobj) {
			event_timer_list_del(&master->timer, t);
			if (t->ref)
				*t->ref = NULL;
			thread_add_unuse(master, t);
		}

		t = t_next;
	}
}

/* 处理取消请求队列：仅允许由拥有该 event_master 的线程调用（须持 master->mtx）。
 * 对每个请求：若带 eventobj 走 arg 取消；否则按事件类型（读写 / 定时器 /
 * 事件 / 就绪）从对应容器摘除并释放。处理完清空请求链表，置 canceled 并广播
 * cancel_cond 唤醒 event_cancel_async 的等待者。
 */
/**
 * Process cancellation requests.
 *
 * This may only be run from the pthread which owns the event_master.
 *
 * @param master the thread master to process
 * @REQUIRE master->mtx
 */
static void do_event_cancel(struct event_loop *master)
{
	struct event_list_head *list = NULL;
	struct event **thread_array = NULL;
	struct event *event;
	struct cancel_req *cr;
	struct listnode *ln;

	for (ALL_LIST_ELEMENTS_RO(master->cancel_req, ln, cr)) {
		/*
		 * If this is an event object cancellation, search
		 * through task lists deleting any tasks which have the
		 * specified argument - use this handy helper function.
		 */
		if (cr->eventobj) {
			cancel_arg_helper(master, cr);
			continue;
		}

		/*
		 * The pointer varies depending on whether the cancellation
		 * request was made asynchronously or not. If it was, we
		 * need to check whether the thread even exists anymore
		 * before cancelling it.
		 */
		event = (cr->event) ? cr->event : *cr->threadref;

		if (!event)
			continue;

		list = NULL;
		thread_array = NULL;

		/* Determine the appropriate queue to cancel the thread from */
		switch (event->type) {
		case EVENT_READ:
#if EPOLL_ENABLED
			event_cancel_rw(master, event->u.fd, EPOLLIN, -1);
#else
			event_cancel_rw(master, event->u.fd, POLLIN, -1);
#endif
			thread_array = master->read;
			break;
		case EVENT_WRITE:
#if EPOLL_ENABLED
			event_cancel_rw(master, event->u.fd, EPOLLOUT, -1);
#else
			event_cancel_rw(master, event->u.fd, POLLOUT, -1);
#endif
			thread_array = master->write;
			break;
		case EVENT_TIMER:
			event_timer_list_del(&master->timer, event);
			break;
		case EVENT_EVENT:
			list = &master->event;
			break;
		case EVENT_READY:
			list = &master->ready;
			break;
		case EVENT_UNUSED:
		case EVENT_EXECUTE:
			continue;
			break;
		}

		if (list)
			event_list_del(list, event);
		else if (thread_array)
			thread_array[event->u.fd] = NULL;

		if (event->ref)
			*event->ref = NULL;

		thread_add_unuse(event->master, event);
	}

	/* Delete and free all cancellation requests */
	if (master->cancel_req)
		list_delete_all_node(master->cancel_req);

	/* Wake up any threads which may be blocked in event_cancel_async() */
	master->canceled = true;
	pthread_cond_broadcast(&master->cancel_cond);
}

/* arg 取消的公共入口：断言在所属线程调用；构造 cancel_req 入队后在锁内直接
 * 调用 do_event_cancel 处理。arg 为 NULL 时直接返回。
 */
/*
 * Helper function used for multiple flavors of arg-based cancellation.
 */
static void cancel_event_helper(struct event_loop *m, void *arg, int flags)
{
	struct cancel_req *cr;

	assert(m->owner == pthread_self());

	/* Only worth anything if caller supplies an arg. */
	if (arg == NULL)
		return;

	cr = XCALLOC(MTYPE_TMP, sizeof(struct cancel_req));

	cr->flags = flags;

	frr_with_mutex (&m->mtx) {
		cr->eventobj = arg;
		listnode_add(m->cancel_req, cr);
		do_event_cancel(m);
	}
}

/* 取消所有 arg 匹配的事件（含 io 与定时器）。非线程安全，须在所属线程调用。 */
/**
 * Cancel any events which have the specified argument.
 *
 * MT-Unsafe
 *
 * @param m the event_master to cancel from
 * @param arg the argument passed when creating the event
 */
void event_cancel_event(struct event_loop *master, void *arg)
{
	cancel_event_helper(master, arg, 0);
}

/* 只取消就绪（event / ready 链表）中 arg 匹配的任务。非线程安全。 */
/*
 * Cancel ready tasks with an arg matching 'arg'
 *
 * MT-Unsafe
 *
 * @param m the event_master to cancel from
 * @param arg the argument passed when creating the event
 */
void event_cancel_event_ready(struct event_loop *m, void *arg)
{

	/* Only cancel ready/event tasks */
	cancel_event_helper(m, arg, EVENT_CANCEL_FLAG_READY);
}

/* 取消一个具体事件（句柄指针，安全处理 NULL）。非线程安全，须在事件所属线程
 * 调用。内部构造 cancel_req 入队并立即 do_event_cancel 处理，最后清空调用方
 * 的句柄 *event。
 */
/**
 * Cancel a specific task.
 *
 * MT-Unsafe
 *
 * @param thread task to cancel
 */
void event_cancel(struct event **event)
{
	struct event_loop *master;

	if (event == NULL || *event == NULL)
		return;

	master = (*event)->master;

	frrtrace(9, frr_libfrr, event_cancel, master->name, (*event)->xref->funcname,
		 (*event)->xref->xref.file, (*event)->xref->xref.line, NULL, (*event)->u.fd,
		 (*event)->u.val, (*event)->arg, (*event)->u.sands.tv_sec);

	assert(master->owner == pthread_self());

	frr_with_mutex (&master->mtx) {
		struct cancel_req *cr =
			XCALLOC(MTYPE_TMP, sizeof(struct cancel_req));
		cr->event = *event;
		listnode_add(master->cancel_req, cr);
		do_event_cancel(master);

		*event = NULL;
	}
}

/* 异步取消：可由任意线程调用（MT-Safe）。根据参数要么针对事件句柄（thread），
 * 要么针对 arg（eventobj），两者互斥。把 cancel_req 放入目标循环队列并 AWAKEN
 * 唤醒其主线程，然后阻塞等待 master->canceled 被置真（目标线程处理完取消）。
 * 返回前把 *thread 置空。若目标事件已不存在，取消是空操作。
 */
/**
 * Asynchronous cancellation.
 *
 * Called with either a struct event ** or void * to an event argument,
 * this function posts the correct cancellation request and blocks until it is
 * serviced.
 *
 * If the thread is currently running, execution blocks until it completes.
 *
 * The last two parameters are mutually exclusive, i.e. if you pass one the
 * other must be NULL.
 *
 * When the cancellation procedure executes on the target event_master, the
 * thread * provided is checked for nullity. If it is null, the thread is
 * assumed to no longer exist and the cancellation request is a no-op. Thus
 * users of this API must pass a back-reference when scheduling the original
 * task.
 *
 * MT-Safe
 *
 * @param master the thread master with the relevant event / task
 * @param thread pointer to thread to cancel
 * @param eventobj the event
 */
void event_cancel_async(struct event_loop *master, struct event **thread,
			void *eventobj)
{
	assert(!(thread && eventobj) && (thread || eventobj));

	if (!thread)
		frrtrace(9, frr_libfrr, event_cancel_async, master->name, NULL, NULL,
			 0, NULL, 0, 0, eventobj, 0);

	assert(master->owner != pthread_self());

	frr_with_mutex (&master->mtx) {
		master->canceled = false;

		if (thread) {
			if (*thread)
				frrtrace(9, frr_libfrr, event_cancel_async,
					 master->name, (*thread)->xref->funcname,
					 (*thread)->xref->xref.file,
					 (*thread)->xref->xref.line, NULL,
					 (*thread)->u.fd, (*thread)->u.val,
					 (*thread)->arg,
					 (*thread)->u.sands.tv_sec);
			struct cancel_req *cr =
				XCALLOC(MTYPE_TMP, sizeof(struct cancel_req));
			cr->threadref = thread;
			listnode_add(master->cancel_req, cr);
		} else if (eventobj) {
			struct cancel_req *cr =
				XCALLOC(MTYPE_TMP, sizeof(struct cancel_req));
			cr->eventobj = eventobj;
			listnode_add(master->cancel_req, cr);
		}
		AWAKEN(master);

		while (!master->canceled)
			pthread_cond_wait(&master->cancel_cond, &master->mtx);
	}

	if (thread)
		*thread = NULL;
}
/* ------------------------------------------------------------------------- */

/* 计算下一次等待超时：若定时器堆为空返回 NULL（无限等待），否则取最早到期
 * 定时器，算出相对当前的剩余时间写入 timer_val 并返回。
 */
static struct timeval *thread_timer_wait(struct event_timer_list_head *timers,
					 struct timeval *timer_val)
{
	if (!event_timer_list_count(timers))
		return NULL;

	struct event *next_timer = event_timer_list_first(timers);

	monotime_until(&next_timer->u.sands, timer_val);
	return timer_val;
}

/* 把从调度容器取出的 event 复制到 fetch 供应用执行，原 event 放回 unuse 缓存，
 * 返回 fetch（指向调用方提供的临时事件副本）。
 */
static struct event *event_run(struct event_loop *m, struct event *event, struct event *fetch)
{
	*fetch = *event;
	thread_add_unuse(m, event);
	return fetch;
}

/* 下面给出两套 io 处理循环：EPOLL 版与 POLL 版。 */
/*
 * Note two versions of the io handling loops: one for EPOLL, and one for the POLL apis.
 */
#if EPOLL_ENABLED
/* epoll 版：处理某 fd 的一个就绪 io 事件（state 为 EPOLLIN / EPOLLOUT）。先把
 * 该方向从 epoll 集合 / 哈希表清除（避免对不吃的事件反复触发），再把对应读 /
 * 写事件转入 ready 队列并置类型为 EVENT_READY；无事件时按情况打错误日志。
 * 返回 1 表示有事件被处理。
 */
static int thread_process_io_helper(struct event_loop *m, struct event *event,
				    short state, short actual_state,
				    int fd, struct frr_epoll_event *hash_ev)
{
	struct event **thread_array;
	struct epoll_event set_ev = {};
	bool is_regular = CHECK_FLAG(hash_ev->flags, FRR_EV_FD_IS_REGULAR);

	/*
	 * Clear the events corresponding to "state" in the FRR structs and
	 * the epoll set.
	 *
	 * This cleans up a possible infinite loop where we refuse
	 * to respond to a epoll event but epoll is insistent that
	 * we should.
	 */
	set_ev.data.fd = fd;
	set_ev.events = hash_ev->ev.events & ~(state);

	if (!is_regular) {
		if (epoll_ctl(m->handler.epoll_fd, EPOLL_CTL_MOD, fd, &set_ev) == -1) {
			/* Not regular file, update the fd's events
			 * from the epoll set
			 */
			zlog_debug("%s: EPOLL_CTL_MOD error, errno: %d", __func__, errno);
			zlog_debug("[!] loop: %s | fd: %d", m->name ? m->name : "", fd);
		}
	}

	/* update the fd's events in the hash table. */
	hash_ev->ev.events = set_ev.events;

	if (!event) {
		if ((actual_state & (EPOLLHUP | EPOLLIN)) != EPOLLHUP)
			flog_err(EC_LIB_NO_THREAD,
				 "Attempting to process an I/O event but for fd: %d(%d) no event to handle this!",
				 fd, actual_state);
		return 0;
	}

	/* Schedule ready event back to application */
	if (state == EPOLLIN)
		thread_array = m->read;
	else
		thread_array = m->write;

	thread_array[fd] = NULL;
	event_list_add_tail(&m->ready, event);
	event->type = EVENT_READY;

	return 1;
}

/* epoll 版：处理单个 epoll 结果项。跳过唤醒管道 fd；对事件先查哈希表（查不到
 * 说明已被取消，忽略为 stale）；遇到 fd 关闭或 EPOLLHUP / ERR / RDHUP 则把读 /
 * 写事件都转入 ready（表示连接结束），并清理 epoll / 哈希表；正常情况按
 * EPOLLIN / EPOLLOUT 调 thread_process_io_helper。
 */
static inline void thread_process_io_inner_loop(struct event_loop *m,
						const struct epoll_event *revent)
{
	struct frr_epoll_event *hash_ev;
	struct frr_epoll_event set_ev = {};
	int fd;
	struct stat fd_stat = {};
	bool fd_closed = false;
	int ret;
	struct event *event;
	bool is_regular = false;

	fd = revent->data.fd;
	m->handler.fd_poll_counter[fd] += 1;
	if (fd == m->io_pipe[0])
		return;

	get_fd_stat(fd, &fd_stat, &fd_closed);

	set_ev.ev.data.fd = fd;
	hash_ev = epoll_event_hash_find(&(m->handler.epoll_event_hash), &set_ev);
	if (!hash_ev) {
		/*
		 * This fd was already cancelled and removed from the
		 * epoll_event_hash before fd_poll() was called, but
		 * the kernel had already queued this revent for the fd.
		 * Nothing left to process.
		 */
		zlog_debug("%s: stale epoll event on fd %d (events 0x%x)", m->name ? m->name : "",
			   fd, revent->events);
		return;
	}

	is_regular = CHECK_FLAG(hash_ev->flags, FRR_EV_FD_IS_REGULAR);

	/* Process the I/O event. Handle errors also, which may occur for READ or WRITE
	 * events.
	 */

	/*
	 * Error detected: notify the application code appropriately,
	 * and remove the fd from regular/epoll set, and hash table.
	 */
	if (fd_closed || (revent->events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))) {
		/* Return any application tasks back to the application. */
		event = m->read[fd];
		if (event) {
			m->read[fd] = NULL;
			event->type = EVENT_READY;
			event_list_add_tail(&m->ready, event);
		}
		event = m->write[fd];
		if (event) {
			m->write[fd] = NULL;
			event->type = EVENT_READY;
			event_list_add_tail(&m->ready, event);
		}

		if (is_regular) {
			/* Regular file, remove the fd from list */
			epoll_revent_list_del(&(m->handler.epoll_revents_list), hash_ev);
		} else {
			/* Not regular file, remove the fd from the epoll set */
			ret = epoll_ctl(m->handler.epoll_fd, EPOLL_CTL_DEL, fd, NULL);

			if (!fd_closed && ret == -1 && errno != EBADF && errno != ENOENT) {
				zlog_debug("%s: EPOLL_CTL_DEL error, errno: %d",
					   __func__, errno);
				zlog_debug("[!] loop: %s | fd: %d",
					   m->name ? m->name : "", fd);
			}
		}

		/* Remove fd from hash table */
		epoll_event_hash_del(&(m->handler.epoll_event_hash), hash_ev);
		frr_epoll_event_del(&hash_ev);
		return;
	}

	if (revent->events & (EPOLLIN))
		thread_process_io_helper(m, m->read[fd], EPOLLIN, revent->events,
					 fd, hash_ev);

	if (revent->events & (EPOLLOUT)) {
		thread_process_io_helper(m, m->write[fd], EPOLLOUT, revent->events,
					 fd, hash_ev);
	}
}

/* epoll 版入口：先处理常规文件事件链表，再处理 epoll_wait 返回的 num 个事件。 */
/**
 * Process I/O events.
 *
 * @param m the thread master
 * @param num return value of epoll_wait()
 */
static void thread_process_io(struct event_loop *m, int num)
{
	int i;
	struct frr_epoll_event *ev;

	/* First, handle regular file I/O events in regular events list. */
	frr_each (epoll_revent_list, &m->handler.epoll_revents_list, ev)
		thread_process_io_inner_loop(m, &(ev->ev));

	/* Second, handle I/O events in m->handler.revents which are returned by
	 * epoll_wait().
	 */
	for (i = 0; i < num; ++i)
		thread_process_io_inner_loop(m, m->handler.revents + i);
}
#else
/*
 * version for poll family of apis
 */
/* poll 版：处理某 pollfd（下标 pos）的一个就绪 io 事件。同步 pfds 与 copy 的
 * 事件位、必要时把该槽 fd 置 -1，再把对应事件转入 ready 队列。返回 1 表示有
 * 事件被处理。
 */
static int thread_process_io_helper(struct event_loop *m, struct event *event,
				    short state, short actual_state, int pos)
{
	struct event **thread_array;

	/*
	 * poll() clears the .events field, but the pollfd array we
	 * pass to poll() is a copy of the one used to schedule threads.
	 * We need to synchronize state between the two here by applying
	 * the same changes poll() made on the copy of the "real" pollfd
	 * array.
	 *
	 * This cleans up a possible infinite loop where we refuse
	 * to respond to a poll event but poll is insistent that
	 * we should.
	 */
	m->handler.pfds[pos].events &= ~(state);
	/*
	 * ppoll man page says that a fd of -1 causes the particular
	 * array item to be skipped.  So let's skip it
	 */
	if (m->handler.pfds[pos].events == 0)
		m->handler.pfds[pos].fd = -1;

	if (!event) {
		if ((actual_state & (POLLHUP|POLLIN)) != POLLHUP)
			flog_err(EC_LIB_NO_THREAD,
				 "Attempting to process an I/O event but for fd: %d(%d) no event to handle this!",
				 m->handler.pfds[pos].fd, actual_state);
		return 0;
	}

	if (event->type == EVENT_READ)
		thread_array = m->read;
	else
		thread_array = m->write;

	thread_array[event->u.fd] = NULL;
	event_list_add_tail(&m->ready, event);
	event->type = EVENT_READY;

	return 1;
}

/* poll 版：处理 copy 数组中下标 *i 的 pollfd。revents 为空则跳过；对
 * POLLIN / POLLHUP / POLLERR 按读事件处理，对 POLLOUT 按写事件处理（都调用
 * thread_process_io_helper）；若 revents 含 POLLNVAL 则把该槽从 pfds 与 copy 中
 * 移除以清理非法 fd，并回退下标 *i。
 */
static inline void thread_process_io_inner_loop(struct event_loop *m,
						unsigned int num,
						struct pollfd *pfds, nfds_t *i,
						uint32_t *ready)
{
	/* no event for current fd? immediately continue */
	if (pfds[*i].revents == 0)
		return;

	*ready = *ready + 1;

	/*
	 * Unless someone has called event_cancel from another
	 * pthread, the only thing that could have changed in
	 * m->handler.pfds while we were asleep is the .events
	 * field in a given pollfd. Barring event_cancel() that
	 * value should be a superset of the values we have in our
	 * copy, so there's no need to update it. Similarily,
	 * barring deletion, the fd should still be a valid index
	 * into the master's pfds.
	 *
	 * We are including POLLERR here to do a READ event
	 * this is because the read should fail and the
	 * read function should handle it appropriately
	 */
	if (pfds[*i].revents & (POLLIN | POLLHUP | POLLERR)) {
		thread_process_io_helper(m, m->read[pfds[*i].fd], POLLIN,
					 pfds[*i].revents, *i);
	}
	if (pfds[*i].revents & POLLOUT)
		thread_process_io_helper(m, m->write[pfds[*i].fd], POLLOUT,
					 pfds[*i].revents, *i);

	/*
	 * if one of our file descriptors is garbage, remove the same
	 * from both pfds + update sizes and index
	 */
	if (pfds[*i].revents & POLLNVAL) {
		memmove(m->handler.pfds + *i, m->handler.pfds + *i + 1,
			(m->handler.pfdcount - *i - 1) * sizeof(struct pollfd));
		m->handler.pfdcount--;
		m->handler.pfds[m->handler.pfdcount].fd = 0;
		m->handler.pfds[m->handler.pfdcount].events = 0;

		memmove(pfds + *i, pfds + *i + 1,
			(m->handler.copycount - *i - 1) * sizeof(struct pollfd));
		m->handler.copycount--;
		m->handler.copy[m->handler.copycount].fd = 0;
		m->handler.copy[m->handler.copycount].events = 0;

		*i = *i - 1;
	}
}

/* poll 版入口：轮询 copy 中的 pollfd，跳过无效 fd，对就绪的逐个处理。为了公平，
 * 起点用 m->last_read 在数组上循环（先走 last_read..末尾，再走开头..），处理完
 * 把 last_read 加一，使下次轮询从不同 fd 开始，避免固定顺序导致的饥饿。
 */
/**
 * Process I/O events.
 *
 * Walks through file descriptor array looking for those pollfds whose .revents
 * field has something interesting. Deletes any invalid file descriptors.
 *
 * Try to impart some impartiality to handling of io.  The event
 * system will cycle through the fd's available for io
 * giving each one a chance to go first.
 *
 * @param m the thread master
 * @param num the number of active file descriptors (return value of poll())
 */
static void thread_process_io(struct event_loop *m, unsigned int num)
{
	unsigned int ready = 0;
	struct pollfd *pfds = m->handler.copy;
	nfds_t i, last_read = m->last_read % m->handler.copycount;

	for (i = last_read; i < m->handler.copycount && ready < num; ++i)
		thread_process_io_inner_loop(m, num, pfds, &i, &ready);

	for (i = 0; i < last_read && ready < num; ++i)
		thread_process_io_inner_loop(m, num, pfds, &i, &ready);

	m->last_read++;
}
#endif

/* Add all timers that have popped to the ready list. */
/* 把所有已到期的定时器从堆中弹出并转入 ready 队列（按到期时间顺序），返回转
 * 入的数量。
 */
static unsigned int thread_process_timers(struct event_loop *m,
					  struct timeval *timenow)
{
	struct event *event;
	unsigned int ready = 0;

	while ((event = event_timer_list_first(&m->timer))) {
		if (timercmp(timenow, &event->u.sands, <))
			break;

		event_timer_list_pop(&m->timer);
		event->type = EVENT_READY;
		event_list_add_tail(&m->ready, event);
		ready++;
	}

	return ready;
}

/* process a list en masse, e.g. for event thread lists */
/* 把一条事件链表整体搬到所属 master 的 ready 队列（类型改为 EVENT_READY），
 * 返回搬迁数量。常用于把 m->event（一次性事件）一次性就绪化。
 */
static unsigned int thread_process(struct event_list_head *list)
{
	struct event *event;
	unsigned int ready = 0;

	while ((event = event_list_pop(list))) {
		event->type = EVENT_READY;
		event_list_add_tail(&event->master->ready, event);
		ready++;
	}
	return ready;
}

/* 取事件的核心循环（须在所属线程运行）。流程：
 *   - 先处理信号（若是信号处理循环）；
 *   - 处理挂起的取消请求 do_event_cancel；
 *   - 若 ready 队列非空，直接取一个事件运行并返回（性能关键路径）；
 *   - 否则把 m->event 里的事件搬入 ready；
 *   - 计算等待超时：有待处理事件则超时 0；否则取最早定时器剩余时间；都无则无限
 *     阻塞；若既无定时器也无事件 / fd 则判定“无事可做”直接退出（die）；
 *   - 释放锁后调用 fd_poll 等待，再取锁，处理出错与中断；
 *   - 把到期定时器与就绪 io 迁入 ready。
 * 通过 broken / continued 告知调用者：是否结束或需重来一轮。
 */
static void event_fetch_inner_loop(struct event_loop *m, struct event *event,
				   struct event *fetch, bool *broken,
				   bool *continued)
{
	struct timeval now;
	struct timeval zerotime = {0, 0};
	struct timeval tv;
	struct timeval *tw = NULL;
	bool eintr_p = false;
	int num = 0;

	/* Handle signals if any */
	if (m->handle_signals)
		frr_sigevent_process();

	pthread_mutex_lock(&m->mtx);

	/* Process any pending cancellation requests */
	do_event_cancel(m);

	/*
	 * Attempt to flush ready queue before going into poll().
	 * This is performance-critical. Think twice before modifying.
	 */
	if ((event = event_list_pop(&m->ready))) {
		fetch = event_run(m, event, fetch);
		if (fetch->ref)
			*fetch->ref = NULL;
		pthread_mutex_unlock(&m->mtx);
		if (!m->ready_run_loop)
			GETRUSAGE(&m->last_getrusage);
		m->ready_run_loop = true;
		*broken = true;
		return;
	}

	m->ready_run_loop = false;
	/* otherwise, tick through scheduling sequence */

	/*
	 * Post events to ready queue. This must come before the
	 * following block since events should occur immediately
	 */
	thread_process(&m->event);

	/*
	 * If there are no tasks on the ready queue, we will poll()
	 * until a timer expires or we receive I/O, whichever comes
	 * first. The strategy for doing this is:
	 *
	 * - If there are events pending, set the poll() timeout to zero
	 * - If there are no events pending, but there are timers
	 * pending, set the timeout to the smallest remaining time on
	 * any timer.
	 * - If there are neither timers nor events pending, but there
	 * are file descriptors pending, block indefinitely in poll()
	 * - If nothing is pending, it's time for the application to die
	 *
	 * In every case except the last, we need to hit poll() at least
	 * once per loop to avoid starvation by events
	 */
	if (!event_list_count(&m->ready))
		tw = thread_timer_wait(&m->timer, &tv);

	if (event_list_count(&m->ready) || (tw && !timercmp(tw, &zerotime, >)))
		tw = &zerotime;

#if EPOLL_ENABLED
	if (!tw && epoll_revent_list_count(&m->handler.epoll_revents_list) == 0 &&
	    epoll_event_hash_count(&m->handler.epoll_event_hash) == 0) { /* die */
		pthread_mutex_unlock(&m->mtx);
		fetch = NULL;
		*broken = true;
		return;
	}
#else
	if (!tw && m->handler.pfdcount == 0) { /* die */
		pthread_mutex_unlock(&m->mtx);
		fetch = NULL;
		*broken = true;
		return;
	}
#endif

#if !EPOLL_ENABLED
	/*
	 * Copy pollfd array + # active pollfds in it. Not necessary to
	 * copy the array size as this is fixed.
	 */
	m->handler.copycount = m->handler.pfdcount;
	memcpy(m->handler.copy, m->handler.pfds,
	       m->handler.copycount * sizeof(struct pollfd));
#endif

	pthread_mutex_unlock(&m->mtx);
	{
		eintr_p = false;
		num = fd_poll(m, tw, &eintr_p);
	}
	pthread_mutex_lock(&m->mtx);

	/* Handle any errors received in poll()/epoll_wait() */
	if (num < 0) {
		if (eintr_p) {
			pthread_mutex_unlock(&m->mtx);
			/* loop around to signal handler */
			*continued = true;
			return;
		}

		/* else die */
#if EPOLL_ENABLED
		flog_err(EC_LIB_SYSTEM_CALL, "epoll_wait() error: %s",
			 safe_strerror(errno));
#else
		flog_err(EC_LIB_SYSTEM_CALL, "poll() error: %s",
			 safe_strerror(errno));
#endif
		pthread_mutex_unlock(&m->mtx);
		fetch = NULL;
		*broken = true;
		return;
	}

	/* Post timers to ready queue. */
	monotime(&now);
	thread_process_timers(m, &now);

	/* Post I/O to ready queue. */
	if (num > 0)
		thread_process_io(m, num);

	pthread_mutex_unlock(&m->mtx);
}

/* Fetch next ready thread. */
/* 取下一个就绪事件的主入口：循环调用 event_fetch_inner_loop，直到拿到事件、
 * 判定结束（broken）或需要继续（continued）。spin 控制是否持续重试。返回 fetch。
 */
struct event *event_fetch(struct event_loop *m, struct event *fetch)
{
	struct event *event = NULL;
	bool broken = false;
	bool continued = false;

	do {
		broken = false;
		continued = false;
		event_fetch_inner_loop(m, event, fetch, &broken, &continued);
		if (broken)
			break;
		if (continued)
			continue;
	} while (!event && m->spin);

	return fetch;
}

/* 计算自 start 到 now 的墙钟耗时（微秒），并通过 cputime 返回 CPU 耗时。
 * 对 FreeBSD 做了修正，避免时间回退导致的巨大差值。
 */
unsigned long event_consumed_time(RUSAGE_T *now, RUSAGE_T *start,
				  unsigned long *cputime)
{
#ifdef __FreeBSD__
	/*
	 * FreeBSD appears to have an issue when calling clock_gettime
	 * with CLOCK_THREAD_CPUTIME_ID really close to each other
	 * occassionally the now time will be before the start time.
	 * This is not good and FRR is ending up with CPU HOG's
	 * when the subtraction wraps to very large numbers
	 *
	 * What we are going to do here is cheat a little bit
	 * and notice that this is a problem and just correct
	 * it so that it is impossible to happen
	 */
	if (start->cpu.tv_sec == now->cpu.tv_sec &&
	    start->cpu.tv_nsec > now->cpu.tv_nsec)
		now->cpu.tv_nsec = start->cpu.tv_nsec + 1;
	else if (start->cpu.tv_sec > now->cpu.tv_sec) {
		now->cpu.tv_sec = start->cpu.tv_sec;
		now->cpu.tv_nsec = start->cpu.tv_nsec + 1;
	}
#endif
	*cputime = (now->cpu.tv_sec - start->cpu.tv_sec) * TIMER_SECOND_MICRO +
		   (now->cpu.tv_nsec - start->cpu.tv_nsec) / 1000;
	return timeval_elapsed(now->real, start->real);
}

/* 判断事件是否已运行到该让出 CPU：用墙钟时间衡量（默认 EVENT_YIELD_TIME_SLOT
 * 毫秒），超过则返回真。用墙钟而非 CPU 时间的理由见原注释。
 */
/*
 * We should aim to yield after yield milliseconds, which defaults
 * to EVENT_YIELD_TIME_SLOT .
 * Note: we are using real (wall clock) time for this calculation.
 * It could be argued that CPU time may make more sense in certain
 * contexts.  The things to consider are whether the thread may have
 * blocked (in which case wall time increases, but CPU time does not),
 * or whether the system is heavily loaded with other processes competing
 * for CPU time.  On balance, wall clock time seems to make sense.
 * Plus it has the added benefit that gettimeofday should be faster
 * than calling getrusage.
 */
int event_should_yield(struct event *event)
{
	int result;

	frr_with_mutex (&event->mtx) {
		result = monotime_since(&event->real, NULL)
			 > (int64_t)event->yield;
	}
	return result;
}

/* 设置事件的让出时间（微秒，加锁修改 event->yield）。 */
void event_set_yield_time(struct event *event, unsigned long yield_time)
{
	frr_with_mutex (&event->mtx) {
		event->yield = yield_time;
	}
}

/* 取当前资源使用量：real 始终填墙钟时间；cpu 时间仅在 cputime_enabled 时用
 * CLOCK_THREAD_CPUTIME_ID 读取，否则清零。
 */
void event_getrusage(RUSAGE_T *r)
{
	monotime(&r->real);
	if (!cputime_enabled) {
		memset(&r->cpu, 0, sizeof(r->cpu));
		return;
	}

	/* not currently implemented in Linux's vDSO, but maybe at some point
	 * in the future?
	 */
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &r->cpu);
}

/* 打印一次“定时器迟到”告警（含系统负载均值），用于诊断 CPU 饥饿 / 系统过载。
 * 由 event_call 在迟到超过阈值且过了限速间隔时调用。
 */
static void event_tardy_warn(struct event *event, unsigned long since_us)
{
	char buf[64];
	struct fbuf fb = { .buf = buf, .pos = buf, .len = sizeof(buf) };
	double loadavg[3];
	int rv;

#ifdef HAVE_GETLOADAVG
	rv = getloadavg(loadavg, array_size(loadavg));
#else
	rv = -1;
#endif
	if (rv < 0)
		bprintfrr(&fb, "not available");
	else {
		for (int i = 0; i < rv; i++) {
			bprintfrr(&fb, "%.2f", loadavg[i]);
			if (i < rv - 1)
				bputs(&fb, ", ");
		}
	}

	flog_warn(EC_LIB_STARVE_THREAD,
		  "CPU starvation: %pTHD getting executed %lums late, warning threshold %lums. System load: %pFB",
		  event, (since_us + 999) / 1000, (event->tardy_threshold + 999) / 1000, &fb);
}

/* 执行一个事件：这是事件回调真正被调用的地方，同时负责统计。
 *   - 若为定时器且迟到超过 tardy_threshold，按限速间隔打印迟到告警；
 *   - 记录执行前的时间基准（复用上次 rusage 以省一次系统调用）；
 *   - 把当前事件存入线程局部键 thread_current，调用回调，再清空；
 *   - 取执行后 rusage，累加 / 更新该回调的墙钟与 cpu 的 total 与 max，
 *     递增调用计数并或上事件类型位；
 *   - 若 CPU 或墙钟耗时超过阈值则打印 CPU HOG 或 STARVATION 告警
 *     （suppress_warnings 为真时跳过告警）。max 用 CAS 保证单调递增。
 */
/*
 * Call a thread.
 *
 * This function will atomically update the thread's usage history. At present
 * this is the only spot where usage history is written. Nevertheless the code
 * has been written such that the introduction of writers in the future should
 * not need to update it provided the writers atomically perform only the
 * operations done here, i.e. updating the total and maximum times. In
 * particular, the maximum real and cpu times must be monotonically increasing
 * or this code is not correct.
 */
void event_call(struct event *event)
{
	RUSAGE_T before, after;
	bool suppress_warnings = EVENT_ARG(event);

	if (event->tardy_threshold) {
		int64_t timer_late_us = monotime_since(&event->u.sands, NULL);

		/* Timers have a tardiness warning defaulting to 4s.
		 * It can be customized with event_set_tardy_threshold()
		 * (bfdd does that since the protocol has really short timers)
		 *
		 * If we are more than that threshold late, print a warning
		 * since we're running behind in calling timers (probably due
		 * to high system load.)
		 */
		if (timer_late_us > (int64_t)event->tardy_threshold) {
			int64_t since_last_warning;
			struct timeval *tw;

			atomic_fetch_add_explicit(&event->hist->total_starv_warn, 1,
						  memory_order_seq_cst);

			tw = &event->master->last_tardy_warning;
			since_last_warning = monotime_since(tw, NULL);
			if (since_last_warning > TARDY_WARNING_INTERVAL) {
				event_tardy_warn(event, timer_late_us);
				monotime(tw);
			}
		}
	}

	/* if the event being called is the CLI, it may change cputime_enabled
	 * ("service cputime-stats" command), which can result in nonsensical
	 * and very confusing warnings
	 */
	bool cputime_enabled_here = cputime_enabled;

	if (event->master->ready_run_loop)
		before = event->master->last_getrusage;
	else
		GETRUSAGE(&before);

	event->real = before.real;

	frrtrace(9, frr_libfrr, event_call, event->master->name,
		 event->xref->funcname, event->xref->xref.file,
		 event->xref->xref.line, NULL, event->u.fd, event->u.val,
		 event->arg, event->u.sands.tv_sec);

	pthread_setspecific(thread_current, event);
	(*event->func)(event);
	pthread_setspecific(thread_current, NULL);

	GETRUSAGE(&after);
	event->master->last_getrusage = after;

	unsigned long walltime, cputime;
	unsigned long exp;

	walltime = event_consumed_time(&after, &before, &cputime);

	/* update walltime */
	atomic_fetch_add_explicit(&event->hist->real.total, walltime,
				  memory_order_seq_cst);
	exp = atomic_load_explicit(&event->hist->real.max,
				   memory_order_seq_cst);
	while (exp < walltime
	       && !atomic_compare_exchange_weak_explicit(
		       &event->hist->real.max, &exp, walltime,
		       memory_order_seq_cst, memory_order_seq_cst))
		;

	if (cputime_enabled_here && cputime_enabled) {
		/* update cputime */
		atomic_fetch_add_explicit(&event->hist->cpu.total, cputime,
					  memory_order_seq_cst);
		exp = atomic_load_explicit(&event->hist->cpu.max,
					   memory_order_seq_cst);
		while (exp < cputime
		       && !atomic_compare_exchange_weak_explicit(
			       &event->hist->cpu.max, &exp, cputime,
			       memory_order_seq_cst, memory_order_seq_cst))
			;
	}

	atomic_fetch_add_explicit(&event->hist->total_calls, 1,
				  memory_order_seq_cst);
	atomic_fetch_or_explicit(&event->hist->types, 1 << event->add_type,
				 memory_order_seq_cst);

	if (suppress_warnings)
		return;

	if (cputime_enabled_here && cputime_enabled && cputime_threshold
	    && cputime > cputime_threshold) {
		/*
		 * We have a CPU Hog on our hands.  The time FRR has spent
		 * doing actual work (not sleeping) is greater than 5 seconds.
		 * Whinge about it now, so we're aware this is yet another task
		 * to fix.
		 */
		atomic_fetch_add_explicit(&event->hist->total_cpu_warn,
					  1, memory_order_seq_cst);
		flog_warn(
			EC_LIB_SLOW_THREAD_CPU,
			"CPU HOG: task %s (%lx) ran for %lums (cpu time %lums)",
			event->xref->funcname, (unsigned long)event->func,
			walltime / 1000, cputime / 1000);

	} else if (walltime_threshold && walltime > walltime_threshold) {
		/*
		 * The runtime for a task is greater than 5 seconds, but the
		 * cpu time is under 5 seconds.  Let's whine about this because
		 * this could imply some sort of scheduling issue.
		 */
		atomic_fetch_add_explicit(&event->hist->total_wall_warn,
					  1, memory_order_seq_cst);
		flog_warn(
			EC_LIB_SLOW_THREAD_WALL,
			"STARVATION: task %s (%lx) ran for %lums (cpu time %lums)",
			event->xref->funcname, (unsigned long)event->func,
			walltime / 1000, cputime / 1000);
	}
}

/* Execute thread */
/* 同步立即执行一个事件：若有 eref 先取消旧事件；在锁内取 EVENT_EXECUTE 事件、
 * 填 val 与 ref，随后直接 event_call，最后放回 unuse 缓存。用于不经过主循环
 * 排队的即时回调（如 CLI、初始化路径）。
 */
void _event_execute(const struct xref_eventsched *xref, struct event_loop *m,
		    void (*func)(struct event *), void *arg, int val,
		    struct event **eref)
{
	struct event *event;

	/* Cancel existing scheduled task TODO -- nice to do in 1 lock cycle */
	if (eref)
		event_cancel(eref);

	/* Get or allocate new thread to execute. */
	frr_with_mutex (&m->mtx) {
		event = event_get(m, EVENT_EXECUTE, func, arg, xref);

		/* Set its event value. */
		frr_with_mutex (&event->mtx) {
			event->u.val = val;
			event->ref = &event;
		}
	}

	/* Execute thread doing all accounting. */
	event_call(event);

	/* Give back or free thread. */
	thread_add_unuse(m, event);
}

/* Debug signal mask - if 'sigs' is NULL, use current effective mask. */
/* 打印信号掩码内容（逗号分隔的信号编号）供调试；sigs 为 NULL 时用当前线程的
 * 有效掩码。只扫描到 LAST_SIGNAL（实时信号起始或 32）。输出到 debug 日志。
 */
void debug_signals(const sigset_t *sigs)
{
	int i, found;
	sigset_t tmpsigs;
	char buf[300];

	/*
	 * We're only looking at the non-realtime signals here, so we need
	 * some limit value. Platform differences mean at some point we just
	 * need to pick a reasonable value.
	 */
#if defined SIGRTMIN
#  define LAST_SIGNAL SIGRTMIN
#else
#  define LAST_SIGNAL 32
#endif


	if (sigs == NULL) {
		sigemptyset(&tmpsigs);
		pthread_sigmask(SIG_BLOCK, NULL, &tmpsigs);
		sigs = &tmpsigs;
	}

	found = 0;
	buf[0] = '\0';

	for (i = 0; i < LAST_SIGNAL; i++) {
		char tmp[20];

		if (sigismember(sigs, i) > 0) {
			if (found > 0)
				strlcat(buf, ",", sizeof(buf));
			snprintf(tmp, sizeof(tmp), "%d", i);
			strlcat(buf, tmp, sizeof(buf));
			found++;
		}
	}

	if (found == 0)
		snprintf(buf, sizeof(buf), "<none>");

	zlog_debug("%s: %s", __func__, buf);
}

/* Accessors for event loop pthread */
/* 返回事件循环的归属线程。 */
pthread_t frr_event_loop_get_pthread_owner(struct event_loop *loop)
{
	return loop->owner;
}

/* 设置事件循环的归属线程。 */
void frr_event_loop_set_pthread_owner(struct event_loop *loop, pthread_t pth)
{
	loop->owner = pth;
}

/* 设置该事件循环是否负责处理进程信号。 */
/* Control whether 'loop' is the signal-handler for a process */
void frr_event_loop_set_handle_sigs(struct event_loop *loop, bool handle_p)
{
	loop->handle_signals = handle_p;
}

/* "%pTHD" 的调试格式化：输出事件指针、arg、类型名、fd 或定时器剩余时间，以及
 * 回调名、目的地与来源文件:行号，用于日志 / 调试打印事件。
 */
static ssize_t printfrr_thread_dbg(struct fbuf *buf, struct printfrr_eargs *ea,
				   const struct event *event)
{
	static const char *const types[] = {
		[EVENT_READ] = "read",	  [EVENT_WRITE] = "write",
		[EVENT_TIMER] = "timer",  [EVENT_EVENT] = "event",
		[EVENT_READY] = "ready",  [EVENT_UNUSED] = "unused",
		[EVENT_EXECUTE] = "exec",
	};
	ssize_t rv = 0;
	char info[16] = "";

	if (!event)
		return bputs(buf, "{(event *)NULL}");

	rv += bprintfrr(buf, "{(event *)%p arg=%p", event, event->arg);

	if (event->type < array_size(types) && types[event->type])
		rv += bprintfrr(buf, " %-6s", types[event->type]);
	else
		rv += bprintfrr(buf, " INVALID(%u)", event->type);

	switch (event->type) {
	case EVENT_READ:
	case EVENT_WRITE:
		snprintfrr(info, sizeof(info), "fd=%d", event->u.fd);
		break;

	case EVENT_TIMER:
		snprintfrr(info, sizeof(info), "r=%pTVMud", &event->u.sands);
		break;
	case EVENT_READY:
	case EVENT_EVENT:
	case EVENT_UNUSED:
	case EVENT_EXECUTE:
		break;
	}

	rv += bprintfrr(buf, " %-12s %s() %s from %s:%d}", info,
			event->xref->funcname, event->xref->dest,
			event->xref->xref.file, event->xref->xref.line);
	return rv;
}

/* 注册 "%pTH" 打印扩展：把事件格式化为其定时器剩余 / 到期时间（"%pTHD" 则走
 * 调试详细格式）。event 为 NULL 时跳过时间格式标记并输出 '-'。
 */
printfrr_ext_autoreg_p("TH", printfrr_thread);
static ssize_t printfrr_thread(struct fbuf *buf, struct printfrr_eargs *ea,
			       const void *ptr)
{
	const struct event *event = ptr;
	struct timespec remain = {};

	if (ea->fmt[0] == 'D') {
		ea->fmt++;
		return printfrr_thread_dbg(buf, ea, event);
	}

	if (!event) {
		/* need to jump over time formatting flag characters in the
		 * input format string, i.e. adjust ea->fmt!
		 */
		printfrr_time(buf, ea, &remain,
			      TIMEFMT_TIMER_DEADLINE | TIMEFMT_SKIP);
		return bputch(buf, '-');
	}

	TIMEVAL_TO_TIMESPEC(&event->u.sands, &remain);
	return printfrr_time(buf, ea, &remain, TIMEFMT_TIMER_DEADLINE);
}
