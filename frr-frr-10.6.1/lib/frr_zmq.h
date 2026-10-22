// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libzebra ZeroMQ bindings
 * Copyright (C) 2015  David Lamparter
 */

/*
 * ============================================================================
 * frr_zmq.h —— libfrrzmq：FRR 到 ZeroMQ 消息总线的绑定接口
 * ============================================================================
 * 职责：把 libzmq 的 socket 暴露的底层文件描述符接入 libfrr 的事件主循环，
 *   使守护进程可用统一的事件回调处理 ZMQ 的读 / 写事件。它被单独编成一个
 *   库（libfrrzmq），从而让 libfrr 本身不必依赖 libzmq。
 *
 * 核心数据结构：
 *   struct cb_core   一次事件绑定的核心：event、arg、cb_msg / cb_part /
 *                    cb_error 回调，以及 cancelled 标志
 *   struct frrzmq_cb 一个 ZMQ socket 的读写绑定容器：持 zmqsock、fd、
 *                    in_cb 标志，内含 read / write 两个 cb_core
 *   frrzmq_context   全局 libzmq context（已开启 IPv6）
 *
 * 关键宏：
 *   _xref_zmq_a                内部宏：登记 xref 事件调度交叉引用并执行调用
 *   frrzmq_event_add_read_msg  注册“每读一条消息回调一次”的读事件
 *   frrzmq_event_add_read_part 注册“每个 multi-part 子部分回调一次”的读事件
 *   frrzmq_event_add_write_msg 注册写事件
 *
 * 与其他模块关系：
 *   frrevent.h 提供 event 主循环；frr_zmq.c 是唯一实现；
 *   test_zmq.c（自测）与实现存在极脆弱的耦合，改实现须跑 make check。
 *
 * 阅读顺序：
 *   cb_core -> frrzmq_cb -> 注册宏 -> _frrzmq_event_add_read / write ->
 *   frrzmq_thread_cancel -> frrzmq_check_events。
 *
 * 易踩的坑：
 *   1) ZMQ 为边沿触发，注册回调后要处理“同一轮里积压的多条消息”；
 *   2) 回调是持久的，不要在回调内部重复注册事件；
 *   3) 从回调内部取消回调是安全的，但不要在该路径释放 cb（见 .c 实现）。
 * ============================================================================
 */

#ifndef _FRRZMQ_H
#define _FRRZMQ_H

#include "frrevent.h"
#include <zmq.h>

#ifdef __cplusplus
extern "C" {
#endif

/* linking/packaging note:  this is a separate library that needs to be
 * linked into any daemon/library/module that wishes to use its
 * functionality.  The purpose of this is to encapsulate the libzmq
 * dependency and not make libfrr/FRR itself depend on libzmq.
 *
 * libfrrzmq should be put in LDFLAGS/LIBADD *before* either libfrr or
 * libzmq, and both of these should always be listed, e.g.
 *   foo_LDFLAGS = libfrrzmq.la libfrr.la $(ZEROMQ_LIBS)
 */

/* callback integration */
// struct cb_core —— 单次事件绑定（读或写）的核心数据：
//   event     绑定到主循环的 event 句柄
//   arg       回调透传参数
//   cancelled 是否已取消（用于延迟释放内存）
//   cb_msg    收到 / 发出完整消息时调用
//   cb_part   以 multi-part 子部分为单位回调（仅读事件使用）
//   cb_error  出错时调用
struct cb_core {
	struct event *event;
	void *arg;

	bool cancelled;

	void (*cb_msg)(void *arg, void *zmqsock);
	void (*cb_part)(void *arg, void *zmqsock, zmq_msg_t *msg,
			unsigned partnum);
	void (*cb_error)(void *arg, void *zmqsock);
};

// struct frrzmq_cb —— 一个 ZMQ socket 上读 / 写事件绑定的容器：
//   zmqsock 目标 ZMQ socket
//   fd      ZMQ 暴露给主循环的底层文件描述符
//   in_cb   当前是否正处于读 / 写回调中（决定取消时能否就地释放）
//   read    读方向的事件核心；write 为写方向的事件核心
struct frrzmq_cb {
	void *zmqsock;
	int fd;

	bool in_cb; /* This context is in a read or write callback. */

	struct cb_core read;
	struct cb_core write;
};

/* libzmq's context
 *
 * this is mostly here as a convenience, it has IPv6 enabled but nothing
 * else is tied to it;  you can use a separate context without problems
 */
// frrzmq_context —— 全局 libzmq context（frrzmq_init 中创建并开启 IPv6）；
// 仅为方便提供，也可改用独立 context。
extern void *frrzmq_context;

// frrzmq_init   —— 初始化：首次调用时创建 frrzmq_context（引用计数 +1）。
// frrzmq_finish —— 反初始化：引用计数减到 0 时销毁 context。
extern void frrzmq_init(void);
extern void frrzmq_finish(void);

// _xref_zmq_a —— 内部宏：在一个语句表达式里登记 XREFT_EVENTSCHED 类型的
// xref 交叉引用（记录函数名、目标与事件类型），随后执行 call 完成注册。
#define _xref_zmq_a(type, f, d, call)                                          \
	({                                                                     \
		static const struct xref_eventsched _xref __attribute__(       \
			(used)) = {                                            \
			.xref = XREF_INIT(XREFT_EVENTSCHED, NULL, __func__),   \
			.funcname = #f,                                        \
			.dest = #d,                                            \
			.event_type = EVENT_##type,                            \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		call;                                                          \
	}) /* end */

/* core event registration, one of these 2 macros should be used */
// 以下三个宏封装 _frrzmq_event_add_read / write：
//   _msg   版本：每条完整消息回调一次（f 为 msgfunc）
//   _part  版本：每个 multi-part 子部分回调一次（f 为 partfunc）
//   _write 版本：可写时回调一次
// 参数：m=master 事件循环，f=回调，e=错误回调，a=arg，z=zmqsock，d=目标描述。
#define frrzmq_event_add_read_msg(m, f, e, a, z, d)                            \
	_xref_zmq_a(READ, f, d,                                                \
		    _frrzmq_event_add_read(&_xref, m, f, NULL, e, a, z, d))

#define frrzmq_event_add_read_part(m, f, e, a, z, d)                           \
	_xref_zmq_a(READ, f, d,                                                \
		    _frrzmq_event_add_read(&_xref, m, NULL, f, e, a, z, d))

#define frrzmq_event_add_write_msg(m, f, e, a, z, d)                           \
	_xref_zmq_a(WRITE, f, d,                                               \
		    _frrzmq_event_add_write(&_xref, m, f, e, a, z, d))

// 前置声明，供下方函数原型使用。
struct cb_core;
struct frrzmq_cb;

/* Set up a POLLIN or POLLOUT notification to be called from the libfrr main
 * loop. This has the following properties:
 *
 * - since ZeroMQ works with edge triggered notifications, it will loop and
 *   dispatch as many events as ZeroMQ has pending at the time libfrr calls
 *   into this code
 * - due to this looping (which means it non-single-issue), the callback is
 *   also persistent.  Do _NOT_ re-register the event inside of your
 *   callback function.
 * - either msgfunc or partfunc will be called (only one can be specified)
 *   - msgfunc is called once for each incoming message
 *   - if partfunc is specified, the message is read and partfunc is called
 *     for each ZeroMQ multi-part subpart.  Note that you can't send replies
 *     before all parts have been read because that violates the ZeroMQ FSM.
 * - write version doesn't allow for partial callback, you must handle the
 *   whole message (all parts) in msgfunc callback
 * - you can safely cancel the callback from within itself
 * - installing a callback will check for pending events (ZMQ_EVENTS) and
 *   may schedule the event to run as soon as libfrr is back in its main
 *   loop.
 */
// _frrzmq_event_add_read —— 在 master 事件循环上为目标 ZMQ socket 注册读事件：
//   msgfunc 与 partfunc 二选一（都非空或都为空返回 -1）；
//   errfunc 可选，出错时回调；arg 透传；cb 为该 socket 的绑定指针。
// 返回 0 成功，-1 失败。
extern int
_frrzmq_event_add_read(const struct xref_eventsched *xref,
		       struct event_loop *master,
		       void (*msgfunc)(void *arg, void *zmqsock),
		       void (*partfunc)(void *arg, void *zmqsock,
					zmq_msg_t *msg, unsigned partnum),
		       void (*errfunc)(void *arg, void *zmqsock), void *arg,
		       void *zmqsock, struct frrzmq_cb **cb);
// _frrzmq_event_add_write —— 注册写事件：可写时回调 msgfunc；
// 不支持 part 回调，必须在一次 msgfunc 中写完整条消息。返回 0 / -1。
extern int _frrzmq_event_add_write(const struct xref_eventsched *xref,
				   struct event_loop *master,
				   void (*msgfunc)(void *arg, void *zmqsock),
				   void (*errfunc)(void *arg, void *zmqsock),
				   void *arg, void *zmqsock,
				   struct frrzmq_cb **cb);

// frrzmq_thread_cancel —— 取消 cb 上的某个事件核心（读或写）：置 cancelled
// 并 cancel event；若当前不在回调中且读 / 写都已取消则释放 cb。
// 允许从回调内部调用（此时不释放内存，见 .c 实现）。
extern void frrzmq_thread_cancel(struct frrzmq_cb **cb, struct cb_core *core);

/*
 * http://api.zeromq.org/4-2:zmq-getsockopt#toc10
 *
 * As the descriptor is edge triggered, applications must update the state of
 * ZMQ_EVENTS after each invocation of zmq_send or zmq_recv.To be more explicit:
 * after calling zmq_send the socket may become readable (and vice versa)
 * without triggering a read event on the file descriptor.
 */
// frrzmq_check_events —— 因 ZMQ 为边沿触发，在 send / recv 后需复查 ZMQ_EVENTS：
// 若指定 event 已就绪、core->event 存在且未取消，则取消并重新排入事件循环。
extern void frrzmq_check_events(struct frrzmq_cb **cbp, struct cb_core *core,
				int event);

#ifdef __cplusplus
}
#endif

#endif /* _FRRZMQ_H */
