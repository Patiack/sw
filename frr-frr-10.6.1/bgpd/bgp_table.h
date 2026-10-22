// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP routing table
 * Copyright (C) 1998, 2001 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_table.h —— BGP RIB 的数据模型（表 / 节点 / 路径）
 * ============================================================================
 * 这是继 bgpd.h 之后第二重要的数据结构头文件。它定义 BGP 路由表的三层：
 *
 *   struct bgp_table       —— 一张 (bgp实例, afi, safi) 维度的路由表，
 *                             内部包一个通用前缀树 route_table。
 *   struct bgp_dest        —— 树上的一个“前缀节点”（= 一个目的前缀），
 *                             对应 route_node->info，是 RIB 的核心单元。
 *   struct bgp_path_info   —— 某个前缀下的“一条路径/一条属性记录”，
 *                             定义在本文件之外的 bgp_route.h，被 bgp_dest->info
 *                             指向；一个前缀有多条 peer 发来的路径就用它串成链表。
 *
 * 【最需要理解的一点：bgp_dest->info 的多态】
 * bgp_dest 只是一个“挂在 route_node 上的外壳”，它的 void *info 指向什么，取决于
 * 这张表是干什么用的：
 *   rib[afi][safi]           → struct bgp_path_info   （真正的路由）
 *   aggregate[afi][safi]     → struct bgp_aggregate  （聚合配置）
 *   static_routes[afi][safi] → struct bgp_static     （network/静态配置）
 *   connected_table / nexthop 表 → struct bgp_nexthop_cache / bgp_connected_ref
 * 所以本文件后半部分有一大堆 bgp_dest_get_xxx_info()/set_xxx_info()，它们本质
 * 都是对同一个 info 指针的“带类型解释”。读代码时看到 info 被 set 成别的类型，
 * 先确认当前在操作哪张表。同理 bgp_dest_to_rnode()/bgp_dest_from_rnode() 是
 * bgp_dest 与底层 route_node 之间的互转。
 *
 * 【锁与版本】bgp_dest 的锁定计数实际存放在底层 route_node->lock；bgp_table 自己
 * 也有 lock。version 用于判断“表是否变化过”（如向 zebra 增量下发的比较依据）。
 * 建议阅读顺序：struct bgp_table → enum bgp_path_selection_reason →
 * struct bgp_dest → 末尾的 inline 工具函数。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_TABLE_H
#define _QUAGGA_BGP_TABLE_H

#include "mpls.h"
#include "table.h"
#include "queue.h"
#include "linklist.h"
#include "typesafe.h"
#include "bgpd.h"
#include "bgp_advertise.h"
#include "bgp_attr_srv6.h"

/* Typesafe hash for bgp_path_info lookup */
PREDECL_HASH(bgp_pi_hash);

/* 一张 BGP 路由表。bgp->rib[afi][safi] 等多个成员都是它的实例。
 * 注意它不是自己去实现前缀树，而是包了一个通用 route_table（lib/table.c），
 * 这样前缀匹配/最长匹配/迭代等通用能力可以复用。 */
struct bgp_table {
	/* table belongs to this instance */
	struct bgp *bgp;

	/* afi/safi of this table */
	afi_t afi;
	safi_t safi;

	int lock;

	/* Hash for bgp_path_info lookups across all prefixes in this table */
	struct bgp_pi_hash_head pi_hash;

	/* soft_reconfig_table in progress */
	bool soft_reconfig_init;
	struct event *soft_reconfig_thread;

	/* list of peers on which soft_reconfig_table has to run */
	struct list *soft_reconfig_peers;

	struct route_table *route_table;
	uint64_t version;
};

/* 最佳路径选择的“裁决原因”：bestpath 比较器在决出胜者时，把“在哪一步胜出”记在
 * 这里。顺序大体上就是 BGP 选路的比较顺序（先比较的排前面）：
 *   权重 → 本地优先级 → 本地起源 → AS_PATH 长度 → Origin → MED → eBGP/iBGP →
 *   IGP metric → 最早 → router-id → cluster-list。
 * 前面的 evpn_* 与 admin_distance 属于 EVPN/多协议的特殊前置规则。
 * 用途：排障时 show 出来的“为何选它”就是读这个字段（bgp_dest->reason）。 */
enum bgp_path_selection_reason {
	bgp_path_selection_none,
	bgp_path_selection_first,
	bgp_path_selection_evpn_sticky_mac,
	bgp_path_selection_evpn_seq,
	bgp_path_selection_evpn_local_path,
	bgp_path_selection_evpn_non_proxy,
	bgp_path_selection_evpn_lower_ip,
	bgp_path_selection_admin_distance,
	bgp_path_selection_weight,
	bgp_path_selection_local_pref,
	bgp_path_selection_accept_own,
	bgp_path_selection_local_route,
	bgp_path_selection_aigp,
	bgp_path_selection_confed_as_path,
	bgp_path_selection_as_path,
	bgp_path_selection_origin,
	bgp_path_selection_med,
	bgp_path_selection_peer,
	bgp_path_selection_confed,
	bgp_path_selection_igp_metric,
	bgp_path_selection_older,
	bgp_path_selection_router_id,
	bgp_path_selection_cluster_length,
	bgp_path_selection_stale,
	bgp_path_selection_local_configured,
	bgp_path_selection_neighbor_ip,
	bgp_path_selection_default,
};

/* ===========================================================================
 * struct bgp_dest —— RIB 里的一个“前缀节点”
 * ---------------------------------------------------------------------------
 * 它挂在通用前缀树的 route_node->info 上，是 BGP 处理一个前缀时操作的核心对象。
 * 关键成员：
 *   rn      : 反向指回底层 route_node（前缀本身存在 rn->p）。
 *   info    : 【多态】指向不同类型的上层数据（见文件头导读）；在 RIB 里它就是
 *             struct bgp_path_info 链表头（该前缀所有路径/属性）。
 *   adj_out : 本前缀向各 peer 的“出向通告状态”红黑树（bgp_adj_out）——决定
 *             是否需要给某 peer 发/撤该前缀，是 update-group 增量发包的依据。
 *   adj_in  : 本前缀从各 peer “收到的入向记录”（bgp_adj_in），用于 soft-reconfig
 *             与 Route Refresh 时无需重收报文即可重算。
 *   pdest   : 父前缀节点（用于 aggregate/summary 相关处理）。
 *   mpath   : 等价多路径信息（bgp_path_info_mpath），bestpath 选出后计算。
 *   local_label : 本前缀分配到的本地 MPLS 标签（VPN/标签路由用）。
 *   version : 本节点的版本号，随内容变化自增，用于增量同步判断。
 *   flags   : BGP_NODE_* 状态位，例如 PROCESS_SCHEDULED（已排入 bestpath 队列）、
 *             FIB_INSTALL_PENDING/INSTALLED（与 zebra 的下发状态）、
 *             LABEL_*（MPLS 标签分配/请求状态）等。
 * ===========================================================================
 */
struct bgp_dest {
	struct route_node *rn;

	void *info;

	struct bgp_adj_out_rb adj_out;

	struct bgp_adj_in *adj_in;

	struct bgp_dest *pdest;

	STAILQ_ENTRY(bgp_dest) pq;

	struct zebra_announce_item zai;
	struct bgp_path_info *za_bgp_pi;
	struct bgpevpn *za_vpn;
	bool za_is_sync;

	uint64_t version;

	mpls_label_t local_label;

	struct bgp_attr_srv6_l3service *srv6_unicast;

	uint16_t flags;
#define BGP_NODE_PROCESS_SCHEDULED	(1 << 0)
#define BGP_NODE_USER_CLEAR             (1 << 1)
#define BGP_NODE_LABEL_CHANGED          (1 << 2)
#define BGP_NODE_REGISTERED_FOR_LABEL   (1 << 3)
#define BGP_NODE_SELECT_DEFER           (1 << 4)
#define BGP_NODE_FIB_INSTALL_PENDING    (1 << 5)
#define BGP_NODE_FIB_INSTALLED          (1 << 6)
#define BGP_NODE_LABEL_REQUESTED        (1 << 7)
#define BGP_NODE_SOFT_RECONFIG (1 << 8)
#define BGP_NODE_PROCESS_CLEAR (1 << 9)
#define BGP_NODE_SCHEDULE_FOR_INSTALL	(1 << 10)
#define BGP_NODE_SCHEDULE_FOR_DELETE	(1 << 11)
#define BGP_NODE_NHT_RESOLVED_NODE	(1 << 12)

	struct bgp_addpath_node_data tx_addpath;

	enum bgp_path_selection_reason reason;

	/* Multipath information */
	struct bgp_path_info_mpath *mpath;
};

DECLARE_LIST(zebra_announce, struct bgp_dest, zai);

extern void bgp_delete_listnode(struct bgp_dest *dest);
/*
 * bgp_table_iter_t
 *
 * Structure that holds state for iterating over a bgp table.
 */
typedef struct bgp_table_iter_t_ {
	struct bgp_table *table;
	route_table_iter_t rt_iter;
} bgp_table_iter_t;

extern struct bgp_table *bgp_table_init(struct bgp *bgp, afi_t afi, safi_t safi);
extern void bgp_table_lock(struct bgp_table *rt);
extern void bgp_table_unlock(struct bgp_table *rt);
extern void bgp_table_finish(struct bgp_table **table);
extern struct bgp_dest *bgp_dest_unlock_node(struct bgp_dest *dest);
extern struct bgp_dest *bgp_dest_lock_node(struct bgp_dest *dest);
extern const char *bgp_dest_get_prefix_str(struct bgp_dest *dest);


/*
 * bgp_dest_from_rnode
 *
 * Returns the bgp_dest structure corresponding to a route_node.
 */
static inline struct bgp_dest *bgp_dest_from_rnode(struct route_node *rnode)
{
	return (rnode && rnode->info) ? (struct bgp_dest *)rnode->info : NULL;
}

/*
 * bgp_dest_to_rnode
 *
 * Returns the route_node structure corresponding to a bgp_dest.
 */
static inline struct route_node *bgp_dest_to_rnode(const struct bgp_dest *dest)
{
	return dest ? dest->rn : NULL;
}

/*
 * bgp_dest_table
 *
 * Returns the bgp_table that the given dest is in.
 */
static inline struct bgp_table *bgp_dest_table(struct bgp_dest *dest)
{
	return route_table_get_info(bgp_dest_to_rnode(dest)->table);
}

/*
 * bgp_dest_parent_nolock
 *
 * Gets the parent dest of the given node without locking it.
 */
static inline struct bgp_dest *bgp_dest_parent_nolock(struct bgp_dest *dest)
{
	struct route_node *rn = bgp_dest_to_rnode(dest)->parent;

	while (rn && !rn->info)
		rn = rn->parent;

	return bgp_dest_from_rnode(rn);
}

/*
 * bgp_table_top_nolock
 *
 * Gets the top dest in the table without locking it.
 *
 * @see bgp_table_top
 */
static inline struct bgp_dest *
bgp_table_top_nolock(const struct bgp_table *const table)
{
	struct route_node *top;
	struct route_node *rn = top = table->route_table->top;

	while (rn && !rn->info) {
		if (rn == top)
			route_lock_node(rn);
		rn = route_next(rn);
	}
	if (rn && rn != top)
		route_unlock_node(rn);
	return rn ? rn->info : NULL;
}

/*
 * bgp_table_top
 */
static inline struct bgp_dest *
bgp_table_top(const struct bgp_table *const table)
{
	struct route_node *rn = route_top(table->route_table);

	while (rn && !rn->info)
		rn = route_next(rn);
	return rn ? rn->info : NULL;
}

/*
 * bgp_route_next
 */
static inline struct bgp_dest *bgp_route_next(struct bgp_dest *dest)
{
	struct route_node *rn = route_next(bgp_dest_to_rnode(dest));

	while (rn && !rn->info)
		rn = route_next(rn);
	return bgp_dest_from_rnode(rn);
}

/*
 * bgp_route_next_until
 */
static inline struct bgp_dest *bgp_route_next_until(struct bgp_dest *dest,
						    struct bgp_dest *limit)
{
	struct route_node *rnode;

	rnode = route_next_until(bgp_dest_to_rnode(dest),
			bgp_dest_to_rnode(limit));

	while (rnode && !rnode->info)
		rnode = route_next_until(rnode, bgp_dest_to_rnode(limit));

	return bgp_dest_from_rnode(rnode);
}

/*
 * bgp_node_get
 */
/* bgp_node_get：取（不存在则创建）前缀 p 对应的 dest。
 * 注意 info 只在首次创建时分配：若该 route_node 已有 info，直接复用。
 * 与其配套的是 bgp_node_lookup()（只查不建）——读代码时务必分清“查/建/匹配”：
 *   bgp_node_get     = 精确前缀，无则新建（会改表）
 *   bgp_node_lookup  = 精确前缀，仅查找
 *   bgp_node_match   = 最长前缀匹配（不新建） */
static inline struct bgp_dest *bgp_node_get(struct bgp_table *const table,
					    const struct prefix *p)
{
	struct route_node *rn = route_node_get(table->route_table, p);

	if (!rn->info) {
		struct bgp_dest *dest = XCALLOC(MTYPE_BGP_NODE,
						sizeof(struct bgp_dest));

		RB_INIT(bgp_adj_out_rb, &dest->adj_out);
		rn->info = dest;
		dest->rn = rn;
	}
	return rn->info;
}

/*
 * bgp_node_lookup
 */
static inline struct bgp_dest *
bgp_node_lookup(const struct bgp_table *const table, const struct prefix *p)
{
	struct route_node *rn = route_node_lookup(table->route_table, p);

	return bgp_dest_from_rnode(rn);
}

/*
 * bgp_node_match
 */
static inline struct bgp_dest *bgp_node_match(const struct bgp_table *table,
					      const struct prefix *p)
{
	struct route_node *rn = route_node_match(table->route_table, p);

	return bgp_dest_from_rnode(rn);
}

static inline unsigned long bgp_table_count(const struct bgp_table *const table)
{
	return route_table_count(table->route_table);
}

/*
 * bgp_table_get_next
 */
static inline struct bgp_dest *bgp_table_get_next(const struct bgp_table *table,
						  const struct prefix *p)
{
	struct route_node *rn = route_table_get_next(table->route_table, p);

	while (rn && !rn->info)
		rn = route_next(rn);
	return bgp_dest_from_rnode(rn);
}

/* This would benefit from a real atomic operation...
 * until then. */
static inline uint64_t bgp_table_next_version(struct bgp_table *table)
{
	return ++table->version;
}

static inline uint64_t bgp_table_version(struct bgp_table *table)
{
	return table->version;
}

/* Find the subtree of the prefix p
 *
 * This will return the first node that belongs the the subtree of p. Including
 * p itself, if it is in the tree.
 *
 * If the subtree is not present in the table, NULL is returned.
 */
struct bgp_dest *bgp_table_subtree_lookup(const struct bgp_table *table,
					  const struct prefix *p);

/* 下面这一组 get/set 全是对 bgp_dest->info 这个多态指针的“带类型解释”，
 * 只是为了让调用的地方读起来有类型、有语义（见文件头“多态”说明）。
 * 它们不做任何转换/校验，set 会直接覆盖 info——所以同一 dest 同时被两种用途
 * 使用时是错的，但实际上每种 dest 只属于一种表，因此是安全的。 */
/* 把 dest->info 按“聚合信息”类型取出(bgp_aggregate)。dest 为空返回 NULL。 */
static inline struct bgp_aggregate *
bgp_dest_get_bgp_aggregate_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为聚合信息指针。 */
static inline void
bgp_dest_set_bgp_aggregate_info(struct bgp_dest *dest,
				struct bgp_aggregate *aggregate)
{
	dest->info = aggregate;
}

/* 把 dest->info 按“distance(管理距离)信息”类型取出(bgp_distance)。 */
static inline struct bgp_distance *
bgp_dest_get_bgp_distance_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为 distance 信息指针。 */
static inline void bgp_dest_set_bgp_distance_info(struct bgp_dest *dest,
						  struct bgp_distance *distance)
{
	dest->info = distance;
}

/* 把 dest->info 按“静态路由信息”类型取出(bgp_static)。 */
static inline struct bgp_static *
bgp_dest_get_bgp_static_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为静态路由信息指针。 */
static inline void bgp_dest_set_bgp_static_info(struct bgp_dest *dest,
						struct bgp_static *bgp_static)
{
	dest->info = bgp_static;
}

/* 把 dest->info 按“直连引用信息”类型取出(bgp_connected_ref)。 */
static inline struct bgp_connected_ref *
bgp_dest_get_bgp_connected_ref_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为直连引用信息指针。 */
static inline void
bgp_dest_set_bgp_connected_ref_info(struct bgp_dest *dest,
				    struct bgp_connected_ref *bc)
{
	dest->info = bc;
}

/* 把 dest->info 按“下一跳缓存信息”类型取出(bgp_nexthop_cache)。 */
static inline struct bgp_nexthop_cache *
bgp_dest_get_bgp_nexthop_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为下一跳缓存信息指针。 */
static inline void bgp_dest_set_bgp_nexthop_info(struct bgp_dest *dest,
						 struct bgp_nexthop_cache *bnc)
{
	dest->info = bnc;
}

/* 把 dest->info 按“路径信息”类型取出(bgp_path_info)；这是最常见的一种用途。 */
static inline struct bgp_path_info *
bgp_dest_get_bgp_path_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为路径信息指针。 */
static inline void bgp_dest_set_bgp_path_info(struct bgp_dest *dest,
					      struct bgp_path_info *bi)
{
	dest->info = bi;
}

/* 把 dest->info 按“EOIU 信息”类型取出(bgp_eoiu_info)。 */
static inline struct bgp_eoiu_info *bgp_dest_get_bgp_eoiu_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为 EOIU 信息指针。 */
static inline void bgp_dest_set_bgp_eoiu_info(struct bgp_dest *dest, struct bgp_eoiu_info *eoiu_info)
{
	dest->info = eoiu_info;
}

/* 把 dest->info 按“子表信息”类型取出(struct bgp_table)，用于分层的表节点。 */
static inline struct bgp_table *
bgp_dest_get_bgp_table_info(struct bgp_dest *dest)
{
	return dest ? dest->info : NULL;
}

/* 把 dest->info 设置为子表信息指针。 */
static inline void bgp_dest_set_bgp_table_info(struct bgp_dest *dest,
					       struct bgp_table *table)
{
	dest->info = table;
}

/* 判断该 dest 的 info 是否非空，即是否挂了任意类型的关联数据。 */
static inline bool bgp_dest_has_bgp_path_info_data(struct bgp_dest *dest)
{
	return dest ? !!dest->info : false;
}

/* 取该 dest 对应的前缀(&dest->rn->p)；dest 为空返回 NULL。 */
static inline const struct prefix *bgp_dest_get_prefix(const struct bgp_dest *dest)
{
	return dest ? &dest->rn->p : NULL;
}

/* 取该 dest 节点的引用计数(dest->rn->lock)。 */
static inline unsigned int bgp_dest_get_lock_count(const struct bgp_dest *dest)
{
	return dest ? dest->rn->lock : 0;
}

#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pBD"  (struct bgp_dest *)
#endif

#endif /* _QUAGGA_BGP_TABLE_H */
