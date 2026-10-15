// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 8 2025, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2025, LabN Consulting, L.L.C.
 *
 */
/*
 * 文件导读（log_nb.c）
 *
 * 日志功能的 YANG 北向（northbound）回调实现。FRR 用统一的北向接口把 CLI、
 * NETCONF、RESTCONF、配置数据库等所有配置入口收敛到一棵 YANG 数据树上；本
 * 文件就是 frr-logging 模块的各个节点在"创建 / 删除 / 修改"时的处理函数。
 *
 * 职责：
 *  - 把 YANG 树里的日志配置（stdout、syslog、file 等的 level / filename）
 *    翻译成对具体日志目标的操作（设置级别、打开 / 关闭文件、应用到 5424 或
 *    文件目标）。
 *  - 提供两个基础设施：log_stdout_apply_level（统一 stdout 级别）、
 *    set_log_file（设置文件目标）。
 *  - command_setup_early_logging：在进程极早期、尚未用北向框架时，先按命令行
 *    参数把日志出口架起来。
 *
 * 注意：回调在非 APPLY 事件（校验 / 预处理阶段）下通常直接返回 NB_OK，只有
 * APPLY 事件才真正改动运行状态。
 */
#include <zebra.h>
#include "lib/command.h"
#include "lib/northbound.h"
#include "lib/lib_errors.h"
#include "lib/log.h"
#include "lib/log_vty.h"
#include "lib/vty.h"
// #include "lib/zlog_targets.h"
#include "lib/zlog_5424.h"

/* 取两个日志级别中更宽松（数值更大）的一个，用于"配置文件与命令行取较松者" */
#define ZLOG_MAXLVL(a, b) MAX(a, b)

/* Default logging level in the YANG model */
/* log_default_lvl 的定义：默认日志级别为 LOG_DEBUG（声明见 log_vty.h） */
const int log_default_lvl = LOG_DEBUG;

/*
 * 应用 stdout 日志级别：取配置文件级别与命令行级别中较宽松者（MAX）。
 * 若 stdout 由 journald 接管，则设置 zt_stdout_journald.prio_min 并 apply_meta；
 * 否则设置 zt_stdout_file.prio_min 并 zlog_file_set_other。级别未变则直接返回。
 */
void log_stdout_apply_level(void)
{
	int maxlvl;

	maxlvl = ZLOG_MAXLVL(log_config_stdout_lvl, log_cmdline_stdout_lvl);

	if (stdout_journald_in_use) {
		if (zt_stdout_journald.prio_min == maxlvl)
			return;
		zt_stdout_journald.prio_min = maxlvl;
		zlog_5424_apply_meta(&zt_stdout_journald);
	} else {
		if (zt_stdout_file.prio_min == maxlvl)
			return;
		zt_stdout_file.prio_min = maxlvl;
		zlog_file_set_other(&zt_stdout_file);
	}
}

/*
 * 把一个文件日志目标的文件名与级别设好。若 fname 不是以目录分隔符开头，则
 * 拼成以当前工作目录为准的绝对路径（并校验长度，防止超长）。随后调用
 * zlog_file_set_filename 真正打开；成功返回 CMD_SUCCESS，否则（视 vty 是否为
 * 空决定是否提示）返回 CMD_WARNING_CONFIG_FAILED。
 */
static int set_log_file(struct zlog_cfg_file *target, struct vty *vty, const char *fname,
			int loglevel)
{
	char path[MAXPATHLEN + 1];
	const char *fullpath;
	bool ok;

	/* Path detection. */
	if (!IS_DIRECTORY_SEP(*fname)) {
		char cwd[MAXPATHLEN + 1];
		int pr;

		cwd[MAXPATHLEN] = '\0';

		if (getcwd(cwd, MAXPATHLEN) == NULL) {
			flog_err_sys(EC_LIB_SYSTEM_CALL, "config_log_file: Unable to alloc mem!");
			return CMD_WARNING_CONFIG_FAILED;
		}

		pr = snprintf(path, sizeof(path), "%s/%s", cwd, fname);
		if (pr < 0 || (unsigned int)pr >= sizeof(path)) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "%s: Path too long ('%s/%s'); system maximum is %u", __func__,
				     cwd, fname, MAXPATHLEN);
			return CMD_WARNING_CONFIG_FAILED;
		}

		fullpath = path;
	} else
		fullpath = fname;

	target->prio_min = loglevel;
	ok = zlog_file_set_filename(target, fullpath);

	if (!ok) {
		if (vty)
			vty_out(vty, "can't open logfile %s\n", fname);
		return CMD_WARNING_CONFIG_FAILED;
	}
	return CMD_SUCCESS;
}

/*
 * 在极早期（读配置前）按命令行参数设置日志目标，供 main() 解析 --log 时调用。
 * dest 形如 "TYPE[:arg]"，TYPE 可为 stdout / syslog / file:<path> /
 * monitor:<fd>；level 为级别名。级别非法或目标非法会打印错误并 exit。
 * 之所以特事特办，是因为此刻事件循环 / VTY 尚未就绪，只能用最基础的方式先把
 * 日志出口架起来（例如把实时日志流指向启动方给的 fd）。
 */
void command_setup_early_logging(const char *dest, const char *level)
{
	int nlevel;
	char *sep;
	int len;
	char type[8];

	if (level) {
		nlevel = log_level_match(level);

		if (nlevel == ZLOG_DISABLED) {
			fprintf(stderr, "invalid log level \"%s\"\n", level);
			exit(1);
		}
	} else
		nlevel = log_default_lvl;

	if (!dest)
		return;

	sep = strchr(dest, ':');
	len = sep ? (int)(sep - dest) : (int)strlen(dest);

	snprintfrr(type, sizeof(type), "%.*s", len, dest);

	if (strcmp(type, "stdout") == 0) {
		log_cmdline_stdout_lvl = nlevel;
		log_stdout_apply_level();
		return;
	}
	if (strcmp(type, "syslog") == 0) {
		log_cmdline_syslog_lvl = nlevel;
		zlog_syslog_set_prio_min(
			ZLOG_MAXLVL(log_config_syslog_lvl, log_cmdline_syslog_lvl));
		return;
	}
	if (strcmp(type, "file") == 0 && sep) {
		sep++;
		if (!set_log_file(&zt_file_cmdline, NULL, sep, nlevel))
			fprintf(stderr, "Unable to set log file: %s\n", sep);
		return;
	}
	if (strcmp(type, "monitor") == 0 && sep) {
		struct zlog_live_cfg cfg = {};
		unsigned long fd;
		char *endp;

		sep++;
		fd = strtoul(sep, &endp, 10);
		if (!*sep || *endp) {
			fprintf(stderr, "invalid monitor fd \"%s\"\n", sep);
			exit(1);
		}

		zlog_live_open_fd(&cfg, nlevel, fd);
		zlog_live_disown(&cfg);
		return;
	}

	fprintf(stderr, "invalid log target \"%s\" (\"%s\")\n", type, dest);
	exit(1);
}

/*
 * 从北向节点的 level 字段取值并转成内部级别：xpath 为空时用 dnode 自身，否则
 * 在其下按 xpath 查找；若该字段不存在（说明还是 YANG 默认值），返回
 * log_default_lvl。最后用 log_level_match 把字符串级别转成数值。
 */
static int _get_level_value(const struct lyd_node *dnode, const char *xpath)
{
	const struct lyd_node *level_node;
	const char *level_str;

	assert(dnode);

	if (!xpath)
		level_node = dnode;
	else if (yang_dnode_exists(dnode, xpath))
		level_node = yang_dnode_get(dnode, xpath);
	else
		/* If we change the YANG level defaults this needs to look those up */
		return log_default_lvl;
	assert(level_node);

	level_str = lyd_get_value(level_node);
	assert(level_str);

	return log_level_match(level_str);
}

/*
 * XPath: /frr-logging:logging/stdout
 */
/* 北向回调：创建 /frr-logging:logging/stdout 节点。仅在 APPLY 事件下动作：
 * 读取 level 存入 log_config_stdout_lvl 并应用。 */
static int logging_stdout_create(struct nb_cb_create_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	log_config_stdout_lvl = _get_level_value(args->dnode, "level");
	log_stdout_apply_level();
	return NB_OK;
}


/* 北向回调：删除 stdout 节点。把 stdout 级别复位为 ZLOG_DISABLED 并重新应用 */
static int logging_stdout_destroy(struct nb_cb_destroy_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	log_config_stdout_lvl = ZLOG_DISABLED;
	log_stdout_apply_level();
	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/stdout/level
 */
/* 北向回调：修改 stdout 的 level 字段。把新级别写入 log_config_stdout_lvl 并应用 */
static int logging_stdout_level_modify(struct nb_cb_modify_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	log_config_stdout_lvl = _get_level_value(args->dnode, NULL);
	log_stdout_apply_level();
	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/syslog
 */
/* 北向回调：创建 syslog 节点。读取 level 存入 log_config_syslog_lvl，并用
 * 配置文件与命令行中较宽松者设置 syslog 最低级别 */
static int logging_syslog_create(struct nb_cb_create_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	log_config_syslog_lvl = _get_level_value(args->dnode, "level");
	zlog_syslog_set_prio_min(ZLOG_MAXLVL(log_config_syslog_lvl, log_cmdline_syslog_lvl));
	return NB_OK;
}


/* 北向回调：删除 syslog 节点。把 syslog 级别复位为 ZLOG_DISABLED 并重设最低级别 */
static int logging_syslog_destroy(struct nb_cb_destroy_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	log_config_syslog_lvl = ZLOG_DISABLED;
	zlog_syslog_set_prio_min(ZLOG_MAXLVL(log_config_syslog_lvl, log_cmdline_syslog_lvl));
	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/syslog/level
 */
/* 北向回调：修改 syslog 的 level 字段。写入 log_config_syslog_lvl 并重设最低级别 */
static int logging_syslog_level_modify(struct nb_cb_modify_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	log_config_syslog_lvl = _get_level_value(args->dnode, NULL);
	zlog_syslog_set_prio_min(ZLOG_MAXLVL(log_config_syslog_lvl, log_cmdline_syslog_lvl));
	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/file/filename
 */
/*
 * 北向回调：修改 file 的 filename 字段。取文件名与（父节点的）level，调用
 * set_log_file 打开 zt_file；失败则写 errmsg 并返回 NB_ERR_INCONSISTENCY。
 */
static int logging_file_filename_modify(struct nb_cb_modify_args *args)
{
	const char *fname;
	int level;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* XXX Need to check to see if daemon specific if set and ignore if so */

	fname = yang_dnode_get_string(args->dnode, NULL);
	level = _get_level_value(args->dnode, "../level");
	if (set_log_file(&zt_file, NULL, fname, level) != CMD_SUCCESS) {
		snprintf(args->errmsg, args->errmsg_len, "%% Can't open log file %s", fname);
		return NB_ERR_INCONSISTENCY;
	}
	return NB_OK;
}

/* 北向回调：删除 file 的 filename。把 zt_file 级别复位并 zlog_file_set_other
 * 关闭该文件目标 */
static int logging_file_filename_destroy(struct nb_cb_destroy_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* XXX Need to check to see if daemon specific if set and ignore if so */

	zt_file.prio_min = ZLOG_DISABLED;
	zlog_file_set_other(&zt_file);
	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/file/level
 */
/* 北向回调：修改 file 的 level 字段。优先取同级 filename（若存在），否则沿用
 * zt_file 当前文件名，再用 set_log_file 重新打开以应用新级别 */
static int logging_file_level_modify(struct nb_cb_modify_args *args)
{
	const char *fname = NULL;
	int level;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* XXX Need to check to see if daemon specific if set and ignore if so */

	if (yang_dnode_exists(args->dnode, "../filename"))
		fname = yang_dnode_get_string(args->dnode, "../filename");
	if (!fname)
		fname = zt_file.filename;
	level = _get_level_value(args->dnode, NULL);
	if (set_log_file(&zt_file, NULL, fname, level) != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;
	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/filtered-file/filename
 */
/* 北向回调：修改 filtered-file 的 filename。取文件名与父级 level，作用于
 * zt_filterfile.parent（带过滤的日志文件目标） */
static int logging_filtered_file_filename_modify(struct nb_cb_modify_args *args)
{
	const char *fname;
	int level;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	fname = yang_dnode_get_string(args->dnode, NULL);
	level = _get_level_value(args->dnode, "../level");
	if (set_log_file(&zt_filterfile.parent, NULL, fname, level) != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;
	return NB_OK;
}

/* 北向回调：删除 filtered-file 的 filename。复位 zt_filterfile.parent 级别并关闭 */
static int logging_filtered_file_filename_destroy(struct nb_cb_destroy_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	zt_filterfile.parent.prio_min = ZLOG_DISABLED;
	zlog_file_set_other(&zt_filterfile.parent);
	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/filtered-file/level
 */
/* 北向回调：修改 filtered-file 的 level。取同级 filename（若存在，否则沿用
 * zt_file.filename）与新级别，重新打开过滤文件目标 */
static int logging_filtered_file_level_modify(struct nb_cb_modify_args *args)
{
	const char *fname = NULL;
	int level;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	if (yang_dnode_exists(args->dnode, "../filename"))
		fname = yang_dnode_get_string(args->dnode, "../filename");
	if (!fname)
		fname = zt_file.filename;
	level = _get_level_value(args->dnode, NULL);
	if (set_log_file(&zt_filterfile.parent, NULL, fname, level) != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;
	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/filter-text
 */
/* 北向回调：创建 filter-text 节点。把该文本加入日志过滤器
 * （zlog_filter_add），用于按正则等进行日志过滤 */
static int logging_filter_text_create(struct nb_cb_create_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	zlog_filter_add(yang_dnode_get_string(args->dnode, NULL));

	return NB_OK;
}


/* 北向回调：删除 filter-text 节点。把该文本移出日志过滤器（zlog_filter_del） */
static int logging_filter_text_destroy(struct nb_cb_destroy_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	zlog_filter_del(yang_dnode_get_string(args->dnode, NULL));

	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/daemon-file
 */
/*
 * 北向回调：创建 daemon-file 节点（"仅某个守护进程专属的日志文件"）。
 * 若不是本进程（daemon 名与 frr_get_progname() 不符）则忽略；否则取 filename
 * 与 level，作用于本进程的 zt_file。
 */
static int logging_daemon_file_create(struct nb_cb_create_args *args)
{
	const char *daemon, *fname;
	int level;

	/* XXX Revist this and how it interacts with log file */

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	daemon = yang_dnode_get_string(args->dnode, "daemon");
	if (!strmatch(daemon, frr_get_progname()))
		return NB_OK;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	fname = yang_dnode_get_string(args->dnode, "filename");
	level = _get_level_value(args->dnode, "level");
	if (set_log_file(&zt_file, NULL, fname, level) != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;
	return NB_OK;
}


/* 北向回调：删除 daemon-file 节点。仅当属于本进程时才有意义；当前实现只判断
 * 归属后返回（注释指出可能还需恢复 /frr-logging:logging/file 的配置） */
static int logging_daemon_file_destroy(struct nb_cb_destroy_args *args)
{
	const char *daemon;

	/* XXX Revist this and how it interacts with log file */
	/* Probably need to restore the /frr-logging:logging/file config */

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	daemon = yang_dnode_get_string(args->dnode, "daemon");
	if (!strmatch(daemon, frr_get_progname()))
		return NB_OK;


	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/daemon-file/filename
 */
/* 北向回调：修改 daemon-file 的 filename。仅对本进程生效；当前实现只做归属
 * 判断后返回（待完善） */
static int logging_daemon_file_filename_modify(struct nb_cb_modify_args *args)
{
	const char *daemon;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* XXX Revist this and how it interacts with log file */

	daemon = yang_dnode_get_string(args->dnode, "../daemon");
	if (!strmatch(daemon, frr_get_progname()))
		return NB_OK;

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/daemon-file/level
 */
/* 北向回调：修改 daemon-file 的 level。仅对本进程生效，取 filename 与新级别，
 * 重新打开本进程的 zt_file */
static int logging_daemon_file_level_modify(struct nb_cb_modify_args *args)
{
	const char *daemon, *fname;
	int level;

	/* XXX Revist this and how it interacts with log file */

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	daemon = yang_dnode_get_string(args->dnode, "../daemon");
	if (!strmatch(daemon, frr_get_progname()))
		return NB_OK;

	fname = yang_dnode_get_string(args->dnode, "../filename");
	level = _get_level_value(args->dnode, NULL);
	if (set_log_file(&zt_file, NULL, fname, level) != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;
	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/facility
 */
/* 北向回调：修改 facility。值可能是 "ident:facility" 形式，取冒号后的部分，
 * 经 facility_match 解析后设置 syslog facility */
static int logging_facility_modify(struct nb_cb_modify_args *args)
{
	const char *fs, *s;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	s = yang_dnode_get_string(args->dnode, NULL);
	fs = strchr(s, ':');
	if (fs)
		s = fs + 1;

	zlog_syslog_set_facility(facility_match(s));

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/record-priority
 */
/* 北向回调：修改 record-priority。把该布尔值同步到 zt_file、zt_stdout_file
 * （journald 接管 stdout 时跳过）与 zt_filterfile.parent，并各自 set_other 生效 */
static int logging_record_priority_modify(struct nb_cb_modify_args *args)
{
	bool val;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	val = yang_dnode_get_bool(args->dnode, NULL);
	zt_file.record_priority = val;
	zlog_file_set_other(&zt_file);
	if (!stdout_journald_in_use) {
		zt_stdout_file.record_priority = val;
		zlog_file_set_other(&zt_stdout_file);
	}
	zt_filterfile.parent.record_priority = val;
	zlog_file_set_other(&zt_filterfile.parent);

	return NB_OK;
}

/*
 * XPath: /frr-logging:logging/record-severity
 */
/* 北向回调：修改 record-severity。同步到 zt_file、zt_stdout_file（journald
 * 接管时跳过）与 zt_filterfile.parent 并生效 */
static int logging_record_severity_modify(struct nb_cb_modify_args *args)
{
	bool val;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	val = yang_dnode_get_bool(args->dnode, NULL);
	zt_file.record_severity = val;
	zlog_file_set_other(&zt_file);
	if (!stdout_journald_in_use) {
		zt_stdout_file.record_severity = val;
		zlog_file_set_other(&zt_stdout_file);
	}
	zt_filterfile.parent.record_severity = val;
	zlog_file_set_other(&zt_filterfile.parent);

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/timestamp-precision
 */
/* 北向回调：修改 timestamp-precision。把新的子秒位数同步到 zt_file、
 * zt_file_cmdline、zt_stdout_file（journald 接管时跳过）与 zt_filterfile.parent */
static int logging_timestamp_precision_modify(struct nb_cb_modify_args *args)
{
	uint8_t val;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	val = yang_dnode_get_uint8(args->dnode, NULL);
	zt_file.ts_subsec = val;
	zlog_file_set_other(&zt_file);
	zt_file_cmdline.ts_subsec = val;
	zlog_file_set_other(&zt_file_cmdline);
	if (!stdout_journald_in_use) {
		zt_stdout_file.ts_subsec = val;
		zlog_file_set_other(&zt_stdout_file);
	}
	zt_filterfile.parent.ts_subsec = val;
	zlog_file_set_other(&zt_filterfile.parent);
	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/error-category
 */
/* 北向回调：修改 error-category 开关。调用 zlog_set_prefix_ec 决定日志前缀里
 * 是否带错误分类(EC) */
static int logging_error_category_modify(struct nb_cb_modify_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	zlog_set_prefix_ec(yang_dnode_get_bool(args->dnode, NULL));

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/unique-id
 */
/* 北向回调：修改 unique-id 开关。调用 zlog_set_prefix_xid 决定日志前缀里是否
 * 带唯一标识(xref id) */
static int logging_unique_id_modify(struct nb_cb_modify_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	zlog_set_prefix_xid(yang_dnode_get_bool(args->dnode, NULL));

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/immediate-mode
 */
/* 北向回调：修改 immediate-mode。调用 zlog_set_immediate 决定是否"立即"把日志
 * 推送给实时流客户端（而非缓冲 / 去重） */
static int logging_immediate_mode_modify(struct nb_cb_modify_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	zlog_set_immediate(yang_dnode_get_bool(args->dnode, NULL));

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/uid-backtrace
 */
/*
 * 北向回调：创建 uid-backtrace 节点（为某条日志按 UID 加打印 backtrace）。
 * 由于要校验 UID 合法性，它在 VALIDATE 与 APPLY 两个阶段都动作：
 *  - VALIDATE：查 UID 是否存在且确为日志消息，否则回填 errmsg 返回校验错误；
 *  - APPLY：设置 LOGMSG_FLAG_PERSISTENT 并递增持久 backtrace 计数（幂等）。
 */
static int logging_uid_backtrace_create(struct nb_cb_create_args *args)
{
	struct xrefdata search, *xrd;
	struct xrefdata_logmsg *xrdl;
	const char *uid;

	if (args->event != NB_EV_APPLY && args->event != NB_EV_VALIDATE)
		return NB_OK;

	/* XXX need to put some limits on the value in the YANG model */
	uid = yang_dnode_get_string(args->dnode, "uid");
	strlcpy(search.uid, uid, sizeof(search.uid));
	xrd = xrefdata_uid_find(&xrefdata_uid, &search);

	if (args->event == NB_EV_VALIDATE) {
		if (!xrd || xrd->xref->type != XREFT_LOGMSG) {
			if (args->errmsg && args->errmsg_len)
				snprintfrr(args->errmsg, args->errmsg_len,
					   "UID '%s' does not identify a log messages", uid);
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	/* This was done in the validation step */
	assert(xrd && xrd->xref->type == XREFT_LOGMSG);
	xrdl = container_of(xrd, struct xrefdata_logmsg, xrefdata);
	if (!(xrdl->fl_print_bt & LOGMSG_FLAG_PERSISTENT)) {
		xrdl->fl_print_bt |= LOGMSG_FLAG_PERSISTENT;
		logmsgs_with_persist_bt++;
	}

	return NB_OK;
}


/* 北向回调：删除 uid-backtrace 节点。找到对应日志消息，清除
 * LOGMSG_FLAG_PERSISTENT 并递减持久 backtrace 计数 */
static int logging_uid_backtrace_destroy(struct nb_cb_destroy_args *args)
{
	struct xrefdata search, *xrd;
	struct xrefdata_logmsg *xrdl;
	const char *uid;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	uid = yang_dnode_get_string(args->dnode, "uid");
	strlcpy(search.uid, uid, sizeof(search.uid));
	xrd = xrefdata_uid_find(&xrefdata_uid, &search);
	if (!xrd || xrd->xref->type != XREFT_LOGMSG)
		return NB_OK;

	xrdl = container_of(xrd, struct xrefdata_logmsg, xrefdata);
	if (xrdl->fl_print_bt & LOGMSG_FLAG_PERSISTENT) {
		xrdl->fl_print_bt &= ~LOGMSG_FLAG_PERSISTENT;
		logmsgs_with_persist_bt--;
	}

	return NB_OK;
}


/*
 * XPath: /frr-logging:logging/clear-cmdline-targets
 */
/* 北向 RPC 回调：/frr-logging:clear-cmdline-targets。调用 clear_cmdline_targets
 * 清除所有来自命令行的日志目标（见 log_vty.c） */
static int clear_cmdline_targets_rpc(struct nb_cb_rpc_args *args)
{
	clear_cmdline_targets();
	return NB_OK;
}


/* clang-format off */
/*
 * frr-logging 模块的北向回调表：把 YANG 各 xpath 节点映射到上面的处理函数
 * （create / destroy / modify / rpc）。该结构由北向框架在注册模块时使用，末尾
 * 以 xpath==NULL 结束。注意：log_vty.h 里声明的 frr_logging_cli_info 是 CLI
 * 侧的模块信息，此处的 frr_logging_nb_info 是北向侧。
 */
struct frr_yang_module_info frr_logging_nb_info = {
	.name = "frr-logging",
	.nodes = {
		{
			.xpath = "/frr-logging:clear-cmdline-targets",
			.cbs = {
				.rpc = clear_cmdline_targets_rpc,
			}
		},
		{
			.xpath = "/frr-logging:logging/stdout",
			.cbs = {
				.create = logging_stdout_create,
				.destroy = logging_stdout_destroy,
			}
		},
		{
			.xpath = "/frr-logging:logging/stdout/level",
			.cbs = {
				.modify = logging_stdout_level_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/syslog",
			.cbs = {
				.create = logging_syslog_create,
				.destroy = logging_syslog_destroy,
			}
		},
		{
			.xpath = "/frr-logging:logging/syslog/level",
			.cbs = {
				.modify = logging_syslog_level_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/file/filename",
			.cbs = {
				.modify = logging_file_filename_modify,
				.destroy = logging_file_filename_destroy,
			}
		},
		{
			.xpath = "/frr-logging:logging/file/level",
			.cbs = {
				.modify = logging_file_level_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/filtered-file/filename",
			.cbs = {
				.modify = logging_filtered_file_filename_modify,
				.destroy = logging_filtered_file_filename_destroy,
			}
		},
		{
			.xpath = "/frr-logging:logging/filtered-file/level",
			.cbs = {
				.modify = logging_filtered_file_level_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/filter-text",
			.cbs = {
				.create = logging_filter_text_create,
				.destroy = logging_filter_text_destroy,
			}
		},
		{
			.xpath = "/frr-logging:logging/daemon-file",
			.cbs = {
				.create = logging_daemon_file_create,
				.destroy = logging_daemon_file_destroy,
			}
		},
		{
			.xpath = "/frr-logging:logging/daemon-file/filename",
			.cbs = {
				.modify = logging_daemon_file_filename_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/daemon-file/level",
			.cbs = {
				.modify = logging_daemon_file_level_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/facility",
			.cbs = {
				.modify = logging_facility_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/record-priority",
			.cbs = {
				.modify = logging_record_priority_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/record-severity",
			.cbs = {
				.modify = logging_record_severity_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/timestamp-precision",
			.cbs = {
				.modify = logging_timestamp_precision_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/error-category",
			.cbs = {
				.modify = logging_error_category_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/unique-id",
			.cbs = {
				.modify = logging_unique_id_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/immediate-mode",
			.cbs = {
				.modify = logging_immediate_mode_modify,
			}
		},
		{
			.xpath = "/frr-logging:logging/uid-backtrace",
			.cbs = {
				.create = logging_uid_backtrace_create,
				.destroy = logging_uid_backtrace_destroy,
			}
		},
		{
			.xpath = NULL,
		},
	}
};
