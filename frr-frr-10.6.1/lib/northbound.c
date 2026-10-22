// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

#include <zebra.h>

#include "darr.h"
#include "host_nb.h"
#include "libfrr.h"
#include "log.h"
#include "lib_errors.h"
#include "hash.h"
#include "command.h"
#include "debug.h"
#include "db.h"
#include "frr_pthread.h"
#include "northbound.h"
#include "northbound_cli.h"
#include "northbound_db.h"
#include "frrstr.h"

/*
 * ============================================================================
 * 文件导读：FRR 北向接口（northbound）核心实现
 * ============================================================================
 *
 * 本文件是 YANG 北向框架的主体，把 libyang 配置树与各守护进程的内部数据
 * 结构对接起来。整体流程大致为：
 *   1) 注册：守护进程提供一组 frr_yang_module_info（含每个节点的 xpath 与
 *      回调），nb_nodes_create() 为所有 schema 节点建立 struct nb_node；
 *   2) 编辑：前端（CLI / sysrepo / mgmtd）在 candidate 配置上通过
 *      nb_candidate_edit*() 施加变更；
 *   3) 提交：nb_candidate_commit_prepare() 做校验（YANG 语法语义加各节点回调
 *      校验）并准备资源，成功得到 nb_transaction；随后由
 *      nb_candidate_commit_apply() 应用并写入 running，或由
 *      nb_candidate_commit_abort() 回滚；
 *   4) 通知：变更后经通知钩子把状态变化广播出去。
 *
 * 事务的四个阶段（enum nb_event）顺序为：
 *   NB_EV_VALIDATE 到 NB_EV_PREPARE 到 NB_EV_APPLY，任一步失败转 NB_EV_ABORT。
 * 每个配置变更（struct nb_config_change）都会在这四个阶段被回调处理，回调
 * 依据 event 决定具体行为。
 *
 * 阅读重点：nb_transaction_process()（事务驱动核心）、nb_callback_*() 回调
 * 包装族、nb_node_*()（节点构建与查找）、nb_config_*()（配置增删改），以及
 * nb_candidate_commit*()（两阶段提交）。
 * ============================================================================
 */

/*
 * 北向相关的内存类型（MTYPE）定义，供 FRR 内存跟踪使用，便于统计与排查泄漏。
 */
DEFINE_MTYPE_STATIC(LIB, NB_NODE, "Northbound Node");
DEFINE_MTYPE_STATIC(LIB, NB_CONFIG, "Northbound Configuration");
DEFINE_MTYPE_STATIC(LIB, NB_CONFIG_ENTRY, "Northbound Configuration Entry");

/*
 * running 配置：真正生效的配置树。不要直接修改，必须通过配置事务提交来改变。
 */
/* Running configuration - shouldn't be modified directly. */
struct nb_config *running_config;

/*
 * 用户指针哈希表：把配置条目的 xpath 映射到其关联的用户指针（见
 * nb_running_set_entry 与 nb_running_get_entry）。回调据此从配置树节点反查
 * 内部数据结构。
 */
/* Hash table of user pointers associated with configuration entries. */
static struct hash *running_config_entries;

/*
 * running 配置的管理锁，用于实现“锁定 / 解锁 running”语义：mtx 保护本结构，
 * locked 表示是否已锁定，owner_client 与 owner_user 记录持锁者以便判断能否
 * 解锁。注意其类型是匿名 struct（无 tag）。
 */
/* Management lock for the running configuration. */
static struct {
	/* Mutex protecting this structure. */
	pthread_mutex_t mtx;

	/* Actual lock. */
	bool locked;

	/* Northbound client who owns this lock. */
	enum nb_client owner_client;

	/* Northbound user who owns this lock. */
	const void *owner_user;
} running_config_mgmt_lock;

/*
 * 开关：为 true 时把每次配置事务记录到事务日志（db）。
 */
/* Knob to record config transaction */
static bool nb_db_enabled;
/*
 * 全局标志：防止多个配置事务并发执行（同一时刻只允许一个事务）。为 true
 * 表示当前已有事务在进行中。
 */
/*
 * Global lock used to prevent multiple configuration transactions from
 * happening concurrently.
 */
static bool transaction_in_progress;

/*
 * 本文件内部使用的前置声明：前置校验回调包装、配置回调包装、事务的创建 /
 * 释放 / 处理，以及 apply 结束阶段的收尾。具体定义见文件后面。
 */
static int nb_callback_pre_validate(struct nb_context *context,
				    const struct nb_node *nb_node,
				    const struct lyd_node *dnode, char *errmsg,
				    size_t errmsg_len);
static int nb_callback_configuration(struct nb_context *context,
				     const enum nb_event event,
				     struct nb_config_change *change,
				     char *errmsg, size_t errmsg_len);
static struct nb_transaction *
nb_transaction_new(struct nb_context context, struct nb_config *config,
		   struct nb_config_cbs *changes, const char *comment,
		   char *errmsg, size_t errmsg_len);
static void nb_transaction_free(struct nb_transaction *transaction);
static int nb_transaction_process(enum nb_event event,
				  struct nb_transaction *transaction,
				  char *errmsg, size_t errmsg_len);
static void nb_transaction_apply_finish(struct nb_transaction *transaction,
					char *errmsg, size_t errmsg_len);

/*
 * 遍历回调（供 yang_snodes_iterate_subtree 使用）：判断某个 schema 子树是否
 * “只含配置数据”。一旦遇到状态（LYS_CONFIG_R）节点，就把 *config_only 置为
 * false 并停止遍历。用于为容器 / list 设置 F_NB_NODE_CONFIG_ONLY 标志。
 */
static int nb_node_check_config_only(const struct lysc_node *snode, void *arg)
{
	bool *config_only = arg;

	if (CHECK_FLAG(snode->flags, LYS_CONFIG_R)) {
		*config_only = false;
		return YANG_ITER_STOP;
	}

	return YANG_ITER_CONTINUE;
}

/*
 * 遍历回调：为每个 schema 节点创建一个 struct nb_node，主要做四件事：
 *   1) 计算该节点的数据路径并存入 nb_node->xpath；
 *   2) 建立与父节点、父 list 的指针关系（parent / parent_list）；
 *   3) 设置标志：容器 / list 是否只含配置、list 是否无 key、模块是否忽略配置
 *      回调、模块是否使用整树读取；
 *   4) 把 nb_node 挂到 snode->priv 上（同时在 nb_node->snode 记回指针）。
 * 注意 assert(snode->priv == NULL)：要求每个 schema 节点只被处理一次。arg 为
 * 当前正在处理的 frr_yang_module_info。
 */
static int nb_node_new_cb(const struct lysc_node *snode, void *arg)
{
	struct nb_node *nb_node;
	struct lysc_node *sparent, *sparent_list;
	struct frr_yang_module_info *module;

	module = (struct frr_yang_module_info *)arg;
	nb_node = XCALLOC(MTYPE_NB_NODE, sizeof(*nb_node));
	yang_snode_get_path(snode, YANG_PATH_DATA, nb_node->xpath,
			    sizeof(nb_node->xpath));
	nb_node->priority = NB_DFLT_PRIORITY;
	sparent = yang_snode_real_parent(snode);
	if (sparent)
		nb_node->parent = sparent->priv;
	sparent_list = yang_snode_parent_list(snode);
	if (sparent_list)
		nb_node->parent_list = sparent_list->priv;

	/* Set flags. */
	if (CHECK_FLAG(snode->nodetype, LYS_CONTAINER | LYS_LIST)) {
		bool config_only = true;

		(void)yang_snodes_iterate_subtree(snode, NULL,
						  nb_node_check_config_only, 0,
						  &config_only);
		if (config_only)
			SET_FLAG(nb_node->flags, F_NB_NODE_CONFIG_ONLY);
	}
	if (CHECK_FLAG(snode->nodetype, LYS_LIST)) {
		if (yang_snode_num_keys(snode) == 0)
			SET_FLAG(nb_node->flags, F_NB_NODE_KEYLESS_LIST);
	}

	/*
	 * Link the northbound node and the libyang schema node with one
	 * another.
	 */
	nb_node->snode = snode;
	assert(snode->priv == NULL);
	((struct lysc_node *)snode)->priv = nb_node;

	if (module && module->ignore_cfg_cbs)
		SET_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS);
	if (module && module->get_tree_locked)
		SET_FLAG(nb_node->flags, F_NB_NODE_HAS_GET_TREE);

	return YANG_ITER_CONTINUE;
}

/*
 * 遍历回调：删除某个 schema 节点关联的 nb_node，并把 snode->priv 置回 NULL。
 * 与 nb_node_new_cb 成对使用。
 */
static int nb_node_del_cb(const struct lysc_node *snode, void *arg)
{
	struct nb_node *nb_node;

	nb_node = snode->priv;
	if (nb_node) {
		((struct lysc_node *)snode)->priv = NULL;
		XFREE(MTYPE_NB_NODE, nb_node);
	}

	return YANG_ITER_CONTINUE;
}

/*
 * 为所有已加载 YANG 模块的 schema 节点创建 nb_node（遍历全 schema 调用
 * nb_node_new_cb）。通常在模块加载后调用。
 */
void nb_nodes_create(void)
{
	yang_snodes_iterate(NULL, nb_node_new_cb, 0, NULL);
}

/*
 * 删除所有 schema 节点关联的 nb_node（遍历全 schema 调用 nb_node_del_cb）。
 */
void nb_nodes_delete(void)
{
	yang_snodes_iterate(NULL, nb_node_del_cb, 0, NULL);
}

/*
 * 按数据路径在原生 libyang 上下文中查找 schema 节点，再从其 priv 取出 nb_node。
 * 为避免“未找到”时 libyang 打印错误日志，这里临时关闭 libyang 日志
 * （ly_temp_log_options），查找完再恢复。未找到返回 NULL。
 */
struct nb_node *nb_node_find(const char *path)
{
	const struct lysc_node *snode;
	uint32_t llopts = 0;

	/*
	 * Use libyang to find the schema node associated to the path and get
	 * the northbound node from there (snode private pointer). We need to
	 * disable logging temporarily to avoid libyang from logging an error
	 * message when the node is not found.
	 */
	ly_temp_log_options(&llopts);

	snode = yang_find_snode(ly_native_ctx, path, 0);

	ly_temp_log_options(NULL);
	if (!snode)
		return NULL;

	return snode->priv;
}

/*
 * 把复杂 xpath 解析为多个 schema 节点，再逐个取出其 priv（nb_node），返回一个
 * 动态数组（darr，元素为 struct nb_node *）。中间的 snodes 数组用完即释放；
 * 返回的 nb_nodes 由调用方负责释放。解析失败返回 NULL。
 */
struct nb_node **nb_nodes_find(const char *xpath)
{
	const struct lysc_node **snodes = NULL;
	struct nb_node **nb_nodes = NULL;
	bool simple;
	LY_ERR err;
	uint i;

	err = yang_resolve_snode_xpath(ly_native_ctx, xpath, &snodes, &simple);
	if (err)
		return NULL;

	darr_ensure_i(nb_nodes, darr_lasti(snodes));
	darr_foreach_i (snodes, i)
		nb_nodes[i] = snodes[i]->priv;
	darr_free(snodes);
	return nb_nodes;
}


/*
 * 为依赖关系注册回调：分别找到“被依赖”节点（dependency_xpath）与“依赖它”的
 * 节点（dependant_xpath），把 cbs 中的两个函数指针分别挂到两者上。若任一路径
 * 找不到则直接返回、不做任何修改。
 */
void nb_node_set_dependency_cbs(const char *dependency_xpath,
				const char *dependant_xpath,
				struct nb_dependency_callbacks *cbs)
{
	struct nb_node *dependency = nb_node_find(dependency_xpath);
	struct nb_node *dependant = nb_node_find(dependant_xpath);

	if (!dependency || !dependant)
		return;

	dependency->dep_cbs.get_dependant_xpath = cbs->get_dependant_xpath;
	dependant->dep_cbs.get_dependency_xpath = cbs->get_dependency_xpath;
}

/*
 * 判断某 nb_node 是否设置了依赖回调（以 get_dependency_xpath 是否非空为准）。
 */
bool nb_node_has_dependency(struct nb_node *node)
{
	return node->dep_cbs.get_dependency_xpath != NULL;
}

/*
 * 校验单个回调是否合规：
 *   - 若某操作对该 schema 节点无效（nb_cb_operation_is_valid 为 false）却实现了
 *     回调，除运维数据读取类回调（GET_NEXT / GET_KEYS / LIST_ENTRY_DONE /
 *     LOOKUP_ENTRY）外，记录“多余回调”警告。这四类例外是因为 rw list 是否需要
 *     它们取决于是否被其它模块 augment，不能一概而论。
 *   - 若该操作有效、回调又必需（optional 为 false）却未实现，记录“缺失回调”
 *     错误并返回 1（计为一次错误）。
 * 返回 0 表示无致命问题。
 */
static int nb_node_validate_cb(const struct nb_node *nb_node,
			       enum nb_cb_operation operation,
			       int callback_implemented, bool optional)
{
	bool valid;

	valid = nb_cb_operation_is_valid(operation, nb_node->snode);

	/*
	 * Add an exception for operational data callbacks. A rw list usually
	 * doesn't need any associated operational data callbacks. But if this
	 * rw list is augmented by another module which adds state nodes under
	 * it, then this list will need to have the 'get_next()', 'get_keys()'
	 * and 'lookup_entry()' callbacks. As such, never log a warning when
	 * these callbacks are implemented when they are not needed, since this
	 * depends on context (e.g. some daemons might augment "frr-interface"
	 * while others don't).
	 */
	if (!valid && callback_implemented && operation != NB_CB_GET_NEXT &&
	    operation != NB_CB_GET_KEYS && operation != NB_CB_LIST_ENTRY_DONE &&
	    operation != NB_CB_LOOKUP_ENTRY)
		flog_warn(EC_LIB_NB_CB_UNNEEDED,
			  "unneeded '%s' callback for '%s'",
			  nb_cb_operation_name(operation), nb_node->xpath);

	if (!optional && valid && !callback_implemented) {
		flog_err(EC_LIB_NB_CB_MISSING, "missing '%s' callback for '%s'",
			 nb_cb_operation_name(operation), nb_node->xpath);
		return 1;
	}

	return 0;
}

/*
 * 检查给定 nb_node 是否实现了所有必需的回调：逐项调用 nb_node_validate_cb 并
 * 累加错误数。若设置了 F_NB_NODE_IGNORE_CFG_CBS（忽略配置回调）则直接返回 0。
 * state_optional 表示状态类回调是否可省（取决于是否配置了整树读取）。返回错误计数。
 */
/*
 * Check if the required callbacks were implemented for the given northbound
 * node.
 */
static unsigned int nb_node_validate_cbs(const struct nb_node *nb_node)

{
	unsigned int error = 0;
	bool state_optional = CHECK_FLAG(nb_node->flags, F_NB_NODE_HAS_GET_TREE);

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return error;

	error += nb_node_validate_cb(nb_node, NB_CB_CREATE,
				     !!nb_node->cbs.create, false);
	error += nb_node_validate_cb(nb_node, NB_CB_MODIFY,
				     !!nb_node->cbs.modify, false);
	error += nb_node_validate_cb(nb_node, NB_CB_DESTROY,
				     !!nb_node->cbs.destroy, false);
	error += nb_node_validate_cb(nb_node, NB_CB_MOVE, !!nb_node->cbs.move,
				     false);
	error += nb_node_validate_cb(nb_node, NB_CB_PRE_VALIDATE,
				     !!nb_node->cbs.pre_validate, true);
	error += nb_node_validate_cb(nb_node, NB_CB_APPLY_FINISH,
				     !!nb_node->cbs.apply_finish, true);
	error += nb_node_validate_cb(nb_node, NB_CB_GET_ELEM,
				     (nb_node->cbs.get_elem || nb_node->cbs.get), state_optional);
	error += nb_node_validate_cb(nb_node, NB_CB_GET_NEXT,
				     (nb_node->cbs.get_next ||
				      (nb_node->snode->nodetype == LYS_LEAFLIST && nb_node->cbs.get)),
				     state_optional);
	error += nb_node_validate_cb(nb_node, NB_CB_GET_KEYS, !!nb_node->cbs.get_keys,
				     state_optional);
	error += nb_node_validate_cb(nb_node, NB_CB_LIST_ENTRY_DONE, !!nb_node->cbs.list_entry_done,
				     true);
	error += nb_node_validate_cb(nb_node, NB_CB_LOOKUP_ENTRY, !!nb_node->cbs.lookup_entry,
				     state_optional);
	error += nb_node_validate_cb(nb_node, NB_CB_RPC, !!nb_node->cbs.rpc,
				     false);
	error += nb_node_validate_cb(nb_node, NB_CB_NOTIFY,
				     !!nb_node->cbs.notify, true);

	return error;
}

/*
 * 校验节点优先级：顶层节点可任意；非顶层节点的优先级不能大于其父节点（数值越
 * 小优先级越高），否则报错并返回 1。用于保证处理顺序的合理性。
 */
static unsigned int nb_node_validate_priority(const struct nb_node *nb_node)
{
	/* Top-level nodes can have any priority. */
	if (!nb_node->parent)
		return 0;

	if (nb_node->priority < nb_node->parent->priority) {
		flog_err(EC_LIB_NB_CB_INVALID_PRIO,
			 "node has higher priority than its parent [xpath %s]",
			 nb_node->xpath);
		return 1;
	}

	return 0;
}

/*
 * 遍历回调：对每个 schema 节点校验其回调与优先级，把错误数累加到 *errors。
 * 返回 YANG_ITER_CONTINUE 继续遍历。
 */
static int nb_node_validate(const struct lysc_node *snode, void *arg)
{
	struct nb_node *nb_node = snode->priv;
	unsigned int *errors = arg;

	/* Validate callbacks and priority. */
	if (nb_node) {
		*errors += nb_node_validate_cbs(nb_node);
		*errors += nb_node_validate_priority(nb_node);
	}

	return YANG_ITER_CONTINUE;
}

/*
 * 新建一份北向配置。dnode 若给定则直接采用；否则新建一棵空的原生配置树。
 * version 初始化为 0。返回新建的 nb_config。
 */
struct nb_config *nb_config_new(struct lyd_node *dnode)
{
	struct nb_config *config;

	config = XCALLOC(MTYPE_NB_CONFIG, sizeof(*config));
	if (dnode)
		config->dnode = dnode;
	else
		config->dnode = yang_dnode_new(ly_native_ctx, true);
	config->version = 0;

	return config;
}

/*
 * 释放一份北向配置：先释放其数据树（若存在），再释放 config 结构本身。
 */
void nb_config_free(struct nb_config *config)
{
	if (config->dnode)
		yang_dnode_free(config->dnode);

	XFREE(MTYPE_NB_CONFIG, config);
}

/*
 * 复制一份北向配置（深拷贝数据树），并复制 version。返回副本。
 */
struct nb_config *nb_config_dup(const struct nb_config *config)
{
	struct nb_config *dup;

	dup = XCALLOC(MTYPE_NB_CONFIG, sizeof(*dup));
	dup->dnode = yang_dnode_dup(config->dnode);
	dup->version = config->version;

	return dup;
}

/*
 * 把 config_src 合并进 config_dst（用 lyd_merge_siblings 合并数据树）。
 * 若 preserve_source 为 false，合并后释放 config_src。合并失败会记录警告。
 * 返回 NB_OK 或 NB_ERR。
 */
int nb_config_merge(struct nb_config *config_dst, struct nb_config *config_src,
		    bool preserve_source)
{
	int ret;

	ret = lyd_merge_siblings(&config_dst->dnode, config_src->dnode, 0);
	if (ret != 0)
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_merge() failed", __func__);

	if (!preserve_source)
		nb_config_free(config_src);

	return (ret == 0) ? NB_OK : NB_ERR;
}

/*
 * 用 config_src 替换 config_dst：更新版本号（源版本非 0 时），并替换数据树。
 * 若 preserve_source 为 true 则复制源数据树（源保留）；否则直接把源数据树
 * 移交给 dst 并释放源配置结构。注意 dst 原数据树会被先释放。
 */
void nb_config_replace(struct nb_config *config_dst,
		       struct nb_config *config_src, bool preserve_source)
{
	/* Update version. */
	if (config_src->version != 0)
		config_dst->version = config_src->version;

	/* Update dnode. */
	if (config_dst->dnode)
		yang_dnode_free(config_dst->dnode);
	if (preserve_source) {
		config_dst->dnode = yang_dnode_dup(config_src->dnode);
	} else {
		config_dst->dnode = config_src->dnode;
		config_src->dnode = NULL;
		nb_config_free(config_src);
	}
}

/*
 * 变更记录（nb_config_cb）的排序函数，供红黑树使用。排序规则依次为：
 *   1) 按操作类型：所有删除（DESTROY）排在前面——因为 choice 的 case 切换时
 *      必须先删旧 case 再建新 case；
 *   2) 按优先级：数值小者在前；但删除操作要反序，以便先删“依赖者”再删
 *      “被依赖者”；
 *   3) 按序号 seq（保持 libyang 给出的变更顺序）；
 *   4) apply_finish 回调的 seq 都是 0，此时改用 dnode 指针比较（同优先级下
 *      顺序无所谓）。
 * 返回值遵循比较函数约定（负 / 零 / 正，即 -1 / 0 / 1）。
 */
/* Generate the nb_config_cbs tree. */
static inline int nb_config_cb_compare(const struct nb_config_cb *a,
				       const struct nb_config_cb *b)
{
	bool a_destroy = a->operation == NB_CB_DESTROY;
	bool b_destroy = b->operation == NB_CB_DESTROY;

	/*
	 * Sort by operation first. All "destroys" must come first, to correctly
	 * process the change of a "case" inside a "choice". The old "case" must
	 * be deleted before the new "case" is created.
	 */
	if (a_destroy && !b_destroy)
		return -1;
	if (!a_destroy && b_destroy)
		return 1;

	/*
	 * Then sort by priority. If the operation is "destroy", reverse the
	 * order, so that the dependants are deleted before the dependencies.
	 */
	if (a->nb_node->priority < b->nb_node->priority)
		return !a_destroy ? -1 : 1;
	if (a->nb_node->priority > b->nb_node->priority)
		return !a_destroy ? 1 : -1;

	/*
	 * Preserve the order of the configuration changes as told by libyang.
	 */
	if (a->seq < b->seq)
		return -1;
	if (a->seq > b->seq)
		return 1;

	/*
	 * All 'apply_finish' callbacks have their sequence number set to zero.
	 * In this case, compare them using their dnode pointers (the order
	 * doesn't matter for callbacks that have the same priority).
	 */
	if (a->dnode < b->dnode)
		return -1;
	if (a->dnode > b->dnode)
		return 1;

	return 0;
}
RB_GENERATE(nb_config_cbs, nb_config_cb, entry, nb_config_cb_compare);

/*
 * 向变更集 changes 追加一条变更记录：跳过未实现（schema->priv 为空）的节点；
 * 否则分配一个 nb_config_change，填入操作类型、序号（用 *seq 递增分配）、目标
 * 节点等信息，并插入红黑树。
 */
void nb_config_diff_add_change(struct nb_config_cbs *changes,
			       enum nb_cb_operation operation, uint32_t *seq,
			       const struct lyd_node *dnode)
{
	struct nb_config_change *change;

	/* Ignore unimplemented nodes. */
	if (!dnode->schema->priv)
		return;

	change = XCALLOC(MTYPE_TMP, sizeof(*change));
	change->cb.operation = operation;
	change->cb.seq = *seq;
	*seq = *seq + 1;
	change->cb.nb_node = dnode->schema->priv;
	change->cb.dnode = dnode;

	RB_INSERT(nb_config_cbs, changes, &change->cb);
}

/*
 * 清空并释放变更集 changes 中的所有变更记录（逐个从红黑树移除并释放）。
 */
void nb_config_diff_del_changes(struct nb_config_cbs *changes)
{
	while (!RB_EMPTY(nb_config_cbs, changes)) {
		struct nb_config_change *change;

		change = (struct nb_config_change *)RB_ROOT(nb_config_cbs,
							    changes);
		RB_REMOVE(nb_config_cbs, changes, &change->cb);
		XFREE(MTYPE_TMP, change);
	}
}

/*
 * 计算两份配置差异时的辅助函数（递归）：给定一棵新子树 dnode，枚举其中所有
 * 新增的 YANG 数据节点（默认叶子与 leaf-list 除外），为每个节点生成一条变更
 * 记录并追加到 changes。跳过未实现的节点。
 */
/*
 * Helper function used when calculating the delta between two different
 * configurations. Given a new subtree, calculate all new YANG data nodes,
 * excluding default leafs and leaf-lists. This is a recursive function.
 */
void nb_config_diff_created(const struct lyd_node *dnode, uint32_t *seq,
			    struct nb_config_cbs *changes)
{
	enum nb_cb_operation operation;
	struct lyd_node *child;

	/* Ignore unimplemented nodes. */
	if (!dnode->schema->priv)
		return;

	switch (dnode->schema->nodetype) {
	case LYS_LEAF:
	case LYS_LEAFLIST:
		if (lyd_is_default(dnode))
			break;

		if (nb_cb_operation_is_valid(NB_CB_CREATE, dnode->schema))
			operation = NB_CB_CREATE;
		else if (nb_cb_operation_is_valid(NB_CB_MODIFY, dnode->schema))
			operation = NB_CB_MODIFY;
		else
			return;

		nb_config_diff_add_change(changes, operation, seq, dnode);
		break;
	case LYS_CONTAINER:
	case LYS_LIST:
		if (nb_cb_operation_is_valid(NB_CB_CREATE, dnode->schema))
			nb_config_diff_add_change(changes, NB_CB_CREATE, seq,
						  dnode);

		/* Process child nodes recursively. */
		LY_LIST_FOR (lyd_child(dnode), child) {
			nb_config_diff_created(child, seq, changes);
		}
		break;
	default:
		break;
	}
}

/*
 * 计算删除差异（递归）：为一棵被删除的子树生成删除（DESTROY）变更记录。
 *   - 若节点是容器 / list 且其回调设置了 F_NB_CB_DESTROY_RECURSE，则先递归
 *     处理所有子节点，再为本节点生成删除记录；
 *   - 否则，若本节点不支持 DESTROY 回调、且是非存在性容器（NP container），
 *     则对子节点递归处理（NP 容器自身没有 destroy 回调）。
 * 跳过未实现的节点。
 */
static void nb_config_diff_deleted(const struct lyd_node *dnode, uint32_t *seq,
				   struct nb_config_cbs *changes)
{
	struct nb_node *nb_node = dnode->schema->priv;
	struct lyd_node *child;
	bool recursed = false;

	/* Ignore unimplemented nodes. */
	if (!nb_node)
		return;

	/*
	 * If the CB structure indicates it (recurse flag set), call the destroy
	 * callbacks for the children of a containment node.
	 */
	if (CHECK_FLAG(dnode->schema->nodetype, LYS_CONTAINER | LYS_LIST) &&
	    CHECK_FLAG(nb_node->cbs.flags, F_NB_CB_DESTROY_RECURSE)) {
		recursed = true;
		LY_LIST_FOR (lyd_child(dnode), child) {
			nb_config_diff_deleted(child, seq, changes);
		}
	}

	if (nb_cb_operation_is_valid(NB_CB_DESTROY, dnode->schema))
		nb_config_diff_add_change(changes, NB_CB_DESTROY, seq, dnode);
	else if (CHECK_FLAG(dnode->schema->nodetype, LYS_CONTAINER) && !recursed) {
		/*
		 * If we didn't already above, call destroy on the children of
		 * this container (it's an NP container) as NP containers have
		 * no destroy CB themselves.
		 */
		LY_LIST_FOR (lyd_child(dnode), child) {
			nb_config_diff_deleted(child, seq, changes);
		}
	}
}

/*
 * 从 diff 节点上读取 yang:operation 元数据（值形如 c / d / r），即 libyang
 * 标记的变更类型。找不到时返回 'n'（none）。
 */
static int nb_lyd_diff_get_op(const struct lyd_node *dnode)
{
	const struct lyd_meta *meta;
	LY_LIST_FOR (dnode->meta, meta) {
		if (strcmp(meta->name, "operation")
		    || strcmp(meta->annotation->module->name, "yang"))
			continue;
		return lyd_get_meta_value(meta)[0];
	}
	return 'n';
}

#if 0 /* Used below in nb_config_diff inside normally disabled code */
/*
 * 调试辅助：打印一个 diff 节点的路径与取值。本函数及下方的
 * nb_config_diff_dnode_log 都被 #if 0 包围（默认不编译），仅在手工排查 diff
 * 逻辑时启用。
 */
static inline void nb_config_diff_dnode_log_path(const char *context,
						 const char *path,
						 const struct lyd_node *dnode)
{
	if (dnode->schema->nodetype & LYD_NODE_TERM)
		zlog_debug("nb_config_diff: %s: %s: %s", context, path,
			   lyd_get_value(dnode));
	else
		zlog_debug("nb_config_diff: %s: %s", context, path);
}

/*
 * 调试辅助：打印一个 diff 节点（先算出其路径，再调用上面的函数）。
 */
static inline void nb_config_diff_dnode_log(const char *context,
					    const struct lyd_node *dnode)
{
	if (!dnode) {
		zlog_debug("nb_config_diff: %s: NULL", context);
		return;
	}

	char *path = lyd_path(dnode, LYD_PATH_STD, NULL, 0);
	nb_config_diff_dnode_log_path(context, path, dnode);
	free(path);
}
#endif

/*
 * 计算两份配置之间的差异并生成变更集：config1 为基准库（通常是 Running），
 * config2 为被比较的库（通常是 Candidate）。内部用 lyd_diff_siblings 得到
 * diff，再逐个 diff 节点按操作类型分派：
 *   'c' 创建 -> 从 config2 取实际节点，调用 nb_config_diff_created；
 *   'd' 删除 -> 从 config1 取实际节点，调用 nb_config_diff_deleted；
 *   'r' 替换 -> 记为 modify（可能是移动表项或改值）。
 * 处理后用 LYD_TREE_DFS_continue 跳过子树，只按顶层变更处理。
 * 注意：这里刻意用 config2 / config1 中的真实节点（而非 diff 树节点）去查找，
 * 否则处理默认值时会出错，代价是效率略低。
 */
/*
 * Calculate the delta between two different configurations.
 *
 * NOTE: 'config1' is the reference DB, while 'config2' is
 * the DB being compared against 'config1'. Typically 'config1'
 * should be the Running DB and 'config2' is the Candidate DB.
 */
void nb_config_diff(const struct nb_config *config1,
		    const struct nb_config *config2,
		    struct nb_config_cbs *changes)
{
	struct lyd_node *diff = NULL;
	const struct lyd_node *root, *dnode;
	struct lyd_node *target;
	int op;
	LY_ERR err;
	char *path;

#if 0 /* Useful (noisy) when debugging diff code, and for improving later */
	if (DEBUG_MODE_CHECK(&nb_dbg_cbs_config, DEBUG_MODE_ALL)) {
		LY_LIST_FOR(config1->dnode, root) {
			LYD_TREE_DFS_BEGIN(root, dnode) {
				nb_config_diff_dnode_log("from", dnode);
				LYD_TREE_DFS_END(root, dnode);
			}
		}
		LY_LIST_FOR(config2->dnode, root) {
			LYD_TREE_DFS_BEGIN(root, dnode) {
				nb_config_diff_dnode_log("to", dnode);
				LYD_TREE_DFS_END(root, dnode);
			}
		}
	}
#endif

	err = lyd_diff_siblings(config1->dnode, config2->dnode,
				LYD_DIFF_DEFAULTS, &diff);
	assert(!err);

	if (diff && DEBUG_MODE_CHECK(&nb_dbg_cbs_config, DEBUG_MODE_ALL)) {
		char *s;

		if (!lyd_print_mem(&s, diff, LYD_JSON,
				   LYD_PRINT_WITHSIBLINGS | LYD_PRINT_WD_ALL)) {
			zlog_debug("%s: %s", __func__, s);
			free(s);
		}
	}

	uint32_t seq = 0;

	LY_LIST_FOR (diff, root) {
		LYD_TREE_DFS_BEGIN (root, dnode) {
			op = nb_lyd_diff_get_op(dnode);

			path = lyd_path(dnode, LYD_PATH_STD, NULL, 0);

#if 0 /* Useful (noisy) when debugging diff code, and for improving later */
			if (DEBUG_MODE_CHECK(&nb_dbg_cbs_config, DEBUG_MODE_ALL)) {
				char context[80];
				snprintf(context, sizeof(context),
					 "iterating diff: oper: %c seq: %u", op, seq);
				nb_config_diff_dnode_log_path(context, path, dnode);
			}
#endif
			switch (op) {
			case 'c': /* create */
				  /*
				   * This is rather inefficient, but when we use
				   * dnode from the diff instead of the
				   * candidate config node we get failures when
				   * looking up default values, etc, based on
				   * the diff tree.
				   */
				target = yang_dnode_get(config2->dnode, path);
				assert(target);
				nb_config_diff_created(target, &seq, changes);

				/* Skip rest of sub-tree, move to next sibling
				 */
				LYD_TREE_DFS_continue = 1;
				break;
			case 'd': /* delete */
				target = yang_dnode_get(config1->dnode, path);
				assert(target);
				nb_config_diff_deleted(target, &seq, changes);

				/* Skip rest of sub-tree, move to next sibling
				 */
				LYD_TREE_DFS_continue = 1;
				break;
			case 'r': /* replace */
				/* either moving an entry or changing a value */
				target = yang_dnode_get(config2->dnode, path);
				assert(target);
				nb_config_diff_add_change(changes, NB_CB_MODIFY,
							  &seq, target);
				break;
			case 'n': /* none */
			default:
				break;
			}
			free(path);
			LYD_TREE_DFS_END(root, dnode);
		}
	}

	lyd_free_all(diff);
}

/*
 * 在候选配置树中创建一个节点：内部调用 yang_new_path2 按 xpath 新建（可带
 * value），并在需要时用 lyd_new_implicit_tree 补齐隐式节点（不含状态数据）。
 * options 为 lyd_new_path 的选项（如 LYD_NEW_PATH_UPDATE）。可通过 new_dnode
 * 返回新建的节点。成功返回 LY_SUCCESS，失败返回 libyang 错误码。
 */
/**
 * dnode_create() - create a new node in the tree
 * @candidate: config tree to create node in.
 * @xpath: target node to create.
 * @value: value for the new if required.
 * @options: lyd_new_path options
 * @new_dnode: the newly created node. If options includes LYD_NEW_PATH_UPDATE,
 *             and the node exists (i.e., isn't create but updated), then
 *             new_node will be set to NULL not the existing node).
 *
 * Return: NB_OK or NB_ERR.
 */
static LY_ERR dnode_create(struct nb_config *candidate, const char *xpath, const char *value,
			   uint32_t options, struct lyd_node **new_dnode)
{
	struct lyd_node *dnode;
	LY_ERR err;

	err = yang_new_path2(candidate->dnode, ly_native_ctx, xpath, value, 0, 0, options, NULL,
			     &dnode);
	if (err) {
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_new_path(%s) failed: %d",
			  __func__, xpath, err);
		return err;
	} else if (dnode) {
		err = lyd_new_implicit_tree(dnode, LYD_IMPLICIT_NO_STATE, NULL);
		if (err) {
			flog_warn(EC_LIB_LIBYANG,
				  "%s: lyd_new_implicit_all failed: %d",
				  __func__, err);
		}
	}
	if (new_dnode)
		*new_dnode = dnode;
	return LY_SUCCESS;
}

/*
 * 在候选配置上施加一次编辑操作（是上层编辑接口的底层实现之一）。按 operation
 * 分派：
 *   CREATE / MODIFY / CREATE_EXCL：新建或更新节点；若该节点带依赖关系
 *     （get_dependency_xpath），还会连带创建其依赖节点；出错时回滚已建节点；
 *   DESTROY / DELETE：删除节点，并先删除“依赖它的”节点（get_dependant_xpath）；
 *     DELETE 在节点不存在时报错，DESTROY 则视为成功；
 *   REPLACE：先摘下旧节点，再建新节点，出错时把旧节点放回、成功则释放旧节点；
 *   MOVE：暂未实现（TODO）。
 * 返回 NB_OK 或错误码。value 对无需取值的操作可为 NULL。
 */
int nb_candidate_edit(struct nb_config *candidate, const struct nb_node *nb_node,
		      enum nb_operation operation, const char *xpath, const char *value)
{
	struct lyd_node *dnode, *dep_dnode, *old_dnode;
	char dep_xpath[XPATH_MAXLEN];
	struct lyd_node *parent = NULL;
	uint32_t options = 0;
	LY_ERR err;

	switch (operation) {
	case NB_OP_CREATE:
	case NB_OP_MODIFY:
		options = LYD_NEW_PATH_UPDATE;
		fallthrough;
	case NB_OP_CREATE_EXCL:
		err = dnode_create(candidate, xpath, value, options, &dnode);
		if (err) {
			return err;
		} else if (dnode) {
			/*
			 * create dependency
			 *
			 * dnode returned by the lyd_new_path may be from a
			 * different schema, so we need to update the nb_node
			 */
			nb_node = dnode->schema->priv;
			if (nb_node->dep_cbs.get_dependency_xpath) {
				nb_node->dep_cbs.get_dependency_xpath(
					dnode, dep_xpath);

				err = dnode_create(candidate, dep_xpath, NULL,
						   LYD_NEW_PATH_UPDATE, NULL);
				if (err) {
					lyd_free_tree(dnode);
					return err;
				}
			}
		}
		break;
	case NB_OP_DESTROY:
	case NB_OP_DELETE:
		dnode = yang_dnode_get(candidate->dnode, xpath);
		if (!dnode) {
			if (operation == NB_OP_DELETE)
				return NB_ERR;
			else
				return NB_OK;
		}
		/* destroy dependant */
		if (nb_node->dep_cbs.get_dependant_xpath) {
			nb_node->dep_cbs.get_dependant_xpath(dnode, dep_xpath);

			dep_dnode = yang_dnode_get(candidate->dnode, dep_xpath);
			if (dep_dnode)
				lyd_free_tree(dep_dnode);
		}
		lyd_free_tree(dnode);
		break;
	case NB_OP_REPLACE:
		old_dnode = yang_dnode_get(candidate->dnode, xpath);
		if (old_dnode) {
			parent = lyd_parent(old_dnode);
			lyd_unlink_tree(old_dnode);
		}
		err = dnode_create(candidate, xpath, value, options, &dnode);
		if (!err && dnode && !old_dnode) {
			/* create dependency if the node didn't exist */
			nb_node = dnode->schema->priv;
			if (nb_node->dep_cbs.get_dependency_xpath) {
				nb_node->dep_cbs.get_dependency_xpath(
					dnode, dep_xpath);

				err = dnode_create(candidate, dep_xpath, NULL,
						   LYD_NEW_PATH_UPDATE, NULL);
				if (err)
					lyd_free_tree(dnode);
			}
		}
		if (old_dnode) {
			/* restore original node on error, free it otherwise */
			if (err) {
				if (parent)
					lyd_insert_child(parent, old_dnode);
				else
					lyd_insert_sibling(candidate->dnode,
							   old_dnode, NULL);
				return err;
			}

			lyd_free_tree(old_dnode);
		}
		break;
	case NB_OP_MOVE:
		/* TODO: update configuration. */
		break;
	}

	return NB_OK;
}

/*
 * 以“数据树”形式新增 / 修改候选配置（对应 CREATE / MODIFY / REPLACE /
 * CREATE_EXCL）：解析 data（按 format 指定的 LYD_FORMAT）得到待合并的树，再
 * 合并进候选。流程：
 *   1) 解析 data 为 libyang 树；
 *   2) 若目标非根，先构造父节点路径（CREATE_EXCL 传入的已是父路径；其它操作
 *      需去掉最后一级）；
 *   3) 校验 list key：xpath 与数据树中的 key 必须一致（REPLACE / MODIFY）；
 *   4) 检查目标是否已存在：CREATE_EXCL 已存在则报 NB_ERR_EXISTS；REPLACE 若
 *      已存在则先摘下旧节点；
 *   5) 用 lyd_merge_siblings 合并；失败时 REPLACE 会把旧节点放回；
 *   6) 成功则设置 created 输出参数，并释放被替换的旧节点。
 * 返回 NB_OK / NB_ERR / NB_ERR_EXISTS 等。
 */
static int nb_candidate_edit_tree_add(struct nb_config *candidate,
				      enum nb_operation operation,
				      LYD_FORMAT format, const char *xpath,
				      const char *data, bool *created,
				      char *xpath_created, char *errmsg,
				      size_t errmsg_len)
{
	struct lyd_node *tree = NULL;
	struct lyd_node *parent = NULL;
	struct lyd_node *dnode = NULL;
	struct lyd_node *existing = NULL;
	struct lyd_node *ex_parent = NULL;
	char *parent_xpath = NULL;
	struct ly_in *in;
	LY_ERR err;
	bool root;
	int ret;

	err = ly_in_new_memory(data, &in);
	if (err) {
		yang_print_errors(ly_native_ctx, errmsg, errmsg_len);
		return NB_ERR;
	}

	root = xpath[0] == 0 || (xpath[0] == '/' && xpath[1] == 0);

	/* get parent xpath if xpath is not root */
	if (!root) {
		/* NB_OP_CREATE_EXCT already expects parent xpath */
		parent_xpath = XSTRDUP(MTYPE_TMP, xpath);

		/* for other operations - pop one level */
		if (operation != NB_OP_CREATE_EXCL) {
			ret = yang_xpath_pop_node(parent_xpath);
			if (ret) {
				snprintf(errmsg, errmsg_len, "Invalid xpath");
				goto done;
			}

			/* root is not actually a parent */
			if (parent_xpath[0] == 0)
				XFREE(MTYPE_TMP, parent_xpath);
		}
	}

	/*
	 * Create parent if it's not root. We're creating a new tree here to be
	 * merged later with candidate.
	 */
	if (parent_xpath) {
		err = yang_new_path2(NULL, ly_native_ctx, parent_xpath, NULL, 0, 0, 0, &tree,
				     &parent);
		if (err) {
			yang_print_errors(ly_native_ctx, errmsg, errmsg_len);
			ret = NB_ERR;
			goto done;
		}
		assert(parent);
	}

	/* parse data */
	err = yang_lyd_parse_data(ly_native_ctx, parent, in, format,
				  LYD_PARSE_ONLY | LYD_PARSE_STRICT |
					  LYD_PARSE_NO_STATE,
				  0, &dnode);
	if (err) {
		yang_print_errors(ly_native_ctx, errmsg, errmsg_len);
		ret = NB_ERR;
		goto done;
	}

	/* set the tree if we created a top-level node */
	if (!parent)
		tree = dnode;

	/* save xpath of the created node */
	lyd_path(dnode, LYD_PATH_STD, xpath_created, XPATH_MAXLEN);

	/* verify that list keys are the same in the xpath and the data tree */
	if (!root && (operation == NB_OP_REPLACE || operation == NB_OP_MODIFY)) {
		if (lyd_find_path(tree, xpath, 0, NULL)) {
			snprintf(errmsg, errmsg_len,
				 "List keys in xpath and data tree are different");
			ret = NB_ERR;
			goto done;
		}
	}

	/* check if the node already exists in candidate */
	if (operation == NB_OP_CREATE || operation == NB_OP_MODIFY)
		existing = yang_dnode_get(candidate->dnode, xpath_created);
	else if (operation == NB_OP_CREATE_EXCL || operation == NB_OP_REPLACE) {
		existing = yang_dnode_get(candidate->dnode, xpath_created);

		/* if the existing node is implicit default, ignore */
		/* Q: Is this correct for CREATE_EXCL which is supposed to error
		 * if the resouurce already exists? This is used by RESTCONF
		 * when processing the POST command, for example. RFC8040
		 * doesn't say POST fails if resource exists "unless it was a
		 * default".
		 */
		if (existing && (existing->flags & LYD_DEFAULT))
			existing = NULL;

		if (existing) {
			if (operation == NB_OP_CREATE_EXCL) {
				snprintf(errmsg, errmsg_len,
					 "Data already exists");
				ret = NB_ERR_EXISTS;
				goto done;
			}

			if (root) {
				candidate->dnode = NULL;
			} else {
				/* if it's the first top-level node, update candidate */
				if (candidate->dnode == existing)
					candidate->dnode =
						candidate->dnode->next;

				ex_parent = lyd_parent(existing);
				lyd_unlink_tree(existing);
			}
		}
	}

	err = lyd_merge_siblings(&candidate->dnode, tree,
				 LYD_MERGE_DESTRUCT | LYD_MERGE_WITH_FLAGS);
	if (err) {
		/* if replace failed, restore the original node */
		if (existing && operation == NB_OP_REPLACE) {
			if (root) {
				/* Restoring the whole config. */
				candidate->dnode = existing;
			} else if (ex_parent) {
				/*
				 * Restoring a nested node. Insert it as a
				 * child.
				 */
				lyd_insert_child(ex_parent, existing);
			} else {
				/*
				 * Restoring a top-level node. Insert it as a
				 * sibling to candidate->dnode to make sure
				 * the linkage is correct.
				 */
				lyd_insert_sibling(candidate->dnode, existing,
						   &candidate->dnode);
			}
		}
		yang_print_errors(ly_native_ctx, errmsg, errmsg_len);
		ret = NB_ERR;
		goto done;
	} else {
		if (!existing)
			*created = true;
		/*
		 * Free existing node after replace.
		 * We're using `lyd_free_siblings` here to free the whole
		 * tree if we replaced the root node. It won't affect other
		 * siblings if it wasn't root, because the existing node
		 * was unlinked from the tree.
		 */
		if (existing && operation == NB_OP_REPLACE)
			lyd_free_siblings(existing);

		tree = NULL; /* LYD_MERGE_DESTRUCT deleted the tree */
	}

	ret = NB_OK;
done:
	if (tree)
		lyd_free_all(tree);
	XFREE(MTYPE_TMP, parent_xpath);
	ly_in_free(in, 0);

	return ret;
}

/*
 * 以“数据树”形式删除候选配置中的节点（对应 DESTROY / DELETE）：
 *   - 删除根（空 xpath 或 "/"）即清空整份候选；
 *   - 节点不存在（或为隐式默认）时，DELETE 报 NB_ERR_NOT_FOUND，DESTROY 视为
 *     成功；
 *   - 若删除的是顶层首节点，需要修正 candidate->dnode 的指向。
 * 返回 NB_OK / NB_ERR_NOT_FOUND。
 */
static int nb_candidate_edit_tree_del(struct nb_config *candidate,
				      enum nb_operation operation,
				      const char *xpath, char *errmsg,
				      size_t errmsg_len)
{
	struct lyd_node *dnode;

	/* deleting root - remove the whole config */
	if (xpath[0] == 0 || (xpath[0] == '/' && xpath[1] == 0)) {
		lyd_free_all(candidate->dnode);
		candidate->dnode = NULL;
		return NB_OK;
	}

	dnode = yang_dnode_get(candidate->dnode, xpath);
	if (!dnode || (dnode->flags & LYD_DEFAULT)) {
		if (operation == NB_OP_DELETE) {
			snprintf(errmsg, errmsg_len, "Data missing");
			return NB_ERR_NOT_FOUND;
		} else
			return NB_OK;
	}

	/* if it's the first top-level node, update candidate */
	if (candidate->dnode == dnode)
		candidate->dnode = candidate->dnode->next;

	lyd_free_tree(dnode);

	return NB_OK;
}

/*
 * 以数据树形式编辑候选配置的统一入口：按 operation 分派到
 * nb_candidate_edit_tree_add（新建 / 修改 / 替换 / 排他创建）或
 * nb_candidate_edit_tree_del（删除）。MOVE 暂不支持。返回 NB_OK 或错误码。
 */
int nb_candidate_edit_tree(struct nb_config *candidate,
			   enum nb_operation operation, LYD_FORMAT format,
			   const char *xpath, const char *data, bool *created,
			   char *xpath_created, char *errmsg, size_t errmsg_len)
{
	int ret = NB_ERR;

	switch (operation) {
	case NB_OP_CREATE_EXCL:
	case NB_OP_CREATE:
	case NB_OP_MODIFY:
	case NB_OP_REPLACE:
		ret = nb_candidate_edit_tree_add(candidate, operation, format,
						 xpath, data, created,
						 xpath_created, errmsg,
						 errmsg_len);
		break;
	case NB_OP_DESTROY:
	case NB_OP_DELETE:
		ret = nb_candidate_edit_tree_del(candidate, operation, xpath,
						 errmsg, errmsg_len);
		break;
	case NB_OP_MOVE:
		/* not supported yet */
		break;
	}

	return ret;
}

/*
 * 返回配置操作类型对应的可读名称（如 "create"、"delete"）。理论上会穷尽所有
 * 枚举值；若传入非法值则触发断言。
 */
const char *nb_operation_name(enum nb_operation operation)
{
	switch (operation) {
	case NB_OP_CREATE_EXCL:
		return "create exclusive";
	case NB_OP_CREATE:
		return "create";
	case NB_OP_MODIFY:
		return "modify";
	case NB_OP_DESTROY:
		return "destroy";
	case NB_OP_DELETE:
		return "delete";
	case NB_OP_REPLACE:
		return "replace";
	case NB_OP_MOVE:
		return "move";
	}

	assert(!"Reached end of function we should never hit");
	return "DEV ESCAPE";
}

/*
 * 判断某操作是否允许作用于给定节点：list 的 key 叶子不允许 MODIFY / DESTROY /
 * DELETE / REPLACE（key 一经创建即不可改），其余情况允许。返回 true / false。
 */
bool nb_is_operation_allowed(struct nb_node *nb_node, enum nb_operation oper)
{
	if (lysc_is_key(nb_node->snode)) {
		if (oper == NB_OP_MODIFY || oper == NB_OP_DESTROY
		    || oper == NB_OP_DELETE || oper == NB_OP_REPLACE)
			return false;
	}
	return true;
}

/*
 * 编辑候选配置的单条变更（主要供后端使用）：先找到 xpath 对应的 nb_node；
 * 路径未被处理时，若 in_backend 为 true 则忽略并返回 OK，否则告警并返回“可继续
 * 的错误”。还会拒绝作用于 key 节点的操作，并在未给 value 时取该节点的默认值。
 * 返回 NB_CHANGE_OK / NB_CHANGE_ERR / NB_CHANGE_ERR_CONT。
 */
enum nb_change_result nb_candidate_edit_config_change(struct nb_config *candidate_config,
						      enum nb_operation operation, const char *xpath,
						      const char *value, bool in_backend)
{
	struct nb_node *nb_node;
	enum nb_error ret;

	/* Find the northbound node associated to the data path. */
	nb_node = nb_node_find(xpath);
	if (!nb_node) {
		if (in_backend) {
			DEBUGD(&nb_dbg_cbs_config, "%s: ignoring non-handled path: %s", __func__,
			       xpath);
			return NB_CHANGE_OK;
		}
		flog_warn(EC_LIB_YANG_UNKNOWN_DATA_PATH, "%s: unknown data path: %s", __func__,
			  xpath);
		return NB_CHANGE_ERR_CONT;
	}

	/* Find if the node to be edited is not a key node */
	if (!nb_is_operation_allowed(nb_node, operation)) {
		zlog_err("xpath: %s points to key node", xpath);
		return NB_CHANGE_ERR;
	}

	/* If the value is not set, get the default if it exists. */
	/* XXX what about presence containers? */
	if (value == NULL)
		value = yang_snode_get_default(nb_node->snode);

	/*
	 * Ignore "not found" errors when editing the candidate configuration.
	 * [XXX chopps: why?, and then why not check for NB_ERR_NOTFOUND]
	 */
	ret = nb_candidate_edit(candidate_config, nb_node, operation, xpath, value);
	if (ret != NB_OK) {
		flog_warn(EC_LIB_NB_CANDIDATE_EDIT_ERROR,
			  "%s: failed to edit candidate configuration: operation [%s] xpath [%s]",
			  __func__, nb_operation_name(operation), xpath);
		return NB_CHANGE_ERR_CONT;
	}

	return NB_CHANGE_OK;
}

/*
 * 批量编辑候选配置的多条变更：逐条计算实际 xpath（相对路径需拼接 xpath_base），
 * 调用 nb_candidate_edit_config_change。遇到“应中止”的错误（NB_CHANGE_ERR）就
 * 停止；任一错误都会把 *error 置 true，并在 err_buf 填入失败信息。
 */
void nb_candidate_edit_config_changes(struct nb_config *candidate_config,
				      struct nb_cfg_change cfg_changes[], size_t num_cfg_changes,
				      const char *xpath_base, bool in_backend, char *err_buf,
				      int err_bufsize, bool *error)
{
	enum nb_change_result result = NB_CHANGE_OK;

	*error = false;

	if (xpath_base == NULL)
		xpath_base = "";

	/* Edit candidate configuration. */
	for (size_t i = 0; i < num_cfg_changes; i++) {
		struct nb_cfg_change *change = &cfg_changes[i];
		char *change_xpath = change->xpath;
		char xpath[XPATH_MAXLEN];

		memset(xpath, 0, sizeof(xpath));
		/* If change xpath is relative, prepend base xpath. */
		/* XXX shouldn't this be change_xpath[0] != '/'? */
		if (change_xpath[0] == '.') {
			strlcpy(xpath, xpath_base, sizeof(xpath));
			change_xpath++; /* skip '.' */
		}
		strlcat(xpath, change_xpath, sizeof(xpath));

		result = nb_candidate_edit_config_change(candidate_config, change->operation, xpath,
							 change->value, in_backend);
		if (result != NB_CHANGE_OK)
			*error = true;
		if (result == NB_CHANGE_ERR)
			break;
	}
	if (*error) {
		char buf[BUFSIZ];

		snprintf(err_buf, err_bufsize, "%% Failed to edit configuration.\n\n%s",
			 yang_print_errors(ly_native_ctx, buf, sizeof(buf)));
	}
}

/*
 * 判断候选配置是否已过期：候选的 version 小于 running 的 version 即认为需要
 * 基于最新 running 重新更新。返回 true / false。
 */
bool nb_candidate_needs_update(const struct nb_config *candidate)
{
	if (candidate->version < running_config->version)
		return true;

	return false;
}

/*
 * 更新候选配置：复制一份 running，把候选合并进去（保留源），再用合并结果替换
 * 候选。等价于把候选的变更 rebase 到最新 running 之上。合并失败返回 NB_ERR。
 */
int nb_candidate_update(struct nb_config *candidate)
{
	struct nb_config *updated_config;

	updated_config = nb_config_dup(running_config);
	if (nb_config_merge(updated_config, candidate, true) != NB_OK)
		return NB_ERR;

	nb_config_replace(candidate, updated_config, false);

	return NB_OK;
}

/*
 * 执行候选配置的 YANG 语法 / 语义校验：根据 no_state 决定是否包含状态
 * （LYD_VALIDATE_NO_STATE 或 LYD_VALIDATE_PRESENT），在支持时启用多错误报告。
 * 注意 lyd_validate_all 可能会修改配置（补默认值）。返回 NB_OK 或
 * NB_ERR_VALIDATION。
 */
/*
 * Perform YANG syntactic and semantic validation.
 *
 * WARNING: lyd_validate() can change the configuration as part of the
 * validation process.
 */
int nb_candidate_validate_yang(struct nb_config *candidate, bool no_state,
			       char *errmsg, size_t errmsg_len)
{
	uint32_t options = 0;

#ifdef LYD_VALIDATE_MULTI_ERROR
	/* libyang 2.1.36+ */
	options |= LYD_VALIDATE_MULTI_ERROR;
#endif

	if (no_state)
		SET_FLAG(options, LYD_VALIDATE_NO_STATE);
	else
		SET_FLAG(options, LYD_VALIDATE_PRESENT);

	if (lyd_validate_all(&candidate->dnode, ly_native_ctx, options,
			     NULL) != 0) {
		yang_print_errors(ly_native_ctx, errmsg, errmsg_len);
		return NB_ERR_VALIDATION;
	}

	return NB_OK;
}

/*
 * 执行“代码级”校验：先整树遍历候选，对实现了 pre_validate 的节点调用前置校验，
 * 再遍历变更集，对每条变更调用 NB_EV_VALIDATE 阶段的配置回调。任一失败返回
 * NB_ERR_VALIDATION。
 */
/* Perform code-level validation using the northbound callbacks. */
int nb_candidate_validate_code(struct nb_context *context,
			       struct nb_config *candidate,
			       struct nb_config_cbs *changes, char *errmsg,
			       size_t errmsg_len)
{
	struct nb_config_cb *cb;
	struct lyd_node *root, *child;
	int ret;

	/* First validate the candidate as a whole. */
	LY_LIST_FOR (candidate->dnode, root) {
		LYD_TREE_DFS_BEGIN (root, child) {
			struct nb_node *nb_node;

			nb_node = child->schema->priv;
			if (!nb_node || !nb_node->cbs.pre_validate)
				goto next;

			ret = nb_callback_pre_validate(context, nb_node, child,
						       errmsg, errmsg_len);
			if (ret != NB_OK)
				return NB_ERR_VALIDATION;

		next:
			LYD_TREE_DFS_END(root, child);
		}
	}

	/* Now validate the configuration changes. */
	RB_FOREACH (cb, nb_config_cbs, changes) {
		struct nb_config_change *change = (struct nb_config_change *)cb;

		ret = nb_callback_configuration(context, NB_EV_VALIDATE, change,
						errmsg, errmsg_len);
		if (ret != NB_OK)
			return NB_ERR_VALIDATION;
	}

	return NB_OK;
}

/*
 * 计算候选相对 running 的差异并做 YANG 校验：先对候选做 YANG 校验（不含状态），
 * 再初始化变更集并用 nb_config_diff(running, candidate) 生成变更集。返回 NB_OK
 * 或 NB_ERR_VALIDATION。
 */
int nb_candidate_diff_and_validate_yang(struct nb_context *context,
					struct nb_config *candidate,
					struct nb_config_cbs *changes,
					char *errmsg, size_t errmsg_len)
{
	if (nb_candidate_validate_yang(candidate, true, errmsg,
				       sizeof(errmsg_len)) != NB_OK)
		return NB_ERR_VALIDATION;

	RB_INIT(nb_config_cbs, changes);
	nb_config_diff(running_config, candidate, changes);

	return NB_OK;
}

/*
 * 完整校验候选配置：先做“生成差异加 YANG 校验”，再做代码级校验，最后释放临时
 * 变更集。返回 NB_OK 或错误码。
 */
int nb_candidate_validate(struct nb_context *context,
			  struct nb_config *candidate, char *errmsg,
			  size_t errmsg_len)
{
	struct nb_config_cbs changes;
	int ret;

	ret = nb_candidate_diff_and_validate_yang(context, candidate, &changes,
						  errmsg, errmsg_len);
	if (ret != NB_OK)
		return ret;

	ret = nb_candidate_validate_code(context, candidate, &changes, errmsg,
					 errmsg_len);
	nb_config_diff_del_changes(&changes);

	return ret;
}

/*
 * 两阶段提交的第一阶段：可选的 YANG 校验，生成相对 running 的变更集，
 * （非 ignore_zero_change 时）无变更则返回 NB_ERR_NO_CHANGES，可选代码级校验，
 * 最后交给 nb_changes_commit_prepare 建立事务并执行 PREPARE 阶段。失败时释放
 * 已建的变更集。返回 NB_OK / NB_ERR_NO_CHANGES / NB_ERR_VALIDATION /
 * NB_ERR_LOCKED 等。
 */
int nb_candidate_commit_prepare(struct nb_context context,
				struct nb_config *candidate,
				const char *comment,
				struct nb_transaction **transaction,
				bool skip_validate, bool ignore_zero_change,
				char *errmsg, size_t errmsg_len)
{
	struct nb_config_cbs changes;

	if (!skip_validate &&
	    nb_candidate_validate_yang(candidate, true, errmsg, errmsg_len) !=
		    NB_OK) {
		flog_warn(EC_LIB_NB_CANDIDATE_INVALID,
			  "%s: failed to validate candidate configuration",
			  __func__);
		return NB_ERR_VALIDATION;
	}

	RB_INIT(nb_config_cbs, &changes);
	nb_config_diff(running_config, candidate, &changes);
	if (!ignore_zero_change && RB_EMPTY(nb_config_cbs, &changes)) {
		snprintf(
			errmsg, errmsg_len,
			"No changes to apply were found during preparation phase");
		return NB_ERR_NO_CHANGES;
	}

	if (!skip_validate &&
	    nb_candidate_validate_code(&context, candidate, &changes, errmsg,
				       errmsg_len) != NB_OK) {
		flog_warn(EC_LIB_NB_CANDIDATE_INVALID,
			  "%s: failed to validate candidate configuration",
			  __func__);
		nb_config_diff_del_changes(&changes);
		return NB_ERR_VALIDATION;
	}

	return nb_changes_commit_prepare(context, changes, comment, candidate, transaction, errmsg,
					 errmsg_len);
}

/*
 * 由变更集创建并准备事务（供 nb_candidate_commit_prepare 与 mgmtd 直接调用）：
 * 若调用者未提供事务则新建一个；新建失败（例如已有事务正在进行）返回
 * NB_ERR_LOCKED 并释放变更集；成功则执行事务的 PREPARE 阶段并返回其结果。
 */
int nb_changes_commit_prepare(struct nb_context context, struct nb_config_cbs changes,
			      const char *comment, struct nb_config *candidate,
			      struct nb_transaction **transaction, char *errmsg, size_t errmsg_len)
{
	/*
	 * Re-use an existing transaction if provided. Else allocate a new one.
	 */
	if (!*transaction)
		*transaction = nb_transaction_new(context, candidate, &changes,
						  comment, errmsg, errmsg_len);
	if (*transaction == NULL) {
		flog_warn(EC_LIB_NB_TRANSACTION_CREATION_FAILED,
			  "%s: failed to create transaction: %s", __func__,
			  errmsg);
		nb_config_diff_del_changes(&changes);
		return NB_ERR_LOCKED;
	}

	return nb_transaction_process(NB_EV_PREPARE, *transaction, errmsg,
				      errmsg_len);
}

/*
 * 中止事务：执行事务的 ABORT 阶段（回调释放 PREPARE 阶段申请的资源）后释放事务。
 * 返回值为 void，ABORT 失败通常只影响资源回收，不向调用者传播。
 */
void nb_candidate_commit_abort(struct nb_transaction *transaction, char *errmsg,
			       size_t errmsg_len)
{
	(void)nb_transaction_process(NB_EV_ABORT, transaction, errmsg,
				     errmsg_len);
	nb_transaction_free(transaction);
}

/*
 * 应用事务（两阶段提交的落地阶段）：先执行 APPLY 阶段；若失败只记告警并返回
 * （不做回滚，资源由调用方/上层负责）。成功后跑 apply_finish 收尾，再用
 * transaction->config 替换 running_config 并自增版本号；最后按需把事务写入事务
 * 日志（save_transaction 且 nb_db_enabled 时）并释放事务。
 */
void nb_candidate_commit_apply(struct nb_transaction *transaction,
			       bool save_transaction, uint32_t *transaction_id,
			       char *errmsg, size_t errmsg_len)
{
	int err;

	err = nb_transaction_process(NB_EV_APPLY, transaction, errmsg, errmsg_len);
	if (err != NB_OK) {
		flog_warn(EC_LIB_NB_CB_CONFIG_APPLY, "%s: failed to apply transaction: %s",
			  __func__, errmsg);
		return;
	}

	nb_transaction_apply_finish(transaction, errmsg, errmsg_len);

	if (transaction->config) {
		/* Replace running by candidate. */
		transaction->config->version++;
		nb_config_replace(running_config, transaction->config, true);
	}

	/* Record transaction. */
	if (save_transaction && nb_db_enabled && transaction->config &&
	    nb_db_transaction_save(transaction, transaction_id) != NB_OK)
		flog_warn(EC_LIB_NB_TRANSACTION_RECORD_FAILED,
			  "%s: failed to record transaction", __func__);

	nb_transaction_free(transaction);
}

/*
 * 便捷提交：先调用 nb_candidate_commit_prepare（不跳过校验、不允许零变更），
 * 成功则 apply，否则在已拿到事务时 abort。返回 prepare 阶段的返回码。这是绝大多数
 * 调用方想“一步提交”时使用的入口。
 */
int nb_candidate_commit(struct nb_context context, struct nb_config *candidate,
			bool save_transaction, const char *comment,
			uint32_t *transaction_id, char *errmsg,
			size_t errmsg_len)
{
	struct nb_transaction *transaction = NULL;
	int ret;

	ret = nb_candidate_commit_prepare(context, candidate, comment,
					  &transaction, false, false, errmsg,
					  errmsg_len);
	/*
	 * Apply the changes if the preparation phase succeeded. Otherwise abort
	 * the transaction.
	 */
	if (ret == NB_OK)
		nb_candidate_commit_apply(transaction, save_transaction,
					  transaction_id, errmsg, errmsg_len);
	else if (transaction != NULL)
		nb_candidate_commit_abort(transaction, errmsg, errmsg_len);

	return ret;
}

/*
 * 尝试锁定 running 配置：加锁保护下，若当前未被锁定则记录持锁者（client / user）
 * 并返回 0；若已被占用则返回 -1。锁是“软锁”，仅用于 mgmt 层互斥。
 */
int nb_running_lock(enum nb_client client, const void *user)
{
	int ret = -1;

	frr_with_mutex (&running_config_mgmt_lock.mtx) {
		if (!running_config_mgmt_lock.locked) {
			running_config_mgmt_lock.locked = true;
			running_config_mgmt_lock.owner_client = client;
			running_config_mgmt_lock.owner_user = user;
			ret = 0;
		}
	}

	return ret;
}

/*
 * 尝试解锁 running 配置：仅当当前已锁定且调用者正是持锁的 client / user 时才解锁
 * 并返回 0，否则返回 -1。这样可以防止非持锁者误释放别人的锁。
 */
int nb_running_unlock(enum nb_client client, const void *user)
{
	int ret = -1;

	frr_with_mutex (&running_config_mgmt_lock.mtx) {
		if (running_config_mgmt_lock.locked
		    && running_config_mgmt_lock.owner_client == client
		    && running_config_mgmt_lock.owner_user == user) {
			running_config_mgmt_lock.locked = false;
			running_config_mgmt_lock.owner_client = NB_CLIENT_NONE;
			running_config_mgmt_lock.owner_user = NULL;
			ret = 0;
		}
	}

	return ret;
}

/*
 * 检查 running 配置锁状态：未锁定、或就是当前 client / user 持锁时返回 0，否则返回
 * -1。用于在真正提交前做“我是否可写”的判断。
 */
int nb_running_lock_check(enum nb_client client, const void *user)
{
	int ret = -1;

	frr_with_mutex (&running_config_mgmt_lock.mtx) {
		if (!running_config_mgmt_lock.locked
		    || (running_config_mgmt_lock.owner_client == client
			&& running_config_mgmt_lock.owner_user == user))
			ret = 0;
	}

	return ret;
}

/*
 * 调试辅助：当配置回调调试开关（nb_dbg_cbs_config）开启时，打印一次回调调用信息，
 * 包含阶段（event）、操作类型（operation）、xpath 与取值。对无类型数据（存在性容器、
 * 空类型叶子等）取值显示为 "(none)"，否则取字符串形式。
 */
static void nb_log_config_callback(const enum nb_event event,
				   enum nb_cb_operation operation,
				   const struct lyd_node *dnode)
{
	const char *value;
	char xpath[XPATH_MAXLEN];

	if (!DEBUG_MODE_CHECK(&nb_dbg_cbs_config, DEBUG_MODE_ALL))
		return;

	yang_dnode_get_path(dnode, xpath, sizeof(xpath));
	if (yang_snode_is_typeless_data(dnode->schema))
		value = "(none)";
	else
		value = yang_dnode_get_string(dnode, NULL);

	zlog_debug(
		"northbound callback: event [%s] op [%s] xpath [%s] value [%s]",
		nb_event_name(event), nb_cb_operation_name(operation), xpath,
		value);
}

/*
 * 包装 create 回调：填充 nb_cb_create_args（context / event / dnode / resource /
 * errmsg）后调用 nb_node->cbs.create，并对返回码做“非预期错误”的检测与日志
 * （例如在非 VALIDATE 阶段却返回 NB_ERR_VALIDATION）。此处断言本节点不应带
 * F_NB_NODE_IGNORE_CFG_CBS 标志——配置类节点必须实现 create 回调。
 */
static int nb_callback_create(struct nb_context *context,
			      const struct nb_node *nb_node,
			      enum nb_event event, const struct lyd_node *dnode,
			      union nb_resource *resource, char *errmsg,
			      size_t errmsg_len)
{
	struct nb_cb_create_args args = {};
	bool unexpected_error = false;
	int ret;

	assert(!CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS));

	nb_log_config_callback(event, NB_CB_CREATE, dnode);

	args.context = context;
	args.event = event;
	args.dnode = dnode;
	args.resource = resource;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	ret = nb_node->cbs.create(&args);

	/* Detect and log unexpected errors. */
	switch (ret) {
	case NB_OK:
	case NB_ERR:
		break;
	case NB_ERR_VALIDATION:
		if (event != NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	case NB_ERR_RESOURCE:
		if (event != NB_EV_PREPARE)
			unexpected_error = true;
		break;
	case NB_ERR_INCONSISTENCY:
		if (event == NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	default:
		unexpected_error = true;
		break;
	}
	if (unexpected_error)
		DEBUGD(&nb_dbg_cbs_config,
		       "northbound callback: unexpected return value: %s",
		       nb_err_name(ret));

	return ret;
}

/*
 * 包装 modify 回调：填充 nb_cb_modify_args 后调用 nb_node->cbs.modify。与 create
 * 类似，resource 用于在 PREPARE 阶段申请、在 APPLY / ABORT 阶段复用的资源句柄，
 * 并对 NB_ERR_RESOURCE（应只在 PREPARE 出现）等错误做非预期检测与日志。
 */
static int nb_callback_modify(struct nb_context *context,
			      const struct nb_node *nb_node,
			      enum nb_event event, const struct lyd_node *dnode,
			      union nb_resource *resource, char *errmsg,
			      size_t errmsg_len)
{
	struct nb_cb_modify_args args = {};
	bool unexpected_error = false;
	int ret;

	assert(!CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS));

	nb_log_config_callback(event, NB_CB_MODIFY, dnode);

	args.context = context;
	args.event = event;
	args.dnode = dnode;
	args.resource = resource;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	ret = nb_node->cbs.modify(&args);

	/* Detect and log unexpected errors. */
	switch (ret) {
	case NB_OK:
	case NB_ERR:
		break;
	case NB_ERR_VALIDATION:
		if (event != NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	case NB_ERR_RESOURCE:
		if (event != NB_EV_PREPARE)
			unexpected_error = true;
		break;
	case NB_ERR_INCONSISTENCY:
		if (event == NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	default:
		unexpected_error = true;
		break;
	}
	if (unexpected_error)
		DEBUGD(&nb_dbg_cbs_config,
		       "northbound callback: unexpected return value: %s",
		       nb_err_name(ret));

	return ret;
}

/*
 * 包装 destroy 回调：填充 nb_cb_destroy_args 后调用 nb_node->cbs.destroy。注意
 * destroy 的参数里没有 resource——删除阶段不允许申请新资源；合法返回码只有
 * NB_OK / NB_ERR / NB_ERR_VALIDATION / NB_ERR_INCONSISTENCY。
 */
static int nb_callback_destroy(struct nb_context *context,
			       const struct nb_node *nb_node,
			       enum nb_event event,
			       const struct lyd_node *dnode, char *errmsg,
			       size_t errmsg_len)
{
	struct nb_cb_destroy_args args = {};
	bool unexpected_error = false;
	int ret;

	assert(!CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS));

	nb_log_config_callback(event, NB_CB_DESTROY, dnode);

	args.context = context;
	args.event = event;
	args.dnode = dnode;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	ret = nb_node->cbs.destroy(&args);

	/* Detect and log unexpected errors. */
	switch (ret) {
	case NB_OK:
	case NB_ERR:
		break;
	case NB_ERR_VALIDATION:
		if (event != NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	case NB_ERR_INCONSISTENCY:
		if (event == NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	default:
		unexpected_error = true;
		break;
	}
	if (unexpected_error)
		DEBUGD(&nb_dbg_cbs_config,
		       "northbound callback: unexpected return value: %s",
		       nb_err_name(ret));

	return ret;
}

/*
 * 包装 move 回调：填充 nb_cb_move_args 后调用 nb_node->cbs.move。用于处理配置
 * 中列表 / 叶子列表条目的顺序变化（如 user-ordered list）。同样只有 VALIDATE
 * 阶段可返回 NB_ERR_VALIDATION，返回其他错误会被标记为非预期。
 */
static int nb_callback_move(struct nb_context *context,
			    const struct nb_node *nb_node, enum nb_event event,
			    const struct lyd_node *dnode, char *errmsg,
			    size_t errmsg_len)
{
	struct nb_cb_move_args args = {};
	bool unexpected_error = false;
	int ret;

	assert(!CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS));

	nb_log_config_callback(event, NB_CB_MOVE, dnode);

	args.context = context;
	args.event = event;
	args.dnode = dnode;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	ret = nb_node->cbs.move(&args);

	/* Detect and log unexpected errors. */
	switch (ret) {
	case NB_OK:
	case NB_ERR:
		break;
	case NB_ERR_VALIDATION:
		if (event != NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	case NB_ERR_INCONSISTENCY:
		if (event == NB_EV_VALIDATE)
			unexpected_error = true;
		break;
	default:
		unexpected_error = true;
		break;
	}
	if (unexpected_error)
		DEBUGD(&nb_dbg_cbs_config,
		       "northbound callback: unexpected return value: %s",
		       nb_err_name(ret));

	return ret;
}

/*
 * 包装 pre_validate 回调：在所有其他校验之前调用，给模块一次机会做跨节点 / 跨模块
 * 的额外校验（如 A 的配置依赖 B 的配置）。无 resource 参数，且节点带
 * F_NB_NODE_IGNORE_CFG_CBS 时直接返回 0（不调用回调）。合法返回码仅
 * NB_OK / NB_ERR_VALIDATION。
 */
static int nb_callback_pre_validate(struct nb_context *context,
				    const struct nb_node *nb_node,
				    const struct lyd_node *dnode, char *errmsg,
				    size_t errmsg_len)
{
	struct nb_cb_pre_validate_args args = {};
	bool unexpected_error = false;
	int ret;

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return 0;

	nb_log_config_callback(NB_EV_VALIDATE, NB_CB_PRE_VALIDATE, dnode);

	args.dnode = dnode;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	ret = nb_node->cbs.pre_validate(&args);

	/* Detect and log unexpected errors. */
	switch (ret) {
	case NB_OK:
	case NB_ERR_VALIDATION:
		break;
	default:
		unexpected_error = true;
		break;
	}
	if (unexpected_error)
		DEBUGD(&nb_dbg_cbs_config,
		       "northbound callback: unexpected return value: %s",
		       nb_err_name(ret));

	return ret;
}

/*
 * 包装 apply_finish 回调：在 APPLY 阶段全部成功之后调用，用于收尾动作（如发送
 * 通告、启动定时器、刷新硬件）。返回值 void——apply_finish 里的错误无法撤销事务，
 * 只能记录。节点带 F_NB_NODE_IGNORE_CFG_CBS 时跳过。
 */
static void nb_callback_apply_finish(struct nb_context *context,
				     const struct nb_node *nb_node,
				     const struct lyd_node *dnode, char *errmsg,
				     size_t errmsg_len)
{
	struct nb_cb_apply_finish_args args = {};

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return;

	nb_log_config_callback(NB_EV_APPLY, NB_CB_APPLY_FINISH, dnode);

	args.context = context;
	args.dnode = dnode;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	nb_node->cbs.apply_finish(&args);
}

/*
 * 包装 get_elem 回调（操作数据读取）：向模块查询 xpath 对应节点的值，以
 * struct yang_data 返回（调用方负责释放）。这是“从内部数据结构生成状态数据”链路
 * 的起点，供 northbound_oper / CLI 的 show 命令使用。
 */
struct yang_data *nb_callback_get_elem(const struct nb_node *nb_node,
				       const char *xpath,
				       const void *list_entry)
{
	struct nb_cb_get_elem_args args = {};

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return NULL;

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (get_elem): xpath [%s] list_entry [%p]",
	       xpath, list_entry);

	args.xpath = xpath;
	args.list_entry = list_entry;
	return nb_node->cbs.get_elem(&args);
}

/*
 * 包装 get_next 回调：遍历操作数据。语义与 libyang 的 lyd_next_sibling 类似——
 * list_entry 为 NULL 时返回第一个条目，否则返回其后一个；返回 NULL 表示遍历结束。
 * 返回的是模块内部的条目指针（列表项裸指针），不是 lyd_node。
 */
const void *nb_callback_get_next(const struct nb_node *nb_node,
				 const void *parent_list_entry,
				 const void *list_entry)
{
	struct nb_cb_get_next_args args = {};

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return NULL;

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (get_next): node [%s] parent_list_entry [%p] list_entry [%p]",
	       nb_node->xpath, parent_list_entry, list_entry);

	args.parent_list_entry = parent_list_entry;
	args.list_entry = list_entry;
	return nb_node->cbs.get_next(&args);
}

/*
 * 包装 get_keys 回调：取某个列表条目的键值（列表项裸指针 -> 键字符串数组），
 * 结果写入调用者提供的 keys。恒返回 0（键缺失属于模块实现问题，不在这里报错）。
 */
int nb_callback_get_keys(const struct nb_node *nb_node, const void *list_entry,
			 struct yang_list_keys *keys)
{
	struct nb_cb_get_keys_args args = {};

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return 0;

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (get_keys): node [%s] list_entry [%p]",
	       nb_node->xpath, list_entry);

	args.list_entry = list_entry;
	args.keys = keys;
	return nb_node->cbs.get_keys(&args);
}

/*
 * 包装 list_entry_done 回调：遍历完一个列表条目的子节点后由框架调用，让模块释放
 * 遍历过程中临时分配的数据（可选回调，未实现时直接返回）。注意传参是裸指针，
 * 直接调用而非走 args 结构。
 */
void nb_callback_list_entry_done(const struct nb_node *nb_node, const void *parent_list_entry,
				 const void *list_entry)
{
	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS) || !nb_node->cbs.list_entry_done)
		return;

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (list_entry_done): node [%s] parent_list_entry [%p] list_entry [%p]",
	       nb_node->xpath, parent_list_entry, list_entry);

	nb_node->cbs.list_entry_done(parent_list_entry, list_entry);
}

/*
 * 包装 lookup_entry 回调：按父条目 + 键值在模块内部数据结构中查找对应列表项，
 * 命中返回条目裸指针，未命中返回 NULL。用于把 xpath / 键映射回内部对象。
 */
const void *nb_callback_lookup_entry(const struct nb_node *nb_node,
				     const void *parent_list_entry,
				     const struct yang_list_keys *keys)
{
	struct nb_cb_lookup_entry_args args = {};

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return NULL;

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (lookup_entry): node [%s] parent_list_entry [%p]",
	       nb_node->xpath, parent_list_entry);

	args.parent_list_entry = parent_list_entry;
	args.keys = keys;
	return nb_node->cbs.lookup_entry(&args);
}

/*
 * 从已有的 libyang 数据节点 node 反查其对应的内部列表项：先从 node 提取键值，
 * 再调用该节点的 lookup_entry 回调。取键失败会告警并返回 NULL。注意 nb_node 直接
 * 取自 node->schema->priv，因此省去了 xpath 解析。
 */
const void *nb_callback_lookup_node_entry(struct lyd_node *node,
					  const void *parent_list_entry)
{
	struct yang_list_keys keys;
	struct nb_cb_lookup_entry_args args = {};
	const struct nb_node *nb_node = node->schema->priv;

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return NULL;

	if (yang_get_node_keys(node, &keys)) {
		flog_warn(EC_LIB_LIBYANG,
			  "%s: can't get keys for lookup from existing data node %s",
			  __func__, node->schema->name);
		return NULL;
	}

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (lookup_node_entry): node [%s] parent_list_entry [%p]",
	       nb_node->xpath, parent_list_entry);

	args.parent_list_entry = parent_list_entry;
	args.keys = &keys;
	return nb_node->cbs.lookup_entry(&args);
}

/*
 * 包装 lookup_next 回调：按“父条目 + 一组键”返回下一个匹配条目，用于
 * get_next 的高效批量取用场景（跳过逐项比较）。注意实现里复用了
 * nb_cb_lookup_entry_args，但调用的是 cbs.lookup_next，二者参数结构相同。
 */
const void *nb_callback_lookup_next(const struct nb_node *nb_node,
				    const void *parent_list_entry,
				    const struct yang_list_keys *keys)
{
	struct nb_cb_lookup_entry_args args = {};

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return NULL;

	DEBUGD(&nb_dbg_cbs_state,
	       "northbound callback (lookup_entry): node [%s] parent_list_entry [%p]",
	       nb_node->xpath, parent_list_entry);

	args.parent_list_entry = parent_list_entry;
	args.keys = keys;
	return nb_node->cbs.lookup_next(&args);
}

/*
 * 包装 rpc 回调：执行 RPC 操作，input 为输入参数树，output 为待填充的输出树
 * （由调用者预建）。返回 NB_OK / NB_ERR。注意 rpc 不涉及事务与资源管理。
 */
int nb_callback_rpc(const struct nb_node *nb_node, const char *xpath,
		    const struct lyd_node *input, struct lyd_node *output,
		    char *errmsg, size_t errmsg_len)
{
	struct nb_cb_rpc_args args = {};

	DEBUGD(&nb_dbg_cbs_rpc, "northbound RPC: %s", xpath);

	args.xpath = xpath;
	args.input = input;
	args.output = output;
	args.errmsg = errmsg;
	args.errmsg_len = errmsg_len;
	return nb_node->cbs.rpc(&args);
}

/*
 * 包装 notify 回调：投递 YANG 通知。op 为 libyang 通知操作码（LYD_OP_*），dnode 为
 * 通知数据树。返回 void——通知失败只记日志，不影响调用者。
 */
void nb_callback_notify(const struct nb_node *nb_node, uint8_t op, const char *xpath,
			struct lyd_node *dnode)
{
	struct nb_cb_notify_args args = {};

	DEBUGD(&nb_dbg_cbs_notify, "northbound notify: %s", xpath);

	args.xpath = xpath;
	args.op = op;
	args.dnode = dnode;
	nb_node->cbs.notify(&args);
}

/*
 * 分发单个配置变更到对应的回调：按 change->cb.operation（CREATE / MODIFY /
 * DESTROY / MOVE 之一）调用包装函数。VALIDATE 阶段传 resource 为 NULL（不允许申请
 * 资源），其余阶段传 change->resource（PREPARE 阶段申请、后续复用的资源）。
 * 只处理这四种配置操作；出现 PRE_VALIDATE / APPLY_FINISH / GET_* / RPC / NOTIFY
 * 等属实现 bug，直接 flog_err 并 exit。失败时按事件类型用不同严重级别打印日志
 * （VALIDATE / PREPARE / ABORT 为 warn，APPLY 为 err——此时已无法回滚）。
 */
/*
 * Call the northbound configuration callback associated to a given
 * configuration change.
 */
static int nb_callback_configuration(struct nb_context *context,
				     const enum nb_event event,
				     struct nb_config_change *change,
				     char *errmsg, size_t errmsg_len)
{
	enum nb_cb_operation operation = change->cb.operation;
	char xpath[XPATH_MAXLEN];
	const struct nb_node *nb_node = change->cb.nb_node;
	const struct lyd_node *dnode = change->cb.dnode;
	union nb_resource *resource;
	int ret = NB_ERR;

	if (CHECK_FLAG(nb_node->flags, F_NB_NODE_IGNORE_CFG_CBS))
		return NB_OK;

	if (event == NB_EV_VALIDATE)
		resource = NULL;
	else
		resource = &change->resource;

	switch (operation) {
	case NB_CB_CREATE:
		ret = nb_callback_create(context, nb_node, event, dnode,
					 resource, errmsg, errmsg_len);
		break;
	case NB_CB_MODIFY:
		ret = nb_callback_modify(context, nb_node, event, dnode,
					 resource, errmsg, errmsg_len);
		break;
	case NB_CB_DESTROY:
		ret = nb_callback_destroy(context, nb_node, event, dnode,
					  errmsg, errmsg_len);
		break;
	case NB_CB_MOVE:
		ret = nb_callback_move(context, nb_node, event, dnode, errmsg,
				       errmsg_len);
		break;
	case NB_CB_PRE_VALIDATE:
	case NB_CB_APPLY_FINISH:
	case NB_CB_GET_ELEM:
	case NB_CB_GET_NEXT:
	case NB_CB_GET_KEYS:
	case NB_CB_LIST_ENTRY_DONE:
	case NB_CB_LOOKUP_ENTRY:
	case NB_CB_RPC:
	case NB_CB_NOTIFY:
		yang_dnode_get_path(dnode, xpath, sizeof(xpath));
		flog_err(EC_LIB_DEVELOPMENT,
			 "%s: unknown operation (%u) [xpath %s]", __func__,
			 operation, xpath);
		exit(1);
	}

	if (ret != NB_OK) {
		yang_dnode_get_path(dnode, xpath, sizeof(xpath));

		switch (event) {
		case NB_EV_VALIDATE:
			flog_warn(EC_LIB_NB_CB_CONFIG_VALIDATE,
				  "error processing configuration change: error [%s] event [%s] operation [%s] xpath [%s]%s%s",
				  nb_err_name(ret), nb_event_name(event),
				  nb_cb_operation_name(operation), xpath,
				  errmsg[0] ? " message: " : "", errmsg);
			break;
		case NB_EV_PREPARE:
			flog_warn(EC_LIB_NB_CB_CONFIG_PREPARE,
				  "error processing configuration change: error [%s] event [%s] operation [%s] xpath [%s]%s%s",
				  nb_err_name(ret), nb_event_name(event),
				  nb_cb_operation_name(operation), xpath,
				  errmsg[0] ? " message: " : "", errmsg);
			break;
		case NB_EV_ABORT:
			flog_warn(EC_LIB_NB_CB_CONFIG_ABORT,
				  "error processing configuration change: error [%s] event [%s] operation [%s] xpath [%s]%s%s",
				  nb_err_name(ret), nb_event_name(event),
				  nb_cb_operation_name(operation), xpath,
				  errmsg[0] ? " message: " : "", errmsg);
			break;
		case NB_EV_APPLY:
			flog_err(EC_LIB_NB_CB_CONFIG_APPLY,
				 "error processing configuration change: error [%s] event [%s] operation [%s] xpath [%s]%s%s",
				 nb_err_name(ret), nb_event_name(event),
				 nb_cb_operation_name(operation), xpath,
				 errmsg[0] ? " message: " : "", errmsg);
			break;
		default:
			flog_err(EC_LIB_DEVELOPMENT,
				 "%s: unknown event (%u) [xpath %s]", __func__,
				 event, xpath);
			exit(1);
		}
	}

	return ret;
}

/*
 * 创建事务对象：先做两项互斥检查——running 是否被其他 client 锁住（
 * nb_running_lock_check），以及是否已有事务在进行（transaction_in_progress）；
 * 任一不满足就把原因写入 errmsg 并返回 NULL。成功后置 transaction_in_progress
 * 标志、分配事务、记录 context / comment / config，并“接管”变更集（浅拷贝
 * *changes，此后由事务负责释放）。
 */
static struct nb_transaction *
nb_transaction_new(struct nb_context context, struct nb_config *config,
		   struct nb_config_cbs *changes, const char *comment,
		   char *errmsg, size_t errmsg_len)
{
	struct nb_transaction *transaction;

	if (nb_running_lock_check(context.client, context.user)) {
		strlcpy(errmsg,
			"running configuration is locked by another client",
			errmsg_len);
		return NULL;
	}

	if (transaction_in_progress) {
		strlcpy(errmsg,
			"there's already another transaction in progress",
			errmsg_len);
		return NULL;
	}
	transaction_in_progress = true;

	transaction = XCALLOC(MTYPE_TMP, sizeof(*transaction));
	transaction->context = context;
	if (comment)
		strlcpy(transaction->comment, comment,
			sizeof(transaction->comment));
	transaction->config = config;
	transaction->changes = *changes;

	return transaction;
}

/*
 * 释放事务：删除变更集里的所有变更节点、释放事务对象本身，并清除
 * transaction_in_progress 全局标志（允许下一个事务开始）。注意这里不释放
 * transaction->config——运行配置的所有权已通过 nb_config_replace 转移给框架。
 */
static void nb_transaction_free(struct nb_transaction *transaction)
{
	nb_config_diff_del_changes(&transaction->changes);
	XFREE(MTYPE_TMP, transaction);
	transaction_in_progress = false;
}

/*
 * 对一个事务执行指定阶段的处理：按 RB 树的顺序（即提前排好的变更顺序）逐个调用
 * nb_callback_configuration。PREPARE / VALIDATE 阶段允许失败（返回非 NB_OK 即中止并
 * 回传错误，PREPARE 成功时标记 change->prepare_ok）；ABORT 阶段只处理 prepare_ok
 * 的变更（释放已成功申请的资源，未成功的直接 break）；APPLY 阶段失败已无法拒绝
 * 事务，仅记录。
 */
/* Process all configuration changes associated to a transaction. */
static int nb_transaction_process(enum nb_event event,
				  struct nb_transaction *transaction,
				  char *errmsg, size_t errmsg_len)
{
	struct nb_config_cb *cb;

	RB_FOREACH (cb, nb_config_cbs, &transaction->changes) {
		struct nb_config_change *change = (struct nb_config_change *)cb;
		int ret;

		/*
		 * Only try to release resources that were allocated
		 * successfully.
		 */
		if (event == NB_EV_ABORT && !change->prepare_ok)
			break;

		/* Call the appropriate callback. */
		ret = nb_callback_configuration(&transaction->context, event,
						change, errmsg, errmsg_len);
		switch (event) {
		case NB_EV_PREPARE:
			if (ret != NB_OK)
				return ret;
			change->prepare_ok = true;
			break;
		case NB_EV_ABORT:
		case NB_EV_APPLY:
			/*
			 * At this point it's not possible to reject the
			 * transaction anymore, so any failure here can lead to
			 * inconsistencies and should be treated as a bug.
			 * Operations prone to errors, like validations and
			 * resource allocations, should be performed during the
			 * 'prepare' phase.
			 */
			break;
		case NB_EV_VALIDATE:
			break;
		}
	}

	return NB_OK;
}

/*
 * 新建一个 APPLY_FINISH 变更项并插入到 cbs 红黑树（seq 为 0）。apply_finish 回调
 * 用同一套 nb_config_cb 结构临时收集，以便复用按优先级排序的遍历顺序。
 */
static struct nb_config_cb *
nb_apply_finish_cb_new(struct nb_config_cbs *cbs, const struct nb_node *nb_node,
		       const struct lyd_node *dnode)
{
	struct nb_config_cb *cb;

	cb = XCALLOC(MTYPE_TMP, sizeof(*cb));
	cb->operation = NB_CB_APPLY_FINISH;
	cb->nb_node = nb_node;
	cb->dnode = dnode;
	RB_INSERT(nb_config_cbs, cbs, cb);

	return cb;
}

/*
 * 在 apply_finish 收集树上查找（nb_node, dnode）对应的项：构造一个临时查找键
 * （operation=APPLY_FINISH、seq=0）用 RB_FIND 匹配。用于避免对同一数据节点重复注册
 * apply_finish 回调。
 */
static struct nb_config_cb *
nb_apply_finish_cb_find(struct nb_config_cbs *cbs,
			const struct nb_node *nb_node,
			const struct lyd_node *dnode)
{
	struct nb_config_cb s;

	s.operation = NB_CB_APPLY_FINISH;
	s.seq = 0;
	s.nb_node = nb_node;
	s.dnode = dnode;
	return RB_FIND(nb_config_cbs, cbs, &s);
}

/*
 * 在所有变更 APPLY 成功后统一触发 apply_finish 回调。要点有三：
 * 1) 当 mgmtd 处理本地配置变更时 transaction->config 为 NULL（running 由 backends
 *    处理完后再更新），此时回退到 vty_mgmt_candidate_config 取候选树。
 * 2) 沿数据树的每个变更节点及其所有祖先向上查找带 apply_finish 的 nb_node，去重后
 *    收集到临时 cbs 树，最终按优先级（RB 顺序）统一调用——这样即使只改了深层叶子，
 *    其祖先容器的 apply_finish 也会被调用。
 * 3) DESTROY 变更的 dnode 指向 running 树，先取其父级，再从 candidate 树里按
 *    xpath 找回对应节点（被删掉的自身不再触发 apply_finish）。
 */
/* Call the 'apply_finish' callbacks. */
static void nb_transaction_apply_finish(struct nb_transaction *transaction,
					char *errmsg, size_t errmsg_len)
{
	struct nb_config *candidate_config = transaction->config;
	struct nb_config_cbs cbs;
	struct nb_config_cb *cb;

	/*
	 * When mgmtd is processing its local config changes transaction->config
	 * will be NULL (so that running DS is not replaced using it early when
	 * local apply is done -- running is updated after backends client
	 * processing is done). We do need its candidate config though for the
	 * destroy case below.
	 */
	if (!candidate_config) {
		assert(vty_mgmt_candidate_config);
		candidate_config = vty_mgmt_candidate_config;
	}

	/* Initialize tree of 'apply_finish' callbacks. */
	RB_INIT(nb_config_cbs, &cbs);

	/* Identify the 'apply_finish' callbacks that need to be called. */
	RB_FOREACH (cb, nb_config_cbs, &transaction->changes) {
		struct nb_config_change *change = (struct nb_config_change *)cb;
		const struct lyd_node *dnode = change->cb.dnode;

		/*
		 * Iterate up to the root of the data tree. When a node is being
		 * deleted, skip its 'apply_finish' callback if one is defined
		 * (the 'apply_finish' callbacks from the node ancestors should
		 * be called though).
		 */
		if (change->cb.operation == NB_CB_DESTROY) {
			char xpath[XPATH_MAXLEN];

			dnode = lyd_parent(dnode);
			if (!dnode)
				continue;

			/*
			 * The dnode from 'delete' callbacks point to elements
			 * from the running configuration. Use yang_dnode_get()
			 * to get the corresponding dnode from the candidate
			 * configuration that is being committed.
			 */
			yang_dnode_get_path(dnode, xpath, sizeof(xpath));
			dnode = yang_dnode_get(candidate_config->dnode, xpath);
		}
		while (dnode) {
			struct nb_node *nb_node;

			nb_node = dnode->schema->priv;
			if (!nb_node || !nb_node->cbs.apply_finish)
				goto next;

			/*
			 * Don't call the callback more than once for the same
			 * data node.
			 */
			if (nb_apply_finish_cb_find(&cbs, nb_node, dnode))
				goto next;

			nb_apply_finish_cb_new(&cbs, nb_node, dnode);

		next:
			dnode = lyd_parent(dnode);
		}
	}

	/* Call the 'apply_finish' callbacks, sorted by their priorities. */
	RB_FOREACH (cb, nb_config_cbs, &cbs)
		nb_callback_apply_finish(&transaction->context, cb->nb_node,
					 cb->dnode, errmsg, errmsg_len);

	/* Release memory. */
	while (!RB_EMPTY(nb_config_cbs, &cbs)) {
		cb = RB_ROOT(nb_config_cbs, &cbs);
		RB_REMOVE(nb_config_cbs, &cbs, cb);
		XFREE(MTYPE_TMP, cb);
	}
}

/*
 * 校验某个回调操作是否可以合法地绑定到给定 schema 节点：注册 / 自检时使用，防止
 * 把 create 回调挂到只读叶子等非法组合上。规则摘要：
 * - CREATE：仅配置可写（LYS_CONFIG_W）节点；叶子必须是 empty 类型（存在性叶子）、
 *   presence 容器、case 下的容器、或 list / leaf-list。
 * - MODIFY：仅配置可写；只允许非 empty、非键的叶子。
 * - DESTROY：仅配置可写；叶子需可选（非 mandatory / 无默认值）或是 case 子级、带
 *   when 的叶子；容器同理需 presence 或 case 子级；list / leaf-list 均可。
 * - MOVE：仅配置可写且 LYS_ORDBY_USER（用户可排序）的 list / leaf-list。
 * - PRE_VALIDATE / APPLY_FINISH：仅配置可写节点。
 * - GET_ELEM：仅配置只读（LYS_CONFIG_R）；叶子 / leaf-list 或 presence 容器。
 * - GET_NEXT：list（非 CONFIG_ONLY）或只读 leaf-list。
 * - GET_KEYS / LIST_ENTRY_DONE / LOOKUP_ENTRY：list 且非 CONFIG_ONLY、非
 *   KEYLESS_LIST。
 * - RPC：非配置节点（既非 W 也非 R）的 RPC / action。
 * - NOTIFY：LYS_NOTIF 节点。
 */
bool nb_cb_operation_is_valid(enum nb_cb_operation operation,
			      const struct lysc_node *snode)
{
	struct nb_node *nb_node = snode->priv;
	struct lysc_node_container *scontainer;
	struct lysc_node_leaf *sleaf;

	switch (operation) {
	case NB_CB_CREATE:
		if (!CHECK_FLAG(snode->flags, LYS_CONFIG_W))
			return false;

		switch (snode->nodetype) {
		case LYS_LEAF:
			sleaf = (struct lysc_node_leaf *)snode;
			if (sleaf->type->basetype != LY_TYPE_EMPTY)
				return false;
			break;
		case LYS_CONTAINER:
			if (snode->parent && snode->parent->nodetype == LYS_CASE)
				return true;
			scontainer = (struct lysc_node_container *)snode;
			if (!CHECK_FLAG(scontainer->flags, LYS_PRESENCE))
				return false;
			break;
		case LYS_LIST:
		case LYS_LEAFLIST:
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_MODIFY:
		if (!CHECK_FLAG(snode->flags, LYS_CONFIG_W))
			return false;

		switch (snode->nodetype) {
		case LYS_LEAF:
			sleaf = (struct lysc_node_leaf *)snode;
			if (sleaf->type->basetype == LY_TYPE_EMPTY)
				return false;

			/* List keys can't be modified. */
			if (lysc_is_key(sleaf))
				return false;
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_DESTROY:
		if (!CHECK_FLAG(snode->flags, LYS_CONFIG_W))
			return false;

		switch (snode->nodetype) {
		case LYS_LEAF:
			sleaf = (struct lysc_node_leaf *)snode;

			/* List keys can't be deleted. */
			if (lysc_is_key(sleaf))
				return false;

			/*
			 * Only optional leafs can be deleted, or leafs whose
			 * parent is a case statement.
			 */
			if (snode->parent && snode->parent->nodetype == LYS_CASE)
				return true;
			if (sleaf->when)
				return true;
			if (CHECK_FLAG(sleaf->flags, LYS_MAND_TRUE)
#if (LY_VERSION_MAJOR < 4)
			    || sleaf->dflt
#else
			    || sleaf->dflt.str
#endif
			)
				return false;
			break;
		case LYS_CONTAINER:
			if (snode->parent && snode->parent->nodetype == LYS_CASE)
				return true;
			scontainer = (struct lysc_node_container *)snode;
			if (!CHECK_FLAG(scontainer->flags, LYS_PRESENCE))
				return false;
			break;
		case LYS_LIST:
		case LYS_LEAFLIST:
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_MOVE:
		if (!CHECK_FLAG(snode->flags, LYS_CONFIG_W))
			return false;

		switch (snode->nodetype) {
		case LYS_LIST:
		case LYS_LEAFLIST:
			if (!CHECK_FLAG(snode->flags, LYS_ORDBY_USER))
				return false;
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_PRE_VALIDATE:
	case NB_CB_APPLY_FINISH:
		if (!CHECK_FLAG(snode->flags, LYS_CONFIG_W))
			return false;
		return true;
	case NB_CB_GET_ELEM:
		if (!CHECK_FLAG(snode->flags, LYS_CONFIG_R))
			return false;

		switch (snode->nodetype) {
		case LYS_LEAF:
		case LYS_LEAFLIST:
			break;
		case LYS_CONTAINER:
			scontainer = (struct lysc_node_container *)snode;
			if (!CHECK_FLAG(scontainer->flags, LYS_PRESENCE))
				return false;
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_GET_NEXT:
		switch (snode->nodetype) {
		case LYS_LIST:
			if (CHECK_FLAG(nb_node->flags, F_NB_NODE_CONFIG_ONLY))
				return false;
			break;
		case LYS_LEAFLIST:
			if (CHECK_FLAG(snode->flags, LYS_CONFIG_W))
				return false;
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_GET_KEYS:
	case NB_CB_LIST_ENTRY_DONE:
	case NB_CB_LOOKUP_ENTRY:
		switch (snode->nodetype) {
		case LYS_LIST:
			if (CHECK_FLAG(nb_node->flags, F_NB_NODE_CONFIG_ONLY))
				return false;
			if (CHECK_FLAG(nb_node->flags, F_NB_NODE_KEYLESS_LIST))
				return false;
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_RPC:
		if (CHECK_FLAG(snode->flags, LYS_CONFIG_W | LYS_CONFIG_R))
			return false;

		switch (snode->nodetype) {
		case LYS_RPC:
		case LYS_ACTION:
			break;
		default:
			return false;
		}
		return true;
	case NB_CB_NOTIFY:
		if (snode->nodetype != LYS_NOTIF)
			return false;
		return true;
	default:
		return false;
	}
}

DEFINE_HOOK(nb_notification_send, (const char *xpath, struct list *arguments),
	    (xpath, arguments));

/*
 * 发送 YANG 通知（旧接口，参数为 yang_data 列表）：先调用旧 hook
 * nb_notification_send，再把 arguments 里每个 yang_data（xpath + value）逐条转成
 * libyang 数据树，最后调用新 hook nb_notification_tree_send。返回值为各步失败次数
 * 之和。函数会释放 arguments 列表与临时数据树。
 */
int nb_notification_send(const char *xpath, struct list *arguments)
{
	struct lyd_node *root = NULL;
	struct lyd_node *dnode;
	struct yang_data *data;
	struct listnode *ln;
	LY_ERR err;
	int ret;

	DEBUGD(&nb_dbg_notif, "northbound notification: %s", xpath);

	/*
	 * Call old hook functions
	 */
	ret = hook_call(nb_notification_send, xpath, arguments);

	if (!hook_have_hooks(nb_notification_tree_send))
		goto done;
	/*
	 * Convert yang data arguments list to a libyang data tree for new hook
	 * functions.
	 */
	for (ALL_LIST_ELEMENTS_RO(arguments, ln, data)) {
		err = lyd_new_path(root, ly_native_ctx, data->xpath,
				   data->value, LYD_NEW_PATH_UPDATE, &dnode);
		if (err != LY_SUCCESS)
			goto lyerr;
		if (!root) {
			root = dnode;
			while (root->parent)
				root = lyd_parent(root);
		}
	}

	if (!root) {
		err = lyd_new_path(NULL, ly_native_ctx, xpath, "", 0, &root);
		if (err) {
lyerr:
			flog_err(EC_LIB_LIBYANG,
				 "%s: error creating notification data: %s",
				 __func__, ly_strerrcode(err));
			ret += 1;
			goto done;
		}
	}

	/*
	 * Call new hook functions
	 */
	ret += nb_notification_tree_send(xpath, root);

done:
	if (root)
		lyd_free_all(root);
	if (arguments)
		list_delete(&arguments);

	return ret;
}

DEFINE_HOOK(nb_notification_tree_send,
	    (const char *xpath, const struct lyd_node *tree), (xpath, tree));

/*
 * 发送 YANG 通知（新接口，参数为 libyang 数据树）：直接调用
 * nb_notification_tree_send hook。tree 不可为 NULL（断言）。
 */
int nb_notification_tree_send(const char *xpath, const struct lyd_node *tree)
{
	int ret;

	assert(tree);

	DEBUGD(&nb_dbg_notif, "northbound tree notification: %s",
	       tree->schema->name);

	ret = hook_call(nb_notification_tree_send, xpath, tree);

	return ret;
}

/*
 * 运行配置“用户指针”表：把 libyang 数据节点的 xpath 映射到 daemon 自己的内部
 * 数据对象（void *entry）。模块在 create 回调里用 nb_running_set_entry 登记，
 * 之后便能通过 nb_running_get_entry 由节点反查内部对象。key 即 xpath 字符串。
 */
/* Running configuration user pointers management. */
struct nb_config_entry {
	char xpath[XPATH_MAXLEN];
	void *entry;
};

/*
 * 哈希表比较函数：直接以 xpath 字符串相等作为键相等。
 */
static bool running_config_entry_cmp(const void *value1, const void *value2)
{
	const struct nb_config_entry *c1 = value1;
	const struct nb_config_entry *c2 = value2;

	return strmatch(c1->xpath, c2->xpath);
}

/*
 * 哈希键生成：对 xpath 字符串做字符串哈希。
 */
static unsigned int running_config_entry_key_make(const void *value)
{
	return string_hash_make(value);
}

/*
 * 哈希表槽位分配回调：为新的 xpath 键分配一个 nb_config_entry（拷贝 xpath，entry
 * 留待 nb_running_set_entry 填充）。
 */
static void *running_config_entry_alloc(void *p)
{
	struct nb_config_entry *new, *key = p;

	new = XCALLOC(MTYPE_NB_CONFIG_ENTRY, sizeof(*new));
	strlcpy(new->xpath, key->xpath, sizeof(new->xpath));

	return new;
}

/*
 * 哈希表槽位释放回调：释放 nb_config_entry 结构本身（不释放其 entry 指向的模块对象，
 * 那由模块自行管理）。
 */
static void running_config_entry_free(void *arg)
{
	XFREE(MTYPE_NB_CONFIG_ENTRY, arg);
}

/*
 * 为给定数据节点登记用户指针 entry：按 dnode 的 xpath 在哈希表中取（或新建）槽位
 * 并写入 entry。通常在 create 回调里调用，把 YANG 节点与内部对象关联起来。
 */
void nb_running_set_entry(const struct lyd_node *dnode, void *entry)
{
	struct nb_config_entry *config, s;

	yang_dnode_get_path(dnode, s.xpath, sizeof(s.xpath));
	config = hash_get(running_config_entries, &s,
			  running_config_entry_alloc);
	config->entry = entry;
}

/*
 * 批量迁移用户指针：把哈希表中所有 xpath 以 xpath_from 开头的表项改写成以
 * xpath_to 开头（frrstr_replace），再重新插入哈希表。用于父节点改名 / 移动导致
 * 子树 xpath 前缀整体变化的场景。
 */
void nb_running_move_tree(const char *xpath_from, const char *xpath_to)
{
	struct nb_config_entry *entry;
	struct list *entries = hash_to_list(running_config_entries);
	struct listnode *ln;

	for (ALL_LIST_ELEMENTS_RO(entries, ln, entry)) {
		if (!frrstr_startswith(entry->xpath, xpath_from))
			continue;

		hash_release(running_config_entries, entry);

		char *newpath =
			frrstr_replace(entry->xpath, xpath_from, xpath_to);
		strlcpy(entry->xpath, newpath, sizeof(entry->xpath));
		XFREE(MTYPE_TMP, newpath);

		(void)hash_get(running_config_entries, entry,
			       hash_alloc_intern);
	}

	list_delete(&entries);
}

/*
 * 递归注销用户指针的辅助函数：先按 dnode 的 xpath 从哈希表取走并释放表项（返回其
 * entry），再对 list / container 的所有子节点递归处理。返回本节点被注销的 entry
 * （无则为 NULL）。用于删除配置时同步清理内部对象的登记关系。
 */
static void *nb_running_unset_entry_helper(const struct lyd_node *dnode)
{
	struct nb_config_entry *config, s;
	struct lyd_node *child;
	void *entry = NULL;

	yang_dnode_get_path(dnode, s.xpath, sizeof(s.xpath));
	config = hash_release(running_config_entries, &s);
	if (config) {
		entry = config->entry;
		running_config_entry_free(config);
	}

	/* Unset user pointers from the child nodes. */
	if (CHECK_FLAG(dnode->schema->nodetype, LYS_LIST | LYS_CONTAINER)) {
		LY_LIST_FOR (lyd_child(dnode), child) {
			(void)nb_running_unset_entry_helper(child);
		}
	}

	return entry;
}

/*
 * 注销用户指针：调用递归辅助函数并断言确实取到了 entry（找不到说明模块逻辑有误或
 * 重复删除），返回被注销的 entry 供调用者释放内部对象。
 */
void *nb_running_unset_entry(const struct lyd_node *dnode)
{
	void *entry;

	entry = nb_running_unset_entry_helper(dnode);
	assert(entry);

	return entry;
}

/*
 * 查用户指针的公共实现：dnode 为 NULL 时先用 xpath 去 running 树里取节点；然后从
 * 该节点起（rec_search 为真时）沿 lyd_parent 逐级向上，在哈希表里找第一个有登记的
 * 祖先并返回其 entry。“向上查找”是为了让子节点复用祖先（如接口）登记的同一内部对象。
 * 找不到时：abort_if_not_found 为真则打印回溯并 abort（属编程错误），否则返回 NULL。
 */
static void *nb_running_get_entry_worker(const struct lyd_node *dnode,
					 const char *xpath,
					 bool abort_if_not_found,
					 bool rec_search)
{
	const struct lyd_node *orig_dnode = dnode;
	char xpath_buf[XPATH_MAXLEN];
	bool rec_flag = true;

	assert(dnode || xpath);

	if (!dnode)
		dnode = yang_dnode_get(running_config->dnode, xpath);

	while (rec_flag && dnode) {
		struct nb_config_entry *config, s;

		yang_dnode_get_path(dnode, s.xpath, sizeof(s.xpath));
		config = hash_lookup(running_config_entries, &s);
		if (config)
			return config->entry;

		rec_flag = rec_search;

		dnode = lyd_parent(dnode);
	}

	if (!abort_if_not_found)
		return NULL;

	yang_dnode_get_path(orig_dnode, xpath_buf, sizeof(xpath_buf));
	flog_err(EC_LIB_YANG_DNODE_NOT_FOUND,
		 "%s: failed to find entry [xpath %s]", __func__, xpath_buf);
	zlog_backtrace(LOG_ERR);
	abort();
}

/*
 * 查用户指针（含向上递归）：语义见 nb_running_get_entry_worker，rec_search 为真，
 * 即允许从祖先节点继承 entry。这是最常用的版本。
 */
void *nb_running_get_entry(const struct lyd_node *dnode, const char *xpath,
			   bool abort_if_not_found)
{
	return nb_running_get_entry_worker(dnode, xpath, abort_if_not_found,
					   true);
}

/*
 * 查用户指针（不向上递归）：rec_search 为假，只查 dnode 自身的 xpath 是否有登记，
 * 不继承祖先的 entry。适用于必须精确匹配本节点的场景。
 */
void *nb_running_get_entry_non_rec(const struct lyd_node *dnode,
				   const char *xpath, bool abort_if_not_found)
{
	return nb_running_get_entry_worker(dnode, xpath, abort_if_not_found,
					   false);
}

/*
 * 返回事务阶段（enum nb_event）的可读名字：validate / prepare / abort / apply。
 * 用于日志与错误信息。理论上不会走到末尾，到达则触发断言。
 */
/* Logging functions. */
const char *nb_event_name(enum nb_event event)
{
	switch (event) {
	case NB_EV_VALIDATE:
		return "validate";
	case NB_EV_PREPARE:
		return "prepare";
	case NB_EV_ABORT:
		return "abort";
	case NB_EV_APPLY:
		return "apply";
	}

	assert(!"Reached end of function we should never hit");
	return "DEV ESCAPE";
}

/*
 * 返回回调操作（enum nb_cb_operation）的可读名字：create / modify / destroy /
 * move / pre_validate / apply_finish / get_elem / get_next / get_keys /
 * list_entry_done / lookup_entry / rpc / notify。用于日志。
 */
const char *nb_cb_operation_name(enum nb_cb_operation operation)
{
	switch (operation) {
	case NB_CB_CREATE:
		return "create";
	case NB_CB_MODIFY:
		return "modify";
	case NB_CB_DESTROY:
		return "destroy";
	case NB_CB_MOVE:
		return "move";
	case NB_CB_PRE_VALIDATE:
		return "pre_validate";
	case NB_CB_APPLY_FINISH:
		return "apply_finish";
	case NB_CB_GET_ELEM:
		return "get_elem";
	case NB_CB_GET_NEXT:
		return "get_next";
	case NB_CB_GET_KEYS:
		return "get_keys";
	case NB_CB_LIST_ENTRY_DONE:
		return "list_entry_done";
	case NB_CB_LOOKUP_ENTRY:
		return "lookup_entry";
	case NB_CB_RPC:
		return "rpc";
	case NB_CB_NOTIFY:
		return "notify";
	}

	assert(!"Reached end of function we should never hit");
	return "DEV ESCAPE";
}

/*
 * 返回错误码（enum nb_error）的可读名字：ok / generic error / no changes /
 * element not found / element already exists / resource is locked / validation /
 * failed to allocate resource / internal inconsistency / should yield。用于日志。
 */
const char *nb_err_name(enum nb_error error)
{
	switch (error) {
	case NB_OK:
		return "ok";
	case NB_ERR:
		return "generic error";
	case NB_ERR_NO_CHANGES:
		return "no changes";
	case NB_ERR_NOT_FOUND:
		return "element not found";
	case NB_ERR_EXISTS:
		return "element already exists";
	case NB_ERR_LOCKED:
		return "resource is locked";
	case NB_ERR_VALIDATION:
		return "validation";
	case NB_ERR_RESOURCE:
		return "failed to allocate resource";
	case NB_ERR_INCONSISTENCY:
		return "internal inconsistency";
	case NB_YIELD:
		return "should yield";
	}

	assert(!"Reached end of function we should never hit");
	return "DEV ESCAPE";
}

/*
 * 返回客户端类型（enum nb_client）的可读名字：CLI / Sysrepo / gRPC / Pcep /
 * MGMTD Server / MGMT Backend / None。用于日志与事务记录。
 */
const char *nb_client_name(enum nb_client client)
{
	switch (client) {
	case NB_CLIENT_CLI:
		return "CLI";
	case NB_CLIENT_SYSREPO:
		return "Sysrepo";
	case NB_CLIENT_GRPC:
		return "gRPC";
	case NB_CLIENT_PCEP:
		return "Pcep";
	case NB_CLIENT_MGMTD_SERVER:
		return "MGMTD Server";
	case NB_CLIENT_MGMTD_BE:
		return "MGMT Backend";
	case NB_CLIENT_NONE:
		return "None";
	}

	assert(!"Reached end of function we should never hit");
	return "DEV ESCAPE";
}

/*
 * 把一个 YANG 模块注册的节点表（frr_yang_module_info->nodes，以 xpath 索引、NULL
 * 结尾）中的回调与优先级装载到对应的 nb_node 上。xpath 找不到对应 nb_node 时告警
 * 并跳过（可能是注册路径写错或模块未加载）。节点数超过 YANG_MODULE_MAX_NODES 直接
 * 退出。priority 为 0 时保留 nb_node 原有优先级（不覆盖默认值）。
 */
static void nb_load_callbacks(const struct frr_yang_module_info *module)
{
	for (size_t i = 0; module->nodes[i].xpath; i++) {
		struct nb_node *nb_node;
		uint32_t priority;

		if (i > YANG_MODULE_MAX_NODES) {
			zlog_err(
				"%s: %s.yang has more than %u nodes. Please increase YANG_MODULE_MAX_NODES to fix this problem.",
				__func__, module->name, YANG_MODULE_MAX_NODES);
			exit(1);
		}

		nb_node = nb_node_find(module->nodes[i].xpath);
		if (!nb_node) {
			flog_warn(EC_LIB_YANG_UNKNOWN_DATA_PATH,
				  "%s: unknown data path: %s", __func__,
				  module->nodes[i].xpath);
			continue;
		}

		nb_node->cbs = module->nodes[i].cbs;
		priority = module->nodes[i].priority;
		if (priority != 0)
			nb_node->priority = priority;
	}
}

/*
 * 遍历所有 YANG 编译节点，对每个节点调用 nb_node_validate 做回调合法性自检
 * （详见 nb_cb_operation_is_valid 的规则），累计错误数；有错则打错误日志并 exit(1)。
 * 属于启动期一次性检查，把回调配置错误尽早暴露。
 */
void nb_validate_callbacks(void)
{
	unsigned int errors = 0;

	yang_snodes_iterate(NULL, nb_node_validate, 0, &errors);
	if (errors > 0) {
		flog_err(
			EC_LIB_NB_CBS_VALIDATION,
			"%s: failed to validate northbound callbacks: %u error(s)",
			__func__, errors);
		exit(1);
	}
}


/*
 * northbound 子系统的总初始化入口（daemon 启动时调用）。步骤：
 * 1) 记录 db_enabled，初始化 libyang 上下文（显式编译特性因 libyang2 的已知问题
 *    固定为 false）；
 * 2) 加载 frr-logging 依赖模块，再依次加载各模块的 YANG 与其 frr_info；
 * 3) 若未加载则补上 frr-host / frr-logging 模块；
 * 4) 对每个模块遍历编译节点建立 nb_node（nb_node_new_cb）并装载回调
 *    （nb_load_callbacks）；
 * 5) 自检回调合法性（nb_validate_callbacks）；
 * 6) 建空的 running 配置与用户指针哈希表、初始化 running 配置锁；
 * 7) 初始化 northbound CLI / oper / notif 三个子系统。
 * 参数：tm 事件循环；modules 模块信息数组；nmodules 数量；db_enabled 是否启用事务
 * 日志；load_library 是否加载 YANG 库。
 */
void nb_init(struct event_loop *tm,
	     const struct frr_yang_module_info *const modules[],
	     size_t nmodules, bool db_enabled, bool load_library)
{
	struct yang_module *loaded[nmodules + 2];

	/*
	 * Currently using this explicit compile feature in libyang2 leads to
	 * incorrect behavior in FRR. The functionality suppresses the compiling
	 * of modules until they have all been loaded into the context. This
	 * avoids multiple recompiles of the same modules as they are
	 * imported/augmented etc.
	 * (Done as a #define to make coverity happy)
	 */
#define explicit_compile false

	nb_db_enabled = db_enabled;

	yang_init(true, explicit_compile, load_library);

	/* needed for frr-logging */
	assert(yang_module_load("ietf-syslog-types", NULL));

	/* Load YANG modules and their corresponding northbound callbacks. */
	for (size_t i = 0; i < nmodules; i++) {
		DEBUGD(&nb_dbg_events, "northbound: loading %s.yang",
		       modules[i]->name);
		loaded[i] = yang_module_load(modules[i]->name, modules[i]->features);
		loaded[i]->frr_info = modules[i];
	}

	/* Load the host module if it is not already. */
	if (!yang_module_find("frr-host")) {
		loaded[nmodules] = yang_module_load("frr-host", NULL);
		loaded[nmodules]->frr_info = &frr_host_nb_info;
		nmodules++;
	}

	/* Load the host module if it is not already. */
	if (!yang_module_find("frr-logging")) {
		loaded[nmodules] = yang_module_load("frr-logging", NULL);
		loaded[nmodules]->frr_info = &frr_logging_nb_info;
		nmodules++;
	}

	if (explicit_compile)
		yang_init_loading_complete();

	/* Initialize the compiled nodes with northbound data */
	for (size_t i = 0; i < nmodules; i++) {
		yang_snodes_iterate(loaded[i]->info, nb_node_new_cb, 0,
				    (void *)loaded[i]->frr_info);
		nb_load_callbacks(loaded[i]->frr_info);
	}

	/* Validate northbound callbacks. */
	nb_validate_callbacks();

	/* Create an empty running configuration. */
	running_config = nb_config_new(NULL);
	running_config_entries = hash_create(running_config_entry_key_make,
					     running_config_entry_cmp,
					     "Running Configuration Entries");
	pthread_mutex_init(&running_config_mgmt_lock.mtx, NULL);

	/* Initialize the northbound CLI. */
	nb_cli_init(tm);

	/* Initialize oper-state */
	nb_oper_init(tm);

	/* Initialize notification-state */
	nb_notif_init(tm);
}

/*
 * northbound 子系统的对称清理入口（daemon 关闭时调用）：依次终止 notif / oper /
 * CLI 子系统，删除所有模块的 nb_node，清空并释放用户指针哈希表，释放 running
 * 配置，销毁 running 配置锁。是 nb_init 的逆过程。
 */
void nb_terminate(void)
{
	nb_notif_terminate();

	nb_oper_terminate();

	/* Terminate the northbound CLI. */
	nb_cli_terminate();

	/* Delete all nb_node's from all YANG modules. */
	nb_nodes_delete();

	/* Delete the running configuration. */
	hash_clean_and_free(&running_config_entries, running_config_entry_free);
	nb_config_free(running_config);
	pthread_mutex_destroy(&running_config_mgmt_lock.mtx);
}
