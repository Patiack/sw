// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018        Vmware
 *                           Vishal Dhingra
 */
#include <zebra.h>

#include "northbound.h"
#include "libfrr.h"
#include "routing_nb.h"



/* ============================================================================
 * 【文件导读】routing_nb.c —— frr-routing 模型的注册信息(只有两张表，无函数)
 * ============================================================================
 * 【本文件在整条链路里的位置】FRR 里"配置某协议的某 VRF 实例"这件事被抽象成
 * YANG 路径 `/frr-routing:routing/control-plane-protocols/control-plane-protocol`。
 * 各协议守护进程(isisd/ospfd/bgpd...)并不各自定义一套，而是共用这一个列表节点：
 * 每条 control-plane-protocol 由三元组 (type, name, vrf) 唯一确定，其中 type 是
 * 协议名(如 "frr-isis")、name 是实例名、vrf 是该实例所属的 VRF。
 * 真正的 create / destroy 回调实现不在本文件，而在 routing_nb_config.c
 * (由 libfrr.c 的注册流程间接引用)。这里只放两份**表**。
 *
 * 【为什么是两张表而不是一张】注意两份 frr_yang_module_info 同名 "frr-routing"，
 * 区别在 ignore_cfg_cbs：
 *   frr_routing_info     : 带回调，供配置事务(northbound)使用；
 *   frr_routing_cli_info : ignore_cfg_cbs = true，且节点表为空——
 *                          它是给"仅需要 YANG 模型本身、不需要回调"的场合用的
 *                          (例如某些 daemon 只声明自己依赖 frr-routing 模型)。
 * 两张表 xpath 都指向同一个 YANG 模块，注册时按用途各取所需。
 * ============================================================================
 */

/* 【注册表一：带回调】把 control-plane-protocol 列表节点的 create / destroy
 * 交给 routing_nb_config.c 里的实现。回调函数原型见 routing_nb.h。
 * 同样地，`.nodes` 必须以 { .xpath = NULL } 结尾(终止哨兵)。 */
/* clang-format off */
const struct frr_yang_module_info frr_routing_info = {
	.name = "frr-routing",
	.nodes = {
		{
			.xpath = "/frr-routing:routing/control-plane-protocols/control-plane-protocol",
			.cbs = {
				.create = routing_control_plane_protocols_control_plane_protocol_create,
				.destroy = routing_control_plane_protocols_control_plane_protocol_destroy,
			}
		},
		{
			.xpath = NULL,
		},
	}
};

/* 【注册表二：无回调】ignore_cfg_cbs = true 表示"本表声明的节点不参与配置
 * 回调分派"；节点表为空(仅终止哨兵)。用途见文件导读：只为声明对 frr-routing
 * 模型的依赖，不接管任何配置事件。 */
const struct frr_yang_module_info frr_routing_cli_info = {
	.name = "frr-routing",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = NULL,
		},
	}
};
