// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zebra logging funcions.
 * Copyright (C) 1997, 1998, 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：log.h —— FRR 传统日志入口的公共头文件
 *
 * 本文件是 FRR “老日志接口” 的对外声明集合。历史上 FRR 的日志系统以 zlog 命名，
 * 后来被重写为新的 zlog.c / zlog.h 核心（基于消息对象与目标链）。为兼容大量既有
 * 调用点，老的一套宏与函数入口保留在此，它们大多只是新核心的一层薄封装。
 *
 * 主要内容：
 *   1. 优先级语义说明：LOG_DEBUG 到 LOG_ERR 各自适用的场景，以及为何废弃更重的级别。
 *   2. 老的过滤器接口：zlog_filter_add / zlog_filter_del / zlog_filter_dump /
 *      zlog_filter_clear，按正则过滤日志行，上限 100 条、单条 80 字符。
 *   3. 崩溃与信号场景辅助：zlog_signal、zlog_backtrace、zlog_backtrace_sigsafe、
 *      zlog_hexdump、zlog_sanitize，供 daemon 崩溃处理器安全地写日志。
 *   4. 时间戳辅助：frr_timestamp 与 struct timestamp_control，避免同一秒内重复
 *      调用 localtime 的开销。
 *   5. 路由类型 / zserv 命令 / 协议名与编号的互转表（zebra_route_string 等）。
 *   6. 协议模块自用的日志宏，例如 OSPF_LOG 系列。
 *   7. 与 northbound（YANG 配置）对接的声明 frr_logging_nb_info。
 *
 * 阅读建议：先看本文件了解有哪些遗留入口，再到 zlog.h 看新的消息对象与目标模型，
 * 最后回到 log.c / zlog.c / zlog_targets.c 看一次调用如何落实为最终输出。
 */
#ifndef _ZEBRA_LOG_H
#define _ZEBRA_LOG_H

#include <syslog.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include "lib/hook.h"
#include "lib/zlog.h"
#include "lib/zlog_targets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Here is some guidance on logging levels to use:
 *
 * LOG_DEBUG	- For all messages that are enabled by optional debugging
 *		  features, typically preceded by "if (IS...DEBUG...)"
 * LOG_INFO	- Information that may be of interest, but everything seems
 *		  to be working properly.
 * LOG_NOTICE	- Only for message pertaining to daemon startup or shutdown.
 * LOG_WARNING	- Warning conditions: unexpected events, but the daemon believes
 *		  it can continue to operate correctly.
 * LOG_ERR	- Error situations indicating malfunctions.  Probably require
 *		  attention.
 *
 * Note: LOG_CRIT, LOG_ALERT, and LOG_EMERG are currently not used anywhere,
 * please use LOG_ERR instead.
 */

/*
 * zlog_rotate - 触发一次日志文件轮转（重新打开或搬移旧文件）。
 * 通常由外部信号（例如收到 USR1）或显式调用触发，让已打开的文件目标切换文件。
 * 副作用：会操作日志文件句柄，非严格异步信号安全。
 */
extern void zlog_rotate(void);

/*
 * struct message - “键到文本”的静态映射表条目。
 * key 是枚举值（如路由类型、zserv 命令号），str 是其可读字符串。
 * 常与 lookup_msg() 配合，在 switch 表里把数字翻译成名字。
 */
/* Message structure. */
struct message {
	int key;
	const char *str;
};

/*
 * zlog_thread_info - 以给定优先级打印当前线程 / 进程信息（pid、tid 等）。
 * 参数 log_level 决定这些信息以什么优先级写入日志；无返回值。
 */
extern void zlog_thread_info(int log_level);

/*
 * ZLOG_FILTERS_MAX - 同时生效的日志过滤器条数上限（100）。
 * ZLOG_FILTER_LENGTH_MAX - 单条过滤器的长度上限（80 字符）。
 * 过滤器按正则匹配整行日志文本，命中即被丢弃，用于抑制噪声。
 */
#define ZLOG_FILTERS_MAX 100      /* Max # of filters at once */
#define ZLOG_FILTER_LENGTH_MAX 80 /* 80 character filter limit */

/*
 * struct zlog_cfg_filterfile - 带过滤器能力的文件日志配置。
 * 内嵌 struct zlog_cfg_file parent，继承其全部成员（文件路径、fd、优先级阈值等）；
 * 目前不额外增加字段，仅作语义区分与未来扩展点。
 */
struct zlog_cfg_filterfile {
	struct zlog_cfg_file parent;
};

/*
 * zlog_filterfile_init - 初始化一个带过滤器的文件日志配置对象 zcf。
 * zlog_filterfile_fini - 反初始化并释放其占用的资源。
 */
extern void zlog_filterfile_init(struct zlog_cfg_filterfile *zcf);
extern void zlog_filterfile_fini(struct zlog_cfg_filterfile *zcf);

/* Add/Del/Dump log filters */
/*
 * zlog_filter_clear / zlog_filter_add / zlog_filter_del - 过滤器集合的增删清空。
 * zlog_filter_add 返回 0 表示成功，非 0 表示失败（如超限或正则非法）。
 * zlog_filter_dump - 把当前所有过滤器文本写入 buf（容量 max_size），返回写入长度。
 */
extern void zlog_filter_clear(void);
extern int zlog_filter_add(const char *filter);
extern int zlog_filter_del(const char *filter);
extern int zlog_filter_dump(char *buf, size_t max_size);

/*
 * lookup_msg - 在 message 表 mz 中查找键为 kz 的条目对应的字符串。
 * 查不到时返回 nf（通常是 "unknown" 之类的兜底文本）；返回指针仅供只读使用。
 */
const char *lookup_msg(const struct message *mz, int kz, const char *nf);

/*
 * safe_strerror - strerror() 的安全版本，保证绝不返回 NULL。
 * 当 errnum 非法或 strerror 失败时返回占位字符串，避免调用者空指针解引用。
 */
/* Safe version of strerror -- never returns NULL. */
extern const char *safe_strerror(int errnum);

/* To be called when a fatal signal is caught. */
/*
 * zlog_signal - 捕获到致命信号时调用，记录信号名、动作描述 action 与 siginfo。
 * program_counter 给出出错指令地址。常用于 daemon 的崩溃处理器。
 */
extern void zlog_signal(int signo, const char *action, void *siginfo,
			void *program_counter);

/*
 * zlog_backtrace - 以指定优先级 priority 打印当前调用栈回溯。
 * 允许在此处分配内存或加锁，因此不能用于信号上下文（信号里请用 sigsafe 版本）。
 */
/* Log a backtrace. */
extern void zlog_backtrace(int priority);

/* Log a backtrace, but in an async-signal-safe way.  Should not be
   called unless the program is about to exit or abort, since it messes
   up the state of zlog file pointers.  If program_counter is non-NULL,
   that is logged in addition to the current backtrace. */
/*
 * zlog_backtrace_sigsafe - 异步信号安全版的回溯打印。
 * 只应在程序即将退出或 abort 时调用，因为它会破坏 zlog 的文件指针状态。
 * program_counter 非 NULL 时会连同当前回溯一起记录；priority 为日志优先级。
 */
extern void zlog_backtrace_sigsafe(int priority, void *program_counter);

/* Puts a current timestamp in buf and returns the number of characters
   written (not including the terminating NUL).  The purpose of
   this function is to avoid calls to localtime appearing all over the code.
   It caches the most recent localtime result and can therefore
   avoid multiple calls within the same second.  If buflen is too small,
   *buf will be set to '\0', and 0 will be returned. */
/*
 * FRR_TIMESTAMP_LEN - frr_timestamp 输出缓冲区所需推荐长度（含结尾空字符）。
 * frr_timestamp 把当前时间戳写入 buf 并返回写入字符数（不含结尾空字符）。
 * timestamp_precision 指定亚秒位数；它内部缓存最近一次 localtime 结果，
 * 避免同一秒内反复调用 localtime。若 buflen 太小，buf 首字节置 0 且返回 0。
 */
#define FRR_TIMESTAMP_LEN 40
extern size_t frr_timestamp(int timestamp_precision /* # subsecond digits */,
			    char *buf, size_t buflen);

/*
 * zlog_hexdump - 以十六进制加可打印字符的形式，转储 mem 处 len 字节的内存。
 * zlog_sanitize - 把可能含敏感或不可打印内容的 in 清洗后写入 buf，返回 buf。
 *   参数 inlen 为输入长度，bufsz 为输出缓冲区大小。
 */
extern void zlog_hexdump(const void *mem, size_t len);
extern const char *zlog_sanitize(char *buf, size_t bufsz, const void *in,
				 size_t inlen);

/* Note: whenever a new route-type or zserv-command is added the
 * corresponding {command,route}_types[] table in lib/log.c MUST be
 * updated! */

/* Map a route type to a string.  For example, ZEBRA_ROUTE_RIPNG -> "ripng". */
/*
 * zebra_route_string - 把路由类型编号翻成小写字符串，例如 ZEBRA_ROUTE_RIPNG 得到 "ripng"。
 * 注意：每当新增路由类型或 zserv 命令时，必须同步更新 log.c 里的
 * route_types / command_types 表，否则这里会取到未知兜底值。
 */
extern const char *zebra_route_string(unsigned int route_type);
/* Map a route type to a char.  For example, ZEBRA_ROUTE_RIPNG -> 'R'. */
extern char zebra_route_char(unsigned int route_type);
/* Map a zserv command type to the same string,
 * e.g. ZEBRA_INTERFACE_ADD -> "ZEBRA_INTERFACE_ADD" */
/* Map a protocol name to its number. e.g. ZEBRA_ROUTE_BGP->9*/
/*
 * proto_name2num - 把协议名（如 "bgp"）翻译为协议编号，例如 ZEBRA_ROUTE_BGP 对应 9。
 * 只接受规范名称，不做缩写展开；返回编号，非法输入返回无效值。
 * 与下方的 proto_redistnum 相对照。
 */
extern int proto_name2num(const char *s);
/* Map redistribute X argument to protocol number.
 * unlike proto_name2num, this accepts shorthands and takes
 * an AFI value to restrict input */
/*
 * proto_redistnum - 把 redistribute 命令里的协议名翻成协议编号。
 * 与 proto_name2num 不同，它接受缩写，并用 afi 参数限定取值范围（例如仅 IPv4 协议）。
 * afi 为地址族，s 为协议名；返回编号，非法输入返回无效值。
 */
extern uint8_t proto_redistnum(int afi, const char *s);

/*
 * zserv_command_string - 把 zserv 命令编号翻成名字，如 ZEBRA_INTERFACE_ADD 得到 "ZEBRA_INTERFACE_ADD"。
 * zserv_gr_client_cap_string - 把 GR（Graceful Restart）客户端能力位 zcc 翻成可读字符串。
 */
extern const char *zserv_command_string(unsigned int command);
extern const char *zserv_gr_client_cap_string(unsigned int zcc);

/*
 * OSPF_LOG 系列宏 - OSPF 模块自用的日志封装。
 * OSPF_LOG(level, cond, fmt, ...) 仅在 cond 为真时调用对应的 zlog_<level>()。
 * 下方的 OSPF_LOG_ERR / _WARN / _INFO / _DEBUG / _NOTICE 是固定优先级与条件的便捷包装，
 * 其中 _DEBUG 由调用方传入 cond 以做调试开关判断。
 */
#define OSPF_LOG(level, cond, fmt, ...)                                        \
	do {                                                                   \
		if (cond)                                                      \
			zlog_##level(fmt, ##__VA_ARGS__);                      \
	} while (0)

#define OSPF_LOG_ERR(fmt, ...) OSPF_LOG(err, true, fmt, ##__VA_ARGS__)

#define OSPF_LOG_WARN(fmt, ...) OSPF_LOG(warn, true, fmt, ##__VA_ARGS__)

#define OSPF_LOG_INFO(fmt, ...) OSPF_LOG(info, true, fmt, ##__VA_ARGS__)

#define OSPF_LOG_DEBUG(cond, fmt, ...) OSPF_LOG(debug, cond, fmt, ##__VA_ARGS__)

#define OSPF_LOG_NOTICE(fmt, ...) OSPF_LOG(notice, true, fmt, ##__VA_ARGS__)

/* structure useful for avoiding repeated rendering of the same timestamp */
/*
 * struct timestamp_control - 用于避免重复渲染同一时间戳的缓存结构。
 * len 为已渲染时间戳的长度；precision 为配置的亚秒精度；
 * already_rendered 初值必须为 0，表示是否已渲染过；buf 存放渲染结果。
 */
struct timestamp_control {
	size_t len;			/* length of rendered timestamp */
	int precision;			/* configuration parameter */
	int already_rendered;		/* should be initialized to 0 */
	char buf[FRR_TIMESTAMP_LEN];	/* will contain the rendered timestamp
					 */
};

/* Defines for use in command construction: */

/*
 * LOG_LEVEL_DESC - 供命令行 / VTY 帮助文本使用的日志级别描述。
 * 按 syslog 级别由重到轻排列（LOG_EMERG 到 LOG_DEBUG，共 8 行），逐行给出说明文字。
 */
#define LOG_LEVEL_DESC                                                         \
	"System is unusable\n"                                                 \
	"Immediate action needed\n"                                            \
	"Critical conditions\n"                                                \
	"Error conditions\n"                                                   \
	"Warning conditions\n"                                                 \
	"Normal but significant conditions\n"                                  \
	"Informational messages\n"                                             \
	"Debugging messages\n"

/*
 * LOG_FACILITY_DESC - facility 描述文本，顺序与 syslog 的 facility 编号一致
 * （kernel、user、mail、daemon、auth、syslog、lpr、news、uucp、cron 及 local0..7）。
 */
#define LOG_FACILITY_DESC                                                      \
	"Kernel\n"                                                             \
	"User process\n"                                                       \
	"Mail system\n"                                                        \
	"System daemons\n"                                                     \
	"Authorization system\n"                                               \
	"Syslog itself\n"                                                      \
	"Line printer system\n"                                                \
	"USENET news\n"                                                        \
	"Unix-to-Unix copy system\n"                                           \
	"Cron/at facility\n"                                                   \
	"Local use\n"                                                          \
	"Local use\n"                                                          \
	"Local use\n"                                                          \
	"Local use\n"                                                          \
	"Local use\n"                                                          \
	"Local use\n"                                                          \
	"Local use\n"                                                          \
	"Local use\n"


/*
 * frr_logging_nb_info - 日志相关的 northbound（YANG）模块信息，供 mgmtd 与各 daemon 注册。
 * frr_logging_merge_cli_to_nb_info - 把 CLI 侧的日志配置合并进 NB 信息。
 */
extern struct frr_yang_module_info frr_logging_nb_info;
extern void frr_logging_merge_cli_to_nb_info(void);

/*
 * frr_time_t_to_uint32_t - 把 time_t 安全地截断 / 转换为 uint32_t，供 YANG 时间字段使用。
 */
extern uint32_t frr_time_t_to_uint32_t(time_t value);
#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_LOG_H */
