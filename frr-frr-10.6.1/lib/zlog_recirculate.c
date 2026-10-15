// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2024  David Lamparter, for NetDEF, Inc.
 */

#include "zebra.h"

/* ============================================================================
 * 【文件导读】zlog_recirculate.c —— 把子进程的日志“回灌”进父进程日志系统
 * ============================================================================
 * 【要解决的问题】像 ldpd 这样的守护进程会 fork 出子进程(lde/ldpe)分担工作。
 * 子进程的日志如果直接写同一个日志后端，容易出现交错、丢失、以及“谁的日志”
 * 分不清。FRR 的做法是：子进程把日志消息通过 **socketpair** 发给父进程，父进程
 * 在本文件里把它们像普通日志一样重新投递进 zlog——这个动作就叫 recirculate
 * (回灌/再循环)。
 *
 * 【本文件只负责“事件循环那一半”】原注释说明得很清楚：这是刻意拆分的边界——
 *   - 本文件：**事件驱动侧**，负责从 fd 读字节流、切分/补终止符；
 *   - zlog_recirculate_live_msg()(在 zlog 内部)：**日志系统侧**，负责把
 *     一行完整消息真正送进日志处理链。
 * 因此看代码时别在本文件里找 zlog 的格式化逻辑，它只做搬运。
 *
 * 【当前使用范围】按原注释，目前只在 ldpd 的**父进程**里启用，用于接收 lde/ldpe
 * 子进程的日志，其它地方尚未使用。读代码时不必假设它是个通用基础设施。
 *
 * 【协议约定(读懂 receive 的关键)】对端发来的数据**不带**结尾的 "\n\0"；
 * 本文件收到后自己补上这两个字节再交给 zlog。所以读缓冲的可用大小是
 * sizeof(rxbuf) - 2——这 2 个字节是**预留给终止符**的，不是随便减的。
 *
 * 【阅读顺序】先读 event 回调 zlog_recirculate_recv(短且是核心) → 再看
 * subscribe(只是把回调挂到事件循环上，一行)。
 * ============================================================================
 */
#include "log.h"
#include "frrevent.h"

#include "zlog_recirculate.h"

/* This is only the event loop part;  it's split off from
 * zlog_recirculate_live_msg since there's an integration boundary;  this
 * half deals with events, the other half with zlog interna.
 *
 * As of writing, this runs in ldpd in the *parent* process and receives log
 * messages from the lde/ldpe subprocesses.  It is not used anywhere else
 * (yet?)
 */
/* 【事件回调】日志中继 socket 可读时被调用：读一批字节并转交 zlog。
 *   ev: 事件对象；用 EVENT_FD(ev) 取出对应的 fd。args 为 NULL(注册时未传)。
 *   返回: 无。返回值不重要，重要的是**正确重新注册读事件**。
 * 【为什么要重新 event_add_read】FRR 的事件注册默认是**一次性**的：回调触发后
 * 该读事件即被注销。想让下一次可读继续回调，必须在回调里重新注册。本函数把
 * 重注册放在“检查完出错情况之后、真正处理数据之前”，位置是关键——
 *   ① n_rd == 0(EOF)：对端关闭，close 后 return，**不重注册**；
 *   ② 真出错(非 EAGAIN/EWOULDBLOCK)：记日志、close、return，**不重注册**；
 *   ③ 暂时无数据(EAGAIN/EWOULDBLOCK)：属于正常，**重注册**后 return；
 *   ④ 有数据：重注册，然后把数据交给 zlog 处理。
 *   两处注释 "event_add_read not called yet, nothing to cancel" 就是在强调：
 *   这两条 return 路径上事件已自动注销，无需(也不能)再取消。
 * 【易踩的坑】① 缓冲用 `sizeof(rxbuf) - 2` 读取，是为了给下面补的 '\n' 与 '\0'
 *   留位置；若改成 sizeof(rxbuf) 且正好读满，就会**栈缓冲区溢出**。
 *   ② errno 必须判 EAGAIN 与 EWOULDBLOCK 两者——虽然 Linux 上数值相同，但
 *   显式都判是跨平台惯例，删掉一个在某些系统上会误判为致命错误。
 *   ③ zlog_recirculate_live_msg 收到的是**显示长度 n_rd**(不含添加的终止符)，
 *   不是缓冲区总长；传 sizeof(rxbuf) 会把尾部垃圾一起打出来。 */
static void zlog_recirculate_recv(struct event *ev)
{
	uint8_t rxbuf[4096];
	ssize_t n_rd;
	int fd = EVENT_FD(ev);

	/* see below for -2, "\n\0" are added */
	n_rd = read(fd, rxbuf, sizeof(rxbuf) - 2);
	if (n_rd == 0) {
		/* EOF */
		close(fd);
		/* event_add_read not called yet, nothing to cancel */
		return;
	}
	if (n_rd < 0 && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
		/* error */
		zlog_warn("error on log relay socket %d: %m", fd);
		close(fd);
		/* event_add_read not called yet, nothing to cancel */
		return;
	}

	event_add_read(ev->master, zlog_recirculate_recv, NULL, fd, NULL);
	if (n_rd < 0)
		return;

	/* log infrastructure has an implicit \n\0 at the end */
	rxbuf[n_rd] = '\n';
	rxbuf[n_rd + 1] = '\0';
	zlog_recirculate_live_msg(rxbuf, n_rd);
}

/* 【函数】把日志中继 fd 挂到事件循环上，开始接收子进程日志。
 *   el: 目标事件循环(master)。
 *   fd: 中继 socket 的**本端**，通常是 socketpair() 的一端(见 .h 的说明)。
 * 【要点】只是把 zlog_recirculate_recv 注册成可读回调并立即返回，不阻塞、不读
 *   数据；真正的读取由事件循环驱动。args 传 NULL，所以回调里拿不到额外上下文
 *   ——这也暗示“一个 fd 一路日志”的用法，状态都藏在 fd 里。
 * 【易踩的坑】按当前实现，同一 fd 只能订阅一次；重复调用会注册两个读事件，
 *   导致同一条数据被两个回调争抢(其中一个会读到 EAGAIN)。调用方需自行保证
 *   一次性订阅。 */
void zlog_recirculate_subscribe(struct event_loop *el, int fd)
{
	event_add_read(el, zlog_recirculate_recv, NULL, fd, NULL);
}
