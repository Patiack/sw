// SPDX-License-Identifier: ISC
/*
 * FRR switchable defaults.
 * Copyright (C) 2017-2019  David Lamparter for NetDEF, Inc.
 */

// ============================================================
// 【文件导读】defaults.h
// ------------------------------------------------------------
// 职责：
//   定义 FRR 的"可切换默认值"(switchable defaults)机制。FRR 里
//   很多默认值并非写死常量，而是随"FRR 版本"或"默认 profile"变化：
//     - 版本维度：加载的配置里写着 "frr version 5.0"，则应使用 5.0
//       的默认值，而不是当前二进制的默认值；
//     - profile 维度：内置 traditional(传统) 与 datacenter(数据中心)
//       两套 profile，同一开关在两者下默认值可能不同。
//   本头文件给出描述规则的 struct 与一组声明默认值的宏，真正的
//   求值逻辑在 defaults.c。
//
// 核心数据结构：
//   - struct frr_default_entry：一条"匹配规则 + 结果值"。规则含
//     可选的版本表达式与可选 profile 名；按数组顺序求值，取第一个
//     命中项。NULL 表示"匹配一切"。
//   - struct frr_default：一个"可切换默认值"对应一个实例，通过
//     一级单链表串起来；含 dflt_* 与 save_* 两组指针。
//
// 关键概念(dflt 与 save)：
//   - dflt_xxx：读配置 / 运行时使用的默认值，可随 "frr version"
//     改变；
//   - save_xxx：生成 running-config 时用来"比较是否等于默认"的值，
//     永远针对*当前*编译版本。
//   这样把老版本配置加载到新版本上时，如果某值恰好等于"老版本默认"
//   但不同于"新版本默认"，也能被正确识别为默认而不被多余写出；而
//   写盘始终以当前版本默认值判断，保证输出干净。
//
// 与其他模块的关系：
//   - defaults.c：实现版本比较、规则求值、profile 校验；
//   - 各 daemon：用 FRR_CFG_DEFAULT_* 宏声明自己的默认值；
//   - version.h：提供编译期版本串 FRR_VER_SHORT / DFLT_NAME。
//
// 阅读顺序：struct frr_default_entry -> struct frr_default ->
//   _FRR_CFG_DEFAULT 宏 -> FRR_CFG_DEFAULT_* 家族 -> 导出函数。
//
// 易踩的坑：
//   - 匹配规则"第一个命中即止"，顺序敏感；
//   - 版本表达式里 NULL 表示匹配所有版本；
//   - 宏的参数 varname 不能用 #define 常量(会被预处理展开而出错)，
//     需要时用 enum 或在名字前加下划线规避。
// ============================================================

#ifndef _FRR_DEFAULTS_H
#define _FRR_DEFAULTS_H

// stdbool.h：bool / true / false。
// compiler.h：编译期辅助宏(如 _CONSTRUCTOR、MACRO_REQUIRE_SEMICOLON)。
#include <stdbool.h>

#include "compiler.h"

// 让 C++ 编译器按 C 链接规则处理本头文件里的符号(避免 name mangling)。
#ifdef __cplusplus
extern "C" {
#endif

/* frr_default wraps information about a default that has different
 * values depending on FRR version or default-set
 *
 * frr_default_entry describes one match rule and the resulting value;
 * entries are evaluated in order and the first matching is used.
 *
 * If both match_version and match_profile are specified, they must both
 * match.  A NULL value matches everything.
 */
// struct frr_default_entry: "规则 -> 值"的单条映射。
//   一条默认值可由多条 entry 组成，求值时按数组顺序扫描，取第一个
//   同时满足 match_version 与 match_profile 的项；两者都为 NULL 时
//   匹配一切(通常作为兜底的最后一条)。
struct frr_default_entry {
	/* syntax: "(<|<=|==|>=|>) [whitespace] version", e.g.
	 *   ">= 6.1-dev" "<6.0"
	 */
	// match_version: 版本匹配表达式，形如 ">= 6.1-dev" 或 "<6.0"。
	//   为 NULL 表示不限版本(匹配所有)。
	const char *match_version;
	/* exact profile string to compare against */
	// match_profile: 要精确匹配的 profile 名(如 "datacenter")；
	//   为 NULL 表示不限 profile。若与 match_version 同时给出，则
	//   二者都要满足才算命中。
	const char *match_profile;

	/* value to use */
	// 下面 5 个字段是"命中后要写入的值"，按默认值类型选用其一：
	// val_bool 布尔、val_str 字符串、val_long/val_ulong 整型、
	// val_float 浮点。求值时节点的 dflt_* / save_* 只会用到匹配的类型。
	bool val_bool;
	const char *val_str;
	long val_long;
	unsigned long val_ulong;
	float val_float;
};

/* one struct frr_default exists for each malleable default value */
// struct frr_default: 一个"可切换默认值"的完整描述。
//   每个被 FRR_CFG_DEFAULT_* 声明的默认值对应一个实例，由 libfrr
//   在构造函数期注册进全局链表(见 defaults.c 的 dflt_first)。
struct frr_default {
	// next: 全局单链表指针，把所有已登记的默认值串起来。
	struct frr_default *next;

	/* for UI/debug use */
	// name: 人类可读的名字(即宏传入的 varname)，仅用于界面/调试。
	const char *name;

	/* the following two sets of variables differ because the written
	 * config always targets the *current* FRR version
	 *
	 * e.g. if you load a config that has "frr version 5.0" on 6.0
	 *   *dflt_long => set to the default value in 5.0
	 *   *save_long => set to the default value in 6.0
	 * config save will write "frr version 6.0" with 6.0 defaults
	 */

	/* variable holding the default value for reading/use */
	// dflt_*: 指向"读配置/运行时默认值"变量的指针(由宏生成的
	//   DFLT_xxx 提供)。求值后这里指向的变量会被填成命中的默认值，
	//   业务代码直接读取该变量即可。5 个指针按类型选用其一。
	bool *dflt_bool;
	const char **dflt_str;
	long *dflt_long;
	unsigned long *dflt_ulong;
	float *dflt_float;

	/* variable to use when comparing for config save */
	// save_*: 指向"生成 running-config 时用于比较的默认值"变量的
	//   指针(由宏生成的 SAVE_xxx 提供)。它永远按当前编译版本求值，
	//   因此写盘时能判断某行是否与*当前*默认相同，从而省去不必要
	//   的输出。5 个指针按类型选用其一。
	bool *save_bool;
	const char **save_str;
	long *save_long;
	unsigned long *save_ulong;
	float *save_float;

	// entries: 柔性数组成员，规则表。以 ".match_version/.match_profile
	//   均为 NULL" 的条目作为结尾标记(见 defaults.c 的扫描循环)。
	struct frr_default_entry entries[];
};

// _FRR_CFG_DEFAULT: FRR_CFG_DEFAULT_* 家族的公共实现。
//   为名为 varname 的默认值生成(build):
//     1) 静态变量 DFLT_varname / SAVE_varname(分别存"运行时默认值"
//        与"写盘比较默认值")；
//     2) 静态 struct frr_default 实例 _dflt_varname，其 .entries 即
//        传入的 __VA_ARGS__ 规则表，dflt_* / save_* 指向上面的变量；
//     3) 一个带 _CONSTRUCTOR(1000) 属性的构造期函数，在程序启动时
//        调用 frr_default_add() 把该默认值注册进全局链表。
//   typname 是类型在字段名里的后缀(bool/long/ulong/float/str)。
#define _FRR_CFG_DEFAULT(type, typname, varname, ...) \
	static type DFLT_##varname;                                            \
	static type SAVE_##varname;                                            \
	static struct frr_default _dflt_##varname = {                          \
		.name = #varname,                                              \
		.dflt_##typname = &DFLT_##varname,                             \
		.save_##typname = &SAVE_##varname,                             \
		.entries = { __VA_ARGS__ },                                    \
	};                                                                     \
	static void _dfltinit_##varname(void)                                  \
		__attribute__((_CONSTRUCTOR(1000)));                           \
	static void _dfltinit_##varname(void)                                  \
	{                                                                      \
		frr_default_add(&_dflt_##varname);                             \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON() /* end */

/* use:
 *   FRR_CFG_DEFAULT_LONG(SHARP_BLUNTNESS,
 *	{ .val_long = 2, .match_version = ">= 10.0" },
 *	{ .val_long = 1, .match_profile = "datacenter" },
 *	{ .val_long = 0 },
 *   )
 *
 * This will create DFLT_SHARP_BLUNTNESS and SAVE_SHARP_BLUNTNESS variables.
 *
 * Note: preprocessor defines cannot be used as variable names because they
 * will be expanded and blow up with a compile error.  Use an enum or add an
 * extra _ at the beginning (e.g. _SHARP_BLUNTNESS => DFLT__SHARP_BLUNTNESS)
 */
// FRR_CFG_DEFAULT_* 家族：按类型声明一个可切换默认值，用法见上方示例。
//   BOOL  -> DFLT_/SAVE_ 为 bool
//   LONG  -> long
//   ULONG -> unsigned long
//   FLOAT -> float
//   STR   -> const char *
// 每个宏都会额外生成构造期注册代码，因此只能在文件作用域使用。
#define FRR_CFG_DEFAULT_BOOL(varname, ...) \
	_FRR_CFG_DEFAULT(bool, bool, varname, ## __VA_ARGS__)
#define FRR_CFG_DEFAULT_LONG(varname, ...) \
	_FRR_CFG_DEFAULT(long, long, varname, ## __VA_ARGS__)
#define FRR_CFG_DEFAULT_ULONG(varname, ...) \
	_FRR_CFG_DEFAULT(unsigned long, ulong, varname, ## __VA_ARGS__)
#define FRR_CFG_DEFAULT_FLOAT(varname, ...) \
	_FRR_CFG_DEFAULT(float, float, varname, ## __VA_ARGS__)
#define FRR_CFG_DEFAULT_STR(varname, ...) \
	_FRR_CFG_DEFAULT(const char *, str, varname, ## __VA_ARGS__)


/* daemons don't need to call any of these, libfrr handles that */
// frr_default_add: 把一个默认值登记进全局链表(构造期由宏自动调用，
//   daemon 一般无需手动调)。
extern void frr_default_add(struct frr_default *dflt);
// frr_defaults_version_set: 设定当前"生效版本"(影响 dflt_* 的取值)，
//   并立即重新求值所有默认值。
extern void frr_defaults_version_set(const char *version);
// frr_defaults_profile_set: 设定当前 profile(traditional/datacenter)，
//   并立即重新求值。
extern void frr_defaults_profile_set(const char *profile);
// frr_defaults_version: 返回当前生效版本串。
extern const char *frr_defaults_version(void);
// frr_defaults_profile: 返回当前 profile 名。
extern const char *frr_defaults_profile(void);
// frr_defaults_apply: 用当前版本/profile 重新求值所有已登记默认值。
extern void frr_defaults_apply(void);

// frr_defaults_profiles: 内置可用 profile 名数组，以 NULL 结尾。
extern const char *frr_defaults_profiles[];
// frr_defaults_profile_valid: 校验给定 profile 名是否在内置列表中。
extern bool frr_defaults_profile_valid(const char *profile);

/* like strcmp(), but with version ordering */
// frr_version_cmp: 类似 strcmp()，但按 FRR 版本号语义排序
//   (数值段按数值比较、'~' 排最前、'\0' 排最后)。返回 <0/0/>0。
extern int frr_version_cmp(const char *aa, const char *bb);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_DEFAULTS_H */
