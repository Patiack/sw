// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IP address structure (for generic IPv4 or IPv6 address)
 * Copyright (C) 2016, 2017 Cumulus Networks, Inc.
 */

/*
 * 文件导读（lib/ipaddr.h）
 *
 * 本文件定义通用 IP 地址类型 struct ipaddr 及其配套工具函数。它把 IPv4
 * 与 IPv6 地址统一到一个结构里，使不确定地址族的代码无需分别处理两套
 * 类型。与 prefix.h 的区别：prefix 表示“带前缀长度的网络地址”，ipaddr
 * 表示“单个主机地址”，二者用途不同、可互相转换。
 *
 * 核心数据结构：
 *   - enum ipaddr_type_t：地址族标记，取值直接复用 AF_UNSPEC / AF_INET /
 *     AF_INET6，方便与 socket API 互操作。
 *   - struct ipaddr：ipa_type 加一个匿名联合体 ip，联合体里同时提供
 *     v4 / v6 / 原始字节三种视图（ipaddr_v4、ipaddr_v6 为便捷宏）。
 *
 * 主要函数（多为 static inline）：
 *   - ipaddr_family：取地址族；str2ipaddr：字符串转 ipaddr。
 *   - ipaddr2str：ipaddr 转字符串，特别处理 IPv4-mapped IPv6。
 *   - ipaddr_cmp：通用排序比较；ipaddr_is_same：是否相等。
 *   - ipaddr_is_zero：是否全零；ipaddr_is_mcast：是否组播。
 *   - ipv4_to_ipv4_mapped_ipv6 / ipv4_mapped_ipv6_to_ipv4：v4 与映射 v6 互转。
 *
 * 与其他模块的关系：被 BGP、zebra、路由表等大量模块使用；文件尾部还注册
 * 了 printfrr 的 %pIA 扩展，使日志能直接打印 ipaddr。
 *
 * 阅读顺序：类型与宏 -> 转换函数 -> 比较 / 判定函数 -> printfrr 扩展注册。
 */

#ifndef __IPADDR_H__
#define __IPADDR_H__

#include <zebra.h>

#include "lib/log.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic IP address - union of IPv4 and IPv6 address.
 */
/* 地址族标记。刻意复用 AF_* 的数值，使本类型可直接喂给 socket 接口。 */
enum ipaddr_type_t {
	IPADDR_NONE = AF_UNSPEC,
	IPADDR_V4 = AF_INET,
	IPADDR_V6 = AF_INET6,
};

/* 通用 IP 地址：ipa_type 标识地址族，联合体 ip 提供 v4 / v6 / 原始字节
 * 三种视图。ipaddr_v4 / ipaddr_v6 是访问联合体成员的便捷宏。 */
struct ipaddr {
	enum ipaddr_type_t ipa_type;
	union {
		uint8_t addr;
		uint8_t addrbytes[16];
		struct in_addr _v4_addr;
		struct in6_addr _v6_addr;
	} ip;
#define ipaddr_v4 ip._v4_addr
#define ipaddr_v6 ip._v6_addr
};

/* 地址族判定宏：判断 ipaddr 的地址族。 */
#define IS_IPADDR_NONE(p) ((p)->ipa_type == IPADDR_NONE)
#define IS_IPADDR_V4(p)   ((p)->ipa_type == IPADDR_V4)
#define IS_IPADDR_V6(p)   ((p)->ipa_type == IPADDR_V6)

/* 地址族设置宏：设置 ipaddr 的地址族标记。 */
#define SET_IPADDR_NONE(p) ((p)->ipa_type = IPADDR_NONE)
#define SET_IPADDR_V4(p)   ((p)->ipa_type = IPADDR_V4)
#define SET_IPADDR_V6(p)   ((p)->ipa_type = IPADDR_V6)

/* 地址占用的字节数：IPv4 为 4 字节，其余按 IPv6 的 16 字节。 */
#define IPADDRSZ(p)                                                            \
	(IS_IPADDR_V4((p)) ? sizeof(struct in_addr) : sizeof(struct in6_addr))

/* 地址字符串缓冲建议长度：足以容纳最长的 IPv6 文本，含结尾空字符。 */
#define IPADDR_STRING_SIZE 46

/* 返回 ipaddr 对应的地址族常量（AF_INET / AF_INET6 / AF_UNSPEC）。 */
static inline int ipaddr_family(const struct ipaddr *ip)
{
	switch (ip->ipa_type) {
	case IPADDR_V4:
		return AF_INET;
	case IPADDR_V6:
		return AF_INET6;
	case IPADDR_NONE:
		return AF_UNSPEC;
	}

	assert(!"Reached end of function where we should never hit");
	return AF_UNSPEC;
}

/* 把字符串解析成 ipaddr，先试 IPv4 再试 IPv6；成功返回 0，失败返回 -1。 */
static inline int str2ipaddr(const char *str, struct ipaddr *ip)
{
	int ret;

	memset(ip, 0, sizeof(struct ipaddr));

	ret = inet_pton(AF_INET, str, &ip->ipaddr_v4);
	if (ret > 0) /* Valid IPv4 address. */
	{
		ip->ipa_type = IPADDR_V4;
		return 0;
	}
	ret = inet_pton(AF_INET6, str, &ip->ipaddr_v6);
	if (ret > 0) /* Valid IPv6 address. */
	{
		ip->ipa_type = IPADDR_V6;
		return 0;
	}

	return -1;
}

/* 判断 IPv6 地址是否为 IPv4-mapped 形式（::ffff:a.b.c.d，见 RFC 4291）。 */
#define IS_MAPPED_IPV6(A)                                                      \
	((A)->s6_addr32[0] == 0x00000000                                       \
		 ? ((A)->s6_addr32[1] == 0x00000000                            \
			    ? (ntohl((A)->s6_addr32[2]) == 0xFFFF ? 1 : 0)     \
			    : 0)                                               \
		 : 0)

/*
 * Convert IPv4 address to IPv4-mapped IPv6 address which is of the
 * form ::FFFF:<IPv4 address> (RFC 4291). This IPv6 address can then
 * be used to represent the IPv4 address, wherever only an IPv6 address
 * is required.
 */
/* 把 IPv4 地址转成 IPv4-mapped IPv6 地址（::ffff:<IPv4>）。当接口只接受
 * IPv6 地址时，可用它代表一个 IPv4 地址。 */
static inline void ipv4_to_ipv4_mapped_ipv6(struct in6_addr *in6,
					    struct in_addr in)
{
	uint32_t addr_type = htonl(0xFFFF);

	memset(in6, 0, sizeof(struct in6_addr));
	memcpy((char *)in6 + 8, &addr_type, sizeof(addr_type));
	memcpy((char *)in6 + 12, &in, sizeof(struct in_addr));
}

/*
 * convert an ipv4 mapped ipv6 address back to ipv4 address
 */
/* 反向操作：从 IPv4-mapped IPv6 地址中取回 IPv4 地址。 */
static inline void ipv4_mapped_ipv6_to_ipv4(const struct in6_addr *in6,
					    struct in_addr *in)
{
	memset(in, 0, sizeof(struct in_addr));
	memcpy(in, (char *)in6 + 12, sizeof(struct in_addr));
}

/* 把 ipaddr 格式化成字符串写入 buf。对 IPv4-mapped IPv6 按 RFC 5952 建议
 * 采用“混合记法”（::ffff:a.b.c.d）而非纯十六进制展示。返回 buf。 */
static inline char *ipaddr2str(const struct ipaddr *ip, char *buf, int size)
{
	buf[0] = '\0';
	if (ip) {
		if (IS_IPADDR_V6(ip) && IN6_IS_ADDR_V4MAPPED(&ip->ipaddr_v6)) {
			/* Handle IPv4-mapped IPv6 addresses specially */
			struct in_addr ipv4;
			char ipv4str[INET_ADDRSTRLEN];

			/*
			 * Extract the IPv4 address from the mapped IPv6 address.
			 * Per RFC 5952 section 5, it is RECOMMENDED to represent
			 * IPv4-mapped IPv6 addresses using "mixed notation" with the
			 * IPv4 part in dot-decimal format: ::ffff:192.0.2.1
			 * instead of ::ffff:c000:0201
			 */
			ipv4_mapped_ipv6_to_ipv4(&ip->ipaddr_v6, &ipv4);

			/* Format as IPv4-mapped IPv6 address (::ffff:a.b.c.d) */
			inet_ntop(AF_INET, &ipv4, ipv4str, sizeof(ipv4str));

			/*
			 * 1. Copy prefix (7 chars for "::ffff:")
			 * 2. Append IPv4 address safely with strlcat
			 */
			snprintf(buf, size, "::ffff:");
			strlcat(buf, ipv4str, size);
		} else {
			/* Regular IP address formatting */
			inet_ntop(ipaddr_family(ip), &ip->ip.addr, buf, size);
		}
	}
	return buf;
}

/*
 * generic ordering comparison between IP addresses
 */
/* 通用排序比较：先比地址族，再比地址本身。返回 -1 / 0 / 1，可用于排序。 */
static inline int ipaddr_cmp(const struct ipaddr *a, const struct ipaddr *b)
{
	uint32_t va, vb;
	va = a->ipa_type;
	vb = b->ipa_type;
	if (va != vb)
		return (va < vb) ? -1 : 1;
	switch (a->ipa_type) {
	case IPADDR_V4:
		va = ntohl(a->ipaddr_v4.s_addr);
		vb = ntohl(b->ipaddr_v4.s_addr);
		if (va != vb)
			return (va < vb) ? -1 : 1;
		return 0;
	case IPADDR_V6:
		return memcmp((void *)&a->ipaddr_v6, (void *)&b->ipaddr_v6,
			      sizeof(a->ipaddr_v6));
	case IPADDR_NONE:
		return 0;
	}

	assert(!"Reached end of function we should never hit");
	return -1;
}

/* 判断地址是否为零地址（未指定）：NONE / 0.0.0.0 / :: 均视为零。 */
static inline bool ipaddr_is_zero(const struct ipaddr *ip)
{
	switch (ip->ipa_type) {
	case IPADDR_NONE:
		return true;
	case IPADDR_V4:
		return ip->ipaddr_v4.s_addr == INADDR_ANY;
	case IPADDR_V6:
		return IN6_IS_ADDR_UNSPECIFIED(&ip->ipaddr_v6);
	}
	return true;
}

/* 判断两个地址是否完全相同，等价于 ipaddr_cmp() == 0。 */
static inline bool ipaddr_is_same(const struct ipaddr *ip1,
				  const struct ipaddr *ip2)
{
	return ipaddr_cmp(ip1, ip2) == 0;
}

/* 判断地址是否属于组播 / 多播范围。 */
static inline bool ipaddr_is_mcast(const struct ipaddr *ip)
{
	switch (ip->ipa_type) {
	case IPADDR_NONE:
		return false;
	case IPADDR_V4:
		return IN_MULTICAST(ntohl(ip->ipaddr_v4.s_addr));
	case IPADDR_V6:
		return IN6_IS_ADDR_MULTICAST(&(ip->ipaddr_v6));
	}

	return false;
}

/* 向 printfrr 注册 %pIA 转换：使日志 / 打印函数能直接接收 struct ipaddr 指针。
 * 仅在使用 frr-format 编译器插件时生效。 */
/* clang-format off */
#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pIA"  (struct ipaddr *)
#endif
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* __IPADDR_H__ */
