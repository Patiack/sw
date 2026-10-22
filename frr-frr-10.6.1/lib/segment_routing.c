// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 *
 * segment_routing.c: Segment-Routing Library
 *
 * Authors
 * -------
 * Hiroki Shirokura
 * Masakazu Asama
 * Louis Scalbert
 */

/* ============================================================================
 * 【文件导读】segment_routing.c —— Segment Routing(段路由) 的公共小工具
 * ============================================================================
 * 【这个文件有多小，就有多"基础"】整个 .c 只有一个函数。它在 lib 里的定位是
 * "各 IGP 协议(IS-IS / OSPF)共用的 SR 积木"，所以放在公共库而不是某个守护进程里。
 * 真正的 SR 实现分别在 isisd/ 与 ospfd/，这里只放跨协议共享的映射与常量。
 *
 * 【IGP Algorithm 是什么】SR 的 SID(段标识)绑定在一个"IGP 算法"上：算法决定了
 * 转发表的算路方式(最短路径优先 / 严格最短路径等)。同一个前缀可以为不同算法
 * 各绑一个 SID，所以算法号必须能与人对上话——否则 `show` 出问题时看不懂。
 * 本文件的唯一职责就是把算法号翻译成人类可读字符串。
 *
 * 【算法号取值(RFC8665 / RFC9350)】
 *   0   = SPF           常规最短路径优先
 *   1   = Strict SPF    严格 SPF(只走单跳邻接，不跨"非本算法"节点)
 *   127 = Unset         FRR 自定义的"未设置"哨兵值
 *   128~255 = Flex-Algo RFC9350 的灵活算法区间
 * 命名常量见 segment_routing.h。
 * ============================================================================
 */
#include "segment_routing.h"

/* 【函数】把 IGP 算法号翻译成显示用字符串。
 *   algo: 算法号(见文件导读的取值表)。
 *   返回: 静态字符串常量，**调用方不要 free**。
 * 【易踩的坑】default 分支是有讲究的：只有落入 128~255 才报 "Flex-Algo"，
 *   其它任何没列出的值(包括 2~126 这些保留段)一律报 "Unknown"。
 *   所以看到 "Unknown" 不一定代表脏数据，也可能是 IANA 尚未分配的保留值。
 *   还有一点：函数**不校验 Unset(127)** 与列表外的边界，纯按值分派。 */
const char *sr_algorithm_string(uint8_t algo)
{
	switch (algo) {
	case SR_ALGORITHM_SPF:
		return "SPF";
	case SR_ALGORITHM_STRICT_SPF:
		return "Strict SPF";
	case SR_ALGORITHM_UNSET:
		return "Unset";
	default:
		return algo >= SR_ALGORITHM_FLEX_MIN ? "Flex-Algo" : "Unknown";
	}
}
