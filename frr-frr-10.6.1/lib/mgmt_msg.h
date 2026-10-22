// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * March 6 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 */
/*
 * 文件导读：mgmt_msg.h —— mgmtd 进程间通信的消息收发骨架（接口）
 *
 * 一、定位
 *   本文件与 mgmt_msg.c 一起，提供一套与业务无关的“基于 stream 的消息
 *   传输”机制，供 mgmtd、各 backend / frontend 客户端复用。它负责：
 *     - 连接的建立（connect / accept）与断线重连；
 *     - 消息的封帧（marker + 长度）与分片读写；
 *     - 读队列 / 写队列，并与 libfrr 的事件循环绑定；
 *     - 区分消息版本（protobuf 或 native）。
 *
 * 二、与上下层的关系
 *   上层：mgmt_be_client.c（backend 客户端）与 mgmt_fe_client.c（frontend
 *   客户端）在本骨架之上实现各自的注册、事务等语义；
 *   格式：mgmt_msg_native.h 定义 native 版本消息的 TLV 编码；
 *   本文件只关心“把一帧字节送到对端”，不关心字节里的业务含义。
 *
 * 三、关键抽象
 *   struct mgmt_msg_state 收发状态（输入 / 输出流、队列、统计、上限）；
 *   struct msg_conn       一条已建立的连接（fd、事件、读写回调、用户数据）；
 *   struct msg_client     客户端侧连接（额外带重连定时器、sopath）；
 *   struct msg_server     服务端监听对象（accept 新连接）。
 *
 * 四、阅读建议
 *   先理解 marker 与 msg_hdr 的封帧格式，再看 mgmt_msg_state 的队列设计，
 *   最后沿 msg_conn / msg_client / msg_server 三条路径理解连接生命周期
 *   与短接（short-circuit）优化。
 */
#ifndef _MGMT_MSG_H
#define _MGMT_MSG_H

#include "memory.h"
#include "stream.h"
#include "frrevent.h"

/*
 * 为 MSG_CONN 分配一个内存类型，用于 mgmt_msg 连接对象的统计与调试。
 */
DECLARE_MTYPE(MSG_CONN);

/*
 * Messages on the stream start with a marker that encodes a version octet.
 */
/*
 * 消息封帧：每条消息在流上以 4 字节 marker 开头，其低 8 位携带协议版本。
 * MGMT_MSG_MARKER_PFX 是 marker 的高 24 位固定前缀（ASCII 的 "###"）。
 */
#define MGMT_MSG_MARKER_PFX (0x23232300u) /* ASCII - "###\ooo"*/
#define MGMT_MSG_IS_MARKER(x) (((x)&0xFFFFFF00u) == MGMT_MSG_MARKER_PFX)
#define MGMT_MSG_MARKER(version) (MGMT_MSG_MARKER_PFX | (version))
#define MGMT_MSG_MARKER_VERSION(x) (0xFF & (x))

/*
 * marker 低 8 位的版本取值：
 *   MGMT_MSG_VERSION_PROTOBUF 使用 protobuf 编码（传统路径）；
 *   MGMT_MSG_VERSION_NATIVE   使用 mgmt_msg_native.h 定义的原生 TLV 编码。
 * 收发双方就一条消息须使用一致的版本。
 */
#define MGMT_MSG_VERSION_PROTOBUF 0
#define MGMT_MSG_VERSION_NATIVE 1

/*
 * 一条连接的收发状态聚合体：
 *   ins / outs   当前的输入流与输出流缓冲；
 *   inq / outq   已读入待处理、待写出的消息队列；
 *   nrxm / nrxb  累计接收的消息条数与字节数（统计用）；
 *   ntxm / ntxb  累计发送的消息条数与字节数（统计用）；
 *   max_read_buf / max_write_buf 读 / 写缓冲上限；
 *   max_msg_sz   单条消息长度上限；
 *   idtag        标识本连接用途的标签，仅用于日志。
 */
struct mgmt_msg_state {
	struct stream *ins;
	struct stream *outs;
	struct stream_fifo inq;
	struct stream_fifo outq;
	uint64_t nrxm;		/* number of received messages */
	uint64_t nrxb;		/* number of received bytes */
	uint64_t ntxm;		/* number of sent messages */
	uint64_t ntxb;		/* number of sent bytes */
	size_t max_read_buf;	/* should replace with max time value */
	size_t max_write_buf;	/* should replace with max time value */
	size_t max_msg_sz;
	char *idtag; /* identifying tag for messages */
};

/*
 * 流上每条消息的头部：marker（含版本）与 len（本帧长度）。读侧先取满该
 * 头部才能确定后续还要读多少字节。
 */
struct mgmt_msg_hdr {
	uint32_t marker;
	uint32_t len;
};

/*
 * 读侧处理完一轮后的调度决定：
 *   MSR_SCHED_BOTH   既继续处理队列又继续读；
 *   MSR_SCHED_STREAM 只需安排继续读；
 *   MSR_DISCONNECT   断开并转入重连。
 */
enum mgmt_msg_rsched {
	MSR_SCHED_BOTH,	  /* schedule both queue and read */
	MSR_SCHED_STREAM, /* schedule read */
	MSR_DISCONNECT,	  /* disconnect and start reconnecting */
};

/*
 * 写侧处理完一轮后的调度决定：
 *   MSW_SCHED_NONE     无需额外调度；
 *   MSW_SCHED_STREAM   安排继续写；
 *   MSW_DISCONNECT     断开并转入重连。
 */
enum mgmt_msg_wsched {
	MSW_SCHED_NONE,	      /* no scheduling required */
	MSW_SCHED_STREAM,     /* schedule writing */
	MSW_DISCONNECT,	      /* disconnect and start reconnecting */
};

/*
 * 连接对象的前向声明（完整定义见下方“Connections”小节）。
 */
struct msg_conn;


/*
 * 作为客户端连接到 path 指定的 unix socket，创建并返回一个 fd。
 *   sendbuf / recvbuf 为该 socket 设置的发送 / 接收缓冲大小；
 *   dbgtag            用于日志标识。
 * 返回套接字描述符，失败返回负值。
 */
extern int mgmt_msg_connect(const char *path, size_t sendbuf, size_t recvbuf,
			    const char *dbgtag);
/*
 * 从输入队列中取出并分发完整的消息：对每条消息调用 handle_msg 回调。
 *   ms         连接状态；
 *   handle_msg 每帧消息的处理回调（参数为版本、消息体指针、长度、user）；
 *   user       透传给回调的私有数据；
 *   debug      是否输出调试日志。
 * 返回 true 表示本轮有消息被处理（调用者据此决定是否继续调度）。
 */
extern bool mgmt_msg_procbufs(struct mgmt_msg_state *ms,
			      void (*handle_msg)(uint8_t version, uint8_t *msg,
						 size_t msglen, void *user),
			      void *user, bool debug);
/*
 * 从 fd 读入数据填充输入流，并按读侧策略返回后续调度决定（见
 * enum mgmt_msg_rsched）。调用者依据返回值安排继续读或断开。
 */
extern enum mgmt_msg_rsched mgmt_msg_read(struct mgmt_msg_state *ms, int fd,
					  bool debug);
/*
 * 重置写侧状态（丢弃未发出的输出队列等），返回被丢弃的待写字节数。
 * 常用于连接断开后清理。
 */
extern size_t mgmt_msg_reset_writes(struct mgmt_msg_state *ms);
/*
 * 把一条消息加入输出队列准备发送。
 *   version 协议版本（protobuf / native）；
 *   msg / len 消息体与长度；
 *   packf  可选的分帧函数，把 msg 打包进 buf，返回写入长度；
 *   debug  是否输出调试日志。
 * 返回 0 成功，负值失败。
 */
extern int mgmt_msg_send_msg(struct mgmt_msg_state *ms, uint8_t version,
			     void *msg, size_t len,
			     size_t (*packf)(void *msg, void *buf), bool debug);
/*
 * 把输出队列中的数据尽可能写入 fd，并按写侧策略返回后续调度决定（见
 * enum mgmt_msg_wsched）。
 */
extern enum mgmt_msg_wsched mgmt_msg_write(struct mgmt_msg_state *ms, int fd,
					   bool debug);

/*
 * 释放 mgmt_msg_state 占用的所有流与队列资源。
 */
extern void mgmt_msg_destroy(struct mgmt_msg_state *state);

/*
 * 初始化一个 mgmt_msg_state：创建输入 / 输出流并按给定上限约束其大小。
 *   max_read_buf / max_write_buf 读 / 写缓冲上限；
 *   max_msg_sz 单条消息长度上限；
 *   idtag 日志标识，会被复制保存。
 */
extern void mgmt_msg_init(struct mgmt_msg_state *ms, size_t max_read_buf,
			  size_t max_write_buf, size_t max_msg_sz,
			  const char *idtag);

/*
 * Connections
 */

/*
 * 一条已建立连接的核心对象：
 *   fd                  底层套接字；
 *   mstate              收发状态（见 struct mgmt_msg_state）；
 *   loop                所属事件循环；
 *   read_ev / write_ev / proc_msg_ev 读事件、写事件、消息处理事件；
 *   remote_conn         短接（short-circuit）时对应的对端连接；
 *   notify_disconnect   断开通知回调；
 *   handle_msg          消息处理回调；
 *   user                用户私有数据；
 *   short_circuit_depth 短接递归深度，防止无限递归；
 *   is_short_circuit    标记当前处理的消息是否来自短接；
 *   is_client           是否为客户端侧连接；
 *   debug               调试开关。
 */
struct msg_conn {
	int fd;
	struct mgmt_msg_state mstate;
	struct event_loop *loop;
	struct event *read_ev;
	struct event *write_ev;
	struct event *proc_msg_ev;
	struct msg_conn *remote_conn;
	int (*notify_disconnect)(struct msg_conn *conn);
	void (*handle_msg)(uint8_t version, uint8_t *data, size_t len,
			   struct msg_conn *conn);
	void *user;
	uint short_circuit_depth;
	bool is_short_circuit;	/* true when the message being handled is SC */
	bool is_client;
	bool debug;
};


/*
 * `notify_disconnect` is not called when `msg_conn_cleanup` is called for a
 * msg_conn which is currently connected. The socket is closed but there is no
 * notification.
 */
/*
 * 清理并释放一条连接：关闭 socket、摘除事件、释放资源。注意：对仍处于
 * 连接状态的 conn 调用本函数不会触发 notify_disconnect 通知。
 */
extern void msg_conn_cleanup(struct msg_conn *conn);
/*
 * 断开连接；reconnect 为 true 时转入自动重连（仅客户端侧有意义）。
 */
extern void msg_conn_disconnect(struct msg_conn *conn, bool reconnect);
/*
 * 通过连接发送一条消息；short_circuit_ok 指示是否允许走短接路径（同一
 * 进程内直接投递，不经过 socket）。
 */
extern int msg_conn_send_msg(struct msg_conn *client, uint8_t version,
			     void *msg, size_t mlen,
			     size_t (*packf)(void *, void *),
			     bool short_circuit_ok);

/*
 * Client-side Connections
 */

/*
 * 客户端侧连接：在 msg_conn 基础上增加
 *   conn_retry_tmr   重连定时器；
 *   sopath           对端 unix socket 路径；
 *   notify_connect   连接建立成功回调；
 *   short_circuit_ok 是否启用短接（用 socketpair 替代 unix socket）。
 */
struct msg_client {
	struct msg_conn conn;
	struct event *conn_retry_tmr;
	char *sopath;
	int (*notify_connect)(struct msg_client *client);
	bool short_circuit_ok;
};

/*
 * `notify_disconnect` is not called when `msg_client_cleanup` is called for a
 * msg_client which is currently connected. The socket is closed but there is no
 * notification.
 */
/*
 * 清理客户端连接对象并释放资源。对仍连接着的客户端调用本函数不会触发
 * notify_disconnect 通知（仅关闭 socket）。
 */
extern void msg_client_cleanup(struct msg_client *client);

/*
 * If `short_circuit_ok` is true, then the client-server connection will use a
 * socketpair() rather than a unix-domain socket. This must be passed true if
 * you wish to send messages short-circuit later.
 *
 * `notify_disconnect` is not called when the user `msg_client_cleanup` is
 * called for a client which is currently connected. The socket is closed
 * but there is no notification.
 */
/*
 * 初始化一个客户端连接对象。
 *   tm        事件循环；
 *   sopath    对端 unix socket 路径；
 *   notify_connect / notify_disconnect 连接建立 / 断开回调；
 *   handle_msg 消息处理回调；
 *   max_read_buf / max_write_buf / max_msg_sz 缓冲与消息上限；
 *   short_circuit_ok 为 true 时改用 socketpair，以便后续短接发送消息；
 *   idtag / debug 日志标识与调试开关。
 */
extern void
msg_client_init(struct msg_client *client, struct event_loop *tm,
		const char *sopath,
		int (*notify_connect)(struct msg_client *client),
		int (*notify_disconnect)(struct msg_conn *client),
		void (*handle_msg)(uint8_t version, uint8_t *data, size_t len,
				   struct msg_conn *client),
		size_t max_read_buf, size_t max_write_buf, size_t max_msg_sz,
		bool short_circuit_ok, const char *idtag, bool debug);

/*
 * Server-side Connections
 */
/*
 * 服务端允许同时接受的最大连接数。
 */
#define MGMTD_MAX_CONN 32

/*
 * 声明一个链表类型 msg_server_list，用于挂接多个 msg_server 实例。
 */
PREDECL_LIST(msg_server_list);

/*
 * 服务端监听对象：
 *   fd        监听套接字；
 *   link      挂入 msg_server_list 的链表节点；
 *   loop      事件循环；
 *   listen_ev 监听事件；
 *   sopath    绑定的 unix socket 路径；
 *   idtag     日志标识；
 *   create    收到新连接时用于创建 msg_conn 的工厂回调；
 *   debug     调试开关。
 */
struct msg_server {
	int fd;
	struct msg_server_list_item link;
	struct event_loop *loop;
	struct event *listen_ev;
	const char *sopath;
	const char *idtag;
	struct msg_conn *(*create)(int fd, union sockunion *su);
	struct debug *debug;
};

/*
 * 初始化服务端监听对象并开始监听 sopath。create 指定新连接的构造方式。
 * 返回 0 成功，负值失败。
 */
extern int msg_server_init(struct msg_server *server, const char *sopath,
			   struct event_loop *loop,
			   struct msg_conn *(*create)(int fd,
						      union sockunion *su),
			   const char *idtag, struct debug *debug);
/*
 * 停止监听并释放服务端对象资源。
 */
extern void msg_server_cleanup(struct msg_server *server);

/*
 * `notify_disconnect` is not called when the user `msg_conn_cleanup` is
 * called for a client which is currently connected. The socket is closed
 * but there is no notification.
 */
/*
 * 依据已 accept 的 fd 构造一条服务端连接对象。
 *   notify_disconnect / handle_msg 断开与消息处理回调；
 *   max_read / max_write / max_size 缓冲与消息上限；
 *   user 用户私有数据；idtag 日志标识。
 * 返回新连接对象。注意对仍连接着的连接调用 cleanup 不会触发断开通知。
 */
struct msg_conn *
msg_server_conn_create(struct event_loop *tm, int fd,
		       int (*notify_disconnect)(struct msg_conn *conn),
		       void (*handle_msg)(uint8_t version, uint8_t *data,
					  size_t len, struct msg_conn *conn),
		       size_t max_read, size_t max_write, size_t max_size,
		       void *user, const char *idtag);

/*
 * 关闭并删除一条服务端连接。
 */
extern void msg_server_conn_delete(struct msg_conn *conn);

/*
 * 用已 accept 的 fd 初始化一条连接对象，属于服务端侧的通用初始化路径。
 * 参数含义同 msg_server_conn_create。
 */
extern void
msg_conn_accept_init(struct msg_conn *conn, struct event_loop *tm, int fd,
		     int (*notify_disconnect)(struct msg_conn *conn),
		     void (*handle_msg)(uint8_t version, uint8_t *data,
					size_t len, struct msg_conn *conn),
		     size_t max_read, size_t max_write, size_t max_size,
		     const char *idtag);

#endif /* _MGMT_MSG_H */
