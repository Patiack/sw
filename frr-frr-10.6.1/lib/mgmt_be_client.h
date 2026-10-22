// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MGMTD Backend Client Library api interfaces
 * Copyright (C) 2021  Vmware, Inc.
 *		       Pushpasis Sarkar <spushpasis@vmware.com>
 */

/*
 * 文件导读：mgmtd 北向客户端（backend client）头文件
 *
 * 一、在整体架构中的位置
 *   mgmtd 是 FRR 的集中式管理守护进程，内部用 libyang 维护配置数据树与
 *   运行状态数据树。围绕它有三类参与者：
 *     - 前端客户端（frontend client）：vtysh、NETCONF 等，向 mgmtd 下发
 *       或查询配置；
 *     - 后端客户端（backend client）：zebra、bgpd 等功能 daemon，以
 *       backend 身份接入 mgmtd，负责真正“拥有”并落地自己那份配置/状态；
 *     - mgmtd 本身：协调前后端，维护事务（commit / rollback）。
 *
 *   本文件描述的就是 backend 客户端库对外暴露的接口。各 daemon 通过
 *   mgmt_be_client_create() 创建客户端并连到 mgmtd，再在回调里声明自己
 *   负责的 config / oper / notify / rpc xpath，mgmtd 按 xpath 把对应的
 *   配置变更路由到正确的 daemon。
 *
 * 二、本组三层的分工
 *   - mgmt_msg.h / mgmt_msg.c：最底层的通用消息收发骨架（连接管理、消息
 *     分片、读写队列、与事件循环绑定），与具体业务无关；
 *   - mgmt_msg_native.h：原生消息（非 protobuf）的 TLV 编解码定义，是
 *     backend / frontend 客户端与 mgmtd 通信使用的消息格式；
 *   - mgmt_be_client.h / mgmt_be_client.c：在 mgmt_msg 骨架之上封装出
 *     backend 客户端语义（注册、事务、配置下发与回报等）。
 *
 * 三、阅读建议
 *   先看本头文件的 cbs 回调结构，理解“客户端需要向 mgmtd 提供什么”，再回到
 *   mgmt_be_client.c 看连接建立、注册以及事务提交 / 回滚的调用链。
 */
#ifndef _FRR_MGMTD_BE_CLIENT_H_
#define _FRR_MGMTD_BE_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "northbound.h"
#include "mgmt_defines.h"

/***************************************************************
 * Constants
 ***************************************************************/

/*
 * 客户端侧的流量控制常量：
 *   MGMTD_BE_MAX_NUM_MSG_PROC  单次事件循环最多处理的待处理消息条数；
 *   MGMTD_BE_MAX_NUM_MSG_WRITE 单次事件循环最多写出的消息条数；
 *   MGMTD_BE_MAX_MSG_LEN       单条消息长度上限（64KB），超出按非法处理。
 */
#define MGMTD_BE_MAX_NUM_MSG_PROC  500
#define MGMTD_BE_MAX_NUM_MSG_WRITE 1000
#define MGMTD_BE_MAX_MSG_LEN	   (64 * 1024)

/*
 * 代表“容器节点”的占位字符串。libyang 中容器没有叶值，某些只接受字符串
 * 值的接口用该常量表示一个容器节点，便于统一处理。
 */
#define MGMTD_BE_CONTAINER_NODE_VAL "<<container>>"

/***************************************************************
 * Data-structures
 ***************************************************************/

/*
 * 单条 xpath 上允许注册的 backend 客户端数量上限。多个 daemon 可能订阅
 * 同一段配置，mgmtd 侧为每个 xpath 最多保存这么多个订阅者。
 */
#define MGMTD_BE_MAX_CLIENTS_PER_XPATH_REG 32

/*
 * backend 客户端对象的不透明（opaque）前向声明。其结构体定义在
 * mgmt_be_client.c 内部，调用者只能通过本头文件提供的接口操作它。
 */
struct mgmt_be_client;

/*
 * 事务上下文：backend 客户端在一次配置事务中随回调透传的私有数据。
 * user_ctx 指向一个调用者自行解释的指针数组（通常是每条 xpath 对应的
 * 处理上下文），生命周期由调用者管理。
 */
struct mgmt_be_client_txn_ctx {
	uintptr_t *user_ctx;
};

/**
 * Backend client callbacks.
 *
 * Callbacks:
 *	client_connect_notify: called when connection is made/lost to mgmtd.
 *	txn_notify: called when a txn has been created
 *	config_xpaths: config xpaths to register with mgmtd.
 *	nconfig_xpaths: number of config xpaths.
 *	oper_xpaths: oper-state xpaths to register with mgmtd.
 *	noper_xpaths: number of oper-state xpaths.
 *	notify_xpaths: notify xpaths to register with mgmtd.
 *	nnotify_xpaths: number of notify xpaths.
 *	rpc_xpaths: rpc xpaths to register with mgmtd.
 *	nrpc_xpaths: number of rpc xpaths.
 *
 */
/*
 * backend 客户端需要向 mgmtd 提供的回调与 xpath 声明集合：
 *   client_connect_notify 与 mgmtd 连接建立 / 断开时的通知；
 *   config_xpaths / nconfig_xpaths   本 daemon 负责落地的配置 xpath；
 *   oper_xpaths / noper_xpaths       本 daemon 提供的运行状态 xpath；
 *   notify_xpaths / nnotify_xpaths   需要订阅通知的 xpath；
 *   rpc_xpaths / nrpc_xpaths         本 daemon 实现的 rpc xpath。
 * mgmtd 依据这些 xpath 把配置变更、状态查询、rpc 调用路由到正确的 daemon。
 */
struct mgmt_be_client_cbs {
	void (*client_connect_notify)(struct mgmt_be_client *client, uintptr_t usr_data,
				      bool connected);
	const char *const *config_xpaths;
	uint nconfig_xpaths;
	const char *const *oper_xpaths;
	uint noper_xpaths;
	const char *const *notify_xpaths;
	uint nnotify_xpaths;
	const char *const *rpc_xpaths;
	uint nrpc_xpaths;
};

/***************************************************************
 * Global data exported
 ***************************************************************/

/*
 * backend 客户端库使用的调试开关（对应 debug mgmtd backend-client ...）。
 */
extern struct debug mgmt_dbg_be_client;

/*
 * 描述 backend 客户端自身（mgmtd 北向）功能的 YANG 模块信息，供各组件在
 * 初始化时注册到 YANG 模块表使用。
 */
extern const struct frr_yang_module_info frr_backend_info;

/***************************************************************
 * API prototypes
 ***************************************************************/

/*
 * 日志 / 调试宏：debug_be_client 仅在调试开关打开时输出（自动带上函数名）；
 * log_err_be_client 始终以错误级别输出；debug_check_be_client 用于判断
 * 调试是否开启，以便跳过昂贵的调试数据构造。
 */
#define debug_be_client(fmt, ...)                                              \
	DEBUGD(&mgmt_dbg_be_client, "BE-CLIENT: %s: " fmt, __func__,           \
	       ##__VA_ARGS__)
#define log_err_be_client(fmt, ...)                                            \
	zlog_err("BE-CLIENT: %s: ERROR: " fmt, __func__, ##__VA_ARGS__)
#define debug_check_be_client()                                                \
	DEBUG_MODE_CHECK(&mgmt_dbg_be_client, DEBUG_MODE_ALL)

/**
 * Create backend client and connect to MGMTD.
 *
 * Args:
 *	client_name: the name of the client
 *	cbs: callbacks for various events.
 *	event_loop: the main event loop.
 *
 * Returns:
 *    Backend client object.
 */
/*
 * 创建 backend 客户端并尝试连接 mgmtd。
 *   name        客户端名称，用于日志与注册标识；
 *   cbs         回调与 xpath 声明集合（见 struct mgmt_be_client_cbs）；
 *   user_data   调用者私有数据，会随各类回调回传；
 *   event_loop  主事件循环，客户端的所有 socket 事件都挂到它上面。
 * 返回客户端对象指针，失败返回 NULL。通常在组件初始化时调用一次。
 */
extern struct mgmt_be_client *
mgmt_be_client_create(const char *name, struct mgmt_be_client_cbs *cbs,
		      uintptr_t user_data, struct event_loop *event_loop);


/**
 * mgmt_be_send_ds_delete_notification() - Send a datastore delete notification.
 */
/*
 * 向 mgmtd 发送“删除了某 xpath 处数据”的通知，用于 daemon 侧直接改动了
 * 自己拥有的配置后同步给 mgmtd。返回 0 成功，负值失败。
 */
extern int mgmt_be_send_ds_delete_notification(const char *path);

/**
 * mgmt_be_send_ds_patch_notification() - Send a datastore YANG patch notification.
 */
/*
 * 向 mgmtd 发送对某路径的 YANG patch 通知，tree 为增量修改的数据子树。
 * 返回 0 成功，负值失败。
 */
extern int mgmt_be_send_ds_patch_notification(const char *path, const struct lyd_node *tree);

/**
 * mgmt_be_send_ds_replace_notification() - Send a datastore replace notification.
 */
/*
 * 向 mgmtd 发送对某路径的“整体替换”通知，tree 为替换后的完整子树；
 * refer_id 用于把该通知与发起它的操作关联起来。返回 0 成功，负值失败。
 */
extern int mgmt_be_send_ds_replace_notification(const char *path, const struct lyd_node *tree,
						uint64_t refer_id);

/*
 * Initialize library vty (adds debug support).
 *
 * This call should be added to your component when enabling other vty code to
 * enable mgmtd client debugs. When adding, one needs to also add a their
 * component in `xref2vtysh.py` as well.
 */
/*
 * 注册本库的 vty 命令（主要是调试命令支持）。组件若启用相关 vty 代码，
 * 需调用本函数以开启 mgmtd 客户端调试；同时还要在 xref2vtysh.py 中登记
 * 对应组件。
 */
extern void mgmt_be_client_lib_vty_init(void);

/*
 * Destroy backend client and cleanup everything.
 */
/*
 * 销毁 backend 客户端：断开与 mgmtd 的连接并释放其占用的资源。
 */
extern void mgmt_be_client_destroy(struct mgmt_be_client *client);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_MGMTD_BE_CLIENT_H_ */
