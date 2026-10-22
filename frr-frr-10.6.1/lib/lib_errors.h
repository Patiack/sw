// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Library-specific error messages.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 *                     Donald Sharp
 */

/*
 * 文件导读: lib_errors.h
 * ----------------------------------------------------------------------------
 * 本文件声明 lib/ 子系统的错误码枚举与初始化接口, 属于 FRR 的统一错误码体系.
 * 作用: 为 lib 库中可能出现的错误分配连续的 error code, 供 ferr 框架在运行时
 *   查找并展示对应的标题, 描述与建议.
 *
 * 核心数据结构: enum lib_log_refs, 从 LIB_FERR_START 开始为每个 lib 错误分配
 *   一个编号; 这些编号应与 lib_errors.c 中的错误表一一对应.
 *
 * 与其他模块关系: 依赖 ferr.h(错误框架与 LIB_FERR_START 定义); lib_errors.c 中
 *   的 ferr_lib_warn 与 ferr_lib_err 表使用这些编号, 由 lib_error_init 注册.
 *
 * 阅读顺序: 先看枚举中的错误码, 再看 lib_errors.c 里各错误码对应的文本.
 *
 * 易踩的坑: 新增错误码时应加在末尾并保持与 .c 中表项一致; 首个枚举值显式以
 *   LIB_FERR_START 为基准, 不要插入会打乱既有编号次序的项.
 */

#ifndef __LIB_ERRORS_H__
#define __LIB_ERRORS_H__

#include "lib/ferr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * lib 子系统错误码枚举.
 * EC_LIB_PRIVILEGES 显式取 LIB_FERR_START(0x06000001) 作为起始, 后续码依次
 * 递增; 覆盖权限, VRF, socket, ZAPI, VTY, 命名空间, YANG 与北向(northbound),
 * 线程与事件, DNS 解析等 lib 侧错误.
 */
enum lib_log_refs {
	EC_LIB_PRIVILEGES = LIB_FERR_START,
	EC_LIB_VRF_START,
	EC_LIB_SOCKET,
	EC_LIB_ZAPI_MISSMATCH,
	EC_LIB_ZAPI_ENCODE,
	EC_LIB_ZAPI_SOCKET,
	EC_LIB_SYSTEM_CALL,
	EC_LIB_VTY,
	EC_LIB_INTERFACE,
	EC_LIB_NS,
	EC_LIB_DEVELOPMENT,
	EC_LIB_ZMQ,
	EC_LIB_UNAVAILABLE,
	EC_LIB_SNMP,
	EC_LIB_STREAM,
	EC_LIB_LINUX_NS,
	EC_LIB_SLOW_THREAD_CPU,
	EC_LIB_SLOW_THREAD_WALL,
	EC_LIB_STARVE_THREAD,
	EC_LIB_NO_THREAD,
	EC_LIB_TIMER_TOO_LONG,
	EC_LIB_RMAP_RECURSION_LIMIT,
	EC_LIB_BACKUP_CONFIG,
	EC_LIB_VRF_LENGTH,
	EC_LIB_YANG_MODULE_LOAD,
	EC_LIB_YANG_MODULE_LOADED_ALREADY,
	EC_LIB_YANG_DATA_CONVERT,
	EC_LIB_YANG_DATA_TRUNCATED,
	EC_LIB_YANG_UNKNOWN_DATA_PATH,
	EC_LIB_YANG_DNODE_NOT_FOUND,
	EC_LIB_YANG_TRANSLATOR_LOAD,
	EC_LIB_YANG_TRANSLATION_ERROR,
	EC_LIB_NB_DATABASE,
	EC_LIB_NB_CB_UNNEEDED,
	EC_LIB_NB_CB_MISSING,
	EC_LIB_NB_CB_INVALID_PRIO,
	EC_LIB_NB_CBS_VALIDATION,
	EC_LIB_NB_CB_CONFIG_VALIDATE,
	EC_LIB_NB_CB_CONFIG_PREPARE,
	EC_LIB_NB_CB_CONFIG_ABORT,
	EC_LIB_NB_CB_CONFIG_APPLY,
	EC_LIB_NB_CB_STATE,
	EC_LIB_NB_CB_RPC,
	EC_LIB_NB_CANDIDATE_INVALID,
	EC_LIB_NB_CANDIDATE_EDIT_ERROR,
	EC_LIB_NB_OPERATIONAL_DATA,
	EC_LIB_NB_TRANSACTION_CREATION_FAILED,
	EC_LIB_NB_TRANSACTION_RECORD_FAILED,
	EC_LIB_LIBYANG,
	EC_LIB_LIBYANG_PLUGIN_LOAD,
	EC_LIB_SYSREPO_INIT,
	EC_LIB_SYSREPO_DATA_CONVERT,
	EC_LIB_LIBSYSREPO,
	EC_LIB_GRPC_INIT,
	EC_LIB_ID_CONSISTENCY,
	EC_LIB_ID_EXHAUST,
	EC_LIB_RESOLVER,
};

/*
 * 注册 lib 子系统的错误表(ferr_lib_warn 与 ferr_lib_err)到全局错误哈希表.
 * 应在进程启动早期调用一次(参见 libfrr 的初始化流程).
 */
extern void lib_error_init(void);

#ifdef __cplusplus
}
#endif

#endif
