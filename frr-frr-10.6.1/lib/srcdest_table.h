// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRC-DEST Routing Table
 *
 * Copyright (C) 2017 by David Lamparter & Christian Franke,
 *                       Open Source Routing / NetDEF Inc.
 *
 * This file is part of FRRouting (FRR)
 */

/*
 * ============================================================================
 * 文件导读：lib/srcdest_table.h —— 源地址 + 目的地址 二维路由表封装
 * ============================================================================
 * 背景：标准 IPv6 路由表只按目的前缀索引。当需要支持 source-specific routing
 * （srcdest，即同一个目的前缀下再按源前缀区分）时，FRR 用本文件提供的两级
 * 结构实现：
 *
 *   目的层（dst）：route_table -> srcdest_rnode（前缀=目的） -> info 或 src_table
 *   源层  （src）：src_table  -> route_node（前缀=源）        -> info
 *
 *   非 srcdest 路由（源为 ::/0）与普通路由一致，信息直接放在 info 指针里；
 *   srcdest 路由先按目的查找，再在该目的节点的 src_table 中按源查找。
 *
 * 注意：当某目的前缀下只有 srcdest 路由时，目的 rnode 的 info 可以为 NULL。
 *
 * 核心结构 / 概念：
 *   struct srcdest_rnode  扩展的 route_node（src_table 指向源层子表）；
 *   delegate（_srcdest_dstnode_delegate 与 _srcdest_srcnode_delegate）定制
 *   节点的创建与销毁；用 rnode_is_dstnode / rnode_is_srcnode 判断所处层级。
 *
 * 与其他模块的关系：
 *   - 基于 table.h 的 route_table / route_node / delegate 机制；
 *   - 被 zebra 的 IPv6 路由表在启用 srcdest 时使用（如 Linux 源路由）。
 *
 * 建议阅读顺序：
 *   1) 本文件顶部的结构说明（原有英文注释）；
 *   2) srcdest_rnode 前向声明与两个 delegate 声明；
 *   3) 各导出函数声明；
 *   4) 末尾的 inline 辅助函数（层级判断、取表）。
 */
#ifndef _ZEBRA_SRC_DEST_TABLE_H
#define _ZEBRA_SRC_DEST_TABLE_H

/* old/IPv4/non-srcdest:
 * table -> route_node .info -> [obj]
 *
 * new/IPv6/srcdest:
 * table -...-> srcdest_rnode [prefix = dest] .info -> [obj]
 *                                            .src_table ->
 *         srcdest table -...-> route_node [prefix = src] .info -> [obj]
 *
 * non-srcdest routes (src = ::/0) are treated just like before, their
 * information being directly there in the info pointer.
 *
 * srcdest routes are found by looking up destination first, then looking
 * up the source in the "src_table".  src_table contains normal route_nodes,
 * whose prefix is the _source_ prefix.
 *
 * NB: info can be NULL on the destination rnode, if there are only srcdest
 * routes for a particular destination prefix.
 */

#include "prefix.h"
#include "table.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SRCDEST2STR_BUFFER：把 “目的前缀 from 源前缀” 格式化所需缓冲区长度。
 * 两个前缀各自最长 PREFIX2STR_BUFFER，加中间 " from " 的长度。
 */
#define SRCDEST2STR_BUFFER (2*PREFIX2STR_BUFFER + sizeof(" from "))

/*
 * struct srcdest_rnode：用于 IPv6 srcdest 路由的目的层扩展 route_node。
 * 具体定义在 srcdest_table.c，此处仅前向声明。
 */
/* extended route node for IPv6 srcdest routing */
struct srcdest_rnode;

/*
 * 两个 route_table delegate：
 *   _srcdest_dstnode_delegate  目的层节点（srcdest_rnode）的创建 / 销毁；
 *   _srcdest_srcnode_delegate  源层节点（普通 route_node）的创建 / 销毁。
 * 外部可通过 rnode_is_dstnode / rnode_is_srcnode 比较 rn->table->delegate
 * 是否指向它们来判断节点所属层级。
 */
extern route_table_delegate_t _srcdest_dstnode_delegate;
extern route_table_delegate_t _srcdest_srcnode_delegate;

/*
 * srcdest_table_init：创建一张 srcdest 路由表（目的层，使用 dstnode delegate）。
 * 返回新表指针。
 */
extern struct route_table *srcdest_table_init(void);
/*
 * srcdest_rnode_get：在 srcdest 表中“取得或创建”节点（参考 route_node_get）。
 * 参数：table 表；dst_pu 目的前缀；src_p 源前缀（可为 NULL 或前缀长为 0，
 *       此时等价于普通目的节点）。
 * 返回对应节点并持有一份引用（调用者需 route_unlock_node）。
 */
extern struct route_node *srcdest_rnode_get(struct route_table *table,
					    union prefixconstptr dst_pu,
					    const struct prefix_ipv6 *src_p);
/*
 * srcdest_rnode_lookup：在 srcdest 表中查找节点（参考 route_node_lookup，不创建）。
 * 参数：table 表；dst_pu 目的前缀；src_p 源前缀。
 * 返回匹配节点并持有一份引用；找不到返回 NULL。若目的节点存在但 info 为空
 * 且无源层匹配，也会返回 NULL（与 route_node_lookup 行为一致）。
 */
extern struct route_node *srcdest_rnode_lookup(struct route_table *table,
					       union prefixconstptr dst_pu,
					       const struct prefix_ipv6 *src_p);
/*
 * srcdest_rnode_prefixes：从节点反推其目的前缀与源前缀。
 * 参数：rn 节点；p 输出目的前缀（可 NULL）；src_p 输出源前缀（可 NULL）。
 * 副作用：若 rn 是源节点，则 p 取自其父目的节点、src_p 取自身；否则 p 取自身、
 * src_p 置 NULL。
 */
extern void srcdest_rnode_prefixes(const struct route_node *rn,
				   const struct prefix **p,
				   const struct prefix **src_p);
/*
 * srcdest2str：把 “目的前缀 from 源前缀” 格式化为字符串。
 * 参数：dst_p 目的前缀；src_p 源前缀（NULL 或前缀长为 0 时不输出 from 部分）；
 *       str / size 输出缓冲区（建议用 SRCDEST2STR_BUFFER）。
 * 返回 str。
 */
extern const char *srcdest2str(const struct prefix *dst_p,
			       const struct prefix_ipv6 *src_p,
			       char *str, int size);
/*
 * srcdest_rnode2str：先取出节点对应前缀，再格式化为 “目的 from 源” 字符串。
 * 参数：rn 节点；str / size 输出缓冲区。
 * 返回 str。
 */
extern const char *srcdest_rnode2str(const struct route_node *rn, char *str,
				     int size);
/*
 * srcdest_route_next：srcdest 感知的遍历函数（对应 route_next）。
 * 参数：rn 当前节点。
 * 返回下一个节点并持有一份引用，遍历结束时返回 NULL。
 * 说明：会先遍历某目的的源层子表，再回到目的层继续；非 srcdest 节点直接
 * 退化为 route_next。调用者需负责 route_unlock_node。
 */
extern struct route_node *srcdest_route_next(struct route_node *rn);

/*
 * rnode_is_dstnode：判断节点是否位于目的层（其表的 delegate 为 dstnode）。
 * 返回非零表示是目的层节点。
 */
static inline int rnode_is_dstnode(const struct route_node *rn)
{
	return rn->table->delegate == &_srcdest_dstnode_delegate;
}

/*
 * rnode_is_srcnode：判断节点是否位于源层（其表的 delegate 为 srcnode）。
 * 返回非零表示是源层节点。
 */
static inline int rnode_is_srcnode(const struct route_node *rn)
{
	return rn->table->delegate == &_srcdest_srcnode_delegate;
}

/*
 * srcdest_rnode_table：取节点“所属的那张顶层表”。
 * 若 rn 是源层节点，则通过其表的 info 找到父目的节点，再返回父节点的表；
 * 否则直接返回 rn->table。
 */
static inline struct route_table *srcdest_rnode_table(struct route_node *rn)
{
	if (rnode_is_srcnode(rn)) {
		struct route_node *dst_rn =
			(struct route_node *)route_table_get_info(rn->table);
		return dst_rn->table;
	} else {
		return rn->table;
	}
}
/*
 * srcdest_rnode_table_info：取节点所属顶层表的 info 指针。
 * 便于在遍历源层节点时访问挂在顶层表上的遍历状态等。
 */
static inline void *srcdest_rnode_table_info(struct route_node *rn)
{
	return route_table_get_info(srcdest_rnode_table(rn));
}

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_SRC_DEST_TABLE_H */
