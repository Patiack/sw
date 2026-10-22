// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * ============================================================================
 * 文件导读：northbound_cli.h —— 旧式 CLI 命令与 YANG 之间的桥（对外接口）
 * ============================================================================
 *
 * 1) 作用
 *    FRR 的每条配置命令最终都要落到 YANG 数据模型上。本头文件声明了
 *    northbound_cli.c 对外暴露的接口，供各守护进程（bgpd、zebra、ospfd ...）
 *    的 CLI 处理函数调用，串起"命令行 -> YANG 路径 -> northbound 回调"的链路：
 *      命令解析 -> nb_cli_enqueue_change() 把改动压入 vty 上的候选队列
 *               -> nb_cli_apply_changes() 统一校验并提交
 *               -> northbound 层据此触发各模块的 create / modify / destroy 回调。
 *
 * 2) 关键接口分组
 *    - 配置修改：nb_cli_enqueue_change / nb_cli_apply_changes /
 *      nb_cli_apply_changes_clear_pending。
 *    - RPC 与 Action：nb_cli_rpc_enqueue / nb_cli_rpc。
 *    - 配置回显：nb_cli_show_dnode_cmds。
 *    - 提交管理：nb_cli_pending_commit_check（以及文件末尾的内部函数原型）。
 *
 * 3) 关键全局量
 *    vty_shared_candidate_config：进程内共享的候选配置。
 *
 * 4) 阅读顺序建议
 *    先理解 nb_cli_enqueue_change 与 nb_cli_apply_changes 的配对关系，再看
 *    RPC / 回显接口，最后看末尾的内部函数原型。
 */

#ifndef _FRR_NORTHBOUND_CLI_H_
#define _FRR_NORTHBOUND_CLI_H_

#include "northbound.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * enum nb_cfg_format
 *   配置回显时可选的输出格式：CLI 命令、JSON、XML，与 vty 上
 *   "show running-config" 的输出选项一一对应。
 */
/* Possible formats in which a configuration can be displayed. */
enum nb_cfg_format {
	NB_CFG_FMT_CMDS = 0,
	NB_CFG_FMT_JSON,
	NB_CFG_FMT_XML,
};

/*
 * NB_CMD_BATCH_SIZE
 *   一次批量提交时允许包含的最大配置命令条数，用于限制单次事务的规模。
 */
/* Maximum config commands in a batch*/
#define NB_CMD_BATCH_SIZE 5000

/*
 * vty_shared_candidate_config
 *   进程级共享的候选配置对象。某些 vty 关联到它而非各自的候选配置，用于在
 *   多个"配置模式"会话之间共享待提交的改动。
 */
extern struct nb_config *vty_shared_candidate_config;

/*
 * nb_cli_enqueue_change
 *   把一条配置改动（xpath + 操作 + 新值）压入 vty 关联的候选队列，等待稍后由
 *   nb_cli_apply_changes 统一应用。
 *   参数：xpath 为绝对或相对路径；operation 取 NB_OP_CREATE / NB_OP_MODIFY /
 *   NB_OP_DESTROY 三者之一；value 为可选新值（无类型数据或恢复默认时传 NULL）。
 */
/*
 * Enqueue change to be applied in the candidate configuration.
 *
 * vty
 *    The vty context.
 *
 * xpath
 *    XPath (absolute or relative) of the configuration option being edited.
 *
 * operation
 *    Operation to apply (either NB_OP_CREATE, NB_OP_MODIFY or NB_OP_DESTROY).
 *
 * value
 *    New value of the configuration option. Should be NULL for typeless YANG
 *    data (e.g. presence-containers). For convenience, NULL can also be used
 *    to restore a leaf to its default value.
 */
extern void nb_cli_enqueue_change(struct vty *vty, const char *xpath,
				  enum nb_operation operation,
				  const char *value);

/*
 * nb_cli_apply_changes_clear_pending
 *   立即应用已入队的改动，不做批量，并把当前挂起的提交一并应用。
 *   参数：xpath_base_fmt 为可选前缀（printf 风格），会拼接到所有入队改动之前。
 *   返回：成功 CMD_SUCCESS，否则 CMD_WARNING_CONFIG_FAILED。
 */
/*
 * Apply enqueued changes to the candidate configuration, do not batch,
 * and apply any pending commits along with the currently enqueued.
 *
 * vty
 *    The vty context.
 *
 * xpath_base_fmt
 *    Prepend the given XPath (absolute or relative) to all enqueued
 *    configuration changes. This is an optional parameter.
 *
 * Returns:
 *    CMD_SUCCESS on success, CMD_WARNING_CONFIG_FAILED otherwise.
 */
extern int nb_cli_apply_changes_clear_pending(struct vty *vty,
					      const char *xpath_base_fmt, ...)
	PRINTFRR(2, 3);

/*
 * nb_cli_apply_changes
 *   应用已入队的改动，但可能不立即生效，而是先把它们加入一个挂起队列（批量）。
 *   参数：xpath_base_fmt 同上，为可选前缀。
 *   返回：成功 CMD_SUCCESS，否则 CMD_WARNING_CONFIG_FAILED。
 *   坑：批量模式下真正生效的时机取决于后续提交，不要假设调用即生效。
 */
/*
 * Apply enqueued changes to the candidate configuration, this function
 * may not immediately apply the changes, instead adding them to a pending
 * queue.
 *
 * vty
 *    The vty context.
 *
 * xpath_base_fmt
 *    Prepend the given XPath (absolute or relative) to all enqueued
 *    configuration changes. This is an optional parameter.
 *
 * Returns:
 *    CMD_SUCCESS on success, CMD_WARNING_CONFIG_FAILED otherwise.
 */
extern int nb_cli_apply_changes(struct vty *vty, const char *xpath_base_fmt,
				...) PRINTFRR(2, 3);

/*
 * nb_cli_rpc_enqueue
 *   为一次 RPC 或 Action 添加一个输入子节点（相对 input 容器的 xpath + 值）。
 *   返回：int 状态码。
 */
/*
 * Add an input child node for an RPC or an action.
 *
 * vty
 *    The vty context.
 *
 * xpath
 *    XPath of the child being added, relative to the input container.
 *
 * value
 *    Value of the child being added. Can be NULL for containers and leafs of
 *    type 'empty'.
 */
extern int nb_cli_rpc_enqueue(struct vty *vty, const char *xpath,
			      const char *value);

/*
 * nb_cli_rpc
 *   用已入队的输入参数执行一个 YANG RPC 或 Action。
 *   参数：xpath 为 RPC / Action 节点路径；output_p 用于接收输出数据树，调用方
 *   负责释放，不关心输出时可传 NULL。
 *   返回：成功 CMD_SUCCESS，否则 CMD_WARNING。
 */
/*
 * Execute a YANG RPC or Action using the enqueued input parameters.
 *
 * vty
 *    The vty terminal to dump any error.
 *
 * xpath
 *    XPath of the YANG RPC or Action node.
 *
 * output_p
 *    A pointer to the libyang data node that will hold the output data tree.
 *    It can be set to NULL if the caller is not interested in processing the
 *    output. The caller is responsible for freeing the output data tree.
 *
 * Returns:
 *    CMD_SUCCESS on success, CMD_WARNING otherwise.
 */
extern int nb_cli_rpc(struct vty *vty, const char *xpath,
		      struct lyd_node **output_p);

/*
 * nb_cli_show_dnode_cmds
 *   把某个 YANG 数据节点以 CLI 命令的形式回显到 vty。
 *   参数：show_defaults 控制是否显示默认值。
 */
/*
 * Show CLI commands associated to the given YANG data node.
 *
 * vty
 *    The vty terminal to dump the configuration to.
 *
 * dnode
 *    libyang data node that should be shown in the form of CLI commands.
 *
 * show_defaults
 *    Specify whether to display default configuration values or not.
 */
extern void nb_cli_show_dnode_cmds(struct vty *vty,
				   const struct lyd_node *dnode,
				   bool show_defaults);

/*
 * nb_cli_pending_commit_check
 *   若存在挂起的提交则执行之。
 *   返回：成功（或本无挂起）CMD_SUCCESS，否则 CMD_WARNING_CONFIG_FAILED。
 */
/*
 * Perform pending commit, if any.
 *
 * vty
 *    The vty context.
 *
 * Returns
 *    CMD_SUCCESS on success (or no pending), CMD_WARNING_CONFIG_FAILED
 *    otherwise.
 */
extern int nb_cli_pending_commit_check(struct vty *vty);

/*
 * 以下为内部（非稳定）接口的原型：配置回显准备、confirmed-commit 的清理与
 * 回滚、默认命令安装，以及本模块的初始化 / 终止。
 */
/* Prototypes of internal functions. */
/*
 * nb_cli_show_config_prepare
 *   为回显配置做准备（补齐默认节点等）；实现见 northbound_cli.c。
 */
extern void nb_cli_show_config_prepare(struct nb_config *config,
				       bool with_defaults);
/*
 * nb_cli_confirmed_commit_clean
 *   清理 confirmed-commit 状态（取消超时定时器、释放回滚快照）。
 */
extern void nb_cli_confirmed_commit_clean(struct vty *vty);
/*
 * nb_cli_confirmed_commit_rollback
 *   执行 confirmed-commit 超时回滚，返回提交结果码。
 */
extern int nb_cli_confirmed_commit_rollback(struct vty *vty);
/*
 * nb_cli_install_default
 *   向指定 CLI 节点安装 northbound 的默认命令集。
 */
extern void nb_cli_install_default(int node);
/*
 * nb_cli_init
 *   初始化 northbound CLI 模块（记录事件循环、创建共享候选配置、安装命令）。
 */
extern void nb_cli_init(struct event_loop *tm);
/*
 * nb_cli_terminate
 *   终止 northbound CLI 模块，释放其占用的资源。
 */
extern void nb_cli_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_NORTHBOUND_CLI_H_ */
