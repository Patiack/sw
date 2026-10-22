// SPDX-License-Identifier: GPL-2.0-or-later
/* Generic vector interface routine
 * Copyright (C) 1997 Kunihiro Ishiguro
 */

/*
 * 【文件导读】vector.c —— vector 动态指针数组的实现
 *
 * 一、这个文件是干什么的
 *   实现 vector.h 声明的“可增长指针数组”。内部是一个 void 指针数组，支持
 *   按下标读写、查找空位、删除、压紧、释放，以及与普通 C 数组互转。
 *
 * 二、核心数据结构
 *   见 vector.h 的 struct _vector：active（含空洞上界）/ count（真实个数）
 *   / alloced（容量）/ dynamic / index。本文件所有函数都在维护这几个不变式：
 *     active >= count，且 alloced >= active >= count。
 *
 * 三、与其它模块的关系
 *   依赖 memory.h 的内存宏（XCALLOC / XMALLOC / XREALLOC / XFREE）和 MTYPE
 *   内存类型统计；被 vty、lib 其它容器用户广为使用。功能与 darr 重叠，但接口
 *   更老、语义更松散。
 *
 * 四、阅读顺序
 *   vector_init -> vector_ensure（扩容核心）-> vector_empty_slot -> vector_set
 *   / vector_set_index -> lookup 系列 -> unset / remove / compact / unset_value
 *   -> to_array / array_to_vector -> free。
 *
 * 易踩的坑：vector_active 含空洞，遍历时要用 vector_slot 判空；
 *   vector_empty_slot 只给下标，真正写入前必须 vector_ensure。
 */

#include <zebra.h>
#include <string.h>

#include "vector.h"
#include "memory.h"

/*
 * 两个内存类型标签：MTYPE_VECTOR 统计 _vector 结构体本身，
 * MTYPE_VECTOR_INDEX 统计 index 指针数组。供内存统计/泄漏排查使用。
 */
DEFINE_MTYPE_STATIC(LIB, VECTOR, "Vector");
DEFINE_MTYPE_STATIC(LIB, VECTOR_INDEX, "Vector index");

/*
 * vector_init —— 新建一个 vector 并返回。
 * 参数 size：期望初始容量；为 0 时按最小尺寸 1 分配。
 * 副作用：分配 _vector 结构体与 index 数组两块内存，dynamic 置真。
 * 返回：新 vector 指针，不会失败（分配失败由内存宏直接终止）。
 */
/* Initialize vector : allocate memory and return vector. */
vector vector_init(unsigned int size)
{
	vector v = XCALLOC(MTYPE_VECTOR, sizeof(struct _vector));

	/* allocate at least one slot */
	if (size == 0)
		size = 1;

	v->alloced = size;
	v->active = 0;
	v->count = 0;
	v->dynamic = true;
	v->index = XCALLOC(MTYPE_VECTOR_INDEX, sizeof(void *) * size);
	return v;
}

/*
 * vector_free —— 释放 vector。
 * 仅当 alloced 非 0 才释放 index（alloced 为 0 表示 index 指向静态内存，
 * 不能释放）；仅当 dynamic 为真才释放结构体自身。释放后指针不可再用。
 */
void vector_free(vector v)
{
	if (v->alloced)
		XFREE(MTYPE_VECTOR_INDEX, v->index);
	if (v->dynamic)
		XFREE(MTYPE_VECTOR, v);
}

/* resize vector to a minimum of num
 * may resize larger to avoid excessive realloc overhead
 */
/*
 * vector_ensure —— 确保 vector 容量至少能容纳下标 num。扩容核心函数。
 * 参数 num：需要确保可用的最大下标（函数保证 alloced > num）。
 * 副作用：可能 realloc 或首次 malloc index；新槽位清零。
 * 坑：若原 index 是静态全局数组（alloced 为 0），会先复制其 active 个元素
 *     到新分配空间；扩容尺寸取 max(active*2, num+1) 以减少频繁 realloc。
 */
void vector_ensure(vector v, unsigned int num)
{
	unsigned int newsz;

	if (v->alloced > num)
		return;

	newsz = MAX(v->active * 2, num + 1);

	if (!v->alloced && v->index) {
		/* currently using global variable, not malloc'd memory */
		void **orig_index = v->index;

		v->index = XMALLOC(MTYPE_VECTOR_INDEX, sizeof(void *) * newsz);
		memcpy(v->index, orig_index, v->active * sizeof(void *));
		v->alloced = v->active;
	} else
		v->index = XREALLOC(MTYPE_VECTOR_INDEX, v->index,
				    sizeof(void *) * newsz);

	memset(&v->index[v->alloced], 0, sizeof(void *) * (newsz - v->alloced));
	v->alloced = newsz;
}

/*
 * vector_empty_slot —— 返回下一个可用空槽的下标。
 * 返回：active == count 时直接返回 active（末尾追加）；否则线性扫描找首个
 *       为 NULL 的槽位；全满时返回 active。
 * 坑：只给出“下标”，并不保证该下标内存已分配，写入前必须 vector_ensure。
 */
/* This function only returns next empty slot index.  It dose not mean
   the slot's index memory is assigned, please call vector_ensure()
   after calling this function. */
int vector_empty_slot(vector v)
{
	unsigned int i;

	if (v->active == v->count)
		return v->active;

	if (v->active == 0)
		return 0;

	for (i = 0; i < v->active; i++)
		if (v->index[i] == 0)
			return i;

	return i;
}

/*
 * vector_set —— 把 val 写入最小的空槽。
 * 参数 val：待存指针；为 NULL 时按“清空该槽”处理（count 可能减少）。
 * 返回：写入所用的槽下标。
 * 副作用：可能扩容；按旧值/新值是否为空调整 count；必要时上调 active。
 */
/* Set value to the smallest empty slot. */
int vector_set(vector v, void *val)
{
	unsigned int i;

	i = vector_empty_slot(v);
	vector_ensure(v, i);

	if (v->index[i])
		v->count--;
	if (val)
		v->count++;
	v->index[i] = val;

	if (v->active <= i)
		v->active = i + 1;

	return i;
}

/*
 * vector_set_index —— 把 val 写入指定下标 i 处。
 * 参数 i：目标下标，越界会自动扩容到 i。
 * 返回：i 本身。
 * 副作用：可能扩容；按旧值/新值是否为空调整 count 与 active。
 * 与 vector_set 的区别：不找空位，强制写 i。
 */
/* Set value to specified index slot. */
int vector_set_index(vector v, unsigned int i, void *val)
{
	vector_ensure(v, i);

	if (v->index[i])
		v->count--;
	if (val)
		v->count++;
	v->index[i] = val;

	if (v->active <= i)
		v->active = i + 1;

	return i;
}

/*
 * vector_lookup —— 按下标 i 取值。
 * 返回：越界（i >= active）返回 NULL，否则返回 index[i]（可能仍为 NULL）。
 * 无副作用，不扩容。
 */
/* Look up vector.  */
void *vector_lookup(vector v, unsigned int i)
{
	if (i >= v->active)
		return NULL;
	return v->index[i];
}

/*
 * vector_lookup_ensure —— 先确保容量可容纳 i，再返回 index[i]。
 * 副作用：可能扩容并把新槽位清零，因此返回值为 NULL 而非越界。
 * 适合“读不到就准备写入”的场景。
 */
/* Lookup vector, ensure it. */
void *vector_lookup_ensure(vector v, unsigned int i)
{
	vector_ensure(v, i);
	return v->index[i];
}

/*
 * vector_unset —— 把下标 i 处的槽位清为 NULL（不回收下标）。
 * 副作用：若该槽原非空则 count--；若 i 恰是最后一个有效槽，则回退 active，
 *         并继续向前回退连续的空尾槽。
 * 坑：会在数组中间留下空洞，故 active 可能大于 count。
 */
/* Unset value at specified index slot. */
void vector_unset(vector v, unsigned int i)
{
	if (i >= v->active)
		return;

	if (v->index[i])
		v->count--;

	v->index[i] = NULL;

	if (i + 1 == v->active) {
		v->active--;
		while (i && v->index[--i] == NULL && v->active--)
			; /* Is this ugly ? */
	}
}

/*
 * vector_remove —— 删除下标 ix 处的元素，并把其后元素整体前移一格（压紧）。
 * 副作用：active--，count 视该槽是否非空而减一；末尾槽清空。
 * 坑：会改变被删元素之后所有元素的下标，持有旧下标的调用方需重算。
 */
void vector_remove(vector v, unsigned int ix)
{
	if (ix >= v->active)
		return;

	if (v->index[ix])
		v->count--;

	int n = (--v->active) - ix;

	memmove(&v->index[ix], &v->index[ix + 1], n * sizeof(void *));
	v->index[v->active] = NULL;
}

/*
 * vector_compact —— 移除所有空洞，使 active == count 且中间无 NULL。
 * 实现：从前向后扫描，遇到 NULL 就 vector_remove 并回退下标重扫。
 * 副作用：破坏原有下标对应关系，之后不能再用旧下标取值。
 */
void vector_compact(vector v)
{
	for (unsigned int i = 0; i < vector_active(v); ++i) {
		if (vector_slot(v, i) == NULL) {
			vector_remove(v, i);
			--i;
		}
	}
}

/*
 * vector_unset_value —— 找到第一个等于 val 的槽并清为 NULL。
 * 副作用：count--；若命中的是最后一个有效槽则回退 active（含连续空尾）。
 * 坑：只删第一个匹配项；val 为 NULL 时也会命中第一个空槽。
 */
void vector_unset_value(vector v, void *val)
{
	size_t i;

	for (i = 0; i < v->active; i++)
		if (v->index[i] == val) {
			v->index[i] = NULL;
			v->count--;
			break;
		}

	if (i + 1 == v->active)
		do
			v->active--;
		while (i && v->index[--i] == NULL);
}

/*
 * vector_to_array —— 把 vector 的 index 浅拷贝成一块独立数组。
 * 参数 dest：出参，指向新分配的数组（MTYPE_TMP，调用方负责释放）；
 *           argc：出参，元素个数设为 active（含空洞，不是 count）。
 * 坑：是浅拷贝，元素指针共享，释放数组不等于释放元素。
 */
void vector_to_array(vector v, void ***dest, int *argc)
{
	*dest = XCALLOC(MTYPE_TMP, sizeof(void *) * v->active);
	memcpy(*dest, v->index, sizeof(void *) * v->active);
	*argc = v->active;
}

/*
 * array_to_vector —— 把 C 数组 src 的 argc 个元素装进新建的 vector。
 * 返回：新 vector（最小初始容量 VECTOR_MIN_SIZE，不足会自动扩充）。
 * 副作用：逐个 vector_set_index 写入，元素为浅拷贝引用。
 */
vector array_to_vector(void **src, int argc)
{
	vector v = vector_init(VECTOR_MIN_SIZE);

	for (int i = 0; i < argc; i++)
		vector_set_index(v, i, src[i]);
	return v;
}
