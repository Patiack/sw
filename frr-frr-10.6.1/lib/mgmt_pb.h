// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MGMTD protobuf main header file
 * Copyright (C) 2021  Vmware, Inc.
 *		       Pushpasis Sarkar <spushpasis@vmware.com>
 */

#ifndef _FRR_MGMTD_PB_H_
#define _FRR_MGMTD_PB_H_

/* ============================================================================
 * 【文件导读】mgmt_pb.h —— 给 protobuf-c 生成代码套一层“短名字”别名
 * ============================================================================
 * 【背景：mgmtd 为什么要用 protobuf】FRR 的 mgmtd(配置管理守护进程)要跟多个
 * 前端/后端进程交换结构化数据(如“把这些 YANG 节点的值下发给你”)。protobuf 的
 * 优势是**跨进程的稳定二进制格式 + 向前向后兼容的字段演进**。消息定义写在
 * .proto 里，由 protobuf-c 生成 `mgmt.pb-c.h`。
 *
 * 【本文件的唯一作用：改名】protobuf-c 的命名规则会把包名拼进符号里，于是生成
 * 的名字长成 `mgmtd__yang_data_init`(双下划线是包名分隔)。本文件用一组宏把它
 * 改写成 FRR 代码里惯用的 `mgmt_yang_data_init`。**没有新类型、没有新逻辑**，
 * 纯粹是给生成代码做一层薄别名，便于阅读和书写。
 *
 * 【易踩的坑】
 *   ① 这些宏只覆盖 *_init 一类操作(还有 free/pack 等由别处处理)，用之前先确认
 *      想用的 protobuf-c 函数是否在别名表里，没在就直接写原名。
 *   ② 宏是**无参数检查**的文本替换，若把宏名当函数指针取址(如 &mgmt_yang_data_init)
 *      会编译失败——宏不能取地址。需要函数指针时用原名。
 *   ③ `#include "lib/mgmt.pb-c.h"` 用的是“带 lib/ 前缀”的路径，说明该生成文件
 *      放在构建目录里，且 FRR 的头文件搜索路径同时包含源码根与构建根两部分。
 * ============================================================================
 */
#include "lib/mgmt.pb-c.h"

/* 【别名】各消息类型的初始化宏。左侧是 FRR 惯用名，右侧是 protobuf-c 生成名。
 * 括号里的 ptr 即消息结构体指针；语义都是“把该消息的字段置为 protobuf 默认值”。 */
#define mgmt_yang_data_xpath_init(ptr) mgmtd__yang_data_xpath__init(ptr)

#define mgmt_yang_data_value_init(ptr) mgmtd__yang_data_value__init(ptr)

#define mgmt_yang_data_init(ptr) mgmtd__yang_data__init(ptr)

#define mgmt_yang_data_reply_init(ptr) mgmtd__yang_data_reply__init(ptr)

#define mgmt_yang_cfg_data_req_init(ptr) mgmtd__yang_cfg_data_req__init(ptr)

#define mgmt_yang_get_data_req_init(ptr) mgmtd__yang_get_data_req__init(ptr)

#endif /* _FRR_MGMTD_PB_H_ */
