// SPDX-License-Identifier: GPL-2.0-or-later
/* Zebra common header.
 * Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002 Kunihiro Ishiguro
 */

/*
 * 文件导读（lib/zebra.h）
 *
 * 本文件是 FRR 的“总头文件”，几乎所有源文件的第一件事就是包含它。它的
 * 职责是把各平台差异集中抹平，让上层代码只需面对一套统一的系统接口与
 * 宏定义。
 *
 * 主要功能：
 *   - 优先包含 config.h 与 compiler.h（编译器属性包装），再批量引入常用
 *     系统头（unistd / stdio / stdlib / socket / netinet / net / arpa 等）。
 *   - 补丁式补齐缺失的平台定义：Netlink 路由表常量、RFC 3542 的 CMSG_*
 *     宏、IP_HDRINCL 字节序修正、IN6_ARE_ADDR_EQUAL、htonll / ntohll、
 *     INADDR_LOOPBACK 等。
 *   - 定义 FRR 的核心枚举与类型：afi_t（地址族）、safi_t（子地址族）、
 *     vrf_id_t（VRF 标识）、route_tag_t（路由 tag），以及配套的校验 /
 *     遍历宏（IS_VALID_AFI、FOREACH_AFI_SAFI 等）。
 *   - 提供标志位操作宏：CHECK_FLAG / SET_FLAG / UNSET_FLAG 及原子版本。
 *   - 引入 route_types.h（zebra 路由类型编号）。
 *
 * 与其他模块的关系：作为全局基础设施被所有守护进程与库共享；compiler.h、
 * route_types.h 等被它直接 incorporate。
 *
 * 阅读顺序：config / compiler -> 系统头分组 -> 平台修正块 -> AFI / SAFI
 * 枚举与遍历宏 -> 标志位宏 -> VRF / route tag 类型。
 */

#ifndef _ZEBRA_H
#define _ZEBRA_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* 先引入编译器属性封装，后续平台修正与宏都依赖它。 */
#include "compiler.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>
#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif
#include <endian.h>

/* 杂项头文件分组（可变参数等）。 */
/* misc include group */
#include <stdarg.h>

/* 网络相关头文件分组：socket、BSD 红黑树、IP / TCP、接口、名字解析等。 */
/* network include group */

#include <sys/socket.h>

#include "openbsd-tree.h"

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <net/if.h>

/* 无 Netlink 的平台（如 BSD）缺少 Linux 的路由表常量，这里补上默认值。 */
#ifndef HAVE_NETLINK
#define RT_TABLE_MAIN		0
#define RT_TABLE_LOCAL		RT_TABLE_MAIN
#endif /* HAVE_NETLINK */

#include <netdb.h>
#include <arpa/inet.h>

#ifdef HAVE_NETINET_IN_VAR_H
#include <netinet/in_var.h>
#endif /* HAVE_NETINET_IN_VAR_H */

#ifdef HAVE_NETINET6_IN_H
#include <netinet6/in.h>
#endif /* HAVE_NETINET6_IN_H */

#ifdef HAVE_NETINET6_IP6_H
#include <netinet6/ip6.h>
#endif /* HAVE_NETINET6_IP6_H */

#ifdef HAVE_NETINET6_ND6_H
#include <netinet6/nd6.h>
#endif /* HAVE_NETINET6_ND6_H */

/* 以下为本地（项目内）头文件的引入。 */
/* Local includes: */
#if !defined(__GNUC__)
#define __attribute__(x)
#endif /* !__GNUC__ */

#include <assert.h>

/* static_cast 宏：C++ 下用真正的 static_cast（带 decltype 推导目标类型），
 * C 下退化为普通括号强转。 */
/*
 * Add explicit static cast only when using a C++ compiler.
 */
#ifdef __cplusplus
#define static_cast(l, r) static_cast<decltype(l)>((r))
#else
#define static_cast(l, r) (r)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 补齐 RFC 3542 定义、但本平台可能缺失的 struct cmsghdr 相关宏。 */
/*
 * RFC 3542 defines several macros for using struct cmsghdr.
 * Here, we define those that are not present
 */

/* 仅供本文件使用的内部对齐宏；在非 ILP32 机器上可能不正确，故不对外。 */
/*
 * Internal defines, for use only in this file.
 * These are likely wrong on other than ILP32 machines, so warn.
 */
/* 辅助控制消息数据区的 4 字节对齐宏。 */
#ifndef _CMSG_DATA_ALIGN
#define _CMSG_DATA_ALIGN(n)           (((n) + 3) & ~3)
#endif /* _CMSG_DATA_ALIGN */

/* 辅助控制消息头部区的 4 字节对齐宏。 */
#ifndef _CMSG_HDR_ALIGN
#define _CMSG_HDR_ALIGN(n)            (((n) + 3) & ~3)
#endif /* _CMSG_HDR_ALIGN */

/* RFC 3542 要求的 CMSG_SPACE / CMSG_LEN，缺失时按 4 字节对齐补上并给出警告。 */
/*
 * CMSG_SPACE and CMSG_LEN are required in RFC3542, but were new in that
 * version.
 */
#ifndef CMSG_SPACE
#define CMSG_SPACE(l)                                                          \
	(_CMSG_DATA_ALIGN(sizeof(struct cmsghdr)) + _CMSG_HDR_ALIGN(l))
#warning "assuming 4-byte alignment for CMSG_SPACE"
#endif /* CMSG_SPACE */


#ifndef CMSG_LEN
#define CMSG_LEN(l)         (_CMSG_DATA_ALIGN(sizeof(struct cmsghdr)) + (l))
#warning "assuming 4-byte alignment for CMSG_LEN"
#endif /* CMSG_LEN */

/* IP_HDRINCL 下 struct ip 的字节序差异：Linux 为网络序，部分 BSD 及旧系统
 * 的长度 / 偏移字段例外；满足条件时定义 HAVE_IP_HDRINCL_BSD_ORDER。 */
/*
 * IP_HDRINCL / struct ip byte order
 *
 * Linux: network byte order
 * *BSD: network, except for length and offset. (cf Stevens)
 * SunOS: nominally as per BSD. but bug: network order on LE.
 * OpenBSD: network byte order, apart from older versions which are as per
 *          *BSD
 */
#if defined(__NetBSD__) ||                                                     \
	(defined(__FreeBSD__) && (__FreeBSD_version < 1100030)) ||             \
	(defined(__OpenBSD__) && (OpenBSD < 200311))
#define HAVE_IP_HDRINCL_BSD_ORDER
#endif

/* 不再依赖 autoconf 宏判断字节序，直接要求 endian.h 提供 BYTE_ORDER。 */
/* autoconf macros for this are deprecated, just find endian.h */
#ifndef BYTE_ORDER
#error please locate an endian.h file appropriate to your platform
#endif

/* 兼容旧定义：旧库里只有 IN6_IS_ADDR_EQUAL，这里做别名。 */
/* For old definition. */
#ifndef IN6_ARE_ADDR_EQUAL
#define IN6_ARE_ADDR_EQUAL IN6_IS_ADDR_EQUAL
#endif /* IN6_ARE_ADDR_EQUAL */

/* Zebra 的路由类型编号定义在 route_types.h，此处引入以便全局可用。 */
/* Zebra route's types are defined in route_types.h */
#include "lib/route_types.h"

/* 字符串相等判定宏：返回非 0 表示两串相同。 */
#define strmatch(a,b) (!strcmp((a), (b)))

/* 64 位字节序转换：小端机上用两次 32 位转换拼合，大端机上为恒等。 */
#if BYTE_ORDER == LITTLE_ENDIAN
#define htonll(x) (((uint64_t)htonl((x)&0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) (((uint64_t)ntohl((x)&0xFFFFFFFF) << 32) | ntohl((x) >> 32))
#else
#define htonll(x) (x)
#define ntohll(x) (x)
#endif

/* 回环地址 127.0.0.1 的常量，缺失时补齐。 */
#ifndef INADDR_LOOPBACK
#define	INADDR_LOOPBACK	0x7f000001	/* Internet address 127.0.0.1.  */
#endif

/* FRR 内部使用的地址族（AFI）枚举：取值紧凑连续（区别于 IANA 编号，见
 * iana_afi.h），供数组下标使用。 */
/* Address family numbers from RFC1700. */
typedef enum {
	AFI_UNSPEC = 0,
	AFI_IP = 1,
	AFI_IP6 = 2,
	AFI_L2VPN = 3,
	AFI_MAX = 4
} afi_t;

/* 判定 AFI 是否有效（落在 UNSPEC 与 MAX 之间）。 */
#define IS_VALID_AFI(a) ((a) > AFI_UNSPEC && (a) < AFI_MAX)

/* 后续地址族标识（SAFI）枚举：同样为内部紧凑编号，涵盖单播 / 组播 /
 * MPLS-VPN / 封装 / EVPN / 带标签单播 / FLOWSPEC。 */
/* Subsequent Address Family Identifier. */
typedef enum {
	SAFI_UNSPEC = 0,
	SAFI_UNICAST = 1,
	SAFI_MULTICAST = 2,
	SAFI_MPLS_VPN = 3,
	SAFI_ENCAP = 4,
	SAFI_EVPN = 5,
	SAFI_LABELED_UNICAST = 6,
	SAFI_FLOWSPEC = 7,
	SAFI_MAX = 8
} safi_t;

/* 双重循环遍历所有有效的 (AFI, SAFI) 组合，供批量处理使用。 */
#define FOREACH_AFI_SAFI(afi, safi)                                            \
	for (afi = AFI_IP; afi < AFI_MAX; afi++)                               \
		for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++)

/* 同上，但 SAFI 仅遍历到 EVPN（用于 NSF / 平滑重启相关处理）。 */
#define FOREACH_AFI_SAFI_NSF(afi, safi)                                                           \
	for (afi = AFI_IP; afi < AFI_MAX; afi++)                                                  \
		for (safi = SAFI_UNICAST; safi <= SAFI_EVPN; safi++)

/* 标志位操作宏：查询 / 置位 / 清位 / 复位 / 按条件设置。 */
/* Flag manipulation macros. */
#define CHECK_FLAG(V,F)      ((V) & (F))
#define SET_FLAG(V,F)        (V) |= (F)
#define UNSET_FLAG(V,F)      (V) &= ~(F)
#define RESET_FLAG(V)        (V) = 0
#define COND_FLAG(V, F, C)   ((C) ? (SET_FLAG(V, F)) : (UNSET_FLAG(V, F)))

/* 原子标志位操作宏：基于 C11 原子操作，供多线程场景下无锁读写同一标志。 */
/* Atomic flag manipulation macros. */
#define CHECK_FLAG_ATOMIC(PV, F)                                               \
	((atomic_load_explicit(PV, memory_order_seq_cst)) & (F))
#define SET_FLAG_ATOMIC(PV, F)                                                 \
	((atomic_fetch_or_explicit(PV, (F), memory_order_seq_cst)))
#define UNSET_FLAG_ATOMIC(PV, F)                                               \
	((atomic_fetch_and_explicit(PV, ~(F), memory_order_seq_cst)))
#define RESET_FLAG_ATOMIC(PV)                                                  \
	((atomic_store_explicit(PV, 0, memory_order_seq_cst)))

/* VRF 标识类型（32 位无符号整数）。 */
/* VRF ID type. */
typedef uint32_t vrf_id_t;

/* 路由 tag 类型及其最大值 / 打印格式（用于给路由打整型标记）。 */
typedef uint32_t route_tag_t;
#define ROUTE_TAG_MAX UINT32_MAX
#define ROUTE_TAG_PRI PRIu32

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_H */
