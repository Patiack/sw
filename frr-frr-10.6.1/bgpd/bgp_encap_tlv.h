// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, LabN Consulting, L.L.C.
 */

#ifndef _QUAGGA_BGP_ENCAP_TLV_H
#define _QUAGGA_BGP_ENCAP_TLV_H

/* ============================================================================
 * 【文件导读】bgp_encap_tlv.h —— 隧道封装属性的 TLV 编解码接口
 * ============================================================================
 * 【本文件与 bgp_encap_types.h 的分工】那边只有**数据定义**（隧道类型号、sub-TLV
 * 类型号、各结构体）；这里放的是**编解码函数**，把结构体 ↔ 线格式 TLV 互相转换。
 * 所以读的时候要对照着看：结构体在 types.h，读写它的代码在 bgp_encap_tlv.c。
 *
 * 【两段式布局（本文件的结构性提示）】文件被分成对称的两半：
 *   上半 **ENCODE**：`bgp_encap_type_xxx_to_tlv(bet, attr)`
 *        —— 把内存里的隧道描述**写进路由属性** attr（准备发给对端）。
 *   下半 **DECODE**：`tlv_to_bgp_encap_type_xxx(stlv, bet)`
 *        —— 把收到的 **sub-TLV 链**解成结构体。
 * 注意两半的**方向、参数顺序、返回值**都不同，看函数名时先判方向：
 *   名字以 `bgp_encap_type_xxx_to_tlv` 结尾 → 编码（内存 → 报文）；
 *   名字以 `tlv_to_bgp_encap_type_xxx` 开头 → 解码（报文 → 内存）。
 * 两半的函数**一一对应 15 种隧道类型**，所以本文件长而重复——抓住对应关系即可，
 * 不必逐条记。
 *
 * 【参数约定（两半都不一样，容易搞反）】
 *   编码：bet 是**输入**（要编码的内容），attr 是**输出**（写进这个属性）。
 *   解码：stlv 是**输入**（报文中解出的 sub-TLV 链表），bet 由**调用方预先分配**
 *        （原注释 "caller-allocated" 就是这个意思——本函数**不分配** bet，
 *        所以调用方要自己清空并保证结构体的大小正确）。
 * 为什么解码要调用方分配：调用方是按“隧道类型号”分发到对应函数的，它才知道该用
 * 哪个结构体，由它分配最自然，也避免了返回 void * 再转换的类型风险。
 *
 * 【返回值差异】编码函数返回 void（**不报错**，遇到问题只能内部处理或忽略）；
 * 解码函数返回 int（**错误码**，非 0 表示解析失败）。因此解码路径的错误处理是
 * 显式的、编码路径几乎没有——读 code 时别以为编码函数“一定会成功”。
 *
 * 【命名上的不一致（读代码时会撞到）】编码侧 `bgp_encap_type_l2tpv3overip_to_tlv`
 * 里是 "l2tpv3overip"（无下划线），而结构体名与解码侧都是 `l2tpv3_over_ip`
 * （有下划线）。这是历史遗留的拼写差异，不是两个不同的类型。
 * ============================================================================
 */

/***********************************************************************
 *		TUNNEL TYPE-SPECIFIC TLV ENCODE
 ***********************************************************************/
/* 【编码半段】下面每个函数把一种隧道类型的结构体编码成 sub-TLV 写进 attr。
 * 共同点：入参 bet 只读、attr 被追加内容（注意是**追加**——一个 Tunnel
 * Encapsulation 属性里可以带多个隧道类型，所以这些函数不覆盖 attr 的既有内容）；
 * 返回 void 无错误报告。调用方必须先确认 bet->valid_subtlvs 里该有的位都已置位，
 * 否则会编码出缺字段的错误 TLV（本函数不做这种校验）。详见文件头导读。 */

extern void
bgp_encap_type_l2tpv3overip_to_tlv(struct bgp_encap_type_l2tpv3_over_ip *bet,
				   struct attr *attr);

extern void bgp_encap_type_gre_to_tlv(struct bgp_encap_type_gre *bet,
				      struct attr *attr);

extern void bgp_encap_type_ip_in_ip_to_tlv(struct bgp_encap_type_ip_in_ip *bet,
					   struct attr *attr);

extern void bgp_encap_type_transmit_tunnel_endpoint(
	struct bgp_encap_type_transmit_tunnel_endpoint *bet, struct attr *attr);

extern void bgp_encap_type_ipsec_in_tunnel_mode_to_tlv(
	struct bgp_encap_type_ipsec_in_tunnel_mode *bet, struct attr *attr);

extern void bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode_to_tlv(
	struct bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode *bet,
	struct attr *attr);

extern void bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode_to_tlv(
	struct bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode *bet,
	struct attr *attr);

extern void bgp_encap_type_pbb_to_tlv(struct bgp_encap_type_pbb *bet,
				      struct attr *attr);

extern void bgp_encap_type_vxlan_to_tlv(struct bgp_encap_type_vxlan *bet,
					struct attr *attr);

extern void bgp_encap_type_nvgre_to_tlv(struct bgp_encap_type_nvgre *bet,
					struct attr *attr);

extern void bgp_encap_type_mpls_to_tlv(struct bgp_encap_type_mpls *bet,
				       struct attr *attr);

extern void
bgp_encap_type_mpls_in_gre_to_tlv(struct bgp_encap_type_mpls_in_gre *bet,
				  struct attr *attr);

extern void
bgp_encap_type_vxlan_gpe_to_tlv(struct bgp_encap_type_vxlan_gpe *bet,
				struct attr *attr);

extern void
bgp_encap_type_mpls_in_udp_to_tlv(struct bgp_encap_type_mpls_in_udp *bet,
				  struct attr *attr);

/***********************************************************************
 *		TUNNEL TYPE-SPECIFIC TLV DECODE
 ***********************************************************************/

/* 【解码半段】下面每个函数把**sub-TLV 链表**解成对应结构体。
 * 每行注释里的两个提示词是本段的阅读关键：
 *   "subtlv chain"     : 入参 stlv 是一条**链表**（同一隧道类型下可能带多个
 *                        sub-TLV，解析时逐个走链），不是单个 TLV——所以函数内部
 *                        是按链遍历并只看自己关心的那几个类型。
 *   "caller-allocated" : bet 由调用方分配（见文件头导读），函数只填内容。
 * 共同点：返回 int 错误码，非 0 表示失败；处理 unknown / 顺序异常的 sub-TLV 时的
 * 策略读实现（协议要求必须能忽略不认识的 sub-TLV，这是向后兼容的关键）。
 *
 * 注：本段下面的 tlv_to_bgp_encap_type_mpls 被**连续声明了两次**（内容完全相同）。
 * 重复声明本身合法、无害（C 允许同一函数被多次声明），推测是复制粘贴遗留；
 * 不影响使用，保留原样不动。 */

/* 【解码】L2TPv3 over IP 隧道。stlv 是 sub-TLV 链表（可能含多个 sub-TLV），
 * bet 由调用方分配；把相关 sub-TLV 解入 bet，返回错误码（非 0 表示失败）。 */
extern int tlv_to_bgp_encap_type_l2tpv3overip(
	struct bgp_attr_encap_subtlv *stlv,	 /* subtlv chain */
	struct bgp_encap_type_l2tpv3_over_ip *bet); /* caller-allocated */

/* 【解码】GRE 隧道。stlv 为 sub-TLV 链表，bet 调用方分配，返回错误码。 */
extern int tlv_to_bgp_encap_type_gre(
	struct bgp_attr_encap_subtlv *stlv, /* subtlv chain */
	struct bgp_encap_type_gre *bet);    /* caller-allocated */

/* 【解码】IP-in-IP 隧道。stlv 为 sub-TLV 链表，bet 调用方分配，返回错误码。 */
extern int tlv_to_bgp_encap_type_ip_in_ip(
	struct bgp_attr_encap_subtlv *stlv,   /* subtlv chain */
	struct bgp_encap_type_ip_in_ip *bet); /* caller-allocated */

/* 【解码】Transmit Tunnel Endpoint 子 TLV（隧道端点的对端地址）。
 * stlv 为 sub-TLV 链表，bet 调用方分配，返回错误码。 */
extern int tlv_to_bgp_encap_type_transmit_tunnel_endpoint(
	struct bgp_attr_encap_subtlv *stlv,
	struct bgp_encap_type_transmit_tunnel_endpoint *bet);

/* 【解码】IPsec in Tunnel Mode 隧道。stlv 链表，bet 调用方分配，返回错误码。 */
extern int tlv_to_bgp_encap_type_ipsec_in_tunnel_mode(
	struct bgp_attr_encap_subtlv *stlv,		  /* subtlv chain */
	struct bgp_encap_type_ipsec_in_tunnel_mode *bet); /* caller-allocated */

/* 【解码】IP-in-IP 隧道加 IPsec Transport Mode。stlv 链表，bet 调用方分配。 */
extern int tlv_to_bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode(
	struct bgp_attr_encap_subtlv *stlv,
	struct bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode *bet);

/* 【解码】MPLS-in-IP 隧道加 IPsec Transport Mode。stlv 链表，bet 调用方分配。 */
extern int tlv_to_bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode(
	struct bgp_attr_encap_subtlv *stlv,
	struct bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode *bet);

/* 【解码】VXLAN 隧道（含 VNI 等参数）。stlv 链表，bet 调用方分配。 */
extern int tlv_to_bgp_encap_type_vxlan(struct bgp_attr_encap_subtlv *stlv,
				       struct bgp_encap_type_vxlan *bet);

/* 【解码】NVGRE 隧道（含 VSID 等参数）。stlv 链表，bet 调用方分配。 */
extern int tlv_to_bgp_encap_type_nvgre(struct bgp_attr_encap_subtlv *stlv,
				       struct bgp_encap_type_nvgre *bet);

/* 【解码】MPLS 隧道（含 label 等参数）。stlv 链表，bet 调用方分配。 */
extern int tlv_to_bgp_encap_type_mpls(struct bgp_attr_encap_subtlv *stlv,
				      struct bgp_encap_type_mpls *bet);

/* 【解码】MPLS 隧道重复声明，内容与本段上一条完全相同（见文首说明）。 */
extern int tlv_to_bgp_encap_type_mpls(struct bgp_attr_encap_subtlv *stlv,
				      struct bgp_encap_type_mpls *bet);

/* 【解码】MPLS-in-GRE 隧道。stlv 链表，bet 调用方分配，返回错误码。 */
extern int
tlv_to_bgp_encap_type_mpls_in_gre(struct bgp_attr_encap_subtlv *stlv,
				  struct bgp_encap_type_mpls_in_gre *bet);

/* 【解码】VXLAN-GPE 隧道（支持通用协议扩展的 VXLAN）。stlv 链表，bet 调用方分配。 */
extern int
tlv_to_bgp_encap_type_vxlan_gpe(struct bgp_attr_encap_subtlv *stlv,
				struct bgp_encap_type_vxlan_gpe *bet);

/* 【解码】MPLS-in-UDP 隧道。stlv 链表，bet 调用方分配，返回错误码。 */
extern int
tlv_to_bgp_encap_type_mpls_in_udp(struct bgp_attr_encap_subtlv *stlv,
				  struct bgp_encap_type_mpls_in_udp *bet);

/* 【解码】PBB（Provider Backbone Bridging）隧道。stlv 链表，bet 调用方分配。 */
extern int tlv_to_bgp_encap_type_pbb(
	struct bgp_attr_encap_subtlv *stlv, /* subtlv chain */
	struct bgp_encap_type_pbb *bet);    /* caller-allocated */

#endif /* _QUAGGA_BGP_ENCAP_TLV_H */
