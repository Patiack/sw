// SPDX-License-Identifier: GPL-2.0-or-later
/* MPLS/BGP L3VPN MIB
 * Copyright (C) 2020 Volta Networks Inc
 */
/* ============================================================================
 * 【文件导读】bgp_mplsvpn_snmp.h —— L3VPN 的 SNMP MIB 支撑
 * ============================================================================
 * 【它是什么】SNMP 网管侧有一套标准 MIB 描述 MPLS/BGP L3VPN（VRF、RT、路由等），
 * 网络管理系统通过 SNMP 轮询 bgpd 就能画拓扑、看 VRF。本文件很短：一个初始化
 * 入口 + 两组**枚举取值的文本常量**——这些数值必须与 MIB 定义里的整数值完全一致，
 * 因为 NMS 是按整数解读的，**改动这里等于改协议**，不要随手调整数值。
 *
 * 【两组常量的含义（名字很长，拆开读就清楚了）】
 *   MPLSL3VPN VRFRTECIDR TYPE xxx  : VRF 里某条“路由目标的 CIDR”条目属于哪一类。
 *       OTHER(1) / REJECT(2) / LOCAL(3) / REMOTE(4) / BLACKHOLE(5)——描述该条目的
 *       来源性质：本地直连、从远端学到、还是被黑洞/拒绝。取值从 1 开始（0 未用），
 *       所以不能用 0 表示“未知”，未知通常靠变量类型里的 NULL 表达。
 *   MPLSVPN VRFRT TYPE xxx         : 一个 RT(Route Target) 在 VRF 上的作用方向：
 *       IMPORT(1) / EXPORT(2) / BOTH(3)。注意 BOTH 是**独立取值 3**而不是
 *       IMPORT|EXPORT 的位或（1|2=3 正好巧合相同，但语义不同，别据此做位运算）。
 *
 * 【与本目录其他文件的关系】RD/RT 的实际数据结构在 bgp_rd.h / bgp_mplsvpn.h；
 * 本文件只服务 SNMP 的呈现层，实现在 bgp_mplsvpn_snmp.c。
 *
 * 【阅读建议】记住“数值是 MIB 契约”这一点即可；实现里主要是把内部 VRF/RT 状态
 *            映射成上表的取值。
 * ============================================================================
 */

/* 【函数】注册 L3VPN MIB 的变量与模块（bgpd 启动且启用 SNMP 时调用一次）。
 * 没有 include guard——它被设计为只在一个 .c 里包含，多次包含会导致重复定义。 */
void bgp_mpls_l3vpn_module_init(void);

/* 【常量】VRF 中一条路由目标 CIDR 条目的来源类型，取值与 MIB 定义绑定(从 1 起)。 */
#define MPLSL3VPNVRFRTECIDRTYPEOTHER 1
#define MPLSL3VPNVRFRTECIDRTYPEREJECT 2
#define MPLSL3VPNVRFRTECIDRTYPELOCAL 3
#define MPLSL3VPNVRFRTECIDRTYPEREMOTE 4
#define MPLSL3VPNVRFRTECIDRTYPEBLACKHOLE 5

/* 【常量】RT 在 VRF 上的作用方向。BOTH=3 是独立枚举值，不是位或结果（见导读）。 */
#define MPLSVPNVRFRTTYPEIMPORT 1
#define MPLSVPNVRFRTTYPEEXPORT 2
#define MPLSVPNVRFRTTYPEBOTH 3
