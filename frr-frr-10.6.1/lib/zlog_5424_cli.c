// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021  David Lamparter for NetDEF, Inc.
 */

/*
 * 文件导读（zlog_5424_cli.c）
 *
 * zlog_5424 目标的 VTY 配置命令实现，也就是用户在 CLI 下配置"扩展 syslog"
 * 目标（log extended-syslog NAME ...）的地方。它把用户输入解析成一个
 * struct zlog_cfg_5424_user 对象（内含 struct zlog_cfg_5424 配置），最终通过
 * zlog_5424_apply_meta / zlog_5424_apply_dst 生效。
 *
 * 结构：
 *  - 用一棵按名字索引的红黑树 targets 管理所有已定义的目标。
 *  - ENTER 节点 EXTLOG_NODE 下的子命令（destination file / fifo / unix / fd、
 *    facility、format 等）逐项修改当前目标配置。
 *  - "延迟应用"机制：配置块内的改动只置 reconf_dst / reconf_meta 标记，待退出
 *    该配置块（或读取文件结束）时统一 apply，避免半成品配置生效。
 *  - unix_special 处理特例：/dev/log 自动按 local 格式、journald 套接字自动按
 *    journald 格式。
 *
 * 命令对象由 clippy 生成的 zlog_5424_cli_clippy.c 提供解析支持。
 */
#include "zebra.h"
#include "zlog_5424.h"

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include "lib/command.h"
#include "lib/libfrr.h"
#include "lib/log_vty.h"

/* 定义本文件私有的内存类型：目标配置对象与其内部字符串数据的内存统计 */
DEFINE_MTYPE_STATIC(LOG, LOG_5424_CONFIG, "extended syslog config");
DEFINE_MTYPE_STATIC(LOG, LOG_5424_DATA, "extended syslog config items");

/* 红黑树的排序比较函数：按目标名字典序比较，供 targets 唯一树使用 */
static int target_cmp(const struct zlog_cfg_5424_user *a,
		      const struct zlog_cfg_5424_user *b)
{
	return strcmp(a->name, b->name);
}

/* 声明按唯一键（名字）组织的红黑树类型 targets，元素为 zlog_cfg_5424_user */
DECLARE_RBTREE_UNIQ(targets, struct zlog_cfg_5424_user, targets_item,
		    target_cmp);
/* 实例化 zlog_cfg_5424_user 的 QObject 类型 */
DEFINE_QOBJ_TYPE(zlog_cfg_5424_user);

/* 全局：所有已定义的 5424 目标树，以及它们共享的事件循环 master
 * （套接字重连等异步操作需要它） */
static struct targets_head targets = INIT_RBTREE_UNIQ(targets);
static struct event_loop *log_5424_master;

/* 前向声明：清空目标的"目的地"相关配置（定义在下方） */
static void clear_dst(struct zlog_cfg_5424_user *cfg);

/*
 * 一个"布尔型日志选项"的描述：name 是 CLI 关键字，offs 是它在
 * struct zlog_cfg_5424 中的字节偏移，dflt 是其默认值。
 * 用偏移表统一处理这些开关，避免为每个开关写一份重复代码。
 */
struct log_option {
	const char *name;
	ptrdiff_t offs;
	bool dflt;
};

/* clang-format off */
/* 布尔日志选项表：code-location、version、unique-id、error-category、
 * format-args；其中 unique-id 与 error-category 默认开启。以 name==NULL 结尾 */
static struct log_option log_opts[] = {
	{ "code-location",	offsetof(struct zlog_cfg_5424, kw_location) },
	{ "version",		offsetof(struct zlog_cfg_5424, kw_version) },
	{ "unique-id",		offsetof(struct zlog_cfg_5424, kw_uid), true },
	{ "error-category",	offsetof(struct zlog_cfg_5424, kw_ec), true },
	{ "format-args",	offsetof(struct zlog_cfg_5424, kw_args) },
	{},
};

/* 新目标默认值：时间戳标志（6 位子秒 + UTC）、facility 取 LOG_DAEMON、
 * 最低级别取 LOG_DEBUG */
#define DFLT_TS_FLAGS		(6 | ZLOG_TS_UTC)
#define DFLT_FACILITY		LOG_DAEMON
#define DFLT_PRIO_MIN		LOG_DEBUG
/* clang-format on */

/*
 * Unix 目的地的特殊情形标记：NONE 普通；SYSLOG 表示连的是 /dev/log 这类本地
 * syslogd 套接字（自动放宽为 local 格式）；JOURNALD 表示连的是 systemd
 * journald 套接字（自动切到 journald 格式）。
 */
enum unix_special {
	SPECIAL_NONE = 0,
	SPECIAL_SYSLOG,
	SPECIAL_JOURNALD,
};

/*
 * 分配并初始化一个新的 5424 目标：分配对象、拷贝名字、填入默认配置（master、
 * kw_location、facility、prio_min、ts_flags），清空目的地，按 log_opts 表设置
 * 各布尔默认值，初始化其互斥锁；最后注册 QObject 并挂进 targets 树。返回新
 * 对象。
 */
static struct zlog_cfg_5424_user *log_5424_alloc(const char *name)
{
	struct zlog_cfg_5424_user *cfg;

	cfg = XCALLOC(MTYPE_LOG_5424_CONFIG, sizeof(*cfg));
	cfg->name = XSTRDUP(MTYPE_LOG_5424_DATA, name);

	cfg->cfg.master = log_5424_master;
	cfg->cfg.kw_location = true;
	cfg->cfg.kw_version = false;
	cfg->cfg.facility = DFLT_FACILITY;
	cfg->cfg.prio_min = DFLT_PRIO_MIN;
	cfg->cfg.ts_flags = DFLT_TS_FLAGS;
	clear_dst(cfg);

	for (struct log_option *opt = log_opts; opt->name; opt++) {
		bool *ptr = (bool *)(((char *)&cfg->cfg) + opt->offs);
		*ptr = opt->dflt;
	}

	zlog_5424_init(&cfg->cfg);

	QOBJ_REG(cfg, zlog_cfg_5424_user);
	targets_add(&targets, cfg);
	return cfg;
}

/*
 * 释放一个 5424 目标：从 targets 树与 QObject 注销，结束其配置（keepopen 透传
 * 给 zlog_5424_fini），清空目的地，再释放各字符串与对象本身。
 */
static void log_5424_free(struct zlog_cfg_5424_user *cfg, bool keepopen)
{
	targets_del(&targets, cfg);
	QOBJ_UNREG(cfg);

	zlog_5424_fini(&cfg->cfg, keepopen);
	clear_dst(cfg);

	XFREE(MTYPE_LOG_5424_DATA, cfg->filename);
	XFREE(MTYPE_LOG_5424_DATA, cfg->name);
	XFREE(MTYPE_LOG_5424_CONFIG, cfg);
}

/*
 * 清空目标的"目的地"配置：释放 filename（并同步 cfg.filename 指针）、
 * file_user/file_group/envvar；把 fd/uid/gid 复位为 -1，权限复位为
 * LOGFILE_MASK & 0666，file_nocreate 置 false，dst 置 NONE。
 */
static void clear_dst(struct zlog_cfg_5424_user *cfg)
{
	XFREE(MTYPE_LOG_5424_DATA, cfg->filename);
	cfg->cfg.filename = cfg->filename;

	XFREE(MTYPE_LOG_5424_DATA, cfg->file_user);
	XFREE(MTYPE_LOG_5424_DATA, cfg->file_group);
	XFREE(MTYPE_LOG_5424_DATA, cfg->envvar);

	cfg->cfg.fd = -1;
	cfg->cfg.file_uid = -1;
	cfg->cfg.file_gid = -1;
	cfg->cfg.file_mode = LOGFILE_MASK & 0666;
	cfg->cfg.file_nocreate = false;
	cfg->cfg.dst = ZLOG_5424_DST_NONE;
}

/*
 * 标记"目的地需要重新应用"。首次标记且非读文件时会提示用户改动将在退出配置
 * 块时生效（避免连续刷屏）。返回 CMD_SUCCESS。
 */
static int reconf_dst(struct zlog_cfg_5424_user *cfg, struct vty *vty)
{
	if (!cfg->reconf_dst && !cfg->reconf_meta && vty->type != VTY_FILE)
		vty_out(vty,
			"%% Changes will be applied when exiting this config block\n");

	cfg->reconf_dst = true;
	return CMD_SUCCESS;
}

/* 标记"元数据需要重新应用"。提示逻辑同 reconf_dst。返回 CMD_SUCCESS。 */
static int reconf_meta(struct zlog_cfg_5424_user *cfg, struct vty *vty)
{
	if (!cfg->reconf_dst && !cfg->reconf_meta && vty->type != VTY_FILE)
		vty_out(vty,
			"%% Changes will be applied when exiting this config block\n");

	cfg->reconf_meta = true;
	return CMD_SUCCESS;
}

/*
 * 清除目的地（若本就没有目的地则直接成功）：先 clear_dst 再按 reconf_dst
 * 标记待应用，供 "no destination ..." 命令使用。
 */
static int reconf_clear_dst(struct zlog_cfg_5424_user *cfg, struct vty *vty)
{
	if (cfg->cfg.dst == ZLOG_5424_DST_NONE)
		return CMD_SUCCESS;

	clear_dst(cfg);
	return reconf_dst(cfg, vty);
}

/* 引入 clippy 为本文件命令生成的解析代码 */
#include "lib/zlog_5424_cli_clippy.c"

/*
 * CLI 命令 "log extended-syslog EXTLOGNAME"：进入某个具名 5424 目标的配置
 * 上下文。若同名目标已存在则复用，否则新建；随后把该对象压入 VTY 上下文栈
 * （节点 EXTLOG_NODE），后续子命令通过 VTY_DECLVAR_CONTEXT 取到它。
 */
DEFPY_NOSH(log_5424_target,
	   log_5424_target_cmd,
	   "log extended-syslog EXTLOGNAME",
	   "Logging control\n"
	   "Extended RFC5424 syslog (including file targets)\n"
	   "Name identifying this syslog target\n")
{
	struct zlog_cfg_5424_user *cfg, ref;

	ref.name = (char *)extlogname;
	cfg = targets_find(&targets, &ref);

	if (!cfg)
		cfg = log_5424_alloc(extlogname);

	VTY_PUSH_CONTEXT(EXTLOG_NODE, cfg);
	return CMD_SUCCESS;
}

/* CLI 命令 "no log extended-syslog EXTLOGNAME"：删除具名 5424 目标；找不到则
 * 告警。删除时 keepopen=false，即真正关闭目标。 */
DEFPY(no_log_5424_target,
      no_log_5424_target_cmd,
      "no log extended-syslog EXTLOGNAME",
      NO_STR
      "Logging control\n"
      "Extended RFC5424 syslog (including file targets)\n"
      "Name identifying this syslog target\n")
{
	struct zlog_cfg_5424_user *cfg, ref;

	ref.name = (char *)extlogname;
	cfg = targets_find(&targets, &ref);

	if (!cfg) {
		vty_out(vty, "%% No extended syslog target named \"%s\"\n",
			extlogname);
		return CMD_WARNING;
	}

	log_5424_free(cfg, false);
	return CMD_SUCCESS;
}

/*    "format <rfc3164|rfc5424|local-syslogd|journald>$fmt" */
/* format 子命令四个可选值的帮助文本（RFC3164 / RFC5424 / local-syslogd /
 * journald），被多条命令复用 */
#define FORMAT_HELP                                                            \
	"Select log message formatting\n"                                      \
	"RFC3164 (legacy) syslog\n"                                            \
	"RFC5424 (modern) syslog, supports structured data (default)\n"        \
	"modified RFC3164 without hostname for local syslogd (/dev/log)\n"     \
	"journald (systemd log) native format\n"                               \
	/* end */

/*
 * 把 format 关键字字符串转成枚举值：rfc5424 / rfc3164 / local-syslogd /
 * journald；fmt 为空或无法识别时返回默认值 dflt。
 */
static enum zlog_5424_format log_5424_fmt(const char *fmt,
					  enum zlog_5424_format dflt)
{
	if (!fmt)
		return dflt;
	else if (!strcmp(fmt, "rfc5424"))
		return ZLOG_FMT_5424;
	else if (!strcmp(fmt, "rfc3164"))
		return ZLOG_FMT_3164;
	else if (!strcmp(fmt, "local-syslogd"))
		return ZLOG_FMT_LOCAL;
	else if (!strcmp(fmt, "journald"))
		return ZLOG_FMT_JOURNALD;

	return dflt;
}

/*
 * CLI 命令 "[no] destination file PATH [create ... |no-create] [format ...]"：
 * 把日志写到普通文件。create 选项可指定创建时的属主 / 属组 / 权限，或
 * no-create 表示不创建；no 前缀表示清除该目的地。
 * 命令体校验权限八进制值等，并通过 VTY_DECLVAR_CONTEXT 直接操作当前目标对象。
 */
DEFPY(log_5424_destination_file,
      log_5424_destination_file_cmd,
      "[no] destination file$type PATH "
		"[create$create [{user WORD|group WORD|mode PERMS}]"
		"|no-create$nocreate] "
		"[format <rfc3164|rfc5424|local-syslogd|journald>$fmt]",
      NO_STR
      "Log destination setup\n"
      "Log to file\n"
      "Path to destination\n"
      "Create file if it does not exist\n"
      "Set file owner\n"
      "User name\n"
      "Set file group\n"
      "Group name\n"
      "Set permissions\n"
      "File permissions (octal)\n"
      "Do not create file if it does not exist\n"
      FORMAT_HELP)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	enum zlog_5424_dst dst;
	bool reconf = true, warn_perm = false;
	char *prev_user, *prev_group;
	mode_t perm_val = LOGFILE_MASK & 0666;
	enum zlog_5424_format fmtv;

	if (no)
		return reconf_clear_dst(cfg, vty);

	fmtv = log_5424_fmt(fmt, ZLOG_FMT_5424);

	if (perms) {
		char *errp = (char *)perms;

		perm_val = strtoul(perms, &errp, 8);
		if (*errp || errp == perms || perm_val == 0 ||
		    (perm_val & ~0666)) {
			vty_out(vty, "%% Invalid permissions value \"%s\"\n",
				perms);
			return CMD_WARNING;
		}
	}

	dst = (strcmp(type, "fifo") == 0) ? ZLOG_5424_DST_FIFO
					  : ZLOG_5424_DST_FILE;

	if (cfg->filename && !strcmp(path, cfg->filename) &&
	    dst == cfg->cfg.dst && cfg->cfg.active && cfg->cfg.fmt == fmtv)
		reconf = false;

	/* keep for compare below */
	prev_user = cfg->file_user;
	prev_group = cfg->file_group;
	cfg->file_user = NULL;
	cfg->file_group = NULL;

	clear_dst(cfg);

	cfg->filename = XSTRDUP(MTYPE_LOG_5424_DATA, path);
	cfg->cfg.dst = dst;
	cfg->cfg.filename = cfg->filename;
	cfg->cfg.fmt = fmtv;

	if (nocreate)
		cfg->cfg.file_nocreate = true;
	else {
		if (user) {
			struct passwd *pwent;

			warn_perm |= (prev_user && strcmp(user, prev_user));
			cfg->file_user = XSTRDUP(MTYPE_LOG_5424_DATA, user);

			errno = 0;
			pwent = getpwnam(user);
			if (!pwent)
				vty_out(vty,
					"%% Could not look up user \"%s\" (%s), file owner will be left untouched!\n",
					user,
					errno ? safe_strerror(errno)
					      : "No entry by this user name");
			else
				cfg->cfg.file_uid = pwent->pw_uid;
		}
		if (group) {
			struct group *grent;

			warn_perm |= (prev_group && strcmp(group, prev_group));
			cfg->file_group = XSTRDUP(MTYPE_LOG_5424_DATA, group);

			errno = 0;
			grent = getgrnam(group);
			if (!grent)
				vty_out(vty,
					"%% Could not look up group \"%s\" (%s), file group will be left untouched!\n",
					group,
					errno ? safe_strerror(errno)
					      : "No entry by this group name");
			else
				cfg->cfg.file_gid = grent->gr_gid;
		}
	}
	XFREE(MTYPE_LOG_5424_DATA, prev_user);
	XFREE(MTYPE_LOG_5424_DATA, prev_group);

	if (cfg->cfg.file_uid != (uid_t)-1 || cfg->cfg.file_gid != (gid_t)-1) {
		struct stat st;

		if (stat(cfg->filename, &st) == 0) {
			warn_perm |= (st.st_uid != cfg->cfg.file_uid);
			warn_perm |= (st.st_gid != cfg->cfg.file_gid);
		}
	}
	if (warn_perm)
		vty_out(vty,
			"%% Warning: ownership and permission bits are only applied when creating\n"
			"%%          log files.  Use system tools to change existing files.\n"
			"%%          FRR may also be missing necessary privileges to set these.\n");

	if (reconf)
		return reconf_dst(cfg, vty);

	return CMD_SUCCESS;
}

/* FIFOs are for legacy /dev/log implementations;  using this is very much not
 * recommended since it can unexpectedly block in logging calls.  Also the fd
 * would need to be reopened when the process at the other end restarts.  None
 * of this is handled - use at your own caution.  It's _HIDDEN for a purpose.
 */
/*
 * "destination fifo" 命令：与 destination file 共用同一处理函数，仅把类型换成
 * fifo。FIFO 面向老的 /dev/log 实现，不推荐使用（日志调用可能意外阻塞、对端
 * 重启后 fd 需重开等，均未处理），故用 _HIDDEN 隐藏（需输入完整关键字）。
 */
ALIAS_HIDDEN(log_5424_destination_file,
	     log_5424_destination_fifo_cmd,
      "[no] destination fifo$type PATH "
		"[create$create [{owner WORD|group WORD|permissions PERMS}]"
		"|no-create$nocreate] "
		"[format <rfc3164|rfc5424|local-syslogd|journald>$fmt]",
      NO_STR
      "Log destination setup\n"
      "Log to filesystem FIFO\n"
      "Path to destination\n"
      "Create file if it does not exist\n"
      "Set file owner\n"
      "User name\n"
      "Set file group\n"
      "Group name\n"
      "Set permissions\n"
      "File permissions (octal)\n"
      "Do not create file if it does not exist\n"
      FORMAT_HELP)

/*
 * 通用 Unix 套接字目的地设置：no 前缀清除目的地；否则记录 unix_special 标记、
 * 若配置未变则直接成功；否则清空并设置 filename / dst / fmt，并初始化套接字
 * 重连退避（初始 25ms，上限 10000ms），最后标记待应用。
 */
static int dst_unix(struct vty *vty, const char *no, const char *path,
		    enum zlog_5424_format fmt, enum unix_special special)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);

	if (no)
		return reconf_clear_dst(cfg, vty);

	cfg->unix_special = special;

	if (cfg->cfg.dst == ZLOG_5424_DST_UNIX && cfg->filename &&
	    !strcmp(path, cfg->filename) && cfg->cfg.active &&
	    cfg->cfg.fmt == fmt)
		return CMD_SUCCESS;

	clear_dst(cfg);

	cfg->filename = XSTRDUP(MTYPE_LOG_5424_DATA, path);
	cfg->cfg.dst = ZLOG_5424_DST_UNIX;
	cfg->cfg.filename = cfg->filename;
	cfg->cfg.fmt = fmt;

	cfg->cfg.reconn_backoff = 25;
	cfg->cfg.reconn_backoff_cur = 25;
	cfg->cfg.reconn_backoff_max = 10000;
	return reconf_dst(cfg, vty);
}

/* CLI 命令 "[no] destination unix PATH [format ...]"：把日志写到指定 Unix
 * 套接字路径。解析 format 后交给 dst_unix，special 为 SPECIAL_NONE。 */
DEFPY(log_5424_destination_unix,
      log_5424_destination_unix_cmd,
      "[no] destination unix PATH "
		 "[format <rfc3164|rfc5424|local-syslogd|journald>$fmt]",
      NO_STR
      "Log destination setup\n"
      "Log to unix socket\n"
      "Unix socket path\n"
      FORMAT_HELP)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	enum zlog_5424_format fmtv = log_5424_fmt(fmt, ZLOG_FMT_5424);

	return dst_unix(vty, no, path, fmtv, SPECIAL_NONE);
}

/* CLI 命令 "[no] destination journald"：直连 systemd journald 套接字
 * (/run/systemd/journal/socket)，固定 journald 格式，special 为 SPECIAL_JOURNALD。 */
DEFPY(log_5424_destination_journald,
      log_5424_destination_journald_cmd,
      "[no] destination journald",
      NO_STR
      "Log destination setup\n"
      "Log directly to systemd's journald\n")
{
	return dst_unix(vty, no, "/run/systemd/journal/socket",
			ZLOG_FMT_JOURNALD, SPECIAL_JOURNALD);
}

/* /dev/log 的默认格式按平台选择：较新的 FreeBSD / NetBSD 支持 RFC5424，其余
 * 平台退化为本地 syslogd 格式（LOCAL，即不带主机名的 RFC3164 变体）。 */
#if defined(__FreeBSD_version) && (__FreeBSD_version >= 1200061)
#define ZLOG_FMT_DEV_LOG	ZLOG_FMT_5424
#elif defined(__NetBSD_Version__) && (__NetBSD_Version__ >= 500000000)
#define ZLOG_FMT_DEV_LOG	ZLOG_FMT_5424
#else
#define ZLOG_FMT_DEV_LOG	ZLOG_FMT_LOCAL
#endif

/* CLI 命令 "[no] destination syslog [supports-rfc5424]"：直连 /dev/log。
 * 带 supports-rfc5424 时用 RFC5424，否则用平台默认格式；无法侦测对端是否真支持
 * 5424，故交给用户声明。special 为 SPECIAL_SYSLOG。 */
DEFPY(log_5424_destination_syslog,
      log_5424_destination_syslog_cmd,
      "[no] destination syslog [supports-rfc5424]$supp5424",
      NO_STR
      "Log destination setup\n"
      "Log directly to syslog\n"
      "Use RFC5424 format (please refer to documentation)\n")
{
	int format = supp5424 ? ZLOG_FMT_5424 : ZLOG_FMT_DEV_LOG;

	/* unfortunately, there is no way to detect 5424 support */
	return dst_unix(vty, no, "/dev/log", format, SPECIAL_SYSLOG);
}

/* could add something like
 *   "destination <udp|tcp>$proto <A.B.C.D|X:X::X:X> (1-65535)$port"
 * here, but there are 2 reasons not to do that:
 *
 *  - each FRR daemon would open its own connection, there's no system level
 *    aggregation.  That's the system's syslogd's job.  It likely also
 *    supports directing & filtering log messages with configurable rules.
 *  - we're likely not going to support DTLS or TLS for more secure logging;
 *    adding this would require a considerable amount of additional config
 *    and an entire TLS library to begin with.  A proper syslogd implements
 *    all of this, why reinvent the wheel?
 */

/*
 * CLI 命令 "[no] destination <fd N | envvar NAME | stdout | stderr>
 * [format ...]"：把日志写到一个"进程启动时就已打开"的 fd。
 * fd 可来自常量、$stdout/$stderr，或从环境变量读取；若环境变量缺失或非法会
 * 给出告警，若 fd 并非启动时打开的也会告警。配置照样保存但不一定立即生效。
 * 若配置未变则直接成功；否则清空并设置 dst=FD、fd、fmt（及 envvar）。
 */
DEFPY(log_5424_destination_fd,
      log_5424_destination_fd_cmd,
      "[no] destination <fd <(0-63)$fd|envvar WORD>|stdout$fd1|stderr$fd2>"
		 "[format <rfc3164|rfc5424|local-syslogd|journald>$fmt]",
      NO_STR
      "Log destination setup\n"
      "Log to pre-opened file descriptor\n"
      "File descriptor number (must be open at startup)\n"
      "Read file descriptor number from environment variable\n"
      "Environment variable name\n"
      "Log to standard output\n"
      "Log to standard error output\n"
      FORMAT_HELP)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	bool envvar_problem = false;
	enum zlog_5424_format fmtv;

	if (no)
		return reconf_clear_dst(cfg, vty);

	fmtv = log_5424_fmt(fmt, ZLOG_FMT_5424);

	if (envvar) {
		char *envval;

		envval = getenv(envvar);
		if (!envval)
			envvar_problem = true;
		else {
			char *errp = envval;

			fd = strtoul(envval, &errp, 0);
			if (errp == envval || *errp)
				envvar_problem = true;
		}

		if (envvar_problem)
			fd = -1;
	} else if (fd1)
		fd = 1;
	else if (fd2)
		fd = 2;

	if (cfg->cfg.dst == ZLOG_5424_DST_FD && cfg->cfg.fd == fd &&
	    cfg->cfg.active && cfg->cfg.fmt == fmtv)
		return CMD_SUCCESS;

	clear_dst(cfg);

	cfg->cfg.dst = ZLOG_5424_DST_FD;
	cfg->cfg.fd = fd;
	cfg->cfg.fmt = fmtv;
	if (envvar)
		cfg->envvar = XSTRDUP(MTYPE_LOG_5424_DATA, envvar);

	if (envvar_problem)
		vty_out(vty,
			"%% environment variable \"%s\" not present or invalid.\n",
			envvar);
	if (!frr_is_startup_fd(fd))
		vty_out(vty,
			"%% file descriptor %d was not open when this process was started\n",
			(int)fd);
	if (envvar_problem || !frr_is_startup_fd(fd))
		vty_out(vty,
			"%% configuration will be saved but has no effect currently\n");

	return reconf_dst(cfg, vty);
}

/* CLI 命令 "[no] destination [none]"：清除当前目标的目的地配置（等价于不再
 * 输出到任何目的地）。 */
DEFPY(log_5424_destination_none,
      log_5424_destination_none_cmd,
      "[no] destination [none]",
      NO_STR
      "Log destination setup\n"
      "Deconfigure destination\n")
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);

	return reconf_clear_dst(cfg, vty);
}

/* end of destinations */

/* CLI 命令 "[no] priority <级别>"：设置该目标的最低记录级别。级别名用
 * log_level_match 解析；与当前值相同则不动，否则更新并标记元数据待应用。 */
DEFPY(log_5424_prio,
      log_5424_prio_cmd,
      "[no] priority <emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg",
      NO_STR
      "Set minimum message priority to include for this target\n"
      LOG_LEVEL_DESC)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	int prio_min = log_level_match(levelarg);

	if (prio_min == cfg->cfg.prio_min)
		return CMD_SUCCESS;

	cfg->cfg.prio_min = prio_min;
	return reconf_meta(cfg, vty);
}

/* CLI 命令 "[no] facility <facility>"：设置该目标使用的 syslog facility。
 * 用 facility_match 解析；相同则不动，否则更新并标记元数据待应用。 */
DEFPY(log_5424_facility,
      log_5424_facility_cmd,
      "[no] facility <kern|user|mail|daemon|auth|syslog|lpr|news|uucp|cron|local0|local1|local2|local3|local4|local5|local6|local7>$facilityarg",
      NO_STR
      "Set syslog facility to use\n"
      LOG_FACILITY_DESC)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	int facility = facility_match(facilityarg);

	if (cfg->cfg.facility == facility)
		return CMD_SUCCESS;

	cfg->cfg.facility = facility;
	return reconf_meta(cfg, vty);
}

/* CLI 命令 "[no] structured-data <选项>"：开关某个结构化数据项
 * （code-location / version / unique-id / error-category / format-args）。
 * 先在 log_opts 表里找到对应项，再用其偏移取得 bool 指针；no 表示关闭。
 * 值未变则成功返回，否则修改并标记元数据待应用。 */
DEFPY(log_5424_meta,
      log_5424_meta_cmd,
      "[no] structured-data <code-location|version|unique-id|error-category|format-args>$option",
      NO_STR
      "Select structured data (key/value pairs) to include in each message\n"
      "FRR source code location\n"
      "FRR version\n"
      "Unique message identifier (XXXXX-XXXXX)\n"
      "Error category (EC numeric)\n"
      "Individual formatted log message arguments\n")
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	bool val = !no, *ptr;
	struct log_option *opt = log_opts;

	while (opt->name && strcmp(opt->name, option))
		opt++;
	if (!opt->name)
		return CMD_WARNING;

	ptr = (bool *)(((char *)&cfg->cfg) + opt->offs);
	if (*ptr == val)
		return CMD_SUCCESS;

	*ptr = val;
	return reconf_meta(cfg, vty);
}

/* CLI 命令 "[no] timestamp precision (0-9)"：设置时间戳的小数秒位数。
 * 先清掉 ZLOG_TS_PREC 位，no 时恢复默认位数，否则取用户给的值；变化才标记
 * 元数据待应用。 */
DEFPY(log_5424_ts_prec,
      log_5424_ts_prec_cmd,
      "[no] timestamp precision (0-9)",
      NO_STR
      "Timestamp options\n"
      "Number of sub-second digits to include\n"
      "Number of sub-second digits to include\n")
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	uint32_t ts_flags = cfg->cfg.ts_flags;

	ts_flags &= ~ZLOG_TS_PREC;
	if (no)
		ts_flags |= DFLT_TS_FLAGS & ZLOG_TS_PREC;
	else
		ts_flags |= precision;

	if (ts_flags == cfg->cfg.ts_flags)
		return CMD_SUCCESS;

	cfg->cfg.ts_flags = ts_flags;
	return reconf_meta(cfg, vty);
}

/* CLI 命令 "[no] timestamp local-time"：选择用本地时区还是 UTC（默认 UTC）。
 * 先清掉 ZLOG_TS_UTC 位，no 表示恢复默认（UTC），否则按默认取反设置 UTC 位；
 * 变化才标记元数据待应用。 */
DEFPY(log_5424_ts_local,
      log_5424_ts_local_cmd,
      "[no] timestamp local-time",
      NO_STR
      "Timestamp options\n"
      "Use local system time zone rather than UTC\n")
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);
	uint32_t ts_flags = cfg->cfg.ts_flags;

	ts_flags &= ~ZLOG_TS_UTC;
	if (no)
		ts_flags |= DFLT_TS_FLAGS & ZLOG_TS_UTC;
	else
		ts_flags |= (~DFLT_TS_FLAGS) & ZLOG_TS_UTC;

	if (ts_flags == cfg->cfg.ts_flags)
		return CMD_SUCCESS;

	cfg->cfg.ts_flags = ts_flags;
	return reconf_meta(cfg, vty);
}

/*
 * 退出 EXTLOG_NODE 配置块的回调：若期间有改动，提示 "applying changes"，并按
 * 标记优先级应用——有目的地变更就 apply_dst（会重建目标），否则仅 apply_meta
 * （只改元数据，不重开目标），最后清空两个标记。返回 1 表示已处理。
 */
static int log_5424_node_exit(struct vty *vty)
{
	VTY_DECLVAR_CONTEXT(zlog_cfg_5424_user, cfg);

	if ((cfg->reconf_dst || cfg->reconf_meta) && vty->type != VTY_FILE)
		vty_out(vty, "%% applying changes.\n");

	if (cfg->reconf_dst)
		zlog_5424_apply_dst(&cfg->cfg);
	else if (cfg->reconf_meta)
		zlog_5424_apply_meta(&cfg->cfg);

	cfg->reconf_dst = cfg->reconf_meta = false;
	return 1;
}

/*
 * 把某个 5424 目标回写成 CLI 配置（供 show running-config）。按 fmt 生成
 * format 关键字，按 dst 生成 destination 行（含 fd / file / fifo / unix /
 * syslog / journald 各分支）；再按需输出 priority、facility、各结构化数据
 * 开关、时间戳精度 / 本地时间等非默认项，最后以 "!" 结束。
 */
static int log_5424_config_write(struct vty *vty)
{
	struct zlog_cfg_5424_user *cfg;

	frr_each (targets, &targets, cfg) {
		const char *fmt_str = "";

		vty_out(vty, "log extended %s\n", cfg->name);

		(void)fmt_str; /* clang-SA */
		switch (cfg->cfg.fmt) {
		case ZLOG_FMT_5424:
			fmt_str = " format rfc5424";
			break;
		case ZLOG_FMT_3164:
			fmt_str = " format rfc3164";
			break;
		case ZLOG_FMT_LOCAL:
			fmt_str = " format local-syslogd";
			break;
		case ZLOG_FMT_JOURNALD:
			fmt_str = " format journald";
			break;
		}

		switch (cfg->cfg.dst) {
		case ZLOG_5424_DST_NONE:
			vty_out(vty, " ! no destination configured\n");
			break;

		case ZLOG_5424_DST_FD:
			if (cfg->cfg.fmt == ZLOG_FMT_5424)
				fmt_str = "";

			if (cfg->envvar)
				vty_out(vty, " destination fd envvar %s%s\n",
					cfg->envvar, fmt_str);
			else if (cfg->cfg.fd == 1)
				vty_out(vty, " destination stdout%s\n",
					fmt_str);
			else if (cfg->cfg.fd == 2)
				vty_out(vty, " destination stderr%s\n",
					fmt_str);
			else
				vty_out(vty, " destination fd %d%s\n",
					cfg->cfg.fd, fmt_str);
			break;

		case ZLOG_5424_DST_FILE:
		case ZLOG_5424_DST_FIFO:
			if (cfg->cfg.fmt == ZLOG_FMT_5424)
				fmt_str = "";

			vty_out(vty, " destination %s %s",
				(cfg->cfg.dst == ZLOG_5424_DST_FIFO) ? "fifo"
								     : "file",
				cfg->filename);

			if (cfg->cfg.file_nocreate)
				vty_out(vty, " no-create");
			else if (cfg->file_user || cfg->file_group ||
				 cfg->cfg.file_mode != (LOGFILE_MASK & 0666)) {
				vty_out(vty, " create");

				if (cfg->file_user)
					vty_out(vty, " user %s",
						cfg->file_user);
				if (cfg->file_group)
					vty_out(vty, " group %s",
						cfg->file_group);
				if (cfg->cfg.file_mode != (LOGFILE_MASK & 0666))
					vty_out(vty, " mode %04o",
						cfg->cfg.file_mode);
			}
			vty_out(vty, "%s\n", fmt_str);
			break;

		case ZLOG_5424_DST_UNIX:
			switch (cfg->unix_special) {
			case SPECIAL_NONE:
				vty_out(vty, " destination unix %s%s\n",
					cfg->filename, fmt_str);
				break;
			case SPECIAL_SYSLOG:
				if (cfg->cfg.fmt == ZLOG_FMT_DEV_LOG)
					vty_out(vty, " destination syslog\n");
				else
					vty_out(vty,
						" destination syslog supports-rfc5424\n");
				break;
			case SPECIAL_JOURNALD:
				vty_out(vty, " destination journald\n");
				break;
			}
			break;
		}

		if (cfg->cfg.prio_min != LOG_DEBUG)
			vty_out(vty, " priority %s\n",
				zlog_priority_str(cfg->cfg.prio_min));
		if (cfg->cfg.facility != DFLT_FACILITY)
			vty_out(vty, " facility %s\n",
				facility_name(cfg->cfg.facility));

		for (struct log_option *opt = log_opts; opt->name; opt++) {
			bool *ptr = (bool *)(((char *)&cfg->cfg) + opt->offs);

			if (*ptr != opt->dflt)
				vty_out(vty, " %sstructured-data %s\n",
					*ptr ? "" : "no ", opt->name);
		}

		if ((cfg->cfg.ts_flags ^ DFLT_TS_FLAGS) & ZLOG_TS_PREC)
			vty_out(vty, " timestamp precision %u\n",
				cfg->cfg.ts_flags & ZLOG_TS_PREC);

		if ((cfg->cfg.ts_flags ^ DFLT_TS_FLAGS) & ZLOG_TS_UTC) {
			if (cfg->cfg.ts_flags & ZLOG_TS_UTC)
				vty_out(vty, " no timestamp local-time\n");
			else
				vty_out(vty, " timestamp local-time\n");
		}

		vty_out(vty, "!\n");
	}
	return 0;
}

/*
 * 展示某个 5424 目标的运行状态（供 show 命令）：输出目标名、目的地（不同 dst
 * 分支给出不同描述）、日志级别与 facility 等。
 */
static int log_5424_show(struct vty *vty)
{
	struct zlog_cfg_5424_user *cfg;

	frr_each (targets, &targets, cfg) {
		vty_out(vty, "\nExtended log target %pSQq\n", cfg->name);

		switch (cfg->cfg.dst) {
		case ZLOG_5424_DST_NONE:
			vty_out(vty,
				"  Inactive (no destination configured)\n");
			break;

		case ZLOG_5424_DST_FD:
			if (cfg->envvar)
				vty_out(vty,
					"  logging to fd %d from environment variable %pSE\n",
					cfg->cfg.fd, cfg->envvar);
			else if (cfg->cfg.fd == 1)
				vty_out(vty, "  logging to stdout\n");
			else if (cfg->cfg.fd == 2)
				vty_out(vty, "  logging to stderr\n");
			else
				vty_out(vty, "  logging to fd %d\n",
					cfg->cfg.fd);
			break;

		case ZLOG_5424_DST_FILE:
		case ZLOG_5424_DST_FIFO:
		case ZLOG_5424_DST_UNIX:
			vty_out(vty, "  logging to %s: %pSE\n",
				(cfg->cfg.dst == ZLOG_5424_DST_FIFO) ? "fifo"
				: (cfg->cfg.dst == ZLOG_5424_DST_UNIX)
					? "unix socket"
					: "file",
				cfg->filename);
			break;
		}

		vty_out(vty, "  log level: %s, facility: %s\n",
			zlog_priority_str(cfg->cfg.prio_min),
			facility_name(cfg->cfg.facility));

		bool any_meta = false, first = true;

		for (struct log_option *opt = log_opts; opt->name; opt++) {
			bool *ptr = (bool *)(((char *)&cfg->cfg) + opt->offs);

			any_meta |= *ptr;
		}

		if (!any_meta)
			continue;

		switch (cfg->cfg.fmt) {
		case ZLOG_FMT_5424:
		case ZLOG_FMT_JOURNALD:
			vty_out(vty, "  structured data: ");

			for (struct log_option *opt = log_opts; opt->name;
			     opt++) {
				bool *ptr = (bool *)(((char *)&cfg->cfg) +
						     opt->offs);

				if (*ptr) {
					vty_out(vty, "%s%s", first ? "" : ", ",
						opt->name);
					first = false;
				}
			}
			break;

		case ZLOG_FMT_3164:
		case ZLOG_FMT_LOCAL:
			vty_out(vty,
				"  structured data is not supported by the selected format\n");
			break;
		}

		vty_out(vty, "\n");

		size_t lost_msgs;
		int last_errno;
		bool stale_errno;
		struct timeval err_ts;
		int64_t since;

		zlog_5424_state(&cfg->cfg, &lost_msgs, &last_errno,
				&stale_errno, &err_ts);
		vty_out(vty, "  number of lost messages: %zu\n", lost_msgs);

		if (last_errno == 0)
			since = 0;
		else
			since = monotime_since(&err_ts, NULL);
		vty_out(vty,
			"  last error: %s (%lld.%06llds ago, currently %s)\n",
			last_errno ? safe_strerror(last_errno) : "none",
			since / 1000000LL, since % 1000000LL,
			stale_errno ? "OK" : "erroring");
	}
	return 0;
}

/* EXTLOG_NODE 的命令节点定义：提示符、父节点 CONFIG_NODE，并挂上配置回写
 * （log_5424_config_write）与退出回调（log_5424_node_exit）。 */
static struct cmd_node extlog_node = {
	.name = "extended",
	.node = EXTLOG_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-ext-log)# ",

	.config_write = log_5424_config_write,
	.node_exit = log_5424_node_exit,
};

/* 命令补全：把 targets 里所有目标名加入补全候选（用于 EXTLOGNAME） */
static void log_5424_autocomplete(vector comps, struct cmd_token *token)
{
	struct zlog_cfg_5424_user *cfg;

	frr_each (targets, &targets, cfg)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, cfg->name));
}

/* 命令变量处理器表：把 EXTLOGNAME 这个 token 的补全接到 log_5424_autocomplete，
 * 以 NULL 结尾表示结束。 */
static const struct cmd_variable_handler log_5424_var_handlers[] = {
	{.tokenname = "EXTLOGNAME", .completions = log_5424_autocomplete},
	{.completions = NULL},
};

/*
 * 注册所有 5424 相关命令与钩子：
 *  - 把 log_5424_show 挂到 zlog_cli_show 钩子，使 show logging 能列出目标；
 *  - 注册 EXTLOGNAME 的补全处理器；
 *  - 安装 extlog_node 节点及其默认命令，并把顶层 log / no log 命令装到
 *    CONFIG_NODE、各子命令装到 EXTLOG_NODE。
 */
void log_5424_cmd_init(void)
{
	hook_register(zlog_cli_show, log_5424_show);

	cmd_variable_handler_register(log_5424_var_handlers);

	/* CLI commands. */
	install_node(&extlog_node);
	install_default(EXTLOG_NODE);

	install_element(CONFIG_NODE, &log_5424_target_cmd);
	install_element(CONFIG_NODE, &no_log_5424_target_cmd);

	install_element(EXTLOG_NODE, &log_5424_destination_file_cmd);
	install_element(EXTLOG_NODE, &log_5424_destination_fifo_cmd);
	install_element(EXTLOG_NODE, &log_5424_destination_unix_cmd);
	install_element(EXTLOG_NODE, &log_5424_destination_journald_cmd);
	install_element(EXTLOG_NODE, &log_5424_destination_syslog_cmd);
	install_element(EXTLOG_NODE, &log_5424_destination_fd_cmd);

	install_element(EXTLOG_NODE, &log_5424_meta_cmd);
	install_element(EXTLOG_NODE, &log_5424_prio_cmd);
	install_element(EXTLOG_NODE, &log_5424_facility_cmd);
	install_element(EXTLOG_NODE, &log_5424_ts_prec_cmd);
	install_element(EXTLOG_NODE, &log_5424_ts_local_cmd);
}

/* hooks */

/* 前向声明：早期初始化、轮转（钩子）与收尾（钩子）函数（定义在下方） */
static int log_5424_early_init(struct event_loop *master);
static int log_5424_rotate(void);
static int log_5424_fini(void);

/*
 * 构造函数（libfrr 加载优先级 475）：把早期初始化挂到 frr_early_init、把
 * log_5424_rotate 挂到 zlog_rotate 钩子、把 log_5424_fini 挂到 frr_fini，从而
 * 让 5424 目标随守护进程生命周期自动初始化 / 轮转 / 收尾。
 */
__attribute__((_CONSTRUCTOR(475))) static void zlog_5424_startup_init(void)
{
	hook_register(frr_early_init, log_5424_early_init);
	hook_register(zlog_rotate, log_5424_rotate);
	hook_register(frr_fini, log_5424_fini);
}

/* frr_early_init 钩子：记录事件循环 master，供后续套接字重连事件使用 */
static int log_5424_early_init(struct event_loop *master)
{
	log_5424_master = master;

	return 0;
}

/* zlog_rotate 钩子：对每个 5424 目标调用 zlog_5424_rotate 做 SIGHUP 轮转，
 * 失败则打印错误 */
static int log_5424_rotate(void)
{
	struct zlog_cfg_5424_user *cfg;

	frr_each (targets, &targets, cfg)
		if (!zlog_5424_rotate(&cfg->cfg))
			zlog_err(
				"log rotation on extended log target %s failed",
				cfg->name);

	return 0;
}

/* frr_fini 钩子：进程退出时逐个弹出并释放所有 5424 目标，keepopen=true
 * （关机时只摘配置、日志继续），最后清空 master 指针 */
static int log_5424_fini(void)
{
	struct zlog_cfg_5424_user *cfg;

	while ((cfg = targets_pop(&targets)))
		log_5424_free(cfg, true);

	log_5424_master = NULL;

	return 0;
}
