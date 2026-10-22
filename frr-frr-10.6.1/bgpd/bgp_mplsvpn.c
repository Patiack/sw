// SPDX-License-Identifier: GPL-2.0-or-later
/* MPLS-VPN
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */
/* ============================================================================
 * 【文件导读】bgp_mplsvpn.c —— BGP 的 MPLS VPN / BGP-LU 数据面：VPN NLRI 解析、
 *              VRF<->VPN 双向路由泄漏(leak)、SRv6/MPLS 标签与 SID 处理
 * ============================================================================
 * 【职责与定位】
 *   本文件处理 RFC4364 的 VPNv4/VPNv6(SAFI_MPLS_VPN)，是 bgp_mplsvpn.h 的实现，
 *   并与 bgp_label.h（标签编解码/共享）、bgp_rd.h（RD）配套。它承担三块职责：
 *     1) 收方向：bgp_nlri_parse_vpn() 把对端 UPDATE 里 "长度|标签|RD|前缀" 的
 *        NLRI 解成 (前缀, RD, 标签) 并交给 bgp_update/bgp_withdraw。
 *     2) 本地 VRF 与 VPN(全局)表之间的路由泄漏：vpn_leak_from_vrf_* 负责把 VRF
 *        单播路由导出到 VPN 表(加 RD/RT/标签)；vpn_leak_to_vrf_* 负责把 VPN 表
 *        路由按 RT 命中导入各 VRF。这是 "import vrf / export vpn" 的核心。
 *     3) 与 zebra 交互的 MPLS/SRv6 数据面：为 VRF 申请出标签(per-vrf 或
 *        per-nexthop)、SRv6 End.DT4/6/46 的 SID 分配与下发、以及 l3vpn 多域
 *        转接(nexthop-label bind)的新标签分配。
 *
 * 【核心数据结构 / 关键概念】
 *   - RD(Route Distinguisher, 8 字节)：VPN 表按 RD 再分一层子表，使不同 VPN 的
 *     相同前缀互不冲突。VPN 前缀的 table key = (前缀, RD)。
 *   - RT(Route Target)：扩展团体。导出时写入、导入时比较交集(ecommunity_include)
 *     决定该 VPN 路由能否进入某 VRF。
 *   - 标签：per-vrf(每个 VRF 一个出口标签) 或 per-nexthop(每个下一跳一个标签，用于
 *     l3vpn 多域转接)。标签走 interned 共享，见 bgp_label.h。
 *   - vrfleak 记录：挂在 bgp_path_info->extra->vrfleak，用 parent 指针把 "VPN 表中
 *     的导入项" 与 "源 VRF 中的原始项" 关联，是判断是否需要撤回/更新的依据。
 *   - SRv6：旁路 MPLS，用 SID (LOC:FUNC) 代替标签；本文件有 ensure/delete_vrf_tovpn_sid
 *     及 srv6_sid_compose/transpose_sid 等，移植自 RFC9252 transposition 方案。
 *
 * 【本文件在主流程中的位置】
 *   谁调它：bgp_route.c/bgp_packet.c 在收到 VPN UPDATE 时调 bgp_nlri_parse_vpn；
 *           bgp_zebra.c / vty 配置变更时调 vpn_leak_*；zebra 回标签时调 *_cb 回调。
 *   它调谁：bgp_update/bgp_withdraw（路由注入）、bgp_attr_intern（属性共享）、
 *           bgp_afi_node_get/bgp_safi_node_lookup（表查询/插入）、
 *           bgp_lp_get/bgp_vpn_lp_release（标签池）、
 *           bgp_zebra_send_nexthop_label/zclient_send_vrf_label/zclient_send_localsid（下发）。
 *
 * 【易错点 / 阅读建议】
 *   - 注意 leak 的 "parent" 关系与引用计数（bgp_path_info_lock / bgp_dest_lock_node）；
 *     漏 unlock 会泄漏，误锁会 dangling。
 *   - per-nexthop 标签的异步特性：申请是异步的，回调未返回前 label 为
 *     MPLS_INVALID_LABEL，此时不导出，等回调再次触发。
 *   - RD/RT 会比较字节序；RD 用 memcmp 直接比 8 字节 val。
 *   建议阅读顺序：argv_find_and_parse_vpnvx → bgp_nlri_parse_vpn
 *     → leak_update / leak_update_nexthop_valid（核心）→ vpn_leak_from_vrf_update
 *     → vpn_leak_to_vrf_update_onevrf → 末尾的 nexthop-label-bind 与 show/init。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "prefix.h"
#include "log.h"
#include "memory.h"
#include "stream.h"
#include "queue.h"
#include "filter.h"
#include "mpls.h"
#include "json.h"
#include "zclient.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_label.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_vpn.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_nexthop.h"
#include "bgpd/bgp_nht.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_memory.h"
#include "bgpd/bgp_aspath.h"

#ifdef ENABLE_BGP_VNC
#include "bgpd/rfapi/rfapi_backend.h"
#endif

DEFINE_MTYPE_STATIC(BGPD, MPLSVPN_NH_LABEL_BIND_CACHE,
		    "BGP MPLSVPN nexthop label bind cache");

/*
 * Definitions and external declarations.
 */
/* 【外部声明】bgp_zclient 是 bgpd 与 zebra 通信的客户端句柄（在 bgp_zebra.c 中
 * 定义）。本文件所有 zclient_send_vrf_label / zclient_send_localsid 都经由它发出。 */
extern struct zclient *bgp_zclient;

/* 【函数】argv_find_and_parse_vpnvx —— 从命令行参数里识别 vpnv4/vpnv6 关键字。
 * 参数：argv/argc 为 vty 命令词表；index 为输入输出参数，命中时回写该关键字的下标；
 *       afi 为输出参数，命中 "vpnv4" 置 AFI_IP，命中 "vpnv6" 置 AFI_IP6。
 * 返回：命中返回 1，否则 0。
 * 调用时机：show 命令中解析 "<vpnv4|vpnv6>" 时使用；注意 afi 允许为 NULL，用前需判空。 */
extern int argv_find_and_parse_vpnvx(struct cmd_token **argv, int argc,
				     int *index, afi_t *afi)
{
	int ret = 0;
	if (argv_find(argv, argc, "vpnv4", index)) {
		ret = 1;
		if (afi)
			*afi = AFI_IP;
	} else if (argv_find(argv, argc, "vpnv6", index)) {
		ret = 1;
		if (afi)
			*afi = AFI_IP6;
	}
	return ret;
}

/* 【函数】bgp_nlri_parse_vpn —— 解析 VPNv4/VPNv6(SAFI_MPLS_VPN) 的 NLRI 列表。
 * 参数：peer 报文来源；attr 非空表示 UPDATE(通告)，为空表示 WITHDRAW(撤销)；
 *       packet 内 nlri/length 指向本段 NLRI 原始字节。
 * 返回：BGP_NLRI_PARSE_ERROR_* 之一的错误码，成功返回 0。
 * 调用时机：bgp_packet.c 解析 UPDATE 的 MP_REACH/MP_UNREACH 里 SAFI_MPLS_VPN 段时调用。
 * 线上 NLRI 布局（每条，按顺序）：[AddPath ID(可选,4B)] 前缀长度(1B,单位bit)
 *       → 标签(3B) → RD(8B) → 前缀字节(去掉标签+RD后的长度)。
 * 关键点：
 *   - 前缀长度单位是 bit，且最小值 = (标签3 + RD8)=11 字节 = 88 bit(VPN_PREFIXLEN_MIN_BYTES)。
 *   - 真正的 IP 前缀长度 = prefixlen - 88，需再做多项边界检查防止越界读。
 *   - 解出的标签用 bgp_set_valid_label() 置内部“有效”标记位，然后连同 RD 一起交给
 *     bgp_update/bgp_withdraw（VPN 表的 key 由前缀 + RD 共同决定）。
 *   - 使用 STREAM_GET* 宏，任一读取越界会 goto stream_failure。
 * 易错点：psize 与 sizeof(p.u)/prefix_blen 的比较都是防溢出；这几处阈值不可随意放宽。 */
int bgp_nlri_parse_vpn(struct peer *peer, struct attr *attr,
		       struct bgp_nlri *packet)
{
	struct prefix p;
	uint8_t psize = 0;
	uint8_t prefixlen;
	uint16_t type;
	struct rd_as rd_as;
	struct rd_ip rd_ip;
	struct prefix_rd prd = {0};
	mpls_label_t label = {0};
	afi_t afi;
	safi_t safi;
	bool addpath_capable;
	uint32_t addpath_id;
	int ret = 0;

	/* 【中文补充】prd 是 RD 容器：prefixlen 固定 64(=8 字节)，family 置 AF_UNSPEC
	 * 表示随后由 decode_rd_* 按 RD 类型填充；这里先初始化，避免残留脏值。 */
	/* Make prefix_rd */
	prd.family = AF_UNSPEC;
	prd.prefixlen = 64;

	struct stream *data = stream_new(packet->length);
	stream_put(data, packet->nlri, packet->length);
	afi = packet->afi;
	safi = packet->safi;
	addpath_id = 0;

	addpath_capable = bgp_addpath_encode_rx(peer, afi, safi);

/* 【约定】VPN NLRI 里除 IP 前缀外固定含 3 字节标签 + 8 字节 RD，共 11 字节。
 * 故“前缀长度”字段的最小值 = 11*8 = 88 bit；低于此值即为非法报文。 */
#define VPN_PREFIXLEN_MIN_BYTES (3 + 8) /* label + RD */
	while (STREAM_READABLE(data) > 0) {
		/* Clear prefix structure. */
		memset(&p, 0, sizeof(p));

		if (addpath_capable) {
			STREAM_GET(&addpath_id, data, BGP_ADDPATH_ID_LEN);
			addpath_id = ntohl(addpath_id);
		}

		if (STREAM_READABLE(data) < 1) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (truncated NLRI of size %u; no prefix length)",
				peer->host, packet->length);
			ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
			goto done;
		}

		/* Fetch prefix length. */
		STREAM_GETC(data, prefixlen);
		p.family = afi2family(packet->afi);
		psize = PSIZE(prefixlen);

		if (prefixlen < VPN_PREFIXLEN_MIN_BYTES * 8) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (prefix length %d less than VPN min length)",
				peer->host, prefixlen);
			ret = BGP_NLRI_PARSE_ERROR_PREFIX_LENGTH;
			goto done;
		}

		/* sanity check against packet data */
		if (STREAM_READABLE(data) < psize) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (prefix length %d exceeds packet size %u)",
				peer->host, prefixlen, packet->length);
			ret = BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;
			goto done;
		}

		/* sanity check against storage for the IP address portion */
		if ((psize - VPN_PREFIXLEN_MIN_BYTES) > (ssize_t)sizeof(p.u)) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (psize %d exceeds storage size %zu)",
				peer->host,
				prefixlen - VPN_PREFIXLEN_MIN_BYTES * 8,
				sizeof(p.u));
			ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
			goto done;
		}

		/* Sanity check against max bitlen of the address family */
		if ((psize - VPN_PREFIXLEN_MIN_BYTES) > prefix_blen(&p)) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (psize %d exceeds family (%u) max byte len %u)",
				peer->host,
				prefixlen - VPN_PREFIXLEN_MIN_BYTES * 8,
				p.family, prefix_blen(&p));
			ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
			goto done;
		}

		/* Copy label to prefix. */
		if (STREAM_READABLE(data) < BGP_LABEL_BYTES) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (truncated NLRI of size %u; no label)",
				peer->host, packet->length);
			ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
			goto done;
		}

		/* 【标签】取出 3 字节线上标签；立即置 FRR 内部“有效”标记位(见 bgp_label.h)，
		 * 表示该标签已校验可接受，后续据此快速判断。 */
		STREAM_GET(&label, data, BGP_LABEL_BYTES);
		bgp_set_valid_label(&label);

		/* Copy routing distinguisher to rd. */
		if (STREAM_READABLE(data) < 8) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / VPN (truncated NLRI of size %u; no RD)",
				peer->host, packet->length);
			ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
			goto done;
		}
		STREAM_GET(&prd.val, data, 8);

		/* 【RD】拷贝 8 字节 RD 到 prd.val；首 2 字节是按类型编码的类型字段。
		 * decode_rd_type 只取类型做 switch，具体解析由 decode_rd_as/ip 等完成。 */
		/* Decode RD type. */
		type = decode_rd_type(prd.val);

		switch (type) {
		case RD_TYPE_AS:
			decode_rd_as(&prd.val[2], &rd_as);
			break;

		case RD_TYPE_AS4:
			decode_rd_as4(&prd.val[2], &rd_as);
			break;

		case RD_TYPE_IP:
			decode_rd_ip(&prd.val[2], &rd_ip);
			break;

#ifdef ENABLE_BGP_VNC
		case RD_TYPE_VNC_ETH:
			break;
#endif

		default:
			flog_err(EC_BGP_UPDATE_RCV, "Unknown RD type %d", type);
			break; /* just report */
		}

		/* 【前缀】真正的 IP 前缀长度 = 总长 - 88bit；u.val 只读入去掉标签+RD后的字节数。
		 * 前面几处 sanity check 已保证 psize - 11 不超过 u 的容量与地址族最大位数。 */
		/* exclude label & RD */
		p.prefixlen = prefixlen - VPN_PREFIXLEN_MIN_BYTES * 8;
		STREAM_GET(p.u.val, data, psize - VPN_PREFIXLEN_MIN_BYTES);

		if (attr) {
			bgp_update(peer, &p, addpath_id, attr, packet->afi,
				   SAFI_MPLS_VPN, ZEBRA_ROUTE_BGP,
				   BGP_ROUTE_NORMAL, &prd, &label, 1, 0, NULL);
		} else {
			bgp_withdraw(peer, &p, addpath_id, packet->afi,
				     SAFI_MPLS_VPN, ZEBRA_ROUTE_BGP,
				     BGP_ROUTE_NORMAL, &prd, &label, 1);
		}
	}
	/* Packet length consistency check. */
	if (STREAM_READABLE(data) != 0) {
		flog_err(
			EC_BGP_UPDATE_RCV,
			"%s [Error] Update packet error / VPN (%zu data remaining after parsing)",
			peer->host, STREAM_READABLE(data));
		ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
	}

	goto done;

stream_failure:
	flog_err(
		EC_BGP_UPDATE_RCV,
		"%s [Error] Update packet error / VPN (NLRI of size %u - length error)",
		peer->host, packet->length);
	ret = BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;

done:
	stream_free(data);
	return ret;

#undef VPN_PREFIXLEN_MIN_BYTES
}

/*
 * This function informs zebra of the label this vrf sets on routes
 * leaked to VPN. Zebra should install this label in the kernel with
 * an action of "pop label and then use this vrf's IP FIB to route the PDU."
 *
 * Sending this vrf-label association is qualified by a) whether vrf->vpn
 * exporting is active ("export vpn" is enabled, vpn-policy RD and RT list
 * are set), b) whether vpn-policy label is set and c) the vrf loopback
 * interface is up.
 *
 * If any of these conditions do not hold, then we send MPLS_LABEL_NONE
 * for this vrf, which zebra interprets to mean "delete this vrf-label
 * association."
 */
/* 【函数】vpn_leak_zebra_vrf_label_update —— 把本 VRF 的“出口(per-vrf)标签”下发
 * 给 zebra，使内核对该 VRF 标记为 “pop 标签后按本 VRF 的 IP FIB 转发”。
 * 参数：bgp 为 VRF 实例；afi 影响下发时携带的 AFI。
 * 返回：无。副作用：更新 bgp->vpn_policy[afi].tovpn_zebra_vrf_label_last_sent 记录。
 * 调用时机：VPN 导出配置就绪/路由器 ID 变化/postchange 等需要重新同步标签时。
 * 关键分支：只有 vpn_leak_to_vpn_active() 为真且 VRF 回环口 up 时才带上真实标签，
 *   否则发 MPLS_LABEL_NONE(=删除该 vrf-label 关联)。BGP_PREVENT_VRF_2_VRF_LEAK
 *   是内部哨兵值，同样当作删除处理，防止 VRF 间误泄漏。 */
/*
 * This function informs zebra of the label this vrf sets on routes
 * leaked to VPN. Zebra should install this label in the kernel with
 * an action of "pop label and then use this vrf's IP FIB to route the PDU."
 *
 * Sending this vrf-label association is qualified by a) whether vrf->vpn
 * exporting is active ("export vpn" is enabled, vpn-policy RD and RT list
 * are set), b) whether vpn-policy label is set and c) the vrf loopback
 * interface is up.
 *
 * If any of these conditions do not hold, then we send MPLS_LABEL_NONE
 * for this vrf, which zebra interprets to mean "delete this vrf-label
 * association."
 */
void vpn_leak_zebra_vrf_label_update(struct bgp *bgp, afi_t afi)
{
	struct interface *ifp;
	mpls_label_t label = MPLS_LABEL_NONE;
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);

	if (bgp->vrf_id == VRF_UNKNOWN) {
		if (debug) {
			zlog_debug(
				"%s: vrf %s: afi %s: vrf_id not set, can't set zebra vrf label",
				__func__, bgp->name_pretty, afi2str(afi));
		}
		return;
	}

	if (vpn_leak_to_vpn_active(bgp, afi, NULL, false)) {
		ifp = if_get_vrf_loopback(bgp->vrf_id);
		if (ifp && if_is_up(ifp))
			label = bgp->vpn_policy[afi].tovpn_label;
	}

	if (debug) {
		zlog_debug("%s: vrf %s: afi %s: setting label %d for vrf id %d",
			   __func__, bgp->name_pretty, afi2str(afi), label,
			   bgp->vrf_id);
	}

	if (label == BGP_PREVENT_VRF_2_VRF_LEAK)
		label = MPLS_LABEL_NONE;
	zclient_send_vrf_label(bgp_zclient, bgp->vrf_id, afi, label, ZEBRA_LSP_BGP);
	bgp->vpn_policy[afi].tovpn_zebra_vrf_label_last_sent = label;
}

/* 【函数】vpn_leak_zebra_vrf_label_withdraw —— 通知 zebra 删除本 VRF 的出口标签关联
 * （VRF 被反配置/删除时调用）。即发送 MPLS_LABEL_NONE，并同步 last_sent 记录。 */
/*
 * If zebra tells us vrf has become unconfigured, tell zebra not to
 * use this label to forward to the vrf anymore
 */
void vpn_leak_zebra_vrf_label_withdraw(struct bgp *bgp, afi_t afi)
{
	mpls_label_t label = MPLS_LABEL_NONE;
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);

	if (bgp->vrf_id == VRF_UNKNOWN) {
		if (debug) {
			zlog_debug(
				"%s: vrf_id not set, can't delete zebra vrf label",
				__func__);
		}
		return;
	}

	if (debug) {
		zlog_debug("%s: deleting label for vrf %s (id=%d)", __func__,
			   bgp->name_pretty, bgp->vrf_id);
	}

	zclient_send_vrf_label(bgp_zclient, bgp->vrf_id, afi, label, ZEBRA_LSP_BGP);
	bgp->vpn_policy[afi].tovpn_zebra_vrf_label_last_sent = label;
}

/*
 * This function informs zebra of the srv6-function this vrf sets on routes
 * leaked to VPN. Zebra should install this srv6-function in the kernel with
 * an action of "End.DT4/6's IP FIB to route the PDU."
 */
/* 【函数】vpn_leak_zebra_vrf_sid_update_per_af —— per-AFI 的 SRv6 场景：把该 AFI 下
 * 的 tovpn_sid 作为 End.DT4/DT6 localsid 下发给 zebra，让内核按 VRF 表做 SRv6 转发。
 * 参数：bgp 为 VRF 实例；afi 决定行为端(END_DT4/END_DT6)。
 * 关键点：未设 sid 或找不到 VRF/回环口则直接返回；下发成功后备份一份 sid 到
 *   tovpn_zebra_vrf_sid_last_sent（供 tovpn 变更比较/撤回用）。 */
void vpn_leak_zebra_vrf_sid_update_per_af(struct bgp *bgp, afi_t afi)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);
	enum seg6local_action_t act;
	struct seg6local_context ctx = {};
	struct in6_addr *tovpn_sid = NULL;
	struct in6_addr *tovpn_sid_ls = NULL;
	struct vrf *vrf;
	struct interface *ifp;

	if (bgp->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug("%s: vrf %s: afi %s: vrf_id not set, can't set zebra vrf label",
				   __func__, bgp->name_pretty, afi2str(afi));
		return;
	}

	tovpn_sid = bgp->vpn_policy[afi].tovpn_sid;
	if (!tovpn_sid) {
		if (debug)
			zlog_debug("%s: vrf %s: afi %s: sid not set", __func__,
				   bgp->name_pretty, afi2str(afi));
		return;
	}

	if (debug)
		zlog_debug("%s: vrf %s: afi %s: setting sid %pI6 for vrf id %d",
			   __func__, bgp->name_pretty, afi2str(afi), tovpn_sid,
			   bgp->vrf_id);

	vrf = vrf_lookup_by_id(bgp->vrf_id);
	if (!vrf)
		return;

	ifp = if_get_vrf_loopback(bgp->vrf_id);
	if (!ifp)
		return;

	if (bgp->vpn_policy[afi].tovpn_sid_locator) {
		ctx.block_len =
			bgp->vpn_policy[afi].tovpn_sid_locator->block_bits_length;
		ctx.node_len =
			bgp->vpn_policy[afi].tovpn_sid_locator->node_bits_length;
		if (CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_FUNC_WIDE))
			ctx.function_len = BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
		else
			ctx.function_len =
				bgp->vpn_policy[afi].tovpn_sid_locator->function_bits_length;
		ctx.argument_len =
			bgp->vpn_policy[afi]
				.tovpn_sid_locator->argument_bits_length;
		if (CHECK_FLAG(bgp->vpn_policy[afi].tovpn_sid_locator->flags, SRV6_LOCATOR_USID))
			SET_SRV6_FLV_OP(ctx.flv.flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_NEXT_CSID);
	}
	ctx.table = vrf->data.l.table_id;
	act = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
		: ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
	zclient_send_localsid(bgp_zclient, ZEBRA_ROUTE_ADD, tovpn_sid, IPV6_MAX_BITLEN,
			      ifp->ifindex, act, &ctx);

	tovpn_sid_ls = XCALLOC(MTYPE_BGP_SRV6_SID, sizeof(struct in6_addr));
	*tovpn_sid_ls = *tovpn_sid;
	if (bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent)
		XFREE(MTYPE_BGP_SRV6_SID,
		      bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent);
	bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent = tovpn_sid_ls;
}

/*
 * This function informs zebra of the srv6-function this vrf sets on routes
 * leaked to VPN. Zebra should install this srv6-function in the kernel with
 * an action of "End.DT46's IP FIB to route the PDU."
 */
/* 【函数】vpn_leak_zebra_vrf_sid_update_per_vrf —— per-VRF 的 SRv6 场景(END.DT46)，
 * 与 per_af 版类似，但使用 bgp->tovpn_sid，行为端固定为 END_DT46。 */
void vpn_leak_zebra_vrf_sid_update_per_vrf(struct bgp *bgp)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);
	enum seg6local_action_t act;
	struct seg6local_context ctx = {};
	struct in6_addr *tovpn_sid = NULL;
	struct in6_addr *tovpn_sid_ls = NULL;
	struct vrf *vrf;
	struct interface *ifp;

	if (bgp->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug(
				"%s: vrf %s: vrf_id not set, can't set zebra vrf label",
				__func__, bgp->name_pretty);
		return;
	}

	tovpn_sid = bgp->tovpn_sid;
	if (!tovpn_sid) {
		if (debug)
			zlog_debug("%s: vrf %s: sid not set", __func__,
				   bgp->name_pretty);
		return;
	}

	if (debug)
		zlog_debug("%s: vrf %s: setting sid %pI6 for vrf id %d",
			   __func__, bgp->name_pretty, tovpn_sid, bgp->vrf_id);

	vrf = vrf_lookup_by_id(bgp->vrf_id);
	if (!vrf)
		return;

	ifp = if_get_vrf_loopback(bgp->vrf_id);
	if (!ifp)
		return;

	if (bgp->tovpn_sid_locator) {
		ctx.block_len = bgp->tovpn_sid_locator->block_bits_length;
		ctx.node_len = bgp->tovpn_sid_locator->node_bits_length;
		if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_FUNC_WIDE))
			ctx.function_len = BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
		else
			ctx.function_len = bgp->tovpn_sid_locator->function_bits_length;
		ctx.argument_len = bgp->tovpn_sid_locator->argument_bits_length;
		if (CHECK_FLAG(bgp->tovpn_sid_locator->flags, SRV6_LOCATOR_USID))
			SET_SRV6_FLV_OP(ctx.flv.flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_NEXT_CSID);
	}
	ctx.table = vrf->data.l.table_id;
	act = ZEBRA_SEG6_LOCAL_ACTION_END_DT46;
	zclient_send_localsid(bgp_zclient, ZEBRA_ROUTE_ADD, tovpn_sid, IPV6_MAX_BITLEN,
			      ifp->ifindex, act, &ctx);

	tovpn_sid_ls = XCALLOC(MTYPE_BGP_SRV6_SID, sizeof(struct in6_addr));
	*tovpn_sid_ls = *tovpn_sid;
	if (bgp->tovpn_zebra_vrf_sid_last_sent)
		XFREE(MTYPE_BGP_SRV6_SID, bgp->tovpn_zebra_vrf_sid_last_sent);
	bgp->tovpn_zebra_vrf_sid_last_sent = tovpn_sid_ls;
}

/*
 * This function informs zebra of the srv6-function this vrf sets on routes
 * leaked to VPN. Zebra should install this srv6-function in the kernel with
 * an action of "End.DT4/6/46's IP FIB to route the PDU."
 */
/* 【函数】vpn_leak_zebra_vrf_sid_update —— SRv6 SID 下发的分发入口：
 * 若配置了 per-AFI SID 走 _per_af，否则若配置了 per-VRF SID 走 _per_vrf，否则只打印调试。 */
void vpn_leak_zebra_vrf_sid_update(struct bgp *bgp, afi_t afi)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);

	if (bgp->vpn_policy[afi].tovpn_sid)
		return vpn_leak_zebra_vrf_sid_update_per_af(bgp, afi);

	if (bgp->tovpn_sid)
		return vpn_leak_zebra_vrf_sid_update_per_vrf(bgp);

	if (debug)
		zlog_debug("%s: vrf %s: afi %s: sid not set", __func__,
			   bgp->name_pretty, afi2str(afi));
}

/*
 * If zebra tells us vrf has become unconfigured, tell zebra not to
 * use this srv6-function to forward to the vrf anymore
 */
/* 【函数】vpn_leak_zebra_vrf_sid_withdraw_per_af —— 撤销 per-AFI 的 SRv6 localsid：
 * 向 zebra 发 ZEBRA_ROUTE_DELETE，释放本地备份的 sid，并向 zebra 归还 SRv6 SID 资源。 */
void vpn_leak_zebra_vrf_sid_withdraw_per_af(struct bgp *bgp, afi_t afi)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);
	struct srv6_sid_ctx ctx = {};
	struct seg6local_context seg6localctx = {};
	struct interface *ifp;

	if (bgp->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug("%s: vrf %s: afi %s: vrf_id not set, can't set zebra vrf label",
				   __func__, bgp->name_pretty, afi2str(afi));
		return;
	}

	if (debug)
		zlog_debug("%s: deleting sid for vrf %s afi %s (id=%d)", __func__,
			   bgp->name_pretty, afi2str(afi), bgp->vrf_id);

	ifp = if_get_vrf_loopback(bgp->vrf_id);
	if (!ifp)
		return;

	if (bgp->vpn_policy[afi].tovpn_sid_locator) {
		seg6localctx.block_len =
			bgp->vpn_policy[afi].tovpn_sid_locator->block_bits_length;
		seg6localctx.node_len =
			bgp->vpn_policy[afi].tovpn_sid_locator->node_bits_length;
		if (CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_FUNC_WIDE))
			seg6localctx.function_len = BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
		else
			seg6localctx.function_len =
				bgp->vpn_policy[afi].tovpn_sid_locator->function_bits_length;
		seg6localctx.argument_len =
			bgp->vpn_policy[afi]
				.tovpn_sid_locator->argument_bits_length;
	}
	zclient_send_localsid(bgp_zclient, ZEBRA_ROUTE_DELETE,
			      bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent, IPV6_MAX_BITLEN,
			      ifp->ifindex, ZEBRA_SEG6_LOCAL_ACTION_UNSPEC, &seg6localctx);
	XFREE(MTYPE_BGP_SRV6_SID,
	      bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent);
	bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent = NULL;

	ctx.vrf_id = bgp->vrf_id;
	ctx.behavior = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
				     : ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
	bgp_zebra_release_srv6_sid(&ctx, bgp->vpn_policy[afi].tovpn_sid_locator->name);
	UNSET_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_FUNC_WIDE);
}

/*
 * If zebra tells us vrf has become unconfigured, tell zebra not to
 * use this srv6-function to forward to the vrf anymore
 */
/* 【函数】vpn_leak_zebra_vrf_sid_withdraw_per_vrf —— 撤销 per-VRF(END.DT46) 的 localsid，
 * 与 per_af 版类似。 */
void vpn_leak_zebra_vrf_sid_withdraw_per_vrf(struct bgp *bgp)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);
	struct srv6_sid_ctx ctx = {};
	struct seg6local_context seg6localctx = {};
	struct interface *ifp;

	if (bgp->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug(
				"%s: vrf %s: vrf_id not set, can't set zebra vrf label",
				__func__, bgp->name_pretty);
		return;
	}

	if (debug)
		zlog_debug("%s: deleting sid for vrf %s (id=%d)", __func__,
			   bgp->name_pretty, bgp->vrf_id);

	ifp = if_get_vrf_loopback(bgp->vrf_id);
	if (!ifp)
		return;

	if (bgp->tovpn_sid_locator) {
		seg6localctx.block_len =
			bgp->tovpn_sid_locator->block_bits_length;
		seg6localctx.node_len = bgp->tovpn_sid_locator->node_bits_length;
		if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_FUNC_WIDE))
			seg6localctx.function_len = BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
		else
			seg6localctx.function_len = bgp->tovpn_sid_locator->function_bits_length;
		seg6localctx.argument_len =
			bgp->tovpn_sid_locator->argument_bits_length;
	}
	zclient_send_localsid(bgp_zclient, ZEBRA_ROUTE_DELETE, bgp->tovpn_zebra_vrf_sid_last_sent,
			      IPV6_MAX_BITLEN, ifp->ifindex, ZEBRA_SEG6_LOCAL_ACTION_UNSPEC,
			      &seg6localctx);
	XFREE(MTYPE_BGP_SRV6_SID, bgp->tovpn_zebra_vrf_sid_last_sent);
	bgp->tovpn_zebra_vrf_sid_last_sent = NULL;

	ctx.vrf_id = bgp->vrf_id;
	ctx.behavior = ZEBRA_SEG6_LOCAL_ACTION_END_DT46;
	bgp_zebra_release_srv6_sid(&ctx, bgp->tovpn_sid_locator->name);
	UNSET_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_FUNC_WIDE);
}

/*
 * If zebra tells us vrf has become unconfigured, tell zebra not to
 * use this srv6-function to forward to the vrf anymore
 */
/* 【函数】vpn_leak_zebra_vrf_sid_withdraw —— 按“哪个 last_sent 非空”分别调用
 * per_af / per_vrf 的撤销路径（两者都有备份时都会撤销）。 */
void vpn_leak_zebra_vrf_sid_withdraw(struct bgp *bgp, afi_t afi)
{
	if (bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent)
		vpn_leak_zebra_vrf_sid_withdraw_per_af(bgp, afi);

	if (bgp->tovpn_zebra_vrf_sid_last_sent)
		vpn_leak_zebra_vrf_sid_withdraw_per_vrf(bgp);
}

/* 【函数】vpn_leak_label_callback —— 标签池(LabelPool)异步分配“per-VRF 出口标签”
 * 的回调。参数 labelid 实际是 struct vpn_policy *（含 afi/bgp 反指）。
 * 参数：label 分配到的新标签；allocated=false 表示标签被回收/失效。
 * 返回：0 表示接受(或无需处理)，非 0 表示拒绝本次分配。
 * 调用时机：bgp_lp_get(LP_TYPE_VRF,...) 的异步结果回来时。
 * 关键分支：
 *   - 失效(allocated=false)且此前是 auto 分配：清 NONE 并 prechange/postchange 触发重算。
 *   - 新分配但当前非 auto 模式：返回 -1 拒绝（可能配置已改）。
 *   - 有标签且未分配过/同一标签：无需动作或用新值(prechange→改label→postchange)。 */
int vpn_leak_label_callback(
	mpls_label_t label,
	void *labelid,
	bool allocated)
{
	struct vpn_policy *vp = (struct vpn_policy *)labelid;
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);

	if (debug)
		zlog_debug("%s: label=%u, allocated=%d",
			__func__, label, allocated);

	if (!allocated) {
		/*
		 * previously-allocated label is now invalid
		 */
		if (CHECK_FLAG(vp->flags, BGP_VPN_POLICY_TOVPN_LABEL_AUTO) &&
			(vp->tovpn_label != MPLS_LABEL_NONE)) {

			vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN,
				vp->afi, bgp_get_default(), vp->bgp);
			vp->tovpn_label = MPLS_LABEL_NONE;
			vpn_leak_postchange(BGP_VPN_POLICY_DIR_TOVPN,
				vp->afi, bgp_get_default(), vp->bgp);
		}
		return 0;
	}

	/*
	 * New label allocation
	 */
	if (!CHECK_FLAG(vp->flags, BGP_VPN_POLICY_TOVPN_LABEL_AUTO)) {

		/*
		 * not currently configured for auto label, reject allocation
		 */
		return -1;
	}

	if (vp->tovpn_label != MPLS_LABEL_NONE) {
		if (label == vp->tovpn_label) {
			/* already have same label, accept but do nothing */
			return 0;
		}
		/* Shouldn't happen: different label allocation */
		flog_err(EC_BGP_LABEL,
			 "%s: %s had label %u but got new assignment %u",
			 __func__, vp->bgp->name_pretty, vp->tovpn_label,
			 label);
		/* use new one */
	}

	vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN,
		vp->afi, bgp_get_default(), vp->bgp);
	vp->tovpn_label = label;
	vpn_leak_postchange(BGP_VPN_POLICY_DIR_TOVPN,
		vp->afi, bgp_get_default(), vp->bgp);

	return 0;
}

/* 【函数】sid_register —— 记录本 bgp 实例已占用一个 SRv6 SID：把 (sid, locator_name)
 * 打包成 bgp_srv6_function 追加到 bgp->srv6_functions 链表，供冲突检测( sid_exist )。
 * 参数：bgp 通常为默认实例(bgp_get_default)；sid 为 16 字节 SID；locator_name 为所属 locator 名。 */
void sid_register(struct bgp *bgp, const struct in6_addr *sid,
		  const char *locator_name)
{
	struct bgp_srv6_function *func;
	func = XCALLOC(MTYPE_BGP_SRV6_FUNCTION,
		       sizeof(struct bgp_srv6_function));
	func->sid = *sid;
	snprintf(func->locator_name, sizeof(func->locator_name),
		 "%s", locator_name);
	listnode_add(bgp->srv6_functions, func);
}

/* 【函数】srv6_function_free —— 释放一个 bgp_srv6_function 记录。 */
void srv6_function_free(struct bgp_srv6_function *func)
{
	XFREE(MTYPE_BGP_SRV6_FUNCTION, func);
}

/* 【函数】sid_unregister —— 从 srv6_functions 链表中删除并释放与给定 sid 匹配的
 * 记录（SID 被释放时调用）。注意使用了 _SAFE 遍历，因为循环内会删除节点。 */
void sid_unregister(struct bgp *bgp, const struct in6_addr *sid)
{
	struct listnode *node, *nnode;
	struct bgp_srv6_function *func;

	for (ALL_LIST_ELEMENTS(bgp->srv6_functions, node, nnode, func))
		if (sid_same(&func->sid, sid)) {
			listnode_delete(bgp->srv6_functions, func);
			srv6_function_free(func);
		}
}

/* 【函数】sid_exist —— 判断给定 SID 是否已被本实例占用（只读遍历 srv6_functions）。
 * 供 srv6_sid_compose() 分配前做冲突检查，防止两个 VRF 分到同一 SID。 */
static bool sid_exist(struct bgp *bgp, const struct in6_addr *sid)
{
	struct listnode *node;
	struct bgp_srv6_function *func;

	for (ALL_LIST_ELEMENTS_RO(bgp->srv6_functions, node, func))
		if (sid_same(&func->sid, sid))
			return true;
	return false;
}

/* 【函数】srv6_sid_compose —— 由 LOCATOR 前缀与 FUNCTION 部分组合出完整 SRv6 SID
 * (布局 LOC:FUNC::)。参数 sid_value 为出参(SID 值)；locator 提供前缀与各段位宽；
 * sid_func 为功能号。返回 true 成功。
 * 关键概念（RFC9252 SID Transposition）：
 *   - 若 function 位宽 <= 20bit，则把 FUNC 左移到 20bit 标签的最高位作为“标签”，
 *     再按 transposition 方案摆放到 SID 中（该标签会随 SRv6 Service TLV 的 label
 *     字段一起通告，见 vpn_leak_from_vrf_fill_srv6）。
 *   - 位宽更大或 f3216/f4816 超出 16bit 时，直接把 FUNC 摆进 SID 的相应偏移。
 *   - 组合前会用 sid_exist() 检查冲突、并核对 function 位宽不超过上限。 */
/**
 * Return the SRv6 SID value obtained by composing the LOCATOR and FUNCTION.
 *
 * @param sid_value SRv6 SID value returned
 * @param locator Parent locator of the SRv6 SID
 * @param sid_func Function part of the SID
 * @return True if success, False otherwise
 */
bool srv6_sid_compose(struct in6_addr *sid_value, struct srv6_locator *locator, uint32_t sid_func)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);
	int label = 0;
	uint8_t offset = 0;
	uint8_t func_len = 0, shift_len = 0;
	uint32_t sid_func_max = 0;
	bool store_in_label = true;

	if (!locator || !sid_value)
		return false;

	if (locator->function_bits_length > BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP) {
		if (debug)
			zlog_debug("%s: invalid SRv6 Locator (%pFX): Function Length must be less or equal to %d",
				   __func__, &locator->prefix,
				   BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL);
		return false;
	}

	/* Max value that can be encoded in the Function part of the SID */
	if ((CHECK_FLAG(locator->flags, SRV6_LOCATOR_F3216) ||
	     CHECK_FLAG(locator->flags, SRV6_LOCATOR_F4816)) &&
	    sid_func > 0xFFFF) {
		/* f3216 and f4816 supports ewlib. increase sid_func_max */
		sid_func_max = 0xffffffff;
		store_in_label = false;
	} else if (locator->function_bits_length == BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP)
		/* shifting 32 bits over a 32 bit is not possible. directly encode func_max */
		sid_func_max = 0xffffffff;
	else
		sid_func_max = (1 << locator->function_bits_length) - 1;


	if (sid_func > sid_func_max) {
		if (debug)
			zlog_debug("%s: invalid SRv6 Locator (%pFX): Function Length is too short (%u) to support specified function (%u)",
				   __func__, &locator->prefix, sid_func_max, sid_func);
		return false;
	}

	/**
	 * Let's build the SID value.
	 * sid_value = LOC:FUNC::
	 */

	/* First, we put the locator (LOC) in the most significant bits of sid_value */
	*sid_value = locator->prefix.prefix;

	if (sid_exist(bgp_get_default(), sid_value)) {
		zlog_warn("%s: skipped to allocate SRv6 SID (%pFX): SID %pI6 already in use",
			  __func__, &locator->prefix, sid_value);
		return false;
	}

	/*
	 * Then, we compute the offset at which we have to place the function (FUNC).
	 * FUNC will be placed immediately after LOC, i.e. at block_bits_length + node_bits_length
	 */
	/* FUNC 紧接 LOC 之后，偏移 = block + node 位宽（从 SID 最高位起算）。 */
	offset = locator->block_bits_length + locator->node_bits_length;

	if (store_in_label)
		func_len = locator->function_bits_length;
	else
		func_len = BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
	if (store_in_label && func_len <= BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL) {
		/*
		 * The FUNC part of the SID is advertised in the label field of SRv6 Service TLV.
		 * (see SID Transposition Scheme, RFC 9252 section #4).
		 * Therefore, we need to encode the FUNC in the most significant bits of the
		 * 20-bit label.
		 */
		/* 把 FUNC 左移到 20bit 标签的最高有效位（右对齐到 20bit 顶端）。 */
		shift_len = BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL - func_len;
		label = sid_func << shift_len;
		if (label < MPLS_LABEL_UNRESERVED_MIN) {
			if (debug)
				zlog_debug("%s: skipped to allocate SRv6 SID (%pFX): Label (%u) is too small to use",
					   __func__, &locator->prefix, label);
			return false;
		}
		/* Finally, we put the FUNC in sid_value at the computed offset */
		transpose_sid(sid_value, label, offset, func_len,
			      BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL);
	} else {
		label = sid_func;
		transpose_sid(sid_value, label, offset, func_len,
			      BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP);
	}

	return true;
}

/* 【函数】ensure_vrf_tovpn_sid_per_af —— per-AFI 场景：为某 VRF 的某 AFI 申请/确保
 * 一个发往 VPN 的出向 SRv6 SID。参数 bgp_vpn 为默认实例(提供 locator)，bgp_vrf 为
 * 目标 VRF，afi 为目标地址族。
 * 三种模式互斥：index-mode(tovpn_sid_index)、auto-mode(SID_AUTO)、explicit-mode(SID_EXPLICIT)。
 * 关键点：已存在则跳过；按模式算/取 SID 后调 bgp_zebra_request_srv6_sid 向 zebra 申请
 *   (异步，真正落地在回调里)。配置冲突/缺值/无 locator 等异常均提前 return。 */
void ensure_vrf_tovpn_sid_per_af(struct bgp *bgp_vpn, struct bgp *bgp_vrf,
				 afi_t afi)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	struct in6_addr tovpn_sid = {};
	uint32_t tovpn_sid_index = 0;
	bool tovpn_sid_auto = false;
	bool is_tovpn_sid_explicit = false;
	struct srv6_sid_ctx ctx = {};
	uint32_t sid_func;
	struct srv6_locator *locator_bgp;

	if (debug)
		zlog_debug("%s: try to allocate new SID for vrf %s: afi %s",
			   __func__, bgp_vrf->name_pretty, afi2str(afi));

	/* skip when tovpn sid is already allocated on vrf instance */
	if (bgp_vrf->vpn_policy[afi].tovpn_sid)
		return;

	/*
	 * skip when bgp vpn instance ins't allocated
	 * or srv6 locator isn't allocated
	 */
	if (!bgp_vpn)
		return;

	locator_bgp = bgp_srv6_locator_lookup(bgp_vrf, bgp_vpn);
	/*
	 * skip when bgp srv6 locator isn't allocated
	 */
	if (!locator_bgp)
		return;

	if (bgp_vrf->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug("%s: vrf %s: vrf_id not set, can't set zebra vrf SRv6 SID",
				   __func__, bgp_vrf->name_pretty);
		return;
	}

	tovpn_sid_index = bgp_vrf->vpn_policy[afi].tovpn_sid_index;
	tovpn_sid_auto = CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags,
				    BGP_VPN_POLICY_TOVPN_SID_AUTO);
	is_tovpn_sid_explicit = CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags,
					   BGP_VPN_POLICY_TOVPN_SID_EXPLICIT);

	/* skip when VPN isn't configured on vrf-instance */
	if (tovpn_sid_index == 0 && !tovpn_sid_auto && !is_tovpn_sid_explicit)
		return;

	/* check invalid case more than one mode configured
	 * among index, auto and explicit
	 */
	if ((tovpn_sid_index != 0 && tovpn_sid_auto) ||
	    (tovpn_sid_index != 0 && is_tovpn_sid_explicit) ||
	    (tovpn_sid_auto && is_tovpn_sid_explicit)) {
		flog_err(EC_BGP_SRV6_SID_MODE_CONFLICT,
			 "%s: more than one mode selected among index-mode, auto-mode and explicit-mode. ignored.",
			 __func__);
		return;
	}

	/* skip when sid value isn't set for explicit-mode */
	if (is_tovpn_sid_explicit && !bgp_vrf->vpn_policy[afi].tovpn_sid_explicit) {
		zlog_err("%s: explicit-mode selected without sid value.", __func__);
		return;
	}
	if (!tovpn_sid_auto && !is_tovpn_sid_explicit) {
		if (!srv6_sid_compose(&tovpn_sid, locator_bgp, tovpn_sid_index)) {
			zlog_err("%s: failed to compose sid for vrf %s: afi %s",
				 __func__, bgp_vrf->name_pretty, afi2str(afi));
			return;
		}
	} else if (is_tovpn_sid_explicit) {
		tovpn_sid = *(bgp_vrf->vpn_policy[afi].tovpn_sid_explicit);
	}

	ctx.vrf_id = bgp_vrf->vrf_id;
	ctx.behavior = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
				     : ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
	if (!bgp_zebra_request_srv6_sid(&ctx, &tovpn_sid, locator_bgp->name, &sid_func)) {
		zlog_err("%s: failed to request sid for vrf %s: afi %s", __func__,
			 bgp_vrf->name_pretty, afi2str(afi));
		return;
	}
	if (debug)
		zlog_debug("%s: allocating new SID for vrf %s: afi %s, locator %s", __func__,
			   bgp_vrf->name_pretty, afi2str(afi), locator_bgp->name);
}

/* 【函数】ensure_vrf_tovpn_sid_per_vrf —— per-VRF 场景，为整个 VRF 申请一个
 * END.DT46 的出向 SID（vrf_flags 上的 index/auto/explicit 模式）。逻辑与 per_af 版同构。 */
void ensure_vrf_tovpn_sid_per_vrf(struct bgp *bgp_vpn, struct bgp *bgp_vrf)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	struct in6_addr tovpn_sid = {};
	uint32_t tovpn_sid_index = 0;
	bool tovpn_sid_auto = false;
	bool is_tovpn_sid_explicit = false;
	struct srv6_sid_ctx ctx = {};
	uint32_t sid_func;
	struct srv6_locator *locator_bgp;

	if (debug)
		zlog_debug("%s: try to allocate new SID for vrf %s", __func__,
			   bgp_vrf->name_pretty);

	/* skip when tovpn sid is already allocated on vrf instance */
	if (bgp_vrf->tovpn_sid)
		return;

	/*
	 * skip when bgp vpn instance ins't allocated
	 * or srv6 locator isn't allocated
	 */
	if (!bgp_vpn)
		return;

	locator_bgp = bgp_srv6_locator_lookup(bgp_vrf, bgp_vpn);
	/*
	 * skip when bgp srv6 locator isn't allocated
	 */
	if (!locator_bgp)
		return;

	if (bgp_vrf->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug("%s: vrf %s: vrf_id not set, can't set zebra vrf SRv6 SID",
				   __func__, bgp_vrf->name_pretty);
		return;
	}

	tovpn_sid_index = bgp_vrf->tovpn_sid_index;
	tovpn_sid_auto = CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_AUTO);
	is_tovpn_sid_explicit = CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT);

	/* skip when VPN isn't configured on vrf-instance */
	if (tovpn_sid_index == 0 && !tovpn_sid_auto && !is_tovpn_sid_explicit)
		return;

	/*
	 * check invalid case more than one mode configured
	 * among index, auto and explicit
	 */
	if ((tovpn_sid_index != 0 && tovpn_sid_auto) ||
	    (tovpn_sid_index != 0 && is_tovpn_sid_explicit) ||
	    (tovpn_sid_auto && is_tovpn_sid_explicit)) {
		flog_err(EC_BGP_SRV6_SID_MODE_CONFLICT,
			 "%s: more than one mode selected among index-mode, auto-mode and explicit-mode. ignored.",
			 __func__);
		return;
	}

	/* skip when sid value isn't set for explicit-mode */
	if (is_tovpn_sid_explicit && !bgp_vrf->tovpn_sid_explicit) {
		zlog_err("%s: explicit-mode selected without sid value.", __func__);
		return;
	}

	if (!tovpn_sid_auto && !is_tovpn_sid_explicit) {
		if (!srv6_sid_compose(&tovpn_sid, locator_bgp, bgp_vrf->tovpn_sid_index)) {
			zlog_err("%s: failed to compose new sid for vrf %s",
				 __func__, bgp_vrf->name_pretty);
			return;
		}
	} else if (is_tovpn_sid_explicit) {
		tovpn_sid = *(bgp_vrf->tovpn_sid_explicit);
	}

	ctx.vrf_id = bgp_vrf->vrf_id;
	ctx.behavior = ZEBRA_SEG6_LOCAL_ACTION_END_DT46;
	if (!bgp_zebra_request_srv6_sid(&ctx, &tovpn_sid, locator_bgp->name, &sid_func)) {
		zlog_err("%s: failed to request new sid for vrf %s", __func__,
			 bgp_vrf->name_pretty);
		return;
	}
	if (debug)
		zlog_debug("%s: allocating new SID for vrf %s, locator %s", __func__,
			   bgp_vrf->name_pretty, locator_bgp->name);
}

/* 【函数】ensure_vrf_tovpn_sid —— 分发：优先 per-AFI(若该 afi 启用 SRv6 VPN)，
 * 否则 per-VRF(若整个 VRF 启用)。 */
void ensure_vrf_tovpn_sid(struct bgp *bgp_vpn, struct bgp *bgp_vrf, afi_t afi)
{
	/* per-af sid */
	if (is_srv6_vpn_afi_enabled(bgp_vrf, afi))
		return ensure_vrf_tovpn_sid_per_af(bgp_vpn, bgp_vrf, afi);

	/* per-vrf sid */
	if (is_srv6_vpn_vrf_enabled(bgp_vrf))
		return ensure_vrf_tovpn_sid_per_vrf(bgp_vpn, bgp_vrf);
}

/* 【函数】delete_vrf_tovpn_sid_per_af —— per-AFI 释放：仅当该 afi 不再配置任何
 * index/auto/explicit 模式时才真正释放 SID、注销(sid_unregister)、释放 locator。
 * 会向 zebra 归还 SRv6 SID 资源并清 BGP_VPN_POLICY_TOVPN_SID_FUNC_WIDE 标志。 */
void delete_vrf_tovpn_sid_per_af(struct bgp *bgp_vpn, struct bgp *bgp_vrf,
				 afi_t afi)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	uint32_t tovpn_sid_index = 0;
	bool tovpn_sid_auto = false;
	bool is_tovpn_sid_explicit = false;
	struct srv6_sid_ctx ctx = {};

	if (debug)
		zlog_debug("%s: try to remove SID for vrf %s: afi %s", __func__,
			   bgp_vrf->name_pretty, afi2str(afi));

	tovpn_sid_index = bgp_vrf->vpn_policy[afi].tovpn_sid_index;
	tovpn_sid_auto = CHECK_FLAG(bgp_vrf->vpn_policy[afi].flags,
				    BGP_VPN_POLICY_TOVPN_SID_AUTO);
	is_tovpn_sid_explicit = CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT);

	/* skip when VPN is configured on vrf-instance */
	if (tovpn_sid_index != 0 || tovpn_sid_auto || is_tovpn_sid_explicit)
		return;

	if (bgp_vrf->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug("%s: vrf %s: vrf_id not set, can't set zebra vrf sid", __func__,
				   bgp_vrf->name_pretty);
		return;
	}

	if (bgp_vrf->vpn_policy[afi].tovpn_sid) {
		ctx.vrf_id = bgp_vrf->vrf_id;
		ctx.behavior = afi == AFI_IP ? ZEBRA_SEG6_LOCAL_ACTION_END_DT4
					     : ZEBRA_SEG6_LOCAL_ACTION_END_DT6;
		bgp_zebra_release_srv6_sid(&ctx, bgp_vrf->vpn_policy[afi].tovpn_sid_locator->name);
		UNSET_FLAG(bgp_vrf->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_FUNC_WIDE);

		sid_unregister(bgp_vpn, bgp_vrf->vpn_policy[afi].tovpn_sid);
		XFREE(MTYPE_BGP_SRV6_SID, bgp_vrf->vpn_policy[afi].tovpn_sid);
	}

	if (bgp_vrf->vpn_policy[afi].tovpn_sid_explicit)
		XFREE(MTYPE_BGP_SRV6_SID, bgp_vrf->vpn_policy[afi].tovpn_sid_explicit);

	bgp_vrf->vpn_policy[afi].tovpn_sid_transpose_label = 0;

	srv6_locator_free(bgp_vrf->vpn_policy[afi].tovpn_sid_locator);
	bgp_vrf->vpn_policy[afi].tovpn_sid_locator = NULL;
}

/* 【函数】delete_vrf_tovpn_sid_per_vrf —— per-VRF(END.DT46) 的释放路径，与 per_af 同构。 */
void delete_vrf_tovpn_sid_per_vrf(struct bgp *bgp_vpn, struct bgp *bgp_vrf)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	uint32_t tovpn_sid_index = 0;
	bool tovpn_sid_auto = false;
	bool is_tovpn_sid_explicit = false;
	struct srv6_sid_ctx ctx = {};

	if (debug)
		zlog_debug("%s: try to remove SID for vrf %s", __func__,
			   bgp_vrf->name_pretty);

	tovpn_sid_index = bgp_vrf->tovpn_sid_index;
	tovpn_sid_auto =
		CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_AUTO);
	is_tovpn_sid_explicit = CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT);

	/* skip when VPN is configured on vrf-instance */
	if (tovpn_sid_index != 0 || tovpn_sid_auto || is_tovpn_sid_explicit)
		return;

	if (bgp_vrf->vrf_id == VRF_UNKNOWN) {
		if (debug)
			zlog_debug("%s: vrf %s: vrf_id not set, can't set zebra vrf label",
				   __func__, bgp_vrf->name_pretty);
		return;
	}

	if (bgp_vrf->tovpn_sid) {
		ctx.vrf_id = bgp_vrf->vrf_id;
		ctx.behavior = ZEBRA_SEG6_LOCAL_ACTION_END_DT46;
		bgp_zebra_release_srv6_sid(&ctx, bgp_vrf->tovpn_sid_locator->name);
		UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_TOVPN_SID_FUNC_WIDE);

		sid_unregister(bgp_vpn, bgp_vrf->tovpn_sid);
		XFREE(MTYPE_BGP_SRV6_SID, bgp_vrf->tovpn_sid);
	}
	if (bgp_vrf->tovpn_sid_explicit) {
		XFREE(MTYPE_BGP_SRV6_SID, bgp_vrf->tovpn_sid_explicit);
	}
	bgp_vrf->tovpn_sid_transpose_label = 0;

	srv6_locator_free(bgp_vrf->tovpn_sid_locator);
	bgp_vrf->tovpn_sid_locator = NULL;
}

/* 【函数】delete_vrf_tovpn_sid —— 无条件调用 per_af 与 per_vrf 两个释放路径
 * （各自内部会判断是否真的配置了对应模式）。 */
void delete_vrf_tovpn_sid(struct bgp *bgp_vpn, struct bgp *bgp_vrf, afi_t afi)
{
	delete_vrf_tovpn_sid_per_af(bgp_vpn, bgp_vrf, afi);
	delete_vrf_tovpn_sid_per_vrf(bgp_vpn, bgp_vrf);
}

/* 【函数】transpose_sid —— 把 label 的高 len 位写入 sid 中从 offset(以 MSB 为 0)起
 * 的位置；len_max 为 label 的有效位宽。逐位操作：先把目标位清零，再按 label 的对应
 * 位设置。注意 offset 以“从最高位起算”计，写入时换算成字节下标 tidx/8 与位 7-(tidx%8)。 */
/*
 * This function embeds upper `len` bits of `label` in `sid`,
 * starting at offset `offset` as seen from the MSB of `sid`.
 *
 * e.g. Given that `label` is 0x12345 and `len` is 16,
 * then `label` will be embedded in `sid` as follows:
 *
 *                 <----   len  ----->
 *         label:  0001 0002 0003 0004 0005
 *         sid:    .... 0001 0002 0003 0004
 *                      <----   len  ----->
 *                    ^
 *                    |
 *                 offset from MSB
 *
 * e.g. Given that `label` is 0x12345 and `len` is 8,
 * `label` will be embedded in `sid` as follows:
 *
 *                 <- len ->
 *         label:  0001 0002 0003 0004 0005
 *         sid:    .... 0001 0002 0000 0000
 *                      <- len ->
 *                    ^
 *                    |
 *                 offset from MSB
 */
void transpose_sid(struct in6_addr *sid, uint32_t label, uint8_t offset, uint8_t len,
		   uint8_t len_max)
{
	for (uint8_t idx = 0; idx < len; idx++) {
		uint8_t tidx = offset + idx;
		sid->s6_addr[tidx / 8] &= ~(0x1 << (7 - tidx % 8));
		if (label >> (len_max - 1 - idx) & 0x1)
			sid->s6_addr[tidx / 8] |= 0x1 << (7 - tidx % 8);
	}
}

/* 【函数】leak_update_nexthop_valid —— 判断一条即将泄漏的路径其下一跳是否“有效”。
 * 参数：to_bgp 目标实例；bn 目标节点；source_bpi 源路径；bpi 目标路径(可空)；p 前缀；
 *       bgp_orig 源实例。返回 true 表示下一跳可达/有效，可置 BGP_PATH_VALID。
 * 关键分支：
 *   - 源实例 VRF 不存在或回环口 down ⇒ 无效。
 *   - 重分发/Zebra 静态(network 命令)/聚合/EVPN/accepted-own 等特例：不走 NHT 或按标志定。
 *   - 其余情况用 bgp_find_or_add_nexthop() 做下一跳跟踪(NHT)。
 *   - 若启用了 SRv6 locator，还要再叠加 is_pi_srv6_valid() 的 SID 有效性判断。
 * 易错点：这里对“重分发/静态/accepted-own”等直接判定有效，绕开了常规 NHT，是历史特例。 */
static bool leak_update_nexthop_valid(struct bgp *to_bgp, struct bgp_dest *bn,
				      struct attr *new_attr, afi_t afi,
				      safi_t safi,
				      struct bgp_path_info *source_bpi,
				      struct bgp_path_info *bpi,
				      struct bgp *bgp_orig,
				      const struct prefix *p, int debug)
{
	struct bgp_path_info *bpi_ultimate;
	struct bgp *bgp_nexthop;
	struct bgp_table *table;
	struct interface *ifp;
	bool nh_valid;

	bpi_ultimate = bgp_get_imported_bpi_ultimate(source_bpi);
	table = bgp_dest_table(bpi_ultimate->net);

	if (bpi->extra && bpi->extra->vrfleak && bpi->extra->vrfleak->bgp_orig)
		bgp_nexthop = bpi->extra->vrfleak->bgp_orig;
	else
		bgp_nexthop = bgp_orig;

	/* The nexthop is invalid if its VRF does not exist */
	if (bgp_nexthop->vrf_id == VRF_UNKNOWN)
		return false;

	/* The nexthop is invalid if its VRF interface is down*/
	ifp = if_get_vrf_loopback(bgp_nexthop->vrf_id);
	if (ifp && !if_is_up(ifp))
		return false;

	/*
	 * No nexthop tracking for redistributed routes, for
	 * EVPN-imported routes that get leaked, or for routes
	 * leaked between VRFs with accept-own community.
	 */
	if (bpi_ultimate->sub_type == BGP_ROUTE_REDISTRIBUTE ||
	    is_pi_family_evpn(bpi_ultimate) ||
	    CHECK_FLAG(bpi_ultimate->flags, BGP_PATH_ACCEPT_OWN))
		nh_valid = true;
	else if (bpi_ultimate->type == ZEBRA_ROUTE_BGP &&
		 bpi_ultimate->sub_type == BGP_ROUTE_STATIC && table &&
		 (table->safi == SAFI_UNICAST ||
		  table->safi == SAFI_LABELED_UNICAST)) {
		/* the route is defined with the "network <prefix>" command */

		if (CHECK_FLAG(bgp_nexthop->flags, BGP_FLAG_IMPORT_CHECK))
			nh_valid = bgp_find_or_add_nexthop(to_bgp, bgp_nexthop, afi, SAFI_UNICAST,
							   bpi_ultimate, NULL, 0, p, bpi_ultimate);
		else
			/* if "no bgp network import-check" is set,
			 * then mark the nexthop as valid.
			 */
			nh_valid = true;
	} else if (bpi_ultimate->type == ZEBRA_ROUTE_BGP &&
		   bpi_ultimate->sub_type == BGP_ROUTE_AGGREGATE) {
		nh_valid = true;
	} else
		/*
		 * TBD do we need to do anything about the
		 * 'connected' parameter?
		 */
		/* VPN paths: the new bpi may be altered like
		 * with 'nexthop vpn export' command. Use the bpi_ultimate
		 * to find the original nexthop
		 */
		nh_valid = bgp_find_or_add_nexthop(to_bgp, bgp_nexthop, afi, safi, bpi, NULL, 0, p,
						   bpi_ultimate);

	/*
	 * If you are using SRv6 VPN instead of MPLS, it need to check
	 * the SID allocation. If the sid is not allocated, the rib
	 * will be invalid.
	 * If the SID per VRF is not available, also consider the rib as
	 * invalid.
	 */
	if ((bgp_srv6_locator_is_configured(to_bgp) ||
	     bgp_srv6_locator_is_configured(bgp_nexthop)) &&
	    nh_valid)
		nh_valid = is_pi_srv6_valid(bpi, bgp_nexthop, afi, safi);

	if (debug)
		zlog_debug("%s: %pFX nexthop is %svalid (in %s)", __func__, p,
			   (nh_valid ? "" : "not "), bgp_nexthop->name_pretty);

	return nh_valid;
}

/* 【函数】leak_update —— 泄漏核心：把 source_bpi 以 new_attr/标签的形式写入目标实例
 * to_bgp 的某前缀节点 bn，存在则更新、不存在则新建一条 BGP_ROUTE_IMPORTED 路径。
 * 参数：new_attr 已 interned 的属性(本函数接管其引用)；source_bpi 源路径；label/num_labels
 *       标签栈；bgp_orig 源实例；nexthop_orig 保存的原始下一跳；nexthop_self_flag 是否自设下一跳。
 * 返回：成功时返回目标 bgp_path_info*；无需变更/失败返回 NULL（NULL 时调用方负责 unlock bn）。
 * 调用时机：vpn_leak_from_vrf_update / vpn_leak_to_vrf_update_onevrf 等泄漏路径。
 * 关键分支与易错点：
 *   - 先用 vrfleak->parent == source_bpi 在 bn 上找已存在的导入项；找到则比对
 *     (标签是否相同 + attr 是否相同 + 下一跳有效性与现存 VALID 位是否一致)，全同则
 *     释放 new_attr 并返回 NULL（“无变化”）。
 *   - attr 变化时若 RT 扩展团体变了，会先把旧路径从各 VRF 隐式撤回。
 *   - 更新路径时：标记 ATTR_CHANGED、替换 labels、按需置 ANNC_NH_SELF/ACCEPT_OWN、
 *     重算 VALID 位，最后 bgp_process 触发下沉。
 *   - 新建路径时：info_make(...BGP_ROUTE_IMPORTED...)，建立 vrfleak 关系(parent/bgp_orig/
 *     nexthop_orig)、对 source 做引用计数(bgp_path_info_lock + bgp_dest_lock_node + bgp_lock)。
 *   - SRv6 与 MPLS 路径互不覆盖（见循环里的 continue）。
 *   - 注意 bn 的 lock/unlock：本函数某些返回路径已经对称 unlock，调用方不要再重复处理。 */
/*
 * returns pointer to new bgp_path_info upon success
 */
static struct bgp_path_info *
leak_update(struct bgp *to_bgp, struct bgp_dest *bn,
	    struct attr *new_attr, /* already interned */
	    afi_t afi, safi_t safi, struct bgp_path_info *source_bpi,
	    mpls_label_t *label, uint8_t num_labels, struct bgp *bgp_orig,
	    struct prefix *nexthop_orig, int nexthop_self_flag, int debug)
{
	const struct prefix *p = bgp_dest_get_prefix(bn);
	struct bgp_path_info *bpi;
	struct bgp_path_info *new;
	struct bgp_path_info_extra *extra;
	struct bgp_labels bgp_labels = {};
	struct bgp *bgp_nexthop;
	bool labelssame;
	uint8_t i;

	if (debug)
		zlog_debug(
			"%s: entry: leak-to=%s, p=%pBD, type=%d, sub_type=%d",
			__func__, to_bgp->name_pretty, bn, source_bpi->type,
			source_bpi->sub_type);

	/*
	 * Routes that are redistributed into BGP from zebra do not get
	 * nexthop tracking, unless MPLS allocation per nexthop is
	 * performed. In the default case nexthop tracking does not apply,
	 * if those routes are subsequently imported to other RIBs within
	 * BGP, the leaked routes do not carry the original
	 * BGP_ROUTE_REDISTRIBUTE sub_type. Therefore, in order to determine
	 * if the route we are currently leaking should have nexthop
	 * tracking, we must find the ultimate parent so we can check its
	 * sub_type.
	 *
	 * As of now, source_bpi may at most be a second-generation route
	 * (only one hop back to ultimate parent for vrf-vpn-vrf scheme).
	 * Using a loop here supports more complex intra-bgp import-export
	 * schemes that could be implemented in the future.
	 *
	 */

	/* 【匹配已有导入项】在 bn 上找“由 source_bpi 派生”的那条路径(vrfleak->parent 相等)。
	 * 循环内还跳过 SRv6/MPLS 互相覆盖的情况，找不到则 bpi 为 NULL 走新建分支。 */
	/*
	 * match parent
	 */
	for (bpi = bgp_dest_get_bgp_path_info(bn); bpi; bpi = bpi->next) {
		if (!bpi->extra || !bpi->extra->vrfleak ||
		    bpi->extra->vrfleak->parent != source_bpi)
			continue;
		if (new_attr->srv6_l3service && !bpi->attr->srv6_l3service &&
		    !bgp_labels_is_implicit_null(bpi))
			/* SRv6 path can not overwrite MPLS path */
			continue;
		if ((!new_attr->srv6_l3service && num_labels == 1 &&
		     decode_label(&label[0]) != MPLS_LABEL_NONE) &&
		    bpi->attr->srv6_l3service)
			/* MPLS path can not overwrite Srv6 path */
			continue;
		/* MPLS or SRv6 path found or path with LABEL_NONE*/
		break;
	}

	bgp_labels.num_labels = num_labels;
	for (i = 0; i < num_labels; i++) {
		bgp_labels.label[i] = label[i];
		bgp_set_valid_label(&bgp_labels.label[i]);
	}

	if (bpi) {
		labelssame = bgp_path_info_labels_same(bpi, bgp_labels.label,
						       bgp_labels.num_labels);

		bgp_nexthop = bpi->extra->vrfleak->bgp_orig ?: bgp_orig;
		if (bgp_nexthop->vrf_id == VRF_UNKNOWN) {
			if (debug) {
				zlog_debug("%s: ->%s(s_flags: 0x%x b_flags: 0x%x): %pFX: Found route, origin VRF does not exist, not leaking",
					   __func__, to_bgp->name_pretty, source_bpi->flags,
					   bpi->flags, p);
			}
			return NULL;
		}

		if (CHECK_FLAG(source_bpi->flags, BGP_PATH_REMOVED)
		    && CHECK_FLAG(bpi->flags, BGP_PATH_REMOVED)) {
			if (debug) {
				zlog_debug(
					"%s: ->%s(s_flags: 0x%x b_flags: 0x%x): %pFX: Found route, being removed, not leaking",
					__func__, to_bgp->name_pretty,
					source_bpi->flags, bpi->flags, p);
			}
			return NULL;
		}

		/* 【无变化判定】标签相同 + 未在删除 + 属性相同 + 下一跳有效性与现存一致
		 * ⇒ 无需更新，释放新属性并返回 NULL。 */
		if (labelssame && !CHECK_FLAG(bpi->flags, BGP_PATH_REMOVED) &&
		    attrhash_cmp(bpi->attr, new_attr) &&
		    leak_update_nexthop_valid(to_bgp, bn, new_attr, afi, safi, source_bpi, bpi,
					      bgp_orig, p,
					      debug) == !!CHECK_FLAG(bpi->flags, BGP_PATH_VALID)) {
			bgp_attr_unintern(&new_attr);
			if (debug)
				zlog_debug(
					"%s: ->%s: %pBD: Found route, no change",
					__func__, to_bgp->name_pretty, bn);
			return NULL;
		}

		/* If the RT was changed via extended communities as an
		 * import/export list, we should withdraw implicitly the old
		 * path from VRFs.
		 * For instance, RT list was modified using route-maps:
		 * route-map test permit 10
		 *   set extcommunity rt none
		 */
		if (bgp_attr_exists(bpi->attr, BGP_ATTR_EXT_COMMUNITIES) &&
		    bgp_attr_exists(new_attr, BGP_ATTR_EXT_COMMUNITIES)) {
			if (!ecommunity_cmp(
				    bgp_attr_get_ecommunity(bpi->attr),
				    bgp_attr_get_ecommunity(new_attr))) {
				vpn_leak_to_vrf_withdraw(bpi);
				bgp_aggregate_decrement(to_bgp, p, bpi, afi,
							safi);
				bgp_path_info_mark_for_delete(bn, bpi);
			}
		}

		/* attr is changed */
		bgp_path_info_set_flag(bn, bpi, BGP_PATH_ATTR_CHANGED);

		/* Rewrite BGP route information. */
		if (CHECK_FLAG(bpi->flags, BGP_PATH_REMOVED))
			bgp_path_info_restore(bn, bpi);
		else
			bgp_aggregate_decrement(to_bgp, p, bpi, afi, safi);
		bgp_attr_unintern(&bpi->attr);
		bpi->attr = new_attr;
		bpi->uptime = monotime(NULL);

		/*
		 * update labels
		 */
		if (!labelssame) {
			bgp_path_info_extra_get(bpi);
			bgp_labels_unintern(&bpi->extra->labels);
			bpi->extra->labels = bgp_labels_intern(&bgp_labels);
		}

		if (nexthop_self_flag)
			bgp_path_info_set_flag(bn, bpi, BGP_PATH_ANNC_NH_SELF);

		if (CHECK_FLAG(source_bpi->flags, BGP_PATH_ACCEPT_OWN))
			bgp_path_info_set_flag(bn, bpi, BGP_PATH_ACCEPT_OWN);

		if (leak_update_nexthop_valid(to_bgp, bn, new_attr, afi, safi,
					      source_bpi, bpi, bgp_orig, p,
					      debug))
			bgp_path_info_set_flag(bn, bpi, BGP_PATH_VALID);
		else
			bgp_path_info_unset_flag(bn, bpi, BGP_PATH_VALID);

		/* Process change. */
		bgp_aggregate_increment(to_bgp, p, bpi, afi, safi);
		bgp_process(to_bgp, bn, bpi, afi, safi);

		if (debug)
			zlog_debug("%s: ->%s: %pBD Found route, changed attr",
				   __func__, to_bgp->name_pretty, bn);

		bgp_dest_unlock_node(bn);

		return bpi;
	}

	if (CHECK_FLAG(source_bpi->flags, BGP_PATH_REMOVED)) {
		if (debug) {
			zlog_debug(
				"%s: ->%s(s_flags: 0x%x): %pFX: New route, being removed, not leaking",
				__func__, to_bgp->name_pretty,
				source_bpi->flags, p);
		}
		return NULL;
	}

	if (bgp_orig->vrf_id == VRF_UNKNOWN) {
		if (debug) {
			zlog_debug("%s: ->%s(s_flags: 0x%x): %pFX: New route, origin VRF does not exist, not leaking",
				   __func__, to_bgp->name_pretty, source_bpi->flags, p);
		}
		return NULL;
	}

	/* 【新建导入项】以 BGP_ROUTE_IMPORTED 子类型建路径，peer 用 peer_self(本地)，
	 * 随后建立 vrfleak 反向关系并对 source 引用计数。 */
	new = info_make(ZEBRA_ROUTE_BGP, BGP_ROUTE_IMPORTED, 0,
			to_bgp->peer_self, new_attr, bn);

	bgp_path_info_extra_get(new);
	if (!new->extra->vrfleak)
		new->extra->vrfleak =
			XCALLOC(MTYPE_BGP_ROUTE_EXTRA_VRFLEAK,
				sizeof(struct bgp_path_info_extra_vrfleak));

	if (source_bpi->peer) {
		extra = bgp_path_info_extra_get(new);
		extra->vrfleak->peer_orig = peer_lock(source_bpi->peer);
	}

	if (nexthop_self_flag)
		bgp_path_info_set_flag(bn, new, BGP_PATH_ANNC_NH_SELF);

	if (CHECK_FLAG(source_bpi->flags, BGP_PATH_ACCEPT_OWN))
		bgp_path_info_set_flag(bn, new, BGP_PATH_ACCEPT_OWN);

	if (bgp_labels.num_labels)
		new->extra->labels = bgp_labels_intern(&bgp_labels);

	new->extra->vrfleak->parent = bgp_path_info_lock(source_bpi);
	bgp_dest_lock_node((struct bgp_dest *)source_bpi->net);

	new->extra->vrfleak->bgp_orig = bgp_lock(bgp_orig);

	if (nexthop_orig)
		new->extra->vrfleak->nexthop_orig = *nexthop_orig;

	if (leak_update_nexthop_valid(to_bgp, bn, new_attr, afi, safi,
				      source_bpi, new, bgp_orig, p, debug))
		bgp_path_info_set_flag(bn, new, BGP_PATH_VALID);
	else
		bgp_path_info_unset_flag(bn, new, BGP_PATH_VALID);

	bgp_path_info_add(bn, new);
	bgp_aggregate_increment(to_bgp, p, new, afi, safi);

	bgp_process(to_bgp, bn, new, afi, safi);

	if (debug)
		zlog_debug("%s: ->%s: %pBD: Added new route", __func__,
			   to_bgp->name_pretty, bn);

	bgp_dest_unlock_node(bn);

	return new;
}

/* 【函数】bgp_mplsvpn_path_nh_label_unlink —— 把一条路径从其 per-nexthop 标签缓存
 * (blnc) 的 paths 链表上解绑：LIST_REMOVE、递减 path_count、清 pi 上的
 * BGP_PATH_MPLSVPN_LABEL_NH 标志；若缓存已空则释放它。pi 为空或未绑定则直接返回。 */
void bgp_mplsvpn_path_nh_label_unlink(struct bgp_path_info *pi)
{
	struct bgp_label_per_nexthop_cache *blnc;

	if (!pi)
		return;

	if (!CHECK_FLAG(pi->flags, BGP_PATH_MPLSVPN_LABEL_NH))
		return;

	blnc = pi->mplsvpn.blnc.label_nexthop_cache;

	if (!blnc)
		return;

	LIST_REMOVE(pi, mplsvpn.blnc.label_nh_thread);
	pi->mplsvpn.blnc.label_nexthop_cache->path_count--;
	pi->mplsvpn.blnc.label_nexthop_cache = NULL;
	UNSET_FLAG(pi->flags, BGP_PATH_MPLSVPN_LABEL_NH);

	if (LIST_EMPTY(&(blnc->paths)))
		bgp_label_per_nexthop_free(blnc);
}

/* 【函数】bgp_mplsvpn_get_label_per_nexthop_cb —— zebra 回复“per-nexthop 出标签”
 * 的异步回调(context=blnc)。allocated=false 表示老标签失效(置 MPLS_INVALID_LABEL)。
 * 若标签真的变了：向 zebra 下发 <label → nexthop> 的 MPLS 绑定(ADD)，然后遍历该
 * 缓存下的所有路径重新泄漏(vpn_leak_from_vrf_update)，使导出的标签得以刷新。
 * 注意：label 未变化时直接返回，避免无谓的重复刷新。 */
/* Called upon reception of a ZAPI Message from zebra, about
 * a new available label.
 */
static int bgp_mplsvpn_get_label_per_nexthop_cb(mpls_label_t label,
						void *context, bool allocated)
{
	struct bgp_label_per_nexthop_cache *blnc = context;
	mpls_label_t old_label;
	int debug = BGP_DEBUG(vpn, VPN_LEAK_LABEL);
	struct bgp_path_info *pi;
	struct bgp_table *table;

	old_label = blnc->label;

	if (debug)
		zlog_debug("%s: label=%u, allocated=%d, nexthop=%pFX", __func__,
			   label, allocated, &blnc->nexthop);
	if (allocated)
		/* update the entry with the new label */
		blnc->label = label;
	else
		/*
		 * previously-allocated label is now invalid
		 * eg: zebra deallocated the labels and notifies it
		 */
		blnc->label = MPLS_INVALID_LABEL;

	if (old_label == blnc->label)
		return 0; /* no change */

	/* update paths */
	if (blnc->label != MPLS_INVALID_LABEL)
		bgp_zebra_send_nexthop_label(ZEBRA_MPLS_LABELS_ADD, blnc->label,
					     blnc->nh->ifindex,
					     blnc->nh->vrf_id, ZEBRA_LSP_BGP,
					     &blnc->nexthop, 0, NULL);

	LIST_FOREACH (pi, &(blnc->paths), mplsvpn.blnc.label_nh_thread) {
		if (!pi->net)
			continue;
		table = bgp_dest_table(pi->net);
		if (!table)
			continue;
		vpn_leak_from_vrf_update(blnc->to_bgp, table->bgp, pi);
	}

	return 0;
}

/* 【函数】_vpn_leak_from_vrf_get_per_nexthop_label —— 取/申请某路径下一跳对应的
 * per-nexthop 标签。逻辑：
 *   - 从 pi->nexthop(BNC) 提取下一跳地址(区分递归/直连)，构造成 prefix。
 *   - 在 from_bgp->mpls_labels_per_nexthop[afi] 树中查找 blnc；没有则新建并向
 *     标签池申请标签(bgp_lp_get → 异步回调 bgp_mplsvpn_get_label_per_nexthop_cb)。
 *   - 把 pi 绑定到 blnc(必要时先解绑旧的)，同步 nexthop；
 *   - 首次返回的 label 可能是 MPLS_INVALID_LABEL(申请中)，调用方据此推迟导出。 */
/* Get a per label nexthop value:
 *  - Find and return a per label nexthop from the cache
 *  - else allocate a new per label nexthop cache entry and request a
 *    label to zebra. Return MPLS_INVALID_LABEL
 */
static mpls_label_t
_vpn_leak_from_vrf_get_per_nexthop_label(struct bgp_path_info *pi,
					 struct bgp *to_bgp,
					 struct bgp *from_bgp, afi_t afi)
{
	struct bgp_nexthop_cache *bnc = pi->nexthop;
	struct bgp_label_per_nexthop_cache *blnc;
	struct bgp_label_per_nexthop_cache_head *tree;
	struct prefix *nh_pfx = NULL;
	struct prefix nh_gate = {0};

	/* extract the nexthop from the BNC nexthop cache */
	switch (bnc->nexthop->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		/* the nexthop is recursive */
		nh_gate.family = AF_INET;
		nh_gate.prefixlen = IPV4_MAX_BITLEN;
		IPV4_ADDR_COPY(&nh_gate.u.prefix4, &bnc->nexthop->gate.ipv4);
		nh_pfx = &nh_gate;
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		/* the nexthop is recursive */
		nh_gate.family = AF_INET6;
		nh_gate.prefixlen = IPV6_MAX_BITLEN;
		IPV6_ADDR_COPY(&nh_gate.u.prefix6, &bnc->nexthop->gate.ipv6);
		nh_pfx = &nh_gate;
		break;
	case NEXTHOP_TYPE_IFINDEX:
		/* the nexthop is directly connected */
		nh_pfx = &bnc->prefix;
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		assert(!"Blackhole nexthop. Already checked by the caller.");
	}

	/* find or allocate a nexthop label cache entry */
	tree = &from_bgp->mpls_labels_per_nexthop[family2afi(nh_pfx->family)];
	blnc = bgp_label_per_nexthop_find(tree, nh_pfx);
	if (!blnc) {
		blnc = bgp_label_per_nexthop_new(tree, nh_pfx);
		blnc->to_bgp = to_bgp;
		/* request a label to zebra for this nexthop
		 * the response from zebra will trigger the callback
		 */
		bgp_lp_get(LP_TYPE_NEXTHOP, blnc, from_bgp->vrf_id,
			   bgp_mplsvpn_get_label_per_nexthop_cb);
	}

	if (pi->mplsvpn.blnc.label_nexthop_cache == blnc)
		/* no change */
		return blnc->label;

	/* Unlink from any existing nexthop cache. Free the entry if unused.
	 */
	bgp_mplsvpn_path_nh_label_unlink(pi);

	/* updates NHT pi list reference */
	LIST_INSERT_HEAD(&(blnc->paths), pi, mplsvpn.blnc.label_nh_thread);
	pi->mplsvpn.blnc.label_nexthop_cache = blnc;
	pi->mplsvpn.blnc.label_nexthop_cache->path_count++;
	SET_FLAG(pi->flags, BGP_PATH_MPLSVPN_LABEL_NH);
	blnc->last_update = monotime(NULL);

	/* then add or update the selected nexthop */
	if (!blnc->nh)
		blnc->nh = nexthop_dup(bnc->nexthop, NULL);
	else if (!nexthop_same(bnc->nexthop, blnc->nh)) {
		nexthop_free(blnc->nh);
		blnc->nh = nexthop_dup(bnc->nexthop, NULL);
		if (blnc->label != MPLS_INVALID_LABEL) {
			bgp_zebra_send_nexthop_label(
				ZEBRA_MPLS_LABELS_REPLACE, blnc->label,
				bnc->nexthop->ifindex, bnc->nexthop->vrf_id,
				ZEBRA_LSP_BGP, &blnc->nexthop, 0, NULL);
		}
	}

	return blnc->label;
}

/* 【函数】bgp_mplsvpn_get_vpn_label —— 取该 VRF/策略当前的“per-VRF 出口标签”。
 * 若为 auto 模式且尚未分配，则发起异步申请(bgp_lp_get→vpn_leak_label_callback)并
 * 返回 MPLS_INVALID_LABEL(调用方需等待回调)；否则返回已配置的 tovpn_label。 */
static mpls_label_t bgp_mplsvpn_get_vpn_label(struct vpn_policy *bgp_policy)
{
	if (bgp_policy->tovpn_label == MPLS_LABEL_NONE &&
	    CHECK_FLAG(bgp_policy->flags, BGP_VPN_POLICY_TOVPN_LABEL_AUTO)) {
		bgp_lp_get(LP_TYPE_VRF, bgp_policy, bgp_policy->bgp->vrf_id,
			   vpn_leak_label_callback);
		return MPLS_INVALID_LABEL;
	}
	return bgp_policy->tovpn_label;
}

/* 【函数】vpn_leak_from_vrf_get_per_nexthop_label —— per-nexthop 标签模式的“守门”：
 * 先排除所有不支持 per-nexthop 的场景（非法下一跳、IPV4/IPV6 无法用/为 any/非单播、
 * ECMP/黑洞、network 静态等），这些一律回退到 per-VRF 标签(bgp_mplsvpn_get_vpn_label)；
 * 下一跳无效时返回 MPLS_INVALID_LABEL(由回调稍后重试)；否则转入
 * _vpn_leak_from_vrf_get_per_nexthop_label() 真正取/申请 per-nexthop 标签。 */
/* Filter out all the cases where a per nexthop label is not possible:
 * - return an invalid label when the nexthop is invalid
 * - return the per VRF label when the per nexthop label is not supported
 * Otherwise, find or request a per label nexthop.
 */
static mpls_label_t
vpn_leak_from_vrf_get_per_nexthop_label(afi_t afi, struct bgp_path_info *pi,
					struct bgp *from_bgp,
					struct bgp *to_bgp)
{
	struct bgp_path_info *bpi_ultimate = bgp_get_imported_bpi_ultimate(pi);
	struct bgp *bgp_nexthop = NULL;
	bool nh_valid;
	afi_t nh_afi;
	bool is_bgp_static_route;

	is_bgp_static_route = bpi_ultimate->sub_type == BGP_ROUTE_STATIC &&
			      bpi_ultimate->type == ZEBRA_ROUTE_BGP;

	if (is_bgp_static_route == false && afi == AFI_IP &&
	    bgp_attr_exists(pi->attr, BGP_ATTR_NEXT_HOP) &&
	    (pi->attr->nexthop.s_addr == INADDR_ANY || !ipv4_unicast_valid(&pi->attr->nexthop))) {
		/* IPv4 nexthop in standard BGP encoding format.
		 * Format of address is not valid (not any, not unicast).
		 * Fallback to the per VRF label.
		 */
		bgp_mplsvpn_path_nh_label_unlink(pi);
		return bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);
	}

	if (is_bgp_static_route == false && afi == AFI_IP &&
	    pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4 &&
	    (pi->attr->mp_nexthop_global_in.s_addr == INADDR_ANY ||
	     !ipv4_unicast_valid(&pi->attr->mp_nexthop_global_in))) {
		/* IPv4 nexthop is in MP-BGP encoding format.
		 * Format of address is not valid (not any, not unicast).
		 * Fallback to the per VRF label.
		 */
		bgp_mplsvpn_path_nh_label_unlink(pi);
		return bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);
	}

	if (is_bgp_static_route == false && afi == AFI_IP6 &&
	    (pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL ||
	     pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL) &&
	    (IN6_IS_ADDR_UNSPECIFIED(&pi->attr->mp_nexthop_global) ||
	     IN6_IS_ADDR_LOOPBACK(&pi->attr->mp_nexthop_global) ||
	     IN6_IS_ADDR_MULTICAST(&pi->attr->mp_nexthop_global))) {
		/* IPv6 nexthop is in MP-BGP encoding format.
		 * Format of address is not valid
		 * Fallback to the per VRF label.
		 */
		bgp_mplsvpn_path_nh_label_unlink(pi);
		return bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);
	}

	/* Check the next-hop reachability.
	 * Get the bgp instance where the bgp_path_info originates.
	 */
	if (pi->extra && pi->extra->vrfleak && pi->extra->vrfleak->bgp_orig)
		bgp_nexthop = pi->extra->vrfleak->bgp_orig;
	else
		bgp_nexthop = from_bgp;

	nh_afi = BGP_ATTR_NH_AFI(afi, pi->attr);
	nh_valid = bgp_find_or_add_nexthop(from_bgp, bgp_nexthop, nh_afi, SAFI_UNICAST, pi, NULL, 0,
					   NULL, NULL);

	if (!nh_valid && is_bgp_static_route &&
	    !CHECK_FLAG(from_bgp->flags, BGP_FLAG_IMPORT_CHECK)) {
		/* "network" prefixes not routable, but since 'no bgp network
		 * import-check' is configured, they are always valid in the BGP
		 * table. Fallback to the per-vrf label
		 */
		bgp_mplsvpn_path_nh_label_unlink(pi);
		return bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);
	}

	if (!nh_valid || !pi->nexthop || pi->nexthop->nexthop_num == 0 ||
	    !pi->nexthop->nexthop) {
		/* invalid next-hop:
		 * do not send the per-vrf label
		 * otherwise, when the next-hop becomes valid,
		 * we will have 2 BGP updates:
		 * - one with the per-vrf label
		 * - the second with the per-nexthop label
		 */
		bgp_mplsvpn_path_nh_label_unlink(pi);
		return MPLS_INVALID_LABEL;
	}

	if (pi->nexthop->nexthop_num > 1 ||
	    pi->nexthop->nexthop->type == NEXTHOP_TYPE_BLACKHOLE) {
		/* Blackhole or ECMP routes
		 * is not compatible with per-nexthop label.
		 * Fallback to per-vrf label.
		 */
		bgp_mplsvpn_path_nh_label_unlink(pi);
		return bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);
	}

	if (is_bgp_static_route && pi->nexthop->nexthop->type == NEXTHOP_TYPE_IFINDEX) {
		/* "network" imported prefixes from vrf
		 * fallback to per-vrf label.
		 */

		bgp_mplsvpn_path_nh_label_unlink(pi);
		return bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);
	}

	return _vpn_leak_from_vrf_get_per_nexthop_label(pi, to_bgp, from_bgp,
							afi);
}

/* 【函数】vpn_leak_from_vrf_fill_srv6 —— 若该 VRF 配置了 SRv6 VPN(per-AFI 或 per-VRF
 * 的 locator+SID)，则为导出属性填充 SRv6 L3 Service 部分，并把派生出的“标签”
 * (SID transposition 得到的 20bit 值)写入 *label。
 * 返回 true 表示已填 SRv6(应走 SRv6 导出)，false 表示未配置 SRv6。
 * 关键点：把 SID 的 block/node/func/arg 位宽、transposition 偏移与端点行为(END.DT4/6/46
 * 及其 USID 变体)记入 attr->srv6_l3service；若 func 位宽超过 20bit 则不使用 label
 * 承载 FUNC(transposition_len/offset 置 0，直接写整段 SID)。 */
static bool vpn_leak_from_vrf_fill_srv6(struct attr *attr, struct bgp *from_bgp, afi_t afi,
					mpls_label_t *label)
{
	/* Set SID for SRv6 VPN */
	if (from_bgp->vpn_policy[afi].tovpn_sid_locator && from_bgp->vpn_policy[afi].tovpn_sid) {
		struct srv6_locator *locator = from_bgp->vpn_policy[afi].tovpn_sid_locator;

		encode_label(from_bgp->vpn_policy[afi].tovpn_sid_transpose_label, label);
		attr->srv6_l3service = XCALLOC(MTYPE_BGP_SRV6_L3SERVICE,
					       sizeof(struct bgp_attr_srv6_l3service));
		attr->srv6_l3service->sid_flags = 0x00;
		attr->srv6_l3service->endpoint_behavior =
			afi == AFI_IP ? (CHECK_FLAG(locator->flags, SRV6_LOCATOR_USID)
						 ? SRV6_ENDPOINT_BEHAVIOR_END_DT4_USID
						 : SRV6_ENDPOINT_BEHAVIOR_END_DT4)
				      : (CHECK_FLAG(locator->flags, SRV6_LOCATOR_USID)
						 ? SRV6_ENDPOINT_BEHAVIOR_END_DT6_USID
						 : SRV6_ENDPOINT_BEHAVIOR_END_DT6);
		attr->srv6_l3service->loc_block_len =
			from_bgp->vpn_policy[afi].tovpn_sid_locator->block_bits_length;
		attr->srv6_l3service->loc_node_len =
			from_bgp->vpn_policy[afi].tovpn_sid_locator->node_bits_length;
		if (CHECK_FLAG(from_bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_FUNC_WIDE))
			attr->srv6_l3service->func_len =
				BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
		else
			attr->srv6_l3service->func_len =
				from_bgp->vpn_policy[afi].tovpn_sid_locator->function_bits_length;
		attr->srv6_l3service->arg_len =
			from_bgp->vpn_policy[afi].tovpn_sid_locator->argument_bits_length;
		if (attr->srv6_l3service->func_len >
		    BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL) {
			attr->srv6_l3service->transposition_len = 0;
			attr->srv6_l3service->transposition_offset = 0;
			IPV6_ADDR_COPY(&attr->srv6_l3service->sid,
				       from_bgp->vpn_policy[afi].tovpn_sid);
		} else {
			attr->srv6_l3service->transposition_len =
				from_bgp->vpn_policy[afi].tovpn_sid_locator->function_bits_length;
			attr->srv6_l3service->transposition_offset =
				from_bgp->vpn_policy[afi].tovpn_sid_locator->block_bits_length +
				from_bgp->vpn_policy[afi].tovpn_sid_locator->node_bits_length;
			IPV6_ADDR_COPY(&attr->srv6_l3service->sid,
				       &from_bgp->vpn_policy[afi].tovpn_sid_locator->prefix.prefix);
		}
		return true;
	}
	if (from_bgp->tovpn_sid_locator && from_bgp->tovpn_sid) {
		struct srv6_locator *locator = from_bgp->tovpn_sid_locator;

		encode_label(from_bgp->tovpn_sid_transpose_label, label);
		attr->srv6_l3service = XCALLOC(MTYPE_BGP_SRV6_L3SERVICE,
					       sizeof(struct bgp_attr_srv6_l3service));
		attr->srv6_l3service->sid_flags = 0x00;
		attr->srv6_l3service->endpoint_behavior =
			CHECK_FLAG(locator->flags, SRV6_LOCATOR_USID)
				? SRV6_ENDPOINT_BEHAVIOR_END_DT46_USID
				: SRV6_ENDPOINT_BEHAVIOR_END_DT46;
		attr->srv6_l3service->loc_block_len =
			from_bgp->tovpn_sid_locator->block_bits_length;
		attr->srv6_l3service->loc_node_len = from_bgp->tovpn_sid_locator->node_bits_length;
		if (CHECK_FLAG(from_bgp->vrf_flags, BGP_VRF_TOVPN_SID_FUNC_WIDE))
			attr->srv6_l3service->func_len =
				BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_BGP;
		else
			attr->srv6_l3service->func_len =
				from_bgp->tovpn_sid_locator->function_bits_length;
		attr->srv6_l3service->arg_len = from_bgp->tovpn_sid_locator->argument_bits_length;
		if (attr->srv6_l3service->func_len >
		    BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL) {
			attr->srv6_l3service->transposition_len = 0;
			attr->srv6_l3service->transposition_offset = 0;
			IPV6_ADDR_COPY(&attr->srv6_l3service->sid, from_bgp->tovpn_sid);
		} else {
			attr->srv6_l3service->transposition_len =
				from_bgp->tovpn_sid_locator->function_bits_length;
			attr->srv6_l3service->transposition_offset =
				from_bgp->tovpn_sid_locator->block_bits_length +
				from_bgp->tovpn_sid_locator->node_bits_length;
			IPV6_ADDR_COPY(&attr->srv6_l3service->sid,
				       &from_bgp->tovpn_sid_locator->prefix.prefix);
		}
		return true;
	}
	return false;
}

/* 【函数】_vpn_leak_from_vrf_update_leak_attr —— VRF→VPN 导出的“落库”步骤：
 * 把 static_attr intern 后写入 VPN 表(以 from_bgp 的 tovpn_rd 为 RD)，调用 leak_update
 * 完成插入/更新；成功后还要把该 VPN 路由再“回灌”给所有 VRF(vpn_leak_to_vrf_update)，
 * 因为它现在也源自 VPN 表了（能否被各 VRF 接受仍取决于 RT 交集）。 */
static void _vpn_leak_from_vrf_update_leak_attr(struct attr *static_attr, struct bgp *to_bgp,
						struct bgp *from_bgp, afi_t afi, safi_t safi,
						struct bgp_path_info *path_vrf,
						int nexthop_self_flag, int debug,
						mpls_label_t *label)
{
	struct attr *new_attr;
	struct bgp_dest *bn;
	const struct prefix *p = bgp_dest_get_prefix(path_vrf->net);
	struct bgp_path_info *new_info;

	new_attr = bgp_attr_intern(static_attr); /* hashed refcounted everything */
	bn = bgp_afi_node_get(to_bgp->rib[afi][safi], afi, safi, p,
			      &(from_bgp->vpn_policy[afi].tovpn_rd));
	new_info = leak_update(to_bgp, bn, new_attr, afi, safi, path_vrf, label, 1, from_bgp, NULL,
			       nexthop_self_flag, debug);
	/*
	 * Routes actually installed in the vpn RIB must also be
	 * offered to all vrfs (because now they originate from
	 * the vpn RIB).
	 *
	 * Acceptance into other vrfs depends on rt-lists.
	 * Originating vrf will not accept the looped back route
	 * because of loop checking.
	 */
	if (new_info)
		vpn_leak_to_vrf_update(from_bgp, new_info, NULL, path_vrf->peer);
	else
		bgp_dest_unlock_node(bn);
}

/* cf vnc_import_bgp_add_route_mode_nvegroup() and add_vnc_route() */
/* 【函数】vpn_leak_from_vrf_update —— VRF(或默认实例)→ VPN 表的单条路由导出入口。
 * 参数：to_bgp 目标(通常是默认/VPN 实例)；from_bgp 源 VRF；path_vrf 待导出的源路径。
 * 调用时机：VRF 路由产生/变化(路由下发、policy 变更、标签回调等)时由各处调用。
 * 处理流程(顺序重要)：
 *   1. 取 afi，检查是否可导出(is_route_injectable_into_vpn)、VPN 导出是否激活；
 *   2. 聚合抑制检查；shallow copy 属性；合并/剥离 RT(VRF-2-VRF 时先剥 RT)；
 *   3. 跑 tovpn route-map(可能 DENY)；校验必须含 RT；
 *   4. 下一跳策略(vpn export nexthop 或自动 mp_nexthop)；取出口标签
 *      (per-nexthop 或 per-VRF；MPLS_LABEL_NONE 时用 implicit-null)；
 *   5. 置 originator-id 为自身；分别按 MPLS / SRv6 调用 _leak_attr 落库。
 * 易错点：标签为 MPLS_INVALID_LABEL(申请中)时提前返回并 flush 临时属性，等回调重来；
 *   末尾务必 bgp_attr_flush(&static_attr) 释放本地临时申请的部分。 */
void vpn_leak_from_vrf_update(struct bgp *to_bgp,	     /* to */
			      struct bgp *from_bgp,	   /* from */
			      struct bgp_path_info *path_vrf) /* route */
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	const struct prefix *p = bgp_dest_get_prefix(path_vrf->net);
	afi_t afi = family2afi(p->family);
	struct attr static_attr = { 0 };
	safi_t safi = SAFI_MPLS_VPN;
	mpls_label_t label_val = { 0 };
	mpls_label_t label = { 0 };
	const char *debugmsg;
	int nexthop_self_flag = 0;
	struct ecommunity *old_ecom;
	struct ecommunity *new_ecom = NULL;
	struct ecommunity *rtlist_ecom;

	if (debug)
		zlog_debug("%s: from vrf %s", __func__, from_bgp->name_pretty);

	if (debug && bgp_attr_get_ecommunity(path_vrf->attr)) {
		char *s = ecommunity_ecom2str(
			bgp_attr_get_ecommunity(path_vrf->attr),
			ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		zlog_debug("%s: %s path_vrf->type=%d, EC{%s}", __func__,
			   from_bgp->name, path_vrf->type, s);
		XFREE(MTYPE_ECOMMUNITY_STR, s);
	}

	if (!to_bgp)
		return;

	if (!afi) {
		if (debug)
			zlog_debug("%s: can't get afi of prefix", __func__);
		return;
	}

	/* Is this route exportable into the VPN table? */
	if (!is_route_injectable_into_vpn(path_vrf))
		return;

	if (!vpn_leak_to_vpn_active(from_bgp, afi, &debugmsg, false)) {
		if (debug)
			zlog_debug("%s: %s skipping: %s", __func__,
				   from_bgp->name, debugmsg);
		return;
	}

	/* Aggregate-address suppress check. */
	if (bgp_path_suppressed(path_vrf)) {
		if (debug)
			zlog_debug("%s: %s skipping: suppressed path will not be exported",
				   __func__, from_bgp->name);
		return;
	}

	/* shallow copy */
	static_attr = *path_vrf->attr;

	if (debug && bgp_attr_get_ecommunity(&static_attr)) {
		char *s = ecommunity_ecom2str(
			bgp_attr_get_ecommunity(&static_attr),
			ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		zlog_debug("%s: post route map static_attr.ecommunity{%s}",
			   __func__, s);
		XFREE(MTYPE_ECOMMUNITY_STR, s);
	}

	/*
	 * Add the vpn-policy rt-list
	 */

	/* Export with the 'from' instance's export RTs. */
	/* If doing VRF-to-VRF leaking, strip existing RTs first. */
	old_ecom = bgp_attr_get_ecommunity(&static_attr);
	rtlist_ecom = from_bgp->vpn_policy[afi].rtlist[BGP_VPN_POLICY_DIR_TOVPN];
	if (old_ecom) {
		new_ecom = ecommunity_dup(old_ecom);
		if (CHECK_FLAG(from_bgp->af_flags[afi][SAFI_UNICAST],
			       BGP_CONFIG_VRF_TO_VRF_EXPORT))
			ecommunity_strip_rts(new_ecom);
		if (rtlist_ecom)
			new_ecom = ecommunity_merge(new_ecom, rtlist_ecom);
		if (!old_ecom->refcnt)
			ecommunity_free(&old_ecom);
	} else if (rtlist_ecom) {
		new_ecom = ecommunity_dup(rtlist_ecom);
	} else {
		new_ecom = NULL;
	}

	bgp_attr_set_ecommunity(&static_attr, new_ecom);

	/*
	 * route map handling
	 */
	if (from_bgp->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_TOVPN]) {
		struct bgp_path_info info;
		route_map_result_t ret;

		memset(&info, 0, sizeof(info));
		info.peer = path_vrf->peer ? path_vrf->peer : to_bgp->peer_self;
		info.attr = &static_attr;
		ret = route_map_apply(from_bgp->vpn_policy[afi]
					      .rmap[BGP_VPN_POLICY_DIR_TOVPN],
				      p, &info);
		if (RMAP_DENYMATCH == ret) {
			bgp_attr_flush(&static_attr); /* free any added parts */
			if (debug)
				zlog_debug("%s: vrf %s route map \"%s\" says DENY, returning",
					   __func__, from_bgp->name_pretty,
					   from_bgp->vpn_policy[afi]
						   .rmap[BGP_VPN_POLICY_DIR_TOVPN]
						   ->name);
			return;
		}
	}

	new_ecom = bgp_attr_get_ecommunity(&static_attr);
	if (!ecommunity_has_route_target(new_ecom)) {
		ecommunity_free(&new_ecom);
		if (debug)
			zlog_debug("%s: %s skipping: waiting for a valid export rt list.",
				   __func__, from_bgp->name_pretty);
		return;
	}

	if (debug && bgp_attr_get_ecommunity(&static_attr)) {
		char *s = ecommunity_ecom2str(
			bgp_attr_get_ecommunity(&static_attr),
			ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		zlog_debug("%s: post merge static_attr.ecommunity{%s}",
			   __func__, s);
		XFREE(MTYPE_ECOMMUNITY_STR, s);
	}

	community_strip_accept_own(&static_attr);

	/* Nexthop */
	/* if policy nexthop not set, use 0 */
	if (CHECK_FLAG(from_bgp->vpn_policy[afi].flags,
		       BGP_VPN_POLICY_TOVPN_NEXTHOP_SET)) {
		struct prefix *nexthop =
			&from_bgp->vpn_policy[afi].tovpn_nexthop;

		switch (nexthop->family) {
		case AF_INET:
			/* prevent mp_nexthop_global_in <- self in bgp_route.c
			 */
			static_attr.nexthop.s_addr = nexthop->u.prefix4.s_addr;

			static_attr.mp_nexthop_global_in = nexthop->u.prefix4;
			static_attr.mp_nexthop_len = BGP_ATTR_NHLEN_IPV4;
			break;

		case AF_INET6:
			static_attr.mp_nexthop_global = nexthop->u.prefix6;
			static_attr.mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL;
			break;

		default:
			assert(0);
		}
	} else {
		if (!CHECK_FLAG(from_bgp->af_flags[afi][SAFI_UNICAST],
				BGP_CONFIG_VRF_TO_VRF_EXPORT)) {
			if (afi == AFI_IP &&
			    !BGP_ATTR_NEXTHOP_AFI_IP6(path_vrf->attr)) {
				/*
				 * For ipv4, copy to multiprotocol
				 * nexthop field
				 */
				static_attr.mp_nexthop_global_in =
					static_attr.nexthop;
				static_attr.mp_nexthop_len =
					BGP_ATTR_NHLEN_IPV4;
				/*
				 * XXX Leave static_attr.nexthop
				 * intact for NHT
				 */
				static_attr.flag &=
					~ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP);
			}
		} else {
			/* Update based on next-hop family to account for
			 * RFC 5549 (BGP unnumbered) scenario. Note that
			 * specific action is only needed for the case of
			 * IPv4 nexthops as the attr has been copied
			 * otherwise.
			 */
			if (afi == AFI_IP
			    && !BGP_ATTR_NEXTHOP_AFI_IP6(path_vrf->attr)) {
				static_attr.mp_nexthop_global_in.s_addr =
					static_attr.nexthop.s_addr;
				static_attr.mp_nexthop_len =
					BGP_ATTR_NHLEN_IPV4;
				static_attr.flag |=
					ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP);
			}
		}
		nexthop_self_flag = 1;
	}

	if (CHECK_FLAG(from_bgp->vpn_policy[afi].flags,
		       BGP_VPN_POLICY_TOVPN_LABEL_PER_NEXTHOP))
		/* per nexthop label mode */
		label_val = vpn_leak_from_vrf_get_per_nexthop_label(
			afi, path_vrf, from_bgp, to_bgp);
	else
		label_val =
			bgp_mplsvpn_get_vpn_label(&from_bgp->vpn_policy[afi]);

	if (label_val == MPLS_INVALID_LABEL) {
		/* no valid label for the moment
		 * when the 'bgp_mplsvpn_get_label_per_nexthop_cb' callback gets
		 * a valid label value, it will call the current function again.
		 */
		if (debug)
			zlog_debug(
				"%s: %s skipping: waiting for a valid per-label nexthop.",
				__func__, from_bgp->name_pretty);
		bgp_attr_flush(&static_attr);
		return;
	}
	/* 【标签编码】label_val==NONE(未配置/回退)用 implicit-null；否则编码实际标签。 */
	if (label_val == MPLS_LABEL_NONE)
		encode_label(MPLS_LABEL_IMPLICIT_NULL, &label);
	else
		encode_label(label_val, &label);

	/* Set originator ID to "me" */
	SET_FLAG(static_attr.flag, ATTR_FLAG_BIT(BGP_ATTR_ORIGINATOR_ID));
	static_attr.originator_id = to_bgp->router_id;

	if (debug && bgp_attr_get_ecommunity(&static_attr)) {
		char *s = ecommunity_ecom2str(bgp_attr_get_ecommunity(&static_attr),
					      ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		zlog_debug("%s: static_attr.ecommunity{%s}", __func__, s);
		XFREE(MTYPE_ECOMMUNITY_STR, s);
	}

	if (label_val != MPLS_LABEL_NONE)
		/* MPLS */
		_vpn_leak_from_vrf_update_leak_attr(&static_attr, to_bgp, from_bgp, afi, safi,
						    path_vrf, nexthop_self_flag, debug, &label);

	if (vpn_leak_from_vrf_fill_srv6(&static_attr, from_bgp, afi, &label))

		/* SRv6 */
		_vpn_leak_from_vrf_update_leak_attr(&static_attr, to_bgp, from_bgp, afi, safi,
						    path_vrf, nexthop_self_flag, debug, &label);
	else if (label_val == MPLS_LABEL_NONE)
		/* import-vrf */
		_vpn_leak_from_vrf_update_leak_attr(&static_attr, to_bgp, from_bgp, afi, safi,
						    path_vrf, nexthop_self_flag, debug, &label);
	bgp_attr_flush(&static_attr); /* free locally-allocated parts */
}

/* 【函数】vpn_leak_from_vrf_withdraw —— VRF→VPN 的单条路由撤回：在 VPN 表中按
 * (前缀, from_bgp->tovpn_rd) 定位节点，遍历其上由 path_vrf 派生的导入项(parent==path_vrf)，
 * 逐条先 vpn_leak_to_vrf_withdraw(从各 VRF 撤)再标记删除并 bgp_process。
 * 仅导出仍处于激活态时才撤(此处用 ignore_export_rt_list=true 强制撤)。 */
void vpn_leak_from_vrf_withdraw(struct bgp *to_bgp,		/* to */
				struct bgp *from_bgp,		/* from */
				struct bgp_path_info *path_vrf) /* route */
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	const struct prefix *p = bgp_dest_get_prefix(path_vrf->net);
	afi_t afi = family2afi(p->family);
	safi_t safi = SAFI_MPLS_VPN;
	struct bgp_path_info *bpi, *bpi_next;
	struct bgp_dest *bn;
	const char *debugmsg;

	if (debug) {
		zlog_debug(
			"%s: entry: leak-from=%s, p=%pBD, type=%d, sub_type=%d",
			__func__, from_bgp->name_pretty, path_vrf->net,
			path_vrf->type, path_vrf->sub_type);
	}

	if (!to_bgp)
		return;

	if (!afi) {
		if (debug)
			zlog_debug("%s: can't get afi of prefix", __func__);
		return;
	}

	/* Is this route exportable into the VPN table? */
	if (!is_route_injectable_into_vpn(path_vrf))
		return;

	if (!vpn_leak_to_vpn_active(from_bgp, afi, &debugmsg, true)) {
		if (debug)
			zlog_debug("%s: skipping: %s", __func__, debugmsg);
		return;
	}

	bn = bgp_safi_node_lookup(to_bgp->rib[afi][safi], safi, p,
				  &(from_bgp->vpn_policy[afi].tovpn_rd));

	if (!bn)
		return;
	if (debug)
		zlog_debug("%s: withdrawing (path_vrf=%p)", __func__, path_vrf);

	/*
	 * vrf -> vpn
	 * match original bpi imported from
	 */
	bpi_next = bgp_dest_get_bgp_path_info(bn);
	while (bpi_next) {
		bpi = bpi_next;
		bpi_next = bpi->next;
		if (!bpi->extra || !bpi->extra->vrfleak || bpi->extra->vrfleak->parent != path_vrf)
			continue;
		if (path_vrf->attr->srv6_l3service && !bpi->attr->srv6_l3service &&
		    !bgp_labels_is_implicit_null(bpi))
			/* SRv6 path can not overwrite MPLS path */
			continue;
		if ((!path_vrf->attr->srv6_l3service && BGP_PATH_INFO_NUM_LABELS(path_vrf) == 1 &&
		     decode_label(&path_vrf->extra->labels->label[0]) != MPLS_LABEL_NONE) &&
		    bpi->attr->srv6_l3service)
			/* MPLS path can not overwrite Srv6 path */
			continue;
		/* MPLS or SRv6 path found or path with LABEL_NONE*/
		/* withdraw from looped vrfs as well */
		vpn_leak_to_vrf_withdraw(bpi);

		bgp_aggregate_decrement(to_bgp, p, bpi, afi, safi);
		bgp_path_info_mark_for_delete(bn, bpi);
		bgp_process(to_bgp, bn, bpi, afi, safi);
	}
	bgp_dest_unlock_node(bn);
}

/* 【函数】vpn_leak_from_vrf_withdraw_all —— 批量撤回 from_bgp 导出到 VPN 表的所有路由：
 * 遍历 VPN 表的 per-RD 子表，删除 bgp_orig==from_bgp 的 BGP_ROUTE_IMPORTED 项
 * （同时从各 VRF 撤回、并解绑其 per-nexthop 标签）。 */
void vpn_leak_from_vrf_withdraw_all(struct bgp *to_bgp, struct bgp *from_bgp,
				    afi_t afi)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);
	struct bgp_dest *pdest;
	safi_t safi = SAFI_MPLS_VPN;

	/*
	 * Walk vpn table, delete bpi with bgp_orig == from_bgp
	 */
	for (pdest = bgp_table_top(to_bgp->rib[afi][safi]); pdest;
	     pdest = bgp_route_next(pdest)) {

		struct bgp_table *table;
		struct bgp_dest *bn;
		struct bgp_path_info *bpi, *next;

		/* This is the per-RD table of prefixes */
		table = bgp_dest_get_bgp_table_info(pdest);

		if (!table)
			continue;

		for (bn = bgp_table_top(table); bn; bn = bgp_route_next(bn)) {
			bpi = bgp_dest_get_bgp_path_info(bn);
			if (debug && bpi) {
				zlog_debug("%s: looking at prefix %pBD",
					   __func__, bn);
			}

			for (; (bpi != NULL) && (next = bpi->next, 1);
			     bpi = next) {
				if (debug)
					zlog_debug("%s: type %d, sub_type %d",
						   __func__, bpi->type,
						   bpi->sub_type);
				if (bpi->sub_type != BGP_ROUTE_IMPORTED)
					continue;
				if (!bpi->extra || !bpi->extra->vrfleak)
					continue;
				if ((struct bgp *)bpi->extra->vrfleak->bgp_orig ==
				    from_bgp) {
					/* delete route */
					if (debug)
						zlog_debug("%s: deleting it",
							   __func__);
					/* withdraw from leak-to vrfs as well */
					vpn_leak_to_vrf_withdraw(bpi);
					bgp_aggregate_decrement(
						to_bgp, bgp_dest_get_prefix(bn),
						bpi, afi, safi);
					bgp_path_info_mark_for_delete(bn, bpi);
					bgp_process(to_bgp, bn, bpi, afi, safi);
					bgp_mplsvpn_path_nh_label_unlink(
						bpi->extra->vrfleak->parent);
				}
			}
		}
	}
}

/* 【函数】vpn_leak_from_vrf_update_all —— 把 from_bgp 单播表中的所有路径重新导出到
 * VPN 表（postchange：如 RT/label 变化后全量刷新）。 */
void vpn_leak_from_vrf_update_all(struct bgp *to_bgp, struct bgp *from_bgp,
				  afi_t afi)
{
	struct bgp_dest *bn;
	struct bgp_path_info *bpi;
	int debug = BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF);

	if (debug)
		zlog_debug("%s: entry, afi=%d, vrf=%s", __func__, afi,
			   from_bgp->name_pretty);

	for (bn = bgp_table_top(from_bgp->rib[afi][SAFI_UNICAST]); bn;
	     bn = bgp_route_next(bn)) {

		if (debug)
			zlog_debug("%s: node=%p", __func__, bn);

		for (bpi = bgp_dest_get_bgp_path_info(bn); bpi;
		     bpi = bpi->next) {
			if (debug)
				zlog_debug(
					"%s: calling vpn_leak_from_vrf_update",
					__func__);
			vpn_leak_from_vrf_update(to_bgp, from_bgp, bpi);
		}
	}
}

/* 【函数】bgp_lookup_by_rd —— 依据 RD 值在配置中反查“源 VRF”实例。
 * 仅对带 ACCEPT_OWN 的路径有意义：遍历所有 VRF 实例，找其 vpn_policy[afi].tovpn_rd
 * 与给定 rd 相同(逐字节 memcmp)的那个。用于 VRF-2-VRF 泄漏时确定来源 VRF。 */
static struct bgp *bgp_lookup_by_rd(struct bgp_path_info *bpi,
				    struct prefix_rd *rd, afi_t afi)
{
	struct listnode *node, *nnode;
	struct bgp *bgp;

	if (!rd)
		return NULL;

	/* If ACCEPT_OWN is not enabled for this path - return. */
	if (!CHECK_FLAG(bpi->flags, BGP_PATH_ACCEPT_OWN))
		return NULL;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		if (bgp->inst_type != BGP_INSTANCE_TYPE_VRF)
			continue;

		if (!CHECK_FLAG(bgp->vpn_policy[afi].flags,
				BGP_VPN_POLICY_TOVPN_RD_SET))
			continue;

		/* Check if we have source VRF by RD value */
		if (memcmp(&bgp->vpn_policy[afi].tovpn_rd.val, rd->val,
			   ECOMMUNITY_SIZE) == 0)
			return bgp;
	}

	return NULL;
}

/* 【函数】vpn_leak_to_vrf_update_onevrf —— VPN 表 → 单个目标 VRF 的导入核心。
 * 参数：to_bgp 目标 VRF；from_bgp VPN 实例；path_vpn 源 VPN 路径；prd 其 RD；from 来源 peer。
 * 返回：无。调用时机：vpn_leak_to_vrf_update 遍历所有 VRF 时对每个目标调用。
 * 主要步骤：
 *   1. 确认目标可导入(vpn_leak_from_vpn_active)；确定源 VRF(src_vrf：优先 vrfleak->bgp_orig，
 *      否则 ACCEPT_OWN 反查 src_bgp，再否则 from_bgp)；
 *   2. RT 交集检查(ecommunity_include)，未命中直接跳过；
 *   3. accepted-own 防回环：RD 与目标自身 RD 相同则拒绝导入；
 *   4. allowas-in 处理：若 AS_PATH 中已有本 AS 且超出允许次数，则撤销已导入项并返回；
 *   5. shallow copy 属性，VRF-2-VRF 时剥离 RT；
 *   6. 预判下一跳有效性设 NH_VALID；暂存并清零下一跳(改写成 “me”)，按需把 nh_ifindex
 *      改写为目标 VRF 回环口；
 *   7. 跑 fromvpn route-map；按需补 AS_PATH(源 VRF 的 AS)以助回环检测；
 *   8. intern 属性；决定是否带标签(来源为本地单播则不带标签)；leak_update 落库。
 * 易错点：标签拷贝的 origin_local 特例——若是本机另一 VRF 原创(未过 VPN)，走 IP 转发无 LSP，
 *   故导入 VRF 的这条不应挂标签；这也影响 NHT 对非标签下一跳的判定。 */
static void vpn_leak_to_vrf_update_onevrf(struct bgp *to_bgp,	/* to */
					  struct bgp *from_bgp, /* from */
					  struct bgp_path_info *path_vpn, struct prefix_rd *prd,
					  struct peer *from)
{
	const struct prefix *p = bgp_dest_get_prefix(path_vpn->net);
	afi_t afi = family2afi(p->family);

	struct attr static_attr = {0};
	struct attr *new_attr = NULL;
	struct bgp_dest *bn;
	safi_t safi = SAFI_UNICAST;
	const char *debugmsg;
	struct prefix nexthop_orig;
	mpls_label_t *label_pnt = NULL;
	uint8_t num_labels = 0;
	int nexthop_self_flag = 1;
	struct bgp_path_info *bpi_ultimate = NULL;
	struct bgp_path_info *bpi;
	int origin_local = 0;
	struct bgp *src_vrf;
	struct interface *ifp = NULL;
	char rd_buf[RD_ADDRSTRLEN];
	struct aspath *new_aspath;
	int32_t aspath_loop_count = 0;
	struct peer *peer = path_vpn->peer;

	int debug = BGP_DEBUG(vpn, VPN_LEAK_TO_VRF);

	if (!vpn_leak_from_vpn_active(to_bgp, afi, &debugmsg)) {
		if (debug)
			zlog_debug(
				"%s: from vpn (%s) to vrf (%s), skipping: %s",
				__func__, from_bgp->name_pretty,
				to_bgp->name_pretty, debugmsg);
		return;
	}

	/*
	 * For VRF-2-VRF route-leaking,
	 * the source will be the originating VRF.
	 *
	 * If ACCEPT_OWN mechanism is enabled, then we SHOULD(?)
	 * get the source VRF (BGP) by looking at the RD.
	 */
	struct bgp *src_bgp = bgp_lookup_by_rd(path_vpn, prd, afi);

	if (path_vpn->extra && path_vpn->extra->vrfleak &&
	    path_vpn->extra->vrfleak->bgp_orig)
		src_vrf = path_vpn->extra->vrfleak->bgp_orig;
	else if (src_bgp)
		src_vrf = src_bgp;
	else
		src_vrf = from_bgp;

	/* Check for intersection of route targets */
	if (!ecommunity_include(
		    to_bgp->vpn_policy[afi].rtlist[BGP_VPN_POLICY_DIR_FROMVPN],
		    bgp_attr_get_ecommunity(path_vpn->attr))) {
		if (debug)
			zlog_debug(
				"from vpn (%s) to vrf (%s), skipping after no intersection of route targets",
				from_bgp->name_pretty, to_bgp->name_pretty);
		return;
	}

	rd_buf[0] = '\0';
	if (debug && prd)
		prefix_rd2str(prd, rd_buf, sizeof(rd_buf), to_bgp->asnotation);

	/* A route MUST NOT ever be accepted back into its source VRF, even if
	 * it carries one or more RTs that match that VRF.
	 */
	if (CHECK_FLAG(path_vpn->flags, BGP_PATH_ACCEPT_OWN) && prd &&
	    memcmp(&prd->val, &to_bgp->vpn_policy[afi].tovpn_rd.val,
		   ECOMMUNITY_SIZE) == 0) {
		if (debug)
			zlog_debug(
				"%s: skipping import, match RD (%s) of src VRF (%s) and the prefix (%pFX)",
				__func__, rd_buf, to_bgp->name_pretty, p);
		return;
	}

	bn = bgp_afi_node_get(to_bgp->rib[afi][safi], afi, safi, p, NULL);

	/* Check if leaked route has our asn. If so, don't import it. */
	if (CHECK_FLAG(peer->af_flags[afi][SAFI_MPLS_VPN], PEER_FLAG_ALLOWAS_IN))
		aspath_loop_count = peer->allowas_in[afi][SAFI_MPLS_VPN];
	else if (peer == peer->bgp->peer_self && from)
		/* If this is an import from one VRF to another and the source
		 * VRF's peer has allowas-in applied, respect it.
		 */
		aspath_loop_count = from->allowas_in[afi][SAFI_UNICAST];

	if (aspath_loop_check(path_vpn->attr->aspath, to_bgp->as) > aspath_loop_count) {
		for (bpi = bgp_dest_get_bgp_path_info(bn); bpi;
		     bpi = bpi->next) {
			if (!bpi->extra || !bpi->extra->vrfleak ||
			    bpi->extra->vrfleak->parent != path_vpn)
				continue;
			if (path_vpn->attr->srv6_l3service && !bpi->attr->srv6_l3service &&
			    !bgp_labels_is_implicit_null(bpi))
				/* SRv6 path can not overwrite MPLS path */
				continue;
			if ((!path_vpn->attr->srv6_l3service &&
			     BGP_PATH_INFO_NUM_LABELS(path_vpn) == 1 &&
			     decode_label(&path_vpn->extra->labels->label[0]) != MPLS_LABEL_NONE) &&
			    bpi->attr->srv6_l3service)
				/* MPLS path can not overwrite Srv6 path */
				continue;
			/* MPLS or SRv6 path found or path with LABEL_NONE*/

			if (debug)
				zlog_debug("%s: blocking import of %p, as-path match",
					   __func__, bpi);
			bgp_aggregate_decrement(to_bgp, p, bpi, afi, safi);
			bgp_path_info_mark_for_delete(bn, bpi);
			bgp_process(to_bgp, bn, bpi, afi, safi);
		}
		bgp_dest_unlock_node(bn);
		return;
	}

	if (debug)
		zlog_debug("%s: updating RD %s, %pFX to %s", __func__, rd_buf,
			   p, to_bgp->name_pretty);

	/* shallow copy */
	static_attr = *path_vpn->attr;

	struct ecommunity *old_ecom;
	struct ecommunity *new_ecom;

	/* If doing VRF-to-VRF leaking, strip RTs. */
	old_ecom = bgp_attr_get_ecommunity(&static_attr);
	if (old_ecom && CHECK_FLAG(to_bgp->af_flags[afi][safi],
				   BGP_CONFIG_VRF_TO_VRF_IMPORT)) {
		new_ecom = ecommunity_dup(old_ecom);
		ecommunity_strip_rts(new_ecom);
		bgp_attr_set_ecommunity(&static_attr, new_ecom);

		if (new_ecom->size == 0) {
			ecommunity_free(&new_ecom);
			bgp_attr_set_ecommunity(&static_attr, NULL);
		}

		if (!old_ecom->refcnt)
			ecommunity_free(&old_ecom);
	}

	community_strip_accept_own(&static_attr);

	bn = bgp_afi_node_get(to_bgp->rib[afi][safi], afi, safi, p, NULL);

	for (bpi = bgp_dest_get_bgp_path_info(bn); bpi; bpi = bpi->next) {
		if (bpi->extra && bpi->extra->vrfleak &&
		    bpi->extra->vrfleak->parent == path_vpn)
			break;
	}

	if (bpi && leak_update_nexthop_valid(to_bgp, bn, &static_attr, afi, safi, path_vpn, bpi,
					     src_vrf, p, debug))
		SET_FLAG(static_attr.nh_flags, BGP_ATTR_NH_VALID);
	else
		UNSET_FLAG(static_attr.nh_flags, BGP_ATTR_NH_VALID);

	/* 【下一跳处理】VPN 里的下一跳在核心网有效、但在目标 VRF 里无意义：先暂存原始
	 * 下一跳(nexthop_orig)供后续按标签解析，再把通告用下一跳改写成 0(=me)。 */
	/*
	 * Nexthop: stash and clear
	 *
	 * Nexthop is valid in context of VPN core, but not in destination vrf.
	 * Stash it for later label resolution by vrf ingress path and then
	 * overwrite with 0, i.e., "me", for the sake of vrf advertisement.
	 */
	uint8_t nhfamily = NEXTHOP_FAMILY(path_vpn->attr->mp_nexthop_len);

	memset(&nexthop_orig, 0, sizeof(nexthop_orig));
	nexthop_orig.family = nhfamily;

	/* If the path has accept-own community and the source VRF
	 * is valid, reset next-hop to self, to allow importing own
	 * routes between different VRFs on the same node.
	 */

	if (src_bgp)
		subgroup_announce_reset_nhop(nhfamily, &static_attr);

	bpi_ultimate = bgp_get_imported_bpi_ultimate(path_vpn);

	/* The nh ifindex may not be defined (when the route is
	 * imported from the network statement => BGP_ROUTE_STATIC)
	 * or to the real interface.
	 * Rewrite the nh ifindex to VRF's interface.
	 * Let the kernel to decide with double lookup the real next-hop
	 * interface when installing the route.
	 */
	if (src_vrf->vrf_id != VRF_DEFAULT &&
	    (src_bgp || bpi_ultimate->sub_type == BGP_ROUTE_STATIC ||
	     bpi_ultimate->sub_type == BGP_ROUTE_REDISTRIBUTE)) {
		ifp = if_get_vrf_loopback(src_vrf->vrf_id);
		if (ifp)
			static_attr.nh_ifindex = ifp->ifindex;
	}

	switch (nhfamily) {
	case AF_INET:
		/* save */
		nexthop_orig.u.prefix4 = path_vpn->attr->mp_nexthop_global_in;
		nexthop_orig.prefixlen = IPV4_MAX_BITLEN;

		if (CHECK_FLAG(to_bgp->af_flags[afi][safi],
			       BGP_CONFIG_VRF_TO_VRF_IMPORT)) {
			static_attr.nexthop.s_addr =
				nexthop_orig.u.prefix4.s_addr;

			static_attr.mp_nexthop_global_in =
				path_vpn->attr->mp_nexthop_global_in;
			static_attr.mp_nexthop_len =
				path_vpn->attr->mp_nexthop_len;
		}
		static_attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP);
		break;
	case AF_INET6:
		/* save */
		nexthop_orig.u.prefix6 = path_vpn->attr->mp_nexthop_global;
		nexthop_orig.prefixlen = IPV6_MAX_BITLEN;

		if (CHECK_FLAG(to_bgp->af_flags[afi][safi],
			       BGP_CONFIG_VRF_TO_VRF_IMPORT)) {
			static_attr.mp_nexthop_global = nexthop_orig.u.prefix6;
		}
		break;
	}

	if (!ifp && static_attr.nh_ifindex)
		ifp = if_lookup_by_index(static_attr.nh_ifindex,
					 src_vrf->vrf_id);

	if (ifp && if_is_operative(ifp))
		SET_FLAG(static_attr.nh_flags, BGP_ATTR_NH_IF_OPERSTATE);
	else
		UNSET_FLAG(static_attr.nh_flags, BGP_ATTR_NH_IF_OPERSTATE);

	/*
	 * route map handling
	 */
	if (to_bgp->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_FROMVPN]) {
		struct bgp_path_info info;
		route_map_result_t ret;

		memset(&info, 0, sizeof(info));
		info.peer = from ? from : to_bgp->peer_self;
		info.attr = &static_attr;
		info.extra = path_vpn->extra; /* Used for source-vrf filter */
		info.net = path_vpn->net;     /* Used to detect afi and safi */
		ret = route_map_apply(to_bgp->vpn_policy[afi]
					      .rmap[BGP_VPN_POLICY_DIR_FROMVPN],
				      p, &info);
		if (RMAP_DENYMATCH == ret) {
			bgp_attr_flush(&static_attr); /* free any added parts */
			if (debug)
				zlog_debug(
					"%s: vrf %s vpn-policy route map \"%s\" says DENY, returning",
					__func__, to_bgp->name_pretty,
					to_bgp->vpn_policy[afi]
						.rmap[BGP_VPN_POLICY_DIR_FROMVPN]
						->name);
			return;
		}
		/*
		 * if route-map changed nexthop, don't nexthop-self on output
		 */
		if (!CHECK_FLAG(static_attr.rmap_change_flags,
						BATTR_RMAP_NEXTHOP_UNCHANGED))
			nexthop_self_flag = 0;
	}

	/*
	 * if the asn values are different, copy the asn of the source vrf
	 * into the entry before importing. This helps with as-path loop
	 * detection
	 */
	if (path_vpn->extra && path_vpn->extra->vrfleak &&
	    path_vpn->extra->vrfleak->bgp_orig &&
	    (to_bgp->as != path_vpn->extra->vrfleak->bgp_orig->as)) {
		new_aspath = aspath_dup(static_attr.aspath);
		new_aspath =
			aspath_add_seq(new_aspath,
				       path_vpn->extra->vrfleak->bgp_orig->as);
		static_attr.aspath = new_aspath;
	}

	new_attr = bgp_attr_intern(&static_attr);
	bgp_attr_flush(&static_attr);

	/* 【标签拷贝策略】非 VRF-2-VRF 导入时：若源路径的本源是单播表(即本机另一 VRF 原创、
	 * 未经 VPN)，则走纯 IP 转发无 LSP，故导入 VRF 的这条不带标签(num_labels=0)；
	 * 否则按源 VPN 路径的标签数拷贝。 */
	/*
	 * ensure labels are copied
	 *
	 * However, there is a special case: if the route originated in
	 * another local VRF (as opposed to arriving via VPN), then the
	 * nexthop is reached by hairpinning through this router (me)
	 * using IP forwarding only (no LSP). Therefore, the route
	 * imported to the VRF should not have labels attached. Note
	 * that nexthop tracking is also involved: eliminating the
	 * labels for these routes enables the non-labeled nexthops
	 * from the originating VRF to be considered valid for this route.
	 */
	if (!CHECK_FLAG(to_bgp->af_flags[afi][safi],
			BGP_CONFIG_VRF_TO_VRF_IMPORT)) {
		/*
		 * if original route was unicast,
		 * then it did not arrive over vpn
		 */
		if (bpi_ultimate->net) {
			struct bgp_table *table;

			table = bgp_dest_table(bpi_ultimate->net);
			if (table && (table->safi == SAFI_UNICAST))
				origin_local = 1;
		}

		num_labels = origin_local ? 0
					  : BGP_PATH_INFO_NUM_LABELS(path_vpn);
		label_pnt = num_labels ? path_vpn->extra->labels->label : NULL;
	}

	if (debug)
		zlog_debug("%s: pfx %pBD: num_labels %d", __func__,
			   path_vpn->net, num_labels);

	if (!leak_update(to_bgp, bn, new_attr, afi, safi, path_vpn, label_pnt,
			 num_labels, src_vrf, &nexthop_orig, nexthop_self_flag,
			 debug))
		bgp_dest_unlock_node(bn);
}

/* 【函数】vpn_leak_to_vrf_no_retain_filter_check —— 判断某条 VPN 属性是否“无人需要”
 * （应当过滤掉、不保留）。遍历所有目标 VRF，只要有一个 VRF 可导入且 RT 命中，就
 * 返回 false(需要)；全部不命中则返回 true(可过滤)。用于 no-retain 优化：源实例删除时
 * 把没人要的 VPN 路由清理掉。 */
bool vpn_leak_to_vrf_no_retain_filter_check(struct bgp *from_bgp,
					    struct attr *attr, afi_t afi)
{
	struct ecommunity *ecom_route_target = bgp_attr_get_ecommunity(attr);
	int debug = BGP_DEBUG(vpn, VPN_LEAK_TO_VRF);
	struct listnode *node;
	const char *debugmsg;
	struct bgp *to_bgp;

	/* Loop over BGP instances */
	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, to_bgp)) {
		if (!vpn_leak_from_vpn_active(to_bgp, afi, &debugmsg)) {
			if (debug)
				zlog_debug(
					"%s: from vpn (%s) to vrf (%s) afi %s, skipping: %s",
					__func__, from_bgp->name_pretty,
					to_bgp->name_pretty, afi2str(afi),
					debugmsg);
			continue;
		}

		/* Check for intersection of route targets */
		if (!ecommunity_include(
			    to_bgp->vpn_policy[afi]
				    .rtlist[BGP_VPN_POLICY_DIR_FROMVPN],
			    ecom_route_target)) {
			if (debug)
				zlog_debug(
					"%s: from vpn (%s) to vrf (%s) afi %s %s, skipping after no intersection of route targets",
					__func__, from_bgp->name_pretty,
					to_bgp->name_pretty, afi2str(afi),
					ecommunity_str(ecom_route_target));
			continue;
		}
		return false;
	}

	if (debug)
		zlog_debug(
			"%s: from vpn (%s) afi %s %s, no import - must be filtered",
			__func__, from_bgp->name_pretty, afi2str(afi),
			ecommunity_str(ecom_route_target));

	return true;
}

/* 【函数】vpn_leak_to_vrf_update —— VPN 表路由变化时，向所有 VRF 扩散更新：
 * 遍历 bm->bgp 中的每个实例，跳过“正是其来源实例”(避免回环)，其余逐个调用
 * vpn_leak_to_vrf_update_onevrf 让它们按 RT 决定是否导入。prd/peer 透传。 */
void vpn_leak_to_vrf_update(struct bgp *from_bgp, struct bgp_path_info *path_vpn,
			    struct prefix_rd *prd, struct peer *peer)
{
	struct listnode *mnode, *mnnode;
	struct bgp *bgp;
	const struct prefix *p = bgp_dest_get_prefix(path_vpn->net);

	int debug = BGP_DEBUG(vpn, VPN_LEAK_TO_VRF);

	if (debug)
		zlog_debug("%s: start (path_vpn=%p, prefix=%pFX)", __func__, path_vpn, p);

	/* Loop over VRFs */
	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {
		if (!path_vpn->extra || !path_vpn->extra->vrfleak ||
		    path_vpn->extra->vrfleak->bgp_orig != bgp) { /* no loop */
			vpn_leak_to_vrf_update_onevrf(bgp, from_bgp, path_vpn, prd, peer);
		}
	}
}

/* 【函数】vpn_leak_to_vrf_withdraw —— VPN 路由被删除时，从所有导入了它的 VRF 中撤掉：
 * 遍历所有实例，对可导入且 RT 命中的 VRF，在其单播表中找到 parent==path_vpn 的导入项
 * 并标记删除+process。path_vpn->net 为空(特定 VNC 场景除外)则直接返回。 */
void vpn_leak_to_vrf_withdraw(struct bgp_path_info *path_vpn)
{
	const struct prefix *p;
	afi_t afi;
	safi_t safi = SAFI_UNICAST;
	struct bgp *bgp;
	struct listnode *mnode, *mnnode;
	struct bgp_dest *bn;
	struct bgp_path_info *bpi;
	const char *debugmsg;

	int debug = BGP_DEBUG(vpn, VPN_LEAK_TO_VRF);

	if (debug)
		zlog_debug("%s: entry: p=%pBD, type=%d, sub_type=%d", __func__,
			   path_vpn->net, path_vpn->type, path_vpn->sub_type);

	if (debug)
		zlog_debug("%s: start (path_vpn=%p)", __func__, path_vpn);

	if (!path_vpn->net) {
#ifdef ENABLE_BGP_VNC
		/* BGP_ROUTE_RFP routes do not have path_vpn->net set (yet) */
		if (path_vpn->type == ZEBRA_ROUTE_BGP
		    && path_vpn->sub_type == BGP_ROUTE_RFP) {

			return;
		}
#endif
		if (debug)
			zlog_debug(
				"%s: path_vpn->net unexpectedly NULL, no prefix, bailing",
				__func__);
		return;
	}

	p = bgp_dest_get_prefix(path_vpn->net);
	afi = family2afi(p->family);

	/* Loop over VRFs */
	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {
		if (!vpn_leak_from_vpn_active(bgp, afi, &debugmsg)) {
			if (debug)
				zlog_debug("%s: from %s, skipping: %s",
					   __func__, bgp->name_pretty,
					   debugmsg);
			continue;
		}

		/* Check for intersection of route targets */
		if (!ecommunity_include(
			    bgp->vpn_policy[afi]
				    .rtlist[BGP_VPN_POLICY_DIR_FROMVPN],
			    bgp_attr_get_ecommunity(path_vpn->attr))) {

			continue;
		}

		if (debug)
			zlog_debug("%s: withdrawing from vrf %s", __func__,
				   bgp->name_pretty);

		bn = bgp_afi_node_get(bgp->rib[afi][safi], afi, safi, p, NULL);

		for (bpi = bgp_dest_get_bgp_path_info(bn); bpi;
		     bpi = bpi->next) {
			if (bpi->extra && bpi->extra->vrfleak &&
			    (struct bgp_path_info *)bpi->extra->vrfleak->parent ==
				    path_vpn) {
				break;
			}
		}

		if (bpi) {
			if (debug)
				zlog_debug("%s: deleting bpi %p", __func__,
					   bpi);
			bgp_aggregate_decrement(bgp, p, bpi, afi, safi);
			bgp_path_info_mark_for_delete(bn, bpi);
			bgp_process(bgp, bn, bpi, afi, safi);
		}
		bgp_dest_unlock_node(bn);
	}
}

/* 【函数】vpn_leak_to_vrf_withdraw_all —— 清空某 VRF 中“来自 VPN”的所有导入项：
 * 遍历其单播表，删除 bgp_orig != to_bgp 且 parent 属于 VPN 族(is_pi_family_vpn)的路径。 */
void vpn_leak_to_vrf_withdraw_all(struct bgp *to_bgp, afi_t afi)
{
	struct bgp_dest *bn;
	struct bgp_path_info *bpi, *next;
	safi_t safi = SAFI_UNICAST;
	int debug = BGP_DEBUG(vpn, VPN_LEAK_TO_VRF);

	if (debug)
		zlog_debug("%s: entry", __func__);
	/*
	 * Walk vrf table, delete bpi with bgp_orig in a different vrf
	 */
	for (bn = bgp_table_top(to_bgp->rib[afi][safi]); bn;
	     bn = bgp_route_next(bn)) {
		for (bpi = bgp_dest_get_bgp_path_info(bn);
		     (bpi != NULL) && (next = bpi->next, 1); bpi = next) {
			if (bpi->extra && bpi->extra->vrfleak &&
			    bpi->extra->vrfleak->bgp_orig != to_bgp &&
			    bpi->extra->vrfleak->parent &&
			    is_pi_family_vpn(bpi->extra->vrfleak->parent)) {
				/* delete route */
				bgp_aggregate_decrement(to_bgp,
							bgp_dest_get_prefix(bn),
							bpi, afi, safi);
				bgp_path_info_mark_for_delete(bn, bpi);
				bgp_process(to_bgp, bn, bpi, afi, safi);
			}
		}
	}
}

/* 【函数】vpn_leak_no_retain —— no-retain 优化：遍历 VPN 表，把没有任何 VRF 需要
 * (no_retain_filter_check 为真)、且非本机原创(bgp_orig != to_bgp)的普通 BGP 路由
 * 从 VPN 表里移除(rib_remove)，以免在源 VRF 删除后留下无人认领的残留。 */
void vpn_leak_no_retain(struct bgp *to_bgp, struct bgp *vpn_from, afi_t afi)
{
	struct bgp_dest *pdest;
	safi_t safi = SAFI_MPLS_VPN;

	assert(vpn_from);

	/*
	 * Walk vpn table
	 */
	for (pdest = bgp_table_top(vpn_from->rib[afi][safi]); pdest;
	     pdest = bgp_route_next(pdest)) {
		struct bgp_table *table;
		struct bgp_dest *bn;
		struct bgp_path_info *bpi;

		/* This is the per-RD table of prefixes */
		table = bgp_dest_get_bgp_table_info(pdest);

		if (!table)
			continue;

		for (bn = bgp_table_top(table); bn; bn = bgp_route_next(bn)) {
			struct bgp_path_info *next;

			for (bpi = bgp_dest_get_bgp_path_info(bn);
			     (bpi != NULL) && (next = bpi->next, 1);
			     bpi = next) {
				if (bpi->extra && bpi->extra->vrfleak &&
				    bpi->extra->vrfleak->bgp_orig == to_bgp)
					continue;

				if (bpi->sub_type != BGP_ROUTE_NORMAL)
					continue;

				if (!vpn_leak_to_vrf_no_retain_filter_check(
					    vpn_from, bpi->attr, afi))
					/* do not filter */
					continue;

				bgp_unlink_nexthop(bpi);
				bgp_rib_remove(bn, bpi, bpi->peer, afi, safi);
			}
		}
	}
}

/* 【函数】vpn_leak_to_vrf_update_all —— postchange 全量导入：遍历 VPN 表所有路由，
 * 对每个非本机原创项调用 vpn_leak_to_vrf_update_onevrf 重新导入到 to_bgp。 */
void vpn_leak_to_vrf_update_all(struct bgp *to_bgp, struct bgp *vpn_from,
				afi_t afi)
{
	struct bgp_dest *pdest;
	safi_t safi = SAFI_MPLS_VPN;

	assert(vpn_from);

	/*
	 * Walk vpn table
	 */
	for (pdest = bgp_table_top(vpn_from->rib[afi][safi]); pdest;
	     pdest = bgp_route_next(pdest)) {
		struct bgp_table *table;
		struct bgp_dest *bn;
		struct bgp_path_info *bpi;

		/* This is the per-RD table of prefixes */
		table = bgp_dest_get_bgp_table_info(pdest);

		if (!table)
			continue;

		for (bn = bgp_table_top(table); bn; bn = bgp_route_next(bn)) {

			for (bpi = bgp_dest_get_bgp_path_info(bn); bpi;
			     bpi = bpi->next) {
				if (bpi->extra && bpi->extra->vrfleak &&
				    bpi->extra->vrfleak->bgp_orig == to_bgp)
					continue;

				vpn_leak_to_vrf_update_onevrf(to_bgp, vpn_from, bpi, NULL,
							      bpi->peer);
			}
		}
	}
}

/* 【函数】vpn_policy_routemap_update —— 路由映射被定义/删除/变更时的响应：
 * 对默认/VRF 实例，若 rmap_name 命中其 tovpn 或 fromvpn 的 policy 路由映射名，
 * 先 vpn_leak_prechange 撤销旧效果，再更新保存的 rmap 指针(可为 NULL 表示已删)，
 * 最后 vpn_leak_postchange 应用新效果。保证路由映射改动即时生效。 */
/*
 * This function is called for definition/deletion/change to a route-map
 */
static void vpn_policy_routemap_update(struct bgp *bgp, const char *rmap_name)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_RMAP_EVENT);
	afi_t afi;
	struct route_map *rmap;

	if (bgp->inst_type != BGP_INSTANCE_TYPE_DEFAULT
	    && bgp->inst_type != BGP_INSTANCE_TYPE_VRF) {

		return;
	}

	rmap = route_map_lookup_by_name(rmap_name); /* NULL if deleted */

	for (afi = 0; afi < AFI_MAX; ++afi) {

		if (bgp->vpn_policy[afi].rmap_name[BGP_VPN_POLICY_DIR_TOVPN]
			&& !strcmp(rmap_name,
			       bgp->vpn_policy[afi]
				       .rmap_name[BGP_VPN_POLICY_DIR_TOVPN])) {

			if (debug)
				zlog_debug(
					"%s: rmap \"%s\" matches vrf-policy tovpn for as %d afi %s",
					__func__, rmap_name, bgp->as,
					afi2str(afi));

			vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN, afi,
					   bgp_get_default(), bgp);
			if (debug)
				zlog_debug("%s: after vpn_leak_prechange",
					   __func__);

			/* in case of definition/deletion */
			bgp->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_TOVPN] =
				rmap;

			vpn_leak_postchange(BGP_VPN_POLICY_DIR_TOVPN, afi,
					    bgp_get_default(), bgp);

			if (debug)
				zlog_debug("%s: after vpn_leak_postchange",
					   __func__);
		}

		if (bgp->vpn_policy[afi].rmap_name[BGP_VPN_POLICY_DIR_FROMVPN]
			&& !strcmp(rmap_name,
				bgp->vpn_policy[afi]
				.rmap_name[BGP_VPN_POLICY_DIR_FROMVPN]))  {

			if (debug) {
				zlog_debug("%s: rmap \"%s\" matches vrf-policy fromvpn for as %d afi %s",
					__func__, rmap_name, bgp->as,
					afi2str(afi));
			}

			vpn_leak_prechange(BGP_VPN_POLICY_DIR_FROMVPN, afi,
					   bgp_get_default(), bgp);

			/* in case of definition/deletion */
			bgp->vpn_policy[afi].rmap[BGP_VPN_POLICY_DIR_FROMVPN] =
					rmap;

			vpn_leak_postchange(BGP_VPN_POLICY_DIR_FROMVPN, afi,
					    bgp_get_default(), bgp);
		}
	}
}

/* 【函数】vpn_handle_router_id_update —— router-id 变化时维护 VPN 的 auto RD/RT 并
 * 重通告。参数：withdraw=true 表示撤回阶段；is_config 表示是否显式配置触发。
 * 关键点：
 *   - 非显式(如 zebra 引起)的 router-id 变化直接忽略，避免覆盖已配置的 RD/RT。
 *   - withdraw 阶段：从各导入 VRF 的 import RT 列表中删除本 VRF 导出的 RT。
 *   - 新建阶段：用新 router_id 生成 auto RD(未手工设置时采用)、重建导出 RT 列表，
 *     并把该 RT 合并进各导入 VRF 的 import RT 列表，最后 postchange 重通告。
 * 注意：只对 AFI_IP/AFI_IP6 且 vpn 导出激活的实例执行。 */
/* This API is used during router-id change, reflect VPNs
 * auto RD and RT values and readvertise routes to VPN table.
 */
void vpn_handle_router_id_update(struct bgp *bgp, bool withdraw,
				 bool is_config)
{
	afi_t afi;
	int debug = (BGP_DEBUG(vpn, VPN_LEAK_TO_VRF)
		     | BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF));
	char *vname;
	const char *export_name;
	char buf[RD_ADDRSTRLEN];
	struct bgp *bgp_import;
	struct listnode *node;
	struct ecommunity *ecom;
	enum vpn_policy_direction idir, edir;

	/*
	 * Router-id change that is not explicitly configured
	 * (a change from zebra, frr restart for example)
	 * should not replace a configured vpn RD/RT.
	 */
	if (!is_config) {
		if (debug)
			zlog_debug("%s: skipping non explicit router-id change",
				   __func__);
		return;
	}

	if (bgp->inst_type != BGP_INSTANCE_TYPE_DEFAULT
	    && bgp->inst_type != BGP_INSTANCE_TYPE_VRF)
		return;

	export_name = bgp->name ? bgp->name : VRF_DEFAULT_NAME;
	idir = BGP_VPN_POLICY_DIR_FROMVPN;
	edir = BGP_VPN_POLICY_DIR_TOVPN;

	for (afi = 0; afi < AFI_MAX; ++afi) {
		if (!vpn_leak_to_vpn_active(bgp, afi, NULL, false))
			continue;

		if (withdraw) {
			vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN,
					   afi, bgp_get_default(), bgp);
			if (debug)
				zlog_debug("%s: %s after to_vpn vpn_leak_prechange",
					   __func__, export_name);

			/* Remove import RT from VRFs */
			ecom = bgp->vpn_policy[afi].rtlist[edir];
			for (ALL_LIST_ELEMENTS_RO(bgp->vpn_policy[afi].
						  export_vrf, node, vname)) {
				if (strcmp(vname, VRF_DEFAULT_NAME) == 0)
					bgp_import = bgp_get_default();
				else
					bgp_import = bgp_lookup_by_name(vname);
				if (!bgp_import)
					continue;

				ecommunity_del_val(
					bgp_import->vpn_policy[afi]
						.rtlist[idir],
					(struct ecommunity_val *)ecom->val);
			}
		} else {
			/* New router-id derive auto RD and RT and export
			 * to VPN
			 */
			form_auto_rd(bgp->router_id, bgp->vrf_rd_id,
				     &bgp->vrf_prd_auto);
			if (!CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_RD_CLI_SET))
				bgp->vpn_policy[afi].tovpn_rd = bgp->vrf_prd_auto;

			prefix_rd2str(&bgp->vpn_policy[afi].tovpn_rd, buf,
				      sizeof(buf), bgp->asnotation);

			/* free up pre-existing memory if any and allocate
			 *  the ecommunity attribute with new RD/RT
			 */
			if (bgp->vpn_policy[afi].rtlist[edir])
				ecommunity_free(
					&bgp->vpn_policy[afi].rtlist[edir]);
			bgp->vpn_policy[afi].rtlist[edir] = ecommunity_str2com(
				buf, ECOMMUNITY_ROUTE_TARGET, 0);

			/* Update import_vrf rt_list */
			ecom = bgp->vpn_policy[afi].rtlist[edir];
			for (ALL_LIST_ELEMENTS_RO(bgp->vpn_policy[afi].
						  export_vrf, node, vname)) {
				if (strcmp(vname, VRF_DEFAULT_NAME) == 0)
					bgp_import = bgp_get_default();
				else
					bgp_import = bgp_lookup_by_name(vname);
				if (!bgp_import)
					continue;
				if (bgp_import->vpn_policy[afi].rtlist[idir])
					bgp_import->vpn_policy[afi].rtlist[idir]
						= ecommunity_merge(
						bgp_import->vpn_policy[afi]
						.rtlist[idir], ecom);
				else
					bgp_import->vpn_policy[afi].rtlist[idir]
						= ecommunity_dup(ecom);
			}

			/* Update routes to VPN */
			vpn_leak_postchange(BGP_VPN_POLICY_DIR_TOVPN,
					    afi, bgp_get_default(),
					    bgp);
			if (debug)
				zlog_debug("%s: %s after to_vpn vpn_leak_postchange",
					   __func__, export_name);
		}
	}
}

/* 【函数】vpn_policy_routemap_event —— 路由映射事件的对外入口：遍历所有 bgp 实例，
 * 逐个调用 vpn_policy_routemap_update 让其检查是否有策略引用该 rmap_name。
 * 注意 bm->bgp 可能为 NULL(清理阶段)时需提前返回。 */
void vpn_policy_routemap_event(const char *rmap_name)
{
	int debug = BGP_DEBUG(vpn, VPN_LEAK_RMAP_EVENT);
	struct listnode *mnode, *mnnode;
	struct bgp *bgp;

	if (debug)
		zlog_debug("%s: entry", __func__);

	if (bm->bgp == NULL) /* may be called during cleanup */
		return;

	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp))
		vpn_policy_routemap_update(bgp, rmap_name);
}

/* 【函数】vrf_import_from_vrf —— 配置 “import vrf <name>” 时建立 VRF 间导入关系：
 * 在 to_bgp 的 import_vrf 列表登记来源名，标记 BGP_CONFIG_VRF_TO_VRF_IMPORT；若 from_bgp
 * 存在，则在 from_bgp 的 export_vrf 列表登记本实例，并在“首次导出”时为其生成 auto RD/RT
 * (同时置 RD_SET、VRF_TO_VRF_EXPORT、tovpn_label=BGP_PREVENT_VRF_2_VRF_LEAK 防真标签泄漏)；
 * 最后把导出 RT 合并进 to_bgp 的 import RT 列表并触发 prechange/postchange。
 * 易错点：首次导出走 edir(让 from_bgp 先去导出)，否则走 idir(直接导入)。 */
void vrf_import_from_vrf(struct bgp *to_bgp, struct bgp *from_bgp, const char *import_name,
			 afi_t afi, safi_t safi)
{
	const char *export_name;
	enum vpn_policy_direction idir, edir;
	char *vname, *tmp_name;
	char buf[RD_ADDRSTRLEN];
	struct ecommunity *ecom;
	bool first_export = false;
	int debug;
	struct listnode *node;
	bool is_inst_match = false;

	export_name = to_bgp->name ? to_bgp->name : VRF_DEFAULT_NAME;
	idir = BGP_VPN_POLICY_DIR_FROMVPN;
	edir = BGP_VPN_POLICY_DIR_TOVPN;

	debug = (BGP_DEBUG(vpn, VPN_LEAK_TO_VRF) |
		     BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF));

	/*
	 * Cross-ref both VRFs. Also, note if this is the first time
	 * any VRF is importing from "import_vrf".
	 */
	vname = (import_name ? XSTRDUP(MTYPE_TMP, import_name)
			     : XSTRDUP(MTYPE_TMP, VRF_DEFAULT_NAME));

	/* Check the import_vrf list of destination vrf for the source vrf name,
	 * insert otherwise.
	 */
	for (ALL_LIST_ELEMENTS_RO(to_bgp->vpn_policy[afi].import_vrf,
				  node, tmp_name)) {
		if (strcmp(vname, tmp_name) == 0) {
			is_inst_match = true;
			break;
		}
	}
	if (!is_inst_match)
		listnode_add(to_bgp->vpn_policy[afi].import_vrf,
				     vname);
	else
		XFREE(MTYPE_TMP, vname);

	SET_FLAG(to_bgp->af_flags[afi][safi], BGP_CONFIG_VRF_TO_VRF_IMPORT);

	if (!from_bgp)
		/* import vrf VRF context does not exist yet. */
		return;

	/* Check if the source vrf already exports to any vrf,
	 * first time export requires to setup auto derived RD/RT values.
	 * Add the destination vrf name to export vrf list if it is
	 * not present.
	 */
	is_inst_match = false;
	vname = XSTRDUP(MTYPE_TMP, export_name);
	if (!listcount(from_bgp->vpn_policy[afi].export_vrf)) {
		first_export = true;
	} else {
		for (ALL_LIST_ELEMENTS_RO(from_bgp->vpn_policy[afi].export_vrf,
					  node, tmp_name)) {
			if (strcmp(vname, tmp_name) == 0) {
				is_inst_match = true;
				break;
			}
		}
	}
	if (!is_inst_match)
		listnode_add(from_bgp->vpn_policy[afi].export_vrf,
			     vname);
	else
		XFREE(MTYPE_TMP, vname);

	/* Update import RT for current VRF using export RT of the VRF we're
	 * importing from. First though, make sure "import_vrf" has that
	 * set.
	 */
	if (first_export) {
		form_auto_rd(from_bgp->router_id, from_bgp->vrf_rd_id,
			     &from_bgp->vrf_prd_auto);

		if (!CHECK_FLAG(from_bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_RD_CLI_SET))
			from_bgp->vpn_policy[afi].tovpn_rd = from_bgp->vrf_prd_auto;

		SET_FLAG(from_bgp->vpn_policy[afi].flags,
			 BGP_VPN_POLICY_TOVPN_RD_SET);

		prefix_rd2str(&from_bgp->vpn_policy[afi].tovpn_rd, buf,
			      sizeof(buf), from_bgp->asnotation);
		from_bgp->vpn_policy[afi].rtlist[edir] =
			ecommunity_str2com(buf, ECOMMUNITY_ROUTE_TARGET, 0);
		SET_FLAG(from_bgp->af_flags[afi][safi],
			 BGP_CONFIG_VRF_TO_VRF_EXPORT);
		from_bgp->vpn_policy[afi].tovpn_label =
			BGP_PREVENT_VRF_2_VRF_LEAK;
	}
	ecom = from_bgp->vpn_policy[afi].rtlist[edir];
	if (to_bgp->vpn_policy[afi].rtlist[idir])
		to_bgp->vpn_policy[afi].rtlist[idir] =
			ecommunity_merge(to_bgp->vpn_policy[afi]
					 .rtlist[idir], ecom);
	else
		to_bgp->vpn_policy[afi].rtlist[idir] = ecommunity_dup(ecom);

	if (debug) {
		const char *from_name;
		char *ecom1, *ecom2;

		from_name = from_bgp->name ? from_bgp->name :
			VRF_DEFAULT_NAME;

		ecom1 = ecommunity_ecom2str(
			to_bgp->vpn_policy[afi].rtlist[idir],
			ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		ecom2 = ecommunity_ecom2str(
			to_bgp->vpn_policy[afi].rtlist[edir],
			ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		zlog_debug(
			"%s from %s to %s first_export %u import-rt %s export-rt %s",
			__func__, from_name, export_name, first_export, ecom1,
			ecom2);

		ecommunity_strfree(&ecom1);
		ecommunity_strfree(&ecom2);
	}

	/* Does "import_vrf" first need to export its routes or that
	 * is already done and we just need to import those routes
	 * from the global table?
	 */
	if (first_export)
		vpn_leak_postchange(edir, afi, bgp_get_default(), from_bgp);
	else
		vpn_leak_postchange(idir, afi, bgp_get_default(), to_bgp);
}

/* 【函数】vrf_unimport_from_vrf —— 拆除 “import vrf <name>” 关系：
 * 从 to_bgp 的 import_vrf 删名并撤掉已导入的路由(prechange)；若 import 列表已空，
 * 清 BGP_CONFIG_VRF_TO_VRF_IMPORT 与 import RT 列表；否则只从 import RT 列表删掉该来源 RT。
 * 再从 from_bgp 的 export_vrf 删名；若其导出列表已空，则清 RD/RT/EXPORT 标志与 tovpn_label。
 * 注意多处 vname 为 NULL 的安全判断（配置与实例状态可能不一致）。 */
void vrf_unimport_from_vrf(struct bgp *to_bgp, struct bgp *from_bgp, const char *import_name,
			   afi_t afi, safi_t safi)
{
	const char *export_name;
	enum vpn_policy_direction idir, edir;
	char *vname;
	struct ecommunity *ecom = NULL;
	struct listnode *node;
	int debug;

	export_name = to_bgp->name ? to_bgp->name : VRF_DEFAULT_NAME;
	idir = BGP_VPN_POLICY_DIR_FROMVPN;
	edir = BGP_VPN_POLICY_DIR_TOVPN;

	debug = (BGP_DEBUG(vpn, VPN_LEAK_TO_VRF) |
		     BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF));

	/* Were we importing from "import_vrf"? */
	for (ALL_LIST_ELEMENTS_RO(to_bgp->vpn_policy[afi].import_vrf, node,
				  vname)) {
		if (strcmp(vname, import_name) == 0)
			break;
	}

	/*
	 * We do not check in the cli if the passed in bgp
	 * instance is actually imported into us before
	 * we call this function.  As such if we do not
	 * find this in the import_vrf list than
	 * we just need to return safely.
	 */
	if (!vname)
		return;

	if (debug)
		zlog_debug("%s from %s to %s", __func__, import_name, export_name);

	/* Remove "import_vrf" from our import list. */
	listnode_delete(to_bgp->vpn_policy[afi].import_vrf, vname);
	XFREE(MTYPE_TMP, vname);

	/* Remove routes imported from "import_vrf". */
	/* TODO: In the current logic, we have to first remove all
	 * imported routes and then (if needed) import back routes
	 */
	vpn_leak_prechange(idir, afi, bgp_get_default(), to_bgp);

	if (to_bgp->vpn_policy[afi].import_vrf->count == 0) {
		if (!to_bgp->vpn_policy[afi].rmap[idir] && !to_bgp->vpn_policy[afi].rmap_name[idir])
			UNSET_FLAG(to_bgp->af_flags[afi][safi],
				   BGP_CONFIG_VRF_TO_VRF_IMPORT);
		if (to_bgp->vpn_policy[afi].rtlist[idir])
			ecommunity_free(&to_bgp->vpn_policy[afi].rtlist[idir]);
	} else if (from_bgp) {
		ecom = from_bgp->vpn_policy[afi].rtlist[edir];
		if (ecom)
			ecommunity_del_val(to_bgp->vpn_policy[afi].rtlist[idir],
				   (struct ecommunity_val *)ecom->val);
		vpn_leak_postchange(idir, afi, bgp_get_default(), to_bgp);
	}

	if (!from_bgp)
		/* import vrf VRF context not (or no more) existing */
		return;

	/*
	 * What?
	 * So SA is assuming that since the ALL_LIST_ELEMENTS_RO
	 * below is checking for NULL that export_vrf can be
	 * NULL, consequently it is complaining( like a cabbage )
	 * that we could dereference and crash in the listcount(..)
	 * check below.
	 * So make it happy, under protest, with liberty and justice
	 * for all.
	 */
	assert(from_bgp->vpn_policy[afi].export_vrf);

	/* Remove us from "import_vrf's" export list. If no other VRF
	 * is importing from "import_vrf", cleanup appropriately.
	 */
	for (ALL_LIST_ELEMENTS_RO(from_bgp->vpn_policy[afi].export_vrf,
				  node, vname)) {
		if (strcmp(vname, export_name) == 0)
			break;
	}

	/*
	 * If we have gotten to this point then the vname must
	 * exist.  If not, we are in a world of trouble and
	 * have slag sitting around.
	 *
	 * import_vrf and export_vrf must match in having
	 * the in/out names as appropriate.
	 * export_vrf list could have been cleaned up
	 * as part of no router bgp source instance.
	 */
	if (!vname)
		return;

	listnode_delete(from_bgp->vpn_policy[afi].export_vrf, vname);
	XFREE(MTYPE_TMP, vname);

	if (!listcount(from_bgp->vpn_policy[afi].export_vrf)) {
		vpn_leak_prechange(edir, afi, bgp_get_default(), from_bgp);
		ecommunity_free(&from_bgp->vpn_policy[afi].rtlist[edir]);
		UNSET_FLAG(from_bgp->af_flags[afi][safi],
			   BGP_CONFIG_VRF_TO_VRF_EXPORT);
		memset(&from_bgp->vpn_policy[afi].tovpn_rd, 0,
		       sizeof(struct prefix_rd));
		UNSET_FLAG(from_bgp->vpn_policy[afi].flags,
			   BGP_VPN_POLICY_TOVPN_RD_SET);
		from_bgp->vpn_policy[afi].tovpn_label = MPLS_LABEL_NONE;

	}
}

/* 【命令】network A.B.C.D/M rd ASN:NN... <tag|label> (0-1048575) —— 在 VPNv4 地址族
 * 下手工配置一条带 RD 与标签的静态 VPN 网络，内部交给 bgp_static_set 处理。
 * argv 索引：1=前缀，3=RD，5=标签值。 */
/* For testing purpose, static route of MPLS-VPN. */
DEFUN (vpnv4_network,
       vpnv4_network_cmd,
       "network A.B.C.D/M rd ASN:NN_OR_IP-ADDRESS:NN <tag|label> (0-1048575)",
       "Specify a network to announce via BGP\n"
       "IPv4 prefix\n"
       "Specify Route Distinguisher\n"
       "VPN Route Distinguisher\n"
       "VPN NLRI label (tag)\n"
       "VPN NLRI label (tag)\n"
       "Label value\n")
{
	int idx_ipv4_prefixlen = 1;
	int idx_ext_community = 3;
	int idx_label = 5;

	return bgp_static_set(vty, false, argv[idx_ipv4_prefixlen]->arg,
			      argv[idx_ext_community]->arg,
			      argv[idx_label]->arg, AFI_IP, SAFI_MPLS_VPN, NULL,
			      0, 0, 0, NULL, NULL, NULL, NULL);
}

/* 【命令】vpnv4 network ... route-map RMAP_NAME —— 同上，但额外关联一个 route-map，
 * 用于对导入/导出该静态 VPN 路由时施加策略(argv 索引 7=RMAP)。 */
DEFUN (vpnv4_network_route_map,
       vpnv4_network_route_map_cmd,
       "network A.B.C.D/M rd ASN:NN_OR_IP-ADDRESS:NN <tag|label> (0-1048575) route-map RMAP_NAME",
       "Specify a network to announce via BGP\n"
       "IPv4 prefix\n"
       "Specify Route Distinguisher\n"
       "VPN Route Distinguisher\n"
       "VPN NLRI label (tag)\n"
       "VPN NLRI label (tag)\n"
       "Label value\n"
       "route map\n"
       "route map name\n")
{
	int idx_ipv4_prefixlen = 1;
	int idx_ext_community = 3;
	int idx_label = 5;
	int idx_rmap = 7;

	return bgp_static_set(vty, false, argv[idx_ipv4_prefixlen]->arg,
			      argv[idx_ext_community]->arg, argv[idx_label]->arg,
			      AFI_IP, SAFI_MPLS_VPN, argv[idx_rmap]->arg, 0, 0,
			      0, NULL, NULL, NULL, NULL);
}

/* 【命令】no vpnv4 network ... —— 删除上面配置的静态 VPN 路由(argv 索引 2/4/6)。 */
/* For testing purpose, static route of MPLS-VPN. */
DEFUN (no_vpnv4_network,
       no_vpnv4_network_cmd,
       "no network A.B.C.D/M rd ASN:NN_OR_IP-ADDRESS:NN <tag|label> (0-1048575)",
       NO_STR
       "Specify a network to announce via BGP\n"
       "IPv4 prefix\n"
       "Specify Route Distinguisher\n"
       "VPN Route Distinguisher\n"
       "VPN NLRI label (tag)\n"
       "VPN NLRI label (tag)\n"
       "Label value\n")
{
	int idx_ipv4_prefixlen = 2;
	int idx_ext_community = 4;
	int idx_label = 6;

	return bgp_static_set(vty, true, argv[idx_ipv4_prefixlen]->arg,
			      argv[idx_ext_community]->arg,
			      argv[idx_label]->arg, AFI_IP, SAFI_MPLS_VPN, NULL,
			      0, 0, 0, NULL, NULL, NULL, NULL);
}

/* 【命令】vpnv6 network X:X::X:X/M rd ... [route-map RMAP_NAME] —— VPNv6 的静态
 * 网络配置；route-map 为可选(argc==8 时带) 。 */
DEFUN (vpnv6_network,
       vpnv6_network_cmd,
       "network X:X::X:X/M rd ASN:NN_OR_IP-ADDRESS:NN <tag|label> (0-1048575) [route-map RMAP_NAME]",
       "Specify a network to announce via BGP\n"
       "IPv6 prefix <network>/<length>, e.g., 3ffe::/16\n"
       "Specify Route Distinguisher\n"
       "VPN Route Distinguisher\n"
       "VPN NLRI label (tag)\n"
       "VPN NLRI label (tag)\n"
       "Label value\n"
       "route map\n"
       "route map name\n")
{
	int idx_ipv6_prefix = 1;
	int idx_ext_community = 3;
	int idx_label = 5;
	int idx_rmap = 7;

	if (argc == 8)
		return bgp_static_set(vty, false, argv[idx_ipv6_prefix]->arg,
				      argv[idx_ext_community]->arg,
				      argv[idx_label]->arg, AFI_IP6,
				      SAFI_MPLS_VPN, argv[idx_rmap]->arg, 0, 0,
				      0, NULL, NULL, NULL, NULL);
	else
		return bgp_static_set(vty, false, argv[idx_ipv6_prefix]->arg,
				      argv[idx_ext_community]->arg,
				      argv[idx_label]->arg, AFI_IP6,
				      SAFI_MPLS_VPN, NULL, 0, 0, 0, NULL, NULL,
				      NULL, NULL);
}

/* 【命令】no vpnv6 network ... —— 删除 VPNv6 静态网络。 */
/* For testing purpose, static route of MPLS-VPN. */
DEFUN (no_vpnv6_network,
       no_vpnv6_network_cmd,
       "no network X:X::X:X/M rd ASN:NN_OR_IP-ADDRESS:NN <tag|label> (0-1048575)",
       NO_STR
       "Specify a network to announce via BGP\n"
       "IPv6 prefix <network>/<length>, e.g., 3ffe::/16\n"
       "Specify Route Distinguisher\n"
       "VPN Route Distinguisher\n"
       "VPN NLRI label (tag)\n"
       "VPN NLRI label (tag)\n"
       "Label value\n")
{
	int idx_ipv6_prefix = 2;
	int idx_ext_community = 4;
	int idx_label = 6;

	return bgp_static_set(vty, true, argv[idx_ipv6_prefix]->arg,
			      argv[idx_ext_community]->arg,
			      argv[idx_label]->arg, AFI_IP6, SAFI_MPLS_VPN,
			      NULL, 0, 0, 0, NULL, NULL, NULL, NULL);
}

/* 【函数】bgp_show_mpls_vpn —— “show bgp ... vpn ...” 的公共实现：取默认实例的
 * VPN 表(rib[afi][SAFI_MPLS_VPN])，按可选 RD 过滤与 type(普通/邻居等)转交
 * bgp_show_table_rd 输出。无 BGP 进程时给出提示/空 JSON。tags 控制是否显示标签。 */
int bgp_show_mpls_vpn(struct vty *vty, afi_t afi, struct prefix_rd *prd,
		      enum bgp_show_type type, void *output_arg, int tags,
		      bool use_json)
{
	struct bgp *bgp;
	struct bgp_table *table;
	uint16_t show_flags = 0;

	if (use_json)
		SET_FLAG(show_flags, BGP_SHOW_OPT_JSON);

	bgp = bgp_get_default();
	if (bgp == NULL) {
		if (!use_json)
			vty_out(vty, "No BGP process is configured\n");
		else
			vty_out(vty, "{}\n");
		return CMD_WARNING;
	}
	table = bgp->rib[afi][SAFI_MPLS_VPN];
	return bgp_show_table_rd(vty, bgp, afi, SAFI_MPLS_VPN, table, prd, type,
				 output_arg, show_flags);
}

/* 【命令】show bgp <afi> vpn all [rd <..|all>] [json] —— 展示 VPN 表；可指定 RD。 */
DEFUN (show_bgp_ip_vpn_all_rd,
       show_bgp_ip_vpn_all_rd_cmd,
       "show bgp "BGP_AFI_CMD_STR" vpn all [rd <ASN:NN_OR_IP-ADDRESS:NN|all>] [json]",
       SHOW_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display VPN NLRI specific information\n"
       "Display VPN NLRI specific information\n"
       "Display information for a route distinguisher\n"
       "VPN Route Distinguisher\n"
       "All VPN Route Distinguishers\n"
       JSON_STR)
{
	int ret;
	struct prefix_rd prd;
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_afi(argv, argc, &idx, &afi)) {
		/* Constrain search if user supplies RD && RD != "all" */
		if (argv_find(argv, argc, "rd", &idx)
		    && strcmp(argv[idx + 1]->arg, "all")) {
			ret = str2prefix_rd(argv[idx + 1]->arg, &prd);
			if (!ret) {
				vty_out(vty,
					"%% Malformed Route Distinguisher\n");
				return CMD_WARNING;
			}
			return bgp_show_mpls_vpn(vty, afi, &prd,
						 bgp_show_type_normal, NULL, 0,
						 use_json(argc, argv));
		} else {
			return bgp_show_mpls_vpn(vty, afi, NULL,
						 bgp_show_type_normal, NULL, 0,
						 use_json(argc, argv));
		}
	}
	return CMD_SUCCESS;
}

/* 【别名】show bgp <afi> vpn rd <..|all> [json] —— 与上面的 all_rd 同一实现。 */
ALIAS(show_bgp_ip_vpn_all_rd,
      show_bgp_ip_vpn_rd_cmd,
       "show bgp "BGP_AFI_CMD_STR" vpn rd <ASN:NN_OR_IP-ADDRESS:NN|all> [json]",
       SHOW_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display VPN NLRI specific information\n"
       "Display information for a route distinguisher\n"
       "VPN Route Distinguisher\n"
       "All VPN Route Distinguishers\n"
       JSON_STR)

/* 【旧命令兼容】以下 show 命令为兼容旧 CLI(ip bgp 前缀、vpnv4/vpnv6 关键字)保留，
 * 仅在编译期定义 KEEP_OLD_VPN_COMMANDS 时生效；逻辑与上方新命令等价。 */
#ifdef KEEP_OLD_VPN_COMMANDS
/* 【命令】show ip bgp <vpnv4|vpnv6> rd <..|all> —— 按 RD 展示 VPN 表(旧版语法)。 */
DEFUN (show_ip_bgp_vpn_rd,
       show_ip_bgp_vpn_rd_cmd,
       "show ip bgp "BGP_AFI_CMD_STR" vpn rd <ASN:NN_OR_IP-ADDRESS:NN|all>",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_AFI_HELP_STR
       BGP_AF_MODIFIER_STR
       "Display information for a route distinguisher\n"
       "VPN Route Distinguisher\n"
       "All VPN Route Distinguishers\n")
{
	int idx_ext_community = argc - 1;
	int ret;
	struct prefix_rd prd;
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		if (!strcmp(argv[idx_ext_community]->arg, "all"))
			return bgp_show_mpls_vpn(vty, afi, NULL,
						 bgp_show_type_normal, NULL, 0,
						 0);
		ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
		if (!ret) {
			vty_out(vty, "%% Malformed Route Distinguisher\n");
			return CMD_WARNING;
		}
		return bgp_show_mpls_vpn(vty, afi, &prd, bgp_show_type_normal,
					 NULL, 0, 0);
	}
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> —— 展示全部 VPN 路由(旧版语法)。 */
DEFUN (show_ip_bgp_vpn_all,
       show_ip_bgp_vpn_all_cmd,
       "show [ip] bgp <vpnv4|vpnv6>",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR)
{
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi))
		return bgp_show_mpls_vpn(vty, afi, NULL, bgp_show_type_normal,
					 NULL, 0, 0);
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> all tags —— 展示全部 VPN 路由并显示标签。 */
DEFUN (show_ip_bgp_vpn_all_tags,
       show_ip_bgp_vpn_all_tags_cmd,
       "show [ip] bgp <vpnv4|vpnv6> all tags",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display information about all VPNv4/VPNV6 NLRIs\n"
       "Display BGP tags for prefixes\n")
{
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi))
		return bgp_show_mpls_vpn(vty, afi, NULL, bgp_show_type_normal,
					 NULL, 1, 0);
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> rd <..|all> tags —— 按 RD 展示并显示标签。 */
DEFUN (show_ip_bgp_vpn_rd_tags,
       show_ip_bgp_vpn_rd_tags_cmd,
       "show [ip] bgp <vpnv4|vpnv6> rd <ASN:NN_OR_IP-ADDRESS:NN|all> tags",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display information for a route distinguisher\n"
       "VPN Route Distinguisher\n"
       "All VPN Route Distinguishers\n"
       "Display BGP tags for prefixes\n")
{
	int idx_ext_community = 5;
	int ret;
	struct prefix_rd prd;
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		if (!strcmp(argv[idx_ext_community]->arg, "all"))
			return bgp_show_mpls_vpn(vty, afi, NULL,
						 bgp_show_type_normal, NULL, 1,
						 0);
		ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
		if (!ret) {
			vty_out(vty, "%% Malformed Route Distinguisher\n");
			return CMD_WARNING;
		}
		return bgp_show_mpls_vpn(vty, afi, &prd, bgp_show_type_normal,
					 NULL, 1, 0);
	}
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> all neighbors A.B.C.D routes —— 展示某邻居
 * 通告给本端的全部 VPN 路由(按邻居过滤)。 */
DEFUN (show_ip_bgp_vpn_all_neighbor_routes,
       show_ip_bgp_vpn_all_neighbor_routes_cmd,
       "show [ip] bgp <vpnv4|vpnv6> all neighbors A.B.C.D routes [json]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display information about all VPNv4/VPNv6 NLRIs\n"
       "Detailed information on TCP and BGP neighbor connections\n"
       "Neighbor to display information about\n"
       "Display routes learned from neighbor\n"
       JSON_STR)
{
	int idx_ipv4 = 6;
	union sockunion su;
	struct peer *peer;
	int ret;
	bool uj = use_json(argc, argv);
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		ret = str2sockunion(argv[idx_ipv4]->arg, &su);
		if (ret < 0) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(json_no, "warning",
						       "Malformed address");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty, "Malformed address: %s\n",
					argv[idx_ipv4]->arg);
			return CMD_WARNING;
		}

		peer = peer_lookup(NULL, &su);
		if (!peer || !peer->afc[afi][SAFI_MPLS_VPN]) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(
					json_no, "warning",
					"No such neighbor or address family");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty,
					"%% No such neighbor or address family\n");
			return CMD_WARNING;
		}

		return bgp_show_mpls_vpn(vty, afi, NULL, bgp_show_type_neighbor,
					 &su, 0, uj);
	}
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> rd <..|all> neighbors A.B.C.D routes ——
 * 按 RD 与邻居过滤展示 VPN 路由。 */
DEFUN (show_ip_bgp_vpn_rd_neighbor_routes,
       show_ip_bgp_vpn_rd_neighbor_routes_cmd,
       "show [ip] bgp <vpnv4|vpnv6> rd <ASN:NN_OR_IP-ADDRESS:NN|all> neighbors A.B.C.D routes [json]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display information for a route distinguisher\n"
       "VPN Route Distinguisher\n"
       "All VPN Route Distinguishers\n"
       "Detailed information on TCP and BGP neighbor connections\n"
       "Neighbor to display information about\n"
       "Display routes learned from neighbor\n"
       JSON_STR)
{
	int idx_ext_community = 5;
	int idx_ipv4 = 7;
	int ret;
	union sockunion su;
	struct peer *peer;
	struct prefix_rd prd;
	bool prefix_rd_all = false;
	bool uj = use_json(argc, argv);
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		if (!strcmp(argv[idx_ext_community]->arg, "all"))
			prefix_rd_all = true;
		else {
			ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
			if (!ret) {
				if (uj) {
					json_object *json_no = NULL;
					json_no = json_object_new_object();
					json_object_string_add(
						json_no, "warning",
						"Malformed Route Distinguisher");
					vty_out(vty, "%s\n",
						json_object_to_json_string(
							json_no));
					json_object_free(json_no);
				} else
					vty_out(vty,
						"%% Malformed Route Distinguisher\n");
				return CMD_WARNING;
			}
		}

		ret = str2sockunion(argv[idx_ipv4]->arg, &su);
		if (ret < 0) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(json_no, "warning",
						       "Malformed address");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty, "Malformed address: %s\n",
					argv[idx_ext_community]->arg);
			return CMD_WARNING;
		}

		peer = peer_lookup(NULL, &su);
		if (!peer || !peer->afc[afi][SAFI_MPLS_VPN]) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(
					json_no, "warning",
					"No such neighbor or address family");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty,
					"%% No such neighbor or address family\n");
			return CMD_WARNING;
		}

		if (prefix_rd_all)
			return bgp_show_mpls_vpn(vty, afi, NULL,
						 bgp_show_type_neighbor, &su, 0,
						 uj);
		else
			return bgp_show_mpls_vpn(vty, afi, &prd,
						 bgp_show_type_neighbor, &su, 0,
						 uj);
	}
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> all neighbors A.B.C.D advertised-routes ——
 * 展示本端向某邻居通告的全部 VPN 路由。 */
DEFUN (show_ip_bgp_vpn_all_neighbor_advertised_routes,
       show_ip_bgp_vpn_all_neighbor_advertised_routes_cmd,
       "show [ip] bgp <vpnv4|vpnv6> all neighbors A.B.C.D advertised-routes [json]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display information about all VPNv4/VPNv6 NLRIs\n"
       "Detailed information on TCP and BGP neighbor connections\n"
       "Neighbor to display information about\n"
       "Display the routes advertised to a BGP neighbor\n"
       JSON_STR)
{
	int idx_ipv4 = 6;
	int ret;
	struct peer *peer;
	union sockunion su;
	bool uj = use_json(argc, argv);
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		ret = str2sockunion(argv[idx_ipv4]->arg, &su);
		if (ret < 0) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(json_no, "warning",
						       "Malformed address");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty, "Malformed address: %s\n",
					argv[idx_ipv4]->arg);
			return CMD_WARNING;
		}
		peer = peer_lookup(NULL, &su);
		if (!peer || !peer->afc[afi][SAFI_MPLS_VPN]) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(
					json_no, "warning",
					"No such neighbor or address family");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty,
					"%% No such neighbor or address family\n");
			return CMD_WARNING;
		}
		return show_adj_route_vpn(vty, peer, NULL, AFI_IP,
					  SAFI_MPLS_VPN, uj);
	}
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp <vpnv4|vpnv6> rd <..|all> neighbors A.B.C.D advertised-routes ——
 * 按 RD 过滤展示向某邻居通告的 VPN 路由。 */
DEFUN (show_ip_bgp_vpn_rd_neighbor_advertised_routes,
       show_ip_bgp_vpn_rd_neighbor_advertised_routes_cmd,
       "show [ip] bgp <vpnv4|vpnv6> rd <ASN:NN_OR_IP-ADDRESS:NN|all> neighbors A.B.C.D advertised-routes [json]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_VPNVX_HELP_STR
       "Display information for a route distinguisher\n"
       "VPN Route Distinguisher\n"
       "All VPN Route Distinguishers\n"
       "Detailed information on TCP and BGP neighbor connections\n"
       "Neighbor to display information about\n"
       "Display the routes advertised to a BGP neighbor\n"
       JSON_STR)
{
	int idx_ext_community = 5;
	int idx_ipv4 = 7;
	int ret;
	struct peer *peer;
	struct prefix_rd prd;
	union sockunion su;
	bool uj = use_json(argc, argv);
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		ret = str2sockunion(argv[idx_ipv4]->arg, &su);
		if (ret < 0) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(json_no, "warning",
						       "Malformed address");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty, "Malformed address: %s\n",
					argv[idx_ext_community]->arg);
			return CMD_WARNING;
		}
		peer = peer_lookup(NULL, &su);
		if (!peer || !peer->afc[afi][SAFI_MPLS_VPN]) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(
					json_no, "warning",
					"No such neighbor or address family");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty,
					"%% No such neighbor or address family\n");
			return CMD_WARNING;
		}

		if (!strcmp(argv[idx_ext_community]->arg, "all"))
			return show_adj_route_vpn(vty, peer, NULL, AFI_IP,
						  SAFI_MPLS_VPN, uj);
		ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
		if (!ret) {
			if (uj) {
				json_object *json_no = NULL;
				json_no = json_object_new_object();
				json_object_string_add(
					json_no, "warning",
					"Malformed Route Distinguisher");
				vty_out(vty, "%s\n",
					json_object_to_json_string(json_no));
				json_object_free(json_no);
			} else
				vty_out(vty,
					"%% Malformed Route Distinguisher\n");
			return CMD_WARNING;
		}

		return show_adj_route_vpn(vty, peer, &prd, AFI_IP,
					  SAFI_MPLS_VPN, uj);
	}
	return CMD_SUCCESS;
}
#endif /* KEEP_OLD_VPN_COMMANDS */

/* 【函数】bgp_mplsvpn_init —— 模块初始化：把 network 命令注册到 VPNv4/VPNv6 节点，
 * 并把各 show 命令注册到 VIEW 节点（旧命令按宏条件注册）。 */
void bgp_mplsvpn_init(void)
{
	install_element(BGP_VPNV4_NODE, &vpnv4_network_cmd);
	install_element(BGP_VPNV4_NODE, &vpnv4_network_route_map_cmd);
	install_element(BGP_VPNV4_NODE, &no_vpnv4_network_cmd);

	install_element(BGP_VPNV6_NODE, &vpnv6_network_cmd);
	install_element(BGP_VPNV6_NODE, &no_vpnv6_network_cmd);

	install_element(VIEW_NODE, &show_bgp_ip_vpn_all_rd_cmd);
	install_element(VIEW_NODE, &show_bgp_ip_vpn_rd_cmd);
#ifdef KEEP_OLD_VPN_COMMANDS
	install_element(VIEW_NODE, &show_ip_bgp_vpn_rd_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_vpn_all_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_vpn_all_tags_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_vpn_rd_tags_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_vpn_all_neighbor_routes_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_vpn_rd_neighbor_routes_cmd);
	install_element(VIEW_NODE,
			&show_ip_bgp_vpn_all_neighbor_advertised_routes_cmd);
	install_element(VIEW_NODE,
			&show_ip_bgp_vpn_rd_neighbor_advertised_routes_cmd);
#endif /* KEEP_OLD_VPN_COMMANDS */
}

/* 【函数】get_first_vrf_for_redirect_with_rt —— 给定目标 RT(扩展团体)，找出第一个
 * 在其 import_redirect_rtlist 中包含该 RT 的 VRF 实例，返回其 vrf_id；没有返回 VRF_UNKNOWN。
 * 用于 EVPN/redirect 场景按 RT 定位应重定向到的 VRF。eckey 的 unit_size 可能是 IPv6
 * 扩展团体，据此选择 afi。 */
vrf_id_t get_first_vrf_for_redirect_with_rt(struct ecommunity *eckey)
{
	struct listnode *mnode, *mnnode;
	struct bgp *bgp;
	afi_t afi = AFI_IP;

	if (eckey->unit_size == IPV6_ECOMMUNITY_SIZE)
		afi = AFI_IP6;

	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {
		struct ecommunity *ec;

		if (bgp->inst_type != BGP_INSTANCE_TYPE_VRF)
			continue;

		ec = bgp->vpn_policy[afi].import_redirect_rtlist;

		if (ec && eckey->unit_size != ec->unit_size)
			continue;

		if (ecommunity_include(ec, eckey))
			return bgp->vrf_id;
	}
	return VRF_UNKNOWN;
}

/* 【函数】vpn_leak_postchange_all —— 处理“因默认实例尚未创建而延后”的泄漏：
 * 当默认实例("router bgp NNN")刚建立时被调用。分两趟遍历 VRF 实例：
 *   第一趟对每个非自动 VRF 执行 tovpn postchange(导出到 VPN)；
 *   第二趟执行 fromvpn postchange(从 VPN 导入)。
 * 这样先前仅配置了 VRF 内 rd/rt 的场景在默认实例出现后能一次性生效。 */
/*
 * The purpose of this function is to process leaks that were deferred
 * from earlier per-vrf configuration due to not-yet-existing default
 * vrf, in other words, configuration such as:
 *
 *     router bgp MMM vrf FOO
 *       address-family ipv4 unicast
 *         rd vpn export 1:1
 *       exit-address-family
 *
 *     router bgp NNN
 *       ...
 *
 * This function gets called when the default instance ("router bgp NNN")
 * is created.
 */
void vpn_leak_postchange_all(void)
{
	struct listnode *next;
	struct bgp *bgp;
	struct bgp *bgp_default = bgp_get_default();

	assert(bgp_default);

	/* First, do any exporting from VRFs to the single VPN RIB */
	for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, bgp)) {

		if (bgp->inst_type != BGP_INSTANCE_TYPE_VRF)
			continue;

		if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_AUTO))
			continue;

		vpn_leak_postchange(
			BGP_VPN_POLICY_DIR_TOVPN,
			AFI_IP,
			bgp_default,
			bgp);

		vpn_leak_postchange(
			BGP_VPN_POLICY_DIR_TOVPN,
			AFI_IP6,
			bgp_default,
			bgp);
	}

	/* Now, do any importing to VRFs from the single VPN RIB */
	for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, bgp)) {

		if (bgp->inst_type != BGP_INSTANCE_TYPE_VRF)
			continue;

		if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_AUTO))
			continue;

		vpn_leak_postchange(
			BGP_VPN_POLICY_DIR_FROMVPN,
			AFI_IP,
			bgp_default,
			bgp);

		vpn_leak_postchange(
			BGP_VPN_POLICY_DIR_FROMVPN,
			AFI_IP6,
			bgp_default,
			bgp);
	}
}

/* 【函数】bgp_vpn_leak_unimport —— bgp vrf 实例被反配置时清理泄漏关系：
 * 对每个 afi(仅 IP/IPv6 单播)，遍历其他实例，若其 import_vrf 列表含本实例名，
 * 则拆掉该导入关系(vrf_unimport_from_vrf)后把名字加回(配置仍在)，并处理本实例
 * 被别的 VRF 导出时的情况；最后若默认实例未配置 retain RT all，则对本实例执行
 * vpn_leak_no_retain 清理其残留在 VPN 表里的路由。 */
/* When a bgp vrf instance is unconfigured, remove its routes
 * from the VPN table and this vrf could be importing routes from other
 * bgp vrf instances, unimport them.
 * VRF X and VRF Y are exporting routes to each other.
 * When VRF X is deleted, unimport its routes from all target vrfs,
 * also VRF Y should unimport its routes from VRF X table.
 * This will ensure VPN table is cleaned up appropriately.
 */
void bgp_vpn_leak_unimport(struct bgp *from_bgp)
{
	struct bgp *bgp_default = bgp_get_default();
	struct bgp *to_bgp;
	const char *tmp_name;
	char *vname;
	struct listnode *node, *next;
	safi_t safi = SAFI_UNICAST;
	afi_t afi;
	bool is_vrf_leak_bind;
	int debug;

	if (from_bgp->inst_type != BGP_INSTANCE_TYPE_VRF &&
	    from_bgp->inst_type != BGP_INSTANCE_TYPE_DEFAULT)
		return;

	debug = (BGP_DEBUG(vpn, VPN_LEAK_TO_VRF) |
		     BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF));

	tmp_name = from_bgp->name ? from_bgp->name : VRF_DEFAULT_NAME;

	for (afi = 0; afi < AFI_MAX; ++afi) {
		/* vrf leak is for IPv4 and IPv6 Unicast only */
		if (afi != AFI_IP && afi != AFI_IP6)
			continue;

		for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, to_bgp)) {
			if (from_bgp == to_bgp)
				continue;

			/* Unimport and remove source vrf from the
			 * other vrfs import list.
			 */
			struct vpn_policy *to_vpolicy;

			is_vrf_leak_bind = false;
			to_vpolicy = &(to_bgp->vpn_policy[afi]);
			for (ALL_LIST_ELEMENTS_RO(to_vpolicy->import_vrf, node,
						  vname)) {
				if (strcmp(vname, tmp_name) == 0) {
					is_vrf_leak_bind = true;
					break;
				}
			}
			/* skip this bgp instance as there is no leak to this
			 * vrf instance.
			 */
			if (!is_vrf_leak_bind)
				continue;

			if (debug)
				zlog_debug("%s: unimport routes from %s to_bgp %s afi %s import vrfs count %u",
					   __func__, from_bgp->name_pretty,
					   to_bgp->name_pretty, afi2str(afi),
					   to_vpolicy->import_vrf->count);

			vrf_unimport_from_vrf(to_bgp, from_bgp, tmp_name, afi, safi);

			/* readd vrf name as unimport removes import vrf name
			 * from the destination vrf's import list where the
			 * `import vrf` configuration still exist.
			 */
			vname = XSTRDUP(MTYPE_TMP, tmp_name);
			listnode_add(to_bgp->vpn_policy[afi].import_vrf,
				     vname);
			SET_FLAG(to_bgp->af_flags[afi][safi],
				 BGP_CONFIG_VRF_TO_VRF_IMPORT);

			/* If to_bgp exports its routes to the bgp vrf
			 * which is being deleted, un-import the
			 * to_bgp routes from VPN.
			 */
			for (ALL_LIST_ELEMENTS_RO(to_bgp->vpn_policy[afi]
						  .export_vrf, node,
						  vname)) {
				if (strcmp(vname, tmp_name) == 0) {
					vrf_unimport_from_vrf(from_bgp, to_bgp, tmp_name, afi,
							      safi);
					break;
				}
			}
		}

		if (bgp_default &&
		    !CHECK_FLAG(bgp_default->af_flags[afi][SAFI_MPLS_VPN],
				BGP_VPNVX_RETAIN_ROUTE_TARGET_ALL)) {
			/* 'from_bgp' instance will be deleted
			 * so force to unset importation to update VPN labels
			 */
			UNSET_FLAG(from_bgp->af_flags[afi][SAFI_UNICAST],
				   BGP_CONFIG_MPLSVPN_TO_VRF_IMPORT);
			vpn_leak_no_retain(from_bgp, bgp_default, afi);
		}
	}
	return;
}

/* 【函数】bgp_vpn_leak_export —— 新建 router bgp(vrf) 实例时，若已有 VRF 声明从它导入，
 * 则把这些导入关系正式建立：遍历各实例的 import_vrf，命中本实例名者，调整 import RT
 * 后调用 vrf_import_from_vrf 重新导入（并导出本实例路由）。 */
/* When a router bgp is configured, there could be a bgp vrf
 * instance importing routes from this newly configured
 * bgp vrf instance. Export routes from configured
 * bgp vrf to VPN.
 * VRF Y has import from bgp vrf x,
 * when a bgp vrf x instance is created, export its routes
 * to VRF Y instance.
 */
void bgp_vpn_leak_export(struct bgp *from_bgp)
{
	afi_t afi;
	const char *export_name;
	char *vname;
	struct listnode *node, *next;
	struct ecommunity *ecom;
	enum vpn_policy_direction idir, edir;
	safi_t safi = SAFI_UNICAST;
	struct bgp *to_bgp;
	int debug;

	debug = (BGP_DEBUG(vpn, VPN_LEAK_TO_VRF) |
		     BGP_DEBUG(vpn, VPN_LEAK_FROM_VRF));

	idir = BGP_VPN_POLICY_DIR_FROMVPN;
	edir = BGP_VPN_POLICY_DIR_TOVPN;

	export_name = from_bgp->name ? from_bgp->name : VRF_DEFAULT_NAME;

	for (afi = 0; afi < AFI_MAX; ++afi) {
		/* vrf leak is for IPv4 and IPv6 Unicast only */
		if (afi != AFI_IP && afi != AFI_IP6)
			continue;

		for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, to_bgp)) {
			if (from_bgp == to_bgp)
				continue;

			/* bgp instance has import list, check to see if newly
			 * configured bgp instance is the list.
			 */
			struct vpn_policy *to_vpolicy;

			to_vpolicy = &(to_bgp->vpn_policy[afi]);
			for (ALL_LIST_ELEMENTS_RO(to_vpolicy->import_vrf,
						  node, vname)) {
				if (strcmp(vname, export_name) != 0)
					continue;

				if (debug)
					zlog_debug("%s: found from_bgp %s in to_bgp %s import list, import routes.",
					   __func__,
					   export_name, to_bgp->name_pretty);

				ecom = from_bgp->vpn_policy[afi].rtlist[edir];
				/* remove import rt, it will be readded
				 * as part of import from vrf.
				 */
				if (ecom)
					ecommunity_del_val(
						to_vpolicy->rtlist[idir],
						(struct ecommunity_val *)
							ecom->val);
				vrf_import_from_vrf(to_bgp, from_bgp, export_name, afi, safi);
				break;

			}
		}
	}
}

/* 【函数】bgp_vpn_release_label —— 释放先前自动分配的 per-VRF 出口标签，并(可选)复位
 * auto 模式标志。非 auto 模式、标签为 NONE(分配在途)、或处于 per-nexthop 模式时均直接返回
 * (这些情况由各自路径负责)。vty 中改分配模式时也调用本函数。 */
/* It releases the label from labelpool which
 * was previously assigned and unsets the flag based on reset arg
 * This also used in vty to release the label and to change the allocation mode as well
 */
void bgp_vpn_release_label(struct bgp *bgp, afi_t afi, bool reset)
{
	if (!CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_LABEL_AUTO))
		return;
	/*
	 * label has previously been automatically
	 * assigned by labelpool: release it
	 *
	 * NB if tovpn_label == MPLS_LABEL_NONE it
	 * means the automatic assignment is in flight
	 * and therefore the labelpool callback must
	 * detect that the auto label is not needed.
	 */
	if (bgp->vpn_policy[afi].tovpn_label == MPLS_LABEL_NONE)
		return;
	if (CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_LABEL_PER_NEXTHOP))
		return;

	bgp_vpn_lp_release(&bgp->vpn_policy[afi], bgp->vpn_policy[afi].tovpn_label);

	if (reset)
		UNSET_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_LABEL_AUTO);
}

/* 【函数】bgp_mplsvpn_nh_label_bind_cmp —— RB 树比较函数：先按下一跳 prefix 比较，
 * 再按 orig_label 比较，确定 (nexthop, orig_label) 在树中的唯一位置。 */
/* The nexthops values are compared to
 * find in the tree the appropriate cache entry
 */
int bgp_mplsvpn_nh_label_bind_cmp(
	const struct bgp_mplsvpn_nh_label_bind_cache *a,
	const struct bgp_mplsvpn_nh_label_bind_cache *b)
{
	if (prefix_cmp(&a->nexthop, &b->nexthop))
		return 1;
	if (a->orig_label > b->orig_label)
		return 1;
	if (a->orig_label < b->orig_label)
		return -1;
	return 0;
}

/* 【函数】bgp_mplsvpn_nh_label_bind_send_nexthop_label —— 向 zebra 下发/更新/删除一条
 * MPLS 绑定：把 new_label 指向的入标签与 (下一跳, orig_label) 的转发动作下发给内核。
 * cmd 取 ZEBRA_MPLS_LABELS_ADD/DELETE/REPLACE。会连同下一跳已有的 nh_label 组成标签栈
 * (出栈=下层 LSP 标签 + orig_label)；标签栈超过 MPLS_MAX_LABELS 则放弃安装并报错。
 * 关键点：直连(NEXTHOP_TYPE_IFINDEX)用前缀本身作目标，递归下一跳用其网关地址；
 *   黑洞下一跳直接返回。 */
static void bgp_mplsvpn_nh_label_bind_send_nexthop_label(
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc, int cmd)
{
	struct prefix pfx_nh, *p = NULL;
	uint8_t num_labels = 0, lsp_num_labels;
	mpls_label_t label[MPLS_MAX_LABELS];
	struct nexthop *nh;
	ifindex_t ifindex = IFINDEX_INTERNAL;
	vrf_id_t vrf_id = VRF_DEFAULT;
	uint32_t i;

	if (bmnc->nh == NULL)
		return;
	nh = bmnc->nh;
	switch (nh->type) {
	case NEXTHOP_TYPE_IFINDEX:
		p = &bmnc->nexthop;
		label[num_labels] = bmnc->orig_label;
		num_labels += 1;
		ifindex = nh->ifindex;
		vrf_id = nh->vrf_id;
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		if (nh->type == NEXTHOP_TYPE_IPV4 ||
		    nh->type == NEXTHOP_TYPE_IPV4_IFINDEX) {
			pfx_nh.family = AF_INET;
			pfx_nh.prefixlen = IPV4_MAX_BITLEN;
			IPV4_ADDR_COPY(&pfx_nh.u.prefix4, &nh->gate.ipv4);
		} else {
			pfx_nh.family = AF_INET6;
			pfx_nh.prefixlen = IPV6_MAX_BITLEN;
			IPV6_ADDR_COPY(&pfx_nh.u.prefix6, &nh->gate.ipv6);
		}
		p = &pfx_nh;
		if (nh->nh_label) {
			if (nh->nh_label->num_labels + 1 > MPLS_MAX_LABELS) {
				/* label stack overflow. no label switching will be performed
				 */
				flog_err(EC_BGP_LABEL,
					 "%s [Error] BGP label %u->%u to %pFX, forged label stack too big: %u. Abort LSP installation",
					 bmnc->bgp_vpn->name_pretty,
					 bmnc->new_label, bmnc->orig_label,
					 &bmnc->nexthop,
					 nh->nh_label->num_labels + 1);
				return;
			}
			lsp_num_labels = nh->nh_label->num_labels;
			for (i = 0; i < lsp_num_labels; i++)
				label[num_labels + i] = nh->nh_label->label[i];
			num_labels = lsp_num_labels;
		}
		label[num_labels] = bmnc->orig_label;
		num_labels += 1;
		if (nh->type == NEXTHOP_TYPE_IPV4_IFINDEX ||
		    nh->type == NEXTHOP_TYPE_IPV6_IFINDEX) {
			ifindex = nh->ifindex;
			vrf_id = nh->vrf_id;
		}
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		return;
	}
	bgp_zebra_send_nexthop_label(cmd, bmnc->new_label, ifindex, vrf_id,
				     ZEBRA_LSP_BGP, p, num_labels, &label[0]);
}

/* 【函数】bgp_mplsvpn_nh_label_bind_free —— 释放一个 nexthop-label 绑定缓存：
 * 若标签仍在申请中(allocation_in_progress)，仅标记并移除缓存(回调会识别并真正删除)；
 * 否则删除内核 MPLS 绑定、归还标签(bgp_vpn_nh_lp_release)、从树中移除、释放 nexthop 与内存。 */
void bgp_mplsvpn_nh_label_bind_free(
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc)
{
	if (bmnc->allocation_in_progress) {
		bmnc->allocation_in_progress = false;
		bgp_mplsvpn_nh_label_bind_cache_del(
			&bmnc->bgp_vpn->mplsvpn_nh_label_bind, bmnc);
		return;
	}
	if (bmnc->new_label != MPLS_INVALID_LABEL) {
		bgp_mplsvpn_nh_label_bind_send_nexthop_label(
			bmnc, ZEBRA_MPLS_LABELS_DELETE);
		bgp_vpn_nh_lp_release(bmnc, bmnc->new_label);
	}
	bgp_mplsvpn_nh_label_bind_cache_del(
		&bmnc->bgp_vpn->mplsvpn_nh_label_bind, bmnc);

	if (bmnc->nh)
		nexthop_free(bmnc->nh);

	XFREE(MTYPE_MPLSVPN_NH_LABEL_BIND_CACHE, bmnc);
}

/* 【函数】bgp_mplsvpn_nh_label_bind_new —— 分配并初始化一个绑定缓存：
 * 以 (p, orig_label) 为键，new_label 先置 MPLS_INVALID_LABEL，初始化 paths 链表后插入树。 */
struct bgp_mplsvpn_nh_label_bind_cache *
bgp_mplsvpn_nh_label_bind_new(struct bgp_mplsvpn_nh_label_bind_cache_head *tree,
			      struct prefix *p, mpls_label_t orig_label)
{
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc;

	bmnc = XCALLOC(MTYPE_MPLSVPN_NH_LABEL_BIND_CACHE,
		       sizeof(struct bgp_mplsvpn_nh_label_bind_cache));
	bmnc->new_label = MPLS_INVALID_LABEL;
	prefix_copy(&bmnc->nexthop, p);
	bmnc->orig_label = orig_label;

	LIST_INIT(&(bmnc->paths));
	bgp_mplsvpn_nh_label_bind_cache_add(tree, bmnc);

	return bmnc;
}

/* 【函数】bgp_mplsvpn_nh_label_bind_find —— 以 (p, orig_label) 在树中查找绑定缓存；
 * 用栈上临时对象走 RB 查找。tree 为空返回 NULL。 */
struct bgp_mplsvpn_nh_label_bind_cache *bgp_mplsvpn_nh_label_bind_find(
	struct bgp_mplsvpn_nh_label_bind_cache_head *tree, struct prefix *p,
	mpls_label_t orig_label)
{
	struct bgp_mplsvpn_nh_label_bind_cache bmnc = {0};

	if (!tree)
		return NULL;
	prefix_copy(&bmnc.nexthop, p);
	bmnc.orig_label = orig_label;

	return bgp_mplsvpn_nh_label_bind_cache_find(tree, &bmnc);
}

/* 【函数】bgp_mplsvpn_path_uses_valid_mpls_label —— 判断该路径是否携带“可用的 MPLS 标签”：
 * SRv6 路径(srv6_l3service)与带 prefix_sid 标签索引的路径不算；其余按
 * bgp_path_info_has_valid_label() 判定是否含有效 MPLS 标签。 */
/* Called to check if the incoming l3vpn path entry
 * has mpls label information
 */
bool bgp_mplsvpn_path_uses_valid_mpls_label(struct bgp_path_info *pi)
{
	if (pi->attr && pi->attr->srv6_l3service)
		/* srv6 sid */
		return false;

	if (pi->attr && bgp_attr_exists(pi->attr, BGP_ATTR_PREFIX_SID) &&
	    pi->attr->label_index != BGP_INVALID_LABEL_INDEX)
		/* prefix_sid attribute */
		return false;

	if (!bgp_path_info_has_valid_label(pi))
		/* invalid MPLS label */
		return false;
	return true;
}

/* 【函数】bgp_mplsvpn_nh_label_bind_get_label —— 取该路径绑定后应对外通告的“新标签”：
 * 从绑定缓存取 new_label，编码为带 BOS 的 MPLS 标签并置内部有效位；未绑定或仍在
 * 申请中则返回 MPLS_INVALID_LABEL。 */
mpls_label_t bgp_mplsvpn_nh_label_bind_get_label(struct bgp_path_info *pi)
{
	mpls_label_t label;
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc;

	bmnc = pi->mplsvpn.bmnc.nh_label_bind_cache;
	if (!bmnc || bmnc->new_label == MPLS_INVALID_LABEL)
		/* allocation in progress
		 * or path not eligible for local label
		 */
		return MPLS_INVALID_LABEL;

	label = mpls_lse_encode(bmnc->new_label, 0, 0, 1);
	bgp_set_valid_label(&label);

	return label;
}

/* 【函数】bgp_mplsvpn_nh_label_bind_get_local_label_cb —— zebra 回复 l3vpn 绑定用
 * 本地新标签的异步回调(context=bmnc)。allocated=false 时置 INVALID。若申请已被取消
 * (allocation_in_progress 为假)则直接释放缓存；否则下发内核绑定，并遍历绑定下的路径
 * 置 BGP_NODE_LABEL_CHANGED 后 bgp_process，触发携带新标签的重通告。 */
/* Called upon reception of a ZAPI Message from zebra, about
 * a new available label.
 */
static int bgp_mplsvpn_nh_label_bind_get_local_label_cb(mpls_label_t label,
							void *context,
							bool allocated)
{
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc = context;
	struct bgp_table *table;
	struct bgp_path_info *pi;

	if (BGP_DEBUG(labelpool, LABELPOOL))
		zlog_debug("%s: label=%u, allocated=%d, nexthop=%pFX, label %u",
			   __func__, label, allocated, &bmnc->nexthop,
			   bmnc->orig_label);
	if (allocated)
		/* update the entry with the new label */
		bmnc->new_label = label;
	else
		/*
		 * previously-allocated label is now invalid
		 * eg: zebra deallocated the labels and notifies it
		 */
		bmnc->new_label = MPLS_INVALID_LABEL;

	if (!bmnc->allocation_in_progress) {
		bgp_mplsvpn_nh_label_bind_free(bmnc);
		return 0;
	}
	bmnc->allocation_in_progress = false;

	if (bmnc->new_label != MPLS_INVALID_LABEL)
		/*
		 * Create the LSP : <local_label -> bmnc->orig_label,
		 * via bmnc->prefix, interface bnc->nexthop->ifindex
		 */
		bgp_mplsvpn_nh_label_bind_send_nexthop_label(
			bmnc, ZEBRA_MPLS_LABELS_ADD);

	LIST_FOREACH (pi, &(bmnc->paths), mplsvpn.bmnc.nh_label_bind_thread) {
		/* we can advertise it */
		if (!pi->net)
			continue;
		table = bgp_dest_table(pi->net);
		if (!table)
			continue;
		SET_FLAG(pi->net->flags, BGP_NODE_LABEL_CHANGED);
		bgp_process(table->bgp, pi->net, pi, table->afi, table->safi);
	}

	return 0;
}

/* 【函数】bgp_mplsvpn_path_nh_label_bind_unlink —— 把路径从其 nexthop-label 绑定缓存
 * 上解绑：LIST_REMOVE、递减 path_count、清缓存指针。
 * 疑点：此处对 BGP_PATH_MPLSVPN_NH_LABEL_BIND 用的是 SET_FLAG，而对照相邻的
 *   bgp_mplsvpn_path_nh_label_unlink() 用的是 UNSET_FLAG；语义上“解绑后应清除标志”，
 *   这里可能是笔误（待确认）。未绑定/缓存指针为空则直接返回；缓存空了则释放。 */
void bgp_mplsvpn_path_nh_label_bind_unlink(struct bgp_path_info *pi)
{
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc;

	if (!pi)
		return;

	if (!CHECK_FLAG(pi->flags, BGP_PATH_MPLSVPN_NH_LABEL_BIND))
		return;

	bmnc = pi->mplsvpn.bmnc.nh_label_bind_cache;

	if (!bmnc)
		return;

	LIST_REMOVE(pi, mplsvpn.bmnc.nh_label_bind_thread);
	pi->mplsvpn.bmnc.nh_label_bind_cache->path_count--;
	pi->mplsvpn.bmnc.nh_label_bind_cache = NULL;
	SET_FLAG(pi->flags, BGP_PATH_MPLSVPN_NH_LABEL_BIND);

	if (LIST_EMPTY(&(bmnc->paths)))
		bgp_mplsvpn_nh_label_bind_free(bmnc);
}

/* 【函数】bgp_mplsvpn_nh_label_bind_register_local_label —— 为一条 l3vpn 路径登记
 * 本地“出标签绑定”：以 (下一跳, 收到的 orig_label) 为键找/建缓存，没有则新建并异步
 * 申请本地标签(bgp_lp_get→...get_local_label_cb)；把 pi 绑定进缓存并同步 nexthop。
 * 这样在 next-hop 被改写(next-hop-self/eBGP)时仍能换出新标签并保持 orig_label 转发。
 * 注意：形参 dest 未在函数体中使用（仅 pi 提供下一跳等信息）。 */
void bgp_mplsvpn_nh_label_bind_register_local_label(struct bgp *bgp,
						    struct bgp_dest *dest,
						    struct bgp_path_info *pi)
{
	struct bgp_mplsvpn_nh_label_bind_cache *bmnc;
	struct bgp_mplsvpn_nh_label_bind_cache_head *tree;
	mpls_label_t label;

	label = BGP_PATH_INFO_NUM_LABELS(pi)
			? decode_label(&pi->extra->labels->label[0])
			: MPLS_INVALID_LABEL;

	tree = &bgp->mplsvpn_nh_label_bind;
	bmnc = bgp_mplsvpn_nh_label_bind_find(tree, &pi->nexthop->prefix, label);
	if (!bmnc) {
		bmnc = bgp_mplsvpn_nh_label_bind_new(tree, &pi->nexthop->prefix,
						     label);
		bmnc->bgp_vpn = bgp;
		bmnc->allocation_in_progress = true;
		bgp_lp_get(LP_TYPE_BGP_L3VPN_BIND, bmnc, bgp->vrf_id,
			   bgp_mplsvpn_nh_label_bind_get_local_label_cb);
	}

	if (pi->mplsvpn.bmnc.nh_label_bind_cache == bmnc)
		/* no change */
		return;

	bgp_mplsvpn_path_nh_label_bind_unlink(pi);

	/* updates NHT pi list reference */
	LIST_INSERT_HEAD(&(bmnc->paths), pi, mplsvpn.bmnc.nh_label_bind_thread);
	pi->mplsvpn.bmnc.nh_label_bind_cache = bmnc;
	pi->mplsvpn.bmnc.nh_label_bind_cache->path_count++;
	SET_FLAG(pi->flags, BGP_PATH_MPLSVPN_NH_LABEL_BIND);
	bmnc->last_update = monotime(NULL);

	/* Add or update the selected nexthop */
	if (!bmnc->nh)
		bmnc->nh = nexthop_dup(pi->nexthop->nexthop, NULL);
	else if (!nexthop_same(pi->nexthop->nexthop, bmnc->nh)) {
		nexthop_free(bmnc->nh);
		bmnc->nh = nexthop_dup(pi->nexthop->nexthop, NULL);
		if (bmnc->new_label != MPLS_INVALID_LABEL)
			bgp_mplsvpn_nh_label_bind_send_nexthop_label(
				bmnc, ZEBRA_MPLS_LABELS_REPLACE);
	}
}

/* 【函数】show_bgp_mplsvpn_nh_label_bind_internal —— 打印某实例的 nexthop-label 绑定
 * 缓存表：每项显示下一跳、orig_label、已分配的 local(new) label、路径数与接口；
 * detail 为真时再列出引用该缓存的每条路径。 */
static void show_bgp_mplsvpn_nh_label_bind_internal(struct vty *vty,
						    struct bgp *bgp,
						    bool detail)
{
	struct bgp_mplsvpn_nh_label_bind_cache_head *tree;
	struct bgp_mplsvpn_nh_label_bind_cache *iter;
	afi_t afi;
	safi_t safi;
	struct bgp_dest *dest;
	struct bgp_path_info *path;
	struct bgp *bgp_path;
	struct bgp_table *table;
	char buf[32];

	vty_out(vty, "Current BGP mpls-vpn nexthop label bind cache, %s\n",
		bgp->name_pretty);

	tree = &bgp->mplsvpn_nh_label_bind;
	frr_each (bgp_mplsvpn_nh_label_bind_cache, tree, iter) {
		if (iter->nexthop.family == AF_INET)
			vty_out(vty, " %pI4", &iter->nexthop.u.prefix4);
		else
			vty_out(vty, " %pI6", &iter->nexthop.u.prefix6);
		vty_out(vty, ", label %u, local label %u #paths %u\n",
			iter->orig_label, iter->new_label, iter->path_count);
		if (iter->nh)
			vty_out(vty, "  interface %s\n",
				ifindex2ifname(iter->nh->ifindex,
					       iter->nh->vrf_id));
		vty_out(vty, "  Last update: %s", time_to_string(iter->last_update, buf));
		if (!detail)
			continue;
		vty_out(vty, "  Paths:\n");
		LIST_FOREACH (path, &(iter->paths),
			      mplsvpn.bmnc.nh_label_bind_thread) {
			dest = path->net;
			table = bgp_dest_table(dest);
			assert(dest && table);
			afi = family2afi(bgp_dest_get_prefix(dest)->family);
			safi = table->safi;
			bgp_path = table->bgp;

			vty_out(vty, "    %d/%d %pBD %s flags 0x%x\n", afi,
				safi, dest, bgp_path->name_pretty, path->flags);
		}
	}
}


/* 【命令】show bgp [<view|vrf> NAME] mplsvpn-nh-label-bind [detail] —— 展示 nexthop-label
 * 绑定缓存；可指定 vrf，带 detail 显示每条路径。 */
DEFUN(show_bgp_mplsvpn_nh_label_bind, show_bgp_mplsvpn_nh_label_bind_cmd,
      "show bgp [<view|vrf> VIEWVRFNAME] mplsvpn-nh-label-bind [detail]",
      SHOW_STR BGP_STR BGP_INSTANCE_HELP_STR
      "BGP mplsvpn nexthop label binding entries\n"
      "Show detailed information\n")
{
	int idx = 0;
	char *vrf = NULL;
	struct bgp *bgp;
	bool detail = false;

	if (argv_find(argv, argc, "vrf", &idx)) {
		vrf = argv[++idx]->arg;
		bgp = bgp_lookup_by_name(vrf);
	} else
		bgp = bgp_get_default();

	if (!bgp)
		return CMD_SUCCESS;

	if (argv_find(argv, argc, "detail", &idx))
		detail = true;

	show_bgp_mplsvpn_nh_label_bind_internal(vty, bgp, detail);
	return CMD_SUCCESS;
}

/* 【函数】bgp_mplsvpn_nexthop_init —— 注册 nexthop-label 绑定相关的 show 命令。 */
void bgp_mplsvpn_nexthop_init(void)
{
	install_element(VIEW_NODE, &show_bgp_mplsvpn_nh_label_bind_cmd);
}
