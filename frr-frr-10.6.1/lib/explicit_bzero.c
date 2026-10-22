// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Public domain.
 * Written by Matthew Dempsky.
 * Adapted for frr.
 */

/* ============================================================================
 * 【文件导读】explicit_bzero.c —— “清零且不许被优化掉”的兜底实现
 * ============================================================================
 * 【为什么需要它】普通的 memset(buf, 0, len) 语义上只是“写 0”，如果这块内存在
 * 之后没被读过，编译器有权**整个删掉**这次 memset——这对内存安全性无碍，但对
 * 密码/密钥这类敏感数据是致命的：你以为抹掉了，其实还留在栈或堆里。glibc 2.25
 * 起提供了 explicit_bzero()，它的契约是“编译器必须假设这次清零有可观测副作用，
 * 不允许消除”。文件名即来源于此。
 *
 * 【本文件的角色：可移植性垫片(shim)】平台没有 explicit_bzero 时，由这里补上
 * 一个行为等价的实现；系统已有时整个文件被 #ifndef 关掉，什么也不编译。
 * 这是 FRR 大量“兼容旧平台”垫片文件的标准写法。
 *
 * 【实现要诀】关键不是 memset 本身，而是两件事：
 *   ① 用一个 **weak 钩子** __explicit_bzero_hook() 把“清零”这个事实**传递到
 *      函数外部**——编译器无法证明这个外部钩子不会读取 buf，于是不敢删除 memset。
 *      默认钩子是空实现，需要时(如测试或自定义内存校验)可以被强符号覆盖。
 *   ② 对该函数关闭优化(#pragma clang optimize off / GCC optimize("00"))，双保险。
 * 注意 GCC 那一行写的是 optimize("00")，是**字符串 "00"** 而非数字 0；GCC 实际
 * 接受 "0"... 这是上游沿用至今的写法，此处只作标注、不改动。
 *
 * 【阅读顺序】看清 #ifndef HAVE_EXPLICIT_BZERO 的条件编译边界 → 看钩子的 weak
 * 属性 → 看函数体(只有两行，重点是钩子调用不能被优化掉)。
 * ============================================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#ifndef HAVE_EXPLICIT_BZERO
#undef explicit_bzero


void explicit_bzero(void *buf, size_t len);
__attribute__((__weak__)) void
__explicit_bzero_hook(void *buf, size_t len);

/* 【钩子】explicit_bzero 的“副作用锚点”。
 *   buf/len: 与 explicit_bzero 完全相同，直接透传。
 *   返回: 无。
 * 【为什么是 weak】__attribute__((__weak__)) 表示本定义可以被**同名的强符号
 * 覆盖**。默认实现刻意保持为空——它存在的意义不是做事，而是让编译器认为
 * “清零之后还有一个外部函数要跑、它可能读 buf”，从而**无法把 memset 优化掉**。
 * 需要在清零后做校验(例如内存调试工具)时，链接自己的强符号即可替换。
 * 【易踩的坑】不要给这个空函数加 __attribute__((pure)) 之类的属性，也不要让它
 * 被 LTO 内联成空——那会让整个机制失效。上游用 weak + 空体是权衡后的写法。 */
__attribute__((__weak__)) void
__explicit_bzero_hook(void *buf, size_t len)
{
}

#if defined(__clang__)
#pragma clang optimize off
#else
#pragma GCC optimize("00")
#endif

/* 【函数】等价于 glibc 的 explicit_bzero：清零并且**保证不会被优化消除**。
 *   buf/len: 要清零的内存块与长度，语义与 memset 一致。
 *   返回: 无。返回值也没有——这么做是为了让调用方无法“顺手”把它当 memset 用。
 * 【实现顺序很关键】先 memset 后调钩子。若反过来(先钩子后 memset)，编译器仍有
 * 可能判定 memset 结果不可观测而删除它；现在钩子排在后面，等于给 memset 的
 * 结果加了一个“可能被读取”的出口。
 * 【易踩的坑】本函数没有内建的内存对称检查，buf 为 NULL 时与 memset 一样是 UB；
 *   长度传错同样是 UB——它不是安全边界，只是一个防优化工具。 */
void
explicit_bzero(void *buf, size_t len)
{
	memset(buf, 0, len);
	__explicit_bzero_hook(buf, len);
}

#endif
