// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025, Donatas Abraitis <donatas@opensourcerouting.org>
 */
/* ============================================================================
 * 【文件导读】bgp_nhc.h —— NHC(下一跳相关特性)属性的内部表示
 * ============================================================================
 * 【NHC 是什么】NHC = Next Hop dependent Characteristics：一个“依附于下一跳”的
 * 附加特性集合，以 **TLV 列表**的形式随 BGP 属性传播。它解决的问题是：同一个
 * 下一跳可能带有多组附加信息（如“再下一跳有哪些节点”、“产生这条路由的 BGP
 * 标识”等），这些信息既不属于标准的 NEXT_HOP 字段，也不适合拆成多个独立属性，
 * 于是统一塞进一个可扩展的 TLV 容器。
 *
 * 【线上格式（必须读懂，它决定了本文件所有长度常量）】
 *   属性值 = 固定头 + 变长下一跳 + TLV 列表：
 *       [ AFI(2) | SAFI(1) | NextHopLen(1) | 下一跳地址(变长) | TLV... ]
 *   每个 TLV = [ Characteristic Code(2) | Characteristic Length(2) | Value ]
 *   由此可以精确推出两个下界：
 *       IPv4 下一跳：4(头) + 4(地址) + 4(TLV 头) = BGP_NHC_MIN_LEN(12)
 *       IPv6 下一跳：4(头) + 16(地址) + 4(TLV 头) = BGP_NHC_MIN_IPV6_LEN(24)
 *   ★ 注意 BGP_NHC_MIN_LEN 这个宏**没有加外层括号**（写成了
 *     `sizeof(uint16_t) + sizeof(uint16_t)`，而 BGP_NHC_TLV_MIN_LEN 那个反而加了
 *     括号）。后者被当成一个整体使用没问题；前者若出现在乘除表达式里会因运算符
 *     优先级出错（如 `BGP_NHC_MIN_LEN * 2` 会先算加法）。使用时请自行加括号。
 *
 * 【生命周期】struct bgp_nhc 与别的 BGP 属性一样参与 **interned 共享**（看 refcnt）：
 * 内容相同的对象全局只留一份、多处引用，因此拿到之后必须当作只读。
 *
 * 【命名提醒】本文件里函数参数名 `bnc` 指的是 **bgp_nhc**，与 bgpd 别处常用的
 * `bnc`(bgp_nexthop_cache，下一跳缓存) 完全不是一个东西。跨文件读代码时最容易
 * 在这里看串，务必按所在文件判断。
 *
 * 【阅读建议】TLV 结构体 → bgp_nhc 结构体 → 长度常量与 TLV code 表 →
 *            末尾的 TLV 增删查与计数接口（实现在 bgp_nhc.c）。
 * ============================================================================
 */

#ifndef _FRR_BGP_NHC_H
#define _FRR_BGP_NHC_H

#include "zebra.h"
#include <bgpd/bgpd.h>
#include <bgpd/bgp_attr.h>

/* 【结构】bgp_nhc_tlv —— 一个“特性”TLV 的解析结果。
 *   next   : 单向链表后继——多个 TLV 串成一条链，挂在 bgp_nhc.tlvs 上。
 *   code   : TLV 类型号（见下面的 BGP_ATTR_NHC_TLV_xxx）。
 *   length : Value 的字节数（决定后面 value 的有效长度）。
 *   value  : 值本体。**变长**，长度由 length 决定；因为长度不定，这里只能存
 *            指针再另配一块内存，不能像定长字段那样内联在结构体里。
 * 注意 value 的所有权：由谁分配、由谁释放，看 bgp_nhc_tlv_new()/free() 的实现。 */
struct bgp_nhc_tlv {
	struct bgp_nhc_tlv *next;
	uint16_t code;
	uint16_t length;
	uint8_t *value;
};

/* 【结构】bgp_nhc —— 一个完整的 NHC 属性值（解析后）。
 *   refcnt       : 共享引用计数（interned 语义，>1 时必须视为只读）。
 *   afi / safi   : 该特性所针对的地址族与子地址族——**该特性只对这对 AFI/SAFI
 *                  的下一跳有意义**，不要跨地址族复用。
 *   nh_length    : 下一跳地址的字节数（线上就是这一字节，取值 4/16/…）。
 *   nh_ipv4 / nh_ipv6 : 下一跳地址，两个成员按 afi 二选一使用（不是 union）。
 *                  → 读代码时要先按 afi 判断该看哪一个，别把两个都当有效。
 *   tlvs_length  : TLV 列表的总字节数（与线上表示一致，便于整段拷贝/比较）。
 *   tlvs         : TLV 链表头（见 bgp_nhc_tlv）。 */
struct bgp_nhc {
	unsigned long refcnt;
	uint16_t afi;
	uint8_t safi;
	uint8_t nh_length;
	struct in_addr nh_ipv4;
	struct in6_addr nh_ipv6;
	uint16_t tlvs_length;
	struct bgp_nhc_tlv *tlvs;
};

/* 【常量】各长度的来历见文件头导读的格式推导。
 * BGP_NHC_TLV_MIN_LEN = 4：一个 TLV 的固定头长度 = 类型(2) + 长度(2)。
 * BGP_NHC_MIN_LEN = 12 / BGP_NHC_MIN_IPV6_LEN = 24：分别是 IPv4 / IPv6 下一跳
 *        时，一个“连 TLV 都没带”的最小 NHC 属性值长度。
 * 提醒：只有 BGP_NHC_TLV_MIN_LEN 加了外层括号，另两个没有，参与运算时请自己补括号。 */
/* 4 => Characteristic Code + Characteristic Length */
#define BGP_NHC_TLV_MIN_LEN sizeof(uint16_t) + sizeof(uint16_t)
/*
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Address Family Identifier   |     SAFI      | Next Hop Len  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ~             Network Address of Next Hop (variable)            ~
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Characteristic Code      |      Characteristic Length    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ~                Characteristic Value (variable)                ~
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define BGP_NHC_MIN_LEN	     12
#define BGP_NHC_MIN_IPV6_LEN 24

/* 【TLV 类型号】这两个值是**线上常量**，不能随意改；其归属的草案见各自上面的
 * 英文注释。因为相关规范仍在演进（草案而非 RFC），解析这类 TLV 时对**未知
 * 类型号必须能安全跳过**——靠的就是每个 TLV 自带的 Length 字段。
 * BGP_ATTR_NHC_TLV_NNHN = 2：Next Next Hop Nodes，列出该下一跳后面还有哪些节点。
 * BGP_ATTR_NHC_TLV_BGPID = 3：携带 BGP Identifier（本文件注明出处见上方英文注释），
 *        具体承载内容以 bgp_nhc.c 的解析与使用代码为准。 */
/* TLV values: */
/* draft-ietf-idr-next-next-hop-nodes-00 */
#define BGP_ATTR_NHC_TLV_NNHN 2
/* draft-ietf-idr-entropy-label */
#define BGP_ATTR_NHC_TLV_BGPID 3

/* 【TLV 与属性的增删查】
 *   bgp_nhc_tlv_add   : 把 tlv 追加到 nhc 的 TLV 链表上（注意所有权归 nhc 了，
 *                       调用方不要自行释放）。
 *   bgp_nhc_tlv_find  : 按 code 查找某个 TLV，找不到返回 NULL。
 *   bgp_nhc_tlv_free  : 释放**单个** TLV（含它的 value）。
 *   bgp_nhc_tlvs_free : 释放**整条** TLV 链表（两函数名字只差一个 s，容易写错）。
 *   bgp_nhc_tlv_new   : 新建一个 TLV；value 指向的长度为 length 的内容会被拷贝，
 *                       而不是接管原指针（以实现为准）。
 *   bgp_nhc_free      : 释放整个 bgp_nhc（含其全部 TLV）。
 *   bgp_nhc_nnhn_count: 统计 NNHN 类 TLV 里列出的节点数（供上层判断“这条路由的
 *                       下一跳后面还挂着多少个节点”）。 */
extern void bgp_nhc_tlv_add(struct bgp_nhc *nhc, struct bgp_nhc_tlv *tlv);
extern struct bgp_nhc_tlv *bgp_nhc_tlv_find(struct bgp_nhc *nhc, uint16_t code);
extern void bgp_nhc_tlv_free(struct bgp_nhc_tlv *tlv);
extern void bgp_nhc_tlvs_free(struct bgp_nhc_tlv *tlv);
extern void bgp_nhc_free(struct bgp_nhc *bnc);
extern struct bgp_nhc_tlv *bgp_nhc_tlv_new(uint16_t code, uint16_t length, const void *value);
extern uint64_t bgp_nhc_nnhn_count(struct bgp_nhc *nhc);

#endif /* _FRR_BGP_NHC_H */
