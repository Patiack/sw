// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRv6 definitions
 * Copyright (C) 2025 6WIND
 * Loïc SANG <loic.sang@6wind.com>
 */

/* ============================================================================
 * 【文件导读】bgp_srv6.c —— 本端 IPv6 单播前缀的 SRv6 SID 分配与通告
 * ============================================================================
 * 【本文件解决什么问题】SRv6-BGP（RFC 9252）里，一台 PE 可以给自己的 IPv6 单播
 * 前缀分配 Prefix-SID，随 BGP 通告发给邻居，邻居据此用 SRv6 转发。本文件负责
 * “本端 SID 从哪来、什么时候装/卸、怎么随路由通告”这一侧（与 bgp_attr_srv6.c 的
 * “收到对端 SID 怎么存”正好相对）。
 *
 * 【SID 的三个来源】由 is_srv6_unicast_enabled() 的三判据体现：
 *   1) auto    ：BGP_CONFIG_SRV6_UNICAST_SID_AUTO，由 BGP 从 locator 自动切分；
 *   2) index   ：只给索引 sid_index，用 srv6_sid_compose 从 locator 推导完整 SID；
 *   3) explicit：用户直接给定完整 SID(sid_explicit)。
 * ★ ensure_afi_sid 里会检查“三种模式不能同时选”，冲突则报错返回。
 *
 * 【需要理解的两层“安装”】
 *   - 数据面：SID 要在内核注册为 local SID endpoint(END.DT4/END.DT6)，由
 *     bgp_srv6_unicast_sid_endpoint 通过 zclient_send_localsid 完成；
 *   - 通告面：SID 作为 Prefix-SID 属性随路由发给邻居，由 register_route 把 SID
 *     挂到 bgp_dest->srv6_unicast 上，announce 再触发重发。
 *
 * 【与 route-map 的交互】bgp_srv6_unicast_register_route 在给前缀挂 SID 前，会先
 * 应用 srv6_unicast[afi].rmap_name 指定的 route-map；被 deny 的前缀不挂 SID。
 * 这实现了“只对匹配的前缀分配/通告 SID”的精细控制。
 *
 * 【关键数据结构】bgp->srv6_unicast[afi]（struct bgp_srv6_unicast）保存本 AFI 的
 * SID、locator、模式标志、route-map 名、以及“上次发给 zebra 的 SID”
 * (zebra_sid_last_sent，用于精确撤销)。
 *
 * 【阅读建议】ensure_afi_sid（SID 就绪）-> sid_endpoint（数据面装/卸）->
 *            register/unregister_route（逐前缀挂 SID，核心）->
 *            announce/withdraw（面向邻居）-> sid_update/withdraw/delete（生命周期）。
 * ============================================================================
 */

#include <zebra.h>

#include "log.h"
#include "zclient.h"

#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_srv6.h"
#include "bgpd/bgpd.h"

extern struct zclient *bgp_zclient;

/* 【函数】bgp_srv6_unicast_ensure_afi_sid —— 确保该 AFI 的本端 SID 已就绪。
 * 早退条件（依次）：未配置 SRv6(is_srv6_unicast_enabled 为假)、已分配过 SID、
 * 未设置 locator。随后检查“index / auto / explicit”三种模式是否冲突（两两不可同时
 * 成立），冲突则报错返回。
 * SID 的确定：
 *   - 既非 auto 也非 explicit（即 index 模式）：用 srv6_sid_compose 从 locator+index
 *     推导；失败则报错返回；
 *   - explicit：直接取用户给定的 SID；
 *   - auto（且非 explicit）：此处**不**计算 SID，交由 SRv6 管理器分配。
 * 最后填 ctx(vrf_id + 行为 END.DT4/DT6) 调 bgp_zebra_request_srv6_sid 向 SRv6 管理器
 * 申请 SID。行为按 AFI 选：IPv4->END.DT4，IPv6->END.DT6。 */
void bgp_srv6_unicast_ensure_afi_sid(struct bgp *bgp, afi_t afi)
{
	uint32_t sid_func;
	safi_t safi = SAFI_UNICAST;
	struct srv6_sid_ctx ctx = {};
	bool unicast_sid_auto = false;
	uint32_t unicast_sid_index = 0;
	struct in6_addr unicast_sid = {};
	struct srv6_locator *locator_bgp;
	bool unicast_sid_explicit = false;

	/* no configured */
	if (!is_srv6_unicast_enabled(bgp, afi))
		return;

	/* already allocated */
	if (bgp->srv6_unicast[afi].sid)
		return;

	locator_bgp = bgp->srv6_locator;
	/* locator no set */
	if (!locator_bgp)
		return;

	unicast_sid_index = bgp->srv6_unicast[afi].sid_index;
	unicast_sid_auto = CHECK_FLAG(bgp->af_flags[afi][safi],
				      BGP_CONFIG_SRV6_UNICAST_SID_AUTO);
	unicast_sid_explicit = bgp->srv6_unicast[afi].sid_explicit;

	if ((unicast_sid_index != 0 && unicast_sid_auto) ||
	    (unicast_sid_index != 0 && unicast_sid_explicit) ||
	    (unicast_sid_auto && unicast_sid_explicit)) {
		zlog_err("%s: more than one mode selected among index-mode, auto-mode and explicit-mode. ignored.",
			 __func__);
		return;
	}

	if (!unicast_sid_auto && !unicast_sid_explicit) {
		if (!srv6_sid_compose(&unicast_sid, locator_bgp, unicast_sid_index)) {
			zlog_err("%s: failed to compose unicast sid %s: afi %s",
				 __func__, bgp->name_pretty, afi2str(afi));
			return;
		}
	} else if (unicast_sid_explicit) {
		unicast_sid = *(bgp->srv6_unicast[afi].sid_explicit);
	} else if (!unicast_sid_auto) {
		zlog_err("%s: neither index, auto, nor explicit mode is selected.",  __func__);
		return;
	}

	ctx.vrf_id = bgp->vrf_id;
	ctx.behavior = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
				     : ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
	if (!bgp_zebra_request_srv6_sid(&ctx, &unicast_sid, locator_bgp->name, &sid_func)) {
		zlog_err("%s: failed to request sid for bgp %s: afi %s", __func__,
			 bgp->name_pretty, afi2str(afi));
	}
}

/* 【函数】bgp_srv6_unicast_sid_endpoint —— 在接口 ifp 上安装(install=true)或卸载
 * (false)本 AFI 的 SID endpoint（数据面 / local SID）。
 * 先按 locator 的各段长度(block/node/function/argument bits)填 ctx。
 * install 时：若 locator 带 SRV6_LOCATOR_USID 标志，设 NEXT_CSID 的 FLV 操作；用
 *   zclient_send_localsid(ADD) 下发 END.DT4/DT6；并把本次下发的 SID 记录到
 *   zebra_sid_last_sent（先释放旧记录），以便将来精确删除。
 * uninstall 时：用之前记录的 zebra_sid_last_sent 下 DEL，再释放该记录并置空。
 * 行为同样按 AFI 选 END.DT4/DT6。 */
void bgp_srv6_unicast_sid_endpoint(struct bgp *bgp, afi_t afi,
				   struct interface *ifp, bool install)
{
	enum seg6local_action_t act;
	struct seg6local_context ctx = {};
	struct in6_addr *unicast_sid_ls = NULL;

	if (!bgp->srv6_unicast[afi].sid)
		return;

	ctx.block_len = bgp->srv6_unicast[afi].sid_locator->block_bits_length;
	ctx.node_len = bgp->srv6_unicast[afi].sid_locator->node_bits_length;
	ctx.function_len = bgp->srv6_unicast[afi].sid_locator->function_bits_length;
	ctx.argument_len = bgp->srv6_unicast[afi].sid_locator->argument_bits_length;

	if (install) {
		if (CHECK_FLAG(bgp->srv6_unicast[afi].sid_locator->flags, SRV6_LOCATOR_USID))
			SET_SRV6_FLV_OP(ctx.flv.flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_NEXT_CSID);
		ctx.table = ifp->vrf->data.l.table_id;
		act = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4 :
			ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
		zclient_send_localsid(bgp_zclient, ZEBRA_ROUTE_ADD, bgp->srv6_unicast[afi].sid,
				      IPV6_MAX_BITLEN, ifp->ifindex, act, &ctx);
		unicast_sid_ls = XCALLOC(MTYPE_BGP_SRV6_SID, sizeof(struct in6_addr));
		*unicast_sid_ls = *bgp->srv6_unicast[afi].sid;
		if (bgp->srv6_unicast[afi].zebra_sid_last_sent)
			XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].zebra_sid_last_sent);
		bgp->srv6_unicast[afi].zebra_sid_last_sent = unicast_sid_ls;

	} else {
		zclient_send_localsid(bgp_zclient, ZEBRA_ROUTE_DELETE,
				      bgp->srv6_unicast[afi].zebra_sid_last_sent, IPV6_MAX_BITLEN,
				      ifp->ifindex, ZEBRA_SEG6_LOCAL_ACTION_UNSPEC, &ctx);
		XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].zebra_sid_last_sent);
		bgp->srv6_unicast[afi].zebra_sid_last_sent = NULL;
	}
}

/* 【函数】bgp_srv6_unicast_sid_withdraw —— 撤销该 AFI 的 SID：先卸掉接口上的
 * endpoint，再向 SRv6 管理器释放 SID（bgp_zebra_release_srv6_sid）。
 * 仅对默认 VRF（vrf_id == VRF_DEFAULT）操作；找不到默认 SRv6 接口时告警并返回。
 * 注意：只做 withdraw（卸 endpoint + 释放 SID），不释放本端结构体（那是 delete 的事）。 */
void bgp_srv6_unicast_sid_withdraw(struct bgp *bgp, afi_t afi)
{
	struct interface *ifp;
	struct srv6_sid_ctx ctx = {};
	int debug = BGP_DEBUG(zebra, ZEBRA);

	if (bgp->vrf_id != VRF_DEFAULT)
		return;

	if (debug)
		zlog_debug("%s: vrf %s: deleting sid %pI6 for vrf id %d", __func__,
			   bgp->name_pretty, bgp->srv6_unicast[afi].sid, bgp->vrf_id);

	ifp = if_lookup_by_name(DEFAULT_SRV6_IFNAME, VRF_DEFAULT);
	if (!ifp) {
		zlog_warn("%s interface not found, nothing to uninstall",
			  DEFAULT_SRV6_IFNAME);
		return;
	}

	if (bgp->srv6_unicast[afi].zebra_sid_last_sent)
		bgp_srv6_unicast_sid_endpoint(bgp, afi, ifp, false);

	ctx.behavior = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
				     : ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
	ctx.vrf_id = bgp->vrf_id;
	bgp_zebra_release_srv6_sid(&ctx, bgp->srv6_unicast[afi].sid_locator->name);
}

/* 【函数】bgp_srv6_unicast_delete —— 删除该 AFI 的全部 SRv6 SID 状态（实例清理或
 * 功能关闭时用）：卸 endpoint、向管理器释放 SID、sid_unregister、释放本端 sid 与
 * sid_explicit、释放 rmap_name（并递减 route-map 引用计数）、释放 locator、清除 AUTO 标志。
 * 比 withdraw 更彻底（连结构体都释放）。仅对默认 VRF 且已启用 SRv6 时执行。 */
void bgp_srv6_unicast_delete(struct bgp *bgp, afi_t afi)
{
	struct interface *ifp;
	struct srv6_sid_ctx ctx = {};

	if (!bgp || bgp->vrf_id != VRF_DEFAULT)
		return;

	if (!is_srv6_unicast_enabled(bgp, afi))
		return;

	if (bgp->srv6_unicast[afi].sid) {
		ifp = if_lookup_by_name(DEFAULT_SRV6_IFNAME, VRF_DEFAULT);
		if (ifp && bgp->srv6_unicast[afi].zebra_sid_last_sent)
			bgp_srv6_unicast_sid_endpoint(bgp, afi, ifp, false);

		ctx.vrf_id = bgp->vrf_id;
		ctx.behavior = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
					     : ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
		bgp_zebra_release_srv6_sid(&ctx, bgp->srv6_unicast[afi].sid_locator->name);

		sid_unregister(bgp, bgp->srv6_unicast[afi].sid);
		XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].sid);
	}

	if (bgp->srv6_unicast[afi].sid_explicit)
		XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].sid_explicit);

	if (bgp->srv6_unicast[afi].rmap_name) {
		XFREE(MTYPE_ROUTE_MAP_NAME, bgp->srv6_unicast[afi].rmap_name);
		route_map_counter_decrement(
			route_map_lookup_by_name(bgp->srv6_unicast[afi].rmap_name));
	}

	srv6_locator_free(bgp->srv6_unicast[afi].sid_locator);
	bgp->srv6_unicast[afi].sid_locator = NULL;
	UNSET_FLAG(bgp->af_flags[afi][SAFI_UNICAST],
		   BGP_CONFIG_SRV6_UNICAST_SID_AUTO);
}

/* 【函数】bgp_srv6_unicast_sid_update —— SID 内容变化后重新在接口上安装 endpoint。
 * 早退：无 SID、无默认 SRv6 接口、接口未 up 则返回。核心就是调 sid_endpoint(install=true)。 */
void bgp_srv6_unicast_sid_update(struct bgp *bgp, afi_t afi)
{
	struct interface *ifp;

	if (!bgp->srv6_unicast[afi].sid)
		return;

	ifp = if_lookup_by_name(DEFAULT_SRV6_IFNAME, VRF_DEFAULT);
	if (!ifp) {
		zlog_warn("%s interface not found, can not install SRV6 endpoint behavior",
			  DEFAULT_SRV6_IFNAME);
		return;
	}
	if (!if_is_up(ifp))
		return;

	bgp_srv6_unicast_sid_endpoint(bgp, afi, ifp, true);
}

/* 【函数】bgp_srv6_unicast_unregister_route —— 归还某前缀的 SID：释放 dest 上的
 * srv6_unicast（struct bgp_attr_srv6_l3service）并置空。与 register_route 成对。 */
void bgp_srv6_unicast_unregister_route(struct bgp_dest *dest)
{
	XFREE(MTYPE_BGP_SRV6_L3SERVICE, dest->srv6_unicast);
	dest->srv6_unicast = NULL;
}

/* 【函数】bgp_srv6_unicast_register_route —— 给某条被选中的前缀挂上本端 SID
 * （写成 Prefix-SID 相关的 l3service 信息放在 dest->srv6_unicast，供通告编码用）。
 * 这是“配了 SRv6 何时真正生效”的关键时点。逻辑：
 *   - bpi 为空（该前缀无路径）：若已挂过则 unregister，返回；
 *   - 若该路径本身已带 srv6_l3service，或本 AFI 未设 locator：直接返回；
 *   - 若配置了 route-map：把候选路径信息喂进 route_map_apply，DENY 则 unregister 并
 *     返回（即 route-map 决定“哪些前缀配 SID”）；匹配则递增引用计数；
 *   - 若 dest 已挂且 SID 与当前相同，则无需改动；
 *   - 否则分配 srv6_unicast，填 endpoint_behavior（按 AFI 与 locator 是否 USID 选
 *     END.DT4/DT6 及 _USID 变体）、locator 各段长度、以及 SID 本身。
 * 疑点：函数里先给 info.attr / info.peer 赋值，随即又 memset(&info, 0, sizeof(info))
 *   清零，等于把刚设的 attr / peer 抹掉后传给 route_map_apply。依据是代码顺序本身；
 *   是否影响 route-map 匹配结果请以实际运行行为为准，此处仅标注、不改代码。 */
void bgp_srv6_unicast_register_route(struct bgp *bgp, afi_t afi, struct bgp_dest *dest,
				     struct bgp_path_info *bpi)
{
	struct attr attr_tmp;
	const struct prefix *p;
	struct route_map *rmap;
	route_map_result_t ret;
	struct bgp_path_info info;
	struct srv6_locator *locator;

	if (!bpi) {
		if (dest->srv6_unicast)
			bgp_srv6_unicast_unregister_route(dest);

		return;
	}

	if (bpi->attr->srv6_l3service)
		return;

	if (!bgp->srv6_unicast[afi].sid_locator)
		return;

	if (bgp->srv6_unicast[afi].rmap_name) {
		rmap = route_map_lookup_by_name(bgp->srv6_unicast[afi].rmap_name);
		if (rmap) {
			attr_tmp = *bpi->attr;
			info.attr = &attr_tmp;
			info.peer = bgp->peer_self;
			memset(&info, 0, sizeof(info));
			p = bgp_dest_get_prefix(bpi->net);

			ret = route_map_apply(rmap, p, &info);

			if (ret == RMAP_DENYMATCH) {
				if (dest->srv6_unicast)
					bgp_srv6_unicast_unregister_route(dest);

				if (BGP_DEBUG(update, UPDATE_OUT))
					zlog_debug("srv6 unicast prefix %pBD denied", dest);

				return;
			}

			route_map_counter_increment(rmap);
		} else {
			zlog_warn("route-map %s was no found, ignored",
				  bgp->srv6_unicast[afi].rmap_name);
		}
	}

	if (dest->srv6_unicast && sid_same(bgp->srv6_unicast[afi].sid, &dest->srv6_unicast->sid))
		return;

	locator = bgp->srv6_unicast[afi].sid_locator;
	dest->srv6_unicast = XCALLOC(MTYPE_BGP_SRV6_L3SERVICE,
				     sizeof(struct bgp_attr_srv6_l3service));
	dest->srv6_unicast->sid_flags = 0x00;
	dest->srv6_unicast->endpoint_behavior =
			afi == AFI_IP ? (CHECK_FLAG(locator->flags, SRV6_LOCATOR_USID)
						 ? SRV6_ENDPOINT_BEHAVIOR_END_DT4_USID
						 : SRV6_ENDPOINT_BEHAVIOR_END_DT4)
				      : (CHECK_FLAG(locator->flags, SRV6_LOCATOR_USID)
						 ? SRV6_ENDPOINT_BEHAVIOR_END_DT6_USID
						 : SRV6_ENDPOINT_BEHAVIOR_END_DT6);
	dest->srv6_unicast->loc_block_len = locator->block_bits_length;
	dest->srv6_unicast->loc_node_len = locator->node_bits_length;
	dest->srv6_unicast->func_len = locator->function_bits_length;
	dest->srv6_unicast->arg_len = locator->argument_bits_length;
	memcpy(&dest->srv6_unicast->sid, bgp->srv6_unicast[afi].sid,
	       sizeof(struct in6_addr));
}

/* 【函数】bgp_srv6_unicast_announce —— 遍历该 AFI 的 RIB，对每个前缀里被选中的路径
 * 调用 register_route 挂 SID（每个前缀只处理第一条被选中的路径，遇到即 break）；
 * 然后对启用了 SRv6 封装能力(PEER_FLAG_CONFIG_ENCAPSULATION_SRV6(_RELAX))的邻居
 * 强制重发全部路由(bgp_announce_route ... true)，使新 SID 随通告出去。
 * 早退：本 AFI 未设 locator。 */
void bgp_srv6_unicast_announce(struct bgp *bgp, afi_t afi)
{
	struct peer *peer;
	struct bgp_dest *pdest;
	struct bgp_path_info *bpi;
	safi_t safi = SAFI_UNICAST;
	struct listnode *node, *nnode;

	if (!bgp->srv6_unicast[afi].sid_locator)
		return;

	for (pdest = bgp_table_top(bgp->rib[afi][safi]); pdest; pdest = bgp_route_next(pdest)) {
		for (bpi = bgp_dest_get_bgp_path_info(pdest); bpi; bpi = bpi->next) {
			if (!CHECK_FLAG(bpi->flags, BGP_PATH_SELECTED))
				continue;

			if (bpi->attr->srv6_l3service)
				continue;

			bgp_srv6_unicast_register_route(bgp, afi, pdest, bpi);
			break;
		}
	}

	/* force to resend all routes */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		if (peergroup_af_flag_check(peer, afi, safi,
					    PEER_FLAG_CONFIG_ENCAPSULATION_SRV6_RELAX) ||
		    peergroup_af_flag_check(peer, afi, safi, PEER_FLAG_CONFIG_ENCAPSULATION_SRV6))
			bgp_announce_route(peer, afi, safi, true);
	}
}

/* 【函数】bgp_srv6_unicast_withdraw —— 遍历该 AFI 的 RIB，对已挂 srv6_unicast 的前缀
 * 逐个 unregister（清掉 SID），再对支持 SRv6 封装的邻居强制重发，使撤销生效。
 * 与 announce 相对。 */
void bgp_srv6_unicast_withdraw(struct bgp *bgp, afi_t afi)
{
	struct peer *peer;
	struct bgp_dest *pdest;
	safi_t safi = SAFI_UNICAST;
	struct listnode *node, *nnode;

	for (pdest = bgp_table_top(bgp->rib[afi][safi]); pdest; pdest = bgp_route_next(pdest)) {
		if (!pdest->srv6_unicast)
			continue;

		bgp_srv6_unicast_unregister_route(pdest);
	}

	/* force to resend all routes */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		if (peergroup_af_flag_check(peer, afi, safi,
					    PEER_FLAG_CONFIG_ENCAPSULATION_SRV6_RELAX) ||
		    peergroup_af_flag_check(peer, afi, safi, PEER_FLAG_CONFIG_ENCAPSULATION_SRV6))
			bgp_announce_route(peer, afi, safi, true);
	}
}
