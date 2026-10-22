/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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
 * 【文件导读】printf/printflocal.h —— FRR 内嵌 printf 的内部常量与本地声明
 * ============================================================================
 * 【它在 FRR 里干什么】本文件是 FRR 从 FreeBSD 引入的整份 printf 实现的内部头，
 * 只被 printf/ 目录下的实现文件（vfprintf.c 等）包含；它定义格式化时用到的
 * “转换标志位”、数字与字符换算宏、可变参数联合体，以及几个内部函数原型。
 *
 * 【核心内容】
 *   - 一组转换标志位（ALT / LADJUST / LONGDBL / ZEROPAD / FPT ...）：解析格式串
 *     时按位累积，决定对齐、补零、长度修饰（l / ll / h / z / j / t）等。
 *   - to_digit / is_digit / to_char：字符与数字互转的小工具宏。
 *   - union arg：把所有可能的参数类型并进一个联合体，供“位置参数”(%1$s) 与常规
 *     取值统一定位（取哪种成员由转换标志决定）。
 *   - _frr_find_arguments / _frr_find_warguments：位置参数解析入口（见 printf-pos.c）。
 *   - printfrr_extp / printfrr_exti：FRR 自定义 %p 扩展输出的两个内部辅助函数。
 *
 * 【与其它模块的关系】直接依赖 printfrr.h（FRR 的 printf 扩展接口，如 fbuf、
 * printfrr_eargs、自定义 %p 转换）与 compiler.h。
 *
 * 【阅读顺序】转换标志位 → 字符换算宏 → union arg → 位置参数与扩展函数原型。
 * ============================================================================
 */

/* 编译器特性宏（DSO_LOCAL、__restrict 等）与 FRR printf 扩展接口
 * （fbuf / printfrr_eargs / 自定义 %p 转换）。
 */
#include "compiler.h"
#include "printfrr.h"

/* 【标志位】转换标志位：解析格式串时按位累加到 flags，供后续决定对齐(LADJUST)、
 * 补零(ZEROPAD)、替换形式(ALT)、浮点(FPT)、千分位分组(GROUPING)，以及长度修饰
 * （LONGINT / LLONGINT / SHORTINT / SIZET / PTRDIFFT / INTMAXT / FASTINT 等）。
 * 注意：这些是内部位掩码，不是格式串里看到的字符。
 */
/*
 * Flags used during conversion.
 */
#define	ALT		0x001		/* alternate form */
#define	LADJUST		0x004		/* left adjustment */
#define	LONGDBL		0x008		/* long double */
#define	LONGINT		0x010		/* long integer */
#define	LLONGINT	0x020		/* long long integer */
#define	SHORTINT	0x040		/* short integer */
#define	ZEROPAD		0x080		/* zero (as opposed to blank) pad */
#define	FPT		0x100		/* Floating point number */
#define	GROUPING	0x200		/* use grouping ("'" flag) */
					/* C99 additional size modifiers: */
#define	SIZET		0x400		/* size_t */
#define	PTRDIFFT	0x800		/* ptrdiff_t */
#define	INTMAXT		0x1000		/* intmax_t */
#define	CHARINT		0x2000		/* print char using int format */
#define	FASTINT		0x4000		/* int_fastN_t */

/* 【宏】字符与数字互转：to_digit 把 '0'~'9' 变为数值，is_digit 判断是否为数字，
 * to_char 把数值变回字符。仅供内部整数转换使用。
 */
/*
 * Macros for converting digits to letters and vice versa
 */
#define	to_digit(c)	((c) - '0')
#define is_digit(c)	((unsigned)to_digit(c) <= 9)
#define	to_char(n)	((n) + '0')

/* Size of the static argument table. */
/* 【宏】位置参数解析时栈上静态参数表的容量：参数不超过 8 个就在栈上小数组里收集，
 * 超过则退回堆分配。
 */
#define STATIC_ARG_TBL_SIZE 8

/* 【联合体】union arg —— 承载任意一种 printf 参数类型。所有成员共享同一块存储，
 * 具体按哪个成员解释由转换标志（长度修饰）决定；指针成员用于 %n 写入或取地址
 * 型参数。用联合体可在按序取参时以统一步长推进 va_list。
 */
union arg {
	int	intarg;
	u_int	uintarg;
	long	longarg;
	u_long	ulongarg;
	long long longlongarg;
	unsigned long long ulonglongarg;
	ptrdiff_t ptrdiffarg;
	size_t	sizearg;
	intmax_t intmaxarg;
	uintmax_t uintmaxarg;
	void	*pvoidarg;
	char	*pchararg;
	signed char *pschararg;
	short	*pshortarg;
	int	*pintarg;
	long	*plongarg;
	long long *plonglongarg;
	ptrdiff_t *pptrdiffarg;
	ssize_t	*pssizearg;
	intmax_t *pintmaxarg;
#ifndef NO_FLOATING_POINT
	double	doublearg;
	long double longdoublearg;
#endif
	wint_t	wintarg;
	wchar_t	*pwchararg;
};

/* Handle positional parameters. */
/* 【函数原型】_frr_find_arguments —— 解析位置参数（如 %1$s）：扫描格式串，把各参数
 * 就地收集并（必要时在堆上）分配 union arg 数组，返回参数个数（负数表示出错），
 * 同时经 *argv 回填数组首址。实现在 printf-pos.c。DSO_LOCAL 表示不导出该符号。
 */
int	_frr_find_arguments(const char *, va_list, union arg **) DSO_LOCAL;
#ifdef WCHAR_SUPPORT
/* 【函数原型】宽字符版本的位置参数解析（仅在 WCHAR_SUPPORT 下编译）。 */
int	_frr_find_warguments(const wchar_t *, va_list, union arg **) DSO_LOCAL;
#endif

/* returns number of bytes needed for full output, or -1 */
/* 【函数原型】FRR 自定义 %p 扩展的输出辅助：printfrr_extp 处理指针型参数，
 * printfrr_exti 处理整型参数。二者把格式化结果写入 fbuf，返回完整输出所需字节数
 * （负数表示出错）。由 printfrr.h 中的扩展机制调用。
 */
ssize_t printfrr_extp(struct fbuf *, struct printfrr_eargs *ea, const void *)
	DSO_LOCAL;
ssize_t printfrr_exti(struct fbuf *, struct printfrr_eargs *ea, uintmax_t)
	DSO_LOCAL;
