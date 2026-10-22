// SPDX-License-Identifier: GPL-2.0-or-later
/* PTM Library
 * Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * ============================================================================
 * 【文件导读】lib/ptm_lib.h —— PTM（Prescriptive Topology Manager）客户端库
 * ============================================================================
 * 职责：
 *   PTM 是 Cumulus 提出的“拓扑驱动”机制：独立的 ptmd 守护进程读取拓扑
 *   描述文件，通过与网络设备交互来校验链路是否符合预期。本头文件定义了
 *   “客户端”（如 bgpd、zebra）与 ptmd 通信时的消息格式与回调签名的公共
 *   约定。客户端通过 Unix socket 收发文本消息，消息体用 CSV 编码
 *   （见 lib/csv.h）。
 *
 * 核心数据结构：
 *   ptmlib_msg_type  —— 消息类型枚举（通知 / 命令 / 响应）。
 *   ptmlib_mod_type  —— 目标模块枚举（BFD / LLDP）。
 *   ptm_lib_handle_t —— 客户端句柄：保存客户端名与三个回调。
 *   回调函数指针类型 —— 处理各类消息的入口签名。
 *
 * 与其他模块的关系：
 *   - 与 lib/ptm_lib.c 成对：c 文件负责编解码与收发，本头只做声明。
 *   - 使用 lib/csv.h 作为消息体编码格式。
 *   - 典型使用方是 BFD 集成：通过 PTM 查询或启停 BFD 会话。
 *
 * 阅读顺序：
 *   1. 先看 4 个长度常量与 4 个命令字符串宏，理解消息的外部契约。
 *   2. 再看两个枚举（消息类型、模块类型）。
 *   3. 再看回调类型与句柄结构体。
 *   4. 最后看文件末尾的编解码 / 收发函数原型。
 *
 * 易踩的坑：
 *   - 消息头长度固定为 37 字节（PTMLIB_MSG_HDR_LEN），所有消息都带。
 *   - PTMLIB_MSG_SZ 是整条消息缓冲的上限（1024）。
 *   - 回调签名统一为 int (*)(void *arg, void *ctxt)，ctxt 是消息上下文，
 *     需由回调自行按需解析。
 *   - 消息类型枚举从 1 开始（不是 0），0 留作非法值。
 * ============================================================================
 */

#ifndef __PTM_LIB_H__
/* include guard：防止本头文件被重复包含。 */
#define __PTM_LIB_H__

/* 以 C 语言链接方式导出，供 C++ 调用者使用。 */
#ifdef __cplusplus
extern "C" {
#endif

/* PTMLIB_MSG_SZ：单条 PTM 消息的最大尺寸（字节），用于分配缓冲区。 */
#define PTMLIB_MSG_SZ           1024
/* PTMLIB_MSG_HDR_LEN：PTM 消息头的固定长度（37 字节），
 * 由 CSV 头部记录（长度 / 版本 / 类型 / 命令号 / 客户端名）决定。
 */
#define PTMLIB_MSG_HDR_LEN      37
/* PTMLIB_MSG_VERSION：当前 PTM 消息协议的版本号。 */
#define PTMLIB_MSG_VERSION      2
/* PTMLIB_MAXNAMELEN：客户端名的最大长度（含结尾 0）。 */
#define PTMLIB_MAXNAMELEN       32

/* PTMLIB_CMD_*：PTM 支持的命令字符串（消息体里 cmd 字段的取值）。
 *   GET_STATUS     —— 查询 PTM 状态。
 *   GET_BFD_CLIENT —— 获取 BFD 客户端信息。
 *   START_BFD_SESS —— 启动一个 BFD 会话。
 *   STOP_BFD_SESS  —— 停止一个 BFD 会话。
 */
#define    PTMLIB_CMD_GET_STATUS        "get-status"
#define    PTMLIB_CMD_GET_BFD_CLIENT    "get-bfd-client"
#define    PTMLIB_CMD_START_BFD_SESS    "start-bfd-sess"
#define    PTMLIB_CMD_STOP_BFD_SESS     "stop-bfd-sess"

/* ptmlib_msg_type：PTM 消息类型。
 *   PTMLIB_MSG_TYPE_NOTIFICATION —— 通知（ptmd 主动推送）。
 *   PTMLIB_MSG_TYPE_CMD          —— 命令（客户端发往 ptmd）。
 *   PTMLIB_MSG_TYPE_RESPONSE     —— 响应（ptmd 对命令的回复）。
 * 注意从 1 开始编号，0 留作非法值。
 */
typedef enum {
	PTMLIB_MSG_TYPE_NOTIFICATION = 1,
	PTMLIB_MSG_TYPE_CMD,
	PTMLIB_MSG_TYPE_RESPONSE,
} ptmlib_msg_type;

/* ptmlib_mod_type：PTM 消息面向的目标模块。
 *   MODULE_BFD —— BFD 模块。
 *   MODULE_LLDP —— LLDP 模块。
 *   MODULE_MAX —— 模块数量上界（用于数组定界），非有效模块。
 */
typedef enum {
	MODULE_BFD = 0,
	MODULE_LLDP,
	MODULE_MAX,
} ptmlib_mod_type;

/* ptm_cmd_cb：处理 PTM 命令消息的回调签名。 */
typedef int (*ptm_cmd_cb)(void *data, void *arg);
/* ptm_notify_cb：处理 PTM 通知消息的回调签名。 */
typedef int (*ptm_notify_cb)(void *data, void *arg);
/* ptm_response_cb：处理 PTM 响应消息的回调签名。 */
typedef int (*ptm_response_cb)(void *data, void *arg);
/* ptm_log_cb：日志回调（可变参数，形如 printf）。 */
typedef int (*ptm_log_cb)(void *data, void *arg, ...);

/* ptm_lib_handle_t：PTM 客户端句柄。
 *   client_name —— 客户端标识名，用于注册到 ptmd。
 *   cmd_cb      —— 处理命令消息的回调。
 *   notify_cb   —— 处理通知消息的回调。
 *   response_cb —— 处理响应消息的回调。
 * 由 ptm_lib_register 分配并初始化。
 */
typedef struct ptm_lib_handle_s {
	char client_name[PTMLIB_MAXNAMELEN];
	ptm_cmd_cb cmd_cb;
	ptm_notify_cb notify_cb;
	ptm_response_cb response_cb;
} ptm_lib_handle_t;

/* 以下为本库对外提供的函数原型。 */
/* Prototypes */
/* ptm_lib_process_msg：从 fd 读取并解析一条消息，按类型分派到句柄的回调。
 * 参数：句柄、socket fd、输入缓冲、缓冲长度、传给回调的上下文 ctxt。
 */
int ptm_lib_process_msg(ptm_lib_handle_t *hdl, int fd, char *inbuf, int inlen, void *ctxt);
/* ptm_lib_register：创建并注册一个客户端句柄，绑定三类回调。 */
ptm_lib_handle_t *ptm_lib_register(char *client_name, ptm_cmd_cb cmd_cb, ptm_notify_cb notify_cb,
				   ptm_response_cb response_cb);
/* ptm_lib_deregister：注销句柄并释放其内存。 */
void ptm_lib_deregister(ptm_lib_handle_t *hdl);
/* ptm_lib_find_key_in_msg：在消息上下文里按 key 查找并取出对应的 value。 */
int ptm_lib_find_key_in_msg(void *ctxt, const char *key, char *val);
/* ptm_lib_init_msg：初始化一条消息上下文，可基于已有上下文 in_ctxt 克隆。 */
int ptm_lib_init_msg(ptm_lib_handle_t *hdl, int msgType, int msgLen, void *in_ctxt,
		     void **out_ctxt);
/* ptm_lib_append_msg：向消息追加一个 key / value 键值对。 */
int ptm_lib_append_msg(ptm_lib_handle_t *hdl, void *ctxt, const char *key, const char *value);
/* ptm_lib_complete_msg：完成消息，把 CSV 内容序列化到 buf，
 * 并通过 *len 返回实际长度。
 */
int ptm_lib_complete_msg(ptm_lib_handle_t *hdl, void *ctxt, char *buf, int *len);
/* ptm_lib_cleanup_msg：释放消息上下文及其内部 CSV 资源。 */
int ptm_lib_cleanup_msg(ptm_lib_handle_t *hdl, void *ctxt);

/* 结束 C 语言链接导出块。 */
#ifdef __cplusplus
}
#endif

/* 结束 include guard。 */
#endif
