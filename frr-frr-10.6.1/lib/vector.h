// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic vector interface header.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 */

/*
 * 【文件导读】vector —— FRR 里最古老的“可增长数组”容器
 *
 * 一、这个文件是干什么的
 *   vector 是 FRR 早期就存在的一种“可增长指针数组”：内部维护一个 void 指针
 *   数组，可按下标存取任意指针。相比 darr，vector 更老、语义更松散，历史上
 *   被 vty、路由表、各种列表大量使用。多数字段名以 _vector 结构体承载。
 *
 * 二、核心数据结构
 *   struct _vector 里有四个关键字段：
 *     active  —— 最后一个非空槽位的下标加一（含空洞）；
 *     count   —— 实际非空元素个数（加一），与 active 的区别在于是否出现过
 *                “设为 NULL 但未上移后续元素”造成的空洞；
 *     alloced —— index 数组的容量；为 0 时表示 index 指向的是静态全局数组，
 *                尚未转为 malloc 分配，vector 代码会在需要修改时按需转换；
 *     dynamic —— struct _vector 本身是否动态分配。
 *   typedef vector 是 struct _vector 的指针。
 *
 * 三、与其它模块的关系
 *   与 darr 功能重叠，但 vector 允许通过 vector_active 直接暴露内部容量语义，
 *   vector_to_array / array_to_vector 用于与 C 数组互转，常配合 vty 输出。
 *
 * 四、阅读顺序
 *   先看 vector.h 的宏（vector_slot / vector_active / vector_count），再看
 *   vector.c 里的 init / set / unset / compact / free，最后看互转函数。
 */

#ifndef _ZEBRA_VECTOR_H
#define _ZEBRA_VECTOR_H

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * _vector —— vector 的底层结构体
 * 见文件导读对 active / count / alloced / dynamic / index 的说明。
 * 通常不直接操作本结构体，而用 vector_ 系列函数；读取元素用 vector_slot 宏。
 */
/* struct for vector */
struct _vector {
	/* active: index of last non-NULL item (+1)
	 * count:  number of non-NULL items (+1)
	 *
	 * the two will be different if a slot is set to NULL (without pulling
	 * up later items in the array).  Whether this happens depends on
	 * which vector functions are used.  If no empty slots are used, the
	 * two fields will be identical.
	 *
	 * alloced: size of array pointed to by index.  If this is 0, index
	 * points at a global variable rather than a malloc'd bit of memory.
	 * The vector code will convert to malloc'd memory if necessary to
	 * perform updates.
	 */
	unsigned int active;
	unsigned int alloced;
	unsigned int count;

	/* whether struct _vector itself is dynamically allocated */
	bool dynamic;

	void **index;	 /* index to data */
};
/*
 * vector —— 指向 _vector 的指针，对外统一以该别名使用。
 * 注意 NULL 也是合法的 vector 值，部分函数对 NULL 有特判。
 */
typedef struct _vector *vector;

/*
 * VECTOR_MIN_SIZE —— vector 申请时的最小容量。
 * vector_init 传 0 时也会按此值至少分配一个槽位，避免容量为 0 的边界问题。
 */
#define VECTOR_MIN_SIZE 1

/* (Sometimes) usefull macros.  This macro convert index expression to
 array expression. */
/* Reference slot at given index, caller must ensure slot is active */
/*
 * vector_slot(V,I) —— 直接取下标 I 处的元素（不检查是否越界/是否为空）。
 * 调用方必须保证该槽位是 active 的；越界访问不会报错，属未定义行为。
 */
#define vector_slot(V,I)  ((V)->index[(I)])
/* Number of active slots.
 * Note that this differs from vector_count() as it the count returned
 * will include any empty slots
 */
/*
 * vector_active(V) —— 返回 active 字段（含空洞的上界），不是真实元素个数。
 * 若想要真实元素个数，用 vector_count()。
 */
#define vector_active(V) ((V)->active)

/*
 * 以下为 vector 的公开接口原型，逐条含义见 vector.c 中同名函数的实现注释。
 * 记忆要点：init 建表；ensure 扩容；set / set_index 写入并返回槽位下标；
 * empty_slot 找第一个空位；unset / unset_value / remove 删除；compact 压紧；
 * free 释放；lookup 系列按下标取值。
 */
/* Prototypes. */
extern vector vector_init(unsigned int size);
extern void vector_ensure(vector v, unsigned int num);
extern int vector_empty_slot(vector v);
extern int vector_set(vector v, void *val);
extern int vector_set_index(vector v, unsigned int i, void *val);
extern void vector_unset(vector v, unsigned int i);
extern void vector_unset_value(vector v, void *val);
extern void vector_remove(vector v, unsigned int ix);
extern void vector_compact(vector v);

/*
 * vector_count —— 返回真实元素个数（count 字段）。
 * 与 vector_active 的区别：发生过“置 NULL 未压实”时 count 更小。
 * 内联实现，无副作用，不检查 v 是否为 NULL。
 */
static inline unsigned int vector_count(vector v)
{
	return v->count;
}

extern void vector_free(vector v);

extern void *vector_lookup(vector, unsigned int);
extern void *vector_lookup_ensure(vector, unsigned int);
extern void vector_to_array(vector v, void ***dest, int *argc);
extern vector array_to_vector(void **src, int argc);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_VECTOR_H */
