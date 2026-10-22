// SPDX-License-Identifier: GPL-2.0-or-later
/* Community attribute related functions.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/* 【文件导读】 bgp_community.h —— BGP 标准 Community 属性
 *
 * 一、解决什么问题
 *    标准 community（RFC 1997）是一个 4 字节的无符号整数，线格式为
 *    “2 字节 AS + 2 字节值”，对外通常写作 "AS:VALUE"（例如 65001:100）。
 *    运营商 / 客户常用它对路由打标记，再由 route-map、community-list 做
 *    匹配与过滤。本文件负责在内存中表示、解析、打印和比较这些 community。
 *
 * 二、为什么这样设计
 *    同一份 community 集合往往被成千上万条路由共享，如果每条路由都各存
 *    一份，内存开销巨大。FRR 用一个全局哈希表做“驻留 / 归一化”（intern）：
 *    相同的值集合在内存中只保留一份，用 refcnt 引用计数共享，见
 *    community_intern 与 community_unintern。val 中的值始终以网络字节序
 *    保存，并按数值升序去重排序（community_uniq_sort），因此可直接用
 *    memcmp 比较、用 jhash2 计算哈希键。
 *
 * 三、关键数据结构
 *    struct community
 *        size  值的个数（不是字节数）；字节数 = size 乘以 COMMUNITY_SIZE(4)；
 *        val   网络字节序的 4 字节值数组；
 *        str   缓存的字符串（"65001:100 no-export ..."）；
 *        json  可选的 JSON 表示，供 vty json 输出使用；
 *        refcnt 引用计数，0 表示未被哈希表驻留。
 *
 * 四、推荐阅读顺序
 *    1) struct community 与 well-known 常量（本文件）；
 *    2) bgp_community.c 的 community_new 与 community_free；
 *    3) community_uniq_sort 与 community_intern / community_unintern（驻留机制）；
 *    4) set_community_string 与 community_str，以及 community_gettoken 与
 *       community_str2com（字符串互转）；
 *    5) community_strip_accept_own（本文件末尾的内联函数）。
 */

#ifndef _QUAGGA_BGP_COMMUNITY_H
#define _QUAGGA_BGP_COMMUNITY_H

#include "lib/json.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"

/* Communities attribute.  */
/* 【结构】标准 community 集合。
 * val 中每个元素是 4 字节网络字节序整数，含义为 “2 字节 AS 与 2 字节值”。
 * 同一份集合在全局哈希表中只驻留一份，refcnt 记录引用它的 attr 数量。
 */
struct community {
	/* Reference count of communities value.  */
	unsigned long refcnt;

	/* Communities value size.  */
	int size;

	/* Communities value.  */
	uint32_t *val;

	/* Communities as a json object */
	json_object *json;

	/* String of community attribute.  This string is used by vty output
	   and expanded community-list for regular expression match.  */
	char *str;
};

/* Well-known communities value.  */
/* 【well-known 常量】RFC 1997 及后续 RFC 定义的“著名”community。
 * 高 16 位为 0xFFFF 表示保留（well-known）区间：
 *   0xFFFFxxxx 段：较新定义（graceful-shutdown、accept-own 等，含 RFC 8326）；
 *   0xFFFFFFxx 段：RFC 1997 传统三大值（no-export / no-advertise /
 *                  no-export-subconfed 即 local-AS）。
 * 疑点：COMMUNITY_LOCAL_AS 与 COMMUNITY_NO_EXPORT_SUBCONFED 数值相同
 *   （0xFFFFFF03），同一线值在不同语境下打印为不同名称，属历史遗留。
 */
#define COMMUNITY_GSHUT                         0xFFFF0000
#define COMMUNITY_ACCEPT_OWN                    0xFFFF0001
#define COMMUNITY_ROUTE_FILTER_TRANSLATED_v4    0xFFFF0002
#define COMMUNITY_ROUTE_FILTER_v4               0xFFFF0003
#define COMMUNITY_ROUTE_FILTER_TRANSLATED_v6    0xFFFF0004
#define COMMUNITY_ROUTE_FILTER_v6               0xFFFF0005
#define COMMUNITY_LLGR_STALE                    0xFFFF0006
#define COMMUNITY_NO_LLGR                       0xFFFF0007
#define COMMUNITY_ACCEPT_OWN_NEXTHOP            0xFFFF0008
#define COMMUNITY_BLACKHOLE                     0xFFFF029A
#define COMMUNITY_NO_EXPORT                     0xFFFFFF01
#define COMMUNITY_NO_ADVERTISE                  0xFFFFFF02
#define COMMUNITY_NO_EXPORT_SUBCONFED           0xFFFFFF03
#define COMMUNITY_LOCAL_AS                      0xFFFFFF03
#define COMMUNITY_NO_PEER                       0xFFFFFF04

#define COMMUNITY_SIZE 4

/* Macros of community attribute.  */
/* 【宏】com_length 返回字节长度（size 乘 4）；com_lastval 指向最后一个值；
 * com_nthval 取第 n 个值。这些宏直接对 val 指针做算术，调用者需自行保证
 * 下标 n 不越界。
 */
#define com_length(X)    ((X)->size * COMMUNITY_SIZE)
#define com_lastval(X)   ((X)->val + (X)->size - 1)
#define com_nthval(X,n)  ((X)->val + (n))

/* Prototypes of communities attribute functions.  */
/* 【接口分组】
 *   生命周期：community_init / community_finish（管理全局哈希表）、community_free；
 *   解析构造：community_parse（线格式）、community_str2com（字符串）；
 *   驻留共享：community_intern / community_unintern；
 *   字符串输出：community_str（返回对象内部缓存的字符串，调用方不得释放）；
 *   集合运算：uniq_sort / merge / delete / add_val / del_val / include；
 *   比较匹配：match（子集）与 cmp（全等）；
 *   聚合相关：bgp_compute_aggregate_community 系列 / bgp_remove_*（供
 *            summary-only 聚合计算汇总 community 使用）。
 */
extern void community_init(void);
extern void community_finish(void);
extern void community_free(struct community **comm);
extern struct community *community_uniq_sort(struct community *com);
extern struct community *community_parse(uint32_t *pnt, unsigned short length);
extern struct community *community_intern(struct community *com);
extern void community_unintern(struct community **com);
extern char *community_str(struct community *com, bool make_json,
			   bool translate_alias);
extern unsigned int community_hash_make(const struct community *com);
extern struct community *community_str2com(const char *str);
extern bool community_match(const struct community *com1,
			    const struct community *com2);
extern bool community_cmp(const struct community *c1,
			  const struct community *c2);
extern struct community *community_merge(struct community *com1,
					 struct community *com2);
extern struct community *community_delete(struct community *com1,
					  struct community *com2);
extern struct community *community_dup(struct community *com);
extern bool community_include(struct community *com, uint32_t val);
extern void community_add_val(struct community *com, uint32_t val);
extern void community_del_val(struct community *com, uint32_t *val);
extern unsigned long community_count(void);
extern struct hash *community_hash(void);
extern uint32_t community_val_get(struct community *com, int i);
extern void bgp_compute_aggregate_community(struct bgp_aggregate *aggregate,
					    struct community *community);

extern void bgp_compute_aggregate_community_val(
					       struct bgp_aggregate *aggregate);
extern void bgp_compute_aggregate_community_hash(
						struct bgp_aggregate *aggregate,
						struct community *community);
extern void bgp_remove_community_from_aggregate(struct bgp_aggregate *aggregate,
						struct community *community);
extern void bgp_remove_comm_from_aggregate_hash(struct bgp_aggregate *aggregate,
						struct community *community);
extern void bgp_aggr_community_remove(void *arg);

/* This implies that when propagating routes into a VRF, the ACCEPT_OWN
 * community SHOULD NOT be propagated.
 */
/* 【内联函数】community_strip_accept_own：向 VRF 传播路由时，FRR 会把
 * ACCEPT_OWN 从属性中剔除。逻辑：若原有 community 含 ACCEPT_OWN，则复制一份、
 * 删除该值（del_val 需要网络字节序，故先 htonl）；原集合若无人引用则释放；
 * 删除后若集合为空，则把属性置 NULL，否则写回新集合。
 */
static inline void community_strip_accept_own(struct attr *attr)
{
	struct community *old_com = bgp_attr_get_community(attr);
	struct community *new_com = NULL;
	uint32_t val = COMMUNITY_ACCEPT_OWN;

	if (old_com && community_include(old_com, val)) {
		new_com = community_dup(old_com);
		val = htonl(val);
		community_del_val(new_com, &val);

		if (!old_com->refcnt)
			community_free(&old_com);

		if (!new_com->size) {
			community_free(&new_com);
			bgp_attr_set_community(attr, NULL);
		} else {
			bgp_attr_set_community(attr, new_com);
		}
	}
}

#endif /* _QUAGGA_BGP_COMMUNITY_H */
