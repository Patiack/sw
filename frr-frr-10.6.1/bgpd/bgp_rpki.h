// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bgp_rpki code
 * Copyright (C) 2021 NVIDIA Corporation and Mellanox Technologies, LTD
 *                    All Rights Reserved
 *               Donald Sharp
 */
/* ============================================================================
 * 【文件导读】bgp_rpki.h —— RPKI 源验证的状态定义
 * ============================================================================
 * 【RPKI 是什么、为什么 BGP 需要它】BGP 默认不校验“某个 AS 是否有权通告某个
 * 前缀”，因此前缀劫持很常见。RPKI(RFC6480 体系) 用一个分层的、签名保护的数据库
 * 发布 **ROA**(Route Origin Authorization)：“AS X 有权通告前缀 P（且允许的最大
 * 前缀长度 L）”。BGP 拿学来的路由去比对 ROA，得出三种结论（见下面的枚举），
 * 再由策略决定怎么处理（通常 invalid 丢弃）。比对本身由 **RTR 协议**从
 * rpki 缓存服务器取数据（rpki 守护进程），bgpd 只消费结果。
 *
 * 【状态语义（本文件的核心，务必分清 Not Found 与 Invalid）】
 *   NOTFOUND : **没有任何 ROA 覆盖这个前缀**（数据库里查无此项）→ 通常**放行**，
 *              因为未部署 RPKI 的地址空间属于正常情况。
 *   INVALID  : **有 ROA 覆盖，但本路由的源 AS 或前缀长度不符合**→ 这才是劫持的
 *              特征，通常丢弃。
 *   把 NOTFOUND 与 INVALID 混为一谈是 RPKI 部署中最常见的策略错误——前者是
 *   “没证据”，后者是“有反证据”。
 *
 * 【与选路/过滤的关系】这个枚举值会挂到路径上（见 bgp_route.h / bgp_attr 侧的
 * rpki 字段），既能被 route-map 的 match rpki 使用，也能触发过滤。
 *
 * 【阅读建议】本文件极短：先记住三态语义，再看 bgp_rpki.c 里“怎么拿到比对结果、
 *            结果缓存在哪、如何触发重新校验”。注意 bgpd_privs 是 bgpd 的权限
 *            声明（Linux capabilities / 降权用），放在这里只是历史位置，与本
 *            功能无关。
 * ============================================================================
 */
#ifndef __BGP_RPKI_H__
#define __BGP_RPKI_H__

extern struct zebra_privs_t bgpd_privs;

/* 【枚举】RPKI 验证结果三态（语义与易错点见文件头导读）。
 * RPKI_NOT_BEING_USED 是“压根没启用 RPKI”的初始态，与 NOTFOUND 不同：前者是
 * 功能关闭，后者是功能开启但数据库无覆盖——策略上通常都放行，但排障时要能区分
 * “为什么这条路由被放行”。 */
enum rpki_states {
	RPKI_NOT_BEING_USED,
	RPKI_VALID,
	RPKI_NOTFOUND,
	RPKI_INVALID
};

#endif
