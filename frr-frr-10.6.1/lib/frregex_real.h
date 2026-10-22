// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2025  David Lamparter, for NetDEF, Inc.
 *
 * The point of this is to "hide" from ABI/API which specific regex library is
 * used.  Otherwise some structs end up with config-dependent layouts, which
 * is a pain.
 */

#ifndef _FRR_FRREGEX_REAL_H
#define _FRR_FRREGEX_REAL_H

/* ============================================================================
 * 【文件导读】frregex_real.h —— 把“实际用哪个正则库”这件事藏起来
 * ============================================================================
 * 【要解决的麻烦(原注释说得很清楚)】FRR 可链接不同的 POSIX 正则实现：
 * PCRE2 的 POSIX 兼容层 / 老 PCRE 的兼容层 / 系统自带 regex.h。三者虽然都提供
 * 名字叫 regex_t 的类型，**但它是不同库里的不同结构体**(大小、布局都可能不同)。
 * 一旦某个公开结构体(如 filter / as-path regex)直接内嵌 regex_t，它的**内存
 * 布局就随构建配置而变**——这就是原注释说的 config-dependent layouts，对 ABI
 * 以及跨模块传结构体是灾难。
 *
 * 【本文件的解法】统一包装成 `struct frregex { regex_t real; }`，让上层只认
 * frregex。这样：“用哪个库”的知识**只存在于本文件的条件编译里**，其余代码不再
 * 关心。注意它其实**没有消除**布局依赖——frregex 内部仍内嵌 regex_t——真正的
 * 收益是把这个依赖**收敛到一处**，将来要彻底抽象只需改这里。
 *
 * 【三档优先级】按顺序探测，先 PCRE2、再 PCRE、最后回退系统 regex.h。
 *   注意 `#ifndef _FRR_PCRE2_POSIX / #define _FRR_PCRE2_POSIX` 这对哨兵：pcre2
 *   的 posix 头文件自己也会带同名保护宏，先定义它可避免重复包含时的奇怪现象。
 * 【易踩的坑】最后那个 #endif 的尾注写的是条件宏名 HAVE_LIBPCRE2_POSIX，它确实
 *   与对应的 #if 匹配；但读条件编译块时请**以代码缩进结构为准**，不要仅凭尾注
 *   文字去判断配对了哪个分支。
 * ============================================================================
 */
#ifdef HAVE_LIBPCRE2_POSIX
#ifndef _FRR_PCRE2_POSIX
#define _FRR_PCRE2_POSIX
#include <pcre2posix.h>
#endif /* _FRR_PCRE2_POSIX */
#elif defined(HAVE_LIBPCREPOSIX)
#include <pcreposix.h>
#else
#include <regex.h>
#endif /* HAVE_LIBPCRE2_POSIX */

/* 【结构体】对“具体正则库”的一层薄封装。
 *   real: 真正的正则编译结果(类型随构建配置变化)。
 * 【为什么要包一层而不是 typedef】typedef 只是换名字，无法在上面再加字段或改
 * 语义；用结构体则保留了将来扩展(如加引用计数、加原始模式串)的余地，也让
 * “这是 FRR 的正则句柄”在代码里可辨认。
 * 【易踩的坑】本结构体**按值可拷贝**，但 POSIX 正则句柄内部可能持有堆内存，
 * 拷贝后两份会共享内部指针；实践上应通过指针传递，不要直接赋值一份新的。 */
struct frregex {
	regex_t real;
};

#endif /* _FRR_FRREGEX_REAL_H */
