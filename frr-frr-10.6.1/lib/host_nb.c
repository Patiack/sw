// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 12 2025, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2025, LabN Consulting, L.L.C.
 *
 */
#include <zebra.h>
#include "northbound.h"
#include "command.h"
#include "host_nb.h"

/* ============================================================================
 * 【文件导读】host_nb.c —— 把 YANG 模型 frr-host 接到全局 host 结构体上
 * ============================================================================
 * 【为什么会有这个文件】FRR 正在把"旧 CLI 直接改内存"的模式迁移到 YANG 北向
 * 接口(northbound)：每一条配置都要先在 YANG 模型里有一个节点，再提供回调把
 * 节点的值落到真正的数据结构里。本文件就是 `frr-host` 这个模型里
 * allow-reserved-ranges 这一个节点的回调实现，落点就是 command.h 里的
 * `struct host`。
 *
 * 【这个开关干什么】默认情况下路由器不使用 IPv4 Class E(240.0.0.0/4) 这段
 * 保留地址；打开 allow-reserved-ranges 后允许把这段地址当普通地址使用。
 * 字段本体是 command.h 中 `struct host::allow_reserved_ranges`。
 *
 * 【看回调要盯的两点】① 事务事件过滤：northbound 的回调会在多个事务阶段被
 * 调用(CREATE/APPLY/DESTROY...)，绝大多数回调只关心 APPLY —— 因为只有 APPLY
 * 阶段才允许产生副作用(改内存、发报文)。本文件里那行 `if (event != NB_EV_APPLY)
 * return NB_OK;` 就是这道闸。② 模块注册表：文件尾部的 frr_host_nb_info 把
 * xpath 与回调绑定，daemon 启动时统一注册；漏注册则配置永远进不来。
 * ============================================================================
 */
/*
 * XPath: /frr-host:host/allow-reserved-ranges
 */
/* 【回调】allow-reserved-ranges 节点的 modify 事件处理器。
 *   args: northbound 的通用参数包，args->event 是当前事务阶段，
 *         args->dnode 指向被修改的 YANG 数据节点。
 *   返回: 固定 NB_OK(本回调不会失败)。
 * 【要点】只在 NB_EV_APPLY 阶段真正写内存；其它阶段(如 prepare/validation)
 *   直接返回成功——**不是**错误。第二个参数 NULL 表示"从 dnode 自身取值"
 *   (相对路径为空)。这句写完之后值就立刻生效，没有额外的"提交"步骤。 */
static int host_allow_reserved_ranges_modify(struct nb_cb_modify_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	host.allow_reserved_ranges = yang_dnode_get_bool(args->dnode, NULL);

	return NB_OK;
}

/* 【模块注册表】frr-host 模块的节点清单。
 *   .name : 必须是 YANG 模型名，与 frr-host.yang 的 `module frr-host` 一致，
 *           写错会在注册时直接报错找不到模块。
 *   .nodes: 以 xpath 为键的静态数组，**必须以 { .xpath = NULL } 结尾**——
 *           它是遍历终止哨兵，漏掉会导致越界读。
 *   .priority: 同一事务里多个回调的执行顺序，数值越小越先跑。这里用
 *           `NB_DFLT_PRIORITY - 3`(默认值减 3)表示"要早于大多数模块"。
 *           用相对值而不是硬编码数字，是为了跟框架默认值保持联动。
 *   clang-format off/on 包夹是为了保住表格式排版。 */
/* clang-format off */
const struct frr_yang_module_info frr_host_nb_info = {
	.name = "frr-host",
	.nodes = {
		{ .xpath = "/frr-host:host/allow-reserved-ranges",
		  .cbs.modify = host_allow_reserved_ranges_modify,
		  .priority = NB_DFLT_PRIORITY - 3,
		},
		{ .xpath = NULL },
	}
};
/* clang-format on */
