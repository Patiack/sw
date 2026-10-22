// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP dump routine.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_dump.h —— 把 BGP 报文与路由表导出成 MRT 格式文件
 * ============================================================================
 * 【MRT 是什么、为什么要它】MRT(Multi-threaded Routing Toolkit) 是路由软件的
 * 通用**离线记录格式**（RFC 6396 一系）。bgpd 可以把收发的 BGP 报文、以及某时刻
 * 的 RIB 快照按这个格式落盘，之后用 bgpdump / Wireshark / 自研工具离线分析：
 * 排障时“当时邻居到底发了什么、我们的表长什么样”都能回溯，也用于生成研究数据集。
 * 注意这是**旁路功能**：dump 与否不影响路由计算，只是往磁盘写字节。
 *
 * 【两层结构：type 定大类，subtype 定细分】每条 MRT 记录 = type + subtype +
 * 时间戳 + 负载。本文件这些宏就是这两级的取值表，共两大族：
 *   BGP4MP 族 (MSG_PROTOCOL_BGP4MP / _ET) : 记录**事件流**——报文收发、状态机变化。
 *       带 _ET 后缀的表示使用**微秒级时间戳**的扩展记录头。
 *   TABLE_DUMP_V2 族                      : 记录**RIB 快照**——某一时刻表的内容。
 *
 * 【为什么 subtype 编号那么多且不连续】BGP4MP 起初只承载 IPv4 报文体；AS4 出现后
 * 需要用 4 字节 AS 编码，于是新增 *_AS4 变体(4、5)；Add-Path 出现后报文里多出
 * path-id 字段，于是再加 *_ADDPATH 变体(8、9)。因此 2、3、6、7 号在 BGP4MP 里是
 * **空缺**的，不是笔误。LOCAL_ADDPATH 指“本端自己加上 path-id”的那一类。
 * 同一逻辑在 TABLE_DUMP_V2 里重演：IPv4 / IPv6 × 单播 / 组播 × 是否 Add-Path，
 * 于是 6、7 空缺，GENERIC_ADDPATH(12) 是能装任意 AFI/SAFI 的通用容器。
 *
 * 【PEER_INDEX_TABLE 相关宏】TABLE_DUMP_V2 的快照以“PEER_INDEX_TABLE”开头，为
 * 每个邻居分配一个编号，后续 RIB 项只写编号而不写整段 IP——下面后 4 个宏是该表
 * 里两个**位标志**的取值：地址族是 IP 还是 IP6(0 / 1)，AS 宽度是 AS2 还是
 * AS4(0 / 2)。注意 AS 宽度用的是**位 1**(值 2)而不是位 0，写成 1 是错的。
 *
 * 【两个长度宏】BGP_DUMP_HEADER_SIZE 是 MRT 记录头长度；BGP_DUMP_MSG_HEADER 是
 * BGP4MP 报文记录头的长度。两者**不同**，读写时必须各用各的，否则整个文件解析错位。
 *
 * 【阅读建议】宏看完去看 bgp_dump.c 的写文件路径；解析产物时对照 RFC 6396。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_DUMP_H
#define _QUAGGA_BGP_DUMP_H

/* MRT compatible packet dump values.  */
/* type value */
/* 【顶层 type】16 = BGP4MP(普通时间戳)，17 = BGP4MP_ET(带微秒的扩展时间戳)。
 * 值直接取自 MRT 规范，不能改。 */
#define MSG_PROTOCOL_BGP4MP    16
#define MSG_PROTOCOL_BGP4MP_ET 17

/* subtype value */
/* 【BGP4MP 的子类型】0/1/4/5 是事件流（状态变化、报文），2/3 是旧式逐条 entry /
 * snapshot（已很少用，6、7 空缺，见文件头导读）；8~11 是带 path-id 的 Add-Path
 * 变体：8/9 记录**收到的** path-id，10/11 记录**本端添加的** path-id。 */
#define BGP4MP_STATE_CHANGE 0
#define BGP4MP_MESSAGE 1
#define BGP4MP_ENTRY 2
#define BGP4MP_SNAPSHOT 3
#define BGP4MP_MESSAGE_AS4 4
#define BGP4MP_STATE_CHANGE_AS4 5
#define BGP4MP_MESSAGE_ADDPATH 8
#define BGP4MP_MESSAGE_AS4_ADDPATH 9
#define BGP4MP_MESSAGE_LOCAL_ADDPATH 10
#define BGP4MP_MESSAGE_AS4_LOCAL_ADDPATH 11

/* 【记录头长度】MRT 公共头 12 字节；BGP4MP 报文记录头 40 字节。别混用。 */
#define BGP_DUMP_HEADER_SIZE 12
#define BGP_DUMP_MSG_HEADER  40

/* 【TABLE_DUMP_V2 的子类型】1 = PEER_INDEX_TABLE（邻居编号表，每个快照文件开头
 * 必有一份）；2~5 是 IPv4 / IPv6 × 单播 / 组播的 RIB 记录；8~11 是对应的
 * Add-Path 版本；12 是 GENERIC（通用 AFI/SAFI，用于 VRF、VPN、EVPN 等）。
 * 与 BGP4MP 一样，6、7 是**空缺**号。 */
#define TABLE_DUMP_V2_PEER_INDEX_TABLE 1
#define TABLE_DUMP_V2_RIB_IPV4_UNICAST 2
#define TABLE_DUMP_V2_RIB_IPV4_MULTICAST 3
#define TABLE_DUMP_V2_RIB_IPV6_UNICAST 4
#define TABLE_DUMP_V2_RIB_IPV6_MULTICAST 5
#define TABLE_DUMP_V2_RIB_IPV4_UNICAST_ADDPATH 8
#define TABLE_DUMP_V2_RIB_IPV4_MULTICAST_ADDPATH 9
#define TABLE_DUMP_V2_RIB_IPV6_UNICAST_ADDPATH 10
#define TABLE_DUMP_V2_RIB_IPV6_MULTICAST_ADDPATH 11
#define TABLE_DUMP_V2_RIB_GENERIC_ADDPATH 12

/* 【PEER_INDEX_TABLE 的两个位标志】前者是地址族（0=IPv4，1=IPv6），后者是 AS
 * 编码宽度（0=2 字节，2=4 字节）。注意 AS4 的值是 **2**（位 1）而不是 1，
 * 这是 MRT 规范定的位布局，照抄即可。 */
#define TABLE_DUMP_V2_PEER_INDEX_TABLE_IP  0
#define TABLE_DUMP_V2_PEER_INDEX_TABLE_IP6 1
#define TABLE_DUMP_V2_PEER_INDEX_TABLE_AS2 0
#define TABLE_DUMP_V2_PEER_INDEX_TABLE_AS4 2

/* 【函数】生命周期与状态查询：
 *   bgp_dump_init   : 启动时按配置打开 dump 目标（文件 / BMP 之类的输出端）并注册
 *                     定时刷写，是所有 dump 功能的入口。
 *   bgp_dump_finish : 退出时关闭输出、收尾（保证缓冲区内容落盘）。
 *   bgp_dump_state  : 查询“这个邻居当前是否处于需要记录的状态”——dump 并非记录
 *                     所有邻居的全部流量，是否记录取决于配置与邻居状态，本函数用于
 *                     在发报文/变状态前先判断，避免无谓开销。 */
extern void bgp_dump_init(void);
extern void bgp_dump_finish(void);
extern int bgp_dump_state(struct peer *peer);

#endif /* _QUAGGA_BGP_DUMP_H */
