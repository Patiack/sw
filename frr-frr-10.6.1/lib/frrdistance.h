// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zebra distance header
 * Copyright (C) 2023 NVIDIA Corporation
 * Donald Sharp
 *
 * Distance related defines.  FRR needs a common set
 * of values for distance.
 */
/*
 * 文件导读（lib/frrdistance.h）
 *
 * 本文件集中定义 FRR 各路由协议的“行政距离”（Administrative Distance，
 * 简称 distance / AD）。行政距离是一个 0~255 的整数，值越小代表该来源
 * 的路由越可信；当多条路由前缀相同时，路由表（RIB）用它来挑选优选路
 * 由，类似 Cisco 的 AD 概念。
 *
 * 这些默认值被 zebra 与各路由协议守护进程共享，避免各模块各写一套常
 * 量导致不一致。典型值：直连 / 内核为 0，静态路由为 1，eBGP 为 20，
 * EIGRP 为 90，OSPF 为 110，IS-IS 为 115，RIP 为 120，iBGP 为 200，
 * PBR 与 Sharp 为 200 / 150。
 *
 * 与其他模块的关系：zebra 在比较路由优先级时读取这些常量；各协议的
 * vty 配置可覆盖部分默认值（如 distance 命令），但默认值来源即此处。
 *
 * 阅读顺序：本文件只有一组宏，按顺序浏览即可，注意 255 为最大距离值
 * （ZEBRA_MAX_DISTANCE_DEFAULT），常用于表示“不可达 / 最低优先级”。
 */

#ifndef __FRRDISTANCE_H__
#define __FRRDISTANCE_H__

/* 各协议的默认行政距离。数值越小优先级越高；ZEBRA_MAX_DISTANCE_DEFAULT
 * 为 255，通常代表最低可信度 / 不可路由。 */
/* Default Administrative Distance of each protocol. */
#define ZEBRA_KERNEL_DISTANCE_DEFAULT       0
#define ZEBRA_CONNECT_DISTANCE_DEFAULT      0
#define ZEBRA_STATIC_DISTANCE_DEFAULT       1
#define ZEBRA_RIP_DISTANCE_DEFAULT        120
#define ZEBRA_RIPNG_DISTANCE_DEFAULT      120
#define ZEBRA_OSPF_DISTANCE_DEFAULT       110
#define ZEBRA_OSPF6_DISTANCE_DEFAULT      110
#define ZEBRA_ISIS_DISTANCE_DEFAULT       115
#define ZEBRA_IBGP_DISTANCE_DEFAULT       200
#define ZEBRA_EBGP_DISTANCE_DEFAULT        20
#define ZEBRA_TABLE_DISTANCE_DEFAULT       15
#define ZEBRA_TABLEDIRECT_DISTANCE_DEFAULT 14
#define ZEBRA_EIGRP_DISTANCE_DEFAULT       90
#define ZEBRA_NHRP_DISTANCE_DEFAULT        10
#define ZEBRA_LDP_DISTANCE_DEFAULT        150
#define ZEBRA_BABEL_DISTANCE_DEFAULT      100
#define ZEBRA_SHARP_DISTANCE_DEFAULT      150
#define ZEBRA_PBR_DISTANCE_DEFAULT        200
#define ZEBRA_OPENFABRIC_DISTANCE_DEFAULT 115
#define ZEBRA_MAX_DISTANCE_DEFAULT        255

#endif
