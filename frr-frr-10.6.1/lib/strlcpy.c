// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copy a null-terminated string to a fixed-size buffer, with length checking.
 * Copyright (C) 2016 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 */

/* adapted for Quagga from glibc patch submission originally from
 * Florian Weimer <fweimer@redhat.com>, 2016-05-18 */

/* ============================================================================
 * 【文件导读】strlcpy.c —— 有界字符串拷贝的兜底实现(glibc 垫片)
 * ============================================================================
 * 【为什么不用 strcpy】strcpy 不接收目标长度，源串一长就写穿缓冲区，是经典漏洞
 * 来源。strlcpy 的思路是“**拷贝有上限，但返回值告诉你有多少**”：
 *   - 目标缓冲区大小 destsize 由调用方给出，最多写入 destsize 字节(含结尾 NUL)；
 *   - 返回**源串完整长度 strlen(src)**(不含 NUL)，即使发生了截断也照样返回真值。
 * 于是调用方能拿返回值与 destsize 比较来判断“是不是被截断了”，这是它比
 * strncpy 好的核心：strncpy 既不保证 NUL 结尾，也不告诉你截断了多少。
 *
 * 【本文件的角色】strlcpy 是 BSD 系(long 在 BSD libc)的接口，glibc **不提供**。
 * 配置阶段若检测不到 HAVE_STRLCPY，就编译本文件补上；系统已有时整个文件被
 * #ifndef 关掉。代码来自 glibc 的补丁提交，作者 Florian Weimer。
 *
 * 【实现里唯一需要理解的技巧(截断分支)】当源串放不下时，它**先 memcpy 整整
 * destsize 字节**(可能把源串的 NUL 也拷进来)，再手工把最后一个字节改成 '\0'。
 * 这么写不是笔误：目标长度通常是 2 的小幂次(内存对齐友好)，一次按固定长度拷贝
 * 比“拷 destsize-1 个字节”更容易被编译成高效的 mov 序列，末尾那一次重复写
 * 反而不亏。理解这点，那两行注释就不再突兀。
 *
 * 【阅读顺序】先看开头条件编译与前置声明 → 再看返回值的定义(返回源串长度，
 * 不是写入长度) → 最后看截断分支为何多写一次。
 * ============================================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#ifndef HAVE_STRLCPY
#undef strlcpy

size_t strlcpy(char *__restrict dest,
	       const char *__restrict src, size_t destsize);

/* 【函数】把 src 拷贝到 dest，最多写 destsize 字节(含结尾 NUL)。
 *   dest    : 目标缓冲区，须至少可写 destsize 字节(destsize==0 时可为无效指针)。
 *   src     : 源串，必须以 NUL 结尾(本函数靠 strlen 找结尾)。
 *   destsize: 目标缓冲区容量，**包含**结尾 NUL 的位置。
 *   返回    : **源串的完整长度** strlen(src)，与是否截断无关。
 * 【怎么判截断】调用方用 `ret >= destsize` 判断发生了截断(destsize>0 时)。
 *   注意返回值**不是**实际写入的字节数——这是与 strncpy/snprintf 最大的区别，
 *   也是初学最容易用错的地方。
 * 【实现要点】开头 __builtin_expect(..., 0) 只是分支预测提示：normal 情况是
 *   “放得下”，所以把“放不下”标注为 unlikely。它不影响语义，删掉结果一样。
 * 【易踩的坑】① dest 与 src 重叠时行为未定义(没有 memmove 语义)；② src 不是
 *   以 NUL 结尾时会一直读到越界(本函数不做长度上限保护，只做写入上限保护)。 */
size_t strlcpy(char *__restrict dest,
	       const char *__restrict src, size_t destsize)
{
	size_t src_length = strlen(src);

	if (__builtin_expect(src_length >= destsize, 0)) {
		if (destsize > 0) {
			/*
			 * Copy the leading portion of the string.  The last
			 * character is subsequently overwritten with the NUL
			 * terminator, but the destination destsize is usually
			 * a multiple of a small power of two, so writing it
			 * twice should be more efficient than copying an odd
			 * number of bytes.
			 */
			memcpy(dest, src, destsize);
			dest[destsize - 1] = '\0';
		}
	} else
		/* Copy the string and its terminating NUL character.  */
		memcpy(dest, src, src_length + 1);
	return src_length;
}
#endif /* HAVE_STRLCPY */
