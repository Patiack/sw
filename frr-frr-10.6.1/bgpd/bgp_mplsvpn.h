// SPDX-License-Identifier: GPL-2.0-or-later
/* MPLS-VPN
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 *
 * This file is part of GxNU Zebra.
 */
/* ============================================================================
 * 【文件导读】bgp_mplsvpn.h —— MPLS-VPN 子系统的对外接口与一组内联“状态判定”辅助
 * ============================================================================
 * 【职责与定位】声明 bgp_mplsvpn.c 对外暴露的入口（NLRI 解析、VRF<->VPN 泄漏、SID
 *   分配、show/init、nexthop-label bind），并集中定义若干被多个 .c 复用的内联判定：
 *   这些判定回答“某个 VRF/bgp 实例当前是否处于可导出/可导入 VPN 的状态”。
 *
 * 【核心概念】RD/RT、SAFI_MPLS_VPN、per-vrf / per-nexthop 标签、SRv6 End.DT4/6/46、
 *   vrfleak 关系，详见 bgp_mplsvpn.c 的文件头导读。
 *
 * 【本文件的使用者】bgp_route.c/bgp_vty.c/bgp_zebra.c 等常调用这里的 inline 判定
 *   来决定是否触发导出/导入；struct bgp_mplsvpn_nh_label_bind_cache 是 l3vpn 多域
 *   转接(改下一跳后换标签)的缓存，仅在 bgp_mplsvpn.c 内使用。
 *
 * 【易错点】以下 vpn_policy 的 direction 常量 BGP_VPN_POLICY_DIR_TOVPN/FROMVPN 决定
 *   “这是导出(VRF->VPN)还是导入(VPN->VRF)方向”，读内联函数时务必先分清方向。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_MPLSVPN_H
#define _QUAGGA_BGP_MPLSVPN_H

#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_rd.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_label.h"

/* 【宏】MPLS_LABEL_IS_SPECIAL —— 标签值 <= MPLS_LABEL_EXTENSION(特殊预留区间)则为特殊标签。
 * 【宏】MPLS_LABEL_IS_NULL —— 是否为“空标签”三兄弟之一：IPv4/IPv6 显式空标签、隐式空标签。
 *   这些标签不是真实转发标签，处理时通常需要特判（如 implicit-null 触发 PHP）。 */
#define MPLS_LABEL_IS_SPECIAL(label) ((label) <= MPLS_LABEL_EXTENSION)
#define MPLS_LABEL_IS_NULL(label)                                              \
	((label) == MPLS_LABEL_IPV4_EXPLICIT_NULL                              \
	 || (label) == MPLS_LABEL_IPV6_EXPLICIT_NULL                           \
	 || (label) == MPLS_LABEL_IMPLICIT_NULL)

/* 【宏】BGP_VPNVX_HELP_STR —— 命令帮助串，展开为两个 BGP_AF_STR，用于 <vpnv4|vpnv6> 的 helper。 */
#define BGP_VPNVX_HELP_STR BGP_AF_STR BGP_AF_STR

/* 【宏】以下是 show 命令的表头字符串：
 *   V4_HEADER        : 常规路由表头；
 *   V4_HEADER_TAG    : 带标签(In tag/Out tag)列；
 *   V4_HEADER_OVERLAY: EVPN overlay 相关列(EthTag/Overlay Index/RouterMac)。 */
#define V4_HEADER                                                              \
	"   Network          Next Hop            Metric LocPrf Weight Path\n"
#define V4_HEADER_TAG "   Network          Next Hop      In tag/Out tag\n"
#define V4_HEADER_OVERLAY                                                      \
	"   Network          Next Hop      EthTag    Overlay Index   RouterMac\n"

/* 【宏】SRv6 FUNCTION 位宽上限：FOR_LABEL=20(可塞进 20bit MPLS 标签的最高位)，
 * FOR_BGP=32(以 BGP 属性直接承载 FUNC 的上限)。见 srv6_sid_compose 的使用。 */
#define BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL 20
#define BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP	  32

/* 【接口：初始化/解析/展示】
 *   bgp_mplsvpn_init      : 注册 network/show 命令。
 *   bgp_mplsvpn_path_nh_label_unlink : 解绑路径的 per-nexthop 标签缓存。
 *   bgp_nlri_parse_vpn    : 解析 VPNv4/v6 的 NLRI(收方向入口)。
 *   argv_find_and_parse_vpnvx : 解析 vpnv4/vpnv6 关键字。
 *   bgp_show_mpls_vpn     : show bgp vpn 的公共实现。 */
extern void bgp_mplsvpn_init(void);
extern void bgp_mplsvpn_path_nh_label_unlink(struct bgp_path_info *pi);
extern int bgp_nlri_parse_vpn(struct peer *peer, struct attr *attr, struct bgp_nlri *packet);

extern int argv_find_and_parse_vpnvx(struct cmd_token **argv, int argc,
				     int *index, afi_t *afi);
extern int bgp_show_mpls_vpn(struct vty *vty, afi_t afi, struct prefix_rd *prd,
			     enum bgp_show_type type, void *output_arg,
			     int tags, bool use_json);

/* 【接口：VRF<->VPN 路由泄漏】
 *   from_vrf_update/withdraw/withdraw_all/update_all : VRF->VPN(导出方向)。
 *   to_vrf_update/withdraw/withdraw_all             : VPN->VRF(导入方向)。
 *   vpn_leak_to_vrf_no_retain_filter_check : 判断某属性是否已无 VRF 需要。
 *   vpn_leak_no_retain : 清理无人需要的 VPN 残留路由。 */
extern void vpn_leak_from_vrf_update(struct bgp *to_bgp, struct bgp *from_bgp,
				     struct bgp_path_info *path_vrf);

extern void vpn_leak_from_vrf_withdraw(struct bgp *to_bgp, struct bgp *from_bgp,
				       struct bgp_path_info *path_vrf);

extern void vpn_leak_from_vrf_withdraw_all(struct bgp *to_bgp,
					   struct bgp *from_bgp, afi_t afi);

extern void vpn_leak_from_vrf_update_all(struct bgp *to_bgp,
					 struct bgp *from_bgp, afi_t afi);

extern void vpn_leak_to_vrf_withdraw_all(struct bgp *to_bgp, afi_t afi);

extern void vpn_leak_no_retain(struct bgp *to_bgp, struct bgp *vpn_from,
			       afi_t afi);

extern void vpn_leak_to_vrf_update_all(struct bgp *to_bgp, struct bgp *from_bgp,
				       afi_t afi);

extern bool vpn_leak_to_vrf_no_retain_filter_check(struct bgp *from_bgp,
						   struct attr *attr,
						   afi_t afi);

extern void vpn_leak_to_vrf_update(struct bgp *from_bgp, struct bgp_path_info *path_vpn,
				   struct prefix_rd *prd, struct peer *peer);

extern void vpn_leak_to_vrf_withdraw(struct bgp_path_info *path_vpn);

/* 【接口：向 zebra 下发/撤销标签与 SRv6 SID，以及 SID 的申请/释放与组合】
 *   vpn_leak_zebra_vrf_label_update/withdraw : 下发/撤销 per-vrf 出口标签。
 *   vpn_leak_zebra_vrf_sid_update / _withdraw  : 下发/撤销 SRv6 SID(per-af / per-vrf)。
 *   vpn_leak_label_callback : 标签池异步分配回调。
 *   ensure/delete_vrf_tovpn_sid[_per_af/_per_vrf] : 申请/释放出向 SID。
 *   transpose_sid / srv6_sid_compose : SID 组合与位搬运。
 *   sid_register/sid_unregister : SID 占用登记(防冲突)。 */
extern void vpn_leak_zebra_vrf_label_update(struct bgp *bgp, afi_t afi);
extern void vpn_leak_zebra_vrf_label_withdraw(struct bgp *bgp, afi_t afi);
extern void vpn_leak_zebra_vrf_sid_update(struct bgp *bgp, afi_t afi);
extern void vpn_leak_zebra_vrf_sid_update_per_af(struct bgp *bgp, afi_t afi);
extern void vpn_leak_zebra_vrf_sid_update_per_vrf(struct bgp *bgp);
extern void vpn_leak_zebra_vrf_sid_withdraw(struct bgp *bgp, afi_t afi);
extern void vpn_leak_zebra_vrf_sid_withdraw_per_af(struct bgp *bgp, afi_t afi);
extern void vpn_leak_zebra_vrf_sid_withdraw_per_vrf(struct bgp *bgp);
extern int vpn_leak_label_callback(mpls_label_t label, void *lblid, bool alloc);
extern void ensure_vrf_tovpn_sid(struct bgp *vpn, struct bgp *vrf, afi_t afi);
extern void delete_vrf_tovpn_sid(struct bgp *vpn, struct bgp *vrf, afi_t afi);
extern void delete_vrf_tovpn_sid_per_af(struct bgp *vpn, struct bgp *vrf,
					afi_t afi);
extern void delete_vrf_tovpn_sid_per_vrf(struct bgp *vpn, struct bgp *vrf);
extern void ensure_vrf_tovpn_sid_per_af(struct bgp *vpn, struct bgp *vrf,
					afi_t afi);
extern void ensure_vrf_tovpn_sid_per_vrf(struct bgp *vpn, struct bgp *vrf);
extern void transpose_sid(struct in6_addr *sid, uint32_t label, uint8_t offset, uint8_t size,
			  uint8_t size_max);
/* 【接口：VRF 间导入关系(import vrf)的建立/拆除】 */
extern void vrf_import_from_vrf(struct bgp *to_bgp, struct bgp *from_bgp, const char *import_name,
				afi_t afi, safi_t safi);
void vrf_unimport_from_vrf(struct bgp *to_bgp, struct bgp *from_bgp, const char *import_name,
			   afi_t afi, safi_t safi);
bool srv6_sid_compose(struct in6_addr *sid_value, struct srv6_locator *locator, uint32_t sid_func);

/* 【函数】is_bgp_vrf_mplsvpn —— 判断该 VRF 实例是否启用了任一“MPLSVPN 泄漏”方向：
 * 只要某个 AFI 的单播上设置了 VRF->MPLSVPN_EXPORT 或 MPLSVPN->VRF_IMPORT 即为真。
 * 注意：只对 BGP_INSTANCE_TYPE_VRF 的实例返回 true。 */
static inline bool is_bgp_vrf_mplsvpn(struct bgp *bgp)
{
	afi_t afi;

	if (bgp->inst_type == BGP_INSTANCE_TYPE_VRF)
		for (afi = 0; afi < AFI_MAX; ++afi) {
			if (CHECK_FLAG(bgp->af_flags[afi][SAFI_UNICAST],
				       BGP_CONFIG_VRF_TO_MPLSVPN_EXPORT)
			    || CHECK_FLAG(bgp->af_flags[afi][SAFI_UNICAST],
					  BGP_CONFIG_MPLSVPN_TO_VRF_IMPORT))
				return true;
		}
	return false;
}

/* 【函数】vpn_leak_to_vpn_active —— 判定“该实例当前能否把路由导出/泄漏到 VPN 表”，
 * 并(通过 pmsg)回传失败原因。逐项检查：实例类型、是否配置了任一种 export、导出 RT 列表/
 * 导出 route-map(除非 ignore_export_rt_list)、是否设了 RD、手动标签是否已分配、SID 是否就绪。
 * 返回 1 表示可用，0 表示不可用(附原因)。参数 ignore_export_rt_list=true 时跳过 RT 列表检查
 * (用于撤回阶段强制撤)。 */
static inline int vpn_leak_to_vpn_active(struct bgp *bgp_vrf, afi_t afi,
					 const char **pmsg,
					 bool ignore_export_rt_list)
{
	if (bgp_vrf->inst_type != BGP_INSTANCE_TYPE_VRF
		&& bgp_vrf->inst_type != BGP_INSTANCE_TYPE_DEFAULT) {

		if (pmsg)
			*pmsg = "source bgp instance neither vrf nor default";
		return 0;
	}

	/* Is vrf configured to export to vpn? */
	if (!CHECK_FLAG(bgp_vrf->af_flags[afi][SAFI_UNICAST],
			BGP_CONFIG_VRF_TO_MPLSVPN_EXPORT)
	    && !CHECK_FLAG(bgp_vrf->af_flags[afi][SAFI_UNICAST],
			   BGP_CONFIG_VRF_TO_VRF_EXPORT)) {
		if (pmsg)
			*pmsg = "export not set";
		return 0;
	}

	/* Before performing withdrawal, VPN activation is checked; however,
	 * when the route-map modifies the export route-target (RT) list, it
	 * becomes challenging to determine if VPN prefixes were previously
	 * present, or not. The 'ignore_export_rt_list' parameter will be
	 * used to force the withdraw operation by not checking the possible
	 * route-map changes.
	 * Of the 'ignore_export_rt_list' is set to false, check the following:
	 * - Is there an RT list set?
	 * - Is there a route-map that sets RT communities
	 */
	if (!ignore_export_rt_list &&
	    !bgp_vrf->vpn_policy[afi].rtlist[BGP_VPN_POLICY_DIR_TOVPN] &&
	    (!bgp_vrf->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_TOVPN] ||
	     !bgp_route_map_has_extcommunity_rt(
		     bgp_vrf->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_TOVPN]))) {
		if (pmsg)
			*pmsg = "rtlist tovpn not defined";
		return 0;
	}

	/* Is there an RD set? */
	if (!CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags,
			BGP_VPN_POLICY_TOVPN_RD_SET)) {
		if (pmsg)
			*pmsg = "rd not defined";
		return 0;
	}

	/* Is a route-map specified, but not defined? */
	if (bgp_vrf->vpn_policy[afi].rmap_name[BGP_VPN_POLICY_DIR_TOVPN] &&
		!bgp_vrf->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_TOVPN]) {
		if (pmsg)
			*pmsg = "route-map tovpn named but not defined";
		return 0;
	}

	/* Is there a "manual" export label that isn't allocated yet? */
	if (!CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags,
			BGP_VPN_POLICY_TOVPN_LABEL_AUTO) &&
	    bgp_vrf->vpn_policy[afi].tovpn_label != BGP_PREVENT_VRF_2_VRF_LEAK &&
	    bgp_vrf->vpn_policy[afi].tovpn_label != MPLS_LABEL_NONE &&
	    (bgp_vrf->vpn_policy[afi].tovpn_label >= MPLS_LABEL_UNRESERVED_MIN &&
	     !CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags,
			 BGP_VPN_POLICY_TOVPN_LABEL_MANUAL_REG))) {
		if (!bgp_zebra_request_label_range(bgp_vrf->vpn_policy[afi]
							   .tovpn_label,
						   1, false)) {
			if (pmsg)
				*pmsg = "manual label could not be allocated";
			return 0;
		}
		SET_FLAG(bgp_vrf->vpn_policy[afi].flags,
			 BGP_VPN_POLICY_TOVPN_LABEL_MANUAL_REG);
	}

	/* Is there an export SID that isn't allocated yet? */
	if ((CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_AUTO) ||
	     CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_EXPLICIT) ||
	     bgp_vrf->vpn_policy[afi].tovpn_sid_index) &&
	    !bgp_vrf->vpn_policy[afi].tovpn_sid) {
		if (pmsg)
			*pmsg = "SID could not be allocated";
		return 0;
	}
	return 1;
}

/* 【函数】vpn_leak_from_vpn_active —— 判定“该实例当前能否从 VPN 表导入路由到自身”，并回传原因。
 * 检查：实例类型、vrf_id 已知、是否配置了 import(MPLSVPN->VRF 或 VRF->VRF)、导入 RT 列表、
 * 导入 route-map 是否已定义。返回 1/0。 */
static inline int vpn_leak_from_vpn_active(struct bgp *bgp_vrf, afi_t afi,
					   const char **pmsg)
{
	if (bgp_vrf->inst_type != BGP_INSTANCE_TYPE_VRF
		&& bgp_vrf->inst_type != BGP_INSTANCE_TYPE_DEFAULT) {

		if (pmsg)
			*pmsg = "destination bgp instance neither vrf nor default";
		return 0;
	}

	if (bgp_vrf->vrf_id == VRF_UNKNOWN) {
		if (pmsg)
			*pmsg = "destination bgp instance vrf is VRF_UNKNOWN";
		return 0;
	}

	/* Is vrf configured to import from vpn? */
	if (!CHECK_FLAG(bgp_vrf->af_flags[afi][SAFI_UNICAST],
			BGP_CONFIG_MPLSVPN_TO_VRF_IMPORT)
	    && !CHECK_FLAG(bgp_vrf->af_flags[afi][SAFI_UNICAST],
			   BGP_CONFIG_VRF_TO_VRF_IMPORT)) {
		if (pmsg)
			*pmsg = "import not set";
		return 0;
	}

	/* Is there an RT list set? */
	if (!bgp_vrf->vpn_policy[afi].rtlist[BGP_VPN_POLICY_DIR_FROMVPN]) {
		if (pmsg)
			*pmsg = "rtlist fromvpn not defined";
		return 0;
	}

	/* Is a route-map specified, but not defined? */
	if (bgp_vrf->vpn_policy[afi].rmap_name[BGP_VPN_POLICY_DIR_FROMVPN] &&
		!bgp_vrf->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_FROMVPN]) {
		if (pmsg)
			*pmsg = "route-map fromvpn named but not defined";
		return 0;
	}
	return 1;
}

/* 【函数】vpn_leak_prechange —— 配置变更“前”的收尾：按方向先把旧的效果撤掉
 * (fromvpn 方向撤本 VRF 已导入的；tovpn 方向撤本 VRF 已导出的)。bgp_vpn(默认实例)为空则跳过。
 *
 * 【函数】vpn_leak_postchange —— 配置变更“后”的应用：fromvpn 方向触发软重配并重新导入
 * 全部 VPN 路由到本 VRF；tovpn 方向同步 zebra 标签/SID 并重新导出本 VRF 全部路由到 VPN。
 * 两者成对使用（先 prechange 再改配置再 postchange），保证变更原子性。 */
static inline void vpn_leak_prechange(enum vpn_policy_direction direction,
				      afi_t afi, struct bgp *bgp_vpn,
				      struct bgp *bgp_vrf)
{
	/* Detect when default bgp instance is not (yet) defined by config */
	if (!bgp_vpn)
		return;

	if ((direction == BGP_VPN_POLICY_DIR_FROMVPN) &&
		vpn_leak_from_vpn_active(bgp_vrf, afi, NULL)) {

		vpn_leak_to_vrf_withdraw_all(bgp_vrf, afi);
	}
	if ((direction == BGP_VPN_POLICY_DIR_TOVPN) &&
	    vpn_leak_to_vpn_active(bgp_vrf, afi, NULL, true)) {
		vpn_leak_from_vrf_withdraw_all(bgp_vpn, bgp_vrf, afi);
	}
}

static inline void vpn_leak_postchange(enum vpn_policy_direction direction,
				       afi_t afi, struct bgp *bgp_vpn,
				       struct bgp *bgp_vrf)
{
	/* Detect when default bgp instance is not (yet) defined by config */
	if (!bgp_vpn)
		return;

	if (direction == BGP_VPN_POLICY_DIR_FROMVPN) {
		/* trigger a flush to re-sync with ADJ-RIB-in */
		if (!CHECK_FLAG(bgp_vpn->af_flags[afi][SAFI_MPLS_VPN],
				BGP_VPNVX_RETAIN_ROUTE_TARGET_ALL))
			bgp_clear_soft_in(bgp_vpn, afi, SAFI_MPLS_VPN);
		vpn_leak_to_vrf_update_all(bgp_vrf, bgp_vpn, afi);
	}
	if (direction == BGP_VPN_POLICY_DIR_TOVPN) {

		if (bgp_vrf->vpn_policy[afi].tovpn_label !=
			bgp_vrf->vpn_policy[afi]
			       .tovpn_zebra_vrf_label_last_sent) {
			vpn_leak_zebra_vrf_label_update(bgp_vrf, afi);
		}

		if (bgp_vrf->vpn_policy[afi].tovpn_sid_index == 0 &&
		    !CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_AUTO) &&
		    !CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_EXPLICIT) &&
		    bgp_vrf->tovpn_sid_index == 0 &&
		    !CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_AUTO) &&
		    !CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT))
			delete_vrf_tovpn_sid(bgp_vpn, bgp_vrf, afi);

		if (CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT) ||
		    (!bgp_vrf->vpn_policy[afi].tovpn_sid && !bgp_vrf->tovpn_sid) ||
		    CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_EXPLICIT))
			ensure_vrf_tovpn_sid(bgp_vpn, bgp_vrf, afi);

		if ((!bgp_vrf->vpn_policy[afi].tovpn_sid &&
		     bgp_vrf->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent) ||
		    (!bgp_vrf->tovpn_sid &&
		     bgp_vrf->tovpn_zebra_vrf_sid_last_sent))
			vpn_leak_zebra_vrf_sid_withdraw(bgp_vrf, afi);

		if (bgp_vrf->vpn_policy[afi].tovpn_sid) {
			if (sid_diff(bgp_vrf->vpn_policy[afi].tovpn_sid,
				     bgp_vrf->vpn_policy[afi]
					     .tovpn_zebra_vrf_sid_last_sent)) {
				vpn_leak_zebra_vrf_sid_update(bgp_vrf, afi);
			}
		} else if (bgp_vrf->tovpn_sid) {
			if (sid_diff(bgp_vrf->tovpn_sid,
				     bgp_vrf->tovpn_zebra_vrf_sid_last_sent)) {
				vpn_leak_zebra_vrf_sid_update(bgp_vrf, afi);
			}
		}

		vpn_leak_from_vrf_update_all(bgp_vpn, bgp_vrf, afi);
	}
}

/* 【函数】is_route_injectable_into_vpn(中文补充) —— 判断一条路径是否可注入 VPN：
 * 若它是“从 VPN 导入的(BGP_ROUTE_IMPORTED 且其 parent 处于 VPN 表)”则不可再注入(避免回环)，
 * 其余(含非导入、或从非 VPN 处导入)都可注入。返回 true=可注入。 */
/* Flag if the route is injectable into VPN. This would be either a
 * non-imported route or a non-VPN imported route.
 */
static inline bool is_route_injectable_into_vpn(struct bgp_path_info *pi)
{
	struct bgp_path_info *parent_pi;
	struct bgp_table *table;
	struct bgp_dest *dest;

	if (pi->sub_type != BGP_ROUTE_IMPORTED || !pi->extra ||
	    !pi->extra->vrfleak || !pi->extra->vrfleak->parent)
		return true;

	parent_pi = (struct bgp_path_info *)pi->extra->vrfleak->parent;
	dest = parent_pi->net;
	if (!dest)
		return true;
	table = bgp_dest_table(dest);
	if (table &&
	    (table->afi == AFI_IP || table->afi == AFI_IP6) &&
	    table->safi == SAFI_MPLS_VPN)
		return false;
	return true;
}

/* 【函数】is_pi_family_vpn(中文补充) —— 该路径是否属于 VPN 族(AFI_IP/AFI_IP6 + SAFI_MPLS_VPN)。 */
/* Flag if the route path's family is VPN. */
static inline bool is_pi_family_vpn(struct bgp_path_info *pi)
{
	return (is_pi_family_matching(pi, AFI_IP, SAFI_MPLS_VPN) ||
		is_pi_family_matching(pi, AFI_IP6, SAFI_MPLS_VPN));
}

/* 【函数】is_pi_srv6_valid —— SRv6 VPN 场景下判断某路径的 SID 分配是否有效(否则 RIB 视为无效)。
 * 无 srv6_l3service/srv6_vpn 属性时，取决于实例是否 srv6_only；已是单播导入的路径直接有效；
 * 否则要求相应 tovpn_sid 已分配(per-af 或 per-vrf)，缺则无效。 */
/*
 * If you are using SRv6 VPN instead of MPLS, it need to check
 * the SID allocation. If the sid is not allocated, the rib
 * will be invalid.
 * If the SID per VRF is not available, also consider the rib as
 * invalid.
 */
static inline bool is_pi_srv6_valid(struct bgp_path_info *pi, struct bgp *bgp_nexthop, afi_t afi,
				    safi_t safi)
{
	if (!pi->attr->srv6_l3service && !pi->attr->srv6_vpn)
		return !bgp_nexthop->srv6_only;

	/* imported paths from VPN: srv6 enabled and nht reachability
	 * are enough to know if that path is valid
	 */
	if (safi == SAFI_UNICAST)
		return true;

	if (bgp_nexthop->vpn_policy[afi].tovpn_sid == NULL && bgp_nexthop->tovpn_sid == NULL)
		return false;

	if (bgp_nexthop->tovpn_sid_index == 0 &&
	    !CHECK_FLAG(bgp_nexthop->vrf_flags, BGP_VRF_TOVPN_SID_AUTO) &&
	    !CHECK_FLAG(bgp_nexthop->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT) &&
	    bgp_nexthop->vpn_policy[afi].tovpn_sid_index == 0 &&
	    !CHECK_FLAG(bgp_nexthop->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_AUTO) &&
	    !CHECK_FLAG(bgp_nexthop->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_EXPLICIT))
		return false;

	return true;
}

/* 【函数】is_srv6_vpn_afi_enabled —— 该 afi 下是否启用 SRv6 VPN(SID auto/explicit 或设了 index)。
 * 【函数】is_srv6_vpn_vrf_enabled —— 整个 VRF 是否启用 SRv6 VPN(VRF 级 auto/explicit/index)。
 * 【函数】is_srv6_vpn_enabled —— 综合判定(VRF 级或任一 AFI 级启用)。 */
static inline bool is_srv6_vpn_afi_enabled(struct bgp *bgp, afi_t afi)
{
	if (CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_AUTO) ||
	    CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_EXPLICIT) ||
	    bgp->vpn_policy[afi].tovpn_sid_index)
		return true;

	return false;
}

static inline bool is_srv6_vpn_vrf_enabled(struct bgp *bgp)
{
	if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_AUTO) ||
	    CHECK_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT) || bgp->tovpn_sid_index)
		return true;

	return false;
}

static inline bool is_srv6_vpn_enabled(struct bgp *bgp)
{
	if (is_srv6_vpn_vrf_enabled(bgp))
		return true;

	if (is_srv6_vpn_afi_enabled(bgp, AFI_IP) || is_srv6_vpn_afi_enabled(bgp, AFI_IP6))
		return true;

	return false;
}

/* 【接口：事件与收尾】
 *   vpn_policy_routemap_event : route-map 定义/删除/变更事件入口。
 *   get_first_vrf_for_redirect_with_rt : 按 RT 找应重定向到的 VRF。
 *   vpn_leak_postchange_all : 默认实例建立后统一补做泄漏。
 *   vpn_handle_router_id_update : router-id 变化时刷新 auto RD/RT 并重通告。
 *   bgp_vpn_leak_unimport/export : VRF 实例创建/删除时的泄漏收尾/建立。
 *   bgp_vpn_release_label : 释放 per-vrf 自动标签。 */
extern void vpn_policy_routemap_event(const char *rmap_name);

extern vrf_id_t get_first_vrf_for_redirect_with_rt(struct ecommunity *eckey);

extern void vpn_leak_postchange_all(void);
extern void vpn_handle_router_id_update(struct bgp *bgp, bool withdraw,
					bool is_config);
extern void bgp_vpn_leak_unimport(struct bgp *from_bgp);
extern void bgp_vpn_leak_export(struct bgp *from_bgp);
extern void bgp_vpn_release_label(struct bgp *bgp, afi_t afi, bool reset);

/* 【接口：l3vpn nexthop-label bind(多域转接)】
 *   用于“改写了下一跳(next-hop-self/eBGP)但仍需按原标签转发”的场景：把 (下一跳, orig_label)
 *   绑定一个本地新标签(异步申请)，并下发内核 MPLS 交换动作。 */
extern bool bgp_mplsvpn_path_uses_valid_mpls_label(struct bgp_path_info *pi);
extern int
bgp_mplsvpn_nh_label_bind_cmp(const struct bgp_mplsvpn_nh_label_bind_cache *a,
			      const struct bgp_mplsvpn_nh_label_bind_cache *b);
extern void bgp_mplsvpn_path_nh_label_bind_unlink(struct bgp_path_info *pi);
extern void bgp_mplsvpn_nh_label_bind_register_local_label(
	struct bgp *bgp, struct bgp_dest *dest, struct bgp_path_info *pi);
mpls_label_t bgp_mplsvpn_nh_label_bind_get_label(struct bgp_path_info *pi);

/* used to bind a local label to the (label, nexthop) values
 * from an incoming BGP mplsvpn update
 */
/* ===========================================================================
 * struct bgp_mplsvpn_nh_label_bind_cache —— l3vpn 多域转接的 nexthop-label 绑定缓存
 * ---------------------------------------------------------------------------
 * 键 = (下一跳 prefix, 从 BGP Update 收到的 orig_label)；一棵 RB 树按此唯一化。
 * 用途：当被测路径因 next-hop-self/ebgp 改写了下发下一跳，但内部仍要按 orig_label
 * 转发时，为其分配一个本地 new_label，并让内核做 new_label -> (orig_label, 新下一跳)
 * 的标签交换，对外通告 new_label。
 *
 * 逐字段：
 *   entry                : RB 树挂点(item 类型来自 DECLARE_RBTREE_UNIQ)。
 *   nexthop/orig_label   : 树键。nexthop 为前缀形式；orig_label 为收到的原标签。
 *   nh                   : 解析到的下一跳(提供 ifindex/vrf_id 用于下发)。
 *   path_count           : 引用本缓存的路径数；归零则可释放。
 *   bgp_vpn              : 反指的 bgp 实例(通常是 VPN/默认实例)。
 *   new_label            : 已分配的本地新标签；MPLS_INVALID_LABEL 表示尚未到位。
 *   paths                : 使用本缓存的所有 bgp_path_info 链表。
 *   last_update          : 最近更新时间(monotime)。
 *   allocation_in_progress: 标签申请是否在途；在途时 free 需让回调去真正释放。
 * ===========================================================================
 */
struct bgp_mplsvpn_nh_label_bind_cache {

	/* RB-tree entry. */
	struct bgp_mplsvpn_nh_label_bind_cache_item entry;

	/* The nexthop and the vpn label are the key of the list.
	 * Only received BGP MPLSVPN updates may use that structure.
	 * orig_label is the original label received from the BGP Update.
	 */
	struct prefix nexthop;
	mpls_label_t orig_label;

	/* resolved interface for the paths */
	struct nexthop *nh;

	/* number of mplsvpn path */
	unsigned int path_count;

	/* back pointer to bgp instance */
	struct bgp *bgp_vpn;

	/* MPLS label allocated value.
	 * When the next-hop is changed because of 'next-hop-self' or
	 * because it is an eBGP peer, the redistributed orig_label value
	 * is unmodified, unless the 'l3vpn-multi-domain-switching'
	 * is enabled: a new_label value is allocated:
	 * - The new_label value is sent in the advertised BGP update,
	 * instead of the label value.
	 * - An MPLS entry is set to swap <new_label> with <orig_label>.
	 */
	mpls_label_t new_label;

	/* list of path_vrfs using it */
	LIST_HEAD(mplsvpn_nh_label_bind_path_lists, bgp_path_info) paths;

	time_t last_update;

	bool allocation_in_progress;
};

/* 【RB 树声明】按 bgp_mplsvpn_nh_label_bind_cmp 唯一化上表结构。 */
DECLARE_RBTREE_UNIQ(bgp_mplsvpn_nh_label_bind_cache,
		    struct bgp_mplsvpn_nh_label_bind_cache, entry,
		    bgp_mplsvpn_nh_label_bind_cmp);

/* 【接口】bgp_mplsvpn_nh_label_bind_free/new/find : 绑定缓存的释放/新建/查找。
 *   bgp_mplsvpn_nexthop_init : 注册相关 show 命令。
 *   sid_register/sid_unregister : SRv6 SID 占用登记/注销。 */
void bgp_mplsvpn_nh_label_bind_free(
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc);

struct bgp_mplsvpn_nh_label_bind_cache *
bgp_mplsvpn_nh_label_bind_new(struct bgp_mplsvpn_nh_label_bind_cache_head *tree,
			      struct prefix *p, mpls_label_t orig_label);
struct bgp_mplsvpn_nh_label_bind_cache *bgp_mplsvpn_nh_label_bind_find(
	struct bgp_mplsvpn_nh_label_bind_cache_head *tree, struct prefix *p,
	mpls_label_t orig_label);
void bgp_mplsvpn_nexthop_init(void);
extern void sid_register(struct bgp *bgp, const struct in6_addr *sid,
			 const char *locator_name);
extern void sid_unregister(struct bgp *bgp, const struct in6_addr *sid);

#endif /* _QUAGGA_BGP_MPLSVPN_H */
