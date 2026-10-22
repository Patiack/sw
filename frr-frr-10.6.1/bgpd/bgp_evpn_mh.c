// SPDX-License-Identifier: GPL-2.0-or-later
/* EVPN Multihoming procedures
 *
 * Copyright (C) 2019 Cumulus Networks, Inc.
 * Anuradha Karuppiah
 *
 */
/* ============================================================================
 * 【文件导读】bgp_evpn_mh.c —— EVPN 多归属(EVPN Multihoming, EVPN-MH)的核心实现
 * ============================================================================
 * 【职责与定位】本文件实现 RFC7432 及相关草案里定义的 EVPN 多归属：
 *   一台主机通过多条链路接入多台 PE(交换机)，多条链路构成一个“以太网接入段(ES)”，
 *   同段各链路共享一个 ESI。本文件负责：
 *     1) 本地 ES / ES-EVI 的增删（由 zebra 的配置/链路通知驱动）；
 *     2) 生成并通告本端的 Type-4(ESR) 与 Type-1(EAD-per-ES / EAD-per-EVI) 路由；
 *     3) 接收远端 Type-1/Type-4 路由，落到对应 ES / VNI，维护远端 ES-PE 视图；
 *     4) DF(Designated Forwarder) 选举参数的学习（df_alg / df_pref）；
 *     5) 基于 ES 的下一跳 / L3 NHG 管理（对称 IRB）、路径与 ES 的关联；
 *     6) 周期性的 ES 一致性检查（本地 ES 的 VTEP 列表 vs 各 EVI 的 VTEP 列表）。
 *
 * 【关键概念速查】
 *   ESI           : 接入段标识；zero_esi 为全 0（无效）。
 *   EAD-per-ES    : Type-1，ES 级，含 ESI Label（用于水平分割）。
 *   EAD-per-EVI   : Type-1，EVI 级。
 *   ESR           : Type-4 以太网段路由，携带 DF 选举参数。
 *   DF 选举        : BUM 流量默认只由 DF 转发；本文件用基于偏好值(pref)的选举
 *                   (draft-ietf-bess-evpn-pref-df)，非 RFC7432 的 service-carving。
 *   ES 分片(frag)  : 一个 ES 的 EVI 按 BGP_EVPN_MAX_EVI_PER_ES_FRAG 切分，每片一个
 *                   RD，产生一条 EAD-per-ES。
 *
 * 【本文件在主流程中的位置】
 *   上游：bgp_zebra.c(zebra 通知本地 ES)、bgp_evpn.c(远端 Type-1/Type-4 报文入口)、
 *   bgp_evpn_vty.c(show / 配置)。下游：bgp_evpn.c 的全局表、zebra 的 FDB / NHG、
 *   bgp_nhg.c。数据结构定义见 bgp_evpn_mh.h。
 *
 * 【易错点 / 阅读建议】
 *   1) 本地 ES 的相关路由在“ES 路由表”里，远端 EAD-per-ES 与所有 EAD-per-EVI 在
 *      “VNI 路由表”里，两者不要混淆。
 *   2) 本地 ES 与远端 ES 会合并成同一个对象（同 ESI），flags 上两者可并存。
 *   3) 一致性检查是异步的，靠 pend_es_list 与定时器驱动，读的时候要跟随
 *      bgp_evpn_run_consistency_checks。
 *   4) 建议阅读顺序：bgp_evpn_mh_init(bgp_evpn_es_ 新建/释放族) → 本地 ES 增删
 *      (bgp_evpn_local_es_add/del) → 本地 Type-4/Type-1 生成与通告
 *      → 远端 Type-1/Type-4 处理与导入 → DF 选举与一致性检查
 *      → 路径/NH/L3 NHG 管理 → CLI/show 与初始化收尾。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "filter.h"
#include "prefix.h"
#include "log.h"
#include "memory.h"
#include "stream.h"
#include "hash.h"
#include "jhash.h"
#include "zclient.h"

#include "lib/printfrr.h"

#include "bgpd/bgp_attr_evpn.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_evpn_mh.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_encap_types.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_addpath.h"
#include "bgpd/bgp_label.h"
#include "bgpd/bgp_nhg.h"
#include "bgpd/bgp_mpath.h"
#include "bgpd/bgp_trace.h"

/* 【函数】bgp_evpn_mh_get_vtep_ip —— 从路由属性 attr 的下一跳字段中提取 VTEP IP，
 * 写入出参 vtep_ip。按 mp_nexthop_len 判定地址族：IPv4 走 mp_nexthop_global_in
 * (VPNv4/短格式) 或 attr->nexthop；IPv6 走 mp_nexthop_global；否则回退到 nexthop。
 * 入参非法(attr 或 vtep_ip 为 NULL)时直接返回，不改动出参。 */
static void bgp_evpn_mh_get_vtep_ip(const struct attr *attr, struct ipaddr *vtep_ip)
{
	uint8_t nhfamily;

	if (!attr || !vtep_ip)
		return;

	nhfamily = NEXTHOP_FAMILY(attr->mp_nexthop_len);

	if (nhfamily == AF_INET) {
		SET_IPADDR_V4(vtep_ip);
		if (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4 ||
		    attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4)
			IPV4_ADDR_COPY(&vtep_ip->ipaddr_v4, &attr->mp_nexthop_global_in);
		else
			IPV4_ADDR_COPY(&vtep_ip->ipaddr_v4, &attr->nexthop);
	} else if (nhfamily == AF_INET6) {
		SET_IPADDR_V6(vtep_ip);
		IPV6_ADDR_COPY(&vtep_ip->ipaddr_v6, &attr->mp_nexthop_global);
	} else {
		SET_IPADDR_V4(vtep_ip);
		IPV4_ADDR_COPY(&vtep_ip->ipaddr_v4, &attr->nexthop);
	}
}

static inline void
/* 【函数】bgp_evpn_mh_fill_vtep_ip_from_pi —— 从路径 pi 的属性里提取 VTEP IP。
 * 与上一函数的区别：这里从 path_info 而非裸 attr 取，且注释强调“从 BGP 属性取，
 * 不从前缀取”（全局表里的前缀把 IP 段清零了，取前缀会得到 0）。 */
bgp_evpn_mh_fill_vtep_ip_from_pi(const struct bgp_path_info *pi,
				 struct ipaddr *vtep_ip)
{
	if (!pi || !pi->attr || !vtep_ip)
		return;

	/* Extract VTEP IP from BGP path attributes, not from prefix
	 * (prefix in global table has IP zeroed out)
	 */
	if (pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4 ||
	    pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4) {
		SET_IPADDR_V4(vtep_ip);
		vtep_ip->ipaddr_v4 = pi->attr->mp_nexthop_global_in;
	} else if (pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL ||
		   pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL ||
		   pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL ||
		   pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL) {
		SET_IPADDR_V6(vtep_ip);
		IPV6_ADDR_COPY(&vtep_ip->ipaddr_v6,
			       &pi->attr->mp_nexthop_global);
	}
}

/* 【函数】bgp_evpn_vtep_ip_to_attr_nh —— 反方向：把 VTEP IP 写进 attr 的下一跳字段。
 * v4 同时填 attr->nexthop 与 mp_nexthop_global_in 且 mp_nexthop_len=IPv4；
 * v6 填 mp_nexthop_global 且长度=IPv6_GLOBAL。入参非法则直接返回。 */
void bgp_evpn_vtep_ip_to_attr_nh(const struct ipaddr *vtep_ip, struct attr *attr)
{
	if (!vtep_ip || !attr)
		return;

	/* v4 vtep use v4 attr.nexthop, v6 vtep use mp_nexthop_global */
	if (IS_IPADDR_V4(vtep_ip)) {
		attr->nexthop = vtep_ip->ipaddr_v4;
		attr->mp_nexthop_global_in = vtep_ip->ipaddr_v4;
		attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV4;
	} else if (IS_IPADDR_V6(vtep_ip)) {
		IPV6_ADDR_COPY(&attr->mp_nexthop_global, &vtep_ip->ipaddr_v6);
		attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL;
	}
}

/* 【前置声明】以下为文件内稍后定义、此处先声明以便互相调用的静态函数：
 *   - 本地 ES 下线 / 本地 Type-1 EVI 路由删除；
 *   - es_vtep 的增删、一致性检查挂起/解除；
 *   - ES-EVI 的删除与活跃 VTEP 计数；
 *   - L3 NHG 随 VTEP 变化而更新；
 *   - ES 的新建/释放、路径 ES 解链、ES 本地状态变化时的 MAC 更新；
 *   - 路径 NH 信息释放/解链、ES-VRF 删除、ES-EVI 释放族、一致性检查定时器。 */
static void bgp_evpn_local_es_down(struct bgp *bgp,
		struct bgp_evpn_es *es);
static void bgp_evpn_local_type1_evi_route_del(struct bgp *bgp,
		struct bgp_evpn_es *es);
static struct bgp_evpn_es_vtep *
bgp_evpn_es_vtep_add(struct bgp *bgp, struct bgp_evpn_es *es,
		     struct ipaddr vtep_ip, bool esr, uint8_t df_alg,
		     uint16_t df_pref, int *zret);
static enum zclient_send_status bgp_evpn_es_vtep_del(struct bgp *bgp,
						     struct bgp_evpn_es *es,
						     struct ipaddr vtep_ip,
						     bool esr);
static void bgp_evpn_es_cons_checks_pend_add(struct bgp_evpn_es *es);
static void bgp_evpn_es_cons_checks_pend_del(struct bgp_evpn_es *es);
static struct bgp_evpn_es_evi *
bgp_evpn_local_es_evi_do_del(struct bgp_evpn_es_evi *es_evi);
static uint32_t bgp_evpn_es_get_active_vtep_cnt(struct bgp_evpn_es *es);
static void bgp_evpn_l3nhg_update_on_vtep_chg(struct bgp_evpn_es *es);
static struct bgp_evpn_es *bgp_evpn_es_new(struct bgp *bgp, const esi_t *esi);
static void bgp_evpn_es_free(struct bgp_evpn_es *es, const char *caller);
static void bgp_evpn_path_es_unlink(struct bgp_path_es_info *es_info);
static void bgp_evpn_mac_update_on_es_local_chg(struct bgp_evpn_es *es,
						bool is_local);

/* 【全局量】zero_esi —— 指向全 0 的 ESI 缓冲区，代表“无 ES/无效 ESI”。
 * 头文件里 extern 声明、这里给出定义。 */
esi_t zero_esi_buf, *zero_esi = &zero_esi_buf;
static void bgp_evpn_run_consistency_checks(struct event *t);
static void bgp_evpn_path_nh_info_free(struct bgp_path_evpn_nh_info *nh_info);
static void bgp_evpn_path_nh_unlink(struct bgp_path_evpn_nh_info *nh_info);
static void bgp_evpn_es_vrf_delete(struct bgp_evpn_es_vrf *es_vrf);
static struct bgp_evpn_es_evi *bgp_evpn_es_evi_free_internal(struct bgp_evpn_es_evi *es_evi,
							     bool force);
static struct bgp_evpn_es_evi *bgp_evpn_es_evi_free(struct bgp_evpn_es_evi *es_evi);

/* 【本章】per-ES 路由表（ES 级路由表）
 * 存放内容：本地/远端 ESR(Type-4) 与本地 EAD-per-ES(Type-1)。
 * 这些路由的键是 {ESI, VTEP-IP}，所以“选路”几乎是空操作——所有路径都指向同一个
 * VTEP-IP，最终效果相同。
 * 注意：远端 EAD-per-ES 与所有(本地与远端) EAD-per-EVI 不放这里，而是放 VNI 路由表。
 * 下面先给出该表的“最优路径选择并安装到 zebra”逻辑，再给出安装/卸载入口。 */
/******************************************************************************
 * per-ES (Ethernet Segment) routing table
 *
 * Following routes are added to the ES's routing table -
 * 1. Local and remote ESR (Type-4)
 * 2. Local EAD-per-ES (Type-1).
 *
 * Key for these routes is {ESI, VTEP-IP} so the path selection is practically
 * a no-op i.e. all paths lead to same VTEP-IP (i.e. result in the same VTEP
 * being added to same ES).
 *
 * Note the following routes go into the VNI routing table (instead of the
 * ES routing table) -
 * 1. Remote EAD-per-ES
 * 2. Local and remote EAD-per-EVI
 */

/* 【函数】bgp_evpn_es_route_select_install —— 对 ES 路由表里的一条 MH(Type-1/Type-4)
 * 路由重跑最优路径选择，并按结果更新 ES 上的 es_vtep。
 * 关键分支：
 *   1) 若最优路径未变（old 与 new 相同且均为 IMPORTED、无 addpath、无变更标记）——
 *      只做“是否需要重发”的检查，必要时重新调用 es_vtep_add；
 *   2) 最优路径发生变化——给新 best 置 SELECTED、清旧 best 的 SELECTED；新 best 为
 *      IMPORTED 时 es_vtep_add(esr)，否则对旧的 IMPORTED 路径做 es_vtep_del；
 *   3) 结尾清理 zebra 路由变更标记，并对被删除的旧 best 做 reap。
 * 参数：bgp 实例、es 目标 ES、dest 路由节点、pi 本次变更对应的路径。
 * 返回：0（当前恒为 0，ret 仅预留）。 */
/* Calculate the best path for a multi-homing (Type-1 or Type-4) route
 * installed in the ES's routing table.
 */
static int bgp_evpn_es_route_select_install(struct bgp *bgp,
					    struct bgp_evpn_es *es,
					    struct bgp_dest *dest,
					    struct bgp_path_info *pi)
{
	int ret = 0;
	int zret = 0;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct bgp_path_info *old_select; /* old best */
	struct bgp_path_info *new_select; /* new best */
	struct bgp_path_info_pair old_and_new;

	SET_FLAG(pi->flags, BGP_PATH_UNSORTED);

	/* Compute the best path. */
	bgp_best_selection(bgp, dest, &bgp->maxpaths[afi][safi], &old_and_new,
			   afi, safi);
	old_select = old_and_new.old;
	new_select = old_and_new.new;

	/*
	 * If the best path hasn't changed - see if something needs to be
	 * updated
	 */
	if (old_select && old_select == new_select
	    && old_select->type == ZEBRA_ROUTE_BGP
	    && old_select->sub_type == BGP_ROUTE_IMPORTED
	    && !CHECK_FLAG(dest->flags, BGP_NODE_USER_CLEAR)
	    && !CHECK_FLAG(old_select->flags, BGP_PATH_ATTR_CHANGED)
	    && !bgp_addpath_is_addpath_used(&bgp->tx_addpath, afi, safi)) {
		if (bgp_zebra_has_route_changed(old_select)) {
			struct ipaddr vtep_ip = {};

			bgp_evpn_mh_get_vtep_ip(old_select->attr, &vtep_ip);
			bgp_evpn_es_vtep_add(bgp, es, vtep_ip, true /*esr*/,
					     old_select->attr->df_alg, old_select->attr->df_pref,
					     &zret);
		}
		UNSET_FLAG(old_select->flags, BGP_PATH_MULTIPATH_CHG);
		bgp_zebra_clear_route_change_flags(dest);
		return ret;
	}

	/* If the user did a "clear" this flag will be set */
	UNSET_FLAG(dest->flags, BGP_NODE_USER_CLEAR);

	/* bestpath has changed; update relevant fields and install or uninstall
	 * into the zebra RIB.
	 */
	if (old_select || new_select)
		bgp_bump_version(dest);

	if (old_select)
		bgp_path_info_unset_flag(dest, old_select, BGP_PATH_SELECTED);
	if (new_select) {
		bgp_path_info_set_flag(dest, new_select, BGP_PATH_SELECTED);
		bgp_path_info_unset_flag(dest, new_select,
					 BGP_PATH_ATTR_CHANGED);
		UNSET_FLAG(new_select->flags, BGP_PATH_MULTIPATH_CHG);
	}

	if (new_select && new_select->type == ZEBRA_ROUTE_BGP
			&& new_select->sub_type == BGP_ROUTE_IMPORTED) {
		struct ipaddr vtep_ip = {};

		bgp_evpn_mh_get_vtep_ip(new_select->attr, &vtep_ip);
		bgp_evpn_es_vtep_add(bgp, es, vtep_ip, true /*esr */, new_select->attr->df_alg,
				     new_select->attr->df_pref, &zret);
	} else {
		if (old_select && old_select->type == ZEBRA_ROUTE_BGP &&
		    old_select->sub_type == BGP_ROUTE_IMPORTED) {
			struct ipaddr vtep_ip = {};
			uint8_t nhfamily = NEXTHOP_FAMILY(old_select->attr->mp_nexthop_len);

			if (nhfamily == AF_INET) {
				SET_IPADDR_V4(&vtep_ip);
				IPV4_ADDR_COPY(&vtep_ip.ipaddr_v4,
					       &old_select->attr->mp_nexthop_global_in);
			} else if (nhfamily == AF_INET6) {
				SET_IPADDR_V6(&vtep_ip);
				IPV6_ADDR_COPY(&vtep_ip.ipaddr_v6,
					       &old_select->attr->mp_nexthop_global);
			} else {
				/* default condition */
				SET_IPADDR_V4(&vtep_ip);
				IPV4_ADDR_COPY(&vtep_ip.ipaddr_v4, &old_select->attr->nexthop);
			}
			bgp_evpn_es_vtep_del(bgp, es, vtep_ip, true /*esr*/);
		}
	}

	/* Clear any route change flags. */
	bgp_zebra_clear_route_change_flags(dest);

	/* Reap old select bgp_path_info, if it has been removed */
	if (old_select && CHECK_FLAG(old_select->flags, BGP_PATH_REMOVED))
		bgp_path_info_reap(dest, old_select);

	return ret;
}

/* 【函数】bgp_evpn_es_route_install —— 把父路由(parent_pi)对应的 Type-1/Type-4
 * 路由安装进 ES 路由表(es->route_table)。
 * 做法：先 bgp_node_get 取节点；若已存在同父(vrfleak->parent == parent_pi)的路径，
 * 属性未变则直接返回；属性变了则 intern 新属性、必要时 restore 被删路径、标记
 * IGP_CHANGED、替换属性。若不存在则 info_make 建 IMPORTED 路径、置 VALID，并建立
 * vrfleak 关联到 parent_pi（持锁）。最后调用 select_install 做选路。
 * 注意：这里没有 RD。 */
/* Install Type-1/Type-4 route entry in the per-ES routing table */
static int bgp_evpn_es_route_install(struct bgp *bgp,
		struct bgp_evpn_es *es, struct prefix_evpn *p,
		struct bgp_path_info *parent_pi)
{
	int ret = 0;
	struct bgp_dest *dest = NULL;
	struct bgp_path_info *pi = NULL;
	struct attr *attr_new = NULL;

	/* Create (or fetch) route within the VNI.
	 * NOTE: There is no RD here.
	 */
	dest = bgp_node_get(es->route_table, (struct prefix *)p);

	/* Check if route entry is already present. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
		if (pi->extra && pi->extra->vrfleak &&
		    (struct bgp_path_info *)pi->extra->vrfleak->parent ==
			    parent_pi)
			break;

	if (!pi) {
		/* Add (or update) attribute to hash. */
		attr_new = bgp_attr_intern(parent_pi->attr);

		/* Create new route with its attribute. */
		pi = info_make(parent_pi->type, BGP_ROUTE_IMPORTED, 0,
			       parent_pi->peer, attr_new, dest);
		SET_FLAG(pi->flags, BGP_PATH_VALID);
		bgp_path_info_extra_get(pi);
		if (!pi->extra->vrfleak)
			pi->extra->vrfleak =
				XCALLOC(MTYPE_BGP_ROUTE_EXTRA_VRFLEAK,
					sizeof(struct bgp_path_info_extra_vrfleak));
		pi->extra->vrfleak->parent = bgp_path_info_lock(parent_pi);
		bgp_dest_lock_node((struct bgp_dest *)parent_pi->net);
		bgp_path_info_add(dest, pi);
	} else {
		if (!CHECK_FLAG(pi->flags, BGP_PATH_REMOVED) &&
		    attrhash_cmp(pi->attr, parent_pi->attr)) {
			bgp_dest_unlock_node(dest);
			return 0;
		}
		/* The attribute has changed. */
		/* Add (or update) attribute to hash. */
		attr_new = bgp_attr_intern(parent_pi->attr);

		/* Restore route, if needed. */
		if (CHECK_FLAG(pi->flags, BGP_PATH_REMOVED))
			bgp_path_info_restore(dest, pi);

		/* Mark if nexthop has changed. */
		if (!IPV4_ADDR_SAME(&pi->attr->nexthop, &attr_new->nexthop))
			SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);

		/* Unintern existing, set to new. */
		bgp_attr_unintern(&pi->attr);
		pi->attr = attr_new;
		pi->uptime = monotime(NULL);
	}

	/* Perform route selection and update zebra, if required. */
	ret = bgp_evpn_es_route_select_install(bgp, es, dest, pi);

	bgp_dest_unlock_node(dest);

	return ret;
}

/* 【函数】bgp_evpn_es_route_uninstall —— 从 ES 路由表卸载父路由 parent_pi 对应的
 * Type-1/Type-4 路由：找到匹配 vrfleak->parent 的路径，mark_for_delete，再触发
 * select_install 重选。找不到或表为空则直接返回 0。 */
/* Uninstall Type-1/Type-4 route entry from the ES routing table */
static int bgp_evpn_es_route_uninstall(struct bgp *bgp, struct bgp_evpn_es *es,
		struct prefix_evpn *p, struct bgp_path_info *parent_pi)
{
	int ret;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;

	if (!es->route_table)
		return 0;

	/* Locate route within the ESI.
	 * NOTE: There is no RD here.
	 */
	dest = bgp_node_lookup(es->route_table, (struct prefix *)p);
	if (!dest)
		return 0;

	/* Find matching route entry. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
		if (pi->extra && pi->extra->vrfleak &&
		    (struct bgp_path_info *)pi->extra->vrfleak->parent ==
			    parent_pi)
			break;

	if (!pi) {
		bgp_dest_unlock_node(dest);
		return 0;
	}

	/* Mark entry for deletion */
	bgp_path_info_mark_for_delete(dest, pi);

	/* Perform route selection and update zebra, if required. */
	ret = bgp_evpn_es_route_select_install(bgp, es, dest, pi);

	/* Unlock route node. */
	bgp_dest_unlock_node(dest);

	return ret;
}

/* 【函数】bgp_evpn_es_route_install_uninstall —— 对外统一入口：install 非 0 调
 * install，否则调 uninstall；失败时打错误日志并返回非 0。 */
/* Install or uninstall a Type-4 route in the per-ES routing table */
int bgp_evpn_es_route_install_uninstall(struct bgp *bgp, struct bgp_evpn_es *es,
		afi_t afi, safi_t safi, struct prefix_evpn *evp,
		struct bgp_path_info *pi, int install)
{
	int ret = 0;

	if (install)
		ret = bgp_evpn_es_route_install(bgp, es, evp, pi);
	else
		ret = bgp_evpn_es_route_uninstall(bgp, es, evp, pi);

	if (ret) {
		flog_err(
				EC_BGP_EVPN_FAIL,
				"%u: Failed to %s EVPN %s route in ESI %s",
				bgp->vrf_id,
				install ? "install" : "uninstall",
				"ES", es->esi_str);
		return ret;
	}
	return 0;
}

/* 【函数】bgp_evpn_es_route_del_all —— ES 被删除时的整体清理：先把 ES 置为 down、
 * 删除本地 Type-1 EVI 路由，然后遍历 es->route_table 把所有路径标记删除并 reap。 */
/* Delete (and withdraw) local routes for specified ES from global and ES table.
 * Also remove all remote routes from the per ES table. Invoked when ES
 * is deleted.
 */
static void bgp_evpn_es_route_del_all(struct bgp *bgp, struct bgp_evpn_es *es)
{
	struct bgp_dest *dest;
	struct bgp_path_info *pi, *nextpi;

	/* de-activate the ES */
	bgp_evpn_local_es_down(bgp, es);
	bgp_evpn_local_type1_evi_route_del(bgp, es);

	/* Walk this ES's routing table and delete all routes. */
	for (dest = bgp_table_top(es->route_table); dest;
	     dest = bgp_route_next(dest)) {
		for (pi = bgp_dest_get_bgp_path_info(dest);
		     (pi != NULL) && (nextpi = pi->next, 1); pi = nextpi) {
			bgp_path_info_mark_for_delete(dest, pi);
			dest = bgp_path_info_reap(dest, pi);

			assert(dest);
		}
	}
}

/* 【本章】在“本地以太网段变化”时创建 MH(Type-1/Type-4) 路由的基础接口。
 * 核心是 bgp_evpn_mh_route_update / _delete：它们按路由类型决定把本端路由放进
 * ES 表(ESR、EAD-ES)、VNI 表(EAD-EVI) 还是全局表，并维护属性与标签。 */
/*****************************************************************************
 * Base APIs for creating MH routes (Type-1 or Type-4) on local ethernet
 * segment updates.
 */

/* 【函数】bgp_evpn_mh_route_update —— 创建或更新一条本地 EVPN Type-1/Type-4 路由。
 * 根据目的地不同，可能落在：ES 表(ESR/EAD-ES)、VNI 表(EAD-EVI) 或全局表。
 * 逻辑：
 *   1) 在 dest 上找本地路径(peer_self 且 STATIC) 与远端 IMPORTED 路径；正常情况
 *      不应存在远端路径，若存在说明键冲突，报错返回 -1；
 *   2) 无本地路径则 intern 属性、info_make 建 STATIC 路径；EAD 路由还要给路径
 *      挂上 VNI 标签(bgp_labels)；
 *   3) 有本地路径则比较属性，变了就 intern 新属性、置 ATTR_CHANGED、必要时 restore、
 *      替换属性并刷新 uptime，未变则 route_changed=0；
 *   4) route_changed 时打调试日志/触发 trace；最后通过出参 *ri 返回该路径。
 * vpn 仅对 EAD-EVI 有效(EAD-ES 与 ESR 传 NULL)。返回 0 成功、-1 失败。 */
/* create or update local EVPN type1/type4 route entry.
 *
 * This could be in -
 *     the ES table if ESR/EAD-ES (or)
 *     the VNI table if EAD-EVI (or)
 *     the global table if ESR/EAD-ES/EAD-EVI
 *
 * Note: vpn is applicable only to EAD-EVI routes (NULL for EAD-ES and
 * ESR).
 */
int bgp_evpn_mh_route_update(struct bgp *bgp, struct bgp_evpn_es *es,
			     struct bgpevpn *vpn, afi_t afi, safi_t safi,
			     struct bgp_dest *dest, struct attr *attr,
			     struct bgp_path_info **ri, int *route_changed)
{
	struct bgp_path_info *tmp_pi = NULL;
	struct bgp_path_info *local_pi = NULL;  /* local route entry if any */
	struct bgp_path_info *remote_pi = NULL; /* remote route entry if any */
	struct bgp_labels bgp_labels = {};
	struct attr *attr_new = NULL;
	struct prefix_evpn *evp;

	*ri = NULL;
	evp = (struct prefix_evpn *)bgp_dest_get_prefix(dest);
	*route_changed = 1;

	/* locate the local and remote entries if any */
	for (tmp_pi = bgp_dest_get_bgp_path_info(dest); tmp_pi;
	     tmp_pi = tmp_pi->next) {
		if (tmp_pi->peer == bgp->peer_self
				&& tmp_pi->type == ZEBRA_ROUTE_BGP
				&& tmp_pi->sub_type == BGP_ROUTE_STATIC)
			local_pi = tmp_pi;
		if (tmp_pi->type == ZEBRA_ROUTE_BGP
				&& tmp_pi->sub_type == BGP_ROUTE_IMPORTED
				&& CHECK_FLAG(tmp_pi->flags, BGP_PATH_VALID))
			remote_pi = tmp_pi;
	}

	/* we don't expect to see a remote_pi at this point as
	 * an ES route has {esi, vtep_ip} as the key in the ES-rt-table
	 * in the VNI-rt-table.
	 */
	if (remote_pi) {
		flog_err(
			EC_BGP_ES_INVALID,
			"%u ERROR: local es route for ESI: %s vtep %pIA also learnt from remote",
			bgp->vrf_id, es ? es->esi_str : "Null",
			es ? &es->originator_ip : NULL);
		return -1;
	}

	/* create or update the entry */
	if (!local_pi) {

		/* Add or update attribute to hash */
		attr_new = bgp_attr_intern(attr);

		/* Create new route with its attribute. */
		tmp_pi = info_make(ZEBRA_ROUTE_BGP, BGP_ROUTE_STATIC, 0,
				   bgp->peer_self, attr_new, dest);
		SET_FLAG(tmp_pi->flags, BGP_PATH_VALID);

		if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE) {
			bgp_path_info_extra_get(tmp_pi);
			bgp_labels.num_labels = 1;
			if (vpn)
				vni2label(vpn->vni, &bgp_labels.label[0]);
			if (!bgp_path_info_labels_same(tmp_pi,
						       &bgp_labels.label[0],
						       bgp_labels.num_labels)) {
				bgp_labels_unintern(&tmp_pi->extra->labels);
				tmp_pi->extra->labels =
					bgp_labels_intern(&bgp_labels);
			}
		}

		/* add the newly created path to the route-node */
		bgp_path_info_add(dest, tmp_pi);
	} else {
		tmp_pi = local_pi;
		if (!CHECK_FLAG(tmp_pi->flags, BGP_PATH_REMOVED) && attrhash_cmp(tmp_pi->attr, attr))
			*route_changed = 0;
		else {
			/* The attribute has changed.
			 * Add (or update) attribute to hash.
			 */
			attr_new = bgp_attr_intern(attr);
			bgp_path_info_set_flag(dest, tmp_pi,
					       BGP_PATH_ATTR_CHANGED);

			/* Restore route, if needed. */
			if (CHECK_FLAG(tmp_pi->flags, BGP_PATH_REMOVED))
				bgp_path_info_restore(dest, tmp_pi);

			/* Unintern existing, set to new. */
			bgp_attr_unintern(&tmp_pi->attr);
			tmp_pi->attr = attr_new;
			tmp_pi->uptime = monotime(NULL);
		}
	}

	if (*route_changed) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug(
				"local ES %s vni %u route-type %s nexthop %pI4 updated",
				es ? es->esi_str : "Null", vpn ? vpn->vni : 0,
				evp->prefix.route_type == BGP_EVPN_ES_ROUTE
					? "esr"
					: (vpn ? "ead-evi" : "ead-es"),
				&attr->mp_nexthop_global_in);

		if (es) {
			struct ipaddr vtep_ip = {};

			if (IS_IPADDR_V4(&es->originator_ip)) {
				SET_IPADDR_V4(&vtep_ip);
				vtep_ip.ipaddr_v4 = attr->mp_nexthop_global_in;
			} else if (IS_IPADDR_V6(&es->originator_ip)) {
				SET_IPADDR_V6(&vtep_ip);
				IPV6_ADDR_COPY(&vtep_ip.ipaddr_v6, &attr->mp_nexthop_global);
			}
			frrtrace(4, frr_bgp, evpn_mh_local_ead_es_evi_route_upd, &es->esi,
				 (vpn ? vpn->vni : 0), evp->prefix.route_type, &vtep_ip);
		}
	}

	/* Return back th*e route entry. */
	*ri = tmp_pi;
	return 0;
}

/* 【函数】bgp_evpn_mh_route_delete —— 删除一条本地 ESR(Type-4) 或 EAD(Type-1) 路由。
 * 先按 vpn 是否非空选表(VNI 表用 vpn->prd；否则 ES 表用 es_frag->prd)，在本地表
 * 里定位节点；再到全局表(bgp_evpn_global_node_lookup)删对应条目并 bgp_process 触发
 * 对端撤销；最后删本地表条目录 reap。vpn 仅对 EAD-EVI 有效。 */
/* Delete local EVPN ESR (type-4) and EAD (type-1) route
 *
 * Note: vpn is applicable only to EAD-EVI routes (NULL for EAD-ES and
 * ESR).
 */
static int bgp_evpn_mh_route_delete(struct bgp *bgp, struct bgp_evpn_es *es,
				    struct bgpevpn *vpn,
				    struct bgp_evpn_es_frag *es_frag,
				    struct prefix_evpn *p)
{
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct bgp_path_info *pi;
	struct bgp_dest *dest = NULL;	     /* dest in esi table */
	struct bgp_dest *global_dest = NULL; /* dest in global table */
	struct bgp_table *rt_table;
	struct prefix_rd *prd;

	if (vpn) {
		rt_table = vpn->ip_table;
		prd = &vpn->prd;
	} else {
		rt_table = es->route_table;
		prd = &es_frag->prd;
	}

	/* First, locate the route node within the ESI or VNI.
	 * If it doesn't exist, there is nothing to do.
	 * Note: there is no RD here.
	 */
	dest = bgp_node_lookup(rt_table, (struct prefix *)p);
	if (!dest)
		return 0;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug(
			"local ES %s vni %u route-type %s nexthop %pIA delete",
			es->esi_str, vpn ? vpn->vni : 0,
			p->prefix.route_type == BGP_EVPN_ES_ROUTE
				? "esr"
				: (vpn ? "ead-evi" : "ead-es"),
			&es->originator_ip);

	frrtrace(4, frr_bgp, evpn_mh_local_ead_es_evi_route_del, &es->esi,
		 (vpn ? vpn->vni : 0), p->prefix.route_type, &es->originator_ip);
	/* Next, locate route node in the global EVPN routing table.
	 * Note that this table is a 2-level tree (RD-level + Prefix-level)
	 */
	global_dest = bgp_evpn_global_node_lookup(bgp->rib[afi][safi], safi, p,
						  prd, NULL);
	if (global_dest) {

		/* Delete route entry in the global EVPN table. */
		pi = delete_evpn_route_entry(bgp, afi, safi, global_dest, NULL, 0);

		/* Schedule for processing - withdraws to peers happen from
		 * this table.
		 */
		if (pi)
			bgp_process(bgp, global_dest, pi, afi, safi);
		bgp_dest_unlock_node(global_dest);
	}

	/*
	 * Delete route entry in the ESI or VNI routing table.
	 * This can just be removed.
	 */
	pi = delete_evpn_route_entry(bgp, afi, safi, dest, NULL, 0);
	if (pi)
		dest = bgp_path_info_reap(dest, pi);

	assert(dest);
	bgp_dest_unlock_node(dest);

	return 0;
}

/* 【函数】delete_global_ead_evi_routes —— VNI 的 RD 变化时调用：删除该 VNI 在全局
 * 表里的所有 EAD/EVI 本地路由(这些是发给对端的，删除会被调度为 withdraw)。
 * 做法：按 vpn->prd 找到全局表的 RD 节点，遍历其下每条路由，仅处理 Type-1
 * (BGP_EVPN_AD_ROUTE) 的条目，逐条 delete 并 bgp_process。 */
/*
 * This function is called when the VNI RD changes.
 * Delete all EAD/EVI local routes for this VNI from the global routing table.
 * These routes are scheduled for withdraw from peers.
 */
int delete_global_ead_evi_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rdrn, *bd;
	struct bgp_table *table;
	struct bgp_path_info *pi;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* Find the RD node for the VNI in the global table */
	rdrn = bgp_node_lookup(bgp->rib[afi][safi], (struct prefix *)&vpn->prd);
	if (rdrn && bgp_dest_has_bgp_path_info_data(rdrn)) {
		table = bgp_dest_get_bgp_table_info(rdrn);

		/*
		 * Iterate over all the routes in this table and delete EAD/EVI
		 * routes
		 */
		for (bd = bgp_table_top(table); bd; bd = bgp_route_next(bd)) {
			struct prefix_evpn *evp = (struct prefix_evpn *)&bd->rn->p;

			if (evp->prefix.route_type != BGP_EVPN_AD_ROUTE)
				continue;

			pi = delete_evpn_route_entry(bgp, afi, safi, bd, NULL, 0);
			if (pi)
				bgp_process(bgp, bd, pi, afi, safi);
		}
	}

	/* Unlock RD node. */
	if (rdrn)
		bgp_dest_unlock_node(rdrn);

	return 0;
}

/* 【本章】以太网段(Type-4) 路由(ESR)
 * ESR 用于 DF 选举。当前实现**不支持** RFC7432 描述的 service-carving，默认采用
 * 基于偏好值(pref)的 DF 选举，参考 draft-ietf-bess-evpn-pref-df。
 * 本章函数：构建 ESR 的扩展团体、创建/更新/删除本地 ESR、处理远端 ESR 及其导入。 */
/*****************************************************************************
 * Ethernet Segment (Type-4) Routes
 * ESRs are used for DF election. Currently service-carving described in
 * RFC 7432 is NOT supported. Instead preference based DF election is
 * used by default.
 * Reference: draft-ietf-bess-evpn-pref-df
 */
/* 【函数】bgp_evpn_type4_route_extcomm_build —— 为本地 ESR(Type-4) 构建扩展团体：
 *   1) Encap 团体(BGP_ENCAP_TYPE_VXLAN)；
 *   2) ES-Import RT 团体(由 ESI 派生的 system MAC 生成，用于 ES 路由的导入匹配)；
 *   3) DF 选举团体(encode_df_elect_extcomm，携带 es->df_pref)。
 * 三者依次 merge 到 attr 的 ecommunity 上。 */
/* Build extended community for EVPN ES (type-4) route */
static void bgp_evpn_type4_route_extcomm_build(struct bgp_evpn_es *es,
		struct attr *attr)
{
	struct ecommunity ecom_encap;
	struct ecommunity ecom_es_rt;
	struct ecommunity ecom_df;
	struct ecommunity_val eval;
	struct ecommunity_val eval_es_rt;
	struct ecommunity_val eval_df;
	bgp_encap_types tnl_type;
	struct ethaddr mac;

	/* Encap */
	tnl_type = BGP_ENCAP_TYPE_VXLAN;
	memset(&ecom_encap, 0, sizeof(ecom_encap));
	encode_encap_extcomm(tnl_type, &eval);
	ecom_encap.size = 1;
	ecom_encap.unit_size = ECOMMUNITY_SIZE;
	ecom_encap.val = (uint8_t *)eval.val;
	bgp_attr_set_ecommunity(attr, ecommunity_dup(&ecom_encap));

	/* ES import RT */
	memset(&mac, 0, sizeof(mac));
	memset(&ecom_es_rt, 0, sizeof(ecom_es_rt));
	es_get_system_mac(&es->esi, &mac);
	encode_es_rt_extcomm(&eval_es_rt, &mac);
	ecom_es_rt.size = 1;
	ecom_es_rt.unit_size = ECOMMUNITY_SIZE;
	ecom_es_rt.val = (uint8_t *)eval_es_rt.val;
	bgp_attr_set_ecommunity(
		attr,
		ecommunity_merge(bgp_attr_get_ecommunity(attr), &ecom_es_rt));

	/* DF election extended community */
	memset(&ecom_df, 0, sizeof(ecom_df));
	encode_df_elect_extcomm(&eval_df, es->df_pref);
	ecom_df.size = 1;
	ecom_df.val = (uint8_t *)eval_df.val;
	bgp_attr_set_ecommunity(
		attr,
		ecommunity_merge(bgp_attr_get_ecommunity(attr), &ecom_df));
}

/* 【函数】bgp_evpn_type4_route_update —— 创建/更新本地 Type-4(ESR) 路由。
 * 步骤：设默认属性(IGP origin)、按 es->originator_ip 填下一跳、构建扩展团体；
 * 在 ES 表里取节点并调 bgp_evpn_mh_route_update 写入；再 select_install 让本地路由
 * 胜出(仅用于正确置标志)；若 route_changed，则把该路由也写进全局表(带 es_base_frag
 * 的 RD)并 bgp_process 出去；最后 unintern 临时 aspath。 */
/* Create or update local type-4 route */
static int bgp_evpn_type4_route_update(struct bgp *bgp,
		struct bgp_evpn_es *es, struct prefix_evpn *p)
{
	int ret = 0;
	int route_changed = 0;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct attr attr;
	struct attr *attr_new = NULL;
	struct bgp_dest *dest = NULL;
	struct bgp_path_info *pi = NULL;

	memset(&attr, 0, sizeof(attr));

	/* Build path-attribute for this route. */
	bgp_attr_default_set(&attr, bgp, BGP_ORIGIN_IGP);
	/* v4 vtep use v4 attr.nexthop, v6 vtep use mp_nexthop_global */
	bgp_evpn_vtep_ip_to_attr_nh(&es->originator_ip, &attr);

	/* Set up extended community. */
	bgp_evpn_type4_route_extcomm_build(es, &attr);

	/* First, create (or fetch) route node within the ESI. */
	/* NOTE: There is no RD here. */
	dest = bgp_node_get(es->route_table, (struct prefix *)p);

	/* Create or update route entry. */
	ret = bgp_evpn_mh_route_update(bgp, es, NULL, afi, safi, dest, &attr,
				       &pi, &route_changed);
	if (ret != 0)
		flog_err(
			EC_BGP_ES_INVALID,
			"%u ERROR: Failed to updated ES route ESI: %s VTEP %pIA",
			bgp->vrf_id, es->esi_str, &es->originator_ip);

	assert(pi);
	attr_new = pi->attr;

	/* Perform route selection;
	 * this is just to set the flags correctly
	 * as local route in the ES always wins.
	 */
	bgp_evpn_es_route_select_install(bgp, es, dest, pi);
	bgp_dest_unlock_node(dest);

	/* If this is a new route or some attribute has changed, export the
	 * route to the global table. The route will be advertised to peers
	 * from there. Note that this table is a 2-level tree (RD-level +
	 * Prefix-level) similar to L3VPN routes.
	 */
	if (route_changed) {
		struct bgp_path_info *global_pi;

		dest = bgp_evpn_global_node_get(bgp->rib[afi][safi], afi, safi,
						p, &es->es_base_frag->prd,
						NULL);
		bgp_evpn_mh_route_update(bgp, es, NULL, afi, safi, dest,
					 attr_new, &global_pi, &route_changed);

		/* Schedule for processing and unlock node. */
		bgp_process(bgp, dest, global_pi, afi, safi);
		bgp_dest_unlock_node(dest);
	}

	/* Unintern temporary. */
	aspath_unintern(&attr.aspath);
	return 0;
}

/* 【函数】bgp_evpn_type4_route_delete —— 删除本地 Type-4 路由；若无 es_base_frag
 * 返回 -1，否则走 bgp_evpn_mh_route_delete(vpn 传 NULL)。 */
/* Delete local type-4 route */
static int bgp_evpn_type4_route_delete(struct bgp *bgp,
		struct bgp_evpn_es *es, struct prefix_evpn *p)
{
	if (!es->es_base_frag)
		return -1;

	return bgp_evpn_mh_route_delete(bgp, es, NULL /* l2vni */,
					es->es_base_frag, p);
}

/* 【函数】bgp_evpn_type4_route_process —— 处理收到的远端 EVPN Type-4 路由(报文解析
 * 入口，由 bgp_evpn.c 调用)。NLRI 布局：RD(8) + ESI(10) + ip-len(1) + ip(4 或 16)。
 * 校验报文长度(23 或 35 字节)与 IP 长度；逐段取 RD、ESI、IP；build_evpn_type4_prefix
 * 组前缀；attr 非空则 bgp_update，否则 bgp_withdraw。返回 0 成功、-1 校验失败。 */
/* Process remote/received EVPN type-4 route (advertise or withdraw)  */
int bgp_evpn_type4_route_process(struct peer *peer, afi_t afi, safi_t safi,
		struct attr *attr, uint8_t *pfx, int psize,
		uint32_t addpath_id)
{
	esi_t esi;
	uint8_t ipaddr_len;
	struct ipaddr vtep_ip = {};
	struct prefix_rd prd;
	struct prefix_evpn p = {};

	/* Type-4 route should be either 23 or 35 bytes
	 *  RD (8), ESI (10), ip-len (1), ip (4 or 16)
	 */
	if (psize != BGP_EVPN_TYPE4_V4_PSIZE &&
			psize != BGP_EVPN_TYPE4_V6_PSIZE) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
				"%u:%s - Rx EVPN Type-4 NLRI with invalid length %d",
				peer->bgp->vrf_id, peer->host, psize);
		return -1;
	}

	/* Make prefix_rd */
	prd.family = AF_UNSPEC;
	prd.prefixlen = 64;
	memcpy(&prd.val, pfx, RD_BYTES);
	pfx += RD_BYTES;

	/* get the ESI */
	memcpy(&esi, pfx, ESI_BYTES);
	pfx += ESI_BYTES;

	/* Get the IP. */
	ipaddr_len = *pfx++;

	/* Validate */
	if (psize != 19 + (ipaddr_len / 8)) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-4 NLRI with invalid IP address length %d",
			 peer->bgp->vrf_id, peer->host, ipaddr_len);
		return -1;
	}

	if (ipaddr_len == IPV4_MAX_BITLEN) {
		SET_IPADDR_V4(&vtep_ip);
		memcpy(&vtep_ip.ipaddr_v4, pfx, IPV4_MAX_BYTELEN);
	} else if (ipaddr_len == IPV6_MAX_BITLEN) {
		SET_IPADDR_V6(&vtep_ip);
		IPV6_ADDR_COPY(&vtep_ip.ipaddr_v6, (struct in6_addr *)pfx);
	} else {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-4 NLRI with unsupported IP address length %d",
			 peer->bgp->vrf_id, peer->host, ipaddr_len);
		return -1;
	}

	build_evpn_type4_prefix(&p, &esi, vtep_ip);
	/* Process the route. */
	if (attr) {
		bgp_update(peer, (struct prefix *)&p, addpath_id, attr, afi,
			   safi, ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd, NULL,
			   0, 0, NULL);
	} else {
		bgp_withdraw(peer, (struct prefix *)&p, addpath_id, afi, safi,
			     ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd, NULL, 0);
	}
	return 0;
}

/* 【函数】bgp_evpn_type4_prefix_match —— 判断某前缀是否为“本 ES 的 Type-4”：
 * 路由类型为 ES_ROUTE 且其 ESI 与 es->esi 相同。 */
/* Check if a prefix belongs to the local ES */
static bool bgp_evpn_type4_prefix_match(struct prefix_evpn *p,
		struct bgp_evpn_es *es)
{
	return (p->prefix.route_type == BGP_EVPN_ES_ROUTE) &&
		!memcmp(&p->prefix.es_addr.esi, &es->esi, sizeof(esi_t));
}

/* 【函数】bgp_evpn_type4_remote_routes_import —— 本地 ES 新增/删除时，扫描整个全局
 * EVPN 表，把“属于本 ES”的远端 Type-4 路由导入(install) 或移除(uninstall) 到 ES 表。
 * 只考虑 VALID 且为 BGP_ROUTE_NORMAL 的远端路径，且通过 type4_prefix_match 过滤；
 * 逐条调用 es_route_install / uninstall，失败打错误日志。 */
/* Import remote ESRs on local ethernet segment add  */
static int bgp_evpn_type4_remote_routes_import(struct bgp *bgp,
		struct bgp_evpn_es *es, bool install)
{
	int ret;
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rd_dest, *dest;
	struct bgp_table *table;
	struct bgp_path_info *pi;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* Walk entire global routing table and evaluate routes which could be
	 * imported into this Ethernet Segment.
	 */
	for (rd_dest = bgp_table_top(bgp->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (!table)
			continue;

		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			struct prefix_evpn *evp =
				(struct prefix_evpn *)bgp_dest_get_prefix(dest);

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				/*
				 * Consider "valid" remote routes applicable for
				 * this ES.
				 */
				if (!(CHECK_FLAG(pi->flags, BGP_PATH_VALID)
					&& pi->type == ZEBRA_ROUTE_BGP
					&& pi->sub_type == BGP_ROUTE_NORMAL))
					continue;

				if (!bgp_evpn_type4_prefix_match(evp, es))
					continue;

				if (install)
					ret = bgp_evpn_es_route_install(
							bgp, es, evp, pi);
				else
					ret = bgp_evpn_es_route_uninstall(
							bgp, es, evp, pi);

				if (ret) {
					flog_err(
						EC_BGP_EVPN_FAIL,
						"Failed to %s EVPN %pFX route in ESI %s",
						install ? "install"
							: "uninstall",
						evp, es->esi_str);

					bgp_dest_unlock_node(rd_dest);
					bgp_dest_unlock_node(dest);
					return ret;
				}
			}
		}
	}
	return 0;
}

/* 【本章】以太网自动发现(EAD / Type-1) 路由处理。
 * EAD 有两种：
 *   1) EAD-per-ES  —— 键为 {ESI, ET=0xffffffff}，ES 级，携带 ESI Label(水平分割)；
 *   2) EAD-per-EVI —— 键为 {ESI, ET=0}，EVI 级。
 * 下面依次是：扩展团体构建、Type-1 本地路由的创建/更新/删除、按 VNI 批量更新、
 * 以及收到远端 Type-1 报文的解析入口。 */
/*****************************************************************************
 * Ethernet Auto Discovery (EAD/Type-1) route handling
 * There are two types of EAD routes -
 * 1. EAD-per-ES - Key: {ESI, ET=0xffffffff}
 * 2. EAD-per-EVI - Key: {ESI, ET=0}
 */

/* 【函数】bgp_evpn_type1_es_route_extcomm_build —— 为 EAD-per-ES 路由构建扩展团体：
 *   1) Encap 团体(VXLAN)；
 *   2) ESI Label 团体(encode_esi_label_extcomm，single_active=false)——用于水平分割；
 *   3) 出口 RT：若有全局 ead_es_export_rtl 则用它；否则遍历本分片内所有本地 ES-EVI，
 *      把各 L2-VNI 的 export_rtl 都 merge 进来(这样对端多个 VNI 都能导入这条 ES 路由)。 */
/* Extended communities associated with EAD-per-ES */
static void
bgp_evpn_type1_es_route_extcomm_build(struct bgp_evpn_es_frag *es_frag,
				      struct attr *attr)
{
	struct ecommunity ecom_encap;
	struct ecommunity ecom_esi_label;
	struct ecommunity_val eval;
	struct ecommunity_val eval_esi_label;
	bgp_encap_types tnl_type;
	struct listnode *evi_node, *rt_node;
	struct ecommunity *ecom;
	struct bgp_evpn_es_evi *es_evi;

	/* Encap */
	tnl_type = BGP_ENCAP_TYPE_VXLAN;
	memset(&ecom_encap, 0, sizeof(ecom_encap));
	encode_encap_extcomm(tnl_type, &eval);
	ecom_encap.size = 1;
	ecom_encap.unit_size = ECOMMUNITY_SIZE;
	ecom_encap.val = (uint8_t *)eval.val;
	bgp_attr_set_ecommunity(attr, ecommunity_dup(&ecom_encap));

	/* ESI label */
	encode_esi_label_extcomm(&eval_esi_label,
			false /*single_active*/);
	ecom_esi_label.size = 1;
	ecom_esi_label.unit_size = ECOMMUNITY_SIZE;
	ecom_esi_label.val = (uint8_t *)eval_esi_label.val;
	bgp_attr_set_ecommunity(attr,
				ecommunity_merge(bgp_attr_get_ecommunity(attr),
						 &ecom_esi_label));

	/* Add export RTs for all L2-VNIs associated with this ES */
	/* XXX - suppress EAD-ES advertisement if there are no EVIs associated
	 * with it.
	 */
	if (listcount(bgp_mh_info->ead_es_export_rtl)) {
		for (ALL_LIST_ELEMENTS_RO(bgp_mh_info->ead_es_export_rtl,
					  rt_node, ecom))
			bgp_attr_set_ecommunity(
				attr, ecommunity_merge(attr->ecommunity, ecom));
	} else {
		for (ALL_LIST_ELEMENTS_RO(es_frag->es_evi_frag_list, evi_node,
					  es_evi)) {
			if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
				continue;
			for (ALL_LIST_ELEMENTS_RO(es_evi->vpn->export_rtl,
						  rt_node, ecom))
				bgp_attr_set_ecommunity(
					attr, ecommunity_merge(attr->ecommunity,
							       ecom));
		}
	}
}

/* 【函数】bgp_evpn_type1_evi_route_extcomm_build —— 为 EAD-per-EVI 路由构建扩展团体：
 *   Encap 团体(VXLAN) + 该 L2-VNI 的 export RT 列表。 */
/* Extended communities associated with EAD-per-EVI */
static void bgp_evpn_type1_evi_route_extcomm_build(struct bgp_evpn_es *es,
		struct bgpevpn *vpn, struct attr *attr)
{
	struct ecommunity ecom_encap;
	struct ecommunity_val eval;
	bgp_encap_types tnl_type;
	struct listnode *rt_node;
	struct ecommunity *ecom;

	/* Encap */
	tnl_type = BGP_ENCAP_TYPE_VXLAN;
	memset(&ecom_encap, 0, sizeof(ecom_encap));
	encode_encap_extcomm(tnl_type, &eval);
	ecom_encap.size = 1;
	ecom_encap.unit_size = ECOMMUNITY_SIZE;
	ecom_encap.val = (uint8_t *)eval.val;
	bgp_attr_set_ecommunity(attr, ecommunity_dup(&ecom_encap));

	/* Add export RTs for the L2-VNI */
	for (ALL_LIST_ELEMENTS_RO(vpn->export_rtl, rt_node, ecom))
		bgp_attr_set_ecommunity(
			attr,
			ecommunity_merge(bgp_attr_get_ecommunity(attr), ecom));
}

/* 【函数】bgp_evpn_type1_route_update —— 创建/更新一条本地 EAD(Type-1) 路由。
 * vpn 非空表示 EAD-EVI，为 NULL 表示 EAD-ES。
 * EAD-EVI 分支：给属性挂 VNI 标签(vni2label)，构建 EVI 扩展团体，路由节点建在
 *   vpn->ip_table(无 RD)，全局表用 vpn->prd。
 * EAD-ES 分支：标签为 0，构建 ES 扩展团体，路由节点建在 es->route_table，全局表用
 *   es_frag->prd。
 * 之后 evpn_route_select_install 让本地路由胜出；route_changed 时把路由写进全局表
 * 并 bgp_process 通告；最后 unintern 临时 aspath。 */
/* Update EVPN EAD (type-1) route -
 * vpn - valid for EAD-EVI routes and NULL for EAD-ES routes
 */
static int bgp_evpn_type1_route_update(struct bgp *bgp, struct bgp_evpn_es *es,
				       struct bgpevpn *vpn,
				       struct bgp_evpn_es_frag *es_frag,
				       struct prefix_evpn *p)
{
	int ret = 0;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct attr attr;
	struct attr *attr_new = NULL;
	struct bgp_dest *dest = NULL;
	struct bgp_path_info *pi = NULL;
	int route_changed = 0;
	struct prefix_rd *global_rd;

	memset(&attr, 0, sizeof(attr));

	/* Build path-attribute for this route. */
	bgp_attr_default_set(&attr, bgp, BGP_ORIGIN_IGP);
	bgp_evpn_vtep_ip_to_attr_nh(&es->originator_ip, &attr);


	if (vpn) {
		/* EAD-EVI route update */
		/* MPLS label */
		vni2label(vpn->vni, &(attr.label));

		/* Set up extended community */
		bgp_evpn_type1_evi_route_extcomm_build(es, vpn, &attr);

		/* First, create (or fetch) route node within the VNI. */
		dest = bgp_node_get(vpn->ip_table, (struct prefix *)p);

		/* Create or update route entry. */
		ret = bgp_evpn_mh_route_update(bgp, es, vpn, afi, safi, dest,
					       &attr, &pi, &route_changed);
		if (ret != 0)
			flog_err(
				EC_BGP_ES_INVALID,
				"%u Failed to update EAD-EVI route ESI: %s VNI %u VTEP %pIA",
				bgp->vrf_id, es->esi_str, vpn->vni,
				&es->originator_ip);
		global_rd = &vpn->prd;
	} else {
		/* EAD-ES route update */
		/* MPLS label is 0 for EAD-ES route */

		/* SA warning - es_frag is always non-NULL when vpn is NULL */
		assert(es_frag);

		/* Set up extended community */
		bgp_evpn_type1_es_route_extcomm_build(es_frag, &attr);

		/* First, create (or fetch) route node within the ES. */
		/* NOTE: There is no RD here. */
		/* XXX: fragment ID must be included as a part of the prefix. */
		dest = bgp_node_get(es->route_table, (struct prefix *)p);

		/* Create or update route entry. */
		ret = bgp_evpn_mh_route_update(bgp, es, vpn, afi, safi, dest,
					       &attr, &pi, &route_changed);
		if (ret != 0) {
			flog_err(
				EC_BGP_ES_INVALID,
				"%u ERROR: Failed to updated EAD-ES route ESI: %s VTEP %pIA",
				bgp->vrf_id, es->esi_str, &es->originator_ip);
		}
		global_rd = &es_frag->prd;
	}


	assert(pi);
	attr_new = pi->attr;

	/* Perform route selection;
	 * this is just to set the flags correctly as local route in
	 * the ES always wins.
	 */
	evpn_route_select_install(bgp, vpn, dest, pi);
	bgp_dest_unlock_node(dest);

	/* If this is a new route or some attribute has changed, export the
	 * route to the global table. The route will be advertised to peers
	 * from there. Note that this table is a 2-level tree (RD-level +
	 * Prefix-level) similar to L3VPN routes.
	 */
	if (route_changed) {
		struct bgp_path_info *global_pi;

		dest = bgp_evpn_global_node_get(bgp->rib[afi][safi], afi, safi,
						p, global_rd, NULL);
		bgp_evpn_mh_route_update(bgp, es, vpn, afi, safi, dest,
					 attr_new, &global_pi, &route_changed);

		/* Schedule for processing and unlock node. */
		bgp_process(bgp, dest, global_pi, afi, safi);
		bgp_dest_unlock_node(dest);
	}

	/* Unintern temporary. */
	aspath_unintern(&attr.aspath);
	return 0;
}

/* 【函数】bgp_evpn_ead_es_route_update —— 为 ES 的每个分片生成/刷新 EAD-per-ES 路由。
 * 做法：组前缀(EAD-per-ES, ET=0xffffffff)，遍历 es_frag_list；对每个非空分片用其
 * rd_id 作为 frag_id，调 type1_route_update(vpn=NULL)。失败打错误日志。 */
/*
 * This function is called when the export RT for a VNI changes.
 * Update all type-1 local routes for this VNI from VNI/ES tables and the global
 * table and advertise these routes to peers.
 */

static void bgp_evpn_ead_es_route_update(struct bgp *bgp,
					 struct bgp_evpn_es *es)
{
	struct listnode *node;
	struct bgp_evpn_es_frag *es_frag;
	struct prefix_evpn p;

	build_evpn_type1_prefix(&p, BGP_EVPN_AD_ES_ETH_TAG, &es->esi,
				es->originator_ip);
	for (ALL_LIST_ELEMENTS_RO(es->es_frag_list, node, es_frag)) {
		if (!listcount(es_frag->es_evi_frag_list))
			continue;

		p.prefix.ead_addr.frag_id = es_frag->rd_id;
		if (bgp_evpn_type1_route_update(bgp, es, NULL, es_frag, &p))
			flog_err(
				EC_BGP_EVPN_ROUTE_CREATE,
				"EAD-ES route creation failure for ESI %s frag %u",
				es->esi_str, es_frag->rd_id);
	}
}

/* 【函数】bgp_evpn_ead_evi_route_update —— 生成/刷新一条 EAD-per-EVI 路由：转调
 * bgp_evpn_type1_route_update(带 vpn)，失败打错误日志。 */
static void bgp_evpn_ead_evi_route_update(struct bgp *bgp,
					  struct bgp_evpn_es *es,
					  struct bgpevpn *vpn,
					  struct prefix_evpn *p)
{
	if (bgp_evpn_type1_route_update(bgp, es, vpn, NULL, p))
		flog_err(EC_BGP_EVPN_ROUTE_CREATE,
			 "EAD-EVI route creation failure for ESI %s VNI %u",
			 es->esi_str, vpn->vni);
}

/* 【函数】update_type1_routes_for_evi —— VNI 配置变化时，遍历该 VNI 上的所有 ES-EVI：
 *   1) 若对应本地 ES 活跃，刷新其 EAD-per-ES；
 *   2) 若 ES 置了 ADV_EVI 位，则组 EAD-per-EVI 前缀并刷新该 EVI 的 Type-1 路由。 */
void update_type1_routes_for_evi(struct bgp *bgp, struct bgpevpn *vpn)
{
	struct prefix_evpn p;
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_evi *es_evi;


	RB_FOREACH (es_evi, bgp_es_evi_rb_head, &vpn->es_evi_rb_tree) {
		es = es_evi->es;

		if (es_evi->vpn != vpn)
			continue;

		/* Update EAD-ES */
		if (bgp_evpn_local_es_is_active(es))
			bgp_evpn_ead_es_route_update(bgp, es);

		/* Update EAD-EVI */
		if (CHECK_FLAG(es->flags, BGP_EVPNES_ADV_EVI)) {
			build_evpn_type1_prefix(&p, BGP_EVPN_AD_EVI_ETH_TAG,
						&es->esi, es->originator_ip);
			bgp_evpn_ead_evi_route_update(bgp, es, vpn, &p);
		}
	}
}

/* 【函数】bgp_evpn_ead_es_route_delete —— 删除本 ES 的所有 EAD-per-ES 路由：组前缀
 * (ET=0xffffffff)，遍历分片用 rd_id 作 frag_id，逐条调 mh_route_delete(vpn=NULL)。 */
/* Delete local Type-1 route */
static void bgp_evpn_ead_es_route_delete(struct bgp *bgp,
					 struct bgp_evpn_es *es)
{
	struct listnode *node;
	struct bgp_evpn_es_frag *es_frag;
	struct prefix_evpn p;

	build_evpn_type1_prefix(&p, BGP_EVPN_AD_ES_ETH_TAG, &es->esi,
				es->originator_ip);
	for (ALL_LIST_ELEMENTS_RO(es->es_frag_list, node, es_frag)) {
		p.prefix.ead_addr.frag_id = es_frag->rd_id;
		bgp_evpn_mh_route_delete(bgp, es, NULL, es_frag, &p);
	}
}

/* 【函数】bgp_evpn_ead_evi_route_delete —— 删除一条 EAD-per-EVI 路由：
 * 转调 bgp_evpn_mh_route_delete(带 vpn)。 */
static int bgp_evpn_ead_evi_route_delete(struct bgp *bgp,
					 struct bgp_evpn_es *es,
					 struct bgpevpn *vpn,
					 struct prefix_evpn *p)
{
	return bgp_evpn_mh_route_delete(bgp, es, vpn, NULL, p);
}

/* 【函数】bgp_evpn_local_type1_evi_route_add —— 为本 ES 的所有本地 EVI 生成 EAD-EVI：
 * 若全局关闭了 EAD-EVI 发送(enable_ead_evi_tx=false) 或本 ES 已置 ADV_EVI 则直接
 * 返回；否则置 ADV_EVI 位、组 EAD-per-EVI 前缀，遍历本地 ES-EVI 逐个刷新。 */
/* Generate EAD-EVI for all VNIs */
static void bgp_evpn_local_type1_evi_route_add(struct bgp *bgp,
		struct bgp_evpn_es *es)
{
	struct listnode *evi_node;
	struct prefix_evpn p;
	struct bgp_evpn_es_evi *es_evi;

	/* EAD-per-EVI routes have been suppressed */
	if (!bgp_mh_info->enable_ead_evi_tx)
		return;

	if (CHECK_FLAG(es->flags, BGP_EVPNES_ADV_EVI))
		/* EAD-EVI route add for this ES is already done */
		return;

	SET_FLAG(es->flags, BGP_EVPNES_ADV_EVI);
	build_evpn_type1_prefix(&p, BGP_EVPN_AD_EVI_ETH_TAG,
			&es->esi, es->originator_ip);

	for (ALL_LIST_ELEMENTS_RO(es->es_evi_list, evi_node, es_evi)) {
		if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
			continue;
		bgp_evpn_ead_evi_route_update(bgp, es, es_evi->vpn, &p);
	}
}

/* 【函数】bgp_evpn_local_type1_evi_route_del —— 撤销本 ES 所有 EVI 的 EAD-EVI：若未置
 * ADV_EVI 直接返回；否则清位、组前缀、遍历本地 ES-EVI 逐条删除并记录错误。 */
/*
 * Withdraw EAD-EVI for all VNIs
 */
static void bgp_evpn_local_type1_evi_route_del(struct bgp *bgp,
		struct bgp_evpn_es *es)
{
	struct listnode *evi_node;
	struct prefix_evpn p;
	struct bgp_evpn_es_evi *es_evi;

	/* Delete and withdraw locally learnt EAD-EVI route */
	if (!CHECK_FLAG(es->flags, BGP_EVPNES_ADV_EVI))
		/* EAD-EVI route has not been advertised for this ES */
		return;

	UNSET_FLAG(es->flags, BGP_EVPNES_ADV_EVI);
	build_evpn_type1_prefix(&p, BGP_EVPN_AD_EVI_ETH_TAG,
			&es->esi, es->originator_ip);
	for (ALL_LIST_ELEMENTS_RO(es->es_evi_list, evi_node, es_evi)) {
		if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
			continue;
		if (bgp_evpn_mh_route_delete(bgp, es, es_evi->vpn, NULL, &p))
			flog_err(EC_BGP_EVPN_ROUTE_CREATE,
					"%u: Type4 route creation failure for ESI %s",
					bgp->vrf_id, es->esi_str);
	}
}

/* 【函数】bgp_evpn_type1_route_process —— 处理收到的远端 EVPN Type-1 路由(报文解析入口)。
 * NLRI 布局：RD(8) + ESI(10) + Ethernet Tag(4) + Label(3)。校验长度(BGP_EVPN_TYPE1_PSIZE)。
 * 注意：全局表里 EAD 前缀**不含下一跳**，故这里把 VTEP IP 显式置为 0.0.0.0(INADDR_ANY)，
 * 真正的下一跳来自属性。最后 bgp_update / bgp_withdraw。返回 0 成功、-1 校验失败。 */
/*
 * Process received EVPN type-1 route (advertise or withdraw).
 */
int bgp_evpn_type1_route_process(struct peer *peer, afi_t afi, safi_t safi,
		struct attr *attr, uint8_t *pfx, int psize,
		uint32_t addpath_id)
{
	struct prefix_rd prd;
	esi_t esi;
	uint32_t eth_tag;
	mpls_label_t label[BGP_MAX_LABELS] = {};
	struct ipaddr vtep_ip = {};
	struct prefix_evpn p;

	if (psize != BGP_EVPN_TYPE1_PSIZE) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
				"%u:%s - Rx EVPN Type-1 NLRI with invalid length %d",
				peer->bgp->vrf_id, peer->host, psize);
		return -1;
	}

	/* Make prefix_rd */
	prd.family = AF_UNSPEC;
	prd.prefixlen = 64;
	memcpy(&prd.val, pfx, RD_BYTES);
	pfx += RD_BYTES;

	/* get the ESI */
	memcpy(&esi, pfx, ESI_BYTES);
	pfx += ESI_BYTES;

	/* Copy Ethernet Tag */
	memcpy(&eth_tag, pfx, EVPN_ETH_TAG_BYTES);
	eth_tag = ntohl(eth_tag);
	pfx += EVPN_ETH_TAG_BYTES;

	memcpy(&label[0], pfx, BGP_LABEL_BYTES);

	/* EAD route prefix doesn't include the nexthop in the global
	 * table
	 */
	vtep_ip.ipa_type = IPADDR_V4;
	vtep_ip.ipaddr_v4.s_addr = INADDR_ANY;
	build_evpn_type1_prefix(&p, eth_tag, &esi, vtep_ip);
	/* Process the route. */
	if (attr) {
		bgp_update(peer, (struct prefix *)&p, addpath_id, attr, afi, safi, ZEBRA_ROUTE_BGP,
			   BGP_ROUTE_NORMAL, &prd, &label[0], 1, 0, NULL);
	} else {
		bgp_withdraw(peer, (struct prefix *)&p, addpath_id, afi, safi, ZEBRA_ROUTE_BGP,
			     BGP_ROUTE_NORMAL, &prd, &label[0], 1);
	}
	return 0;
}

/* 【函数】bgp_evpn_mh_config_ead_export_rt —— 配置 EAD-per-ES 使用的全局 export RT：
 *   del 且 ecomcfg 为 NULL : 清空整个列表；
 *   del 且 ecomcfg 非 NULL : 删除列表中与 ecomcfg 匹配的那一项；
 *   非 del                 : 把 ecomcfg 按序加入列表。
 * 之后遍历所有活跃本地 ES，先撤旧 EAD-ES 再重新生成，使新 RT 生效。 */
void bgp_evpn_mh_config_ead_export_rt(struct bgp *bgp,
				      struct ecommunity *ecomcfg, bool del)
{
	struct listnode *node, *nnode, *node_to_del;
	struct ecommunity *ecom;
	struct bgp_evpn_es *es;

	if (del) {
		if (ecomcfg == NULL) {
			/* Reset to default and process all routes. */
			for (ALL_LIST_ELEMENTS(bgp_mh_info->ead_es_export_rtl,
					       node, nnode, ecom)) {
				ecommunity_free(&ecom);
				list_delete_node(bgp_mh_info->ead_es_export_rtl,
						 node);
			}
		}

		/* Delete a specific export RT */
		else {
			node_to_del = NULL;

			for (ALL_LIST_ELEMENTS(bgp_mh_info->ead_es_export_rtl,
					       node, nnode, ecom)) {
				if (ecommunity_match(ecom, ecomcfg)) {
					ecommunity_free(&ecom);
					node_to_del = node;
					break;
				}
			}

			assert(node_to_del);
			list_delete_node(bgp_mh_info->ead_es_export_rtl,
					 node_to_del);
		}
	} else {
		listnode_add_sort(bgp_mh_info->ead_es_export_rtl, ecomcfg);
	}

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("local ES del/re-add EAD route on export RT change");
	/*
	 * walk through all active ESs withdraw the old EAD and
	 * generate a new one
	 */
	RB_FOREACH (es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree) {
		if (!bgp_evpn_is_es_local(es) ||
		    !bgp_evpn_local_es_is_active(es))
			continue;

		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug(
				"local ES %s del/re-add EAD route on export RT change",
				es->esi_str);

		/*
		 * withdraw EAD-ES. XXX - this should technically not be
		 * needed; can be removed after testing
		 */
		bgp_evpn_ead_es_route_delete(bgp, es);

		/* generate EAD-ES */
		bgp_evpn_ead_es_route_update(bgp, es);
	}
}

/*****************************************************************************/
/* 【本章】以太网段管理(Ethernet Segment Management)：ES 对象的生命周期与本地状态机，
 * 包括 ES 的新建/释放、本地/远端标志维护、上下线激活、一致性检查挂起等。 */
/* Ethernet Segment Management
 * 1. Ethernet Segment is a collection of links attached to the same
 *    server (MHD) or switch (MHN)
 * 2. An Ethernet Segment can span multiple PEs and is identified by the
 *    10-byte ES-ID.
 * 3. Local ESs are configured in zebra and sent to BGP
 * 4. Remote ESs are created by BGP when one or more ES-EVIs reference it i.e.
 *    created on first reference and release on last de-reference
 * 5. An ES can be both local and remote. In fact most local ESs are expected
 *    to have an ES peer.
 */

/* 【函数】bgp_evpn_es_vtep_cmp —— 比较两个 es_vtep 的 VTEP IP，供 list 排序用。
 * 本 ES 的远端 VTEP 列表包含两类：已导入其 ESR 的 ES 对端，和在某 VNI 上 EAD-ES 与
 * EAD-per-EVI 都已导入的活跃 VTEP。 */
/* A list of remote VTEPs is maintained for each ES. This list includes -
 * 1. VTEPs for which we have imported the ESR i.e. ES-peers
 * 2. VTEPs that have an "active" ES-EVI VTEP i.e. EAD-per-ES and EAD-per-EVI
 *    have been imported into one or more VNIs
 */
static int bgp_evpn_es_vtep_cmp(void *p1, void *p2)
{
	const struct bgp_evpn_es_vtep *es_vtep1 = p1;
	const struct bgp_evpn_es_vtep *es_vtep2 = p2;

	return ipaddr_cmp(&es_vtep1->vtep_ip, &es_vtep2->vtep_ip);
}

/* 【函数】bgp_evpn_es_vtep_new —— 新建一个 es_vtep：填 IP 与字符串形式，初始化链表
 * 节点并按 IP 排序插入 es->es_vtep_list。 */
static struct bgp_evpn_es_vtep *bgp_evpn_es_vtep_new(struct bgp_evpn_es *es, struct ipaddr vtep_ip)
{
	struct bgp_evpn_es_vtep *es_vtep;

	es_vtep = XCALLOC(MTYPE_BGP_EVPN_ES_VTEP, sizeof(*es_vtep));

	es_vtep->es = es;
	es_vtep->vtep_ip = vtep_ip;
	ipaddr2str(&es_vtep->vtep_ip, es_vtep->vtep_str, sizeof(es_vtep->vtep_str));
	listnode_init(&es_vtep->es_listnode, es_vtep);
	listnode_add_sort(es->es_vtep_list, &es_vtep->es_listnode);

	return es_vtep;
}

/* 【函数】bgp_evpn_es_vtep_free —— 释放 es_vtep：只要有 ESR 引用或 evi_cnt>0 就
 * 不释放(还有引用者)；否则从列表摘除并 free。 */
static void bgp_evpn_es_vtep_free(struct bgp_evpn_es_vtep *es_vtep)
{
	struct bgp_evpn_es *es = es_vtep->es;

	if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR) ||
			es_vtep->evi_cnt)
		/* as long as there is some reference we can't free it */
		return;

	list_delete_node(es->es_vtep_list, &es_vtep->es_listnode);
	XFREE(MTYPE_BGP_EVPN_ES_VTEP, es_vtep);
}

/* 【函数】bgp_evpn_es_vtep_find —— 在 es->es_vtep_list 里按 VTEP IP 查找，找到返回
 * 该 es_vtep，否则 NULL。 */
/* check if VTEP is already part of the list */
static struct bgp_evpn_es_vtep *bgp_evpn_es_vtep_find(struct bgp_evpn_es *es,
		struct ipaddr vtep_ip)
{
	struct listnode *node = NULL;
	struct bgp_evpn_es_vtep *es_vtep;

	for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list, node, es_vtep)) {
		if (ipaddr_is_same(&es_vtep->vtep_ip, &vtep_ip))
			return es_vtep;
	}
	return NULL;
}

/* 【函数】bgp_zebra_send_remote_es_vtep —— 把远端 ES-VTEP 的增/删通知 zebra，供其
 * 编程 NHG。add 时附带标志(是否收到 ESR)与 DF 参数(df_alg/df_pref)。无 zclient 或
 * 该实例不被 zebra 知晓时直接返回成功。 */
/* Send the remote ES to zebra for NHG programming */
static enum zclient_send_status
bgp_zebra_send_remote_es_vtep(struct bgp *bgp, struct bgp_evpn_es_vtep *es_vtep,
			      bool add)
{
	struct bgp_evpn_es *es = es_vtep->es;
	struct stream *s;
	uint32_t flags = 0;

	/* Check socket. */
	if (!bgp_zclient || bgp_zclient->sock < 0) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("%s: No zclient or zclient->sock exists",
				   __func__);
		return ZCLIENT_SEND_SUCCESS;
	}

	/* Don't try to register if Zebra doesn't know of this instance. */
	if (!IS_BGP_INST_KNOWN_TO_ZEBRA(bgp)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("No zebra instance, not installing remote es %s",
					es->esi_str);
		return ZCLIENT_SEND_SUCCESS;
	}

	if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR))
		SET_FLAG(flags, ZAPI_ES_VTEP_FLAG_ESR_RXED);

	s = bgp_zclient->obuf;
	stream_reset(s);

	zclient_create_header(s,
		add ? ZEBRA_REMOTE_ES_VTEP_ADD : ZEBRA_REMOTE_ES_VTEP_DEL,
		bgp->vrf_id);
	stream_put(s, &es->esi, sizeof(esi_t));
	stream_put_ipaddr(s, &es_vtep->vtep_ip);
	if (add) {
		stream_putl(s, flags);
		stream_putc(s, es_vtep->df_alg);
		stream_putw(s, es_vtep->df_pref);
	}

	stream_putw_at(s, 0, stream_get_endp(s));

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("Tx %s Remote ESI %s VTEP %pIA", add ? "ADD" : "DEL", es->esi_str,
			   &es_vtep->vtep_ip);

	frrtrace(3, frr_bgp, evpn_mh_vtep_zsend, add, es, es_vtep);

	return zclient_send_message(bgp_zclient);
}

/* 【函数】bgp_evpn_es_vtep_re_eval_active —— 重评估 es_vtep 的 ACTIVE 状态：
 * 有活跃 EVI 引用(evi_cnt>0) 则置 ACTIVE，否则清位。状态变化(或参数变化)时：通知
 * zebra、更新 ES 的 L3 NHG、并把本 ES 挂入一致性检查队列。返回 zebra 发送状态。 */
static enum zclient_send_status bgp_evpn_es_vtep_re_eval_active(
	struct bgp *bgp, struct bgp_evpn_es_vtep *es_vtep, bool param_change)
{
	bool old_active;
	bool new_active;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;

	old_active = CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE);
	/* currently we need an active EVI reference to use the VTEP as
	 * a nexthop. this may change...
	 */
	if (es_vtep->evi_cnt)
		SET_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE);
	else
		UNSET_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE);

	new_active = CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE);

	if ((old_active != new_active) || (new_active && param_change)) {

		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("es %s vtep %pIA %s df %u/%u", es_vtep->es->esi_str,
				   &es_vtep->vtep_ip, new_active ? "active" : "inactive",
				   es_vtep->df_alg, es_vtep->df_pref);

		/* send remote ES to zebra */
		ret = bgp_zebra_send_remote_es_vtep(bgp, es_vtep, new_active);

		/* The NHG is updated first for efficient failover handling.
		 * Note the NHG can be de-activated while there are bgp
		 * routes referencing it. Zebra is capable of handling that
		 * elegantly by holding the NHG till all routes using it are
		 * removed.
		 */
		bgp_evpn_l3nhg_update_on_vtep_chg(es_vtep->es);
		/* queue up the es for background consistency checks */
		bgp_evpn_es_cons_checks_pend_add(es_vtep->es);
	}

/* 【函数】bgp_evpn_es_vtep_add —— 新增/更新一个 es_vtep：先 find，找不到则 new。
 * esr=true 表示因收到 ESR 而添加：置 ESR 位并更新 df_alg/df_pref(参数变化时置
 * param_change)；esr=false 表示因活跃 EVI 引用而添加：evi_cnt++。最后 re_eval_active
 * 并算出 zret 出参。返回 es_vtep。 */
	return ret;
}

static struct bgp_evpn_es_vtep *
bgp_evpn_es_vtep_add(struct bgp *bgp, struct bgp_evpn_es *es,
		     struct ipaddr vtep_ip, bool esr, uint8_t df_alg,
		     uint16_t df_pref, int *zret)
{
	struct bgp_evpn_es_vtep *es_vtep;
	bool param_change = false;

	es_vtep = bgp_evpn_es_vtep_find(es, vtep_ip);

	if (!es_vtep)
		es_vtep = bgp_evpn_es_vtep_new(es, vtep_ip);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vtep %pIA add %s df %u/%u", es_vtep->es->esi_str,
			   &es_vtep->vtep_ip, esr ? "esr" : "ead", df_alg, df_pref);

	if (esr) {
		SET_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR);
		if ((es_vtep->df_pref != df_pref)
		    || (es_vtep->df_alg != df_alg)) {
			param_change = true;
			es_vtep->df_pref = df_pref;
			es_vtep->df_alg = df_alg;
		}
	} else {
		++es_vtep->evi_cnt;
	}

	*zret = bgp_evpn_es_vtep_re_eval_active(bgp, es_vtep, param_change);

	return es_vtep;
}

static enum zclient_send_status
/* 【函数】bgp_evpn_es_vtep_do_del —— es_vtep 的删除主体：esr=true 清 ESR 位并复位
 * DF 参数；否则 evi_cnt--。随后 re_eval_active 并按引用情况调用 es_vtep_free。返回
 * zebra 状态。 */
bgp_evpn_es_vtep_do_del(struct bgp *bgp, struct bgp_evpn_es_vtep *es_vtep,
			bool esr)
{
	bool param_change = false;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vtep %pIA del %s", es_vtep->es->esi_str, &es_vtep->vtep_ip,
			   esr ? "esr" : "ead");
	if (esr) {
		UNSET_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR);
		if (es_vtep->df_pref || es_vtep->df_alg) {
			param_change = true;
			es_vtep->df_pref = 0;
			es_vtep->df_alg = 0;
		}
	} else {
		if (es_vtep->evi_cnt)
			--es_vtep->evi_cnt;
	}

	ret = bgp_evpn_es_vtep_re_eval_active(bgp, es_vtep, param_change);
	bgp_evpn_es_vtep_free(es_vtep);

/* 【函数】bgp_evpn_es_vtep_del —— 按 VTEP IP 找到 es_vtep 后转 do_del；找不到返回
 * 成功。 */
	return ret;
}

static enum zclient_send_status bgp_evpn_es_vtep_del(struct bgp *bgp,
						     struct bgp_evpn_es *es,
						     struct ipaddr vtep_ip,
						     bool esr)
{
	struct bgp_evpn_es_vtep *es_vtep;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;

	es_vtep = bgp_evpn_es_vtep_find(es, vtep_ip);
	if (es_vtep)
		ret = bgp_evpn_es_vtep_do_del(bgp, es_vtep, esr);

	return ret;
}

/* 【本章】ES 上的 MAC-IP 路径关联。
 *   1) VNI 路由表里的本地 MAC-IP 路由 → 关联到目的 ES 的 macip_evi_path_list，
 *      便于 ES oper 状态变化时快速更新；
 *   2) 全局表里的非本地 MAC-IP 路由 → 关联到目的 ES，便于在“VTEP 增删”或
 *      “ES-VRF 增删”时快速更新(前者触发 L3 NHG 更新，后者可能使主机路由在
 *      L3 NHG 与平铺多路径之间迁移)。 */
/********************** ES MAC-IP paths *************************************
 * 1. Local MAC-IP routes in the VNI routing table are linked to the
 * destination ES (macip_evi_path_list) for efficient updates on ES oper
 * state changes.
 * 2. Non-local MAC-IP routes in the global routing table are linked to
 * the destination for efficient updates on -
 * a. VTEP add/del - this results in a L3NHG update.
 * b. ES-VRF add/del - this may result in the host route being migrated to
 *    L3NHG or vice versa (flat multipath list).
 ****************************************************************************/
/* 【函数】bgp_evpn_path_es_info_free —— 释放路径的 es_info：先 unlink(解关联并可能
 * 释放 ES)，再 free 内存。 */
static void bgp_evpn_path_es_info_free(struct bgp_path_es_info *es_info)
{
	bgp_evpn_path_es_unlink(es_info);
	XFREE(MTYPE_BGP_EVPN_PATH_ES_INFO, es_info);
}

/* 【函数】bgp_evpn_path_mh_info_free —— 释放路径的 MH 附加信息：先分别释放 es_info
 * 与 nh_info(若存在)，再 free mh_info。 */
void bgp_evpn_path_mh_info_free(struct bgp_path_mh_info *mh_info)
{
	if (mh_info->es_info)
		bgp_evpn_path_es_info_free(mh_info->es_info);
	if (mh_info->nh_info)
		bgp_evpn_path_nh_info_free(mh_info->nh_info);
	XFREE(MTYPE_BGP_EVPN_PATH_MH_INFO, mh_info);
}

static struct bgp_path_es_info *
/* 【函数】bgp_evpn_path_es_info_new —— 为路径确保 es_info 存在(必要时连带创建
 * mh_info)，记录 vni 与所属 pi，返回 es_info。 */
bgp_evpn_path_es_info_new(struct bgp_path_info *pi, vni_t vni)
{
	struct bgp_path_info_extra *e;
	struct bgp_path_mh_info *mh_info;
	struct bgp_path_es_info *es_info;

	e = bgp_path_info_extra_get(pi);

	/* If mh_info doesn't exist allocate it */
	mh_info = e->evpn->mh_info;
	if (!mh_info)
		e->evpn->mh_info = mh_info =
			XCALLOC(MTYPE_BGP_EVPN_PATH_MH_INFO,
				sizeof(struct bgp_path_mh_info));

	/* If es_info doesn't exist allocate it */
	es_info = mh_info->es_info;
	if (!es_info) {
		mh_info->es_info = es_info =
			XCALLOC(MTYPE_BGP_EVPN_PATH_ES_INFO,
				sizeof(struct bgp_path_es_info));
		es_info->vni = vni;
		es_info->pi = pi;
	}

/* 【函数】bgp_evpn_path_es_unlink —— 把路径的 es_info 从它所属 ES 的列表(按 vni 选
 * macip_evi_path_list 或 macip_global_path_list)摘除，清 es 指针，并对 ES 尝试释放
 * (引用归零则真正释放)。注意：这里不释放 es_info 本身，它随路径一起释放。 */
	return es_info;
}

static void bgp_evpn_path_es_unlink(struct bgp_path_es_info *es_info)
{
	struct bgp_evpn_es *es = es_info->es;
	struct bgp_path_info *pi;

	if (!es)
		return;

	pi = es_info->pi;
	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("vni %u path %pFX unlinked from es %s", es_info->vni,
			   &pi->net->rn->p, es->esi_str);

	if (es_info->vni)
		list_delete_node(es->macip_evi_path_list,
				 &es_info->es_listnode);
	else
		list_delete_node(es->macip_global_path_list,
				 &es_info->es_listnode);

	es_info->es = NULL;

	/* if there are no other references against the ES it
	 * needs to be freed
	 */
	bgp_evpn_es_free(es, __func__);

	/* Note we don't free the path es_info on unlink; it will be freed up
	 * along with the path.
	 */
}

/* 【函数】bgp_evpn_path_es_link —— 把路径 pi 关联到 ESI 对应的 ES：
 *   ESI 为空/全 0：只做解关联后返回；
 *   否则确保 es_info 存在、找到或新建 ES，重复则不处理，解旧关联后按 vni 是否非 0
 *   把 es_info 挂进目的 ES 的对应列表。 */
void bgp_evpn_path_es_link(struct bgp_path_info *pi, vni_t vni, esi_t *esi)
{
	struct bgp_path_es_info *es_info;
	struct bgp_evpn_es *es;
	struct bgp *bgp_evpn;

	es_info = (pi->extra && pi->extra->evpn && pi->extra->evpn->mh_info)
			  ? pi->extra->evpn->mh_info->es_info
			  : NULL;
	/* if the esi is zero just unlink the path from the old es */
	if (!esi || !memcmp(esi, zero_esi, sizeof(*esi))) {
		if (es_info)
			bgp_evpn_path_es_unlink(es_info);
		return;
	}

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return;

	/* setup es_info against the path if it doesn't already exist */
	if (!es_info)
		es_info = bgp_evpn_path_es_info_new(pi, vni);

	/* find-create ES */
	es = bgp_evpn_es_find(esi);
	if (!es)
		es = bgp_evpn_es_new(bgp_evpn, esi);

	/* dup check */
	if (es_info->es == es)
		return;

	/* unlink old ES if any */
	bgp_evpn_path_es_unlink(es_info);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("vni %u path %pFX linked to es %s", vni, &pi->net->rn->p,
			   es->esi_str);

	/* link mac-ip path to the new destination ES */
	es_info->es = es;
	listnode_init(&es_info->es_listnode, es_info);
	if (es_info->vni)
		listnode_add(es->macip_evi_path_list, &es_info->es_listnode);
	else
		listnode_add(es->macip_global_path_list, &es_info->es_listnode);
}

/* 【函数】bgp_evpn_is_macip_path —— 判断路径是否为 MAC-IP 路由(前缀带 IPv4 或 IPv6
 * 地址)。只有 MAC-IP 路由需要关联(纯 MAC 路由可跳过)，因为这些列表是为管理租户
 * VRF 里的主机路由而维护的。 */
static bool bgp_evpn_is_macip_path(struct bgp_path_info *pi)
{
	struct prefix_evpn *evp;

	/* Only MAC-IP routes need to be linked (MAC-only routes can be
	 * skipped) as these lists are maintained for managing
	 * host routes in the tenant VRF
	 */
	evp = (struct prefix_evpn *)&pi->net->rn->p;
	return is_evpn_prefix_ipaddr_v4(evp) || is_evpn_prefix_ipaddr_v6(evp);
}

/* 【函数】bgp_evpn_es_path_update_on_es_vrf_chg —— 远端 ES 加入/退出某 VRF 时，让
 * 以该 ES 为目的地的全局 MAC-IP 路由重新尝试安装到关联 VRF：安装过程中会重新评估
 * 是否使用 L3 NHG(从而置/清属性上的 ATTR_ES_L3_NHG_USE)。若未开启 host_routes_use_l3nhg
 * 直接返回。 */
/* When a remote ES is added to a VRF, routes using that as
 * a destination need to be migrated to a L3NHG or vice versa.
 * This is done indirectly by re-attempting an install of the
 * route in the associated VRFs. As a part of the VRF install use
 * of l3 NHG is evaluated and this results in the
 * attr.es_flag ATTR_ES_L3_NHG_USE being set or cleared.
 */
static void
bgp_evpn_es_path_update_on_es_vrf_chg(struct bgp_evpn_es_vrf *es_vrf,
				      const char *reason)
{
	struct listnode *node;
	struct bgp_path_es_info *es_info;
	struct bgp_path_info *pi;
	struct bgp_evpn_es *es = es_vrf->es;

	if (!bgp_mh_info->host_routes_use_l3nhg)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("update paths linked to es %s on es-vrf %s %s",
			   es->esi_str, es_vrf->bgp_vrf->name_pretty, reason);

	for (ALL_LIST_ELEMENTS_RO(es->macip_global_path_list, node, es_info)) {
		pi = es_info->pi;

		if (!bgp_evpn_is_macip_path(pi))
			continue;

		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug(
				"update path %pFX linked to es %s on vrf chg",
				&pi->net->rn->p, es->esi_str);
		bgp_evpn_route_entry_install_if_vrf_match(es_vrf->bgp_vrf, pi,
							  1);
	}
}

/* 【函数】bgp_evpn_es_frag_free —— 释放一个 ES 分片：若是基分片则清 es_base_frag；
 * 从 es_frag_list 摘除；删掉分片内的 ES-EVI 列表；归还 RD 索引；free 内存。 */
static void bgp_evpn_es_frag_free(struct bgp_evpn_es_frag *es_frag)
{
	struct bgp_evpn_es *es = es_frag->es;

	if (es->es_base_frag == es_frag)
		es->es_base_frag = NULL;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s frag %u free", es->esi_str, es_frag->rd_id);
	list_delete_node(es->es_frag_list, &es_frag->es_listnode);

	/* EVIs that are advertised using the info in this fragment */
	list_delete(&es_frag->es_evi_frag_list);

	bf_release_index(bm->rd_idspace, es_frag->rd_id);


	XFREE(MTYPE_BGP_EVPN_ES_FRAG, es_frag);
}

/* 【函数】bgp_evpn_es_frag_free_unused —— 仅在“非基分片且分片内无 ES-EVI”时才释放；
 * 用于分片变为空后的自动回收。 */
static void bgp_evpn_es_frag_free_unused(struct bgp_evpn_es_frag *es_frag)
{
	if ((es_frag->es->es_base_frag == es_frag) ||
	    listcount(es_frag->es_evi_frag_list))
		return;

	bgp_evpn_es_frag_free(es_frag);
}

/* 【函数】bgp_evpn_es_frag_free_all —— 释放本 ES 的所有分片。 */
static void bgp_evpn_es_frag_free_all(struct bgp_evpn_es *es)
{
	struct listnode *node;
	struct listnode *nnode;
	struct bgp_evpn_es_frag *es_frag;

	for (ALL_LIST_ELEMENTS(es->es_frag_list, node, nnode, es_frag))
		bgp_evpn_es_frag_free(es_frag);
}

/* 【函数】bgp_evpn_es_frag_new —— 为一个 ES 新建分片：申请 RD 索引，由 router_id 与
 * rd_id 组出 RD(形如 "router_id:rd_id")写入 prd，初始化 ES-EVI 列表。 */
static struct bgp_evpn_es_frag *bgp_evpn_es_frag_new(struct bgp_evpn_es *es)
{
	struct bgp_evpn_es_frag *es_frag;
	char buf[BGP_EVPN_PREFIX_RD_LEN];
	struct bgp *bgp;

	es_frag = XCALLOC(MTYPE_BGP_EVPN_ES_FRAG, sizeof(*es_frag));
	bf_assign_index(bm->rd_idspace, es_frag->rd_id);
	es_frag->prd.family = AF_UNSPEC;
	es_frag->prd.prefixlen = 64;
	bgp = bgp_get_evpn();
	snprintfrr(buf, sizeof(buf), "%pI4:%hu", &bgp->router_id,
		   es_frag->rd_id);
	(void)str2prefix_rd(buf, &es_frag->prd);

	/* EVIs that are advertised using the info in this fragment */
	es_frag->es_evi_frag_list = list_new();
	listset_app_node_mem(es_frag->es_evi_frag_list);

	/* Link the fragment to the parent ES */
	es_frag->es = es;
	listnode_init(&es_frag->es_listnode, es_frag);
	listnode_add(es->es_frag_list, &es_frag->es_listnode);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s frag %u new", es->esi_str, es_frag->rd_id);
	return es_frag;
}

static struct bgp_evpn_es_frag *
/* 【函数】bgp_evpn_es_find_frag_with_space —— 在 ES 的分片列表里找一个还没满
 * (EVI 数 < evi_per_es_frag) 的分片；找不到则新建一个。 */
bgp_evpn_es_find_frag_with_space(struct bgp_evpn_es *es)
{
	struct listnode *node;
	struct bgp_evpn_es_frag *es_frag;

	for (ALL_LIST_ELEMENTS_RO(es->es_frag_list, node, es_frag)) {
		if (listcount(es_frag->es_evi_frag_list) <
		    bgp_mh_info->evi_per_es_frag)
			return es_frag;
	}

	/* No frags where found with space; allocate a new one */
	return bgp_evpn_es_frag_new(es);
}

/* 【函数】bgp_evpn_es_frag_evi_add —— 把一个 ES-EVI 挂到某个 ES 分片上：已有分片或
 * 非本地 ES-EVI 则跳过；否则找有空间的分片，设置 es_frag 并把节点挂进分片的
 * es_evi_frag_list。 */
/* Link the ES-EVI to one of the ES fragments */
static void bgp_evpn_es_frag_evi_add(struct bgp_evpn_es_evi *es_evi)
{
	struct bgp_evpn_es_frag *es_frag;
	struct bgp_evpn_es *es = es_evi->es;

	if (es_evi->es_frag ||
	    !(CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL)))
		return;

	es_frag = bgp_evpn_es_find_frag_with_space(es);

	es_evi->es_frag = es_frag;
	listnode_init(&es_evi->es_frag_listnode, es_evi);
	listnode_add(es_frag->es_evi_frag_list, &es_evi->es_frag_listnode);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vni %d linked to frag %u", es->esi_str,
			   es_evi->vpn->vni, es_frag->rd_id);
}

/* 【函数】bgp_evpn_es_frag_evi_del —— 把 ES-EVI 从分片上摘下：清 es_frag、摘链表节点；
 * 若 send_ead_del_if_empty 且分片已空，则删除该分片的 EAD-ES 路由；最后尝试回收空分片。 */
/* UnLink the ES-EVI from the ES fragment */
static void bgp_evpn_es_frag_evi_del(struct bgp_evpn_es_evi *es_evi,
				     bool send_ead_del_if_empty)
{
	struct bgp_evpn_es_frag *es_frag = es_evi->es_frag;
	struct prefix_evpn p;
	struct bgp_evpn_es *es;
	struct bgp *bgp;

	if (!es_frag)
		return;

	es = es_frag->es;
	es_evi->es_frag = NULL;
	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vni %d unlinked from frag %u", es->esi_str,
			   es_evi->vpn->vni, es_frag->rd_id);

	list_delete_node(es_frag->es_evi_frag_list, &es_evi->es_frag_listnode);

	/*
	 * if there are no other EVIs on the fragment deleted the EAD-ES for
	 * the fragment
	 */
	if (send_ead_del_if_empty && !listcount(es_frag->es_evi_frag_list)) {
		bgp = bgp_get_evpn();

		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("es %s frag %u ead-es route delete",
				   es->esi_str, es_frag->rd_id);
		build_evpn_type1_prefix(&p, BGP_EVPN_AD_ES_ETH_TAG, &es->esi,
					es->originator_ip);
		p.prefix.ead_addr.frag_id = es_frag->rd_id;
		bgp_evpn_mh_route_delete(bgp, es, NULL, es_frag, &p);
	}

	/* We don't attempt to coalesce frags that may not be full. Instead we
	 * only free up the frag when it is completely empty.
	 */
	bgp_evpn_es_frag_free_unused(es_frag);
}

/* 【函数】bgp_evpn_es_frag_evi_update_all —— 遍历 ES 的所有 ES-EVI，按 add 决定是逐个
 * 挂到分片(add=true) 还是逐个摘下(add=false)。 */
/* Link the ES-EVIs to one of the ES fragments */
static void bgp_evpn_es_frag_evi_update_all(struct bgp_evpn_es *es, bool add)
{
	struct listnode *node;
	struct bgp_evpn_es_evi *es_evi;

	for (ALL_LIST_ELEMENTS_RO(es->es_evi_list, node, es_evi)) {
		if (add)
			bgp_evpn_es_frag_evi_add(es_evi);
		else
			bgp_evpn_es_frag_evi_del(es_evi, false);
	}
}

/* 【函数】bgp_es_rb_cmp —— 全局 ES RB 树的比较函数：按 ESI 逐字节比较。
 * 紧随其后的 RB_GENERATE 生成该树的操作函数。 */
/* compare ES-IDs for the global ES RB tree */
static int bgp_es_rb_cmp(const struct bgp_evpn_es *es1,
		const struct bgp_evpn_es *es2)
{
	return memcmp(&es1->esi, &es2->esi, ESI_BYTES);
}
RB_GENERATE(bgp_es_rb_head, bgp_evpn_es, rb_node, bgp_es_rb_cmp);

/* 【函数】bgp_evpn_es_find —— 按 ESI 在全局 ES RB 树中查找 ES，返回找到的 ES 或 NULL。 */
struct bgp_evpn_es *bgp_evpn_es_find(const esi_t *esi)
{
	struct bgp_evpn_es tmp;

	memcpy(&tmp.esi, esi, sizeof(esi_t));
/* 【函数】bgp_evpn_es_new —— 新建一个 ES 对象：置 ESI、生成 esi_str、建空的 VTEP 列表
 * (带比较函数)、建 ES 路由表、插入全局 RB 树，初始化 ES-EVI / ES-VRF / MACIP 路径 /
 * 分片等列表，并向 QOBJ 注册。仅分配，不含本地/远端标志。 */
	return RB_FIND(bgp_es_rb_head, &bgp_mh_info->es_rb_tree, &tmp);
}

static struct bgp_evpn_es *bgp_evpn_es_new(struct bgp *bgp, const esi_t *esi)
{
	struct bgp_evpn_es *es;

	es = XCALLOC(MTYPE_BGP_EVPN_ES, sizeof(struct bgp_evpn_es));

	/* set the ESI */
	memcpy(&es->esi, esi, sizeof(esi_t));

	/* Initialise the VTEP list */
	es->es_vtep_list = list_new();
	listset_app_node_mem(es->es_vtep_list);
	es->es_vtep_list->cmp = bgp_evpn_es_vtep_cmp;

	esi_to_str(&es->esi, es->esi_str, sizeof(es->esi_str));

	/* Initialize the ES routing table */
	es->route_table = bgp_table_init(bgp, AFI_L2VPN, SAFI_EVPN);

	/* Add to rb_tree */
	RB_INSERT(bgp_es_rb_head, &bgp_mh_info->es_rb_tree, es);

	/* Initialise the ES-EVI list */
	es->es_evi_list = list_new();
	listset_app_node_mem(es->es_evi_list);

	/* Initialise the ES-VRF list used for L3NHG management */
	es->es_vrf_list = list_new();
	listset_app_node_mem(es->es_vrf_list);

	/* Initialise the route list used for efficient event handling */
	es->macip_evi_path_list = list_new();
	listset_app_node_mem(es->macip_evi_path_list);
	es->macip_global_path_list = list_new();
	listset_app_node_mem(es->macip_global_path_list);
	es->es_frag_list = list_new();
	listset_app_node_mem(es->es_frag_list);

	QOBJ_REG(es, bgp_evpn_es);

	return es;
}

/* 【函数】bgp_evpn_es_free —— 释放 ES(仅释放内存，调用者需先做必要清理)：
 * 只要有 LOCAL/REMOTE 标志或仍有 MACIP 路径引用就不释放；否则依次清理残留的
 * ES-VRF、ES-EVI(逐个释放)，删除各列表，解锁路由表，从 RB 树移除、解除一致性检查
 * 挂起，最后 QOBJ 注销并 free。caller 仅用于日志。 */
/* Free a given ES -
 * This just frees appropriate memory, caller should have taken other
 * needed actions.
 */
static void bgp_evpn_es_free(struct bgp_evpn_es *es, const char *caller)
{
	if (CHECK_FLAG(es->flags, (BGP_EVPNES_LOCAL | BGP_EVPNES_REMOTE)) ||
	    listcount(es->macip_evi_path_list) ||
	    listcount(es->macip_global_path_list))
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("%s: es %s free", caller, es->esi_str);

	/* Clean up any remaining ES-VRFs */
	if (es->es_vrf_list) {
		struct listnode *node, *next;
		struct bgp_evpn_es_vrf *es_vrf;

		for (ALL_LIST_ELEMENTS(es->es_vrf_list, node, next, es_vrf)) {
			if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
				zlog_debug("es %s vrf %u cleanup with evi ref_cnt %d", es->esi_str,
					   es_vrf->bgp_vrf->vrf_id, es_vrf->ref_cnt);
			bgp_evpn_es_vrf_delete(es_vrf);
		}
		list_delete(&es->es_vrf_list);
	}

	/* Clean up any remaining ES-EVIs */
	if (es->es_evi_list) {
		struct listnode *node, *next;
		struct bgp_evpn_es_evi *es_evi;

		for (ALL_LIST_ELEMENTS(es->es_evi_list, node, next, es_evi)) {
			if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
				zlog_debug("es %s cleanup ES-EVI VNI %u flags 0x%x", es->esi_str,
					   es_evi->vpn->vni, es_evi->flags);
			bgp_evpn_es_evi_free(es_evi);
		}
		list_delete(&es->es_evi_list);
	}
	list_delete(&es->es_vtep_list);
	list_delete(&es->macip_evi_path_list);
	list_delete(&es->macip_global_path_list);
	list_delete(&es->es_frag_list);
	bgp_table_unlock(es->route_table);

	/* remove the entry from various databases */
	RB_REMOVE(bgp_es_rb_head, &bgp_mh_info->es_rb_tree, es);
	bgp_evpn_es_cons_checks_pend_del(es);

	QOBJ_UNREG(es);
	XFREE(MTYPE_BGP_EVPN_ES, es);
}

/* 【函数】bgp_evpn_is_es_local_and_non_bypass —— 该 ES 是否“本地且非 BYPASS”。
 * 与外层同名函数(按 ESI 查找后的版本)区分：这里直接作用于 ES 对象。 */
static inline bool bgp_evpn_is_es_local_and_non_bypass(struct bgp_evpn_es *es)
{
	return CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL) &&
	       !CHECK_FLAG(es->flags, BGP_EVPNES_BYPASS);
}

/* 【函数】bgp_evpn_es_local_info_set —— 给 ES 打上本地标志并初始化本地信息：置 LOCAL、
 * 挂进 local_es_list、创建基分片并把现有 ES-EVI 分配到各分片；若本地(且非 bypass)
 * 状态发生变化，则触发相关 MAC-IP 路径的重新安装。 */
/* init local info associated with the ES */
static void bgp_evpn_es_local_info_set(struct bgp *bgp, struct bgp_evpn_es *es)
{
	bool old_is_local;
	bool is_local;

	if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
		return;

	old_is_local = bgp_evpn_is_es_local_and_non_bypass(es);
	SET_FLAG(es->flags, BGP_EVPNES_LOCAL);

	listnode_init(&es->es_listnode, es);
	listnode_add(bgp_mh_info->local_es_list, &es->es_listnode);

	/* setup the first ES fragment; more fragments may be allocated based
	 * on the the number of EVI entries
	 */
	es->es_base_frag = bgp_evpn_es_frag_new(es);
	/* distribute ES-EVIs to one or more ES fragments */
	bgp_evpn_es_frag_evi_update_all(es, true);

	is_local = bgp_evpn_is_es_local_and_non_bypass(es);
	if (old_is_local != is_local)
		bgp_evpn_mac_update_on_es_local_chg(es, is_local);
}

/* 【函数】bgp_evpn_es_local_info_clear —— 清除 ES 的本地信息：先把 ES-EVI 从分片摘除
 * 并释放全部分片、清基分片；清 LOCAL 位并触发 MAC 路径更新(finish 时跳过)；从
 * local_es_list 摘除；尝试释放 ES。 */
/* clear any local info associated with the ES */
static void bgp_evpn_es_local_info_clear(struct bgp_evpn_es *es, bool finish)
{
	bool old_is_local;
	bool is_local;

	if (!CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
		return;

	/* clear the es frag references and free them up */
	bgp_evpn_es_frag_evi_update_all(es, false);
	es->es_base_frag = NULL;
	bgp_evpn_es_frag_free_all(es);

	old_is_local = bgp_evpn_is_es_local_and_non_bypass(es);
	UNSET_FLAG(es->flags, BGP_EVPNES_LOCAL);

	is_local = bgp_evpn_is_es_local_and_non_bypass(es);
	if (!finish && (old_is_local != is_local))
		bgp_evpn_mac_update_on_es_local_chg(es, is_local);

	/* remove from the ES local list */
	list_delete_node(bgp_mh_info->local_es_list, &es->es_listnode);

	bgp_evpn_es_free(es, __func__);
}

/* 【函数】bgp_evpn_es_remote_info_re_eval —— 重评估远端信息：若 remote_es_evi_cnt>0
 * 置 REMOTE 位；否则若原本有 REMOTE 位则清位并尝试释放 ES。 */
/* eval remote info associated with the ES */
static void bgp_evpn_es_remote_info_re_eval(struct bgp_evpn_es *es)
{
	if (es->remote_es_evi_cnt) {
		SET_FLAG(es->flags, BGP_EVPNES_REMOTE);
	} else {
		if (CHECK_FLAG(es->flags, BGP_EVPNES_REMOTE)) {
			UNSET_FLAG(es->flags, BGP_EVPNES_REMOTE);
			bgp_evpn_es_free(es, __func__);
		}
	}
}

/* 【函数】bgp_evpn_es_add_l3_attrs_ok —— 判断能否为该 ESI 的通告附加 L3 属性：
 * 若 ESI 为空或未开启 suppress_l3_ecomm_on_inactive_es，一律允许；否则要求该 ES
 * 不存在、或非本地、或本地活跃。 */
/* If ES is present and local it needs to be active/oper-up for
 * including L3 EC
 */
bool bgp_evpn_es_add_l3_attrs_ok(esi_t *esi)
{
	struct bgp_evpn_es *es;

	if (!esi || !bgp_mh_info->suppress_l3_ecomm_on_inactive_es)
		return true;

	es = bgp_evpn_es_find(esi);

	return (!es || !CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL) ||
		bgp_evpn_local_es_is_active(es));
}

/* 【函数】bgp_evpn_is_valid_local_path —— 路径是否为“有效的本地路径”：VALID 且为
 * BGP/STATIC 子类型。 */
static bool bgp_evpn_is_valid_local_path(struct bgp_path_info *pi)
{
	return (CHECK_FLAG(pi->flags, BGP_PATH_VALID)
		&& pi->type == ZEBRA_ROUTE_BGP
		&& pi->sub_type == BGP_ROUTE_STATIC);
}

/* 【函数】bgp_evpn_mac_update_on_es_oper_chg —— ES oper 状态变化时，更新 VNI 路由表里
 * 所有关联到该 ES 的本地 MAC-IP 路由(ES down 时去掉 L3 扩展团体)。若未开启
 * suppress_l3_ecomm_on_inactive_es 直接返回。 */
/* Update all local MAC-IP routes in the VNI routing table associated
 * with the ES. When the ES is down the routes are advertised without
 * the L3 extcomm
 */
static void bgp_evpn_mac_update_on_es_oper_chg(struct bgp_evpn_es *es)
{
	struct listnode *node;
	struct bgp_path_es_info *es_info;
	struct bgp_path_info *pi;
	struct bgp *bgp;
	struct bgpevpn *vpn;

	if (!bgp_mh_info->suppress_l3_ecomm_on_inactive_es)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("update paths linked to es %s on oper chg",
			   es->esi_str);

	bgp = bgp_get_evpn();
	assert(bgp != NULL);

	for (ALL_LIST_ELEMENTS_RO(es->macip_evi_path_list, node, es_info)) {
		pi = es_info->pi;

		if (!bgp_evpn_is_valid_local_path(pi))
			continue;

		if (!bgp_evpn_is_macip_path(pi))
			continue;

		vpn = bgp_evpn_lookup_vni(bgp, es_info->vni);
		if (!vpn)
			continue;

		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug(
				"update path %d %pFX linked to es %s on oper chg",
				es_info->vni, &pi->net->rn->p, es->esi_str);

		bgp_evpn_update_type2_route_entry(bgp, vpn, pi->net, pi,
						  __func__);
	}
}

/* 【函数】bgp_evpn_is_valid_bgp_path —— 路径是否为“有效的远端 BGP 路径”：VALID 且为
 * BGP/NORMAL 子类型。 */
static bool bgp_evpn_is_valid_bgp_path(struct bgp_path_info *pi)
{
	return (CHECK_FLAG(pi->flags, BGP_PATH_VALID)
		&& pi->type == ZEBRA_ROUTE_BGP
		&& pi->sub_type == BGP_ROUTE_NORMAL);
}

/* 【函数】bgp_evpn_mac_update_on_es_local_chg —— ES 的本地/非本地状态变化时，重新安装
 * 以该 ES 为目的地的全局 MAC-IP 路径(因为最优路径判据变了)：对每条有效的远端路径，
 * 按其 attr 里的 ATTR_ES_IS_LOCAL 位与新状态是否一致来决定是否需要改属性并重新导入。 */
/* If an ES is no longer local (or becomes local) we need to re-install
 * paths using that ES as destination. This is needed as the criteria
 * for best path selection has changed.
 */
static void bgp_evpn_mac_update_on_es_local_chg(struct bgp_evpn_es *es,
						bool is_local)
{
	struct listnode *node;
	struct bgp_path_es_info *es_info;
	struct bgp_path_info *pi;
	bool tmp_local;
	struct attr *attr_new;
	struct attr attr_tmp;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("update paths linked to es %s on chg to %s",
			   es->esi_str, is_local ? "local" : "non-local");

	for (ALL_LIST_ELEMENTS_RO(es->macip_global_path_list, node, es_info)) {
		pi = es_info->pi;

		/* Consider "valid" remote routes */
		if (!bgp_evpn_is_valid_bgp_path(pi))
			continue;

		if (!pi->attr)
			continue;

		tmp_local = !!(pi->attr->es_flags & ATTR_ES_IS_LOCAL);
		if (tmp_local == is_local)
			continue;

		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug(
				"update path %pFX linked to es %s on chg to %s",
				&pi->net->rn->p, es->esi_str,
				is_local ? "local" : "non-local");

		attr_tmp = *pi->attr;
		if (is_local)
			SET_FLAG(attr_tmp.es_flags, ATTR_ES_IS_LOCAL);
		else
			UNSET_FLAG(attr_tmp.es_flags, ATTR_ES_IS_LOCAL);
		attr_new = bgp_attr_intern(&attr_tmp);
		bgp_attr_unintern(&pi->attr);
		pi->attr = attr_new;
		bgp_evpn_import_type2_route(pi, 1);
	}
}

/* 【函数】bgp_evpn_local_es_deactivate —— 本地 ES 去激活：撤销 ESR(Type-4)、必要时撤销
 * EAD-EVI、撤销 EAD-ES，并刷新相关 MAC-IP 路径。 */
static void bgp_evpn_local_es_deactivate(struct bgp *bgp,
					 struct bgp_evpn_es *es)
{
	struct prefix_evpn p;
	int ret;

	/* withdraw ESR */
	/* Delete and withdraw locally learnt ES route */
	build_evpn_type4_prefix(&p, &es->esi, es->originator_ip);
	ret = bgp_evpn_type4_route_delete(bgp, es, &p);
	if (ret) {
		flog_err(EC_BGP_EVPN_ROUTE_DELETE,
				"%u failed to delete type-4 route for ESI %s",
				bgp->vrf_id, es->esi_str);
	}

	/* withdraw EAD-EVI */
	if (!bgp_mh_info->ead_evi_adv_for_down_links)
		bgp_evpn_local_type1_evi_route_del(bgp, es);

	/* withdraw EAD-ES */
	bgp_evpn_ead_es_route_delete(bgp, es);

	bgp_evpn_mac_update_on_es_oper_chg(es);
}

/* 【函数】bgp_evpn_local_es_down —— 处理 ES 链路 oper-down：若已是 down 直接返回；清
 * OPER_UP 位；若此前为活跃态，则执行 deactivate(撤路由)。 */
/* Process ES link oper-down by withdrawing ES-EAD and ESR */
static void bgp_evpn_local_es_down(struct bgp *bgp, struct bgp_evpn_es *es)
{
	bool old_active;

	if (!CHECK_FLAG(es->flags, BGP_EVPNES_OPER_UP))
		return;

	old_active = bgp_evpn_local_es_is_active(es);
	UNSET_FLAG(es->flags, BGP_EVPNES_OPER_UP);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("local es %s down", es->esi_str);

	if (old_active)
		bgp_evpn_local_es_deactivate(bgp, es);
}

/* 【函数】bgp_evpn_local_es_activate —— 本地 ES 激活：按 regen_esr/regen_ead 重新生成
 * ESR(Type-4) 与 EAD-EVI / EAD-ES，并刷新相关 MAC-IP 路径。 */
static void bgp_evpn_local_es_activate(struct bgp *bgp, struct bgp_evpn_es *es,
				       bool regen_ead, bool regen_esr)
{
	struct prefix_evpn p;

	if (regen_esr) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("local es %s generate ESR", es->esi_str);
		/* generate ESR */
		build_evpn_type4_prefix(&p, &es->esi, es->originator_ip);
		if (bgp_evpn_type4_route_update(bgp, es, &p))
			flog_err(EC_BGP_EVPN_ROUTE_CREATE,
				 "%u: Type4 route creation failure for ESI %s",
				 bgp->vrf_id, es->esi_str);
	}

	if (regen_ead) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("local es %s generate EAD", es->esi_str);
		/* generate EAD-EVI */
		bgp_evpn_local_type1_evi_route_add(bgp, es);

		/* generate EAD-ES */
		bgp_evpn_ead_es_route_update(bgp, es);
	}

	bgp_evpn_mac_update_on_es_oper_chg(es);
}

/* 【函数】bgp_evpn_local_es_up —— 处理 ES 链路 oper-up：若此前非 up，则置 OPER_UP 位并
 * 要求重生成 ESR 与 EAD；再在活跃时执行 activate。 */
/* Process ES link oper-up by generating ES-EAD and ESR */
static void bgp_evpn_local_es_up(struct bgp *bgp, struct bgp_evpn_es *es,
				 bool regen_esr)
{
	bool regen_ead = false;
	bool active = false;

	if (!CHECK_FLAG(es->flags, BGP_EVPNES_OPER_UP)) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("local es %s up", es->esi_str);

		SET_FLAG(es->flags, BGP_EVPNES_OPER_UP);
		regen_esr = true;
		regen_ead = true;
	}

	active = bgp_evpn_local_es_is_active(es);
	if (active && (regen_ead || regen_esr))
		bgp_evpn_local_es_activate(bgp, es, regen_ead, regen_esr);
}

/* 【函数】bgp_evpn_local_es_bypass_update —— 处理 ES 的 LACP bypass 状态变化：bypass
 * 时不通告 EAD-per-ES 与 ESR(EAD-per-EVI 仍通告)。状态变化会触发 activate/deactivate
 * 以及 MAC 路径更新。 */
/* If an ethernet segment is in LACP bypass we cannot advertise
 * reachability to it i.e. EAD-per-ES and ESR is not advertised in
 * bypass state.
 * PS: EAD-per-EVI will continue to be advertised
 */
static void bgp_evpn_local_es_bypass_update(struct bgp *bgp,
					    struct bgp_evpn_es *es, bool bypass)
{
	bool old_bypass = !!(es->flags & BGP_EVPNES_BYPASS);
	bool old_active;
	bool new_active;
	bool old_is_local;
	bool is_local;

	if (bypass == old_bypass)
		return;

	old_active = bgp_evpn_local_es_is_active(es);
	old_is_local = bgp_evpn_is_es_local_and_non_bypass(es);
	if (bypass)
		SET_FLAG(es->flags, BGP_EVPNES_BYPASS);
	else
		UNSET_FLAG(es->flags, BGP_EVPNES_BYPASS);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("local es %s bypass %s", es->esi_str,
			   bypass ? "set" : "clear");

	new_active = bgp_evpn_local_es_is_active(es);
	if (old_active != new_active) {
		if (new_active)
			bgp_evpn_local_es_activate(bgp, es, true, true);
		else
			bgp_evpn_local_es_deactivate(bgp, es);
	}

	is_local = bgp_evpn_is_es_local_and_non_bypass(es);
	if (old_is_local != is_local)
		bgp_evpn_mac_update_on_es_local_chg(es, is_local);
}

/* 【函数】bgp_evpn_local_es_do_del —— 删除本地 ES 的主体：删除 ES 表里所有本地路由、
 * 释放本 ES 关联的所有本地 ES-EVI，最后清除本地信息(无远端引用时释放 ES)。 */
static void bgp_evpn_local_es_do_del(struct bgp *bgp, struct bgp_evpn_es *es)
{
	struct bgp_evpn_es_evi *es_evi;
	struct listnode *evi_node, *evi_next_node;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("del local es %s", es->esi_str);

	/* Delete all local EVPN ES routes from ESI table
	 * and schedule for processing (to withdraw from peers))
	 */
	bgp_evpn_es_route_del_all(bgp, es);

	/* release all local ES EVIs associated with the ES */
	for (ALL_LIST_ELEMENTS(es->es_evi_list, evi_node,
				evi_next_node, es_evi)) {
		bgp_evpn_local_es_evi_do_del(es_evi);
	}

	/* Clear local info associated with the ES and free it up if there is
	 * no remote reference
	 */
	bgp_evpn_es_local_info_clear(es, false);
}

/* 【函数】bgp_evpn_is_esi_local_and_non_bypass —— 按 ESI 查找 ES 后判断其是否“本地且
 * 非 BYPASS”；找不到返回 false。 */
bool bgp_evpn_is_esi_local_and_non_bypass(esi_t *esi)
{
	struct bgp_evpn_es *es = NULL;

	/* Lookup ESI hash - should exist. */
	es = bgp_evpn_es_find(esi);

	return es && bgp_evpn_is_es_local_and_non_bypass(es);
}

/* 【函数】bgp_evpn_local_es_del —— zebra 通知删除本地 ES：找不到则告警返回 -1，否则
 * 走 do_del 并返回 0。 */
int bgp_evpn_local_es_del(struct bgp *bgp, esi_t *esi)
{
	struct bgp_evpn_es *es = NULL;

	/* Lookup ESI hash - should exist. */
	es = bgp_evpn_es_find(esi);
	if (!es) {
		flog_warn(EC_BGP_EVPN_ESI, "%u: ES missing at local ES DEL",
			  bgp->vrf_id);
		return -1;
	}

	bgp_evpn_local_es_do_del(bgp, es);
	return 0;
}

/* 【函数】bgp_evpn_local_es_add —— zebra 通知“设备与 ES 关联”(本地 ES 新增/更新)：
 * 找到或新建 ES，记录/更新 originator_ip 与 df_pref(df_pref 变化则需重生成 ESR)，
 * 置本地信息；新 ES 则导入远端 Type-4 路由；按需生成 EAD-EVI；处理 bypass；最后按
 * oper_up 走 up/down。返回 0。 */
/* Handle device to ES id association. Results in the creation of a local
 * ES.
 */
int bgp_evpn_local_es_add(struct bgp *bgp, esi_t *esi, struct ipaddr originator_ip, bool oper_up,
			  uint16_t df_pref, bool bypass)
{
	struct bgp_evpn_es *es;
	bool new_es = true;
	bool regen_esr = false;

	/* create the new es */
	es = bgp_evpn_es_find(esi);
	if (es) {
		if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
			new_es = false;
	} else
		es = bgp_evpn_es_new(bgp, esi);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("add local es %s orig-ip %pIA df_pref %u %s", es->esi_str,
			   &originator_ip, df_pref, bypass ? "bypass" : "");

	es->originator_ip = originator_ip;
	if (df_pref != es->df_pref) {
		es->df_pref = df_pref;
		regen_esr = true;
	}
	bgp_evpn_es_local_info_set(bgp, es);

	/* import all remote Type-4 routes in the ES table */
	if (new_es)
		bgp_evpn_type4_remote_routes_import(bgp, es,
				true /* install */);

	/* create and advertise EAD-EVI routes for the ES -
	 * XXX - till an ES-EVI reference is created there is really nothing to
	 * advertise
	 */
	if (bgp_mh_info->ead_evi_adv_for_down_links)
		bgp_evpn_local_type1_evi_route_add(bgp, es);

	bgp_evpn_local_es_bypass_update(bgp, es, bypass);

	/* If the ES link is operationally up generate EAD-ES. EAD-EVI
	 * can be generated even if the link is inactive.
	 */
	if (oper_up)
		bgp_evpn_local_es_up(bgp, es, regen_esr);
	else
		bgp_evpn_local_es_down(bgp, es);

	return 0;
}

/* 【函数】bgp_evpn_es_json_frag_fill —— 把 ES 的各分片信息(RD、EVI 数)填进 JSON 数组，
 * 供 show es ... json 使用。 */
static void bgp_evpn_es_json_frag_fill(json_object *json_frags,
				       struct bgp_evpn_es *es)
{
	json_object *json_frag;
	struct listnode *node;
	struct bgp_evpn_es_frag *es_frag;

	for (ALL_LIST_ELEMENTS_RO(es->es_frag_list, node, es_frag)) {
		json_frag = json_object_new_object();

		json_object_string_addf(json_frag, "rd", "%pRDP",
					&es_frag->prd);
		json_object_int_add(json_frag, "eviCount",
				    listcount(es_frag->es_evi_frag_list));

		json_object_array_add(json_frags, json_frag);
	}
}

/* 【函数】bgp_evpn_es_frag_show_detail —— 文本方式打印某 ES 的每个分片头(RD 及其
 * EVI 个数)。在每个 ES 的详细视图(detail)里被调用。 */
static void bgp_evpn_es_frag_show_detail(struct vty *vty,
					 struct bgp_evpn_es *es)
{
	struct listnode *node;
	struct bgp_evpn_es_frag *es_frag;

	for (ALL_LIST_ELEMENTS_RO(es->es_frag_list, node, es_frag)) {
		vty_out(vty, "  %pRDP EVIs: %d\n", &es_frag->prd,
			listcount(es_frag->es_evi_frag_list));
	}
}

/* 【函数】bgp_evpn_es_vteps_str —— 把 ES 的 VTEP 列表拼成一行字符串，形如
 *   IP1(flags1),IP2(flags2),...
 * 每个 VTEP 后括号内是标志位缩写：E=ESR(Type-4 来源)、A=ACTIVE(活跃 nexthop)、
 * 无标志时用 "-"。供 show 的简要表格列使用，返回传入的 vtep_str 缓冲。 */
static char *bgp_evpn_es_vteps_str(char *vtep_str, struct bgp_evpn_es *es,
				   size_t vtep_str_size)
{
	char vtep_flag_str[BGP_EVPN_FLAG_STR_SZ];
	struct listnode *node;
	struct bgp_evpn_es_vtep *es_vtep;
	bool first = true;
	char ip_buf[INET6_ADDRSTRLEN];

	vtep_str[0] = '\0';
	for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list, node, es_vtep)) {
		vtep_flag_str[0] = '\0';

		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR))
			strlcat(vtep_flag_str, "E", sizeof(vtep_flag_str));
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE))
			strlcat(vtep_flag_str, "A", sizeof(vtep_flag_str));

		if (!strlen(vtep_flag_str))
			strlcat(vtep_flag_str, "-", sizeof(vtep_flag_str));
		if (first)
			first = false;
		else
			strlcat(vtep_str, ",", vtep_str_size);
		ipaddr2str(&es_vtep->vtep_ip, ip_buf, sizeof(ip_buf));
		strlcat(vtep_str, ip_buf, vtep_str_size);
		strlcat(vtep_str, "(", vtep_str_size);
		strlcat(vtep_str, vtep_flag_str, vtep_str_size);
		strlcat(vtep_str, ")", vtep_str_size);
	}

	return vtep_str;
}

/* 【函数】bgp_evpn_es_json_vtep_fill —— 把一个 ES-VTEP 的字段(vtep_ip、flags、
 * ESR 时的 dfPreference/dfAlgorithm)写成一个 JSON 对象并追加进 json_vteps 数组。
 * 只有含 ESR/ACTIVE 标志的才会写 flags；df_* 仅在 ESR 内部置时输出。 */
static void bgp_evpn_es_json_vtep_fill(json_object *json_vteps,
		struct bgp_evpn_es_vtep *es_vtep)
{
	json_object *json_vtep_entry;
	json_object *json_flags;
	char alg_buf[EVPN_DF_ALG_STR_LEN];

	json_vtep_entry = json_object_new_object();

	json_object_string_addf(json_vtep_entry, "vtep_ip", "%pIA", &es_vtep->vtep_ip);
	if (CHECK_FLAG(es_vtep->flags,
		       (BGP_EVPNES_VTEP_ESR | BGP_EVPNES_VTEP_ACTIVE))) {
		json_flags = json_object_new_array();
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR))
			json_array_string_add(json_flags, "esr");
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE))
			json_array_string_add(json_flags, "active");
		json_object_object_add(json_vtep_entry, "flags", json_flags);
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR)) {
			json_object_int_add(json_vtep_entry, "dfPreference",
					    es_vtep->df_pref);
			json_object_string_add(
				json_vtep_entry, "dfAlgorithm",
				evpn_es_df_alg2str(es_vtep->df_alg, alg_buf,
						   sizeof(alg_buf)));
		}
	}

	json_object_array_add(json_vteps,
			json_vtep_entry);
}

/* 【函数】bgp_evpn_es_vteps_show_detail —— 文本方式逐行打印 ES 的每个 VTEP：
 * "IP flags: EA df_alg: xx df_pref: yy"（非 ESR 的没有 df 部分）。 */
static void bgp_evpn_es_vteps_show_detail(struct vty *vty,
					  struct bgp_evpn_es *es)
{
	char vtep_flag_str[BGP_EVPN_FLAG_STR_SZ];
	struct listnode *node;
	struct bgp_evpn_es_vtep *es_vtep;
	char alg_buf[EVPN_DF_ALG_STR_LEN];

	for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list, node, es_vtep)) {
		vtep_flag_str[0] = '\0';
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR))
			strlcat(vtep_flag_str, "E", sizeof(vtep_flag_str));
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE))
			strlcat(vtep_flag_str, "A", sizeof(vtep_flag_str));

		if (!strlen(vtep_flag_str))
			strlcat(vtep_flag_str, "-", sizeof(vtep_flag_str));

		vty_out(vty, "  %pIA flags: %s", &es_vtep->vtep_ip, vtep_flag_str);

		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ESR))
			vty_out(vty, " df_alg: %s df_pref: %u\n",
				evpn_es_df_alg2str(es_vtep->df_alg, alg_buf,
						   sizeof(alg_buf)),
				es_vtep->df_pref);
		else
			vty_out(vty, "\n");
	}
}

/* 【函数】bgp_evpn_es_show_entry —— 打印某个 ES 的“简要”视图（表格一行或一条
 * JSON）。type_str 由标志拼成：B=bypass、L=local、R=remote、I=inconsistent。
 * json 非空走 JSON 分支，否则走文本表格分支。 */
static void bgp_evpn_es_show_entry(struct vty *vty,
		struct bgp_evpn_es *es, json_object *json)
{
	struct listnode *node;
	struct bgp_evpn_es_vtep *es_vtep;

	if (json) {
		json_object *json_vteps;
		json_object *json_types;

		json_object_string_add(json, "esi", es->esi_str);
		if (es->es_base_frag)
			json_object_string_addf(json, "rd", "%pRDP",
						&es->es_base_frag->prd);

		if (CHECK_FLAG(es->flags,
			       (BGP_EVPNES_LOCAL | BGP_EVPNES_REMOTE))) {
			json_types = json_object_new_array();
			if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
				json_array_string_add(json_types, "local");
			if (CHECK_FLAG(es->flags, BGP_EVPNES_REMOTE))
				json_array_string_add(json_types, "remote");
			json_object_object_add(json, "type", json_types);
		}

		if (listcount(es->es_vtep_list)) {
			json_vteps = json_object_new_array();
			for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list,
						node, es_vtep)) {
				bgp_evpn_es_json_vtep_fill(json_vteps, es_vtep);
			}
			json_object_object_add(json, "vteps", json_vteps);
		}
		json_object_int_add(json, "vniCount",
				listcount(es->es_evi_list));
	} else {
		char type_str[4];
		char vtep_str[ES_VTEP_LIST_STR_SZ + BGP_EVPN_VTEPS_FLAG_STR_SZ];

		type_str[0] = '\0';
		if (CHECK_FLAG(es->flags, BGP_EVPNES_BYPASS))
			strlcat(type_str, "B", sizeof(type_str));
		if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
			strlcat(type_str, "L", sizeof(type_str));
		if (CHECK_FLAG(es->flags, BGP_EVPNES_REMOTE))
			strlcat(type_str, "R", sizeof(type_str));
		if (es->inconsistencies)
			strlcat(type_str, "I", sizeof(type_str));

		bgp_evpn_es_vteps_str(vtep_str, es, sizeof(vtep_str));

		vty_out(vty, "%-30s %-5s %-21pRDP %-8d %s\n", es->esi_str,
			type_str,
			es->es_base_frag ? &es->es_base_frag->prd : NULL,
			listcount(es->es_evi_list), vtep_str);
	}
}

/* 【函数】bgp_evpn_es_show_entry_detail —— 打印某 ES 的“详细”视图：先在前面
 * 追加简要信息(bgp_evpn_es_show_entry)，再补充 flags/originator_ip/vrfCount/
 * macipPathCount/inconsistencies/fragments/VTEPs 等。JSON 与文本两条分支。 */
static void bgp_evpn_es_show_entry_detail(struct vty *vty,
		struct bgp_evpn_es *es, json_object *json)
{
	if (json) {
		json_object *json_flags;
		json_object *json_incons;
		json_object *json_vteps;
		json_object *json_frags;
		struct listnode *node;
		struct bgp_evpn_es_vtep *es_vtep;

		/* Add the "brief" info first */
		bgp_evpn_es_show_entry(vty, es, json);
		if (CHECK_FLAG(es->flags,
			       (BGP_EVPNES_OPER_UP | BGP_EVPNES_ADV_EVI |
				BGP_EVPNES_BYPASS))) {
			json_flags = json_object_new_array();
			if (CHECK_FLAG(es->flags, BGP_EVPNES_OPER_UP))
				json_array_string_add(json_flags, "up");
			if (CHECK_FLAG(es->flags, BGP_EVPNES_ADV_EVI))
				json_array_string_add(json_flags,
						"advertiseEVI");
			if (CHECK_FLAG(es->flags, BGP_EVPNES_BYPASS))
				json_array_string_add(json_flags, "bypass");
			json_object_object_add(json, "flags", json_flags);
		}
		json_object_string_addf(json, "originator_ip", "%pIA", &es->originator_ip);
		json_object_int_add(json, "remoteVniCount",
				es->remote_es_evi_cnt);
		json_object_int_add(json, "vrfCount",
				    listcount(es->es_vrf_list));
		json_object_int_add(json, "macipPathCount",
				    listcount(es->macip_evi_path_list));
		json_object_int_add(json, "macipGlobalPathCount",
				    listcount(es->macip_global_path_list));
		json_object_int_add(json, "inconsistentVniVtepCount",
				es->incons_evi_vtep_cnt);
		if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
			json_object_int_add(json, "localEsDfPreference",
					    es->df_pref);
		if (listcount(es->es_vtep_list)) {
			json_vteps = json_object_new_array();
			for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list, node,
						  es_vtep)) {
				bgp_evpn_es_json_vtep_fill(json_vteps, es_vtep);
			}
			json_object_object_add(json, "vteps", json_vteps);
		}
		if (listcount(es->es_frag_list)) {
			json_frags = json_object_new_array();
			bgp_evpn_es_json_frag_fill(json_frags, es);
			json_object_object_add(json, "fragments", json_frags);
		}
		if (es->inconsistencies) {
			json_incons = json_object_new_array();
			if (CHECK_FLAG(es->inconsistencies,
				       BGP_EVPNES_INCONS_VTEP_LIST))
				json_array_string_add(json_incons,
						"vni-vtep-mismatch");
			json_object_object_add(json, "inconsistencies",
					json_incons);
		}
	} else {
		char incons_str[BGP_EVPNES_INCONS_STR_SZ];
		char type_str[4];

		type_str[0] = '\0';
		if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
			strlcat(type_str, "L", sizeof(type_str));
		if (CHECK_FLAG(es->flags, BGP_EVPNES_REMOTE))
			strlcat(type_str, "R", sizeof(type_str));

		vty_out(vty, "ESI: %s\n", es->esi_str);
		vty_out(vty, " Type: %s\n", type_str);
		vty_out(vty, " RD: %pRDP\n",
			es->es_base_frag ? &es->es_base_frag->prd : NULL);
		vty_out(vty, " Originator-IP: %pIA\n", &es->originator_ip);
		if (CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL))
			vty_out(vty, " Local ES DF preference: %u\n",
				es->df_pref);
		if (CHECK_FLAG(es->flags, BGP_EVPNES_BYPASS))
			vty_out(vty, " LACP bypass: on\n");
		vty_out(vty, " VNI Count: %d\n", listcount(es->es_evi_list));
		vty_out(vty, " Remote VNI Count: %d\n",
				es->remote_es_evi_cnt);
		vty_out(vty, " VRF Count: %d\n", listcount(es->es_vrf_list));
		vty_out(vty, " MACIP EVI Path Count: %d\n",
			listcount(es->macip_evi_path_list));
		vty_out(vty, " MACIP Global Path Count: %d\n",
			listcount(es->macip_global_path_list));
		vty_out(vty, " Inconsistent VNI VTEP Count: %d\n",
				es->incons_evi_vtep_cnt);
		if (es->inconsistencies) {
			incons_str[0] = '\0';
			if (CHECK_FLAG(es->inconsistencies,
				       BGP_EVPNES_INCONS_VTEP_LIST))
				strlcat(incons_str, "vni-vtep-mismatch",
					sizeof(incons_str));
		} else {
			strlcpy(incons_str, "-", sizeof(incons_str));
		}
		vty_out(vty, " Inconsistencies: %s\n",
				incons_str);
		if (listcount(es->es_frag_list)) {
			vty_out(vty, " Fragments:\n");
			bgp_evpn_es_frag_show_detail(vty, es);
		}
		if (listcount(es->es_vtep_list)) {
			vty_out(vty, " VTEPs:\n");
			bgp_evpn_es_vteps_show_detail(vty, es);
		}
		vty_out(vty, "\n");
	}
}

/* 【函数】bgp_evpn_es_show —— 遍历 ES 红黑树(bgp_mh_info->es_rb_tree)，逐个打印。
 * uj=true 时收集成 JSON 数组输出；detail=true 时调用详细视图，否则只出简要表格。
 * 非 JSON 且非 detail 时先打印表头与图例(B/L/R/I、E/A)。 */
/* Display all ESs */
void bgp_evpn_es_show(struct vty *vty, bool uj, bool detail)
{
	struct bgp_evpn_es *es;
	json_object *json_array = NULL;
	json_object *json = NULL;

	if (uj) {
		/* create an array of ESs */
		json_array = json_object_new_array();
	} else {
		if (!detail) {
			vty_out(vty,
				"ES Flags: B - bypass, L local, R remote, I inconsistent\n");
			vty_out(vty,
				"VTEP Flags: E ESR/Type-4, A active nexthop\n");
			vty_out(vty,
				"%-30s %-5s %-21s %-8s %s\n",
				"ESI", "Flags", "RD", "#VNIs", "VTEPs");
		}
	}

	RB_FOREACH(es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree) {
		if (uj)
			/* create a separate json object for each ES */
			json = json_object_new_object();
		if (detail)
			bgp_evpn_es_show_entry_detail(vty, es, json);
		else
			bgp_evpn_es_show_entry(vty, es, json);
		/* add ES to the json array */
		if (uj)
			json_object_array_add(json_array, json);
	}

	/* print the array of json-ESs */
	if (uj)
		vty_json(vty, json_array);
}

/* 【函数】bgp_evpn_es_show_esi —— 打印**指定 ESI** 的那个 ES 的详细视图。
 * 找不到该 ES 时文本分支输出 "ESI not found"；JSON 分支则输出空对象。 */
/* Display specific ES */
void bgp_evpn_es_show_esi(struct vty *vty, esi_t *esi, bool uj)
{
	struct bgp_evpn_es *es;
	json_object *json = NULL;

	if (uj)
		json = json_object_new_object();

	es = bgp_evpn_es_find(esi);
	if (es) {
		bgp_evpn_es_show_entry_detail(vty, es, json);
	} else {
		if (!uj)
			vty_out(vty, "ESI not found\n");
	}

	if (uj)
		vty_json(vty, json);
}

/* ============================================================================
 * 【本章】ES 与 VRF 的关联 + L3 NHG(三层下一跳组)管理
 * ============================================================================
 * 背景：一个 ES-EVI 属于某个租户 VRF，于是产生一条 ES-VRF 记录，专门用于
 *   生成 L3-NHG。Type-2(MAC-IP) 路由被导入租户 VRF 后按 /32 主机路由下发；
 *   若该主机路由的目的地是“远端 ES”，则用按 {VRF, ES-id} 索引的 L3-NHG 下发。
 * 为什么绕这一层(路由→L3-NHG→VTEP 列表)：远端 ES 的成员链路抖动时，只需更新
 *   NHG 的内容，而**不必**去改所有依赖它的路由，减少 dplane 更新量、加快收敛。
 * 本章函数分三组：
 *   1) L3 NHG 的 zebra 下发(v4/v6 两套 nhg_id)与激活/去激活状态机；
 *   2) ES-VRF 的创建/查找/引用计数/释放(每租户 VRF 一棵 ES-VRF 红黑树)；
 *   3) host route 是否走 L3-NHG 的判定与 MAC-IP 路径的 NHG 使用逻辑。
 * ============================================================================
 */
/*****************************************************************************/
/* Ethernet Segment to VRF association -
 * 1. Each ES-EVI entry is associated with a tenant VRF. This associaton
 * triggers the creation of an ES-VRF entry.
 * 2. The ES-VRF entry is maintained for the purpose of L3-NHG creation
 * 3. Type-2/MAC-IP routes are imported into a tenant VRF and programmed as
 * a /32 or host route entry in the dataplane. If the destination of
 * the host route is a remote-ES the route is programmed with the
 * corresponding (keyed in by {vrf,ES-id}) L3-NHG.
 * 4. The reason for this indirection (route->L3-NHG, L3-NHG->list-of-VTEPs)
 * is to avoid route updates to the dplane when a remote-ES link flaps i.e.
 * instead of updating all the dependent routes the NHG's contents are updated.
 * This reduces the amount of datplane updates (nhg updates vs. route updates)
 * allowing for a faster failover.
 *
 * XXX - can the L3 SVI index change without change in vpn->bgp_vrf
 * association? If yes we need to handle that by updating all the L3 NHGs
 * in that VRF.
 */
/******************************** L3 NHG management *************************/
/* 【函数】bgp_evpn_l3nhg_zebra_add_v4_or_v6 —— 向 zebra 下发/新增一个 L3-NHG。
 * v4_nhg=true 用 es_vrf->nhg_id(v4 组)，false 用 v6_nhg_id(v6 组)。
 * 要点：
 *   - 若 nhg_id==0(未分配/不走 NHG) 直接返回；
 *   - 只把 ACTIVE 的 ES-VTEP 作为下一跳，且条数受 MULTIPATH_NUM 限制；
 *   - V4 VTEP 进 v4 组用原址，进 v6 组用 v4-mapped-v6 地址；V6 VTEP 只进 v6 组；
 *   - 每个 nexthop 的 vrf/ifindex(onlink, 指向 l3vni_svi)/weight 相同，仅网关 IP 不同。
 */
static void bgp_evpn_l3nhg_zebra_add_v4_or_v6(struct bgp_evpn_es_vrf *es_vrf,
					      bool v4_nhg)
{
	uint32_t nhg_id = v4_nhg ? es_vrf->nhg_id : es_vrf->v6_nhg_id;
	struct bgp_evpn_es *es = es_vrf->es;
	struct listnode *node;
	struct bgp_evpn_es_vtep *es_vtep;
	struct nexthop nh;
	struct zapi_nexthop *api_nh;
	struct zapi_nhg api_nhg = {};

	/* Skip installation of L3-NHG if host routes used */
	if (!nhg_id)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vrf %u %s nhg %u to zebra", es->esi_str,
			   es_vrf->bgp_vrf->vrf_id,
			   v4_nhg ? "v4_nhg" : "v6_nhg", nhg_id);

	frrtrace(4, frr_bgp, evpn_mh_nhg_zsend, true, v4_nhg, nhg_id, es_vrf);

	/* only the gateway ip changes for each NH. rest of the params
	 * are constant
	 */
	memset(&nh, 0, sizeof(nh));
	nh.vrf_id = es_vrf->bgp_vrf->vrf_id;
	nh.flags = NEXTHOP_FLAG_ONLINK;
	nh.ifindex = es_vrf->bgp_vrf->l3vni_svi_ifindex;
	nh.weight = 1;
	nh.type =
		v4_nhg ? NEXTHOP_TYPE_IPV4_IFINDEX : NEXTHOP_TYPE_IPV6_IFINDEX;

	api_nhg.id = nhg_id;
	for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list, node, es_vtep)) {
		if (!CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE))
			continue;

		/* Don't overrun the zapi buffer. */
		if (api_nhg.nexthop_num == MULTIPATH_NUM)
			break;

		/* for V4 VTEP sync v4_nhg or v6 mapped v6_nhg
		 * for V6 VTEP sync V6 nhg and v4 is noop.
		 */
		if (IS_IPADDR_V4(&es_vtep->vtep_ip)) {
			/* overwrite the gw */
			if (v4_nhg)
				nh.gate.ipv4 = es_vtep->vtep_ip.ipaddr_v4;
			else
				ipv4_to_ipv4_mapped_ipv6(&nh.gate.ipv6, es_vtep->vtep_ip.ipaddr_v4);
		} else if (IS_IPADDR_V6(&es_vtep->vtep_ip)) {
			if (v4_nhg)
				nh.gate.ipv4 = es_vtep->vtep_ip.ipaddr_v4;
			else
				IPV6_ADDR_COPY(&nh.gate.ipv6, &es_vtep->vtep_ip.ipaddr_v6);
		}

		/* convert to zapi format */
		api_nh = &api_nhg.nexthops[api_nhg.nexthop_num];
		zapi_nexthop_from_nexthop(api_nh, &nh);

		++api_nhg.nexthop_num;
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("nhg %u %pNHv vtep %pIA l3-svi %d", api_nhg.id, &nh,
				   &es_vtep->vtep_ip, es_vrf->bgp_vrf->l3vni_svi_ifindex);

		frrtrace(3, frr_bgp, evpn_mh_nh_zsend, nhg_id, es_vtep, es_vrf);
	}

	if (!api_nhg.nexthop_num)
		return;

	zclient_nhg_send(bgp_zclient, ZEBRA_NHG_ADD, &api_nhg);
}

/* 【函数】bgp_evpn_l3nhg_zebra_ok —— 判断“此刻能否向 zebra 下发 NHG”：
 * 需全局开启 host_routes_use_l3nhg 且 bgp_zclient 套接字已就绪。 */
static bool bgp_evpn_l3nhg_zebra_ok(struct bgp_evpn_es_vrf *es_vrf)
{
	if (!bgp_mh_info->host_routes_use_l3nhg)
		return false;

	/* Check socket. */
	if (!bgp_zclient || bgp_zclient->sock < 0)
		return false;

	return true;
}

/* 【函数】bgp_evpn_l3nhg_zebra_add —— 同时下发 v4 与 v6 两套 L3-NHG(add)。 */
static void bgp_evpn_l3nhg_zebra_add(struct bgp_evpn_es_vrf *es_vrf)
{
	if (!bgp_evpn_l3nhg_zebra_ok(es_vrf))
		return;

	bgp_evpn_l3nhg_zebra_add_v4_or_v6(es_vrf, true /*v4_nhg*/);
	bgp_evpn_l3nhg_zebra_add_v4_or_v6(es_vrf, false /*v4_nhg*/);
}

/* 【函数】bgp_evpn_l3nhg_zebra_del_v4_or_v6 —— 向 zebra 删除指定(v4/v6)的 L3-NHG。
 * 只发 NHG id，不重填下一跳内容。 */
static void bgp_evpn_l3nhg_zebra_del_v4_or_v6(struct bgp_evpn_es_vrf *es_vrf,
					      bool v4_nhg)
{
	struct zapi_nhg api_nhg = {};

	api_nhg.id = v4_nhg ? es_vrf->nhg_id : es_vrf->v6_nhg_id;

	/* Skip installation of L3-NHG if host routes used */
	if (!api_nhg.id)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vrf %u %s nhg %u to zebra",
			   es_vrf->es->esi_str, es_vrf->bgp_vrf->vrf_id,
			   v4_nhg ? "v4_nhg" : "v6_nhg", api_nhg.id);


	frrtrace(4, frr_bgp, evpn_mh_nhg_zsend, false, v4_nhg, api_nhg.id,
		 es_vrf);

	zclient_nhg_send(bgp_zclient, ZEBRA_NHG_DEL, &api_nhg);
}

/* 【函数】bgp_evpn_l3nhg_zebra_del —— 同时删除 v4 与 v6 两套 L3-NHG。 */
static void bgp_evpn_l3nhg_zebra_del(struct bgp_evpn_es_vrf *es_vrf)
{
	if (!bgp_evpn_l3nhg_zebra_ok(es_vrf))
		return;

	bgp_evpn_l3nhg_zebra_del_v4_or_v6(es_vrf, true /*v4_nhg*/);
	bgp_evpn_l3nhg_zebra_del_v4_or_v6(es_vrf, false /*v4_nhg*/);
}

/* 【函数】bgp_evpn_l3nhg_deactivate —— 去激活该 ES-VRF 的 NHG：清 NHG_ACTIVE 标志、
 * 从 zebra 删除 NHG，并通知依赖它的 MAC-IP 路径重新评估。若本就没激活则直接返回。 */
static void bgp_evpn_l3nhg_deactivate(struct bgp_evpn_es_vrf *es_vrf)
{
	if (!CHECK_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE))
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vrf %u nhg %u de-activate",
			   es_vrf->es->esi_str, es_vrf->bgp_vrf->vrf_id,
			   es_vrf->nhg_id);
	bgp_evpn_l3nhg_zebra_del(es_vrf);
	UNSET_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE);
	/* MAC-IPs can now be installed via the L3NHG */
	bgp_evpn_es_path_update_on_es_vrf_chg(es_vrf, "l3nhg-deactivate");
}

/* 【函数】bgp_evpn_l3nhg_activate —— 激活该 ES-VRF 的 NHG。
 * 若 ES 当前没有 ACTIVE VTEP，则转为去激活并返回。update=true 表示“已激活但
 * VTEP 集合变了”，需要重新下发 NHG(否则已激活且非 update 时直接返回，避免重复)。
 * 首次激活会置 NHG_ACTIVE 并通知 MAC-IP 路径重新评估。 */
static void bgp_evpn_l3nhg_activate(struct bgp_evpn_es_vrf *es_vrf, bool update)
{
	if (!bgp_evpn_es_get_active_vtep_cnt(es_vrf->es)) {
		bgp_evpn_l3nhg_deactivate(es_vrf);
		return;
	}

	if (CHECK_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE)) {
		if (!update)
			return;
	} else {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("es %s vrf %u nhg %u activate",
				   es_vrf->es->esi_str, es_vrf->bgp_vrf->vrf_id,
				   es_vrf->nhg_id);
		SET_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE);
		/* MAC-IPs can now be installed via the L3NHG */
		bgp_evpn_es_path_update_on_es_vrf_chg(es_vrf, "l3nhg_activate");
	}

	bgp_evpn_l3nhg_zebra_add(es_vrf);
}

/* 【函数】bgp_evpn_l3nhg_update_on_vtep_chg —— 当某 ES 有 VTEP 被激活/去激活时，
 * 遍历该 ES 关联的所有 ES-VRF，逐个以 update=true 重新激活/刷新其 NHG。 */
/* when a VTEP is activated or de-activated against an ES associated
 * VRFs' NHG needs to be updated
 */
static void bgp_evpn_l3nhg_update_on_vtep_chg(struct bgp_evpn_es *es)
{
	struct bgp_evpn_es_vrf *es_vrf;
	struct listnode *es_vrf_node;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s nhg update on vtep chg", es->esi_str);

	for (ALL_LIST_ELEMENTS_RO(es->es_vrf_list, es_vrf_node, es_vrf))
		bgp_evpn_l3nhg_activate(es_vrf, true /* update */);
}

/* 【函数】bgp_es_vrf_rb_cmp —— 每租户 VRF 的 ES-VRF 红黑树比较函数：按 ESI 字节序
 * 比较(memcmp)，即同一 VRF 下按 ESI 唯一。 */
/* compare ES-IDs for the ES-VRF RB tree maintained per-VRF */
static int bgp_es_vrf_rb_cmp(const struct bgp_evpn_es_vrf *es_vrf1,
			     const struct bgp_evpn_es_vrf *es_vrf2)
{
	return memcmp(&es_vrf1->es->esi, &es_vrf2->es->esi, ESI_BYTES);
}
RB_GENERATE(bgp_es_vrf_rb_head, bgp_evpn_es_vrf, rb_node, bgp_es_vrf_rb_cmp);

/* 【函数】bgp_evpn_vrf_es_init —— 初始化某租户 VRF 的 ES-VRF 红黑树(创建 VRF 时调用)。 */
/* Initialize the ES tables maintained per-tenant vrf */
void bgp_evpn_vrf_es_init(struct bgp *bgp_vrf)
{
	/* Initialize the ES-VRF RB tree */
	RB_INIT(bgp_es_vrf_rb_head, &bgp_vrf->es_vrf_rb_tree);
}

/* 【函数】bgp_evpn_es_vrf_find —— 在 bgp_vrf 的 ES-VRF 红黑树里按 ESI 查找 ES-VRF。
 * 用一个临时栈对象(仅填 es 指针)作查找键，比较函数只用到 es->esi。 */
/* find the ES-VRF in the per-VRF RB tree */
static struct bgp_evpn_es_vrf *bgp_evpn_es_vrf_find(struct bgp_evpn_es *es,
						    struct bgp *bgp_vrf)
{
	struct bgp_evpn_es_vrf es_vrf;

	es_vrf.es = es;

	return RB_FIND(bgp_es_vrf_rb_head, &bgp_vrf->es_vrf_rb_tree, &es_vrf);
}

/* 【函数】bgp_evpn_es_vrf_create —— 新建一个 ES-VRF：分配内存、插入 VRF-ESI 红黑树
 * 与 ES 的 es_vrf_list、为 v4/v6 各申请一个 NHG id，随后尝试激活 NHG，并让该 VRF
 * 里可能已关联此目的 ES 的路径重新评估。ref_cnt 初始为 0(由调用者 ref 时递增)。 */
/* allocate a new ES-VRF and setup L3NHG for it */
static struct bgp_evpn_es_vrf *bgp_evpn_es_vrf_create(struct bgp_evpn_es *es,
						      struct bgp *bgp_vrf)
{
	struct bgp_evpn_es_vrf *es_vrf;

	es_vrf = XCALLOC(MTYPE_BGP_EVPN_ES_VRF, sizeof(*es_vrf));

	es_vrf->es = es;
	es_vrf->bgp_vrf = bgp_vrf;
	es_vrf->ref_cnt = 0;
	es_vrf->flags = 0;

	/* insert into the VRF-ESI rb tree */
	RB_INSERT(bgp_es_vrf_rb_head, &bgp_vrf->es_vrf_rb_tree, es_vrf);

	/* add to the ES's VRF list */
	listnode_init(&es_vrf->es_listnode, es_vrf);
	listnode_add(es->es_vrf_list, &es_vrf->es_listnode);

	/* setup the L3 NHG id for the ES */
	es_vrf->nhg_id = bgp_nhg_id_alloc();
	es_vrf->v6_nhg_id = bgp_nhg_id_alloc();

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vrf %u nhg %u v6_nhg %d create", es->esi_str,
			   bgp_vrf->vrf_id, es_vrf->nhg_id, es_vrf->v6_nhg_id);
	bgp_evpn_l3nhg_activate(es_vrf, false /* update */);

	/* update paths in the VRF that may already be associated with
	 * this destination ES
	 */
	bgp_evpn_es_path_update_on_es_vrf_chg(es_vrf, "es-vrf-create");

	return es_vrf;
}

/* 【函数】bgp_evpn_es_vrf_delete —— 删除 ES-VRF：先 deactivate 并释放 v4/v6 的 NHG id，
 * 再从 ES 的 es_vrf_list 与 VRF-ESI 红黑树摘除，通知依赖路径重评估，最后释放内存。
 * 注意此处是“真正删除”，仅在 ref_cnt 归零时由 deref 调用。 */
/* remove the L3-NHG associated with the ES-VRF and free it */
static void bgp_evpn_es_vrf_delete(struct bgp_evpn_es_vrf *es_vrf)
{
	struct bgp_evpn_es *es = es_vrf->es;
	struct bgp *bgp_vrf = es_vrf->bgp_vrf;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s vrf %u nhg %u delete", es->esi_str,
			   bgp_vrf->vrf_id, es_vrf->nhg_id);

	/* Remove the NHG resources */
	bgp_evpn_l3nhg_deactivate(es_vrf);
	if (es_vrf->nhg_id)
		bgp_nhg_id_free(es_vrf->nhg_id);
	es_vrf->nhg_id = 0;
	if (es_vrf->v6_nhg_id)
		bgp_nhg_id_free(es_vrf->v6_nhg_id);
	es_vrf->v6_nhg_id = 0;

	/* remove from the ES's VRF list */
	list_delete_node(es->es_vrf_list, &es_vrf->es_listnode);

	/* remove from the VRF-ESI rb tree */
	RB_REMOVE(bgp_es_vrf_rb_head, &bgp_vrf->es_vrf_rb_tree, es_vrf);

	/* update paths in the VRF that may already be associated with
	 * this destination ES
	 */
	bgp_evpn_es_path_update_on_es_vrf_chg(es_vrf, "es-vrf-delete");

	XFREE(MTYPE_BGP_EVPN_ES_VRF, es_vrf);
}

/* 【函数】bgp_evpn_es_vrf_deref —— 解除 es_evi 对其 ES-VRF 的引用：ref_cnt--，
 * 归零则删除该 ES-VRF。若本就没关联则直接返回。 */
/* deref and delete if there are no references */
void bgp_evpn_es_vrf_deref(struct bgp_evpn_es_evi *es_evi)
{
	struct bgp_evpn_es_vrf *es_vrf = es_evi->es_vrf;

	if (!es_vrf)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es-evi %s vni %u vrf %u de-ref",
			   es_evi->es->esi_str, es_evi->vpn->vni,
			   es_vrf->bgp_vrf->vrf_id);

	es_evi->es_vrf = NULL;
	if (es_vrf->ref_cnt > 0)
		--es_vrf->ref_cnt;

	if (!es_vrf->ref_cnt)
		bgp_evpn_es_vrf_delete(es_vrf);
}

/* 【函数】bgp_evpn_es_vrf_ref —— 让 es_evi 关联(引用)到 bgp_vrf 对应的 ES-VRF。
 * 若 es_evi 已关联同一 VRF 则直接返回；否则先 deref 旧的，若 bgp_vrf 为空(取消关联)
 * 则结束；再 find-or-create 新 ES-VRF 并 ref_cnt++。 */
/* find or create and reference */
void bgp_evpn_es_vrf_ref(struct bgp_evpn_es_evi *es_evi, struct bgp *bgp_vrf)
{
	struct bgp_evpn_es *es = es_evi->es;
	struct bgp_evpn_es_vrf *es_vrf = es_evi->es_vrf;
	struct bgp *old_bgp_vrf = NULL;

	if (es_vrf)
		old_bgp_vrf = es_vrf->bgp_vrf;

	if (old_bgp_vrf == bgp_vrf)
		return;

	/* deref the old ES-VRF */
	bgp_evpn_es_vrf_deref(es_evi);

	if (!bgp_vrf)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es-evi %s vni %u vrf %u ref", es_evi->es->esi_str,
			   es_evi->vpn->vni, bgp_vrf->vrf_id);

	/* find-create the new ES-VRF */
	es_vrf = bgp_evpn_es_vrf_find(es, bgp_vrf);
	if (!es_vrf)
		es_vrf = bgp_evpn_es_vrf_create(es, bgp_vrf);

	es_evi->es_vrf = es_vrf;
	++es_vrf->ref_cnt;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es-vrf %s vrf %u evi ref_cnt incremented to %d", es_vrf->es->esi_str,
			   es_vrf->bgp_vrf->vrf_id, es_vrf->ref_cnt);
}

/* 【函数】bgp_evpn_es_evi_vrf_deref —— 当 L2-VNI 与 L3-VNI/VRF 的关联发生变化时，
 * 遍历该 VNI 的所有 ES-EVI，逐个解除对各自 ES-VRF 的引用(可能触发删除)。 */
/* When the L2-VNI is associated with a L3-VNI/VRF update all the
 * associated ES-EVI entries
 */
void bgp_evpn_es_evi_vrf_deref(struct bgpevpn *vpn)
{
	struct bgp_evpn_es_evi *es_evi;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es-vrf de-ref for vni %u", vpn->vni);

	RB_FOREACH (es_evi, bgp_es_evi_rb_head, &vpn->es_evi_rb_tree)
		bgp_evpn_es_vrf_deref(es_evi);
}
/* 【函数】bgp_evpn_es_evi_vrf_ref —— 遍历该 VNI 的所有 ES-EVI，逐个关联(引用)到
 * vpn->bgp_vrf 对应的 ES-VRF(见 bgp_evpn_es_vrf_ref)。与上面的 deref 配对使用。 */
void bgp_evpn_es_evi_vrf_ref(struct bgpevpn *vpn)
{
	struct bgp_evpn_es_evi *es_evi;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es-vrf ref for vni %u", vpn->vni);

	RB_FOREACH (es_evi, bgp_es_evi_rb_head, &vpn->es_evi_rb_tree)
		bgp_evpn_es_vrf_ref(es_evi, vpn->bgp_vrf);
}

/* 【函数】bgp_evpn_es_vrf_use_nhg —— 判定(esi, bgp_vrf) 对应的主机路由能否/是否
 * 使用 L3-NHG，并通过出参回传：
 *   use_l3nhg      : 是否需要走 L3-NHG(找到 ES 且找到 ES-VRF 时为 true)；
 *   is_l3nhg_active: 该 ES-VRF 的 NHG 是否已激活(NHG_ACTIVE 标志)；
 *   es_vrf_p(可空) : 回传找到的 ES-VRF 指针，供调用方取 nhg_id。
 * 若全局未开 host_routes_use_l3nhg、或找不到 ES / ES-VRF，则各出参保持默认(false/NULL)。 */
/* 1. If ES-VRF is not present install the host route with the exploded/flat
 * multi-path list.
 * 2. If ES-VRF is present -
 * - if L3NHG has not been activated for the ES-VRF (this could be because
 *   all the PEs attached to the VRF are down) do not install the route
 *   in zebra.
 * - if L3NHG has been activated install the route via that L3NHG
 */
void bgp_evpn_es_vrf_use_nhg(struct bgp *bgp_vrf, esi_t *esi, bool *use_l3nhg,
			     bool *is_l3nhg_active,
			     struct bgp_evpn_es_vrf **es_vrf_p)
{
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_vrf *es_vrf;

	if (!bgp_mh_info->host_routes_use_l3nhg)
		return;

	es = bgp_evpn_es_find(esi);
	if (!es)
		return;

	es_vrf = bgp_evpn_es_vrf_find(es, bgp_vrf);
	if (!es_vrf)
		return;

	*use_l3nhg = true;
	if (CHECK_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE))
		*is_l3nhg_active = true;
	if (es_vrf_p)
		*es_vrf_p = es_vrf;
}

/* 【函数】bgp_evpn_path_es_use_nhg —— 判断一条 MAC-IP(pi) 能否用 L3-NHG 下发，
 * 适用则把选定的 NHG id 写到 *nhg_p 并返回 true；否则返回 false(需退回 legacy
 * “展开式多路径”下发)。返回 true 但 *nhg_p==0 表示 NHG 尚未激活、暂不能装路由。
 * 不适用 NHG 的情形：来自其它 VRF 泄漏的路由、无 EVPN 父路径、非 MAC-IP、
 * 非 ES 路径(zero_esi)、d-vni 多路径、多路径中各路径 ESI 不一致。 */
/* returns false if legacy-exploded mp needs to be used for route install */
bool bgp_evpn_path_es_use_nhg(struct bgp *bgp_vrf, struct bgp_path_info *pi,
			      uint32_t *nhg_p)
{
	esi_t *esi;
	struct bgp_evpn_es_vrf *es_vrf = NULL;
	struct bgp_path_info *parent_pi;
	struct bgp_dest *bd;
	struct prefix_evpn *evp;
	struct bgp_path_info *mpinfo;
	bool use_l3nhg = false;
	bool is_l3nhg_active = false;
	bool is_ipv6_vtep = false;
	uint8_t nhfamily;

	*nhg_p = 0;

	/* we don't support NHG for routes leaked from another VRF yet */
	if (pi->extra && pi->extra->vrfleak && pi->extra->vrfleak->bgp_orig)
		return false;

	parent_pi = get_route_parent_evpn(pi);
	if (!parent_pi)
		return false;

	bd = parent_pi->net;
	if (!bd)
		return false;

	evp = (struct prefix_evpn *)&bd->rn->p;
	if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return false;

	/* non-es path, use legacy-exploded multipath */
	esi = bgp_evpn_attr_get_esi(parent_pi->attr);
	if (!memcmp(esi, zero_esi, sizeof(*esi)))
		return false;

	/* we don't support NHG for d-vni yet */
	if (bgp_evpn_mpath_has_dvni(bgp_vrf, pi))
		return false;

	bgp_evpn_es_vrf_use_nhg(bgp_vrf, esi, &use_l3nhg, &is_l3nhg_active,
				&es_vrf);

	/* L3NHG support is disabled, use legacy-exploded multipath */
	if (!use_l3nhg)
		return false;

	/* if the NHG has not been installed we cannot install the route yet,
	 * return a 0-NHG to indicate that
	 */
	if (!is_l3nhg_active)
		return true;

	/* EVPN imported IPv4 and IPv6 route nexthop is IPv6 implies IPv6 VTEP
	 * use v6 nhg.
	 */
	nhfamily = NEXTHOP_FAMILY(pi->attr->mp_nexthop_len);
	if (nhfamily == AF_INET6)
		is_ipv6_vtep = true;

	/* this needs to be set the v6NHG if v6route */
	if (is_evpn_prefix_ipaddr_v6(evp) || is_ipv6_vtep)
		*nhg_p = es_vrf->v6_nhg_id;
	else
		*nhg_p = es_vrf->nhg_id;

	for (mpinfo = bgp_path_info_mpath_next(pi); mpinfo;
	     mpinfo = bgp_path_info_mpath_next(mpinfo)) {
		/* if any of the paths have a different ESI we can't use
		 * the NHG associated with the ES. fallback to legacy-exploded
		 * multipath
		 */
		if (memcmp(esi, bgp_evpn_attr_get_esi(mpinfo->attr),
			   sizeof(*esi)))
			return false;
	}

	return true;
}

/* 【函数】bgp_evpn_es_vrf_show_entry —— 打印单条 ES-VRF：ESI、VRF 名、flags(A=激活)、
 * 两个 NHG id(ipv4NHG/ipv6NHG)、refCount。JSON 与文本两条分支。 */
static void bgp_evpn_es_vrf_show_entry(struct vty *vty,
				       struct bgp_evpn_es_vrf *es_vrf,
				       json_object *json)
{
	struct bgp_evpn_es *es = es_vrf->es;
	struct bgp *bgp_vrf = es_vrf->bgp_vrf;

	if (json) {
		json_object *json_types;

		json_object_string_add(json, "esi", es->esi_str);
		json_object_string_add(json, "vrf", bgp_vrf->name_pretty);

		if (CHECK_FLAG(es_vrf->flags, (BGP_EVPNES_VRF_NHG_ACTIVE))) {
			json_types = json_object_new_array();
			if (CHECK_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE))
				json_array_string_add(json_types, "active");
			json_object_object_add(json, "flags", json_types);
		}

		json_object_int_add(json, "ipv4NHG", es_vrf->nhg_id);
		json_object_int_add(json, "ipv6NHG", es_vrf->v6_nhg_id);
		json_object_int_add(json, "refCount", es_vrf->ref_cnt);
	} else {
		char flags_str[4];

		flags_str[0] = '\0';
		if (CHECK_FLAG(es_vrf->flags, BGP_EVPNES_VRF_NHG_ACTIVE))
			strlcat(flags_str, "A", sizeof(flags_str));

		vty_out(vty, "%-30s %-15s %-5s %-8u %-8u %u\n", es->esi_str,
			bgp_vrf->name_pretty, flags_str, es_vrf->nhg_id,
			es_vrf->v6_nhg_id, es_vrf->ref_cnt);
	}
}

/* 【函数】bgp_evpn_es_vrf_show_es —— 遍历某 ES 的 es_vrf_list，逐条打印其
 * ES-VRF(json_array 非空时收集 JSON)。 */
static void bgp_evpn_es_vrf_show_es(struct vty *vty, json_object *json_array,
				    struct bgp_evpn_es *es)
{
	json_object *json = NULL;
	struct listnode *es_vrf_node;
	struct bgp_evpn_es_vrf *es_vrf;

	for (ALL_LIST_ELEMENTS_RO(es->es_vrf_list, es_vrf_node, es_vrf)) {
		/* create a separate json object for each ES-VRF */
		if (json_array)
			json = json_object_new_object();
		bgp_evpn_es_vrf_show_entry(vty, es_vrf, json);
		/* add ES-VRF to the json array */
		if (json_array)
			json_object_array_add(json_array, json);
	}
}

/* 【函数】bgp_evpn_es_vrf_show —— 打印某 ES 的所有 ES-VRF(表格或 JSON)，非 JSON
 * 时先打印表头。 */
/* Display all ES VRFs */
void bgp_evpn_es_vrf_show(struct vty *vty, bool uj, struct bgp_evpn_es *es)
{
	json_object *json_array = NULL;

	if (uj) {
		/* create an array of ESs */
		json_array = json_object_new_array();
	} else {
		vty_out(vty, "ES-VRF Flags: A Active\n");
		vty_out(vty, "%-30s %-15s %-5s %-8s %-8s %s\n", "ESI", "VRF",
			"Flags", "IPv4-NHG", "IPv6-NHG", "Ref");
	}

	if (es) {
		bgp_evpn_es_vrf_show_es(vty, json_array, es);
	} else {
		RB_FOREACH (es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree)
			bgp_evpn_es_vrf_show_es(vty, json_array, es);
	}

	/* print the array of json-ESs */
	if (uj)
		vty_json(vty, json_array);
}

/* 【函数】bgp_evpn_es_vrf_show_esi —— 打印指定 ESI 那个 ES 的所有 ES-VRF；
 * 找不到则文本分支输出 "ESI not found"。 */
/* Display specific ES VRF */
void bgp_evpn_es_vrf_show_esi(struct vty *vty, esi_t *esi, bool uj)
{
	struct bgp_evpn_es *es;

	es = bgp_evpn_es_find(esi);
	if (es) {
		bgp_evpn_es_vrf_show(vty, uj, es);
	} else {
		if (!uj)
			vty_out(vty, "ESI not found\n");
	}
}

/* ============================================================================
 * 【本章】ES 与 EVI(每 VNI) 的关联 + ES-EVI VTEP 管理
 * ============================================================================
 * 1. ES-EVI 记录按 L2-VNI 组织成一棵红黑树(bgpevpn->es_evi_rb_tree)。
 * 2. 本地 ES-EVI 从 zebra 收到后，BGP 用它通告 EAD-per-EVI(Type-1 EVPN)路由。
 * 3. 远端 ES-EVI 在某个 bgp_evpn_es_evi_vtep 引用它时被创建。
 * 每个 ES-EVI 维护一张远端 VTEP 列表，含：
 *   (a) 已导入 EAD-per-ES(Type-1) 路由的 VTEP；
 *   (b) 已导入 EAD-per-EVI(Type-1) 路由的 VTEP。
 * 同时收到 (a)(b) 两类路由的 VTEP 才被激活(ACTIVE)，激活会在父 ES 上创建 NHG。
 * ============================================================================
 */
/*****************************************************************************/
/* Ethernet Segment to EVI association -
 * 1. The ES-EVI entry is maintained as a RB tree per L2-VNI
 * (bgpevpn->es_evi_rb_tree).
 * 2. Each local ES-EVI entry is rxed from zebra and then used by BGP to
 * advertises an EAD-EVI (Type-1 EVPN) route
 * 3. The remote ES-EVI is created when a bgp_evpn_es_evi_vtep references
 * it.
 */

/* 【函数】bgp_evpn_es_evi_vtep_cmp —— ES-EVI 远端 VTEP 列表的排序比较回调；
 * 以 VTEP 的 IP 地址(ipaddr_cmp)为序，供 listnode_add_sort 维持链表有序。 */
/* A list of remote VTEPs is maintained for each ES-EVI. This list includes -
 * 1. VTEPs for which we have imported the EAD-per-ES Type1 route
 * 2. VTEPs for which we have imported the EAD-per-EVI Type1 route
 * VTEPs for which both routes have been rxed are activated. Activation
 * creates a NHG in the parent ES.
 */
static int bgp_evpn_es_evi_vtep_cmp(void *p1, void *p2)
{
	const struct bgp_evpn_es_evi_vtep *evi_vtep1 = p1;
	const struct bgp_evpn_es_evi_vtep *evi_vtep2 = p2;

	return ipaddr_cmp(&evi_vtep1->vtep_ip, &evi_vtep2->vtep_ip);
}

/* 【函数】bgp_evpn_es_evi_vtep_new —— 新建一个 ES-EVI 远端 VTEP 记录，
 * 记录其 VTEP IP 并有序挂入 es_evi 的 es_evi_vtep_list。返回新对象；
 * 内存由 XCALLOC 分配，失败即终止，不会返回 NULL。 */
static struct bgp_evpn_es_evi_vtep *bgp_evpn_es_evi_vtep_new(
		struct bgp_evpn_es_evi *es_evi, struct ipaddr vtep_ip)
{
	struct bgp_evpn_es_evi_vtep *evi_vtep;

	evi_vtep = XCALLOC(MTYPE_BGP_EVPN_ES_EVI_VTEP, sizeof(*evi_vtep));

	evi_vtep->es_evi = es_evi;
	evi_vtep->vtep_ip = vtep_ip;
	listnode_init(&evi_vtep->es_evi_listnode, evi_vtep);
	listnode_add_sort(es_evi->es_evi_vtep_list, &evi_vtep->es_evi_listnode);

	return evi_vtep;
}

/* 【函数】bgp_evpn_es_evi_vtep_free —— 释放一个 ES-EVI 远端 VTEP 记录；
 * 若仍带 BGP_EVPN_EVI_VTEP_EAD 标志（仍有引用）则直接返回不释放，
 * 否则从父 es_evi 的列表摘除并 XFREE。 */
static void bgp_evpn_es_evi_vtep_free(struct bgp_evpn_es_evi_vtep *evi_vtep)
{
	struct bgp_evpn_es_evi *es_evi = evi_vtep->es_evi;

	if (CHECK_FLAG(evi_vtep->flags, (BGP_EVPN_EVI_VTEP_EAD)))
		/* as long as there is some reference we can't free it */
		return;

	list_delete_node(es_evi->es_evi_vtep_list, &evi_vtep->es_evi_listnode);
	XFREE(MTYPE_BGP_EVPN_ES_EVI_VTEP, evi_vtep);
}

/* 【函数】bgp_evpn_es_evi_vtep_find —— 在 es_evi 的远端 VTEP 列表中
 * 按 IP 线性查找记录；命中返回对象指针，否则返回 NULL。 */
/* check if VTEP is already part of the list */
static struct bgp_evpn_es_evi_vtep *bgp_evpn_es_evi_vtep_find(
		struct bgp_evpn_es_evi *es_evi, struct ipaddr vtep_ip)
{
	struct listnode *node = NULL;
	struct bgp_evpn_es_evi_vtep *evi_vtep;

	for (ALL_LIST_ELEMENTS_RO(es_evi->es_evi_vtep_list, node, evi_vtep)) {
		if (ipaddr_is_same(&evi_vtep->vtep_ip, &vtep_ip))
			return evi_vtep;
	}
	return NULL;
}

/* 【函数】bgp_evpn_es_evi_vtep_re_eval_active —— 重估某远端 VTEP 的 ACTIVE 状态；
 * 依 bgp_mh_info->enable_ead_evi_rx 选定激活条件（EAD-per-ES 加 EAD-per-EVI，
 * 或仅 EAD-per-ES），状态翻转时在父 ES 上增/删对应 ES-VTEP（NHG），
 * 并把父 ES 加入后台一致性检查队列。返回 zebra 发送状态。 */
/* A VTEP can be added as "active" attach to an ES if EAD-per-ES and
 * EAD-per-EVI routes are rxed from it.
 */
static enum zclient_send_status
bgp_evpn_es_evi_vtep_re_eval_active(struct bgp *bgp,
				    struct bgp_evpn_es_evi_vtep *evi_vtep)
{
	bool old_active;
	bool new_active;
	uint32_t ead_activity_flags;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;

	old_active = CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_ACTIVE);

	if (bgp_mh_info->enable_ead_evi_rx)
		/* Both EAD-per-ES and EAD-per-EVI routes must be rxed from a PE
		 * before it can be activated.
		 */
		ead_activity_flags = BGP_EVPN_EVI_VTEP_EAD;
	else
		/* EAD-per-ES is sufficient to activate the PE */
		ead_activity_flags = BGP_EVPN_EVI_VTEP_EAD_PER_ES;

	if (CHECK_FLAG(evi_vtep->flags, ead_activity_flags) ==
	    ead_activity_flags)
		SET_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_ACTIVE);
	else
		UNSET_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_ACTIVE);

	new_active = CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_ACTIVE);

	if (old_active == new_active)
		return ret;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("es %s evi %u vtep %pIA %s",
			   evi_vtep->es_evi->es->esi_str,
			   evi_vtep->es_evi->vpn->vni, &evi_vtep->vtep_ip,
			   new_active ? "active" : "inactive");

	/* add VTEP to parent es */
	if (new_active) {
		evi_vtep->es_vtep =
			bgp_evpn_es_vtep_add(bgp, evi_vtep->es_evi->es,
					     evi_vtep->vtep_ip, false /*esr*/,
					     0, 0, &ret);
	} else {
		if (evi_vtep->es_vtep) {
			ret = bgp_evpn_es_vtep_do_del(bgp, evi_vtep->es_vtep,
						      false /*esr*/);
			evi_vtep->es_vtep = NULL;
		}
	}
	/* queue up the parent es for background consistency checks */
	bgp_evpn_es_cons_checks_pend_add(evi_vtep->es_evi->es);

	return ret;
}

/* 【函数】bgp_evpn_es_evi_vtep_add —— 为 ES-EVI 添加/更新一个远端 VTEP；
 * ead_es 为真置 EAD_PER_ES 标志，否则置 EAD_PER_EVI 标志，
 * 随后调用 re_eval_active 决定是否激活；返回其 zebra 发送状态。
 * 坑：若记录不存在会先新建，故对本函数而言"添加"总是成功。 */
static enum zclient_send_status
bgp_evpn_es_evi_vtep_add(struct bgp *bgp, struct bgp_evpn_es_evi *es_evi,
			 struct ipaddr vtep_ip, bool ead_es)
{
	struct bgp_evpn_es_evi_vtep *evi_vtep;

	evi_vtep = bgp_evpn_es_evi_vtep_find(es_evi, vtep_ip);

	if (!evi_vtep)
		evi_vtep = bgp_evpn_es_evi_vtep_new(es_evi, vtep_ip);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("add es %s evi %u vtep %pIA %s",
			   evi_vtep->es_evi->es->esi_str,
			   evi_vtep->es_evi->vpn->vni, &evi_vtep->vtep_ip,
			   ead_es ? "ead_es" : "ead_evi");

	frrtrace(4, frr_bgp, evpn_mh_es_evi_vtep_add, &evi_vtep->es_evi->es->esi,
		 evi_vtep->es_evi->vpn->vni, &evi_vtep->vtep_ip, ead_es);

	if (ead_es)
		SET_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_ES);
	else
		SET_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_EVI);

	return bgp_evpn_es_evi_vtep_re_eval_active(bgp, evi_vtep);
}

/* 【函数】bgp_evpn_es_evi_vtep_del —— 撤销某远端 VTEP 在 ES-EVI 上的对应标志；
 * 未找到该 VTEP 记录则直接返回 SUCCESS；清除 EAD_PER_ES / EAD_PER_EVI 后
 * 重估激活状态，再尝试释放该记录。返回重估过程产生的 zebra 发送状态。 */
static enum zclient_send_status
bgp_evpn_es_evi_vtep_del(struct bgp *bgp, struct bgp_evpn_es_evi *es_evi,
			 struct ipaddr vtep_ip, bool ead_es)
{
	struct bgp_evpn_es_evi_vtep *evi_vtep;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;

	evi_vtep = bgp_evpn_es_evi_vtep_find(es_evi, vtep_ip);
	if (!evi_vtep)
		return ret;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("del es %s evi %u vtep %pIA %s", evi_vtep->es_evi->es->esi_str,
			   evi_vtep->es_evi->vpn->vni, &evi_vtep->vtep_ip,
			   ead_es ? "ead_es" : "ead_evi");

	frrtrace(4, frr_bgp, evpn_mh_es_evi_vtep_del, &evi_vtep->es_evi->es->esi,
		 evi_vtep->es_evi->vpn->vni, &evi_vtep->vtep_ip, ead_es);

	if (ead_es)
		UNSET_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_ES);
	else
		UNSET_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_EVI);

	ret = bgp_evpn_es_evi_vtep_re_eval_active(bgp, evi_vtep);
	bgp_evpn_es_evi_vtep_free(evi_vtep);

	return ret;
}

/* 【函数】bgp_es_evi_rb_cmp —— 每个 L2-VNI 下 ES-EVI 红黑树的键比较函数：
 * 以父 ES 的 ESI（ESI_BYTES 字节 memcmp）作为排序键。 */
/* compare ES-IDs for the ES-EVI RB tree maintained per-VNI */
static int bgp_es_evi_rb_cmp(const struct bgp_evpn_es_evi *es_evi1,
		const struct bgp_evpn_es_evi *es_evi2)
{
	return memcmp(&es_evi1->es->esi, &es_evi2->es->esi, ESI_BYTES);
}
/* RB_GENERATE —— 依据上面的比较函数，为 bgp_evpn_es_evi 生成以 ESI 为键的
 * 红黑树类型 bgp_es_evi_rb_head 及其查找/插入/删除例程。 */
RB_GENERATE(bgp_es_evi_rb_head, bgp_evpn_es_evi, rb_node, bgp_es_evi_rb_cmp);

/* 【函数】bgp_evpn_es_evi_find —— 在指定 L2-VNI(vpn) 的 ES-EVI 红黑树中
 * 按 es 的 ESI 查找 ES-EVI；临时用 es 字段作键，找到返回对象否则 NULL。 */
/* find the ES-EVI in the per-L2-VNI RB tree */
static struct bgp_evpn_es_evi *bgp_evpn_es_evi_find(struct bgp_evpn_es *es,
		struct bgpevpn *vpn)
{
	struct bgp_evpn_es_evi es_evi;

	es_evi.es = es;

	return RB_FIND(bgp_es_evi_rb_head, &vpn->es_evi_rb_tree, &es_evi);
}

/* 【函数】bgp_evpn_es_evi_new —— 新建 ES-EVI，插入 L2-VNI 的 ESI 红黑树、
 * 挂入父 ES 的 es_evi_list，初始化远端 VTEP 列表，并对 bgp_vrf 加一次引用。
 * 返回新对象；VTEP 列表创建失败时释放对象并返回 NULL。 */
/* allocate a new ES-EVI and insert it into the per-L2-VNI and per-ES
 * tables.
 */
static struct bgp_evpn_es_evi *bgp_evpn_es_evi_new(struct bgp_evpn_es *es,
		struct bgpevpn *vpn)
{
	struct bgp_evpn_es_evi *es_evi;

	es_evi = XCALLOC(MTYPE_BGP_EVPN_ES_EVI, sizeof(*es_evi));
	es_evi->es = es;
	es_evi->vpn = vpn;
	es_evi->flags = 0;

	/* Initialise the VTEP list */
	es_evi->es_evi_vtep_list = list_new();
	if (!es_evi->es_evi_vtep_list) {
		flog_err(EC_BGP_ES_CREATE, "Failed to create VTEP list for ES-EVI ES %s VNI %u",
			 es->esi_str, vpn->vni);
		XFREE(MTYPE_BGP_EVPN_ES_EVI, es_evi);
		return NULL;
	}
	listset_app_node_mem(es_evi->es_evi_vtep_list);
	es_evi->es_evi_vtep_list->cmp = bgp_evpn_es_evi_vtep_cmp;

	/* insert into the VNI-ESI rb tree */
	RB_INSERT(bgp_es_evi_rb_head, &vpn->es_evi_rb_tree, es_evi);

	/* add to the ES's VNI list */
	listnode_init(&es_evi->es_listnode, es_evi);
	listnode_add(es->es_evi_list, &es_evi->es_listnode);

	bgp_evpn_es_vrf_ref(es_evi, vpn->bgp_vrf);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("Created ES-EVI for ES %s VNI %u", es->esi_str, vpn->vni);

	return es_evi;
}

/* 【函数】bgp_evpn_es_evi_free_internal —— 释放 ES-EVI 的内部实现；
 * force 为假且有 LOCAL 或 REMOTE 引用时不释放、直接返回原对象；
 * 否则删 frag、解除 vrf 引用、从 ES 列表与红黑树摘除、释放 VTEP 列表并 XFREE。
 * 成功释放返回 NULL，保留则返回原 es_evi。 */
/* remove the ES-EVI from the per-L2-VNI and per-ES tables and free
 * up the memory.
 */
static struct bgp_evpn_es_evi *bgp_evpn_es_evi_free_internal(struct bgp_evpn_es_evi *es_evi,
							     bool force)
{
	struct bgp_evpn_es *es = es_evi->es;
	struct bgpevpn *vpn = es_evi->vpn;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("Freeing ES-EVI for ES %s VNI %u flags 0x%x%s", es->esi_str, vpn->vni,
			   es_evi->flags, force ? " (forced)" : "");

	/* cannot free the element as long as there is a local or remote
	 * reference (unless force is set during shutdown)
	 */
	if (!force && CHECK_FLAG(es_evi->flags, (BGP_EVPNES_EVI_LOCAL | BGP_EVPNES_EVI_REMOTE)))
		return es_evi;
	bgp_evpn_es_frag_evi_del(es_evi, false);
	bgp_evpn_es_vrf_deref(es_evi);

	/* remove from the ES's VNI list */
	list_delete_node(es->es_evi_list, &es_evi->es_listnode);

	/* remove from the VNI-ESI rb tree */
	RB_REMOVE(bgp_es_evi_rb_head, &vpn->es_evi_rb_tree, es_evi);

	/* free the VTEP list */
	list_delete(&es_evi->es_evi_vtep_list);

	/* Set flags max value */
	es_evi->flags = 0xFFFF;

	/* remove from the VNI-ESI rb tree */
	XFREE(MTYPE_BGP_EVPN_ES_EVI, es_evi);

	return NULL;
}

/* 【函数】bgp_evpn_es_evi_free —— 普通（非强制）释放的封装，
 * 直接转调 bgp_evpn_es_evi_free_internal(es_evi, false)。 */
/* Wrapper function for normal operation (non-forced cleanup) */
static struct bgp_evpn_es_evi *bgp_evpn_es_evi_free(struct bgp_evpn_es_evi *es_evi)
{
	return bgp_evpn_es_evi_free_internal(es_evi, false);
}

/* 【函数】bgp_evpn_es_evi_local_info_set —— 置位本地标志 BGP_EVPNES_EVI_LOCAL，
 * 把 ES-EVI 挂入 vpn 的 local_es_evi_list，并加入 es_frag；已置位则直接返回。 */
/* init local info associated with the ES-EVI */
static void bgp_evpn_es_evi_local_info_set(struct bgp_evpn_es_evi *es_evi)
{
	struct bgpevpn *vpn = es_evi->vpn;

	if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("Setting local info for ES-EVI ES %s VNI %u", es_evi->es->esi_str,
			   vpn->vni);

	SET_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL);
	listnode_init(&es_evi->l2vni_listnode, es_evi);
	listnode_add(vpn->local_es_evi_list, &es_evi->l2vni_listnode);
	bgp_evpn_es_frag_evi_add(es_evi);
}

/* 【函数】bgp_evpn_es_evi_local_info_clear —— 清除本地标志并从 vpn 的
 * local_es_evi_list 摘除，随后尝试释放该 ES-EVI；返回释放结果
 * （成功释放为 NULL，因仍有引用而保留则为原对象）。 */
/* clear any local info associated with the ES-EVI */
static struct bgp_evpn_es_evi *
bgp_evpn_es_evi_local_info_clear(struct bgp_evpn_es_evi *es_evi)
{
	struct bgpevpn *vpn = es_evi->vpn;

	UNSET_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL);
	list_delete_node(vpn->local_es_evi_list, &es_evi->l2vni_listnode);

	return bgp_evpn_es_evi_free(es_evi);
}

/* 【函数】bgp_evpn_es_evi_remote_info_re_eval —— 依远端 VTEP 列表是否非空
 * 重估 ES-EVI 的 REMOTE 标志：非空则置位并递增父 ES 的 remote_es_evi_cnt；
 * 变空则清位、递减计数、释放该 ES-EVI，并让父 ES 重估其 REMOTE 状态。 */
/* eval remote info associated with the ES */
static void bgp_evpn_es_evi_remote_info_re_eval(struct bgp_evpn_es_evi *es_evi)
{
	struct bgp_evpn_es *es = es_evi->es;

	/* if there are remote VTEPs the ES-EVI is classified as "remote" */
	if (listcount(es_evi->es_evi_vtep_list)) {
		if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE)) {
			SET_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE);
			++es->remote_es_evi_cnt;
			/* set remote on the parent es */
			bgp_evpn_es_remote_info_re_eval(es);
		}
	} else {
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE)) {
			UNSET_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE);
			if (es->remote_es_evi_cnt)
				--es->remote_es_evi_cnt;
			bgp_evpn_es_evi_free(es_evi);
			/* check if "remote" can be cleared from the
			 * parent es.
			 */
			bgp_evpn_es_remote_info_re_eval(es);
		}
	}
}

/* 【函数】bgp_evpn_local_es_evi_do_del —— 删除本地 ES-EVI 的本地信息；
 * 非 LOCAL 直接返回原对象；否则从 frag 摘除、按需刷新 EAD-ES、撤销并删除
 * EAD-EVI 路由，最后清除本地信息并返回其释放结果。 */
static struct bgp_evpn_es_evi *
bgp_evpn_local_es_evi_do_del(struct bgp_evpn_es_evi *es_evi)
{
	struct prefix_evpn p;
	struct bgp_evpn_es *es = es_evi->es;
	struct bgp *bgp;

	if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
		return es_evi;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("del local es %s evi %u",
				es_evi->es->esi_str,
				es_evi->vpn->vni);

	bgp = bgp_get_evpn();

	/* remove the es_evi from the es_frag before sending the update */
	bgp_evpn_es_frag_evi_del(es_evi, true);
	if (bgp) {
		/* update EAD-ES with new list of VNIs */
		if (bgp_evpn_local_es_is_active(es))
			bgp_evpn_ead_es_route_update(bgp, es);

		/* withdraw and delete EAD-EVI */
		if (CHECK_FLAG(es->flags, BGP_EVPNES_ADV_EVI)) {
			build_evpn_type1_prefix(&p, BGP_EVPN_AD_EVI_ETH_TAG,
					&es->esi, es->originator_ip);
			if (bgp_evpn_ead_evi_route_delete(bgp, es, es_evi->vpn,
							  &p))
				flog_err(EC_BGP_EVPN_ROUTE_DELETE,
					"%u: EAD-EVI route deletion failure for ESI %s VNI %u",
					bgp->vrf_id, es->esi_str,
					es_evi->vpn->vni);
		}
	}

	return bgp_evpn_es_evi_local_info_clear(es_evi);
}

/* 【函数】bgp_evpn_local_es_evi_del —— 对外接口：按 (esi, vni) 删除本地
 * ES-EVI。ES / VNI / ES-EVI 任一缺失都会记错误日志并返回 -1，成功返回 0。 */
int bgp_evpn_local_es_evi_del(struct bgp *bgp, esi_t *esi, vni_t vni)
{
	struct bgpevpn *vpn;
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_evi *es_evi;
	char buf[ESI_STR_LEN];

	es = bgp_evpn_es_find(esi);
	if (!es) {
		flog_err(
				EC_BGP_ES_CREATE,
				"%u: Failed to deref VNI %d from ESI %s; ES not present",
				bgp->vrf_id, vni,
				esi_to_str(esi, buf, sizeof(buf)));
		return -1;
	}

	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		flog_err(
				EC_BGP_ES_CREATE,
				"%u: Failed to deref VNI %d from ESI %s; VNI not present",
				bgp->vrf_id, vni, es->esi_str);
		return -1;
	}

	es_evi = bgp_evpn_es_evi_find(es, vpn);
	if (!es_evi) {
		flog_err(
				EC_BGP_ES_CREATE,
				"%u: Failed to deref VNI %d from ESI %s; ES-VNI not present",
				bgp->vrf_id, vni, es->esi_str);
		return -1;
	}

	bgp_evpn_local_es_evi_do_del(es_evi);
	return 0;
}

/* 【函数】bgp_evpn_local_es_evi_add —— 对外接口：按 (esi, vni) 新建本地
 * ES-EVI 并通告对应 EAD 路由。ES / VNI 缺失返回 -1，重复添加返回 0；
 * 新建后经 local_info_set 置本地标志，生成 EAD-EVI、处理 VRF 内本地路由
 * 并刷新 EAD-ES，成功返回 0。 */
/* Create ES-EVI and advertise the corresponding EAD routes */
int bgp_evpn_local_es_evi_add(struct bgp *bgp, esi_t *esi, vni_t vni)
{
	struct bgpevpn *vpn;
	struct prefix_evpn p;
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_evi *es_evi;
	char buf[ESI_STR_LEN];

	es = bgp_evpn_es_find(esi);
	if (!es) {
		flog_err(
				EC_BGP_ES_CREATE,
				"%u: Failed to associate VNI %d with ESI %s; ES not present",
				bgp->vrf_id, vni,
				esi_to_str(esi, buf, sizeof(buf)));
		return -1;
	}

	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		flog_err(
				EC_BGP_ES_CREATE,
				"%u: Failed to associate VNI %d with ESI %s; VNI not present",
				bgp->vrf_id, vni, es->esi_str);
		return -1;
	}

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("add local es %s evi %u",
				es->esi_str, vni);

	es_evi = bgp_evpn_es_evi_find(es, vpn);

	if (es_evi) {
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
			/* dup */
			return 0;
	} else {
		es_evi = bgp_evpn_es_evi_new(es, vpn);
		if (!es_evi) {
			flog_err(EC_BGP_ES_CREATE, "%u: Failed to create ES-EVI for ES %s VNI %u",
				 bgp->vrf_id, es->esi_str, vni);
			return -1;
		}
	}

	bgp_evpn_es_evi_local_info_set(es_evi);

	/* generate an EAD-EVI for this new VNI */
	if (CHECK_FLAG(es->flags, BGP_EVPNES_ADV_EVI)) {
		build_evpn_type1_prefix(&p, BGP_EVPN_AD_EVI_ETH_TAG, &es->esi,
					es->originator_ip);
		bgp_evpn_ead_evi_route_update(bgp, es, vpn, &p);
	}

	bgp_evpn_local_es_evi_uninstall_local_routes_in_vrfs(es, es_evi);
	/* update EAD-ES */
	if (bgp_evpn_local_es_is_active(es))
		bgp_evpn_ead_es_route_update(bgp, es);

	return 0;
}

/* 【函数】bgp_evpn_remote_es_evi_add —— 处理远端 EAD 路由导入（Type-1）；
 * 由路径属性取 VTEP IP，按需新建 ES 与 ES-EVI，再添加远端 VTEP 并重估 REMOTE。
 * 本地 EAD-ES（vpn 为 NULL）无需回送 zebra，直接返回 SUCCESS。 */
/* Add remote ES-EVI entry. This is actually the remote VTEP add and the
 * ES-EVI is implicitly created on first VTEP's reference.
 */
enum zclient_send_status bgp_evpn_remote_es_evi_add(struct bgp *bgp,
						    struct bgpevpn *vpn,
						    const struct prefix_evpn *p,
							struct bgp_path_info *pi)
{
	char buf[ESI_STR_LEN];
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_evi *es_evi;
	bool ead_es;
	const esi_t *esi = &p->prefix.ead_addr.esi;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;
	struct ipaddr vtep_ip = {};

	if (!vpn)
		/* local EAD-ES need not be sent back to zebra */
		return ret;

	bgp_evpn_mh_fill_vtep_ip_from_pi(pi, &vtep_ip);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("add remote %s es %s evi %u ead-ip %pIA vtep %pIA",
			   p->prefix.ead_addr.eth_tag ? "ead-es" : "ead-evi",
			   esi_to_str(esi, buf, sizeof(buf)), vpn->vni, &p->prefix.ead_addr.ip,
			   &vtep_ip);

	es = bgp_evpn_es_find(esi);
	if (!es)
		es = bgp_evpn_es_new(bgp, esi);

	es_evi = bgp_evpn_es_evi_find(es, vpn);
	if (!es_evi)
		es_evi = bgp_evpn_es_evi_new(es, vpn);

	ead_es = !!p->prefix.ead_addr.eth_tag;
	ret = bgp_evpn_es_evi_vtep_add(bgp, es_evi, vtep_ip, ead_es);

	bgp_evpn_es_evi_remote_info_re_eval(es_evi);
	return ret;
}

/* 【函数】bgp_evpn_remote_es_evi_del —— 处理远端 EAD 路由撤销；
 * 由路径属性提取 VTEP IP（全局表前缀中的 IP 已清零，故不能从前缀取），
 * 删除对应远端 VTEP 并重估 REMOTE；ES / ES-EVI 不存在时记调试日志后
 * 原样返回 SUCCESS。 */
/* A remote VTEP has withdrawn. The es-evi-vtep will be deleted and the
 * parent es-evi freed up implicitly in last VTEP's deref.
 */
enum zclient_send_status bgp_evpn_remote_es_evi_del(struct bgp *bgp,
						    struct bgpevpn *vpn,
						    const struct prefix_evpn *p,
						    struct bgp_path_info *pi)
{
	char buf[ESI_STR_LEN];
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_evi *es_evi;
	bool ead_es;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;
	struct ipaddr vtep_ip = {};

	if (!vpn)
		/* local EAD-ES need not be sent back to zebra */
		return ret;

	/* Extract VTEP IP from BGP path attributes, not from prefix
	 * (prefix in global table has IP zeroed out)
	 */
	bgp_evpn_mh_fill_vtep_ip_from_pi(pi, &vtep_ip);
	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("del remote %s es %s evi %u ead_ip %pIA vtep %pIA",
			   p->prefix.ead_addr.eth_tag ? "ead-es" : "ead-evi",
			   esi_to_str(&p->prefix.ead_addr.esi, buf, sizeof(buf)), vpn->vni,
			   &p->prefix.ead_addr.ip, &vtep_ip);

	es = bgp_evpn_es_find(&p->prefix.ead_addr.esi);
	if (!es) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("del remote %s es %s evi %u vtep %pIA, NO es",
				   p->prefix.ead_addr.eth_tag ? "ead-es" : "ead-evi",
				   esi_to_str(&p->prefix.ead_addr.esi, buf, sizeof(buf)), vpn->vni,
				   &vtep_ip);
		return ret;
	}
	es_evi = bgp_evpn_es_evi_find(es, vpn);
	if (!es_evi) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("del remote %s es %s evi %u vtep %pIA, NO es-evi",
				   p->prefix.ead_addr.eth_tag ? "ead-es" : "ead-evi",
				   esi_to_str(&p->prefix.ead_addr.esi, buf, sizeof(buf)), vpn->vni,
				   &vtep_ip);
		return ret;
	}

	ead_es = !!p->prefix.ead_addr.eth_tag;
	ret = bgp_evpn_es_evi_vtep_del(bgp, es_evi, vtep_ip, ead_es);
	bgp_evpn_es_evi_remote_info_re_eval(es_evi);

	return ret;
}

/* 【函数】bgp_evpn_remote_es_evi_flush —— VNI 被删除时强制清空该 ES-EVI
 * 的所有远端 VTEP：逐个清除 EAD 标志、重估激活状态并释放记录，
 * 最后重估 ES-EVI 的 REMOTE。取不到 EVPN 实例(bgp)则直接返回。 */
/* If a VNI is being deleted we need to force del all remote VTEPs */
static void bgp_evpn_remote_es_evi_flush(struct bgp_evpn_es_evi *es_evi)
{
	struct listnode *node = NULL;
	struct listnode *nnode = NULL;
	struct bgp_evpn_es_evi_vtep *evi_vtep;
	struct bgp *bgp;

	bgp = bgp_get_evpn();
	if (!bgp)
		return;

	/* delete all VTEPs */
	for (ALL_LIST_ELEMENTS(es_evi->es_evi_vtep_list, node, nnode,
			       evi_vtep)) {
		UNSET_FLAG(evi_vtep->flags, (BGP_EVPN_EVI_VTEP_EAD_PER_ES |
					     BGP_EVPN_EVI_VTEP_EAD_PER_EVI));
		bgp_evpn_es_evi_vtep_re_eval_active(bgp, evi_vtep);
		bgp_evpn_es_evi_vtep_free(evi_vtep);
	}
	/* delete the EVI */
	bgp_evpn_es_evi_remote_info_re_eval(es_evi);
}

/* 【函数】bgp_evpn_vni_es_init —— 初始化每个 L2-VNI 的 ES 相关表：
 * 初始化 ES-EVI 红黑树，并创建供按类型快速遍历的本地 ES-EVI 链表。 */
/* Initialize the ES tables maintained per-L2_VNI */
void bgp_evpn_vni_es_init(struct bgpevpn *vpn)
{
	/* Initialize the ES-EVI RB tree */
	RB_INIT(bgp_es_evi_rb_head, &vpn->es_evi_rb_tree);

	/* Initialize the local list maintained for quick walks by type */
	vpn->local_es_evi_list = list_new();
	listset_app_node_mem(vpn->local_es_evi_list);
}

/* 【函数】bgp_evpn_vni_es_cleanup —— 清理每个 L2-VNI 的 ES 信息：
 * 安全遍历红黑树逐个删除本地信息，若对象仍保留则再强制 flush 其远端 VTEP，
 * 最后释放本地 ES-EVI 链表。 */
/* Cleanup the ES info maintained per-L2_VNI */
void bgp_evpn_vni_es_cleanup(struct bgpevpn *vpn)
{
	struct bgp_evpn_es_evi *es_evi;
	struct bgp_evpn_es_evi *es_evi_next;

	RB_FOREACH_SAFE(es_evi, bgp_es_evi_rb_head,
			&vpn->es_evi_rb_tree, es_evi_next) {
		es_evi = bgp_evpn_local_es_evi_do_del(es_evi);
		if (es_evi)
			bgp_evpn_remote_es_evi_flush(es_evi);
	}

	list_delete(&vpn->local_es_evi_list);
}

/* 【函数】bgp_evpn_es_evi_vteps_str —— 把 ES-EVI 的远端 VTEP 列表格式化为
 * "ip(EV),ip(E),..." 形式字符串（E=ead-per-es，V=ead-per-evi，无标志为 "-"）。
 * 结果写入调用方缓冲区 vtep_str 并返回之，供 show 输出使用。 */
static char *bgp_evpn_es_evi_vteps_str(char *vtep_str,
				       struct bgp_evpn_es_evi *es_evi,
				       size_t vtep_str_size)
{
	char vtep_flag_str[BGP_EVPN_FLAG_STR_SZ];
	struct listnode *node;
	struct bgp_evpn_es_evi_vtep *evi_vtep;
	bool first = true;
	char ip_buf[INET6_ADDRSTRLEN];

	vtep_str[0] = '\0';
	for (ALL_LIST_ELEMENTS_RO(es_evi->es_evi_vtep_list, node, evi_vtep)) {
		vtep_flag_str[0] = '\0';
		if (CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_ES))
			strlcat(vtep_flag_str, "E", sizeof(vtep_flag_str));
		if (CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_EVI))
			strlcat(vtep_flag_str, "V", sizeof(vtep_flag_str));

		if (!strnlen(vtep_flag_str, sizeof(vtep_flag_str)))
			strlcpy(vtep_flag_str, "-", sizeof(vtep_flag_str));
		if (first)
			first = false;
		else
			strlcat(vtep_str, ",", vtep_str_size);
		strlcat(vtep_str, ipaddr2str(&evi_vtep->vtep_ip, ip_buf, sizeof(ip_buf)),
			vtep_str_size);
		strlcat(vtep_str, "(", vtep_str_size);
		strlcat(vtep_str, vtep_flag_str, vtep_str_size);
		strlcat(vtep_str, ")", vtep_str_size);
	}

	return vtep_str;
}

/* 【函数】bgp_evpn_es_evi_json_vtep_fill —— 把一个远端 VTEP 追加到传入的
 * json 数组：含 vtep_ip，以及存在 EAD 标志时的 flags 数组
 * （ead-per-es / ead-per-evi）。 */
static void bgp_evpn_es_evi_json_vtep_fill(json_object *json_vteps,
		struct bgp_evpn_es_evi_vtep *evi_vtep)
{
	json_object *json_vtep_entry;
	json_object *json_flags;

	json_vtep_entry = json_object_new_object();

	json_object_string_addf(json_vtep_entry, "vtep_ip", "%pIA", &evi_vtep->vtep_ip);
	if (CHECK_FLAG(evi_vtep->flags, (BGP_EVPN_EVI_VTEP_EAD_PER_ES |
					 BGP_EVPN_EVI_VTEP_EAD_PER_EVI))) {
		json_flags = json_object_new_array();
		if (CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_ES))
			json_array_string_add(json_flags, "ead-per-es");
		if (CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_EAD_PER_EVI))
			json_array_string_add(json_flags, "ead-per-evi");
		json_object_object_add(json_vtep_entry,
				"flags", json_flags);
	}

	json_object_array_add(json_vteps,
			json_vtep_entry);
}

/* 【函数】bgp_evpn_es_evi_show_entry —— 输出单个 ES-EVI 的简要展示；
 * json 非空走 JSON（esi/vni/type 及 vteps 数组），否则走文本一行
 * （VNI/ESI/类型标志 L R I/远端 VTEP 列表）。 */
static void bgp_evpn_es_evi_show_entry(struct vty *vty,
		struct bgp_evpn_es_evi *es_evi, json_object *json)
{
	struct listnode *node;
	struct bgp_evpn_es_evi_vtep *evi_vtep;

	if (json) {
		json_object *json_vteps;
		json_object *json_types;

		json_object_string_add(json, "esi", es_evi->es->esi_str);
		if (es_evi->vpn)
			json_object_int_add(json, "vni", es_evi->vpn->vni);

		if (CHECK_FLAG(es_evi->flags, (BGP_EVPNES_EVI_LOCAL |
					       BGP_EVPNES_EVI_REMOTE))) {
			json_types = json_object_new_array();
			if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
				json_array_string_add(json_types, "local");
			if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE))
				json_array_string_add(json_types, "remote");
			json_object_object_add(json, "type", json_types);
		}

		if (listcount(es_evi->es_evi_vtep_list)) {
			json_vteps = json_object_new_array();
			for (ALL_LIST_ELEMENTS_RO(es_evi->es_evi_vtep_list,
						node, evi_vtep)) {
				bgp_evpn_es_evi_json_vtep_fill(json_vteps,
						evi_vtep);
			}
			json_object_object_add(json, "vteps", json_vteps);
		}
	} else {
		char type_str[4];
		char vtep_str[ES_VTEP_LIST_STR_SZ + BGP_EVPN_VTEPS_FLAG_STR_SZ];

		type_str[0] = '\0';
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
			strlcat(type_str, "L", sizeof(type_str));
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE))
			strlcat(type_str, "R", sizeof(type_str));
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_INCONS_VTEP_LIST))
			strlcat(type_str, "I", sizeof(type_str));

		bgp_evpn_es_evi_vteps_str(vtep_str, es_evi, sizeof(vtep_str));

		vty_out(vty, "%-8d %-30s %-5s %s\n",
				es_evi->vpn->vni, es_evi->es->esi_str,
				type_str, vtep_str);
	}
}

/* 【函数】bgp_evpn_es_evi_show_entry_detail —— 输出单个 ES-EVI 的详细信息：
 * 先复用 show_entry 输出简要部分，再补充 ES 片段 RD 以及不一致标志
 * （es-vtep-mismatch）；文本分支同样多打印 RD 与 inconsistencies。 */
static void bgp_evpn_es_evi_show_entry_detail(struct vty *vty,
		struct bgp_evpn_es_evi *es_evi, json_object *json)
{
	enum asnotation_mode mode;

	mode = bgp_get_asnotation(es_evi->vpn->bgp_vrf);

	if (json) {
		json_object *json_flags;

		/* Add the "brief" info first */
		bgp_evpn_es_evi_show_entry(vty, es_evi, json);
		if (es_evi->es_frag)
			json_object_string_addf(json, "esFragmentRd",
						BGP_RD_AS_FORMAT(mode),
						&es_evi->es_frag->prd);
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_INCONS_VTEP_LIST)) {
			json_flags = json_object_new_array();
			json_array_string_add(json_flags, "es-vtep-mismatch");
			json_object_object_add(json, "flags", json_flags);
		}
	} else {
		char vtep_str[ES_VTEP_LIST_STR_SZ + BGP_EVPN_VTEPS_FLAG_STR_SZ];
		char type_str[4];

		type_str[0] = '\0';
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_LOCAL))
			strlcat(type_str, "L", sizeof(type_str));
		if (CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE))
			strlcat(type_str, "R", sizeof(type_str));

		bgp_evpn_es_evi_vteps_str(vtep_str, es_evi, sizeof(vtep_str));
		if (!strlen(vtep_str))
			strlcpy(vtep_str, "-", sizeof(type_str));

		vty_out(vty, "VNI: %d ESI: %s\n",
				es_evi->vpn->vni, es_evi->es->esi_str);
		vty_out(vty, " Type: %s\n", type_str);
		if (es_evi->es_frag) {
			vty_out(vty, " ES fragment RD: ");
			vty_out(vty, BGP_RD_AS_FORMAT(mode),
				&es_evi->es_frag->prd);
			vty_out(vty, "\n");
		}
		vty_out(vty, " Inconsistencies: %s\n",
			CHECK_FLAG(es_evi->flags,
				   BGP_EVPNES_EVI_INCONS_VTEP_LIST)
				? "es-vtep-mismatch"
				: "-");
		vty_out(vty, " VTEPs: %s\n", vtep_str);
		vty_out(vty, "\n");
	}
}

/* 【函数】bgp_evpn_es_evi_show_one_vni —— 输出单个 L2-VNI 下的所有 ES-EVI；
 * 遍历该 VNI 的 ES-EVI 红黑树，据 detail 选择简要或详细输出，
 * JSON 模式下每个 ES-EVI 单独建对象并追加到 json_array。 */
static void bgp_evpn_es_evi_show_one_vni(struct bgpevpn *vpn, struct vty *vty,
		json_object *json_array, bool detail)
{
	struct bgp_evpn_es_evi *es_evi;
	json_object *json = NULL;

	RB_FOREACH(es_evi, bgp_es_evi_rb_head, &vpn->es_evi_rb_tree) {
		if (json_array)
			/* create a separate json object for each ES */
			json = json_object_new_object();
		if (detail)
			bgp_evpn_es_evi_show_entry_detail(vty, es_evi, json);
		else
			bgp_evpn_es_evi_show_entry(vty, es_evi, json);
		/* add ES to the json array */
		if (json_array)
			json_object_array_add(json_array, json);
	}
}

/* 【结构】es_evi_show_ctx —— 遍历 vnihash 逐个 VNI 输出时的上下文，
 * 承载 vty、目标 json 及是否详细(detail)，供 hash 回调透传。 */
struct es_evi_show_ctx {
	struct vty *vty;
	json_object *json;
	int detail;
};

/* 【函数】bgp_evpn_es_evi_show_one_vni_hash_cb —— vnihash 遍历回调：
 * 从 bucket 取出 bgpevpn，转调 show_one_vni 输出该 VNI 的 ES-EVI。 */
static void bgp_evpn_es_evi_show_one_vni_hash_cb(struct hash_bucket *bucket,
		void *ctxt)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	struct es_evi_show_ctx *wctx = (struct es_evi_show_ctx *)ctxt;

	bgp_evpn_es_evi_show_one_vni(vpn, wctx->vty, wctx->json, wctx->detail);
}

/* 【函数】bgp_evpn_es_evi_show —— 输出所有 VNI 下的 ES-EVI；
 * uj 为真走 JSON（收集到 json_array 后统一 vty_json 输出），否则先打印表头，
 * 再遍历 EVPN 实例的 vnihash 逐 VNI 输出。 */
/* Display all ES EVIs */
void bgp_evpn_es_evi_show(struct vty *vty, bool uj, bool detail)
{
	json_object *json_array = NULL;
	struct es_evi_show_ctx wctx;
	struct bgp *bgp;

	if (uj) {
		/* create an array of ES-EVIs */
		json_array = json_object_new_array();
	}

	wctx.vty = vty;
	wctx.json = json_array;
	wctx.detail = detail;

	bgp = bgp_get_evpn();

	if (!json_array && !detail) {
		vty_out(vty, "Flags: L local, R remote, I inconsistent\n");
		vty_out(vty, "VTEP-Flags: E EAD-per-ES, V EAD-per-EVI\n");
		vty_out(vty, "%-8s %-30s %-5s %s\n",
				"VNI", "ESI", "Flags", "VTEPs");
	}

	if (bgp)
		hash_iterate(bgp->vnihash,
				(void (*)(struct hash_bucket *,
				  void *))bgp_evpn_es_evi_show_one_vni_hash_cb,
				&wctx);
	if (uj)
		vty_json(vty, json_array);
}

/* 【函数】bgp_evpn_es_evi_show_vni —— 输出指定 VNI 下的 ES-EVI；
 * uj 走 JSON，否则文本先打印表头；VNI 不存在时（非 JSON）输出
 * "VNI not found"。 */
/* Display specific ES EVI */
void bgp_evpn_es_evi_show_vni(struct vty *vty, vni_t vni,
		bool uj, bool detail)
{
	struct bgpevpn *vpn = NULL;
	json_object *json_array = NULL;
	struct bgp *bgp;

	if (uj) {
		/* create an array of ES-EVIs */
		json_array = json_object_new_array();
	}

	bgp = bgp_get_evpn();
	if (bgp)
		vpn = bgp_evpn_lookup_vni(bgp, vni);

	if (vpn) {
		if (!json_array && !detail) {
			vty_out(vty, "Flags: L local, R remote, I inconsistent\n");
			vty_out(vty, "VTEP-Flags: E EAD-per-ES, V EAD-per-EVI\n");
			vty_out(vty, "%-8s %-30s %-5s %s\n",
					"VNI", "ESI", "Flags", "VTEPs");
		}

		bgp_evpn_es_evi_show_one_vni(vpn, vty, json_array, detail);
	} else {
		if (!uj)
			vty_out(vty, "VNI not found\n");
	}

	if (uj)
		vty_json(vty, json_array);
}

/* 【本章】Ethernet Segment 一致性检查（Consistency checks）
 * 一致性检查用于发现误配置或误接线；检测到不一致时仅记录日志并通过 show
 * 命令展示，未来可基于用户配置执行更严厉的动作。
 * 相关函数：cons_checks_timer_start / pend_add / pend_del、
 * es_run_consistency_checks、run_consistency_checks 等。 */
/*****************************************************************************
 * Ethernet Segment Consistency checks
 *     Consistency checking is done to detect misconfig or mis-cabling. When
 * an inconsistency is detected it is simply logged (and displayed via
 * show commands) at this point. A more drastic action can be executed (based
 * on user config) in the future.
 */
/* 【函数】bgp_evpn_es_cons_checks_timer_start —— 启动周期性一致性检查定时器；
 * 未开启 consistency_checking 或定时器已在运行则直接返回，否则以
 * BGP_EVPN_CONS_CHECK_INTERVAL 注册 bgp_evpn_run_consistency_checks。 */
static void bgp_evpn_es_cons_checks_timer_start(void)
{
	if (!bgp_mh_info->consistency_checking || bgp_mh_info->t_cons_check)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("periodic consistency checking started");

	event_add_timer(bm->master, bgp_evpn_run_consistency_checks, NULL,
			BGP_EVPN_CONS_CHECK_INTERVAL,
			&bgp_mh_info->t_cons_check);
}

/* 【函数】bgp_evpn_es_cons_checks_pend_add —— 把 ES 加入后台一致性检查队列；
 * 未开启检查或该 ES 已排队(BGP_EVPNES_CONS_CHECK_PEND)则直接返回；
 * 否则按需启动定时器，置标志并追加到 bgp_mh_info->pend_es_list 尾部。 */
/* queue up the es for background consistency checks */
static void bgp_evpn_es_cons_checks_pend_add(struct bgp_evpn_es *es)
{
	if (!bgp_mh_info->consistency_checking)
		/* consistency checking is not enabled */
		return;

	if (CHECK_FLAG(es->flags, BGP_EVPNES_CONS_CHECK_PEND))
		/* already queued for consistency checking */
		return;

	/* start the periodic timer for consistency checks if it is not
	 * already running */
	bgp_evpn_es_cons_checks_timer_start();

	SET_FLAG(es->flags, BGP_EVPNES_CONS_CHECK_PEND);
	listnode_init(&es->pend_es_listnode, es);
	listnode_add_after(bgp_mh_info->pend_es_list,
			listtail_unchecked(bgp_mh_info->pend_es_list),
			&es->pend_es_listnode);
}

/* 【函数】bgp_evpn_es_cons_checks_pend_del —— 把 ES 从一致性检查队列摘除；
 * 未置 PEND 标志则直接返回；否则清标志并从 pend_es_list 删除其节点。 */
/* pull the ES from the consistency check list */
static void bgp_evpn_es_cons_checks_pend_del(struct bgp_evpn_es *es)
{
	if (!CHECK_FLAG(es->flags, BGP_EVPNES_CONS_CHECK_PEND))
		return;

	UNSET_FLAG(es->flags, BGP_EVPNES_CONS_CHECK_PEND);
	list_delete_node(bgp_mh_info->pend_es_list,
			&es->pend_es_listnode);
}

/* 【函数】bgp_evpn_es_evi_get_active_vtep_cnt —— 统计某 ES-EVI 上
 * 处于 ACTIVE 状态的远端 VTEP 数量；返回计数。 */
/* Number of active VTEPs associated with the ES-per-EVI */
static uint32_t bgp_evpn_es_evi_get_active_vtep_cnt(
		struct bgp_evpn_es_evi *es_evi)
{
	struct bgp_evpn_es_evi_vtep *evi_vtep;
	struct listnode *node;
	uint32_t vtep_cnt = 0;

	for (ALL_LIST_ELEMENTS_RO(es_evi->es_evi_vtep_list, node, evi_vtep)) {
		if (CHECK_FLAG(evi_vtep->flags, BGP_EVPN_EVI_VTEP_ACTIVE))
			++vtep_cnt;
	}

	return vtep_cnt;
}

/* 【函数】bgp_evpn_es_get_active_vtep_cnt —— 统计某 ES 上处于 ACTIVE
 * 状态的 ES-VTEP 数量；返回计数。 */
/* Number of active VTEPs associated with the ES */
static uint32_t bgp_evpn_es_get_active_vtep_cnt(struct bgp_evpn_es *es)
{
	struct listnode *node;
	uint32_t vtep_cnt = 0;
	struct bgp_evpn_es_vtep *es_vtep;

	for (ALL_LIST_ELEMENTS_RO(es->es_vtep_list, node, es_vtep)) {
		if (CHECK_FLAG(es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE))
			++vtep_cnt;
	}

	return vtep_cnt;
}

/* 【函数】bgp_evpn_es_get_next_active_vtep —— 取 ES 列表中下一个 ACTIVE 的
 * ES-VTEP；es_vtep 为 NULL 时从头开始，否则从其下一节点开始，遍历到底返回 NULL。 */
static struct bgp_evpn_es_vtep *bgp_evpn_es_get_next_active_vtep(
		struct bgp_evpn_es *es, struct bgp_evpn_es_vtep *es_vtep)
{
	struct listnode *node;
	struct bgp_evpn_es_vtep *next_es_vtep;

	if (es_vtep)
		node = listnextnode_unchecked(&es_vtep->es_listnode);
	else
		node = listhead(es->es_vtep_list);

	for (; node; node = listnextnode_unchecked(node)) {
		next_es_vtep = listgetdata(node);
		if (CHECK_FLAG(next_es_vtep->flags, BGP_EVPNES_VTEP_ACTIVE))
			return next_es_vtep;
	}

	return NULL;
}

/* 【函数】bgp_evpn_es_evi_get_next_active_vtep —— 取 ES-EVI 远端 VTEP 列表中
 * 下一个 ACTIVE 的记录；evi_vtep 为 NULL 时从头开始，否则从其下一节点开始，
 * 遍历到底返回 NULL。 */
static struct bgp_evpn_es_evi_vtep *bgp_evpn_es_evi_get_next_active_vtep(
	struct bgp_evpn_es_evi *es_evi,
	struct bgp_evpn_es_evi_vtep *evi_vtep)
{
	struct listnode *node;
	struct bgp_evpn_es_evi_vtep *next_evi_vtep;

	if (evi_vtep)
		node = listnextnode_unchecked(&evi_vtep->es_evi_listnode);
	else
		node = listhead(es_evi->es_evi_vtep_list);

	for (; node; node = listnextnode_unchecked(node)) {
		next_evi_vtep = listgetdata(node);
		if (CHECK_FLAG(next_evi_vtep->flags, BGP_EVPN_EVI_VTEP_ACTIVE))
			return next_evi_vtep;
	}

	return NULL;
}

/* 【函数】bgp_evpn_es_evi_set_inconsistent —— 标记 ES-EVI 的远端 VTEP 列表
 * 存在不一致：置 BGP_EVPNES_EVI_INCONS_VTEP_LIST，并递增父 ES 的
 * incons_evi_vtep_cnt、在父 ES 的 inconsistencies 上置对应位；已置位则跳过。 */
static void bgp_evpn_es_evi_set_inconsistent(struct bgp_evpn_es_evi *es_evi)
{
	if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_INCONS_VTEP_LIST)) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("inconsistency detected - es %s evi %u vtep list mismatch",
					es_evi->es->esi_str,
					es_evi->vpn->vni);
		SET_FLAG(es_evi->flags, BGP_EVPNES_EVI_INCONS_VTEP_LIST);

		/* update parent ES with the inconsistency setting */
		if (!es_evi->es->incons_evi_vtep_cnt &&
				BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("inconsistency detected - es %s vtep list mismatch",
					es_evi->es->esi_str);
		++es_evi->es->incons_evi_vtep_cnt;
		SET_FLAG(es_evi->es->inconsistencies,
				BGP_EVPNES_INCONS_VTEP_LIST);
	}
}

/* 【函数】bgp_evpn_es_run_consistency_checks —— 对单个 ES 做一致性检查：
 * 复位不一致计数后，比较父 ES 的 ACTIVE VTEP 数与每个 ES-EVI 的 ACTIVE
 * 远端 VTEP 数，并逐个比对 VTEP 序列是否一致；任何不符即置该 ES-EVI 不一致。
 * 返回本次处理的 ES-EVI 个数。 */
static uint32_t bgp_evpn_es_run_consistency_checks(struct bgp_evpn_es *es)
{
	int proc_cnt = 0;
	int es_active_vtep_cnt;
	int evi_active_vtep_cnt;
	struct bgp_evpn_es_evi *es_evi;
	struct listnode *evi_node;
	struct bgp_evpn_es_vtep *es_vtep;
	struct bgp_evpn_es_evi_vtep *evi_vtep;

	/* reset the inconsistencies and re-evaluate */
	es->incons_evi_vtep_cnt = 0;
	es->inconsistencies = 0;

	es_active_vtep_cnt = bgp_evpn_es_get_active_vtep_cnt(es);
	for (ALL_LIST_ELEMENTS_RO(es->es_evi_list,
				evi_node, es_evi)) {
		++proc_cnt;

		/* reset the inconsistencies on the EVI and re-evaluate*/
		UNSET_FLAG(es_evi->flags, BGP_EVPNES_EVI_INCONS_VTEP_LIST);

		evi_active_vtep_cnt =
			bgp_evpn_es_evi_get_active_vtep_cnt(es_evi);
		if (es_active_vtep_cnt != evi_active_vtep_cnt) {
			bgp_evpn_es_evi_set_inconsistent(es_evi);
			continue;
		}

		if (!es_active_vtep_cnt)
			continue;

		es_vtep = NULL;
		evi_vtep = NULL;
		while ((es_vtep = bgp_evpn_es_get_next_active_vtep(
						es, es_vtep))) {
			evi_vtep = bgp_evpn_es_evi_get_next_active_vtep(es_evi,
					evi_vtep);
			if (!evi_vtep) {
				bgp_evpn_es_evi_set_inconsistent(es_evi);
				break;
			}
			if (!ipaddr_is_same(&es_vtep->vtep_ip, &evi_vtep->vtep_ip)) {
				/* inconsistency detected; set it and move
				 * to the next evi
				 */
				bgp_evpn_es_evi_set_inconsistent(es_evi);
				break;
			}
		}
	}

	return proc_cnt;
}

/* 【函数】bgp_evpn_run_consistency_checks —— 周期性定时回调：遍历待检查
 * ES 队列，逐个执行一致性检查并从队列摘除，处理数超过 500 即提前退出；
 * 最后重新挂上 BGP_EVPN_CONS_CHECK_INTERVAL 定时器。 */
static void bgp_evpn_run_consistency_checks(struct event *t)
{
	int proc_cnt = 0;
	struct listnode *node;
	struct listnode *nextnode;
	struct bgp_evpn_es *es;

	for (ALL_LIST_ELEMENTS(bgp_mh_info->pend_es_list,
				node, nextnode, es)) {
		++proc_cnt;
		/* run consistency checks on the ES and remove it from the
		 * pending list
		 */
		proc_cnt += bgp_evpn_es_run_consistency_checks(es);
		bgp_evpn_es_cons_checks_pend_del(es);
		if (proc_cnt > 500)
			break;
	}

	/* restart the timer */
	event_add_timer(bm->master, bgp_evpn_run_consistency_checks, NULL,
			BGP_EVPN_CONS_CHECK_INTERVAL,
			&bgp_mh_info->t_cons_check);
}

/* 【本章】EVPN 下一跳与 RMAC 管理
 * 以 ES 为目的的 Type-2 路由，其下一跳由 BGP 汇总为每 VRF 的 nh 到 rmac 映射
 * 下发 zebra，zebra 用 dummy(type-1) 前缀引用安装为远端邻居 / fdb 表项。
 * 之所以需要单独处理：此类以 ES 为目的的 Type-2 路由所用 NHG 由 EAD 路由
 * 建立，其中并不包含 RMAC 信息，故须另行补充。 */
/*****************************************************************************
 * EVPN-Nexthop and RMAC management: nexthops associated with Type-2 routes
 * that have an ES as destination are consolidated by BGP into a per-VRF
 * nh->rmac mapping which is sent to zebra. Zebra installs the nexthop
 * as a remote neigh/fdb entry with a dummy (type-1) prefix referencing it.
 *
 * This handling is needed because Type-2 routes with ES as dest use NHG
 * that is setup using EAD routes (i.e. such NHGs do not include the
 * RMAC info).
 ****************************************************************************/
/* 【函数】bgp_evpn_nh_zebra_update_send —— 向 zebra 下发某 EVPN 下一跳的
 * 增 / 删消息(ZEBRA_EVPN_REMOTE_NH_ADD / _DEL)，增消息携带 RMAC。
 * 无 zclient 或 zebra 尚未知悉该实例时直接返回。 */
static void bgp_evpn_nh_zebra_update_send(struct bgp_evpn_nh *nh, bool add)
{
	struct stream *s;
	struct bgp *bgp_vrf = nh->bgp_vrf;

	/* Check socket. */
	if (!bgp_zclient || bgp_zclient->sock < 0)
		return;

	/* Don't try to register if Zebra doesn't know of this instance. */
	if (!IS_BGP_INST_KNOWN_TO_ZEBRA(bgp_vrf)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("No zebra instance, not %s remote nh %s",
				   add ? "adding" : "deleting", nh->nh_str);
		return;
	}

	s = bgp_zclient->obuf;
	stream_reset(s);

	zclient_create_header(
		s, add ? ZEBRA_EVPN_REMOTE_NH_ADD : ZEBRA_EVPN_REMOTE_NH_DEL,
		bgp_vrf->vrf_id);
	stream_putl(s, bgp_vrf->vrf_id);
	stream_put(s, &nh->ip, sizeof(nh->ip));
	if (add)
		stream_put(s, &nh->rmac, sizeof(nh->rmac));

	stream_putw_at(s, 0, stream_get_endp(s));

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES) || bgp_debug_zebra(NULL)) {
		if (add)
			zlog_debug("evpn %s nh %s rmac %pEA add to zebra",
				   nh->bgp_vrf->name_pretty, nh->nh_str,
				   &nh->rmac);
		else
			zlog_debug("evpn %s nh %s del to zebra",
				   nh->bgp_vrf->name_pretty, nh->nh_str);
	}

	frrtrace(2, frr_bgp, evpn_mh_nh_rmac_zsend, add, nh);

	zclient_send_message(bgp_zclient);
}

/* 【函数】bgp_evpn_nh_zebra_update —— 依据 add 与 rmac 是否有效维护下一跳的
 * READY_FOR_ZEBRA 标志并向 zebra 同步：add 且 rmac 非零则置位并下发增；
 * 否则仅在已置位时清位并下发删。 */
static void bgp_evpn_nh_zebra_update(struct bgp_evpn_nh *nh, bool add)
{
	if (add && !is_zero_mac(&nh->rmac)) {
		SET_FLAG(nh->flags, BGP_EVPN_NH_READY_FOR_ZEBRA);
		bgp_evpn_nh_zebra_update_send(nh, true);
	} else {
		if (!CHECK_FLAG(nh->flags, BGP_EVPN_NH_READY_FOR_ZEBRA))
			return;
		UNSET_FLAG(nh->flags, BGP_EVPN_NH_READY_FOR_ZEBRA);
		bgp_evpn_nh_zebra_update_send(nh, false);
	}
}

/* 【函数】bgp_evpn_nh_alloc —— hash_get 的分配回调：按模板 *p 复制出一个
 * 新的 bgp_evpn_nh 并返回。 */
static void *bgp_evpn_nh_alloc(void *p)
{
	struct bgp_evpn_nh *tmp_n = p;
	struct bgp_evpn_nh *n;

	n = XCALLOC(MTYPE_BGP_EVPN_NH, sizeof(struct bgp_evpn_nh));
	*n = *tmp_n;

	return ((void *)n);
}

/* 【函数】bgp_evpn_nh_find —— 在 bgp_vrf 的 evpn_nh_table 中按 IP 查找
 * EVPN 下一跳记录；找到返回对象否则返回 NULL。 */
static struct bgp_evpn_nh *bgp_evpn_nh_find(struct bgp *bgp_vrf,
					    struct ipaddr *ip)
{
	struct bgp_evpn_nh tmp;
	struct bgp_evpn_nh *n;

	memset(&tmp, 0, sizeof(tmp));
	memcpy(&tmp.ip, ip, sizeof(struct ipaddr));
	n = hash_lookup(bgp_vrf->evpn_nh_table, &tmp);

	return n;
}

/* 【函数】bgp_evpn_nh_add —— 添加（或复用）某 IP 的 EVPN 下一跳记录：
 * 首次引用时隐式创建，初始化 pi_list；若首个 pi 有效则设 ref_pi 并复制其
 * rmac；随后同步 zebra。返回该下一跳记录。 */
/* Add nexthop entry - implicitly created on first path reference */
static struct bgp_evpn_nh *bgp_evpn_nh_add(struct bgp *bgp_vrf,
					   struct ipaddr *ip,
					   struct bgp_path_info *pi)
{
	struct bgp_evpn_nh tmp_n;
	struct bgp_evpn_nh *n = NULL;

	memset(&tmp_n, 0, sizeof(tmp_n));
	memcpy(&tmp_n.ip, ip, sizeof(struct ipaddr));
	n = hash_get(bgp_vrf->evpn_nh_table, &tmp_n, bgp_evpn_nh_alloc);
	ipaddr2str(ip, n->nh_str, sizeof(n->nh_str));
	n->bgp_vrf = bgp_vrf;

	n->pi_list = list_new();
	listset_app_node_mem(n->pi_list);

	/* Setup ref_pi when the nh is created */
	if (CHECK_FLAG(pi->flags, BGP_PATH_VALID) && pi->attr) {
		n->ref_pi = pi;
		memcpy(&n->rmac, &pi->attr->rmac, ETH_ALEN);
	}

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("evpn vrf %s nh %s rmac %pEA add",
			   n->bgp_vrf->name_pretty, n->nh_str, &n->rmac);
	bgp_evpn_nh_zebra_update(n, true);
	return n;
}

/* 【函数】bgp_evpn_nh_del —— 删除 EVPN 下一跳记录：仍有路径引用(pi_list
 * 非空)则不删；否则先通知 zebra 删除、释放 pi_list、从哈希表移除并释放记录。 */
/* Delete nexthop entry if there are no paths referencing it */
static void bgp_evpn_nh_del(struct bgp_evpn_nh *n)
{
	struct bgp_evpn_nh *tmp_n;
	struct bgp *bgp_vrf = n->bgp_vrf;

	if (listcount(n->pi_list))
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("evpn vrf %s nh %s del to zebra",
			   bgp_vrf->name_pretty, n->nh_str);

	bgp_evpn_nh_zebra_update(n, false);
	list_delete(&n->pi_list);
	tmp_n = hash_release(bgp_vrf->evpn_nh_table, n);
	XFREE(MTYPE_BGP_EVPN_NH, tmp_n);
}

/* 【函数】hash_evpn_nh_free —— 哈希表释放回调，XFREE 单个 bgp_evpn_nh。 */
static void hash_evpn_nh_free(struct bgp_evpn_nh *ben)
{
	XFREE(MTYPE_BGP_EVPN_NH, ben);
}

/* 【函数】bgp_evpn_nh_hash_keymake —— evpn_nh_table 的哈希键生成函数：
 * IPv4 用地址 s_addr 做 jhash_1word，IPv6 用 s6_addr32 做 jhash2。 */
static unsigned int bgp_evpn_nh_hash_keymake(const void *p)
{
	const struct bgp_evpn_nh *n = p;
	const struct ipaddr *ip = &n->ip;

	if (IS_IPADDR_V4(ip))
		return jhash_1word(ip->ipaddr_v4.s_addr, 0);

	return jhash2(ip->ipaddr_v6.s6_addr32,
		      array_size(ip->ipaddr_v6.s6_addr32), 0);
}

/* 【函数】bgp_evpn_nh_cmp —— evpn_nh_table 的键比较函数：按 IP 相等判定，
 * 两个 NULL 视为相等，其一为 NULL 视为不等。 */
static bool bgp_evpn_nh_cmp(const void *p1, const void *p2)
{
	const struct bgp_evpn_nh *n1 = p1;
	const struct bgp_evpn_nh *n2 = p2;

	if (n1 == NULL && n2 == NULL)
		return true;

	if (n1 == NULL || n2 == NULL)
		return false;

	return (ipaddr_cmp(&n1->ip, &n2->ip) == 0);
}

/* 【函数】bgp_evpn_nh_init —— 为某 BGP VRF 创建 EVPN 下一跳哈希表
 * (bgp_vrf->evpn_nh_table，以 IP 为键)。 */
void bgp_evpn_nh_init(struct bgp *bgp_vrf)
{
	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("evpn vrf %s nh init", bgp_vrf->name_pretty);
	bgp_vrf->evpn_nh_table = hash_create(
		bgp_evpn_nh_hash_keymake, bgp_evpn_nh_cmp, "BGP EVPN NH table");
}

/* 【函数】bgp_evpn_nh_flush_entry —— 强制清空单个 EVPN 下一跳的全部引用
 * 路径：遍历 pi_list 逐个调用 bgp_evpn_path_nh_del 删除。 */
static void bgp_evpn_nh_flush_entry(struct bgp_evpn_nh *nh)
{
	struct listnode *node;
	struct listnode *nnode;
	struct bgp_path_evpn_nh_info *nh_info;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("evpn vrf %s nh %s flush", nh->bgp_vrf->name_pretty,
			   nh->nh_str);

	/* force flush paths */
	for (ALL_LIST_ELEMENTS(nh->pi_list, node, nnode, nh_info))
		bgp_evpn_path_nh_del(nh->bgp_vrf, nh_info->pi);
}

/* 【函数】bgp_evpn_nh_flush_cb —— evpn_nh_table 的遍历回调，
 * 从 bucket 取记录后转调 bgp_evpn_nh_flush_entry 清空该下一跳。 */
static void bgp_evpn_nh_flush_cb(struct hash_bucket *bucket, void *ctxt)
{
	struct bgp_evpn_nh *nh = (struct bgp_evpn_nh *)bucket->data;

	bgp_evpn_nh_flush_entry(nh);
}

/* 【函数】bgp_evpn_nh_finish —— 销毁某 VRF 的 EVPN 下一跳表：
 * 先遍历清空每条记录的引用路径，再整体清空并释放哈希表。 */
void bgp_evpn_nh_finish(struct bgp *bgp_vrf)
{
	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("evpn vrf %s nh finish", bgp_vrf->name_pretty);
	hash_iterate(
		bgp_vrf->evpn_nh_table,
		(void (*)(struct hash_bucket *, void *))bgp_evpn_nh_flush_cb,
		NULL);
	hash_clean_and_free(&bgp_vrf->evpn_nh_table,
			    (void (*)(void *))hash_evpn_nh_free);
}

/* 【函数】bgp_evpn_nh_update_ref_pi —— 在 ref_pi 为空时，从 pi_list 中挑一个
 * 有效路径补位为 ref_pi，并据其 rmac 是否变化决定是否刷新 zebra；
 * 已有 ref_pi 则直接返回。 */
static void bgp_evpn_nh_update_ref_pi(struct bgp_evpn_nh *nh)
{
	struct listnode *node;
	struct bgp_path_info *pi;
	struct bgp_path_evpn_nh_info *nh_info;

	if (nh->ref_pi)
		return;

	for (ALL_LIST_ELEMENTS_RO(nh->pi_list, node, nh_info)) {
		pi = nh_info->pi;
		if (!CHECK_FLAG(pi->flags, BGP_PATH_VALID) || !pi->attr)
			continue;

		if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
			zlog_debug("evpn %s nh %s ref_pi update",
				   nh->bgp_vrf->name_pretty, nh->nh_str);
		nh->ref_pi = pi;
		/* If we have a new pi copy rmac from it and update
		 * zebra if the new rmac is different
		 */
		if (memcmp(&nh->rmac, &nh->ref_pi->attr->rmac, ETH_ALEN)) {
			memcpy(&nh->rmac, &nh->ref_pi->attr->rmac, ETH_ALEN);
			bgp_evpn_nh_zebra_update(nh, true);
		}
		break;
	}
}

/* 【函数】bgp_evpn_nh_clear_ref_pi —— 当某路径 pi 正是当前 ref_pi 时清除它，
 * 先尝试换用其它有效路径作为 ref_pi，若找不到则把 rmac 清零并通知 zebra。 */
static void bgp_evpn_nh_clear_ref_pi(struct bgp_evpn_nh *nh,
				     struct bgp_path_info *pi)
{
	if (nh->ref_pi != pi)
		return;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("evpn vrf %s nh %s ref_pi clear",
			   nh->bgp_vrf->name_pretty, nh->nh_str);
	nh->ref_pi = NULL;
	/* try to find another ref_pi */
	bgp_evpn_nh_update_ref_pi(nh);
	/* couldn't find one - clear the old rmac and notify zebra */
	if (!nh->ref_pi) {
		memset(&nh->rmac, 0, ETH_ALEN);
		bgp_evpn_nh_zebra_update(nh, true);
	}
}

/* 【函数】bgp_evpn_path_nh_info_free —— 释放一条路径-下一跳关联记录：
 * 先 unlink 解绑，再 XFREE 内存。 */
static void bgp_evpn_path_nh_info_free(struct bgp_path_evpn_nh_info *nh_info)
{
	bgp_evpn_path_nh_unlink(nh_info);
	XFREE(MTYPE_BGP_EVPN_PATH_NH_INFO, nh_info);
}

/* 【函数】bgp_evpn_path_nh_info_new —— 取得（必要时创建）路径 pi 的
 * EVPN 下一跳关联信息 nh_info：先确保路径 extra 的 mh_info 存在，
 * 再确保其中的 nh_info 存在并记录 pi。返回该 nh_info。 */
static struct bgp_path_evpn_nh_info *
bgp_evpn_path_nh_info_new(struct bgp_path_info *pi)
{
	struct bgp_path_info_extra *e;
	struct bgp_path_mh_info *mh_info;
	struct bgp_path_evpn_nh_info *nh_info;

	e = bgp_evpn_path_info_extra_get(pi);

	/* If mh_info doesn't exist allocate it */
	mh_info = e->evpn->mh_info;
	if (!mh_info)
		e->evpn->mh_info = mh_info =
			XCALLOC(MTYPE_BGP_EVPN_PATH_MH_INFO,
				sizeof(struct bgp_path_mh_info));

	/* If nh_info doesn't exist allocate it */
	nh_info = mh_info->nh_info;
	if (!nh_info) {
		mh_info->nh_info = nh_info =
			XCALLOC(MTYPE_BGP_EVPN_PATH_NH_INFO,
				sizeof(struct bgp_path_evpn_nh_info));
		nh_info->pi = pi;
	}

	return nh_info;
}

/* 【函数】bgp_evpn_path_nh_unlink —— 把路径的 nh_info 从 EVPN 下一跳解绑：
 * 从 nh 的 pi_list 摘除该节点、清 nh 指针，重估 ref_pi，若已无其它引用
 * 则删除该 nh。注意：unlink 不释放 nh_info 本身，它随路径一同释放。 */
static void bgp_evpn_path_nh_unlink(struct bgp_path_evpn_nh_info *nh_info)
{
	struct bgp_evpn_nh *nh = nh_info->nh;
	struct bgp_path_info *pi;
	char prefix_buf[PREFIX_STRLEN];

	if (!nh)
		return;

	pi = nh_info->pi;
	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("path %s unlinked from %s nh %s pathcount %u",
			   pi->net ? prefix2str(&pi->net->rn->p, prefix_buf,
						sizeof(prefix_buf))
				   : "",
			   nh->bgp_vrf->name_pretty, nh->nh_str,
			   listcount(nh->pi_list));

	list_delete_node(nh->pi_list, &nh_info->nh_listnode);

	nh_info->nh = NULL;

	/* check if the ref_pi need to be updated */
	bgp_evpn_nh_clear_ref_pi(nh, pi);

	/* if there are no other references against the nh it
	 * needs to be freed
	 */
	bgp_evpn_nh_del(nh);

	/* Note we don't free the path nh_info on unlink; it will be freed up
	 * along with the path.
	 */
}

/* 【函数】bgp_evpn_path_nh_link —— 把路径 pi 关联到其 EVPN 下一跳记录：
 * 未启用 nh setup 或该 VRF 无 nh 表则跳过；不使用 L3 NHG 的路径则解绑。
 * 由路径属性推出下一跳 IP，查找（必要时创建）nh，去重后解绑旧的、
 * 挂入新 nh 的 pi_list，并重估 ref_pi 以同步 RMAC。 */
static void bgp_evpn_path_nh_link(struct bgp *bgp_vrf, struct bgp_path_info *pi)
{
	struct bgp_path_evpn_nh_info *nh_info;
	struct bgp_evpn_nh *nh;
	struct ipaddr ip;

	/* EVPN nexthop setup in bgp has been turned off */
	if (!bgp_mh_info->bgp_evpn_nh_setup)
		return;

	if (!bgp_vrf->evpn_nh_table) {
		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug("path %pFX linked to %s failed",
				   &pi->net->rn->p, bgp_vrf->name_pretty);
		return;
	}

	nh_info = (pi->extra && pi->extra->evpn && pi->extra->evpn->mh_info)
			  ? pi->extra->evpn->mh_info->nh_info
			  : NULL;

	/* if NHG is not being used for this path we don't need to manage the
	 * nexthops in bgp (they are managed by zebra instead)
	 */
	if (!CHECK_FLAG(pi->attr->es_flags, ATTR_ES_L3_NHG_USE)) {
		if (nh_info)
			bgp_evpn_path_nh_unlink(nh_info);
		return;
	}

	/* setup nh_info against the path if it doesn't already exist */
	if (!nh_info)
		nh_info = bgp_evpn_path_nh_info_new(pi);

	/* find-create nh */
	memset(&ip, 0, sizeof(ip));
	/* copy path attribute's ipv4 or ipv6 address as nexthop field synced to zebra */
	if (pi->net->rn->p.family == AF_INET6 || BGP_ATTR_MP_NEXTHOP_LEN_IP6(pi->attr)) {
		SET_IPADDR_V6(&ip);
		memcpy(&ip.ipaddr_v6, &pi->attr->mp_nexthop_global,
		       sizeof(ip.ipaddr_v6));
	} else {
		SET_IPADDR_V4(&ip);
		if (pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4 ||
		    pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4)
			memcpy(&ip.ipaddr_v4, &pi->attr->mp_nexthop_global_in,
			       sizeof(ip.ipaddr_v4));
		else
			memcpy(&ip.ipaddr_v4, &pi->attr->nexthop, sizeof(ip.ipaddr_v4));
	}

	nh = bgp_evpn_nh_find(bgp_vrf, &ip);
	if (!nh)
		nh = bgp_evpn_nh_add(bgp_vrf, &ip, pi);

	/* dup check */
	if (nh_info->nh == nh) {
		/* Check if any of the paths are now valid */
		bgp_evpn_nh_update_ref_pi(nh);
		return;
	}

	/* unlink old nh if any */
	bgp_evpn_path_nh_unlink(nh_info);

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("path %pFX linked to %s nh %s pathcount %u",
			   &pi->net->rn->p, nh->bgp_vrf->name_pretty,
			   nh->nh_str, listcount(nh->pi_list));

	/* link mac-ip path to the new nh */
	nh_info->nh = nh;
	listnode_init(&nh_info->nh_listnode, nh_info);
	listnode_add(nh->pi_list, &nh_info->nh_listnode);
	/* If a new valid path got linked to the nh see if can get the rmac
	 * from it
	 */
	bgp_evpn_nh_update_ref_pi(nh);
	if (BGP_DEBUG(evpn_mh, EVPN_MH_ES)) {
		if (!nh->ref_pi)
			zlog_debug(
				"path %pFX linked to nh %s %s with no valid pi",
				&pi->net->rn->p, nh->bgp_vrf->name_pretty,
				nh->nh_str);
	}
}

/* 【函数】bgp_evpn_path_nh_del —— 对外接口：删除路径 pi 的下一跳关联，
 * 即取出其 nh_info 后调用 bgp_evpn_path_nh_unlink；无 nh_info 则直接返回。 */
void bgp_evpn_path_nh_del(struct bgp *bgp_vrf, struct bgp_path_info *pi)
{
	struct bgp_path_evpn_nh_info *nh_info;

	nh_info = (pi->extra && pi->extra->evpn && pi->extra->evpn->mh_info)
			  ? pi->extra->evpn->mh_info->nh_info
			  : NULL;

	if (!nh_info)
		return;

	bgp_evpn_path_nh_unlink(nh_info);
}

/* 【函数】bgp_evpn_path_nh_add —— 对外接口：添加路径 pi 的下一跳关联，
 * 直接转调 bgp_evpn_path_nh_link。 */
void bgp_evpn_path_nh_add(struct bgp *bgp_vrf, struct bgp_path_info *pi)
{
	bgp_evpn_path_nh_link(bgp_vrf, pi);
}

/* 【函数】bgp_evpn_nh_show_entry —— 输出单个 EVPN 下一跳信息：
 * json 非空走 JSON（vrf/ip/rmac/basePath/pathCount），否则文本一行；
 * basePath 取自 ref_pi 对应的网络前缀。 */
static void bgp_evpn_nh_show_entry(struct bgp_evpn_nh *nh, struct vty *vty,
				   json_object *json_array)
{
	json_object *json = NULL;
	char mac_buf[ETHER_ADDR_STRLEN];
	char prefix_buf[PREFIX_STRLEN];

	if (json_array)
		/* create a separate json object for each ES */
		json = json_object_new_object();

	prefix_mac2str(&nh->rmac, mac_buf, sizeof(mac_buf));
	if (nh->ref_pi && nh->ref_pi->net)
		prefix2str(&nh->ref_pi->net->rn->p, prefix_buf, sizeof(prefix_buf));
	else
		prefix_buf[0] = '\0';
	if (json) {
		json_object_string_add(json, "vrf", nh->bgp_vrf->name_pretty);
		json_object_string_add(json, "ip", nh->nh_str);
		json_object_string_add(json, "rmac", mac_buf);
		json_object_string_add(json, "basePath", prefix_buf);
		json_object_int_add(json, "pathCount", listcount(nh->pi_list));
	} else {
		vty_out(vty, "%-15s %-39s %-17s %-10d %s\n", nh->bgp_vrf->name_pretty, nh->nh_str,
			mac_buf, listcount(nh->pi_list), prefix_buf);
	}

	/* add ES to the json array */
	if (json_array)
		json_object_array_add(json_array, json);
}

/* 【结构】nh_show_ctx —— 遍历各 VRF 的 evpn_nh_table 输出时的上下文，
 * 承载 vty 与目标 json。 */
struct nh_show_ctx {
	struct vty *vty;
	json_object *json;
};

/* 【函数】bgp_evpn_nh_show_hash_cb —— evpn_nh_table 遍历回调，转调
 * bgp_evpn_nh_show_entry 输出该下一跳。 */
static void bgp_evpn_nh_show_hash_cb(struct hash_bucket *bucket, void *ctxt)
{
	struct bgp_evpn_nh *nh = (struct bgp_evpn_nh *)bucket->data;
	struct nh_show_ctx *wctx = (struct nh_show_ctx *)ctxt;

	bgp_evpn_nh_show_entry(nh, wctx->vty, wctx->json);
}

/* 【函数】bgp_evpn_nh_show —— 输出所有 VRF 的 EVPN 下一跳；
 * uj 走 JSON（收集后统一 vty_json 输出），否则先打印表头，
 * 再遍历 bm->bgp 中每个 VRF 的 evpn_nh_table。 */
/* Display all evpn nexthops */
void bgp_evpn_nh_show(struct vty *vty, bool uj)
{
	json_object *json_array = NULL;
	struct bgp *bgp_vrf;
	struct listnode *node;
	struct nh_show_ctx wctx;

	if (uj) {
		/* create an array of nexthops */
		json_array = json_object_new_array();
	} else {
		vty_out(vty, "%-15s %-39s %-17s %-10s %s\n", "VRF", "IP", "RMAC", "#Paths",
			"Base Path");
	}

	wctx.vty = vty;
	wctx.json = json_array;

	/* walk through all vrfs */
	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_vrf)) {
		hash_iterate(bgp_vrf->evpn_nh_table,
			     (void (*)(struct hash_bucket *,
				       void *))bgp_evpn_nh_show_hash_cb,
			     &wctx);
	}

	/* print the array of json-ESs */
	if (uj)
		vty_json(vty, json_array);
}

/* 【函数】bgp_evpn_mh_init —— 初始化 EVPN 多归属全局状态 bgp_mh_info：
 * 分配结构、初始化 ES 红黑树与本地 / 待处理 ES 链表、EAD-EVI 收发开关、
 * EAD-ES 导出 RT 链表及各项配置开关，并清零 zero_esi_buf。 */
/*****************************************************************************/
void bgp_evpn_mh_init(void)
{
	bm->mh_info = XCALLOC(MTYPE_BGP_EVPN_MH_INFO, sizeof(*bm->mh_info));

	/* setup ES tables */
	RB_INIT(bgp_es_rb_head, &bgp_mh_info->es_rb_tree);
	/* local ES list */
	bgp_mh_info->local_es_list = list_new();
	listset_app_node_mem(bgp_mh_info->local_es_list);
	/* list of ESs with pending processing */
	bgp_mh_info->pend_es_list = list_new();
	listset_app_node_mem(bgp_mh_info->pend_es_list);

	bgp_mh_info->enable_ead_evi_rx = BGP_EVPN_MH_EAD_EVI_RX_DEF;
	bgp_mh_info->enable_ead_evi_tx = BGP_EVPN_MH_EAD_EVI_TX_DEF;
	bgp_mh_info->ead_es_export_rtl = list_new();
	bgp_mh_info->ead_es_export_rtl->cmp =
		(int (*)(void *, void *))bgp_evpn_route_target_cmp;
	bgp_mh_info->ead_es_export_rtl->del = bgp_evpn_xxport_delete_ecomm;

	/* config knobs - XXX add cli to control it */
	bgp_mh_info->ead_evi_adv_for_down_links = true;
	bgp_mh_info->consistency_checking = true;
	bgp_mh_info->host_routes_use_l3nhg = BGP_EVPN_MH_USE_ES_L3NHG_DEF;
	bgp_mh_info->suppress_l3_ecomm_on_inactive_es = true;
	bgp_mh_info->bgp_evpn_nh_setup = true;
	bgp_mh_info->evi_per_es_frag = BGP_EVPN_MAX_EVI_PER_ES_FRAG;

	memset(&zero_esi_buf, 0, sizeof(esi_t));
}

/* 【函数】bgp_evpn_mh_finish —— 关闭时强制清理 EVPN 多归属状态：
 * 遍历 ES，对残留 ES-EVI 清 REMOTE 后强制释放，再清 ES 本地信息；
 * 随后取消一致性检查定时器、删除各链表并释放 bgp_mh_info。 */
void bgp_evpn_mh_finish(void)
{
	struct bgp_evpn_es *es;
	struct bgp_evpn_es *es_next;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
		zlog_debug("evpn mh finish");

	/* Force cleanup of all ES structures including ES-EVI and ES-VRF.
	 * During shutdown, normal cleanup may fail due to guard conditions
	 * that prevent freeing structures with REMOTE flags set. We force
	 * cleanup here to ensure no memory leaks.
	 */
	RB_FOREACH_SAFE (es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree, es_next) {
		/* Force cleanup of any remaining structures that couldn't be
		 * freed due to REMOTE flags or other guard conditions
		 */
		if (es->es_evi_list && listcount(es->es_evi_list) > 0) {
			struct listnode *node, *next;
			struct bgp_evpn_es_evi *es_evi;

			if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
				zlog_debug("evpn mh finish: force cleanup %d ES-EVIs for ES %s",
					   listcount(es->es_evi_list), es->esi_str);

			for (ALL_LIST_ELEMENTS(es->es_evi_list, node, next, es_evi)) {
				/* Clear remote flag and force free */
				UNSET_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE);
				bgp_evpn_es_evi_free_internal(es_evi, true);
			}
		}

		/* Clear local info (attempts normal cleanup and may free es) */
		bgp_evpn_es_local_info_clear(es, true);
	}
	if (bgp_mh_info->t_cons_check)
		event_cancel(&bgp_mh_info->t_cons_check);
	list_delete(&bgp_mh_info->local_es_list);
	list_delete(&bgp_mh_info->pend_es_list);
	list_delete(&bgp_mh_info->ead_es_export_rtl);

	XFREE(MTYPE_BGP_EVPN_MH_INFO, bgp_mh_info);
}

/* 【函数】bgp_evpn_switch_ead_evi_tx —— disable-ead-evi-tx 开关翻转时的回调：
 * 取 EVPN 实例后遍历所有 ES，按 enable_ead_evi_tx 添加或删除本地
 * EAD-per-EVI(Type-1) 路由。 */
/* This function is called when disable-ead-evi-tx knob flaps */
void bgp_evpn_switch_ead_evi_tx(void)
{
	struct bgp *bgp;
	struct bgp_evpn_es *es = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return;

	RB_FOREACH (es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree) {
		if (bgp_mh_info->enable_ead_evi_tx)
			bgp_evpn_local_type1_evi_route_add(bgp, es);
		else
			bgp_evpn_local_type1_evi_route_del(bgp, es);
	}
}

/* 【函数】bgp_evpn_switch_ead_evi_rx —— disable-ead-evi-rx 开关翻转时的回调：
 * 取 EVPN 实例后遍历所有 REMOTE 的 ES 及其 REMOTE 的 ES-EVI，对其上每个
 * 远端 VTEP 调用 re_eval_active，据新的激活条件刷新激活 / NHG 状态。 */
/* This function is called when disable-ead-evi-rx knob flaps */
void bgp_evpn_switch_ead_evi_rx(void)
{
	struct bgp *bgp;
	struct bgp_evpn_es *es;
	struct bgp_evpn_es_evi *es_evi;
	struct listnode *evi_node = NULL;
	struct listnode *evi_next = NULL;
	struct bgp_evpn_es_evi_vtep *vtep;
	struct listnode *vtep_node = NULL;
	struct listnode *vtep_next = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return;

	/*
	 * Process all the remote es_evi_vteps and reevaluate if the es_evi_vtep
	 * is active.
	 */
	RB_FOREACH (es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree) {
		if (!CHECK_FLAG(es->flags, BGP_EVPNES_REMOTE))
			continue;

		for (ALL_LIST_ELEMENTS(es->es_evi_list, evi_node, evi_next,
				       es_evi)) {
			if (!CHECK_FLAG(es_evi->flags, BGP_EVPNES_EVI_REMOTE))
				continue;

			for (ALL_LIST_ELEMENTS(es_evi->es_evi_vtep_list,
					       vtep_node, vtep_next, vtep))
				bgp_evpn_es_evi_vtep_re_eval_active(bgp, vtep);
		}
	}
}

/* 【函数】bgp_evpn_local_es_evi_uninstall_local_routes_in_vrfs —— 把与本本地
 * ES-EVI 匹配、且具备跳过本地导入条件的 EVPN 路由从相关 VRF 中卸载；
 * 无 es_vrf 则直接返回。用于本地上线 / 下线时清理 VRF 内既有导入路由。 */
void bgp_evpn_local_es_evi_uninstall_local_routes_in_vrfs(struct bgp_evpn_es *es,
							  struct bgp_evpn_es_evi *es_evi)
{
	struct listnode *node;
	struct bgp_path_es_info *es_info;
	struct bgp_path_info *pi;
	const struct prefix_evpn *evp;
	struct bgp_evpn_es_vrf *es_vrf = es_evi->es_vrf;

	if (!es_vrf)
		return;

	for (ALL_LIST_ELEMENTS_RO(es->macip_global_path_list, node, es_info)) {
		pi = es_info->pi;

		if (!bgp_evpn_is_macip_path(pi))
			continue;

		evp = (const struct prefix_evpn *)bgp_dest_get_prefix(pi->net);

		if (!(CHECK_FLAG(pi->flags, BGP_PATH_VALID) && pi->type == ZEBRA_ROUTE_BGP &&
		      pi->sub_type == BGP_ROUTE_NORMAL))
			continue;

		if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
			zlog_debug("route %pFX is matched on local esi %s, uninstall from %s route table",
				   evp, es->esi_str, es_vrf->bgp_vrf->name_pretty);

		if (!bgp_evpn_skip_vrf_import_of_local_es(es_vrf->bgp_vrf, evp, pi, 0))
			continue;

		uninstall_evpn_route_entry_in_vrf(es_vrf->bgp_vrf, evp, pi);
	}
}
