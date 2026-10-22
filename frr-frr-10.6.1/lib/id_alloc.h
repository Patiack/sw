// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR ID Number Allocator
 * Copyright (C) 2018  Amazon.com, Inc. or its affiliates
 */

#ifndef _ZEBRA_ID_ALLOC_H
#define _ZEBRA_ID_ALLOC_H

#include <strings.h>
#include <limits.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * id_alloc.h —— 小整数 ID 分配器（位图 + 分层目录）
 * ============================================================================
 *
 * 本文件声明一个用于分配小整数 ID（如 VRF id、table id 等）的分配器。
 * 它把 ID 空间按 目录(dir) -> 子目录(subdir) -> 页(page) -> 字(word) -> 位(offset)
 * 的层次结构组织，用位图记录分配情况，并用惰性分配（按需创建目录 / 子目录 /
 * 页）避免一次性占用巨量内存。
 *
 * 层次与位宽（合计 32 位）：
 *   DIR_BITS=8, SUBDIR_BITS=7, PAGE_BITS=7, WORD_BITS=5, OFFSET_BITS=5
 * 一页覆盖 32 个 word（每 word 32 位），即 1024 个 ID。
 *
 * 核心类型：
 *   - struct id_alloc_page 一页位图：allocated_mask[32] + full_word_mask +
 *                          base_value + next_has_free 链表钩子
 *   - struct id_alloc_subdir / id_alloc_dir  中间层目录
 *   - struct id_alloc      分配器根：目录指针数组 + 有空闲页链表 + 名称 +
 *                          已分配数 / 容量
 *   - struct id_alloc_pool 临时“回收池”单链表节点（延迟回收 ID）
 *
 * 对外接口见文件末尾声明（实现见 id_alloc.c）。
 *
 * 阅读顺序：各 BIT 宏 -> struct id_alloc_page -> struct id_alloc -> 函数声明。
 * ============================================================================
 */

/*
 * IDALLOC_INVALID - 无效 ID，值为 0；新分配器会预先保留它，使其永不被分配。
 */
#define IDALLOC_INVALID 0

/*
 * 各位段的位宽（合计 32 位），决定 ID 空间的分层切分方式。
 */
#define IDALLOC_DIR_BITS     8
#define IDALLOC_SUBDIR_BITS  7
#define IDALLOC_PAGE_BITS    7
#define IDALLOC_WORD_BITS    5
#define IDALLOC_OFFSET_BITS  5

/*
 * 各层的条目数量（2 的位宽次幂），用于定义目录 / 子目录 / 页数组的大小。
 */
#define IDALLOC_DIR_COUNT (1 << IDALLOC_DIR_BITS)
#define IDALLOC_SUBDIR_COUNT (1 << IDALLOC_SUBDIR_BITS)
#define IDALLOC_PAGE_COUNT (1 << IDALLOC_PAGE_BITS)
#define IDALLOC_WORD_COUNT (1 << IDALLOC_WORD_BITS)

/*
 * struct id_alloc_page - 一页（1024 个 ID）的分配位图。
 * allocated_mask  32 个 32 位字，共 1024 位，1 表示对应 ID 已被分配
 * full_word_mask  每个 bit 表示 allocated_mask 中对应字是否已满
 * base_value      bit 0 所对应的 ID 基址
 * next_has_free   指向下一个仍有空闲位的页，串成分配器级别的空闲链表
 */
struct id_alloc_page {
	/* Bitmask of allocations. 1s indicates the ID is already allocated. */
	uint32_t allocated_mask[IDALLOC_WORD_COUNT];

	/* Bitmask for free space in allocated_mask. 1s indicate whole 32 bit
	 * section is full.
	 */
	uint32_t full_word_mask;

	/* The ID that bit 0 in allocated_mask corresponds to. */
	uint32_t base_value;

	struct id_alloc_page
		*next_has_free; /* Next page with at least one bit open */
};

/*
 * struct id_alloc_subdir - 子目录层：IDALLOC_PAGE_COUNT 个页指针。
 */
struct id_alloc_subdir {
	struct id_alloc_page *sublevels[IDALLOC_PAGE_COUNT];
};

/*
 * struct id_alloc_dir - 目录层：IDALLOC_SUBDIR_COUNT 个子目录指针。
 */
struct id_alloc_dir {
	struct id_alloc_subdir *sublevels[IDALLOC_SUBDIR_COUNT];
};

/*
 * struct id_alloc - 分配器根结构。
 * sublevels  目录指针数组（IDALLOC_DIR_COUNT 项，惰性创建）
 * has_free   指向第一个还有空闲位的页
 * name       分配器名称（用于日志）
 * allocated  当前已分配数量；capacity 当前已建立的容量
 */
struct id_alloc {
	struct id_alloc_dir *sublevels[IDALLOC_DIR_COUNT];

	struct id_alloc_page *has_free;

	char *name;

	uint32_t allocated, capacity;
};

/*
 * struct id_alloc_pool - 临时回收池的单链表节点，暂存待回收 / 复用的 ID。
 */
struct id_alloc_pool {
	struct id_alloc_pool *next;
	uint32_t id;
};

/*
 * idalloc_free - 把一个 ID 归还给分配器（仅释放位，不释放底层内存）。
 */
void idalloc_free(struct id_alloc *alloc, uint32_t id);
/*
 * idalloc_free_to_pool - 把 ID 暂存到回收池（延迟归还），入池为头插。
 */
void idalloc_free_to_pool(struct id_alloc_pool **pool_ptr, uint32_t id);
/*
 * idalloc_drain_pool - 把回收池中所有 ID 归还给分配器并释放池节点。
 */
void idalloc_drain_pool(struct id_alloc *alloc,
			struct id_alloc_pool **pool_ptr);
/*
 * idalloc_allocate - 分配一个可用 ID；空间耗尽返回 IDALLOC_INVALID。
 */
uint32_t idalloc_allocate(struct id_alloc *alloc);
/*
 * idalloc_allocate_prefer_pool - 优先从回收池取 ID，池空才向主分配器申请。
 */
uint32_t idalloc_allocate_prefer_pool(struct id_alloc *alloc,
				      struct id_alloc_pool **pool_ptr);
/*
 * idalloc_reserve - 预留一个指定 ID；若已被分配则返回 IDALLOC_INVALID。
 */
uint32_t idalloc_reserve(struct id_alloc *alloc, uint32_t id);
/*
 * idalloc_new - 创建一个空分配器（已预保留 IDALLOC_INVALID）。
 */
struct id_alloc *idalloc_new(const char *name);
/*
 * idalloc_destroy - 释放分配器及其所有目录 / 页内存。
 */
void idalloc_destroy(struct id_alloc *alloc);

#ifdef __cplusplus
}
#endif

#endif
