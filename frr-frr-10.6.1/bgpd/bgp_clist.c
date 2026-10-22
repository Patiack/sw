// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP community-list and extcommunity-list.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/* ============================================================================
 * 【文件导读】bgp_clist.c —— community-list（团体过滤器）的实现
 * ============================================================================
 * 【它是什么】bgp_clist.h 里声明的那一堆接口的实现：提供 `ip community-list`、
 * `ip extcommunity-list`、`ip large-community-list` 三套过滤器配置的增删改查与
 * 匹配。route-map 的 `match community / extcommunity / large-community` 最终都会
 * 落到本文件的匹配函数上。术语与常量一律沿用 bgp_clist.h 的中文注释，请对照阅读。
 *
 * 【两类正交维度（头文件已述，此处再点一次）】
 *   种类 MASTER：standard community / extcommunity / large-community 三类，各有
 *       独立的命名空间与匹配函数族，由 master 编号 0 1 2 索引（见头文件宏）。
 *   写法 style：standard（列出具体团体值，要求精确出现）与 expanded（写正则去
 *       匹配团体的字符串形式）。**同一 list 内不允许混用两种写法**，配置时即被
 *       拒绝并返回 STANDARD_CONFLICT / EXPANDED_CONFLICT 错误码。
 *   两个维度相乘即头文件里的 6 个扁平编号（COMMUNITY_LIST_STANDARD 等）。
 *
 * 【命名空间与查找】每个 master 维护 num（数字名 1~500）与 str（字符串名）两张
 * 有序链表，外加一张按名字哈希的 hash。链表保证回显配置时顺序稳定，hash 负责
 * 高频的名字查找；插入时按名字排序以维持链表有序。
 *
 * 【条目 seq 语义】条目按 seq 升序排列，匹配时**第一条命中即定论**（permit 才
 * 放行、deny 即拒绝）。seq 省略时由 bgp_clist_new_seq_get 按“当前最大 seq 向上
 * 取 5 的倍数再加 5”自动分配，留出插入空档。
 *
 * 【返回值陷阱（务必记住）】..._match 系列返回 bool，含义是“命中了列表里某条规则”
 * 并已把它换算成 permit / deny 的结论——即 return true 表示该路由被这条 list 放行；
 * return false 则可能是“没有任何条目命中”，也可能是“命中了 deny 条目”。而
 * ..._any_match / ..._exact_match / ..._match_delete 是语义不同的变体，不可混用，
 * 详见各函数上方注释。
 *
 * 【阅读顺序】seq 与 hash 辅助 → 生命周期 new / free → 插入 / 查找 / 删除 list
 *            与 entry → 团体转字符串 → 正则辅助 → 各匹配函数 → 三套 set / unset
 *            → init / terminate → CLI 补全。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "prefix.h"
#include "memory.h"
#include "queue.h"
#include "filter.h"
#include "stream.h"
#include "frrstr.h"
#include "frregex_real.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_community_alias.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_regex.h"
#include "bgpd/bgp_clist.h"

/* 【函数】为新条目自动分配 seq：扫描全表取最大 seq，向上取 5 的倍数再加 5，
 * 使相邻自动值之间保留空档便于后续手工插入。结果被截断到 UINT_MAX 以防越界。
 * 仅当用户未显式指定 seq（entry->seq == COMMUNITY_SEQ_NUMBER_AUTO）时才调用。 */
/* Calculate new sequential number. */
static int64_t bgp_clist_new_seq_get(struct community_list *list)
{
	int64_t maxseq;
	int64_t newseq;
	struct community_entry *entry;

	maxseq = 0;

	for (entry = list->head; entry; entry = entry->next) {
		if (maxseq < entry->seq)
			maxseq = entry->seq;
	}

	newseq = ((maxseq / 5) * 5) + 5;

	return (newseq > UINT_MAX) ? UINT_MAX : newseq;
}

/* 【函数】线性查找 list 中 seq 与给定值相同的条目；找到返回该条目，否则 NULL。
 * 供新增条目时判断是否要“替换同号条目”。复杂度 O(n)，最坏全表扫描。 */
/* Return community-list entry which has same seq number. */
static struct community_entry *bgp_clist_seq_check(struct community_list *list,
						   int64_t seq)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next)
		if (entry->seq == seq)
			return entry;
	return NULL;
}

/* 【函数】hash 回调：算 community_list 的哈希键。名字哈希结果缓存在 name_hash
 * 字段，已算过即直接返回，避免重复 jhash。注意此缓存“算一次存终身”——若改了
 * 名字必须同步刷新该字段，否则哈希表查找会失效。 */
static uint32_t bgp_clist_hash_key_community_list(const void *data)
{
	struct community_list *cl = (struct community_list *) data;

	if (cl->name_hash)
		return cl->name_hash;

	cl->name_hash = bgp_clist_hash_key(cl->name);
	return cl->name_hash;
}

/* 【函数】hash 回调：判断两个 community_list 是否同一个键（同名）。先比缓存的
 * name_hash，不等则快速返回 false，再 strcmp 名字，两者都相等才命中。 */
static bool bgp_clist_hash_cmp_community_list(const void *a1, const void *a2)
{
	const struct community_list *cl1 = a1;
	const struct community_list *cl2 = a2;

	if (cl1->name_hash != cl2->name_hash)
		return false;

	if (strcmp(cl1->name, cl2->name) == 0)
		return true;

	return false;
}

/* 【函数】按 master 编号取出三类团体容器之一。返回的是 handler 内部的地址，
 * 不可释放。ch 为 NULL 或 master 非法时返回 NULL。这是所有 set / unset / lookup
 * 定位“操作哪一类团体”的统一入口。 */
/* Lookup master structure for community-list or
   extcommunity-list.  */
struct community_list_master *
community_list_master_lookup(struct community_list_handler *ch, int master)
{
	if (ch)
		switch (master) {
		case COMMUNITY_LIST_MASTER:
			return &ch->community_list;
		case EXTCOMMUNITY_LIST_MASTER:
			return &ch->extcommunity_list;
		case LARGE_COMMUNITY_LIST_MASTER:
			return &ch->lcommunity_list;
		}
	return NULL;
}

/* 【函数】分配一个零初始化的 community_entry。注意 memset 后 direct 为 0，
 * 即 COMMUNITY_DENY——调用方必须显式设 direct，否则规则会变成 deny。 */
/* Allocate a new community list entry.  */
static struct community_entry *community_entry_new(void)
{
	return XCALLOC(MTYPE_COMMUNITY_LIST_ENTRY,
		       sizeof(struct community_entry));
}

/* 【函数】释放一条规则，按 style 分别释放 union 里对应的团体对象或正则对象，
 * 并释放 config 字符串（注意不同 style 用的 config 内存类型不同：standard 的
 * extcommunity 用 MTYPE_ECOMMUNITY_STR，expanded 用 MTYPE_COMMUNITY_LIST_CONFIG）。
 * 最后释放条目本身。必须先 free 内部数据再 free 结构体。 */
/* Free community list entry.  */
static void community_entry_free(struct community_entry *entry)
{
	switch (entry->style) {
	case COMMUNITY_LIST_STANDARD:
		if (entry->u.com)
			community_free(&entry->u.com);
		break;
	case LARGE_COMMUNITY_LIST_STANDARD:
		if (entry->u.lcom)
			lcommunity_free(&entry->u.lcom);
		break;
	case EXTCOMMUNITY_LIST_STANDARD:
		/* In case of standard extcommunity-list, configuration string
		   is made by ecommunity_ecom2str().  */
		XFREE(MTYPE_ECOMMUNITY_STR, entry->config);
		if (entry->u.ecom)
			ecommunity_free(&entry->u.ecom);
		break;
	case COMMUNITY_LIST_EXPANDED:
	case EXTCOMMUNITY_LIST_EXPANDED:
	case LARGE_COMMUNITY_LIST_EXPANDED:
		XFREE(MTYPE_COMMUNITY_LIST_CONFIG, entry->config);
		if (entry->reg)
			bgp_regex_free(entry->reg);
		break;
	default:
		break;
	}
	XFREE(MTYPE_COMMUNITY_LIST_ENTRY, entry);
}

/* 【函数】分配一个零初始化的 community_list（一个命名的过滤器）。 */
/* Allocate a new community-list.  */
static struct community_list *community_list_new(void)
{
	return XCALLOC(MTYPE_COMMUNITY_LIST, sizeof(struct community_list));
}

/* 【函数】释放一个 community_list 结构本身（名字 + 结构体）。注意它**不**负责
 * 释放链表里的规则条目，那些由 community_list_delete 先行处理。 */
/* Free community-list.  */
static void community_list_free(struct community_list *list)
{
	XFREE(MTYPE_COMMUNITY_LIST_NAME, list->name);
	XFREE(MTYPE_COMMUNITY_LIST, list);
}

/* 【函数】新建一个 community_list 并插入对应 master。先判断名字是否“全数字且
 * 不超过 500”——是则归入 num 链表（sort = NUMBER），否则归入 str 链表
 * （sort = STRING）；两个命名空间互不相干。同时把新对象放进 hash 便于后续查找。
 * 链表按名字排序插入（数字名按数值比较、字符串名按字典序），以保证回显顺序稳定。
 * 返回新建的 list。 */
static struct community_list *
community_list_insert(struct community_list_handler *ch, const char *name,
		      int master)
{
	size_t i;
	long number;
	struct community_list *new;
	struct community_list *point;
	struct community_list_list *list;
	struct community_list_master *cm;

	/* Lookup community-list master.  */
	cm = community_list_master_lookup(ch, master);
	if (!cm)
		return NULL;

	/* Allocate new community_list and copy given name. */
	new = community_list_new();
	new->name = XSTRDUP(MTYPE_COMMUNITY_LIST_NAME, name);
	new->name_hash = bgp_clist_hash_key_community_list(new);

	/* Save for later */
	(void)hash_get(cm->hash, new, hash_alloc_intern);

	/* If name is made by all digit character.  We treat it as
	   number. */
	for (number = 0, i = 0; i < strlen(name); i++) {
		if (isdigit((unsigned char)name[i]))
			number = (number * 10) + (name[i] - '0');
		else
			break;
	}

	/* In case of name is all digit character */
	if (i == strlen(name) && number <= COMMUNITY_LIST_NUMBER_MAX) {
		new->sort = COMMUNITY_LIST_NUMBER;

		/* Set access_list to number list. */
		list = &cm->num;

		for (point = list->head; point; point = point->next)
			if (atol(point->name) >= number)
				break;
	} else {
		new->sort = COMMUNITY_LIST_STRING;

		/* Set access_list to string list. */
		list = &cm->str;

		/* Set point to insertion point. */
		for (point = list->head; point; point = point->next)
			if (strcmp(point->name, name) >= 0)
				break;
	}

	/* Link to upper list.  */
	new->parent = list;

	/* In case of this is the first element of master. */
	if (list->head == NULL) {
		list->head = list->tail = new;
		return new;
	}

	/* In case of insertion is made at the tail of access_list. */
	if (point == NULL) {
		new->prev = list->tail;
		list->tail->next = new;
		list->tail = new;
		return new;
	}

	/* In case of insertion is made at the head of access_list. */
	if (point == list->head) {
		new->next = list->head;
		list->head->prev = new;
		list->head = new;
		return new;
	}

	/* Insertion is made at middle of the access_list. */
	new->next = point;
	new->prev = point->prev;

	if (point->prev)
		point->prev->next = new;
	point->prev = new;

	return new;
}

/* 【函数】在指定 master 的 hash 里按名字查找一个 community_list；找不到返回
 * NULL。name_hash 由调用方提供以避免重复算哈希；**传错哈希会查不到**（哈希表
 * 按值找桶），务必用 bgp_clist_hash_key 算出的值，或直接传 0 让它按名字重建键
 * （见 community_list_get 的用法）。name 为 NULL 时直接返回 NULL。 */
struct community_list *community_list_lookup(struct community_list_handler *ch,
					     const char *name,
					     uint32_t name_hash,
					     int master)
{
	struct community_list lookup;
	struct community_list_master *cm;

	if (!name)
		return NULL;

	cm = community_list_master_lookup(ch, master);
	if (!cm)
		return NULL;

	lookup.name = (char *)name;
	lookup.name_hash = name_hash;
	return hash_lookup(cm->hash, &lookup);
}

/* 【函数】取或建：先在 master 中查找名为 name 的 list，不存在则新建一个。配置
 * 路径用它实现“首次书写某 list 名时自动创建”。注意这里 lookup 传的 name_hash
 * 为 0，表示让 hash 回调按名字重新计算键。 */
static struct community_list *
community_list_get(struct community_list_handler *ch, const char *name,
		   int master)
{
	struct community_list *list;

	list = community_list_lookup(ch, name, 0, master);
	if (!list)
		list = community_list_insert(ch, name, master);
	return list;
}

/* 【函数】删除整个 list：先逐条 free 其所有规则条目，再从父链表（num 或 str）
 * 上摘除，最后从 hash 释放并 free 结构体。list->parent 指出它挂在哪个链表上，
 * 摘链时用 head / tail 维护边界。调用方必须先确保不再有引用。 */
static void community_list_delete(struct community_list_master *cm,
				  struct community_list *list)
{
	struct community_list_list *clist;
	struct community_entry *entry, *next;

	for (entry = list->head; entry; entry = next) {
		next = entry->next;
		community_entry_free(entry);
	}

	clist = list->parent;

	if (list->next)
		list->next->prev = list->prev;
	else
		clist->tail = list->prev;

	if (list->prev)
		list->prev->next = list->next;
	else
		clist->head = list->next;

	hash_release(cm->hash, list);
	community_list_free(list);
}

/* 【函数】判断 list 是否没有任何规则条目（head 与 tail 均为空）。用于决定是否
 * 需要删除整个 list，以及配置时检查写法冲突。 */
static bool community_list_empty_p(struct community_list *list)
{
	return list->head == NULL && list->tail == NULL;
}

/* 【函数】从 list 中删除单条规则：从链表摘除该 entry 并 free，若删完后 list
 * 变空则连带删除整个 list（cm 供删除 list 时用）。 */
/* Delete community-list entry from the list.  */
static void community_list_entry_delete(struct community_list_master *cm,
					struct community_list *list,
					struct community_entry *entry)
{
	if (entry->next)
		entry->next->prev = entry->prev;
	else
		list->tail = entry->prev;

	if (entry->prev)
		entry->prev->next = entry->next;
	else
		list->head = entry->next;

	community_entry_free(entry);

	if (community_list_empty_p(list))
		community_list_delete(cm, list);
}

/* 【函数】用新条目 entry 原地替换旧条目 replace（两者 seq 相同）：把 entry
 * 接进 replace 在链表中的位置，再 free 掉 replace。注意参数命名——entry 是新的、
 * replace 是被替换的，容易看反。 */
/*
 * Replace community-list entry in the list. Note that entry is the new one
 * and replace is one one being replaced.
 */
static void community_list_entry_replace(struct community_list *list,
					 struct community_entry *replace,
					 struct community_entry *entry)
{
	if (replace->next) {
		entry->next = replace->next;
		replace->next->prev = entry;
	} else {
		entry->next = NULL;
		list->tail = entry;
	}

	if (replace->prev) {
		entry->prev = replace->prev;
		replace->prev->next = entry;
	} else {
		entry->prev = NULL;
		list->head = entry;
	}

	community_entry_free(replace);
}

/* 【函数】把新规则 entry 加入 list，是维护“按 seq 升序”不变量的核心：
 *   1) seq 为 AUTO 时先分配自动序号；
 *   2) 若已存在同 seq 的条目，则走 entry_replace 原地替换；
 *   3) 否则找到第一个 seq 不小于它的位置插入，维持升序。
 * 代码里对“插到尾部 / 头部 / 中间”三种情况分别处理，注意链表 head / tail 的维护。 */
/* Add community-list entry to the list.  */
static void community_list_entry_add(struct community_list *list,
				     struct community_entry *entry,
				     struct community_list_handler *ch,
				     int master)
{
	struct community_entry *replace;
	struct community_entry *point;

	/* Automatic assignment of seq no. */
	if (entry->seq == COMMUNITY_SEQ_NUMBER_AUTO)
		entry->seq = bgp_clist_new_seq_get(list);

	if (list->tail && entry->seq > list->tail->seq)
		point = NULL;
	else {
		replace = bgp_clist_seq_check(list, entry->seq);
		if (replace) {
			community_list_entry_replace(list, replace, entry);
			return;
		}

		/* Check insert point. */
		for (point = list->head; point; point = point->next)
			if (point->seq >= entry->seq)
				break;
	}

	/* In case of this is the first element of the list. */
	entry->next = point;

	if (point) {
		if (point->prev)
			point->prev->next = entry;
		else
			list->head = entry;

		entry->prev = point->prev;
		point->prev = entry;
	} else {
		if (list->tail)
			list->tail->next = entry;
		else
			list->head = entry;

		entry->prev = list->tail;
		list->tail = entry;
	}
}

/* 【函数】在 list 中查找与给定值匹配且 direct 相同的规则条目，用于 unset 时定位
 * 要删除的那一条。standard 写法按团体值精确比较（分别走 community_cmp /
 * ecommunity_cmp / lcommunity_cmp），expanded 写法按 config 字符串比较。
 * arg 的类型取决于 style：standard 时是团体结构体指针，expanded 时是配置串。 */
/* Lookup community-list entry from the list.  */
static struct community_entry *
community_list_entry_lookup(struct community_list *list, const void *arg,
			    int direct)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		switch (entry->style) {
		case COMMUNITY_LIST_STANDARD:
			if (entry->direct == direct
			    && community_cmp(entry->u.com, arg))
				return entry;
			break;
		case EXTCOMMUNITY_LIST_STANDARD:
			if (entry->direct == direct
			    && ecommunity_cmp(entry->u.ecom, arg))
				return entry;
			break;
		case LARGE_COMMUNITY_LIST_STANDARD:
			if (entry->direct == direct
			    && lcommunity_cmp(entry->u.lcom, arg))
				return entry;
			break;
		case COMMUNITY_LIST_EXPANDED:
		case EXTCOMMUNITY_LIST_EXPANDED:
		case LARGE_COMMUNITY_LIST_EXPANDED:
			if (entry->direct == direct
			    && strcmp(entry->config, arg) == 0)
				return entry;
			break;
		default:
			break;
		}
	}
	return NULL;
}

/* 【函数】把 standard community 的第 i 个值转成可读字符串（供 expanded 正则匹配
 * 与回显使用）。已知的著名团体（no-export、blackhole 等，见 COMMUNITY_* 常量）返回
 * 名字串，其余按 as:val 格式返回。注意它**总是新分配字符串**（MTYPE_COMMUNITY_STR），
 * 调用方负责释放；未知团体那条分支里 snprintf 的缓冲区来自 XSTRDUP("65536:65535")，
 * 长度足够容纳替换后的内容。 */
static char *community_str_get(struct community *com, int i)
{
	uint32_t comval;
	uint16_t as;
	uint16_t val;
	char *str;

	memcpy(&comval, com_nthval(com, i), sizeof(uint32_t));
	comval = ntohl(comval);

	switch (comval) {
	case COMMUNITY_GSHUT:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "graceful-shutdown");
		break;
	case COMMUNITY_ACCEPT_OWN:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "accept-own");
		break;
	case COMMUNITY_ROUTE_FILTER_TRANSLATED_v4:
		str = XSTRDUP(MTYPE_COMMUNITY_STR,
			      "route-filter-translated-v4");
		break;
	case COMMUNITY_ROUTE_FILTER_v4:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "route-filter-v4");
		break;
	case COMMUNITY_ROUTE_FILTER_TRANSLATED_v6:
		str = XSTRDUP(MTYPE_COMMUNITY_STR,
			      "route-filter-translated-v6");
		break;
	case COMMUNITY_ROUTE_FILTER_v6:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "route-filter-v6");
		break;
	case COMMUNITY_LLGR_STALE:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "llgr-stale");
		break;
	case COMMUNITY_NO_LLGR:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "no-llgr");
		break;
	case COMMUNITY_ACCEPT_OWN_NEXTHOP:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "accept-own-nexthop");
		break;
	case COMMUNITY_BLACKHOLE:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "blackhole");
		break;
	case COMMUNITY_NO_EXPORT:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "no-export");
		break;
	case COMMUNITY_NO_ADVERTISE:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "no-advertise");
		break;
	case COMMUNITY_LOCAL_AS:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "local-AS");
		break;
	case COMMUNITY_NO_PEER:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "no-peer");
		break;
	default:
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "65536:65535");
		as = CHECK_FLAG((comval >> 16), 0xFFFF);
		val = CHECK_FLAG(comval, 0xFFFF);
		snprintf(str, strlen(str), "%u:%d", as, val);
		break;
	}

	return str;
}

/* 【函数】对单个 community（第 i 个值）做正则匹配：先取该值的字符串，再用
 * regexec 匹配 reg。com 为空或 size 为 0 时按空串匹配。命中返回 true。
 * 供 any_match 这类“逐个值判断”的语义使用。 */
/* Internal function to perform regular expression match for
 * a single community. */
static bool community_regexp_include(struct frregex *reg, struct community *com, int i)
{
	char *str;
	int rv;

	/* When there is no communities attribute it is treated as empty string.
	 */
	if (com == NULL || com->size == 0)
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "");
	else
		str = community_str_get(com, i);

	/* Regular expression match.  */
	rv = regexec(&reg->real, str, 0, NULL, 0);

	XFREE(MTYPE_COMMUNITY_STR, str);

	return rv == 0;
}

/* 【函数】对**整个** community 属性做正则匹配：把全部团体拼成一个字符串（用
 * community_str），再整体 regexec。com 为空/长度为 0 时直接返回 false。
 * 若配置了 community alias（bgp_ca_alias_hash 非空），会先把别名还原成真实团体串
 * （bgp_alias2community_str）再匹配——这样正则里可以写别名。还原出的临时串需释放。 */
/* Internal function to perform regular expression match for community
   attribute.  */
static bool community_regexp_match(struct community *com, struct frregex *reg)
{
	const char *str;
	char *regstr;
	int rv;
	bool translate_alias = !!bgp_ca_alias_hash->count;

	/* When there is no communities attribute it is treated as empty
	   string.  */
	if (com == NULL || com->size == 0)
		return false;

	str = community_str(com, false, translate_alias);

	/* If at least one community alias is configured, then let's
	 * do the work, otherwise we don't need to spend time on splitting
	 * stuff and creating a new string.
	 */
	regstr = translate_alias ? bgp_alias2community_str(str) : (char *)str;

	/* Regular expression match.  */
	rv = regexec(&reg->real, regstr, 0, NULL, 0);

	/* This is allocated by frrstr_join(), and needs to be freed
	 * only if it was created.
	 */
	if (translate_alias)
		XFREE(MTYPE_TMP, regstr);

	return rv == 0;
}

/* 【函数】取扩展团体第 i 个值的字符串形式（DISPLAY 格式）。返回新分配串，调用
 * 方负责释放。是对 ecommunity_ecom2str_one 的薄封装。 */
static char *ecommunity_str_get(struct ecommunity *ecom, int i)
{
	return ecommunity_ecom2str_one(ecom, ECOMMUNITY_FORMAT_DISPLAY, i);
}

/* 【函数】取大团体第 i 个值的字符串形式，固定格式为 global:local1:local2
 * （三段各 32 位）。直接按 LCOMMUNITY_SIZE 偏移定位、大端解析。返回新分配的
 * 48 字节串（MTYPE_LCOMMUNITY_STR），调用方负责释放。 */
static char *lcommunity_str_get(struct lcommunity *lcom, int i)
{
	struct lcommunity_val lcomval;
	uint32_t globaladmin;
	uint32_t localdata1;
	uint32_t localdata2;
	char *str;
	const uint8_t *ptr;

	ptr = lcom->val + (i * LCOMMUNITY_SIZE);

	memcpy(&lcomval, ptr, LCOMMUNITY_SIZE);

	/* Allocate memory.  48 bytes taken off bgp_lcommunity.c */
	ptr = (uint8_t *)lcomval.val;
	ptr = ptr_get_be32(ptr, &globaladmin);
	ptr = ptr_get_be32(ptr, &localdata1);
	ptr = ptr_get_be32(ptr, &localdata2);
	(void)ptr; /* consume value */

	str = XMALLOC(MTYPE_LCOMMUNITY_STR, 48);
	snprintf(str, 48, "%u:%u:%u", globaladmin, localdata1, localdata2);

	return str;
}

/* 【函数】对单个大团体（第 i 个值）做正则匹配。lcom 为空/长度为 0 时按空串匹配。
 * 命中返回 true。供大团体的 any_match 语义使用。 */
/* Internal function to perform regular expression match for
 * a single community. */
static bool lcommunity_regexp_include(struct frregex *reg, struct lcommunity *lcom, int i)
{
	char *str;

	/* When there is no communities attribute it is treated as empty string.
	 */
	if (lcom == NULL || lcom->size == 0)
		str = XSTRDUP(MTYPE_LCOMMUNITY_STR, "");
	else
		str = lcommunity_str_get(lcom, i);

	/* Regular expression match.  */
	if (regexec(&reg->real, str, 0, NULL, 0) == 0) {
		XFREE(MTYPE_LCOMMUNITY_STR, str);
		return true;
	}

	XFREE(MTYPE_LCOMMUNITY_STR, str);
	/* No match.  */
	return false;
}

/* 【函数】对单个扩展团体（第 i 个值）做正则匹配。ecom 为空/长度为 0 时按空串
 * 匹配。命中返回 true。供扩展团体的 any_match 语义使用。 */
/* Internal function to perform regular expression match for a single ecommunity. */
static bool ecommunity_regexp_include(struct frregex *reg, struct ecommunity *ecom, int i)
{
	char *str;

	/* When there is no communities attribute it is treated as empty string.
	 */
	if (ecom == NULL || ecom->size == 0)
		str = XSTRDUP(MTYPE_ECOMMUNITY_STR, "");
	else
		str = ecommunity_str_get(ecom, i);

	/* Regular expression match.  */
	if (regexec(&reg->real, str, 0, NULL, 0) == 0) {
		XFREE(MTYPE_ECOMMUNITY_STR, str);
		return true;
	}

	XFREE(MTYPE_ECOMMUNITY_STR, str);
	/* No match.  */
	return false;
}

/* 【函数】对**整个**大团体属性做正则匹配：先拼成整串（lcommunity_str），com
 * 为空/长度为 0 时返回 false。与 community_regexp_match 一样支持 community alias
 * 还原后再匹配。返回是否命中。 */
static bool lcommunity_regexp_match(struct lcommunity *com, struct frregex *reg)
{
	const char *str;
	char *regstr;
	int rv;
	bool translate_alias = !!bgp_ca_alias_hash->count;

	/* When there is no communities attribute it is treated as empty
	   string.  */
	if (com == NULL || com->size == 0)
		return false;

	str = lcommunity_str(com, false, translate_alias);

	/* If at least one community alias is configured, then let's
	 * do the work, otherwise we don't need to spend time on splitting
	 * stuff and creating a new string.
	 */
	regstr = translate_alias ? bgp_alias2community_str(str) : (char *)str;

	/* Regular expression match.  */
	rv = regexec(&reg->real, regstr, 0, NULL, 0);

	/* This is allocated by frrstr_join(), and needs to be freed
	 * only if it was created.
	 */
	if (translate_alias)
		XFREE(MTYPE_TMP, regstr);

	return rv == 0;
}


/* 【函数】对**整个**扩展团体属性做正则匹配：取 ecommunity_str 整串后 regexec。
 * ecom 为空/长度为 0 时按空串匹配。返回是否命中。注意扩展团体不做 alias 还原。 */
static bool ecommunity_regexp_match(struct ecommunity *ecom, struct frregex *reg)
{
	const char *str;

	/* When there is no communities attribute it is treated as empty
	   string.  */
	if (ecom == NULL || ecom->size == 0)
		str = "";
	else
		str = ecommunity_str(ecom);

	/* Regular expression match.  */
	if (regexec(&reg->real, str, 0, NULL, 0) == 0)
		return true;

	/* No match.  */
	return false;
}

/* 【函数】常规匹配：按 seq 顺序遍历 list 的条目，standard 走 community_match
 * （整体包含）、expanded 走 community_regexp_match；**第一条命中即定论**，返回值
 * 等于该条目的 direct 是否为 permit。全部不中返回 false。注意返回 true 意味着
 * “被这条 list 放行”，false 可能是没命中也可能是命中 deny，调用方需按语义使用。 */
/* When given community attribute matches to the community-list return
   1 else return 0.  */
bool community_list_match(struct community *com, struct community_list *list)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style == COMMUNITY_LIST_STANDARD) {
			if (community_match(com, entry->u.com))
				return entry->direct == COMMUNITY_PERMIT;
		} else if (entry->style == COMMUNITY_LIST_EXPANDED) {
			if (community_regexp_match(com, entry->reg))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】大团体的常规匹配，逻辑与 community_list_match 相同：standard 走
 * lcommunity_match，expanded 走 lcommunity_regexp_match，第一条命中即定论，返回
 * 该条目的 permit 结论。 */
bool lcommunity_list_match(struct lcommunity *lcom, struct community_list *list)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style == LARGE_COMMUNITY_LIST_STANDARD) {
			if (lcommunity_match(lcom, entry->u.lcom))
				return entry->direct == COMMUNITY_PERMIT;
		} else if (entry->style == LARGE_COMMUNITY_LIST_EXPANDED) {
			if (lcommunity_regexp_match(lcom, entry->reg))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】扩展团体的 exact_match：standard 用 ecommunity_cmp 做**全等**比较
 * （集合完全一致而非包含），expanded 仍走正则（与常规 match 相同——原注释已说明
 * 这种写法下二者等价）。第一条命中即返回该条目的 permit 结论。 */
/* Perform exact matching. In case of expanded extended-community-list, do
 * same thing as ecommunity_list_match().
 */
bool ecommunity_list_exact_match(struct ecommunity *ecom, struct community_list *list)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style == EXTCOMMUNITY_LIST_STANDARD) {
			if (ecommunity_cmp(ecom, entry->u.lcom))
				return entry->direct == COMMUNITY_PERMIT;
		} else if (entry->style == EXTCOMMUNITY_LIST_EXPANDED) {
			if (ecommunity_regexp_match(ecom, entry->reg))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】大团体的 exact_match：standard 用 lcommunity_cmp 做全等比较，expanded
 * 仍走正则（与常规 match 等价）。命中返回该条目的 permit 结论。 */
/* Perform exact matching.  In case of expanded large-community-list, do
 * same thing as lcommunity_list_match().
 */
bool lcommunity_list_exact_match(struct lcommunity *lcom,
				 struct community_list *list)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style == LARGE_COMMUNITY_LIST_STANDARD) {
			if (lcommunity_cmp(lcom, entry->u.lcom))
				return entry->direct == COMMUNITY_PERMIT;
		} else if (entry->style == LARGE_COMMUNITY_LIST_EXPANDED) {
			if (lcommunity_regexp_match(lcom, entry->reg))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】扩展团体的常规匹配：standard 走 ecommunity_match，expanded 走
 * ecommunity_regexp_match，第一条命中即定论，返回该条目的 permit 结论。 */
bool ecommunity_list_match(struct ecommunity *ecom, struct community_list *list)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style == EXTCOMMUNITY_LIST_STANDARD) {
			if (ecommunity_match(ecom, entry->u.ecom))
				return entry->direct == COMMUNITY_PERMIT;
		} else if (entry->style == EXTCOMMUNITY_LIST_EXPANDED) {
			if (ecommunity_regexp_match(ecom, entry->reg))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】standard community 的 exact_match：standard 用 community_cmp 做全等
 * 比较，expanded 仍走正则（与常规 match 等价）。命中返回该条目的 permit 结论。 */
/* Perform exact matching.  In case of expanded community-list, do
   same thing as community_list_match().  */
bool community_list_exact_match(struct community *com,
				struct community_list *list)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style == COMMUNITY_LIST_STANDARD) {
			if (community_cmp(com, entry->u.com))
				return entry->direct == COMMUNITY_PERMIT;
		} else if (entry->style == COMMUNITY_LIST_EXPANDED) {
			if (community_regexp_match(com, entry->reg))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】any_match：**逐值**判断——只要 com 里任意一个团体值命中列表任意一条
 * 规则就立即返回该规则的 permit 结论。与 community_list_match 的区别在于它是
 * “值级”匹配（standard 用 community_include 判断某单值是否在规则集合中，expanded
 * 用 community_regexp_include 对单值做正则），适合“有没有打某个标”的高效判断。 */
bool community_list_any_match(struct community *com, struct community_list *list)
{
	struct community_entry *entry;
	uint32_t val;
	int i;

	for (i = 0; i < com->size; i++) {
		val = community_val_get(com, i);

		for (entry = list->head; entry; entry = entry->next) {
			if (entry->style == COMMUNITY_LIST_STANDARD &&
			    community_include(entry->u.com, val))
				return entry->direct == COMMUNITY_PERMIT;
			if ((entry->style == COMMUNITY_LIST_EXPANDED) &&
			    community_regexp_include(entry->reg, com, i))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】变换而非查询：把 com 中所有**命中 list 里 permit 条目**的团体值删除，
 * 返回同一个 com（原地修改）。用于 route-map 的 `set comm-list ... delete`。
 * 实现要点：先记录要删的下标（com_index_to_delete），**倒序**删除以避免下标错位；
 * 命中条目后 break（每条值只被第一条命中的规则决定去留）。命中 deny 条目的值不动。 */
/* Delete all permitted communities in the list from com.  */
struct community *community_list_match_delete(struct community *com,
					      struct community_list *list)
{
	struct community_entry *entry;
	uint32_t val;
	uint32_t com_index_to_delete[com->size];
	int delete_index = 0;
	int i;

	/* Loop over each community value and evaluate each against the
	 * community-list.  If we need to delete a community value add its index
	 * to com_index_to_delete.
	 */
	for (i = 0; i < com->size; i++) {
		val = community_val_get(com, i);

		for (entry = list->head; entry; entry = entry->next) {
			if ((entry->style == COMMUNITY_LIST_STANDARD) &&
			    community_include(entry->u.com, val)) {
				if (entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			} else if ((entry->style == COMMUNITY_LIST_EXPANDED) &&
				   community_regexp_include(entry->reg, com, i)) {
				if (entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}
		}
	}

	/* Delete all of the communities we flagged for deletion */
	for (i = delete_index - 1; i >= 0; i--) {
		val = community_val_get(com, com_index_to_delete[i]);
		val = htonl(val);
		community_del_val(com, &val);
	}

	return com;
}

/* 【函数】查重：遍历 list 已有条目，判断新条目 new 是否与之重复。判定要求
 * **style 与 direct 都相同**，再按 style 比较团体值（standard 三套 *_cmp，expanded
 * 比 config 串）。重名条件成立返回 true，调用方据此丢弃新条目。避免同一 list 内
 * 出现功能完全相同的两条规则。 */
/* To avoid duplicated entry in the community-list, this function
   compares specified entry to existing entry.  */
static bool community_list_dup_check(struct community_list *list,
				     struct community_entry *new)
{
	struct community_entry *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (entry->style != new->style)
			continue;

		if (entry->direct != new->direct)
			continue;

		switch (entry->style) {
		case COMMUNITY_LIST_STANDARD:
			if (community_cmp(entry->u.com, new->u.com))
				return true;
			break;
		case LARGE_COMMUNITY_LIST_STANDARD:
			if (lcommunity_cmp(entry->u.lcom, new->u.lcom))
				return true;
			break;
		case EXTCOMMUNITY_LIST_STANDARD:
			if (ecommunity_cmp(entry->u.ecom, new->u.ecom))
				return true;
			break;
		case COMMUNITY_LIST_EXPANDED:
		case EXTCOMMUNITY_LIST_EXPANDED:
		case LARGE_COMMUNITY_LIST_EXPANDED:
			if (new->config && (strcmp(entry->config, new->config) == 0))
				return true;
			break;
		default:
			break;
		}
	}
	return false;
}

/* 【函数】新增/覆盖一条 standard community-list 规则。流程：解析可选 seq（NULL
 * 表示 AUTO）→ 取或建 list → **检查写法一致性**（非空 list 的首条 style 必须与新
 * 条目相同，否则返回 CONFLICT 错误码）→ 按 style 解析团体值或编译正则 → 查重 →
 * 加入并按需通知 route-map 依赖刷新。返回 0 成功，负数见 COMMUNITY_LIST_ERR_*。 */
/* Set community-list.  */
int community_list_set(struct community_list_handler *ch, const char *name,
		       const char *str, const char *seq, int direct, int style)
{
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct community *com = NULL;
	struct frregex *regex = NULL;
	int64_t seqnum = COMMUNITY_SEQ_NUMBER_AUTO;

	if (seq)
		seqnum = (int64_t)atol(seq);

	/* Get community list. */
	list = community_list_get(ch, name, COMMUNITY_LIST_MASTER);

	/* When community-list already has entry, new entry should have same
	   style.  If you want to have mixed style community-list, you can
	   comment out this check.  */
	if (!community_list_empty_p(list)) {
		struct community_entry *first;

		first = list->head;

		if (style != first->style) {
			return (first->style == COMMUNITY_LIST_STANDARD
					? COMMUNITY_LIST_ERR_STANDARD_CONFLICT
					: COMMUNITY_LIST_ERR_EXPANDED_CONFLICT);
		}
	}

	if (style == COMMUNITY_LIST_STANDARD)
		com = community_str2com(str);
	else
		regex = bgp_regcomp(str);

	if (!com && !regex)
		return COMMUNITY_LIST_ERR_MALFORMED_VAL;

	entry = community_entry_new();
	entry->direct = direct;
	entry->style = style;
	entry->u.com = com;
	entry->reg = regex;
	entry->seq = seqnum;
	entry->config =
		(regex ? XSTRDUP(MTYPE_COMMUNITY_LIST_CONFIG, str) : NULL);

	/* Do not put duplicated community entry.  */
	if (community_list_dup_check(list, entry))
		community_entry_free(entry);
	else {
		community_list_entry_add(list, entry, ch,
					 COMMUNITY_LIST_MASTER);
		route_map_notify_dependencies(name, RMAP_EVENT_CLIST_ADDED);
	}

	return 0;
}

/* 【函数】删除 standard community-list 规则。str 为 NULL 时删除整个 list 的所有
 * 条目（并连带删 list）；否则按 style 解析出待删值定位条目后删除。list 或条目不
 * 存在时**静默返回**（无错误提示）。删除成功会通知 route-map 依赖刷新。
 * 注意它返回 void，无法告知调用方是否真的删掉了东西。 */
/* Unset community-list */
void community_list_unset(struct community_list_handler *ch, const char *name,
			  const char *str, const char *seq, int direct,
			  int style)
{
	struct community_list_master *cm = NULL;
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct community *com = NULL;

	/* Lookup community list.  */
	list = community_list_lookup(ch, name, 0, COMMUNITY_LIST_MASTER);
	if (list == NULL)
		return;

	cm = community_list_master_lookup(ch, COMMUNITY_LIST_MASTER);
	/* Delete all of entry belongs to this community-list.  */
	if (!str) {
		community_list_delete(cm, list);
		route_map_notify_dependencies(name, RMAP_EVENT_CLIST_DELETED);
		return;
	}

	if (style == COMMUNITY_LIST_STANDARD)
		com = community_str2com(str);

	if (com) {
		entry = community_list_entry_lookup(list, com, direct);
		community_free(&com);
	} else
		entry = community_list_entry_lookup(list, str, direct);

	if (!entry)
		return;

	community_list_entry_delete(cm, list, entry);
	route_map_notify_dependencies(name, RMAP_EVENT_CLIST_DELETED);
}

/* 【函数】大团体的 any_match：逐值判断，任意一个大团体值命中任意一条规则即返回
 * 该规则的 permit 结论。standard 用 lcommunity_include（按 12 字节值比较），
 * expanded 用 lcommunity_regexp_include。 */
bool lcommunity_list_any_match(struct lcommunity *lcom,
			       struct community_list *list)
{
	struct community_entry *entry;
	uint8_t *ptr;
	int i;

	for (i = 0; i < lcom->size; i++) {
		ptr = lcom->val + (i * LCOMMUNITY_SIZE);

		for (entry = list->head; entry; entry = entry->next) {
			if ((entry->style == LARGE_COMMUNITY_LIST_STANDARD) &&
			    lcommunity_include(entry->u.lcom, ptr))
				return entry->direct == COMMUNITY_PERMIT;
			if ((entry->style == LARGE_COMMUNITY_LIST_EXPANDED) &&
			    lcommunity_regexp_include(entry->reg, lcom, i))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】扩展团体的 any_match：逐值判断，standard 用 ecommunity_include_one，
 * expanded 用 ecommunity_regexp_include。任意一值命中即返回该规则的 permit 结论。
 * 注意这里按 ecom->unit_size 步进（扩展团体的单元长度可变）。 */
bool ecommunity_list_any_match(struct ecommunity *ecom, struct community_list *list)
{
	struct community_entry *entry;
	uint8_t *ptr;
	uint32_t i;

	for (i = 0; i < ecom->size; i++) {
		ptr = ecom->val + (i * ecom->unit_size);

		for (entry = list->head; entry; entry = entry->next) {
			if ((entry->style == EXTCOMMUNITY_LIST_STANDARD) &&
			    ecommunity_include_one(entry->u.ecom, ptr))
				return entry->direct == COMMUNITY_PERMIT;
			if ((entry->style == EXTCOMMUNITY_LIST_EXPANDED) &&
			    ecommunity_regexp_include(entry->reg, ecom, i))
				return entry->direct == COMMUNITY_PERMIT;
		}
	}
	return false;
}

/* 【函数】把 lcom 中所有命中 list 里 permit 条目的大团体值删除，返回同一个 lcom
 * （原地修改）。实现同 community 版本：记录待删下标后**倒序**删除避免错位；命中
 * 即 break，deny 条目不删。 */
/* Delete all permitted large communities in the list from com.  */
struct lcommunity *lcommunity_list_match_delete(struct lcommunity *lcom,
						struct community_list *list)
{
	struct community_entry *entry;
	uint32_t com_index_to_delete[lcom->size];
	uint8_t *ptr;
	int delete_index = 0;
	int i;

	/* Loop over each lcommunity value and evaluate each against the
	 * community-list.  If we need to delete a community value add its index
	 * to com_index_to_delete.
	 */
	for (i = 0; i < lcom->size; i++) {
		ptr = lcom->val + (i * LCOMMUNITY_SIZE);
		for (entry = list->head; entry; entry = entry->next) {
			if ((entry->style == LARGE_COMMUNITY_LIST_STANDARD) &&
			    lcommunity_include(entry->u.lcom, ptr)) {
				if (entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}

			else if ((entry->style ==
				  LARGE_COMMUNITY_LIST_EXPANDED) &&
				 lcommunity_regexp_include(entry->reg, lcom,
							   i)) {
				if (entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}
		}
	}

	/* Delete all of the communities we flagged for deletion */
	for (i = delete_index - 1; i >= 0; i--) {
		ptr = lcom->val + (com_index_to_delete[i] * LCOMMUNITY_SIZE);
		lcommunity_del_val(lcom, ptr);
	}

	return lcom;
}

/* 【函数】把 ecom 中所有命中 list 里 permit 条目的扩展团体值删除，返回同一个 ecom
 * （原地修改）。这里用了一个临时的单值 ecommunity（local_ecom）来复用 standard 与
 * expanded 的判断接口：standard 走 ecommunity_include，expanded 走
 * ecommunity_regexp_match。记录下标后倒序删除；循环内用 ecommunity_strfree 释放
 * 临时串。 */
/* Delete all permitted extended communities in the list from ecom.*/
struct ecommunity *ecommunity_list_match_delete(struct ecommunity *ecom,
						struct community_list *list)
{
	struct community_entry *entry;
	uint32_t com_index_to_delete[ecom->size];
	uint8_t *ptr;
	uint32_t delete_index = 0;
	uint32_t i;
	struct ecommunity local_ecom = {.size = 1};
	struct ecommunity_val local_eval = {0};

	local_ecom.unit_size = ecom->unit_size;
	local_ecom.disable_ieee_floating = ecom->disable_ieee_floating;
	for (i = 0; i < ecom->size; i++) {
		local_ecom.val = ecom->val + (i * ECOMMUNITY_SIZE);
		for (entry = list->head; entry; entry = entry->next) {
			if (((entry->style == EXTCOMMUNITY_LIST_STANDARD) &&
			     ecommunity_include(entry->u.ecom, &local_ecom)) ||
			    ((entry->style == EXTCOMMUNITY_LIST_EXPANDED) &&
			     ecommunity_regexp_match(&local_ecom, entry->reg))) {
				if (entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}
		}
		ecommunity_strfree(&local_ecom.str);
	}

	/* Delete all of the extended communities we flagged for deletion */
	for (i = delete_index; i > 0; i--) {
		ptr = ecom->val + (com_index_to_delete[i-1] * ECOMMUNITY_SIZE);
		memcpy(&local_eval.val, ptr, sizeof(local_eval.val));
		ecommunity_del_val(ecom, &local_eval);
	}

	return ecom;
}

/* 【函数】校验一个（可能含多个、空格分隔的）大团体字符串在指定 style 下是否
 * 合法，供 CLI 在写入前拦截坏输入。standard 要求每个值恰好 3 段、每段非负十进制
 * 且不超过 UINT_MAX；expanded 要求能成功编译为正则。任一不合格即返回 false。
 * 用 invalid 计数累加，最后 invalid 大于 0 视为非法。注意它只校验不写入。 */
/* Helper to check if every octet do not exceed UINT_MAX */
bool lcommunity_list_valid(const char *community, int style)
{
	int octets;
	char **splits, **communities;
	char *endptr;
	int num, num_communities;
	struct frregex *regres;
	int invalid = 0;

	frrstr_split(community, " ", &communities, &num_communities);

	for (int j = 0; j < num_communities; j++) {
		octets = 0;
		frrstr_split(communities[j], ":", &splits, &num);

		for (int i = 0; i < num; i++) {
			if (strlen(splits[i]) == 0)
				/* There is no digit to check */
				invalid++;

			if (style == LARGE_COMMUNITY_LIST_STANDARD) {
				if (*splits[i] == '-')
					/* Must not be negative */
					invalid++;
				else if (strtoul(splits[i], &endptr, 10)
					 > UINT_MAX)
					/* Larger than 4 octets */
					invalid++;
				else if (*endptr)
					/* Not all characters were digits */
					invalid++;
			} else {
				regres = bgp_regcomp(communities[j]);
				if (!regres)
					/* malformed regex */
					invalid++;
				else
					bgp_regex_free(regres);
			}

			octets++;
			XFREE(MTYPE_TMP, splits[i]);
		}
		XFREE(MTYPE_TMP, splits);

		if (octets != 3)
			invalid++;

		XFREE(MTYPE_TMP, communities[j]);
	}
	XFREE(MTYPE_TMP, communities);

	return (invalid > 0) ? false : true;
}

/* 【函数】新增/覆盖一条 large-community-list 规则，流程与 community_list_set 相同
 * （seq 解析 → 取或建 list → 写法一致性检查 → 解析/编译 → 查重 → 加入并通知）。
 * 区别仅在于操作 LARGE_COMMUNITY_LIST_MASTER，且 str 为 NULL 时跳过解析（允许只
 * 建空 list）。返回 0 成功，负数错误码见 COMMUNITY_LIST_ERR_*。 */
/* Set lcommunity-list.  */
int lcommunity_list_set(struct community_list_handler *ch, const char *name,
			const char *str, const char *seq, int direct, int style)
{
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct lcommunity *lcom = NULL;
	struct frregex *regex = NULL;
	int64_t seqnum = COMMUNITY_SEQ_NUMBER_AUTO;

	if (seq)
		seqnum = (int64_t)atol(seq);

	/* Get community list. */
	list = community_list_get(ch, name, LARGE_COMMUNITY_LIST_MASTER);

	/* When community-list already has entry, new entry should have same
	   style.  If you want to have mixed style community-list, you can
	   comment out this check.  */
	if (!community_list_empty_p(list)) {
		struct community_entry *first;

		first = list->head;

		if (style != first->style) {
			return (first->style == COMMUNITY_LIST_STANDARD
					? COMMUNITY_LIST_ERR_STANDARD_CONFLICT
					: COMMUNITY_LIST_ERR_EXPANDED_CONFLICT);
		}
	}

	if (str) {
		if (style == LARGE_COMMUNITY_LIST_STANDARD)
			lcom = lcommunity_str2com(str);
		else
			regex = bgp_regcomp(str);

		if (!lcom && !regex)
			return COMMUNITY_LIST_ERR_MALFORMED_VAL;
	}

	entry = community_entry_new();
	entry->direct = direct;
	entry->style = style;
	entry->u.lcom = lcom;
	entry->reg = regex;
	entry->seq = seqnum;
	entry->config =
		(regex ? XSTRDUP(MTYPE_COMMUNITY_LIST_CONFIG, str) : NULL);

	/* Do not put duplicated community entry.  */
	if (community_list_dup_check(list, entry))
		community_entry_free(entry);
	else {
		community_list_entry_add(list, entry, ch,
					 LARGE_COMMUNITY_LIST_MASTER);
		route_map_notify_dependencies(name, RMAP_EVENT_LLIST_ADDED);
	}

	return 0;
}

/* 【函数】删除 large-community-list 规则。str 为 NULL 时删除整个 list，否则按
 * style 解析出大团体值或正则，定位条目后删除；list 或条目不存在时静默返回。
 * 与 community_list_unset 对应，区别是解析用 lcommunity_str2com / bgp_regcomp，
 * 且删除成功后通知 RMAP_EVENT_LLIST_DELETED。返回 void，无失败提示。 */
/* Unset community-list.  When str is NULL, delete all of
   community-list entry belongs to the specified name.  */
void lcommunity_list_unset(struct community_list_handler *ch, const char *name,
			   const char *str, const char *seq, int direct,
			   int style)
{
	struct community_list_master *cm = NULL;
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct lcommunity *lcom = NULL;
	struct frregex *regex = NULL;

	/* Lookup community list.  */
	list = community_list_lookup(ch, name, 0, LARGE_COMMUNITY_LIST_MASTER);
	if (list == NULL)
		return;

	cm = community_list_master_lookup(ch, LARGE_COMMUNITY_LIST_MASTER);
	/* Delete all of entry belongs to this community-list.  */
	if (!str) {
		community_list_delete(cm, list);
		route_map_notify_dependencies(name, RMAP_EVENT_LLIST_DELETED);
		return;
	}

	if (style == LARGE_COMMUNITY_LIST_STANDARD)
		lcom = lcommunity_str2com(str);
	else
		regex = bgp_regcomp(str);

	if (!lcom && !regex)
		return;

	if (lcom)
		entry = community_list_entry_lookup(list, lcom, direct);
	else
		entry = community_list_entry_lookup(list, str, direct);

	if (lcom)
		lcommunity_free(&lcom);
	if (regex)
		bgp_regex_free(regex);

	if (!entry)
		return;

	community_list_entry_delete(cm, list, entry);
	route_map_notify_dependencies(name, RMAP_EVENT_LLIST_DELETED);
}

/* 【函数】新增/覆盖一条 extcommunity-list 规则。流程与另两套 set 相同，要点：
 * str 为 NULL 直接返回 MALFORMED_VAL；standard 用 ecommunity_str2com 解析并额外
 * 生成 DISPLAY 与 COMMUNITY_LIST 两种字符串（ecom->str 与 entry->config）；
 * 操作 EXTCOMMUNITY_LIST_MASTER，通知 RMAP_EVENT_ECLIST_ADDED。返回 0 或负错误码。 */
/* Set extcommunity-list.  */
int extcommunity_list_set(struct community_list_handler *ch, const char *name,
			  const char *str, const char *seq, int direct,
			  int style)
{
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct ecommunity *ecom = NULL;
	struct frregex *regex = NULL;
	int64_t seqnum = COMMUNITY_SEQ_NUMBER_AUTO;

	if (seq)
		seqnum = (int64_t)atol(seq);

	if (str == NULL)
		return COMMUNITY_LIST_ERR_MALFORMED_VAL;

	/* Get community list. */
	list = community_list_get(ch, name, EXTCOMMUNITY_LIST_MASTER);

	/* When community-list already has entry, new entry should have same
	   style.  If you want to have mixed style community-list, you can
	   comment out this check.  */
	if (!community_list_empty_p(list)) {
		struct community_entry *first;

		first = list->head;

		if (style != first->style) {
			return (first->style == EXTCOMMUNITY_LIST_STANDARD
					? COMMUNITY_LIST_ERR_STANDARD_CONFLICT
					: COMMUNITY_LIST_ERR_EXPANDED_CONFLICT);
		}
	}

	if (style == EXTCOMMUNITY_LIST_STANDARD)
		ecom = ecommunity_str2com(str, 0, 1);
	else
		regex = bgp_regcomp(str);

	if (!ecom && !regex)
		return COMMUNITY_LIST_ERR_MALFORMED_VAL;

	if (ecom)
		ecom->str =
			ecommunity_ecom2str(ecom, ECOMMUNITY_FORMAT_DISPLAY, 0);

	entry = community_entry_new();
	entry->direct = direct;
	entry->style = style;
	if (ecom)
		entry->config = ecommunity_ecom2str(
			ecom, ECOMMUNITY_FORMAT_COMMUNITY_LIST, 0);
	else if (regex)
		entry->config = XSTRDUP(MTYPE_COMMUNITY_LIST_CONFIG, str);

	entry->u.ecom = ecom;
	entry->reg = regex;
	entry->seq = seqnum;

	/* Do not put duplicated community entry.  */
	if (community_list_dup_check(list, entry))
		community_entry_free(entry);
	else {
		community_list_entry_add(list, entry, ch,
					 EXTCOMMUNITY_LIST_MASTER);
		route_map_notify_dependencies(name, RMAP_EVENT_ECLIST_ADDED);
	}

	return 0;
}

/* 【函数】删除 extcommunity-list 规则。str 为 NULL 时删除整个 list，否则解析定位
 * 后删除；不存在则静默返回。操作 EXTCOMMUNITY_LIST_MASTER，成功后通知
 * RMAP_EVENT_ECLIST_DELETED。返回 void，无失败提示。 */
/* Unset extcommunity-list.
 *
 * When str is NULL, delete all extcommunity-list entries belonging to the
 * specified name.
 */
void extcommunity_list_unset(struct community_list_handler *ch,
			     const char *name, const char *str, const char *seq,
			     int direct, int style)
{
	struct community_list_master *cm = NULL;
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct ecommunity *ecom = NULL;

	/* Lookup extcommunity list.  */
	list = community_list_lookup(ch, name, 0, EXTCOMMUNITY_LIST_MASTER);
	if (list == NULL)
		return;

	cm = community_list_master_lookup(ch, EXTCOMMUNITY_LIST_MASTER);
	/* Delete all of entry belongs to this extcommunity-list.  */
	if (!str) {
		community_list_delete(cm, list);
		route_map_notify_dependencies(name, RMAP_EVENT_ECLIST_DELETED);
		return;
	}

	if (style == EXTCOMMUNITY_LIST_STANDARD)
		ecom = ecommunity_str2com(str, 0, 1);

	if (ecom) {
		entry = community_list_entry_lookup(list, ecom, direct);
		ecommunity_free(&ecom);
	} else
		entry = community_list_entry_lookup(list, str, direct);

	if (!entry)
		return;

	community_list_entry_delete(cm, list, entry);
	route_map_notify_dependencies(name, RMAP_EVENT_ECLIST_DELETED);
}

/* 【函数】初始化三类团体的总句柄：分配 handler，并为 community / extcommunity /
 * lcommunity 三个 master 各建一张哈希表（初始桶数 4，名字哈希回调共用同一对）。
 * 返回句柄，调用方需保存（通常赋给全局 bgp_clist）。链表 num/str 初始为空。 */
/* Initialize community-list.  Return community-list handler.  */
struct community_list_handler *community_list_init(void)
{
	struct community_list_handler *ch;
	ch = XCALLOC(MTYPE_COMMUNITY_LIST_HANDLER,
		     sizeof(struct community_list_handler));

	ch->community_list.hash =
		hash_create_size(4, bgp_clist_hash_key_community_list,
				 bgp_clist_hash_cmp_community_list,
				 "Community List Number Quick Lookup");

	ch->extcommunity_list.hash =
		hash_create_size(4, bgp_clist_hash_key_community_list,
				 bgp_clist_hash_cmp_community_list,
				 "Extended Community List Quick Lookup");

	ch->lcommunity_list.hash =
		hash_create_size(4, bgp_clist_hash_key_community_list,
				 bgp_clist_hash_cmp_community_list,
				 "Large Community List Quick Lookup");

	return ch;
}

/* 【函数】释放整个句柄：对三类 master 分别清空 num / str 两条链表（逐 list 调
 * community_list_delete），再释放其 hash，最后释放 handler 本身。注意先删链表
 * 再 free hash 的顺序——delete 内部还要访问 hash。 */
/* Terminate community-list.  */
void community_list_terminate(struct community_list_handler *ch)
{
	struct community_list_master *cm;
	struct community_list *list;

	cm = &ch->community_list;
	while ((list = cm->num.head) != NULL)
		community_list_delete(cm, list);
	while ((list = cm->str.head) != NULL)
		community_list_delete(cm, list);
	hash_free(cm->hash);

	cm = &ch->lcommunity_list;
	while ((list = cm->num.head) != NULL)
		community_list_delete(cm, list);
	while ((list = cm->str.head) != NULL)
		community_list_delete(cm, list);
	hash_free(cm->hash);

	cm = &ch->extcommunity_list;
	while ((list = cm->num.head) != NULL)
		community_list_delete(cm, list);
	while ((list = cm->str.head) != NULL)
		community_list_delete(cm, list);
	hash_free(cm->hash);

	XFREE(MTYPE_COMMUNITY_LIST_HANDLER, ch);
}

/* 【函数】hash 遍历回调：把每个 bucket 里的 list 名字复制一份塞进 vector
 * （CLI 补全候选集）。data 指向 vector 指针。返回值恒为 1 表示继续遍历。 */
static int bgp_community_list_vector_walker(struct hash_bucket *bucket,
					    void *data)
{
	vector *comps = data;
	struct community_list *list = bucket->data;

	vector_set(*comps, XSTRDUP(MTYPE_COMPLETION, list->name));

	return 1;
}

/* 【函数】CLI 补全：把已配置的全部 standard community-list 名字收集为补全候选
 * （用于 COMMUNITY_LIST_NAME 令牌的 Tab 提示）。 */
static void bgp_community_list_cmd_completion(vector comps,
					      struct cmd_token *token)
{
	struct community_list_master *cm;

	cm = community_list_master_lookup(bgp_clist, COMMUNITY_LIST_MASTER);

	hash_walk(cm->hash, bgp_community_list_vector_walker, &comps);
}

/* 【函数】CLI 补全：收集已配置的 large-community-list 名字（LCOMMUNITY_LIST_NAME
 * 令牌的 Tab 提示）。 */
static void bgp_lcommunity_list_cmd_completion(vector comps,
					       struct cmd_token *token)
{
	struct community_list_master *cm;

	cm = community_list_master_lookup(bgp_clist,
					  LARGE_COMMUNITY_LIST_MASTER);

	hash_walk(cm->hash, bgp_community_list_vector_walker, &comps);
}

/* 【函数】CLI 补全：收集已配置的 extcommunity-list 名字
 * （EXTCOMMUNITY_LIST_NAME 令牌的 Tab 提示）。 */
static void bgp_extcommunity_list_cmd_completion(vector comps,
						 struct cmd_token *token)
{
	struct community_list_master *cm;

	cm = community_list_master_lookup(bgp_clist, EXTCOMMUNITY_LIST_MASTER);

	hash_walk(cm->hash, bgp_community_list_vector_walker, &comps);
}

/* 【数据】三套 CLI 补全处理器注册表：把令牌名（COMMUNITY_LIST_NAME 等）绑定到
 * 对应的补全回调。每个数组以一条 {.completions = NULL} 作为结束哨兵。 */
static const struct cmd_variable_handler community_list_handlers[] = {
	{.tokenname = "COMMUNITY_LIST_NAME",
	 .completions = bgp_community_list_cmd_completion},
	{.completions = NULL}};

static const struct cmd_variable_handler lcommunity_list_handlers[] = {
	{.tokenname = "LCOMMUNITY_LIST_NAME",
	 .completions = bgp_lcommunity_list_cmd_completion},
	{.completions = NULL}};

static const struct cmd_variable_handler extcommunity_list_handlers[] = {
	{.tokenname = "EXTCOMMUNITY_LIST_NAME",
	 .completions = bgp_extcommunity_list_cmd_completion},
	{.completions = NULL}};

/* 【函数】向 CLI 框架注册上面三张补全表，让 community / large-community /
 * extcommunity 三种列表名在命令里支持 Tab 补全。进程启动时调用一次即可。 */
void bgp_community_list_command_completion_setup(void)
{
	cmd_variable_handler_register(community_list_handlers);
	cmd_variable_handler_register(lcommunity_list_handlers);
	cmd_variable_handler_register(extcommunity_list_handlers);
}
