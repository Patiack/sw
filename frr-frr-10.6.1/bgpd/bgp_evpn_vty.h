// SPDX-License-Identifier: GPL-2.0-or-later
/* EVPN VTY functions to EVPN
 * Copyright (C) 2017 6WIND
 */

/* ============================================================================
 * 【文件导读】bgp_evpn_vty.h —— EVPN 的 CLI 层对外接口声明与帮助字符串常量
 * ============================================================================
 * 【职责与定位】本文件是 bgp_evpn_vty.c（EVPN 的 VTY / CLI 实现）的**头文件**，
 *   只声明“别的模块需要调用的少数几个函数”，并集中定义 EVPN 相关命令的帮助
 *   字符串（HELP_STR）常量。真正的 CLI 命令（DEFUN / DEFPY）都注册在 .c 里，
 *   本文件不注册任何命令。
 *
 * 【核心数据结构 / 关键概念】
 *   - AFI_L2VPN / SAFI_EVPN : EVPN 的地址族组合，EVPN 路由表挂在这套 afi/safi 下。
 *   - VNI（VXLAN Network Identifier）: EVPN 的“虚拟网络实例”标识。
 *   - VNI 类型: L2 的 MAC-VRF（MAC 表项）与 L3 的 IP-VRF（IP 前缀）。
 *   - RD（Route Distinguisher）/ RT（Route Target）: EVPN 路由的区分与导入导出。
 *   - ESI（Ethernet Segment Identifier）: 多归属接入的以太网段标识。
 *   - HELP_STR 常量: 传给 DEFUN / DEFPY 的 help 字段；多个命令共用同一份字符串，
 *     集中在这里便于保持一致。
 *
 * 【本文件在主流程中的位置】（谁调它、它调谁）
 *   - bgp_vty.c 的 evpn 配置写到 running-config 时调用 bgp_config_write_evpn_info。
 *   - bgpd 主初始化流程调用 bgp_ethernetvpn_init 来注册全部 EVPN CLI 命令。
 *   - 其他模块（如 EVPN MH）通过 bgp_evpn_cli_parse_type / argv_find_and_parse_oly_idx
 *     复用命令行的参数解析逻辑，避免各写一套。
 *   - 本文件只声明接口，不产生实现；实现在 bgp_evpn_vty.c。
 *
 * 【易错点 / 阅读建议】
 *   - 这里的 HELP_STR 是**给命令解析器看的帮助文本**，不是输出格式；改它只影响
 *     `?` 帮助提示，不影响 show 输出或配置行为。
 *   - bgp_config_write_evpn_info 与 bgp_ethernetvpn_init 都带 extern 且在别处定义，
 *     阅读时须转到 bgp_evpn_vty.c 看实现。
 *   建议阅读顺序：函数声明（3 组）→ HELP_STR 常量 → 末尾 endif。
 * ============================================================================
 */

#ifndef _FRR_BGP_EVPN_VTY_H
#define _FRR_BGP_EVPN_VTY_H

/* 【函数】bgp_config_write_evpn_info —— 把某个 bgp 实例在 EVPN 地址族(afi/safi)
 * 下的配置写进 vty（用于 `show running-config`）。由 bgp_config_write 类入口调用，
 * 写出的内容是 EVPN 全局配置（如 advertise-all-vni 等）与各 VNI 的子配置。 */
extern void bgp_config_write_evpn_info(struct vty *vty, struct bgp *bgp,
				       afi_t afi, safi_t safi);
/* 【函数】bgp_ethernetvpn_init —— 注册全部 EVPN 相关的 VTY 命令（install_element）。
 * bgpd 启动时调用一次；本文件不实现它，实现在 bgp_evpn_vty.c 末尾。 */
extern void bgp_ethernetvpn_init(void);

/* 【帮助字符串常量组】以下几个宏都是 DEFUN / DEFPY 命令的 help 字段文本，
 * 用 `?` 提示时显示。集中定义使多条命令的帮助文字保持一致。
 *   L2VPN_HELP_STR  : “show bgp l2vpn ...” 里 l2vpn 关键字提示。
 *   EVPN_HELP_STR   : evpn 关键字提示。
 *   VNI_HELP_STR / VNI_NUM_HELP_STR / VNI_ALL_HELP_STR : VNI 相关提示。
 *   DETAIL_HELP_STR : detail 关键字提示。
 *   VTEP_HELP_STR / VTEP_IP_HELP_STR / VTEP_IPV6_HELP_STR : 远端 VTEP 相关提示。 */
#define L2VPN_HELP_STR        "Layer 2 Virtual Private Network\n"
#define EVPN_HELP_STR        "Ethernet Virtual Private Network\n"
#define VNI_HELP_STR "VXLAN Network Identifier\n"
#define VNI_NUM_HELP_STR "VNI number\n"
#define VNI_ALL_HELP_STR "All VNIs\n"
#define DETAIL_HELP_STR "Print Detailed Output\n"
#define VTEP_HELP_STR "Remote VTEP\n"
#define VTEP_IP_HELP_STR "Remote VTEP IPv4 address\n"
#define VTEP_IPV6_HELP_STR "Remote VTEP IPv6 address\n"

/* 【函数】argv_find_and_parse_oly_idx —— 解析 overlay index（叠加层索引）类型。
 * 查命令行里是否出现 "gateway-ip" 关键字，命中则把 oly 设为 OVERLAY_INDEX_GATEWAY_IP，
 * 否则保持 OVERLAY_INDEX_TYPE_NONE。用于 L3VNI 场景下 show 命令决定按哪种索引展示。 */
extern int argv_find_and_parse_oly_idx(struct cmd_token **argv, int argc,
				       int *oly_idx,
				       enum overlay_index_type *oly);

/* 【函数】bgp_evpn_cli_parse_type —— 从命令行 “type <ead|mac|multicast|es|...>”
 * 解析出 EVPN 路由类型数值（EVPN_ROUTE_TYPE_* 之一），失败返回 -1。
 * 供多个 show 命令（如 show bgp l2vpn evpn route type ...）共用。 */
/* Parse type from "type <ead|1|...>", return -1 on failure */
extern int bgp_evpn_cli_parse_type(int *type, struct cmd_token **argv,
				   int argc);

/* 【函数】bgp_evpn_show_all_routes —— 打印某 bgp 实例在 EVPN 地址族下的**全部**路由。
 *   bgp      : 要展示的 bgp 实例（通常为 default 或某个 VRF）。
 *   type     : 只打印该 EVPN 路由类型（-1/EVPN_ROUTE_TYPE_MAX 表示不限）。
 *   use_json : true 输出 JSON，false 输出人类可读文本。
 *   detail   : 是否打印明细行（逐路径/逐属性）。
 * 供 `show bgp l2vpn evpn route` 系列命令复用，是 EVPN 路由展示的主入口。 */
extern int bgp_evpn_show_all_routes(struct vty *vty, struct bgp *bgp, int type,
				    bool use_json, int detail);

#endif /* _QUAGGA_BGP_EVPN_VTY_H */
