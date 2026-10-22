// SPDX-License-Identifier: GPL-2.0-or-later
/* Policy Based Routing (PBR) main header
 * Copyright (C) 2018 6WIND
 * Portions:
 *      Copyright (c) 2021 The MITRE Corporation.
 *      Copyright (c) 2023 LabN Consulting, L.L.C.
 */

/*
 * 文件导读（lib/pbr.h）
 *
 * 本文件定义策略路由（Policy Based Routing，PBR）的核心数据结构与常量。
 * PBR 允许按流特征（源 / 目的 IP、端口、协议、fwmark、DSCP、VLAN 等）
 * 匹配报文，并执行非传统查表动作（指向指定路由表、设队列、改 DSCP、
 * 丢弃等），是实现基于策略转发的基础。转发面实现仅支持 Linux。
 *
 * 核心数据结构：
 *   - struct pbr_filter：匹配条件集合，filter_bm 位图标记哪些条件生效，
 *     具体条件包括 IP / 端口 / 协议 / fwmark / DSCP / ECN / VLAN 等。
 *   - struct pbr_action：命中后执行的动作，flags 位图标记生效动作，含
 *     目标表、队列、VLAN 改写、源 / 目的地址改写、DSCP / ECN、丢弃等。
 *   - struct pbr_rule：一条完整规则，即 filter 加 action 再加序号、优先级、
 *     所属 VRF 与入接口。
 *
 * 大量 PBR_FILTER_* / PBR_ACTION_* / MATCH_* / TCP_HEADER_* 宏是上述位图
 * 的取值，用于 zebra 与上层（如 bgpd flowspec）之间的编解码匹配。
 *
 * 与其他模块的关系：bgpd 的 flowspec 将其转换为 PBR 规则，经 zclient /
 * ZAPI 下发给 zebra，由 zebra 落到 Linux 的规则与路由表。
 *
 * 阅读顺序：PBR_STR 宏 -> pbr_filter（含 filter 位图宏）-> pbr_action
 * （含 action 位图宏）-> pbr_rule -> TCP / MATCH 宏 -> 两个编解码函数声明。
 */

#ifndef _PBR_H
#define _PBR_H

#include <zebra.h>
#include "stream.h"
#include "prefix.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PBR 的字符串常量，用于 CLI 帮助文本。 */
#define PBR_STR "Policy Based Routing\n"

/*
 * A PBR filter
 *
 * The filter or match criteria in a PBR rule.
 * For simplicity, all supported filters are grouped into a structure rather
 * than delineating further. A bitmask denotes which filters are actually
 * specified.
 */
/* 一条 PBR 规则的匹配条件集合。filter_bm 的每个二进制位对应一个是否
 * 生效的条件；下方 PBR_FILTER_* 即各条件对应的位。所有条件并列放在
 * 一个结构里，避免再细分嵌套结构。 */
struct pbr_filter {
	uint32_t filter_bm;
#define PBR_FILTER_SRC_IP         (1 << 0)
#define PBR_FILTER_DST_IP         (1 << 1)
#define PBR_FILTER_SRC_PORT       (1 << 2)
#define PBR_FILTER_DST_PORT       (1 << 3)
#define PBR_FILTER_FWMARK         (1 << 4)
#define PBR_FILTER_IP_PROTOCOL	  (1 << 5)
#define PBR_FILTER_SRC_PORT_RANGE (1 << 6)
#define PBR_FILTER_DST_PORT_RANGE (1 << 7)
#define PBR_FILTER_DSCP		  (1 << 8)
#define PBR_FILTER_ECN		  (1 << 9)
#define PBR_FILTER_PCP            (1 << 10)
#define PBR_FILTER_VLAN_FLAGS     (1 << 11)
#define PBR_FILTER_VLAN_ID        (1 << 12)

#define PBR_DSFIELD_DSCP (0xfc) /* Upper 6 bits of DS field: DSCP */
#define PBR_DSFIELD_ECN (0x03)	/* Lower 2 bits of DS field: BCN */

#define PBR_PCP (0x07)		/* 3-bit value 0..7 for prioritization*/

#define PBR_VLAN_FLAGS_NO_WILD	  0
#define PBR_VLAN_FLAGS_TAGGED	  (1 << 0)
#define PBR_VLAN_FLAGS_UNTAGGED	  (1 << 1)
#define PBR_VLAN_FLAGS_UNTAGGED_0 (1 << 2)

	/* Source and Destination IP address with masks */
	struct prefix src_ip;
	struct prefix dst_ip;

	/* Source and Destination layer 4 (TCP/UDP/etc.) port numbers */
	uint16_t src_port;
	uint16_t dst_port;

	/* Filter by VLAN and prioritization */
	uint8_t pcp;
	uint16_t vlan_id;
	uint16_t vlan_flags;

	/* Filter by Differentiated Services field  */
	uint8_t dsfield; /* DSCP (6 bits) & ECN (2 bits) */

	/* Filter with fwmark */
	uint32_t fwmark;

	/* Filter with the ip protocol */
	uint8_t ip_proto;
};

/*
 * A PBR action
 *
 * The action corresponding to a PBR rule.
 * While the user specifies the action in a particular way, the forwarding
 * plane implementation (Linux only) requires that to be encoded into a
 * route table and the rule then point to that route table; in some cases,
 * the user criteria may directly point to a table too.
 */
/* 一条 PBR 规则命中后执行的动作集合。flags 位图标记生效动作；Linux 转发
 * 面要求把目标动作编码进一张路由表，再由规则指向该表；某些情况下用户
 * 也可直接指定表号。 */
struct pbr_action {
	uint32_t flags;

#define PBR_ACTION_TABLE		(1 << 0)
#define PBR_ACTION_QUEUE_ID		(1 << 1)
#define PBR_ACTION_PCP			(1 << 2)
#define PBR_ACTION_VLAN_ID		(1 << 3)
#define PBR_ACTION_VLAN_STRIP_INNER_ANY (1 << 4)
#define PBR_ACTION_SRC_IP		(1 << 5)
#define PBR_ACTION_DST_IP		(1 << 6)
#define PBR_ACTION_SRC_PORT		(1 << 7)
#define PBR_ACTION_DST_PORT		(1 << 8)
#define PBR_ACTION_DSCP			(1 << 9)
#define PBR_ACTION_ECN			(1 << 10)
#define PBR_ACTION_DROP			(1 << 11) /* nexthop == blackhole */

	uint32_t table;
	uint32_t queue_id;

	/* VLAN */
	uint8_t pcp;
	uint16_t vlan_id;

	/* Source and Destination IP addresses */
	union sockunion src_ip;
	union sockunion dst_ip;

	/* Source and Destination layer 4 (TCP/UDP/etc.) port numbers */
	uint32_t src_port;
	uint32_t dst_port;

	/* Differentiated Services field  */
	uint8_t dscp; /* stored here already shifted to upper 6 bits */
	uint8_t ecn;  /* stored here as lower 2 bits */
};

/*
 * A PBR rule
 *
 * This is a combination of the filter criteria and corresponding action.
 * Rules also have a user-defined sequence number which defines the relative
 * order amongst rules.
 */
/* 一条 PBR 规则 = 匹配条件 + 对应动作，另带用户自定义序号 seq（决定规则
 * 之间的相对顺序）、priority、unique 标识、所属 VRF 与入接口名。 */
struct pbr_rule {
	vrf_id_t vrf_id;
	uint8_t family; /* netlink: select which rule database */

	uint32_t seq;
	uint32_t priority;
	uint32_t unique;
	struct pbr_filter filter;
	struct pbr_action action;

	char ifname[IFNAMSIZ + 1];
};

/* TCP 首部各标志位取值，见 RFC 793；TCP_HEADER_ALL_FLAGS 为其并集。 */
/* TCP flags value shared
 * those are values of byte 13 of TCP header
 * as mentioned in rfc793
 */
#define TCP_HEADER_FIN (0x01)
#define TCP_HEADER_SYN (0x02)
#define TCP_HEADER_RST (0x04)
#define TCP_HEADER_PSH (0x08)
#define TCP_HEADER_ACK (0x10)
#define TCP_HEADER_URG (0x20)
#define TCP_HEADER_ALL_FLAGS (TCP_HEADER_FIN | TCP_HEADER_SYN \
			      | TCP_HEADER_RST | TCP_HEADER_PSH \
			      | TCP_HEADER_ACK | TCP_HEADER_URG)

/* 匹配条件位定义，BGP（flowspec）与 Zebra 之间共用；与 filter_bm 类似，
 * 用于标记 IP / 端口 / DSCP / 协议 / 流标签等条件是否设置及其反义形式。 */
/* Pbr IPTable defines
 * those are common flags shared between BGP and Zebra
 */
#define MATCH_IP_SRC_SET		(1 << 0)
#define MATCH_IP_DST_SET		(1 << 1)
#define MATCH_PORT_SRC_SET		(1 << 2)
#define MATCH_PORT_DST_SET		(1 << 3)
#define MATCH_PORT_SRC_RANGE_SET	(1 << 4)
#define MATCH_PORT_DST_RANGE_SET	(1 << 5)
#define MATCH_DSCP_SET			(1 << 6)
#define MATCH_DSCP_INVERSE_SET		(1 << 7)
#define MATCH_PKT_LEN_INVERSE_SET	(1 << 8)
#define MATCH_FRAGMENT_INVERSE_SET	(1 << 9)
#define MATCH_ICMP_SET			(1 << 10)
#define MATCH_PROTOCOL_SET		(1 << 11)
#define MATCH_FLOW_LABEL_SET		(1 << 12)
#define MATCH_FLOW_LABEL_INVERSE_SET	(1 << 13)

/* PBR 规则的 ZAPI 编解码接口：encode 把规则写入流，decode 从流还原规则；
 * 供 zclient 与 zebra 之间的消息收发使用。 */
extern int zapi_pbr_rule_encode(struct stream *s, struct pbr_rule *r);
extern bool zapi_pbr_rule_decode(struct stream *s, struct pbr_rule *r);

#ifdef __cplusplus
}
#endif

#endif /* _PBR_H */
