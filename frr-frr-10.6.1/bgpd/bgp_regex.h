// SPDX-License-Identifier: GPL-2.0-or-later
/* AS regular expression routine
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_regex.h —— AS_PATH 正则匹配的三个接口
 * ============================================================================
 * 【用途】路由策略里最常见的写法之一 `ip as-path access-list 1 permit _65001_`，
 * 就是对 AS_PATH 做正则匹配。这里的“正则”不是 POSIX/Libc 的正则：它匹配的对象
 * 不是字符串，而是 AS 序列，且下划线 `_` 表示“AS 边界”（可匹配开头/结尾/空格）。
 * 真正的实现（含把 aspath 展开成可匹配序列的细节）在 bgp_regex.c。
 *
 * 【生命周期】bgp_regcomp() 成功返回一个已编译的正则结构；必须用
 * bgp_regex_free() 释放（编译结果可能带内部缓存/自动机）。bgp_regexec() 只读，
 * 不改变 regex 与 aspath。
 *
 * 【调用方】bgp_aspath.c 的 aspath_regmatch()/aspath_regexp() 会转发到这里，
 * 最终被 bgp_filter.c 的 as-path access-list 匹配逻辑调用。
 *
 * 【为什么单独建一个头文件】把“正则编译”这个依赖较重的功能隔离出来，使
 * bgp_aspath.c 等模块不必直接依赖底层正则设施。
 * ============================================================================
 */

#ifndef _FRR_BGP_REGEX_H
#define _FRR_BGP_REGEX_H

#include <zebra.h>

struct frregex;

/* 【函数】bgp_regex_free —— 释放 bgp_regcomp 编译出来的正则结构。
 * 【函数】bgp_regcomp —— 把 as-path 正则字符串编译成内部结构；失败返回 NULL
 *        （配置里写错正则时在这里失败，调用方需判空）。
 * 【函数】bgp_regexec —— 用已编译的正则去匹配一个 AS_PATH。返回值沿用
 *        regexec 的惯例：0 表示**匹配成功**，非 0 表示不匹配/出错——注意这与
 *        “返回 true 表示命中”的直觉相反，读条件判断时别搞反。
 * 注意 struct frregex 是**不透明类型**（只前向声明），外部不需要知道其内部构造。 */
extern void bgp_regex_free(struct frregex *regex);
extern struct frregex *bgp_regcomp(const char *str);
extern int bgp_regexec(struct frregex *regex, struct aspath *aspath);

#endif /* _FRR_BGP_REGEX_H */
