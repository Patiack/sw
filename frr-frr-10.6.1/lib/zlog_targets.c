// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-19  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：zlog_targets.c —— zlog 内置目标的实现
 *
 * 本文件实现 zlog 核心所用的几个“内置目标”，即日志消息的最终落点。每个目标都是一个
 * 内嵌 struct zlog_target 的更大结构（zlt_fd / zlt_syslog），并按需提供两个写函数：
 *   - logfn：普通路径，接收一批 zlog_msg，先按各自 prio_min 过滤再渲染输出。
 *   - logfn_sigsafe：崩溃 / 信号路径，接收已经渲染好的纯文本，只能使用异步信号安全调用。
 *
 * 内置目标一览：
 *   1. fd / 文件目标（zlt_fd）：本文件主力。zlog_fd() 用 writev 把“时间戳 + 严重级 +
 *      优先级名 + 前缀 + 正文”聚合成 iovec 批量写出，尽量减少系统调用；信号安全版为
 *      zlog_fd_sigsafe()。配套配置管理见 zlog_file_init / fini / cycle / rotate 系列。
 *   2. 崩溃日志目标（zlog_crashlog）：把 LOG_CRIT 及以上写入 tmpdir 下的 crashlog 文件。
 *   3. 辅助程序 stdout 目标（zlog_aux_stdout）：供工具 / 测试程序使用，写 stdout。
 *   4. 启动期 stderr 目标（zlog_startup_stderr）：启动阶段（配置加载前）把告警以上写 stderr。
 *   5. syslog 目标（zlt_syslog）：把消息交给系统 syslog，facility 与 prio_min 可配。
 *
 * 配置与并发：目标链以 RCU 无锁替换，替换时先 zlog_target_clone 出新目标，再
 * zlog_target_replace，最后释放旧目标。所有配置修改都用 frr_with_mutex 加锁串行化。
 *
 * 阅读建议：先看 zlog_fd() 理解“一条消息如何变成一次 writev”，再看 zlog_file_cycle()
 * 理解“配置变化如何换出一个新的目标实例”。
 */
#include "zebra.h"

#include <fcntl.h>
#include <sys/un.h>
#include <syslog.h>

#include "memory.h"
#include "frrcu.h"
#include "frr_pthread.h"
#include "printfrr.h"
#include "zlog.h"
#include "zlog_targets.h"

/* these allocations are intentionally left active even when doing full exit
 * cleanup, in order to keep the logging subsystem fully functional until the
 * absolute end.
 */

DEFINE_MGROUP_ACTIVEATEXIT(LOG, "logging subsystem");

DEFINE_MTYPE_STATIC(LOG, LOG_FD,        "log file target");
DEFINE_MTYPE_STATIC(LOG, LOG_FD_NAME,   "log file name");
DEFINE_MTYPE_STATIC(LOG, LOG_FD_ROTATE, "log file rotate helper");
DEFINE_MTYPE_STATIC(LOG, LOG_SYSL,      "syslog target");

/*
 * struct zlt_fd - fd / 文件类目标的实例结构（zt 必须是第一个字段）。
 * fd 为输出文件描述符，用原子变量保存以便无锁读取；ts_subsec 为时间戳亚秒位数；
 * record_priority / record_severity 控制是否输出优先级名 / 严重级名；
 * head_close 用于在 RCU 周期结束后安全地关闭旧 fd。
 */
struct zlt_fd {
	struct zlog_target zt;

	atomic_uint_fast32_t fd;

	char ts_subsec;
	bool record_priority;
	bool record_severity;

	struct rcu_head_close head_close;
};

/*
 * severity_names - 优先级到“严重级标签”文本的映射（如 LOG_ERR 得到 "ERROR: "）。
 * 当目标开启 record_severity 时，加在每条日志正文之前。
 */
static const char *const severity_names[] = {
	[LOG_EMERG] = "EMERG: ", [LOG_ALERT] = "ALERT: ",  [LOG_CRIT] = "CRIT: ",
	[LOG_ERR] = "ERROR: ",	 [LOG_WARNING] = "WARN: ", [LOG_NOTICE] = "NOTICE: ",
	[LOG_INFO] = "INFO: ",	 [LOG_DEBUG] = "DEBUG: ",
};

/*
 * prionames - 优先级到“优先级名”文本的映射（如 LOG_ERR 得到 "errors: "）。
 * 当目标开启 record_priority 时，加在每条日志正文之前。
 */
static const char * const prionames[] = {
	[LOG_EMERG] =	"emergencies: ",
	[LOG_ALERT] =	"alerts: ",
	[LOG_CRIT] =	"critical: ",
	[LOG_ERR] =	"errors: ",
	[LOG_WARNING] =	"warnings: ",
	[LOG_NOTICE] =	"notifications: ",
	[LOG_INFO] =	"informational: ",
	[LOG_DEBUG] =	"debugging: ",
};

/*
 * zlog_fd - 文件 / fd 目标的普通写函数，实现一次 zlog 调用的“落盘聚合”。
 * 输入为一组 msgs（nmsgs 个）与目标 zt；对每个消息：
 *   1. 若优先级高于阈值（数值更大）则跳过（prio <= zt->prio_min 才输出）；
 *   2. 依次渲染时间戳（ZLOG_TS_LEGACY）、可选严重级、优先级名、全局前缀与正文，
 *      压入 iovec 数组；
 *   3. 当时间戳缓冲将满、到了最后一条、或 iovec 快用完时，用 writev 一次性写出并复位。
 * 无返回值。坑：fd 为 -1 时 writev 失败但不致命；assert 保证循环结束时 iovpos 归零。
 */
void zlog_fd(struct zlog_target *zt, struct zlog_msg *msgs[], size_t nmsgs)
{
	struct zlt_fd *zte = container_of(zt, struct zlt_fd, zt);
	int fd;
	size_t i, textlen, iovpos = 0;
	size_t niov = MIN(4 * nmsgs + 1, IOV_MAX);
	struct iovec iov[niov];
	/* "\nYYYY-MM-DD HH:MM:SS.NNNNNNNNN+ZZ:ZZ " = 37 chars */
#define TS_LEN 40
	char ts_buf[TS_LEN * nmsgs], *ts_pos = ts_buf;

	fd = atomic_load_explicit(&zte->fd, memory_order_relaxed);

	for (i = 0; i < nmsgs; i++) {
		struct zlog_msg *msg = msgs[i];
		int prio = zlog_msg_prio(msg);

		if (prio <= zt->prio_min) {
			struct fbuf fbuf = {
				.buf = ts_buf,
				.pos = ts_pos,
				.len = sizeof(ts_buf),
			};

			iov[iovpos].iov_base = ts_pos;
			zlog_msg_ts(msg, &fbuf,
				    ZLOG_TS_LEGACY | zte->ts_subsec);
			ts_pos = fbuf.pos;

			*ts_pos++ = ' ';
			iov[iovpos].iov_len =
				ts_pos - (char *)iov[iovpos].iov_base;

			iovpos++;

			if (zte->record_severity) {
				iov[iovpos].iov_base = (char *)severity_names[prio];
				iov[iovpos].iov_len = strlen(iov[iovpos].iov_base);

				iovpos++;
			}

			if (zte->record_priority) {
				iov[iovpos].iov_base = (char *)prionames[prio];
				iov[iovpos].iov_len =
					strlen(iov[iovpos].iov_base);

				iovpos++;
			}

			iov[iovpos].iov_base = zlog_prefix;
			iov[iovpos].iov_len = zlog_prefixsz;

			iovpos++;

			iov[iovpos].iov_base =
				(char *)zlog_msg_text(msg, &textlen);
			iov[iovpos].iov_len = textlen + 1;

			iovpos++;
		}

		/* conditions that trigger writing:
		 *  - out of space for more timestamps/headers
		 *  - this being the last message in the batch
		 *  - not enough remaining iov entries
		 */
		if (iovpos > 0 && (ts_buf + sizeof(ts_buf) - ts_pos < TS_LEN
				   || i + 1 == nmsgs
				   || array_size(iov) - iovpos < 5)) {
			writev(fd, iov, iovpos);

			iovpos = 0;
			ts_pos = ts_buf;
		}
	}

	assert(iovpos == 0);
}

/*
 * zlog_fd_sigsafe - 文件 / fd 目标的信号安全写函数。
 * 只做最少工作：拼一个固定以 LOG_CRIT 表示“严重级 / 优先级名 + 前缀 + 文本 + 换行”的
 * iovec 并 writev。参数 text 已在别处渲染好且不含换行（len 为其长度）。
 * 坑：只能用异步信号安全函数；不渲染时间戳。
 */
static void zlog_fd_sigsafe(struct zlog_target *zt, const char *text,
			    size_t len)
{
	struct zlt_fd *zte = container_of(zt, struct zlt_fd, zt);
	struct iovec iov[5];
	int fd, i = 0;

	if (zte->record_severity) {
		iov[i].iov_base = (char *)severity_names[LOG_CRIT];
		iov[i++].iov_len = strlen(iov[0].iov_base);
	}

	if (zte->record_priority) {
		iov[i].iov_base = (char *)prionames[LOG_CRIT];
		iov[i++].iov_len = strlen(iov[0].iov_base);
	}

	iov[i].iov_base = zlog_prefix;
	iov[i++].iov_len = zlog_prefixsz;

	iov[i].iov_base = (char *)text;
	iov[i++].iov_len = len;

	iov[i].iov_base = (char *)"\n";
	iov[i++].iov_len = 1;

	fd = atomic_load_explicit(&zte->fd, memory_order_relaxed);

	writev(fd, iov, i);
}

/*
 * (re-)configuration
 */

/*
 * zlog_file_init - 初始化文件目标配置对象 zcf：清零、prio_min 设为 ZLOG_DISABLED
 * （默认不输出）、fd 置 -1，并初始化配置互斥锁。
 */
void zlog_file_init(struct zlog_cfg_file *zcf)
{
	memset(zcf, 0, sizeof(*zcf));
	zcf->prio_min = ZLOG_DISABLED;
	zcf->fd = -1;
	pthread_mutex_init(&zcf->cfg_mtx, NULL);
}

/*
 * zlog_file_target_free - 释放一个 zlt_fd 目标实例。
 * 用 rcu_close 延后关闭其 fd（等 RCU 周期结束，确保没有线程仍在写），再 rcu_free 回收内存。
 * 参数 zlt 可为 NULL（直接返回）。
 */
static void zlog_file_target_free(struct zlt_fd *zlt)
{
	if (!zlt)
		return;

	rcu_close(&zlt->head_close, zlt->fd);
	rcu_free(MTYPE_LOG_FD, zlt, zt.rcu_head);
}

/*
 * zlog_file_fini - 反初始化文件目标配置对象 zcf。
 * 若仍有活跃目标，先把它从目标链中摘除并释放；再释放保存的文件名、销毁互斥锁。
 */
void zlog_file_fini(struct zlog_cfg_file *zcf)
{
	if (zcf->active) {
		struct zlt_fd *ztf;
		struct zlog_target *zt;

		zt = zlog_target_replace(&zcf->active->zt, NULL);
		ztf = container_of(zt, struct zlt_fd, zt);
		zlog_file_target_free(ztf);
	}
	XFREE(MTYPE_LOG_FD_NAME, zcf->filename);
	pthread_mutex_destroy(&zcf->cfg_mtx);
}

/*
 * zlog_file_cycle - 根据 zcf 的当前配置，“换出”一个新的 fd 目标实例。
 * 流程：若被禁用则跳过；否则按 fd 或 filename 打开（dup / open）新 fd；用
 * zlog_target_clone 基于旧目标克隆出新目标并填好 fd 与各记录选项；调用
 * zlog_target_replace 原子替换，并释放被替换下来的旧目标。
 * 返回值：true 表示成功；false 表示打开失败（此时目标保持原样）。
 * 坑：应在持有 cfg_mtx 时调用；zlog_wrap 非空时用它替代默认 logfn。
 */
static bool zlog_file_cycle(struct zlog_cfg_file *zcf)
{
	struct zlog_target *zt, *old;
	struct zlt_fd *zlt = NULL;
	int fd;
	bool rv = true;

	do {
		if (zcf->prio_min == ZLOG_DISABLED)
			break;

		if (zcf->fd != -1)
			fd = dup(zcf->fd);
		else if (zcf->filename)
			fd = open(zcf->filename,
				  O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC
					| O_NOCTTY,
				  LOGFILE_MASK);
		else
			fd = -1;

		if (fd < 0) {
			rv = false;
			break;
		}

		zt = zlog_target_clone(MTYPE_LOG_FD,
				       zcf->active ? &zcf->active->zt : NULL,
				       sizeof(*zlt));
		zlt = container_of(zt, struct zlt_fd, zt);

		zlt->fd = fd;
		zlt->record_priority = zcf->record_priority;
		zlt->record_severity = zcf->record_severity;
		zlt->ts_subsec = zcf->ts_subsec;

		zlt->zt.prio_min = zcf->prio_min;
		zlt->zt.logfn = zcf->zlog_wrap ? zcf->zlog_wrap : zlog_fd;
		zlt->zt.logfn_sigsafe = zlog_fd_sigsafe;
	} while (0);

	old = zlog_target_replace(zcf->active ? &zcf->active->zt : NULL,
				  zlt ? &zlt->zt : NULL);
	zcf->active = zlt;

	zlog_file_target_free(container_of_null(old, struct zlt_fd, zt));

	return rv;
}

/*
 * zlog_file_set_other - 应用“其它”配置字段（prio_min / ts_subsec / record_*）到目标。
 * 做法其实是整体重跑一遍 zlog_file_cycle()，在锁保护下换出目标。无返回值。
 */
void zlog_file_set_other(struct zlog_cfg_file *zcf)
{
	frr_with_mutex (&zcf->cfg_mtx) {
		zlog_file_cycle(zcf);
	}
}

/*
 * zlog_file_set_filename - 让 zcf 输出到名为 filename 的文件。
 * 先替换保存的文件名、把 fd 置 -1，再 zlog_file_cycle() 换出目标；成功返回 true。
 * 坑：末尾的 assert(0) 表示 frr_with_mutex 宏块理论上必然 return。
 */
bool zlog_file_set_filename(struct zlog_cfg_file *zcf, const char *filename)
{
	frr_with_mutex (&zcf->cfg_mtx) {
		XFREE(MTYPE_LOG_FD_NAME, zcf->filename);
		zcf->filename = XSTRDUP(MTYPE_LOG_FD_NAME, filename);
		zcf->fd = -1;

		return zlog_file_cycle(zcf);
	}
	assert(0);
	return false;
}

/*
 * zlog_file_set_fd - 让 zcf 输出到 fd 指定的文件描述符。
 * 若 fd 与当前相同直接返回 true；否则清掉已保存的文件名、记录新 fd 并换出目标。
 */
bool zlog_file_set_fd(struct zlog_cfg_file *zcf, int fd)
{
	frr_with_mutex (&zcf->cfg_mtx) {
		if (zcf->fd == fd)
			return true;

		XFREE(MTYPE_LOG_FD_NAME, zcf->filename);
		zcf->fd = fd;

		return zlog_file_cycle(zcf);
	}
	assert(0);
	return false;
}

/*
 * struct rcu_close_rotate - 轮转时用于“两步延迟释放”的辅助结构：
 * 一个 rcu_head_close 负责延迟关闭旧 fd，一个 rcu_head 负责延迟释放本结构自身。
 */
struct rcu_close_rotate {
	struct rcu_head_close head_close;
	struct rcu_head head_self;
};

/*
 * zlog_file_rotate - 触发文件轮转：重新打开 zcf->filename 得到新 fd，并原子替换
 * 活跃目标里的 fd；旧 fd 交给 RCU 延迟关闭。
 * 返回值：true 成功，false 表示没有活跃目标 / 无文件名 / 打开失败。
 * 坑：需在 cfg_mtx 锁保护下修改配置；替换用 atomic_exchange。
 */
bool zlog_file_rotate(struct zlog_cfg_file *zcf)
{
	struct rcu_close_rotate *rcr;
	int fd;

	frr_with_mutex (&zcf->cfg_mtx) {
		if (!zcf->active || !zcf->filename)
			return true;

		fd = open(zcf->filename,
			  O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOCTTY,
			  LOGFILE_MASK);
		if (fd < 0)
			return false;

		fd = atomic_exchange_explicit(&zcf->active->fd,
					      (uint_fast32_t)fd,
					      memory_order_relaxed);
	}

	rcr = XCALLOC(MTYPE_LOG_FD_ROTATE, sizeof(*rcr));
	rcu_close(&rcr->head_close, fd);
	rcu_free(MTYPE_LOG_FD_ROTATE, rcr, head_self);

	return true;
}

/* fixed crash logging */

/*
 * zlog_crashlog - 固定的崩溃日志目标（全局单例），把 LOG_CRIT 以上的消息写入
 * tmpdir 下的 crashlog 文件。它同时提供普通与信号安全两种写函数。
 */
static struct zlt_fd zlog_crashlog;

/*
 * zlog_crashlog_sigsafe - 崩溃日志目标的信号安全写函数。
 * 首次调用时用 openat 在 zlog_tmpdirfd 下打开 "crashlog" 文件（失败则记为 -2 并放弃），
 * 之后复用该 fd，经 zlog_fd_sigsafe 写出。参数 text / len 为已渲染文本。
 * 坑：只做必要工作，尽量少用非异步信号安全调用。
 */
static void zlog_crashlog_sigsafe(struct zlog_target *zt, const char *text,
				  size_t len)
{
	static int crashlog_fd = -1;

	if (crashlog_fd == -1) {
#ifdef HAVE_OPENAT
		crashlog_fd = openat(zlog_tmpdirfd, "crashlog",
				     O_WRONLY | O_APPEND | O_CREAT,
				     LOGFILE_MASK);
#endif
		if (crashlog_fd < 0)
			crashlog_fd = -2;
	}

	if (crashlog_fd == -2)
		return;

	zlog_crashlog.fd = crashlog_fd;
	zlog_fd_sigsafe(&zlog_crashlog.zt, text, len);
}

/* this is used for assert failures (they don't need AS-Safe logging) */
/*
 * zlog_crashlog_plain - 崩溃日志目标的普通写函数（用于 assert 失败等非信号上下文）。
 * 遍历 msgs，按 prio_min 过滤后取出文本，再转交 zlog_crashlog_sigsafe 写出。
 */
static void zlog_crashlog_plain(struct zlog_target *zt, struct zlog_msg *msgs[],
				size_t nmsgs)
{
	size_t i, len;
	const char *text;

	for (i = 0; i < nmsgs; i++) {
		if (zlog_msg_prio(msgs[i]) > zt->prio_min)
			continue;

		text = zlog_msg_text(msgs[i], &len);
		zlog_crashlog_sigsafe(zt, text, len);
	}
}

/*
 * zlog_crashlog_init - 初始化并挂载崩溃日志目标：prio_min 设为 LOG_CRIT，
 * 安装普通 / 信号安全写函数，并把 zlog_crashlog 作为一个新目标加入目标链。
 */
static void zlog_crashlog_init(void)
{
	zlog_crashlog.zt.prio_min = LOG_CRIT;
	zlog_crashlog.zt.logfn = zlog_crashlog_plain;
	zlog_crashlog.zt.logfn_sigsafe = zlog_crashlog_sigsafe;
	zlog_crashlog.fd = -1;

	zlog_target_replace(NULL, &zlog_crashlog.zt);
}

/* fixed logging for test/auxiliary programs */

/*
 * zlog_aux_stdout / zlog_is_aux - 辅助程序（工具 / 测试）专用目标与标志。
 * zlog_is_aux 标记本进程是辅助程序，从而跳过 daemon 专属的崩溃日志初始化等步骤。
 */
static struct zlt_fd zlog_aux_stdout;
static bool zlog_is_aux;

/*
 * zlt_aux_init - zlog_aux_init 钩子的实现：把 stdout 目标（prio_min 为 prio_min）
 * 挂入目标链，随后调用 zlog_startup_end()。返回 0。
 */
static int zlt_aux_init(const char *prefix, int prio_min)
{
	zlog_is_aux = true;

	zlog_aux_stdout.zt.prio_min = prio_min;
	zlog_aux_stdout.zt.logfn = zlog_fd;
	zlog_aux_stdout.zt.logfn_sigsafe = zlog_fd_sigsafe;
	zlog_aux_stdout.fd = STDOUT_FILENO;

	zlog_target_replace(NULL, &zlog_aux_stdout.zt);
	zlog_startup_end();
	return 0;
}

/*
 * zlt_init - zlog_init 钩子的实现：调用 openlog，把 syslog 连接信息登记好
 * （facility 用 LOG_DAEMON）。返回 0。
 */
static int zlt_init(const char *progname, const char *protoname,
		     unsigned short instance, uid_t uid, gid_t gid)
{
	openlog(progname, LOG_CONS | LOG_NDELAY | LOG_PID, LOG_DAEMON);
	return 0;
}

/*
 * zlt_fini - zlog_fini 钩子的实现：调用 closelog 关闭 syslog 连接。返回 0。
 */
static int zlt_fini(void)
{
	closelog();
	return 0;
}

/* fixed startup logging to stderr */

/*
 * zlog_startup_stderr - 启动阶段（配置加载前）的 stderr 目标（全局单例）。
 * 让启动早期的告警及以上能够直接写 stderr，以免在日志系统尚未配置好时丢消息。
 */
static struct zlt_fd zlog_startup_stderr;

/*
 * zlog_startup_init - 带 _CONSTRUCTOR(450) 的构造器，在库加载早期自动运行：
 * 建立启动期 stderr 目标（prio_min 为 LOG_WARNING），并注册 zlog_aux_init /
 * zlog_init / zlog_fini 三个钩子的处理函数。
 */
__attribute__((_CONSTRUCTOR(450))) static void zlog_startup_init(void)
{
	zlog_startup_stderr.zt.prio_min = LOG_WARNING;
	zlog_startup_stderr.zt.logfn = zlog_fd;
	zlog_startup_stderr.zt.logfn_sigsafe = zlog_fd_sigsafe;
	zlog_startup_stderr.fd = STDERR_FILENO;

	zlog_target_replace(NULL, &zlog_startup_stderr.zt);

	hook_register(zlog_aux_init, zlt_aux_init);
	hook_register(zlog_init, zlt_init);
	hook_register(zlog_fini, zlt_fini);
}

/*
 * zlog_startup_end - 宣布启动阶段结束：摘除启动期 stderr 目标；
 * 若不是辅助程序，则接着初始化崩溃日志目标（此前崩溃日志写 stderr，此后写 crashlog）。
 * 用静态标志保证只执行一次。
 */
void zlog_startup_end(void)
{
	static bool startup_ended = false;

	if (startup_ended)
		return;
	startup_ended = true;

	zlog_target_replace(&zlog_startup_stderr.zt, NULL);

	if (zlog_is_aux)
		return;

	/* until here, crashlogs go to stderr */
	zlog_crashlog_init();
}

/* syslog */

/*
 * struct zlt_syslog - syslog 目标的实例结构（zt 为第一个字段）。
 * syslog_facility 记录该目标使用的 facility。
 */
struct zlt_syslog {
	struct zlog_target zt;

	int syslog_facility;
};

/*
 * zlog_syslog - syslog 目标的普通写函数。遍历 msgs，按 prio_min 过滤，
 * 用 syslog() 把每条消息文本送出（优先级与 facility 按位或）。无返回值。
 */
static void zlog_syslog(struct zlog_target *zt, struct zlog_msg *msgs[],
			size_t nmsgs)
{
	size_t i;
	struct zlt_syslog *zte = container_of(zt, struct zlt_syslog, zt);
	const char *text;
	size_t text_len;

	for (i = 0; i < nmsgs; i++) {
		if (zlog_msg_prio(msgs[i]) > zt->prio_min)
			continue;

		text = zlog_msg_text(msgs[i], &text_len);
		syslog(zlog_msg_prio(msgs[i]) | zte->syslog_facility, "%.*s",
		       (int)text_len, text);
	}
}

#ifndef _PATH_LOG
/*
 * _PATH_LOG - 系统日志套接字路径，默认为 "/dev/log"；若系统头文件已定义则不覆盖。
 */
#define _PATH_LOG "/dev/log"
#endif

/*
 * zlog_syslog_sigsafe - syslog 目标的信号安全写函数。
 * 首次调用时尝试连接 _PATH_LOG 的 UNIX 数据报套接字（失败则退化为按文件打开，再失败
 * 记为 -2 放弃）；随后拼出 "<prio>前缀[pid]: " 头部与文本，用 writev 直接写入该 fd。
 * 参数 text / len 为已渲染文本。坑：只使用异步信号安全调用。
 */
static void zlog_syslog_sigsafe(struct zlog_target *zt, const char *text,
				size_t len)
{
	static int syslog_fd = -1;

	char hdr[192];
	size_t hdrlen;
	struct iovec iov[2];

	if (syslog_fd == -1) {
		syslog_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (syslog_fd >= 0) {
			struct sockaddr_un sa;
			socklen_t salen = sizeof(sa);

			sa.sun_family = AF_UNIX;
			strlcpy(sa.sun_path, _PATH_LOG, sizeof(sa.sun_path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
			salen = sa.sun_len = SUN_LEN(&sa);
#endif
			if (connect(syslog_fd, (struct sockaddr *)&sa, salen)) {
				close(syslog_fd);
				syslog_fd = -1;
			}
		}

		/* /dev/log could be a fifo instead of a socket */
		if (syslog_fd == -1) {
			syslog_fd = open(_PATH_LOG, O_WRONLY | O_NOCTTY);
			if (syslog_fd < 0)
				/* give up ... */
				syslog_fd = -2;
		}
	}

	if (syslog_fd == -2)
		return;

	/* note zlog_prefix includes trailing ": ", need to cut off 2 chars */
	hdrlen = snprintfrr(hdr, sizeof(hdr), "<%d>%.*s[%ld]: ", LOG_CRIT,
			    zlog_prefixsz > 2 ? (int)(zlog_prefixsz - 2) : 0,
			    zlog_prefix, (long)getpid());

	iov[0].iov_base = hdr;
	iov[0].iov_len = hdrlen;

	iov[1].iov_base = (char *)text;
	iov[1].iov_len = len;

	writev(syslog_fd, iov, array_size(iov));
}


/*
 * syslog 配置的全局状态与互斥锁：
 *   syslog_cfg_mutex - 串行化 syslog 配置修改。
 *   zlt_syslog - 当前活跃的 syslog 目标（空表示未启用）。
 *   syslog_facility - 当前 facility（默认 LOG_DAEMON）。
 *   syslog_prio_min - 当前最低优先级（默认 ZLOG_DISABLED，即停用）。
 */
static pthread_mutex_t syslog_cfg_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct zlt_syslog *zlt_syslog;
static int syslog_facility = LOG_DAEMON;
static int syslog_prio_min = ZLOG_DISABLED;

/*
 * zlog_syslog_set_facility - 设置 syslog 的 facility，并在启用状态下换出目标使其生效。
 * 若 facility 未变或当前被禁用，只更新全局值并返回。无返回值。
 */
void zlog_syslog_set_facility(int facility)
{
	struct zlog_target *newztc;
	struct zlt_syslog *newzt;

	frr_with_mutex (&syslog_cfg_mutex) {
		if (facility == syslog_facility)
			return;
		syslog_facility = facility;

		if (syslog_prio_min == ZLOG_DISABLED)
			return;

		newztc = zlog_target_clone(MTYPE_LOG_SYSL, &zlt_syslog->zt,
					   sizeof(*newzt));
		newzt = container_of(newztc, struct zlt_syslog, zt);
		newzt->syslog_facility = syslog_facility;

		zlog_target_free(MTYPE_LOG_SYSL,
				 zlog_target_replace(&zlt_syslog->zt,
						     &newzt->zt));

		zlt_syslog = newzt;
	}
}

/*
 * zlog_syslog_get_facility - 返回当前 syslog facility。加锁读取。
 * 坑：末尾 assert(0) 仅为让编译器相信锁块一定返回。
 */
int zlog_syslog_get_facility(void)
{
	frr_with_mutex (&syslog_cfg_mutex) {
		return syslog_facility;
	}
	assert(0);
	return 0;
}

/*
 * zlog_syslog_set_prio_min - 设置 syslog 的最低优先级；传入 ZLOG_DISABLED 即停用。
 * 若变为启用则克隆 / 新建目标并填好 prio_min、facility、写函数，再原子替换；
 * 若变为停用则把目标从链上摘除（newzt 为 NULL）。
 */
void zlog_syslog_set_prio_min(int prio_min)
{
	struct zlog_target *newztc;
	struct zlt_syslog *newzt = NULL;

	frr_with_mutex (&syslog_cfg_mutex) {
		if (prio_min == syslog_prio_min)
			return;
		syslog_prio_min = prio_min;

		if (syslog_prio_min != ZLOG_DISABLED) {
			newztc = zlog_target_clone(MTYPE_LOG_SYSL,
						   zlt_syslog ? &zlt_syslog->zt : NULL,
						   sizeof(*newzt));
			newzt = container_of(newztc, struct zlt_syslog, zt);
			newzt->zt.prio_min = prio_min;
			newzt->zt.logfn = zlog_syslog;
			newzt->zt.logfn_sigsafe = zlog_syslog_sigsafe;
			newzt->syslog_facility = syslog_facility;
		}

		zlog_target_free(MTYPE_LOG_SYSL,
				 zlog_target_replace(zlt_syslog ? &zlt_syslog->zt : NULL,
						     &newzt->zt));

		zlt_syslog = newzt;
	}
}

/*
 * zlog_syslog_get_prio_min - 返回当前 syslog 最低优先级。加锁读取。
 */
int zlog_syslog_get_prio_min(void)
{
	frr_with_mutex (&syslog_cfg_mutex) {
		return syslog_prio_min;
	}
	assert(0);
	return 0;
}
