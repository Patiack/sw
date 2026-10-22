// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Prefix structure.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * 文件导读：struct prefix —— FRR 的“前缀 / 地址”统一表示
 *
 * 这是 FRR 整个路由栈最底层的公共类型之一。无论 BGP、OSPF 还是静态路由，
 * 内部传递“一条路由 / 一个地址”时，几乎都用一个 struct prefix。
 *
 * 设计要点：
 *   - family 是 FRR 内部自定的地址族编号（大体沿用 AF_ 数值，但额外引入了
 *     AF_EVPN、AF_FLOWSPEC 等 OS 不认识的族）。它不是内核 AF_，只有真正要
 *     和内核交互时才需映射。
 *   - prefixlen 统一用“位数”（bit）表示：IPv4 是 0 到 32，IPv6 是 0 到 128。
 *   - 具体地址放在匿名 union u 里：IPv4 用 prefix4（in_addr），IPv6 用 prefix6
 *     （in6_addr），以太网用 prefix_eth，EVPN 用 prefix_evpn，flowspec 用
 *     prefix_flowspec；另有 lp（标签交换路由）等历史成员。该 union 8 字节对齐，
 *     便于按机器字访问。
 *
 * 族的布局差异（务必分清）：
 *   - struct prefix        ：通用容器，family / prefixlen + union，最灵活。
 *   - struct prefix_ipv4   ：family 固定 AF_INET，地址即 in_addr。
 *   - struct prefix_ipv6   ：family 固定 AF_INET6，地址即 in6_addr。
 *   - struct prefix_eth    ：以太网 MAC（AF_ETHERNET）。
 *   - struct prefix_evpn   ：EVPN 路由（AF_EVPN，RFC 7432）。
 *   - struct prefix_rd / prefix_ls：路由区分符 / 标签交换路由的旧式布局。
 * 因为它们都以 family + prefixlen 开头，指针可在这些类型间安全互转（FRR
 * 大量这样做），这正是“跨族统一表示”的关键。
 *
 * 本头文件只放类型与少量 inline；真正的运算（掩码长度、包含判定、字符串
 * 互转、apply_mask 等）在 prefix.c，plist.c 的匹配引擎也建立在这些原语之上。
 */

#ifndef _ZEBRA_PREFIX_H
#define _ZEBRA_PREFIX_H

#ifdef __linux__
#include <net/ethernet.h>
#else
#include <netinet/if_ether.h>
#endif
#include "sockunion.h"
#include "ipaddr.h"
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ETH_ALEN
/*
 * 以太网 MAC 地址的字节长度，固定 6 字节。
 * 某些平台的系统头文件未提供该宏，这里做兜底定义。
 */
#define ETH_ALEN 6
#endif

/* EVPN route types. */
/*
 * EVPN 路由类型枚举（对应 BGP EVPN NLRI 的 route type 字段）。
 * 数值 1 到 5 与线上报文一致：
 *   1 A-D 路由、2 MAC/IP 通告、3 IMET 包含多播、4 ES 以太网段、5 IP 前缀路由。
 * 每类路由的具体字段布局见下面各 evpn_*_addr 结构。
 */
typedef enum {
	BGP_EVPN_AD_ROUTE = 1,    /* Ethernet Auto-Discovery (A-D) route */
	BGP_EVPN_MAC_IP_ROUTE,    /* MAC/IP Advertisement route */
	BGP_EVPN_IMET_ROUTE,      /* Inclusive Multicast Ethernet Tag route */
	BGP_EVPN_ES_ROUTE,        /* Ethernet Segment route */
	BGP_EVPN_IP_PREFIX_ROUTE, /* IP Prefix route */
} bgp_evpn_route_type;

/* value of first byte of ESI */
/*
 * ESI 第一个字节（type 字段）的取值，标识该 ESI 由何种方式生成。
 * 各取值的具体组合格式见每条宏后所附的英文示意图。
 */
#define ESI_TYPE_ARBITRARY 0  /* */
#define ESI_TYPE_LACP      1  /* <> */
#define ESI_TYPE_BRIDGE    2  /* <Root bridge Mac-6B>:<Root Br Priority-2B>:00 */
#define ESI_TYPE_MAC       3  /* <Syst Mac Add-6B>:<Local Discriminator Value-3B> */
#define ESI_TYPE_ROUTER    4  /* <RouterId-4B>:<Local Discriminator Value-4B> */
#define ESI_TYPE_AS        5  /* <AS-4B>:<Local Discriminator Value-4B> */

/*
 * ESI 全 0xff 的特殊取值，表示“单活 / 无 ESI”，即该以太网段不做多活负载分担。
 */
#define MAX_ESI {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}


/*
 * EVPN 相关定长与字符串长度：
 *   EVPN_ETH_TAG_BYTES  ：以太网标签（Ethernet Tag）字节数，4 字节。
 *   ESI_BYTES           ：ESI 字节数，10 字节（1 字节 type + 9 字节内容）。
 *   ESI_STR_LEN         ：ESI 转冒号分隔字符串的长度（每字节两位十六进制加冒号）。
 *   EVPN_DF_ALG_STR_LEN ：DF（指定转发器）选举算法名字符串最大长度。
 */
#define EVPN_ETH_TAG_BYTES 4
#define ESI_BYTES 10
#define ESI_STR_LEN (3 * ESI_BYTES)
#define EVPN_DF_ALG_STR_LEN 24

/* Maximum number of VTEPs per-ES -
 * XXX - temporary limit for allocating strings etc.
 */
#define ES_VTEP_MAX_CNT	    10
#define ES_VTEP_LIST_STR_SZ (ES_VTEP_MAX_CNT * IPADDR_STRING_SIZE)

#define ETHER_ADDR_STRLEN (3 * ETH_ALEN)
/*
 * there isn't a portable ethernet address type. We define our
 * own to simplify internal handling
 */
/*
 * 通用以太网地址（MAC）容器。
 * 之所以自己定义而不复用系统类型，是因为可移植的以太网地址类型并不统一；
 * 用固定字节数组最省心，packed 保证无内部填充，可直接与报文 / 内核交互。
 */
struct ethaddr {
	uint8_t octet[ETH_ALEN];
} __attribute__((packed));


/* length is the number of valuable bits of prefix structure
* 18 bytes is current length in structure, if address is ipv4
* 30 bytes is in case of ipv6
*/
/*
 * EVPN type-5（IP-Prefix）路由在按位比较 / 哈希时用到的“有效位长度”。
 * 数值等于对应结构体的有效字节数乘以 8：IPv4 为 18 字节，IPv6 为 30 字节。
 */
#define PREFIX_LEN_ROUTE_TYPE_5_IPV4 (18*8)
#define PREFIX_LEN_ROUTE_TYPE_5_IPV6 (30*8)

/*
 * ESI（以太网段标识）的通用类型，本质是 10 字节不透明数组。
 */
typedef struct esi_t_ {
	uint8_t val[ESI_BYTES];
} esi_t;

/*
 * EVPN 各类路由的地址参数（即下面 evpn_addr->u 的成员）：
 *   evpn_ead_addr    ：A-D 路由参数（ESI + 以太网标签 + 对端 IP + 分片 id）。
 *   evpn_macip_addr  ：MAC/IP 通告参数（以太网标签 + 掩码长度 + MAC + IP）。
 *   evpn_imet_addr   ：IMET 参数（以太网标签 + 掩码长度 + 源 IP）。
 *   evpn_es_addr     ：ES 参数（本端 ESI + 掩码长度 + 源 IP）。
 *   evpn_prefix_addr ：IP-Prefix 参数（以太网标签 + 掩码长度 + IP 前缀）。
 * 它们只在 evpn_addr.u 里按 route_type 选用其一。
 */
struct evpn_ead_addr {
	esi_t esi;
	uint32_t eth_tag;
	struct ipaddr ip;
	uint16_t frag_id;
};

struct evpn_macip_addr {
	uint32_t eth_tag;
	uint8_t ip_prefix_length;
	struct ethaddr mac;
	struct ipaddr ip;
};

struct evpn_imet_addr {
	uint32_t eth_tag;
	uint8_t ip_prefix_length;
	struct ipaddr ip;
};

struct evpn_es_addr {
	esi_t esi;
	uint8_t ip_prefix_length;
	struct ipaddr ip;
};

struct evpn_prefix_addr {
	uint32_t eth_tag;
	uint8_t ip_prefix_length;
	struct ipaddr ip;
};

/* EVPN address (RFC 7432) */
/*
 * EVPN 地址（RFC 7432）：route_type 决定 union u 中实际生效的成员。
 * 紧随其后的 #define 是 union 成员的简写别名（例如 evpn->macip_addr 等价于
 * evpn->u._macip_addr），让调用处少写一层 u。
 */
struct evpn_addr {
	uint8_t route_type;
	union {
		struct evpn_ead_addr _ead_addr;
		struct evpn_macip_addr _macip_addr;
		struct evpn_imet_addr _imet_addr;
		struct evpn_es_addr _es_addr;
		struct evpn_prefix_addr _prefix_addr;
	} u;
#define ead_addr u._ead_addr
#define macip_addr u._macip_addr
#define imet_addr u._imet_addr
#define es_addr u._es_addr
#define prefix_addr u._prefix_addr
};

/*
 * A struct prefix contains an address family, a prefix length, and an
 * address.  This can represent either a 'network prefix' as defined
 * by CIDR, where the 'host bits' of the prefix are 0
 * (e.g. AF_INET:10.0.0.0/8), or an address and netmask
 * (e.g. AF_INET:10.0.0.9/8), such as might be configured on an
 * interface.
 */

/* different OSes use different names */
#if defined(AF_PACKET)
#define AF_ETHERNET AF_PACKET
#else
#if defined(AF_LINK)
#define AF_ETHERNET AF_LINK
#endif
#endif

/* The 'family' in the prefix structure is internal to FRR and need not
 * map to standard OS AF_ definitions except where needed for interacting
 * with the kernel. However, AF_ definitions are currently in use and
 * prevalent across the code. Define a new FRR-specific AF for EVPN to
 * distinguish between 'ethernet' (MAC-only) and 'evpn' prefixes and
 * ensure it does not conflict with any OS AF_ definition.
 */
#if !defined(AF_EVPN)
#define AF_EVPN (AF_MAX + 1)
#endif

#if !defined(AF_FLOWSPEC)
#define AF_FLOWSPEC (AF_MAX + 2)
#endif

struct flowspec_prefix {
	uint8_t family;
	uint16_t prefixlen; /* length in bytes */
	uintptr_t ptr;
};

/* FRR generic prefix structure. */
/*
 * FRR 通用前缀结构，整个路由栈最核心的“地址 / 前缀”统一表示。
 *   family   ：FRR 内部地址族编号（多数沿用 AF_ 数值，另含 AF_EVPN、AF_FLOWSPEC）。
 *   prefixlen：前缀长度，单位是“位”，IPv4 为 0 到 32、IPv6 为 0 到 128。
 *   u        ：按族选用的地址载体，8 字节对齐：
 *              prefix    - 单字节，兼容旧代码；val / val32 - 原始字节 / 字访问；
 *              prefix4    - IPv4 地址（in_addr）；
 *              prefix6    - IPv6 地址（in6_addr）；
 *              lp         - 标签交换路由的 (id, adv_router)；
 *              prefix_eth - 以太网 MAC（AF_ETHERNET）；
 *              prefix_evpn- EVPN 地址（AF_EVPN）；
 *              prefix_flowspec - 流规则（AF_FLOWSPEC）。
 * 因为 family + prefixlen 是所有族共有的前缀，struct prefix 与其族专用变体
 * （如 struct prefix_ipv4）之间可以安全地互相强转。
 */
struct prefix {
	uint8_t family;
	uint16_t prefixlen;
	union {
		uint8_t prefix;
		struct in_addr prefix4;
		struct in6_addr prefix6;
		struct {
			struct in_addr id;
			struct in_addr adv_router;
		} lp;
		struct ethaddr prefix_eth; /* AF_ETHERNET */
		uint8_t val[16];
		uint32_t val32[4];
		uintptr_t ptr;
		struct evpn_addr prefix_evpn; /* AF_EVPN */
		struct flowspec_prefix prefix_flowspec; /* AF_FLOWSPEC */
	} u __attribute__((aligned(8)));
};

/* IPv4 prefix structure. */
/*
 * IPv4 专用前缀结构。前半段布局与 struct prefix 一致，
 * 因此可与 struct prefix 指针互换（FRR 中常见的强转用法）。
 */
struct prefix_ipv4 {
	uint8_t family;
	uint16_t prefixlen;
	struct in_addr prefix __attribute__((aligned(8)));
};

/* IPv6 prefix structure. */
/*
 * IPv6 专用前缀结构，同样与 struct prefix 布局兼容，可互相强转。
 */
struct prefix_ipv6 {
	uint8_t family;
	uint16_t prefixlen;
	struct in6_addr prefix __attribute__((aligned(8)));
};

/*
 * MPLS 标签交换（labeled-unicast）路由用的前缀结构（旧式）：
 * 由 BGP 的 id（下一跳 router id）与 adv_router 组成，已较少使用。
 */
struct prefix_ls {
	uint8_t family;
	uint16_t prefixlen;
	struct in_addr id __attribute__((aligned(8)));
	struct in_addr adv_router;
};

/* Prefix for routing distinguisher. */
/*
 * 路由区分符（Route Distinguisher）前缀，8 字节值（形如 ASN:nn 或 IP:nn）。
 * 用于 MPLS L3VPN 中区分不同 VPN 里相同的路由前缀；family 由调用方约定。
 */
struct prefix_rd {
	uint8_t family;
	uint16_t prefixlen;
	uint8_t val[8] __attribute__((aligned(8)));
};

/* Prefix for ethernet. */
/*
 * 以太网前缀（AF_ETHERNET）：family + prefixlen + 6 字节 MAC，用于 EVPN 等场景。
 */
struct prefix_eth {
	uint8_t family;
	uint16_t prefixlen;
	struct ethaddr eth_addr __attribute__((aligned(8))); /* AF_ETHERNET */
};

/* EVPN prefix structure. */
/*
 * EVPN 前缀（AF_EVPN）：family + prefixlen + evpn_addr，承载五类 EVPN 路由。
 */
struct prefix_evpn {
	uint8_t family;
	uint16_t prefixlen;
	struct evpn_addr prefix __attribute__((aligned(8)));
};

/*
 * 判断某条 EVPN 前缀里“对应的 IP 地址字段”是否为空（未设置）。
 * 依据 route_type 选用不同成员：A-D 用 ead_addr.ip，MAC/IP 用 macip_addr.ip，
 * IMET 用 imet_addr.ip，ES 用 es_addr.ip，IP-Prefix 用 prefix_addr.ip。
 * 存在对应路由类型即返回该 IP 是否为“无地址”的结果。
 */
static inline int is_evpn_prefix_ipaddr_none(const struct prefix_evpn *evp)
{
	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE)
		return IS_IPADDR_NONE(&(evp)->prefix.ead_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
		return IS_IPADDR_NONE(&(evp)->prefix.macip_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_IMET_ROUTE)
		return IS_IPADDR_NONE(&(evp)->prefix.imet_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_ES_ROUTE)
		return IS_IPADDR_NONE(&(evp)->prefix.es_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE)
		return IS_IPADDR_NONE(&(evp)->prefix.prefix_addr.ip);
	return 0;
}

static inline int is_evpn_prefix_ipaddr_v4(const struct prefix_evpn *evp)
{
	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE)
		return IS_IPADDR_V4(&(evp)->prefix.ead_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
		return IS_IPADDR_V4(&(evp)->prefix.macip_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_IMET_ROUTE)
		return IS_IPADDR_V4(&(evp)->prefix.imet_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_ES_ROUTE)
		return IS_IPADDR_V4(&(evp)->prefix.es_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE)
		return IS_IPADDR_V4(&(evp)->prefix.prefix_addr.ip);
	return 0;
}

static inline int is_evpn_prefix_ipaddr_v6(const struct prefix_evpn *evp)
{
	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE)
		return IS_IPADDR_V6(&(evp)->prefix.ead_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
		return IS_IPADDR_V6(&(evp)->prefix.macip_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_IMET_ROUTE)
		return IS_IPADDR_V6(&(evp)->prefix.imet_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_ES_ROUTE)
		return IS_IPADDR_V6(&(evp)->prefix.es_addr.ip);
	if (evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE)
		return IS_IPADDR_V6(&(evp)->prefix.prefix_addr.ip);
	return 0;
}

/* Prefix for a Flowspec entry */
/*
 * Flowspec（流规则）前缀（AF_FLOWSPEC）：family + prefixlen（此结构里未用）
 * + 指向 flowspec_prefix 的负载，用于 BGP FlowSpec 的流匹配规则。
 */
struct prefix_fs {
	uint8_t family;
	uint16_t prefixlen; /* unused */
	struct flowspec_prefix  prefix __attribute__((aligned(8)));
};

/*
 * (S,G) 组播源组前缀：family + prefixlen + 源地址 src + 组地址 grp。
 * 用于 MSDP / PIM 等表示“某源发往某组的流”。
 */
struct prefix_sg {
	uint8_t family;
	uint16_t prefixlen;
	struct ipaddr src;
	struct ipaddr grp;
};

/* clang-format off */
/*
 * 这组 TRANSPARENT_UNION 让同一指针可在 struct prefix 与其各族专用变体
 * （prefix_ipv4 / prefix_ipv6 / prefix_evpn / prefix_fs / prefix_rd）之间
 * 透明互换：uniontype 宏为每个成员生成同址别名，因此调用方既可传 .p 也可传 .p4。
 * prefixptr 用于可写指针，prefixconstptr 用于只读指针。
 */
union prefixptr {
	uniontype(prefixptr, struct prefix,      p)
	uniontype(prefixptr, struct prefix_ipv4, p4)
	uniontype(prefixptr, struct prefix_ipv6, p6)
	uniontype(prefixptr, struct prefix_evpn, evp)
	uniontype(prefixptr, struct prefix_fs,   fs)
	uniontype(prefixptr, struct prefix_rd,   rd)
} TRANSPARENT_UNION;

union prefixconstptr {
	uniontype(prefixconstptr, const struct prefix,      p)
	uniontype(prefixconstptr, const struct prefix_ipv4, p4)
	uniontype(prefixconstptr, const struct prefix_ipv6, p6)
	uniontype(prefixconstptr, const struct prefix_evpn, evp)
	uniontype(prefixconstptr, const struct prefix_fs,   fs)
	uniontype(prefixconstptr, const struct prefix_rd,   rd)
} TRANSPARENT_UNION;
/* clang-format on */

/*
 * 各类 IPv4 / IPv6 地址与 (S,G) 字符串的最长缓冲区长度：
 *   INET_ADDRSTRLEN  ：点分十进制 IPv4 最长 15 字符加结尾，取 16。
 *   INET6_ADDRSTRLEN ：IPv6 文本最长 45 字符加结尾，取 46。
 *   INET6_BUFSIZ     ：IPv6 相关缓冲区常用尺寸 53。
 *   PREFIX_STRLEN    ：prefix2str 结果的最大长度。
 *   PREFIX_SG_STR_LEN：形如 (S,G) 字符串的最大长度（两地址加括号逗号与结尾）。
 * 这些是分配字符串缓冲区的依据，切勿写小。
 */
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif /* INET_ADDRSTRLEN */

#ifndef INET6_ADDRSTRLEN
/* Use this for GUA or LL IPv6 address len */
/* dead:beef:dead:beef:dead:beef:dead:beef + \0 */
#define INET6_ADDRSTRLEN 46
#endif /* INET6_ADDRSTRLEN */

#ifndef INET6_BUFSIZ
#define INET6_BUFSIZ 53
#endif /* INET6_BUFSIZ */

/* Maximum string length of the result of prefix2str */
#define PREFIX_STRLEN 84

/*
 * Longest possible length of a (S,G) string is 82 bytes
 * 1111:2222:3333:4444:5555:6666:7777:8888 = INET6_ADDRSTRLEN * 2 = 92
 * (,) = 3
 * NULL Character at end = 1
 * (1111:2222:3333:4444:5555:6666:7777:8888,1111:2222:3333:4444:5555:6666:7777:8888)
 */
#define PREFIX_SG_STR_LEN (INET6_ADDRSTRLEN * 2 + 3 + 1)

/* Max bit/byte length of IPv4 address. */
/*
 * IPv4 基本常量：
 *   IPV4_MAX_BYTELEN / IPV4_MAX_BITLEN：IPv4 地址的字节数 4 与比特数 32。
 *   IPV4_ADDR_CMP / _SAME / _COPY    ：比较、判等、复制（现经由下方 inline 实现）。
 * 这些是 prefix.c 中大量掩码运算的基础常量。
 */
#define IPV4_MAX_BYTELEN    4
#define IPV4_MAX_BITLEN    32
#define IPV4_ADDR_CMP(D,S)   memcmp ((D), (S), IPV4_MAX_BYTELEN)

/*
 * 判断两个 IPv4 地址是否相同（比较 s_addr）。
 * 对应宏 IPV4_ADDR_SAME，改用 inline 以提升类型安全。
 */
static inline bool ipv4_addr_same(const struct in_addr *a,
				  const struct in_addr *b)
{
	return (a->s_addr == b->s_addr);
}
#define IPV4_ADDR_SAME(A,B)  ipv4_addr_same((A), (B))

/*
 * 复制 IPv4 地址（把 dst 赋值为 src）。对应宏 IPV4_ADDR_COPY。
 */
static inline void ipv4_addr_copy(struct in_addr *dst,
				  const struct in_addr *src)
{
	dst->s_addr = src->s_addr;
}
#define IPV4_ADDR_COPY(D,S)  ipv4_addr_copy((D), (S))

/*
 * IPv4 特殊网段 / 分类判定宏（操作数是主机字节序的 32 位地址）：
 *   IPV4_NET0 / NET127 / NET127_16 ：0.0.0.0/8、127.0.0.0/8、127.0.0.0/16。
 *   IPV4_LINKLOCAL                 ：169.254.0.0/16 链路本地。
 *   IPV4_CLASS_D / E / DE          ：D 类组播 / E 类保留 / D 与 E 合并范围。
 *   IPV4_MC_LINKLOCAL              ：224.0.0.0/24 组播链路本地。
 */
#define IPV4_NET0(a) ((((uint32_t)(a)) & 0xff000000) == 0x00000000)
#define IPV4_NET127(a) ((((uint32_t)(a)) & 0xff000000) == 0x7f000000)
#define IPV4_NET127_16(a)    ((((uint32_t)(a)) & 0xffff0000) == 0x7f000000)
#define IPV4_LINKLOCAL(a) ((((uint32_t)(a)) & 0xffff0000) == 0xa9fe0000)
#define IPV4_CLASS_D(a) ((((uint32_t)(a)) & 0xf0000000) == 0xe0000000)
#define IPV4_CLASS_E(a) ((((uint32_t)(a)) & 0xf0000000) == 0xf0000000)
#define IPV4_CLASS_DE(a) ((((uint32_t)(a)) & 0xe0000000) == 0xe0000000)
#define IPV4_MC_LINKLOCAL(a) ((((uint32_t)(a)) & 0xffffff00) == 0xe0000000)

/* Max bit/byte length of IPv6 address. */
/*
 * IPv6 地址的字节数 16 与比特数 128，以及比较 / 判等 / 复制宏
 * （基于 memcmp / memcpy 对整个 16 字节操作）。
 */
#define IPV6_MAX_BYTELEN    16
#define IPV6_MAX_BITLEN    128
#define IPV6_ADDR_CMP(D,S)   memcmp ((D), (S), IPV6_MAX_BYTELEN)
#define IPV6_ADDR_SAME(D,S)  (memcmp ((D), (S), IPV6_MAX_BYTELEN) == 0)
#define IPV6_ADDR_COPY(D,S)  memcpy ((D), (S), IPV6_MAX_BYTELEN)

/* Count prefix size from mask length */
/*
 * 前缀长度换算：
 *   PSIZE(a)：由比特数向上取整得到所需字节数。
 *   BSIZE(a)：由字节数换算回比特数。
 */
#define PSIZE(a) (((a) + 7) / (8))

#define BSIZE(a) ((a) * (8))

/* Prefix's family member. */
/*
 * 取出前缀的地址族字段。对所有以 family 开头的前缀结构都适用。
 */
#define PREFIX_FAMILY(p)  ((p)->family)

/* glibc defines s6_addr32 to __in6_u.__u6_addr32 if __USE_{MISC || GNU} */
#ifndef s6_addr32
#define s6_addr32 __u6_addr.__u6_addr32
#endif /*s6_addr32*/

/* Prototypes. */
/*
 * 地址族相关转换与显示：
 *   str2family      ：字符串（如 ip / ipv6）转内部 family 编号，失败返回 -1。
 *   afi2family / family2afi：AFI 与 family 互转。
 *   family2str / safi2str / afi2str / afi2str_lower：把 family、SAFI、AFI
 *     格式化为可读字符串（*_lower 为小写形式）。
 */
extern int str2family(const char *string);
extern int afi2family(afi_t afi);
extern afi_t family2afi(int family);
extern const char *family2str(int family);
extern const char *safi2str(safi_t safi);
extern const char *afi2str(afi_t afi);
extern const char *afi2str_lower(afi_t afi);

/*
 * 由传入的（可能是任意族专用类型的）前缀取地址族并转成 AFI。
 */
static inline afi_t prefix_afi(union prefixconstptr pu)
{
	return family2afi(pu.p->family);
}

/*
 * Check bit of the prefix.
 *
 * prefix
 *    byte buffer
 *
 * bit_index
 *    which bit to fetch from byte buffer, 0 indexed.
 */
/*
 * 读取字节缓冲 prefix 中第 bit_index 位（0 为最高位起算）的值，返回 0 或 1。
 * 是掩码比较、prefix_common_bits 等算法的位级原语。
 */
extern unsigned int prefix_bit(const uint8_t *prefix, const uint16_t bit_index);

extern void prefix_flowspec_ptr_free(struct prefix *p);

/*
 * 前缀对象的内存管理：
 *   prefix_new        ：分配一个零初始化的 struct prefix（family 置为 AF_UNSPEC）。
 *   prefix_free       ：释放 *p 并把 *p 置 NULL，可安全地重复调用。
 *   prefix_free_lists ：适配“以指针的指针为参数”的释放回调，供链表清空使用。
 */
extern struct prefix *prefix_new(void);
extern void prefix_free(struct prefix **p);
/*
 * Function to handle prefix_free being used as a del function.
 */
extern void prefix_free_lists(void *arg);
/*
 *   prefix_family_str：返回前缀地址族的字符串名。
 *   prefix_blen      ：返回该族地址的字节长度（IPv4 为 4，IPv6 为 16）。
 *   str2prefix       ：把 "10.0.0.0/8" 之类字符串解析进 struct prefix，成功返回 1。
 */
extern const char *prefix_family_str(union prefixconstptr pu);
extern int prefix_blen(union prefixconstptr pu);
extern int str2prefix(const char *string, struct prefix *prefix);

#define PREFIX2STR_BUFFER  PREFIX_STRLEN

/*
 * 显示与转换辅助：
 *   prefix_mcast_ip_dump / prefix_mcast_inet4_dump：把组播地址格式化进缓冲区。
 *   prefix_sg2str ：把 (S,G) 结构格式化为字符串。
 *   prefix2str    ：把任意前缀格式化为 "addr/len" 形式的字符串。
 */
extern void prefix_mcast_ip_dump(const char *onfail, const struct ipaddr *addr,
				 char *buf, int buf_size);
extern void prefix_mcast_inet4_dump(const char *onfail, struct in_addr addr,
				char *buf, int buf_size);
extern const char *prefix_sg2str(const struct prefix_sg *sg, char *str);
extern const char *prefix2str(union prefixconstptr upfx, char *buffer,
			      int size);
/*
 * EVPN type-5（IP-Prefix）前缀匹配：判断 evpn_pfx 是否匹配 match_pfx。
 */
extern int evpn_type5_prefix_match(const struct prefix *evpn_pfx,
				   const struct prefix *match_pfx);
/*
 * 前缀核心关系判定（入参用 union 以兼容任意族）：
 *   prefix_match                  ：unet 是否包含 upfx（前缀归属判定）。
 *   prefix_match_network_statement：网络语句式匹配（对主机位的处理略有不同）。
 *   prefix_same                   ：两前缀是否完全相同（族、长度、地址都相同）。
 *   prefix_cmp                    ：比较两前缀顺序，返回负数 / 0 / 正数。
 *   prefix_common_bits            ：两前缀从最高位起连续相同的比特数。
 *   prefix_copy                   ：把 usrc 复制到 udst（按地址族选择正确长度）。
 */
extern int prefix_match(union prefixconstptr unet, union prefixconstptr upfx);
extern int prefix_match_network_statement(union prefixconstptr unet,
					  union prefixconstptr upfx);
extern int prefix_same(union prefixconstptr ua, union prefixconstptr ub);
extern int prefix_cmp(union prefixconstptr ua, union prefixconstptr ub);
extern int prefix_common_bits(union prefixconstptr ua, union prefixconstptr ub);
extern void prefix_copy(union prefixptr udst, union prefixconstptr usrc);
/*
 * 把前缀的“主机位”清零，得到规范的前缀形式（例如 10.1.2.3/8 变成 10.0.0.0/8）。
 * 原地修改 *pu，按 family 选择合适的地址长度。
 */
extern void apply_mask(union prefixptr pu);
/*
 * 比较两个 EVPN 地址是否相同（逐字段按 route_type 判定）。
 */
extern bool evpn_addr_same(const struct evpn_addr *e1, const struct evpn_addr *e2);

#ifdef __clang_analyzer__
/*
 * 绕过 clang 静态分析器对 transparent union 的误报：分析器会以为
 * prefix_copy 的目标未初始化，故此处先用 memset 清零再调用真实函数。
 * 仅在 clang 静态分析（__clang_analyzer__）时生效，正常运行不涉及。
 */
/* clang-SA doesn't understand transparent unions, making it think that the
 * target of prefix_copy is uninitialized.  So just memset the target.
 * cf. https://bugs.llvm.org/show_bug.cgi?id=42811
 */
#define prefix_copy(a, b) ({ memset(a, 0, sizeof(*a)); prefix_copy(a, b); })
#endif

/*
 * 套接字地址（sockunion）与前缀互转：
 *   sockunion2hostprefix：把 sockunion 转成对应族的 host 前缀，返回该前缀。
 *   prefix2sockunion    ：把前缀写成 sockunion（用于 bind / 连接等场景）。
 */
extern struct prefix *sockunion2hostprefix(const union sockunion *su,
					   struct prefix *p);
extern void prefix2sockunion(const struct prefix *p, union sockunion *su);

/*
 * 解析 "aa:bb:cc:dd:ee:ff" 形式的 MAC 字符串到 struct prefix_eth，成功返回 1。
 */
extern int str2prefix_eth(const char *string, struct prefix_eth *p);

/*
 * IPv4 前缀的创建 / 销毁 / 解析 / 规整：
 *   prefix_ipv4_new / prefix_ipv4_free：分配与释放 struct prefix_ipv4。
 *   str2prefix_ipv4 ：解析 "a.b.c.d/len" 到 struct prefix_ipv4。
 *   apply_mask_ipv4 ：把 IPv4 前缀的主机位清零（得到规范前缀）。
 */
extern struct prefix_ipv4 *prefix_ipv4_new(void);
extern void prefix_ipv4_free(struct prefix_ipv4 **p);
extern int str2prefix_ipv4(const char *string, struct prefix_ipv4 *p);
extern void apply_mask_ipv4(struct prefix_ipv4 *p);

/*
 *   prefix_ipv4_any          ：判断是否为 0.0.0.0/0（默认路由）。
 *   apply_classful_mask_ipv4 ：按有类边界（A / B / C 类）应用掩码，兼容旧式有类前缀。
 */
extern int prefix_ipv4_any(const struct prefix_ipv4 *p);
extern void apply_classful_mask_ipv4(struct prefix_ipv4 *p);

/*
 * IPv4 掩码与长度互转及广播地址计算：
 *   ip_masklen         ：把点分掩码（如 255.255.0.0）转成掩码长度。
 *   masklen2ip         ：把掩码长度转成点分掩码。
 *   ipv4_broadcast_addr：由主机地址与掩码长度算出网络广播地址；
 *                        /31 按 RFC3021 3.3 特殊处理（点对点链路无广播）。
 */
extern uint8_t ip_masklen(struct in_addr addr);
extern void masklen2ip(const int length, struct in_addr *addr);
/* given the address of a host on a network and the network mask length,
 * calculate the broadcast address for that network;
 * special treatment for /31 according to RFC3021 section 3.3 */
extern in_addr_t ipv4_broadcast_addr(in_addr_t hostaddr, int masklen);

/*
 * 把“网络地址字符串 + 掩码字符串”组合成 "prefix/len" 字符串，返回 0 表示成功。
 */
extern int netmask_str2prefix_str(const char *net_str, const char *mask_str,
				  char *prefix_str, size_t prefix_str_len);

/*
 * IPv6 前缀的创建 / 销毁 / 解析 / 规整，与 IPv4 版本一一对应。
 */
extern struct prefix_ipv6 *prefix_ipv6_new(void);
extern void prefix_ipv6_free(struct prefix_ipv6 **p);
extern int str2prefix_ipv6(const char *str, struct prefix_ipv6 *p);
extern void apply_mask_ipv6(struct prefix_ipv6 *p);

/*
 * IPv6 掩码与长度互转：ip6_masklen 由 in6_addr 掩码得长度，
 * masklen2ip6 由长度得 in6_addr 掩码。
 */
extern int ip6_masklen(struct in6_addr netmask);
extern void masklen2ip6(const int masklen, struct in6_addr *netmask);

/*
 * 以太网 MAC 辅助：
 *   is_zero_mac   ：是否为全 0 MAC。
 *   is_mcast_mac  ：是否为组播 MAC（首字节最低位为 1）。
 *   is_bcast_mac  ：是否为广播 MAC（ff:ff:ff:ff:ff:ff）。
 *   prefix_str2mac：解析 MAC 字符串进 struct ethaddr。
 *   prefix_mac2str：把 struct ethaddr 格式化为字符串写入 buf。
 */
extern int is_zero_mac(const struct ethaddr *mac);
extern bool is_mcast_mac(const struct ethaddr *mac);
extern bool is_bcast_mac(const struct ethaddr *mac);
extern int prefix_str2mac(const char *str, struct ethaddr *mac);
extern char *prefix_mac2str(const struct ethaddr *mac, char *buf, int size);

/*
 * 计算一个前缀的哈希键，供哈希表（如前缀去重）使用。
 */
extern unsigned prefix_hash_key(const void *pp);

/*
 * ESI 与 EVPN 相关辅助：
 *   str_to_esi / esi_to_str ：ESI 的字符串与二进制互转。
 *   evpn_es_df_alg2str      ：DF 选举算法编号转名字字符串。
 *   prefix_evpn_hexdump     ：十六进制转储一条 EVPN 前缀（调试用）。
 *   ipv4_unicast_valid      ：是否为合法单播 IPv4（排除 0/8、环回、组播等）。
 *   ipv4_ietf_unicast_valid ：按 IETF 更严格口径判断合法单播 IPv4。
 *   evpn_prefix2prefix      ：从 EVPN type-5 前缀提取出内部 IP 前缀。
 */
extern int str_to_esi(const char *str, esi_t *esi);
extern char *esi_to_str(const esi_t *esi, char *buf, int size);
extern char *evpn_es_df_alg2str(uint8_t df_alg, char *buf, int buf_len);
extern void prefix_evpn_hexdump(const struct prefix_evpn *p);
extern bool ipv4_unicast_valid(const struct in_addr *addr);
extern bool ipv4_ietf_unicast_valid(const struct in_addr *addr);
extern int evpn_prefix2prefix(const struct prefix *evpn, struct prefix *to);

/*
 * 判断 IPv6 地址是否为“火星地址”（此处仅排除环回 ::1）。
 */
static inline int ipv6_martian(const struct in6_addr *addr)
{
	struct in6_addr localhost_addr;

	inet_pton(AF_INET6, "::1", &localhost_addr);

	if (IPV6_ADDR_SAME(&localhost_addr, addr))
		return 1;

	return 0;
}

/*
 * 解析 "aa:bb:.../ip..." 形式的 EVPN MAC/IP 前缀字符串到 struct prefix_evpn。
 */
extern int macstr2prefix_evpn(const char *str, struct prefix_evpn *p);

/* NOTE: This routine expects the address argument in network byte order. */
/*
 * 判断 IPv4 地址是否为“火星地址”：只要不是合法单播即视为火星。
 * 注意：调用方需保证传入地址为网络字节序（见上方英文注释）。
 */
static inline bool ipv4_martian(const struct in_addr *addr)
{
	if (!ipv4_unicast_valid(addr))
		return true;
	return false;
}

/*
 * 判断是否为默认路由前缀（掩码长度为 0 且地址为任意 / 全零）；
 * is_default_prefix 按 family 分派到 v4 / v6 版本。
 */
static inline bool is_default_prefix4(const struct prefix_ipv4 *p)
{
	return p && p->family == AF_INET && p->prefixlen == 0
	       && p->prefix.s_addr == INADDR_ANY;
}

static inline bool is_default_prefix6(const struct prefix_ipv6 *p)
{
	return p && p->family == AF_INET6 && p->prefixlen == 0
	       && memcmp(&p->prefix, &in6addr_any, sizeof(struct in6_addr))
			  == 0;
}

static inline bool is_default_prefix(const struct prefix *p)
{
	if (p == NULL)
		return false;

	switch (p->family) {
	case AF_INET:
		return is_default_prefix4((const struct prefix_ipv4 *)p);
	case AF_INET6:
		return is_default_prefix6((const struct prefix_ipv6 *)p);
	}

	return false;
}

/*
 * 判断是否为主机路由：IPv4 掩码长度为 32 或 IPv6 为 128。
 */
static inline int is_host_route(const struct prefix *p)
{
	if (p->family == AF_INET)
		return (p->prefixlen == IPV4_MAX_BITLEN);
	else if (p->family == AF_INET6)
		return (p->prefixlen == IPV6_MAX_BITLEN);
	return 0;
}

/*
 * 判断是否为“默认主机路由”：地址全零且掩码长度为满长（32 或 128）。
 */
static inline int is_default_host_route(const struct prefix *p)
{
	if (p->family == AF_INET) {
		return (p->u.prefix4.s_addr == INADDR_ANY &&
			p->prefixlen == IPV4_MAX_BITLEN);
	} else if (p->family == AF_INET6) {
		return ((!memcmp(&p->u.prefix6, &in6addr_any,
				 sizeof(struct in6_addr))) &&
			p->prefixlen == IPV6_MAX_BITLEN);
	}
	return 0;
}

/*
 * 判断 IPv6 地址是否属于全局单播：排除未指定、环回、链路本地、组播。
 */
static inline bool is_ipv6_global_unicast(const struct in6_addr *p)
{
	if (IN6_IS_ADDR_UNSPECIFIED(p) || IN6_IS_ADDR_LOOPBACK(p) ||
	    IN6_IS_ADDR_LINKLOCAL(p) || IN6_IS_ADDR_MULTICAST(p))
		return false;

	return true;
}

/*
 * 组播作用域（scope）取值，IPv4 与 IPv6 通用（对应地址中的 scope 字段）。
 * 数值越小作用域越小：接口 / 链路 / realm / 管理 / 站点 / 组织 / 全局。
 */
/* IPv6 scope values, usable for IPv4 too (cf. below) */
/* clang-format off */
enum {
	/* 0: reserved */
	MCAST_SCOPE_IFACE  = 0x1,
	MCAST_SCOPE_LINK   = 0x2,
	MCAST_SCOPE_REALM  = 0x3,
	MCAST_SCOPE_ADMIN  = 0x4,
	MCAST_SCOPE_SITE   = 0x5,
	/* 6-7: unassigned */
	MCAST_SCOPE_ORG    = 0x8,
	/* 9-d: unassigned */
	MCAST_SCOPE_GLOBAL = 0xe,
	/* f: reserved */
};
/* clang-format on */

/*
 * IPv6 组播地址属性（对 in6_addr 操作）：
 *   ipv6_mcast_scope   ：取 scope 字段（第 2 字节低 4 位）。
 *   ipv6_mcast_nofwd   ：scope 不大于链路本地时不可转发。
 *   ipv6_mcast_ssm     ：是否为 SSM 地址段 ff3x::/32。
 *   ipv6_mcast_reserved：是否为保留段 ffx2::/16。
 */
static inline uint8_t ipv6_mcast_scope(const struct in6_addr *addr)
{
	return addr->s6_addr[1] & 0xf;
}

static inline bool ipv6_mcast_nofwd(const struct in6_addr *addr)
{
	return (addr->s6_addr[1] & 0xf) <= MCAST_SCOPE_LINK;
}

static inline bool ipv6_mcast_ssm(const struct in6_addr *addr)
{
	uint32_t bits = ntohl(addr->s6_addr32[0]);

	/* ff3x:0000::/32 */
	return (bits & 0xfff0ffff) == 0xff300000;
}

static inline bool ipv6_mcast_reserved(const struct in6_addr *addr)
{
	uint32_t bits = ntohl(addr->s6_addr32[0]);

	/* ffx2::/16 */
	return (bits & 0xff0fffff) == 0xff020000;
}

/*
 * IPv4 组播作用域判定：
 *   ipv4_mcast_scope：224.0.0.0/24 为链路，239.0.0.0/8 为组织，其余为全局。
 *   ipv4_mcast_nofwd：224.0.0.0/24 链路本地组播不可转发。
 */
static inline uint8_t ipv4_mcast_scope(const struct in_addr *addr)
{
	uint32_t bits = ntohl(addr->s_addr);

	/* 224.0.0.0/24 - link scope */
	if ((bits & 0xffffff00) == 0xe0000000)
		return MCAST_SCOPE_LINK;
	/* 239.0.0.0/8 - org scope */
	if ((bits & 0xff000000) == 0xef000000)
		return MCAST_SCOPE_ORG;

	return MCAST_SCOPE_GLOBAL;
}

static inline bool ipv4_mcast_nofwd(const struct in_addr *addr)
{
	uint32_t bits = ntohl(addr->s_addr);

	/* 224.0.0.0/24 */
	return (bits & 0xffffff00) == 0xe0000000;
}

/*
 * 判断 IPv4 地址是否属于 SSM 组播段（232.0.0.0/8）。
 */
static inline bool ipv4_mcast_ssm(const struct in_addr *addr)
{
	uint32_t bits = ntohl(addr->s_addr);

	/* 232.0.0.0/8 */
	return (bits & 0xff000000) == 0xe8000000;
}

#ifdef _FRR_ATTRIBUTE_PRINTFRR
/*
 * printfrr 扩展：向 lib/printfrr 注册自定义格式符，遇到对应指针类型时自动
 * 按指定格式渲染。例如 %pFX 渲染任意 struct prefix，%pI4 / %pI6 渲染 v4 / v6
 * 地址，%pEA 渲染 ethaddr，%pRDP / %pRDD / %pRDE 渲染路由区分符，%pPSG 渲染 (S,G)。
 * 仅在启用 _FRR_ATTRIBUTE_PRINTFRR 时生效，属编译期机制。
 */
#pragma FRR printfrr_ext "%pEA"  (struct ethaddr *)

#pragma FRR printfrr_ext "%pI4"  (struct in_addr *)
#pragma FRR printfrr_ext "%pI4"  (in_addr_t *)

#pragma FRR printfrr_ext "%pI6"  (struct in6_addr *)

#pragma FRR printfrr_ext "%pFX"  (struct prefix *)
#pragma FRR printfrr_ext "%pFX"  (struct prefix_ipv4 *)
#pragma FRR printfrr_ext "%pFX"  (struct prefix_ipv6 *)
#pragma FRR printfrr_ext "%pFX"  (struct prefix_eth *)
#pragma FRR printfrr_ext "%pFX"  (struct prefix_evpn *)
#pragma FRR printfrr_ext "%pFX"  (struct prefix_fs *)
#pragma FRR printfrr_ext "%pRDP"  (struct prefix_rd *)
/* RD with AS4B with dot and dot+ format */
#pragma FRR printfrr_ext "%pRDD"  (struct prefix_rd *)
#pragma FRR printfrr_ext "%pRDE"  (struct prefix_rd *)

#pragma FRR printfrr_ext "%pPSG" (struct prefix_sg *)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_PREFIX_H */
