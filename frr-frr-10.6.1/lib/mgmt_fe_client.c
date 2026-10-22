// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MGMTD Frontend Client Library api interfaces
 * Copyright (C) 2021  Vmware, Inc.
 *		       Pushpasis Sarkar <spushpasis@vmware.com>
 */

/*
 * 文件导读：mgmt_fe_client.c —— mgmtd 前端（frontend）客户端库的实现
 *
 * 1. 这个文件干什么：
 *    实现 mgmt_fe_client.h 声明的 API，让各守护进程作为“前端客户端”通过 unix
 *    socket（MGMTD_FE_SOCK_NAME）连到 mgmtd，用原生 TLV 消息收发配置请求：
 *    建 / 销会话、加解锁数据存储、提交配置、get-data / edit / RPC，并把 mgmtd 的
 *    回复 / 通知分发到用户注册的回调。
 *
 * 2. 核心数据结构：
 *    - struct mgmt_fe_client        ：客户端实例，内嵌 msg_client（连接管理），
 *                                     另存名字、回调集合、会话链表。
 *    - struct mgmt_fe_client_session：一个客户端会话，记录 client_id / session_id、
 *                                     用户上下文，以及“已发出未回复”的消息数组。
 *    - __fe_client                  ：进程内唯一的客户端指针（当前每进程仅一个）。
 *    - mgmt_sessions 链表           ：用 PREDECL_LIST / DECLARE_LIST 生成的会话链表。
 *
 * 3. 关键机制：
 *    - sent_msgs 数组：发出的请求按 session_id / req_id 暂存，收到回复再取出，
 *      用于把“回复”与原始“请求”配对（fe_client_push_sent_msg / pop_sent_msg）。
 *    - 回调分发集中在 fe_client_handle_native_msg，按消息码 switch 到对应回调。
 *
 * 4. 与其他模块的关系：
 *    依赖 mgmt_msg.c（msg_client 连接框架）、mgmt_msg_native.c（原生消息编解码）、
 *    debug.h（调试开关）；是后端客户端 mgmt_be_client 的前端对应物。
 *
 * 5. 阅读顺序建议：
 *    先看两个结构体与 sent_msgs 收发配对辅助函数 -> 各 send_xxx_req ->
 *    fe_client_handle_native_msg（核心分发）-> 连接 / 断连回调与 create / destroy。
 *
 * 6. 易踩的坑：
 *    - 发送前先入队、发送失败再出队，顺序不能乱，否则回复到达时找不到原请求。
 *    - 断连时会遍历删除所有会话并回调通知，注意避免重复释放。
 */
#include <zebra.h>
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "libfrr.h"
#include "mgmt_fe_client.h"
#include "mgmt_msg.h"
#include "mgmt_msg_native.h"
#include "network.h"
#include "stream.h"
#include "sockopt.h"

/*
 * 引入 clippy 生成的 CLI 代码（DEFPY 命令的声明 / 定义部分）。
 */
#include "lib/mgmt_fe_client_clippy.c"

/*
 * 预声明会话链表类型 mgmt_sessions（基于 FRR 的链表模板）。
 */
PREDECL_LIST(mgmt_sessions);

/*
 * struct mgmt_fe_client_session —— 单个前端会话
 * client_id ：前端客户端自己使用的标识（发送时作为 req_id）；
 * session_id：mgmtd 为该会话分配的标识（回复中作为 refer_id 带回）；
 * client    ：所属客户端；user_ctx：用户上下文（回调时原样回传）；
 * sent_msgs ：本会话已发出但尚未收到回复的消息数组，用于请求 / 回复配对；
 * list_linkage：挂到客户端会话链表上的节点。
 */
struct mgmt_fe_client_session {
	uint64_t client_id;  /* FE client identifies itself with this ID */
	uint64_t session_id; /* FE adapter identified session with this ID */
	struct mgmt_fe_client *client;
	uintptr_t user_ctx;
	struct mgmt_msg_header **sent_msgs; /* un-replied sent messages */

	struct mgmt_sessions_item list_linkage;
};

/*
 * 展开 mgmt_sessions 链表的增删 / 遍历等操作函数。
 */
DECLARE_LIST(mgmt_sessions, struct mgmt_fe_client_session, list_linkage);

/*
 * 本文件使用的内存类型：客户端实例、客户端名、会话，供内存统计 / 泄漏排查。
 */
DEFINE_MTYPE_STATIC(LIB, MGMTD_FE_CLIENT, "frontend client");
DEFINE_MTYPE_STATIC(LIB, MGMTD_FE_CLIENT_NAME, "frontend client name");
DEFINE_MTYPE_STATIC(LIB, MGMTD_FE_SESSION, "frontend session");

/*
 * struct mgmt_fe_client —— 前端客户端实例
 * client    ：内嵌的连接管理对象（含 socket、事件、消息缓冲等）；
 * name      ：客户端名；cbs：用户注册的回调集合；user_data：用户私有数据；
 * sessions  ：会话链表；
 * sent_msgs ：不隶属于任何会话的“已发未回”消息（如会话创建请求）。
 */
struct mgmt_fe_client {
	struct msg_client client;
	char *name;
	struct mgmt_fe_client_cbs cbs;
	uintptr_t user_data;
	struct mgmt_sessions_head sessions;
	struct mgmt_msg_header **sent_msgs; /* un-replied session-less sent messages */
};

/*
 * FOREACH_SESSION_IN_LIST -- 安全遍历某客户端的所有会话（遍历中可删除当前项）。
 */
#define FOREACH_SESSION_IN_LIST(client, session)                               \
	frr_each_safe (mgmt_sessions, &(client)->sessions, (session))

/*
 * mgmt_dbg_fe_client -- 前端客户端调试开关的全局实例；
 * conf 为写入配置文件的命令（debug mgmt client frontend），desc 为描述。
 */
struct debug mgmt_dbg_fe_client = {
	.conf = "debug mgmt client frontend",
	.desc = "Management frontend client operations"
};

/* NOTE: only one client per proc for now. */
/*
 * __fe_client -- 进程内唯一的前端客户端指针。当前实现限制每个进程只能有一个客户端。
 */
static struct mgmt_fe_client *__fe_client;

/*
 * dsid2name -- 把数据存储 ID 转成可读名字（日志用）。未知 ID 返回固定字符串。
 */
static inline const char *dsid2name(enum mgmt_ds_id id)
{
	switch (id) {
	case MGMTD_DS_NONE:
		return "none";
	case MGMTD_DS_RUNNING:
		return "running";
	case MGMTD_DS_CANDIDATE:
		return "candidate";
	case MGMTD_DS_OPERATIONAL:
		return "operational";
	default:
		return "unknown-datastore-id";
	}
}

static struct mgmt_fe_client_session *
/*
 * mgmt_fe_find_session_by_client_id -- 按 client_id 在会话链表中查找会话。
 * 返回匹配的会话指针；未找到返回 NULL。
 */
mgmt_fe_find_session_by_client_id(struct mgmt_fe_client *client,
				  uint64_t client_id)
{
	struct mgmt_fe_client_session *session;

	FOREACH_SESSION_IN_LIST (client, session) {
		if (session->client_id == client_id) {
			debug_fe_client("Found session-id %" PRIu64
					" using client-id %" PRIu64,
					session->session_id, client_id);
			return session;
		}
	}
	debug_fe_client("Session not found using client-id %" PRIu64, client_id);
	return NULL;
}

static struct mgmt_fe_client_session *
/*
 * mgmt_fe_find_session_by_session_id -- 按 session_id 在会话链表中查找会话。
 * 返回匹配的会话指针；未找到返回 NULL。
 */
mgmt_fe_find_session_by_session_id(struct mgmt_fe_client *client,
				   uint64_t session_id)
{
	struct mgmt_fe_client_session *session;

	FOREACH_SESSION_IN_LIST (client, session) {
		if (session->session_id == session_id) {
			debug_fe_client("Found session of client-id %" PRIu64
					" using session-id %" PRIu64,
					session->client_id, session_id);
			return session;
		}
	}
	debug_fe_client("Session not found using session-id %" PRIu64,
			session_id);
	return NULL;
}

/**
 * fe_client_push_sent_msg() - save the sent message for later reference on reply
 */
/*
 * fe_client_push_sent_msg -- 把已发送的消息压入“待回复”队列，供收到回复时配对。
 * 参数：client 客户端；msg 已发送的消息头；ret 发送结果。
 * 行为：若 ret 非 0（发送失败）则直接释放 msg 并返回 ret；否则按 msg->refer_id
 *       找到所属会话，把消息压入该会话的 sent_msgs（无 refer_id 则压入客户端的
 *       通用 sent_msgs）。返回 0 表示已入队。
 * 坑：需在合适的时机（发送前或发送后）入队，否则回复先到会找不到原消息。
 */
static int fe_client_push_sent_msg(struct mgmt_fe_client *client, struct mgmt_msg_header *msg,
				   int ret)
{
	struct mgmt_fe_client_session *session = NULL;

	if (ret) {
		mgmt_msg_native_free_msg(msg);
		return ret;
	}
	if (msg->refer_id) {
		session = mgmt_fe_find_session_by_session_id(client, msg->refer_id);
		if (!session) {
			log_err_fe_client("No session for sent msg with session-id %Lu",
					  msg->refer_id);
			mgmt_msg_native_free_msg(msg);
			return -1;
		}
	}

	if (session) {
		debug_fe_client("Push sent message refer-id %Lu req-id %Lu for session-id %Lu of client %s",
				msg->refer_id, msg->req_id, session->session_id, client->name);
		darr_push(session->sent_msgs, msg);
	} else {
		debug_fe_client("Push sent message refer-id %Lu req-id %Lu for client %s",
				msg->refer_id, msg->req_id, client->name);
		darr_push(client->sent_msgs, msg);
	}
	return 0;
}

/**
 * fe_client_pop_sent_msg() - retreive the sent message for reference and freeing
 */
/*
 * fe_client_pop_sent_msg -- 从“待回复”队列取出与 req_id 匹配的原始请求消息。
 * 参数：client；session_id（非 0 时在对应会话队列里找，否则找客户端通用队列）；
 *       req_id 请求 ID；sessionp 非空时回填找到的会话指针。
 * 返回：找到的消息头并从队列移除；未找到返回 NULL。
 * 注意：取出后由调用方负责 mgmt_msg_native_free_msg 释放。
 */
static struct mgmt_msg_header *fe_client_pop_sent_msg(struct mgmt_fe_client *client,
						      uint64_t session_id, uint64_t req_id,
						      struct mgmt_fe_client_session **sessionp)
{
	struct mgmt_fe_client_session *session = NULL;
	struct mgmt_msg_header ***sent_msgsp;
	struct mgmt_msg_header *msg;
	uint i;

	if (sessionp)
		*sessionp = NULL;
	if (session_id) {
		session = mgmt_fe_find_session_by_session_id(client, session_id);
		if (!session) {
			log_err_fe_client("No session for sent msg with session-id %Lu",
					  session_id);
			return NULL;
		}
		if (sessionp)
			*sessionp = session;
	}
	if (session)
		sent_msgsp = &session->sent_msgs;
	else
		sent_msgsp = &client->sent_msgs;
	darr_foreach_i (*sent_msgsp, i) {
		msg = (*sent_msgsp)[i];
		if (msg->req_id == req_id) {
			darr_remove(*sent_msgsp, i);
			debug_fe_client("Popping sent message for client %s session-id %Lu req-id %Lu",
					client->name, session_id, req_id);
			return msg;
		}
	}
	debug_fe_client("No sent message found for client %s session-id %Lu req-id %Lu",
			client->name, session_id, req_id);
	return NULL;
}

/**
 * fe_client_pop_session_req_msg() - pop the sent session_req msg for freeing
 */
/*
 * fe_client_pop_sess_req_msg -- 从客户端通用队列取出 req_id 匹配的 SESSION_REQ 消息。
 * 这是收到“会话请求相关错误”时定位原始请求的专用版本，会校验消息码确为
 * MGMT_MSG_CODE_SESSION_REQ。返回找到的消息（已出队）或 NULL。
 */
static struct mgmt_msg_header *fe_client_pop_sess_req_msg(struct mgmt_fe_client *client,
							  uint64_t req_id)
{
	struct mgmt_msg_header *msg;
	uint i;

	darr_foreach_i (client->sent_msgs, i) {
		msg = client->sent_msgs[i];
		if (msg->req_id != req_id)
			continue;
		if (msg->code != MGMT_MSG_CODE_SESSION_REQ) {
			log_err_fe_client("non-session-req sent msg found for client %s req-id %Lu",
					  client->name, req_id);
			return NULL;
		}
		debug_fe_client("Popping sent session_req message for client %s req-id %Lu",
				client->name, req_id);
		darr_remove(client->sent_msgs, i);
		return msg;
	}
	return NULL;
}

/*
 * mgmt_fe_send_session_req -- 构造并发送会话请求（创建或销毁）。
 * 参数：client；session；create 为真表示创建、假表示销毁。
 * 关键：发送前先入队（create 入客户端队列、destroy 入会话队列），这样即便走
 *       “短路”同步回复也能立刻找到原请求。req_id 使用 session->client_id。
 * 返回：底层发送接口的返回码。
 */
static int mgmt_fe_send_session_req(struct mgmt_fe_client *client,
				    struct mgmt_fe_client_session *session,
				    bool create)
{
	struct mgmt_msg_session_req *msg;

	msg = mgmt_msg_native_alloc_msg(struct mgmt_msg_session_req, 0,
					MTYPE_MSG_NATIVE_SESSION_REQ);
	msg->code = MGMT_MSG_CODE_SESSION_REQ;
	msg->req_id = session->client_id;
	if (create) {
		debug_fe_client("Sending SESSION_REQ create message for client-id %Lu",
				session->client_id);
		/* we need to queue before sending short-circuit so it's there when replied to */
		darr_push(client->sent_msgs, (struct mgmt_msg_header *)msg);
	} else {
		msg->refer_id = session->session_id;
		debug_fe_client("Sending SESSION_REQ destroy message for session-id: %Lu client-id %Lu",
				msg->refer_id, msg->req_id);
		/* we need to queue before sending short-circuit so it's there when replied to */
		darr_push(session->sent_msgs, (struct mgmt_msg_header *)msg);
	}

	return mgmt_msg_native_send_msg(&client->client.conn, msg, true);
}

/*
 * mgmt_fe_send_lockds_req -- 发送数据存储加锁 / 解锁请求。
 * 参数：client；session_id；req_id；ds_id 目标数据存储；lock 真为加锁、假为解锁；
 *       scok 是否允许短路（同步直返）。
 * 行为：scok 为真时先入队再发送；发送失败则把已入队的消息弹出并释放。返回发送结果码。
 */
int mgmt_fe_send_lockds_req(struct mgmt_fe_client *client, uint64_t session_id, uint64_t req_id,
			    enum mgmt_ds_id ds_id, bool lock, bool scok)
{
	struct mgmt_msg_lock *msg;
	int ret = 0;

	msg = mgmt_msg_native_alloc_msg(struct mgmt_msg_lock, 0, MTYPE_MSG_NATIVE_LOCK);
	msg->refer_id = session_id;
	msg->req_id = req_id;
	msg->code = MGMT_MSG_CODE_LOCK;
	msg->datastore = ds_id;
	msg->lock = lock;

	debug_fe_client("Sending LOCKDS_REQ (%sLOCK) message for DS:%s session-id %" PRIu64,
			lock ? "" : "UN", dsid2name(ds_id), session_id);

	if (scok)
		ret = fe_client_push_sent_msg(client, (struct mgmt_msg_header *)msg, 0);
	if (!ret) {
		ret = mgmt_msg_native_send_msg(&client->client.conn, msg, scok);
		if (!scok)
			ret = fe_client_push_sent_msg(client, (struct mgmt_msg_header *)msg, ret);
		else if (ret && fe_client_pop_sent_msg(client, session_id, msg->req_id, NULL))
			mgmt_msg_native_free_msg(msg);
	}
	return ret;
}

/*
 * mgmt_fe_send_commit_req -- 发送提交配置请求。
 * 参数：client；session_id；req_id；src_ds_id 源 DS；dest_ds_id 目标 DS；
 *       validate_only 仅校验；abort 回滚；unlock 提交后解锁。
 * 行为：validate_only / abort / 其余分别对应 MGMT_MSG_COMMIT_VALIDATE / _ABORT /
 *       _APPLY 动作；发出后把消息入队以便配对回复。返回发送结果码。
 */
int mgmt_fe_send_commit_req(struct mgmt_fe_client *client, uint64_t session_id, uint64_t req_id,
			    enum mgmt_ds_id src_ds_id, enum mgmt_ds_id dest_ds_id,
			    bool validate_only, bool abort, bool unlock)
{
	struct mgmt_msg_commit *msg;
	int ret;

	msg = mgmt_msg_native_alloc_msg(struct mgmt_msg_commit, 0, MTYPE_MSG_NATIVE_COMMIT);
	msg->code = MGMT_MSG_CODE_COMMIT;
	msg->refer_id = session_id;
	msg->req_id = req_id;
	msg->source = src_ds_id;
	msg->target = dest_ds_id;
	if (validate_only)
		msg->action = MGMT_MSG_COMMIT_VALIDATE;
	else if (abort)
		msg->action = MGMT_MSG_COMMIT_ABORT;
	else
		msg->action = MGMT_MSG_COMMIT_APPLY;
	msg->unlock = unlock;

	debug_fe_client("Sending COMMIT message for src: %s dst: %s session-id %Lu action: %s unlock: %d",
			dsid2name(src_ds_id), dsid2name(dest_ds_id), session_id,
			msg->action == MGMT_MSG_COMMIT_VALIDATE ? "validate"
			: msg->action == MGMT_MSG_COMMIT_ABORT	? "abort"
								: "apply",
			unlock);

	ret = mgmt_msg_native_send_msg(&client->client.conn, msg, false);
	ret = fe_client_push_sent_msg(client, (struct mgmt_msg_header *)msg, ret);
	return ret;
}

/*
 * Send get-data request.
 */
/*
 * mgmt_fe_send_get_data_req -- 发送 get-data 请求。
 * 参数：client；session_id；req_id；datastore 数据存储；result_type 结果格式；
 *       flags 行为标志；defaults 默认值选项；xpath 目标路径。
 * 行为：把 xpath 拷进消息尾部，发出后入队。返回发送结果码。
 */
int mgmt_fe_send_get_data_req(struct mgmt_fe_client *client,
			      uint64_t session_id, uint64_t req_id,
			      uint8_t datastore, LYD_FORMAT result_type,
			      uint8_t flags, uint8_t defaults, const char *xpath)
{
	struct mgmt_msg_get_data *msg;
	size_t xplen = strlen(xpath);
	int ret;

	msg = mgmt_msg_native_alloc_msg(struct mgmt_msg_get_data, xplen + 1,
					MTYPE_MSG_NATIVE_GET_DATA);
	msg->refer_id = session_id;
	msg->req_id = req_id;
	msg->code = MGMT_MSG_CODE_GET_DATA;
	msg->result_type = result_type;
	msg->flags = flags;
	msg->defaults = defaults;
	msg->datastore = datastore;
	strlcpy(msg->xpath, xpath, xplen + 1);

	debug_fe_client("Sending GET_DATA_REQ session-id %" PRIu64
			" req-id %" PRIu64 " xpath: %s",
			session_id, req_id, xpath);

	ret = mgmt_msg_native_send_msg(&client->client.conn, msg, false);
	ret = fe_client_push_sent_msg(client, (struct mgmt_msg_header *)msg, ret);
	return ret;
}

/*
 * mgmt_fe_send_edit_req -- 发送 edit 请求。
 * 参数：client；session_id；req_id；datastore；request_type 请求格式；flags；
 *       operation NB_OP_* 操作；xpath；data 数据树（可空）。
 * 行为：先按 session_id 找到会话并 assert 存在；用 mgmt_msg_native_xpath_encode
 *       写入 xpath，data 非空时追加到消息尾；发出后入队。返回发送结果码。
 * 坑：这里把 session 声明为 static 变量，仅为复用缓冲，非线程安全也别依赖其值。
 */
int mgmt_fe_send_edit_req(struct mgmt_fe_client *client, uint64_t session_id,
			  uint64_t req_id, uint8_t datastore,
			  LYD_FORMAT request_type, uint8_t flags,
			  uint8_t operation, const char *xpath, const char *data)
{
	static struct mgmt_fe_client_session *session;
	struct mgmt_msg_edit *msg;
	int ret;

	session = mgmt_fe_find_session_by_session_id(client, session_id);
	assert(session);

	msg = mgmt_msg_native_alloc_msg(struct mgmt_msg_edit, 0,
					MTYPE_MSG_NATIVE_EDIT);
	msg->refer_id = session_id;
	msg->req_id = req_id;
	msg->code = MGMT_MSG_CODE_EDIT;
	msg->request_type = request_type;
	msg->flags = flags;
	msg->datastore = datastore;
	msg->operation = operation;

	mgmt_msg_native_xpath_encode(msg, xpath);
	if (data)
		mgmt_msg_native_append(msg, data, strlen(data) + 1);

	debug_fe_client("Sending EDIT_REQ session-id %" PRIu64
			" req-id %" PRIu64 " xpath: %s",
			session_id, req_id, xpath);

	ret = mgmt_msg_native_send_msg(&client->client.conn, msg, false);
	ret = fe_client_push_sent_msg(client, (struct mgmt_msg_header *)msg, ret);
	return ret;
}

/*
 * mgmt_fe_send_rpc_req -- 发送 RPC 请求。
 * 参数：client；session_id；req_id；request_type 结果格式；xpath；data 数据树（可空）。
 * 行为：编码 xpath、追加 data，发出后入队。返回发送结果码。
 */
int mgmt_fe_send_rpc_req(struct mgmt_fe_client *client, uint64_t session_id,
			 uint64_t req_id, LYD_FORMAT request_type,
			 const char *xpath, const char *data)
{
	struct mgmt_msg_rpc *msg;
	int ret;

	msg = mgmt_msg_native_alloc_msg(struct mgmt_msg_rpc, 0,
					MTYPE_MSG_NATIVE_RPC);
	msg->refer_id = session_id;
	msg->req_id = req_id;
	msg->code = MGMT_MSG_CODE_RPC;
	msg->request_type = request_type;

	mgmt_msg_native_xpath_encode(msg, xpath);
	if (data)
		mgmt_msg_native_append(msg, data, strlen(data) + 1);

	debug_fe_client("Sending RPC_REQ session-id %" PRIu64 " req-id %" PRIu64
			" xpath: %s",
			session_id, req_id, xpath);

	ret = mgmt_msg_native_send_msg(&client->client.conn, msg, false);
	ret = fe_client_push_sent_msg(client, (struct mgmt_msg_header *)msg, ret);
	return ret;
}

/*
 * Handle a native encoded message
 */
/*
 * fe_client_handle_native_msg -- 处理一条从 mgmtd 收到的原生消息（核心分发）。
 * 参数：client；msg 消息头；msg_len 整条消息长度。
 * 流程：
 *   1) 若是 ERROR 且针对会话请求，先取出原始会话请求消息；
 *   2) SESSION_REPLY 优先单独处理（此时会话状态可能尚未建好），创建分支回填
 *      session_id 并回调 client_session_notify；
 *   3) 其余消息先按 refer_id / req_id 用 pop_sent_msg 取出原始请求消息配对；
 *   4) 按 msg->code 分派：ERROR（再按原请求类型分派到 lock_ds_notify /
 *      commit_config_notify / error_notify）、各类 *_REPLY、TREE_DATA、
 *      EDIT_REPLY、RPC_REPLY、NOTIFY（异步通知，必要时做格式转换）。
 * 返回：无。副作用：在 done 标签处统一释放取出的原始消息。
 * 坑：回调内部可能删除会话，故回调返回后不要再访问 session。
 */
static void fe_client_handle_native_msg(struct mgmt_fe_client *client,
					struct mgmt_msg_header *msg,
					size_t msg_len)
{
	struct mgmt_fe_client_session *session = NULL;
	struct mgmt_msg_session_req *session_req = NULL;
	struct mgmt_msg_session_reply *session_reply = NULL;
	struct mgmt_msg_notify_data *notify_msg;
	struct mgmt_msg_tree_data *tree_msg;
	struct mgmt_msg_edit_reply *edit_msg;
	struct mgmt_msg_rpc_reply *rpc_msg;
	struct mgmt_msg_error *err_msg;
	struct mgmt_msg_commit *commit_msg;
	struct mgmt_msg_lock *lock_msg;
	struct mgmt_msg_header *orig_msg = NULL;
	const char *xpath = NULL;
	const char *data = NULL;
	uint16_t orig_code;
	size_t dlen;

	debug_fe_client("Got native message for session-id %" PRIu64,
			msg->refer_id);

	/* See if this is an error for a session request */
	if (msg->code == MGMT_MSG_CODE_ERROR)
		orig_msg = fe_client_pop_sess_req_msg(client, msg->req_id);

	/*
	 * Handle SESSION messages up front as state might not all be there
	 */
	if (msg->code == MGMT_MSG_CODE_SESSION_REPLY) {
		session_reply = (typeof(session_reply))msg;

		/* Obtain the original message for a given reply (or error reply) */
		if (session_reply->created)
			orig_msg = fe_client_pop_sent_msg(client, 0, msg->req_id, NULL);
		else
			orig_msg = fe_client_pop_sent_msg(client, msg->refer_id, msg->req_id,
							  &session);

		debug_fe_client("Got SESSION_REPLY (%s) for client-id %Lu with session-id: %Lu",
				session_reply->created ? "create" : "destroy", msg->req_id,
				msg->refer_id);
		if (session_reply->created) {
			session = mgmt_fe_find_session_by_client_id(client, msg->req_id);
			if (!session) {
				log_err_fe_client("Session create failed for client-id %Lu",
						  msg->req_id);
				goto done;
			}
			session->session_id = msg->refer_id;
		} else {
			debug_fe_client("Got SESSION_REPLY (destroy) for session-id %Lu",
					session_reply->refer_id);
			session = mgmt_fe_find_session_by_session_id(client, msg->refer_id);
		}
		if (session && session->client && session->client->cbs.client_session_notify)
			(*session->client->cbs.client_session_notify)(client, client->user_data,
								      session->client_id,
								      session_reply->created, true,
								      session_reply->refer_id,
								      session->user_ctx);
		goto done;
	} else if (orig_msg) {
		/* We got this above */
		assert(msg->code == MGMT_MSG_CODE_ERROR &&
		       orig_msg->code == MGMT_MSG_CODE_SESSION_REQ);

		debug_fe_client("Error handling session-req client-id %Lu req-id %Lu", msg->req_id,
				msg->req_id);

		if (msg->refer_id)
			session = mgmt_fe_find_session_by_session_id(client, msg->refer_id);
		if (!session)
			session = mgmt_fe_find_session_by_client_id(client, msg->req_id);
		if (session && !session->client->cbs.client_session_notify)
			goto generic_error_handler;

		session_req = (typeof(session_req))orig_msg;
		/* The session state may be deleted by the callback */
		if (session && session->client && session->client->cbs.client_session_notify)
			(*session->client->cbs.client_session_notify)(client, client->user_data,
								      session->client_id,
								      session_req->refer_id == 0,
								      false, msg->refer_id,
								      session->user_ctx);
		goto done;
	}

	/* Obtain the original message for a given reply (or error reply) */
	orig_msg = fe_client_pop_sent_msg(client, msg->refer_id, msg->req_id, &session);
	orig_code = orig_msg ? orig_msg->code : MGMT_MSG_CODE_ERROR;
	if (!session || !session->client) {
		log_err_fe_client("No session for received native msg session-id %Lu",
				  msg->refer_id);
		goto done;
	}

	switch (msg->code) {
	case MGMT_MSG_CODE_ERROR:
		err_msg = (typeof(err_msg))msg;
		if (!MGMT_MSG_VALIDATE_NUL_TERM(err_msg, msg_len)) {
			log_err_fe_client("Corrupt error msg recv");
			break;
		}

		/* Try calling handlers that take success arg based on original message */
		switch (orig_code) {
		case MGMT_MSG_CODE_LOCK:
			if (!session->client->cbs.lock_ds_notify)
				goto generic_error_handler;
			lock_msg = (typeof(lock_msg))orig_msg;
			session->client->cbs.lock_ds_notify(client, client->user_data,
							    session->client_id,
							    session->session_id, session->user_ctx,
							    msg->req_id, lock_msg->lock, false,
							    lock_msg->datastore, err_msg->errstr);
			break;
		case MGMT_MSG_CODE_COMMIT:
			if (!session->client->cbs.commit_config_notify)
				goto generic_error_handler;
			commit_msg = (typeof(commit_msg))orig_msg;
			session->client->cbs.commit_config_notify(client, client->user_data,
								  session->client_id,
								  session->session_id,
								  session->user_ctx, msg->req_id,
								  false, commit_msg->source,
								  commit_msg->target,
								  commit_msg->action ==
									  MGMT_MSG_COMMIT_VALIDATE,
								  commit_msg->unlock,
								  err_msg->errstr);
			break;
		case MGMT_MSG_CODE_GET_DATA:
		case MGMT_MSG_CODE_ERROR:
		case MGMT_MSG_CODE_EDIT:
		case MGMT_MSG_CODE_NOTIFY:
		case MGMT_MSG_CODE_RPC:
generic_error_handler:
			err_msg = (typeof(err_msg))msg;
			if (!orig_msg)
				log_err_fe_client("No saved message for session-id %Lu req-id %Lu",
						  msg->refer_id, msg->req_id);
			if (session->client->cbs.error_notify)
				session->client->cbs.error_notify(client, client->user_data,
								  session->client_id, msg->refer_id,
								  session->user_ctx, msg->req_id,
								  err_msg->error, err_msg->errstr);
			break;
		default:
			log_err_fe_client("Unhandled original message code %u for session-id %Lu req-id %Lu",
					  orig_code, msg->refer_id, msg->req_id);
			assert(!"Unhandled error for original message code");
			break;
		}
		break;
	case MGMT_MSG_CODE_COMMIT_REPLY:
		if (!session->client->cbs.commit_config_notify)
			break;
		commit_msg = (typeof(commit_msg))msg;
		session->client->cbs.commit_config_notify(client, client->user_data,
							  session->client_id, session->session_id,
							  session->user_ctx, msg->req_id, true,
							  commit_msg->source, commit_msg->target,
							  commit_msg->action ==
								  MGMT_MSG_COMMIT_VALIDATE,
							  commit_msg->unlock, NULL);

		break;
	case MGMT_MSG_CODE_LOCK_REPLY:
		if (!session->client->cbs.lock_ds_notify)
			break;
		lock_msg = (typeof(lock_msg))msg;
		session->client->cbs.lock_ds_notify(client, client->user_data, session->client_id,
						    msg->refer_id, session->user_ctx, msg->req_id,
						    lock_msg->lock, true, lock_msg->datastore,
						    NULL);
		break;
	case MGMT_MSG_CODE_TREE_DATA:
		if (!session->client->cbs.get_tree_notify)
			break;

		tree_msg = (typeof(tree_msg))msg;
		if (msg_len < sizeof(*tree_msg)) {
			log_err_fe_client("Corrupt tree-data msg recv");
			break;
		}
		session->client->cbs.get_tree_notify(client, client->user_data,
						     session->client_id,
						     msg->refer_id,
						     session->user_ctx,
						     msg->req_id,
						     MGMTD_DS_OPERATIONAL,
						     tree_msg->result_type,
						     tree_msg->result,
						     msg_len - sizeof(*tree_msg),
						     tree_msg->partial_error);
		break;
	case MGMT_MSG_CODE_EDIT_REPLY:
		if (!session->client->cbs.edit_notify)
			break;

		edit_msg = (typeof(edit_msg))msg;
		if (msg_len < sizeof(*edit_msg)) {
			log_err_fe_client("Corrupt edit-reply msg recv");
			break;
		}

		xpath = mgmt_msg_native_xpath_decode(edit_msg, msg_len);
		if (!xpath) {
			log_err_fe_client("Corrupt edit-reply msg recv");
			break;
		}

		session->client->cbs.edit_notify(client, client->user_data,
						 session->client_id,
						 msg->refer_id,
						 session->user_ctx, msg->req_id,
						 xpath);
		break;
	case MGMT_MSG_CODE_RPC_REPLY:
		if (!session->client->cbs.rpc_notify)
			break;

		rpc_msg = (typeof(rpc_msg))msg;
		if (msg_len < sizeof(*rpc_msg)) {
			log_err_fe_client("Corrupt rpc-reply msg recv");
			break;
		}
		dlen = msg_len - sizeof(*rpc_msg);

		session->client->cbs.rpc_notify(client, client->user_data,
						session->client_id,
						msg->refer_id,
						session->user_ctx, msg->req_id,
						dlen ? rpc_msg->data : NULL);
		break;
	case MGMT_MSG_CODE_NOTIFY:
		if (!session->client->cbs.async_notification)
			break;

		notify_msg = (typeof(notify_msg))msg;
		if (msg_len < sizeof(*notify_msg)) {
			log_err_fe_client("Corrupt notify-data msg recv");
			break;
		}

		data = mgmt_msg_native_data_decode(notify_msg, msg_len);
		if (!data) {
			log_err_fe_client("Corrupt error msg recv");
			break;
		}
		dlen = mgmt_msg_native_data_len_decode(notify_msg, msg_len);
		if (notify_msg->result_type != LYD_JSON)
			data = yang_convert_lyd_format(data, dlen,
						       notify_msg->result_type,
						       LYD_JSON, true);
		if (!data) {
			log_err_fe_client("Can't convert format %d to JSON",
					  notify_msg->result_type);
			break;
		}

		session->client->cbs.async_notification(client,
							client->user_data,
							session->client_id,
							msg->refer_id,
							session->user_ctx, data);

		if (notify_msg->result_type != LYD_JSON)
			darr_free(data);
		break;
	default:
		log_err_fe_client("unknown native message session-id %" PRIu64
				  " req-id %" PRIu64 " code %u",
				  msg->refer_id, msg->req_id, msg->code);
		break;
	}
done:
	if (orig_msg)
		mgmt_msg_native_free_msg(orig_msg);
}

/*
 * mgmt_fe_client_process_msg -- 连接层收到完整消息后的入口回调（注册给 msg_client）。
 * 参数：version 消息版本；data 载荷；len 长度；conn 连接。
 * 行为：用 container_of 反推出 client；仅支持 MGMT_MSG_VERSION_NATIVE，长度足够
 *       则交给 fe_client_handle_native_msg，否则报错。收到非 native（旧 protobuf）
 *       消息视为不兼容，主动断开连接。
 */
static void mgmt_fe_client_process_msg(uint8_t version, uint8_t *data,
				       size_t len, struct msg_conn *conn)
{
	struct mgmt_fe_client *client;
	struct msg_client *msg_client;

	msg_client = container_of(conn, struct msg_client, conn);
	client = container_of(msg_client, struct mgmt_fe_client, client);

	if (version == MGMT_MSG_VERSION_NATIVE) {
		struct mgmt_msg_header *msg = (typeof(msg))data;

		if (len >= sizeof(*msg))
			fe_client_handle_native_msg(client, msg, len);
		else
			log_err_fe_client("native message to FE client %s too short %zu",
					  client->name, len);
		return;
	}

	log_err_fe_client("Protobuf no longer used in frontend API");
	msg_conn_disconnect(&client->client.conn, true);
}

/*
 * _notify_connect_disconnect -- 连接建立 / 断开时的统一处理。
 * 参数：msg_client；connected 真为连接、假为断开。
 * 行为：断开时遍历删除本客户端的全部会话——先在链表上移除（避免双重释放），
 *       再回调 client_session_notify（destroy）通知用户，最后释放 sent_msgs 与会话内存；
 *       之后无条件回调 client_connect_notify 通知连接状态。返回 0。
 */
static int _notify_connect_disconnect(struct msg_client *msg_client,
				      bool connected)
{
	struct mgmt_fe_client *client =
		container_of(msg_client, struct mgmt_fe_client, client);
	struct mgmt_fe_client_session *session;

	/* Walk list of sessions for this FE client deleting them */
	if (!connected && mgmt_sessions_count(&client->sessions)) {
		debug_fe_client("Cleaning up existing sessions");

		FOREACH_SESSION_IN_LIST (client, session) {
			assert(session->client);

			/* unlink from list first this avoids double free */
			mgmt_sessions_del(&client->sessions, session);

			/* notify FE client the session is being deleted */
			if (session->client->cbs.client_session_notify) {
				(*session->client->cbs.client_session_notify)(
					client, client->user_data,
					session->client_id, false, true,
					session->session_id, session->user_ctx);
			}

			darr_free_free(session->sent_msgs);
			XFREE(MTYPE_MGMTD_FE_SESSION, session);
		}
	}

	/* Notify FE client through registered callback (if any). */
	if (client->cbs.client_connect_notify)
		(void)(*client->cbs.client_connect_notify)(
			client, client->user_data, connected);
	return 0;
}

/*
 * mgmt_fe_client_notify_connect -- 传给 msg_client 的连接建立回调，转发给统一处理。
 */
static int mgmt_fe_client_notify_connect(struct msg_client *client)
{
	return _notify_connect_disconnect(client, true);
}

/*
 * mgmt_fe_client_notify_disconnect -- 连接断开回调，先用 container_of 反推 client，
 * 再转发给统一处理。
 */
static int mgmt_fe_client_notify_disconnect(struct msg_conn *conn)
{
	struct msg_client *client = container_of(conn, struct msg_client, conn);

	return _notify_connect_disconnect(client, false);
}

/*
 * mgmt_debug_client_fe_set -- 设置前端客户端调试开关，并同步到连接的 debug 标志。
 * 参数：mode 模式位；set 真开假关。无客户端实例时只更新调试记录本身。
 */
static void mgmt_debug_client_fe_set(uint32_t mode, bool set)
{
	DEBUG_FLAGS_SET(&mgmt_dbg_fe_client, mode, set);

	if (!__fe_client)
		return;

	__fe_client->client.conn.debug = DEBUG_MODE_CHECK(&mgmt_dbg_fe_client,
							  DEBUG_MODE_ALL);
}

/*
 * debug_mgmt_client_fe -- CLI 命令 "[no] debug mgmt client frontend"。
 * 根据当前 VTY 节点换算调试模式（config 节点可持久化），用 !no 决定开 / 关。
 */
DEFPY(debug_mgmt_client_fe, debug_mgmt_client_fe_cmd,
      "[no] debug mgmt client frontend",
      NO_STR DEBUG_STR MGMTD_STR
      "client\n"
      "frontend\n")
{
	mgmt_debug_client_fe_set(DEBUG_NODE2MODE(vty->node), !no);

	return CMD_SUCCESS;
}

/*
 * Initialize library and try connecting with MGMTD.
 */
/*
 * mgmt_fe_client_create -- 创建前端客户端并尝试连接 mgmtd。
 * 参数：client_name；cbs 回调集合（可空）；user_data；event_loop。
 * 行为：若已有实例则返回 NULL（每进程仅一个）；分配并记录 __fe_client，复制名字 /
 *       回调，初始化会话链表，构造 mgmtd FE socket 路径，用 msg_client_init 建立
 *       连接管理并注册各回调。
 * 返回：客户端句柄；创建失败（已有实例）返回 NULL。
 */
struct mgmt_fe_client *mgmt_fe_client_create(const char *client_name,
					     struct mgmt_fe_client_cbs *cbs,
					     uintptr_t user_data,
					     struct event_loop *event_loop)
{
	struct mgmt_fe_client *client;
	char server_path[MAXPATHLEN];

	if (__fe_client)
		return NULL;

	client = XCALLOC(MTYPE_MGMTD_FE_CLIENT, sizeof(*client));
	__fe_client = client;

	client->name = XSTRDUP(MTYPE_MGMTD_FE_CLIENT_NAME, client_name);
	client->user_data = user_data;
	if (cbs)
		client->cbs = *cbs;

	mgmt_sessions_init(&client->sessions);

	snprintf(server_path, sizeof(server_path), MGMTD_FE_SOCK_NAME);

	msg_client_init(&client->client, event_loop, server_path,
			mgmt_fe_client_notify_connect,
			mgmt_fe_client_notify_disconnect,
			mgmt_fe_client_process_msg, MGMTD_FE_MAX_NUM_MSG_PROC,
			MGMTD_FE_MAX_NUM_MSG_WRITE, MGMTD_FE_MAX_MSG_LEN, true,
			"FE-client", debug_check_fe_client());

	debug_fe_client("Initialized client '%s'", client_name);

	return client;
}

/*
 * mgmt_fe_client_lib_vty_init -- 安装调试开关到调试系统，并在 ENABLE / CONFIG
 * 节点注册 debug 命令。使用本库的组件需调用它（并在 xref2vtysh.py 中登记）。
 */
void mgmt_fe_client_lib_vty_init(void)
{
	debug_install(&mgmt_dbg_fe_client);

	install_element(ENABLE_NODE, &debug_mgmt_client_fe_cmd);
	install_element(CONFIG_NODE, &debug_mgmt_client_fe_cmd);
}

/*
 * mgmt_fe_client_session_count -- 返回当前会话数。
 */
uint mgmt_fe_client_session_count(struct mgmt_fe_client *client)
{
	return mgmt_sessions_count(&client->sessions);
}

/*
 * mgmt_fe_client_current_msg_short_circuit -- 返回底层连接是否处于“短路”状态
 * （真表示当前消息走同步直返路径）。
 */
bool mgmt_fe_client_current_msg_short_circuit(struct mgmt_fe_client *client)
{
	return client->client.conn.is_short_circuit;
}

/*
 * mgmt_fe_client_name -- 返回客户端名。
 */
const char *mgmt_fe_client_name(struct mgmt_fe_client *client)
{
	return client->name;
}

/*
 * Create a new Session for a Frontend Client connection.
 */
/*
 * mgmt_fe_create_client_session -- 创建前端会话。
 * 参数：client；client_id；user_ctx 用户上下文。
 * 行为：分配会话、填好字段、挂到链表尾、发送会话创建请求；发送失败则释放会话
 *       并返回 MGMTD_INTERNAL_ERROR。返回 MGMTD_SUCCESS 或错误码。
 */
enum mgmt_result mgmt_fe_create_client_session(struct mgmt_fe_client *client,
					       uint64_t client_id,
					       uintptr_t user_ctx)
{
	struct mgmt_fe_client_session *session;

	session = XCALLOC(MTYPE_MGMTD_FE_SESSION,
			  sizeof(struct mgmt_fe_client_session));
	assert(session);
	session->user_ctx = user_ctx;
	session->client_id = client_id;
	session->client = client;
	session->session_id = 0;

	mgmt_sessions_add_tail(&client->sessions, session);

	if (mgmt_fe_send_session_req(client, session, true) != 0) {
		XFREE(MTYPE_MGMTD_FE_SESSION, session);
		return MGMTD_INTERNAL_ERROR;
	}

	return MGMTD_SUCCESS;
}

/*
 * Delete an existing Session for a Frontend Client connection.
 */
/*
 * mgmt_fe_destroy_client_session -- 销毁前端会话。
 * 参数：client；client_id。若找不到会话或归属不符返回 MGMTD_INVALID_PARAM；
 * 若已分配 session_id 则先发送销毁请求（失败仅记日志）；随后从链表移除并释放
 * 会话内存及其 sent_msgs。返回 MGMTD_SUCCESS。
 */
enum mgmt_result mgmt_fe_destroy_client_session(struct mgmt_fe_client *client,
						uint64_t client_id)
{
	struct mgmt_fe_client_session *session;

	session = mgmt_fe_find_session_by_client_id(client, client_id);
	if (!session || session->client != client)
		return MGMTD_INVALID_PARAM;

	if (session->session_id &&
	    mgmt_fe_send_session_req(client, session, false) != 0)
		log_err_fe_client("Failed to send session destroy request for the session-id %" PRIu64,
				  session->session_id);

	mgmt_sessions_del(&client->sessions, session);
	darr_free_free(session->sent_msgs);
	XFREE(MTYPE_MGMTD_FE_SESSION, session);

	return MGMTD_SUCCESS;
}

/*
 * Destroy library and cleanup everything.
 */
/*
 * mgmt_fe_client_destroy -- 销毁客户端并清理全部资源。
 * 行为：断言 client 就是全局 __fe_client；遍历销毁所有会话；清理连接；
 *       释放通用 sent_msgs、名字、客户端内存，并把 __fe_client 置空。
 */
void mgmt_fe_client_destroy(struct mgmt_fe_client *client)
{
	struct mgmt_fe_client_session *session;

	assert(client == __fe_client);

	debug_fe_client("Destroying MGMTD Frontend Client '%s'", client->name);

	FOREACH_SESSION_IN_LIST (client, session)
		mgmt_fe_destroy_client_session(client, session->client_id);

	msg_client_cleanup(&client->client);

	darr_free_free(client->sent_msgs);

	XFREE(MTYPE_MGMTD_FE_CLIENT_NAME, client->name);
	XFREE(MTYPE_MGMTD_FE_CLIENT, client);

	__fe_client = NULL;
}
