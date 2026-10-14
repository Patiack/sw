// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Quagga Signal handling header.
 *
 * Copyright (C) 2004 Paul Jakma.
 */

/*
 * 文件导读：sigevent.h —— 信号处理与事件循环的桥接层（对外接口）
 *
 * 一、这个文件在 FRR 里干什么
 *   FRR 的每个守护进程都运行在事件循环（event_loop，见 frrevent.h）之上。
 *   操作系统信号（SIGINT / SIGHUP / SIGTERM / SIGUSR1 等）不能直接在
 *   信号处理函数里做复杂工作（受异步信号安全限制），因此本模块把“收到信号”
 *   这件事记下来，交给事件循环在安全上下文里统一处理：
 *     - 每个被关心的信号对应一个 struct frr_signal_t，含处理函数指针；
 *     - 信号真正到达时只置位 caught 标记；
 *     - 事件循环随后调用 frr_sigevent_process() 逐个执行对应 handler。
 *
 * 二、核心数据结构
 *   struct frr_signal_t：一个信号的描述——信号编号、回调函数、私有 caught 标志。
 *   sigmaster（定义在 sigevent.c）：全局单例，聚合所有信号描述。
 *
 * 三、与其他模块的关系
 *   - frrevent.h：事件循环，signal_init() 需要其 struct event_loop 指针；
 *   - libfrr.c：守护进程初始化时构造 frr_signal_t 数组并调用 signal_init()；
 *   - 多线程：frr_sigset_add_mainonly() 把“应用级”信号限制在主线程处理。
 *
 * 四、阅读顺序
 *   先读本头文件了解接口，再读 sigevent.c：frr_signal_handler（信号入口）
 *   -> frr_sigevent_check / frr_sigevent_process（事件循环侧处理）
 *   -> signal_init（安装与初始化）。
 *
 * 五、关键“坑”
 *   - caught 必须是 volatile sig_atomic_t，读写才不会被编译器优化掉；
 *   - 处理函数里只能做最小工作，业务逻辑一律延后到事件循环；
 *   - 默认信号陷阱（core / exit / ignore）在 sigevent.c 的
 *     trap_default_signals() 中安装，应用可用 signal_init 覆盖。
 */
#ifndef _FRR_SIGNAL_H
#define _FRR_SIGNAL_H

#include <frrevent.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 仅在启用 SIGEVENT_SCHEDULE_THREAD（兼容旧平台的备用路径）时使用：
 * 定时器线程以该间隔（秒）周期性轮询是否有信号待处理。
 * 现代实现走“事件循环回调”路径，一般不再使用该定时器。
 */
#define FRR_SIGNAL_TIMER_INTERVAL 2L

/*
 * 单个信号的描述结构。
 * signal  : 信号编号（如 SIGINT / SIGHUP）。
 * handler : 事件循环回调里真正执行的函数（可为 NULL，表示只记标记不处理）。
 * caught  : 私有成员，信号到达时由信号处理函数置 1；必须是
 *           volatile sig_atomic_t，才能保证信号上下文与主流程之间的可见性。
 */
struct frr_signal_t {
	int signal;	    /* signal number    */
	void (*handler)(void); /* handler to call  */

	volatile sig_atomic_t caught; /* private member   */
};

/* initialise sigevent system
 * takes:
 * - pointer to valid struct event_loop
 * - number of elements in passed in signals array
 * - array of frr_signal_t's describing signals to handle
 *   and handlers to use for each signal
 */
/*
 * 初始化信号处理系统。
 * m      : 守护进程的事件循环（新实现里仅用于可选定时器路径）。
 * sigc   : 信号数组元素个数。
 * signals: frr_signal_t 数组，描述要处理的每个信号及回调。
 * 副作用：先安装默认信号陷阱，再为每个信号注册 frr_signal_handler，
 *         并把数组保存在全局 sigmaster 中；安装失败会直接退出进程。
 */
extern void signal_init(struct event_loop *m, int sigc,
			struct frr_signal_t *signals);


/*
 * Check whether any signals have been received and are pending. This is done
 * with the application's key signals blocked. The complete set of signals
 * is returned in 'setp', so the caller can restore them when appropriate.
 * If there are pending signals, returns 'true', 'false' otherwise.
 */
/*
 * 判断是否已有信号到达（在屏蔽“应用信号”的前提下检查，避免误报）。
 * setp : 输出参数，返回被阻塞的信号集合，调用者事后可据此恢复原掩码。
 * 返回 : 有信号待处理返回 true，否则 false。
 */
bool frr_sigevent_check(sigset_t *setp);

/* check whether there are signals to handle, process any found */
/*
 * 检查并处理所有已捕获的信号：对每个 caught 的信号执行其 handler。
 * 返回 : 成功 0；若启用 SIGEVENT_BLOCK_SIGNALS 且屏蔽/恢复失败返回 -1。
 * 副作用：会清空各信号的 caught 标志并调用用户回调，故不要在信号
 *         上下文里调用；须由主事件循环安全地调用。
 */
extern int frr_sigevent_process(void);

/* Ensure we don't handle "application-type" signals on a secondary thread by
 * blocking these signals when creating threads
 *
 * NB: SIGSEGV, SIGABRT, etc. must be allowed on all threads or we get no
 * crashlogs.  Since signals vary a little bit between platforms, below is a
 * list of known things to go to the main thread.  Any unknown signals should
 * stay thread-local.
 */
/*
 * 在创建子线程时调用，把“应用级”信号加入待屏蔽集合，
 * 让它们只在主线程被处理；而 SIGSEGV / SIGABRT 等致命信号不在此列，
 * 以便任意线程崩溃时都能生成 core 或 crashlog。
 */
static inline void frr_sigset_add_mainonly(sigset_t *blocksigs)
{
	/* signals we actively handle */
	sigaddset(blocksigs, SIGHUP);
	sigaddset(blocksigs, SIGINT);
	sigaddset(blocksigs, SIGTERM);
	sigaddset(blocksigs, SIGUSR1);

	/* signals we don't actively use but that semantically belong */
	sigaddset(blocksigs, SIGUSR2);
	sigaddset(blocksigs, SIGQUIT);
	sigaddset(blocksigs, SIGCHLD);
	sigaddset(blocksigs, SIGPIPE);
	sigaddset(blocksigs, SIGTSTP);
	sigaddset(blocksigs, SIGTTIN);
	sigaddset(blocksigs, SIGTTOU);
	sigaddset(blocksigs, SIGWINCH);
}

#ifdef __cplusplus
}
#endif

#endif /* _FRR_SIGNAL_H */
