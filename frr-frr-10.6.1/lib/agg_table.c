// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Aggregate Route
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *               Donald Sharp
 */
#include "zebra.h"

/* ============================================================================
 * 【文件导读】agg_table.c —— 让路由表节点“带上聚合信息”的定制表
 * ============================================================================
 * 【背景：FRR 的路由表是通用的】lib/table.c 里的 route_table 是一棵通用前缀树，
 * 它只关心“键是前缀、值是 void*”。但有些场景(如 BGP 的聚合路由)需要**把额外
 * 字段塞进节点本身**——每次插入都单独挂一份数据既慢又费内存。
 *
 * 【解法：委托(delegate)模式】route_table 支持一个 route_table_delegate_t：
 * 建节点/销毁节点时不自己 malloc，而是回调 delegate 里指定的函数。本文件就用
 * 这个钩子，把节点换成更大的 `struct agg_node`(内部头是 route_node，后面跟着
 * 聚合字段)。于是对上层而言它仍是一个 route_table，但节点里已经能存聚合数据。
 *
 * 【两个方向转换宏是理解本文件的关键】
 *   agg_node_to_rnode(anode) : 取 agg_node 内嵌的 route_node(向基类转型)；
 *   agg_node_from_rnode(node): 由 route_node 反推回 agg_node。
 * 它们通常基于“route_node 是结构体首成员”这一布局约定，所以反向转换才成立。
 * 参见 agg_table.h 的宏定义——务必注意这两者不是同一种指针，用错就是经典的
 * 基类/派生类混用错误。
 *
 * 【阅读顺序】delegate 的两个回调(create/destroy) → 委托表 agg_table_delegate
 * → agg_table_init()(建表并把 delegate 装上去)。
 * ============================================================================
 */
#include "agg_table.h"


/* 【delegate 回调】创建节点。由 route_table 在需要新节点时调用。
 *   delegate: 发起请求的委托表(本函数用不到，仅为接口签名一致)。
 *   table   : 所属路由表(同样未使用)。
 *   返回    : 转型成 route_node* 的 agg_node 指针，交给通用表使用。
 * 【要点】这里**只分配 agg_node**，绝不单独分配 route_node——两者是同一块内存，
 * 靠 agg_node_to_rnode() 取基类部分。若在此处也 malloc 一个 route_node，就会
 * 出现析构时释放错指针的严重问题。
 * 【易踩的坑】MTYPE_TMP 是临时内存类型的记账标签，说明这块内存不计入长期统计；
 * 真正记账用的是 XCALLOC/XFREE 宏。返回值必须做转型，通用表只认 route_node*。 */
static struct route_node *agg_node_create(route_table_delegate_t *delegate,
					  struct route_table *table)
{
	struct agg_node *node;

	node = XCALLOC(MTYPE_TMP, sizeof(struct agg_node));

	return agg_node_to_rnode(node);
}

/* 【delegate 回调】销毁节点。由 route_table 在删除节点时调用(承接上面的创建)。
 *   node: 通用表持有的 route_node 指针。
 * 【要点】必须先用 agg_node_from_rnode() 反推回 agg_node 再 XFREE——直接
 * XFREE(node) 在两者布局不同的实现下会释放错误地址。与 create 严格配对：
 * 一个 agg_node 只释放一次。
 * 【易踩的坑】本函数**不清理**聚合字段里可能指向的其它资源(如链表)；若
 * agg_node 未来新增了需要深度释放的成员，必须在这里补上，否则泄漏。 */
static void agg_node_destroy(route_table_delegate_t *delegate,
			     struct route_table *table, struct route_node *node)

{
	struct agg_node *anode = agg_node_from_rnode(node);

	XFREE(MTYPE_TMP, anode);
}

/* 【委托表】把建/销毁节点这两件事交给上面两个函数。**静态且无状态**，所以所有
 * agg_table 实例共用这一份即可，无需每个表各存一份。通用 table.c 会在建表时
 * 记下这个指针，之后每次增删节点都回调它。 */
static route_table_delegate_t agg_table_delegate = {
	.create_node = agg_node_create,
	.destroy_node = agg_node_destroy,
};

/* 【函数】创建一个“带聚合信息”的路由表。
 *   返回: 新分配的 struct agg_table*，调用方负责最终销毁。
 * 【三步做了什么】① XCALLOC 表壳；② 用**带 delegate 的**建表函数建立内层通用
 *   路由表(这一步决定了节点的实际类型是 agg_node)；③ route_table_set_info()
 *   把表壳反向指回自己，方便从内层 route_table 找回外层 agg_table。
 * 【易踩的坑】必须用 route_table_init_with_delegate()，不能用普通的
 * route_table_init()——后者会退回默认节点类型，聚合字段就没有落脚处了(而且
 * 后面的转型会越界)。这是本文件最容易被改错的一行。 */
struct agg_table *agg_table_init(void)
{
	struct agg_table *at;

	at = XCALLOC(MTYPE_TMP, sizeof(struct agg_table));

	at->route_table = route_table_init_with_delegate(&agg_table_delegate);
	route_table_set_info(at->route_table, at);

	return at;
}
