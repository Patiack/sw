// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2021  David Lamparter, for NetDEF, Inc.
 */

/* WARNING: this file is "special" in that it overrides the system-provided
 * assert.h by being on the include path before it.  That means it should
 * provide the functional equivalent.
 *
 * This is intentional because FRR extends assert() to write to the log and
 * add backtraces.  Overriding the entire file is the simplest and most
 * reliable way to get this to work;  there were problems previously with the
 * system assert.h getting included afterwards and redefining assert() back to
 * the system variant.
 */

/* ============================================================================
 * 【文件导读】assert/assert.h —— FRR 断言宏（整份覆盖系统 assert.h）
 * ============================================================================
 * 【它在 FRR 里干什么】本文件是 FRR 自带的 assert.h，被放在 include 搜索路径中
 * 系统同名头文件之前，从而整份“顶替”系统提供的 assert.h，让 FRR 的 assert()
 * 在失败时写日志(zlog)并打印调用栈(backtrace)。
 *
 * 【为什么要替换】历史上出现过系统 assert.h 被随后包含、把 assert() 重新定义回
 * 系统版本的问题；整份覆盖是最简单也最可靠的做法。
 *
 * 【核心结构】
 *   struct xref_assert  : 断言登记项。把断言表达式、附加信息、可变参数登记进
 *                         全局交叉引用表(xref)，便于事后枚举与调试。
 *   _zlog_assert_failed : 断言失败时真正的处理函数（noreturn，不返回）。
 *
 * 【关键宏】
 *   assert(expr_)             : 标准断言语义，失败时写日志并经 xref 上报。
 *   assertf(expr_, extra_, ...): 带附加格式化信息的断言。
 *   zassert                   : assert 的别名（历史写法，保留兼容）。
 *   static_assert             : C11 之前由本文件补齐。
 *
 * 【阅读顺序】struct xref_assert 与 _zlog_assert_failed 声明 → assert / assertf
 *            的宏展开，理解“xref 登记 + 失败处理”如何配合。
 * ============================================================================
 */

#ifndef _FRR_ASSERT_H
#define _FRR_ASSERT_H

/* 交叉引用(xref)基础设施：断言宏借助 XREF_INIT / XREF_LINK 把每条断言登记到
 * 全局表，供调试器或自省工具枚举。
 */
#include "xref.h"

/* C++ 兼容：断言相关符号按 C 链接约定导出，避免 C++ 编译时名称修饰
 * (name mangling) 不一致导致链接失败。
 */
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
/* C++ has this built-in, but C provides it in assert.h for >=C11.  Since we
 * replace assert.h entirely, we need to provide it here too.
 */
/* 为 C 语言补齐 static_assert：因本文件整份替换了系统 assert.h，需一并提供
 * （C++ 自带该关键字，故用 __cplusplus 排除）。
 */
#define static_assert _Static_assert
#endif

/* 【结构】xref_assert —— 一条断言的登记项。由 assert / assertf 宏就地创建为
 * 静态 const 实例，编译器用 __attribute__((used)) 保证它不被优化掉，从而让失败
 * 时的表达式文本与附加信息可被外部枚举。
 *   xref  : 通用交叉引用节点（含类型、文件名、函数名等，供 XREF_LINK 串联）。
 *   expr  : 断言表达式字符串化后的文本。
 *   extra : assertf 的附加格式化串（assert 时为 NULL）。
 *   args  : assertf 可变参数字符串化后的文本（仅供调试查看）。
 */
struct xref_assert {
	struct xref xref;

	const char *expr;
	const char *extra, *args;
};

/* 【函数】_zlog_assert_failed —— 断言失败的统一处理入口：写日志、打印 backtrace
 * 并终止进程（noreturn，不返回）。
 *   xref  : 出错的断言登记项。
 *   extra : assertf 传入的附加格式化串（assert 时为 NULL）。
 *   ...   : 与 extra 对应的可变参数。
 * PRINTFRR(2, 3) 提示编译器按 printf 规则校验：第 2 个参数(extra)是格式串，
 * 第 3 个起(...)是待校验的可变参数。
 */
extern void _zlog_assert_failed(const struct xref_assert *xref,
				const char *extra, ...) PRINTFRR(2, 3)
	__attribute__((noreturn));

/* 【宏】assert(expr_) —— 断言宏。表达式为假时，就地构造一个静态 xref_assert 登记项
 * （把表达式文本 #expr_ 存入），再调用 _zlog_assert_failed 终止程序。语义与系统
 * assert 一致，但额外写日志与 backtrace。
 * 注意：__builtin_expect 把“表达式为假”标记为冷路径，利于正常路径分支预测。
 */
/* the "do { } while (expr_)" is there to get a warning for assignments inside
 * the assert expression aka "assert(x = 1)".  The (necessary) braces around
 * expr_ in the if () statement would suppress these warnings.  Since
 * _zlog_assert_failed() is noreturn, the while condition will never be
 * checked.
 */
#define assert(expr_)                                                          \
	({                                                                     \
		static const struct xref_assert _xref __attribute__(           \
			(used)) = {                                            \
			.xref = XREF_INIT(XREFT_ASSERT, NULL, __func__),       \
			.expr = #expr_,                                        \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		if (__builtin_expect((expr_) ? 0 : 1, 0))                      \
			do {                                                   \
				_zlog_assert_failed(&_xref, NULL);             \
			} while (expr_);                                       \
	})

/* 【宏】assertf(expr_, extra_, ...) —— 带附加信息的断言。除断言表达式外再传入一段
 * 格式化文本(extra_)及可变参数，失败时随日志一并打印，便于定位上下文。
 * 易踩的坑：extra_ 必须能在编译期作为静态初始化值（通常是字面量串）；__VA_ARGS__
 * 也被字符串化为 .args 仅用于调试展示，不参与真正的格式化传参。
 */
#define assertf(expr_, extra_, ...)                                            \
	({                                                                     \
		static const struct xref_assert _xref __attribute__(           \
			(used)) = {                                            \
			.xref = XREF_INIT(XREFT_ASSERT, NULL, __func__),       \
			.expr = #expr_,                                        \
			.extra = extra_,                                       \
			.args = #__VA_ARGS__,                                  \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		if (__builtin_expect((expr_) ? 0 : 1, 0))                      \
			do {                                                   \
				_zlog_assert_failed(&_xref, extra_,            \
						    ##__VA_ARGS__);            \
			} while (expr_);                                       \
	})

/* 【宏】zassert —— assert 的别名。历史写法，现仅为兼容保留。 */
#define zassert assert

#ifdef __cplusplus
}
#endif

#endif /* _FRR_ASSERT_H */
