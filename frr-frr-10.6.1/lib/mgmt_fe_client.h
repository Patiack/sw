// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MGMTD Frontend Client Library api interfaces
 * Copyright (C) 2021  Vmware, Inc.
 *		       Pushpasis Sarkar <spushpasis@vmware.com>
 */

#ifndef _FRR_MGMTD_FE_CLIENT_H_
#define _FRR_MGMTD_FE_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "frrevent.h"
#include "mgmt_defines.h"
#include "mgmt_msg_native.h"

/*
 * 文件导读：mgmt_fe_client.h —— mgmtd 前端（frontend）客户端库的头文件
 *
 * 1. 这个文件干什么：
 *    FRR 里各守护进程 / 工具不再各自解析配置，而是作为“前端客户端”连到 mgmtd，
 *    由 mgmtd 统一管理配置数据（datastore）。本头文件声明了前端客户端库对外
 *    暴露的 API：创建 / 销毁客户端、建 / 销会话、加解锁数据存储（DS）、提交配置、
 *    get-data / edit / RPC 请求，以及以回调方式接收 mgmtd 的异步通知。
 *
 * 2. 核心数据结构：
 *    - struct mgmt_fe_client_cbs：一组回调函数指针（连接、会话、加锁、提交、
 *      get-tree、edit、rpc、异步通知、错误），由 mgmt_fe_client_create 注册。
 *    - struct mgmt_fe_client：不透明句柄，代表一个前端客户端实例。
 *    - mgmt_dbg_fe_client：本模块的调试开关记录（struct debug）。
 *
 * 3. 与其他模块的关系：
 *    依赖 frrevent.h（事件循环）、mgmt_defines.h（结果码 / DS 定义）、
 *    mgmt_msg_native.h（原生消息）；实现见 mgmt_fe_client.c。
 *
 * 4. 阅读顺序：
 *    先看 mgmt_fe_client_cbs 了解回调契约，再看 create / destroy、会话与各类请求
 *    发送函数，最后看调试宏。
 *
 * 5. 易踩的坑：
 *    - 回调中得到的 session_id 等多为不透明句柄，别当普通整数使用。
 *    - 会话 / 客户端 ID 用 MGMTD_SESSION_ID_NONE / MGMTD_CLIENT_ID_NONE（0）表示无。
 *    - 发送类 API 返回 0 表示成功，非 0 是底层 msg_conn_send_msg 的返回码。
 */
/***************************************************************
 * Macros
 ***************************************************************/

/*
 * The server port MGMTD daemon is listening for Backend Client
 * connections.
 */

/*
 * 前端消息处理的调优常量：
 *   MSG_PROC_DELAY_USEC  每轮消息处理前的延迟（微秒）；
 *   MAX_NUM_MSG_PROC     单次循环最多处理的消息数；
 *   MAX_NUM_MSG_WRITE    单次最多写出的消息数；
 *   MAX_MSG_LEN          单条消息最大长度（64KB）。
 */
#define MGMTD_FE_MSG_PROC_DELAY_USEC 10

#define MGMTD_FE_MAX_NUM_MSG_PROC  500
#define MGMTD_FE_MAX_NUM_MSG_WRITE 100
#define MGMTD_FE_MAX_MSG_LEN	   (64 * 1024)

/***************************************************************
 * Data-structures
 ***************************************************************/

/*
 * 会话 / 客户端 ID 的“无”值，均为 0（有效 ID 从 1 开始）。
 */
#define MGMTD_SESSION_ID_NONE 0

#define MGMTD_CLIENT_ID_NONE 0

/*
 * struct mgmt_fe_client -- 前端客户端实例的不透明句柄（结构体定义在 .c 中）。
 */
struct mgmt_fe_client;


/*
 * All the client specific information this library needs to
 * initialize itself, setup connection with MGMTD FrontEnd interface
 * and carry on all required procedures appropriately.
 *
 * FrontEnd clients need to initialise a instance of this structure
 * with appropriate data and pass it while calling the API
 * to initialize the library (See mgmt_fe_client_lib_init for
 * more details).
 */
/*
 * struct mgmt_fe_client_cbs —— 前端客户端回调集合
 * 调用者在 mgmt_fe_client_create 时提供，mgmtd 侧事件到达时逐一分发：
 *   client_connect_notify  连接建立 / 断开通知；
 *   client_session_notify  会话创建 / 销毁结果；
 *   lock_ds_notify         数据存储加锁 / 解锁结果；
 *   commit_config_notify   提交配置（含 validate_only / unlock / abort）结果；
 *   get_tree_notify        get-tree 结果返回；
 *   edit_notify            edit 结果返回；
 *   rpc_notify             RPC 结果返回；
 *   async_notification     来自后端的异步通知；
 *   error_notify           原生错误消息。
 * 返回 int 的若干回调用于把处理结果反馈给库内部（一般为 0）。
 */
struct mgmt_fe_client_cbs {
	void (*client_connect_notify)(struct mgmt_fe_client *client,
				      uintptr_t user_data, bool connected);

	void (*client_session_notify)(struct mgmt_fe_client *client,
				      uintptr_t user_data, uint64_t client_id,
				      bool create, bool success,
				      uintptr_t session_id,
				      uintptr_t user_session_client);

	void (*lock_ds_notify)(struct mgmt_fe_client *client, uintptr_t user_data,
			       uint64_t client_id, uintptr_t session_id,
			       uintptr_t user_session_client, uint64_t req_id, bool lock_ds,
			       bool success, enum mgmt_ds_id ds_id, char *errmsg_if_any);

	void (*commit_config_notify)(struct mgmt_fe_client *client, uintptr_t user_data,
				     uint64_t client_id, uintptr_t session_id,
				     uintptr_t user_session_client, uint64_t req_id, bool success,
				     enum mgmt_ds_id src_ds_id, enum mgmt_ds_id dst_ds_id,
				     bool validate_only, bool unlock, char *errmsg_if_any);

	/* Called when get-tree result is returned */
	int (*get_tree_notify)(struct mgmt_fe_client *client, uintptr_t user_data,
			       uint64_t client_id, uint64_t session_id, uintptr_t session_ctx,
			       uint64_t req_id, enum mgmt_ds_id ds_id, LYD_FORMAT result_type,
			       void *result, size_t len, int partial_error);

	/* Called when edit result is returned */
	int (*edit_notify)(struct mgmt_fe_client *client, uintptr_t user_data,
			   uint64_t client_id, uint64_t session_id,
			   uintptr_t session_ctx, uint64_t req_id,
			   const char *xpath);

	/* Called when RPC result is returned */
	int (*rpc_notify)(struct mgmt_fe_client *client, uintptr_t user_data,
			  uint64_t client_id, uint64_t session_id,
			  uintptr_t session_ctx, uint64_t req_id,
			  const char *result);

	/* Called with asynchronous notifications from backends */
	int (*async_notification)(struct mgmt_fe_client *client,
				  uintptr_t user_data, uint64_t client_id,
				  uint64_t session_id, uintptr_t session_ctx,
				  const char *result);

	/* Called when new native error is returned */
	int (*error_notify)(struct mgmt_fe_client *client, uintptr_t user_data,
			    uint64_t client_id, uint64_t session_id,
			    uintptr_t session_ctx, uint64_t req_id, int error,
			    const char *errstr);
};

/*
 * mgmt_dbg_fe_client -- 前端客户端库的调试开关（配合 debug.h 的 DEBUG_MODE_* 宏）。
 */
extern struct debug mgmt_dbg_fe_client;

/***************************************************************
 * API prototypes
 ***************************************************************/

/*
 * 日志 / 调试便捷宏（输出都会带前缀 "FE-CLIENT: 函数名"）：
 *   debug_fe_client        按 mgmt_dbg_fe_client 开关输出 debug 级日志；
 *   log_err_fe_client      直接输出 err 级日志（不受调试开关控制）；
 *   debug_check_fe_client  查询该模块调试是否开启。
 */
#define debug_fe_client(fmt, ...)                                              \
	DEBUGD(&mgmt_dbg_fe_client, "FE-CLIENT: %s: " fmt, __func__,           \
	       ##__VA_ARGS__)
#define log_err_fe_client(fmt, ...)                                            \
	zlog_err("FE-CLIENT: %s: ERROR: " fmt, __func__, ##__VA_ARGS__)
#define debug_check_fe_client()                                                \
	DEBUG_MODE_CHECK(&mgmt_dbg_fe_client, DEBUG_MODE_ALL)

/*
 * Initialize library and try connecting with MGMTD FrontEnd interface.
 *
 * params
 *    Frontend client parameters.
 *
 * master_thread
 *    Thread master.
 *
 * Returns:
 *    Frontend client lib handler (nothing but address of mgmt_fe_client)
 */
/*
 * mgmt_fe_client_create -- 初始化前端客户端库并尝试连接 mgmtd。
 * 参数：client_name 客户端名；cbs 回调集合；user_data 用户私有数据；
 *       event_loop 事件循环。返回：客户端句柄（struct mgmt_fe_client *），失败为 NULL。
 */
extern struct mgmt_fe_client *
mgmt_fe_client_create(const char *client_name, struct mgmt_fe_client_cbs *cbs,
		      uintptr_t user_data, struct event_loop *event_loop);

/*
 * Initialize library vty (adds debug support).
 *
 * This call should be added to your component when enabling other vty
 * code to enable mgmtd client debugs. When adding, one needs to also
 * add a their component in `xref2vtysh.py` as well.
 */
/*
 * mgmt_fe_client_lib_vty_init -- 初始化本库的 VTY（注册调试命令）。
 * 想启用 mgmtd 客户端调试的组件需调用它；同时要在 xref2vtysh.py 里登记该组件。
 */
extern void mgmt_fe_client_lib_vty_init(void);

/*
 * Create a new Session for a Frontend Client connection.
 *
 * lib_hndl
 *    Client library handler.
 *
 * client_id
 *    Unique identifier of client.
 *
 * user_client
 *    Client context.
 *
 * Returns:
 *    MGMTD_SUCCESS on success, MGMTD_* otherwise.
 */
/*
 * mgmt_fe_create_client_session -- 为前端客户端连接创建一个新会话。
 * 参数：client 客户端句柄；client_id 客户端唯一标识；user_client 用户上下文。
 * 返回：MGMTD_SUCCESS 成功，否则 MGMTD_* 错误码。
 */
extern enum mgmt_result
mgmt_fe_create_client_session(struct mgmt_fe_client *client, uint64_t client_id,
			      uintptr_t user_client);

/*
 * Delete an existing Session for a Frontend Client connection.
 *
 * lib_hndl
 *    Client library handler.
 *
 * client_id
 *    Unique identifier of client.
 *
 * Returns:
 *    0 on success, otherwise msg_conn_send_msg() return values.
 */
extern enum mgmt_result
/*
 * mgmt_fe_destroy_client_session -- 销毁前端客户端的某个会话。
 * 参数：client 客户端句柄；client_id 要销毁的客户端 ID。
 * 返回：MGMTD_SUCCESS 成功，否则底层 msg_conn_send_msg 的返回码。
 */
mgmt_fe_destroy_client_session(struct mgmt_fe_client *client,
			       uint64_t client_id);

/*
 * Send UN/LOCK_DS_REQ to MGMTD for a specific Datastore DS.
 *
 * lib_hndl
 *    Client library handler.
 *
 * session_id
 *    Client session ID.
 *
 * req_id
 *    Client request ID.
 *
 * ds_id
 *    Datastore ID (Running/Candidate/Oper/Startup)
 *
 * lock_ds
 *    TRUE for lock request, FALSE for unlock request.
 *
 * Returns:
 *    0 on success, otherwise msg_conn_send_msg() return values.
 */
/*
 * mgmt_fe_send_lockds_req -- 向 mgmtd 发送数据存储加锁 / 解锁请求。
 * 参数：client 句柄；session_id 会话 ID；req_id 请求 ID；ds_id 目标数据存储
 *       （Running / Candidate / Oper / Startup）；lock_ds 真为加锁、假为解锁；
 *       scok 是否允许短路。返回：0 成功，否则底层发送接口返回码。
 */
extern int mgmt_fe_send_lockds_req(struct mgmt_fe_client *client, uint64_t session_id,
				   uint64_t req_id, enum mgmt_ds_id ds_id, bool lock_ds, bool scok);

/*
 * Send SET_COMMMIT_REQ to MGMTD for one or more config data(s).
 *
 * lib_hndl
 *    Client library handler.
 *
 * session_id
 *    Client session ID.
 *
 * req_id
 *    Client request ID.
 *
 * src_ds_id
 *    Source datastore ID from where data needs to be committed from.
 *
 * dst_ds_id
 *    Destination datastore ID where data needs to be committed to.
 *
 * validate_only
 *    TRUE if data needs to be validated only, FALSE otherwise.
 *
 * abort
 *    TRUE if need to restore Src DS back to Dest DS, FALSE otherwise.
 *
 * unlock
 *    Passed through to the resulting reply.
 *
 * Returns:
 *    0 on success, otherwise msg_conn_send_msg() return values.
 */
/*
 * mgmt_fe_send_commit_req -- 向 mgmtd 发送提交配置请求。
 * 参数：client 句柄；session_id 会话 ID；req_id 请求 ID；src_ds_id 源数据存储；
 *       dst_ds_id 目标数据存储；validate_only 仅校验不提交；abort 回滚（把目标
 *       恢复为源）；unlock 是否在提交后解锁。返回：0 成功，否则底层发送接口返回码。
 */
extern int mgmt_fe_send_commit_req(struct mgmt_fe_client *client, uint64_t session_id,
				   uint64_t req_id, enum mgmt_ds_id src_ds_id,
				   enum mgmt_ds_id dst_ds_id, bool validate_only, bool abort,
				   bool unlock);

/*
 * Send GET-DATA to MGMTD daemon.
 *
 * client
 *    Client object.
 *
 * session_id
 *    Client session ID.
 *
 * req_id
 *    Client request ID.
 *
 * datastore
 *    Datastore for getting data.
 *
 * result_type
 *    The LYD_FORMAT of the result.
 *
 * flags
 *    Flags to control the behavior of the request.
 *
 * defaults
 *    Options to control the reporting of default values.
 *
 * xpath
 *    the xpath to get.
 *
 * Returns:
 *    0 on success, otherwise msg_conn_send_msg() return values.
 */
/*
 * mgmt_fe_send_get_data_req -- 向 mgmtd 发送 get-data 请求。
 * 参数：client 句柄；session_id 会话；req_id 请求 ID；datastore 目标数据存储；
 *       result_type 结果编码格式（LYD_FORMAT）；flags 行为标志；defaults 默认值报告
 *       选项；xpath 要获取的节点路径。返回：0 成功，否则底层发送接口返回码。
 */
extern int mgmt_fe_send_get_data_req(struct mgmt_fe_client *client,
				     uint64_t session_id, uint64_t req_id,
				     uint8_t datastore, LYD_FORMAT result_type,
				     uint8_t flags, uint8_t defaults,
				     const char *xpath);

/*
 * Send EDIT to MGMTD daemon.
 *
 * client
 *    Client object.
 *
 * session_id
 *    Client session ID.
 *
 * req_id
 *    Client request ID.
 *
 * datastore
 *    Datastore for editing.
 *
 * request_type
 *    The LYD_FORMAT of the request.
 *
 * flags
 *    Flags to control the behavior of the request.
 *
 * operation
 *    NB_OP_* operation to perform.
 *
 * xpath
 *    the xpath to edit.
 *
 * data
 *    the data tree.
 *
 * Returns:
 *    0 on success, otherwise msg_conn_send_msg() return values.
 */
/*
 * mgmt_fe_send_edit_req -- 向 mgmtd 发送 edit 请求。
 * 参数：client 句柄；session_id 会话；req_id 请求 ID；datastore 目标数据存储；
 *       request_type 请求编码格式（LYD_FORMAT）；flags 行为标志；operation NB_OP_*
 *       操作类型；xpath 编辑路径；data 数据树。返回：0 成功，否则底层发送接口返回码。
 */
extern int mgmt_fe_send_edit_req(struct mgmt_fe_client *client,
				 uint64_t session_id, uint64_t req_id,
				 uint8_t datastore, LYD_FORMAT request_type,
				 uint8_t flags, uint8_t operation,
				 const char *xpath, const char *data);

/*
 * Send RPC request to MGMTD daemon.
 *
 * client
 *    Client object.
 *
 * session_id
 *    Client session ID.
 *
 * req_id
 *    Client request ID.
 *
 * result_type
 *    The LYD_FORMAT of the result.
 *
 * xpath
 *    the xpath of the RPC.
 *
 * data
 *    the data tree.
 *
 * Returns:
 *    0 on success, otherwise msg_conn_send_msg() return values.
 */
/*
 * mgmt_fe_send_rpc_req -- 向 mgmtd 发送 RPC 请求。
 * 参数：client 句柄；session_id 会话；req_id 请求 ID；request_type 结果编码格式；
 *       xpath RPC 路径；data 数据树。返回：0 成功，否则底层发送接口返回码。
 */
extern int mgmt_fe_send_rpc_req(struct mgmt_fe_client *client,
				uint64_t session_id, uint64_t req_id,
				LYD_FORMAT request_type, const char *xpath,
				const char *data);

/*
 * Destroy library and cleanup everything.
 */
/*
 * mgmt_fe_client_destroy -- 销毁客户端库并清理全部资源。参数 client 为句柄。
 */
extern void mgmt_fe_client_destroy(struct mgmt_fe_client *client);

/*
 * Get count of open sessions.
 */
/*
 * mgmt_fe_client_session_count -- 返回该客户端当前打开的会话数。
 */
extern uint mgmt_fe_client_session_count(struct mgmt_fe_client *client);

/*
 * True if the current handled message is being short-circuited
 */
extern bool
/*
 * mgmt_fe_client_current_msg_short_circuit -- 返回真表示当前正在处理的消息
 * 走了“短路”路径（同步直返，而非异步等待）。参数为客户端句柄。
 */
mgmt_fe_client_current_msg_short_circuit(struct mgmt_fe_client *client);

/**
 * Get the name of the client
 *
 * Args:
 *	The client object.
 *
 * Return:
 *	The name of the client.
 */
/*
 * mgmt_fe_client_name -- 返回客户端名。参数为客户端句柄，返回其名字字符串。
 */
extern const char *mgmt_fe_client_name(struct mgmt_fe_client *client);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_MGMTD_FE_CLIENT_H_ */
