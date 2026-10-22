// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, LabN Consulting, L.L.C.
 */

#ifndef _QUAGGA_BGP_ENCAP_TYPES_H
#define _QUAGGA_BGP_ENCAP_TYPES_H

#include "bgpd/bgp_ecommunity.h"

/* ============================================================================
 * 【文件导读】bgp_encap_types.h —— 隧道封装（Tunnel Encapsulation）的数据定义
 * ============================================================================
 * 【要解决什么】同一个前缀可能可以通过多种**隧道方式**送达——GRE、VXLAN、IP-in-IP、
 * MPLS-in-UDP、SRv6……接收方要能知道“用哪种封装、端点在哪儿、VNI/KEY 是多少”。
 * Tunnel Encapsulation 属性（RFC 9012，早期是 draft-rosen-idr-tunnel-encaps）
 * 就是承载这些信息的路由属性：属性里是**一串“隧道类型”条目**，每个条目又由
 * 若干 **sub-TLV** 组成（封装参数、协议类型、颜色、远端端点、IPsec 认证串等）。
 * 本文件把“类型号表”和“每个类型对应的 sub-TLV 组合”定义成结构体。
 *
 * 【⚠ 两套机制，别混淆（这是本文件最大的阅读陷阱）】
 *   ① **Tunnel Encapsulation 属性**：内容丰富的那个，能带 sub-TLV（本文件里
 *      bgp_tea_subtlv_* 与 bgp_encap_type_* 这些结构）；实现在 bgp_encap_tlv.c。
 *   ② **Encapsulation 扩展团体（BGP Encapsulation Extended Community）**：
 *      只有 8 字节，本质上就是**一个隧道类型号**，没有任何参数。文件末尾那两个
 *      inline 函数（encode_encap_extcomm / ecommunity_tunnel_type）处理的是它。
 *      它的用途是给路由“贴一个隧道类型标签”，用于 SD-WAN/color 场景。
 *   两者名字都带 encap、都带类型号，但**线格式、携带位置、表达能力完全不同**。
 *   看到 `bgp_encap_types` 这个枚举被两者共用，尤其容易看走眼。
 *
 * 【valid_subtlvs 位图的作用（读懂那堆结构体的钥匙）】每个 bgp_encap_type_xxx
 * 结构体都**固定包含该隧道类型可能用到的全部 sub-TLV 字段**（因为 C 结构体不能
 * 按需生长），但实际收到的报文里某些 sub-TLV 可能**根本不存在**。于是用
 * valid_subtlvs 位图标记“哪些字段是真收到了的”，其余字段虽在结构里但内容无意义。
 * 因此**读取任何 st_xxx 字段前必须先 CHECK_SUBTLV_FLAG**——这是本文件最重要的
 * 使用约定，忽略它就会把未初始化的零值当成真实参数用（例如把 color 读成 0）。
 *
 * 【阅读建议】类型号枚举 → sub-TLV 类型号 → 各 sub-TLV 结构体（原生数据）→
 *            bgp_encap_type_* （每种隧道的字段组合）→ 末尾两个 inline 编解码函数。
 *            更完整的解析/封装实现见 bgp_encap_tlv.c；上层的 color/远程端点
 *            处理与 SRv6 场景相关。
 * ============================================================================
 */

/* from
 * http://www.iana.org/assignments/bgp-parameters/bgp-parameters.xhtml#tunnel-types
 */
/* 【枚举】隧道类型号，取值来自 IANA 注册表（**不能自造**，因为要跨厂商互通）。
 * 0 是 RESERVED（保留，不是“默认 GRE”之类）；11~13 是 MPLS 的几种承载变体
 * （MPLS-in-GRE / VXLAN-GPE / MPLS-in-UDP）；PBB 紧跟 13 之后即 14——它**没有写
 * 显式数值**，靠前一个值自增得到，所以在它前面插入新条目会**静默改变 PBB 的值**，
 * 这是个隐蔽的维护风险。
 * 特别地 SRv6 = 255（“For internal usage only”）——它不在 IANA 常规号段内，
 * 是 FRR 内部用于 SRv6 场景的**私用值**，绝不能当作标准值发给对端解读。 */
typedef enum {
	BGP_ENCAP_TYPE_RESERVED = 0,
	BGP_ENCAP_TYPE_L2TPV3_OVER_IP = 1,
	BGP_ENCAP_TYPE_GRE = 2,
	BGP_ENCAP_TYPE_TRANSMIT_TUNNEL_ENDPOINT = 3,
	BGP_ENCAP_TYPE_IPSEC_IN_TUNNEL_MODE = 4,
	BGP_ENCAP_TYPE_IP_IN_IP_TUNNEL_WITH_IPSEC_TRANSPORT_MODE = 5,
	BGP_ENCAP_TYPE_MPLS_IN_IP_TUNNEL_WITH_IPSEC_TRANSPORT_MODE = 6,
	BGP_ENCAP_TYPE_IP_IN_IP = 7,
	BGP_ENCAP_TYPE_VXLAN = 8,
	BGP_ENCAP_TYPE_NVGRE = 9,
	BGP_ENCAP_TYPE_MPLS = 10, /* NOTE: Encap SAFI&Attribute not used */
	BGP_ENCAP_TYPE_MPLS_IN_GRE = 11,
	BGP_ENCAP_TYPE_VXLAN_GPE = 12,
	BGP_ENCAP_TYPE_MPLS_IN_UDP = 13,
	BGP_ENCAP_TYPE_PBB,
	BGP_ENCAP_TYPE_SRV6 = 255, /* For internal usage only */
} bgp_encap_types;

/* 【枚举】sub-TLV 的类型号（每个隧道类型条目内部再用一层 TLV 描述细节）。
 *   ENCAPSULATION : 该隧道自己的封装参数（如 GRE key、L2TPv3 session）。
 *   PROTO_TYPE    : 被承载的**上层协议/以太类型**（EtherType），告诉对端隧道里
 *                   装的是什么（IPv4/IPv6/MPLS…）。
 *   IPSEC_TA      : IPsec 隧道认证串（Tunnel Authenticator）。
 *   COLOR         : **color**——SD-WAN/流量工程里用来给路径“染色”，同色才能
 *                   选同一条隧道，是 color-based routing 的核心字段。
 *   REMOTE_ENDPOINT : 远端端点（对端隧道地址）+ AS。注意 6 号缺 5——
 *                   5 在 IANA 里被占用或未定义，**不是笔误**，也说明这里的
 *                   编号不是连续的，遍历时不能用“从 1 数到 N”。 */
typedef enum {
	BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION = 1,
	BGP_ENCAP_SUBTLV_TYPE_PROTO_TYPE = 2,
	BGP_ENCAP_SUBTLV_TYPE_IPSEC_TA = 3,
	BGP_ENCAP_SUBTLV_TYPE_COLOR = 4,
	BGP_ENCAP_SUBTLV_TYPE_REMOTE_ENDPOINT =
		6 /* speculative, IANA assignment TBD */
} bgp_encap_subtlv_types;

/*
 * Tunnel Encapsulation Attribute subtlvs
 */
/* 【结构】L2TPv3 over IP 的封装参数（ENCAPSULATION sub-TLV 的一种形态）：
 *   sessionid      : L2TPv3 会话号。
 *   cookie_length  : cookie 的**有效字节数**（cookie 最长 8 字节，但实际可能只用
 *                    前几字节，所以长度必须单独给；不要假设 cookie[8] 全部有效）。
 *   cookie         : L2TPv3 cookie 的原始字节。 */
struct bgp_tea_subtlv_encap_l2tpv3_over_ip {
	uint32_t sessionid;
	uint8_t cookie_length;
	uint8_t cookie[8];
};

/* 【结构】GRE 的封装参数：只有 GRE key。注意这里是**接收解析出的主机字节序**
 * uint32_t，线格式上则是 4 字节大端——解析/编码时要做字节序转换。 */
struct bgp_tea_subtlv_encap_gre_key {
	uint32_t gre_key;
};

/* 【结构】PBB(P802.1ah) 的封装参数，用了**位域**（⚠ 位域在 C 里的内存布局属于
 * 实现定义，因此这个结构体**不能直接拿来当线格式缓冲区读写**，必须先解析成
 * 这些字段再序列化——读实现里对它是否做了显式字节序处理要留心）。
 *   flag_isid : 标记 isid 字段是否有效（与 valid_subtlvs 位图不同，这是**线格式
 *               协议自己定义的**标志位，随报文一起传输）。
 *   flag_vid  : 同上，标记 vid 是否有效。
 *   isid      : 24 位服务实例标识。
 *   vid       : 12 位 VLAN ID。
 *   macaddr   : 6 字节 MAC。
 * 注意 flag 只有 1 位而 isid 有 24 位，合起来正好凑满第一个 32 位字——这是协议
 * 紧凑编码的常见手法。 */
struct bgp_tea_subtlv_encap_pbb {
	uint32_t flag_isid : 1;
	uint32_t flag_vid : 1;
	uint32_t isid : 24;
	uint16_t vid : 12;
	uint8_t macaddr[6];
};

/* 【结构】PROTO_TYPE sub-TLV：隧道里承载的**以太类型**（EtherType，如 0x0800=IPv4）。
 * 字段名 proto 容易与“IP 协议号”混淆——它是 EtherType，不是 IP proto。 */
struct bgp_tea_subtlv_proto_type {
	uint16_t proto; /* ether-type */
};

/* 【结构】COLOR sub-TLV：color 值（4 字节）。用于 SD-WAN 里“同色才是同一条路径”
 * 的匹配；常与 color 扩展团体配合使用，两者语义相近但载体不同，读代码时注意区分。 */
struct bgp_tea_subtlv_color {
	uint32_t color;
};

/* per draft-rosen-idr-tunnel-encaps */
/* 【结构】REMOTE_ENDPOINT sub-TLV：远端隧道端点。这是最实用的一个 sub-TLV——
 * 它告诉接收方“隧道对端在这个地址上”。三个字段：
 *   family     : 地址族（IPv4 或 IPv6），**决定下面 union 里哪个成员有效**。
 *                必须先判 family 再取 ip_address，否则读出的是垃圾。
 *   ip_address : 用 union 承载 v4/v6——这是节省空间的常见做法，但代价是访问方
 *                无法从类型上得知该取哪个成员，只能依赖上面的 family。
 *   as4        : 远端 AS 号。原注释强调 “always 4 bytes”——**不论发起方是
 *                2 字节 AS 还是 4 字节 AS，这里都按 4 字节编码**，所以不能用
 *                “对方是 2 字节 AS 环境就按 2 字节读”的思路处理，会错位。 */
struct bgp_tea_subtlv_remote_endpoint {
	uint8_t family; /* IPv4 or IPv6 */
	union {
		struct in_addr v4;
		struct in6_addr v6;
	} ip_address;
	as_t as4; /* always 4 bytes */
};

/*
 * This is the length of the value part of the ipsec tunnel authenticator
 * subtlv. Currently we only support the length for authenticator type 1.
 */
#define BGP_ENCAP_SUBTLV_IPSEC_TA_SIZE	20

/* 【结构】IPSEC_TA sub-TLV：IPsec 隧道认证串。
 *   authenticator_type   : 认证算法类型。FRR **目前只支持类型 1**（见上方的
 *                          原注释），所以遇到别的类型只能忽略/报错，别指望通用处理。
 *   authenticator_length : value 字段的**有效字节数**（不是数组总长 20）。
 *                          与 L2TPv3 的 cookie_length 同理：定长数组 + 有效长度。
 *   value                : 认证串原始字节，缓冲固定 20 字节（BGP_ENCAP_SUBTLV_
 *                          IPSEC_TA_SIZE 就是按类型 1 的算法输出长度定的）。
 *                          **若将来支持更长算法的类型，这个常量与数组都要改**。 */
struct bgp_tea_subtlv_ipsec_ta {
	uint16_t authenticator_type;  /* only type 1 is supported so far */
	uint8_t authenticator_length; /* octets in value field */
	uint8_t value[BGP_ENCAP_SUBTLV_IPSEC_TA_SIZE];
};

/*
 * Subtlv valid flags
 * TBD change names to add "VALID"
 */
/* 【位图】valid_subtlvs 的各个位——**每一位对应“该 sub-TLV 确实收到了”**
 * （见文件头导读的 valid_subtlvs 说明）。用独立的一位（而不是连续编号）是为了
 * 将来扩 sub-TLV 类型时不必改动已有位。原注释的 TBD 说明作者自己也觉得这组名字
 * 不够清楚（打算加 VALID 字样），读的时候心里默念“BGP_TEA_SUBTLV_COLOR 表示
 * **color 有效**”，而不是“color 值本身”。
 * 注意数组结构体里都有 valid_subtlvs 字段，但**位图与结构体字段必须成对**：
 * 置位了却没有填值、或填了值却忘置位，都会导致后续误读。 */
#define BGP_TEA_SUBTLV_ENCAP		0x00000001
#define BGP_TEA_SUBTLV_PROTO_TYPE	0x00000002
#define BGP_TEA_SUBTLV_COLOR		0x00000004
#define BGP_TEA_SUBTLV_IPSEC_TA		0x00000008
#define BGP_TEA_SUBTLV_REMOTE_ENDPOINT	0x00000010

/* 【宏】对上述位图的三个操作，本质是 lib 里 CHECK_FLAG / SET_FLAG / UNSET_FLAG
 * 的转发（把 (ptr)->valid_subtlvs 作为目标）。ptr 必须是带 valid_subtlvs 字段的
 * 结构体**指针**——传对象本身会编译报错，传错类型则不会。 */
#define CHECK_SUBTLV_FLAG(ptr, flag)  CHECK_FLAG((ptr)->valid_subtlvs, (flag))
#define SET_SUBTLV_FLAG(ptr, flag)      SET_FLAG((ptr)->valid_subtlvs, (flag))
#define UNSET_SUBTLV_FLAG(ptr, flag)  UNSET_FLAG((ptr)->valid_subtlvs, (flag))

/*
 * Tunnel Type-specific APIs
 */
/* 【以下一整组】每种隧道类型对应一个结构体，形状都是：
 *     uint32_t valid_subtlvs;         ← 位图，标记下面哪些字段有效
 *     struct bgp_tea_subtlv_xxx st_xxx;   ← 该隧道类型允许出现的各 sub-TLV
 * 每个字段后的 "optional" 注释表示**该 sub-TLV 在协议上是可选的**（于是更必须靠
 * valid_subtlvs 判断有没有）。因为结构体是定长的，所有可选字段都占了位置，
 * 所以这些结构体**不能直接 memset 后当解析结果用**——必须先清零再填，且读取前
 * 查位图。
 * 各类型允许的组合由协议规定，不是实现随意定的：例如 ip_in_ip / gre 允许
 * proto + color + endpoint；而 transmit_tunnel_endpoint 与 nvgre / mpls /
 * mpls_in_gre / vxlan_gpe / mpls_in_udp 只定义了 endpoint（原注释写作
 * "No subtlvs defined in spec?" 是作者对**除 endpoint 之外是否还有别的 sub-TLV**
 * 拿不准，不要理解成“这些类型没有任何 sub-TLV”）。
 * 阅读时不必逐个记字段，抓住“哪些类型有自己特有的封装参数”即可：
 *   l2tpv3_over_ip → st_encap 是 L2TPv3 session；
 *   gre            → st_encap 是 GRE key；
 *   pbb            → st_encap 是 PBB 的 isid/vid/mac；
 *   ipsec 系列     → 有 st_ipsec_ta；vxlan 额外有 vnid / mac_address。*/
struct bgp_encap_type_reserved {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

struct bgp_encap_type_l2tpv3_over_ip {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_encap_l2tpv3_over_ip st_encap;
	struct bgp_tea_subtlv_proto_type st_proto;	 /* optional */
	struct bgp_tea_subtlv_color st_color;		   /* optional */
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

struct bgp_encap_type_gre {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_encap_gre_key st_encap;      /* optional */
	struct bgp_tea_subtlv_proto_type st_proto;	 /* optional */
	struct bgp_tea_subtlv_color st_color;		   /* optional */
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

struct bgp_encap_type_ip_in_ip {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_proto_type st_proto;	 /* optional */
	struct bgp_tea_subtlv_color st_color;		   /* optional */
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

struct bgp_encap_type_transmit_tunnel_endpoint {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* No subtlvs defined in spec? */
};

struct bgp_encap_type_ipsec_in_tunnel_mode {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_ipsec_ta st_ipsec_ta;	/* optional */
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

struct bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_ipsec_ta st_ipsec_ta;	/* optional */
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

struct bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_ipsec_ta st_ipsec_ta;	/* optional */
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
};

/* 【宏】VXLAN 的 VNID 字段里**最高两位是标志位**（V=valid、M=MAC 存在），
 * 不是 VNI 本身的一部分——这是 VXLAN 封装 TLV 的编码规定。这两个掩码用于
 * 从 32 位字里把标志位**挑出来**判断；真正取 VNI 时应把这两位**掩掉**
 * （见下面结构体注释的提醒）。 */
#define VXLAN_ENCAP_MASK_VNID_VALID 0x80000000
#define VXLAN_ENCAP_MASK_MAC_VALID  0x40000000

/* 【结构】VXLAN 的封装参数：比别的类型多了后面两项。
 *   vnid        : VXLAN 网络标识。原注释 "does not include V and M bit" 是在
 *                强调——**本字段存放的是已经去掉标志位的纯 VNI**。因此若你在
 *                别处看到从线格式直接读出的原始 32 位字，那里是**带**标志位的，
 *                两者不能混用（这正是上面两个 MASK 宏存在的意义）。
 *   mac_address : 可选的对端 MAC。注意它是**指针**而不是内嵌数组——本结构里唯一
 *                 一个需要额外内存的字段，因此释放逻辑必须单独处理它，否则会泄漏；
 *                同时也意味着本结构**不能简单按值拷贝后各用各的**（会共享同一块
 *                MAC 内存）。这是本组结构体里最容易出内存问题的一个，值得特别留意。 */
struct bgp_encap_type_vxlan {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* draft-ietf-idr-tunnel-encaps-02 */
	uint32_t vnid;	/* does not include V and M bit */
	uint8_t *mac_address; /* optional */
};

struct bgp_encap_type_nvgre {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* No subtlvs defined in spec? */
};

struct bgp_encap_type_mpls {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* No subtlvs defined in spec? */
};

struct bgp_encap_type_mpls_in_gre {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* No subtlvs defined in spec? */
};

struct bgp_encap_type_vxlan_gpe {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* No subtlvs defined in spec? */
};

struct bgp_encap_type_mpls_in_udp {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	/* No subtlvs defined in spec? */
};

struct bgp_encap_type_pbb {
	uint32_t valid_subtlvs;
	struct bgp_tea_subtlv_remote_endpoint st_endpoint; /* optional */
	struct bgp_tea_subtlv_encap_pbb st_encap;
};

/* 【函数】encode_encap_extcomm —— 把隧道类型号编成 **Encapsulation 扩展团体**
 * 的 8 字节线格式（**注意这是文件头导读里说的第二套机制**：只有类型号，没有
 * 参数；带参数的那种是 Tunnel Encapsulation 属性）。
 * 编码要点：
 *   - 先整体清零，保证未被显式赋值的字节为 0（否则会带上栈上的垃圾数据，
 *     这在生成协议报文时是严重问题）。
 *   - val[0]/val[1] 写死成 opaque 类型 + encaps 子类型，即“这是一条 opaquely
 *     编码的扩展团体，子类型是封装”。这两个常量来自 bgp_ecommunity.h。
 *   - 真正的类型号写在 **val[6] 与 val[7]**（最后两字节，大端：高字节在前），
 *     即“6 字节值里只有后 2 字节有意义”，前面 4 字节留 0。
 * eval 是**出参**（调用方提供存储），函数不分配内存、不返回错误——类型号超界
 * 会被静默截断成低 16 位。 */
static inline void encode_encap_extcomm(bgp_encap_types tnl_type,
					struct ecommunity_val *eval)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_OPAQUE;
	eval->val[1] = ECOMMUNITY_OPAQUE_SUBTYPE_ENCAP;
	eval->val[6] = ((tnl_type) >> 8) & 0xff;
	eval->val[7] = (tnl_type)&0xff;
}

/* 【函数】ecommunity_tunnel_type —— 上者的逆操作：从扩展团体属性里取出隧道类型。
 *   ecom           : 扩展团体属性（其内部 val 是**若干个 8 字节条目首尾相接**）。
 *   ecom_increment : 要看**第几个**条目（以 8 字节为单位的下标）。扩展团体是一个
 *                    数组，调用方通常靠它遍历；本函数**不做边界检查**，下标越界
 *                    会直接读到属性缓冲之外——调用前必须自己确认条目数。
 *   tunnel_type    : 出参，解出的类型号（按 bgp_encap_types 语义）。
 * 返回 true 表示“这个条目确实是 Encapsulation 扩展团体”（type/subtype 匹配），
 * 此时才写了出参；返回 false 时**出参未被赋值**，不要使用。
 * 解码时把 val[6] 当高字节、val[7] 当低字节合成 16 位——与 encode 端的写法对称。 */
static inline bool ecommunity_tunnel_type(struct ecommunity *ecom, int ecom_increment,
					  bgp_encap_types *tunnel_type)
{
	uint8_t *pnt;
	uint8_t type, sub_type;

	pnt = (ecom->val + (ecom_increment * ECOMMUNITY_SIZE));
	type = pnt[0];
	sub_type = pnt[1];
	if (!(type == ECOMMUNITY_ENCODE_OPAQUE && sub_type == ECOMMUNITY_OPAQUE_SUBTYPE_ENCAP))
		return false;
	*tunnel_type = ((pnt[6] << 8) | pnt[7]);
	return true;
}

#endif /* _QUAGGA_BGP_ENCAP_TYPES_H */
