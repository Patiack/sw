// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP I/O.
 * Implements packet I/O in a pthread.
 * Copyright (C) 2017  Cumulus Networks
 * Quentin Young
 */
/* ============================================================================
 * 【文件导读】bgp_io.c —— 把 BGP 套接字读写放进独立的 I/O 线程
 * ============================================================================
 * 【解决什么问题】BGP 报文要经 TCP 收发。若在主线程里直接 read / write，一旦对端
 * 变慢（发送窗口打满、迟迟不发），write 就会阻塞、read 也拿不到数据，整个 bgpd 被
 * 一个邻居拖死，其它邻居全部受牵连。FRR 因此把“读写套接字”这件事从主线程剥离：
 * 每条 peer 连接(peer_connection) 的读、写都挂到全局唯一的 I/O 线程(bgp_pth_io) 上，
 * 主线程只做状态机与路由计算。于是某连接写不动时，只是它自己的报文堆在队列里，
 * 不会阻塞别人。
 *
 * 【线程分工（先建立这个心智模型再看代码）】
 *   I/O 线程（本文件）:
 *     读侧：fd 可读时把字节读进环形缓冲 ibuf_work，再从缓冲区里按 BGP 报文头切出
 *           一个个完整报文，push 到 connection->ibuf（供主线程消费），并在主线程上
 *           唤醒 bgp_packet.c:bgp_process_packet 去处理。
 *     写侧：fd 可写时从 connection->obuf（待发 FIFO）取报文，用 writev 一次尽量多
 *           地写出（聚集 I/O）；写不完就等下次可写事件重试。
 *   主线程:
 *     把要发的报文 push 到 connection->obuf；通过 bgp_process_packet 消费
 *     connection->ibuf。
 *
 * 【状态机与“部分读 / 部分写”】
 *   写：bgp_write() 用 writev 把多个报文的 iovec 一次提交；若只写出一部分
 *       （num != writenum），就前移已写完整的报文、对最后那个“写了一半”的报文用
 *       stream_forward_getp 后移读指针留下，等下次可写事件继续；EAGAIN 等归为
 *       暂时性错误静默重试；致命错误则发 FSM 事件拆会话。bgp_process_writes 中：
 *       obuf 里若还有报文就“预登记”下一个可写事件，否则取消并（非致命时）启动
 *       更新组发送定时器。
 *   读：bgp_read() 把字节读入环形缓冲 ibuf_work；read_ibuf_work() 检查缓冲区是否
 *       已凑齐“至少一个完整报文”，是则切出该报文入队，否则返回 0 等下次可读事件。
 *
 * 【线程间如何安全共享（本文件并发安全的三个要点）】
 *   1) connection->io_mtx（互斥锁）：读写线程凡是访问 obuf / ibuf_work / ibuf
 *      的地方都用 frr_with_mutex 包住，防止与主线程并发改动这两个队列。
 *   2) 上报错误用跨线程投递：bgp_read / bgp_write 发现致命错误时不在 I/O 线程里
 *      直接改状态机，而是 bgp_enqueue_conn_err（读侧）或 BGP_EVENT_ADD（写侧）把
 *      事件丢给主线程，由主线程处理状态迁移。
 *   3) 唤醒主线程用 event_add_event：读到报文后往主线程 master 上投递
 *      bgp_process_packet 事件来消费，而非跨线程直接调用。
 *   另注意 struct ringbuf（ibuf_work）用于容纳“已读入但尚未凑齐一个整包”的零头。
 *
 * 【建议阅读顺序】bgp_writes_on/off、bgp_reads_on/off（谁在开关什么）
 *   → bgp_process_writes → bgp_write（写状态机）
 *   → bgp_process_reads → bgp_read → read_ibuf_work → validate_header（读状态机）。
 * ============================================================================
 */

/* clang-format off */
#include <zebra.h>
#include <pthread.h>		// for pthread_mutex_unlock, pthread_mutex_lock
#include <sys/uio.h>		// for writev

#include "frr_pthread.h"
#include "linklist.h"		// for list_delete, list_delete_all_node, lis...
#include "log.h"		// for zlog_debug, safe_strerror, zlog_err
#include "memory.h"		// for MTYPE_TMP, XCALLOC, XFREE
#include "network.h"		// for ERRNO_IO_RETRY
#include "stream.h"		// for stream_get_endp, stream_getw_from, str...
#include "ringbuf.h"		// for ringbuf_remain, ringbuf_peek, ringbuf_...
#include "frrevent.h"		// for event, EVENT_ARG, thread...

#include "bgpd/bgp_io.h"
#include "bgpd/bgp_debug.h"	// for bgp_debug_neighbor_events, bgp_type_str
#include "bgpd/bgp_errors.h"	// for expanded error reference information
#include "bgpd/bgp_fsm.h"	// for BGP_EVENT_ADD, bgp_event
#include "bgpd/bgp_packet.h"	// for bgp_notify_io_invalid...
#include "bgpd/bgp_trace.h"	// for frrtraces
#include "bgpd/bgpd.h"		// for peer, BGP_MARKER_SIZE, bgp_master, bm
/* clang-format on */

/* 【前向声明】本文件内部的几个静态函数：先声明后定义，便于相互调用时不依赖书写顺序。 */
/* forward declarations */
static uint16_t bgp_write(struct peer_connection *connection);
static uint16_t bgp_read(struct peer_connection *connection, int *code_p);
static void bgp_process_writes(struct event *event);
static void bgp_process_reads(struct event *event);
static bool validate_header(struct peer_connection *connection);

/* 【I/O 状态位】bgp_read / bgp_write 的返回值是一个位标志集合，上层据其决定重试、
 * 拆会话还是暂停：
 *   BGP_IO_TRANS_ERR    : 暂时性错误（EAGAIN / EWOULDBLOCK 等），非故障，稍后重试。
 *   BGP_IO_FATAL_ERR    : 致命 TCP 错误（连接异常 / 收到 EOF 等），需要拆除会话。
 *   BGP_IO_WORK_FULL_ERR: 读侧工作缓冲 ibuf_work 已满，暂时无空间可读。
 * 三者用位或组合，故用 CHECK_FLAG 单独判定。 */
/* generic i/o status codes */
#define BGP_IO_TRANS_ERR (1 << 0) /* EAGAIN or similar occurred */
#define BGP_IO_FATAL_ERR (1 << 1) /* some kind of fatal TCP error */
#define BGP_IO_WORK_FULL_ERR (1 << 2) /* No room in work buffer */

/* Thread external API ----------------------------------------------------- */
/* ========== 对外 API：打开 / 关闭某连接的读、写 ========== */

/* 【函数】bgp_writes_on —— 为某连接打开“写”。
 * 先做一批 assert 检查前置条件（连接未删除、读写缓冲均在、无残留的建连检查定时器、
 * fd 有效），随即在 I/O 线程的事件循环上登记一个“fd 可写”事件，处理函数是
 * bgp_process_writes；最后置上 PEER_THREAD_WRITES_ON 标志。
 * 调用后本线程就**不应再直接操作 connection->fd**（改由 I/O 线程负责）。 */
void bgp_writes_on(struct peer_connection *connection)
{
	struct frr_pthread *fpt = bgp_pth_io;

	/* 必须确保 I/O 线程已在运行，否则登记事件没有意义 */
	assert(fpt->running);

	/* 前置条件：连接有效、obuf（待发）/ ibuf（待收）缓冲及 ibuf_work 工作缓冲
	 * 都已分配、没有遗留的建连检查定时器、fd 有效 */
	assert(connection->status != Deleted);
	assert(connection->obuf);
	assert(connection->ibuf);
	assert(connection->ibuf_work);
	assert(!connection->t_connect_check_r);
	assert(!connection->t_connect_check_w);
	assert(connection->fd);

	/* 在 I/O 线程的 master 上登记“可写”事件，事件句柄存入 connection->t_write */
	event_add_write(fpt->master, bgp_process_writes, connection,
			connection->fd, &connection->t_write);

	/* 置位，标记该连接的“写”已交由 I/O 线程负责 */
	SET_FLAG(connection->thread_flags, PEER_THREAD_WRITES_ON);
}

/* 【函数】bgp_writes_off —— 关闭某连接的“写”，与 bgp_writes_on 相对。
 * 步骤：清掉 PEER_THREAD_WRITES_ON 标志 → 把 obuf 待发队列里积压的报文全部释放
 * （这些报文不再发出去）→ 异步取消可写事件 t_write → 取消更新组发送定时器。
 * 返回后，本线程可再次安全地直接操作 connection->fd。 */
void bgp_writes_off(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;
	struct frr_pthread *fpt = bgp_pth_io;
	struct stream *s;

	assert(fpt->running);

	/* 清除“写已开启”标志（peer->connection 即传入的同一个连接） */
	UNSET_FLAG(peer->connection->thread_flags, PEER_THREAD_WRITES_ON);

	/* 清空待发 FIFO：把尚未发出的报文逐个弹出并释放内存。
	 * 用 io_mtx 保护，避免与 I/O 线程并发访问 obuf。 */
	/* Clear out the write fifo */
	frr_with_mutex (&connection->io_mtx) {
		if (connection->obuf != NULL) {
			while ((s = stream_fifo_pop(connection->obuf)) != NULL)
				stream_free(s);
		}
	}

	/* 异步取消可写事件（可能需要 I/O 线程侧生效）；再取消更新组发送定时器 */
	event_cancel_async(fpt->master, &connection->t_write, NULL);
	event_cancel(&connection->t_generate_updgrp_packets);
}

/* 【函数】bgp_reads_on —— 为某连接打开“读”，与 bgp_writes_on 对称。
 * 先做前置断言，再在 I/O 线程事件循环上登记“fd 可读”事件（处理函数
 * bgp_process_reads），最后置 PEER_THREAD_READS_ON 标志。
 * 调用后本线程**不应再直接操作 connection->fd**。 */
void bgp_reads_on(struct peer_connection *connection)
{
	struct frr_pthread *fpt = bgp_pth_io;
	assert(fpt->running);

	/* 与“写”类似的前置条件断言（顺序略有不同，但检查项一致） */
	assert(connection->status != Deleted);
	assert(connection->ibuf);
	assert(connection->fd);
	assert(connection->ibuf_work);
	assert(connection->obuf);
	assert(!connection->t_connect_check_r);
	assert(!connection->t_connect_check_w);
	assert(connection->fd);

	/* 登记“可读”事件，事件句柄存入 connection->t_read */
	event_add_read(fpt->master, bgp_process_reads, connection,
		       connection->fd, &connection->t_read);

	/* 置位，标记该连接的“读”已交由 I/O 线程负责 */
	SET_FLAG(connection->thread_flags, PEER_THREAD_READS_ON);
}

/* 【函数】bgp_reads_off —— 关闭某连接的“读”，与 bgp_reads_on 相对。
 * 步骤：异步取消可读事件 t_read → 若该连接还在“有新报文待处理”的全局连接队列
 * bm->connection_fifo 里，则把它摘出 → 清除 PEER_THREAD_READS_ON 标志。
 * 返回后本线程可再次安全地直接操作 connection->fd。 */
void bgp_reads_off(struct peer_connection *connection)
{
	struct frr_pthread *fpt = bgp_pth_io;
	assert(fpt->running);

	/* 异步取消可读事件 */
	event_cancel_async(fpt->master, &connection->t_read, NULL);

	/* 从主线程的“待处理连接”队列中摘除本连接，防止 bgp_process_packet 再去读它。
	 * 用 bm->peer_connection_mtx 保护该全局队列。 */
	frr_with_mutex (&bm->peer_connection_mtx) {
		if (peer_connection_fifo_member(&bm->connection_fifo, connection))
			peer_connection_fifo_del(&bm->connection_fifo, connection);
	}

	/* 清除“读已开启”标志 */
	UNSET_FLAG(connection->thread_flags, PEER_THREAD_READS_ON);
}

/* Thread internal functions ----------------------------------------------- */

/* ========== 线程内部函数（只在 I/O 线程里运行） ========== */

/*
 * Called from I/O pthread when a file descriptor has become ready for writing.
 */
/* 【函数】bgp_process_writes —— 写侧事件处理：fd 可写时由 I/O 线程回调。
 * 流程：
 *   1) fd 无效则直接返回；
 *   2) **预登记**下一个可写事件（先假定后面还有数据要写，随后若判断无需再写就取消，
 *      以免“本次写完”到“下次登记”之间出现空档而漏掉可写时机）；
 *   3) 加 io_mtx 调 bgp_write 实际发送，并按 obuf 是否还有残留决定 reschedule；
 *   4) 依状态位：暂时性错误无需处理（是否重试交给 reschedule）；致命错误则不再重试
 *      （reschedule=false, fatal=true）；
 *   5) 若无需重试，取消可写事件；非致命时再启动更新组发送定时器，把积压的路由更新
 *      聚合成批后统一发送（suppress-fib-pending 场景借此聚合更多路由）。 */
static void bgp_process_writes(struct event *event)
{
	static struct peer *peer;
	struct peer_connection *connection = EVENT_ARG(event);
	uint16_t status;
	bool reschedule = false;
	bool fatal = false;
	struct frr_pthread *fpt = bgp_pth_io;

	peer = connection->peer;

	/* fd 已失效（连接正在拆除），本事件无事可做 */
	if (connection->fd < 0)
		return;

	/* 预登记下一次可写事件：先假设后面还有数据要写，若最终判定无需再写会在下方
	 * event_cancel 掉。如此可避免两次写之间的空档丢失可写时机。 */
	/* Anticipate rescheduling */
	event_add_write(fpt->master, bgp_process_writes, connection, connection->fd,
			&connection->t_write);

	frr_with_mutex (&connection->io_mtx) {
		status = bgp_write(connection);
		reschedule = (stream_fifo_head(connection->obuf) != NULL);
	}

	/* 暂时性错误（EAGAIN 等）：无需特殊处理，是否重试由 reschedule 决定 */
	/* no problem */
	if (CHECK_FLAG(status, BGP_IO_TRANS_ERR)) {
	}

	/* 致命错误：停止重试并标记 fatal，交由后续流程拆除 */
	/* problem */
	if (CHECK_FLAG(status, BGP_IO_FATAL_ERR)) {
		reschedule = false;
		fatal = true;
	}

	/* 若无需再写：取消预登记的可写事件；非致命错误时启动更新组发送定时器，
	 * 让积压的路由更新聚集后统一生成再发送。 */
	/* If suppress fib pending is enabled, route is advertised to peers when
	 * the status is received from the FIB. The delay is added
	 * to update group packet generate which will allow more routes to be
	 * sent in the update message
	 */
	if (!reschedule) {
		event_cancel(&connection->t_write);

		if (!fatal)
			BGP_UPDATE_GROUP_TIMER_ON(&connection->t_generate_updgrp_packets,
						  bgp_generate_updgrp_packets);
	}
}

/* 【函数】read_ibuf_work —— 从工作缓冲 ibuf_work 里切出**一个完整 BGP 报文**，
 * 入队到 connection->ibuf，供主线程处理。
 * 返回：> 0 为成功切出的报文字节数；0 表示数据尚不足以凑齐一个整包，需等下次可读
 * 事件；负值为错误码（-ENOMEM 待处理队列已满，-EBADMSG 报文头非法）。
 * 步骤：
 *   1) 若 ibuf->count 已达上限 bm->inq_limit，返回 -ENOMEM（主线程消费不过来）；
 *   2) 数据不足一个报文头（BGP_HEADER_SIZE）则返回 0 等更多数据；
 *   3) validate_header 校验标记 / 类型 / 长度，非法则发 NOTIFY 并返回 -EBADMSG；
 *   4) 从头部读出报文长度 pktsize（网络序转主机序）并做上限检查；
 *   5) 缓冲区仍不足 pktsize 则返回 0；
 *   6) 分配 pktsize 大小的 stream，从环形缓冲拷出整包，设好写端点后 push 到 ibuf。
 * 注意：本函数只负责“切成整包入队”，真正的报文解析在主线程的 bgp_process_packet。 */
static int read_ibuf_work(struct peer_connection *connection)
{
	/* static buffer for transferring packets */
	/* shorter alias to peer's input buffer */
	/* ibw：peer 输入工作缓冲（环形缓冲）的短别名 */
	struct ringbuf *ibw = connection->ibuf_work;
	/* packet size as given by header */
	/* pktsize：报文头里标明的报文总长度 */
	uint16_t pktsize = 0;
	struct stream *pkt;

	/* 若待处理队列 ibuf 的报文数已达上限 bm->inq_limit，说明主线程来不及消费，
	 * 返回 -ENOMEM 让上层暂停读取，防止内存无限增长 */
	/* ============================================== */
	frr_with_mutex (&connection->io_mtx) {
		if (connection->ibuf->count >= bm->inq_limit)
			return -ENOMEM;
	}

	/* 数据还不足一个 BGP 报文头，返回 0 等下次可读事件再拼 */
	/* check that we have enough data for a header */
	if (ringbuf_remain(ibw) < BGP_HEADER_SIZE)
		return 0;

	/* 校验报文头（标记 / 类型 / 长度），非法时函数内部已发 NOTIFY */
	/* check that header is valid */
	if (!validate_header(connection))
		return -EBADMSG;

	/* 头部合法，从偏移 BGP_MARKER_SIZE 处读出长度字段（含 2 字节长度 + 1 字节类型） */
	/* header is valid; retrieve packet size */
	ringbuf_peek(ibw, BGP_MARKER_SIZE, &pktsize, sizeof(pktsize));

	/* 长度字段是网络字节序，转成主机序 */
	pktsize = ntohs(pktsize);

	/* 长度超过该 peer 允许的最大报文，属严重异常，判为非法报文 */
	/* if this fails we are seriously screwed */
	if (pktsize > connection->peer->max_packet_size)
		return -EBADMSG;

	/* 若缓冲区已凑齐 pktsize 字节，就把整包拷进独立 stream 并入队；否则返回 0
	 * 等更多数据。 */
	/*
	 * If we have that much data, chuck it into its own
	 * stream and append to input queue for processing.
	 *
	 * Otherwise, come back later.
	 */
	if (ringbuf_remain(ibw) < pktsize)
		return 0;

	/* 分配一个恰好 pktsize 大小的 stream 来承载整包 */
	pkt = stream_new(pktsize);
	assert(STREAM_WRITEABLE(pkt) == pktsize);
	/* 从环形缓冲取出 pktsize 字节填入该 stream，并把写端点设到 pktsize */
	assert(ringbuf_get(ibw, pkt->data, pktsize) == pktsize);
	stream_set_endp(pkt, pktsize);

	/* 打点到 frr trace：启用 BGP trace 时能观察到收包事件 */
	frrtrace(2, frr_bgp, packet_read, connection, pkt);
	/* 入队到 ibuf：加 io_mtx 保护，避免与主线程并发访问该 FIFO */
	frr_with_mutex (&connection->io_mtx) {
		stream_fifo_push(connection->ibuf, pkt);
	}

	/* 返回本次切出的报文字节数（正值），上层据此继续尝试切下一个包 */
	return pktsize;
}

/*
 * Called from I/O pthread when a file descriptor has become ready for reading,
 * or has hung up.
 *
 * We read as much data as possible, process as many packets as we can and
 * place them on peer->connection.ibuf for secondary processing by the main
 * thread.
 */
/* 【函数】bgp_process_reads —— 读侧事件处理：fd 可读或对端挂断时由 I/O 线程回调。
 * 策略：一次尽量多读、尽量多切包，把切好的整包放到 connection->ibuf，交给主线程做
 * 二次处理（bgp_process_packet）。
 * 流程：
 *   1) 全局正在退出或 fd 失效则直接返回；
 *   2) 加 io_mtx 调 bgp_read 把字节读进工作缓冲；
 *   3) 暂时性错误：本轮不切包，跳到 done（稍后再读）；
 *   4) 致命错误：置 fatal，并把错误 + FSM 码经 bgp_enqueue_conn_err 投给主线程处理，
 *      跳 done；
 *   5) 循环调 read_ibuf_work 尽量切出整包，直到再无完整包（返回值 <= 0）；
 *   6) 按最后一次返回值收尾：-EBADMSG 判为致命；-ENOMEM（队列满）仅在首次出现时打
 *      一条调试日志（用 ibuf_full_logged 去重）；其余情况清除该去重标志；
 *   7) done 处：fatal 则清空工作缓冲并返回（等主线程拆会话）；否则重新登记可读事件，
 *      且本轮若新增过报文，就把本连接加入 bm->connection_fifo 并唤醒主线程的
 *      bgp_process_packet 事件去消费。
 * 注意：这里刻意不在 I/O 线程里直接跑状态机——错误一律投递给主线程。 */
static void bgp_process_reads(struct event *event)
{
	/* clang-format off */
	struct peer_connection *connection = EVENT_ARG(event);
	static struct peer *peer;       /* peer to read from */
	uint16_t status;                /* bgp_read status code */
	bool fatal = false;             /* whether fatal error occurred */
	bool added_pkt = false;         /* whether we pushed onto ->connection.ibuf */
	int code = 0;                   /* FSM code if error occurred */
	static bool ibuf_full_logged;   /* Have we logged full already */
	int ret = 1;
	/* clang-format on */

	peer = connection->peer;

	/* 进程正在退出，或 fd 已失效，本事件无需处理 */
	if (bm->terminating || connection->fd < 0)
		return;

	struct frr_pthread *fpt = bgp_pth_io;

	/* 加锁调用 bgp_read，从 socket 读字节进工作缓冲；code 用于回传错误对应的 FSM 事件码 */
	frr_with_mutex (&connection->io_mtx) {
		status = bgp_read(connection, &code);
	}

	/* error checking phase */
	/* 暂时性错误（EAGAIN 等）：本轮不切包，直接跳到 done 挂下一个可读事件 */
	if (CHECK_FLAG(status, BGP_IO_TRANS_ERR)) {
		/* no problem; just don't process packets */
		goto done;
	}

	/* 致命错误：标记 fatal；并把错误连同 bgp_read 给出的具体状态迁移码交给主线程处理 */
	if (CHECK_FLAG(status, BGP_IO_FATAL_ERR)) {
		/* problem; tear down session */
		fatal = true;

		/* Handle the error in the main pthread, include the
		 * specific state change from 'bgp_read'.
		 */
		bgp_enqueue_conn_err(peer->bgp, connection, code);
		goto done;
	}

	/* 循环尽量切包：read_ibuf_work 返回 > 0 表示又切出一个整包，继续；<= 0 则退出，
	 * 其返回码留给下面 switch 分情况处理 */
	while (true) {
		ret = read_ibuf_work(connection);
		if (ret <= 0)
			break;

		added_pkt = true;
	}

	/* 按最后一次返回值收尾 */
	switch (ret) {
	case -EBADMSG:
		/* 报文头非法：判为致命错误（validate_header 内已发出 NOTIFY） */
		fatal = true;
		break;
	case -ENOMEM:
		/* 待处理队列满：只打一次日志以免刷屏；非致命，稍后再读 */
		if (!ibuf_full_logged) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"%s [Event] Peer Input-Queue is full: limit (%u)",
					peer->host, bm->inq_limit);

			ibuf_full_logged = true;
		}
		break;
	default:
		/* 0：数据尚不足一个整包，正常等待；清掉“已记录队列满”标志以便下次可再记录 */
		ibuf_full_logged = false;
		break;
	}

done:
	/* handle invalid header */
	/* 致命错误：清空工作缓冲（防御性，避免残留脏数据），直接返回等主线程拆会话 */
	if (fatal) {
		/* wipe buffer just in case someone screwed up */
		ringbuf_wipe(connection->ibuf_work);
		return;
	}

	/* 重新登记可读事件，保证后续仍能被唤醒 */
	event_add_read(fpt->master, bgp_process_reads, connection,
		       connection->fd, &connection->t_read);
	/* 若本轮往 ibuf 里放过报文，则唤醒主线程来消费 */
	if (added_pkt) {
		/* 先把本连接加入主线程的“待处理连接”队列（查重后追加到尾部） */
		frr_with_mutex (&bm->peer_connection_mtx) {
			if (!peer_connection_fifo_member(&bm->connection_fifo, connection))
				peer_connection_fifo_add_tail(&bm->connection_fifo, connection);
		}
		/* 往主线程 master 投递 bgp_process_packet 事件去处理 ibuf */
		event_add_event(bm->master, bgp_process_packet, NULL, 0, &bm->e_process_packet);
	}
}

/*
 * Flush peer output buffer.
 *
 * This function pops packets off of peer->connection.obuf and writes them to
 * peer->connection.fd. The amount of packets written is equal to the minimum of
 * peer->wpkt_quanta and the number of packets on the output buffer, unless an
 * error occurs.
 *
 * If write() returns an error, the appropriate FSM event is generated.
 *
 * The return value is equal to the number of packets written
 * (which may be zero).
 */
/* 【函数】bgp_write —— 把 obuf 待发 FIFO 里的报文尽量写向 socket（写状态机核心）。
 * 一次最多提交 wpkt_quanta 个报文，用 writev 聚集发送，以摊薄系统调用开销；若一次
 * 没写完（部分写），就对未发完的报文前移读指针后留下，等下次可写事件继续。
 * 关键点：
 *   - wpkt_quanta 用 atomic_load 读进局部变量，保证本次循环内它不变；
 *   - ostreams / iov 都是变长数组（VLA，长度由 wpkt_quanta 决定），需编译器支持；
 *   - 写完后逐条统计各报文类型（open / update / notify / keepalive / refresh /
 *     capability）的出向计数（原子自增），并更新 last_update / last_write 时间戳；
 *   - 若本次发出过 NOTIFY：把 v_start 翻倍（退避），并把 FSM 事件 BGP_Stop 投给主
 *     线程（Graceful Restart 下可能迁到 Connect 而非 Idle），随后结束。
 * 返回：状态位集合（见文件顶部的 BGP_IO_*）。 */
static uint16_t bgp_write(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;
	uint8_t type;
	struct stream *s;
	int update_last_write = 0;
	unsigned int count;
	uint32_t uo = 0;
	uint16_t status = 0;
	uint32_t wpkt_quanta_old;

	int writenum = 0;
	int num;
	unsigned int iovsz;
	unsigned int strmsz;
	unsigned int total_written;
	time_t now;

	/* 读取本次允许一次写出的报文数 wpkt_quanta（原子读，保证循环内取值稳定） */
	wpkt_quanta_old = atomic_load_explicit(&peer->bgp->wpkt_quanta,
					       memory_order_relaxed);
	/* ostreams 记录本次参与发送的报文指针；iov 是与之对应的 writev 向量表。
	 * 二者都是长度为 wpkt_quanta_old 的变长数组（VLA）。 */
	struct stream *ostreams[wpkt_quanta_old];
	struct stream **streams = ostreams;
	struct iovec iov[wpkt_quanta_old];

	/* 取待发队列第一个报文；为空说明没东西可写，直接去 done 更新时间戳 */
	s = stream_fifo_head(connection->obuf);

	if (!s)
		goto done;

	/* 收集最多 wpkt_quanta 个报文，逐个填入 iovec；writenum 累计待写总字节数 */
	count = iovsz = 0;
	while (count < wpkt_quanta_old && iovsz < array_size(iov) && s) {
		ostreams[iovsz] = s;
		iov[iovsz].iov_base = stream_pnt(s);
		iov[iovsz].iov_len = STREAM_READABLE(s);
		writenum += STREAM_READABLE(s);
		s = s->next;
		++iovsz;
		++count;
	}

	/* strmsz 记录本次收集到的报文数（供“全部写完”时设置 total_written） */
	strmsz = iovsz;
	total_written = 0;

	do {
		/* 一次提交全部 iovec；返回实际写出的字节数，失败返回 -1 */
		num = writev(connection->fd, iov, iovsz);

		if (num < 0) {
			/* 失败：区分可重试（EAGAIN 等）与致命错误 */
			if (!ERRNO_IO_RETRY(errno)) {
				/* 致命：投递 FSM 事件并置致命标志 */
				BGP_EVENT_ADD(connection, TCP_fatal_error);
				SET_FLAG(status, BGP_IO_FATAL_ERR);
			} else {
				/* 暂时性：置可重试标志，稍后再写 */
				SET_FLAG(status, BGP_IO_TRANS_ERR);
			}

			break;
		} else if (num != writenum) {
			/* 部分写：只写出去 num 字节，需处理已写完整的报文并保留写了一半的那个 */
			unsigned int msg_written = 0;
			unsigned int ic = iovsz;

			/* 从头数：哪些 iovec 已整包写完（其长度不超过已写字节数）；遇到
			 * “写了一半”的那个就停下，它不计数、留给下轮 */
			for (unsigned int i = 0; i < ic; i++) {
				size_t ss = iov[i].iov_len;

				if (ss > (unsigned int) num)
					break;

				msg_written++;
				iovsz--;
				writenum -= ss;
				num -= ss;
			}

			total_written += msg_written;

			assert(total_written < count);

			/* 丢弃已写完整的前 msg_written 个 iovec，把剩下（含写了一半的首包）
			 * 前移到数组头部；并对首元素前移读指针，反映该包已写出的部分 */
			memmove(&iov, &iov[msg_written],
				sizeof(iov[0]) * iovsz);
			streams = &streams[msg_written];
			stream_forward_getp(streams[0], num);
			iov[0].iov_base = stream_pnt(streams[0]);
			iov[0].iov_len = STREAM_READABLE(streams[0]);

			/* 剩余待写字节数扣掉刚写出的部分；num 归零后条件 num != writenum 仍
			 * 成立，于是循环会再次 writev，把剩余报文（含那个半包）继续写出 */
			writenum -= num;
			num = 0;
			assert(writenum > 0);
		} else {
			/* 全部写出：本次收集的 strmsz 个报文都发完了 */
			total_written = strmsz;
		}

	} while (num != writenum);

	/* 统计阶段：把已成功写出的报文从 obuf 弹出，按类型累加出向计数并释放其内存 */
	/* Handle statistics */
	for (unsigned int i = 0; i < total_written; i++) {
		s = stream_fifo_pop(connection->obuf);

		assert(s == ostreams[i]);

		/* 定位报文头里的 type 字段（BGP marker 16 字节 + length 2 字节之后） */
		/* Retrieve BGP packet type. */
		stream_set_getp(s, BGP_MARKER_SIZE + 2);
		type = stream_getc(s);

		/* 按报文类型累加各出向计数器（原子自增） */
		switch (type) {
		case BGP_MSG_OPEN:
			atomic_fetch_add_explicit(&peer->open_out, 1,
						  memory_order_relaxed);
			break;
		case BGP_MSG_UPDATE:
			atomic_fetch_add_explicit(&peer->update_out, 1,
						  memory_order_relaxed);
			uo++;
			break;
		case BGP_MSG_NOTIFY:
			/* 发出 NOTIFY：累加计数，并做连接重试退避 */
			atomic_fetch_add_explicit(&peer->notify_out, 1,
						  memory_order_relaxed);
			/* start 定时器翻倍（退避），上限 120 秒 */
			/* Double start timer. */
			peer->v_start *= 2;

			/* Overflow check. */
			if (peer->v_start >= (60 * 2))
				peer->v_start = (60 * 2);

			/* 投递 BGP_Stop：正常迁到 Idle；Graceful Restart 场景可能迁到
			 * Connect（由主线程 FSM 决定），随后结束本次写 */
			/*
			 * Handle Graceful Restart case where the state changes
			 * to Connect instead of Idle.
			 */
			BGP_EVENT_ADD(connection, BGP_Stop);
			goto done;

		case BGP_MSG_KEEPALIVE:
			atomic_fetch_add_explicit(&peer->keepalive_out, 1,
						  memory_order_relaxed);
			break;
		case BGP_MSG_ROUTE_REFRESH_NEW:
		case BGP_MSG_ROUTE_REFRESH_OLD:
			atomic_fetch_add_explicit(&peer->refresh_out, 1,
						  memory_order_relaxed);
			break;
		case BGP_MSG_CAPABILITY:
			atomic_fetch_add_explicit(&peer->dynamic_cap_out, 1,
						  memory_order_relaxed);
			break;
		}

		stream_free(s);
		ostreams[i] = NULL;
		update_last_write = 1;
	}

done : {
	now = monotime(NULL);
	/*
	 * Update last_update if UPDATEs were written.
	 * Note: that these are only updated at end,
	 *       not per message (i.e., per loop)
	 */
	if (uo)
		atomic_store_explicit(&peer->last_update, now,
				      memory_order_relaxed);

	/* If we TXed any flavor of packet */
	if (update_last_write) {
		atomic_store_explicit(&peer->last_write, now,
				      memory_order_relaxed);
		peer->last_sendq_ok = now;
	}
}

	return status;
}

/* ibuf_scratch：bgp_read 从连接 fd 读取数据时使用的全局共享暂存缓冲区。
 * 大小为单个报文最大尺寸乘以一次最多可读的报文数，读到的数据先落在这里，
 * 再拷贝进 connection->ibuf_work 环形缓冲区。
 * 注意：它是全局单例、并非每个 peer 各一份；若将来改为每 peer 一个读线程，
 * 必须重新设计此结构，否则多个 peer 并发读写会互相踩踏。 */
uint8_t ibuf_scratch[BGP_EXTENDED_MESSAGE_MAX_PACKET_SIZE * BGP_READ_PACKET_MAX];
/* bgp_read：从 connection->fd 读取一段数据，写入 connection->ibuf_work 环形缓冲区。
 * 入口参数 connection 为本 peer 的连接对象，内部使用其 fd 与 ibuf_work。
 * code_p：发生致命错误时回写 FSM 事件码（如 TCP_fatal_error、
 *   TCP_connection_closed），为 NULL 则不回写；本函数只负责读取，
 *   具体事件处理交由主线程 FSM 完成。
 * 返回值：状态位掩码，可能包含 BGP_IO_WORK_FULL_ERR（work 缓冲区已满，
 *   读之前就直接返回）、BGP_IO_TRANS_ERR（EAGAIN / EWOULDBLOCK，
 *   暂无数据、稍后再读）、BGP_IO_FATAL_ERR（读出错或对端关闭，需拆链）。
 * 要点：先探测 ringbuf 剩余空间，为 0 不读；只读 min(剩余空间, 暂存区大小)
 *   个字节；读到的数据用 ringbuf_put 写入，并用 assert 保证整段写入成功。 */
/*
 * Reads a chunk of data from peer->connection.fd into
 * peer->connection.ibuf_work.
 *
 * code_p
 *    Pointer to location to store FSM event code in case of fatal error.
 *
 * @return status flag (see top-of-file)
 *
 * PLEASE NOTE:  If we ever transform the bgp_read to be a pthread
 * per peer then we need to rethink the global ibuf_scratch
 * data structure above.
 */
static uint16_t bgp_read(struct peer_connection *connection, int *code_p)
{
	size_t readsize; /* how many bytes we want to read */
	ssize_t nbytes;  /* how many bytes we actually read */
	size_t ibuf_work_space; /* space we can read into the work buf */
	uint16_t status = 0;

	ibuf_work_space = ringbuf_space(connection->ibuf_work);

	if (ibuf_work_space == 0) {
		SET_FLAG(status, BGP_IO_WORK_FULL_ERR);
		return status;
	}

	readsize = MIN(ibuf_work_space, sizeof(ibuf_scratch));

#ifdef __clang_analyzer__
	/* clang-SA doesn't want you to call read() while holding a mutex */
	(void)readsize;
	nbytes = 0;
#else
	nbytes = read(connection->fd, ibuf_scratch, readsize);
#endif

	/* EAGAIN or EWOULDBLOCK; come back later */
	if (nbytes < 0 && ERRNO_IO_RETRY(errno)) {
		SET_FLAG(status, BGP_IO_TRANS_ERR);
	} else if (nbytes < 0) {
		/* Fatal error; tear down session */
		flog_err(EC_BGP_UPDATE_RCV,
			 "%s [Error] bgp_read_packet error: %s",
			 connection->peer->host, safe_strerror(errno));

		/* Handle the error in the main pthread. */
		if (code_p)
			*code_p = TCP_fatal_error;

		SET_FLAG(status, BGP_IO_FATAL_ERR);

	} else if (nbytes == 0) {
		/* Received EOF / TCP session closed */
		if (bgp_debug_neighbor_events(connection->peer))
			zlog_debug("%s [Event] BGP connection closed fd %d",
				   connection->peer->host, connection->fd);

		/* Handle the error in the main pthread. */
		if (code_p)
			*code_p = TCP_connection_closed;

		SET_FLAG(status, BGP_IO_FATAL_ERR);
	} else {
		assert(ringbuf_put(connection->ibuf_work, ibuf_scratch,
				   nbytes) == (size_t)nbytes);
	}

	return status;
}

/* validate_header：读取到 BGP 报文头后，校验 marker、消息类型与报文长度。
 * 入口参数 connection 为本 peer 的连接对象，包头从它的 ibuf_work 环形缓冲区取。
 * 前提：假定 ibuf_work 中至少已有 BGP_HEADER_SIZE 个可读字节（由调用方保证）。
 * 校验项：16 字节 marker 必须全为 0xff，否则回送 NOT_SYNC 通知；
 *   类型必须属于已知 BGP 报文（OPEN / UPDATE / NOTIFY / KEEPALIVE /
 *   ROUTE_REFRESH_NEW / ROUTE_REFRESH_OLD / CAPABILITY），否则回送 BAD_MESTYPE；
 *   长度必须落在合法区间（不超过对端协商的 max_packet_size，并满足各类型最小值，
 *   其中 KEEPALIVE 必须恰好等于其最小长度），否则回送 BAD_MESLEN。
 * 返回值：true 表示包头合法；false 表示已发送对应 NOTIFY，调用方应终止本包处理。 */
/*
 * Called after we have read a BGP packet header. Validates marker, message
 * type and packet length. If any of these aren't correct, sends a notify.
 *
 * Assumes that there are at least BGP_HEADER_SIZE readable bytes in the input
 * buffer.
 */
static bool validate_header(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;
	uint16_t size;
	uint8_t type;
	struct ringbuf *pkt = connection->ibuf_work;

	static const uint8_t m_correct[BGP_MARKER_SIZE] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	uint8_t m_rx[BGP_MARKER_SIZE] = {0x00};

	if (ringbuf_peek(pkt, 0, m_rx, BGP_MARKER_SIZE) != BGP_MARKER_SIZE)
		return false;

	if (memcmp(m_correct, m_rx, BGP_MARKER_SIZE) != 0) {
		bgp_notify_io_invalid(peer, BGP_NOTIFY_HEADER_ERR,
				      BGP_NOTIFY_HEADER_NOT_SYNC, NULL, 0);
		return false;
	}

	/* Get size and type in network byte order. */
	ringbuf_peek(pkt, BGP_MARKER_SIZE, &size, sizeof(size));
	ringbuf_peek(pkt, BGP_MARKER_SIZE + 2, &type, sizeof(type));

	size = ntohs(size);

	/* BGP type check. */
	if (type != BGP_MSG_OPEN && type != BGP_MSG_UPDATE
	    && type != BGP_MSG_NOTIFY && type != BGP_MSG_KEEPALIVE
	    && type != BGP_MSG_ROUTE_REFRESH_NEW
	    && type != BGP_MSG_ROUTE_REFRESH_OLD
	    && type != BGP_MSG_CAPABILITY) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s unknown message type 0x%02x", peer->host,
				   type);

		bgp_notify_io_invalid(peer, BGP_NOTIFY_HEADER_ERR,
				      BGP_NOTIFY_HEADER_BAD_MESTYPE, &type, 1);
		return false;
	}

	/* Minimum packet length check. */
	if ((size < BGP_HEADER_SIZE) || (size > peer->max_packet_size)
	    || (type == BGP_MSG_OPEN && size < BGP_MSG_OPEN_MIN_SIZE)
	    || (type == BGP_MSG_UPDATE && size < BGP_MSG_UPDATE_MIN_SIZE)
	    || (type == BGP_MSG_NOTIFY && size < BGP_MSG_NOTIFY_MIN_SIZE)
	    || (type == BGP_MSG_KEEPALIVE && size != BGP_MSG_KEEPALIVE_MIN_SIZE)
	    || (type == BGP_MSG_ROUTE_REFRESH_NEW
		&& size < BGP_MSG_ROUTE_REFRESH_MIN_SIZE)
	    || (type == BGP_MSG_ROUTE_REFRESH_OLD
		&& size < BGP_MSG_ROUTE_REFRESH_MIN_SIZE)
	    || (type == BGP_MSG_CAPABILITY
		&& size < BGP_MSG_CAPABILITY_MIN_SIZE)) {
		if (bgp_debug_neighbor_events(peer)) {
			zlog_debug("%s bad message length - %d for %s",
				   peer->host, size,
				   type == 128 ? "ROUTE-REFRESH"
					       : bgp_type_str[(int)type]);
		}

		uint16_t nsize = htons(size);

		bgp_notify_io_invalid(peer, BGP_NOTIFY_HEADER_ERR,
				      BGP_NOTIFY_HEADER_BAD_MESLEN,
				      (unsigned char *)&nsize, 2);
		return false;
	}

	return true;
}
