// SPDX-License-Identifier: GPL-2.0-or-later
/* VxLAN common header.
 * Copyright (C) 2016, 2017 Cumulus Networks, Inc.
 */

#ifndef __VXLAN_H__
#define __VXLAN_H__

/* ============================================================================
 * 【文件导读】vxlan.h —— VXLAN / EVPN 多归属共用的少量常量
 * ============================================================================
 * 【定位】VXLAN(RFC7348)用 24 位 VNI 区分二层域，配合 EVPN 做控制面。本文件只
 * 放**跨模块共享的常量与枚举**(zebra、bgpd 的 EVPN 代码都要用)，所以抽到 lib/。
 * 这里没有函数、没有全局状态，纯声明。
 *
 * 【两个主题】
 *   ① VNI：vni_t 与 VNI_MAX，划清 24 位的取值范围。
 *   ② EVPN 多归属(Multi-Homing)的 DF(指定转发者)选举：三种算法 + 优先级范围。
 *      DF 选举解决的是“同一段以太网被多条 PE 链路接入时，BUM 流量由谁转发”，
 *      避免重复泛洪；不同算法在“负载均衡程度”与“成员变动时的抖动”之间取舍。
 * ============================================================================
 */
#ifdef __cplusplus
extern "C" {
#endif

/* EVPN MH DF election algorithm */
/* 【常量组：DF 选举算法】EVPN-MH 里选“指定转发者(DF)”的算法编号。
 *   SERVICE_CARVING(0) : 按服务(业务实例)划分，不同 VLAN / VNI 可以选到不同
 *                        DF，负载更均衡，但要求所有成员对划分结果达成一致。
 *   HRW(1)             : Highest Random Weight(最高随机权重)，对每条链路算一个
 *                        权重取最大者；成员增减时只有受影响的少数 VNI 换 DF，
 *                        稳定性最好。
 *   PREF(2)            : 直接按配置的**优先级**选，最大者当 DF；运维可控，但需要
 *                        人工规划，且成员变动引起的切换影响面较大。
 * 【易踩的坑】这三个数值属于配置表示，改动会破坏兼容性，不要重排。 */
#define EVPN_MH_DF_ALG_SERVICE_CARVING 0
#define EVPN_MH_DF_ALG_HRW 1
#define EVPN_MH_DF_ALG_PREF 2

/* preference range for DF election */
/* 【常量组：DF 选举优先级范围】用于 PREF 算法，数值越大越优先。
 *   MIN(0) / MAX(65535) : 取值范围边界(16 位)。
 *   DEFAULT(32767)      : 恰好取中值，使得不显式配置时既方便调高也方便调低。 */
#define EVPN_MH_DF_PREF_MIN 0
#define EVPN_MH_DF_PREF_DEFAULT 32767
#define EVPN_MH_DF_PREF_MAX 65535

/* VxLAN Network Identifier - 24-bit (RFC 7348) */
/* 【类型】VNI(VXLAN Network Identifier)：24 位标识一个 VXLAN 二层域，相当于
 * “可扩展的 VLAN ID”。这里用 uint32_t 承载(只有低 24 位有效)，是为了避免位域或
 * 窄整型带来的对齐与整型提升问题。
 * 【易踩的坑】vni_t 是 uint32_t，编译器**不会**阻止写入超过 24 位的值；越界值在
 *   封装成报文或写进配置之前必须由上层自行校验(对照 VNI_MAX)。 */
typedef uint32_t vni_t;
#define VNI_MAX 16777215 /* (2^24 - 1) */

/* Flooding mechanisms for BUM packets. */
/* Currently supported mechanisms are head-end (ingress) replication
 * (which is the default) and no flooding. Future options could be
 * using PIM-SM, PIM-Bidir etc.
 */
/* 【枚举：BUM 流量的泛洪方式】BUM = Broadcast / Unknown-unicast / Multicast，
 * 即 VXLAN 二层域内需要“泛洪”的三类报文。各成员含义：
 *   VXLAN_FLOOD_HEAD_END_REPL(0) : 头端(入向)复制——由入口 PE 逐个复制并单播
 *                                  发给所有远端 PE。这是**默认值**，不需要底层组播。
 *   VXLAN_FLOOD_DISABLED         : 不泛洪(封闭域，或泛洪交由其他手段完成)。
 *   VXLAN_FLOOD_PIM_SM           : 用 PIM-SM 组播承载泛洪(需要底层组播支持)。
 *   VXLAN_FLOOD_INHERIT_GLOBAL   : 继承全局(网络实例级)的泛洪设置。
 * 【易踩的坑】前三项是**具体机制**，最后一项是**继承语义**——把 INHERIT 当成某个
 *   具体机制去比较，会在解析或展示配置时走错分支。 */
enum vxlan_flood_control {
	VXLAN_FLOOD_HEAD_END_REPL = 0,
	VXLAN_FLOOD_DISABLED,
	VXLAN_FLOOD_PIM_SM,
	VXLAN_FLOOD_INHERIT_GLOBAL,
};

#ifdef __cplusplus
}
#endif

#endif /* __VXLAN_H__ */
