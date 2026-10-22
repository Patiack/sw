// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2019  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：typesafe.c —— typesafe 框架中与元素类型无关的数据结构实现
 *
 * typesafe.h 通过宏为每种容器生成"类型安全"的接口；当这些接口里需要
 * 一段不依赖元素类型的算法时，就调用本文件中以 typesafe_ 为前缀的函数。
 * 也就是说：宏负责"剥出内嵌节点 / 反推外层对象"，本文件负责"真正的算法"。
 *
 * 本文件覆盖以下容器：
 *   1) 单向链表 slist：typesafe_list_member（判断元素是否在链上）；
 *   2) 双向链表 dlist：typesafe_dlist_member；
 *   3) 哈希表 thash：桶数组按 2 的幂增长 / 收缩，
 *      typesafe_hash_grow / typesafe_hash_shrink；
 *   4) 跳表 skiplist：typesafe_skiplist_add / find / find_gteq / find_lt /
 *      del / pop，用多层随机索引加速有序查找；
 *   5) 堆 heap：N 叉堆，typesafe_heap_resize / pushdown / pullup。
 *
 * 相关结构定义见 typesafe.h：slist_head / slist_item、dlist_head / dlist_item、
 * thash_head / thash_item、sskip_head / sskip_item、heap_head / heap_item。
 *
 * 易踩的坑：
 *   - 跳表的 level 计数在 add / find 等函数里是 1 基，读代码务必看清注释；
 *   - 哈希表 grow 假设调用前 head->count 已自增（见函数内说明）；
 *   - del / pop 一类函数返回后元素内存会被 memset 清零。
 *
 * 阅读顺序建议：slist / dlist 的 member -> hash -> skiplist -> heap。
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "typesafe.h"
#include "memory.h"
#include "network.h"

/* 为各容器登记内存类型（MTYPE），便于内存统计与泄漏定位。 */
DEFINE_MTYPE_STATIC(LIB, TYPEDHASH_BUCKET, "Typed-hash bucket");
DEFINE_MTYPE_STATIC(LIB, SKIPLIST_OFLOW, "Skiplist overflow");
DEFINE_MTYPE_STATIC(LIB, HEAP_ARRAY, "Typed-heap array");

/*
 * 单链表"哨兵"元素。slist 用哨兵对象而非 NULL 表示链尾，
 * 以此支持 O(1) 尾部追加且无需特判空链；此处是它的静态实例。
 */
struct slist_item typesafe_slist_sentinel = { NULL };

/*
 * 判断 item 是否挂在 head 这条单向链表上。
 * 从两端（head->first 与 item->next）同时推进，若中途相遇则说明在链上。
 * 主要用于调试 / 断言，正常数据路径不调用。
 * 参数：head 链头，item 待检查元素。返回：在链上为 true，否则 false。
 */
bool typesafe_list_member(const struct slist_head *head,
			  const struct slist_item *item)
{
	struct slist_item *fromhead = head->first;
	struct slist_item **fromnext = (struct slist_item **)&item->next;

	while (fromhead != _SLIST_LAST) {
		if (fromhead == item || fromnext == head->last_next)
			return true;

		fromhead = fromhead->next;
		if (!*fromnext || *fromnext == _SLIST_LAST)
			break;
		fromnext = &(*fromnext)->next;
	}

	return false;
}

/*
 * 判断 item 是否挂在 head 这条双向链表上。
 * 若 item 的 prev / next 为空则立即判否；否则从头尾两端相向推进比较。
 * 参数：head 链头，item 待检查元素。返回：在链上为 true，否则 false。
 */
bool typesafe_dlist_member(const struct dlist_head *head,
			   const struct dlist_item *item)
{
	const struct dlist_item *fromhead = head->hitem.next;
	const struct dlist_item *fromitem = item->next;

	if (!item->prev || !item->next)
		return false;

	while (fromhead != &head->hitem && fromitem != item) {
		if (fromitem == &head->hitem || fromhead == item)
			return true;
		fromhead = fromhead->next;
		fromitem = fromitem->next;
	}

	return false;
}

/*
 * 调试用的哈希表一致性自检：逐桶校验每个元素确实落在其 hashval
 * 对应的桶内，且桶内按 hashval 非降序排列。
 * 默认被下面的 #define 编译为空，仅在把 #if 0 打开时生效。
 */
#if 0
static void hash_consistency_check(struct thash_head *head)
{
	uint32_t i;
	struct thash_item *item, *prev;

	for (i = 0; i < HASH_SIZE(*head); i++) {
		item = head->entries[i];
		prev = NULL;
		while (item) {
			assert(HASH_KEY(*head, item->hashval) == i);
			assert(!prev || item->hashval >= prev->hashval);
			prev = item;
			item = item->next;
		}
	}
}
#else
/* 默认把哈希一致性检查编译为空操作，避免发布版本的性能开销。 */
#define hash_consistency_check(x)
#endif

/*
 * 哈希表扩容：桶数按 2 的幂翻倍（由当前 count 向上取整再算 shift）。
 * 关键假设：调用方已先令 head->count 自增（故 newsize 至少为 1，
 * builtin_ctz 的最小入参为 2，保证 newshift 不小于 2）。
 * 受 head->maxshift 上限约束；若 shift 未发生变化则直接返回。
 * 副作用：XREALLOC 新的 entries 数组，并把旧桶元素按新增高位重新散列。
 * 参数：head 哈希表头。
 */
void typesafe_hash_grow(struct thash_head *head)
{
	uint32_t newsize = head->count, i, j;
	uint8_t newshift, delta;

	/* note hash_grow is called after head->count++, so newsize is
	 * guaranteed to be >= 1.  So the minimum argument to builtin_ctz
	 * below is 2, which returns 1, and that makes newshift >= 2.
	 *
	 * Calling hash_grow with a zero head->count would result in a
	 * malformed hash table that has tabshift == 1.
	 */
	assert(head->count > 0);

	hash_consistency_check(head);

	newsize |= newsize >> 1;
	newsize |= newsize >> 2;
	newsize |= newsize >> 4;
	newsize |= newsize >> 8;
	newsize |= newsize >> 16;
	newsize++;
	newshift = __builtin_ctz(newsize) + 1;

	if (head->maxshift && newshift > head->maxshift)
		newshift = head->maxshift;
	if (newshift == head->tabshift)
		return;
	newsize = _HASH_SIZE(newshift);

	head->entries = XREALLOC(MTYPE_TYPEDHASH_BUCKET, head->entries,
			sizeof(head->entries[0]) * newsize);
	memset(head->entries + HASH_SIZE(*head), 0,
			sizeof(head->entries[0]) *
				(newsize - HASH_SIZE(*head)));

	delta = newshift - head->tabshift;

	i = HASH_SIZE(*head);
	if (i == 0)
		goto out;
	do {
		struct thash_item **apos, *item;

		i--;
		apos = &head->entries[i];

		for (j = 0; j < (1U << delta); j++) {
			item = *apos;
			*apos = NULL;

			head->entries[(i << delta) + j] = item;
			apos = &head->entries[(i << delta) + j];

			while ((item = *apos)) {
				uint32_t midbits;
				midbits = _HASH_KEY(newshift, item->hashval);
				midbits &= (1 << delta) - 1;
				if (midbits > j)
					break;
				apos = &item->next;
			}
		}
	} while (i > 0);

out:
	head->tabshift = newshift;
	hash_consistency_check(head);
}

/*
 * 哈希表缩容：桶数按 2 的幂缩小到约等于 count，受 head->minshift 下限约束。
 * 若 count 为 0 则直接释放 entries 并把 tabshift 归零。
 * 副作用：把相邻低位桶合并成一条链，再 XREALLOC 缩小 entries 数组。
 * 参数：head 哈希表头。
 */
void typesafe_hash_shrink(struct thash_head *head)
{
	uint32_t newsize = head->count, i, j;
	uint8_t newshift, delta;

	hash_consistency_check(head);

	if (!head->count) {
		XFREE(MTYPE_TYPEDHASH_BUCKET, head->entries);
		head->tabshift = 0;
		return;
	}

	newsize |= newsize >> 1;
	newsize |= newsize >> 2;
	newsize |= newsize >> 4;
	newsize |= newsize >> 8;
	newsize |= newsize >> 16;
	newsize++;
	newshift = __builtin_ctz(newsize) + 1;

	if (head->minshift && newshift < head->minshift)
		newshift = head->minshift;
	if (newshift == head->tabshift)
		return;
	newsize = _HASH_SIZE(newshift);

	delta = head->tabshift - newshift;

	for (i = 0; i < newsize; i++) {
		struct thash_item **apos = &head->entries[i];

		for (j = 0; j < (1U << delta); j++) {
			*apos = head->entries[(i << delta) + j];
			while (*apos)
				apos = &(*apos)->next;
		}
	}
	head->entries = XREALLOC(MTYPE_TYPEDHASH_BUCKET, head->entries,
			sizeof(head->entries[0]) * newsize);
	head->tabshift = newshift;

	hash_consistency_check(head);
}

/* skiplist */

/*
 * 读取跳表元素 item 在第 level 层的 next 指针。
 * 当 level 超出内嵌层数时，经 SKIPLIST_OVERFLOW 处的"溢出指针"
 * （最低位作标记）跳到堆上分配的 sskip_overflow 数组里取值。
 * 参数：item 元素，level 层号（0 基）。返回：该层的后继指针。
 */
static inline struct sskip_item *sl_level_get(const struct sskip_item *item,
			size_t level)
{
	if (level < SKIPLIST_OVERFLOW)
		return item->next[level];
	if (level == SKIPLIST_OVERFLOW && !((uintptr_t)item->next[level] & 1))
		return item->next[level];

	uintptr_t ptrval = (uintptr_t)item->next[SKIPLIST_OVERFLOW];
	ptrval &= UINTPTR_MAX - 3;
	struct sskip_overflow *oflow = (struct sskip_overflow *)ptrval;
	return oflow->next[level - SKIPLIST_OVERFLOW];
}

/*
 * 设置跳表元素 item 在第 level 层的 next 指针，是 sl_level_get 的写入版。
 * 内嵌层直接写字段；溢出层先解出 sskip_overflow 再写。
 */
static inline void sl_level_set(struct sskip_item *item, size_t level,
		struct sskip_item *value)
{
	if (level < SKIPLIST_OVERFLOW)
		item->next[level] = value;
	else if (level == SKIPLIST_OVERFLOW && !((uintptr_t)item->next[level] & 1))
		item->next[level] = value;
	else {
		uintptr_t ptrval = (uintptr_t)item->next[SKIPLIST_OVERFLOW];
		ptrval &= UINTPTR_MAX - 3;
		struct sskip_overflow *oflow = (struct sskip_overflow *)ptrval;
		oflow->next[level - SKIPLIST_OVERFLOW] = value;
	}
}

/*
 * 跳表插入元素。
 * 先用 frr_weak_random 随机决定新节点层数 newlevel（几何分布，上限
 * SKIPLIST_MAXDEPTH）；从高层向低层找到每层插入位置。若发现等键元素
 * （cmpfn 返回 0）则返回该已存在元素且不插入（跳表按唯一键语义使用）。
 * 若 newlevel 超过内嵌层数，则额外分配 sskip_overflow 存放高层的 next。
 * 参数：head 跳表，item 待插入元素，cmpfn 比较函数。
 * 返回：NULL 表示插入成功；非 NULL 为冲突时命中的已有元素。
 * 副作用：head->count 加一，item 的 next 指针被设置。
 */
struct sskip_item *typesafe_skiplist_add(struct sskip_head *head,
		struct sskip_item *item,
		int (*cmpfn)(const struct sskip_item *a,
				const struct sskip_item *b))
{
	size_t level = SKIPLIST_MAXDEPTH, newlevel, auxlevel;
	struct sskip_item *prev = &head->hitem, *next, *auxprev, *auxnext;
	int cmpval;
	unsigned long rnd;

	/* level / newlevel are 1-counted here */
	do {
		rnd = (unsigned long)frr_weak_random();
	} while (rnd == 0);
	newlevel = __builtin_ctzl(rnd) + 1;
	if (newlevel > SKIPLIST_MAXDEPTH)
		newlevel = SKIPLIST_MAXDEPTH;

	next = NULL;
	while (level >= newlevel) {
		next = sl_level_get(prev, level - 1);
		if (!next) {
			level--;
			continue;
		}
		cmpval = cmpfn(next, item);
		if (cmpval < 0) {
			prev = next;
			continue;
		} else if (cmpval == 0) {
			return next;
		}
		level--;
	}

	/* check for duplicate item - could be removed if code doesn't rely
	 * on it, but not really work the complication. */
	auxlevel = level;
	auxprev = prev;
	while (auxlevel) {
		auxlevel--;
		auxnext = sl_level_get(auxprev, auxlevel);
		cmpval = 1;
		while (auxnext && (cmpval = cmpfn(auxnext, item)) < 0) {
			auxprev = auxnext;
			auxnext = sl_level_get(auxprev, auxlevel);
		}
		if (cmpval == 0)
			return auxnext;
	};

	head->count++;
	memset(item, 0, sizeof(*item));
	if (newlevel > SKIPLIST_EMBED) {
		struct sskip_overflow *oflow;
		oflow = XMALLOC(MTYPE_SKIPLIST_OFLOW, sizeof(void *)
				* (newlevel - SKIPLIST_OVERFLOW));
		item->next[SKIPLIST_OVERFLOW] = (struct sskip_item *)
				((uintptr_t)oflow | 1);
	}

	sl_level_set(item, level, next);
	sl_level_set(prev, level, item);
	/* level is now 0-counted and < newlevel*/
	while (level) {
		level--;
		next = sl_level_get(prev, level);
		while (next && cmpfn(next, item) < 0) {
			prev = next;
			next = sl_level_get(prev, level);
		}

		sl_level_set(item, level, next);
		sl_level_set(prev, level, item);
	};
	return NULL;
}

/* NOTE: level counting below is 1-based since that makes the code simpler! */

/*
 * 跳表精确查找：从最高层向下降，找到与 item 同键的元素返回，否则 NULL。
 * 注意：下面 level 计数是 1 基（作者注释说明这样代码更简单）。
 */
const struct sskip_item *typesafe_skiplist_find(
		const struct sskip_head *head,
		const struct sskip_item *item, int (*cmpfn)(
				const struct sskip_item *a,
				const struct sskip_item *b))
{
	size_t level = SKIPLIST_MAXDEPTH;
	const struct sskip_item *prev = &head->hitem, *next;
	int cmpval;

	while (level) {
		next = sl_level_get(prev, level - 1);
		if (!next) {
			level--;
			continue;
		}
		cmpval = cmpfn(next, item);
		if (cmpval < 0) {
			prev = next;
			continue;
		}
		if (cmpval == 0)
			return next;
		level--;
	}
	return NULL;
}

/*
 * 跳表查找键不小于 item 的最小元素（后继 / 下界）。
 * 命中相等即返回；否则返回最后一次停下的 next（可能为 NULL）。
 */
const struct sskip_item *typesafe_skiplist_find_gteq(
		const struct sskip_head *head,
		const struct sskip_item *item, int (*cmpfn)(
				const struct sskip_item *a,
				const struct sskip_item *b))
{
	size_t level = SKIPLIST_MAXDEPTH;
	const struct sskip_item *prev = &head->hitem, *next;
	int cmpval;

	while (level) {
		next = sl_level_get(prev, level - 1);
		if (!next) {
			level--;
			continue;
		}
		cmpval = cmpfn(next, item);
		if (cmpval < 0) {
			prev = next;
			continue;
		}
		if (cmpval == 0)
			return next;
		level--;
	}
	return next;
}

/*
 * 跳表查找键严格小于 item 的最大元素（前驱 / 上界）。
 * 用 best 记录最后一个小于 item 的元素，走到末尾返回 best 或 NULL。
 */
const struct sskip_item *typesafe_skiplist_find_lt(
		const struct sskip_head *head,
		const struct sskip_item *item, int (*cmpfn)(
				const struct sskip_item *a,
				const struct sskip_item *b))
{
	size_t level = SKIPLIST_MAXDEPTH;
	const struct sskip_item *prev = &head->hitem, *next, *best = NULL;
	int cmpval;

	while (level) {
		next = sl_level_get(prev, level - 1);
		if (!next) {
			level--;
			continue;
		}
		cmpval = cmpfn(next, item);
		if (cmpval < 0) {
			best = prev = next;
			continue;
		}
		level--;
	}
	return best;
}

/*
 * 跳表删除元素 item：逐层把指向 item 的指针改为指向其后继。
 * 摘除后释放可能的溢出数组、count 减一，并把 item 内存清零。
 * 返回：被删元素；若 item 不在表中则返回 NULL。
 */
struct sskip_item *typesafe_skiplist_del(
	struct sskip_head *head, struct sskip_item *item,
	int (*cmpfn)(const struct sskip_item *a, const struct sskip_item *b))
{
	size_t level = SKIPLIST_MAXDEPTH;
	struct sskip_item *prev = &head->hitem, *next;
	int cmpval;
	bool found = false;

	while (level) {
		next = sl_level_get(prev, level - 1);
		if (!next) {
			level--;
			continue;
		}
		if (next == item) {
			sl_level_set(prev, level - 1,
				sl_level_get(item, level - 1));
			level--;
			found = true;
			continue;
		}
		cmpval = cmpfn(next, item);
		if (cmpval < 0) {
			prev = next;
			continue;
		}
		level--;
	}

	if (!found)
		return NULL;

	/* TBD: assert when trying to remove non-existing item? */
	head->count--;

	if ((uintptr_t)item->next[SKIPLIST_OVERFLOW] & 1) {
		uintptr_t ptrval = (uintptr_t)item->next[SKIPLIST_OVERFLOW];
		ptrval &= UINTPTR_MAX - 3;
		struct sskip_overflow *oflow = (struct sskip_overflow *)ptrval;
		XFREE(MTYPE_SKIPLIST_OFLOW, oflow);
	}
	memset(item, 0, sizeof(*item));

	return item;
}

/*
 * 跳表弹出最小元素（第 0 层第一个）：摘除各层对它的引用、释放溢出数组、
 * count 减一并清空元素内存。
 * 返回：被弹出的最小元素；空表返回 NULL。
 */
struct sskip_item *typesafe_skiplist_pop(struct sskip_head *head)
{
	size_t level = SKIPLIST_MAXDEPTH;
	struct sskip_item *prev = &head->hitem, *next, *item;

	item = sl_level_get(prev, 0);
	if (!item)
		return NULL;

	do {
		level--;

		next = sl_level_get(prev, level);
		if (next != item)
			continue;

		sl_level_set(prev, level, sl_level_get(item, level));
	} while (level);

	head->count--;

	if ((uintptr_t)item->next[SKIPLIST_OVERFLOW] & 1) {
		uintptr_t ptrval = (uintptr_t)item->next[SKIPLIST_OVERFLOW];
		ptrval &= UINTPTR_MAX - 3;
		struct sskip_overflow *oflow = (struct sskip_overflow *)ptrval;
		XFREE(MTYPE_SKIPLIST_OFLOW, oflow);
	}
	memset(item, 0, sizeof(*item));

	return item;
}

/* heap */

/*
 * 调试用的堆一致性自检（递归校验每个父节点都不大于其子节点）。
 * 与哈希版同理，默认经下面的 #define 编译为空。
 */
#if 0
static void heap_consistency_check(struct heap_head *head,
				   int (*cmpfn)(const struct heap_item *a,
						const struct heap_item *b),
				   uint32_t pos)
{
	uint32_t rghtpos = pos + 1;
	uint32_t downpos = HEAP_NARY * (pos + 1);

	if (pos + 1 > ~0U / HEAP_NARY)
		downpos = ~0U;

	if ((pos & (HEAP_NARY - 1)) != HEAP_NARY - 1 && rghtpos < head->count) {
		assert(cmpfn(head->array[rghtpos], head->array[pos]) >= 0);
		heap_consistency_check(head, cmpfn, rghtpos);
	}
	if (downpos < head->count) {
		assert(cmpfn(head->array[downpos], head->array[pos]) >= 0);
		heap_consistency_check(head, cmpfn, downpos);
	}
}
#else
/* 默认把堆一致性检查编译为空操作。 */
#define heap_consistency_check(head, cmpfn, pos)
#endif

/*
 * 调整堆底层数组容量。grow 为真时按比例扩容（<=36 直接到 72；大表按
 * 1.5 或 4/3 倍增长）；否则缩容到 count；count 为 0 则释放数组。
 * 新容量向上取整到 HEAP_NARY 的倍数，容量未变则直接返回。
 * 参数：head 堆，grow 是否扩容。副作用：XREALLOC head->array。
 */
void typesafe_heap_resize(struct heap_head *head, bool grow)
{
	uint32_t newsize;

	if (grow) {
		newsize = head->arraysz;
		if (newsize <= 36)
			newsize = 72;
		else if (newsize < 262144)
			newsize += newsize / 2;
		else if (newsize < 0xaaaa0000)
			newsize += newsize / 3;
		else
			assert(!newsize);
	} else if (head->count > 0) {
		newsize = head->count;
	} else {
		XFREE(MTYPE_HEAP_ARRAY, head->array);
		head->arraysz = 0;
		return;
	}

	newsize += HEAP_NARY - 1;
	newsize &= ~(HEAP_NARY - 1);
	if (newsize == head->arraysz)
		return;

	head->array = XREALLOC(MTYPE_HEAP_ARRAY, head->array,
			       newsize * sizeof(struct heap_item *));
	head->arraysz = newsize;
}

/*
 * 下沉操作：把 item 从 pos 处向下调整到合适位置（建堆 / 取出堆顶后使用）。
 * 这是 N 叉堆（每节点 HEAP_NARY 个子）。每个节点有两个候选下移位置：
 *   rghtpos —— 同块内右侧兄弟；downpos —— 下一层块中的第一个孩子。
 * 比较后选择更小的子节点继续下沉，直到 item 就位。
 * 参数：head 堆，pos 起点下标，item 待安放元素，cmpfn 比较函数。
 * 副作用：就地移动 head->array 元素并更新各元素的 index 字段。
 */
void typesafe_heap_pushdown(struct heap_head *head, uint32_t pos,
		struct heap_item *item,
		int (*cmpfn)(const struct heap_item *a,
			     const struct heap_item *b))
{
	uint32_t rghtpos, downpos, moveto;

	while (1) {
		/* rghtpos: neighbor to the "right", inside block of NARY.
		 *          may be invalid if last in block, check nary_last()
		 * downpos: first neighbor in the "downwards" block further
		 *          away from the root
		 */
		rghtpos = pos + 1;

		/* make sure we can use the full 4G items */
		downpos = HEAP_NARY * (pos + 1);
		if (pos + 1 > ~0U / HEAP_NARY)
			/* multiplication overflowed.  ~0U is guaranteed
			 * to be an invalid index; size limit is enforced in
			 * resize()
			 */
			downpos = ~0U;

		/* only used on break */
		moveto = pos;

#define nary_last(x) (((x) & (HEAP_NARY - 1)) == HEAP_NARY - 1)
		if (downpos >= head->count
		    || cmpfn(head->array[downpos], item) >= 0) {
			/* not moving down; either at end or down is >= item */
			if (nary_last(pos) || rghtpos >= head->count
			    || cmpfn(head->array[rghtpos], item) >= 0)
				/* not moving right either - got our spot */
				break;

			moveto = rghtpos;

		/* else: downpos is valid and < item.  choose between down
		 * or right (if the latter is an option) */
		} else if (nary_last(pos) || cmpfn(head->array[rghtpos],
						   head->array[downpos]) >= 0)
			moveto = downpos;
		else
			moveto = rghtpos;
#undef nary_last

		head->array[pos] = head->array[moveto];
		head->array[pos]->index = pos;
		pos = moveto;
	}

	head->array[moveto] = item;
	item->index = moveto;

	heap_consistency_check(head, cmpfn, 0);
}

/*
 * 上浮操作：把 item 从 pos 处向上调整（插入新元素时使用）。
 * 按 N 叉堆规则计算父节点位置（块首元素的父是前一块末元素，否则是左邻），
 * 若父节点更大则交换上移，直到父节点不大于 item。
 * 参数：head 堆，pos 起点下标，item 待安放元素，cmpfn 比较函数。
 * 副作用：就地移动 head->array 元素并更新 index 字段。
 */
void typesafe_heap_pullup(struct heap_head *head, uint32_t pos,
		struct heap_item *item,
		int (*cmpfn)(const struct heap_item *a,
			     const struct heap_item *b))
{
	uint32_t moveto;

	while (pos != 0) {
		if ((pos & (HEAP_NARY - 1)) == 0)
			moveto = pos / HEAP_NARY - 1;
		else
			moveto = pos - 1;

		if (cmpfn(head->array[moveto], item) <= 0)
			break;

		head->array[pos] = head->array[moveto];
		head->array[pos]->index = pos;

		pos = moveto;
	}

	head->array[pos] = item;
	item->index = pos;

	heap_consistency_check(head, cmpfn, 0);
}
