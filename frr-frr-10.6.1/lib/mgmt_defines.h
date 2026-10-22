// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MGMTD public defines.
 *
 * Copyright (C) 2021  Vmware, Inc.
 *		       Pushpasis Sarkar <spushpasis@vmware.com>
 */

#ifndef _FRR_MGMTD_DEFINES_H
#define _FRR_MGMTD_DEFINES_H

/* ============================================================================
 * 【文件导读】mgmt_defines.h —— mgmtd 的跨模块常量与返回码
 * ============================================================================
 * 【定位】mgmtd 是 FRR 的**集中配置管理进程**：前端(frontend，各守护进程)通过
 * UNIX socket 连到 mgmtd，后端(backend)提供 YANG 回调。本文件把这些进程之间
 * 共享的少量定义集中起来：socket 路径、名字长度上限、XPath 长度上限、统一返回码，
 * 以及数据集(datastore) ID。
 * 【socket 名字宏的写法很特别】它们不是普通的字符串常量，而是“格式串 + 可变参”
 * 的整体写法，见下方说明。
 * 【两组枚举】
 *   enum mgmt_result : 所有 mgmtd API 的统一返回码，调用方按它分支。
 *   enum mgmt_ds_id  : 数据集编号，对齐 NETCONF 的 datastore 概念
 *                      (running / candidate / operational)，另有 NONE 表示不指定。
 * ============================================================================
 */
#include "yang.h"

/* 【常量：mgmtd 的两个 UNIX socket 路径】注意它们是把**格式串与实参一起**写进
 * 宏的整体形式，展开后等于 "%s/mgmtd_fe.sock", frr_runstatedir。所以使用时应当写
 * sprintf(buf, MGMTD_FE_SOCK_NAME) 这样的调用式，而**不能**把它赋给 const char *
 * 指针(那样会编译不过或语义错乱)。
 *   FE : frontend 侧，各守护进程用它连到 mgmtd。
 *   BE : backend 侧，mgmtd 用它连到各守护进程。
 * 【易踩的坑】socket 位于 frr_runstatedir(运行期目录，如 /var/run 下)，它**不是**
 *   配置文件路径，不要写进持久化配置。 */
#define MGMTD_FE_SOCK_NAME "%s/mgmtd_fe.sock", frr_runstatedir
#define MGMTD_BE_SOCK_NAME "%s/mgmtd_be.sock", frr_runstatedir

/* 【常量组：长度上限】
 *   MGMTD_CLIENT_NAME_MAX_LEN(32) : 客户端(前端/后端)名字的最大长度。mgmtd 用它
 *     在栈上开缓冲并校验注册请求，超长名字应被显式拒绝。
 *   MGMTD_MAX_XPATH_LEN           : 配置路径 XPath 的最大长度，直接复用 YANG 模块
 *     的 XPATH_MAXLEN，目的是让两侧缓冲大小一致。
 * 【易踩的坑】MGMTD_MAX_XPATH_LEN 是**别名**而不是独立数值，不要在这里另写一个
 *   数字，否则与 yang.c 侧的缓冲大小不一致会造成静默截断。 */
#define MGMTD_CLIENT_NAME_MAX_LEN 32

#define MGMTD_MAX_XPATH_LEN XPATH_MAXLEN

/* 【枚举：mgmtd 统一返回码】mgmtd 对外 API 的返回值。
 *   MGMTD_SUCCESS(0)       : 成功。**取值为 0**，所以判断成功要写
 *                            == MGMTD_SUCCESS，不要写成“非零即成功”。
 *   MGMTD_INVALID_PARAM    : 入参非法(空指针、越界、未知枚举值等)。
 *   MGMTD_INTERNAL_ERROR   : mgmtd 内部错误(内存分配失败、状态机异常)。
 *   MGMTD_VALIDATION_ERROR : YANG 校验不通过(取值非法，或违反 must / when)。
 *   MGMTD_NO_CFG_CHANGES   : 提交本身成功，但**没有任何实际改动**(幂等提交)。
 *   MGMTD_VALUE_EXISTS     : 创建时目标已存在(冲突)。
 *   MGMTD_DS_LOCK_FAILED   : 获取数据集锁失败，通常意味着**另一个事务正持有锁**。
 *   MGMTD_DS_UNLOCK_FAILED : 释放数据集锁失败。
 *   MGMTD_UNKNOWN_FAILURE  : 兜底错误码。
 * 【易踩的坑】MGMTD_NO_CFG_CHANGES 不是失败，很多调用方却会把它当错误处理；它
 *   恰恰是“这次提交是空操作”的正常反馈。 */
enum mgmt_result {
	MGMTD_SUCCESS = 0,
	MGMTD_INVALID_PARAM,
	MGMTD_INTERNAL_ERROR,
	MGMTD_VALIDATION_ERROR,
	MGMTD_NO_CFG_CHANGES,
	MGMTD_VALUE_EXISTS,
	MGMTD_DS_LOCK_FAILED,
	MGMTD_DS_UNLOCK_FAILED,
	MGMTD_UNKNOWN_FAILURE
};

/* 【枚举：数据集(datastore) ID】对齐 NETCONF / YANG 的数据集模型。
 *   MGMTD_DS_NONE(0)    : 未指定。注意它取 0，与“结构体被零初始化”同形；凡是要
 *                         区分“没指定”和“明确指定为 NONE”的地方，必须另设标志。
 *   MGMTD_DS_RUNNING    : 正在运行的配置(已生效)。
 *   MGMTD_DS_CANDIDATE  : 候选配置(事务提交前的工作区)。
 *   MGMTD_DS_OPERATIONAL: 运行状态数据(只读，由各守护进程上报)。
 * 【配套宏】MGMTD_DS_MAX_ID 表示**合法 ID 的个数**，用于开按 ID 索引的数组；
 *   若误写成 MGMTD_DS_OPERATIONAL，数组会少一个元素，是典型的 off-by-one。 */
enum mgmt_ds_id {
	MGMTD_DS_NONE,
	MGMTD_DS_RUNNING,
	MGMTD_DS_CANDIDATE,
	MGMTD_DS_OPERATIONAL,
};
#define MGMTD_DS_MAX_ID (MGMTD_DS_OPERATIONAL + 1)

#endif /* _FRR_MGMTD_DEFINES_H */
