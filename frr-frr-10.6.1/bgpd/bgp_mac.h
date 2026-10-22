// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGPd - Mac hash header
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *               Donald Sharp
 */
/* ============================================================================
 * 【文件导读】bgp_mac.h —— EVPN 场景下的 MAC 表（本机学到的 MAC 的汇总视图）
 * ============================================================================
 * 【为什么 EVPN 需要一份独立的 MAC 表】EVPN(Type-2 路由) 里 MAC 才是主角：每个
 * PE 都要把本端口学到的 MAC 以 MAC/IP Advertisement 路由通告出去，收到对端通告
 * 时也要把对端 MAC 装进转发表。为了回答“**某个 MAC 我到底有没有、从哪学的**”
 * （用于本地 MAC 与对端通告 MAC 的去重/冲突判断，也就是原注释提到的 win/loss），
 * bgpd 需要一份统一的 MAC 索引，这就是 bgp_mac.c 维护的哈希表。
 *
 * 【数据来源】MAC 并不是 bgpd 自己嗅探的，而是 **zebra 从内核 FDB 同步过来的**：
 * 接口上的 MAC 增删会以“接口 + MAC”事件形式到达，于是有了下面那两个以
 * `struct interface *` 为参数的 add/del 入口。
 *
 * 【与 EVPN 路由的关系】这份表是“本端视角的 MAC 存在性索引”，具体到某条 EVPN
 * 路由的处理（NLRI 组装、RT/VNI、替换/去重）在 bgp_evpn.c；两者配合完成
 * “本端 MAC → 通告出去”“对端 MAC → 装表”。读 bgp_evpn.c 时若看到
 * bgp_mac_entry_exists 之类的判断，就回到这里找实现。
 *
 * 【阅读建议】init/finish（哈希表生命周期）→ add/del_mac_entry（事件入口）
 *            → dump_table（show 命令）→ 两个 exists（查询接口）。
 * ============================================================================
 */
#ifndef __BGP_MAC_H__
#define __BGP_MAC_H__

/* 【函数】哈希表生命周期（进程级，各一次）
 *   bgp_mac_init   : 建 MAC 哈希表（bgpd 启动时）。
 *   bgp_mac_finish : 销毁该表（退出时）。 */
void bgp_mac_init(void);
void bgp_mac_finish(void);

/* 【中文补充：什么是 win/loss】同一个 MAC 可能**同时**出现在“本端口学到的”
 * 与“某个远端 PE 通告来的”两处——此时必须判定谁赢（哪个源应当体现在转发表里），
 * 否则会出现 MAC 漂移或来回震荡。原注释里的 "win/loss" 指的就是这个判定过程：
 * 这两个函数除了增删表项，还要把该 MAC 的归属结果算出来并驱动相应的 EVPN 动作。
 * 另注意：接口上的 MAC 变化**本来就不是**“新增一条独立条目”那么简单，它可能
 * 影响已有条目，所以入口参数是 interface 而不是一个裸的 MAC 地址。
 */
/*
 * Functions to add/delete the mac entry from the appropriate
 * bgp hash's.  Additionally to do some additional processing
 * to allow the win/loss to be processed.
 */
void bgp_mac_add_mac_entry(struct interface *ifp);
void bgp_mac_del_mac_entry(struct interface *ifp);

void bgp_mac_dump_table(struct vty *vty);

/* 【中文补充：两个查询的差别】
 *   bgp_mac_entry_exists(p) : 入参是**前缀**（IPv4/IPv6 主机路由），即“这个 IP
 *       对应的 MAC 我学过吗”——用于 EVPN 的 IP 路由与 MAC 的联动判断。
 *   bgp_mac_exist(mac)      : 入参就是 **MAC**，直接问“这个 MAC 在不在表里”。
 * 一个按 IP 找、一个按 MAC 找，别用反了。
 */
/*
 * Function to lookup the prefix and see if we have a matching mac
 */
bool bgp_mac_entry_exists(const struct prefix *p);
bool bgp_mac_exist(const struct ethaddr *mac);

#endif
