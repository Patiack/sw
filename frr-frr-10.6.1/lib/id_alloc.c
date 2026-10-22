// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR ID Number Allocator
 * Copyright (C) 2018  Amazon.com, Inc. or its affiliates
 */

/*
 * ============================================================================
 * id_alloc.c —— 小整数 ID 分配器（位图 + 分层目录）的实现
 * ============================================================================
 *
 * 实现 id_alloc.h 声明的 ID 分配器：把 32 位 ID 空间按 目录 / 子目录 / 页 /
 * 字 / 位 分层切分，叶子页用 1024 位的位图描述分配状态。目录层按需惰性创建，
 * 因此分配少量 ID 时不会占用巨量内存。
 *
 * 关键设计：
 *   - 每页维护 allocated_mask[32] 与 full_word_mask，快速定位空位
 *   - 分配器维护 has_free 单向链表，把“还有空位的页”串起来，使分配接近 O(1)
 *   - 用 FFS32（find first set）找回第一个 0 位来定位可用 ID
 *   - 额外提供“回收池”（id_alloc_pool）以批量延迟回收
 *
 * 关键函数：
 *   find_or_create_page  定位（可创建）某 ID 所属页
 *   reserve_bit          在页内标记某位已占用，并维护 full_word_mask / has_free
 *   idalloc_allocate     分配任意可用 ID
 *   idalloc_reserve      预留指定 ID
 *   idalloc_free / drain_pool / allocate_prefer_pool  回收与复用
 *
 * 与其它模块关系：依赖 memory（MTYPE）、log / lib_errors（错误上报）。
 *
 * 阅读顺序：宏 -> find_or_create_page -> reserve_bit -> idalloc_allocate ->
 *           idalloc_free -> idalloc_new / destroy -> 回收池相关函数。
 * ============================================================================
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "id_alloc.h"

#include "log.h"
#include "lib_errors.h"
#include "memory.h"

#include <inttypes.h>

/*
 * 本文件使用的内存类型：分配器根、分配器名称、目录、子目录、页、回收池节点。
 */
DEFINE_MTYPE_STATIC(LIB, IDALLOC_ALLOCATOR, "ID Number Allocator");
DEFINE_MTYPE_STATIC(LIB, IDALLOC_ALLOCATOR_NAME, "ID Number Allocator Name");
DEFINE_MTYPE_STATIC(LIB, IDALLOC_DIRECTORY, "ID Number Allocator Directory");
DEFINE_MTYPE_STATIC(LIB, IDALLOC_SUBDIRECTORY,
		    "ID Number Allocator Subdirectory");
DEFINE_MTYPE_STATIC(LIB, IDALLOC_PAGE, "ID Number Allocator Page");
DEFINE_MTYPE_STATIC(LIB, IDALLOC_POOL,
		    "ID Number temporary holding pool entry");

/*
 * FFS32 - “查找第一个被置位的位”的封装：32 位平台上用 ffs，否则用 ffsl。
 * 用于在掩码中定位第一个空闲（为 0）的位。
 */
#if UINT_MAX >= UINT32_MAX
#define FFS32(x) ffs(x)
#else
/* ints less than 32 bits? Yikes. */
#define FFS32(x) ffsl(x)
#endif

/*
 * 各层位段（dir / subdir / page / word / offset）的取位掩码。
 */
#define DIR_MASK    ((1<<IDALLOC_DIR_BITS)-1)
#define SUBDIR_MASK ((1<<IDALLOC_SUBDIR_BITS)-1)
#define FRR_ID_PAGE_MASK ((1<<IDALLOC_PAGE_BITS)-1)
#define WORD_MASK   ((1<<IDALLOC_WORD_BITS)-1)
#define OFFSET_MASK ((1<<IDALLOC_OFFSET_BITS)-1)

/*
 * 各层位段在 32 位 ID 中的右移位数（由低位段位宽累加得到）。
 */
#define DIR_SHIFT    (IDALLOC_OFFSET_BITS + IDALLOC_WORD_BITS + \
		      IDALLOC_PAGE_BITS + IDALLOC_SUBDIR_BITS)
#define SUBDIR_SHIFT (IDALLOC_OFFSET_BITS + IDALLOC_WORD_BITS + \
		      IDALLOC_PAGE_BITS)
#define FRR_ID_PAGE_SHIFT (IDALLOC_OFFSET_BITS + IDALLOC_WORD_BITS)
#define WORD_SHIFT   (IDALLOC_OFFSET_BITS)
#define OFFSET_SHIFT (0)

/*
 * ID_DIR / ID_SUBDIR / ID_PAGE / ID_WORD / ID_OFFSET -
 * 从一个 ID 中提取各层索引。
 */
#define ID_DIR(id)    ((id >> DIR_SHIFT)    & DIR_MASK)
#define ID_SUBDIR(id) ((id >> SUBDIR_SHIFT) & SUBDIR_MASK)
#define ID_PAGE(id)   ((id >> FRR_ID_PAGE_SHIFT) & FRR_ID_PAGE_MASK)
#define ID_WORD(id)   ((id >> WORD_SHIFT)   & WORD_MASK)
#define ID_OFFSET(id) ((id >> OFFSET_SHIFT) & OFFSET_MASK)

/*
 * Find the page that an ID number belongs to in an allocator.
 * Optionally create the page if it doesn't exist.
 */
/*
 * find_or_create_page - 在分配器中定位某 ID 所属的页
 * @alloc  分配器
 * @id     目标 ID
 * @create 为真时按需创建缺失的目录 / 子目录 / 页；为假时缺失即返回 NULL
 * 返回：找到（或创建）的页；未创建且不存在则 NULL
 * 副作用：create 时惰性分配 dir / subdir / page，新页会更新 capacity 并挂入
 *         has_free 链表头；重复创建已存在的页会记一条一致性错误。
 */
static struct id_alloc_page *find_or_create_page(struct id_alloc *alloc,
						 uint32_t id, int create)
{
	struct id_alloc_dir *dir = NULL;
	struct id_alloc_subdir *subdir = NULL;
	struct id_alloc_page *page = NULL;

	dir = alloc->sublevels[ID_DIR(id)];
	if (dir == NULL) {
		if (create) {
			dir = XCALLOC(MTYPE_IDALLOC_DIRECTORY, sizeof(*dir));
			alloc->sublevels[ID_DIR(id)] = dir;
		} else {
			return NULL;
		}
	}

	subdir = dir->sublevels[ID_SUBDIR(id)];
	if (subdir == NULL) {
		if (create) {
			subdir = XCALLOC(MTYPE_IDALLOC_SUBDIRECTORY,
					 sizeof(*subdir));
			dir->sublevels[ID_SUBDIR(id)] = subdir;
		} else {
			return NULL;
		}
	}

	page = subdir->sublevels[ID_PAGE(id)];
	if (page == NULL && create) {
		page = XCALLOC(MTYPE_IDALLOC_PAGE, sizeof(*page));
		page->base_value = id;
		subdir->sublevels[ID_PAGE(id)] = page;

		alloc->capacity += 1 << FRR_ID_PAGE_SHIFT;
		page->next_has_free = alloc->has_free;
		alloc->has_free = page;
	} else if (page != NULL && create) {
		flog_err(
			EC_LIB_ID_CONSISTENCY,
			"ID Allocator %s attempt to re-create page at %u",
			alloc->name, id);
	}

	return page;
}

/*
 * Return an ID number back to the allocator.
 * While this ID can be re-assigned through idalloc_allocate, the underlying
 * memory will not be freed. If this is the first free ID in the page, the page
 * will be added to the allocator's list of pages with free IDs.
 */
/*
 * idalloc_free - 把一个 ID 归还给分配器
 * @alloc 分配器
 * @id    待释放的 ID
 * 副作用：清除位图中对应位、allocated 减一；若是该 32 位字中最先被释放的位，
 *         清 full_word_mask 对应位；若整页首次出现空位，则把页挂回 has_free。
 * 注意：仅释放位，不释放底层页内存（页可被复用）。
 * 坑：页不存在或该 ID 未被分配时记一致性错误并返回。
 */
void idalloc_free(struct id_alloc *alloc, uint32_t id)
{
	struct id_alloc_page *page = NULL;

	int word, offset;
	uint32_t old_word, old_word_mask;

	page = find_or_create_page(alloc, id, 0);
	if (!page) {
		flog_err(EC_LIB_ID_CONSISTENCY,
			"ID Allocator %s cannot free #%u. ID Block does not exist.",
			alloc->name, id);
		return;
	}

	word = ID_WORD(id);
	offset = ID_OFFSET(id);

	if ((page->allocated_mask[word] & (1 << offset)) == 0) {
		flog_err(EC_LIB_ID_CONSISTENCY,
			"ID Allocator %s cannot free #%u. ID was not allocated at the time of free.",
			alloc->name, id);
		return;
	}

	old_word = page->allocated_mask[word];
	page->allocated_mask[word] &= ~(((uint32_t)1) << offset);
	alloc->allocated -= 1;

	if (old_word == UINT32_MAX) {
		/* first bit in this block of 32 to be freed.*/

		old_word_mask = page->full_word_mask;
		page->full_word_mask &= ~(((uint32_t)1) << word);

		if (old_word_mask == UINT32_MAX) {
			/* first bit in page freed, add this to the allocator's
			 * list of pages with free space
			 */
			page->next_has_free = alloc->has_free;
			alloc->has_free = page;
		}
	}
}

/*
 * Add a allocation page to the end of the allocator's current range.
 * Returns null if the allocator has had all possible pages allocated already.
 */
/*
 * create_next_page - 在分配器当前容量末尾追加一页
 * @alloc 分配器
 * 返回：新建的页；已无更多可用页（容量回绕且首目录已占）时返回 NULL
 * 说明：新页 base_value 取当前 capacity，从而按顺序扩展 ID 空间。
 */
static struct id_alloc_page *create_next_page(struct id_alloc *alloc)
{
	if (alloc->capacity == 0 && alloc->sublevels[0])
		return NULL; /* All IDs allocated and the capacity looped. */

	return find_or_create_page(alloc, alloc->capacity, 1);
}

/*
 * Marks an ID within an allocator page as in use.
 * If the ID was the last free ID in the page, the page is removed from the
 * allocator's list of free IDs. In the typical allocation case, this page is
 * the first page in the list, and removing the page is fast. If instead an ID
 * is being reserved by number, this may end up scanning the whole single linked
 * list of pages in order to remove it.
 */
/*
 * reserve_bit - 在页内把某位标记为已占用
 * @alloc  分配器
 * @page   目标页
 * @word   字下标
 * @offset 位下标
 * 副作用：置位、allocated 加一；若该字变满则更新 full_word_mask，整页变满时
 *         把页从 has_free 链表中摘除。
 * 坑：若页不在链表头，需要线性扫描链表才能摘除（预留指定 ID 时可能变慢）。
 */
static void reserve_bit(struct id_alloc *alloc, struct id_alloc_page *page,
			int word, int offset)
{
	struct id_alloc_page *itr;

	page->allocated_mask[word] |= ((uint32_t)1) << offset;
	alloc->allocated += 1;

	if (page->allocated_mask[word] == UINT32_MAX) {
		page->full_word_mask |= ((uint32_t)1) << word;
		if (page->full_word_mask == UINT32_MAX) {
			if (alloc->has_free == page) {
				/* allocate always pulls from alloc->has_free */
				alloc->has_free = page->next_has_free;
			} else {
				/* reserve could pull from any page with free
				 * bits
				 */
				itr = alloc->has_free;
				while (itr) {
					if (itr->next_has_free == page) {
						itr->next_has_free =
							page->next_has_free;
						return;
					}

					itr = itr->next_has_free;
				}
			}
		}
	}
}

/*
 * Reserve an ID number from the allocator. Returns IDALLOC_INVALID (0) if the
 * allocator has no more IDs available.
 */
/*
 * idalloc_allocate - 分配一个可用 ID
 * @alloc 分配器
 * 返回：分配到的 ID；无可用 ID 时返回 IDALLOC_INVALID 并记错误日志
 * 流程：has_free 为空则尝试扩展一页；用 FFS32(~full_word_mask) 找第一个未满
 *       的字，再用 FFS32(~allocated_mask[word]) 找该字内第一个空闲位。
 * 坑：若字 / 位索引越界说明内部状态不一致，会记一致性错误并返回无效值。
 */
uint32_t idalloc_allocate(struct id_alloc *alloc)
{
	struct id_alloc_page *page;
	int word, offset;
	uint32_t return_value;

	if (alloc->has_free == NULL)
		create_next_page(alloc);

	if (alloc->has_free == NULL) {
		flog_err(EC_LIB_ID_EXHAUST,
			"ID Allocator %s has run out of IDs.", alloc->name);
		return IDALLOC_INVALID;
	}

	page = alloc->has_free;
	word = FFS32(~(page->full_word_mask)) - 1;

	if (word < 0 || word >= 32) {
		flog_err(EC_LIB_ID_CONSISTENCY,
			"ID Allocator %s internal error. Page starting at %d is inconsistent.",
			alloc->name, page->base_value);
		return IDALLOC_INVALID;
	}

	offset = FFS32(~(page->allocated_mask[word])) - 1;
	if (offset < 0 || offset >= 32) {
		flog_err(EC_LIB_ID_CONSISTENCY,
			"ID Allocator %s internal error. Page starting at %d is inconsistent on word %d",
			alloc->name, page->base_value, word);
		return IDALLOC_INVALID;
	}
	return_value = page->base_value + word * 32 + offset;

	reserve_bit(alloc, page, word, offset);

	return return_value;
}

/*
 * Tries to allocate a specific ID from the allocator. Returns IDALLOC_INVALID
 * when the ID being "reserved" has allready been assigned/reserved. This should
 * only be done with low numbered IDs, as the allocator needs to reserve bit-map
 * pages in order
 */
/*
 * idalloc_reserve - 预留一个指定的 ID
 * @alloc 分配器
 * @id    要预留的 ID
 * 返回：成功返回该 ID；该 ID 已被占用则返回 IDALLOC_INVALID 并记错误
 * 说明：会先扩展容量直到能覆盖 id（分配器需要按顺序建立页）；规则上应只用于
 *       低编号 ID。
 */
uint32_t idalloc_reserve(struct id_alloc *alloc, uint32_t id)
{
	struct id_alloc_page *page;
	int word, offset;

	while (alloc->capacity <= id)
		create_next_page(alloc);

	word = ID_WORD(id);
	offset = ID_OFFSET(id);
	page = find_or_create_page(alloc, id, 0);
	/* page can't be null because the loop above ensured it was created. */

	if (page->allocated_mask[word] & (((uint32_t)1) << offset)) {
		flog_err(EC_LIB_ID_CONSISTENCY,
			"ID Allocator %s could not reserve %u because it is already allocated.",
			alloc->name, id);
		return IDALLOC_INVALID;
	}

	reserve_bit(alloc, page, word, offset);
	return id;
}

/*
 * Set up an empty ID allocator, with IDALLOC_INVALID pre-reserved.
 */
/*
 * idalloc_new - 创建一个空的 ID 分配器
 * @name 分配器名称（会拷贝一份）
 * 返回：新分配器
 * 副作用：分配根结构与名称；预先保留 IDALLOC_INVALID(0)，使其永不被分配。
 */
struct id_alloc *idalloc_new(const char *name)
{
	struct id_alloc *ret;

	ret = XCALLOC(MTYPE_IDALLOC_ALLOCATOR, sizeof(*ret));
	ret->name = XSTRDUP(MTYPE_IDALLOC_ALLOCATOR_NAME, name);

	idalloc_reserve(ret, IDALLOC_INVALID);

	return ret;
}

/*
 * Free a subdir, and all pages below it.
 */
/*
 * idalloc_destroy_subdir - 释放一个子目录及其下所有页
 * @subdir 目标子目录
 * 说明：页是顺序创建的，遇到第一个空槽即可停止（break）；最后释放子目录本身。
 */
static void idalloc_destroy_subdir(struct id_alloc_subdir *subdir)
{
	int i;

	for (i = 0; i < IDALLOC_PAGE_COUNT; i++) {
		if (subdir->sublevels[i])
			XFREE(MTYPE_IDALLOC_PAGE, subdir->sublevels[i]);
		else
			break;
	}
	XFREE(MTYPE_IDALLOC_SUBDIRECTORY, subdir);
}

/*
 * Free a dir, and all subdirs/pages below it.
 */
/*
 * idalloc_destroy_dir - 释放一个目录及其下所有子目录 / 页
 * @dir 目标目录
 * 说明：同样利用顺序创建的假设，遇到空槽 break；最后释放目录本身。
 */
static void idalloc_destroy_dir(struct id_alloc_dir *dir)
{
	int i;

	for (i = 0; i < IDALLOC_SUBDIR_COUNT; i++) {
		if (dir->sublevels[i])
			idalloc_destroy_subdir(dir->sublevels[i]);
		else
			break;
	}
	XFREE(MTYPE_IDALLOC_DIRECTORY, dir);
}

/*
 * Free all memory associated with an ID allocator.
 */
/*
 * idalloc_destroy - 释放分配器及其全部内存
 * @alloc 目标分配器
 * 副作用：逐目录递归释放，再释放名称与分配器根结构。
 */
void idalloc_destroy(struct id_alloc *alloc)
{
	int i;

	for (i = 0; i < IDALLOC_DIR_COUNT; i++) {
		if (alloc->sublevels[i])
			idalloc_destroy_dir(alloc->sublevels[i]);
		else
			break;
	}

	XFREE(MTYPE_IDALLOC_ALLOCATOR_NAME, alloc->name);
	XFREE(MTYPE_IDALLOC_ALLOCATOR, alloc);
}

/*
 * Give an ID number to temporary holding pool.
 */
/*
 * idalloc_free_to_pool - 把一个 ID 暂存到回收池（延迟归还）
 * @pool_ptr 回收池头指针的地址
 * @id       待暂存的 ID
 * 副作用：新节点头插入池；此时并不真正归还给主分配器。
 */
void idalloc_free_to_pool(struct id_alloc_pool **pool_ptr, uint32_t id)
{
	struct id_alloc_pool *new_pool;

	new_pool = XMALLOC(MTYPE_IDALLOC_POOL, sizeof(*new_pool));
	new_pool->id = id;
	new_pool->next = *pool_ptr;
	*pool_ptr = new_pool;
}

/*
 * Free all ID numbers held in a holding pool back to the main allocator.
 */
/*
 * idalloc_drain_pool - 把回收池中的 ID 全部归还给分配器并清空池
 * @alloc    分配器（可为 NULL，为 NULL 时只释放池节点、不归还 ID）
 * @pool_ptr 回收池头指针的地址
 * 副作用：遍历池，逐个 idalloc_free，并释放池节点，最后池置空。
 */
void idalloc_drain_pool(struct id_alloc *alloc, struct id_alloc_pool **pool_ptr)
{
	struct id_alloc_pool *current, *next;

	while (*pool_ptr) {
		current = *pool_ptr;
		next = current->next;

		if (alloc)
			idalloc_free(alloc, current->id);

		XFREE(MTYPE_IDALLOC_POOL, current);
		*pool_ptr = next;
	}
}

/*
 * Allocate an ID from either a holding pool, or the main allocator. IDs will
 * only be pulled form the main allocator when the pool is empty.
 */
/*
 * idalloc_allocate_prefer_pool - 优先从回收池取 ID，否则向主分配器申请
 * @alloc    分配器
 * @pool_ptr 回收池头指针的地址
 * 返回：池非空则弹出池头节点并返回其 ID；否则调用 idalloc_allocate。
 * 说明：用于“回收的 ID 尽快被复用”的场景。
 */
uint32_t idalloc_allocate_prefer_pool(struct id_alloc *alloc,
				      struct id_alloc_pool **pool_ptr)
{
	uint32_t ret;
	struct id_alloc_pool *pool_head = *pool_ptr;

	if (pool_head) {
		ret = pool_head->id;
		*pool_ptr = pool_head->next;
		XFREE(MTYPE_IDALLOC_POOL, pool_head);
		return ret;
	} else {
		return idalloc_allocate(alloc);
	}
}
