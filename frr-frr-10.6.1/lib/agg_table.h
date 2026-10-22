// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * agg_table - Aggregate Table Header
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *               Donald Sharp
 */
/*
 * 【文件导读】agg_table.h —— 聚合表(Aggregate Table)头文件
 * =====================================================================
 * 【是什么】聚合表是“把多条明细路由按某个粒度归拢展示”的容器。典型用途是
 *   BGP 汇总 / show 命令把同前缀族的多条路径聚合到一条“聚合节点”上，节点上挂
 *   一个不透明的 void *aggregate 指针，由调用方(如 bgpd)塞入自己的聚合数据。
 *
 * 【实现方式：复用 route_table】它并不自建数据结构，而是**完全复用**
 *   struct route_table / struct route_node 这套按前缀组织的前缀树(见 table.h)。
 *   做法是把 agg_node 定义成“头部若干字段与 route_node 二进制兼容”的结构：
 *   开头放 ROUTE_NODE_FIELDS，后面追加聚合专用字段 aggregate。
 *   于是同一块内存既能被 agg_* 当 agg_node 用，也能被 route_* 当 route_node 用，
 *   二者通过下面那组强转(agg_node_to_rnode / agg_node_from_rnode)来回切换。
 *
 * 【关键前提】ROUTE_NODE_FIELDS 必须是 agg_node 的**前导**字段——头文件里那段
 *   Caution 注释就是在强调这点。一旦在它前面加字段，两个视图就错位了。
 *
 * 【不透明 info】agg_table 里也带一个 void *info，配合 agg_(set/get)_table_info
 *   让调用方把“表级别的私有数据”挂上去(与节点级的 aggregate 对应)。
 *
 * 【阅读顺序】两个结构体 -> 视图转换(强转) -> 锁 / 解锁 -> info 存取
 *   -> 遍历(top / next / next_until) -> 查找(get / lookup / match)
 *   -> 树形导航(parent / left / right) -> 初始化 / 销毁 -> 杂项。
 * =====================================================================
 */
#ifndef __AGG_TABLE_H__
#define __AGG_TABLE_H__

#include "prefix.h"
#include "table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 【结构体】聚合表句柄。
 *   route_table : 底层复用的一般路由表(前缀树)，真正的存储都在这里。
 *   info        : 表级不透明私有数据；由调用方通过 agg_set_table_info 挂载。 */
struct agg_table {
	struct route_table *route_table;

	void *info;
};

/* 【结构体】聚合表节点。前导字段 ROUTE_NODE_FIELDS 使其与 route_node 二进制兼容。
 *   aggregate : 节点级不透明私有数据(如 bgpd 的 bgp_aggregate 指针)；
 *   本头文件只负责搬运该指针，不解释其含义。 */
struct agg_node {
	/*
	 * Caution these must be the very first fields
	 * @see agg_node_to_rnode
	 * @see agg_node_from_rnode
	 */
	ROUTE_NODE_FIELDS

	/* Aggregation. */
	void *aggregate;
};

/* 【视图转换】把 agg_node 指针当作 route_node 用(上行到通用实现)。
 * 依据：ROUTE_NODE_FIELDS 是前导字段，两者地址相同、布局兼容，故为纯强转。
 * 【易踩的坑】仅当 node 确为聚合节点时才成立；对普通 route_node 误用会类型混淆。 */
static inline struct route_node *agg_node_to_rnode(struct agg_node *node)
{
	return (struct route_node *)node;
}

/* 【视图转换】把 route_node 指针当作 agg_node 用(由通用层下行回聚合视图)。
 * 与 agg_node_to_rnode 互逆。 */
static inline struct agg_node *agg_node_from_rnode(struct route_node *node)
{
	return (struct agg_node *)node;
}

/* 【锁】增加节点引用计数并返回聚合视图。
 * 说明：底层 route_lock_node 维护 route_node.lock；聚合层沿用同一计数。
 * 返回：传入的 node(聚合视图)。 */
static inline struct agg_node *agg_lock_node(struct agg_node *node)
{
	return (struct agg_node *)route_lock_node(agg_node_to_rnode(node));
}

/* 【解锁】减少节点引用计数；归零时底层可能释放节点。
 * 返回：无。副作用：可能触发节点回收——解锁后不应再访问该 node。 */
static inline void agg_unlock_node(struct agg_node *node)
{
	route_unlock_node(agg_node_to_rnode(node));
}

/* 【存取】设置聚合表的表级私有数据(info 指针)。纯赋值，不做生命周期管理。 */
static inline void agg_set_table_info(struct agg_table *atable, void *data)
{
	atable->info = data;
}

/* 【存取】读取表级私有数据(见 agg_set_table_info)，未设置时为 NULL。 */
static inline void *agg_get_table_info(struct agg_table *atable)
{
	return atable->info;
}

/* 【遍历】取路由表的“顶”(遍历起点哨兵)，返回聚合视图。
 * 配合 agg_route_next 做整表遍历；顶节点不是真实前缀。 */
static inline struct agg_node *agg_route_top(struct agg_table *table)
{
	return agg_node_from_rnode(route_top(table->route_table));
}

/* 【遍历】取当前节点在树序下的下一个节点，返回聚合视图。
 * 返回：下一节点；遍历结束返回 NULL。
 * 副作用：内部会解锁当前节点(与 route_next 语义一致)，遍历中勿再访问旧 node。 */
static inline struct agg_node *agg_route_next(struct agg_node *node)
{
	return agg_node_from_rnode(route_next(agg_node_to_rnode(node)));
}

/* 【查找/创建】按前缀 p 获取节点，不存在则创建(route_node_get 语义)。
 * 返回：聚合视图节点(引用计数已加)；失败返回 NULL。 */
static inline struct agg_node *agg_node_get(struct agg_table *table,
					    const struct prefix *p)
{
	return agg_node_from_rnode(route_node_get(table->route_table, p));
}

static inline struct agg_node *
/* 【查找】按前缀 p 查找已存在的节点，不创建(route_node_lookup 语义)。
 * 参数 table 用 const 修饰，表示查找不改表。
 * 返回：命中节点的聚合视图；未命中返回 NULL。 */
agg_node_lookup(const struct agg_table *const table, const struct prefix *p)
{
	return agg_node_from_rnode(route_node_lookup(table->route_table, p));
}

/* 【遍历】从 node 逐个 next，直到 limit(不含)为止。
 * 参数 limit : 停止边界节点(为 NULL 表示不设边界，等同 agg_route_next)。
 * 返回：下一节点聚合视图；到达 limit 或遍历结束返回 NULL。
 * 【易踩的坑】limit 自身不会被访问；node 与 limit 都是聚合视图。 */
static inline struct agg_node *agg_route_next_until(struct agg_node *node,
						    struct agg_node *limit)
{
	struct route_node *rnode;

	rnode = route_next_until(agg_node_to_rnode(node),
				agg_node_to_rnode(limit));

	return agg_node_from_rnode(rnode);
}

/* 【查找】按前缀 p 做“最长匹配”查找(route_node_match 语义)。
 * 返回：匹配到的最具体节点的聚合视图；无匹配返回 NULL。 */
static inline struct agg_node *agg_node_match(struct agg_table *table,
					      const struct prefix *p)
{
	return agg_node_from_rnode(route_node_match(table->route_table, p));
}

/* 【导航】取父节点聚合视图(树根返回 NULL)。 */
static inline struct agg_node *agg_node_parent(struct agg_node *node)
{
	struct route_node *rn = agg_node_to_rnode(node);

	return agg_node_from_rnode(rn->parent);
}

/* 【导航】取左(0 位分支)子节点聚合视图。 */
static inline struct agg_node *agg_node_left(struct agg_node *node)
{
	struct route_node *rn = agg_node_to_rnode(node);

	return agg_node_from_rnode(rn->l_left);
}

/* 【导航】取右(1 位分支)子节点聚合视图。 */
static inline struct agg_node *agg_node_right(struct agg_node *node)
{
	struct route_node *rn = agg_node_to_rnode(node);

	return agg_node_from_rnode(rn->l_right);
}

/* 【外部函数】创建并返回一个空的聚合表(实现在 agg_table.c)。
 * 返回：新表指针，需用 agg_table_finish 释放。 */
extern struct agg_table *agg_table_init(void);

/* 【销毁】释放聚合表：先 route_table_finish 释放底层前缀树，把 route_table 置 NULL，
 * 再用 MTYPE_TMP 释放 agg_table 本体。
 * 返回：无。副作用：atable 之后不可再用。 */
static inline void agg_table_finish(struct agg_table *atable)
{
	route_table_finish(atable->route_table);
	atable->route_table = NULL;

	XFREE(MTYPE_TMP, atable);
}

/* 【遍历】从**节点**出发取其所处路由表的顶(遍历起点)。
 * 与 agg_route_top 的区别：那个从表取顶，这个从节点取。 */
static inline struct agg_node *agg_route_table_top(struct agg_node *node)
{
	return (struct agg_node *)route_top(node->table);
}

/* 【反查】由聚合节点反查其所属的聚合表。
 * 说明：底层 route_table_get_info 存的就是 agg_table，故强转即可。 */
static inline struct agg_table *agg_get_table(struct agg_node *node)
{
	return (struct agg_table *)route_table_get_info(node->table);
}

/* 【访问器】取节点前缀(只读，返回指向节点内 p 字段的指针)。 */
static inline const struct prefix *
agg_node_get_prefix(const struct agg_node *node)
{
	return &node->p;
}

/* 【访问器】取节点当前引用计数(route_node.lock)，可用于判断节点是否仍被引用。 */
static inline unsigned int agg_node_get_lock_count(const struct agg_node *node)
{
	return node->lock;
}

#ifdef _FRR_ATTRIBUTE_PRINTFRR
/* 【PRINTFRR 扩展】注册 "%pRN" 打印器，使 printfrr 系列能把 struct agg_node *
 * 打印成“前缀路由节点”形式(见 _FRR_ATTRIBUTE_PRINTFRR 编译开关)。 */
#pragma FRR printfrr_ext "%pRN"  (struct agg_node *)
#endif

#ifdef __cplusplus
}
#endif

#endif
