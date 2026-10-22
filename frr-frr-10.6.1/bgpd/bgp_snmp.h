// SPDX-License-Identifier: GPL-2.0-or-later
/* Common header file for BGP SNMP implementation.
 *
 * Copyright (C) 2022 Donatas Abraitis <donatas@opensourcerouting.org>
 */
/* ============================================================================
 * 【文件导读】bgp_snmp.h —— SNMP 实现的公共头（类型别名 + 两个告警入口）
 * ============================================================================
 * 【职责】把 BGP 状态暴露给 SNMP 网管；本文件是全模块共用的极小头文件，只放
 *   1) 一组“名字好看”的宏——把 SNMP 库里的 ASN_* 类型常量映射成 MIB 里惯用的
 *      类型名（INTEGER/COUNTER32/IPADDRESS…），让后面写 MIB 表结构时代码更贴近
 *      MIB 定义、可读性更好。
 *   2) 两个 **trap（告警）** 入口：会话建立、会话回退。
 *
 * 【重要：这不是“定义新类型”】这些宏只是**别名**，`#define INTEGER ASN_INTEGER`
 * 之后本文件域内的 `INTEGER` 就等于 `ASN_INTEGER`。副作用是：任何**include 了本
 * 头文件的 .c**都会把 `INTEGER`、`COUNTER32` 这些很常见的词变成宏，容易与本文件
 * 之外的普通标识符冲突。读代码时若发现某处 `INTEGER` 被替换成了 ASN 常量，回到
 * 这里找原因。因此本头文件只在 SNMP 相关的 .c 里包含，不要扩散使用。
 *
 * 【两个 trap 的语义】`bgpTrapEstablished` 在邻居进入 Established 时发出；
 * `bgpTrapBackwardTransition` 在 FSM 向后迁移（掉出 Established、或状态回退）时
 * 发出。命名沿用 MIB 里 bgpBackwardTransition 的定义——不是“回退到某个数字状态”，
 * 而是“状态机向后退了”，边界在 FSM 里判断（见 bgp_fsm.c）。
 *
 * 【编译开关】整个 SNMP 功能由 configure 的 `--enable-snmp` 控制；未启用时这些
 * 函数不会被编译进来（调用点通常也在 `#ifdef HAVE_SNMP` 内）。
 *
 * 【阅读建议】本文件很短；配合 bgp_snmp_bgp4.c / bgp_snmp_bgp4v2.c（分别对应
 *            BGP4-MIB 与 BGP4-V2-MIB）一起看。
 * ============================================================================
 */

#ifndef _FRR_BGP_SNMP_H_
#define _FRR_BGP_SNMP_H_

/* 【宏】名称别名（见文件头导读：这是别名而非新类型；注意作用域污染风险）。
 * 映射所依据的是 MIB 定义里的类型写法，例如 MIB 写 Counter32 就映射到 ASN_COUNTER，
 * 写 Gauge32 就映射到 ASN_UNSIGNED。改这些映射等于改变对外暴露的 SNMP 类型，
 * 属于接口变更，不要随手改。 */
/* SNMP value hack. */
#define INTEGER ASN_INTEGER
#define INTEGER32 ASN_INTEGER
#define COUNTER32 ASN_COUNTER
#define OCTET_STRING ASN_OCTET_STR
#define IPADDRESS ASN_IPADDRESS
#define GAUGE32 ASN_UNSIGNED

/* 【函数】两个 SNMP trap 发送入口，成功返回 0、失败返回非 0（沿用 SNMP 库惯例）：
 *   bgpTrapEstablished         : 邻居进入 Established 时发 bgpEstablished 告警。
 *   bgpTrapBackwardTransition  : FSM 向后迁移时发 bgpBackwardTransition 告警。
 * 注意：发 trap 是**尽力而为**的——SNMP 侧不可用不应影响 BGP 会话本身，所以
 * 调用点只检查返回值而不据此改变 BGP 行为。 */
extern int bgpTrapEstablished(struct peer *peer);
extern int bgpTrapBackwardTransition(struct peer *peer);

#endif /* _FRR_BGP_SNMP_H_ */
