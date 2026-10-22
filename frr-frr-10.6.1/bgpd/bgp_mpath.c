// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Multipath
 * Copyright (C) 2010 Google Inc.
 *               2024 Nvidia Corporation
 *                    Donald Sharp
 *
 * This file is part of FRR
 */

/*
 * 文件导读：BGP 多路径（Multipath）选择与聚合属性
 *
 * 本模块负责 BGP 的等价多路径（ECMP）：在最佳路径选择完成后，决定哪些次优
 * 路径与最佳路径“等价”从而可一起参与转发，并把这些多路径聚合成一条对外通告
 * 的属性。它是“路径选择（bgp_route.c 的 bestpath）”的补充：bestpath 选出
 * 一条最佳路径，本模块再挑出与之并列的多条路径。
 *
 * 关键数据结构：每个前缀（bgp_dest）的 mpath 字段是一个 struct
 * bgp_path_info_mpath，惰性分配，记录多路径条数（mp_count）、聚合后的属性
 * （mp_attr）、累计链路带宽（cum_bw）以及若干标志（mp_flags：是否有链路带宽
 * BGP_MP_LB_PRESENT、是否所有路径都有带宽 BGP_MP_LB_ALL）。参与多路径的路径
 * 会被打上 BGP_PATH_MULTIPATH 标志。
 *
 * 与相邻模块的配合：
 *   - 由 bgp_route.c 在最佳路径变化后调用 bgp_path_info_mpath_update 同步标志，
 *     再调用 bgp_path_info_mpath_aggregate_update 生成聚合属性；
 *   - 链路带宽（link-bandwidth, lb）相关的加权 ECMP 依赖 bgp_nhc.c 的 NNHN 计数
 *     与扩展团体里的带宽值；
 *   - 对外通告时读取 bgp_path_info_mpath_attr / cumbw / chkwtd 提供聚合结果。
 *
 * 读的顺序：先看 maxpaths 配置的 set / unset，再看下一跳比较（判断“等价”的
 * 核心），然后是 mpath 元素的分配 / 释放 / 取用与各种 get / set 访问器，
 * 最后看庞大的 mpath_update 标志同步与 aggregate_update 属性聚合。
 */

#include <zebra.h>

#include "command.h"
#include "prefix.h"
#include "sockunion.h"
#include "memory.h"
#include "queue.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_mpath.h"
#include "bgpd/bgp_nhc.h"

/*
 * 记录某实例在指定 AFI / SAFI 下的 maximum-paths 配置。ibgp 类型同时记录
 * same_clusterlen（是否把 cluster list 长度纳入等价比较），ebgp 类型只记条数。
 * 成功返回 0；bgp 为空或 afi / safi 越界、peertype 非法时返回 -1。
 */
/*
 * bgp_maximum_paths_set
 *
 * Record maximum-paths configuration for BGP instance
 */
int bgp_maximum_paths_set(struct bgp *bgp, afi_t afi, safi_t safi, int peertype,
			  uint16_t maxpaths, bool same_clusterlen)
{
	if (!bgp || (afi >= AFI_MAX) || (safi >= SAFI_MAX))
		return -1;

	switch (peertype) {
	case BGP_PEER_IBGP:
		bgp->maxpaths[afi][safi].maxpaths_ibgp = maxpaths;
		bgp->maxpaths[afi][safi].same_clusterlen = same_clusterlen;
		break;
	case BGP_PEER_EBGP:
		bgp->maxpaths[afi][safi].maxpaths_ebgp = maxpaths;
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * 移除某实例的 maximum-paths 配置（回到默认值 multipath_num，并清除 ibgp 的
 * same_clusterlen）。成功返回 0；参数非法时返回 -1。
 */
/*
 * bgp_maximum_paths_unset
 *
 * Remove maximum-paths configuration from BGP instance
 */
int bgp_maximum_paths_unset(struct bgp *bgp, afi_t afi, safi_t safi,
			    int peertype)
{
	if (!bgp || (afi >= AFI_MAX) || (safi >= SAFI_MAX))
		return -1;

	switch (peertype) {
	case BGP_PEER_IBGP:
		bgp->maxpaths[afi][safi].maxpaths_ibgp = multipath_num;
		bgp->maxpaths[afi][safi].same_clusterlen = false;
		break;
	case BGP_PEER_EBGP:
		bgp->maxpaths[afi][safi].maxpaths_ebgp = multipath_num;
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * 判断两个接口是否为同一接口（比较 ifindex）。两者皆空视为相同（返回 1），
 * 仅一个为空视为不同（返回 0）。用于 IPv6 全局加链路本地下一跳的场景下，
 * 判断下一跳是否落在同一接口上。
 */
/*
 * bgp_interface_same
 *
 * Return true if ifindex for ifp1 and ifp2 are the same, else return false.
 */
static int bgp_interface_same(struct interface *ifp1, struct interface *ifp2)
{
	if (!ifp1 && !ifp2)
		return 1;

	if (!ifp1 && ifp2)
		return 0;

	if (ifp1 && !ifp2)
		return 0;

	return (ifp1->ifindex == ifp2->ifindex);
}


/*
 * 比较两条路径的下一跳，是判断“多路径等价”的核心。返回小于 / 等于 / 大于 0，
 * 分别表示 bpi1 的下一跳小于 / 等于 / 大于 bpi2。比较顺序：先比 IPv4 下一跳，
 * 再按 mp_nexthop_len 类型分别比 IPv4 / IPv6 全局 / IPv6 全局加链路本地地址；
 * 对全局加链路本地情形，若双方都未偏好全局地址还要先比接口是否相同。
 * 最后若下一跳相同且不是黑洞下一跳，还要检查两者来源 VRF 是否相同（vrfleak）。
 * 该函数在 bestpath 之后被调用，供多路径判定使用。
 */
/*
 * bgp_path_info_nexthop_cmp
 *
 * Compare the nexthops of two paths. Return value is less than, equal to,
 * or greater than zero if bpi1 is respectively less than, equal to,
 * or greater than bpi2.
 */
int bgp_path_info_nexthop_cmp(struct bgp_path_info *bpi1,
			      struct bgp_path_info *bpi2)
{
	int compare;
	struct in6_addr addr1, addr2;

	compare = IPV4_ADDR_CMP(&bpi1->attr->nexthop, &bpi2->attr->nexthop);
	if (!compare) {
		if (bpi1->attr->mp_nexthop_len == bpi2->attr->mp_nexthop_len) {
			switch (bpi1->attr->mp_nexthop_len) {
			case BGP_ATTR_NHLEN_IPV4:
			case BGP_ATTR_NHLEN_VPNV4:
				compare = IPV4_ADDR_CMP(
					&bpi1->attr->mp_nexthop_global_in,
					&bpi2->attr->mp_nexthop_global_in);
				break;
			case BGP_ATTR_NHLEN_IPV6_GLOBAL:
			case BGP_ATTR_NHLEN_VPNV6_GLOBAL:
				compare = IPV6_ADDR_CMP(
					&bpi1->attr->mp_nexthop_global,
					&bpi2->attr->mp_nexthop_global);
				break;
			case BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL:
				addr1 = (CHECK_FLAG(bpi1->attr->nh_flags,
						    BGP_ATTR_NH_MP_PREFER_GLOBAL))
						? bpi1->attr->mp_nexthop_global
						: bpi1->attr->mp_nexthop_local;
				addr2 = (CHECK_FLAG(bpi2->attr->nh_flags,
						    BGP_ATTR_NH_MP_PREFER_GLOBAL))
						? bpi2->attr->mp_nexthop_global
						: bpi2->attr->mp_nexthop_local;

				if (!CHECK_FLAG(bpi1->attr->nh_flags,
						BGP_ATTR_NH_MP_PREFER_GLOBAL) &&
				    !CHECK_FLAG(bpi2->attr->nh_flags,
						BGP_ATTR_NH_MP_PREFER_GLOBAL))
					compare = !bgp_interface_same(
						bpi1->peer->ifp,
						bpi2->peer->ifp);

				if (!compare)
					compare = IPV6_ADDR_CMP(&addr1, &addr2);
				break;
			}
		}

		/* This can happen if one IPv6 peer sends you global and
		 * link-local
		 * nexthops but another IPv6 peer only sends you global
		 */
		else if (bpi1->attr->mp_nexthop_len
				 == BGP_ATTR_NHLEN_IPV6_GLOBAL
			 || bpi1->attr->mp_nexthop_len
				    == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL) {
			compare = IPV6_ADDR_CMP(&bpi1->attr->mp_nexthop_global,
						&bpi2->attr->mp_nexthop_global);
			if (!compare) {
				if (bpi1->attr->mp_nexthop_len
				    < bpi2->attr->mp_nexthop_len)
					compare = -1;
				else
					compare = 1;
			}
		}
	}

	/*
	 * If both nexthops are same then check
	 * if they belong to same VRF
	 */
	if (!compare && bpi1->attr->nh_type != NEXTHOP_TYPE_BLACKHOLE) {
		if (bpi1->extra && bpi1->extra->vrfleak &&
		    bpi1->extra->vrfleak->bgp_orig && bpi2->extra &&
		    bpi2->extra->vrfleak && bpi2->extra->vrfleak->bgp_orig) {
			if (bpi1->extra->vrfleak->bgp_orig->vrf_id !=
			    bpi2->extra->vrfleak->bgp_orig->vrf_id) {
				compare = 1;
			}
		}
	}

	return compare;
}

/*
 * 分配并清零一个新的多路径元素（struct bgp_path_info_mpath），初始 mp_count
 * 置 1，其余字段为 0。返回新对象，由调用方负责释放。
 */
/*
 * bgp_path_info_mpath_new
 *
 * Allocate and zero memory for a new bgp_path_info_mpath element
 */
static struct bgp_path_info_mpath *bgp_path_info_mpath_new(void)
{
	struct bgp_path_info_mpath *new_mpath;

	new_mpath = XCALLOC(MTYPE_BGP_MPATH_INFO,
			    sizeof(struct bgp_path_info_mpath));

	new_mpath->mp_count = 1;
	return new_mpath;
}

/*
 * 释放多路径元素：若存在会先 unintern 其聚合属性（mp_attr），再释放结构体。
 * 传入的是二级指针，便于调用方在释放后置空。mpath 或 *mpath 为空时安全返回。
 */
/*
 * bgp_path_info_mpath_free
 *
 * Release resources for a bgp_path_info_mpath element and zero out pointer
 */
void bgp_path_info_mpath_free(struct bgp_path_info_mpath **mpath)
{
	if (mpath && *mpath) {
		if ((*mpath)->mp_attr)
			bgp_attr_unintern(&(*mpath)->mp_attr);
		(*mpath)->mp_attr = NULL;

		XFREE(MTYPE_BGP_MPATH_INFO, *mpath);
	}
}

/*
 * 取某前缀的多路径元素，若尚未分配则惰性新建（并把 mp_dest 回指 dest）。
 * dest 为空返回 NULL。这是所有 mpath 写入路径的统一入口，保证用到才分配。
 */
/*
 * bgp_path_info_mpath_get
 *
 * Fetch the mpath element for the given bgp_dest. Used for
 * doing lazy allocation.
 */
static struct bgp_path_info_mpath *bgp_path_info_mpath_get(struct bgp_dest *dest)
{
	struct bgp_path_info_mpath *mpath;

	if (!dest)
		return NULL;

	if (!dest->mpath) {
		mpath = bgp_path_info_mpath_new();
		dest->mpath = mpath;
		mpath->mp_dest = dest;
	}
	return dest->mpath;
}

/*
 * 从给定路径出发，沿 next 链向后找下一条带 BGP_PATH_MULTIPATH 标志的路径，
 * 即“下一条多路径”。未找到返回 NULL。注意是从 path->next 开始找，不含自身。
 */
/*
 * bgp_path_info_mpath_next
 *
 * Given a bgp_path_info, return the next multipath entry
 */
struct bgp_path_info *bgp_path_info_mpath_next(struct bgp_path_info *path)
{
	path = path->next;

	while (path) {
		if (CHECK_FLAG(path->flags, BGP_PATH_MULTIPATH))
			return path;

		path = path->next;
	}

	return NULL;
}

/*
 * 给定最佳路径，返回第一条多路径（即从其后起第一条带多路径标志的路径）。
 * 直接复用 bgp_path_info_mpath_next。
 */
/*
 * bgp_path_info_mpath_first
 *
 * Given bestpath bgp_path_info, return the first multipath entry.
 */
struct bgp_path_info *bgp_path_info_mpath_first(struct bgp_path_info *path)
{
	return bgp_path_info_mpath_next(path);
}

/*
 * 返回某前缀的多路径总数（含最佳路径自身）。dest 为空或尚无 mpath 元素时
 * 返回 1，表示“只有一条路径”。
 */
/*
 * bgp_path_info_mpath_count
 *
 * Given the bgp_dest, return the number of multipath entries
 */
uint32_t bgp_path_info_mpath_count(struct bgp_dest *dest)
{
	if (!dest || !dest->mpath)
		return 1;

	return dest->mpath->mp_count;
}

/*
 * 把多路径条数写入前缀的 mpath 元素。特殊情形：count 为 0 且该前缀本来就没有
 * mpath 元素时直接返回（不为清零而分配）。否则取（必要时惰性新建）mpath 元素
 * 并写 mp_count。注意 count 参数类型是 uint16_t，而 getter 返回 uint32_t。
 */
/*
 * bgp_path_info_mpath_count_set
 *
 * Sets the count of multipaths into bgp_dest's mpath element
 */
static void bgp_path_info_mpath_count_set(struct bgp_dest *dest, uint16_t count)
{
	struct bgp_path_info_mpath *mpath;
	if (!count && (!dest || !dest->mpath))
		return;
	mpath = bgp_path_info_mpath_get(dest);
	if (!mpath)
		return;
	mpath->mp_count = count;
}

/*
 * 更新与前缀 mpath 元素相关的链路带宽（link-bandwidth）累计信息。
 * set 为真：按 cum_bw 是否非零置 / 清 BGP_MP_LB_PRESENT，按 all_paths_lb 置 /
 * 清 BGP_MP_LB_ALL，并记录累计带宽 cum_bw。
 * set 为假：清除全部标志与累计带宽（移除时务必清干净，避免残留标志误触发）。
 * 当前缀没有 mpath 元素时，只有在 set 且确实有数据（cum_bw 非零或 all_paths_lb）
 * 才惰性新建，否则直接返回，避免无谓分配。
 */
/*
 * bgp_path_info_mpath_lb_update
 *
 * Update cumulative info related to link-bandwidth
 *
 * This is set on mpath of the bgp_dest,
 * we should UNSET the flags when removing
 * to ensure nothing accidentally happens
 */
static void bgp_path_info_mpath_lb_update(struct bgp_dest *dest, bool set, bool all_paths_lb,
					  uint64_t cum_bw)
{
	struct bgp_path_info_mpath *mpath;

	if (!dest)
		return;

	mpath = dest->mpath;
	if (mpath == NULL) {
		if (!set || (cum_bw == 0 && !all_paths_lb))
			return;

		mpath = bgp_path_info_mpath_get(dest);
		if (!mpath)
			return;
	}
	if (set) {
		if (cum_bw)
			SET_FLAG(mpath->mp_flags, BGP_MP_LB_PRESENT);
		else
			UNSET_FLAG(mpath->mp_flags, BGP_MP_LB_PRESENT);
		if (all_paths_lb)
			SET_FLAG(mpath->mp_flags, BGP_MP_LB_ALL);
		else
			UNSET_FLAG(mpath->mp_flags, BGP_MP_LB_ALL);
		mpath->cum_bw = cum_bw;
	} else {
		mpath->mp_flags = 0;
		mpath->cum_bw = 0;
	}
}

/*
 * 返回用于对外通告该多路径路由的聚合属性（mp_attr）。dest 为空或没有 mpath
 * 元素时返回 NULL。
 */
/*
 * bgp_path_info_mpath_attr
 *
 * Given bgp_dest, return aggregated attribute set used
 * for advertising the multipath route
 */
struct attr *bgp_path_info_mpath_attr(struct bgp_dest *dest)
{
	if (!dest || !dest->mpath)
		return NULL;
	return dest->mpath->mp_attr;
}

/*
 * 决定是否对某前缀做加权 ECMP，返回具体行为枚举。默认行为由实例是否开启
 * 递归权重（BGP_FLAG_USE_RECURSIVE_WEIGHT）决定。若不是多路径则直接返回默认。
 * 当链路带宽被配置为忽略（IGNORE_BW）且路径带 NHC 属性时，改用 NNHN 计数做
 * 加权。否则根据 lb_handling 配置：非“跳过缺失 / 缺失用默认权重”时，只有
 * 所有路径都有带宽（BGP_MP_LB_ALL）才用带宽加权；在该两种配置下则只要有带宽
 * 存在（BGP_MP_LB_PRESENT）即可。
 */
/*
 * bgp_path_info_chkwtd
 *
 * Return if we should attempt to do weighted ECMP or not
 * Pass the bgp_dest in.
 */
enum bgp_wecmp_behavior bgp_path_info_mpath_chkwtd(struct bgp *bgp, struct bgp_dest *dest)
{
	enum bgp_wecmp_behavior default_val = BGP_WECMP_BEHAVIOR_NONE;

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_USE_RECURSIVE_WEIGHT))
		default_val = BGP_WECMP_BEHAVIOR_USE_RECURSIVE_VALUE;

	/* Check if not multipath */
	if (!dest || !dest->mpath)
		return default_val;

	struct bgp_path_info *path = bgp_dest_get_bgp_path_info(dest);

	/* If link bandwidth is to be ignored, check if we have Next-Next Hop Nodes
	 * characteristic and do weighted ECMP based on that.
	 */
	if (bgp->lb_handling == BGP_LINK_BW_IGNORE_BW) {
		if (bgp_attr_exists(path->attr, BGP_ATTR_NHC))
			return BGP_WECMP_BEHAVIOR_NNHN_COUNT;
	}

	/* All paths in multipath should have associated weight (bandwidth)
	 * unless told explicitly otherwise.
	 */
	if (bgp->lb_handling != BGP_LINK_BW_SKIP_MISSING &&
	    bgp->lb_handling != BGP_LINK_BW_DEFWT_4_MISSING) {
		if (CHECK_FLAG(dest->mpath->mp_flags, BGP_MP_LB_ALL))
			return BGP_WECMP_BEHAVIOR_LINK_BW;
		else
			return default_val;
	}

	if (CHECK_FLAG(dest->mpath->mp_flags, BGP_MP_LB_PRESENT))
		return BGP_WECMP_BEHAVIOR_LINK_BW;

	return default_val;
}

/*
 * 返回该前缀所有带带宽信息的多路径累计带宽。dest 为空或没有 mpath 元素时返回 0。
 */
/*
 * bgp_path_info_mpath_attr
 *
 * Given bgp_dest, return cumulative bandwidth
 * computed for all multipaths with bandwidth info
 */
uint64_t bgp_path_info_mpath_cumbw(struct bgp_dest *dest)
{
	if (!dest || !dest->mpath)
		return 0;
	return dest->mpath->cum_bw;
}

/*
 * 把聚合属性写入前缀的 mpath 元素。特殊情形：attr 为空且该前缀本无 mpath 元素时
 * 直接返回（不为置空而分配）。否则取（必要时惰性新建）mpath 元素并写 mp_attr。
 * 注意本函数不管理 attr 的引用计数，交给调用方处理。
 */
/*
 * bgp_path_info_mpath_attr_set
 *
 * Sets the aggregated attribute into bgp_dest's mpath element
 */
static void bgp_path_info_mpath_attr_set(struct bgp_dest *dest, struct attr *attr)
{
	struct bgp_path_info_mpath *mpath;
	if (!attr && (!dest || !dest->mpath))
		return;
	mpath = bgp_path_info_mpath_get(dest);
	if (!mpath)
		return;
	mpath->mp_attr = attr;
}

/*
 * 多路径标志同步：把路径上的 BGP_PATH_MULTIPATH 标志与本次最佳路径选择的结果
 * 对齐，并更新条数与链路带宽累计。是 bestpath 之后的核心步骤。
 * 参数：dest 前缀、new_best 新最佳、old_best 旧最佳、num_candidates 候选数、
 * mpath_cfg 该实例的最大路径配置。流程：先按 old_best 清理旧的 mpath 元素并
 * 记下旧条数 / 旧累计带宽；再依据 new_best 的 peer 类型取 ibgp / ebgp 的上限；
 * 然后沿路径链有序地扫描，用旧标志（MULTIPATH）与新标志（MULTIPATH_NEW）判断
 * 一条路径是否被新选为多路径，达到 maxpaths 上限即清理余下标志并结束；据此
 * 累计条数、带宽并判断 all_paths_lb；最后回写 mpath 元素，设置 MULTIPATH_CHG
 * 与 LINK_BW_CHG 标志以驱动后续对邻居的更新。附带大量调试题日志。
 */
/*
 * bgp_path_info_mpath_update
 *
 * Compare and sync up the multipath flags with what was chosen
 * in best selection
 */
void bgp_path_info_mpath_update(struct bgp *bgp, struct bgp_dest *dest,
				struct bgp_path_info *new_best, struct bgp_path_info *old_best,
				uint32_t num_candidates, struct bgp_maxpaths_cfg *mpath_cfg)
{
	uint16_t maxpaths, mpath_count, old_mpath_count;
	uint64_t bwval;
	uint64_t cum_bw, old_cum_bw;
	struct bgp_path_info *cur_iterator = NULL;
	bool mpath_changed, debug;
	bool all_paths_lb;
	char path_buf[PATH_ADDPATH_STR_BUFFER];
	bool old_mpath, new_mpath;

	mpath_changed = false;
	maxpaths = multipath_num;
	mpath_count = 0;
	old_mpath_count = 0;
	old_cum_bw = cum_bw = 0;
	debug = bgp_debug_bestpath(dest);

	if (old_best) {
		old_mpath_count = bgp_path_info_mpath_count(dest);
		if (old_mpath_count == 1)
			SET_FLAG(old_best->flags, BGP_PATH_MULTIPATH);
		old_cum_bw = bgp_path_info_mpath_cumbw(dest);
		bgp_path_info_mpath_count_set(dest, 0);
		bgp_path_info_mpath_lb_update(dest, false, false, 0);
		bgp_path_info_mpath_free(&dest->mpath);
		dest->mpath = NULL;
	}

	if (new_best) {
		maxpaths = (new_best->peer->sort == BGP_PEER_IBGP) ? mpath_cfg->maxpaths_ibgp
								   : mpath_cfg->maxpaths_ebgp;
		cur_iterator = new_best;
	}

	if (debug)
		zlog_debug("%pBD(%s): starting mpath update, newbest %s num candidates %d old-mpath-count %d old-cum-bw %" PRIu64
			   " maxpaths set %u",
			   dest, bgp->name_pretty, new_best ? new_best->peer->host : "NONE",
			   num_candidates, old_mpath_count, old_cum_bw, maxpaths);

	/*
	 * We perform an ordered walk through both lists in parallel.
	 * The reason for the ordered walk is that if there are paths
	 * that were previously multipaths and are still multipaths, the walk
	 * should encounter them in both lists at the same time. Otherwise
	 * there will be paths that are in one list or another, and we
	 * will deal with these separately.
	 *
	 * Note that new_best might be somewhere in the mp_list, so we need
	 * to skip over it
	 */
	all_paths_lb = true; /* We'll reset if any path doesn't have LB. */

	while (cur_iterator) {
		old_mpath = CHECK_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH);
		new_mpath = CHECK_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH_NEW);

		UNSET_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH_NEW);
		/*
		 * If the current mpath count is equal to the number of
		 * maxpaths that can be used then we can bail, after
		 * we clean up the flags associated with the rest of the
		 * bestpaths
		 */
		if (mpath_count >= maxpaths) {
			while (cur_iterator) {
				UNSET_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH);
				UNSET_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH_NEW);

				cur_iterator = cur_iterator->next;
			}

			if (debug)
				zlog_debug("%pBD(%s): Mpath count %u is equal to maximum paths allowed, finished comparison for MPATHS",
					   dest, bgp->name_pretty, mpath_count);

			break;
		}

		if (debug)
			zlog_debug("%pBD(%s): Candidate %s old_mpath: %u new_mpath: %u, Nexthop %pI4 current mpath count: %u",
				   dest, bgp->name_pretty, cur_iterator->peer->host, old_mpath,
				   new_mpath, &cur_iterator->attr->nexthop, mpath_count);
		/*
		 * There is nothing to do if the cur_iterator is neither a old path
		 * or a new path
		 */
		if (!old_mpath && !new_mpath) {
			UNSET_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH);
			cur_iterator = cur_iterator->next;
			continue;
		}

		if (new_mpath) {
			mpath_count++;

			if (cur_iterator != new_best)
				SET_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH);

			if (!old_mpath)
				mpath_changed = true;

			if (ecommunity_linkbw_present(bgp_attr_get_ecommunity(cur_iterator->attr),
						      &bwval) ||
			    ecommunity_linkbw_present(bgp_attr_get_ipv6_ecommunity(
							      cur_iterator->attr),
						      &bwval))
				cum_bw += bwval;
			else
				all_paths_lb = false;

			if (debug) {
				bgp_path_info_path_with_addpath_rx_str(cur_iterator, path_buf,
								       sizeof(path_buf));
				zlog_debug("%pBD: add mpath %s nexthop %pI4, cur count %d cum_bw: %" PRIu64
					   " all_paths_lb: %u",
					   dest, path_buf, &cur_iterator->attr->nexthop,
					   mpath_count, cum_bw, all_paths_lb);
			}
		} else {
			/*
			 * We know that old_mpath is true and new_mpath is false in this path
			 */
			mpath_changed = true;
			UNSET_FLAG(cur_iterator->flags, BGP_PATH_MULTIPATH);
		}

		cur_iterator = cur_iterator->next;
	}

	if (new_best) {
		if (mpath_count > 1) {
			bgp_path_info_mpath_count_set(dest, mpath_count);
			bgp_path_info_mpath_lb_update(dest, true, all_paths_lb, cum_bw);
		}
		if (debug)
			zlog_debug("%pBD(%s): New mpath count (incl newbest) %d mpath-change %s all_paths_lb %d cum_bw %" PRIu64,
				   dest, bgp->name_pretty, mpath_count,
				   mpath_changed ? "YES" : "NO", all_paths_lb,
				   cum_bw);

		if (mpath_count == 1) {
			UNSET_FLAG(new_best->flags, BGP_PATH_MULTIPATH);
			if (dest->mpath)
				bgp_path_info_mpath_free(&dest->mpath);
		}
		if (mpath_changed || (bgp_path_info_mpath_count(dest) != old_mpath_count))
			SET_FLAG(new_best->flags, BGP_PATH_MULTIPATH_CHG);
		if ((mpath_count) != old_mpath_count || old_cum_bw != cum_bw)
			SET_FLAG(new_best->flags, BGP_PATH_LINK_BW_CHG);
	}
}

/*
 * 生成 / 更新多路径的聚合属性，并在其变化时给最佳路径打上 BGP_PATH_ATTR_CHANGED，
 * 以驱动对邻居的更新。判定“是否变化”的办法是：生成当前聚合属性、intern（去重
 * 引用）后与已有聚合属性指针比较。若多路径选择与各路径属性都没有变化，可直接跳过
 * 生成比较这一步。
 * 逻辑：先处理旧最佳（若换了最佳则 unintern 其旧聚合属性）；new_best 为空则返回；
 * 条数为 1 时清除聚合属性并标记变化后返回。随后在开启 MULTIPATH_RELAX_AS_SET 时，
 * 从最佳路径起逐条合并多路径成员的 AS_PATH（聚合）、origin（取较大者）、标准 /
 * 扩展 / 大团体（合并去重排序），并把下一跳清为 0（表示改由本机作为下一跳）。
 * 最后 intern 新属性，与旧指针不同则替换并标记 ATTR_CHANGED，否则释放新引用。
 * 注意：属性各字段的 dup / merge / unintern 引用计数必须成对，避免泄漏或悬垂。
 */
/*
 * bgp_path_info_mpath_aggregate_update
 *
 * Set the multipath aggregate attribute. We need to see if the
 * aggregate has changed and then set the ATTR_CHANGED flag on the
 * bestpath info so that a peer update will be generated. The
 * change is detected by generating the current attribute,
 * interning it, and then comparing the interned pointer with the
 * current value. We can skip this generate/compare step if there
 * is no change in multipath selection and no attribute change in
 * any multipath.
 */
void bgp_path_info_mpath_aggregate_update(struct bgp_path_info *new_best,
					  struct bgp_path_info *old_best)
{
	struct bgp_path_info *mpinfo;
	struct aspath *aspath;
	struct aspath *asmerge;
	struct attr *new_attr, *old_attr;
	uint8_t origin;
	struct community *community, *commerge;
	struct ecommunity *ecomm, *ecommerge;
	struct lcommunity *lcomm, *lcommerge;
	struct attr attr = {0};

	if (old_best && (old_best != new_best) &&
	    (old_attr = bgp_path_info_mpath_attr(old_best->net))) {
		bgp_attr_unintern(&old_attr);
		bgp_path_info_mpath_attr_set(old_best->net, NULL);
	}

	if (!new_best)
		return;

	if (bgp_path_info_mpath_count(new_best->net) == 1) {
		if ((new_attr = bgp_path_info_mpath_attr(new_best->net))) {
			bgp_attr_unintern(&new_attr);
			bgp_path_info_mpath_attr_set(new_best->net, NULL);
			SET_FLAG(new_best->flags, BGP_PATH_ATTR_CHANGED);
		}
		return;
	}

	attr = *new_best->attr;

	if (new_best->peer
	    && CHECK_FLAG(new_best->peer->bgp->flags,
			  BGP_FLAG_MULTIPATH_RELAX_AS_SET)) {

		/* aggregate attribute from multipath constituents */
		aspath = aspath_dup(attr.aspath);
		origin = attr.origin;
		community =
			bgp_attr_get_community(&attr)
				? community_dup(bgp_attr_get_community(&attr))
				: NULL;
		ecomm = (bgp_attr_get_ecommunity(&attr))
				? ecommunity_dup(bgp_attr_get_ecommunity(&attr))
				: NULL;
		lcomm = (bgp_attr_get_lcommunity(&attr))
				? lcommunity_dup(bgp_attr_get_lcommunity(&attr))
				: NULL;

		for (mpinfo = bgp_path_info_mpath_first(new_best); mpinfo;
		     mpinfo = bgp_path_info_mpath_next(mpinfo)) {
			asmerge =
				aspath_aggregate(aspath, mpinfo->attr->aspath);
			aspath_free(aspath);
			aspath = asmerge;

			if (origin < mpinfo->attr->origin)
				origin = mpinfo->attr->origin;

			if (bgp_attr_get_community(mpinfo->attr)) {
				if (community) {
					commerge = community_merge(
						community,
						bgp_attr_get_community(
							mpinfo->attr));
					community =
						community_uniq_sort(commerge);
					community_free(&commerge);
				} else
					community = community_dup(
						bgp_attr_get_community(
							mpinfo->attr));
			}

			if (bgp_attr_get_ecommunity(mpinfo->attr)) {
				if (ecomm) {
					ecommerge = ecommunity_merge(
						ecomm, bgp_attr_get_ecommunity(
							       mpinfo->attr));
					ecomm = ecommunity_uniq_sort(ecommerge);
					ecommunity_free(&ecommerge);
				} else
					ecomm = ecommunity_dup(
						bgp_attr_get_ecommunity(
							mpinfo->attr));
			}
			if (bgp_attr_get_lcommunity(mpinfo->attr)) {
				if (lcomm) {
					lcommerge = lcommunity_merge(
						lcomm, bgp_attr_get_lcommunity(
							       mpinfo->attr));
					lcomm = lcommunity_uniq_sort(lcommerge);
					lcommunity_free(&lcommerge);
				} else
					lcomm = lcommunity_dup(
						bgp_attr_get_lcommunity(
							mpinfo->attr));
			}
		}

		attr.aspath = aspath;
		attr.origin = origin;
		if (community)
			bgp_attr_set_community(&attr, community);
		if (ecomm)
			bgp_attr_set_ecommunity(&attr, ecomm);
		if (lcomm)
			bgp_attr_set_lcommunity(&attr, lcomm);

		/* Zap multipath attr nexthop so we set nexthop to self */
		attr.nexthop.s_addr = INADDR_ANY;
		memset(&attr.mp_nexthop_global, 0, sizeof(struct in6_addr));

		/* TODO: should we set ATOMIC_AGGREGATE and AGGREGATOR? */
	}

	new_attr = bgp_attr_intern(&attr);

	if (new_attr != bgp_path_info_mpath_attr(new_best->net)) {
		if ((old_attr = bgp_path_info_mpath_attr(new_best->net)))
			bgp_attr_unintern(&old_attr);
		bgp_path_info_mpath_attr_set(new_best->net, new_attr);
		SET_FLAG(new_best->flags, BGP_PATH_ATTR_CHANGED);
	} else
		bgp_attr_unintern(&new_attr);
}
