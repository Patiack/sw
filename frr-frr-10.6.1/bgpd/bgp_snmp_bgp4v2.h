// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP4V2-MIB SNMP support
 *
 * Using: http://www.circitor.fr/Mibs/Html/B/BGP4V2-MIB.php
 *
 * Copyright (C) 2022 Donatas Abraitis <donatas@opensourcerouting.org>
 */
/* ============================================================================
 * 【文件导读】bgp_snmp_bgp4v2.h —— BGP4V2-MIB 的 OID 列号契约
 * ============================================================================
 * 【它是什么】BGP4V2-MIB 是 BGP4-MIB 的升级版：支持 4 字节 AS、IPv6 地址、无类前缀，
 *   并把“邻居表 / 邻居错误表 / 事件时间表 / NLRI 表”拆得更细。本头文件只放 OID 前缀与
 *   各表“列号 -> 名字”的常量，是**对外协议契约**，数值不能改。
 *
 * 【与 BGP4-MIB 的关键差别】
 *   - 地址类型要显式表达：LOCAL_ADDR_TYPE / REMOTE_ADDR_TYPE / NEXT_HOP_ADDR_TYPE
 *     用 1=IPv4、2=IPv6 之类取值；地址本身是变长 OctetString（4 或 16 字节）。
 *   - 邻居相关表每列都带两套实例：OID 尾段为 1 表示 IPv4 族、2 表示 IPv6 族
 *     （这正是 .c 里变量表每列写两遍、尾段分别为 1 和 2 的原因）。
 *   - 多了 NLRI 表（逐前缀暴露路径属性），比 BGP4-MIB 的 bgp4PathAttrTable 更完整。
 *
 * 【ENTRY_OFFSET 是什么】BGP4V2_PEER_ENTRY_OFFSET / BGP4V2_NLRI_ENTRY_OFFSET = 13，
 *   表示“从 OID 根到该表索引之前”的固定段数，供 lookup 函数计算索引起点。
 *
 * 【阅读建议】配合 bgp_snmp_bgp4v2.c 看：.c 里 bgpv2_variables[] 每列写两遍（IPv4/IPv6），
 *   回调按 name[namelen-1] 判断地址族。
 * ============================================================================
 */

#ifndef _FRR_BGP_SNMP_BGP4V2_H_
#define _FRR_BGP_SNMP_BGP4V2_H_

/* 【OID 前缀】bgp4V2 子树：1.3.6.1.3.5.1（在 mib-2 之外的私有 / 实验子树下）。 */
/* bgp4V2 */
#define BGP4V2MIB 1, 3, 6, 1, 3, 5, 1

/* bgp4V2PeerEntry:
 * offset 1.3.6.1.3.5.1.1.2.1.x.(1|2).(4|16) = 13
 * offset 1.3.6.1.4.1.7336.3.2.1.1.2.1.x.1.(1|2) = 16
 */


/* 【trap 编号】作为 smux_trap 的最后一个 sptrap 参数：bgp4V2Established=1、
 * bgp4V2BackwardTransition=2。与文件末尾 *_NOTIFICATION 常量取值一致。 */
/* bgpTraps */
#define BGP4V2ESTABLISHED	 1
#define BGP4V2BACKWARDTRANSITION 2

/* bgpPeerTable */

/* 【表】bgp4V2PeerTable 各列（列号 1..14）。逐邻居表；OID 尾段的 1 / 2 区分 IPv4 / IPv6。
 * 常量 -> 列号：1 Instance(实例 / VRF id), 2 LocalAddrType, 3 LocalAddr, 4 RemoteAddrType,
 * 5 RemoteAddr, 6 LocalPort, 7 LocalAs, 8 LocalIdentifier, 9 RemotePort, 10 RemoteAs,
 * 11 RemoteIdentifier, 12 AdminStatus, 13 State, 14 Description。 */
#define BGP4V2_PEER_ENTRY_OFFSET 13
#define BGP4V2_PEER_INSTANCE 1
#define BGP4V2_PEER_LOCAL_ADDR_TYPE 2
#define BGP4V2_PEER_LOCAL_ADDR 3
#define BGP4V2_PEER_REMOTE_ADDR_TYPE 4
#define BGP4V2_PEER_REMOTE_ADDR 5
#define BGP4V2_PEER_LOCAL_PORT 6
#define BGP4V2_PEER_LOCAL_AS 7
#define BGP4V2_PEER_LOCAL_IDENTIFIER 8
#define BGP4V2_PEER_REMOTE_PORT 9
#define BGP4V2_PEER_REMOTE_AS 10
#define BGP4V2_PEER_REMOTE_IDENTIFIER 11
#define BGP4V2_PEER_ADMIN_STATUS 12
#define BGP4V2_PEER_STATE 13
#define BGP4V2_PEER_DESCRIPTION 14

/* 【表】bgp4V2PeerErrorsEntry 各列（1..10）：最近一次错误的收 / 发两个方向，每方向含
 * code / subcode / 时间 / 文本 / data 五项。1..5 为“收到”、6..10 为“发出”。 */
/* bgp4V2PeerErrorsEntry */
#define BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED 1
#define BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED 2
#define BGP4V2_PEER_LAST_ERROR_RECEIVED_TIME 3
#define BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT 4
#define BGP4V2_PEER_LAST_ERROR_RECEIVED_DATA 5
#define BGP4V2_PEER_LAST_ERROR_CODE_SENT 6
#define BGP4V2_PEER_LAST_ERROR_SUBCODE_SENT 7
#define BGP4V2_PEER_LAST_ERROR_SENT_TIME 8
#define BGP4V2_PEER_LAST_ERROR_SENT_TEXT 9
#define BGP4V2_PEER_LAST_ERROR_SENT_DATA 10

/* 【表】bgp4V2PeerEventTimesEntry 各列（1..2）：进入 Established 的时长、
 * 距上次收到 UPDATE 的时长（均由 monotime() 与内部时间戳相减得到）。 */
/* bgp4V2PeerEventTimesEntry */
#define BGP4V2_PEER_FSM_ESTABLISHED_TIME 1
#define BGP4V2_PEER_PEER_IN_UPDATES_ELAPSED_TIME 2

/* bgp4V2NlriEntry
 * offset 1.3.6.1.3.5.1.1.9.1.x.(1|2).(4|16) = 13
 * offset 1.3.6.1.4.1.7336.3.2.1.1.9.1.x.1.(1|2) = 16
 */
/* 【表】bgp4V2NlriEntry 各列（1..25）：逐 (前缀, 邻居, index) 暴露路径属性。相比
 * BGP4-MIB 的 bgp4PathAttrTable，这里显式带 AFI / SAFI、前缀类型、下一跳类型、
 * link-local 下一跳、MED / local-pref 的 present 标志、聚合信息、AS_PATH 原文等。
 * 1 Index, 2 AFI, 3 SAFI, 4 PrefixType, 5 Prefix, 6 PrefixLen, 7 Best, 8 CalcLocalPref,
 * 9 Origin, 10 NextHopAddrType, 11 NextHopAddr, 12/13 link-local 下一跳类型 / 值,
 * 14 LocalPrefPresent, 15 LocalPref, 16 MedPresent, 17 Med, 18 AtomicAggregate,
 * 19 AggregatorPresent, 20 AggregatorAS, 21 AggregatorAddr, 22 AsPathCalcLength,
 * 23 AsPathString, 24 AsPath, 25 PathAttrUnknown。 */
#define BGP4V2_NLRI_ENTRY_OFFSET 13
#define BGP4V2_NLRI_INDEX 1
#define BGP4V2_NLRI_AFI 2
#define BGP4V2_NLRI_SAFI 3
#define BGP4V2_NLRI_PREFIX_TYPE 4
#define BGP4V2_NLRI_PREFIX 5
#define BGP4V2_NLRI_PREFIX_LEN 6
#define BGP4V2_NLRI_BEST 7
#define BGP4V2_NLRI_CALC_LOCAL_PREF 8
#define BGP4V2_NLRI_ORIGIN 9
#define BGP4V2_NLRI_NEXT_HOP_ADDR_TYPE 10
#define BGP4V2_NLRI_NEXT_HOP_ADDR 11
#define BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR_TYPE 12
#define BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR 13
#define BGP4V2_NLRI_LOCAL_PREF_PRESENT 14
#define BGP4V2_NLRI_LOCAL_PREF 15
#define BGP4V2_NLRI_MED_PRESENT 16
#define BGP4V2_NLRI_MED 17
#define BGP4V2_NLRI_ATOMIC_AGGREGATE 18
#define BGP4V2_NLRI_AGGREGATOR_PRESENT 19
#define BGP4V2_NLRI_AGGREGATOR_AS 20
#define BGP4V2_NLRI_AGGREGATOR_ADDR 21
#define BGP4V2_NLRI_AS_PATH_CALC_LENGTH 22
#define BGP4V2_NLRI_AS_PATH_STRING 23
#define BGP4V2_NLRI_AS_PATH 24
#define BGP4V2_NLRI_PATH_ATTR_UNKNOWN 25

/* 【通知编号】BGP4V2 的两个通知：Established=1、BackwardTransition=2。取值与上面的
 * BGP4V2ESTABLISHED / BGP4V2BACKWARDTRANSITION 相同；.c 实际用上面那组常量。 */
/* bgp4V2Notifications */
#define BGP4V2_ESTABLISHED_NOTIFICATION 1
#define BGP4V2_BACKWARD_TRANSITION_NOTIFICATION 2

/* 【函数】三个对外入口：MIB 初始化 + 两个 trap 发送。实现在 bgp_snmp_bgp4v2.c。 */
extern int bgp_snmp_bgp4v2_init(struct event_loop *tm);
extern int bgpv2TrapEstablished(struct peer *peer);
extern int bgpv2TrapBackwardTransition(struct peer *peer);

#endif /* _FRR_BGP_SNMP_BGP4V2_H_ */
