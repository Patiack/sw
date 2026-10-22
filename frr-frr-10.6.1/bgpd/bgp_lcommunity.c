// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Large Communities Attribute
 *
 * Copyright (C) 2016 Keyur Patel <keyur@arrcus.com>
 */

/* 【文件导读】 bgp_lcommunity.c —— Large Community 属性的实现
 *
 * 一、本文件做什么
 *    实现 bgp_lcommunity.h 声明的全部操作：分配 / 释放、排序去重、字符串与
 *    二进制互转、全局哈希驻留（intern）与引用计数、集合运算，以及聚合器相关
 *    函数。
 *
 * 二、核心设计：驻留 + 引用计数
 *    lcomhash 是全局哈希表。内容相同的 large community 集合在进程内只保留一份
 *    实例，用 refcnt 共享；parse / str2com / intern 拿到的都是共享指针。
 *
 * 三、线格式与内存格式
 *    每个 large community 固定 12 字节 = 三个大端 32 位整数
 *    （Global:Local1:Local2）。内存里 val 就是连续的 12 字节块，与线格式一致，
 *    故 memcmp 判等、字节序天然按数值排序。
 *
 * 四、字符串与返回约定
 *    lcommunity_str 返回对象内部缓存的字符串，调用者不得 free；
 *    lcommunity_str2com 返回未驻留的新对象；
 *    set_lcommunity_string 为别名预留了扩容逻辑（别名可能比默认格式更长）。
 *
 * 五、推荐阅读顺序
 *    lcommunity_new / free -> lcommunity_add_val -> lcommunity_uniq_sort
 *    -> lcommunity_intern / unintern -> set_lcommunity_string / lcommunity_str
 *    -> lcommunity_gettoken / lcommunity_str2com -> 末尾 aggregate 系列。
 */

#include <zebra.h>

#include "hash.h"
#include "memory.h"
#include "prefix.h"
#include "command.h"
#include "filter.h"
#include "jhash.h"
#include "stream.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_community_alias.h"
#include "bgpd/bgp_aspath.h"

/* Hash of community attribute. */
static struct hash *lcomhash;

/* Allocate a new lcommunities.  */
/* 【函数】lcommunity_new：分配一个空的 lcommunity（val / str / json 均未分配）。 */
static struct lcommunity *lcommunity_new(void)
{
	return XCALLOC(MTYPE_LCOMMUNITY, sizeof(struct lcommunity));
}

/* Allocate lcommunities.  */
/* 【函数】lcommunity_free：释放 lcommunity 及其内部 val / str / json。
 * 疑点：原英文注释写作 Allocate 但函数实际是释放，应为笔误。
 */
void lcommunity_free(struct lcommunity **lcom)
{
	if (!(*lcom))
		return;

	XFREE(MTYPE_LCOMMUNITY_VAL, (*lcom)->val);
	XFREE(MTYPE_LCOMMUNITY_STR, (*lcom)->str);
	if ((*lcom)->json)
		json_object_free((*lcom)->json);
	XFREE(MTYPE_LCOMMUNITY, *lcom);
}

/* 【函数】lcommunity_hash_free：哈希销毁回调，释放每个 lcommunity。 */
static void lcommunity_hash_free(struct lcommunity *lcom)
{
	lcommunity_free(&lcom);
}

/* Add a new Large Communities value to Large Communities
   Attribute structure.  When the value is already exists in the
   structure, we don't add the value.  Newly added value is sorted by
   numerical order.  When the value is added to the structure return 1
   else return 0.  */
/* 【函数】lcommunity_add_val：把单个 12 字节值插入集合，保持按数值升序且去重。
 * 已有相同值则返回 false 不插入；否则在正确位置插入并返回 true。
 */
static bool lcommunity_add_val(struct lcommunity *lcom,
			       struct lcommunity_val *lval)
{
	uint8_t *p;
	int ret;
	int c;

	/* When this is fist value, just add it.  */
	if (lcom->val == NULL) {
		lcom->size++;
		lcom->val = XMALLOC(MTYPE_LCOMMUNITY_VAL, lcom_length(lcom));
		memcpy(lcom->val, lval->val, LCOMMUNITY_SIZE);
		return true;
	}

	/* If the value already exists in the structure return 0.  */
	c = 0;
	for (p = lcom->val; c < lcom->size; p += LCOMMUNITY_SIZE, c++) {
		ret = memcmp(p, lval->val, LCOMMUNITY_SIZE);
		if (ret == 0)
			return false;
		if (ret > 0)
			break;
	}

	/* Add the value to the structure with numerical sorting.  */
	lcom->size++;
	lcom->val =
		XREALLOC(MTYPE_LCOMMUNITY_VAL, lcom->val, lcom_length(lcom));

	memmove(lcom->val + (c + 1) * LCOMMUNITY_SIZE,
		lcom->val + c * LCOMMUNITY_SIZE,
		(lcom->size - 1 - c) * LCOMMUNITY_SIZE);
	memcpy(lcom->val + c * LCOMMUNITY_SIZE, lval->val, LCOMMUNITY_SIZE);

	return true;
}

/* This function takes pointer to Large Communities structure then
   create a new Large Communities structure by uniq and sort each
   Large Communities value.  */
/* 【函数】lcommunity_uniq_sort：返回新集合，值已去重升序。内部逐个调用
 * lcommunity_add_val 完成插入排序。不驻留。
 */
struct lcommunity *lcommunity_uniq_sort(struct lcommunity *lcom)
{
	int i;
	struct lcommunity *new;
	struct lcommunity_val *lval;

	if (!lcom)
		return NULL;

	new = lcommunity_new();

	for (i = 0; i < lcom->size; i++) {
		lval = (struct lcommunity_val *)(lcom->val
						 + (i * LCOMMUNITY_SIZE));
		lcommunity_add_val(new, lval);
	}
	return new;
}

/* Parse Large Communities Attribute in BGP packet.  */
/* 【函数】lcommunity_parse：从报文解析。length 必须是 12 的倍数，否则返回 NULL。
 * 去重排序后 intern 成共享对象返回。
 */
struct lcommunity *lcommunity_parse(uint8_t *pnt, unsigned short length)
{
	struct lcommunity tmp;
	struct lcommunity *new;

	/* Length check.  */
	if (length % LCOMMUNITY_SIZE)
		return NULL;

	/* Prepare tmporary structure for making a new Large Communities
	   Attribute.  */
	tmp.size = length / LCOMMUNITY_SIZE;
	tmp.val = pnt;

	/* Create a new Large Communities Attribute by uniq and sort each
	   Large Communities value  */
	new = lcommunity_uniq_sort(&tmp);

	return lcommunity_intern(new);
}

/* Duplicate the Large Communities Attribute structure.  */
/* 【函数】lcommunity_dup：深拷贝，refcnt 归零，不驻留。 */
struct lcommunity *lcommunity_dup(struct lcommunity *lcom)
{
	struct lcommunity *new;

	new = lcommunity_new();
	new->size = lcom->size;
	if (new->size) {
		new->val = XMALLOC(MTYPE_LCOMMUNITY_VAL, lcom_length(lcom));
		memcpy(new->val, lcom->val, lcom_length(lcom));
	} else
		new->val = NULL;
	return new;
}

/* Merge two Large Communities Attribute structure.  */
/* 【函数】lcommunity_merge：把 lcom2 追加到 lcom1 末尾（原地扩展，不去重）。 */
struct lcommunity *lcommunity_merge(struct lcommunity *lcom1,
				    struct lcommunity *lcom2)
{
	lcom1->val = XREALLOC(MTYPE_LCOMMUNITY_VAL, lcom1->val,
			      lcom_length(lcom1) + lcom_length(lcom2));

	memcpy(lcom1->val + lcom_length(lcom1), lcom2->val, lcom_length(lcom2));
	lcom1->size += lcom2->size;

	return lcom1;
}

/* 【函数】set_lcommunity_string：把集合渲染为字符串缓存到 lcom->str，make_json
 * 为真时同时构造 lcom->json。每个值格式为 "Global:Local1:Local2"；translate_alias
 * 为真时用别名替换。因别名可能比默认长度更长，缓冲区不足时会 XREALLOC 扩容。
 */
static void set_lcommunity_string(struct lcommunity *lcom, bool make_json,
				  bool translate_alias)
{
	int i;
	int len;
	char *str_buf;
	const uint8_t *pnt;
	uint32_t global, local1, local2;
	json_object *json_lcommunity_list = NULL;
	json_object *json_string = NULL;

	/* 3 32-bit integers, 2 colons, and a space */
#define LCOMMUNITY_STRLEN (10 * 3 + 2 + 1)

	if (!lcom)
		return;

	if (make_json) {
		lcom->json = json_object_new_object();
		json_lcommunity_list = json_object_new_array();
	}

	if (lcom->size == 0) {
		str_buf = XCALLOC(MTYPE_LCOMMUNITY_STR, 1);

		if (make_json) {
			json_object_string_add(lcom->json, "string", "");
			json_object_object_add(lcom->json, "list",
					       json_lcommunity_list);
		}

		lcom->str = str_buf;
		return;
	}

	/* 1 space + lcom->size lcom strings + null terminator */
	size_t str_buf_sz = (LCOMMUNITY_STRLEN * lcom->size) + 2;
	str_buf = XCALLOC(MTYPE_LCOMMUNITY_STR, str_buf_sz);

	len = 0;
	for (i = 0; i < lcom->size; i++) {
		if (i > 0)
			len = strlcat(str_buf, " ", str_buf_sz);

		pnt = lcom->val + (i * LCOMMUNITY_SIZE);
		pnt = ptr_get_be32(pnt, &global);
		pnt = ptr_get_be32(pnt, &local1);
		pnt = ptr_get_be32(pnt, &local2);
		(void)pnt;

		char lcsb[LCOMMUNITY_STRLEN + 1];

		snprintf(lcsb, sizeof(lcsb), "%u:%u:%u", global, local1,
			 local2);

		/*
		 * Aliases can cause havoc, if the alias length is greater
		 * than the LCOMMUNITY_STRLEN for a particular item
		 * then we need to realloc the memory associated
		 * with the string so that it can fit
		 */
		const char *com2alias =
			translate_alias ? bgp_community2alias(lcsb) : lcsb;
		size_t individual_len = strlen(com2alias);
		if (individual_len + len > str_buf_sz) {
			str_buf_sz = individual_len + len + 1;
			str_buf = XREALLOC(MTYPE_LCOMMUNITY_STR, str_buf,
					   str_buf_sz);
		}

		len = strlcat(str_buf, com2alias, str_buf_sz);

		if (make_json) {
			json_string = json_object_new_string(com2alias);
			json_object_array_add(json_lcommunity_list,
					      json_string);
		}
	}

	if (make_json) {
		json_object_string_add(lcom->json, "string", str_buf);
		json_object_object_add(lcom->json, "list",
				       json_lcommunity_list);
	}

	lcom->str = str_buf;
}

/* Intern Large Communities Attribute.  */
/* 【函数】lcommunity_intern：驻留。与 community_intern 同构：命中已存者则释放
 * 传入对象、返回已有者并 refcnt 加一；首个快照会生成 str。
 */
struct lcommunity *lcommunity_intern(struct lcommunity *lcom)
{
	struct lcommunity *find;

	assert(lcom->refcnt == 0);

	find = (struct lcommunity *)hash_get(lcomhash, lcom, hash_alloc_intern);

	if (find != lcom)
		lcommunity_free(&lcom);

	find->refcnt++;

	if (!find->str)
		set_lcommunity_string(find, false, true);

	return find;
}

/* Unintern Large Communities Attribute.  */
/* 【函数】lcommunity_unintern：refcnt 减一，减到 0 从哈希摘除并释放，指针置 NULL。 */
void lcommunity_unintern(struct lcommunity **lcom)
{
	struct lcommunity *ret;

	if (!*lcom)
		return;

	if ((*lcom)->refcnt)
		(*lcom)->refcnt--;

	/* Pull off from hash.  */
	if ((*lcom)->refcnt == 0) {
		/* Large community must be in the hash.  */
		ret = (struct lcommunity *)hash_release(lcomhash, *lcom);
		assert(ret != NULL);

		lcommunity_free(lcom);
	}
}

/* Return string representation of lcommunities attribute. */
/* 【函数】lcommunity_str：返回 lcom->str（内部缓存），必要时惰性生成；调用者
 * 不得 free。
 */
char *lcommunity_str(struct lcommunity *lcom, bool make_json,
		     bool translate_alias)
{
	if (!lcom)
		return NULL;

	if (make_json && !lcom->json && lcom->str)
		XFREE(MTYPE_LCOMMUNITY_STR, lcom->str);

	if (!lcom->str)
		set_lcommunity_string(lcom, make_json, translate_alias);

	return lcom->str;
}

/* Utility function to make hash key.  */
/* 【函数】lcommunity_hash_make：对 val 的全部字节做 jhash 得到哈希键。 */
unsigned int lcommunity_hash_make(const void *arg)
{
	const struct lcommunity *lcom = arg;
	int size = lcom_length(lcom);

	return jhash(lcom->val, size, 0xab125423);
}

/* Compare two Large Communities Attribute structure.  */
/* 【函数】lcommunity_cmp：全等比较（size 相同且字节全等）。哈希 key 比较回调。 */
bool lcommunity_cmp(const void *arg1, const void *arg2)
{
	const struct lcommunity *lcom1 = arg1;
	const struct lcommunity *lcom2 = arg2;

	if (lcom1 == NULL && lcom2 == NULL)
		return true;

	if (lcom1 == NULL || lcom2 == NULL)
		return false;

	return (lcom1->size == lcom2->size
		&& memcmp(lcom1->val, lcom2->val, lcom_length(lcom1)) == 0);
}

/* Return communities hash.  */
/* 【函数】lcommunity_hash：返回全局哈希表指针，供只读遍历。 */
struct hash *lcommunity_hash(void)
{
	return lcomhash;
}

/* Initialize Large Comminities related hash. */
/* 【函数】lcommunity_init：创建全局哈希表 lcomhash。 */
void lcommunity_init(void)
{
	lcomhash = hash_create(lcommunity_hash_make, lcommunity_cmp,
			       "BGP lcommunity hash");
}

/* 【函数】lcommunity_finish：清空并销毁全局哈希表。 */
void lcommunity_finish(void)
{
	hash_clean_and_free(&lcomhash, (void (*)(void *))lcommunity_hash_free);
}

/* Get next Large Communities token from the string.
 * Assumes str is space-delimeted and describes 0 or more
 * valid large communities
 */
/* 【函数】lcommunity_gettoken：解析 "Global:Local1:Local2" 形式的下一个值。
 * 手工按 ':' 切分三个 32 位字段，再逐字节以大端写入 lval。前置断言必须恰好有
 * 两个分隔符且末段含数字。
 */
static const char *lcommunity_gettoken(const char *str,
				       struct lcommunity_val *lval)
{
	const char *p = str;

	/* Skip white space. */
	while (isspace((unsigned char)*p)) {
		p++;
		str++;
	}

	/* Check the end of the line. */
	if (*p == '\0')
		return NULL;

	/* Community value. */
	int separator = 0;
	int digit = 0;
	uint32_t globaladmin = 0;
	uint32_t localdata1 = 0;
	uint32_t localdata2 = 0;

	while (*p && *p != ' ') {
		/* large community valid chars */
		assert(isdigit((unsigned char)*p) || *p == ':');

		if (*p == ':') {
			separator++;
			digit = 0;
			if (separator == 1) {
				globaladmin = localdata2;
			} else {
				localdata1 = localdata2;
			}
			localdata2 = 0;
		} else {
			digit = 1;
			/* left shift the accumulated value and add current
			 * digit
			 */
			localdata2 *= 10;
			localdata2 += (*p - '0');
		}
		p++;
	}

	/* Assert str was a valid large community */
	assert(separator == 2 && digit == 1);

	/*
	 * Copy the large comm.
	 */
	lval->val[0] = (globaladmin >> 24) & 0xff;
	lval->val[1] = (globaladmin >> 16) & 0xff;
	lval->val[2] = (globaladmin >> 8) & 0xff;
	lval->val[3] = globaladmin & 0xff;
	lval->val[4] = (localdata1 >> 24) & 0xff;
	lval->val[5] = (localdata1 >> 16) & 0xff;
	lval->val[6] = (localdata1 >> 8) & 0xff;
	lval->val[7] = localdata1 & 0xff;
	lval->val[8] = (localdata2 >> 24) & 0xff;
	lval->val[9] = (localdata2 >> 16) & 0xff;
	lval->val[10] = (localdata2 >> 8) & 0xff;
	lval->val[11] = localdata2 & 0xff;

	return p;
}

/*
  Convert string to large community attribute.
  When type is already known, please specify both str and type.

  When string includes keyword for each large community value.
  Please specify keyword_included as non-zero value.
*/
/* 【函数】lcommunity_str2com：把空格分隔的若干 "G:L1:L2" 解析为集合。
 * 先 lcommunity_list_valid 校验，再循环 gettoken 收集，逐个 add_val（已排序去重）。
 * 返回未驻留的新对象。
 */
struct lcommunity *lcommunity_str2com(const char *str)
{
	struct lcommunity *lcom = NULL;
	struct lcommunity_val lval;

	if (!lcommunity_list_valid(str, LARGE_COMMUNITY_LIST_STANDARD))
		return NULL;

	do {
		str = lcommunity_gettoken(str, &lval);
		if (lcom == NULL)
			lcom = lcommunity_new();
		lcommunity_add_val(lcom, &lval);
	} while (str);

	return lcom;
}

/* 【函数】lcommunity_include：判断 12 字节原始值 ptr 是否在集合中（逐项 memcmp）。 */
bool lcommunity_include(struct lcommunity *lcom, uint8_t *ptr)
{
	int i;
	uint8_t *lcom_ptr;

	for (i = 0; i < lcom->size; i++) {
		lcom_ptr = lcom->val + (i * LCOMMUNITY_SIZE);
		if (memcmp(ptr, lcom_ptr, LCOMMUNITY_SIZE) == 0)
			return true;
	}
	return false;
}

/* 【函数】lcommunity_match：判断 lcom2 是否为 lcom1 的子集。归并式遍历实现。 */
bool lcommunity_match(const struct lcommunity *lcom1,
		      const struct lcommunity *lcom2)
{
	int i = 0;
	int j = 0;

	if (lcom1 == NULL && lcom2 == NULL)
		return true;

	if (lcom1 == NULL || lcom2 == NULL)
		return false;

	if (lcom1->size < lcom2->size)
		return false;

	/* Every community on com2 needs to be on com1 for this to match */
	while (i < lcom1->size && j < lcom2->size) {
		if (memcmp(lcom1->val + (i * LCOMMUNITY_SIZE),
			   lcom2->val + (j * LCOMMUNITY_SIZE), LCOMMUNITY_SIZE)
		    == 0)
			j++;
		i++;
	}

	if (j == lcom2->size)
		return true;
	else
		return false;
}

/* Delete one lcommunity. */
/* 【函数】lcommunity_del_val：删除与 ptr 匹配的一个 12 字节值，前移后续元素并
 * 收缩分配；删空则释放 val。
 */
void lcommunity_del_val(struct lcommunity *lcom, uint8_t *ptr)
{
	int i = 0;
	int c = 0;

	if (!lcom->val)
		return;

	while (i < lcom->size) {
		if (memcmp(lcom->val + i * LCOMMUNITY_SIZE, ptr,
			   LCOMMUNITY_SIZE)
		    == 0) {
			c = lcom->size - i - 1;

			if (c > 0)
				memmove(lcom->val + i * LCOMMUNITY_SIZE,
					lcom->val + (i + 1) * LCOMMUNITY_SIZE,
					c * LCOMMUNITY_SIZE);

			lcom->size--;

			if (lcom->size > 0)
				lcom->val =
					XREALLOC(MTYPE_LCOMMUNITY_VAL,
						 lcom->val, lcom_length(lcom));
			else {
				XFREE(MTYPE_LCOMMUNITY_VAL, lcom->val);
			}
			return;
		}
		i++;
	}
}

/* 【函数】bgp_aggr_lcommunity_lookup：在聚合器的 lcommunity 哈希中查找。 */
static struct lcommunity *bgp_aggr_lcommunity_lookup(
						struct bgp_aggregate *aggregate,
						struct lcommunity *lcommunity)
{
	return hash_lookup(aggregate->lcommunity_hash, lcommunity);
}

/* 【函数】bgp_aggr_lcommunty_hash_alloc：哈希插入分配回调，返回深拷贝。
 * 注：函数名 lcommunty 少一个 i，属原始拼写笔误，未改动。
 */
static void *bgp_aggr_lcommunty_hash_alloc(void *p)
{
	struct lcommunity *ref = (struct lcommunity *)p;
	struct lcommunity *lcommunity = NULL;

	lcommunity = lcommunity_dup(ref);
	return lcommunity;
}

/* 【函数】bgp_aggr_lcommunity_prepare：哈希遍历回调，把各桶集合合并进聚合器。 */
static void bgp_aggr_lcommunity_prepare(struct hash_bucket *hb, void *arg)
{
	struct lcommunity *hb_lcommunity = hb->data;
	struct lcommunity **aggr_lcommunity = arg;

	if (*aggr_lcommunity)
		*aggr_lcommunity = lcommunity_merge(*aggr_lcommunity,
						    hb_lcommunity);
	else
		*aggr_lcommunity = lcommunity_dup(hb_lcommunity);
}

/* 【函数】bgp_aggr_lcommunity_remove：释放聚合器持有的 lcommunity。 */
void bgp_aggr_lcommunity_remove(void *arg)
{
	struct lcommunity *lcommunity = arg;

	lcommunity_free(&lcommunity);
}

/* 【函数】bgp_compute_aggregate_lcommunity：计入聚合器并重算汇总。 */
void bgp_compute_aggregate_lcommunity(struct bgp_aggregate *aggregate,
				      struct lcommunity *lcommunity)
{

	bgp_compute_aggregate_lcommunity_hash(aggregate, lcommunity);
	bgp_compute_aggregate_lcommunity_val(aggregate);
}

/* 【函数】bgp_compute_aggregate_lcommunity_hash：按需建哈希；已在则 refcnt 加一，
 * 否则插入深拷贝并加一。
 */
void bgp_compute_aggregate_lcommunity_hash(struct bgp_aggregate *aggregate,
					   struct lcommunity *lcommunity)
{

	struct lcommunity *aggr_lcommunity = NULL;

	if ((aggregate == NULL) || (lcommunity == NULL))
		return;

	/* Create hash if not already created.
	 */
	if (aggregate->lcommunity_hash == NULL)
		aggregate->lcommunity_hash = hash_create(
					lcommunity_hash_make, lcommunity_cmp,
					"BGP Aggregator lcommunity hash");

	aggr_lcommunity = bgp_aggr_lcommunity_lookup(aggregate, lcommunity);
	if (aggr_lcommunity == NULL) {
		/* Insert lcommunity into hash.
		 */
		aggr_lcommunity = hash_get(aggregate->lcommunity_hash,
					   lcommunity,
					   bgp_aggr_lcommunty_hash_alloc);
	}

	/* Increment reference counter.
	 */
	aggr_lcommunity->refcnt++;
}

/* 【函数】bgp_compute_aggregate_lcommunity_val：合并哈希中全部集合，去重排序后
 * 存入 aggregate->lcommunity。
 */
void bgp_compute_aggregate_lcommunity_val(struct bgp_aggregate *aggregate)
{
	struct lcommunity *lcommerge = NULL;

	if (aggregate == NULL)
		return;

	/* Re-compute aggregate's lcommunity.
	 */
	if (aggregate->lcommunity)
		lcommunity_free(&aggregate->lcommunity);
	if (aggregate->lcommunity_hash &&
	    aggregate->lcommunity_hash->count) {
		hash_iterate(aggregate->lcommunity_hash,
			     bgp_aggr_lcommunity_prepare,
			     &aggregate->lcommunity);
		lcommerge = aggregate->lcommunity;
		aggregate->lcommunity = lcommunity_uniq_sort(lcommerge);
		if (lcommerge)
			lcommunity_free(&lcommerge);
	}
}

/* 【函数】bgp_remove_lcommunity_from_aggregate：减记引用；归零则摘除释放并重算汇总。 */
void bgp_remove_lcommunity_from_aggregate(struct bgp_aggregate *aggregate,
					  struct lcommunity *lcommunity)
{
	struct lcommunity *aggr_lcommunity = NULL;
	struct lcommunity *ret_lcomm = NULL;

	if ((!aggregate)
	    || (!aggregate->lcommunity_hash)
	    || (!lcommunity))
		return;

	/* Look-up the lcommunity in the hash.
	 */
	aggr_lcommunity = bgp_aggr_lcommunity_lookup(aggregate, lcommunity);
	if (aggr_lcommunity) {
		aggr_lcommunity->refcnt--;

		if (aggr_lcommunity->refcnt == 0) {
			ret_lcomm = hash_release(aggregate->lcommunity_hash,
						 aggr_lcommunity);
			lcommunity_free(&ret_lcomm);

			bgp_compute_aggregate_lcommunity_val(aggregate);

		}
	}
}

/* 【函数】bgp_remove_lcomm_from_aggregate_hash：仅减记引用（归零则释放），不重算汇总。 */
void bgp_remove_lcomm_from_aggregate_hash(struct bgp_aggregate *aggregate,
					  struct lcommunity *lcommunity)
{
	struct lcommunity *aggr_lcommunity = NULL;
	struct lcommunity *ret_lcomm = NULL;

	if ((!aggregate)
	    || (!aggregate->lcommunity_hash)
	    || (!lcommunity))
		return;

	/* Look-up the lcommunity in the hash.
	 */
	aggr_lcommunity = bgp_aggr_lcommunity_lookup(aggregate, lcommunity);
	if (aggr_lcommunity) {
		aggr_lcommunity->refcnt--;

		if (aggr_lcommunity->refcnt == 0) {
			ret_lcomm = hash_release(aggregate->lcommunity_hash,
						 aggr_lcommunity);
			lcommunity_free(&ret_lcomm);
		}
	}
}
