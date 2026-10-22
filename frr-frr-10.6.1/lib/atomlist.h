// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2016-2019  David Lamparter, for NetDEF, Inc.
 */

/*
 * 【文件导读】atomlist.h —— 无锁（lock-free）原子单链表 / 原子有序表
 *
 * 一、这个文件是干什么的
 *   提供“原子操作”的链表容器：多线程并发地插入、删除、弹出元素时，不需要外加
 *   互斥锁，靠 CPU 的原子比较交换（CAS）保证一致性。主要面向 RCU 类读多写少的
 *   场景，如路由 / 邻居表的无锁发布。本头文件定义底层结点 / 头部结构、指针位技巧，
 *   以及给业务用的类型安全宏（PREDECL_ATOMLIST / DECLARE_ATOMLIST）。
 *
 * 二、核心数据结构
 *   atomptr_t —— 带“低位标记位”的指针：最低位 ATOMPTR_LOCK 表示“该结点正在被
 *     删除、不可再更新”，其余高位是真正的指针；ATOPTR_MASK 用于抹掉标记位取回
 *     裸指针。ATOMLIST 结点内联在业务结构体里（无独立分配），通过 offsetof 反推
 *     业务对象地址。
 *   struct atomlist_item 只有一个原子 next 指针；struct atomlist_head 含原子
 *     first / last / count。另有基于同一套机制的原子有序表（atomsort）。
 *
 * 三、与其它模块的关系
 *   依赖 typesafe.h（宏生成类型安全包装）与 frratomic.h（atomic_uintptr_t 等）。
 *   atomlist.c 实现真正的 CAS 逻辑；atomsort.h 复用原子指针机制实现有序表。
 *
 * 四、阅读顺序
 *   先读 ATOMPTR_* 与 atomptr_* 内联函数（理解低位标记），再看 struct
 *   atomlist_item / atomlist_head，然后 PREDECL_ATOMLIST / DECLARE_ATOMLIST
 *   宏，最后看底部各原子操作函数（add_head / add_tail / del_hint / pop 等）原型。
 *   注意：在原子表上调用 find() 本质上无法做到原子，默认会给出弃用警告。
 */

#ifndef _FRR_ATOMLIST_H
#define _FRR_ATOMLIST_H

#include "typesafe.h"
#ifndef _TYPESAFE_EXPAND_MACROS
#include "frratomic.h"
#endif /* _TYPESAFE_EXPAND_MACROS */

#ifdef __cplusplus
extern "C" {
#endif

/* pointer with lock/deleted/invalid bit in lowest bit
 *
 * for atomlist/atomsort, "locked" means "this pointer can't be updated, the
 * item is being deleted".  it is permissible to assume the item will indeed
 * be deleted (as there are no replace/etc. ops in this).
 *
 * in general, lowest 2/3 bits on 32/64bit architectures are available for
 * uses like this; the only thing that will really break this is putting an
 * atomlist_item in a struct with "packed" attribute.  (it'll break
 * immediately and consistently.) -- don't do that.
 *
 * ATOMPTR_USER is currently unused (and available for atomic hash or skiplist
 * implementations.)
 */

/* atomic_atomptr_t may look a bit odd, it's for the sake of C++ compat */
/*
 * atomptr_t —— 把指针存成整数类型，低若干位可作标记位使用。
 * atomic_atomptr_t —— 对应的“原子版”，供 CAS 操作。C++ 兼容考虑才分别定义。
 */
typedef uintptr_t		atomptr_t;
typedef atomic_uintptr_t	atomic_atomptr_t;

/*
 * 指针标记位定义：
 *   ATOMPTR_MASK —— 抹掉最低两位后剩下的“真指针”掩码；
 *   ATOMPTR_LOCK —— 最低位，标记该结点正在被删除、不可再更新；
 *   ATOMPTR_USER —— 次低位，预留给 hash / skiplist 等；当前未使用；
 *   ATOMPTR_NULL —— 空值 0。
 * 约束：业务结构体不能用 packed 属性，否则结点地址对齐不足会破坏低位方案。
 */
#define ATOMPTR_MASK (UINTPTR_MAX - 3)
#define ATOMPTR_LOCK (1)
#define ATOMPTR_USER (2)
#define ATOMPTR_NULL (0)

/*
 * atomptr_i —— 把裸指针 val 转成 atomptr_t，断言其未带 LOCK 标记。
 * 用于“写入”路径：确保存进去的指针干净、无标记位。
 */
static inline atomptr_t atomptr_i(void *val)
{
	atomptr_t atomval = (atomptr_t)val;

	assert(!(atomval & ATOMPTR_LOCK));
	return atomval;
}
/*
 * atomptr_p —— 从 atomptr_t 中抹掉低位标记，取回真正的裸指针。
 * 用于“读取”路径；若指针本就是 NULL（0）则返回 NULL。
 */
static inline void *atomptr_p(atomptr_t val)
{
	return (void *)(val & ATOMPTR_MASK);
}
/*
 * atomptr_l —— 判断 atomptr_t 是否带 LOCK 标记（最低位为 1）。
 * 为真表示该结点正在被删除。
 */
static inline bool atomptr_l(atomptr_t val)
{
	return (bool)(val & ATOMPTR_LOCK);
}
/*
 * atomptr_u —— 判断 atomptr_t 是否带 USER 标记（次低位为 1）。
 * 该标记位当前未使用，留给未来 hash / skiplist 实现。
 */
static inline bool atomptr_u(atomptr_t val)
{
	return (bool)(val & ATOMPTR_USER);
}


/* the problem with, find(), find_gteq() and find_lt() on atomic lists is that
 * they're neither an "acquire" nor a "release" operation;  the element that
 * was found is still on the list and doesn't change ownership.  Therefore,
 * an atomic transition in ownership state can't be implemented.
 *
 * Contrast this with add() or pop(): both function calls atomically transfer
 * ownership of an item to or from the list, which makes them "acquire" /
 * "release" operations.
 *
 * What can be implemented atomically is a "find_pop()", i.e. try to locate an
 * item and atomically try to remove it if found.  It's not currently
 * implemented but can be added when needed.
 *
 * Either way - for find(), generally speaking, if you need to use find() on
 * a list then the whole thing probably isn't well-suited to atomic
 * implementation and you'll need to have extra locks around to make it work
 * correctly.
 */
/*
 * atomic_find_warn —— 用于给原子表上的 find() 打“不安全”弃用标记的宏。
 * 若未定义 WNO_ATOMLIST_UNSAFE_FIND，则展开为 __attribute__((deprecated))，
 * 编译时对在原子表上调用 find() 的地方报警；确认用法没问题后可定义该宏消除警告。
 */
#ifdef WNO_ATOMLIST_UNSAFE_FIND
# define atomic_find_warn
#else
# define atomic_find_warn __attribute__((_DEPRECATED( \
	"WARNING: find() on atomic lists cannot be atomic by principle; " \
	"check code to make sure usage pattern is OK and if it is, use " \
	"#define WNO_ATOMLIST_UNSAFE_FIND")))
#endif


/* single-linked list, unsorted/arbitrary.
 * can be used as queue with add_tail / pop
 *
 * all operations are lock-free, but not necessarily wait-free.  this means
 * that there is no state where the system as a whole stops making process,
 * but it *is* possible that a *particular* thread is delayed by some time.
 *
 * the only way for this to happen is for other threads to continuously make
 * updates.  an inactive / blocked / deadlocked other thread cannot cause such
 * delays, and to cause such delays a thread must be heavily hitting the list -
 * it's a rather theoretical concern.
 */

/* don't use these structs directly */
/*
 * atomlist_item —— 单链表结点，内联在业务结构体里（不单独分配）。
 * 只有一个原子 next 指针（存 atomptr_t，低位可带 LOCK 标记）。
 * atomlist_itemp(val) —— 把带标记的原子指针还原成结点指针。
 */
struct atomlist_item {
	atomic_uintptr_t next;
};
#define atomlist_itemp(val) ((struct atomlist_item *)atomptr_p(val))

/*
 * atomlist_head —— 链表头部。
 *   first / last —— 首、尾结点的原子指针（last 使 add_tail 高效）；
 *   count        —— 原子元素计数，读取用 relaxed 语义即可。
 * 可被用作队列：add_tail 入队、pop 出队。所有操作无锁，但非 wait-free。
 */
struct atomlist_head {
	atomic_uintptr_t first, last;
	atomic_size_t count;
};

/* clang-format off */

/* use as:
 *
 * PREDECL_ATOMLIST(namelist);
 * struct name {
 *   struct namelist_item nlitem;
 * }
 * DECLARE_ATOMLIST(namelist, struct name, nlitem);
 */
/*
 * PREDECL_ATOMLIST(prefix) —— 前向声明一组类型安全的原子表类型。
 * 展开出 struct prefix_head 与 struct prefix_item（分别包装 atomlist_head /
 * atomlist_item）。业务结构体里放一个 prefix_item 字段即可挂入该表。
 * 用法见上方英文示例：先前向声明，再用 DECLARE_ATOMLIST 生成操作函数。
 */
#define PREDECL_ATOMLIST(prefix)                                               \
struct prefix ## _head { struct atomlist_head ah; };                           \
struct prefix ## _item { struct atomlist_item ai; };                           \
MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * INIT_ATOMLIST(var) —— 原子表头部的静态初始化占位（K&R 风格空初始化）。
 * 一般直接用 memset 清零（见 DECLARE_ATOMLIST 生成的 prefix_init）。
 */
#define INIT_ATOMLIST(var) { }

/*
 * DECLARE_ATOMLIST(prefix, type, field) —— 生成类型安全的原子表操作函数。
 *   prefix  类型前缀，生成的函数形如 prefix_add_head / prefix_pop ...
 *   type    业务结构体类型；field 业务结构体里那个 prefix_item 字段名。
 * 生成：add_head / add_tail / del_hint / del / pop / first / next / next_safe /
 *   count / init / fini。其中 next/next 系列用 offsetof 从结点反推业务对象地址。
 * 坑：init/fini 用 memset 清零/校验，fini 会断言表已空。
 */
#define DECLARE_ATOMLIST(prefix, type, field)                                  \
macro_inline void prefix ## _add_head(struct prefix##_head *h, type *item)     \
{	atomlist_add_head(&h->ah, &item->field.ai); }                          \
macro_inline void prefix ## _add_tail(struct prefix##_head *h, type *item)     \
{	atomlist_add_tail(&h->ah, &item->field.ai); }                          \
macro_inline void prefix ## _del_hint(struct prefix##_head *h, type *item,     \
		atomic_atomptr_t *hint)                                        \
{	atomlist_del_hint(&h->ah, &item->field.ai, hint); }                    \
macro_inline type *prefix ## _del(struct prefix##_head *h, type *item)         \
{	atomlist_del_hint(&h->ah, &item->field.ai, NULL);                      \
	/* TODO: Return NULL if not found */                                   \
	return item; }                                                         \
macro_inline type *prefix ## _pop(struct prefix##_head *h)                     \
{	char *p = (char *)atomlist_pop(&h->ah);                                \
	return p ? (type *)(p - offsetof(type, field)) : NULL; }               \
macro_inline type *prefix ## _first(struct prefix##_head *h)                   \
{	char *p = atomptr_p(atomic_load_explicit(&h->ah.first,                 \
				memory_order_acquire));                        \
	return p ? (type *)(p - offsetof(type, field)) : NULL; }               \
macro_inline type *prefix ## _next(struct prefix##_head *h, type *item)        \
{	char *p = atomptr_p(atomic_load_explicit(&item->field.ai.next,         \
				memory_order_acquire));                        \
	return p ? (type *)(p - offsetof(type, field)) : NULL; }               \
macro_inline type *prefix ## _next_safe(struct prefix##_head *h, type *item)   \
{	return item ? prefix##_next(h, item) : NULL; }                         \
macro_inline size_t prefix ## _count(struct prefix##_head *h)                  \
{	return atomic_load_explicit(&h->ah.count, memory_order_relaxed); }     \
macro_inline void prefix ## _init(struct prefix##_head *h)                     \
{                                                                              \
	memset(h, 0, sizeof(*h));                                              \
}                                                                              \
macro_inline void prefix ## _fini(struct prefix##_head *h)                     \
{                                                                              \
	assert(prefix ## _count(h) == 0);                                      \
	memset(h, 0, sizeof(*h));                                              \
}                                                                              \
MACRO_REQUIRE_SEMICOLON() /* end */

/* clang-format on */

/*
 * atomlist_add_head —— 把 item 头插到链表 h 的最前面。
 * 无锁实现（CAS 竞争的是 h->first）；函数返回即表示插入已完成。
 */
/* add_head:
 * - contention on ->first pointer
 * - return implies completion
 */
void atomlist_add_head(struct atomlist_head *h, struct atomlist_item *item);

/*
 * atomlist_add_tail —— 把 item 尾插到链表 h 末尾（可用作队列入队）。
 * 并发 add_tail 之间可能需要等待，但有进展保证；函数返回并不意味着该结点对
 * 其它线程可见，只有当“此调用之前已开始的其它 add_tail 都完成”后才算完成。
 */
/* add_tail:
 * - concurrent add_tail can cause wait but has progress guarantee
 * - return does NOT imply completion.  completion is only guaranteed after
 *   all other add_tail operations that started before this add_tail have
 *   completed as well.
 */
void atomlist_add_tail(struct atomlist_head *h, struct atomlist_item *item);

/*
 * atomlist_del_hint —— 从链表 h 删除 item；hint 是调用方提供的“前驱指针缓存”，
 *   用于加速定位（可为 NULL 表示从头找）。
 * 前置条件：调用方必须独占持有该 item 的引用，确保没有其它线程同时删它（含 pop）。
 * 早于本次删除开始读取的线程可能仍持有旧指针；晚于删除开始读取的线程保证看不到它。
 */
/* del/del_hint:
 *
 * OWNER MUST HOLD REFERENCE ON ITEM TO BE DELETED, ENSURING NO OTHER THREAD
 * WILL TRY TO DELETE THE SAME ITEM.  DELETING INCLUDES pop().
 *
 * as with all deletions, threads that started reading earlier may still hold
 * pointers to the deleted item.  completion is however guaranteed for all
 * reads starting later.
 */
void atomlist_del_hint(struct atomlist_head *h, struct atomlist_item *item,
		atomic_atomptr_t *hint);

/*
 * atomlist_pop —— 从链表头弹出一个元素并返回其结点指针；空表返回 NULL。
 * 用于把该表当队列/栈消费。与所有删除一样：更早开始读的线程可能仍持有该结点指针。
 */
/* pop:
 *
 * as with all deletions, threads that started reading earlier may still hold
 * pointers to the deleted item.  completion is however guaranteed for all
 * reads starting later.
 */
struct atomlist_item *atomlist_pop(struct atomlist_head *h);



/*
 * atomsort_item —— 原子“有序表”的结点，内联在业务结构体里。
 * 与 atomlist_item 类似，但表按调用方提供的比较函数保持排序。
 * atomsort_itemp(val) —— 把带标记的原子指针还原成结点指针。
 */
struct atomsort_item {
	atomic_atomptr_t next;
};
#define atomsort_itemp(val) ((struct atomsort_item *)atomptr_p(val))

/*
 * atomsort_head —— 原子有序表的头部：原子首指针 first 与原子计数 count。
 * 插入时需按排序位置找到合适的前驱，因此比无序原子表更费；适合读多写少的有序集合。
 */
struct atomsort_head {
	atomic_atomptr_t first;
	atomic_size_t count;
};

/* clang-format off */

/*
 * _PREDECL_ATOMSORT(prefix) —— 内部：前向声明原子有序表的一对类型包装。
 * 业务代码一般不直接用，而是用下面的 PREDECL_ATOMSORT_UNIQ / _NONUNIQ。
 */
#define _PREDECL_ATOMSORT(prefix)                                              \
struct prefix ## _head { struct atomsort_head ah; };                           \
struct prefix ## _item { struct atomsort_item ai; };                           \
MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * INIT_ATOMSORT_UNIQ / INIT_ATOMSORT_NONUNIQ(var) —— 原子有序表头部的空初始化占位。
 * UNIQ 指表中 key 唯一，NONUNIQ 指允许重复 key；两者初始化都为空。
 */
#define INIT_ATOMSORT_UNIQ(var)		{ }
#define INIT_ATOMSORT_NONUNIQ(var)	{ }

/*
 * _DECLARE_ATOMSORT(prefix, type, field, cmpfn_nuq, cmpfn_uq) —— 内部：生成有序表操作。
 *   cmpfn_nuq 不把“结点地址”纳入比较（用于遍历查找），cmpfn_uq 在 key 相等时
 *   再按结点地址比较以保证全序（用于插入定位）。
 * 生成 init / fini / add / first / next / next_safe / find_gteq / find_lt /
 *   del_hint / del / count / pop，底层转调 atomsort_* 函数。
 * find_gteq / find_lt 因无法原子化而带 atomic_find_warn 弃用标记。
 */
#define _DECLARE_ATOMSORT(prefix, type, field, cmpfn_nuq, cmpfn_uq)            \
macro_inline void prefix ## _init(struct prefix##_head *h)                     \
{                                                                              \
	memset(h, 0, sizeof(*h));                                              \
}                                                                              \
macro_inline void prefix ## _fini(struct prefix##_head *h)                     \
{                                                                              \
	assert(h->ah.count == 0);                                              \
	memset(h, 0, sizeof(*h));                                              \
}                                                                              \
macro_inline type *prefix ## _add(struct prefix##_head *h, type *item)         \
{                                                                              \
	struct atomsort_item *p;                                               \
	p = atomsort_add(&h->ah, &item->field.ai, cmpfn_uq);                   \
	if (p)                                                                 \
		_sa_dummy_store(item);                                         \
	return container_of_null(p, type, field.ai);                           \
}                                                                              \
macro_inline type *prefix ## _first(struct prefix##_head *h)                   \
{                                                                              \
	struct atomsort_item *p;                                               \
	p = atomptr_p(atomic_load_explicit(&h->ah.first,                       \
				memory_order_acquire));                        \
	return container_of_null(p, type, field.ai);                           \
}                                                                              \
macro_inline type *prefix ## _next(struct prefix##_head *h, type *item)        \
{                                                                              \
	struct atomsort_item *p;                                               \
	p = atomptr_p(atomic_load_explicit(&item->field.ai.next,               \
				memory_order_acquire));                        \
	return container_of_null(p, type, field.ai);                           \
}                                                                              \
macro_inline type *prefix ## _next_safe(struct prefix##_head *h, type *item)   \
{                                                                              \
	return item ? prefix##_next(h, item) : NULL;                           \
}                                                                              \
atomic_find_warn                                                               \
macro_inline type *prefix ## _find_gteq(struct prefix##_head *h,               \
		const type *item)                                              \
{                                                                              \
	type *p = prefix ## _first(h);                                         \
	while (p && cmpfn_nuq(&p->field.ai, &item->field.ai) < 0)              \
		p = prefix ## _next(h, p);                                     \
	return p;                                                              \
}                                                                              \
atomic_find_warn                                                               \
macro_inline type *prefix ## _find_lt(struct prefix##_head *h,                 \
		const type *item)                                              \
{                                                                              \
	type *p = prefix ## _first(h), *prev = NULL;                           \
	while (p && cmpfn_nuq(&p->field.ai, &item->field.ai) < 0)              \
		p = prefix ## _next(h, (prev = p));                            \
	return prev;                                                           \
}                                                                              \
macro_inline void prefix ## _del_hint(struct prefix##_head *h, type *item,     \
		atomic_atomptr_t *hint)                                        \
{                                                                              \
	atomsort_del_hint(&h->ah, &item->field.ai, hint);                      \
}                                                                              \
macro_inline type *prefix ## _del(struct prefix##_head *h, type *item)         \
{                                                                              \
	atomsort_del_hint(&h->ah, &item->field.ai, NULL);                      \
	/* TODO: Return NULL if not found */                                   \
	return item;                                                           \
}                                                                              \
macro_inline size_t prefix ## _count(struct prefix##_head *h)                  \
{                                                                              \
	return atomic_load_explicit(&h->ah.count, memory_order_relaxed);       \
}                                                                              \
macro_inline type *prefix ## _pop(struct prefix##_head *h)                     \
{                                                                              \
	struct atomsort_item *p = atomsort_pop(&h->ah);                        \
	return p ? container_of(p, type, field.ai) : NULL;                     \
}                                                                              \
MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * PREDECL_ATOMSORT_UNIQ(prefix) —— 前向声明“key 唯一”的原子有序表类型。
 * 等同 _PREDECL_ATOMSORT；配合 DECLARE_ATOMSORT_UNIQ 使用。
 */
#define PREDECL_ATOMSORT_UNIQ(prefix)                                          \
	_PREDECL_ATOMSORT(prefix)
/*
 * DECLARE_ATOMSORT_UNIQ(prefix, type, field, cmpfn) —— 生成“key 唯一”有序表操作。
 * 额外生成 prefix_find：遍历找到 key 相等者返回，否则返回 NULL。
 * cmpfn 既作遍历比较也作插入定位比较（唯一键下无需再比地址）。
 */
#define DECLARE_ATOMSORT_UNIQ(prefix, type, field, cmpfn)                      \
                                                                               \
macro_inline int prefix ## __cmp(const struct atomsort_item *a,                \
		const struct atomsort_item *b)                                 \
{                                                                              \
	return cmpfn(container_of(a, type, field.ai),                          \
			container_of(b, type, field.ai));                      \
}                                                                              \
                                                                               \
_DECLARE_ATOMSORT(prefix, type, field,                                         \
		prefix ## __cmp, prefix ## __cmp);                             \
                                                                               \
atomic_find_warn                                                               \
macro_inline type *prefix ## _find(struct prefix##_head *h, const type *item)  \
{                                                                              \
	type *p = prefix ## _first(h);                                         \
	int cmpval = 0;                                                        \
	while (p && (cmpval = cmpfn(p, item)) < 0)                             \
		p = prefix ## _next(h, p);                                     \
	if (!p || cmpval > 0)                                                  \
		return NULL;                                                   \
	return p;                                                              \
}                                                                              \
MACRO_REQUIRE_SEMICOLON() /* end */

/*
 * PREDECL_ATOMSORT_NONUNIQ(prefix) —— 前向声明“允许重复 key”的原子有序表类型。
 * 等同 _PREDECL_ATOMSORT；配合 DECLARE_ATOMSORT_NONUNIQ 使用。
 */
#define PREDECL_ATOMSORT_NONUNIQ(prefix)                                       \
	_PREDECL_ATOMSORT(prefix)
/*
 * DECLARE_ATOMSORT_NONUNIQ(prefix, type, field, cmpfn) —— 生成“允许重复 key”的操作。
 * 额外生成 __cmp_uq：key 相等时再按结点地址排序，从而给重复键一个稳定全序，
 * 保证插入定位唯一。不生成 prefix_find（重复键下 find 无意义）。
 */
#define DECLARE_ATOMSORT_NONUNIQ(prefix, type, field, cmpfn)                   \
                                                                               \
macro_inline int prefix ## __cmp(const struct atomsort_item *a,                \
		const struct atomsort_item *b)                                 \
{                                                                              \
	return cmpfn(container_of(a, type, field.ai),                          \
			container_of(b, type, field.ai));                      \
}                                                                              \
macro_inline int prefix ## __cmp_uq(const struct atomsort_item *a,             \
		const struct atomsort_item *b)                                 \
{                                                                              \
	int cmpval = cmpfn(container_of(a, type, field.ai),                    \
			container_of(b, type, field.ai));                      \
	if (cmpval)                                                            \
		return cmpval;                                                 \
	if (a < b)                                                             \
		return -1;                                                     \
	if (a > b)                                                             \
		return 1;                                                      \
	return 0;                                                              \
}                                                                              \
                                                                               \
_DECLARE_ATOMSORT(prefix, type, field,                                         \
		prefix ## __cmp, prefix ## __cmp_uq);                          \
MACRO_REQUIRE_SEMICOLON() /* end */

/* clang-format on */

/*
 * atomsort_add —— 按 cmpfn 把 item 插入有序表 h 的正确位置（无锁 CAS）。
 * 参数：h 表头；item 待插结点；cmpfn 比较函数。
 * 返回：成功插入返回 NULL；在唯一键表中发现相等键时返回已存在的那个结点（不插入）。
 */
struct atomsort_item *atomsort_add(struct atomsort_head *h,
		struct atomsort_item *item, int (*cmpfn)(
			const struct atomsort_item *,
			const struct atomsort_item *));

/*
 * atomsort_del_hint —— 从有序表 h 删除 item，hint 为前驱指针缓存（可 NULL）。
 * 前置条件与 atomlist_del_hint 相同：调用方须独占持有该 item 引用。
 */
void atomsort_del_hint(struct atomsort_head *h,
		struct atomsort_item *item, atomic_atomptr_t *hint);

/*
 * atomsort_pop —— 弹出有序表 h 的第一个（最小）结点并返回；空表返回 NULL。
 */
struct atomsort_item *atomsort_pop(struct atomsort_head *h);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_ATOMLIST_H */
