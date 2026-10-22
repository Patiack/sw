// SPDX-License-Identifier: GPL-2.0-or-later
/* Prefix list functions.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：plist.c —— prefix-list（前缀列表）匹配引擎
 *
 * 本文件实现前缀列表的存储、增删、命令行 / JSON 展示与（核心的）匹配逻辑。
 * 它与 prefix.c 的分工：prefix.c 提供前缀本身的运算原语（见那边导读），plist.c
 * 在地基之上实现“按序逐条比较前缀并给出 permit / deny”的策略引擎。
 *
 * 核心数据结构（定义在 plist_int.h）：
 *   struct prefix_master    ：按 (地址族, 是否 ORF) 组织的全局总账，本文件用
 *                             四个静态实例分别管理 v4 / v6 的普通列表与 ORF 列表。
 *   struct prefix_list      ：一条列表，含名字、描述、条目双向链表与一棵 pltrie。
 *   struct prefix_list_entry：一条表项（seq / ge / le / type / prefix 等）。
 *
 * 加速匹配：pltrie（前缀 trie）
 *   为把“逐条线性比较”变成按位下推，本文件用 PLC_BITS 位一层的多级表
 *   （struct pltrie_table，每层 PLC_LEN 个槽）表示前缀树。v4 建到第 2 层
 *   （约 /24），v6 建到第 4 层（约 /48）；更深的部分挂在链上，仍线性比较。
 *
 * 匹配流程（prefix_list_apply_ext）：
 *   沿 trie 走到对应节点后，依次比较该节点的候选表项（final_chain / up_chain），
 *   第一条命中者决定结果；全部未命中按缺省 deny。命中会更新 hitcnt 统计。
 *
 * 其它：本文件还负责把列表内容以命令行 / JSON 形式输出，以及实现 BGP ORF
 * 所需的编解码（prefix_bgp_orf_*）。与 routemap 的联动通过 route_map_notify_*
 * 通知依赖的 route-map。
 */

#include <zebra.h>

#include "prefix.h"
#include "command.h"
#include "memory.h"
#include "plist.h"
#include "sockunion.h"
#include "buffer.h"
#include "log.h"
#include "routemap.h"
#include "lib/json.h"
#include "libfrr.h"

#include <typesafe.h>
#include "plist_int.h"

DEFINE_MTYPE_STATIC(LIB, PREFIX_LIST, "Prefix List");
DEFINE_MTYPE_STATIC(LIB, MPREFIX_LIST_STR, "Prefix List Str");
DEFINE_MTYPE_STATIC(LIB, PREFIX_LIST_ENTRY, "Prefix List Entry");
DEFINE_MTYPE_STATIC(LIB, PREFIX_LIST_TRIE, "Prefix List Trie Table");

/*
 * trie 分层的参数：
 *   PLC_BITS      ：每层用 8 位索引（即一层覆盖一个字节）。
 *   PLC_LEN       ：每层槽数，1 左移 PLC_BITS 位，即 256。
 *   PLC_MAXLEVELV4：IPv4 建 trie 的层数，2 层约对应 /24。
 *   PLC_MAXLEVELV6：IPv6 建 trie 的层数，4 层约对应 /48。
 *   PLC_MAXLEVEL  ：两者中的较大值 4。
 * 更深的前缀不继续细分，改为挂在链上线性比较。
 */
/* not currently changeable, code assumes bytes further down */
#define PLC_BITS	8
#define PLC_LEN		(1 << PLC_BITS)
#define PLC_MAXLEVELV4	2	/* /24 for IPv4 */
#define PLC_MAXLEVELV6	4	/* /48 for IPv6 */
#define PLC_MAXLEVEL	4	/* max(v4,v6) */

/*
 * trie 中一个“槽”的内容，用联合体节省空间：
 *   中间层节点用 next_table 指向下一级子表；
 *   叶子节点用 final_chain 指向命中该槽的表项链。
 * up_chain 指向“从祖先节点继承下来的候选表项链”，用于回溯匹配。
 */
struct pltrie_entry {
	union {
		struct pltrie_table *next_table;
		struct prefix_list_entry *final_chain;
	};

	struct prefix_list_entry *up_chain;
};

/*
 * trie 的一层：PLC_LEN 个槽。多层嵌套即构成按前缀位下推的查找结构。
 */
struct pltrie_table {
	struct pltrie_entry entries[PLC_LEN];
};

/* Master structure of prefix_list. */
/*
 * 前缀列表的总账（按地址族 / 是否 ORF 各一份）：
 *   recent                 ：最近被修改的列表，用于增量刷新的快捷引用。
 *   add_hook / delete_hook ：列表新增 / 删除时的回调。
 *   trie_depth             ：本族 trie 的层数（v4 为 2，v6 为 4）。
 *   str                    ：以名字为主键、按名称排序的红黑树，存放本族所有列表。
 */
struct prefix_master {
	/* The latest update. */
	struct prefix_list *recent;

	/* Hook function which is executed when new prefix_list is added. */
	void (*add_hook)(struct prefix_list *);

	/* Hook function which is executed when prefix_list is deleted. */
	void (*delete_hook)(struct prefix_list *);

	/* number of bytes that have a trie level */
	size_t trie_depth;

	struct plist_head str;
};
/*
 * 红黑树比较函数的前向声明：以列表名字（name）为键做排序与唯一性判定，
 * 供紧随其后的 DECLARE_RBTREE_UNIQ 生成 plist 红黑树类型使用；实现见文件后部。
 */
static int prefix_list_compare_func(const struct prefix_list *a,
				    const struct prefix_list *b);
DECLARE_RBTREE_UNIQ(plist, struct prefix_list, plist_item,
		    prefix_list_compare_func);

/* Static structure of IPv4 prefix_list's master. */
static struct prefix_master prefix_master_ipv4 = {
	NULL, NULL, NULL, PLC_MAXLEVELV4,
};

/* Static structure of IPv6 prefix-list's master. */
static struct prefix_master prefix_master_ipv6 = {
	NULL, NULL, NULL, PLC_MAXLEVELV6,
};

/* Static structure of BGP ORF prefix_list's master. */
static struct prefix_master prefix_master_orf_v4 = {
	NULL, NULL, NULL, PLC_MAXLEVELV4,
};

/* Static structure of BGP ORF prefix_list's master. */
static struct prefix_master prefix_master_orf_v6 = {
	NULL, NULL, NULL, PLC_MAXLEVELV6,
};

/*
 * 按 (地址族, 是否 ORF) 取对应的 prefix_master 静态实例；不支持的组合返回 NULL。
 */
static struct prefix_master *prefix_master_get(afi_t afi, int orf)
{
	if (afi == AFI_IP)
		return orf ? &prefix_master_orf_v4 : &prefix_master_ipv4;
	if (afi == AFI_IP6)
		return orf ? &prefix_master_orf_v6 : &prefix_master_ipv6;
	return NULL;
}

/*
 * 返回列表名字（内部指针，勿释放）。
 */
const char *prefix_list_name(struct prefix_list *plist)
{
	return plist->name;
}

/*
 * 由列表归属的 master 反推其地址族（v4 相关返回 AFI_IP，否则 AFI_IP6）。
 */
afi_t prefix_list_afi(struct prefix_list *plist)
{
	if (plist->master == &prefix_master_ipv4
	    || plist->master == &prefix_master_orf_v4)
		return AFI_IP;
	return AFI_IP6;
}

/*
 * 红黑树比较函数：按列表名字做 strcmp，用于 master->str 的排序 / 查找。
 */
static int prefix_list_compare_func(const struct prefix_list *a,
				    const struct prefix_list *b)
{
	return strcmp(a->name, b->name);
}

/* Lookup prefix_list from list of prefix_list by name. */
/*
 * 内部查找：在指定 (afi, orf) 的 master 里按名字找列表，找不到返回 NULL。
 * 实现上临时复制一份名字作为查找键（键比较用 strcmp），用完即释放。
 */
static struct prefix_list *prefix_list_lookup_do(afi_t afi, int orf,
						 const char *name)
{
	struct prefix_list *plist, lookup;
	struct prefix_master *master;

	if (name == NULL)
		return NULL;

	master = prefix_master_get(afi, orf);
	if (master == NULL)
		return NULL;

	lookup.name = XSTRDUP(MTYPE_TMP, name);
	plist = plist_find(&master->str, &lookup);
	XFREE(MTYPE_TMP, lookup.name);
	return plist;
}

/*
 * 对外查找普通（非 ORF）前缀列表。
 */
struct prefix_list *prefix_list_lookup(afi_t afi, const char *name)
{
	return prefix_list_lookup_do(afi, 0, name);
}

/*
 * 查找 ORF 用的前缀列表。
 */
struct prefix_list *prefix_bgp_orf_lookup(afi_t afi, const char *name)
{
	return prefix_list_lookup_do(afi, 1, name);
}

/*
 * 分配并清零一条 struct prefix_list（MTYPE_PREFIX_LIST）。
 */
static struct prefix_list *prefix_list_new(void)
{
	struct prefix_list *new;

	new = XCALLOC(MTYPE_PREFIX_LIST, sizeof(struct prefix_list));
	return new;
}

/*
 * 释放 struct prefix_list 结构体本身。
 */
static void prefix_list_free(struct prefix_list *plist)
{
	XFREE(MTYPE_PREFIX_LIST, plist);
}

/*
 * 分配并清零一条 struct prefix_list_entry（MTYPE_PREFIX_LIST_ENTRY）。
 */
struct prefix_list_entry *prefix_list_entry_new(void)
{
	struct prefix_list_entry *new;

	new = XCALLOC(MTYPE_PREFIX_LIST_ENTRY,
		      sizeof(struct prefix_list_entry));
	return new;
}

/*
 * 释放一条表项结构体。
 */
void prefix_list_entry_free(struct prefix_list_entry *pentry)
{
	XFREE(MTYPE_PREFIX_LIST_ENTRY, pentry);
}

/* Insert new prefix list to list of prefix_list.  Each prefix_list
   is sorted by the name. */
/*
 * 新建并插入一条 prefix_list：分配结构、复制名字、关联 master，
 * 并为它分配空的 trie 根表，最后按名字插入 master->str 红黑树。返回新列表。
 */
static struct prefix_list *prefix_list_insert(afi_t afi, int orf,
					      const char *name)
{
	struct prefix_list *plist;
	struct prefix_master *master;

	master = prefix_master_get(afi, orf);
	if (master == NULL)
		return NULL;

	/* Allocate new prefix_list and copy given name. */
	plist = prefix_list_new();
	plist->name = XSTRDUP(MTYPE_MPREFIX_LIST_STR, name);
	plist->master = master;
	plist->trie =
		XCALLOC(MTYPE_PREFIX_LIST_TRIE, sizeof(struct pltrie_table));

	plist_add(&master->str, plist);

	return plist;
}

/*
 * 取得列表：已存在则返回，否则新建。是命令行“进入 / 创建 prefix-list”的入口。
 */
struct prefix_list *prefix_list_get(afi_t afi, int orf, const char *name)
{
	struct prefix_list *plist;

	plist = prefix_list_lookup_do(afi, orf, name);

	if (plist == NULL)
		plist = prefix_list_insert(afi, orf, name);
	return plist;
}

/* 前向声明：把一条表项从其所属列表的 trie 中注销，实现见文件后部。 */
static void prefix_list_trie_del(struct prefix_list *plist,
				 struct prefix_list_entry *pentry);

/* Delete prefix-list from prefix_list_master and free it. */
/*
 * 删除一条 prefix_list 及其全部表项：
 * 遍历表项——通知 routemap 依赖变化、从 trie 注销、释放表项并递减计数；
 * 再从红黑树摘除、释放描述 / 名字 / trie，清空 master->recent，
 * 通知删除钩子与 routemap，最后释放列表结构本身。
 */
void prefix_list_delete(struct prefix_list *plist)
{
	struct prefix_master *master;
	struct prefix_list_entry *pentry;
	struct prefix_list_entry *next;

	/* If prefix-list contain prefix_list_entry free all of it. */
	for (pentry = plist->head; pentry; pentry = next) {
		route_map_notify_pentry_dependencies(plist->name, pentry,
						     RMAP_EVENT_PLIST_DELETED);
		next = pentry->next;
		prefix_list_trie_del(plist, pentry);
		prefix_list_entry_free(pentry);
		plist->count--;
	}

	master = plist->master;

	plist_del(&master->str, plist);

	XFREE(MTYPE_TMP, plist->desc);

	/* Make sure master's recent changed prefix-list information is
	   cleared. */
	master->recent = NULL;

	route_map_notify_dependencies(plist->name, RMAP_EVENT_PLIST_DELETED);

	if (master->delete_hook)
		(*master->delete_hook)(plist);

	XFREE(MTYPE_MPREFIX_LIST_STR, plist->name);

	XFREE(MTYPE_PREFIX_LIST_TRIE, plist->trie);

	prefix_list_free(plist);
}

static struct prefix_list_entry *
/*
 * 按给定参数新建一条表项：分配结构、按需置 any 标志、拷贝前缀，
 * 并填入 type / seq / le / ge。返回该表项（此时尚未挂链）。
 */
prefix_list_entry_make(struct prefix *prefix, enum prefix_list_type type,
		       int64_t seq, int le, int ge, bool any)
{
	struct prefix_list_entry *pentry;

	pentry = prefix_list_entry_new();

	if (any)
		pentry->any = true;

	prefix_copy(&pentry->prefix, prefix);
	pentry->type = type;
	pentry->seq = seq;
	pentry->le = le;
	pentry->ge = ge;

	return pentry;
}

/* Add hook function. */
/*
 * 注册列表新增回调，同时设置到 v4 与 v6 两个 master 上。
 */
void prefix_list_add_hook(void (*func)(struct prefix_list *plist))
{
	prefix_master_ipv4.add_hook = func;
	prefix_master_ipv6.add_hook = func;
}

/* Delete hook function. */
/*
 * 注册列表删除回调，同时设置到 v4 与 v6 两个 master 上。
 */
void prefix_list_delete_hook(void (*func)(struct prefix_list *plist))
{
	prefix_master_ipv4.delete_hook = func;
	prefix_master_ipv6.delete_hook = func;
}

/* Calculate new sequential number. */
/*
 * 计算该列表下一个可用序号：取当前最大 seq，向上取到 5 的倍数再加 5
 * （即按 5 递增），并把上限封顶到 UINT_MAX。
 */
int64_t prefix_new_seq_get(struct prefix_list *plist)
{
	int64_t maxseq;
	int64_t newseq;
	struct prefix_list_entry *pentry;

	maxseq = 0;

	for (pentry = plist->head; pentry; pentry = pentry->next) {
		if (maxseq < pentry->seq)
			maxseq = pentry->seq;
	}

	newseq = ((maxseq / 5) * 5) + 5;

	return (newseq > UINT_MAX) ? UINT_MAX : newseq;
}

/* Return prefix list entry which has same seq number. */
/*
 * 在列表中查找序号等于 seq 的表项，找到则返回之，否则返回 NULL。
 */
static struct prefix_list_entry *prefix_seq_check(struct prefix_list *plist,
						  int64_t seq)
{
	struct prefix_list_entry *pentry;

	for (pentry = plist->head; pentry; pentry = pentry->next)
		if (pentry->seq == seq)
			return pentry;
	return NULL;
}

struct prefix_list_entry *
/*
 * 按 (前缀, type, seq, le, ge) 精确查找表项：前缀与类型必须一致；
 * seq 小于 0 表示不限定序号，否则须匹配；le / ge 也必须一致。找不到返回 NULL。
 */
prefix_list_entry_lookup(struct prefix_list *plist, struct prefix *prefix,
			 enum prefix_list_type type, int64_t seq,
			 int le, int ge)
{
	struct prefix_list_entry *pentry;

	for (pentry = plist->head; pentry; pentry = pentry->next)
		if (prefix_same(&pentry->prefix, prefix)
		    && pentry->type == type) {
			if (seq >= 0 && pentry->seq != seq)
				continue;

			if (pentry->le != le)
				continue;
			if (pentry->ge != ge)
				continue;

			return pentry;
		}

	return NULL;
}

/*
 * 在前缀 trie 的某个节点上，对“受该表项影响的所有槽”调用回调 fn。
 * 若剩余有效位数超过一层（validbits > PLC_BITS），说明该前缀在本层已完整，
 * 只需处理精确匹配的那一个槽（final_chain）；否则按剩余位数算出掩码范围，
 * 对范围内每个槽的 up_chain 调用 fn（这些槽都覆盖该前缀）。
 */
static void trie_walk_affected(size_t validbits, struct pltrie_table *table,
			       uint8_t byte, struct prefix_list_entry *object,
			       void (*fn)(struct prefix_list_entry *object,
					  struct prefix_list_entry **updptr))
{
	uint8_t mask;
	uint16_t bwalk;

	if (validbits > PLC_BITS) {
		fn(object, &table->entries[byte].final_chain);
		return;
	}

	mask = (1 << (8 - validbits)) - 1;
	for (bwalk = byte & ~mask; bwalk <= byte + mask; bwalk++) {
		fn(object, &table->entries[bwalk].up_chain);
	}
}

/*
 * trie 卸载回调：沿 next_best 链找到 object，把它从链上摘除。
 */
static void trie_uninstall_fn(struct prefix_list_entry *object,
			      struct prefix_list_entry **updptr)
{
	for (; *updptr; updptr = &(*updptr)->next_best)
		if (*updptr == object) {
			*updptr = object->next_best;
			break;
		}
}

/*
 * 判断某层 trie 表是否为空（所有槽既无子表也无候选链），空则可回收。
 */
static int trie_table_empty(struct pltrie_table *table)
{
	size_t i;
	for (i = 0; i < PLC_LEN; i++)
		if (table->entries[i].next_table || table->entries[i].up_chain)
			return 0;
	return 1;
}

/*
 * 从 trie 中注销一条表项：沿前缀字节逐层下推（直到最大层或不足一层），
 * 在命中节点上用 trie_uninstall_fn 摘除；随后自底向上回收变空的层表。
 */
static void prefix_list_trie_del(struct prefix_list *plist,
				 struct prefix_list_entry *pentry)
{
	size_t depth, maxdepth = plist->master->trie_depth;
	uint8_t *bytes = pentry->prefix.u.val;
	size_t validbits = pentry->prefix.prefixlen;
	struct pltrie_table *table, **tables[PLC_MAXLEVEL];

	table = plist->trie;
	for (depth = 0; validbits > PLC_BITS && depth < maxdepth - 1; depth++) {
		uint8_t byte = bytes[depth];
		assert(table->entries[byte].next_table);

		tables[depth + 1] = &table->entries[byte].next_table;
		table = table->entries[byte].next_table;

		validbits -= PLC_BITS;
	}

	trie_walk_affected(validbits, table, bytes[depth], pentry,
			   trie_uninstall_fn);

	for (; depth > 0; depth--)
		if (trie_table_empty(*tables[depth])) {
			XFREE(MTYPE_PREFIX_LIST_TRIE, *tables[depth]);
		}
}

/**
 * Find duplicated prefix entry (same prefix but different entry) in prefix
 * list.
 */
/*
 * 判断列表中是否已有“同前缀但不同对象”的重复表项：
 * 下推到目标节点，遍历其候选链（跳过自身），只要出现 prefix_same 的即算重复。
 */
static bool prefix_list_entry_is_duplicated(struct prefix_list *list,
					    struct prefix_list_entry *entry)
{
	size_t depth, maxdepth = list->master->trie_depth;
	uint8_t byte, *bytes = entry->prefix.u.val;
	size_t validbits = entry->prefix.prefixlen;
	struct pltrie_table *table = list->trie;
	struct prefix_list_entry *pentry;

	for (depth = 0; validbits > PLC_BITS && depth < maxdepth - 1; depth++) {
		byte = bytes[depth];
		if (!table->entries[byte].next_table)
			return NULL;

		table = table->entries[byte].next_table;
		validbits -= PLC_BITS;
	}

	byte = bytes[depth];
	if (validbits > PLC_BITS)
		pentry = table->entries[byte].final_chain;
	else
		pentry = table->entries[byte].up_chain;

	for (; pentry; pentry = pentry->next_best) {
		if (pentry == entry)
			continue;
		if (prefix_same(&pentry->prefix, &entry->prefix))
			return true;
	}

	return false;
}

/*
 * 从列表中删除一条表项：
 * 先从 trie 注销，再把它从双向链表摘除；若非重复表项则通知 routemap；
 * 随后释放并递减计数。update_list 非 0 时还要刷新依赖、调用删除钩子，并在列表
 * 已空且无描述时整表删除，否则把 master->recent 置为本列表。
 */
void prefix_list_entry_delete(struct prefix_list *plist,
			      struct prefix_list_entry *pentry,
			      int update_list)
{
	bool duplicate;

	if (plist == NULL || pentry == NULL)
		return;

	duplicate = prefix_list_entry_is_duplicated(plist, pentry);

	prefix_list_trie_del(plist, pentry);

	if (pentry->prev)
		pentry->prev->next = pentry->next;
	else
		plist->head = pentry->next;
	if (pentry->next)
		pentry->next->prev = pentry->prev;
	else
		plist->tail = pentry->prev;

	if (!duplicate)
		route_map_notify_pentry_dependencies(plist->name, pentry,
						     RMAP_EVENT_PLIST_DELETED);

	prefix_list_entry_free(pentry);

	plist->count--;

	if (update_list) {
		route_map_notify_dependencies(plist->name,
					      RMAP_EVENT_PLIST_DELETED);
		if (plist->master->delete_hook)
			(*plist->master->delete_hook)(plist);

		if (plist->head == NULL && plist->tail == NULL
		    && plist->desc == NULL)
			prefix_list_delete(plist);
		else
			plist->master->recent = plist;
	}
}

/*
 * trie 安装回调：把 object 按“前缀长度升序、同长度按 seq 升序”插入候选链，
 * 这样遍历候选链时短的、序号小的表项优先，符合 prefix-list 的匹配顺序。
 */
static void trie_install_fn(struct prefix_list_entry *object,
			    struct prefix_list_entry **updptr)
{
	while (*updptr) {
		if (*updptr == object)
			return;
		if ((*updptr)->prefix.prefixlen < object->prefix.prefixlen)
			break;
		if ((*updptr)->prefix.prefixlen == object->prefix.prefixlen
		    && (*updptr)->seq > object->seq)
			break;
		updptr = &(*updptr)->next_best;
	}

	if (!object->next_best)
		object->next_best = *updptr;
	else
		assert(object->next_best == *updptr || !*updptr);

	*updptr = object;
}

/*
 * 把一条表项加入 trie：从根沿前缀字节逐层下推，按需创建子表，
 * 到达合适层后调用 trie_install_fn 挂入候选链。
 */
static void prefix_list_trie_add(struct prefix_list *plist,
				 struct prefix_list_entry *pentry)
{
	size_t depth = plist->master->trie_depth;
	uint8_t *bytes = pentry->prefix.u.val;
	size_t validbits = pentry->prefix.prefixlen;
	struct pltrie_table *table;

	table = plist->trie;
	while (validbits > PLC_BITS && depth > 1) {
		if (!table->entries[*bytes].next_table)
			table->entries[*bytes].next_table =
				XCALLOC(MTYPE_PREFIX_LIST_TRIE,
					sizeof(struct pltrie_table));
		table = table->entries[*bytes].next_table;
		bytes++;
		depth--;
		validbits -= PLC_BITS;
	}

	trie_walk_affected(validbits, table, *bytes, pentry, trie_install_fn);
}

/*
 * 把一条表项加入列表：若 seq 为 -1 则自动分配序号；按 seq 找到插入点
 * （同时删除同 seq 的旧表项），把它插入双向链表、加入 trie，递增计数，
 * 通知 routemap 新增依赖、调用新增钩子，并把本列表记为 master->recent。
 */
static void prefix_list_entry_add(struct prefix_list *plist,
				  struct prefix_list_entry *pentry)
{
	struct prefix_list_entry *replace;
	struct prefix_list_entry *point;

	/* Automatic asignment of seq no. */
	if (pentry->seq == -1)
		pentry->seq = prefix_new_seq_get(plist);

	if (plist->tail && pentry->seq > plist->tail->seq)
		point = NULL;
	else {
		/* Is there any same seq prefix list entry? */
		replace = prefix_seq_check(plist, pentry->seq);
		if (replace)
			prefix_list_entry_delete(plist, replace, 0);

		/* Check insert point. */
		for (point = plist->head; point; point = point->next)
			if (point->seq >= pentry->seq)
				break;
	}

	/* In case of this is the first element of the list. */
	pentry->next = point;

	if (point) {
		if (point->prev)
			point->prev->next = pentry;
		else
			plist->head = pentry;

		pentry->prev = point->prev;
		point->prev = pentry;
	} else {
		if (plist->tail)
			plist->tail->next = pentry;
		else
			plist->head = pentry;

		pentry->prev = plist->tail;
		plist->tail = pentry;
	}

	prefix_list_trie_add(plist, pentry);

	/* Increment count. */
	plist->count++;

	route_map_notify_pentry_dependencies(plist->name, pentry,
					     RMAP_EVENT_PLIST_ADDED);

	/* Run hook function. */
	if (plist->master->add_hook)
		(*plist->master->add_hook)(plist);

	route_map_notify_dependencies(plist->name, RMAP_EVENT_PLIST_ADDED);
	plist->master->recent = plist;
}

/**
 * Prefix list entry update start procedure:
 * Remove entry from previosly installed master list, tries and notify
 * observers.
 *
 * \param[in] ple prefix list entry.
 */
/*
 * 更新前的“下架”过程：若表项已安装，则先算出是否重复、从 trie 与链表中摘除，
 * 按需通知 routemap 删除依赖、调用删除钩子，更新最近列表引用，
 * 最后清空 next_best 并把 installed 置 false。与 update_finish 成对使用。
 */
void prefix_list_entry_update_start(struct prefix_list_entry *ple)
{
	struct prefix_list *pl = ple->pl;
	bool duplicate;

	/* Not installed, nothing to do. */
	if (!ple->installed)
		return;

	duplicate = prefix_list_entry_is_duplicated(pl, ple);

	prefix_list_trie_del(pl, ple);

	/* List manipulation: shameless copy from `prefix_list_entry_delete`. */
	if (ple->prev)
		ple->prev->next = ple->next;
	else
		pl->head = ple->next;
	if (ple->next)
		ple->next->prev = ple->prev;
	else
		pl->tail = ple->prev;

	if (!duplicate)
		route_map_notify_pentry_dependencies(pl->name, ple,
						     RMAP_EVENT_PLIST_DELETED);
	pl->count--;

	route_map_notify_dependencies(pl->name, RMAP_EVENT_PLIST_DELETED);
	if (pl->master->delete_hook)
		(*pl->master->delete_hook)(pl);

	if (pl->head || pl->tail || pl->desc)
		pl->master->recent = pl;

	ple->next_best = NULL;
	ple->installed = false;
}

/**
 * Prefix list entry update finish procedure:
 * Add entry back master list, to the trie, notify observers and call master
 * hook.
 *
 * \param[in] ple prefix list entry.
 */
/*
 * 更新后的“重新上架”过程：若尚未安装且前缀族为 IPv4 / IPv6，则按 seq 找插入点
 * 进入链表、加入 trie、递增计数，通知 routemap 新增依赖与新增钩子，
 * 更新最近引用，并把 installed 置 true。
 */
void prefix_list_entry_update_finish(struct prefix_list_entry *ple)
{
	struct prefix_list *pl = ple->pl;
	struct prefix_list_entry *point;

	/* Already installed, nothing to do. */
	if (ple->installed)
		return;

	/*
	 * Check if the entry is installable:
	 * We can only install entry if at least the prefix is provided (IPv4
	 * or IPv6).
	 */
	if (ple->prefix.family != AF_INET && ple->prefix.family != AF_INET6)
		return;

	/* List manipulation: shameless copy from `prefix_list_entry_add`. */
	if (pl->tail && ple->seq > pl->tail->seq)
		point = NULL;
	else {
		/* Check insert point. */
		for (point = pl->head; point; point = point->next)
			if (point->seq >= ple->seq)
				break;
	}

	/* In case of this is the first element of the list. */
	ple->next = point;

	if (point) {
		if (point->prev)
			point->prev->next = ple;
		else
			pl->head = ple;

		ple->prev = point->prev;
		point->prev = ple;
	} else {
		if (pl->tail)
			pl->tail->next = ple;
		else
			pl->head = ple;

		ple->prev = pl->tail;
		pl->tail = ple;
	}

	prefix_list_trie_add(pl, ple);
	pl->count++;

	route_map_notify_pentry_dependencies(pl->name, ple,
					     RMAP_EVENT_PLIST_ADDED);

	/* Run hook function. */
	if (pl->master->add_hook)
		(*pl->master->add_hook)(pl);

	route_map_notify_dependencies(pl->name, RMAP_EVENT_PLIST_ADDED);
	pl->master->recent = pl;

	ple->installed = true;
}

/**
 * Same as `prefix_list_entry_delete` but without `free()`ing the list if its
 * empty.
 *
 * \param[in] ple prefix list entry.
 */
/*
 * 与 prefix_list_entry_delete 类似，但列表变空时也不删除整表：
 * 先做下架处理（update_start），再释放表项内存。用于需保留空列表壳的场景。
 */
void prefix_list_entry_delete2(struct prefix_list_entry *ple)
{
	/* Does the boiler plate list removal and entry removal notification. */
	prefix_list_entry_update_start(ple);

	/* Effective `free()` memory. */
	prefix_list_entry_free(ple);
}

/* Return string of prefix_list_type. */
/*
 * 把表项类型转成字符串（permit / deny），未知返回空串。
 */
static const char *prefix_list_type_str(struct prefix_list_entry *pentry)
{
	switch (pentry->type) {
	case PREFIX_PERMIT:
		return "permit";
	case PREFIX_DENY:
		return "deny";
	default:
		return "";
	}
}

/*
 * 判断单个表项是否匹配前缀 p：
 * 族必须相同且 prefix_match 成立；address_mode 为真时到此即算匹配（忽略长度约束）；
 * 否则 le / ge 都未设置时要求长度精确相等，设置了则要求 p 的长度落在 [ge, le] 内。
 */
static int prefix_list_entry_match(struct prefix_list_entry *pentry,
				   const struct prefix *p, bool address_mode)
{
	int ret;

	if (pentry->prefix.family != p->family)
		return 0;

	ret = prefix_match(&pentry->prefix, p);
	if (!ret)
		return 0;

	if (address_mode)
		return 1;

	/* In case of le nor ge is specified, exact match is performed. */
	if (!pentry->le && !pentry->ge) {
		if (pentry->prefix.prefixlen != p->prefixlen)
			return 0;
	} else {
		if (pentry->le)
			if (p->prefixlen > pentry->le)
				return 0;

		if (pentry->ge)
			if (p->prefixlen < pentry->ge)
				return 0;
	}
	return 1;
}

/*
 * 前缀列表匹配主入口。沿 trie 下推定位候选链，按“长度升序、seq 升序”逐条用
 * prefix_list_entry_match 比较，第一条命中的表项即决定结果；若 which 非空则回填
 * 该表项指针；命中计数 hitcnt 递增。返回 PREFIX_PERMIT 或 PREFIX_DENY
 * （未命中一律返回 PREFIX_DENY）。address_mode 见 plist.h 说明。
 */
enum prefix_list_type prefix_list_apply_ext(
	struct prefix_list *plist,
	const struct prefix_list_entry **which,
	union prefixconstptr object,
	bool address_mode)
{
	struct prefix_list_entry *pentry, *pbest = NULL;

	const struct prefix *p = object.p;
	const uint8_t *byte = p->u.val;
	size_t depth;
	size_t validbits = p->prefixlen;
	struct pltrie_table *table;

	if (plist == NULL) {
		if (which)
			*which = NULL;
		return PREFIX_DENY;
	}

	if (plist->count == 0) {
		if (which)
			*which = NULL;
		return PREFIX_PERMIT;
	}

	depth = plist->master->trie_depth;
	table = plist->trie;
	while (1) {
		for (pentry = table->entries[*byte].up_chain; pentry;
		     pentry = pentry->next_best) {
			if (pbest && pbest->seq < pentry->seq)
				continue;
			if (prefix_list_entry_match(pentry, p, address_mode))
				pbest = pentry;
		}

		if (validbits <= PLC_BITS)
			break;
		validbits -= PLC_BITS;

		if (--depth) {
			if (!table->entries[*byte].next_table)
				break;

			table = table->entries[*byte].next_table;
			byte++;
			continue;
		}

		for (pentry = table->entries[*byte].final_chain; pentry;
		     pentry = pentry->next_best) {
			if (pbest && pbest->seq < pentry->seq)
				continue;
			if (prefix_list_entry_match(pentry, p, address_mode))
				pbest = pentry;
		}
		break;
	}

	if (which) {
		if (pbest)
			*which = pbest;
		else
			*which = NULL;
	}

	if (pbest == NULL)
		return PREFIX_DENY;

	pbest->hitcnt++;
	return pbest->type;
}

/*
 * 调试用：把列表内容打印到标准输出（含 seq / 类型 / 前缀与 ge / le）。
 * 标注 __attribute__((unused)) 以避免未被调用时产生告警。
 */
static void __attribute__((unused)) prefix_list_print(struct prefix_list *plist)
{
	struct prefix_list_entry *pentry;

	if (plist == NULL)
		return;

	printf("ip prefix-list %s: %d entries\n", plist->name, plist->count);

	for (pentry = plist->head; pentry; pentry = pentry->next) {
		if (pentry->any)
			printf("any %s\n", prefix_list_type_str(pentry));
		else {
			struct prefix *p;

			p = &pentry->prefix;

			printf("  seq %lld %s %pFX", (long long)pentry->seq,
			       prefix_list_type_str(pentry), p);
			if (pentry->ge)
				printf(" ge %d", pentry->ge);
			if (pentry->le)
				printf(" le %d", pentry->le);
			printf("\n");
		}
	}
}

/* Return 1 when plist already include pentry policy. */
static struct prefix_list_entry *
/*
 * 检查列表中是否已存在与 new “同前缀、同类型、同 le / ge 但 seq 不同”的表项，
 * 有则返回那条已有表项（视为重复），否则返回 NULL。new->seq 为 -1 时先算出待用 seq。
 */
prefix_entry_dup_check(struct prefix_list *plist, struct prefix_list_entry *new)
{
	size_t depth, maxdepth = plist->master->trie_depth;
	uint8_t byte, *bytes = new->prefix.u.val;
	size_t validbits = new->prefix.prefixlen;
	struct pltrie_table *table;
	struct prefix_list_entry *pentry;
	int64_t seq = 0;

	if (new->seq == -1)
		seq = prefix_new_seq_get(plist);
	else
		seq = new->seq;

	table = plist->trie;
	for (depth = 0; validbits > PLC_BITS && depth < maxdepth - 1; depth++) {
		byte = bytes[depth];
		if (!table->entries[byte].next_table)
			return NULL;

		table = table->entries[byte].next_table;
		validbits -= PLC_BITS;
	}

	byte = bytes[depth];
	if (validbits > PLC_BITS)
		pentry = table->entries[byte].final_chain;
	else
		pentry = table->entries[byte].up_chain;

	for (; pentry; pentry = pentry->next_best) {
		if (prefix_same(&pentry->prefix, &new->prefix)
		    && pentry->type == new->type && pentry->le == new->le
		    && pentry->ge == new->ge && pentry->seq != seq)
			return pentry;
	}
	return NULL;
}

/*
 * show 命令的输出模式：普通 / 汇总 / 详细 / 按序号 / 只显示更具体前缀 /
 * 只显示首个匹配。
 */
enum display_type {
	normal_display,
	summary_display,
	detail_display,
	sequential_display,
	longer_display,
	first_match_display
};

/*
 * 按指定显示模式把列表条目输出到 vty（或 JSON）。dtype 决定输出粒度，
 * seqnum 用于“首个匹配”模式限定起始序号。
 */
static void vty_show_prefix_entry(struct vty *vty, json_object *json, afi_t afi,
				  struct prefix_list *plist,
				  struct prefix_master *master,
				  enum display_type dtype, int seqnum)
{
	struct prefix_list_entry *pentry;
	json_object *json_pl = NULL;

	/* Print the name of the protocol */
	if (json) {
		json_pl = json_object_new_object();
		json_object_object_add(json, plist->name, json_pl);
	} else
		vty_out(vty, "%s: ", frr_protoname);

	if (dtype == normal_display) {
		if (json) {
			json_object_string_add(json_pl, "addressFamily",
					       afi2str(afi));
			json_object_int_add(json_pl, "entries", plist->count);
			if (plist->desc)
				json_object_string_add(json_pl, "description",
						       plist->desc);
		} else {
			vty_out(vty, "ip%s prefix-list %s: %d entries\n",
				afi == AFI_IP ? "" : "v6", plist->name,
				plist->count);
			if (plist->desc)
				vty_out(vty, "   Description: %s\n",
					plist->desc);
		}
	} else if (dtype == summary_display || dtype == detail_display) {
		if (json) {
			json_object_string_add(json_pl, "addressFamily",
					       afi2str(afi));
			if (plist->desc)
				json_object_string_add(json_pl, "description",
						       plist->desc);
			json_object_int_add(json_pl, "count", plist->count);
			json_object_int_add(json_pl, "rangeEntries",
					    plist->rangecount);
			json_object_int_add(json_pl, "sequenceStart",
					    plist->head ? plist->head->seq : 0);
			json_object_int_add(json_pl, "sequenceEnd",
					    plist->tail ? plist->tail->seq : 0);
		} else {
			vty_out(vty, "ip%s prefix-list %s:\n",
				afi == AFI_IP ? "" : "v6", plist->name);

			if (plist->desc)
				vty_out(vty, "   Description: %s\n",
					plist->desc);

			vty_out(vty,
				"   count: %d, range entries: %d, sequences: %" PRId64
				" - %" PRId64 "\n",
				plist->count, plist->rangecount,
				plist->head ? plist->head->seq : 0,
				plist->tail ? plist->tail->seq : 0);
		}
	}

	if (dtype != summary_display) {
		json_object *json_entries = NULL;

		if (json) {
			json_entries = json_object_new_array();
			json_object_object_add(json_pl, "entries",
					       json_entries);
		}

		for (pentry = plist->head; pentry; pentry = pentry->next) {
			if (dtype == sequential_display
			    && pentry->seq != seqnum)
				continue;

			if (json) {
				json_object *json_entry;

				json_entry = json_object_new_object();
				json_object_array_add(json_entries, json_entry);

				json_object_int_add(json_entry,
						    "sequenceNumber",
						    pentry->seq);
				json_object_string_add(
					json_entry, "type",
					prefix_list_type_str(pentry));
				json_object_string_addf(json_entry, "prefix",
							"%pFX",
							&pentry->prefix);

				if (pentry->ge)
					json_object_int_add(
						json_entry,
						"minimumPrefixLength",
						pentry->ge);
				if (pentry->le)
					json_object_int_add(
						json_entry,
						"maximumPrefixLength",
						pentry->le);

				if (dtype == detail_display
				    || dtype == sequential_display) {
					json_object_int_add(json_entry,
							    "hitCount",
							    pentry->hitcnt);
					json_object_int_add(json_entry,
							    "referenceCount",
							    pentry->refcnt);
				}
			} else {
				vty_out(vty, "   ");

				vty_out(vty, "seq %" PRId64 " ", pentry->seq);

				vty_out(vty, "%s ",
					prefix_list_type_str(pentry));

				if (pentry->any)
					vty_out(vty, "any");
				else {
					struct prefix *p = &pentry->prefix;

					vty_out(vty, "%pFX", p);

					if (pentry->ge)
						vty_out(vty, " ge %d",
							pentry->ge);
					if (pentry->le)
						vty_out(vty, " le %d",
							pentry->le);
				}

				if (dtype == detail_display
				    || dtype == sequential_display)
					vty_out(vty,
						" (hit count: %ld, refcount: %ld)",
						pentry->hitcnt, pentry->refcnt);

				vty_out(vty, "\n");
			}
		}
	}
}

/*
 * 实现 "show [ip|ipv6] prefix-list"：无名字时遍历本族所有列表输出，
 * 有名字时只输出该列表（找不到报错）。uj 为真则输出 JSON。返回命令结果码。
 */
static int vty_show_prefix_list(struct vty *vty, afi_t afi, const char *name,
				const char *seq, enum display_type dtype,
				bool uj)
{
	struct prefix_list *plist;
	struct prefix_master *master;
	int64_t seqnum = 0;
	json_object *json = NULL;

	master = prefix_master_get(afi, 0);
	if (master == NULL)
		return CMD_WARNING;

	if (uj)
		json = json_object_new_object();

	if (seq)
		seqnum = (int64_t)atol(seq);

	if (name) {
		plist = prefix_list_lookup(afi, name);
		if (!plist) {
			if (uj)
				vty_json(vty, json);
			else
				vty_out(vty,
					"%% Can't find specified prefix-list\n");
			return CMD_WARNING;
		}
		vty_show_prefix_entry(vty, json, afi, plist, master, dtype,
				      seqnum);
	} else {
		if (dtype == detail_display || dtype == summary_display) {
			if (master->recent && !uj)
				vty_out(vty,
					"Prefix-list with the last deletion/insertion: %s\n",
					master->recent->name);
		}

		frr_each (plist, &master->str, plist)
			vty_show_prefix_entry(vty, json, afi, plist, master,
					      dtype, seqnum);
	}

	return vty_json(vty, json);
}

/*
 * 实现按前缀过滤显示：普通 / 首个匹配模式下用 prefix_list_entry_match 判断；
 * longer 模式下显示比给定前缀更具体的条目。首个匹配模式命中一条即返回。
 */
static int vty_show_prefix_list_prefix(struct vty *vty, afi_t afi,
				       const char *name, const char *prefix,
				       enum display_type type)
{
	struct prefix_list *plist;
	struct prefix_list_entry *pentry;
	struct prefix p;
	int ret;
	int match;

	plist = prefix_list_lookup(afi, name);
	if (!plist) {
		vty_out(vty, "%% Can't find specified prefix-list\n");
		return CMD_WARNING;
	}

	ret = str2prefix(prefix, &p);
	if (ret <= 0) {
		vty_out(vty, "%% prefix is malformed\n");
		return CMD_WARNING;
	}

	for (pentry = plist->head; pentry; pentry = pentry->next) {
		match = 0;

		if (type == normal_display || type == first_match_display)
			if (prefix_list_entry_match(pentry, &p, false)) {
				pentry->hitcnt++;
				match = 1;
			}

		if (type == longer_display) {
			if ((p.family == pentry->prefix.family)
			    && (prefix_match(&p, &pentry->prefix)))
				match = 1;
		}

		if (match) {
			vty_out(vty, "   seq %" PRId64 " %s ", pentry->seq,
				prefix_list_type_str(pentry));

			if (pentry->any)
				vty_out(vty, "any");
			else {
				struct prefix *pf = &pentry->prefix;

				vty_out(vty, "%pFX", pf);

				if (pentry->ge)
					vty_out(vty, " ge %d", pentry->ge);
				if (pentry->le)
					vty_out(vty, " le %d", pentry->le);
			}

			if (type == normal_display
			    || type == first_match_display)
				vty_out(vty, " (hit count: %ld, refcount: %ld)",
					pentry->hitcnt, pentry->refcnt);

			vty_out(vty, "\n");

			if (type == first_match_display)
				return CMD_SUCCESS;
		}
	}
	return CMD_SUCCESS;
}

/*
 * 实现 "clear prefix-list" 计数清零：无名字无前缀时清空全族所有条目的 hitcnt；
 * 指定名字时可只清该列表，或仅清与给定前缀匹配的条目的 hitcnt。
 */
static int vty_clear_prefix_list(struct vty *vty, afi_t afi, const char *name,
				 const char *prefix)
{
	struct prefix_master *master;
	struct prefix_list *plist;
	struct prefix_list_entry *pentry;
	int ret;
	struct prefix p;

	master = prefix_master_get(afi, 0);
	if (master == NULL)
		return CMD_WARNING;

	if (name == NULL && prefix == NULL) {
		frr_each (plist, &master->str, plist)
			for (pentry = plist->head; pentry;
			     pentry = pentry->next)
				pentry->hitcnt = 0;
	} else {
		plist = prefix_list_lookup(afi, name);
		if (!plist) {
			vty_out(vty, "%% Can't find specified prefix-list\n");
			return CMD_WARNING;
		}

		if (prefix) {
			ret = str2prefix(prefix, &p);
			if (ret <= 0) {
				vty_out(vty, "%% prefix is malformed\n");
				return CMD_WARNING;
			}
		}

		for (pentry = plist->head; pentry; pentry = pentry->next) {
			if (prefix) {
				if (pentry->prefix.family == p.family
				    && prefix_match(&pentry->prefix, &p))
					pentry->hitcnt = 0;
			} else
				pentry->hitcnt = 0;
		}
	}
	return CMD_SUCCESS;
}

#include "lib/plist_clippy.c"

/*
 * “show ip prefix-list” 命令定义：可选列表名、序号与 json 输出，
 * 最终调用 vty_show_prefix_list 完成输出。
 */
DEFPY_NOSH (show_ip_prefix_list,
       show_ip_prefix_list_cmd,
       "show ip prefix-list [PREFIXLIST4_NAME$name [seq$dseq (1-4294967295)$arg]] [json$uj]",
       SHOW_STR
       IP_STR
       PREFIX_LIST_STR
       "Name of a prefix list\n"
       "sequence number of an entry\n"
       "Sequence number\n"
       JSON_STR)
{
	enum display_type dtype = normal_display;

	if (dseq)
		dtype = sequential_display;

	return vty_show_prefix_list(vty, AFI_IP, name, arg_str, dtype,
				    !!uj);
}

/*
 * “show ip prefix-list <名字> <前缀> [longer|first-match]”命令：
 * 按前缀查询模式展示列表，把 longer / first-match 关键字映射为对应 display_type，
 * 交给 vty_show_prefix_list_prefix 输出。
 */
DEFPY (show_ip_prefix_list_prefix,
       show_ip_prefix_list_prefix_cmd,
       "show ip prefix-list PREFIXLIST4_NAME$name A.B.C.D/M$prefix [longer$dl|first-match$dfm]",
       SHOW_STR
       IP_STR
       PREFIX_LIST_STR
       "Name of a prefix list\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
       "Lookup longer prefix\n"
       "First matched prefix\n")
{
	enum display_type dtype = normal_display;

	if (dl)
		dtype = longer_display;
	else if (dfm)
		dtype = first_match_display;

	return vty_show_prefix_list_prefix(vty, AFI_IP, name, prefix_str,
					   dtype);
}

/*
 * “show ip prefix-list summary”命令：以概要模式（summary_display）输出，
 * 实际工作交给 vty_show_prefix_list（不传具体前缀）。
 */
DEFPY_NOSH (show_ip_prefix_list_summary,
       show_ip_prefix_list_summary_cmd,
       "show ip prefix-list summary [PREFIXLIST4_NAME$name] [json$uj]",
       SHOW_STR
       IP_STR
       PREFIX_LIST_STR
       "Summary of prefix lists\n"
       "Name of a prefix list\n"
       JSON_STR)
{
	return vty_show_prefix_list(vty, AFI_IP, name, NULL,
				    summary_display, !!uj);
}

/*
 * “show ip prefix-list detail”命令：以明细模式（detail_display）输出，
 * 实际工作交给 vty_show_prefix_list。
 */
DEFPY_NOSH (show_ip_prefix_list_detail,
       show_ip_prefix_list_detail_cmd,
       "show ip prefix-list detail [PREFIXLIST4_NAME$name] [json$uj]",
       SHOW_STR
       IP_STR
       PREFIX_LIST_STR
       "Detail of prefix lists\n"
       "Name of a prefix list\n"
       JSON_STR)
{
	return vty_show_prefix_list(vty, AFI_IP, name, NULL,
				    detail_display, !!uj);
}

/*
 * “clear ip prefix-list [名字 [前缀]]”命令：清零指定 IPv4 前缀列表
 * （或其中某前缀对应表项）的命中计数，实际工作交给 vty_clear_prefix_list。
 */
DEFPY (clear_ip_prefix_list,
       clear_ip_prefix_list_cmd,
       "clear ip prefix-list [PREFIXLIST4_NAME$name [A.B.C.D/M$prefix]]",
       CLEAR_STR
       IP_STR
       PREFIX_LIST_STR
       "Name of a prefix list\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n")
{
	return vty_clear_prefix_list(vty, AFI_IP, name, prefix_str);
}

/*
 * “show ipv6 prefix-list”命令：可选列表名、序号与 json 输出，
 * 最终调用 vty_show_prefix_list（AFI_IP6）完成输出。
 */
DEFPY_NOSH(show_ipv6_prefix_list,
       show_ipv6_prefix_list_cmd,
       "show ipv6 prefix-list [PREFIXLIST6_NAME$name [seq$dseq (1-4294967295)$arg]] [json$uj]",
       SHOW_STR
       IPV6_STR
       PREFIX_LIST_STR
       "Name of a prefix list\n"
       "sequence number of an entry\n"
       "Sequence number\n"
       JSON_STR)
{
	enum display_type dtype = normal_display;

	if (dseq)
		dtype = sequential_display;

	return vty_show_prefix_list(vty, AFI_IP6, name, arg_str, dtype,
				    !!uj);
}

/*
 * “show ipv6 prefix-list <名字> <前缀> [longer|first-match]”命令：
 * 按前缀查询模式展示列表，关键字映射为对应 display_type，
 * 交给 vty_show_prefix_list_prefix（AFI_IP6）输出。
 */
DEFPY (show_ipv6_prefix_list_prefix,
       show_ipv6_prefix_list_prefix_cmd,
       "show ipv6 prefix-list PREFIXLIST6_NAME$name X:X::X:X/M$prefix [longer$dl|first-match$dfm]",
       SHOW_STR
       IPV6_STR
       PREFIX_LIST_STR
       "Name of a prefix list\n"
       "IPv6 prefix <network>/<length>, e.g., 3ffe::/16\n"
       "Lookup longer prefix\n"
       "First matched prefix\n")
{
	enum display_type dtype = normal_display;

	if (dl)
		dtype = longer_display;
	else if (dfm)
		dtype = first_match_display;

	return vty_show_prefix_list_prefix(vty, AFI_IP6, name,
					   prefix_str, dtype);
}

/*
 * “show ipv6 prefix-list summary”命令：以概要模式输出，
 * 实际工作交给 vty_show_prefix_list（AFI_IP6）。
 */
DEFPY_NOSH (show_ipv6_prefix_list_summary,
       show_ipv6_prefix_list_summary_cmd,
       "show ipv6 prefix-list summary [PREFIXLIST6_NAME$name] [json$uj]",
       SHOW_STR
       IPV6_STR
       PREFIX_LIST_STR
       "Summary of prefix lists\n"
       "Name of a prefix list\n"
       JSON_STR)
{
	return vty_show_prefix_list(vty, AFI_IP6, name, NULL,
				    summary_display, !!uj);
}

/*
 * “show ipv6 prefix-list detail”命令：以明细模式输出，
 * 实际工作交给 vty_show_prefix_list（AFI_IP6）。
 */
DEFPY_NOSH (show_ipv6_prefix_list_detail,
       show_ipv6_prefix_list_detail_cmd,
       "show ipv6 prefix-list detail [PREFIXLIST6_NAME$name] [json$uj]",
       SHOW_STR
       IPV6_STR
       PREFIX_LIST_STR
       "Detail of prefix lists\n"
       "Name of a prefix list\n"
       JSON_STR)
{
	return vty_show_prefix_list(vty, AFI_IP6, name, NULL,
				    detail_display, !!uj);
}

/*
 * “clear ipv6 prefix-list [名字 [前缀]]”命令：清零指定 IPv6 前缀列表
 * （或其中某前缀对应表项）的命中计数，实际工作交给 vty_clear_prefix_list。
 */
DEFPY (clear_ipv6_prefix_list,
       clear_ipv6_prefix_list_cmd,
       "clear ipv6 prefix-list [PREFIXLIST6_NAME$name [X:X::X:X/M$prefix]]",
       CLEAR_STR
       IPV6_STR
       PREFIX_LIST_STR
       "Name of a prefix list\n"
       "IPv6 prefix <network>/<length>, e.g., 3ffe::/16\n")
{
	return vty_clear_prefix_list(vty, AFI_IP6, name, prefix_str);
}

/*
 * “debug prefix-list WORD match <前缀> [address-mode]”命令：
 * 对给定前缀运行一次匹配测试，打印命中的表项序号与 ge / le，便于排查策略。
 * address-mode 为真时按单地址匹配（忽略长度约束）。
 * 返回值供脚本快速判定：命中 PERMIT 返回 CMD_SUCCESS，否则 CMD_WARNING。
 */
DEFPY (debug_prefix_list_match,
       debug_prefix_list_match_cmd,
       "debug prefix-list WORD$prefix-list match <A.B.C.D/M|X:X::X:X/M>"
       " [address-mode$addr_mode]",
       DEBUG_STR
       "Prefix-list test access\n"
       "Name of a prefix list\n"
       "Test prefix for prefix list result\n"
       "Prefix to test in ip prefix-list\n"
       "Prefix to test in ipv6 prefix-list\n"
       "Use address matching mode (PIM RP)\n")
{
	struct prefix_list *plist;
	const struct prefix_list_entry *entry = NULL;
	enum prefix_list_type ret;

	plist = prefix_list_lookup(family2afi(match->family), prefix_list);
	if (!plist) {
		vty_out(vty, "%% no prefix list named %s for AFI %s\n",
			prefix_list, afi2str(family2afi(match->family)));
		return CMD_WARNING;
	}

	ret = prefix_list_apply_ext(plist, &entry, match, !!addr_mode);

	vty_out(vty, "%s prefix list %s yields %s for %pFX, ",
		afi2str(family2afi(match->family)), prefix_list,
		ret == PREFIX_DENY ? "DENY" : "PERMIT", match);

	if (!entry)
		vty_out(vty, "no match found\n");
	else {
		vty_out(vty, "matching entry #%"PRId64": %pFX", entry->seq,
			&entry->prefix);
		if (entry->ge)
			vty_out(vty, " ge %d", entry->ge);
		if (entry->le)
			vty_out(vty, " le %d", entry->le);
		vty_out(vty, "\n");
	}

	/* allow using this in scripts for quick prefix-list member tests */
	return (ret == PREFIX_PERMIT) ? CMD_SUCCESS : CMD_WARNING;
}

/*
 * 把一条前缀列表按 ORF 报文字节流编码写入 stream s。
 * 每条表项的标志位 = init_flag 再按 permit / deny 或上 permit_flag / deny_flag；
 * 随后依次写入：标志、seq（4 字节）、ge（1 字节）、le（1 字节）以及前缀
 * （stream_put_prefix）。若 plist 为空则原样返回 s。返回写入后的流指针。
 */
struct stream *prefix_bgp_orf_entry(struct stream *s, struct prefix_list *plist,
				    uint8_t init_flag, uint8_t permit_flag,
				    uint8_t deny_flag)
{
	struct prefix_list_entry *pentry;

	if (!plist)
		return s;

	for (pentry = plist->head; pentry; pentry = pentry->next) {
		uint8_t flag = init_flag;
		struct prefix *p = &pentry->prefix;

		flag |= (pentry->type == PREFIX_PERMIT ? permit_flag
						       : deny_flag);
		stream_putc(s, flag);
		stream_putl(s, (uint32_t)pentry->seq);
		stream_putc(s, (uint8_t)pentry->ge);
		stream_putc(s, (uint8_t)pentry->le);
		stream_put_prefix(s, p);
	}

	return s;
}

/*
 * 依据收到的 ORF 表项 orfp 增删本地前缀过滤器（set 非 0 为添加，否则删除）。
 * 先做 ge / le 合法性检查：ge 或 le 不得小于前缀长度，ge 不得大于 le；
 * 当 le 恰好等于该地址族最大长度（v4 为 32 / v6 为 128）时归零，表示不再限制上界。
 * 取到或创建对应列表后，添加路径做重复检查，删除路径按
 * (前缀, type, seq, le, ge) 精确查找目标表项。成功返回 CMD_SUCCESS，
 * 校验失败或未找到返回 CMD_WARNING_CONFIG_FAILED。
 */
int prefix_bgp_orf_set(char *name, afi_t afi, struct orf_prefix *orfp,
		       int permit, int set)
{
	struct prefix_list *plist;
	struct prefix_list_entry *pentry;

	/* ge and le value check */
	if (orfp->ge && orfp->ge < orfp->p.prefixlen)
		return CMD_WARNING_CONFIG_FAILED;
	if (orfp->le && orfp->le < orfp->p.prefixlen)
		return CMD_WARNING_CONFIG_FAILED;
	if (orfp->le && orfp->ge > orfp->le)
		return CMD_WARNING_CONFIG_FAILED;

	if (orfp->ge && orfp->le == (afi == AFI_IP ? 32 : 128))
		orfp->le = 0;

	plist = prefix_list_get(afi, 1, name);
	if (!plist)
		return CMD_WARNING_CONFIG_FAILED;

	apply_mask(&orfp->p);

	if (set) {
		pentry = prefix_list_entry_make(
			&orfp->p, (permit ? PREFIX_PERMIT : PREFIX_DENY),
			orfp->seq, orfp->le, orfp->ge, false);

		if (prefix_entry_dup_check(plist, pentry)) {
			prefix_list_entry_free(pentry);
			return CMD_WARNING_CONFIG_FAILED;
		}

		prefix_list_entry_add(plist, pentry);
	} else {
		pentry = prefix_list_entry_lookup(
			plist, &orfp->p, (permit ? PREFIX_PERMIT : PREFIX_DENY),
			orfp->seq, orfp->le, orfp->ge);

		if (!pentry)
			return CMD_WARNING_CONFIG_FAILED;

		prefix_list_entry_delete(plist, pentry, 1);
	}

	return CMD_SUCCESS;
}

/*
 * 删除指定地址族（afi）下名为 name 的 ORF 前缀列表（若存在）。
 */
void prefix_bgp_orf_remove_all(afi_t afi, char *name)
{
	struct prefix_list *plist;

	plist = prefix_bgp_orf_lookup(afi, name);
	if (plist)
		prefix_list_delete(plist);
}

/*
 * 把一条 ORF 前缀列表的内容输出到 vty（use_json 为真时输出 JSON）。
 * vty 为 NULL 时只返回条目数，供内部查询计数使用。
 * 返回该列表当前的表项数 plist->count。
 */
/* return prefix count */
int prefix_bgp_show_prefix_list(struct vty *vty, afi_t afi, char *name,
				bool use_json)
{
	struct prefix_list *plist;
	struct prefix_list_entry *pentry;
	json_object *json = NULL;
	json_object *json_prefix = NULL;
	json_object *json_list = NULL;

	plist = prefix_bgp_orf_lookup(afi, name);
	if (!plist)
		return 0;

	if (!vty)
		return plist->count;

	if (use_json) {
		json = json_object_new_object();
		json_prefix = json_object_new_object();

		json_object_int_add(json_prefix, "prefixListCounter",
				    plist->count);
		json_object_string_add(json_prefix, "prefixListName",
				       plist->name);

		for (pentry = plist->head; pentry; pentry = pentry->next) {
			json_list = json_object_new_object();

			json_object_int_add(json_list, "seq", pentry->seq);
			json_object_string_add(json_list, "seqPrefixListType",
					       prefix_list_type_str(pentry));

			if (pentry->ge)
				json_object_int_add(json_list, "ge",
						    pentry->ge);
			if (pentry->le)
				json_object_int_add(json_list, "le",
						    pentry->le);

			json_object_object_addf(json_prefix, json_list, "%pFX", &pentry->prefix);
		}
		if (afi == AFI_IP)
			json_object_object_add(json, "ipPrefixList",
					       json_prefix);
		else
			json_object_object_add(json, "ipv6PrefixList",
					       json_prefix);

		vty_json(vty, json);
	} else {
		vty_out(vty, "ip%s prefix-list %s: %d entries\n",
			afi == AFI_IP ? "" : "v6", plist->name, plist->count);

		for (pentry = plist->head; pentry; pentry = pentry->next) {
			struct prefix *p = &pentry->prefix;

			vty_out(vty, "   seq %" PRId64 " %s %pFX", pentry->seq,
				prefix_list_type_str(pentry), p);

			if (pentry->ge)
				vty_out(vty, " ge %d", pentry->ge);
			if (pentry->le)
				vty_out(vty, " le %d", pentry->le);

			vty_out(vty, "\n");
		}
	}
	return plist->count;
}

/*
 * 清空某地址族（afi）下、由 orf 标志区分的全部前缀列表：
 * 反复取出红黑树首元素并删除，最后把 master->recent 置空。
 * 供 prefix_list_reset 按 v4 / v6 与普通 / ORF 四种组合逐类清理。
 */
static void prefix_list_reset_afi(afi_t afi, int orf)
{
	struct prefix_list *plist;
	struct prefix_master *master;

	master = prefix_master_get(afi, orf);
	if (master == NULL)
		return;

	while ((plist = plist_first(&master->str))) {
		prefix_list_delete(plist);
	}

	master->recent = NULL;
}

/*
 * IPv4 前缀列表的命令节点定义：命令行在此节点下解析，prompt 为空表示不改变提示符。
 */
/* Prefix-list node. */
static struct cmd_node prefix_node = {
	.name = "ipv4 prefix list",
	.node = PREFIX_NODE,
	.prompt = "",
};

/*
 * 命令行自动补全辅助：把某地址族（afi）下普通前缀列表的所有名字加入候选集 comps。
 * 名字用 MTYPE_COMPLETION 分配副本，交由补全框架后续释放。
 */
static void plist_autocomplete_afi(afi_t afi, vector comps,
				   struct cmd_token *token)
{
	struct prefix_list *plist;
	struct prefix_master *master;

	master = prefix_master_get(afi, 0);
	if (master == NULL)
		return;

	frr_each (plist, &master->str, plist)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, plist->name));
}

/*
 * 命令行自动补全回调：同时收集 IPv4 与 IPv6 两个地址族的前缀列表名。
 */
static void plist_autocomplete(vector comps, struct cmd_token *token)
{
	plist_autocomplete_afi(AFI_IP, comps, token);
	plist_autocomplete_afi(AFI_IP6, comps, token);
}

/*
 * 命令行自动补全回调：只收集 IPv4 地址族的前缀列表名。
 */
static void plist4_autocomplete(vector comps, struct cmd_token *token)
{
	plist_autocomplete_afi(AFI_IP, comps, token);
}

/*
 * 命令行自动补全回调：只收集 IPv6 地址族的前缀列表名。
 */
static void plist6_autocomplete(vector comps, struct cmd_token *token)
{
	plist_autocomplete_afi(AFI_IP6, comps, token);
}

/*
 * 前缀列表相关命令行变量的补全处理器表：
 * 按变量名（prefix_list / PREFIXLIST_NAME）或词法名（PREFIXLIST4_NAME /
 * PREFIXLIST6_NAME）注册对应补全函数，末项以 completions 为 NULL 结束。
 */
static const struct cmd_variable_handler plist_var_handlers[] = {
	{/* "prefix-list WORD" */
	 .varname = "prefix_list",
	 .completions = plist_autocomplete},
	{.tokenname = "PREFIXLIST_NAME",
	 .completions = plist_autocomplete},
	{.tokenname = "PREFIXLIST4_NAME",
	 .completions = plist4_autocomplete},
	{.tokenname = "PREFIXLIST6_NAME",
	 .completions = plist6_autocomplete},
	{.completions = NULL}};


/*
 * 注册 IPv4 前缀列表的命令节点与相关命令：
 * 在 VIEW 节点安装各 show 命令，在 ENABLE 节点安装 clear 命令。
 */
static void prefix_list_init_ipv4(void)
{
	install_node(&prefix_node);

	install_element(VIEW_NODE, &show_ip_prefix_list_cmd);
	install_element(VIEW_NODE, &show_ip_prefix_list_prefix_cmd);
	install_element(VIEW_NODE, &show_ip_prefix_list_summary_cmd);
	install_element(VIEW_NODE, &show_ip_prefix_list_detail_cmd);

	install_element(ENABLE_NODE, &clear_ip_prefix_list_cmd);
}

/*
 * IPv6 前缀列表的命令节点定义：命令行在此节点下解析，prompt 为空表示不改变提示符。
 */
/* Prefix-list node. */
static struct cmd_node prefix_ipv6_node = {
	.name = "ipv6 prefix list",
	.node = PREFIX_IPV6_NODE,
	.prompt = "",
};

/*
 * 注册 IPv6 前缀列表的命令节点与相关命令：
 * 在 VIEW 节点安装各 show 命令与 debug 匹配命令，在 ENABLE 节点安装 clear 命令。
 */
static void prefix_list_init_ipv6(void)
{
	install_node(&prefix_ipv6_node);

	install_element(VIEW_NODE, &show_ipv6_prefix_list_cmd);
	install_element(VIEW_NODE, &show_ipv6_prefix_list_prefix_cmd);
	install_element(VIEW_NODE, &show_ipv6_prefix_list_summary_cmd);
	install_element(VIEW_NODE, &show_ipv6_prefix_list_detail_cmd);
	install_element(VIEW_NODE, &debug_prefix_list_match_cmd);

	install_element(ENABLE_NODE, &clear_ipv6_prefix_list_cmd);
}

/*
 * 前缀列表子系统初始化，进程启动时调用一次：
 * 初始化四个总账（v4 / v6 的普通与 ORF）对应的红黑树，注册补全处理器，
 * 再分别完成 IPv4 / IPv6 的命令注册。
 */
void prefix_list_init(void)
{
	plist_init(&prefix_master_ipv4.str);
	plist_init(&prefix_master_orf_v4.str);
	plist_init(&prefix_master_ipv6.str);
	plist_init(&prefix_master_orf_v6.str);

	cmd_variable_handler_register(plist_var_handlers);

	prefix_list_init_ipv4();
	prefix_list_init_ipv6();
}

/*
 * 重置全部前缀列表：对 v4 / v6 的普通与 ORF 四种组合分别调用
 * prefix_list_reset_afi 逐类清空，用于重载配置或退出前。
 */
void prefix_list_reset(void)
{
	prefix_list_reset_afi(AFI_IP, 0);
	prefix_list_reset_afi(AFI_IP6, 0);
	prefix_list_reset_afi(AFI_IP, 1);
	prefix_list_reset_afi(AFI_IP6, 1);
}
