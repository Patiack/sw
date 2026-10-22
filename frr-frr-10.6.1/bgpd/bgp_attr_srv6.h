// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRv6 definitions
 * Copyright (C) 2025 6WIND
 * Loïc SANG <loic.sang@6wind.com>
 */
/* ============================================================================
 * 【文件导读】bgp_attr_srv6.h —— SRv6 Prefix-SID 属性(RFC 9252) 的内部表示
 * ============================================================================
 * 【SRv6 与 BGP 的关系】SRv6(Segment Routing over IPv6) 用 128 bit 的 IPv6 地址
 * 表示“段”，转发时按 SID 列表执行指令。BGP 通过 **Prefix-SID attribute**
 * (RFC 9252) 把“某前缀对应的 SRv6 SID”随路由一起通告给邻居，邻居据此把流量
 * 导入 SRv6 域。本文件只定义该属性的**解析后**的内部结构。
 *
 * 【Prefix-SID attribute 的 TLV 结构（读懂本结构体的前提）】
 *   属性里每个 TLV 由 [TLV 类型 | TLV 长度 | TLV 值] 组成；
 *   类型 5 = SRv6 L3 Service TLV，其值里又嵌套 SRv6 Service Data Sub-TLV，
 *   子 TLV 类型 1 = SRv6 SID Information Sub-TLV（本结构体对应它）。
 *   所以：本结构体 = 一个“SID 及其定位参数”的解析结果；一个前缀可能有多个。
 *
 * 【locator 长度那几个字段的意义】SRv6 SID 是“Locator + Function + Arguments”
 * 拼出来的，这几个 *_len 描述的是**该 SID 在本域内的切分方式**，收到 SID 的
 * 设备据此知道怎么解释它、以及在转发表里怎么安装。读代码时别把它们当成 SID
 * 本身的长度（SID 恒为 16 字节 = struct in6_addr）。
 *
 * 【refcnt】与其它 BGP 属性一样参与 interning 共享：内容相同的对象全局只留一份，
 * 被多处引用时 refcnt>1，此时必须当作只读；refcnt 归零才释放。
 *
 * 【阅读建议】本文件很短：先看结构体字段含义，再对照 bgp_attr.c 里
 *            bgp_attr_srv6_l3service_parse / _intern / _unintern 看它怎么来、怎么走。
 * ============================================================================
 */

#ifndef _BGP_ATTR_SRV6_H_
#define _BGP_ATTR_SRV6_H_

/*
 * 【结构】bgp_attr_srv6_l3service —— 一个 SRv6 SID 及其定位参数，对应 RFC 9252
 * 的 “SRv6 SID Information Sub-TLV”。逐字段：
 *   refcnt             : interned 共享计数（>1 时视为只读，不可就地改）。
 *   sid_flags          : SID 的标志位（如是否对 BGP 对等体不可用/转发表是否已装）。
 *   endpoint_behavior  : 16 bit 的 Endpoint Behavior 编码——**这是 SID 的“动作码”**，
 *                        决定报文到这个 SID 后做什么（如 End.DT4/End.DT6/End.X）。
 *                        编码分配见 IANA “SRv6 Endpoint Behaviors”注册表。
 *   sid                : SID 本体，128 bit IPv6 地址。
 *   loc_block_len / loc_node_len : Locator 的 Block 长度与 Node 长度（SRv6 定位
 *                        部分的分层切分，用于路由通告与压缩）。
 *   func_len           : Function 部分长度。
 *   arg_len            : Arguments 部分长度（可为 0）。
 *   transposition_len / transposition_offset : SID 结构中的“换位”长度与偏移
 *                        (RFC 8986)，用于把 arguments 挪到 SID 中间以节省空间；
 *                        不使用时为 0。
 * 读这些 *_len 时记住：它们描述 **SID 的切分**，不是任何字段自身的字节数。 */

/*
 * Prefix-SID type-5
 * SRv6-L3-Service-TLV
 * RFC 9252
 */
/* 【结构用法】SRv6 Prefix-SID 属性（RFC 9252，type 5 L3-Service TLV 内嵌的 SID
 * 信息子 TLV）解析后就用本结构体保存；一个前缀可携带多个这样的项。字段语义见
 * 文首导读，refcnt 使其可 interning 共享（大于 1 时须视为只读）。 */
struct bgp_attr_srv6_l3service {
	unsigned long refcnt;
	uint8_t sid_flags;
	uint16_t endpoint_behavior;
	struct in6_addr sid;
	uint8_t loc_block_len;
	uint8_t loc_node_len;
	uint8_t func_len;
	uint8_t arg_len;
	uint8_t transposition_len;
	uint8_t transposition_offset;
};
#endif /* _BGP_ATTR_SRV6_H_ */
