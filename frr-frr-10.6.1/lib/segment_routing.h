// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 *
 * segment_routing.h: Segment-Routing Library
 *
 * Authors
 * -------
 * Hiroki Shirokura
 * Masakazu Asama
 * Louis Scalbert
 */

#ifndef _FRR_SR_H
#define _FRR_SR_H

#include <zebra.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 【文件导读】segment_routing.h —— Segment Routing 公共常量与接口
 * ============================================================================
 * 【本文件只有两类东西】① IGP 算法号常量；② 一个把算法号转成字符串的函数声明。
 * 把它单独拎成一个头文件的用意是：isisd / ospfd 等协议都要"理解算法号"，若各自
 * 硬编码 0 / 1 / 127，一旦 RFC 调整就会各改各的、还不一致。集中在这里定义，
 * 保证跨协议语义统一。实现见 segment_routing.c。
 *
 * 【使用姿势】协议侧读到线缆上的算法号后，直接拿本头文件的常量做比较，
 * 只在需要打印时才调 sr_algorithm_string()。
 * ============================================================================
 */
/*
 * IGP Algorithm Types
 * https://www.iana.org/assignments/igp-parameters/igp-parameters.xhtml
 */
/* 【常量】各 IGP 算法号。
 *   SPF(0) / STRICT_SPF(1) : RFC8665 定义的两种基本算法。
 *   UNSET(127)             : FRR 自己的哨兵值，表示"这个 SID 还没绑算法"。
 *                            注意它是**值域中间**的洞，不是 0，所以判"未设置"
 *                            必须显式比 127，不能用"非 0 即已设置"的老套路。
 *   FLEX_MIN(128)/MAX(255) : RFC9350 Flex-Algo 的合法区间，用于判断某个算法号
 *                            是否属于灵活算法(见 segment_routing.c 的 default 分支)。
 *   COUNT(256)             : 算法号取值总数，常用于开定长数组或做上界循环。 */
#define SR_ALGORITHM_SPF 0	/* RFC8665 */
#define SR_ALGORITHM_STRICT_SPF 1 /* RFC8665 */
#define SR_ALGORITHM_UNSET 127    /* FRRouting defined */
#define SR_ALGORITHM_FLEX_MIN 128 /* RFC9350 Flex-Algorithm */
#define SR_ALGORITHM_FLEX_MAX 255 /* RFC9350 Flex-Algorithm */
#define SR_ALGORITHM_COUNT 256

/* 【声明】算法号 → 字符串。返回静态常量，调用方不可 free。实现见 .c */
const char *sr_algorithm_string(uint8_t algo);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_SR_H */
