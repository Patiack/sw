// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2024  David Lamparter, for NetDEF, Inc.
 */

#ifndef _FRR_ZLOG_RECIRCULATE_H
#define _FRR_ZLOG_RECIRCULATE_H

/* ============================================================================
 * 【文件导读】zlog_recirculate.h —— 日志回灌的对外入口
 * ============================================================================
 * 【只有一个声明，但语义要看清】zlog_recirculate_subscribe() 把“日志中继 fd”
 * 挂到事件循环上，此后该 fd 上到达的字节流会被当成日志文本，补上终止符后送进
 * zlog。详细机制见 zlog_recirculate.c 的文件导读。
 *
 * 【参数名的小提示】原注释写 "fd should be one end of a socketpair()"：
 * 本端用来读，对端由发送方(子进程)写。这决定了它**不是一个普通文件描述符**，
 * 而是面向字节流的 IPC 通道——因此.c 里才需要处理 EAGAIN/EWOULDBLOCK。
 * 另注：声明里的形参名是 tm 而非 el(与 .c 中的定义名不一致)，只是命名遗留，
 * 不影响语义(形参名对调用方不可见)。
 * ============================================================================
 */
/* fd should be one end of a socketpair() */
extern void zlog_recirculate_subscribe(struct event_loop *tm, int fd);

#endif
