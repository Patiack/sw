// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP FlowSpec VTY
 * Copyright (C) 2018 6WIND
 */

/* ============================================================================
 * 【文件导读】bgp_flowspec_vty.c —— BGP FlowSpec 的 vty / show / JSON 输出
 * ============================================================================
 * 【本文件做什么】把 FlowSpec 规则（二进制 NLRI + 流量动作扩展团体）渲染成人可
 * 读的文本或 JSON，并附带 debug 开关、local-install 配置、以及“某个 IP 命中了
 * 哪些规则”的反查。它属于 FlowSpec 的**展示层 / 配置层**，本身不解析 NLRI
 * （解析在 bgp_flowspec_util.c，策略下发在 bgp_pbr.c）。
 *
 * 【显示模式】format / display 参数取 bgp_flowspec.h 里的 NLRI_STRING_FORMAT
 * 系列常量：LARGE 最全（show detail）、DEBUG 带逗号分隔、MIN 精简（列表用）、
 * JSON / JSON_SIMPLE 结构化。本文件主要用到 LARGE / DEBUG / MIN / JSON。
 * 注意区分它和 bgp_flowspec_util.h 里的 enum bgp_flowspec_util_nlri_t：那个决定
 * “解码结果往哪去”，这个只决定“字符串要多详细”，是两套不同的常量。
 *
 * 【两套分量标签表】bgp_flowspec_display_large 与 bgp_flowspec_display_min 把
 * 分量类型号映射成英文标签：前者用完整名（Destination Address），后者用缩写
 * （to / proto / port）。显示时按 format 选择用哪张表，再用 lookup_msg 查。
 *
 * 【阅读建议】先看 FS_STRING_UPDATE 宏（字符串拼接帮手）→ bgp_fs_nlri_get_string
 *            （整条 NLRI 渲染的核心）→ route_vty_out_flowspec（单条条目输出）
 *            → bgp_show_table_flowspec（表遍历）→ 其余配置与反查函数。
 * ============================================================================
 */

#include <zebra.h>
#include "command.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_flowspec.h"
#include "bgpd/bgp_flowspec_util.h"
#include "bgpd/bgp_flowspec_private.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_pbr.h"

/* 【说明】下面这块存放本地结构体 / 常量：两张“分量类型号 → 英文标签”的消息表，
 * 以及把渲染结果往缓冲里拼的宏。它们都只服务于显示，不参与协议解析。 */
/* Local Structures and variables declarations
 * This code block hosts the struct declared that host the flowspec rules
 * as well as some structure used to convert to stringx
 */

/* 【数据表】完整标签表：分量类型号 → 完整英文名，供 LARGE / DEBUG 模式使用。
 * struct message 是 lib 的“代号 → 文本”查表结构，末尾以 {0} 结尾；
 * 用 lookup_msg 按类型号取出标签。表里的常量名即协议里各分量的编号宏。 */
static const struct message bgp_flowspec_display_large[] = {
	{FLOWSPEC_DEST_PREFIX, "Destination Address"},
	{FLOWSPEC_SRC_PREFIX, "Source Address"},
	{FLOWSPEC_IP_PROTOCOL, "IP Protocol"},
	{FLOWSPEC_PORT, "Port"},
	{FLOWSPEC_DEST_PORT, "Destination Port"},
	{FLOWSPEC_SRC_PORT, "Source Port"},
	{FLOWSPEC_ICMP_TYPE, "ICMP Type"},
	{FLOWSPEC_ICMP_CODE, "ICMP Code"},
	{FLOWSPEC_TCP_FLAGS, "TCP Flags"},
	{FLOWSPEC_PKT_LEN, "Packet Length"},
	{FLOWSPEC_DSCP, "DSCP field"},
	{FLOWSPEC_FRAGMENT, "Packet Fragment"},
	{FLOWSPEC_FLOW_LABEL, "Packet Flow Label"},
	{0}
};

/* 【数据表】精简标签表：与上一张一一对应，但用极短缩写（to / from / proto /
 * port / dstp / srcp 等），供 MIN 模式使用，目的是列表输出时不刷屏。
 * 分量类型号必须与 _large 表保持一致，只有文本不同。 */
static const struct message bgp_flowspec_display_min[] = {
	{FLOWSPEC_DEST_PREFIX, "to"},
	{FLOWSPEC_SRC_PREFIX, "from"},
	{FLOWSPEC_IP_PROTOCOL, "proto"},
	{FLOWSPEC_PORT, "port"},
	{FLOWSPEC_DEST_PORT, "dstp"},
	{FLOWSPEC_SRC_PORT, "srcp"},
	{FLOWSPEC_ICMP_TYPE, "type"},
	{FLOWSPEC_ICMP_CODE, "code"},
	{FLOWSPEC_TCP_FLAGS, "tcp"},
	{FLOWSPEC_PKT_LEN, "pktlen"},
	{FLOWSPEC_DSCP, "dscp"},
	{FLOWSPEC_FRAGMENT, "pktfrag"},
	{FLOWSPEC_FLOW_LABEL, "flwlbl"},
	{0}
};

/* 【宏】FS_STRING_UPDATE —— 在把下一个分量的文本追加到输出缓冲**之前**，按模式
 * 补一个分隔符，并推进 count 计数。规则：
 *   DEBUG 模式且不是第一个分量 → 追加 ", "（逗号分隔，便于阅读）；
 *   MIN 模式且不是第一个分量   → 追加一个空格；
 *   LARGE / JSON 模式          → 不加分隔（换行由调用处的 extra 变量处理）。
 * 参数：count 是已输出分量数（以是否为零判断“是不是第一个”）；ptr 是当前写入
 * 位置；format 是显示模式；remaining_len 是剩余缓冲长度，写满即停止推进。
 * 坑：只有当 snprintf 返回值大于 0 且小于 remaining_len 时才真正前移 ptr 和扣减
 * 长度，避免截断时指针越界。宏在末尾无条件 count++。 */
#define FS_STRING_UPDATE(count, ptr, format, remaining_len)                                       \
	do {                                                                                      \
		int _len_written;                                                                 \
                                                                                                  \
		if (((format) == NLRI_STRING_FORMAT_DEBUG) && (count)) {                          \
			_len_written = snprintf((ptr), (remaining_len), ", ");                    \
			if (_len_written > 0 && _len_written < (remaining_len)) {                 \
				(remaining_len) -= _len_written;                                  \
				(ptr) += _len_written;                                            \
			}                                                                         \
		} else if (((format) == NLRI_STRING_FORMAT_MIN) && (count)) {                     \
			_len_written = snprintf((ptr), (remaining_len), " ");                     \
			if (_len_written > 0 && _len_written < (remaining_len)) {                 \
				(remaining_len) -= _len_written;                                  \
				(ptr) += _len_written;                                            \
			}                                                                         \
		}                                                                                 \
		count++;                                                                          \
	} while (0)

/* 【核心】bgp_fs_nlri_get_string —— 把一整段二进制 NLRI（nlri_content / len）渲染
 * 成字符串或 JSON。这是本文件最核心的函数。
 * 入参：return_string 是调用方提供的文本缓冲（至少 BGP_FLOWSPEC_STRING_DISPLAY_MAX
 *       字节）；format 是显示模式；json_path 非空时结果写进该 json 对象（文本模式
 *       下可传 NULL）；afi 决定 v4 / v6 解释。
 * 逻辑：循环读分量——先取 1 字节分量类型 type，再按类型分派到三种解码器：
 *       前缀类（DEST/SRC_PREFIX）走 bgp_flowspec_ip_address；数值类走
 *       bgp_flowspec_op_decode；位掩码类（TCP_FLAGS/FRAGMENT）走
 *       bgp_flowspec_bitmask_decode。三者都以 RETURN_STRING 模式解码到
 *       local_string，再按 json / 文本分别落盘；最后 offset += ret（ret 是解码器
 *       返回的已消耗字节数）向前推进。
 * 返回 void，结果直接写入入参缓冲。
 * 坑：解码器返回值 ret 既表示“消耗字节数”又可能为负；循环以 error >= 0 作为继续
 * 条件，遇到未知分量类型时把 error 置 -1 结束。JSON 模式下用 lookup_msg 拿分量名
 * 作为键；文本模式下用 FS_STRING_UPDATE 补分隔符、extra 决定行尾。 */
/* Parse FLOWSPEC NLRI
 * passed return_string string has assumed length
 * BGP_FLOWSPEC_STRING_DISPLAY_MAX
 */
void bgp_fs_nlri_get_string(unsigned char *nlri_content, size_t len,
			    char *return_string, int format,
			    json_object *json_path,
			    afi_t afi)
{
	uint32_t offset = 0;
	int type;
	int ret = 0, error = 0;
	char *ptr = return_string;
	char local_string[BGP_FLOWSPEC_STRING_DISPLAY_MAX];
	int count = 0;
	char extra[2] = "";
	char pre_extra[2] = "";
	const struct message *bgp_flowspec_display;
	enum bgp_flowspec_util_nlri_t type_util;
	int len_string = BGP_FLOWSPEC_STRING_DISPLAY_MAX;
	int len_written;

	if (format == NLRI_STRING_FORMAT_LARGE) {
		snprintf(pre_extra, sizeof(pre_extra), "\t");
		snprintf(extra, sizeof(extra), "\n");
		bgp_flowspec_display = bgp_flowspec_display_large;
	} else
		bgp_flowspec_display = bgp_flowspec_display_min;
	/* if needed. type_util can be set to other values */
	type_util = BGP_FLOWSPEC_RETURN_STRING;
	error = 0;
	while (offset + 1 < len && error >= 0) {
		type = nlri_content[offset];
		offset++;
		switch (type) {
		case FLOWSPEC_DEST_PREFIX:
		case FLOWSPEC_SRC_PREFIX:
			ret = bgp_flowspec_ip_address(
						type_util,
						nlri_content+offset,
						len - offset,
						local_string, &error,
						afi, NULL);
			if (ret <= 0)
				break;
			if (json_path) {
				json_object_string_add(json_path,
				     lookup_msg(bgp_flowspec_display, type, ""),
				     local_string);
				break;
			}
			FS_STRING_UPDATE(count, ptr, format, len_string);
			len_written = snprintf(ptr, len_string, "%s%s %s%s",
					pre_extra,
					lookup_msg(bgp_flowspec_display,
						   type, ""),
					local_string, extra);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		case FLOWSPEC_FLOW_LABEL:
		case FLOWSPEC_IP_PROTOCOL:
		case FLOWSPEC_PORT:
		case FLOWSPEC_DEST_PORT:
		case FLOWSPEC_SRC_PORT:
		case FLOWSPEC_ICMP_TYPE:
		case FLOWSPEC_ICMP_CODE:
			ret = bgp_flowspec_op_decode(type_util,
						     nlri_content+offset,
						     len - offset,
						     local_string, &error);
			if (ret <= 0)
				break;
			if (json_path) {
				json_object_string_add(json_path,
				     lookup_msg(bgp_flowspec_display, type, ""),
				     local_string);
				break;
			}
			FS_STRING_UPDATE(count, ptr, format, len_string);
			len_written = snprintf(ptr, len_string, "%s%s %s%s",
					pre_extra,
					lookup_msg(bgp_flowspec_display,
					type, ""),
				     local_string, extra);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		case FLOWSPEC_TCP_FLAGS:
			ret = bgp_flowspec_bitmask_decode(
					      type_util,
					      nlri_content+offset,
					      len - offset,
					      local_string, &error);
			if (ret <= 0)
				break;
			if (json_path) {
				json_object_string_add(json_path,
				     lookup_msg(bgp_flowspec_display,
						type, ""),
				     local_string);
				break;
			}
			FS_STRING_UPDATE(count, ptr, format, len_string);
			len_written = snprintf(ptr, len_string, "%s%s %s%s",
					pre_extra,
					lookup_msg(bgp_flowspec_display,
						   type, ""),
					local_string, extra);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		case FLOWSPEC_PKT_LEN:
		case FLOWSPEC_DSCP:
			ret = bgp_flowspec_op_decode(
						type_util,
						nlri_content + offset,
						len - offset, local_string,
						&error);
			if (ret <= 0)
				break;
			if (json_path) {
				json_object_string_add(json_path,
				    lookup_msg(bgp_flowspec_display, type, ""),
				    local_string);
				break;
			}
			FS_STRING_UPDATE(count, ptr, format, len_string);
			len_written = snprintf(ptr, len_string, "%s%s %s%s",
					pre_extra,
					lookup_msg(bgp_flowspec_display,
					type, ""),
				     local_string, extra);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		case FLOWSPEC_FRAGMENT:
			ret = bgp_flowspec_bitmask_decode(
					      type_util,
					      nlri_content+offset,
					      len - offset,
					      local_string, &error);
			if (ret <= 0)
				break;
			if (json_path) {
				json_object_string_add(json_path,
				    lookup_msg(bgp_flowspec_display,
					       type, ""),
				    local_string);
				break;
			}
			FS_STRING_UPDATE(count, ptr, format, len_string);
			len_written = snprintf(ptr, len_string, "%s%s %s%s",
					pre_extra,
					lookup_msg(bgp_flowspec_display,
					type, ""),
					local_string, extra);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		default:
			error = -1;
			break;
		}
		offset += ret;
	}
}

/* 【输出】route_vty_out_flowspec —— 打印**单条** FlowSpec 条目（供表遍历逐条调用）。
 * 入参：p 是承载 NLRI 的前缀（family 必须是 AF_FLOWSPEC，否则直接返回）；
 *       path 是路径（可为 NULL，为 NULL 时只输出 NLRI 部分）；
 *       display 是显示模式；json_paths 非空时走 JSON 分支。
 * 处理内容依次为：NLRI 文本（借 bgp_fs_nlri_get_string）→ 流量动作扩展团体
 * （ecommunity / ipv6_ecommunity，用 ecommunity_ecom2str 转字符串，LARGE 下还补打
 * NLRI 下一跳）→ 接收时长（peer_uptime）→ LARGE 下额外打印该条目在本机 PBR 里的
 * 安装情况（遍历 extra->flowspec->bgp_fs_pbr 与 bgp_fs_iprule，列出对应的 ipset /
 * rule 名，用 list_bpm 去重）。
 * 返回 void。
 * 坑：ecommunity_ecom2str 返回的字符串需 XFREE(MTYPE_ECOMMUNITY_STR) 释放；
 * JSON 模式（NLRI_STRING_FORMAT_JSON）与“其它 JSON 语境”的写法不同，前者用
 * json_object_array_add 把子对象挂到数组。 */
void route_vty_out_flowspec(struct vty *vty, const struct prefix *p,
			    struct bgp_path_info *path, int display,
			    json_object *json_paths)
{
	struct attr *attr;
	char return_string[BGP_FLOWSPEC_STRING_DISPLAY_MAX];
	char *s1 = NULL, *s2 = NULL;
	json_object *json_nlri_path = NULL;
	json_object *json_ecom_path = NULL;
	json_object *json_time_path = NULL;
	char timebuf[BGP_UPTIME_LEN];
	struct ecommunity *ipv6_ecomm = NULL;

	if (p == NULL || p->family != AF_FLOWSPEC)
		return;
	if (json_paths) {
		if (display == NLRI_STRING_FORMAT_JSON)
			json_nlri_path = json_object_new_object();
		else
			json_nlri_path = json_paths;
	}
	if (display == NLRI_STRING_FORMAT_LARGE && path)
		vty_out(vty, "BGP flowspec entry: (flags 0x%x)\n",
			path->flags);
	bgp_fs_nlri_get_string((unsigned char *)
			       p->u.prefix_flowspec.ptr,
			       p->u.prefix_flowspec.prefixlen,
			       return_string,
			       display,
			       json_nlri_path,
			       family2afi(p->u.prefix_flowspec
					  .family));
	if (display == NLRI_STRING_FORMAT_LARGE)
		vty_out(vty, "%s", return_string);
	else if (display == NLRI_STRING_FORMAT_DEBUG)
		vty_out(vty, "%s", return_string);
	else if (display == NLRI_STRING_FORMAT_MIN)
		vty_out(vty, " %-30s", return_string);
	else if (json_paths && display == NLRI_STRING_FORMAT_JSON)
		json_object_array_add(json_paths, json_nlri_path);
	if (!path)
		return;

	if (path->attr)
		ipv6_ecomm = bgp_attr_get_ipv6_ecommunity(path->attr);

	if (path->attr && (bgp_attr_get_ecommunity(path->attr) || ipv6_ecomm)) {
		/* Print attribute */
		attr = path->attr;
		if (bgp_attr_get_ecommunity(attr))
			s1 = ecommunity_ecom2str(bgp_attr_get_ecommunity(attr),
						 ECOMMUNITY_FORMAT_ROUTE_MAP,
						 0);
		if (ipv6_ecomm)
			s2 = ecommunity_ecom2str(
				ipv6_ecomm, ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
		if (!s1 && !s2)
			return;
		if (display == NLRI_STRING_FORMAT_LARGE)
			vty_out(vty, "\t%s%s%s\n", s1 ? s1 : "",
				s2 && s1 ? " " : "", s2 ? s2 : "");
		else if (display == NLRI_STRING_FORMAT_MIN)
			vty_out(vty, "%s%s", s1 ? s1 : "", s2 ? s2 : "");
		else if (json_paths) {
			json_ecom_path = json_object_new_object();
			if (s1)
				json_object_string_add(json_ecom_path,
						       "ecomlist", s1);
			if (s2)
				json_object_string_add(json_ecom_path,
						       "ecom6list", s2);
			if (display == NLRI_STRING_FORMAT_JSON)
				json_object_array_add(json_paths,
						      json_ecom_path);
		}
		if (display == NLRI_STRING_FORMAT_LARGE) {
			char local_buff[INET6_ADDRSTRLEN];

			local_buff[0] = '\0';
			if (p->u.prefix_flowspec.family == AF_INET
			    && attr->nexthop.s_addr != INADDR_ANY)
				inet_ntop(AF_INET, &attr->nexthop.s_addr,
					  local_buff, sizeof(local_buff));
			else if (p->u.prefix_flowspec.family == AF_INET6 &&
				 attr->mp_nexthop_len != 0 &&
				 attr->mp_nexthop_len != BGP_ATTR_NHLEN_IPV4 &&
				 attr->mp_nexthop_len != BGP_ATTR_NHLEN_VPNV4)
				inet_ntop(AF_INET6, &attr->mp_nexthop_global,
					  local_buff, sizeof(local_buff));
			if (local_buff[0] != '\0')
				vty_out(vty, "\tNLRI NH %s\n",
					local_buff);
		}
		XFREE(MTYPE_ECOMMUNITY_STR, s1);
		XFREE(MTYPE_ECOMMUNITY_STR, s2);
	}
	peer_uptime(path->uptime, timebuf, BGP_UPTIME_LEN, 0, NULL);
	if (display == NLRI_STRING_FORMAT_LARGE) {
		vty_out(vty, "\treceived for %8s\n", timebuf);
	} else if (json_paths) {
		json_time_path = json_object_new_object();
		json_object_string_add(json_time_path,
				       "time", timebuf);
		if (display == NLRI_STRING_FORMAT_JSON)
			json_object_array_add(json_paths, json_time_path);
	}
	if (display == NLRI_STRING_FORMAT_LARGE) {
		struct bgp_path_info_extra *extra =
			bgp_path_info_extra_get(path);
		bool list_began = false;

		if (extra->flowspec && extra->flowspec->bgp_fs_pbr &&
		    listcount(extra->flowspec->bgp_fs_pbr)) {
			struct listnode *node;
			struct bgp_pbr_match_entry *bpme;
			struct bgp_pbr_match *bpm;
			struct list *list_bpm;

			list_bpm = list_new();
			vty_out(vty, "\tinstalled in PBR");
			for (ALL_LIST_ELEMENTS_RO(extra->flowspec->bgp_fs_pbr, node,
						  bpme)) {
				bpm = bpme->backpointer;
				if (listnode_lookup(list_bpm, bpm))
					continue;
				listnode_add(list_bpm, bpm);
				if (!list_began) {
					vty_out(vty, " (");
					list_began = true;
				} else
					vty_out(vty, ", ");
				vty_out(vty, "%s", bpm->ipset_name);
			}
			list_delete(&list_bpm);
		}
		if (extra->flowspec && extra->flowspec->bgp_fs_iprule &&
		    listcount(extra->flowspec->bgp_fs_iprule)) {
			struct listnode *node;
			struct bgp_pbr_rule *bpr;

			if (!list_began)
				vty_out(vty, "\tinstalled in PBR");
			for (ALL_LIST_ELEMENTS_RO(extra->flowspec->bgp_fs_iprule,
						  node, bpr)) {
				if (!bpr->action)
					continue;
				if (!list_began) {
					vty_out(vty, " (");
					list_began = true;
				} else
					vty_out(vty, ", ");
				vty_out(vty, "-ipv4-rule %d action lookup %u-",
					bpr->priority,
					bpr->action->table_id);
			}
		}
		if (list_began)
			vty_out(vty, ")\n");
		else
			vty_out(vty, "\tnot installed in PBR\n");
	}
}

/* 【遍历】bgp_show_table_flowspec —— 遍历整张 FlowSpec 表并逐条输出，是
 * `show bgp ... flowspec` 的通用实现（签名与 bgp_flowspec.h 一致）。
 * 仅当 type == bgp_show_type_detail 时才实际输出，其余类型直接返回 CMD_SUCCESS。
 * 逻辑：从表顶开始逐个 dest 走，取 dest 上的 path_info 链，逐条调用
 * route_vty_out_flowspec；JSON 模式下为每个 dest 建一个数组、输出后 vty_json。
 * 末尾（非 JSON 且有内容时）打印 “Displayed N flowspec entries”。
 * 返回 CMD_SUCCESS。
 * 坑：JSON 模式下 json_paths 是**按 dest 重建**的，一个 dest 一个 JSON 数组对象；
 * display 在 JSON 时切成 NLRI_STRING_FORMAT_JSON（默认是 LARGE）。 */
int bgp_show_table_flowspec(struct vty *vty, struct bgp *bgp, afi_t afi,
			    struct bgp_table *table, enum bgp_show_type type,
			    void *output_arg, bool use_json, int is_last,
			    unsigned long *output_cum, unsigned long *total_cum)
{
	struct bgp_path_info *pi;
	struct bgp_dest *dest;
	unsigned long total_count = 0;
	json_object *json_paths = NULL;
	int display = NLRI_STRING_FORMAT_LARGE;

	if (type != bgp_show_type_detail)
		return CMD_SUCCESS;

	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		pi = bgp_dest_get_bgp_path_info(dest);
		if (pi == NULL)
			continue;
		if (use_json) {
			json_paths = json_object_new_array();
			display = NLRI_STRING_FORMAT_JSON;
		}
		for (; pi; pi = pi->next) {
			total_count++;
			route_vty_out_flowspec(vty, bgp_dest_get_prefix(dest),
					       pi, display, json_paths);
		}
		if (use_json) {
			vty_json(vty, json_paths);
			json_paths = NULL;
		}
	}
	if (total_count && !use_json)
		vty_out(vty,
			"\nDisplayed %ld flowspec entries\n",
			total_count);
	return CMD_SUCCESS;
}

/* 【命令】`debug bgp flowspec`：开启 FlowSpec 调试。在 CONFIG_NODE 下写入持久
 * 调试开关（DEBUG_ON），否则作为终端临时调试（TERM_DEBUG_ON）并提示已开启。
 * 返回 CMD_SUCCESS。 */
DEFUN (debug_bgp_flowspec,
       debug_bgp_flowspec_cmd,
       "debug bgp flowspec",
       DEBUG_STR
       BGP_STR
       "BGP allow flowspec debugging entries\n")
{
	if (vty->node == CONFIG_NODE)
		DEBUG_ON(flowspec, FLOWSPEC);
	else {
		TERM_DEBUG_ON(flowspec, FLOWSPEC);
		vty_out(vty, "BGP flowspec debugging is on\n");
	}
	return CMD_SUCCESS;
}

/* 【命令】`no debug bgp flowspec`：关闭 FlowSpec 调试，逻辑与上面开关相反
 * （DEBUG_OFF / TERM_DEBUG_OFF）。返回 CMD_SUCCESS。 */
DEFUN (no_debug_bgp_flowspec,
       no_debug_bgp_flowspec_cmd,
       "no debug bgp flowspec",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP allow flowspec debugging entries\n")
{
	if (vty->node == CONFIG_NODE)
		DEBUG_OFF(flowspec, FLOWSPEC);
	else {
		TERM_DEBUG_OFF(flowspec, FLOWSPEC);
		vty_out(vty, "BGP flowspec debugging is off\n");
	}
	return CMD_SUCCESS;
}

/* 【配置回写】bgp_fs_config_write_pbr —— 把本机 PBR 接口配置反向写成可再解析的
 * 配置行（`show running-config` 用）。仅处理 SAFI_FLOWSPEC，且需 bgp->bgp_pbr_cfg
 * 存在。
 * 按 afi 选 IPv4 / IPv6 的接口红黑树 head 与 pbr_interface_any 标志；遍历 head 为
 * 每个接口输出 “local-install <name>”。返回 1 表示需要在该 afi 下声明 flowspec
 * 配置节（head 非空或 any 标志为假），0 表示无需声明。
 * 坑：返回值不是“行数”而是“是否需要声明配置节点”，与 bgp_flowspec.h 的注释一致。 */
int bgp_fs_config_write_pbr(struct vty *vty, struct bgp *bgp,
			    afi_t afi, safi_t safi)
{
	struct bgp_pbr_interface *pbr_if;
	bool declare_node = false;
	struct bgp_pbr_config *bgp_pbr_cfg = bgp->bgp_pbr_cfg;
	struct bgp_pbr_interface_head *head;
	bool bgp_pbr_interface_any;

	if (!bgp_pbr_cfg || safi != SAFI_FLOWSPEC)
		return 0;
	if (afi == AFI_IP) {
		head = &(bgp_pbr_cfg->ifaces_by_name_ipv4);
		bgp_pbr_interface_any = bgp_pbr_cfg->pbr_interface_any_ipv4;
	} else if (afi == AFI_IP6) {
		head = &(bgp_pbr_cfg->ifaces_by_name_ipv6);
		bgp_pbr_interface_any = bgp_pbr_cfg->pbr_interface_any_ipv6;
	} else {
		return 0;
	}
	if (!RB_EMPTY(bgp_pbr_interface_head, head) ||
	     !bgp_pbr_interface_any)
		declare_node = true;
	RB_FOREACH (pbr_if, bgp_pbr_interface_head, head) {
		vty_out(vty, "  local-install %s\n", pbr_if->name);
	}
	return declare_node ? 1 : 0;
}

/* 【配置核心】bgp_fs_local_install_interface —— local-install 命令的实际处理逻辑，
 * 维护“哪些接口启用本机 PBR”的红黑树与 any 标志。
 * 入参：no 非空表示删除；ifname 为 NULL 表示“操作 any（全部接口）”而非某个接口；
 *       afi 选择 IPv4 / IPv6 的表。
 * 分支：no+无 ifname → 清 any 标志并 bgp_pbr_reset 移除全部接口列表；no+有 ifname
 * → 从树里删该接口；正向+ifname → 查重后新建接口节点插入并清 any；正向+无 ifname
 * → 置 any 为真、清空具体接口列表。返回 CMD_SUCCESS。
 * 坑：any 为真与“存在具体接口”互斥，两条路径都会用 bgp_pbr_reset 清空另一侧。 */
static int bgp_fs_local_install_interface(struct bgp *bgp,
					  const char *no, const char *ifname,
					  afi_t afi)
{
	struct bgp_pbr_interface *pbr_if;
	struct bgp_pbr_config *bgp_pbr_cfg = bgp->bgp_pbr_cfg;
	struct bgp_pbr_interface_head *head;
	bool *bgp_pbr_interface_any;

	if (!bgp_pbr_cfg)
		return CMD_SUCCESS;
	if (afi == AFI_IP) {
		head = &(bgp_pbr_cfg->ifaces_by_name_ipv4);
		bgp_pbr_interface_any = &(bgp_pbr_cfg->pbr_interface_any_ipv4);
	} else {
		head = &(bgp_pbr_cfg->ifaces_by_name_ipv6);
		bgp_pbr_interface_any = &(bgp_pbr_cfg->pbr_interface_any_ipv6);
	}
	if (no) {
		if (!ifname) {
			if (*bgp_pbr_interface_any) {
				*bgp_pbr_interface_any = false;
				/* remove all other interface list */
				bgp_pbr_reset(bgp, afi);
			}
			return CMD_SUCCESS;
		}
		pbr_if = bgp_pbr_interface_lookup(ifname, head);
		if (!pbr_if)
			return CMD_SUCCESS;
		RB_REMOVE(bgp_pbr_interface_head, head, pbr_if);
		return CMD_SUCCESS;
	}
	if (ifname) {
		pbr_if = bgp_pbr_interface_lookup(ifname, head);
		if (pbr_if)
			return CMD_SUCCESS;
		pbr_if = XCALLOC(MTYPE_TMP,
				 sizeof(struct bgp_pbr_interface));
		strlcpy(pbr_if->name, ifname, IFNAMSIZ);
		RB_INSERT(bgp_pbr_interface_head, head, pbr_if);
		*bgp_pbr_interface_any = false;
	} else {
		/* set to default */
		if (!*bgp_pbr_interface_any) {
			/* remove all other interface list
			 */
			bgp_pbr_reset(bgp, afi);
			*bgp_pbr_interface_any = true;
		}
	}
	return CMD_SUCCESS;
}

/* 【命令】`[no] local-install INTERFACE`：在 flowspec 地址族节点下配置本机 PBR
 * 的安装接口。从 vty 上下文取 bgp，解析出可选 no 关键字与接口名，再调用
 * bgp_fs_local_install_interface。返回 CMD_SUCCESS / CMD_WARNING（无 bgp 上下文）。 */
DEFUN (bgp_fs_local_install_ifname,
	bgp_fs_local_install_ifname_cmd,
	"[no] local-install INTERFACE",
	NO_STR
	"Apply local policy routing\n"
	"Interface name\n")
{
	struct bgp *bgp = VTY_GET_CONTEXT(bgp);
	int idx = 0;
	const char *no = strmatch(argv[0]->text, "no") ? "no" : NULL;
	char *ifname = argv_find(argv, argc, "INTERFACE", &idx) ?
		argv[idx]->arg : NULL;

	if (!bgp)
		return CMD_WARNING;

	return bgp_fs_local_install_interface(bgp, no, ifname,
					      bgp_node_afi(vty));
}

/* 【反查】bgp_flowspec_display_match_per_ip —— 在 rib 里找出所有“匹配给定地址”
 * 的 FlowSpec 规则并输出，用于 `show bgp ... flowspec <ip>` 这类排障。
 * 遍历表：只取 family 为 AF_FLOWSPEC 的前缀，用 bgp_flowspec_contains_prefix 判断
 * 是否命中（prefix_check 决定按前缀包含还是按具体地址匹配）；命中则调用
 * route_vty_out_flowspec 输出（use_json 决定 JSON 还是 LARGE 模式）。返回命中的
 * 条目数。
 * 坑：json_paths 由调用方提供并跨条累积；命中即 display++ 并继续扫描，不去重。 */
extern int bgp_flowspec_display_match_per_ip(afi_t afi, struct bgp_table *rib,
					     struct prefix *match,
					     int prefix_check, struct vty *vty,
					     bool use_json,
					     json_object *json_paths)
{
	struct bgp_dest *dest;
	const struct prefix *prefix;
	int display = 0;

	for (dest = bgp_table_top(rib); dest; dest = bgp_route_next(dest)) {
		prefix = bgp_dest_get_prefix(dest);

		if (prefix->family != AF_FLOWSPEC)
			continue;

		if (bgp_flowspec_contains_prefix(prefix, match, prefix_check)) {
			route_vty_out_flowspec(
				vty, prefix, bgp_dest_get_bgp_path_info(dest),
				use_json ? NLRI_STRING_FORMAT_JSON
					 : NLRI_STRING_FORMAT_LARGE,
				json_paths);
			display++;
		}
	}
	return display;
}

/* 【注册】bgp_flowspec_vty_init —— bgpd 启动时调用一次，把 FlowSpec 相关的命令
 * 挂到各节点：debug / no debug 挂 ENABLE_NODE 与 CONFIG_NODE；local-install
 * 命令挂 BGP_FLOWSPECV4_NODE 与 BGP_FLOWSPECV6_NODE（v4 / v6 各一份）。 */
void bgp_flowspec_vty_init(void)
{
	install_element(ENABLE_NODE, &debug_bgp_flowspec_cmd);
	install_element(CONFIG_NODE, &debug_bgp_flowspec_cmd);
	install_element(ENABLE_NODE, &no_debug_bgp_flowspec_cmd);
	install_element(CONFIG_NODE, &no_debug_bgp_flowspec_cmd);
	install_element(BGP_FLOWSPECV4_NODE, &bgp_fs_local_install_ifname_cmd);
	install_element(BGP_FLOWSPECV6_NODE, &bgp_fs_local_install_ifname_cmd);
}
