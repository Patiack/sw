// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, LabN Consulting, L.L.C.
 */

/* ============================================================================
 * 【文件导读】bgp_encap_tlv.c —— 隧道封装属性的 TLV 编解码实现
 * ============================================================================
 * 【本文件做什么】把 bgp_encap_types.h 里定义的“隧道类型结构体”与报文里的
 * “sub-TLV 线格式”互相转换。协议是 Tunnel Encapsulation 属性（RFC 9012，
 * 早期为 draft-rosen-idr-tunnel-encaps）：一个属性里串着若干 sub-TLV，每个
 * sub-TLV 由 type / length / value 三段组成。数据定义见 bgp_encap_types.h，
 * 接口声明见 bgp_encap_tlv.h，本文件的术语与这两个头文件保持一致。
 *
 * 【文件布局（上下两大对称半段）】
 *   上半 **ENCODE**（内存变成报文）：
 *     1) SUBTLV ENCODE：subtlv_encode_xxx，把单个 sub-TLV 结构体编码成一个
 *        bgp_attr_encap_subtlv 节点，返回新节点（动态分配）。
 *     2) TUNNEL TYPE-SPECIFIC TLV ENCODE：bgp_encap_type_xxx_to_tlv，按隧道
 *        类型把该类型下各个有效的 sub-TLV 依次挂进 attr->encap_subtlvs 链表，
 *        返回 void。
 *   下半 **DECODE**（报文变成内存）：
 *     3) SUBTLV DECODE：subtlv_decode_xxx，把单个 sub-TLV 的 value 解进结构体，
 *        返回 int 错误码（0 成功，非 0 失败）。
 *     4) TUNNEL TYPE-SPECIFIC TLV DECODE：tlv_to_bgp_encap_type_xxx，遍历整条
 *        sub-TLV 链，按类型分发到上面的单条解码函数，返回 int 错误码。
 *
 * 【方向与返回值差异（最易搞反，务必记牢）】
 *   编码侧函数名以 bgp_encap_type_xxx_to_tlv 结尾，返回 void，不报错；
 *   解码侧函数名以 tlv_to_bgp_encap_type_xxx 开头，返回 int 错误码。
 *   编码侧 bet 是只读输入、attr 是被追加（append）的输出；
 *   解码侧 stlv 是只读输入的链表、bet 由调用方预分配（caller-allocated），
 *   本文件只往里面填值、不负责分配。
 *
 * 【易踩的坑】所有 sub-TLV 的线格式都是**大端**，而结构体字段是主机字节序，
 *   所以编码时手动拆字节、解码时用 ptr_get_be32 转换；remote_endpoint 的 AS 号
 *   无论对端 AS 宽度如何都按 4 字节处理。解码侧遇到不认识的 sub-TLV 会记
 *   rc |= -1 但仍继续走完整条链（向后兼容，不因陌生 TLV 直接中止）。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "memory.h"
#include "prefix.h"
#include "filter.h"
#include "stream.h"

#include "bgpd.h"
#include "bgp_attr.h"

#include "bgp_encap_types.h"
#include "bgp_encap_tlv.h"

/***********************************************************************
 *			SUBTLV ENCODE
 ***********************************************************************/

/* 【编码】把 L2TPv3 over IP 的封装参数编成一个 ENCAPSULATION sub-TLV 节点。
 * 入参 st 是 bgp_tea_subtlv_encap_l2tpv3_over_ip（含 sessionid / cookie_length
 * / cookie）；返回**新分配**的 sub-TLV 节点（type=BGP_ENCAP_SUBTLV_TYPE_
 * ENCAPSULATION），由调用方负责释放。
 * 线格式：4 字节 sessionid（大端）+ cookie_length 字节 cookie。
 * 坑：cookie 数组定长 8 字节但只有前 cookie_length 字节有效，所以长度要单独编；
 * 两个 assert 分别挡住“cookie_length 超过数组容量”和“total 超过一字节长度域”。 */
/* rfc5512 4.1 */
static struct bgp_attr_encap_subtlv *subtlv_encode_encap_l2tpv3_over_ip(
	struct bgp_tea_subtlv_encap_l2tpv3_over_ip *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;
	int total = 4 + st->cookie_length;

	/* sanity check */
	assert(st->cookie_length <= sizeof(st->cookie));
	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION;
	new->length = total;
	p = new->value;

	*p++ = (st->sessionid & 0xff000000) >> 24;
	*p++ = (st->sessionid & 0xff0000) >> 16;
	*p++ = (st->sessionid & 0xff00) >> 8;
	*p++ = (st->sessionid & 0xff);
	memcpy(p, st->cookie, st->cookie_length);
	return new;
}

/* 【编码】把 GRE 的封装参数（只有 4 字节 gre_key）编成一个 ENCAPSULATION
 * sub-TLV 节点，返回新分配节点。
 * 线格式：4 字节 gre_key（大端），主机字节序转大端在这里手工拆字节完成。 */
/* rfc5512 4.1 */
static struct bgp_attr_encap_subtlv *
subtlv_encode_encap_gre(struct bgp_tea_subtlv_encap_gre_key *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;
	int total = 4;

	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION;
	new->length = total;
	p = new->value;

	*p++ = (st->gre_key & 0xff000000) >> 24;
	*p++ = (st->gre_key & 0xff0000) >> 16;
	*p++ = (st->gre_key & 0xff00) >> 8;
	*p++ = (st->gre_key & 0xff);
	return new;
}

/* 【编码】把 PBB(P802.1ah) 的封装参数编成 ENCAPSULATION sub-TLV 节点，返回
 * 新分配节点。
 * 布局：1 字节 flags + 3 字节 isid + 6 字节 MAC + 2 字节 vid（共 12 字节）。
 * flags 字节中 bit7=isid 有效、bit6=vid 有效（与 valid_subtlvs 位图不同，这是
 * **线格式自带**的标志位）。isid 只在前 3 字节真正写出，vid 只写低 12 位。
 * 坑：st 里的 flag_isid / flag_vid 是位域，布局由编译器决定，因此这里逐字段
 * 取值再序列化，而不是把结构体整体 memcpy 出去。 */
static struct bgp_attr_encap_subtlv *
subtlv_encode_encap_pbb(struct bgp_tea_subtlv_encap_pbb *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;
	int total = 1 + 3 + 6 + 2; /* flags + isid + madaddr + vid */

	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION;
	new->length = total;
	p = new->value;

	*p++ = (st->flag_isid ? 0x80 : 0) | (st->flag_vid ? 0x40 : 0) | 0;
	if (st->flag_isid) {
		*p = (st->isid & 0xff0000) >> 16;
		*(p + 1) = (st->isid & 0xff00) >> 8;
		*(p + 2) = (st->isid & 0xff);
	}
	p += 3;
	memcpy(p, st->macaddr, 6);
	p += 6;
	if (st->flag_vid) {
		*p++ = (st->vid & 0xf00) >> 8;
		*p++ = st->vid & 0xff;
	}
	return new;
}

/* 【编码】把 PROTO_TYPE sub-TLV 编码出来，返回新分配节点。
 * 线格式：2 字节 proto（EtherType，大端），表示隧道里承载的以太类型。 */
/* rfc5512 4.2 */
static struct bgp_attr_encap_subtlv *
subtlv_encode_proto_type(struct bgp_tea_subtlv_proto_type *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;
	int total = 2;

	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_PROTO_TYPE;
	new->length = total;
	p = new->value;

	*p++ = (st->proto & 0xff00) >> 8;
	*p++ = (st->proto & 0xff);
	return new;
}

/* 【编码】把 COLOR sub-TLV 编码出来，返回新分配节点。
 * 线格式固定 8 字节：前 4 字节是扩展团体头（0x03 transitivi、0x0b、两个 0 保留），
 * 后 4 字节是 color 值（大端）。color 用于 SD-WAN 里“同色才走同一条隧道”的匹配。
 * 坑：前 4 字节是协议规定的固定前缀，不是任意填充，解码时会核对它。 */
/* rfc5512 4.3 */
static struct bgp_attr_encap_subtlv *
subtlv_encode_color(struct bgp_tea_subtlv_color *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;
	int total = 8;

	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_COLOR;
	new->length = total;
	p = new->value;

	*p++ = 0x03; /* transitive*/
	*p++ = 0x0b;
	*p++ = 0; /* reserved */
	*p++ = 0; /* reserved */

	*p++ = (st->color & 0xff000000) >> 24;
	*p++ = (st->color & 0xff0000) >> 16;
	*p++ = (st->color & 0xff00) >> 8;
	*p++ = (st->color & 0xff);

	return new;
}

/* 【编码】把 IPsec 隧道认证串 (IPSEC_TA) sub-TLV 编码出来，返回新分配节点。
 * 线格式：2 字节 authenticator_type（大端）+ authenticator_length 字节 value。
 * 坑：value 数组定长 20 字节（BGP_ENCAP_SUBTLV_IPSEC_TA_SIZE），只编有效长度；
 * assert 保证 authenticator_length 不超数组容量。 */
/* rfc 5566 4. */
static struct bgp_attr_encap_subtlv *
subtlv_encode_ipsec_ta(struct bgp_tea_subtlv_ipsec_ta *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;
	int total = 2 + st->authenticator_length;

	/* sanity check */
	assert(st->authenticator_length <= sizeof(st->value));
	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_IPSEC_TA;
	new->length = total;
	p = new->value;

	*p++ = (st->authenticator_type & 0xff00) >> 8;
	*p++ = st->authenticator_type & 0xff;
	memcpy(p, st->value, st->authenticator_length);
	return new;
}

/* 【编码】把远端端点 (REMOTE_ENDPOINT) sub-TLV 编码出来，返回新分配节点。
 * 长度由地址族决定：IPv4 为 8 字节（4 字节地址 + 4 字节 AS），IPv6 为 20 字节
 * （16 字节地址 + 4 字节 AS）。AS 号 as4 无论对端 AS 宽度如何都按 4 字节写。
 * 坑：family 决定从 union 里取 v4 还是 v6，取错成员会编出垃圾；非 v4 时断言
 * 必须是 v6，避免出现第三种未处理的情况。 */
/* draft-rosen-idr-tunnel-encaps 2.1 */
static struct bgp_attr_encap_subtlv *
subtlv_encode_remote_endpoint(struct bgp_tea_subtlv_remote_endpoint *st)
{
	struct bgp_attr_encap_subtlv *new;
	uint8_t *p;

	int total = (st->family == AF_INET ? 8 : 20);

	assert(total <= 0xff);

	new = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + total);
	assert(new);
	new->type = BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT;
	new->length = total;
	p = new->value;
	if (st->family == AF_INET) {
		memcpy(p, &(st->ip_address.v4.s_addr), IPV4_MAX_BYTELEN);
		p += IPV4_MAX_BYTELEN;
	} else {
		assert(st->family == AF_INET6);
		memcpy(p, &(st->ip_address.v6.s6_addr), IPV6_MAX_BYTELEN);
		p += IPV6_MAX_BYTELEN;
	}
	memcpy(p, &(st->as4), 4);
	return new;
}

/***********************************************************************
 *		TUNNEL TYPE-SPECIFIC TLV ENCODE
 ***********************************************************************/

/*
 * requires "extra" and "last" to be defined in caller
 */
/* 【宏】ENC_SUBTLV —— 编码半段的“按位图追加一个 sub-TLV”帮手，省掉每个隧道
 * 类型函数里重复的链表追加代码。
 *   flag   : valid_subtlvs 里的标志位，置位表示该 sub-TLV 存在、需要编码。
 *   function: 单条编码函数（subtlv_encode_xxx）。
 *   field  : bet 里对应的字段（如 st_encap / st_proto）。
 * 依赖调用方在作用域里已定义两个变量：last（当前链表尾节点，初始可能为 NULL）
 * 与 attr（输出属性）。若 last 非空就接到它后面，否则写入 attr->encap_subtlvs
 * 作为链表头；追加后把 last 前移。
 * 坑：本宏只在 flag 已置位时才编码；last 初值必须是 attr->encap_subtlvs 现有的
 * 尾节点，否则会把已有 sub-TLV 覆盖掉（VXLAN 是唯一例外，见其函数说明）。 */
#define ENC_SUBTLV(flag, function, field)                                      \
	do {                                                                   \
		struct bgp_attr_encap_subtlv *new;                             \
		if (CHECK_FLAG(bet->valid_subtlvs, (flag))) {                  \
			new = function(&bet->field);                           \
			if (last) {                                            \
				last->next = new;                              \
			} else {                                               \
				attr->encap_subtlvs = new;                     \
			}                                                      \
			last = new;                                            \
		}                                                              \
	} while (0)

/* 【编码/隧道级】L2TPv3 over IP：把该类型下有效的 sub-TLV 依次追加进 attr。
 * 先把 encap_tunneltype 置为本类型号，再把链表尾 last 推进到现有末尾，然后按
 * valid_subtlvs 位图依次编码 ENCAPSULATION（断言必有）/ PROTO_TYPE / COLOR /
 * REMOTE_ENDPOINT。返回 void，不报错。 */
void bgp_encap_type_l2tpv3overip_to_tlv(
	struct bgp_encap_type_l2tpv3_over_ip *bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_L2TPV3_OVER_IP;

	assert(CHECK_FLAG(bet->valid_subtlvs, BGP_TEA_SUBTLV_ENCAP));

	ENC_SUBTLV(BGP_TEA_SUBTLV_ENCAP, subtlv_encode_encap_l2tpv3_over_ip,
		   st_encap);
	ENC_SUBTLV(BGP_TEA_SUBTLV_PROTO_TYPE, subtlv_encode_proto_type,
		   st_proto);
	ENC_SUBTLV(BGP_TEA_SUBTLV_COLOR, subtlv_encode_color, st_color);
	ENC_SUBTLV(BGP_TEA_SUBTLV_REMOTE_ENDPOINT,
		   subtlv_encode_remote_endpoint, st_endpoint);
}

/* 【编码/隧道级】GRE：置类型号后，按位图追加 ENCAPSULATION（GRE key）/
 * PROTO_TYPE / COLOR / REMOTE_ENDPOINT。返回 void。 */
void bgp_encap_type_gre_to_tlv(
	struct bgp_encap_type_gre *bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_GRE;

	ENC_SUBTLV(BGP_TEA_SUBTLV_ENCAP, subtlv_encode_encap_gre, st_encap);
	ENC_SUBTLV(BGP_TEA_SUBTLV_PROTO_TYPE, subtlv_encode_proto_type,
		   st_proto);
	ENC_SUBTLV(BGP_TEA_SUBTLV_COLOR, subtlv_encode_color, st_color);
	ENC_SUBTLV(BGP_TEA_SUBTLV_REMOTE_ENDPOINT,
		   subtlv_encode_remote_endpoint, st_endpoint);
}

/* 【编码/隧道级】IP-in-IP：没有自己的封装参数，只按位图追加 PROTO_TYPE /
 * COLOR / REMOTE_ENDPOINT。返回 void。 */
void bgp_encap_type_ip_in_ip_to_tlv(
	struct bgp_encap_type_ip_in_ip *bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_IP_IN_IP;

	ENC_SUBTLV(BGP_TEA_SUBTLV_PROTO_TYPE, subtlv_encode_proto_type,
		   st_proto);
	ENC_SUBTLV(BGP_TEA_SUBTLV_COLOR, subtlv_encode_color, st_color);
	ENC_SUBTLV(BGP_TEA_SUBTLV_REMOTE_ENDPOINT,
		   subtlv_encode_remote_endpoint, st_endpoint);
}

/* 【编码/隧道级】Transmit Tunnel Endpoint：只置类型号，没有 sub-TLV 要编
 * （函数体里那句英文注释即此意）。返回 void。 */
void bgp_encap_type_transmit_tunnel_endpoint(
	struct bgp_encap_type_transmit_tunnel_endpoint
		*bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_TRANSMIT_TUNNEL_ENDPOINT;

	/* no subtlvs for this type */
}

/* 【编码/隧道级】IPsec in Tunnel Mode：置类型号后只追加 IPSEC_TA sub-TLV。
 * 注意本类型**不编码 REMOTE_ENDPOINT**（与结构体注释里列的组合一致）。返回 void。 */
void bgp_encap_type_ipsec_in_tunnel_mode_to_tlv(
	struct bgp_encap_type_ipsec_in_tunnel_mode *bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_IPSEC_IN_TUNNEL_MODE;

	ENC_SUBTLV(BGP_TEA_SUBTLV_IPSEC_TA, subtlv_encode_ipsec_ta,
		   st_ipsec_ta);
}

/* 【编码/隧道级】IP-in-IP tunnel with IPsec transport mode：置类型号后追加
 * IPSEC_TA sub-TLV。返回 void。 */
void bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode_to_tlv(
	struct bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode
		*bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype =
		BGP_ENCAP_TYPE_IP_IN_IP_TUNNEL_WITH_IPSEC_TRANSPORT_MODE;

	ENC_SUBTLV(BGP_TEA_SUBTLV_IPSEC_TA, subtlv_encode_ipsec_ta,
		   st_ipsec_ta);
}

/* 【编码/隧道级】MPLS-in-IP tunnel with IPsec transport mode：置类型号后追加
 * IPSEC_TA sub-TLV。返回 void。 */
void bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode_to_tlv(
	struct bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode
		*bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype =
		BGP_ENCAP_TYPE_MPLS_IN_IP_TUNNEL_WITH_IPSEC_TRANSPORT_MODE;

	ENC_SUBTLV(BGP_TEA_SUBTLV_IPSEC_TA, subtlv_encode_ipsec_ta,
		   st_ipsec_ta);
}

/* 【编码/隧道级】PBB：置类型号后编码 ENCAPSULATION（PBB 的 isid/vid/MAC，
 * 断言必有），本类型只支持这一个 sub-TLV。返回 void。 */
void bgp_encap_type_pbb_to_tlv(
	struct bgp_encap_type_pbb *bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *last;

	/* advance to last subtlv */
	for (last = attr->encap_subtlvs; last && last->next; last = last->next)
		;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_PBB;

	assert(CHECK_FLAG(bet->valid_subtlvs, BGP_TEA_SUBTLV_ENCAP));
	ENC_SUBTLV(BGP_TEA_SUBTLV_ENCAP, subtlv_encode_encap_pbb, st_encap);
}

/* 【编码/隧道级】VXLAN：本函数与上面几个**写法不同**——它不走 ENC_SUBTLV 宏，
 * 而是**整体重建**单个 sub-TLV：先置类型号，若 bet 为空或 vnid 为 0 直接返回；
 * 否则先 XFREE 掉 attr 里已有的 encap_subtlvs（**注意：这会丢弃此前已编码的
 * sub-TLV**，是本类型特有的覆盖式写法），再分配 12 字节节点填 type=1、len=12。
 * 线格式：4 字节 vnid（主机序转大端，并 OR 上 VXLAN_ENCAP_MASK_VNID_VALID 置
 * Valid 位）+ 6 字节可选 MAC。返回 void。
 * 坑：这里的 vnid 是“已去掉标志位的纯 VNI”，编码时才补回 V 位，与解码侧对应。 */
void bgp_encap_type_vxlan_to_tlv(
	struct bgp_encap_type_vxlan *bet, /* input structure */
	struct attr *attr)
{
	struct bgp_attr_encap_subtlv *tlv;
	uint32_t vnid;

	attr->encap_tunneltype = BGP_ENCAP_TYPE_VXLAN;

	if (bet == NULL || !bet->vnid)
		return;
	XFREE(MTYPE_ENCAP_TLV, attr->encap_subtlvs);
	tlv = XCALLOC(MTYPE_ENCAP_TLV,
		      sizeof(struct bgp_attr_encap_subtlv) + 12);
	tlv->type = 1; /* encapsulation type */
	tlv->length = 12;
	if (bet->vnid) {
		vnid = htonl(bet->vnid | VXLAN_ENCAP_MASK_VNID_VALID);
		memcpy(&tlv->value, &vnid, 4);
	}
	if (bet->mac_address) {
		char *ptr = (char *)&tlv->value + 4;
		memcpy(ptr, bet->mac_address, 6);
	}
	attr->encap_subtlvs = tlv;
	return;
}

/* 【编码/隧道级】NVGRE：只置类型号，无 sub-TLV 可编。返回 void。 */
void bgp_encap_type_nvgre_to_tlv(
	struct bgp_encap_type_nvgre *bet, /* input structure */
	struct attr *attr)
{
	attr->encap_tunneltype = BGP_ENCAP_TYPE_NVGRE;
}

/* 【编码/隧道级】MPLS：**直接 return**，既不置类型号也不编码（英文注释明确说
 * MPLS 不用这个封装属性）。返回 void。 */
void bgp_encap_type_mpls_to_tlv(
	struct bgp_encap_type_mpls *bet, /* input structure */
	struct attr *attr)
{
	return; /* no encap attribute for MPLS */
}

/* 【编码/隧道级】MPLS-in-GRE：只置类型号，无 sub-TLV 可编。返回 void。 */
void bgp_encap_type_mpls_in_gre_to_tlv(
	struct bgp_encap_type_mpls_in_gre *bet, /* input structure */
	struct attr *attr)
{
	attr->encap_tunneltype = BGP_ENCAP_TYPE_MPLS_IN_GRE;
}

/* 【编码/隧道级】VXLAN-GPE：只置类型号，无 sub-TLV 可编。返回 void。 */
void bgp_encap_type_vxlan_gpe_to_tlv(
	struct bgp_encap_type_vxlan_gpe *bet, /* input structure */
	struct attr *attr)
{

	attr->encap_tunneltype = BGP_ENCAP_TYPE_VXLAN_GPE;
}

/* 【编码/隧道级】MPLS-in-UDP：只置类型号，无 sub-TLV 可编。返回 void。 */
void bgp_encap_type_mpls_in_udp_to_tlv(
	struct bgp_encap_type_mpls_in_udp *bet, /* input structure */
	struct attr *attr)
{

	attr->encap_tunneltype = BGP_ENCAP_TYPE_MPLS_IN_UDP;
}


/***********************************************************************
 *			SUBTLV DECODE
 ***********************************************************************/
/* 【解码】把单个 ENCAPSULATION sub-TLV 的 value 解进 L2TPv3 结构体 st。
 * 返回 0 成功，-1 失败（长度 <4，或 cookie 长度超过数组容量）。
 * 坑：cookie_length 由 length 反推（length 减 4），因此长度必须自洽；失败时会
 * 打 zlog_debug 但不改 st 已解出的部分（调用方应据返回值决定是否采用）。 */
/* rfc5512 4.1 */
static int subtlv_decode_encap_l2tpv3_over_ip(
	struct bgp_attr_encap_subtlv *subtlv,
	struct bgp_tea_subtlv_encap_l2tpv3_over_ip *st)
{
	if (subtlv->length < 4) {
		zlog_debug("%s, subtlv length %d is less than 4", __func__,
			   subtlv->length);
		return -1;
	}

	ptr_get_be32(subtlv->value, &st->sessionid);
	st->cookie_length = subtlv->length - 4;
	if (st->cookie_length > sizeof(st->cookie)) {
		zlog_debug("%s, subtlv length %d is greater than %d", __func__,
			   st->cookie_length, (int)sizeof(st->cookie));
		return -1;
	}
	memcpy(st->cookie, subtlv->value + 4, st->cookie_length);
	return 0;
}

/* 【解码】把 ENCAPSULATION sub-TLV 解进 GRE 结构体 st。返回 0 成功；
 * -1 失败（length 必须恰为 4）。ptr_get_be32 把大端 4 字节转成主机序 gre_key。 */
/* rfc5512 4.1 */
static int subtlv_decode_encap_gre(struct bgp_attr_encap_subtlv *subtlv,
				   struct bgp_tea_subtlv_encap_gre_key *st)
{
	if (subtlv->length != 4) {
		zlog_debug("%s, subtlv length %d does not equal 4", __func__,
			   subtlv->length);
		return -1;
	}
	ptr_get_be32(subtlv->value, &st->gre_key);
	return 0;
}

/* 【解码】把 ENCAPSULATION sub-TLV 解进 PBB 结构体 st。返回 0 成功；
 * -1 失败（length 不等于 12）。value[0] 的 bit7/bit6 分别决定 isid/vid 是否有效：
 * 仅当有效位为 1 时才置对应 flag 字段并取 isid(3 字节)/vid(低 12 位)，MAC 恒取。
 * 坑：无效位对应的字段**不会写入**，保持调用方清零后的值，读取前须查 flag。 */
static int subtlv_decode_encap_pbb(struct bgp_attr_encap_subtlv *subtlv,
				   struct bgp_tea_subtlv_encap_pbb *st)
{
	if (subtlv->length != 1 + 3 + 6 + 2) {
		zlog_debug("%s, subtlv length %d does not equal %d", __func__,
			   subtlv->length, 1 + 3 + 6 + 2);
		return -1;
	}
	if (subtlv->value[0] & 0x80) {
		st->flag_isid = 1;
		st->isid = (subtlv->value[1] << 16) | (subtlv->value[2] << 8)
			   | subtlv->value[3];
	}
	if (subtlv->value[0] & 0x40) {
		st->flag_vid = 1;
		st->vid = ((subtlv->value[10] & 0x0f) << 8) | subtlv->value[11];
	}
	memcpy(st->macaddr, subtlv->value + 4, 6);
	return 0;
}

/* 【解码】把 PROTO_TYPE sub-TLV 解进结构体 st。返回 0 成功；-1 失败
 * （length 必须恰为 2）。proto 为 EtherType（大端合成）。 */
/* rfc5512 4.2 */
static int subtlv_decode_proto_type(struct bgp_attr_encap_subtlv *subtlv,
				    struct bgp_tea_subtlv_proto_type *st)
{
	if (subtlv->length != 2) {
		zlog_debug("%s, subtlv length %d does not equal 2", __func__,
			   subtlv->length);
		return -1;
	}
	st->proto = (subtlv->value[0] << 8) | subtlv->value[1];
	return 0;
}

/* 【解码】把 COLOR sub-TLV 解进结构体 st。返回 0 成功；-1 失败
 * （length 不等于 8，或前 4 字节不是固定前缀 0x030b0000）。
 * 坑：会**显式核对**前 4 字节前缀，前缀不符即判失败——与编码侧写死的那 4 字节
 * 对称，说明这里不信任报文格式。 */
/* rfc5512 4.3 */
static int subtlv_decode_color(struct bgp_attr_encap_subtlv *subtlv,
			       struct bgp_tea_subtlv_color *st)
{
	if (subtlv->length != 8) {
		zlog_debug("%s, subtlv length %d does not equal 8", __func__,
			   subtlv->length);
		return -1;
	}
	if ((subtlv->value[0] != 0x03) || (subtlv->value[1] != 0x0b)
	    || (subtlv->value[2] != 0) || (subtlv->value[3] != 0)) {
		zlog_debug("%s, subtlv value 1st 4 bytes are not 0x030b0000",
			   __func__);
		return -1;
	}
	ptr_get_be32(subtlv->value + 4, &st->color);
	return 0;
}

/* 【解码】把 IPSEC_TA sub-TLV 解进结构体 st。返回 0 成功；-1 失败
 * （认证串长度超过数组容量）。authenticator_length 由 length 减 2 反推，
 * authenticator_type 取前 2 字节（大端）。
 * 坑：**没有检查 length 至少为 2**，若 length < 2 则长度为负，会落到容量检查
 * 之外的路径，是潜在边界点（读代码时留意）。 */
/* rfc 5566 4. */
static int subtlv_decode_ipsec_ta(struct bgp_attr_encap_subtlv *subtlv,
				  struct bgp_tea_subtlv_ipsec_ta *st)
{
	st->authenticator_length = subtlv->length - 2;
	if (st->authenticator_length > sizeof(st->value)) {
		zlog_debug(
			"%s, authenticator length %d exceeds storage maximum %d",
			__func__, st->authenticator_length,
			(int)sizeof(st->value));
		return -1;
	}
	st->authenticator_type = (subtlv->value[0] << 8) | subtlv->value[1];
	memcpy(st->value, subtlv->value + 2, st->authenticator_length);
	return 0;
}

/* 【解码】把 REMOTE_ENDPOINT sub-TLV 解进结构体 st。返回 0 成功；-1 失败
 * （length 既不是 8 也不是 20）。length==8 视为 IPv4、否则 IPv6，据此设 family
 * 并复制地址；AS 号取**最后 4 字节**（i = length - 4），无论地址族都按 4 字节。
 * 坑：family 与地址复制是强耦合的，改了长度判定要同步改 family 赋值。 */
/* draft-rosen-idr-tunnel-encaps 2.1 */
static int
subtlv_decode_remote_endpoint(struct bgp_attr_encap_subtlv *subtlv,
			      struct bgp_tea_subtlv_remote_endpoint *st)
{
	int i;
	if (subtlv->length != 8 && subtlv->length != 20) {
		zlog_debug("%s, subtlv length %d does not equal 8 or 20",
			   __func__, subtlv->length);
		return -1;
	}
	if (subtlv->length == 8) {
		st->family = AF_INET;
		memcpy(&st->ip_address.v4.s_addr, subtlv->value,
		       IPV4_MAX_BYTELEN);
	} else {
		st->family = AF_INET6;
		memcpy(&(st->ip_address.v6.s6_addr), subtlv->value,
		       IPV6_MAX_BYTELEN);
	}
	i = subtlv->length - 4;
	ptr_get_be32(subtlv->value + i, &st->as4);
	return 0;
}

/***********************************************************************
 *		TUNNEL TYPE-SPECIFIC TLV DECODE
 ***********************************************************************/

/* 【解码/隧道级】L2TPv3 over IP：遍历 sub-TLV 链 stlv，按 type 分发到
 * subtlv_decode_encap_l2tpv3_over_ip / proto_type / color / remote_endpoint，
 * 每解成功一条就 SET_SUBTLV_FLAG 置对应有效位。返回累计错误码 rc（0 全成功；
 * 遇到**不认识**的 type 时 rc |= -1 但仍继续走链）。bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_l2tpv3overip(
	struct bgp_attr_encap_subtlv *stlv,	/* subtlv chain */
	struct bgp_encap_type_l2tpv3_over_ip *bet) /* caller-allocated */
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION:
			rc |= subtlv_decode_encap_l2tpv3_over_ip(
				st, &bet->st_encap);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_ENCAP);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_PROTO_TYPE:
			rc |= subtlv_decode_proto_type(st, &bet->st_proto);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_PROTO_TYPE);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_COLOR:
			rc |= subtlv_decode_color(st, &bet->st_color);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_COLOR);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】GRE：遍历 sub-TLV 链，按 type 分发到 ENCAPSULATION(GRE key) /
 * PROTO_TYPE / COLOR / REMOTE_ENDPOINT，成功即置位。返回累计错误码 rc，遇未知
 * 类型记 -1 但继续。bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_gre(
	struct bgp_attr_encap_subtlv *stlv, /* subtlv chain */
	struct bgp_encap_type_gre *bet)     /* caller-allocated */
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION:
			rc |= subtlv_decode_encap_gre(st, &bet->st_encap);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_ENCAP);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_PROTO_TYPE:
			rc |= subtlv_decode_proto_type(st, &bet->st_proto);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_PROTO_TYPE);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_COLOR:
			rc |= subtlv_decode_color(st, &bet->st_color);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_COLOR);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】IP-in-IP：遍历 sub-TLV 链，按 type 分发到 PROTO_TYPE /
 * COLOR / REMOTE_ENDPOINT（本类型无封装参数）。返回累计错误码 rc，遇未知类型
 * 记 -1 但继续。bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_ip_in_ip(
	struct bgp_attr_encap_subtlv *stlv,  /* subtlv chain */
	struct bgp_encap_type_ip_in_ip *bet) /* caller-allocated */
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_PROTO_TYPE:
			rc |= subtlv_decode_proto_type(st, &bet->st_proto);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_PROTO_TYPE);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_COLOR:
			rc |= subtlv_decode_color(st, &bet->st_color);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_COLOR);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】Transmit Tunnel Endpoint：本类型**只认 REMOTE_ENDPOINT**
 * 一种 sub-TLV，其余一律落到 default 记 rc |= -1。返回累计错误码 rc。
 * bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_transmit_tunnel_endpoint(
	struct bgp_attr_encap_subtlv *stlv,
	struct bgp_encap_type_transmit_tunnel_endpoint *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】IPsec in Tunnel Mode：遍历 sub-TLV 链，分发到 IPSEC_TA /
 * REMOTE_ENDPOINT 两种，成功即置位。返回累计错误码 rc，遇未知类型记 -1 但继续。
 * bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_ipsec_in_tunnel_mode(
	struct bgp_attr_encap_subtlv *stlv,		 /* subtlv chain */
	struct bgp_encap_type_ipsec_in_tunnel_mode *bet) /* caller-allocated */
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_IPSEC_TA:
			rc |= subtlv_decode_ipsec_ta(st, &bet->st_ipsec_ta);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_IPSEC_TA);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】IP-in-IP tunnel with IPsec transport mode：遍历链，分发到
 * IPSEC_TA / REMOTE_ENDPOINT。返回累计错误码 rc，遇未知类型记 -1 但继续。
 * bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode(
	struct bgp_attr_encap_subtlv *stlv,
	struct bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_IPSEC_TA:
			rc |= subtlv_decode_ipsec_ta(st, &bet->st_ipsec_ta);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_IPSEC_TA);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】MPLS-in-IP tunnel with IPsec transport mode：遍历链，分发到
 * IPSEC_TA / REMOTE_ENDPOINT。返回累计错误码 rc，遇未知类型记 -1 但继续。
 * bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode(
	struct bgp_attr_encap_subtlv *stlv,
	struct bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_IPSEC_TA:
			rc |= subtlv_decode_ipsec_ta(st, &bet->st_ipsec_ta);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_IPSEC_TA);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】VXLAN：本类型只认 REMOTE_ENDPOINT 一种 sub-TLV。
 * 注意：这里**没有**处理 vnid 的 Valid/MAC 标志位（本函数的 switch 只有 endpoint
 * 分支），所以结构体里的 vnid / mac_address 不会被本函数填充。返回累计错误码 rc。 */
int tlv_to_bgp_encap_type_vxlan(struct bgp_attr_encap_subtlv *stlv,
				struct bgp_encap_type_vxlan *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】NVGRE：只认 REMOTE_ENDPOINT 一种 sub-TLV。返回累计错误码 rc。 */
int tlv_to_bgp_encap_type_nvgre(struct bgp_attr_encap_subtlv *stlv,
				struct bgp_encap_type_nvgre *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】MPLS：只认 REMOTE_ENDPOINT 一种 sub-TLV。返回累计错误码 rc。 */
int tlv_to_bgp_encap_type_mpls(struct bgp_attr_encap_subtlv *stlv,
			       struct bgp_encap_type_mpls *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】MPLS-in-GRE：只认 REMOTE_ENDPOINT 一种 sub-TLV。
 * 返回累计错误码 rc。 */
int tlv_to_bgp_encap_type_mpls_in_gre(struct bgp_attr_encap_subtlv *stlv,
				      struct bgp_encap_type_mpls_in_gre *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】VXLAN-GPE：只认 REMOTE_ENDPOINT 一种 sub-TLV。
 * 返回累计错误码 rc。 */
int tlv_to_bgp_encap_type_vxlan_gpe(struct bgp_attr_encap_subtlv *stlv,
				    struct bgp_encap_type_vxlan_gpe *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】MPLS-in-UDP：只认 REMOTE_ENDPOINT 一种 sub-TLV。
 * 返回累计错误码 rc。 */
int tlv_to_bgp_encap_type_mpls_in_udp(struct bgp_attr_encap_subtlv *stlv,
				      struct bgp_encap_type_mpls_in_udp *bet)
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}

/* 【解码/隧道级】PBB：遍历 sub-TLV 链，分发到 ENCAPSULATION（PBB 参数）/
 * REMOTE_ENDPOINT 两种，成功即置位。返回累计错误码 rc，遇未知类型记 -1 但继续。
 * bet 由调用方预分配。 */
int tlv_to_bgp_encap_type_pbb(
	struct bgp_attr_encap_subtlv *stlv, /* subtlv chain */
	struct bgp_encap_type_pbb *bet)     /* caller-allocated */
{
	struct bgp_attr_encap_subtlv *st;
	int rc = 0;

	for (st = stlv; st; st = st->next) {
		switch (st->type) {
		case BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION:
			rc |= subtlv_decode_encap_pbb(st, &bet->st_encap);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_ENCAP);
			break;

		case BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT:
			rc |= subtlv_decode_remote_endpoint(st,
							    &bet->st_endpoint);
			SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_REMOTE_ENDPOINT);
			break;

		default:
			zlog_debug("%s: unexpected subtlv type %d", __func__,
				   st->type);
			rc |= -1;
			break;
		}
	}
	return rc;
}
