// SPDX-License-Identifier: LicenseRef-Skiplist-BSD-0-Clause
/*
 * Copyright 1990 William Pugh
 *
 * Permission to include in quagga provide on March 31, 2016
 */

/*
 * Skip List implementation based on code from William Pugh.
 * ftp://ftp.cs.umd.edu/pub/skipLists/
 *
 * Skip Lists are a probabilistic alternative to balanced trees, as
 * described in the June 1990 issue of CACM and were invented by
 * William Pugh in 1987.
 *
 * This file contains source code to implement a dictionary using
 * skip lists and a test driver to test the routines.
 *
 * A couple of comments about this implementation:
 *   The routine randomLevel has been hard-coded to generate random
 *   levels using p=0.25. It can be easily changed.
 *
 *   The insertion routine has been implemented so as to use the
 *   dirty hack described in the CACM paper: if a random level is
 *   generated that is more than the current maximum level, the
 *   current maximum level plus one is used instead.
 *
 *   Levels start at zero and go up to MaxLevel (which is equal to
 * 	(MaxNumberOfLevels-1).
 *
 * The run-time flag SKIPLIST_FLAG_ALLOW_DUPLICATES determines whether or
 * not duplicates are allowed for a given list. If set, duplicates are
 * allowed and act in a FIFO manner. If not set, an insertion of a value
 * already in the list updates the previously existing binding.
 *
 * BitsInRandom is defined to be the number of bits returned by a call to
 * random(). For most all machines with 32-bit integers, this is 31 bits
 * as currently set.
 */


/*
 * ============================================================================
 * 文件导读：skiplist.c —— 跳表（skip list）实现
 * ============================================================================
 * 本文件是 William Pugh 跳表实现的 FRR 版本（BSD-0-Clause），提供有序字典。
 * 跳表用多层索引链表替代平衡树：随机决定节点层数（randomLevel），
 * 查找 / 插入 / 删除平均 O(log N)，最坏 O(N)。
 *
 * 结构要点：
 *   - 节点层数上限 MaxNumberOfLevels=16；层从 0 起，最高为 MaxLevel=15
 *   - header 是一个 16 层哨兵头，forward[0] 串起所有节点（第 0 层即完整有序链）
 *   - randomLevel 以 p=0.25 的概率升层（由 2 位随机数决定）
 *   - 插入时用 update[] 记录各层前驱，再逐层改指针
 *
 * 重复键：
 *   - SKIPLIST_FLAG_ALLOW_DUPLICATES 决定是否允许重复键；允许时按 FIFO 处理，
 *     删除需同时给出 key 与 value 精确定位
 *
 * 关键回调：
 *   - cmp(key1,key2) 排序比较；del(value) 删除时释放用户数据
 *
 * 阅读顺序：
 *   randomLevel / default_cmp -> skiplist_new / free -> skiplist_insert
 *   -> skiplist_delete -> first_value / next_value -> first / last / next
 *   -> 末尾的 skiplist_debug / skiplist_test（调试与自测）
 * ============================================================================
 */
#include <zebra.h>

#include "memory.h"
#include "log.h"
#include "vty.h"
#include "skiplist.h"
#include "lib_errors.h"
#include "network.h"

/*
 * 内存类型登记：跳表本体 / 节点 / 层统计数组三类分配纳入内存记账。
 */
DEFINE_MTYPE_STATIC(LIB, SKIP_LIST, "Skip List");
DEFINE_MTYPE_STATIC(LIB, SKIP_LIST_NODE, "Skip Node");
DEFINE_MTYPE_STATIC(LIB, SKIP_LIST_STATS, "Skiplist Counters");

/*
 * 每次随机调用可提供的位数（32 位机上取 31）。
 */
#define BitsInRandom 31

/*
 * 跳表最大层数 16；层编号从 0 到 MaxLevel（= 15）。
 */
#define MaxNumberOfLevels 16
#define MaxLevel (MaxNumberOfLevels-1)
/*
 * 宏：为一个 l 层节点分配内存（变长 forward 数组按层数扩展）。
 */
#define newNodeOfLevel(l)                                                      \
	XCALLOC(MTYPE_SKIP_LIST_NODE,                                          \
		sizeof(struct skiplistnode)                                    \
			+ (l) * sizeof(struct skiplistnode *))

/*
 * 宏：为 l+1 个 int 的层统计数组分配内存。
 * 注意：其元素类型必须与 struct skiplist.level_stats 保持一致。
 */
/* XXX must match type of (struct skiplist).level_stats */
#define newStatsOfLevel(l)                                                     \
	XCALLOC(MTYPE_SKIP_LIST_STATS, ((l) + 1) * sizeof(int))

/*
 * 随机数缓冲：从低位依次取出随机比特供 randomLevel 使用，
 * 以减少 frr_weak_random 的调用次数。
 */
static int randomsLeft;
static int randomBits;

/*
 * 一致性自检宏：header->forward[0] 与 last 指针必须同时“有 / 无”，
 * 否则说明有序链与 last 记录不一致。仅在 SKIPLIST_DEBUG 下生效，否则展开为空。
 */
#ifdef SKIPLIST_DEBUG
#define CHECKLAST(sl)                                                          \
	do {                                                                   \
		if ((sl)->header->forward[0] && !(sl)->last)                   \
			assert(0);                                             \
		if (!(sl)->header->forward[0] && (sl)->last)                   \
			assert(0);                                             \
	} while (0)
#else
#define CHECKLAST(sl)
#endif


/*
 * randomLevel：随机生成新节点的层数（p=0.25 升层，见文件头说明）。
 * 返回：0 到 MaxLevel 之间的层号。
 * 副作用：消耗静态随机比特缓冲 randomsLeft / randomBits。
 */
static int randomLevel(void)
{
	register int level = 0;
	register int b;

	do {
		if (randomsLeft <= 0) {
			randomBits = frr_weak_random();
			randomsLeft = BitsInRandom / 2;
		}
		b = randomBits & 3;
		randomBits >>= 2;
		--randomsLeft;

		if (!b) {
			level++;
			if (level >= MaxLevel)
				return MaxLevel;
		}
	} while (!b);

	return level;
}

/*
 * default_cmp：默认键比较函数：直接比较 key1 / key2 两个指针的地址大小。
 * 返回：-1 / 0 / 1。
 */
static int default_cmp(const void *key1, const void *key2)
{
	if (key1 < key2)
		return -1;
	if (key1 > key2)
		return 1;
	return 0;
}

/*
 * skiplist_count：返回跳表元素个数。
 */
unsigned int skiplist_count(struct skiplist *l)
{
	return l->count;
}

/*
 * skiplist_new：创建跳表。
 * 分配表结构、16 层哨兵 header、层统计数组；cmp 为空时用 default_cmp，
 * del 为空则不自动释放 value。
 * 返回：新跳表指针。
 */
struct skiplist *skiplist_new(int flags,
			      int (*cmp)(const void *key1, const void *key2),
			      void (*del)(void *val))
{
	struct skiplist *new;

	new = XCALLOC(MTYPE_SKIP_LIST, sizeof(struct skiplist));
	assert(new);

	new->level = 0;
	new->count = 0;
	new->header = newNodeOfLevel(MaxNumberOfLevels);
	new->level_stats = newStatsOfLevel(MaxNumberOfLevels);

	new->flags = flags;
	if (cmp)
		new->cmp = cmp;
	else
		new->cmp = default_cmp;

	if (del)
		new->del = del;

	return new;
}

/*
 * skiplist_free：释放所有节点（若有 del 回调，对每个真实节点的 value 调 del）、
 * 层统计数组和表本身。
 */
void skiplist_free(struct skiplist *l)
{
	register struct skiplistnode *p, *q;

	p = l->header;

	do {
		q = p->forward[0];
		if (l->del && p != l->header)
			(*l->del)(p->value);
		XFREE(MTYPE_SKIP_LIST_NODE, p);
		p = q;
	} while (p);

	XFREE(MTYPE_SKIP_LIST_STATS, l->level_stats);
	XFREE(MTYPE_SKIP_LIST, l);
}


/*
 * skiplist_insert：插入 key / value。
 * 先自顶向下用 update[] 记录各层前驱；若不允许重复键且已存在同键值则返回 -1。
 * 否则随机取层数，逐层改指针插入，并维护 last / count / level_stats。
 * 返回：成功 0，已存在（不允许重复时）-1。
 */
int skiplist_insert(register struct skiplist *l, register void *key,
		    register void *value)
{
	register int k;
	struct skiplistnode *update[MaxNumberOfLevels];
	register struct skiplistnode *p, *q;

	CHECKLAST(l);

#ifdef SKIPLIST_DEBUG
	/* DEBUG */
	if (!key) {
		flog_err(EC_LIB_DEVELOPMENT, "%s: key is 0, value is %p",
			 __func__, value);
	}
#endif

	p = l->header;
	k = l->level;
	do {
		while (q = p->forward[k], q && (*l->cmp)(q->key, key) < 0)
			p = q;
		update[k] = p;
	} while (--k >= 0);

	if (!(l->flags & SKIPLIST_FLAG_ALLOW_DUPLICATES) && q
	    && ((*l->cmp)(q->key, key) == 0)) {

		return -1;
	}

	k = randomLevel();
	assert(k >= 0);
	if (k > l->level) {
		k = ++l->level;
		update[k] = l->header;
	}

	q = newNodeOfLevel(k);
	q->key = key;
	q->value = value;
#ifdef SKIPLIST_0TIMER_DEBUG
	q->flags = SKIPLIST_NODE_FLAG_INSERTED; /* debug */
#endif

	++(l->level_stats[k]);
#ifdef SKIPLIST_DEBUG
	zlog_debug("%s: incremented level_stats @%p:%d, now %d", __func__, l, k,
		   l->level_stats[k]);
#endif

	do {
		p = update[k];
		q->forward[k] = p->forward[k];
		p->forward[k] = q;
	} while (--k >= 0);

	/*
	 * If this is the last item in the list, update the "last" pointer
	 */
	if (!q->forward[0]) {
		l->last = q;
	}

	++(l->count);

	CHECKLAST(l);

	return 0;
}

/*
 * skiplist_delete：删除指定 key（重复键时再按 value 精确定位）的节点。
 * 用 update[] 记录各层前驱，找到后逐层摘链，维护 last / level / level_stats / count，
 * 并对被删 value 调用 del 回调。
 * 返回：成功 0，未找到 -1。
 */
int skiplist_delete(register struct skiplist *l, register void *key,
		    register void *value) /* used only if duplicates allowed */
{
	register int k, m;
	struct skiplistnode *update[MaxNumberOfLevels];
	register struct skiplistnode *p, *q;

	CHECKLAST(l);

	/* to make debugging easier */
	for (k = 0; k < MaxNumberOfLevels; ++k)
		update[k] = NULL;

	p = l->header;
	k = m = l->level;
	do {
		while (q = p->forward[k], q && (*l->cmp)(q->key, key) < 0)
			p = q;
		update[k] = p;
	} while (--k >= 0);

	if (l->flags & SKIPLIST_FLAG_ALLOW_DUPLICATES) {
		while (q && ((*l->cmp)(q->key, key) == 0)
		       && (q->value != value)) {
			int i;
			for (i = 0; i <= l->level; ++i) {
				if (update[i]->forward[i] == q)
					update[i] = q;
			}
			q = q->forward[0];
		}
	}

	if (q && (*l->cmp)(q->key, key) == 0) {
		if (!(l->flags & SKIPLIST_FLAG_ALLOW_DUPLICATES)
		    || (q->value == value)) {

/*
 * found node to delete
 */
#ifdef SKIPLIST_0TIMER_DEBUG
			q->flags &= ~SKIPLIST_NODE_FLAG_INSERTED;
#endif
			/*
			 * If we are deleting the last element of the list,
			 * update the list's "last" pointer.
			 */
			if (l->last == q) {
				if (update[0] == l->header)
					l->last = NULL;
				else
					l->last = update[0];
			}

			for (k = 0; k <= m && (p = update[k])->forward[k] == q;
			     k++) {
				p->forward[k] = q->forward[k];
			}
			--(l->level_stats[k - 1]);
#ifdef SKIPLIST_DEBUG
			zlog_debug("%s: decremented level_stats @%p:%d, now %d",
				   __func__, l, k - 1, l->level_stats[k - 1]);
#endif
			if (l->del)
				(*l->del)(q->value);
			XFREE(MTYPE_SKIP_LIST_NODE, q);
			while (l->header->forward[m] == NULL && m > 0)
				m--;
			l->level = m;
			CHECKLAST(l);
			--(l->count);
			return 0;
		}
	}

	CHECKLAST(l);
	return -1;
}

/*
 * skiplist_first_value：找到第一个键等于 key 的节点，输出其 value 与游标。
 * 参数：valuePointer 可空，输出值；cursor 可空，输出游标（供 next_value 用）。
 * 返回：成功 0，无匹配 -1。
 */
/*
 * Obtain first value matching "key". Unless SKIPLIST_FLAG_ALLOW_DUPLICATES
 * is set, this will also be the only value matching "key".
 *
 * Also set a cursor for use with skiplist_next_value.
 */
int skiplist_first_value(register struct skiplist *l, /* in */
			 register const void *key,    /* in */
			 void **valuePointer,	      /* out */
			 void **cursor)		      /* out */
{
	register int k;
	register struct skiplistnode *p, *q;

	p = l->header;
	k = l->level;

	do {
		while (q = p->forward[k], q && (*l->cmp)(q->key, key) < 0)
			p = q;

	} while (--k >= 0);

	if (!q || (*l->cmp)(q->key, key))
		return -1;

	if (valuePointer)
		*valuePointer = q->value;

	if (cursor)
		*cursor = q;

	return 0;
}

/*
 * skiplist_search：查找 key 对应的 value（内部即 first_value，不取游标）。
 * 返回：成功 0，未找到 -1。
 */
int skiplist_search(register struct skiplist *l, register void *key,
		    void **valuePointer)
{
	return skiplist_first_value(l, key, valuePointer, NULL);
}


/*
 * skiplist_next_value：在允许重复键时，取同一 key 的下一个 value（FIFO 顺序）。
 * 参数：cursor 为 in / out 游标；首次可传 NULL 由内部自行定位。
 * 返回：成功 0；key / value 不匹配或已是同键最后一个 -1。
 */
/*
 * Caller supplies key and value of an existing item in the list.
 * Function returns the value of the next list item that has the
 * same key (useful when SKIPLIST_FLAG_ALLOW_DUPLICATES is set).
 *
 * Returns 0 on success. If the caller-supplied key and value
 * do not correspond to a list element, or if they specify the
 * last element with the given key, -1 is returned.
 */
int skiplist_next_value(register struct skiplist *l, /* in */
			register const void *key,	  /* in */
			void **valuePointer,	 /* in/out */
			void **cursor)		     /* in/out */
{
	register int k;
	register struct skiplistnode *p, *q;

	CHECKLAST(l);

	if (!(l->flags & SKIPLIST_FLAG_ALLOW_DUPLICATES)) {
		return -1;
	}

	if (!cursor || !*cursor) {
		p = l->header;
		k = l->level;

		/*
		 * Find matching key
		 */
		do {
			while (q = p->forward[k],
			       q && (*l->cmp)(q->key, key) < 0)
				p = q;
		} while (--k >= 0);

		/*
		 * Find matching value
		 */
		while (q && ((*l->cmp)(q->key, key) == 0)
		       && (q->value != *valuePointer)) {
			q = q->forward[0];
		}

		if (!q || ((*l->cmp)(q->key, key) != 0)
		    || (q->value != *valuePointer)) {
			/*
			 * No matching value
			 */
			CHECKLAST(l);
			return -1;
		}
	} else {
		q = (struct skiplistnode *)*cursor;
	}

	/*
	 * Advance cursor
	 */
	q = q->forward[0];

	/*
	 * If we reached end-of-list or if the key is no longer the same,
	 * then return error
	 */
	if (!q || ((*l->cmp)(q->key, key) != 0))
		return -1;

	*valuePointer = q->value;
	if (cursor)
		*cursor = q;
	CHECKLAST(l);
	return 0;
}

/*
 * skiplist_first：取最小键节点，输出其 key 与 value（指针参数可空）。
 * 返回：成功 0，空表 -1。
 */
int skiplist_first(register struct skiplist *l, void **keyPointer,
		   void **valuePointer)
{
	register struct skiplistnode *p;

	CHECKLAST(l);
	p = l->header->forward[0];
	if (!p)
		return -1;

	if (keyPointer)
		*keyPointer = p->key;

	if (valuePointer)
		*valuePointer = p->value;

	CHECKLAST(l);

	return 0;
}

/*
 * skiplist_last：取最大键节点（用 last 指针 O(1) 命中），输出 key / value。
 * 返回：成功 0，空表 -1。
 */
int skiplist_last(register struct skiplist *l, void **keyPointer,
		  void **valuePointer)
{
	CHECKLAST(l);
	if (l->last) {
		if (keyPointer)
			*keyPointer = l->last->key;
		if (valuePointer)
			*valuePointer = l->last->value;
		return 0;
	}
	return -1;
}

/*
 * skiplist_empty：空返回 true(1)，非空返回 0。
 */
/*
 * true = empty
 */
int skiplist_empty(register struct skiplist *l)
{
	CHECKLAST(l);
	if (l->last)
		return 0;
	return 1;
}

/*
 * skiplist_next：遍历用。调用者先把 *cursor 置 NULL 取第一个元素，之后每次取下一个。
 * 参数：keyPointer / valuePointer 为输出（可空）；cursor 为 in / out。
 * 返回：成功 0；cursor 为 NULL 或已到末尾 -1。
 */
/*
 * Use this to walk the list. Caller sets *cursor to NULL to obtain
 * first element. Return value of 0 indicates valid cursor/element
 * returned, otherwise NULL cursor arg or EOL.
 */
int skiplist_next(register struct skiplist *l, /* in */
		  void **keyPointer,	   /* out */
		  void **valuePointer,	 /* out */
		  void **cursor)	       /* in/out */
{
	struct skiplistnode *p;

	if (!cursor)
		return -1;

	CHECKLAST(l);

	if (!*cursor) {
		p = l->header->forward[0];
	} else {
		p = *cursor;
		p = p->forward[0];
	}
	*cursor = p;

	if (!p)
		return -1;

	if (keyPointer)
		*keyPointer = p->key;

	if (valuePointer)
		*valuePointer = p->value;

	CHECKLAST(l);

	return 0;
}

/*
 * skiplist_delete_first：删除最小键节点。
 * 从第 0 层头节点后继摘链，逐层调整指针，必要时降低 level、更新 last / count，
 * 并对 value 调用 del 回调。
 * 返回：成功 0，空表 -1。
 */
int skiplist_delete_first(register struct skiplist *l)
{
	register int k;
	register struct skiplistnode *p, *q;
	int nodelevel = 0;

	CHECKLAST(l);

	p = l->header;
	q = l->header->forward[0];

	if (!q)
		return -1;

	for (k = l->level; k >= 0; --k) {
		if (p->forward[k] == q) {
			p->forward[k] = q->forward[k];
			if ((k == l->level) && (p->forward[k] == NULL)
			    && (l->level > 0))
				--(l->level);
			if (!nodelevel)
				nodelevel = k;
		}
	}

#ifdef SKIPLIST_0TIMER_DEBUG
	q->flags &= ~SKIPLIST_NODE_FLAG_INSERTED;
#endif
	/*
	 * If we are deleting the last element of the list,
	 * update the list's "last" pointer.
	 */
	if (l->last == q) {
		l->last = NULL;
	}

	--(l->level_stats[nodelevel]);
#ifdef SKIPLIST_DEBUG
	zlog_debug("%s: decremented level_stats @%p:%d, now %d", __func__, l,
		   nodelevel, l->level_stats[nodelevel]);
#endif

	if (l->del)
		(*l->del)(q->value);

	XFREE(MTYPE_SKIP_LIST_NODE, q);

	CHECKLAST(l);

	--(l->count);

	return 0;
}

/*
 * skiplist_debug：向 vty 输出跳表信息：最大层号与各层节点计数（调试用）。
 */
void skiplist_debug(struct vty *vty, struct skiplist *l)
{
	int i;

	if (!l)
		return;

	vty_out(vty, "Skiplist %p has max level %d\n", l, l->level);
	for (i = l->level; i >= 0; --i)
		vty_out(vty, "  @%d: %d\n", i, l->level_stats[i]);
}

/*
 * scramble：把整数打散成“看起来随机”的指针值，供 skiplist_test 造测试键用。
 * 返回：打散后的伪指针值。
 */
static void *scramble(int i)
{
	uintptr_t result;

	result = (unsigned)(i & 0xff) << 24;
	result |= (unsigned)i >> 8;

	return (void *)result;
}

/*
 * 自测样本规模：65536 个元素。
 */
#define sampleSize 65536
/*
 * skiplist_test：跳表自测驱动：多轮执行“批量插入 -> 查找 -> 删除并重插 ->
 * 逐个删除”，并打印进度；供开发期验证实现正确性（生产不调用）。
 */
void skiplist_test(struct vty *vty)
{
	struct skiplist *l;
	register int i, k;
	void *keys[sampleSize];
	void *v = NULL;

	zlog_debug("%s: entry", __func__);

	l = skiplist_new(SKIPLIST_FLAG_ALLOW_DUPLICATES, NULL, NULL);

	zlog_debug("%s: skiplist_new returned %p", __func__, l);

	for (i = 0; i < 4; i++) {

		for (k = 0; k < sampleSize; k++) {
			if (!(k % 1000)) {
				zlog_debug("%s: (%d:%d)", __func__, i, k);
			}
			// keys[k] = (void *)random();
			keys[k] = scramble(k);
			if (skiplist_insert(l, keys[k], keys[k]))
				zlog_debug("error in insert #%d,#%d", i, k);
		}

		zlog_debug("%s: inserts done", __func__);

		for (k = 0; k < sampleSize; k++) {

			if (!(k % 1000))
				zlog_debug("[%d:%d]", i, k);
			if (skiplist_search(l, keys[k], &v))
				zlog_debug("error in search #%d,#%d", i, k);

			if (v != keys[k])
				zlog_debug("search returned wrong value");
		}


		for (k = 0; k < sampleSize; k++) {

			if (!(k % 1000))
				zlog_debug("<%d:%d>", i, k);
			if (skiplist_delete(l, keys[k], keys[k]))
				zlog_debug("error in delete");
			keys[k] = scramble(k ^ 0xf0f0f0f0);
			if (skiplist_insert(l, keys[k], keys[k]))
				zlog_debug("error in insert #%d,#%d", i, k);
		}

		for (k = 0; k < sampleSize; k++) {

			if (!(k % 1000))
				zlog_debug("{%d:%d}", i, k);
			if (skiplist_delete_first(l))
				zlog_debug("error in delete_first");
		}
	}

	skiplist_free(l);
}
