// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP4-MIB SNMP support
 *
 * Using: http://www.circitor.fr/Mibs/Html/B/BGP4-MIB.php
 *
 * Copyright (C) 2022 Donatas Abraitis <donatas@opensourcerouting.org>
 */
/* ============================================================================
 * 【文件导读】bgp_snmp_bgp4.h —— BGP4-MIB（RFC4273）的 OID 列号契约
 * ============================================================================
 * 【它是什么】BGP4-MIB 描述的是“IPv4 单播、AS 用 2 字节表示”时代的 BGP4 状态。
 *   本头文件只放两样东西：
 *     1) 各张表的 OID 前缀（例如 BGP4MIB = 1.3.6.1.2.1.15，即 bgp 子树）；
 *     2) 每张表里“列号 -> 名字”的整型常量（例如 BGPPEERSTATE 表示 bgpPeerTable 的
 *        第 2 列）。这些整型与 MIB 定义一一对应，是**对外协议契约**，数值不能改。
 *
 * 【为什么这样设计】变量表（bgp_snmp_bgp4.c 里的 bgp_variables[]）用这些常量既当
 *   v->magic（用于在回调里 switch 选列）又当 OID 的最后一段，命名常量让
 *   “第 N 列到底是什么”一眼可读。
 *
 * 【关键常量分组】
 *   BGP4MIB            : 整个 bgp 子树的 OID 前缀
 *   BGPESTABLISHED 等   : bgpTraps 下的 trap 编号
 *   BGPPEERxxx         : bgpPeerTable 各列（1..23）
 *   BGPPATHATTRxxx     : bgpPathAttrTable 各列（已废弃，实现里直接返回 NULL）
 *   BGP4PATHATTRxxx    : bgp4PathAttrTable 各列（真正在用的 IPv4 路径属性表）
 *
 * 【阅读建议】配合 bgp_snmp_bgp4.c 一起看：.c 里的 bgp_variables[] 把每个常量映射到
 *   一个回调函数与一段 OID 路径；本文件只解释这些常量的含义与取值。
 * ============================================================================
 */

#ifndef _FRR_BGP_SNMP_BGP4_H_
#define _FRR_BGP_SNMP_BGP4_H_

/* 【标量】bgp 子树下的三个标量叶子：协议版本 / 本地 AS / BGP Identifier（Router ID）。
 * 值 0 只是占位——这里不关心其数值，仅借这些宏名当作变量表里的 magic。 */
#define BGPVERSION 0
#define BGPLOCALAS 0
#define BGPIDENTIFIER 0

/* 【OID 前缀】整个 bgp 子树：iso.org.dod.internet.mgmt.mib-2.bgp = 1.3.6.1.2.1.15。
 * 下面各表的 OID 都以它为前缀。 */
/* bgp */
#define BGP4MIB 1, 3, 6, 1, 2, 1, 15

/* 【trap】bgpTraps 子树下的两个通知编号：bgpEstablished=1、bgpBackwardTransition=2。
 * 由 bgp_snmp_bgp4.c 末尾的 bgp4TrapEstablished / bgp4TrapBackwardTransition 使用。 */
/* bgpTraps */
#define BGPESTABLISHED 1
#define BGPBACKWARDTRANSITION 2

/* 【表】bgpPeerTable 各列（列号 1..23）。这是逐邻居的状态/计数表，索引为邻居 IPv4 地址。
 * 常量 -> 列号与含义：
 *   1  bgpPeerIdentifier              邻居 BGP Router ID
 *   2  bgpPeerState                   FSM 状态
 *   3  bgpPeerAdminStatus             管理状态（1=stop, 2=start），可写
 *   4  bgpPeerNegotiatedVersion       协商出的协议版本
 *   5  bgpPeerLocalAddr               本端 IPv4 地址
 *   6  bgpPeerLocalPort               本端 TCP 端口
 *   7  bgpPeerRemoteAddr              对端 IPv4 地址（表索引）
 *   8  bgpPeerRemotePort              对端 TCP 端口
 *   9  bgpPeerRemoteAs                对端 AS
 *   10 bgpPeerInUpdates              收到 UPDATE 数
 *   11 bgpPeerOutUpdates             发出 UPDATE 数
 *   12 bgpPeerInTotalMessages        收到消息总数
 *   13 bgpPeerOutTotalMessages       发出消息总数
 *   14 bgpPeerLastError              最近一次错误（2 字节：code + subcode）
 *   15 bgpPeerFsmEstablishedTransitions 进入 Established 的次数
 *   16 bgpPeerFsmEstablishedTime     处于 Established 的时长
 *   17 bgpPeerConnectRetryInterval   连接重试间隔，可写
 *   18 bgpPeerHoldTime               当前生效的 HoldTime
 *   19 bgpPeerKeepAlive              当前生效的 KeepAlive
 *   20 bgpPeerHoldTimeConfigured     配置的 HoldTime，可写
 *   21 bgpPeerKeepAliveConfigured    配置的 KeepAlive，可写
 *   22 bgpPeerMinRouteAdvertisementInterval 最小路由通告间隔
 *   23 bgpPeerInUpdateElapsedTime    距上次收到 UPDATE 的时长
 * 疑点：bgp_snmp_bgp4.c 的 bgp_variables[] 里最后两列注册的 OID 列号是 23 / 24，与
 * 这里的 22 / 23 相差 1；而 .c 的 write_bgpPeerTable 又用 name[9]（OID 最后一段）
 * 做 switch。此处只作标注，不改动既有代码。 */
/* bgpPeerTable */
#define BGPPEERIDENTIFIER 1
#define BGPPEERSTATE 2
#define BGPPEERADMINSTATUS 3
#define BGPPEERNEGOTIATEDVERSION 4
#define BGPPEERLOCALADDR 5
#define BGPPEERLOCALPORT 6
#define BGPPEERREMOTEADDR 7
#define BGPPEERREMOTEPORT 8
#define BGPPEERREMOTEAS 9
#define BGPPEERINUPDATES 10
#define BGPPEEROUTUPDATES 11
#define BGPPEERINTOTALMESSAGES 12
#define BGPPEEROUTTOTALMESSAGES 13
#define BGPPEERLASTERROR 14
#define BGPPEERFSMESTABLISHEDTRANSITIONS 15
#define BGPPEERFSMESTABLISHEDTIME 16
#define BGPPEERCONNECTRETRYINTERVAL 17
#define BGPPEERHOLDTIME 18
#define BGPPEERKEEPALIVE 19
#define BGPPEERHOLDTIMECONFIGURED 20
#define BGPPEERKEEPALIVECONFIGURED 21
#define BGPPEERMINROUTEADVERTISEMENTINTERVAL 22
#define BGPPEERINUPDATEELAPSEDTIME 23

/* 【表】bgpPathAttrTable 各列。这是已废弃的“旧版路径属性表”（面向 BGP3 及更早），
 * bgp_snmp_bgp4.c 里的 bgpRcvdPathAttrTable 直接返回 NULL，仅保留占位以维持 MIB 结构。 */
/* bgpPathAttrEntry */
#define BGPPATHATTRPEER 1
#define BGPPATHATTRDESTNETWORK 2
#define BGPPATHATTRORIGIN 3
#define BGPPATHATTRASPATH 4
#define BGPPATHATTRNEXTHOP 5
#define BGPPATHATTRINTERASMETRIC 6

/* 【表】bgp4PathAttrTable 各列（1..14）。这是真正在用的“IPv4 路径属性表”，逐
 * (前缀, 邻居) 暴露一条路径的 BGP 属性；bgp_snmp_bgp4.c 的 bgp4PathAttrTable 填充它。
 * 1=Peer, 2=PrefixLen, 3=Prefix, 4=Origin, 5=ASPathSegment, 6=NextHop,
 * 7=MultiExitDisc, 8=LocalPref, 9=AtomicAggregate, 10=AggregatorAS,
 * 11=AggregatorAddr, 12=CalcLocalPref, 13=Best, 14=Unknown。 */
/* bgp4PathAttrEntry */
#define BGP4PATHATTRPEER 1
#define BGP4PATHATTRIPADDRPREFIXLEN 2
#define BGP4PATHATTRIPADDRPREFIX 3
#define BGP4PATHATTRORIGIN 4
#define BGP4PATHATTRASPATHSEGMENT 5
#define BGP4PATHATTRNEXTHOP 6
#define BGP4PATHATTRMULTIEXITDISC 7
#define BGP4PATHATTRLOCALPREF 8
#define BGP4PATHATTRATOMICAGGREGATE 9
#define BGP4PATHATTRAGGREGATORAS 10
#define BGP4PATHATTRAGGREGATORADDR 11
#define BGP4PATHATTRCALCLOCALPREF 12
#define BGP4PATHATTRBEST 13
#define BGP4PATHATTRUNKNOWN 14

/* 【函数】三个对外入口：两个 trap 发送 + 一个 MIB 初始化。实现在 bgp_snmp_bgp4.c。 */
extern int bgp4TrapEstablished(struct peer *peer);
extern int bgp4TrapBackwardTransition(struct peer *peer);
extern int bgp_snmp_bgp4_init(struct event_loop *tm);

#endif /* _FRR_BGP_SNMP_BGP4_H_ */
