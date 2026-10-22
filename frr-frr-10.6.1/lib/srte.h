// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SR-TE definitions
 * Copyright 2020 NetDef Inc.
 *                Sascha Kattelmann
 */

#ifndef _FRR_SRTE_H
#define _FRR_SRTE_H

/* ============================================================================
 * 【文件导读】srte.h —— SR-TE(Segment Routing Traffic Engineering)公共定义
 * ============================================================================
 * 【定位】SR-TE 用“策略(policy)”描述一条从入口到出口的显式路径，策略由若干
 * SID 段(segment list)组成。本文件只放**跨模块共享**的东西：策略名长度上限、
 * 策略状态枚举，以及一个规范比较函数。真正的策略实现分散在 zebra / bgpd /
 * isisd 等模块里。
 *
 * 【color(颜色)是什么】SR-TE 策略用 (endpoint, color) 二元组标识：endpoint 是
 * 出口地址，color 是业务意图(例如“低延迟”“高带宽”)。同一个 endpoint 上允许
 * 并存多条 color 不同的策略。
 *
 * 【为什么比较要先比地址再比 color】见 sr_policy_compare——这两级比较构成了策略
 * 的**规范排序键**，红黑树/哈希表的查找与去重全都依赖它。两级顺序写反，同一条
 * 策略就会被当成两个不同对象。
 * ============================================================================
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 【常量】SR-TE 策略名的最大长度(含结尾符)。配置解析与栈上缓冲都以此为准；
 * 超长名字应在这里被显式拒绝，而不是靠后面静默截断。 */
#define SRTE_POLICY_NAME_MAX_LENGTH 64

/* 【枚举：SR 策略的状态】表示“这条策略当前是否可用”：
 *   ZEBRA_SR_POLICY_UP(0) : 策略已解析出可用路径(至少有一个可用 nexthop)。
 *   ZEBRA_SR_POLICY_DOWN   : 策略不可用(路径算不出来，或依赖的 SID 失效)。
 * 【易踩的坑】UP 的数值恰好是 0——若某处用 calloc / 零初始化表示“尚未知”，就会
 *   与 UP 混淆。判断“未初始化”必须另设标志位，不能拿这个枚举的 0 值当哨兵。 */
enum zebra_sr_policy_status {
	ZEBRA_SR_POLICY_UP = 0,
	ZEBRA_SR_POLICY_DOWN,
};

/* 【函数】SR-TE 策略的规范比较：先比**端点地址**，地址相同再比 **color**。
 *   返回 -1 / 0 / 1，符合 qsort 与 typesafe RB 树回调的约定。
 * 【参数】a_endpoint / b_endpoint 为策略出口地址(ipaddr，IPv4/IPv6 通用)；
 *   a_color / b_color 为业务颜色。
 * 【易踩的坑】① 本函数**不比较策略名**，即同一 (endpoint, color) 只能存在一条
 *   策略——这是有意的设计，但也意味着无法靠它区分两条同名同色的策略；
 *   ② 它是 static inline，改动后所有包含者都要重新编译。 */
static inline int sr_policy_compare(const struct ipaddr *a_endpoint,
				    const struct ipaddr *b_endpoint,
				    uint32_t a_color, uint32_t b_color)
{
	int ret;

	ret = ipaddr_cmp(a_endpoint, b_endpoint);
	if (ret < 0)
		return -1;
	if (ret > 0)
		return 1;
	if (a_color > b_color)
		return 1;
	if (a_color < b_color)
		return -1;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _FRR_SRTE_H */
