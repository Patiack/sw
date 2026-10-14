// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2016  David Lamparter, for NetDEF, Inc.
 */

#ifndef _FRR_HOOK_H
#define _FRR_HOOK_H

#include <stdbool.h>

#include "module.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 文件导读：hook.h —— FRR 的“类型安全”钩子（hook）回调框架
 *
 * 1. 这个文件干什么：
 *    FRR 各模块之间需要“事件发生时通知一批订阅者”的解耦机制，本文件用一组宏
 *    实现了一个轻量的观察者 / 发布订阅框架。头文件里 DECLARE_HOOK 声明钩子，
 *    源文件里 DEFINE_HOOK 定义钩子（同时生成 hook_call_xxx 调用函数），任何
 *    模块用 hook_register 注册回调，业务代码用 hook_call 触发全部回调。
 *
 * 2. 核心数据结构：
 *    - struct hook    ：一个钩子点，含名字、订阅者链表 entries、reverse 标志。
 *    - struct hookent ：单个订阅项，保存回调指针、可选参数、优先级、所属模块。
 *    - HOOK_DEFAULT_PRIORITY：默认优先级 1000（越小越先被调用）。
 *
 * 3. 关键宏（建议阅读顺序）：
 *    DECLARE_HOOK / DECLARE_KOOH  -> 声明钩子（放头文件）
 *    DEFINE_HOOK / DEFINE_KOOH    -> 定义并生成调用函数（放源文件）
 *    hook_register[_arg][_prio]   -> 注册回调
 *    hook_unregister[_arg]        -> 注销回调
 *    hook_call                    -> 触发调用，累加各回调返回值
 *
 * 4. 与其他模块的关系：
 *    依赖 module.h（THIS_MODULE）、memory.h；被 FRR 几乎所有子系统使用。
 *    “类型安全”指注册时通过 _hook_typecheck_xxx 内联函数校验函数指针签名。
 *
 * 5. 易踩的坑：
 *    - DECLARE 与 DEFINE 的 arglist / passlist 参数名必须完全一致。
 *    - 参数名不要以 "hook" 开头，否则会与框架内部生成的变量冲突。
 *    - Kooh 只是反向优先级，用于清理 / 反初始化，本质与 Hook 相同。
 */

/* type-safe subscribable hook points
 *
 * where "type-safe" applies to the function pointers used for subscriptions
 *
 * overall usage:
 * - to create a hook:
 *
 *   mydaemon.h:
 *     #include "hook.h"
 *     DECLARE_HOOK (some_update_event, (struct eventinfo *info), (info));
 *
 *   mydaemon.c:
 *     DEFINE_HOOK (some_update_event, (struct eventinfo *info), (info));
 *     ...
 *     hook_call (some_update_event, info)
 *
 * Note:  the second and third macro args must be the hook function's
 * parameter list, with the same names for each parameter.  The second
 * macro arg is with types (used for defining things), the third arg is
 * just the names (used for passing along parameters).
 *
 * Do not use parameter names starting with "hook", these can collide with
 * names used by the hook code itself.
 *
 * The return value is always "int" for now;  hook_call will sum up the
 * return values from each registered user.  Default is 0.
 *
 * There are no pre-defined semantics for the value, in most cases it is
 * ignored.  For success/failure indication, 0 should be success, and
 * handlers should make sure to only return 0 or 1 (not -1 or other values).
 *
 *
 * - to use a hook / create a handler:
 *
 *     #include "mydaemon.h"
 *     int event_handler (struct eventinfo *info) { ... }
 *     hook_register (some_update_event, event_handler);
 *
 *   or, if you need an argument to be passed along (addonptr will be added
 *   as first argument when calling the handler):
 *
 *     #include "mydaemon.h"
 *     int event_handler (void *addonptr, struct eventinfo *info) { ... }
 *     hook_register_arg (some_update_event, event_handler, addonptr);
 *
 *   (addonptr isn't typesafe, but that should be manageable.)
 *
 * Hooks also support a "priority" value for ordering registered calls
 * relative to each other.  The priority is a signed integer where lower
 * values are called earlier.  There is also "Koohs", which is hooks with
 * reverse priority ordering (for cleanup/deinit hooks, so you can use the
 * same priority value).
 *
 * Recommended priority value ranges are:
 *
 *  -999 ...     0 ...  999 - main executable / daemon, or library
 * -1999 ... -1000          - modules registering calls that should run before
 *                            the daemon's bits
 *            1000 ... 1999 - modules calls that should run after daemon's
 *
 * Note: the default value is 1000, based on the following 2 expectations:
 * - most hook_register() usage will be in loadable modules
 * - usage of hook_register() in the daemon itself may need relative ordering
 *   to itself, making an explicit value the expected case
 *
 * The priority value is passed as extra argument on hook_register_prio() /
 * hook_register_arg_prio().  Whether a hook runs in reverse is determined
 * solely by the code defining / calling the hook.  (DECLARE_KOOH is actually
 * the same thing as DECLARE_HOOK, it's just there to make it obvious.)
 */

/* TODO:
 * - hook_unregister_all_module()
 * - introspection / CLI / debug
 * - testcases ;)
 *
 * For loadable modules, the idea is that hooks could be automatically
 * unregistered when a module is unloaded.
 *
 * It's also possible to add a constructor (MTYPE style) to DEFINE_HOOK,
 * which would make it possible for the CLI to show all hooks and all
 * registered handlers.
 */

/*
 * struct hookent —— 单个钩子订阅项（挂在某个 struct hook 的 entries 链表上）
 * 每次 hook_register 都会产生一个 hookent，记录回调地址、可选附加参数、
 * 优先级，以及注册它的模块（模块卸载时可据此批量注销）。
 * 位域说明：has_arg 表示该回调是否带 addonptr 附加参数；
 *           ent_on_heap 表示该表项分配在堆上（动态注册需释放）；
 *           ent_used 表示该表项当前是否已被占用。
 */
struct hookent {
	struct hookent *next;
	void *hookfn; /* actually a function pointer */
	void *hookarg;
	bool has_arg : 1;
	bool ent_on_heap : 1;
	bool ent_used : 1;
	int priority;
	struct frrmod_runtime *module;
	const char *fnname;
};

/*
 * struct hook —— 一个钩子点
 * name 为钩子名（调试用）；entries 指向订阅者单向链表；
 * reverse 为真表示按优先级反向调用（即 Kooh，用于清理型回调）。
 */
struct hook {
	const char *name;
	struct hookent *entries;
	bool reverse;
};

/*
 * 钩子默认优先级：1000。
 * 约定 0..999 留给守护进程 / 库自身，负值表示先于守护进程执行，
 * 1000..1999 表示后于守护进程执行（默认取 1000 主要是给可加载模块用）。
 */
#define HOOK_DEFAULT_PRIORITY 1000

/* subscribe/add callback function to a hook
 *
 * always use hook_register(), which uses the static inline helper from
 * DECLARE_HOOK in order to get type safety
 */
/*
 * _hook_register -- 钩子框架内部的注册实现（不要直接调用，用 hook_register 系列宏）
 * 参数：hook 目标钩子点；stackent 可复用的静态 hookent（通常在数据段）；
 *       funcptr 回调函数指针；arg 可选附加参数；has_arg 是否带附加参数；
 *       module 注册者所属模块；funcname 回调函数名字符串；priority 排序优先级。
 * 返回值：无。副作用：把该订阅项按优先级插入 hook->entries 链表。
 * 坑：函数指针被强制成 void * 传递，真正调用时再由 DEFINE_HOOK 生成的代码转回。
 */
extern void _hook_register(struct hook *hook, struct hookent *stackent,
			   void *funcptr, void *arg, bool has_arg,
			   struct frrmod_runtime *module,
			   const char *funcname, int priority);

/* most hook_register calls are not in a loop or similar and can use a
 * statically allocated "struct hookent" from the data segment
 */
/*
 * _hook_reg_svar -- 用静态 hookent 完成注册的辅助宏（绝大多数注册点都用它）。
 * 原理：在调用处声明一个 static struct hookent（放在数据段、零初始化、进程内唯一），
 *       再交给 _hook_register 按优先级插入链表。static 保证表项生命周期覆盖全程。
 * 坑：因为是 static，同一处注册多次会复用同一个入口，通常只在初始化时注册一次。
 */
#define _hook_reg_svar(hook, funcptr, arg, has_arg, module, funcname, prio)    \
	do {                                                                   \
		static struct hookent stack_hookent = {};                      \
		_hook_register(hook, &stack_hookent, funcptr, arg, has_arg,    \
			       module, funcname, prio);                        \
	} while (0)

/*
 * hook_register 系列 -- 把回调函数注册到某个钩子。
 *   hook_register(hookname, func)              无附加参数、默认优先级
 *   hook_register_arg(hookname, func, arg)     带 addonptr 附加参数、默认优先级
 *   hook_register_prio(hookname, prio, func)   指定优先级
 *   hook_register_arg_prio(...)                带参数并指定优先级
 * 其中 _hook_typecheck_##hookname(func) 是 DECLARE_HOOK 生成的类型检查内联函数，
 * 编译期即可发现回调签名写错，这正是本框架“类型安全”的体现。
 */
#define hook_register(hookname, func)                                          \
	_hook_reg_svar(&_hook_##hookname, _hook_typecheck_##hookname(func),    \
		       NULL, false, THIS_MODULE, #func, HOOK_DEFAULT_PRIORITY)
#define hook_register_arg(hookname, func, arg)                                 \
	_hook_reg_svar(&_hook_##hookname,                                      \
		       _hook_typecheck_arg_##hookname(func), arg, true,        \
		       THIS_MODULE, #func, HOOK_DEFAULT_PRIORITY)
#define hook_register_prio(hookname, prio, func)                               \
	_hook_reg_svar(&_hook_##hookname, _hook_typecheck_##hookname(func),    \
		       NULL, false, THIS_MODULE, #func, prio)
#define hook_register_arg_prio(hookname, prio, func, arg)                      \
	_hook_reg_svar(&_hook_##hookname,                                      \
		       _hook_typecheck_arg_##hookname(func), arg, true,        \
		       THIS_MODULE, #func, prio)

/*
 * _hook_unregister -- 注销实现，从 hook->entries 中摘除匹配的订阅项。
 * 匹配依据是（funcptr, arg, has_arg）三者都相同。无返回值。
 * 通常不直接调用，使用 hook_unregister / hook_unregister_arg 宏。
 */
extern void _hook_unregister(struct hook *hook, void *funcptr, void *arg,
			     bool has_arg);
#define hook_unregister(hookname, func)                                        \
	_hook_unregister(&_hook_##hookname, _hook_typecheck_##hookname(func),  \
			 NULL, false)
#define hook_unregister_arg(hookname, func, arg)                               \
	_hook_unregister(&_hook_##hookname,                                    \
			 _hook_typecheck_arg_##hookname(func), arg, true)

/*
 * hook_have_hooks -- 判断某钩子是否已有订阅者，有则返回真。
 * 常在“没有订阅者就跳过昂贵准备”场景做提前退出优化。
 */
#define hook_have_hooks(hookname) (_hook_##hookname.entries != NULL)

/* invoke hooks
 * this is private (static) to the file that has the DEFINE_HOOK statement
 */
/*
 * hook_call -- 触发钩子，调用所有已注册回调。
 * 展开为 DEFINE_HOOK 生成的静态函数 hook_call_##hookname（仅定义该钩子的文件可见）。
 * 参数与原钩子 arglist 一致；返回值是各回调返回值之和（默认 0，通常被忽略）。
 */
#define hook_call(hookname, ...) hook_call_##hookname(__VA_ARGS__)

/*
 * HOOK_ADDDEF / HOOK_ADDARG -- 在参数列表前插入附加参数。
 * HOOK_ADDDEF 用于声明（带类型 void *hookarg），HOOK_ADDARG 用于调用（只给名字）。
 * 二者配合把可选 addonptr 作为回调的第一个参数传递。
 */
/* helpers to add the void * arg */
#define HOOK_ADDDEF(...) (void *hookarg , ## __VA_ARGS__)
#define HOOK_ADDARG(...) (hookarg , ## __VA_ARGS__)

/*
 * _SKIP_10 / _MAKE_VOID / HOOK_VOIDIFY -- 把空参数 () 变成 (void) 的小把戏。
 * C 里 () 表示“参数未指定”，而 (void) 才表示“无参数”；这里通过丢掉前 10 个
 * 形参、取第 11 个 ret 来实现：无参时返回类型位置补上 void，得到规范原型。
 * HOOK_VOIDIFY 用于把“钩子的类型化参数表”插入到函数声明中。
 */
/* and another helper to convert () into (void) to get a proper prototype */
#define _SKIP_10(x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, ret, ...) ret
#define _MAKE_VOID(...) _SKIP_10(, ##__VA_ARGS__, , , , , , , , , , void)

#define HOOK_VOIDIFY(...) (_MAKE_VOID(__VA_ARGS__) __VA_ARGS__)

/* use in header file - declares the hook and its arguments
 * usage:  DECLARE_HOOK(my_hook, (int arg1, struct foo *arg2), (arg1, arg2));
 * as above, "passlist" must use the same order and same names as "arglist"
 *
 * theoretically passlist is not necessary, but let's keep things simple and
 * use exact same args on DECLARE and DEFINE.
 */
/*
 * DECLARE_HOOK -- 在头文件中声明一个钩子（不含定义）。
 * 展开产生：全局变量 _hook_##hookname 的 extern 声明，以及两个仅做签名检查的
 * 静态内联函数 _hook_typecheck_##hookname 和 _hook_typecheck_arg_##hookname。
 * arglist：带类型的参数表；passlist：仅参数名（供调用时转发）。两者顺序/名字须一致。
 */
#define DECLARE_HOOK(hookname, arglist, passlist)                              \
	extern struct hook _hook_##hookname;                                   \
	__attribute__((unused)) static inline void *                           \
		_hook_typecheck_##hookname(int(*funcptr) HOOK_VOIDIFY arglist) \
	{                                                                      \
		return (void *)funcptr;                                        \
	}                                                                      \
	__attribute__((unused)) static inline void                             \
		*_hook_typecheck_arg_##hookname(int(*funcptr)                  \
							HOOK_ADDDEF arglist)   \
	{                                                                      \
		return (void *)funcptr;                                        \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * DECLARE_KOOH -- 声明一个 Kooh（反向优先级钩子）。实现与 DECLARE_HOOK 完全相同，
 * 单独取名只为在代码里明确表达“这是清理 / 反初始化型钩子”。
 */
#define DECLARE_KOOH(hookname, arglist, passlist)                              \
	DECLARE_HOOK(hookname, arglist, passlist)

/* use in source file - contains hook-related definitions.
 */
/*
 * DEFINE_HOOK_INT -- 在源文件中定义钩子核心，并生成静态调用函数 hook_call_##hookname。
 * 生成内容：初始化全局 struct hook（名字、空链表、reverse=rev），以及遍历
 * entries 链表依次调用回调的静态函数——无附加参数的回调直接调用，带参数的
 * 回调把 hookarg 作为首参传入；所有返回值累加后返回。
 * 坑：该调用函数是 static 的，只能在定义钩子的这个 .c 文件里被 hook_call 使用。
 */
#define DEFINE_HOOK_INT(hookname, arglist, passlist, rev)                      \
	struct hook _hook_##hookname = {                                       \
		.name = #hookname, .entries = NULL, .reverse = rev,            \
	};                                                                     \
	static int hook_call_##hookname HOOK_VOIDIFY arglist                   \
	{                                                                      \
		int hooksum = 0;                                               \
		struct hookent *he = _hook_##hookname.entries;                 \
		void *hookarg;                                                 \
		union {                                                        \
			void *voidptr;                                         \
			int(*fptr) HOOK_VOIDIFY arglist;                       \
			int(*farg) HOOK_ADDDEF arglist;                        \
		} hookp;                                                       \
		for (; he; he = he->next) {                                    \
			hookarg = he->hookarg;                                 \
			hookp.voidptr = he->hookfn;                            \
			if (!he->has_arg)                                      \
				hooksum += hookp.fptr passlist;                \
			else                                                   \
				hooksum += hookp.farg HOOK_ADDARG passlist;    \
		}                                                              \
		return hooksum;                                                \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * DEFINE_HOOK / DEFINE_KOOH -- 在源文件中定义钩子。
 * DEFINE_HOOK 正向调用（优先级小者先），DEFINE_KOOH 反向调用（优先级大者先，
 * 用于清理）。两者都转发到 DEFINE_HOOK_INT，仅 reverse 取值不同。
 */
#define DEFINE_HOOK(hookname, arglist, passlist)                               \
	DEFINE_HOOK_INT(hookname, arglist, passlist, false)
#define DEFINE_KOOH(hookname, arglist, passlist)                               \
	DEFINE_HOOK_INT(hookname, arglist, passlist, true)

#ifdef __cplusplus
}
#endif

#endif /* _FRR_HOOK_H */
