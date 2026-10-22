// SPDX-License-Identifier: GPL-2.0-or-later
/* Opaque data for Zebra from other daemons.
 *
 * Copyright (C) 2021 Donatas Abraitis <donatas.abraitis@gmail.com>
 */

/*
 * 文件导读（lib/route_opaque.h）
 *
 * 本文件定义“路由不透明数据”（opaque data）的结构，用于让 BGP、OSPF
 * 等上层协议守护进程把一段自定义文本随路由一起下发给 zebra，zebra 只
 * 原样存放、不解析，之后可通过 CLI / 日志回显给运维人员。
 *
 * 用途举例：当 BGP 向 zebra 安装路由时，可附带“为什么这条路由最优”的
 * 选择原因（selection reason）、AS 路径、community 列表等，便于排障。
 *
 * 核心数据结构：
 *   - struct bgp_zebra_opaque：BGP 侧不透明数据，含 aspath、community、
 *     lcommunity、selection_reason 四个定长字符数组。
 *   - struct ospf_zebra_opaque：OSPF 侧不透明数据，含 path_type、area_id、
 *     tag 三个定长字符数组。
 *
 * 关键约束：两者大小通过 static_assert 保证不超过 ZAPI_MESSAGE_OPAQUE_LENGTH
 * （zebra 侧缓冲区长度），否则会编译失败。
 *
 * 与其他模块的关系：结构定义被 bgpd、ospfd 填充，经 zclient / ZAPI 消息
 * 的 opaque 字段传输，zebra 存储于路由对象。
 *
 * 阅读顺序：先看三个尺寸宏来源，再看两个结构体，最后看两个静态断言。
 */

#ifndef FRR_ROUTE_OPAQUE_H
#define FRR_ROUTE_OPAQUE_H

#include "assert.h"
#include "zclient.h"

/* 以下尺寸宏按需从 bgpd 各头文件复制而来：community 每条 4 字节，
 * large-community 每条 12 字节，选择原因字符串缓冲 32 字节。 */
/* copied from bgpd/bgp_community.h */
#define COMMUNITY_SIZE 4
/* copied from bgpd/bgp_lcommunity.h */
#define LCOMMUNITY_SIZE 12
/* copied from bgpd/bgp_route.h */
#define BGP_MAX_SELECTION_REASON_STR_BUF 32

/* BGP 下发给 zebra 的路由附带信息，全部为定长字符串，方便直接打印。 */
struct bgp_zebra_opaque {
	char aspath[256];

	/* Show at least 10 communities AA:BB */
	char community[COMMUNITY_SIZE * 20];

	/* Show at least 10 large-communities AA:BB:CC */
	char lcommunity[LCOMMUNITY_SIZE * 30];

	/* 32 bytes seems enough because of
	 * bgp_path_selection_confed_as_path which is
	 * `Confederation based AS Path`.
	 */
	char selection_reason[BGP_MAX_SELECTION_REASON_STR_BUF];
};

/* OSPF 下发给 zebra 的路由附带信息：路径类型、区域 ID、路由 tag。 */
struct ospf_zebra_opaque {
	char path_type[32];
	char area_id[INET_ADDRSTRLEN];
	char tag[16];
};

/* 编译期校验：两个不透明结构体都必须能塞进 ZAPI 消息的 opaque 缓冲区。 */
static_assert(sizeof(struct bgp_zebra_opaque) <= ZAPI_MESSAGE_OPAQUE_LENGTH,
              "BGP opaque data shouldn't be larger than zebra's buffer");
static_assert(sizeof(struct ospf_zebra_opaque) <= ZAPI_MESSAGE_OPAQUE_LENGTH,
              "OSPF opaque data shouldn't be larger than zebra's buffer");

#endif /* FRR_ROUTE_OPAQUE_H */
