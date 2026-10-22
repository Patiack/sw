// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * affinity map northbound implementation.
 *
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 */

/*
 * 【文件导读】affinitymap_northbound.c —— 亲和映射的 YANG 北向回调实现
 * =====================================================================
 * 【职责】实现 /frr-affinity-map:lib/affinity-maps 子树的数据回调。当 CLI(或其它
 *   northbound 客户端)通过 nb_cli_apply_changes 提交配置时，northbound 框架按
 *   “校验(VALIDATE) -> 预备(PREPARE) -> 应用(APPLY) / 回滚(ABORT)”事务流程逐个
 *   调用本文件里的回调。
 *
 * 【YANG 树结构】
 *   /frr-affinity-map:lib/affinity-maps/affinity-map        <- list，key 为 name
 *   /frr-affinity-map:lib/affinity-maps/affinity-map/name   <- leaf，映射名
 *   /frr-affinity-map:lib/affinity-maps/affinity-map/value  <- leaf，位下标
 *
 * 【回调与事件的对应】
 *   create  : 仅返回 NB_OK(占位)——真正建映射发生在 value 的 modify。
 *   destroy : 在 NB_EV_APPLY 时调用 affinity_map_unset(name)。
 *   modify  : 在 NB_EV_APPLY 时先 affinity_map_update_hook(name, pos)
 *             (对已存在的映射报告旧->新变更)，再 affinity_map_set(name, pos)。
 *
 * 【为什么 create 是空实现】因为“位号”才是映射的实质内容；只创建 name 而没设
 *   value 的中间态无需落库，等 value 的 modify 一并处理即可(注意 YANG 里 value 是
 *   必选 leaf，故实际不会长期处于缺 value 的状态)。
 *
 * 【事务纪律】除 APPLY 外的 VALIDATE / PREPARE / ABORT 事件一律不产生副作用，
 *   这保证配置提交失败时可安全回滚。
 * =====================================================================
 */
#include <zebra.h>

#include "lib/command.h"
#include "lib/log.h"
#include "lib/northbound.h"
#include "lib/affinitymap.h"

/*
 * XPath: /frr-affinity-map:lib/affinity-maps/affinity-map
 */

/* 【回调】list 节点(一条 affinity-map)的 create。
 * 参数 args : 北向回调参数(含 dnode、event 等)。
 * 返回：恒为 NB_OK。
 * 说明：有意留空——建映射的实质动作放在 value 的 modify 里；此处仅满足框架要求。 */
static int lib_affinity_map_create(struct nb_cb_create_args *args)
{
	return NB_OK;
}

/* 【回调】list 节点(一条 affinity-map)的 destroy。
 * 参数 args : 北向回调参数；args->event 决定事务阶段，args->dnode 指向被删节点。
 * 返回：恒为 NB_OK。
 * 副作用：仅在 NB_EV_APPLY 阶段调用 affinity_map_unset(name) 真正删除；
 *   VALIDATE / PREPARE / ABORT 阶段不做任何事。
 * 取值：name 从 dnode 的 ./name 子 leaf 读取。 */
static int lib_affinity_map_destroy(struct nb_cb_destroy_args *args)
{
	const char *name;

	name = yang_dnode_get_string((const struct lyd_node *)args->dnode,
				     "./name");

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		affinity_map_unset(name);
		break;
	}
	return NB_OK;
}

/*
 * XPath: /frr-affinity-map:lib/affinity-maps/affinity-map/value
 */
/* 【回调】leaf value(位下标) 的 modify。
 * 参数 args : 北向回调参数；name / pos 都从 args->dnode 的**父节点**读取。
 * 返回：恒为 NB_OK。
 * 副作用：仅在 NB_EV_APPLY 阶段：
 *   1) affinity_map_update_hook(name, pos) —— 通知旧->新位号变更(若映射已存在)；
 *   2) affinity_map_set(name, pos)         —— 落库(存在则改、不存在则新建)。
 * 顺序上很重要：先 update_hook(取旧值)再 set(改新值)，否则旧值已被覆盖。
 * 易踩的坑：args->dnode 是 value 这个 leaf，name 在其父(list 条目)上，故要先
 *   args->dnode->parent 再取 ./name。 */
static int lib_affinity_map_value_modify(struct nb_cb_modify_args *args)
{
	const char *name;
	uint16_t pos;

	name = yang_dnode_get_string(
		(const struct lyd_node *)args->dnode->parent, "./name");

	pos = yang_dnode_get_uint16(
		(const struct lyd_node *)args->dnode->parent, "./value");

	switch (args->event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		affinity_map_update_hook(name, pos);
		affinity_map_set(name, pos);
		break;
	}

	return NB_OK;
}

/* clang-format off */
/* 【YANG 模块描述符】frr-affinity-map 的北向回调表(数据 / 配置侧)。
 *   节点 1 : /.../affinity-map       -> create + destroy，
 *            priority = NB_DFLT_PRIORITY - 1(略早于默认优先级执行，
 *            以保证订阅者回调顺序正确)。
 *   节点 2 : /.../affinity-map/value -> modify。
 * 说明：上方的 clang-format off 为源码原有开关，用于避免格式化破坏节点表布局。 */
const struct frr_yang_module_info frr_affinity_map_info = {
	.name = "frr-affinity-map",
	.nodes = {
		{
			.xpath = "/frr-affinity-map:lib/affinity-maps/affinity-map",
			.cbs = {
				.create = lib_affinity_map_create,
				.destroy = lib_affinity_map_destroy,
			},
			.priority = NB_DFLT_PRIORITY - 1,
		},
		{
			.xpath = "/frr-affinity-map:lib/affinity-maps/affinity-map/value",
			.cbs = {
				.modify = lib_affinity_map_value_modify,
			}
		},
		{
			.xpath = NULL,
		},
	}
};
