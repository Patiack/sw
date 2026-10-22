// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * ============================================================================
 * 文件导读：northbound_cli.c —— 旧式 CLI 命令与 YANG 配置之间的桥
 * ============================================================================
 *
 * 1) 本文件做什么
 *    实现 northbound_cli.h 声明的接口，把"用命令行做配置"这一传统交互模式，
 *    翻译成对 YANG 数据模型的编辑与提交。核心链路：
 *      命令处理函数 -> nb_cli_enqueue_change() 收集本次命令的若干改动
 *                    -> nb_cli_apply_changes() 拼接基准路径、编辑候选配置
 *                    -> classic 模式下隐式触发 nb_candidate_commit() 提交。
 *
 * 2) 两种 CLI 模式
 *    - FRR_CLI_CLASSIC：命令一经应用即（可能延迟批量地）提交，行为与历史一致。
 *    - FRR_CLI_TRANSACTIONAL：改动只进候选配置，由用户显式 commit 才生效。
 *    分支见 nb_cli_apply_changes_internal。
 *
 * 3) 批处理与挂起提交
 *    短时间内大量命令会被合并进同一事务：nb_cli_schedule_command 累积命令
 *    文本，达到 NB_CMD_BATCH_SIZE 或遇到显式提交时才真正 commit。
 *
 * 4) 重要全局量与回调
 *    vty_shared_candidate_config —— 共享候选配置；
 *    nb_cli_apply_changes_mgmt_cb / nb_cli_rpc_mgmt_cb —— 供 mgmtd 接管配置与
 *    RPC 的钩子，非 NULL 时本文件的本地处理会被代理。
 *
 * 5) 阅读顺序建议
 *    先读 enqueue / apply 这条主链路，再读 RPC 与 confirmed-commit，最后读
 *    与 show running-config 相关的回显代码。
 */

#include <zebra.h>
#include <sys/stat.h>

#include "libfrr.h"
#include "lib/version.h"
#include "defaults.h"
#include "log.h"
#include "lib_errors.h"
#include "command.h"
#include "termtable.h"
#include "db.h"
#include "debug.h"
#include "yang_translator.h"
#include "northbound.h"
#include "northbound_cli.h"
#include "northbound_db.h"
#include "lib/northbound_cli_clippy.c"

/*
 * 各调试开关：nb_dbg_cbs_config / _state / _rpc / _notify 分别控制 northbound
 * 在配置 / 状态 / RPC / 通知回调路径上的调试输出；nb_dbg_notif 控制通知框架，
 * nb_dbg_events 控制本框架事件，nb_dbg_libyang 用于透传 libyang 日志。
 */
struct debug nb_dbg_cbs_config = { 0, "debug northbound callbacks configuration",
				   "Northbound callbacks: configuration" };
struct debug nb_dbg_cbs_state = { 0, "debug northbound callbacks state",
				  "Northbound callbacks: state" };
struct debug nb_dbg_cbs_rpc = { 0, "debug northbound callbacks rpc",
				"Northbound callbacks: RPCs" };
struct debug nb_dbg_cbs_notify = { 0, "debug northbound callbacks notify",
				   "Northbound callbacks: notifications" };
struct debug nb_dbg_notif = { 0, "debug northbound notifications",
			      "Northbound notifications" };
struct debug nb_dbg_events = { 0, "debug northbound events",
			       "Northbound events" };
struct debug nb_dbg_libyang = { 0, "debug northbound libyang", "libyang" };

/*
 * vty_shared_candidate_config —— 进程级共享候选配置（见头文件说明）。
 * master —— 本模块缓存的事件循环，用于挂定时器（如 confirmed-commit）。
 */
struct nb_config *vty_shared_candidate_config;
static struct event_loop *master;

/*
 * vty_show_nb_errors
 *   把一次 northbound 操作的错误类型与描述输出到 vty。
 *   参数：error 为 nb_error 值；errmsg 为可选补充说明（空串则省去该行）。
 */
static void vty_show_nb_errors(struct vty *vty, int error, const char *errmsg)
{
	vty_out(vty, "Error type: %s\n", nb_err_name(error));
	if (strlen(errmsg) > 0)
		vty_out(vty, "Error description: %s\n", errmsg);
}

/*
 * nb_cli_classic_commit
 *   在 classic 模式下提交 vty 的候选配置：调用 nb_candidate_commit 使其生效。
 *   返回：CMD_SUCCESS；失败时打印错误（含被合并进同一事务的命令文本）并重建
 *   候选配置后返回 CMD_WARNING_CONFIG_FAILED。
 *   坑：失败时把候选配置重置回 running，避免残留半成品状态。
 */
static int nb_cli_classic_commit(struct vty *vty)
{
	struct nb_context context = {};
	char errmsg[BUFSIZ] = {0};
	int ret;

	context.client = NB_CLIENT_CLI;
	context.user = vty;
	ret = nb_candidate_commit(context, vty->candidate_config, true, NULL,
				  NULL, errmsg, sizeof(errmsg));
	switch (ret) {
	case NB_OK:
		/* Successful commit. Print warnings (if any). */
		if (strlen(errmsg) > 0)
			vty_out(vty, "%s\n", errmsg);
		break;
	case NB_ERR_NO_CHANGES:
		break;
	default:
		vty_out(vty, "%% Configuration failed.\n\n");
		vty_show_nb_errors(vty, ret, errmsg);
		if (vty->pending_commit)
			vty_out(vty,
				"The following commands were dynamically grouped into the same transaction and rejected:\n%s",
				vty->pending_cmds_buf);

		/* Regenerate candidate for consistency. */
		nb_config_replace(vty->candidate_config, running_config, true);
		return CMD_WARNING_CONFIG_FAILED;
	}

	return CMD_SUCCESS;
}

/*
 * nb_cli_pending_commit_clear
 *   清除 vty 上挂起提交的全部状态与缓冲（pending_commit 标志、累积命令缓冲
 *   及其长度 / 位置计数）。
 */
static void nb_cli_pending_commit_clear(struct vty *vty)
{
	vty->pending_commit = 0;
	vty->buffer_cmd_count = 0;
	XFREE(MTYPE_TMP, vty->pending_cmds_buf);
	vty->pending_cmds_buflen = 0;
	vty->pending_cmds_bufpos = 0;
}

/*
 * nb_cli_pending_commit_check
 *   若 vty 上有挂起提交则执行一次 classic 提交并清空挂起状态。
 *   返回：提交结果；无挂起时直接返回 CMD_SUCCESS。
 */
int nb_cli_pending_commit_check(struct vty *vty)
{
	int ret = CMD_SUCCESS;

	if (vty->pending_commit) {
		ret = nb_cli_classic_commit(vty);
		nb_cli_pending_commit_clear(vty);
	}

	return ret;
}

/*
 * nb_cli_schedule_command
 *   把当前命令文本追加进 vty 的动态挂起缓冲（每行前加 "- " 前缀），置位
 *   pending_commit 并累加命令计数；当计数达到 NB_CMD_BATCH_SIZE 时立即触发
 *   一次提交，从而把多条命令合并进同一事务。
 *   返回：CMD_SUCCESS。
 *   坑：缓冲按需翻倍扩容；待处理文本取自 vty->buf。
 */
static int nb_cli_schedule_command(struct vty *vty)
{
	/* Append command to dynamically sized buffer of scheduled commands.
	 * vty->buf -Incoming config
	 * vty->pending_cmds_buf - Pending buffer where incoming configs are
	 *                         accumulated for later processing
	 * vty->pending_cmds_bufpos - length of the pending buffer
	 *
	 */
	if (!vty->pending_cmds_buf) {
		vty->pending_cmds_buflen = 4096;
		vty->pending_cmds_buf =
			XCALLOC(MTYPE_TMP, vty->pending_cmds_buflen);
	}

	if ((strlen(vty->buf) + 3)
	    > (vty->pending_cmds_buflen - vty->pending_cmds_bufpos)) {
		vty->pending_cmds_buflen *= 2;
		vty->pending_cmds_buf =
			XREALLOC(MTYPE_TMP, vty->pending_cmds_buf,
				 vty->pending_cmds_buflen);
	}
	strlcat(vty->pending_cmds_buf, "- ", vty->pending_cmds_buflen);
	vty->pending_cmds_bufpos = strlcat(vty->pending_cmds_buf, vty->buf,
					   vty->pending_cmds_buflen);

	/* Schedule the commit operation. */
	vty->pending_commit = 1;
	vty->buffer_cmd_count++;
	if (vty->buffer_cmd_count == NB_CMD_BATCH_SIZE)
		nb_cli_pending_commit_check(vty);

	return CMD_SUCCESS;
}

/*
 * nb_cli_enqueue_change
 *   将一个配置改动记录到 vty 的改动数组（cfg_changes）中，供后续 apply 使用。
 *   参数：xpath 目标路径；operation 操作类型；value 可选新值（此处只保存指针，
 *   注意其生命周期须覆盖到 apply）。
 *   坑：改动数达到 VTY_MAXCFGCHANGES 时只打印错误并丢弃本条，不做断言。
 */
void nb_cli_enqueue_change(struct vty *vty, const char *xpath,
			   enum nb_operation operation, const char *value)
{
	struct nb_cfg_change *change;

	if (vty->num_cfg_changes == VTY_MAXCFGCHANGES) {
		/* Not expected to happen. */
		vty_out(vty,
			"%% Exceeded the maximum number of changes (%u) for a single command\n\n",
			VTY_MAXCFGCHANGES);
		return;
	}

	change = &vty->cfg_changes[vty->num_cfg_changes++];
	strlcpy(change->xpath, xpath, sizeof(change->xpath));
	change->operation = operation;
	change->value = value;
}

/*
 * nb_cli_apply_changes_internal
 *   把 vty 上累积的改动一次性编辑进候选配置；classic 模式下再依据 clear_pending
 *   与 pending_allowed 决定是"清挂起并提交""排入批量"还是"立即提交"。
 *   参数：xpath_base 已解析为绝对路径的基准前缀；clear_pending 控制是否清挂起。
 *   返回：classic 模式返回提交结果，事务模式返回 CMD_SUCCESS。
 */
static int nb_cli_apply_changes_internal(struct vty *vty,
					 const char *xpath_base,
					 bool clear_pending)
{
	bool error = false;
	char buf[BUFSIZ];

	VTY_CHECK_XPATH;

	nb_candidate_edit_config_changes(vty->candidate_config, vty->cfg_changes,
					 vty->num_cfg_changes, xpath_base,
					 false, buf, sizeof(buf), &error);
	if (error) {
		/*
		 * Failure to edit the candidate configuration should never
		 * happen in practice, unless there's a bug in the code. When
		 * that happens, log the error but otherwise ignore it.
		 */
		vty_out(vty, "%s", buf);
	}

	/*
	 * Maybe do an implicit commit when using the classic CLI mode.
	 *
	 * NOTE: the implicit commit might be scheduled to run later when
	 * too many commands are being sent at the same time. This is a
	 * protection mechanism where multiple commands are grouped into the
	 * same configuration transaction, allowing them to be processed much
	 * faster.
	 */
	if (frr_get_cli_mode() == FRR_CLI_CLASSIC) {
		if (clear_pending) {
			if (vty->pending_commit)
				return nb_cli_pending_commit_check(vty);
		} else if (vty->pending_allowed)
			return nb_cli_schedule_command(vty);
		assert(!vty->pending_commit);
		return nb_cli_classic_commit(vty);
	}

	return CMD_SUCCESS;
}

/*
 * create_xpath_base_abs
 *   把（可能相对的）xpath_base 解析成绝对路径：为空时视作 "."；若 vty 当前已
 *   进入某个配置层级且 base 以 '.' 开头，则先拼上 VTY_CURR_XPATH。
 *   副作用：结果写入调用方提供的 xpath_base_abs 缓冲。
 */
static void create_xpath_base_abs(struct vty *vty, char *xpath_base_abs,
				  size_t xpath_base_abs_size,
				  const char *xpath_base)
{
	memset(xpath_base_abs, 0, xpath_base_abs_size);

	if (xpath_base[0] == 0)
		xpath_base = ".";

	/* If base xpath is relative, prepend current vty xpath. */
	if (vty->xpath_index > 0 && xpath_base[0] == '.') {
		strlcpy(xpath_base_abs, VTY_CURR_XPATH, xpath_base_abs_size);
		xpath_base++; /* skip '.' */
	}
	strlcat(xpath_base_abs, xpath_base, xpath_base_abs_size);
}

/*
 * _nb_cli_apply_changes
 *   内部统一入口：先把基准路径转成绝对路径；随后若注册了
 *   nb_cli_apply_changes_mgmt_cb（mgmtd 接管）且当前非文件输入，则交给该回调；
 *   否则走本地 nb_cli_apply_changes_internal。
 *   参数：clear_pending 透传给内部函数。
 *   返回：提交 / 处理结果。
 */
static int _nb_cli_apply_changes(struct vty *vty, const char *xpath_base, bool clear_pending)
{
	char xpath_base_abs[XPATH_MAXLEN] = {};

	create_xpath_base_abs(vty, xpath_base_abs, sizeof(xpath_base_abs),
			      xpath_base);

	if (vty->type != VTY_FILE && nb_cli_apply_changes_mgmt_cb)
		return nb_cli_apply_changes_mgmt_cb(vty, xpath_base_abs);

	return nb_cli_apply_changes_internal(vty, xpath_base_abs, clear_pending);
}

/*
 * nb_cli_apply_changes
 *   公开接口：解析 printf 风格的基准路径，然后应用改动（不清挂起）。
 *   参数：xpath_base_fmt 可选前缀格式串。
 *   返回：提交 / 处理结果。
 */
int nb_cli_apply_changes(struct vty *vty, const char *xpath_base_fmt, ...)
{
	char xpath_base[XPATH_MAXLEN] = {};

	/* Parse the base XPath format string. */
	if (xpath_base_fmt) {
		va_list ap;

		va_start(ap, xpath_base_fmt);
		vsnprintf(xpath_base, sizeof(xpath_base), xpath_base_fmt, ap);
		va_end(ap);
	}

	return _nb_cli_apply_changes(vty, xpath_base, false);
}

/*
 * nb_cli_apply_changes_clear_pending
 *   公开接口：解析基准路径后应用改动，并清掉挂起提交（立即生效语义）。
 *   返回：提交 / 处理结果。
 */
int nb_cli_apply_changes_clear_pending(struct vty *vty, const char *xpath_base_fmt, ...)
{
	char xpath_base[XPATH_MAXLEN] = {};

	/* Parse the base XPath format string. */
	if (xpath_base_fmt) {
		va_list ap;

		va_start(ap, xpath_base_fmt);
		vsnprintf(xpath_base, sizeof(xpath_base), xpath_base_fmt, ap);
		va_end(ap);
	}

	return _nb_cli_apply_changes(vty, xpath_base, true);
}

/*
 * nb_cli_rpc_enqueue
 *   把一个 RPC / Action 的输入参数记录到 vty 的 rpc_params 数组。
 *   参数：xpath 相对 input 容器的路径；value 值。
 *   返回：CMD_SUCCESS；超过上限时打印错误并返回 CMD_WARNING。
 */
int nb_cli_rpc_enqueue(struct vty *vty, const char *xpath, const char *value)
{
	struct nb_cfg_change *param;

	if (vty->num_rpc_params == VTY_MAXCFGCHANGES) {
		/* Not expected to happen. */
		vty_out(vty,
			"%% Exceeded the maximum number of params (%u) for a single command\n\n",
			VTY_MAXCFGCHANGES);
		return CMD_WARNING;
	}

	param = &vty->rpc_params[vty->num_rpc_params++];
	strlcpy(param->xpath, xpath, sizeof(param->xpath));
	param->value = value;

	return CMD_SUCCESS;
}

/*
 * nb_cli_rpc
 *   执行一个 YANG RPC / Action：按 xpath 找到 nb_node，构造 input 树并填入
 *   rpc_params；若注册了 nb_cli_rpc_mgmt_cb 则交其处理，否则本地校验并执行，
 *   最终把输出树经 output_p 返回。
 *   返回：CMD_SUCCESS 或 CMD_WARNING。
 *   坑：output_p 可为 NULL；输出树所有权交给调用方。
 */
int nb_cli_rpc(struct vty *vty, const char *xpath, struct lyd_node **output_p)
{
	struct nb_node *nb_node;
	struct lyd_node *input = NULL;
	struct lyd_node *output = NULL;
	LY_ERR err;
	int ret;
	char errmsg[BUFSIZ] = {0};

	nb_node = nb_node_find(xpath);
	if (!nb_node) {
		flog_warn(EC_LIB_YANG_UNKNOWN_DATA_PATH,
			  "%s: unknown data path: %s", __func__, xpath);
		return CMD_WARNING;
	}

	/* create input tree */
	err = yang_new_path2(NULL, ly_native_ctx, xpath, NULL, 0, 0, 0, NULL, &input);
	assert(err == LY_SUCCESS);

	for (size_t i = 0; i < vty->num_rpc_params; i++) {
		err = lyd_new_path(input, ly_native_ctx,
				   vty->rpc_params[i].xpath,
				   vty->rpc_params[i].value, 0, NULL);
		assert(err == LY_SUCCESS);
	}

	if (nb_cli_rpc_mgmt_cb) {
		ret = nb_cli_rpc_mgmt_cb(vty, xpath, input);
		lyd_free_all(input);
		return ret;
	}

	/* validate input tree to create implicit defaults */
	err = lyd_validate_op(input, NULL, LYD_TYPE_RPC_YANG, NULL);
	assert(err == LY_SUCCESS);

	/* create output tree root for population in the callback */
	err = yang_new_path2(NULL, ly_native_ctx, xpath, NULL, 0, 0, 0, NULL, &output);
	assert(err == LY_SUCCESS);

	ret = nb_callback_rpc(nb_node, xpath, input, output, errmsg,
			      sizeof(errmsg));

	/* validate output tree to create implicit defaults */
	err = lyd_validate_op(output, NULL, LYD_TYPE_REPLY_YANG, NULL);
	assert(err == LY_SUCCESS);

	lyd_free_all(input);
	vty->num_rpc_params = 0;

	switch (ret) {
	case NB_OK:
		if (output_p)
			*output_p = output;
		else
			lyd_free_all(output);
		return CMD_SUCCESS;
	default:
		lyd_free_all(output);
		if (strlen(errmsg))
			vty_show_nb_errors(vty, ret, errmsg);
		return CMD_WARNING;
	}
}

/*
 * nb_cli_confirmed_commit_clean
 *   清理 confirmed-commit 状态：取消超时定时器并释放回滚配置快照。
 */
void nb_cli_confirmed_commit_clean(struct vty *vty)
{
	event_cancel(&vty->t_confirmed_commit_timeout);
	nb_config_free(vty->confirmed_commit_rollback);
	vty->confirmed_commit_rollback = NULL;
}

/*
 * nb_cli_confirmed_commit_rollback
 *   执行 confirmed-commit 超时回滚：把保存的回滚快照作为一次提交写回运行配置。
 *   返回：提交结果码；并据结果向 vty 打印成功（含事务 ID）或失败信息。
 */
int nb_cli_confirmed_commit_rollback(struct vty *vty)
{
	struct nb_context context = {};
	uint32_t transaction_id;
	char errmsg[BUFSIZ] = {0};
	int ret;

	/* Perform the rollback. */
	context.client = NB_CLIENT_CLI;
	context.user = vty;
	ret = nb_candidate_commit(
		context, vty->confirmed_commit_rollback, true,
		"Rollback to previous configuration - confirmed commit has timed out",
		&transaction_id, errmsg, sizeof(errmsg));
	if (ret == NB_OK) {
		vty_out(vty,
			"Rollback performed successfully (Transaction ID #%u).\n",
			transaction_id);
		/* Print warnings (if any). */
		if (strlen(errmsg) > 0)
			vty_out(vty, "%s\n", errmsg);
	} else {
		vty_out(vty,
			"Failed to rollback to previous configuration.\n\n");
		vty_show_nb_errors(vty, ret, errmsg);
	}

	return ret;
}

/*
 * nb_cli_confirmed_commit_timeout
 *   定时器回调：confirmed-commit 到期，向用户提示并触发回滚，随后清理状态。
 */
static void nb_cli_confirmed_commit_timeout(struct event *event)
{
	struct vty *vty = EVENT_ARG(event);

	/* XXX: broadcast this message to all logged-in users? */
	vty_out(vty,
		"\nConfirmed commit has timed out, rolling back to previous configuration.\n\n");

	nb_cli_confirmed_commit_rollback(vty);
	nb_cli_confirmed_commit_clean(vty);
}

/*
 * nb_cli_commit
 *   执行 commit 命令的核心逻辑。
 *   参数：force 为 true 时即使候选需要 update 也强行提交；confirmed_timeout 非 0
 *   表示 commit confirmed（超时自动回滚）；comment 为提交注释。
 *   返回：CMD_SUCCESS 或 CMD_WARNING。
 *   坑：若已存在挂起的 confirmed-commit，本次调用要么重置其超时、要么被当作
 *   "确认"而清理，并不会真正提交新配置。
 */
static int nb_cli_commit(struct vty *vty, bool force,
			 unsigned int confirmed_timeout, char *comment)
{
	struct nb_context context = {};
	uint32_t transaction_id = 0;
	char errmsg[BUFSIZ] = {0};
	int ret;

	/* Check if there's a pending confirmed commit. */
	if (vty->t_confirmed_commit_timeout) {
		if (confirmed_timeout) {
			/* Reset timeout if "commit confirmed" is used again. */
			vty_out(vty,
				"%% Resetting confirmed-commit timeout to %u minute(s)\n\n",
				confirmed_timeout);

			event_cancel(&vty->t_confirmed_commit_timeout);
			event_add_timer(master, nb_cli_confirmed_commit_timeout,
					vty, confirmed_timeout * 60,
					&vty->t_confirmed_commit_timeout);
		} else {
			/* Accept commit confirmation. */
			vty_out(vty, "%% Commit complete.\n\n");
			nb_cli_confirmed_commit_clean(vty);
		}
		return CMD_SUCCESS;
	}

	/* "force" parameter. */
	if (!force && nb_candidate_needs_update(vty->candidate_config)) {
		vty_out(vty,
			"%% Candidate configuration needs to be updated before commit.\n\n");
		vty_out(vty,
			"Use the \"update\" command or \"commit force\".\n");
		return CMD_WARNING;
	}

	/* "confirm" parameter. */
	if (confirmed_timeout) {
		vty->confirmed_commit_rollback = nb_config_dup(running_config);

		vty->t_confirmed_commit_timeout = NULL;
		event_add_timer(master, nb_cli_confirmed_commit_timeout, vty,
				confirmed_timeout * 60,
				&vty->t_confirmed_commit_timeout);
	}

	context.client = NB_CLIENT_CLI;
	context.user = vty;
	ret = nb_candidate_commit(context, vty->candidate_config, true, comment,
				  &transaction_id, errmsg, sizeof(errmsg));

	/* Map northbound return code to CLI return code. */
	switch (ret) {
	case NB_OK:
		nb_config_replace(vty->candidate_config_base, running_config,
				  true);
		vty_out(vty,
			"%% Configuration committed successfully (Transaction ID #%u).\n\n",
			transaction_id);
		/* Print warnings (if any). */
		if (strlen(errmsg) > 0)
			vty_out(vty, "%s\n", errmsg);
		return CMD_SUCCESS;
	case NB_ERR_NO_CHANGES:
		vty_out(vty, "%% No configuration changes to commit.\n\n");
		return CMD_SUCCESS;
	default:
		vty_out(vty,
			"%% Failed to commit candidate configuration.\n\n");
		vty_show_nb_errors(vty, ret, errmsg);
		return CMD_WARNING;
	}
}

/*
 * nb_cli_candidate_load_file
 *   从文件把配置装载进候选：按 format 选择走 CLI 命令解析（NB_CFG_FMT_CMDS）
 *   还是 libyang 解析 JSON / XML，必要时经 translator 转成原生模型。
 *   参数：replace 为 true 时替换候选，否则合并。
 *   返回：CMD_SUCCESS 或 CMD_WARNING。
 */
static int nb_cli_candidate_load_file(struct vty *vty,
				      enum nb_cfg_format format,
				      struct yang_translator *translator,
				      const char *path, bool replace)
{
	struct nb_config *loaded_config = NULL;
	struct lyd_node *dnode;
	struct ly_ctx *ly_ctx;
	int ly_format;
	char buf[BUFSIZ];
	LY_ERR err;

	switch (format) {
	case NB_CFG_FMT_CMDS:
		loaded_config = nb_config_new(NULL);
		if (!vty_read_config(loaded_config, path, config_default)) {
			vty_out(vty, "%% Failed to load configuration.\n\n");
			vty_out(vty,
				"Please check the logs for more details.\n");
			nb_config_free(loaded_config);
			return CMD_WARNING;
		}
		break;
	case NB_CFG_FMT_JSON:
	case NB_CFG_FMT_XML:
		ly_format = (format == NB_CFG_FMT_JSON) ? LYD_JSON : LYD_XML;

		ly_ctx = translator ? translator->ly_ctx : ly_native_ctx;
		err = lyd_parse_data_path(ly_ctx, path, ly_format,
					  LYD_PARSE_ONLY | LYD_PARSE_NO_STATE,
					  0, &dnode);
		if (err || !dnode) {
			flog_warn(EC_LIB_LIBYANG, "%s: lyd_parse_path() failed",
				  __func__);
			vty_out(vty, "%% Failed to load configuration:\n\n");
			vty_out(vty, "%s",
				yang_print_errors(ly_native_ctx, buf,
						  sizeof(buf)));
			return CMD_WARNING;
		}
		if (translator
		    && yang_translate_dnode(translator,
					    YANG_TRANSLATE_TO_NATIVE, &dnode)
			       != YANG_TRANSLATE_SUCCESS) {
			vty_out(vty, "%% Failed to translate configuration\n");
			yang_dnode_free(dnode);
			return CMD_WARNING;
		}
		loaded_config = nb_config_new(dnode);
		break;
	}

	if (replace)
		nb_config_replace(vty->candidate_config, loaded_config, false);
	else if (nb_config_merge(vty->candidate_config, loaded_config, false)
		 != NB_OK) {
		vty_out(vty,
			"%% Failed to merge the loaded configuration:\n\n");
		vty_out(vty, "%s",
			yang_print_errors(ly_native_ctx, buf, sizeof(buf)));
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

/*
 * nb_cli_candidate_load_transaction
 *   从 northbound 事务数据库按 transaction_id 取出配置并装载进候选。
 *   参数：replace 决定替换还是合并。
 *   返回：CMD_SUCCESS 或 CMD_WARNING（事务不存在或合并失败）。
 */
static int nb_cli_candidate_load_transaction(struct vty *vty,
					     uint32_t transaction_id,
					     bool replace)
{
	struct nb_config *loaded_config;
	char buf[BUFSIZ];

	loaded_config = nb_db_transaction_load(transaction_id);
	if (!loaded_config) {
		vty_out(vty, "%% Transaction %u does not exist.\n\n",
			transaction_id);
		return CMD_WARNING;
	}

	if (replace)
		nb_config_replace(vty->candidate_config, loaded_config, false);
	else if (nb_config_merge(vty->candidate_config, loaded_config, false)
		 != NB_OK) {
		vty_out(vty,
			"%% Failed to merge the loaded configuration:\n\n");
		vty_out(vty, "%s",
			yang_print_errors(ly_native_ctx, buf, sizeof(buf)));
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

/*
 * nb_cli_show_config_prepare
 *   为回显配置做准备：仅当配置不是 running 时调用一次校验以补齐默认子节点。
 *   参数：with_defaults 表示是否需要默认值（这里用于触发默认节点生成）。
 *   坑：故意忽略校验错误——回显不应因校验问题而失败。
 */
/* Prepare the configuration for display. */
void nb_cli_show_config_prepare(struct nb_config *config, bool with_defaults)
{
	/* Nothing to do for daemons that don't implement any YANG module. */
	if (config->dnode == NULL)
		return;

	/*
	 * Call lyd_validate() only to create default child nodes, ignoring
	 * any possible validation error. This doesn't need to be done when
	 * displaying the running configuration since it's always fully
	 * validated.
	 */
	if (config != running_config)
		(void)lyd_validate_all(&config->dnode, ly_native_ctx,
				       LYD_VALIDATE_NO_STATE, NULL);
}

/*
 * lyd_node_cmp
 *   包装 nb_node 的 cli_cmp 回调，供 list_sort 比较两个 libyang 数据节点。
 *   返回：比较结果。
 */
static int lyd_node_cmp(const struct lyd_node **dnode1,
			const struct lyd_node **dnode2)
{
	struct nb_node *nb_node = (*dnode1)->schema->priv;

	return nb_node->cbs.cli_cmp(*dnode1, *dnode2);
}

/*
 * show_dnode_children_cmds
 *   遍历某节点的所有子节点并按序回显其 CLI 命令：对声明了 cli_cmp 的相邻子
 *   节点先收集到临时链表、排序后再输出，以保证多项条目的顺序稳定。
 *   参数：with_defaults 控制是否显示默认值。
 */
static void show_dnode_children_cmds(struct vty *vty,
				     const struct lyd_node *root,
				     bool with_defaults)
{
	struct nb_node *nb_node, *sort_node = NULL;
	struct listnode *listnode;
	struct lyd_node *child;
	struct list *sort_list;
	void *data;

	LY_LIST_FOR (lyd_child(root), child) {
		nb_node = child->schema->priv;

		/*
		 * We finished processing current list,
		 * it's time to print the config.
		 */
		if (sort_node && nb_node != sort_node) {
			list_sort(sort_list,
				  (int (*)(const void **,
					   const void **))lyd_node_cmp);

			for (ALL_LIST_ELEMENTS_RO(sort_list, listnode, data))
				nb_cli_show_dnode_cmds(vty, data,
						       with_defaults);

			list_delete(&sort_list);
			sort_node = NULL;
		}

		/*
		 * If the config needs to be sorted,
		 * then add the dnode to the sorting
		 * list for later processing.
		 */
		if (nb_node && nb_node->cbs.cli_cmp) {
			if (!sort_node) {
				sort_node = nb_node;
				sort_list = list_new();
			}

			listnode_add(sort_list, child);
			continue;
		}

		nb_cli_show_dnode_cmds(vty, child, with_defaults);
	}

	if (sort_node) {
		list_sort(sort_list,
			  (int (*)(const void **, const void **))lyd_node_cmp);

		for (ALL_LIST_ELEMENTS_RO(sort_list, listnode, data))
			nb_cli_show_dnode_cmds(vty, data, with_defaults);

		list_delete(&sort_list);
		sort_node = NULL;
	}
}

/*
 * nb_cli_show_dnode_cmds
 *   回显单个数据节点对应的 CLI：若为默认值且不要求显示默认则整体跳过；有
 *   cli_show 回调则调用之，非叶节点再递归子节点，最后调用 cli_show_end。
 *   参数：with_defaults 控制是否显示默认值。
 */
void nb_cli_show_dnode_cmds(struct vty *vty, const struct lyd_node *root,
			    bool with_defaults)
{
	struct nb_node *nb_node;

	if (!with_defaults && yang_dnode_is_default_recursive(root))
		return;

	nb_node = root->schema->priv;

	if (nb_node && nb_node->cbs.cli_show)
		(*nb_node->cbs.cli_show)(vty, root, with_defaults);

	if (!(root->schema->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYDATA)))
		show_dnode_children_cmds(vty, root, with_defaults);

	if (nb_node && nb_node->cbs.cli_show_end)
		(*nb_node->cbs.cli_show_end)(vty, root);
}

/*
 * nb_cli_show_config_cmds
 *   以经典 CLI 文本形式回显整份配置：打印头部（version / defaults），逐棵根
 *   子树调用 nb_cli_show_dnode_cmds，最后以 "end" 结束。
 */
static void nb_cli_show_config_cmds(struct vty *vty, struct nb_config *config,
				    bool with_defaults)
{
	struct lyd_node *root;

	vty_out(vty, "Configuration:\n");
	vty_out(vty, "!\n");
	vty_out(vty, "frr version %s\n", FRR_VER_SHORT);
	vty_out(vty, "frr defaults %s\n", frr_defaults_profile());

	LY_LIST_FOR (config->dnode, root) {
		nb_cli_show_dnode_cmds(vty, root, with_defaults);
	}

	vty_out(vty, "!\n");
	vty_out(vty, "end\n");
}

/*
 * nb_cli_show_config_libyang
 *   以 libyang 原生格式（JSON / XML）回显配置：复制数据树、必要时翻译为目标
 *   模型，设置打印选项后输出到 vty。
 *   返回：CMD_SUCCESS 或 CMD_WARNING（翻译失败）。
 */
static int nb_cli_show_config_libyang(struct vty *vty, LYD_FORMAT format,
				      struct nb_config *config,
				      struct yang_translator *translator,
				      bool with_defaults)
{
	struct lyd_node *dnode;
	char *strp;
	int options = 0;

	dnode = yang_dnode_dup(config->dnode);
	if (translator
	    && yang_translate_dnode(translator, YANG_TRANSLATE_FROM_NATIVE,
				    &dnode)
		       != YANG_TRANSLATE_SUCCESS) {
		vty_out(vty, "%% Failed to translate configuration\n");
		yang_dnode_free(dnode);
		return CMD_WARNING;
	}

	SET_FLAG(options, LYD_PRINT_WITHSIBLINGS);
	if (with_defaults)
		SET_FLAG(options, LYD_PRINT_WD_ALL);
	else
		SET_FLAG(options, LYD_PRINT_WD_TRIM);

	if (lyd_print_mem(&strp, dnode, format, options) == 0 && strp) {
		vty_out(vty, "%s", strp);
		free(strp);
	}

	yang_dnode_free(dnode);

	return CMD_SUCCESS;
}

/*
 * nb_cli_show_config
 *   统一回显入口：先做回显准备，再按 format 分派到 CLI 文本或 libyang 输出。
 *   返回：CMD_SUCCESS 或错误码。
 */
static int nb_cli_show_config(struct vty *vty, struct nb_config *config,
			      enum nb_cfg_format format,
			      struct yang_translator *translator,
			      bool with_defaults)
{
	nb_cli_show_config_prepare(config, with_defaults);

	switch (format) {
	case NB_CFG_FMT_CMDS:
		nb_cli_show_config_cmds(vty, config, with_defaults);
		break;
	case NB_CFG_FMT_JSON:
		return nb_cli_show_config_libyang(vty, LYD_JSON, config,
						  translator, with_defaults);
	case NB_CFG_FMT_XML:
		return nb_cli_show_config_libyang(vty, LYD_XML, config,
						  translator, with_defaults);
	}

	return CMD_SUCCESS;
}

/*
 * nb_write_config
 *   把配置写入一个临时文件（mkstemp），供后续 diff 使用。
 *   参数：path 输出临时文件路径；pathlen 缓冲长度。
 *   返回：0 成功，-1 失败（创建或设置权限失败）。
 *   坑：临时文件权限按 CONFIGFILE_MASK 收紧。
 */
static int nb_write_config(struct nb_config *config, enum nb_cfg_format format,
			   struct yang_translator *translator, char *path,
			   size_t pathlen)
{
	int fd;
	struct vty *file_vty;
	int ret = 0;

	snprintf(path, pathlen, "/tmp/frr.tmp.XXXXXXXX");
	fd = mkstemp(path);
	if (fd < 0) {
		flog_warn(EC_LIB_SYSTEM_CALL, "%s: mkstemp() failed: %s",
			  __func__, safe_strerror(errno));
		return -1;
	}
	if (fchmod(fd, CONFIGFILE_MASK) != 0) {
		flog_warn(EC_LIB_SYSTEM_CALL,
			  "%s: fchmod() failed: %s(%d):", __func__,
			  safe_strerror(errno), errno);
		return -1;
	}

	/* Make vty for configuration file. */
	file_vty = vty_new();
	file_vty->wfd = fd;
	file_vty->type = VTY_FILE;
	if (config)
		ret = nb_cli_show_config(file_vty, config, format, translator,
					 false);
	vty_close(file_vty);

	return ret;
}

/*
 * nb_cli_show_config_compare
 *   把两份配置各写入临时文件，再用外部 "diff -u" 比较并把结果逐行输出到 vty
 *   （跳过前两行 diff 头）。
 *   返回：CMD_SUCCESS 或 CMD_WARNING。
 *   坑：依赖系统存在 diff；无论成败都会清理临时文件。
 */
static int nb_cli_show_config_compare(struct vty *vty,
				      struct nb_config *config1,
				      struct nb_config *config2,
				      enum nb_cfg_format format,
				      struct yang_translator *translator)
{
	char config1_path[256];
	char config2_path[256];
	char command[BUFSIZ];
	FILE *fp;
	char line[1024];
	int lineno = 0;

	if (nb_write_config(config1, format, translator, config1_path,
			    sizeof(config1_path))
	    != 0) {
		vty_out(vty, "%% Failed to process configurations.\n\n");
		return CMD_WARNING;
	}
	if (nb_write_config(config2, format, translator, config2_path,
			    sizeof(config2_path))
	    != 0) {
		vty_out(vty, "%% Failed to process configurations.\n\n");
		unlink(config1_path);
		return CMD_WARNING;
	}

	snprintf(command, sizeof(command), "diff -u %s %s", config1_path,
		 config2_path);
	fp = popen(command, "r");
	if (!fp) {
		vty_out(vty, "%% Failed to generate configuration diff.\n\n");
		unlink(config1_path);
		unlink(config2_path);
		return CMD_WARNING;
	}
	/* Print diff line by line. */
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (lineno++ < 2)
			continue;
		vty_out(vty, "%s", line);
	}
	pclose(fp);

	unlink(config1_path);
	unlink(config2_path);

	return CMD_SUCCESS;
}

/*
 * config_exclusive（命令 "configure exclusive"）
 *   进入独占配置模式：其他会话对候选配置的改动会被作废。
 */
/* Configure exclusively from this terminal. */
DEFUN (config_exclusive,
       config_exclusive_cmd,
       "configure exclusive",
       "Configuration from vty interface\n"
       "Configure exclusively from this terminal\n")
{
	return vty_config_enter(vty, true, true, false);
}

/*
 * config_private（命令 "configure private"）
 *   进入私有候选配置模式：每个会话拥有独立、互不干扰的候选配置。
 */
/* Configure using a private candidate configuration. */
DEFUN (config_private,
       config_private_cmd,
       "configure private",
       "Configuration from vty interface\n"
       "Configure using a private candidate configuration\n")
{
	return vty_config_enter(vty, true, false, false);
}

/*
 * config_commit（命令 "commit [force|confirmed (1-60)]"）
 *   提交候选配置：force 表示强制提交，confirmed N 表示 N 分钟内若未再次确认
 *   则自动回滚。最终调用 nb_cli_commit。
 */
DEFPY (config_commit,
       config_commit_cmd,
       "commit [{force$force|confirmed (1-60)}]",
       "Commit changes into the running configuration\n"
       "Force commit even if the candidate is outdated\n"
       "Rollback this commit unless there is a confirming commit\n"
       "Timeout in minutes for the commit to be confirmed\n")
{
	return nb_cli_commit(vty, !!force, confirmed, NULL);
}

/*
 * config_commit_comment（命令 "commit ... comment LINE..."）
 *   与 config_commit 相同，但允许附带一条提交注释（最长 80 字符）。
 *   坑：注释由 argv_concat 动态分配，使用后必须 XFREE。
 */
DEFPY (config_commit_comment,
       config_commit_comment_cmd,
       "commit [{force$force|confirmed (1-60)}] comment LINE...",
       "Commit changes into the running configuration\n"
       "Force commit even if the candidate is outdated\n"
       "Rollback this commit unless there is a confirming commit\n"
       "Timeout in minutes for the commit to be confirmed\n"
       "Assign a comment to this commit\n"
       "Comment for this commit (Max 80 characters)\n")
{
	char *comment;
	int idx = 0;
	int ret;

	argv_find(argv, argc, "LINE", &idx);
	comment = argv_concat(argv, argc, idx);
	ret = nb_cli_commit(vty, !!force, confirmed, comment);
	XFREE(MTYPE_TMP, comment);

	return ret;
}

/*
 * config_commit_check（命令 "commit check"）
 *   只校验候选配置是否合法，不做提交。
 *   返回：CMD_SUCCESS 或 CMD_WARNING。
 */
DEFPY (config_commit_check,
       config_commit_check_cmd,
       "commit check",
       "Commit changes into the running configuration\n"
       "Check if the configuration changes are valid\n")
{
	struct nb_context context = {};
	char errmsg[BUFSIZ] = {0};
	int ret;

	context.client = NB_CLIENT_CLI;
	context.user = vty;
	ret = nb_candidate_validate(&context, vty->candidate_config, errmsg,
				    sizeof(errmsg));
	if (ret != NB_OK) {
		vty_out(vty,
			"%% Failed to validate candidate configuration.\n\n");
		vty_show_nb_errors(vty, ret, errmsg);
		return CMD_WARNING;
	}

	vty_out(vty, "%% Candidate configuration validated successfully.\n\n");

	return CMD_SUCCESS;
}

/*
 * config_update（命令 "update"）
 *   当 running 被外部（如 mgmtd 或其它会话）更新后，把候选配置更新到最新基线。
 *   返回：CMD_SUCCESS；若本就无需更新则直接成功。
 */
DEFPY (config_update,
       config_update_cmd,
       "update",
       "Update candidate configuration\n")
{
	if (!nb_candidate_needs_update(vty->candidate_config)) {
		vty_out(vty, "%% Update is not necessary.\n\n");
		return CMD_SUCCESS;
	}

	if (nb_candidate_update(vty->candidate_config) != NB_OK) {
		vty_out(vty,
			"%% Failed to update the candidate configuration.\n\n");
		vty_out(vty, "Please check the logs for more details.\n");
		return CMD_WARNING;
	}

	nb_config_replace(vty->candidate_config_base, running_config, true);

	vty_out(vty, "%% Candidate configuration updated successfully.\n\n");

	return CMD_SUCCESS;
}

/*
 * config_discard（命令 "discard"）
 *   放弃候选中的改动，把候选重置回其基线（candidate_config_base）。
 */
DEFPY (config_discard,
       config_discard_cmd,
       "discard",
       "Discard changes in the candidate configuration\n")
{
	nb_config_replace(vty->candidate_config, vty->candidate_config_base,
			  true);

	return CMD_SUCCESS;
}

/*
 * config_load（命令 "configuration load ..."）
 *   从文件（可选 JSON / XML 及模块翻译器）或从某个事务把配置装载进候选；
 *   replace 选项决定是替换还是合并。
 */
DEFPY (config_load,
       config_load_cmd,
       "configuration load\
          <\
	    file [<json$json|xml$xml> [translate WORD$translator_family]] FILENAME$filename\
	    |transaction (1-4294967295)$tid\
	  >\
	  [replace$replace]",
       "Configuration related settings\n"
       "Load configuration into candidate\n"
       "Load configuration file into candidate\n"
       "Load configuration file in JSON format\n"
       "Load configuration file in XML format\n"
       "Translate configuration file\n"
       "YANG module translator\n"
       "Configuration file name (full path)\n"
       "Load configuration from transaction into candidate\n"
       "Transaction ID\n"
       "Replace instead of merge\n")
{
	if (filename) {
		enum nb_cfg_format format;
		struct yang_translator *translator = NULL;

		if (json)
			format = NB_CFG_FMT_JSON;
		else if (xml)
			format = NB_CFG_FMT_XML;
		else
			format = NB_CFG_FMT_CMDS;

		if (translator_family) {
			translator = yang_translator_find(translator_family);
			if (!translator) {
				vty_out(vty,
					"%% Module translator \"%s\" not found\n",
					translator_family);
				return CMD_WARNING;
			}
		}

		return nb_cli_candidate_load_file(vty, format, translator,
						  filename, !!replace);
	}

	return nb_cli_candidate_load_transaction(vty, tid, !!replace);
}

/*
 * show_config_running（命令 "show configuration running ..."）
 *   按指定格式（CLI / JSON / XML，可带翻译器与默认值）回显运行配置。
 */
DEFPY (show_config_running,
       show_config_running_cmd,
       "show configuration running\
          [<json$json|xml$xml> [translate WORD$translator_family]]\
	  [with-defaults$with_defaults]",
       SHOW_STR
       "Configuration information\n"
       "Running configuration\n"
       "Change output format to JSON\n"
       "Change output format to XML\n"
       "Translate output\n"
       "YANG module translator\n"
       "Show default values\n")

{
	enum nb_cfg_format format;
	struct yang_translator *translator = NULL;

	if (json)
		format = NB_CFG_FMT_JSON;
	else if (xml)
		format = NB_CFG_FMT_XML;
	else
		format = NB_CFG_FMT_CMDS;

	if (translator_family) {
		translator = yang_translator_find(translator_family);
		if (!translator) {
			vty_out(vty, "%% Module translator \"%s\" not found\n",
				translator_family);
			return CMD_WARNING;
		}
	}

	nb_cli_show_config(vty, running_config, format, translator,
			   !!with_defaults);

	return CMD_SUCCESS;
}

/*
 * show_config_candidate（命令 "show configuration candidate ..."）
 *   回显候选配置；可直接整体显示，或只显示相对基线的差异（changes 选项）。
 */
DEFPY (show_config_candidate,
       show_config_candidate_cmd,
       "show configuration candidate\
          [<json$json|xml$xml> [translate WORD$translator_family]]\
          [<\
	    with-defaults$with_defaults\
	    |changes$changes\
	   >]",
       SHOW_STR
       "Configuration information\n"
       "Candidate configuration\n"
       "Change output format to JSON\n"
       "Change output format to XML\n"
       "Translate output\n"
       "YANG module translator\n"
       "Show default values\n"
       "Show changes applied in the candidate configuration\n")

{
	enum nb_cfg_format format;
	struct yang_translator *translator = NULL;

	if (json)
		format = NB_CFG_FMT_JSON;
	else if (xml)
		format = NB_CFG_FMT_XML;
	else
		format = NB_CFG_FMT_CMDS;

	if (translator_family) {
		translator = yang_translator_find(translator_family);
		if (!translator) {
			vty_out(vty, "%% Module translator \"%s\" not found\n",
				translator_family);
			return CMD_WARNING;
		}
	}

	if (changes)
		return nb_cli_show_config_compare(
			vty, vty->candidate_config_base, vty->candidate_config,
			format, translator);

	nb_cli_show_config(vty, vty->candidate_config, format, translator,
			   !!with_defaults);

	return CMD_SUCCESS;
}

/*
 * show_config_candidate_section（命令 "show"）
 *   配置模式下输入的裸 "show"：在顶层时回显整个候选配置；已进入某个配置节点
 *   时只回显当前这一节（VTY_CURR_XPATH）对应的命令。
 */
DEFPY (show_config_candidate_section,
       show_config_candidate_section_cmd,
       "show",
       SHOW_STR)
{
	struct lyd_node *dnode;

	/* Top-level configuration node, display everything. */
	if (vty->xpath_index == 0)
		return nb_cli_show_config(vty, vty->candidate_config,
					  NB_CFG_FMT_CMDS, NULL, false);

	/* Display only the current section of the candidate configuration. */
	dnode = yang_dnode_get(vty->candidate_config->dnode, VTY_CURR_XPATH);
	if (!dnode)
		/* Shouldn't happen. */
		return CMD_WARNING;

	nb_cli_show_dnode_cmds(vty, dnode, 0);
	vty_out(vty, "!\n");

	return CMD_SUCCESS;
}

/*
 * show_config_compare（命令 "show configuration compare A B ..."）
 *   比较两份配置（候选 / running / 事务，任选两个），按指定格式输出差异。
 */
DEFPY (show_config_compare,
       show_config_compare_cmd,
       "show configuration compare\
          <\
	    candidate$c1_candidate\
	    |running$c1_running\
	    |transaction (1-4294967295)$c1_tid\
	  >\
          <\
	    candidate$c2_candidate\
	    |running$c2_running\
	    |transaction (1-4294967295)$c2_tid\
	  >\
	  [<json$json|xml$xml> [translate WORD$translator_family]]",
       SHOW_STR
       "Configuration information\n"
       "Compare two different configurations\n"
       "Candidate configuration\n"
       "Running configuration\n"
       "Configuration transaction\n"
       "Transaction ID\n"
       "Candidate configuration\n"
       "Running configuration\n"
       "Configuration transaction\n"
       "Transaction ID\n"
       "Change output format to JSON\n"
       "Change output format to XML\n"
       "Translate output\n"
       "YANG module translator\n")
{
	enum nb_cfg_format format;
	struct yang_translator *translator = NULL;
	struct nb_config *config1, *config_transaction1 = NULL;
	struct nb_config *config2, *config_transaction2 = NULL;
	int ret = CMD_WARNING;

	if (c1_candidate)
		config1 = vty->candidate_config;
	else if (c1_running)
		config1 = running_config;
	else {
		config_transaction1 = nb_db_transaction_load(c1_tid);
		if (!config_transaction1) {
			vty_out(vty, "%% Transaction %u does not exist\n\n",
				(unsigned int)c1_tid);
			goto exit;
		}
		config1 = config_transaction1;
	}

	if (c2_candidate)
		config2 = vty->candidate_config;
	else if (c2_running)
		config2 = running_config;
	else {
		config_transaction2 = nb_db_transaction_load(c2_tid);
		if (!config_transaction2) {
			vty_out(vty, "%% Transaction %u does not exist\n\n",
				(unsigned int)c2_tid);
			goto exit;
		}
		config2 = config_transaction2;
	}

	if (json)
		format = NB_CFG_FMT_JSON;
	else if (xml)
		format = NB_CFG_FMT_XML;
	else
		format = NB_CFG_FMT_CMDS;

	if (translator_family) {
		translator = yang_translator_find(translator_family);
		if (!translator) {
			vty_out(vty, "%% Module translator \"%s\" not found\n",
				translator_family);
			goto exit;
		}
	}

	ret = nb_cli_show_config_compare(vty, config1, config2, format,
					 translator);
exit:
	if (config_transaction1)
		nb_config_free(config_transaction1);
	if (config_transaction2)
		nb_config_free(config_transaction2);

	return ret;
}

/*
 * show_config_compare_without_candidate_cmd（ALIAS）
 *   这是 "show configuration compare" 的精简版本：去掉 candidate 选项，因而
 *   可以安装到 enable 节点下。
 */
/*
 * Stripped down version of the "show configuration compare" command.
 * The "candidate" option is not present so the command can be installed in
 * the enable node.
 */
ALIAS (show_config_compare,
       show_config_compare_without_candidate_cmd,
       "show configuration compare\
          <\
	    running$c1_running\
	    |transaction (1-4294967295)$c1_tid\
	  >\
          <\
	    running$c2_running\
	    |transaction (1-4294967295)$c2_tid\
	  >\
	 [<json$json|xml$xml> [translate WORD$translator_family]]",
       SHOW_STR
       "Configuration information\n"
       "Compare two different configurations\n"
       "Running configuration\n"
       "Configuration transaction\n"
       "Transaction ID\n"
       "Running configuration\n"
       "Configuration transaction\n"
       "Transaction ID\n"
       "Change output format to JSON\n"
       "Change output format to XML\n"
       "Translate output\n"
       "YANG module translator\n")

/*
 * clear_config_transactions（命令 "clear configuration transactions oldest (1-100)"）
 *   从事务日志中删除最旧的 n 条事务。未编译 rollback 支持时提示不可用。
 */
DEFPY (clear_config_transactions,
       clear_config_transactions_cmd,
       "clear configuration transactions oldest (1-100)$n",
       CLEAR_STR
       "Configuration activity\n"
       "Delete transactions from the transactions log\n"
       "Delete oldest <n> transactions\n"
       "Number of transactions to delete\n")
{
#ifdef HAVE_CONFIG_ROLLBACKS
	if (nb_db_clear_transactions(n) != NB_OK) {
		vty_out(vty, "%% Failed to delete transactions.\n\n");
		return CMD_WARNING;
	}
#else
	vty_out(vty,
		"%% FRR was compiled without --enable-config-rollbacks.\n\n");
#endif /* HAVE_CONFIG_ROLLBACKS */

	return CMD_SUCCESS;
}

/*
 * config_database_max_transactions（命令 "configuration database max-transactions (1-100)"）
 *   设置事务数据库中保留的最大事务数量。未编译 rollback 支持时提示不可用。
 */
DEFPY (config_database_max_transactions,
       config_database_max_transactions_cmd,
       "configuration database max-transactions (1-100)$max",
       "Configuration related settings\n"
       "Configuration database\n"
       "Set the maximum number of transactions to store\n"
       "Number of transactions\n")
{
#ifdef HAVE_CONFIG_ROLLBACKS
	if (nb_db_set_max_transactions(max) != NB_OK) {
		vty_out(vty,
			"%% Failed to update the maximum number of transactions.\n\n");
		return CMD_WARNING;
	}
	vty_out(vty,
		"%% Maximum number of transactions updated successfully.\n\n");
#else
	vty_out(vty,
		"%% FRR was compiled without --enable-config-rollbacks.\n\n");
#endif /* HAVE_CONFIG_ROLLBACKS */

	return CMD_SUCCESS;
}

/*
 * yang_module_translator_load（命令 "yang module-translator load FILENAME"）
 *   从文件加载一个 YANG 模块翻译器。失败时提示并返回 CMD_WARNING。
 */
DEFPY (yang_module_translator_load,
       yang_module_translator_load_cmd,
       "yang module-translator load FILENAME$filename",
       "YANG related settings\n"
       "YANG module translator\n"
       "Load YANG module translator\n"
       "File name (full path)\n")
{
	struct yang_translator *translator;

	translator = yang_translator_load(filename);
	if (!translator) {
		vty_out(vty, "%% Failed to load \"%s\"\n\n", filename);
		vty_out(vty, "Please check the logs for more details.\n");
		return CMD_WARNING;
	}

	vty_out(vty, "%% Module translator \"%s\" loaded successfully.\n\n",
		translator->family);

	return CMD_SUCCESS;
}

/*
 * yang_module_translator_unload_family（命令 "yang module-translator unload WORD"）
 *   按 family 名卸载一个已加载的 YANG 模块翻译器。
 */
DEFPY (yang_module_translator_unload_family,
       yang_module_translator_unload_cmd,
       "yang module-translator unload WORD$translator_family",
       "YANG related settings\n"
       "YANG module translator\n"
       "Unload YANG module translator\n"
       "Name of the module translator\n")
{
	struct yang_translator *translator;

	translator = yang_translator_find(translator_family);
	if (!translator) {
		vty_out(vty, "%% Module translator \"%s\" not found\n",
			translator_family);
		return CMD_WARNING;
	}

	yang_translator_unload(translator);

	return CMD_SUCCESS;
}

#ifdef HAVE_CONFIG_ROLLBACKS
/*
 * nb_cli_show_transactions_cb
 *   事务遍历回调：把单个事务信息（事务 ID / 客户端 / 日期 / 注释）追加到表格。
 *   参数：arg 为 ttable 句柄。
 */
static void nb_cli_show_transactions_cb(void *arg, int transaction_id,
					const char *client_name,
					const char *date, const char *comment)
{
	struct ttable *tt = arg;

	ttable_add_row(tt, "%d|%s|%s|%s", transaction_id, client_name, date,
		       comment);
}

/*
 * nb_cli_show_transactions
 *   构建并输出事务列表表格：从 northbound 事务数据库遍历所有事务，无数据时
 *   打印提示。
 *   返回：CMD_SUCCESS 或 CMD_WARNING。
 */
static int nb_cli_show_transactions(struct vty *vty)
{
	struct ttable *tt;

	/* Prepare table. */
	tt = ttable_new(&ttable_styles[TTSTYLE_BLANK]);
	ttable_add_row(tt, "Transaction ID|Client|Date|Comment");
	tt->style.cell.rpad = 2;
	tt->style.corner = '+';
	ttable_restyle(tt);
	ttable_rowseps(tt, 0, BOTTOM, true, '-');

	/* Fetch transactions from the northbound database. */
	if (nb_db_transactions_iterate(nb_cli_show_transactions_cb, tt)
	    != NB_OK) {
		vty_out(vty,
			"%% Failed to fetch configuration transactions.\n");
		return CMD_WARNING;
	}

	/* Dump the generated table. */
	if (tt->nrows > 1) {
		char *table;

		table = ttable_dump(tt, "\n");
		vty_out(vty, "%s\n", table);
		XFREE(MTYPE_TMP_TTABLE, table);
	} else
		vty_out(vty, "No configuration transactions to display.\n\n");

	ttable_del(tt);

	return CMD_SUCCESS;
}
#endif /* HAVE_CONFIG_ROLLBACKS */

/*
 * show_config_transaction（命令 "show configuration transaction ..."）
 *   显示配置事务：给 transaction_id 时回显该事务内容（可用 changes 显示与上一
 *   事务的差异）；未给 ID 时列出所有事务。需要编译开启 rollback 支持。
 */
DEFPY (show_config_transaction,
       show_config_transaction_cmd,
       "show configuration transaction\
          [\
	    (1-4294967295)$transaction_id\
	    [<json$json|xml$xml> [translate WORD$translator_family]]\
            [<\
	      with-defaults$with_defaults\
	      |changes$changes\
	     >]\
	  ]",
       SHOW_STR
       "Configuration information\n"
       "Configuration transaction\n"
       "Transaction ID\n"
       "Change output format to JSON\n"
       "Change output format to XML\n"
       "Translate output\n"
       "YANG module translator\n"
       "Show default values\n"
       "Show changes compared to the previous transaction\n")
{
#ifdef HAVE_CONFIG_ROLLBACKS
	if (transaction_id) {
		struct nb_config *config;
		enum nb_cfg_format format;
		struct yang_translator *translator = NULL;

		if (json)
			format = NB_CFG_FMT_JSON;
		else if (xml)
			format = NB_CFG_FMT_XML;
		else
			format = NB_CFG_FMT_CMDS;

		if (translator_family) {
			translator = yang_translator_find(translator_family);
			if (!translator) {
				vty_out(vty,
					"%% Module translator \"%s\" not found\n",
					translator_family);
				return CMD_WARNING;
			}
		}

		config = nb_db_transaction_load(transaction_id);
		if (!config) {
			vty_out(vty, "%% Transaction %u does not exist.\n\n",
				(unsigned int)transaction_id);
			return CMD_WARNING;
		}

		if (changes) {
			struct nb_config *prev_config;
			int ret;

			/* NOTE: this can be NULL. */
			prev_config =
				nb_db_transaction_load(transaction_id - 1);

			ret = nb_cli_show_config_compare(
				vty, prev_config, config, format, translator);
			if (prev_config)
				nb_config_free(prev_config);
			nb_config_free(config);

			return ret;
		}

		nb_cli_show_config(vty, config, format, translator,
				   !!with_defaults);
		nb_config_free(config);

		return CMD_SUCCESS;
	}

	return nb_cli_show_transactions(vty);
#else
	vty_out(vty,
		"%% FRR was compiled without --enable-config-rollbacks.\n\n");
	return CMD_WARNING;
#endif /* HAVE_CONFIG_ROLLBACKS */
}

/*
 * nb_cli_oper_data_cb
 *   传给 nb_oper_walk 的数据回调：把每条 operational data（必要时先经
 *   translator 转换 xpath 与上下文）插入到目标 libyang 树 dnode。
 *   返回：NB_OK 或 NB_ERR；翻译找不到时静默跳过。
 */
static int nb_cli_oper_data_cb(const struct lysc_node *snode,
			       struct yang_translator *translator,
			       struct yang_data *data, void *arg)
{
	struct lyd_node *dnode = arg;
	struct ly_ctx *ly_ctx;

	if (translator) {
		int ret;

		ret = yang_translate_xpath(translator,
					   YANG_TRANSLATE_FROM_NATIVE,
					   data->xpath, sizeof(data->xpath));
		switch (ret) {
		case YANG_TRANSLATE_SUCCESS:
			break;
		case YANG_TRANSLATE_NOTFOUND:
			goto exit;
		case YANG_TRANSLATE_FAILURE:
			goto error;
		}

		ly_ctx = translator->ly_ctx;
	} else
		ly_ctx = ly_native_ctx;

	LY_ERR err =
		lyd_new_path(dnode, ly_ctx, data->xpath, (void *)data->value,
			     LYD_NEW_PATH_UPDATE, &dnode);
	if (err) {
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_new_path(%s) failed: %s",
			  __func__, data->xpath, ly_errmsg(ly_native_ctx));
		goto error;
	}

exit:
	return NB_OK;

error:
	return NB_ERR;
}

/*
 * show_yang_operational_data（命令 "show yang operational-data XPATH ..."）
 *   以 CLI / JSON / XML 回显某 xpath 的 operational data；支持模块翻译器与
 *   with-config 选项。
 */
DEFPY (show_yang_operational_data,
       show_yang_operational_data_cmd,
       "show yang operational-data XPATH$xpath\
         [{\
	   format <json$json|xml$xml>\
	   |translate WORD$translator_family\
	   |with-config$with_config\
	 }]",
       SHOW_STR
       "YANG information\n"
       "Show YANG operational data\n"
       "XPath expression specifying the YANG data path\n"
       "Set the output format\n"
       "JavaScript Object Notation\n"
       "Extensible Markup Language\n"
       "Translate operational data\n"
       "YANG module translator\n"
       "Merge configuration data\n")
{
	LYD_FORMAT format;
	struct yang_translator *translator = NULL;
	struct ly_ctx *ly_ctx;
	struct lyd_node *dnode;
	char *strp;
	uint32_t print_options = LYD_PRINT_WITHSIBLINGS;
	int ret;

	if (xml)
		format = LYD_XML;
	else
		format = LYD_JSON;

	if (translator_family) {
		translator = yang_translator_find(translator_family);
		if (!translator) {
			vty_out(vty, "%% Module translator \"%s\" not found\n",
				translator_family);
			return CMD_WARNING;
		}

		ly_ctx = translator->ly_ctx;
	} else
		ly_ctx = ly_native_ctx;

	/* Obtain data. */
	if (translator) {
		dnode = yang_dnode_new(ly_ctx, false);
		ret = nb_oper_iterate_legacy(xpath, translator, 0,
					   nb_cli_oper_data_cb, dnode, NULL);
	} else {
		dnode = NULL;
		ret = nb_oper_iterate_legacy(xpath, NULL, 0, NULL, NULL, &dnode);
	}
	if (ret != NB_OK) {
		if (format == LYD_JSON)
			vty_out(vty, "{}\n");
		else {
			/* embed ly_last_errmsg() when we get newer libyang */
			vty_out(vty, "<!-- Not found -->\n");
		}
		if (dnode)
			yang_dnode_free(dnode);
		return CMD_WARNING;
	}

	if (with_config && yang_dnode_exists(running_config->dnode, xpath)) {
		struct lyd_node *config_dnode =
			yang_dnode_get(running_config->dnode, xpath);
		if (config_dnode != NULL) {
			lyd_merge_tree(&dnode, yang_dnode_dup(config_dnode),
				       LYD_MERGE_DESTRUCT);
			print_options |= LYD_PRINT_WD_ALL;
		}
	}

	(void)lyd_validate_all(&dnode, ly_ctx, 0, NULL);

	/* Display the data. */
	if (lyd_print_mem(&strp, dnode, format, print_options) != 0 || !strp) {
		vty_out(vty, "%% Failed to display operational data.\n");
		yang_dnode_free(dnode);
		return CMD_WARNING;
	}
	vty_out(vty, "%s", strp);
	free(strp);
	yang_dnode_free(dnode);

	return CMD_SUCCESS;
}

/*
 * show_yang_module（命令 "show yang module ..."）
 *   以表格列出已加载的 YANG 模块（名称 / 版本 / 修订 / 标志 / 命名空间），
 *   可指定模块翻译器以查看其上下文中的模块。
 */
DEFPY (show_yang_module,
       show_yang_module_cmd,
       "show yang module [module-translator WORD$translator_family]",
       SHOW_STR
       "YANG information\n"
       "Show loaded modules\n"
       "YANG module translator\n"
       "YANG module translator\n")
{
	struct ly_ctx *ly_ctx;
	struct yang_translator *translator = NULL;
	const struct lys_module *module;
	struct ttable *tt;
	uint32_t idx = 0;

	if (translator_family) {
		translator = yang_translator_find(translator_family);
		if (!translator) {
			vty_out(vty, "%% Module translator \"%s\" not found\n",
				translator_family);
			return CMD_WARNING;
		}
		ly_ctx = translator->ly_ctx;
	} else
		ly_ctx = ly_native_ctx;

	/* Prepare table. */
	tt = ttable_new(&ttable_styles[TTSTYLE_BLANK]);
	ttable_add_row(tt, "Module|Version|Revision|Flags|Namespace");
	tt->style.cell.rpad = 2;
	tt->style.corner = '+';
	ttable_restyle(tt);
	ttable_rowseps(tt, 0, BOTTOM, true, '-');

	while ((module = ly_ctx_get_module_iter(ly_ctx, &idx))) {
		char flags[8];

		snprintf(flags, sizeof(flags), "%c%c",
			 module->implemented ? 'I' : ' ',
			 LY_ARRAY_COUNT(module->deviated_by) ? 'D' : ' ');

		ttable_add_row(tt, "%s|%s|%s|%s|%s", module->name,
			       (module->parsed->version == 2) ? "1.1" : "1.0",
			       module->revision ? module->revision : "-", flags,
			       module->ns);
	}

	/* Dump the generated table. */
	if (tt->nrows > 1) {
		char *table;

		vty_out(vty, " Flags: I - Implemented, D - Deviated\n\n");

		table = ttable_dump(tt, "\n");
		vty_out(vty, "%s\n", table);
		XFREE(MTYPE_TMP_TTABLE, table);
	} else
		vty_out(vty, "No YANG modules to display.\n\n");

	ttable_del(tt);

	return CMD_SUCCESS;
}

/*
 * show_yang_module_detail（命令 "show yang module [translator] WORD <compiled|summary|tree|yang|yin>"）
 *   按指定输出格式打印单个 YANG 模块（编译后 YANG / 树 / YANG / YIN 等）；
 *   其中 summary 在 libyang v2 下不受支持。
 */
DEFPY(show_yang_module_detail, show_yang_module_detail_cmd,
      "show yang module\
          [module-translator WORD$translator_family]\
          WORD$module_name <compiled$compiled|summary|tree$tree|yang$yang|yin$yin>",
      SHOW_STR
      "YANG information\n"
      "Show loaded modules\n"
      "YANG module translator\n"
      "YANG module translator\n"
      "Module name\n"
      "Display compiled module in YANG format\n"
      "Display summary information about the module\n"
      "Display module in the tree (RFC 8340) format\n"
      "Display module in the YANG format\n"
      "Display module in the YIN format\n")
{
	struct ly_ctx *ly_ctx;
	struct yang_translator *translator = NULL;
	const struct lys_module *module;
	LYS_OUTFORMAT format;
	char *strp;

	if (translator_family) {
		translator = yang_translator_find(translator_family);
		if (!translator) {
			vty_out(vty, "%% Module translator \"%s\" not found\n",
				translator_family);
			return CMD_WARNING;
		}
		ly_ctx = translator->ly_ctx;
	} else
		ly_ctx = ly_native_ctx;

	module = ly_ctx_get_module_latest(ly_ctx, module_name);
	if (!module) {
		vty_out(vty, "%% Module \"%s\" not found\n", module_name);
		return CMD_WARNING;
	}

	if (yang)
		format = LYS_OUT_YANG;
	else if (yin)
		format = LYS_OUT_YIN;
	else if (compiled)
		format = LYS_OUT_YANG_COMPILED;
	else if (tree)
		format = LYS_OUT_TREE;
	else {
		vty_out(vty,
			"%% libyang v2 does not currently support summary\n");
		return CMD_WARNING;
	}

	if (lys_print_mem(&strp, module, format, 0) == 0) {
		vty_out(vty, "%s\n", strp);
		free(strp);
	} else {
		/* Unexpected. */
		vty_out(vty, "%% Error generating module information\n");
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

/*
 * show_yang_module_translator（命令 "show yang module-translator"）
 *   以表格列出所有已加载的 YANG 模块翻译器及其模块 / 偏差 / 覆盖率。
 */
DEFPY (show_yang_module_translator,
       show_yang_module_translator_cmd,
       "show yang module-translator",
       SHOW_STR
       "YANG information\n"
       "Show loaded YANG module translators\n")
{
	struct yang_translator *translator;
	struct ttable *tt;

	/* Prepare table. */
	tt = ttable_new(&ttable_styles[TTSTYLE_BLANK]);
	ttable_add_row(tt, "Family|Module|Deviations|Coverage (%%)");
	tt->style.cell.rpad = 2;
	tt->style.corner = '+';
	ttable_restyle(tt);
	ttable_rowseps(tt, 0, BOTTOM, true, '-');

	RB_FOREACH (translator, yang_translators, &yang_translators) {
		struct yang_tmodule *tmodule;
		struct listnode *ln;

		for (ALL_LIST_ELEMENTS_RO(translator->modules, ln, tmodule)) {
			ttable_add_row(tt, "%s|%s|%s|%.2f", translator->family,
				       tmodule->module->name,
				       tmodule->deviations->name,
				       tmodule->coverage);
		}
	}

	/* Dump the generated table. */
	if (tt->nrows > 1) {
		char *table;

		table = ttable_dump(tt, "\n");
		vty_out(vty, "%s\n", table);
		XFREE(MTYPE_TMP_TTABLE, table);
	} else
		vty_out(vty, "No YANG module translators to display.\n\n");

	ttable_del(tt);

	return CMD_SUCCESS;
}

#ifdef HAVE_CONFIG_ROLLBACKS
/*
 * nb_cli_rollback_configuration
 *   回滚到指定事务：从事务库载入该事务配置并作为一次提交写回运行配置。
 *   参数：transaction_id 目标事务号。
 *   返回：CMD_SUCCESS 或 CMD_WARNING（事务不存在 / 无变化 / 失败）。
 */
static int nb_cli_rollback_configuration(struct vty *vty,
					 uint32_t transaction_id)
{
	struct nb_context context = {};
	struct nb_config *candidate;
	char comment[80];
	char errmsg[BUFSIZ] = {0};
	int ret;

	candidate = nb_db_transaction_load(transaction_id);
	if (!candidate) {
		vty_out(vty, "%% Transaction %u does not exist.\n\n",
			transaction_id);
		return CMD_WARNING;
	}

	snprintf(comment, sizeof(comment), "Rollback to transaction %u",
		 transaction_id);

	context.client = NB_CLIENT_CLI;
	context.user = vty;
	ret = nb_candidate_commit(context, candidate, true, comment, NULL,
				  errmsg, sizeof(errmsg));
	nb_config_free(candidate);
	switch (ret) {
	case NB_OK:
		vty_out(vty,
			"%% Configuration was successfully rolled back.\n\n");
		/* Print warnings (if any). */
		if (strlen(errmsg) > 0)
			vty_out(vty, "%s\n", errmsg);
		return CMD_SUCCESS;
	case NB_ERR_NO_CHANGES:
		vty_out(vty,
			"%% Aborting - no configuration changes detected.\n\n");
		return CMD_WARNING;
	default:
		vty_out(vty, "%% Rollback failed.\n\n");
		vty_show_nb_errors(vty, ret, errmsg);
		return CMD_WARNING;
	}
}
#endif /* HAVE_CONFIG_ROLLBACKS */

/*
 * rollback_config（命令 "rollback configuration (1-4294967295)"）
 *   回滚到指定事务。未编译 rollback 支持时提示不可用。
 */
DEFPY (rollback_config,
       rollback_config_cmd,
       "rollback configuration (1-4294967295)$transaction_id",
       "Rollback to a previous state\n"
       "Running configuration\n"
       "Transaction ID\n")
{
#ifdef HAVE_CONFIG_ROLLBACKS
	return nb_cli_rollback_configuration(vty, transaction_id);
#else
	vty_out(vty,
		"%% FRR was compiled without --enable-config-rollbacks.\n\n");
	return CMD_SUCCESS;
#endif /* HAVE_CONFIG_ROLLBACKS */
}

/*
 * debug_nb（命令 "[no] debug northbound ..."）
 *   开关 northbound 各调试类别（callbacks / notifications / events / libyang）。
 *   不指定类别时一次性作用于全部；libyang 类别还会联动 yang_debugging_set。
 */
/* Debug CLI commands. */
DEFPY (debug_nb,
       debug_nb_cmd,
       "[no] debug northbound\
          [<\
	    callbacks$cbs [{configuration$cbs_cfg|state$cbs_state|rpc$cbs_rpc|notify$cbs_notify}]\
	    |notifications$notifications\
	    |events$events\
	    |libyang$libyang\
          >]",
       NO_STR
       DEBUG_STR
       "Northbound debugging\n"
       "Callbacks\n"
       "Configuration\n"
       "State\n"
       "RPC\n"
       "Notifications\n"
       "Notifications\n"
       "Events\n"
       "libyang debugging\n")
{
	uint32_t mode = DEBUG_NODE2MODE(vty->node);
	bool all = false;

	/* no specific debug --> act on all of them */
	if (strmatch(argv[argc - 1]->text, "northbound"))
		all = true;

	if (cbs || all) {
		bool none = (!cbs_cfg && !cbs_state && !cbs_rpc && !cbs_notify);

		if (none || cbs_cfg)
			DEBUG_MODE_SET(&nb_dbg_cbs_config, mode, !no);
		if (none || cbs_state)
			DEBUG_MODE_SET(&nb_dbg_cbs_state, mode, !no);
		if (none || cbs_rpc)
			DEBUG_MODE_SET(&nb_dbg_cbs_rpc, mode, !no);
		if (none || cbs_notify)
			DEBUG_MODE_SET(&nb_dbg_cbs_notify, mode, !no);
	}
	if (notifications || all)
		DEBUG_MODE_SET(&nb_dbg_notif, mode, !no);
	if (events || all)
		DEBUG_MODE_SET(&nb_dbg_events, mode, !no);
	if (libyang || all) {
		DEBUG_MODE_SET(&nb_dbg_libyang, mode, !no);
		yang_debugging_set(!no);
	}

	return CMD_SUCCESS;
}

/*
 * nb_cli_install_default
 *   向指定节点安装 northbound 默认命令：始终安装裸 "show"；事务模式下再安装
 *   commit / update / discard 与 show configuration 系列命令。
 */
void nb_cli_install_default(int node)
{
	_install_element(node, &show_config_candidate_section_cmd);

	if (frr_get_cli_mode() != FRR_CLI_TRANSACTIONAL)
		return;

	_install_element(node, &config_commit_cmd);
	_install_element(node, &config_commit_comment_cmd);
	_install_element(node, &config_commit_check_cmd);
	_install_element(node, &config_update_cmd);
	_install_element(node, &config_discard_cmd);
	_install_element(node, &show_config_running_cmd);
	_install_element(node, &show_config_candidate_cmd);
	_install_element(node, &show_config_compare_cmd);
	_install_element(node, &show_config_transaction_cmd);
}

/*
 * yang_module_autocomplete
 *   命令补全回调：把本地 ctx 及所有模块翻译器 ctx 中的模块名加入补全候选。
 */
/* YANG module autocomplete. */
static void yang_module_autocomplete(vector comps, struct cmd_token *token)
{
	const struct lys_module *module;
	struct yang_translator *module_tr;
	uint32_t idx;

	idx = 0;
	while ((module = ly_ctx_get_module_iter(ly_native_ctx, &idx)))
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, module->name));

	RB_FOREACH (module_tr, yang_translators, &yang_translators) {
		idx = 0;
		while ((module = ly_ctx_get_module_iter(module_tr->ly_ctx,
							&idx)))
			vector_set(comps,
				   XSTRDUP(MTYPE_COMPLETION, module->name));
	}
}

/*
 * yang_translator_autocomplete
 *   命令补全回调：把所有已加载模块翻译器的 family 名加入补全候选。
 */
/* YANG module translator autocomplete. */
static void yang_translator_autocomplete(vector comps, struct cmd_token *token)
{
	struct yang_translator *module_tr;

	RB_FOREACH (module_tr, yang_translators, &yang_translators)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, module_tr->family));
}

/*
 * yang_var_handlers
 *   命令变量补全表：把 "module_name" 与 "translator_family" 两个变量名绑定到
 *   上面的补全回调（末尾以空项收尾）。
 */
static const struct cmd_variable_handler yang_var_handlers[] = {
	{.varname = "module_name", .completions = yang_module_autocomplete},
	{.varname = "translator_family",
	 .completions = yang_translator_autocomplete},
	{.completions = NULL}};

/*
 * nb_cli_init
 *   模块初始化：记录事件循环、创建共享候选配置、注册各调试开关、安装命令，
 *   并注册命令变量补全处理器。
 */
void nb_cli_init(struct event_loop *tm)
{
	master = tm;

	/* Initialize the shared candidate configuration. */
	vty_shared_candidate_config = nb_config_new(NULL);

	debug_install(&nb_dbg_cbs_config);
	debug_install(&nb_dbg_cbs_state);
	debug_install(&nb_dbg_cbs_rpc);
	debug_install(&nb_dbg_cbs_notify);
	debug_install(&nb_dbg_notif);
	debug_install(&nb_dbg_events);
	debug_install(&nb_dbg_libyang);

	install_element(ENABLE_NODE, &debug_nb_cmd);
	install_element(CONFIG_NODE, &debug_nb_cmd);

	/* Install commands specific to the transaction-base mode. */
	if (frr_get_cli_mode() == FRR_CLI_TRANSACTIONAL) {
		install_element(ENABLE_NODE, &config_exclusive_cmd);
		install_element(ENABLE_NODE, &config_private_cmd);
		install_element(ENABLE_NODE,
				&show_config_compare_without_candidate_cmd);
		install_element(ENABLE_NODE, &show_config_transaction_cmd);
		install_element(ENABLE_NODE, &rollback_config_cmd);
		install_element(ENABLE_NODE, &clear_config_transactions_cmd);

		install_element(CONFIG_NODE, &config_load_cmd);
		install_element(CONFIG_NODE,
				&config_database_max_transactions_cmd);
	}

	/* Other commands. */
	install_element(ENABLE_NODE, &show_config_running_cmd);
	install_element(CONFIG_NODE, &yang_module_translator_load_cmd);
	install_element(CONFIG_NODE, &yang_module_translator_unload_cmd);
	install_element(ENABLE_NODE, &show_yang_operational_data_cmd);
	install_element(ENABLE_NODE, &show_yang_module_cmd);
	install_element(ENABLE_NODE, &show_yang_module_detail_cmd);
	install_element(ENABLE_NODE, &show_yang_module_translator_cmd);
	cmd_variable_handler_register(yang_var_handlers);
}

/*
 * nb_cli_terminate
 *   模块退出清理：释放共享候选配置。
 */
void nb_cli_terminate(void)
{
	nb_config_free(vty_shared_candidate_config);
}
