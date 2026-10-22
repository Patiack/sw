/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 *
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* ============================================================================
 * 【文件导读】printf/printfcommon.h —— printf 与 wprintf 共用的内部例程
 * ============================================================================
 * 【它在 FRR 里干什么】本文件是被“模板化包含”的头：调用方在包含它之前必须先定义
 * CHAR 为 char 或 wchar_t，于是同一份代码分别产出窄字符与宽字符两个版本。它提供
 * 格式化过程中反复用到的两块公共设施：
 *   1) 输出缓冲适配层 io_state：把格式化结果写入 FRR 的 fbuf（而非裸 FILE）。
 *   2) 整数转 ASCII 的底层实现 __ultoa / __ujtoa。
 *
 * 【核心内容】
 *   - struct io_state、io_init、io_print、io_pad、io_printandpad：对 fbuf 的小块
 *     写入、补空格或补零、以及“截断或填充到定长”等操作。
 *   - __ultoa / __ujtoa：把无符号长整型 / uintmax_t 按给定进制（2 / 8 / 10 / 16）
 *     转成字符串，供 vfprintf.c 输出整数时调用。
 *   - 常量：NIOV、PADSIZE，以及 blanks / zeroes 两个填充字符数组。
 *
 * 【易踩的坑】io_print 不做拷贝：传入的缓冲必须一直有效直到数据真正被消费
 * （见其上方英文 WARNING）。这些例程是 static inline，故只应在 printf/ 内部包含。
 *
 * 【阅读顺序】io_state 与 io_* 系列 → PADSIZE / blanks / zeroes → 整数转换例程。
 * ============================================================================
 */

/*
 * This file defines common routines used by both printf and wprintf.
 * You must define CHAR to either char or wchar_t prior to including this.
 */


/* 【函数原型】整数转字符串的前置声明：__ultoa 处理 u_long，__ujtoa 处理 uintmax_t。
 * 两者都从后向前写入缓冲区并返回结果串首字符的指针。
 */
static CHAR	*__ujtoa(uintmax_t, CHAR *, int, int, const char *);
static CHAR	*__ultoa(u_long, CHAR *, int, int, const char *);

/* 【结构】io_state —— 输出适配层。cb 指向目标 fbuf（可为 NULL，表示丢弃输出），
 * avail 记录目标缓冲剩余可用字节数。NIOV 为 BSD 原版遗留的 iovec 组数常量，
 * 本实现未直接使用，保留以贴近上游。
 */
#define NIOV 8
struct io_state {
	struct fbuf *cb;
	size_t avail;
};

/* 【函数】io_init —— 初始化 io_state：记录目标 fbuf，并按 cb->len - (cb->pos - cb->buf)
 * 算出剩余可用空间（cb 为 NULL 时 avail 记 0，等价于“黑洞”输出）。
 */
static inline void
io_init(struct io_state *iop, struct fbuf *cb)
{
	iop->cb = cb;
	iop->avail = cb ? cb->len - (cb->pos - cb->buf) : 0;
}

/* 【函数】io_print —— 把 ptr 起 len 字节写入 fbuf。不做越界增长：最多写 avail 字节
 * （写满即截断），并同步更新 avail 与 fbuf->pos。返回 0 表示成功。
 * 注意：传入的缓冲不会被拷贝，必须保持有效直到内容被真正消费。
 */
/*
 * WARNING: The buffer passed to io_print() is not copied immediately; it must
 * remain valid until io_flush() is called.
 */
static inline int
io_print(struct io_state *iop, const CHAR * __restrict ptr, size_t len)
{
	size_t copylen = len;

	if (!iop->cb || !len)
		return 0;
	if (iop->avail < copylen)
		copylen = iop->avail;

	memcpy(iop->cb->pos, ptr, copylen);
	iop->avail -= copylen;
	iop->cb->pos += copylen;
	return 0;
}

/* 【常量】PADSIZE 为一次填充的块大小（兼顾效率与体积）；blanks 是空格数组，
 * zeroes 是零字符数组。io_pad 从中取片段批量写入以减少调用次数。
 */
/*
 * Choose PADSIZE to trade efficiency vs. size.  If larger printf
 * fields occur frequently, increase PADSIZE and make the initialisers
 * below longer.
 */
#define	PADSIZE	16		/* pad chunk size */
static const CHAR blanks[PADSIZE] =
{' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
static const CHAR zeroes[PADSIZE] =
{'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

/* 【函数】io_pad —— 用 with（指向 blanks 或 zeroes）填充 howmany 个字符，按 PADSIZE
 * 成块写入。返回 0 成功、-1 失败（写入出错）。
 */
/*
 * Pad with blanks or zeroes. 'with' should point to either the blanks array
 * or the zeroes array.
 */
static inline int
io_pad(struct io_state *iop, int howmany, const CHAR * __restrict with)
{
	int n;

	while (howmany > 0) {
		n = (howmany >= PADSIZE) ? PADSIZE : howmany;
		if (io_print(iop, with, n))
			return (-1);
		howmany -= n;
	}
	return (0);
}

/* 【函数】io_printandpad —— 输出 [p, ep) 区间字符串，但恰好输出 len 个字符：不足则用
 * with 补足，超出则截断。返回 0 成功、-1 失败。
 */
/*
 * Print exactly len characters of the string spanning p to ep, truncating
 * or padding with 'with' as necessary.
 */
static inline int
io_printandpad(struct io_state *iop, const CHAR *p, const CHAR *ep,
	       int len, const CHAR * __restrict with)
{
	int p_len;

	p_len = ep - p;
	if (p_len > len)
		p_len = len;
	if (p_len > 0) {
		if (io_print(iop, p, p_len))
			return (-1);
	} else {
		p_len = 0;
	}
	return (io_pad(iop, len - p_len, with));
}

/* 【函数】__ultoa —— 把 u_long 值按 base（10 / 2 / 8 / 16）从后向前写入以 endp 为
 * 尾端的缓冲区，返回结果串首字符指针。octzero 为真时八进制保证前导 0；xdigs 提供
 * 十六进制数字表。base 非上述值会 abort（不应发生）。为速度对各进制分别实现。
 */
/*
 * Convert an unsigned long to ASCII for printf purposes, returning
 * a pointer to the first character of the string representation.
 * Octal numbers can be forced to have a leading zero; hex numbers
 * use the given digits.
 */
static CHAR *
__ultoa(u_long val, CHAR *endp, int base, int octzero, const char *xdigs)
{
	CHAR *cp = endp;
	long sval;

	/*
	 * Handle the three cases separately, in the hope of getting
	 * better/faster code.
	 */
	switch (base) {
	case 10:
		if (val < 10) {	/* many numbers are 1 digit */
			*--cp = to_char(val);
			return (cp);
		}
		/*
		 * On many machines, unsigned arithmetic is harder than
		 * signed arithmetic, so we do at most one unsigned mod and
		 * divide; this is sufficient to reduce the range of
		 * the incoming value to where signed arithmetic works.
		 */
		if (val > LONG_MAX) {
			*--cp = to_char(val % 10);
			sval = val / 10;
		} else
			sval = val;
		do {
			*--cp = to_char(sval % 10);
			sval /= 10;
		} while (sval != 0);
		break;

	case 2:
		do {
			*--cp = to_char(val & 1);
			val >>= 1;
		} while (val);
		break;

	case 8:
		do {
			*--cp = to_char(val & 7);
			val >>= 3;
		} while (val);
		if (octzero && *cp != '0')
			*--cp = '0';
		break;

	case 16:
		do {
			*--cp = xdigs[val & 15];
			val >>= 4;
		} while (val);
		break;

	default:			/* oops */
		abort();
	}
	return (cp);
}

/* Identical to __ultoa, but for intmax_t. */
/* 【函数】__ujtoa —— 与 __ultoa 同构，但用于 uintmax_t。若值不超过 ULONG_MAX 会直接
 * 委托给更快的 __ultoa，否则走本函数的通用路径（含 INTMAX_MAX 溢出处理）。
 */
static CHAR *
__ujtoa(uintmax_t val, CHAR *endp, int base, int octzero, const char *xdigs)
{
	CHAR *cp = endp;
	intmax_t sval;

	/* quick test for small values; __ultoa is typically much faster */
	/* (perhaps instead we should run until small, then call __ultoa?) */
	if (val <= ULONG_MAX)
		return (__ultoa((u_long)val, endp, base, octzero, xdigs));
	switch (base) {
	case 10:
		if (val < 10) {
			*--cp = to_char(val % 10);
			return (cp);
		}
		if (val > INTMAX_MAX) {
			*--cp = to_char(val % 10);
			sval = val / 10;
		} else
			sval = val;
		do {
			*--cp = to_char(sval % 10);
			sval /= 10;
		} while (sval != 0);
		break;

	case 2:
		do {
			*--cp = to_char(val & 1);
			val >>= 1;
		} while (val);
		break;

	case 8:
		do {
			*--cp = to_char(val & 7);
			val >>= 3;
		} while (val);
		if (octzero && *cp != '0')
			*--cp = '0';
		break;

	case 16:
		do {
			*--cp = xdigs[val & 15];
			val >>= 4;
		} while (val);
		break;

	default:
		abort();
	}
	return (cp);
}
