// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020        Vmware
 *                           Sarita Patra
 */
/* ============================================================================
 * 【文件导读】bgp_routemap_nb_config.c —— route-map 的 northbound(YANG) 配置回调实现
 * ============================================================================
 * 【职责与定位】
 *   本文件是 BGP 的 route-map 配置在新配置框架(northbound, NB)下的**回调层**。
 *   FRR 有两套配置入口并存：旧的 vty/命令层(bgp_routemap.c 里的 DEFUN)与
 *   新的 NB 层(YANG 模型 frr-route-map + frr-bgp-route-map augment)。本文件只实现
 *   NB 侧的回调，把 YANG 叶子节点(dnode)的值翻译成对 route-map 内部结构的操作，
 *   最终复用 lib/routemap.c 提供的 route_map_add_match / route_map_delete_match /
 *   route_map_add_set / route_map_delete_set 等通用 API。
 *
 * 【核心数据结构 / 关键概念】
 *   - struct routemap_hook_context (rhc)：一次配置会话挂在 lib 层 route-map 节点上的
 *     上下文。它记录“本次要执行的动作”三元组：
 *         rhc_rmi    : 目标 route_map_index（条目级对象，含序号+permit/deny）
 *         rhc_rule   : 规则字符串（如 "vpn dataplane" / "local-preference"）
 *         rhc_event  : 触发的事件类型(route_map_event_t，如 RMAP_EVENT_MATCH_ADDED)
 *         rhc_mhook  : 销毁钩子(通常是 bgp_route_match_delete/bgp_route_set_delete)
 *     lib 层在合适的时机回调这些 hook 完成真正的 add/delete。
 *   - args->event：NB 事件枚举，本文件几乎只关心 NB_EV_APPLY；其余
 *     (NB_EV_VALIDATE / NB_EV_PREPARE / NB_EV_ABORT)一律直接放行。
 *   - args->dnode：当前 YANG 叶子/容器的数据节点，值用 yang_dnode_get_string 等取。
 *
 * 【回调命名约定（本文件函数名极长的原因）】
 *   函数名 = lib_route_map_entry_<父路径>_<叶子名>_<操作>，直接编码了 YANG 中的
 *   节点路径。例如：
 *     lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_modify
 *   对应 XPath
 *     /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/
 *      frr-bgp-route-map:vpn-dataplane
 *   后缀 _modify/_destroy/_create/_finish 即 NB 回调类型：
 *     _create  创建（新增实例，如某个 list 的一行）
 *     _modify  改值（标量/叶子被写入）
 *     _destroy 删除（节点被移除）
 *     _finish  apply_finish —— 整棵子树应用完后汇总（如重建 list/hash）
 *   因此读某个回调时，先看它上方的 XPath 英文注释确定“它属于 match 还是 set、
 *   是哪个叶子”，再看函数体。
 *
 * 【本文件在主流程中的位置】
 *   上游：NB 引擎在加载/校验/应用配置时，按 YANG schema 查回调表
 *         (bgp_routemap_nb.c 里注册) 调用本文件函数。
 *   下游：本文件多数函数只是“设置 rhc 三元组 + 调 route_map_add_match/set”，
 *         真正建/删规则的逻辑在 lib/routemap.c，最终生效于 bgp_routemap.c 注册的
 *         匹配/设置编译函数。
 *
 * 【易错点 / 阅读建议】
 *   - 大量 _modify 回调结构几乎一模一样：仅“rule 字符串”“取值的 yang 类型函数”
 *     “返回的错误码(NB_ERR_INCONSISTENCY / NB_ERR_VALIDATION)”不同；看到重复可速读。
 *   - 注意返回码不统一：有的失败返回 NB_ERR_INCONSISTENCY，有的返回 NB_ERR_VALIDATION，
 *     这是各回调历史演进遗留，阅读时以代码为准。
 *   - 有的 _modify 在 switch 的 NB_EV_APPLY 分支后用了 break，有的用了 return，
 *     缩进也不完全一致——属历史风格，勿当作逻辑差异。
 *   - 少数回调(comm-list、aggregator、extcommunity-lb 等)涉及 list 节点和
 *     _finish 汇总，逻辑较重，建议重点看。
 *   建议阅读顺序：文件头两个通用助手 bgp_route_match_add / bgp_route_match_delete
 *               → 一个典型 match 的 _modify+_destroy → 一个典型 set 的 _modify+_destroy
 *               → comm-list / aggregator / extcommunity 等复杂节点。
 * ============================================================================
 */

#include <zebra.h>

#include "lib/command.h"
#include "lib/log.h"
#include "lib/northbound.h"
#include "lib/routemap.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_routemap_nb.h"

/* 【函数】bgp_route_match_add —— 向 route-map 条目新增一条 match 规则的通用助手。
 * 职责：把 match 命令串(command)+参数(arg)交给 lib 层的 route_map_add_match 编译并挂载。
 * 参数：
 *   index   : 目标条目(route_map_index，含序号与 permit/deny)。
 *   command : 规则名，如 "vpn dataplane"、"local-preference"。
 *   arg     : 规则参数（可为 NULL，取决于规则）。
 *   type    : 事件类型(route_map_event_t)，用于通知依赖者，如 RMAP_EVENT_MATCH_ADDED。
 *   errmsg/errmsg_len : 出错时写入的提示缓冲。
 * 返回：CMD_SUCCESS 成功；失败返回 CMD_WARNING_CONFIG_FAILED（并填 errmsg）。
 * 关键分支：把 lib 层的 rmap_compile_rets 结果翻译成 CLI 风格返回码；
 *   RMAP_RULE_MISSING/ RMAP_COMPILE_ERROR 均为配置失败。
 * 注意：本函数末尾总是返回 retval，成功路径(retval 初值 CMD_SUCCESS)对应
 *   RMAP_COMPILE_SUCCESS。 */
/* Add bgp route map rule. */
static int bgp_route_match_add(struct route_map_index *index,
		const char *command, const char *arg,
		route_map_event_t type,
		char *errmsg, size_t errmsg_len)
{
	int retval = CMD_SUCCESS;
	enum rmap_compile_rets ret;

	ret = route_map_add_match(index, command, arg, type);
	switch (ret) {
	case RMAP_RULE_MISSING:
		snprintf(errmsg, errmsg_len, "%% BGP Can't find rule.");
		retval = CMD_WARNING_CONFIG_FAILED;
		break;
	case RMAP_COMPILE_ERROR:
		snprintf(errmsg, errmsg_len, "%% BGP Argument is malformed.");
		retval = CMD_WARNING_CONFIG_FAILED;
		break;
	case RMAP_COMPILE_SUCCESS:
		/*
		 * Intentionally doing nothing here.
		 */
		break;
	}

	return retval;
}

/* 【函数】bgp_route_match_delete —— 从 route-map 条目删除一条 match 规则的通用助手。
 * 职责：调用 lib 层的 route_map_delete_match 删除指定规则；在删除前会临时复制
 *   规则名(rmap_name)与依赖名(dep_name)，因为删除可能触发依赖者回调而释放这些字符串。
 * 参数：同 bgp_route_match_add（command/arg/type/errmsg）。
 * 返回：CMD_SUCCESS 或 CMD_WARNING_CONFIG_FAILED。
 * 关键分支：
 *   - 仅当 type != RMAP_EVENT_MATCH_DELETED 时才去查/复制 dep_name 与 rmap_name
 *     （注释所述“ignore the mundane, the types without any dependency”即：
 *      纯删除事件本身不携带依赖，无需额外保存名称）。
 *   - arg 为 NULL 时从 index 反查当前匹配参数(route_map_get_match_arg)作为依赖名。
 * 易错点：dep_name/rmap_name 用 XSTRDUP 特意复制，随后在函数末尾 XFREE 释放；
 *   之所以复制，是因为底层删除过程中原始字符串可能已失效。 */
/* Delete bgp route map rule. */
static int bgp_route_match_delete(struct route_map_index *index,
		const char *command, const char *arg,
		route_map_event_t type,
		char *errmsg, size_t errmsg_len)
{
	enum rmap_compile_rets ret;
	int retval = CMD_SUCCESS;
	char *dep_name = NULL;
	const char *tmpstr;
	char *rmap_name = NULL;

	if (type != RMAP_EVENT_MATCH_DELETED) {
		/* ignore the mundane, the types without any dependency */
		if (arg == NULL) {
			tmpstr = route_map_get_match_arg(index, command);
			if (tmpstr != NULL)
				dep_name =
					XSTRDUP(MTYPE_ROUTE_MAP_RULE, tmpstr);
		} else {
			dep_name = XSTRDUP(MTYPE_ROUTE_MAP_RULE, arg);
		}
		rmap_name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, index->map->name);
	}

	ret = route_map_delete_match(index, command, dep_name, type);
	switch (ret) {
		case RMAP_RULE_MISSING:
			snprintf(errmsg, errmsg_len, "%% BGP Can't find rule.");
			retval = CMD_WARNING_CONFIG_FAILED;
			break;
		case RMAP_COMPILE_ERROR:
			snprintf(errmsg, errmsg_len,
				 "%% BGP Argument is malformed.");
			retval = CMD_WARNING_CONFIG_FAILED;
			break;
		case RMAP_COMPILE_SUCCESS:
			/*
			 * Nothing to do here
			 */
			break;
	}

	XFREE(MTYPE_ROUTE_MAP_RULE, dep_name);
	XFREE(MTYPE_ROUTE_MAP_NAME, rmap_name);

	return retval;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:vpn-dataplane
 */
/* ========== 1. match-condition 回调（对应 YANG .../match-condition/rmap-match-condition/*） ========== */
/* 【NB 回调】vpn_dataplane_modify —— match 条件 "vpn dataplane"
 * 对应 XPath：/frr-route-map:lib/route-map/entry/match-condition/
 *   rmap-match-condition/frr-bgp-route-map:vpn-dataplane
 * 职责：该叶子被写入/改值时，向 route-map 条目添加 "vpn dataplane" 匹配。
 * 参数：args（含 dnode/errmsg/event）。
 * 返回：NB_OK；编译失败返回 NB_ERR_INCONSISTENCY。
 * 调用时机：NB 引擎在 NB_EV_APPLY 阶段调用。
 * 关键分支：仅处理 NB_EV_APPLY。先用 nb_running_get_entry 取挂载在 lib 层的
 *   routemap_hook_context(rhc)，读入 dataplane 字符串；写回 rhc 三元组
 *   (rhc_mhook=bgp_route_match_delete, rhc_rule="vpn dataplane", rhc_event=...DELETED)
 *   供后续删除复用；再调 bgp_route_match_add；失败时务必把 rhc_mhook 置 NULL 并返回错误。
 */
int lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *dataplane;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		dataplane = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "vpn dataplane";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "vpn dataplane", dataplane,
					  RMAP_EVENT_MATCH_ADDED, args->errmsg, args->errmsg_len);
		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】vpn_dataplane_destroy —— 移除 match "vpn dataplane"。
 * 对应 XPath 同 vpn_dataplane_modify（叶子被删除时触发）。
 * 职责：NB_EV_APPLY 时委托 lib_route_map_entry_match_destroy 执行真正的删除
 *   （lib 层依据 rhc 里保存的 hook 上下文调用 bgp_route_match_delete）。
 * 其余事件直接返回 NB_OK。
 */
int lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:local-preference
 */
/* 【NB 回调】local_preference_modify —— match 条件 "local-preference"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:local-preference
 * 职责/参数/返回/调用时机：同 vpn_dataplane_modify；rhc_rule 为 "local-preference"。
 * 易错点：本函数在 switch 的 NB_EV_APPLY 分支之后用的是 return NB_OK（非 break），
 *   且该 return 带有额外缩进——属历史风格，逻辑上仍是成功返回 NB_OK。
 */
int
lib_route_map_entry_match_condition_rmap_match_condition_local_preference_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *local_pref;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		local_pref = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "local-preference";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "local-preference",
				local_pref, RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

		return NB_OK;
}

/* 【NB 回调】local_preference_destroy —— 移除 match "local-preference"。
 * 职责：NB_EV_APPLY 时委托 lib_route_map_entry_match_destroy；其余事件返回 NB_OK。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_local_preference_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:alias
 */
/* 【NB 回调】alias_modify —— match 条件 "alias"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:alias
 * 职责：添加 "alias" 匹配；rhc_rule="alias"。
 * 易错点：编译失败这里返回的是 NB_ERR_VALIDATION（而非多数回调的 NB_ERR_INCONSISTENCY）；
 *   且该分支用 break 跳出 switch 而非直接 return——阅读时注意返回值不一致。
 */
int lib_route_map_entry_match_condition_rmap_match_condition_alias_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *alias;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		alias = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "alias";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "alias", alias,
					  RMAP_EVENT_MATCH_ADDED, args->errmsg,
					  args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_VALIDATION;
		}

		break;
	}

	return NB_OK;
}

/* 【NB 回调】alias_destroy —— 移除 match "alias"。
 * 职责：NB_EV_APPLY 时委托 lib_route_map_entry_match_destroy。 */
int lib_route_map_entry_match_condition_rmap_match_condition_alias_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:script
 */
/* 【NB 回调】script_modify —— match 条件 "script"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:script
 * 职责：添加 "script" 匹配；rhc_rule="script"；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_script_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *script;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		script = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "script";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "script",
				script, RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】script_destroy —— 移除 match "script"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_script_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:origin
 */
/* 【NB 回调】origin_modify —— match 条件 "origin"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:origin
 * 职责：添加 "origin" 匹配（取值来自 yang 字符串）；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_origin_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *origin;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		origin = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "origin";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "origin", origin,
					  RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】origin_destroy —— 移除 match "origin"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_origin_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:rpki
 */
/* 【NB 回调】rpki_modify —— match 条件 "rpki"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:rpki
 * 职责：添加 "rpki" 匹配（RPKI 起源校验状态）；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_rpki_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *rpki;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		rpki = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "rpki";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "rpki", rpki,
				RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】rpki_destroy —— 移除 match "rpki"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_rpki_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:source-protocol
 */
/* 【NB 回调】source_protocol_modify —— match 条件 "source-protocol"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:source-protocol
 * 职责：添加 "source-protocol" 匹配（按来源协议号/route type 匹配）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_source_protocol_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	enum rmap_compile_rets ret;
	const char *proto;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		proto = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "source-protocol";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "source-protocol",
					  proto, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】source_protocol_destroy —— 移除 match "source-protocol"。 */
int lib_route_map_entry_match_condition_rmap_match_condition_source_protocol_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:rpki-extcommunity
 */
/* 【NB 回调】rpki_extcommunity_modify —— match 条件 "rpki-extcommunity"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:rpki-extcommunity
 * 职责：添加 "rpki-extcommunity" 匹配（对 extended community 的 RPKI 起源校验）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_rpki_extcommunity_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *rpki;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		rpki = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "rpki-extcommunity";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "rpki-extcommunity",
					  rpki, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】rpki_extcommunity_destroy —— 移除 match "rpki-extcommunity"。 */
int lib_route_map_entry_match_condition_rmap_match_condition_rpki_extcommunity_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:probability
 */
/* 【NB 回调】probability_modify —— match 条件 "probability"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:probability
 * 职责：添加 "probability" 匹配（按概率随机匹配，用于路由抖动抑制/负载分摊）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_probability_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *probability;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		probability = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "probability";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "probability",
					  probability, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】probability_destroy —— 移除 match "probability"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_probability_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:source-vrf
 */
/* 【NB 回调】source_vrf_modify —— match 条件 "source-vrf"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:source-vrf
 * 职责：添加 "source-vrf" 匹配（按来源 VRF 匹配）；取值 vrf；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_source_vrf_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *vrf;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		vrf = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "source-vrf";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "source-vrf", vrf,
					  RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】source_vrf_destroy —— 移除 match "source-vrf"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_source_vrf_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-ipv4-address
 */
/* 【NB 回调】peer_ipv4_address_modify —— match 条件 "peer-ipv4-address"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:peer-ipv4-address
 * 职责：添加对端(邻居)IPv4 地址匹配。注意：rhc_rule 与传给 bgp_route_match_add 的
 *   command 都是 "peer"，具体地址作为 arg——即 YANG 里 peer-ipv4-address /
 *   peer-interface / peer-ipv6-address / peer-local 共用底层的 "peer" 匹配命令，
 *   靠参数区分。失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv4_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "peer", peer,
					  RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】peer_ipv4_address_destroy —— 移除对端 IPv4 地址 match("peer")。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv4_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-interface
 */
/* 【NB 回调】peer_interface_modify —— match 条件 "peer-interface"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:peer-interface
 * 职责：添加对端出接口匹配；rhc_rule/command 仍为 "peer"，接口名作为 arg。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_interface_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "peer", peer,
				RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】peer_interface_destroy —— 移除对端出接口 match("peer")。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_interface_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-ipv6-address
 */
/* 【NB 回调】peer_ipv6_address_modify —— match 条件 "peer-ipv6-address"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:peer-ipv6-address
 * 职责：添加对端 IPv6 地址匹配；rhc_rule/command 为 "peer"，地址作为 arg。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv6_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "peer", peer,
				RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】peer_ipv6_address_destroy —— 移除对端 IPv6 地址 match("peer")。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv6_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-local
 */
/* 【NB 回调】peer_local_modify —— match 条件 "peer-local"（布尔叶子）
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:peer-local
 * 职责：当值为 true 时，添加 "peer local" 匹配（匹配来自本机/直连本地对端的路由）。
 * 关键分支：用 yang_dnode_get_bool 取布尔值，**仅当 value 为 true 才**调
 *   bgp_route_match_add；false 时不添加任何匹配（rhc 已设置但不会走 add）。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_local_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	bool value;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		value = yang_dnode_get_bool(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		if (value) {
			ret = bgp_route_match_add(rhc->rhc_rmi, "peer",
						"local",
						RMAP_EVENT_MATCH_ADDED,
						args->errmsg, args->errmsg_len);

			if (ret != RMAP_COMPILE_SUCCESS) {
				rhc->rhc_mhook = NULL;
				return NB_ERR_INCONSISTENCY;
			}
		}
	}

	return NB_OK;
}

/* 【NB 回调】peer_local_destroy —— 移除 "peer local" match。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_peer_local_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:src-peer-ipv4-address
 */
/* 【NB 回调】src_peer_ipv4_address_modify —— match 条件 "src-peer-ipv4-address"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:src-peer-ipv4-address
 * 职责：添加 "src-peer" 匹配（按路由的源端/对端地址匹配），地址作为 arg；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv4_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "src-peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "src-peer", peer, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】src_peer_ipv4_address_destroy —— 移除 "src-peer" match。 */
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv4_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:src-peer-interface
 */
/* 【NB 回调】src_peer_interface_modify —— match 条件 "src-peer-interface"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:src-peer-interface
 * 职责：添加 "src-peer" 匹配（源端接口名作为 arg）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_interface_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "src-peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "src-peer", peer, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】src_peer_interface_destroy —— 移除 "src-peer" match。 */
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_interface_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:src-peer-ipv6-address
 */
/* 【NB 回调】src_peer_ipv6_address_modify —— match 条件 "src-peer-ipv6-address"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:src-peer-ipv6-address
 * 职责：添加 "src-peer" 匹配（源端 IPv6 地址作为 arg）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv6_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "src-peer";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "src-peer", peer, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】src_peer_ipv6_address_destroy —— 移除 "src-peer" match。 */
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv6_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:list-name
 */
/* 【NB 回调】list_name_modify —— match 条件 "list-name"（一个叶子的多态分发）
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:list-name
 * 职责：这是一个被复用的 list-name 叶子。它没有自己的类型，而是读取**兄弟**叶子
 *   "../../frr-route-map:condition" 的值，据此决定把它挂成哪种匹配命令：
 *     IS_MATCH_AS_LIST(condition)        -> "as-path"        + 事件 RMAP_EVENT_ASLIST_*
 *     IS_MATCH_MAC_LIST(condition)       -> "mac address"   + RMAP_EVENT_FILTER_*
 *     IS_MATCH_ROUTE_SRC(condition)      -> "ip route-source" + RMAP_EVENT_FILTER_*
 *     IS_MATCH_ROUTE_SRC_PL(condition)   -> "ip route-source prefix-list" + RMAP_EVENT_PLIST_*
 * 关键分支：四个 if/else if 各自设置 rhc 三元组并调用 bgp_route_match_add；
 *   ret 初值为 RMAP_COMPILE_SUCCESS，若都不匹配则保持成功（空操作）。
 * 易错点：destroy 侧不区分类型，统一走 lib_route_map_entry_match_destroy。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_list_name_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *list_name;
	enum rmap_compile_rets ret = RMAP_COMPILE_SUCCESS;
	const char *condition;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		list_name = yang_dnode_get_string(args->dnode, NULL);
		condition = yang_dnode_get_string(args->dnode,
				"../../frr-route-map:condition");

		if (IS_MATCH_AS_LIST(condition)) {
			/* Set destroy information. */
			rhc->rhc_mhook = bgp_route_match_delete;
			rhc->rhc_rule = "as-path";
			rhc->rhc_event = RMAP_EVENT_ASLIST_DELETED;

			ret = bgp_route_match_add(rhc->rhc_rmi, "as-path",
					list_name, RMAP_EVENT_ASLIST_ADDED,
					args->errmsg, args->errmsg_len);
		} else if (IS_MATCH_MAC_LIST(condition)) {
			/* Set destroy information. */
			rhc->rhc_mhook = bgp_route_match_delete;
			rhc->rhc_rule = "mac address";
			rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;

			ret = bgp_route_match_add(rhc->rhc_rmi,
						  "mac address",
						  list_name,
						  RMAP_EVENT_FILTER_ADDED,
						  args->errmsg, args->errmsg_len);
		} else if (IS_MATCH_ROUTE_SRC(condition)) {
			/* Set destroy information. */
			rhc->rhc_mhook = bgp_route_match_delete;
			rhc->rhc_rule = "ip route-source";
			rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;

			ret = bgp_route_match_add(rhc->rhc_rmi,
					"ip route-source",
					list_name, RMAP_EVENT_FILTER_ADDED,
					args->errmsg, args->errmsg_len);
		} else if (IS_MATCH_ROUTE_SRC_PL(condition)) {
			/* Set destroy information. */
			rhc->rhc_mhook = bgp_route_match_delete;
			rhc->rhc_rule = "ip route-source prefix-list";
			rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;

			ret = bgp_route_match_add(rhc->rhc_rmi,
					"ip route-source prefix-list",
					list_name, RMAP_EVENT_PLIST_ADDED,
					args->errmsg, args->errmsg_len);
		}

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】list_name_destroy —— 移除 list-name 对应的匹配（不区分类型统一删除）。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_list_name_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:evpn-default-route
 */
/* 【NB 回调】evpn_default_route_create —— match 条件 "evpn-default-route"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:evpn-default-route
 * 职责：这是 **create** 回调（YANG 里该节点存在即触发，无取值），故 arg 传 NULL。
 *   添加 "evpn default-route" 匹配并返回 NB_ERR_INCONSISTENCY 于失败。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_evpn_default_route_create(
	struct nb_cb_create_args *args)
{
	struct routemap_hook_context *rhc;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "evpn default-route";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "evpn default-route",
					  NULL, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】evpn_default_route_destroy —— 移除 match "evpn default-route"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_evpn_default_route_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:evpn-vni
 */
/* 【NB 回调】evpn_vni_modify —— match 条件 "evpn-vni"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:evpn-vni
 * 职责：添加 "evpn vni" 匹配（VNI 作为 arg，匹配指定 EVPN VNI 的路由）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_evpn_vni_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *vni;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		vni = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "evpn vni";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "evpn vni", vni,
				RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】evpn_vni_destroy —— 移除 match "evpn vni"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_evpn_vni_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:evpn-route-type
 */
/* 【NB 回调】evpn_route_type_modify —— match 条件 "evpn-route-type"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:evpn-route-type
 * 职责：添加 "evpn route-type" 匹配（EVPN 路由类型，如 1..5，作为 arg）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_evpn_route_type_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "evpn route-type";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "evpn route-type",
					  type,
					  RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】evpn_route_type_destroy —— 移除 match "evpn route-type"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_evpn_route_type_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:route-distinguisher
 */
/* 【NB 回调】route_distinguisher_modify —— match 条件 "route-distinguisher"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:route-distinguisher
 * 职责：添加 "evpn rd" 匹配（按 EVPN 的 RD 匹配；注意命名的 rule 串是 "evpn rd"，
 *   而非 YANG 叶子名 route-distinguisher）；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_route_distinguisher_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *rd;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		rd = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "evpn rd";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "evpn rd", rd,
				RMAP_EVENT_MATCH_ADDED,
				args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】route_distinguisher_destroy —— 移除 match "evpn rd"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_route_distinguisher_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:community-limit
 */
/* 【NB 回调】community_limit_modify —— match 条件 "community-limit"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:community-limit
 * 职责：添加 "community-limit" 匹配（限制/过滤 community 个数上限类场景）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_community_limit_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *limit;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		limit = yang_dnode_get_string(args->dnode, NULL);

		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "community-limit";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "community-limit", limit,
					  RMAP_EVENT_MATCH_ADDED, args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】community_limit_destroy —— 移除 match "community-limit"。 */
int lib_route_map_entry_match_condition_rmap_match_condition_community_limit_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}


/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:as-path-count
 */
/* 【NB 回调】aspath_count_modify —— match 条件 "as-path-count"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:as-path-count
 * 职责：添加 "as-path-count" 匹配（按 AS_PATH 中 AS 数量匹配）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_aspath_count_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *count;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		count = yang_dnode_get_string(args->dnode, NULL);

		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "as-path-count";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "as-path-count", count,
					  RMAP_EVENT_MATCH_ADDED, args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】aspath_count_destroy —— 移除 match "as-path-count"。 */
int lib_route_map_entry_match_condition_rmap_match_condition_aspath_count_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath = /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list
 */
/* 【NB 回调】comm_list_create —— match 条件 "comm-list"（容器/list 的创建）
 * 对应 XPath：/frr-route-map:lib/route-map/entry/match-condition/
 *   rmap-match-condition/frr-bgp-route-map:comm-list
 * 职责：目前**什么都不做**，直接返回 NB_OK。真正的添加逻辑在子叶子
 *   (comm-list-name-any / comm-list-name-exact-match) 的 _modify 及本容器的
 *   _finish 里完成。读这个空函数时勿以为漏了逻辑——是刻意为之。 */
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_create(
	struct nb_cb_create_args *args)
{
	return NB_OK;
}

/* 【NB 回调】comm_list_destroy —— 移除 comm-list 容器对应的 match。
 * 职责：NB_EV_APPLY 时委托 lib_route_map_entry_match_destroy。 */
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/* 【NB 回调】comm_list_finish —— match 条件 "comm-list" 的 apply_finish 汇总
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:comm-list
 * 职责：comm-list 是 list 容器，其子叶子(comm-list-name 及 any / exact-match 修饰)
 *   在各自 _modify 里都不做事；到整棵子树应用完，由本 _finish 汇总成**一条**匹配命令。
 * 参数：args（apply_finish 参数，dnode 指向 comm-list 容器）。
 * 返回：void。
 * 调用时机：apply_finish 阶段（子树全部应用后）。注意此处**不判断 event**。
 * 关键逻辑：
 *   1) 读 comm-list-name 的值(value)；
 *   2) 若存在 comm-list-name-exact-match 且为 true，则拼成 "<value> exact-match"；
 *      否则若存在 comm-list-name-any 且为 true，则拼成 "<value> any"；
 *      两者都无则直接用 value（此时 argstr 指向 value，注意后面的 XFREE 条件）；
 *   3) 读兄弟 "../../frr-route-map:condition" 决定类型：
 *        IS_MATCH_COMMUNITY  -> rule "community"      + CLIST 事件
 *        IS_MATCH_LCOMMUNITY -> rule "large-community" + LLIST 事件
 *        其他               -> rule "extcommunity"    + ECLIST 事件
 *   4) 调 bgp_route_match_add 并 assert 成功——注释说明：此阶段若失败 bgpd 即将崩溃，
 *      故直接 assert 暴露问题（属有意为之的防御，不是 bug）。
 * 易错点：argstr 仅在需要拼装时 XMALLOC；通过 (argstr != value) 判断是否需 XFREE，
 *   避免释放指向 YANG 内部的 value 指针。 */
void
lib_route_map_entry_match_condition_rmap_match_condition_comm_list_finish(
	struct nb_cb_apply_finish_args *args)
{
	struct routemap_hook_context *rhc;
	const char *value;
	bool match_p = false;
	bool any = false;
	char *argstr;
	const char *condition;
	route_map_event_t event;
	int ret;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	value = yang_dnode_get_string(args->dnode, "comm-list-name");

	if (yang_dnode_exists(args->dnode, "comm-list-name-exact-match"))
		match_p = yang_dnode_get_bool(
			args->dnode, "./comm-list-name-exact-match");

	if (yang_dnode_exists(args->dnode, "comm-list-name-any"))
		any = yang_dnode_get_bool(args->dnode, "comm-list-name-any");

	if (match_p) {
		argstr = XMALLOC(MTYPE_ROUTE_MAP_COMPILED,
				 strlen(value) + strlen("exact-match") + 2);

		snprintf(argstr, (strlen(value) + strlen("exact-match") + 2),
			 "%s exact-match", value);
	} else if (any) {
		argstr = XMALLOC(MTYPE_ROUTE_MAP_COMPILED,
				 strlen(value) + strlen("any") + 2);

		snprintf(argstr, (strlen(value) + strlen("any") + 2), "%s any",
			 value);
	} else
		argstr = (char *)value;

	/* Set destroy information. */
	rhc->rhc_mhook = bgp_route_match_delete;

	condition = yang_dnode_get_string(args->dnode,
					  "../../frr-route-map:condition");
	if (IS_MATCH_COMMUNITY(condition)) {
		rhc->rhc_rule = "community";
		event = RMAP_EVENT_CLIST_ADDED;
		rhc->rhc_event = RMAP_EVENT_CLIST_DELETED;
	} else if (IS_MATCH_LCOMMUNITY(condition)) {
		rhc->rhc_rule = "large-community";
		event = RMAP_EVENT_LLIST_ADDED;
		rhc->rhc_event = RMAP_EVENT_LLIST_DELETED;
	} else {
		rhc->rhc_rule = "extcommunity";
		event = RMAP_EVENT_ECLIST_ADDED;
		rhc->rhc_event = RMAP_EVENT_ECLIST_DELETED;
	}

	ret = bgp_route_match_add(rhc->rhc_rmi, rhc->rhc_rule, argstr, event,
				  args->errmsg, args->errmsg_len);
	/*
	 * At this point if this is not a successful operation
	 * bgpd is about to crash.  Let's just cut to the
	 * chase and do it.
	 */
	assert(ret == RMAP_COMPILE_SUCCESS);

	if (argstr != value)
		XFREE(MTYPE_ROUTE_MAP_COMPILED, argstr);
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name
 */
/* 【NB 回调】comm_list_comm_list_name_modify —— comm-list 的 "comm-list-name" 叶子
 * 对应 XPath：.../comm-list/comm-list-name
 * 职责：本函数是**空操作**——所有事件(VALIDATE/PREPARE/ABORT/APPLY)都直接 break，
 *   返回 NB_OK。真正的添加在 comm_list_finish 里统一完成。刻意留空，勿以为漏写。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_modify(
	struct nb_cb_modify_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any
 */
/* 【NB 回调】comm_list_comm_list_name_any_modify —— "comm-list-name-any" 修饰叶子
 * 对应 XPath：.../comm-list/comm-list-name-any
 * 职责：**空操作**（仅 break 后返回 NB_OK）；其语义由 comm_list_finish 统一处理。 */
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_any_modify(
	struct nb_cb_modify_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/* 【NB 回调】comm_list_comm_list_name_any_destroy —— "comm-list-name-any" 删除
 * 职责：**空操作**（仅 break 后返回 NB_OK）。 */
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_any_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match
 */
/* 【NB 回调】comm_list_comm_list_name_exact_match_modify —— "comm-list-name-exact-match" 修饰叶子
 * 对应 XPath：.../comm-list/comm-list-name-exact-match
 * 职责：**空操作**（仅 break 后返回 NB_OK）；其语义由 comm_list_finish 统一处理。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_exact_match_modify(
	struct nb_cb_modify_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/* 【NB 回调】comm_list_comm_list_name_exact_match_destroy —— "comm-list-name-exact-match" 删除
 * 职责：**空操作**（仅 break 后返回 NB_OK）。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_exact_match_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:ipv4-address
 */
/* 【NB 回调】ipv4_address_modify —— match 条件 "ipv4-address"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:ipv4-address
 * 职责：添加 "ip next-hop address" 匹配（按下一跳 IPv4 地址匹配）。
 * 易错点：YANG 叶子叫 ipv4-address，但底层 rule/command 串是 "ip next-hop address"；
 *   注意本回调直接用 rhc->rhc_rule 作为 command 传入。失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_ipv4_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "ip next-hop address";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, rhc->rhc_rule,
					  peer, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】ipv4_address_destroy —— 移除 match "ip next-hop address"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_ipv4_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:ipv6-address
 */
/* 【NB 回调】ipv6_address_modify —— match 条件 "ipv6-address"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:ipv6-address
 * 职责：添加 "ipv6 next-hop address" 匹配（按下一跳 IPv6 地址匹配）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_ipv6_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *peer;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		peer = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "ipv6 next-hop address";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, rhc->rhc_rule,
					  peer, RMAP_EVENT_MATCH_ADDED,
					  args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】ipv6_address_destroy —— 移除 match "ipv6 next-hop address"。 */
int
lib_route_map_entry_match_condition_rmap_match_condition_ipv6_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:distance
 */
/* ========== 2. set-action 回调（对应 YANG .../set-action/rmap-set-action/*） ==========
 * 【与 match 回调的关键差异】set 侧用**另一套** hook 字段与 API：
 *   rhc->rhc_shook（set hook，对应 generic_set_delete）
 *   rhc->rhc_event = RMAP_EVENT_SET_DELETED
 *   执行用 generic_set_add(rhc->rhc_rmi, <rule>, <arg>, errmsg...)（返回 CMD_*）
 *   删除委托 lib_route_map_entry_set_destroy(args)
 * 而 match 侧用 rhc_mhook / bgp_route_match_add / bgp_route_match_delete /
 *   lib_route_map_entry_match_destroy。阅读时先分清当前函数属于哪一侧。
 *
 * 【NB 回调】distance_modify —— set 动作 "distance"
 * 对应 XPath：/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/
 *   frr-bgp-route-map:distance
 * 职责：generic_set_add "distance"（设置管理距离）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_distance_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "distance";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "distance", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】distance_destroy —— 移除 set "distance"。委托 lib_route_map_entry_set_destroy。 */
int lib_route_map_entry_set_action_rmap_set_action_distance_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:extcommunity-limit
 */
/* 【NB 回调】extcommunity_limit_modify —— match 条件 "extcommunity-limit"
 * 对应 XPath：.../rmap-match-condition/frr-bgp-route-map:extcommunity-limit
 * 职责：添加 "extcommunity-limit" 匹配；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_match_condition_rmap_match_condition_extcommunity_limit_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *limit;
	enum rmap_compile_rets ret;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		limit = yang_dnode_get_string(args->dnode, NULL);

		rhc->rhc_mhook = bgp_route_match_delete;
		rhc->rhc_rule = "extcommunity-limit";
		rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

		ret = bgp_route_match_add(rhc->rhc_rmi, "extcommunity-limit", limit,
					  RMAP_EVENT_MATCH_ADDED, args->errmsg, args->errmsg_len);

		if (ret != RMAP_COMPILE_SUCCESS) {
			rhc->rhc_mhook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_limit_destroy —— 移除 match "extcommunity-limit"。 */
int lib_route_map_entry_match_condition_rmap_match_condition_extcommunity_limit_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_match_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-rt
 */
/* 【NB 回调】extcommunity_rt_modify —— set 动作 "extcommunity-rt"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-rt
 * 职责：generic_set_add "extcommunity rt"（追加 RT 扩展团体）；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_rt_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "extcommunity rt";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "extcommunity rt", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_rt_destroy —— 移除 set "extcommunity rt"。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_rt_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-nt
 */
/* 【NB 回调】extcommunity_nt_modify —— set 动作 "extcommunity-nt"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-nt
 * 职责：generic_set_add "extcommunity nt"（追加 NT/No-Export 类扩展团体）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_nt_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *str;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		str = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "extcommunity nt";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "extcommunity nt", str,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_nt_destroy —— 移除 set "extcommunity nt"。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_nt_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-soo
 */
/* 【NB 回调】extcommunity_soo_modify —— set 动作 "extcommunity-soo"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-soo
 * 职责：generic_set_add "extcommunity soo"（设置 Site-of-Origin 扩展团体）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_soo_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "extcommunity soo";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "extcommunity soo",
				     type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_soo_destroy —— 移除 set "extcommunity soo"。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_soo_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:ipv4-address
 */
/* 【NB 回调】ipv4_address_modify —— set 动作 "ipv4-address"（VPN 下一跳）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:ipv4-address
 * 职责：generic_set_add "ipv4 vpn next-hop"（设置 VPNv4 下一跳地址）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_ipv4_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *addr;
	int rv = CMD_SUCCESS;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		addr = yang_dnode_get_string(args->dnode, NULL);

		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;
		rhc->rhc_rule = "ipv4 vpn next-hop";

		rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, addr,
				     args->errmsg, args->errmsg_len);

		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】ipv4_address_destroy —— 移除 set "ipv4 vpn next-hop"。 */
int lib_route_map_entry_set_action_rmap_set_action_ipv4_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:ipv4-nexthop
 */
/* 【NB 回调】ipv4_nexthop_modify —— set 动作 "ipv4-nexthop"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:ipv4-nexthop
 * 职责：generic_set_add "ip next-hop"（设置下一跳，值为类型/动作字符串）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_ipv4_nexthop_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "ip next-hop";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, type,
				    args->errmsg, args->errmsg_len);

		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】ipv4_nexthop_destroy —— 移除 set "ip next-hop"。 */
int lib_route_map_entry_set_action_rmap_set_action_ipv4_nexthop_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:ipv6-address
 */
/* 【NB 回调】ipv6_address_modify —— set 动作 "ipv6-address"（含校验，稍复杂）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:ipv6-address
 * 职责：generic_set_add 设置 IPv6 下一跳。与多数 set 回调不同，本函数**在
 *   NB_EV_VALIDATE 阶段做校验**：读兄弟 "../../frr-route-map:action"，
 *   当其为 IS_SET_IPV6_NH_GLOBAL(action)(全局下一跳) 时，取值若是
 *   未指定(::)/环回/组播/链路本地地址，则直接返回 NB_ERR_VALIDATION 拒绝配置。
 * 关键分支：APPLY 时再次读 action，决定 rhc_rule：
 *   IS_SET_IPV6_NH_GLOBAL(action) ? "ipv6 next-hop global" : "ipv6 vpn next-hop"。
 * 易错点：源码中那一行英文注释（内容为 Set destroy information. 的注释块）被夹在
 *   if 的单一语句前，位置略显别扭（该注释其实是给下面的 rhc_rule 赋值用的），非逻辑错误。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_ipv6_address_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *addr;
	int rv = CMD_SUCCESS;
	const char *action = NULL;
	struct in6_addr i6a;

	action = yang_dnode_get_string(args->dnode,
				       "../../frr-route-map:action");
	switch (args->event) {
	case NB_EV_VALIDATE:
		if (action && IS_SET_IPV6_NH_GLOBAL(action)) {
			yang_dnode_get_ipv6(&i6a, args->dnode, NULL);
			if (IN6_IS_ADDR_UNSPECIFIED(&i6a)
			    || IN6_IS_ADDR_LOOPBACK(&i6a)
			    || IN6_IS_ADDR_MULTICAST(&i6a)
			    || IN6_IS_ADDR_LINKLOCAL(&i6a))
				return NB_ERR_VALIDATION;
		}
		return NB_OK;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		return NB_OK;
	case NB_EV_APPLY:
		break;
	}

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	addr = yang_dnode_get_string(args->dnode, NULL);

	rhc->rhc_shook = generic_set_delete;
	rhc->rhc_event = RMAP_EVENT_SET_DELETED;

	if (IS_SET_IPV6_NH_GLOBAL(action))
		/* Set destroy information. */
		rhc->rhc_rule = "ipv6 next-hop global";
	else
		rhc->rhc_rule = "ipv6 vpn next-hop";

	rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, addr,
			     args->errmsg, args->errmsg_len);

	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 【NB 回调】ipv6_address_destroy —— 移除 set IPv6 下一跳。 */
int lib_route_map_entry_set_action_rmap_set_action_ipv6_address_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:preference
 */
/* 【NB 回调】preference_modify —— set 动作 "preference"（布尔叶子）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:preference
 * 职责：读布尔 value，**仅当 value 为 true** 时才设置一条 ipv6 下一跳偏好类动作；
 *   读兄弟 action 决定具体 rule：IS_SET_IPV6_PEER_ADDR(action) ?
 *   "ipv6 next-hop peer-address" : "ipv6 next-hop prefer-global"，arg 传 NULL。
 * 关键分支：value 为 false 时不做任何 generic_set_add（rv 保持初值 CMD_SUCCESS）。
 * 易错点：rv 初值为 CMD_SUCCESS，若 value 为 false 则跳过添加、直接成功返回。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_preference_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	int rv = CMD_SUCCESS;
	const char *action = NULL;
	bool value;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		value = yang_dnode_get_bool(args->dnode, NULL);

		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		action = yang_dnode_get_string(args->dnode,
				"../../frr-route-map:action");

		if (value) {
			if (IS_SET_IPV6_PEER_ADDR(action))
				/* Set destroy information. */
				rhc->rhc_rule = "ipv6 next-hop peer-address";
			else
				rhc->rhc_rule = "ipv6 next-hop prefer-global";

			rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule,
					     NULL,
					     args->errmsg, args->errmsg_len);
		}

		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】preference_destroy —— 移除 set preference。 */
int lib_route_map_entry_set_action_rmap_set_action_preference_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:label-index
 */
/* 【NB 回调】label_index_modify —— set 动作 "label-index"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:label-index
 * 职责：generic_set_add "label-index"（设置 MPLS 标签索引）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_label_index_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "label-index";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "label-index", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】label_index_destroy —— 移除 set "label-index"。 */
int lib_route_map_entry_set_action_rmap_set_action_label_index_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:local-pref
 */
/* 【NB 回调】local_pref_modify —— set 动作 "local-pref"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:local-pref
 * 职责：generic_set_add "local-preference"（设置 BGP LOCAL_PREF）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_local_pref_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "local-preference";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "local-preference",
				     type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】local_pref_destroy —— 移除 set "local-preference"。 */
int lib_route_map_entry_set_action_rmap_set_action_local_pref_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:weight
 */
/* 【NB 回调】weight_modify —— set 动作 "weight"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:weight
 * 职责：generic_set_add "weight"（设置 BGP 权重）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_weight_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "weight";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "weight", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】weight_destroy —— 移除 set "weight"。 */
int lib_route_map_entry_set_action_rmap_set_action_weight_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:origin
 */
/* 【NB 回调】origin_modify（set 侧）—— set 动作 "origin"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:origin
 * 职责：generic_set_add "origin"（设置 ORIGIN：igp/egp/incomplete）；
 *   失败返回 NB_ERR_INCONSISTENCY。注意与 match 侧的 origin 回调区分（前缀不同）。 */
int lib_route_map_entry_set_action_rmap_set_action_origin_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "origin";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "origin", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】origin_destroy（set 侧）—— 移除 set "origin"。 */
int lib_route_map_entry_set_action_rmap_set_action_origin_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);

	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:originator-id
 */
/* 【NB 回调】originator_id_modify —— set 动作 "originator-id"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:originator-id
 * 职责：generic_set_add "originator-id"（设置 ORIGINATOR_ID 属性，通常为路由器 ID）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_originator_id_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "originator-id";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "originator-id", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】originator_id_destroy —— 移除 set "originator-id"。 */
int lib_route_map_entry_set_action_rmap_set_action_originator_id_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:table
 */
/* 【NB 回调】table_modify —— set 动作 "table"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:table
 * 职责：generic_set_add "table"（设置目标 RIB 表号/table id）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_table_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "table";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "table", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】table_destroy —— 移除 set "table"。 */
int lib_route_map_entry_set_action_rmap_set_action_table_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:atomic-aggregate
 */
/* 【NB 回调】atomic_aggregate_create —— set 动作 "atomic-aggregate"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:atomic-aggregate
 * 职责：create 回调（存在即设置），generic_set_add "atomic-aggregate"，arg 传 NULL
 *   （设置 ATOMIC_AGGREGATE 属性）；失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_atomic_aggregate_create(
	struct nb_cb_create_args *args)
{
	struct routemap_hook_context *rhc;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "atomic-aggregate";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, NULL,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】atomic_aggregate_destroy —— 移除 set "atomic-aggregate"。 */
int
lib_route_map_entry_set_action_rmap_set_action_atomic_aggregate_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aigp-metric
 */
/* 【NB 回调】aigp_metric_modify —— set 动作 "aigp-metric"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:aigp-metric
 * 职责：generic_set_add "aigp-metric"（设置 AIGP metric 值或相对动作）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_aigp_metric_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *aigp;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		aigp = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "aigp-metric";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, aigp,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】aigp_metric_destroy —— 移除 set "aigp-metric"。 */
int lib_route_map_entry_set_action_rmap_set_action_aigp_metric_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:prepend-as-path
 */
/* 【NB 回调】prepend_as_path_modify —— set 动作 "prepend-as-path"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:prepend-as-path
 * 职责：generic_set_add "as-path prepend"（在 AS_PATH 前插入指定 AS 串）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_prepend_as_path_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "as-path prepend";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "as-path prepend",
				     type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】prepend_as_path_destroy —— 移除 set "as-path prepend"。 */
int
lib_route_map_entry_set_action_rmap_set_action_prepend_as_path_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:last-as
 */
/* 【NB 回调】last_as_modify —— set 动作 "last-as"（拼接参数，稍特殊）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:last-as
 * 职责：把 YANG 的数值 value 拼成 "last-as <value>" 作为参数，再 generic_set_add
 *   "as-path prepend"。rhc_rule 仍为 "as-path prepend"（与 prepend-as-path 共用底层）。
 * 关键分支：用 XMALLOC(MTYPE_ROUTE_MAP_COMPILED) 拼装 argstr；**成功与失败两条路径
 *   都必须 XFREE 掉 argstr**（失败路径在 return 前先 XFREE，成功路径在 switch 末尾 XFREE）。
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_last_as_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *value;
	char *argstr;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		value = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "as-path prepend";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		argstr = XMALLOC(MTYPE_ROUTE_MAP_COMPILED,
				strlen(value) + strlen("last-as") + 2);

		snprintf(argstr, (strlen(value) + strlen("last-as") + 2),
			 "last-as %s", value);

		rv = generic_set_add(rhc->rhc_rmi, "as-path prepend",
				     argstr,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			XFREE(MTYPE_ROUTE_MAP_COMPILED, argstr);
			return NB_ERR_INCONSISTENCY;
		}

		XFREE(MTYPE_ROUTE_MAP_COMPILED, argstr);
	}

	return NB_OK;
}

/* 【NB 回调】last_as_destroy —— 移除 set "as-path prepend"(last-as)。 */
int lib_route_map_entry_set_action_rmap_set_action_last_as_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:exclude-as-path
 */
/* 【NB 回调】exclude_as_path_modify —— set 动作 "exclude-as-path"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:exclude-as-path
 * 职责：generic_set_add "as-path exclude"（从 AS_PATH 中排除指定 AS）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_exclude_as_path_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "as-path exclude";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "as-path exclude",
				     type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】exclude_as_path_destroy —— 移除 set "as-path exclude"。 */
int
lib_route_map_entry_set_action_rmap_set_action_exclude_as_path_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:replace-as-path
 */
/* 【NB 回调】replace_as_path_modify —— set 动作 "replace-as-path"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:replace-as-path
 * 职责：generic_set_add "as-path replace"（替换 AS_PATH 中的 AS）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_replace_as_path_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "as-path replace";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "as-path replace", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】replace_as_path_destroy —— 移除 set "as-path replace"。 */
int lib_route_map_entry_set_action_rmap_set_action_replace_as_path_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:community-none
 */
/* 【NB 回调】community_none_modify —— set 动作 "community-none"（布尔叶子）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:community-none
 * 职责：读布尔 none；**仅当 none 为 true** 时 generic_set_add "community" "none"
 *   （清空标准 community），成功即返回 NB_OK。
 * 关键分支：若 none 为 false，函数走到末尾 return NB_ERR_INCONSISTENCY——即“该叶子被
 *   置为 false”会被视为错误返回。这是本文件里的一个特别行为，阅读时注意（非 bug 判断）。 */
int lib_route_map_entry_set_action_rmap_set_action_community_none_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	bool none = false;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		none = yang_dnode_get_bool(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "community";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		if (none) {
			rv = generic_set_add(rhc->rhc_rmi, "community",
					     "none",
					     args->errmsg, args->errmsg_len);
			if (rv != CMD_SUCCESS) {
				rhc->rhc_shook = NULL;
				return NB_ERR_INCONSISTENCY;
			}
			return NB_OK;
		}

		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 【NB 回调】community_none_destroy —— 移除 set "community none"。 */
int
lib_route_map_entry_set_action_rmap_set_action_community_none_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:community-string
 */
/* 【NB 回调】community_string_modify —— set 动作 "community-string"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:community-string
 * 职责：generic_set_add "community" <字符串>（按字符串设置标准 community）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_community_string_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "community";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "community", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】community_string_destroy —— 移除 set "community"。 */
int
lib_route_map_entry_set_action_rmap_set_action_community_string_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:large-community-none
 */
/* 【NB 回调】large_community_none_modify —— set 动作 "large-community-none"（布尔叶子）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:large-community-none
 * 职责：读布尔 none；仅当为 true 时 generic_set_add "large-community" "none"。
 * 关键分支：与 community_none 同构——none 为 false 时返回 NB_ERR_INCONSISTENCY。
 * 易错点：源码里该 return NB_OK 的缩进比周围少一级（对齐略有出入），非逻辑差异。 */
int
lib_route_map_entry_set_action_rmap_set_action_large_community_none_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	bool none = false;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		none = yang_dnode_get_bool(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "large-community";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		if (none) {
			rv = generic_set_add(rhc->rhc_rmi,
					     "large-community",
					     "none",
					      args->errmsg, args->errmsg_len);
			if (rv != CMD_SUCCESS) {
				rhc->rhc_shook = NULL;
				return NB_ERR_INCONSISTENCY;
			}
		return NB_OK;
		}

		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 【NB 回调】large_community_none_destroy —— 移除 set "large-community none"。 */
int
lib_route_map_entry_set_action_rmap_set_action_large_community_none_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:large-community-string
 */
/* 【NB 回调】large_community_string_modify —— set 动作 "large-community-string"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:large-community-string
 * 职责：generic_set_add "large-community" <字符串>（设置大 community）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int
lib_route_map_entry_set_action_rmap_set_action_large_community_string_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "large-community";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "large-community",
				     type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】large_community_string_destroy —— 移除 set "large-community"。 */
int
lib_route_map_entry_set_action_rmap_set_action_large_community_string_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * xpath =
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aggregator
 */
/* 【NB 回调】aggregator_create —— set 动作 "aggregator"（容器）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:aggregator
 * 职责：**空操作**，直接返回 NB_OK。真正的设置在其子叶子与 aggregator_finish
 *   （汇总 asn+address 成 "aggregator as"）里完成。 */
int lib_route_map_entry_set_action_rmap_set_action_aggregator_create(
	struct nb_cb_create_args *args)
{
	return NB_OK;
}

/* 【NB 回调】aggregator_destroy —— 移除 set "aggregator"。 */
int lib_route_map_entry_set_action_rmap_set_action_aggregator_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/* 【NB 回调】aggregator_finish —— set "aggregator" 的 apply_finish 汇总
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:aggregator
 * 职责：子叶子 aggregator-asn / aggregator-address 在各自 _modify 里只做校验或空操作；
 *   _finish 汇总为一条命令：读两个子节点值拼成 "<asn> <addr>"，generic_set_add
 *   "aggregator as"，并 assert 成功（注释同 comm_list_finish：此阶段失败即崩溃）。
 * 参数：args（apply_finish）。返回：void。
 * 易错点：argstr 由 XMALLOC 拼装，函数末尾 XFREE 释放；注意此函数**不判断 event**。 */
void lib_route_map_entry_set_action_rmap_set_action_aggregator_finish(
	struct nb_cb_apply_finish_args *args)
{
	struct routemap_hook_context *rhc;
	const char *asn;
	const char *addr;
	char *argstr;
	int ret;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	asn = yang_dnode_get_string(args->dnode, "aggregator-asn");
	addr = yang_dnode_get_string(args->dnode, "aggregator-address");

	argstr = XMALLOC(MTYPE_ROUTE_MAP_COMPILED,
			 strlen(asn) + strlen(addr) + 2);

	snprintf(argstr, (strlen(asn) + strlen(addr) + 2), "%s %s", asn, addr);

	/* Set destroy information. */
	rhc->rhc_shook = generic_set_delete;
	rhc->rhc_rule = "aggregator as";
	rhc->rhc_event = RMAP_EVENT_SET_DELETED;

	ret = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, argstr, args->errmsg,
			      args->errmsg_len);
	/*
	 * At this point if this is not a successful operation
	 * bgpd is about to crash.  Let's just cut to the
	 * chase and do it.
	 */
	assert(ret == CMD_SUCCESS);

	XFREE(MTYPE_ROUTE_MAP_COMPILED, argstr);
}
/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aggregator/aggregator-asn
 */
/* 【NB 回调】aggregator_aggregator_asn_modify —— aggregator 的 "aggregator-asn" 叶子
 * 对应 XPath：.../aggregator/aggregator-asn
 * 职责：只做**校验**，不做添加（添加在 aggregator_finish）。
 * 关键分支：NB_EV_VALIDATE 时读 asn，用 asn_str2asn_match 判断是否为 exact_match：
 *   是则返回 NB_OK，否则返回 NB_ERR_VALIDATION（即只接受单一精确 ASN，不接受区间/集合）。
 *   其余事件直接 break，返回 NB_OK。 */
int
lib_route_map_entry_set_action_rmap_set_action_aggregator_aggregator_asn_modify(
	struct nb_cb_modify_args *args)
{
	const char *asn;
	enum match_type match;

	switch (args->event) {
	case NB_EV_VALIDATE:
		asn = yang_dnode_get_string(args->dnode, NULL);
		if (!asn)
			return NB_ERR_VALIDATION;
		match = asn_str2asn_match(asn);
		if (match == exact_match)
			return NB_OK;
		return NB_ERR_VALIDATION;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aggregator/aggregator-address
 */
/* 【NB 回调】aggregator_aggregator_address_modify —— aggregator 的 "aggregator-address" 叶子
 * 对应 XPath：.../aggregator/aggregator-address
 * 职责：**空操作**（所有事件都 break 后返回 NB_OK）；实际汇总在 aggregator_finish。 */
int
lib_route_map_entry_set_action_rmap_set_action_aggregator_aggregator_address_modify(
	struct nb_cb_modify_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
	case NB_EV_APPLY:
		break;
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:comm-list-name
 */
/* 【NB 回调】comm_list_name_modify（set 侧）—— set 动作 "comm-list-name"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:comm-list-name
 * 职责：一个多态 set 叶子。读兄弟 "../../frr-route-map:action" 决定 rule：
 *     IS_SET_COMM_LIST_DEL(action)     -> "comm-list"
 *     IS_SET_EXTCOMM_LIST_DEL(action)  -> "extended-comm-list"
 *     其他                             -> "large-comm-list"
 *   然后 generic_set_add <rule> <value>。失败返回 NB_ERR_INCONSISTENCY。
 * 注意：与 match 侧的 comm-list（_finish 汇总）不同，这里直接在 _modify 内完成设置。 */
int lib_route_map_entry_set_action_rmap_set_action_comm_list_name_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *value;
	const char *action;
	int rv = CMD_SUCCESS;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		value = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;

		action = yang_dnode_get_string(args->dnode,
				"../../frr-route-map:action");
		if (IS_SET_COMM_LIST_DEL(action))
			rhc->rhc_rule = "comm-list";
		else if (IS_SET_EXTCOMM_LIST_DEL(action))
			rhc->rhc_rule = "extended-comm-list";
		else
			rhc->rhc_rule = "large-comm-list";

		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, rhc->rhc_rule, value,
				     args->errmsg, args->errmsg_len);

		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】comm_list_name_destroy（set 侧）—— 移除 set comm-list。 */
int
lib_route_map_entry_set_action_rmap_set_action_comm_list_name_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb
 */
/* 【NB 回调】extcommunity_lb_create —— set 动作 "extcommunity-lb"（容器，Link Bandwidth）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-lb
 * 职责：**空操作**，直接返回 NB_OK。真正设置在其子叶子 + extcommunity_lb_finish 汇总。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_create(
	struct nb_cb_create_args *args)
{
	return NB_OK;
}

/* 【NB 回调】extcommunity_lb_destroy —— 移除 set "extcommunity-lb"。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_lb_finish —— set "extcommunity-lb" 的 apply_finish 汇总
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-lb
 * 职责：把 lb-type / bandwidth / two-octet-as-specific 三个子叶子汇总成一条
 *   generic_set_add "extcommunity bandwidth" 命令。
 * 关键逻辑（switch on lb_type，枚举 ecommunity_lb_type）：
 *     EXPLICIT_BANDWIDTH   -> 取 uint32 bandwidth，格式化成十进制数字串
 *     CUMULATIVE_BANDWIDTH -> "cumulative"
 *     COMPUTED_BANDWIDTH   -> "num-multipaths"
 *   若 two-octet-as-specific 为 true，再在末尾追加 " non-transitive"。
 * 返回：void。末尾 assert 成功（注释同其它 _finish：失败即崩溃）。
 * 易错点：str 用固定 VTY_BUFSIZ 栈缓冲；switch 的三个分支覆盖了所有枚举，未设 default。 */
void
lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_finish(
	struct nb_cb_apply_finish_args *args)
{
	struct routemap_hook_context *rhc;
	enum ecommunity_lb_type lb_type;
	char str[VTY_BUFSIZ];
	uint32_t bandwidth;
	int ret;

	/* Add configuration. */
	rhc = nb_running_get_entry(args->dnode, NULL, true);
	lb_type = yang_dnode_get_enum(args->dnode, "lb-type");

	/* Set destroy information. */
	rhc->rhc_shook = generic_set_delete;
	rhc->rhc_rule = "extcommunity bandwidth";
	rhc->rhc_event = RMAP_EVENT_SET_DELETED;

	switch (lb_type) {
	case EXPLICIT_BANDWIDTH:
		bandwidth = yang_dnode_get_uint32(args->dnode, "bandwidth");
		snprintf(str, sizeof(str), "%u", bandwidth);
		break;
	case CUMULATIVE_BANDWIDTH:
		snprintf(str, sizeof(str), "%s", "cumulative");
		break;
	case COMPUTED_BANDWIDTH:
		snprintf(str, sizeof(str), "%s", "num-multipaths");
	}

	if (yang_dnode_get_bool(args->dnode, "two-octet-as-specific"))
		strlcat(str, " non-transitive", sizeof(str));

	ret = generic_set_add(rhc->rhc_rmi, "extcommunity bandwidth", str,
			      args->errmsg, args->errmsg_len);
	/*
	 * At this point if this is not a successful operation
	 * bgpd is about to crash.  Let's just cut to the
	 * chase and do it.
	 */
	assert(ret == CMD_SUCCESS);
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb/lb-type
 */
/* 【NB 回调】extcommunity_lb_lb_type_modify —— extcommunity-lb 的 "lb-type" 叶子
 * 对应 XPath：.../extcommunity-lb/lb-type
 * 职责：**空操作**，直接返回 NB_OK（实际处理在 extcommunity_lb_finish）。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_lb_type_modify(
		struct nb_cb_modify_args *args)
{
	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb/bandwidth
 */
/* 【NB 回调】extcommunity_lb_bandwidth_modify —— extcommunity-lb 的 "bandwidth" 叶子
 * 对应 XPath：.../extcommunity-lb/bandwidth
 * 职责：**空操作**，直接返回 NB_OK（实际处理在 extcommunity_lb_finish）。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_bandwidth_modify(
		struct nb_cb_modify_args *args)
{
	return NB_OK;
}

/* 【NB 回调】extcommunity_lb_bandwidth_destroy —— "bandwidth" 叶子删除
 * 职责：**空操作**，直接返回 NB_OK（不做 event 判断与删除动作）。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_bandwidth_destroy(
		struct nb_cb_destroy_args *args)
{
	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-color
 */
/* 【NB 回调】extcommunity_color_modify —— set 动作 "extcommunity-color"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-color
 * 职责：generic_set_add "extcommunity color"（设置 Color 扩展团体，用于 SR/着色策略）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_color_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *str;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		str = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "extcommunity color";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "extcommunity color", str,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_color_destroy —— 移除 set "extcommunity color"。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_color_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb/two-octet-as-specific
 */
/* 【NB 回调】extcommunity_lb_two_octet_as_specific_modify —— "two-octet-as-specific" 叶子
 * 对应 XPath：.../extcommunity-lb/two-octet-as-specific
 * 职责：**空操作**，直接返回 NB_OK（其语义在 extcommunity_lb_finish 里读取并追加
 *   " non-transitive"）。 */
int
lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_two_octet_as_specific_modify(
	struct nb_cb_modify_args *args)
{
	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-none
 */
/* 【NB 回调】extcommunity_none_modify —— set 动作 "extcommunity-none"（布尔叶子）
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:extcommunity-none
 * 职责：读布尔 none；仅当为 true 时 generic_set_add "extcommunity" "none"（清空扩展团体）。
 * 关键分支：none 为 false 时返回 NB_ERR_INCONSISTENCY（与 community_none 同构的行为）。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_none_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	bool none = false;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		none = yang_dnode_get_bool(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "extcommunity";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		if (none) {
			rv = generic_set_add(rhc->rhc_rmi, "extcommunity",
					     "none", args->errmsg,
					     args->errmsg_len);
			if (rv != CMD_SUCCESS) {
				rhc->rhc_shook = NULL;
				return NB_ERR_INCONSISTENCY;
			}
			return NB_OK;
		}

		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

/* 【NB 回调】extcommunity_none_destroy —— 移除 set "extcommunity none"。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_none_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv4
 */
/* 【NB 回调】evpn_gateway_ip_ipv4_modify —— set 动作 "evpn-gateway-ip-ipv4"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv4
 * 职责：generic_set_add "evpn gateway-ip ipv4"（设置 EVPN 网关 IPv4，用于 EVPN 路由）；
 *   失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv4_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "evpn gateway-ip ipv4";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "evpn gateway-ip ipv4", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】evpn_gateway_ip_ipv4_destroy —— 移除 set "evpn gateway-ip ipv4"。 */
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv4_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv6
 */
/* 【NB 回调】evpn_gateway_ip_ipv6_modify —— set 动作 "evpn-gateway-ip-ipv6"
 * 对应 XPath：.../rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv6
 * 职责：generic_set_add "evpn gateway-ip ipv6"；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv6_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "evpn gateway-ip ipv6";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi, "evpn gateway-ip ipv6", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】evpn_gateway_ip_ipv6_destroy —— 移除 set "evpn gateway-ip ipv6"。 */
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv6_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/set-action/rmap-set-action/l3vpn-nexthop-encapsulation
 */
/* 【NB 回调】l3vpn_nexthop_encapsulation_modify —— set 动作 "l3vpn-nexthop-encapsulation"
 * 对应 XPath：.../rmap-set-action/l3vpn-nexthop-encapsulation（注意此叶子直接在
 *   rmap-set-action 下，不走 frr-bgp-route-map 的 augment 前缀，XPath 与其它不同）
 * 职责：generic_set_add "l3vpn next-hop encapsulation"（设置 L3VPN 下一跳封装类型，
 *   如 vxlan/gre）；失败返回 NB_ERR_INCONSISTENCY。 */
int lib_route_map_entry_set_action_rmap_set_action_l3vpn_nexthop_encapsulation_modify(
	struct nb_cb_modify_args *args)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/* Add configuration. */
		rhc = nb_running_get_entry(args->dnode, NULL, true);
		type = yang_dnode_get_string(args->dnode, NULL);

		/* Set destroy information. */
		rhc->rhc_shook = generic_set_delete;
		rhc->rhc_rule = "l3vpn next-hop encapsulation";
		rhc->rhc_event = RMAP_EVENT_SET_DELETED;

		rv = generic_set_add(rhc->rhc_rmi,
				     "l3vpn next-hop encapsulation", type,
				     args->errmsg, args->errmsg_len);
		if (rv != CMD_SUCCESS) {
			rhc->rhc_shook = NULL;
			return NB_ERR_INCONSISTENCY;
		}
	}

	return NB_OK;
}

/* 【NB 回调】l3vpn_nexthop_encapsulation_destroy —— 移除 set "l3vpn next-hop encapsulation"。
 * 本文件到此结束（文件末尾无额外全局数据或注册表）。 */
int lib_route_map_entry_set_action_rmap_set_action_l3vpn_nexthop_encapsulation_destroy(
	struct nb_cb_destroy_args *args)
{
	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		return lib_route_map_entry_set_destroy(args);
	}

	return NB_OK;
}
