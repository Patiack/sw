// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

#ifndef _FRR_MODULE_H
#define _FRR_MODULE_H

#include <stdint.h>
#include <stdbool.h>

#include "compiler.h"
#include "xref.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * module.h —— FRR 模块（module）注册与元信息的声明
 * ============================================================================
 *
 * FRR 中每个守护进程（以及可动态加载的模块）都以一个“模块”存在：它携带
 * 名称、版本、描述等元信息，并可提供初始化回调。本文件定义这些元信息的
 * 数据结构与注册宏。
 *
 * 核心类型：
 *   - struct frrmod_info      模块静态元信息：name / version / description / init
 *   - struct frrmod_runtime   模块运行时信息：链表 next、info、dl_handle、
 *                             加载名 / 参数、是否已加载完成
 *   - union _frrmod_runtime_u 运行时结构 + 预留空间（保证 ABI 兼容与扩展）
 *
 * 关键宏：
 *   - FRR_COREMOD_SETUP 在源码中声明本编译单元所属模块的元信息
 *   - FRR_MODULE_SETUP  在可加载模块中额外导出 frr_module 符号
 *   - THIS_MODULE       取得当前模块的运行时指针
 *
 * 与其它模块关系：依赖 xref（XREF_SETUP）、compiler（DSO_LOCAL / MACRO_...）。
 *
 * 阅读顺序：frrmod_info -> frrmod_runtime -> SETUP 宏 -> 函数声明。
 * ============================================================================
 */

/*
 * 前置声明模块运行时结构，具体定义见下方。
 */
struct frrmod_runtime;

/*
 * struct frrmod_info - 模块的静态元信息（编译期常量）。
 * name        单行、少词的模块名
 * version     人类可读的版本号（不含空格）
 * description 一段话的描述
 * init        可选的初始化回调，返回非 0 表示失败
 */
struct frrmod_info {
	/* single-line few-word title */
	const char *name;
	/* human-readable version number, should not contain spaces */
	const char *version;
	/* one-paragraph description */
	const char *description;

	int (*init)(void);
};

/* primary entry point structure to be present in loadable module under
 * "_frrmod_this_module" dlsym() name
 *
 * note space for future extensions is reserved below, so other modules
 * (e.g. memory management, hooks) can add fields
 *
 * const members/info are in frrmod_info.
 */
/*
 * struct frrmod_runtime - 模块的运行时信息。
 * next              模块链表的下一个节点
 * info              指向静态元信息 frrmod_info
 * dl_handle         dlopen 得到的句柄（可加载模块）
 * finished_loading  是否已完成加载
 * load_name         加载时使用的名字
 * load_args         加载时传入的参数
 * 说明：这是可加载模块须以 "_frrmod_this_module" 名字导出的主入口结构，
 *       其下保留了向未来扩展的空间（见 _frrmod_runtime_size）。
 */
struct frrmod_runtime {
	struct frrmod_runtime *next;

	const struct frrmod_info *info;
	void *dl_handle;
	bool finished_loading;

	char *load_name;
	char *load_args;
};

/* space-reserving foo */
/*
 * struct _frrmod_runtime_size - 预留空间结构：在 frrmod_runtime 之后补齐到
 * 1024 字节，以便将来在不破坏 ABI 的前提下扩展运行时结构；
 * 若 frrmod_runtime 超过 1024 字节则此处会编译报错。
 */
struct _frrmod_runtime_size {
	struct frrmod_runtime r;
	/* this will barf if frrmod_runtime exceeds 1024 bytes ... */
	uint8_t space[1024 - sizeof(struct frrmod_runtime)];
};
/*
 * union _frrmod_runtime_u - 让运行时结构与固定大小的预留空间共用同一段内存，
 * 从而保证不同版本间二进制的兼容。
 */
union _frrmod_runtime_u {
	struct frrmod_runtime r;
	struct _frrmod_runtime_size s;
};

/*
 * 本编译单元自身的模块运行时对象（由 FRR_COREMOD_SETUP 定义，
 * 或在未定义时经弱符号回退到 frrmod_default）。
 */
extern union _frrmod_runtime_u _frrmod_this_module;
/*
 * THIS_MODULE - 取得当前模块的 struct frrmod_runtime 指针。
 */
#define THIS_MODULE (&_frrmod_this_module.r)

/*
 * FRR_COREMOD_SETUP - 在“编译进主程序”的模块源码里声明模块元信息。
 * 参数依次对应 frrmod_info 的字段（name, version, description, init?）。
 * 它定义一个静态 _frrmod_info 和本编译单元可见的 _frrmod_this_module，
 * 并展开 XREF_SETUP() 以支持 xref 交叉引用。
 */
#define FRR_COREMOD_SETUP(...)                                                 \
	static const struct frrmod_info _frrmod_info = {__VA_ARGS__};          \
	DSO_LOCAL union _frrmod_runtime_u _frrmod_this_module = {{             \
		NULL,                                                          \
		&_frrmod_info,                                                 \
	}};                                                                    \
	XREF_SETUP();                                                          \
	MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * FRR_MODULE_SETUP - 用于可动态加载的模块：先展开 FRR_COREMOD_SETUP，
 * 再导出一个非 static 的 frr_module 指针（供 dlsym 查找）。
 */
#define FRR_MODULE_SETUP(...)                                                  \
	FRR_COREMOD_SETUP(__VA_ARGS__);                                        \
	DSO_SELF struct frrmod_runtime *frr_module = &_frrmod_this_module.r;   \
	MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * frrmod_list - 全部已注册模块运行时信息组成的链表（含默认的 libfrr）。
 */
extern struct frrmod_runtime *frrmod_list;

/*
 * frrmod_init - 把模块运行时信息注册进全局链表（供主程序内部模块使用）。
 */
extern void frrmod_init(struct frrmod_runtime *modinfo);
/*
 * frrmod_terminate - 依次释放各模块运行时的加载名 / 参数等资源。
 */
extern void frrmod_terminate(void);
/*
 * frrmod_load - 从磁盘加载一个 FRR 模块。
 * spec 为加载规格（形如 "name" 或 "name:args"），dir 为搜索目录；
 * pFerrlog / pErrlogCookie 为可选错误回调及其上下文，出错时会被调用。
 * 返回加载成功的运行时信息，失败返回 NULL。
 */
extern struct frrmod_runtime *frrmod_load(const char *spec, const char *dir,
					  void (*pFerrlog)(const void *,
							   const char *),
					  const void *pErrlogCookie);
/*
 * frrmod_unload 尚未实现，暂时以 #if 0 禁用。
 */
#if 0
/* not implemented yet */
extern void frrmod_unload(struct frrmod_runtime *module);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _FRR_MODULE_H */
