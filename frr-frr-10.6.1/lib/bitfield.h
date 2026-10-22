// SPDX-License-Identifier: GPL-2.0-or-later
/* Bitfields
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */
/**
 * A simple bit array implementation to allocate and free IDs. An example
 * of its usage is in allocating link state IDs for OSPFv3 as OSPFv3 has
 * removed all address semantics from LS ID. Another usage can be in
 * allocating IDs for BGP neighbors (and dynamic update groups) for
 * efficient storage of adj-rib-out.
 *
 * An example:
 * #include "bitfield.h"
 *
 * bitfield_t bitfield;
 *
 * bf_init(bitfield, 32);
 * ...
 * bf_assign_index(bitfield, id1);
 * bf_assign_index(bitfield, id2);
 * ...
 * bf_release_index(bitfield, id1);
 */

/*
 * 文件导读（lib/bitfield.h）
 *
 * 本文件实现一个简单的变长位图（bit array），用于分配与回收整数 ID。
 *
 * 典型用途：OSPFv3 为链路状态 ID 分配编号（因 OSPFv3 移除了 LS ID 里的
 * 地址语义）；BGP 为邻居与动态更新组分配编号，以便紧凑存储 adj-rib-out。
 *
 * 核心数据结构：
 *   - word_t：位图的存储单元，为 unsigned int，WORD_SIZE 为其位数。
 *   - bitfield_t：位图本体，data 为动态增长的 word_t 数组；m 为当前已分配
 *     的字数；n 为当前已写满（全 1）的字数，用于加速查找空位。
 *
 * 关键设计：位图按需增长。bf_set_bit 若把当前最后一个字填满且已无空间，
 * 会自动再扩一个字；bf_find_bit 会从头找第一个 0 位并二分定位其偏移。
 *
 * 主要操作：bf_init / bf_free（初始化与释放）、bf_assign_index（分配位）、
 * bf_release_index（释放位）、bf_test_index（查询位）、bf_set_bit /
 * bf_find_bit（底层置位与找空位）、bf_find_next_set_bit /
 * bf_find_next_clear_bit_wrap（遍历）、bf_for_each_set_bit（遍历宏）、
 * bf_copy（拷贝）、bf_cmp（比较）。
 *
 * 与其他模块的关系：被 OSPFv3、BGP 的动态邻居 / 更新组等代码使用；内存
 * 通过 MTYPE_BITFIELD 交给 FRR 的内存跟踪。
 *
 * 阅读顺序：结构 与 宏定义 -> 初始化 / 分配 / 释放宏 -> 找位算法 ->
 * 遍历与拷贝。
 */

#ifndef _BITFIELD_H
#define _BITFIELD_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 位图的存储单元类型及其位数；WORD_MAX 为全 1 的字（表示该字已满）。 */
typedef unsigned int word_t;
#define WORD_MAX 0xFFFFFFFF
#define WORD_SIZE (sizeof(word_t) * 8)

/**
 * The bitfield structure.
 * @data: the bits to manage.
 * @n: The current word number that is being used.
 * @m: total number of words in 'data'
 */
/* 位图结构：data 指向动态数组，m 为字数，n 为其中已写满的字数（加速查空）。 */
typedef struct {word_t *data; size_t n, m; } bitfield_t;

/* 声明 BITFIELD 内存类型，用于内存分配与泄漏跟踪。 */
DECLARE_MTYPE(BITFIELD);

/* 初始化位图：清零计数并分配可容纳 N 个位的字数。N 为初始可分配的 ID 数。 */
/**
 * Initialize the bits.
 * @v: an instance of bitfield_t struct.
 * @N: number of bits to start with, which equates to how many
 *     IDs can be allocated.
 */
#define bf_init(v, N)                                                          \
	do {                                                                   \
		(v).n = 0;                                                     \
		(v).m = ((N) / WORD_SIZE + 1);                                 \
		(v).data = (word_t *)XCALLOC(MTYPE_BITFIELD,                   \
					     ((v).m * sizeof(word_t)));        \
	} while (0)

/* 从位图 v 中找到一个空位，并把其编号写入 id（先找空位再置位）。 */
/**
 * allocate and assign an id from bitfield v.
 */
#define bf_assign_index(v, id)                                                 \
	do {                                                                   \
		bf_find_bit(v, id);                                            \
		bf_set_bit(v, id);                                             \
	} while (0)

/* 分配并占用第 0 号位。 */
/*
 * allocate and assign 0th bit in the bitfiled.
 */
#define bf_assign_zero_index(v)                                                \
	do {                                                                   \
		int id = 0;                                                    \
		bf_assign_index(v, id);                                        \
	} while (0)

/* 释放编号 id 对应的位（清 0），归还给位图 v。 */
/*
 * return an id to bitfield v
 */
#define bf_release_index(v, id)                                                \
	(v).data[bf_index(id)] &= ~(1U << (bf_offset(id)))

/* 查询编号 id 是否已被占用（对应位为 1）；返回非 0 表示已占用。 */
/* check if an id is in use */
#define bf_test_index(v, id)                                                \
	((v).data[bf_index(id)] & (1U << (bf_offset(id))))

/* 判断位图是否已初始化（以 data 指针非空为准）。 */
/* check if the bit field has been setup */
#define bf_is_inited(v) ((v).data)

/* 比较两个等长位图：逐字节比较，返回 0 表示完全相同。 */
/* compare two bitmaps of the same length */
#define bf_cmp(v1, v2) (memcmp((v1).data, (v2).data, ((v1).m * sizeof(word_t))))

/* 释放第 0 号位，归还给位图。 */
/*
 * return 0th index back to bitfield
 */
#define bf_release_zero_index(v) bf_release_index(v, 0)

/* 由位号 b 算出其所在字的下标与字内偏移。 */
#define bf_index(b) ((b) / WORD_SIZE)
#define bf_offset(b) ((b) % WORD_SIZE)

/* 置位：把第 b 位置 1；若该字因此被填满则更新计数，并在已无空闲字时自动
 * 再扩展一个字，保证位图可容纳后续分配。 */
/**
 * Set a bit in the array. If it fills up that word and we are
 * out of words, extend it by one more word.
 */
#define bf_set_bit(v, b)                                                       \
	do {                                                                   \
		size_t w = bf_index(b);                                        \
		(v).data[w] |= 1U << (bf_offset(b));	\
		(v).n += ((v).data[w] == WORD_MAX);                            \
		if ((v).n == (v).m) {                                          \
			(v).m = (v).m + 1;                                     \
			(v).data = XREALLOC(MTYPE_BITFIELD, (v).data,          \
					    (v).m * sizeof(word_t));           \
			(v).data[(v).m - 1] = 0;                               \
		}                                                              \
	} while (0)

/* 在 v 中查找一个空位并写入 b：先定位第一个非全 1 的字，再在该字内用二分
 * （每次取半掩码）确定最低的 0 位，最后加上字偏移得到全局位号。 */
/* Find a clear bit in v and assign it to b. */
#define bf_find_bit(v, b)                                                      \
	do {                                                                   \
		word_t word = 0;                                               \
		unsigned int w, sh;                                            \
		for (w = 0; w <= (v).n; w++) {                                 \
			if ((word = (v).data[w]) != WORD_MAX)                  \
				break;                                         \
		}                                                              \
		(b) = ((word & 0xFFFF) == 0xFFFF) << 4;                        \
		word >>= (b);                                                  \
		sh = ((word & 0xFF) == 0xFF) << 3;                             \
		word >>= sh;                                                   \
		(b) |= sh;                                                     \
		sh = ((word & 0xF) == 0xF) << 2;                               \
		word >>= sh;                                                   \
		(b) |= sh;                                                     \
		sh = ((word & 0x3) == 0x3) << 1;                               \
		word >>= sh;                                                   \
		(b) |= sh;                                                     \
		sh = ((word & 0x1) == 0x1) << 0;                               \
		word >>= sh;                                                   \
		(b) |= sh;                                                     \
		(b) += (w * WORD_SIZE);                                        \
	} while (0)

/* 查找一个空位并返回其编号。从包含 start_index 的字开始扫描，必要时在
 * max_index 之后回绕；若位图尚可扩展则直接扩一字返回下一个字首位。
 * 找不到时返回 WORD_MAX。 */
/*
 * Find a clear bit in v and return it
 * Start looking in the word containing bit position start_index.
 * If necessary, wrap around after bit position max_index.
 */
static inline unsigned int
bf_find_next_clear_bit_wrap(bitfield_t *v, word_t start_index, word_t max_index)
{
	int start_bit;
	unsigned long i, offset, scanbits, wordcount_max, index_max;

	if (start_index > max_index)
		start_index = 0;

	start_bit = start_index & (WORD_SIZE - 1);
	wordcount_max = bf_index(max_index) + 1;

	scanbits = WORD_SIZE;
	for (i = bf_index(start_index); i < v->m; ++i) {
		if (v->data[i] == WORD_MAX) {
			/* if the whole word is full move to the next */
			start_bit = 0;
			continue;
		}
		/* scan one word for clear bits */
		if ((i == v->m - 1) && (v->m >= wordcount_max))
			/* max index could be only part of word */
			scanbits = (max_index % WORD_SIZE) + 1;
		for (offset = start_bit; offset < scanbits; ++offset) {
			if (!((v->data[i] >> offset) & 1))
				return ((i * WORD_SIZE) + offset);
		}
		/* move to the next word */
		start_bit = 0;
	}

	if (v->m < wordcount_max) {
		/*
		 * We can expand bitfield, so no need to wrap.
		 * Return the index of the first bit of the next word.
		 * Assumption is that caller will call bf_set_bit which
		 * will allocate additional space.
		 */
		v->m += 1;
		v->data = (word_t *)XREALLOC(MTYPE_BITFIELD, v->data,
					     v->m * sizeof(word_t));
		v->data[v->m - 1] = 0;
		return v->m * WORD_SIZE;
	}

	/*
	 * start looking for a clear bit at the start of the bitfield and
	 * stop when we reach start_index
	 */
	scanbits = WORD_SIZE;
	index_max = bf_index(start_index - 1);
	for (i = 0; i <= index_max; ++i) {
		if (i == index_max)
			scanbits = ((start_index - 1) % WORD_SIZE) + 1;
		for (offset = start_bit; offset < scanbits; ++offset) {
			if (!((v->data[i] >> offset) & 1))
				return ((i * WORD_SIZE) + offset);
		}
		/* move to the next word */
		start_bit = 0;
	}

	return WORD_MAX;
}

/* 从 start_index 起查找下一个已置位（值为 1）的位并返回其编号；跳过全
 * 空的字，找不到时返回 WORD_MAX。 */
static inline unsigned int bf_find_next_set_bit(bitfield_t v,
		word_t start_index)
{
	int start_bit;
	unsigned long i, offset;

	start_bit = start_index & (WORD_SIZE - 1);

	for (i = bf_index(start_index); i < v.m; ++i) {
		if (v.data[i] == 0) {
			/* if the whole word is empty move to the next */
			start_bit = 0;
			continue;
		}
		/* scan one word for set bits */
		for (offset = start_bit; offset < WORD_SIZE; ++offset) {
			if ((v.data[i] >> offset) & 1)
				return ((i * WORD_SIZE) + offset);
		}
		/* move to the next word */
		start_bit = 0;
	}
	return WORD_MAX;
}

/* 遍历位图中所有已置位的位：b 依次取每个置位编号，直到达到 max。 */
/* iterate through all the set bits */
#define bf_for_each_set_bit(v, b, max)                 \
	for ((b) = bf_find_next_set_bit((v), 0);           \
			(b) < max;                                 \
			(b) = bf_find_next_set_bit((v), (b) + 1))

/* 释放位图数据内存并把 data 置空。 */
/*
 * Free the allocated memory for data
 * @v: an instance of bitfield_t struct.
 */
#define bf_free(v)                                                             \
	do {                                                                   \
		XFREE(MTYPE_BITFIELD, (v).data);                               \
		(v).data = NULL;                                               \
	} while (0)

/* 拷贝一份位图：按源的字数分配新数组并复制内容与计数，返回新位图。 */
static inline bitfield_t bf_copy(bitfield_t src)
{
	bitfield_t dst;

	assert(bf_is_inited(src));
	bf_init(dst, WORD_SIZE * (src.m - 1));
	memcpy(dst.data, src.data, src.m * sizeof(word_t));
	dst.n = src.n;
	return dst;
}


#ifdef __cplusplus
}
#endif

#endif
