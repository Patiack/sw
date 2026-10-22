// SPDX-License-Identifier: GPL-2.0-or-later
/* Hash routine.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * ============================================================================
 * 文件导读：hash.c —— FRR 通用哈希表（拉链法）实现
 * ============================================================================
 * 本文件实现 hash.h 声明的哈希表：桶数组加上单链表拉链解决冲突。
 *
 * 核心机制：
 *   - index 桶数组长度恒为 2 的幂，用 key 按位与 (size-1) 代替取模求桶下标
 *   - 每个桶项 struct hash_bucket 记录 key / data / next，冲突挂到同一链上
 *   - 只有链表头节点的 len 有效，记录该链长度，便于 O(1) 维护统计
 *   - stats.ssq 是各桶长度的平方和，用增量公式 (new^2 - old^2) 维护
 *
 * 扩容：
 *   - 元素数超过桶数时调用 hash_expand，桶数翻倍并重新散列所有元素
 *   - max_size 非 0 时限定了桶数上限
 *
 * 全局登记：
 *   - 所有创建的哈希表登记在全局链表 _hashes 中（_hashes_mtx 保护），
 *     供“show debugging hashtable”统计命令枚举
 *
 * 与其他模块的关系：
 *   - 依赖 memory.h 做内存记账，依赖 linklist.h 做 hash_to_list
 *   - 依赖 termtable.h / vty.h / command.h 实现统计 CLI
 *   - 新代码建议改用 typesafe.h 的类型安全哈希
 *
 * 阅读顺序：
 *   hash_create_size -> hash_get（含扩容与统计维护）-> hash_release
 *   -> hash_iterate / hash_walk -> 末尾的统计 CLI
 * ============================================================================
 */
#include <zebra.h>
#include <math.h>

#include "hash.h"
#include "memory.h"
#include "linklist.h"
#include "termtable.h"
#include "vty.h"
#include "command.h"
#include "libfrr.h"
#include "frr_pthread.h"
#include "libfrr_trace.h"

/*
 * 内存类型登记：把哈希表本体 / 桶项 / 桶数组三类分配纳入 FRR 内存记账。
 */
DEFINE_MTYPE_STATIC(LIB, HASH, "Hash");
DEFINE_MTYPE_STATIC(LIB, HASH_BUCKET, "Hash Bucket");
DEFINE_MTYPE_STATIC(LIB, HASH_INDEX, "Hash Index");

/*
 * 全局哈希表登记表：所有 hash_create* 创建的表都会挂到 _hashes，
 * 便于统计命令统一枚举；_hashes_mtx 保护对 _hashes 的并发访问。
 */
static pthread_mutex_t _hashes_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct list *_hashes;

/*
 * hash_create_size：真正干活儿的创建函数（hash_create 只是给它传默认桶数）。
 * 断言 size 是 2 的幂；分配并清零表结构、桶数组，并登记到全局 _hashes。
 * 返回：新表指针。
 * 副作用：向全局 _hashes 链表登记；name 会被复制一份保存。
 */
struct hash *hash_create_size(unsigned int size,
			      unsigned int (*hash_key)(const void *),
			      bool (*hash_cmp)(const void *, const void *),
			      const char *name)
{
	struct hash *hash;

	assert((size & (size - 1)) == 0);
	hash = XCALLOC(MTYPE_HASH, sizeof(struct hash));
	hash->index =
		XCALLOC(MTYPE_HASH_INDEX, sizeof(struct hash_bucket *) * size);
	hash->size = size;
	hash->hash_key = hash_key;
	hash->hash_cmp = hash_cmp;
	hash->count = 0;
	hash->name = name ? XSTRDUP(MTYPE_HASH, name) : NULL;
	hash->stats.empty = hash->size;

	frr_with_mutex (&_hashes_mtx) {
		if (!_hashes)
			_hashes = list_new();

		listnode_add(_hashes, hash);
	}

	return hash;
}

/*
 * hash_create：以默认桶数 HASH_INITIAL_SIZE 创建哈希表。
 * 返回：新表指针（内部转调 hash_create_size）。
 */
struct hash *hash_create(unsigned int (*hash_key)(const void *),
			 bool (*hash_cmp)(const void *, const void *),
			 const char *name)
{
	return hash_create_size(HASH_INITIAL_SIZE, hash_key, hash_cmp, name);
}

/*
 * hash_alloc_intern：hash_get 的默认插入回调，原样返回传入的 data，
 * 表示“我已构造好完整元素，直接挂进哈希表”。
 */
void *hash_alloc_intern(void *arg)
{
	return arg;
}

/*
 * hash_update_ssq：增量维护“桶长平方和”统计 ssq。
 * 公式 ssq += new^2 - old^2 = (new+old)*(new-old)，避免每次全表重算。
 * 结果可能为负，故按符号分别做原子加 / 减。
 */
/*
 * ssq = ssq + (new^2 - old^2)
 *     = ssq + ((new + old) * (new - old))
 */
#define hash_update_ssq(hz, old, new)                                          \
	do {                                                                   \
		int _adjust = (new + old) * (new - old);                       \
		if (_adjust < 0)                                               \
			atomic_fetch_sub_explicit(&hz->stats.ssq, -_adjust,    \
						  memory_order_relaxed);       \
		else                                                           \
			atomic_fetch_add_explicit(&hz->stats.ssq, _adjust,     \
						  memory_order_relaxed);       \
	} while (0)

/*
 * hash_expand：把桶数组大小翻倍，并重新散列全部元素。
 * 参数：hash 目标表。
 * 副作用：改写 index / size，并重算 stats.empty 与 stats.ssq。
 * 坑：max_size 非 0 且新尺寸超限时直接返回（不扩容）。
 */
/* Expand hash if the chain length exceeds the threshold. */
static void hash_expand(struct hash *hash)
{
	unsigned int i, new_size;
	struct hash_bucket *hb, *hbnext, **new_index;

	new_size = hash->size * 2;

	if (hash->max_size && new_size > hash->max_size)
		return;

	new_index = XCALLOC(MTYPE_HASH_INDEX,
			    sizeof(struct hash_bucket *) * new_size);

	hash->stats.empty = new_size;

	for (i = 0; i < hash->size; i++)
		for (hb = hash->index[i]; hb; hb = hbnext) {
			unsigned int h = hb->key & (new_size - 1);

			hbnext = hb->next;
			hb->next = new_index[h];

			int oldlen = hb->next ? hb->next->len : 0;
			int newlen = oldlen + 1;

			if (newlen == 1)
				hash->stats.empty--;
			else
				hb->next->len = 0;

			hb->len = newlen;

			hash_update_ssq(hash, oldlen, newlen);

			new_index[h] = hb;
		}

	/* Switch to new table */
	XFREE(MTYPE_HASH_INDEX, hash->index);
	hash->size = new_size;
	hash->index = new_index;
}

/*
 * hash_get：查找；未命中则用 alloc_func 构造并插入。
 * 流程：算 key -> 定位桶 -> 沿链比较（key 相同且 hash_cmp 为真即命中）
 *       -> 未命中且 alloc_func 非空时先判断是否扩容，再新建桶项插入。
 * 返回：命中或新插入的元素；alloc_func 为 NULL 且未命中时返回 NULL。
 * 副作用：可能触发 hash_expand；维护 count / empty / ssq 统计。
 * 坑：alloc_func 返回 NULL 时不插入（bucket->data 恒非 NULL）。
 */
void *hash_get(struct hash *hash, void *data, void *(*alloc_func)(void *))
{
	frrtrace(2, frr_libfrr, hash_get, hash, data);

	unsigned int key;
	unsigned int index;
	void *newdata;
	struct hash_bucket *bucket;

	if (!alloc_func && !hash->count)
		return NULL;

	key = (*hash->hash_key)(data);
	index = key & (hash->size - 1);

	for (bucket = hash->index[index]; bucket != NULL;
	     bucket = bucket->next) {
		if (bucket->key == key && (*hash->hash_cmp)(bucket->data, data))
			return bucket->data;
	}

	if (alloc_func) {
		newdata = (*alloc_func)(data);
		if (newdata == NULL)
			return NULL;

		if (HASH_THRESHOLD(hash->count + 1, hash->size)) {
			hash_expand(hash);
			index = key & (hash->size - 1);
		}

		bucket = XCALLOC(MTYPE_HASH_BUCKET, sizeof(struct hash_bucket));
		bucket->data = newdata;
		bucket->key = key;
		bucket->next = hash->index[index];
		hash->index[index] = bucket;
		hash->count++;

		frrtrace(3, frr_libfrr, hash_insert, hash, data, key);

		int oldlen = bucket->next ? bucket->next->len : 0;
		int newlen = oldlen + 1;

		if (newlen == 1)
			hash->stats.empty--;
		else
			bucket->next->len = 0;

		bucket->len = newlen;

		hash_update_ssq(hash, oldlen, newlen);

		return bucket->data;
	}
	return NULL;
}

/*
 * hash_lookup：只查不建的薄封装，等价于 alloc_func 传 NULL 的 hash_get。
 */
void *hash_lookup(struct hash *hash, void *data)
{
	return hash_get(hash, data, NULL);
}

/*
 * string_hash_make：改进版 Bernstein 哈希：h = h*33 异或 当前字符（33 = 32 + 1，便于移位）。
 * 返回：字符串的 32 位哈希值；空串返回 0。
 */
unsigned int string_hash_make(const char *str)
{
	unsigned int hash = 0;

	while (*str)
		hash = (hash * 33) ^ (unsigned int)*str++;

	return hash;
}

/*
 * hash_release：在桶链上查找并摘除匹配元素（仅摘链，不释放 data 内存）。
 * 参数：data 提供键字段；内部用 pp 记录前驱以便摘链。
 * 返回：被摘除元素；未找到返回 NULL。
 * 副作用：维护链表头 len、count、empty、ssq；释放桶项结构体内存。
 */
void *hash_release(struct hash *hash, void *data)
{
	void *ret = NULL;
	unsigned int key;
	unsigned int index;
	struct hash_bucket *bucket;
	struct hash_bucket *pp;

	key = (*hash->hash_key)(data);
	index = key & (hash->size - 1);

	for (bucket = pp = hash->index[index]; bucket; bucket = bucket->next) {
		if (bucket->key == key
		    && (*hash->hash_cmp)(bucket->data, data)) {
			int oldlen = hash->index[index]->len;
			int newlen = oldlen - 1;

			if (bucket == pp)
				hash->index[index] = bucket->next;
			else
				pp->next = bucket->next;

			if (hash->index[index])
				hash->index[index]->len = newlen;
			else
				hash->stats.empty++;

			hash_update_ssq(hash, oldlen, newlen);

			ret = bucket->data;
			XFREE(MTYPE_HASH_BUCKET, bucket);
			hash->count--;
			break;
		}
		pp = bucket;
	}

	frrtrace(3, frr_libfrr, hash_release, hash, data, ret);

	return ret;
}

/*
 * hash_iterate：遍历所有桶及桶内链，逐个调用 func(hb, arg)。
 * 关键点：进入回调前先缓存 hbnext，因为回调可能删除当前 hb。
 * 语义：遍历期间禁止插入新元素，否则可能漏掉或重复访问。
 */
void hash_iterate(struct hash *hash, void (*func)(struct hash_bucket *, void *),
		  void *arg)
{
	unsigned int i;
	struct hash_bucket *hb;
	struct hash_bucket *hbnext;

	for (i = 0; i < hash->size; i++)
		for (hb = hash->index[i]; hb; hb = hbnext) {
			/* get pointer to next hash bucket here, in case (*func)
			 * decides to delete hb by calling hash_release
			 */
			hbnext = hb->next;
			(*func)(hb, arg);
		}
}

/*
 * hash_walk：同 hash_iterate，但回调返回值可提前终止（返回 HASHWALK_ABORT 时停）。
 * 同样先缓存 hbnext 以允许回调删除当前元素。
 */
void hash_walk(struct hash *hash, int (*func)(struct hash_bucket *, void *),
	       void *arg)
{
	unsigned int i;
	struct hash_bucket *hb;
	struct hash_bucket *hbnext;
	int ret = HASHWALK_CONTINUE;

	for (i = 0; i < hash->size; i++) {
		for (hb = hash->index[i]; hb; hb = hbnext) {
			/* get pointer to next hash bucket here, in case (*func)
			 * decides to delete hb by calling hash_release
			 */
			hbnext = hb->next;
			ret = (*func)(hb, arg);
			if (ret == HASHWALK_ABORT)
				return;
		}
	}
}

/*
 * hash_clean：清空所有元素：对每个元素调用 free_func（可空），释放桶项，
 * 递减 count；清空后把 stats.ssq 置 0、empty 置回 size。
 * 注意：不释放哈希表结构本身。
 */
void hash_clean(struct hash *hash, void (*free_func)(void *))
{
	unsigned int i;
	struct hash_bucket *hb;
	struct hash_bucket *next;

	for (i = 0; i < hash->size; i++) {
		for (hb = hash->index[i]; hb; hb = next) {
			next = hb->next;

			if (free_func)
				(*free_func)(hb->data);

			XFREE(MTYPE_HASH_BUCKET, hb);
			hash->count--;
		}
		hash->index[i] = NULL;
	}

	hash->stats.ssq = 0;
	hash->stats.empty = hash->size;
}

/*
 * hash_clean_and_free：清空元素后释放表本身，并把 *hash 置 NULL（防悬垂）。
 * 参数 *hash 为空时直接返回。
 */
void hash_clean_and_free(struct hash **hash, void (*free_func)(void *))
{
	if (!*hash)
		return;

	hash_clean(*hash, free_func);
	hash_free(*hash);
	*hash = NULL;
}

/*
 * hash_to_list_iter：hash_iterate 的回调，把每个元素的 data 追加到链表尾部。
 */
static void hash_to_list_iter(struct hash_bucket *hb, void *arg)
{
	struct list *list = arg;

	listnode_add(list, hb->data);
}

/*
 * hash_to_list：新建链表，遍历哈希表把元素 data 全部追加进去。
 * 返回：新链表（无序），由调用者负责释放；不修改原哈希表。
 */
struct list *hash_to_list(struct hash *hash)
{
	struct list *list = list_new();

	hash_iterate(hash, hash_to_list_iter, list);
	return list;
}

/*
 * hash_free：先从全局 _hashes 登记表摘除，再释放 name / index 桶数组 / 表本体。
 * 要求：调用前表必须已为空（否则元素内存会泄漏）。
 */
void hash_free(struct hash *hash)
{
	frr_with_mutex (&_hashes_mtx) {
		if (_hashes) {
			listnode_delete(_hashes, hash);
			if (_hashes->count == 0) {
				list_delete(&_hashes);
			}
		}
	}

	XFREE(MTYPE_HASH, hash->name);

	XFREE(MTYPE_HASH_INDEX, hash->index);
	XFREE(MTYPE_HASH, hash);
}


/* CLI commands ------------------------------------------------------------ */

/*
 * show_hash_stats：CLI “show debugging hashtable [statistics]”。
 * 遍历全局 _hashes，对每个有名字的表计算并输出载入因子 LF、全载入因子 FLF、
 * 标准差（整体 / 全量），用以评估哈希函数质量：标准差越小分布越均匀。
 * 返回：CMD_SUCCESS。
 */
DEFUN_NOSH(show_hash_stats,
           show_hash_stats_cmd,
           "show debugging hashtable [statistics]",
           SHOW_STR
           DEBUG_STR
           "Statistics about hash tables\n"
           "Statistics about hash tables\n")
{
	struct hash *h;
	struct listnode *ln;
	struct ttable *tt = ttable_new(&ttable_styles[TTSTYLE_BLANK]);

	ttable_add_row(tt, "Hash table|Buckets|Entries|Empty|LF|SD|FLF|SD");
	tt->style.cell.lpad = 2;
	tt->style.cell.rpad = 1;
	tt->style.corner = '+';
	ttable_restyle(tt);
	ttable_rowseps(tt, 0, BOTTOM, true, '-');

	/* Summary statistics calculated are:
	 *
	 * - Load factor: This is the number of elements in the table divided
	 *   by the number of buckets. Since this hash table implementation
	 *   uses chaining, this value can be greater than 1.
	 *   This number provides information on how 'full' the table is, but
	 *   does not provide information on how evenly distributed the
	 *   elements are.
	 *   Notably, a load factor >= 1 does not imply that every bucket has
	 *   an element; with a pathological hash function, all elements could
	 *   be in a single bucket.
	 *
	 * - Full load factor: this is the number of elements in the table
	 *   divided by the number of buckets that have some elements in them.
	 *
	 * - Std. Dev.: This is the standard deviation calculated from the
	 *   relevant load factor. If the load factor is the mean of number of
	 *   elements per bucket, the standard deviation measures how much any
	 *   particular bucket is likely to deviate from the mean.
	 *   As a rule of thumb this number should be less than 2, and ideally
	 *   <= 1 for optimal performance. A number larger than 3 generally
	 *   indicates a poor hash function.
	 */

	double lf;    // load factor
	double flf;   // full load factor
	double var;   // overall variance
	double fvar;  // full variance
	double stdv;  // overall stddev
	double fstdv; // full stddev

	long double x2;   // h->count ^ 2
	long double ldc;  // (long double) h->count
	long double full; // h->size - h->stats.empty
	long double ssq;  // ssq casted to long double

	pthread_mutex_lock(&_hashes_mtx);
	if (!_hashes) {
		pthread_mutex_unlock(&_hashes_mtx);
		ttable_del(tt);
		vty_out(vty, "No hash tables in use.\n");
		return CMD_SUCCESS;
	}

	for (ALL_LIST_ELEMENTS_RO(_hashes, ln, h)) {
		if (!h->name)
			continue;

		ssq = (long double)h->stats.ssq;
		x2 = h->count * h->count;
		ldc = (long double)h->count;
		full = h->size - h->stats.empty;
		lf = h->count / (double)h->size;
		flf = full ? h->count / (double)(full) : 0;
		var = ldc ? (1.0 / ldc) * (ssq - x2 / ldc) : 0;
		fvar = full ? (1.0 / full) * (ssq - x2 / full) : 0;
		var = (var < .0001) ? 0 : var;
		fvar = (fvar < .0001) ? 0 : fvar;
		stdv = sqrt(var);
		fstdv = sqrt(fvar);

		ttable_add_row(tt, "%s|%d|%ld|%.0f%%|%.2lf|%.2lf|%.2lf|%.2lf",
			       h->name, h->size, h->count,
			       (h->stats.empty / (double)h->size) * 100, lf,
			       stdv, flf, fstdv);
	}
	pthread_mutex_unlock(&_hashes_mtx);

	/* display header */
	char header[] = "Showing hash table statistics for ";
	char underln[sizeof(header) + strlen(frr_protonameinst)];
	memset(underln, '-', sizeof(underln));
	underln[sizeof(underln) - 1] = '\0';
	vty_out(vty, "%s%s\n", header, frr_protonameinst);
	vty_out(vty, "%s\n", underln);

	vty_out(vty, "# allocated: %d\n", _hashes->count);
	vty_out(vty, "# named:     %d\n\n", tt->nrows - 1);

	if (tt->nrows > 1) {
		ttable_colseps(tt, 0, RIGHT, true, '|');
		char *table = ttable_dump(tt, "\n");
		vty_out(vty, "%s\n", table);
		XFREE(MTYPE_TMP_TTABLE, table);
	} else
		vty_out(vty, "No named hash tables to display.\n");

	ttable_del(tt);

	return CMD_SUCCESS;
}

/*
 * hash_cmd_init：在 ENABLE 节点安装 show_hash_stats 命令。
 */
void hash_cmd_init(void)
{
	install_element(ENABLE_NODE, &show_hash_stats_cmd);
}
