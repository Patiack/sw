// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Addpath TX ID selection, and related utilities
 * Copyright (C) 2018  Amazon.com, Inc. or its affiliates
 */
/* ============================================================================
 * 【文件导读】bgp_addpath_types.h —— Add-Path(RFC7911) **发送侧**的数据结构
 * ============================================================================
 * 【Add-Path 解决什么问题】标准 BGP 一条前缀只通告**一条**最佳路径；Add-Path 允许
 * 同时通告**多条**（每条用 4 字节 Path-ID 区分）。好处：接收端可以做多路径负载
 * 分担、快速收敛（备用路径已在本地，无需重学）。代价是 UPDATE 量变大。
 *
 * 【发送侧的三个决策维度】本文件的结构体就是围绕这三层展开的：
 *   1) **策略(strategy)** —— 哪几条路径值得发？见 enum bgp_addpath_strat：
 *         ALL           : 全部路径都发（最激进，流量最大）
 *         BEST_PER_AS   : 每个 AS 里挑一条最佳发（常用于给上游做 AS 级多路径）
 *         BEST_SELECTED : 只发被选中的那几条（通常是 multipath 的最佳集合）
 *         NONE          : 不发 Add-Path
 *      ★ 注意枚举里 BGP_ADDPATH_MAX(=3) 与 BGP_ADDPATH_NONE(=4) 的**相对顺序**：
 *        MAX 不是“最大策略”，而是**数组维度**(有 3 种真实策略)，用来定义
 *        `xxx[BGP_ADDPATH_MAX]`；NONE 排在它之后，所以**不能**用 MAX 去索引
 *        “无策略”这个状态。读数组声明时务必分清。
 *   2) **粒度(granularity)** —— 三个结构体对应三层对象上的状态：
 *         bgp_addpath_bgp_data    : 每个 bgp 实例一份（全局计数 + ID 分配器）
 *         bgp_addpath_node_data   : 每个前缀(bgp_dest)一份（该前缀的空闲 ID 池）
 *         bgp_addpath_info_data   : 每条路径(bgp_path_info)一份（已分配的 TX ID）
 *   3) **ID 分配** —— TX ID 是**发送方向对端宣告**的编号，必须唯一、可回收。
 *      分配策略是“低编号优先 + 用完回收”：一个前缀下用一个 id_allocator 统一
 *      分配，ID 归还进该前缀的 free_id 池后再复用（这就是为什么每个 dest 还要
 *      单独维护 free_ids，而不能只用全局分配器）。
 *
 * 【阅读建议】先看 enum(策略有哪几种) → 再看三个 struct 分别挂在哪一层 →
 *            最后回到 bgp_addpath.c 看 ID 是怎么分配/回收的。
 *            注意本文件只有“发送侧(TX)”；接收侧(RX)的 path_id 在 bgp_route.h /
 *            bgp_packet.c 里。
 * ============================================================================
 */

#ifndef _QUAGGA_BGPD_TX_ADDPATH_DATA_H
#define _QUAGGA_BGPD_TX_ADDPATH_DATA_H
#include "lib/id_alloc.h"
#include <stdint.h>

/* 【枚举】发送侧 Add-Path 策略：决定“一条前缀的哪些路径要被通告出去”。
 * 取值顺序有意义——前三个是**真实策略**，是数组维度；后两个不是：
 *   BGP_ADDPATH_MAX  : 数组维度的上界(=3)，即真实策略的个数；
 *   BGP_ADDPATH_NONE : “不发 Add-Path”这个状态本身。
 * 因此 BGP_ADDPATH_NONE > BGP_ADDPATH_MAX，**用它当数组下标会越界**。
 * 配置名/人类可读名的映射见 bgp_addpath.c 里的策略表。 */
enum bgp_addpath_strat {
	BGP_ADDPATH_ALL = 0,
	BGP_ADDPATH_BEST_PER_AS,
	BGP_ADDPATH_BEST_SELECTED,
	BGP_ADDPATH_MAX,
	BGP_ADDPATH_NONE,
};

/* TX Addpath structures */
/* 【结构】bgp_addpath_bgp_data —— 挂在 `struct bgp` 上的**全局**Add-Path 状态。
 *   peercount[afi][safi][strategy]      : 各(地址族, SAFI, 策略)组合下有多少个
 *                                         peer 启用了 Add-Path。用于快速判断
 *                                        “这个组合到底用没用 Add-Path”，避免每
 *                                        次都遍历所有 peer。
 *   total_peercount[afi][safi]          : 上述在策略维度上的汇总（该 AFI/SAFI 下
 *                                        启用 Add-Path 的 peer 总数）。
 *   id_allocators[afi][safi][strategy]  : ID 分配器，为该维度下的路径分配 TX ID。
 * 为什么按策略分开计数/分配：同一条路径在不同策略下可能是“要发的”或“不要发的”，
 * TX ID 的分配空间也应按策略独立，否则改策略会互相干扰。 */
struct bgp_addpath_bgp_data {
	unsigned int peercount[AFI_MAX][SAFI_MAX][BGP_ADDPATH_MAX];
	unsigned int total_peercount[AFI_MAX][SAFI_MAX];
	struct id_alloc *id_allocators[AFI_MAX][SAFI_MAX][BGP_ADDPATH_MAX];
};

/* 【结构】bgp_addpath_node_data —— 挂在 `bgp_dest`(某条前缀) 上的**每前缀**状态。
 *   free_ids[strategy] : 该前缀下按策略维护的“空闲 TX ID 池”。
 * 为什么要每前缀一份池：TX ID 的语义是“该前缀的第几条路径”，同一个 ID 在不同
 * 前缀下是**可以重复使用**的（收到的报文也是按 (前缀, path_id) 定位）。若只用
 * 全局分配器，ID 会被无谓地耗尽，且删除前缀时无法把它的 ID 收回来。
 * 所以：分配时从本前缀的池里取，路径删除时把 ID 还回**本前缀**的池。 */
struct bgp_addpath_node_data {
	struct id_alloc_pool *free_ids[BGP_ADDPATH_MAX];
};

/* 【结构】bgp_addpath_info_data —— 挂在 `bgp_path_info`(某条具体路径) 上的状态。
 *   addpath_tx_id[strategy] : 该路径在各策略下被分配到的发送用 TX ID（0 表示未分配）。
 * 一条路径可能**同时**在多种策略下生效（例如既满足 ALL 又在 BEST_PER_AS 里），
 * 所以按策略分别记录，而不是只存一个 ID。 */
struct bgp_addpath_info_data {
	uint32_t addpath_tx_id[BGP_ADDPATH_MAX];
};

/* 【结构】bgp_addpath_strategy_names —— 每种策略的“5 种叫法”集中在一个表里，
 * 避免各处硬编码字符串、也保证各输出渠道(配置/文本 show/JSON)措辞一致。
 * 下面英文注释已说明各自用途，这里补充两句：
 *   - 前三个是**给人看/给配置解析**的（config_name 用于 `addpath-tx-*` 命令）；
 *   - 后两个是**给机器解析**的 JSON 键名，改动它们属于接口变更，会影响脚本。 */
struct bgp_addpath_strategy_names {
	const char *config_name;
	const char *human_name;	       /* path detail non-json */
	const char *human_description; /* non-json peer descriptions */
	const char *type_json_name;    /* json peer listings */
	const char *id_json_name;      /* path json output for tx ID# */
};

#endif
