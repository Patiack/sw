// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP open message handling
 * Copyright (C) 1998, 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_open.c —— BGP OPEN 报文的构造与解析，以及“能力协商”的核心实现
 * ============================================================================
 * 【职责】
 *   本文件只管一件事：OPEN 报文。BGP 建连时双方先互发 OPEN，用来交换版本、
 *   Hold Time、BGP Identifier、自治系统号(AS)，并通过“可选参数”里的
 *   Capability TLV 列表协商双方共同支持的特性（MP-BGP、Route Refresh、
 *   Graceful Restart、4 字节 AS、Add-Path、Extended Message、Role……）。
 *   能力谈妥与否，直接决定后续 UPDATE 能用哪些地址族、报文最大能发多大、能否
 *   GR 重启。它是 FSM 从 OpenSent/OpenConfirm 走向 Established 的必经环节。
 *
 * 【属于哪条主线 / 被谁调用】
 *   连接线 (peer -> peer_connection) 的一环。
 *   发送侧：bgp_fsm.c 在 OpenSent 状态调用 bgp_open_send()（定义在 bgp_packet.c），
 *           后者调用本文件的 bgp_open_capability() 逐条写入 Capability TLV。
 *   接收侧：bgp_packet.c 的报文分发器收到 BGP_MSG_OPEN 时调用 bgp_open_receive()，
 *           它先调 peek_for_as4_capability()（抢读 AS4，用于判定远端身份），
 *           再调 bgp_open_option_parse() -> bgp_capability_parse() 逐条解析。
 *   展示侧：bgp_vty.c 的 “show bgp neighbor” 调用 bgp_capability_vty_out() 打印
 *           协商失败时记录在 peer->notify 里的能力错误详情。
 *
 * 【四个关键数组：能力协商的“账本”】—— 都在 struct peer 里
 *   afc[afi][safi]      本端“已配置”该地址族（配置驱动）。
 *   afc_adv[afi][safi]  本端“已通告”该地址族（发送 OPEN 时置位）。
 *   afc_recv[afi][safi] 对端“已通告”该地址族（收到对端 MP 能力时置位）。
 *   afc_nego[afi][safi] 双方“已协商成功”该地址族（本端有配置且对端也通告才置位）。
 *   最终一条路由能不能发/收，看的是 afc_nego。
 *   peer->cap   记录“本端能力”的全局协商位（*_ADV 我方发出、*_RCV 对方发来）。
 *   peer->af_cap[afi][safi] 记录各地址族粒度上的协商位（如 Add-Path 的 TX/RX）。
 *
 * 【关键函数清单（按阅读顺序）】
 *   接收解析：peek_for_as4_capability -> bgp_open_option_parse -> bgp_capability_parse
 *             -> 各 bgp_capability_<cap>() 解析器 -> strict_capability_same /
 *                bgp_role_violation（一致性校验）
 *   发送构造：bgp_open_capability（总入口，逐条写 MP/Refresh/AS4/.../FQDN）
 *             -> bgp_open_capability_orf / bgp_peer_send_gr_capability /
 *                bgp_peer_send_llgr_capability（子能力）
 *   辅助表：  capcode_str（能力码->名字）、cap_minsizes / cap_modsizes（长度校验）
 *
 * 【建议阅读顺序】
 *   1) 先看 capcode_str / cap_minsizes / cap_modsizes 三张静态表，建立“能力码”印象。
 *   2) 读发送侧 bgp_open_capability：理解一条 Capability TLV 怎么写入流，以及
 *      ext_opt_params（RFC9072 扩展可选参数长度）如何影响每个长度字段的宽度。
 *   3) 读接收侧 bgp_capability_parse 主循环，再看它分派的各 bgp_capability_*。
 *   4) 读 bgp_open_option_parse 收尾：strict 比对、max_packet_size 决定、
 *      Role 校验、AFI/SAFI 是否重叠。
 * ============================================================================
 */

#include <zebra.h>

#include "linklist.h"
#include "prefix.h"
#include "stream.h"
#include "frrevent.h"
#include "log.h"
#include "command.h"
#include "memory.h"
#include "queue.h"
#include "filter.h"

#include "lib/json.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_open.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_memory.h"
#include "bgpd/bgp_trace.h"

/* ==========================================================================
 * 0. 能力码静态表：能力码 <-> 名称 / 最小长度 / 对齐模数
 *    —— 解析时用来查名字、做长度合法性检查的“查表数据”，改协议时只需改这里
 * ========================================================================== */

/* 能力码 -> 可读名称。lookup_msg() 用它把数字码翻译成人看的字符串（日志/调试）。 */
const struct message capcode_str[] = {
	{ CAPABILITY_CODE_MP, "MultiProtocol Extensions" },
	{ CAPABILITY_CODE_REFRESH, "Route Refresh" },
	{ CAPABILITY_CODE_ORF, "Cooperative Route Filtering" },
	{ CAPABILITY_CODE_RESTART, "Graceful Restart" },
	{ CAPABILITY_CODE_AS4, "4-octet AS number" },
	{ CAPABILITY_CODE_ADDPATH, "AddPath" },
	{ CAPABILITY_CODE_DYNAMIC, "Dynamic" },
	{ CAPABILITY_CODE_ENHE, "Extended Next Hop Encoding" },
	{ CAPABILITY_CODE_FQDN, "FQDN" },
	{ CAPABILITY_CODE_ENHANCED_RR, "Enhanced Route Refresh" },
	{ CAPABILITY_CODE_EXT_MESSAGE, "BGP Extended Message" },
	{ CAPABILITY_CODE_LLGR, "Long-lived BGP Graceful Restart" },
	{ CAPABILITY_CODE_ROLE, "Role" },
	{ CAPABILITY_CODE_SOFT_VERSION, "Software Version" },
	{ CAPABILITY_CODE_PATHS_LIMIT, "Paths-Limit" },
	{ CAPABILITY_CODE_LINK_LOCAL, "Link-Local Next Hop" },
	{ 0 }
};

/* Minimum sizes for length field of each cap (so not inc. the header) */
/* 每种能力“数据部分”（不含 code/length 两个头字节）的最小合法长度，用于
 * bgp_capability_parse 里的第一层长度校验：收到比这更短的能力即判非法、发通知。
 * 例如 MP 数据固定 4 字节，AS4 固定 4 字节，FQDN 至少 2 字节（home+domain 两个长度字节）。 */
const size_t cap_minsizes[] = {
	[CAPABILITY_CODE_MP] = CAPABILITY_CODE_MP_LEN,
	[CAPABILITY_CODE_REFRESH] = CAPABILITY_CODE_REFRESH_LEN,
	[CAPABILITY_CODE_ORF] = CAPABILITY_CODE_ORF_LEN,
	[CAPABILITY_CODE_RESTART] = CAPABILITY_CODE_RESTART_LEN,
	[CAPABILITY_CODE_AS4] = CAPABILITY_CODE_AS4_LEN,
	[CAPABILITY_CODE_ADDPATH] = CAPABILITY_CODE_ADDPATH_LEN,
	[CAPABILITY_CODE_DYNAMIC] = CAPABILITY_CODE_DYNAMIC_LEN,
	[CAPABILITY_CODE_ENHE] = CAPABILITY_CODE_ENHE_LEN,
	[CAPABILITY_CODE_FQDN] = CAPABILITY_CODE_MIN_FQDN_LEN,
	[CAPABILITY_CODE_ENHANCED_RR] = CAPABILITY_CODE_ENHANCED_LEN,
	[CAPABILITY_CODE_EXT_MESSAGE] = CAPABILITY_CODE_EXT_MESSAGE_LEN,
	[CAPABILITY_CODE_LLGR] = CAPABILITY_CODE_LLGR_LEN,
	[CAPABILITY_CODE_ROLE] = CAPABILITY_CODE_ROLE_LEN,
	[CAPABILITY_CODE_SOFT_VERSION] = CAPABILITY_CODE_SOFT_VERSION_LEN,
	[CAPABILITY_CODE_PATHS_LIMIT] = CAPABILITY_CODE_PATHS_LIMIT_LEN,
	[CAPABILITY_CODE_LINK_LOCAL] = CAPABILITY_CODE_LINK_LOCAL_LEN,
};

/* value the capability must be a multiple of.
 * 0-data capabilities won't be checked against this.
 * Other capabilities whose data doesn't fall on convenient boundaries for this
 * table should be set to 1.
 */
/* 每种能力“数据部分”长度必须整除的模数（第二层长度校验）。
 * 0 数据长度的能力（Refresh/Enhanced RR/Ext-Message/Link-Local）不参与该检查；
 * 长度不便对齐的项一律设 1（任何数都整除，等价于不检查）。
 * parse 中只在 caphdr.length 非 0 时才套用，故取值为 0 的项（如未列出的
 * LINK_LOCAL）不会造成除零或误判。 */
const size_t cap_modsizes[] = {
	[CAPABILITY_CODE_MP] = 4,
	[CAPABILITY_CODE_REFRESH] = 1,
	[CAPABILITY_CODE_ORF] = 1,
	[CAPABILITY_CODE_RESTART] = 1,
	[CAPABILITY_CODE_AS4] = 4,
	[CAPABILITY_CODE_ADDPATH] = 4,
	[CAPABILITY_CODE_DYNAMIC] = 1,
	[CAPABILITY_CODE_ENHE] = 6,
	[CAPABILITY_CODE_FQDN] = 1,
	[CAPABILITY_CODE_ENHANCED_RR] = 1,
	[CAPABILITY_CODE_EXT_MESSAGE] = 1,
	[CAPABILITY_CODE_LLGR] = 1,
	[CAPABILITY_CODE_ROLE] = 1,
	[CAPABILITY_CODE_SOFT_VERSION] = 1,
	[CAPABILITY_CODE_PATHS_LIMIT] = 5,
};

/* ==========================================================================
 * 1. 协商结果展示：把 OPEN 解析阶段记录的能力错误倒给 vty/JSON
 * ========================================================================== */

/* BGP-4 Multiprotocol Extensions lead us to the complex world. We can
   negotiate remote peer supports extensions or not. But if
   remote-peer doesn't supports negotiation process itself.  We would
   like to do manual configuration.

   So there is many configurable point.  First of all we want set each
   peer whether we send capability negotiation to the peer or not.
   Next, if we send capability to the peer we want to set my capability
   information at each peer. */

/* 【函数】bgp_capability_vty_out —— 把协商阶段记录的能力错误格式化输出到 vty/JSON。
 * 参数: vty/peer（取 peer->notify.data 里的错误缓冲）/use_json/json_neigh。
 * 数据来源: peer->notify.data —— 当 OPEN 里出现“不支持的 MP 能力码 / 未知能力码”时，
 *   bgp_capability_parse() 会把出错的 Capability TLV（含 2 字节头）原样拷进该缓冲，
 *   本函数再把它解析回来，逐条翻译成 AFI/SAFI/能力码的可读文本。
 * 调用时机: "show bgp neighbors" 展示邻居信息时（bgp_vty.c）。
 * 返回: 无。use_json 时结果挂到 json_neigh 的 "capabilityErrors" 下。
 * 关键分支: 循环里先做两次长度越界检查（hdr 头 2 字节、hdr 体），任一越界即 break
 *   并放弃 JSON 输出——防止对端构造畸形 notify 缓冲导致越界读。 */
void bgp_capability_vty_out(struct vty *vty, struct peer *peer, bool use_json,
			    json_object *json_neigh)
{
	char *pnt;
	char *end;
	struct capability_mp_data mpc;
	struct capability_header *hdr;
	json_object *json_cap = NULL;
	bool status_ok = true;

	if (use_json)
		json_cap = json_object_new_object();

	pnt = peer->notify.data;
	end = pnt + peer->notify.length;

	while (pnt < end) {
		if (pnt + sizeof(struct capability_mp_data) + 2 > end) {
			status_ok = false;
			break;
		}

		hdr = (struct capability_header *)pnt;
		if (pnt + hdr->length + 2 > end) {
			status_ok = false;
			break;
		}

		memcpy(&mpc, pnt + 2, sizeof(struct capability_mp_data));

		if (hdr->code == CAPABILITY_CODE_MP) {
			afi_t afi;
			safi_t safi;

			(void)bgp_map_afi_safi_iana2int(ntohs(mpc.afi),
							mpc.safi, &afi, &safi);

			if (use_json) {
				switch (afi) {
				case AFI_IP:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolAfi",
						"IPv4");
					break;
				case AFI_IP6:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolAfi",
						"IPv6");
					break;
				case AFI_L2VPN:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolAfi",
						"L2VPN");
					break;
				case AFI_UNSPEC:
				case AFI_MAX:
					json_object_int_add(
						json_cap,
						"capabilityErrorMultiProtocolAfiUnknown",
						ntohs(mpc.afi));
					break;
				}
				switch (safi) {
				case SAFI_UNICAST:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"unicast");
					break;
				case SAFI_MULTICAST:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"multicast");
					break;
				case SAFI_LABELED_UNICAST:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"labeled-unicast");
					break;
				case SAFI_MPLS_VPN:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"MPLS-labeled VPN");
					break;
				case SAFI_ENCAP:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"encap");
					break;
				case SAFI_EVPN:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"EVPN");
					break;
				case SAFI_FLOWSPEC:
					json_object_string_add(
						json_cap,
						"capabilityErrorMultiProtocolSafi",
						"flowspec");
					break;
				case SAFI_UNSPEC:
				case SAFI_MAX:
					json_object_int_add(
						json_cap,
						"capabilityErrorMultiProtocolSafiUnknown",
						mpc.safi);
					break;
				}
			} else {
				vty_out(vty,
					"  Capability error for: Multi protocol ");
				switch (afi) {
				case AFI_IP:
					vty_out(vty, "AFI IPv4, ");
					break;
				case AFI_IP6:
					vty_out(vty, "AFI IPv6, ");
					break;
				case AFI_L2VPN:
					vty_out(vty, "AFI L2VPN, ");
					break;
				case AFI_UNSPEC:
				case AFI_MAX:
					vty_out(vty, "AFI Unknown %d, ",
						ntohs(mpc.afi));
					break;
				}
				switch (safi) {
				case SAFI_UNICAST:
					vty_out(vty, "SAFI Unicast");
					break;
				case SAFI_MULTICAST:
					vty_out(vty, "SAFI Multicast");
					break;
				case SAFI_LABELED_UNICAST:
					vty_out(vty, "SAFI Labeled-unicast");
					break;
				case SAFI_MPLS_VPN:
					vty_out(vty, "SAFI MPLS-labeled VPN");
					break;
				case SAFI_ENCAP:
					vty_out(vty, "SAFI ENCAP");
					break;
				case SAFI_FLOWSPEC:
					vty_out(vty, "SAFI FLOWSPEC");
					break;
				case SAFI_EVPN:
					vty_out(vty, "SAFI EVPN");
					break;
				case SAFI_UNSPEC:
				case SAFI_MAX:
					vty_out(vty, "SAFI Unknown %d ",
						mpc.safi);
					break;
				}
				vty_out(vty, "\n");
			}
		} else if (hdr->code >= 128) {
			if (use_json)
				json_object_int_add(
					json_cap,
					"capabilityErrorVendorSpecificCapabilityCode",
					hdr->code);
			else
				vty_out(vty,
					"  Capability error: vendor specific capability code %d",
					hdr->code);
		} else {
			if (use_json)
				json_object_int_add(
					json_cap,
					"capabilityErrorUnknownCapabilityCode",
					hdr->code);
			else
				vty_out(vty,
					"  Capability error: unknown capability code %d",
					hdr->code);
		}
		pnt += hdr->length + 2;
	}

	if (status_ok) {
		if (use_json)
			json_object_object_add(json_neigh, "capabilityErrors",
					       json_cap);
	} else if (json_cap) {
		json_object_free(json_cap);
	}
}

/* ==========================================================================
 * 2. OPEN 接收 —— 各 Capability TLV 的解析器
 *    统一约定: 入参 (peer, connection, hdr)，从 BGP_INPUT(connection) 流里读取
 *    hdr->length 字节的数据部分；返回 0 成功、-1 失败（失败由 bgp_capability_parse
 *    统一发 OPEN Malformed 通知）。各解析器把协商结果写回 peer->cap / peer->af_cap /
 *    peer->afc_* 等“账本”。
 * ========================================================================== */

/* 【函数】bgp_capability_mp_data —— 从流里读出一个 MP 能力的公共头:
 *   AFI(16bit) + Reserved(8bit) + SAFI(8bit)，共 4 字节。MP 与 ORF 解析器共用。 */
static void bgp_capability_mp_data(struct stream *s, struct capability_mp_data *mpc)
{
	mpc->afi = stream_getw(s);
	mpc->reserved = stream_getc(s);
	mpc->safi = stream_getc(s);
}

/* 【函数】bgp_capability_mp —— 解析 MultiProtocol Extensions 能力 (码 1, RFC4760)。
 * 职责: 校验长度必须为 4；用 bgp_map_afi_safi_iana2int() 把对端通告的
 *   (IANA AFI, IANA SAFI) 转成内部枚举（失败即视为不支持该 AF）；
 * 返回: 0 成功；-1 表示长度非法 / AFI-SAFI 不支持 / 本端未配置该 AF。
 * 副作用（写“账本”）:
 *   afc_recv[afi][safi]=1  记录“对端通告了该 AF”（无论本端是否配置）；
 *   afc_nego[afi][safi]=1  仅当本端 afc[afi][safi] 也已配置时置位 —— 这一步才是
 *     MP-BGP 协商“成功”的判定，后续收发路由都看它。 */
/* Set negotiated capability value. */
static int bgp_capability_mp(struct peer *peer, struct peer_connection *connection,
			     struct capability_header *hdr)
{
	struct capability_mp_data mpc;
	struct stream *s = BGP_INPUT(connection);
	afi_t afi;
	safi_t safi;

	/* Verify length is 4 */
	if (hdr->length != 4) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_LENGTH,
			"MP Cap: Received invalid length %d, non-multiple of 4",
			hdr->length);
		return -1;
	}

	bgp_capability_mp_data(s, &mpc);

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s OPEN has %s capability for afi/safi: %s/%s",
			   peer->host, lookup_msg(capcode_str, hdr->code, NULL),
			   iana_afi2str(mpc.afi), iana_safi2str(mpc.safi));

	/* Convert AFI, SAFI to internal values, check. */
	if (bgp_map_afi_safi_iana2int(mpc.afi, mpc.safi, &afi, &safi))
		return -1;

	/* Now safi remapped, and afi/safi are valid array indices */
	/* 对端能力里出现的该 AF，先无条件记录为“已收到通告” */
	peer->afc_recv[afi][safi] = 1;

	/* 只有本端 afc 也已配置，才升级为“已协商成功”(afc_nego)，否则本 AF 谈不成 */
	if (peer->afc[afi][safi])
		peer->afc_nego[afi][safi] = 1;
	else
		return -1;

	return 0;
}

/* 【函数】bgp_capability_orf_not_support —— 纯日志辅助：某 AF 下的 ORF 类型/模式
 * 不被支持时打印一条 debug。本身无副作用，对应 RFC 里“忽略不理解的 ORF 项”的语义。 */
static void bgp_capability_orf_not_support(struct peer *peer, iana_afi_t afi,
					   iana_safi_t safi, uint8_t type,
					   uint8_t mode)
{
	if (bgp_debug_neighbor_events(peer))
		zlog_debug(
			"%s Addr-family %d/%d has ORF type/mode %d/%d not supported",
			peer->host, afi, safi, type, mode);
}

/* ORF 类型码 -> 名称 / ORF 模式码 -> 名称，供日志 lookup_msg 使用。 */
const struct message orf_type_str[] = { { ORF_TYPE_RESERVED, "Reserved" },
					{ ORF_TYPE_PREFIX, "Prefixlist" },
					{ 0 } };

const struct message orf_mode_str[] = { { ORF_MODE_RECEIVE, "Receive" },
					{ ORF_MODE_SEND, "Send" },
					{ ORF_MODE_BOTH, "Both" },
					{ 0 } };

/* 【函数】bgp_capability_orf_entry —— 解析 ORF (Cooperative Route Filtering) 能力
 * 的一个 entry (码 3, RFC5291)。
 * 结构: MP 公共头(4B) + Number(1B) + Number 个 (Type 1B, Mode 1B)。
 * 支持范围: 仅 ORF_TYPE_PREFIX(64)，且仅 IPv4-unicast / IPv4-multicast / IPv6-unicast。
 * 协商位（写 peer->af_cap[afi][safi]）:
 *   sm_cap = PEER_CAP_ORF_PREFIX_SM_RCV（我方作为“发送方”收到对端请求）
 *   rm_cap = PEER_CAP_ORF_PREFIX_RM_RCV（我方作为“接收方”）
 *   Mode=BOTH 两位都置，Mode=SEND 只置 sm，Mode=RECEIVE 只置 rm。
 * 返回: 0；Number 字段与 hdr->length 不自洽时返回 -1（发 MALFORMED 通知）。 */
static int bgp_capability_orf_entry(struct peer *peer, struct peer_connection *connection,
				    struct capability_header *hdr)
{
	struct stream *s = BGP_INPUT(connection);
	struct capability_mp_data mpc;
	uint8_t num;
	iana_afi_t pkt_afi;
	afi_t afi;
	iana_safi_t pkt_safi;
	safi_t safi;
	uint8_t type;
	uint8_t mode;
	uint16_t sm_cap = 0; /* capability send-mode receive */
	uint16_t rm_cap = 0; /* capability receive-mode receive */
	int i;

	/* ORF Entry header */
	bgp_capability_mp_data(s, &mpc);
	num = stream_getc(s);
	pkt_afi = mpc.afi;
	pkt_safi = mpc.safi;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s ORF Cap entry for afi/safi: %s/%s", peer->host,
			   iana_afi2str(mpc.afi), iana_safi2str(mpc.safi));

	/* Convert AFI, SAFI to internal values, check. */
	if (bgp_map_afi_safi_iana2int(pkt_afi, pkt_safi, &afi, &safi)) {
		zlog_info(
			"%s Addr-family %d/%d not supported. Ignoring the ORF capability",
			peer->host, pkt_afi, pkt_safi);
		return 0;
	}

	mpc.afi = pkt_afi;
	mpc.safi = safi;

	/* validate number field */
	if (CAPABILITY_CODE_ORF_LEN + (num * 2) > hdr->length) {
		zlog_info(
			"%s ORF Capability entry length error, Cap length %u, num %u",
			peer->host, hdr->length, num);
		bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR, BGP_NOTIFY_OPEN_MALFORMED_ATTR);
		return -1;
	}

	for (i = 0; i < num; i++) {
		type = stream_getc(s);
		mode = stream_getc(s);

		/* ORF Mode error check */
		switch (mode) {
		case ORF_MODE_BOTH:
		case ORF_MODE_SEND:
		case ORF_MODE_RECEIVE:
			break;
		default:
			bgp_capability_orf_not_support(peer, pkt_afi, pkt_safi,
						       type, mode);
			continue;
		}
		/* ORF Type and afi/safi error checks */
		/* capcode versus type */
		switch (hdr->code) {
		case CAPABILITY_CODE_ORF:
			switch (type) {
			case ORF_TYPE_RESERVED:
				if (bgp_debug_neighbor_events(peer))
					zlog_debug(
						"%s Addr-family %d/%d has reserved ORF type, ignoring",
						peer->host, afi, safi);
				break;
			case ORF_TYPE_PREFIX:
				break;
			default:
				bgp_capability_orf_not_support(
					peer, pkt_afi, pkt_safi, type, mode);
				continue;
			}
			break;
		default:
			bgp_capability_orf_not_support(peer, pkt_afi, pkt_safi,
						       type, mode);
			continue;
		}

		/* AFI vs SAFI */
		if (!((afi == AFI_IP && safi == SAFI_UNICAST)
		      || (afi == AFI_IP && safi == SAFI_MULTICAST)
		      || (afi == AFI_IP6 && safi == SAFI_UNICAST))) {
			bgp_capability_orf_not_support(peer, pkt_afi, pkt_safi,
						       type, mode);
			continue;
		}

		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%s OPEN has %s ORF capability as %s for afi/safi: %s/%s",
				peer->host,
				lookup_msg(orf_type_str, type, NULL),
				lookup_msg(orf_mode_str, mode, NULL),
				iana_afi2str(pkt_afi), iana_safi2str(pkt_safi));

		if (hdr->code == CAPABILITY_CODE_ORF) {
			sm_cap = PEER_CAP_ORF_PREFIX_SM_RCV;
			rm_cap = PEER_CAP_ORF_PREFIX_RM_RCV;
		} else {
			bgp_capability_orf_not_support(peer, pkt_afi, pkt_safi,
						       type, mode);
			continue;
		}

		switch (mode) {
		case ORF_MODE_BOTH:
			SET_FLAG(peer->af_cap[afi][safi], sm_cap);
			SET_FLAG(peer->af_cap[afi][safi], rm_cap);
			break;
		case ORF_MODE_SEND:
			SET_FLAG(peer->af_cap[afi][safi], sm_cap);
			break;
		case ORF_MODE_RECEIVE:
			SET_FLAG(peer->af_cap[afi][safi], rm_cap);
			break;
		}
	}
	return 0;
}

/* 【函数】bgp_capability_restart —— 解析 Graceful Restart 能力 (码 64, RFC4724/8538)。
 * 结构: 前 2 字节 = Restart Flags + Time（最高位 R=重启态、次高位 N=优雅通知、
 *   低 12 位 = Restart Time 秒）；随后每 4 字节一个 (AFI 2B, SAFI 1B, Flags 1B) 条目，
 *   条目的 F 位=“转发保持(Forwarding Preserved)”。
 * 写回: peer->cap 的 PEER_CAP_RESTART_RCV 及 R/N 位；peer->v_gr_restart；
 *   peer->af_cap[afi][safi] 的 RESTART_AF_RCV / RESTART_AF_PRESERVE_RCV。
 * 返回: 0；长度非 4 倍数时 -1（发长度错误告警）。 */
static int bgp_capability_restart(struct peer *peer, struct peer_connection *connection,
				  struct capability_header *caphdr)
{
	struct stream *s = BGP_INPUT(connection);
	uint16_t restart_flag_time;
	size_t end = stream_get_getp(s) + caphdr->length;

	/* Verify length is a multiple of 4 */
	if ((caphdr->length - 2) % 4) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_LENGTH,
			"Restart Cap: Received invalid length %d, non-multiple of 4",
			caphdr->length);
		return -1;
	}

	SET_FLAG(peer->cap, PEER_CAP_RESTART_RCV);
	restart_flag_time = stream_getw(s);

	/* The most significant bit is defined in [RFC4724] as
	 * the Restart State ("R") bit.
	 */
	if (CHECK_FLAG(restart_flag_time, GRACEFUL_RESTART_R_BIT))
		SET_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_RCV);
	else
		UNSET_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_RCV);

	/* The second most significant bit is defined in this
	 * document as the Graceful Notification ("N") bit.
	 */
	if (CHECK_FLAG(restart_flag_time, GRACEFUL_RESTART_N_BIT))
		SET_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_N_BIT_RCV);
	else
		UNSET_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_N_BIT_RCV);

	UNSET_FLAG(restart_flag_time, 0xF000);
	peer->v_gr_restart = restart_flag_time;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%pBP OPEN has GR capability, Restart time %d R-bit %s N-bit %s",
			   peer, peer->v_gr_restart,
			   CHECK_FLAG(peer->cap,
				      PEER_CAP_GRACEFUL_RESTART_R_BIT_RCV)
				   ? "SET"
				   : "NOT-SET",
			   CHECK_FLAG(peer->cap,
				      PEER_CAP_GRACEFUL_RESTART_N_BIT_RCV)
				   ? "SET"
				   : "NOT-SET");

	while (stream_get_getp(s) + 4 <= end) {
		afi_t afi;
		safi_t safi;
		iana_afi_t pkt_afi = stream_getw(s);
		iana_safi_t pkt_safi = stream_getc(s);
		uint8_t flag = stream_getc(s);

		/* Convert AFI, SAFI to internal values, check. */
		if (bgp_map_afi_safi_iana2int(pkt_afi, pkt_safi, &afi, &safi)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not supported. Ignore the Graceful Restart capability for this AFI/SAFI",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
		} else if (!peer->afc[afi][safi]) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not enabled. Ignore the Graceful Restart capability",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
		} else {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug("%pBP F-bit %s for %s", peer,
					   CHECK_FLAG(flag, GRACEFUL_RESTART_F_BIT) ? "SET"
										    : "NOT-SET",
					   get_afi_safi_str(afi, safi, false));

			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_RESTART_AF_RCV);
			/* F 位对端置位才记录“该 AF 具备转发保持能力”，否则我在 GR 里
			 * 重启后不能保留该 AF 转发表项 */
			if (CHECK_FLAG(flag, GRACEFUL_RESTART_F_BIT))
				SET_FLAG(peer->af_cap[afi][safi],
					 PEER_CAP_RESTART_AF_PRESERVE_RCV);
		}
	}
	return 0;
}

/* 【函数】bgp_capability_llgr —— 解析 Long-Lived Graceful Restart 能力 (码 71, RFC9494)。
 * 结构: 每 7 字节一个条目 = AFI 2B + SAFI 1B + Flags 1B + Stale Time 3B。
 * 前提: 仅当该 AF 对端同时通告了 GR(PEER_CAP_RESTART_AF_RCV) 才接受，否则忽略。
 * 写回: peer->cap 的 PEER_CAP_LLGR_RCV；peer->llgr[afi][safi]（stale_time 会与本地
 *   bgp->llgr_stale_time 取 MIN，防止对端给过长时间）；af_cap 的 LLGR_AF_RCV。
 * 返回: 总是 0（单条不合法的 AF 只跳过，不致命）。 */
static int bgp_capability_llgr(struct peer *peer, struct peer_connection *connection,
			       struct capability_header *caphdr)
{
	struct stream *s = BGP_INPUT(connection);
	size_t end = stream_get_getp(s) + caphdr->length;

	SET_FLAG(peer->cap, PEER_CAP_LLGR_RCV);

	while (stream_get_getp(s) + BGP_CAP_LLGR_MIN_PACKET_LEN <= end) {
		afi_t afi;
		safi_t safi;
		iana_afi_t pkt_afi = stream_getw(s);
		iana_safi_t pkt_safi = stream_getc(s);
		uint8_t flags = stream_getc(s);
		uint32_t stale_time = stream_get3(s);

		if (bgp_map_afi_safi_iana2int(pkt_afi, pkt_safi, &afi, &safi)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not supported. Ignore the Long-lived Graceful Restart capability for this AFI/SAFI",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
		} else if (!peer->afc[afi][safi]
			   || !CHECK_FLAG(peer->af_cap[afi][safi],
					  PEER_CAP_RESTART_AF_RCV)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not enabled. Ignore the Long-lived Graceful Restart capability",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
		} else {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) Long-lived Graceful Restart capability stale time %u sec",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi), stale_time);

			peer->llgr[afi][safi].flags = flags;
			peer->llgr[afi][safi].stale_time =
				MIN(stale_time, peer->bgp->llgr_stale_time);
			SET_FLAG(peer->af_cap[afi][safi], PEER_CAP_LLGR_AF_RCV);
		}
	}

	return 0;
}

/* 【函数】bgp_capability_as4 —— 解析 4 字节 AS 能力 (码 65, RFC6793)。
 * 返回: 对端 AS4 号（as_t）；长度非法或对端 AS4 值为 0 时返回 0 表示失败。
 * 特殊约定（见下方英文注释）: 其它解析器返回 0 表示成功，本函数 0 表示“失败”，
 *   因为成功时要把 AS4 号作为返回值带回调用者（peek_for_as4_capability 依赖它）。
 * 副作用: 解析成功即置 peer->cap 的 PEER_CAP_AS4_RCV。 */
/* Unlike other capability parsing routines, this one returns 0 on error */
static as_t bgp_capability_as4(struct peer *peer, struct peer_connection *connection,
			       struct capability_header *hdr)
{
	if (hdr->length != CAPABILITY_CODE_AS4_LEN) {
		flog_err(EC_BGP_PKT_OPEN,
			 "%s AS4 capability has incorrect data length %d",
			 peer->host, hdr->length);
		return 0;
	}

	as_t as4 = stream_getl(BGP_INPUT(connection));

	SET_FLAG(peer->cap, PEER_CAP_AS4_RCV);

	if (BGP_DEBUG(as4, AS4))
		zlog_debug(
			"%s [AS4] about to set cap PEER_CAP_AS4_RCV, got as4 %u",
			peer->host, as4);
	return as4;
}

/* 【函数】bgp_capability_ext_message —— 解析 Extended Message 能力 (码 6, RFC8092)。
 * 长度必须为 0；成功即置 PEER_CAP_EXTENDED_MESSAGE_RCV，含义是对端允许 BGP 报文
 * 最大到 65535 字节。真正放大 peer->max_packet_size 的动作在 bgp_open_option_parse
 * 收尾处，且要求双方都通告(ADV&RCV)才生效。 */
static int bgp_capability_ext_message(struct peer *peer, struct peer_connection *connection,
				      struct capability_header *hdr)
{
	if (hdr->length != CAPABILITY_CODE_EXT_MESSAGE_LEN) {
		flog_err(
			EC_BGP_PKT_OPEN,
			"%s: BGP Extended Message capability has incorrect data length %d",
			peer->host, hdr->length);
		return -1;
	}

	SET_FLAG(peer->cap, PEER_CAP_EXTENDED_MESSAGE_RCV);

	return 0;
}

/* 【函数】bgp_capability_addpath —— 解析 Add-Path 能力 (码 69, RFC7911)。
 * 结构: 每 4 字节一个 (AFI 2B, SAFI 1B, Send/Receive 1B)；Send/Receive 合法值 1..3。
 *   位 BGP_ADDPATH_RX(1) = 对端声明“我能接收多条路径”→ 于是本端可对其发送(本端 TX)；
 *   位 BGP_ADDPATH_TX(2) = 对端声明“我会发送多条路径”→ 于是本端可接收(本端 RX)。
 * 写回 peer->af_cap[afi][safi]: 收到 RX 位置 ADDPATH_AF_RX_RCV，收到 TX 位置 ADDPATH_AF_TX_RCV。
 * 返回: 0；长度非 4 倍数时 -1。非法 Send/Receive 值只跳过该条（按 RFC 要求当作未理解）。 */
static int bgp_capability_addpath(struct peer *peer, struct peer_connection *connection,
				  struct capability_header *hdr)
{
	struct stream *s = BGP_INPUT(connection);
	size_t end = stream_get_getp(s) + hdr->length;

	/* Verify length is a multiple of 4 */
	if (hdr->length % CAPABILITY_CODE_ADDPATH_LEN) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_LENGTH,
			"Add Path: Received invalid length %d, non-multiple of 4",
			hdr->length);
		return -1;
	}

	SET_FLAG(peer->cap, PEER_CAP_ADDPATH_RCV);

	while (stream_get_getp(s) + CAPABILITY_CODE_ADDPATH_LEN <= end) {
		afi_t afi;
		safi_t safi;
		iana_afi_t pkt_afi = stream_getw(s);
		iana_safi_t pkt_safi = stream_getc(s);
		uint8_t send_receive = stream_getc(s);

		/* If any other value (other than 1-3) is received, then
		 * the capability SHOULD be treated as not understood
		 * and ignored.
		 */
		if (!send_receive || send_receive > 3) {
			flog_warn(EC_BGP_CAPABILITY_INVALID_DATA,
				  "Add Path: Received invalid send/receive value %u in Add Path capability",
				  send_receive);
			continue;
		}

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s OPEN has %s capability for afi/safi: %s/%s%s%s",
				   peer->host,
				   lookup_msg(capcode_str, hdr->code, NULL),
				   iana_afi2str(pkt_afi),
				   iana_safi2str(pkt_safi),
				   CHECK_FLAG(send_receive, BGP_ADDPATH_RX)
					   ? ", receive"
					   : "",
				   CHECK_FLAG(send_receive, BGP_ADDPATH_TX)
					   ? ", transmit"
					   : "");

		/* Convert AFI, SAFI to internal values, check. */
		if (bgp_map_afi_safi_iana2int(pkt_afi, pkt_safi, &afi, &safi)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not supported. Ignore the Addpath Attribute for this AFI/SAFI",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
			continue;
		} else if (!peer->afc[afi][safi]) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not enabled. Ignore the AddPath capability for this AFI/SAFI",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
			continue;
		}

		if (CHECK_FLAG(send_receive, BGP_ADDPATH_RX))
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ADDPATH_AF_RX_RCV);
		else
			UNSET_FLAG(peer->af_cap[afi][safi],
				   PEER_CAP_ADDPATH_AF_RX_RCV);

		if (CHECK_FLAG(send_receive, BGP_ADDPATH_TX))
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ADDPATH_AF_TX_RCV);
		else
			UNSET_FLAG(peer->af_cap[afi][safi],
				   PEER_CAP_ADDPATH_AF_TX_RCV);
	}

	return 0;
}

/* 【函数】bgp_capability_paths_limit —— 解析 Paths-Limit 能力 (码 76, RFC 草案)。
 * 结构: 每 5 字节一个 (AFI 2B, SAFI 1B, Paths-Limit 2B)。
 * 前提: 必须先收到对端的 Add-Path 能力(PEER_CAP_ADDPATH_RCV)，否则直接返回 -1
 *   （Paths-Limit 依赖 Add-Path，二者不能单独出现）。
 * 写回: peer->cap 的 PATHS_LIMIT_RCV；af_cap 的 PATHS_LIMIT_AF_RCV；
 *   peer->addpath_paths_limit[afi][safi].receive = 对端允许我发多路径的上限。
 * 返回: 0；长度非 5 倍数或缺少 Add-Path 时 -1。 */
static int bgp_capability_paths_limit(struct peer *peer, struct peer_connection *connection,
				      struct capability_header *hdr)
{
	struct stream *s = BGP_INPUT(connection);
	size_t end = stream_get_getp(s) + hdr->length;

	if (hdr->length % CAPABILITY_CODE_PATHS_LIMIT_LEN) {
		flog_warn(EC_BGP_CAPABILITY_INVALID_LENGTH,
			  "Paths-Limit: Received invalid length %d, non-multiple of %d",
			  hdr->length, CAPABILITY_CODE_PATHS_LIMIT_LEN);
		return -1;
	}

	if (!CHECK_FLAG(peer->cap, PEER_CAP_ADDPATH_RCV)) {
		flog_warn(EC_BGP_CAPABILITY_INVALID_DATA,
			  "Paths-Limit: Received Paths-Limit capability without Add-Path capability");
		return -1;
	}

	SET_FLAG(peer->cap, PEER_CAP_PATHS_LIMIT_RCV);

	while (stream_get_getp(s) + CAPABILITY_CODE_PATHS_LIMIT_LEN <= end) {
		afi_t afi;
		safi_t safi;
		iana_afi_t pkt_afi = stream_getw(s);
		iana_safi_t pkt_safi = stream_getc(s);
		uint16_t paths_limit = stream_getw(s);

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s OPEN has %s capability for afi/safi: %s/%s limit: %u",
				   peer->host,
				   lookup_msg(capcode_str, hdr->code, NULL),
				   iana_afi2str(pkt_afi),
				   iana_safi2str(pkt_safi), paths_limit);

		if (bgp_map_afi_safi_iana2int(pkt_afi, pkt_safi, &afi, &safi)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug("%s Addr-family %s/%s(afi/safi) not supported. Ignore the Paths-Limit capability for this AFI/SAFI",
					   peer->host, iana_afi2str(pkt_afi),
					   iana_safi2str(pkt_safi));
			continue;
		} else if (!peer->afc[afi][safi]) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug("%s Addr-family %s/%s(afi/safi) not enabled. Ignore the Paths-Limit capability for this AFI/SAFI",
					   peer->host, iana_afi2str(pkt_afi),
					   iana_safi2str(pkt_safi));
			continue;
		}

		SET_FLAG(peer->af_cap[afi][safi], PEER_CAP_PATHS_LIMIT_AF_RCV);
		peer->addpath_paths_limit[afi][safi].receive = paths_limit;
	}

	return 0;
}

/* 【函数】bgp_capability_enhe —— 解析 Extended Next Hop Encoding 能力 (码 5, RFC5549)。
 * 结构: 每 6 字节一个 (NLRI AFI 2B, NLRI SAFI 2B, Nexthop AFI 2B)。
 * 取值范围: 仅允许 NLRI AFI=IPv4 且 Nexthop AFI=IPv6，SAFI 仅 unicast/MPLS_VPN/
 *   labeled-unicast；其它组合记日志后忽略（未来规范可能扩展）。
 * 写回: 先置该 AF 的 PEER_CAP_ENHE_AF_RCV；若我方也已发过 ADV，则再置 NEGO
 *   （表示该 AF 的扩展下一跳最终谈成，收/发路由时据此用 IPv6 地址作为 IPv4 下一跳）。
 * 收尾: 只要收到任何一条就置 peer->cap 的 PEER_CAP_ENHE_RCV。
 * 返回: 0；长度非 6 倍数时 -1。 */
static int bgp_capability_enhe(struct peer *peer, struct peer_connection *connection,
			       struct capability_header *hdr)
{
	struct stream *s = BGP_INPUT(connection);
	size_t end = stream_get_getp(s) + hdr->length;

	/* Verify length is a multiple of 4 */
	if (hdr->length % 6) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_LENGTH,
			"Extended NH: Received invalid length %d, non-multiple of 6",
			hdr->length);
		return -1;
	}

	while (stream_get_getp(s) + 6 <= end) {
		iana_afi_t pkt_afi = stream_getw(s);
		afi_t afi;
		iana_safi_t pkt_safi = stream_getw(s);
		safi_t safi;
		iana_afi_t pkt_nh_afi = stream_getw(s);
		afi_t nh_afi;

		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%s Received with afi/safi/next-hop afi: %s/%s/%u",
				peer->host, iana_afi2str(pkt_afi),
				iana_safi2str(pkt_safi), pkt_nh_afi);

		/* Convert AFI, SAFI to internal values, check. */
		if (bgp_map_afi_safi_iana2int(pkt_afi, pkt_safi, &afi, &safi)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s Addr-family %s/%s(afi/safi) not supported. Ignore the ENHE Attribute for this AFI/SAFI",
					peer->host, iana_afi2str(pkt_afi),
					iana_safi2str(pkt_safi));
			continue;
		}

		/* RFC 5549 specifies use of this capability only for IPv4 AFI,
		 * with
		 * the Nexthop AFI being IPv6. A future spec may introduce other
		 * possibilities, so we ignore other values with a log. Also,
		 * only
		 * SAFI_UNICAST and SAFI_LABELED_UNICAST are currently supported
		 * (and expected).
		 */
		nh_afi = afi_iana2int(pkt_nh_afi);

		if (afi != AFI_IP || nh_afi != AFI_IP6
		    || !(safi == SAFI_UNICAST || safi == SAFI_MPLS_VPN
			 || safi == SAFI_LABELED_UNICAST)) {
			flog_warn(
				EC_BGP_CAPABILITY_INVALID_DATA,
				"%s Unexpected afi/safi/next-hop afi: %s/%s/%u in Extended Next-hop capability, ignoring",
				peer->host, iana_afi2str(pkt_afi),
				iana_safi2str(pkt_safi), pkt_nh_afi);
			continue;
		}

		SET_FLAG(peer->af_cap[afi][safi], PEER_CAP_ENHE_AF_RCV);

		if (CHECK_FLAG(peer->af_cap[afi][safi], PEER_CAP_ENHE_AF_ADV))
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ENHE_AF_NEGO);
	}

	SET_FLAG(peer->cap, PEER_CAP_ENHE_RCV);

	return 0;
}

/* 【函数】bgp_capability_hostname —— 解析 FQDN 能力 (码 73, draft-ietf-idr-fqdn-capability)。
 * 结构: 主机名长度(1B)+主机名字节+域名长度(1B)+域名字节（域名部分可缺省）。
 * 写回: peer->hostname / peer->domainname（先释放旧值再复制，长度超 BGP_MAX_HOSTNAME
 *   时截断），并置 PEER_CAP_HOSTNAME_RCV。
 * 返回: 0；任何长度越界/空主机名时 -1（并回滚已分配的主机名，避免半写入状态）。
 * 安全: 全程用 stream_get_getp 相对 end 做越界检查，严防对端构造超长/截断字段。 */
static int bgp_capability_hostname(struct peer *peer, struct peer_connection *connection,
				   struct capability_header *hdr)
{
	struct stream *s = BGP_INPUT(connection);
	char str[BGP_MAX_HOSTNAME + 1];
	size_t end = stream_get_getp(s) + hdr->length;
	uint8_t len;

	if (stream_get_getp(s) + 1 > end) {
		flog_warn(EC_BGP_CAPABILITY_INVALID_DATA,
			  "%s: Received invalid hostname len (hostname capability) from peer %s",
			  __func__, peer->host);
		return -1;
	}

	len = stream_getc(s);
	if (!len) {
		flog_warn(EC_BGP_CAPABILITY_INVALID_DATA,
			  "%s: Received empty hostname from peer %s", __func__, peer->host);
		return -1;
	}

	if (stream_get_getp(s) + len > end) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_DATA,
			"%s: Received malformed hostname capability from peer %s",
			__func__, peer->host);
		return -1;
	}

	if (len > BGP_MAX_HOSTNAME) {
		stream_get(str, s, BGP_MAX_HOSTNAME);
		stream_forward_getp(s, len - BGP_MAX_HOSTNAME);
		len = BGP_MAX_HOSTNAME; /* to set the '\0' below */
	} else {
		stream_get(str, s, len);
	}

	str[len] = '\0';

	XFREE(MTYPE_BGP_PEER_HOST, peer->hostname);
	XFREE(MTYPE_BGP_PEER_HOST, peer->domainname);

	peer->hostname = XSTRDUP(MTYPE_BGP_PEER_HOST, str);

	if (stream_get_getp(s) + 1 > end) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_DATA,
			"%s: Received invalid domain name len (hostname capability) from peer %s",
			__func__, peer->host);
		XFREE(MTYPE_BGP_PEER_HOST, peer->hostname);
		return -1;
	}

	len = stream_getc(s);
	if (stream_get_getp(s) + len > end) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_DATA,
			"%s: Received runt domain name (hostname capability) from peer %s",
			__func__, peer->host);
		XFREE(MTYPE_BGP_PEER_HOST, peer->hostname);
		return -1;
	}

	if (len > BGP_MAX_HOSTNAME) {
		stream_get(str, s, BGP_MAX_HOSTNAME);
		stream_forward_getp(s, len - BGP_MAX_HOSTNAME);
		len = BGP_MAX_HOSTNAME; /* to set the '\0' below */
	} else if (len)
		stream_get(str, s, len);

	if (len) {
		str[len] = '\0';

		XFREE(MTYPE_BGP_PEER_HOST, peer->domainname);

		peer->domainname = XSTRDUP(MTYPE_BGP_PEER_HOST, str);
	}

	SET_FLAG(peer->cap, PEER_CAP_HOSTNAME_RCV);

	if (bgp_debug_neighbor_events(peer)) {
		zlog_debug("%s received hostname %s, domainname %s", peer->host,
			   peer->hostname, peer->domainname);
	}

	return 0;
}

/* 【函数】bgp_capability_role —— 解析 Role 能力 (码 9, RFC9234)。
 * 长度必须为 1；读出的 role 仅记录到 peer->remote_role，并置 PEER_CAP_ROLE_RCV。
 * 真正的“角色是否匹配”检查不在这里，而在 bgp_role_violation()（协商收尾时统一做）。 */
static int bgp_capability_role(struct peer *peer, struct peer_connection *connection,
			       struct capability_header *hdr)
{
	if (hdr->length != CAPABILITY_CODE_ROLE_LEN) {
		flog_warn(EC_BGP_CAPABILITY_INVALID_LENGTH,
			  "Role: Received invalid length %d", hdr->length);
		return -1;
	}

	uint8_t role = stream_getc(BGP_INPUT(connection));

	SET_FLAG(peer->cap, PEER_CAP_ROLE_RCV);

	peer->remote_role = role;
	return 0;
}

/* 【函数】bgp_capability_software_version —— 解析 Software Version 能力 (码 75, RFC 草案)。
 * 兼容性要点（见下方英文注释）: 早期草案在能力“值”内部又带了一个长度字节，新版直接用
 *   能力长度字段。这里用 stream_dup 先窥探第一个字节，若它恰好等于 hdr->length 说明是
 *   旧格式，需要多读一个长度字节(len = stream_getc(s))。
 * 写回: peer->soft_version（超 BGP_MAX_SOFT_VERSION 截断），置 PEER_CAP_SOFT_VERSION_RCV。
 * 返回: 0；长度越界时 -1。 */
static int bgp_capability_software_version(struct peer *peer, struct peer_connection *connection,
					   struct capability_header *hdr)
{
	struct stream *s = BGP_INPUT(connection);
	struct stream *dup = stream_dup(s);
	char str[BGP_MAX_SOFT_VERSION + 1];
	size_t end = stream_get_getp(s) + hdr->length;
	uint8_t len = hdr->length;
	uint8_t cap_value_len_field = stream_getc(dup) + 1;

	/* For backward compatibility.
	 * Older draft versions defined the length field inside
	 * the capability's value. Newer versions use just the capability's
	 * length which is hdr->length.
	 */
	if (cap_value_len_field == len)
		len = stream_getc(s);

	stream_free(dup);

	if (stream_get_getp(s) + len > end) {
		flog_warn(
			EC_BGP_CAPABILITY_INVALID_DATA,
			"%s: Received malformed Software Version capability from peer %s",
			__func__, peer->host);
		return -1;
	}

	SET_FLAG(peer->cap, PEER_CAP_SOFT_VERSION_RCV);

	if (len > BGP_MAX_SOFT_VERSION) {
		flog_warn(EC_BGP_CAPABILITY_INVALID_LENGTH,
			  "%s: Received Software Version, but the length is too big, truncating, from peer %s",
			  __func__, peer->host);
		stream_get(str, s, BGP_MAX_SOFT_VERSION);
		stream_forward_getp(s, len - BGP_MAX_SOFT_VERSION);
		len = BGP_MAX_SOFT_VERSION;
	} else if (len) {
		stream_get(str, s, len);
	}

	if (len) {
		str[len] = '\0';

		XFREE(MTYPE_BGP_SOFT_VERSION, peer->soft_version);

		peer->soft_version = XSTRDUP(MTYPE_BGP_SOFT_VERSION, str);

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s received Software Version: %s",
				   peer->host, peer->soft_version);
	}

	return 0;
}

/* 【函数】bgp_capability_parse —— OPEN 能力解析主循环（接收侧核心）。
 * 参数:
 *   length        本段可选参数(能力集合)的字节数；
 *   mp_capability 出参，只要出现 ≥1 个 MP 能力就置 1（供调用者判断“是否需检查 AF 重叠”）；
 *   error/error_end 出参“错误缓冲”指针对，遇到“不支持的 MP 能力 / 未知能力码”时，
 *     把该 Capability TLV 原样拷进来，最后回填给对端发 UNSUP_CAPBL 通知。
 * 流程: 逐个读 (code,length) 头 -> 头与体各做一次长度越界检查（越界即发 MALFORMED 通知）
 *   -> 对已知能力按 cap_minsizes/cap_modsizes 做长度校验 -> switch 分派到各解析器
 *   -> 若解析器移动了读指针位置(未等于 start+length)，强制 set_getp 对齐到本能力末尾。
 * 返回: 0 正常；-1 表示严重错误（已发通知，应中止 OPEN 处理）。
 * 注意: 单独一个能力解析失败（ret<0）也会在这里统一发 OPEN MALFORMED 通知并返回 -1。 */
/**
 * Parse given capability.
 * XXX: This is reading into a stream, but not using stream API
 *
 * @param[out] mp_capability Set to 1 on return iff one or more Multiprotocol
 *                           capabilities were encountered.
 */
static int bgp_capability_parse(struct peer *peer, struct peer_connection *connection,
				size_t length, int *mp_capability, uint8_t **error,
				uint8_t *error_end)
{
	int ret;
	struct stream *s = BGP_INPUT(connection);
	size_t end = stream_get_getp(s) + length;
	uint16_t restart_flag_time = 0;

	assert(STREAM_READABLE(s) >= length);

	while (stream_get_getp(s) < end) {
		size_t start;
		uint8_t *sp = stream_pnt(s);
		struct capability_header caphdr;

		ret = 0;
		/* We need at least capability code and capability length. */
		if (stream_get_getp(s) + 2 > end) {
			zlog_info("%s Capability length error (< header)",
				  peer->host);
			bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
					BGP_NOTIFY_OPEN_MALFORMED_ATTR);
			return -1;
		}

		caphdr.code = stream_getc(s);
		caphdr.length = stream_getc(s);
		start = stream_get_getp(s);

		/* Capability length check sanity check. */
		if (start + caphdr.length > end) {
			zlog_info("%s Capability length error (< length)",
				  peer->host);
			bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
					BGP_NOTIFY_OPEN_MALFORMED_ATTR);
			return -1;
		}

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s OPEN has %s capability (%u), length %u",
				   peer->host,
				   lookup_msg(capcode_str, caphdr.code, NULL),
				   caphdr.code, caphdr.length);

		/* Length sanity check, type-specific, for known capabilities */
		switch (caphdr.code) {
		case CAPABILITY_CODE_MP:
		case CAPABILITY_CODE_REFRESH:
		case CAPABILITY_CODE_ORF:
		case CAPABILITY_CODE_RESTART:
		case CAPABILITY_CODE_AS4:
		case CAPABILITY_CODE_ADDPATH:
		case CAPABILITY_CODE_DYNAMIC:
		case CAPABILITY_CODE_ENHE:
		case CAPABILITY_CODE_FQDN:
		case CAPABILITY_CODE_ENHANCED_RR:
		case CAPABILITY_CODE_EXT_MESSAGE:
		case CAPABILITY_CODE_ROLE:
		case CAPABILITY_CODE_SOFT_VERSION:
		case CAPABILITY_CODE_PATHS_LIMIT:
			/* Check length. */
			if (caphdr.length < cap_minsizes[caphdr.code]) {
				zlog_info(
					"%s %s Capability length error: got %u, expected at least %u",
					peer->host,
					lookup_msg(capcode_str, caphdr.code,
						   NULL),
					caphdr.length,
					(unsigned)cap_minsizes[caphdr.code]);
				bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
						BGP_NOTIFY_OPEN_MALFORMED_ATTR);
				return -1;
			}
			if (caphdr.length
			    && caphdr.length % cap_modsizes[caphdr.code] != 0) {
				zlog_info(
					"%s %s Capability length error: got %u, expected a multiple of %u",
					peer->host,
					lookup_msg(capcode_str, caphdr.code,
						   NULL),
					caphdr.length,
					(unsigned)cap_modsizes[caphdr.code]);
				bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
						BGP_NOTIFY_OPEN_MALFORMED_ATTR);
				return -1;
			}
			break;
		/* we deliberately ignore unknown codes, see below */
		default:
			break;
		}

		switch (caphdr.code) {
		case CAPABILITY_CODE_MP: {
			*mp_capability = 1;

			/* 收到过 MP 能力，置出参：调用者据此在收尾时判断“本端配置的
			 * AF 是否与对端有任何重叠” */
			/* Ignore capability when override-capability is set. */
			if (!CHECK_FLAG(peer->flags,
					PEER_FLAG_OVERRIDE_CAPABILITY)) {
				/* Set negotiated value. */
				ret = bgp_capability_mp(peer, connection, &caphdr);

				/* Unsupported Capability. */
				if (ret < 0) {
					/* Store return data. */
					/* 把整个 TLV（含 2 字节 code/length 头，故 +2）拷进
					 * 错误缓冲，稍后由 UNSUP_CAPBL 通知带回或供 vty 展示 */
					if (*error + caphdr.length + 2 <= error_end) {
						memcpy(*error, sp, caphdr.length + 2);
						*error += caphdr.length + 2;
					}
				}
				ret = 0; /* Don't return error for this */
			}
		} break;
		case CAPABILITY_CODE_ENHANCED_RR:
		case CAPABILITY_CODE_REFRESH: {
			/* BGP refresh capability */
			if (caphdr.code == CAPABILITY_CODE_ENHANCED_RR)
				SET_FLAG(peer->cap, PEER_CAP_ENHANCED_RR_RCV);
			else
				SET_FLAG(peer->cap, PEER_CAP_REFRESH_RCV);
		} break;
		case CAPABILITY_CODE_ORF:
			ret = bgp_capability_orf_entry(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_RESTART:
			ret = bgp_capability_restart(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_LLGR:
			ret = bgp_capability_llgr(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_DYNAMIC:
			SET_FLAG(peer->cap, PEER_CAP_DYNAMIC_RCV);
			break;
		case CAPABILITY_CODE_AS4:
			/* Already handled as a special-case parsing of the
			 * capabilities
			 * at the beginning of OPEN processing. So we care not a
			 * jot
			 * for the value really, only error case.
			 */
			if (!bgp_capability_as4(peer, connection, &caphdr))
				ret = -1;
			break;
		case CAPABILITY_CODE_ADDPATH:
			ret = bgp_capability_addpath(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_ENHE:
			ret = bgp_capability_enhe(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_EXT_MESSAGE:
			ret = bgp_capability_ext_message(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_FQDN:
			ret = bgp_capability_hostname(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_ROLE:
			ret = bgp_capability_role(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_SOFT_VERSION:
			ret = bgp_capability_software_version(peer, connection, &caphdr);
			break;
		case CAPABILITY_CODE_LINK_LOCAL:
			SET_FLAG(peer->cap, PEER_CAP_LINK_LOCAL_RCV);
			break;
		case CAPABILITY_CODE_PATHS_LIMIT:
			ret = bgp_capability_paths_limit(peer, connection, &caphdr);
			break;
		default:
			if (caphdr.code > 128) {
				/* We don't send Notification for unknown vendor
				   specific
				   capabilities.  It seems reasonable for now...
				   */
				flog_warn(EC_BGP_CAPABILITY_VENDOR,
					  "%s Vendor specific capability %d",
					  peer->host, caphdr.code);
			} else {
				flog_warn(
					EC_BGP_CAPABILITY_UNKNOWN,
					"%s unrecognized capability code: %d - ignored",
					peer->host, caphdr.code);
				if (*error + caphdr.length + 2 <= error_end) {
					memcpy(*error, sp, caphdr.length + 2);
					*error += caphdr.length + 2;
				}
			}
		}

		if (ret < 0) {
			bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
					BGP_NOTIFY_OPEN_MALFORMED_ATTR);
			return -1;
		}
		/* 各解析器应当恰好把读指针停在 start+length 处；若没对齐，强制纠正，
		 * 保证下一个能力从正确偏移开始解析（防止某解析器多读/少读导致整体错位） */
		if (stream_get_getp(s) != (start + caphdr.length)) {
			if (stream_get_getp(s) > (start + caphdr.length))
				flog_warn(
					EC_BGP_CAPABILITY_INVALID_LENGTH,
					"%s Cap-parser for %s read past cap-length, %u!",
					peer->host,
					lookup_msg(capcode_str, caphdr.code,
						   NULL),
					caphdr.length);
			stream_set_getp(s, start + caphdr.length);
		}

		/* 注意: 局部变量 restart_flag_time 全程保持初值 0，此块实际把
		 * v_gr_restart 清零；真正的 GR 重启时间是 bgp_capability_restart() 里
		 * 直接写 peer->v_gr_restart 的。这里疑似遗留/冗余代码，保留原样仅作标注。 */
		if (!CHECK_FLAG(peer->cap, PEER_CAP_RESTART_RCV)) {
			UNSET_FLAG(restart_flag_time, 0xF000);
			peer->v_gr_restart = restart_flag_time;
		}
	}
	return 0;
}

/* ==========================================================================
 * 3. OPEN 解析收尾 —— 严格比对 / 角色校验 / AS4 预读 / 选项遍历总入口
 * ========================================================================== */

/* 【函数】strict_capability_same —— 严格模式下判断“本端配置的 AF 集合(afc)”与
 * “已协商的 AF 集合(afc_nego)”是否完全一致。
 * 返回: true 完全一致；只要存在任一 (afi,safi) 是 afc 有而 afc_nego 无（说明对端
 *   不支持/未谈成），即 false。配合 PEER_FLAG_STRICT_CAP_MATCH 使用：严格模式下
 *   能力不完全匹配就拒绝建连（bgp_open_option_parse 里发 UNSUP_CAPBL 通知）。 */
static bool strict_capability_same(struct peer *peer)
{
	int i, j;

	for (i = AFI_IP; i < AFI_MAX; i++)
		for (j = SAFI_UNICAST; j < SAFI_MAX; j++)
			if (peer->afc[i][j] != peer->afc_nego[i][j])
				return false;
	return true;
}


/* 【函数】bgp_role_violation —— RFC9234 路由角色(Role)一致性检查，OPEN 协商收尾调用。
 * 规则: 双方角色都已定义(local/remote 均 != ROLE_UNDEFINED)时，只允许这些组合：
 *   Peer-Peer / Provider-Customer / Customer-Provider / RS-Server--RS-Client(双向)。
 *   任一对不上 -> 发 OPEN_ROLE_MISMATCH 通知并返回 true（拒绝建连）。
 * 另: 本端配了 PEER_FLAG_ROLE_STRICT_MODE(strict-mode) 而对端未声明角色时也报错，
 *   错误文本随通知数据回带，提示对端“请在你那侧配置角色”。
 * 返回: true 表示存在违规（调用者据此中止处理）；false 表示 OK。 */
static bool bgp_role_violation(struct peer *peer, struct peer_connection *connection)
{
	uint8_t local_role = peer->local_role;
	uint8_t remote_role = peer->remote_role;

	if (local_role != ROLE_UNDEFINED && remote_role != ROLE_UNDEFINED &&
	    !((local_role == ROLE_PEER && remote_role == ROLE_PEER) ||
	      (local_role == ROLE_PROVIDER && remote_role == ROLE_CUSTOMER) ||
	      (local_role == ROLE_CUSTOMER && remote_role == ROLE_PROVIDER) ||
	      (local_role == ROLE_RS_SERVER && remote_role == ROLE_RS_CLIENT) ||
	      (local_role == ROLE_RS_CLIENT &&
	       remote_role == ROLE_RS_SERVER))) {
		bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR, BGP_NOTIFY_OPEN_ROLE_MISMATCH);
		return true;
	}
	if (remote_role == ROLE_UNDEFINED &&
	    CHECK_FLAG(peer->flags, PEER_FLAG_ROLE_STRICT_MODE)) {
		const char *err_msg =
			"Strict mode. Please set the role on your side.";
		bgp_notify_send_with_data(connection, BGP_NOTIFY_OPEN_ERR,
					  BGP_NOTIFY_OPEN_ROLE_MISMATCH, (uint8_t *)err_msg,
					  strlen(err_msg));
		return true;
	}
	return false;
}


/* 【函数】peek_for_as4_capability —— 在正式解析能力前“预读”AS4 能力，把对端的 4 字节
 * AS 号抢先用回来。
 * 为什么要预读: 收到 OPEN 时用户可能还没确定这条连接属于哪个 peer（动态邻居/对端用
 *   2 字节 AS_TRANS 占位），而判断“你是哪个 peer”往往需要真正的 AS 号；AS4 藏在能力里，
 *   所以先扫一遍把 AS4 找出来。返回 0 表示没找到（对端是旧式 2 字节 AS）。
 * 实现: 只读不消费——末尾 stream_set_getp(s, orig_getp) 复位读指针；遇到任何格式错误
 *   一律 goto end 提前退出（错误留给后续正式解析去报），因此它对畸形报文是“宁漏不误”。
 * 副作用: 命中 AS4 时内部调用 bgp_capability_as4() 会置 PEER_CAP_AS4_RCV。 */
/* peek into option, stores ASN to *as4 if the AS4 capability was found.
 * Returns  0 if no as4 found, as4cap value otherwise.
 */
as_t peek_for_as4_capability(struct peer_connection *connection, uint16_t length)
{
	struct peer *peer = connection->peer;
	struct stream *s = BGP_INPUT(connection);
	size_t orig_getp = stream_get_getp(s);
	size_t end = orig_getp + length;
	as_t as4 = 0;

	if (BGP_DEBUG(as4, AS4))
		zlog_debug(
			"%s [AS4] rcv OPEN w/ OPTION parameter len: %u, peeking for as4",
			peer->host, length);
	/* the error cases we DONT handle, we ONLY try to read as4 out of
	 * correctly formatted options.
	 */
	while (stream_get_getp(s) < end) {
		uint8_t opt_type;
		uint16_t opt_length;

		/* Ensure we can read the option type */
		if (stream_get_getp(s) + 1 > end)
			goto end;

		/* Fetch the option type */
		opt_type = stream_getc(s);

		/*
		 * Check the length and fetch the opt_length
		 * If the peer is BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer)
		 * then we do a getw which is 2 bytes.  So we need to
		 * ensure that we can read that as well
		 */
		if (BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer)) {
			if (stream_get_getp(s) + 2 > end)
				goto end;

			opt_length = stream_getw(s);
		} else {
			if (stream_get_getp(s) + 1 > end)
				goto end;

			opt_length = stream_getc(s);
		}

		/* Option length check. */
		if (stream_get_getp(s) + opt_length > end)
			goto end;

		if (opt_type == BGP_OPEN_OPT_CAP) {
			unsigned long capd_start = stream_get_getp(s);
			unsigned long capd_end = capd_start + opt_length;

			assert(capd_end <= end);

			while (stream_get_getp(s) < capd_end) {
				struct capability_header hdr;

				if (stream_get_getp(s) + 2 > capd_end)
					goto end;

				hdr.code = stream_getc(s);
				hdr.length = stream_getc(s);

				if ((stream_get_getp(s) + hdr.length)
				    > capd_end)
					goto end;

				if (hdr.code == CAPABILITY_CODE_AS4) {
					if (BGP_DEBUG(as4, AS4))
						zlog_debug(
							"[AS4] found AS4 capability, about to parse");
					as4 = bgp_capability_as4(peer, connection, &hdr);

					goto end;
				}
				stream_forward_getp(s, hdr.length);
			}
		}
	}

end:
	stream_set_getp(s, orig_getp);
	return as4;
}

/* 【函数】bgp_open_option_parse —— OPEN 可选参数区遍历总入口（接收侧）。
 * 职责: 逐条读取可选参数 (opt_type, opt_length, value)；只认得 BGP_OPEN_OPT_CAP(2)，
 *   把它交给 bgp_capability_parse 解析；其它 opt_type 一律以 OPEN_UNSUP_PARAM 通知拒绝。
 * 长度字段宽度由 BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer) 决定(1 或 2 字节)，对应 RFC9072
 *   扩展可选参数长度；每一步读取前都用 STREAM_READABLE 做越界保护。
 * 收尾做四件事（务必看懂）:
 *   1) strict 模式: 能力不匹配或有过错误 -> 发 UNSUP_CAPBL 通知并返回 -1；
 *   2) 决定 peer->max_packet_size: 双方都通告 Ext-Message 才放大到 65535；
 *   3) bgp_role_violation(): Role 违规则返回 -1；
 *   4) 若对端有 MP 能力但与本端配置零重叠 -> 发 UNSUP_CAPBL 通知。
 * 返回: 0 正常；-1 表示应中止建连（通知已发）。 */
/**
 * Parse open option.
 *
 * @param[out] mp_capability @see bgp_capability_parse() for semantics.
 */
int bgp_open_option_parse(struct peer *peer, struct peer_connection *connection, uint16_t length,
			  int *mp_capability)
{
	int ret = 0;
	uint8_t *error;
	uint8_t error_data[BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE];
	struct stream *s = BGP_INPUT(connection);
	size_t end = stream_get_getp(s) + length;

	error = error_data;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s rcv OPEN w/ OPTION parameter len: %u",
			   peer->host, length);

	/* Unset any previously received GR capability. */
	/* 每次新 OPEN 解析前清掉“上次收到的 GR”，避免连接重建时带上陈旧状态 */
	UNSET_FLAG(peer->cap, PEER_CAP_RESTART_RCV);

	while (stream_get_getp(s) < end) {
		uint8_t opt_type;
		uint16_t opt_length;

		/*
		 * Check that we can read the opt_type and fetch it
		 */
		if (STREAM_READABLE(s) < 1) {
			zlog_err("%s Option length error", peer->host);
			bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
					BGP_NOTIFY_OPEN_MALFORMED_ATTR);
			return -1;
		}
		opt_type = stream_getc(s);

		/*
		 * Check the length of the stream to ensure that
		 * FRR can properly read the opt_length. Then read it
		 */
		if (BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer)) {
			if (STREAM_READABLE(s) < 2) {
				zlog_err("%s Option length error", peer->host);
				bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
						BGP_NOTIFY_OPEN_MALFORMED_ATTR);
				return -1;
			}

			opt_length = stream_getw(s);
		} else {
			if (STREAM_READABLE(s) < 1) {
				zlog_err("%s Option length error", peer->host);
				bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
						BGP_NOTIFY_OPEN_MALFORMED_ATTR);
				return -1;
			}

			opt_length = stream_getc(s);
		}

		/* Option length check. */
		if (STREAM_READABLE(s) < opt_length) {
			zlog_err("%s Option length error (%d)", peer->host,
				 opt_length);
			bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
					BGP_NOTIFY_OPEN_MALFORMED_ATTR);
			return -1;
		}

		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%s rcvd OPEN w/ optional parameter type %u (%s) len %u",
				peer->host, opt_type,
				opt_type == BGP_OPEN_OPT_CAP ? "Capability"
							     : "Unknown",
				opt_length);

		switch (opt_type) {
		case BGP_OPEN_OPT_CAP:
			ret = bgp_capability_parse(peer, connection, opt_length, mp_capability,
						   &error,
						   error_data +
							   BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE);
			break;
		default:
			bgp_notify_send(connection, BGP_NOTIFY_OPEN_ERR,
					BGP_NOTIFY_OPEN_UNSUP_PARAM);
			ret = -1;
			break;
		}

		/* Parse error.  To accumulate all unsupported capability codes,
		   bgp_capability_parse does not return -1 when encounter
		   unsupported capability code.  To detect that, please check
		   error and erro_data pointer, like below.  */
		if (ret < 0)
			return -1;
	}

	/* All OPEN option is parsed.  Check capability when strict compare
	   flag is enabled.*/
	if (CHECK_FLAG(peer->flags, PEER_FLAG_STRICT_CAP_MATCH)) {
		/* If Unsupported Capability exists or local capability does
		 * not negotiated with remote peer
		 */
		if (error != error_data || !strict_capability_same(peer)) {
			bgp_notify_send_with_data(connection, BGP_NOTIFY_OPEN_ERR,
						  BGP_NOTIFY_OPEN_UNSUP_CAPBL, error_data,
						  error - error_data);
			return -1;
		}
	}

	/* Extended Message Support */
	/* 只有“双方都通告”Ext-Message(ADV & RCV)才把最大报文放大到 65535，
	 * 否则维持标准 4096；该值被后续所有发送/接收长度校验引用 */
	peer->max_packet_size =
		(CHECK_FLAG(peer->cap, PEER_CAP_EXTENDED_MESSAGE_RCV)
		 && CHECK_FLAG(peer->cap, PEER_CAP_EXTENDED_MESSAGE_ADV))
			? BGP_EXTENDED_MESSAGE_MAX_PACKET_SIZE
			: BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE;

	/* Check that roles are corresponding to each other */
	if (bgp_role_violation(peer, connection))
		return -1;

	/* Check there are no common AFI/SAFIs and send Unsupported Capability
	   error. */
	/* 对端发了 MP 能力，但本端配置的所有 AF 都没能和它对上(afc_nego 全 0)：
	 * 说明两端地址族零重叠，正常协商下去也没意义，直接发 UNSUP_CAPBL 拒绝。
	 * override-capability(跳过能力协商) 时不做此检查。 */
	if (*mp_capability
	    && !CHECK_FLAG(peer->flags, PEER_FLAG_OVERRIDE_CAPABILITY)) {
		if (!peer->afc_nego[AFI_IP][SAFI_UNICAST]
		    && !peer->afc_nego[AFI_IP][SAFI_MULTICAST]
		    && !peer->afc_nego[AFI_IP][SAFI_LABELED_UNICAST]
		    && !peer->afc_nego[AFI_IP][SAFI_MPLS_VPN]
		    && !peer->afc_nego[AFI_IP][SAFI_ENCAP]
		    && !peer->afc_nego[AFI_IP][SAFI_FLOWSPEC]
		    && !peer->afc_nego[AFI_IP6][SAFI_UNICAST]
		    && !peer->afc_nego[AFI_IP6][SAFI_MULTICAST]
		    && !peer->afc_nego[AFI_IP6][SAFI_LABELED_UNICAST]
		    && !peer->afc_nego[AFI_IP6][SAFI_MPLS_VPN]
		    && !peer->afc_nego[AFI_IP6][SAFI_ENCAP]
		    && !peer->afc_nego[AFI_IP6][SAFI_FLOWSPEC]
		    && !peer->afc_nego[AFI_L2VPN][SAFI_EVPN]) {
			flog_err(EC_BGP_PKT_OPEN,
				 "%s [Error] Configured AFI/SAFIs do not overlap with received MP capabilities",
				 peer->host);

			bgp_notify_send_with_data(connection, BGP_NOTIFY_OPEN_ERR,
						  BGP_NOTIFY_OPEN_UNSUP_CAPBL, error_data,
						  error - error_data);
		}
	}
	return 0;
}

/* ==========================================================================
 * 4. OPEN 发送 —— Capability TLV 的构造（bgp_open_capability 及其子函数）
 *    统一约定: ext_opt_params=true 表示使用 RFC9072 扩展可选参数长度，
 *    此时每个参数的“长度字段”占 2 字节，否则占 1 字节（由调用方 stream_putw/putc 区分）。
 * ========================================================================== */

/* 【函数】bgp_open_capability_orf —— 构造一个 ORF 可选参数（含单个 Address Prefix
 * ORF 条目）写入流 s。
 * 参数: afi/safi 为内部枚举；code 目前只能是 CAPABILITY_CODE_ORF；ext_opt_params 见上。
 * 写回: 依据本端配置 af_flags 的 SM/RM 置 peer->af_cap 的 ORF_PREFIX_SM_ADV/RM_ADV，
 *   并写入对应 Mode(BOTH/SEND/RECEIVE)。读“配置”(af_flags)、写“通告态”(af_cap)。
 * 实现: 先用占位长度 0 顺序写出各字段，回填 capp(参数长度)、orfp(ORF 长度)、numberp(条目数)。 */
static void bgp_open_capability_orf(struct stream *s, struct peer *peer,
				    afi_t afi, safi_t safi, uint8_t code,
				    bool ext_opt_params)
{
	uint16_t cap_len;
	uint8_t orf_len;
	unsigned long capp;
	unsigned long orfp;
	unsigned long numberp;
	int number_of_orfs = 0;
	iana_afi_t pkt_afi = IANA_AFI_IPV4;
	iana_safi_t pkt_safi = IANA_SAFI_UNICAST;

	/* Convert AFI, SAFI to values for packet. */
	bgp_map_afi_safi_int2iana(afi, safi, &pkt_afi, &pkt_safi);

	stream_putc(s, BGP_OPEN_OPT_CAP);
	capp = stream_get_endp(s); /* Set Capability Len Pointer */
	ext_opt_params ? stream_putw(s, 0)
		       : stream_putc(s, 0); /* Capability Length */
	stream_putc(s, code);      /* Capability Code */
	orfp = stream_get_endp(s); /* Set ORF Len Pointer */
	stream_putc(s, 0);	 /* ORF Length */
	stream_putw(s, pkt_afi);
	stream_putc(s, 0);
	stream_putc(s, pkt_safi);
	numberp = stream_get_endp(s); /* Set Number Pointer */
	stream_putc(s, 0);	    /* Number of ORFs */

	/* Address Prefix ORF */
	if (CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_ORF_PREFIX_SM)
	    || CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_ORF_PREFIX_RM)) {
		stream_putc(s, ORF_TYPE_PREFIX);

		if (CHECK_FLAG(peer->af_flags[afi][safi],
			       PEER_FLAG_ORF_PREFIX_SM)
		    && CHECK_FLAG(peer->af_flags[afi][safi],
				  PEER_FLAG_ORF_PREFIX_RM)) {
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ORF_PREFIX_SM_ADV);
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ORF_PREFIX_RM_ADV);
			stream_putc(s, ORF_MODE_BOTH);
		} else if (CHECK_FLAG(peer->af_flags[afi][safi],
				      PEER_FLAG_ORF_PREFIX_SM)) {
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ORF_PREFIX_SM_ADV);
			stream_putc(s, ORF_MODE_SEND);
		} else {
			SET_FLAG(peer->af_cap[afi][safi],
				 PEER_CAP_ORF_PREFIX_RM_ADV);
			stream_putc(s, ORF_MODE_RECEIVE);
		}
		number_of_orfs++;
	}

	/* Total Number of ORFs. */
	stream_putc_at(s, numberp, number_of_orfs);

	/* Total ORF Len. */
	orf_len = stream_get_endp(s) - orfp - 1;
	stream_putc_at(s, orfp, orf_len);

	/* Total Capability Len. */
	/* 注意: 与 GR/LLGR/FQDN 等处“扩展模式写 len-1”的写法不一致——此处无论是否
	 * 扩展都写 cap_len(=endp-capp-1)。扩展模式(capp 后 2 字节长度)下长度字段实际
	 * 应比 cap_len 少 1；若本端开 ORF 且走 RFC9072 扩展长度，疑为 off-by-one，需实测确认。 */
	cap_len = stream_get_endp(s) - capp - 1;
	ext_opt_params ? stream_putw_at(s, capp, cap_len)
		       : stream_putc_at(s, capp, cap_len);
}

/* 【函数】bgp_peer_send_gr_capability —— 构造并写入 Graceful Restart 能力 (码 64)。
 * 前置: 本端配了 GR 或 GR-helper 才会发；未配置直接 return（不发该能力）。
 * 内容: Restart Flags+Time（本端处于启动/GR 中时置 R 位，开优雅通知则置 N 位），
 *   随后按每个已使能且支持 GR 的 AF 写 (AFI,SAFI,F-bit)；F 位取
 *   bgp_gr_is_forwarding_preserved(bgp)（本端能否在重启后保留转发表项）。
 * 写回: peer->cap 的 RESTART_ADV、R/N 位_ADV。
 * 长度回填: rcapp 处写能力长度；capp 处写参数长度（扩展模式写 len-1）。 */
static void bgp_peer_send_gr_capability(struct stream *s, struct peer *peer,
					bool ext_opt_params)
{
	int len;
	iana_afi_t pkt_afi = IANA_AFI_IPV4;
	afi_t afi;
	safi_t safi;
	iana_safi_t pkt_safi = IANA_SAFI_UNICAST;
	uint32_t restart_time;
	unsigned long capp = 0;
	unsigned long rcapp = 0;
	struct bgp *bgp = peer->bgp;

	if (!CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART)
	    && !CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART_HELPER))
		return;

	SET_FLAG(peer->cap, PEER_CAP_RESTART_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	capp = stream_get_endp(s); /* Set Capability Len Pointer */
	ext_opt_params ? stream_putw(s, 0)
		       : stream_putc(s, 0); /* Capability Length */
	stream_putc(s, CAPABILITY_CODE_RESTART);
	/* Set Restart Capability Len Pointer */
	rcapp = stream_get_endp(s);
	stream_putc(s, 0);
	restart_time = bgp->restart_time;
	/* R 位=本端正处于重启(或刚启动、尚未收敛)，通知对端“请为我保留/等待” */
	if (peer->bgp->t_startup || bgp_in_graceful_restart()) {
		SET_FLAG(restart_time, GRACEFUL_RESTART_R_BIT);
		SET_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_ADV);
	}

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_GRACEFUL_NOTIFICATION)) {
		SET_FLAG(restart_time, GRACEFUL_RESTART_N_BIT);
		SET_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_N_BIT_ADV);
	}

	stream_putw(s, restart_time);

	if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
		zlog_debug("%s: Sending GR Capability, Restart time %d R-bit %s, N-bit %s",
			   peer->host, bgp->restart_time,
			   CHECK_FLAG(peer->cap,
				      PEER_CAP_GRACEFUL_RESTART_R_BIT_ADV)
				   ? "SET"
				   : "NOT-SET",
			   CHECK_FLAG(peer->cap,
				      PEER_CAP_GRACEFUL_RESTART_N_BIT_ADV)
				   ? "SET"
				   : "NOT-SET");
	frrtrace(4, frr_bgp, gr_send_rbit_capability, bgp->name_pretty, peer->host,
		 bgp->restart_time, CHECK_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_ADV));

	/* Send address-family specific graceful-restart capability
	 * only when GR config is present
	 */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART)) {
		FOREACH_AFI_SAFI (afi, safi) {
			bool f_bit = false;

			if (!peer->afc[afi][safi])
				continue;
			if (!bgp_gr_supported_for_afi_safi(afi, safi))
				continue;

			/* Convert AFI, SAFI to values for
			 * packet.
			 */
			bgp_map_afi_safi_int2iana(afi, safi, &pkt_afi,
						  &pkt_safi);
			stream_putw(s, pkt_afi);
			stream_putc(s, pkt_safi);

			f_bit = bgp_gr_is_forwarding_preserved(bgp);

			if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
				zlog_debug("... F-bit %s for %s",
					   f_bit ? "SET" : "NOT-SET",
					   get_afi_safi_str(afi, safi, false));

			frrtrace(5, frr_bgp, gr_send_fbit_capability, bgp->name_pretty, peer->host,
				 afi, safi, f_bit);
			stream_putc(s, f_bit ? GRACEFUL_RESTART_F_BIT : 0);
		}
	}

	/* Total Graceful restart capability Len. */
	len = stream_get_endp(s) - rcapp - 1;
	stream_putc_at(s, rcapp, len);

	/* Total Capability Len. */
	len = stream_get_endp(s) - capp - 1;
	ext_opt_params ? stream_putw_at(s, capp, len - 1)
		       : stream_putc_at(s, capp, len);
}

/* 【函数】bgp_peer_send_llgr_capability —— 构造并写入 Long-Lived GR 能力 (码 71)。
 * 前置: 必须已经置了 PEER_CAP_RESTART_ADV（即上一函数已发过 GR），否则直接 return
 *   —— LLGR 依附于 GR，不能单独发。
 * 内容: 对每个已使能 AF 写 (AFI,SAFI,LLGR_F_BIT, stale_time=bgp->llgr_stale_time)。
 * 写回: peer->cap 的 LLGR_ADV；af_cap 的 LLGR_AF_ADV。 */
static void bgp_peer_send_llgr_capability(struct stream *s, struct peer *peer,
					  bool ext_opt_params)
{
	int len;
	iana_afi_t pkt_afi = IANA_AFI_IPV4;
	afi_t afi;
	safi_t safi;
	iana_safi_t pkt_safi = IANA_SAFI_UNICAST;
	unsigned long capp = 0;
	unsigned long rcapp = 0;

	if (!CHECK_FLAG(peer->cap, PEER_CAP_RESTART_ADV))
		return;

	SET_FLAG(peer->cap, PEER_CAP_LLGR_ADV);

	stream_putc(s, BGP_OPEN_OPT_CAP);
	capp = stream_get_endp(s); /* Set Capability Len Pointer */
	ext_opt_params ? stream_putw(s, 0)
		       : stream_putc(s, 0); /* Capability Length */
	stream_putc(s, CAPABILITY_CODE_LLGR);

	rcapp = stream_get_endp(s);
	stream_putc(s, 0);

	FOREACH_AFI_SAFI (afi, safi) {
		if (!peer->afc[afi][safi])
			continue;

		bgp_map_afi_safi_int2iana(afi, safi, &pkt_afi, &pkt_safi);

		stream_putw(s, pkt_afi);
		stream_putc(s, pkt_safi);
		stream_putc(s, LLGR_F_BIT);
		stream_put3(s, peer->bgp->llgr_stale_time);

		SET_FLAG(peer->af_cap[afi][safi], PEER_CAP_LLGR_AF_ADV);
	}

	/* Total Long-lived Graceful Restart capability Len. */
	len = stream_get_endp(s) - rcapp - 1;
	stream_putc_at(s, rcapp, len);

	/* Total Capability Len. */
	len = stream_get_endp(s) - capp - 1;
	ext_opt_params ? stream_putw_at(s, capp, len - 1)
		       : stream_putc_at(s, capp, len);
}

/* 【函数】bgp_open_capability —— 发送侧总入口：把本端所有要通告的能力写进流 s（OPEN
 * 报文的可选参数区），返回写入的“可选参数总长度”。
 * 参数: ext_opt_params 决定是否用 RFC9072 扩展可选参数长度（每个参数长度字段 2 字节）。
 *   调用方 bgp_open_make() 会先用非扩展试写一遍，超过 255 字节再改用扩展重写。
 * 写入顺序（与 FRR 惯例一致，对端解析不依赖顺序）: MP(每 AF) -> Route Refresh ->
 *   Enhanced RR -> AS4 -> Ext-Message -> Role -> AddPath -> Paths-Limit -> ORF ->
 *   Dynamic -> Link-Local -> FQDN -> GR -> LLGR -> Soft-Version。
 * 末尾回填: 非扩展时把总长写回 cp 处的 1 字节；扩展时把总长写回 eopl 处的 2 字节。
 * 返回: 可选参数总长度（扩展模式为扩展长度字段的值）。
 * 副作用: 沿途 SET_FLAG 大量 peer->cap / peer->af_cap 的 *_ADV 位，标记“我发过什么”。 */
/* Fill in capability open option to the packet. */
uint16_t bgp_open_capability(struct stream *s, struct peer *peer,
			     struct peer_connection *connection, bool ext_opt_params)
{
	uint16_t len;
	unsigned long cp, capp, rcapp, eopl = 0;
	iana_afi_t pkt_afi = IANA_AFI_IPV4;
	afi_t afi;
	safi_t safi;
	iana_safi_t pkt_safi = IANA_SAFI_UNICAST;
	as_t local_as;
	uint8_t afi_safi_count = 0;
	bool adv_addpath_tx = false;

	/* Non-Ext OP Len. */
	cp = stream_get_endp(s);
	stream_putc(s, 0);

	if (ext_opt_params) {
		/* Non-Ext OP Len. */
		/* RFC9072: 把 1 字节的“可选参数长度”填成 255，紧接一个类型为 255
		 * 的特殊参数，其值就是一个 2 字节的“扩展可选参数长度”。 */
		stream_putc_at(s, cp, BGP_OPEN_NON_EXT_OPT_LEN);

		/* Non-Ext OP Type */
		stream_putc(s, BGP_OPEN_NON_EXT_OPT_TYPE_EXTENDED_LENGTH);

		/* Extended Opt. Parm. Length */
		eopl = stream_get_endp(s);
		stream_putw(s, 0);
	}

	/* Do not send capability. */
	/* 未开启能力协商(老式对端)或显式配了 dont-capability 时，一个能力都不发，
	 * 直接让 OPEN 里该区域为空（返回 0 长度） */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_CAPABILITY_OPEN)
	    || CHECK_FLAG(peer->flags, PEER_FLAG_DONT_CAPABILITY))
		return 0;

	/* MP capability for configured AFI, SAFI */
	/* 为每个已配置的 AF 通告一条 MP 能力，并置 afc_adv 表示“本端已通告该 AF” */
	FOREACH_AFI_SAFI (afi, safi) {
		if (peer->afc[afi][safi]) {
			/* Convert AFI, SAFI to values for packet. */
			bgp_map_afi_safi_int2iana(afi, safi, &pkt_afi,
						  &pkt_safi);

			peer->afc_adv[afi][safi] = 1;
			stream_putc(s, BGP_OPEN_OPT_CAP);
			ext_opt_params
				? stream_putw(s, CAPABILITY_CODE_MP_LEN + 2)
				: stream_putc(s, CAPABILITY_CODE_MP_LEN + 2);
			stream_putc(s, CAPABILITY_CODE_MP);
			stream_putc(s, CAPABILITY_CODE_MP_LEN);
			stream_putw(s, pkt_afi);
			stream_putc(s, 0);
			stream_putc(s, pkt_safi);

			/* Extended nexthop capability - currently
			 * supporting RFC-5549 for
			 * Link-Local peering only
			 */
			if (CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE) &&
			    connection->su.sa.sa_family == AF_INET6 && afi == AFI_IP &&
			    (safi == SAFI_UNICAST || safi == SAFI_MPLS_VPN ||
			     safi == SAFI_LABELED_UNICAST)) {
				/* RFC 5549 Extended Next Hop Encoding
				 */
				SET_FLAG(peer->cap, PEER_CAP_ENHE_ADV);
				stream_putc(s, BGP_OPEN_OPT_CAP);
				ext_opt_params
					? stream_putw(s,
						      CAPABILITY_CODE_ENHE_LEN
							      + 2)
					: stream_putc(s,
						      CAPABILITY_CODE_ENHE_LEN
							      + 2);
				stream_putc(s, CAPABILITY_CODE_ENHE);
				stream_putc(s, CAPABILITY_CODE_ENHE_LEN);

				SET_FLAG(peer->af_cap[AFI_IP][safi],
					 PEER_CAP_ENHE_AF_ADV);
				stream_putw(s, pkt_afi);
				stream_putw(s, pkt_safi);
				stream_putw(s, afi_int2iana(AFI_IP6));

				if (CHECK_FLAG(peer->af_cap[afi][safi],
					       PEER_CAP_ENHE_AF_RCV))
					SET_FLAG(peer->af_cap[afi][safi],
						 PEER_CAP_ENHE_AF_NEGO);
			}
		}
	}

	/* Route refresh. */
	SET_FLAG(peer->cap, PEER_CAP_REFRESH_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	ext_opt_params ? stream_putw(s, CAPABILITY_CODE_REFRESH_LEN + 2)
		       : stream_putc(s, CAPABILITY_CODE_REFRESH_LEN + 2);
	stream_putc(s, CAPABILITY_CODE_REFRESH);
	stream_putc(s, CAPABILITY_CODE_REFRESH_LEN);

	/* Enhanced Route Refresh. */
	SET_FLAG(peer->cap, PEER_CAP_ENHANCED_RR_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	ext_opt_params ? stream_putw(s, CAPABILITY_CODE_ENHANCED_LEN + 2)
		       : stream_putc(s, CAPABILITY_CODE_ENHANCED_LEN + 2);
	stream_putc(s, CAPABILITY_CODE_ENHANCED_RR);
	stream_putc(s, CAPABILITY_CODE_ENHANCED_LEN);

	/* AS4 */
	SET_FLAG(peer->cap, PEER_CAP_AS4_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	ext_opt_params ? stream_putw(s, CAPABILITY_CODE_AS4_LEN + 2)
		       : stream_putc(s, CAPABILITY_CODE_AS4_LEN + 2);
	stream_putc(s, CAPABILITY_CODE_AS4);
	stream_putc(s, CAPABILITY_CODE_AS4_LEN);
	/* AS4 能力里携带真正的 4 字节 ASN；local-as 覆盖(change_local_as)优先。
	 * 注意与 OPEN 头部的 2 字节 MyAS 区分：头部在 ASN>65535 时写 AS_TRANS(23456)，
	 * 真实值就靠这个 AS4 能力传给对端。 */
	if (peer->change_local_as)
		local_as = peer->change_local_as;
	else
		local_as = peer->local_as;
	stream_putl(s, local_as);

	/* Extended Message Support */
	SET_FLAG(peer->cap, PEER_CAP_EXTENDED_MESSAGE_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	ext_opt_params ? stream_putw(s, CAPABILITY_CODE_EXT_MESSAGE_LEN + 2)
		       : stream_putc(s, CAPABILITY_CODE_EXT_MESSAGE_LEN + 2);
	stream_putc(s, CAPABILITY_CODE_EXT_MESSAGE);
	stream_putc(s, CAPABILITY_CODE_EXT_MESSAGE_LEN);

	/* Role*/
	/* 只在本地显式配了角色时才通告 RFC9234 Role 能力 */
	if (peer->local_role != ROLE_UNDEFINED) {
		SET_FLAG(peer->cap, PEER_CAP_ROLE_ADV);
		/* 注意: 其它能力都按 ext_opt_params 选择 2/1 字节长度字段，唯独这里
		 * 恒用 stream_putc 写 1 字节参数长度。若本端配了角色且走 RFC9072 扩展
		 * 可选参数长度，该长度字段宽度与整体不一致，疑为 bug（保留原样仅标注）。 */
		stream_putc(s, BGP_OPEN_OPT_CAP);
		stream_putc(s, CAPABILITY_CODE_ROLE_LEN + 2);
		stream_putc(s, CAPABILITY_CODE_ROLE);
		stream_putc(s, CAPABILITY_CODE_ROLE_LEN);
		stream_putc(s, peer->local_role);
	}

	/* AddPath */
	/* 先统计已使能 AF 的数量(afi_safi_count)，用于计算 AddPath 与 Paths-Limit
	 * 能力的总长度；并决定是否通告 TX（有任一 AF 配了 addpath 类型才发 TX）。 */
	FOREACH_AFI_SAFI (afi, safi) {
		if (peer->afc[afi][safi]) {
			afi_safi_count++;

			/* Only advertise addpath TX if a feature that
			 * will use it is
			 * configured */
			if (peer->addpath_type[afi][safi] != BGP_ADDPATH_NONE)
				adv_addpath_tx = true;

			/* If we have enabled labeled unicast, we MUST check
			 * against unicast SAFI because addpath IDs are
			 * allocated under unicast SAFI, the same as the RIB
			 * is managed in unicast SAFI.
			 */
			if (safi == SAFI_LABELED_UNICAST)
				if (peer->addpath_type[afi][SAFI_UNICAST] !=
				    BGP_ADDPATH_NONE)
					adv_addpath_tx = true;
		}
	}

	SET_FLAG(peer->cap, PEER_CAP_ADDPATH_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	ext_opt_params
		? stream_putw(s, (CAPABILITY_CODE_ADDPATH_LEN * afi_safi_count)
					 + 2)
		: stream_putc(s, (CAPABILITY_CODE_ADDPATH_LEN * afi_safi_count)
					 + 2);
	stream_putc(s, CAPABILITY_CODE_ADDPATH);
	stream_putc(s, CAPABILITY_CODE_ADDPATH_LEN * afi_safi_count);

	FOREACH_AFI_SAFI (afi, safi) {
		if (peer->afc[afi][safi]) {
			bool adv_addpath_rx =
				!CHECK_FLAG(peer->af_flags[afi][safi],
					    PEER_FLAG_DISABLE_ADDPATH_RX);
			uint8_t flags = 0;

			/* Convert AFI, SAFI to values for packet. */
			bgp_map_afi_safi_int2iana(afi, safi, &pkt_afi,
						  &pkt_safi);

			stream_putw(s, pkt_afi);
			stream_putc(s, pkt_safi);

			if (adv_addpath_rx) {
				SET_FLAG(flags, BGP_ADDPATH_RX);
				SET_FLAG(peer->af_cap[afi][safi],
					 PEER_CAP_ADDPATH_AF_RX_ADV);
			} else {
				UNSET_FLAG(peer->af_cap[afi][safi],
					   PEER_CAP_ADDPATH_AF_RX_ADV);
			}

			if (adv_addpath_tx) {
				SET_FLAG(flags, BGP_ADDPATH_TX);
				SET_FLAG(peer->af_cap[afi][safi],
					 PEER_CAP_ADDPATH_AF_TX_ADV);
				if (safi == SAFI_LABELED_UNICAST)
					SET_FLAG(
						peer->af_cap[afi][SAFI_UNICAST],
						PEER_CAP_ADDPATH_AF_TX_ADV);
			} else {
				UNSET_FLAG(peer->af_cap[afi][safi],
					   PEER_CAP_ADDPATH_AF_TX_ADV);
			}

			stream_putc(s, flags);
		}
	}

	/* Paths-Limit capability */
	/* 逐 AF 写“我允许对端发来的多路径数量上限”= addpath_paths_limit[afi][safi].send */
	SET_FLAG(peer->cap, PEER_CAP_PATHS_LIMIT_ADV);
	stream_putc(s, BGP_OPEN_OPT_CAP);
	ext_opt_params ? stream_putw(s, (CAPABILITY_CODE_PATHS_LIMIT_LEN *
					 afi_safi_count) +
						2)
		       : stream_putc(s, (CAPABILITY_CODE_PATHS_LIMIT_LEN *
					 afi_safi_count) +
						2);
	stream_putc(s, CAPABILITY_CODE_PATHS_LIMIT);
	stream_putc(s, CAPABILITY_CODE_PATHS_LIMIT_LEN * afi_safi_count);

	FOREACH_AFI_SAFI (afi, safi) {
		if (!peer->afc[afi][safi])
			continue;

		bgp_map_afi_safi_int2iana(afi, safi, &pkt_afi, &pkt_safi);

		stream_putw(s, pkt_afi);
		stream_putc(s, pkt_safi);
		stream_putw(s, peer->addpath_paths_limit[afi][safi].send);

		SET_FLAG(peer->af_cap[afi][safi], PEER_CAP_PATHS_LIMIT_AF_ADV);
	}

	/* ORF capability. */
	FOREACH_AFI_SAFI (afi, safi) {
		if (CHECK_FLAG(peer->af_flags[afi][safi],
			       PEER_FLAG_ORF_PREFIX_SM)
		    || CHECK_FLAG(peer->af_flags[afi][safi],
				  PEER_FLAG_ORF_PREFIX_RM)) {
			bgp_open_capability_orf(s, peer, afi, safi,
						CAPABILITY_CODE_ORF,
						ext_opt_params);
		}
	}

	/* Dynamic capability. */
	if (peergroup_flag_check(peer, PEER_FLAG_DYNAMIC_CAPABILITY)) {
		SET_FLAG(peer->cap, PEER_CAP_DYNAMIC_ADV);
		stream_putc(s, BGP_OPEN_OPT_CAP);
		ext_opt_params
			? stream_putw(s, CAPABILITY_CODE_DYNAMIC_LEN + 2)
			: stream_putc(s, CAPABILITY_CODE_DYNAMIC_LEN + 2);
		stream_putc(s, CAPABILITY_CODE_DYNAMIC);
		stream_putc(s, CAPABILITY_CODE_DYNAMIC_LEN);
	}

	/* Link-Local Next Hop capability. */
	if (peergroup_flag_check(peer, PEER_FLAG_CAPABILITY_LINK_LOCAL)) {
		SET_FLAG(peer->cap, PEER_CAP_LINK_LOCAL_ADV);
		stream_putc(s, BGP_OPEN_OPT_CAP);
		ext_opt_params ? stream_putw(s, CAPABILITY_CODE_LINK_LOCAL_LEN + 2)
			       : stream_putc(s, CAPABILITY_CODE_LINK_LOCAL_LEN + 2);
		stream_putc(s, CAPABILITY_CODE_LINK_LOCAL);
		stream_putc(s, CAPABILITY_CODE_LINK_LOCAL_LEN);
	}

	/* FQDN capability */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_FQDN)
	    && cmd_hostname_get()) {
		SET_FLAG(peer->cap, PEER_CAP_HOSTNAME_ADV);
		stream_putc(s, BGP_OPEN_OPT_CAP);
		rcapp = stream_get_endp(s); /* Ptr to length placeholder */
		ext_opt_params ? stream_putw(s, 0)
			       : stream_putc(s, 0); /* Capability Length */
		stream_putc(s, CAPABILITY_CODE_FQDN);
		capp = stream_get_endp(s);
		stream_putc(s, 0); /* dummy len for now */
		len = strlen(cmd_hostname_get());
		if (len > BGP_MAX_HOSTNAME)
			len = BGP_MAX_HOSTNAME;

		stream_putc(s, len);
		stream_put(s, cmd_hostname_get(), len);
		if (cmd_domainname_get()) {
			len = strlen(cmd_domainname_get());
			if (len > BGP_MAX_HOSTNAME)
				len = BGP_MAX_HOSTNAME;

			stream_putc(s, len);
			stream_put(s, cmd_domainname_get(), len);
		} else
			stream_putc(s, 0); /* 0 length */

		/* Set the lengths straight */
		len = stream_get_endp(s) - rcapp - 1;
		ext_opt_params ? stream_putw_at(s, rcapp, len - 1)
			       : stream_putc_at(s, rcapp, len);

		len = stream_get_endp(s) - capp - 1;
		stream_putc_at(s, capp, len);

		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%s Sending hostname cap with hn = %s, dn = %s",
				peer->host, cmd_hostname_get(),
				cmd_domainname_get());
	}

	bgp_peer_send_gr_capability(s, peer, ext_opt_params);
	bgp_peer_send_llgr_capability(s, peer, ext_opt_params);

	/* Software Version capability
	 * An implementation is REQUIRED Extended Optional Parameters
	 * Length for BGP OPEN Message support as defined in [RFC9072].
	 * The inclusion of the Software Version Capability is OPTIONAL.
	 * If an implementation supports the inclusion of the capability,
	 * the implementation MUST include a configuration switch to enable
	 * or disable its use, and that switch MUST be off by default.
	 */
	if (peergroup_flag_check(peer, PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD) ||
	    peergroup_flag_check(peer, PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW)) {
		SET_FLAG(peer->cap, PEER_CAP_SOFT_VERSION_ADV);
		stream_putc(s, BGP_OPEN_OPT_CAP);
		rcapp = stream_get_endp(s);
		ext_opt_params ? stream_putw(s, 0)
			       : stream_putc(s, 0); /* Capability Length */
		stream_putc(s, CAPABILITY_CODE_SOFT_VERSION);
		capp = stream_get_endp(s);
		stream_putc(s, 0); /* dummy placeholder len */

		/* The Capability Length SHOULD be no greater than 64.
		 * This is the limit to allow other capabilities as much
		 * space as they require.
		 */
		len = strlen(cmd_software_version_get());
		if (len > BGP_MAX_SOFT_VERSION)
			len = BGP_MAX_SOFT_VERSION;

		if (peergroup_flag_check(peer, PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD))
			/* For old software version capability, prepend the length byte. */
			stream_putc(s, len);
		stream_put(s, cmd_software_version_get(), len);

		/* Software Version capability Len. */
		len = stream_get_endp(s) - rcapp - 1;
		ext_opt_params ? stream_putw_at(s, rcapp, len - 1)
			       : stream_putc_at(s, rcapp, len);

		/* Total Capability Len. */
		len = stream_get_endp(s) - capp - 1;
		stream_putc_at(s, capp, len);

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s Sending Software Version cap, value: %s",
				   peer->host, cmd_software_version_get());
	}

	/* Total Opt Parm Len. */
	len = stream_get_endp(s) - cp - 1;

	if (ext_opt_params) {
		len = stream_get_endp(s) - eopl - 2;
		stream_putw_at(s, eopl, len);
	} else {
		stream_putc_at(s, cp, len);
	}

	return len;
}
