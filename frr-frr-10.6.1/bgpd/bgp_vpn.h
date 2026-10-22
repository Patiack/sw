// SPDX-License-Identifier: GPL-2.0-or-later
/* VPN common functions to MP-BGP
 * Copyright (C) 2017 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_vpn.h —— VPN 相关 CLI 的对外声明（仅一个函数）
 * ============================================================================
 * 【为什么这个头文件这么小】bgp_vpn.c 提供的是 `show bgp ipv4 vpn ...` 这类
 * **按 RD 过滤查看 VPN 路由**的命令实现；它需要被 bgp_vty.c / bgp_mplsvpn.c
 * 调用，但又不想把 bgpd.h 里的所有细节都暴露出去，于是单独开一个极小的头文件
 * 只声明这一个入口。这是 FRR 里常见的“按功能切小头文件”的做法。
 *
 * 【相关概念】RD(Route Distinguisher)：8 字节，用来在 MP-BGP 里区分**不同 VPN
 * 里相同前缀**的路由；本函数的 prd 参数就是用户指定的 RD 过滤器（可为 NULL，
 * 表示不过滤、显示全部）。
 *
 * 【对比】查看 VPN 路由的参数解析与命令注册在 bgp_vty.c，
 * 实际的“导出/导入 VPN 路由、维护 RD 表”在 bgp_mplsvpn.c。
 * ============================================================================
 */

#ifndef _FRR_BGP_VPN_H
#define _FRR_BGP_VPN_H

#include <zebra.h>

/* 【函数】show_adj_route_vpn —— 显示“某邻居在 VPN 表里的通告/接收路由”，
 * 即 `show bgp <afi> <safi> neighbors X advertised-routes/routes` 的 VPN 版实现。
 * 参数：vty 输出目标；peer 要看的邻居；prd RD 过滤器(NULL=不过滤)；
 *       afi/safi 地址族与子地址族(通常是 AFI_IP/SAFI_MPLS_VPN)；
 *       use_json 是否输出 JSON（给脚本消费）。
 * 为什么单独有 VPN 版本：VPN 路由的存储结构是“RD 表套前缀表”两层，
 * 与普通 IPv4/IPv6 表的遍历方式不同，无法复用 show_adj_route() 的实现。 */
extern int show_adj_route_vpn(struct vty *vty, struct peer *peer,
			      struct prefix_rd *prd, afi_t afi, safi_t safi,
			      bool use_json);

#endif /* _QUAGGA_BGP_VPN_H */
