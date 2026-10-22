// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Defines and structures common to OSPFv2 and OSPFv3
 * Copyright (C) 1998, 99, 2000 Kunihiro Ishiguro, Toshiaki Takada
 */

/*
 * ============================ 文件导读 ============================
 * 1. 职责
 *    OSPFv2 与 OSPFv3 共享的常量定义头文件：协议号、IP 优先级、LSA
 *    老化 / 刷新时间、接口类型、接口默认参数、区域类型、SPF 节流参数。
 *    纯宏集合，不含代码与结构体。
 * 2. 核心数据结构
 *    本文件没有结构体，只有大量 #define 常量，按语义分组。
 * 3. 与其他模块的关系
 *    ospfd / ospf6d 的众多源文件包含本头文件，用于取值与比较。
 *    接口类型常量对应接口状态机，区域类型对应区域配置逻辑。
 * 4. 阅读顺序
 *    按文件顺序自上而下阅读即可：优先级 / 协议号 -> 体系常量 ->
 *    接口类型 -> 接口默认值 -> 区域类型 -> SPF 节流 -> 老化检查。
 * 5. 易踩的坑
 *    - OSPF_LS_REFRESH_TIME 在 DEBUG 下被缩短为 120，非 DEBUG 为 1800。
 *    - 时间单位混用：带 milliseconds 注释的是毫秒，其余多为秒。
 *    - OSPF_AREA_RANGE_COST_UNSPEC 用 -1U 表示未指定，注意无符号语义。
 * ================================================================
 */
#ifndef _LIBOSPFD_H
#define _LIBOSPFD_H

#ifdef __cplusplus
extern "C" {
#endif

/* IP precedence. */
/*
 * IPTOS_PREC_INTERNETCONTROL：IP 优先级中的网络控制值 0xC0。
 * 仅在本平台未定义时补定义，避免与系统头文件冲突。
 */
#ifndef IPTOS_PREC_INTERNETCONTROL
#define IPTOS_PREC_INTERNETCONTROL	0xC0
#endif /* IPTOS_PREC_INTERNETCONTROL */

/* Default protocol, port number. */
/*
 * IPPROTO_OSPFIGP：OSPF 协议号 89，用于原始套接字与报文识别。
 * 同样仅在系统未定义时补定义。
 */
#ifndef IPPROTO_OSPFIGP
#define IPPROTO_OSPFIGP         89
#endif /* IPPROTO_OSPFIGP */

/* Architectural Constants */
/*
 * 体系结构常量：
 * OSPF_LS_REFRESH_TIME：LSA 刷新周期。DEBUG 下 120 秒，否则 1800 秒。
 * OSPF_MIN_LS_INTERVAL：同一 LSA 最小发送间隔，5000 毫秒。
 * OSPF_MIN_LS_ARRIVAL：同一 LSA 最小到达间隔，1000 毫秒。
 * OSPF_MIN_LS_ARRIVAL_MAX：上述到达间隔取值的上界，5000 毫秒。
 * OSPF_LSA_INITIAL_AGE：LSA 初始年龄 0，便于调试观察。
 * OSPF_LSA_MAXAGE：LSA 最大年龄 3600 秒，达到即从数据库清除。
 * OSPF_CHECK_AGE：检查 LSA 年龄的时间粒度，300 秒。
 * OSPF_LSA_MAXAGE_DIFF：判断 MaxAge 差异的阈值，900 秒。
 * OSPF_LS_INFINITY：LSA 泛洪中表示无穷大的标记 0xffffff。
 * OSPF_DEFAULT_DESTINATION：默认路由目的地址 0.0.0.0。
 * OSPF_INITIAL_SEQUENCE_NUMBER：LSA 序列号初值 0x80000001。
 * OSPF_MAX_SEQUENCE_NUMBER：LSA 序列号上限 0x7fffffff。
 * OSPF_INVALID_SEQUENCE_NUMBER：LSA 序列号无效值 0x80000000。
 */
#ifdef DEBUG
#define OSPF_LS_REFRESH_TIME                   120
#else
#define OSPF_LS_REFRESH_TIME                  1800
#endif
#define OSPF_MIN_LS_INTERVAL			 5000	/* milliseconds */
#define OSPF_MIN_LS_ARRIVAL			 1000	/* milliseconds */
#define OSPF_MIN_LS_ARRIVAL_MAX			 5000	/* milliseconds */
#define OSPF_LSA_INITIAL_AGE                     0	/* useful for debug */
#define OSPF_LSA_MAXAGE                       3600
#define OSPF_CHECK_AGE                         300
#define OSPF_LSA_MAXAGE_DIFF                   900
#define OSPF_LS_INFINITY                  0xffffff
#define OSPF_DEFAULT_DESTINATION        0x00000000      /* 0.0.0.0 */
#define OSPF_INITIAL_SEQUENCE_NUMBER    0x80000001U
#define OSPF_MAX_SEQUENCE_NUMBER        0x7fffffffU
#define OSPF_INVALID_SEQUENCE_NUMBER 0x80000000U

/* OSPF Interface Types */
/*
 * OSPF 接口类型：
 * OSPF_IFTYPE_NONE：未指定类型。
 * OSPF_IFTYPE_POINTOPOINT：点对点。
 * OSPF_IFTYPE_BROADCAST：广播网。
 * OSPF_IFTYPE_NBMA：非广播多路访问。
 * OSPF_IFTYPE_POINTOMULTIPOINT：点对多点。
 * OSPF_IFTYPE_VIRTUALLINK：虚链路。
 * OSPF_IFTYPE_LOOPBACK：环回接口。
 * OSPF_IFTYPE_MAX：类型上界计数。
 */
#define OSPF_IFTYPE_NONE		0
#define OSPF_IFTYPE_POINTOPOINT		1
#define OSPF_IFTYPE_BROADCAST		2
#define OSPF_IFTYPE_NBMA		3
#define OSPF_IFTYPE_POINTOMULTIPOINT	4
#define OSPF_IFTYPE_VIRTUALLINK		5
#define OSPF_IFTYPE_LOOPBACK            6
#define OSPF_IFTYPE_MAX			7

/* OSPF interface default values. */
/*
 * OSPF 接口默认参数：
 * OSPF_OUTPUT_COST_DEFAULT：接口输出代价默认 10。
 * OSPF_OUTPUT_COST_INFINITE：输出代价无穷大值 UINT16_MAX。
 * OSPF_ROUTER_DEAD_INTERVAL_DEFAULT：邻居失效间隔默认 40 秒。
 * OSPF_ROUTER_DEAD_INTERVAL_MINIMAL：邻居失效间隔最小值 1 秒。
 * OSPF_HELLO_INTERVAL_DEFAULT：Hello 发送间隔默认 10 秒。
 * OSPF_HELLO_DELAY_DEFAULT：Hello 延迟默认 10 秒。
 * OSPF_ROUTER_PRIORITY_DEFAULT：路由器优先级默认 1。
 * OSPF_RETRANSMIT_INTERVAL_DEFAULT：重传间隔默认 5 秒。
 * OSPF_RETRANSMIT_WINDOW_DEFAULT：重传窗口默认 50 毫秒。
 * OSPF_TRANSMIT_DELAY_DEFAULT：发送延迟默认 1 秒。
 * OSPF_DEFAULT_BANDWIDTH：默认带宽 10000 Mbps。
 * OSPF_ACK_DELAY_DEFAULT：确认延迟默认 1 秒。
 * OSPF_DEFAULT_REF_BANDWIDTH：默认参考带宽 100000 Kbps。
 * OSPF_POLL_INTERVAL_DEFAULT：轮询间隔默认 60 秒。
 * OSPF_NEIGHBOR_PRIORITY_DEFAULT：邻居优先级默认 0。
 * OSPF_MTU_IGNORE_DEFAULT：是否忽略 MTU，默认 0。
 * OSPF_FAST_HELLO_DEFAULT：快速 Hello，默认关闭。
 * OSPF_P2MP_DELAY_REFLOOD_DEFAULT：点对多点延迟重泛洪，默认关闭。
 * OSPF_P2MP_NON_BROADCAST_DEFAULT：点对多点非广播模式，默认关闭。
 * OSPF_OPAQUE_CAPABLE_DEFAULT：Opaque 能力默认开启。
 * OSPF_PREFIX_SUPPRESSION_DEFAULT：前缀抑制默认关闭。
 * OSPF_AREA_BACKBONE：骨干区域 ID 0.0.0.0。
 * OSPF_AREA_RANGE_COST_UNSPEC：区域范围代价未指定，用 -1U 表示。
 */
#define OSPF_OUTPUT_COST_DEFAULT           10
#define OSPF_OUTPUT_COST_INFINITE	   UINT16_MAX
#define OSPF_ROUTER_DEAD_INTERVAL_DEFAULT  40
#define OSPF_ROUTER_DEAD_INTERVAL_MINIMAL   1
#define OSPF_HELLO_INTERVAL_DEFAULT        10
#define OSPF_HELLO_DELAY_DEFAULT           10
#define OSPF_ROUTER_PRIORITY_DEFAULT        1
#define OSPF_RETRANSMIT_INTERVAL_DEFAULT    5
#define OSPF_RETRANSMIT_WINDOW_DEFAULT	    50 /* milliseconds */
#define OSPF_TRANSMIT_DELAY_DEFAULT         1
#define OSPF_DEFAULT_BANDWIDTH		 10000	/* Mbps */
#define OSPF_ACK_DELAY_DEFAULT		    1

#define OSPF_DEFAULT_REF_BANDWIDTH	100000  /* Kbps */

#define OSPF_POLL_INTERVAL_DEFAULT         60
#define OSPF_NEIGHBOR_PRIORITY_DEFAULT      0

#define OSPF_MTU_IGNORE_DEFAULT             0
#define OSPF_FAST_HELLO_DEFAULT             0
#define OSPF_P2MP_DELAY_REFLOOD_DEFAULT	    false
#define OSPF_P2MP_NON_BROADCAST_DEFAULT	    false
#define OSPF_OPAQUE_CAPABLE_DEFAULT true
#define OSPF_PREFIX_SUPPRESSION_DEFAULT	    false
#define OSPF_AREA_BACKBONE              0x00000000      /* 0.0.0.0 */
#define OSPF_AREA_RANGE_COST_UNSPEC	-1U

/*
 * OSPF 区域类型：
 * OSPF_AREA_DEFAULT：普通区域。
 * OSPF_AREA_STUB：末梢区域。
 * OSPF_AREA_NSSA：非完全末梢区域。
 * OSPF_AREA_TYPE_MAX：区域类型上界计数。
 */
#define OSPF_AREA_DEFAULT       0
#define OSPF_AREA_STUB          1
#define OSPF_AREA_NSSA          2
#define OSPF_AREA_TYPE_MAX	3

/* SPF Throttling timer values. */
/*
 * SPF 节流定时参数：
 * OSPF_SPF_DELAY_DEFAULT：SPF 触发延迟默认 0。
 * OSPF_SPF_HOLDTIME_DEFAULT：SPF 保持时间默认 50 毫秒。
 * OSPF_SPF_MAX_HOLDTIME_DEFAULT：SPF 最大保持时间默认 5000 毫秒。
 */
#define OSPF_SPF_DELAY_DEFAULT              0
#define OSPF_SPF_HOLDTIME_DEFAULT           50
#define OSPF_SPF_MAX_HOLDTIME_DEFAULT	    5000

/*
 * OSPF_LSA_MAXAGE_CHECK_INTERVAL：检查 MaxAge LSA 的周期，30 秒。
 * OSPF_LSA_MAXAGE_REMOVE_DELAY_DEFAULT：MaxAge LSA 移除延迟默认 60 秒。
 */
#define OSPF_LSA_MAXAGE_CHECK_INTERVAL		30
#define OSPF_LSA_MAXAGE_REMOVE_DELAY_DEFAULT	60

#ifdef __cplusplus
}
#endif

#endif /* _LIBOSPFD_H */
