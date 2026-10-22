// SPDX-License-Identifier: LicenseRef-Skiplist-BSD-0-Clause
/*
 * Copyright 1990 William Pugh
 *
 * Permission to include in quagga provide on March 31, 2016
 */

/*
 * Skip List implementation based on code from William Pugh.
 * ftp://ftp.cs.umd.edu/pub/skipLists/
 */

/* skiplist.h */


/*
 * ============================================================================
 * 文件导读：skiplist.h —— 跳表（skip list）接口
 * ============================================================================
 * 本头文件声明 FRR 里的跳表容器，源自 William Pugh 的经典实现（BSD-0-Clause）。
 * 跳表是一种“有序链表 + 多层索引”的概率型数据结构：
 *   - 平均查找 / 插入 / 删除复杂度 O(log N)，最坏 O(N)
 *   - 相比平衡树实现更简单，天然有序并支持范围遍历
 *
 * 核心数据结构：
 *   - struct skiplistnode  节点：key、value、forward[] 各层后继指针（变长数组）
 *   - struct skiplist      表：header 哨兵头、level 当前层数、count 元素数、
 *                          cmp 排序回调、del 值释放回调
 *
 * 关键约定：
 *   - cmp(key1, key2)：返回 -1 / 0 / 1 定义排序；传 NULL 用默认比较
 *   - del(val)：删除节点时释放用户数据，强烈建议提供，否则会泄漏
 *   - 允许重复键时需置 SKIPLIST_FLAG_ALLOW_DUPLICATES
 *
 * 与其他模块的关系：
 *   - skiplist_debug / skiplist_test 依赖 vty，供 CLI 调试与自测
 *
 * 阅读顺序：
 *   struct skiplistnode -> struct skiplist -> skiplist_new / insert / search
 *   -> skiplist_first_value / next_value（带游标的有序遍历）
 * ============================================================================
 */
#ifndef _ZEBRA_SKIPLIST_H
#define _ZEBRA_SKIPLIST_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 调试开关：开启后节点带 flags 字段，记录“已插入”等调试信息。
 */
#define SKIPLIST_0TIMER_DEBUG 1

/*
 * 节点语义：节点必须有数据才算有效，禁止插入空节点。
 */
/*
 * skiplistnodes must always contain data to be valid. Adding an
 * empty node to a list is invalid
 */
/*
 * 跳表节点。
 *   key       —— 排序键
 *   value     —— 关联数据
 *   flags     —— 仅调试模式存在（如 SKIPLIST_NODE_FLAG_INSERTED）
 *   forward[] —— 各层后继指针，声明为长度 1 的变长数组（按层数多分配）
 */
struct skiplistnode {
	void *key;
	void *value;
#if SKIPLIST_0TIMER_DEBUG
	int flags;
#define SKIPLIST_NODE_FLAG_INSERTED 0x00000001
#endif

	struct skiplistnode *forward[1]; /* variable sized */
};

/*
 * 跳表主体。
 *   flags       —— 行为标志（如 SKIPLIST_FLAG_ALLOW_DUPLICATES 允许重复键）
 *   level       —— 当前最大层数（= 1 + 已用层数）
 *   count       —— 元素个数
 *   header      —— 哨兵头节点（不存数据）
 *   level_stats —— 各层节点计数（调试 / 统计用）
 *   last        —— 最后一个真实节点（空表为 NULL），便于 O(1) 取尾
 *   cmp         —— 排序比较回调：返回 -1 / 0 / 1
 *   del         —— 删除节点时释放用户数据的回调，强烈建议提供
 */
struct skiplist {
	int flags;

#define SKIPLIST_FLAG_ALLOW_DUPLICATES	0x00000001

	int level; /* max lvl (1 + current # of levels in list) */
	unsigned int count;
	struct skiplistnode *header;
	int *level_stats;
	struct skiplistnode
		*last; /* last real list item (NULL if empty list) */

	/*
	 * Returns -1 if val1 < val2, 0 if equal?, 1 if val1 > val2.
	 * Used as definition of sorted for listnode_add_sort
	 */
	int (*cmp)(const void *val1, const void *val2);

	/* callback to free user-owned data when listnode is deleted. supplying
	 * this callback is very much encouraged!
	 */
	void (*del)(void *val);
};


/* 函数原型声明。 */
/* Prototypes. */
/*
 * skiplist_new：创建跳表。
 * 参数：flags 行为标志；cmp 排序回调（NULL 用默认比较）；
 *       del 删除节点时释放 value 的回调（NULL 则不自动释放，强烈建议提供）。
 * 返回：新跳表指针。
 */
extern struct skiplist *
skiplist_new(/* encouraged: set list.del callback on new lists */
	     int flags,
	     int (*cmp)(const void *key1,
			const void *key2), /* NULL => default cmp */
	     void (*del)(void *val));	   /* NULL => no auto val free */

/*
 * skiplist_free：释放整张跳表（含所有节点；如有 del 回调会一并释放各 value）。
 */
extern void skiplist_free(struct skiplist *l);

/*
 * skiplist_insert：插入一个 key / value 对。
 * 返回：成功 0，失败 -1。
 * 注意：是否允许重复键由 SKIPLIST_FLAG_ALLOW_DUPLICATES 决定。
 */
extern int skiplist_insert(register struct skiplist *l, register void *key,
			   register void *value);

/*
 * skiplist_delete：删除指定 key（配合 value 精确定位）的一个节点。
 * 返回：成功 0，未找到 / 失败 -1。
 */
extern int skiplist_delete(register struct skiplist *l, register void *key,
			   register void *value);

/*
 * skiplist_search：查找指定 key 对应的 value。
 * 参数：valuePointer 输出命中的值。
 * 返回：成功 0，未找到 -1。
 */
extern int skiplist_search(register struct skiplist *l, register void *key,
			   void **valuePointer);

/*
 * skiplist_first_value：取“第一个键 >= key”的节点值，并初始化游标，便于遍历重复键。
 * 参数：valuePointer 输出值；cursor 输出游标。
 * 返回：成功 0，无匹配 -1。
 */
extern int skiplist_first_value(register struct skiplist *l, /* in */
				register const void *key,    /* in */
				void **valuePointer,	     /* in/out */
				void **cursor);		     /* out */

/*
 * skiplist_next_value：配合 skiplist_first_value 使用，用游标取下一个相同 key 的值。
 * 返回：成功 0，无更多 -1。
 */
extern int skiplist_next_value(register struct skiplist *l, /* in */
			       register const void *key,	  /* in */
			       void **valuePointer,	 /* in/out */
			       void **cursor);		    /* in/out */

/*
 * skiplist_first：取表中第一个（最小键）节点，输出其 key 与 value。
 * 返回：成功 0，空表 -1。
 */
extern int skiplist_first(register struct skiplist *l, void **keyPointer,
			  void **valuePointer);

/*
 * skiplist_last：取表中最后一个（最大键）节点，输出其 key 与 value。
 * 返回：成功 0，空表 -1。
 */
extern int skiplist_last(register struct skiplist *l, void **keyPointer,
			 void **valuePointer);

/*
 * skiplist_delete_first：删除表中第一个（最小键）节点。
 * 返回：成功 0，空表 -1。
 */
extern int skiplist_delete_first(register struct skiplist *l);

/*
 * skiplist_next：从游标位置向前遍历，输出节点的 key 与 value。
 * 参数：cursor 既是输入（当前游标）也是输出（下一个游标）。
 * 返回：成功 0，到达末尾 -1。
 */
extern int skiplist_next(register struct skiplist *l, /* in */
			 void **keyPointer,	   /* out */
			 void **valuePointer,	 /* out */
			 void **cursor);	      /* in/out */

/*
 * skiplist_empty：判断跳表是否为空。
 * 返回：空返回 1，非空返回 0。
 */
extern int skiplist_empty(register struct skiplist *l); /* in */

/*
 * skiplist_count：返回跳表元素个数。
 */
extern unsigned int skiplist_count(register struct skiplist *l); /* in */

/* 前向声明：CLI 输出对象（定义见 vty.h）。 */
struct vty;
/*
 * skiplist_debug：在 CLI 上打印跳表结构（各层 / 各节点），用于调试。
 */
extern void skiplist_debug(struct vty *vty, struct skiplist *l);

/*
 * skiplist_test：运行跳表自测（随机插入 / 删除 / 查找），供开发验证。
 */
extern void skiplist_test(struct vty *vty);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_SKIPLIST_H */
