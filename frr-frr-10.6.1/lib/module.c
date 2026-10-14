// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * ============================================================================
 * module.c —— FRR 模块加载与注册的实现
 * ============================================================================
 *
 * 本文件实现 module.h 声明的模块机制：维护一个以 frrmod_default（libfrr 自身）
 * 打头的运行时模块链表，并提供从磁盘 dlopen 加载可加载模块（.so）的能力。
 *
 * 核心数据 / 全局变量：
 *   - frrmod_default_info / frrmod_default  libfrr 自身的默认模块信息
 *   - frrmod_list                           模块运行时链表头
 *   - frrmod_last                           链表尾指针（O(1) 追加）
 *   - execname                              当前可执行名，用于拼出模块文件名
 *
 * 关键流程（frrmod_load）：
 *   1) 解析 spec 中的“名字:参数”，确定候选文件名
 *   2) 依次尝试 <dir>/<execname>_<name>.so、<dir>/<name>.so、<name>
 *   3) dlsym 查找 frr_module 符号取得模块运行时结构
 *   4) 检查是否重复加载、调用模块的 init 回调
 *   5) 成功则挂入链表；失败则回滚并（可选）经 pFerrlog 报告错误
 *
 * 错误处理：用 ERR_RECORD / ERR_REPORT / ERR_FREE 三个宏成组管理错误信息。
 *
 * 阅读顺序：全局变量 -> frrmod_init -> frrmod_load -> frrmod_terminate。
 * ============================================================================
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dlfcn.h>

#include "module.h"
#include "memory.h"
#include "lib/version.h"
#include "printfrr.h"

/*
 * MODULE_LOADNAME - 模块加载名的内存类型。
 * MODULE_LOADARGS - 模块加载参数的内存类型。
 * MODULE_LOAD_ERR - 加载错误字符串的内存类型。
 */
DEFINE_MTYPE_STATIC(LIB, MODULE_LOADNAME, "Module loading name");
DEFINE_MTYPE_STATIC(LIB, MODULE_LOADARGS, "Module loading arguments");
DEFINE_MTYPE_STATIC(LIB, MODULE_LOAD_ERR, "Module loading error");

/*
 * frrmod_default_info - libfrr 自身（核心模块）的默认元信息。
 * 名字固定为 "libfrr"，版本取编译期 FRR_VERSION。
 */
static struct frrmod_info frrmod_default_info = {
	.name = "libfrr",
	.version = FRR_VERSION,
	.description = "libfrr core module",
};
/*
 * frrmod_default - libfrr 自身的运行时结构，也是模块链表的第一项。
 * finished_loading 置 1 表示核心模块始终视为“已加载”。
 */
union _frrmod_runtime_u frrmod_default = {
	.r =
		{
			.info = &frrmod_default_info,
			.finished_loading = 1,
		},
};

/*
 * 为本编译单元设置 xref 交叉引用支持（与 FRR_COREMOD_SETUP 中的一致）。
 */
XREF_SETUP();

/*
 * 把外部引用 _frrmod_this_module 弱符号化并别名到 frrmod_default：
 * 对未自带 FRR_COREMOD_SETUP 的编译单元，THIS_MODULE 会指向 libfrr 自身。
 * 上方被注释掉的行列出按编译器能力可选的几种实现方式。
 */
// if defined(HAVE_SYS_WEAK_ALIAS_ATTRIBUTE)
// union _frrmod_runtime_u _frrmod_this_module
//	__attribute__((weak, alias("frrmod_default")));
// elif defined(HAVE_SYS_WEAK_ALIAS_PRAGMA)
#pragma weak _frrmod_this_module = frrmod_default
// else
// error need weak symbol support
// endif

/*
 * frrmod_list - 模块运行时链表头，初值指向 libfrr 自身。
 * frrmod_last  - 链表尾指针，便于 O(1) 追加新模块。
 * execname     - 当前可执行名，用于拼出 "<execname>_<mod>.so" 形式的模块文件名。
 */
struct frrmod_runtime *frrmod_list = &frrmod_default.r;
static struct frrmod_runtime **frrmod_last = &frrmod_default.r.next;
static const char *execname = NULL;

/*
 * frrmod_init - 把模块运行时结构追加到全局链表并记录可执行名
 * @modinfo 待注册的模块运行时结构
 * 副作用：置 finished_loading、把节点接到链表尾，并把 execname 设为该模块名。
 */
void frrmod_init(struct frrmod_runtime *modinfo)
{
	modinfo->finished_loading = true;
	*frrmod_last = modinfo;
	frrmod_last = &modinfo->next;

	execname = modinfo->info->name;
}

/*
 * If caller wants error strings, it should define non-NULL pFerrlog
 * which will be called with 0-terminated error messages. These
 * messages will NOT contain newlines, and the (*pFerrlog)() function
 * could be called multiple times for a single call to frrmod_load().
 *
 * The (*pFerrlog)() function may copy these strings if needed, but
 * should expect them to be freed by frrmod_load() before frrmod_load()
 * returns.
 *
 * frrmod_load() is coded such that (*pFerrlog)() will be called only
 * in the case where frrmod_load() returns an error.
 */
/*
 * frrmod_load - 从磁盘加载一个 FRR 模块（dlopen 可加载模块）
 * @spec          加载规格，形如 "name" 或 "name:args"
 * @dir           模块搜索目录
 * @pFerrlog      可选错误回调，出错时逐条消息调用；为 NULL 则静默
 * @pErrlogCookie 透传给 pFerrlog 的上下文
 * 返回：成功返回模块运行时结构指针；失败返回 NULL
 * 副作用：dlopen / dlclose 目标 .so，dlsym 查找 frr_module，调用模块 init；
 *         成功时把模块挂入 frrmod_list。
 * 坑：错误信息字符串在返回前会被释放，pFerrlog 若需保留须自行拷贝。
 */
struct frrmod_runtime *frrmod_load(const char *spec, const char *dir,
				   void (*pFerrlog)(const void *, const char *),
				   const void *pErrlogCookie)
{
	void *handle = NULL;
	char name[PATH_MAX], fullpath[PATH_MAX * 2], *args;
	struct frrmod_runtime *rtinfo, **rtinfop;
	const struct frrmod_info *info;

/*
 * 单次加载最多记录的错误条数，超出的错误信息将被丢弃。
 */
#define FRRMOD_LOAD_N_ERRSTR 10
	char *aErr[FRRMOD_LOAD_N_ERRSTR];
	unsigned int iErr = 0;

	memset(aErr, 0, sizeof(aErr));

/*
 * ERR_RECORD - 记录一条加载错误（仅在提供了 pFerrlog 且未超上限时）。
 * 使用 asprintfrr 格式化后存入 aErr 数组，供稍后统一上报。
 */
#define ERR_RECORD(...)                                                        \
	do {                                                                   \
		if (pFerrlog && (iErr < FRRMOD_LOAD_N_ERRSTR)) {               \
			aErr[iErr++] = asprintfrr(MTYPE_MODULE_LOAD_ERR,       \
						  __VA_ARGS__);                \
		}                                                              \
	} while (0)

/*
 * ERR_REPORT - 把已记录的加载错误逐条交给 pFerrlog 回调上报。
 */
#define ERR_REPORT                                                             \
	do {                                                                   \
		if (pFerrlog) {                                                \
			unsigned int i;                                        \
                                                                               \
			for (i = 0; i < iErr; ++i) {                           \
				(*pFerrlog)(pErrlogCookie, aErr[i]);           \
			}                                                      \
		}                                                              \
	} while (0)

/*
 * ERR_FREE - 释放所有已记录的错误字符串并清零计数。
 */
#define ERR_FREE                                                               \
	do {                                                                   \
		unsigned int i;                                                \
                                                                               \
		for (i = 0; i < iErr; ++i) {                                   \
			XFREE(MTYPE_MODULE_LOAD_ERR, aErr[i]);                 \
			aErr[i] = 0;                                           \
		}                                                              \
		iErr = 0;                                                      \
	} while (0)

	snprintf(name, sizeof(name), "%s", spec);
	args = strchr(name, ':');
	if (args)
		*args++ = '\0';

	if (!strchr(name, '/')) {
		if (execname) {
			snprintf(fullpath, sizeof(fullpath), "%s/%s_%s.so", dir,
				 execname, name);
			handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);
			if (!handle)
				ERR_RECORD("loader error: dlopen(%s): %s",
					   fullpath, dlerror());
		}
		if (!handle) {
			snprintf(fullpath, sizeof(fullpath), "%s/%s.so", dir,
				 name);
			handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);
			if (!handle)
				ERR_RECORD("loader error: dlopen(%s): %s",
					   fullpath, dlerror());
		}
	}
	if (!handle) {
		snprintf(fullpath, sizeof(fullpath), "%s", name);
		handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);
		if (!handle)
			ERR_RECORD("loader error: dlopen(%s): %s", fullpath,
				   dlerror());
	}
	if (!handle) {
		ERR_REPORT;
		ERR_FREE;
		return NULL;
	}

	/* previous dlopen() errors are no longer relevant */
	ERR_FREE;

	rtinfop = dlsym(handle, "frr_module");
	if (!rtinfop) {
		dlclose(handle);
		ERR_RECORD("\"%s\" is not an FRR module: %s", name, dlerror());
		ERR_REPORT;
		ERR_FREE;
		return NULL;
	}
	rtinfo = *rtinfop;
	rtinfo->load_name = XSTRDUP(MTYPE_MODULE_LOADNAME, name);
	rtinfo->dl_handle = handle;
	if (args)
		rtinfo->load_args = XSTRDUP(MTYPE_MODULE_LOADARGS, args);
	info = rtinfo->info;

	if (rtinfo->finished_loading) {
		dlclose(handle);
		ERR_RECORD("module \"%s\" already loaded", name);
		goto out_fail;
	}

	if (info->init && info->init()) {
		dlclose(handle);
		ERR_RECORD("module \"%s\" initialisation failed", name);
		goto out_fail;
	}

	rtinfo->finished_loading = true;

	*frrmod_last = rtinfo;
	frrmod_last = &rtinfo->next;
	ERR_FREE;
	return rtinfo;

out_fail:
	XFREE(MTYPE_MODULE_LOADARGS, rtinfo->load_args);
	XFREE(MTYPE_MODULE_LOADNAME, rtinfo->load_name);
	ERR_REPORT;
	ERR_FREE;
	return NULL;
}

/*
 * frrmod_unload 尚未实现，暂以 #if 0 屏蔽。
 */
#if 0
void frrmod_unload(struct frrmod_runtime *module)
{
}
#endif

/*
 * frrmod_terminate - 释放各模块运行时占用的加载名 / 加载参数字符串。
 * 说明：仅释放字符串资源，不解绑 dlopen 句柄，也不释放链表节点本身。
 */
void frrmod_terminate(void)
{
	struct frrmod_runtime *rtinfo = frrmod_list;

	while (rtinfo) {
		XFREE(MTYPE_MODULE_LOADNAME, rtinfo->load_name);
		XFREE(MTYPE_MODULE_LOADARGS, rtinfo->load_args);

		rtinfo = rtinfo->next;
	}
}
