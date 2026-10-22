// SPDX-License-Identifier: GPL-2.0-or-later
/* Ethernet-VPN Packet and vty Processing File
 * Copyright (C) 2017 6WIND
 *
 * This file is part of FRRouting
 */

/* ============================================================================
 * 【文件导读】bgp_evpn_vty.c —— EVPN 的 CLI（VTY）层：命令解析、show 输出、配置读写
 * ============================================================================
 * 【职责与定位】本文件是 EVPN 的**命令行前端**，不含 EVPN 核心协议逻辑。它做三件事：
 *   1) show 命令族：`show bgp l2vpn evpn ...`（route / vni / es / next-hop / all /
 *      summary / vni ... 等），把 bgp_evpn.c / bgp_evpn_mh.c 里维护的表**遍历并
 *      格式化**成文本或 JSON。
 *   2) 配置命令：`advertise-all-vni`、`vni <n>` 下的 `flood` / `rd` / `route-target`
 *      / `import` / `export` / `advertise-svi-ip` 等；它们改写 EVPN 配置结构并
 *      触发相应的 apply / 重建（例如 flood 变化触发 VNI 通告 / 重新编程）。
 *   3) 配置回写：把 EVPN 配置写进 running-config（bgp_config_write_evpn_info）。
 *
 * 【核心数据结构 / 关键概念】
 *   - AFI_L2VPN / SAFI_EVPN : EVPN 的地址族组合；EVPN 路由表挂在这套 afi/safi 下。
 *   - struct bgpevpn            : 一个 L2 VNI（MAC-VRF，MAC 表项）的本地实例。
 *   - struct bgp_evpn_vrf       : 一个 L3 VNI / 租户 VRF（IP-VRF，IP 前缀）的本地实例。
 *     两者由 bgp_evpn_private.h 定义（本文件不定义，只使用）。
 *   - VNI 类型: L2 = MAC-VRF，L3 = IP-VRF，由 vni / vrf_id 是否非零区分。
 *   - RD（Route Distinguisher）: 每 VNI/VRF 一条，用于 EVPN 路由的区分。
 *   - RT（Route Target / VNI 出/入向路由目标）: 控制 import / export 的扩展团体。
 *   - ESI（Ethernet Segment Identifier）/ ES: 多归属接入，MH 相关（bgp_evpn_mh.h）。
 *   - 显示模式开关: SHOW_DISPLAY_STANDARD / TAGS / OVERLAY，控制 show 的详细程度。
 *
 * 【本文件在主流程中的位置】（谁调它、它调谁）
 *   - 谁调它: bgpd 初始化时调用 bgp_ethernetvpn_init() 注册全部命令；VTY 解析到
 *     这些命令后进入本文件的 handler；bgp_config_write 通过 bgp_config_write_evpn_info
 *     回写配置。
 *   - 它调谁: 遍历 bgp_evpn.c 暴露的 VNI 表 / EVPN 路由表 / import-rt 表、
 *     bgp_evpn_mh.c 暴露的 ES 表，并调用 bgp_evpn.h 的查询/操作函数（如
 *     bgp_evpn_lookup_vni、evpn_XX_install 等）；配置命令还会调用
 *     bgp_evpn_mh.h / bgp_zebra.h 的接口下发/回收硬件状态。
 *   - 本文件基本只“读表 + 格式化”以及“改配置 + 触发 apply”，不直接编码 BGP 报文。
 *
 * 【易错点 / 阅读建议】
 *   - show 输出与 JSON 输出是**两套并行代码**，同一函数里常见 `if (json) {...}
 *     else {...}`；注意两边字段要一一对应，改一处别忘另一处。
 *   - 配置命令（DEFUN/DEFPY）里往往先校验（VNI 是否存在、是否与 L3 VNI 冲突），
 *     再改结构，最后按需调用 apply/重建函数；注释会标注“改了哪个字段 / 是否触发重建”。
 *   - 大量使用 hash 遍历回调（如 vni_walk_ctx + 各 *_walk 回调），请先看清“遍历的
 *     是哪种表、回调里的 ctx 携带了什么”。
 *   建议阅读顺序：文件头常量与 vni_walk_ctx → display_* / show_* 辅助函数
 *                 → show bgp l2vpn evpn 各子命令 DEFUN/DEFPY
 *                 → vni / advertise-* / rd / route-target 等配置命令
 *                 → bgp_config_write_evpn_info（回写）
 *                 → 末尾 bgp_ethernetvpn_init（命令注册总表）。
 * ============================================================================
 */

#include <zebra.h>
#include "command.h"
#include "prefix.h"
#include "lib/json.h"
#include "lib/printfrr.h"
#include "lib/vxlan.h"
#include "stream.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_vpn.h"
#include "bgpd/bgp_evpn_vty.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_evpn_mh.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_addpath.h"

/* 【显示模式】传给 show 辅助函数的“详细程度”开关，决定打印哪些额外信息：
 *   SHOW_DISPLAY_STANDARD : 标准输出（默认）。
 *   SHOW_DISPLAY_TAGS     : 额外打印标签 / 叠加层信息位（see bgp_evpn.c 的 detail 位）。
 *   SHOW_DISPLAY_OVERLAY  : 打印叠加层(overlay)索引，如 L3VNI / gateway-ip 相关列。 */
#define SHOW_DISPLAY_STANDARD 0
#define SHOW_DISPLAY_TAGS 1
#define SHOW_DISPLAY_OVERLAY 2
/* VNI 名字字符串缓冲长度（形如 “VNI 1000” / 十进制最长 8 位，留足余量）。 */
#define VNI_STR_LEN 32

/* 【结构】vni_walk_ctx —— 遍历 VNI 哈希表时传给回调的上下文。
 * 本文件大量使用 hash_walk 回调（参数只有 bucket + args），需要的信息都从这里取。
 * 字段（按代码实际使用）：
 *   bgp       : 当前操作的 bgp 实例（default 或某 VRF）。
 *   vty       : 输出目标（文本模式使用）。
 *   vtep_ip   : 过滤条件——只看该远端 VTEP 关联的条目（用于 vni <n> 下按 VTEP 展示）。
 *   json      : JSON 输出对象；非 NULL 时走 JSON 分支，否则走文本分支。
 *   detail    : 是否明细输出（传自命令行的 detail 关键字）。
 *   type      : 只处理该 EVPN 路由类型过滤（见 bgp_evpn_cli_parse_type）。
 *   mac_table : 是否以“MAC 表(mac_table)”视角展示（决定打印 MAC 还是 IP 前缀）。 */
/*
 * Context for VNI hash walk - used by callbacks.
 */
struct vni_walk_ctx {
	struct bgp *bgp;
	struct vty *vty;
	struct ipaddr vtep_ip;
	json_object *json;
	int detail;
	int type;
	bool mac_table;
};

/* 【函数】argv_find_and_parse_oly_idx —— 解析命令行里的叠加层索引类型。
 * 见 bgp_evpn_vty.h 的同名声明：命中 "gateway-ip" 则返回 OVERLAY_INDEX_GATEWAY_IP，
 * 否则为 OVERLAY_INDEX_TYPE_NONE；始终返回 1（表示解析无致命错误）。 */
int argv_find_and_parse_oly_idx(struct cmd_token **argv, int argc, int *oly_idx,
				enum overlay_index_type *oly)
{
	*oly = OVERLAY_INDEX_TYPE_NONE;
	if (argv_find(argv, argc, "gateway-ip", oly_idx))
		*oly = OVERLAY_INDEX_GATEWAY_IP;
	return 1;
}

/* 【函数】display_vrf_import_rt —— 打印一个“VRF 导入 RT 节点”(vrf_irt_node)：
 * 即某个 route-target 被哪些 **VRF（L3）** 导入。用于 EVPN L3 的 show import-rt。
 *   入参 irt->rt 是扩展团体格式的 RT；先解出 type/subtype，非 ECOMMUNITY_ROUTE_TARGET
 *   直接返回；再按 AS(2B) / IP / AS4(4B) 三种编码把 RT 还原成 “a:b” 文本。
 *   最后遍历 irt->vrfs 列出所有导入该 RT 的 VRF 名字。
 * 文本与 JSON 两条分支并存：json 非 NULL 时填充 json_rt/json_vrfs。 */
static void display_vrf_import_rt(struct vty *vty, struct vrf_irt_node *irt,
				  json_object *json)
{
	const uint8_t *pnt;
	uint8_t type, sub_type;
	struct ecommunity_as eas;
	struct ecommunity_ip eip;
	struct listnode *node, *nnode;
	struct bgp *tmp_bgp_vrf = NULL;
	json_object *json_rt = NULL;
	json_object *json_vrfs = NULL;
	char rt_buf[RT_ADDRSTRLEN];

	pnt = (uint8_t *)&irt->rt.val;
	type = *pnt++;
	sub_type = *pnt++;
	if (sub_type != ECOMMUNITY_ROUTE_TARGET)
		return;

	if (json) {
		json_rt = json_object_new_object();
		json_vrfs = json_object_new_array();
	}

	memset(&eas, 0, sizeof(eas));
	switch (type) {
	case ECOMMUNITY_ENCODE_AS:
		eas.as = (*pnt++ << 8);
		eas.as |= (*pnt++);
		ptr_get_be32(pnt, &eas.val);

		snprintf(rt_buf, sizeof(rt_buf), "%u:%u", eas.as, eas.val);

		if (json)
			json_object_string_add(json_rt, "rt", rt_buf);
		else
			vty_out(vty, "Route-target: %s", rt_buf);

		break;

	case ECOMMUNITY_ENCODE_IP:
		memcpy(&eip.ip, pnt, 4);
		pnt += 4;
		eip.val = (*pnt++ << 8);
		eip.val |= (*pnt++);

		snprintfrr(rt_buf, sizeof(rt_buf), "%pI4:%u", &eip.ip, eip.val);

		if (json)
			json_object_string_add(json_rt, "rt", rt_buf);
		else
			vty_out(vty, "Route-target: %s", rt_buf);

		break;

	case ECOMMUNITY_ENCODE_AS4:
		pnt = ptr_get_be32(pnt, &eas.as);
		eas.val = (*pnt++ << 8);
		eas.val |= (*pnt++);

		snprintf(rt_buf, sizeof(rt_buf), "%u:%u", eas.as, eas.val);

		if (json)
			json_object_string_add(json_rt, "rt", rt_buf);
		else
			vty_out(vty, "Route-target: %s", rt_buf);

		break;

	default:
		/* Clean up */
		json_object_free(json_rt);
		json_object_free(json_vrfs);
		return;
	}

	if (!json) {
		vty_out(vty,
			"\nList of VRFs importing routes with this route-target:\n");
	}

	for (ALL_LIST_ELEMENTS(irt->vrfs, node, nnode, tmp_bgp_vrf)) {
		if (json)
			json_object_array_add(
				json_vrfs,
				json_object_new_string(
					vrf_id_to_name(tmp_bgp_vrf->vrf_id)));
		else
			vty_out(vty, "  %s\n",
				vrf_id_to_name(tmp_bgp_vrf->vrf_id));
	}

	if (json) {
		json_object_object_add(json_rt, "vrfs", json_vrfs);
		json_object_object_add(json, rt_buf, json_rt);
	}
}

/* 【函数】show_vrf_import_rt_entry —— 哈希遍历回调：从 bucket 取出 vrf_irt_node，
 * 转交 display_vrf_import_rt 打印。args[0]=vty，args[1]=json。 */
static void show_vrf_import_rt_entry(struct hash_bucket *bucket, void *args[])
{
	json_object *json = NULL;
	struct vty *vty = NULL;
	struct vrf_irt_node *irt = (struct vrf_irt_node *)bucket->data;

	vty = (struct vty *)args[0];
	json = (struct json_object *)args[1];

	display_vrf_import_rt(vty, irt, json);
}

/* 【函数】display_import_rt —— 与 display_vrf_import_rt 对应，但打印的是某个
 * route-target 被哪些 **VNI（L2）** 导入（irt->vnis 列表，元素为 struct bgpevpn）。
 * RT 解码逻辑与 display_vrf_import_rt 基本相同（AS / IP / AS4）。
 * 注意：源码此处有 “TODO: This needs to go into a function” —— 两个 display_import_rt
 * 的 RT 解码部分是重复代码，将来可能抽出公共函数，阅读时以实际代码为准。 */
static void display_import_rt(struct vty *vty, struct irt_node *irt,
			      json_object *json)
{
	const uint8_t *pnt;
	uint8_t type, sub_type;
	struct ecommunity_as eas;
	struct ecommunity_ip eip;
	struct listnode *node, *nnode;
	struct bgpevpn *tmp_vpn;
	json_object *json_rt = NULL;
	json_object *json_vnis = NULL;
	char rt_buf[RT_ADDRSTRLEN];

	/* TODO: This needs to go into a function */

	pnt = (uint8_t *)&irt->rt.val;
	type = *pnt++;
	sub_type = *pnt++;
	if (sub_type != ECOMMUNITY_ROUTE_TARGET)
		return;

	if (json) {
		json_rt = json_object_new_object();
		json_vnis = json_object_new_array();
	}

	memset(&eas, 0, sizeof(eas));
	switch (type) {
	case ECOMMUNITY_ENCODE_AS:
		eas.as = (*pnt++ << 8);
		eas.as |= (*pnt++);
		ptr_get_be32(pnt, &eas.val);

		snprintf(rt_buf, sizeof(rt_buf), "%u:%u", eas.as, eas.val);

		if (json)
			json_object_string_add(json_rt, "rt", rt_buf);
		else
			vty_out(vty, "Route-target: %s", rt_buf);

		break;

	case ECOMMUNITY_ENCODE_IP:
		memcpy(&eip.ip, pnt, 4);
		pnt += 4;
		eip.val = (*pnt++ << 8);
		eip.val |= (*pnt++);

		snprintfrr(rt_buf, sizeof(rt_buf), "%pI4:%u", &eip.ip, eip.val);

		if (json)
			json_object_string_add(json_rt, "rt", rt_buf);
		else
			vty_out(vty, "Route-target: %s", rt_buf);

		break;

	case ECOMMUNITY_ENCODE_AS4:
		pnt = ptr_get_be32(pnt, &eas.as);
		eas.val = (*pnt++ << 8);
		eas.val |= (*pnt++);

		snprintf(rt_buf, sizeof(rt_buf), "%u:%u", eas.as, eas.val);

		if (json)
			json_object_string_add(json_rt, "rt", rt_buf);
		else
			vty_out(vty, "Route-target: %s", rt_buf);

		break;

	default:
		json_object_free(json_vnis);
		json_object_free(json_rt);
		return;
	}

	if (!json) {
		vty_out(vty,
			"\nList of VNIs importing routes with this route-target:\n");
	}

	for (ALL_LIST_ELEMENTS(irt->vnis, node, nnode, tmp_vpn)) {
		if (json)
			json_object_array_add(
				json_vnis, json_object_new_int(tmp_vpn->vni));
		else
			vty_out(vty, "  %u\n", tmp_vpn->vni);
	}

	if (json) {
		json_object_object_add(json_rt, "vnis", json_vnis);
		json_object_object_add(json, rt_buf, json_rt);
	}
}

/* 【函数】show_import_rt_entry —— 哈希遍历回调：取出 irt_node 交给 display_import_rt。
 * args[0]=vty，args[1]=json。 */
static void show_import_rt_entry(struct hash_bucket *bucket, void *args[])
{
	json_object *json = NULL;
	struct vty *vty = NULL;
	struct irt_node *irt = (struct irt_node *)bucket->data;

	vty = args[0];
	json = args[1];

	display_import_rt(vty, irt, json);

	return;
}

/* 【函数】bgp_evpn_show_route_rd_header —— 打印/输出某条 “RD 分组节点” 的表头。
 * EVPN 路由表在 show 时按 RD 分组；rd_dest 的前缀里携带 RD 值。
 *   按 RD 类型（RD_TYPE_AS / AS4 / IP）把 RD 解码为 “a:b” 文本存入 rd_str，
 *   文本模式打印 “Route Distinguisher: <rd>”，JSON 模式写入 json 的 "rd" 字段。
 * 未知类型输出 "Unknown"。 */
static void bgp_evpn_show_route_rd_header(struct vty *vty,
					  struct bgp_dest *rd_dest,
					  json_object *json, char *rd_str,
					  int len)
{
	uint16_t type;
	struct rd_as rd_as;
	struct rd_ip rd_ip;
	const uint8_t *pnt;
	const struct prefix *p = bgp_dest_get_prefix(rd_dest);

	pnt = p->u.val;

	/* Decode RD type. */
	type = decode_rd_type(pnt);

	if (!json)
		vty_out(vty, "Route Distinguisher: ");

	switch (type) {
	case RD_TYPE_AS:
		decode_rd_as(pnt + 2, &rd_as);
		snprintf(rd_str, len, "%u:%d", rd_as.as, rd_as.val);
		if (json)
			json_object_string_add(json, "rd", rd_str);
		else
			vty_out(vty, "%s\n", rd_str);
		break;

	case RD_TYPE_AS4:
		decode_rd_as4(pnt + 2, &rd_as);
		snprintf(rd_str, len, "%u:%d", rd_as.as, rd_as.val);
		if (json)
			json_object_string_add(json, "rd", rd_str);
		else
			vty_out(vty, "%s\n", rd_str);
		break;

	case RD_TYPE_IP:
		decode_rd_ip(pnt + 2, &rd_ip);
		snprintfrr(rd_str, len, "%pI4:%d", &rd_ip.ip, rd_ip.val);
		if (json)
			json_object_string_add(json, "rd", rd_str);
		else
			vty_out(vty, "%s\n", rd_str);
		break;

	default:
		if (json) {
			snprintf(rd_str, len, "Unknown");
			json_object_string_add(json, "rd", rd_str);
		} else {
			snprintf(rd_str, len, "Unknown RD type");
			vty_out(vty, "%s\n", rd_str);
		}
		break;
	}
}

/* 【函数】bgp_evpn_show_route_header —— 打印 EVPN 路由表的人类可读表头：
 *   - 表版本号(tbl_ver) 与本地 router-id；
 *   - 状态码/起源码图例；
 *   - type-1..type-5 五种 EVPN 前缀的字段布局说明（[EthTag]/[ESI]/[MAC]/[IP] 等）。
 * JSON 模式直接返回（JSON 不需要这些图例文字）。 */
static void bgp_evpn_show_route_header(struct vty *vty, struct bgp *bgp,
				       uint64_t tbl_ver, json_object *json)
{
	char ri_header[] =
		"   Network          Next Hop            Metric LocPrf Weight Path\n";

	if (json)
		return;

	vty_out(vty,
		"BGP table version is %" PRIu64 ", local router ID is %pI4\n",
		tbl_ver, &bgp->router_id);
	vty_out(vty,
		"Status codes: s suppressed, d damped, h history, * valid, > best, i - internal\n");
	vty_out(vty, "Origin codes: i - IGP, e - EGP, ? - incomplete\n");
	vty_out(vty,
		"EVPN type-1 prefix: [1]:[EthTag]:[ESI]:[IPlen]:[VTEP-IP]:[Frag-id]\n");
	vty_out(vty,
		"EVPN type-2 prefix: [2]:[EthTag]:[MAClen]:[MAC]:[IPlen]:[IP]\n");
	vty_out(vty, "EVPN type-3 prefix: [3]:[EthTag]:[IPlen]:[OrigIP]\n");
	vty_out(vty, "EVPN type-4 prefix: [4]:[ESI]:[IPlen]:[OrigIP]\n");
	vty_out(vty, "EVPN type-5 prefix: [5]:[EthTag]:[IPlen]:[IP]\n\n");
	vty_out(vty, "%s", ri_header);
}

/* 【函数】display_l3vni —— 打印一个 **L3 VNI（IP-VRF / 租户 VRF）** 的详情。
 * 信息来源为 struct bgp（该 VRF 的 bgp 实例）：l3vni、RD(vrf_prd)、originator IP、
 * 站点 MAC(site-of-origin)、System-IP / System-MAC(advertise-pip)、Router-MAC，
 * 以及该 VRF 的 import / export Route Target 列表(vrf_import_rtl / vrf_export_rtl)。
 * L3 VNI 的 Advertise-gw-macip / Advertise-svi-macip 恒为 "n/a"（那是 L2 的概念）。
 * 文本与 JSON 双分支。 */
static void display_l3vni(struct vty *vty, struct bgp *bgp_vrf,
			  json_object *json)
{
	char *ecom_str;
	struct listnode *node, *nnode;
	struct vrf_route_target *l3rt;
	struct bgp *bgp_evpn = NULL;
	json_object *json_import_rtl = NULL;
	json_object *json_export_rtl = NULL;

	bgp_evpn = bgp_get_evpn();
	json_import_rtl = json_export_rtl = 0;

	if (json) {
		json_import_rtl = json_object_new_array();
		json_export_rtl = json_object_new_array();
		json_object_int_add(json, "vni", bgp_vrf->l3vni);
		json_object_string_add(json, "type", "L3");
		json_object_string_add(json, "inKernel", "True");
		json_object_string_addf(json, "rd",
					BGP_RD_AS_FORMAT(bgp_vrf->asnotation),
					&bgp_vrf->vrf_prd);
		json_object_string_addf(json, "originatorIp", "%pIA",
					&bgp_vrf->originator_ip);
		if (bgp_evpn && bgp_evpn->evpn_info) {
			ecom_str = ecommunity_ecom2str(
				bgp_evpn->evpn_info->soo,
				ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			json_object_string_add(json, "siteOfOrigin", ecom_str);
			ecommunity_strfree(&ecom_str);
		}
		json_object_string_add(json, "advertiseGatewayMacip", "n/a");
		json_object_string_add(json, "advertiseSviMacIp", "n/a");
		if (bgp_vrf->evpn_info) {
			json_object_string_add(json, "advertisePip",
					       bgp_vrf->evpn_info->advertise_pip
						       ? "Enabled"
						       : "Disabled");
			json_object_string_addf(json, "sysIP", "%pIA", &bgp_vrf->evpn_info->pip_ip);
			json_object_string_addf(json, "sysMac", "%pEA",
						&bgp_vrf->evpn_info->pip_rmac);
		}
		json_object_string_addf(json, "rmac", "%pEA", &bgp_vrf->rmac);
	} else {
		vty_out(vty, "VNI: %d", bgp_vrf->l3vni);
		vty_out(vty, " (known to the kernel)");
		vty_out(vty, "\n");

		vty_out(vty, "  Type: %s\n", "L3");
		vty_out(vty, "  Tenant VRF: %s\n",
			vrf_id_to_name(bgp_vrf->vrf_id));
		vty_out(vty, "  RD: ");
		vty_out(vty, BGP_RD_AS_FORMAT(bgp_vrf->asnotation),
			&bgp_vrf->vrf_prd);
		vty_out(vty, "\n");
		vty_out(vty, "  Originator IP: %pIA\n",
			&bgp_vrf->originator_ip);
		if (bgp_evpn && bgp_evpn->evpn_info) {
			ecom_str = ecommunity_ecom2str(
				bgp_evpn->evpn_info->soo,
				ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			vty_out(vty, "  MAC-VRF Site-of-Origin: %s\n",
				ecom_str);
			ecommunity_strfree(&ecom_str);
		}
		vty_out(vty, "  Advertise-gw-macip : %s\n", "n/a");
		vty_out(vty, "  Advertise-svi-macip : %s\n", "n/a");
		if (bgp_vrf->evpn_info) {
			vty_out(vty, "  Advertise-pip: %s\n",
				bgp_vrf->evpn_info->advertise_pip ? "Yes"
								  : "No");
			vty_out(vty, "  System-IP: %pIA\n", &bgp_vrf->evpn_info->pip_ip);
			vty_out(vty, "  System-MAC: %pEA\n",
				&bgp_vrf->evpn_info->pip_rmac);
		}
		vty_out(vty, "  Router-MAC: %pEA\n", &bgp_vrf->rmac);
	}

	if (!json)
		vty_out(vty, "  Import Route Target:\n");

	for (ALL_LIST_ELEMENTS(bgp_vrf->vrf_import_rtl, node, nnode, l3rt)) {
		ecom_str = ecommunity_ecom2str(l3rt->ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json)
			json_object_array_add(json_import_rtl,
					      json_object_new_string(ecom_str));
		else
			vty_out(vty, "    %s\n", ecom_str);

		ecommunity_strfree(&ecom_str);
	}

	if (json)
		json_object_object_add(json, "importRts", json_import_rtl);
	else
		vty_out(vty, "  Export Route Target:\n");

	for (ALL_LIST_ELEMENTS(bgp_vrf->vrf_export_rtl, node, nnode, l3rt)) {
		ecom_str = ecommunity_ecom2str(l3rt->ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json)
			json_object_array_add(json_export_rtl,
					      json_object_new_string(ecom_str));
		else
			vty_out(vty, "    %s\n", ecom_str);

		ecommunity_strfree(&ecom_str);
	}

	if (json)
		json_object_object_add(json, "exportRts", json_export_rtl);
}

/* 【函数】display_vni —— 打印一个 **L2 VNI（MAC-VRF）** 的详情。
 * 入参 struct bgpevpn 携带：vni、租户 VRF(tenant_vrf_id)、RD(prd)、originator IP、
 * 组播组(mcast_grp)、SVI 接口(svi_ifindex，经 ifindex2ifname 转名)、
 * 站点 MAC、以及三个开关的显示：advertise-gw-macip / advertise-svi-macip /
 * （L2 没有 advertise-pip）。这里对 gw/svi macip 做“三级显示”：
 *   本 VNI 未显式开启但全局开启 → "Active"；
 *   本 VNI 显式开启 → "Enabled"；
 *   否则 → "Disabled"。
 * 最后列 import / export Route Target(import_rtl / export_rtl)。
 * 文本与 JSON 双分支，字段需一一对应。 */
static void display_vni(struct vty *vty, struct bgpevpn *vpn, json_object *json)
{
	char *ecom_str;
	struct listnode *node, *nnode;
	struct ecommunity *ecom;
	json_object *json_import_rtl = NULL;
	json_object *json_export_rtl = NULL;
	struct bgp *bgp_evpn;
	enum asnotation_mode asnotation;

	bgp_evpn = bgp_get_evpn();
	asnotation = bgp_get_asnotation(bgp_evpn);

	if (json) {
		json_import_rtl = json_object_new_array();
		json_export_rtl = json_object_new_array();
		json_object_int_add(json, "vni", vpn->vni);
		json_object_string_add(json, "type", "L2");
		json_object_string_add(json, "inKernel",
				       is_vni_live(vpn) ? "True" : "False");
		json_object_string_addf(
			json, "rd", BGP_RD_AS_FORMAT(asnotation), &vpn->prd);
		json_object_string_addf(json, "originatorIp", "%pIA",
					&vpn->originator_ip);
		json_object_string_addf(json, "mcastGroup", "%pI4",
					&vpn->mcast_grp);
		if (bgp_evpn && bgp_evpn->evpn_info) {
			ecom_str = ecommunity_ecom2str(
				bgp_evpn->evpn_info->soo,
				ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			json_object_string_add(json, "siteOfOrigin", ecom_str);
			ecommunity_strfree(&ecom_str);
		}
		/* per vni knob is enabled -- Enabled
		 * Global knob is enabled  -- Active
		 * default  -- Disabled
		 */
		if (!vpn->advertise_gw_macip &&
		    bgp_evpn && bgp_evpn->advertise_gw_macip)
			json_object_string_add(json, "advertiseGatewayMacip",
					       "Active");
		else if (vpn->advertise_gw_macip)
			json_object_string_add(json, "advertiseGatewayMacip",
					       "Enabled");
		else
			json_object_string_add(json, "advertiseGatewayMacip",
					       "Disabled");
		if (!vpn->advertise_svi_macip && bgp_evpn &&
		    bgp_evpn->evpn_info &&
		    bgp_evpn->evpn_info->advertise_svi_macip)
			json_object_string_add(json, "advertiseSviMacIp",
					       "Active");
		else if (vpn->advertise_svi_macip)
			json_object_string_add(json, "advertiseSviMacIp",
					       "Enabled");
		else
			json_object_string_add(json, "advertiseSviMacIp",
					       "Disabled");
		json_object_string_add(
			json, "sviInterface",
			ifindex2ifname(vpn->svi_ifindex, vpn->tenant_vrf_id));
	} else {
		vty_out(vty, "VNI: %u", vpn->vni);
		if (is_vni_live(vpn))
			vty_out(vty, " (known to the kernel)");
		vty_out(vty, "\n");

		vty_out(vty, "  Type: %s\n", "L2");
		vty_out(vty, "  Tenant-Vrf: %s\n",
			vrf_id_to_name(vpn->tenant_vrf_id));
		vty_out(vty, "  RD: ");
		vty_out(vty, BGP_RD_AS_FORMAT(asnotation), &vpn->prd);
		vty_out(vty, "\n");
		vty_out(vty, "  Originator IP: %pIA\n", &vpn->originator_ip);
		vty_out(vty, "  Mcast group: %pI4\n", &vpn->mcast_grp);
		if (bgp_evpn && bgp_evpn->evpn_info) {
			ecom_str = ecommunity_ecom2str(
				bgp_evpn->evpn_info->soo,
				ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			vty_out(vty, "  MAC-VRF Site-of-Origin: %s\n",
				ecom_str);
			ecommunity_strfree(&ecom_str);
		}
		if (!vpn->advertise_gw_macip &&
		    bgp_evpn && bgp_evpn->advertise_gw_macip)
			vty_out(vty, "  Advertise-gw-macip : %s\n",
				"Active");
		else if (vpn->advertise_gw_macip)
			vty_out(vty, "  Advertise-gw-macip : %s\n",
				"Enabled");
		else
			vty_out(vty, "  Advertise-gw-macip : %s\n",
				"Disabled");
		if (!vpn->advertise_svi_macip && bgp_evpn &&
		    bgp_evpn->evpn_info &&
		    bgp_evpn->evpn_info->advertise_svi_macip)
			vty_out(vty, "  Advertise-svi-macip : %s\n",
				"Active");
		else if (vpn->advertise_svi_macip)
			vty_out(vty, "  Advertise-svi-macip : %s\n",
				"Enabled");
		else
			vty_out(vty, "  Advertise-svi-macip : %s\n",
				"Disabled");
		vty_out(vty, "  SVI interface : %s\n",
			ifindex2ifname(vpn->svi_ifindex, vpn->tenant_vrf_id));
	}

	if (!json)
		vty_out(vty, "  Import Route Target:\n");

	for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode, ecom)) {
		ecom_str = ecommunity_ecom2str(ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json)
			json_object_array_add(json_import_rtl,
					      json_object_new_string(ecom_str));
		else
			vty_out(vty, "    %s\n", ecom_str);

		ecommunity_strfree(&ecom_str);
	}

	if (json)
		json_object_object_add(json, "importRts", json_import_rtl);
	else
		vty_out(vty, "  Export Route Target:\n");

	for (ALL_LIST_ELEMENTS(vpn->export_rtl, node, nnode, ecom)) {
		ecom_str = ecommunity_ecom2str(ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json)
			json_object_array_add(json_export_rtl,
					      json_object_new_string(ecom_str));
		else
			vty_out(vty, "    %s\n", ecom_str);

		ecommunity_strfree(&ecom_str);
	}

	if (json)
		json_object_object_add(json, "exportRts", json_export_rtl);
}

/* 【函数】show_esi_routes —— 打印某个 ES(bgp_evpn_es) 自有的 EVPN 路由表
 * (es->route_table) 的全部前缀与路径。用于 `show bgp l2vpn evpn es ...` 之类。
 *   遍历 es->route_table：每个前缀打印其所有路径(route_vty_out)；
 *   表头/图例仅在首次有路径时打印一次(header 标志)；
 *   末尾输出前缀数/路径数(prefix_cnt / path_cnt)，无路径时提示
 *   “No EVPN prefixes exist for this ESI”。JSON 模式下每个前缀一个对象。 */
static void show_esi_routes(struct bgp *bgp,
			    struct bgp_evpn_es *es,
			    struct vty *vty,
			    json_object *json)
{
	int header = 1;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	uint32_t prefix_cnt, path_cnt;
	uint64_t tbl_ver;

	prefix_cnt = path_cnt = 0;

	tbl_ver = es->route_table->version;
	for (dest = bgp_table_top(es->route_table); dest;
	     dest = bgp_route_next(dest)) {
		int add_prefix_to_json = 0;
		json_object *json_paths = NULL;
		json_object *json_prefix = NULL;
		const struct prefix *p = bgp_dest_get_prefix(dest);

		if (json)
			json_prefix = json_object_new_object();

		pi = bgp_dest_get_bgp_path_info(dest);
		if (pi) {
			/* Overall header/legend displayed once. */
			if (header) {
				bgp_evpn_show_route_header(vty, bgp,
							   tbl_ver, json);
				header = 0;
			}

			prefix_cnt++;
		}

		if (json)
			json_paths = json_object_new_array();

		/* For EVPN, the prefix is displayed for each path (to fit in
		 * with code that already exists).
		 */
		for (; pi; pi = pi->next) {
			json_object *json_path = NULL;

			if (json)
				json_path = json_object_new_array();

			route_vty_out(vty, p, pi, 0, NULL, SAFI_EVPN, json_path, false, NULL);

			if (json)
				json_object_array_add(json_paths, json_path);

			path_cnt++;
			add_prefix_to_json = 1;
		}

		if (json) {
			if (add_prefix_to_json) {
				json_object_string_addf(json_prefix, "prefix",
							"%pFX", p);
				json_object_int_add(json_prefix, "prefixLen",
						    p->prefixlen);
				json_object_object_add(json_prefix, "paths",
						       json_paths);
				json_object_object_addf(json, json_prefix,
							"%pFX", p);
			} else {
				json_object_free(json_paths);
				json_object_free(json_prefix);
				json_paths = NULL;
				json_prefix = NULL;
			}
		}
	}

	if (json) {
		json_object_int_add(json, "numPrefix", prefix_cnt);
		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		if (prefix_cnt == 0)
			vty_out(vty, "No EVPN prefixes exist for this ESI\n");
		else
			vty_out(vty, "\nDisplayed %u prefixes (%u paths)\n",
				prefix_cnt, path_cnt);
	}
}

/* 【函数】bgp_evpn_show_routes_mac_ip_es —— 打印与某个 ESI 关联的 MAC-IP 路由。
 * 遍历全局 ES 表(bgp_mh_info->es_rb_tree)：esi 非 NULL 时只处理匹配该 ESI 的 ES。
 *   global_table 决定取自哪个路径链表：
 *     true  → es->macip_global_path_list（全局表里的 MAC-IP 路径）；
 *     false → es->macip_evi_path_list（按 EVI 分组的 MAC-IP 路径）。
 *   只统计/输出 BGP_PATH_VALID 的路径；detail 决定用 route_vty_out_detail 还是
 *   route_vty_out。表头仅打印一次。末尾给出路径计数或“无匹配”提示。 */
/* Display all MAC-IP VNI routes linked to an ES */
static void bgp_evpn_show_routes_mac_ip_es(struct vty *vty, esi_t *esi,
					   json_object *json, int detail,
					   bool global_table)
{
	struct bgp_dest *bd;
	struct bgp_path_info *pi;
	int header = detail ? 0 : 1;
	uint32_t path_cnt;
	struct listnode *node;
	struct bgp_evpn_es *es;
	struct bgp_path_es_info *es_info;
	struct bgp *bgp = bgp_get_evpn();
	json_object *json_paths = NULL;

	if (!bgp)
		return;

	path_cnt = 0;

	if (json)
		json_paths = json_object_new_array();

	RB_FOREACH (es, bgp_es_rb_head, &bgp_mh_info->es_rb_tree) {
		struct list *es_list;

		if (esi && memcmp(esi, &es->esi, sizeof(*esi)))
			continue;

		if (global_table)
			es_list = es->macip_global_path_list;
		else
			es_list = es->macip_evi_path_list;

		for (ALL_LIST_ELEMENTS_RO(es_list, node, es_info)) {
			json_object *json_path = NULL;

			pi = es_info->pi;
			bd = pi->net;

			if (!CHECK_FLAG(pi->flags, BGP_PATH_VALID))
				continue;

			/* Overall header/legend displayed once. */
			if (header) {
				bgp_evpn_show_route_header(vty, bgp, 0, json);
				header = 0;
			}

			path_cnt++;

			if (json)
				json_path = json_object_new_array();

			if (detail)
				route_vty_out_detail(vty, bgp, bd, bgp_dest_get_prefix(bd), pi,
						     AFI_L2VPN, SAFI_EVPN, RPKI_NOT_BEING_USED,
						     json_path, NULL, 0);
			else
				route_vty_out(vty, &bd->rn->p, pi, 0, NULL, SAFI_EVPN, json_path,
					      false, NULL);

			if (json)
				json_object_array_add(json_paths, json_path);
		}
	}

	if (json) {
		json_object_object_add(json, "paths", json_paths);
		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		if (path_cnt == 0)
			vty_out(vty, "There are no MAC-IP ES paths");
		else
			vty_out(vty, "\nDisplayed %u paths\n", path_cnt);
		vty_out(vty, "\n");
	}
}

/* 【函数】bgp_evpn_show_routes_mac_ip_evi_es —— 薄封装：以 global_table=false
 * 调用 bgp_evpn_show_routes_mac_ip_es，展示按 EVI 分组的 MAC-IP 路径。 */
static void bgp_evpn_show_routes_mac_ip_evi_es(struct vty *vty, esi_t *esi,
					       json_object *json, int detail)
{
	bgp_evpn_show_routes_mac_ip_es(vty, esi, json, detail, false);
}

/* 【函数】bgp_evpn_show_routes_mac_ip_global_es —— 薄封装：以 global_table=true
 * 调用 bgp_evpn_show_routes_mac_ip_es，展示全局表里的 MAC-IP 路径。 */
static void bgp_evpn_show_routes_mac_ip_global_es(struct vty *vty, esi_t *esi,
						  json_object *json, int detail)
{
	bgp_evpn_show_routes_mac_ip_es(vty, esi, json, detail, true);
}

/* 【函数】show_vni_routes —— 打印**单个 VNI** 的路由表内容。这是 vni 路由展示的核心。
 *   表选择：mac_table=true → vpn->mac_table（MAC-VRF 的 MAC 表）；
 *           mac_table=false → vpn->ip_table（IP-VRF 的 IP 前缀/远端 MAC-IP）。
 *   type 非 0 时只打印该 EVPN 路由类型(route_type)的条目。
 *   vtep_ip 非 NULL 时只保留下一跳等于该 VTEP 的路径（IPv4 比 attr->nexthop，
 *     IPv6 比 attr->mp_nexthop_global）。
 *   MAC-IP 路由(BGP_EVPN_MAC_IP_ROUTE)的特殊处理：VNI 本地表里的条目常常只存了
 *     MAC 或只存了 IP，这里用 evpn_type2_prefix_global_copy 从 path info 补全成
 *     “全局完整”的 MAC-IP 前缀再打印。
 *   detail 决定 route_vty_out_detail（含属性明细）还是 route_vty_out（精简行）。
 *   末尾输出前缀数/路径数。文本与 JSON 双分支。 */
static void show_vni_routes(struct bgp *bgp, struct bgpevpn *vpn,
			    struct vty *vty, int type, bool mac_table,
			    struct ipaddr *vtep_ip, json_object *json,
			    int detail)
{
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	struct bgp_table *table;
	int header = detail ? 0 : 1;
	uint64_t tbl_ver;
	uint32_t prefix_cnt, path_cnt;

	prefix_cnt = path_cnt = 0;

	if (mac_table)
		table = vpn->mac_table;
	else
		table = vpn->ip_table;

	tbl_ver = table->version;
	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		const struct prefix_evpn *evp =
			(const struct prefix_evpn *)bgp_dest_get_prefix(dest);
		int add_prefix_to_json = 0;
		json_object *json_paths = NULL;
		json_object *json_prefix = NULL;
		const struct prefix *p = bgp_dest_get_prefix(dest);

		if (type && evp->prefix.route_type != type)
			continue;

		if (json)
			json_prefix = json_object_new_object();

		pi = bgp_dest_get_bgp_path_info(dest);
		if (pi) {
			/* Overall header/legend displayed once. */
			if (header) {
				bgp_evpn_show_route_header(vty, bgp,
							   tbl_ver, json);
				header = 0;
			}

			prefix_cnt++;
		}

		if (json)
			json_paths = json_object_new_array();

		/* For EVPN, the prefix is displayed for each path (to fit in
		 * with code that already exists).
		 */
		for (; pi; pi = pi->next) {
			struct prefix tmp_p;
			json_object *json_path = NULL;

			if (vtep_ip && IS_IPADDR_V4(vtep_ip) &&
			    !IPV4_ADDR_SAME(&(vtep_ip->ipaddr_v4), &pi->attr->nexthop))
				continue;

			if (vtep_ip && IS_IPADDR_V6(vtep_ip) &&
			    !IPV6_ADDR_SAME(&(vtep_ip->ipaddr_v6), &(pi->attr->mp_nexthop_global)))
				continue;

			if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
				/*
				 * VNI IP/MAC table prefixes don't have MAC/IP
				 * respectively so make sure it's set from path
				 * info here.
				 */
				if (is_evpn_prefix_ipaddr_none(evp)) {
					/* VNI MAC -> Global */
					evpn_type2_prefix_global_copy(
						(struct prefix_evpn *)&tmp_p,
						evp, NULL /* mac */,
						evpn_type2_path_info_get_ip(
							pi));
				} else {
					/* VNI IP -> Global */
					evpn_type2_prefix_global_copy(
						(struct prefix_evpn *)&tmp_p,
						evp,
						evpn_type2_path_info_get_mac(
							pi),
						NULL /* ip */);
				}
			} else
				memcpy(&tmp_p, p, sizeof(tmp_p));


			if (json)
				json_path = json_object_new_array();

			if (detail)
				route_vty_out_detail(vty, bgp, dest, &tmp_p, pi, AFI_L2VPN,
						     SAFI_EVPN, RPKI_NOT_BEING_USED, json_path,
						     NULL, 0);

			else
				route_vty_out(vty, &tmp_p, pi, 0, NULL, SAFI_EVPN, json_path,
					      false, NULL);

			if (json)
				json_object_array_add(json_paths, json_path);

			path_cnt++;
			add_prefix_to_json = 1;
		}

		if (json) {
			if (add_prefix_to_json) {
				json_object_string_addf(json_prefix, "prefix",
							"%pFX", p);
				json_object_int_add(json_prefix, "prefixLen",
						    p->prefixlen);
				json_object_object_add(json_prefix, "paths",
						       json_paths);
				json_object_object_addf(json, json_prefix,
							"%pFX", p);
			} else {
				json_object_free(json_paths);
				json_object_free(json_prefix);
				json_paths = NULL;
				json_prefix = NULL;
			}
		}
	}

	if (json) {
		json_object_int_add(json, "numPrefix", prefix_cnt);
		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		if (prefix_cnt == 0)
			vty_out(vty, "No EVPN prefixes %sexist for this VNI",
				type ? "(of requested type) " : "");
		else
			vty_out(vty, "\nDisplayed %u prefixes (%u paths)%s\n",
				prefix_cnt, path_cnt,
				type ? " (of requested type)" : "");
		vty_out(vty, "\n");
	}
}

/* 【函数】show_vni_routes_hash —— VNI 哈希遍历回调：对一个 VNI 调用 show_vni_routes，
 * 使用 wctx 里的 bgp / type / mac_table / vtep_ip / detail。JSON 时以 VNI 号字符串
 * 为键挂到上层 json。用于 `show bgp l2vpn evpn route vni <n>` 一类命令。 */
static void show_vni_routes_hash(struct hash_bucket *bucket, void *arg)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	struct vni_walk_ctx *wctx = arg;
	struct vty *vty = wctx->vty;
	json_object *json = wctx->json;
	json_object *json_vni = NULL;
	char vni_str[VNI_STR_LEN];

	snprintf(vni_str, sizeof(vni_str), "%u", vpn->vni);
	if (json) {
		json_vni = json_object_new_object();
		json_object_int_add(json_vni, "vni", vpn->vni);
	} else {
		vty_out(vty, "\nVNI: %u\n\n", vpn->vni);
	}

	show_vni_routes(wctx->bgp, vpn, wctx->vty, wctx->type, wctx->mac_table,
			&wctx->vtep_ip, json_vni, wctx->detail);

	if (json)
		json_object_object_add(json, vni_str, json_vni);
}

/* 【函数】show_vni_routes_all_hash —— VNI 哈希遍历回调：对一个 VNI **两张表都打**：
 *   先 show_vni_routes(mac_table=false) 打 IP 表，再打 MAC 表(mac_table=true)。
 * JSON 时把 MAC 表挂在 json_vni 下的 "macTable"。用于不带 mac_table 限定的
 * “show … route vni <n>” 概览。 */
static void show_vni_routes_all_hash(struct hash_bucket *bucket, void *arg)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	struct vni_walk_ctx *wctx = arg;
	struct vty *vty = wctx->vty;
	json_object *json = wctx->json;
	json_object *json_vni = NULL;
	json_object *json_vni_mac = NULL;
	char vni_str[VNI_STR_LEN];

	snprintf(vni_str, sizeof(vni_str), "%u", vpn->vni);
	if (json) {
		json_vni = json_object_new_object();
		json_object_int_add(json_vni, "vni", vpn->vni);
	} else {
		vty_out(vty, "\nVNI: %u\n\n", vpn->vni);
	}

	show_vni_routes(wctx->bgp, vpn, wctx->vty, 0, false, &wctx->vtep_ip,
			json_vni, wctx->detail);

	if (json)
		json_object_object_add(json, vni_str, json_vni);

	if (json)
		json_vni_mac = json_object_new_object();
	else
		vty_out(vty, "\nVNI: %u MAC Table\n\n", vpn->vni);

	show_vni_routes(wctx->bgp, vpn, wctx->vty, 0, true, &wctx->vtep_ip,
			json_vni_mac, wctx->detail);

	if (json)
		json_object_object_add(json_vni, "macTable", json_vni_mac);
}

/* 【函数】show_l3vni_entry —— 打印**一行** L3 VNI 摘要（用于 `show bgp l2vpn evpn vni`
 * 的表格视图）。bgp->l3vni 为 0 直接返回（该 bgp 不是 L3 VNI）。
 *   文本模式一行列：in-kernel 标记(*)、VNI、类型(L3)、RD、import RT、export RT、
 *   站点 MAC、租户 VRF 名；多 RT 时只显示第一个并加 “, ...”。
 *   JSON 模式组装成结构化对象，键为 VNI 号字符串。 */
static void show_l3vni_entry(struct vty *vty, struct bgp *bgp,
			     json_object *json)
{
	json_object *json_vni = NULL;
	json_object *json_import_rtl = NULL;
	json_object *json_export_rtl = NULL;
	char buf1[10];
	char buf2[INET6_ADDRSTRLEN];
	char rt_buf[25];
	char *ecom_str;
	struct listnode *node, *nnode;
	struct vrf_route_target *l3rt;
	struct bgp *bgp_evpn;

	if (!bgp->l3vni)
		return;

	bgp_evpn = bgp_get_evpn();

	if (json) {
		json_vni = json_object_new_object();
		json_import_rtl = json_object_new_array();
		json_export_rtl = json_object_new_array();
	}

	/* if an l3vni is present in bgp it is live */
	buf1[0] = '\0';
	snprintf(buf1, sizeof(buf1), "*");

	if (json) {
		json_object_int_add(json_vni, "vni", bgp->l3vni);
		json_object_string_add(json_vni, "type", "L3");
		json_object_string_add(json_vni, "inKernel", "True");
		json_object_string_addf(json_vni, "originatorIp", "%pIA",
					&bgp->originator_ip);
		json_object_string_addf(json_vni, "rd",
					BGP_RD_AS_FORMAT(bgp->asnotation),
					&bgp->vrf_prd);
		json_object_string_add(json_vni, "advertiseGatewayMacip",
				       "n/a");
		json_object_string_add(json_vni, "advertiseSviMacIp", "n/a");
		json_object_string_add(
			json_vni, "advertisePip",
			bgp->evpn_info->advertise_pip ? "Enabled" : "Disabled");
		json_object_string_addf(json_vni, "sysIP", "%pIA", &bgp->evpn_info->pip_ip);
		json_object_string_add(json_vni, "sysMAC",
				       prefix_mac2str(&bgp->evpn_info->pip_rmac,
						      buf2, sizeof(buf2)));
		json_object_string_add(
			json_vni, "rmac",
			prefix_mac2str(&bgp->rmac, buf2, sizeof(buf2)));
	} else {
		vty_out(vty, "%-1s %-10u %-4s ", buf1, bgp->l3vni, "L3");
		vty_out(vty, BGP_RD_AS_FORMAT_SPACE(bgp->asnotation),
			&bgp->vrf_prd);
	}

	for (ALL_LIST_ELEMENTS(bgp->vrf_import_rtl, node, nnode, l3rt)) {
		ecom_str = ecommunity_ecom2str(l3rt->ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json) {
			json_object_array_add(json_import_rtl,
					      json_object_new_string(ecom_str));
		} else {
			if (listcount(bgp->vrf_import_rtl) > 1)
				snprintf(rt_buf, sizeof(rt_buf), "%s, ...",
					 ecom_str);
			else
				snprintf(rt_buf, sizeof(rt_buf), "%s",
					 ecom_str);
			vty_out(vty, " %-25s", rt_buf);
		}

		ecommunity_strfree(&ecom_str);

		/* If there are multiple import RTs we break here and show only
		 * one */
		if (!json)
			break;
	}

	if (json)
		json_object_object_add(json_vni, "importRTs", json_import_rtl);

	for (ALL_LIST_ELEMENTS(bgp->vrf_export_rtl, node, nnode, l3rt)) {
		ecom_str = ecommunity_ecom2str(l3rt->ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json) {
			json_object_array_add(json_export_rtl,
					      json_object_new_string(ecom_str));
		} else {
			if (listcount(bgp->vrf_export_rtl) > 1)
				snprintf(rt_buf, sizeof(rt_buf), "%s, ...",
					 ecom_str);
			else
				snprintf(rt_buf, sizeof(rt_buf), "%s",
					 ecom_str);
			vty_out(vty, " %-25s", rt_buf);
		}

		ecommunity_strfree(&ecom_str);

		/* If there are multiple export RTs we break here and show only
		 * one */
		if (!json) {
			if (bgp_evpn && bgp_evpn->evpn_info) {
				ecom_str = ecommunity_ecom2str(
					bgp_evpn->evpn_info->soo,
					ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
				vty_out(vty, " %-25s", ecom_str);
				ecommunity_strfree(&ecom_str);
			}
			vty_out(vty, " %-37s", vrf_id_to_name(bgp->vrf_id));
			break;
		}
	}

	if (json) {
		char vni_str[VNI_STR_LEN];

		json_object_object_add(json_vni, "exportRTs", json_export_rtl);
		if (bgp_evpn && bgp_evpn->evpn_info) {
			ecom_str = ecommunity_ecom2str(
				bgp_evpn->evpn_info->soo,
				ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			json_object_string_add(json_vni, "siteOfOrigin",
					       ecom_str);
			ecommunity_strfree(&ecom_str);
		}
		snprintf(vni_str, sizeof(vni_str), "%u", bgp->l3vni);
		json_object_object_add(json, vni_str, json_vni);
	} else
		vty_out(vty, "\n");
}

/* 【函数】show_vni_entry —— VNI 哈希遍历回调：打印**一行** L2 VNI 摘要（对应
 * `show bgp l2vpn evpn vni` 表格）。文本一行列：in-kernel 标记(*)、VNI、类型(L2)、
 * RD、import RT、export RT、站点 MAC、租户 VRF 名；多 RT 时只显示第一个并加 “, ...”。
 * JSON 组装为结构化对象。args[0]=vty，args[1]=json。 */
static void show_vni_entry(struct hash_bucket *bucket, void *args[])
{
	struct vty *vty;
	json_object *json;
	json_object *json_vni = NULL;
	json_object *json_import_rtl = NULL;
	json_object *json_export_rtl = NULL;
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	char buf1[10];
	char rt_buf[25];
	char *ecom_str;
	struct listnode *node, *nnode;
	struct ecommunity *ecom;
	struct bgp *bgp_evpn;
	enum asnotation_mode asnotation;

	vty = args[0];
	json = args[1];

	bgp_evpn = bgp_get_evpn();
	asnotation = bgp_get_asnotation(bgp_evpn);

	if (json) {
		json_vni = json_object_new_object();
		json_import_rtl = json_object_new_array();
		json_export_rtl = json_object_new_array();
	}

	buf1[0] = '\0';
	if (is_vni_live(vpn))
		snprintf(buf1, sizeof(buf1), "*");

	if (json) {
		json_object_int_add(json_vni, "vni", vpn->vni);
		json_object_string_add(json_vni, "type", "L2");
		json_object_string_add(json_vni, "inKernel",
				       is_vni_live(vpn) ? "True" : "False");
		json_object_string_addf(json_vni, "rd",
					BGP_RD_AS_FORMAT(asnotation),
					&vpn->prd);
		json_object_string_addf(json_vni, "originatorIp", "%pIA",
					&vpn->originator_ip);
		json_object_string_addf(json_vni, "mcastGroup", "%pI4",
					&vpn->mcast_grp);
		/* per vni knob is enabled -- Enabled
		 * Global knob is enabled  -- Active
		 * default  -- Disabled
		 */
		if (!vpn->advertise_gw_macip && bgp_evpn
		    && bgp_evpn->advertise_gw_macip)
			json_object_string_add(
				json_vni, "advertiseGatewayMacip", "Active");
		else if (vpn->advertise_gw_macip)
			json_object_string_add(
				json_vni, "advertiseGatewayMacip", "Enabled");
		else
			json_object_string_add(
				json_vni, "advertiseGatewayMacip", "Disabled");
		if (!vpn->advertise_svi_macip && bgp_evpn
		    && bgp_evpn->evpn_info->advertise_svi_macip)
			json_object_string_add(json_vni, "advertiseSviMacIp",
					       "Active");
		else if (vpn->advertise_svi_macip)
			json_object_string_add(json_vni, "advertiseSviMacIp",
					       "Enabled");
		else
			json_object_string_add(json_vni, "advertiseSviMacIp",
					       "Disabled");
	} else {
		vty_out(vty, "%-1s %-10u %-4s ", buf1, vpn->vni, "L2");
		vty_out(vty, BGP_RD_AS_FORMAT_SPACE(asnotation), &vpn->prd);
	}

	for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode, ecom)) {
		ecom_str = ecommunity_ecom2str(ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json) {
			json_object_array_add(json_import_rtl,
					      json_object_new_string(ecom_str));
		} else {
			if (listcount(vpn->import_rtl) > 1)
				snprintf(rt_buf, sizeof(rt_buf), "%s, ...",
					 ecom_str);
			else
				snprintf(rt_buf, sizeof(rt_buf), "%s",
					 ecom_str);
			vty_out(vty, " %-25s", rt_buf);
		}

		XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);

		/* If there are multiple import RTs we break here and show only
		 * one */
		if (!json)
			break;
	}

	if (json)
		json_object_object_add(json_vni, "importRTs", json_import_rtl);

	for (ALL_LIST_ELEMENTS(vpn->export_rtl, node, nnode, ecom)) {
		ecom_str = ecommunity_ecom2str(ecom,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

		if (json) {
			json_object_array_add(json_export_rtl,
					      json_object_new_string(ecom_str));
		} else {
			if (listcount(vpn->export_rtl) > 1)
				snprintf(rt_buf, sizeof(rt_buf), "%s, ...",
					 ecom_str);
			else
				snprintf(rt_buf, sizeof(rt_buf), "%s",
					 ecom_str);
			vty_out(vty, " %-25s", rt_buf);
		}

		XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);

		/* If there are multiple export RTs we break here and show only
		 * one */
		if (!json) {
			if (bgp_evpn && bgp_evpn->evpn_info) {
				ecom_str = ecommunity_ecom2str(
					bgp_evpn->evpn_info->soo,
					ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
				vty_out(vty, " %-25s", ecom_str);
				ecommunity_strfree(&ecom_str);
			}
			vty_out(vty, " %-37s",
				vrf_id_to_name(vpn->tenant_vrf_id));
			break;
		}
	}

	if (json) {
		char vni_str[VNI_STR_LEN];

		json_object_object_add(json_vni, "exportRTs", json_export_rtl);
		if (bgp_evpn && bgp_evpn->evpn_info) {
			ecom_str = ecommunity_ecom2str(
				bgp_evpn->evpn_info->soo,
				ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			json_object_string_add(json_vni, "siteOfOrigin",
					       ecom_str);
			ecommunity_strfree(&ecom_str);
		}
		snprintf(vni_str, sizeof(vni_str), "%u", vpn->vni);
		json_object_object_add(json, vni_str, json_vni);
	} else
		vty_out(vty, "\n");
}

/* 【函数】bgp_show_ethernet_vpn —— `show bgp l2vpn evpn ...` 的**总引擎**。
 * 在 EVPN 地址族(bgp->rib[AFI_L2VPN][SAFI_EVPN])下遍历“RD 分组 → 前缀 → 路径”。
 *   prd       : 只显示该 RD（NULL 表示全部 RD）。
 *   type      : bgp_show_type_* 过滤类型（邻居 / 团体 / 扩展团体 / 大团体 等），
 *               决定用哪套 attr 做匹配（output_arg 是匹配目标）。
 *   option    : SHOW_DISPLAY_STANDARD / TAGS / OVERLAY，选择打印格式
 *               （route_vty_out / route_vty_out_tag / route_vty_out_overlay）。
 *   use_json  : JSON 或文本。
 *   逐 RD 打印表头(bgp_evpn_show_route_rd_header)、必要时打印整体表头
 *   (bgp_evpn_show_route_header 或 TAG/OVERLAY 头)。末尾统计并输出
 *   output_count / total_count；无 bgp 实例时文本提示或输出 "{}"。
 * 多个 show 子命令（含 rd / tags / neighbors 变体）复用本函数。 */
static int bgp_show_ethernet_vpn(struct vty *vty, struct prefix_rd *prd,
				 enum bgp_show_type type, void *output_arg,
				 int option, bool use_json)
{
	afi_t afi = AFI_L2VPN;
	struct bgp *bgp;
	struct bgp_table *table;
	struct bgp_dest *dest;
	struct bgp_dest *rm;
	struct bgp_path_info *pi;
	int rd_header;
	int header = 1;
	char rd_str[RD_ADDRSTRLEN];
	int no_display;

	unsigned long output_count = 0;
	unsigned long total_count = 0;
	json_object *json = NULL;
	json_object *json_array = NULL;
	json_object *json_prefix_info = NULL;

	memset(rd_str, 0, RD_ADDRSTRLEN);

	bgp = bgp_get_evpn();
	if (bgp == NULL) {
		if (!use_json)
			vty_out(vty, "No BGP process is configured\n");
		else
			vty_out(vty, "{}\n");
		return CMD_WARNING;
	}

	if (use_json)
		json = json_object_new_object();

	for (dest = bgp_table_top(bgp->rib[afi][SAFI_EVPN]); dest;
	     dest = bgp_route_next(dest)) {
		uint64_t tbl_ver;
		json_object *json_nroute = NULL;
		const struct prefix *p = bgp_dest_get_prefix(dest);

		if (prd && memcmp(p->u.val, prd->val, 8) != 0)
			continue;

		table = bgp_dest_get_bgp_table_info(dest);
		if (!table)
			continue;

		rd_header = 1;
		tbl_ver = table->version;

		for (rm = bgp_table_top(table); rm; rm = bgp_route_next(rm)) {
			pi = bgp_dest_get_bgp_path_info(rm);
			if (pi == NULL)
				continue;

			no_display = 0;
			for (; pi; pi = pi->next) {
				struct community *picomm = NULL;

				picomm = bgp_attr_get_community(pi->attr);

				total_count++;
				if (type == bgp_show_type_neighbor) {
				        struct peer *peer = output_arg;

					if (peer_cmp(peer, pi->peer) != 0)
						continue;
				}
				if (type == bgp_show_type_extcommunity) {
					struct ecommunity *ecom = output_arg;

					if (!bgp_attr_get_ecommunity(pi->attr) ||
					    !ecommunity_match(bgp_attr_get_ecommunity(pi->attr),
							      ecom))
						continue;
				}
				if (type == bgp_show_type_extcommunity_exact) {
					struct ecommunity *ecom = output_arg;

					if (!bgp_attr_get_ecommunity(pi->attr) ||
					    !ecommunity_cmp(bgp_attr_get_ecommunity(pi->attr),
							    ecom))
						continue;
				}
				if (type == bgp_show_type_lcommunity_exact) {
					struct lcommunity *lcom = output_arg;

					if (!bgp_attr_get_lcommunity(
						    pi->attr) ||
					    !lcommunity_cmp(
						    bgp_attr_get_lcommunity(
							    pi->attr),
						    lcom))
						continue;
				}
				if (type == bgp_show_type_lcommunity) {
					struct lcommunity *lcom = output_arg;

					if (!bgp_attr_get_lcommunity(
						    pi->attr) ||
					    !lcommunity_match(
						    bgp_attr_get_lcommunity(
							    pi->attr),
						    lcom))
						continue;
				}
				if (type == bgp_show_type_community) {
					struct community *com = output_arg;

					if (!picomm ||
					    !community_match(picomm, com))
						continue;
				}
				if (type == bgp_show_type_community_exact) {
					struct community *com = output_arg;

					if (!picomm ||
					    !community_cmp(picomm, com))
						continue;
				}
				if (header) {
					if (use_json) {
						json_object_int_add(
							json, "bgpTableVersion",
							tbl_ver);
						json_object_string_addf(
							json,
							"bgpLocalRouterId",
							"%pI4",
							&bgp->router_id);
						json_object_int_add(
							json,
							"defaultLocPrf",
							bgp->default_local_pref);
						asn_asn2json(json, "localAS",
							     bgp->as,
							     bgp->asnotation);
					} else {
						if (option == SHOW_DISPLAY_TAGS)
							vty_out(vty,
								V4_HEADER_TAG);
						else if (
							option
							== SHOW_DISPLAY_OVERLAY)
							vty_out(vty,
								V4_HEADER_OVERLAY);
						else {
							bgp_evpn_show_route_header(vty, bgp, tbl_ver, NULL);
						}
					}
					header = 0;
				}
				if (rd_header) {
					if (use_json)
						json_nroute =
						       json_object_new_object();
					bgp_evpn_show_route_rd_header(
						vty, dest, json_nroute, rd_str,
						RD_ADDRSTRLEN);
					rd_header = 0;
				}
				if (use_json && !json_array)
					json_array = json_object_new_array();

				if (option == SHOW_DISPLAY_TAGS)
					route_vty_out_tag(
						vty, bgp_dest_get_prefix(rm),
						pi, no_display, SAFI_EVPN,
						json_array);
				else if (option == SHOW_DISPLAY_OVERLAY)
					route_vty_out_overlay(
						vty, bgp_dest_get_prefix(rm),
						pi, no_display, json_array);
				else
					route_vty_out(vty, bgp_dest_get_prefix(rm), pi, no_display,
						      NULL, SAFI_EVPN, json_array, false, NULL);
				no_display = 1;
			}

			if (no_display)
				output_count++;

			if (use_json && json_array) {
				const struct prefix *pfx =
					bgp_dest_get_prefix(rm);

				json_prefix_info = json_object_new_object();

				json_object_string_addf(json_prefix_info,
							"prefix", "%pFX", pfx);

				json_object_int_add(json_prefix_info,
						    "prefixLen", pfx->prefixlen);

				json_object_object_add(json_prefix_info,
					"paths", json_array);
				json_object_object_addf(json_nroute,
							json_prefix_info,
							"%pFX", pfx);
				json_array = NULL;
			}
		}

		if (use_json && json_nroute)
			json_object_object_add(json, rd_str, json_nroute);
	}

	if (use_json) {
		json_object_int_add(json, "numPrefix", output_count);
		json_object_int_add(json, "totalPrefix", total_count);
		vty_json(vty, json);
	} else {
		if (output_count == 0)
			vty_out(vty, "No prefixes displayed, %ld exist\n",
				total_count);
		else
			vty_out(vty,
				"\nDisplayed %ld out of %ld total prefixes\n",
				output_count, total_count);
	}
	return CMD_SUCCESS;
}

/* 【命令】show [ip] bgp l2vpn evpn [json]
 * 语法：显示整个 EVPN 地址族的全部路由（标准显示）。可选 [json] 输出 JSON。
 * 行为：调用 bgp_show_ethernet_vpn(prd=NULL, normal, STANDARD)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn,
      show_ip_bgp_l2vpn_evpn_cmd,
      "show [ip] bgp l2vpn evpn [json]",
      SHOW_STR IP_STR BGP_STR L2VPN_HELP_STR EVPN_HELP_STR JSON_STR)
{
	return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_normal, NULL,
				     SHOW_DISPLAY_STANDARD,
				     use_json(argc, argv));
}

/* 【命令】show [ip] bgp l2vpn evpn rd <RD|all> [json]
 * 语法：显示指定 RD（或 all）下的 EVPN 路由。
 * 行为：all 走 prd=NULL 全量；否则 str2prefix_rd 解析 RD 字符串，失败报
 *   "%% Malformed Route Distinguisher"。再调 bgp_show_ethernet_vpn(STANDARD)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_rd,
      show_ip_bgp_l2vpn_evpn_rd_cmd,
      "show [ip] bgp l2vpn evpn rd <ASN:NN_OR_IP-ADDRESS:NN|all> [json]",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information for a route distinguisher\n"
      "VPN Route Distinguisher\n"
      "All VPN Route Distinguishers\n"
      JSON_STR)
{
	int idx_ext_community = 0;
	int ret;
	struct prefix_rd prd;
	int rd_all = 0;

	if (argv_find(argv, argc, "all", &rd_all))
		return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_normal,
					     NULL, SHOW_DISPLAY_STANDARD,
					     use_json(argc, argv));

	argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN", &idx_ext_community);
	ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}
	return bgp_show_ethernet_vpn(vty, &prd, bgp_show_type_normal, NULL,
				     SHOW_DISPLAY_STANDARD,
				     use_json(argc, argv));
}

/* 【命令】show [ip] bgp l2vpn evpn all tags
 * 语法：显示全部 EVPN 前缀的 BGP 标签（tag 视角）。
 * 行为：bgp_show_ethernet_vpn(prd=NULL, normal, SHOW_DISPLAY_TAGS)，内部走
 *   route_vty_out_tag 打印标签列。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_all_tags,
      show_ip_bgp_l2vpn_evpn_all_tags_cmd,
      "show [ip] bgp l2vpn evpn all tags",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information about all EVPN NLRIs\n"
      "Display BGP tags for prefixes\n")
{
	return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_normal, NULL,
				     SHOW_DISPLAY_TAGS, 0);
}

/* 【命令】show [ip] bgp l2vpn evpn rd <RD|all> tags
 * 语法：显示指定 RD 下 EVPN 前缀的 BGP 标签。
 * 行为：all → 全量；否则解析 RD；均以 SHOW_DISPLAY_TAGS 调用 bgp_show_ethernet_vpn。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_rd_tags,
      show_ip_bgp_l2vpn_evpn_rd_tags_cmd,
      "show [ip] bgp l2vpn evpn rd <ASN:NN_OR_IP-ADDRESS:NN|all> tags",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information for a route distinguisher\n"
      "VPN Route Distinguisher\n"
      "All VPN Route Distinguishers\n"
      "Display BGP tags for prefixes\n")
{
	int idx_ext_community = 0;
	int ret;
	struct prefix_rd prd;
	int rd_all = 0;

	if (argv_find(argv, argc, "all", &rd_all))
		return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_normal,
					     NULL, SHOW_DISPLAY_TAGS, 0);

	argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN", &idx_ext_community);
	ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}
	return bgp_show_ethernet_vpn(vty, &prd, bgp_show_type_normal, NULL,
				     SHOW_DISPLAY_TAGS, 0);
}

/* 【命令】show [ip] bgp l2vpn evpn neighbors <addr|WORD> routes [json]
 * 语法：显示从指定邻居学到的 EVPN 路由。
 * 行为：先 bgp_vty_find_and_parse_afi_safi_bgp 定位 bgp 实例，再 peer_lookup_in_view
 *   找邻居；邻居不存在或未启用 L2VPN/EVPN 地址族则报错（JSON 时给出 warning 对象）。
 *   成功则以 bgp_show_type_neighbor 调 bgp_show_ethernet_vpn（output_arg=peer）。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_neighbor_routes,
      show_ip_bgp_l2vpn_evpn_neighbor_routes_cmd,
      "show [ip] bgp l2vpn evpn neighbors <A.B.C.D|X:X::X:X|WORD> routes [json]",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Detailed information on TCP and BGP neighbor connections\n"
      "IPv4 Neighbor to display information about\n"
      "IPv6 Neighbor to display information about\n"
      "Neighbor on BGP configured interface\n"
      "Display routes learned from neighbor\n" JSON_STR)
{
	int idx = 0;
	struct peer *peer;
	char *peerstr = NULL;
	bool uj = use_json(argc, argv);
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct bgp *bgp = NULL;

	bgp_vty_find_and_parse_afi_safi_bgp(vty, argv, argc, &idx, &afi, &safi,
					    &bgp, uj);
	if (!idx) {
	        vty_out(vty, "No index\n");
		return CMD_WARNING;
	}

	/* neighbors <A.B.C.D|X:X::X:X|WORD> */
	argv_find(argv, argc, "neighbors", &idx);
	peerstr = argv[++idx]->arg;

	peer = peer_lookup_in_view(vty, bgp, peerstr, uj);
	if (!peer) {
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
				argv[idx]->arg);
		return CMD_WARNING;
	}
	if (!peer || !peer->afc[AFI_L2VPN][SAFI_EVPN]) {
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
			vty_out(vty, "%% No such neighbor or address family\n");
		return CMD_WARNING;
	}

	return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_neighbor, peer,
				     SHOW_DISPLAY_STANDARD, uj);
}

/* 【命令】show [ip] bgp l2vpn evpn rd <RD|all> neighbors <addr|WORD> routes [json]
 * 语法：显示指定 RD 下、从指定邻居学到的 EVPN 路由。
 * 行为：定位 bgp 与 peer（同前），all → prd=0，否则解析 RD；以
 *   bgp_show_type_neighbor 调 bgp_show_ethernet_vpn。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_rd_neighbor_routes,
      show_ip_bgp_l2vpn_evpn_rd_neighbor_routes_cmd,
      "show [ip] bgp l2vpn evpn rd <ASN:NN_OR_IP-ADDRESS:NN|all> neighbors <A.B.C.D|X:X::X:X|WORD> routes [json]",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information for a route distinguisher\n"
      "VPN Route Distinguisher\n"
      "All VPN Route Distinguishers\n"
      "Detailed information on TCP and BGP neighbor connections\n"
      "IPv4 Neighbor to display information about\n"
      "IPv6 Neighbor to display information about\n"
      "Neighbor on BGP configured interface\n"
      "Display routes learned from neighbor\n" JSON_STR)
{
	int idx_ext_community = 0;
	int idx = 0;
	int ret;
	struct peer *peer;
	char *peerstr = NULL;
	struct prefix_rd prd = {};
	bool uj = use_json(argc, argv);
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct bgp *bgp = NULL;
	int rd_all = 0;

	bgp_vty_find_and_parse_afi_safi_bgp(vty, argv, argc, &idx, &afi, &safi,
					    &bgp, uj);
	if (!idx) {
	        vty_out(vty, "No index\n");
		return CMD_WARNING;
	}

	if (argv_find(argv, argc, "all", &rd_all)) {
		argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN",
			  &idx_ext_community);
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
	}

	/* neighbors <A.B.C.D|X:X::X:X|WORD> */
	argv_find(argv, argc, "neighbors", &idx);
	peerstr = argv[++idx]->arg;

	peer = peer_lookup_in_view(vty, bgp, peerstr, uj);
	if (!peer) {
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
				argv[idx]->arg);
		return CMD_WARNING;
	}
	if (!peer || !peer->afc[AFI_L2VPN][SAFI_EVPN]) {
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
			vty_out(vty, "%% No such neighbor or address family\n");
		return CMD_WARNING;
	}


	if (rd_all)
		return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_neighbor,
					     peer, SHOW_DISPLAY_STANDARD, uj);
	else
		return bgp_show_ethernet_vpn(vty, &prd, bgp_show_type_neighbor,
					     peer, SHOW_DISPLAY_STANDARD, uj);
}

/* 【命令】show [ip] bgp l2vpn evpn neighbors <addr|WORD> advertised-routes [json]
 * 语法：显示**本机通告给**指定邻居的 EVPN 路由。
 * 行为：定位 bgp/peer 后调用 show_adj_route_vpn(vty, peer, NULL, AFI_L2VPN, SAFI_EVPN, uj)。
 *   uj 时 argc--（去掉 json 关键字，因为 show_adj_route_vpn 自己解析）。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_neighbor_advertised_routes,
      show_ip_bgp_l2vpn_evpn_neighbor_advertised_routes_cmd,
      "show [ip] bgp l2vpn evpn neighbors <A.B.C.D|X:X::X:X|WORD> advertised-routes [json]",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Detailed information on TCP and BGP neighbor connections\n"
      "IPv4 Neighbor to display information about\n"
      "IPv6 Neighbor to display information about\n"
      "Neighbor on BGP configured interface\n"
      "Display the routes advertised to a BGP neighbor\n" JSON_STR)
{
	int idx = 0;
	struct peer *peer;
	bool uj = use_json(argc, argv);
	struct bgp *bgp = NULL;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	char *peerstr = NULL;

	if (uj)
		argc--;

	bgp_vty_find_and_parse_afi_safi_bgp(vty, argv, argc, &idx, &afi, &safi,
					    &bgp, uj);
	if (!idx) {
	        vty_out(vty, "No index\n");
		return CMD_WARNING;
	}

	/* neighbors <A.B.C.D|X:X::X:X|WORD> */
	argv_find(argv, argc, "neighbors", &idx);
	peerstr = argv[++idx]->arg;

	peer = peer_lookup_in_view(vty, bgp, peerstr, uj);
	if (!peer) {
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
				argv[idx]->arg);
		return CMD_WARNING;
	}
	if (!peer || !peer->afc[AFI_L2VPN][SAFI_EVPN]) {
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
			vty_out(vty, "%% No such neighbor or address family\n");
		return CMD_WARNING;
	}

	return show_adj_route_vpn(vty, peer, NULL, AFI_L2VPN, SAFI_EVPN, uj);
}

/* 【命令】show [ip] bgp l2vpn evpn rd <RD|all> neighbors <addr|WORD> advertised-routes [json]
 * 语法：显示本机通告给指定邻居、且属于指定 RD（或 all）的 EVPN 路由。
 * 行为：定位 bgp/peer；all → show_adj_route_vpn(prd=NULL)，否则解析 RD 再调用。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_rd_neighbor_advertised_routes,
      show_ip_bgp_l2vpn_evpn_rd_neighbor_advertised_routes_cmd,
      "show [ip] bgp l2vpn evpn rd <ASN:NN_OR_IP-ADDRESS:NN|all> neighbors <A.B.C.D|X:X::X:X|WORD> advertised-routes [json]",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information for a route distinguisher\n"
      "VPN Route Distinguisher\n"
      "All VPN Route Distinguishers\n"
      "Detailed information on TCP and BGP neighbor connections\n"
      "IPv4 Neighbor to display information about\n"
      "IPv6 Neighbor to display information about\n"
      "Neighbor on BGP configured interface\n"
      "Display the routes advertised to a BGP neighbor\n" JSON_STR)
{
	int idx_ext_community = 0;
	int idx = 0;
	int ret;
	struct peer *peer;
	struct prefix_rd prd;
	struct bgp *bgp = NULL;
	bool uj = use_json(argc, argv);
	char *peerstr = NULL;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	int rd_all = 0;

	if (uj)
		argc--;

	if (uj)
		argc--;

	bgp_vty_find_and_parse_afi_safi_bgp(vty, argv, argc, &idx, &afi, &safi,
					    &bgp, uj);
	if (!idx) {
	        vty_out(vty, "No index\n");
		return CMD_WARNING;
	}

	/* neighbors <A.B.C.D|X:X::X:X|WORD> */
	argv_find(argv, argc, "neighbors", &idx);
	peerstr = argv[++idx]->arg;

	peer = peer_lookup_in_view(vty, bgp, peerstr, uj);
	if (!peer) {
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
				argv[idx]->arg);
		return CMD_WARNING;
	}
	if (!peer || !peer->afc[AFI_L2VPN][SAFI_EVPN]) {
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
			vty_out(vty, "%% No such neighbor or address family\n");
		return CMD_WARNING;
	}

	if (argv_find(argv, argc, "all", &rd_all))
		return show_adj_route_vpn(vty, peer, NULL, AFI_L2VPN, SAFI_EVPN,
					  uj);
	else {
		argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN",
			  &idx_ext_community);
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
	}

	return show_adj_route_vpn(vty, peer, &prd, AFI_L2VPN, SAFI_EVPN, uj);
}

/* 【命令】show [ip] bgp l2vpn evpn all overlay [json]
 * 语法：以“叠加层(overlay)信息”视角显示全部 EVPN 前缀（额外列如 L3VNI 等）。
 * 行为：bgp_show_ethernet_vpn(prd=NULL, normal, SHOW_DISPLAY_OVERLAY)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_l2vpn_evpn_all_overlay,
      show_ip_bgp_l2vpn_evpn_all_overlay_cmd,
      "show [ip] bgp l2vpn evpn all overlay [json]",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information about all EVPN NLRIs\n"
      "Display BGP Overlay Information for prefixes\n"
      JSON_STR)
{
	return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_normal, NULL,
				     SHOW_DISPLAY_OVERLAY,
				     use_json(argc, argv));
}

/* 【命令】show [ip] bgp l2vpn evpn rd <RD|all> overlay
 * 语法：以叠加层(overlay)视角显示指定 RD 下的 EVPN 前缀。
 * 行为：all → 全量；否则解析 RD；均以 SHOW_DISPLAY_OVERLAY 调 bgp_show_ethernet_vpn。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_ip_bgp_evpn_rd_overlay,
      show_ip_bgp_evpn_rd_overlay_cmd,
      "show [ip] bgp l2vpn evpn rd <ASN:NN_OR_IP-ADDRESS:NN|all> overlay",
      SHOW_STR
      IP_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display information for a route distinguisher\n"
      "VPN Route Distinguisher\n"
      "All VPN Route Distinguishers\n"
      "Display BGP Overlay Information for prefixes\n")
{
	int idx_ext_community = 0;
	int ret;
	struct prefix_rd prd;
	int rd_all = 0;

	if (argv_find(argv, argc, "all", &rd_all))
		return bgp_show_ethernet_vpn(vty, NULL, bgp_show_type_normal,
					     NULL, SHOW_DISPLAY_OVERLAY,
					     use_json(argc, argv));

	argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN", &idx_ext_community);
	ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}
	return bgp_show_ethernet_vpn(vty, &prd, bgp_show_type_normal, NULL,
				     SHOW_DISPLAY_OVERLAY,
				     use_json(argc, argv));
}

/* 【命令】show bgp l2vpn evpn rt RT [exact-match] [json]
 * 语法：显示匹配指定 route-target 的 EVPN 路由。
 * 行为：把 RT 字符串转成 ecommunity；exact-match 时用“精确匹配”类型
 *   (bgp_show_type_extcommunity_exact)，否则用包含匹配；以该 ecom 为 output_arg
 *   调 bgp_show_ethernet_vpn 过滤。失败报 "%% Malformed Route Target list"。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_bgp_l2vpn_evpn_rt,
      show_bgp_l2vpn_evpn_rt_cmd,
      "show bgp l2vpn evpn rt RT [exact-match] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display routes matching the route-target\n"
      "Route target (A.B.C.D:MN|AA:NN|AA:NNNNNN)\n"
      "Exact match of the route-target\n"
      JSON_STR)
{
	int idx = 0;
	int ret = 0;
	const char *rt_str;
	struct ecommunity *ecom;
	int show_type = bgp_show_type_extcommunity;

	argv_find(argv, argc, "RT", &idx);
	rt_str = argv[idx]->arg;

	if ((idx + 1) < argc && strmatch(argv[idx + 1]->text, "exact-match"))
		show_type = bgp_show_type_extcommunity_exact;

	ecom = ecommunity_str2com(rt_str, ECOMMUNITY_ROUTE_TARGET, 0);
	if (!ecom) {
		vty_out(vty, "%% Malformed Route Target list\n");
		return CMD_WARNING;
	}

	ret = bgp_show_ethernet_vpn(vty, NULL, show_type, ecom, SHOW_DISPLAY_STANDARD,
				    use_json(argc, argv));

	ecommunity_free(&ecom);
	return ret;
}


/* 【命令】show bgp l2vpn evpn <community AA:NN | large-community AA:BB:CC> [exact-match] [json]
 * 语法：显示匹配标准团体或大团体的 EVPN 路由。
 * 行为：大团体走 lcommunity 解析+匹配（bgp_show_type_lcommunity / _exact），
 *   标准团体走 community 解析+匹配（bgp_show_type_community / _exact）；
 *   以对应 com/lcom 为 output_arg 调 bgp_show_ethernet_vpn，随后释放。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN(show_bgp_l2vpn_evpn_com,
      show_bgp_l2vpn_evpn_com_cmd,
      "show bgp l2vpn evpn \
      <community AA:NN|large-community AA:BB:CC> \
      [exact-match] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Display routes matching the community\n"
      "Community number where AA and NN are (0-65535)\n"
      "Display routes matching the large-community\n"
      "List of large-community numbers\n"
      "Exact match of the communities\n"
      JSON_STR)
{
	int idx = 0;
	int ret = 0;
	const char *clist_number_or_name;
	int show_type = bgp_show_type_normal;
	struct community *com;
	struct lcommunity *lcom;

	if (argv_find(argv, argc, "large-community", &idx)) {
		clist_number_or_name = argv[++idx]->arg;
		show_type = bgp_show_type_lcommunity;

		if (++idx < argc && strmatch(argv[idx]->text, "exact-match"))
			show_type = bgp_show_type_lcommunity_exact;

		lcom = lcommunity_str2com(clist_number_or_name);
		if (!lcom) {
			vty_out(vty, "%% Large-community malformed\n");
			return CMD_WARNING;
		}

		ret = bgp_show_ethernet_vpn(vty, NULL, show_type, lcom,
					    SHOW_DISPLAY_STANDARD,
					    use_json(argc, argv));

		lcommunity_free(&lcom);
	} else if (argv_find(argv, argc, "community", &idx)) {
		clist_number_or_name = argv[++idx]->arg;
		show_type = bgp_show_type_community;

		if (++idx < argc && strmatch(argv[idx]->text, "exact-match"))
			show_type = bgp_show_type_community_exact;

		com = community_str2com(clist_number_or_name);

		if (!com) {
			vty_out(vty, "%% Community malformed: %s\n",
				clist_number_or_name);
			return CMD_WARNING;
		}

		ret = bgp_show_ethernet_vpn(vty, NULL, show_type, com,
					    SHOW_DISPLAY_STANDARD,
					    use_json(argc, argv));
		community_free(&com);
	}

	return ret;
}

/* 【命令】network <prefix> rd <RD> ethtag <v> label <v> esi <v> gwip <ip> routermac <mac> [route-map RM]
 * 语法：手工注入一条 EVPN type-5（IP 前缀）静态路由，主要用于**测试**。
 * 行为：解析各字段索引后调用 bgp_static_set(...BGP_EVPN_IP_PREFIX_ROUTE...)，
 *   把该 RT-5 前缀作为本地静态的 EVPN 路由（按给定 RD/ESI/gwip/routermac 组装）。
 * 改配置结构：向 EVPN 静态路由表加入一条 type-5；触发 apply/重建：是（会触发路由通告）。 */
/* For testing purpose, static route of EVPN RT-5. */
DEFUN(evpnrt5_network,
      evpnrt5_network_cmd,
      "network <A.B.C.D/M|X:X::X:X/M> rd ASN:NN_OR_IP-ADDRESS:NN ethtag WORD label WORD esi WORD gwip <A.B.C.D|X:X::X:X> routermac WORD [route-map RMAP_NAME]",
      "Specify a network to announce via BGP\n"
      "IP prefix\n"
      "IPv6 prefix\n"
      "Specify Route Distinguisher\n"
      "VPN Route Distinguisher\n"
      "Ethernet Tag\n"
      "Ethernet Tag Value\n"
      "BGP label\n"
      "label value\n"
      "Ethernet Segment Identifier\n"
      "ESI value ( 00:11:22:33:44:55:66:77:88:99 format) \n"
      "Gateway IP\n"
      "Gateway IP ( A.B.C.D )\n"
      "Gateway IPv6 ( X:X::X:X )\n"
      "Router Mac Ext Comm\n"
      "Router Mac address Value ( aa:bb:cc:dd:ee:ff format)\n"
      "Route-map to modify the attributes\n"
      "Name of the route map\n")
{
	int idx_ipv4_prefixlen = 1;
	int idx_route_distinguisher = 3;
	int idx_label = 7;
	int idx_esi = 9;
	int idx_gwip = 11;
	int idx_ethtag = 5;
	int idx_routermac = 13;

	return bgp_static_set(vty, false, argv[idx_ipv4_prefixlen]->arg,
			      argv[idx_route_distinguisher]->arg,
			      argv[idx_label]->arg, AFI_L2VPN, SAFI_EVPN, NULL,
			      0, 0, BGP_EVPN_IP_PREFIX_ROUTE,
			      argv[idx_esi]->arg, argv[idx_gwip]->arg,
			      argv[idx_ethtag]->arg, argv[idx_routermac]->arg);
}

/* 【命令】no network <prefix> rd <RD> ethtag <v> label <v> esi <v> gwip <ip>
 * 语法：删除上面 network 命令注入的 EVPN type-5 静态路由。
 * 行为：以 set=false 调 bgp_static_set(...BGP_EVPN_IP_PREFIX_ROUTE...) 删除对应条目。
 * 改配置结构：从 EVPN 静态路由表删除一条 type-5；触发 apply/重建：是（撤销通告）。 */
/* For testing purpose, static route of EVPN RT-5. */
DEFUN(no_evpnrt5_network,
      no_evpnrt5_network_cmd,
      "no network <A.B.C.D/M|X:X::X:X/M> rd ASN:NN_OR_IP-ADDRESS:NN ethtag WORD label WORD esi WORD gwip <A.B.C.D|X:X::X:X>",
      NO_STR
      "Specify a network to announce via BGP\n"
      "IP prefix\n"
      "IPv6 prefix\n"
      "Specify Route Distinguisher\n"
      "VPN Route Distinguisher\n"
      "Ethernet Tag\n"
      "Ethernet Tag Value\n"
      "BGP label\n"
      "label value\n"
      "Ethernet Segment Identifier\n"
      "ESI value ( 00:11:22:33:44:55:66:77:88:99 format) \n"
      "Gateway IP\n" "Gateway IP ( A.B.C.D )\n" "Gateway IPv6 ( X:X::X:X )\n")
{
	int idx_ipv4_prefixlen = 2;
	int idx_ext_community = 4;
	int idx_label = 8;
	int idx_ethtag = 6;
	int idx_esi = 10;
	int idx_gwip = 12;

	return bgp_static_set(vty, true, argv[idx_ipv4_prefixlen]->arg,
			      argv[idx_ext_community]->arg,
			      argv[idx_label]->arg, AFI_L2VPN, SAFI_EVPN, NULL,
			      0, 0, BGP_EVPN_IP_PREFIX_ROUTE, argv[idx_esi]->arg,
			      argv[idx_gwip]->arg, argv[idx_ethtag]->arg, NULL);
}

/* 【函数】evpn_import_rt_delete_auto —— 删除该 VNI 自动生成的 import RT
 * （调用 evpn_rt_delete_auto(bgp, vni, vpn->import_rtl, false)）。
 * 一般在用户显式配置了 import RT 后调用，以避免“自动 RT”与“手工 RT”并存。 */
static void evpn_import_rt_delete_auto(struct bgp *bgp, struct bgpevpn *vpn)
{
	evpn_rt_delete_auto(bgp, vpn->vni, vpn->import_rtl, false);
}

/* 【函数】evpn_export_rt_delete_auto —— 删除该 VNI 自动生成的 export RT
 * （调用 evpn_rt_delete_auto(bgp, vni, vpn->export_rtl, false)）。 */
static void evpn_export_rt_delete_auto(struct bgp *bgp, struct bgpevpn *vpn)
{
	evpn_rt_delete_auto(bgp, vpn->vni, vpn->export_rtl, false);
}

/* 【函数】evpn_configure_import_rt —— 为一个 VNI 增加一条 import RT（VTY 处理内核）。
 * 调用方需自行确认“这确实是一次变更”。顺序很关键：
 *   1) 若 VNI 是 live（已在 kernel），先 bgp_evpn_uninstall_routes 卸载旧 RT 命中的路由；
 *   2) bgp_evpn_unmap_vni_from_its_rts 解除 RT↔VNI 映射；
 *   3) evpn_import_rt_delete_auto 去掉自动 RT；
 *   4) 把新 RT 加入 vpn->import_rtl，置 VNI_FLAG_IMPRT_CFGD 标记，
 *      bgp_evpn_map_vni_to_its_rts 重建映射；
 *   5) 若 live，bgp_evpn_install_routes 按新 RT 重新安装路由。
 * 即：改 import RT 会**先卸后装**已有路由。 */
/*
 * Configure the Import RTs for a VNI (vty handler). Caller expected to
 * check that this is a change.
 */
static void evpn_configure_import_rt(struct bgp *bgp, struct bgpevpn *vpn,
				     struct ecommunity *ecomadd)
{
	/* If the VNI is "live", we need to uninstall routes using the current
	 * import RT(s) first before we update the import RT, and subsequently
	 * install routes.
	 */
	if (is_vni_live(vpn))
		bgp_evpn_uninstall_routes(bgp, vpn);

	/* Cleanup the RT to VNI mapping and get rid of existing import RT. */
	bgp_evpn_unmap_vni_from_its_rts(bgp, vpn);

	/* If the auto route-target is in use we must remove it */
	evpn_import_rt_delete_auto(bgp, vpn);

	/* Add new RT and rebuild the RT to VNI mapping */
	listnode_add_sort(vpn->import_rtl, ecomadd);

	SET_FLAG(vpn->flags, VNI_FLAG_IMPRT_CFGD);
	bgp_evpn_map_vni_to_its_rts(bgp, vpn);

	/* Install routes that match new import RT */
	if (is_vni_live(vpn))
		bgp_evpn_install_routes(bgp, vpn);
}

/* 【函数】evpn_unconfigure_import_rt —— 为一个 VNI 删除 import RT（VTY 处理内核）。
 * 与 configure 对称：先 uninstall 已有路由、解除 RT↔VNI 映射；
 *   ecomdel==NULL 表示删除全部 import RT；否则按 ecommunity 比较精确删除单条。
 * 删除后通常回退到“自动 RT”（由调用方随后重建），并重建映射 / 重装路由。 */
/*
 * Unconfigure Import RT(s) for a VNI (vty handler).
 */
static void evpn_unconfigure_import_rt(struct bgp *bgp, struct bgpevpn *vpn,
				       struct ecommunity *ecomdel)
{
	struct listnode *node, *nnode, *node_to_del;
	struct ecommunity *ecom;

	/* Along the lines of "configure" except we have to reset to the
	 * automatic value.
	 */
	if (is_vni_live(vpn))
		bgp_evpn_uninstall_routes(bgp, vpn);

	/* Cleanup the RT to VNI mapping and get rid of existing import RT. */
	bgp_evpn_unmap_vni_from_its_rts(bgp, vpn);

	/* Delete all import RTs */
	if (ecomdel == NULL) {
		for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode, ecom)) {
			ecommunity_free(&ecom);
			list_delete_node(vpn->import_rtl, node);
		}
	}

	/* Delete a specific import RT */
	else {
		node_to_del = NULL;

		for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode, ecom)) {
			if (ecommunity_match(ecom, ecomdel)) {
				ecommunity_free(&ecom);
				node_to_del = node;
				break;
			}
		}

		if (node_to_del)
			list_delete_node(vpn->import_rtl, node_to_del);
	}

	assert(vpn->import_rtl);
	/* Reset to auto RT - this also rebuilds the RT to VNI mapping */
	if (list_isempty(vpn->import_rtl)) {
		UNSET_FLAG(vpn->flags, VNI_FLAG_IMPRT_CFGD);
		bgp_evpn_derive_auto_rt_import(bgp, vpn);
	}
	/* Rebuild the RT to VNI mapping */
	else
		bgp_evpn_map_vni_to_its_rts(bgp, vpn);

	/* Install routes that match new import RT */
	if (is_vni_live(vpn))
		bgp_evpn_install_routes(bgp, vpn);
}

/* 【函数】evpn_configure_export_rt —— 为 VNI 配置/替换 export RT（VTY 处理内核）。
 * 注意：每个 VNI **只允许一条** export RT，故配置语义是“替换”：
 *   先去自动 RT(evpn_export_rt_delete_auto)，把新 RT 加入 vpn->export_rtl，
 *   置 VNI_FLAG_EXPRT_CFGD；若 VNI live，bgp_evpn_handle_export_rt_change
 *   触发用新 export RT 重新通告本 VNI 的路由。 */
/*
 * Configure the Export RT for a VNI (vty handler). Caller expected to
 * check that this is a change. Note that only a single export RT is
 * allowed for a VNI and any change to configuration is implemented as
 * a "replace" (similar to other configuration).
 */
static void evpn_configure_export_rt(struct bgp *bgp, struct bgpevpn *vpn,
				     struct ecommunity *ecomadd)
{
	/* If the auto route-target is in use we must remove it */
	evpn_export_rt_delete_auto(bgp, vpn);

	listnode_add_sort(vpn->export_rtl, ecomadd);
	SET_FLAG(vpn->flags, VNI_FLAG_EXPRT_CFGD);

	if (is_vni_live(vpn))
		bgp_evpn_handle_export_rt_change(bgp, vpn);
}

/* 【函数】evpn_unconfigure_export_rt —— 删除 VNI 的 export RT（VTY 处理内核）。
 *   ecomdel==NULL 删除全部；否则 ecommunity_match 精确删除一条。
 *   若删空则清 VNI_FLAG_EXPRT_CFGD 并回退到自动 export RT(bgp_evpn_derive_auto_rt_export)；
 *   若 VNI live，触发 bgp_evpn_handle_export_rt_change 重新通告。 */
/*
 * Unconfigure the Export RT for a VNI (vty handler)
 */
static void evpn_unconfigure_export_rt(struct bgp *bgp, struct bgpevpn *vpn,
				       struct ecommunity *ecomdel)
{
	struct listnode *node, *nnode, *node_to_del;
	struct ecommunity *ecom;

	/* Delete all export RTs */
	if (ecomdel == NULL) {
		/* Reset to default and process all routes. */
		for (ALL_LIST_ELEMENTS(vpn->export_rtl, node, nnode, ecom)) {
			ecommunity_free(&ecom);
			list_delete_node(vpn->export_rtl, node);
		}
	}

	/* Delete a specific export RT */
	else {
		node_to_del = NULL;

		for (ALL_LIST_ELEMENTS(vpn->export_rtl, node, nnode, ecom)) {
			if (ecommunity_match(ecom, ecomdel)) {
				ecommunity_free(&ecom);
				node_to_del = node;
				break;
			}
		}

		if (node_to_del)
			list_delete_node(vpn->export_rtl, node_to_del);
	}

	assert(vpn->export_rtl);
	if (list_isempty(vpn->export_rtl)) {
		UNSET_FLAG(vpn->flags, VNI_FLAG_EXPRT_CFGD);
		bgp_evpn_derive_auto_rt_export(bgp, vpn);
	}

	if (is_vni_live(vpn))
		bgp_evpn_handle_export_rt_change(bgp, vpn);
}

/* 【函数】evpn_configure_vrf_rd —— 为某个 **VRF（L3VNI）** 配置/替换 RD。
 *   先 bgp_evpn_handle_vrf_rd_change(bgp_vrf, 1)：撤下用旧 RD 通告的所有 type-5；
 *   更新 vrf_prd / vrf_prd_pretty，置 BGP_VRF_RD_CFGD；
 *   再 bgp_evpn_handle_vrf_rd_change(bgp_vrf, 0)：用新 RD 重新通告 type-5。 */
/*
 * Configure RD for VRF
 */
static void evpn_configure_vrf_rd(struct bgp *bgp_vrf, struct prefix_rd *rd,
				  const char *rd_pretty)
{
	/* If we have already advertise type-5 routes with a different RD, we
	 * have to delete and withdraw them first
	 */
	bgp_evpn_handle_vrf_rd_change(bgp_vrf, 1);

	if (bgp_vrf->vrf_prd_pretty)
		XFREE(MTYPE_BGP_NAME, bgp_vrf->vrf_prd_pretty);

	/* update RD */
	memcpy(&bgp_vrf->vrf_prd, rd, sizeof(struct prefix_rd));
	bgp_vrf->vrf_prd_pretty = XSTRDUP(MTYPE_BGP_NAME, rd_pretty);
	SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_RD_CFGD);

	/* We have a new RD for VRF.
	 * Advertise all type-5 routes again with the new RD
	 */
	bgp_evpn_handle_vrf_rd_change(bgp_vrf, 0);
}

/* 【函数】evpn_unconfigure_vrf_rd —— 取消 VRF 的手工 RD，回退到自动 RD。
 *   先撤下旧 RD 通告的 type-5(bgp_evpn_handle_vrf_rd_change(bgp_vrf,1))；
 *   清 BGP_VRF_RD_CFGD，bgp_evpn_derive_auto_rd_for_vrf 重算自动 RD，释放 pretty；
 *   再用新 RD 重新通告(bgp_evpn_handle_vrf_rd_change(bgp_vrf,0))。 */
/*
 * Unconfigure RD for VRF
 */
static void evpn_unconfigure_vrf_rd(struct bgp *bgp_vrf)
{
	/* If we have already advertise type-5 routes with a diffrent RD, we
	 * have to delete and withdraw them firs
	 */
	bgp_evpn_handle_vrf_rd_change(bgp_vrf, 1);

	/* fall back to default RD */
	UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_RD_CFGD);
	bgp_evpn_derive_auto_rd_for_vrf(bgp_vrf);
	if (bgp_vrf->vrf_prd_pretty)
		XFREE(MTYPE_BGP_NAME, bgp_vrf->vrf_prd_pretty);
	/* We have a new RD for VRF.
	 * Advertise all type-5 routes again with the new RD
	 */
	bgp_evpn_handle_vrf_rd_change(bgp_vrf, 0);
}

/* 【函数】evpn_configure_rd —— 为 **L2 VNI** 配置/替换 RD（VTY 处理内核）。
 *   若 VNI live，先 bgp_evpn_handle_rd_change(bgp, vpn, 1) 撤下用旧 RD 的通告；
 *   更新 vpn->prd / prd_pretty，置 VNI_FLAG_RD_CFGD；
 *   再 bgp_evpn_handle_rd_change(bgp, vpn, 0) 用新 RD 重新通告本 VNI 路由。 */
/*
 * Configure RD for a VNI (vty handler)
 */
static void evpn_configure_rd(struct bgp *bgp, struct bgpevpn *vpn,
			      struct prefix_rd *rd, const char *rd_pretty)
{
	/* If the VNI is "live", we need to delete and withdraw this VNI's
	 * local routes with the prior RD first. Then, after updating RD,
	 * need to re-advertise.
	 */
	if (is_vni_live(vpn))
		bgp_evpn_handle_rd_change(bgp, vpn, 1);

	if (vpn->prd_pretty)
		XFREE(MTYPE_BGP_NAME, vpn->prd_pretty);
	/* update RD */
	memcpy(&vpn->prd, rd, sizeof(struct prefix_rd));
	vpn->prd_pretty = XSTRDUP(MTYPE_BGP_NAME, rd_pretty);
	SET_FLAG(vpn->flags, VNI_FLAG_RD_CFGD);

	if (is_vni_live(vpn))
		bgp_evpn_handle_rd_change(bgp, vpn, 0);
}

/* 【函数】evpn_unconfigure_rd —— 取消 L2 VNI 的手工 RD，回退到自动 RD。
 *   若 live 先撤旧通告；bgp_evpn_derive_auto_rd 重算自动 RD；若 live 再重新通告。 */
/*
 * Unconfigure RD for a VNI (vty handler)
 */
static void evpn_unconfigure_rd(struct bgp *bgp, struct bgpevpn *vpn)
{
	/* If the VNI is "live", we need to delete and withdraw this VNI's
	 * local routes with the prior RD first. Then, after resetting RD
	 * to automatic value, need to re-advertise.
	 */
	if (is_vni_live(vpn))
		bgp_evpn_handle_rd_change(bgp, vpn, 1);

	/* reset RD to default */
	bgp_evpn_derive_auto_rd(bgp, vpn);

	if (is_vni_live(vpn))
		bgp_evpn_handle_rd_change(bgp, vpn, 0);
}

/* 【函数】evpn_create_update_vni —— 若 L2 VNI 不存在则创建，并标记“用户已配置”。
 *   先 bgp_evpn_lookup_vni 查：不存在时若该 VNI 号已被配置为 L3VNI 则报错返回 NULL，
 *   否则以本地 router-id 作为 originator、INADDR_ANY 组播组新建(bgp_evpn_new)。
 *   最后置 VNI_FLAG_CFGD 表示“这是用户显式配置的 VNI”（区别于仅学习到的 VNI）。 */
/*
 * Create VNI, if not already present (VTY handler). Mark as configured.
 */
static struct bgpevpn *evpn_create_update_vni(struct bgp *bgp, vni_t vni)
{
	struct bgpevpn *vpn;
	struct in_addr mcast_grp = {INADDR_ANY};
	struct ipaddr orignator_ip = {};

	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		/* Check if this L2VNI is already configured as L3VNI */
		if (bgp_evpn_lookup_l3vni_l2vni_table(vni)) {
			flog_err(
				EC_BGP_VNI,
				"%u: Failed to create L2VNI %u, it is configured as L3VNI",
				bgp->vrf_id, vni);
			return NULL;
		}

		/* tenant vrf will be updated when we get local_vni_add from
		 * zebra
		 */
		SET_IPADDR_V4(&orignator_ip);
		orignator_ip.ipaddr_v4 = bgp->router_id;
		vpn = bgp_evpn_new(bgp, vni, &orignator_ip, 0, mcast_grp, 0);
	}

	/* Mark as configured. */
	SET_FLAG(vpn->flags, VNI_FLAG_CFGD);
	return vpn;
}

/* 【函数】evpn_delete_vni —— 删除一个 L2 VNI 的“用户配置”。
 *   若 VNI 不 live（尚未在 kernel，只是配置），直接 bgp_evpn_free 释放；
 *   否则清 VNI_FLAG_CFGD，并逐一回退已配置的参数：
 *     RD(is_rd_configured)、export RT、import RT —— 各走对应的 unconfigure，
 *     从而恢复到自动值；VNI 本身继续存在（作为“学习到的”实体）。
 * 注释提醒：这里对每个参数都按需做 unconfigure，未做优化（因为该操作很少见）。 */
/*
 * Delete VNI. If VNI does not exist in the system (i.e., just
 * configuration), all that is needed is to free it. Otherwise,
 * any parameters configured for the VNI need to be reset (with
 * appropriate action) and the VNI marked as unconfigured; the
 * VNI will continue to exist, purely as a "learnt" entity.
 */
static void evpn_delete_vni(struct bgp *bgp, struct bgpevpn *vpn)
{
	if (!is_vni_live(vpn)) {
		bgp_evpn_free(bgp, vpn);
		return;
	}

	/* We need to take the unconfigure action for each parameter of this VNI
	 * that is configured. Some optimization is possible, but not worth the
	 * additional code for an operation that should be pretty rare.
	 */
	UNSET_FLAG(vpn->flags, VNI_FLAG_CFGD);

	/* First, deal with the export side - RD and export RT changes. */
	if (is_rd_configured(vpn))
		evpn_unconfigure_rd(bgp, vpn);
	if (is_export_rt_configured(vpn))
		evpn_unconfigure_export_rt(bgp, vpn, NULL);

	/* Next, deal with the import side. */
	if (is_import_rt_configured(vpn))
		evpn_unconfigure_import_rt(bgp, vpn, NULL);
}

/* 【函数】evpn_show_vrf_import_rts —— 遍历 EVPN 实例的“VRF 导入 RT 哈希表”
 * (bgp_evpn->vrf_import_rt_hash)，逐条调用 show_vrf_import_rt_entry 打印
 * “哪些 VRF 导入了哪些 RT”。args[0]=vty，args[1]=json。 */
/*
 * Display import RT mapping to VRFs (vty handler)
 * bgp_evpn: evpn bgp instance
 */
static void evpn_show_vrf_import_rts(struct vty *vty, struct bgp *bgp_evpn,
				     json_object *json)
{
	void *args[2];

	args[0] = vty;
	args[1] = json;

	hash_iterate(bgp_evpn->vrf_import_rt_hash,
		     (void (*)(struct hash_bucket *,
			       void *))show_vrf_import_rt_entry,
		     args);
}

/* 【函数】evpn_show_import_rts —— 遍历 bgp 的“VNI 导入 RT 哈希表”(bgp->import_rt_hash)，
 * 逐条调用 show_import_rt_entry 打印“哪些 VNI 导入了哪些 RT”。 */
/*
 * Display import RT mapping to VNIs (vty handler)
 */
static void evpn_show_import_rts(struct vty *vty, struct bgp *bgp,
				 json_object *json)
{
	void *args[2];

	args[0] = vty;
	args[1] = json;

	hash_iterate(
		bgp->import_rt_hash,
		(void (*)(struct hash_bucket *, void *))show_import_rt_entry,
		args);
}

/* 【函数】evpn_show_routes_vni_all —— 打印**所有 VNI** 的 EVPN 路由（可按类型/表/VTEP 过滤）。
 *   填好 vni_walk_ctx（含把 sockunion 形式的 vtep_ip 转成 struct ipaddr），
 *   再 hash_iterate(bgp->vnihash, show_vni_routes_hash)。无 VNI 时直接返回。 */
/*
 * Display EVPN routes for all VNIs - vty handler.
 */
static void evpn_show_routes_vni_all(struct vty *vty, struct bgp *bgp, int type,
				     bool mac_table, const union sockunion *vtep_ip,
				     json_object *json, int detail)
{
	uint32_t num_vnis;
	struct vni_walk_ctx wctx;

	num_vnis = hashcount(bgp->vnihash);
	if (!num_vnis)
		return;
	memset(&wctx, 0, sizeof(wctx));
	wctx.bgp = bgp;
	wctx.vty = vty;
	wctx.type = type;
	wctx.mac_table = mac_table;
	SET_IPADDR_NONE(&wctx.vtep_ip);
	if (vtep_ip) {
		if (sockunion_family(vtep_ip) == AF_INET) {
			SET_IPADDR_V4(&wctx.vtep_ip);
			wctx.vtep_ip.ipaddr_v4 = vtep_ip->sin.sin_addr;
		} else if (sockunion_family(vtep_ip) == AF_INET6) {
			SET_IPADDR_V6(&wctx.vtep_ip);
			wctx.vtep_ip.ipaddr_v6 = vtep_ip->sin6.sin6_addr;
		}
	}
	wctx.json = json;
	wctx.detail = detail;
	hash_iterate(bgp->vnihash, (void (*)(struct hash_bucket *,
					     void *))show_vni_routes_hash,
		     &wctx);
}

/* 【函数】evpn_show_routes_vni_all_type_all —— 打印所有 VNI、所有类型的 EVPN 路由
 * （IP 表与 MAC 表都打）。用 show_vni_routes_all_hash 作遍历回调。 */
/*
 * Display EVPN routes for all VNIs & all types - vty handler.
 */
static void evpn_show_routes_vni_all_type_all(struct vty *vty, struct bgp *bgp,
					      const union sockunion *vtep_ip,
					      json_object *json, int detail)
{
	uint32_t num_vnis;
	struct vni_walk_ctx wctx;

	num_vnis = hashcount(bgp->vnihash);
	if (!num_vnis)
		return;

	memset(&wctx, 0, sizeof(struct vni_walk_ctx));
	wctx.bgp = bgp;
	wctx.vty = vty;
	SET_IPADDR_NONE(&wctx.vtep_ip);
	if (vtep_ip) {
		if (sockunion_family(vtep_ip) == AF_INET) {
			SET_IPADDR_V4(&wctx.vtep_ip);
			wctx.vtep_ip.ipaddr_v4 = vtep_ip->sin.sin_addr;
		} else if (sockunion_family(vtep_ip) == AF_INET6) {
			SET_IPADDR_V6(&wctx.vtep_ip);
			wctx.vtep_ip.ipaddr_v6 = vtep_ip->sin6.sin6_addr;
		}
	}
	wctx.json = json;
	wctx.detail = detail;
	hash_iterate(bgp->vnihash,
		     (void (*)(struct hash_bucket *,
			       void *))show_vni_routes_all_hash,
		     &wctx);
}

/* 【函数】evpn_show_route_vni_multicast —— 打印指定 VNI 下**一条 type-3 路由**
 * （含组播/IMET 路由，由 originator IP 标识）。用于 show ... vni <n> route type-3
 * 的精确查询。
 *   定位 VNI(bgp_evpn_lookup_vni)，用 build_evpn_type3_prefix 构造前缀查表；
 *   命中后打印明细头 + 每条路径的明细(route_vty_out_detail)。未命中报
 *   "%% Network not in table"。用毕 unlock dest。 */
/*
 * Display EVPN routes for a VNI -- for specific type-3 route (vty handler).
 */
static void evpn_show_route_vni_multicast(struct vty *vty, struct bgp *bgp,
					  vni_t vni, struct ipaddr *orig_ip,
					  json_object *json)
{
	struct bgpevpn *vpn;
	struct prefix_evpn p;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	uint32_t path_cnt = 0;
	afi_t afi;
	safi_t safi;
	json_object *json_paths = NULL;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* Locate VNI. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		vty_out(vty, "VNI not found\n");
		return;
	}

	/* See if route exists. */
	build_evpn_type3_prefix(&p, orig_ip);
	dest = bgp_evpn_vni_node_lookup(vpn, &p, NULL);
	if (!dest || !bgp_dest_has_bgp_path_info_data(dest)) {
		if (!json)
			vty_out(vty, "%% Network not in table\n");

		if (dest)
			bgp_dest_unlock_node(dest);

		return;
	}

	if (json)
		json_paths = json_object_new_array();

	/* Prefix and num paths displayed once per prefix. */
	route_vty_out_detail_header(vty, bgp, dest, bgp_dest_get_prefix(dest),
				    NULL, afi, safi, json, false, true);

	/* Display each path for this prefix. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		json_object *json_path = NULL;

		if (json)
			json_path = json_object_new_array();

		route_vty_out_detail(vty, bgp, dest, bgp_dest_get_prefix(dest), pi, afi, safi,
				     RPKI_NOT_BEING_USED, json_path, NULL, 0);

		if (json)
			json_object_array_add(json_paths, json_path);

		path_cnt++;
	}

	if (json) {
		if (path_cnt)
			json_object_object_add(json, "paths", json_paths);

		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		vty_out(vty, "\nDisplayed %u paths for requested prefix\n",
			path_cnt);
	}

	bgp_dest_unlock_node(dest);
}

/* 【函数】evpn_show_route_vni_macip —— 打印指定 VNI 下**一条 type-2（MAC/IP）路由**。
 * 由于 MAC 是“按路径”存储的，这里需要在路径链表里按 MAC 匹配：
 *   build_evpn_type2_prefix 构造前缀查表；若同时给了 mac 和 ip，则先在该前缀的
 *   路径里找 MAC 匹配的路径，找不到报 “%% Network not in table”。
 *   打印明细前，对 VNI 本地 MAC-IP 前缀做“全局补全”(evpn_type2_prefix_global_copy)，
 *   再逐路径打印明细。用毕 unlock。 */
/*
 * Display EVPN routes for a VNI -- for specific MAC and/or IP (vty handler).
 * By definition, only matching type-2 route will be displayed.
 */
static void evpn_show_route_vni_macip(struct vty *vty, struct bgp *bgp,
				      vni_t vni, struct ethaddr *mac,
				      struct ipaddr *ip, json_object *json)
{
	struct bgpevpn *vpn;
	struct prefix_evpn p;
	struct prefix_evpn tmp_p;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	uint32_t path_cnt = 0;
	afi_t afi;
	safi_t safi;
	json_object *json_paths = NULL;
	struct ethaddr empty_mac = {};
	struct ipaddr empty_ip = {};
	const struct prefix_evpn *evp;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* Locate VNI. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		if (!json)
			vty_out(vty, "VNI not found\n");
		return;
	}

	build_evpn_type2_prefix(&p, mac ? mac : &empty_mac,
				ip ? ip : &empty_ip);

	/* See if route exists. Look for both non-sticky and sticky. */
	dest = bgp_evpn_vni_node_lookup(vpn, &p, NULL);
	if (!dest || !bgp_dest_has_bgp_path_info_data(dest)) {
		if (!json)
			vty_out(vty, "%% Network not in table\n");

		if (dest)
			bgp_dest_unlock_node(dest);

		return;
	}

	/*
	 * MAC is per-path, we have to walk the path_info's and look for it
	 * first here.
	 */
	if (ip && mac) {
		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
			if (memcmp(mac, evpn_type2_path_info_get_mac(pi),
				   sizeof(*mac)) == 0)
				break;
		}

		if (!pi) {
			if (!json)
				vty_out(vty, "%% Network not in table\n");
			return;
		}
	}

	if (json)
		json_paths = json_object_new_array();

	/* Prefix and num paths displayed once per prefix. */
	route_vty_out_detail_header(vty, bgp, dest, (struct prefix *)&p, NULL,
				    afi, safi, json, false, true);

	evp = (const struct prefix_evpn *)bgp_dest_get_prefix(dest);

	/* Display each path for this prefix. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		json_object *json_path = NULL;

		/* skip non-matching MACs */
		if (ip && mac &&
		    memcmp(mac, evpn_type2_path_info_get_mac(pi),
			   sizeof(*mac)) != 0)
			continue;

		if (json)
			json_path = json_object_new_array();

		/*
		 * VNI table MAC-IP prefixes don't have MAC so
		 * make sure it's set from path info
		 * here.
		 */
		if (is_evpn_prefix_ipaddr_none(evp)) {
			/* VNI MAC -> Global */
			evpn_type2_prefix_global_copy(&tmp_p, evp,
						      NULL /* mac */,
						      evpn_type2_path_info_get_ip(
							      pi));
		} else {
			/* VNI IP -> Global */
			evpn_type2_prefix_global_copy(&tmp_p, evp,
						      evpn_type2_path_info_get_mac(
							      pi),
						      NULL /* ip */);
		}

		route_vty_out_detail(vty, bgp, dest, (struct prefix *)&tmp_p, pi, afi, safi,
				     RPKI_NOT_BEING_USED, json_path, NULL, 0);

		if (json)
			json_object_array_add(json_paths, json_path);

		path_cnt++;
	}

	if (json) {
		if (path_cnt)
			json_object_object_add(json, "paths", json_paths);

		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		vty_out(vty, "\nDisplayed %u paths for requested prefix\n",
			path_cnt);
	}

	bgp_dest_unlock_node(dest);
}

/* 【函数】evpn_show_routes_esi —— 按 ESI 查找 ES(bgp_evpn_es_find)，找到则调用
 * show_esi_routes 打印该 ES 的路由表；找不到报 "ESI not found"。 */
/* Disaplay EVPN routes for a ESI - VTY handler */
static void evpn_show_routes_esi(struct vty *vty, struct bgp *bgp,
				 esi_t *esi, json_object *json)
{
	struct bgp_evpn_es *es = NULL;

	/* locate the ES */
	es = bgp_evpn_es_find(esi);
	if (!es) {
		if (!json)
			vty_out(vty, "ESI not found\n");
		return;
	}

	show_esi_routes(bgp, es, vty, json);
}

/* 【函数】evpn_show_routes_vni —— 按 VNI 打印路由（show ... vni <n> route 的处理）。
 *   定位 VNI；把可选的 sockunion 形式 VTEP 过滤条件转成 struct ipaddr；
 *   最终调用 show_vni_routes(bgp, vpn, vty, type, mac_table, &vtep_ip, json, 0)。
 * type 非 0 只显示该类型；vtep_ip 非空只显示该 VTEP 后面的路由。 */
/*
 * Display EVPN routes for a VNI - vty handler.
 * If 'type' is non-zero, only routes matching that type are shown.
 * If the vtep_ip is non zero, only routes behind that vtep are shown
 */
static void evpn_show_routes_vni(struct vty *vty, struct bgp *bgp, vni_t vni,
				 int type, bool mac_table,
				 const union sockunion *_vtep_ip, json_object *json)
{
	struct bgpevpn *vpn;
	struct ipaddr vtep_ip;

	/* Locate VNI. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		if (!json)
			vty_out(vty, "VNI not found\n");
		return;
	}

	if (_vtep_ip && sockunion_family(_vtep_ip) == AF_INET) {
		SET_IPADDR_V4(&vtep_ip);
		vtep_ip.ipaddr_v4 = _vtep_ip->sin.sin_addr;
	} else if (_vtep_ip && sockunion_family(_vtep_ip) == AF_INET6) {
		SET_IPADDR_V6(&vtep_ip);
		vtep_ip.ipaddr_v6 = _vtep_ip->sin6.sin6_addr;
	} else {
		SET_IPADDR_NONE(&vtep_ip);
	}

	/* Walk this VNI's route table and display appropriate routes. */
	show_vni_routes(bgp, vpn, vty, type, mac_table, &vtep_ip, json, 0);
}

/* 【函数】evpn_show_route_rd_macip —— 在**指定 RD 的全局 EVPN 表**里查一条 type-2
 * （MAC/IP）路由并打印明细。与 evpn_show_route_vni_macip 的区别：这里查的是
 * bgp->rib[AFI_L2VPN][SAFI_EVPN] 下按 RD 分组的“全局表”，不是 VNI 本地表。
 *   用 build_evpn_type2_prefix 构造前缀 + bgp_safi_node_lookup(prd) 查表；
 *   命中打印明细头 + 各路径明细；未命中报 “%% Network not in table”；用毕 unlock。 */
/*
 * Display BGP EVPN routing table -- for specific RD and MAC and/or
 * IP (vty handler). By definition, only matching type-2 route will be
 * displayed.
 */
static void evpn_show_route_rd_macip(struct vty *vty, struct bgp *bgp,
				     struct prefix_rd *prd, struct ethaddr *mac,
				     struct ipaddr *ip, json_object *json)
{
	struct prefix_evpn p;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	afi_t afi;
	safi_t safi;
	uint32_t path_cnt = 0;
	json_object *json_paths = NULL;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* See if route exists. Look for both non-sticky and sticky. */
	build_evpn_type2_prefix(&p, mac, ip);
	dest = bgp_safi_node_lookup(bgp->rib[afi][safi], safi,
				    (struct prefix *)&p, prd);
	if (!dest || !bgp_dest_has_bgp_path_info_data(dest)) {
		if (!json)
			vty_out(vty, "%% Network not in table\n");

		if (dest)
			bgp_dest_unlock_node(dest);

		return;
	}

	/* Prefix and num paths displayed once per prefix. */
	route_vty_out_detail_header(vty, bgp, dest, bgp_dest_get_prefix(dest),
				    prd, afi, safi, json, false, false);

	if (json)
		json_paths = json_object_new_array();

	/* Display each path for this prefix. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		json_object *json_path = NULL;

		if (json)
			json_path = json_object_new_array();

		route_vty_out_detail(vty, bgp, dest, bgp_dest_get_prefix(dest), pi, afi, safi,
				     RPKI_NOT_BEING_USED, json_path, NULL, 0);

		if (json)
			json_object_array_add(json_paths, json_path);

		path_cnt++;
	}

	if (json) {
		if (path_cnt)
			json_object_object_add(json, "paths", json_paths);
		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		vty_out(vty, "\nDisplayed %u paths for requested prefix\n",
			path_cnt);
	}

	bgp_dest_unlock_node(dest);
}

/* 【函数】evpn_show_route_rd —— 打印**指定 RD** 下的全部 EVPN 路由（可限定 type）。
 *   bgp_node_lookup 找到该 RD 的二层表(RD→前缀)；遍历其下每个前缀与路径，
 *   打印明细头 + 各路径明细(route_vty_out_detail)；图例仅文本模式打印一次。
 *   末尾输出该 RD 下的前缀数/路径数；无则提示 “No prefixes exist with this RD”。
 * JSON 以 RD 为键组织。 */
/*
 * Display BGP EVPN routing table -- for specific RD (vty handler)
 * If 'type' is non-zero, only routes matching that type are shown.
 */
static void evpn_show_route_rd(struct vty *vty, struct bgp *bgp,
			       struct prefix_rd *prd, int type,
			       json_object *json)
{
	struct bgp_dest *rd_dest;
	struct bgp_table *table;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	int rd_header = 1;
	afi_t afi;
	safi_t safi;
	uint32_t prefix_cnt, path_cnt;
	json_object *json_rd = NULL;
	int add_rd_to_json = 0;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;
	prefix_cnt = path_cnt = 0;

	rd_dest = bgp_node_lookup(bgp->rib[afi][safi], (struct prefix *)prd);
	if (!rd_dest)
		return;

	table = bgp_dest_get_bgp_table_info(rd_dest);
	if (table == NULL) {
		bgp_dest_unlock_node(rd_dest);
		return;
	}

	if (json) {
		json_rd = json_object_new_object();
		json_object_string_addf(json_rd, "rd",
					BGP_RD_AS_FORMAT(bgp->asnotation), prd);
	}

	bgp_dest_unlock_node(rd_dest);

	/* Display all prefixes with this RD. */
	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		const struct prefix_evpn *evp =
			(const struct prefix_evpn *)bgp_dest_get_prefix(dest);
		json_object *json_prefix = NULL;
		json_object *json_paths = NULL;
		int add_prefix_to_json = 0;

		if (type && evp->prefix.route_type != type)
			continue;

		if (json)
			json_prefix = json_object_new_object();

		pi = bgp_dest_get_bgp_path_info(dest);
		if (pi) {
			/* RD header and legend - once overall. */
			if (rd_header && !json) {
				vty_out(vty,
					"EVPN type-1 prefix: [1]:[EthTag]:[ESI]:[IPlen]:[VTEP-IP]:[Frag-id]\n");
				vty_out(vty,
					"EVPN type-2 prefix: [2]:[EthTag]:[MAClen]:[MAC]\n");
				vty_out(vty,
					"EVPN type-3 prefix: [3]:[EthTag]:[IPlen]:[OrigIP]\n");
				vty_out(vty,
					"EVPN type-4 prefix: [4]:[ESI]:[IPlen]:[OrigIP]\n");
				vty_out(vty,
					"EVPN type-5 prefix: [5]:[EthTag]:[IPlen]:[IP]\n\n");
				rd_header = 0;
			}

			/* Prefix and num paths displayed once per prefix. */
			route_vty_out_detail_header(vty, bgp, dest,
						    bgp_dest_get_prefix(dest),
						    prd, afi, safi, json_prefix,
						    false, false);

			prefix_cnt++;
		}

		if (json)
			json_paths = json_object_new_array();

		/* Display each path for this prefix. */
		for (; pi; pi = pi->next) {
			json_object *json_path = NULL;

			if (json)
				json_path = json_object_new_array();

			route_vty_out_detail(vty, bgp, dest, bgp_dest_get_prefix(dest), pi, afi,
					     safi, RPKI_NOT_BEING_USED, json_path, NULL, 0);

			if (json)
				json_object_array_add(json_paths, json_path);

			path_cnt++;
			add_prefix_to_json = 1;
			add_rd_to_json = 1;
		}

		if (json) {
			if (add_prefix_to_json) {
				json_object_object_add(json_prefix, "paths",
						       json_paths);
				json_object_object_addf(json_rd, json_prefix,
							"%pFX", evp);
			} else {
				json_object_free(json_paths);
				json_object_free(json_prefix);
				json_paths = NULL;
				json_prefix = NULL;
			}
		}
	}

	if (json) {
		if (add_rd_to_json)
			json_object_object_addf(
				json, json_rd,
				BGP_RD_AS_FORMAT(bgp->asnotation), prd);
		else {
			json_object_free(json_rd);
			json_rd = NULL;
		}

		json_object_int_add(json, "numPrefix", prefix_cnt);
		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		if (prefix_cnt == 0)
			vty_out(vty, "No prefixes exist with this RD%s\n",
				type ? " (of requested type)" : "");
		else
			vty_out(vty,
				"\nDisplayed %u prefixes (%u paths) with this RD%s\n",
				prefix_cnt, path_cnt,
				type ? " (of requested type)" : "");
	}
}

/* 【函数】evpn_show_route_rd_all_macip —— 在**所有 RD** 下查找 MAC/IP(type-2)
 * 匹配的路由。EVPN 表是“RD → 前缀”的两层结构，故此处外层遍历所有 RD，
 *   内层用 build_evpn_type2_prefix(mac, ip) + bgp_safi_node_lookup(rd) 逐 RD 查表；
 *   命中则打印该 RD 头 + 前缀明细。用于不带 rd 限定的 `show ... mac/ip`。 */
/*
 * Display BGP EVPN routing table -- all RDs and MAC and/or IP
 * (vty handler).  Only matching type-2 routes will be displayed.
 */
static void evpn_show_route_rd_all_macip(struct vty *vty, struct bgp *bgp,
					 struct ethaddr *mac, struct ipaddr *ip,
					 json_object *json)
{
	struct bgp_dest *rd_dest;
	struct bgp_table *table;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	uint32_t prefix_cnt, path_cnt;
	prefix_cnt = path_cnt = 0;

	/* EVPN routing table is a 2-level table with the first level being
	 * the RD. We need to look in every RD we know about.
	 */
	for (rd_dest = bgp_table_top(bgp->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		json_object *json_paths = NULL;  /* paths array for prefix */
		json_object *json_prefix = NULL; /* prefix within an RD */
		json_object *json_rd = NULL;     /* holds all prefixes for RD */
		char rd_str[RD_ADDRSTRLEN];
		int add_rd_to_json = 0;
		struct prefix_evpn ep;
		const struct prefix *rd_destp = bgp_dest_get_prefix(rd_dest);

		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (table == NULL)
			continue;

		prefix_rd2str((struct prefix_rd *)rd_destp, rd_str,
			      sizeof(rd_str), bgp->asnotation);

		/* Construct an RT-2 from the user-supplied mac(ip),
		 * then search the l2vpn evpn table for it.
		 */
		build_evpn_type2_prefix(&ep, mac, ip);
		dest = bgp_safi_node_lookup(bgp->rib[afi][safi], safi,
					    (struct prefix *)&ep,
					    (struct prefix_rd *)rd_destp);
		if (!dest)
			continue;

		if (json)
			json_rd = json_object_new_object();

		const struct prefix *p = bgp_dest_get_prefix(dest);

		pi = bgp_dest_get_bgp_path_info(dest);
		if (pi) {
			/* RD header - per RD. */
			bgp_evpn_show_route_rd_header(vty, rd_dest, json_rd,
						      rd_str, RD_ADDRSTRLEN);
			prefix_cnt++;
		}

		if (json) {
			json_prefix = json_object_new_object();
			json_paths = json_object_new_array();
			json_object_string_addf(json_prefix, "prefix", "%pFX",
						p);
			json_object_int_add(json_prefix, "prefixLen",
					    p->prefixlen);
		} else
			/* Prefix and num paths displayed once per prefix. */
			route_vty_out_detail_header(vty, bgp, dest, p,
						    (struct prefix_rd *)rd_destp,
						    AFI_L2VPN, SAFI_EVPN,
						    json_prefix, false, false);

		/* For EVPN, the prefix is displayed for each path (to
		 * fit in with code that already exists).
		 */
		for (; pi; pi = pi->next) {
			json_object *json_path = NULL;

			add_rd_to_json = 1;
			path_cnt++;

			if (json)
				json_path = json_object_new_array();

			route_vty_out_detail(vty, bgp, dest, p, pi, AFI_L2VPN, SAFI_EVPN,
					     RPKI_NOT_BEING_USED, json_path, NULL, 0);

			if (json)
				json_object_array_add(json_paths, json_path);
			else
				vty_out(vty, "\n");
		}

		if (json) {
			json_object_object_add(json_prefix, "paths",
					       json_paths);
			json_object_object_addf(json_rd, json_prefix, "%pFX",
						p);
			if (add_rd_to_json)
				json_object_object_add(json, rd_str, json_rd);
			else {
				json_object_free(json_rd);
				json_rd = NULL;
			}
		}

		bgp_dest_unlock_node(dest);
	}

	if (json) {
		json_object_int_add(json, "numPrefix", prefix_cnt);
		json_object_int_add(json, "numPaths", path_cnt);
	} else {
		if (prefix_cnt == 0) {
			vty_out(vty, "No Matching EVPN prefixes exist\n");
		} else {
			vty_out(vty, "Displayed %u prefixes (%u paths)\n",
				prefix_cnt, path_cnt);
		}
	}
}

/* 【函数】evpn_show_all_routes —— 打印**整个 EVPN 路由表**（所有 RD）的核心实现。
 * bgp_evpn_vty.h 里导出的 bgp_evpn_show_all_routes 最终转到此处。
 *   外层遍历 RD，内层遍历前缀与路径；type 非 0 只显示该类型；
 *   self_orig=true 时只保留本机自己产生(peer == bgp->peer_self)的路径。
 *   detail 时走 route_vty_out_detail（含属性明细），否则 route_vty_out（精简行）。
 *   JSON 模式用 vty_json_no_pretty 逐 RD 拼接（首条不加逗号）。 */
/*
 * Display BGP EVPN routing table - all routes (vty handler).
 * If 'type' is non-zero, only routes matching that type are shown.
 */
static void evpn_show_all_routes(struct vty *vty, struct bgp *bgp, int type,
				 json_object *json, int detail, bool self_orig)
{
	struct bgp_dest *rd_dest;
	struct bgp_table *table;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	int header = detail ? 0 : 1;
	int rd_header;
	afi_t afi;
	safi_t safi;
	uint32_t prefix_cnt, path_cnt;
	int first = true;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;
	prefix_cnt = path_cnt = 0;

	/* EVPN routing table is a 2-level table with the first level being
	 * the RD.
	 */
	for (rd_dest = bgp_table_top(bgp->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		char rd_str[RD_ADDRSTRLEN];
		json_object *json_rd = NULL; /* contains routes for an RD */
		int add_rd_to_json = 0;
		uint64_t tbl_ver;
		const struct prefix *rd_destp = bgp_dest_get_prefix(rd_dest);

		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (table == NULL)
			continue;

		tbl_ver = table->version;
		prefix_rd2str((struct prefix_rd *)rd_destp, rd_str,
			      sizeof(rd_str), bgp->asnotation);

		if (json) {
			if (first) {
				vty_out(vty, "\"%s\":", rd_str);
				first = false;
			} else {
				vty_out(vty, ",\"%s\":", rd_str);
			}
			json_rd = json_object_new_object();
		}

		rd_header = 1;

		/* Display all prefixes for an RD */
		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			json_object *json_prefix =
				NULL; /* contains prefix under a RD */
			json_object *json_paths =
				NULL; /* array of paths under a prefix*/
			const struct prefix_evpn *evp =
				(const struct prefix_evpn *)bgp_dest_get_prefix(
					dest);
			int add_prefix_to_json = 0;
			const struct prefix *p = bgp_dest_get_prefix(dest);

			if (type && evp->prefix.route_type != type)
				continue;

			pi = bgp_dest_get_bgp_path_info(dest);
			if (pi) {
				if (self_orig && (pi->peer != bgp->peer_self))
					continue;

				/* Overall header/legend displayed once. */
				if (header) {
					bgp_evpn_show_route_header(vty, bgp,
								   tbl_ver,
								   json);
					if (!json)
						vty_out(vty,
							"%19s Extended Community\n"
							, " ");
					header = 0;
				}

				/* RD header - per RD. */
				if (rd_header) {
					bgp_evpn_show_route_rd_header(
						vty, rd_dest, json_rd, rd_str,
						RD_ADDRSTRLEN);
					rd_header = 0;
				}

				prefix_cnt++;
			}

			if (json) {
				json_prefix = json_object_new_object();
				json_paths = json_object_new_array();
				json_object_string_addf(json_prefix, "prefix",
							"%pFX", p);
				json_object_int_add(json_prefix, "prefixLen",
						    p->prefixlen);
			}

			/* Prefix and num paths displayed once per prefix. */
			if (detail)
				route_vty_out_detail_header(vty, bgp, dest,
							    bgp_dest_get_prefix(
								    dest),
							    (struct prefix_rd *)
								    rd_destp,
							    AFI_L2VPN, SAFI_EVPN,
							    json_prefix, false,
							    false);

			/* For EVPN, the prefix is displayed for each path (to
			 * fit in
			 * with code that already exists).
			 */
			for (; pi; pi = pi->next) {
				json_object *json_path = NULL;

				path_cnt++;
				add_prefix_to_json = 1;
				add_rd_to_json = 1;

				if (json)
					json_path = json_object_new_array();

				if (detail) {
					route_vty_out_detail(vty, bgp, dest,
							     bgp_dest_get_prefix(dest), pi,
							     AFI_L2VPN, SAFI_EVPN,
							     RPKI_NOT_BEING_USED, json_path, NULL,
							     0);
				} else
					route_vty_out(vty, p, pi, 0, NULL, SAFI_EVPN, json_path,
						      false, rd_str);

				if (json)
					json_object_array_add(json_paths,
							      json_path);
			}

			if (json) {
				if (add_prefix_to_json) {
					json_object_object_add(json_prefix,
							       "paths",
							       json_paths);
					json_object_object_addf(json_rd,
								json_prefix,
								"%pFX", p);
				} else {
					json_object_free(json_prefix);
					json_object_free(json_paths);
					json_prefix = NULL;
					json_paths = NULL;
				}
			}
		}

		if (json) {
			if (add_rd_to_json) {
				vty_json_no_pretty(vty, json_rd);
			} else {
				vty_out(vty, "{}");
				json_object_free(json_rd);
			}
		}
	}

	if (json) {
		/* at least one prefix was printed */
		if (!first)
			vty_out(vty, ",");
		vty_out(vty, "\"numPrefix\":%u", prefix_cnt);
		vty_out(vty, ",\"numPaths\":%u", path_cnt);
	} else {
		if (prefix_cnt == 0) {
			vty_out(vty, "No EVPN prefixes %sexist\n",
				type ? "(of requested type) " : "");
		} else {
			vty_out(vty, "\nDisplayed %u prefixes (%u paths)%s\n",
				prefix_cnt, path_cnt,
				type ? " (of requested type)" : "");
		}
	}
}

/* 【函数】bgp_evpn_show_all_routes —— 对外导出的“显示全部 EVPN 路由”入口
 * （声明在 bgp_evpn_vty.h）；JSON 时自行包裹 “{ ... }”，内部交给
 * evpn_show_all_routes(vty, bgp, type, json, detail, false)。 */
int bgp_evpn_show_all_routes(struct vty *vty, struct bgp *bgp, int type,
			     bool use_json, int detail)
{
	json_object *json = NULL;

	if (use_json) {
		json = json_object_new_object();
		vty_out(vty, "{\n");
	}

	evpn_show_all_routes(vty, bgp, type, json, detail, false);

	if (use_json) {
		vty_out(vty, "}\n");
		json_object_free(json);
	}

	return CMD_SUCCESS;
}

/* 【函数】evpn_show_vni —— 按 VNI 号显示**单个 VNI 详情**（不分 L2/L3）：
 *   先按 L2 查(bgp_evpn_lookup_vni)，命中则 display_vni；
 *   否则遍历所有 bgp 实例找 l3vni == vni 的，命中则 display_l3vni。
 *   都没找到且非 JSON 时报 "VNI not found"。 */
/*
 * Display specified VNI (vty handler)
 */
static void evpn_show_vni(struct vty *vty, struct bgp *bgp, vni_t vni,
			  json_object *json)
{
	uint8_t found = 0;
	struct bgpevpn *vpn;

	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (vpn) {
		found = 1;
		display_vni(vty, vpn, json);
	} else {
		struct bgp *bgp_temp;
		struct listnode *node = NULL;

		for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_temp)) {
			if (bgp_temp->l3vni == vni) {
				found = 1;
				display_l3vni(vty, bgp_temp, json);
			}
		}
	}

	if (!found && !json) {
		vty_out(vty, "VNI not found\n");
		return;
	}
}

/* 【函数】evpn_show_all_vnis —— 显示**所有 VNI 的一行摘要表**（L2 + L3）。
 *   文本模式先打印表头（Flags / VNI / Type / RD / Import RT / Export RT /
 *   MAC-VRF Site-of-Origin / Tenant VRF）；然后 hash_iterate(bgp->vnihash,
 *   show_vni_entry) 打 L2，再遍历所有 bgp 实例用 show_l3vni_entry 打 L3。 */
/*
 * Display a VNI (upon user query).
 */
static void evpn_show_all_vnis(struct vty *vty, struct bgp *bgp,
			       json_object *json)
{
	void *args[2];
	struct bgp *bgp_temp = NULL;
	struct listnode *node;


	if (!json) {
		vty_out(vty, "Flags: * - Kernel\n");
		vty_out(vty, "  %-10s %-4s %-21s %-25s %-25s %-25s %-37s\n",
			"VNI", "Type", "RD", "Import RT", "Export RT",
			"MAC-VRF Site-of-Origin", "Tenant VRF");
	}

	/* print all L2 VNIS */
	args[0] = vty;
	args[1] = json;
	hash_iterate(bgp->vnihash,
		     (void (*)(struct hash_bucket *, void *))show_vni_entry,
		     args);

	/* print all L3 VNIs */
	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_temp))
		show_l3vni_entry(vty, bgp_temp, json);
}

/* 【函数】evpn_set_advertise_svi_macip —— 开启/关闭“通告 SVI 的 MAC-IP”。
 *   vpn==NULL 时设全局开关 bgp->evpn_info->advertise_svi_macip（id 传 0）；
 *   否则设该 VNI 的 vpn->advertise_svi_macip（带 vpn->vni）。
 *   值未变化则直接返回（避免重复下发）。改完调 bgp_zebra_advertise_svi_macip
 *   通知 zebra。 */
/*
 * evpn - enable advertisement of svi MAC-IP
 */
static void evpn_set_advertise_svi_macip(struct bgp *bgp, struct bgpevpn *vpn,
					 uint32_t set)
{
	if (!vpn) {
		if (set && bgp->evpn_info->advertise_svi_macip)
			return;
		else if (!set && !bgp->evpn_info->advertise_svi_macip)
			return;

		bgp->evpn_info->advertise_svi_macip = set;
		bgp_zebra_advertise_svi_macip(bgp,
					bgp->evpn_info->advertise_svi_macip, 0);
	} else {
		if (set && vpn->advertise_svi_macip)
			return;
		else if (!set && !vpn->advertise_svi_macip)
			return;

		vpn->advertise_svi_macip = set;
		bgp_zebra_advertise_svi_macip(bgp, vpn->advertise_svi_macip,
					      vpn->vni);
	}
}

/* 【函数】evpn_set_advertise_default_gw —— 开启“通告默认网关的 MAC-IP”。
 *   vpn==NULL 设全局 bgp->advertise_gw_macip（id 0）；否则设 vpn->advertise_gw_macip
 *   （带 vni）。已开启则返回。改完调 bgp_zebra_advertise_gw_macip 通知 zebra。 */
/*
 * evpn - enable advertisement of default g/w
 */
static void evpn_set_advertise_default_gw(struct bgp *bgp, struct bgpevpn *vpn)
{
	if (!vpn) {
		if (bgp->advertise_gw_macip)
			return;

		bgp->advertise_gw_macip = 1;
		bgp_zebra_advertise_gw_macip(bgp, bgp->advertise_gw_macip, 0);
	} else {
		if (vpn->advertise_gw_macip)
			return;

		vpn->advertise_gw_macip = 1;
		bgp_zebra_advertise_gw_macip(bgp, vpn->advertise_gw_macip,
					     vpn->vni);
	}
	return;
}

/* 【函数】evpn_unset_advertise_default_gw —— 关闭“通告默认网关的 MAC-IP”。
 *   与 set 对称：未开启则返回；清对应标志（全局或按 VNI）后调
 *   bgp_zebra_advertise_gw_macip 通知 zebra。 */
/*
 * evpn - disable advertisement of default g/w
 */
static void evpn_unset_advertise_default_gw(struct bgp *bgp,
					    struct bgpevpn *vpn)
{
	if (!vpn) {
		if (!bgp->advertise_gw_macip)
			return;

		bgp->advertise_gw_macip = 0;
		bgp_zebra_advertise_gw_macip(bgp, bgp->advertise_gw_macip, 0);
	} else {
		if (!vpn->advertise_gw_macip)
			return;

		vpn->advertise_gw_macip = 0;
		bgp_zebra_advertise_gw_macip(bgp, vpn->advertise_gw_macip,
					     vpn->vni);
	}
	return;
}

/* 【函数】evpn_process_default_originate_cmd —— 处理 VRF 上的 default-originate
 * （按 afi 即 ipv4/ipv6 区分）。add=true 时置对应
 * BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4 / _IPV6 标志；add=false 时清除。
 * 若已处于目标状态则提前返回。最后若 L3VNI live，调
 * bgp_evpn_install_uninstall_default_route 实际安装/撤销默认路由。 */
/*
 * evpn - enable advertisement of default g/w
 */
static void evpn_process_default_originate_cmd(struct bgp *bgp_vrf,
					       afi_t afi, bool add)
{
	safi_t safi = SAFI_UNICAST; /* ipv4/ipv6 unicast */

	if (add) {
		/* bail if we are already advertising default route */
		if (evpn_default_originate_set(bgp_vrf, afi, safi))
			return;

		if (afi == AFI_IP)
			SET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN],
				 BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4);
		else if (afi == AFI_IP6)
			SET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN],
				 BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV6);
	} else {
		/* bail out if we haven't advertised the default route */
		if (!evpn_default_originate_set(bgp_vrf, afi, safi))
			return;
		if (afi == AFI_IP)
			UNSET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN],
				   BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4);
		else if (afi == AFI_IP6)
			UNSET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN],
				   BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV6);
	}

	if (is_l3vni_live(bgp_vrf))
		bgp_evpn_install_uninstall_default_route(bgp_vrf, afi, safi, NULL, add);
}

/* 【函数】evpn_set_advertise_subnet —— 开启该 VNI 的 subnet 通告
 * （vpn->advertise_subnet=1），并调 bgp_zebra_advertise_subnet 通知 zebra
 * （id 带 vpn->vni）。已开启则返回。 */
/*
 * evpn - enable advertisement of default g/w
 */
static void evpn_set_advertise_subnet(struct bgp *bgp,
				      struct bgpevpn *vpn)
{
	if (vpn->advertise_subnet)
		return;

	vpn->advertise_subnet = 1;
	bgp_zebra_advertise_subnet(bgp, vpn->advertise_subnet, vpn->vni);
}

/* 【函数】evpn_unset_advertise_subnet —— 关闭该 VNI 的 subnet 通告
 * （vpn->advertise_subnet=0），并调 bgp_zebra_advertise_subnet 通知 zebra。
 * 未开启则返回。 */
/*
 * evpn - disable advertisement of default g/w
 */
static void evpn_unset_advertise_subnet(struct bgp *bgp, struct bgpevpn *vpn)
{
	if (!vpn->advertise_subnet)
		return;

	vpn->advertise_subnet = 0;
	bgp_zebra_advertise_subnet(bgp, vpn->advertise_subnet, vpn->vni);
}

/* 【函数】evpn_set_advertise_all_vni —— 开启“通告所有本地 VNI”（EVPN 总开关）。
 *   置 bgp->advertise_all_vni=1，bgp_set_evpn 登记本实例为 EVPN 实例，
 *   再 bgp_zebra_advertise_all_vni 通知 zebra 开始上报本地 VNI。 */
/*
 * EVPN (VNI advertisement) enabled. Register with zebra.
 */
static void evpn_set_advertise_all_vni(struct bgp *bgp)
{
	bgp->advertise_all_vni = 1;
	bgp_set_evpn(bgp);
	bgp_zebra_advertise_all_vni(bgp, bgp->advertise_all_vni);
}

/* 【函数】evpn_unset_advertise_all_vni —— 关闭“通告所有本地 VNI”。
 *   置 bgp->advertise_all_vni=0，把 EVPN 实例切回 default(bgp_set_evpn(bgp_get_default()))，
 *   通知 zebra 停报，并 bgp_evpn_cleanup_on_disable 清理 VNI 缓存与 EVPN 路由
 *   （删除并向对端撤销）。 */
/*
 * EVPN (VNI advertisement) disabled. De-register with zebra. Cleanup VNI
 * cache, EVPN routes (delete and withdraw from peers).
 */
static void evpn_unset_advertise_all_vni(struct bgp *bgp)
{
	bgp->advertise_all_vni = 0;
	bgp_set_evpn(bgp_get_default());
	bgp_zebra_advertise_all_vni(bgp, bgp->advertise_all_vni);
	bgp_evpn_cleanup_on_disable(bgp);
}

/* 【函数】bgp_evpn_set_unset_resolve_overlay_index —— 设置/清除
 * bgp->resolve_overlay_index（是否解析叠加层索引以确定下一跳）。
 *   值未变化则返回；set 时置位并对每个 VNI 调
 *   bgp_evpn_handle_resolve_overlay_index_set，否则先对每个 VNI 调 _unset 再清位。 */
/* Set resolve overlay index flag */
static void bgp_evpn_set_unset_resolve_overlay_index(struct bgp *bgp, bool set)
{
	if (set == bgp->resolve_overlay_index)
		return;

	if (set) {
		bgp->resolve_overlay_index = true;
		hash_iterate(bgp->vnihash,
			     (void (*)(struct hash_bucket *, void *))
				     bgp_evpn_handle_resolve_overlay_index_set,
			     NULL);
	} else {
		hash_iterate(
			bgp->vnihash,
			(void (*)(struct hash_bucket *, void *))
				bgp_evpn_handle_resolve_overlay_index_unset,
			NULL);
		bgp->resolve_overlay_index = false;
	}
}

/* 【函数】evpn_set_advertise_autort_rfc8365 —— 置 bgp->advertise_autort_rfc8365=1
 * （按 RFC8365 自动派生 RT），随后 bgp_evpn_handle_autort_change 让所有 VNI
 * 按新规则重算自动 RT。 */
/*
 * EVPN - use RFC8365 to auto-derive RT
 */
static void evpn_set_advertise_autort_rfc8365(struct bgp *bgp)
{
	bgp->advertise_autort_rfc8365 = 1;
	bgp_evpn_handle_autort_change(bgp);
}

/* 【函数】evpn_unset_advertise_autort_rfc8365 —— 清 bgp->advertise_autort_rfc8365=0
 * （不按 RFC8365 派生 RT），随后 bgp_evpn_handle_autort_change 让所有 VNI 重算自动 RT。 */
/*
 * EVPN - don't use RFC8365 to auto-derive RT
 */
static void evpn_unset_advertise_autort_rfc8365(struct bgp *bgp)
{
	bgp->advertise_autort_rfc8365 = 0;
	bgp_evpn_handle_autort_change(bgp);
}

/* 【函数】write_vni_config —— 把一个 L2 VNI 的用户配置写入 running-config。
 *   仅当 is_vni_configured(vpn) 时输出 "vni <n>" 块及其子项：rd、flooding 控制、
 *   route-target import / export、advertise-default-gw / advertise-svi-ip /
 *   advertise-subnet，最后 "exit-vni"。是 bgp_config_write_evpn_info 的组成部分。 */
static void write_vni_config(struct vty *vty, struct bgpevpn *vpn)
{
	char *ecom_str;
	struct listnode *node, *nnode;
	struct ecommunity *ecom;

	if (is_vni_configured(vpn)) {
		vty_out(vty, "  vni %u\n", vpn->vni);
		if (is_rd_configured(vpn))
			vty_out(vty, "   rd %s\n", vpn->prd_pretty);

		if (!vpn->bgp_vrf ||
		    (vpn->bgp_vrf && (vpn->vxlan_flood_ctrl != vpn->bgp_vrf->vxlan_flood_ctrl))) {
			if (vpn->vxlan_flood_ctrl == VXLAN_FLOOD_DISABLED)
				vty_out(vty, "   flooding disable\n");
			else if (vpn->vxlan_flood_ctrl == VXLAN_FLOOD_HEAD_END_REPL)
				vty_out(vty, "   flooding head-end-replication\n");
		}

		if (is_import_rt_configured(vpn)) {
			for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode,
					       ecom)) {
				ecom_str = ecommunity_ecom2str(
					ecom, ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
				vty_out(vty, "   route-target import %s\n",
					ecom_str);
				XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);
			}
		}

		if (is_export_rt_configured(vpn)) {
			for (ALL_LIST_ELEMENTS(vpn->export_rtl, node, nnode,
					       ecom)) {
				ecom_str = ecommunity_ecom2str(
					ecom, ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
				vty_out(vty, "   route-target export %s\n",
					ecom_str);
				XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);
			}
		}

		if (vpn->advertise_gw_macip)
			vty_out(vty, "   advertise-default-gw\n");

		if (vpn->advertise_svi_macip)
			vty_out(vty, "   advertise-svi-ip\n");

		if (vpn->advertise_subnet)
			vty_out(vty, "   advertise-subnet\n");

		vty_out(vty, "  exit-vni\n");
	}
}

#include "bgpd/bgp_evpn_vty_clippy.c"

/* 【命令】[no] flooding <disable | head-end-replication>
 * 语法：设置 EVPN 的 BUM（广播/未知单播/组播）泛洪方式。disable = 不泛洪；
 *   head-end-replication = 头端复制；no = 恢复为头端复制。
 * 行为：设 bgp->vxlan_flood_ctrl 为 VXLAN_FLOOD_DISABLED / VXLAN_FLOOD_HEAD_END_REPL，
 *   值未变则直接成功；变更后 bgp_evpn_flood_control_change(bgp) 触发相应重建。
 * 改配置结构：bgp->vxlan_flood_ctrl；触发 apply/重建：是。
 * 注：文件上方 include 了 bgp_evpn_vty_clippy.c（clippy 生成物），供 DEFPY 使用。 */
DEFPY(bgp_evpn_flood_control,
      bgp_evpn_flood_control_cmd,
      "[no$no] flooding <disable$disable|head-end-replication$her>",
      NO_STR
      "Specify handling for BUM packets\n"
      "Do not flood any BUM packets\n"
      "Flood BUM packets using head-end replication\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	enum vxlan_flood_control flood_ctrl;

	if (!bgp)
		return CMD_WARNING;

	if (disable && !no)
		flood_ctrl = VXLAN_FLOOD_DISABLED;
	else if (her || no)
		flood_ctrl = VXLAN_FLOOD_HEAD_END_REPL;
	else
		return CMD_WARNING;

	if (bgp->vxlan_flood_ctrl == flood_ctrl)
		return CMD_SUCCESS;

	bgp->vxlan_flood_ctrl = flood_ctrl;
	bgp_evpn_flood_control_change(bgp);

	return CMD_SUCCESS;
}

/* 【命令】advertise-default-gw（VNI 子模式）
 * 语法：在 `vni <n>` 上下文里开启“通告默认网关 MAC-IP”。
 * 行为：VTY_DECLVAR_CONTEXT_SUB 取当前 VNI(vpn)，调 evpn_set_advertise_default_gw(bgp, vpn)。
 * 改配置结构：vpn->advertise_gw_macip；触发 apply/重建：通知 zebra。 */
DEFUN (bgp_evpn_advertise_default_gw_vni,
       bgp_evpn_advertise_default_gw_vni_cmd,
       "advertise-default-gw",
       "Advertise default g/w mac-ip routes in EVPN for a VNI\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);

	if (!bgp)
		return CMD_WARNING;

	evpn_set_advertise_default_gw(bgp, vpn);

	return CMD_SUCCESS;
}

/* 【命令】no advertise-default-gw（VNI 子模式）
 * 语法：在 `vni <n>` 上下文里关闭“通告默认网关 MAC-IP”。
 * 行为：调 evpn_unset_advertise_default_gw(bgp, vpn)。
 * 改配置结构：vpn->advertise_gw_macip；触发 apply/重建：通知 zebra。 */
DEFUN (no_bgp_evpn_advertise_default_vni_gw,
       no_bgp_evpn_advertise_default_gw_vni_cmd,
       "no advertise-default-gw",
       NO_STR
       "Withdraw default g/w mac-ip routes from EVPN for a VNI\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);

	if (!bgp)
		return CMD_WARNING;

	evpn_unset_advertise_default_gw(bgp, vpn);

	return CMD_SUCCESS;
}


/* 【命令】advertise-default-gw（EVPN 全局模式）
 * 语法：全局开启“通告所有默认网关 MAC-IP”。
 * 行为：须在 EVPN VRF 下(EVPN_ENABLED(bgp))，否则提示
 *   "This command is only supported under the EVPN VRF"；调
 *   evpn_set_advertise_default_gw(bgp, NULL)。
 * 改配置结构：bgp->advertise_gw_macip；触发 apply/重建：通知 zebra。 */
DEFUN (bgp_evpn_advertise_default_gw,
       bgp_evpn_advertise_default_gw_cmd,
       "advertise-default-gw",
       "Advertise All default g/w mac-ip routes in EVPN\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under the EVPN VRF\n");
		return CMD_WARNING;
	}

	evpn_set_advertise_default_gw(bgp, NULL);

	return CMD_SUCCESS;
}

/* 【命令】no advertise-default-gw（EVPN 全局模式）
 * 语法：全局关闭“通告所有默认网关 MAC-IP”。
 * 行为：调 evpn_unset_advertise_default_gw(bgp, NULL)。
 * 改配置结构：bgp->advertise_gw_macip；触发 apply/重建：通知 zebra。 */
DEFUN (no_bgp_evpn_advertise_default_gw,
       no_bgp_evpn_advertise_default_gw_cmd,
       "no advertise-default-gw",
       NO_STR
       "Withdraw All default g/w mac-ip routes from EVPN\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;

	evpn_unset_advertise_default_gw(bgp, NULL);

	return CMD_SUCCESS;
}

/* 【命令】advertise-all-vni
 * 语法：开启 EVPN 并通告所有本地 VNI（EVPN 功能总开关）。
 * 行为：若已有别的 EVPN 实例且不是本实例，则提示
 *   "%% Please unconfigure EVPN in <name>"；否则调 evpn_set_advertise_all_vni(bgp)。
 * 改配置结构：bgp->advertise_all_vni，并把本实例登记为 EVPN 实例；
 *   触发 apply/重建：是（向 zebra 注册 VNI 上报）。 */
DEFUN (bgp_evpn_advertise_all_vni,
       bgp_evpn_advertise_all_vni_cmd,
       "advertise-all-vni",
       "Advertise All local VNIs\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct bgp *bgp_evpn = NULL;

	if (!bgp)
		return CMD_WARNING;

	bgp_evpn = bgp_get_evpn();
	if (bgp_evpn && bgp_evpn != bgp) {
		vty_out(vty, "%% Please unconfigure EVPN in %s\n",
			bgp_evpn->name_pretty);
		return CMD_WARNING_CONFIG_FAILED;
	}

	evpn_set_advertise_all_vni(bgp);
	return CMD_SUCCESS;
}

/* 【命令】no advertise-all-vni
 * 语法：关闭 EVPN（停止通告所有本地 VNI）。
 * 行为：调 evpn_unset_advertise_all_vni(bgp)：撤销 EVPN 实例登记、通知 zebra、
 *   清理 VNI 缓存并删除/撤销 EVPN 路由。
 * 改配置结构：bgp->advertise_all_vni；触发 apply/重建：是（大量清理）。 */
DEFUN (no_bgp_evpn_advertise_all_vni,
       no_bgp_evpn_advertise_all_vni_cmd,
       "no advertise-all-vni",
       NO_STR
       "Advertise All local VNIs\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;
	evpn_unset_advertise_all_vni(bgp);
	return CMD_SUCCESS;
}

/* 【命令】autort rfc8365-compatible
 * 语法：按 RFC8365 方式自动派生 RT。
 * 行为：调 evpn_set_advertise_autort_rfc8365(bgp)。
 * 改配置结构：bgp->advertise_autort_rfc8365；触发 apply/重建：是（所有 VNI 重算自动 RT）。 */
DEFUN (bgp_evpn_advertise_autort_rfc8365,
       bgp_evpn_advertise_autort_rfc8365_cmd,
       "autort rfc8365-compatible",
       "Auto-derivation of RT\n"
       "Auto-derivation of RT using RFC8365\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;
	evpn_set_advertise_autort_rfc8365(bgp);
	return CMD_SUCCESS;
}

/* 【命令】no autort rfc8365-compatible
 * 语法：取消 RFC8365 方式的 RT 自动派生（回到默认派生）。
 * 行为：调 evpn_unset_advertise_autort_rfc8365(bgp)。
 * 改配置结构：bgp->advertise_autort_rfc8365；触发 apply/重建：是（所有 VNI 重算自动 RT）。 */
DEFUN (no_bgp_evpn_advertise_autort_rfc8365,
       no_bgp_evpn_advertise_autort_rfc8365_cmd,
       "no autort rfc8365-compatible",
       NO_STR
       "Auto-derivation of RT\n"
       "Auto-derivation of RT using RFC8365\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;
	evpn_unset_advertise_autort_rfc8365(bgp);
	return CMD_SUCCESS;
}

/* 【命令】default-originate <ipv4 | ipv6>
 * 语法：在 VRF（L3VNI）上通告一条默认路由。
 * 行为：解析 afi 后调 evpn_process_default_originate_cmd(bgp_vrf, afi, true)。
 * 改配置结构：BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4 / _IPV6 标志；
 *   触发 apply/重建：是（L3VNI live 时安装默认路由）。 */
DEFUN (bgp_evpn_default_originate,
       bgp_evpn_default_originate_cmd,
       "default-originate <ipv4 | ipv6>",
       "originate a default route\n"
       "ipv4 address family\n"
       "ipv6 address family\n")
{
	afi_t afi = 0;
	int idx_afi = 0;
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);

	if (!bgp_vrf)
		return CMD_WARNING;
	argv_find_and_parse_afi(argv, argc, &idx_afi, &afi);
	evpn_process_default_originate_cmd(bgp_vrf, afi, true);
	return CMD_SUCCESS;
}

/* 【命令】no default-originate <ipv4 | ipv6>
 * 语法：撤销 VRF 上的默认路由通告。
 * 行为：解析 afi 后调 evpn_process_default_originate_cmd(bgp_vrf, afi, false)。
 * 改配置结构：清 BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4 / _IPV6 标志；
 *   触发 apply/重建：是（L3VNI live 时撤销默认路由）。 */
DEFUN (no_bgp_evpn_default_originate,
       no_bgp_evpn_default_originate_cmd,
       "no default-originate <ipv4 | ipv6>",
       NO_STR
       "withdraw a default route\n"
       "ipv4 address family\n"
       "ipv6 address family\n")
{
	afi_t afi = 0;
	int idx_afi = 0;
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);

	if (!bgp_vrf)
		return CMD_WARNING;
	argv_find_and_parse_afi(argv, argc, &idx_afi, &afi);
	evpn_process_default_originate_cmd(bgp_vrf, afi, false);
	return CMD_SUCCESS;
}

/* 【命令】dup-addr-detection [max-moves (2-1000) time (2-1800)]
 * 语法：开启 MAC/IP 重复地址检测(DAD)，可选最大移动次数与时间窗口。
 * 行为：须在 EVPN VRF 下；置 evpn_info->dup_addr_detect=true，并按需设
 *   dad_time / dad_max_moves（缺省则保持默认）；最后 bgp_zebra_dup_addr_detection
 *   通知 zebra 生效。
 * 改配置结构：bgp_vrf->evpn_info 的 DAD 参数；触发 apply/重建：通知 zebra。 */
DEFPY (dup_addr_detection,
       dup_addr_detection_cmd,
       "dup-addr-detection [max-moves (2-1000)$max_moves_val time (2-1800)$time_val]",
       "Duplicate address detection\n"
       "Max allowed moves before address detected as duplicate\n"
       "Num of max allowed moves (2-1000) default 5\n"
       "Duplicate address detection time\n"
       "Time in seconds (2-1800) default 180\n")
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);

	if (!bgp_vrf)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp_vrf)) {
		vty_out(vty,
			"This command is only supported under the EVPN VRF\n");
		return CMD_WARNING;
	}

	bgp_vrf->evpn_info->dup_addr_detect = true;

	if (time_val)
		bgp_vrf->evpn_info->dad_time = time_val;
	if (max_moves_val)
		bgp_vrf->evpn_info->dad_max_moves = max_moves_val;

	bgp_zebra_dup_addr_detection(bgp_vrf);

	return CMD_SUCCESS;
}

/* 【命令】dup-addr-detection freeze <permanent | 30-3600>
 * 语法：配置 DAD 命中后的“冻结”方式：永久冻结，或冻结指定秒数后自动恢复。
 * 行为：置 dup_addr_detect=true、dad_freeze=true、dad_freeze_time=freeze_time；
 *   bgp_zebra_dup_addr_detection 通知 zebra。
 * 改配置结构：bgp_vrf->evpn_info 的 DAD freeze 参数；触发 apply/重建：通知 zebra。 */
DEFPY (dup_addr_detection_auto_recovery,
       dup_addr_detection_auto_recovery_cmd,
       "dup-addr-detection freeze <permanent |(30-3600)$freeze_time_val>",
       "Duplicate address detection\n"
       "Duplicate address detection freeze\n"
       "Duplicate address detection permanent freeze\n"
       "Duplicate address detection freeze time (30-3600)\n")
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);
	uint32_t freeze_time = freeze_time_val;

	if (!bgp_vrf)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp_vrf)) {
		vty_out(vty,
			"This command is only supported under the EVPN VRF\n");
		return CMD_WARNING;
	}

	bgp_vrf->evpn_info->dup_addr_detect = true;
	bgp_vrf->evpn_info->dad_freeze = true;
	bgp_vrf->evpn_info->dad_freeze_time = freeze_time;

	bgp_zebra_dup_addr_detection(bgp_vrf);

	return CMD_SUCCESS;
}

/* 【命令】no dup-addr-detection [max-moves ... time ... | freeze ...]
 * 语法：关闭 DAD，或按给定值与当前配置**匹配校验后**回退单项到默认。
 * 行为：argc==2 时（无参数）整体关闭并全部复位默认；
 *   否则对给出的 max-moves / time / freeze 项：若与当前配置不一致则报
 *   "%% Value does not match with config"（返回 SUCCESS 但不改），一致则复位默认。
 * 改配置结构：bgp_vrf->evpn_info 的 DAD 参数；触发 apply/重建：通知 zebra。 */
DEFPY (no_dup_addr_detection,
       no_dup_addr_detection_cmd,
       "no dup-addr-detection [max-moves (2-1000)$max_moves_val time (2-1800)$time_val | freeze <permanent$permanent_val | (30-3600)$freeze_time_val>]",
       NO_STR
       "Duplicate address detection\n"
       "Max allowed moves before address detected as duplicate\n"
       "Num of max allowed moves (2-1000) default 5\n"
       "Duplicate address detection time\n"
       "Time in seconds (2-1800) default 180\n"
       "Duplicate address detection freeze\n"
       "Duplicate address detection permanent freeze\n"
       "Duplicate address detection freeze time (30-3600)\n")
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);
	uint32_t max_moves = (uint32_t)max_moves_val;
	uint32_t freeze_time = (uint32_t)freeze_time_val;

	if (!bgp_vrf)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp_vrf)) {
		vty_out(vty,
			"This command is only supported under the EVPN VRF\n");
		return CMD_WARNING;
	}

	if (argc == 2) {
		if (!bgp_vrf->evpn_info->dup_addr_detect)
			return CMD_SUCCESS;
		/* Reset all parameters to default. */
		bgp_vrf->evpn_info->dup_addr_detect = false;
		bgp_vrf->evpn_info->dad_time = EVPN_DAD_DEFAULT_TIME;
		bgp_vrf->evpn_info->dad_max_moves = EVPN_DAD_DEFAULT_MAX_MOVES;
		bgp_vrf->evpn_info->dad_freeze = false;
		bgp_vrf->evpn_info->dad_freeze_time = 0;
	} else {
		if (max_moves) {
			if (bgp_vrf->evpn_info->dad_max_moves != max_moves) {
				vty_out(vty,
				"%% Value does not match with config\n");
				return CMD_SUCCESS;
			}
			bgp_vrf->evpn_info->dad_max_moves =
				EVPN_DAD_DEFAULT_MAX_MOVES;
		}

		if (time_val) {
			if (bgp_vrf->evpn_info->dad_time != time_val) {
				vty_out(vty,
				"%% Value does not match with config\n");
				return CMD_SUCCESS;
			}
			bgp_vrf->evpn_info->dad_time = EVPN_DAD_DEFAULT_TIME;
		}

		if (freeze_time) {
			if (bgp_vrf->evpn_info->dad_freeze_time
			    != freeze_time) {
				vty_out(vty,
				"%% Value does not match with config\n");
				return CMD_SUCCESS;
			}
			bgp_vrf->evpn_info->dad_freeze_time = 0;
			bgp_vrf->evpn_info->dad_freeze = false;
		}

		if (permanent_val) {
			if (bgp_vrf->evpn_info->dad_freeze_time) {
				vty_out(vty,
				"%% Value does not match with config\n");
				return CMD_SUCCESS;
			}
			bgp_vrf->evpn_info->dad_freeze = false;
		}
	}

	bgp_zebra_dup_addr_detection(bgp_vrf);

	return CMD_SUCCESS;
}

/* 【命令】[no] advertise-svi-ip（EVPN 全局模式）
 * 语法：全局开启/关闭“通告 SVI 的 MAC-IP”。
 * 行为：no 时 evpn_set_advertise_svi_macip(bgp, NULL, 0)；
 *   否则须在 EVPN VRF 下，调 evpn_set_advertise_svi_macip(bgp, NULL, 1)。
 * 改配置结构：bgp->evpn_info->advertise_svi_macip；触发 apply/重建：通知 zebra。 */
DEFPY(bgp_evpn_advertise_svi_ip,
      bgp_evpn_advertise_svi_ip_cmd,
      "[no$no] advertise-svi-ip",
      NO_STR
      "Advertise svi mac-ip routes in EVPN\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;

	if (no)
		evpn_set_advertise_svi_macip(bgp, NULL, 0);
	else {
		if (!EVPN_ENABLED(bgp)) {
			vty_out(vty,
				"This command is only supported under EVPN VRF\n");
			return CMD_WARNING;
		}
		evpn_set_advertise_svi_macip(bgp, NULL, 1);
	}

	return CMD_SUCCESS;
}

/* 【命令】[no] advertise-svi-ip（VNI 子模式）
 * 语法：在 `vni <n>` 上下文里开启/关闭“通告 SVI 的 MAC-IP”。
 * 行为：VTY_DECLVAR_CONTEXT_SUB 取 vpn，调 evpn_set_advertise_svi_macip(bgp, vpn, no?0:1)。
 * 改配置结构：vpn->advertise_svi_macip；触发 apply/重建：通知 zebra。 */
DEFPY(bgp_evpn_advertise_svi_ip_vni,
      bgp_evpn_advertise_svi_ip_vni_cmd,
      "[no$no] advertise-svi-ip",
      NO_STR
      "Advertise svi mac-ip routes in EVPN for a VNI\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);

	if (!bgp)
		return CMD_WARNING;

	if (no)
		evpn_set_advertise_svi_macip(bgp, vpn, 0);
	else
		evpn_set_advertise_svi_macip(bgp, vpn, 1);

	return CMD_SUCCESS;
}

/* 【命令】mac-vrf soo <ASN:NN | IP:NN>
 * 语法：配置全局 MAC-VRF Site-of-Origin(SoO) 扩展团体（用于防环）。
 * 行为：须在 EVPN underlay 实例下配置（否则提示到 EVPN underlay 去配）；
 *   把 SoO 字符串转成 ecommunity(ECOMMUNITY_SITE_ORIGIN)，失败报
 *   "%% Malformed SoO extended community"；再 bgp_evpn_handle_global_macvrf_soo_change
 *   应用（会用到各 VNI 上）。
 * 改配置结构：EVPN 全局 SoO(ecommunity)；触发 apply/重建：是。 */
DEFPY(macvrf_soo_global, macvrf_soo_global_cmd,
      "mac-vrf soo ASN:NN_OR_IP-ADDRESS:NN$soo",
      "EVPN MAC-VRF\n"
      "Site-of-Origin extended community\n"
      "VPN extended community\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct bgp *bgp_evpn = bgp_get_evpn();
	struct ecommunity *ecomm_soo;

	if (!bgp || !bgp_evpn || !bgp_evpn->evpn_info)
		return CMD_WARNING;

	if (bgp != bgp_evpn) {
		vty_out(vty,
			"%% Please configure MAC-VRF SoO in the EVPN underlay: %s\n",
			bgp_evpn->name_pretty);
		return CMD_WARNING_CONFIG_FAILED;
	}

	ecomm_soo = ecommunity_str2com(soo, ECOMMUNITY_SITE_ORIGIN, 0);
	if (!ecomm_soo) {
		vty_out(vty, "%% Malformed SoO extended community\n");
		return CMD_WARNING_CONFIG_FAILED;
	}
	ecommunity_str(ecomm_soo);

	bgp_evpn_handle_global_macvrf_soo_change(bgp_evpn, ecomm_soo);

	return CMD_SUCCESS;
}

/* 【命令】no mac-vrf soo [<SoO>]
 * 语法：删除全局 MAC-VRF Site-of-Origin。可选带原值（本实现不校验原值）。
 * 行为：调 bgp_evpn_handle_global_macvrf_soo_change(bgp_evpn, NULL) 清空并应用。
 * 改配置结构：EVPN 全局 SoO；触发 apply/重建：是。 */
DEFPY(no_macvrf_soo_global, no_macvrf_soo_global_cmd,
      "no mac-vrf soo [ASN:NN_OR_IP-ADDRESS:NN$soo]",
      NO_STR
      "EVPN MAC-VRF\n"
      "Site-of-Origin extended community\n"
      "VPN extended community\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct bgp *bgp_evpn = bgp_get_evpn();

	if (!bgp || !bgp_evpn || !bgp_evpn->evpn_info)
		return CMD_WARNING;

	if (bgp_evpn)
		bgp_evpn_handle_global_macvrf_soo_change(bgp_evpn,
							 NULL /* new_soo */);

	return CMD_SUCCESS;
}

/* 【命令（隐藏）】advertise-subnet（VNI 子模式）
 * 语法：通告该 VNI 对应的子网（隐藏命令）。
 * 行为：VTY_DECLVAR_CONTEXT_SUB 取 vpn，查其租户 VRF(bgp_lookup_by_vrf_id)，
 *   调 evpn_set_advertise_subnet(bgp, vpn)。
 * 改配置结构：vpn->advertise_subnet；触发 apply/重建：通知 zebra。 */
DEFUN_HIDDEN (bgp_evpn_advertise_vni_subnet,
	      bgp_evpn_advertise_vni_subnet_cmd,
	      "advertise-subnet",
	      "Advertise the subnet corresponding to VNI\n")
{
	struct bgp *bgp_vrf = NULL;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);

	if (!bgp)
		return CMD_WARNING;

	bgp_vrf = bgp_lookup_by_vrf_id(vpn->tenant_vrf_id);
	if (!bgp_vrf)
		return CMD_WARNING;

	evpn_set_advertise_subnet(bgp, vpn);
	return CMD_SUCCESS;
}

/* 【命令（隐藏）】no advertise-subnet（VNI 子模式）
 * 语法：取消该 VNI 的子网通告（隐藏命令）。
 * 行为：调 evpn_unset_advertise_subnet(bgp, vpn)。
 * 改配置结构：vpn->advertise_subnet；触发 apply/重建：通知 zebra。 */
DEFUN_HIDDEN (no_bgp_evpn_advertise_vni_subnet,
	      no_bgp_evpn_advertise_vni_subnet_cmd,
	      "no advertise-subnet",
	      NO_STR
	      "Advertise All local VNIs\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);

	if (!bgp)
		return CMD_WARNING;

	evpn_unset_advertise_subnet(bgp, vpn);
	return CMD_SUCCESS;
}

/* 【命令】advertise <ipv4|ipv6> unicast [gateway-ip] [route-map RMAP_NAME]
 * 语法：在 VRF（L3VNI）上把 IPv4/IPv6 单播前缀作为 EVPN **type-5** 通告；
 *   gateway-ip 表示叠加层索引用网关 IP；route-map 可过滤/改写属性。
 * 行为（较复杂）：
 *   - 解析 afi/safi/overlay-index/route-map，只支持 unicast；
 *   - 计算两个“通告方向”标志：flag_oi_none(无叠加索引) 与 flag_oi_gw_ip(网关IP)；
 *     据此判断本次命令是否真正改变了通告方式(adv_flag_changed)或 route-map(rmap_changed)，
 *     若都未变则返回 CMD_WARNING；
 *   - 若需变更：先 bgp_evpn_withdraw_type5_routes 撤销旧 type-5（必须在改标志前，
 *     以配合 addpath 的撤销逻辑），必要时释放旧 route-map 引用；
 *   - 按情况 SET/UNSET af_flags[AFI_L2VPN][SAFI_EVPN] 对应的 ADV_* 标志；
 *   - adv_flag_changed 时 bgp_addpath_type_changed 重算 addpath id；
 *   - 更新 adv_cmd_rmap[afi][safi]（name+map 引用计数）；
 *   - 最后按 bestpath 或 multipath 情况调 bgp_evpn_advertise_type5_routes 通告。
 * 改配置结构：af_flags 的 ADV 标志、adv_cmd_rmap；触发 apply/重建：是（撤销+重通告 type-5）。 */
DEFUN (bgp_evpn_advertise_type5,
       bgp_evpn_advertise_type5_cmd,
       "advertise " BGP_AFI_CMD_STR "" BGP_SAFI_CMD_STR " [gateway-ip] [route-map RMAP_NAME]",
       "Advertise prefix routes\n"
       BGP_AFI_HELP_STR
       BGP_SAFI_HELP_STR
       "advertise gateway IP overlay index\n"
       "route-map for filtering specific routes\n"
       "Name of the route map\n")
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp); /* bgp vrf instance */
	int idx_afi = 0;
	int idx_safi = 0;
	int idx_rmap = 0;
	afi_t afi = 0;
	safi_t safi = 0;
	int ret = 0;
	int rmap_changed = 0;
	enum overlay_index_type oly = OVERLAY_INDEX_TYPE_NONE;
	int idx_oly = 0;
	bool adv_flag_changed = false;
	uint16_t flag_oi_none, flag_oi_gw_ip;
	bool has_flag_oi_none, has_flag_oi_gw_ip;

	if (!bgp_vrf)
		return CMD_WARNING;

	argv_find_and_parse_afi(argv, argc, &idx_afi, &afi);
	argv_find_and_parse_safi(argv, argc, &idx_safi, &safi);
	argv_find_and_parse_oly_idx(argv, argc, &idx_oly, &oly);

	ret = argv_find(argv, argc, "route-map", &idx_rmap);
	if (ret) {
		if (!bgp_vrf->adv_cmd_rmap[afi][safi].name)
			rmap_changed = 1;
		else if (strcmp(argv[idx_rmap + 1]->arg,
				bgp_vrf->adv_cmd_rmap[afi][safi].name)
			 != 0)
			rmap_changed = 1;
	} else if (bgp_vrf->adv_cmd_rmap[afi][safi].name) {
		rmap_changed = 1;
	}

	if (!(afi == AFI_IP || afi == AFI_IP6)) {
		vty_out(vty,
			"%% Only ipv4 or ipv6 address families are supported\n");
		return CMD_WARNING;
	}

	if (safi != SAFI_UNICAST) {
		vty_out(vty,
			"%% Only ipv4 unicast or ipv6 unicast are supported\n");
		return CMD_WARNING;
	}

	if ((oly != OVERLAY_INDEX_TYPE_NONE)
	    && (oly != OVERLAY_INDEX_GATEWAY_IP)) {
		vty_out(vty, "%% Unknown overlay-index type specified\n");
		return CMD_WARNING;
	}

	if (afi == AFI_IP) {
		flag_oi_none = BGP_L2VPN_EVPN_ADV_IPV4_UNICAST;
		flag_oi_gw_ip = BGP_L2VPN_EVPN_ADV_IPV4_UNICAST_GW_IP;
	} else {
		flag_oi_none = BGP_L2VPN_EVPN_ADV_IPV6_UNICAST;
		flag_oi_gw_ip = BGP_L2VPN_EVPN_ADV_IPV6_UNICAST_GW_IP;
	}
	has_flag_oi_none = CHECK_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_none);
	has_flag_oi_gw_ip = CHECK_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_gw_ip);

	if (!has_flag_oi_none && !has_flag_oi_gw_ip) {
		if (oly == OVERLAY_INDEX_GATEWAY_IP)
			adv_flag_changed = true;
	} else if (!has_flag_oi_none && oly == OVERLAY_INDEX_TYPE_NONE)
		adv_flag_changed = true;
	else if (!has_flag_oi_gw_ip && oly == OVERLAY_INDEX_GATEWAY_IP)
		adv_flag_changed = true;
	else if (!rmap_changed)
		/* Command is issued with the same option (no overlay index or gateway-ip) which
		 * was already configured. So nothing to do. However, has not been modified.
		 * Return an error
		 */
		return CMD_WARNING;

	if (rmap_changed || adv_flag_changed) {
		/* If either of these are changed, then FRR needs to withdraw already advertised
		 * type5 routes.
		 * This needs to be done before actually changing the flags, for proper addpath
		 * handling in the withdraw logic.
		 */
		bgp_evpn_withdraw_type5_routes(bgp_vrf, afi, safi);
		if (rmap_changed)
			if (bgp_vrf->adv_cmd_rmap[afi][safi].name) {
				XFREE(MTYPE_ROUTE_MAP_NAME, bgp_vrf->adv_cmd_rmap[afi][safi].name);
				route_map_counter_decrement(bgp_vrf->adv_cmd_rmap[afi][safi].map);
				bgp_vrf->adv_cmd_rmap[afi][safi].name = NULL;
				bgp_vrf->adv_cmd_rmap[afi][safi].map = NULL;
			}
	}

	if (!has_flag_oi_none && !has_flag_oi_gw_ip) {
		/* this is the case for first time ever configuration adv ipv4 unicast is enabled
		 * for the first time.
		 * So no need to reset any flag
		 */
		if (oly == OVERLAY_INDEX_TYPE_NONE)
			SET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_none);
		else if (oly == OVERLAY_INDEX_GATEWAY_IP)
			SET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_gw_ip);
	} else if (oly == OVERLAY_INDEX_TYPE_NONE && !has_flag_oi_none) {
		/* This is modify case from gateway-ip to no overlay index */
		UNSET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_gw_ip);
		SET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_none);
	} else if (oly == OVERLAY_INDEX_GATEWAY_IP && !has_flag_oi_gw_ip) {
		/* This is modify case from no overlay index to gateway-ip */
		UNSET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_none);
		SET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_gw_ip);
	}

	if (adv_flag_changed)
		/* Generate/cleanup addpath ids */
		bgp_addpath_type_changed(bgp_vrf);

	/* set the route-map for advertise command */
	if (ret && argv[idx_rmap + 1]->arg) {
		/* Only allocate/update if the route-map name is different */
		if (!bgp_vrf->adv_cmd_rmap[afi][safi].name ||
		    strcmp(bgp_vrf->adv_cmd_rmap[afi][safi].name, argv[idx_rmap + 1]->arg) != 0) {
			if (bgp_vrf->adv_cmd_rmap[afi][safi].name) {
				XFREE(MTYPE_ROUTE_MAP_NAME, bgp_vrf->adv_cmd_rmap[afi][safi].name);
				route_map_counter_decrement(bgp_vrf->adv_cmd_rmap[afi][safi].map);
			}
			bgp_vrf->adv_cmd_rmap[afi][safi].name = XSTRDUP(MTYPE_ROUTE_MAP_NAME,
									argv[idx_rmap + 1]->arg);
			bgp_vrf->adv_cmd_rmap[afi][safi].map =
				route_map_lookup_by_name(argv[idx_rmap + 1]->arg);
			route_map_counter_increment(bgp_vrf->adv_cmd_rmap[afi][safi].map);
		}
	}

	/* advertise type-5 routes */
	if (advertise_type5_routes_bestpath(bgp_vrf, afi) ||
	    advertise_type5_routes_multipath(bgp_vrf, afi))
		bgp_evpn_advertise_type5_routes(bgp_vrf, afi, safi);
	return CMD_SUCCESS;
}

/* 【命令】no advertise <ipv4|ipv6> unicast [route-map WORD]
 * 语法：停止把 IPv4/IPv6 单播前缀作为 EVPN type-5 通告。
 * 行为：解析 afi/safi（只支持 unicast）；若当前正在通告(有 ADV 标志)，
 *   先 bgp_evpn_withdraw_type5_routes 撤销，再清 flag_oi_none / flag_oi_gw_ip，
 *   并在原为 gateway-ip 时 bgp_addpath_type_changed 清理 addpath id；
 *   最后清 adv_cmd_rmap 的 route-map 信息。
 * 改配置结构：af_flags 的 ADV 标志、adv_cmd_rmap；触发 apply/重建：是（撤销 type-5）。 */
DEFUN (no_bgp_evpn_advertise_type5,
       no_bgp_evpn_advertise_type5_cmd,
       "no advertise " BGP_AFI_CMD_STR "" BGP_SAFI_CMD_STR " [route-map WORD]",
       NO_STR
       "Advertise prefix routes\n"
       BGP_AFI_HELP_STR
       BGP_SAFI_HELP_STR
       "route-map for filtering specific routes\n"
       "Name of the route map\n")
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp); /* bgp vrf instance */
	int idx_afi = 0;
	int idx_safi = 0;
	afi_t afi = 0;
	safi_t safi = 0;
	uint16_t flag_oi_none, flag_oi_gw_ip;
	bool has_flag_oi_none, has_flag_oi_gw_ip;

	if (!bgp_vrf)
		return CMD_WARNING;

	argv_find_and_parse_afi(argv, argc, &idx_afi, &afi);
	argv_find_and_parse_safi(argv, argc, &idx_safi, &safi);

	if (!(afi == AFI_IP || afi == AFI_IP6)) {
		vty_out(vty,
			"%% Only ipv4 or ipv6 address families are supported\n");
		return CMD_WARNING;
	}

	if (safi != SAFI_UNICAST) {
		vty_out(vty,
			"%% Only ipv4 unicast or ipv6 unicast are supported\n");
		return CMD_WARNING;
	}

	if (afi == AFI_IP) {
		flag_oi_none = BGP_L2VPN_EVPN_ADV_IPV4_UNICAST;
		flag_oi_gw_ip = BGP_L2VPN_EVPN_ADV_IPV4_UNICAST_GW_IP;
	} else {
		flag_oi_none = BGP_L2VPN_EVPN_ADV_IPV6_UNICAST;
		flag_oi_gw_ip = BGP_L2VPN_EVPN_ADV_IPV6_UNICAST_GW_IP;
	}
	has_flag_oi_none = CHECK_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_none);
	has_flag_oi_gw_ip = CHECK_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_gw_ip);

	/* if we are not advertising ipv4/ipv6 prefix as type-5, nothing to do */
	if (has_flag_oi_none || has_flag_oi_gw_ip) {
		bgp_evpn_withdraw_type5_routes(bgp_vrf, afi, safi);

		UNSET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_none);
		UNSET_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN], flag_oi_gw_ip);

		/* cleanup addpath IDs if evpn export was the only consumer */
		if (has_flag_oi_gw_ip)
			bgp_addpath_type_changed(bgp_vrf);
	}

	/* clear the route-map information for advertise ipv4/ipv6 unicast */
	if (bgp_vrf->adv_cmd_rmap[afi][safi].name) {
		XFREE(MTYPE_ROUTE_MAP_NAME, bgp_vrf->adv_cmd_rmap[afi][safi].name);
		bgp_vrf->adv_cmd_rmap[afi][safi].name = NULL;
		bgp_vrf->adv_cmd_rmap[afi][safi].map = NULL;
	}

	return CMD_SUCCESS;
}

/* 【命令】[no] use-es-l3nhg
 * 语法：是否对“目的为 ES 的主机路由”使用 L3 nexthop group。
 * 行为：直接置 bgp_mh_info->host_routes_use_l3nhg = no ? false : true。
 * 改配置结构：bgp_mh_info->host_routes_use_l3nhg；触发 apply/重建：由 MH 侧后续处理。 */
DEFPY (bgp_evpn_use_es_l3nhg,
       bgp_evpn_use_es_l3nhg_cmd,
       "[no$no] use-es-l3nhg",
       NO_STR
       "use L3 nexthop group for host routes with ES destination\n")
{
	bgp_mh_info->host_routes_use_l3nhg = no ? false :true;
	return CMD_SUCCESS;
}

/* 【命令】[no] disable-ead-evi-rx
 * 语法：配置“即使未收到 EAD-EVI 也在 EAD-ES 上激活 PE”（EAD-EVI 接收行为）。
 * 行为：no 表示恢复接收(true)，否则禁用接收(false)；仅在值变化时更新
 *   bgp_mh_info->enable_ead_evi_rx 并 bgp_evpn_switch_ead_evi_rx 应用。
 * 改配置结构：bgp_mh_info->enable_ead_evi_rx；触发 apply/重建：是（切换 EAD-EVI rx）。 */
DEFPY (bgp_evpn_ead_evi_rx_disable,
       bgp_evpn_ead_evi_rx_disable_cmd,
       "[no$no] disable-ead-evi-rx",
       NO_STR
       "Activate PE on EAD-ES even if EAD-EVI is not received\n")
{
	bool old_ead_evi_rx = no ? true : false;

	if (old_ead_evi_rx != bgp_mh_info->enable_ead_evi_rx) {
		bgp_mh_info->enable_ead_evi_rx = old_ead_evi_rx;
		bgp_evpn_switch_ead_evi_rx();
	}

	return CMD_SUCCESS;
}

/* 【命令】[no] disable-ead-evi-tx
 * 语法：配置“不为本地 ES 通告 EAD-EVI”（EAD-EVI 发送行为）。
 * 行为：no 表示恢复发送(true)，否则禁止发送(false)；仅在值变化时更新
 *   bgp_mh_info->enable_ead_evi_tx 并 bgp_evpn_switch_ead_evi_tx 应用。
 * 改配置结构：bgp_mh_info->enable_ead_evi_tx；触发 apply/重建：是（切换 EAD-EVI tx）。 */
DEFPY (bgp_evpn_ead_evi_tx_disable,
       bgp_evpn_ead_evi_tx_disable_cmd,
       "[no$no] disable-ead-evi-tx",
       NO_STR
       "Don't advertise EAD-EVI for local ESs\n")
{
	bool old_ead_evi_tx = no ? true : false;

	if (old_ead_evi_tx != bgp_mh_info->enable_ead_evi_tx) {
		bgp_mh_info->enable_ead_evi_tx = old_ead_evi_tx;
		bgp_evpn_switch_ead_evi_tx();
	}

	return CMD_SUCCESS;
}

/* 【命令】[no] enable-resolve-overlay-index
 * 语法：开启/关闭 type-5 路由叠加层索引(overlay index)的递归解析。
 * 行为：须在 EVPN VRF 下；调 bgp_evpn_set_unset_resolve_overlay_index(bgp, no?false:true)。
 * 改配置结构：bgp->resolve_overlay_index；触发 apply/重建：是（对每个 VNI 重解析下一跳）。 */
DEFPY (bgp_evpn_enable_resolve_overlay_index,
       bgp_evpn_enable_resolve_overlay_index_cmd,
       "[no$no] enable-resolve-overlay-index",
       NO_STR
       "Enable Recursive Resolution of type-5 route overlay index\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING;

	if (bgp != bgp_get_evpn()) {
		vty_out(vty, "This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	bgp_evpn_set_unset_resolve_overlay_index(bgp, no ? false : true);
	return CMD_SUCCESS;
}

/* 【命令】[no] advertise-pip [ip <A.B.C.D> [mac <MAC>]]
 * 语法：配置“通告 primary IP(PIP)”，即本 PE 作为 VTEP 的系统 IP/MAC（用于多活场景）。
 * 行为：须在 **L3VNI 的 VRF** 下(非 EVPN underlay 实例)。
 *   set：置 evpn_info->advertise_pip=true；ip 非 0 时记 pip_ip_static 与 pip_ip，
 *     否则 pip_ip 取 EVPN 实例 router-id；mac 非 0 记 pip_rmac_static/pip_rmac，
 *     否则用 zebra 上报的系统 MAC。已配同值则直接成功。
 *   no：argc==2 时整体关闭 advertise_pip 并把 pip_rmac 复位为本机 rmac；
 *     否则仅回退静态 IP/MAC 到默认（保留 PIP 开关），并对不匹配的输入报错。
 *   最后若 EVPN 启用：update_advertise_vrf_routes 更新 VRF 相关路由，
 *     并对启用了 svi macip 的 L2 VNI 调 update_routes_for_vni 刷新 type-2 路由。
 * 改配置结构：bgp_vrf->evpn_info 的 advertise_pip/pip_ip/pip_rmac 等；触发 apply/重建：是。 */
DEFPY (bgp_evpn_advertise_pip_ip_mac,
       bgp_evpn_advertise_pip_ip_mac_cmd,
       "[no$no] advertise-pip [ip <A.B.C.D> [mac <X:X:X:X:X:X|X:X:X:X:X:X/M>]]",
       NO_STR
       "evpn system primary IP\n"
       IP_STR
       "ip address\n"
       MAC_STR MAC_STR MAC_STR)
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp); /* bgp vrf instance */
	struct bgp *bgp_evpn = NULL;

	if (!bgp_vrf || EVPN_ENABLED(bgp_vrf)) {
		vty_out(vty,
			"This command is supported under L3VNI BGP EVPN VRF\n");
		return CMD_WARNING_CONFIG_FAILED;
	}
	bgp_evpn = bgp_get_evpn();

	if (!no) {
		/* pip is already enabled */
		if (argc == 1 && bgp_vrf->evpn_info->advertise_pip)
			return CMD_SUCCESS;

		bgp_vrf->evpn_info->advertise_pip = true;
		if (ip.s_addr != INADDR_ANY) {
			/* Already configured with same IP */
			if (IPV4_ADDR_SAME(&ip, &bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4))
				return CMD_SUCCESS;

			bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4 = ip;
			bgp_vrf->evpn_info->pip_ip.ipaddr_v4 = ip;
		} else {
			bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4.s_addr = INADDR_ANY;
			/* default instance router-id assignemt */
			if (bgp_evpn)
				bgp_vrf->evpn_info->pip_ip.ipaddr_v4 = bgp_evpn->router_id;
		}
		/* parse sys mac */
		if (!is_zero_mac(&mac->eth_addr)) {
			/* Already configured with same MAC */
			if (memcmp(&bgp_vrf->evpn_info->pip_rmac_static,
				   &mac->eth_addr, ETH_ALEN) == 0)
				return CMD_SUCCESS;

			memcpy(&bgp_vrf->evpn_info->pip_rmac_static,
			       &mac->eth_addr, ETH_ALEN);
			memcpy(&bgp_vrf->evpn_info->pip_rmac,
			       &bgp_vrf->evpn_info->pip_rmac_static,
			       ETH_ALEN);
		} else {
			/* Copy zebra sys mac */
			if (!is_zero_mac(&bgp_vrf->evpn_info->pip_rmac_zebra))
				memcpy(&bgp_vrf->evpn_info->pip_rmac,
				       &bgp_vrf->evpn_info->pip_rmac_zebra,
				       ETH_ALEN);
		}
	} else {
		if (argc == 2) {
			if (!bgp_vrf->evpn_info->advertise_pip)
				return CMD_SUCCESS;
			/* Disable PIP feature */
			bgp_vrf->evpn_info->advertise_pip = false;
			/* copy anycast mac */
			memcpy(&bgp_vrf->evpn_info->pip_rmac,
			       &bgp_vrf->rmac, ETH_ALEN);
		} else {
			/* remove MAC-IP option retain PIP knob. */
			if ((ip.s_addr != INADDR_ANY) &&
			    !IPV4_ADDR_SAME(&ip, &bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4)) {
				vty_out(vty,
					"%% BGP EVPN PIP IP does not match\n");
				return CMD_WARNING_CONFIG_FAILED;
			}

			if (!is_zero_mac(&mac->eth_addr) &&
			    memcmp(&bgp_vrf->evpn_info->pip_rmac_static,
				   &mac->eth_addr, ETH_ALEN) != 0) {
				vty_out(vty,
					"%% BGP EVPN PIP MAC does not match\n");
				return CMD_WARNING_CONFIG_FAILED;
			}
			/* pip_rmac can carry vrr_rmac reset only if it matches
			 * with static value.
			 */
			if (memcmp(&bgp_vrf->evpn_info->pip_rmac,
				   &bgp_vrf->evpn_info->pip_rmac_static,
				   ETH_ALEN) == 0) {
				/* Copy zebra sys mac */
				if (!is_zero_mac(
					&bgp_vrf->evpn_info->pip_rmac_zebra))
					memcpy(&bgp_vrf->evpn_info->pip_rmac,
					&bgp_vrf->evpn_info->pip_rmac_zebra,
					       ETH_ALEN);
				else {
					/* copy anycast mac */
					memcpy(&bgp_vrf->evpn_info->pip_rmac,
					       &bgp_vrf->rmac, ETH_ALEN);
				}
			}
		}
		/* reset user configured sys MAC */
		memset(&bgp_vrf->evpn_info->pip_rmac_static, 0, ETH_ALEN);
		/* reset user configured sys IP */
		bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4.s_addr = INADDR_ANY;
		/* Assign default PIP IP (bgp instance router-id) */
		if (bgp_evpn)
			bgp_vrf->evpn_info->pip_ip.ipaddr_v4 = bgp_evpn->router_id;
		else
			bgp_vrf->evpn_info->pip_ip.ipaddr_v4.s_addr = INADDR_ANY;
	}

	if (is_evpn_enabled()) {
		struct listnode *node = NULL;
		struct bgpevpn *vpn = NULL;

		/*
		 * At this point if bgp_evpn is NULL and evpn is enabled
		 * something stupid has gone wrong
		 */
		assert(bgp_evpn);

		update_advertise_vrf_routes(bgp_vrf);

		/* Update (svi) type-2 routes */
		for (ALL_LIST_ELEMENTS_RO(bgp_vrf->l2vnis, node, vpn)) {
			if (!bgp_evpn_is_svi_macip_enabled(vpn))
				continue;
			update_routes_for_vni(bgp_evpn, vpn);
		}
	}

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn vni [<vni>] [json]
 * 语法：显示所有 VNI 的汇总（含 L2/L3 计数与全局开关状态），或单个 VNI 的详情。
 * 行为：无 vni 参数时输出全局状态（Advertise Gateway Macip / SVI Macip / All VNI /
 *   BUM flooding / VXLAN flooding，及 L2/L3 VNI 计数）再 evpn_show_all_vnis；
 *   带 vni 参数时 strtoul 解析并 evpn_show_vni。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display VNI information - for all or a specific VNI
 */
DEFUN(show_bgp_l2vpn_evpn_vni,
      show_bgp_l2vpn_evpn_vni_cmd,
      "show bgp l2vpn evpn vni [" CMD_VNI_RANGE "] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Show VNI\n"
      "VNI number\n"
      JSON_STR)
{
	struct bgp *bgp_evpn;
	vni_t vni;
	int idx = 0;
	bool uj = false;
	json_object *json = NULL;
	uint32_t num_l2vnis = 0;
	uint32_t num_l3vnis = 0;
	uint32_t num_vnis = 0;
	struct listnode *node = NULL;
	struct bgp *bgp_temp = NULL;

	uj = use_json(argc, argv);

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return CMD_WARNING;

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	if (uj)
		json = json_object_new_object();

	if ((uj && argc == ((idx + 1) + 2)) || (!uj && argc == (idx + 1) + 1)) {

		num_l2vnis = hashcount(bgp_evpn->vnihash);

		for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_temp)) {
			if (bgp_temp->l3vni)
				num_l3vnis++;
		}
		num_vnis = num_l2vnis + num_l3vnis;
		if (uj) {
			json_object_string_add(json, "advertiseGatewayMacip",
					       bgp_evpn->advertise_gw_macip
						       ? "Enabled"
						       : "Disabled");
			json_object_string_add(json, "advertiseSviMacIp",
					bgp_evpn->evpn_info->advertise_svi_macip
					? "Enabled" : "Disabled");
			json_object_string_add(json, "advertiseAllVnis",
					       is_evpn_enabled() ? "Enabled"
								 : "Disabled");
			json_object_string_add(
				json, "flooding",
				bgp_evpn->vxlan_flood_ctrl ==
						VXLAN_FLOOD_HEAD_END_REPL
					? "Head-end replication"
					: "Disabled");
			json_object_string_add(
				json, "vxlanFlooding",
				bgp_evpn->vxlan_flood_ctrl ==
						VXLAN_FLOOD_HEAD_END_REPL
					? "Enabled"
					: "Disabled");
			json_object_int_add(json, "numVnis", num_vnis);
			json_object_int_add(json, "numL2Vnis", num_l2vnis);
			json_object_int_add(json, "numL3Vnis", num_l3vnis);
		} else {
			vty_out(vty, "Advertise Gateway Macip: %s\n",
				bgp_evpn->advertise_gw_macip ? "Enabled"
							    : "Disabled");
			vty_out(vty, "Advertise SVI Macip: %s\n",
				bgp_evpn->evpn_info->advertise_svi_macip ? "Enabled"
							: "Disabled");
			vty_out(vty, "Advertise All VNI flag: %s\n",
				is_evpn_enabled() ? "Enabled" : "Disabled");
			vty_out(vty, "BUM flooding: %s\n",
				bgp_evpn->vxlan_flood_ctrl ==
						VXLAN_FLOOD_HEAD_END_REPL
					? "Head-end replication"
					: "Disabled");
			vty_out(vty, "VXLAN flooding: %s\n",
				bgp_evpn->vxlan_flood_ctrl ==
						VXLAN_FLOOD_HEAD_END_REPL
					? "Enabled"
					: "Disabled");
			vty_out(vty, "Number of L2 VNIs: %u\n", num_l2vnis);
			vty_out(vty, "Number of L3 VNIs: %u\n", num_l3vnis);
		}
		evpn_show_all_vnis(vty, bgp_evpn, json);
	} else {
		int vni_idx = 0;

		if (!argv_find(argv, argc, "vni", &vni_idx)) {
			json_object_free(json);
			return CMD_WARNING;
		}

		/* Display specific VNI */
		vni = strtoul(argv[vni_idx + 1]->arg, NULL, 10);
		evpn_show_vni(vty, bgp_evpn, vni, json);
	}

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令（隐藏）】show bgp l2vpn evpn vni remote-ip-hash
 * 语法：dump 各 VNI 的远端 IP 哈希表（调试用）。
 * 行为：hash_iterate(bgp_evpn->vnihash, bgp_evpn_show_remote_ip_hash)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN_HIDDEN(show_bgp_l2vpn_evpn_vni_remote_ip_hash,
	     show_bgp_l2vpn_evpn_vni_remote_ip_hash_cmd,
	     "show bgp l2vpn evpn vni remote-ip-hash",
	     SHOW_STR
	     BGP_STR
	     L2VPN_HELP_STR
	     EVPN_HELP_STR
	     "Show VNI\n"
	     "Remote IP hash\n")
{
	struct bgp *bgp_evpn;
	int idx = 0;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return CMD_WARNING;

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	hash_iterate(bgp_evpn->vnihash,
		     (void (*)(struct hash_bucket *,
			       void *))bgp_evpn_show_remote_ip_hash,
		     vty);

	return CMD_SUCCESS;
}

/* 【命令（隐藏）】show bgp l2vpn evpn vni-svi-hash
 * 语法：dump VNI→SVI 哈希表（调试用）。
 * 行为：hash_iterate(bgp_evpn->vni_svi_hash, bgp_evpn_show_vni_svi_hash)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFUN_HIDDEN(show_bgp_l2vpn_evpn_vni_svi_hash,
	     show_bgp_l2vpn_evpn_vni_svi_hash_cmd,
	     "show bgp l2vpn evpn vni-svi-hash",
	     SHOW_STR
	     BGP_STR
	     L2VPN_HELP_STR
	     EVPN_HELP_STR
	     "Show vni-svi-hash\n")
{
	struct bgp *bgp_evpn;
	int idx = 0;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return CMD_WARNING;

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	hash_iterate(bgp_evpn->vni_svi_hash,
		     (void (*)(struct hash_bucket *,
			       void *))bgp_evpn_show_vni_svi_hash,
		     vty);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn es-evi [vni <v>] [json] [detail]
 * 语法：显示“每个 EVI(VNI) 下的 ES”信息，可限定单个 VNI。
 * 行为：带 vni → bgp_evpn_es_evi_show_vni，否则 bgp_evpn_es_evi_show（均在 MH 模块）。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFPY(show_bgp_l2vpn_evpn_es_evi,
      show_bgp_l2vpn_evpn_es_evi_cmd,
      "show bgp l2vpn evpn es-evi [vni (1-16777215)$vni] [json$uj] [detail$detail]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "ES per EVI\n"
      "VxLAN Network Identifier\n"
      "VNI\n"
      JSON_STR
      "Detailed information\n")
{
	if (vni)
		bgp_evpn_es_evi_show_vni(vty, vni, !!uj, !!detail);
	else
		bgp_evpn_es_evi_show(vty, !!uj, !!detail);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn es [<ESI> | detail] [json]
 * 语法：显示所有 ES，或指定 ESI 的 ES 详情。
 * 行为：给了 esi_str 则 str_to_esi 校验后 bgp_evpn_es_show_esi（格式错误报
 *   "%% Malformed ESI"），否则 bgp_evpn_es_show（MH 模块）。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFPY(show_bgp_l2vpn_evpn_es,
      show_bgp_l2vpn_evpn_es_cmd,
      "show bgp l2vpn evpn es [NAME$esi_str|detail$detail] [json$uj]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Ethernet Segment\n"
      "ES ID\n"
      "Detailed information\n"
      JSON_STR)
{
	esi_t esi;

	if (esi_str) {
		if (!str_to_esi(esi_str, &esi)) {
			vty_out(vty, "%% Malformed ESI\n");
			return CMD_WARNING;
		}
		bgp_evpn_es_show_esi(vty, &esi, uj);
	} else {

		bgp_evpn_es_show(vty, uj, !!detail);
	}

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn es-vrf [<ESI>] [json]
 * 语法：显示 ES 与 VRF 的对应关系，可限定某个 ESI。
 * 行为：给了 esi_str 则校验后 bgp_evpn_es_vrf_show_esi，否则 bgp_evpn_es_vrf_show。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFPY(show_bgp_l2vpn_evpn_es_vrf, show_bgp_l2vpn_evpn_es_vrf_cmd,
      "show bgp l2vpn evpn es-vrf [NAME$esi_str] [json$uj]",
      SHOW_STR BGP_STR L2VPN_HELP_STR EVPN_HELP_STR
      "Ethernet Segment\n"
      "ES ID\n" JSON_STR)
{
	esi_t esi;

	if (esi_str) {
		if (!str_to_esi(esi_str, &esi)) {
			vty_out(vty, "%% Malformed ESI\n");
			return CMD_WARNING;
		}
		bgp_evpn_es_vrf_show_esi(vty, &esi, uj);
	} else {

		bgp_evpn_es_vrf_show(vty, uj, NULL);
	}

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn next-hops [json]
 * 语法：显示 EVPN 的下一跳(nexthop)表。
 * 行为：调 bgp_evpn_nh_show(vty, uj)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFPY(show_bgp_l2vpn_evpn_nh,
      show_bgp_l2vpn_evpn_nh_cmd,
      "show bgp l2vpn evpn next-hops [json$uj]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Nexthops\n"
      JSON_STR)
{
	bgp_evpn_nh_show(vty, uj);

	return CMD_SUCCESS;
}

/* 【命令】show bgp [vrf VRFNAME] l2vpn evpn summary [established|failed]
 *   [<neighbor <addr>|remote-as <AS|internal|external>>] [terse] [wide] [json]
 * 语法：显示 EVPN 地址族的邻居汇总（BGP summary 的 EVPN 版）。
 * 行为：解析 vrf / established|failed / neighbor / remote-as / terse / wide / json，
 *   转成 show_flags 位后调 bgp_show_summary_vty(vty, vrf, AFI_L2VPN, SAFI_EVPN, ...)
 *   （汇总本身在 bgp_vty.c，此处只是参数搬运）。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display EVPN neighbor summary.
 */
DEFUN(show_bgp_l2vpn_evpn_summary, show_bgp_l2vpn_evpn_summary_cmd,
      "show bgp [vrf VRFNAME] l2vpn evpn summary [established|failed] [<neighbor <A.B.C.D|X:X::X:X|WORD>|remote-as <(1-4294967295)|internal|external>>] [terse] [wide] [json]",
      SHOW_STR BGP_STR
      "bgp vrf\n"
      "vrf name\n" L2VPN_HELP_STR EVPN_HELP_STR
      "Summary of BGP neighbor status\n"
      "Show only sessions in Established state\n"
      "Show only sessions not in Established state\n"
      "Show only the specified neighbor session\n"
      "Neighbor to display information about\n"
      "Neighbor to display information about\n"
      "Neighbor on BGP configured interface\n"
      "Show only the specified remote AS sessions\n"
      "AS number\n"
      "Internal (iBGP) AS sessions\n"
      "External (eBGP) AS sessions\n"
      "Shorten the information on BGP instances\n"
      "Increase table width for longer output\n" JSON_STR)
{
	int idx_vrf = 0;
	int idx = 0;
	char *vrf = NULL;
	char *neighbor = NULL;
	as_t as = 0; /* 0 means AS filter not set */
	enum peer_asn_type as_type = AS_UNSPECIFIED;
	uint16_t show_flags = 0;

	if (argv_find(argv, argc, "vrf", &idx_vrf))
		vrf = argv[++idx_vrf]->arg;

	if (argv_find(argv, argc, "failed", &idx))
		SET_FLAG(show_flags, BGP_SHOW_OPT_FAILED);

	if (argv_find(argv, argc, "established", &idx))
		SET_FLAG(show_flags, BGP_SHOW_OPT_ESTABLISHED);


	if (argv_find(argv, argc, "neighbor", &idx))
		neighbor = argv[idx + 1]->arg;

	if (argv_find(argv, argc, "remote-as", &idx)) {
		if (argv[idx + 1]->arg[0] == 'i')
			as_type = AS_INTERNAL;
		else if (argv[idx + 1]->arg[0] == 'e')
			as_type = AS_EXTERNAL;
		else
			as = (as_t)atoi(argv[idx + 1]->arg);
	}

	if (argv_find(argv, argc, "terse", &idx))
		SET_FLAG(show_flags, BGP_SHOW_OPT_TERSE);

	if (argv_find(argv, argc, "wide", &idx))
		SET_FLAG(show_flags, BGP_SHOW_OPT_WIDE);

	if (use_json(argc, argv))
		SET_FLAG(show_flags, BGP_SHOW_OPT_JSON);

	return bgp_show_summary_vty(vty, vrf, AFI_L2VPN, SAFI_EVPN, neighbor,
				    as_type, as, show_flags);
}

/* 【函数】bgp_evpn_cli_parse_type_cmp —— 把 “type” 关键字后的字符串映射为 EVPN 路由类型：
 *   "ma*"/"2" → BGP_EVPN_MAC_IP_ROUTE   (type-2)
 *   "mu*"/"3" → BGP_EVPN_IMET_ROUTE     (type-3)
 *   "es*"/"4" → BGP_EVPN_ES_ROUTE       (type-4)
 *   "ea*"/"1" → BGP_EVPN_AD_ROUTE       (type-1)
 *   "p*" /"5" → BGP_EVPN_IP_PREFIX_ROUTE(type-5)
 *   未识别返回 -1。注意匹配用的是前缀首字母缩写（ma/mu/es/ea/p）。 */
static int bgp_evpn_cli_parse_type_cmp(int *type, const char *type_str)
{
	if ((strncmp(type_str, "ma", 2) == 0) || (strmatch(type_str, "2")))
		*type = BGP_EVPN_MAC_IP_ROUTE;
	else if ((strncmp(type_str, "mu", 2) == 0) || (strmatch(type_str, "3")))
		*type = BGP_EVPN_IMET_ROUTE;
	else if ((strncmp(type_str, "es", 2) == 0) || (strmatch(type_str, "4")))
		*type = BGP_EVPN_ES_ROUTE;
	else if ((strncmp(type_str, "ea", 2) == 0) || (strmatch(type_str, "1")))
		*type = BGP_EVPN_AD_ROUTE;
	else if ((strncmp(type_str, "p", 1) == 0) || (strmatch(type_str, "5")))
		*type = BGP_EVPN_IP_PREFIX_ROUTE;
	else
		return -1;

	return 0;
}

/* 【函数】bgp_evpn_cli_parse_type —— 从命令行里找 “type <x>” 并解析出路由类型
 * （声明见 bgp_evpn_vty.h）。未出现 type 关键字则 type 保持 0（不限类型）；
 * 出现则交给 bgp_evpn_cli_parse_type_cmp，失败返回 -1。 */
int bgp_evpn_cli_parse_type(int *type, struct cmd_token **argv, int argc)
{
	int type_idx = 0;

	if (argv_find(argv, argc, "type", &type_idx)) {
		/* Specific type is requested */
		if (bgp_evpn_cli_parse_type_cmp(type,
						argv[type_idx + 1]->arg) != 0)
			return -1;
	}

	return 0;
}

/* 【命令】show bgp l2vpn evpn route [detail] [type <t>] [self-originate] [json]
 * 语法：显示**全局 EVPN 路由表**，可选 detail、按类型过滤、只看本机自身产生。
 * 行为：bgp_evpn_cli_parse_type 解析 type（失败返回 CMD_WARNING）；解析 detail /
 *   self-originate；调 evpn_show_all_routes(vty, bgp, type, json, detail, self_orig)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display global EVPN routing table.
 */
DEFUN(show_bgp_l2vpn_evpn_route,
      show_bgp_l2vpn_evpn_route_cmd,
      "show bgp l2vpn evpn route [detail] [type "EVPN_TYPE_ALL_LIST"] ["BGP_SELF_ORIG_CMD_STR"] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      "Display Detailed Information\n"
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_ALL_LIST_HELP_STR
      BGP_SELF_ORIG_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	int detail = 0;
	int type = 0;
	bool uj = false;
	int arg_idx = 0;
	bool self_orig = false;
	json_object *json = NULL;

	uj = use_json(argc, argv);

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	if (bgp_evpn_cli_parse_type(&type, argv, argc) < 0)
		return CMD_WARNING;

	if (uj) {
		json = json_object_new_object();
		vty_out(vty, "{\n");
	}

	if (argv_find(argv, argc, "detail", &detail))
		detail = 1;

	if (argv_find(argv, argc, BGP_SELF_ORIG_CMD_STR, &arg_idx))
		self_orig = true;

	evpn_show_all_routes(vty, bgp, type, json, detail, self_orig);

	if (uj) {
		vty_out(vty, "}\n");
		json_object_free(json);
	}

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn route rd <RD|all> [type <t>] [json]
 * 语法：显示**指定 RD** 的 EVPN 路由表。
 * 行为：解析 RD(失败报 "%% Malformed Route Distinguisher") 与 type；all 时以
 *   evpn_show_all_routes(detail=1) 展示全部，否则 evpn_show_route_rd 展示该 RD。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display global EVPN routing table for specific RD.
 */
DEFUN(show_bgp_l2vpn_evpn_route_rd,
      show_bgp_l2vpn_evpn_route_rd_cmd,
      "show bgp l2vpn evpn route rd <ASN:NN_OR_IP-ADDRESS:NN|all> [type "EVPN_TYPE_ALL_LIST"] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      EVPN_RT_DIST_HELP_STR
      EVPN_ASN_IP_HELP_STR
      "All VPN Route Distinguishers\n"
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_ALL_LIST_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	int ret = 0;
	struct prefix_rd prd;
	int type = 0;
	bool uj = false;
	json_object *json = NULL;
	int idx_ext_community = 0;
	int rd_all = 0;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	uj = use_json(argc, argv);

	if (!argv_find(argv, argc, "all", &rd_all)) {
		/* get the RD */
		if (argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN",
			      &idx_ext_community)) {
			ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
			if (!ret) {
				vty_out(vty,
					"%% Malformed Route Distinguisher\n");
				return CMD_WARNING;
			}
		}
	}

	if (bgp_evpn_cli_parse_type(&type, argv, argc) < 0)
		return CMD_WARNING;

	if (uj)
		json = json_object_new_object();

	if (rd_all) {
		if (uj)
			vty_out(vty, "{\n");

		evpn_show_all_routes(vty, bgp, type, json, 1, false);

		if (uj) {
			vty_out(vty, "}\n");
			json_object_free(json);
			return CMD_SUCCESS;
		}
	} else {
		evpn_show_route_rd(vty, bgp, &prd, type, json);
	}

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn route rd <RD|all> mac <MAC> [ip <IP>] [json]
 * 语法：在指定 RD（或全部 RD）下按 MAC（可选带 IP）查 type-2 路由。
 * 行为：解析 RD、MAC(prefix_str2mac，失败报错)、可选 IP(str2ipaddr)；
 *   all → evpn_show_route_rd_all_macip，否则 evpn_show_route_rd_macip。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display global EVPN routing table for specific RD and MACIP.
 */
DEFUN(show_bgp_l2vpn_evpn_route_rd_macip,
      show_bgp_l2vpn_evpn_route_rd_macip_cmd,
      "show bgp l2vpn evpn route rd <ASN:NN_OR_IP-ADDRESS:NN|all> mac WORD [ip WORD] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      EVPN_RT_DIST_HELP_STR
      EVPN_ASN_IP_HELP_STR
      "All VPN Route Distinguishers\n"
      "MAC\n"
      "MAC address (e.g., 00:e0:ec:20:12:62)\n"
      "IP\n"
      "IP address (IPv4 or IPv6)\n"
      JSON_STR)
{
	struct bgp *bgp;
	int ret = 0;
	struct prefix_rd prd;
	struct ethaddr mac;
	struct ipaddr ip;
	int idx_ext_community = 0;
	int mac_idx = 0;
	int ip_idx = 0;
	bool uj = false;
	json_object *json = NULL;
	int rd_all = 0;

	memset(&mac, 0, sizeof(struct ethaddr));
	memset(&ip, 0, sizeof(struct ipaddr));

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	uj = use_json(argc, argv);

	/* get the prd */
	if (!argv_find(argv, argc, "all", &rd_all)) {
		if (argv_find(argv, argc, "ASN:NN_OR_IP-ADDRESS:NN",
			      &idx_ext_community)) {
			ret = str2prefix_rd(argv[idx_ext_community]->arg, &prd);
			if (!ret) {
				vty_out(vty,
					"%% Malformed Route Distinguisher\n");
				return CMD_WARNING;
			}
		}
	}

	/* get the mac */
	if (argv_find(argv, argc, "mac", &mac_idx)) {
		if (!prefix_str2mac(argv[mac_idx + 1]->arg, &mac)) {
			vty_out(vty, "%% Malformed MAC address\n");
			return CMD_WARNING;
		}
	}

	/* get the ip if specified */
	if (argv_find(argv, argc, "ip", &ip_idx)) {
		if (str2ipaddr(argv[ip_idx + 1]->arg, &ip) != 0) {
			vty_out(vty, "%% Malformed IP address\n");
			return CMD_WARNING;
		}
	}

	if (uj)
		json = json_object_new_object();

	if (rd_all)
		evpn_show_route_rd_all_macip(vty, bgp, &mac, &ip, json);
	else
		evpn_show_route_rd_macip(vty, bgp, &prd, &mac, &ip, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn route esi <ESI> [json]
 * 语法：显示指定 ESI 的 EVPN 路由表（该 ES 自有的路由）。
 * 行为：str_to_esi 校验(失败报 "%% Malformed ESI")，调 evpn_show_routes_esi。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/* Display per ESI routing table */
DEFUN(show_bgp_l2vpn_evpn_route_esi,
      show_bgp_l2vpn_evpn_route_esi_cmd,
      "show bgp l2vpn evpn route esi ESI [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      "Ethernet Segment Identifier\n"
      "ESI ID\n"
      JSON_STR)
{
	bool uj = false;
	esi_t esi;
	struct bgp *bgp = NULL;
	json_object *json = NULL;

	memset(&esi, 0, sizeof(esi));
	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	uj = use_json(argc, argv);

	/* get the ESI - ESI-ID is at argv[6] */
	if (!str_to_esi(argv[6]->arg, &esi)) {
		vty_out(vty, "%% Malformed ESI\n");
		return CMD_WARNING;
	}

	if (uj)
		json = json_object_new_object();

	evpn_show_routes_esi(vty, bgp, &esi, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}


/* 【命令】show bgp l2vpn evpn route vni <vni> [type <ead|1|macip|2|multicast|3> | vtep <ip>] [json]
 * 语法：显示指定 VNI 的 EVPN 路由表，可按 type 过滤或按远端 vtep 过滤。
 * 行为：解析 type；调 evpn_show_routes_vni(vty, bgp, vni, type, false, vtep_ip, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN routing table.
 */
DEFPY(show_bgp_l2vpn_evpn_route_vni, show_bgp_l2vpn_evpn_route_vni_cmd,
      "show bgp l2vpn evpn route vni " CMD_VNI_RANGE " [<type <ead|1|macip|2|multicast|3> | vtep <A.B.C.D|X:X::X:X>$vtep_ip>] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      "VXLAN Network Identifier\n"
      "VNI number\n"
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_1_HELP_STR
      EVPN_TYPE_1_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_3_HELP_STR
      EVPN_TYPE_3_HELP_STR
      "Remote VTEP address\n"
      "Remote VTEP IPv4 address\n"
      "Remote VTEP IPv6 address\n"
      JSON_STR)
{
	struct bgp *bgp;
	int type = 0;
	int idx = 0;
	bool uj = false;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	uj = use_json(argc, argv);

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	if (bgp_evpn_cli_parse_type(&type, argv, argc) < 0)
		return CMD_WARNING;

	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni(vty, bgp, vni, type, false, vtep_ip, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn route vni <vni> mac <MAC> [ip <IP>] [json]
 * 语法：在指定 VNI 里按 MAC（可选带 IP）查 type-2 路由。
 * 行为：解析 vni / mac(prefix_str2mac) / 可选 ip(str2ipaddr)，
 *   调 evpn_show_route_vni_macip。这里的 argv 索引是写死的(idx+3 / idx+5 / idx+7)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN routing table for specific MACIP.
 */
DEFUN(show_bgp_l2vpn_evpn_route_vni_macip,
      show_bgp_l2vpn_evpn_route_vni_macip_cmd,
      "show bgp l2vpn evpn route vni " CMD_VNI_RANGE " mac WORD [ip WORD] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      "VXLAN Network Identifier\n"
      "VNI number\n"
      "MAC\n"
      "MAC address (e.g., 00:e0:ec:20:12:62)\n"
      "IP\n"
      "IP address (IPv4 or IPv6)\n"
      JSON_STR)
{
	vni_t vni;
	struct bgp *bgp;
	struct ethaddr mac;
	struct ipaddr ip;
	int idx = 0;
	bool uj = false;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	uj = use_json(argc, argv);

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	/* get the VNI */
	vni = strtoul(argv[idx + 3]->arg, NULL, 10);

	/* get the mac */
	if (!prefix_str2mac(argv[idx + 5]->arg, &mac)) {
		vty_out(vty, "%% Malformed MAC address\n");
		return CMD_WARNING;
	}

	/* get the ip */
	memset(&ip, 0, sizeof(ip));
	if ((!uj && ((argc == (idx + 1 + 7)) && argv[idx + 7]->arg != NULL))
	    || (uj
		&& ((argc == (idx + 1 + 8)) && argv[idx + 7]->arg != NULL))) {
		if (str2ipaddr(argv[idx + 7]->arg, &ip) != 0) {
			vty_out(vty, "%% Malformed IP address\n");
			return CMD_WARNING;
		}
	}

	if (uj)
		json = json_object_new_object();

	evpn_show_route_vni_macip(vty, bgp, vni, &mac, &ip, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn route vni <vni> multicast <A.B.C.D> [json]
 * 语法：在指定 VNI 里查一条 type-3(组播/IMET) 路由，按 originator IP 定位。
 * 行为：解析 vni 与 orig_ip(inet_aton，失败报 "%% Malformed Originating Router IP address")，
 *   调 evpn_show_route_vni_multicast。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN routing table for specific multicast IP (remote VTEP).
 */
DEFUN(show_bgp_l2vpn_evpn_route_vni_multicast,
      show_bgp_l2vpn_evpn_route_vni_multicast_cmd,
      "show bgp l2vpn evpn route vni " CMD_VNI_RANGE " multicast A.B.C.D [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      "VXLAN Network Identifier\n"
      "VNI number\n"
      EVPN_TYPE_3_HELP_STR
      "Originating Router IP address\n"
      JSON_STR)
{
	vni_t vni;
	struct bgp *bgp;
	int ret;
	struct ipaddr orig_ip;
	int idx = 0;
	bool uj = false;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	uj = use_json(argc, argv);

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	/* get the VNI */
	vni = strtoul(argv[idx + 3]->arg, NULL, 10);

	/* get the ip */
	ret = inet_aton(argv[idx + 5]->arg, &orig_ip.ipaddr_v4);
	SET_IPADDR_V4(&orig_ip);
	if (!ret) {
		vty_out(vty, "%% Malformed Originating Router IP address\n");
		return CMD_WARNING;
	}

	if (uj)
		json = json_object_new_object();

	evpn_show_route_vni_multicast(vty, bgp, vni, &orig_ip, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn route vni all [detail] [vtep <ip>] [json]
 * 语法：显示**所有 VNI** 的 EVPN 路由表，可选 detail、按远端 vtep 过滤。
 * 行为：调 evpn_show_routes_vni_all(vty, bgp, 0(不限类型), false, vtep_ip, json, da)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN routing table - for all VNIs.
 */
DEFPY(show_bgp_l2vpn_evpn_route_vni_all,
      show_bgp_l2vpn_evpn_route_vni_all_cmd,
      "show bgp l2vpn evpn route vni all [detail] [vtep <A.B.C.D|X:X::X:X>$vtep_ip] [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      EVPN_RT_HELP_STR
      "VXLAN Network Identifier\n"
      "All VNIs\n"
      "Print Detailed Output\n"
      "Remote VTEP\n"
      "Remote VTEP IPv4 address\n"
      "Remote VTEP IPv6 address\n"
      JSON_STR)
{
	struct bgp *bgp;
	int idx = 0;
	bool uj = false;
	json_object *json = NULL;
	/* Detail Adjust. Adjust indexes according to detail option */
	int da = 0;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	uj = use_json(argc, argv);

	if (!argv_find(argv, argc, "evpn", &idx))
		return CMD_WARNING;

	if (argv_find(argv, argc, "detail", &da))
		da = 1;

	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni_all(vty, bgp, 0, false, vtep_ip, json, da);

	if (uj) {
		vty_json(vty, json);
		json_object_free(json);
	}

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni all [vtep <ip>] [detail] [json]
 * 语法：显示所有 VNI 的**全部**路由表（IP 表与 MAC 表都打）。
 * 行为：调 evpn_show_routes_vni_all_type_all(vty, bgp, addr, json, !!detail)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN ALL routing tables - for all VNIs.
 */
DEFPY(show_bgp_vni_all,
      show_bgp_vni_all_cmd,
      "show bgp vni all [vtep <A.B.C.D|X:X::X:X>$addr] [detail$detail] [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_ALL_HELP_STR
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      DETAIL_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni_all_type_all(vty, bgp, addr, json, !!detail);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni all type <1|ead> [vtep <ip>] [detail|json]
 * 语法：显示所有 VNI 的 type-1(EAD) 路由表。
 * 行为：调 evpn_show_routes_vni_all(vty, bgp, BGP_EVPN_AD_ROUTE, false, addr, json, !!detail)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN EAD routing table - for all VNIs.
 */
DEFPY(show_bgp_vni_all_ead,
      show_bgp_vni_all_ead_cmd,
      "show bgp vni all type <1|ead> [vtep <A.B.C.D|X:X::X:X>$addr] [<detail$detail|json$uj>]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_ALL_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_1_HELP_STR
      EVPN_TYPE_1_HELP_STR
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      DETAIL_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni_all(vty, bgp, BGP_EVPN_AD_ROUTE, false, addr, json,
				 !!detail);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni all type <2|macip> mac [vtep <ip>] [detail|json]
 * 语法：显示所有 VNI 的 type-2(0 MAC-IP) 的 **MAC 表**。
 * 行为：调 evpn_show_routes_vni_all(...BGP_EVPN_MAC_IP_ROUTE, mac_table=true...)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN MAC routing table - for all VNIs.
 */
DEFPY(show_bgp_vni_all_macip_mac,
      show_bgp_vni_all_macip_mac_cmd,
      "show bgp vni all type <2|macip> mac [vtep <A.B.C.D|X:X::X:X>$addr] [<detail$detail|json$uj>]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_ALL_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_2_HELP_STR
      "MAC Table\n"
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      DETAIL_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni_all(vty, bgp, BGP_EVPN_MAC_IP_ROUTE, true, addr,
				 json, !!detail);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni all type <2|macip> ip [vtep <ip>] [detail|json]
 * 语法：显示所有 VNI 的 type-2(MAC-IP) 的 **IP 表**（远端 MAC-IP 前缀视图）。
 * 行为：调 evpn_show_routes_vni_all(...BGP_EVPN_MAC_IP_ROUTE, mac_table=false...)，
 *   即用 type 过滤 MAC-IP 路由、并展示 IP 表那一侧。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN IP routing table - for all VNIs.
 */
DEFPY(show_bgp_vni_all_macip_ip,
      show_bgp_vni_all_macip_ip_cmd,
      "show bgp vni all type <2|macip> ip [vtep <A.B.C.D|X:X::X:X>$addr] [<detail$detail|json$uj>]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_ALL_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_2_HELP_STR
      "IP Table\n"
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      DETAIL_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni_all(vty, bgp, BGP_EVPN_MAC_IP_ROUTE, false, addr,
				 json, !!detail);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni all type <3|multicast> [vtep <ip>] [detail|json]
 * 语法：显示所有 VNI 的 type-3(组播/IMET) 路由表。
 * 行为：调 evpn_show_routes_vni_all(...BGP_EVPN_IMET_ROUTE...)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN Multicast routing table - for all VNIs.
 */
DEFPY(show_bgp_vni_all_imet,
      show_bgp_vni_all_imet_cmd,
      "show bgp vni all type <3|multicast> [vtep <A.B.C.D|X:X::X:X>$addr] [<detail$detail|json$uj>]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_ALL_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_3_HELP_STR
      EVPN_TYPE_3_HELP_STR
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      DETAIL_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni_all(vty, bgp, BGP_EVPN_IMET_ROUTE, false, addr,
				 json, !!detail);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> [vtep <ip>] [json]
 * 语法：显示**指定 VNI** 的全部路由表：先 IP 表，再 MAC 表。
 * 行为：evpn_show_routes_vni(mac_table=false) 打 IP 表；文本时插入 "MAC Table:" 标题；
 *   再 evpn_show_routes_vni(mac_table=true) 打 MAC 表（JSON 时挂在 "macTable")。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN ALL routing tables - for select VNI
 */
DEFPY(show_bgp_vni,
      show_bgp_vni_cmd,
      "show bgp vni "CMD_VNI_RANGE"$vni [vtep <A.B.C.D|X:X::X:X>$addr] [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_NUM_HELP_STR
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;
	json_object *json_mac = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj) {
		json = json_object_new_object();
		json_mac = json_object_new_object();
	}

	evpn_show_routes_vni(vty, bgp, vni, 0, false, addr, json);

	if (!uj)
		vty_out(vty, "\n\nMAC Table:\n\n");

	evpn_show_routes_vni(vty, bgp, vni, 0, true, addr, json_mac);

	if (uj) {
		json_object_object_add(json, "macTable", json_mac);
		vty_json(vty, json);
	}

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> type <1|ead> [vtep <ip>] [json]
 * 语法：显示指定 VNI 的 type-1(EAD) 路由表。
 * 行为：evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_AD_ROUTE, false, addr, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN EAD routing table - for select VNI
 */
DEFPY(show_bgp_vni_ead,
      show_bgp_vni_ead_cmd,
      "show bgp vni "CMD_VNI_RANGE"$vni type <1|ead> [vtep <A.B.C.D|X:X::X:X>$addr] [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_NUM_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_1_HELP_STR
      EVPN_TYPE_1_HELP_STR
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_AD_ROUTE, false, addr,
			     json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> type <2|macip> mac [vtep <ip>] [json]
 * 语法：显示指定 VNI 的 type-2(MAC-IP) 的 MAC 表。
 * 行为：evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_MAC_IP_ROUTE, mac_table=true, addr, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN MAC-IP MAC routing table - for select VNI
 */
DEFPY(show_bgp_vni_macip_mac,
      show_bgp_vni_macip_mac_cmd,
      "show bgp vni "CMD_VNI_RANGE"$vni type <2|macip> mac [vtep <A.B.C.D|X:X::X:X>$addr] [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_NUM_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_2_HELP_STR
      "MAC Table\n"
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_MAC_IP_ROUTE, true, addr,
			     json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> type <2|macip> ip [vtep <ip>] [json]
 * 语法：显示指定 VNI 的 type-2(MAC-IP) 的 IP 表。
 * 行为：evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_MAC_IP_ROUTE, mac_table=false, addr, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN MAC-IP IP routing table - for select VNI
 */
DEFPY(show_bgp_vni_macip_ip,
      show_bgp_vni_macip_ip_cmd,
      "show bgp vni "CMD_VNI_RANGE"$vni type <2|macip> ip [vtep <A.B.C.D|X:X::X:X>$addr] [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_NUM_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_2_HELP_STR
      "IP Table\n"
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_MAC_IP_ROUTE, false, addr,
			     json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> type <3|multicast> [vtep <ip>] [json]
 * 语法：显示指定 VNI 的 type-3(组播/IMET) 路由表。
 * 行为：evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_IMET_ROUTE, false, addr, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN Multicast routing table - for select VNI
 */
DEFPY(show_bgp_vni_imet,
      show_bgp_vni_imet_cmd,
      "show bgp vni "CMD_VNI_RANGE"$vni type <3|multicast> [vtep <A.B.C.D|X:X::X:X>$addr] [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_NUM_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_3_HELP_STR
      EVPN_TYPE_3_HELP_STR
      VTEP_HELP_STR
      VTEP_IP_HELP_STR
      VTEP_IPV6_HELP_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_routes_vni(vty, bgp, vni, BGP_EVPN_IMET_ROUTE, false, addr,
			     json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> type <2|macip> mac <MAC> [json]
 * 语法：在指定 VNI 里按 MAC 精确查一条 type-2 路由。
 * 行为：evpn_show_route_vni_macip(vty, bgp, vni, &mac->eth_addr, NULL, json)（ip 传 NULL）。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN MACIP MAC routing table - for select VNI & MAC
 */
DEFPY(show_bgp_vni_macip_mac_addr,
      show_bgp_vni_macip_mac_addr_cmd,
      "show bgp vni "CMD_VNI_RANGE"$vni type <2|macip> mac X:X:X:X:X:X [json$uj]",
      SHOW_STR
      BGP_STR
      VNI_HELP_STR
      VNI_NUM_HELP_STR
      EVPN_TYPE_HELP_STR
      EVPN_TYPE_2_HELP_STR
      EVPN_TYPE_2_HELP_STR
      "MAC Table\n"
      MAC_STR
      JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	evpn_show_route_vni_macip(vty, bgp, vni, &mac->eth_addr, NULL, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp vni <vni> type <2|macip> ip <IP> [json]
 * 语法：在指定 VNI 里按 IP 精确查一条 type-2 路由。
 * 行为：把 sockunion 形式的 IP 转成 struct ipaddr，evpn_show_route_vni_macip(...NULL, &ip_addr, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display per-VNI EVPN MACIP IP routing table - for select VNI & IP
 */
DEFPY(show_bgp_vni_macip_ip_addr, show_bgp_vni_macip_ip_addr_cmd,
      "show bgp vni " CMD_VNI_RANGE
      "$vni type <2|macip> ip <A.B.C.D|X:X::X:X> [json$uj]",
      SHOW_STR BGP_STR VNI_HELP_STR VNI_NUM_HELP_STR EVPN_TYPE_HELP_STR
	      EVPN_TYPE_2_HELP_STR EVPN_TYPE_2_HELP_STR
      "IP Table\n" IP_ADDR_STR IP6_ADDR_STR JSON_STR)
{
	struct bgp *bgp;
	json_object *json = NULL;
	struct ipaddr ip_addr = {.ipa_type = IPADDR_NONE};

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	/* check if we need json output */
	if (uj)
		json = json_object_new_object();

	if (sockunion_family(ip) == AF_INET) {
		ip_addr.ipa_type = IPADDR_V4;
		ip_addr.ipaddr_v4.s_addr = sockunion2ip(ip);
	} else {
		ip_addr.ipa_type = IPADDR_V6;
		memcpy(&ip_addr.ipaddr_v6, &ip->sin6.sin6_addr,
				sizeof(struct in6_addr));
	}
	evpn_show_route_vni_macip(vty, bgp, vni, NULL, &ip_addr, json);


	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令（隐藏）】show bgp l2vpn evpn route mac-ip-evi-es [<ESI>|detail] [json]
 * 语法：显示 EVI 表里链接到 ES 的 MAC-IP 路由（隐藏，调试用）。
 * 行为：可选 ESI(str_to_esi)；调 bgp_evpn_show_routes_mac_ip_evi_es。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFPY_HIDDEN(
	show_bgp_l2vpn_evpn_route_mac_ip_evi_es,
	show_bgp_l2vpn_evpn_route_mac_ip_evi_es_cmd,
	"show bgp l2vpn evpn route mac-ip-evi-es [NAME$esi_str|detail$detail] [json$uj]",
	SHOW_STR BGP_STR L2VPN_HELP_STR EVPN_HELP_STR
	"EVPN route information\n"
	"MAC IP routes in the EVI tables linked to the ES\n"
	"ES ID\n"
	"Detailed information\n" JSON_STR)
{
	esi_t esi;
	esi_t *esi_p;
	json_object *json = NULL;

	if (esi_str) {
		if (!str_to_esi(esi_str, &esi)) {
			vty_out(vty, "%% Malformed ESI\n");
			return CMD_WARNING;
		}
		esi_p = &esi;
	} else {
		esi_p = NULL;
	}

	if (uj)
		json = json_object_new_object();
	bgp_evpn_show_routes_mac_ip_evi_es(vty, esi_p, json, !!detail);
	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令（隐藏）】show bgp l2vpn evpn route mac-ip-global-es [<ESI>|detail] [json]
 * 语法：显示全局表里链接到 ES 的 MAC-IP 路由（隐藏，调试用）。
 * 行为：可选 ESI(str_to_esi)；调 bgp_evpn_show_routes_mac_ip_global_es。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
DEFPY_HIDDEN(
	show_bgp_l2vpn_evpn_route_mac_ip_global_es,
	show_bgp_l2vpn_evpn_route_mac_ip_global_es_cmd,
	"show bgp l2vpn evpn route mac-ip-global-es [NAME$esi_str|detail$detail] [json$uj]",
	SHOW_STR BGP_STR L2VPN_HELP_STR EVPN_HELP_STR
	"EVPN route information\n"
	"MAC IP routes in the global table linked to the ES\n"
	"ES ID\n"
	"Detailed information\n" JSON_STR)
{
	esi_t esi;
	esi_t *esi_p;
	json_object *json = NULL;

	if (esi_str) {
		if (!str_to_esi(esi_str, &esi)) {
			vty_out(vty, "%% Malformed ESI\n");
			return CMD_WARNING;
		}
		esi_p = &esi;
	} else {
		esi_p = NULL;
	}

	if (uj)
		json = json_object_new_object();
	bgp_evpn_show_routes_mac_ip_global_es(vty, esi_p, json, !!detail);
	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn vrf-import-rt [json]
 * 语法：显示 VRF 导入 RT 哈希表（哪些 VRF 导入了哪些 RT）。
 * 行为：调 evpn_show_vrf_import_rts(vty, bgp_evpn, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display EVPN import route-target hash table
 */
DEFUN(show_bgp_l2vpn_evpn_vrf_import_rt,
      show_bgp_l2vpn_evpn_vrf_import_rt_cmd,
      "show bgp l2vpn evpn vrf-import-rt [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Show vrf import route target\n"
      JSON_STR)
{
	bool uj = false;
	struct bgp *bgp_evpn = NULL;
	json_object *json = NULL;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return CMD_WARNING;

	uj = use_json(argc, argv);
	if (uj)
		json = json_object_new_object();

	evpn_show_vrf_import_rts(vty, bgp_evpn, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】show bgp l2vpn evpn import-rt [json]
 * 语法：显示 VNI 导入 RT 哈希表（哪些 VNI 导入了哪些 RT）。
 * 行为：调 evpn_show_import_rts(vty, bgp, json)。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/*
 * Display EVPN import route-target hash table
 */
DEFUN(show_bgp_l2vpn_evpn_import_rt,
      show_bgp_l2vpn_evpn_import_rt_cmd,
      "show bgp l2vpn evpn import-rt [json]",
      SHOW_STR
      BGP_STR
      L2VPN_HELP_STR
      EVPN_HELP_STR
      "Show import route target\n"
      JSON_STR)
{
	struct bgp *bgp;
	bool uj = false;
	json_object *json = NULL;

	bgp = bgp_get_evpn();
	if (!bgp)
		return CMD_WARNING;

	uj = use_json(argc, argv);
	if (uj)
		json = json_object_new_object();

	evpn_show_import_rts(vty, bgp, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令（隐藏，仅供测试）】[no] test es <ESI> [state up|down]
 * 语法：手工在本机添加/删除一个以太网段(ES)。
 * 行为：str_to_esi 解析 ESI；no 走 bgp_evpn_local_es_del，
 *   否则 bgp_evpn_local_es_add（vtep 用 router_id，oper_up 依 state 关键字）。
 * 改配置结构：直接操作本地 ES 数据库（bgp_evpn_es），非配置树。
 * 触发 apply/重建：会触发 ES 相关的本地处理与通告。 */
DEFPY_HIDDEN(test_es_add,
      test_es_add_cmd,
      "[no$no] test es NAME$esi_str [state NAME$state_str]",
      NO_STR
      "Test\n"
      "Ethernet-segment\n"
      "Ethernet-Segment Identifier\n"
      "ES link state\n"
      "up|down\n"
)
{
	int ret = 0;
	esi_t esi;
	struct bgp *bgp;
	struct ipaddr vtep_ip = {};
	bool oper_up;

	bgp = bgp_get_evpn();
	if (!bgp) {
		vty_out(vty, "%% EVPN BGP instance not yet created\n");
		return CMD_WARNING;
	}

	if (!str_to_esi(esi_str, &esi)) {
		vty_out(vty, "%% Malformed ESI\n");
		return CMD_WARNING;
	}

	if (no) {
		ret = bgp_evpn_local_es_del(bgp, &esi);
		if (ret == -1) {
			vty_out(vty, "%% Failed to delete ES\n");
			return CMD_WARNING;
		}
	} else {
		if (state_str && !strcmp(state_str, "up"))
			oper_up = true;
		else
			oper_up = false;
		SET_IPADDR_V4(&vtep_ip);
		vtep_ip.ipaddr_v4 = bgp->router_id;

		ret = bgp_evpn_local_es_add(bgp, &esi, vtep_ip, oper_up,
					    EVPN_MH_DF_PREF_MIN, false);
		if (ret == -1) {
			vty_out(vty, "%% Failed to add ES\n");
			return CMD_WARNING;
		}
	}
	return CMD_SUCCESS;
}

/* 【命令（隐藏，仅供测试）】[no] test es <ESI> vni <vni>
 * 语法：手工把某 ES 与某 VNI 关联/解除关联。
 * 行为：str_to_esi 解析 ESI；no 走 bgp_evpn_local_es_evi_del，
 *   否则 bgp_evpn_local_es_evi_add。
 * 改配置结构：直接操作本地 ES-VNI 关联，非配置树。
 * 触发 apply/重建：会触发 ES-EVI 相关处理与通告。 */
DEFPY_HIDDEN(test_es_vni_add,
      test_es_vni_add_cmd,
      "[no$no] test es NAME$esi_str vni (1-16777215)$vni",
      NO_STR
      "Test\n"
      "Ethernet-segment\n"
      "Ethernet-Segment Identifier\n"
      "VNI\n"
      "1-16777215\n"
)
{
	int ret = 0;
	esi_t esi;
	struct bgp *bgp;

	bgp = bgp_get_evpn();
	if (!bgp) {
		vty_out(vty, "%% EVPN BGP instance not yet created\n");
		return CMD_WARNING;
	}

	if (!str_to_esi(esi_str, &esi)) {
		vty_out(vty, "%% Malformed ESI\n");
		return CMD_WARNING;
	}

	if (no) {
		ret = bgp_evpn_local_es_evi_del(bgp, &esi, vni);
		if (ret == -1) {
			vty_out(vty, "%% Failed to deref ES VNI\n");
			return CMD_WARNING;
		}
	} else {
		ret = bgp_evpn_local_es_evi_add(bgp, &esi, vni);
		if (ret == -1) {
			vty_out(vty, "%% Failed to ref ES VNI\n");
			return CMD_WARNING;
		}
	}
	return CMD_SUCCESS;
}

/* 【隐藏别名组】以下 ALIAS_HIDDEN 都是 `show bgp l2vpn evpn ...` 系列的
 * 隐藏别名，令用户可直接输入更短的 `show bgp evpn ...`（同样功能，help 里不显示）。
 * ALIAS_HIDDEN 只是把新命令名映射到已存在的命令函数上，不产生新实现。 */
ALIAS_HIDDEN(show_bgp_l2vpn_evpn_vni, show_bgp_evpn_vni_cmd,
	     "show bgp evpn vni [" CMD_VNI_RANGE "]", SHOW_STR BGP_STR EVPN_HELP_STR
	     "Show VNI\n"
	     "VNI number\n")

ALIAS_HIDDEN(show_bgp_l2vpn_evpn_summary, show_bgp_evpn_summary_cmd,
	     "show bgp evpn summary [json]", SHOW_STR BGP_STR EVPN_HELP_STR
	     "Summary of BGP neighbor status\n" JSON_STR)

ALIAS_HIDDEN(show_bgp_l2vpn_evpn_route, show_bgp_evpn_route_cmd,
	     "show bgp evpn route [detail] [type <macip|2|multicast|3>]",
	     SHOW_STR BGP_STR EVPN_HELP_STR
	     EVPN_RT_HELP_STR
	     "Display Detailed Information\n"
	     EVPN_TYPE_HELP_STR
	     EVPN_TYPE_2_HELP_STR
	     EVPN_TYPE_2_HELP_STR
	     EVPN_TYPE_3_HELP_STR
	     EVPN_TYPE_3_HELP_STR)

ALIAS_HIDDEN(
	show_bgp_l2vpn_evpn_route_rd, show_bgp_evpn_route_rd_cmd,
	"show bgp evpn route rd ASN:NN_OR_IP-ADDRESS:NN [type <macip|2|multicast|3>]",
	SHOW_STR BGP_STR EVPN_HELP_STR
	EVPN_RT_HELP_STR
	EVPN_RT_DIST_HELP_STR
	EVPN_ASN_IP_HELP_STR
	EVPN_TYPE_HELP_STR
	EVPN_TYPE_2_HELP_STR
	EVPN_TYPE_2_HELP_STR
	EVPN_TYPE_3_HELP_STR
	EVPN_TYPE_3_HELP_STR)

ALIAS_HIDDEN(
	show_bgp_l2vpn_evpn_route_rd_macip, show_bgp_evpn_route_rd_macip_cmd,
	"show bgp evpn route rd ASN:NN_OR_IP-ADDRESS:NN mac WORD [ip WORD]",
	SHOW_STR BGP_STR EVPN_HELP_STR
	EVPN_RT_HELP_STR
	EVPN_RT_DIST_HELP_STR
	EVPN_ASN_IP_HELP_STR
	"MAC\n"
	"MAC address (e.g., 00:e0:ec:20:12:62)\n"
	"IP\n"
	"IP address (IPv4 or IPv6)\n")

ALIAS_HIDDEN(
	show_bgp_l2vpn_evpn_route_vni, show_bgp_evpn_route_vni_cmd,
	"show bgp evpn route vni " CMD_VNI_RANGE " [<type <macip|2|multicast|3> | vtep A.B.C.D>]",
	SHOW_STR BGP_STR EVPN_HELP_STR
	EVPN_RT_HELP_STR
	"VXLAN Network Identifier\n"
	"VNI number\n"
	EVPN_TYPE_HELP_STR
	EVPN_TYPE_2_HELP_STR
	EVPN_TYPE_2_HELP_STR
	EVPN_TYPE_3_HELP_STR
	EVPN_TYPE_3_HELP_STR
	"Remote VTEP\n"
	"Remote VTEP IP address\n")

ALIAS_HIDDEN(show_bgp_l2vpn_evpn_route_vni_macip,
	     show_bgp_evpn_route_vni_macip_cmd,
	     "show bgp evpn route vni " CMD_VNI_RANGE " mac WORD [ip WORD]",
	     SHOW_STR BGP_STR EVPN_HELP_STR
	     EVPN_RT_HELP_STR
	     "VXLAN Network Identifier\n"
	     "VNI number\n"
	     "MAC\n"
	     "MAC address (e.g., 00:e0:ec:20:12:62)\n"
	     "IP\n"
	     "IP address (IPv4 or IPv6)\n")

ALIAS_HIDDEN(show_bgp_l2vpn_evpn_route_vni_multicast,
	     show_bgp_evpn_route_vni_multicast_cmd,
	     "show bgp evpn route vni " CMD_VNI_RANGE " multicast A.B.C.D",
	     SHOW_STR BGP_STR EVPN_HELP_STR
	     EVPN_RT_HELP_STR
	     "VXLAN Network Identifier\n"
	     "VNI number\n"
	     EVPN_TYPE_3_HELP_STR
	     "Originating Router IP address\n")

ALIAS_HIDDEN(show_bgp_l2vpn_evpn_route_vni_all, show_bgp_evpn_route_vni_all_cmd,
	     "show bgp evpn route vni all [detail] [vtep A.B.C.D]",
	     SHOW_STR BGP_STR EVPN_HELP_STR
	     EVPN_RT_HELP_STR
	     "VXLAN Network Identifier\n"
	     "All VNIs\n"
	     "Print Detailed Output\n"
	     "Remote VTEP\n"
	     "Remote VTEP IP address\n")

ALIAS_HIDDEN(show_bgp_l2vpn_evpn_import_rt, show_bgp_evpn_import_rt_cmd,
	     "show bgp evpn import-rt",
	     SHOW_STR BGP_STR EVPN_HELP_STR "Show import route target\n")

/* 【命令】[no] flooding <disable|head-end-replication>（BGP EVPN VNI 节点下）
 * 语法：为**当前 VNI** 指定 BUM（广播/未知单播/组播）报文处理方式。
 * 行为：no 则回落到继承全局（VXLAN_FLOOD_INHERIT_GLOBAL）；否则设
 *   disable=VXLAN_FLOOD_DISABLED 或 her=VXLAN_FLOOD_HEAD_END_REPL。
 *   值变化时写 evpn->vxlan_flood_ctrl 并调 bgp_evpn_flood_control_change(bgp)。
 * 改配置结构：struct bgpevpn 的 vxlan_flood_ctrl。
 * 触发 apply/重建：是，触发本 VNI 的洪泛处理调整与相关通告。 */
DEFPY(bgp_evpn_flood_control_vni,
      bgp_evpn_flood_control_vni_cmd,
      "[no$no] flooding <disable$disable|head-end-replication$her>",
      NO_STR
      "Specify handling for BUM packets\n"
      "Do not flood any BUM packets\n"
      "Flood BUM packets using head-end replication\n")
{
	struct bgpevpn *evpn = NULL;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	enum vxlan_flood_control flood_ctrl = VXLAN_FLOOD_INHERIT_GLOBAL;

	if (vty->node == BGP_EVPN_VNI_NODE)
		evpn = VTY_GET_CONTEXT_SUB(bgpevpn);

	if (!bgp)
		return CMD_WARNING;

	if (!evpn)
		return CMD_WARNING;

	if (no) {
		flood_ctrl = VXLAN_FLOOD_INHERIT_GLOBAL;
	} else {
		if (disable)
			flood_ctrl = VXLAN_FLOOD_DISABLED;
		else if (her)
			flood_ctrl = VXLAN_FLOOD_HEAD_END_REPL;
	}

	if (evpn->vxlan_flood_ctrl == flood_ctrl)
		return CMD_SUCCESS;

	evpn->vxlan_flood_ctrl = flood_ctrl;
	bgp_evpn_flood_control_change(bgp);

	return CMD_SUCCESS;
}

/* 【命令】vni <vni>（BGP EVPN 节点下，进入 VNI 子节点）
 * 语法：创建一个 VNI 并进入其配置子节点（BGP_EVPN_VNI_NODE）。
 * 行为：evpn_create_update_vni(bgp, vni) 创建或标记为“已配置”；
 *   成功则 VTY_PUSH_CONTEXT_SUB 把该 bgpevpn 压入上下文。
 * 改配置结构：创建/更新 struct bgpevpn 并标记 configured。
 * 触发 apply/重建：是（首次创建会触发 VNI 初始化）。
 * 注：用 DEFUN_NOSH 注册，不出现在 help 中（配置节点专用）。 */
DEFUN_NOSH (bgp_evpn_vni,
            bgp_evpn_vni_cmd,
            "vni " CMD_VNI_RANGE,
            "VXLAN Network Identifier\n"
            "VNI number\n")
{
	vni_t vni;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct bgpevpn *vpn;

	if (!bgp)
		return CMD_WARNING;

	vni = strtoul(argv[1]->arg, NULL, 10);

	/* Create VNI, or mark as configured. */
	vpn = evpn_create_update_vni(bgp, vni);
	if (!vpn) {
		vty_out(vty, "%% Failed to create VNI \n");
		return CMD_WARNING;
	}

	VTY_PUSH_CONTEXT_SUB(BGP_EVPN_VNI_NODE, vpn);
	return CMD_SUCCESS;
}

/* 【命令】no vni <vni>（BGP EVPN 节点下）
 * 语法：删除一个已配置的 VNI。
 * 行为：bgp_evpn_lookup_vni 查表；不存在报错；用 is_vni_configured 判断是否
 *   为“已配置”，未配置报错；最后 evpn_delete_vni(bgp, vpn)。
 * 改配置结构：删除 struct bgpevpn。
 * 触发 apply/重建：是（拆除 VNI 相关状态与通告）。 */
DEFUN (no_bgp_evpn_vni,
       no_bgp_evpn_vni_cmd,
       "no vni " CMD_VNI_RANGE,
       NO_STR
       "VXLAN Network Identifier\n"
       "VNI number\n")
{
	vni_t vni;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct bgpevpn *vpn;

	if (!bgp)
		return CMD_WARNING;

	vni = strtoul(argv[2]->arg, NULL, 10);

	/* Check if we should disallow. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn) {
		vty_out(vty, "%% Specified VNI does not exist\n");
		return CMD_WARNING;
	}
	if (!is_vni_configured(vpn)) {
		vty_out(vty, "%% Specified VNI is not configured\n");
		return CMD_WARNING;
	}

	evpn_delete_vni(bgp, vpn);
	return CMD_SUCCESS;
}

/* 【命令】exit-vni（BGP EVPN VNI 节点下）
 * 语法：从 VNI 子节点退出，回到 BGP EVPN 父节点。
 * 行为：把 vty->node 由 BGP_EVPN_VNI_NODE 改为 BGP_EVPN_NODE。
 * 改配置结构：无（只切换 CLI 节点）。触发 apply/重建：无。 */
DEFUN_NOSH (exit_vni,
            exit_vni_cmd,
            "exit-vni",
            "Exit from VNI mode\n")
{
	if (vty->node == BGP_EVPN_VNI_NODE)
		vty->node = BGP_EVPN_NODE;
	return CMD_SUCCESS;
}

/* 【命令】rd ASN:NN_OR_IP-ADDRESS:NN（BGP VRF 节点下）
 * 语法：为当前 VRF 配置 EVPN 的 RD（Route Distinguisher）。
 * 行为：str2prefix_rd 解析；若与现有一致则直接返回；否则 evpn_configure_vrf_rd。
 * 改配置结构：VRF 的 EVPN RD（bgp->evpn_info）。
 * 触发 apply/重建：是（会影响该 VRF 的 EVPN 路由区分与重通告）。 */
DEFUN (bgp_evpn_vrf_rd,
       bgp_evpn_vrf_rd_cmd,
       "rd ASN:NN_OR_IP-ADDRESS:NN",
       EVPN_RT_DIST_HELP_STR
       EVPN_ASN_IP_HELP_STR)
{
	int ret;
	struct prefix_rd prd;
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);

	if (!bgp_vrf)
		return CMD_WARNING;

	ret = str2prefix_rd(argv[1]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}

	/* If same as existing value, there is nothing more to do. */
	if (bgp_evpn_vrf_rd_matches_existing(bgp_vrf, &prd))
		return CMD_SUCCESS;

	/* Configure or update the RD. */
	evpn_configure_vrf_rd(bgp_vrf, &prd, argv[1]->arg);
	return CMD_SUCCESS;
}

/* 【命令】no rd ASN:NN_OR_IP-ADDRESS:NN（BGP VRF 节点下）
 * 语法：删除当前 VRF 的 EVPN RD。
 * 行为：str2prefix_rd 解析；is_vrf_rd_configured 确认已配置；
 *   bgp_evpn_vrf_rd_matches_existing 校验给出的 RD 与现有一致；最后 evpn_unconfigure_vrf_rd。
 * 改配置结构：清除 VRF 的 EVPN RD。
 * 触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vrf_rd,
       no_bgp_evpn_vrf_rd_cmd,
       "no rd ASN:NN_OR_IP-ADDRESS:NN",
       NO_STR
       EVPN_RT_DIST_HELP_STR
       EVPN_ASN_IP_HELP_STR)
{
	int ret;
	struct prefix_rd prd;
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);

	if (!bgp_vrf)
		return CMD_WARNING;

	ret = str2prefix_rd(argv[2]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}

	/* Check if we should disallow. */
	if (!is_vrf_rd_configured(bgp_vrf)) {
		vty_out(vty, "%% RD is not configured for this VRF\n");
		return CMD_WARNING;
	}

	if (!bgp_evpn_vrf_rd_matches_existing(bgp_vrf, &prd)) {
		vty_out(vty,
			"%% RD specified does not match configuration for this VRF\n");
		return CMD_WARNING;
	}

	evpn_unconfigure_vrf_rd(bgp_vrf);
	return CMD_SUCCESS;
}

/* 【命令】no rd（无参数形式，BGP VRF 节点下）
 * 语法：不带 RD 值直接删除当前 VRF 的 EVPN RD（便捷写法）。
 * 行为：is_vrf_rd_configured 确认已配置，然后 evpn_unconfigure_vrf_rd。
 * 改配置结构：清除 VRF 的 EVPN RD。触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vrf_rd_without_val,
       no_bgp_evpn_vrf_rd_without_val_cmd,
       "no rd",
       NO_STR
       EVPN_RT_DIST_HELP_STR)
{
	struct bgp *bgp_vrf = VTY_GET_CONTEXT(bgp);

	if (!bgp_vrf)
		return CMD_WARNING;

	/* Check if we should disallow. */
	if (!is_vrf_rd_configured(bgp_vrf)) {
		vty_out(vty, "%% RD is not configured for this VRF\n");
		return CMD_WARNING;
	}

	evpn_unconfigure_vrf_rd(bgp_vrf);
	return CMD_SUCCESS;
}

/* 【命令】rd ASN:NN_OR_IP-ADDRESS:NN（BGP EVPN VNI 节点下）
 * 语法：为**当前 VNI**配置 RD（L2 VNI / MAC-VRF 的 RD）。
 * 行为：EVPN_ENABLED 检查；str2prefix_rd 解析；与现有一致则返回；
 *   否则 evpn_configure_rd(bgp, vpn, &prd, str)。
 * 改配置结构：struct bgpevpn 的 RD。
 * 触发 apply/重建：是（影响该 VNI 的 EVPN 路由重通告）。 */
DEFUN (bgp_evpn_vni_rd,
       bgp_evpn_vni_rd_cmd,
       "rd ASN:NN_OR_IP-ADDRESS:NN",
       EVPN_RT_DIST_HELP_STR
       EVPN_ASN_IP_HELP_STR)
{
	struct prefix_rd prd;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);
	int ret;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	ret = str2prefix_rd(argv[1]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}

	/* If same as existing value, there is nothing more to do. */
	if (bgp_evpn_rd_matches_existing(vpn, &prd))
		return CMD_SUCCESS;

	/* Configure or update the RD. */
	evpn_configure_rd(bgp, vpn, &prd, argv[1]->arg);
	return CMD_SUCCESS;
}

/* 【命令】no rd ASN:NN_OR_IP-ADDRESS:NN（BGP EVPN VNI 节点下）
 * 语法：删除当前 VNI 的 RD（需给出与现有一致的值）。
 * 行为：is_rd_configured 确认已配置；bgp_evpn_rd_matches_existing 校验一致；
 *   evpn_unconfigure_rd(bgp, vpn)。
 * 改配置结构：清除 struct bgpevpn 的 RD。触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vni_rd,
       no_bgp_evpn_vni_rd_cmd,
       "no rd ASN:NN_OR_IP-ADDRESS:NN",
       NO_STR
       EVPN_RT_DIST_HELP_STR
       EVPN_ASN_IP_HELP_STR)
{
	struct prefix_rd prd;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);
	int ret;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	ret = str2prefix_rd(argv[2]->arg, &prd);
	if (!ret) {
		vty_out(vty, "%% Malformed Route Distinguisher\n");
		return CMD_WARNING;
	}

	/* Check if we should disallow. */
	if (!is_rd_configured(vpn)) {
		vty_out(vty, "%% RD is not configured for this VNI\n");
		return CMD_WARNING;
	}

	if (!bgp_evpn_rd_matches_existing(vpn, &prd)) {
		vty_out(vty,
			"%% RD specified does not match configuration for this VNI\n");
		return CMD_WARNING;
	}

	evpn_unconfigure_rd(bgp, vpn);
	return CMD_SUCCESS;
}

/* 【命令】no rd（无参数形式，BGP EVPN VNI 节点下）
 * 语法：不带 RD 值直接删除当前 VNI 的 RD（便捷写法）。
 * 行为：EVPN_ENABLED 检查；is_rd_configured 确认已配置；evpn_unconfigure_rd。
 * 改配置结构：清除 struct bgpevpn 的 RD。触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vni_rd_without_val,
       no_bgp_evpn_vni_rd_without_val_cmd,
       "no rd",
       NO_STR
       EVPN_RT_DIST_HELP_STR)
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	/* Check if we should disallow. */
	if (!is_rd_configured(vpn)) {
		vty_out(vty, "%% RD is not configured for this VNI\n");
		return CMD_WARNING;
	}

	evpn_unconfigure_rd(bgp, vpn);
	return CMD_SUCCESS;
}

/* 【函数】bgp_evpn_rt_matches_existing —— 在 RT 列表 rtl 中查找是否已有
 * ecomtarget（用于 L2VNI 场景）。遍历列表逐个 ecommunity_match，命中返回 true。
 * rtl 的元素是 struct ecommunity。 */
/*
 * Loop over all extended-communities in the route-target list rtl and
 * return 1 if we find ecomtarget
 */
static bool bgp_evpn_rt_matches_existing(struct list *rtl,
					 struct ecommunity *ecomtarget)
{
	struct listnode *node;
	struct ecommunity *ecom;

	for (ALL_LIST_ELEMENTS_RO(rtl, node, ecom)) {
		if (ecommunity_match(ecom, ecomtarget))
			return true;
	}

	return false;
}

/* 【函数】bgp_evpn_vrf_rt_matches_existing —— 上面函数的 L3VNI/VRF 版本。
 * 区别：rtl 的元素是 struct vrf_route_target（内嵌 ecom），故比较 l3rt->ecom。 */
/*
 * L3 RT version of above.
 */
static bool bgp_evpn_vrf_rt_matches_existing(struct list *rtl,
					     struct ecommunity *ecomtarget)
{
	struct listnode *node;
	struct vrf_route_target *l3rt;

	for (ALL_LIST_ELEMENTS_RO(rtl, node, l3rt)) {
		if (ecommunity_match(l3rt->ecom, ecomtarget))
			return true;
	}

	return false;
}

/* 【命令】show bgp vrf <VRFNAME> vni [json]
 * 语法：显示指定 VRF 的 L3VNI 相关信息。
 * 输出：Local-Ip、L3-VNI、Rmac、VNI Filter(prefix-routes-only|none)、
 *   L2-VNI List、Export-RTs、Import-RTs、RD；JSON 时对应到各键。
 * 行为：bgp_lookup_by_name 查 BGP 实例（VRF_DEFAULT_NAME 用 bgp_get_default），
 *   遍历 bgp->l2vnis / vrf_export_rtl / vrf_import_rtl 打印。
 * 改配置结构：无（只读 show）。触发 apply/重建：无。 */
/* display L3VNI related info for a VRF instance */
DEFUN (show_bgp_vrf_l3vni_info,
       show_bgp_vrf_l3vni_info_cmd,
       "show bgp vrf VRFNAME vni [json]",
       SHOW_STR
       BGP_STR
       "show bgp vrf\n"
       "VRF Name\n"
       "L3-VNI\n"
       JSON_STR)
{
	char buf[ETHER_ADDR_STRLEN];
	int idx_vrf = 3;
	const char *name = NULL;
	struct bgp *bgp = NULL;
	struct listnode *node = NULL;
	struct bgpevpn *vpn = NULL;
	struct vrf_route_target *l3rt;
	json_object *json = NULL;
	json_object *json_vnis = NULL;
	json_object *json_export_rts = NULL;
	json_object *json_import_rts = NULL;
	bool uj = use_json(argc, argv);

	if (uj)
		json = json_object_new_object();

	name = argv[idx_vrf]->arg;
	bgp = bgp_lookup_by_name(name);
	if (strmatch(name, VRF_DEFAULT_NAME))
		bgp = bgp_get_default();

	if (!bgp) {
		if (!uj)
			vty_out(vty, "BGP instance for VRF %s not found\n",
				name);
		else {
			json_object_string_add(json, "warning",
					       "BGP instance not found");
			vty_out(vty, "%s\n", json_object_to_json_string(json));
			json_object_free(json);
		}
		return CMD_WARNING;
	}

	if (uj) {
		json_vnis = json_object_new_array();
		json_export_rts = json_object_new_array();
		json_import_rts = json_object_new_array();
	}

	if (!json) {
		vty_out(vty, "BGP VRF: %s\n", name);
		vty_out(vty, "  Local-Ip: %pIA\n", &bgp->originator_ip);
		vty_out(vty, "  L3-VNI: %u\n", bgp->l3vni);
		vty_out(vty, "  Rmac: %s\n",
			prefix_mac2str(&bgp->rmac, buf, sizeof(buf)));
		vty_out(vty, "  VNI Filter: %s\n",
			CHECK_FLAG(bgp->vrf_flags,
				   BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY)
				? "prefix-routes-only"
				: "none");
		vty_out(vty, "  L2-VNI List:\n");
		vty_out(vty, "    ");
		for (ALL_LIST_ELEMENTS_RO(bgp->l2vnis, node, vpn))
			vty_out(vty, "%u  ", vpn->vni);
		vty_out(vty, "\n");
		vty_out(vty, "  Export-RTs:\n");
		vty_out(vty, "    ");
		for (ALL_LIST_ELEMENTS_RO(bgp->vrf_export_rtl, node, l3rt))
			vty_out(vty, "%s  ", ecommunity_str(l3rt->ecom));
		vty_out(vty, "\n");
		vty_out(vty, "  Import-RTs:\n");
		vty_out(vty, "    ");
		for (ALL_LIST_ELEMENTS_RO(bgp->vrf_import_rtl, node, l3rt))
			vty_out(vty, "%s  ", ecommunity_str(l3rt->ecom));
		vty_out(vty, "\n");
		vty_out(vty, "  RD: ");
		vty_out(vty, BGP_RD_AS_FORMAT(bgp->asnotation), &bgp->vrf_prd);
		vty_out(vty, "\n");
	} else {
		json_object_string_add(json, "vrf", name);
		json_object_string_addf(json, "local-ip", "%pIA",
					&bgp->originator_ip);
		json_object_int_add(json, "l3vni", bgp->l3vni);
		json_object_string_add(
			json, "rmac",
			prefix_mac2str(&bgp->rmac, buf, sizeof(buf)));
		json_object_string_add(
			json, "vniFilter",
			CHECK_FLAG(bgp->vrf_flags,
				   BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY)
				? "prefix-routes-only"
				: "none");
		/* list of l2vnis */
		for (ALL_LIST_ELEMENTS_RO(bgp->l2vnis, node, vpn))
			json_object_array_add(json_vnis,
					      json_object_new_int(vpn->vni));
		json_object_object_add(json, "l2vnis", json_vnis);

		/* export rts */
		for (ALL_LIST_ELEMENTS_RO(bgp->vrf_export_rtl, node, l3rt))
			json_object_array_add(
				json_export_rts,
				json_object_new_string(
					ecommunity_str(l3rt->ecom)));
		json_object_object_add(json, "export-rts", json_export_rts);

		/* import rts */
		for (ALL_LIST_ELEMENTS_RO(bgp->vrf_import_rtl, node, l3rt))
			json_object_array_add(
				json_import_rts,
				json_object_new_string(
					ecommunity_str(l3rt->ecom)));
		json_object_object_add(json, "import-rts", json_import_rts);
		json_object_string_addf(json, "rd",
					BGP_RD_AS_FORMAT(bgp->asnotation),
					&bgp->vrf_prd);
	}

	if (uj)
		vty_json(vty, json);
	return CMD_SUCCESS;
}

/* 【函数】add_rt —— 给 VRF 增加一条 RT（import 或 export）。
 *   若对应方向已配置且列表里已有该 RT，返回 -1（表示重复，调用方报错）。
 *   否则调 bgp_evpn_configure_import_rt_for_vrf /
 *   bgp_evpn_configure_export_rt_for_vrf。成功返回 0。
 * is_wildcard 用于 import 的 `*:NN` 通配 RT。 */
static int add_rt(struct bgp *bgp, struct ecommunity *ecom, bool is_import,
		  bool is_wildcard)
{
	/* Do nothing if we already have this route-target */
	if (is_import) {
		if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_RT_CFGD) &&
		    bgp_evpn_vrf_rt_matches_existing(bgp->vrf_import_rtl, ecom))
			return -1;

		bgp_evpn_configure_import_rt_for_vrf(bgp, ecom, is_wildcard);
	} else {
		if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_RT_CFGD) &&
		    bgp_evpn_vrf_rt_matches_existing(bgp->vrf_export_rtl, ecom))
			return -1;

		bgp_evpn_configure_export_rt_for_vrf(bgp, ecom);
	}

	return 0;
}

/* 【函数】del_rt —— 从 VRF 删除一条 RT（import 或 export）。
 *   若列表里没有该 RT，返回 -1（调用方报错）；否则调
 *   bgp_evpn_unconfigure_import_rt_for_vrf / bgp_evpn_unconfigure_export_rt_for_vrf。
 * 成功返回 0。与 add_rt 对称。 */
static int del_rt(struct bgp *bgp, struct ecommunity *ecom, bool is_import)
{
	/* Verify we already have this route-target */
	if (is_import) {
		if (!bgp_evpn_vrf_rt_matches_existing(bgp->vrf_import_rtl,
						      ecom))
			return -1;

		bgp_evpn_unconfigure_import_rt_for_vrf(bgp, ecom);
	} else {
		if (!bgp_evpn_vrf_rt_matches_existing(bgp->vrf_export_rtl,
						      ecom))
			return -1;

		bgp_evpn_unconfigure_export_rt_for_vrf(bgp, ecom);
	}

	return 0;
}

/* 【函数】parse_rtlist —— 解析命令行从 rt_idx 起的 RT 列表并逐条增/删。
 *   对 `*` 开头（通配）特殊处理：临时把首字符换成 '0' 交给 ecommunity 解析，
 *   解析完再还原，并置 is_wildcard=true。
 *   is_add=true 调 add_rt，否则 del_rt；任何一条失败即返回 CMD_WARNING。 */
static int parse_rtlist(struct bgp *bgp, struct vty *vty, int argc,
			struct cmd_token **argv, int rt_idx, bool is_add,
			bool is_import)
{
	int ret = CMD_SUCCESS;
	bool is_wildcard = false;
	struct ecommunity *ecom = NULL;

	for (int i = rt_idx; i < argc; i++) {
		is_wildcard = false;

		/*
		 * Special handling for wildcard '*' here.
		 *
		 * Let's just convert it to 0 here so we dont have to modify
		 * the ecommunity parser.
		 */
		if ((argv[i]->arg)[0] == '*') {
			(argv[i]->arg)[0] = '0';
			is_wildcard = true;
		}

		ecom = ecommunity_str2com(argv[i]->arg, ECOMMUNITY_ROUTE_TARGET,
					  0);

		/* Put it back as was */
		if (is_wildcard)
			(argv[i]->arg)[0] = '*';

		if (!ecom) {
			vty_out(vty, "%% Malformed Route Target list\n");
			ret = CMD_WARNING;
			continue;
		}

		ecommunity_str(ecom);

		if (is_add) {
			if (add_rt(bgp, ecom, is_import, is_wildcard) != 0) {
				vty_out(vty,
					"%% RT specified already configured for this VRF: %s\n",
					argv[i]->arg);
				ecommunity_free(&ecom);
				ret = CMD_WARNING;
			}

		} else {
			if (del_rt(bgp, ecom, is_import) != 0) {
				vty_out(vty,
					"%% RT specified does not match configuration for this VRF: %s\n",
					argv[i]->arg);
				ret = CMD_WARNING;
			}

			ecommunity_free(&ecom);
		}
	}

	return ret;
}

/* 【命令】route-target <both|import|export> RTLIST...（L3VNI VRF 节点下）
 * 语法：为 L3VNI 的 VRF 配置一批 route-target。
 * 行为：解析 both/import/export 得 rt_type；`auto` 不允许走此命令（用 ... auto）；
 *   通配 `*` 只允许用于 import；随后按类型调 parse_rtlist(is_add=true)。
 * 改配置结构：VRF 的 vrf_import_rtl / vrf_export_rtl 及对应 CfgD 标志位。
 * 触发 apply/重建：是（影响 L3VNI 前缀路由的导入/导出）。 */
/* import/export rt for l3vni-vrf */
DEFUN (bgp_evpn_vrf_rt,
       bgp_evpn_vrf_rt_cmd,
       "route-target <both|import|export> RTLIST...",
       "Route Target\n"
       "import and export\n"
       "import\n"
       "export\n"
       "Space separated route target list (A.B.C.D:MN|EF:OPQR|GHJK:MN|*:OPQR|*:MN)\n")
{
	int ret = CMD_SUCCESS;
	int tmp_ret = CMD_SUCCESS;
	int rt_type;
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);

	if (!bgp)
		return CMD_WARNING_CONFIG_FAILED;

	if (!strcmp(argv[1]->arg, "import"))
		rt_type = RT_TYPE_IMPORT;
	else if (!strcmp(argv[1]->arg, "export"))
		rt_type = RT_TYPE_EXPORT;
	else if (!strcmp(argv[1]->arg, "both"))
		rt_type = RT_TYPE_BOTH;
	else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (strmatch(argv[2]->arg, "auto")) {
		vty_out(vty, "%% `auto` cannot be configured via list\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (rt_type != RT_TYPE_IMPORT) {
		for (int i = 2; i < argc; i++) {
			if ((argv[i]->arg)[0] == '*') {
				vty_out(vty,
					"%% Wildcard '*' only applicable for import\n");
				return CMD_WARNING_CONFIG_FAILED;
			}
		}
	}

	/* Add/update the import route-target */
	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_IMPORT)
		tmp_ret = parse_rtlist(bgp, vty, argc, argv, 2, true, true);

	if (ret == CMD_SUCCESS && tmp_ret != CMD_SUCCESS)
		ret = tmp_ret;

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_EXPORT)
		tmp_ret = parse_rtlist(bgp, vty, argc, argv, 2, true, false);

	if (ret == CMD_SUCCESS && tmp_ret != CMD_SUCCESS)
		ret = tmp_ret;

	return ret;
}

/* 【命令】route-target <both|import|export> auto（L3VNI VRF 节点下）
 * 语法：为 L3VNI 的 VRF 自动推导 route-target。
 * 行为：按类型调 bgp_evpn_configure_import_auto_rt_for_vrf /
 *   bgp_evpn_configure_export_auto_rt_for_vrf。
 * 改配置结构：VRF 的 vrf_import_rtl / vrf_export_rtl（自动值）。
 * 触发 apply/重建：是。 */
DEFPY (bgp_evpn_vrf_rt_auto,
       bgp_evpn_vrf_rt_auto_cmd,
       "route-target <both|import|export>$type auto",
       "Route Target\n"
       "import and export\n"
       "import\n"
       "export\n"
       "Automatically derive route target\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	int rt_type;

	if (!bgp)
		return CMD_WARNING_CONFIG_FAILED;

	if (strmatch(type, "import"))
		rt_type = RT_TYPE_IMPORT;
	else if (strmatch(type, "export"))
		rt_type = RT_TYPE_EXPORT;
	else if (strmatch(type, "both"))
		rt_type = RT_TYPE_BOTH;
	else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_IMPORT)
		bgp_evpn_configure_import_auto_rt_for_vrf(bgp);

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_EXPORT)
		bgp_evpn_configure_export_auto_rt_for_vrf(bgp);

	return CMD_SUCCESS;
}

/* 【命令】no route-target <both|import|export> RTLIST...（L3VNI VRF 节点下）
 * 语法：删除 VRF 上的一批 route-target。
 * 行为：解析类型后按类型调 parse_rtlist(is_add=false)。
 * 改配置结构：VRF 的 vrf_import_rtl / vrf_export_rtl。触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vrf_rt,
       no_bgp_evpn_vrf_rt_cmd,
       "no route-target <both|import|export> RTLIST...",
       NO_STR
       "Route Target\n"
       "import and export\n"
       "import\n"
       "export\n"
       "Space separated route target list (A.B.C.D:MN|EF:OPQR|GHJK:MN)\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	int ret = CMD_SUCCESS;
	int tmp_ret = CMD_SUCCESS;
	int rt_type;

	if (!bgp)
		return CMD_WARNING_CONFIG_FAILED;

	if (!strcmp(argv[2]->arg, "import"))
		rt_type = RT_TYPE_IMPORT;
	else if (!strcmp(argv[2]->arg, "export"))
		rt_type = RT_TYPE_EXPORT;
	else if (!strcmp(argv[2]->arg, "both"))
		rt_type = RT_TYPE_BOTH;
	else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (!strcmp(argv[3]->arg, "auto")) {
		vty_out(vty, "%% `auto` cannot be unconfigured via list\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (rt_type == RT_TYPE_IMPORT) {
		if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_RT_CFGD)) {
			vty_out(vty,
				"%% Import RT is not configured for this VRF\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	} else if (rt_type == RT_TYPE_EXPORT) {
		if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_RT_CFGD)) {
			vty_out(vty,
				"%% Export RT is not configured for this VRF\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	} else if (rt_type == RT_TYPE_BOTH) {
		if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_RT_CFGD)
		    && !CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_RT_CFGD)) {
			vty_out(vty,
				"%% Import/Export RT is not configured for this VRF\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	}

	if (rt_type != RT_TYPE_IMPORT) {
		for (int i = 3; i < argc; i++) {
			if ((argv[i]->arg)[0] == '*') {
				vty_out(vty,
					"%% Wildcard '*' only applicable for import\n");
				return CMD_WARNING_CONFIG_FAILED;
			}
		}
	}

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_IMPORT)
		tmp_ret = parse_rtlist(bgp, vty, argc, argv, 3, false, true);

	if (ret == CMD_SUCCESS && tmp_ret != CMD_SUCCESS)
		ret = tmp_ret;

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_EXPORT)
		tmp_ret = parse_rtlist(bgp, vty, argc, argv, 3, false, false);

	if (ret == CMD_SUCCESS && tmp_ret != CMD_SUCCESS)
		ret = tmp_ret;

	return ret;
}

/* 【命令】no route-target <both|import|export> auto（L3VNI VRF 节点下）
 * 语法：取消 VRF 上自动推导的 route-target。
 * 行为：按类型检查 AUTO_RT 的 CfgD 标志，再调
 *   bgp_evpn_unconfigure_import_auto_rt_for_vrf / ..._export_auto_rt_for_vrf。
 * 改配置结构：VRF 的 vrf_import_rtl / vrf_export_rtl。触发 apply/重建：是。 */
DEFPY (no_bgp_evpn_vrf_rt_auto,
       no_bgp_evpn_vrf_rt_auto_cmd,
       "no route-target <both|import|export>$type auto",
       NO_STR
       "Route Target\n"
       "import and export\n"
       "import\n"
       "export\n"
       "Automatically derive route target\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	int rt_type;

	if (!bgp)
		return CMD_WARNING_CONFIG_FAILED;

	if (strmatch(type, "import"))
		rt_type = RT_TYPE_IMPORT;
	else if (strmatch(type, "export"))
		rt_type = RT_TYPE_EXPORT;
	else if (strmatch(type, "both"))
		rt_type = RT_TYPE_BOTH;
	else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (rt_type == RT_TYPE_IMPORT) {
		if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD)) {
			vty_out(vty,
				"%% Import AUTO RT is not configured for this VRF\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	} else if (rt_type == RT_TYPE_EXPORT) {
		if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD)) {
			vty_out(vty,
				"%% Export AUTO RT is not configured for this VRF\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	} else if (rt_type == RT_TYPE_BOTH) {
		if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD) &&
		    !CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD)) {
			vty_out(vty,
				"%% Import/Export AUTO RT is not configured for this VRF\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	}

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_IMPORT)
		bgp_evpn_unconfigure_import_auto_rt_for_vrf(bgp);

	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_EXPORT)
		bgp_evpn_unconfigure_export_auto_rt_for_vrf(bgp);

	return CMD_SUCCESS;
}

/* 【命令】[no] ead-es-frag evi-limit (1-1000)
 * 语法：设置每个 EAD-ES 分片容纳的 EVI 上限（MH 相关）。
 * 行为：写 bgp_mh_info->evi_per_es_frag；no 则恢复默认 BGP_EVPN_MAX_EVI_PER_ES_FRAG。
 * 改配置结构：全局 bgp_mh_info->evi_per_es_frag。
 * 触发 apply/重建：是（影响 EAD-ES 分片通告策略）。 */
DEFPY(bgp_evpn_ead_ess_frag_evi_limit, bgp_evpn_ead_es_frag_evi_limit_cmd,
      "[no$no] ead-es-frag evi-limit (1-1000)$limit",
      NO_STR
      "EAD ES fragment config\n"
      "EVIs per-fragment\n"
      "limit\n")
{
	bgp_mh_info->evi_per_es_frag =
		no ? BGP_EVPN_MAX_EVI_PER_ES_FRAG : limit;

	return CMD_SUCCESS;
}

/* 【命令】ead-es-route-target export RT
 * 语法：为 EAD-ES 路由设置供导出的 route-target（MH 相关）。
 * 行为：EVPN_ENABLED 检查；ecommunity_str2com 解析 RT；若列表里没有则调
 *   bgp_evpn_mh_config_ead_export_rt(bgp, ecomadd, false)（false=添加/更新）。
 * 改配置结构：bgp_mh_info->ead_es_export_rtl。触发 apply/重建：是。 */
DEFUN(bgp_evpn_ead_es_rt, bgp_evpn_ead_es_rt_cmd,
      "ead-es-route-target export RT",
      "EAD ES Route Target\n"
      "export\n"
      "Route target (A.B.C.D:MN|EF:OPQR|GHJK:MN)\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct ecommunity *ecomadd = NULL;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty, "This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	/* Add/update the export route-target */
	ecomadd = ecommunity_str2com(argv[2]->arg, ECOMMUNITY_ROUTE_TARGET, 0);
	if (!ecomadd) {
		vty_out(vty, "%% Malformed Route Target list\n");
		return CMD_WARNING;
	}
	ecommunity_str(ecomadd);

	/* Do nothing if we already have this export route-target */
	if (!bgp_evpn_rt_matches_existing(bgp_mh_info->ead_es_export_rtl,
					  ecomadd))
		bgp_evpn_mh_config_ead_export_rt(bgp, ecomadd, false);
	else
		ecommunity_free(&ecomadd);

	return CMD_SUCCESS;
}

/* 【命令】no ead-es-route-target export RT
 * 语法：删除 EAD-ES 路由导出的 route-target。
 * 行为：EVPN_ENABLED 检查；ecommunity_str2com 解析；列表里必须有（否则报错）；
 *   调 bgp_evpn_mh_config_ead_export_rt(bgp, ecomdel, true)（true=删除）。
 * 改配置结构：bgp_mh_info->ead_es_export_rtl。触发 apply/重建：是。 */
DEFUN(no_bgp_evpn_ead_es_rt, no_bgp_evpn_ead_es_rt_cmd,
      "no ead-es-route-target export RT",
      NO_STR
      "EAD ES Route Target\n"
      "export\n" EVPN_ASN_IP_HELP_STR)
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	struct ecommunity *ecomdel = NULL;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty, "This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	ecomdel = ecommunity_str2com(argv[3]->arg, ECOMMUNITY_ROUTE_TARGET, 0);
	if (!ecomdel) {
		vty_out(vty, "%% Malformed Route Target list\n");
		return CMD_WARNING;
	}
	ecommunity_str(ecomdel);

	if (!bgp_evpn_rt_matches_existing(bgp_mh_info->ead_es_export_rtl,
					  ecomdel)) {
		ecommunity_free(&ecomdel);
		vty_out(vty,
			"%% RT specified does not match EAD-ES RT configuration\n");
		return CMD_WARNING;
	}
	bgp_evpn_mh_config_ead_export_rt(bgp, ecomdel, true);

	ecommunity_free(&ecomdel);
	return CMD_SUCCESS;
}

/* 【命令】route-target <both|import|export> RT（BGP EVPN VNI 节点下）
 * 语法：为当前 VNI 配置一条 route-target（L2 VNI）。
 * 行为：EVPN_ENABLED 检查；解析类型；分别走 import（"both" 时第一条）与
 *   export（"both" 时第二条）分支——各自 ecommunity_str2com 解析，
 *   若 VNI 已配置且列表已有则该 RT 则跳过，否则 evpn_configure_import_rt /
 *   evpn_configure_export_rt。
 * 改配置结构：struct bgpevpn 的 import_rtl / export_rtl 与 VNI_FLAG_IMPRT/EXPRT_CFGD。
 * 触发 apply/重建：是（影响该 VNI 路由的导入/导出）。 */
DEFUN (bgp_evpn_vni_rt,
       bgp_evpn_vni_rt_cmd,
       "route-target <both|import|export> RT",
       "Route Target\n"
       "import and export\n"
       "import\n"
       "export\n"
       "Route target (A.B.C.D:MN|EF:OPQR|GHJK:MN)\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);
	int rt_type;
	struct ecommunity *ecomadd = NULL;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	if (!strcmp(argv[1]->text, "import"))
		rt_type = RT_TYPE_IMPORT;
	else if (!strcmp(argv[1]->text, "export"))
		rt_type = RT_TYPE_EXPORT;
	else if (!strcmp(argv[1]->text, "both"))
		rt_type = RT_TYPE_BOTH;
	else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING;
	}

	/* Add/update the import route-target */
	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_IMPORT) {
		/* Note that first of the two RTs is created for "both" type */
		ecomadd = ecommunity_str2com(argv[2]->arg,
					     ECOMMUNITY_ROUTE_TARGET, 0);
		if (!ecomadd) {
			vty_out(vty, "%% Malformed Route Target list\n");
			return CMD_WARNING;
		}
		ecommunity_str(ecomadd);

		/* Do nothing if we already have this import route-target */
		if (CHECK_FLAG(vpn->flags, VNI_FLAG_IMPRT_CFGD) &&
		    bgp_evpn_rt_matches_existing(vpn->import_rtl, ecomadd))
			ecommunity_free(&ecomadd);
		else
			evpn_configure_import_rt(bgp, vpn, ecomadd);
	}

	/* Add/update the export route-target */
	if (rt_type == RT_TYPE_BOTH || rt_type == RT_TYPE_EXPORT) {
		/* Note that second of the two RTs is created for "both" type */
		ecomadd = ecommunity_str2com(argv[2]->arg,
					     ECOMMUNITY_ROUTE_TARGET, 0);
		if (!ecomadd) {
			vty_out(vty, "%% Malformed Route Target list\n");
			return CMD_WARNING;
		}
		ecommunity_str(ecomadd);

		/* Do nothing if we already have this export route-target */
		if (CHECK_FLAG(vpn->flags, VNI_FLAG_EXPRT_CFGD) &&
		    bgp_evpn_rt_matches_existing(vpn->export_rtl, ecomadd))
			ecommunity_free(&ecomadd);
		else
			evpn_configure_export_rt(bgp, vpn, ecomadd);
	}

	return CMD_SUCCESS;
}

/* 【命令】no route-target <both|import|export> RT（BGP EVPN VNI 节点下）
 * 语法：删除当前 VNI 上的一条 route-target。
 * 行为：EVPN_ENABLED 检查；解析类型并确认对应方向已配置；解析 RT；
 *   按 import/export/both 分别校验列表里存在后调 evpn_unconfigure_import_rt /
 *   evpn_unconfigure_export_rt（both 时任一方向命中即可）。
 * 改配置结构：struct bgpevpn 的 import_rtl / export_rtl。触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vni_rt,
       no_bgp_evpn_vni_rt_cmd,
       "no route-target <both|import|export> RT",
       NO_STR
       "Route Target\n"
       "import and export\n"
       "import\n"
       "export\n"
       EVPN_ASN_IP_HELP_STR)
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);
	int rt_type, found_ecomdel;
	struct ecommunity *ecomdel = NULL;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	if (!strcmp(argv[2]->text, "import"))
		rt_type = RT_TYPE_IMPORT;
	else if (!strcmp(argv[2]->text, "export"))
		rt_type = RT_TYPE_EXPORT;
	else if (!strcmp(argv[2]->text, "both"))
		rt_type = RT_TYPE_BOTH;
	else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING;
	}

	/* The user did "no route-target import", check to see if there are any
	 * import route-targets configured. */
	if (rt_type == RT_TYPE_IMPORT) {
		if (!is_import_rt_configured(vpn)) {
			vty_out(vty,
				"%% Import RT is not configured for this VNI\n");
			return CMD_WARNING;
		}
	} else if (rt_type == RT_TYPE_EXPORT) {
		if (!is_export_rt_configured(vpn)) {
			vty_out(vty,
				"%% Export RT is not configured for this VNI\n");
			return CMD_WARNING;
		}
	} else if (rt_type == RT_TYPE_BOTH) {
		if (!is_import_rt_configured(vpn)
		    && !is_export_rt_configured(vpn)) {
			vty_out(vty,
				"%% Import/Export RT is not configured for this VNI\n");
			return CMD_WARNING;
		}
	}

	ecomdel = ecommunity_str2com(argv[3]->arg, ECOMMUNITY_ROUTE_TARGET, 0);
	if (!ecomdel) {
		vty_out(vty, "%% Malformed Route Target list\n");
		return CMD_WARNING;
	}
	ecommunity_str(ecomdel);

	if (rt_type == RT_TYPE_IMPORT) {
		if (!bgp_evpn_rt_matches_existing(vpn->import_rtl, ecomdel)) {
			ecommunity_free(&ecomdel);
			vty_out(vty,
				"%% RT specified does not match configuration for this VNI\n");
			return CMD_WARNING;
		}
		evpn_unconfigure_import_rt(bgp, vpn, ecomdel);
	} else if (rt_type == RT_TYPE_EXPORT) {
		if (!bgp_evpn_rt_matches_existing(vpn->export_rtl, ecomdel)) {
			ecommunity_free(&ecomdel);
			vty_out(vty,
				"%% RT specified does not match configuration for this VNI\n");
			return CMD_WARNING;
		}
		evpn_unconfigure_export_rt(bgp, vpn, ecomdel);
	} else if (rt_type == RT_TYPE_BOTH) {
		found_ecomdel = 0;

		if (bgp_evpn_rt_matches_existing(vpn->import_rtl, ecomdel)) {
			evpn_unconfigure_import_rt(bgp, vpn, ecomdel);
			found_ecomdel = 1;
		}

		if (bgp_evpn_rt_matches_existing(vpn->export_rtl, ecomdel)) {
			evpn_unconfigure_export_rt(bgp, vpn, ecomdel);
			found_ecomdel = 1;
		}

		if (!found_ecomdel) {
			ecommunity_free(&ecomdel);
			vty_out(vty,
				"%% RT specified does not match configuration for this VNI\n");
			return CMD_WARNING;
		}
	}

	ecommunity_free(&ecomdel);
	return CMD_SUCCESS;
}

/* 【命令】no route-target <import|export>（无 RT 值，BGP EVPN VNI 节点下）
 * 语法：不带具体 RT 值，直接清空当前 VNI 的某方向 RT 列表（便捷写法）。
 * 行为：EVPN_ENABLED 检查；解析 import/export 类型并确认已配置；
 *   随后清空对应方向的 RT 列表（详见函数后半）。
 * 改配置结构：struct bgpevpn 的 import_rtl / export_rtl。触发 apply/重建：是。 */
DEFUN (no_bgp_evpn_vni_rt_without_val,
       no_bgp_evpn_vni_rt_without_val_cmd,
       "no route-target <import|export>",
       NO_STR
       "Route Target\n"
       "import\n"
       "export\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	VTY_DECLVAR_CONTEXT_SUB(bgpevpn, vpn);
	int rt_type;

	if (!bgp)
		return CMD_WARNING;

	if (!EVPN_ENABLED(bgp)) {
		vty_out(vty,
			"This command is only supported under EVPN VRF\n");
		return CMD_WARNING;
	}

	if (!strcmp(argv[2]->text, "import")) {
		rt_type = RT_TYPE_IMPORT;
	} else if (!strcmp(argv[2]->text, "export")) {
		rt_type = RT_TYPE_EXPORT;
	} else {
		vty_out(vty, "%% Invalid Route Target type\n");
		return CMD_WARNING;
	}

	/* Check if we should disallow. */
	if (rt_type == RT_TYPE_IMPORT) {
		if (!is_import_rt_configured(vpn)) {
			vty_out(vty,
				"%% Import RT is not configured for this VNI\n");
			return CMD_WARNING;
		}
	} else {
		if (!is_export_rt_configured(vpn)) {
			vty_out(vty,
				"%% Export RT is not configured for this VNI\n");
			return CMD_WARNING;
		}
	}

	/* Unconfigure the RT. */
	if (rt_type == RT_TYPE_IMPORT)
		evpn_unconfigure_import_rt(bgp, vpn, NULL);
	else
		evpn_unconfigure_export_rt(bgp, vpn, NULL);
	return CMD_SUCCESS;
}

/* 【函数】vni_cmp —— list_sort 的比较函数：按 VNI 从大到小排序。
 * 注意返回值是 secnd->vni - first->vni（降序），用于 show running-config
 * 时让 VNI 输出顺序稳定、可复现。 */
static int vni_cmp(const void **a, const void **b)
{
	const struct bgpevpn *first = *a;
	const struct bgpevpn *secnd = *b;

	return secnd->vni - first->vni;
}

/* 【函数】bgp_config_write_evpn_info —— 把某 bgp 实例的 EVPN 全局配置写进
 * vty（供 `show running-config` 使用）。由 bgp_config_write 类入口调用。
 * 顺序要点：
 *   1) advertise-all-vni；
 *   2) 若 vnihash 非空，转 list 并按 vni_cmp 排序后逐条 write_vni_config
 *      （各 VNI 自身的子配置由 write_vni_config 负责）；
 *   3) 其余全局开关：autort rfc8365-compatible、advertise-default-gw、
 *      advertise-svi-ip、mac-vrf soo、enable-resolve-overlay-index、
 *      ead-es-frag evi-limit、use-es-l3nhg、disable-ead-evi-rx/tx、
 *      dup-addr-detection（含 max-moves/time/freeze）、flooding、
 *      advertise ipv4/ipv6 unicast [gateway-ip] [route-map]、EAD-ES 导出 RT、
 *      default-originate ipv4/ipv6；
 *   4) VRF 专属：advertise-pip(ip/mac)、rd、import/export route-target(含 auto)。
 * 说明：此函数只“输出”配置文本，不改变任何状态。
 */
/*
 * Output EVPN configuration information.
 */
void bgp_config_write_evpn_info(struct vty *vty, struct bgp *bgp, afi_t afi,
				safi_t safi)
{
	if (bgp->advertise_all_vni)
		vty_out(vty, "  advertise-all-vni\n");

	if (hashcount(bgp->vnihash)) {
		struct list *vnilist = hash_to_list(bgp->vnihash);
		struct listnode *ln;
		struct bgpevpn *data;

		list_sort(vnilist, vni_cmp);
		for (ALL_LIST_ELEMENTS_RO(vnilist, ln, data))
			write_vni_config(vty, data);

		list_delete(&vnilist);
	}

	if (bgp->advertise_autort_rfc8365)
		vty_out(vty, "  autort rfc8365-compatible\n");

	if (bgp->advertise_gw_macip)
		vty_out(vty, "  advertise-default-gw\n");

	if (bgp->evpn_info->advertise_svi_macip)
		vty_out(vty, "  advertise-svi-ip\n");

	if (bgp->evpn_info->soo) {
		char *ecom_str;

		ecom_str = ecommunity_ecom2str(bgp->evpn_info->soo,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
		vty_out(vty, "  mac-vrf soo %s\n", ecom_str);
		ecommunity_strfree(&ecom_str);
	}

	if (bgp->resolve_overlay_index)
		vty_out(vty, "  enable-resolve-overlay-index\n");

	if (bgp_mh_info->evi_per_es_frag != BGP_EVPN_MAX_EVI_PER_ES_FRAG)
		vty_out(vty, "  ead-es-frag evi-limit %u\n",
			bgp_mh_info->evi_per_es_frag);

	if (bgp_mh_info->host_routes_use_l3nhg !=
			BGP_EVPN_MH_USE_ES_L3NHG_DEF) {
		if (bgp_mh_info->host_routes_use_l3nhg)
			vty_out(vty, "  use-es-l3nhg\n");
		else
			vty_out(vty, "  no use-es-l3nhg\n");
	}

	if (bgp_mh_info->enable_ead_evi_rx != BGP_EVPN_MH_EAD_EVI_RX_DEF) {
		if (bgp_mh_info->enable_ead_evi_rx)
			vty_out(vty, "  no disable-ead-evi-rx\n");
		else
			vty_out(vty, "  disable-ead-evi-rx\n");
	}

	if (bgp_mh_info->enable_ead_evi_tx != BGP_EVPN_MH_EAD_EVI_TX_DEF) {
		if (bgp_mh_info->enable_ead_evi_tx)
			vty_out(vty, "  no disable-ead-evi-tx\n");
		else
			vty_out(vty, "  disable-ead-evi-tx\n");
	}

	if (!bgp->evpn_info->dup_addr_detect)
		vty_out(vty, "  no dup-addr-detection\n");

	if (bgp->evpn_info->dad_max_moves !=
		EVPN_DAD_DEFAULT_MAX_MOVES ||
		bgp->evpn_info->dad_time != EVPN_DAD_DEFAULT_TIME)
		vty_out(vty, "  dup-addr-detection max-moves %u time %u\n",
			bgp->evpn_info->dad_max_moves,
			bgp->evpn_info->dad_time);

	if (bgp->evpn_info->dad_freeze) {
		if (bgp->evpn_info->dad_freeze_time)
			vty_out(vty,
				"  dup-addr-detection freeze %u\n",
				bgp->evpn_info->dad_freeze_time);
		else
			vty_out(vty,
				"  dup-addr-detection freeze permanent\n");
	}

	if (bgp->vxlan_flood_ctrl == VXLAN_FLOOD_DISABLED)
		vty_out(vty, "  flooding disable\n");

	if (CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
		       BGP_L2VPN_EVPN_ADV_IPV4_UNICAST)) {
		if (bgp->adv_cmd_rmap[AFI_IP][SAFI_UNICAST].name)
			vty_out(vty, "  advertise ipv4 unicast route-map %s\n",
				bgp->adv_cmd_rmap[AFI_IP][SAFI_UNICAST].name);
		else
			vty_out(vty,
				"  advertise ipv4 unicast\n");
	} else if (CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
		   BGP_L2VPN_EVPN_ADV_IPV4_UNICAST_GW_IP)) {
		if (bgp->adv_cmd_rmap[AFI_IP][SAFI_UNICAST].name)
			vty_out(vty,
				"  advertise ipv4 unicast gateway-ip route-map %s\n",
				bgp->adv_cmd_rmap[AFI_IP][SAFI_UNICAST].name);
		else
			vty_out(vty, "  advertise ipv4 unicast gateway-ip\n");
	}

	/* EAD ES export route-target */
	if (listcount(bgp_mh_info->ead_es_export_rtl)) {
		struct ecommunity *ecom;
		char *ecom_str;
		struct listnode *node;

		for (ALL_LIST_ELEMENTS_RO(bgp_mh_info->ead_es_export_rtl, node,
					  ecom)) {

			ecom_str = ecommunity_ecom2str(
				ecom, ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			vty_out(vty, "  ead-es-route-target export %s\n",
				ecom_str);
			XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);
		}
	}

	if (CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
		       BGP_L2VPN_EVPN_ADV_IPV6_UNICAST)) {
		if (bgp->adv_cmd_rmap[AFI_IP6][SAFI_UNICAST].name)
			vty_out(vty,
				"  advertise ipv6 unicast route-map %s\n",
				bgp->adv_cmd_rmap[AFI_IP6][SAFI_UNICAST].name);
		else
			vty_out(vty,
				"  advertise ipv6 unicast\n");
	} else if (CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
			      BGP_L2VPN_EVPN_ADV_IPV6_UNICAST_GW_IP)) {
		if (bgp->adv_cmd_rmap[AFI_IP6][SAFI_UNICAST].name)
			vty_out(vty,
				"  advertise ipv6 unicast gateway-ip route-map %s\n",
				bgp->adv_cmd_rmap[AFI_IP6][SAFI_UNICAST].name);
		else
			vty_out(vty, "  advertise ipv6 unicast gateway-ip\n");
	}

	if (CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
		       BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4))
		vty_out(vty, "  default-originate ipv4\n");

	if (CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
		       BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV6))
		vty_out(vty, "  default-originate ipv6\n");

	if (bgp->inst_type == BGP_INSTANCE_TYPE_VRF) {
		if (!bgp->evpn_info->advertise_pip)
			vty_out(vty, "  no advertise-pip\n");
		if (bgp->evpn_info->advertise_pip) {
			if (bgp->evpn_info->pip_ip_static.ipaddr_v4.s_addr != INADDR_ANY) {
				vty_out(vty, "  advertise-pip ip %pIA",
					&bgp->evpn_info->pip_ip_static);
				if (!is_zero_mac(&(
					    bgp->evpn_info->pip_rmac_static))) {
					char buf[ETHER_ADDR_STRLEN];

					vty_out(vty, " mac %s",
						prefix_mac2str(
							&bgp->evpn_info
								 ->pip_rmac,
							buf, sizeof(buf)));
				}
				vty_out(vty, "\n");
			}
		}
	}
	if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_RD_CFGD))
		vty_out(vty, "  rd %s\n", bgp->vrf_prd_pretty);

	/* import route-target */
	if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_RT_CFGD)) {
		char *ecom_str;
		struct listnode *node, *nnode;
		struct vrf_route_target *l3rt;

		for (ALL_LIST_ELEMENTS(bgp->vrf_import_rtl, node, nnode,
				       l3rt)) {

			if (CHECK_FLAG(l3rt->flags, BGP_VRF_RT_AUTO))
				continue;

			ecom_str = ecommunity_ecom2str(
				l3rt->ecom, ECOMMUNITY_FORMAT_ROUTE_MAP, 0);

			if (CHECK_FLAG(l3rt->flags, BGP_VRF_RT_WILD)) {
				char *vni_str = NULL;

				vni_str = strchr(ecom_str, ':');
				if (!vni_str) {
					XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);
					continue;
				}

				/* Move pointer to vni */
				vni_str += 1;

				vty_out(vty, "  route-target import *:%s\n",
					vni_str);

			} else
				vty_out(vty, "  route-target import %s\n",
					ecom_str);

			XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);
		}
	}

	/* import route-target auto */
	if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD))
		vty_out(vty, "  route-target import auto\n");

	/* export route-target */
	if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_RT_CFGD)) {
		char *ecom_str;
		struct listnode *node, *nnode;
		struct vrf_route_target *l3rt;

		for (ALL_LIST_ELEMENTS(bgp->vrf_export_rtl, node, nnode,
				       l3rt)) {

			if (CHECK_FLAG(l3rt->flags, BGP_VRF_RT_AUTO))
				continue;

			ecom_str = ecommunity_ecom2str(
				l3rt->ecom, ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
			vty_out(vty, "  route-target export %s\n", ecom_str);
			XFREE(MTYPE_ECOMMUNITY_STR, ecom_str);
		}
	}

	/* export route-target auto */
	if (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD))
		vty_out(vty, "  route-target export auto\n");
}

/* 【函数】bgp_ethernetvpn_init —— EVPN 的 CLI 命令注册入口。
 * bgpd 启动时调用一次，用 install_element(节点, &命令) 把上面 DEFUN/DEFPY
 * 注册的命令挂到对应 CLI 节点，例如 VIEW_NODE、BGP_EVPN_NODE、
 * BGP_EVPN_VNI_NODE、BGP_NODE、BGP_VRF_NODE 等。
 * 注意：install_element 的参数（节点与命令变量）**绝不可改动**，否则命令
 * 会注册到错误节点或消失；本注释只在其上方补充说明，不触碰这些调用。
 * 分块：show 命令(VIEW_NODE) / EVPN AF 配置(BGP_EVPN_NODE) / VNI 子节点
 * (BGP_EVPN_VNI_NODE) / 测试命令 / `show bgp l2vpn evpn` 与
 * `show bgp vni` / `show bgp evpn` 系列。 */
void bgp_ethernetvpn_init(void)
{
	install_element(VIEW_NODE, &show_ip_bgp_l2vpn_evpn_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_l2vpn_evpn_rd_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_l2vpn_evpn_all_tags_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_l2vpn_evpn_rd_tags_cmd);
	install_element(VIEW_NODE,
			&show_ip_bgp_l2vpn_evpn_neighbor_routes_cmd);
	install_element(VIEW_NODE,
			&show_ip_bgp_l2vpn_evpn_rd_neighbor_routes_cmd);
	install_element(
		VIEW_NODE,
		&show_ip_bgp_l2vpn_evpn_neighbor_advertised_routes_cmd);
	install_element(
		VIEW_NODE,
		&show_ip_bgp_l2vpn_evpn_rd_neighbor_advertised_routes_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_evpn_rd_overlay_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_l2vpn_evpn_all_overlay_cmd);
	install_element(BGP_EVPN_NODE, &no_evpnrt5_network_cmd);
	install_element(BGP_EVPN_NODE, &evpnrt5_network_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_advertise_all_vni_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_advertise_all_vni_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_advertise_autort_rfc8365_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_advertise_autort_rfc8365_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_advertise_default_gw_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_advertise_default_gw_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_advertise_svi_ip_cmd);
	install_element(BGP_EVPN_NODE, &macvrf_soo_global_cmd);
	install_element(BGP_EVPN_NODE, &no_macvrf_soo_global_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_advertise_type5_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_advertise_type5_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_default_originate_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_default_originate_cmd);
	install_element(BGP_EVPN_NODE, &dup_addr_detection_cmd);
	install_element(BGP_EVPN_NODE, &dup_addr_detection_auto_recovery_cmd);
	install_element(BGP_EVPN_NODE, &no_dup_addr_detection_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_flood_control_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_advertise_pip_ip_mac_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_use_es_l3nhg_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_ead_evi_rx_disable_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_ead_evi_tx_disable_cmd);
	install_element(BGP_EVPN_NODE,
			&bgp_evpn_enable_resolve_overlay_index_cmd);

	/* 【注册】EVPN 测试命令（test_es_add / test_es_vni_add，隐藏命令）。 */
	/* test commands */
	install_element(BGP_EVPN_NODE, &test_es_add_cmd);
	install_element(BGP_EVPN_NODE, &test_es_vni_add_cmd);

	/* 【注册】`show bgp l2vpn evpn ...` 系列 show 命令（ES / VNI / summary /
	 * route / import-rt 等），挂在 VIEW_NODE。 */
	/* "show bgp l2vpn evpn" commands. */
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_es_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_es_evi_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_es_vrf_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_nh_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_vni_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_vni_remote_ip_hash_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_vni_svi_hash_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_summary_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_rd_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_rd_macip_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_esi_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_vni_cmd);
	install_element(VIEW_NODE,
			&show_bgp_l2vpn_evpn_route_vni_multicast_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_vni_macip_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_route_vni_all_cmd);
	install_element(VIEW_NODE,
			&show_bgp_l2vpn_evpn_route_mac_ip_evi_es_cmd);
	install_element(VIEW_NODE,
			&show_bgp_l2vpn_evpn_route_mac_ip_global_es_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_import_rt_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_vrf_import_rt_cmd);

	/* 【注册】`show bgp vni ...` 系列 show 命令（all / type / mac / ip 等），
	 * 挂在 VIEW_NODE。 */
	/* "show bgp vni" commands. */
	install_element(VIEW_NODE, &show_bgp_vni_all_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_all_ead_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_all_macip_mac_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_all_macip_ip_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_all_imet_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_ead_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_macip_mac_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_macip_ip_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_imet_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_macip_mac_addr_cmd);
	install_element(VIEW_NODE, &show_bgp_vni_macip_ip_addr_cmd);

	/* 【注册】`show bgp evpn ...` 系列隐藏别名 show 命令，挂在 VIEW_NODE。 */
	/* "show bgp evpn" commands. */
	install_element(VIEW_NODE, &show_bgp_evpn_vni_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_summary_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_rd_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_rd_macip_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_vni_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_vni_multicast_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_vni_macip_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_route_vni_all_cmd);
	install_element(VIEW_NODE, &show_bgp_evpn_import_rt_cmd);
	install_element(VIEW_NODE, &show_bgp_vrf_l3vni_info_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_com_cmd);
	install_element(VIEW_NODE, &show_bgp_l2vpn_evpn_rt_cmd);

	install_element(BGP_EVPN_NODE, &bgp_evpn_vni_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_vni_cmd);
	install_element(BGP_EVPN_VNI_NODE, &exit_vni_cmd);
	install_element(BGP_EVPN_VNI_NODE, &bgp_evpn_flood_control_vni_cmd);
	install_element(BGP_EVPN_VNI_NODE, &bgp_evpn_vni_rd_cmd);
	install_element(BGP_EVPN_VNI_NODE, &no_bgp_evpn_vni_rd_cmd);
	install_element(BGP_EVPN_VNI_NODE, &no_bgp_evpn_vni_rd_without_val_cmd);
	install_element(BGP_EVPN_VNI_NODE, &bgp_evpn_vni_rt_cmd);
	install_element(BGP_EVPN_VNI_NODE, &no_bgp_evpn_vni_rt_cmd);
	install_element(BGP_EVPN_VNI_NODE, &no_bgp_evpn_vni_rt_without_val_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_vrf_rd_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_vrf_rd_cmd);
	install_element(BGP_NODE, &no_bgp_evpn_vrf_rd_without_val_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_vrf_rt_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_vrf_rt_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_vrf_rt_auto_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_vrf_rt_auto_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_ead_es_rt_cmd);
	install_element(BGP_EVPN_NODE, &no_bgp_evpn_ead_es_rt_cmd);
	install_element(BGP_EVPN_NODE, &bgp_evpn_ead_es_frag_evi_limit_cmd);
	install_element(BGP_EVPN_VNI_NODE, &bgp_evpn_advertise_svi_ip_vni_cmd);
	install_element(BGP_EVPN_VNI_NODE,
			&bgp_evpn_advertise_default_gw_vni_cmd);
	install_element(BGP_EVPN_VNI_NODE,
			&no_bgp_evpn_advertise_default_gw_vni_cmd);
	install_element(BGP_EVPN_VNI_NODE, &bgp_evpn_advertise_vni_subnet_cmd);
	install_element(BGP_EVPN_VNI_NODE,
			&no_bgp_evpn_advertise_vni_subnet_cmd);
}
