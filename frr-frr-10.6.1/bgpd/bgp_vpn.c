// SPDX-License-Identifier: GPL-2.0-or-later
/* VPN Related functions
 * Copyright (C) 2017 6WIND
 *
 * This file is part of FRRouting
 */

/* ============================================================================
 * 【文件导读】bgp_vpn.c —— `show ... vpn` 类命令的 VPN 路由输出实现
 * ============================================================================
 * 【本文件解决什么问题】普通 IPv4/IPv6 表的 `show bgp ... neighbors X routes`
 * 由 bgp_route.c 里的通用函数输出；但 VPN(MPLS-VPN) 路由是**两层结构**——
 * “RD 表”里再挂“前缀表”，遍历方式不同，无法复用，于是有本文件唯一的函数
 * show_adj_route_vpn() 专门输出。
 *
 * 【两层层级（读代码关键）】外层遍历 bgp->rib[afi][safi] 的每个 dest，它的 info
 * 其实是一张子表（该 RD 对应的前缀表）；用 bgp_dest_get_bgp_table_info 取出后内层
 * 再遍历前缀。RD 由外层 dest 前缀的前 8 字节给出，用 decode_rd_type + decode_rd_*
 * 解码后打印。
 *
 * 【如何判断“某邻居是否收到该前缀”】内层对每个前缀遍历 adj_out 红黑树（出向通告
 * 记录），用 SUBGRP_FOREACH_PEER 找到与目标 peer 匹配、且带 attr 的那条 adj，取其
 * attr 打印。找不到就跳过——所以输出的是“该邻居确实被通告过”的路由。
 *
 * 【输出两种形态】use_json=false 走 vty_out 文本表格（表头一次、RD 头每个 RD 一次）；
 * use_json=true 组 json 对象，最后 vty_json 输出。prd 非空时按 RD 过滤（只输出匹配的
 * 那个 RD）。
 * ============================================================================
 */

#include <zebra.h>
#include "command.h"
#include "prefix.h"
#include "lib/json.h"
#include "lib/printfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_vpn.h"
#include "bgpd/bgp_updgrp.h"

/* 【函数】show_adj_route_vpn —— 输出指定邻居在 VPN 表里的路由（文本或 JSON）。
 * 流程：
 *   1) 取默认 BGP 实例；无则输出提示 / 空 JSON 并返回 CMD_WARNING；
 *   2) 外层遍历 rib[afi][safi] 的每个 dest（每个就是一个 RD 表）：
 *      - prd 非空时用 memcmp 比较该 dest 前缀前 8 字节与 prd->val，不匹配则跳过；
 *      - bgp_dest_get_bgp_table_info 取子表，空则跳过；
 *      - 打印表头（仅第一次，header 标志）与 RD 头（每个 RD 第一次，rd_header）；
 *   3) 内层遍历子表每个前缀，在 adj_out 里找目标 peer 的 adj（取其 attr）；无路径
 *      信息或无 attr 则跳过；否则调 route_vty_out_tmp 打印该条路由；
 *   4) use_json 时把每个 RD 的前缀集合挂到 advertisedRoutes 下，最后统一输出。
 * 返回 CMD_SUCCESS / CMD_WARNING。 */
int show_adj_route_vpn(struct vty *vty, struct peer *peer,
		       struct prefix_rd *prd, afi_t afi, safi_t safi,
		       bool use_json)
{
	struct bgp *bgp;
	struct bgp_table *table;
	struct bgp_dest *dest;
	struct bgp_dest *rm;
	int rd_header;
	int header = 1;
	json_object *json = NULL;
	json_object *json_adv = NULL;
	json_object *json_routes = NULL;
	char rd_str[BUFSIZ];
	unsigned long output_count = 0;

	bgp = bgp_get_default();
	if (bgp == NULL) {
		if (!use_json)
			vty_out(vty, "No BGP process is configured\n");
		else
			vty_out(vty, "{}\n");
		return CMD_WARNING;
	}

	if (use_json) {
		json = json_object_new_object();
		json_adv = json_object_new_object();
	}

	/* 外层：遍历每个 RD 子表——dest 的 info 本身就是该 RD 对应的前缀表。 */
	for (dest = bgp_table_top(bgp->rib[afi][safi]); dest;
	     dest = bgp_route_next(dest)) {
		const struct prefix *dest_p = bgp_dest_get_prefix(dest);

		if (prd && memcmp(dest_p->u.val, prd->val, 8) != 0)
			continue;

		table = bgp_dest_get_bgp_table_info(dest);
		if (table == NULL)
			continue;

		/*
		 * Initialize variables for each RD
		 * All prefixes under an RD is aggregated within "json_routes"
		 */
		rd_header = 1;
		memset(rd_str, 0, sizeof(rd_str));
		json_routes = NULL;

		/* 内层：遍历该 RD 下的每个前缀节点。 */
		for (rm = bgp_table_top(table); rm; rm = bgp_route_next(rm)) {
			struct bgp_adj_out *adj = NULL;
			struct attr *attr = NULL;
			struct peer_af *paf = NULL;

			/* 在本前缀的出向通告红黑树里找目标 peer 的那条记录，取其 attr
			 * 用于展示；SUBGRP_FOREACH_PEER 遍历该 adj 所属子组的成员 peer。 */
			RB_FOREACH (adj, bgp_adj_out_rb, &rm->adj_out)
				SUBGRP_FOREACH_PEER (adj->subgroup, paf) {
					if (paf->peer != peer || !adj->attr)
						continue;

					attr = adj->attr;
					break;
			}

			if (bgp_dest_get_bgp_path_info(rm) == NULL)
				continue;

			if (!attr)
				continue;

			if (header) {
				if (use_json) {
					json_object_int_add(
						json, "bgpTableVersion", 0);
					json_object_string_addf(
						json, "bgpLocalRouterId",
						"%pI4", &bgp->router_id);
					json_object_int_add(
						json,
						"defaultLocPrf",
						bgp->default_local_pref);
					json_object_int_add(
						json, "localAS",
						bgp->as);
				} else {
					vty_out(vty,
						"BGP table version is 0, local router ID is %pI4\n",
						&bgp->router_id);
					vty_out(vty, "Default local pref %u, ",
						bgp->default_local_pref);
					vty_out(vty, "local AS %u\n", bgp->as);
					vty_out(vty,
						"Status codes: s suppressed, d damped, h history, * valid, > best, i - internal\n");
					vty_out(vty,
						"Origin codes: i - IGP, e - EGP, ? - incomplete\n\n");
					vty_out(vty, V4_HEADER);
				}
				header = 0;
			}

			/* 每个 RD 只打印一次 RD 头：解码 RD 类型与数值后输出。
			 * 注意 RD 前 8 字节即外层 dest 前缀的 u.val，解码时用 pnt+2 跳过类型。 */
			if (rd_header) {
				uint16_t type;
				struct rd_as rd_as = {0};
				struct rd_ip rd_ip = {0};
#ifdef ENABLE_BGP_VNC
				struct rd_vnc_eth rd_vnc_eth = {0};
#endif
				const uint8_t *pnt;

				pnt = dest_p->u.val;

				/* Decode RD type. */
				type = decode_rd_type(pnt);
				/* Decode RD value. */
				if (type == RD_TYPE_AS)
					decode_rd_as(pnt + 2, &rd_as);
				else if (type == RD_TYPE_AS4)
					decode_rd_as4(pnt + 2, &rd_as);
				else if (type == RD_TYPE_IP)
					decode_rd_ip(pnt + 2, &rd_ip);
#ifdef ENABLE_BGP_VNC
				else if (type == RD_TYPE_VNC_ETH)
					decode_rd_vnc_eth(pnt, &rd_vnc_eth);
#endif
				/* 【JSON 模式】为当前 RD 新建子对象 json_routes：
				 * AS/AS4 号用 "as:val" 拼串，IP 用 %pI4:val 拼串，
				 * 再以 "rd" 为键写入该子对象。 */
				if (use_json) {
					json_routes = json_object_new_object();

					if (type == RD_TYPE_AS
					    || type == RD_TYPE_AS4)
						snprintf(rd_str, sizeof(rd_str),
							 "%u:%d", rd_as.as,
							 rd_as.val);
					else if (type == RD_TYPE_IP)
						snprintfrr(rd_str,
							   sizeof(rd_str),
							   "%pI4:%d", &rd_ip.ip,
							   rd_ip.val);
					json_object_string_add(
						json_routes,
						"rd", rd_str);
				} else {
					vty_out(vty, "Route Distinguisher: ");

					if (type == RD_TYPE_AS
					    || type == RD_TYPE_AS4)
						vty_out(vty, "%u:%d", rd_as.as,
							rd_as.val);
					else if (type == RD_TYPE_IP)
						vty_out(vty, "%pI4:%d",
							&rd_ip.ip, rd_ip.val);
#ifdef ENABLE_BGP_VNC
					else if (type == RD_TYPE_VNC_ETH)
						vty_out(vty,
							"%u:%02x:%02x:%02x:%02x:%02x:%02x",
							rd_vnc_eth.local_nve_id,
							rd_vnc_eth.macaddr
								.octet[0],
							rd_vnc_eth.macaddr
								.octet[1],
							rd_vnc_eth.macaddr
								.octet[2],
							rd_vnc_eth.macaddr
								.octet[3],
							rd_vnc_eth.macaddr
								.octet[4],
							rd_vnc_eth.macaddr
								.octet[5]);
#endif

					vty_out(vty, "\n");
				}
				rd_header = 0;
			}
			/* 【输出该条路由】把本前缀命中的 attr 交给通用函数
			 * route_vty_out_tmp 打印（文本表格或 JSON）；
			 * use_json 时结果会累加进 json_routes。 */
			route_vty_out_tmp(vty, bgp, rm, bgp_dest_get_prefix(rm),
					  attr, safi, use_json, json_routes,
					  false);
			output_count++;
		}

		/* 本 RD 的全部前缀已输出，把它对应的 json_routes 挂到
		 * advertisedRoutes 下，键为该 RD 字符串；非 JSON 模式跳过。 */
		if (use_json && json_routes)
			json_object_object_add(json_adv, rd_str, json_routes);
	}

	/* 【收尾】JSON 模式挂上 advertisedRoutes 与前缀计数后统一输出；
	 * 文本模式则打印总计前缀数。 */
	if (use_json) {
		json_object_object_add(json, "advertisedRoutes", json_adv);
		json_object_int_add(json,
			"totalPrefixCounter", output_count);
		vty_json(vty, json);
	} else
		vty_out(vty, "\nTotal number of prefixes %ld\n", output_count);

	return CMD_SUCCESS;
}
