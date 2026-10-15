// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-19  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：zlog.h —— FRR 新日志核心的对外接口
 *
 * 新日志系统把“一次日志调用”抽象为消息对象 struct zlog_msg，并把它广播给一组目标
 * （target）。核心思想是“延迟格式化”：只有当某个目标确实需要文本时才渲染。
 *
 * 关键概念：
 *   - 调用入口：zlog(prio, fmt, ...) 是最基础的调用；vzlogx / ezlog 接收不同形式参数；
 *     zlog_err / zlog_warn / zlog_info / zlog_notice / zlog_debug 以及带错误码的
 *     flog_err / flog_warn / flog_err_sys 都是便捷宏，通过 _zlog_ecref 记录调用点信息。
 *   - xref：把静态格式串、优先级、错误码登记到一张表，运行时可反查“这条消息在哪定义”。
 *   - struct zlog_msg：不透明消息对象，目标用 zlog_msg_* 系列函数取文本 / 时间戳 / pid。
 *   - struct zlog_target：目标结构，必须内嵌于更大结构且为第一个字段；通过
 *     zlog_target_clone / replace / free 以 RCU 方式无锁替换，详见下方说明。
 *   - 优先级过滤：每个目标有自己的 prio_min，低于阈值的消息被丢弃；ZLOG_DISABLED 表示停用。
 *   - 时间戳格式：ZLOG_TS_ISO8601 / ZLOG_TS_LEGACY 决定样式，ZLOG_TS_UTC 决定时区。
 *
 * 生命周期：daemon 用 zlog_init 初始化，工具程序用 zlog_aux_init；退出前 zlog_fini。
 * 另有许多前缀控制（prefix_ec / prefix_xid）、immediate 模式与 TLS 缓冲接口。
 *
 * 阅读建议：配合 zlog.c（实现）与 zlog_targets.c（内置目标），自上而下追踪
 * “zlog 调用 -> 消息对象 -> 遍历目标 -> 渲染并写出”的完整链路。
 */
#ifndef _FRR_ZLOG_H
#define _FRR_ZLOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/uio.h>

#include <assert.h>

#include "atomlist.h"
#include "frrcu.h"
#include "memory.h"
#include "hook.h"
#include "printfrr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DECLARE_MGROUP(LOG) - 声明日志模块专用的内存类型组，供 zlog 内部各分配点命名使用，
 * 便于内存统计与泄漏排查。
 */
DECLARE_MGROUP(LOG);

/*
 * 全局日志上下文变量：
 *   zlog_prefix / zlog_prefixsz - 每条日志前附加的前缀（如 "bgpd[1234]:"）及其长度。
 *   zlog_tmpdirfd - 临时目录的 fd，用于崩溃时落盘。
 *   zlog_instance - 本进程的实例号（多实例场景）。
 *   zlog_progname - 进程名（如 "bgpd"），由 zlog_init 设置。
 */
extern char zlog_prefix[];
extern size_t zlog_prefixsz;
extern int zlog_tmpdirfd;
extern int zlog_instance;
extern const char *zlog_progname;

/*
 * struct xref_logmsg - 一条日志调用的静态元数据（交叉引用条目）。
 * fmtstring 是格式串原文，priority 是其优先级，ec 是错误码（错误日志用），
 * args 是传给格式串的参数名文本。xref 字段用于把本条目登记进全局 xref 表，
 * 从而支持运行时反查消息来源，并据此实现按消息启停等日志配置。
 */
struct xref_logmsg {
	struct xref xref;

	const char *fmtstring;
	uint32_t priority;
	uint32_t ec;
	const char *args;
};

/* whether flag was added in config mode or enable mode */
/*
 * LOGMSG_FLAG_EPHEMERAL - 该日志消息的启用状态是在 enable 模式（运行期）配置的。
 * LOGMSG_FLAG_PERSISTENT - 该日志消息的启用状态是在 config 模式（持久配置）里配置的。
 * 两者用于区分配置来源，决定重启后是否保留。
 */
#define LOGMSG_FLAG_EPHEMERAL	(1 << 0)
#define LOGMSG_FLAG_PERSISTENT	(1 << 1)

/*
 * struct xrefdata_logmsg - xref_logmsg 对应的运行时数据。
 * fl_print_bt 是“是否打印回溯”的用户可配置标志位。
 */
struct xrefdata_logmsg {
	struct xrefdata xrefdata;

	uint8_t fl_print_bt;
};

/* These functions are set up to write to stdout/stderr without explicit
 * initialization and/or before config load.  There is no need to call e.g.
 * fprintf(stderr, ...) just because it's "too early" at startup.  Depending
 * on context, it may still be the right thing to use fprintf though -- try to
 * determine whether something is a log message or something else.
 */

/*
 * vzlogx - 最底层的可变参数日志入口。prio 为优先级，fmt 为格式串，ap 为参数列表；
 * xref 为该消息的静态元数据，可为 NULL。宏 vzlog(prio, ...) 是 xref 为 NULL 的简写。
 */
extern void vzlogx(const struct xref_logmsg *xref, int prio, const char *fmt,
		   va_list ap) PRINTFRR(3, 0);
#define vzlog(prio, ...) vzlogx(NULL, prio, __VA_ARGS__)

/*
 * ezlog - 便捷变参入口，接收 prio、fmt 及后续可变参数，内部无 xref。
 * 参数风格与 printf 相同；返回 void。
 */
extern void ezlog(int prio, const char *fmt, ...) PRINTFRR(2, 3);

PRINTFRR(2, 3)
/*
 * zlog - 最常用的日志入口（内联）。把可变参数打包后交给 vzlog 处理。
 * 参数 prio 为优先级，fmt 为 printfrr 风格格式串。
 */
static inline void zlog(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vzlog(prio, fmt, ap);
	va_end(ap);
}

PRINTFRR(2, 3)
/*
 * zlog_ref - 与 zlog 类似，但携带 xref 元数据 xref，优先级取自 xref->priority。
 */
static inline void zlog_ref(const struct xref_logmsg *xref,
			    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vzlogx(xref, xref->priority, fmt, ap);
	va_end(ap);
}

/*
 * _zlog_ecref - 所有带 xref 的便捷日志宏的公共实现。
 * 它在调用点静态生成两个对象：_xrefdata（运行时数据）与 _xref（静态元数据），
 * 把格式串 msg、优先级 prio、错误码 ec_ 及参数名文本 #__VA_ARGS__ 登记进去，
 * 再用 XREF_LINK 挂进全局表，最后通过 zlog_ref 发出。因此每条消息都会出现在
 * 交叉引用表里，便于按消息来源查询与配置。
 */
#define _zlog_ecref(ec_, prio, msg, ...)                                       \
	do {                                                                   \
		static struct xrefdata_logmsg _xrefdata = {                    \
			.xrefdata =                                            \
				{                                              \
					.xref = NULL,                          \
					.uid = {},                             \
					.hashstr = (msg),                      \
					.hashu32 = {(prio), (ec_)},            \
				},                                             \
		};                                                             \
		static const struct xref_logmsg _xref __attribute__(           \
			(used)) = {                                            \
			.xref = XREF_INIT(XREFT_LOGMSG, &_xrefdata.xrefdata,   \
					  __func__),                           \
			.fmtstring = (msg),                                    \
			.priority = (prio),                                    \
			.ec = (ec_),                                           \
			.args = (#__VA_ARGS__),                                \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		zlog_ref(&_xref, (msg), ##__VA_ARGS__);                        \
	} while (0)

/*
 * zlog_err / zlog_warn / zlog_info / zlog_notice / zlog_debug - 固定优先级的便捷日志宏，
 * 错误码 ec 传 0（表示“无对应错误码”）。真正带错误码的见下方 flog_* 系列。
 */
#define zlog_err(...)    _zlog_ecref(0, LOG_ERR, __VA_ARGS__)
#define zlog_warn(...)   _zlog_ecref(0, LOG_WARNING, __VA_ARGS__)
#define zlog_info(...)   _zlog_ecref(0, LOG_INFO, __VA_ARGS__)
#define zlog_notice(...) _zlog_ecref(0, LOG_NOTICE, __VA_ARGS__)
#define zlog_debug(...)  _zlog_ecref(0, LOG_DEBUG, __VA_ARGS__)

/*
 * flog_err - 带错误码 ferr_id 的 LOG_ERR 日志。
 * flog_warn - 带错误码 ferr_id 的 LOG_WARNING 日志。
 * flog_err_sys - 带错误码的 LOG_ERR（历史命名，语义与 flog_err 相同）。
 * 错误码会随消息一起记录，供错误目录 / 文档查阅。
 */
#define flog_err(ferr_id, format, ...)                                         \
	_zlog_ecref(ferr_id, LOG_ERR, format, ## __VA_ARGS__)
#define flog_warn(ferr_id, format, ...)                                        \
	_zlog_ecref(ferr_id, LOG_WARNING, format, ## __VA_ARGS__)

#define flog_err_sys(ferr_id, format, ...)                                     \
	_zlog_ecref(ferr_id, LOG_ERR, format, ## __VA_ARGS__)

/*
 * zlog_sigsafe - 异步信号安全的文本日志，直接把 text（长 len）写出，不做格式化。
 * 只应在崩溃处理路径使用。
 */
extern void zlog_sigsafe(const char *text, size_t len);

/* recirculate a log message from zlog_live */
/*
 * zlog_recirculate_live_msg - 把来自 zlog_live（实时日志转发）的一条消息重新注入日志系统。
 * data / len 为原始缓冲；用于让 live 订阅得到的消息再走一遍本地目标。
 */
extern void zlog_recirculate_live_msg(uint8_t *data, size_t len);

/* extra priority value to disable a target without deleting it */
/*
 * ZLOG_DISABLED - 特殊的优先级值（比 LOG_EMERG 还低一位）。
 * 把目标的 prio_min 设为它即可停用该目标，而无需从目标链中删除。
 */
#define ZLOG_DISABLED	(LOG_EMERG-1)

/* zlog_msg encapsulates a particular logging call from somewhere in the code.
 * The same struct is passed around to all zlog_targets.
 *
 * This is used to defer formatting the log message until it is actually
 * requested by one of the targets.  If none of the targets needs the message
 * formatted, the formatting call is avoided entirely.
 *
 * This struct is opaque / private to the core zlog code.  Logging targets
 * should use zlog_msg_* functions to get text / timestamps / ... for a
 * message.
 */

/*
 * struct zlog_msg - 不透明（opaque）的日志消息对象，核心私有，目标不应直接访问其字段。
 * 目标请使用 zlog_msg_* 系列函数获取文本、时间戳、pid 等信息。
 * 由于延迟格式化，若某次调用没有任何目标需要文本，则根本不会渲染。
 */
struct zlog_msg;

/*
 * zlog_msg_prio - 返回该消息的优先级。
 * zlog_msg_xref - 返回该消息的 xref 元数据（可能为 NULL），可用于取错误码等。
 */
extern int zlog_msg_prio(struct zlog_msg *msg);
extern const struct xref_logmsg *zlog_msg_xref(struct zlog_msg *msg);

/* text is NOT \0 terminated; instead there is a \n after textlen since the
 * logging targets would jump extra hoops otherwise for a single byte.  (the
 * \n is not included in textlen)
 *
 * calling this with NULL textlen is likely wrong.
 * use  "%.*s", (int)textlen, text  when passing to printf-like functions
 */
/*
 * zlog_msg_text - 取消息正文。返回的文本不以空字符结尾，而是在 textlen 之后紧跟一个换行符，
 * 且该换行符不计入返回的 textlen。传 NULL 的 textlen 基本是错的。
 * 传给 printf 风格函数时应写成 "%.*s", (int)textlen, text。
 */
extern const char *zlog_msg_text(struct zlog_msg *msg, size_t *textlen);

/*
 * zlog_msg_args - 取消息的头部长度与参数在格式化输出中的位置信息，供需要高亮 /
 * 结构化的目标使用。hdrlen 为头部长度；n_argpos 为参数位置个数；argpos 为位置数组。
 */
extern void zlog_msg_args(struct zlog_msg *msg, size_t *hdrlen,
			  size_t *n_argpos, const struct fmt_outpos **argpos);

/* timestamp formatting control flags */

/* sub-second digit count */
/*
 * 时间戳格式化标志：
 *   ZLOG_TS_PREC - 低 4 位掩码，表示亚秒位数。
 *   ZLOG_TS_ISO8601 - 采用 ISO8601 样式（配合 ZLOG_TS_UTC 时带 Z 后缀，否则带时区偏移）。
 *   ZLOG_TS_LEGACY - 采用老式 "0000 / 00 / 00 00:00:00" 样式（不标注时区）。
 *   ZLOG_TS_UTC - 使用 UTC，否则默认本地时区。
 */
#define ZLOG_TS_PREC		0xfU

/* 8601:   0000-00-00T00:00:00Z      (if used with ZLOG_TS_UTC)
 *         0000-00-00T00:00:00+00:00 (otherwise)
 * Legacy: 0000/00/00 00:00:00       (no TZ indicated!)
 */
#define ZLOG_TS_ISO8601		(1 << 8)
#define ZLOG_TS_LEGACY		(1 << 9)

/* default is local time zone */
#define ZLOG_TS_UTC		(1 << 10)

struct timespec;

/*
 * zlog_msg_ts - 按 flags 控制，把该消息的时间戳渲染进输出缓冲 out，返回写入长度。
 * zlog_msg_tsraw - 取原始 struct timespec，不做渲染。
 */
extern size_t zlog_msg_ts(struct zlog_msg *msg, struct fbuf *out,
			  uint32_t flags);
extern void zlog_msg_tsraw(struct zlog_msg *msg, struct timespec *ts);

/* "mmm dd hh:mm:ss" for RFC3164 syslog.  Only ZLOG_TS_UTC for flags. */
/*
 * zlog_msg_ts_3164 - 生成 RFC3164 syslog 使用的时间戳样式 "mmm dd hh:mm:ss"。
 * flags 目前只支持 ZLOG_TS_UTC；返回写入 out 的长度。
 */
extern size_t zlog_msg_ts_3164(struct zlog_msg *msg, struct fbuf *out,
			       uint32_t flags);

/* currently just returns the current PID/TID since we never write another
 * thread's messages
 */
/*
 * zlog_msg_pid - 取该消息关联的 pid 与 tid。当前实现直接返回当前进程 / 线程号，
 * 因为不会去写别的线程产生的消息。
 */
extern void zlog_msg_pid(struct zlog_msg *msg, intmax_t *pid, intmax_t *tid);

/* This list & struct implements the actual logging targets.  It is accessed
 * lock-free from all threads, and thus MUST only be changed atomically, i.e.
 * RCU.
 *
 * Since there's no atomic replace, the replacement action is an add followed
 * by a delete.  This means that during logging config changes, log messages
 * may be duplicated in the log target that is being changed.  The old entry
 * being changed MUST also at the very least not crash or do other stupid
 * things.
 *
 * This list and struct are NOT related to config.  Logging config is kept
 * separately, and results in creating appropriate zlog_target(s) to realize
 * the config.  Log targets may also be created from varying sources, e.g.
 * command line options, or VTY commands ("log monitor").
 *
 * struct zlog_target is intended to be embedded into a larger structure that
 * contains additional field for the specific logging target, e.g. an fd or
 * additional options.  It MUST be the first field in that larger struct.
 */

/*
 * 目标链与 struct zlog_target：
 *   PREDECL_ATOMLIST(zlog_targets) 声明目标链表的原子操作（定义见 atomlist.h）。
 *   struct zlog_target 必须内嵌在更大的目标结构里，且必须是第一个字段。
 *   prio_min 为该目标的最低优先级阈值；logfn 为普通写函数；
 *   logfn_sigsafe 为崩溃路径专用写函数（若该目标无法安全写崩溃日志则置 NULL），
 *   其文本不以空字符结尾且按行切分（不含换行）；rcu_head 供 RCU 释放使用。
 *   链表以无锁方式被所有线程访问，因此只允许原子替换（RCU）。
 */
PREDECL_ATOMLIST(zlog_targets);
struct zlog_target {
	struct zlog_targets_item head;

	int prio_min;

	void (*logfn)(struct zlog_target *zt, struct zlog_msg *msg[],
		      size_t nmsgs);

	/* for crash handlers, set to NULL if log target can't write crash logs
	 * without possibly deadlocking (AS-Safe)
	 *
	 * text is not \0 terminated & split up into lines (e.g. no \n)
	 */
	void (*logfn_sigsafe)(struct zlog_target *zt, const char *text,
			      size_t len);

	struct rcu_head rcu_head;
};

/* make a copy for RCUpdating.  oldzt may be NULL to allocate a fresh one. */
/*
 * zlog_target_clone - 为 RCU 更新克隆一份目标。oldzt 可为 NULL，表示分配一个全新的目标。
 * mt 为内存类型，size 为要分配的字节数。返回新目标指针。
 */
extern struct zlog_target *zlog_target_clone(struct memtype *mt,
					     struct zlog_target *oldzt,
					     size_t size);

/* update the zlog_targets list;  both oldzt and newzt may be NULL.  You
 * still need to zlog_target_free() the old target afterwards if it wasn't
 * NULL.
 *
 * Returns oldzt so you can zlog_target_free(zlog_target_replace(old, new));
 * (Some log targets may need extra cleanup inbetween, but remember the old
 * target MUST remain functional until the end of the current RCU cycle.)
 */
/*
 * zlog_target_replace - 原子地更新目标链，把 oldzt 替换成 newzt（两者均可为 NULL）。
 * 返回被替换下来的旧目标，你需要随后对它调用 zlog_target_free()。
 * 旧目标必须保持可用直到当前 RCU 周期结束，因此不要在替换后立刻释放其内部资源。
 */
extern struct zlog_target *zlog_target_replace(struct zlog_target *oldzt,
					       struct zlog_target *newzt);

/* Mostly for symmetry for zlog_target_clone(), just rcu_free() internally. */
/*
 * zlog_target_free - zlog_target_clone 的对称操作，内部就是一次 rcu_free。
 */
#define zlog_target_free(mt, zt) \
	rcu_free(mt, zt, rcu_head)

/*
 * zlog_init - 初始化日志核心。参数：progname 进程名，protoname 协议名，
 * instance 实例号，uid / gid 为运行身份。会设置全局前缀并触发 zlog_init 钩子。
 */
extern void zlog_init(const char *progname, const char *protoname,
		      unsigned short instance, uid_t uid, gid_t gid);
DECLARE_HOOK(zlog_init, (const char *progname, const char *protoname,
			 unsigned short instance, uid_t uid, gid_t gid),
			(progname, protoname, instance, uid, gid));

/*
 * zlog_fini - 关闭日志核心，释放资源。退出前调用；对应一个反向钩子 KOOH。
 */
extern void zlog_fini(void);
DECLARE_KOOH(zlog_fini, (), ());

/*
 * zlog_set_prefix_ec / zlog_get_prefix_ec - 控制日志前缀是否包含错误码。
 * zlog_set_prefix_xid / zlog_get_prefix_xid - 控制日志前缀是否包含事务 ID / xid。
 */
extern void zlog_set_prefix_ec(bool enable);
extern bool zlog_get_prefix_ec(void);
extern void zlog_set_prefix_xid(bool enable);
extern bool zlog_get_prefix_xid(void);

/* for tools & test programs, i.e. anything not a daemon.
 * (no cleanup needed at exit)
 */
/*
 * zlog_aux_init - 供工具与测试程序（非 daemon）使用的轻量初始化。
 * prefix 为前缀，prio_min 为最低优先级。无需在退出时清理。
 */
extern void zlog_aux_init(const char *prefix, int prio_min);
DECLARE_HOOK(zlog_aux_init, (const char *prefix, int prio_min),
			    (prefix, prio_min));

/*
 * zlog_startup_end - 标记启动阶段结束，此后可切换到正常（可能带缓冲的）输出模式。
 */
extern void zlog_startup_end(void);

/*
 * zlog_tls_buffer_init / zlog_tls_buffer_flush / zlog_tls_buffer_fini -
 * 每线程（TLS）日志缓冲的生命周期管理：初始化、冲刷到目标、反初始化。
 */
extern void zlog_tls_buffer_init(void);
extern void zlog_tls_buffer_flush(void);
extern void zlog_tls_buffer_fini(void);

/* Enable or disable 'immediate' output - default is to buffer messages. */
/*
 * zlog_set_immediate / zlog_get_immediate_mode - 是否启用“立即输出”。
 * 默认是缓冲消息，启用后每条日志即时写出，便于调试但性能较差。
 */
extern void zlog_set_immediate(bool set_p);
bool zlog_get_immediate_mode(void);

/*
 * zlog_priority_str - 把优先级数值翻成可读字符串（如 LOG_ERR 得到 "Error" 之类）。
 */
extern const char *zlog_priority_str(int priority);

/* Remove temp dirs at shutdown */
/*
 * zlog_tmpdir_fini - 关闭时清理临时目录。
 */
void zlog_tmpdir_fini(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_ZLOG_H */
