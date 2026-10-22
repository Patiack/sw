// SPDX-License-Identifier: GPL-2.0-or-later
/* Community attribute related functions.
 * Copyright (C) 1998, 2001 Kunihiro Ishiguro
 */

/* 【文件导读】 bgp_community.c —— 标准 Community 属性的实现
 *
 * 一、本文件做什么
 *    实现 bgp_community.h 声明的全部操作：内存分配 / 释放、排序去重、
 *    字符串与二进制互转、全局哈希驻留（intern）与引用计数、集合运算，
 *    以及供 summary-only 聚合使用的 aggregate 系列函数。
 *
 * 二、核心设计：驻留 + 引用计数
 *    comhash 是全局哈希表（在 community_init 中创建）。任何一份内容相同的
 *    community 集合，在整个进程里只保留一个 struct community 实例：
 *      - community_intern：把临时构造的集合放进哈希；若已存在相同者，
 *        释放临时对象并返回哈希中的那份，同时 refcnt 加一；
 *      - community_unintern：refcnt 减一，减到 0 时从哈希摘除并释放。
 *    因此凡是从 community_parse / community_str2com / intern 拿到的指针，
 *    都是“共享”的：attr 持有它时应保持引用，不要随意 free。
 *
 * 三、线格式与内存格式
 *    线格式：每个 community 4 字节，含义为“2 字节 AS 与 2 字节值”，大端。
 *    内存里 val 数组同样保存大端（网络字节序），于是：
 *      - 用 memcmp 就能做全等 / 子集判断；
 *      - 需要数值时用 ntohl 转换（community_val_get 已代劳）。
 *    字符串格式为 "AS:VALUE"（如 65001:100），well-known 值打印为关键字
 *    （no-export、no-advertise 等）。
 *
 * 四、推荐阅读顺序
 *    community_new / community_free -> community_add_val / community_del_val
 *    -> community_uniq_sort -> community_intern / community_unintern
 *    -> set_community_string / community_str -> community_gettoken /
 *    community_str2com -> 末尾的 aggregate 系列。
 *
 * 五、返回约定提醒
 *    community_str 返回对象内部缓存的 str，属于 com 所有，调用者不得 free，
 *    也不要在 free 掉 com 之后再引用它。
 */

#include <zebra.h>

#include "command.h"
#include "hash.h"
#include "memory.h"
#include "jhash.h"
#include "frrstr.h"

#include "bgpd/bgp_memory.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_community_alias.h"

/* Hash of community attribute. */
/* 【全局变量】comhash：所有已驻留标准 community 的全局哈希表。
 * 哈希键用 community_hash_make、比较用 community_cmp，在 community_init 中
 * 创建、community_finish 中销毁。凡 refcnt 大于 0 的集合都在此表中。
 */
static struct hash *comhash;

/* Allocate a new communities value.  */
/* 【函数】community_new：分配一个空的 community 结构（val / str / json 均未分配，
 * val 为 NULL，size 为 0）。只做内存分配，不驻留。
 */
static struct community *community_new(void)
{
	return XCALLOC(MTYPE_COMMUNITY, sizeof(struct community));
}

/* Free communities value.  */
/* 【函数】community_free：释放一个 community 及其内部的 val / str / json。
 * 参数是二级指针，释放后自动置 NULL，避免悬空指针。
 */
void community_free(struct community **com)
{
	if (!(*com))
		return;

	XFREE(MTYPE_COMMUNITY_VAL, (*com)->val);
	XFREE(MTYPE_COMMUNITY_STR, (*com)->str);

	if ((*com)->json) {
		json_object_free((*com)->json);
		(*com)->json = NULL;
	}

	XFREE(MTYPE_COMMUNITY, (*com));
}

/* Add one community value to the community. */
/* 【函数】community_add_val：把单个值追加到集合末尾（不做去重、不排序）。
 * 入参 val 为主机字节序，函数内部先 htonl 再存入 val 数组（保持大端）。
 */
void community_add_val(struct community *com, uint32_t val)
{
	com->size++;
	com->val = XREALLOC(MTYPE_COMMUNITY_VAL, com->val, com_length(com));

	val = htonl(val);
	memcpy(com_lastval(com), &val, sizeof(uint32_t));
}

/* Delete one community. */
/* 【函数】community_del_val：删除与 val 匹配的一个值（按字节比较，故 val 需为
 * 网络字节序）。命中后前移后续元素并收缩分配；删除后若集合为空则释放 val 数组。
 * 找到一个匹配即返回，故若有重复值只删一个。
 */
void community_del_val(struct community *com, uint32_t *val)
{
	int i = 0;
	int c = 0;

	if (!com->val)
		return;

	while (i < com->size) {
		if (memcmp(com->val + i, val, sizeof(uint32_t)) == 0) {
			c = com->size - i - 1;

			if (c > 0)
				memmove(com->val + i, com->val + (i + 1),
					c * sizeof(*val));

			com->size--;

			if (com->size > 0)
				com->val = XREALLOC(MTYPE_COMMUNITY_VAL,
						    com->val, com_length(com));
			else {
				XFREE(MTYPE_COMMUNITY_VAL, com->val);
			}
			return;
		}
		i++;
	}
}

/* Delete all communities listed in com2 from com1 */
/* 【函数】community_delete：把 com2 中列出的每个值都从 com1 删除（原地修改 com1，
 * 返回 com1）。复杂度为 O(com1->size 乘 com2->size)。
 */
struct community *community_delete(struct community *com1,
				   struct community *com2)
{
	int i = 0;

	while (i < com2->size) {
		community_del_val(com1, com2->val + i);
		i++;
	}

	return com1;
}

/* Callback function from qsort(). */
/* 【函数】community_compare：qsort 比较函数，按数值升序比较两个大端值
 * （先 ntohl 还原为主机序再比大小）。
 */
static int community_compare(const void *a1, const void *a2)
{
	uint32_t v1;
	uint32_t v2;

	memcpy(&v1, a1, sizeof(uint32_t));
	memcpy(&v2, a2, sizeof(uint32_t));
	v1 = ntohl(v1);
	v2 = ntohl(v2);

	if (v1 < v2)
		return -1;
	if (v1 > v2)
		return 1;
	return 0;
}

/* 【函数】community_include：判断 val（主机字节序）是否在集合中。
 * 内部先 htonl 再逐项 memcmp。复杂度 O(size)。
 */
bool community_include(struct community *com, uint32_t val)
{
	int i;

	val = htonl(val);

	for (i = 0; i < com->size; i++)
		if (memcmp(&val, com_nthval(com, i), sizeof(uint32_t)) == 0)
			return true;
	return false;
}

/* 【函数】community_val_get：取出第 i 个值并返回为主机字节序（ntohl）。
 * 调用者需保证 i 在 [0, size) 范围内。
 */
uint32_t community_val_get(struct community *com, int i)
{
	uint8_t *p;
	uint32_t val;

	p = (uint8_t *)com->val;
	p += (i * COMMUNITY_SIZE);

	memcpy(&val, p, sizeof(uint32_t));

	return ntohl(val);
}

/* Sort and uniq given community. */
/* 【函数】community_uniq_sort：返回一个新的集合，值已去重并升序排序。
 * 这是进入哈希驻留前的必经步骤：保证“内容相同即字节相同”，使 memcmp 等价于
 * 语义相等。注意它不驻留，只构造一个新对象（refcnt 为 0），由调用者决定何时
 * intern。
 */
struct community *community_uniq_sort(struct community *com)
{
	int i;
	struct community *new;
	uint32_t val;

	if (!com)
		return NULL;

	new = community_new();
	new->json = NULL;

	for (i = 0; i < com->size; i++) {
		val = community_val_get(com, i);

		if (!community_include(new, val))
			community_add_val(new, val);
	}

	qsort(new->val, new->size, sizeof(uint32_t), community_compare);

	return new;
}

/* Convert communities attribute to string.

   For Well-known communities value, below keyword is used.

   0xFFFF0000      "graceful-shutdown"
   0xFFFF0001      "accept-own"
   0xFFFF0002      "route-filter-translated-v4"
   0xFFFF0003      "route-filter-v4"
   0xFFFF0004      "route-filter-translated-v6"
   0xFFFF0005      "route-filter-v6"
   0xFFFF0006      "llgr-stale"
   0xFFFF0007      "no-llgr"
   0xFFFF0008      "accept-own-nexthop"
   0xFFFF029A      "blackhole"
   0xFFFFFF01      "no-export"
   0xFFFFFF02      "no-advertise"
   0xFFFFFF03      "local-AS"
   0xFFFFFF04      "no-peer"

   For other values, "AS:VAL" format is used.  */
/* 【函数】set_community_string：把集合渲染成字符串缓存到 com->str；make_json
 * 为真时同时构造 com->json（含 "string" 与 "list" 两个字段）。
 * 为省分配，先遍历一遍估算所需长度、再一次性分配、第二遍填充。
 * well-known 值打印为固定关键字；其余打印为 "AS:VAL"；translate_alias 为真时
 * 会把该字符串翻译为配置的别名（bgp_community2alias）。
 */
static void set_community_string(struct community *com, bool make_json,
				 bool translate_alias)
{
	int i;
	char *str;
	int len;
	int first;
	uint32_t comval;
	uint16_t as;
	uint16_t val;
	json_object *json_community_list = NULL;
	json_object *json_string = NULL;

	if (!com)
		return;

	if (make_json) {
		com->json = json_object_new_object();
		json_community_list = json_object_new_array();
	}

	/* When communities attribute is empty.  */
	if (com->size == 0) {
		str = XMALLOC(MTYPE_COMMUNITY_STR, 1);
		str[0] = '\0';

		if (make_json) {
			json_object_string_add(com->json, "string", "");
			json_object_object_add(com->json, "list",
					       json_community_list);
		}
		com->str = str;
		return;
	}

	/* Memory allocation is time consuming work.  So we calculate
	   required string length first.  */
	len = 0;

	for (i = 0; i < com->size; i++) {
		memcpy(&comval, com_nthval(com, i), sizeof(uint32_t));
		comval = ntohl(comval);

		switch (comval) {
		case COMMUNITY_GSHUT:
			len += strlen(" graceful-shutdown");
			break;
		case COMMUNITY_ACCEPT_OWN:
			len += strlen(" accept-own");
			break;
		case COMMUNITY_ROUTE_FILTER_TRANSLATED_v4:
			len += strlen(" route-filter-translated-v4");
			break;
		case COMMUNITY_ROUTE_FILTER_v4:
			len += strlen(" route-filter-v4");
			break;
		case COMMUNITY_ROUTE_FILTER_TRANSLATED_v6:
			len += strlen(" route-filter-translated-v6");
			break;
		case COMMUNITY_ROUTE_FILTER_v6:
			len += strlen(" route-filter-v6");
			break;
		case COMMUNITY_LLGR_STALE:
			len += strlen(" llgr-stale");
			break;
		case COMMUNITY_NO_LLGR:
			len += strlen(" no-llgr");
			break;
		case COMMUNITY_ACCEPT_OWN_NEXTHOP:
			len += strlen(" accept-own-nexthop");
			break;
		case COMMUNITY_BLACKHOLE:
			len += strlen(" blackhole");
			break;
		case COMMUNITY_NO_EXPORT:
			len += strlen(" no-export");
			break;
		case COMMUNITY_NO_ADVERTISE:
			len += strlen(" no-advertise");
			break;
		case COMMUNITY_LOCAL_AS:
			len += strlen(" local-AS");
			break;
		case COMMUNITY_NO_PEER:
			len += strlen(" no-peer");
			break;
		default:
			len = BUFSIZ;
			break;
		}
	}

	/* Allocate memory.  */
	str = XCALLOC(MTYPE_COMMUNITY_STR, len);
	first = 1;

	/* Fill in string.  */
	for (i = 0; i < com->size; i++) {
		memcpy(&comval, com_nthval(com, i), sizeof(uint32_t));
		comval = ntohl(comval);

		if (first)
			first = 0;
		else
			strlcat(str, " ", len);

		switch (comval) {
		case COMMUNITY_GSHUT:
			strlcat(str, "graceful-shutdown", len);
			if (make_json) {
				json_string = json_object_new_string(
					"gracefulShutdown");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_ACCEPT_OWN:
			strlcat(str, "accept-own", len);
			if (make_json) {
				json_string = json_object_new_string(
					"acceptown");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_ROUTE_FILTER_TRANSLATED_v4:
			strlcat(str, "route-filter-translated-v4", len);
			if (make_json) {
				json_string = json_object_new_string(
					"routeFilterTranslatedV4");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_ROUTE_FILTER_v4:
			strlcat(str, "route-filter-v4", len);
			if (make_json) {
				json_string = json_object_new_string(
					"routeFilterV4");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_ROUTE_FILTER_TRANSLATED_v6:
			strlcat(str, "route-filter-translated-v6", len);
			if (make_json) {
				json_string = json_object_new_string(
					"routeFilterTranslatedV6");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_ROUTE_FILTER_v6:
			strlcat(str, "route-filter-v6", len);
			if (make_json) {
				json_string = json_object_new_string(
					"routeFilterV6");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_LLGR_STALE:
			strlcat(str, "llgr-stale", len);
			if (make_json) {
				json_string = json_object_new_string(
					"llgrStale");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_NO_LLGR:
			strlcat(str, "no-llgr", len);
			if (make_json) {
				json_string = json_object_new_string(
					"noLlgr");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_ACCEPT_OWN_NEXTHOP:
			strlcat(str, "accept-own-nexthop", len);
			if (make_json) {
				json_string = json_object_new_string(
					"acceptownnexthop");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_BLACKHOLE:
			strlcat(str, "blackhole", len);
			if (make_json) {
				json_string = json_object_new_string(
					"blackhole");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_NO_EXPORT:
			strlcat(str, "no-export", len);
			if (make_json) {
				json_string =
					json_object_new_string("noExport");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_NO_ADVERTISE:
			strlcat(str, "no-advertise", len);
			if (make_json) {
				json_string =
					json_object_new_string("noAdvertise");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_LOCAL_AS:
			strlcat(str, "local-AS", len);
			if (make_json) {
				json_string = json_object_new_string("localAs");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		case COMMUNITY_NO_PEER:
			strlcat(str, "no-peer", len);
			if (make_json) {
				json_string = json_object_new_string("noPeer");
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		default:
			as = CHECK_FLAG((comval >> 16), 0xFFFF);
			val = CHECK_FLAG(comval, 0xFFFF);
			char buf[32];
			snprintf(buf, sizeof(buf), "%u:%d", as, val);
			const char *com2alias =
				translate_alias ? bgp_community2alias(buf) : buf;

			strlcat(str, com2alias, len);
			if (make_json) {
				json_string = json_object_new_string(com2alias);
				json_object_array_add(json_community_list,
						      json_string);
			}
			break;
		}
	}

	if (make_json) {
		json_object_string_add(com->json, "string", str);
		json_object_object_add(com->json, "list", json_community_list);
	}
	com->str = str;
}

/* Intern communities attribute.  */
/* 【函数】community_intern：驻留。把 com 放入全局哈希：
 *   若哈希中已有内容相同者，释放传入的 com 并返回已存在者；
 *   否则返回 com 本身。无论哪种情况，返回者的 refcnt 加一；
 *   若返回者的 str 尚未生成，顺手生成。
 * 前置断言传入对象的 refcnt 必须为 0，即只允许驻留“未共享”的新对象。
 */
struct community *community_intern(struct community *com)
{
	struct community *find;

	/* Assert this community structure is not interned. */
	assert(com->refcnt == 0);

	/* Lookup community hash. */
	find = (struct community *)hash_get(comhash, com, hash_alloc_intern);

	/* Argument com is allocated temporary.  So when it is not used in
	   hash, it should be freed.  */
	if (find != com)
		community_free(&com);

	/* Increment reference counter.  */
	find->refcnt++;

	/* Make string.  */
	if (!find->str)
		set_community_string(find, false, true);

	return find;
}

/* Free community attribute. */
/* 【函数】community_unintern：解除一次引用。refcnt 减一；减到 0 时从哈希摘除
 * 并释放，同时把调用者的指针置 NULL。断言该对象确实在哈希中。
 */
void community_unintern(struct community **com)
{
	struct community *ret;

	if (!*com)
		return;

	if ((*com)->refcnt)
		(*com)->refcnt--;

	/* Pull off from hash.  */
	if ((*com)->refcnt == 0) {
		/* Community value com must exist in hash. */
		ret = (struct community *)hash_release(comhash, *com);
		assert(ret != NULL);

		community_free(com);
	}
}

/* Create new community attribute. */
/* 【函数】community_parse：从报文的裸字节解析。pnt 指向网络字节序数据，
 * length 是字节数（必须是 4 的倍数，否则返回 NULL）。内部构造临时结构后调用
 * community_uniq_sort 去重排序，再 intern 成共享对象返回。
 */
struct community *community_parse(uint32_t *pnt, unsigned short length)
{
	struct community tmp;
	struct community *new;

	/* If length is malformed return NULL. */
	if (length % COMMUNITY_SIZE)
		return NULL;

	/* Make temporary community for hash look up. */
	tmp.size = length / COMMUNITY_SIZE;
	tmp.val = pnt;

	new = community_uniq_sort(&tmp);

	return community_intern(new);
}

/* 【函数】community_dup：深拷贝一份集合（拷贝 val，不复制 refcnt，也不驻留）。
 * 新对象 refcnt 为 0，可继续修改或用于 intern。
 */
struct community *community_dup(struct community *com)
{
	struct community *new;

	new = XCALLOC(MTYPE_COMMUNITY, sizeof(struct community));
	new->size = com->size;
	if (new->size) {
		new->val = XMALLOC(MTYPE_COMMUNITY_VAL,
				   com->size * COMMUNITY_SIZE);
		memcpy(new->val, com->val, com->size * COMMUNITY_SIZE);
	} else
		new->val = NULL;
	return new;
}

/* Return string representation of communities attribute. */
/* 【函数】community_str：返回 com->str（内部缓存），必要时惰性生成。
 * 返回的字符串归 com 所有，调用者不得 free。make_json 为真且已有 str 但无 json
 * 时，会先释放旧 str 再重建以补齐 json。
 */
char *community_str(struct community *com, bool make_json, bool translate_alias)
{
	if (!com)
		return NULL;

	if (make_json && !com->json && com->str)
		XFREE(MTYPE_COMMUNITY_STR, com->str);

	if (!com->str)
		set_community_string(com, make_json, translate_alias);
	return com->str;
}

/* Make hash value of community attribute. This function is used by
   hash package.*/
/* 【函数】community_hash_make：对 val 数组（大端）做 jhash2，得到哈希键。
 * 内容相同必然键相同，因此可用于哈希驻留与查找。
 */
unsigned int community_hash_make(const struct community *com)
{
	uint32_t *pnt = com->val;

	return jhash2(pnt, com->size, 0x43ea96c1);
}

/* 【函数】community_match：判断 com2 是否为 com1 的子集（com1 含 com2 的全部值）。
 * 双方都为空时返回 true。利用两个指针同步推进的归并式遍历，复杂度 O(n+m)。
 */
bool community_match(const struct community *com1, const struct community *com2)
{
	int i = 0;
	int j = 0;

	if (com1 == NULL && com2 == NULL)
		return true;

	if (com1 == NULL || com2 == NULL)
		return false;

	if (com1->size < com2->size)
		return false;

	/* Every community on com2 needs to be on com1 for this to match */
	while (i < com1->size && j < com2->size) {
		if (memcmp(com1->val + i, com2->val + j, sizeof(uint32_t)) == 0)
			j++;
		i++;
	}

	if (j == com2->size)
		return true;
	else
		return false;
}

/* 【函数】community_cmp：全等比较（size 相同且 val 字节全等）。这是哈希表的
 * key 比较回调。两个都为 NULL 视为相等。
 */
bool community_cmp(const struct community *com1, const struct community *com2)
{
	if (com1 == NULL && com2 == NULL)
		return true;
	if (com1 == NULL || com2 == NULL)
		return false;

	if (com1->size == com2->size)
		if (memcmp(com1->val, com2->val, com1->size * COMMUNITY_SIZE)
		    == 0)
			return true;
	return false;
}

/* Add com2 to the end of com1. */
/* 【函数】community_merge：把 com2 的全部值追加到 com1 末尾（原地扩展 com1，
 * 不做去重；com2 保持不变）。多用于聚合时合并多份集合，之后通常再 uniq_sort。
 */
struct community *community_merge(struct community *com1,
				  struct community *com2)
{
	com1->val = XREALLOC(MTYPE_COMMUNITY_VAL, com1->val,
			     (com1->size + com2->size) * COMMUNITY_SIZE);

	memcpy(com1->val + com1->size, com2->val, com2->size * COMMUNITY_SIZE);
	com1->size += com2->size;

	return com1;
}

/* Community token enum. */
/* 【枚举】community_token：字符串解析器的词法类型。val 是 "AS:VAL" 数字形式，
 * 其余是 well-known 关键字，unknown 表示非法输入。
 */
enum community_token {
	community_token_val,
	community_token_gshut,
	community_token_accept_own,
	community_token_route_filter_translated_v4,
	community_token_route_filter_v4,
	community_token_route_filter_translated_v6,
	community_token_route_filter_v6,
	community_token_llgr_stale,
	community_token_no_llgr,
	community_token_accept_own_nexthop,
	community_token_blackhole,
	community_token_no_export,
	community_token_no_advertise,
	community_token_local_as,
	community_token_no_peer,
	community_token_unknown
};

/* Helper to check if a given community is valid */
/* 【函数】community_valid：校验 "AS:VAL" 形式的字符串。按 ':' 切分，要求至少
 * 两段、每段非空且数值不超过 UINT16_MAX（标准 community 每段 16 位）。
 */
static bool community_valid(const char *community)
{
	int octets = 0;
	char **splits;
	int num;
	int invalid = 0;

	frrstr_split(community, ":", &splits, &num);

	for (int i = 0; i < num; i++) {
		if (strtoul(splits[i], NULL, 10) > UINT16_MAX)
			invalid++;

		if (strlen(splits[i]) == 0)
			invalid++;

		octets++;
		XFREE(MTYPE_TMP, splits[i]);
	}
	XFREE(MTYPE_TMP, splits);

	return (octets < 2 || invalid) ? false : true;
}

/* Get next community token from string. */
/* 【函数】community_gettoken：从字符串头部解析下一个 token。
 *   若以字母开头：匹配 well-known 关键字（graceful-shutdown、no-export 等）；
 *   若以数字开头：解析 "AS:VAL"，先 community_valid 校验，再手工累加两段 16 位
 *     数值（前半段左移 16 得到 community_high，后半段得到 community_low）。
 * 返回指向剩余字符串的指针（NULL 表示结束），并通过 token / val 输出结果。
 */
static const char *
community_gettoken(const char *buf, enum community_token *token, uint32_t *val)
{
	const char *p = buf;

	/* Skip white space. */
	while (isspace((unsigned char)*p))
		p++;

	/* Check the end of the line. */
	if (*p == '\0')
		return NULL;

	/* Well known community string check. */
	if (isalpha((unsigned char)*p)) {
		if (strncmp(p, "graceful-shutdown", strlen("graceful-shutdown"))
		    == 0) {
			*val = COMMUNITY_GSHUT;
			*token = community_token_gshut;
			p += strlen("graceful-shutdown");
			return p;
		}
		if (strncmp(p, "accept-own-nexthop",
			    strlen("accept-own-nexthop"))
		    == 0) {
			*val = COMMUNITY_ACCEPT_OWN_NEXTHOP;
			*token = community_token_accept_own_nexthop;
			p += strlen("accept-own-nexthop");
			return p;
		}
		if (strncmp(p, "accept-own", strlen("accept-own"))
		    == 0) {
			*val = COMMUNITY_ACCEPT_OWN;
			*token = community_token_accept_own;
			p += strlen("accept-own");
			return p;
		}
		if (strncmp(p, "route-filter-translated-v4",
			strlen("route-filter-translated-v4"))
		    == 0) {
			*val = COMMUNITY_ROUTE_FILTER_TRANSLATED_v4;
			*token = community_token_route_filter_translated_v4;
			p += strlen("route-filter-translated-v4");
			return p;
		}
		if (strncmp(p, "route-filter-v4", strlen("route-filter-v4"))
		    == 0) {
			*val = COMMUNITY_ROUTE_FILTER_v4;
			*token = community_token_route_filter_v4;
			p += strlen("route-filter-v4");
			return p;
		}
		if (strncmp(p, "route-filter-translated-v6",
			strlen("route-filter-translated-v6"))
		    == 0) {
			*val = COMMUNITY_ROUTE_FILTER_TRANSLATED_v6;
			*token = community_token_route_filter_translated_v6;
			p += strlen("route-filter-translated-v6");
			return p;
		}
		if (strncmp(p, "route-filter-v6", strlen("route-filter-v6"))
		    == 0) {
			*val = COMMUNITY_ROUTE_FILTER_v6;
			*token = community_token_route_filter_v6;
			p += strlen("route-filter-v6");
			return p;
		}
		if (strncmp(p, "llgr-stale", strlen("llgr-stale"))
		    == 0) {
			*val = COMMUNITY_LLGR_STALE;
			*token = community_token_llgr_stale;
			p += strlen("llgr-stale");
			return p;
		}
		if (strncmp(p, "no-llgr", strlen("no-llgr"))
		    == 0) {
			*val = COMMUNITY_NO_LLGR;
			*token = community_token_no_llgr;
			p += strlen("no-llgr");
			return p;
		}
		if (strncmp(p, "blackhole", strlen("blackhole"))
		    == 0) {
			*val = COMMUNITY_BLACKHOLE;
			*token = community_token_blackhole;
			p += strlen("blackhole");
			return p;
		}
		if (strncmp(p, "no-export", strlen("no-export")) == 0) {
			*val = COMMUNITY_NO_EXPORT;
			*token = community_token_no_export;
			p += strlen("no-export");
			return p;
		}
		if (strncmp(p, "no-advertise", strlen("no-advertise")) == 0) {
			*val = COMMUNITY_NO_ADVERTISE;
			*token = community_token_no_advertise;
			p += strlen("no-advertise");
			return p;
		}
		if (strncmp(p, "local-AS", strlen("local-AS")) == 0) {
			*val = COMMUNITY_LOCAL_AS;
			*token = community_token_local_as;
			p += strlen("local-AS");
			return p;
		}
		if (strncmp(p, "no-peer", strlen("no-peer")) == 0) {
			*val = COMMUNITY_NO_PEER;
			*token = community_token_no_peer;
			p += strlen("no-peer");
			return p;
		}

		/* Unknown string. */
		*token = community_token_unknown;
		return NULL;
	}

	/* Community value. */
	if (isdigit((unsigned char)*p)) {
		int separator = 0;
		int digit = 0;
		uint32_t community_low = 0;
		uint32_t community_high = 0;

		if (!community_valid(p)) {
			*token = community_token_unknown;
			return NULL;
		}

		while (isdigit((unsigned char)*p) || *p == ':') {
			if (*p == ':') {
				if (separator) {
					*token = community_token_unknown;
					return NULL;
				} else {
					separator = 1;
					digit = 0;

					if (community_low > UINT16_MAX) {
						*token =
							community_token_unknown;
						return NULL;
					}

					community_high = community_low << 16;
					community_low = 0;
				}
			} else {
				digit = 1;
				community_low *= 10;
				community_low += (*p - '0');
			}
			p++;
		}
		if (!digit) {
			*token = community_token_unknown;
			return NULL;
		}

		*val = community_high + community_low;
		*token = community_token_val;
		return p;
	}
	*token = community_token_unknown;
	return NULL;
}

/* convert string to community structure */
/* 【函数】community_str2com：解析 "65001:100 no-export ..." 形式的字符串，
 * 循环调用 community_gettoken 收集所有值，最后 uniq_sort 去重排序。
 * 返回的是未驻留的新对象（refcnt 为 0），通常由调用方交给 intern。
 * 遇到未知 token 返回 NULL。
 */
struct community *community_str2com(const char *str)
{
	struct community *com = NULL;
	struct community *com_sort = NULL;
	uint32_t val = 0;
	enum community_token token = community_token_unknown;

	do {
		str = community_gettoken(str, &token, &val);

		switch (token) {
		case community_token_val:
		case community_token_gshut:
		case community_token_accept_own:
		case community_token_route_filter_translated_v4:
		case community_token_route_filter_v4:
		case community_token_route_filter_translated_v6:
		case community_token_route_filter_v6:
		case community_token_llgr_stale:
		case community_token_no_llgr:
		case community_token_accept_own_nexthop:
		case community_token_blackhole:
		case community_token_no_export:
		case community_token_no_advertise:
		case community_token_local_as:
		case community_token_no_peer:
			if (com == NULL) {
				com = community_new();
				com->json = NULL;
			}
			community_add_val(com, val);
			break;
		case community_token_unknown:
			if (com)
				community_free(&com);
			return NULL;
		}
	} while (str);

	com_sort = community_uniq_sort(com);
	community_free(&com);

	return com_sort;
}

/* Return communities hash entry count.  */
/* 【函数】community_count：返回全局哈希中已驻留的集合个数（调试 / show 用）。 */
unsigned long community_count(void)
{
	return comhash->count;
}

/* Return communities hash.  */
/* 【函数】community_hash：返回全局哈希表指针，供只读遍历（如 show 命令）。 */
struct hash *community_hash(void)
{
	return comhash;
}

/* Initialize community related hash. */
/* 【函数】community_init：创建全局哈希表 comhash（进程启动时调用一次）。 */
void community_init(void)
{
	comhash =
		hash_create((unsigned int (*)(const void *))community_hash_make,
			    (bool (*)(const void *, const void *))community_cmp,
			    "BGP Community Hash");
}

/* 【函数】community_hash_free：哈希销毁时的回调，释放每个桶中的数据。 */
static void community_hash_free(void *data)
{
	struct community *com = data;

	community_free(&com);
}

/* 【函数】community_finish：清空并销毁全局哈希表（进程退出时调用）。 */
void community_finish(void)
{
	hash_clean_and_free(&comhash, community_hash_free);
}

/* 【函数】bgp_aggr_community_lookup：在聚合器的 community 哈希中查找。 */
static struct community *bgp_aggr_community_lookup(
						struct bgp_aggregate *aggregate,
						struct community *community)
{
	return hash_lookup(aggregate->community_hash, community);
}

/* 【函数】bgp_aggr_community_hash_alloc：哈希插入时的分配回调，返回一份深拷贝。 */
static void *bgp_aggr_community_hash_alloc(void *p)
{
	struct community *ref = (struct community *)p;
	struct community *community = NULL;

	community = community_dup(ref);
	return community;
}

/* 【函数】bgp_aggr_community_prepare：哈希遍历回调，把每个桶的集合合并进
 * 聚合器的 community（首见时复制，其后追加合并）。
 */
static void bgp_aggr_community_prepare(struct hash_bucket *hb, void *arg)
{
	struct community *hb_community = hb->data;
	struct community **aggr_community = arg;

	if (*aggr_community)
		*aggr_community = community_merge(*aggr_community,
						  hb_community);
	else
		*aggr_community = community_dup(hb_community);
}

/* 【函数】bgp_aggr_community_remove：释放聚合器持有的 community。 */
void bgp_aggr_community_remove(void *arg)
{
	struct community *community = arg;

	community_free(&community);
}

/* 【函数】bgp_compute_aggregate_community：把某条路由的 community 计入聚合器，
 * 然后重新计算聚合后的汇总 community。
 */
void bgp_compute_aggregate_community(struct bgp_aggregate *aggregate,
				     struct community *community)
{
	bgp_compute_aggregate_community_hash(aggregate, community);
	bgp_compute_aggregate_community_val(aggregate);
}


/* 【函数】bgp_compute_aggregate_community_hash：把 community 计入聚合器哈希：
 * 按需创建哈希；若该集合已在哈希中则 refcnt 加一，否则插入一份深拷贝并加一。
 */
void bgp_compute_aggregate_community_hash(struct bgp_aggregate *aggregate,
					  struct community *community)
{
	struct community *aggr_community = NULL;

	if ((aggregate == NULL) || (community == NULL))
		return;

	/* Create hash if not already created.
	 */
	if (aggregate->community_hash == NULL)
		aggregate->community_hash = hash_create(
			(unsigned int (*)(const void *))community_hash_make,
			(bool (*)(const void *, const void *))community_cmp,
			"BGP Aggregator community hash");

	aggr_community = bgp_aggr_community_lookup(aggregate, community);
	if (aggr_community == NULL) {
		/* Insert community into hash.
		 */
		aggr_community = hash_get(aggregate->community_hash, community,
					  bgp_aggr_community_hash_alloc);
	}

	/* Increment reference counter.
	 */
	aggr_community->refcnt++;
}

/* 【函数】bgp_compute_aggregate_community_val：遍历聚合器哈希，合并出全部
 * community，去重排序后存入 aggregate->community。
 */
void bgp_compute_aggregate_community_val(struct bgp_aggregate *aggregate)
{
	struct community *commerge = NULL;

	if (aggregate == NULL)
		return;

	/* Re-compute aggregate's community.
	 */
	if (aggregate->community)
		community_free(&aggregate->community);
	if (aggregate->community_hash &&
	    aggregate->community_hash->count) {
		hash_iterate(aggregate->community_hash,
			     bgp_aggr_community_prepare,
			     &aggregate->community);
		commerge = aggregate->community;
		aggregate->community = community_uniq_sort(commerge);
		if (commerge)
			community_free(&commerge);
	}
}



/* 【函数】bgp_remove_community_from_aggregate：从聚合器减记一份 community
 * 引用；refcnt 减到 0 则摘除并释放，并重算汇总 community。
 */
void bgp_remove_community_from_aggregate(struct bgp_aggregate *aggregate,
					 struct community *community)
{
	struct community *aggr_community = NULL;
	struct community *ret_comm = NULL;

	if ((!aggregate)
	    || (!aggregate->community_hash)
	    || (!community))
		return;

	/* Look-up the community in the hash.
	 */
	aggr_community = bgp_aggr_community_lookup(aggregate, community);
	if (aggr_community) {
		aggr_community->refcnt--;

		if (aggr_community->refcnt == 0) {
			ret_comm = hash_release(aggregate->community_hash,
						aggr_community);
			community_free(&ret_comm);

			bgp_compute_aggregate_community_val(aggregate);
		}
	}
}

/* 【函数】bgp_remove_comm_from_aggregate_hash：仅从聚合器哈希中减记引用
 * （refcnt 归零则释放），与上一个函数的区别是不重算汇总 community。
 */
void bgp_remove_comm_from_aggregate_hash(struct bgp_aggregate *aggregate,
		struct community *community)
{

	struct community *aggr_community = NULL;
	struct community *ret_comm = NULL;

	if ((!aggregate)
	    || (!aggregate->community_hash)
	    || (!community))
		return;

	/* Look-up the community in the hash.
	 */
	aggr_community = bgp_aggr_community_lookup(aggregate, community);
	if (aggr_community) {
		aggr_community->refcnt--;

		if (aggr_community->refcnt == 0) {
			ret_comm = hash_release(aggregate->community_hash,
						aggr_community);
			community_free(&ret_comm);
		}
	}
}
