// SPDX-License-Identifier: ISC
/*
 * FRR switchable defaults.
 * Copyright (c) 2017-2019  David Lamparter, for NetDEF, Inc.
 */

// ============================================================
// 【文件导读】defaults.c
// ------------------------------------------------------------
// 职责：
//   "可切换默认值"机制的实现。维护一张全局默认值链表，并在"当前
//   版本 + 当前 profile"变化时，为每个默认值在它的规则表里找出
//   第一个命中的取值，分别写进 DFLT_xxx(运行时用) 与 SAVE_xxx
//   (写盘比较用)变量。defaults.h 讲数据结构与宏，本文件讲求值算法。
//
// 核心状态：
//   - df_version: 当前生效版本串(默认取编译期 FRR_VER_SHORT)；
//   - df_profile: 当前 profile(默认 DFLT_NAME，通常 traditional)；
//   - dflt_first / dflt_next: 默认值单链表的头指针与"尾指针的指针"。
//
// 关键函数：
//   - version_value / frr_version_cmp: 版本串比较(数字段按数值、
//     '~' 特判、字母/标点分级)；
//   - frr_match_version: 解析 "<= 1.2" 这类表达式再比较；
//   - frr_default_apply_one: 对单个默认值分别求 dflt 命中与 save 命中；
//   - frr_defaults_apply / *_set: 触发全体重算。
//
// 与其他模块的关系：
//   - defaults.h: 数据结构与声明宏；各 daemon 用宏声明默认值；
//   - version.h: 提供 FRR_VER_SHORT / DFLT_NAME；
//   - 宏生成的 _CONSTRUCTOR(1000) 构造函数会在启动时自动调用
//     frr_default_add() 完成注册。
//
// 阅读顺序：全局状态 -> version_value -> frr_version_cmp ->
//   frr_default_add -> frr_match_version -> frr_default_apply_one ->
//   frr_defaults_apply -> 各 set/get 小函数。
//
// 易踩的坑：
//   - 规则表以"match_version 与 match_profile 都为 NULL"的条目收尾；
//   - save 一侧固定用当前编译版本 FRR_VER_SHORT 求值；
//   - check 参数控制是否打印"无效版本表达式"告警(注册时 true、
//     重算时 false)，避免反复刷屏。
// ============================================================

#include <zebra.h>

// defaults.h：本机制的数据结构与宏。
// lib/version.h：提供 FRR_VER_SHORT(编译版本)、DFLT_NAME(默认 profile)。
#include "defaults.h"
#include "lib/version.h"

// df_version: 当前"生效版本"串，初值为编译期版本 FRR_VER_SHORT。
//   加载配置时若遇到 "frr version X" 会被 *_version_set 改写。
// df_profile: 当前 profile 名，初值为编译期默认 DFLT_NAME。
static char df_version[128] = FRR_VER_SHORT, df_profile[128] = DFLT_NAME;
// dflt_first: 全局默认值单链表头。
// dflt_next: 指向"链表尾节点 next 字段"的指针，便于 O(1) 尾插。
static struct frr_default *dflt_first = NULL, **dflt_next = &dflt_first;

/* these are global for all FRR daemons.  they have to be, since we write an
 * integrated config with the same value for all daemons.
 */
// frr_defaults_profiles: 内置 profile 名列表，以 NULL 结尾。
//   traditional(传统) 与 datacenter(数据中心)两套预设；
//   它是"全局"的，因为 FRR 写的是所有 daemon 共用同一份值的集成配置。
const char *frr_defaults_profiles[] = {
	"traditional",
	"datacenter",
	NULL,
};

// version_value: 把一个版本串字符映射成可比较的"权重"，用于
//   frr_version_cmp 的字符级比较。分级规则(数值越小越"老")：
//     '~'        -> 0      (预发布后缀，排在最前)
//     '\0'(结尾) -> 1      (字符串结束，排在 '~' 之后)
//     字母       -> 0x100 + tolower(字母)  (按字母序)
//     其它       -> 0x200 + ch             (标点/数字等按 ASCII)
//   非 ASCII(>=128 或负值)返回 2，理论上不该出现。
static int version_value(int ch)
{
	/* non-ASCII shouldn't happen */
	if (ch < 0 || ch >= 128)
		return 2;

	/* ~foo sorts older than nothing */
	if (ch == '~')
		return 0;
	if (ch == '\0')
		return 1;
	if (isalpha(ch))
		return 0x100 + tolower(ch);

	/* punctuation and digits (and everything else) */
	return 0x200 + ch;
}

// frr_version_cmp: 按 FRR 版本语义比较 aa 与 bb，返回 <0/0/>0。
//   策略：
//     - 当两侧当前位置都是数字时，用 strtoul 取整段数值比较(避免
//       "10" < "9" 的字符串陷阱)，相同则继续；
//     - 否则用 version_value 逐字符比较；
//     - 用 while (apos[0] || bpos[0]) 作为循环条件，':' 注释见函数内。
int frr_version_cmp(const char *aa, const char *bb)
{
	const char *apos = aa, *bpos = bb;

	/* || is correct, we won't scan past the end of a string since that
	 * doesn't compare equal to anything else */
	while (apos[0] || bpos[0]) {
		// 两侧当前都处于数字段：按整段数值比较，
		// 而非逐字符，避免 "10" 被误判小于 "9"。
		if (isdigit((unsigned char)apos[0]) &&
		    isdigit((unsigned char)bpos[0])) {
			unsigned long av, bv;
			char *aend = NULL, *bend = NULL;

			av = strtoul(apos, &aend, 10);
			bv = strtoul(bpos, &bend, 10);
			if (av < bv)
				return -1;
			if (av > bv)
				return 1;

			apos = aend;
			bpos = bend;
			continue;
		}

		int a = version_value(*apos++);
		int b = version_value(*bpos++);

		if (a < b)
			return -1;
		if (a > b)
			return 1;
	}
	return 0;
}

// 前置声明：frr_default_add 里会用到 apply_one，而它定义在下方。
static void frr_default_apply_one(struct frr_default *dflt, bool check);

// frr_default_add: 把一个默认值挂到链表尾，并立即求值一次
//   (check=true，即允许打印无效版本表达式告警)。由宏生成的构造
//   函数在程序启动期调用，daemon 一般无需手动调。
void frr_default_add(struct frr_default *dflt)
{
	dflt->next = NULL;
	*dflt_next = dflt;
	dflt_next = &dflt->next;

	frr_default_apply_one(dflt, true);
}

// frr_match_version: 判断版本 version 是否满足表达式 vspec。
//   - vspec 为 NULL 表示"匹配所有版本"；
//   - specs[] 列出支持的比较符(<=,>=,==,<,>,=)，逐个用 strncmp 试
//     前缀；匹配到哪个就把 vspec 指针后移跳过操作符与空白；
//   - 表达式非法时不命中(并在 check 时打印告警)；
//   - 用 frr_version_cmp 得到 cmp，再按操作符方向(dir/eq)判定。
//   name 仅用于告警时显示是哪个默认值出了错。
static bool frr_match_version(const char *name, const char *vspec,
			      const char *version, bool check)
{
	int cmp;
	static const struct spec {
		const char *str;
		int dir, eq;
	} specs[] = {
		{"<=", -1, 1},
		{">=", 1, 1},
		{"==", 0, 1},
		{"<", -1, 0},
		{">", 1, 0},
		{"=", 0, 1},
		{NULL, 0, 0},
	};
	const struct spec *s;

	// vspec 为空指针 => 不限版本，直接命中。
	if (!vspec)
		/* NULL = all versions */
		return true;

	for (s = specs; s->str; s++)
		if (!strncmp(s->str, vspec, strlen(s->str)))
			break;
	if (!s->str) {
		if (check)
			fprintf(stderr, "invalid version specifier for %s: %s",
				name, vspec);
		/* invalid version spec, never matches */
		return false;
	}

	vspec += strlen(s->str);
	while (isspace((unsigned char)*vspec))
		vspec++;

	// 用版本语义比较 version 与表达式中的目标版本，
	// 再依据操作符方向(dir)或相等(eq)判定是否命中。
	cmp = frr_version_cmp(version, vspec);
	if (cmp == s->dir || (s->eq && cmp == 0))
		return true;

	return false;
}

// frr_default_apply_one: 对单个默认值 dflt 求值。
//   遍历其规则表 entries(以 match_version 与 match_profile 都为 NULL
//   的条目作为哨兵结尾)：
//     - profile 不符的条目跳过；
//     - dfltentry: 第一条同时满足(当前生效版本 df_version 与 profile)
//       的规则，用于填 DFLT_xxx；
//     - saveentry: 第一条同时满足(当前编译版本 FRR_VER_SHORT 与
//       profile)的规则，用于填 SAVE_xxx；
//     - check 为假时一旦两者都找到即提前退出;
//   若某侧始终没找到(只有 NULL,NULL 哨兵能匹配)，则回退到哨兵条目。
//   最后按各自类型把命中项的值写入 dflt_* / save_* 指向的变量。
static void frr_default_apply_one(struct frr_default *dflt, bool check)
{
	struct frr_default_entry *entry = dflt->entries;
	struct frr_default_entry *dfltentry = NULL, *saveentry = NULL;

	// 逐条扫描规则；当 match_version 与 match_profile 都为 NULL
	// 时(哨兵条目)循环终止。entry 停在哨兵上，供下方兜底使用。
	for (; entry->match_version || entry->match_profile; entry++) {
		if (entry->match_profile
			&& strcmp(entry->match_profile, df_profile))
			continue;

		if (!dfltentry && frr_match_version(dflt->name,
				entry->match_version, df_version, check))
			dfltentry = entry;
		if (!saveentry && frr_match_version(dflt->name,
				entry->match_version, FRR_VER_SHORT, check))
			saveentry = entry;

		// 非"校验"模式(重算)下，两侧都已确定命中即可提前收工；
		// 校验模式(注册)则继续扫描，以便发现更多非法表达式并告警。
		if (dfltentry && saveentry && !check)
			break;
	}
	/* found default or arrived at last entry that has NULL,NULL spec */

	if (!dfltentry)
		dfltentry = entry;
	if (!saveentry)
		saveentry = entry;

	if (dflt->dflt_bool)
		*dflt->dflt_bool = dfltentry->val_bool;
	if (dflt->dflt_str)
		*dflt->dflt_str = dfltentry->val_str;
	if (dflt->dflt_long)
		*dflt->dflt_long = dfltentry->val_long;
	if (dflt->dflt_ulong)
		*dflt->dflt_ulong = dfltentry->val_ulong;
	if (dflt->dflt_float)
		*dflt->dflt_float = dfltentry->val_float;
	if (dflt->save_bool)
		*dflt->save_bool = saveentry->val_bool;
	if (dflt->save_str)
		*dflt->save_str = saveentry->val_str;
	if (dflt->save_long)
		*dflt->save_long = saveentry->val_long;
	if (dflt->save_ulong)
		*dflt->save_ulong = saveentry->val_ulong;
	if (dflt->save_float)
		*dflt->save_float = saveentry->val_float;
}

// frr_defaults_apply: 用当前 df_version / df_profile 重新求值链表上
//   的每一个默认值(check=false，不重复告警)。版本或 profile 变化后
//   会被 *_set 调用。
void frr_defaults_apply(void)
{
	struct frr_default *dflt;

	for (dflt = dflt_first; dflt; dflt = dflt->next)
		frr_default_apply_one(dflt, false);
}

// frr_defaults_profile_valid: 判断 profile 名是否在
//   frr_defaults_profiles[] 内置列表中。
bool frr_defaults_profile_valid(const char *profile)
{
	const char **p;

	for (p = frr_defaults_profiles; *p; p++)
		if (!strcmp(profile, *p))
			return true;
	return false;
}

// frr_defaults_version: 返回当前生效版本串(df_version)。
const char *frr_defaults_version(void)
{
	return df_version;
}

// frr_defaults_profile: 返回当前 profile 名(df_profile)。
const char *frr_defaults_profile(void)
{
	return df_profile;
}

// frr_defaults_version_set: 设置生效版本(边界安全拷贝)，随后立即
//   触发全体默认值重算。
void frr_defaults_version_set(const char *version)
{
	strlcpy(df_version, version, sizeof(df_version));
	frr_defaults_apply();
}

// frr_defaults_profile_set: 设置当前 profile(边界安全拷贝)，随后
//   立即触发全体默认值重算。
void frr_defaults_profile_set(const char *profile)
{
	strlcpy(df_profile, profile, sizeof(df_profile));
	frr_defaults_apply();
}
