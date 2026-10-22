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
 * 文件导读：lib/srcdest_table.c —— 二维（源 + 目的）路由表实现
 * ============================================================================
 * 本文件实现 srcdest_table.h 声明的接口，把“目的前缀 -> 源前缀 -> 信息”的
 * 两级结构落成代码。关键点：
 *
 *   - 目的层节点用 struct srcdest_rnode 承载（在普通 route_node 字段后追加
 *     src_table 指针）。它必须与 route_node 布局兼容，因此 ROUTE_NODE_FIELDS
 *     必须放在结构体最前面，这样才能与 route_node 相互强制转换。
 *   - 源层节点就是普通 route_node，但通过独立 delegate 定制创建 / 销毁，
 *     以便在源层表清空时释放 src_table 并归还引用。
 *   - 引用计数是本文件最容易出错之处：srcnode_get 与 srcnode_lookup 等函数
 *     内部会做 route_lock_node 与 route_unlock_node 配平，函数内的原有英文
 *     注释详细解释了为何这样配平。
 *
 * 与其他模块的关系：
 *   - 依赖 table.h 的 route_table / route_node / delegate 与引用计数；
 *   - 通过 printfrr_ext_autoreg_p 注册 "RN" 打印扩展，使 %pRN 可直接打印节点。
 *
 * 建议阅读顺序：
 *   1) srcdest_rnode 结构 与 from / to 转换辅助；
 *   2) 目的层 create / destroy 与 dstnode delegate；
 *   3) 源层 create / destroy / get / lookup 与 srcnode delegate；
 *   4) 导出的 table_init、route_next、rnode_get 与 lookup、prefixes、各转串函数。
 */
#include <zebra.h>

#include "srcdest_table.h"

#include "memory.h"
#include "prefix.h"
#include "table.h"
#include "printfrr.h"

/*
 * 注册本文件使用的内存类型 ROUTE_SRC_NODE（源层 route_node 的分配 / 释放），
 * 便于内存统计与泄漏排查。目的层节点复用 MTYPE_ROUTE_NODE。
 */
DEFINE_MTYPE_STATIC(LIB, ROUTE_SRC_NODE, "Route source node");

/* ----- functions to manage rnodes _with_ srcdest table ----- */
/*
 * struct srcdest_rnode：目的层节点，在普通 route_node 之上扩展一个 src_table。
 * ROUTE_NODE_FIELDS 必须位于最前，保证可安全地与 struct route_node 互转；
 * src_table 是由本目的前缀派生出的源层子表（可能为 NULL）。
 */
struct srcdest_rnode {
	/* must be first in structure for casting to/from route_node */
	ROUTE_NODE_FIELDS;

	struct route_table *src_table;
};

/*
 * srcdest_rnode_from_rnode：把目的层 route_node 指针转成 srcdest_rnode 指针。
 * 参数：rn 必须确为目的层节点（内部 assert 校验）。
 * 返回同一地址的 srcdest_rnode 视图（布局兼容，仅类型转换）。
 */
static struct srcdest_rnode *srcdest_rnode_from_rnode(struct route_node *rn)
{
	assert(rnode_is_dstnode(rn));
	return (struct srcdest_rnode *)rn;
}

/*
 * srcdest_rnode_to_rnode：srcdest_rnode 指针转回通用 route_node 指针。
 */
static struct route_node *srcdest_rnode_to_rnode(struct srcdest_rnode *srn)
{
	return (struct route_node *)srn;
}

/*
 * srcdest_rnode_create：目的层节点的分配回调（delegate->create_node）。
 * 分配零初始化的 srcdest_rnode（用 MTYPE_ROUTE_NODE）并返回其 route_node 视图。
 * 参数 delegate / table 由 table 框架传入，此处不使用。
 */
static struct route_node *srcdest_rnode_create(route_table_delegate_t *delegate,
					       struct route_table *table)
{
	struct srcdest_rnode *srn;
	srn = XCALLOC(MTYPE_ROUTE_NODE, sizeof(struct srcdest_rnode));
	return srcdest_rnode_to_rnode(srn);
}

/*
 * srcdest_rnode_destroy：目的层节点的销毁回调（delegate->destroy_node）。
 * 参数：rn 待销毁的目的层节点。
 * 副作用：先取出并清空 src_table 再 route_table_finish，避免释放源层最后
 * 一个节点时再次触发对同一张 src_table 的 finish（见函数内原有注释），
 * 最后释放节点本体。
 */
static void srcdest_rnode_destroy(route_table_delegate_t *delegate,
				  struct route_table *table,
				  struct route_node *rn)
{
	struct srcdest_rnode *srn = srcdest_rnode_from_rnode(rn);
	struct route_table *src_table;

	/* Clear route node's src_table here already, otherwise the
	 * deletion of the last node in the src_table will trigger
	 * another call to route_table_finish for the src_table.
	 *
	 * (Compare with srcdest_srcnode_destroy)
	 */
	src_table = srn->src_table;
	srn->src_table = NULL;
	route_table_finish(src_table);
	XFREE(MTYPE_ROUTE_NODE, rn);
}

/*
 * 目的层 delegate：把节点创建 / 销毁指向本文件的 srcdest_rnode_* 实现。
 * rnode_is_dstnode 通过与本结构体地址比较来判定节点是否属于目的层。
 */
route_table_delegate_t _srcdest_dstnode_delegate = {
	.create_node = srcdest_rnode_create,
	.destroy_node = srcdest_rnode_destroy};

/* ----- functions to manage rnodes _in_ srcdest table ----- */

/* node creation / deletion for srcdest source prefix nodes.
 * the route_node isn't actually different from the normal route_node,
 * but the cleanup is special to free the table (and possibly the
 * destination prefix's route_node) */

static struct route_node *
/*
 * srcdest_srcnode_create：源层节点的分配回调。
 * 分配零初始化的普通 route_node（用 MTYPE_ROUTE_SRC_NODE）并返回。
 */
srcdest_srcnode_create(route_table_delegate_t *delegate,
		       struct route_table *table)
{
	return XCALLOC(MTYPE_ROUTE_SRC_NODE, sizeof(struct route_node));
}

/*
 * srcdest_srcnode_destroy：源层节点的销毁回调。
 * 参数：table 为源层表（其 info 指向父目的节点 srn）；rn 待销毁源节点。
 * 副作用：释放源节点后，若源层表已空则 finish 该 src_table、清空指针，
 * 并 unlock 父目的节点（归还 srcdest_srcnode_get 时持有的引用）。
 * 注意：在 destroy_node 内删除整张表仅当路由表计数为 0 时才允许（见原有注释）。
 */
static void srcdest_srcnode_destroy(route_table_delegate_t *delegate,
				    struct route_table *table,
				    struct route_node *rn)
{
	struct srcdest_rnode *srn;

	XFREE(MTYPE_ROUTE_SRC_NODE, rn);

	srn = route_table_get_info(table);
	if (srn->src_table && route_table_count(srn->src_table) == 0) {
		/* deleting the route_table from inside destroy_node is ONLY
		 * permitted IF table->count is 0!  see lib/table.c
		 * route_node_delete()
		 * for details */
		route_table_finish(srn->src_table);
		srn->src_table = NULL;

		/* drop the ref we're holding in srcdest_node_get().  there
		 * might be
		 * non-srcdest routes, so the route_node may still exist.
		 * hence, it's
		 * important to clear src_table above. */
		route_unlock_node(srcdest_rnode_to_rnode(srn));
	}
}

/*
 * 源层 delegate：把节点创建 / 销毁指向本文件的 srcdest_srcnode_* 实现。
 * rnode_is_srcnode 通过与本结构体地址比较来判定节点是否属于源层。
 */
route_table_delegate_t _srcdest_srcnode_delegate = {
	.create_node = srcdest_srcnode_create,
	.destroy_node = srcdest_srcnode_destroy};

/* NB: read comments in code for refcounting before using! */
/*
 * srcdest_srcnode_get：在给定目的节点下取得（或创建）源层节点。
 * 参数：rn 目的层节点；src_p 源前缀（NULL 或前缀长为 0 时直接返回 rn，
 *       视为非 srcdest 路由）。
 * 返回源层节点（持有一份引用）或 rn 本身。
 * 引用计数要点（务必结合函数内原有英文注释阅读）：
 *   - 首次创建 src_table 时不 unlock 原 rn，把引用“转给” src_table 持有；
 *   - src_table 已存在时先 unlock rn（因为 src_table 另有引用，不会释放 rn），
 *     使返回后仅剩返回值这一份引用，行为贴近非 srcdest 情形。
 */
static struct route_node *srcdest_srcnode_get(struct route_node *rn,
					      const struct prefix_ipv6 *src_p)
{
	struct srcdest_rnode *srn;

	if (!src_p || src_p->prefixlen == 0)
		return rn;

	srn = srcdest_rnode_from_rnode(rn);
	if (!srn->src_table) {
		/* this won't use srcdest_rnode, we're already on the source
		 * here */
		srn->src_table = route_table_init_with_delegate(
			&_srcdest_srcnode_delegate);
		route_table_set_info(srn->src_table, srn);

		/* there is no route_unlock_node on the original rn here.
		 * The reference is kept for the src_table. */
	} else {
		/* only keep 1 reference for the src_table, makes the
		 * refcounting
		 * more similar to the non-srcdest case.  Either way after
		 * return from
		 * function, the only reference held is the one on the return
		 * value.
		 *
		 * We can safely drop our reference here because src_table is
		 * holding
		 * another reference, so this won't free rn */
		route_unlock_node(rn);
	}

	return route_node_get(srn->src_table, (const struct prefix *)src_p);
}

/*
 * srcdest_srcnode_lookup：在给定目的节点下查找源层节点（不创建）。
 * 参数：rn 目的层节点（可为 NULL）；src_p 源前缀。
 * 返回匹配节点（持有一份引用），无匹配返回 NULL。
 * 引用计数要点：rn 来自 lookup 已加锁，而本函数后续不会返回 rn，
 * 故先 route_unlock_node(rn) 归还；src_table 不存在则返回 NULL。
 */
static struct route_node *srcdest_srcnode_lookup(
	struct route_node *rn,
	const struct prefix_ipv6 *src_p)
{
	struct srcdest_rnode *srn;

	if (!rn || !src_p || src_p->prefixlen == 0)
		return rn;

	/* We got this rn from a lookup, so its refcnt was incremented. As we
	 * won't
	 * return return rn from any point beyond here, we should decrement its
	 * refcnt.
	 */
	route_unlock_node(rn);

	srn = srcdest_rnode_from_rnode(rn);
	if (!srn->src_table)
		return NULL;

	return route_node_lookup(srn->src_table, (const struct prefix *)src_p);
}

/* ----- exported functions ----- */

/*
 * srcdest_table_init：创建 srcdest 表（目的层），使用 dstnode delegate。
 * 返回新表指针。
 */
struct route_table *srcdest_table_init(void)
{
	return route_table_init_with_delegate(&_srcdest_dstnode_delegate);
}

/*
 * srcdest_route_next：srcdest 感知的遍历函数。
 * 参数：rn 当前节点（可能为目的层或源层）。
 * 返回下一个节点（持有一份引用），遍历结束返回 NULL。
 * 逻辑：非 srcdest 节点退化为 route_next；目的层节点若有源层子表则先进入
 * 子表顶端；源层节点则在子表内继续，子表遍历完回到父表继续。
 * 注意：各分支都做了 lock / unlock 配平，调用者仍需按约定 unlock 返回节点。
 */
struct route_node *srcdest_route_next(struct route_node *rn)
{
	struct route_node *next, *parent;

	/* For a non src-dest node, just return route_next */
	if (!(rnode_is_dstnode(rn) || rnode_is_srcnode(rn)))
		return route_next(rn);

	if (rnode_is_dstnode(rn)) {
		/* This means the route_node is part of the top hierarchy
		 * and refers to a destination prefix. */
		struct srcdest_rnode *srn = srcdest_rnode_from_rnode(rn);

		if (srn->src_table)
			next = route_top(srn->src_table);
		else
			next = NULL;

		if (next) {
			/* There is a source prefix. Return the node for it */
			route_unlock_node(rn);
			return next;
		} else {
			/* There is no source prefix, just continue as usual */
			return route_next(rn);
		}
	}

	/* This part handles the case of iterating source nodes. */
	parent = route_lock_node(route_table_get_info(rn->table));
	next = route_next(rn);

	if (next) {
		/* There is another source node, continue in the source table */
		route_unlock_node(parent);
		return next;
	} else {
		/* The source table is complete, continue in the parent table */
		return route_next(parent);
	}
}

/*
 * srcdest_rnode_get：对外接口，取得（或创建）匹配目的 + 源的节点。
 * 参数：table srcdest 表；dst_pu 目的前缀；src_p 源前缀。
 * 返回节点并持有一份引用。实现：先按目的 route_node_get，再 srcdest_srcnode_get。
 */
struct route_node *srcdest_rnode_get(struct route_table *table,
				     union prefixconstptr dst_pu,
				     const struct prefix_ipv6 *src_p)
{
	const struct prefix_ipv6 *dst_p = dst_pu.p6;
	struct route_node *rn;

	rn = route_node_get(table, (const struct prefix *)dst_p);
	return srcdest_srcnode_get(rn, src_p);
}

/*
 * srcdest_rnode_lookup：对外接口，查找匹配目的 + 源的节点（不创建）。
 * 参数：table srcdest 表；dst_pu 目的前缀；src_p 源前缀。
 * 返回节点并持有一份引用；无匹配返回 NULL。
 * 注意：若目的节点存在但其 info 为空，也返回 NULL，以匹配 route_node_lookup
 * 对“空节点”的处理（见函数内原有注释）。
 */
struct route_node *srcdest_rnode_lookup(struct route_table *table,
					union prefixconstptr dst_pu,
					const struct prefix_ipv6 *src_p)
{
	const struct prefix_ipv6 *dst_p = dst_pu.p6;
	struct route_node *rn;
	struct route_node *srn;

	rn = route_node_lookup_maynull(table, (const struct prefix *)dst_p);
	srn = srcdest_srcnode_lookup(rn, src_p);

	if (rn != NULL && rn == srn && !rn->info) {
		/* Match the behavior of route_node_lookup and don't return an
		 * empty route-node for a dest-route */
		route_unlock_node(rn);
		return NULL;
	}
	return srn;
}

/*
 * srcdest_rnode_prefixes：从节点反推目的前缀与源前缀。
 * 参数：rn 节点；p 输出目的前缀（可 NULL）；src_p 输出源前缀（可 NULL）。
 * 副作用：源层节点时 p 取其父目的节点前缀、src_p 取自身；目的层节点时 p 取
 * 自身、src_p 置 NULL。
 */
void srcdest_rnode_prefixes(const struct route_node *rn,
			    const struct prefix **p,
			    const struct prefix **src_p)
{
	if (rnode_is_srcnode(rn)) {
		struct route_node *dst_rn = route_table_get_info(rn->table);
		if (p)
			*p = &dst_rn->p;
		if (src_p)
			*src_p = &rn->p;
	} else {
		if (p)
			*p = &rn->p;
		if (src_p)
			*src_p = NULL;
	}
}

/*
 * srcdest2str：把 “目的前缀 from 源前缀” 格式化进缓冲区。
 * 参数：dst_p 目的前缀；src_p 源前缀（NULL 或前缀长为 0 时不输出 from 部分）；
 *       str / size 输出缓冲区。
 * 返回 str。
 */
const char *srcdest2str(const struct prefix *dst_p,
			const struct prefix_ipv6 *src_p,
			char *str, int size)
{
	char dst_buf[PREFIX_STRLEN], src_buf[PREFIX_STRLEN];

	snprintf(str, size, "%s%s%s",
		 prefix2str(dst_p, dst_buf, sizeof(dst_buf)),
		 (src_p && src_p->prefixlen) ? " from " : "",
		 (src_p && src_p->prefixlen)
			 ? prefix2str(src_p, src_buf, sizeof(src_buf))
			 : "");
	return str;
}

/*
 * srcdest_rnode2str：先取节点对应前缀，再调用 srcdest2str 格式化。
 * 参数：rn 节点；str / size 输出缓冲区。
 * 返回 str。
 */
const char *srcdest_rnode2str(const struct route_node *rn, char *str, int size)
{
	const struct prefix *dst_p, *src_p;

	srcdest_rnode_prefixes(rn, &dst_p, &src_p);
	return srcdest2str(dst_p, (const struct prefix_ipv6 *)src_p, str, size);
}

/*
 * 向 printfrr 框架注册 "RN" 扩展，使格式串 %pRN 可打印 route_node。
 * 注册项指向下方的 printfrr_rn 回调。
 */
printfrr_ext_autoreg_p("RN", printfrr_rn);
/*
 * printfrr_rn：printfrr 的 %pRN 打印回调。
 * 参数：buf 输出缓冲；ea 打印参数（此处未使用）；ptr 指向 route_node。
 * 返回写入字节数。ptr 为 NULL 时输出 "(null)"；否则用 srcdest2str 格式化
 * 节点的“目的 from 源”并写入。
 */
static ssize_t printfrr_rn(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct route_node *rn = ptr;
	const struct prefix *dst_p, *src_p;
	char cbuf[PREFIX_STRLEN * 2 + 6];

	if (!rn)
		return bputs(buf, "(null)");

	srcdest_rnode_prefixes(rn, &dst_p, &src_p);
	srcdest2str(dst_p, (const struct prefix_ipv6 *)src_p,
		    cbuf, sizeof(cbuf));
	return bputs(buf, cbuf);
}
