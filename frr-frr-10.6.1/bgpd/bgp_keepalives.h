// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Keepalives.
 * Implements a producer thread to generate BGP keepalives for peers.
 * Copyright (C) 2017 Cumulus Networks, Inc.
 * Quentin Young
 */

/* ============================================================================
 * 【文件导读】bgp_keepalives.h —— BGP keepalive 生产者线程的对外接口
 * ============================================================================
 * 【解决什么问题】BGP 每个已建立(Established)的邻居都要按 Hold Time 的 1/3 周期
 * 发送 KEEPALIVE 报文保活。最朴素的做法是“每个邻居在主线程里挂一个定时器”，但
 * 邻居成千上万时会产生海量定时器、且主线程被频繁唤醒。FRR 改为：**单独开一个
 * keepalive 生产者线程**，用一个哈希表(peerhash)集中管理所有需要发 keepalive 的
 * 邻居，循环里按各自的到期时间统一调度——这就是本文件(.c)与它所声明的接口。
 *
 * 【设计要点】调用者只关心“把一个邻居加入 / 移出 keepalive 集合”这两件事：
 *   bgp_keepalives_on  : 邻居进入 Established 时调用，把它登记进来。
 *   bgp_keepalives_off : 邻居离开 Established / 断开时调用，把它摘除。
 * 线程的启动 / 停止由 frr_pthread 框架托管(见 bgp_main.c 里 bgp_pth_ka 的创建)。
 *
 * 【为什么对外接口收得这么窄】发 keepalive 的时机与频度由线程内部按
 * peer->v_keepalive 统一决定，外部只增删成员，避免了两处各自计时带来的竞态。
 * 线程内部用互斥锁 + 条件变量保护 peerhash，所以这些函数可被主线程安全调用。
 *
 * 【阅读建议】先看本头文件 4 个函数各自语义，再进 bgp_keepalives.c 看
 * peer_process(单邻居是否到期) 与 bgp_keepalives_start(线程主循环) 的配合。
 * ============================================================================
 */

#ifndef _FRR_BGP_KEEPALIVES_H
#define _FRR_BGP_KEEPALIVES_H

#include "frr_pthread.h"
#include "bgpd.h"

/* 【登记】把一个邻居(连接)加入 keepalive 集合：若尚未登记则新建 pkat 条目并按
 * peer->v_keepalive 计时；已登记则无操作。会顺带唤醒线程，令其重算下次睡眠时长。
 * 线程安全(内部持互斥锁)。注意：调用后不要再改 peer->v_keepalive，否则行为未定义。 */
/**
 * Turns on keepalives for a peer.
 *
 * This function adds the peer to an internal list of peers to generate
 * keepalives for.
 *
 * At set intervals, a BGP KEEPALIVE packet is generated and placed on
 * peer->obuf. This operation is thread-safe with respect to peer->obuf.
 *
 * peer->v_keepalive determines the interval. Changing this value before
 * unregistering this peer with bgp_keepalives_off() results in undefined
 * behavior.
 *
 * If the peer is already registered for keepalives via this function, nothing
 * happens.
 */
extern void bgp_keepalives_on(struct peer_connection *connection);

/* 【注销】把邻居移出 keepalive 集合并释放其 pkat、解除对 peer 的引用；未登记则
 * 无操作。线程安全；会清掉内部标志位，使邻居之后可被重新登记。 */
/**
 * Turns off keepalives for a peer.
 *
 * Removes the peer from the internal list of peers to generate keepalives for.
 *
 * If the peer is already unregistered for keepalives, nothing happens.
 */
extern void bgp_keepalives_off(struct peer_connection *connection);

/* 【初始化】keepalive 线程的“启动前准备”：初始化同步原语(互斥锁 / 条件变量)。
 * 必须在任何 on / off 之前调用，否则会触碰未初始化的锁。 */
/**
 * Pre-run initialization function for keepalives pthread.
 *
 * Initializes synchronization primitives. This should be called before
 * anything else to avoid race conditions.
 */
extern void bgp_keepalives_init(void);

/* 【线程入口】keepalive 生产者线程的主体：循环遍历内部邻居列表，按各自定时器
 * 到期情况生成并发送 keepalive；当没有到期的邻居时，用条件变量阻塞到“下一个
 * 最近到期时刻”。返回 NULL。实现见 bgp_keepalives.c 的同名函数。 */
/**
 * Entry function for keepalives pthread.
 *
 * This function loops over an internal list of peers, generating keepalives at
 * regular intervals as determined by each peer's keepalive timer.
 *
 * See bgp_keepalives_on() for additional details.
 *
 * @param arg pthread arg, not used
 */
extern void *bgp_keepalives_start(void *arg);

/* 【停止】请求线程退出并阻塞等待其结束(join)：先置 fpt->running=false 并唤醒条件
 * 变量，使线程从阻塞中返回、跳出主循环，再 join。返回 0。 */
/**
 * Stops the thread and blocks until it terminates.
 */
int bgp_keepalives_stop(struct frr_pthread *fpt, void **result);

#endif /* _FRR_BGP_KEEPALIVES_H */
