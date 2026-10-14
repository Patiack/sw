// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：memory.h —— FRR 的内存记账体系（MTYPE / MGROUP 与 XMALLOC 族）
 *
 * FRR 不用裸 malloc，而是把所有分配按“类型”（MTYPE）分类记账，以便在 show memory
 * 与进程退出时打印内存使用与泄漏。本头文件是这套体系的接口。
 *
 * 两个核心数据结构：
 *   struct memtype  代表一种分配类型（如 MTYPE_TMP），用原子计数器统计当前分配次数、
 *     峰值、字节数等；每个管理组下挂着若干 MTYPE。
 *   struct memgroup 把相关的 MTYPE 分组（如某守护进程一组），便于分类汇总与打印。
 *
 * 宏体系（靠构造函数在程序启动时自动注册到链表）：
 *   DECLARE_MGROUP / DEFINE_MGROUP 与 _ACTIVEATEXIT：声明与定义管理组。
 *   DECLARE_MTYPE / DEFINE_MTYPE / DEFINE_MTYPE_STATIC：声明与定义类型，
 *     STATIC 版只在本文件可见。
 *
 * 分配接口：底层是 qmalloc/qcalloc/qrealloc/qstrdup/qfree（显式传 MTYPE），
 * 上层用 XMALLOC/XCALLOC/XREALLOC/XSTRDUP/XFREE 宏包装，XFREE 还会把指针置空防悬垂。
 *
 * 依赖关系：实现全部在 memory.c；本文件几乎被每个 .c 文件包含。qmem_walk 供
 * memory.c 遍历所有组与类型，log_memstats 负责打印，memory_oom 在分配失败时终止进程。
 */
#ifndef _QUAGGA_MEMORY_H
#define _QUAGGA_MEMORY_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <frratomic.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIZE_VAR ~0UL
/* 一种内存分配类型的记账记录。name 供打印；n_alloc 当前活跃分配次数，
 * n_max 历史峰值；size 当前占用字节，total 累计分配字节，max_size 峰值字节。
 * 计数器是原子的，方便多线程守护进程并发更新；next/ref 把类型串成链表。 */
struct memtype {
	struct memtype *next, **ref;
	const char *name;
	atomic_size_t n_alloc;
	atomic_size_t n_max;
	atomic_size_t size;
	atomic_size_t total;
	atomic_size_t max_size;
};

/* 管理组：把相关 MTYPE 归为一类，便于分类统计。types 指向组内第一个类型，
 * insert 是插入尾部用的二级指针；active_at_exit 为 false 时退出时不计入泄漏报告。 */
struct memgroup {
	struct memgroup *next, **ref;
	struct memtype *types, **insert;
	const char *name;
	/* ignore group on dumping memleaks at exit */
	bool active_at_exit;
};

/* 下面是 MTYPE 与 MGROUP 的典型用法：头文件里 DECLARE，在某个 .c 里 DEFINE，
 * 其它文件引用同一 MTYPE 直接 qmalloc 即可。MTYPE_* 在编译器眼里是 struct memtype*
 * 类型的符号，而 MGROUP_* 不是（仅用于分组注册）。 */
/* macro usage:
 *
 *  mydaemon.h
 *    DECLARE_MGROUP(MYDAEMON);
 *    DECLARE_MTYPE(MYDAEMON_COMMON);
 *
 *  mydaemon.c
 *    DEFINE_MGROUP(MYDAEMON, "my daemon memory");
 *    DEFINE_MTYPE(MYDAEMON, MYDAEMON_COMMON,
 *                   "this mtype is used in multiple files in mydaemon");
 *    foo = qmalloc(MTYPE_MYDAEMON_COMMON, sizeof(*foo))
 *
 *  mydaemon_io.c
 *    bar = qmalloc(MTYPE_MYDAEMON_COMMON, sizeof(*bar))
 *
 *    DEFINE_MTYPE_STATIC(MYDAEMON, MYDAEMON_IO,
 *                          "this mtype is used only in this file");
 *    baz = qmalloc(MTYPE_MYDAEMON_IO, sizeof(*baz))
 *
 *  Note:  Naming conventions (MGROUP_ and MTYPE_ prefixes are enforced
 *         by not having these as part of the macro arguments)
 *  Note:  MTYPE_* are symbols to the compiler (of type struct memtype *),
 *         but MGROUP_* aren't.
 */

/* clang-format off */

/* 声明一个管理组：只产生 extern 符号 _mg_<name>，实体留给某 .c 里的 DEFINE_MGROUP */
#define DECLARE_MGROUP(name) extern struct memgroup _mg_##name
/* 定义管理组的实体：把组对象放进 mgroups 段，并用优先级 1000 的构造函数在启动时
 * 把本组链入全局链表 mg_insert，析构函数在退出时摘除。底层宏，一般不直接用。 */
#define _DEFINE_MGROUP(mname, desc, ...)                                       \
	struct memgroup _mg_##mname _DATA_SECTION("mgroups") = {               \
		.name = desc,                                                  \
		.types = NULL,                                                 \
		.next = NULL,                                                  \
		.insert = NULL,                                                \
		.ref = NULL,                                                   \
		__VA_ARGS__                                                    \
	};                                                                     \
	static void _mginit_##mname(void) __attribute__((_CONSTRUCTOR(1000))); \
	static void _mginit_##mname(void)                                      \
	{                                                                      \
		extern struct memgroup **mg_insert;                            \
		_mg_##mname.ref = mg_insert;                                   \
		*mg_insert = &_mg_##mname;                                     \
		mg_insert = &_mg_##mname.next;                                 \
	}                                                                      \
	static void _mgfini_##mname(void) __attribute__((_DESTRUCTOR(1000)));  \
	static void _mgfini_##mname(void)                                      \
	{                                                                      \
		if (_mg_##mname.next)                                          \
			_mg_##mname.next->ref = _mg_##mname.ref;               \
		*_mg_##mname.ref = _mg_##mname.next;                           \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON() /* end */

/* 定义管理组（默认退出时不主动打印）；ACTIVEATEXIT 版在退出内存泄漏统计中一并显示 */
#define DEFINE_MGROUP(mname, desc) \
	_DEFINE_MGROUP(mname, desc, )
#define DEFINE_MGROUP_ACTIVEATEXIT(mname, desc) \
	_DEFINE_MGROUP(mname, desc, .active_at_exit = true)

/* 声明一种分配类型：产生 extern struct memtype MTYPE_<name> 的单元素数组符号 */
#define DECLARE_MTYPE(name)                                                    \
	extern struct memtype MTYPE_##name[1]                                  \
	/* end */

/* 带属性的类型定义底层宏：把 MTYPE_<mname> 放进 mtypes 段，用优先级 1001 的构造函数
 * 在启动时插入所属 group 的类型链表；attr 可注入 static 等修饰。一般不直接用。 */
#define DEFINE_MTYPE_ATTR(group, mname, attr, desc)                            \
	attr struct memtype MTYPE_##mname[1] _DATA_SECTION("mtypes") = { {     \
		.name = desc,                                                  \
		.next = NULL,                                                  \
		.n_alloc = 0,                                                  \
		.size = 0,                                                     \
		.ref = NULL,                                                   \
	} };                                                                   \
	static void _mtinit_##mname(void) __attribute__((_CONSTRUCTOR(1001))); \
	static void _mtinit_##mname(void)                                      \
	{                                                                      \
		if (_mg_##group.insert == NULL)                                \
			_mg_##group.insert = &_mg_##group.types;               \
		MTYPE_##mname->ref = _mg_##group.insert;                       \
		*_mg_##group.insert = MTYPE_##mname;                           \
		_mg_##group.insert = &MTYPE_##mname->next;                      \
	}                                                                      \
	static void _mtfini_##mname(void) __attribute__((_DESTRUCTOR(1001)));  \
	static void _mtfini_##mname(void)                                      \
	{                                                                      \
		if (MTYPE_##mname->next)                                       \
			MTYPE_##mname->next->ref = MTYPE_##mname->ref;         \
		*MTYPE_##mname->ref = MTYPE_##mname->next;                     \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON() /* end */

/* 定义一种跨文件可见的分配类型，供 XMALLOC(MTYPE_XXX, n) 之类引用 */
#define DEFINE_MTYPE(group, name, desc)                                        \
	DEFINE_MTYPE_ATTR(group, name, , desc)                                 \
	/* end */

/* 定义仅本文件可见的分配类型（带 static） */
#define DEFINE_MTYPE_STATIC(group, name, desc)                                 \
	DEFINE_MTYPE_ATTR(group, name, static, desc)                           \
	/* end */

/* clang-format on */

/* libfrr 自己保留的管理组与类型：LIB 为公共库管理组，TMP 为通用临时分配，
 * TMP_TTABLE 供路由表相关临时结构使用 */
DECLARE_MGROUP(LIB);
DECLARE_MTYPE(TMP);
DECLARE_MTYPE(TMP_TTABLE);


/* 内存记账分配族，均需显式传入 MTYPE。
 * qmalloc/qcalloc：分配并计数；qrealloc：重分配；qstrdup：复制字符串；
 * qcountfree：只减计数不归还内存（用于走自定义释放路径的场景）；
 * qfree：真正释放并更新统计。声明上的 malloc / _ALLOC_SIZE / nonnull / _RET_NONNULL
 * 属性用于让编译器做更好的静态检查与优化。 */
extern void *qmalloc(struct memtype *mt, size_t size)
	__attribute__((malloc, _ALLOC_SIZE(2), nonnull(1) _RET_NONNULL));
extern void *qcalloc(struct memtype *mt, size_t size)
	__attribute__((malloc, _ALLOC_SIZE(2), nonnull(1) _RET_NONNULL));
extern void *qrealloc(struct memtype *mt, void *ptr, size_t size)
	__attribute__((_ALLOC_SIZE(3), nonnull(1) _RET_NONNULL));
extern void *qstrdup(struct memtype *mt, const char *str);
extern void qcountfree(struct memtype *mt, void *ptr)
	__attribute__((nonnull(1)));
extern void qfree(struct memtype *mt, void *ptr) __attribute__((nonnull(1)));

/* XMALLOC 族：对 q* 函数的便捷封装，是代码里最常见的调用方式。
 * 注意 XFREE 是 do-while(0) 宏，释放后会把 ptr 置为 NULL，避免悬垂指针被重复释放。 */
#define XMALLOC(mtype, size)		qmalloc(mtype, size)
#define XCALLOC(mtype, size)		qcalloc(mtype, size)
#define XREALLOC(mtype, ptr, size)	qrealloc(mtype, ptr, size)
#define XSTRDUP(mtype, str)		qstrdup(mtype, str)
#define XCOUNTFREE(mtype, ptr)		qcountfree(mtype, ptr)
#define XFREE(mtype, ptr)                                                      \
	do {                                                                   \
		qfree(mtype, ptr);                                             \
		ptr = NULL;                                                    \
	} while (0)

/* 返回该类型的当前活跃分配次数（读取原子计数） */
static inline size_t mtype_stats_alloc(struct memtype *mt)
{
	return mt->n_alloc;
}

/* 内存统计遍历接口：qmem_walk 按管理组顺序遍历所有 MTYPE 并回调 func，
 * 每个组会额外以 mt == NULL 回调一次，以便打印组标题（空组也会出现）。
 * 回调返回 0 继续、非 0 中止；qmem_walk 返回最后一次回调值。
 * log_memstats 打印指定守护进程的内存统计。 */
/* NB: calls are ordered by memgroup; and there is a call with mt == NULL for
 * each memgroup (so that a header can be printed, and empty memgroups show)
 *
 * return value: 0: continue, !0: abort walk.  qmem_walk will return the
 * last value from qmem_walk_fn. */
typedef int qmem_walk_fn(void *arg, struct memgroup *mg, struct memtype *mt);
extern int qmem_walk(qmem_walk_fn *func, void *arg);
extern int log_memstats(const char *daemon_name, bool enabled);

/* 内存耗尽处理：打印 size 与 name 后终止进程（FRR_NORETURN 表示不会返回） */
extern FRR_NORETURN void memory_oom(size_t size, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _QUAGGA_MEMORY_H */
