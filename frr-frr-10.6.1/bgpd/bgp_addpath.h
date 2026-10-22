// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Addpath TX ID selection, and related utilities
 * Copyright (C) 2018  Amazon.com, Inc. or its affiliates
 */
/* ============================================================================
 * 【文件导读】bgp_addpath.h —— Add-Path(发送侧) 的对外接口
 * ============================================================================
 * 【数据结构见 bgp_addpath_types.h】本文件只放接口；三层结构体
 * (bgp_addpath_bgp_data / _node_data / _info_data) 与策略枚举在 types.h 里，
 * 建议先看那份文件头的导读。
 *
 * 【本文件回答的问题】
 *   1) 某个 (peer, afi, safi) 到底用不用 Add-Path、用哪种策略？
 *      → bgp_addpath_is_addpath_used()、bgp_addpath_set_peer_type()
 *   2) 给定策略，一条路径**该不该发**？
 *      → bgp_addpath_tx_path()  ← 发送侧最核心的判据
 *   3) 发送时用哪个 TX ID？
 *      → bgp_addpath_id_for_peer()
 *   4) 路径/前缀/实例增删时，ID 如何分配与回收？
 *      → bgp_addpath_update_ids() 与三个 free_*()
 *
 * 【与更新组的关系】Add-Path 让“同一条前缀要多条路径”，这直接影响
 * bgp_updgrp_adv / bgp_updgrp_packet 里“一个 dest 对应多少个待发条目”的逻辑；
 * 但注意：**同一个子组内的 peer 必须策略一致**才能共用一段 UPDATE——这也是
 * 更新组分组时要考虑 Add-Path 配置的原因（见 bgp_updgrp.c 的分组键）。
 *
 * 【阅读建议】enum 策略 → bgp_addpath_is_addpath_used → bgp_addpath_tx_path
 *            → bgp_addpath_id_for_peer → bgp_addpath_update_ids。
 * ============================================================================
 */

#ifndef _QUAGGA_BGPD_TX_ADDPATH_H
#define _QUAGGA_BGPD_TX_ADDPATH_H

#include <stdint.h>
#include <zebra.h>

#include "bgpd/bgp_addpath_types.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_table.h"
#include "lib/json.h"

/* 【常量】默认路由(network/default-originate 产生的那条)固定使用的 TX ID。
 * 为什么写死成 1：默认路由是本机自己产生的、永远只有一条，不参与“多条路径
 * 竞争 ID”的分配；固定值让各端行为一致且便于排障（看到 path-id=1 就知道是
 * 本机 orig 出来的默认路由）。 */
#define BGP_ADDPATH_TX_ID_FOR_DEFAULT_ORIGINATE 1

/* 【结构】bgp_paths_limit_capability —— RFC 7911 里 Add-Path 能力的“路径数上限”
 * 部分的**线上格式**：(afi, safi, paths_limit) 三元组序列。
 * 注意 __attribute__((packed))：这是要按字节序列化进 OPEN 报文的，编译器
 * 若插了填充字节，对端就解析不了——packed 在这里是**必需**的，别以为是随手加的。
 * 该能力见 bgp_open.c 的 bgp_capability_parse_addpath / 组包部分。 */
struct bgp_paths_limit_capability {
	uint16_t afi;
	uint8_t safi;
	uint16_t paths_limit;
} __attribute__((packed));

/* 【函数】bgp_addpath_init_bgp_data —— 初始化某个 bgp 实例的 Add-Path 全局数据
 * (计数清零 + 建立各维度的 ID 分配器)。在 bgp 实例创建时调用。 */
void bgp_addpath_init_bgp_data(struct bgp_addpath_bgp_data *d);

/* 【函数】bgp_addpath_is_addpath_used —— 该 (afi, safi) 下是否**有任何一个** peer
 * 启用了 Add-Path。实现就是看 total_peercount 是否非 0（O(1)，不遍历 peer）。
 * 用途：快速短路——若整个 AFI/SAFI 都没人用 Add-Path，发送/选路路径上所有
 * Add-Path 相关处理都可以直接跳过，不必逐 peer 判断。 */
bool bgp_addpath_is_addpath_used(struct bgp_addpath_bgp_data *d, afi_t afi,
				 safi_t safi);

/* 【函数】bgp_addpath_free_node_data —— 销毁**某个前缀**的 Add-Path 状态：
 * 把该前缀各策略下的空闲 ID 池还回全局分配器，再释放自身。
 * bd 是所属实例的全局数据（ID 池要还给它），afi/safi 用于定位对应维度的分配器。
 * 前缀被删除(bgp_dest 释放)时调用——漏掉会造成 ID 池泄漏。 */
void bgp_addpath_free_node_data(struct bgp_addpath_bgp_data *bd,
			      struct bgp_addpath_node_data *nd,
			      afi_t afi, safi_t safi);

/* 【函数】bgp_addpath_free_info_data —— 释放**某条路径**占用的 TX ID：把各策略下
 * 已分配的 ID 还回该前缀(nd)的空闲池。路径从 bgp_dest 上摘除时调用。
 * 与 free_node_data 的区别：这个是“还 ID 给本前缀”，那个是“还池给全局”。 */
void bgp_addpath_free_info_data(struct bgp_addpath_info_data *d,
			      struct bgp_addpath_node_data *nd);


/* 【函数】bgp_addpath_info_has_ids —— 该路径在**任一**策略下是否已分配了 TX ID。
 * 用途：判断“这条路径已经在按 Add-Path 发送了吗”，避免重复分配 / 判断是否需要
 * 先分配再发送。 */
bool bgp_addpath_info_has_ids(struct bgp_addpath_info_data *d);

/* 【函数】bgp_addpath_id_for_peer —— 取“发给该 peer 时，这条路径应使用哪个 TX ID”。
 * 为什么要带 peer：ID 是按**策略**分配的，而每个 peer 用的策略可能不同
 * （peer->addpath_type[afi][safi]），所以要先把 peer 的策略翻译成策略枚举，
 * 再从该路径的 addpath_tx_id[strategy] 里取值。
 * 返回值就是将要写进 UPDATE 的 4 字节 Path-ID。 */
uint32_t bgp_addpath_id_for_peer(struct peer *peer, afi_t afi, safi_t safi,
				struct bgp_addpath_info_data *d);

/* 【函数】bgp_addpath_names —— 由策略枚举取到那组名字(配置名/文本名/JSON 键)，
 * 返回的是静态常量表里的一项（不要释放，也不要修改）。 */
const struct bgp_addpath_strategy_names *
bgp_addpath_names(enum bgp_addpath_strat strat);

/* 【函数】bgp_addpath_dmed_required —— 该策略下是否需要处理 dmed
 * (deterministic MED，确定性的 MED 比较；参见 bgp_mpath.c)。
 * 为什么相关：Add-Path 按 BEST_PER_AS 这类策略挑选“每个 AS 的最佳路径”时，
 * 若 MED 比较结果不稳定，选出的路径会随报文到达顺序漂移，对端看到的 TX ID
 * 集合也随之抖动。具体判据在 bgp_addpath.c 的实现里，读的时候对照核一下。 */
bool bgp_addpath_dmed_required(int strategy);

/*
 * 【函数】bgp_addpath_tx_path —— 发送侧**最核心的判据**：给定策略 strat，这条
 * 路径 pi 是否应该被作为 Add-Path 额外通告出去（返回 true 表示“要发”）。
 * 调用时机：为某子组/peer 决定“这条前缀要发几条路径”时，对每条路径逐条问一遍。
 * 注意它不是“这条路径是否是最佳路径”——最佳路径由选路(bgp_route.c)决定、
 * 且一定是会发的；本函数回答的是**除最佳之外**还要不要多发几条。
 * Return true if this is a path we should advertise due to a configured
 * addpath-tx knob
 */
bool bgp_addpath_tx_path(enum bgp_addpath_strat strat,
			 struct bgp_path_info *pi);
/*
 * 【函数】bgp_addpath_set_peer_type —— 改某 peer 在 (afi, safi) 下的 Add-Path
 * 策略：更新 peercount 计数、必要时创建/更新 ID 分配器、并把 peer 自己的
 * addpath_type / paths_limit 记下来（供 bgp_addpath_id_for_peer 反查）。
 * paths 是“路径数上限”，0 表示不限制。
 * 注意：改策略会影响**更新组的分组**（同组 peer 必须策略一致），所以调用后
 * 通常需要触发重新分组——这就是下面 bgp_addpath_type_changed() 的用途。
 * Change the type of addpath used for a peer.
 */
void bgp_addpath_set_peer_type(struct peer *peer, afi_t afi, safi_t safi,
			       enum bgp_addpath_strat addpath_type, uint16_t paths);

/* 【函数】bgp_addpath_update_ids —— 某前缀(dest)上的路径集合发生变化后，重新
 * 计算/分配它各条路径的 TX ID（新增路径要分 ID、消失的路径要还 ID）。
 * 调用时机：最佳路径变更、路径增删之后。这是发送侧 ID 状态的**收敛点**——
 * 排障“对端收到的 path-id 不对/重复”时从这里查。 */
void bgp_addpath_update_ids(struct bgp *bgp, struct bgp_dest *dest, afi_t afi,
			    safi_t safi);

/* 【函数】bgp_addpath_type_changed —— 有 peer 的 Add-Path 策略变化后调用，触发
 * 受影响 peer 的**重新分组**(update-group)与必要的路由重发。
 * 为什么必须做：Add-Path 策略是更新组的分组键之一，策略不同的 peer 不能共享
 * 同一段 UPDATE；不重新分组会出现“发给 A 的报文里带了 B 不该收的 path-id”。 */
void bgp_addpath_type_changed(struct bgp *bgp);
#endif
