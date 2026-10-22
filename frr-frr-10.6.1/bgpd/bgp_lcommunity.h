// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Large Communities Attribute.
 *
 * Copyright (C) 2016 Keyur Patel <keyur@arrcus.com>
 */

/* 【文件导读】 bgp_lcommunity.h —— BGP Large Community 属性
 *
 * 一、解决什么问题
 *    RFC 1997 的标准 community 只有“2 字节 AS 与 2 字节值”，4 字节 ASN 放不下。
 *    RFC 8092 定义 large community：固定 12 字节 = 三个 4 字节字段，写作
 *    "Global:Local1:Local2"（例如 64512:1:100）。三个字段都是完整的 32 位
 *    无符号整数，信息量远大于标准 community，专为承载 4 字节 ASN 而生。
 *
 * 二、为什么这样设计
 *    与标准 community 一样采用全局哈希驻留（intern）+ 引用计数共享：同一份
 *    值集合在内存中只存一份，并按字节序升序去重排序。区别在于：
 *      - 每个值固定 12 字节，比较用 memcmp 直接按字节比（大端序天然按数值排序）；
 *      - 哈希用 jhash（而非 jhash2）；
 *      - 支持 IPv6 扩展社区情形时 unit 概念不存在，只有固定 12 字节。
 *
 * 三、关键数据结构
 *    struct lcommunity
 *        size  值的个数；字节数 = size 乘以 LCOMMUNITY_SIZE(12)；
 *        val   连续的 12 字节值数组（每项内含三个大端 32 位整数）；
 *        str   缓存的字符串（"64512:1:100 ..."）；
 *        json  可选的 JSON 表示，供 vty json 输出使用；
 *        refcnt 引用计数，0 表示未被哈希表驻留。
 *    struct lcommunity_val 单个 12 字节值的裸容器。
 *
 * 四、推荐阅读顺序
 *    1) struct lcommunity 与 LCOMMUNITY_SIZE（本文件）；
 *    2) bgp_lcommunity.c 的 lcommunity_new / lcommunity_free；
 *    3) lcommunity_add_val / lcommunity_uniq_sort（排序去重）；
 *    4) lcommunity_intern / lcommunity_unintern（驻留机制）；
 *    5) set_lcommunity_string / lcommunity_str / lcommunity_gettoken /
 *       lcommunity_str2com（字符串互转）。
 */

#ifndef _QUAGGA_BGP_LCOMMUNITY_H
#define _QUAGGA_BGP_LCOMMUNITY_H

#include "lib/json.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_clist.h"

/* Large Communities value is twelve octets long.  */
#define LCOMMUNITY_SIZE                        12

/* Large Communities attribute.  */
/* 【结构】Large Community 集合。
 * val 中每 12 字节为一个值，内含三个大端 32 位整数：Global、Local1、Local2。
 * 同一份集合在全局哈希表中只驻留一份，refcnt 记录引用它的 attr 数量。
 */
struct lcommunity {
	/* Reference counter.  */
	unsigned long refcnt;

	/* Size of Extended Communities attribute.  */
	int size;

	/* Large Communities value.  */
	uint8_t *val;

	/* Large Communities as a json object */
	json_object *json;

	/* Human readable format string.  */
	char *str;
};

/* Large community value is 12 octets.  */
/* 【结构】单个 large community 值的裸容器，仅含 12 字节数组。 */
struct lcommunity_val {
	char val[LCOMMUNITY_SIZE];
};

/* 【宏】lcom_length 返回字节长度（size 乘 12），用于分配与比较。 */
#define lcom_length(X)    ((X)->size * LCOMMUNITY_SIZE)

/* 【接口分组】
 *   生命周期：lcommunity_init / lcommunity_finish、lcommunity_free；
 *   解析构造：lcommunity_parse（线格式）、lcommunity_str2com（字符串）；
 *   驻留共享：lcommunity_intern / lcommunity_unintern；
 *   字符串输出：lcommunity_str（返回内部缓存，调用方不得释放）；
 *   集合运算：uniq_sort / merge / dup / include / del_val；
 *   比较匹配：cmp（全等）与 match（子集）；
 *   聚合相关：bgp_compute_aggregate_lcommunity 系列 / bgp_remove_*。
 */
/* 【函数】lcommunity_init —— 进程启动时初始化全局 large community 哈希表。
 * 必须在任何 lcommunity_intern 之前调用一次。 */
extern void lcommunity_init(void);
/* 【函数】lcommunity_finish —— 进程退出时销毁全局哈希表及其驻留项。 */
extern void lcommunity_finish(void);
/* 【函数】lcommunity_free —— 释放单个 large community 并置空调用者指针。
 * 仅在引用计数归零（未驻留）时真正释放内存。 */
extern void lcommunity_free(struct lcommunity **lcom);
/* 【函数】lcommunity_parse —— 把线格式的属性值解析成结构体。
 * buf 为原始字节，length 为字节数（必须是 12 的倍数）。 */
extern struct lcommunity *lcommunity_parse(uint8_t *buf, unsigned short length);
/* 【函数】lcommunity_dup —— 深拷贝一份 large community；新对象未驻留（refcnt 为 0）。 */
extern struct lcommunity *lcommunity_dup(struct lcommunity *lcom);
/* 【函数】lcommunity_merge —— 把 lcom2 的值并入 lcom1（就地扩容）。
 * 合并结果通常还需再 lcommunity_uniq_sort 去重排序。 */
extern struct lcommunity *lcommunity_merge(struct lcommunity *lcom1,
					   struct lcommunity *lcom2);
/* 【函数】lcommunity_uniq_sort —— 就地排序并去重，返回处理后的对象。
 * 返回值可能与入参不是同一指针，调用方要用返回值。 */
extern struct lcommunity *lcommunity_uniq_sort(struct lcommunity *lcom);
/* 【函数】lcommunity_intern —— 把对象放入全局哈希表驻留共享。
 * 若已有内容相同者则释放入参并返回既有对象；返回值才是规范对象。 */
extern struct lcommunity *lcommunity_intern(struct lcommunity *lcom);
/* 【函数】lcommunity_cmp —— 比较两个 large community 内容是否全等。
 * 入参是 void 二级指针（指向 struct lcommunity 指针），供哈希查找回调使用。 */
extern bool lcommunity_cmp(const void *arg1, const void *arg2);
/* 【函数】lcommunity_unintern —— 递减引用计数；归零时从哈希表移除并释放。 */
extern void lcommunity_unintern(struct lcommunity **lcom);
/* 【函数】lcommunity_hash_make —— 对 large community 值计算 jhash，供哈希表使用。 */
extern unsigned int lcommunity_hash_make(const void *arg);
/* 【函数】lcommunity_hash —— 返回全局 large community 哈希表句柄（惰性创建）。 */
extern struct hash *lcommunity_hash(void);
/* 【函数】lcommunity_str2com —— 把 64512:1:100 形式的字符串解析成对象。 */
extern struct lcommunity *lcommunity_str2com(const char *str);
/* 【函数】lcommunity_match —— 判断 lcom2 的值是否全部包含在 lcom1 中（子集匹配）。 */
extern bool lcommunity_match(const struct lcommunity *lcom1, const struct lcommunity *lcom2);
/* 【函数】lcommunity_str —— 生成可读字符串（必要时含 JSON）。
 * 结果缓存在对象内，调用方不得释放；translate_alias 控制是否翻译团体别名。 */
extern char *lcommunity_str(struct lcommunity *lcom, bool make_json,
			    bool translate_alias);
/* 【函数】lcommunity_include —— 判断某单个 12 字节值是否已存在于集合中。 */
extern bool lcommunity_include(struct lcommunity *lcom, uint8_t *ptr);
/* 【函数】lcommunity_del_val —— 从集合中就地删除某单个 12 字节值。 */
extern void lcommunity_del_val(struct lcommunity *lcom, uint8_t *ptr);

/* 【函数】bgp_compute_aggregate_lcommunity —— 聚合时把某成员的 large community
 * 累积进聚合体的汇总列表（供随后求交与汇总）。 */
extern void bgp_compute_aggregate_lcommunity(
					struct bgp_aggregate *aggregate,
					struct lcommunity *lcommunity);

/* 【函数】bgp_compute_aggregate_lcommunity_hash —— 哈希去重版本：
 * 把成员的 large community 累积进聚合体哈希表，避免重复值。 */
extern void bgp_compute_aggregate_lcommunity_hash(
					struct bgp_aggregate *aggregate,
					struct lcommunity *lcommunity);
/* 【函数】bgp_compute_aggregate_lcommunity_val —— 在聚合体上把累积值去重排序，
 * 生成最终要通告的 large community。 */
extern void bgp_compute_aggregate_lcommunity_val(
					struct bgp_aggregate *aggregate);

/* 【函数】bgp_remove_lcommunity_from_aggregate —— 从聚合体汇总中移除某成员
 * 贡献的 large community（成员消失时调用）。 */
extern void bgp_remove_lcommunity_from_aggregate(
					struct bgp_aggregate *aggregate,
					struct lcommunity *lcommunity);
/* 【函数】bgp_remove_lcomm_from_aggregate_hash —— 哈希表版本的移除：
 * 从聚合体汇总中移除某成员的 large community。 */
extern void bgp_remove_lcomm_from_aggregate_hash(
					struct bgp_aggregate *aggregate,
					struct lcommunity *lcommunity);
/* 【函数】bgp_aggr_lcommunity_remove —— 释放聚合体 summary 里 large community
 * 占用的内存，作为 list 析构回调注册使用。 */
extern void bgp_aggr_lcommunity_remove(void *arg);

#endif /* _QUAGGA_BGP_LCOMMUNITY_H */
