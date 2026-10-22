// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * 文件导读: checksum.h
 * ----------------------------------------------------------------------------
 * 本文件声明 FRR 的校验和(checksum)计算接口.
 * 作用: 计算与校验 IPv4 / IPv6 首部以及 TCP / UDP 报文的反码求和校验和, 并
 *       提供 ISO 8473 所用的 Fletcher 校验和. 这些是构造与校验 IP 报文时的
 *       底层工具.
 *
 * 核心数据结构:
 *   - struct ipv4_ph: IPv4 伪首部(src, dst, rsvd, proto, len), 参与 TCP / UDP
 *     校验和计算.
 *   - struct ipv6_ph: IPv6 伪首部(src, dst, ulpl 上层载荷长度, zero, next_hdr).
 *   - 二者都用 packed 属性, 保证内存布局与线上格式严格一致.
 *
 * 与其他模块关系: 依赖标准整数类型与 iovec 结构; 被 zebra 及各协议模块用于
 *   封装与校验报文. 具体实现位于 checksum.c.
 *
 * 阅读顺序: 先读两个伪首部结构, 再看 in_cksumv 及 in_cksum 家族的包装,
 *   最后看 fletcher_checksum.
 *
 * 易踩的坑: 伪首部必须与被校验的数据一起喂入; 计算校验和时校验和字段通常要先
 *   置 0; in_cksum 系列返回的是反码结果, 使用时还要注意字节序处理.
 */

#ifndef _FRR_CHECKSUM_H
#define _FRR_CHECKSUM_H

#include <stdint.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 * IPv4 伪首部, 用于 TCP / UDP 校验和计算.
 * src / dst 为源和目的 IPv4 地址, rsvd 为保留字节(恒 0), proto 为上层协议号,
 * len 为上层报文(TCP / UDP 首部加数据)长度. 采用 packed 布局.
 */
/* IPv4 pseudoheader */
struct ipv4_ph {
	struct in_addr src;
	struct in_addr dst;
	uint8_t rsvd;
	uint8_t proto;
	uint16_t len;
} __attribute__((packed));

/*
 * IPv6 伪首部, 用于 TCP / UDP 校验和计算.
 * src / dst 为源和目的 IPv6 地址, ulpl 为上层载荷长度, zero 为 3 字节全 0,
 * next_hdr 为下一个首部(上层协议号). 采用 packed 布局.
 */
/* IPv6 pseudoheader */
struct ipv6_ph {
	struct in6_addr src;
	struct in6_addr dst;
	uint32_t ulpl;
	uint8_t zero[3];
	uint8_t next_hdr;
} __attribute__((packed));


/*
 * 对多段(iovec 数组)数据做 Internet 反码求和校验和.
 * iov / iov_len 描述数据段数组及段数.
 * 返回: 16 位反码校验和(已折叠进位). 这是其它 in_cksum 系列的基础实现.
 */
extern uint16_t in_cksumv(const struct iovec *iov, size_t iov_len);

/*
 * 对一段连续内存计算校验和(不含伪首部).
 * data / nbytes 为待校验数据及长度.
 * 返回 16 位校验和. 内部把单段数据包成 1 个 iovec 后调用 in_cksumv.
 */
static inline uint16_t in_cksum(const void *data, size_t nbytes)
{
	struct iovec iov[1];

	iov[0].iov_base = (void *)data;
	iov[0].iov_len = nbytes;
	return in_cksumv(iov, array_size(iov));
}

/*
 * 计算带 IPv4 伪首部的校验和(典型用于 TCP / UDP over IPv4).
 * ph 为 IPv4 伪首部, data / nbytes 为被校验数据.
 * 返回 16 位校验和. 内部把伪首部与数据作为两段 iovec 一起求和.
 */
static inline uint16_t in_cksum_with_ph4(const struct ipv4_ph *ph,
					 const void *data, size_t nbytes)
{
	struct iovec iov[2];

	iov[0].iov_base = (void *)ph;
	iov[0].iov_len = sizeof(*ph);
	iov[1].iov_base = (void *)data;
	iov[1].iov_len = nbytes;
	return in_cksumv(iov, array_size(iov));
}

/*
 * 计算带 IPv6 伪首部的校验和(典型用于 TCP / UDP over IPv6).
 * ph 为 IPv6 伪首部, data / nbytes 为被校验数据.
 * 返回 16 位校验和. 内部把伪首部与数据作为两段 iovec 一起求和.
 */
static inline uint16_t in_cksum_with_ph6(const struct ipv6_ph *ph,
					 const void *data, size_t nbytes)
{
	struct iovec iov[2];

	iov[0].iov_base = (void *)ph;
	iov[0].iov_len = sizeof(*ph);
	iov[1].iov_base = (void *)data;
	iov[1].iov_len = nbytes;
	return in_cksumv(iov, array_size(iov));
}

/*
 * 传给 fletcher_checksum 的 offset 使用该特殊值时, 表示只校验而不修改缓冲区;
 * 校验和正确时返回 0.
 */
#define FLETCHER_CHECKSUM_VALIDATE 0xffff
/*
 * 计算或校验 Fletcher 校验和(RFC 1008).
 * buffer: 报文起始地址; len: 报文长度; offset: 校验和字段在报文中的 0 基偏移.
 * 返回: 计算模式下返回校验和(并写入缓冲区); 传入 FLETCHER_CHECKSUM_VALIDATE
 *       时只校验, 合法时返回 0.
 * 副作用: 计算模式下会先把校验和字段清零再回写.
 */
extern uint16_t fletcher_checksum(uint8_t *buffer, const size_t len, const uint16_t offset);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_CHECKSUM_H */
