// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 29 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 *
 * Public APIs:
 *
 * The message type codes and corresponding message data definitions for
 * front-end client messages represent a public API, as such any changes should
 * only be made according to backward compatible principles (basically never,
 * just use a new message type). Back-end clients being always compiled with FRR
 * can be updated (although one should take care in modifying BE messages as it
 * could impact private back-end client implementations which will then need to
 * be updated by their owners).
 */

/*
 * 文件导读：mgmt_msg_native.h —— mgmtd 原生（非 protobuf）消息定义
 *
 * 一、定位
 *   mgmtd 与前端 / 后端客户端之间既可以用 protobuf 编码，也可以使用本文件
 *   定义的原生消息。原生消息把每条消息直接布局成 C 结构体，按“头部 + 固定
 *   字段 + 可选变长字段”的方式直接读写线缆，从而摆脱 protobuf 依赖，也便于
 *   用 C、rust、go、python 等语言直接解析。
 *
 * 二、消息的通用布局规则
 *   1) 每条消息以 struct mgmt_msg_header 作为第一个（匿名）字段；
 *   2) 其后是定宽字段，按自然对齐边界摆放；
 *   3) 可选地追加一个零长度变长字段，按 64 位对齐，这样可用
 *      “头部指针加一”直接得到变长区首地址；
 *   4) 每种消息定义一个独立的内存类型（MTYPE），成对的 DEFINE_MTYPE 位于
 *      mgmt_msg_native.c。
 *   本文件里大量 _Static_assert 用于在编译期校验各结构体的大小与字段偏移，
 *   一旦布局被破坏就会编译失败。
 *
 * 三、消息分类（按 code 区分，见 MGMT_MSG_CODE_*）
 *   - 公共 API（前端可用）：GET_DATA、TREE_DATA、NOTIFY、EDIT、RPC、
 *     LOCK、COMMIT 及其对应的 reply；
 *   - 仅后端（non-public）：GET_TREE、SUBSCRIBE、TXN_REQ / TXN_REPLY、
 *     CFG_REQ / CFG_REPLY、CFG_APPLY_REQ / CFG_APPLY_REPLY。
 *   前端消息编码属于对外稳定 API，变更须遵循向后兼容原则（基本不改，只新增）。
 *
 * 四、分配与使用
 *   用 mgmt_msg_native_alloc_msg() 按 MTYPE 分配、mgmt_msg_native_free_msg()
 *   释放；变长内容用 mgmt_msg_native_append() 追加。底层其实是 darr 动态
 *   数组，重新分配后旧的内部指针会失效，须重新获取。
 */
#ifndef _FRR_MGMT_MSG_NATIVE_H_
#define _FRR_MGMT_MSG_NATIVE_H_

#ifdef __cplusplus
extern "C" {
#elif 0
}
#endif

#include <zebra.h>
#include "compiler.h"
#include "darr.h"
#include "memory.h"
#include "mgmt_msg.h"
#include "mgmt_defines.h"
#include "northbound.h"

#include <stdalign.h>

/*
 * ==================
 * Native Message API
 * ==================
 *
 * -----------------------
 * Defining A New Message:
 * -----------------------
 *
 *  1) Start with `struct mgmt_msg_header` as the first (unnamed) field.
 *
 *  2) Add fixed-width fields. Add on natural aligned boundaries (*)
 *
 *  3) [Optional] Add a zero-length variable field. Add aligned on a 64-bit
 *     boundary, this is done so that: `value = (HDR + 1)` works.
 *
 *  4) Define a new MTYPE for the new message type (see DECLARE_MTYPE below
 *     as well as the paired DEFINE_MTYPE in mgmt_msg_native.c)
 *
 * These rules are so the messages may be read from and written directly to
 * "the wire", easily, using common programming languages (e.g., C, rust, go,
 * python, ...)
 *
 * (*) Natrual aligned boundaries, i.e., uint16_t on 2-byte boundary, uint64_t
 * on 8-byte boundaries, ...)
 *
 * ------------------------------
 * Allocating New Native Messages
 * ------------------------------
 *
 * For fixed-length and variable length messages one should allocate new
 * messages with the mgmt_msg_native_alloc_msg() passing in the newly defined
 * MTYPE. Likewise, to free the message one should use
 * mgmt_msg_native_free_msg().
 *
 * Unknown Variable Length Messages:
 * ---------------------------------
 *
 * If using a zero-length variable length field and the length is not known at
 * message creation time, you can use the `native` API function
 * mgmt_msg_native_append() to add data to the end of the message, or if a more
 * full set of operations are required, the darr_xxxx() API is also available as
 * in the Advanced section below.
 *
 * Notable API Functions:
 * ---------------------------------
 *
 * mgmt_msg_native_alloc_msg() - Allocate a native msg.
 * mgmt_msg_native_free_msg() - Free a native msg.
 * mgmt_msg_native_append() - Append data to the end of the msg.
 * mgmt_msg_native_get_msg_len() - Get the total length of the msg.
 * mgmt_msg_native_send_msg() - Send the message.
 *
 * mgmt_msg_native_xpath_encode() - Encode xpath in xpath, data format message.
 * mgmt_msg_native_xpath_data_decode() - Decode xpath, data format message.
 * mgmt_msg_native_xpath_decode() - Get the xpath, from xpath, data format message.
 * mgmt_msg_native_data_decode() - Get the secondary data from xpath, data message.
 * mgmt_msg_native_data_len_decode() - Get length of secondary data.
 *
 * -------------------------------------
 * [Advanced Use] Dynamic Array Messages
 * -------------------------------------
 *
 * NOTE: Most users can simply use mgmt_msg_native_append() and skip this
 * section.
 *
 * This section is only important to understand if you wish to utilize the fact
 * that native messages allocated with mgmt_msg_native_alloc_msg are
 * actually allocated as uint8_t dynamic arrays (`darr`).
 *
 * You can utilize all the darr_xxxx() API to manipulate the variable length
 * message data in a native message. To do so you simply need to understand that
 * the native message is actually a `uint8_t *` darr. So, for example, to append
 * data to the end of a message one could do the following:
 *
 *     void append_metric_path(struct mgmt_msg_my_msg *msg)
 *     {
 *          msg = (struct mggm_msg_my_msg *)
 *              darr_strcat((uint8_t *)msg, "/metric");
 *
 *	    // ...
 *     }
 *
 * NOTE: If reallocs happen the original passed in pointer will be updated;
 * however, any other pointers into the message will become invalid, and so they
 * should always be discarded or reinitialized after using any reallocating
 * darr_xxx() API functions.
 *
 *     void append_metric_path(struct mgmt_msg_my_msg *msg)
 *     {
 *          char *xpath = msg->xpath;	// pointer into message
 *
 *          darr_in_strcat((uint8_t *)msg, "/metric");
 *          // msg may have been updated to point at new memory
 *
 *          xpath = NULL;		// now invalid
 *          xpath = msg->xpath;         // reinitialize
 *	    // ...
 *     }
 *
 * Rather than worry about this, it's typical when using dynamic arrays to always
 * work from the main pointer to the dynamic array, rather than caching multiple
 * pointers into the data. Modern compilers will optimize the code so that it
 * adds no extra execution cost.
 *
 *     void append_metric_path(struct mgmt_msg_my_msg *msg)
 *     {
 *          darr_in_strcat((uint8_t *)msg, "/metric");
 *
 *          // Use `msg->xpath` directly rather creating and using an
 *          // `xpath = msg->xpath` local variable.
 *
 *          if (strcmp(msg->xpath, "foobar/metric")) {
 *	        // ...
 *          }
 *     }
 *
 */

/*
 * 为每种原生消息声明独立的内存类型，便于内存统计与泄漏排查。对应的
 * DEFINE_MTYPE 在 mgmt_msg_native.c 中给出。
 */
DECLARE_MTYPE(MSG_NATIVE_MSG);
DECLARE_MTYPE(MSG_NATIVE_ERROR);
DECLARE_MTYPE(MSG_NATIVE_GET_TREE);
DECLARE_MTYPE(MSG_NATIVE_TREE_DATA);
DECLARE_MTYPE(MSG_NATIVE_GET_DATA);
DECLARE_MTYPE(MSG_NATIVE_NOTIFY);
DECLARE_MTYPE(MSG_NATIVE_NOTIFY_SELECT);
DECLARE_MTYPE(MSG_NATIVE_EDIT);
DECLARE_MTYPE(MSG_NATIVE_EDIT_REPLY);
DECLARE_MTYPE(MSG_NATIVE_RPC);
DECLARE_MTYPE(MSG_NATIVE_RPC_REPLY);
DECLARE_MTYPE(MSG_NATIVE_SESSION_REQ);
DECLARE_MTYPE(MSG_NATIVE_SESSION_REPLY);
DECLARE_MTYPE(MSG_NATIVE_SUBSCRIBE);
DECLARE_MTYPE(MSG_NATIVE_TXN_REQ);
DECLARE_MTYPE(MSG_NATIVE_TXN_REPLY);
DECLARE_MTYPE(MSG_NATIVE_CFG_REQ);
DECLARE_MTYPE(MSG_NATIVE_CFG_REPLY);
DECLARE_MTYPE(MSG_NATIVE_CFG_APPLY_REQ);
DECLARE_MTYPE(MSG_NATIVE_CFG_APPLY_REPLY);
DECLARE_MTYPE(MSG_NATIVE_LOCK);
DECLARE_MTYPE(MSG_NATIVE_LOCK_REPLY);
DECLARE_MTYPE(MSG_NATIVE_COMMIT);
DECLARE_MTYPE(MSG_NATIVE_COMMIT_REPLY);

/*
 * Native message codes
 */
/*
 * 原生消息的类型码（即 struct mgmt_msg_header.code）。注释里的 Public API
 * 表示前端客户端可用的对外稳定接口；BE only 表示仅 mgmtd 与后端客户端
 * 之间使用的非公开接口。注意 12~18 号是从 protobuf 消息派生而来、用于
 * 去除 protobuf 依赖的后端消息。
 */
#define MGMT_MSG_CODE_ERROR	 0 /* Public API */
#define MGMT_MSG_CODE_GET_TREE	 1 /* BE only, non-public API */
#define MGMT_MSG_CODE_TREE_DATA	 2 /* Public API */
#define MGMT_MSG_CODE_GET_DATA	 3 /* Public API */
#define MGMT_MSG_CODE_NOTIFY	 4 /* Public API */
#define MGMT_MSG_CODE_EDIT	 5 /* Public API */
#define MGMT_MSG_CODE_EDIT_REPLY 6 /* Public API */
#define MGMT_MSG_CODE_RPC	 7 /* Public API */
#define MGMT_MSG_CODE_RPC_REPLY	 8 /* Public API */
#define MGMT_MSG_CODE_NOTIFY_SELECT 9 /* Public API */
#define MGMT_MSG_CODE_SESSION_REQ   10 /* Public API */
#define MGMT_MSG_CODE_SESSION_REPLY 11 /* Public API */
#define MGMT_MSG_CODE_LOCK	    19 /* Public API */
#define MGMT_MSG_CODE_LOCK_REPLY    20 /* Public API */
#define MGMT_MSG_CODE_COMMIT	    21 /* Public API */
#define MGMT_MSG_CODE_COMMIT_REPLY  22 /* Public API */

/* Derived from protobuf messages to remove protobuf dependency */
#define MGMT_MSG_CODE_SUBSCRIBE	      12 /* BE only, non-public API */
#define MGMT_MSG_CODE_TXN_REQ	      13 /* BE only, non-public API */
#define MGMT_MSG_CODE_TXN_REPLY	      14 /* BE only, non-public API */
#define MGMT_MSG_CODE_CFG_REQ	      15 /* BE only, non-public API */
#define MGMT_MSG_CODE_CFG_REPLY	      16 /* BE only, non-public API */
#define MGMT_MSG_CODE_CFG_APPLY_REQ   17 /* BE only, non-public API */
#define MGMT_MSG_CODE_CFG_APPLY_REPLY 18 /* BE only, non-public API */

/*
 * Datastores
 */
/*
 * 数据存储（datastore）标识：NONE 表示未指定；RUNNING 为生效配置；
 * CANDIDATE 为候选配置（提交前）；OPERATIONAL 为运行状态数据。
 */
#define MGMT_MSG_DATASTORE_NONE	       0
#define MGMT_MSG_DATASTORE_RUNNING     1
#define MGMT_MSG_DATASTORE_CANDIDATE   2
#define MGMT_MSG_DATASTORE_OPERATIONAL 3

/*
 * 返回消息类型码对应的可读名称，主要用于日志。参数 code 为 MGMT_MSG_CODE_*。
 */
const char *mgmt_msg_code_name(uint code);

/*
 * Formats
 */
/*
 * 数据序列化格式，取值刻意与 libyang 的 LYD_FORMAT 保持一致（见下方静态
 * 断言），以便直接透传而不必做映射。
 */
#define MGMT_MSG_FORMAT_XML    1
#define MGMT_MSG_FORMAT_JSON   2
#define MGMT_MSG_FORMAT_BINARY 3 /* non-standard libyang internal format */
#define MGMT_MSG_FORMAT_LAST   3

/*
 * Now we're using LYD_FORMAT directly to avoid mapping code, but having our
 * own definitions allows us to create such a mapping in the future if libyang
 * makes a backwards incompatible change.
 */
_Static_assert(MGMT_MSG_FORMAT_XML == LYD_XML, "Format mismatch");
_Static_assert(MGMT_MSG_FORMAT_JSON == LYD_JSON, "Format mismatch");
_Static_assert(MGMT_MSG_FORMAT_BINARY == LYD_LYB, "Format mismatch");

/**
 * struct mgmt_msg_header - Header common to all native messages.
 *
 * @code: the actual type of the message.
 * @resv: Set to zero, ignore on receive.
 * @vsplit: If a variable section is split in 2, the length of first part.
 * @refer_id: the session, txn, conn, etc, this message is associated with.
 * @req_id: the request this message is for.
 */
/*
 * 所有原生消息的公共头部。code 为消息类型码；resv 保留（发送置 0，接收
 * 忽略）；vsplit 在变长区被拆成两段时给出第一段长度；refer_id 标识本条消息
 * 关联的会话 / 事务 / 连接；req_id 标识本条消息针对的请求。
 */
struct mgmt_msg_header {
	uint16_t code;
	uint16_t resv;
	uint32_t vsplit;
	uint64_t refer_id;
	uint64_t req_id;
};
/*
 * 编译期校验头部布局：总大小为 3*8 字节，且最后一个字段 req_id 正好贴到
 * 结构体末尾（无尾部填充）。后续每个消息结构体都有类似的静态断言。
 */
_Static_assert(sizeof(struct mgmt_msg_header) == 3 * 8, "Bad padding");
_Static_assert(sizeof(struct mgmt_msg_header) ==
		       offsetof(struct mgmt_msg_header, req_id) +
			       sizeof(((struct mgmt_msg_header *)0)->req_id),
	       "Size mismatch");

/**
 * struct mgmt_msg_error - Common error message.
 *
 * @error: An error value.
 * @errst: Description of error can be 0 length.
 *
 * This common error message can be used for replies for many msg requests
 * (req_id).
 */
/*
 * 通用错误消息：error 为错误值，errstr 为可选的错误描述（可为 0 长度）。
 * 可作为多种请求（按 req_id 匹配）的通用应答。
 */
struct mgmt_msg_error {
	struct mgmt_msg_header;
	int16_t error;
	uint8_t resv2[6];

	alignas(8) char errstr[];
};
_Static_assert(sizeof(struct mgmt_msg_error) ==
		       offsetof(struct mgmt_msg_error, errstr),
	       "Size mismatch");

/**
 * struct mgmt_msg_get_tree - backend oper data request.
 *
 * @result_type: ``LYD_FORMAT`` for the returned result.
 * @xpath: the query for the data to return.
 */
/*
 * 后端 oper 数据请求：result_type 指定返回结果的 LYD_FORMAT，
 * xpath 为要查询的数据路径（零长度变长字段）。
 */
struct mgmt_msg_get_tree {
	struct mgmt_msg_header;
	uint8_t result_type;
	uint8_t resv2[7];

	alignas(8) char xpath[];
};
_Static_assert(sizeof(struct mgmt_msg_get_tree) ==
		       offsetof(struct mgmt_msg_get_tree, xpath),
	       "Size mismatch");

/**
 * struct mgmt_msg_tree_data - Message carrying tree data.
 *
 * @partial_error: If the full result could not be returned do to this error.
 * @result_type: ``LYD_FORMAT`` for format of the @result value.
 * @more: if this is a partial return and there will be more coming.
 * @result: The tree data in @result_type format.
 *
 */
/*
 * 承载数据树的消息：partial_error 表示因何错误导致结果不完整；
 * result_type 为 result 的格式；more 表示这是分片返回且后续还有；
 * result 为按 result_type 编码的树数据（零长度变长字段）。
 */
struct mgmt_msg_tree_data {
	struct mgmt_msg_header;
	int8_t partial_error;
	uint8_t result_type;
	uint8_t more;
	uint8_t resv2[5];

	alignas(8) uint8_t result[];
};
_Static_assert(sizeof(struct mgmt_msg_tree_data) ==
		       offsetof(struct mgmt_msg_tree_data, result),
	       "Size mismatch");

/* Flags for get-data request */
/*
 * get-data 请求的标志位：STATE 包含 config false 的状态数据；CONFIG 包含
 * config true 的配置数据；EXACT 表示只要精确命中的节点而非整棵子树。
 */
#define GET_DATA_FLAG_STATE	0x01	/* include "config false" data */
#define GET_DATA_FLAG_CONFIG	0x02	/* include "config true" data */
#define GET_DATA_FLAG_EXACT	0x04	/* get exact data node instead of the full tree */

/*
 * Modes of reporting default values. Non-default values are always reported.
 * These options reflect "with-defaults" modes as defined in RFC 6243.
 */
/*
 * 默认值上报模式，对应 RFC 6243 的 with-defaults：非默认值总是上报，
 * 这里控制默认值如何处理。EXPLICIT 仅显式设置的值；TRIM 省略等于默认的
 * 值；ALL 上报全部；ALL_ADD_TAG 上报全部并给默认值打标记。
 */
#define GET_DATA_DEFAULTS_EXPLICIT    0 /* "explicit" */
#define GET_DATA_DEFAULTS_TRIM	      1 /* "trim"  */
#define GET_DATA_DEFAULTS_ALL	      2 /* "report-all" */
#define GET_DATA_DEFAULTS_ALL_ADD_TAG 3 /* "report-all-tagged" */

/**
 * struct mgmt_msg_get_data - frontend get-data request.
 *
 * @result_type: ``LYD_FORMAT`` for the returned result.
 * @flags: combination of ``GET_DATA_FLAG_*`` flags.
 * @defaults: one of ``GET_DATA_DEFAULTS_*`` values.
 * @xpath: the query for the data to return.
 */
/*
 * 前端 get-data 请求：result_type 指定返回格式；flags 为 GET_DATA_FLAG_*
 * 的组合；defaults 取 GET_DATA_DEFAULTS_*；datastore 指定数据存储；
 * xpath 为查询路径（零长度变长字段）。
 */
struct mgmt_msg_get_data {
	struct mgmt_msg_header;
	uint8_t result_type;
	uint8_t flags;
	uint8_t defaults;
	uint8_t datastore;
	uint8_t resv2[4];

	alignas(8) char xpath[];
};
_Static_assert(sizeof(struct mgmt_msg_get_data) ==
		       offsetof(struct mgmt_msg_get_data, xpath),
	       "Size mismatch");


/*
 * 通知（notify）的操作类型：NOTIFICATION 普通通知；DS_REPLACE 数据存储
 * 整体替换；DS_DELETE 删除；DS_PATCH 增量修改；DS_GET_SYNC 获取同步。
 */
#define NOTIFY_OP_NOTIFICATION 0
#define NOTIFY_OP_DS_REPLACE   1
#define NOTIFY_OP_DS_DELETE    2
#define NOTIFY_OP_DS_PATCH     3
#define NOTIFY_OP_DS_GET_SYNC  4

/**
 * struct mgmt_msg_notify_data - Message carrying notification data.
 *
 * @result_type: ``LYD_FORMAT`` for format of the @result value.
 * @data: The xpath string of the notification followed by the tree data in
 *        @result_type format.
 * @op: notify operation type.
 */
/*
 * 承载通知数据的消息：result_type 为 data 的格式；op 为 NOTIFY_OP_* 操作
 * 类型；data 为“通知的 xpath 字符串 + 按 result_type 编码的树数据”的
 * 拼接变长区。
 */
struct mgmt_msg_notify_data {
	struct mgmt_msg_header;
	uint8_t result_type;
	uint8_t op;
	uint8_t resv2[6];

	alignas(8) char data[];
};
_Static_assert(sizeof(struct mgmt_msg_notify_data) == offsetof(struct mgmt_msg_notify_data, data),
	       "Size mismatch");

/*
 * deprecated -- can simply do the locks as needed, if the session holds the
 * locks then they aren't needed, otherwise an attempt to acquire them is made
 * and if that fails the edit fails.
 */
#define EDIT_FLAG_IMPLICIT_LOCK 0x01
/* also deprecated -- unneeded, if DS is candidate, then no "commit", if running then "commit" */
#define EDIT_FLAG_IMPLICIT_COMMIT 0x02

/*
 * edit 请求的操作类型，取值刻意与 northbound 的 NB_OP_* 对齐（见下方静态
 * 断言）：CREATE 新建、MERGE 修改合并、REMOVE 删除整个节点、DELETE 删除值、
 * REPLACE 整体替换。上方的 EDIT_FLAG_IMPLICIT_LOCK / IMPLICIT_COMMIT 已废弃。
 */
#define EDIT_OP_CREATE	0
#define EDIT_OP_DELETE	4
#define EDIT_OP_MERGE	2
#define EDIT_OP_REPLACE 5
#define EDIT_OP_REMOVE	3

_Static_assert(EDIT_OP_CREATE == NB_OP_CREATE_EXCL, "Operation mismatch");
_Static_assert(EDIT_OP_DELETE == NB_OP_DELETE, "Operation mismatch");
_Static_assert(EDIT_OP_MERGE == NB_OP_MODIFY, "Operation mismatch");
_Static_assert(EDIT_OP_REPLACE == NB_OP_REPLACE, "Operation mismatch");
_Static_assert(EDIT_OP_REMOVE == NB_OP_DESTROY, "Operation mismatch");

/**
 * struct mgmt_msg_edit - frontend edit request.
 *
 * @request_type: ``LYD_FORMAT`` for the @data.
 * @flags: combination of ``EDIT_FLAG_*`` flags.
 * @datastore: the datastore to edit.
 * @operation: one of ``EDIT_OP_*`` operations.
 * @data: the xpath followed by the tree data for the operation.
 *        for CREATE, xpath points to the parent node.
 */
/*
 * 前端 edit 请求：request_type 为 data 的 LYD_FORMAT；flags 为 EDIT_FLAG_*
 * 组合（已废弃）；datastore 指定要编辑的存储；operation 为 EDIT_OP_*；
 * data 为“xpath + 该操作对应的树数据”，其中 CREATE 时 xpath 指向父节点。
 */
struct mgmt_msg_edit {
	struct mgmt_msg_header;
	uint8_t request_type;
	uint8_t flags;
	uint8_t datastore;
	uint8_t operation;
	uint8_t resv2[4];

	alignas(8) char data[];
};
_Static_assert(sizeof(struct mgmt_msg_edit) ==
		       offsetof(struct mgmt_msg_edit, data),
	       "Size mismatch");

/**
 * struct mgmt_msg_edit_reply - frontend edit reply.
 *
 * @changed: If true then changes in datastore resulted.
 * @created: If true then object was newly created (non-existing before)
 * @data: @vsplit values, second value may be zero len.
 * @data: [0] the xpath of the data node that was created.
 * @data: [1] Possible structured data to pass back to client (e.g., non-"error"
 *        yang modeled error data).
 */
/*
 * 前端 edit 应答：changed 表示数据存储确实被改动；created 表示对象是新建
 * 的；data 为按 vsplit 拆分的两段内容——第一段是新建节点的 xpath，第二段
 * 是可选的结构化数据（如非 error 的 YANG 建模错误数据），可为 0 长度。
 */
struct mgmt_msg_edit_reply {
	struct mgmt_msg_header;
	uint8_t changed;
	uint8_t created;
	uint8_t resv2[6];

	alignas(8) char data[];
};
_Static_assert(sizeof(struct mgmt_msg_edit_reply) ==
		       offsetof(struct mgmt_msg_edit_reply, data),
	       "Size mismatch");

/**
 * struct mgmt_msg_rpc - RPC/action request.
 *
 * @request_type: ``LYD_FORMAT`` for the @data.
 * @restconf: If true the and tree data is present it is in RESTCONF format
 * (i.e., the topmost node is `input`)
 * @data: the xpath followed by the tree data for the operation.
 */
/*
 * RPC / action 请求：request_type 为 data 的 LYD_FORMAT；restconf 为真且
 * 携带树数据时，数据采用 RESTCONF 格式（顶层节点为 input）；data 为
 * “xpath + 操作对应的树数据”。
 */
struct mgmt_msg_rpc {
	struct mgmt_msg_header;
	uint8_t request_type;
	uint8_t restconf;
	uint8_t resv2[6];

	alignas(8) char data[];
};

_Static_assert(sizeof(struct mgmt_msg_rpc) == offsetof(struct mgmt_msg_rpc, data), "Size mismatch");

/**
 * struct mgmt_msg_rpc_reply - RPC/action reply.
 *
 * @result_type: ``LYD_FORMAT`` for the @data.
 * @restconf: If true the and tree data is present it is in RESTCONF format
 * (i.e., the topmost node is `output`)
 * @data: the tree data for the reply.
 */
/*
 * RPC / action 应答：result_type 为 data 的格式；restconf 为真且携带树数据
 * 时采用 RESTCONF 格式（顶层节点为 output）；data 为回复的树数据。
 */
struct mgmt_msg_rpc_reply {
	struct mgmt_msg_header;
	uint8_t result_type;
	uint8_t restconf;
	uint8_t resv2[6];

	alignas(8) char data[];
};

_Static_assert(sizeof(struct mgmt_msg_rpc_reply) ==
		       offsetof(struct mgmt_msg_rpc_reply, data),
	       "Size mismatch");

/**
 * struct mgmt_msg_notify_select - Add notification selectors for FE client.
 *
 * Add xpath prefix notification selectors to limit the notifications sent
 * to the front-end client.
 *
 * Also used by mgmtd to the backend clients. In this case behavior depends on
 * the @get_only boolean. If unset the backend filter set is updated
 * accordingly, otherwise @get_only is true and a "get" operation is done within
 * the notification stream using the given selectors, and the filter set in the
 * backend is not modified. This is used so front-end clients can get an initial
 * dump of the state followed by notifications w/o missing any changes to the
 * state.
 *
 * @selectors: the xpath prefixes to selectors notifications through.
 * @replace: if true replace existing selectors with `selectors`.
 * @get_only: [backend-only]: if true do get op, don't change selectors.
 */
/*
 * 为前端客户端添加通知选择器：用一组 xpath 前缀限定要下发给该客户端的
 * 通知。也用于 mgmtd 向后端客户端下发，此时行为取决于 get_only：为假则
 * 更新后端的过滤器集合；为真则在通知流中按给定选择器做一次 get，且不改动
 * 后端过滤器（用于让前端先拿到状态快照、再接收通知而不漏掉变化）。
 *   selectors 选择器 xpath 前缀；
 *   replace   为真则用 selectors 替换既有选择器；
 *   get_only  仅后端：为真则执行 get 而不改选择器。
 */
struct mgmt_msg_notify_select {
	struct mgmt_msg_header;
	uint8_t replace;
	uint8_t get_only;
	uint8_t resv2[6];

	alignas(8) char selectors[];
};

_Static_assert(sizeof(struct mgmt_msg_notify_select) ==
		       offsetof(struct mgmt_msg_notify_select, selectors),
	       "Size mismatch");


/*
 * 通知数据的默认格式。session 请求里 notify_format 为 0 时使用该默认值。
 */
#define DEFAULT_NOTIFY_FORMAT MGMT_MSG_FORMAT_JSON
/**
 * struct mgmt_msg_session_req - Create or delete a front-end session.
 *
 * @refer_id: Zero for create, otherwise the session-id to delete.
 * @req_id: For create will use as client-id.
 * @notify_format: Format for notification data or 0 for default.
 * @client_name: For first session request the client name, otherwise empty.
 */
/*
 * 创建或删除一个前端会话：refer_id 为 0 表示创建，否则为要删除的会话 id；
 * req_id 在创建时用作 client-id；notify_format 指定通知数据格式（0 用默认）；
 * client_name 首次会话请求时给出客户端名，其余情况为空。
 */
struct mgmt_msg_session_req {
	struct mgmt_msg_header;
	uint8_t notify_format;
	uint8_t resv2[7];

	alignas(8) char client_name[];
};

_Static_assert(sizeof(struct mgmt_msg_session_req) ==
		       offsetof(struct mgmt_msg_session_req, client_name),
	       "Size mismatch");

/**
 * struct mgmt_msg_session_reply - Reply to session request message.
 *
 * @created: true if this is a reply to a create request, otherwise 0.
 * @refer_id: The session-id for the action (create or delete) just taken.
 */
/*
 * 会话请求的应答：created 表示这是对创建请求的应答（否则为 0）；
 * refer_id 为刚创建或删除的会话 id。
 */
struct mgmt_msg_session_reply {
	struct mgmt_msg_header;
	uint8_t created;
	uint8_t resv2[7];
};

/**
 * struct mgmt_msg_lock - Set or clear a lock on a datastore.
 *
 * @refer_id: The session-id that will own (or release) the lock.
 * @datastore: the datastore to lock or unlock.
 * @lock: true to lock, false to unlock.
 */
/*
 * 对某个数据存储加锁或解锁：refer_id 为将持有（或释放）锁的会话 id；
 * datastore 取值 MGMT_MSG_DATASTORE_*；lock 为真加锁、为假解锁。
 */
struct mgmt_msg_lock {
	struct mgmt_msg_header;
	uint8_t datastore; /* MGMT_MSG_DATASTORE_* */
	uint8_t lock;	   /* True to lock, false to unlock */
	uint8_t resv2[6];
};

/**
 * struct mgmt_msg_lock_reply - Reply to lock message.
 *
 * @refer_id: The session-id that will own (or release) the lock.
 * @datastore: the datastore to lock or unlock.
 * @lock: true if locked, false if unlocked.
 */
/*
 * 加解锁请求的应答：datastore、lock 呼应请求；lock 为真表示当前已加锁。
 */
struct mgmt_msg_lock_reply {
	struct mgmt_msg_header;
	uint8_t datastore; /* MGMT_MSG_DATASTORE_* */
	uint8_t lock;	   /* True if locked, false to unlocked */
	uint8_t resv2[6];
};

/*
 * commit 动作：APPLY 提交生效；ABORT 放弃改动；VALIDATE 仅校验不生效。
 */
#define MGMT_MSG_COMMIT_APPLY	 0
#define MGMT_MSG_COMMIT_ABORT	 1
#define MGMT_MSG_COMMIT_VALIDATE 2
/**
 * struct mgmt_msg_commit - Set or clear a commit on a datastore.
 *
 * @refer_id: The session-id that will own (or release) the commit.
 * @source: the datastore to commit from, abort changes or validate changes.
 * @target: the datastore to commit into.
 * @action: apply, abort, or validate the changes from source to target.
 * @unlock: value passed to reply message.
 */
/*
 * 对数据存储执行提交 / 放弃 / 校验：source 为来源存储，target 为目标存储，
 * action 取 MGMT_MSG_COMMIT_*，unlock 会透传到应答中（指示对方是否解锁）。
 */
struct mgmt_msg_commit {
	struct mgmt_msg_header;
	uint8_t source; /* MGMT_MSG_DATASTORE_* */
	uint8_t target; /* MGMT_MSG_DATASTORE_* */
	uint8_t action; /* MGMT_MSG_COMMIT_* */
	uint8_t unlock;
	uint8_t resv2[4];
};

/**
 * struct mgmt_msg_commit_reply - Reply to commit message.
 *
 * @refer_id: The session-id that will own (or release) the commit.
 * @source: the datastore committed from, or the changes aborted or validated.
 * @target: the datastore committed into.
 * @action: action taken, i.e., apply, abort, or validate.
 * @unlock: true indicates receiver should unlock the datastores.
 */
/*
 * commit 请求的应答：source / target / action 呼应请求；unlock 为真表示
 * 接收方应解锁这些数据存储。
 */
struct mgmt_msg_commit_reply {
	struct mgmt_msg_header;
	uint8_t source; /* MGMT_MSG_DATASTORE_* */
	uint8_t target; /* MGMT_MSG_DATASTORE_* */
	uint8_t action; /* MGMT_MSG_COMMIT_* */
	uint8_t unlock;
	uint8_t resv2[4];
};

/**
 * struct mgmt_msg_subscribe - Subscribe YANG paths for backend client.
 *
 * @nconfig: Number of config paths that start the array.
 * @noper: Number of oper-state paths that follow config.
 * @nnotif: Number of notif paths that follow oper.
 * @nrpc: Number of rpc paths that follow notif.
 * @strings: A sequence of strings that starts with the name of the client
 *	     followed by @nconfig xpath prefxies, then by @noper xpath
 *	     prefixes, finally by @nrpc xpath prefixes.
 *
 */
/*
 * 后端客户端订阅 YANG 路径：nconfig / noper / nnotify / nrpc 分别为后续
 * strings 中 config / oper / 通知 / rpc 路径的条数；strings 以客户端名
 * 开头，其后依次是 nconfig 个 config 前缀、noper 个 oper 前缀、nnotify 个
 * 通知前缀、nrpc 个 rpc 前缀。
 */
struct mgmt_msg_subscribe {
	struct mgmt_msg_header;
	uint16_t nconfig; /* config path count */
	uint16_t noper;	  /* oper-state path count */
	uint16_t nnotify; /* notif path count */
	uint16_t nrpc;	  /* rpc path count */

	alignas(8) char strings[];
};


/**
 * struct mgmt_msg_txn_req - Create or delete a transaction.
 *
 * @refer_id: The transaction ID to create or delete.
 * @create: true if this is a create request, otherwise delete.
 */
/*
 * 创建或删除一个事务：refer_id 为要创建或删除的事务 id；create 为真表示
 * 创建，否则为删除。
 */
struct mgmt_msg_txn_req {
	struct mgmt_msg_header;
	uint8_t create;
	uint8_t resv2[7];
};

/**
 * struct mgmt_msg_txn_reply - Reply to transaction request message.
 *
 * @refer_id: The transaction ID for the action (create or delete) just taken.
 * @created: true if this is a reply to a create request, otherwise 0.
 */
/*
 * 事务请求的应答：refer_id 为刚创建或删除的事务 id；created 表示这是对
 * 创建请求的应答。
 */
struct mgmt_msg_txn_reply {
	struct mgmt_msg_header;
	uint8_t created;
	uint8_t resv2[7];
};

/**
 * struct mgmt_msg_cfg_req - Send the configuration to backend.
 *
 * @refer_id: The transaction ID to add config changes to.
 * @config: The configuration changes as a sequence of strings to validate and
 *	    store for applying. These are followed by a final string indicating
 *	    the action to take for the preceding strings. There are 2 actions
 *	    possible: "m" for modify and "r" for remove.
 *
 * To process the config data, walk the action string, each byte indicates the
 * action to take. For "d" (delete/remove) consume 1 string which is the xpath to
 * remove. For "m" (modify) consume 2 strings, the first is the xpath to modify
 * and the second is the new value. The value may be zero length for YANG value
 * type "empty".
 */
/*
 * 向后端下发配置：refer_id 为要加入配置变更的事务 id；config 是一串字符串
 * 组成的变更序列，末尾一个字符串给出动作串。动作串逐字节解释：'d'（删除）
 * 后跟 1 个字符串（要删除的 xpath）；'m'（修改）后跟 2 个字符串（xpath 与
 * 新值，值为 YANG 的 empty 类型时长度可为 0）。
 */
struct mgmt_msg_cfg_req {
	struct mgmt_msg_header;
	uint8_t resv2[8]; /* bug in compiler */

	alignas(8) char config[];
};

_Static_assert(sizeof(struct mgmt_msg_cfg_req) == offsetof(struct mgmt_msg_cfg_req, config),
	       "Size mismatch");

/**
 * struct mgmt_msg_cfg_reply - Reply to configuration message.
 *
 * @refer_id: The transaction ID whose configuration was received and validated.
 */
/*
 * 配置下发（cfg_req）的应答，表示对应事务的配置已被接收并校验；用 refer_id
 * 关联事务。
 */
struct mgmt_msg_cfg_reply {
	struct mgmt_msg_header;
};

/**
 * struct mgmt_msg_cfg_apply_req - Apply config in transaction.
 *
 * @refer_id: The transaction ID to apply.
 */
/*
 * 请求应用某事务中暂存的配置：refer_id 为要应用的事务 id。
 */
struct mgmt_msg_cfg_apply_req {
	struct mgmt_msg_header;
};

/**
 * struct mgmt_msg_cfg_apply_reply - Signal the config has been applied.
 *
 * @refer_id: The transaction ID which was applied.
 */
/*
 * 配置已应用完成的应答：refer_id 为被应用的事务 id。
 */
struct mgmt_msg_cfg_apply_reply {
	struct mgmt_msg_header;
};

/*
 * Validate that the message ends in a NUL terminating byte
 */
/*
 * 校验消息以 NUL 结尾：长度至少比结构体本身多 1 字节，且最后一个字节为 0。
 * 用于确保变长字符串区以 NUL 终止，可安全当作字符串处理。
 */
#define MGMT_MSG_VALIDATE_NUL_TERM(msgp, len)                                  \
	((len) >= sizeof(*msgp) + 1 && ((char *)msgp)[(len)-1] == 0)

/**
 * mgmt_msg_get_min_size() - Get minimum message size given the type
 * @code: The type of the message (MGMT_MSG_CODE_*)
 *
 * Return:
 *	The minimum size of a message of the given type or 0 if the message
 *	code is unknown.
 */
/*
 * 返回给定消息类型码的最小结构体大小（不含变长区）。code 为 MGMT_MSG_CODE_*；
 * 类型码未知时返回 0。用于接收侧做长度合法性检查。
 */
size_t mgmt_msg_get_min_size(uint code);

/**
 * Send a native message error to the other end of the connection.
 *
 * This function is normally used by the server-side to indicate a failure to
 * process a client request. For this server side handling of client messages
 * which expect a reply, either that reply or this error should be returned, as
 * closing the connection is not allowed during message handling.
 *
 * Args:
 *	conn: the connection.
 *	sess_or_txn_id: Session ID (to FE client) or Txn ID (from BE client)
 *	req_id: which req_id this error is associated with.
 *	short_circuit_ok: if short circuit sending is OK.
 *	error: the error value
 *	errfmt: vprintfrr style format string
 *	ap: the variable args for errfmt.
 *
 * Return:
 *	The return value of ``msg_conn_send_msg``.
 */
/*
 * 向连接对端发送一条原生错误消息，通常由服务端在无法处理客户端请求时使用。
 *   conn             连接；
 *   sess_or_txn_id   会话 id（发给前端）或事务 id（来自后端）；
 *   req_id           该错误对应的请求 id；
 *   short_circuit_ok 是否允许短接发送；
 *   error            错误值；
 *   errfmt / ap      变参错误描述（vprintfrr 风格）。
 * 返回 msg_conn_send_msg 的返回值。注意：处理消息过程中不允许关闭连接，故此
 * 对需要应答的请求必须回以应答或本错误消息。
 */
extern int vmgmt_msg_native_send_error(struct msg_conn *conn,
				       uint64_t sess_or_txn_id, uint64_t req_id,
				       bool short_circuit_ok, int16_t error,
				       const char *errfmt, va_list ap)
	PRINTFRR(6, 0);

/**
 * mgmt_msg_native_alloc_msg() - Create a native appendable msg.
 * @msg_type: The message structure type.
 * @var_len: The initial additional length to add to the message.
 * @mem_type: The initial additional length to add to the message.
 *
 * This function takes a C type (e.g., `struct mgmt_msg_get_tree`) as an
 * argument and returns a new native message. The newly allocated message
 * can be used with the other `native` functions.
 *
 * Importantly the mgmt_msg_native_append() function can be used to add data
 * to the end of the message, and mgmt_msg_get_native_msg_len() can be used
 * to obtain the total length of the message (i.e., the fixed sized header plus
 * the variable length data that has been appended).
 *
 * Additionally, a dynamic array (darr) pointer can be obtained using
 * mgmt_msg_get_native_darr() which allows adding and manipulating the
 * variable data that follows the fixed sized header.
 *
 * Return: A `msg_type` object created using a dynamic_array.
 */
/*
 * 分配一条可追加的原生消息。msg_type 为消息结构体类型，var_len 为初始额外的
 * 变长区长度，mem_type 为内存类型。返回按该结构体解释的 darr 指针，随后可
 * 用 mgmt_msg_native_append 追加数据。
 */
#define mgmt_msg_native_alloc_msg(msg_type, var_len, mem_type)                                    \
	({                                                                                        \
		uint8_t *_nam_buf = NULL;                                                         \
		darr_append_nz_mt(_nam_buf, sizeof(msg_type) + (var_len), mem_type);              \
		(msg_type *)_nam_buf;                                                             \
	})

/**
 * mgmt_msg_free_native_msg() - Free a native msg.
 * @msg: pointer to message allocated by mgmt_msg_create_native_msg().
 */
/*
 * 释放由 mgmt_msg_native_alloc_msg 分配的原生消息（本质是 darr_free）。
 */
#define mgmt_msg_native_free_msg darr_free

/**
 * mgmt_msg_native_get_msg_len() - Get the total length of the msg.
 * @msg: the native message.
 *
 * Return: the total length of the message, fixed + variable length.
 */
/*
 * 返回消息的总长度（定长头部 + 已追加的变长数据）。
 */
#define mgmt_msg_native_get_msg_len(msg) (darr_len((uint8_t *)(msg)))

/**
 * mgmt_msg_native_append() - Append data to the end of the msg.
 * @msg: (IN/OUT) Pointer to the native message, variable may be updated.
 * @data: data to append.
 * @len: length of data to append.
 *
 * Append @data of length @len to the native message @msg.
 *
 * NOTE: Be aware @msg pointer may change as a result of reallocating the
 * message to fit the new data. Any other pointers into the old message should
 * be discarded.
 *
 * Return: a pointer to the newly appended data.
 */
/*
 * 向消息尾部追加 len 字节数据，返回指向新追加数据的指针。注意 msg 可能因
 * 重新分配而改变，之前保存的指向该消息内部的其它指针都会失效，须丢弃。
 */
#define mgmt_msg_native_append(msg, data, len)                                                     \
	({                                                                                         \
		uint8_t **_na_darrp = mgmt_msg_native_get_darrp(msg);                              \
		uint8_t *_na_p = darr_append_n(*_na_darrp, len);                                   \
		memcpy(_na_p, data, len);                                                          \
		_na_p;                                                                             \
	})

/**
 * mgmt_msg_native_add_str() - Append [another] string to the msg.
 * @msg: (IN/OUT) Pointer to the native message, variable may be updated.
 * @s: string to append.
 *
 * Append string @s to the native message @msg. @msg is assumed to have a
 * sequence of NUL-terminated strings at the end of it. This function appends
 * the string @s and it's NUL terminating octet to the message.
 *
 * NOTE: Be aware @msg pointer may change as a result of reallocating the
 * message to fit the new data. Any other pointers into the old message should
 * be discarded.
 */
/*
 * 向消息尾部追加一个以 NUL 结尾的字符串 s（连同结尾的 0 一起追加）。假定
 * 消息尾部本来就是一串 NUL 结尾字符串。msg 同样可能被重新分配。
 */
#define mgmt_msg_native_add_str(msg, s)                                                            \
	do {                                                                                       \
		int _nas_len = strlen(s) + 1;                                                      \
		mgmt_msg_native_append(msg, s, _nas_len);                                          \
	} while (0)

/**
 * mgmt_msg_native_send_msg(msg, short_circuit_ok) - Send a native msg.
 * @conn: the mgmt_msg connection.
 * @msg: the native message.
 * @short_circuit_ok: True if short-circuit sending is required.
 *
 * Return: The error return value of msg_conn_send_msg().
 */
/*
 * 以 MGMT_MSG_VERSION_NATIVE 版本发送一条原生消息。short_circuit_ok 指示是
 * 否允许短接路径。返回 msg_conn_send_msg 的错误码。
 */
#define mgmt_msg_native_send_msg(conn, msg, short_circuit_ok)                  \
	msg_conn_send_msg(conn, MGMT_MSG_VERSION_NATIVE, msg,                  \
			  mgmt_msg_native_get_msg_len(msg), NULL,              \
			  short_circuit_ok)

/**
 * mgmt_msg_native_get_darrp() - Return a ptr to the dynamic array ptr.
 * @msg: Pointer to the native message.
 *
 * NOTE: Most users can simply use mgmt_msg_native_append() instead of this.
 *
 * This function obtains a pointer to the dynamic byte array for this message,
 * this array actually includes the message header if one is going to look at
 * the length value. With that in mind any of the `darr_*()` functions/API may
 * be used to manipulate the variable data at the end of the message.
 *
 * NOTE: The pointer returned is actually a pointer to the message pointer
 * passed in to this function. This pointer to pointer is required so that
 * realloc can be done inside the darr API.
 *
 * NOTE: If reallocs happen the original passed in pointer will be updated;
 * however, any other pointers into the message will become invalid and so they
 * should always be discarded after using the returned value.
 *
 * Example:
 *
 *     void append_metric_path(struct mgmt_msg_my_msg *msg)
 *     {
 *          char *xpath = msg->xpath;	// pointer into message
 *          uint8_t **darp;
 *
 *          darrp = mgmt_msg_native_get_darrp(msg);
 *          darr_in_strcat(*darrp, "/metric");
 *
 *          xpath = NULL;		// now invalid
 *          xpath = msg->xpath;
 *     }
 *
 *
 * Return: A pointer to the first argument -- which is a pointer to a pointer to
 * a dynamic array.
 */
/*
 * 返回指向消息底层动态数组指针的指针（uint8_t 二级指针）。绝大多数场景直接
 * 用 mgmt_msg_native_append 即可；需要完整 darr 操作时才用它。该数组包含消息
 * 头部，故可用各种 darr_* 接口操作尾部的变长数据。
 */
#define mgmt_msg_native_get_darrp(msg) ((uint8_t **)&(msg))

/* ------------------------- */
/* Encode and Decode Helpers */
/* ------------------------- */

/**
 * mgmt_msg_native_xpath_encode() - encode an xpath in a xpath, data message.
 * @msg: Pointer to the native message.
 * @xpath: The xpath string to encode.
 *
 * This function starts the encoding of a message that can be decoded with
 * `mgmt_msg_native_xpath_data_decode()`. The variable length data is comprised
 * of a NUL terminated string followed by some data of any format. This starts
 * the first half of the encoding, after which one can simply append the
 * secondary data to the message.
 */
/*
 * xpath, data 格式消息的编码第一步：把 xpath 字符串（含结尾 NUL）追加到消息
 * 变长区，并把 vsplit 设为该字符串长度。随后再追加任意格式的次级数据，即可用
 * mgmt_msg_native_xpath_data_decode 解码。
 */
#define mgmt_msg_native_xpath_encode(msg, xpath)                                                   \
	do {                                                                                       \
		size_t _m__slen = strlen(xpath) + 1;                                               \
		mgmt_msg_native_append(msg, xpath, _m__slen);                                      \
		(msg)->vsplit = _m__slen;                                                          \
	} while (0)

/**
 * mgmt_msg_native_xpath_data_decode() - decode an xpath, data format message.
 * @msg: Pointer to the native message.
 * @msglen: Length of the message.
 * @data: [OUT] Pointer to the data section of the variable data
 *
 * This function decodes a message that was encoded with
 * `mgmt_msg_native_xpath_encode()`. The variable length data is comprised of a
 * NUL terminated string followed by some data of any format.
 *
 * Return:
 *	The xpath string or NULL if there was an error decoding (i.e., the
 *	message is corrupt).
 */
/*
 * 解码 xpath, data 格式消息：变长区为“NUL 结尾的 xpath + 任意次级数据”。
 * 校验 vsplit 合法且 xpath 末尾为 NUL 后，返回 xpath 指针，并把次级数据起点
 * 写入 _data（无次级数据时置 NULL）。消息损坏（校验失败）时返回 NULL。
 */
#define mgmt_msg_native_xpath_data_decode(msg, msglen, _data)                                      \
	({                                                                                         \
		size_t _m__len = (msglen) - sizeof(*msg);                                          \
		const char *_m__s = NULL;                                                          \
		(_data) = NULL;                                                                    \
		if (msg->vsplit && msg->vsplit <= _m__len && msg->data[msg->vsplit - 1] == 0) {    \
			if (msg->vsplit < _m__len)                                                 \
				(_data) = msg->data + msg->vsplit;                                 \
			_m__s = msg->data;                                                         \
		}                                                                                  \
		_m__s;                                                                             \
	})

/**
 * mgmt_msg_native_xpath_decode() - return the xpath from xpath, data message.
 * @msg: Pointer to the native message.
 * @msglen: Length of the message.
 *
 * This function decodes the xpath from a message that was encoded with
 * `mgmt_msg_native_xpath_encode()`. The variable length data is comprised of a
 * NUL terminated string followed by some data of any format.
 *
 * Return:
 *	The xpath string or NULL if there was an error decoding (i.e., the
 *	message is corrupt).
 */
/*
 * 仅取出 xpath, data 格式消息中的 xpath 字符串。校验 vsplit 合法且末尾为 NUL，
 * 否则返回 NULL（表示消息损坏）。
 */
#define mgmt_msg_native_xpath_decode(msg, msglen)                                                  \
	({                                                                                         \
		size_t _m__len = (msglen) - sizeof(*msg);                                          \
		const char *_m__s = msg->data;                                                     \
		if (!msg->vsplit || msg->vsplit > _m__len || _m__s[msg->vsplit - 1] != 0)          \
			_m__s = NULL;                                                              \
		_m__s;                                                                             \
	})

/**
 * mgmt_msg_native_data_decode() - return the data from xpath, data message.
 * @msg: Pointer to the native message.
 * @msglen: Length of the message.
 *
 * This function decodes the secondary data from a message that was encoded with
 * `mgmt_msg_native_xpath_encode()`. The variable length data is comprised of a
 * NUL terminated string followed by some data of any format.
 *
 * Return:
 *	The secondary data or NULL if there was an error decoding (i.e., the
 *	message is corrupt).
 */
/*
 * 取出 xpath, data 格式消息中的次级数据段起点。校验 vsplit 合法且 xpath 以
 * NUL 结束，否则返回 NULL（表示消息损坏）。
 */
#define mgmt_msg_native_data_decode(msg, msglen)                                                   \
	({                                                                                         \
		size_t _m__len = (msglen) - sizeof(*msg);                                          \
		const char *_m__data = msg->data + msg->vsplit;                                    \
		if (!msg->vsplit || msg->vsplit > _m__len || _m__data[-1] != 0)                    \
			_m__data = NULL;                                                           \
		_m__data;                                                                          \
	})

/**
 * mgmt_msg_native_data_len_decode() - len of data in xpath, data format message.
 * @msg: Pointer to the native message.
 * @msglen: Length of the message.
 *
 * This function returns the length of the secondary variable data from a
 * message that was encoded with `mgmt_msg_native_xpath_encode()`. The variable
 * length data is comprised of a NUL terminated string followed by some data of
 * any format.
 *
 * Return:
 *	The length of the secondary variable data. The message is assumed to be
 *	validated as not corrupt already.
 */
/*
 * 返回 xpath, data 格式消息中次级数据的长度，即总长减去结构体大小再减去
 * vsplit。假定消息已通过合法性校验、未被破坏。
 */
#define mgmt_msg_native_data_len_decode(msg, msglen)                           \
	((msglen) - sizeof(*msg) - msg->vsplit)

/**
 * mgmt_msg_native_strings_decode() - Get dynamic array of str ptrs from the msg.
 * @msg: Pointer to the native message.
 * @msglen: Length of the message.
 * @sdata: pointer to the variable length string data at end of @msg.
 *
 * Given a pointer to a sequence of NUL-terminated strings allocate
 * and return a dynamic array of dynamic array strings. This function
 * can be used to decode a message that was built using
 * mgmt_msg_native_add_str().
 *
 * Return: a dynamic array (darr) of string pointers, or NULL if the message
 * is corrupt.
 */
/*
 * 把消息尾部的“一串 NUL 结尾字符串”解码成一个字符串指针的动态数组（darr）。
 * sdata 指向变长字符串区起点。字符串区损坏时返回 NULL。用户须自行释放该数组。
 */
#define mgmt_msg_native_strings_decode(msg, msg_len, sdata)                    \
	_mgmt_msg_native_strings_decode(sdata,                                 \
					(msg_len) - ((sdata) - (char *)(msg)))

/*
 * strings_decode 的底层实现：从 sdata 起、共 sdlen 字节的字符串区，解析出
 * 各字符串并构造字符串指针数组返回；失败返回 NULL。
 */
extern const char **_mgmt_msg_native_strings_decode(const void *sdata,
						    int sdlen);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_MGMT_MSG_NATIVE_H_ */
