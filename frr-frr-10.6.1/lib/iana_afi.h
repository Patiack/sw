// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iana_afi and safi definitions.
 * Copyright (C) 2018-2019 Cumulus Networks, Inc.
 * Donald Sharp
 */
/*
 * 文件导读（lib/iana_afi.h）
 *
 * 本文件定义“IANA 编号形式”的 AFI / SAFI 枚举，并在其与 FRR 内部使用的
 * afi_t / safi_t 之间提供双向映射函数。
 *
 * 为什么要两套编号：IANA 的 AFI / SAFI 取值稀疏（例如 SAFI EVPN 为 70、
 * MPLS-VPN 为 128、FLOWSPEC 为 133），直接用它们做数组下标会浪费大量空
 * 间。因此 FRR 内部用紧凑连续的 afi_t / safi_t（定义于 zebra.h），仅在
 * 需要收发协议报文（如 BGP NLRI 中的编号）时与 IANA 值互转。
 *
 * 核心内容：
 *   - iana_afi_t：IANA AFI 值（IPv4=1、IPv6=2、L2VPN=25）。
 *   - iana_safi_t：IANA SAFI 值（单播 / 组播 / 带标签 / 封装 / EVPN /
 *     MPLS-VPN / FLOWSPEC 等）。
 *   - afi_iana2int / afi_int2iana：内部 AFI 与 IANA AFI 互转。
 *   - safi_iana2int / safi_int2iana：内部 SAFI 与 IANA SAFI 互转。
 *   - iana_afi2str / iana_safi2str：转成可读字符串。
 *
 * 阅读顺序：先看两个枚举，再逐个看映射函数；所有函数都是 static inline。
 */

#ifndef __IANA_AFI_H__

#include <prefix.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The above AFI and SAFI definitions are for internal use. The protocol
 * definitions (IANA values) as for example used in BGP protocol packets
 * are defined below and these will get mapped to/from the internal values
 * in the appropriate places.
 * The rationale is that the protocol (IANA) values may be sparse and are
 * not optimal for use in data-structure sizing.
 * Note: Only useful (i.e., supported) values are defined below.
 */
/* IANA 形式的 AFI 枚举：取值即协议报文中的地址族编号，仅列出 FRR 支持
 * 的项；RESERVED 用于占位与非法值。 */
typedef enum {
	IANA_AFI_RESERVED = 0,
	IANA_AFI_IPV4 = 1,
	IANA_AFI_IPV6 = 2,
	IANA_AFI_L2VPN = 25,
} iana_afi_t;

/* IANA 形式的 SAFI 枚举：注意取值不连续（EVPN=70、MPLS-VPN=128、
 * FLOWSPEC=133），这是直接用 IANA 编号做下标不经济的根本原因。 */
typedef enum {
	IANA_SAFI_RESERVED = 0,
	IANA_SAFI_UNICAST = 1,
	IANA_SAFI_MULTICAST = 2,
	IANA_SAFI_LABELED_UNICAST = 4,
	IANA_SAFI_ENCAP = 7,
	IANA_SAFI_EVPN = 70,
	IANA_SAFI_MPLS_VPN = 128,
	IANA_SAFI_FLOWSPEC = 133
} iana_safi_t;

/* 把 IANA AFI 值转成内部 afi_t；未支持 / 保留值返回 AFI_MAX 表示非法。 */
static inline afi_t afi_iana2int(iana_afi_t afi)
{
	switch (afi) {
	case IANA_AFI_IPV4:
		return AFI_IP;
	case IANA_AFI_IPV6:
		return AFI_IP6;
	case IANA_AFI_L2VPN:
		return AFI_L2VPN;
	case IANA_AFI_RESERVED:
		return AFI_MAX;
	}

	return AFI_MAX;
}

/* 把内部 afi_t 转回 IANA AFI 值；未指定 / 越界值返回 IANA_AFI_RESERVED。 */
static inline iana_afi_t afi_int2iana(afi_t afi)
{
	switch (afi) {
	case AFI_IP:
		return IANA_AFI_IPV4;
	case AFI_IP6:
		return IANA_AFI_IPV6;
	case AFI_L2VPN:
		return IANA_AFI_L2VPN;
	case AFI_UNSPEC:
	case AFI_MAX:
		return IANA_AFI_RESERVED;
	}

	return IANA_AFI_RESERVED;
}

/* IANA AFI 值的可读字符串：先转内部 afi_t 再调用 afi2str()。 */
static inline const char *iana_afi2str(iana_afi_t afi)
{
	return afi2str(afi_iana2int(afi));
}

/* 把 IANA SAFI 值转成内部 safi_t；未支持 / 保留值返回 SAFI_MAX 表示非法。 */
static inline safi_t safi_iana2int(iana_safi_t safi)
{
	switch (safi) {
	case IANA_SAFI_UNICAST:
		return SAFI_UNICAST;
	case IANA_SAFI_MULTICAST:
		return SAFI_MULTICAST;
	case IANA_SAFI_MPLS_VPN:
		return SAFI_MPLS_VPN;
	case IANA_SAFI_ENCAP:
		return SAFI_ENCAP;
	case IANA_SAFI_EVPN:
		return SAFI_EVPN;
	case IANA_SAFI_LABELED_UNICAST:
		return SAFI_LABELED_UNICAST;
	case IANA_SAFI_FLOWSPEC:
		return SAFI_FLOWSPEC;
	case IANA_SAFI_RESERVED:
		return SAFI_MAX;
	}

	return SAFI_MAX;
}

/* 把内部 safi_t 转回 IANA SAFI 值；未指定 / 越界值返回 IANA_SAFI_RESERVED。 */
static inline iana_safi_t safi_int2iana(safi_t safi)
{
	switch (safi) {
	case SAFI_UNICAST:
		return IANA_SAFI_UNICAST;
	case SAFI_MULTICAST:
		return IANA_SAFI_MULTICAST;
	case SAFI_MPLS_VPN:
		return IANA_SAFI_MPLS_VPN;
	case SAFI_ENCAP:
		return IANA_SAFI_ENCAP;
	case SAFI_EVPN:
		return IANA_SAFI_EVPN;
	case SAFI_LABELED_UNICAST:
		return IANA_SAFI_LABELED_UNICAST;
	case SAFI_FLOWSPEC:
		return IANA_SAFI_FLOWSPEC;
	case SAFI_UNSPEC:
	case SAFI_MAX:
		return IANA_SAFI_RESERVED;
	}

	return IANA_SAFI_RESERVED;
}

/* IANA SAFI 值的可读字符串：先转内部 safi_t 再调用 safi2str()。 */
static inline const char *iana_safi2str(iana_safi_t safi)
{
	return safi2str(safi_iana2int(safi));
}

#ifdef __cplusplus
}
#endif

#endif
