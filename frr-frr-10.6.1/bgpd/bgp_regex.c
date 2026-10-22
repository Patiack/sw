// SPDX-License-Identifier: GPL-2.0-or-later
/* AS regular expression routine
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/* ============================================================================
 * 【文件导读】bgp_regex.c —— AS_PATH 正则匹配的实现（POSIX regex 封装 + '_' 魔法）
 * ============================================================================
 * 【本文件解决什么问题】路由策略 `ip as-path access-list` 需要对 AS_PATH 做“正则”
 * 匹配。但配置里写的正则与 POSIX 正则**不完全一样**：下划线 `_` 是一个特殊符号，
 * 代表“AS 边界”，即 `[,{}() ]` 这类分隔符处、以及行首 `^` 与行尾 `$`。
 * 因此本文件先把用户写的正则里的每个 `_` 替换成 `(^|[,{}() ]|$)`，再做真正的
 * POSIX 扩展正则编译。所以它本质是“as-path 正则 -> 真实正则”的翻译层。
 *
 * 【为什么这样设计】直接暴露 POSIX 正则会逼运维写一长串边界组合，易错且难读；
 * 用单个 `_` 表达边界是 Cisco / Juniper 通行的 as-path 正则惯例，FRR 沿用之。
 * 匹配对象是 aspath 的字符串形式 aspath->str（形如 "65001 65002 65003"），
 * 因此在序列分隔处、行首行尾匹配 `_` 就等价于“匹配到某个 AS 的边界”。
 *
 * 【关键数据结构】struct frregex —— 对底层正则库的不透明封装（定义在
 * lib/frregex_real.h），内部含编译好的 regex_t。本文件只用到它的 .real 成员。
 *
 * 【内存归属】bgp_regcomp 成功返回的对象必须由调用方用 bgp_regex_free 释放，
 * 否则泄漏；编译失败返回 NULL（调用方要判空）。中间拼装用的 magic_str 是临时
 * 缓冲（MTYPE_TMP），函数内分配、函数内释放，不逸出。
 *
 * 【★ 反直觉约定】bgp_regexec 直接返回 regexec 的原始结果：**返回 0 表示匹配
 * 成功**，非 0（如 REG_NOMATCH）表示不匹配。这与“true=命中”的直觉相反，读任何
 * 调用点的 if (bgp_regexec(...)) 都要按“非 0 = 不命中”理解，切勿搞反。
 *
 * 【推荐阅读顺序】bgp_regcomp（'_' 展开 + 编译）-> bgp_regexec（匹配）->
 *                bgp_regex_free（释放）。
 * ============================================================================
 */

#include <zebra.h>

#include "log.h"
#include "command.h"
#include "memory.h"
#include "queue.h"
#include "filter.h"
#include "frregex_real.h"

#include "bgpd.h"
#include "bgp_aspath.h"
#include "bgp_regex.h"

/* Character `_' has special mean.  It represents [,{}() ] and the
   beginning of the line(^) and the end of the line ($).

   (^|[,{}() ]|$) */

/* 【函数】bgp_regcomp —— 把一个 as-path 正则字符串编译成 struct frregex。
 * 步骤：
 *   1) 先统计字符串里 '_' 的个数 magic，据此计算展开后所需缓冲大小；
 *   2) 逐个字符复制到 magic_str，遇到 '_' 就写入边界表达式 magic_regexp
 *      "(^|[,{}() ]|$)"（共 14 字符），其它字符原样拷贝；
 *   3) 用 regcomp(..., REG_EXTENDED | REG_NOSUB) 编译；REG_NOSUB 表示不关心
 *     匹配位置，只关心是否匹配；
 *   4) 编译失败则释放并返回 NULL。
 * 辅助缓冲 magic_str 在返回前即释放。
 * 返回值：成功返回已编译对象（调用方负责 bgp_regex_free），失败返回 NULL。 */
struct frregex *bgp_regcomp(const char *regstr)
{
	/* Convert _ character to generic regular expression. */
	int i, j;
	int len;
	int magic = 0;
	char *magic_str;
	char magic_regexp[] = "(^|[,{}() ]|$)";
	int ret;
	struct frregex *regex;

	len = strlen(regstr);
	for (i = 0; i < len; i++)
		if (regstr[i] == '_')
			magic++;

	magic_str = XMALLOC(MTYPE_TMP, len + (14 * magic) + 1);

	for (i = 0, j = 0; i < len; i++) {
		if (regstr[i] == '_') {
			memcpy(magic_str + j, magic_regexp,
			       strlen(magic_regexp));
			j += strlen(magic_regexp);
		} else
			magic_str[j++] = regstr[i];
	}
	magic_str[j] = '\0';

	regex = XMALLOC(MTYPE_BGP_REGEXP, sizeof(*regex));

	ret = regcomp(&regex->real, magic_str, REG_EXTENDED | REG_NOSUB);

	XFREE(MTYPE_TMP, magic_str);

	if (ret != 0) {
		XFREE(MTYPE_BGP_REGEXP, regex);
		return NULL;
	}

	return regex;
}

/* 【函数】bgp_regexec —— 用已编译正则匹配一个 AS_PATH。
 * 匹配对象是 aspath->str（AS_PATH 的字符串形式，如 "65001 65002"）。flags 传 0
 * 沿用 regcomp 时既定的选项；后两个参数（nmatch/pmatch）传 0/NULL 表示
 * 只关心“是否匹配”，不需要捕获子串位置。
 * ★ 返回值沿用 regexec 原始语义：0 = 匹配成功，REG_NOMATCH(非 0) = 不匹配。
 *   调用方若写成 if，务必记住“非 0 是不命中”。 */
int bgp_regexec(struct frregex *regex, struct aspath *aspath)
{
	return regexec(&regex->real, aspath->str, 0, NULL, 0);
}

/* 【函数】bgp_regex_free —— 释放 bgp_regcomp 返回的对象。
 * 先用 regfree 释放正则库内部资源，再释放 frregex 结构体本身。与 bgp_regcomp 成对。 */
void bgp_regex_free(struct frregex *regex)
{
	regfree(&regex->real);
	XFREE(MTYPE_BGP_REGEXP, regex);
}
