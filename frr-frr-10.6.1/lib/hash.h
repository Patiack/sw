// SPDX-License-Identifier: GPL-2.0-or-later
/* Hash routine.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * ============================================================================
 * 文件导读：hash.h —— FRR 通用哈希表（拉链 hash table）接口
 * ============================================================================
 * 本头文件声明了 FRR 早期通用的哈希表 struct hash 及其全部操作。
 *
 * 核心数据结构：
 *   - struct hash        哈希表主体：index 是桶数组，size 必须是 2 的幂
 *   - struct hash_bucket 单个桶项，同桶冲突元素用 next 串成单链表
 *   - struct hashstats   运行期统计（空桶数 / 桶长平方和），用于全局统计展示
 *
 * 关键回调约定：
 *   - hash_key(data)  由用户提供，返回哈希值；冲突交给链表化解
 *   - hash_cmp(a, b)  相等比较函数：返回 true 表示“相等”而不是“不同”，切勿写反
 *
 * 复杂度：
 *   - 理想哈希函数下查找 / 插入 / 删除平均 O(1)，最坏 O(N)（常数哈希函数）
 *   - 元素数超过桶数（HASH_THRESHOLD）时自动按“桶数翻倍”扩容并重新散列
 *
 * 与其他模块的关系：
 *   - 依赖 memory.h 做内存记录，依赖 frratomic.h 做原子统计
 *   - hash_to_list 可与 lib/linklist.h 的 struct list 互转
 *   - 新代码建议优先使用 typesafe.h 里的 *_hash 系列（编译期类型安全）
 *
 * 阅读顺序：
 *   1) HASH_INITIAL_SIZE / HASH_THRESHOLD 了解初始容量与扩容策略
 *   2) struct hash_bucket / hash / hashstats 三个结构体
 *   3) 常用流程：hash_get -> hash_lookup -> hash_release -> hash_iterate
 *
 * 注意：本文件（及 hash.c）属“老式”容器，新代码请优先使用 typesafe.h。
 * ============================================================================
 */
#ifndef _ZEBRA_HASH_H
#define _ZEBRA_HASH_H

#include "memory.h"
#include "frratomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOTICE:
 *
 * If you are reading this file in an effort to add a new hash structure
 * this is the wrong place to be using it.  Please see the typesafe
 * data structures, or ask one of the other developers.
 *
 * If you are reading this file as a way to update an existing usage
 * of this data structure, please consider just converting the data
 * structure to one of the typesafe data structures instead.
 */

/*
 * 哈希表默认初始桶数，必须是 2 的幂（这样可以用位与代替取模做索引）。
 */
/* Default hash table size.  */
#define HASH_INITIAL_SIZE 256
/*
 * 扩容阈值：当元素数 used 严格大于桶数 size 时，触发桶数翻倍与重新散列。
 */
/* Expansion threshold */
#define HASH_THRESHOLD(used, size) ((used) > (size))

/*
 * hash_walk 回调的返回值约定：
 *   HASHWALK_CONTINUE 继续遍历；HASHWALK_ABORT 立即停止遍历。
 */
#define HASHWALK_CONTINUE 0
#define HASHWALK_ABORT -1

/*
 * 哈希桶：对应桶数组 index[] 中的一格，同桶内冲突元素用链表串起来。
 *   len   —— 仅链表头节点的 len 有效，表示该冲突链上的元素个数
 *   key   —— 该元素参与哈希的键值（哈希函数算出的结果）
 *   next  —— 指向同桶下一个元素，链表尾为 NULL
 *   data  —— 用户真正存放的数据指针，保证非 NULL
 */
struct hash_bucket {
	/*
	 * if this bucket is the head of the linked listed, len denotes the
	 * number of elements in the list
	 */
	int len;

	/* Hash key. */
	unsigned int key;

	/* Linked list.  */
	struct hash_bucket *next;

	/* Data.  */
	void *data;
};

/*
 * 哈希表统计信息，供全局 hash 统计命令展示。
 *   empty —— 空桶数量（无任何元素的桶）
 *   ssq   —— 各桶长度的平方和，用于评估冲突分布是否均匀
 * 字段使用原子类型，便于在遍历统计时避免持有锁。
 */
struct hashstats {
	/* number of empty hash buckets */
	atomic_uint_fast32_t empty;
	/* sum of squares of bucket length */
	atomic_uint_fast32_t ssq;
};

/*
 * 哈希表主体。
 *   index    —— 桶数组指针，每个元素是一条冲突链表
 *   size     —— 桶数组长度，必须是 2 的幂（用位与代替取模）
 *   max_size —— 桶数上限，0 表示不限制
 *   hash_key —— 用户提供的哈希函数
 *   hash_cmp —— 用户提供的相等比较函数（相等返回 true）
 *   count    —— 当前元素总数
 *   stats    —— 运行期统计
 *   name     —— 哈希表名称（用于统计展示），可为 NULL
 */
struct hash {
	/* Hash bucket. */
	struct hash_bucket **index;

	/* Hash table size. Must be power of 2 */
	unsigned int size;

	/* If max_size is 0 there is no limit */
	unsigned int max_size;

	/* Key make function. */
	unsigned int (*hash_key)(const void *data);

	/* Data compare function. */
	bool (*hash_cmp)(const void *data1, const void *data2);

	/* Bucket alloc. */
	unsigned long count;

	struct hashstats stats;

	/* hash name */
	char *name;
};

/* 宏：读取哈希表中元素总数。 */
#define hashcount(X) ((X)->count)

/*
 * Create a hash table.
 *
 * The created hash table uses chaining and a user-provided comparator function
 * to resolve collisions. For best performance use a perfect hash function.
 * Worst case lookup time is O(N) when using a constant hash function. Best
 * case lookup time is O(1) when using a perfect hash function.
 *
 * The initial size of the created hash table is HASH_INITIAL_SIZE.
 *
 * hash_key
 *    hash function to use; should return a unique unsigned integer when called
 *    with a data item. Collisions are acceptable.
 *
 * hash_cmp
 *    comparison function used for resolving collisions; when called with two
 *    data items, should return true if the two items are equal and false
 *    otherwise
 *
 * name
 *    optional name for the hashtable; this is used when displaying global
 *    hashtable statistics. If this parameter is NULL the hash's name will be
 *    set to NULL and the default name will be displayed when showing
 *    statistics.
 *
 * Returns:
 *    a new hash table
 */
/*
 * hash_create：用默认初始桶数（HASH_INITIAL_SIZE）创建哈希表。
 * 参数：hash_key 哈希函数；hash_cmp 相等比较函数（相等必须返回 true）；
 *       name 可选名称，用于统计展示，可为 NULL。
 * 返回：新建哈希表指针。
 */
extern struct hash *hash_create(unsigned int (*hash_key)(const void *data),
				bool (*hash_cmp)(const void *data1, const void *data2),
				const char *name);

/*
 * Create a hash table.
 *
 * The created hash table uses chaining and a user-provided comparator function
 * to resolve collisions. For best performance use a perfect hash function.
 * Worst case lookup time is O(N) when using a constant hash function. Best
 * case lookup time is O(1) when using a perfect hash function.
 *
 * size
 *    initial number of hash buckets to allocate; must be a power of 2 or the
 *    program will assert
 *
 * hash_key
 *    hash function to use; should return a unique unsigned integer when called
 *    with a data item. Collisions are acceptable.
 *
 * hash_cmp
 *    comparison function used for resolving collisions; when called with two
 *    data items, should return true if the two items are equal and false
 *    otherwise
 *
 * name
 *    optional name for the hashtable; this is used when displaying global
 *    hashtable statistics. If this parameter is NULL the hash's name will be
 *    set to NULL and the default name will be displayed when showing
 *    statistics.
 *
 * Returns:
 *    a new hash table
 */
/*
 * hash_create_size：按指定初始桶数创建哈希表。
 * 参数：size 初始桶数，必须是 2 的幂，否则 assert 失败；其余同 hash_create。
 * 返回：新建哈希表指针。
 * 用途：已知元素规模时一次性给足桶数，避免运行期反复扩容。
 */
extern struct hash *
hash_create_size(unsigned int size, unsigned int (*hash_key)(const void *data),
		 bool (*hash_cmp)(const void *data1, const void *data2),
		 const char *name);

/*
 * Retrieve or insert data from / into a hash table.
 *
 * This function is somewhat counterintuitive in its usage. In order to look up
 * an element from its key, you must provide the data item itself, with the
 * portions used in the hash function set to the same values as the data item
 * to retrieve. To insert a data element, either provide the key as just
 * described and provide alloc_func as described below to allocate the full
 * data element, or provide the full data element and pass 'hash_alloc_intern'
 * to alloc_func.
 *
 * hash
 *    hash table to operate on
 *
 * data
 *    data to insert or retrieve - A hash bucket will not be created if
 *    the alloc_func returns a NULL pointer and nothing will be added to
 *    the hash.  As such bucket->data will always be non-NULL.
 *
 * alloc_func
 *    function to call if the item is not found in the hash table. This
 *    function is called with the value of 'data' and should create the data
 *    item to insert and return a pointer to it. If the data has already been
 *    completely created and provided in the 'data' parameter, passing
 *    'hash_alloc_intern' to this parameter will cause 'data' to be inserted.
 *    If this parameter is NULL, then this call to hash_get is equivalent to
 *    hash_lookup.
 *
 * Returns:
 *    the data item found or inserted, or NULL if alloc_func is NULL and the
 *    data is not found
 */
/*
 * hash_get：查找；未命中则按 alloc_func 回调插入（“查不到就建”）。
 * 参数：data 提供用于计算哈希键的字段；alloc_func 为未命中时的分配回调：
 *       返回 NULL 则不插入；传 hash_alloc_intern 表示把 data 本身插入；
 *       传 NULL 时等价于 hash_lookup（只查不建）。
 * 返回：命中或新插入的元素；alloc_func 为 NULL 且未命中时返回 NULL。
 * 坑：查找时必须提供与插入一致的键字段，否则会定位到错误的桶。
 */
extern void *hash_get(struct hash *hash, void *data,
		      void *(*alloc_func)(void *data));

/*
 * Dummy element allocation function.
 *
 * See hash_get for details.
 *
 * data
 *    data to insert into the hash table
 *
 * Returns:
 *    data
 */
/*
 * hash_alloc_intern：内置的“空分配”回调，原样返回 data。
 * 作为 hash_get 的 alloc_func 传入时，表示 data 已是完整元素，直接插入而不复制。
 */
extern void *hash_alloc_intern(void *data);

/*
 * Retrieve an item from a hash table.
 *
 * This function is equivalent to calling hash_get with alloc_func set to NULL.
 *
 * hash
 *    hash table to operate on
 *
 * data
 *    data element with values used for key computation set
 *
 * Returns:
 *    the data element if found, or NULL if not found
 */
/*
 * hash_lookup：只查找、不插入（等价于 alloc_func 传 NULL 的 hash_get）。
 * 参数：data 需按哈希键字段填好。
 * 返回：命中的元素；未命中返回 NULL。
 */
extern void *hash_lookup(struct hash *hash, void *data);

/*
 * Remove an element from a hash table.
 *
 * hash
 *    hash table to operate on
 *
 * data
 *    data element to remove with values used for key computation set
 *
 * Returns:
 *    the removed element if found, or NULL if not found
 */
/*
 * hash_release：从哈希表中摘除一个元素（仅解除挂载，不释放 data 内存）。
 * 参数：data 需按哈希键字段填好。
 * 返回：被摘除的元素指针；未找到返回 NULL。
 * 注意：data 所指向内存的释放由调用者负责。
 */
extern void *hash_release(struct hash *hash, void *data);

/*
 * Iterate over the elements in a hash table.
 *
 * The passed in arg to the handler function is the only safe
 * item to delete from the hash.
 *
 * Please note that adding entries to the hash
 * during the walk will cause undefined behavior in that some new entries
 * will be walked and some will not.  So do not do this.
 *
 * The bucket passed to func will have a non-NULL data pointer.
 *
 * hash
 *    hash table to operate on
 *
 * func
 *    function to call with each data item
 *
 * arg
 *    arbitrary argument passed as the second parameter in each call to 'func'
 */
/*
 * hash_iterate：遍历哈希表全部元素。
 * 参数：func 对每个桶项调用的回调（bucket->data 保证非 NULL）；arg 为透传参数。
 * 语义：遍历期间不得新增元素，否则行为未定义（部分新元素可能被遍历到）。
 * 唯一安全可删除的是当前回调正在处理的那个元素。
 */
extern void hash_iterate(struct hash *hash,
			 void (*func)(struct hash_bucket *bucket, void *arg),
			 void *arg);

/*
 * Iterate over the elements in a hash table, stopping on condition.
 *
 * The passed in arg to the handler function is the only safe item
 * to delete from the hash.
 *
 * Please note that adding entries to the hash
 * during the walk will cause undefined behavior in that some new entries
 * will be walked and some will not.  So do not do this.
 *
 * The bucket passed to func will have a non-NULL data pointer.
 *
 * hash
 *    hash table to operate on
 *
 * func
 *    function to call with each data item. If this function returns
 *    HASHWALK_ABORT then the iteration stops.
 *
 * arg
 *    arbitrary argument passed as the second parameter in each call to 'func'
 */
/*
 * hash_walk：可提前中断的遍历，回调返回 HASHWALK_ABORT 时立即停止。
 * 参数与限制同 hash_iterate，区别是能通过回调返回值提前结束遍历。
 */
extern void hash_walk(struct hash *hash,
		      int (*func)(struct hash_bucket *bucket, void *arg),
		      void *arg);

/*
 * Remove all elements from a hash table.
 *
 * hash
 *    hash table to operate on
 *
 * free_func
 *    function to call with each removed item; intended to free the data
 */
/*
 * hash_clean：清空哈希表所有元素，并对每个元素调用 free_func（通常用于释放内存）。
 * 注意：只清空元素，不释放哈希表结构本身。
 */
extern void hash_clean(struct hash *hash, void (*free_func)(void *data));

/*
 * Remove all elements from a hash table and free the table,
 * setting the pointer to NULL.
 *
 * hash
 *    hash table to operate on
 * free_func
 *    function to call with each removed item, intended to free the data
 */
/*
 * hash_clean_and_free：清空元素后释放哈希表本身，并把 *hash 置为 NULL。
 * 参数：hash 是“哈希表指针的指针”（二级指针），便于置空以防悬垂指针。
 */
extern void hash_clean_and_free(struct hash **hash, void (*free_func)(void *data));

/*
 * Delete a hash table.
 *
 * This function assumes the table is empty. Call hash_clean to delete the
 * hashtable contents if necessary.
 *
 * hash
 *    hash table to delete
 */
/*
 * hash_free：释放哈希表结构本身（要求表已为空）。
 * 若表中仍有元素，请先调用 hash_clean 清空。
 */
extern void hash_free(struct hash *hash);

/*
 * Converts a hash table to an unsorted linked list.
 * Does not modify the hash table in any way.
 *
 * hash
 *    hash table to convert
 */
/*
 * hash_to_list：把哈希表所有元素转成一个无序链表（struct list）。
 * 不修改原哈希表；返回的链表需由调用者负责释放。
 */
extern struct list *hash_to_list(struct hash *hash);

/*
 * Hash a string using the modified Bernstein hash.
 *
 * This is not a perfect hash function.
 *
 * str
 *    string to hash
 *
 * Returns:
 *    modified Bernstein hash of the string
 */
/*
 * string_hash_make：对 C 字符串计算哈希值（改进版 Bernstein 哈希）。
 * 非完美哈希，允许冲突。
 * 返回：字符串对应的哈希值。
 */
extern unsigned int string_hash_make(const char *str);

/*
 * Install CLI commands for viewing global hash table statistics.
 */
/*
 * hash_cmd_init：注册用于查看全局哈希表统计信息的 CLI 命令。
 */
extern void hash_cmd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_HASH_H */
