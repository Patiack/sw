// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Traffic Control (TC) main header
 * Copyright (C) 2022  Shichu Yang
 */

/*
 * ============================================================================
 * 【文件导读】lib/tc.h —— 流量控制（Traffic Control）数据结构与接口
 * ============================================================================
 * 职责：
 *   定义 Linux 流量控制子系统的用户态抽象。TC 由三类对象组成：
 *     qdisc  —— 排队规则（挂在网卡上的排队器，如 HTB、NOQUEUE）。
 *     class  —— 分类（qdisc 内部的等级，决定各流量的带宽份额）。
 *     filter —— 过滤器（把报文分类到某个 class，如 flower / u32 / bpf）。
 *   本文件只放“纯数据结构 + 编解码函数原型”，不涉及具体内核交互。
 *
 * 核心数据结构：
 *   enum tc_qdisc_kind —— qdisc / class 的种类枚举。
 *   struct tc_qdisc    —— 一条 qdisc 描述（ifindex + kind + 联合体参数）。
 *   struct tc_class    —— 一个 class 描述（ifindex + handle + kind + 参数）。
 *   struct tc_filter   —— 一条 filter 描述（ifindex + handle + 优先级 +
 *                         协议 + kind + 联合体具体类型）。
 *   union 里的 htb / flower / u32 / bpf / flow 承载各 kind 的专属参数。
 *
 * 与其他模块的关系：
 *   - 与 lib/tc.c 成对：tc.c 实现 tc_getrate，其余编码接口由 zapi 侧实现。
 *   - 通过 stream（lib/stream.h）在 zclient 与 zebra 之间序列化传递。
 *   - 上层通常是 zebra 的 tc 命令处理以及各协议下发的 TC 规则。
 *
 * 阅读顺序：
 *   1. 先看三个枚举：tc_qdisc_kind、tc_filter_kind 以及 flower 的位标志宏。
 *   2. 再看三组“主结构 + 联合体参数”的排布：qdisc / class / filter。
 *   3. 最后看文件末尾的编解码与解析函数原型。
 *
 * 易踩的坑：
 *   - kind 决定 union 里哪个成员生效：读取前务必先判断 kind，否则读到
 *     的可能是复用同一块内存的其它成员。
 *   - handle / priority / protocol 的编码含义来自内核 tc，注意其主次编号
 *     与字节序。
 *   - 带 TODO 的结构（bpf / flow / u32 / qdisc_htb）目前只是占位。
 *   - filter_bm 的置位必须与 src_ip / port 等字段保持一致，否则下发结果
 *     与预期不符。
 * ============================================================================
 */

#ifndef _TC_H
/* include guard：防止本头文件被重复包含。 */
#define _TC_H

#include <zebra.h>
#include "stream.h"

#ifdef __cplusplus
/* 以 C 语言链接方式导出，供 C++ 调用者使用。 */
extern "C" {
#endif

/* TC_STR：VTY 帮助信息里使用的标题串，输出 "Traffic Control" 并换行。 */
#define TC_STR "Traffic Control\n"

/* ============ 第一部分：qdisc（排队规则）相关定义 ============ */
/* qdisc definitions */

/* enum tc_qdisc_kind：qdisc 与 class 共用的种类枚举。
 *   TC_QDISC_UNSPEC  —— 未指定 / 占位（值为 0）。
 *   TC_QDISC_HTB     —— Hierarchical Token Bucket，层级令牌桶，最常用。
 *   TC_QDISC_NOQUEUE —— 不排队，表示该接口当前无需排队。
 */
/* qdisc kind (same as class kinds) */
enum tc_qdisc_kind {
	TC_QDISC_UNSPEC,
	TC_QDISC_HTB,
	TC_QDISC_NOQUEUE,
};

/* struct tc_qdisc_htb：HTB qdisc 的专属参数。
 * 目前预留为空结构，HTB 的详细参数尚未在此建模。
 */
struct tc_qdisc_htb {
	/* currently no members */
};

/* struct tc_qdisc：一条 qdisc 的通用描述。
 *   ifindex —— 绑定到哪块接口（ifindex_t）。
 *   kind    —— 种类，决定下面联合体 u 里哪个成员有效。
 *   u.htb   —— HTB 专属参数（仅 kind == TC_QDISC_HTB 时使用）。
 */
struct tc_qdisc {
	ifindex_t ifindex;

	enum tc_qdisc_kind kind;
	union {
		struct tc_qdisc_htb htb;
	} u;
};

/* ============ 第二部分：class（分类）相关定义 ============ */
/* class definitions */

/* since classes share the same kinds of qdisc, duplicates omitted */
/* struct tc_class_htb：HTB class 的专属参数。
 *   rate —— 承诺速率（bps），令牌桶的基准速率。
 *   ceil —— 最高速率（bps），突发时允许到达的上限。
 */
struct tc_class_htb {
	uint64_t rate;
	uint64_t ceil;
};

/* struct tc_class：一个 class 的通用描述。
 *   ifindex —— 所属接口。
 *   handle  —— class 句柄（内核用 major:minor 编码）。
 *   kind    —— 种类（与 qdisc 共用 tc_qdisc_kind）。
 *   u.htb   —— kind == TC_QDISC_HTB 时的 HTB 参数。
 */
struct tc_class {
	ifindex_t ifindex;
	uint32_t handle;

	enum tc_qdisc_kind kind;
	union {
		struct tc_class_htb htb;
	} u;
};

/* ============ 第三部分：filter（过滤器）相关定义 ============ */
/* filter definitions */

/* enum tc_filter_kind：过滤器种类。
 *   TC_FILTER_UNSPEC  —— 未指定 / 占位。
 *   TC_FILTER_BPF     —— 经典 BPF 字节码过滤器。
 *   TC_FILTER_FLOW    —— flow 分类器（基于报文流）。
 *   TC_FILTER_FLOWER  —— flower 分类器（可匹配五元组等，最灵活）。
 *   TC_FILTER_U32     —— u32 分类器（经典可编程匹配）。
 */
/* filter kinds */
enum tc_filter_kind {
	TC_FILTER_UNSPEC,
	TC_FILTER_BPF,
	TC_FILTER_FLOW,
	TC_FILTER_FLOWER,
	TC_FILTER_U32,
};

/* struct tc_bpf：BPF 过滤器的参数容器。
 * 目前为占位结构，具体字段待补（见内部 TODO）。
 */
struct tc_bpf {
	/* TODO: fill in */
};

/* struct tc_flow：flow 过滤器的参数容器。
 * 目前为占位结构，具体字段待补（见内部 TODO）。
 */
struct tc_flow {
	/* TODO: fill in */
};

/* struct tc_flower：flower 过滤器参数（本项目 TC 里建模最完整的一种）。
 *   classid   —— 命中后把报文归属到的 class 句柄。
 *   filter_bm —— 位图，标记下面哪些字段被启用（见 TC_FLOWER_ 系列宏）。
 *   ip_proto  —— 匹配的 IP 协议号。
 *   src_ip / dst_ip —— 匹配的源 / 目的前缀（struct prefix）。
 *   src_port_min / src_port_max —— 源端口区间。
 *   dst_port_min / dst_port_max —— 目的端口区间。
 *   dsfield / dsfield_mask —— 匹配的 DSCP 字段及其掩码。
 */
struct tc_flower {
	uint32_t classid;

/* TC_FLOWER_* 位标志：与 tc_flower.filter_bm 配合，置位表示“要匹配该
 * 字段”。依次为 IP 协议、源 IP、目的 IP、源端口、目的端口、DSCP 字段。
 */
#define TC_FLOWER_IP_PROTOCOL (1 << 0)
#define TC_FLOWER_SRC_IP (1 << 1)
#define TC_FLOWER_DST_IP (1 << 2)
#define TC_FLOWER_SRC_PORT (1 << 3)
#define TC_FLOWER_DST_PORT (1 << 4)
#define TC_FLOWER_DSFIELD (1 << 5)

	uint32_t filter_bm;

	uint8_t ip_proto;

	struct prefix src_ip;
	struct prefix dst_ip;

	uint16_t src_port_min;
	uint16_t src_port_max;
	uint16_t dst_port_min;
	uint16_t dst_port_max;

	uint8_t dsfield;
	uint8_t dsfield_mask;
};

/* struct tc_u32：u32 过滤器的参数容器。
 * 目前为占位结构，具体字段待补（见内部 TODO）。
 */
struct tc_u32 {
	/* TODO: fill in */
};

/* struct tc_filter：一条 filter 的通用描述。
 *   ifindex  —— 挂在哪块接口。
 *   handle   —— filter 句柄。
 *   priority —— 优先级（数值越小越先匹配）。
 *   protocol —— 匹配的以太网协议（如 IP / IPv6）。
 *   kind     —— 种类，决定联合体 u 里哪个成员有效。
 *   u.bpf / u.flow / u.flower / u.u32 —— 各 kind 的专属参数。
 */
struct tc_filter {
	ifindex_t ifindex;
	uint32_t handle;

	uint32_t priority;
	uint16_t protocol;

	enum tc_filter_kind kind;

	union {
		struct tc_bpf bpf;
		struct tc_flow flow;
		struct tc_flower flower;
		struct tc_u32 u32;
	} u;
};

/* tc_getrate：把人类可读的速率字符串解析成以“比特每秒(bps)”为单位的
 * 数值，例如 "10Mbit"、"100MBps"、"1g" 等；实现在 lib/tc.c。
 */
extern int tc_getrate(const char *str, uint64_t *rate);

/* zapi_tc_qdisc_encode：把一条 qdisc 描述编码进 stream，
 * cmd 为 ZAPI 子命令，供 zebra 与客户端之间传输。
 */
extern int zapi_tc_qdisc_encode(uint8_t cmd, struct stream *s,
				struct tc_qdisc *qdisc);
/* zapi_tc_class_encode：把一条 class 描述编码进 stream，供 zapi 传输。 */
extern int zapi_tc_class_encode(uint8_t cmd, struct stream *s,
				struct tc_class *class);
/* zapi_tc_filter_encode：把一条 filter 描述编码进 stream，供 zapi 传输。 */
extern int zapi_tc_filter_encode(uint8_t cmd, struct stream *s,
				 struct tc_filter *filter);

/* 结束 C 语言链接导出块。 */
#ifdef __cplusplus
}
#endif

/* 结束 include guard。 */
#endif /* _TC_H */
