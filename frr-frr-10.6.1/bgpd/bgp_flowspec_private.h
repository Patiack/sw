// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Flowspec header . private structs and defines
 * Copyright (C) 2018 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_flowspec_private.h —— FlowSpec 的“匹配分量”类型表
 * ============================================================================
 * 【FlowSpec 的 NLRI 长什么样】它不是前缀，而是**一串匹配分量(component)**，
 * 每个分量 = 「类型号(1 字节) + 该类型的编码内容」。多个分量之间是与(AND)关系，
 * 也就是“所有条件都满足才命中”。本文件就是这 13 种分量类型号的字典，以及
 * 几个内部限值/动作位。
 *
 * 【分量类型号的排布规律（读 RFC 8955 第 4.1 节可对上）】
 *   1 / 2  : 目的 / 源前缀（含长度与掩码补齐，编码方式特殊）。
 *   3      : IP 协议号。
 *   4 / 5 / 6 : 端口 / 目的端口 / 源端口（只有协议是 TCP/UDP 时才允许出现）。
 *   7 / 8  : ICMP 类型 / 码。
 *   9      : TCP flags。
 *   10     : 报文长度。
 *   11     : DSCP。
 *   12     : 分片（fragment）位。
 *   13     : flow-label，**仅 IPv6**（所以注释里特别标出）。
 * 注意 NUM_OF_FLOWSPEC_MATCH_TYPES=12 只统计到 12——13 号是后来为 IPv6 追加的
 * “编外”类型，因此**不要用这个宏去做数组下标上界**去索引包含 13 的表，会漏。
 *
 * 【关于三个 ACTION 宏】它们是写代码时读到就容易困惑的一组：名字不同，取值却
 * 相同(TERMINAL 与 DISTRIBUTE 都是 1，SAMPLE 是 0)。它们表达的是 traffic-action
 * 扩展团体里**各自的位**的定义，而不是三个互斥的枚举值，因此不可互相比较大小、
 * 也不可当作一个三态选择。读代码时以使用处的位运算为准。
 *
 * 【与主头的关系】bgp_flowspec.h 是对外的解析/展示接口；本文件是**内部**限定
 * （缓冲区上限、分量号），只应被 bgp_flowspec*.c 与 bgp_pbr.c 之类内部模块包含。
 * ============================================================================
 */

#ifndef _FRR_BGP_FLOWSPEC_PRIVATE_H
#define _FRR_BGP_FLOWSPEC_PRIVATE_H

/* 【内部限值】单条 FlowSpec NLRI 的总长度上限(字节)。用来给解析时的缓冲/递归
 * 设一个安全边界，防止畸形报文导致越界或无限增长。它不是协议规定值，是 FRR 的
 * 实现约束——所以看到它不要当成“标准里写的 240”。 */
#define FLOWSPEC_NLRI_SIZELIMIT			240

/* Flowspec raffic action bit*/
/* 【traffic-action 位定义】见文件头导读：三者不是互斥枚举，而是同一位组里不同
 * 位的取值，不要拿它们做三选一。 */
#define FLOWSPEC_TRAFFIC_ACTION_TERMINAL	1
#define FLOWSPEC_TRAFFIC_ACTION_SAMPLE		0
#define FLOWSPEC_TRAFFIC_ACTION_DISTRIBUTE	1

/* Flow Spec Component Types */
/* 【分量类型号】含义见文件头导读逐条说明；注意 13(flow-label) 未计入上面的
 * NUM_OF_FLOWSPEC_MATCH_TYPES，索引数组时小心差一。 */
#define NUM_OF_FLOWSPEC_MATCH_TYPES		12
#define FLOWSPEC_DEST_PREFIX		1
#define FLOWSPEC_SRC_PREFIX		2
#define FLOWSPEC_IP_PROTOCOL		3
#define FLOWSPEC_PORT			4
#define FLOWSPEC_DEST_PORT		5
#define FLOWSPEC_SRC_PORT		6
#define FLOWSPEC_ICMP_TYPE		7
#define FLOWSPEC_ICMP_CODE		8
#define FLOWSPEC_TCP_FLAGS		9
#define FLOWSPEC_PKT_LEN		10
#define FLOWSPEC_DSCP			11
#define FLOWSPEC_FRAGMENT		12
#define FLOWSPEC_FLOW_LABEL		13 /* For IPv6 only */

#endif /* _FRR_BGP_FLOWSPEC_PRIVATE_H */
