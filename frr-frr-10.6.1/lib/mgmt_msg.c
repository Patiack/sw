// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * March 6 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (C) 2021  Vmware, Inc.
 *		       Pushpasis Sarkar <spushpasis@vmware.com>
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 */
/*
 * 文件导读：mgmt_msg.c —— mgmtd 通用消息收发骨架（实现）
 *
 * 本文件实现 mgmt_msg.h 声明的、与业务无关的流式消息收发机制，被 mgmtd 及其
 * 前后端客户端共用。核心是三件事：
 *   1) 封帧：每条消息为“4 字节 marker（含版本）+ 4 字节长度 + 消息体”，
 *      见 struct mgmt_msg_hdr；
 *   2) 读：从 socket 读入字节，切分出完整消息，压入输入队列（mgmt_msg_read
 *      与 mgmt_msg_procbufs）；
 *   3) 写：把输出队列里的消息尽量写到 socket，短写则分多次（mgmt_msg_write
 *      与 mgmt_msg_send_msg）。
 *
 * 数据流向：
 *   socket 读入到 ms->ins，整帧后进 ms->inq，再由回调交给上层业务；
 *   上层业务入队到 ms->outs 或 ms->outq，再由写逻辑送到 socket。
 *
 * 关键约定：
 *   - 收发都要处理“短读 / 短写”，把不完整的部分留在 stream 里下次继续；
 *   - 单条消息超过 max_msg_sz 视为非法；发送侧过大的消息会被拒绝；
 *   - 所有函数都不阻塞，设计为可由事件循环反复重调度。
 * 更高层语义（连接对象、客户端、服务端、短接）在本目录的后续代码里实现。
 */
#include <zebra.h>
#include <sys/stat.h>

#include "debug.h"
#include "network.h"
#include "sockopt.h"
#include "stream.h"
#include "frrevent.h"
#include "mgmt_msg.h"
#include "mgmt_msg_native.h"

/*
 * 全局 trace 开关，控制 MGMT_MSG_TRACE 是否输出跟踪日志。
 */
static bool trace;

/*
 * 日志宏：MGMT_MSG_TRACE 仅在全局 trace 打开且有 dbgtag 时输出跟踪；MGMT_MSG_DBG
 * 只要有 dbgtag 就输出调试；MGMT_MSG_ERR 始终以错误级别输出（用状态里的 idtag
 * 作为标识）。dbgtag 通常只在 debug 参数为真时取自 ms->idtag，以便按需关闭日志。
 */
#define MGMT_MSG_TRACE(dbgtag, fmt, ...)                                                          \
	do {                                                                                      \
		if (dbgtag && trace)                                                              \
			zlog_debug("%s: %s: " fmt, dbgtag, __func__, ##__VA_ARGS__);              \
	} while (0)


#define MGMT_MSG_DBG(dbgtag, fmt, ...)                                         \
	do {                                                                   \
		if (dbgtag)                                                    \
			zlog_debug("%s: %s: " fmt, dbgtag, __func__,           \
				   ##__VA_ARGS__);                             \
	} while (0)

#define MGMT_MSG_ERR(ms, fmt, ...)                                             \
	zlog_err("%s: %s: " fmt, (ms)->idtag, __func__, ##__VA_ARGS__)

/*
 * 注册 MSG_CONN 内存类型，与 mgmt_msg.h 里的 DECLARE_MTYPE(MSG_CONN) 配对。
 */
DEFINE_MTYPE(LIB, MSG_CONN, "msg connection state");

/**
 * Read data from a socket into streams containing 1 or more full msgs headed by
 * mgmt_msg_hdr which contain API messages (currently protobuf).
 *
 * Args:
 *	ms: mgmt_msg_state for this process.
 *	fd: socket/file to read data from.
 *	debug: true to enable debug logging.
 *
 * Returns:
 *	MPP_DISCONNECT - socket should be closed and connect retried.
 *	MSV_SCHED_STREAM - this call should be rescheduled to run.
 *	MPP_SCHED_BOTH - this call and the procmsg buf should be scheduled to
 *run.
 */
/*
 * 从 fd 读入数据并按封帧切分出完整消息，把完整消息所在的 stream 压入输入队列。
 *   ms    收发的连接状态；
 *   fd    要读取的 socket；
 *   debug 是否输出调试日志。
 * 返回调度决定：MSR_SCHED_BOTH 有完整消息已入队（继续处理并继续读）；
 * MSR_SCHED_STREAM 暂无完整消息（继续读即可）；MSR_DISCONNECT 对端 EOF、
 * 读取出错或数据损坏（应断开并重连）。
 * 关键点：stream 若装不下一条超长消息会按需扩容；读到的不完整尾巴会被搬到新的
 * stream 里留给下次继续。
 */
enum mgmt_msg_rsched mgmt_msg_read(struct mgmt_msg_state *ms, int fd,
				   bool debug)
{
	const char *dbgtag = debug ? ms->idtag : NULL;
	size_t avail = STREAM_WRITEABLE(ms->ins);
	struct mgmt_msg_hdr *mhdr = NULL;
	size_t total = 0;
	size_t mcount = 0;
	ssize_t n, left;

	assert(ms && fd != -1);
	MGMT_MSG_TRACE(dbgtag, "enter with %zu bytes available to read on fd %d", avail, fd);

	/*
	 * Read as much as we can into the stream.
	 */
	while (avail > sizeof(struct mgmt_msg_hdr)) {
		n = stream_read_try(ms->ins, fd, avail);

		/* -2 is normal nothing read, and to retry */
		if (n == -2) {
			MGMT_MSG_TRACE(dbgtag, "nothing more to read on fd %d", fd);
			break;
		}
		if (n <= 0) {
			if (n == 0)
				MGMT_MSG_ERR(ms, "got EOF/disconnect on fd %d", fd);
			else
				MGMT_MSG_ERR(ms, "got error while reading on fd %d: '%s'", fd,
					     safe_strerror(errno));
			return MSR_DISCONNECT;
		}
		MGMT_MSG_TRACE(dbgtag, "read %zd bytes on fd %d", n, fd);
		ms->nrxb += n;
		avail -= n;
	}

	/*
	 * Check if we have read a complete messages or not.
	 */
	assert(stream_get_getp(ms->ins) == 0);
	left = stream_get_endp(ms->ins);
	while (left > (ssize_t)sizeof(struct mgmt_msg_hdr)) {
		mhdr = (struct mgmt_msg_hdr *)(STREAM_DATA(ms->ins) + total);
		if (!MGMT_MSG_IS_MARKER(mhdr->marker)) {
			MGMT_MSG_DBG(dbgtag, "recv corrupt buffer on fd %d, disconnect", fd);
			return MSR_DISCONNECT;
		}
		if ((ssize_t)mhdr->len > left)
			break;

		MGMT_MSG_TRACE(dbgtag, "read full message on fd %d len %u", fd, mhdr->len);
		total += mhdr->len;
		left -= mhdr->len;
		mcount++;
	}

	if (!mcount) {
		/* Didn't manage to read a full message */
		if (mhdr && avail == 0) {
			struct stream *news;
			/*
			 * Message was longer than what was left and we have no
			 * available space to read more in. B/c mcount == 0 the
			 * message starts at the beginning of the stream so
			 * therefor the stream is too small to fit the message..
			 * Resize the stream to fit.
			 */
			news = stream_new(mhdr->len);
			stream_put(news, mhdr, left);
			stream_set_endp(news, left);
			stream_free(ms->ins);
			ms->ins = news;
		}
		return MSR_SCHED_STREAM;
	}

	/*
	 * We have read at least one message into the stream, queue it up.
	 */
	mhdr = (struct mgmt_msg_hdr *)(STREAM_DATA(ms->ins) + total);
	stream_set_endp(ms->ins, total);
	stream_fifo_push(&ms->inq, ms->ins);
	if (left < (ssize_t)sizeof(struct mgmt_msg_hdr))
		ms->ins = stream_new(ms->max_msg_sz);
	else
		/* handle case where message is greater than max */
		ms->ins = stream_new(MAX(ms->max_msg_sz, mhdr->len));
	if (left) {
		stream_put(ms->ins, mhdr, left);
		stream_set_endp(ms->ins, left);
	}

	return MSR_SCHED_BOTH;
}

/**
 * Process streams containing whole messages that have been pushed onto the
 * FIFO. This should be called from an event/timer handler and should be
 * reschedulable.
 *
 * Args:
 *	ms: mgmt_msg_state for this process.
 *	handle_mgs: function to call for each received message.
 *	user: opaque value passed through to handle_msg.
 *	debug: true to enable debug logging.
 *
 * Returns:
 *	true if more to process (so reschedule) else false
 */
/*
 * 取出输入队列中的完整消息，逐条调用 handle_msg 回调。
 *   ms         连接状态；
 *   handle_msg 每帧消息的处理回调（参数为版本、消息体、长度、user）；
 *   user       透传给回调的私有数据；
 *   debug      是否输出调试日志。
 * 返回 true 表示队列里还有消息待处理（应重调度本函数），否则 false。
 * 注意：每轮最多处理 max_read_buf 条，避免长时间独占事件循环。
 */
bool mgmt_msg_procbufs(struct mgmt_msg_state *ms,
		       void (*handle_msg)(uint8_t version, uint8_t *msg,
					  size_t msglen, void *user),
		       void *user, bool debug)
{
	const char *dbgtag = debug ? ms->idtag : NULL;
	struct mgmt_msg_hdr *mhdr;
	struct stream *work;
	uint8_t *data;
	size_t left, nproc;

	MGMT_MSG_TRACE(dbgtag, "Have %zu streams to process", ms->inq.count);

	nproc = 0;
	while (nproc < ms->max_read_buf) {
		work = stream_fifo_pop(&ms->inq);
		if (!work)
			break;

		data = STREAM_DATA(work);
		left = stream_get_endp(work);
		MGMT_MSG_TRACE(dbgtag, "Processing stream of len %zu", left);

		for (; left > sizeof(struct mgmt_msg_hdr);
		     left -= mhdr->len, data += mhdr->len) {
			mhdr = (struct mgmt_msg_hdr *)data;

			assert(MGMT_MSG_IS_MARKER(mhdr->marker));
			assert(left >= mhdr->len);

			/*
			 * Q: if the handler disconnects should stop/flush?
			 */
			handle_msg(MGMT_MSG_MARKER_VERSION(mhdr->marker), (uint8_t *)(mhdr + 1),
				   mhdr->len - sizeof(struct mgmt_msg_hdr), user);
			ms->nrxm++;
			nproc++;
		}

		if (work != ms->ins)
			stream_free(work); /* Free it up */
		else
			stream_reset(work); /* Reset stream for next read */
	}

	/* return true if should reschedule b/c more to process. */
	return stream_fifo_head(&ms->inq) != NULL;
}

/**
 * Write data onto the socket, using streams that have been queued for
 * sending by mgmt_msg_send_msg. This function should be reschedulable.
 *
 * Args:
 *	ms: mgmt_msg_state for this process.
 *	fd: socket/file to read data from.
 *	debug: true to enable debug logging.
 *
 * Returns:
 *	MSW_SCHED_NONE - do not reschedule anything.
 *	MSW_SCHED_STREAM - this call should be rescheduled to run again.
 *	MSW_SCHED_WRITES_OFF - writes should be disabled with a timer to
 *	    re-enable them a short time later
 *	MSW_DISCONNECT - socket should be closed and reconnect retried.
 *run.
 */
/*
 * 把输出队列中的消息尽量写入 fd。
 *   ms、fd、debug 含义同 mgmt_msg_read。
 * 返回：MSW_SCHED_NONE 全部写完；MSW_SCHED_STREAM 遇可重试错误或达到单轮上限，
 * 需继续写；MSW_DISCONNECT 连接关闭或写错误（清空待写队列后断开重连）。
 * 关键点：短写时只推进已写部分、下次继续，不会丢弃剩余数据。
 */
enum mgmt_msg_wsched mgmt_msg_write(struct mgmt_msg_state *ms, int fd,
				    bool debug)
{
	const char *dbgtag = debug ? ms->idtag : NULL;
	struct stream *s;
	size_t nproc = 0;
	ssize_t left;
	ssize_t n;

	if (ms->outs) {
		MGMT_MSG_TRACE(dbgtag, "found unqueued stream with %zu bytes on fd %d, queueing",
			       stream_get_endp(ms->outs), fd);
		stream_fifo_push(&ms->outq, ms->outs);
		ms->outs = NULL;
	}

	for (s = stream_fifo_head(&ms->outq); s && nproc < ms->max_write_buf;
	     s = stream_fifo_head(&ms->outq)) {
		left = STREAM_READABLE(s);
		assert(left);

		n = stream_flush(s, fd);
		if (n <= 0) {
			if (n == 0)
				MGMT_MSG_ERR(ms, "connection closed while writing on fd %d", fd);
			else if (ERRNO_IO_RETRY(errno)) {
				MGMT_MSG_DBG(dbgtag,
					     "retry error while writing %zd bytes on fd %d: %s (%d)",
					     left, fd, safe_strerror(errno), errno);
				return MSW_SCHED_STREAM;
			} else
				MGMT_MSG_ERR(ms, "error while writing %zd bytes on fd %d: %s (%d)",
					     left, fd, safe_strerror(errno), errno);

			n = mgmt_msg_reset_writes(ms);
			MGMT_MSG_ERR(ms, "drop, freed %zd streams on fd %d for disconnect", n, fd);

			return MSW_DISCONNECT;
		}

		ms->ntxb += n;
		if (n != left) {
			MGMT_MSG_DBG(dbgtag, "short stream write %zd of %zd on fd %d", n, left, fd);
			stream_forward_getp(s, n);
			return MSW_SCHED_STREAM;
		}

		stream_free(stream_fifo_pop(&ms->outq));
		MGMT_MSG_TRACE(dbgtag, "wrote stream of %zd bytes on fd %d", n, fd);
		nproc++;
	}
	if (s) {
		MGMT_MSG_DBG(dbgtag,
			     "reached %zu buffer writes on fd %d, pausing with %zu streams left",
			     ms->max_write_buf, fd, ms->outq.count);
		return MSW_SCHED_STREAM;
	}
	MGMT_MSG_TRACE(dbgtag, "flushed all streams from output q for fd %d", fd);
	return MSW_SCHED_NONE;
}


/**
 * Send a message by enqueueing it to be written over the socket by
 * mgmt_msg_write.
 *
 * Args:
 *	ms: mgmt_msg_state for this process.
 *	version: version of this message, will be given to receiving side.
 *	msg: the message to be sent.
 *	len: the length of the message.
 *	packf: a function to pack the message.
 *	debug: true to enable debug logging.
 *
 * Returns:
 *      0 on success, otherwise -1 on failure. The only failure mode is if a
 *      the message exceeds the maximum message size configured on init.
 */
/*
 * 把一条消息加入待发送的 stream（真正写出由 mgmt_msg_write 完成）。
 *   version 消息版本，会随帧头一起发给对端；
 *   msg、len 消息体与长度；
 *   packf   可选的分帧 / 打包函数；
 *   debug   是否输出调试日志。
 * 返回 0 成功，-1 失败（唯一失败原因是消息超过初始化时配置的最大长度）。
 * 内部按需复用或新建 stream，并把写满的 stream 压入输出队列。
 */
int mgmt_msg_send_msg(struct mgmt_msg_state *ms, uint8_t version, void *msg,
		      size_t len, size_t (*packf)(void *msg, void *buf),
		      bool debug)
{
	const char *dbgtag = debug ? ms->idtag : NULL;
	struct mgmt_msg_hdr *mhdr;
	struct stream *s;
	uint8_t *dstbuf;
	size_t endp, n;
	size_t mlen = len + sizeof(*mhdr);

	if (mlen > ms->max_msg_sz)
		MGMT_MSG_TRACE(dbgtag, "Sending large msg size %zu > max size %zu", mlen,
			       ms->max_msg_sz);

	if (!ms->outs) {
		MGMT_MSG_TRACE(dbgtag, "creating new stream for msg len %zu", mlen);
		ms->outs = stream_new(MAX(ms->max_msg_sz, mlen));
	} else if (mlen > ms->max_msg_sz && ms->outs->endp == 0) {
		/* msg is larger than stream max size get a fit-to-size stream */
		MGMT_MSG_TRACE(dbgtag, "replacing old stream with fit-to-size for msg len %zu",
			       mlen);
		stream_free(ms->outs);
		ms->outs = stream_new(mlen);
	} else if (STREAM_WRITEABLE(ms->outs) < mlen) {
		MGMT_MSG_TRACE(dbgtag,
			       "enq existing stream len %zu and creating new stream for msg len %zu",
			       STREAM_WRITEABLE(ms->outs), mlen);
		stream_fifo_push(&ms->outq, ms->outs);
		ms->outs = stream_new(MAX(ms->max_msg_sz, mlen));
	} else {
		MGMT_MSG_TRACE(dbgtag, "using existing stream with avail %zu for msg len %zu",
			       STREAM_WRITEABLE(ms->outs), mlen);
	}
	s = ms->outs;

	if (dbgtag && version == MGMT_MSG_VERSION_NATIVE) {
		struct mgmt_msg_header *native_msg = msg;

		MGMT_MSG_DBG(
			dbgtag,
			"Sending native msg sess/txn-id %"PRIu64" req-id %"PRIu64" code %u",
			native_msg->refer_id, native_msg->req_id, native_msg->code);

	}

	/* We have a stream with space, pack the message into it. */
	mhdr = (struct mgmt_msg_hdr *)(STREAM_DATA(s) + s->endp);
	mhdr->marker = MGMT_MSG_MARKER(version);
	mhdr->len = mlen;
	stream_forward_endp(s, sizeof(*mhdr));
	endp = stream_get_endp(s);
	dstbuf = STREAM_DATA(s) + endp;
	if (packf)
		n = packf(msg, dstbuf);
	else {
		memcpy(dstbuf, msg, len);
		n = len;
	}
	stream_set_endp(s, endp + n);
	ms->ntxm++;

	return 0;
}

/**
 * Create and open a unix domain stream socket on the given path
 * setting non-blocking and send and receive buffer sizes.
 *
 * Args:
 *	path: path of unix domain socket to connect to.
 *	sendbuf: size of socket send buffer.
 *	recvbuf: size of socket receive buffer.
 *	dbgtag: if non-NULL enable log debug, and use this tag.
 *
 * Returns:
 *	socket fd or -1 on error.
 */
/*
 * 创建一个非阻塞的 unix 域流式 socket 并连接到 path。
 *   path    要连接的对端 socket 路径；
 *   sendbuf / recvbuf 设置 socket 发送 / 接收缓冲大小；
 *   dbgtag  非 NULL 时启用调试日志并用作标识。
 * 返回 socket fd，失败返回 -1。连接成功后立即设为非阻塞。
 */
int mgmt_msg_connect(const char *path, size_t sendbuf, size_t recvbuf,
		     const char *dbgtag)
{
	int ret, sock, len;
	struct sockaddr_un addr;

	MGMT_MSG_DBG(dbgtag, "connecting to server on %s", path);
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		MGMT_MSG_DBG(dbgtag, "socket failed: %s", safe_strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	len = addr.sun_len = SUN_LEN(&addr);
#else
	len = sizeof(addr.sun_family) + strlen(addr.sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */
	ret = connect(sock, (struct sockaddr *)&addr, len);
	if (ret < 0) {
		MGMT_MSG_DBG(dbgtag, "failed to connect on %s: %s", path,
			     safe_strerror(errno));
		close(sock);
		return -1;
	}

	MGMT_MSG_DBG(dbgtag, "connected fd %d to server on %s", sock, path);
	set_nonblocking(sock);
	setsockopt_so_sendbuf(sock, sendbuf);
	setsockopt_so_recvbuf(sock, recvbuf);
	return sock;
}

/**
 * Reset the sending queue, by dequeueing all streams and freeing them. Return
 * the number of streams freed.
 *
 * Args:
 *	ms: mgmt_msg_state for this process.
 *
 * Returns:
 *      Number of streams that were freed.
 *
 */
/*
 * 清空发送队列：弹出并释放所有待写 stream，返回释放的 stream 数量。
 * 连接断开时用来丢弃积压的待发数据。
 */
size_t mgmt_msg_reset_writes(struct mgmt_msg_state *ms)
{
	struct stream *s;
	size_t nproc = 0;

	for (s = stream_fifo_pop(&ms->outq); s;
	     s = stream_fifo_pop(&ms->outq), nproc++)
		stream_free(s);

	return nproc;
}


/*
 * 初始化 mgmt_msg_state：清零、创建输入流与两个 FIFO 队列、记录各项上限，
 * 并复制保存 idtag。注意：代码里把 max_read_buf 与 max_write_buf 做了交叉
 * 赋值，阅读时以下游实际使用到的字段含义为准。
 */
void mgmt_msg_init(struct mgmt_msg_state *ms, size_t max_read_buf,
		   size_t max_write_buf, size_t max_msg_sz, const char *idtag)
{
	memset(ms, 0, sizeof(*ms));
	ms->ins = stream_new(max_msg_sz);
	stream_fifo_init(&ms->inq);
	stream_fifo_init(&ms->outq);
	ms->max_read_buf = max_write_buf;
	ms->max_write_buf = max_read_buf;
	ms->max_msg_sz = max_msg_sz;
	ms->idtag = strdup(idtag);
}

/*
 * 释放 mgmt_msg_state：清空待写队列，释放输入 / 输出流与 idtag。注意：输入
 * 队列（inq）中的 stream 不在此释放，调用前应确保其已被处理或回收。
 */
void mgmt_msg_destroy(struct mgmt_msg_state *ms)
{
	mgmt_msg_reset_writes(ms);
	if (ms->ins)
		stream_free(ms->ins);
	if (ms->outs)
		stream_free(ms->outs);
	free(ms->idtag);
}

/*
 * Connections
 */

/*
 * 连接层默认参数：断线后的重连间隔（毫秒），以及 socket 的发送 / 接收缓冲
 * 大小（各 64KB）。
 */
#define MSG_CONN_DEFAULT_CONN_RETRY_MSEC 250
#define MSG_CONN_SEND_BUF_SIZE (1u << 16)
#define MSG_CONN_RECV_BUF_SIZE (1u << 16)

static void msg_client_sched_connect(struct msg_client *client,
				     unsigned long msec);

/*
 * 以下为本文件内部使用的静态函数前向声明：调度重连（msg_client_sched_connect）
 * 以及调度处理消息 / 读 / 写。
 */
static void msg_conn_sched_proc_msgs(struct msg_conn *conn);
static void msg_conn_sched_read(struct msg_conn *conn);
static void msg_conn_sched_write(struct msg_conn *conn);

/*
 * 写事件回调：调用 mgmt_msg_write 写出数据，并按返回值安排继续写、断开或结束。
 */
static void msg_conn_write(struct event *event)
{
	struct msg_conn *conn = EVENT_ARG(event);
	enum mgmt_msg_wsched rv;

	rv = mgmt_msg_write(&conn->mstate, conn->fd, conn->debug);
	if (rv == MSW_SCHED_STREAM)
		msg_conn_sched_write(conn);
	else if (rv == MSW_DISCONNECT)
		msg_conn_disconnect(conn, conn->is_client);
	else
		assert(rv == MSW_SCHED_NONE);
}

/*
 * 读事件回调：调用 mgmt_msg_read 读入数据；若需断开则触发断连（客户端会自动
 * 重连）；若有完整消息则调度处理，并继续安排下一次读。
 */
static void msg_conn_read(struct event *event)
{
	struct msg_conn *conn = EVENT_ARG(event);
	enum mgmt_msg_rsched rv;

	rv = mgmt_msg_read(&conn->mstate, conn->fd, conn->debug);
	if (rv == MSR_DISCONNECT) {
		msg_conn_disconnect(conn, conn->is_client);
		return;
	}
	if (rv == MSR_SCHED_BOTH)
		msg_conn_sched_proc_msgs(conn);
	msg_conn_sched_read(conn);
}

/* collapse this into mgmt_msg_procbufs */
/*
 * 消息处理事件回调：调用 mgmt_msg_procbufs 逐条处理已入队的消息；若仍有剩余
 * 则再次调度本函数继续处理。
 */
static void msg_conn_proc_msgs(struct event *event)
{
	struct msg_conn *conn = EVENT_ARG(event);

	if (mgmt_msg_procbufs(&conn->mstate,
			      (void (*)(uint8_t, uint8_t *, size_t,
					void *))conn->handle_msg,
			      conn, conn->debug))
		/* there's more, schedule handling more */
		msg_conn_sched_proc_msgs(conn);
}

/*
 * 在当前事件循环上为连接注册读事件（fd 可读时调用 msg_conn_read）。
 */
static void msg_conn_sched_read(struct msg_conn *conn)
{
	event_add_read(conn->loop, msg_conn_read, conn, conn->fd,
		       &conn->read_ev);
}

/*
 * 在当前事件循环上为连接注册写事件（fd 可写时调用 msg_conn_write）。
 */
static void msg_conn_sched_write(struct msg_conn *conn)
{
	event_add_write(conn->loop, msg_conn_write, conn, conn->fd,
			&conn->write_ev);
}

/*
 * 在当前事件循环上注册一个立即执行的事件，异步处理输入队列中的消息，避免在
 * 读写回调里长时间占用。
 */
static void msg_conn_sched_proc_msgs(struct msg_conn *conn)
{
	event_add_event(conn->loop, msg_conn_proc_msgs, conn, 0,
			&conn->proc_msg_ev);
}


/*
 * 断开连接：解除短接关联，关闭 fd，取消读写事件，并调用 notify_disconnect
 * 通知上层。reconnect 为真时（要求是客户端连接）安排稍后重连。
 * 注意：只有 fd 原本有效时才会关闭并触发断开通知；重复调用是安全的。
 */
void msg_conn_disconnect(struct msg_conn *conn, bool reconnect)
{

	/* disconnect short-circuit if present */
	if (conn->remote_conn) {
		conn->remote_conn->remote_conn = NULL;
		conn->remote_conn = NULL;
	}

	if (conn->fd != -1) {
		close(conn->fd);
		conn->fd = -1;

		/* We need to unschedule any pending events on this fd */
		event_cancel(&conn->read_ev);
		event_cancel(&conn->write_ev);

		/* Notify client through registered callback (if any) */
		if (conn->notify_disconnect)
			(void)(*conn->notify_disconnect)(conn);
	}

	if (reconnect) {
		assert(conn->is_client);
		msg_client_sched_connect(
			container_of(conn, struct msg_client, conn),
			MSG_CONN_DEFAULT_CONN_RETRY_MSEC);
	}
}

/*
 * 发送一条消息。若连接存在短接对端（remote_conn）且 short_circuit_ok 为真，
 * 则不经 socket，直接把消息交给对端的 handle_msg 同步处理（用 is_short_circuit
 * 标记、short_circuit_depth 记录嵌套深度）；否则入队并由写事件异步送出。
 * 连接已关闭（fd 为 -1）时返回 -1；返回 0 成功，负值失败。
 */
int msg_conn_send_msg(struct msg_conn *conn, uint8_t version, void *msg,
		      size_t mlen, size_t (*packf)(void *, void *),
		      bool short_circuit_ok)
{
	const char *dbgtag = conn->debug ? conn->mstate.idtag : NULL;

	if (conn->fd == -1) {
		MGMT_MSG_ERR(&conn->mstate,
			     "can't send message on closed connection");
		return -1;
	}

	/* immediately handle the message if short-circuit is present */
	if (conn->remote_conn && short_circuit_ok) {
		uint8_t *buf = msg;
		size_t n = mlen;
		bool old;

		if (packf) {
			buf = XMALLOC(MTYPE_TMP, mlen);
			n = packf(msg, buf);
		}

		++conn->short_circuit_depth;
		MGMT_MSG_DBG(dbgtag, "SC send: depth %u msg: %p",
			     conn->short_circuit_depth, msg);

		old = conn->remote_conn->is_short_circuit;
		conn->remote_conn->is_short_circuit = true;
		conn->remote_conn->handle_msg(version, buf, n,
					      conn->remote_conn);
		conn->remote_conn->is_short_circuit = old;

		--conn->short_circuit_depth;
		MGMT_MSG_DBG(dbgtag, "SC return from depth: %u msg: %p",
			     conn->short_circuit_depth, msg);

		if (packf)
			XFREE(MTYPE_TMP, buf);
		return 0;
	}

	int rv = mgmt_msg_send_msg(&conn->mstate, version, msg, mlen, packf,
				   conn->debug);

	msg_conn_sched_write(conn);

	return rv;
}

/*
 * 清理一条连接：解除短接关联、关闭 fd、取消全部事件，并释放底层
 * mgmt_msg_state。注意：对仍连接着的连接调用本函数不触发 notify_disconnect。
 */
void msg_conn_cleanup(struct msg_conn *conn)
{
	struct mgmt_msg_state *ms = &conn->mstate;

	/* disconnect short-circuit if present */
	if (conn->remote_conn) {
		conn->remote_conn->remote_conn = NULL;
		conn->remote_conn = NULL;
	}

	if (conn->fd != -1) {
		close(conn->fd);
		conn->fd = -1;
	}

	event_cancel(&conn->read_ev);
	event_cancel(&conn->write_ev);
	event_cancel(&conn->proc_msg_ev);

	mgmt_msg_destroy(ms);
}

/*
 * Client Connections
 */

/*
 * 定义 msg_server_list 链表类型（元素通过 struct msg_server 里的 link 成员串接）
 * 及其头部类型，用于登记本进程所有监听中的服务端对象。
 */
DECLARE_LIST(msg_server_list, struct msg_server, link);

/*
 * 本进程所有已启动的 msg_server 的全局链表头，短接连接时据此按 sopath 查找
 * 匹配的服务端。
 */
static struct msg_server_list_head msg_servers;

/*
 * 前向声明：真正发起（或重试）客户端到服务端的连接。
 */
static void msg_client_connect(struct msg_client *conn);

/*
 * 重连定时器 / 事件回调：时间到后调用 msg_client_connect 发起连接尝试。
 */
static void msg_client_connect_timer(struct event *event)
{
	msg_client_connect(EVENT_ARG(event));
}

/*
 * 安排一次连接尝试：msec 为 0 时用立即执行的事件，否则用 msec 毫秒后的定时器，
 * 到期后由 msg_client_connect_timer 触发 msg_client_connect。
 */
static void msg_client_sched_connect(struct msg_client *client,
				     unsigned long msec)
{
	struct msg_conn *conn = &client->conn;
	const char *dbgtag = conn->debug ? conn->mstate.idtag : NULL;

	MGMT_MSG_DBG(dbgtag, "connection retry in %lu msec", msec);
	if (msec)
		event_add_timer_msec(conn->loop, msg_client_connect_timer,
				     client, msec, &client->conn_retry_tmr);
	else
		event_add_event(conn->loop, msg_client_connect_timer, client, 0,
				&client->conn_retry_tmr);
}

/*
 * 建立“短接”连接：不经过 unix socket，而是在同一进程内用 socketpair 把客户端
 * 与服务端直接对接。做法是先在 msg_servers 里按 sopath 找到匹配的服务端，再用
 * socketpair 造出两个 fd，一端作为客户端 fd，另一端交给服务端的 create 回调
 * 生成 server_conn，然后互相记录 remote_conn，此后消息可同步直达。
 * 返回 0 成功，-1 失败（尚无对应服务端或 socketpair 失败）。
 */
static int msg_client_connect_short_circuit(struct msg_client *client)
{
	struct msg_conn *server_conn;
	struct msg_server *server;
	const char *dbgtag =
		client->conn.debug ? client->conn.mstate.idtag : NULL;
	union sockunion su = {};
	int sockets[2];

	frr_each (msg_server_list, &msg_servers, server)
		if (!strcmp(server->sopath, client->sopath))
			break;
	if (!server) {
		MGMT_MSG_DBG(dbgtag,
			     "no short-circuit server available yet for %s",
			     client->sopath);
		return -1;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) {
		MGMT_MSG_ERR(
			&client->conn.mstate,
			"socketpair failed trying to short-circuit connection on %s: %s",
			client->sopath, safe_strerror(errno));
		return -1;
	}

	/* client side */
	client->conn.fd = sockets[0];
	set_nonblocking(sockets[0]);
	setsockopt_so_sendbuf(sockets[0], client->conn.mstate.max_write_buf);
	setsockopt_so_recvbuf(sockets[0], client->conn.mstate.max_read_buf);

	/* server side */
	memset(&su, 0, sizeof(union sockunion));
	server_conn = server->create(sockets[1], &su);
	server_conn->debug = DEBUG_MODE_CHECK(server->debug, DEBUG_MODE_ALL)
				     ? true
				     : false;

	client->conn.remote_conn = server_conn;
	server_conn->remote_conn = &client->conn;

	MGMT_MSG_DBG(
		dbgtag,
		"short-circuit connection on %s server %s:%d to client %s:%d",
		client->sopath, server_conn->mstate.idtag, server_conn->fd,
		client->conn.mstate.idtag, client->conn.fd);

	MGMT_MSG_DBG(
		server_conn->debug ? server_conn->mstate.idtag : NULL,
		"short-circuit connection on %s client %s:%d to server %s:%d",
		client->sopath, client->conn.mstate.idtag, client->conn.fd,
		server_conn->mstate.idtag, server_conn->fd);

	return 0;
}


/* Connect and start reading from the socket */
/*
 * 发起一次连接：非短接时用 mgmt_msg_connect 连接 sopath，短接时走
 * msg_client_connect_short_circuit；失败则安排重试；成功则先回调 notify_connect
 * （返回非 0 视为失败，断开并重连），否则开始读。
 */
static void msg_client_connect(struct msg_client *client)
{
	struct msg_conn *conn = &client->conn;
	const char *dbgtag = conn->debug ? conn->mstate.idtag : NULL;

	if (!client->short_circuit_ok)
		conn->fd =
			mgmt_msg_connect(client->sopath, MSG_CONN_SEND_BUF_SIZE,
					 MSG_CONN_RECV_BUF_SIZE, dbgtag);
	else if (msg_client_connect_short_circuit(client))
		conn->fd = -1;

	if (conn->fd == -1)
		/* retry the connection */
		msg_client_sched_connect(client,
					 MSG_CONN_DEFAULT_CONN_RETRY_MSEC);
	else if (client->notify_connect && client->notify_connect(client))
		/* client connect notify failed */
		msg_conn_disconnect(conn, true);
	else
		/* start reading */
		msg_conn_sched_read(conn);
}

/*
 * 初始化客户端连接对象并立即安排首次连接（延迟 0）。
 *   tm        事件循环；
 *   sopath    对端 unix socket 路径（会被复制保存）；
 *   notify_connect / notify_disconnect 连接建立 / 断开回调；
 *   handle_msg 消息处理回调；
 *   max_read_buf / max_write_buf / max_msg_sz 缓冲与消息上限；
 *   short_circuit_ok 为真时走 socketpair 短接；
 *   idtag / debug 日志标识与调试开关。
 */
void msg_client_init(struct msg_client *client, struct event_loop *tm,
		     const char *sopath,
		     int (*notify_connect)(struct msg_client *client),
		     int (*notify_disconnect)(struct msg_conn *client),
		     void (*handle_msg)(uint8_t version, uint8_t *data,
					size_t len, struct msg_conn *client),
		     size_t max_read_buf, size_t max_write_buf,
		     size_t max_msg_sz, bool short_circuit_ok,
		     const char *idtag, bool debug)
{
	struct msg_conn *conn = &client->conn;
	memset(client, 0, sizeof(*client));

	conn->loop = tm;
	conn->fd = -1;
	conn->handle_msg = handle_msg;
	conn->notify_disconnect = notify_disconnect;
	conn->is_client = true;
	conn->debug = debug;
	client->short_circuit_ok = short_circuit_ok;
	client->sopath = strdup(sopath);
	client->notify_connect = notify_connect;

	mgmt_msg_init(&conn->mstate, max_read_buf, max_write_buf, max_msg_sz,
		      idtag);

	/* Start trying to connect to server */
	msg_client_sched_connect(client, 0);
}

/*
 * 清理客户端连接：断言是客户端连接，取消重连定时器，释放 sopath，并清理底层
 * 连接。对仍连接着的客户端调用不会触发 notify_disconnect。
 */
void msg_client_cleanup(struct msg_client *client)
{
	assert(client->conn.is_client);

	event_cancel(&client->conn_retry_tmr);
	free(client->sopath);

	msg_conn_cleanup(&client->conn);
}


/*
 * Server-side connections
 */

/*
 * 监听套接字可读时的回调：继续挂上下一次监听，accept 出新连接，设为非阻塞并
 * 关闭 exec 继承，然后调用 server->create 构造服务端连接对象并设置其调试开关。
 */
static void msg_server_accept(struct event *event)
{
	struct msg_server *server = EVENT_ARG(event);
	struct msg_conn *conn;
	union sockunion su;
	int fd;

	if (server->fd < 0)
		return;

	/* We continue hearing server listen socket. */
	event_add_read(server->loop, msg_server_accept, server, server->fd,
		       &server->listen_ev);

	memset(&su, 0, sizeof(union sockunion));

	/* We can handle IPv4 or IPv6 socket. */
	fd = sockunion_accept(server->fd, &su);
	if (fd < 0) {
		zlog_err("Failed to accept %s client connection: %s",
			 server->idtag, safe_strerror(errno));
		return;
	}
	set_nonblocking(fd);
	set_cloexec(fd);

	DEBUGD(server->debug, "Accepted new %s connection", server->idtag);

	conn = server->create(fd, &su);
	if (conn)
		conn->debug = DEBUG_MODE_CHECK(server->debug, DEBUG_MODE_ALL)
				      ? true
				      : false;
}

/*
 * 初始化并启动服务端监听：创建 unix 域 socket，绑定 sopath（先 unlink 旧文件，
 * 期间收紧 umask），listen 后登记到 msg_servers 链表并挂上 accept 事件。
 * 返回 0 成功，-1 失败。
 */
int msg_server_init(struct msg_server *server, const char *sopath,
		    struct event_loop *loop,
		    struct msg_conn *(*create)(int fd, union sockunion *su),
		    const char *idtag, struct debug *debug)
{
	int ret;
	int sock;
	struct sockaddr_un addr;
	mode_t old_mask;

	memset(server, 0, sizeof(*server));
	server->fd = -1;

	sock = socket(AF_UNIX, SOCK_STREAM, PF_UNSPEC);
	if (sock < 0) {
		zlog_err("Failed to create %s server socket: %s", server->idtag,
			 safe_strerror(errno));
		goto fail;
	}

	addr.sun_family = AF_UNIX,
	strlcpy(addr.sun_path, sopath, sizeof(addr.sun_path));
	unlink(addr.sun_path);
	old_mask = umask(0077);
	ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		zlog_err("Failed to bind %s server socket to '%s': %s",
			 server->idtag, addr.sun_path, safe_strerror(errno));
		umask(old_mask);
		goto fail;
	}
	umask(old_mask);

	ret = listen(sock, MGMTD_MAX_CONN);
	if (ret < 0) {
		zlog_err("Failed to listen on %s server socket: %s",
			 server->idtag, safe_strerror(errno));
		goto fail;
	}

	server->fd = sock;
	server->loop = loop;
	server->sopath = strdup(sopath);
	server->idtag = strdup(idtag);
	server->create = create;
	server->debug = debug;

	msg_server_list_add_head(&msg_servers, server);

	event_add_read(server->loop, msg_server_accept, server, server->fd,
		       &server->listen_ev);


	DEBUGD(debug, "Started %s server, listening on %s", idtag, sopath);
	return 0;

fail:
	if (sock >= 0)
		close(sock);
	server->fd = -1;
	return -1;
}

/*
 * 停止服务端：取消监听事件，从 msg_servers 链表摘除，关闭监听 fd，释放 sopath
 * 与 idtag，并清零对象。
 */
void msg_server_cleanup(struct msg_server *server)
{
	DEBUGD(server->debug, "Closing %s server", server->idtag);

	if (server->listen_ev)
		event_cancel(&server->listen_ev);

	msg_server_list_del(&msg_servers, server);

	if (server->fd >= 0)
		close(server->fd);
	free((char *)server->sopath);
	free((char *)server->idtag);

	memset(server, 0, sizeof(*server));
	server->fd = -1;
}

/*
 * Initialize and start reading from the accepted socket
 *
 *     notify_connect - only called for disconnect i.e., connected == false
 */
/*
 * 用已 accept 的 fd 初始化一条（服务端）连接：设置事件循环、回调，标记为非
 * 客户端，初始化 mgmt_msg_state 并开始读，最后把 socket 设为非阻塞并设置收发
 * 缓冲大小。
 */
void msg_conn_accept_init(struct msg_conn *conn, struct event_loop *tm, int fd,
			  int (*notify_disconnect)(struct msg_conn *conn),
			  void (*handle_msg)(uint8_t version, uint8_t *data,
					     size_t len, struct msg_conn *conn),
			  size_t max_read, size_t max_write, size_t max_size,
			  const char *idtag)
{
	conn->loop = tm;
	conn->fd = fd;
	conn->notify_disconnect = notify_disconnect;
	conn->handle_msg = handle_msg;
	conn->is_client = false;

	mgmt_msg_init(&conn->mstate, max_read, max_write, max_size, idtag);

	/* start reading */
	msg_conn_sched_read(conn);

	/* Make socket non-blocking.  */
	set_nonblocking(conn->fd);
	setsockopt_so_sendbuf(conn->fd, MSG_CONN_SEND_BUF_SIZE);
	setsockopt_so_recvbuf(conn->fd, MSG_CONN_RECV_BUF_SIZE);
}

/*
 * 分配并初始化一条服务端连接（XMALLOC MTYPE_MSG_CONN 后调用 msg_conn_accept_init），
 * 记录 user 私有数据后返回。参数含义同 msg_conn_accept_init。
 */
struct msg_conn *
msg_server_conn_create(struct event_loop *tm, int fd,
		       int (*notify_disconnect)(struct msg_conn *conn),
		       void (*handle_msg)(uint8_t version, uint8_t *data,
					  size_t len, struct msg_conn *conn),
		       size_t max_read, size_t max_write, size_t max_size,
		       void *user, const char *idtag)
{
	struct msg_conn *conn = XMALLOC(MTYPE_MSG_CONN, sizeof(*conn));
	memset(conn, 0, sizeof(*conn));
	msg_conn_accept_init(conn, tm, fd, notify_disconnect, handle_msg,
			     max_read, max_write, max_size, idtag);
	conn->user = user;
	return conn;
}

/*
 * 删除一条服务端连接：先清理（msg_conn_cleanup）再释放内存。入参为 NULL 时
 * 直接返回。
 */
void msg_server_conn_delete(struct msg_conn *conn)
{
	if (!conn)
		return;
	msg_conn_cleanup(conn);
	XFREE(MTYPE_MSG_CONN, conn);
}
