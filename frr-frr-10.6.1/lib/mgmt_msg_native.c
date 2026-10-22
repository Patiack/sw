// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 29 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 *
 */
#include <zebra.h>
#include "darr.h"
#include "mgmt_msg_native.h"

/*
 * 文件导读：mgmt_msg_native.c —— mgmtd 原生 TLV 消息的分配 / 元数据实现
 *
 * 1. 这个文件干什么：
 *    FRR 的 mgmtd（管理守护进程）与各前端（frontend）之间通过一套“原生消息”
 *    协议通信，消息头之后是 TLV 编码的载荷。本文件负责这套协议的基础设施：
 *      (1) 用 DEFINE_MTYPE 注册各类消息的内存类型（便于内存统计 / 泄漏排查）；
 *      (2) 维护“消息码 -> 最小结构体大小”的映射表，供解码时校验；
 *      (3) 提供消息码到名字的转换（日志用）；
 *      (4) 发送 / 构造 error 消息；
 *      (5) 解码字符串数组载荷。
 *
 * 2. 关键数据结构 / 全局变量：
 *    - mgmt_msg_min_sizes[]：按消息码索引，给出该码对应的最小结构体字节数。
 *    - nmgmt_msg_min_sizes：上表的元素个数。
 *    - 各类消息结构体（mgmt_msg_error 等）定义在 mgmt_msg_native.h。
 *
 * 3. 与其他模块的关系：
 *    依赖 mgmt_msg_native.h（协议定义）、darr.h（动态数组 / 字符串工具）、
 *    memory.h（MTYPE 机制）；为 mgmt_be_client / mgmt_fe_client 提供底层编解码。
 *
 * 4. 阅读顺序：
 *    先看顶部 MTYPE 与 min_sizes 表了解“有哪些消息”，再看三个查询 / 发送函数，
 *    最后看字符串数组解码。
 *
 * 5. 易踩的坑：
 *    - min_sizes 表用指定初始化器（[CODE] = ...），顺序不影响正确性，但漏项会得 0。
 *    - mgmt_msg_get_min_size 对越界 code 返回 0，调用方需自行判空。
 *    - 字符串解码要求载荷以 0 结尾，否则直接返回 NULL。
 */
/*
 * 内存类型定义：为原生消息子系统注册内存组 MSG_NATIVE，并为每种消息注册
 * 一个 MTYPE（供内存统计 / 泄漏报告区分用途），这些名字会出现在 show memory 输出里。
 */
DEFINE_MGROUP(MSG_NATIVE, "Native message allocations");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_ERROR, "native error msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_GET_TREE, "native get tree msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_TREE_DATA, "native tree data msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_GET_DATA, "native get data msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_NOTIFY, "native notify msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_NOTIFY_SELECT, "native notify select msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_EDIT, "native edit msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_EDIT_REPLY, "native edit reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_RPC, "native RPC msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_RPC_REPLY, "native RPC reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_SUBSCRIBE, "native subscribe msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_SESSION_REQ, "native session-req msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_SESSION_REPLY, "native session-reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_TXN_REQ, "native txn-req msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_TXN_REPLY, "native txn-reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_CFG_REQ, "native cfg-req msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_CFG_REPLY, "native cfg-reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_CFG_APPLY_REQ, "native cfg-act-req msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_CFG_APPLY_REPLY, "native cfg-act-reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_LOCK, "native lock msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_LOCK_REPLY, "native lock-reply msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_COMMIT, "native commit msg");
DEFINE_MTYPE(MSG_NATIVE, MSG_NATIVE_COMMIT_REPLY, "native commit-reply msg");

/*
 * mgmt_msg_min_sizes[] -- “消息码 -> 最小结构体大小”的映射表。
 * 下标即消息码（MGMT_MSG_CODE_*），值为对应定长消息结构体的 sizeof；
 * 接收端据此判断收到的载荷是否至少有这么多字节。未登记的项为 0。
 */
size_t mgmt_msg_min_sizes[] = {
	[MGMT_MSG_CODE_ERROR] = sizeof(struct mgmt_msg_error),
	[MGMT_MSG_CODE_GET_TREE] = sizeof(struct mgmt_msg_get_tree),
	[MGMT_MSG_CODE_TREE_DATA] = sizeof(struct mgmt_msg_tree_data),
	[MGMT_MSG_CODE_GET_DATA] = sizeof(struct mgmt_msg_get_data),
	[MGMT_MSG_CODE_NOTIFY] = sizeof(struct mgmt_msg_notify_data),
	[MGMT_MSG_CODE_EDIT] = sizeof(struct mgmt_msg_edit),
	[MGMT_MSG_CODE_EDIT_REPLY] = sizeof(struct mgmt_msg_edit_reply),
	[MGMT_MSG_CODE_RPC] = sizeof(struct mgmt_msg_rpc),
	[MGMT_MSG_CODE_RPC_REPLY] = sizeof(struct mgmt_msg_rpc_reply),
	[MGMT_MSG_CODE_NOTIFY_SELECT] = sizeof(struct mgmt_msg_notify_select),
	[MGMT_MSG_CODE_SESSION_REQ] = sizeof(struct mgmt_msg_session_req),
	[MGMT_MSG_CODE_SESSION_REPLY] = sizeof(struct mgmt_msg_session_reply),
	[MGMT_MSG_CODE_SUBSCRIBE] = sizeof(struct mgmt_msg_subscribe),
	[MGMT_MSG_CODE_TXN_REQ] = sizeof(struct mgmt_msg_txn_req),
	[MGMT_MSG_CODE_TXN_REPLY] = sizeof(struct mgmt_msg_txn_reply),
	[MGMT_MSG_CODE_CFG_REQ] = sizeof(struct mgmt_msg_cfg_req),
	[MGMT_MSG_CODE_CFG_REPLY] = sizeof(struct mgmt_msg_cfg_reply),
	[MGMT_MSG_CODE_CFG_APPLY_REQ] = sizeof(struct mgmt_msg_cfg_apply_req),
	[MGMT_MSG_CODE_CFG_APPLY_REPLY] = sizeof(struct mgmt_msg_cfg_apply_reply),
	[MGMT_MSG_CODE_LOCK] = sizeof(struct mgmt_msg_lock),
	[MGMT_MSG_CODE_LOCK_REPLY] = sizeof(struct mgmt_msg_lock_reply),
	[MGMT_MSG_CODE_COMMIT] = sizeof(struct mgmt_msg_commit),
	[MGMT_MSG_CODE_COMMIT_REPLY] = sizeof(struct mgmt_msg_commit_reply),
};
/*
 * nmgmt_msg_min_sizes -- 上表的元素个数（编译期由 sizeof 计算得出）。
 */
size_t nmgmt_msg_min_sizes = sizeof(mgmt_msg_min_sizes) /
			     sizeof(*mgmt_msg_min_sizes);

/*
 * mgmt_msg_get_min_size -- 查询某消息码对应的最小结构体大小。
 * 参数 code：消息码。返回：大小（字节）；若 code 越界则返回 0。
 * 坑：返回 0 既可能表示“越界”，也可能表示某消息未登记，调用方需注意区分。
 */
size_t mgmt_msg_get_min_size(uint code)
{
	if (code >= nmgmt_msg_min_sizes)
		return 0;
	return mgmt_msg_min_sizes[code];
}

/*
 * mgmt_msg_code_name -- 把消息码转成可读名字（主要用于日志 / 调试）。
 * 参数 code：消息码。返回：静态字符串常量；未知码则写入静态缓冲区 buf 并返回
 *   "UNKNOWN(n)"。
 * 坑：对未知码返回的是共享的 static buf，非线程安全，下次调用即被覆盖。
 */
const char *mgmt_msg_code_name(uint code)
{
	static char buf[48];

	switch (code) {
	case MGMT_MSG_CODE_ERROR:
		return "ERROR";
	case MGMT_MSG_CODE_GET_TREE:
		return "GET_TREE";
	case MGMT_MSG_CODE_TREE_DATA:
		return "TREE_DATA";
	case MGMT_MSG_CODE_GET_DATA:
		return "GET_DATA";
	case MGMT_MSG_CODE_NOTIFY:
		return "NOTIFY";
	case MGMT_MSG_CODE_EDIT:
		return "EDIT";
	case MGMT_MSG_CODE_EDIT_REPLY:
		return "EDIT_REPLY";
	case MGMT_MSG_CODE_RPC:
		return "RPC";
	case MGMT_MSG_CODE_RPC_REPLY:
		return "RPC_REPLY";
	case MGMT_MSG_CODE_NOTIFY_SELECT:
		return "NOTIFY_SELECT";
	case MGMT_MSG_CODE_SESSION_REQ:
		return "SESSION_REQ";
	case MGMT_MSG_CODE_SESSION_REPLY:
		return "SESSION_REPLY";
	case MGMT_MSG_CODE_LOCK:
		return "LOCK";
	case MGMT_MSG_CODE_LOCK_REPLY:
		return "LOCK_REPLY";
	case MGMT_MSG_CODE_COMMIT:
		return "COMMIT";
	case MGMT_MSG_CODE_COMMIT_REPLY:
		return "COMMIT_REPLY";
	case MGMT_MSG_CODE_SUBSCRIBE:
		return "SUBSCRIBE";
	case MGMT_MSG_CODE_TXN_REQ:
		return "TXN_REQ";
	case MGMT_MSG_CODE_TXN_REPLY:
		return "TXN_REPLY";
	case MGMT_MSG_CODE_CFG_REQ:
		return "CFG_REQ";
	case MGMT_MSG_CODE_CFG_REPLY:
		return "CFG_REPLY";
	case MGMT_MSG_CODE_CFG_APPLY_REQ:
		return "CFG_APPLY_REQ";
	case MGMT_MSG_CODE_CFG_APPLY_REPLY:
		return "CFG_APPLY_REPLY";
	default:
		snprintf(buf, sizeof(buf), "UNKNOWN(%u)", code);
		return buf;
	}
}


/*
 * vmgmt_msg_native_send_error -- 构造并发送一条 ERROR 消息。
 * 参数：conn 连接；sess_or_txn_id 会话或事务 ID（作为 refer_id）；
 *       req_id 触发它的请求 ID；short_circuit_ok 是否允许短路发送；
 *       error 错误码；errfmt + ap 变参错误文本。
 * 返回：底层发送接口的返回值。副作用：动态分配错误消息并发出，随后立即释放。
 * 坑：errstr 是 darr 分配的动态字符串，用完必须 darr_free；发送后消息即释放。
 */
int vmgmt_msg_native_send_error(struct msg_conn *conn, uint64_t sess_or_txn_id, uint64_t req_id,
				bool short_circuit_ok, int16_t error, const char *errfmt,
				va_list ap)
{
	struct mgmt_msg_error *msg;
	char *errstr;
	ssize_t slen;
	int ret;

	errstr = darr_vsprintf(errfmt, ap);
	slen = strlen(errstr);

	msg = mgmt_msg_native_alloc_msg(typeof(*msg), slen + 1,
					MTYPE_MSG_NATIVE_ERROR);
	msg->refer_id = sess_or_txn_id;
	msg->req_id = req_id;
	msg->code = MGMT_MSG_CODE_ERROR;
	msg->error = error;
	strlcpy(msg->errstr, errstr, slen + 1);
	darr_free(errstr);

	if (conn->debug)
		zlog_debug("Sending error %d session-id %" PRIu64
			   " req-id %" PRIu64 " scok %d errstr: %s",
			   error, sess_or_txn_id, req_id, short_circuit_ok,
			   msg->errstr);

	ret = mgmt_msg_native_send_msg(conn, msg, short_circuit_ok);
	mgmt_msg_native_free_msg(msg);
	return ret;
}

/*
 * _mgmt_msg_native_strings_decode -- 解码一串以 0 结尾、连续存放的字符串。
 * 参数：_sdata 载荷首地址；sdlen 载荷字节数。
 * 返回：新分配的字符串指针数组（darr），失败返回 NULL；元素是 darr_strdup 的副本。
 * 坑：要求载荷最后一个字节是 0（否则返回 NULL）；循环以上一轮得到的 len 作为步进，
 *     因此每个字符串都必须完整 0 结尾，否则会越界读取。
 */
const char **_mgmt_msg_native_strings_decode(const void *_sdata, int sdlen)
{
	const char *sdata = _sdata;
	const char **strings = NULL;
	int len;

	if (sdata[sdlen - 1] != 0)
		return NULL;

	for (; sdlen; sdata += len, sdlen -= len) {
		*darr_append(strings) = darr_strdup(sdata);
		len = 1 + darr_strlen(strings[darr_lasti(strings)]);
	}

	return strings;
}
