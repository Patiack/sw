// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-19  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：zlog.c —— FRR 新日志系统的核心引擎
 *
 * 本文件是“一次 zlog 调用到最终落盘”的中枢。它定义了消息对象 struct zlog_msg、
 * 目标链表，以及把消息分发给各目标的逻辑。整体链路如下：
 *
 *   vzlogx(prio, fmt, ap)                 <- 各 zlog_* / flog_* 宏最终都汇到这里
 *     |-- 若配置了 LTTng，附加一条 trace
 *     |-- 有 TLS 缓冲：vzlog_tls()，把消息先暂存进本线程的 mmap 缓冲，择机 flush
 *     |-- 无 TLS 缓冲：vzlog_notls()，直接遍历目标、逐个 logfn 输出
 *     |-- 如该消息登记了“打印回溯”标志，再调用 zlog_backtrace_msg()
 *
 *   目标遍历统一走 RCU 读锁加 frr_each(zlog_targets)，因此目标链可被无锁替换。
 *   每个目标用自己的 prio_min 过滤；prio 数值越小越严重，输出条件为 prio <= prio_min。
 *
 * 关键构件：
 *   - struct zlog_msg：延迟格式化的消息对象，正文在 zlog_msg_text() 里才真正渲染，
 *     渲染结果优先放进内联 stackbuf，放不下再另行分配。
 *   - struct zlog_tls：每线程 mmap 日志缓冲，崩溃时内核可把未写出的日志同步回文件。
 *   - zlog_target_clone / replace：以“先加后删”的方式原子替换目标，期间可能短暂重复。
 *   - 时间戳：zlog_msg_ts() 支持 ISO8601 / 传统样式，zlog_msg_ts_3164() 供 syslog 用。
 *   - 前缀开关：zlog_set_prefix_ec / xid 控制是否输出错误码与事务 ID。
 *
 * 阅读建议：从 vzlogx() 入手，顺着 vzlog_notls() / vzlog_tls() 看消息如何被分发；
 * 再到 zlog_msg_text() 看正文如何按需渲染；目标的具体写出在 zlog_targets.c。
 */
#include "zebra.h"
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_GLIBC_BACKTRACE
#include <execinfo.h>
#endif /* HAVE_GLIBC_BACKTRACE */

#include <unistd.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

/* gettid() & co. */
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#ifdef linux
#include <sys/syscall.h>
#endif
#ifdef __FreeBSD__
#include <sys/thr.h>
#endif
#ifdef __NetBSD__
#include <lwp.h>
#endif
#ifdef __DragonFly__
#include <sys/lwp.h>
#endif

#ifdef HAVE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <dlfcn.h>
#endif

#include "memory.h"
#include "atomlist.h"
#include "printfrr.h"
#include "frrcu.h"
#include "zlog.h"
#include "zlog_live.h"
#include "libfrr_trace.h"
#include "frrevent.h"

/*
 * 内存类型定义：LOG_MESSAGE 用于 zlog_msg 正文的动态缓冲，LOG_TLSBUF 用于每线程
 * 日志缓冲结构；都用 DEFINE_MTYPE_STATIC 限制在本文件内。
 */
DEFINE_MTYPE_STATIC(LIB, LOG_MESSAGE,  "log message");
DEFINE_MTYPE_STATIC(LIB, LOG_TLSBUF,   "log thread-local buffer");

/*
 * 钩子定义：zlog_init / zlog_fini / zlog_aux_init。
 * 日志核心在初始化、退出与辅助程序初始化时会调用这些钩子，让内置目标（见
 * zlog_targets.c）有机会把自己挂入 / 摘出目标链。
 */
DEFINE_HOOK(zlog_init, (const char *progname, const char *protoname,
			unsigned short instance, uid_t uid, gid_t gid),
		       (progname, protoname, instance, uid, gid));
DEFINE_KOOH(zlog_fini, (), ());
DEFINE_HOOK(zlog_aux_init, (const char *prefix, int prio_min),
			   (prefix, prio_min));

/*
 * 日志全局上下文：
 *   zlog_prefix / zlog_prefixsz - 每条日志的固定前缀（如 "bgpd[1]: "）及其长度。
 *   zlog_tmpdirfd - 运行期临时目录的 fd，供崩溃转储与 TLS 缓冲文件使用，开局为 -1。
 *   zlog_instance - 本进程实例号（非多实例时为 -1）。
 */
char zlog_prefix[128];
size_t zlog_prefixsz;
int zlog_tmpdirfd = -1;
int zlog_instance = -1;

/*
 * zlog_ec / zlog_xid - 两个全局开关（原子变量）：是否在日志前缀里输出错误码（EC）
 * 与事务 ID（xid）。默认都开启。
 */
static atomic_bool zlog_ec = true, zlog_xid = true;

/* these are kept around because logging is initialized (and directories
 * & files created) before zprivs code switches to the FRR user;  therefore
 * we need to chown() things so we don't get permission errors later when
 * trying to delete things on shutdown
 */
/*
 * zlog_uid / zlog_gid - 运行身份。日志目录与文件在权限切换前创建，故记录 uid / gid，
 * 以便之后 chown，避免退出时以 FRR 用户身份删除文件遇到权限问题。默认 -1。
 */
static uid_t zlog_uid = -1;
static gid_t zlog_gid = -1;

/*
 * zlog_targets - 全局目标链表（原子链表）。所有线程以 RCU 读锁无锁遍历它；
 * 增删只通过 zlog_target_replace()（先加后删）进行，因此遍历期间可能短暂看到新旧并存。
 */
DECLARE_ATOMLIST(zlog_targets, struct zlog_target, head);
static struct zlog_targets_head zlog_targets;

/* Global setting for buffered vs immediate output. The default is
 * per-pthread buffering.
 */
/*
 * zlog_default_immediate - 是否默认“立即输出”。默认按线程缓冲（false）。
 * 置 true 后每条日志即时写出，便于调试。
 */
static bool zlog_default_immediate;

/* cf. zlog.h for additional comments on this struct.
 *
 * Note: you MUST NOT pass the format string + va_list to non-FRR format
 * string functions (e.g. vsyslog, sd_journal_printv, ...) since FRR uses an
 * extended prinf() with additional formats (%pI4 and the like).
 *
 * Also remember to use va_copy() on args.
 */

/*
 * struct zlog_msg - 一条日志消息的完整状态（延迟格式化）。
 * ts 为时间戳；prio 为优先级；fmt / args / xref 保存格式串、参数与元数据；
 * stackbuf / stackbufsz 是内联渲染缓冲，text / textlen 是最终正文（可能指向 stackbuf
 * 或另行动态分配），hdrlen 为 "[xid][EC n] " 前缀长度。
 * pid / tid 仅用于转发来的消息（见 zlog_recirculate_live_msg）。
 * ts_str / ts_dot / ts_zonetail / ts_flags 缓存 ISO8601 时间戳；
 * ts_3164_str / ts_3164_flags 缓存 syslog 用的 RFC3164 时间戳。
 * argpos / n_argpos 记录正文中各参数在输出里的位置，供高亮 / 结构化目标使用。
 * 坑：不要把 fmt 加 va_list 传给非 FRR 的格式化函数，因为 FRR 扩展了格式串（如 %pI4）；
 * 复用参数前要 va_copy。
 */
struct zlog_msg {
	struct timespec ts;
	int prio;

	const char *fmt;
	va_list args;
	const struct xref_logmsg *xref;

	char *stackbuf;
	size_t stackbufsz;
	char *text;
	size_t textlen;
	size_t hdrlen;

	/* for relayed log messages ONLY (cf. zlog_recirculate_live_msg) */
	intmax_t pid, tid;

	/* This is always ISO8601 with sub-second precision 9 here, it's
	 * converted for callers as needed.  ts_dot points to the "."
	 * separating sub-seconds.  ts_zonetail is "Z" or "+00:00" for the
	 * local time offset.
	 *
	 * Valid if ZLOG_TS_ISO8601 is set.
	 * (0 if timestamp has not been formatted yet)
	 */
	char ts_str[32], *ts_dot, ts_zonetail[8];
	uint32_t ts_flags;

	/* "mmm dd hh:mm:ss" for 3164 legacy syslog - too dissimilar from
	 * the above, so just kept separately here.
	 */
	uint32_t ts_3164_flags;
	char ts_3164_str[16];

	/* at the time of writing, 16 args was the actual maximum of arguments
	 * to a single zlog call.  Particularly printing flag bitmasks seems
	 * to drive this.  That said, the overhead of dynamically sizing this
	 * probably outweighs the value.  If anything, a printfrr extension
	 * for printing flag bitmasks might be a good idea.
	 */
	struct fmt_outpos argpos[24];
	size_t n_argpos;
};

/* thread-local log message buffering
 *
 * This is strictly optional and set up by calling zlog_tls_buffer_init()
 * on a particular thread.
 *
 * If in use, this will create a temporary file in /var/tmp which is used as
 * memory-mapped MAP_SHARED log message buffer.  The idea there is that buffer
 * access doesn't require any syscalls, but in case of a crash the kernel
 * knows to sync the memory back to disk.  This way the user can still get the
 * last log messages if there were any left unwritten in the buffer.
 *
 * Sizing this dynamically isn't particularly useful, so here's an 8k buffer
 * with a message limit of 64 messages.  Message metadata (e.g. priority,
 * timestamp) aren't in the mmap region, so they're lost on crash, but we can
 * live with that.
 */

#if defined(HAVE_OPENAT) && defined(HAVE_UNLINKAT)
#define CAN_DO_TLS 1
#endif

/*
 * CAN_DO_TLS - 仅当平台同时具备 openat 与 unlinkat 时才启用 TLS 缓冲实现。
 * TLS_LOG_BUF_SIZE - 单线程缓冲区大小（8KB）。
 * TLS_LOG_MAXMSG - 缓冲内最多暂存的消息条数（64）。
 */
#define TLS_LOG_BUF_SIZE	8192
#define TLS_LOG_MAXMSG		64

/*
 * struct zlog_tls - 每线程日志缓冲。mmbuf 指向一块 mmap 缓冲（优先落盘到 tmpdir 下的
 * 共享文件，失败时退化为匿名映射）；bufpos 为当前写入偏移；do_unlink 表示退出时是否
 * 要删除对应临时文件；nmsgs / msgs / msgp 保存本线程待写出的消息对象。
 * 设计意图：缓冲访问不需系统调用；崩溃时内核会把 MAP_SHARED 内容同步回磁盘文件，
 * 从而尽量保留最后若干条尚未写出的日志（消息元数据不在 mmap 区内，崩溃时会丢失）。
 */
struct zlog_tls {
	char *mmbuf;
	size_t bufpos;
	bool do_unlink;

	size_t nmsgs;
	struct zlog_msg msgs[TLS_LOG_MAXMSG];
	struct zlog_msg *msgp[TLS_LOG_MAXMSG];
};

static inline void zlog_tls_free(void *arg);

/* proper ELF TLS is a bit faster than pthread_[gs]etspecific, so if it's
 * available we'll use it here
 */

#ifdef __OpenBSD__
/*
 * OpenBSD 分支：用 pthread key 实现线程私有存储（该平台没有可用的 ELF TLS）。
 * zlog_tls_key 由构造器 zlog_tls_key_init 创建、析构器 zlog_tls_key_fini 删除。
 */
static pthread_key_t zlog_tls_key;

static void zlog_tls_key_init(void) __attribute__((_CONSTRUCTOR(500)));
static void zlog_tls_key_init(void)
{
	pthread_key_create(&zlog_tls_key, zlog_tls_free);
}

static void zlog_tls_key_fini(void) __attribute__((_DESTRUCTOR(500)));
static void zlog_tls_key_fini(void)
{
	pthread_key_delete(zlog_tls_key);
}

static inline struct zlog_tls *zlog_tls_get(void)
{
	return pthread_getspecific(zlog_tls_key);
}

static inline void zlog_tls_set(struct zlog_tls *val)
{
	pthread_setspecific(zlog_tls_key, val);
}
#else
# ifndef thread_local
#  define thread_local __thread
# endif

/*
 * 通用分支：用 thread_local（ELF TLS）直接保存线程私有指针 zlog_tls_var，
 * 比 pthread_[gs]etspecific 更快；zlog_tls_get / zlog_tls_set 即围绕它读写。
 */
static thread_local struct zlog_tls *zlog_tls_var
	__attribute__((tls_model("initial-exec")));

static inline struct zlog_tls *zlog_tls_get(void)
{
	return zlog_tls_var;
}

static inline void zlog_tls_set(struct zlog_tls *val)
{
	zlog_tls_var = val;
}
#endif

#ifdef CAN_DO_TLS
/*
 * zlog_gettid - 取当前线程的内核线程号（tid）。
 * 在 Linux 上用 syscall(__NR_gettid)，其他平台各有对应接口；结果用 thread_local 缓存，
 * 避免重复系统调用。返回 tid（失败时可能为 -1）。
 */
static intmax_t zlog_gettid(void)
{
#ifndef __OpenBSD__
	/* accessing a TLS variable is much faster than a syscall */
	static thread_local intmax_t cached_tid = -1;
	if (cached_tid != -1)
		return cached_tid;
#endif

	long rv = -1;
#ifdef HAVE_PTHREAD_GETTHREADID_NP
	rv = pthread_getthreadid_np();
#elif defined(linux)
	rv = syscall(__NR_gettid);
#elif defined(__NetBSD__)
	rv = _lwp_self();
#elif defined(__FreeBSD__)
	thr_self(&rv);
#elif defined(__DragonFly__)
	rv = lwp_gettid();
#elif defined(__OpenBSD__)
	rv = getthrid();
#elif defined(__sun)
	rv = pthread_self();
#elif defined(__APPLE__)
	rv = mach_thread_self();
	mach_port_deallocate(mach_task_self(), rv);
#endif

#ifndef __OpenBSD__
	cached_tid = rv;
#endif
	return rv;
}

/*
 * zlog_tls_buffer_init - 为当前线程初始化 mmap 日志缓冲（幂等；已初始化或 tmpdir 不可用则直接返回）。
 * 在 tmpdir 下打开名字形如 "logbuf.<tid>" 的文件，设置大小并 mmap（MAP_SHARED），
 * 成功后记 do_unlink=true；失败则退化为匿名 mmap。完成后把结构挂到 TLS。无返回值。
 * 坑：任何一步失败都会 goto 清理并降级，最终仍会设置 TLS 指针或清空。
 */
void zlog_tls_buffer_init(void)
{
	struct zlog_tls *zlog_tls;
	char mmpath[MAXPATHLEN];
	int mmfd;
	size_t i;

	zlog_tls = zlog_tls_get();

	if (zlog_tls || zlog_tmpdirfd < 0)
		return;

	zlog_tls = XCALLOC(MTYPE_LOG_TLSBUF, sizeof(*zlog_tls));
	for (i = 0; i < array_size(zlog_tls->msgp); i++)
		zlog_tls->msgp[i] = &zlog_tls->msgs[i];

	snprintfrr(mmpath, sizeof(mmpath), "logbuf.%jd", zlog_gettid());

	mmfd = openat(zlog_tmpdirfd, mmpath,
		      O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (mmfd < 0) {
		zlog_err("failed to open thread log buffer \"%s\": %s",
			 mmpath, strerror(errno));
		goto out_anon;
	}
	fchown(mmfd, zlog_uid, zlog_gid);

#ifdef HAVE_POSIX_FALLOCATE
	if (posix_fallocate(mmfd, 0, TLS_LOG_BUF_SIZE) != 0)
	/* note next statement is under above if() */
#endif
	if (ftruncate(mmfd, TLS_LOG_BUF_SIZE) < 0) {
		zlog_err("failed to allocate thread log buffer \"%s\": %s",
			 mmpath, strerror(errno));
		goto out_anon_unlink;
	}

	zlog_tls->mmbuf = mmap(NULL, TLS_LOG_BUF_SIZE, PROT_READ | PROT_WRITE,
			      MAP_SHARED, mmfd, 0);
	if (zlog_tls->mmbuf == MAP_FAILED) {
		zlog_err("failed to mmap thread log buffer \"%s\": %s",
			 mmpath, strerror(errno));
		goto out_anon_unlink;
	}
	zlog_tls->do_unlink = true;

	close(mmfd);
	zlog_tls_set(zlog_tls);
	return;

out_anon_unlink:
	unlinkat(zlog_tmpdirfd, mmpath, 0);
	close(mmfd);
out_anon:

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
	zlog_tls->mmbuf = mmap(NULL, TLS_LOG_BUF_SIZE, PROT_READ | PROT_WRITE,
			      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (!zlog_tls->mmbuf) {
		zlog_err("failed to anonymous-mmap thread log buffer: %s",
			 strerror(errno));
		XFREE(MTYPE_LOG_TLSBUF, zlog_tls);
		zlog_tls_set(NULL);
		return;
	}

	zlog_tls_set(zlog_tls);
}

/*
 * zlog_tls_buffer_fini - 反初始化当前线程的日志缓冲：先 flush 待写消息，再 munmap 并释放结构、
 * 清空 TLS 指针，最后按需 unlink 对应的临时文件。
 */
void zlog_tls_buffer_fini(void)
{
	char mmpath[MAXPATHLEN];
	struct zlog_tls *zlog_tls = zlog_tls_get();
	bool do_unlink = zlog_tls ? zlog_tls->do_unlink : false;

	zlog_tls_buffer_flush();

	zlog_tls_free(zlog_tls);
	zlog_tls_set(NULL);

	snprintfrr(mmpath, sizeof(mmpath), "logbuf.%jd", zlog_gettid());
	if (do_unlink && unlinkat(zlog_tmpdirfd, mmpath, 0))
		zlog_err("unlink logbuf: %s (%d)", strerror(errno), errno);
}

/*
 * 平台不支持 TLS 缓冲（缺 openat / unlinkat）时的空实现占位，用于保持接口一致。
 */
#else /* !CAN_DO_TLS */
void zlog_tls_buffer_init(void)
{
}

void zlog_tls_buffer_fini(void)
{
}
#endif

/*
 * zlog_msg_pid - 取消息对应的 pid / tid。
 * 若消息是转发来的（msg->pid 非 0）则直接用其保存的 pid / tid；否则用当前进程 pid 与
 * 线程 tid，pid 用 thread_local 缓存。
 */
void zlog_msg_pid(struct zlog_msg *msg, intmax_t *pid, intmax_t *tid)
{
#ifndef __OpenBSD__
	static thread_local intmax_t cached_pid = -1;
#endif

	/* recirculated messages */
	if (msg->pid) {
		*pid = msg->pid;
		*tid = msg->tid;
		return;
	}

#ifndef __OpenBSD__
	if (cached_pid != -1)
		*pid = cached_pid;
	else
		cached_pid = *pid = (intmax_t)getpid();
#else
	*pid = (intmax_t)getpid();
#endif
#ifdef CAN_DO_TLS
	*tid = zlog_gettid();
#else
	*tid = *pid;
#endif
}

/*
 * zlog_tls_free - 释放一个 zlog_tls 结构：munmap 其缓冲并释放结构本身。
 * 参数 arg 可为 NULL；作为 pthread key 的析构回调使用。
 */
static inline void zlog_tls_free(void *arg)
{
	struct zlog_tls *zlog_tls = arg;

	if (!zlog_tls)
		return;

	munmap(zlog_tls->mmbuf, TLS_LOG_BUF_SIZE);
	XFREE(MTYPE_LOG_TLSBUF, zlog_tls);
}

/*
 * zlog_tls_buffer_flush - 把当前线程缓冲里的全部消息一次性分发给所有目标。
 * 在 RCU 读锁内遍历目标链，对每个有 logfn 的目标调用它；随后把 bufpos / nmsgs 归零。
 * 无缓冲或无待写消息时直接返回。
 */
void zlog_tls_buffer_flush(void)
{
	struct zlog_target *zt;
	struct zlog_tls *zlog_tls = zlog_tls_get();

	if (!zlog_tls)
		return;
	if (!zlog_tls->nmsgs)
		return;

	rcu_read_lock();
	frr_each_safe (zlog_targets, &zlog_targets, zt) {
		if (!zt->logfn)
			continue;

		zt->logfn(zt, zlog_tls->msgp, zlog_tls->nmsgs);
	}
	rcu_read_unlock();

	zlog_tls->bufpos = 0;
	zlog_tls->nmsgs = 0;
}


/*
 * vzlog_notls - 无 TLS 缓冲时的日志路径：就地构造一个栈上的 zlog_msg（内联 512 字节缓冲），
 * 在 RCU 读锁内遍历目标链，对 prio <= zt->prio_min 且有 logfn 的目标调用其 logfn。
 * 结束后释放可能动态分配的正文内存。参数同 vzlogx。
 */
static void vzlog_notls(const struct xref_logmsg *xref, int prio,
			const char *fmt, va_list ap)
{
	struct zlog_target *zt;
	struct zlog_msg stackmsg = {
		.prio = prio & LOG_PRIMASK,
		.fmt = fmt,
		.xref = xref,
	}, *msg = &stackmsg;
	char stackbuf[512];

	clock_gettime(CLOCK_REALTIME, &msg->ts);
	va_copy(msg->args, ap);
	msg->stackbuf = stackbuf;
	msg->stackbufsz = sizeof(stackbuf);

	rcu_read_lock();
	frr_each_safe (zlog_targets, &zlog_targets, zt) {
		if (prio > zt->prio_min)
			continue;
		if (!zt->logfn)
			continue;

		zt->logfn(zt, &msg, 1);
	}
	rcu_read_unlock();

	va_end(msg->args);
	if (msg->text && msg->text != stackbuf)
		XFREE(MTYPE_LOG_MESSAGE, msg->text);
}

/*
 * vzlog_tls - 有 TLS 缓冲时的日志路径：把消息暂存进本线程缓冲，择机 flush。
 * 步骤：先快速探测“是否有任何目标想要这条消息”，没有则直接返回；否则取出一个 msg 槽，
 * 把正文立即渲染进 mmap 缓冲（因为无法长期持有可变参数引用）。
 * 以下情况会强制立即 flush：缓冲消息数达上限、格式化结果用到了动态分配、缓冲空间将尽、
 * 或优先级高于 LOG_INFO（数值小于 LOG_INFO，即较严重的消息）。
 * 参数：zlog_tls 为当前线程缓冲，其余同 vzlogx。
 */
static void vzlog_tls(struct zlog_tls *zlog_tls, const struct xref_logmsg *xref,
		      int prio, const char *fmt, va_list ap)
{
	struct zlog_target *zt;
	struct zlog_msg *msg;
	char *buf;
	bool ignoremsg = true;
	bool immediate = zlog_default_immediate;

	/* avoid further processing cost if no target wants this message */
	rcu_read_lock();
	frr_each (zlog_targets, &zlog_targets, zt) {
		if (prio > zt->prio_min)
			continue;
		ignoremsg = false;
		break;
	}
	rcu_read_unlock();

	if (ignoremsg)
		return;

	msg = &zlog_tls->msgs[zlog_tls->nmsgs];
	zlog_tls->nmsgs++;
	if (zlog_tls->nmsgs == array_size(zlog_tls->msgs))
		immediate = true;

	memset(msg, 0, sizeof(*msg));
	clock_gettime(CLOCK_REALTIME, &msg->ts);
	va_copy(msg->args, ap);
	msg->stackbuf = buf = zlog_tls->mmbuf + zlog_tls->bufpos;
	msg->stackbufsz = TLS_LOG_BUF_SIZE - zlog_tls->bufpos - 1;
	msg->fmt = fmt;
	msg->prio = prio & LOG_PRIMASK;
	msg->xref = xref;
	if (msg->prio < LOG_INFO)
		immediate = true;

	if (!immediate) {
		/* messages written later need to take the formatting cost
		 * immediately since we can't hold a reference on varargs
		 */
		zlog_msg_text(msg, NULL);

		if (msg->text != buf)
			/* zlog_msg_text called malloc() on us :( */
			immediate = true;
		else {
			zlog_tls->bufpos += msg->textlen + 1;
			/* write a second \0 to mark current end position
			 * (in case of crash this signals end of unwritten log
			 * messages in mmap'd logbuf file)
			 */
			zlog_tls->mmbuf[zlog_tls->bufpos] = '\0';

			/* avoid malloc() for next message */
			if (TLS_LOG_BUF_SIZE - zlog_tls->bufpos < 256)
				immediate = true;
		}
	}

	if (immediate)
		zlog_tls_buffer_flush();

	va_end(msg->args);
	if (msg->text && msg->text != buf)
		XFREE(MTYPE_LOG_MESSAGE, msg->text);
}

/* reinject log message received by zlog_recirculate_recv().  As of writing,
 * only used in the ldpd parent process to proxy messages from lde/ldpe
 * subprocesses.
 */
/*
 * zlog_recirculate_live_msg - 把 zlog_live 收到的一条“线格式”日志重新注入本进程日志系统。
 * data / len 为原始缓冲；先校验头部长度与正文长度，再从头部填充 ts / pid / tid / prio /
 * 正文 / 参数位置等字段；随后尝试用 uid 在本进程 xref 表中找回对应的 xref_logmsg，
 * 找到就用其格式串，找不到则用正文里的格式串兜底（假造 fmt）。
 * 最后在 RCU 读锁内分发给各目标。坑：正文必须以换行加空字符结尾；仅当收发双方是同一个
 * daemon（如 ldpd 父进程代理 lde / ldpe 子进程）时 uid 才能命中。
 */
void zlog_recirculate_live_msg(uint8_t *data, size_t len)
{
	struct zlog_target *zt;
	struct zlog_msg stackmsg = {}, *msg = &stackmsg;
	struct zlog_live_hdr *hdr;
	struct xrefdata *xrefdata, ref = {};

	if (len < sizeof(*hdr))
		return;

	hdr = (struct zlog_live_hdr *)data;
	if (hdr->hdrlen < sizeof(*hdr))
		return;
	data += hdr->hdrlen;
	len -= sizeof(*hdr);

	msg->ts.tv_sec = hdr->ts_sec;
	msg->ts.tv_nsec = hdr->ts_nsec;
	msg->pid = hdr->pid;
	msg->tid = hdr->tid;
	msg->prio = hdr->prio;

	if (hdr->textlen > len)
		return;
	msg->textlen = hdr->textlen;
	msg->hdrlen = hdr->texthdrlen;
	msg->text = (char *)data;

	/* caller needs to make sure we have a trailing \n\0, it's not
	 * transmitted on zlog_live
	 */
	if (msg->text[msg->textlen] != '\n' ||
	    msg->text[msg->textlen + 1] != '\0')
		return;

	static_assert(sizeof(msg->argpos[0]) == sizeof(hdr->argpos[0]),
		      "in-memory struct doesn't match on-wire variant");
	msg->n_argpos = MIN(hdr->n_argpos, array_size(msg->argpos));
	memcpy(msg->argpos, hdr->argpos, msg->n_argpos * sizeof(msg->argpos[0]));

	/* This will only work if we're in the same daemon: we received a log
	 * message uid and are now doing a lookup in *our* known uids to find
	 * it.  This works for ldpd because it's the same binary containing the
	 * same log messages, and ldpd is the only use case right now.
	 *
	 * When the uid is not found, the log message uid is lost but the
	 * message itself is still processed correctly.  If this is needed,
	 * this can be made to work in two ways:
	 * (a) synthesize a temporary xref_logmsg from the received data.
	 *     This is a bit annoying due to lifetimes with per-thread buffers.
	 * (b) extract and aggregate all log messages.  This already happens
	 *     with frr.xref but that would need to be fed back in.
	 */
	strlcpy(ref.uid, hdr->uid, sizeof(ref.uid));
	xrefdata = xrefdata_uid_find(&xrefdata_uid, &ref);

	if (xrefdata && xrefdata->xref->type == XREFT_LOGMSG) {
		struct xref_logmsg *xref_logmsg;

		xref_logmsg = (struct xref_logmsg *)xrefdata->xref;
		msg->xref = xref_logmsg;
		msg->fmt = xref_logmsg->fmtstring;
	} else {
		/* fake out format string... */
		msg->fmt = msg->text + hdr->texthdrlen;
	}

	rcu_read_lock();
	frr_each_safe (zlog_targets, &zlog_targets, zt) {
		if (msg->prio > zt->prio_min)
			continue;
		if (!zt->logfn)
			continue;

		zt->logfn(zt, &msg, 1);
	}
	rcu_read_unlock();
}

/*
 * zlog_backtrace_msg - 针对“被标记为打印回溯”的某条消息，额外打印其调用点与回溯。
 * 先打印消息所在线程与源位置；随后用 libunwind（或 glibc backtrace）逐帧打印，
 * 从命中源函数的那一帧开始输出，并在线程入口帧处标注“scheduled from”。每行前缀带 uid。
 * 参数 xref 为该消息元数据，prio 为优先级。
 */
static void zlog_backtrace_msg(const struct xref_logmsg *xref, int prio)
{
	struct event *tc = pthread_getspecific(thread_current);
	const char *uid = xref->xref.xrefdata->uid;
	bool found_thread = false;

	zlog(prio, "| (%s) message in thread %jd, at %s(), %s:%d", uid,
	     zlog_gettid(), xref->xref.func, xref->xref.file, xref->xref.line);

#ifdef HAVE_LIBUNWIND
	const char *threadfunc = tc ? tc->xref->funcname : NULL;
	bool found_caller = false;
	unw_cursor_t cursor;
	unw_context_t uc;
	unw_word_t ip, off, sp;
	Dl_info dlinfo;

	unw_getcontext(&uc);

	unw_init_local(&cursor, &uc);
	while (unw_step(&cursor) > 0) {
		char buf[96], name[128] = "?";
		bool is_thread = false;

		unw_get_reg(&cursor, UNW_REG_IP, &ip);
		unw_get_reg(&cursor, UNW_REG_SP, &sp);

		if (unw_is_signal_frame(&cursor))
			zlog(prio, "| (%s)    ---- signal ----", uid);

		if (!unw_get_proc_name(&cursor, buf, sizeof(buf), &off)) {
			if (!strcmp(buf, xref->xref.func))
				found_caller = true;
			if (threadfunc && !strcmp(buf, threadfunc))
				found_thread = is_thread = true;

			snprintf(name, sizeof(name), "%s+%#lx", buf, (long)off);
		}

		if (!found_caller)
			continue;

		if (dladdr((void *)ip, &dlinfo))
			zlog(prio, "| (%s) %-36s %16lx+%08lx %16lx %s", uid,
			     name, (long)dlinfo.dli_fbase,
			     (long)ip - (long)dlinfo.dli_fbase, (long)sp,
			     dlinfo.dli_fname);
		else
			zlog(prio, "| (%s) %-36s %16lx %16lx", uid, name,
			     (long)ip, (long)sp);

		if (is_thread)
			zlog(prio, "| (%s) ^- scheduled from %s(), %s:%u", uid,
			     tc->xref->xref.func, tc->xref->xref.file,
			     tc->xref->xref.line);
	}
#elif defined(HAVE_GLIBC_BACKTRACE)
	void *frames[64];
	char **names = NULL;
	int n_frames, i;

	n_frames = backtrace(frames, array_size(frames));
	if (n_frames < 0)
		n_frames = 0;
	if (n_frames)
		names = backtrace_symbols(frames, n_frames);

	for (i = 0; i < n_frames; i++) {
		void *retaddr = frames[i];
		char *loc = names[i];

		zlog(prio, "| (%s) %16lx %-36s", uid, (long)retaddr, loc);
	}
	free(names);
#endif
	if (!found_thread && tc)
		zlog(prio, "| (%s) scheduled from %s(), %s:%u", uid,
		     tc->xref->xref.func, tc->xref->xref.file,
		     tc->xref->xref.line);
}

PRINTFRR(2, 3)
/*
 * ezlog - 无 xref 的便捷变参日志：把 va_list 打包后交给 vzlog（xref 为 NULL）。
 */
void ezlog(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vzlog(prio, fmt, ap);
	va_end(ap);
}

/*
 * vzlogx - 日志总入口（所有 zlog_* / flog_* 宏的最终汇聚点）。
 * 步骤：若启用 LTTng，按优先级映射出一条 trace；然后依据是否存在 TLS 缓冲，分别走
 * vzlog_tls 或 vzlog_notls；最后若该消息的 xref 标记了打印回溯，调用 zlog_backtrace_msg。
 * 参数 xref 可为 NULL，prio 为优先级，fmt 加 ap 为格式串与参数。
 */
void vzlogx(const struct xref_logmsg *xref, int prio, const char *fmt, va_list ap)
{
	struct zlog_tls *zlog_tls = zlog_tls_get();

#ifdef HAVE_LTTNG
	va_list copy;
	va_copy(copy, ap);
	char *msg = vasprintfrr(MTYPE_LOG_MESSAGE, fmt, copy);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
	switch (prio) {
	case LOG_ERR:
		frrtracelog(TRACE_ERR, msg);
		break;
	case LOG_WARNING:
		frrtracelog(TRACE_WARNING, msg);
		break;
	case LOG_DEBUG:
		frrtracelog(TRACE_DEBUG, msg);
		break;
	case LOG_NOTICE:
		frrtracelog(TRACE_DEBUG, msg);
		break;
	case LOG_INFO:
	default:
		frrtracelog(TRACE_INFO, msg);
		break;
	}

	va_end(copy);
	XFREE(MTYPE_LOG_MESSAGE, msg);
#pragma GCC diagnostic pop
#endif

	if (zlog_tls)
		vzlog_tls(zlog_tls, xref, prio, fmt, ap);
	else
		vzlog_notls(xref, prio, fmt, ap);

	if (xref) {
		struct xrefdata_logmsg *xrdl;

		xrdl = container_of(xref->xref.xrefdata, struct xrefdata_logmsg,
				    xrefdata);
		if (xrdl->fl_print_bt)
			zlog_backtrace_msg(xref, prio);
	}
}

/*
 * zlog_sigsafe - 崩溃路径的日志输出：把文本 text（长 len）按行切分，逐行交给各目标的
 * logfn_sigsafe（仅当 LOG_CRIT 满足该目标的 prio_min，且目标提供了信号安全写函数）。
 * 只使用异步信号安全调用。无返回值。
 */
void zlog_sigsafe(const char *text, size_t len)
{
	struct zlog_target *zt;
	const char *end = text + len, *nlpos;

	while (text < end) {
		nlpos = memchr(text, '\n', end - text);
		if (!nlpos)
			nlpos = end;

		frr_each (zlog_targets, &zlog_targets, zt) {
			if (LOG_CRIT > zt->prio_min)
				continue;
			if (!zt->logfn_sigsafe)
				continue;

			zt->logfn_sigsafe(zt, text, nlpos - text);
		}

		if (nlpos == end)
			break;
		text = nlpos + 1;
	}
}

/*
 * _zlog_assert_failed - assert 失败时的处理。
 * 用 LOG_CRIT 打印断言所在的文件 / 行 / 函数 / 表达式，extra 非空时附带额外信息（%pVA）。
 * 用静态标志防止断言处理过程再次断言（二次进入直接 abort）。
 * 坑：本函数最后调用 abort()，不返回；回溯与内存统计由 SIGABRT 处理器输出。
 */
void _zlog_assert_failed(const struct xref_assert *xref, const char *extra, ...)
{
	va_list ap;
	static bool assert_in_assert; /* "global-ish" variable, init to 0 */

	if (assert_in_assert)
		abort();
	assert_in_assert = true;

	if (extra) {
		struct va_format vaf;

		va_start(ap, extra);
		vaf.fmt = extra;
		vaf.va = &ap;

		zlog(LOG_CRIT,
		     "%s:%d: %s(): assertion (%s) failed, extra info: %pVA",
		     xref->xref.file, xref->xref.line, xref->xref.func,
		     xref->expr, &vaf);

		va_end(ap);
	} else
		zlog(LOG_CRIT, "%s:%d: %s(): assertion (%s) failed",
		     xref->xref.file, xref->xref.line, xref->xref.func,
		     xref->expr);

	/* abort() prints backtrace & memstats in SIGABRT handler */
	abort();
}

/*
 * zlog_msg_prio - 返回消息优先级。
 */
int zlog_msg_prio(struct zlog_msg *msg)
{
	return msg->prio;
}

/*
 * zlog_msg_xref - 返回消息的 xref 元数据（可能为 NULL）。
 */
const struct xref_logmsg *zlog_msg_xref(struct zlog_msg *msg)
{
	return msg->xref;
}

/*
 * zlog_msg_text - 返回消息正文（按需渲染，结果缓存）。
 * 首次调用时：先在正文前拼出前缀 "[xid][EC n] "（前缀长度记入 hdrlen），再用
 * vbprintfrr 渲染格式串；结果能塞进内联 stackbuf 就用它，否则另分配一块更大缓冲重新渲染。
 * 正文以换行结尾但不额外补空字符（换行不算进 textlen）。
 * 参数 textlen 可为 NULL。返回正文指针。坑：不要跨线程共享返回的指针。
 */
const char *zlog_msg_text(struct zlog_msg *msg, size_t *textlen)
{
	if (!msg->text) {
		va_list args;
		bool do_xid, do_ec;
		size_t need = 0, hdrlen;
		struct fbuf fb = {
			.buf = msg->stackbuf,
			.pos = msg->stackbuf,
			.len = msg->stackbufsz,
		};

		do_ec = atomic_load_explicit(&zlog_ec, memory_order_relaxed);
		do_xid = atomic_load_explicit(&zlog_xid, memory_order_relaxed);

		if (msg->xref && do_xid && msg->xref->xref.xrefdata->uid[0]) {
			need += bputch(&fb, '[');
			need += bputs(&fb, msg->xref->xref.xrefdata->uid);
			need += bputch(&fb, ']');
		}
		if (msg->xref && do_ec && msg->xref->ec)
			need += bprintfrr(&fb, "[EC %u]", msg->xref->ec);
		if (need)
			need += bputch(&fb, ' ');

		msg->hdrlen = hdrlen = need;
		assert(hdrlen < msg->stackbufsz);

		fb.outpos = msg->argpos;
		fb.outpos_n = array_size(msg->argpos);
		fb.outpos_i = 0;

		va_copy(args, msg->args);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
		/* format-string checking is done further up the chain */
		need += vbprintfrr(&fb, msg->fmt, args);
#pragma GCC diagnostic pop
		va_end(args);

		msg->textlen = need;
		need += bputch(&fb, '\n');

		if (need <= msg->stackbufsz)
			msg->text = msg->stackbuf;
		else {
			msg->text = XMALLOC(MTYPE_LOG_MESSAGE, need);

			memcpy(msg->text, msg->stackbuf, hdrlen);

			fb.buf = msg->text;
			fb.len = need;
			fb.pos = msg->text + hdrlen;
			fb.outpos_i = 0;

			va_copy(args, msg->args);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
			/* same as above */
			vbprintfrr(&fb, msg->fmt, args);
#pragma GCC diagnostic pop
			va_end(args);

			bputch(&fb, '\n');
		}

		msg->n_argpos = fb.outpos_i;
	}
	if (textlen)
		*textlen = msg->textlen;
	return msg->text;
}

/*
 * zlog_msg_args - 返回正文头部长度、参数位置个数与位置数组（对应指针非 NULL 时填充）。
 * 若正文尚未渲染则先触发一次 zlog_msg_text。
 */
void zlog_msg_args(struct zlog_msg *msg, size_t *hdrlen, size_t *n_argpos,
		   const struct fmt_outpos **argpos)
{
	if (!msg->text)
		zlog_msg_text(msg, NULL);

	if (hdrlen)
		*hdrlen = msg->hdrlen;
	if (n_argpos)
		*n_argpos = msg->n_argpos;
	if (argpos)
		*argpos = msg->argpos;
}

/*
 * ZLOG_TS_FORMAT - 两种时间戳样式的掩码（ISO8601 与 LEGACY）。
 * ZLOG_TS_FLAGS - 除精度位以外的有效标志掩码（按位取反 ZLOG_TS_PREC）。
 */
#define ZLOG_TS_FORMAT		(ZLOG_TS_ISO8601 | ZLOG_TS_LEGACY)
#define ZLOG_TS_FLAGS		~ZLOG_TS_PREC

/*
 * zlog_msg_ts - 按 flags 把消息时间戳渲染进 out，返回写入长度。
 * 若缓存的时间戳样式不符或 UTC 设置变化，则重新用 strftime 生成 ISO8601 基础串
 * （"%Y-%m-%dT%H:%M:%S"），并据 flags 追加时区尾部（UTC 为 "Z"，否则为 +/-hh:mm），
 * 再把亚秒精度（ZLOG_TS_PREC 指定的位数）拼上。
 * ZLOG_TS_LEGACY 时把已缓存串里的连字符换成斜杠、字母 T 换成空格，得到传统样式。
 * out 为 NULL 时只返回所需长度而不写出。坑：out 空间不足时会把剩余空间清零并返回所需长度。
 */
size_t zlog_msg_ts(struct zlog_msg *msg, struct fbuf *out, uint32_t flags)
{
	size_t outsz = out ? (out->buf + out->len - out->pos) : 0;
	size_t len1;

	if (!(flags & ZLOG_TS_FORMAT))
		return 0;

	if (!(msg->ts_flags & ZLOG_TS_FORMAT) ||
	    ((msg->ts_flags ^ flags) & ZLOG_TS_UTC)) {
		struct tm tm;

		if (flags & ZLOG_TS_UTC)
			gmtime_r(&msg->ts.tv_sec, &tm);
		else
			localtime_r(&msg->ts.tv_sec, &tm);

		strftime(msg->ts_str, sizeof(msg->ts_str),
			 "%Y-%m-%dT%H:%M:%S", &tm);

		if (flags & ZLOG_TS_UTC) {
			msg->ts_zonetail[0] = 'Z';
			msg->ts_zonetail[1] = '\0';
		} else
			snprintfrr(msg->ts_zonetail, sizeof(msg->ts_zonetail),
				   "%+03d:%02d",
				   (int)(tm.tm_gmtoff / 3600),
				   (int)(labs(tm.tm_gmtoff) / 60) % 60);

		msg->ts_dot = msg->ts_str + strlen(msg->ts_str);
		snprintfrr(msg->ts_dot,
			   msg->ts_str + sizeof(msg->ts_str) - msg->ts_dot,
			   ".%09lu", (unsigned long)msg->ts.tv_nsec);

		msg->ts_flags = ZLOG_TS_ISO8601 | (flags & ZLOG_TS_UTC);
	}

	len1 = flags & ZLOG_TS_PREC;
	len1 = (msg->ts_dot - msg->ts_str) + (len1 ? len1 + 1 : 0);

	if (len1 > strlen(msg->ts_str))
		len1 = strlen(msg->ts_str);

	if (flags & ZLOG_TS_LEGACY) {
		if (!out)
			return len1;

		if (len1 > outsz) {
			memset(out->pos, 0, outsz);
			out->pos += outsz;
			return len1;
		}

		/* just swap out the formatting, faster than redoing it */
		for (char *p = msg->ts_str; p < msg->ts_str + len1; p++) {
			switch (*p) {
			case '-':
				*out->pos++ = '/';
				break;
			case 'T':
				*out->pos++ = ' ';
				break;
			default:
				*out->pos++ = *p;
			}
		}
		return len1;
	} else {
		size_t len2 = strlen(msg->ts_zonetail);

		if (!out)
			return len1 + len2;

		if (len1 + len2 > outsz) {
			memset(out->pos, 0, outsz);
			out->pos += outsz;
			return len1 + len2;
		}

		memcpy(out->pos, msg->ts_str, len1);
		out->pos += len1;
		memcpy(out->pos, msg->ts_zonetail, len2);
		out->pos += len2;
		return len1 + len2;
	}
}

/*
 * zlog_msg_ts_3164 - 生成 RFC3164 syslog 用的 "mmm dd hh:mm:ss" 时间戳并写入 out。
 * 只接受 ZLOG_TS_UTC 标志；结果缓存于 msg->ts_3164_str，标志变化时重新生成。
 * 月份名硬编码为英文缩写。返回 bputs 的写入长度。
 */
size_t zlog_msg_ts_3164(struct zlog_msg *msg, struct fbuf *out, uint32_t flags)
{
	flags &= ZLOG_TS_UTC;

	if (!msg->ts_3164_str[0] || flags != msg->ts_3164_flags) {
		/* these are "hardcoded" in RFC3164, so they're here too... */
		static const char *const months[12] = {
			"Jan", "Feb", "Mar", "Apr", "May", "Jun",
			"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
		};
		struct tm tm;

		/* RFC3164 explicitly asks for local time, but common usage
		 * also includes UTC.
		 */
		if (flags & ZLOG_TS_UTC)
			gmtime_r(&msg->ts.tv_sec, &tm);
		else
			localtime_r(&msg->ts.tv_sec, &tm);

		snprintfrr(msg->ts_3164_str, sizeof(msg->ts_3164_str),
			   "%3s %2d %02d:%02d:%02d", months[tm.tm_mon],
			   tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

		msg->ts_3164_flags = flags;
	}
	return bputs(out, msg->ts_3164_str);
}

/*
 * zlog_msg_tsraw - 把消息的原始 struct timespec 拷进 ts，不做渲染。
 */
void zlog_msg_tsraw(struct zlog_msg *msg, struct timespec *ts)
{
	memcpy(ts, &msg->ts, sizeof(*ts));
}

/*
 * zlog_set_prefix_ec / zlog_get_prefix_ec - 设置 / 读取“前缀是否含错误码”开关（原子操作）。
 */
void zlog_set_prefix_ec(bool enable)
{
	atomic_store_explicit(&zlog_ec, enable, memory_order_relaxed);
}

bool zlog_get_prefix_ec(void)
{
	return atomic_load_explicit(&zlog_ec, memory_order_relaxed);
}

/*
 * zlog_set_prefix_xid / zlog_get_prefix_xid - 设置 / 读取“前缀是否含事务 ID”开关（原子操作）。
 */
void zlog_set_prefix_xid(bool enable)
{
	atomic_store_explicit(&zlog_xid, enable, memory_order_relaxed);
}

bool zlog_get_prefix_xid(void)
{
	return atomic_load_explicit(&zlog_xid, memory_order_relaxed);
}

/* setup functions */

/*
 * zlog_target_clone - 分配并克隆一个 zlog_target（size 字节，内存类型 mt）。
 * oldzt 非空时复制其 prio_min / logfn / logfn_sigsafe；oldzt 为 NULL 则清零新建。
 * 返回值：新目标指针。坑：只克隆基类字段，内嵌目标的私有字段需调用方自行补填。
 */
struct zlog_target *zlog_target_clone(struct memtype *mt,
				      struct zlog_target *oldzt, size_t size)
{
	struct zlog_target *newzt;

	newzt = XCALLOC(mt, size);
	if (oldzt) {
		newzt->prio_min = oldzt->prio_min;
		newzt->logfn = oldzt->logfn;
		newzt->logfn_sigsafe = oldzt->logfn_sigsafe;
	}

	return newzt;
}

/*
 * zlog_target_replace - 原子更新目标链：newzt 非空先加入链尾，oldzt 非空再从链上摘除。
 * 返回 oldzt，便于调用方随后 zlog_target_free()。两参数均可为 NULL。
 * 坑：期间新旧目标可能短暂并存，旧目标必须保持可用直到当前 RCU 周期结束。
 */
struct zlog_target *zlog_target_replace(struct zlog_target *oldzt,
					struct zlog_target *newzt)
{
	if (newzt)
		zlog_targets_add_tail(&zlog_targets, newzt);
	if (oldzt)
		zlog_targets_del(&zlog_targets, oldzt);
	return oldzt;
}

/*
 * Enable or disable 'immediate' output - default is to buffer
 * each pthread's messages.
 */
/*
 * zlog_set_immediate / zlog_get_immediate_mode - 设置 / 读取默认“立即输出”模式。
 */
void zlog_set_immediate(bool set_p)
{
	zlog_default_immediate = set_p;
}

bool zlog_get_immediate_mode(void)
{
	return zlog_default_immediate;
}

/* common init */

/*
 * TMPBASEDIR - 运行期临时目录根（"/var/tmp/frr"）。
 * zlog_tmpdir - 本进程专属的临时子目录路径（含 pid / 实例号），供 TLS 缓冲文件与崩溃日志使用。
 */
#define TMPBASEDIR "/var/tmp/frr"

static char zlog_tmpdir[MAXPATHLEN];

/*
 * zlog_aux_init - 辅助程序（工具 / 测试）的日志初始化：设置前缀后触发 zlog_aux_init 钩子，
 * 由目标侧（zlog_targets.c）完成实际的输出目标挂载。
 */
void zlog_aux_init(const char *prefix, int prio_min)
{
	if (prefix)
		strlcpy(zlog_prefix, prefix, sizeof(zlog_prefix));

	hook_call(zlog_aux_init, prefix, prio_min);
}

/*
 * zlog_init - daemon 的日志核心初始化。
 * 记录 uid / gid / 实例号；按是否多实例拼出前缀（形如 "name[inst]: " 或 "name: "）与
 * 临时目录路径；创建 TMPBASEDIR 与进程临时目录并 chown 为运行身份；打开临时目录的 fd，
 * 最后触发 zlog_init 钩子挂载目标。
 * 坑：任一步失败会跳到 out_warn，提示“崩溃日志与线程日志缓冲不可用”，但仍继续触发钩子。
 */
void zlog_init(const char *progname, const char *protoname,
	       unsigned short instance, uid_t uid, gid_t gid)
{
	zlog_uid = uid;
	zlog_gid = gid;
	zlog_instance = instance;

	if (instance) {
		snprintfrr(zlog_tmpdir, sizeof(zlog_tmpdir), "%s/%s-%d.%ld",
			   TMPBASEDIR, progname, instance, (long)getpid());

		zlog_prefixsz = snprintfrr(zlog_prefix, sizeof(zlog_prefix),
					   "%s[%d]: ", protoname, instance);
	} else {
		snprintfrr(zlog_tmpdir, sizeof(zlog_tmpdir), "%s/%s.%ld",
			   TMPBASEDIR, progname, (long)getpid());

		zlog_prefixsz = snprintfrr(zlog_prefix, sizeof(zlog_prefix),
					   "%s: ", protoname);
	}

	if (mkdir(TMPBASEDIR, 0700) != 0) {
		if (errno != EEXIST) {
			zlog_err("failed to mkdir \"%s\": %s",
				 TMPBASEDIR, strerror(errno));
			goto out_warn;
		}
	}
	chown(TMPBASEDIR, zlog_uid, zlog_gid);

	if (mkdir(zlog_tmpdir, 0700) != 0) {
		zlog_err("failed to mkdir \"%s\": %s",
			 zlog_tmpdir, strerror(errno));
		goto out_warn;
	}

#ifdef O_PATH
	zlog_tmpdirfd = open(zlog_tmpdir,
			     O_PATH | O_RDONLY | O_CLOEXEC);
#else
	zlog_tmpdirfd = open(zlog_tmpdir,
			     O_DIRECTORY | O_RDONLY | O_CLOEXEC);
#endif
	if (zlog_tmpdirfd < 0) {
		zlog_err("failed to open \"%s\": %s",
			 zlog_tmpdir, strerror(errno));
		goto out_warn;
	}

#ifdef AT_EMPTY_PATH
	fchownat(zlog_tmpdirfd, "", zlog_uid, zlog_gid, AT_EMPTY_PATH);
#else
	chown(zlog_tmpdir, zlog_uid, zlog_gid);
#endif

	hook_call(zlog_init, progname, protoname, instance, uid, gid);
	return;

out_warn:
	zlog_err("crashlog and per-thread log buffering unavailable!");
	hook_call(zlog_init, progname, protoname, instance, uid, gid);
}

/*
 * zlog_fini - 关闭日志核心，触发 zlog_fini 钩子（由目标侧做清理）。
 */
void zlog_fini(void)
{
	hook_call(zlog_fini);
}

/*
 * Remove the process's temp log dir, at shutdown
 */
/*
 * zlog_tmpdir_fini - 退出时删除本进程的临时日志目录：关闭其 fd 并 rmdir。
 */
void zlog_tmpdir_fini(void)
{
	if (zlog_tmpdirfd >= 0) {
		close(zlog_tmpdirfd);
		zlog_tmpdirfd = -1;

		if (rmdir(zlog_tmpdir))
			zlog_err("failed to rmdir \"%s\": %s",
				 zlog_tmpdir, strerror(errno));
	}
}
