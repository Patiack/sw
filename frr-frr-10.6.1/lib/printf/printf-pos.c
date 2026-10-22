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
 * 【文件导读】printf/printf-pos.c —— 位置参数（%m$ / %m$.n$）解析
 * ============================================================================
 * 【它在 FRR 里干什么】本文件处理 printf 的位置参数语法：%2$s 取第 2 个参数、
 * %1$*2$d 用第 2 个参数作第 1 个的宽度等。vfprintf.c 在遇到形如 %n$ 的写法时调用
 * 这里的 _frr_find_arguments，把散落在 va_list 里的实参“拉直”成一张按下标索引的参数
 * 表，之后主循环即可按任意顺序取参。
 *
 * 【为什么需要它】标准 printf 只能顺序消费 va_list；位置参数要求可乱序、可重复引用，
 * 因此必须先把所有实参的类型收集一遍，再逐个取出。
 *
 * 【核心数据结构】
 *   enum typeid      : 每个实参的类型标记（值类型 T_*、指针类型 TP_* 等）。
 *   struct typetable : 可扩展的类型表，记录每个实参下标对应的 typeid。
 *   两个阶段：先扫描格式串填 typetable，再由 build_arg_table 依类型用 va_arg 取出实参
 *   填入 union arg 数组（union arg 定义见 printflocal.h）。
 *
 * 【主要函数】inittypes、freetypes、_ensurespace、addtype、addsarg、adduarg、addaster、
 * addwaster 负责维护类型表；_frr_find_arguments 与 _frr_find_warguments 是窄 / 宽字符
 * 两个解析入口；__grow_type_table 负责扩容；build_arg_table 负责取值。
 *
 * 【阅读顺序】enum typeid → struct typetable → 类型表维护函数 → 两个 find_arguments
 * → __grow_type_table → build_arg_table。
 * ============================================================================
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

/*
 * This is the code responsible for handling positional arguments
 * (%m$ and %m$.n$) for vfprintf() and vfwprintf().
 */

#include <sys/types.h>

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>

#include "printflocal.h"

/* 【常量】MAX_POSARG —— 位置参数下标上限：优先取 <limits.h> 的 NL_ARGMAX，缺省为
 * 65536。解析 %n$ 时超过此值即视为错误（防止恶意格式串导致巨量分配）。
 */
#ifdef	NL_ARGMAX
#define	MAX_POSARG	NL_ARGMAX
#else
#define	MAX_POSARG	65536
#endif

/* 【枚举】typeid —— 位置参数类型表里每个实参的类型标记。分三类：T_* 为值类型
 * （如 T_INT / T_LONG / T_DOUBLE），TP_* 为指针类型（如 TP_INT / TP_VOID，对应 %n
 * 或需要地址的参数），另有 T_WINT / TP_WCHAR 处理宽字符。构建参数表时据此决定用哪种
 * va_arg 类型取值。
 */
/*
 * Type ids for argument type table.
 */
enum typeid {
	T_UNUSED, TP_SHORT, T_INT, T_U_INT, TP_INT,
	T_LONG, T_U_LONG, TP_LONG, T_LLONG, T_U_LLONG, TP_LLONG,
	T_PTRDIFFT, TP_PTRDIFFT, T_SSIZET, T_SIZET, TP_SSIZET,
	T_INT64T, T_UINT64T, T_INTMAXT, T_UINTMAXT, TP_INTMAXT, TP_VOID,
	TP_CHAR, TP_SCHAR, T_DOUBLE, T_LONG_DOUBLE, T_WINT, TP_WCHAR
};

/* An expandable array of types. */
/* 【结构】typetable —— 位置参数类型表。table 指向类型数组（初始指向内嵌的
 * stattable，溢出后改为 malloc / realloc 所得）；tablesize 为当前容量；tablemax 为
 * 已用到的最大下标；nextarg 是 1 基的当前参数下标。初始容量为 STATIC_ARG_TBL_SIZE。
 */
struct typetable {
	enum typeid *table; /* table of types */
	enum typeid stattable[STATIC_ARG_TBL_SIZE];
	u_int tablesize;	/* current size of type table */
	u_int tablemax;		/* largest used index in table */
	u_int nextarg;		/* 1-based argument index */
};

/* 【函数原型】__grow_type_table 扩容类型表；build_arg_table 依据类型表用 va_arg 取值
 * 填出参数表。前者返回 0 / -1，后者失败时把 *argtable 置 NULL。
 */
static int	__grow_type_table(struct typetable *);
static void	build_arg_table (struct typetable *, va_list, union arg **);

/* 【函数】inittypes —— 初始化 typetable：table 指向内嵌静态数组、容量置
 * STATIC_ARG_TBL_SIZE、nextarg 从 1 起，并把所有槽位置为 T_UNUSED。
 */
/*
 * Initialize a struct typetable.
 */
static inline void
inittypes(struct typetable *types)
{
	u_int n;

	types->table = types->stattable;
	types->tablesize = STATIC_ARG_TBL_SIZE;
	types->tablemax = 0;
	types->nextarg = 1;
	for (n = 0; n < STATIC_ARG_TBL_SIZE; n++)
		types->table[n] = T_UNUSED;
}

/* 【函数】freetypes —— typetable 析构：仅当 table 不是内嵌静态数组（即由 malloc /
 * realloc 所得）时才 free。
 */
/*
 * struct typetable destructor.
 */
static inline void
freetypes(struct typetable *types)
{

	if (types->table != types->stattable)
		free (types->table);
}

/* 【函数】_ensurespace —— 确保类型表还能再放一个类型：nextarg 达到容量时调用
 * __grow_type_table 扩容；并顺带把 tablemax 推进到 nextarg。返回 0 成功、-1 失败。
 */
/*
 * Ensure that there is space to add a new argument type to the type table.
 * Expand the table if necessary. Returns 0 on success.
 */
static inline int
_ensurespace(struct typetable *types)
{

	if (types->nextarg >= types->tablesize) {
		if (__grow_type_table(types))
			return -1;
	}
	if (types->nextarg > types->tablemax)
		types->tablemax = types->nextarg;
	return 0;
}

/* 【函数】addtype —— 向类型表追加一个类型（先 _ensurespace 保证空间）。返回 0 成功、
 * -1 失败（空间不足或溢出）。
 */
/*
 * Add an argument type to the table, expanding if necessary.
 * Returns 0 on success.
 */
static inline int
addtype(struct typetable *types, enum typeid type)
{

	if (_ensurespace(types))
		return -1;
	types->table[types->nextarg++] = type;
	return 0;
}

/* 【函数】addsarg —— 按长度修饰 flags 为有符号整数转换符(d / i)登记类型：LONGDBL 对应
 * T_INT64T、INTMAXT 对应 T_INTMAXT、SIZET 对应 T_SSIZET、PTRDIFFT 对应 T_PTRDIFFT、
 * LLONGINT 对应 T_LLONG、LONGINT 对应 T_LONG、否则 T_INT。返回 0 / -1。
 */
static inline int
addsarg(struct typetable *types, int flags)
{

	if (_ensurespace(types))
		return -1;
	if (flags & LONGDBL)
		types->table[types->nextarg++] = T_INT64T;
	else if (flags & INTMAXT)
		types->table[types->nextarg++] = T_INTMAXT;
	else if (flags & SIZET)
		types->table[types->nextarg++] = T_SSIZET;
	else if (flags & PTRDIFFT)
		types->table[types->nextarg++] = T_PTRDIFFT;
	else if (flags & LLONGINT)
		types->table[types->nextarg++] = T_LLONG;
	else if (flags & LONGINT)
		types->table[types->nextarg++] = T_LONG;
	else
		types->table[types->nextarg++] = T_INT;
	return 0;
}

/* 【函数】adduarg —— 与 addsarg 对应，为无符号转换符(o / u / x / X)登记类型；注意
 * SIZET 与 PTRDIFFT 都映射到 T_SIZET（无符号）。返回 0 / -1。
 */
static inline int
adduarg(struct typetable *types, int flags)
{

	if (_ensurespace(types))
		return -1;
	if (flags & LONGDBL)
		types->table[types->nextarg++] = T_UINT64T;
	else if (flags & INTMAXT)
		types->table[types->nextarg++] = T_UINTMAXT;
	else if (flags & SIZET)
		types->table[types->nextarg++] = T_SIZET;
	else if (flags & PTRDIFFT)
		types->table[types->nextarg++] = T_SIZET;
	else if (flags & LLONGINT)
		types->table[types->nextarg++] = T_U_LLONG;
	else if (flags & LONGINT)
		types->table[types->nextarg++] = T_U_LONG;
	else
		types->table[types->nextarg++] = T_U_INT;
	return 0;
}

/* 【函数】addaster —— 处理“星号”宽度 / 精度参数：形如 *n$ 时把 nextarg 临时切到 n
 * 登记一个 T_INT 再复位，并前移 *fmtp 越过 "n$"；否则直接登记下一个 T_INT。
 * 返回 0 / -1。
 */
/*
 * Add * arguments to the type array.
 */
static inline int
addaster(struct typetable *types, char **fmtp)
{
	char *cp;
	u_int n2;

	n2 = 0;
	cp = *fmtp;
	while (is_digit(*cp)) {
		n2 = 10 * n2 + to_digit(*cp);
		cp++;
	}
	if (*cp == '$') {
		u_int hold = types->nextarg;
		types->nextarg = n2;
		if (addtype(types, T_INT))
			return -1;
		types->nextarg = hold;
		*fmtp = ++cp;
	} else {
		if (addtype(types, T_INT))
			return -1;
	}
	return 0;
}

#ifdef WCHAR_SUPPORT
/* 【函数】addwaster —— addaster 的宽字符版（仅 WCHAR_SUPPORT 下编译），逻辑相同，
 * 只是操作 wchar_t 格式串。
 */
static inline int
addwaster(struct typetable *types, wchar_t **fmtp)
{
	wchar_t *cp;
	u_int n2;

	n2 = 0;
	cp = *fmtp;
	while (is_digit(*cp)) {
		n2 = 10 * n2 + to_digit(*cp);
		cp++;
	}
	if (*cp == '$') {
		u_int hold = types->nextarg;
		types->nextarg = n2;
		if (addtype(types, T_INT))
			return -1;
		types->nextarg = hold;
		*fmtp = ++cp;
	} else {
		if (addtype(types, T_INT))
			return -1;
	}
	return 0;
}
#endif /* WCHAR_SUPPORT */

/* 【函数】_frr_find_arguments —— 位置参数（%m$ / %m$.n$）解析入口：先扫描格式串得到
 * 每个实参的类型表(typetable)，再经 build_arg_table 构建 union arg 参数表，经
 * *argtable 回填。返回 0 表示成功；失败返回非 0 并置 errno（*argtable 为 NULL 也返回
 * 非 0）。初始参数表应指向 STATIC_ARG_TBL_SIZE 大小的数组，溢出时会被替换为 malloc
 * 的表；堆表由调用方（vfprintf）负责释放。
 */
/*
 * Find all arguments when a positional parameter is encountered.  Returns a
 * table, indexed by argument number, of pointers to each arguments.  The
 * initial argument table should be an array of STATIC_ARG_TBL_SIZE entries.
 * It will be replaces with a malloc-ed one if it overflows.
 * Returns 0 on success. On failure, returns nonzero and sets errno.
 */
int
_frr_find_arguments (const char *fmt0, va_list ap, union arg **argtable)
{
	char *fmt;		/* format string */
	int ch;			/* character from fmt */
	u_int n;		/* handy integer (short term usage) */
	int error;
	int flags;		/* flags as above */
	struct typetable types;	/* table of types */

	fmt = (char *)fmt0;
	inittypes(&types);
	error = 0;

	/*
	 * Scan the format for conversions (`%' character).
	 */
	for (;;) {
		while ((ch = *fmt) != '\0' && ch != '%')
			fmt++;
		if (ch == '\0')
			goto done;
		fmt++;		/* skip over '%' */

		flags = 0;

rflag:		ch = *fmt++;
reswitch:	switch (ch) {
		case ' ':
		case '#':
			goto rflag;
		case '*':
			if ((error = addaster(&types, &fmt)))
				goto error;
			goto rflag;
		case '-':
		case '+':
		case '\'':
			goto rflag;
		case '.':
			if ((ch = *fmt++) == '*') {
				if ((error = addaster(&types, &fmt)))
					goto error;
				goto rflag;
			}
			while (is_digit(ch)) {
				ch = *fmt++;
			}
			goto reswitch;
		case '0':
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do {
				n = 10 * n + to_digit(ch);
				/* Detect overflow */
				if (n > MAX_POSARG) {
					error = -1;
					goto error;
				}
				ch = *fmt++;
			} while (is_digit(ch));
			if (ch == '$') {
				types.nextarg = n;
				goto rflag;
			}
			goto reswitch;
		case 'L':
			flags |= LONGDBL;
			goto rflag;
		case 'h':
			if (flags & SHORTINT) {
				flags &= ~SHORTINT;
				flags |= CHARINT;
			} else
				flags |= SHORTINT;
			goto rflag;
		case 'j':
			flags |= INTMAXT;
			goto rflag;
		case 'l':
			if (flags & LONGINT) {
				flags &= ~LONGINT;
				flags |= LLONGINT;
			} else
				flags |= LONGINT;
			goto rflag;
		case 'q':
			flags |= LLONGINT;	/* not necessarily */
			goto rflag;
		case 't':
			flags |= PTRDIFFT;
			goto rflag;
		case 'z':
			flags |= SIZET;
			goto rflag;
		case 'C':
			flags |= LONGINT;
			fallthrough;
		case 'c':
			error = addtype(&types,
					(flags & LONGINT) ? T_WINT : T_INT);
			if (error)
				goto error;
			break;
		case 'D':
			flags |= LONGINT;
			fallthrough;
		case 'd':
		case 'i':
			if ((error = addsarg(&types, flags)))
				goto error;
			break;
#ifndef NO_FLOATING_POINT
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':
			error = addtype(&types,
			    (flags & LONGDBL) ? T_LONG_DOUBLE : T_DOUBLE);
			if (error)
				goto error;
			break;
#endif /* !NO_FLOATING_POINT */
#ifdef DANGEROUS_PERCENT_N
		case 'n':
			if (flags & INTMAXT)
				error = addtype(&types, TP_INTMAXT);
			else if (flags & PTRDIFFT)
				error = addtype(&types, TP_PTRDIFFT);
			else if (flags & SIZET)
				error = addtype(&types, TP_SSIZET);
			else if (flags & LLONGINT)
				error = addtype(&types, TP_LLONG);
			else if (flags & LONGINT)
				error = addtype(&types, TP_LONG);
			else if (flags & SHORTINT)
				error = addtype(&types, TP_SHORT);
			else if (flags & CHARINT)
				error = addtype(&types, TP_SCHAR);
			else
				error = addtype(&types, TP_INT);
			if (error)
				goto error;
			continue;	/* no output */
#endif
		case 'O':
			flags |= LONGINT;
			fallthrough;
		case 'o':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		case 'p':
			if ((error = addtype(&types, TP_VOID)))
				goto error;
			break;
		case 'S':
			flags |= LONGINT;
			fallthrough;
		case 's':
			error = addtype(&types,
					(flags & LONGINT) ? TP_WCHAR : TP_CHAR);
			if (error)
				goto error;
			break;
		case 'U':
			flags |= LONGINT;
			fallthrough;
		case 'u':
		case 'X':
		case 'x':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		default:	/* "%?" prints ?, unless ? is NUL */
			if (ch == '\0')
				goto done;
			break;
		}
	}
done:
	build_arg_table(&types, ap, argtable);
error:
	freetypes(&types);
	return (error || *argtable == NULL);
}

#ifdef WCHAR_SUPPORT
/* wchar version of __find_arguments. */
/* 【函数】_frr_find_warguments —— _frr_find_arguments 的宽字符版（WCHAR_SUPPORT 下），
 * 逻辑一致，只是格式串为 wchar_t * 且用 addwaster 处理星号参数。
 */
int
_frr_find_warguments (const wchar_t *fmt0, va_list ap, union arg **argtable)
{
	wchar_t *fmt;		/* format string */
	wchar_t ch;		/* character from fmt */
	u_int n;		/* handy integer (short term usage) */
	int error;
	int flags;		/* flags as above */
	struct typetable types;	/* table of types */

	fmt = (wchar_t *)fmt0;
	inittypes(&types);
	error = 0;

	/*
	 * Scan the format for conversions (`%' character).
	 */
	for (;;) {
		while ((ch = *fmt) != '\0' && ch != '%')
			fmt++;
		if (ch == '\0')
			goto done;
		fmt++;		/* skip over '%' */

		flags = 0;

rflag:		ch = *fmt++;
reswitch:	switch (ch) {
		case ' ':
		case '#':
			goto rflag;
		case '*':
			if ((error = addwaster(&types, &fmt)))
				goto error;
			goto rflag;
		case '-':
		case '+':
		case '\'':
			goto rflag;
		case '.':
			if ((ch = *fmt++) == '*') {
				if ((error = addwaster(&types, &fmt)))
					goto error;
				goto rflag;
			}
			while (is_digit(ch)) {
				ch = *fmt++;
			}
			goto reswitch;
		case '0':
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do {
				n = 10 * n + to_digit(ch);
				/* Detect overflow */
				if (n > MAX_POSARG) {
					error = -1;
					goto error;
				}
				ch = *fmt++;
			} while (is_digit(ch));
			if (ch == '$') {
				types.nextarg = n;
				goto rflag;
			}
			goto reswitch;
		case 'L':
			flags |= LONGDBL;
			goto rflag;
		case 'h':
			if (flags & SHORTINT) {
				flags &= ~SHORTINT;
				flags |= CHARINT;
			} else
				flags |= SHORTINT;
			goto rflag;
		case 'j':
			flags |= INTMAXT;
			goto rflag;
		case 'l':
			if (flags & LONGINT) {
				flags &= ~LONGINT;
				flags |= LLONGINT;
			} else
				flags |= LONGINT;
			goto rflag;
		case 'q':
			flags |= LLONGINT;	/* not necessarily */
			goto rflag;
		case 't':
			flags |= PTRDIFFT;
			goto rflag;
		case 'z':
			flags |= SIZET;
			goto rflag;
		case 'C':
			flags |= LONGINT;
			fallthrough;
		case 'c':
			error = addtype(&types,
					(flags & LONGINT) ? T_WINT : T_INT);
			if (error)
				goto error;
			break;
		case 'D':
			flags |= LONGINT;
			fallthrough;
		case 'd':
		case 'i':
			if ((error = addsarg(&types, flags)))
				goto error;
			break;
#ifndef NO_FLOATING_POINT
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':
			error = addtype(&types,
			    (flags & LONGDBL) ? T_LONG_DOUBLE : T_DOUBLE);
			if (error)
				goto error;
			break;
#endif /* !NO_FLOATING_POINT */
#ifdef DANGEROUS_PERCENT_N
		case 'n':
			if (flags & INTMAXT)
				error = addtype(&types, TP_INTMAXT);
			else if (flags & PTRDIFFT)
				error = addtype(&types, TP_PTRDIFFT);
			else if (flags & SIZET)
				error = addtype(&types, TP_SSIZET);
			else if (flags & LLONGINT)
				error = addtype(&types, TP_LLONG);
			else if (flags & LONGINT)
				error = addtype(&types, TP_LONG);
			else if (flags & SHORTINT)
				error = addtype(&types, TP_SHORT);
			else if (flags & CHARINT)
				error = addtype(&types, TP_SCHAR);
			else
				error = addtype(&types, TP_INT);
			if (error)
				goto error;
			continue;	/* no output */
#endif
		case 'O':
			flags |= LONGINT;
			fallthrough;
		case 'o':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		case 'p':
			if ((error = addtype(&types, TP_VOID)))
				goto error;
			break;
		case 'S':
			flags |= LONGINT;
			fallthrough;
		case 's':
			error = addtype(&types,
			    (flags & LONGINT) ? TP_WCHAR : TP_CHAR);
			if (error)
				goto error;
			break;
		case 'U':
			flags |= LONGINT;
			fallthrough;
		case 'u':
		case 'X':
		case 'x':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		default:	/* "%?" prints ?, unless ? is NUL */
			if (ch == '\0')
				goto done;
			break;
		}
	}
done:
	build_arg_table(&types, ap, argtable);
error:
	freetypes(&types);
	return (error || *argtable == NULL);
}
#endif /* WCHAR_SUPPORT */

/* 【函数】__grow_type_table —— 把类型表容量翻倍（并保证至少能容纳 nextarg+1 项）。
 * 首次扩容从内嵌静态数组 bcopy 到新 malloc 表，之后用 realloc；新增槽位置 T_UNUSED。
 * 返回 0 成功、-1 失败（超过 MAX_POSARG 或内存分配失败）。
 */
/*
 * Increase the size of the type table. Returns 0 on success.
 */
static int
__grow_type_table(struct typetable *types)
{
	enum typeid *const oldtable = types->table;
	const int oldsize = types->tablesize;
	enum typeid *newtable;
	u_int n, newsize;

	/* Detect overflow */
	if (types->nextarg > MAX_POSARG)
		return -1;

	newsize = oldsize * 2;
	if (newsize < types->nextarg + 1)
		newsize = types->nextarg + 1;
	if (oldsize == STATIC_ARG_TBL_SIZE) {
		if ((newtable = malloc(newsize * sizeof(enum typeid))) == NULL)
			return -1;
		bcopy(oldtable, newtable, oldsize * sizeof(enum typeid));
	} else {
		newtable = realloc(oldtable, newsize * sizeof(enum typeid));
		if (newtable == NULL)
			return -1;
	}
	for (n = oldsize; n < newsize; n++)
		newtable[n] = T_UNUSED;

	types->table = newtable;
	types->tablesize = newsize;

	return 0;
}

/* 【函数】build_arg_table —— 依据已建好的类型表，用 va_arg 逐个取出实参填入 union arg
 * 数组：tablemax < STATIC_ARG_TBL_SIZE 时复用调用者传入的静态表，否则 malloc 一块。
 * 注意：T_UNUSED 槽位也会消耗一个 int 参数（“whoops” 兜底）；任意一次 malloc 失败都会
 * 把 *argtable 置 NULL，交由调用方判定出错。
 */
/*
 * Build the argument table from the completed type table.
 * On malloc failure, *argtable is set to NULL.
 */
static void
build_arg_table(struct typetable *types, va_list ap, union arg **argtable)
{
	u_int n;

	if (types->tablemax >= STATIC_ARG_TBL_SIZE) {
		*argtable = (union arg *)
		    malloc (sizeof(union arg) * (types->tablemax + 1));
		if (*argtable == NULL)
			return;
	}

	(*argtable) [0].intarg = 0;
	for (n = 1; n <= types->tablemax; n++) {
		switch (types->table[n]) {
		    case T_UNUSED: /* whoops! */
			(*argtable) [n].intarg = va_arg (ap, int);
			break;
		    case TP_SCHAR:
			(*argtable) [n].pschararg = va_arg (ap, signed char *);
			break;
		    case TP_SHORT:
			(*argtable) [n].pshortarg = va_arg (ap, short *);
			break;
		    case T_INT:
			(*argtable) [n].intarg = va_arg (ap, int);
			break;
		    case T_U_INT:
			(*argtable) [n].uintarg = va_arg (ap, unsigned int);
			break;
		    case TP_INT:
			(*argtable) [n].pintarg = va_arg (ap, int *);
			break;
		    case T_LONG:
			(*argtable) [n].longarg = va_arg (ap, long);
			break;
		    case T_U_LONG:
			(*argtable) [n].ulongarg = va_arg (ap, unsigned long);
			break;
		    case TP_LONG:
			(*argtable) [n].plongarg = va_arg (ap, long *);
			break;
		    case T_LLONG:
			(*argtable) [n].longlongarg = va_arg (ap, long long);
			break;
		    case T_U_LLONG:
			(*argtable) [n].ulonglongarg = va_arg (ap, unsigned long long);
			break;
		    case TP_LLONG:
			(*argtable) [n].plonglongarg = va_arg (ap, long long *);
			break;
		    case T_PTRDIFFT:
			(*argtable) [n].ptrdiffarg = va_arg (ap, ptrdiff_t);
			break;
		    case TP_PTRDIFFT:
			(*argtable) [n].pptrdiffarg = va_arg (ap, ptrdiff_t *);
			break;
		    case T_SIZET:
			(*argtable) [n].sizearg = va_arg (ap, size_t);
			break;
		    case T_SSIZET:
			(*argtable) [n].sizearg = va_arg (ap, ssize_t);
			break;
		    case TP_SSIZET:
			(*argtable) [n].pssizearg = va_arg (ap, ssize_t *);
			break;
		    case T_INTMAXT:
			(*argtable) [n].intmaxarg = va_arg (ap, intmax_t);
			break;
		    case T_UINTMAXT:
			(*argtable) [n].uintmaxarg = va_arg (ap, uintmax_t);
			break;
		    case TP_INTMAXT:
			(*argtable) [n].pintmaxarg = va_arg (ap, intmax_t *);
			break;
		    case T_INT64T:
			(*argtable) [n].intmaxarg = va_arg (ap, int64_t);
			break;
		    case T_UINT64T:
			(*argtable) [n].uintmaxarg = va_arg (ap, uint64_t);
			break;
		    case T_DOUBLE:
#ifndef NO_FLOATING_POINT
			(*argtable) [n].doublearg = va_arg (ap, double);
#endif
			break;
		    case T_LONG_DOUBLE:
#ifndef NO_FLOATING_POINT
			(*argtable) [n].longdoublearg = va_arg (ap, long double);
#endif
			break;
		    case TP_CHAR:
			(*argtable) [n].pchararg = va_arg (ap, char *);
			break;
		    case TP_VOID:
			(*argtable) [n].pvoidarg = va_arg (ap, void *);
			break;
		    case T_WINT:
			(*argtable) [n].wintarg = va_arg (ap, wint_t);
			break;
		    case TP_WCHAR:
			(*argtable) [n].pwchararg = va_arg (ap, wchar_t *);
			break;
		}
	}
}
