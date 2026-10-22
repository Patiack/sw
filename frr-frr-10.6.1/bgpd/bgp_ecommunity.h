// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Extended Communities Attribute.
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

/* 【文件导读】 bgp_ecommunity.h —— BGP Extended Community 属性
 *
 * 一、解决什么问题
 *    Extended community（RFC 4360 及后续 RFC）是 8 字节的属性单元：
 *    2 字节 type 与 6 字节 value。type 又由两部分组成：高序字节
 *    high-order octet 决定“如何解释那 6 字节”，低序字节 low-order octet 是
 *    子类型 subtype，分工如下：
 *      - 高序字节决定编码空间（2 字节 AS / IPv4 / 4 字节 AS / opaque / EVPN 等）；
 *      - subtype 在同一编码空间里指定用途，如 Route Target、SoO、带宽、Router MAC。
 *
 * 二、常见 subtype 在各场景表示什么
 *    - Route Target (RT)：L3VPN / EVPN 的导入导出路由目标，形如 AS:nn 或 IP:nn；
 *    - Site of Origin (SoO)：标识路由来源站点，用于防回环；
 *    - Link Bandwidth / Extended Link Bandwidth：携带链路带宽；
 *    - EVPN 系列：Router MAC、MAC mobility、ES-Import RT、ESI label、DF election 等；
 *    - FlowSpec 系列：traffic-rate / traffic-action / traffic-marking / redirect 等。
 *
 * 三、为什么这样设计
 *    与 community / lcommunity 一样，使用全局哈希驻留 + 引用计数共享，见
 *    ecommunity_intern。不同点是各单元字节数可能不同：普通扩展社区 8 字节
 *    （ECOMMUNITY_SIZE），IPv6 扩展社区 20 字节（IPV6_ECOMMUNITY_SIZE），因此
 *    结构里额外有 unit_size 字段记录单元长度，size 表示单元个数。
 *
 * 四、关键数据结构
 *    struct ecommunity：refcnt / unit_size / disable_ieee_floating / size / val / str；
 *    struct ecommunity_val：8 字节单元；
 *    struct ecommunity_val_ipv6：20 字节单元；
 *    struct ecommunity_as / ecommunity_ip / ecommunity_ip6：解析中间视图。
 *
 * 五、推荐阅读顺序
 *    1) 高序 / 低序类型常量与本文件的编码器 inline 函数；
 *    2) bgp_ecommunity.c 的 ecommunity_new / free / add_val / uniq_sort；
 *    3) ecommunity_intern / unintern；
 *    4) ecommunity_str2com / ecommunity_gettoken / ecommunity_ecom2str（互转）；
 *    5) 各 subtype 的打印分支（在 _ecommunity_ecom2str 中）。
 *
 * 六、返回约定
 *    ecommunity_str 返回内部缓存的字符串，调用者不得 free；
 *    ecommunity_ecom2str 及其内部函数返回新分配的字符串，需用 ecommunity_strfree
 *    释放。
 */

#ifndef _QUAGGA_BGP_ECOMMUNITY_H
#define _QUAGGA_BGP_ECOMMUNITY_H

#include "bgpd/bgp_route.h"
#include "bgpd/bgp_rpki.h"
#include "bgpd/bgpd.h"

/* 【常量】带宽换算：BGP link-bandwidth 以字节每秒（bytes per sec）为单位，
 * 这里给出 Gbps/Mbps/Kbps 对应的字节数，用于人类可读打印时选择单位。
 */
#define ONE_GBPS_BYTES (1000 * 1000 * 1000 / 8)
#define ONE_MBPS_BYTES (1000 * 1000 / 8)
#define ONE_KBPS_BYTES (1000 / 8)

/* Refer to rfc7153 for the IANA registry definitions. These are
 * updated by other standards like rfc7674.
 */
/* High-order octet of the Extended Communities type field.  */
/* 【type 字段分工】扩展社区 8 字节 = 2 字节 type + 6 字节 value。
 * 高序字节（high-order octet）决定如何解释 value，常见取值：
 *   0x00  AS 编码（2 字节 AS 与 4 字节值，写作 AS:nn）；
 *   0x01  IPv4 编码（4 字节 IP 与 2 字节值，写作 IP:nn）；
 *   0x02  AS4 编码（4 字节 AS 与 2 字节值）；
 *   0x03  opaque（语义由 subtype 定义，如 color、encap）；
 *   0x06  EVPN（EVPN 专用子类型）；
 *   0x08  FlowSpec redirect-to-IP-nexthop。
 * 0x40 位是“非传递 non-transitive”标志，与高序字节或在一起；某些取值
 * （如 0x41、0x43）本身已含该标志。低序字节是 subtype。
 * 经验：判断“是不是 RT”主要看 subtype，判断“如何取出 AS 或 IP”才看高序字节。
 */
#define ECOMMUNITY_ENCODE_AS                0x00
#define ECOMMUNITY_ENCODE_IP                0x01
#define ECOMMUNITY_ENCODE_AS4               0x02
#define ECOMMUNITY_ENCODE_OPAQUE            0x03
#define ECOMMUNITY_ENCODE_EVPN              0x06
#define ECOMMUNITY_ENCODE_REDIRECT_IP_NH    0x08 /* Flow Spec */
/* Generic Transitive Experimental */
#define ECOMMUNITY_ENCODE_TRANS_EXP         0x80

/* RFC7674 */
#define ECOMMUNITY_EXTENDED_COMMUNITY_PART_2 0x81
#define ECOMMUNITY_EXTENDED_COMMUNITY_PART_3 0x82

/* Non-transitive extended community types. */
#define ECOMMUNITY_ENCODE_IP_NON_TRANS      0x41
#define ECOMMUNITY_ENCODE_OPAQUE_NON_TRANS  0x43

/* Low-order octet of the Extended Communities type field.  */
/* Note: This really depends on the high-order octet. This means that
 * multiple definitions for the same value are possible.
 */
/* 【subtype 常量】低序字节。其含义依赖高序字节：同一数值在不同编码空间里可能
 * 表示不同东西（参见上面注释）。此处列出常见 subtype：Route Target、Site of
 * Origin、Link Bandwidth、Traffic Rate / Action / Marking（FlowSpec）、
 * Redirect VRF / IP-NH、Color 等。
 */
#define ECOMMUNITY_ORIGIN_VALIDATION_STATE  0x00
#define ECOMMUNITY_ROUTE_TARGET             0x02
#define ECOMMUNITY_SITE_ORIGIN              0x03
#define ECOMMUNITY_LINK_BANDWIDTH           0x04
#define ECOMMUNITY_TRAFFIC_RATE             0x06 /* Flow Spec */
#define ECOMMUNITY_TRAFFIC_ACTION           0x07
#define ECOMMUNITY_REDIRECT_VRF             0x08
#define ECOMMUNITY_TRAFFIC_MARKING          0x09
#define ECOMMUNITY_REDIRECT_IP_NH           0x00
#define ECOMMUNITY_COLOR 0x0b /* RFC9012 - color */

/* from IANA: bgp-extended-communities/bgp-extended-communities.xhtml
 * 0x0c Flow-spec Redirect to IPv4 - draft-ietf-idr-flowspec-redirect
 */
#define ECOMMUNITY_FLOWSPEC_REDIRECT_IPV4   0x0c
/* RFC 8956 */
#define ECOMMUNITY_FLOWSPEC_REDIRECT_IPV6 0x0d

/* https://datatracker.ietf.org/doc/html/draft-li-idr-link-bandwidth-ext-01
 * Sub-type is allocated by IANA, just the draft is not yet updated with the
 * new value.
 */
#define ECOMMUNITY_EXTENDED_LINK_BANDWIDTH 0x0006

/* Low-order octet of the Extended Communities type field for EVPN types */
/* 【EVPN subtype】类型为 0x06 时的子类型（EVPN 场景）：
 *   MAC mobility、ESI label、ES-Import RT、Router MAC、Layer2 attr、
 *   DF election、Default Gateway、ND 等。
 */
#define ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY  0x00
#define ECOMMUNITY_EVPN_SUBTYPE_ESI_LABEL    0x01
#define ECOMMUNITY_EVPN_SUBTYPE_ES_IMPORT_RT 0x02
#define ECOMMUNITY_EVPN_SUBTYPE_ROUTERMAC    0x03
#define ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR  0x04
#define ECOMMUNITY_EVPN_SUBTYPE_DF_ELECTION 0x06
#define ECOMMUNITY_EVPN_SUBTYPE_DEF_GW       0x0d
#define ECOMMUNITY_EVPN_SUBTYPE_ND           0x08

#define ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY_FLAG_STICKY 0x01

/* Layer2 Attributes: RFC8214 */
#define ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR_PRIMARY_PE_FLAG   0x01
#define ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR_BACKUP_PE_FLAG    0x02
#define ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR_CONTROL_WORD_FLAG 0x04

/* DF alg bits - only lower 5 bits are applicable */
#define ECOMMUNITY_EVPN_SUBTYPE_DF_ALG_BITS 0x1f

#define ECOMMUNITY_EVPN_SUBTYPE_ND_ROUTER_FLAG   0x01
#define ECOMMUNITY_EVPN_SUBTYPE_ND_OVERRIDE_FLAG 0x02
#define ECOMMUNITY_EVPN_SUBTYPE_PROXY_FLAG       0x04

#define ECOMMUNITY_EVPN_SUBTYPE_ESI_SA_FLAG (1 << 0) /* single-active */

/* Low-order octet of the Extended Communities type field for OPAQUE types */
/* 【opaque subtype】类型为 0x03（transitive）或 0x43（non-transitive）时：
 *   encap（封装 / 隧道类型）、color（RFC 9256 着色）。
 */
#define ECOMMUNITY_OPAQUE_SUBTYPE_ENCAP     0x0c
#define ECOMMUNITY_OPAQUE_SUBTYPE_COLOR	    0x0b

/* Extended communities attribute string format.  */
/* 【字符串格式】同一份扩展社区有 3 种打印格式（历史原因，见 ecom2str 说明）：
 *   ROUTE_MAP：route-map set extcommunity，形如 "rt 100:1 100:2"；
 *   COMMUNITY_LIST：extcommunity-list，形如 "rt 100:1 rt 100:2 soo 100:1"；
 *   DISPLAY：show bgp，形如 "RT:100:1 SoO:100:1"。
 */
#define ECOMMUNITY_FORMAT_ROUTE_MAP            0
#define ECOMMUNITY_FORMAT_COMMUNITY_LIST       1
#define ECOMMUNITY_FORMAT_DISPLAY              2

/* 【尺寸】普通扩展社区单元 8 字节；IPv6 扩展社区单元 20 字节
 * （16 字节 IPv6 地址外加 2 字节值等，见 rt_soo_str_internal 的处理）。
 */
/* Extended Communities value is eight octet long.  */
#define ECOMMUNITY_SIZE                        8
#define IPV6_ECOMMUNITY_SIZE                  20

/* Extended Community Origin Validation State */
/* 【枚举】RPKI ROV 校验状态，编码进 OVS 扩展社区
 * （type 0x43 非传递 opaque，subtype 0x00）。
 */
enum ecommunity_origin_validation_states {
	ECOMMUNITY_ORIGIN_VALIDATION_STATE_VALID,
	ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTFOUND,
	ECOMMUNITY_ORIGIN_VALIDATION_STATE_INVALID,
	ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTUSED
};

/* Extended Communities type flag.  */
#define ECOMMUNITY_FLAG_NON_TRANSITIVE      0x40

/* Extended Community readable string length */
#define ECOMMUNITY_STRLEN 64

/* Node Target Extended Communities */
#define ECOMMUNITY_NODE_TARGET 0x09
#define ECOMMUNITY_NODE_TARGET_RESERVED 0

/* Extended Communities attribute.  */
/* 【结构】扩展社区集合。
 *   unit_size 单元字节数（8 或 20），size 是单元个数，val 是连续单元数组；
 *   disable_ieee_floating 为真时 link-bandwidth 按整数解释（不做 IEEE-754 浮点）；
 *   refcnt 引用计数，驻留后由全局哈希表管理。
 */
struct ecommunity {
	/* Reference counter.  */
	unsigned long refcnt;

	/* Size of Each Unit of Extended Communities attribute.
	 * to differentiate between IPv6 ext comm and ext comm
	 */
	uint8_t unit_size;

	/* Disable IEEE floating-point encoding for extended community */
	bool disable_ieee_floating;

	/* Size of Extended Communities attribute.  */
	uint32_t size;

	/* Extended Communities value.  */
	uint8_t *val;

	/* Human readable format string.  */
	char *str;
};

/* 【结构】解析用的中间视图：
 *   ecommunity_as：AS 编码的值（2 或 4 字节 AS 与数值）；
 *   ecommunity_ip：IPv4 编码的值（4 字节 IP 与 2 字节值）；
 *   ecommunity_ip6：IPv6 编码的值（16 字节 IP 与 2 字节值）。
 */
struct ecommunity_as {
	as_t as;
	uint32_t val;
};

struct ecommunity_ip {
	struct in_addr ip;
	uint16_t val;
};

struct ecommunity_ip6 {
	struct in6_addr ip;
	uint16_t val;
};

/* Extended community value is eight octet.  */
/* 【结构】单个扩展社区值（8 字节）。 */
struct ecommunity_val {
	uint8_t val[ECOMMUNITY_SIZE];
};

/* IPv6 Extended community value is eight octet.  */
/* 【结构】单个 IPv6 扩展社区值（20 字节）。
 * 疑点：原英文注释写作 eight octet 但实际是 20 字节（IPV6_ECOMMUNITY_SIZE），
 * 注释有误，代码未改动。
 */
struct ecommunity_val_ipv6 {
	uint8_t val[IPV6_ECOMMUNITY_SIZE];
};

#define ecom_length_size(X, Y)    ((X)->size * (Y))

/*
 * Encode BGP Route Target AS:nn.
 */
/* 【函数】encode_route_target_as：编码 "RT 2字节AS:值"（type 0x00，subtype 0x02）。
 * trans 为假时设置 non-transitive 标志（0x40）。val 为 4 字节值。
 */
static inline void encode_route_target_as(as_t as, uint32_t val,
					  struct ecommunity_val *eval,
					  bool trans)
{
	eval->val[0] = ECOMMUNITY_ENCODE_AS;
	if (!trans)
		eval->val[0] |= ECOMMUNITY_FLAG_NON_TRANSITIVE;
	eval->val[1] = ECOMMUNITY_ROUTE_TARGET;
	eval->val[2] = (as >> 8) & 0xff;
	eval->val[3] = as & 0xff;
	eval->val[4] = (val >> 24) & 0xff;
	eval->val[5] = (val >> 16) & 0xff;
	eval->val[6] = (val >> 8) & 0xff;
	eval->val[7] = val & 0xff;
}

/*
 * Encode BGP Route Target IP:nn.
 */
/* 【函数】encode_route_target_ip：编码 "RT IP:值"（type 0x01，subtype 0x02）。 */
static inline void encode_route_target_ip(struct in_addr *ip, uint16_t val,
					  struct ecommunity_val *eval,
					  bool trans)
{
	eval->val[0] = ECOMMUNITY_ENCODE_IP;
	if (!trans)
		eval->val[0] |= ECOMMUNITY_FLAG_NON_TRANSITIVE;
	eval->val[1] = ECOMMUNITY_ROUTE_TARGET;
	memcpy(&eval->val[2], ip, sizeof(struct in_addr));
	eval->val[6] = (val >> 8) & 0xff;
	eval->val[7] = val & 0xff;
}

/*
 * Encode BGP Route Target AS4:nn.
 */
/* 【函数】encode_route_target_as4：编码 "RT 4字节AS:值"（type 0x02，subtype 0x02）。 */
static inline void encode_route_target_as4(as_t as, uint16_t val,
					   struct ecommunity_val *eval,
					   bool trans)
{
	eval->val[0] = ECOMMUNITY_ENCODE_AS4;
	if (!trans)
		eval->val[0] |= ECOMMUNITY_FLAG_NON_TRANSITIVE;
	eval->val[1] = ECOMMUNITY_ROUTE_TARGET;
	eval->val[2] = (as >> 24) & 0xff;
	eval->val[3] = (as >> 16) & 0xff;
	eval->val[4] = (as >> 8) & 0xff;
	eval->val[5] = as & 0xff;
	eval->val[6] = (val >> 8) & 0xff;
	eval->val[7] = val & 0xff;
}

/* Helper function to convert uint32 to IEEE-754 Floating Point */
/* 【函数】uint32_to_ieee_float_uint32：把整数按 IEEE-754 单精度浮点的位模式
 * 重新解释（union 类型双关）。用于 link-bandwidth 的浮点编码。
 */
static uint32_t uint32_to_ieee_float_uint32(uint32_t u)
{
	union {
		float r;
		uint32_t d;
	} f = {.r = (float)u};

	return f.d;
}

/*
 * Encode BGP Link Bandwidth extended community
 *  bandwidth (bw) is in bytes-per-sec
 */
/* 【函数】encode_lb_extcomm：编码 link-bandwidth 扩展社区（type 0x00，
 * subtype 0x04）。带宽 bw 以字节每秒计；disable_ieee_floating 为假时把带宽按
 * IEEE-754 浮点编码，否则按整数编码。
 */
static inline void encode_lb_extcomm(as_t as, uint64_t bw, bool non_trans,
				     struct ecommunity_val *eval,
				     bool disable_ieee_floating)
{
	uint64_t bandwidth = disable_ieee_floating
				     ? bw
				     : uint32_to_ieee_float_uint32(bw);

	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_AS;
	if (non_trans)
		eval->val[0] |= ECOMMUNITY_FLAG_NON_TRANSITIVE;
	eval->val[1] = ECOMMUNITY_LINK_BANDWIDTH;
	eval->val[2] = (as >> 8) & 0xff;
	eval->val[3] = as & 0xff;
	eval->val[4] = (bandwidth >> 24) & 0xff;
	eval->val[5] = (bandwidth >> 16) & 0xff;
	eval->val[6] = (bandwidth >> 8) & 0xff;
	eval->val[7] = bandwidth & 0xff;
}

/*
 * Encode BGP Link Bandwidth inside IPv6 Extended Community,
 * bandwidth is in bytes per second.
 */
/* 【函数】encode_lb_extended_extcomm：编码 extended-link-bandwidth（20 字节，
 * subtype 0x0006）。带宽为 8 字节整数，另附 4 字节 AS。
 */
static inline void encode_lb_extended_extcomm(as_t as, uint64_t bandwidth,
					      bool non_trans,
					      struct ecommunity_val_ipv6 *eval)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_AS4;
	if (non_trans)
		eval->val[0] |= ECOMMUNITY_FLAG_NON_TRANSITIVE;
	eval->val[1] = ECOMMUNITY_EXTENDED_LINK_BANDWIDTH;
	eval->val[4] = (bandwidth >> 56) & 0xff;
	eval->val[5] = (bandwidth >> 48) & 0xff;
	eval->val[6] = (bandwidth >> 40) & 0xff;
	eval->val[7] = (bandwidth >> 32) & 0xff;
	eval->val[8] = (bandwidth >> 24) & 0xff;
	eval->val[9] = (bandwidth >> 16) & 0xff;
	eval->val[10] = (bandwidth >> 8) & 0xff;
	eval->val[11] = bandwidth & 0xff;
	eval->val[12] = (as >> 24) & 0xff;
	eval->val[13] = (as >> 16) & 0xff;
	eval->val[14] = (as >> 8) & 0xff;
	eval->val[15] = as & 0xff;
}

/* 【函数】encode_origin_validation_state：把 RPKI 校验状态编码为 OVS 扩展社区
 * （type 0x43 非传递 opaque，subtype 0x00，状态在最后一个字节）。
 */
static inline void encode_origin_validation_state(enum rpki_states state,
						  struct ecommunity_val *eval)
{
	enum ecommunity_origin_validation_states ovs_state =
		ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTUSED;

	switch (state) {
	case RPKI_VALID:
		ovs_state = ECOMMUNITY_ORIGIN_VALIDATION_STATE_VALID;
		break;
	case RPKI_NOTFOUND:
		ovs_state = ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTFOUND;
		break;
	case RPKI_INVALID:
		ovs_state = ECOMMUNITY_ORIGIN_VALIDATION_STATE_INVALID;
		break;
	case RPKI_NOT_BEING_USED:
		break;
	}

	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_OPAQUE_NON_TRANS;
	eval->val[1] = ECOMMUNITY_ORIGIN_VALIDATION_STATE;
	eval->val[7] = ovs_state;
}

/* 【函数】encode_node_target：编码 node target（subtype 0x09），携带目标
 * BGP Identifier。上面英文注释给出了位布局。
 */
static inline void encode_node_target(struct in_addr *node_id,
				      struct ecommunity_val *eval, bool trans)
{
	/*
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  | 0x01 or 0x41 | Sub-Type(0x09) |    Target BGP Identifier      |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  | Target BGP Identifier (cont.) |           Reserved            |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = trans ? ECOMMUNITY_ENCODE_IP : ECOMMUNITY_ENCODE_IP_NON_TRANS;
	eval->val[1] = ECOMMUNITY_NODE_TARGET;
	memcpy(&eval->val[2], node_id, sizeof(*node_id));
	eval->val[6] = ECOMMUNITY_NODE_TARGET_RESERVED;
	eval->val[7] = ECOMMUNITY_NODE_TARGET_RESERVED;
}

/*
 * Encode BGP Color extended community
 * it's a transitive opaque Extended community (RFC 9256  8.8.1)
 * flag is set to 0
 *
 */
/* 【函数】encode_color：编码 color 扩展社区（RFC 9256，type 0x03，
 * subtype 0x0b）。flags 的高两位是 CO 位。
 */
static inline void encode_color(uint32_t color_id, uint32_t flags, struct ecommunity_val *eval)
{
	/*
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  | 0x03         | Sub-Type(0x0b) |CO |         Flags             |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  |                          Color Value                          |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  https://datatracker.ietf.org/doc/rfc9256/, Section 8.8.1
	 *  The CO bits can have 4 different values: 00 01 10 11
	 */
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_OPAQUE;
	eval->val[1] = ECOMMUNITY_COLOR;
	eval->val[2] = (flags << 6) & 0xff;
	eval->val[3] = 0x00;
	eval->val[4] = (color_id >> 24) & 0xff;
	eval->val[5] = (color_id >> 16) & 0xff;
	eval->val[6] = (color_id >> 8) & 0xff;
	eval->val[7] = color_id & 0xff;
}

/* 【接口分组】
 *   生命周期：ecommunity_init / ecommunity_finish、ecommunity_new、ecommunity_free；
 *   解析构造：ecommunity_parse / ecommunity_parse_ipv6 / ecommunity_str2com /
 *            ecommunity_str2com_ipv6；
 *   驻留共享：ecommunity_intern / ecommunity_unintern；
 *   字符串输出：ecommunity_str（内部缓存）、ecommunity_ecom2str（新分配，需
 *            ecommunity_strfree 释放）；
 *   集合运算：add_val / add_val_ipv6 / merge / uniq_sort / strip / filter / del_val；
 *   匹配判断：include / include_one / match / cmp / has_route_target / lookup；
 *   链路带宽：ecommunity_linkbw_present / ecommunity_replace_linkbw；
 *   聚合相关：bgp_compute_aggregate_ecommunity 系列 / bgp_remove_*。
 */
extern void ecommunity_init(void);
extern void ecommunity_finish(void);
extern void ecommunity_free(struct ecommunity **ecom);
extern struct ecommunity *ecommunity_parse(uint8_t *pnt, unsigned short length,
					   bool disable_ieee_floating);
extern struct ecommunity *ecommunity_parse_ipv6(uint8_t *pnt,
						unsigned short length);
extern struct ecommunity *ecommunity_dup(struct ecommunity *);
extern struct ecommunity *ecommunity_merge(struct ecommunity *ecom1,
					   struct ecommunity *ecom2);
extern struct ecommunity *ecommunity_uniq_sort(struct ecommunity *);
extern struct ecommunity *ecommunity_intern(struct ecommunity *);
extern bool ecommunity_cmp(const void *arg1, const void *arg2);
extern void ecommunity_unintern(struct ecommunity **ecommunity);
extern unsigned int ecommunity_hash_make(const void *arg);
extern struct ecommunity *ecommunity_str2com(const char *str, int type,
					     int keyword_included);
extern struct ecommunity *ecommunity_str2com_ipv6(const char *str, int type,
						  int keyword_included);
extern char *ecommunity_ecom2str(struct ecommunity *ecom, int format, int filter);
extern char *ecommunity_ecom2str_one(struct ecommunity *ecom, int format, int number);
extern bool ecommunity_has_route_target(struct ecommunity *ecom);
extern void ecommunity_strfree(char **s);
extern bool ecommunity_include_one(struct ecommunity *ecom, uint8_t *ptr);
extern bool ecommunity_include(struct ecommunity *e1, struct ecommunity *e2);
extern bool ecommunity_match(const struct ecommunity *ecom1, const struct ecommunity *ecom2);
extern const char *ecommunity_str(struct ecommunity *ecom);
extern struct ecommunity_val *ecommunity_lookup(const struct ecommunity *ecom,
						uint8_t type, uint8_t subtype);

extern uint32_t ecommunity_select_color(const struct ecommunity *ecom);
extern bool ecommunity_add_val(struct ecommunity *ecom,
			       struct ecommunity_val *eval,
			       bool unique, bool overwrite);
extern bool ecommunity_add_val_ipv6(struct ecommunity *ecom,
				    struct ecommunity_val_ipv6 *eval,
				    bool unique, bool overwrite);

/* for vpn */
extern struct ecommunity *ecommunity_new(void);
extern bool ecommunity_strip(struct ecommunity *ecom, uint8_t type,
			     uint8_t subtype);
extern struct ecommunity *ecommunity_new(void);
extern bool ecommunity_strip_non_transitive(struct ecommunity *ecom);
extern struct ecommunity *ecommunity_filter(struct ecommunity *ecom,
					    bool (*filter)(uint8_t *val, uint8_t usize, void *arg),
					    void *arg);
extern bool ecommunity_del_val(struct ecommunity *ecom,
			       struct ecommunity_val *eval);
struct bgp_pbr_entry_action;
extern int ecommunity_fill_pbr_action(struct ecommunity_val *ecom_eval,
				      struct bgp_pbr_entry_action *api,
				      afi_t afi);

extern void bgp_compute_aggregate_ecommunity(
					struct bgp_aggregate *aggregate,
					struct ecommunity *ecommunity);

extern void bgp_compute_aggregate_ecommunity_hash(
					struct bgp_aggregate *aggregate,
					struct ecommunity *ecommunity);
extern void bgp_compute_aggregate_ecommunity_val(
					struct bgp_aggregate *aggregate);
extern void bgp_remove_ecommunity_from_aggregate(
					struct bgp_aggregate *aggregate,
					struct ecommunity *ecommunity);
extern void bgp_remove_ecomm_from_aggregate_hash(
					struct bgp_aggregate *aggregate,
					struct ecommunity *ecommunity);
extern void bgp_aggr_ecommunity_remove(void *arg);
extern const uint8_t *ecommunity_linkbw_present(struct ecommunity *ecom,
						uint64_t *bw);
extern struct ecommunity *
ecommunity_replace_linkbw(as_t as, struct ecommunity *ecom, uint64_t cum_bw,
			  bool disable_ieee_floating, bool extended);

extern bool soo_in_ecom(struct ecommunity *ecom, struct ecommunity *soo);

/* 【内联函数】ecommunity_strip_rts：剥离全部 Route Target 扩展社区
 * （AS / IP / AS4 三种编码都要剥），用于向对端发送前清理 RT。
 */
static inline void ecommunity_strip_rts(struct ecommunity *ecom)
{
	uint8_t subtype = ECOMMUNITY_ROUTE_TARGET;

	ecommunity_strip(ecom, ECOMMUNITY_ENCODE_AS, subtype);
	ecommunity_strip(ecom, ECOMMUNITY_ENCODE_IP, subtype);
	ecommunity_strip(ecom, ECOMMUNITY_ENCODE_AS4, subtype);
}
extern struct ecommunity *
ecommunity_add_origin_validation_state(enum rpki_states rpki_state,
				       struct ecommunity *ecom);
extern struct ecommunity *ecommunity_add_node_target(struct in_addr *node_id,
						     struct ecommunity *old,
						     bool non_trans);
extern bool ecommunity_node_target_match(struct ecommunity *ecomm,
					 struct in_addr *local_id);
#endif /* _QUAGGA_BGP_ECOMMUNITY_H */
