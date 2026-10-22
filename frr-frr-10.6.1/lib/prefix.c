// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Prefix related functions.
 * Copyright (C) 1997, 98, 99 Kunihiro Ishiguro
 */

/*
 * 文件导读：prefix.c —— IP 前缀运算底座
 *
 * 本文件实现 struct prefix（见 prefix.h）的全部运算原语，是 FRR 各协议模块
 * 处理“网络前缀 / 地址”时共同的底层依赖。它与 plist.c 的分工是：
 *   prefix.c ：前缀本身怎么算——掩码长度、包含关系、相等比较、字符串互转、
 *              掩码应用、跨地址族（IPv4 / IPv6 / EVPN / flowspec）的统一处理。
 *   plist.c  ：在此地基之上实现 prefix-list 的按条目匹配引擎。
 *
 * 主要函数族（按出现顺序）：
 *   - 地址族转换与显示：str2family / family2str / afi2family / family2afi /
 *     afi2str / safi2str 等，负责 AFI 与 family、字符串之间的映射。
 *   - 包含与相等：prefix_match（n 是否包含 p）、prefix_same、prefix_cmp、
 *     prefix_common_bits、evpn_type5_prefix_match。
 *   - 掩码与规整：apply_mask / apply_mask_ipv4 / apply_mask_ipv6、ip_masklen /
 *     masklen2ip、ip6_masklen / masklen2ip6 等。
 *   - 字符串互转：str2prefix / prefix2str / str2prefix_ipv4 / str2prefix_ipv6、
 *     MAC 与 ESI 的互转。
 *   - 内存与哈希：prefix_new / prefix_free、prefix_hash_key。
 *
 * 贯穿全文件的关键点：
 *   1) prefixlen 一律是“位数”；换算字节用 PSIZE，取某一位用 prefix_bit。
 *   2) maskbit 数组把“取前 k 位掩码”的头部字节做成了查表，避免逐位运算。
 *   3) 因为所有 prefix_* 结构都以 family 开头，函数常用 union prefixconstptr
 *      接收，再按 family 强转成具体类型处理。
 */

#include <zebra.h>

#include "command.h"
#include "prefix.h"
#include "ipaddr.h"
#include "vty.h"
#include "sockunion.h"
#include "memory.h"
#include "log.h"
#include "jhash.h"
#include "lib_errors.h"
#include "printfrr.h"
#include "vxlan.h"

DEFINE_MTYPE_STATIC(LIB, PREFIX, "Prefix");
DEFINE_MTYPE_STATIC(LIB, PREFIX_FLOWSPEC, "Prefix Flowspec");

/*
 * maskbit 查表：maskbit[k] 给出“保留高 k 位、其余清零”时头部字节的掩码值。
 * 例如 k=0 得 0x00，k=8 得 0xff，k=1 得 0x80。按位比较时直接查表避免逐位循环。
 */
/* Maskbit. */
static const uint8_t maskbit[] = {0x00, 0x80, 0xc0, 0xe0, 0xf0,
				  0xf8, 0xfc, 0xfe, 0xff};

/* Number of bits in prefix type. */
#ifndef PNBBY
#define PNBBY 8
#endif /* PNBBY */

/*
 * PNBBY：每个“前缀单元”的位数，此处按字节处理，取 8。
 * MASKBIT(offset)：由偏移量（1..8）算出掩码字节，等价于 maskbit[offset]。
 */
#define MASKBIT(offset)  ((0xff << (PNBBY - (offset))) & 0xff)

/*
 * 判断 MAC 是否为全 0。返回非 0 表示全 0。
 */
int is_zero_mac(const struct ethaddr *mac)
{
	int i = 0;

	for (i = 0; i < ETH_ALEN; i++) {
		if (mac->octet[i])
			return 0;
	}

	return 1;
}

/*
 * 判断 MAC 是否为广播地址（6 字节全 0xFF）。
 */
bool is_bcast_mac(const struct ethaddr *mac)
{
	int i = 0;

	for (i = 0; i < ETH_ALEN; i++)
		if (mac->octet[i] != 0xFF)
			return false;

	return true;
}

/*
 * 判断 MAC 是否为组播：以太网首字节的最低位（I/G 位）为 1。
 */
bool is_mcast_mac(const struct ethaddr *mac)
{
	if ((mac->octet[0] & 0x01) == 0x01)
		return true;

	return false;
}

/*
 * 取字节缓冲 prefix 中第 bit_index 位的值（0 或 1）。
 * offset 定位到字节，shift 定位到该字节从高位起的那一位。
 */
unsigned int prefix_bit(const uint8_t *prefix, const uint16_t bit_index)
{
	unsigned int offset = bit_index / 8;
	unsigned int shift = 7 - (bit_index % 8);

	return (prefix[offset] >> shift) & 1;
}

/*
 * 把字符串地址族名（ipv4 / ipv6 / ethernet / evpn）转成内部 family 编号；
 * 无法识别时返回 -1。
 */
int str2family(const char *string)
{
	if (!strcmp("ipv4", string))
		return AF_INET;
	else if (!strcmp("ipv6", string))
		return AF_INET6;
	else if (!strcmp("ethernet", string))
		return AF_ETHERNET;
	else if (!strcmp("evpn", string))
		return AF_EVPN;
	return -1;
}

/*
 * 把内部 family 编号转成显示用字符串（IPv4 / IPv6 / Ethernet / Evpn），
 * 未知族返回 "?"。
 */
const char *family2str(int family)
{
	switch (family) {
	case AF_INET:
		return "IPv4";
	case AF_INET6:
		return "IPv6";
	case AF_ETHERNET:
		return "Ethernet";
	case AF_EVPN:
		return "Evpn";
	}
	return "?";
}

/* Address Family Identifier to Address Family converter. */
/*
 * AFI 到 family 的映射：AFI_IP 到 AF_INET，AFI_IP6 到 AF_INET6，
 * AFI_L2VPN 到 AF_ETHERNET；其余返回 0（注意 EVPN 代码不应走此接口）。
 */
int afi2family(afi_t afi)
{
	if (afi == AFI_IP)
		return AF_INET;
	else if (afi == AFI_IP6)
		return AF_INET6;
	else if (afi == AFI_L2VPN)
		return AF_ETHERNET;
	/* NOTE: EVPN code should NOT use this interface. */
	return 0;
}

/*
 * family 到 AFI 的逆映射：AF_INET 到 AFI_IP，AF_INET6 到 AFI_IP6，
 * AF_ETHERNET / AF_EVPN 都归为 AFI_L2VPN；其余返回 0。
 */
afi_t family2afi(int family)
{
	if (family == AF_INET)
		return AFI_IP;
	else if (family == AF_INET6)
		return AFI_IP6;
	else if (family == AF_ETHERNET || family == AF_EVPN)
		return AFI_L2VPN;
	return 0;
}

/*
 * 把 AFI 转成小写显示名（ipv4 / ipv6 / l2vpn）。
 * 对 AFI_MAX / AFI_UNSPEC 返回 "bad-value"，其余非法值触发 assert。
 */
const char *afi2str_lower(afi_t afi)
{
	switch (afi) {
	case AFI_IP:
		return "ipv4";
	case AFI_IP6:
		return "ipv6";
	case AFI_L2VPN:
		return "l2vpn";
	case AFI_MAX:
	case AFI_UNSPEC:
		return "bad-value";
	}

	assert(!"Reached end of function we should never reach");
	return "DEV ESCAPE";
}

/*
 * 把 AFI 转成显示名（IPv4 / IPv6 / l2vpn）；AFI_MAX / AFI_UNSPEC 返回 "bad-value"。
 */
const char *afi2str(afi_t afi)
{
	switch (afi) {
	case AFI_IP:
		return "IPv4";
	case AFI_IP6:
		return "IPv6";
	case AFI_L2VPN:
		return "l2vpn";
	case AFI_MAX:
	case AFI_UNSPEC:
		return "bad-value";
	}

	assert(!"Reached end of function we should never reach");
	return "DEV ESCAPE";
}

/*
 * 把 SAFI 转成显示名（unicast / multicast / vpn / encap / evpn /
 * labeled-unicast / flowspec）；未知值返回 "unknown"。
 */
const char *safi2str(safi_t safi)
{
	switch (safi) {
	case SAFI_UNICAST:
		return "unicast";
	case SAFI_MULTICAST:
		return "multicast";
	case SAFI_MPLS_VPN:
		return "vpn";
	case SAFI_ENCAP:
		return "encap";
	case SAFI_EVPN:
		return "evpn";
	case SAFI_LABELED_UNICAST:
		return "labeled-unicast";
	case SAFI_FLOWSPEC:
		return "flowspec";
	case SAFI_UNSPEC:
	case SAFI_MAX:
		return "unknown";
	}

	assert(!"Reached end of function we should never reach");
	return "DEV ESCAPE";
}

/* If n includes p prefix then return 1 else return 0. */
/*
 * 判断前缀 n 是否包含前缀 p（即 p 落在 n 所表示的网络内），包含返回 1。
 * 规则：n 的掩码长度必须不大于 p 的掩码长度，且两者在 n 前缀长度内逐位相同。
 * 特例：flowspec 前缀不使用 prefixlen，而比较 u.prefix_flowspec 内的有效字节。
 * 这是最常用的“路由是否属于某网段”判定，是大量协议模块的基础调用。
 */
int prefix_match(union prefixconstptr unet, union prefixconstptr upfx)
{
	const struct prefix *n = unet.p;
	const struct prefix *p = upfx.p;
	int offset;
	int shift;
	const uint8_t *np, *pp;

	/* If n's prefix is longer than p's one return 0. */
	if (n->prefixlen > p->prefixlen)
		return 0;

	if (n->family == AF_FLOWSPEC) {
		/* prefixlen is unused. look at fs prefix len */
		if (n->u.prefix_flowspec.family !=
		    p->u.prefix_flowspec.family)
			return 0;

		if (n->u.prefix_flowspec.prefixlen >
		    p->u.prefix_flowspec.prefixlen)
			return 0;

		/* Set both prefix's head pointer. */
		np = (const uint8_t *)&n->u.prefix_flowspec.ptr;
		pp = (const uint8_t *)&p->u.prefix_flowspec.ptr;

		offset = n->u.prefix_flowspec.prefixlen;

		while (offset--)
			if (np[offset] != pp[offset])
				return 0;
		return 1;
	}

	/* Set both prefix's head pointer. */
	np = n->u.val;
	pp = p->u.val;

	offset = n->prefixlen / PNBBY;
	shift = n->prefixlen % PNBBY;

	if (shift)
		if (maskbit[shift] & (np[offset] ^ pp[offset]))
			return 0;

	while (offset--)
		if (np[offset] != pp[offset])
			return 0;
	return 1;

}

/*
 * n is a type5 evpn prefix. This function tries to see if there is an
 * ip-prefix within n which matches prefix p
 * If n includes p prefix then return 1 else return 0.
 */
/*
 * 判断 EVPN type-5（IP-Prefix）前缀 n 内嵌的 IP 前缀是否包含前缀 p，包含返回 1。
 * 依据 n 内嵌地址的族（v4 / v6）选用不同长度做按位比较。
 */
int evpn_type5_prefix_match(const struct prefix *n, const struct prefix *p)
{
	int offset;
	int shift;
	int prefixlen;
	const uint8_t *np, *pp;
	struct prefix_evpn *evp;

	if (n->family != AF_EVPN)
		return 0;

	evp = (struct prefix_evpn *)n;
	pp = p->u.val;

	if ((evp->prefix.route_type != 5) ||
	    (p->family == AF_INET6 && !is_evpn_prefix_ipaddr_v6(evp)) ||
	    (p->family == AF_INET && !is_evpn_prefix_ipaddr_v4(evp)) ||
	    (is_evpn_prefix_ipaddr_none(evp)))
		return 0;

	prefixlen = evp->prefix.prefix_addr.ip_prefix_length;
	np = evp->prefix.prefix_addr.ip.ip.addrbytes;

	/* If n's prefix is longer than p's one return 0. */
	if (prefixlen > p->prefixlen)
		return 0;

	offset = prefixlen / PNBBY;
	shift = prefixlen % PNBBY;

	if (shift)
		if (maskbit[shift] & (np[offset] ^ pp[offset]))
			return 0;

	while (offset--)
		if (np[offset] != pp[offset])
			return 0;
	return 1;

}

/* If n includes p then return 1 else return 0. Prefix mask is not considered */
/*
 * 与 prefix_match 类似，判断 n 是否包含 p，但不考虑前缀掩码（长度）约束，
 * 只比较 n 前缀长度范围内的位。用于网络语句式的匹配场景。
 */
int prefix_match_network_statement(union prefixconstptr unet,
				   union prefixconstptr upfx)
{
	const struct prefix *n = unet.p;
	const struct prefix *p = upfx.p;
	int offset;
	int shift;
	const uint8_t *np, *pp;

	/* Set both prefix's head pointer. */
	np = n->u.val;
	pp = p->u.val;

	offset = n->prefixlen / PNBBY;
	shift = n->prefixlen % PNBBY;

	if (shift)
		if (maskbit[shift] & (np[offset] ^ pp[offset]))
			return 0;

	while (offset--)
		if (np[offset] != pp[offset])
			return 0;
	return 1;
}

/*
 * 静态分析时撤销 prefix.h 里 prefix_copy 的宏包装，避免递归展开，
 * 保证下面这份真实实现能被分析到。
 */
#ifdef __clang_analyzer__
#undef prefix_copy	/* cf. prefix.h */
#endif

/*
 * 把源前缀 src 完整复制到目标 dest（含 family、prefixlen 与地址负载）。
 * 按 family 分派：v4 / v6 直接赋值，以太网与 EVPN 用 memcpy，AF_UNSPEC 复制
 * lp 的两个字段；flowspec 会为新 dest 单独分配一份负载内存并拷贝（记得释放）。
 * 未识别的族会记录错误并 assert。
 */
void prefix_copy(union prefixptr udest, union prefixconstptr usrc)
{
	struct prefix *dest = udest.p;
	const struct prefix *src = usrc.p;

	dest->family = src->family;
	dest->prefixlen = src->prefixlen;

	if (src->family == AF_INET)
		dest->u.prefix4 = src->u.prefix4;
	else if (src->family == AF_INET6)
		dest->u.prefix6 = src->u.prefix6;
	else if (src->family == AF_ETHERNET) {
		memcpy(&dest->u.prefix_eth, &src->u.prefix_eth,
		       sizeof(struct ethaddr));
	} else if (src->family == AF_EVPN) {
		memcpy(&dest->u.prefix_evpn, &src->u.prefix_evpn,
		       sizeof(struct evpn_addr));
	} else if (src->family == AF_UNSPEC) {
		dest->u.lp.id = src->u.lp.id;
		dest->u.lp.adv_router = src->u.lp.adv_router;
	} else if (src->family == AF_FLOWSPEC) {
		void *temp;
		int len;

		len = src->u.prefix_flowspec.prefixlen;
		dest->u.prefix_flowspec.prefixlen =
			src->u.prefix_flowspec.prefixlen;
		dest->u.prefix_flowspec.family =
			src->u.prefix_flowspec.family;
		dest->family = src->family;
		temp = XCALLOC(MTYPE_PREFIX_FLOWSPEC, len);
		dest->u.prefix_flowspec.ptr = (uintptr_t)temp;
		memcpy((void *)dest->u.prefix_flowspec.ptr,
		       (void *)src->u.prefix_flowspec.ptr, len);
	} else {
		flog_err(EC_LIB_DEVELOPMENT,
			 "prefix_copy(): Unknown address family %d",
			 src->family);
		assert(0);
	}
}

/*
 * 比较两个 EVPN 地址是否完全相同：先比 route_type，再按类型逐字段比较
 * （ESI、以太网标签、MAC、IP、掩码长度等）。类型一致但未列出的情况返回 true。
 */
bool evpn_addr_same(const struct evpn_addr *e1, const struct evpn_addr *e2)
{
	if (e1->route_type != e2->route_type)
		return false;
	if (e1->route_type == BGP_EVPN_AD_ROUTE)
		return (!memcmp(&e1->ead_addr.esi.val,
				&e2->ead_addr.esi.val, ESI_BYTES) &&
			e1->ead_addr.eth_tag == e2->ead_addr.eth_tag &&
			!ipaddr_cmp(&e1->ead_addr.ip, &e2->ead_addr.ip));
	if (e1->route_type == BGP_EVPN_MAC_IP_ROUTE)
		return (e1->macip_addr.eth_tag == e2->macip_addr.eth_tag &&
			e1->macip_addr.ip_prefix_length
				== e2->macip_addr.ip_prefix_length &&
			!memcmp(&e1->macip_addr.mac,
				&e2->macip_addr.mac, ETH_ALEN) &&
			!ipaddr_cmp(&e1->macip_addr.ip, &e2->macip_addr.ip));
	if (e1->route_type == BGP_EVPN_IMET_ROUTE)
		return (e1->imet_addr.eth_tag == e2->imet_addr.eth_tag &&
			e1->imet_addr.ip_prefix_length
				== e2->imet_addr.ip_prefix_length &&
			!ipaddr_cmp(&e1->imet_addr.ip, &e2->imet_addr.ip));
	if (e1->route_type == BGP_EVPN_ES_ROUTE)
		return (!memcmp(&e1->es_addr.esi.val,
				&e2->es_addr.esi.val, ESI_BYTES) &&
			e1->es_addr.ip_prefix_length
				== e2->es_addr.ip_prefix_length &&
			!ipaddr_cmp(&e1->es_addr.ip, &e2->es_addr.ip));
	if (e1->route_type == BGP_EVPN_IP_PREFIX_ROUTE)
		return (e1->prefix_addr.eth_tag == e2->prefix_addr.eth_tag &&
			e1->prefix_addr.ip_prefix_length
				== e2->prefix_addr.ip_prefix_length &&
			!ipaddr_cmp(&e1->prefix_addr.ip, &e2->prefix_addr.ip));
	return true;
}

/*
 * Return 1 if the address/netmask contained in the prefix structure
 * is the same, and else return 0.  For this routine, 'same' requires
 * that not only the prefix length and the network part be the same,
 * but also the host part.  Thus, 10.0.0.1/8 and 10.0.0.2/8 are not
 * the same.  Note that this routine has the same return value sense
 * as '==' (which is different from prefix_cmp).
 */
/*
 * 判断两前缀是否“完全相同”，相同返回 1（返回语义等同 ==，与 prefix_cmp 不同）。
 * 这里的“相同”不仅要求前缀长度与网络位一致，主机位也须一致，
 * 因此 10.0.0.1/8 与 10.0.0.2/8 并不相同。
 * 允许指针为 NULL：一空一非空返回 0，两者都空返回 1。
 */
int prefix_same(union prefixconstptr up1, union prefixconstptr up2)
{
	const struct prefix *p1 = up1.p;
	const struct prefix *p2 = up2.p;

	if ((p1 && !p2) || (!p1 && p2))
		return 0;

	if (!p1 && !p2)
		return 1;

	if (p1->family == p2->family && p1->prefixlen == p2->prefixlen) {
		if (p1->family == AF_INET)
			if (IPV4_ADDR_SAME(&p1->u.prefix4, &p2->u.prefix4))
				return 1;
		if (p1->family == AF_INET6)
			if (IPV6_ADDR_SAME(&p1->u.prefix6.s6_addr,
					   &p2->u.prefix6.s6_addr))
				return 1;
		if (p1->family == AF_ETHERNET)
			if (!memcmp(&p1->u.prefix_eth, &p2->u.prefix_eth,
				    sizeof(struct ethaddr)))
				return 1;
		if (p1->family == AF_EVPN)
			if (evpn_addr_same(&p1->u.prefix_evpn, &p2->u.prefix_evpn))
				return 1;
		if (p1->family == AF_FLOWSPEC) {
			if (p1->u.prefix_flowspec.family !=
			    p2->u.prefix_flowspec.family)
				return 0;
			if (p1->u.prefix_flowspec.prefixlen !=
			    p2->u.prefix_flowspec.prefixlen)
				return 0;
			if (!memcmp(&p1->u.prefix_flowspec.ptr,
				    &p2->u.prefix_flowspec.ptr,
				    p2->u.prefix_flowspec.prefixlen))
				return 1;
		}
	}
	return 0;
}

/*
 * Return -1/0/1 comparing the prefixes in a way that gives a full/linear
 * order.
 *
 * Network prefixes are considered the same if the prefix lengths are equal
 * and the network parts are the same.  Host bits (which are considered masked
 * by the prefix length) are not significant.  Thus, 10.0.0.1/8 and
 * 10.0.0.2/8 are considered equivalent by this routine.  Note that
 * this routine has the same return sense as strcmp (which is different
 * from prefix_same).
 */
/*
 * 按“全序 / 线性序”比较两前缀，返回 -1 / 0 / 1（语义同 strcmp，与 prefix_same 不同）。
 * 只要前缀长度相等且网络位相同即视为等价，主机位被掩码忽略，
 * 因此 10.0.0.1/8 与 10.0.0.2/8 在本题下等价。
 * 先比 family，再比 prefixlen，最后逐字节比较网络位（不足一字节时用 maskbit 取高位）。
 * 是多种有序容器（如前缀表）的排序 / 查找比较函数。
 */
int prefix_cmp(union prefixconstptr up1, union prefixconstptr up2)
{
	const struct prefix *p1 = up1.p;
	const struct prefix *p2 = up2.p;
	int offset;
	int shift;
	int i;

	/* Set both prefix's head pointer. */
	const uint8_t *pp1;
	const uint8_t *pp2;

	if (p1->family != p2->family)
		return numcmp(p1->family, p2->family);
	if (p1->family == AF_FLOWSPEC) {
		pp1 = (const uint8_t *)p1->u.prefix_flowspec.ptr;
		pp2 = (const uint8_t *)p2->u.prefix_flowspec.ptr;

		if (p1->u.prefix_flowspec.family !=
		    p2->u.prefix_flowspec.family)
			return 1;

		if (p1->u.prefix_flowspec.prefixlen !=
		    p2->u.prefix_flowspec.prefixlen)
			return numcmp(p1->u.prefix_flowspec.prefixlen,
				      p2->u.prefix_flowspec.prefixlen);

		offset = p1->u.prefix_flowspec.prefixlen;
		while (offset--)
			if (pp1[offset] != pp2[offset])
				return numcmp(pp1[offset], pp2[offset]);
		return 0;
	}
	pp1 = p1->u.val;
	pp2 = p2->u.val;

	if (p1->prefixlen != p2->prefixlen)
		return numcmp(p1->prefixlen, p2->prefixlen);
	offset = p1->prefixlen / PNBBY;
	shift = p1->prefixlen % PNBBY;

	i = memcmp(pp1, pp2, offset);
	if (i)
		return i;

	/*
	 * At this point offset was the same, if we have shift
	 * that means we still have data to compare, if shift is
	 * 0 then we are at the end of the data structure
	 * and should just return, as that we will be accessing
	 * memory beyond the end of the party zone
	 */
	if (shift)
		return numcmp(pp1[offset] & maskbit[shift],
			      pp2[offset] & maskbit[shift]);

	return 0;
}

/*
 * Count the number of common bits in 2 prefixes. The prefix length is
 * ignored for this function; the whole prefix is compared. If the prefix
 * address families don't match, return -1; otherwise the return value is
 * in range 0 ... maximum prefix length for the address family.
 */
/*
 * 计算两前缀从最高位起连续相同的比特数。忽略前缀长度，比较整个前缀地址。
 * family 不同返回 -1；否则返回值在 0 到该族最大比特数之间。
 * 常用于聚合 / 最长前缀相关判断。
 */
int prefix_common_bits(union prefixconstptr ua, union prefixconstptr ub)
{
	const struct prefix *p1 = ua.p;
	const struct prefix *p2 = ub.p;
	int pos, bit;
	int length = 0;
	uint8_t xor ;

	/* Set both prefix's head pointer. */
	const uint8_t *pp1 = p1->u.val;
	const uint8_t *pp2 = p2->u.val;

	if (p1->family == AF_INET)
		length = IPV4_MAX_BYTELEN;
	if (p1->family == AF_INET6)
		length = IPV6_MAX_BYTELEN;
	if (p1->family == AF_ETHERNET)
		length = ETH_ALEN;
	if (p1->family == AF_EVPN)
		length = 8 * sizeof(struct evpn_addr);

	if (p1->family != p2->family || !length)
		return -1;

	for (pos = 0; pos < length; pos++)
		if (pp1[pos] != pp2[pos])
			break;
	if (pos == length)
		return pos * 8;

	xor = pp1[pos] ^ pp2[pos];
	for (bit = 0; bit < 8; bit++)
		if (xor&(1 << (7 - bit)))
			break;

	return pos * 8 + bit;
}

/* Return prefix family type string. */
/*
 * 返回前缀地址族的短字符串名（inet / inet6 / ether / evpn），未知返回 "unspec"。
 */
const char *prefix_family_str(union prefixconstptr pu)
{
	const struct prefix *p = pu.p;

	if (p->family == AF_INET)
		return "inet";
	if (p->family == AF_INET6)
		return "inet6";
	if (p->family == AF_ETHERNET)
		return "ether";
	if (p->family == AF_EVPN)
		return "evpn";
	return "unspec";
}

/* Allocate new prefix_ipv4 structure. */
/*
 * 分配一个 IPv4 前缀结构（family 置为 AF_INET）。
 * 内部调用 prefix_new 分配完整的 struct prefix 大小，避免后续被强转成
 * struct prefix 时引用到未分配的字节。
 */
struct prefix_ipv4 *prefix_ipv4_new(void)
{
	struct prefix_ipv4 *p;

	/* Call prefix_new to allocate a full-size struct prefix to avoid
	   problems
	   where the struct prefix_ipv4 is cast to struct prefix and unallocated
	   bytes were being referenced (e.g. in structure assignments). */
	p = (struct prefix_ipv4 *)prefix_new();
	p->family = AF_INET;
	return p;
}

/* Free prefix_ipv4 structure. */
/*
 * 释放 IPv4 前缀并把 *p 置 NULL（转调 prefix_free）。
 */
void prefix_ipv4_free(struct prefix_ipv4 **p)
{
	prefix_free((struct prefix **)p);
}

/* If given string is valid return 1 else return 0 */
/*
 * 解析 IPv4 前缀字符串到 struct prefix_ipv4。
 * 含斜杠时按 "a.b.c.d/len" 解析长度（超 32 判非法）；不含斜杠时按主机地址处理，
 * 前缀长度置为 32。成功返回 1，失败返回 0。注意临时串用 MTYPE_TMP 分配后已释放。
 */
int str2prefix_ipv4(const char *str, struct prefix_ipv4 *p)
{
	int ret;
	int plen;
	char *pnt;
	char *cp;

	/* Find slash inside string. */
	pnt = strchr(str, '/');

	/* String doesn't contail slash. */
	if (pnt == NULL) {
		/* Convert string to prefix. */
		ret = inet_pton(AF_INET, str, &p->prefix);
		if (ret == 0)
			return 0;

		/* If address doesn't contain slash we assume it host address.
		 */
		p->family = AF_INET;
		p->prefixlen = IPV4_MAX_BITLEN;

		return ret;
	} else {
		cp = XMALLOC(MTYPE_TMP, (pnt - str) + 1);
		memcpy(cp, str, pnt - str);
		*(cp + (pnt - str)) = '\0';
		ret = inet_pton(AF_INET, cp, &p->prefix);
		XFREE(MTYPE_TMP, cp);
		if (ret == 0)
			return 0;

		/* Get prefix length. */
		plen = (uint8_t)atoi(++pnt);
		if (plen > IPV4_MAX_BITLEN)
			return 0;

		p->family = AF_INET;
		p->prefixlen = plen;
	}

	return ret;
}

/* When string format is invalid return 0. */
/*
 * 解析以太网前缀字符串到 struct prefix_eth。
 * "any" 表示全 0 前缀且长度 0；"aa:bb:...:ff" 或带 "/len" 形式解析 MAC 与长度。
 * 未带斜杠且 MAC 为全 0 时约定长度为 0（兼容旧配置）。成功返回 1。
 */
int str2prefix_eth(const char *str, struct prefix_eth *p)
{
	int ret = 0;
	int plen = 48;
	char *pnt;
	char *cp = NULL;
	const char *str_addr = str;
	unsigned int a[6];
	int i;
	bool slash = false;

	if (!strcmp(str, "any")) {
		memset(p, 0, sizeof(*p));
		p->family = AF_ETHERNET;
		return 1;
	}

	/* Find slash inside string. */
	pnt = strchr(str, '/');

	if (pnt) {
		/* Get prefix length. */
		plen = (uint8_t)atoi(++pnt);
		if (plen > 48) {
			ret = 0;
			goto done;
		}

		cp = XMALLOC(MTYPE_TMP, (pnt - str) + 1);
		memcpy(cp, str, pnt - str);
		*(cp + (pnt - str)) = '\0';

		str_addr = cp;
		slash = true;
	}

	/* Convert string to prefix. */
	if (sscanf(str_addr, "%2x:%2x:%2x:%2x:%2x:%2x", a + 0, a + 1, a + 2,
		   a + 3, a + 4, a + 5)
	    != 6) {
		ret = 0;
		goto done;
	}
	for (i = 0; i < 6; ++i) {
		p->eth_addr.octet[i] = a[i] & 0xff;
	}
	p->prefixlen = plen;
	p->family = AF_ETHERNET;

	/*
	 * special case to allow old configurations to work
	 * Since all zero's is implicitly meant to allow
	 * a comparison to zero, let's assume
	 */
	if (!slash && is_zero_mac(&(p->eth_addr)))
		p->prefixlen = 0;

	ret = 1;

done:
	XFREE(MTYPE_TMP, cp);

	return ret;
}

/* Convert masklen into IP address's netmask (network byte order). */
/*
 * 把掩码长度转成 IPv4 点分掩码（网络字节序），结果写入 *netmask。
 * 掩码长度须在 0 到 32 之间（由 assert 保证）；用 64 位左移规避移位越界 UB。
 */
void masklen2ip(const int masklen, struct in_addr *netmask)
{
	assert(masklen >= 0 && masklen <= IPV4_MAX_BITLEN);

	/* left shift is only defined for less than the size of the type.
	 * we unconditionally use long long in case the target platform
	 * has defined behaviour for << 32 (or has a 64-bit left shift) */

	if (sizeof(unsigned long long) > 4)
		netmask->s_addr = htonl(0xffffffffULL << (32 - masklen));
	else
		netmask->s_addr =
			htonl(masklen ? 0xffffffffU << (32 - masklen) : 0);
}

/* Convert IP address's netmask into integer. We assume netmask is
 * sequential one. Argument netmask should be network byte order. */
/*
 * 把 IPv4 点分掩码（网络字节序、假定为连续的 1）转成掩码长度。
 * 用 __builtin_clz 数前导零；因 clz(0) 行为未定义，全 1 掩码单独返回 32。
 */
uint8_t ip_masklen(struct in_addr netmask)
{
	uint32_t tmp = ~ntohl(netmask.s_addr);

	/*
	 * clz: count leading zeroes. sadly, the behaviour of this builtin is
	 * undefined for a 0 argument, even though most CPUs give 32
	 */
	return tmp ? __builtin_clz(tmp) : 32;
}

/* Apply mask to IPv4 prefix (network byte order). */
/*
 * 把 IPv4 前缀的主机位清零（用掩码与地址做按位与），得到规范前缀。
 */
void apply_mask_ipv4(struct prefix_ipv4 *p)
{
	struct in_addr mask;
	masklen2ip(p->prefixlen, &mask);
	p->prefix.s_addr &= mask.s_addr;
}

/* If prefix is 0.0.0.0/0 then return 1 else return 0. */
/*
 * 判断是否为 0.0.0.0/0（默认路由），是则返回 1。
 */
int prefix_ipv4_any(const struct prefix_ipv4 *p)
{
	return (p->prefix.s_addr == INADDR_ANY && p->prefixlen == 0);
}

/* Allocate a new ip version 6 route */
/*
 * 分配一个 IPv6 前缀结构（family 置为 AF_INET6）。同样按完整 struct prefix 分配。
 */
struct prefix_ipv6 *prefix_ipv6_new(void)
{
	struct prefix_ipv6 *p;

	/* Allocate a full-size struct prefix to avoid problems with structure
	   size mismatches. */
	p = (struct prefix_ipv6 *)prefix_new();
	p->family = AF_INET6;
	return p;
}

/* Free prefix for IPv6. */
/*
 * 释放 IPv6 前缀并把 *p 置 NULL。
 */
void prefix_ipv6_free(struct prefix_ipv6 **p)
{
	prefix_free((struct prefix **)p);
}

/* If given string is valid return 1 else return 0 */
/*
 * 解析 IPv6 前缀字符串到 struct prefix_ipv6。
 * 无斜杠按主机地址处理（长度 128），有斜杠解析长度（超 128 判非法）。
 * 成功返回 1，失败返回 0。
 */
int str2prefix_ipv6(const char *str, struct prefix_ipv6 *p)
{
	char *pnt;
	char *cp;
	int ret;

	pnt = strchr(str, '/');

	/* If string doesn't contain `/' treat it as host route. */
	if (pnt == NULL) {
		ret = inet_pton(AF_INET6, str, &p->prefix);
		if (ret == 0)
			return 0;
		p->prefixlen = IPV6_MAX_BITLEN;
	} else {
		int plen;

		cp = XMALLOC(MTYPE_TMP, (pnt - str) + 1);
		memcpy(cp, str, pnt - str);
		*(cp + (pnt - str)) = '\0';
		ret = inet_pton(AF_INET6, cp, &p->prefix);
		XFREE(MTYPE_TMP, cp);
		if (ret == 0)
			return 0;
		plen = (uint8_t)atoi(++pnt);
		if (plen > IPV6_MAX_BITLEN)
			return 0;
		p->prefixlen = plen;
	}
	p->family = AF_INET6;

	return ret;
}

/* Convert struct in6_addr netmask into integer.
 * FIXME return uint8_t as ip_maskleni() does. */
/*
 * 把 IPv6 掩码（in6_addr）转成掩码长度。
 * 逐 32 位字检查是否为全 1，定位到第一个非全 1 的字后用 clz 算出该字内的位数，
 * 再加上前面 32 的整数倍；全 1 掩码返回 128（因为 clz(0) 行为未定义）。
 * 注意返回值类型为 int（英文注释提到本应统一为 uint8_t）。
 */
int ip6_masklen(struct in6_addr netmask)
{
	if (netmask.s6_addr32[0] != 0xffffffffU)
		return __builtin_clz(~ntohl(netmask.s6_addr32[0]));
	if (netmask.s6_addr32[1] != 0xffffffffU)
		return __builtin_clz(~ntohl(netmask.s6_addr32[1])) + 32;
	if (netmask.s6_addr32[2] != 0xffffffffU)
		return __builtin_clz(~ntohl(netmask.s6_addr32[2])) + 64;
	if (netmask.s6_addr32[3] != 0xffffffffU)
		return __builtin_clz(~ntohl(netmask.s6_addr32[3])) + 96;
	/* note __builtin_clz(0) is undefined */
	return 128;
}

/*
 * 把掩码长度转成 IPv6 掩码（in6_addr）。按长度落在哪个 32 位字分段处理，
 * 该字左移出掩码位，其余字置全 1 或 0。长度应在 0 到 128（assert 保证）。
 */
void masklen2ip6(const int masklen, struct in6_addr *netmask)
{
	assert(masklen >= 0 && masklen <= IPV6_MAX_BITLEN);

	if (masklen == 0) {
		/* note << 32 is undefined */
		memset(netmask, 0, sizeof(*netmask));
	} else if (masklen <= 32) {
		netmask->s6_addr32[0] = htonl(0xffffffffU << (32 - masklen));
		netmask->s6_addr32[1] = 0;
		netmask->s6_addr32[2] = 0;
		netmask->s6_addr32[3] = 0;
	} else if (masklen <= 64) {
		netmask->s6_addr32[0] = 0xffffffffU;
		netmask->s6_addr32[1] = htonl(0xffffffffU << (64 - masklen));
		netmask->s6_addr32[2] = 0;
		netmask->s6_addr32[3] = 0;
	} else if (masklen <= 96) {
		netmask->s6_addr32[0] = 0xffffffffU;
		netmask->s6_addr32[1] = 0xffffffffU;
		netmask->s6_addr32[2] = htonl(0xffffffffU << (96 - masklen));
		netmask->s6_addr32[3] = 0;
	} else {
		netmask->s6_addr32[0] = 0xffffffffU;
		netmask->s6_addr32[1] = 0xffffffffU;
		netmask->s6_addr32[2] = 0xffffffffU;
		netmask->s6_addr32[3] = htonl(0xffffffffU << (128 - masklen));
	}
}

/*
 * 把 IPv6 前缀的主机位清零：定位到前缀长度所在的字节，
 * 用 maskbit 保留其高位，之后所有字节置 0。
 */
void apply_mask_ipv6(struct prefix_ipv6 *p)
{
	uint8_t *pnt;
	int index;
	int offset;

	index = p->prefixlen / 8;

	if (index < 16) {
		pnt = (uint8_t *)&p->prefix;
		offset = p->prefixlen % 8;

		pnt[index] &= maskbit[offset];
		index++;

		while (index < 16)
			pnt[index++] = 0;
	}
}

/*
 * 通用把前缀主机位清零：按 family 分派到 apply_mask_ipv4 / apply_mask_ipv6，
 * 其余族不做处理。
 */
void apply_mask(union prefixptr pu)
{
	struct prefix *p = pu.p;

	switch (p->family) {
	case AF_INET:
		apply_mask_ipv4(pu.p4);
		break;
	case AF_INET6:
		apply_mask_ipv6(pu.p6);
		break;
	default:
		break;
	}
	return;
}

/* Utility function of convert between struct prefix <=> union sockunion. */
/*
 * 把套接字地址 su 转成对应族的“主机前缀”（长度 32 或 128）。
 * 若传入 prefix 非空则复用该缓冲区，否则新建一个；返回该前缀指针。
 * 遇到既非 IPv4 也非 IPv6 的地址族返回 NULL。
 */
struct prefix *sockunion2hostprefix(const union sockunion *su,
				    struct prefix *prefix)
{
	if (su->sa.sa_family == AF_INET) {
		struct prefix_ipv4 *p;

		p = prefix ? (struct prefix_ipv4 *)prefix : prefix_ipv4_new();
		p->family = AF_INET;
		p->prefix = su->sin.sin_addr;
		p->prefixlen = IPV4_MAX_BITLEN;
		return (struct prefix *)p;
	}
	if (su->sa.sa_family == AF_INET6) {
		struct prefix_ipv6 *p;

		p = prefix ? (struct prefix_ipv6 *)prefix : prefix_ipv6_new();
		p->family = AF_INET6;
		p->prefixlen = IPV6_MAX_BITLEN;
		memcpy(&p->prefix, &su->sin6.sin6_addr,
		       sizeof(struct in6_addr));
		return (struct prefix *)p;
	}
	return NULL;
}

/*
 * 把前缀 p 写进套接字地址 su：先清零，再按 family 填 sa_family 与地址字段。
 */
void prefix2sockunion(const struct prefix *p, union sockunion *su)
{
	memset(su, 0, sizeof(*su));

	su->sa.sa_family = p->family;
	if (p->family == AF_INET)
		su->sin.sin_addr = p->u.prefix4;
	if (p->family == AF_INET6)
		memcpy(&su->sin6.sin6_addr, &p->u.prefix6,
		       sizeof(struct in6_addr));
}

/*
 * 返回前缀地址族的字节长度：IPv4 为 4、IPv6 为 16、以太网为 6；未知返回 0。
 */
int prefix_blen(union prefixconstptr pu)
{
	const struct prefix *p = pu.p;

	switch (p->family) {
	case AF_INET:
		return IPV4_MAX_BYTELEN;
	case AF_INET6:
		return IPV6_MAX_BYTELEN;
	case AF_ETHERNET:
		return ETH_ALEN;
	}
	return 0;
}

/* Generic function for conversion string to struct prefix. */
/*
 * 通用字符串转前缀：依次尝试按 IPv4、IPv6、以太网解析，哪个成功用哪个，
 * 全部失败返回 0。注意会依次覆写同一个 *p。
 */
int str2prefix(const char *str, struct prefix *p)
{
	int ret;

	if (!str || !p)
		return 0;

	/* First we try to convert string to struct prefix_ipv4. */
	ret = str2prefix_ipv4(str, (struct prefix_ipv4 *)p);
	if (ret)
		return ret;

	/* Next we try to convert string to struct prefix_ipv6. */
	ret = str2prefix_ipv6(str, (struct prefix_ipv6 *)p);
	if (ret)
		return ret;

	/* Next we try to convert string to struct prefix_eth. */
	ret = str2prefix_eth(str, (struct prefix_eth *)p);
	if (ret)
		return ret;

	return 0;
}

/*
 * 把 EVPN A-D 路由前缀格式化为字符串：
 * 形如 [type]:[eth_tag]:[esi]:[bitlen]:[ip]:[frag_id]。返回 str。
 */
static const char *prefixevpn_ead2str(const struct prefix_evpn *p, char *str,
				      int size)
{
	uint8_t family;
	char buf[ESI_STR_LEN];
	char buf1[INET6_ADDRSTRLEN];

	family = IS_IPADDR_V4(&p->prefix.ead_addr.ip) ? AF_INET : AF_INET6;

	snprintf(str, size, "[%d]:[%u]:[%s]:[%d]:[%s]:[%u]",
		 p->prefix.route_type, p->prefix.ead_addr.eth_tag,
		 esi_to_str(&p->prefix.ead_addr.esi, buf, sizeof(buf)),
		 (family == AF_INET) ? IPV4_MAX_BITLEN : IPV6_MAX_BITLEN,
		 inet_ntop(family, &p->prefix.ead_addr.ip.ip.addr, buf1,
			   sizeof(buf1)),
		 p->prefix.ead_addr.frag_id);
	return str;
}

/*
 * 把 EVPN MAC/IP 路由前缀格式化为字符串；
 * 无 IP 时只输出 [type]:[eth_tag]:[bitlen]:[mac]，有 IP 时再追加掩码长度与地址。
 */
static const char *prefixevpn_macip2str(const struct prefix_evpn *p, char *str,
					int size)
{
	uint8_t family;
	char buf1[ETHER_ADDR_STRLEN];
	char buf2[PREFIX2STR_BUFFER];

	if (is_evpn_prefix_ipaddr_none(p))
		snprintf(str, size, "[%d]:[%d]:[%d]:[%s]", p->prefix.route_type,
			 p->prefix.macip_addr.eth_tag, 8 * ETH_ALEN,
			 prefix_mac2str(&p->prefix.macip_addr.mac, buf1,
					sizeof(buf1)));
	else {
		family = is_evpn_prefix_ipaddr_v4(p) ? AF_INET : AF_INET6;
		snprintf(str, size, "[%d]:[%d]:[%d]:[%s]:[%d]:[%s]",
			 p->prefix.route_type, p->prefix.macip_addr.eth_tag,
			 8 * ETH_ALEN,
			 prefix_mac2str(&p->prefix.macip_addr.mac, buf1,
					sizeof(buf1)),
			 family == AF_INET ? IPV4_MAX_BITLEN : IPV6_MAX_BITLEN,
			 inet_ntop(family, &p->prefix.macip_addr.ip.ip.addr,
				   buf2, PREFIX2STR_BUFFER));
	}
	return str;
}

/*
 * 把 EVPN IMET 路由前缀格式化为字符串。
 */
static const char *prefixevpn_imet2str(const struct prefix_evpn *p, char *str,
				       int size)
{
	uint8_t family;
	char buf[INET6_ADDRSTRLEN];

	family = IS_IPADDR_V4(&p->prefix.imet_addr.ip) ? AF_INET : AF_INET6;
	snprintf(str, size, "[%d]:[%d]:[%d]:[%s]", p->prefix.route_type,
		 p->prefix.imet_addr.eth_tag,
		 (family == AF_INET) ? IPV4_MAX_BITLEN : IPV6_MAX_BITLEN,
		 inet_ntop(family, &p->prefix.imet_addr.ip.ipaddr_v4, buf,
			   sizeof(buf)));

	return str;
}

/*
 * 把 EVPN ES 路由前缀格式化为字符串。
 */
static const char *prefixevpn_es2str(const struct prefix_evpn *p, char *str,
				     int size)
{
	uint8_t family;
	char buf[ESI_STR_LEN];
	char buf1[INET6_ADDRSTRLEN];

	family = IS_IPADDR_V4(&p->prefix.es_addr.ip) ? AF_INET : AF_INET6;
	snprintf(str, size, "[%d]:[%s]:[%d]:[%s]", p->prefix.route_type,
		 esi_to_str(&p->prefix.es_addr.esi, buf, sizeof(buf)),
		 (family == AF_INET) ? IPV4_MAX_BITLEN : IPV6_MAX_BITLEN,
		 inet_ntop(family, &p->prefix.es_addr.ip.ipaddr_v4, buf1,
			   sizeof(buf1)));

	return str;
}

/*
 * 把 EVPN IP-Prefix（type-5）路由前缀格式化为字符串。
 */
static const char *prefixevpn_prefix2str(const struct prefix_evpn *p, char *str,
					 int size)
{
	uint8_t family;
	char buf[INET6_ADDRSTRLEN];

	family = IS_IPADDR_V4(&p->prefix.prefix_addr.ip) ? AF_INET : AF_INET6;
	snprintf(str, size, "[%d]:[%d]:[%d]:[%s]", p->prefix.route_type,
		 p->prefix.prefix_addr.eth_tag,
		 p->prefix.prefix_addr.ip_prefix_length,
		 inet_ntop(family, &p->prefix.prefix_addr.ip.ipaddr_v4, buf,
			   sizeof(buf)));
	return str;
}

/*
 * EVPN 前缀格式化总入口：按 route_type 分派到对应格式函数，未知类型输出提示串。
 */
static const char *prefixevpn2str(const struct prefix_evpn *p, char *str,
				  int size)
{
	switch (p->prefix.route_type) {
	case BGP_EVPN_AD_ROUTE:
		return prefixevpn_ead2str(p, str, size);
	case BGP_EVPN_MAC_IP_ROUTE:
		return prefixevpn_macip2str(p, str, size);
	case BGP_EVPN_IMET_ROUTE:
		return prefixevpn_imet2str(p, str, size);
	case BGP_EVPN_ES_ROUTE:
		return prefixevpn_es2str(p, str, size);
	case BGP_EVPN_IP_PREFIX_ROUTE:
		return prefixevpn_prefix2str(p, str, size);
	default:
		snprintf(str, size, "Unsupported EVPN prefix");
		break;
	}
	return str;
}

/* Helper function to format the prefix length in the format /xx */
/*
 * 把前缀长度以 "/xx" 形式追加写入 buf（从偏移 l 处），返回新的写入位置。
 * 为容纳最长 "/128"（含结尾）预留足够空间，空间不足时原样返回 l 不做写入。
 * 手写十进制以避免依赖 printf，属性能敏感的小工具。
 */
static size_t format_prefixlen(char *buf, size_t l, int prefixlen, size_t buf_size)
{
	int byte, tmp, a, b;
	bool z = false;

	/*
	 * Ensure buffer has enough space for the maximum prefix length case.
	 * For IPv6 with prefix length 128, we need:
	 * - 1 byte for '/'
	 * - 1 byte for '1' (hundreds place)
	 * - 1 byte for '2' (tens place)
	 * - 1 byte for '8' (ones place)
	 * - 1 byte for the null terminator
	 * Total: 5 bytes from the current position
	 */
	if (l + 4 >= buf_size)
		return l;

	buf[l++] = '/';
	byte = prefixlen;
	tmp = prefixlen - 100;
	if (tmp >= 0) {
		buf[l++] = '1';
		z = true;
		byte = tmp;
	}
	b = byte % 10;
	a = byte / 10;
	if (a || z)
		buf[l++] = '0' + a;
	buf[l++] = '0' + b;

	buf[l] = '\0';
	return l;
}

/*
 * 把任意前缀格式化到字符串 str（容量 size），返回 str。
 * 按 family 分派：IPv4 / IPv6 用 inet_ntop 后追加 /len；IPv4-mapped 的 IPv6
 * 特判为 "::ffff:a.b.c.d/len"；以太网输出 "mac/len"；EVPN 转调 prefixevpn2str；
 * flowspec / 未知族输出占位字符串。
 */
const char *prefix2str(union prefixconstptr pu, char *str, int size)
{
	const struct prefix *p = pu.p;
	char buf[PREFIX2STR_BUFFER];

	switch (p->family) {
	case AF_INET:
		inet_ntop(p->family, &p->u.prefix, buf, sizeof(buf));
		format_prefixlen(buf, strlen(buf), p->prefixlen, sizeof(buf));
		strlcpy(str, buf, size);
		break;

	case AF_INET6:
		/* Check if it's an IPv4-mapped IPv6 address */
		if (IN6_IS_ADDR_V4MAPPED(&p->u.prefix6)) {
			struct in_addr ipv4;
			char ipv4str[INET_ADDRSTRLEN];

			ipv4_mapped_ipv6_to_ipv4(&p->u.prefix6, &ipv4);

			/* Format as ::ffff:a.b.c.d/plen format */
			inet_ntop(AF_INET, &ipv4, ipv4str, sizeof(ipv4str));
			/*
			 * 1. Copy prefix (7 chars for "::ffff:")
			 * 2. Append IPv4 address safely with strlcat
			 */
			snprintf(buf, sizeof(buf), "::ffff:");
			strlcat(buf, ipv4str, sizeof(buf));
			format_prefixlen(buf, strlen(buf), p->prefixlen, sizeof(buf));
			strlcpy(str, buf, size);
		} else {
			/* Regular IPv6 address */
			inet_ntop(p->family, &p->u.prefix, buf, sizeof(buf));
			format_prefixlen(buf, strlen(buf), p->prefixlen, sizeof(buf));
			strlcpy(str, buf, size);
		}
		break;

	case AF_ETHERNET:
		snprintf(str, size, "%s/%d",
			 prefix_mac2str(&p->u.prefix_eth, buf, sizeof(buf)),
			 p->prefixlen);
		break;

	case AF_EVPN:
		prefixevpn2str((const struct prefix_evpn *)p, str, size);
		break;

	case AF_FLOWSPEC:
		strlcpy(str, "FS prefix", size);
		break;

	default:
		strlcpy(str, "UNK prefix", size);
		break;
	}

	return str;
}

/*
 * 把组播地址 addr 格式化进 buf；若地址为全零则写 "*"（表示“任意”）。
 * onfail 为解析失败时使用的替代文本。
 */
void prefix_mcast_ip_dump(const char *onfail, const struct ipaddr *addr,
			  char *buf, int buf_size)
{
	if (ipaddr_is_zero(addr))
		strlcpy(buf, "*", buf_size);
	else
		(void)snprintfrr(buf, buf_size, "%pIA", addr);
}

/*
 * 把前缀当作“主机地址”格式化（不带 /len）写入 fbuf，返回写入长度。
 * IPv4 / IPv6 走 inet_ntop，以太网走 MAC 字符串，其余输出带 family 的占位串。
 */
static ssize_t prefixhost2str(struct fbuf *fbuf, union prefixconstptr pu)
{
	const struct prefix *p = pu.p;
	char buf[PREFIX2STR_BUFFER];

	switch (p->family) {
	case AF_INET:
	case AF_INET6:
		inet_ntop(p->family, &p->u.prefix, buf, sizeof(buf));
		return bputs(fbuf, buf);

	case AF_ETHERNET:
		prefix_mac2str(&p->u.prefix_eth, buf, sizeof(buf));
		return bputs(fbuf, buf);

	default:
		return bprintfrr(fbuf, "{prefix.af=%dPF}", p->family);
	}
}

/*
 * 把 IPv4 组播地址 addr 格式化进 buf；地址为 0.0.0.0 时写 "*"。
 * inet_ntop 失败且提供了 onfail 时写入 onfail 文本。会保存并恢复 errno。
 */
void prefix_mcast_inet4_dump(const char *onfail, struct in_addr addr,
		char *buf, int buf_size)
{
	int save_errno = errno;

	if (addr.s_addr == INADDR_ANY)
		strlcpy(buf, "*", buf_size);
	else {
		if (!inet_ntop(AF_INET, &addr, buf, buf_size)) {
			if (onfail)
				snprintf(buf, buf_size, "%s", onfail);
		}
	}

	errno = save_errno;
}

/*
 * 把 (S,G) 结构格式化为 "(src,grp)" 字符串（源 / 组可能显示为 *），返回 sg_str。
 */
const char *prefix_sg2str(const struct prefix_sg *sg, char *sg_str)
{
	char src_str[INET6_ADDRSTRLEN];
	char grp_str[INET6_ADDRSTRLEN];

	prefix_mcast_ip_dump("<src?>", &sg->src, src_str, sizeof(src_str));
	prefix_mcast_ip_dump("<grp?>", &sg->grp, grp_str, sizeof(grp_str));
	snprintf(sg_str, PREFIX_SG_STR_LEN, "(%s,%s)", src_str, grp_str);

	return sg_str;
}

/*
 * 释放 flowspec 前缀里动态分配的负载（u.prefix_flowspec.ptr）并置空。
 * 非 flowspec 或指针为空时直接返回。
 */
void prefix_flowspec_ptr_free(struct prefix *p)
{
	void *temp;

	if (!p || p->family != AF_FLOWSPEC || !p->u.prefix_flowspec.ptr)
		return;

	temp = (void *)p->u.prefix_flowspec.ptr;
	XFREE(MTYPE_PREFIX_FLOWSPEC, temp);
	p->u.prefix_flowspec.ptr = (uintptr_t)NULL;
}

/*
 * 分配并清零一个 struct prefix（MTYPE_PREFIX）。是各 prefix_*_new 的基础。
 */
struct prefix *prefix_new(void)
{
	struct prefix *p;

	p = XCALLOC(MTYPE_PREFIX, sizeof(*p));
	return p;
}

/*
 * 作为列表释放回调：参数是元素本身（非指针的指针），取其地址后调用 prefix_free。
 * 用于需要“回调只提供元素值”的容器清空场景。
 */
void prefix_free_lists(void *arg)
{
	struct prefix *p = arg;

	prefix_free(&p);
}

/* Free prefix structure. */
/*
 * 释放 *p（MTYPE_PREFIX）。XFREE 宏会在释放后把 *p 置 NULL，可避免悬垂与重复释放。
 */
void prefix_free(struct prefix **p)
{
	XFREE(MTYPE_PREFIX, *p);
}

/* Utility function to convert ipv4 prefixes to Classful prefixes */
/*
 * 把 IPv4 前缀转成有类（classful）前缀：主机路由（/32）不变，
 * 否则按地址所属的 A / B / C 类把掩码长度设为 8 / 16 / 24 并应用掩码。
 * 用于兼容旧式按类划分网络的配置。
 */
void apply_classful_mask_ipv4(struct prefix_ipv4 *p)
{

	uint32_t destination;

	destination = ntohl(p->prefix.s_addr);

	if (p->prefixlen == IPV4_MAX_BITLEN)
		;
	/* do nothing for host routes */
	else if (IN_CLASSC(destination)) {
		p->prefixlen = 24;
		apply_mask_ipv4(p);
	} else if (IN_CLASSB(destination)) {
		p->prefixlen = 16;
		apply_mask_ipv4(p);
	} else {
		p->prefixlen = 8;
		apply_mask_ipv4(p);
	}
}

/*
 * 由主机地址 hostaddr 与掩码长度 masklen 算出该网络的广播地址。
 * 常规为“地址或上掩码取反”；/31 按 RFC3021 3.3 特殊返回 255.255.255.255。
 * 地址与返回值均为网络字节序。
 */
in_addr_t ipv4_broadcast_addr(in_addr_t hostaddr, int masklen)
{
	struct in_addr mask;

	masklen2ip(masklen, &mask);
	return (masklen != IPV4_MAX_BITLEN - 1)
		       ?
		       /* normal case */
		       (hostaddr | ~mask.s_addr)
		       :
		       /* For prefix 31 return 255.255.255.255 (RFC3021) */
		       htonl(0xFFFFFFFF);
}

/* Utility function to convert ipv4 netmask to prefixes
   ex.) "1.1.0.0" "255.255.0.0" => "1.1.0.0/16"
   ex.) "1.0.0.0" NULL => "1.0.0.0/8"                   */
/*
 * 把“网络地址 + 可选掩码字符串”转成 "prefix/len" 字符串写入 prefix_str。
 * 提供 mask_str 时用 ip_masklen 求长度；未提供掩码时按有类规则推断
 * （0.0.0.0 为 0，C / B / A 类为 24 / 16 / 8）。成功返回 1，失败返回 0。
 */
int netmask_str2prefix_str(const char *net_str, const char *mask_str,
			   char *prefix_str, size_t prefix_str_len)
{
	struct in_addr network;
	struct in_addr mask;
	uint8_t prefixlen;
	uint32_t destination;
	int ret;

	ret = inet_aton(net_str, &network);
	if (!ret)
		return 0;

	if (mask_str) {
		ret = inet_aton(mask_str, &mask);
		if (!ret)
			return 0;

		prefixlen = ip_masklen(mask);
	} else {
		destination = ntohl(network.s_addr);

		if (network.s_addr == INADDR_ANY)
			prefixlen = 0;
		else if (IN_CLASSC(destination))
			prefixlen = 24;
		else if (IN_CLASSB(destination))
			prefixlen = 16;
		else if (IN_CLASSA(destination))
			prefixlen = 8;
		else
			return 0;
	}

	snprintf(prefix_str, prefix_str_len, "%s/%d", net_str, prefixlen);

	return 1;
}

/* converts to internal representation of mac address
 * returns 1 on success, 0 otherwise
 * format accepted: AA:BB:CC:DD:EE:FF
 * if mac parameter is null, then check only
 */
/*
 * 解析 "AA:BB:CC:DD:EE:FF" 形式的 MAC 字符串到 struct ethaddr，成功返回 1。
 * mac 为 NULL 时只做格式校验不写入。属内部分解表示。
 */
int prefix_str2mac(const char *str, struct ethaddr *mac)
{
	unsigned int a[6];
	int i;

	if (!str)
		return 0;

	if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x", a + 0, a + 1, a + 2, a + 3,
		   a + 4, a + 5)
	    != 6) {
		/* error in incoming str length */
		return 0;
	}
	/* valid mac address */
	if (!mac)
		return 1;
	for (i = 0; i < 6; ++i)
		mac->octet[i] = a[i] & 0xff;
	return 1;
}

/*
 * 把 struct ethaddr 格式化为 "aa:bb:cc:dd:ee:ff" 字符串。
 * 若 buf 为空则内部按 ETHER_ADDR_STRLEN 分配（调用方负责释放），否则要求
 * size 足够大；mac 为 NULL 返回 NULL。
 */
char *prefix_mac2str(const struct ethaddr *mac, char *buf, int size)
{
	char *ptr;

	if (!mac)
		return NULL;
	if (!buf)
		ptr = XMALLOC(MTYPE_TMP, ETHER_ADDR_STRLEN * sizeof(char));
	else {
		assert(size >= ETHER_ADDR_STRLEN);
		ptr = buf;
	}
	snprintf(ptr, (ETHER_ADDR_STRLEN), "%02x:%02x:%02x:%02x:%02x:%02x",
		 (uint8_t)mac->octet[0], (uint8_t)mac->octet[1],
		 (uint8_t)mac->octet[2], (uint8_t)mac->octet[3],
		 (uint8_t)mac->octet[4], (uint8_t)mac->octet[5]);
	return ptr;
}

/*
 * 计算前缀的哈希键：先把副本清零再拷贝一份，确保对齐 / 填充与未用字节都为零
 * （否则相同前缀会因垃圾字节得到不同哈希）。flowspec 前缀额外对负载指针指向的
 * 内容做 jhash，并释放临时拷贝。返回哈希值。
 */
unsigned prefix_hash_key(const void *pp)
{
	struct prefix copy;

	if (((struct prefix *)pp)->family == AF_FLOWSPEC) {
		uint32_t len;

		/* make sure *all* unused bits are zero,
		 * particularly including alignment /
		 * padding and unused prefix bytes.
		 */
		memset(&copy, 0, sizeof(copy));
		prefix_copy(&copy, (struct prefix *)pp);
		len = jhash((void *)copy.u.prefix_flowspec.ptr,
			    copy.u.prefix_flowspec.prefixlen,
			    0x55aa5a5a);
		prefix_flowspec_ptr_free(&copy);
		return len;
	}
	/* make sure *all* unused bits are zero, particularly including
	 * alignment /
	 * padding and unused prefix bytes. */
	memset(&copy, 0, sizeof(copy));
	prefix_copy(&copy, (struct prefix *)pp);
	return jhash(&copy,
		     offsetof(struct prefix, u.prefix) + PSIZE(copy.prefixlen),
		     0x55aa5a5a);
}

/* converts to internal representation of esi
 * returns 1 on success, 0 otherwise
 * format accepted: aa:aa:aa:aa:aa:aa:aa:aa:aa:aa
 * if esi parameter is null, then check only
 */
/*
 * 解析 "aa:aa:...:aa"（10 组两位十六进制）形式的 ESI 字符串到 esi_t，成功返回 1。
 * esi 为 NULL 时只做格式校验。属内部分解表示。
 */
int str_to_esi(const char *str, esi_t *esi)
{
	int i;
	unsigned int a[ESI_BYTES];

	if (!str)
		return 0;

	if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x",
		   a + 0, a + 1, a + 2, a + 3,
		   a + 4, a + 5, a + 6, a + 7,
		   a + 8, a + 9)
	    != ESI_BYTES) {
		/* error in incoming str length */
		return 0;
	}

	/* valid ESI */
	if (!esi)
		return 1;
	for (i = 0; i < ESI_BYTES; ++i)
		esi->val[i] = a[i] & 0xff;
	return 1;
}

/*
 * 把 esi_t 格式化成冒号分隔的十六进制字符串。
 * buf 为空时内部按 ESI_STR_LEN 分配（调用方负责释放），否则要求 size 足够大。
 */
char *esi_to_str(const esi_t *esi, char *buf, int size)
{
	char *ptr;

	if (!esi)
		return NULL;
	if (!buf)
		ptr = XMALLOC(MTYPE_TMP, ESI_STR_LEN * sizeof(char));
	else {
		assert(size >= ESI_STR_LEN);
		ptr = buf;
	}

	snprintf(ptr, ESI_STR_LEN,
		 "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		 esi->val[0], esi->val[1], esi->val[2],
		 esi->val[3], esi->val[4], esi->val[5],
		 esi->val[6], esi->val[7], esi->val[8],
		 esi->val[9]);
	return ptr;
}

/*
 * 把 EVPN ES 的 DF 选举算法编号转成名字字符串（service-carving / HRW /
 * preference，其余为 "unknown N"），结果写入 buf 并返回 buf。
 */
char *evpn_es_df_alg2str(uint8_t df_alg, char *buf, int buf_len)
{
	switch (df_alg) {
	case EVPN_MH_DF_ALG_SERVICE_CARVING:
		snprintf(buf, buf_len, "service-carving");
		break;

	case EVPN_MH_DF_ALG_HRW:
		snprintf(buf, buf_len, "HRW");
		break;

	case EVPN_MH_DF_ALG_PREF:
		snprintf(buf, buf_len, "preference");
		break;

	default:
		snprintf(buf, buf_len, "unknown %u", df_alg);
		break;
	}

	return buf;
}

/*
 * 按 IETF 草稿（unicast-0 / 127 / 240）口径判断 IPv4 是否可作为公网单播。
 * 排除 0.0.0.0；240/4 视为可用；127/8 中仅 127.0.0.0/16 与环回不可用，
 * 其余 127.x 可用；组播（224/4）不可用。
 */
bool ipv4_ietf_unicast_valid(const struct in_addr *addr)
{
	/* New reserved ranges:
	 * draft-schoen-intarea-unicast-0
	 * draft-schoen-intarea-unicast-127
	 * draft-schoen-intarea-unicast-240
	 */
	in_addr_t ip = ntohl(addr->s_addr);

	/* draft-schoen-intarea-unicast-0 */
	if (ip == INADDR_ANY)
		return false;

	/* draft-schoen-intarea-unicast-240 */
	if (IPV4_CLASS_E(ip))
		return true;

	/* draft-schoen-intarea-unicast-127 */
	if (IPV4_NET127(ip)) {
		if (!IPV4_NET127_16(ip) && ip != INADDR_LOOPBACK)
			return true;
		else
			return false;
	}

	if (IPV4_CLASS_D(ip))
		return false;

	return true;
}

/*
 * 判断 IPv4 是否为合法单播：240/4 可用，224/4 组播不可用，
 * 0/8 与 127/8 是否可用取决于配置项 cmd_allow_reserved_ranges。
 */
bool ipv4_unicast_valid(const struct in_addr *addr)
{
	in_addr_t ip = ntohl(addr->s_addr);

	if (IPV4_CLASS_E(ip))
		return true;

	if (IPV4_CLASS_D(ip))
		return false;

	if (IPV4_NET0(ip) || IPV4_NET127(ip)) {
		if (cmd_allow_reserved_ranges_get())
			return true;
		else
			return false;
	}

	return true;
}

/*
 * 把 struct ipaddr 转成 struct prefix：按 ipa_type 填 v4 / v6 的地址与掩码长度，
 * IPADDR_NONE 时 family 置 AF_UNSPEC。始终返回 0。
 */
static int ipaddr2prefix(const struct ipaddr *ip, uint16_t prefixlen,
			 struct prefix *p)
{
	switch (ip->ipa_type) {
	case (IPADDR_V4):
		p->family = AF_INET;
		p->u.prefix4 = ip->ipaddr_v4;
		p->prefixlen = prefixlen;
		break;
	case (IPADDR_V6):
		p->family = AF_INET6;
		p->u.prefix6 = ip->ipaddr_v6;
		p->prefixlen = prefixlen;
		break;
	case (IPADDR_NONE):
		p->family = AF_UNSPEC;
		break;
	}

	return 0;
}

/*
 * Convert type-2 and type-5 evpn route prefixes into the more
 * general ipv4/ipv6 prefix types so we can match prefix lists
 * and such.
 */
/*
 * 把 EVPN type-2（MAC/IP）与 type-5（IP-Prefix）路由前缀提取成普通的
 * IPv4 / IPv6 前缀，便于套用 prefix-list 等通用匹配。
 * 非 EVPN、不支持的类型或缺失 IP 均返回 -1；成功返回 0。
 */
int evpn_prefix2prefix(const struct prefix *evpn, struct prefix *to)
{
	const struct evpn_addr *addr;

	if (evpn->family != AF_EVPN)
		return -1;

	addr = &evpn->u.prefix_evpn;

	switch (addr->route_type) {
	case BGP_EVPN_MAC_IP_ROUTE:
		if (IS_IPADDR_V4(&addr->macip_addr.ip))
			ipaddr2prefix(&addr->macip_addr.ip, IPV4_MAX_BITLEN,
				      to);
		else if (IS_IPADDR_V6(&addr->macip_addr.ip))
			ipaddr2prefix(&addr->macip_addr.ip, IPV6_MAX_BITLEN,
				      to);
		else
			return -1; /* mac only? */

		break;
	case BGP_EVPN_IP_PREFIX_ROUTE:
		ipaddr2prefix(&addr->prefix_addr.ip,
			      addr->prefix_addr.ip_prefix_length, to);
		break;
	default:
		return -1;
	}

	return 0;
}

printfrr_ext_autoreg_p("EA", printfrr_ea);
/*
 * printfrr 扩展 %pEA：把 struct ethaddr 渲染为 MAC 字符串；空指针输出 "(null)"。
 */
static ssize_t printfrr_ea(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct ethaddr *mac = ptr;
	char cbuf[ETHER_ADDR_STRLEN];

	if (!mac)
		return bputs(buf, "(null)");

	/* need real length even if buffer is too short */
	prefix_mac2str(mac, cbuf, sizeof(cbuf));
	return bputs(buf, cbuf);
}

printfrr_ext_autoreg_p("IA", printfrr_ia);
/*
 * printfrr 扩展 %pIA：渲染 struct ipaddr。格式串后可跟 's' 表示“零地址显示为 *”。
 */
static ssize_t printfrr_ia(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct ipaddr *ipa = ptr;
	char cbuf[INET6_ADDRSTRLEN];
	bool use_star = false;

	if (ea->fmt[0] == 's') {
		use_star = true;
		ea->fmt++;
	}

	if (!ipa || !ipa->ipa_type)
		return bputs(buf, "(null)");

	if (use_star) {
		struct in_addr zero4 = {};
		struct in6_addr zero6 = {};

		switch (ipa->ipa_type) {
		case IPADDR_V4:
			if (!memcmp(&ipa->ip.addr, &zero4, sizeof(zero4)))
				return bputch(buf, '*');
			break;

		case IPADDR_V6:
			if (!memcmp(&ipa->ip.addr, &zero6, sizeof(zero6)))
				return bputch(buf, '*');
			break;

		case IPADDR_NONE:
			break;
		}
	}

	ipaddr2str(ipa, cbuf, sizeof(cbuf));
	return bputs(buf, cbuf);
}

printfrr_ext_autoreg_p("I4", printfrr_i4);
/*
 * printfrr 扩展 %pI4：渲染 IPv4 地址。格式串后可跟 's' 表示零地址显示为 *。
 */
static ssize_t printfrr_i4(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	char cbuf[INET_ADDRSTRLEN];
	bool use_star = false;
	struct in_addr zero = {};

	if (ea->fmt[0] == 's') {
		use_star = true;
		ea->fmt++;
	}

	if (!ptr)
		return bputs(buf, "(null)");

	if (use_star && !memcmp(ptr, &zero, sizeof(zero)))
		return bputch(buf, '*');

	inet_ntop(AF_INET, ptr, cbuf, sizeof(cbuf));
	return bputs(buf, cbuf);
}

printfrr_ext_autoreg_p("I6", printfrr_i6);
/*
 * printfrr 扩展 %pI6：渲染 IPv6 地址；对 IPv4-mapped 地址特判为 "::ffff:a.b.c.d"。
 * 格式串后可跟 's' 表示零地址显示为 *。
 */
static ssize_t printfrr_i6(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	char cbuf[INET6_ADDRSTRLEN];
	bool use_star = false;
	struct in6_addr zero = {};

	if (ea->fmt[0] == 's') {
		use_star = true;
		ea->fmt++;
	}

	if (!ptr)
		return bputs(buf, "(null)");

	if (use_star && !memcmp(ptr, &zero, sizeof(zero)))
		return bputch(buf, '*');

	/* Handle IPv4-mapped IPv6 addresses specially */
	const struct in6_addr *addr = ptr;

	if (IN6_IS_ADDR_V4MAPPED(addr)) {
		struct in_addr ipv4;
		char ipv4str[INET_ADDRSTRLEN];

		/* Extract the IPv4 address from the mapped IPv6 address */
		ipv4_mapped_ipv6_to_ipv4(addr, &ipv4);

		/* Format as ::ffff:a.b.c.d */
		inet_ntop(AF_INET, &ipv4, ipv4str, sizeof(ipv4str));
		/*
		 * 1. Copy prefix (7 chars for "::ffff:")
		 * 2. Append IPv4 address safely with strlcat
		 */
		snprintf(cbuf, sizeof(cbuf), "::ffff:");
		strlcat(cbuf, ipv4str, sizeof(cbuf));
	} else {
		/* Regular IPv6 address formatting */
		inet_ntop(AF_INET6, ptr, cbuf, sizeof(cbuf));
	}

	return bputs(buf, cbuf);
}

printfrr_ext_autoreg_p("FX", printfrr_pfx);
/*
 * printfrr 扩展 %pFX：渲染前缀。格式串后可跟 'h' 表示只渲染主机地址（不带 /len）。
 */
static ssize_t printfrr_pfx(struct fbuf *buf, struct printfrr_eargs *ea,
			    const void *ptr)
{
	bool host_only = false;

	if (ea->fmt[0] == 'h') {
		ea->fmt++;
		host_only = true;
	}

	if (!ptr)
		return bputs(buf, "(null)");

	if (host_only)
		return prefixhost2str(buf, (struct prefix *)ptr);
	else {
		char cbuf[PREFIX_STRLEN];

		prefix2str(ptr, cbuf, sizeof(cbuf));
		return bputs(buf, cbuf);
	}
}

printfrr_ext_autoreg_p("PSG", printfrr_psg);
/*
 * printfrr 扩展 %pPSG：把 (S,G) 结构渲染为 "(src,grp)"，源 / 组为零时显示为 *。
 */
static ssize_t printfrr_psg(struct fbuf *buf, struct printfrr_eargs *ea,
			    const void *ptr)
{
	const struct prefix_sg *sg = ptr;
	ssize_t ret = 0;

	if (!sg)
		return bputs(buf, "(null)");

	if (ipaddr_is_zero(&sg->src))
		ret += bputs(buf, "(*,");
	else
		ret += bprintfrr(buf, "(%pIA,", &sg->src);

	if (ipaddr_is_zero(&sg->grp))
		ret += bputs(buf, "*)");
	else
		ret += bprintfrr(buf, "%pIA)", &sg->grp);

	return ret;
}
