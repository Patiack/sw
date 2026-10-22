// SPDX-License-Identifier: ISC
/*
 * optimized ntop, about 10x faster than libc versions [as of 2019]
 *
 * Copyright (c) 2019  David Lamparter, for NetDEF, Inc.
 */

/*
 * ============================================================
 * 【文件导读】ntop.c —— FRR 自研的高性能 inet_ntop 替代实现
 * ============================================================
 *
 * 一、职责
 *   把二进制 IP 地址(AF_INET / AF_INET6 的 4 / 16 字节)转换为
 *   可读文本，如 "192.0.2.1"、"2001:db8::1"。目标是比 libc 的
 *   inet_ntop 快约 10 倍(2019 年基准)，用于路由协议高频的日志 /
 *   show 输出路径。
 *
 * 二、核心机制(整数到缓冲的快速文本化)
 *   - putbyte：把 0~255 以十进制(1~3 位)写入缓冲，用减法而非取模
 *     划分百位 / 十位 / 个位。
 *   - puthex：把 0~65535 以十六进制(1~4 位、无前导零)写入缓冲，
 *     靠小写查表逐位取值。
 *   - 二者都通过宏 pos 直接推进调用方的写指针，避免返回长度开销。
 *
 * 三、与其他模块的关系
 *   - lib/ntop.h 声明 frr_inet_ntop，本文件在合适平台用别名把
 *     libc 的 inet_ntop 覆盖为 frr_inet_ntop。
 *   - compiler.h 提供 assume() 与 OPTIMIZE 编译提示。
 *
 * 四、阅读顺序
 *   pos 宏 -> putbyte -> puthex -> frr_inet_ntop(主逻辑)
 *   -> 平台覆盖段(glibc 判定 + inet_ntop 别名)。
 *
 * 五、易踩的坑
 *   - 结果先写本地栈缓冲 buf 再 memcpy，size 不足时整体返回 NULL。
 *   - IPv6 的 :: 压缩只做“最长一段”，且 lon=1 的零段不压缩。
 *   - glibc 2.41 起自带更快实现，故新 glibc 不再覆盖 libc。
 * ============================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* compiler.h 提供 assume() 与 OPTIMIZE 属性宏 */
#include "compiler.h"

/* 宏：pos 展开为 (*posx)，即调用方写入游标指针的间接引用 */
#define pos (*posx)

/*
 * putbyte：把 0~255 的数值以十进制(最少 1 位、最多 3 位)写入缓冲，
 * 通过 **posx 推进游标；前置声明带 always_inline 与 OPTIMIZE 属性。
 */
static inline void putbyte(uint8_t bytex, char **posx)
	__attribute__((always_inline)) OPTIMIZE;

/* putbyte 实现：先按减 200 / 100 判定百位与十位，再用除法补足余下位 */
static inline void putbyte(uint8_t bytex, char **posx)
{
	bool zero = false;
	int byte = bytex, tmp, a, b;

	tmp = byte - 200;
	if (tmp >= 0) {
		*pos++ = '2';
		zero = true;
		byte = tmp;
	} else {
		tmp = byte - 100;
		if (tmp >= 0) {
			*pos++ = '1';
			zero = true;
			byte = tmp;
		}
	}

	/* make sure the compiler knows the value range of "byte" */
	assume(byte < 100 && byte >= 0);

	b = byte % 10;
	a = byte / 10;
	if (a || zero) {
		*pos++ = '0' + a;
		*pos++ = '0' + b;
	} else
		*pos++ = '0' + b;
}

/*
 * puthex：把 0~0xFFFF 以十六进制写入缓冲，最少 1 位、无前导零；
 * 前置声明带 always_inline 与 OPTIMIZE 属性。
 */
static inline void puthex(uint16_t word, char **posx)
	__attribute__((always_inline)) OPTIMIZE;

/* puthex 实现：用小写十六进制查表，按数值大小决定输出 4/3/2/1 位 */
static inline void puthex(uint16_t word, char **posx)
{
	const char *digits = "0123456789abcdef";
	if (word >= 0x1000)
		*pos++ = digits[(word >> 12) & 0xf];
	if (word >= 0x100)
		*pos++ = digits[(word >> 8) & 0xf];
	if (word >= 0x10)
		*pos++ = digits[(word >> 4) & 0xf];
	*pos++ = digits[word & 0xf];
}

/* 撤销 pos 宏，避免污染后续代码 */
#undef pos

/*
 * frr_inet_ntop：FRR 自研的 inet_ntop 替代；把二进制地址 src 转成
 * 文本写入 dst(size 为 dst 容量)，成功返回 dst，失败返回 NULL。
 * flatten 属性要求把被调函数尽量内联进来以减少调用开销。
 */
const char *frr_inet_ntop(int af, const void * restrict src,
			  char * restrict dst, socklen_t size)
	__attribute__((flatten)) OPTIMIZE;

/* frr_inet_ntop 实现主体：见下方函数体 */
const char *frr_inet_ntop(int af, const void * restrict src,
			  char * restrict dst, socklen_t size)
{
	const uint8_t *b = src;
	/* 8 * "abcd:" for IPv6
	 * note: the IPv4-embedded IPv6 syntax is only used for ::A.B.C.D,
	 * which isn't longer than 40 chars either.  even with ::ffff:A.B.C.D
	 * it's shorter.
	 */
	char buf[8 * 5], *o = buf;
	size_t best = 0, bestlen = 0, curlen = 0, i;

	switch (af) {
	/* 按地址族分派：AF_INET 走点分十进制；inet4 标签供 IPv6 内嵌 IPv4 复用 */
	case AF_INET:
inet4:
		putbyte(b[0], &o);
		*o++ = '.';
		putbyte(b[1], &o);
		*o++ = '.';
		putbyte(b[2], &o);
		*o++ = '.';
		putbyte(b[3], &o);
		*o++ = '\0';
		break;
	case AF_INET6:
		/* 第一趟：扫描 8 组 16 位字，找出最长全零段(curlen 累计，
		 * 记起点 best 与长度 bestlen)，供后续 :: 压缩使用 */
		for (i = 0; i < 8; i++) {
			if (b[i * 2] || b[i * 2 + 1]) {
				if (curlen && curlen > bestlen) {
					best = i - curlen;
					bestlen = curlen;
				}
				curlen = 0;
				continue;
			}
			curlen++;
		}
		if (curlen && curlen > bestlen) {
			best = i - curlen;
			bestlen = curlen;
		}
		if (best == 0 && bestlen == 5 && b[10] == 0xff && b[11] == 0xff) {
			/* ::ffff:A.B.C.D */
			*o++ = ':';
			*o++ = ':';
			*o++ = 'f';
			*o++ = 'f';
			*o++ = 'f';
			*o++ = 'f';
			*o++ = ':';
			b += 12;
			goto inet4;
		}
		if (best == 0 && bestlen == 6) {
			*o++ = ':';
			*o++ = ':';
			b += 12;
			goto inet4;
		}
		if (bestlen == 1)
			bestlen = 0;

		/* 第二趟：正式输出，遇到最长零段用 :: 压缩，否则逐组
		 * puthex 输出并以 : 分隔 */
		for (i = 0; i < 8; i++) {
			if (bestlen && i == best) {
				if (i == 0)
					*o++ = ':';
				*o++ = ':';
				continue;
			}
			if (i > best && i < best + bestlen) {
				continue;
			}
			puthex((b[i * 2] << 8) | b[i * 2 + 1], &o);

			if (i < 7)
				*o++ = ':';
		}
		*o++ = '\0';
		break;
	/* 不支持的地址族，直接失败 */
	default:
		return NULL;
	}

	/* 计算实际写入长度，超过用户缓冲 size 则失败 */
	i = o - buf;
	if (i > size)
		return NULL;
	/* compiler might inline memcpy if it knows the length is short,
	 * although neither gcc nor clang actually do this currently [2019]
	 */
	assume(i <= 8 * 5);
	memcpy(dst, buf, i);
	return dst;
}

/* glibc 2.41 起自带更快的 inet_ntop，故在新 glibc 上放弃覆盖 */
#if defined(__GLIBC__) && !defined(INET_NTOP_NO_OVERRIDE)
#if __GLIBC__ > 2 || __GLIBC_MINOR__ >= 41
#define INET_NTOP_NO_OVERRIDE
#endif
#endif

/*
 * 在未禁用覆盖的平台，用链接器别名把 libc 的 inet_ntop 指向
 * frr_inet_ntop：既获得加速，又让回溯里显示 frr_inet_ntop 便于调试。
 */
#if !defined(INET_NTOP_NO_OVERRIDE)
/* we want to override libc inet_ntop, but make sure it shows up in backtraces
 * as frr_inet_ntop (to avoid confusion while debugging)
 */
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
	__attribute__((alias ("frr_inet_ntop")));
#endif
