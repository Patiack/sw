// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP I/O.
 * Implements packet I/O in a pthread.
 * Copyright (C) 2017  Cumulus Networks
 * Quentin Young
 */
/* ============================================================================
 * 【文件导读】bgp_io.h —— BGP 套接字读写的对外接口
 * ============================================================================
 * 【解决什么问题】bgpd 主线程除了跑状态机、算路由，还要收发 BGP 报文。若把
 * socket 读写直接放在主线程，一个慢速或拥塞的对端（写不出去、发不完）就会把整个
 * 进程卡住，进而拖垮所有邻居。本文件的接口就是为这层“解耦”服务：把每个邻居连接
 * (peer_connection) 的读、写各自挂到一个独立的 I/O 线程上（实现见 bgp_io.c），
 * 主线程只负责把要发的报文塞进缓冲区、把收到并整包切好的报文取走处理。
 *
 * 【四个开关接口的语义】本模块核心是四个成对使用的函数：
 *   bgp_writes_on  / bgp_writes_off : 打开 / 关闭“写”这一侧。
 *   bgp_reads_on   / bgp_reads_off  : 打开 / 关闭“读”这一侧。
 * 关键约定：一旦 writes_on / reads_on 被调用，**对 connection->fd 的直接 socket
 * 操作就变得不安全**——因为此时 I/O 线程也会并发操作它；必须先用对应的 *_off
 * 关掉，才可安全地在本线程直接操作该 fd。这是本模块最容易踩的坑。
 * 打开写之后：放到 connection->obuf 上的报文会被 I/O 线程尽力写出去。
 * 打开读之后：I/O 线程把读到的数据按整包切好，投到 connection->ibuf 队列，
 *             并在主线程上触发 bgp_packet.c 的 bgp_process_packet() 去消费。
 *
 * 【线程模型入口】bgp_io_start / bgp_io_stop 是 I/O 线程自身的启动与收尾钩子，
 * 由 frr_pthread 框架在创建/销毁线程时调用，不是给普通业务代码调用的。
 *
 * 【三个配额宏】用来限制“单轮 I/O 处理多少报文”，避免一次事件里长时间独占
 * I/O 线程或主线程，从而保证其它连接与定时任务能及时得到调度。
 *
 * 【阅读建议】先看四个 on/off 的语义（尤其“何时不可直接碰 fd”），再读 bgp_io.c 的
 * bgp_process_reads / bgp_process_writes 看线程究竟怎么跑，最后回看
 * bgp_io_start / bgp_io_stop。
 * ============================================================================
 */

#ifndef _FRR_BGP_IO_H
#define _FRR_BGP_IO_H

/* 【写配额】单次可写事件里最多一次写出多少个报文（即 writev 最多聚集的报文数）。
 * 目的是让 I/O 线程尽快返回，不被某个连接长期独占。 */
#define BGP_WRITE_PACKET_MAX 64U
/* 【读配额】单次可读事件里最多从 socket 读出多少个报文再交给主线程。 */
#define BGP_READ_PACKET_MAX  10U
/* 【主线程处理配额】主线程一次 bgp_process_packet 事件里最多处理多少条已入队报文，
 * 避免长时间霸占主线程，保证其它定时任务及时调度。 */
#define BGP_PACKET_PROCESS_LIMIT 100

#include "bgpd/bgpd.h"
#include "frr_pthread.h"

/* 前向声明：连接结构体定义在 bgpd.h，这里只需要它的指针类型，避免循环包含。 */
struct peer_connection;

/* 【函数】bgp_io_start —— I/O 线程的入口（线程体）：设计上应由 frr_pthread 框架在
 * 创建 I/O 线程时调用，arg 未使用；内部跑事件循环处理各连接的读写事件，直到线程
 * 被请求停止，返回线程退出码（通常为 NULL）。
 * 疑点：本函数在 FRR 10.6.1 源码里已无定义，且 bgpd.c 创建 I/O 线程时用的是
 * frr_pthread_attr_default.start，故此处仅为历史遗留声明，实际并未被使用。 */
/**
 * Start function for write thread.
 *
 * @param arg - unused
 */
extern void *bgp_io_start(void *arg);

/* 【函数】bgp_io_stop —— I/O 线程的收尾：设计上用于释放资源并结束线程；
 * result 回传线程结果（此处未用），fpt 为目标 I/O 线程。
 * 疑点：与 bgp_io_start 同理，本函数在本版源码中已无定义，属历史遗留声明。 */
/**
 * Start function for write thread.
 *
 * Uninitializes all resources and stops the thread.
 *
 * @param result - where to store data result, unused
 */
extern int bgp_io_stop(void **result, struct frr_pthread *fpt);

/* 【函数】bgp_writes_on —— 为某连接打开“写”。调用后放到 connection->obuf 的报文
 * 会被 I/O 线程写向 connection->fd，直到缓冲区清空；**此后在本线程直接操作该 fd
 * 就不再安全**（I/O 线程会并发操作它）。 */
/**
 * Turns on packet writing for a peer.
 *
 * After this function is called, any packets placed on connection->obuf will be
 * written to connection->fd until no more packets remain.
 *
 * Additionally, it becomes unsafe to perform socket actions on connection->fd.
 *
 * @param peer - peer to register
 */
extern void bgp_writes_on(struct peer_connection *peer);

/* 【函数】bgp_writes_off —— 关闭“写”。返回后 obuf 不再被 I/O 线程写出，且再次允许
 * 在本线程安全地直接操作 connection->fd。实现里会把待发队列清空。 */
/**
 * Turns off packet writing for a peer.
 *
 * After this function returns, packets placed on connection->obuf will not be
 * written to connection->fd by the I/O thread.
 *
 * After this function returns it becomes safe to perform socket actions on
 * connection->fd.
 *
 * @param connection - connection to deregister
 * @param flush - as described
 */
extern void bgp_writes_off(struct peer_connection *connection);

/* 【函数】bgp_reads_on —— 为某连接打开“读”。调用后 I/O 线程把 connection->fd 上
 * 收到的数据按整包读入 connection->ibuf 队列；**此后在本线程直接操作该 fd 不再
 * 安全**。每当有一个及以上报文入队，就会在主线程投递一个事件，处理函数是
 * bgp_packet.c 的 bgp_process_packet()。 */
/**
 * Turns on packet reading for a peer.
 *
 * After this function is called, any packets received on connection->fd
 * will be read and copied into the FIFO queue connection->ibuf.
 *
 * Additionally, it becomes unsafe to perform socket actions on connection->fd.
 *
 * Whenever one or more packets are placed onto connection->ibuf, a task of type
 * THREAD_EVENT will be placed on the main thread whose handler is
 *
 *   bgp_packet.c:bgp_process_packet()
 *
 * @param connection - The connection to register
 */
extern void bgp_reads_on(struct peer_connection *connection);

/* 【函数】bgp_reads_off —— 关闭“读”。返回后 I/O 线程不再从该 fd 读取，本线程可
 * 再次安全地直接操作 connection->fd。 */
/**
 * Turns off packet reading for a peer.
 *
 * After this function is called, any packets received on connection->fd
 * will not be read by the I/O thread.
 *
 * After this function returns it becomes safe to perform socket actions on
 * connection->fd.
 *
 * @param connection - The connection to register for
 */
extern void bgp_reads_off(struct peer_connection *connection);

#endif /* _FRR_BGP_IO_H */
