// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Route map northbound implementation.
 *
 * Copyright (C) 2019 Network Device Education Foundation, Inc. ("NetDEF")
 *                    Rafael Zalamena
 */
/* ============================================================================
 * 【学习导读】routemap_northbound.c —— route-map 的 YANG 回调实现
 * ============================================================================
 * 本文件是 frr-route-map YANG 模型与 routemap.c 内部 C 结构之间的“粘合层”。
 * CLI（routemap_cli.c）提交的 YANG 改动最终会以回调形式落到这里，再由这里
 * 调用 routemap.c 的 route_map_* API 去真正建立/撤销 match/set 动作。
 *
 * 数据模型层级（XPath，父 → 子）：
 *   /frr-route-map:lib
 *     /route-map[name]                      ← 一条 route-map（容器）
 *       /entry[sequence]                    ← 一条 entry（含 action permit/deny）
 *         /match-condition[condition] …     ← 若干 match 条件
 *         /set-action[action] …             ← 若干 set 动作
 *         /exit-policy、/call、/description、/goto-value … ← entry 级属性
 *
 * 回调函数命名约定：lib_route_map_xxx_create / _destroy / _modify / _apply。
 * 每个回调都要处理四个事件（见 nb_cb_*）：
 *   - NB_EV_VALIDATE：校验阶段，只检查合法性，不产生副作用；
 *   - NB_EV_PREPARE ：准备阶段；
 *   - NB_EV_APPLY   ：真正生效阶段，这里才去调用 routemap.c 的 API；
 *   - NB_EV_ABORT   ：提交被中止，回滚。
 * 绝大多数回调只有 APPLY 分支有实质动作，其余分支是空操作（依赖 YANG 事务的
 * 两阶段提交保证一致性）。
 *
 * 两套数据结构要分清：
 *   - route_map / route_map_index / route_map_rule：routemap.c 的内部结构；
 *   - routemap_hook_context（rhc）：把某个 route-map 条目与某个模块的
 *     hook 函数（如 bgpd 的 match/set 回调）绑在一起，用 TAILQ 串成 rhclist。
 *     lib_route_map_entry_*_destroy 就是靠它去调用模块注册的钩子来“撤销”。
 *
 * 末尾是一张巨大的 frr_route_map_info[] 表，把每条 YANG 节点路径映射到对应
 * 的回调函数，是 northbound 框架的入口（真正的“注册表”）。
 * ============================================================================
 */

#include <zebra.h>

#include "lib/command.h"
#include "lib/log.h"
#include "lib/northbound.h"
#include "lib/routemap.h"

/*
 * Auxiliary functions to avoid code duplication:
 *
 * lib_route_map_entry_set_destroy: unset `set` commands.
 * lib_route_map_entry_match_destroy: unset `match` commands.
 */
/* 撤销一个 match 条件：找到该 entry 的 hook 上下文 rhc，若模块注册了 match
 * 钩子（rhc_mhook），就以 (rmi, rule, NULL, event) 调用它，把该 match 动作
 * 从 route-map 里摘掉。
 * 入参：args->dnode 是被删除的 match-condition 节点。
 * 关键点：
 *   - 只在 NB_EV_APPLY 事件下才动作，其余事件直接返回 NB_OK。
 *   - nb_running_get_entry(dnode, NULL, true)：从 YANG 节点取回绑定的 C 结构，
 *     第三个参数 true 表示“找不到就致命报错”。
 *   - 若模块没注册钩子（rhc_mhook==NULL）则无事可做，返回 NB_OK。
 *   - 钩子返回非 CMD_SUCCESS 时，返回 NB_ERR_INCONSISTENCY 表示数据不一致，
 *     让上层知道这次撤销没干净。 */
int lib_route_map_entry_match_destroy(struct nb_cb_destroy_args *args)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	rhc = nb_running_get_entry(args->dnode, NULL, true);
	if (rhc->rhc_mhook == NULL)
		return NB_OK;

	rv = rhc->rhc_mhook(rhc->rhc_rmi, rhc->rhc_rule, NULL,
			    rhc->rhc_event,
			    args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;

	return NB_OK;
}

/* 撤销一个 set 动作：与 match 版完全对称，只是改用 set 钩子 rhc_shook。
 * 入参：args->dnode 是被删除的 set-action 节点。其余语义同
 * lib_route_map_entry_match_destroy（仅 APPLY 生效、无钩子则空转、失败返回
 * NB_ERR_INCONSISTENCY）。 */
int lib_route_map_entry_set_destroy(struct nb_cb_destroy_args *args)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	rhc = nb_running_get_entry(args->dnode, NULL, true);
	if (rhc->rhc_shook == NULL)
		return NB_OK;

	rv = rhc->rhc_shook(rhc->rhc_rmi, rhc->rhc_rule, NULL,
			    args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;

	return NB_OK;
}

/*
 * Auxiliary hook context list manipulation functions.
 */
/* 为一个 route_map_index 新建 hook 上下文并挂到它的 rhclist 尾部。
 * 用途：上层模块（如 bgpd 的 match/set）注册回调时，需要一个对象同时保存
 * “属于哪个 rmi、规则号、事件”，rhc 就是这个载体。
 * 返回：新分配的 rhc（用 MTYPE_TMP 分配，记得用 routemap_hook_context_free 释放）。 */
struct routemap_hook_context *
routemap_hook_context_insert(struct route_map_index *rmi)
{
	struct routemap_hook_context *rhc;

	rhc = XCALLOC(MTYPE_TMP, sizeof(*rhc));
	rhc->rhc_rmi = rmi;
	TAILQ_INSERT_TAIL(&rmi->rhclist, rhc, rhc_entry);

	return rhc;
}

/* 释放一个 hook 上下文：先从所属 rmi 的 rhclist 里摘除，再 XFREE。
 * 注意：它用 rhc->rhc_rmi 反查链表头，所以调用前该 rmi 必须仍然有效。 */
void routemap_hook_context_free(struct routemap_hook_context *rhc)
{
	struct route_map_index *rmi = rhc->rhc_rmi;

	TAILQ_REMOVE(&rmi->rhclist, rhc, rhc_entry);
	XFREE(MTYPE_TMP, rhc);
}

/*
 * XPath: /frr-route-map:lib/route-map
 */
/* 创建一条 route-map（YANG: /frr-route-map:lib/route-map）。
 * APPLY 时用 name 拿到/新建内部 struct route_map，并登记到 YANG 节点的
 * “running” 绑定上（nb_running_set_entry），供后续 entry 级回调取用。
 * 返回：NB_OK。其余事件（VALIDATE/PREPARE/ABORT）不做事。 */
static int lib_route_map_create(struct nb_cb_create_args *args)
{
	struct route_map *rm;
	const char *rm_name;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rm_name = yang_dnode_get_string(args->dnode, "name");
		rm = route_map_get(rm_name);
		nb_running_set_entry(args->dnode, rm);
		break;
	}

	return NB_OK;
}

/* 删除一条 route-map：APPLY 时先从 YANG 节点取回内部 struct route_map
 * （nb_running_unset_entry 会解绑），再调用 route_map_delete 真正释放。
 * 注意 unset 与 delete 配对：unset 拿指针并清除绑定，delete 做释放。 */
static int lib_route_map_destroy(struct nb_cb_destroy_args *args)
{
	struct route_map *rm;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rm = nb_running_unset_entry(args->dnode);
		route_map_delete(rm);
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/optimization-disabled
 */
/* 修改 route-map 的“是否禁用优化”开关（XPath …/optimization-disabled）。
 * APPLY 时把布尔值直接写到 rm->optimization_disabled。 */
static int
lib_route_map_optimization_disabled_modify(struct nb_cb_modify_args *args)
{
	struct route_map *rm;
	bool disabled = yang_dnode_get_bool(args->dnode, NULL);

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rm = nb_running_get_entry(args->dnode, NULL, true);
		rm->optimization_disabled = disabled;
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry
 */
/* 创建一条 entry（XPath …/route-map/entry）。
 * APPLY 时读 sequence 与 action；action 是 YANG 枚举：0=permit / 其它=deny，
 * 映射成内部 RMAP_PERMIT / RMAP_DENY。再从父节点取回 route_map，调用
 * route_map_index_get(rm, action, sequence) 得到（或新建）route_map_index，
 * 并登记到 YANG 节点。
 * 坑：action 的 0/1 语义与 YANG 枚举定义绑定，若模型改动这里要同步。 */
static int lib_route_map_entry_create(struct nb_cb_create_args *args)
{
	struct route_map_index *rmi;
	struct route_map *rm;
	uint16_t sequence;
	int action;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		sequence = yang_dnode_get_uint16(args->dnode, "sequence");
		action = yang_dnode_get_enum(args->dnode, "action") == 0
				 ? RMAP_PERMIT
				 : RMAP_DENY;
		rm = nb_running_get_entry(args->dnode, NULL, true);
		rmi = route_map_index_get(rm, action, sequence);
		nb_running_set_entry(args->dnode, rmi);
		break;
	}

	return NB_OK;
}

/* 删除一条 entry：APPLY 时解绑并拿到 route_map_index，调用
 * route_map_index_delete(rmi, 1)。第二个参数 1 通常表示“同时执行 delete
 * 依赖事件”，即把该 entry 相关的依赖关系一并清理。 */
static int lib_route_map_entry_destroy(struct nb_cb_destroy_args *args)
{
	struct route_map_index *rmi;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_unset_entry(args->dnode);
		route_map_index_delete(rmi, 1);
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/description
 */
/* 设置/修改 entry 的描述文本（XPath …/entry/description）。
 * 这是“两阶段提交 + 资源”写法的范例：
 *   - PREPARE 阶段：把值 XSTRDUP 到 args->resource->ptr（先备好新值，不动旧值），
 *     分配失败返回 NB_ERR_RESOURCE；
 *   - ABORT 阶段：事务中止，释放刚分配的副本；
 *   - APPLY 阶段：先释放旧的 rmi->description，再接管 resource->ptr。
 * 这样保证任意阶段失败都不会留下半新半旧的脏状态。 */
static int
lib_route_map_entry_description_modify(struct nb_cb_modify_args *args)
{
	struct route_map_index *rmi;
	const char *description;

	switch (args->event) {
	case NB_EV_VALIDATE:
		/* NOTHING */
		break;
	case NB_EV_PREPARE:
		description = yang_dnode_get_string(args->dnode, NULL);
		args->resource->ptr = XSTRDUP(MTYPE_TMP, description);
		if (args->resource->ptr == NULL)
			return NB_ERR_RESOURCE;
		break;
	case NB_EV_ABORT:
		XFREE(MTYPE_TMP, args->resource->ptr);
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		XFREE(MTYPE_TMP, rmi->description);
		rmi->description = args->resource->ptr;
		break;
	}

	return NB_OK;
}

/* 删除 entry 描述文本：APPLY 时释放 rmi->description 即可（无两阶段需要，
 * 因为不涉及新值分配）。 */
static int
lib_route_map_entry_description_destroy(struct nb_cb_destroy_args *args)
{
	struct route_map_index *rmi;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		XFREE(MTYPE_TMP, rmi->description);
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/action
 */
/* 修改 entry 的 permit/deny 动作（XPath …/entry/action）。
 * APPLY 时把枚举值写入 rmi->type，然后触发 route_map_master.event_hook，
 * 并 route_map_notify_dependencies(..., RMAP_EVENT_CALL_ADDED) 通知依赖方
 * （例如依赖此 route-map 的过滤/策略需要重算）。
 * 坑：这里广播的是 CALL_ADDED，属于复用的“变更通知”，不必与字面含义一致。 */
static int lib_route_map_entry_action_modify(struct nb_cb_modify_args *args)
{
	struct route_map_index *rmi;
	struct route_map *map;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		rmi->type = yang_dnode_get_enum(args->dnode, NULL);
		map = rmi->map;

		/* Execute event hook. */
		if (route_map_master.event_hook) {
			(*route_map_master.event_hook)(map->name);
			route_map_notify_dependencies(map->name,
						      RMAP_EVENT_CALL_ADDED);
		}

		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/call
 */
/* 设置 entry 的 call（跳转到另一条 route-map，XPath …/entry/call）。
 * VALIDATE：禁止指向自己（比对 ../../name 与本节点值），并留 TODO 说明尚未
 *   检测环形跳转（call A→B→A 之类）。
 * PREPARE：把目标名 XSTRDUP 成资源；ABORT：释放；APPLY：若已有旧目标，
 *   先撤旧依赖并释放，再装新目标并登记新依赖。
 * 关键：rmi->nextrm 保存目标 route-map 名；依赖关系用 route_map_upd8_dependency
 * 维护（CALL_ADDED / CALL_DELETED），保证目标 route-map 变动时能级联更新。 */
static int lib_route_map_entry_call_modify(struct nb_cb_modify_args *args)
{
	struct route_map_index *rmi;
	const char *rm_name, *rmn_name;

	switch (args->event) {
	case NB_EV_VALIDATE:
		rm_name = yang_dnode_get_string(args->dnode, "../../name");
		rmn_name = yang_dnode_get_string(args->dnode, NULL);
		/* Don't allow to jump to the same route map instance. */
		if (strcmp(rm_name, rmn_name) == 0)
			return NB_ERR_VALIDATION;

		/* TODO: detect circular route map sequences. */
		break;
	case NB_EV_PREPARE:
		rmn_name = yang_dnode_get_string(args->dnode, NULL);
		args->resource->ptr = XSTRDUP(MTYPE_ROUTE_MAP_NAME, rmn_name);
		break;
	case NB_EV_ABORT:
		XFREE(MTYPE_ROUTE_MAP_NAME, args->resource->ptr);
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		if (rmi->nextrm) {
			route_map_upd8_dependency(RMAP_EVENT_CALL_DELETED,
						  rmi->nextrm, rmi->map->name);
			XFREE(MTYPE_ROUTE_MAP_NAME, rmi->nextrm);
		}
		rmi->nextrm = args->resource->ptr;
		route_map_upd8_dependency(RMAP_EVENT_CALL_ADDED, rmi->nextrm,
					  rmi->map->name);
		break;
	}

	return NB_OK;
}

/* 删除 call：APPLY 时撤销对目标 route-map 的依赖、释放 rmi->nextrm，置 NULL。 */
static int lib_route_map_entry_call_destroy(struct nb_cb_destroy_args *args)
{
	struct route_map_index *rmi;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		route_map_upd8_dependency(RMAP_EVENT_CALL_DELETED, rmi->nextrm,
					  rmi->map->name);
		XFREE(MTYPE_ROUTE_MAP_NAME, rmi->nextrm);
		rmi->nextrm = NULL;
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/exit-policy
 */
/* 修改 entry 的 exit-policy（XPath …/entry/exit-policy），即 on-match 行为。
 * VALIDATE：若策略为 next / goto，且本 entry 是 deny，则非法（deny 的结果
 *   无需再跳转），返回 NB_ERR_VALIDATION。
 * APPLY：把 YANG 枚举 0/1/2 映射为内部 RMAP_EXIT / RMAP_NEXT / RMAP_GOTO，
 *   然后触发 event_hook 与依赖通知。
 * 坑：exit-policy 与 goto-value 是配套的，goto 时必须再改 goto-value 才有意义。 */
static int
lib_route_map_entry_exit_policy_modify(struct nb_cb_modify_args *args)
{
	struct route_map_index *rmi;
	struct route_map *map;
	int rm_action;
	int policy;

	switch (args->event) {
	case NB_EV_VALIDATE:
		policy = yang_dnode_get_enum(args->dnode, NULL);
		switch (policy) {
		case 0: /* permit-or-deny */
			break;
		case 1: /* next */
		case 2: /* goto */
			rm_action =
				yang_dnode_get_enum(args->dnode, "../action");
			if (rm_action == 1 /* deny */) {
				/*
				 * On deny it is not possible to 'goto'
				 * anywhere.
				 */
				return NB_ERR_VALIDATION;
			}
			break;
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		map = rmi->map;
		policy = yang_dnode_get_enum(args->dnode, NULL);

		switch (policy) {
		case 0: /* permit-or-deny */
			rmi->exitpolicy = RMAP_EXIT;
			break;
		case 1: /* next */
			rmi->exitpolicy = RMAP_NEXT;
			break;
		case 2: /* goto */
			rmi->exitpolicy = RMAP_GOTO;
			break;
		}

		/* Execute event hook. */
		if (route_map_master.event_hook) {
			(*route_map_master.event_hook)(map->name);
			route_map_notify_dependencies(map->name,
						      RMAP_EVENT_CALL_ADDED);
		}

		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/goto-value
 */
/* 修改 goto 的目标序号（XPath …/entry/goto-value）。
 * VALIDATE：目标序号必须大于本 entry 的 sequence（../sequence），即只允许
 *   向后跳，禁止回跳，否则 NB_ERR_VALIDATION。 */
static int lib_route_map_entry_goto_value_modify(struct nb_cb_modify_args *args)
{
	struct route_map_index *rmi;
	uint16_t rmi_index;
	uint16_t rmi_next;

	switch (args->event) {
	case NB_EV_VALIDATE:
		rmi_index = yang_dnode_get_uint16(args->dnode, "../sequence");
		rmi_next = yang_dnode_get_uint16(args->dnode, NULL);
		if (rmi_next <= rmi_index) {
			/* Can't jump backwards on a route map. */
			return NB_ERR_VALIDATION;
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		rmi->nextpref = yang_dnode_get_uint16(args->dnode, NULL);
		break;
	}

	return NB_OK;
}

/* 删除 goto-value：APPLY 时把 rmi->nextpref 清零（回到默认 0）。 */
static int
lib_route_map_entry_goto_value_destroy(struct nb_cb_destroy_args *args)
{
	struct route_map_index *rmi;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		rmi->nextpref = 0;
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition
 */
/* ==== match 条件回调的通用套路（本文件后半大量重复）=====================
 * 每个 match 子节点（interface / list-name / ipv4-next-hop-type …）的 modify
 * 回调都遵循同一套“注册 + 记录撤销信息”的模式：
 *   1) 只在 NB_EV_APPLY 下工作；
 *   2) 若对应模块的钩子未安装（rmap_match_set_hook.xxx == NULL），直接返回，
 *      表示“这个条件无人处理”，交给别的模块或忽略；
 *   3) 从 YANG 节点取回创建 match-condition 时登记的 rhc；
 *   4) 在 rhc 上记录“撤销信息”：rhc_mhook（撤销用的钩子）、rhc_rule（规则名）、
 *      rhc_event（删除事件），供日后 destroy 时回放；
 *   5) 调用模块的 match 钩子真正安装该条件；失败则清掉 rhc_mhook 并返回
 *      NB_ERR_INCONSISTENCY。
 * IS_MATCH_* / rmap_match_set_hook 是 routemap.h 提供的判断宏与钩子表。
 * ======================================================================= */

/* 创建 match-condition 列表项：给 entry 新建一个 hook 上下文 rhc 并绑定到该
 * YANG 节点。后续子字段（interface 等）的 modify 回调会复用这个 rhc。 */
static int
lib_route_map_entry_match_condition_create(struct nb_cb_create_args *args)
{
	struct routemap_hook_context *rhc;
	struct route_map_index *rmi;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(args->dnode, NULL, true);
		rhc = routemap_hook_context_insert(rmi);
		nb_running_set_entry(args->dnode, rhc);
		break;
	}

	return NB_OK;
}

/* 删除整个 match-condition：先调用它记录的撤销钩子（lib_route_map_entry_match_destroy
 * 会回放 rhc_mhook），再解绑并释放 rhc。两步都要做：前者撤销业务侧动作，
 * 后者回收内存。 */
static int
lib_route_map_entry_match_condition_destroy(struct nb_cb_destroy_args *args)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	rv = lib_route_map_entry_match_destroy(args);
	rhc = nb_running_unset_entry(args->dnode);
	routemap_hook_context_free(rhc);

	return rv;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/interface
 */
/* 安装 “match interface IFNAME” 条件（XPath …/match-condition/interface）。
 * APPLY 时先查钩子 match_interface 是否安装；取 rhc 与接口名；在 rhc 上登记
 * 撤销信息（mhook=no_match_interface，rule="interface"，事件 MATCH_DELETED），
 * 然后调用钩子安装，事件为 RMAP_EVENT_MATCH_ADDED。失败则回退 rhc_mhook。 */
static int lib_route_map_entry_match_condition_interface_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *ifname;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_interface == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	ifname = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_interface;
	rhc->rhc_rule = "interface";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_interface(rhc->rhc_rmi,
						 "interface", ifname,
						 RMAP_EVENT_MATCH_ADDED,
						 args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 “match interface”：直接复用通用的 match 撤销函数（回放 rhc_mhook）。 */
static int lib_route_map_entry_match_condition_interface_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_match_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/list-name
 */
/* 安装“带列表名”的一类 match 条件（XPath …/match-condition/list-name）。
 * 这类条件共用同一个 list-name 叶子，靠父节点的 condition（identityref）区分
 * 具体语义，因此本函数先读 ../../condition，再用一串 IS_MATCH_* 分派到不同钩子：
 *   IPv4/IPv6 目的地址、下一跳，及其 prefix-list 变体……
 * 每个分支都：查对应钩子是否安装 → 登记撤销信息（mhook / rule / 事件）→ 调用
 * 钩子安装（ADDED 事件）。注意 ADDED/DELETED 事件成对，且 filter 类与 plist
 * 类用不同事件常量（RMAP_EVENT_FILTER_* 与 RMAP_EVENT_PLIST_*）。
 * 末尾 else 置 rv=CMD_ERR_NO_MATCH，表示 condition 不是已知类型；只要 rv 非
 * CMD_SUCCESS 就回退并返回 NB_ERR_INCONSISTENCY。 */
static int lib_route_map_entry_match_condition_list_name_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *acl;
	const char *condition;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook installation, otherwise we can just stop. */
	acl = yang_dnode_get_string(args->dnode, NULL);
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	condition = yang_dnode_get_string(args->dnode, "../../condition");

	if (IS_MATCH_IPv4_ADDRESS_LIST(condition)) {
		if (rmap_match_set_hook.match_ip_address == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_address;
		rhc->rhc_rule = "ip address";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ip_address(
			rhc->rhc_rmi, "ip address", acl,
			RMAP_EVENT_FILTER_ADDED,
			args->errmsg, args->errmsg_len);
	} else if (IS_MATCH_IPv4_PREFIX_LIST(condition)) {
		if (rmap_match_set_hook.match_ip_address_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ip_address_prefix_list;
		rhc->rhc_rule = "ip address prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ip_address_prefix_list(
			rhc->rhc_rmi, "ip address prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED,
			args->errmsg, args->errmsg_len);
	} else if (IS_MATCH_IPv4_NEXTHOP_LIST(condition)) {
		if (rmap_match_set_hook.match_ip_next_hop == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_next_hop;
		rhc->rhc_rule = "ip next-hop";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ip_next_hop(
			rhc->rhc_rmi, "ip next-hop", acl,
			RMAP_EVENT_FILTER_ADDED,
			args->errmsg, args->errmsg_len);
	} else if (IS_MATCH_IPv6_NEXTHOP_LIST(condition)) {
		if (rmap_match_set_hook.match_ipv6_next_hop == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ipv6_next_hop;
		rhc->rhc_rule = "ipv6 next-hop";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ipv6_next_hop(
			rhc->rhc_rmi, "ipv6 next-hop", acl,
			RMAP_EVENT_FILTER_ADDED, args->errmsg,
			args->errmsg_len);
	} else if (IS_MATCH_IPv4_NEXTHOP_PREFIX_LIST(condition)) {
		if (rmap_match_set_hook.match_ip_next_hop_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ip_next_hop_prefix_list;
		rhc->rhc_rule = "ip next-hop prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ip_next_hop_prefix_list(
			rhc->rhc_rmi, "ip next-hop prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED,
			args->errmsg, args->errmsg_len);
	} else if (IS_MATCH_IPv6_NEXTHOP_PREFIX_LIST(condition)) {
		if (rmap_match_set_hook.match_ipv6_next_hop_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ipv6_next_hop_prefix_list;
		rhc->rhc_rule = "ipv6 next-hop prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ipv6_next_hop_prefix_list(
			rhc->rhc_rmi, "ipv6 next-hop prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED, args->errmsg, args->errmsg_len);
	} else if (IS_MATCH_IPv6_ADDRESS_LIST(condition)) {
		if (rmap_match_set_hook.match_ipv6_address == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ipv6_address;
		rhc->rhc_rule = "ipv6 address";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ipv6_address(
			rhc->rhc_rmi, "ipv6 address", acl,
			RMAP_EVENT_FILTER_ADDED,
			args->errmsg, args->errmsg_len);
	} else if (IS_MATCH_IPv6_PREFIX_LIST(condition)) {
		if (rmap_match_set_hook.match_ipv6_address_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ipv6_address_prefix_list;
		rhc->rhc_rule = "ipv6 address prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ipv6_address_prefix_list(
			rhc->rhc_rmi, "ipv6 address prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED,
			args->errmsg, args->errmsg_len);
	} else
		rv = CMD_ERR_NO_MATCH;

	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 list-name 类 match 条件：同样复用通用 match 撤销函数。 */
static int lib_route_map_entry_match_condition_list_name_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_match_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/ipv4-next-hop-type
 */
/* 安装 “match ip next-hop type” 条件（XPath …/match-condition/ipv4-next-hop-type）。
 * 套路同前：查 match_ip_next_hop_type 钩子 → 登记撤销信息（事件 MATCH_DELETED）
 * → 以 MATCH_ADDED 安装。 */
static int lib_route_map_entry_match_condition_ipv4_next_hop_type_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_ip_next_hop_type == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	type = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_next_hop_type;
	rhc->rhc_rule = "ip next-hop type";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_ip_next_hop_type(
		rhc->rhc_rmi, "ip next-hop type", type,
		RMAP_EVENT_MATCH_ADDED,
		args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 “match ip next-hop type”：复用通用 match 撤销函数。 */
static int lib_route_map_entry_match_condition_ipv4_next_hop_type_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_match_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/ipv6-next-hop-type
 */
/* 安装 “match ipv6 next-hop type” 条件（XPath …/match-condition/ipv6-next-hop-type）。
 * 与 IPv4 版对照阅读即可，仅钩子名与路径不同。 */
static int lib_route_map_entry_match_condition_ipv6_next_hop_type_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_ipv6_next_hop_type == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	type = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_ipv6_next_hop_type;
	rhc->rhc_rule = "ipv6 next-hop type";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_ipv6_next_hop_type(
		rhc->rhc_rmi, "ipv6 next-hop type", type,
		RMAP_EVENT_MATCH_ADDED,
		args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 “match ipv6 next-hop type”：复用通用 match 撤销函数。 */
static int lib_route_map_entry_match_condition_ipv6_next_hop_type_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_match_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/metric
 */
/* 安装 “match metric” 条件：套路同 interface 版（查钩子 → 登记撤销信息 →
 * 以 MATCH_ADDED 安装）。 */
static int lib_route_map_entry_match_condition_metric_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_metric == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	type = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_metric;
	rhc->rhc_rule = "metric";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_metric(rhc->rhc_rmi, "metric",
					      type, RMAP_EVENT_MATCH_ADDED,
					      args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 “match metric”：复用通用 match 撤销函数。 */
static int lib_route_map_entry_match_condition_metric_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_match_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/tag
 */
/* 安装 “match tag” 条件：套路同前。注意 tag 的值是以字符串形式传给钩子的。 */
static int
lib_route_map_entry_match_condition_tag_modify(struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *tag;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_tag == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	tag = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_tag;
	rhc->rhc_rule = "tag";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_tag(rhc->rhc_rmi, "tag", tag,
					   RMAP_EVENT_MATCH_ADDED,
					   args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 “match tag”：复用通用 match 撤销函数。 */
static int
lib_route_map_entry_match_condition_tag_destroy(struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_match_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action
 */
/* ==== set 动作回调（与 match 完全对称）===================================
 * set-action 与 match-condition 结构相同（都是带 key 的 list），因此 set-action
 * 的 create/destroy 直接复用 match-condition 的实现（见下方 create 的转发）。
 * 各 set 子字段的 modify 回调套路也与 match 相同，区别在于：
 *   - 撤销信息记录在 rhc_shook（set 钩子）而非 rhc_mhook（match 钩子）；
 *   - 调用 rmap_match_set_hook.set_xxx()，一般不再需要单独的“事件”参数。
 * ===================================================================== */

/* 创建 set-action 列表项：直接复用 match-condition_create（给 entry 建 rhc 并绑定）。 */
static int lib_route_map_entry_set_action_create(struct nb_cb_create_args *args)
{
	return lib_route_map_entry_match_condition_create(args);
}

/* 删除 set-action：先回放 set 撤销钩子（lib_route_map_entry_set_destroy），
 * 再解绑并释放 rhc。与 match 版本对称。 */
static int
lib_route_map_entry_set_action_destroy(struct nb_cb_destroy_args *args)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	rv = lib_route_map_entry_set_destroy(args);
	rhc = nb_running_unset_entry(args->dnode);
	routemap_hook_context_free(rhc);

	return rv;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/ipv4-address
 */
/* 设置 set-action 的 IPv4 地址（XPath …/set-action/ipv4-address），即 set ip next-hop。
 * VALIDATE：解析地址并要求是合法单播且非 0.0.0.0，否则 NB_ERR_VALIDATION
 *   （注释说明当前只有 ipv4-next-hop 一种动作，无需再判 action）。
 * APPLY：查 set_ip_nexthop 钩子 → 在 rhc_shook 记录撤销钩子 → 调用安装。
 * 返回：NB_OK / NB_ERR_VALIDATION / NB_ERR_INCONSISTENCY。 */
static int lib_route_map_entry_set_action_ipv4_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *address;
	struct in_addr ia;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
		/*
		 * NOTE: validate if 'action' is 'ipv4-next-hop',
		 * currently it is not necessary because this is the
		 * only implemented action.
		 */
		yang_dnode_get_ipv4(&ia, args->dnode, NULL);
		if (ia.s_addr == INADDR_ANY || !ipv4_unicast_valid(&ia))
			return NB_ERR_VALIDATION;
		return NB_OK;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		return NB_OK;
	case NB_EV_APPLY:
		break;
	}

	/* Check for hook function. */
	if (rmap_match_set_hook.set_ip_nexthop == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	address = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_ip_nexthop;
	rhc->rhc_rule = "ip next-hop";

	rv = rmap_match_set_hook.set_ip_nexthop(rhc->rhc_rmi, "ip next-hop",
						address,
						args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 set ip next-hop 的 IPv4 地址：复用通用 set 撤销函数。 */
static int lib_route_map_entry_set_action_ipv4_address_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/ipv6-address
 */
/* 设置 set-action 的 IPv6 地址（XPath …/set-action/ipv6-address），即
 * set ipv6 next-hop local。VALIDATE 要求地址必须是链路本地地址
 * （IN6_IS_ADDR_LINKLOCAL），否则 NB_ERR_VALIDATION。 */
static int lib_route_map_entry_set_action_ipv6_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *address;
	struct in6_addr i6a;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
		/*
		 * NOTE: validate if 'action' is 'ipv6-next-hop',
		 * currently it is not necessary because this is the
		 * only implemented action. Other actions might have
		 * different validations.
		 */
		yang_dnode_get_ipv6(&i6a, args->dnode, NULL);
		if (!IN6_IS_ADDR_LINKLOCAL(&i6a))
			return NB_ERR_VALIDATION;
		return NB_OK;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		return NB_OK;
	case NB_EV_APPLY:
		break;
	}

	/* Check for hook function. */
	if (rmap_match_set_hook.set_ipv6_nexthop_local == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	address = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_ipv6_nexthop_local;
	rhc->rhc_rule = "ipv6 next-hop local";

	rv = rmap_match_set_hook.set_ipv6_nexthop_local(
		rhc->rhc_rmi, "ipv6 next-hop local", address,
		args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 撤销 set ipv6 next-hop local 的 IPv6 地址：复用通用 set 撤销函数。 */
static int lib_route_map_entry_set_action_ipv6_address_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/value
 */
/* ==== set metric 的共享实现（多个 YANG 叶子汇到这一个函数）==============
 * set metric 在 YANG 里被拆成多个叶子（value / add-metric / subtract-metric /
 * use-round-trip-time / use-igp / use-aigp / add-round-trip-time /
 * subtract-round-trip-time），但它们最终都表示“设置 metric”，只是形态不同。
 * 因此每个叶子的 modify 回调先把值规格化成字符串（如 "+5"、"-3"、"rtt"、
 * "igp"、"aigp"、"+rtt"、"-rtt"），再统一调用本函数 set_action_modify()。
 * 本函数即“公共落地点”：登记撤销钩子 no_set_metric、规则名 "metric"，并调用
 * rmap_match_set_hook.set_metric() 安装。
 * 参数：value 是已格式化好的 metric 字符串；其余同标准回调。
 * ===================================================================== */

/* 公共实现（见上方说明）。 */
static int set_action_modify(enum nb_event event, const struct lyd_node *dnode,
			     union nb_resource *resource, const char *value,
			     char *errmsg, size_t errmsg_len)
{
	struct routemap_hook_context *rhc;
	int rv;

	/*
	 * NOTE: validate if 'action' is 'metric', currently it is not
	 * necessary because this is the only implemented action. Other
	 * actions might have different validations.
	 */
	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_metric == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_metric;
	rhc->rhc_rule = "metric";

	rv = rmap_match_set_hook.set_metric(rhc->rhc_rmi, "metric",
					    value,
					    errmsg, errmsg_len
					    );
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* set metric（普通数值形态 value）：把节点值原样交给 set_action_modify。 */
static int
lib_route_map_entry_set_action_value_modify(struct nb_cb_modify_args *args)
{
	const char *metric = yang_dnode_get_string(args->dnode, NULL);

	return set_action_modify(args->event, args->dnode, args->resource,
				 metric, args->errmsg, args->errmsg_len);
}

/* 删除 set metric（普通值）：复用通用 set 撤销函数。这个 destroy 被 metric 各
 * 形态共用（add/subtract/rtt/igp/aigp 等都转发到它）。 */
static int
lib_route_map_entry_set_action_value_destroy(struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/min-metric
 */
/* set min-metric 的实现：登记撤销钩子 no_set_min_metric、规则名 "min-metric"，
 * 调用 set_min_metric 钩子安装。与 set_action_modify 同构，只是目标不同。 */
static int set_action_min_metric_modify(enum nb_event event,
					const struct lyd_node *dnode,
					union nb_resource *resource,
					const char *value, char *errmsg,
					size_t errmsg_len)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_min_metric == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_min_metric;
	rhc->rhc_rule = "min-metric";

	rv = rmap_match_set_hook.set_min_metric(rhc->rhc_rmi, "min-metric",
						value, errmsg, errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* set min-metric 的 YANG modify 包装：取节点值后转调上面的实现。 */
static int
lib_route_map_entry_set_action_min_metric_modify(struct nb_cb_modify_args *args)
{
	const char *min_metric = yang_dnode_get_string(args->dnode, NULL);

	return set_action_min_metric_modify(args->event, args->dnode,
					    args->resource, min_metric,
					    args->errmsg, args->errmsg_len);
}

/* 删除 set min-metric：复用通用 set 撤销函数。 */
static int lib_route_map_entry_set_action_min_metric_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/max-metric
 */
/* set max-metric 的实现：登记撤销钩子 no_set_max_metric、规则名 "max-metric"，
 * 调用 set_max_metric 钩子安装。 */
static int set_action_max_metric_modify(enum nb_event event,
					const struct lyd_node *dnode,
					union nb_resource *resource,
					const char *value, char *errmsg,
					size_t errmsg_len)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_max_metric == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_max_metric;
	rhc->rhc_rule = "max-metric";

	rv = rmap_match_set_hook.set_max_metric(rhc->rhc_rmi, "max-metric",
						value, errmsg, errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* set max-metric 的 YANG modify 包装：取节点值后转调上面的实现。 */
static int
lib_route_map_entry_set_action_max_metric_modify(struct nb_cb_modify_args *args)
{
	const char *max_metric = yang_dnode_get_string(args->dnode, NULL);

	return set_action_max_metric_modify(args->event, args->dnode,
					    args->resource, max_metric,
					    args->errmsg, args->errmsg_len);
}

/* 删除 set max-metric：复用通用 set 撤销函数。 */
static int lib_route_map_entry_set_action_max_metric_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/add-metric
 */
/* set metric +N（add-metric）：VALIDATE 时禁止加 0；把值规格化成 "+N" 后
 * 交给 set_action_modify（最终仍走 set_metric 钩子）。 */
static int
lib_route_map_entry_set_action_add_metric_modify(struct nb_cb_modify_args *args)
{
	char metric_str[16];

	if (args->event == NB_EV_VALIDATE
	    && yang_dnode_get_uint32(args->dnode, NULL) == 0) {
		snprintf(args->errmsg, args->errmsg_len,
			 "Can't add zero to metric");
		return NB_ERR_VALIDATION;
	}

	snprintf(metric_str, sizeof(metric_str), "+%s",
		 yang_dnode_get_string(args->dnode, NULL));
	return set_action_modify(args->event, args->dnode, args->resource,
				 metric_str,
				 args->errmsg, args->errmsg_len);
}

/* 删除 set metric +N：转发到 value_destroy（同样走通用 set 撤销）。 */
static int lib_route_map_entry_set_action_add_metric_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/subtract-metric
 */
/* set metric -N（subtract-metric）：VALIDATE 时禁止减 0；把值规格化成 "-N"
 * 后交给 set_action_modify。 */
static int lib_route_map_entry_set_action_subtract_metric_modify(
	struct nb_cb_modify_args *args)
{
	char metric_str[16];

	if (args->event == NB_EV_VALIDATE
	    && yang_dnode_get_uint32(args->dnode, NULL) == 0) {
		snprintf(args->errmsg, args->errmsg_len,
			 "Can't subtract zero from metric");
		return NB_ERR_VALIDATION;
	}

	snprintf(metric_str, sizeof(metric_str), "-%s",
		 yang_dnode_get_string(args->dnode, NULL));
	return set_action_modify(args->event, args->dnode, args->resource,
				 metric_str,
				 args->errmsg, args->errmsg_len);
}

/* 删除 set metric -N：转发到 value_destroy。 */
static int lib_route_map_entry_set_action_subtract_metric_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/use-round-trip-time
 */
/* set metric rtt（use-round-trip-time）：把固定字符串 "rtt" 交给 set_action_modify。 */
static int lib_route_map_entry_set_action_use_round_trip_time_modify(
	struct nb_cb_modify_args *args)
{
	return set_action_modify(args->event, args->dnode, args->resource,
				 "rtt",
				 args->errmsg, args->errmsg_len);
}

/* 删除 set metric rtt：转发到 value_destroy。 */
static int lib_route_map_entry_set_action_use_round_trip_time_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/use-igp
 */
/* set metric igp（use-igp）：把固定字符串 "igp" 交给 set_action_modify。 */
static int lib_route_map_entry_set_action_use_igp_modify(struct nb_cb_modify_args *args)
{
	return set_action_modify(args->event, args->dnode, args->resource, "igp", args->errmsg,
				 args->errmsg_len);
}

/* 删除 set metric igp：转发到 value_destroy。 */
static int lib_route_map_entry_set_action_use_igp_destroy(struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/use-aigp
 */
/* set metric aigp（use-aigp）：把固定字符串 "aigp" 交给 set_action_modify。 */
static int lib_route_map_entry_set_action_use_aigp_modify(struct nb_cb_modify_args *args)
{
	return set_action_modify(args->event, args->dnode, args->resource, "aigp", args->errmsg,
				 args->errmsg_len);
}

/* 删除 set metric aigp：转发到 value_destroy。 */
static int lib_route_map_entry_set_action_use_aigp_destroy(struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/add-round-trip-time
 */
/* set metric +rtt（add-round-trip-time）：把固定字符串 "+rtt" 交给 set_action_modify。 */
static int lib_route_map_entry_set_action_add_round_trip_time_modify(
	struct nb_cb_modify_args *args)
{
	return set_action_modify(args->event, args->dnode, args->resource,
				 "+rtt",
				 args->errmsg, args->errmsg_len);
}

/* 删除 set metric +rtt：转发到 value_destroy。 */
static int lib_route_map_entry_set_action_add_round_trip_time_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/subtract-round-trip-time
 */
/* set metric -rtt（subtract-round-trip-time）：把固定字符串 "-rtt" 交给 set_action_modify。 */
static int lib_route_map_entry_set_action_subtract_round_trip_time_modify(
	struct nb_cb_modify_args *args)
{
	return set_action_modify(args->event, args->dnode, args->resource,
				 "-rtt", args->errmsg, args->errmsg_len);
}

/* 删除 set metric -rtt：转发到 value_destroy。 */
static int lib_route_map_entry_set_action_subtract_round_trip_time_destroy(
	struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_action_value_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/tag
 */
/* 设置 set tag（XPath …/set-action/tag）：查 set_tag 钩子 → 在 rhc_shook 记录
 * 撤销钩子 no_set_tag、规则名 "tag" → 调用安装。 */
static int
lib_route_map_entry_set_action_tag_modify(struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *tag;
	int rv;

	/*
	 * NOTE: validate if 'action' is 'tag', currently it is not
	 * necessary because this is the only implemented action. Other
	 * actions might have different validations.
	 */
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_tag == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	tag = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_tag;
	rhc->rhc_rule = "tag";

	rv = rmap_match_set_hook.set_tag(rhc->rhc_rmi, "tag", tag,
					 args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 删除 set tag：复用通用 set 撤销函数。 */
static int
lib_route_map_entry_set_action_tag_destroy(struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/policy
 */
/* 设置 set sr-te color 的策略名（XPath …/set-action/policy）。
 * 注意（易踩的坑 / 疑似历史遗留）：这里检查的是 set_srte_color 钩子是否安装，
 * 但记录撤销钩子用的是 no_set_tag，并调用 set_tag 钩子（规则名写 "sr-te color"）。
 * 也就是说 SR-TE color 目前复用了 set_tag 的钩子通道，阅读时不要被函数名误导。 */
static int
lib_route_map_entry_set_action_policy_modify(struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *policy;
	int rv;

	/*
	 * NOTE: validate if 'action' is 'tag', currently it is not
	 * necessary because this is the only implemented action. Other
	 * actions might have different validations.
	 */
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_srte_color == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	policy = yang_dnode_get_string(args->dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_tag;
	rhc->rhc_rule = "sr-te color";

	rv = rmap_match_set_hook.set_tag(rhc->rhc_rmi, "sr-te color", policy,
			args->errmsg, args->errmsg_len);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 删除 set sr-te color 策略名：复用通用 set 撤销函数。 */
static int
lib_route_map_entry_set_action_policy_destroy(struct nb_cb_destroy_args *args)
{
	return lib_route_map_entry_set_destroy(args);
}

/* frr-route-map 模块的 YANG 回调注册表：把每条节点 XPath 映射到本文件实现的
 * 回调（create / destroy / modify / cli_show / cli_cmp / cli_show_end 等）。
 * northbound 框架在加载 frr-route-map.yang 后按此表绑定回调；CLI 提交的改动
 * 沿 XPath 找到对应项，再调用对应函数。
 * .nodes 数组以 { .xpath = NULL } 哨兵结尾；clang-format 前后各有一对
 * on/off 注释，防止格式化工具重整这张大表（保持可读的对齐）。
 * 阅读提示：本表就是“函数 → 作用”的总索引，可对照前面每个函数上方的注释。 */
/* clang-format off */
const struct frr_yang_module_info frr_route_map_info = {
	.name = "frr-route-map",
	.nodes = {
		{
			.xpath = "/frr-route-map:lib/route-map",
			.cbs = {
				.create = lib_route_map_create,
				.destroy = lib_route_map_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/optimization-disabled",
			.cbs = {
				.modify = lib_route_map_optimization_disabled_modify,
				.cli_show = route_map_optimization_disabled_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry",
			.cbs = {
				.create = lib_route_map_entry_create,
				.destroy = lib_route_map_entry_destroy,
				.cli_cmp = route_map_instance_cmp,
				.cli_show = route_map_instance_show,
				.cli_show_end = route_map_instance_show_end,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/description",
			.cbs = {
				.modify = lib_route_map_entry_description_modify,
				.destroy = lib_route_map_entry_description_destroy,
				.cli_show = route_map_description_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/action",
			.cbs = {
				.modify = lib_route_map_entry_action_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/call",
			.cbs = {
				.modify = lib_route_map_entry_call_modify,
				.destroy = lib_route_map_entry_call_destroy,
				.cli_show = route_map_call_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/exit-policy",
			.cbs = {
				.modify = lib_route_map_entry_exit_policy_modify,
				.cli_show = route_map_exit_policy_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/goto-value",
			.cbs = {
				.modify = lib_route_map_entry_goto_value_modify,
				.destroy = lib_route_map_entry_goto_value_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition",
			.cbs = {
				.create = lib_route_map_entry_match_condition_create,
				.destroy = lib_route_map_entry_match_condition_destroy,
				.cli_show = route_map_condition_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/interface",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_interface_modify,
				.destroy = lib_route_map_entry_match_condition_interface_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/list-name",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_list_name_modify,
				.destroy = lib_route_map_entry_match_condition_list_name_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/ipv4-next-hop-type",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_ipv4_next_hop_type_modify,
				.destroy = lib_route_map_entry_match_condition_ipv4_next_hop_type_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/ipv6-next-hop-type",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_ipv6_next_hop_type_modify,
				.destroy = lib_route_map_entry_match_condition_ipv6_next_hop_type_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/metric",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_metric_modify,
				.destroy = lib_route_map_entry_match_condition_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/tag",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_tag_modify,
				.destroy = lib_route_map_entry_match_condition_tag_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action",
			.cbs = {
				.create = lib_route_map_entry_set_action_create,
				.destroy = lib_route_map_entry_set_action_destroy,
				.cli_show = route_map_action_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/ipv4-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_ipv4_address_modify,
				.destroy = lib_route_map_entry_set_action_ipv4_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/ipv6-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_ipv6_address_modify,
				.destroy = lib_route_map_entry_set_action_ipv6_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/value",
			.cbs = {
				.modify = lib_route_map_entry_set_action_value_modify,
				.destroy = lib_route_map_entry_set_action_value_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/min-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_min_metric_modify,
				.destroy = lib_route_map_entry_set_action_min_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/max-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_max_metric_modify,
				.destroy = lib_route_map_entry_set_action_max_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/add-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_add_metric_modify,
				.destroy = lib_route_map_entry_set_action_add_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/subtract-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_subtract_metric_modify,
				.destroy = lib_route_map_entry_set_action_subtract_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/use-round-trip-time",
			.cbs = {
				.modify = lib_route_map_entry_set_action_use_round_trip_time_modify,
				.destroy = lib_route_map_entry_set_action_use_round_trip_time_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/use-igp",
			.cbs = {
				.modify = lib_route_map_entry_set_action_use_igp_modify,
				.destroy = lib_route_map_entry_set_action_use_igp_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/use-aigp",
			.cbs = {
				.modify = lib_route_map_entry_set_action_use_aigp_modify,
				.destroy = lib_route_map_entry_set_action_use_aigp_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/add-round-trip-time",
			.cbs = {
				.modify = lib_route_map_entry_set_action_add_round_trip_time_modify,
				.destroy = lib_route_map_entry_set_action_add_round_trip_time_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/subtract-round-trip-time",
			.cbs = {
				.modify = lib_route_map_entry_set_action_subtract_round_trip_time_modify,
				.destroy = lib_route_map_entry_set_action_subtract_round_trip_time_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/tag",
			.cbs = {
				.modify = lib_route_map_entry_set_action_tag_modify,
				.destroy = lib_route_map_entry_set_action_tag_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/policy",
			.cbs = {
				.modify = lib_route_map_entry_set_action_policy_modify,
				.destroy = lib_route_map_entry_set_action_policy_destroy,
			}
		},

		{
			.xpath = NULL,
		},
	}
};

/* 第二张表：仅用于“配置回显”（CLI show）的注册表，名字同为 frr-route-map，
 * 但 .ignore_cfg_cbs = true 表示忽略“配置回调”（create/destroy/modify），
 * 只需 .cli_show / .cli_cmp 等显示类回调。
 * 为什么拆两张表：配置回显（libyang 遍历 running 配置生成命令）与“配置落地”
 * （真正改内部结构）是两条独立路径，后者靠 frr_route_map_info，前者靠本表。
 * 这样像 show running-config 这类只读展示不会触发任何状态修改。 */
const struct frr_yang_module_info frr_route_map_cli_info = {
	.name = "frr-route-map",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = "/frr-route-map:lib/route-map/optimization-disabled",
			.cbs.cli_show = route_map_optimization_disabled_show,
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry",
			.cbs = {
				.cli_cmp = route_map_instance_cmp,
				.cli_show = route_map_instance_show,
				.cli_show_end = route_map_instance_show_end,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/description",
			.cbs.cli_show = route_map_description_show,
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/call",
			.cbs.cli_show = route_map_call_show,
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/exit-policy",
			.cbs.cli_show = route_map_exit_policy_show,
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition",
			.cbs.cli_show = route_map_condition_show,
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action",
			.cbs.cli_show = route_map_action_show,
		},
		{
			.xpath = NULL,
		},
	}
};
