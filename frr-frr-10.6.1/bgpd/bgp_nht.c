// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Nexthop tracking
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_nht.c —— NHT(下一跳跟踪)逻辑层的实现
 * ============================================================================
 * 职责与定位：本文件是 NHT 的“逻辑层”，负责把一条路径用到的下一跳登记给 zebra、
 *   接收 zebra 的下一跳可达性更新、并把结果扇出(evaluate_paths)到所有引用该下一跳
 *   的路径，从而驱动路由有效性/最佳路径重算。与 bgp_nexthop.c（bnc 数据结构与扫描
 *   层）配套：本文件管“流程”，后者管“结构”。头文件 bgp_nht.h/bgp_nexthop.h 已把
 *   整体设计、bnc 字段、flags/change_flags 位含义讲清，本文件是把那些概念落到实处。
 *
 * 关键路径（谁调用它 / 它调用谁）：
 *   - 谁调用它：bgp_route.c/bgp_attr.c 处理 UPDATE 时调 bgp_find_or_add_nexthop()
 *     登记下一跳；bgp_zebra.c 收到 ZEBRA_NEXTHOP_UPDATE 时调 bgp_nexthop_update()；
 *     bgp_fsm.c/接口模块调 bgp_nht_ifp_up/down()、bgp_nht_interface_events()；
 *     bgp_zebra.c 在 zebra 重连后调 bgp_nht_register_nexthops()。
 *   - 它调用谁：register_zebra_rnh()/sendmsg_zebra_rnh() 向 zebra 发 RNH 登记；
 *     evaluate_paths() 调 bgp_process() 触发选路、并 hook_call(bgp_nht_path_update)；
 *     bgp_fsm_nht_update() 通知 FSM 邻居可达性变化。
 *
 * 关键函数清单（建议阅读顺序）：
 *   1) bgp_find_or_add_nexthop —— 登记入口（理解 bgp_route/bgp_nexthop 可分属不同实例）
 *   2) path_nh_map / bgp_unlink_nexthop —— 映射的建立与拆除（bnc 生命周期）
 *   3) bgp_process_nexthop_update —— zebra 更新如何改写 bnc 的 flags/metric/nexthop
 *   4) evaluate_paths —— 扇出到所有路径、置/清 BGP_PATH_VALID、触发重算与 hook
 *   5) register_zebra_rnh / sendmsg_zebra_rnh —— 真正发往 zebra 的 RNH 消息
 *   6) make_prefix —— 从 path 的属性/下一跳编码推出被跟踪的前缀
 *   7) bgp_nht_ifp_up/down / bgp_nht_interface_events —— 接口事件对 v6 LL 下一跳的影响
 *   8) bgp_nht_reg_enhe_cap_intfs / bgp_nht_dereg_enhe_cap_intfs —— RFC5549 ENHE 接口 RA
 *
 * 建议阅读顺序：先读头文件建立概念，再按上面 1)→8) 阅读。读时始终问自己两个问题：
 *   这个 bnc 是谁登记进来的？它一变会扇出到哪些路径？
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "frrevent.h"
#include "prefix.h"
#include "zclient.h"
#include "stream.h"
#include "network.h"
#include "log.h"
#include "memory.h"
#include "nexthop.h"
#include "vrf.h"
#include "filter.h"
#include "nexthop_group.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_nexthop.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_nht.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_flowspec_util.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_rd.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_ecommunity.h"

extern struct zclient *bgp_zclient;

static void register_zebra_rnh(struct bgp_nexthop_cache *bnc);
static void unregister_zebra_rnh(struct bgp_nexthop_cache *bnc);
static bool make_prefix(int afi, struct bgp_path_info *pi, struct prefix *p,
			struct bgp *bgp_nexthop, struct bgp_path_info *pi_source);
static void bgp_nht_ifp_initial(struct event *event);

DEFINE_HOOK(bgp_nht_path_update, (struct bgp *bgp, struct bgp_path_info *pi, bool valid),
	    (bgp, pi, valid));

/* ========== 1. 下一跳有效性判定辅助（含 MPLS/L3VPN/GRETAP 场景特判） ========== */
/* 【函数】bgp_isvalid_nexthop —— 判定一个 bnc 当前是否“可用作有效下一跳”。
 * 返回值：非 0 视为有效。
 * 关键分支：
 *   - 若与 zebra 的连接数为 0（还没连上/已断），**直接返回有效**：因为拿不到可达性
 *     信息，此时不能把路由判死，属降级容忍（避免启动早期把全部路由当无效）。
 *   - 否则要求 bnc 非空、flags 置了 BGP_NEXTHOP_VALID、且 nexthop_num>0（真的解析
 *     出了至少一个下一跳）。
 * 说明：这是最通用的判据；VPN/MPLS 场景另见下面的 for_mpls / for_l3vpn 变体。 */
static int bgp_isvalid_nexthop(struct bgp_nexthop_cache *bnc)
{
	return (bgp_zebra_num_connects() == 0
		|| (bnc && CHECK_FLAG(bnc->flags, BGP_NEXTHOP_VALID)
		    && bnc->nexthop_num > 0));
}

/* 【函数】bgp_isvalid_nexthop_for_ebgp —— 针对 eBGP 直连场景的“有效性放宽”。
 * 用途：在 MPLS 转发路径上，即使下一跳的可达性判据不完全满足（例如带了标签），
 *   只要该出接口配置了 MPLS/BGP 转发能力(BGP_INTERFACE_MPLS_BGP_FORWARDING)，
 *   就认为这条 eBGP 直连路径可用。
 * 关键点：
 *   - 仅对“原始 eBGP 邻居”生效：通过 path->extra->vrfleak->peer_orig 取回原始 peer
 *     （import/vrf-leak 场景下 path->peer 可能不是真正学到该路由的邻居）；
 *   - 要求 peer 是 eBGP、ttl 为默认（即非多跳）、未禁用 connected-check，且实例未
 *     设置 BGP_FLAG_DISABLE_NH_CONNECTED_CHK，任一不满足即返回 false；
 *   - 遍历 bnc 的下一跳，按 ifindex（v6 LL 场景优先用 ifindex_ipv6_ll）找到接口，
 *     检查其 BGP_INTERFACE_MPLS_BGP_FORWARDING 标志。
 * 返回：找到具备该能力的接口返回 true，否则 false。 */
static int bgp_isvalid_nexthop_for_ebgp(struct bgp_nexthop_cache *bnc,
					struct bgp_path_info *path)
{
	struct interface *ifp = NULL;
	struct nexthop *nexthop;
	struct bgp_interface *iifp;
	struct peer *peer;

	if (!path->extra || !path->extra->vrfleak ||
	    !path->extra->vrfleak->peer_orig)
		return false;

	peer = path->extra->vrfleak->peer_orig;

	/* only connected ebgp peers are valid */
	if (peer->sort != BGP_PEER_EBGP || peer->ttl != BGP_DEFAULT_TTL ||
	    CHECK_FLAG(peer->flags, PEER_FLAG_DISABLE_CONNECTED_CHECK) ||
	    CHECK_FLAG(peer->bgp->flags, BGP_FLAG_DISABLE_NH_CONNECTED_CHK))
		return false;

	for (nexthop = bnc->nexthop; nexthop; nexthop = nexthop->next) {
		if (nexthop->type == NEXTHOP_TYPE_IFINDEX ||
		    nexthop->type == NEXTHOP_TYPE_IPV4_IFINDEX ||
		    nexthop->type == NEXTHOP_TYPE_IPV6_IFINDEX) {
			ifp = if_lookup_by_index(bnc->ifindex_ipv6_ll
							 ? bnc->ifindex_ipv6_ll
							 : nexthop->ifindex,
						 bnc->bgp->vrf_id);
		}
		if (!ifp)
			continue;
		iifp = ifp->info;
		if (CHECK_FLAG(iifp->flags, BGP_INTERFACE_MPLS_BGP_FORWARDING))
			return true;
	}
	return false;
}

/* 【函数】bgp_isvalid_nexthop_for_mplsovergre —— MPLS over GRE / GRE-TAP 的有效性。
 * 场景：L3VPN 的下一跳经 GRE/GRE-TAP 隧道到达时，只要接口类型匹配且路由映射
 *   (route-map) 允许接受该隧道类型（BATTR_RMAP_L3VPN_ACCEPT_GRE / _GRETAP），
 *   即认为该下一跳可用。
 * 实现：遍历 bnc 的下一跳（跳过黑洞类型），按 ifindex（v6 LL 优先 ifindex_ipv6_ll）
 *   取接口，检查 ifp->zif_type 与 path->attr->rmap_change_flags 的组合。
 * 返回：匹配到任一允许的隧道类型即 true，否则 false。 */
static int bgp_isvalid_nexthop_for_mplsovergre(struct bgp_nexthop_cache *bnc,
					       struct bgp_path_info *path)
{
	struct interface *ifp = NULL;
	struct nexthop *nexthop;

	for (nexthop = bnc->nexthop; nexthop; nexthop = nexthop->next) {
		if (nexthop->type != NEXTHOP_TYPE_BLACKHOLE) {
			ifp = if_lookup_by_index(bnc->ifindex_ipv6_ll
							 ? bnc->ifindex_ipv6_ll
							 : nexthop->ifindex,
						 bnc->bgp->vrf_id);
			if (!ifp)
				continue;
			if ((ifp->zif_type == ZEBRA_IF_GRE || ifp->zif_type == ZEBRA_IF_IP6GRE) &&
			    CHECK_FLAG(path->attr->rmap_change_flags, BATTR_RMAP_L3VPN_ACCEPT_GRE))
				return true;
			if ((ifp->zif_type == ZEBRA_IF_GRETAP ||
			     ifp->zif_type == ZEBRA_IF_IP6GRETAP) &&
			    CHECK_FLAG(path->attr->rmap_change_flags,
				       BATTR_RMAP_L3VPN_ACCEPT_GRETAP))
				return true;
		}
	}
	return false;
}

/* 【函数】bgp_isvalid_nexthop_for_mpls —— MPLS-VPN 下一跳的“或”组合判据。
 * 成立条件（前提 bnc 有 nexthop_num>0，下列任一为真即可）：
 *   - path 带 BGP_PATH_ACCEPT_OWN：本机自产、允许自收的路由；
 *   - bnc 带 BGP_NEXTHOP_LABELED_VALID：标签已就绪（VPN 转发依赖标签）；
 *   - bgp_isvalid_nexthop_for_ebgp：eBGP 直连 + MPLS 转发能力；
 *   - bgp_isvalid_nexthop_for_mplsovergre：MPLS over GRE/GRETAP 且被 rmap 允许。
 * 注意：这是 L3VPN 场景的核心判据——普通 VALID 往往不够，还要“有标签”或上述放宽。 */
static int bgp_isvalid_nexthop_for_mpls(struct bgp_nexthop_cache *bnc,
					struct bgp_path_info *path)
{
	return (bnc && (bnc->nexthop_num > 0 &&
			(CHECK_FLAG(path->flags, BGP_PATH_ACCEPT_OWN) ||
			 CHECK_FLAG(bnc->flags, BGP_NEXTHOP_LABELED_VALID) ||
			 bgp_isvalid_nexthop_for_ebgp(bnc, path) ||
			 bgp_isvalid_nexthop_for_mplsovergre(bnc, path))));
}

/* 【函数】bgp_isvalid_nexthop_for_l3vpn —— L3VPN 下一跳有效性主判据。
 * 分支：
 *   - zebra 未连接 → 返回有效（同 bgp_isvalid_nexthop，降级容忍）；
 *   - SRv6-VPN / SRv6-L3service：需要跟踪的是 SID（IPv6 地址）的可达性，故用通用
 *     bgp_isvalid_nexthop（要求 VALID 且 nexthop_num>0）；
 *   - 否则按 MPLS-VPN 处理：标签由 LDP 等学习，记录为 BGP_NEXTHOP_LABELED_VALID，
 *     交给 bgp_isvalid_nexthop_for_mpls。
 * 返回：1 有效 / 0 无效。 */
static bool bgp_isvalid_nexthop_for_l3vpn(struct bgp_nexthop_cache *bnc,
					  struct bgp_path_info *path)
{
	if (bgp_zebra_num_connects() == 0)
		return 1;

	if (path->attr->srv6_l3service || path->attr->srv6_vpn) {
		/* In the case of SRv6-VPN, we need to track the reachability to the
		 * SID (in other words, IPv6 address). We check that the SID is
		 * available in the BGP update; then if it is available, we check
		 * for the nexthop reachability.
		 */
		if (bnc && (bnc->nexthop_num > 0 && bgp_isvalid_nexthop(bnc)))
			return 1;
		return 0;
	}
	/*
	 * In the case of MPLS-VPN, the label is learned from LDP or other
	 * protocols, and nexthop tracking is enabled for the label.
	 * The value is recorded as BGP_NEXTHOP_LABELED_VALID.
	 * - Otherwise check for mpls-gre acceptance
	 */
	return bgp_isvalid_nexthop_for_mpls(bnc, path);
}

/* ========== 2. 路径↔下一跳映射的拆除、迁移与批量清理 ========== */
/* 【函数】bgp_unlink_nexthop_check —— “引用归零则回收”的检查点（bnc 生命周期闸门）。
 * 触发条件：bnc->paths 为空 **且** bnc->nht_info 为空（既无路径引用，也无 peer 绑定）。
 * 动作：
 *   - 打印调试；
 *   - 仅当同前缀已无其它 bnc（bnc_existing_for_prefix 为假）才向 zebra 注销
 *     （unregister_zebra_rnh）——因为同前缀的其它 bnc（不同 color/ifindex）可能
 *     仍在共用 zebra 的登记；
 *   - 释放 bnc（bnc_free）。
 * 为什么重要：只有 path_count 归零且无 peer 才能删，否则会造成悬垂指针（头文件已强调）。
 *   所有“拆映射后”的动作都以它收尾。 */
static void bgp_unlink_nexthop_check(struct bgp_nexthop_cache *bnc)
{
	if (LIST_EMPTY(&(bnc->paths)) && !bnc->nht_info) {
		if (BGP_DEBUG(nht, NHT))
			zlog_debug("%s: freeing bnc %pFX(%d)(%u)(%s)", __func__,
				   &bnc->prefix, bnc->ifindex_ipv6_ll,
				   bnc->srte_color, bnc->bgp->name_pretty);
		/* only unregister if this is the last nh for this prefix*/
		if (!bnc_existing_for_prefix(bnc))
			unregister_zebra_rnh(bnc);
		bnc_free(bnc);
	}
}

/* 【函数】bgp_unlink_nexthop —— 拆除单条路径与其下一跳缓存(bnc)的关联。
 * 参数：path —— 要拆的路径（其 path->nexthop 指向所用 bnc）。
 * 步骤：
 *   1) 先解掉该路径上的 MPLS 标签链接（label / label-bind），避免留下标签悬挂；
 *   2) path->nexthop 为空则无事可做，直接返回；
 *   3) path_nh_map(path, NULL, false)：从 bnc->paths 摘除并 path_count--；
 *   4) bgp_unlink_nexthop_check(bnc)：若该 bnc 已无人引用则注销并释放。
 * 调用时机：路径被替换/删除，或 bgp_find_or_add_nexthop 发现路径要改挂到别的 bnc 时。 */
void bgp_unlink_nexthop(struct bgp_path_info *path)
{
	struct bgp_nexthop_cache *bnc = path->nexthop;

	bgp_mplsvpn_path_nh_label_unlink(path);
	bgp_mplsvpn_path_nh_label_bind_unlink(path);

	if (!bnc)
		return;

	path_nh_map(path, NULL, false);

	bgp_unlink_nexthop_check(bnc);
}

/* 【函数】bgp_replace_nexthop_by_peer —— 把旧连接上的直连下一跳登记“迁”到新连接。
 * 场景：BGP 连接碰撞(collision)处理时用新连接替换旧连接；此时挂在旧连接的 bnc 上的
 *   nht_info（peer 指针）若不一并更新，换心后下一跳跟踪就会断。
 * 实现：分别用 from/to 的 sockunion 求 host prefix，并对 v6 LL 用 scope_id 求 ifindex，
 *   在对应 AFI 的 nexthop_cache_table 里 bnc_find 出两个 bnc；仅当两者是同一个实例
 *   (bnct == bncp)时，把 bnct->nht_info 指向新 peer。
 * 关键点：ifindex 只在“v6 链路本地且 conf_if 场景”才有意义（见头文件对 bnc 键的说明）。 */
void bgp_replace_nexthop_by_peer(struct peer_connection *from, struct peer_connection *to)
{
	struct peer *from_peer = from->peer;
	struct peer *to_peer = to->peer;
	struct prefix pp;
	struct prefix pt;
	struct bgp_nexthop_cache *bncp, *bnct;
	afi_t afi;
	ifindex_t ifindex = 0;

	if (!sockunion2hostprefix(&from->su, &pp))
		return;

	/*
	 * Gather the ifindex for if up/down events to be
	 * tagged into this fun
	 */
	if (from_peer->conf_if && IN6_IS_ADDR_LINKLOCAL(&from->su.sin6.sin6_addr))
		ifindex = from->su.sin6.sin6_scope_id;

	afi = family2afi(pp.family);
	bncp = bnc_find(&from_peer->bgp->nexthop_cache_table[afi], &pp, 0, ifindex);

	if (!sockunion2hostprefix(&to->su, &pt))
		return;

	/*
	 * Gather the ifindex for if up/down events to be
	 * tagged into this fun
	 */
	ifindex = 0;
	if (to_peer->conf_if && IN6_IS_ADDR_LINKLOCAL(&to->su.sin6.sin6_addr))
		ifindex = to->su.sin6.sin6_scope_id;
	bnct = bnc_find(&to_peer->bgp->nexthop_cache_table[afi], &pt, 0, ifindex);

	if (bnct != bncp)
		return;

	if (bnct)
		bnct->nht_info = to_peer;
}

/* 【函数】bgp_find_ipv6_nexthop_matching_peer —— 按 peer 反查它绑定的 v6 bnc。
 * 为什么需要：未编号(unnumbered)场景下，接口先被 shutdown 会把 peer->connection->su
 *   清空，导致无法用地址去 bnc_find；此时只能遍历 IPv6 下一跳表，找 nht_info == peer
 *   的那条 bnc。
 * 实现：frr_each 遍历 AFI_IP6 的 nexthop_cache_table，命中即返回（可带调试打印）。
 * 返回：找到的 bnc，找不到返回 NULL。 */
/*
 * Returns the bnc whose bnc->nht_info matches the LL peer by
 * looping through the IPv6 nexthop table
 */
static struct bgp_nexthop_cache *
bgp_find_ipv6_nexthop_matching_peer(struct peer *peer)
{
	struct bgp_nexthop_cache *bnc;

	frr_each (bgp_nexthop_cache, &peer->bgp->nexthop_cache_table[AFI_IP6],
		  bnc) {
		if (bnc->nht_info == peer) {
			if (BGP_DEBUG(nht, NHT)) {
				zlog_debug(
					"Found bnc: %pFX(%u)(%u)(%p) for peer: %s(%s) %p",
					&bnc->prefix, bnc->ifindex_ipv6_ll,
					bnc->srte_color, bnc, peer->host,
					peer->bgp->name_pretty, peer);
			}
			return bnc;
		}
	}

	if (BGP_DEBUG(nht, NHT))
		zlog_debug(
			"Could not find bnc for peer %s(%s) %p in v6 nexthop table",
			peer->host, peer->bgp->name_pretty, peer);

	return NULL;
}

/* 【函数】bgp_unlink_nexthop_by_peer —— peer 关闭/删除时批量清理其下一跳关联。
 * 步骤：
 *   - 由 peer->connection->su 求 host prefix；若失败（su 已清空，未编号场景），
 *     退化为 bgp_find_ipv6_nexthop_matching_peer 按 peer 反查 bnc；
 *   - 否则按 AFI 与 v6 LL 的 scope_id 求 ifindex，正常 bnc_find；
 *   - 找到后清空 bnc->nht_info，再 bgp_unlink_nexthop_check 做“无引用则回收”。
 * 注意：这里**只解 peer 绑定，不主动删路径映射**——路径可能仍被别的用途引用，
 *   是否释放交给 check（看 paths 是否已空）。 */
void bgp_unlink_nexthop_by_peer(struct peer *peer)
{
	struct prefix p;
	struct bgp_nexthop_cache *bnc;
	afi_t afi = family2afi(peer->connection->su.sa.sa_family);
	ifindex_t ifindex = 0;

	if (!sockunion2hostprefix(&peer->connection->su, &p)) {
		/*
		 * In scenarios where unnumbered BGP session is brought
		 * down by shutting down the interface before unconfiguring
		 * the BGP neighbor, neighbor information in peer->su.sa
		 * will be cleared when the interface is shutdown. So
		 * during the deletion of unnumbered bgp peer, above check
		 * will return true. Therefore, in this case,BGP needs to
		 * find the bnc whose bnc->nht_info matches the
		 * peer being deleted and free it.
		 */
		bnc = bgp_find_ipv6_nexthop_matching_peer(peer);
	} else {
		/*
		 * Gather the ifindex for if up/down events to be
		 * tagged into this fun
		 */
		if (afi == AFI_IP6 &&
		    IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr))
			ifindex = peer->connection->su.sin6.sin6_scope_id;
		bnc = bnc_find(&peer->bgp->nexthop_cache_table[afi], &p, 0,
			       ifindex);
	}

	if (!bnc)
		return;

	/* cleanup the peer reference */
	bnc->nht_info = NULL;

	bgp_unlink_nexthop_check(bnc);
}

/* ========== 3. 登记入口 bgp_find_or_add_nexthop（NHT 核心） ========== */
/* 【函数】bgp_find_or_add_nexthop —— NHT 的“登记入口”（本文件最核心的函数）。
 * 职责：为某条路径(pi)或某个 peer 的下一跳，找到或新建对应的 bnc，必要时向 zebra
 *   登记下一跳、建立 path↔bnc 映射，并返回“该下一跳现在是否有效”。
 *
 * 参数（重点）：
 *   bgp_route   —— **路由所属**的 BGP 实例；bgp_nexthop —— **下一跳所属**的 BGP 实例。
 *       二者可能不同（import vrf / MPLS-VPN 场景：路由在一个 VRF、下一跳在另一个），
 *       所以 bnc 挂在 bgp_nexthop 的表里，而有效性/回注判断要用 bgp_route。这是本
 *       函数最容易看错之处，不要想当然认为二者相同。
 *   pi          —— 使用该下一跳的路径；为 NULL 时进入 peer 模式（为邻居连接登记）。
 *   peer        —— 关联的邻居（peer 模式必给；pi 模式下也用于 v6 LL 的 ifindex 判断）。
 *   connected   —— true 表示要求该下一跳必须是直连路由（eBGP 单跳的严格校验）。
 *   orig_prefix —— 原始前缀，用于 import-check 的“下一跳绕回自身”检测。
 *   source_pi   —— 某些场景（如 vrf-leak）用它的 attr/peer 来构造被跟踪前缀。
 * 返回值：0 表示“无需跟踪/登记失败/自环”等（调用方据此把路径视为不可用或另作处理）；
 *   非 0 表示该下一跳已被登记/找到，其值即当前有效性判定（多为 bgp_isvalid_nexthop 系列）。
 *
 * 关键流程（对照代码中的分支）：
 *   1) 给 pi 时：判定是否 BGP 静态路由(BGP_ROUTE_STATIC)；按 RFC5549 从下一跳编码推
 *      AFI（IPv4-mapped-IPv6 归 IPv4）；make_prefix 得到被跟踪前缀 p；v6 LL 且前缀与
 *      对端 LL 相同则带上 ifindex；import-check 下若“前缀绕回自身”直接返回 0；取 SR-TE color。
 *   2) 给 peer 时：从 su 求前缀，v6 LL 依赖 scope_id 求 ifindex（拿不到就返回等待）。
 *   3) 选表：静态路由用 import_check_table，否则 nexthop_cache_table。
 *   4) bnc_find → 没有则 bnc_new（新建**不**登记 zebra，登记在 step 7）。
 *   5) 维护 flags：EVPN 网关 IP、静态路由/精确匹配、connected 要求、ifindex 变化——
 *      任一“改变了语义”的都要清 REGISTERED/VALID 以便重新登记（避免重复登记）。
 *   6) VIEW 实例特殊：无底层下一跳概念，直接当作已登记且有效。
 *   7) 需要则 register_zebra_rnh。
 *   8) 建立 path↔bnc 映射（先拆旧的再挂新的），必要时置 ULTIMATE、拷 igpmetric。
 *   9) 按 safi/sub_type 选择返回哪一种有效性判据。
 */
/*
 * A route and its nexthop might belong to different VRFs. Therefore,
 * we need both the bgp_route and bgp_nexthop pointers.
 */
int bgp_find_or_add_nexthop(struct bgp *bgp_route, struct bgp *bgp_nexthop, afi_t afi, safi_t safi,
			    struct bgp_path_info *pi, struct peer *peer, int connected,
			    const struct prefix *orig_prefix, struct bgp_path_info *source_pi)
{
	struct bgp_nexthop_cache_head *tree = NULL;
	struct bgp_nexthop_cache *bnc;
	struct bgp_path_info *bpi_ultimate;
	struct prefix p;
	uint32_t srte_color = 0;
	int is_bgp_static_route = 0;
	ifindex_t ifindex = 0;

	/* ---- 模式一：以一条路径(pi)为中心登记，下一跳取自 path 的属性 ---- */
	if (pi) {
		is_bgp_static_route = ((pi->type == ZEBRA_ROUTE_BGP)
				       && (pi->sub_type == BGP_ROUTE_STATIC))
					      ? 1
					      : 0;

		/* 【关键】按 RFC5549(ENHE)：下一跳地址族可能与前缀地址族不同，
		 * 故由下一跳编码长度反推 AFI，而不是想当然用前缀的地址族。 */
		/* Since Extended Next-hop Encoding (RFC5549) support, we want
		   to derive
		   address-family from the next-hop. */
		if (!is_bgp_static_route)
			afi = BGP_ATTR_MP_NEXTHOP_LEN_IP6(pi->attr) ? AFI_IP6
								    : AFI_IP;

		/* Validation for the ipv4 mapped ipv6 nexthop. */
		if (IS_MAPPED_IPV6(&pi->attr->mp_nexthop_global)) {
			afi = AFI_IP;
		}

		/* This will return true if the global IPv6 NH is a link local
		 * addr */
		if (!make_prefix(afi, pi, &p, bgp_nexthop, source_pi))
			return 1;

		/*
		 * If it's a V6 nexthop, path is learnt from a v6 LL peer,
		 * and if the NH prefix matches peer's LL address then
		 * set the ifindex to peer's interface index so that
		 * correct nexthop can be found in nexthop tree.
		 *
		 * NH could be set to different v6 LL address (compared to
		 * peer's LL) using route-map. In such a scenario, do not set
		 * the ifindex.
		 *
		 * Only do this for dynamic LL peers (conf_if set) where
		 * scope_id is populated early from ifp->ifindex.  For
		 * explicit LL peers (conf_if NULL, e.g. "neighbor fe80::X
		 * interface swpN") the scope_id arrives only after the TCP
		 * handshake; using it here would create a BNC keyed with the
		 * real ifindex while peer-tracking already created one with
		 * ifindex 0, causing a stale NHT entry after session flaps.
		 */
		if (afi == AFI_IP6 && pi->peer->conf_if &&
		    IN6_IS_ADDR_LINKLOCAL(
			    &pi->peer->connection->su.sin6.sin6_addr) &&
		    IPV6_ADDR_SAME(&pi->peer->connection->su.sin6.sin6_addr,
				   &p.u.prefix6))
			ifindex = pi->peer->connection->su.sin6.sin6_scope_id;

		if (!is_bgp_static_route && orig_prefix && prefix_same(&p, orig_prefix) &&
		    CHECK_FLAG(bgp_route->flags, BGP_FLAG_IMPORT_CHECK)) {
			if (BGP_DEBUG(nht, NHT)) {
				zlog_debug("%s(%pFX): prefix loops through itself (import-check enabled)",
					   __func__, &p);
			}
			return 0;
		}

		srte_color = bgp_attr_get_color(pi->attr);

	/* ---- 模式二：以邻居(peer)为中心登记（直连/未编号邻居的连通性跟踪） ---- */
	} else if (peer) {
		/*
		 * Gather the ifindex for if up/down events to be
		 * tagged into this fun
		 */
		if (afi == AFI_IP6 && peer->conf_if &&
		    IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr)) {
			ifindex = peer->connection->su.sin6.sin6_scope_id;
			if (ifindex == 0) {
				if (BGP_DEBUG(nht, NHT)) {
					zlog_debug(
						"%s: Unable to locate ifindex, waiting till we have one",
						peer->conf_if);
				}
				return 0;
			}
		}

		if (!sockunion2hostprefix(&peer->connection->su, &p)) {
			if (BGP_DEBUG(nht, NHT)) {
				zlog_debug(
					"%s: Attempting to register with unknown AFI %d (not %d or %d)",
					__func__, afi, AFI_IP, AFI_IP6);
			}
			return 0;
		}
	} else
		return 0;

	/* 选表：BGP 静态路由走“import 检查表”（用于 import-check 语义），其余走常规
	 * 下一跳缓存表。两套表都挂在 bgp_nexthop 实例上（下一跳所属实例）。 */
	if (is_bgp_static_route)
		tree = &bgp_nexthop->import_check_table[afi];
	else
		tree = &bgp_nexthop->nexthop_cache_table[afi];

	bnc = bnc_find(tree, &p, srte_color, ifindex);
	if (!bnc) {
		bnc = bnc_new(tree, &p, srte_color, ifindex);
		bnc->afi = afi;
		bnc->bgp = bgp_nexthop;
		if (BGP_DEBUG(nht, NHT))
			zlog_debug("Allocated bnc %pFX(%d)(%u)(%s) peer %p",
				   &bnc->prefix, bnc->ifindex_ipv6_ll,
				   bnc->srte_color, bnc->bgp->name_pretty,
				   peer);
	} else {
		if (BGP_DEBUG(nht, NHT))
			zlog_debug("Found existing bnc %pFX(%d)(%s) flags 0x%x ifindex %d #paths %d peer %p, resolved prefix %pFX",
				   &bnc->prefix, bnc->ifindex_ipv6_ll,
				   bnc->bgp->name_pretty, bnc->flags,
				   bnc->ifindex_ipv6_ll, bnc->path_count,
				   bnc->nht_info, &bnc->resolved_prefix);
	}

	/* EVPN 场景：若路径是 EVPN 的父路由，标记该 bnc 为“EVPN 网关 IP 下一跳”。
	 * 这类下一跳的有效性判据特殊（需 L3 可达 + MAC/IP 已解析，见 bgp_nexthop.h 对照表）。 */
	if (pi && is_route_parent_evpn(pi))
		bnc->is_evpn_gwip_nexthop = true;

	/* 静态路由首次登记，或 import-check 开关在“精确匹配/非精确匹配”之间翻转时，
	 * 都需清掉 REGISTERED/VALID，下面会按新语义重新向 zebra 登记。 */
	if (is_bgp_static_route && !CHECK_FLAG(bnc->flags, BGP_STATIC_ROUTE)) {
		SET_FLAG(bnc->flags, BGP_STATIC_ROUTE);

		/* If we're toggling the type, re-register */
		if ((CHECK_FLAG(bgp_route->flags, BGP_FLAG_IMPORT_CHECK))
		    && !CHECK_FLAG(bnc->flags, BGP_STATIC_ROUTE_EXACT_MATCH)) {
			SET_FLAG(bnc->flags, BGP_STATIC_ROUTE_EXACT_MATCH);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		} else if ((!CHECK_FLAG(bgp_route->flags,
					BGP_FLAG_IMPORT_CHECK))
			   && CHECK_FLAG(bnc->flags,
					 BGP_STATIC_ROUTE_EXACT_MATCH)) {
			UNSET_FLAG(bnc->flags, BGP_STATIC_ROUTE_EXACT_MATCH);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		}
	}
	/* When nexthop is already known, but now requires 'connected'
	 * resolution,
	 * re-register it. The reverse scenario where the nexthop currently
	 * requires
	 * 'connected' resolution does not need a re-register (i.e., we treat
	 * 'connected-required' as an override) except in the scenario where
	 * this
	 * is actually a case of tracking a peer for connectivity (e.g., after
	 * disable connected-check).
	 * NOTE: We don't track the number of paths separately for 'connected-
	 * required' vs 'connected-not-required' as this change is not a common
	 * scenario.
	 */
	/* connected 语义只是“是否要求直连”，不单独计数；一旦要求在“要求/不要求”之间
	 * 切换，就把登记/有效性清掉，下面会按新语义重新登记（避免用旧语义的 zebra 结果）。 */
	else if (connected && !CHECK_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED)) {
		SET_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
	} else if (peer && !connected
		   && CHECK_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED)) {
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
	}
	if (peer && (bnc->ifindex_ipv6_ll != ifindex)) {
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		bnc->ifindex_ipv6_ll = ifindex;
	}
	/* VIEW（show/bgp view）实例无底层转发表可查，无从判断下一跳，直接“作弊”当成
	 * 已登记且有效；其它实例则在未登记时向 zebra 登记（默认主机路由除外）。 */
	if (bgp_route->inst_type == BGP_INSTANCE_TYPE_VIEW) {
		SET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
		SET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
	} else if (!CHECK_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED)
		   && !is_default_host_route(&bnc->prefix))
		register_zebra_rnh(bnc);

	/* 路径原本挂的 bnc 与目标不是同一个 → 先拆旧映射（可能顺带回收旧 bnc），
	 * 再 path_nh_map 建立到新 bnc 的映射。这是 path↔bnc 多对多关系的落脚点。 */
	if (pi && pi->nexthop != bnc) {
		/* Unlink from existing nexthop cache, if any. This will also
		 * free
		 * the nexthop cache entry, if appropriate.
		 */
		bgp_unlink_nexthop(pi);

		/* updates NHT pi list reference */
		path_nh_map(pi, bnc, true);

		bpi_ultimate = bgp_get_imported_bpi_ultimate(pi);
		if (bpi_ultimate != pi && safi == SAFI_UNICAST &&
		    (CHECK_FLAG(bgp_route->af_flags[afi][SAFI_UNICAST],
				BGP_CONFIG_VRF_TO_VRF_IMPORT) ||
		     CHECK_FLAG(bgp_route->af_flags[afi][SAFI_UNICAST],
				BGP_CONFIG_VRF_TO_VRF_EXPORT)))
			/* import does not require any valid MPLS label */
			SET_FLAG(bnc->flags, BGP_NEXTHOP_ULTIMATE);

		if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_VALID) && bnc->metric)
			(bgp_path_info_extra_get(bpi_ultimate))->igpmetric =
				bnc->metric;
		else if (bpi_ultimate->extra)
			bpi_ultimate->extra->igpmetric = 0;
	} else if (peer) {
		/*
		 * Let's not accidentally save the peer data for a peer
		 * we are going to throw away in a second or so.
		 * When we come back around we'll fix up this
		 * data properly in replace_nexthop_by_peer
		 */
		if (peer_is_config_node(peer))
			bnc->nht_info = (void *)peer; /* NHT peer reference */
	}

	/* 【返回值语义】下面的 return 决定调用方如何看待这条路径的下一跳：
	 *   - VIEW：一律 1（有效）；
	 *   - 单播 + IMPORTED + ULTIMATE：用通用判据（import 不要求有效 MPLS 标签）；
	 *   - 单播 + IMPORTED + 带标签 且非 EVPN 网关 IP：用 L3VPN 判据（要标签）；
	 *   - MPLS-VPN + 非 IMPORTED：直接 1（避免把 mpls vpn 路由判为不可重分发）；
	 *   - 其余（MPLS-VPN 的 IMPORTED）：用通用判据。 */
	/*
	 * We are cheating here.  Views have no associated underlying
	 * ability to detect nexthops.  So when we have a view
	 * just tell everyone the nexthop is valid
	 */
	if (bgp_route->inst_type == BGP_INSTANCE_TYPE_VIEW)
		return 1;
	else if (safi == SAFI_UNICAST && pi &&
		 pi->sub_type == BGP_ROUTE_IMPORTED &&
		 CHECK_FLAG(bnc->flags, BGP_NEXTHOP_ULTIMATE))
		return bgp_isvalid_nexthop(bnc);
	else if (safi == SAFI_UNICAST && pi &&
		 pi->sub_type == BGP_ROUTE_IMPORTED &&
		 BGP_PATH_INFO_NUM_LABELS(pi) && !bnc->is_evpn_gwip_nexthop)
		return bgp_isvalid_nexthop_for_l3vpn(bnc, pi);
	else if (safi == SAFI_MPLS_VPN && pi &&
		 pi->sub_type != BGP_ROUTE_IMPORTED)
		/* avoid not redistributing mpls vpn routes */
		return 1;
	else
		/* mpls-vpn routes with BGP_ROUTE_IMPORTED subtype */
		return (bgp_isvalid_nexthop(bnc));
}

/* ========== 4. 直连下一跳(peer 绑定)清理 与 NHT 重要标记 ========== */
/* 【函数】bgp_delete_connected_nexthop —— peer 删除时清掉它对 bnc 的绑定。
 * 场景：直连场景下 bnc 会记录“这个下一跳属于哪个邻居”（bnc->nht_info = peer）。
 *   peer 走了要解绑，但 bnc 可能还被别的路径用着，不能直接删。
 * 实现：
 *   - su 可用则按 AFI/ifindex bnc_find；su 已清空则退化为按 peer 反查 v6 bnc；
 *   - 仅当 bnc->nht_info == peer 才清空（防止误清别人绑的 peer）；
 *   - 若清空后 paths 已空，则 unregister_zebra_rnh + bnc_free 回收。
 * 与 bgp_unlink_nexthop_by_peer 的区别：本函数是“已确认是直连下一跳”的定向清理。 */
void bgp_delete_connected_nexthop(afi_t afi, struct peer *peer)
{
	struct bgp_nexthop_cache *bnc;
	struct prefix p;
	ifindex_t ifindex = 0;

	if (!peer)
		return;

	if (!sockunion2hostprefix(&peer->connection->su, &p)) {
		/*
		 * If peer->connection->su is cleared before peer deletion,
		 * find the bnc whose nht_info matches the peer and free it.
		 */
		bnc = bgp_find_ipv6_nexthop_matching_peer(peer);
	} else {
		/*
		 * Gather the ifindex for if up/down events to be
		 * tagged into this fun
		 */
		if (afi == AFI_IP6 && IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr))
			ifindex = peer->connection->su.sin6.sin6_scope_id;
		bnc = bnc_find(&peer->bgp->nexthop_cache_table[family2afi(p.family)], &p, 0,
			       ifindex);
	}

	if (!bnc) {
		if (BGP_DEBUG(nht, NHT))
			zlog_debug(
				"Cannot find connected NHT node for peer %s(%s)",
				peer->host, peer->bgp->name_pretty);
		return;
	}

	if (bnc->nht_info != peer) {
		if (BGP_DEBUG(nht, NHT))
			zlog_debug(
				"Connected NHT %p node for peer %s(%s) points to %p",
				bnc, peer->host, bnc->bgp->name_pretty,
				bnc->nht_info);
		return;
	}

	bnc->nht_info = NULL;

	if (LIST_EMPTY(&(bnc->paths))) {
		if (BGP_DEBUG(nht, NHT))
			zlog_debug(
				"Freeing connected NHT node %p for peer %s(%s)",
				bnc, peer->host, bnc->bgp->name_pretty);
		unregister_zebra_rnh(bnc);
		bnc_free(bnc);
	}
}

/* 【函数】bgp_bnc_mark_nht_important —— 标记“被用作 NHT 解析结果”的那条 BGP 路由。
 * 场景：当下一跳最终由一条 BGP 路由解析（nhr->type == ZEBRA_ROUTE_BGP）时，该 BGP
 *   前缀就成了“NHT 关键节点”。在其所在 RIB 节点上置 BGP_NODE_NHT_RESOLVED_NODE，
 *   便于展示/依赖追踪；同时清掉旧的解析前缀上的该标记。
 * 关键点：
 *   - 只处理 BGP 类型解析结果，其它协议直接返回；
 *   - 新旧解析前缀相同则无需处理；
 *   - 用 bgp_afi_node_get 取节点并 bgp_dest_unlock_node 归还引用（注意配对）。 */
static void bgp_bnc_mark_nht_important(struct bgp_nexthop_cache *bnc, struct zapi_route *nhr)
{
	struct bgp *bgp = bnc->bgp;
	afi_t afi = family2afi(nhr->prefix.family);
	struct bgp_table *table = bgp->rib[afi][nhr->safi];
	struct bgp_dest *dest;

	if (nhr->type != ZEBRA_ROUTE_BGP)
		return;

	if (!table)
		return;

	/*
	 * If the old resolved prefix is the same as the new resolved prefix
	 * nothing to mark here, move along
	 */
	if (prefix_same(&bnc->resolved_prefix, &nhr->prefix))
		return;

	dest = bgp_afi_node_get(table, afi, nhr->safi, &bnc->resolved_prefix, NULL);
	if (dest) {
		UNSET_FLAG(dest->flags, BGP_NODE_NHT_RESOLVED_NODE);
		bgp_dest_unlock_node(dest);
	}

	dest = bgp_afi_node_get(table, afi, nhr->safi, &nhr->prefix, NULL);
	if (!dest)
		return;

	SET_FLAG(dest->flags, BGP_NODE_NHT_RESOLVED_NODE);
	bgp_dest_unlock_node(dest);
}

/* ========== 5. 处理 zebra 的下一跳更新 bgp_process_nexthop_update ========== */
/* 【函数】bgp_process_nexthop_update —— 把 zebra 给出的解析结果(nhr)写回 bnc，再扇出。
 * 参数：bnc 目标缓存；nhr zebra 的下一跳解析结果（可达性/metric/下一跳列表/协议类型）；
 *   import_check 为真表示这条 bnc 来自 import 检查表（对解析前缀有额外要求）。
 * 主要步骤：
 *   1) 记录 last_update、清 change_flags（本次重新计算“相对上次变了什么”）；
 *   2) 计算 METRIC_CHANGED / CHANGED（下一跳数量或逐跳内容变化）；
 *   3) import_check 语义：若解析结果来自 BGP 自身路由、或解析前缀与登记前缀不一致，
 *      则视为无效（清 VALID/LABELED_VALID/EVPN_INCOMPLETE，清空 nexthop 链表）；
 *   4) 有一跳可达时：拷 resolved_prefix、按需置 VALID、更新 metric/nexthop_num、
 *      重建下一跳链表并逐跳比较以判定 CHANGED、按需置 LABELED_VALID；
 *      ENHE 场景对 v6 下一跳接口打开 RA（让 v4-over-v6 的下一跳可解析）；
 *      EVPN 网关 IP：L3 可达但 MAC/IP 未解析 → EVPN_INCOMPLETE，解析成功才 VALID；
 *   5) 无任何下一跳时：清 resolved_prefix 与各有效标志，PEER_NOTIFIED 复位（valid→invalid）；
 *   6) evaluate_paths(bnc) 扇出到所有引用该 bnc 的路径。
 * 【PEER_NOTIFIED 的作用】有效性在 valid<->invalid 翻转时清掉它，使 evaluate_paths 里
 *   能再次通知 FSM（避免重复通知，也避免漏通知）。 */
static void bgp_process_nexthop_update(struct bgp_nexthop_cache *bnc,
				       struct zapi_route *nhr,
				       bool import_check)
{
	struct nexthop *nexthop;
	struct nexthop *oldnh;
	struct nexthop *nhlist_head = NULL;
	struct nexthop *nhlist_tail = NULL;
	int i;
	bool evpn_resolved = false;

	/* 每次处理新一次 zebra 更新：刷新时间戳，并把“本次变化位”清零后重算。 */
	bnc->last_update = monotime(NULL);
	bnc->change_flags = 0;

	/* debug print the input */
	if (BGP_DEBUG(nht, NHT)) {
		char bnc_buf[BNC_FLAG_DUMP_SIZE];

		zlog_debug("%s(%u): Rcvd NH update %pFX(%u)(%u) - metric %d/%d #nhops %d/%d flags %s redistributing protocol %s",
			   bnc->bgp->name_pretty, bnc->bgp->vrf_id, &nhr->prefix,
			   bnc->ifindex_ipv6_ll, bnc->srte_color, nhr->metric, bnc->metric,
			   nhr->nexthop_num, bnc->nexthop_num,
			   bgp_nexthop_dump_bnc_flags(bnc, bnc_buf, sizeof(bnc_buf)),
			   zebra_route_string(nhr->type));
	}

	bgp_bnc_mark_nht_important(bnc, nhr);

	if (nhr->metric != bnc->metric)
		SET_FLAG(bnc->change_flags, BGP_NEXTHOP_METRIC_CHANGED);

	if (nhr->nexthop_num != bnc->nexthop_num)
		SET_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED);

	/* import-check 语义：解析结果来自 BGP 自身、或解析到的前缀与登记前缀不一致，
	 * 都不算数——直接判为无效并清空下一跳链表。这正是“import 检查”要防的绕回/自环。 */
	if (import_check && (nhr->type == ZEBRA_ROUTE_BGP ||
			     !prefix_same(&bnc->prefix, &nhr->prefix))) {
		SET_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_LABELED_VALID);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE);

		bnc_nexthop_free(bnc);
		bnc->nexthop = NULL;

		if (BGP_DEBUG(nht, NHT))
			zlog_debug(
				"%s: Import Check does not resolve to the same prefix for %pFX received %pFX or matching route is BGP",
				__func__, &bnc->prefix, &nhr->prefix);
	/* 有一跳(或多跳)可达：登记为有效并重建下一跳链表。 */
	} else if (nhr->nexthop_num) {
		struct peer *peer = bnc->nht_info;

		prefix_copy(&bnc->resolved_prefix, &nhr->prefix);

		/* notify bgp fsm if nbr ip goes from invalid->valid */
		if (!bnc->nexthop_num)
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED);

		if (!bnc->is_evpn_gwip_nexthop)
			SET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		bnc->metric = nhr->metric;
		bnc->nexthop_num = nhr->nexthop_num;

		UNSET_FLAG(bnc->flags,
			   BGP_NEXTHOP_LABELED_VALID); /* check below */

		for (i = 0; i < nhr->nexthop_num; i++) {
			int num_labels = 0;

			nexthop = nexthop_from_zapi_nexthop(&nhr->nexthops[i]);

			/* ENHE(RFC5549)：对来自 BGP 的 v6 下一跳接口打开 RA，以便
			 * “IPv4 路由走 IPv6 下一跳”这种组合能被对端正确解析。 */
			/*
			 * Turn on RA for the v6 nexthops
			 * we receive from bgp.  This is to allow us
			 * to work with v4 routing over v6 nexthops
			 */
			if (peer && !peer->ifp &&
			    CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE) &&
			    !CHECK_FLAG(bnc->bgp->flags,
					BGP_FLAG_IPV6_NO_AUTO_RA) &&
			    nhr->prefix.family == AF_INET6 &&
			    nexthop->type != NEXTHOP_TYPE_BLACKHOLE) {
				struct interface *ifp;

				ifp = if_lookup_by_index(nexthop->ifindex,
							 nexthop->vrf_id);
				if (ifp)
					zclient_send_interface_radv_req(
						bgp_zclient, nexthop->vrf_id, ifp,
						true,
						BGP_UNNUM_DEFAULT_RA_INTERVAL);
			}
			/* There is at least one label-switched path */
			if (nexthop->nh_label &&
				nexthop->nh_label->num_labels) {
				SET_FLAG(bnc->flags, BGP_NEXTHOP_LABELED_VALID);
				num_labels = nexthop->nh_label->num_labels;
			}

			if (BGP_DEBUG(nht, NHT)) {
				char buf[NEXTHOP_STRLEN];
				zlog_debug(
					"    nhop via %s (%d labels)",
					nexthop2str(nexthop, buf, sizeof(buf)),
					num_labels);
			}

			if (nhlist_tail) {
				nhlist_tail->next = nexthop;
				nhlist_tail = nexthop;
			} else {
				nhlist_tail = nexthop;
				nhlist_head = nexthop;
			}

			/* 逐跳与旧链表比较，判断下一跳集合是否变化；已判定变化(CHANGED)
			 * 就无需再逐跳比较（continue 到下一跳）。 */
			/* No need to evaluate the nexthop if we have already
			 * determined
			 * that there has been a change.
			 */
			if (CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED))
				continue;

			for (oldnh = bnc->nexthop; oldnh; oldnh = oldnh->next)
				if (nexthop_same(oldnh, nexthop))
					break;

			if (!oldnh)
				SET_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED);
		}
		bnc_nexthop_free(bnc);
		bnc->nexthop = nhlist_head;

		/*
		 * Gateway IP nexthop is L3 reachable. Mark it as
		 * BGP_NEXTHOP_VALID only if it is recursively resolved with a
		 * remote EVPN RT-2.
		 * Else, mark it as BGP_NEXTHOP_EVPN_INCOMPLETE.
		 * When its mapping with EVPN RT-2 is established, unset
		 * BGP_NEXTHOP_EVPN_INCOMPLETE and set BGP_NEXTHOP_VALID.
		 */
		/* EVPN 网关 IP 下一跳：L3 已可达，但还需 MAC/IP 递归解析成功才真正 VALID。
		 * 解析成功 → 置 VALID 且清 EVPN_INCOMPLETE（并标 MACIP_CHANGED）；
		 * 未成功 → 置 EVPN_INCOMPLETE 并清 VALID（即头文件里那张对照表的第二行）。 */
		if (bnc->is_evpn_gwip_nexthop) {
			evpn_resolved = bgp_evpn_is_gateway_ip_resolved(bnc);

			if (BGP_DEBUG(nht, NHT))
				zlog_debug(
					"EVPN gateway IP %pFX recursive MAC/IP lookup %s",
					&bnc->prefix,
					(evpn_resolved ? "successful"
						       : "failed"));

			if (evpn_resolved) {
				SET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
				UNSET_FLAG(bnc->flags,
					   BGP_NEXTHOP_EVPN_INCOMPLETE);
				SET_FLAG(bnc->change_flags,
					 BGP_NEXTHOP_MACIP_CHANGED);
			} else {
				SET_FLAG(bnc->flags,
					 BGP_NEXTHOP_EVPN_INCOMPLETE);
				UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
			}
		}
	/* 无任何可达下一跳：清解析前缀与所有有效标志，并复位 PEER_NOTIFIED
	 * （valid→invalid 的翻转点，好让 evaluate_paths 能再通知一次 FSM）。 */
	} else {
		memset(&bnc->resolved_prefix, 0, sizeof(bnc->resolved_prefix));
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_LABELED_VALID);
		bnc->nexthop_num = nhr->nexthop_num;

		/* notify bgp fsm if nbr ip goes from valid->invalid */
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED);

		bnc_nexthop_free(bnc);
		bnc->nexthop = NULL;
	}

	/* 状态更新完毕，扇出到所有引用该 bnc 的路径（唯一通向路由表的出口）。 */
	evaluate_paths(bnc);
}

/* ========== 6. 接口 up/down 事件对 NHT 的影响 ========== */
/* 【函数】bgp_nht_ifp_table_handle —— 遍历一张 bnc 表，按接口状态更新相关 bnc。
 * 为什么需要：下一跳常是“接口上的地址”，接口没了下一跳自然失效；而 zebra 未必每次
 *   接口抖动都发 nexthop update，所以这里直接由接口事件兜底。
 * 匹配条件：该 bnc 的唯一下一跳正好落在此接口上（bnc->nexthop_num==1 且 ifindex 相同），
 *   或 bnc 记录的 v6 链路本地 ifindex 就是本接口。
 * 动作：up → 置 VALID/CHANGED、nexthop_num=1；down → 清 PEER_NOTIFIED/VALID、置
 *   CHANGED、nexthop_num=0。metric 置 0（直连路由的 metric 就是 0）。最后 evaluate_paths。
 * 注意：这里是**直接改 bnc 状态**再扇出，等价于收到一次 zebra 更新。 */
static void bgp_nht_ifp_table_handle(struct bgp *bgp,
				     struct bgp_nexthop_cache_head *table,
				     struct interface *ifp, bool up)
{
	struct bgp_nexthop_cache *bnc;

	if (ifp->ifindex == IFINDEX_INTERNAL) {
		zlog_warn("%s: The interface %s ignored", __func__, ifp->name);
		return;
	}

	/* 只关心“下一跳确实在本接口上”的 bnc：要么唯一一跳的 ifindex 就是本接口，
	 * 要么 bnc 的 v6 链路本地 ifindex 记录的是本接口；否则跳过。 */
	frr_each (bgp_nexthop_cache, table, bnc) {
		if (!(bnc->nexthop_num == 1 && bnc->nexthop &&
		      bnc->nexthop->ifindex == ifp->ifindex) &&
		    (bnc->ifindex_ipv6_ll != ifp->ifindex))
			continue;

		bnc->last_update = monotime(NULL);
		bnc->change_flags = 0;

		/*
		 * For interface based routes ( ala the v6 LL routes
		 * that this was written for ) the metric received
		 * for the connected route is 0 not 1.
		 */
		bnc->metric = 0;
		if (up) {
			SET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
			SET_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED);
			bnc->nexthop_num = 1;
		} else {
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
			SET_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED);
			bnc->nexthop_num = 0;
		}

		evaluate_paths(bnc);
	}
}
/* 【函数】bgp_nht_ifp_handle —— 对某个 bgp 实例的四张 bnc 表统一处理接口事件。
 * 四张表：nexthop_cache_table 与 import_check_table，各含 IPv4/IPv6。
 * ifp->vrf->info 为该 VRF 对应的 bgp 实例（没有则直接返回）。
 * down 时用 bgp_clearing_batch_begin/end_event_start 包住整批处理：批量清路由，
 *   避免中间态反复触发最佳路径计算，最后再统一启动事件。 */
static void bgp_nht_ifp_handle(struct interface *ifp, bool up)
{
	struct bgp *bgp;

	bgp = ifp->vrf->info;
	if (!bgp)
		return;

	/* 接口 down：进入“清路由批次”，减少逐条重算，批次结束后统一触发。 */
	if (!up)
		bgp_clearing_batch_begin(bgp);

	bgp_nht_ifp_table_handle(bgp, &bgp->nexthop_cache_table[AFI_IP], ifp,
				 up);
	bgp_nht_ifp_table_handle(bgp, &bgp->import_check_table[AFI_IP], ifp,
				 up);
	bgp_nht_ifp_table_handle(bgp, &bgp->nexthop_cache_table[AFI_IP6], ifp,
				 up);
	bgp_nht_ifp_table_handle(bgp, &bgp->import_check_table[AFI_IP6], ifp,
				 up);

	if (!up)
		bgp_clearing_batch_end_event_start(bgp);
}

/* 【函数】bgp_nht_ifp_up / bgp_nht_ifp_down —— 接口 up/down 的事件入口（薄封装）。
 * 由 zclient/接口模块在收到 ZEBRA_INTERFACE_UP/DOWN 时调用。 */
void bgp_nht_ifp_up(struct interface *ifp)
{
	bgp_nht_ifp_handle(ifp, true);
}

void bgp_nht_ifp_down(struct interface *ifp)
{
	bgp_nht_ifp_handle(ifp, false);
}

/* 【函数】bgp_nht_ifp_initial —— 延迟执行的事件：按 ifindex 重新判定接口状态并触发。
 * 场景：接口事件可能早于我们记录 ifindex，或需要“稍后再补一次”接口 up/down 上报；
 *   这里通过事件携带的 (bgp, ifindex) 反查接口，按当前 up/down 再走一遍处理。
 * 参数：event 的 val 是 ifindex，arg 是 bgp 实例。 */
static void bgp_nht_ifp_initial(struct event *event)
{
	ifindex_t ifindex = EVENT_VAL(event);
	struct bgp *bgp = EVENT_ARG(event);
	struct interface *ifp = if_lookup_by_index(ifindex, bgp->vrf_id);

	if (!ifp)
		return;

	if (BGP_DEBUG(nht, NHT))
		zlog_debug(
			"Handle NHT initial update for Intf %s(%d) status %s",
			ifp->name, ifp->ifindex, if_is_up(ifp) ? "up" : "down");

	if (if_is_up(ifp))
		bgp_nht_ifp_up(ifp);
	else
		bgp_nht_ifp_down(ifp);
}

/* 【函数】bgp_nht_interface_events —— peer 进入某种状态后，把相关 v6 LL bnc 的接口
 *   up/down 事件“补报”一次。FSM 期望在（如进入 active 之后）能收到 NHT 的跟踪事件，
 *   本函数就是让 NHT 按 FSM 期望的时序把接口状态重新上报，避免漏报。
 * 触发：仅对 v6 链路本地对端有意义；找到对应 bnc 后，用其 ifindex_ipv6_ll 排一个
 *   bgp_nht_ifp_initial 延迟事件。 */
/*
 * So the bnc code has the ability to handle interface up/down
 * events to properly handle v6 LL peering.
 * What is happening here:
 * The event system for peering expects the nht code to
 * report on the tracking events after we move to active
 * So let's give the system a chance to report on that event
 * in a manner that is expected.
 */
void bgp_nht_interface_events(struct peer *peer)
{
	struct bgp *bgp = peer->bgp;
	struct bgp_nexthop_cache_head *table;
	struct bgp_nexthop_cache *bnc;
	struct prefix p;
	ifindex_t ifindex = 0;

	if (!IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr))
		return;

	if (!sockunion2hostprefix(&peer->connection->su, &p))
		return;
	/*
	 * Gather the ifindex for if up/down events to be
	 * tagged into this fun
	 */
	if (peer->conf_if &&
	    IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr))
		ifindex = peer->connection->su.sin6.sin6_scope_id;

	table = &bgp->nexthop_cache_table[AFI_IP6];
	bnc = bnc_find(table, &p, 0, ifindex);
	if (!bnc)
		return;

	if (bnc->ifindex_ipv6_ll)
		event_add_event(bm->master, bgp_nht_ifp_initial, bnc->bgp,
				bnc->ifindex_ipv6_ll, NULL);
}

/* ========== 7. zebra 下一跳更新回调入口 bgp_nexthop_update ========== */
/* 【函数】bgp_nexthop_update —— 处理来自 zebra 的下一跳更新消息(nhr)，NHT 的外部入口。
 * 调用时机：zebra 发来 ZEBRA_NEXTHOP_UPDATE，由 bgp_zebra.c 转发到此。
 * 参数：vrf 消息所属 VRF（vrf->info 必须是 bgp 实例，否则记错误返回）；
 *   match 被跟踪的下一跳前缀；nhr zebra 的解析结果。
 * 处理：
 *   1) 在常规 nexthop_cache_table 里找 bnc 并按 import_check=false 处理；
 *   2) 在 import_check_table 里找 bnc 并按 import_check=true 处理；若命中且当前实例
 *      非 default 且有 RIB，还要对“本机 static 路由”触发 vpn_leak_from_vrf_update
 *      （把该 vrf 的静态路由导出到 VPN，import 结果变化会影响导出）。
 *   3) SR-TE 补丁（见下）：收到无色(srte_color==0)的 NH 更新时，顺带更新同前缀的
 *      彩色(colorful) bnc——因为 zebra 不会主动为不存在的 SR-policy 发更新。
 * 注意：vrf 与 bgp 实例必须对应上，多 VRF 下不能串台。 */
void bgp_nexthop_update(struct vrf *vrf, struct prefix *match,
			struct zapi_route *nhr)
{
	struct bgp_nexthop_cache_head *tree = NULL;
	struct bgp_nexthop_cache *bnc_nhc, *bnc_import;
	struct bgp *bgp, *bgp_default;
	struct bgp_path_info *pi;
	struct bgp_dest *dest;
	safi_t safi;
	afi_t afi;

	if (!vrf->info) {
		flog_err(EC_BGP_NH_UPD,
			 "parse nexthop update: instance not found for vrf_id %u",
			 vrf->vrf_id);
		return;
	}

	bgp = (struct bgp *)vrf->info;
	afi = family2afi(match->family);
	tree = &bgp->nexthop_cache_table[afi];

	/* 常规下一跳缓存表：找到对应 bnc 就按“普通下一跳”处理（import_check=false）。 */
	bnc_nhc = bnc_find(tree, match, nhr->srte_color, 0);
	if (bnc_nhc)
		bgp_process_nexthop_update(bnc_nhc, nhr, false);
	else if (BGP_DEBUG(nht, NHT))
		zlog_debug("parse nexthop update %pFX(%u)(%s): bnc info not found for nexthop cache",
			   &nhr->prefix, nhr->srte_color,
			   bgp->name_pretty);

	/* import 检查表：同样前缀若也登记过 import 检查，则按 import_check=true 处理
	 * （对解析前缀有更严格要求，见 bgp_process_nexthop_update）。 */
	tree = &bgp->import_check_table[afi];

	bnc_import = bnc_find(tree, match, nhr->srte_color, 0);
	if (bnc_import) {
		bgp_process_nexthop_update(bnc_import, nhr, true);

		bgp_default = bgp_get_default();
		safi = nhr->safi;
		if (bgp != bgp_default && bgp->rib[afi][safi]) {
			dest = bgp_afi_node_get(bgp->rib[afi][safi], afi, safi,
						match, NULL);

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next)
				if (pi->peer == bgp->peer_self &&
				    pi->type == ZEBRA_ROUTE_BGP &&
				    pi->sub_type == BGP_ROUTE_STATIC)
					vpn_leak_from_vrf_update(bgp_default,
								 bgp, pi);
			bgp_dest_unlock_node(dest);
		}
	} else if (BGP_DEBUG(nht, NHT))
		zlog_debug("parse nexthop update %pFX(%u)(%s): bnc info not found for import check",
			   &nhr->prefix, nhr->srte_color,
			   bgp->name_pretty);

	/* 【SR-TE 补丁】色无关(colorless)的 NH 更新到达时，主动把同前缀、带 color 的
	 * bnc 也当成收到更新处理一遍，避免因 zebra 不发 SR-policy 相关更新而漏更新。 */
	/*
	 * HACK: if any BGP route is dependant on an SR-policy that doesn't
	 * exist, zebra will never send NH updates relative to that policy. In
	 * that case, whenever we receive an update about a colorless NH, update
	 * the corresponding colorful NHs that share the same endpoint but that
	 * are inactive. This ugly hack should work around the problem at the
	 * cost of a performance pernalty. Long term, what should be done is to
	 * make zebra's RNH subsystem aware of SR-TE colors (like bgpd is),
	 * which should provide a better infrastructure to solve this issue in
	 * a more efficient and elegant way.
	 */
	if (nhr->srte_color == 0) {
		struct bgp_nexthop_cache *bnc_iter;

		frr_each (bgp_nexthop_cache, &bgp->nexthop_cache_table[afi],
			  bnc_iter) {
			if (!prefix_same(match, &bnc_iter->prefix) ||
			    bnc_iter->srte_color == 0)
				continue;

			bgp_process_nexthop_update(bnc_iter, nhr, false);
		}
	}
}

/* ========== 8. VRF 删除时的下一跳清理 bgp_cleanup_nexthops ========== */
/* 【函数】bgp_cleanup_nexthops —— VRF/实例删除时的兜底清理：遍历该实例 (IPv4/IPv6)
 *   nexthop_cache_table，把每条 bnc 的 VALID/REGISTERED/PEER_NOTIFIED/EVPN_INCOMPLETE
 *   都清掉，防止把已不存在实例的 bnc 留在“仍登记/仍有效”的状态（悬垂语义）。
 * 注意：这里只清标志，不负责释放 bnc 本体（释放由后续的表销毁/reset 逻辑处理）。 */
/*
 * Cleanup nexthop registration and status information for BGP nexthops
 * pertaining to this VRF. This is invoked upon VRF deletion.
 */
void bgp_cleanup_nexthops(struct bgp *bgp)
{
	for (afi_t afi = AFI_IP; afi < AFI_MAX; afi++) {
		struct bgp_nexthop_cache *bnc;

		frr_each (bgp_nexthop_cache, &bgp->nexthop_cache_table[afi],
			  bnc) {
			/* Clear relevant flags. */
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED);
			UNSET_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE);
		}
	}
}

/* ========== 9. 由路径构造被跟踪前缀 make_prefix ========== */
/* 【函数】make_prefix —— 从一条路径(pi)推出“要被跟踪的下一跳前缀” p。
 * 职责：NHT 跟踪的“键”是下一跳地址，不是路由前缀；本函数负责把 path 的 net 前缀
 *   或它的 BGP 属性(attr)换算成那个下一跳地址。
 * 参数：afi 目标地址族；pi 路径；p 出参（填好的前缀）；bgp_nexthop 下一跳所属实例
 *   （SRv6 SID 判定要用）；source_pi 提供 attr/peer 的来源路径（可为空）。
 * 分族处理：
 *   - flowspec：用 bgp_flowspec_get_first_nh 取；
 *   - AFI_IP：BGP 静态路由用 net 前缀；否则若下一跳是 IPv4-mapped-IPv6 则解回 IPv4，
 *     再否则用 attr->nexthop（EVPN 用 mp_nexthop_global_in），prefixlen 取 /32；
 *   - AFI_IP6：SRv6-L3service 特殊（SID / transposition）；静态用 net；否则按
 *     mp_nexthop_len 在 global 与 link-local 之间取舍（见下方注释）。
 * 返回：true 表示 p 已构造；false 表示无法构造/该下一跳不应被跟踪（如 global 地址
 *   实为链路本地且对端支持 LL 能力的情形）。 */
/**
 * make_prefix - make a prefix structure from the path (essentially
 * path's node.
 */
static bool make_prefix(int afi, struct bgp_path_info *pi, struct prefix *p,
			struct bgp *bgp_nexthop, struct bgp_path_info *source_pi)
{

	int is_bgp_static = ((pi->type == ZEBRA_ROUTE_BGP)
			     && (pi->sub_type == BGP_ROUTE_STATIC))
				    ? 1
				    : 0;
	struct bgp_dest *net = pi->net;
	const struct prefix *p_orig = bgp_dest_get_prefix(net);
	struct in_addr ipv4;
	struct peer *peer;
	struct attr *attr;
	bool local_sid = false;
	struct prefix_ipv6 tmp_prefix;

	if (source_pi) {
		attr = source_pi->attr;
		peer = source_pi->peer;
	} else {
		peer = pi->peer;
		attr = pi->attr;
	}

	/* flowspec 的“下一跳”不是普通地址，需从 flowspec NLRI 里提取第一个 NH。 */
	if (p_orig->family == AF_FLOWSPEC) {
		if (!peer)
			return false;
		return bgp_flowspec_get_first_nh(peer->bgp, pi, p, afi);
	}
	memset(p, 0, sizeof(struct prefix));
	switch (afi) {
	case AFI_IP:
		p->family = AF_INET;
		if (is_bgp_static) {
			/* BGP 静态路由：跟踪的前缀就是路由本身的前缀。 */
			p->u.prefix4 = p_orig->u.prefix4;
			p->prefixlen = p_orig->prefixlen;
		} else {
			/* 下一跳可能是 IPv4-mapped-IPv6（如 ::ffff:a.b.c.d），需还原为 IPv4。 */
			if (IS_MAPPED_IPV6(&attr->mp_nexthop_global)) {
				ipv4_mapped_ipv6_to_ipv4(&attr->mp_nexthop_global, &ipv4);
				p->u.prefix4 = ipv4;
				p->prefixlen = IPV4_MAX_BITLEN;
			} else {
				if (p_orig->family == AF_EVPN)
					p->u.prefix4 = attr->mp_nexthop_global_in;
				else
					p->u.prefix4 = attr->nexthop;
				p->prefixlen = IPV4_MAX_BITLEN;
			}
		}
		break;
	case AFI_IP6:
		p->family = AF_INET6;
		/* SRv6-L3service：跟踪的是 SID；若 SID 落在本机 locator 内（local_sid）则
		 * 另作处理，否则按 transposition（转置）后的地址作为被跟踪前缀。 */
		if (pi->attr->srv6_l3service) {
			tmp_prefix.family = AF_INET6;
			tmp_prefix.prefixlen = IPV6_MAX_BITLEN;
			tmp_prefix.prefix = pi->attr->srv6_l3service->sid;
			if (bgp_nexthop->vpn_policy[afi].tovpn_sid_locator &&
			    bgp_nexthop->vpn_policy[afi].tovpn_sid)
				local_sid = prefix_match(&bgp_nexthop->vpn_policy[afi]
								  .tovpn_sid_locator->prefix,
							 &tmp_prefix);
			else if (bgp_nexthop->tovpn_sid_locator && bgp_nexthop->tovpn_sid)
				local_sid = prefix_match(&bgp_nexthop->tovpn_sid_locator->prefix,
							 &tmp_prefix);
			else if (bgp_nexthop->srv6_unicast[afi].sid_locator &&
				 bgp_nexthop->srv6_unicast[afi].sid)
				local_sid = prefix_match(&bgp_nexthop->srv6_unicast[afi]
								  .sid_locator->prefix,
							 &tmp_prefix);
		}
		if (local_sid == false && pi->attr->srv6_l3service) {
			p->prefixlen = IPV6_MAX_BITLEN;
			if (pi->attr->srv6_l3service->transposition_len != 0 &&
			    BGP_PATH_INFO_NUM_LABELS(pi)) {
				IPV6_ADDR_COPY(&p->u.prefix6, &pi->attr->srv6_l3service->sid);
				transpose_sid(&p->u.prefix6,
					      decode_label(&pi->extra->labels->label[0]),
					      pi->attr->srv6_l3service->transposition_offset,
					      pi->attr->srv6_l3service->transposition_len,
					      BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL);
			} else
				IPV6_ADDR_COPY(&(p->u.prefix6), &(pi->attr->srv6_l3service->sid));
		} else if (is_bgp_static) {
			p->u.prefix6 = p_orig->u.prefix6;
			p->prefixlen = p_orig->prefixlen;
		} else if (attr) {
			/* 【IPv6 下一跳取哪个地址】MP_REACH 可能同时带 global 与 link-local。
			 * 取 LL、取 global 或二者取决于 mp_nexthop_len 与 route-map 偏好：
			 *   - GLOBAL_AND_LL 且 global 为 ::/LL(地址对里 global 无效) → 用 LL；
			 *   - GLOBAL_AND_LL 且正常 → 由 BGP_ATTR_NH_MP_PREFER_GLOBAL 决定；
			 *   - 只有 GLOBAL 但其实是 LL → 特殊处理（见对 LL 能力的检查）。 */
			/* If we receive MP_REACH nexthop with ::(LL)
			 * or LL(LL), use LL address as nexthop cache.
			 */
			if (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL &&
			    (IN6_IS_ADDR_UNSPECIFIED(&attr->mp_nexthop_global) ||
			     IN6_IS_ADDR_LINKLOCAL(&attr->mp_nexthop_global)))
				p->u.prefix6 = attr->mp_nexthop_local;
			/* If we receive MR_REACH with (GA)::(LL)
			 * then check for route-map to choose GA or LL
			 */
			else if (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL) {
				if (CHECK_FLAG(attr->nh_flags, BGP_ATTR_NH_MP_PREFER_GLOBAL))
					p->u.prefix6 = attr->mp_nexthop_global;
				else
					p->u.prefix6 = attr->mp_nexthop_local;
			} else if (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL &&
				   IN6_IS_ADDR_LINKLOCAL(&attr->mp_nexthop_global)) {
				/* If we receive MP_REACH with GUA as LL, we should
				 * check if we have Link-Local Next Hop capability also.
				 */
				if (!PEER_HAS_LINK_LOCAL_CAPABILITY(peer)) {
					zlog_warn("%s: received IPv6 global next-hop as Link-Local, but no capability exchanged",
						  __func__);
					p->u.prefix6 = attr->mp_nexthop_global;
				} else {
					p->u.prefix6 = attr->mp_nexthop_global;
					p->prefixlen = IPV6_MAX_BITLEN;
					return false;
				}
			} else
				p->u.prefix6 = attr->mp_nexthop_global;
			p->prefixlen = IPV6_MAX_BITLEN;
		}
		break;
	default:
		if (BGP_DEBUG(nht, NHT)) {
			zlog_debug(
				"%s: Attempting to make prefix with unknown AFI %d (not %d or %d)",
				__func__, afi, AFI_IP, AFI_IP6);
		}
		break;
	}
	return true;
}

/* ========== 10. 向 zebra 登记/注销下一跳（RNH 消息） ========== */
/* 【函数】sendmsg_zebra_rnh —— 真正把 ZEBRA_NEXTHOP_REGISTER/UNREGISTER 发给 zebra。
 * 参数：bnc 目标缓存；command 两个取值之一（登记/注销）。
 * 关键点：
 *   - 前置守卫：bgp_zclient 为空、或本实例未向 zebra 登记(IS_BGP_INST_KNOWN_TO_ZEBRA
 *     为假)时直接返回，不发送；
 *   - 登记时附加两个语义：CONNECTED 标志 → match_p（要求精确直连匹配）；
 *     STATIC_ROUTE_EXACT_MATCH → resolve_via_default（允许经默认路由解析）；
 *   - 用 zclient_send_rnh 发送；成功后按 command 置/清 BGP_NEXTHOP_REGISTERED。
 * 说明：这是“避免重复登记”的最终落点——调用方(register_zebra_rnh)已先判断未登记。 */
/**
 * sendmsg_zebra_rnh -- Format and send a nexthop register/Unregister
 *   command to Zebra.
 * ARGUMENTS:
 *   struct bgp_nexthop_cache *bnc -- the nexthop structure.
 *   int command -- command to send to zebra
 * RETURNS:
 *   void.
 */
static void sendmsg_zebra_rnh(struct bgp_nexthop_cache *bnc, int command)
{
	bool match_p = false;
	bool resolve_via_default = false;
	int ret;

	if (!bgp_zclient)
		return;

	/* Don't try to register if Zebra doesn't know of this instance. */
	if (!IS_BGP_INST_KNOWN_TO_ZEBRA(bnc->bgp)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"%s: No zebra instance to talk to, not installing NHT entry",
				__func__);
		return;
	}

	if (!bgp_zebra_num_connects()) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"%s: We have not connected yet, cannot send nexthops",
				__func__);
	}
	/* 登记时按 bnc 的两类语义给 zebra 附加要求（仅登记需要）：
	 *   CONNECTED              → 要求该下一跳必须解析为直连路由；
	 *   STATIC_ROUTE_EXACT_MATCH→ 允许通过默认路由/精确匹配来解析。 */
	if (command == ZEBRA_NEXTHOP_REGISTER) {
		if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED))
			match_p = true;
		if (CHECK_FLAG(bnc->flags, BGP_STATIC_ROUTE_EXACT_MATCH))
			resolve_via_default = true;
	}

	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("%s: sending cmd %s for %pFX (vrf %s)", __func__,
			   zserv_command_string(command), &bnc->prefix,
			   bnc->bgp->name_pretty);

	ret = zclient_send_rnh(bgp_zclient, command, &bnc->prefix, SAFI_UNICAST,
			       match_p, resolve_via_default,
			       bnc->bgp->vrf_id);
	if (ret == ZCLIENT_SEND_FAILURE) {
		flog_warn(EC_BGP_ZEBRA_SEND,
			  "sendmsg_nexthop: zclient_send_message() failed");
		return;
	}

	if (command == ZEBRA_NEXTHOP_REGISTER)
		SET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
	else if (command == ZEBRA_NEXTHOP_UNREGISTER)
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
	return;
}

/* 【函数】register_zebra_rnh —— 幂等地把一个下一跳登记给 zebra。
 * 逻辑：
 *   - 已置 REGISTERED → 直接返回（这就是“避免重复登记”的关键检查）；
 *   - 若 bnc 带 ifindex_ipv6_ll（v6 链路本地下一跳由接口事件驱动，无需 zebra 通知），
 *     仅置 REGISTERED 标志、不真正发送；
 *   - 否则发 ZEBRA_NEXTHOP_REGISTER（sendmsg_zebra_rnh 成功后会置 REGISTERED）。
 * 调用时机：bgp_find_or_add_nexthop 发现 bnc 未登记时；bgp_nht_register_nexthops 在
 *   zebra 连接恢复后补登记时。 */
/**
 * register_zebra_rnh - register a NH/route with Zebra for notification
 *    when the route or the route to the nexthop changes.
 * ARGUMENTS:
 *   struct bgp_nexthop_cache *bnc
 * RETURNS:
 *   void.
 */
static void register_zebra_rnh(struct bgp_nexthop_cache *bnc)
{
	/* Check if we have already registered */
	if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED))
		return;

	/* v6 链路本地下一跳：由接口 up/down 事件驱动，不向 zebra 登记，只标状态。 */
	if (bnc->ifindex_ipv6_ll) {
		SET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
		return;
	}

	sendmsg_zebra_rnh(bnc, ZEBRA_NEXTHOP_REGISTER);
}

/* 【函数】unregister_zebra_rnh —— 幂等地把一个下一跳从 zebra 注销。
 * 逻辑：
 *   - 未置 REGISTERED → 返回（本来就没登记）；
 *   - 带 ifindex_ipv6_ll → 只清标志（本就没向 zebra 登记）；
 *   - 否则查同前缀在 import_check_table 与 nexthop_cache_table 里是否**同时**存在：
 *     若两表都有，则不注销（因为 zebra 侧是同一条登记，另一个表还会用到），等只剩
 *     一个时再注销——这正是 sendmsg_zebra_rnh 里 match_p/resolve_via_default 语义
 *     可能不同的两个 bnc 共用一条 zebra 登记导致的去重需求。
 *   - 综上满足条件才 sendmsg_zebra_rnh(UNREGISTER)。 */
/**
 * unregister_zebra_rnh -- Unregister the route/nexthop from Zebra.
 * ARGUMENTS:
 *   struct bgp_nexthop_cache *bnc
 * RETURNS:
 *   void.
 */
static void unregister_zebra_rnh(struct bgp_nexthop_cache *bnc)
{
	struct bgp_nexthop_cache *import;
	struct bgp_nexthop_cache *nexthop;

	struct bgp *bgp = bnc->bgp;

	/* Check if we have already registered */
	if (!CHECK_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED))
		return;

	if (bnc->ifindex_ipv6_ll) {
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED);
		return;
	}

	import = bnc_find(&bgp->import_check_table[bnc->afi], &bnc->prefix, 0,
			  0);
	nexthop = bnc_find(&bgp->nexthop_cache_table[bnc->afi], &bnc->prefix, 0,
			   0);

	/* 同前缀若在两张表里都存在，则 zebra 侧仍有一条在用，先不注销。 */
	/*
	 * If this entry has both a import and a nexthop entry
	 * then let's not send the unregister quite as of yet
	 * wait until we only have 1 left
	 */
	if (import && nexthop)
		return;

	sendmsg_zebra_rnh(bnc, ZEBRA_NEXTHOP_UNREGISTER);
}

/* ========== 11. 扇出重算 evaluate_paths（NHT → 路由表的唯一通路） ========== */
/* 【函数】evaluate_paths —— 一个 bnc 状态变化后，把影响扇出到所有引用它的路径并重算。
 * 为什么是“扇出”：多条路径可共用同一个下一跳（bnc->paths 是多对多的那一侧），所以
 *   一个 bnc 变一次，要沿链表逐条重判有效性/重算最佳路径。排障“下一跳变了但路由没变”
 *   就往这里查。
 * 主流程：
 *   1) 遍历 bnc->paths 的每条路径；
 *   2) 若该路径属于“已断开且与 bnc 绑定的那个 peer”，跳过——这类路径由接口事件/peer
 *      下降路径处理，避免重复处理（见下方大段英文注释）；
 *   3) 只评估 BGP 类型且 sub_type 为 NORMAL/STATIC/IMPORTED（或 REDISTRIBUTE，排除 VNC）；
 *   4) 逐条算出 bnc_is_valid_nexthop：单播+IMPORTED+带标签且非 EVPN 网关 → L3VPN 判据；
 *      MPLS-VPN 非 IMPORTED → 恒有效；其它 → 先做 martian/自环检查，再用通用判据；
 *   5) 与旧的 BGP_PATH_VALID 比较，变化时置/清该标志，并联动 aggregate 计数、EVPN
 *      导入/取消、vpn-leak 更新/撤销；
 *   6) 变化时 hook_call(bgp_nht_path_update) 通知外部模块；
 *   7) 变化或 metric/changed 时 bgp_process() 触发该前缀重新选路；
 *   8) 末尾：若 bnc 绑定 peer，则（未通知过时）用 bgp_fsm_nht_update 通知 FSM 邻居
 *      可达性并置 PEER_NOTIFIED；
 *   9) RESET_FLAG(change_flags)，本次变化位用尽即清（供下次 zebra 更新重新计算）。
 */
/**
 * evaluate_paths - Evaluate the paths/nets associated with a nexthop.
 * ARGUMENTS:
 *   struct bgp_nexthop_cache *bnc -- the nexthop structure.
 * RETURNS:
 *   void.
 */
void evaluate_paths(struct bgp_nexthop_cache *bnc)
{
	struct bgp_dest *dest;
	struct bgp_path_info *path;
	struct bgp_path_info *bpi_ultimate;
	int afi;
	struct peer *peer = (struct peer *)bnc->nht_info;
	struct bgp_table *table;
	safi_t safi;
	struct bgp *bgp_path;
	const struct prefix *p;

	if (BGP_DEBUG(nht, NHT)) {
		char bnc_buf[BNC_FLAG_DUMP_SIZE];
		char chg_buf[BNC_FLAG_DUMP_SIZE];

		zlog_debug(
			"NH update for %pFX(%d)(%u)(%s) - flags %s chgflags %s- evaluate paths",
			&bnc->prefix, bnc->ifindex_ipv6_ll, bnc->srte_color,
			bnc->bgp->name_pretty,
			bgp_nexthop_dump_bnc_flags(bnc, bnc_buf,
						   sizeof(bnc_buf)),
			bgp_nexthop_dump_bnc_change_flags(bnc, chg_buf,
							  sizeof(bnc_buf)));
	}

	/* 沿 bnc->paths 链表遍历所有引用该下一跳的路径（本函数的“扇出”核心）。 */
	LIST_FOREACH (path, &(bnc->paths), nh_thread) {
		/*
		 * Currently when a peer goes down, bgp immediately
		 * sees this via the interface events( if it is directly
		 * connected).  And in this case it takes and puts on
		 * a special peer queue all path info's associated with
		 * but these items are not yet processed typically when
		 * the nexthop is being handled here.  Thus we end
		 * up in a situation where the process Queue for BGP
		 * is being asked to look at the same path info multiple
		 * times.  Let's just cut to the chase here and if
		 * the bnc has a peer associated with it and the path info
		 * being looked at uses that peer and the peer is no
		 * longer established we know the path_info is being
		 * handled elsewhere and we do not need to process
		 * it here at all since the pathinfo is going away
		 */
		if (peer && path->peer == peer && !peer_established(peer->connection))
			continue;

		/* 只对这几类 BGP 路径做 NHT 有效性评估；其它（如 VNC）不在此处理。 */
		if (path->type == ZEBRA_ROUTE_BGP &&
		    (path->sub_type == BGP_ROUTE_NORMAL ||
		     path->sub_type == BGP_ROUTE_STATIC ||
		     path->sub_type == BGP_ROUTE_IMPORTED))
			/* evaluate the path */
			;
		else if (path->sub_type == BGP_ROUTE_REDISTRIBUTE) {
			/* evaluate the path for redistributed routes
			 * except those from VNC
			 */
			if ((path->type == ZEBRA_ROUTE_VNC) ||
			    (path->type == ZEBRA_ROUTE_VNC_DIRECT))
				continue;
		} else
			/* don't evaluate the path */
			continue;

		dest = path->net;
		assert(dest && bgp_dest_table(dest));
		p = bgp_dest_get_prefix(dest);
		afi = family2afi(p->family);
		table = bgp_dest_table(dest);
		safi = table->safi;

		/*
		 * handle routes from other VRFs (they can have a
		 * nexthop in THIS VRF). bgp_path is the bgp instance
		 * that owns the route referencing this nexthop.
		 */
		bgp_path = table->bgp;

		/*
		 * Path becomes valid/invalid depending on whether the nexthop
		 * reachable/unreachable.
		 *
		 * In case of unicast routes that were imported from vpn
		 * and that have labels, they are valid only if there are
		 * nexthops with labels
		 *
		 * If the nexthop is EVPN gateway-IP,
		 * do not check for a valid label.
		 */

		/* 按路径类型决定用哪套“下一跳是否有效”的判据（决定这条路径有效/无效）。 */
		bool bnc_is_valid_nexthop = false;
		bool old_path_valid = false;
		struct bgp_route_evpn *bre =
			bgp_attr_get_evpn_overlay(path->attr);

		if (safi == SAFI_UNICAST &&
		    path->sub_type == BGP_ROUTE_IMPORTED &&
		    BGP_PATH_INFO_NUM_LABELS(path) &&
		    !(bre && bre->type == OVERLAY_INDEX_GATEWAY_IP)) {
			bnc_is_valid_nexthop =
				bgp_isvalid_nexthop_for_l3vpn(bnc, path)
					? true
					: false;
		} else if (safi == SAFI_MPLS_VPN &&
			   path->sub_type != BGP_ROUTE_IMPORTED) {
			/* avoid not redistributing mpls vpn routes */
			bnc_is_valid_nexthop = true;
		} else {
			/* mpls-vpn routes with BGP_ROUTE_IMPORTED subtype */
			if (bgp_update_martian_nexthop(
				    bnc->bgp, afi, safi, path->type,
				    path->sub_type, path->attr, dest)) {
				if (BGP_DEBUG(nht, NHT))
					zlog_debug(
						"%s: prefix %pBD (vrf %s), ignoring path due to martian or self-next-hop",
						__func__, dest, bgp_path->name);
			} else
				bnc_is_valid_nexthop =
					bgp_isvalid_nexthop(bnc) ? true : false;
		}

		if (BGP_DEBUG(nht, NHT)) {

			if (dest->pdest) {
				char rd_buf[RD_ADDRSTRLEN];

				prefix_rd2str(
					(struct prefix_rd *)bgp_dest_get_prefix(
						dest->pdest),
					rd_buf, sizeof(rd_buf),
					bgp_get_asnotation(bnc->bgp));
				zlog_debug(
					"... eval path %d/%d %pBD RD %s %s flags 0x%x",
					afi, safi, dest, rd_buf,
					bgp_path->name_pretty, path->flags);
			} else
				zlog_debug(
					"... eval path %d/%d %pBD %s flags 0x%x",
					afi, safi, dest, bgp_path->name_pretty,
					path->flags);
		}

		/* Skip paths marked for removal or as history. */
		if (CHECK_FLAG(path->flags, BGP_PATH_REMOVED)
		    || CHECK_FLAG(path->flags, BGP_PATH_HISTORY))
			continue;

		/* Copy the metric to the path. Will be used for bestpath
		 * computation */
		bpi_ultimate = bgp_get_imported_bpi_ultimate(path);
		if (bgp_isvalid_nexthop(bnc) && bnc->metric)
			(bgp_path_info_extra_get(bpi_ultimate))->igpmetric =
				bnc->metric;
		else if (bpi_ultimate->extra)
			bpi_ultimate->extra->igpmetric = 0;

		if (CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_METRIC_CHANGED) ||
		    CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED) ||
		    bgp_attr_get_color(path->attr))
			SET_FLAG(path->flags, BGP_PATH_IGP_CHANGED);

		/* 记录评估前该路径的有效性，用于判断本次是否发生了“有效↔无效”翻转。 */
		old_path_valid = CHECK_FLAG(path->flags, BGP_PATH_VALID);
		if (path->type == ZEBRA_ROUTE_BGP &&
		    path->sub_type == BGP_ROUTE_STATIC &&
		    !CHECK_FLAG(bgp_path->flags, BGP_FLAG_IMPORT_CHECK))
			/* static routes with 'no bgp network import-check' are
			 * always valid. if nht is called with static routes,
			 * the vpn exportation needs to be triggered
			 */
			vpn_leak_from_vrf_update(bgp_get_default(), bgp_path,
						 path);
		else if (path->sub_type == BGP_ROUTE_REDISTRIBUTE &&
			 safi == SAFI_UNICAST &&
			 (bgp_path->inst_type == BGP_INSTANCE_TYPE_VRF ||
			  bgp_path->inst_type == BGP_INSTANCE_TYPE_DEFAULT))
			/* redistribute routes are always valid
			 * if nht is called with redistribute routes, the vpn
			 * exportation needs to be triggered
			 */
			vpn_leak_from_vrf_update(bgp_get_default(), bgp_path,
						 path);
		/* 有效性发生翻转：分别处理“变无效”与“变有效”两条路（含 EVPN 导入/取消、
		 * vpn-leak 更新/撤销、aggregate 计数增减），保证路由表与外部状态一致。 */
		else if (old_path_valid != bnc_is_valid_nexthop) {
			if (old_path_valid) {
				/* No longer valid, clear flag; also for EVPN
				 * routes, unimport from VRFs if needed.
				 */
				bgp_aggregate_decrement(bgp_path, p, path, afi,
							safi);
				bgp_path_info_unset_flag(dest, path,
							 BGP_PATH_VALID);
				if (safi == SAFI_EVPN &&
				    bgp_evpn_is_prefix_nht_supported(bgp_dest_get_prefix(dest)))
					bgp_evpn_unimport_route(bgp_path,
						afi, safi, bgp_dest_get_prefix(dest), path);
				if (safi == SAFI_UNICAST &&
				    (bgp_path->inst_type !=
				     BGP_INSTANCE_TYPE_VIEW))
					vpn_leak_from_vrf_withdraw(
						bgp_get_default(), bgp_path,
						path);
			} else {
				/* Path becomes valid, set flag; also for EVPN
				 * routes, import from VRFs if needed.
				 */
				bgp_path_info_set_flag(dest, path,
						       BGP_PATH_VALID);
				bgp_aggregate_increment(bgp_path, p, path, afi,
							safi);
				if (safi == SAFI_EVPN &&
				    bgp_evpn_is_prefix_nht_supported(bgp_dest_get_prefix(dest)))
					bgp_evpn_import_route(bgp_path,
						afi, safi, bgp_dest_get_prefix(dest), path);
				if (safi == SAFI_UNICAST &&
				    (bgp_path->inst_type !=
				     BGP_INSTANCE_TYPE_VIEW))
					vpn_leak_from_vrf_update(
						bgp_get_default(), bgp_path,
						path);
			}
		}

		/* 只有“有效性翻转”才触发 hook（供 BMP/EVPN 等外部模块挂钩）。 */
		if (old_path_valid != bnc_is_valid_nexthop)
			hook_call(bgp_nht_path_update, bgp_path, path, bnc_is_valid_nexthop);

		if (old_path_valid != bnc_is_valid_nexthop ||
		    CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_METRIC_CHANGED) ||
		    CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED))
			bgp_process(bgp_path, dest, path, afi, safi);
	}

	/* 若该 bnc 是“邻居连通性跟踪”（绑定了 peer），把可达性结果通知 FSM。
	 * 黑洞下一跳不算可达；且未通知过(PEER_NOTIFIED 未置)时才通知，避免重复。 */
	if (peer) {
		int valid_nexthops = bgp_isvalid_nexthop(bnc);

		if (valid_nexthops) {
			/*
			 * Peering cannot occur across a blackhole nexthop
			 */
			if (bnc->nexthop_num == 1 && bnc->nexthop &&
			    bnc->nexthop->type == NEXTHOP_TYPE_BLACKHOLE)
				valid_nexthops = 0;
		}

		if (!CHECK_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED)) {
			if (BGP_DEBUG(nht, NHT))
				zlog_debug(
					"%s: Updating peer (%s(%s)) status with NHT nexthops %d",
					__func__, peer->host,
					peer->bgp->name_pretty,
					!!valid_nexthops);
			bgp_fsm_nht_update(peer->connection, peer,
					   !!valid_nexthops);
			SET_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED);
		}
	}

	/* 本次扇出完毕，清空“本次变化位”，等下一次 zebra 更新重算。 */
	RESET_FLAG(bnc->change_flags);
}

/* ========== 12. 路径↔下一跳映射 path_nh_map（多对多的枢纽） ========== */
/* 【函数】path_nh_map —— 建立或拆除 “路径 ↔ 下一跳缓存(bnc)” 的映射。
 * 参数：path 目标路径；bnc 目标 bnc（make=false 时可为 NULL）；make 真=建立，假=拆除。
 * 逻辑：
 *   - 无论 make 真假，**先摘掉 path 上已有的旧关联**（LIST_REMOVE + 旧 bnc 的
 *     path_count--），保证一条 path 同一时刻只挂一个 bnc；
 *   - make 为真时再 LIST_INSERT_HEAD 到新 bnc 的 paths 并 path_count++。
 * 成对使用：建立由 bgp_find_or_add_nexthop 调用；拆除由 bgp_unlink_nexthop /
 *   bgp_nexthop_cache_reset 调用（make=false）。
 * 生命周期约束：bnc 只有在其 path_count 归零且无 nht_info 时才能释放，见
 *   bgp_unlink_nexthop_check。这两个计数器（paths 链表 + path_count）必须始终一致。 */
/**
 * path_nh_map - make or break path-to-nexthop association.
 * ARGUMENTS:
 *   path - pointer to the path structure
 *   bnc - pointer to the nexthop structure
 *   make - if set, make the association. if unset, just break the existing
 *          association.
 */
void path_nh_map(struct bgp_path_info *path, struct bgp_nexthop_cache *bnc,
		 bool make)
{
	/* 先拆除 path 上可能存在的旧映射（幂等：建立新映射前总先清旧的）。 */
	if (path->nexthop) {
		LIST_REMOVE(path, nh_thread);
		path->nexthop->path_count--;
		path->nexthop = NULL;
	}
	/* make 为真：把 path 挂到新 bnc 的 paths 链表头部并计数。 */
	if (make) {
		LIST_INSERT_HEAD(&(bnc->paths), path, nh_thread);
		path->nexthop = bnc;
		path->nexthop->path_count++;
	}
}

/* ========== 13. zebra 连接恢复后的下一跳补登记 ========== */
/* 【函数】bgp_nht_register_nexthops —— zebra 连接建立后，把此前积压的下一跳重新登记。
 * 为什么需要：bgpd 可能先于 zebra 就绪，或 zebra 重启导致登记全丢。此时连接一恢复就
 *   要补登记，否则 BGP 会一直认为下一跳无状态（路由不生效）。
 * 实现：遍历该实例每个 AFI 的 nexthop_cache_table，对每条 bnc 调 register_zebra_rnh
 *   （其内部幂等，已登记/ v6 LL 的会被跳过）。 */
/*
 * This function is called to register nexthops to zebra
 * as that we may have tried to install the nexthops
 * before we actually have a zebra connection
 */
void bgp_nht_register_nexthops(struct bgp *bgp)
{
	for (afi_t afi = AFI_IP; afi < AFI_MAX; afi++) {
		struct bgp_nexthop_cache *bnc;

		frr_each (bgp_nexthop_cache, &bgp->nexthop_cache_table[afi],
			  bnc) {
			register_zebra_rnh(bnc);
		}
	}
}

/* ========== 14. ENHE(RFC5549) 接口 RA 的登记/注销 ========== */
/* 【函数】bgp_nht_reg_enhe_cap_intfs —— 为已建立的 ENHE 邻居打开相关接口的 RA。
 * 场景：PEER_FLAG_CAPABILITY_ENHE 可能在 peer **已经 up 之后**才被置上（动态能力
 *   协商），此时需要回头把相关接口按 RA 重新登记给 zebra，否则对端发的“IPv6 下一跳 +
 *   IPv4 前缀”组合解析不了。
 * 前置：仅对“非接口邻居”（peer->ifp 为空，即非直连接口型）且未关闭自动 RA 时处理；
 *   前缀必须是 IPv6；要求 bnc->nht_info == peer（确认是本邻居的 bnc）。
 * 动作：遍历该 bnc 的下一跳所在接口，逐个发 zclient_send_interface_radv_req(开启 RA)。 */
void bgp_nht_reg_enhe_cap_intfs(struct peer *peer)
{
	struct bgp *bgp;
	struct bgp_nexthop_cache *bnc;
	struct nexthop *nhop;
	struct interface *ifp;
	struct prefix p;
	ifindex_t ifindex = 0;

	if (peer->ifp)
		return;

	bgp = peer->bgp;

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_IPV6_NO_AUTO_RA))
		return;

	if (!sockunion2hostprefix(&peer->connection->su, &p)) {
		zlog_warn("%s: Unable to convert sockunion to prefix for %s",
			  __func__, peer->host);
		return;
	}

	if (p.family != AF_INET6)
		return;
	/*
	 * Gather the ifindex for if up/down events to be
	 * tagged into this fun
	 */
	if (peer->conf_if &&
	    IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr))
		ifindex = peer->connection->su.sin6.sin6_scope_id;

	bnc = bnc_find(&bgp->nexthop_cache_table[AFI_IP6], &p, 0, ifindex);
	if (!bnc)
		return;

	if (peer != bnc->nht_info)
		return;

	for (nhop = bnc->nexthop; nhop; nhop = nhop->next) {
		ifp = if_lookup_by_index(nhop->ifindex, nhop->vrf_id);

		if (!ifp)
			continue;

		zclient_send_interface_radv_req(bgp_zclient,
						nhop->vrf_id,
						ifp, true,
						BGP_UNNUM_DEFAULT_RA_INTERVAL);
	}
}

/* 【函数】bgp_nht_dereg_enhe_cap_intfs —— bgp_nht_reg_enhe_cap_intfs 的逆操作。
 * 当 ENHE 能力被撤下时，把相关接口的 RA 关掉（zclient_send_interface_radv_req 传
 *   false/0）。筛选条件与 reg 版完全一致（非接口邻居、v6、bnc 属于本 peer）。 */
void bgp_nht_dereg_enhe_cap_intfs(struct peer *peer)
{
	struct bgp *bgp;
	struct bgp_nexthop_cache *bnc;
	struct nexthop *nhop;
	struct interface *ifp;
	struct prefix p;
	ifindex_t ifindex = 0;

	if (peer->ifp)
		return;

	bgp = peer->bgp;

	if (!sockunion2hostprefix(&peer->connection->su, &p)) {
		zlog_warn("%s: Unable to convert sockunion to prefix for %s",
			  __func__, peer->host);
		return;
	}

	if (p.family != AF_INET6)
		return;
	/*
	 * Gather the ifindex for if up/down events to be
	 * tagged into this fun
	 */
	if (peer->conf_if &&
	    IN6_IS_ADDR_LINKLOCAL(&peer->connection->su.sin6.sin6_addr))
		ifindex = peer->connection->su.sin6.sin6_scope_id;

	bnc = bnc_find(&bgp->nexthop_cache_table[AFI_IP6], &p, 0, ifindex);
	if (!bnc)
		return;

	if (peer != bnc->nht_info)
		return;

	for (nhop = bnc->nexthop; nhop; nhop = nhop->next) {
		ifp = if_lookup_by_index(nhop->ifindex, nhop->vrf_id);

		if (!ifp)
			continue;

		zclient_send_interface_radv_req(bgp_zclient, nhop->vrf_id, ifp, 0,
						0);
	}
}
