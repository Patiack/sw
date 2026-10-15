// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Logging - VTY code
 * Copyright (C) 2019 Cumulus Networks, Inc.
 *                    Stephen Worley
 */

/*
 * 文件导读（log_vty.c）
 *
 * 实现 log_vty.h 声明的日志 CLI 公共设施，并集中定义各日志目标对象、日志级别
 * 表与 facility 表。可把它理解成"日志子系统的全局状态与控制中枢"：
 *
 *  - 全局目标对象：zt_file（普通日志文件）、zt_filterfile（带过滤的日志文件）、
 *    zt_file_cmdline（命令行指定的日志文件）、zt_stdout_file（stdout 文件目标）、
 *    zt_stdout_journald（把 stdout 接到 systemd journald 的目标）。
 *  - 级别与 facility 名称表：zlog_priority、syslog_severity、syslog_facilities，
 *    以及由此派生的匹配 / 反查函数。
 *  - 钩子：zlog_rotate（SIGHUP 轮转）与 zlog_cli_show（追加配置到 show）。
 *  - 命令：send_log（在 CLI 上直接发一条日志）等。
 *
 * 一条日志的产生路径（概览）：应用调用 zlog_* 宏 -> lib/log.c 的 zlog 核心
 * 统一格式化（时间戳、级别、xref 等）-> 依次分发给所有已注册的 zlog_target
 * （文件、syslog、stdout、journald、实时流……）。本文件负责其中相当一部分
 * 目标的"配置存放与 CLI 呈现"，真正写目标由 zlog_targets / zlog_5424 完成。
 */
#include <zebra.h>

#include "lib/log_vty.h"
#include "command.h"
#include "lib/log.h"
#include "lib/zlog_targets.h"
#include "lib/zlog_5424.h"
#include "lib/lib_errors.h"
#include "lib/northbound_cli.h"
#include "lib/printfrr.h"
#include "lib/systemd.h"
#include "lib/vtysh_daemons.h"

#include "lib/log_vty_clippy.c"

#define ZLOG_MAXLVL(a, b) MAX(a, b)

/* 实例化 zlog_rotate 与 zlog_cli_show 两个钩子（声明见 log_vty.h）：
 * 前者供 SIGHUP 轮转时被触发，后者供各目标追加 show config。 */
DEFINE_HOOK(zlog_rotate, (), ());
DEFINE_HOOK(zlog_cli_show, (struct vty * vty), (vty));

/* 全局日志状态：logmsgs_with_persist_bt 为带持久化 backtrace 的消息计数；
 * 四个 *_lvl 分别记录配置文件 / 命令行下的 stdout 与 syslog 级别，初值
 * ZLOG_DISABLED 表示未启用。 */
uint logmsgs_with_persist_bt;

int log_config_stdout_lvl = ZLOG_DISABLED;
int log_config_syslog_lvl = ZLOG_DISABLED;
int log_cmdline_stdout_lvl = ZLOG_DISABLED;
int log_cmdline_syslog_lvl = ZLOG_DISABLED;

/* 各日志目标对象的定义（此处给出初始值；真正生效需经 apply 打开）。
 *  - zt_file_cmdline / zt_file：普通日志文件目标，默认级别 ZLOG_DISABLED，
 *    时间戳精度取 LOG_TIMESTAMP_PRECISION。
 *  - zt_filterfile：带过滤前缀（parent）的日志文件目标。
 *  - zt_stdout_file：把 stdout 当作文件的目标。
 *  - zt_stdout_journald：把 stdout 接到 systemd journald 套接字
 *    (/run/systemd/journal/socket)，格式为 journald；其 facility 与各关键字
 *    开关不可经配置修改，因为它替代的是"普通" stdout 目标。 */
struct zlog_cfg_file zt_file_cmdline = {
	.prio_min = ZLOG_DISABLED,
	.ts_subsec = LOG_TIMESTAMP_PRECISION,
};
struct zlog_cfg_file zt_file = {
	.prio_min = ZLOG_DISABLED,
	.ts_subsec = LOG_TIMESTAMP_PRECISION,
};
struct zlog_cfg_filterfile zt_filterfile = {
	.parent =
		{
			.prio_min = ZLOG_DISABLED,
			.ts_subsec = LOG_TIMESTAMP_PRECISION,
		},
};

struct zlog_cfg_file zt_stdout_file = {
	.prio_min = ZLOG_DISABLED,
	.ts_subsec = LOG_TIMESTAMP_PRECISION,
};
struct zlog_cfg_5424 zt_stdout_journald = {
	.prio_min = ZLOG_DISABLED,

	.fmt = ZLOG_FMT_JOURNALD,
	.dst = ZLOG_5424_DST_UNIX,
	.filename = "/run/systemd/journal/socket",

	/* this can't be changed through config since this target substitutes
	 * in for the "plain" stdout target
	 */
	.facility = LOG_DAEMON,
	.kw_version = false,
	.kw_location = true,
	.kw_uid = true,
	.kw_ec = true,
	.kw_args = true,
};
/* 标记 stdout 是否正由 journald 目标接管（接管后普通 stdout 目标应让位） */
bool stdout_journald_in_use;

/* 当前进程的日志程序名（syslog 的 ident）及其可读的协议名，用于输出标识 */
const char *zlog_progname;
static const char *zlog_protoname;

/*
 * syslog facility 名字与数值的映射表项，兼作"匹配长度"表：
 *  - facility：syslog facility 常量值。
 *  - name：其名字（如 daemon、local0）。
 *  - match：facility_match 前缀匹配时使用的最短匹配字符数，用来区分前缀
 *    相近的名字（如 auth 与 authpriv）。
 * 表以 name==NULL 结尾。
 */
static const struct facility_map {
	int facility;
	const char *name;
	size_t match;
} syslog_facilities[] = {
	{LOG_KERN, "kern", 1},
	{LOG_USER, "user", 2},
	{LOG_MAIL, "mail", 1},
	{LOG_DAEMON, "daemon", 1},
	{LOG_AUTH, "auth", 1},
	{LOG_SYSLOG, "syslog", 1},
	{LOG_LPR, "lpr", 2},
	{LOG_NEWS, "news", 1},
	{LOG_UUCP, "uucp", 2},
	{LOG_CRON, "cron", 1},
#ifdef LOG_FTP
	{LOG_FTP, "ftp", 1},
#endif
	{LOG_LOCAL0, "local0", 6},
	{LOG_LOCAL1, "local1", 6},
	{LOG_LOCAL2, "local2", 6},
	{LOG_LOCAL3, "local3", 6},
	{LOG_LOCAL4, "local4", 6},
	{LOG_LOCAL5, "local5", 6},
	{LOG_LOCAL6, "local6", 6},
	{LOG_LOCAL7, "local7", 6},
	{0, NULL, 0},
};

/* 日志级别（0=最重要）到名字的映射；下标即级别值，以 NULL 结尾。
 * 索引越大优先级越低（debugging 位于最高索引） */
const char *const zlog_priority[] = {
	"emergencies",	 "alerts",	  "critical",  "errors", "warnings",
	"notifications", "informational", "debugging", NULL,
};

/* 日志级别到 syslog 严重性（severity）名字的映射，供 log_lev2sev 使用 */
const char *const syslog_severity[] = {
	"emergency", "alert", "critical", "error", "warning", "notice", "info", "debug", NULL,
};

/*
 * 把日志级别数值转成名字。超出 LOG_DEBUG 返回 "invalid"，负值返回
 * "disabled"，否则查 zlog_priority 表。
 */
const char *zlog_priority_str(int priority)
{
	if (priority > LOG_DEBUG)
		return "invalid";
	else if (priority < 0)
		return "disabled";
	return zlog_priority[priority];
}

/*
 * 由 facility 数值反查名字：遍历 syslog_facilities，找到相同值即返回其名字；
 * 找不到返回空字符串。
 */
const char *facility_name(int facility)
{
	const struct facility_map *fm;

	for (fm = syslog_facilities; fm->name; fm++)
		if (fm->facility == facility)
			return fm->name;
	return "";
}

/*
 * 由 facility 名字匹配数值：用表中每项自身的 match 长度做前缀匹配，命中即
 * 返回其 facility 值；全部不中返回 -1。
 */
int facility_match(const char *str)
{
	const struct facility_map *fm;

	for (fm = syslog_facilities; fm->name; fm++)
		if (!strncmp(str, fm->name, fm->match))
			return fm->facility;
	return -1;
}

/*
 * 由级别名字前两个字符匹配出级别值（取前 2 字符是为了兼容缩写，如
 * "de" -> debugging）；找不到返回 ZLOG_DISABLED。
 */
int log_level_match(const char *s)
{
	int level;

	for (level = 0; zlog_priority[level] != NULL; level++)
		if (!strncmp(s, zlog_priority[level], 2))
			return level;
	return ZLOG_DISABLED;
}

/*
 * 把内部级别名先转成级别值，再映射到 syslog 严重性名；名字无法识别时回退为
 * "emergency"。
 */
const char *log_lev2sev(const char *level_name)
{
	int level = log_level_match(level_name);

	if (level == ZLOG_DISABLED)
		return "emergency";

	return syslog_severity[level];
}

/*
 * SIGHUP 轮转入口：先轮转内置的文件类目标（zt_file、zt_filterfile.parent、
 * zt_file_cmdline），再触发 zlog_rotate 钩子，通知其它目标（如 5424 目标）
 * 各自轮转。
 */
void zlog_rotate(void)
{
	zlog_file_rotate(&zt_file);
	zlog_file_rotate(&zt_filterfile.parent);
	zlog_file_rotate(&zt_file_cmdline);
	hook_call(zlog_rotate);
}


/*
 * 打印当前 syslog 日志配置到 vty：禁用时输出 disabled，否则输出级别、
 * facility 与程序名（ident）。
 */
void log_show_syslog(struct vty *vty)
{
	int level = zlog_syslog_get_prio_min();

	vty_out(vty, "Syslog logging: ");
	if (level == ZLOG_DISABLED)
		vty_out(vty, "disabled\n");
	else
		vty_out(vty, "level %s, facility %s, ident %s\n",
			zlog_priority[level],
			facility_name(zlog_syslog_get_facility()),
			zlog_progname);
}

/*
 * 清除命令行带来的日志目标：把 zt_file_cmdline 关掉、命令行的 syslog 级别
 * 复位，令 syslog 级别回到配置文件与命令行中较宽松（取 MAX）的那个；stdout
 * 同理复位并重新应用。通常在读取配置文件后调用，让配置文件接管。
 */
void clear_cmdline_targets(void)
{
	zt_file_cmdline.prio_min = ZLOG_DISABLED;
	zlog_file_set_other(&zt_file_cmdline);

	log_cmdline_syslog_lvl = ZLOG_DISABLED;
	zlog_syslog_set_prio_min(ZLOG_MAXLVL(log_config_syslog_lvl, log_cmdline_syslog_lvl));

	log_cmdline_stdout_lvl = ZLOG_DISABLED;
	log_stdout_apply_level();
}

/*
 * CLI 命令 "send log [level ...] LINE..."：在命令行上直接发一条日志。
 * level 参数省略时用默认级别 log_default_lvl；给出时用 log_level_match 解析
 * （前两字符匹配，故优先级 / 严重性两种写法皆可）。
 * 实现要点：为拿到 LINE 的原文，需要在 vty->buf 里跳过前面的命令关键字与
 * 空白，定位到 LINE 对应的位置后整体发出。
 */
DEFPY(send_log,
      send_log_cmd,
      "send log [level <emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg] LINE...",
      "Send message\n"
      "Send log message\n"
      "Level of log message\n"
      LOG_LEVEL_DESC
      "Log message to send\n")
{
	const char *buf = vty->buf;
	int level = log_default_lvl;

	if (levelarg) {
		/* this checks using first 2 chars so works for both priority and severity */
		level = log_level_match(levelarg);
		assert(level != ZLOG_DISABLED);
	}

	/* find start of LINE in `buf` */
	while (*buf && isspace(*buf))
		buf++;
	for (; *buf && (*argv)->type != VARIABLE_TKN; argv++) {
		while (*buf && !isspace(*buf))
			buf++;
		while (*buf && isspace(*buf))
			buf++;
	}

	zlog(level, "%s", buf);
	return CMD_SUCCESS;
}

/*
 * CLI 命令 "show logging"：汇总展示当前日志配置。
 * 输出 syslog 配置、stdout 级别（journald 接管时取 zt_stdout_journald，否则
 * 取 zt_stdout_file）、文件级别、过滤文件级别、来自命令行的配置项、协议名、
 * 是否记录 priority / severity、时间戳精度等。
 * 最后触发 zlog_cli_show 钩子，让各日志目标把自己的配置也追加进来。
 */
DEFUN_NOSH (show_logging,
	    show_logging_cmd,
	    "show logging",
	    SHOW_STR
	    "Show current logging configuration\n")
{
	int stdout_prio;

	log_show_syslog(vty);

	stdout_prio = stdout_journald_in_use ? zt_stdout_journald.prio_min
					     : zt_stdout_file.prio_min;

	vty_out(vty, "Stdout logging: ");
	if (stdout_prio == ZLOG_DISABLED)
		vty_out(vty, "disabled");
	else
		vty_out(vty, "level %s", zlog_priority[stdout_prio]);
	vty_out(vty, "\n");

	vty_out(vty, "File logging: ");
	if (zt_file.prio_min == ZLOG_DISABLED || !zt_file.filename)
		vty_out(vty, "disabled");
	else
		vty_out(vty, "level %s, filename %s",
			zlog_priority[zt_file.prio_min], zt_file.filename);
	vty_out(vty, "\n");

	if (zt_filterfile.parent.prio_min != ZLOG_DISABLED
	    && zt_filterfile.parent.filename)
		vty_out(vty, "Filtered-file logging: level %s, filename %s\n",
			zlog_priority[zt_filterfile.parent.prio_min],
			zt_filterfile.parent.filename);

	if (log_cmdline_syslog_lvl != ZLOG_DISABLED)
		vty_out(vty,
			"From command line: \"--log syslog --log-level %s\"\n",
			zlog_priority[log_cmdline_syslog_lvl]);
	if (log_cmdline_stdout_lvl != ZLOG_DISABLED)
		vty_out(vty,
			"From command line: \"--log stdout --log-level %s\"\n",
			zlog_priority[log_cmdline_stdout_lvl]);
	if (zt_file_cmdline.prio_min != ZLOG_DISABLED)
		vty_out(vty,
			"From command line: \"--log file:%s --log-level %s\"\n",
			zt_file_cmdline.filename,
			zlog_priority[zt_file_cmdline.prio_min]);

	vty_out(vty, "Protocol name: %s\n", zlog_protoname);
	vty_out(vty, "Record priority: %s\n",
		(zt_file.record_priority ? "enabled" : "disabled"));
	vty_out(vty, "Record severity: %s\n", (zt_file.record_severity ? "enabled" : "disabled"));
	vty_out(vty, "Timestamp precision: %d\n", zt_file.ts_subsec);

	hook_call(zlog_cli_show, vty);
	return CMD_SUCCESS;
}

/*
 * CLI 命令 "[no] debug unique-id UID backtrace"：对某条日志（按唯一 ID）
 * 打开 / 关闭"加打印调用栈(backtrace)"。
 * 在 config 节点下设置的标志是持久的(LOGMSG_FLAG_PERSISTENT)，否则是临时的
 * (LOGMSG_FLAG_EPHEMERAL)；no 前缀表示关闭。若目标 ID 不是日志消息则报错；
 * 若标志本来就是想要的状态则直接成功（幂等）。
 */
DEFPY_NOSH (debug_uid_backtrace,
	    debug_uid_backtrace_cmd,
	    "[no] debug unique-id UID backtrace",
	    NO_STR
	    DEBUG_STR
	    "Options per individual log message, by unique ID\n"
	    "Log message unique ID (XXXXX-XXXXX)\n"
	    "Add backtrace to log when message is printed\n")
{
	struct xrefdata search, *xrd;
	struct xrefdata_logmsg *xrdl;
	uint8_t flag;

	strlcpy(search.uid, uid, sizeof(search.uid));
	xrd = xrefdata_uid_find(&xrefdata_uid, &search);

	if (!xrd)
		return CMD_ERR_NOTHING_TODO;

	if (xrd->xref->type != XREFT_LOGMSG) {
		vty_out(vty, "%% ID \"%s\" is not a log message\n", uid);
		return CMD_WARNING;
	}
	xrdl = container_of(xrd, struct xrefdata_logmsg, xrefdata);

	flag = (vty->node == CONFIG_NODE) ? LOGMSG_FLAG_PERSISTENT
					  : LOGMSG_FLAG_EPHEMERAL;

	if ((xrdl->fl_print_bt & flag) == (no ? 0 : flag))
		return CMD_SUCCESS;
	if (flag == LOGMSG_FLAG_PERSISTENT)
		logmsgs_with_persist_bt += no ? -1 : 1;

	xrdl->fl_print_bt ^= flag;
	return CMD_SUCCESS;
}

/* Show log filter */
/*
 * CLI 命令 "show logging filter-text"：展示当前配置的日志文本过滤器
 * （把过滤表 dump 到缓冲区后输出）。
 */
DEFPY (show_log_filter,
       show_log_filter_cmd,
       "show logging filter-text",
       SHOW_STR
       "Show current logging configuration\n"
       FILTER_LOG_STR)
{
	char log_filters[ZLOG_FILTERS_MAX * (ZLOG_FILTER_LENGTH_MAX + 3)] = "";
	int len = 0;

	len = zlog_filter_dump(log_filters, sizeof(log_filters));

	if (len == -1) {
		vty_out(vty, "%% failed to get filters\n");
		return CMD_WARNING;
	}

	if (len != 0)
		vty_out(vty, "%s", log_filters);

	return CMD_SUCCESS;
}

/*
 * 日志子系统收尾钩子：若命令行 / 普通日志文件目标已打开则关闭之。
 * 通过 hook_register(zlog_fini, ...) 在 log_vty_init 中注册。
 */
static int log_vty_fini(void)
{
	if (zt_file_cmdline.filename)
		zlog_file_fini(&zt_file_cmdline);
	if (zt_file.filename)
		zlog_file_fini(&zt_file);
	return 0;
}


/*
 * 日志子系统初始化钩子（进程启动早期经 zlog_init 钩子调用）：
 * 记录程序名 / 协议名、注册 fini 钩子、开启前缀中的错误分类(EC)与唯一 ID、
 * 初始化过滤文件目标；并根据 stdout 是否为 journald 决定是把 stdout 设为
 * journald 目标(5424)还是普通 stdout 文件目标。
 * 参数 progname/protoname 为进程名与协议名，uid/gid 为运行身份。
 */
static int log_vty_init(const char *progname, const char *protoname,
			 unsigned short instance, uid_t uid, gid_t gid)
{
	zlog_progname = progname;
	zlog_protoname = protoname;

	hook_register(zlog_fini, log_vty_fini);

	zlog_set_prefix_ec(true);
	zlog_set_prefix_xid(true);

	zlog_filterfile_init(&zt_filterfile);

	if (sd_stdout_is_journal) {
		stdout_journald_in_use = true;
		zlog_5424_init(&zt_stdout_journald);
		zlog_5424_apply_dst(&zt_stdout_journald);
	} else
		zlog_file_set_fd(&zt_stdout_file, STDOUT_FILENO);
	return 0;
}

/*
 * 构造函数：在 libfrr 加载（优先级 475）时把 log_vty_init 挂到 zlog_init
 * 钩子上，保证各守护进程启动时自动完成日志初始化。
 */
__attribute__((_CONSTRUCTOR(475))) static void log_vty_preinit(void)
{
	hook_register(zlog_init, log_vty_init);
}

/*
 * 注册本文件定义的日志 CLI 命令（send log、show logging、
 * show logging filter-text、debug unique-id ... backtrace），
 * 并调用 log_5424_cmd_init 注册 5424 目标的配置命令。
 */
void log_cmd_init(void)
{
	install_element(VIEW_NODE, &send_log_cmd);
	install_element(VIEW_NODE, &show_logging_cmd);
	install_element(VIEW_NODE, &show_log_filter_cmd);
	install_element(ENABLE_NODE, &debug_uid_backtrace_cmd);

	log_5424_cmd_init();
}
