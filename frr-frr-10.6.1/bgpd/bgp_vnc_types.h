// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015-2016, LabN Consulting, L.L.C.
 */
/* ============================================================================
 * 【文件导读】bgp_vnc_types.h —— VNC(Virtual Network Control) 的 Sub-TLV 类型号
 * ============================================================================
 * 【VNC 是什么】VNC 是 LabN 提出的、用于**虚拟网络编排**的一组 BGP 扩展（draft
 * "BGP-based Virtual Network Control"），不是 IETF 标准 RFC。它复用 BGP 的
 * 扩展团体机制传递虚拟网络的策略/生命周期信息，在 FRR 里通过
 * `#ifdef ENABLE_BGP_VNC` 编译开关整体启用——**默认编译是不带这些代码的**。
 *
 * 【读这个文件要注意的点】
 *   1) 整个内容被 `#ifdef ENABLE_BGP_VNC` 包住：默认配置下这些符号根本不存在，
 *      在代码里搜 bgp_vnc_* 找不到定义时先确认编译开关。
 *   2) 定义的是 **Sub-TLV 类型号**（值本身就是协议常量），不是数据结构。
 *   3) 其中 RFPOPTION 已标注 deprecated：老版本会发它，新实现只需能**识别并
 *      忽略**，不要照它去实现新功能。
 *
 * 【延伸阅读】VNC 的完整实现（lg 团体、对应的解析与状态机）在 lib/ 与
 * 若干 bgp_vnc*.c 里；本仓库若未启用该开关，则只需理解“有个可选的厂商扩展
 * 占用了若干 TLV 类型号”即可。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_VNC_TYPES_H
#define _QUAGGA_BGP_VNC_TYPES_H

#ifdef ENABLE_BGP_VNC
/* 【枚举】bgp_vnc_subtlv_types —— VNC 扩展团体内部 Sub-TLV 的类型号。
 * 这两个值是**线上协议常量**，不能随便改（改了与对端不兼容）。
 * LIFETIME = 1：携带虚拟网络生命周期信息；
 * RFPOPTION = 2：已废弃，新代码不要主动发送，遇到时忽略即可。 */
typedef enum {
	BGP_VNC_SUBTLV_TYPE_LIFETIME = 1,
	BGP_VNC_SUBTLV_TYPE_RFPOPTION = 2, /* deprecated */
} bgp_vnc_subtlv_types;

#endif /* ENABLE_BGP_VNC */
#endif /* _QUAGGA_BGP_VNC_TYPES_H */
