// SPDX-License-Identifier: LGPL-2.1-or-later
/* Append a null-terminated string to another string, with length checking.
 * Copyright (C) 2016 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 */

/* adapted for Quagga from glibc patch submission originally from
 * Florian Weimer <fweimer@redhat.com>, 2016-05-18 */

/* ============================================================================
 * 【文件导读】strlcat.c —— 有界字符串拼接的兜底实现(glibc 垫片)
 * ============================================================================
 * 【它和 strcat / strncat 的区别】strcat 不接收目标容量，必然写穿；strncat 的
 * 第三个参数是“**从源串再拷多少字节**”而不是目标剩余容量，语义反直觉且不保证
 * 结尾 NUL。strlcat 反过来：第三个参数 destsize 是**目标缓冲区的总容量**，
 * 返回的是“本应拼接出的完整长度”，据此判断是否截断。
 *
 * 【返回值到底怎么算(最容易记错)】返回 `dest_length + src_length`：
 *   dest_length : 拼接前 dest 里已有字符串的长度(用 strnlen 在 destsize 内探测)；
 *   src_length  : 源串完整长度。
 * 于是 `ret >= destsize` 表示“放不下、被截断了”。
 * 【边界情形】destsize == 0 时直接返回 src_length —— 这是**故意**的兼容行为，
 *   让 dest==NULL 且 size==0 也能安全调用(与 snprintf / 本文件的 strlcpy 对齐)。
 *   正因为如此，下面不能用 strnlen(dest, 0) 来算长度，必须特判提前返回。
 * 【dest 已经没结尾 NUL 时怎么办】strnlen(dest, destsize) 会返回 destsize，
 *   此时 `dest_length != destsize` 为假，整个拷贝分支被跳过，**只返回长度**——
 *   即“目标已满，什么都不写”。这是有意为之而非漏写。
 *
 * 【阅读顺序】先记住返回值 = dest_length + src_length → 再看 destsize==0 的特判
 * → 再看拷贝分支(留下的空间 = destsize - dest_length - 1，那个 -1 是给 NUL 的)
 * → 最后看 _Static_assert 在论证什么。
 * ============================================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <string.h>

#ifndef HAVE_STRLCAT
#undef strlcat

size_t strlcat(char *__restrict dest,
	       const char *__restrict src, size_t destsize);

/* 【函数】把 src 追加到 dest 末尾，最多写入到 destsize 容量为止。
 *   dest    : 目标缓冲区；在 destsize 内**必须**能找到一个 NUL 才是正常已拼接
 *             状态，否则视为“已满，不再写入”(见文件导读)。
 *   src     : 源串，以 NUL 结尾。
 *   destsize: dest 的总容量(**含**结尾 NUL)。
 *   返回    : dest_length + src_length，可用来判截断(`ret >= destsize`)。
 * 【要点】拷贝长度同时受两个上界约束：目标剩余空间(destsize-dest_length-1)与
 *   源串实际长度，取更小者；拷完手工补 NUL。这与 strncpy 的“补零填充”行为不同，
 *   本函数**只写一个** NUL。
 * 【易踩的坑】返回的是“应有的总长”，不是写入字节数；截断后 dest 是合法 C 字符串
 *   但内容不完整，调用方不检查返回值就会静默丢数据。 */
size_t strlcat(char *__restrict dest,
	       const char *__restrict src, size_t destsize)
{
	size_t src_length = strlen(src);

	/* Our implementation strlcat supports dest == NULL if size == 0
	   (for consistency with snprintf and strlcpy), but strnlen does
	   not, so we have to cover this case explicitly.  */
	if (destsize == 0)
		return src_length;

	size_t dest_length = strnlen(dest, destsize);
	if (dest_length != destsize) {
		/* Copy at most the remaining number of characters in the
		   destination buffer.  Leave for the NUL terminator.  */
		size_t to_copy = destsize - dest_length - 1;
		/* But not more than what is available in the source string.  */
		if (to_copy > src_length)
			to_copy = src_length;

		char *target = dest + dest_length;
		memcpy(target, src, to_copy);
		target[to_copy] = '\0';
	}

/* If the sum wraps around, we have more than SIZE_MAX + 2 bytes in
   the two input strings (including both null terminators).  If each
   byte in the address space can be assigned a unique size_t value
   (which the static_assert checks), then by the pigeonhole
   principle, the two input strings must overlap, which is
   undefined.  */
	/* 【编译期断言】论证“返回值相加不会因整数回绕而给出错误结论”。
	 * 若 sizeof(uintptr_t) != sizeof(size_t)，说明地址空间里不是每个字节都能
	 * 对应唯一的 size_t 值，那么 dest_length + src_length 的回绕就可能被误解，
	 * 上面的推理前提(两串必然重叠)就不成立。编译期把它钉死，属于“用类型系统
	 * 记录假设”的写法——它不是运行时检查，只是把你的假设写成编译错误。 */
	_Static_assert(sizeof(uintptr_t) == sizeof(size_t),
		       "theoretical maximum object size covers address space");
	return dest_length + src_length;
}
#endif /* HAVE_STRLCAT */
