// SPDX-License-Identifier: GPL-2.0-or-later
/* Quagga signal handling functions.
 * Copyright (C) 2004 Paul Jakma,
 */

/*
 * 文件导读：sigevent.c —— 信号处理与事件循环的桥接实现
 *
 * 一、这个文件在 FRR 里干什么
 *   把操作系统信号转成事件循环可处理的回调。信号到达时只置位标记，
 *   真正的处理在事件循环的安全上下文里由 frr_sigevent_process() 执行，
 *   从而规避异步信号安全（AS-safe）限制。
 *
 * 二、核心数据结构
 *   sigmaster：文件级单例 struct frr_sigevent_master_t，聚合所有
 *             frr_signal_t（保存在 signal_init 传入的数组里）。
 *   frr_signal_t：单个信号的编号 + 回调 + caught 标志（见 sigevent.h）。
 *
 * 三、与其他模块的关系
 *   - frrevent.h：提供 event / event_loop，用于可选定时器轮询路径；
 *   - libfrr.c：进程启动时调用 signal_init() 装配信号；
 *   - log.c：崩溃信号走 zlog_signal() / zlog_tls_buffer_flush() 落盘；
 *   - ucontext.h：可选，用 program_counter() 从崩溃上下文取指令指针。
 *
 * 四、阅读顺序
 *   frr_signal_handler（信号真正入口，只置位）
 *   -> frr_sigevent_check / frr_sigevent_process（事件循环侧）
 *   -> trap_default_signals（安装默认 core / exit / ignore 处理）
 *   -> signal_init（对外初始化）。
 *
 * 五、关键“坑”
 *   - 处理完 sigmaster.caught 后不得再读写它，否则与 per-sig 标志竞态；
 *   - core_handler 里只能用 AS-safe 调用，且用 alarm(1) 兜底防挂死；
 *   - SIGEVENT_BLOCK_SIGNALS / SIGEVENT_SCHEDULE_THREAD 是编译期开关，
 *     决定是否额外屏蔽信号 / 使用定时器线程轮询。
 */
#include <zebra.h>

#include <signal.h>
#include <sigevent.h>
#include <log.h>
#include <memory.h>
#include <lib_errors.h>

#ifdef HAVE_UCONTEXT_H
#ifdef GNU_LINUX
/* get REG_EIP from ucontext.h */
#ifndef __USE_GNU
#define __USE_GNU
#endif /* __USE_GNU */
#endif /* GNU_LINUX */
#include <ucontext.h>
#endif /* HAVE_UCONTEXT_H */


/* master signals descriptor struct */
/*
 * 全局信号主控结构（单例 sigmaster）。
 * t       : 可选定时器路径使用的事件句柄。
 * signals : 指向 signal_init 传入的 frr_signal_t 数组（不拥有其内存）。
 * sigc    : 数组元素个数。
 * caught  : 是否有任一被跟踪信号到达的全局标志。
 */
static struct frr_sigevent_master_t {
	struct event *t;

	struct frr_signal_t *signals;
	int sigc;

	volatile sig_atomic_t caught;
} sigmaster;

/* Generic signal handler
 * Schedules signal event thread
 */
/*
 * 通用信号处理函数（真正的异步信号入口，必须 AS-safe）。
 * 只做一件事：遍历信号数组，把匹配 signo 的表项 caught 置 1，
 * 并置全局 caught。不做任何 I/O、内存分配或日志。
 */
static void frr_signal_handler(int signo)
{
	int i;
	struct frr_signal_t *sig;

	for (i = 0; i < sigmaster.sigc; i++) {
		sig = &(sigmaster.signals[i]);

		if (sig->signal == signo)
			sig->caught = 1;
	}

	sigmaster.caught = 1;
}

/*
 * Check whether any signals have been received and are pending. This is done
 * with the application's key signals blocked. The complete set of signals
 * is returned in 'setp', so the caller can restore them when appropriate.
 * If there are pending signals, returns 'true', 'false' otherwise.
 */
/*
 * 判断是否有信号待处理。
 * 做法：先把应用关心的信号全部屏蔽，读取全局 caught 标志，并把被屏蔽的
 * 信号集合回填到 *setp，供调用者事后恢复。
 * 返回 true 表示有信号待处理，false 表示无。
 * 注意：*setp 会先被 sigemptyset 初始化再填充，调用者负责恢复掩码。
 */
bool frr_sigevent_check(sigset_t *setp)
{
	sigset_t blocked;
	int i;
	bool ret;

	sigemptyset(setp);
	sigemptyset(&blocked);

	/* Set up mask of application's signals */
	for (i = 0; i < sigmaster.sigc; i++)
		sigaddset(&blocked, sigmaster.signals[i].signal);

	pthread_sigmask(SIG_BLOCK, &blocked, setp);

	/* Now that the application's signals are blocked, test. */
	ret = (sigmaster.caught != 0);

	return ret;
}

/* check if signals have been caught and run appropriate handlers */
/*
 * 事件循环回调：检查是否有信号被捕获，若有则依次执行其 handler。
 * 若定义了 SIGEVENT_BLOCK_SIGNALS，会先阻塞大部分信号（但不阻塞
 * SIGTRAP / SIGKILL，前者会破坏 gdb，后者本就不允许屏蔽）。
 * 返回 0 成功；信号掩码操作失败返回 -1。
 * 关键：一旦读取并清零 sigmaster.caught 后，就不得再访问它（存在竞态）。
 */
int frr_sigevent_process(void)
{
	struct frr_signal_t *sig;
	int i;
#ifdef SIGEVENT_BLOCK_SIGNALS
	/* shouldn't need to block signals, but potentially may be needed */
	sigset_t newmask, oldmask;

	/*
	 * Block most signals, but be careful not to defer SIGTRAP because
	 * doing so breaks gdb, at least on NetBSD 2.0.  Avoid asking to
	 * block SIGKILL, just because we shouldn't be able to do so.
	 */
	sigfillset(&newmask);
	sigdelset(&newmask, SIGTRAP);
	sigdelset(&newmask, SIGKILL);

	if ((sigprocmask(SIG_BLOCK, &newmask, &oldmask)) < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "frr_signal_timer: couldnt block signals!");
		return -1;
	}
#endif /* SIGEVENT_BLOCK_SIGNALS */

	if (sigmaster.caught > 0) {
		sigmaster.caught = 0;
		/* must not read or set sigmaster.caught after here,
		 * race condition with per-sig caught flags if one does
		 */

		for (i = 0; i < sigmaster.sigc; i++) {
			sig = &(sigmaster.signals[i]);

			if (sig->caught > 0) {
				sig->caught = 0;
				if (sig->handler)
					sig->handler();
			}
		}
	}

#ifdef SIGEVENT_BLOCK_SIGNALS
	if (sigprocmask(SIG_UNBLOCK, &oldmask, NULL) < 0)
		return -1;
#endif /* SIGEVENT_BLOCK_SIGNALS */

	return 0;
}

#ifdef SIGEVENT_SCHEDULE_THREAD
/* timer thread to check signals. shouldn't be needed */
/*
 * 定时器线程回调：周期性重挂自身并调用 frr_sigevent_process()。
 * 仅在 SIGEVENT_SCHEDULE_THREAD 编译开关下存在，属于兼容旧平台的
 * 备用路径，正常不需要。
 */
void frr_signal_timer(struct event *t)
{
	struct frr_sigevent_master_t *sigm;

	sigm = EVENT_ARG(t);
	sigm->t = NULL;
	event_add_timer(sigm->t->master, frr_signal_timer, &sigmaster,
			FRR_SIGNAL_TIMER_INTERVAL, &sigm->t);
	frr_sigevent_process();
}
#endif /* SIGEVENT_SCHEDULE_THREAD */

/* Initialization of signal handles. */
/* Signal wrapper. */
/*
 * 为单个信号安装 frr_signal_handler。
 * 用 sigaction 而非 signal()，掩码取全集；对 SIGALRM 用 SA_INTERRUPT，
 * 其余用 SA_RESTART，避免慢系统调用被意外打断。
 * 返回 0 成功，小于 0 失败（由调用者决定是否致命）。
 */
static int signal_set(int signo)
{
	int ret;
	struct sigaction sig;
	struct sigaction osig;

	sig.sa_handler = &frr_signal_handler;
	sigfillset(&sig.sa_mask);
	sig.sa_flags = 0;
	if (signo == SIGALRM) {
#ifdef SA_INTERRUPT
		sig.sa_flags |= SA_INTERRUPT; /* SunOS */
#endif
	} else {
#ifdef SA_RESTART
		sig.sa_flags |= SA_RESTART;
#endif /* SA_RESTART */
	}

	ret = sigaction(signo, &sig, &osig);
	if (ret < 0)
		return ret;
	else
		return 0;
}

/* XXX This function should be enhanced to support more platforms
       (it currently works only on Linux/x86). */
/*
 * 从崩溃信号上下文（ucontext_t）中取出指令指针，用于日志里定位崩溃点。
 * context：信号处理函数收到的第三个参数，可空。
 * 返回指令指针，或取不到时返回 NULL。
 * 说明：实现依赖平台/体系结构宏（REG_EIP / REG_RIP / gregs 等），
 *       目前基本只对 Linux/x86 有效。
 */
static void *program_counter(void *context)
{
#ifdef HAVE_UCONTEXT_H
#ifdef GNU_LINUX
/* these are from GNU libc, rather than Linux, strictly speaking */
#if defined(REG_EIP)
#  define REG_INDEX REG_EIP
#elif defined(REG_RIP)
#  define REG_INDEX REG_RIP
#elif defined(__powerpc__)
#  define REG_INDEX 32
#endif
#endif		       /* GNU_LINUX */

#ifdef REG_INDEX
#ifdef HAVE_UCONTEXT_T_UC_MCONTEXT_GREGS
#  define REGS gregs[REG_INDEX]
#elif defined(HAVE_UCONTEXT_T_UC_MCONTEXT_UC_REGS)
#  define REGS uc_regs->gregs[REG_INDEX]
#endif /* HAVE_UCONTEXT_T_UC_MCONTEXT_GREGS */
#endif /* REG_INDEX */

#ifdef REGS
	if (context)
		return (void *)(((ucontext_t *)context)->uc_mcontext.REGS);
#elif defined(HAVE_UCONTEXT_T_UC_MCONTEXT_REGS__NIP)
	/* older Linux / struct pt_regs ? */
	if (context)
		return (void *)(((ucontext_t *)context)->uc_mcontext.regs->nip);
#endif /* REGS */

#endif /* HAVE_UCONTEXT_H */
	return NULL;
}

/*
 * 退出类信号（SIGHUP / SIGINT / SIGTERM / SIGUSR1 等）的默认处理。
 * 记录带信号号的日志后调用 _exit(128+signo)，模拟 shell 的退出码约定。
 * FRR_NORETURN 表示该函数不会返回。
 */
static void FRR_NORETURN exit_handler(int signo, siginfo_t *siginfo,
				      void *context)
{
	void *pc = program_counter(context);

	zlog_signal(signo, "exiting...", siginfo, pc);
	_exit(128 + signo);
}

/*
 * 崩溃类信号（SIGSEGV / SIGABRT / SIGBUS 等）的默认处理：尽量留下日志再让
 * 内核生成 coredump。为避免二次崩溃或挂死：先恢复 SIGALRM 与当前信号为默认
 * 动作，解除屏蔽并 alarm(1) 兜底，flush 日志缓冲，最后 raise(signo) 触发核心
 * 转储。FRR_NORETURN：正常路径以 _exit 或 raise 结束，绝不返回。
 * 注意：此处只能用 AS-safe 调用，切勿放会分配内存或加锁的日志函数。
 */
static void FRR_NORETURN core_handler(int signo, siginfo_t *siginfo,
				      void *context)
{
	void *pc = program_counter(context);

	/* make sure we don't hang in here.  default for SIGALRM is terminate.
	 * - if we're in backtrace for more than a second, abort. */
	struct sigaction sa_default = {.sa_handler = SIG_DFL};

	sigaction(SIGALRM, &sa_default, NULL);
	sigaction(signo, &sa_default, NULL);

	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);

	alarm(1);

	zlog_signal(signo, "aborting...", siginfo, pc);

	/* there used to be a log_memstats() call here, to dump MTYPE counters
	 * on a coredump.  This is not possible since log_memstats is not
	 * AS-Safe, as it calls fopen(), fprintf(), and cousins.  This can
	 * lead to a deadlock depending on where we crashed - very much not a
	 * good thing if the process just hangs there after a crash.
	 *
	 * The alarm(1) above tries to alleviate this, but that's really a
	 * last resort recovery.  Stick with AS-safe calls here.
	 *
	 * If the fprintf() calls are removed from log_memstats(), this can be
	 * added back in, since writing to log with zlog_sigsafe() is AS-safe.
	 */

	/*
	 * This is a buffer flush because FRR is going down
	 * hard.  This is especially important if the crash
	 * was caused by a memory operation and if we call
	 * zlog_tls_buffer_fini() then it has memory
	 * operations as well.  This will cause the
	 * core dump to not happen.  BAD MOJO
	 * So this is intentional, let's try to flush
	 * what we can and let the crash happen.
	 */
	zlog_tls_buffer_flush();

	/* give the kernel a chance to generate a coredump */
	sigaddset(&sigset, signo);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);
	raise(signo);

	/* only chance to end up here is if the default action for signo is
	 * something other than kill or coredump the process
	 */
	_exit(128 + signo);
}

/*
 * 安装一组默认信号处理器，作为“未被子系统接管”时的兜底：
 *   - core_signals  : 崩溃类信号 -> core_handler（生成 coredump）；
 *   - exit_signals  : 退出类信号 -> exit_handler（干净退出）；
 *   - ignore_signals: SIGPIPE 等 -> SIG_IGN。
 * 仅当某信号当前仍是系统默认（SIG_DFL）时才安装，不覆盖应用或库已设置的
 * 处理函数。core_handler 额外带 SA_RESETHAND，避免递归打印回溯。
 */
static void trap_default_signals(void)
{
	static const int core_signals[] = {
		SIGQUIT, SIGILL, SIGABRT,
#ifdef SIGEMT
		SIGEMT,
#endif
		SIGFPE,  SIGBUS, SIGSEGV,
#ifdef SIGSYS
		SIGSYS,
#endif
#ifdef SIGXCPU
		SIGXCPU,
#endif
#ifdef SIGXFSZ
		SIGXFSZ,
#endif
	};
	static const int exit_signals[] = {
		SIGHUP,    SIGINT, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2,
#ifdef SIGPOLL
		SIGPOLL,
#endif
#ifdef SIGVTALRM
		SIGVTALRM,
#endif
#ifdef SIGSTKFLT
		SIGSTKFLT,
#endif
	};
	static const int ignore_signals[] = {
		SIGPIPE,
	};
	/*
	 * 处理函数映射表：把信号集合与其 handler 绑定，
	 * 便于下方统一循环安装。handler 为 NULL 表示忽略该组信号。
	 */
	static const struct {
		const int *sigs;
		unsigned int nsigs;
		void (*handler)(int signo, siginfo_t *info, void *context);
	} sigmap[] = {
		{core_signals, array_size(core_signals), core_handler},
		{exit_signals, array_size(exit_signals), exit_handler},
		{ignore_signals, array_size(ignore_signals), NULL},
	};
	unsigned int i;

	for (i = 0; i < array_size(sigmap); i++) {
		unsigned int j;

		for (j = 0; j < sigmap[i].nsigs; j++) {
			struct sigaction oact;
			if ((sigaction(sigmap[i].sigs[j], NULL, &oact) == 0)
			    && (oact.sa_handler == SIG_DFL)) {
				struct sigaction act;
				sigfillset(&act.sa_mask);
				if (sigmap[i].handler == NULL) {
					act.sa_handler = SIG_IGN;
					act.sa_flags = 0;
				} else {
					/* Request extra arguments to signal
					 * handler. */
					act.sa_sigaction = sigmap[i].handler;
					act.sa_flags = SA_SIGINFO;
#ifdef SA_RESETHAND
					/* don't try to print backtraces
					 * recursively */
					if (sigmap[i].handler == core_handler)
						act.sa_flags |= SA_RESETHAND;
#endif
				}
				if (sigaction(sigmap[i].sigs[j], &act, NULL)
				    < 0)
					flog_err(
						EC_LIB_SYSTEM_CALL,
						"Unable to set signal handler for signal %d: %s",
						sigmap[i].sigs[j],
						safe_strerror(errno));
			}
		}
	}
}

/*
 * 初始化信号系统（对外主入口）。
 * m      : 事件循环对象（仅可选定时器路径使用）。
 * sigc   : signals 数组元素个数。
 * signals: 应用要处理的信号描述数组（内存由调用者持有）。
 * 过程：先安装默认陷阱，再为每个信号注册 frr_signal_handler，
 *       最后把数组保存到全局 sigmaster。安装失败直接 exit(-1)。
 */
void signal_init(struct event_loop *m, int sigc, struct frr_signal_t signals[])
{

	int i = 0;
	struct frr_signal_t *sig;

	/* First establish some default handlers that can be overridden by
	   the application. */
	trap_default_signals();

	while (i < sigc) {
		sig = &signals[i];
		if (signal_set(sig->signal) < 0)
			exit(-1);
		i++;
	}

	sigmaster.sigc = sigc;
	sigmaster.signals = signals;

#ifdef SIGEVENT_SCHEDULE_THREAD
	sigmaster.t = NULL;
	event_add_timer(m, frr_signal_timer, &sigmaster,
			FRR_SIGNAL_TIMER_INTERVAL, &sigmaster.t);
#endif /* SIGEVENT_SCHEDULE_THREAD */
}
