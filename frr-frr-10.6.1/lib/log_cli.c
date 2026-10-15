// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 8 2025, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2025, LabN Consulting, L.L.C.
 */
// ============================================================
// 【文件导读】log_cli.c
// ------------------------------------------------------------
// 职责：
//   实现 vty 上的 "log ..." 系列配置命令与 "clear log ..." 命令。
//   本文件只负责"命令 -> northbound(数据模型)"这一层：命令处理函数
//   把用户输入翻译成对 frr-logging YANG 模块(/frr-logging:logging/...)
//   的增删改，再调用 nb_cli_apply_changes() 一次性提交；真正让日志
//   底层生效的活儿由 northbound 回调(在其它文件)完成。
//
//   同时提供一组 cli_show 回调，把数据模型渲染回 running-config 文本，
//   并注册进 YANG 模块信息中(供 show running-config 使用)。
//
// 关键宏/概念：
//   - DEFPY_YANG   / DEFUN_YANG      : 生成"走 northbound"的命令处理体；
//   - DEFPY_YANG_NOSH               : 同上但不提供 show 挂钩；
//   - DEFPY_HIDDEN                  : 隐藏命令(不在帮助里出现)；
//   - log_lev2sev()                 : 把等级关键字(如 debugging)转成
//                                     严重级字符串喂给 YANG 模型；
//   - log_level_match()             : 反向，字符串 -> 内部等级枚举；
//   - nb_cli_enqueue_change()/nb_cli_apply_changes(): 入队改动并提交。
//
// 与其他模块的关系：
//   - log_filter.c: 提供 zlog_filter_add/del/clear，被本文件的
//     `log filter-text` / `clear log filter-text` 间接驱动；
//   - log_vty.c / log.h: 日志核心与等级/严重级映射；
//   - northbound*.h: 事务式数据模型；
//   - log_cli_clippy.c: clippy 生成的解析辅助代码。
//
// 阅读顺序：
//   各 DEFPY/DEFUN 命令(配置项一个个看) -> log_cli_init()
//   (看它们装到哪些节点) -> logging_*_cli_write()(看如何回显)
//   -> frr_logging_cli_info -> frr_logging_merge_cli_to_nb_info()。
//
// 易踩的坑：
//   - 命令体只改 northbound 数据，不会直接调 zlog；
//   - show 回调对"等于默认值"的项通常省略输出(除非 show_defaults)；
//   - frr_logging_cli_info 里被注释掉的 filter-text 表示暂不保存到
//     配置(仅运行时有效)。
// ============================================================

#include <zebra.h>
// pwd.h / grp.h：getpwnam/getgrnam 等(日志文件属主/属组相关)。
#include <pwd.h>
#include <grp.h>
// command.h：命令定义宏(DEFPY 等)与 install_element。
// log.h：日志核心接口与等级枚举。
// log_vty.h：log_lev2sev / log_level_match 等 vty 侧辅助。
// northbound.h / northbound_cli.h：YANG 数据模型与 CLI 事务接口。
// lib/vtysh_daemons.h：DAEMONS_LIST / DAEMONS_STR(daemon 名列表)。
#include "command.h"
#include "log.h"
#include "log_vty.h"
#include "northbound.h"
#include "northbound_cli.h"
#include "lib/vtysh_daemons.h"

// clippy 工具为本文件里的 DEFPY 命令生成的解析辅助代码(直接包含)。
#include "lib/log_cli_clippy.c"

/* ======================= */
/* Basic logging CLI code. */
/* ======================= */

// config_log_stdout: 命令 "[no] log stdout [LEVEL]"。
//   开启/关闭"标准输出"日志目标，并可选设置等级。改动经 northbound
//   落到 /frr-logging:logging/stdout(及其 /level)。带 levelarg 时用
//   log_lev2sev 转成严重级串写入，否则 DESTROY level(回到默认)。
DEFPY_YANG (config_log_stdout,
	    config_log_stdout_cmd,
	    "[no] log stdout [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg]",
	    NO_STR
	    "Logging control\n"
	    "Set stdout logging level\n"
	    LOG_LEVEL_DESC)
{
	if (no)
		nb_cli_enqueue_change(vty, "/frr-logging:logging/stdout", NB_OP_DESTROY, NULL);
	else {
		nb_cli_enqueue_change(vty, "/frr-logging:logging/stdout", NB_OP_CREATE, NULL);
		if (levelarg)
			nb_cli_enqueue_change(vty, "/frr-logging:logging/stdout/level",
					      NB_OP_MODIFY, log_lev2sev(levelarg));
		else
			nb_cli_enqueue_change(vty, "/frr-logging:logging/stdout/level",
					      NB_OP_DESTROY, NULL);
	}
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_monitor: 命令 "[no] log monitor ..."，已废弃。
//   DEFPY_HIDDEN 表示它不出现在命令帮助中；函数体只打印一条提示、
//   什么都不做(兼容老配置，避免报"未知命令")。
DEFPY_HIDDEN (config_log_monitor,
       config_log_monitor_cmd,
       "[no] log monitor [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>]",
       NO_STR
       "Logging control\n"
       "Set terminal line (monitor) logging level\n"
       LOG_LEVEL_DESC)
{
	vty_out(vty, "%% \"log monitor\" is deprecated and does nothing.\n");
	return CMD_SUCCESS;
}

// debug_uid_backtrace: 命令 "[no] debug unique-id UID backtrace"。
//   为某个"日志消息唯一 ID(XXXXX-XXXXX)"开启/关闭打印调用栈。
//   xpath 用带谓词的写法 .../uid-backtrace[uid='...']；NOSH 表示
//   不需要自动生成 show 回调。
DEFPY_YANG_NOSH (debug_uid_backtrace,
            debug_uid_backtrace_cmd,
            "[no] debug unique-id UID backtrace",
            NO_STR
            DEBUG_STR
            "Options per individual log message, by unique ID\n"
            "Log message unique ID (XXXXX-XXXXX)\n"
            "Add backtrace to log when message is printed\n")
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath), "/frr-logging:logging/uid-backtrace[uid='%s']", uid);
	nb_cli_enqueue_change(vty, xpath, no ? NB_OP_DESTROY : NB_OP_CREATE, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* Per-daemon log file config */
// config_log_dmn_file: 命令 "log daemon DAEMON file FILENAME [LEVEL]"。
//   为"某个具体 daemon"指定日志文件(及其等级)。xpath 用
//   .../daemon-file[daemon='...']。argv[2] 是 daemon 名，
//   argv[4] 是文件名，argv[5] 是可选等级。
DEFUN_YANG (config_log_dmn_file,
	    config_log_dmn_file_cmd,
            "log daemon " DAEMONS_LIST " file FILENAME [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg]",
	    "Logging control\n"
	    "Specific daemon\n"
	    DAEMONS_STR
	    "Logging to file\n"
	    "Logging filename\n"
	    LOG_LEVEL_DESC)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath), "/frr-logging:logging/daemon-file[daemon='%s']/filename",
		 argv[2]->text);
	nb_cli_enqueue_change(vty, xpath, NB_OP_MODIFY, argv[4]->arg);
	snprintf(xpath, sizeof(xpath), "/frr-logging:logging/daemon-file[daemon='%s']/level",
		 argv[2]->text);
	if (argc < 6)
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	else
		nb_cli_enqueue_change(vty, xpath, NB_OP_MODIFY, log_lev2sev(argv[5]->text));
	return nb_cli_apply_changes(vty, NULL);
}

/* Per-daemon no log file */
// no_config_log_dmn_file: 命令 "no log daemon DAEMON file [...]"。
//   删除某 daemon 的文件日志配置(整块 daemon-file 列表项 DESTROY)。
DEFUN_YANG (no_config_log_dmn_file,
	    no_config_log_dmn_file_cmd,
	    "no log daemon " DAEMONS_LIST " file [FILENAME [LEVEL]]",
	    NO_STR
	    "Logging control\n"
	    "Specific daemon\n"
	    DAEMONS_STR
	    "Cancel logging to file\n"
	    "Logging file name\n"
	    "Logging level\n")
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath), "/frr-logging:logging/daemon-file[daemon='%s']",
		 argv[3]->text);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_file: 命令 "[no] log file [FILENAME [LEVEL]]"。
//   配置本 daemon 的普通日志文件(可选等级)。注意命令串里的 "!" 表示
//   FILENAME 必填位。改动落到 /frr-logging:logging/file[/filename|level]。
DEFPY_YANG (config_log_file,
	    config_log_file_cmd,
	    "[no] log file ![FILENAME [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg]]",
	    NO_STR
	    "Logging control\n"
	    "Logging to file\n"
	    "Logging filename\n"
	    LOG_LEVEL_DESC)
{
	if (no)
		nb_cli_enqueue_change(vty, "/frr-logging:logging/file", NB_OP_DESTROY, NULL);
	else {
		nb_cli_enqueue_change(vty, "/frr-logging:logging/file/filename", NB_OP_MODIFY,
				      filename);
		if (levelarg)
			nb_cli_enqueue_change(vty, "/frr-logging:logging/file/level", NB_OP_MODIFY,
					      log_lev2sev(levelarg));
		else
			nb_cli_enqueue_change(vty, "/frr-logging:logging/file/level",
					      NB_OP_DESTROY, NULL);
	}
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_syslog: 命令 "log syslog [LEVEL]"。
//   开启 syslog 日志目标并可选设等级；落到 /frr-logging:logging/syslog。
DEFPY_YANG (config_log_syslog,
	    config_log_syslog_cmd,
       "log syslog [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg]",
       "Logging control\n"
       "Set syslog logging level\n"
       LOG_LEVEL_DESC)
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/syslog", NB_OP_CREATE, NULL);
	if (levelarg)
		nb_cli_enqueue_change(vty, "/frr-logging:logging/syslog/level", NB_OP_MODIFY,
				      log_lev2sev(levelarg));
	else
		nb_cli_enqueue_change(vty, "/frr-logging:logging/syslog/level", NB_OP_DESTROY,
				      NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// no_config_log_syslog: 命令 "no log syslog [LEVEL]"。
//   关闭 syslog 日志目标(DESTROY .../syslog 整节点)。
DEFPY_YANG (no_config_log_syslog,
       no_config_log_syslog_cmd,
       "no log syslog [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>]",
       NO_STR
       "Logging control\n"
       "Cancel logging to syslog\n"
       LOG_LEVEL_DESC)
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/syslog", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_facility: 命令 "log facility FACILITY"。
//   设置 syslog facility。facilityarg 是身份名(如 local0)，写入前拼成
//   "ietf-syslog-types:xxx" 形式的 YANG identity。
DEFPY_YANG (config_log_facility,
       config_log_facility_cmd,
       "log facility <kern|user|mail|daemon|auth|syslog|lpr|news|uucp|cron|local0|local1|local2|local3|local4|local5|local6|local7>$facilityarg",
       "Logging control\n"
       "Facility parameter for syslog messages\n"
       LOG_FACILITY_DESC)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "ietf-syslog-types:%s", facilityarg);
	nb_cli_enqueue_change(vty, "/frr-logging:logging/facility", NB_OP_MODIFY, buf);
	return nb_cli_apply_changes(vty, NULL);
}

// no_config_log_facility: 命令 "no log facility [FACILITY] [LEVEL]"。
//   把 facility 复位为默认值(DESTROY .../facility)。参数仅为语法兼容。
DEFPY_YANG (no_config_log_facility,
       no_config_log_facility_cmd,
       "no log facility [<kern|user|mail|daemon|auth|syslog|lpr|news|uucp|cron|local0|local1|local2|local3|local4|local5|local6|local7>] [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>]",
       NO_STR
       "Logging control\n"
       "Reset syslog facility to default (daemon)\n"
       LOG_FACILITY_DESC
       LOG_LEVEL_DESC)
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/facility", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_record_priority: 命令 "[no] log record-priority"。
//   是否在日志消息正文里附带该消息的优先级。写 .../record-priority
//   的 true / false。
DEFPY_YANG (config_log_record_priority,
       config_log_record_priority_cmd,
       "[no] log record-priority",
	NO_STR
       "Logging control\n"
       "Log the priority of the message within the message\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/record-priority", NB_OP_MODIFY,
			      no ? "false" : "true");
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_record_severity: 命令 "[no] log record-severity"。
//   是否在日志消息正文里附带严重级(severity)。写 .../record-severity。
DEFPY_YANG (config_log_record_severity,
       config_log_record_severity_cmd,
       "[no] log record-severity",
	NO_STR
       "Logging control\n"
       "Log the severity of the message within the message\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/record-severity", NB_OP_MODIFY,
			      no ? "false" : "true");
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_timestamp_precision: 命令 "log timestamp precision (0-6)"。
//   设置时间戳小数位(亚秒精度，0~6 位)。precision 由 DEFPY 解析为整数，
//   这里格式化成字符串写入 .../timestamp-precision。
DEFPY_YANG (config_log_timestamp_precision,
       config_log_timestamp_precision_cmd,
       "log timestamp precision (0-6)",
       "Logging control\n"
       "Timestamp configuration\n"
       "Set the timestamp precision\n"
       "Number of subsecond digits\n")
{
	char buf[8];

	snprintf(buf, sizeof(buf), "%u", (uint)precision);
	nb_cli_enqueue_change(vty, "/frr-logging:logging/timestamp-precision", NB_OP_MODIFY, buf);
	return nb_cli_apply_changes(vty, NULL);
}

// no_config_log_timestamp_precision: 命令 "no log timestamp precision"。
//   把时间戳精度复位为默认值 0(DESTROY .../timestamp-precision)。
DEFPY_YANG (no_config_log_timestamp_precision,
       no_config_log_timestamp_precision_cmd,
       "no log timestamp precision [(0-6)]",
       NO_STR
       "Logging control\n"
       "Timestamp configuration\n"
       "Reset the timestamp precision to the default value of 0\n"
       "Number of subsecond digits\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/timestamp-precision", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_ec: 命令 "[no] log error-category"。
//   是否在消息文本前加 "[EC 9999]" 形式的错误类别码，便于跨语言/跨
//   版本检索。写 .../error-category 的 true / false。
DEFPY_YANG (config_log_ec,
       config_log_ec_cmd,
       "[no] log error-category",
       NO_STR
       "Logging control\n"
       "Prefix log message text with [EC 9999] code\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/error-category", NB_OP_MODIFY,
			      no ? "false" : "true");
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_xid: 命令 "[no] log unique-id"。
//   是否在消息文本前加 "[XXXXX-XXXXX]" 形式的唯一 ID 前缀，便于把
//   一条日志与源码位置/backtrace 关联。写 .../unique-id。
DEFPY_YANG (config_log_xid,
       config_log_xid_cmd,
       "[no] log unique-id",
       NO_STR
       "Logging control\n"
       "Prefix log message text with [XXXXX-XXXXX] identifier\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/unique-id", NB_OP_MODIFY,
			      no ? "false" : "true");
	return nb_cli_apply_changes(vty, NULL);
}

// config_log_filterfile: 命令 "log filtered-file FILENAME [LEVEL]"。
//   配置"带字符串过滤的文件日志目标"。它写到
//   /frr-logging:logging/filtered-file(含 filename / level)。
//   该目标在写盘前会按 log_filter.c 维护的过滤串做筛选。
DEFPY_YANG (config_log_filterfile,
       config_log_filterfile_cmd,
       "log filtered-file FILENAME [<emergencies|alerts|critical|errors|warnings|notifications|informational|debugging>$levelarg]",
       "Logging control\n"
       "Logging to file with string filter\n"
       "Logging filename\n"
       LOG_LEVEL_DESC)
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/filtered-file", NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, "/frr-logging:logging/filtered-file/filename", NB_OP_MODIFY,
			      filename);
	if (levelarg)
		nb_cli_enqueue_change(vty, "/frr-logging:logging/filtered-file/level",
				      NB_OP_MODIFY, log_lev2sev(levelarg));
	else
		nb_cli_enqueue_change(vty, "/frr-logging:logging/filtered-file/level",
				      NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// no_config_log_filterfile: 命令 "no log filtered-file [FILENAME [LEVEL]]"。
//   删除 filtered-file 目标整节点(DESTROY .../filtered-file)。
DEFPY_YANG (no_config_log_filterfile,
       no_config_log_filterfile_cmd,
       "no log filtered-file [FILENAME [LEVEL]]",
       NO_STR
       "Logging control\n"
       "Cancel logging to file with string filter\n"
       "Logging file name\n"
       "Logging level\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/filtered-file", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

// log_filter: 命令 "[no] log filter-text WORD"。
//   增/删一条"过滤字符串"。CREATE 时值即 filter 词；DESTROY 删除。
//   落到 /frr-logging:logging/filter-text，由 northbound 回调
//   最终驱动 log_filter.c 的 zlog_filter_add / zlog_filter_del。
DEFPY_YANG (log_filter,
       log_filter_cmd,
       "[no] log filter-text WORD$filter",
       NO_STR
       "Logging control\n"
       FILTER_LOG_STR
       "String to filter by\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/filter-text",
			      no ? NB_OP_DESTROY : NB_OP_CREATE, filter);
	return nb_cli_apply_changes(vty, NULL);
}

/* Clear all log filters */
// log_filter_clear: 命令 "clear log filter-text"，清空全部过滤串。
//   通过 DESTROY .../filter-text 节点实现。函数体在 return 之后还有
//   一句被注释掉的旧调用与一行不可达的 return，属历史残留。
DEFPY_YANG (log_filter_clear,
       log_filter_clear_cmd,
       "clear log filter-text",
       CLEAR_STR
       "Logging control\n"
       FILTER_LOG_STR)
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/filter-text", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
	/* zlog_filter_clear(); */
	return CMD_SUCCESS;
}

/* Enable/disable 'immediate' mode, with no output buffering */
// log_immediate_mode: 命令 "[no] log immediate-mode"。
//   开关"立即输出"模式：开启后日志不做缓冲、每条都立刻落盘/输出，
//   便于实时排障但性能较差。写 .../immediate-mode 的 true / false。
DEFPY_YANG (log_immediate_mode,
       log_immediate_mode_cmd,
       "[no] log immediate-mode",
       NO_STR
       "Logging control\n"
       "Output immediately, without buffering\n")
{
	nb_cli_enqueue_change(vty, "/frr-logging:logging/immediate-mode", NB_OP_MODIFY,
			      no ? "false" : "true");
	return nb_cli_apply_changes(vty, NULL);
}


// clear_log_cmdline: 命令 "clear log cmdline-targets"。
//   关闭由启动命令行 --log 指定的日志目标。它走 RPC(不是普通配置
//   事务)，因为要作用于"命令行阶段"建立的目标。
DEFPY_YANG (clear_log_cmdline,
	    clear_log_cmdline_cmd,
	    "clear log cmdline-targets",
	    CLEAR_STR
	    "Logging control\n"
	    "Disable log targets specified at startup by --log option\n")
{
	return nb_cli_rpc(vty, "/frr-logging:clear-cmdline-targets", NULL);
}

// log_cli_init: 把所有 log 命令安装到对应 vty 节点。
//   绝大多数配置命令装到 CONFIG_NODE(config 模式)；
//   clear log cmdline-targets 装到 ENABLE_NODE(enable 模式)。
//   由库初始化流程调用一次。
void log_cli_init(void)
{
	install_element(CONFIG_NODE, &config_log_stdout_cmd);
	install_element(CONFIG_NODE, &config_log_monitor_cmd);
	install_element(CONFIG_NODE, &config_log_file_cmd);
	install_element(CONFIG_NODE, &config_log_dmn_file_cmd);
	install_element(CONFIG_NODE, &no_config_log_dmn_file_cmd);
	install_element(CONFIG_NODE, &config_log_syslog_cmd);
	install_element(CONFIG_NODE, &no_config_log_syslog_cmd);
	install_element(CONFIG_NODE, &config_log_facility_cmd);
	install_element(CONFIG_NODE, &no_config_log_facility_cmd);
	install_element(CONFIG_NODE, &config_log_record_priority_cmd);
	install_element(CONFIG_NODE, &config_log_record_severity_cmd);
	install_element(CONFIG_NODE, &config_log_timestamp_precision_cmd);
	install_element(CONFIG_NODE, &no_config_log_timestamp_precision_cmd);
	install_element(CONFIG_NODE, &config_log_ec_cmd);
	install_element(CONFIG_NODE, &config_log_xid_cmd);

	install_element(CONFIG_NODE, &log_filter_cmd);
	install_element(CONFIG_NODE, &log_filter_clear_cmd);
	install_element(CONFIG_NODE, &config_log_filterfile_cmd);
	install_element(CONFIG_NODE, &no_config_log_filterfile_cmd);
	install_element(CONFIG_NODE, &log_immediate_mode_cmd);

	install_element(CONFIG_NODE, &debug_uid_backtrace_cmd);

	install_element(ENABLE_NODE, &clear_log_cmdline_cmd);
}


/* clang-format off */
// logging_stdout_cli_write: cli_show 回调，把 .../stdout 渲染成配置文本。
//   取该节点 level 对应等级；若等级不是默认的 LOG_DEBUG 或要求显示默认，
//   就带等级输出 "log stdout LEVEL"，否则输出裸 "log stdout"。
static void logging_stdout_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	int level = log_level_match(yang_dnode_get_string(dnode, "level"));

	if (level != LOG_DEBUG || show_defaults)
		vty_out(vty, "log stdout %s\n", zlog_priority_str(level));
	else
		vty_out(vty, "log stdout\n");
}

// logging_syslog_cli_write: cli_show 回调，把 .../syslog 渲染成
//   "log syslog [LEVEL]"，规则同 stdout。
static void logging_syslog_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	int level = log_level_match(yang_dnode_get_string(dnode, "level"));

	if (level != LOG_DEBUG || show_defaults)
		vty_out(vty, "log syslog %s\n", zlog_priority_str(level));
	else
		vty_out(vty, "log syslog\n");
}

// logging_file_filename_cli_write: cli_show 回调，渲染普通文件目标
//   "log file NAME [LEVEL]"。fname 取本节点(leaf)值，level 取同级
//   "../level"；等级等于默认时省略。
static void logging_file_filename_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	const char *fname = yang_dnode_get_string(dnode, NULL);
	int level = log_level_match(yang_dnode_get_string(dnode, "../level"));

	if (level != LOG_DEBUG  || show_defaults)
		vty_out(vty, "log file %s %s\n", fname, zlog_priority_str(level));
	else
		vty_out(vty, "log file %s\n", fname);
}

// logging_filtered_file_filename_cli_write: cli_show 回调，渲染
//   "log filtered-file NAME [LEVEL]"，规则同普通文件目标。
static void logging_filtered_file_filename_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	const char *fname = yang_dnode_get_string(dnode, NULL);
	int level = log_level_match(yang_dnode_get_string(dnode, "../level"));

	if (level != LOG_DEBUG || show_defaults)
		vty_out(vty, "log filtered-file %s %s\n", fname, zlog_priority_str(level));
	else
		vty_out(vty, "log filtered-file %s\n", fname);
}

// logging_daemon_file_filename_cli_write: cli_show 回调，渲染
//   "log daemon DAEMON file NAME [LEVEL]"。daemon 取同级 "../daemon"。
static void logging_daemon_file_filename_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	const char *fname = yang_dnode_get_string(dnode, NULL);
	const char *daemon = yang_dnode_get_string(dnode, "../daemon");
	int level = log_level_match(yang_dnode_get_string(dnode, "../level"));

	if (level != LOG_DEBUG || show_defaults)
		vty_out(vty, "log daemon %s file %s %s\n", daemon, fname, zlog_priority_str(level));
	else
		vty_out(vty, "log daemon %s file %s\n", daemon, fname);
}

// logging_facility_cli_write: cli_show 回调，渲染 "log facility FACILITY"。
//   yang_dnode_get_identity 取回身份名(不含 "ietf-syslog-types:" 前缀)。
static void logging_facility_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	vty_out(vty, "log facility %s\n", yang_dnode_get_identity(dnode));
}

// logging_record_priority_cli_write: cli_show 回调，渲染 record-priority。
//   开启则输出 "log record-priority"；关闭时仅当 show_defaults 才输出
//   "no log record-priority"(默认不显示)。
static void logging_record_priority_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	bool enable = yang_dnode_get_bool(dnode, NULL);

	if (enable)
		vty_out(vty, "log record-priority\n");
	else if (show_defaults)
		vty_out(vty, "no log record-priority\n");
}

// logging_record_severity_cli_write: cli_show 回调，渲染 record-severity，
//   规则同 record-priority。
static void logging_record_severity_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	bool enable = yang_dnode_get_bool(dnode, NULL);

	if (enable)
		vty_out(vty, "log record-severity\n");
	else if (show_defaults)
		vty_out(vty, "no log record-severity\n");
}

// logging_timestamp_precision_cli_write: cli_show 回调，渲染时间戳精度。
//   精度串非 "0"(默认)或 show_defaults 时输出
//   "log timestamp precision N"。
static void logging_timestamp_precision_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	const char *prec = yang_dnode_get_string(dnode, NULL);

	if (strcmp(prec, "0") || show_defaults)
		vty_out(vty, "log timestamp precision %s\n", prec);
}
// logging_error_category_cli_write: cli_show 回调，渲染 error-category。
//   关闭时输出 "no log error-category"；开启且 show_defaults 时输出
//   "log record-category"(此处串为历史拼写，注意与命令名差异)。
static void logging_error_category_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	bool enable = yang_dnode_get_bool(dnode, NULL);

	if (!enable)
		vty_out(vty, "no log error-category\n");
	else if (show_defaults)
		vty_out(vty, "log record-category\n");
}
// logging_unique_id_cli_write: cli_show 回调，渲染 unique-id。
//   关闭时输出 "no log unique-id"；开启且 show_defaults 时输出
//   "log unique-id"。
static void logging_unique_id_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	bool enable = yang_dnode_get_bool(dnode, NULL);

	if (!enable)
		vty_out(vty, "no log unique-id\n");
	else if (show_defaults)
		vty_out(vty, "log unique-id\n");
}
// logging_immediate_mode_cli_write: cli_show 回调，渲染 immediate-mode。
//   开启输出 "log immediate-mode"；关闭且 show_defaults 输出
//   "no log immediate-mode"。
static void logging_immediate_mode_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	bool enable = yang_dnode_get_bool(dnode, NULL);

	if (enable)
		vty_out(vty, "log immediate-mode\n");
	else if (show_defaults)
		vty_out(vty, "no log immediate-mode\n");
}

// logging_uid_backtrace_cli_write: cli_show 回调，渲染
//   "debug unique-id UID backtrace"。uid 取本节点谓词/键值。
static void logging_uid_backtrace_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	vty_out(vty, "debug unique-id %s backtrace\n", yang_dnode_get_string(dnode, "uid"));
}

// frr_logging_cli_info: frr-logging 模块的 CLI 侧模块信息。
//   ignore_cfg_cbs=true 表示这里只提供 cli_show(回显)回调、不参与
//   配置回放；nodes[] 为每个 xpath 绑定一个 logging_*_cli_write，
//   show running-config 时按 xpath 调用对应回调输出命令行。
//   被注释掉的 filter-text 项表示该配置暂不写进 running-config。
struct frr_yang_module_info frr_logging_cli_info = {
	.name = "frr-logging",
	.ignore_cfg_cbs = true,
	.nodes = {
		{ .xpath = "/frr-logging:logging/stdout", .cbs.cli_show = logging_stdout_cli_write },
		{ .xpath = "/frr-logging:logging/syslog", .cbs.cli_show = logging_syslog_cli_write },
		{ .xpath = "/frr-logging:logging/file/filename", .cbs.cli_show = logging_file_filename_cli_write },
		{ .xpath = "/frr-logging:logging/filtered-file/filename", .cbs.cli_show = logging_filtered_file_filename_cli_write },
		/* This is not saved, but may be the future. */
		/* { .xpath = "/frr-logging:logging/filter-text", .cbs.cli_show = logging_filter_text_cli_write }, */
		{ .xpath = "/frr-logging:logging/daemon-file/filename", .cbs.cli_show = logging_daemon_file_filename_cli_write },
		{ .xpath = "/frr-logging:logging/facility", .cbs.cli_show = logging_facility_cli_write },
		{ .xpath = "/frr-logging:logging/record-priority", .cbs.cli_show = logging_record_priority_cli_write },
		{ .xpath = "/frr-logging:logging/record-severity", .cbs.cli_show = logging_record_severity_cli_write },
		{ .xpath = "/frr-logging:logging/timestamp-precision", .cbs.cli_show = logging_timestamp_precision_cli_write },
		{ .xpath = "/frr-logging:logging/error-category", .cbs.cli_show = logging_error_category_cli_write },
		{ .xpath = "/frr-logging:logging/unique-id", .cbs.cli_show = logging_unique_id_cli_write },
		{ .xpath = "/frr-logging:logging/immediate-mode", .cbs.cli_show = logging_immediate_mode_cli_write },
		{ .xpath = "/frr-logging:logging/uid-backtrace", .cbs.cli_show = logging_uid_backtrace_cli_write },
		{ .xpath = NULL },
	}
};

// frr_logging_merge_cli_to_nb_info: 把 CLI 侧的 cli_show 回调补进
//   northbound 侧的 frr_logging_nb_info(定义在别的文件)。
//   lib 里 YANG 模块信息与 northbound 各持一份节点表，按 xpath 配对，
//   把 frr_logging_cli_info.nodes[i].cbs.cli_show 复制过去；遍历完
//   某条 xpath 后 assert 保证确实在 nb_info 里找到了对应节点。
void frr_logging_merge_cli_to_nb_info(void)
{
	int i, j;

	for (i = 0; frr_logging_cli_info.nodes[i].xpath != NULL; i++) {
		for (j = 0; frr_logging_nb_info.nodes[j].xpath != NULL; j++) {
			if (!strcmp(frr_logging_cli_info.nodes[i].xpath, frr_logging_nb_info.nodes[j].xpath)) {
				frr_logging_nb_info.nodes[j].cbs.cli_show = frr_logging_cli_info.nodes[i].cbs.cli_show;
				break;
			}
		}
		/* Make sure we found one */
		assert(frr_logging_nb_info.nodes[j].xpath != NULL);
	}
}
