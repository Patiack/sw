// SPDX-License-Identifier: ISC
// IWYU pragma: private, include "lib/typesafe.h"
/*
 * The following Red-Black tree implementation is based off code with
 * original copyright:
 *
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
 */

/*
 * 文件导读：typerb.h —— 带类型检查的红黑树内核（头文件）
 *
 * 本文件是 FRR typesafe 框架中"红黑树 rbtree"后端的底层声明层。
 * 它只声明与具体元素类型无关的部分：
 *   1) 通用节点 / 根结构体：typed_rb_entry 与 typed_rb_root；
 *   2) 一组操作裸指针的红黑树函数原型：insert / remove / find / find_gteq /
 *      find_lt / min / max / prev / next / member（实现见 typerb.c）；
 *   3) 一组宏，供 typesafe.h 展开时生成"类型安全"的包装函数。
 *
 * 核心数据结构：
 *   - typed_rb_entry：红黑树节点，含 parent / left / right 指针与颜色位；
 *     它被内嵌进用户结构体中（作为某个 field 的成员）。
 *   - typed_rb_root：树根，含 root 指针与元素计数 count。
 *
 * 关键设计：
 *   - 所有比较都通过函数指针 cmpfn 完成，因此同一份算法可被任意类型复用。
 *   - 分 UNIQ / NONUNIQ 两种模式：UNIQ 树按 cmpfn 唯一排序，遇到等键即冲突；
 *     NONUNIQ 在键相等时再用节点地址做次级比较，从而允许重复键共存。
 *   - 本头文件的宏在展开前需要先被包含一次（见 typesafe.h 的两遍包含机制）。
 *
 * 与其他模块的关系：
 *   - typesafe.h 依据用户的 PREDECL_RBTREE 与 DECLARE_RBTREE 调用，
 *     在包含本文件后展开生成带类型的接口；
 *   - 真正的算法实现位于 typerb.c。
 *
 * 阅读顺序建议：先看 typed_rb_entry 与 typed_rb_root，再看函数原型，
 * 最后读 _PREDECL_RBTREE 与 _DECLARE_RBTREE 两个宏，体会 typesafe 的生成套路。
 */
#ifndef _FRR_TYPERB_H
#define _FRR_TYPERB_H

#ifndef _TYPESAFE_EXPAND_MACROS
#include <stdbool.h>
#include <string.h>

#include "lib/compiler.h"
#endif /* _TYPESAFE_EXPAND_MACROS */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 红黑树节点。被内嵌到用户结构体中的某个 field 里，
 * 由 typed 宏通过 container_of 反推出外层对象。
 * 字段：rbt_parent 父节点指针；rbt_left / rbt_right 左右子节点指针；
 *       rbt_color 节点颜色（0 黑 / 1 红，由 typerb.c 维护）。
 */
struct typed_rb_entry {
	struct typed_rb_entry *rbt_parent;
	struct typed_rb_entry *rbt_left;
	struct typed_rb_entry *rbt_right;
	unsigned int rbt_color;
};

/*
 * 红黑树根。rbt_root 指向树顶节点（NULL 表示空树）；
 * count 实时记录元素个数，供 _count 一类查询以 O(1) 返回。
 */
struct typed_rb_root {
	struct typed_rb_entry *rbt_root;
	size_t count;
};

/*
 * 向树中插入节点 rbe，比较用 cmpfn。
 * 返回 NULL 表示插入成功；若发现等键节点（cmpfn 返回 0），
 * 则放弃插入并返回该已存在节点（供 UNIQ 语义判断冲突）。
 * 副作用：成功时会自平衡并令 count 加一。
 */
struct typed_rb_entry *typed_rb_insert(struct typed_rb_root *rbt,
		struct typed_rb_entry *rbe,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b));
/*
 * 从树中删除节点 rbe，返回被删节点（内容已被清零）。
 * 注意：rbe 必须是树中真实存在的节点，本函数不做存在性检查。
 * 副作用：自平衡并令 count 减一。
 */
struct typed_rb_entry *typed_rb_remove(struct typed_rb_root *rbt,
				       struct typed_rb_entry *rbe);
/* 精确查找与 key 同键的节点，找到返回该节点，否则返回 NULL。 */
const struct typed_rb_entry *typed_rb_find(const struct typed_rb_root *rbt,
		const struct typed_rb_entry *rbe,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b));
/*
 * 查找键不小于 key 的最小节点（大于等于下界），即 key 的后继候选；
 * 无则返回 NULL。
 */
const struct typed_rb_entry *typed_rb_find_gteq(const struct typed_rb_root *rbt,
		const struct typed_rb_entry *rbe,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b));
/*
 * 查找键严格小于 key 的最大节点（小于上界），即 key 的前驱候选；
 * 无则返回 NULL。
 */
const struct typed_rb_entry *typed_rb_find_lt(const struct typed_rb_root *rbt,
		const struct typed_rb_entry *rbe,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b));
/* 返回树中最小（最左）节点；空树返回 NULL。 */
struct typed_rb_entry *typed_rb_min(const struct typed_rb_root *rbt);
/* 返回树中最大（最右）节点；空树返回 NULL。 */
struct typed_rb_entry *typed_rb_max(const struct typed_rb_root *rbt);
/* 返回中序遍历下 rbe 的前驱节点；无前驱返回 NULL。 */
struct typed_rb_entry *typed_rb_prev(const struct typed_rb_entry *rbe);
/* 返回中序遍历下 rbe 的后继节点；无后继返回 NULL。 */
struct typed_rb_entry *typed_rb_next(const struct typed_rb_entry *rbe);
/* 判断 rbe 是否挂在本树中（一路向上找到根再与 rbt_root 比对）。 */
bool typed_rb_member(const struct typed_rb_root *rbt,
		     const struct typed_rb_entry *rbe);

/* clang-format off */

/*
 * 前置声明宏（第一遍包含时展开）。
 * 只定义 head 与 item 两个空壳结构体，让用户类型可以先声明指针或
 * 内嵌成员；真正的函数接口在第二遍包含时由 _DECLARE_RBTREE 生成。
 * head.rr 内嵌 typed_rb_root，item.re 内嵌 typed_rb_entry。
 */
#define _PREDECL_RBTREE(prefix)                                                \
struct prefix ## _head { struct typed_rb_root rr; };                           \
struct prefix ## _item { struct typed_rb_entry re; };                          \
MACRO_REQUIRE_SEMICOLON() /* end */

/* 静态初始化器：把红黑树初始化为空（空花括号即所有字段清零）。 */
#define INIT_RBTREE_UNIQ(var)		{ }
#define INIT_RBTREE_NONUNIQ(var)	{ }

/*
 * 红黑树接口生成宏（第二遍包含时展开），由 DECLARE_RBTREE_* 调用。
 * 参数：
 *   prefix     生成的函数名前缀；
 *   type       用户元素类型；
 *   field      type 中内嵌的类型化节点成员名（其内含 .re）；
 *   cmpfn_nuq  查找 / 前驱后继所用的比较函数；
 *   cmpfn_uq   插入 / 删除所用的比较函数（UNIQ 与 NONUNIQ 在此区分）。
 * 生成 _init / _fini / _add / _del / _pop / _const_find* / _first / _next /
 * _last / _prev / _count / _member 等一组宏内联函数。
 * 说明：_add 冲突时会调用 _sa_dummy_store 吞掉返回值以消除告警。
 */
#define _DECLARE_RBTREE(prefix, type, field, cmpfn_nuq, cmpfn_uq)              \
                                                                               \
macro_inline void prefix ## _init(struct prefix##_head *h)                     \
{                                                                              \
	memset(h, 0, sizeof(*h));                                              \
}                                                                              \
macro_inline void prefix ## _fini(struct prefix##_head *h)                     \
{                                                                              \
	memset(h, 0, sizeof(*h));                                              \
}                                                                              \
macro_inline type *prefix ## _add(struct prefix##_head *h, type *item)         \
{                                                                              \
	struct typed_rb_entry *re;                                             \
	re = typed_rb_insert(&h->rr, &item->field.re, cmpfn_uq);               \
	if (!re)                                                               \
		_sa_dummy_store(item);                                         \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_inline const type *prefix ## _const_find_gteq(                           \
		const struct prefix##_head *h, const type *item)               \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_find_gteq(&h->rr, &item->field.re, cmpfn_nuq);           \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_inline const type *prefix ## _const_find_lt(                             \
		const struct prefix##_head *h, const type *item)               \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_find_lt(&h->rr, &item->field.re, cmpfn_nuq);             \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
TYPESAFE_FIND_CMP(prefix, type)                                                \
macro_inline type *prefix ## _del(struct prefix##_head *h, type *item)         \
{                                                                              \
	struct typed_rb_entry *re;                                             \
	re = typed_rb_remove(&h->rr, &item->field.re);                         \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_inline type *prefix ## _pop(struct prefix##_head *h)                     \
{                                                                              \
	struct typed_rb_entry *re;                                             \
	re = typed_rb_min(&h->rr);                                             \
	if (!re)                                                               \
		return NULL;                                                   \
	typed_rb_remove(&h->rr, re);                                           \
	return container_of(re, type, field.re);                               \
}                                                                              \
TYPESAFE_SWAP_ALL_SIMPLE(prefix)                                               \
macro_pure const type *prefix ## _const_first(const struct prefix##_head *h)   \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_min(&h->rr);                                             \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_pure const type *prefix ## _const_next(const struct prefix##_head *h,    \
					     const type *item)                 \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_next(&item->field.re);                                   \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
TYPESAFE_FIRST_NEXT(prefix, type)                                              \
macro_pure const type *prefix ## _const_last(const struct prefix##_head *h)    \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_max(&h->rr);                                             \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_pure const type *prefix ## _const_prev(const struct prefix##_head *h,    \
					     const type *item)                 \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_prev(&item->field.re);                                   \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
TYPESAFE_LAST_PREV(prefix, type)                                               \
macro_pure type *prefix ## _next_safe(struct prefix##_head *h, type *item)     \
{                                                                              \
	struct typed_rb_entry *re;                                             \
	re = item ? typed_rb_next(&item->field.re) : NULL;                     \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_pure type *prefix ## _prev_safe(struct prefix##_head *h, type *item)     \
{                                                                              \
	struct typed_rb_entry *re;                                             \
	re = item ? typed_rb_prev(&item->field.re) : NULL;                     \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
macro_pure size_t prefix ## _count(const struct prefix##_head *h)              \
{                                                                              \
	return h->rr.count;                                                    \
}                                                                              \
macro_pure bool prefix ## _member(const struct prefix##_head *h,               \
				  const type *item)                            \
{                                                                              \
	return typed_rb_member(&h->rr, &item->field.re);                       \
}                                                                              \
MACRO_REQUIRE_SEMICOLON() /* end */

/* 前置声明：唯一键红黑树。 */
#define PREDECL_RBTREE_UNIQ(prefix)                                            \
	_PREDECL_RBTREE(prefix)
/*
 * 声明唯一键红黑树的完整接口。
 * cmpfn 在键相等时视为冲突：_add 会返回已存在的等键节点而非插入新节点。
 * 内部生成 __cmp 作为统一比较器（查找与插入都用它）。
 */
#define DECLARE_RBTREE_UNIQ(prefix, type, field, cmpfn)                        \
                                                                               \
macro_inline int prefix ## __cmp(const struct typed_rb_entry *a,               \
		const struct typed_rb_entry *b)                                \
{                                                                              \
	return cmpfn(container_of(a, type, field.re),                          \
			container_of(b, type, field.re));                      \
}                                                                              \
macro_inline const type *prefix ## _const_find(const struct prefix##_head *h,  \
					       const type *item)               \
{                                                                              \
	const struct typed_rb_entry *re;                                       \
	re = typed_rb_find(&h->rr, &item->field.re, &prefix ## __cmp);         \
	return container_of_null(re, type, field.re);                          \
}                                                                              \
TYPESAFE_FIND(prefix, type)                                                    \
                                                                               \
_DECLARE_RBTREE(prefix, type, field, prefix ## __cmp, prefix ## __cmp);        \
MACRO_REQUIRE_SEMICOLON() /* end */

/* 前置声明：允许含重复键的红黑树。 */
#define PREDECL_RBTREE_NONUNIQ(prefix)                                         \
	_PREDECL_RBTREE(prefix)
/*
 * 声明可含重复键的红黑树的完整接口。
 * 生成两个比较器：
 *   __cmp    仅按键比较，用于查找 / 前驱后继（可能命中任一重复键元素）；
 *   __cmp_uq 键相等时再按节点地址比较，保证插入 / 删除所需的全序唯一性。
 */
#define DECLARE_RBTREE_NONUNIQ(prefix, type, field, cmpfn)                     \
                                                                               \
macro_inline int prefix ## __cmp(const struct typed_rb_entry *a,               \
		const struct typed_rb_entry *b)                                \
{                                                                              \
	return cmpfn(container_of(a, type, field.re),                          \
			container_of(b, type, field.re));                      \
}                                                                              \
macro_inline int prefix ## __cmp_uq(const struct typed_rb_entry *a,            \
		const struct typed_rb_entry *b)                                \
{                                                                              \
	int cmpval = cmpfn(container_of(a, type, field.re),                    \
			container_of(b, type, field.re));                      \
	if (cmpval)                                                            \
		return cmpval;                                                 \
	if (a < b)                                                             \
		return -1;                                                     \
	if (a > b)                                                             \
		return 1;                                                      \
	return 0;                                                              \
}                                                                              \
                                                                               \
_DECLARE_RBTREE(prefix, type, field, prefix ## __cmp, prefix ## __cmp_uq);     \
MACRO_REQUIRE_SEMICOLON() /* end */

/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* _FRR_TYPERB_H */
