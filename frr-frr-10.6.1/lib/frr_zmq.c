// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libzebra ZeroMQ bindings
 * Copyright (C) 2015  David Lamparter
 */

/*
 * IF YOU MODIFY THIS FILE PLEASE RUN `make check` and ensure that
 * the test_zmq.c unit test is still working.  There are dependencies
 * between the two that are extremely fragile.  My understanding
 * is that there is specialized ownership of the cb pointer based
 * upon what is happening.  Those assumptions are supposed to be
 * tested in the test_zmq.c
 */
/*
 * ============================================================================
 * frr_zmq.c —— libfrrzmq：ZeroMQ 与 FRR 事件主循环的桥接实现
 * ============================================================================
 * 职责：把 libzmq socket 暴露的底层 fd 注册到 libfrr 的事件主循环，并用
 *   持久化的读 / 写事件回调把 ZMQ 消息分发给上层；同时处理 ZMQ 的边沿
 *   触发语义（一轮内可能积压多条消息）。接口声明见 frr_zmq.h。
 *
 * 核心数据结构 / 全局变量：
 *   struct frrzmq_cb   每个 ZMQ socket 的绑定容器（含 read / write 两个
 *                      cb_core），定义在 frr_zmq.h
 *   frrzmq_context     全局 libzmq context
 *   frrzmq_initcount   引用计数，控制 context 的创建 / 销毁
 *
 * 关键函数：
 *   frrzmq_init / frrzmq_finish         context 生命周期（引用计数）
 *   frrzmq_read_msg / frrzmq_write_msg  事件主循环回调，循环排空 ZMQ 事件
 *   _frrzmq_event_add_read / write      注册读 / 写事件
 *   frrzmq_thread_cancel                取消绑定
 *   frrzmq_check_events                 边沿触发下复查 ZMQ_EVENTS 并重排事件
 *
 * 与其他模块关系：
 *   frrevent.h 提供 event / event_add_read 等；memory.h 提供 MTYPE 内存管理；
 *   本文件与 test_zmq.c 有极脆弱的耦合（见文件头英文说明），改实现须跑
 *   make check。
 *
 * 阅读顺序：
 *   frrzmq_init / finish -> frrzmq_read_msg -> _frrzmq_event_add_read ->
 *   frrzmq_write_msg -> _frrzmq_event_add_write -> frrzmq_thread_cancel ->
 *   frrzmq_check_events。
 *
 * 易踩的坑：
 *   1) cb 指针所有权在“回调内 / 外”不同：回调内取消不释放内存，由回调
 *      返回前依 cancelled 分支判断；
 *   2) 事件回调是持久的，回调末尾会重新 event_add_read / write；
 *   3) ZMQ 为边沿触发，读写后必须调用 frrzmq_check_events 复查。
 * ============================================================================
 */

#include <zebra.h>
#include <zmq.h>

#include "frrevent.h"
#include "memory.h"
#include "frr_zmq.h"
#include "log.h"
#include "lib_errors.h"

// 为本编译单元设置 xref 交叉引用支持（与其它 libfrr 文件一致）。
XREF_SETUP();

// DEFINE_MTYPE_STATIC(LIB, ZEROMQ_CB) —— 分配 struct frrzmq_cb 用的私有内存类型。
DEFINE_MTYPE_STATIC(LIB, ZEROMQ_CB, "ZeroMQ callback");

/* libzmq's context */
// frrzmq_context —— 全局 libzmq context，初值 NULL，由 frrzmq_init 创建。
void *frrzmq_context = NULL;
// frrzmq_initcount —— context 的引用计数：0 到 1 时创建，回到 0 时销毁。
static unsigned frrzmq_initcount = 0;

// frrzmq_init —— 初始化 libzmq context（引用计数 +1）：首次调用时
// zmq_ctx_new() 创建 context 并开启 IPv6（ZMQ_IPV6）。
void frrzmq_init(void)
{
	if (frrzmq_initcount++ == 0) {
		frrzmq_context = zmq_ctx_new();
		zmq_ctx_set(frrzmq_context, ZMQ_IPV6, 1);
	}
}

// frrzmq_finish —— 反初始化（引用计数 -1）：减到 0 时 zmq_ctx_term() 销毁
// context 并把它置回 NULL。
void frrzmq_finish(void)
{
	if (--frrzmq_initcount == 0) {
		zmq_ctx_term(frrzmq_context);
		frrzmq_context = NULL;
	}
}

// frrzmq_read_msg —— 读事件主循环回调（event 的参数即 cb 指针的地址 cbp）：
//   循环调用 zmq_poll 排空当前所有可读消息：
//   - 若注册的是 cb_msg，则每轮调用一次（单条消息语义）；
//   - 否则用 ZMQ_NOBLOCK 逐 part 接收，对每个 part 调 cb_part，
//     再用 ZMQ_RCVMORE 判断是否还有后续 part；
//   每处理一轮后若已取消，则做收尾（复查写事件、必要时释放 cb）并返回；
//   函数末尾重新 event_add_read，保持回调持久。
// 出错走 out_err：打日志并调用 cb_error。
static void frrzmq_read_msg(struct event *t)
{
	struct frrzmq_cb **cbp = EVENT_ARG(t);
	struct frrzmq_cb *cb;
	zmq_msg_t msg;
	unsigned partno;
	unsigned char read = 0;
	int ret, more;
	size_t moresz;

	if (!cbp)
		return;
	cb = (*cbp);
	if (!cb || !cb->zmqsock)
		return;

	while (1) {
		zmq_pollitem_t polli = {.socket = cb->zmqsock,
					.events = ZMQ_POLLIN};
		ret = zmq_poll(&polli, 1, 0);

		if (ret < 0)
			goto out_err;

		if (!(polli.revents & ZMQ_POLLIN))
			break;

		if (cb->read.cb_msg) {
			cb->in_cb = true;
			cb->read.cb_msg(cb->read.arg, cb->zmqsock);
			cb->in_cb = false;

			read = 1;

			if (cb->read.cancelled) {
				frrzmq_check_events(cbp, &cb->write,
						    ZMQ_POLLOUT);
				cb->read.event = NULL;
				if (cb->write.cancelled && !cb->write.event)
					XFREE(MTYPE_ZEROMQ_CB, *cbp);

				return;
			}
			continue;
		}

		partno = 0;
		if (zmq_msg_init(&msg))
			goto out_err;
		do {
			ret = zmq_msg_recv(&msg, cb->zmqsock, ZMQ_NOBLOCK);
			if (ret < 0) {
				if (errno == EAGAIN)
					break;

				zmq_msg_close(&msg);
				goto out_err;
			}
			read = 1;

			cb->in_cb = true;
			cb->read.cb_part(cb->read.arg, cb->zmqsock, &msg,
					 partno);
			cb->in_cb = false;

			if (cb->read.cancelled) {
				zmq_msg_close(&msg);
				frrzmq_check_events(cbp, &cb->write,
						    ZMQ_POLLOUT);
				cb->read.event = NULL;
				if (cb->write.cancelled && !cb->write.event)
					XFREE(MTYPE_ZEROMQ_CB, *cbp);

				return;
			}

			/* cb_part may have read additional parts of the
			 * message; don't use zmq_msg_more here */
			moresz = sizeof(more);
			more = 0;
			ret = zmq_getsockopt(cb->zmqsock, ZMQ_RCVMORE, &more,
					     &moresz);
			if (ret < 0) {
				zmq_msg_close(&msg);
				goto out_err;
			}

			partno++;
		} while (more);
		zmq_msg_close(&msg);
	}

	if (read)
		frrzmq_check_events(cbp, &cb->write, ZMQ_POLLOUT);

	event_add_read(t->master, frrzmq_read_msg, cbp, cb->fd, &cb->read.event);
	return;

out_err:
	flog_err(EC_LIB_ZMQ, "ZeroMQ read error: %s(%d)", strerror(errno),
		 errno);
	if (cb->read.cb_error)
		cb->read.cb_error(cb->read.arg, cb->zmqsock);
}

// _frrzmq_event_add_read —— 注册读事件：
//   校验参数（cbp 非空；msgfunc 与 partfunc 二选一）；
//   取 ZMQ_FD 与 ZMQ_EVENTS；若 cbp 尚未分配则新建 frrzmq_cb
//   （并把 write 方向标记为 cancelled）；
//   填好 read 核心各字段后：若已有可读事件则用 event_add_event 立即排入，
//   否则用 event_add_read 等待。返回 0 成功，-1 失败。
int _frrzmq_event_add_read(const struct xref_eventsched *xref,
			   struct event_loop *master,
			   void (*msgfunc)(void *arg, void *zmqsock),
			   void (*partfunc)(void *arg, void *zmqsock,
					    zmq_msg_t *msg, unsigned partnum),
			   void (*errfunc)(void *arg, void *zmqsock), void *arg,
			   void *zmqsock, struct frrzmq_cb **cbp)
{
	int fd, events;
	size_t len;
	struct frrzmq_cb *cb;

	if (!cbp)
		return -1;
	if (!(msgfunc || partfunc) || (msgfunc && partfunc))
		return -1;
	len = sizeof(fd);
	if (zmq_getsockopt(zmqsock, ZMQ_FD, &fd, &len))
		return -1;
	len = sizeof(events);
	if (zmq_getsockopt(zmqsock, ZMQ_EVENTS, &events, &len))
		return -1;

	if (*cbp)
		cb = *cbp;
	else {
		cb = XCALLOC(MTYPE_ZEROMQ_CB, sizeof(struct frrzmq_cb));
		cb->write.cancelled = true;
		*cbp = cb;
	}

	cb->zmqsock = zmqsock;
	cb->fd = fd;
	cb->read.arg = arg;
	cb->read.cb_msg = msgfunc;
	cb->read.cb_part = partfunc;
	cb->read.cb_error = errfunc;
	cb->read.cancelled = false;
	cb->in_cb = false;

	if (events & ZMQ_POLLIN) {
		event_cancel(&cb->read.event);

		event_add_event(master, frrzmq_read_msg, cbp, fd, &cb->read.event);
	} else
		event_add_read(master, frrzmq_read_msg, cbp, fd, &cb->read.event);
	return 0;
}

// frrzmq_write_msg —— 写事件主循环回调：与读回调对称，
//   循环 zmq_poll 排空 ZMQ_POLLOUT，每轮调用 cb_msg 写一条消息；
//   已取消时复查读事件并收尾（必要时释放 cb）；末尾重新 event_add_write。
// 出错走 out_err：打日志并调用 cb_error。
static void frrzmq_write_msg(struct event *t)
{
	struct frrzmq_cb **cbp = EVENT_ARG(t);
	struct frrzmq_cb *cb;
	unsigned char written = 0;
	int ret;

	if (!cbp)
		return;
	cb = (*cbp);
	if (!cb || !cb->zmqsock)
		return;

	while (1) {
		zmq_pollitem_t polli = {.socket = cb->zmqsock,
					.events = ZMQ_POLLOUT};
		ret = zmq_poll(&polli, 1, 0);

		if (ret < 0)
			goto out_err;

		if (!(polli.revents & ZMQ_POLLOUT))
			break;

		if (cb->write.cb_msg) {
			cb->in_cb = true;
			cb->write.cb_msg(cb->write.arg, cb->zmqsock);
			cb->in_cb = false;

			written = 1;

			if (cb->write.cancelled) {
				frrzmq_check_events(cbp, &cb->read, ZMQ_POLLIN);
				cb->write.event = NULL;
				if (cb->read.cancelled && !cb->read.event)
					XFREE(MTYPE_ZEROMQ_CB, *cbp);

				return;
			}
			continue;
		}
	}

	if (written)
		frrzmq_check_events(cbp, &cb->read, ZMQ_POLLIN);

	event_add_write(t->master, frrzmq_write_msg, cbp, cb->fd, &cb->write.event);
	return;

out_err:
	flog_err(EC_LIB_ZMQ, "ZeroMQ write error: %s(%d)", strerror(errno),
		 errno);
	if (cb->write.cb_error)
		cb->write.cb_error(cb->write.arg, cb->zmqsock);
}

// _frrzmq_event_add_write —— 注册写事件：
//   校验参数（cbp 非空；msgfunc 非空）；取 ZMQ_FD 与 ZMQ_EVENTS；
//   必要时新建 frrzmq_cb（把 read 方向标记为 cancelled）；
//   填好 write 核心各字段后：若已可写则用 _event_add_event 立即排入，
//   否则用 event_add_write 等待。返回 0 成功，-1 失败。
int _frrzmq_event_add_write(const struct xref_eventsched *xref,
			    struct event_loop *master,
			    void (*msgfunc)(void *arg, void *zmqsock),
			    void (*errfunc)(void *arg, void *zmqsock),
			    void *arg, void *zmqsock, struct frrzmq_cb **cbp)
{
	int fd, events;
	size_t len;
	struct frrzmq_cb *cb;

	if (!cbp)
		return -1;
	if (!msgfunc)
		return -1;
	len = sizeof(fd);
	if (zmq_getsockopt(zmqsock, ZMQ_FD, &fd, &len))
		return -1;
	len = sizeof(events);
	if (zmq_getsockopt(zmqsock, ZMQ_EVENTS, &events, &len))
		return -1;

	if (*cbp)
		cb = *cbp;
	else {
		cb = XCALLOC(MTYPE_ZEROMQ_CB, sizeof(struct frrzmq_cb));
		cb->read.cancelled = true;
		*cbp = cb;
	}

	cb->zmqsock = zmqsock;
	cb->fd = fd;
	cb->write.arg = arg;
	cb->write.cb_msg = msgfunc;
	cb->write.cb_part = NULL;
	cb->write.cb_error = errfunc;
	cb->write.cancelled = false;
	cb->in_cb = false;

	if (events & ZMQ_POLLOUT) {
		event_cancel(&cb->write.event);

		_event_add_event(xref, master, frrzmq_write_msg, cbp, fd, &cb->write.event);
	} else
		event_add_write(master, frrzmq_write_msg, cbp, fd, &cb->write.event);
	return 0;
}

// frrzmq_thread_cancel —— 取消 cb 上的一个事件核心：
//   置 core->cancelled = true 并 event_cancel(&core->event)；
//   若正处于回调中（in_cb）则直接返回，不在此释放内存；
//   否则当读 / 写均已取消且无挂起 event 时，释放整个 frrzmq_cb。
void frrzmq_thread_cancel(struct frrzmq_cb **cb, struct cb_core *core)
{
	if (!cb || !*cb)
		return;
	core->cancelled = true;
	event_cancel(&core->event);

	/* If cancelled from within a callback, don't try to free memory
	 * in this path.
	 */
	if ((*cb)->in_cb)
		return;

	/* Ok to free the callback context if no more ... context. */
	if ((*cb)->read.cancelled && !(*cb)->read.event && (*cb)->write.cancelled &&
	    ((*cb)->write.event == NULL))
		XFREE(MTYPE_ZEROMQ_CB, *cb);
}

// frrzmq_check_events —— 边沿触发语义下的补漏：在 send / recv 之后调用，
//   复查 ZMQ_EVENTS；若目标 event（ZMQ_POLLIN 或 ZMQ_POLLOUT）已就绪、
//   core->event 存在且未取消，则取消原 event 并按方向重新排入
//   frrzmq_read_msg 或 frrzmq_write_msg。
void frrzmq_check_events(struct frrzmq_cb **cbp, struct cb_core *core,
			 int event)
{
	struct frrzmq_cb *cb;
	int events;
	size_t len;

	if (!cbp)
		return;
	cb = (*cbp);
	if (!cb || !cb->zmqsock)
		return;

	len = sizeof(events);
	if (zmq_getsockopt(cb->zmqsock, ZMQ_EVENTS, &events, &len))
		return;
	if ((events & event) && core->event && !core->cancelled) {
		struct event_loop *tm = core->event->master;

		event_cancel(&core->event);

		if (event == ZMQ_POLLIN)
			event_add_event(tm, frrzmq_read_msg, cbp, cb->fd, &core->event);
		else
			event_add_event(tm, frrzmq_write_msg, cbp, cb->fd, &core->event);
	}
}
