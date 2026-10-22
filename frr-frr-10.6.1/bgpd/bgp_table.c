// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP routing table
 * Copyright (C) 1998, 2001 Kunihiro Ishiguro
 */

/* ============================================================================
 * 【文件导读】bgp_table.c —— BGP RIB 的建表/落表/查表与节点生命周期
 * ============================================================================
 * 【本文件解决什么问题】bgp_table.h 定义了 BGP 路由表的数据模型（table / dest /
 * path_info 三层，详见其导读）。本文件是该模型的“实现层”：
 *   - 表的创建与销毁（bgp_table_init / lock / unlock / finish）；
 *   - dest 节点的加锁/解锁/销毁，以及随之而来的资源回收；
 *   - 前缀的子树查找；
 *   - 供 printf 使用的 %pBD 扩展。
 *
 * 【底层复用了通用前缀树】bgp_table 不自己实现树，而是包一个 lib 的 route_table
 * （前缀树），并通过 route_table_delegate_t(bgp_table_delegate) 定制节点行为：
 *   - create_node 用默认的 route_node_create；
 *   - destroy_node 用 bgp_node_destroy，负责回收 bgp_dest 及其附带资源。
 * 这样最长匹配、遍历等通用能力都直接复用 lib/table.c。
 *
 * 【★ 锁定语义（读本文件必须理解）】bgp_dest 的锁计数存在底层 route_node->lock。
 * bgp_dest_lock_node / unlock_node 是对 route_node 加锁/解锁的封装。当 unlock 使节点
 * 锁计数从 1 减到 0 时，才真正释放 bgp_dest 及其 addpath / mpath 等资源。bgp_table
 * 自身也有 lock，unlock 到 0 时释放整张表。用错会导致 use-after-free 或泄漏，这是
 * BGP 里最容易出内存问题的地方之一。
 *
 * 【bgp_dest->info 的多态】见 bgp_table.h；本文件多处对 info 赋 NULL 等，操作的都是
 * 同一条多态指针。
 *
 * 【阅读建议】bgp_table_lock/unlock（表级计数）-> bgp_table_init（建表）->
 *            bgp_dest_lock_node/unlock_node（节点级计数，核心）->
 *            bgp_node_destroy / bgp_delete_listnode / bgp_table_subtree_lookup ->
 *            末尾 %pBD 打印扩展。
 * ============================================================================
 */

#include <zebra.h>

#include "prefix.h"
#include "memory.h"
#include "sockunion.h"
#include "queue.h"
#include "filter.h"
#include "command.h"
#include "printfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgp_addpath.h"
#include "bgp_trace.h"
#include "bgp_mpath.h"

/* 【函数】bgp_table_lock —— 表引用计数 +1。 */
void bgp_table_lock(struct bgp_table *rt)
{
	rt->lock++;
}

/* 【函数】bgp_table_unlock —— 表引用计数 -1；减到 0 时真正销毁：结束 pi_hash、
 * 销毁底层 route_table、释放 bgp_table 本体。assert(rt->lock > 0) 保证不会多解。 */
void bgp_table_unlock(struct bgp_table *rt)
{
	assert(rt->lock > 0);
	rt->lock--;

	if (rt->lock != 0) {
		return;
	}

	/* Cleanup pi_hash in bgp_table */
	bgp_pi_hash_fini(&rt->pi_hash);

	route_table_finish(rt->route_table);
	rt->route_table = NULL;

	XFREE(MTYPE_BGP_TABLE, rt);
}

/* 【函数】bgp_table_finish —— 解一次表引用并把调用方指针置空（便于调用点复用）。
 * 入参是二级指针，函数把 *rt 置 NULL，避免悬垂。 */
void bgp_table_finish(struct bgp_table **rt)
{
	if (*rt != NULL) {
		bgp_table_unlock(*rt);
		*rt = NULL;
	}
}

/*
 * bgp_dest_lock_node
 */
/* 【函数】bgp_dest_lock_node —— 给 dest 对应的 route_node 加锁并返回同样的 dest。
 * frrtrace 是 FRR 的轻量跟踪钩子（未启用跟踪时展开为空）。 */
struct bgp_dest *bgp_dest_lock_node(struct bgp_dest *dest)
{
	frrtrace(1, frr_bgp, bgp_dest_lock, dest);
	struct route_node *rn = route_lock_node(bgp_dest_to_rnode(dest));

	return bgp_dest_from_rnode(rn);
}

/*
 * bgp_dest_get_prefix_str
 */
/* 【函数】bgp_dest_get_prefix_str —— 把 dest 的前缀格式化成字符串。
 * ★ 返回值指向函数内**静态缓冲**，非线程安全、下次调用会被覆盖；调用方若需长期
 *   保存必须自己拷贝。dest 无效时返回 NULL。 */
const char *bgp_dest_get_prefix_str(struct bgp_dest *dest)
{
	const struct prefix *p = NULL;
	static char str[PREFIX_STRLEN] = {0};

	p = bgp_dest_get_prefix(dest);
	if (p)
		return prefix2str(p, str, sizeof(str));

	return NULL;
}

/*
 * bgp_dest_unlock_node
 */
/* 【函数】bgp_dest_unlock_node —— 给 dest 的 route_node 解锁；当锁计数从 1 变为 0
 * （即此后无人引用）时做节点级清理：
 *   - bgp_delete_listnode：从 graceful-restart 的“选路延后”列表里移除；
 *   - 若表属于某个 bgp 实例，释放该节点的 addpath 数据；
 *   - 释放 mpath（等价多路径信息）；
 *   - 释放 bgp_dest 本体，并把 rn->info 置 NULL。
 * 最后调用 route_unlock_node 递减底层锁计数。返回置空后的 dest（可能为 NULL）。
 * ★ 资源释放都发生在“锁计数恰好为 1 时再解锁”这一拍，注意判断条件 if (rn->lock==1)。 */
inline struct bgp_dest *bgp_dest_unlock_node(struct bgp_dest *dest)
{
	frrtrace(1, frr_bgp, bgp_dest_unlock, dest);
	bgp_delete_listnode(dest);
	struct route_node *rn = bgp_dest_to_rnode(dest);

	/* 仅当本次解锁会让引用计数归零（==1）才做真正的释放。 */
	if (rn->lock == 1) {
		struct bgp_table *rt = bgp_dest_table(dest);


		if (rt->bgp) {
			bgp_addpath_free_node_data(&rt->bgp->tx_addpath,
						   &dest->tx_addpath, rt->afi,
						   rt->safi);
		}

		/* Free mpath if exists */
		if (dest->mpath)
			bgp_path_info_mpath_free(&dest->mpath);

		XFREE(MTYPE_BGP_NODE, dest);
		dest = NULL;
		rn->info = NULL;
	}
	route_unlock_node(rn);

	return dest;
}

/*
 * bgp_node_destroy
 */
/* 【函数】bgp_node_destroy —— route_table 的 destroy_node 回调：销毁一个 route_node
 * 时回收挂在它上面的 bgp_dest 及附带资源（addpath、mpath），并置 node->info=NULL；
 * 若是 flowspec 前缀，还释放 flowspec 前缀指针；最后释放 route_node 本体。
 * 与 bgp_dest_unlock_node 的区别：那是“单节点引用计数归零”触发，这是“整表销毁”时
 * 由 lib/table.c 逐节点回调触发。 */
static void bgp_node_destroy(route_table_delegate_t *delegate,
			     struct route_table *table, struct route_node *node)
{
	struct bgp_dest *dest;
	struct bgp_table *rt;
	dest = bgp_dest_from_rnode(node);
	rt = table->info;
	if (dest) {
		if (rt->bgp) {
			bgp_addpath_free_node_data(&rt->bgp->tx_addpath,
						   &dest->tx_addpath,
						   rt->afi, rt->safi);
		}

		/* Free mpath if exists */
		if (dest->mpath)
			bgp_path_info_mpath_free(&dest->mpath);

		XFREE(MTYPE_BGP_NODE, dest);
		node->info = NULL;
	}

	if (node->p.family == AF_FLOWSPEC)
		prefix_flowspec_ptr_free(&node->p);

	XFREE(MTYPE_ROUTE_NODE, node);
}

/*
 * Function vector to customize the behavior of the route table
 * library for BGP route tables.
 */
/* 【函数向量】bgp_table_delegate —— 交给 lib 的前缀树回调：create_node 用默认实现
 * route_node_create；destroy_node 用本文件的 bgp_node_destroy。 */
route_table_delegate_t bgp_table_delegate = { .create_node = route_node_create,
					      .destroy_node = bgp_node_destroy };

/*
 * bgp_table_init
 */
/* 【函数】bgp_table_init —— 创建一张 (bgp, afi, safi) 的 BGP 表：
 *   - 建底层 route_table（用上面的 delegate）；
 *   - route_table_set_info 把 bgp_table 反向挂回 route_table（供 bgp_dest_table 用）；
 *   - 记录所属 bgp 实例与 afi/safi（bgp 指针让 bgp_path_info 能回指到实例）；
 *   - 初始 lock=1，初始化 pi_hash。
 * 返回新表。注意初始就 lock 了一次，销毁时要对应 unlock。 */
struct bgp_table *bgp_table_init(struct bgp *bgp, afi_t afi, safi_t safi)
{
	struct bgp_table *rt;

	rt = XCALLOC(MTYPE_BGP_TABLE, sizeof(struct bgp_table));

	rt->route_table = route_table_init_with_delegate(&bgp_table_delegate);

	/*
	 * Set up back pointer to bgp_table.
	 */
	route_table_set_info(rt->route_table, rt);

	/*
	 * pointer to bgp instance allows working back from bgp_path_info to bgp
	 */
	rt->bgp = bgp;

	bgp_table_lock(rt);
	rt->afi = afi;
	rt->safi = safi;
	bgp_pi_hash_init(&rt->pi_hash);

	return rt;
}

/* Delete the route node from the selection deferral route list */
/* 【函数】bgp_delete_listnode —— 若该 dest 处于“选路延后”(BGP_NODE_SELECT_DEFER)
 * 状态，则把它从 graceful-restart 的待选路计数中减去并清除标志。
 * 作用对象是 bgp->gr_info[afi][safi].gr_deferred 计数。仅在节点即将被删除
 * （rn->lock == 1）时处理。 */
void bgp_delete_listnode(struct bgp_dest *dest)
{
	const struct route_node *rn = NULL;
	struct bgp_table *table = NULL;
	struct bgp *bgp = NULL;
	afi_t afi;
	safi_t safi;

	/* If the route to be deleted is selection pending, update the
	 * route node in gr_info
	 */
	if (CHECK_FLAG(dest->flags, BGP_NODE_SELECT_DEFER)) {
		table = bgp_dest_table(dest);

		if (table) {
			bgp = table->bgp;
			afi = table->afi;
			safi = table->safi;
		} else
			return;

		rn = bgp_dest_to_rnode(dest);

		if (bgp && rn && rn->lock == 1) {
			/* Delete the route from the selection pending list */
			bgp->gr_info[afi][safi].gr_deferred--;
			UNSET_FLAG(dest->flags, BGP_NODE_SELECT_DEFER);
		}
	}
}

/* 【函数】bgp_table_subtree_lookup —— 在树中查找前缀 p 的子树首节点（含 p 自身）。
 * 从树顶开始逐层向下：按 p 的位在 link 数组里选分支，直到找到覆盖 / 匹配 p 的节点。
 * 详见 bgp_table.h 声明处的英文说明。找到后对其加锁(bgp_dest_lock_node)再返回，
 * 因此调用方需要相应解锁。返回 NULL 表示该子树不存在。 */
struct bgp_dest *bgp_table_subtree_lookup(const struct bgp_table *table,
					  const struct prefix *p)
{
	struct bgp_dest *dest = bgp_dest_from_rnode(table->route_table->top);
	struct bgp_dest *matched = NULL;

	if (dest == NULL)
		return NULL;


	/* 从树顶向下逐层：用 p 在当前层长的下一位在 link 数组里选分支，
	 * 直到某节点的前缀长度已覆盖 p 或恰等于 p，判其是否匹配 p。 */
	while (dest) {
		const struct prefix *dest_p = bgp_dest_get_prefix(dest);
		struct route_node *node = dest->rn;

		if (dest_p->prefixlen >= p->prefixlen) {
			if (!prefix_match(p, dest_p))
				return NULL;

			matched = dest;
			break;
		}

		if (!prefix_match(dest_p, p))
			return NULL;

		if (dest_p->prefixlen == p->prefixlen) {
			matched = dest;
			break;
		}

		dest = bgp_dest_from_rnode(
			node->link[prefix_bit(&p->u.prefix, dest_p->prefixlen)]);
	}

	if (!matched)
		return NULL;

	bgp_dest_lock_node(matched);
	return matched;
}

/* 【打印扩展】%pBD —— 把 struct bgp_dest * 打印成其前缀字符串。
 * DEV_BUILD 下额外附上指针值，便于开发时辨认同一节点的不同实例。
 * ptr 为 NULL 时打印 "(null)"。 */
printfrr_ext_autoreg_p("BD", printfrr_bd);
static ssize_t printfrr_bd(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct bgp_dest *dest = ptr;
	const struct prefix *p = bgp_dest_get_prefix(dest);
	char cbuf[PREFIX_STRLEN];

	if (!dest)
		return bputs(buf, "(null)");

#if !defined(DEV_BUILD)
	/* need to get the real length even if buffer too small */
	prefix2str(p, cbuf, sizeof(cbuf));
	return bputs(buf, cbuf);
#else
	return bprintfrr(buf, "%s(%p)", prefix2str(p, cbuf, sizeof(cbuf)), dest);
#endif
}
