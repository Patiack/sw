// SPDX-License-Identifier: GPL-2.0-or-later
/* VLAN (802.1q) common header.
 * Copyright (C) 2016, 2017 Cumulus Networks, Inc.
 */

#ifndef __VLAN_H__
#define __VLAN_H__

/* ============================================================================
 * 【文件导读】vlan.h —— 802.1Q VLAN 标识的公共类型定义
 * ============================================================================
 * 【为什么只有两行内容还要单独一个头文件】vlanid_t 被 zebra、bgpd(EVPN)、
 * lib 的接口模型等多处使用。若各自写 uint16_t，就会出现“有人用 uint32_t、
 * 有人把 4096 当合法值”的不一致。把它集中定义并配一个上界常量，是让“VLAN
 * 号合法范围”只有一处真相(one source of truth)的做法。
 *
 * 【VLAN ID 的值域】802.1Q 的 VID 字段 12 bit，取值 1~4094；0 表示“无 VLAN
 * 标签(优先级标签)”，4095 是保留值。而本文件的 VLANID_MAX 定义为 **4095**，
 * 即“字段能表示的最大值”，是**上界常量**而非“合法最大 VLAN 号”——
 * 判合法时不要写成 `v == VLANID_MAX` 这种包含判断。
 * ============================================================================
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 【类型】VLAN 标识符。选 uint16_t 是因为协议字段 12 bit，16 位足够且能自然
 * 参与主机字节序运算；注意它**不是**网络字节序的类型，收发报文时仍需显式转换。 */
/* VLAN Identifier */
typedef uint16_t vlanid_t;
/* 【常量】VID 字段的最大可表示值(12 bit 全 1)，不是“合法 VLAN 号的个数”。 */
#define VLANID_MAX 4095

#ifdef __cplusplus
}
#endif

#endif /* __VLAN_H__ */
