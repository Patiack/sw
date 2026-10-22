// SPDX-License-Identifier: ISC AND BSD-2-Clause
/*	$OpenBSD: tree.h,v 1.14 2015/05/25 03:07:49 deraadt Exp $	*/
/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 */

#ifndef _SYS_TREE_H_
#define	_SYS_TREE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 文件导读（openbsd-tree.h）
 *
 * 本文件从 OpenBSD 的 sys/sys/tree.h 移植而来，是 FRR 中两类经典平衡树
 * 的纯头文件实现，全部以宏和内联函数形式提供：
 *
 *   1) SPLAY 树（自平衡伸展树，splay tree），所有宏以 SPLAY_ 前缀。它是自组织
 *      数据结构：每次查找 / 插入 / 删除都会把命中的节点"伸展"到树根，因此
 *      访问局部性好时后续查找更快；代价是每次查找都会写内存。
 *      SPLAY_PROTOTYPE 只生成函数原型与内联查找函数，SPLAY_GENERATE 生成真正
 *      的插入、删除、伸展实现；两者必须配套使用。
 *
 *   2) RB 树（红黑树），所有宏以 RB_ 前缀。它用 RB_PROTOTYPE 生成类型安全的内联
 *      包装，真正的算法在 lib/openbsd-tree.c 中通过 struct rb_type 描述符
 *      （比较函数、augment 回调、节点在宿主结构体中的偏移）以"侵入式内联节点"
 *      方式实现。RB_GENERATE 负责产出该描述符实例。
 *
 * 核心数据结构：
 *   - struct rb_type  : rb 算法回调与节点偏移描述符；
 *   - struct rb_entry : 侵入式红黑树节点（parent / left / right / color）；
 *   - struct rbt_tree : 树根容器，内含 rbt_root 指针；
 *   - SPLAY_HEAD / SPLAY_ENTRY : 伸展树的头与节点，由宏展开生成。
 *
 * 与其他模块的关系：
 *   - 被大量 FRR 模块使用（如路由表、哈希、事件管理等需要有序集合处）；
 *   - RB 依赖 lib/openbsd-tree.c 提供 _rb_ 开头的函数实体；
 *   - 节点内联在宿主结构体中，通过 offsetof 由 rb_type.t_offset 关联。
 *
 * 阅读顺序建议：
 *   先看本文件顶部的英文总述，再读 SPLAY 段（HEAD / ENTRY / 基本操作宏 ->
 *   PROTOTYPE -> GENERATE -> 对外封装宏），最后读 RB 段（描述符结构 ->
 *   _rb_ 函数原型 -> RB_PROTOTYPE -> RB_GENERATE -> 对外封装宏）。
 *
 * 注意：本文件是 BSD 长期验证过的成熟代码，FRR 引入它主要为获得类型安全与
 * 一致性；新增数据结构时官方建议优先使用类型安全的新结构。
 */

/*
 * NOTICE:
 *
 * If you are reading this file in an effort to add a new tree structure
 * this is the wrong place to be using it.  Please see the typesafe
 * data structures, or ask one of the other developers.
 *
 * If you are reading this file as a way to update an existing usage
 * of this data structure, please consider just converting the data
 * structure to one of the typesafe data structures instead.  However,
 * among converting datastrucutres, the the BSD ones are the lowest
 * priority / should be converted last. They are already typesafe and
 * use inline linking nodes, so the only gain is consistency. Please
 * convert uses of linklist.h and hash.h first.
 */

/*
 * This file defines data structures for different types of trees:
 * splay trees and red-black trees.
 *
 * A splay tree is a self-organizing data structure.  Every operation
 * on the tree causes a splay to happen.  The splay moves the requested
 * node to the root of the tree and partly rebalances it.
 *
 * This has the benefit that request locality causes faster lookups as
 * the requested nodes move to the top of the tree.  On the other hand,
 * every lookup causes memory writes.
 *
 * The Balance Theorem bounds the total access time for m operations
 * and n inserts on an initially empty tree as O((m + n)lg n).  The
 * amortized cost for a sequence of m accesses to a splay tree is O(lg n);
 *
 * A red-black tree is a binary search tree with the node color as an
 * extra attribute.  It fulfills a set of conditions:
 *	- every search path from the root to a leaf consists of the
 *	  same number of black nodes,
 *	- each red node (except for the root) has a black parent,
 *	- each leaf node is black.
 *
 * Every operation on a red-black tree is bounded as O(lg n).
 * The maximum height of a red-black tree is 2lg (n+1).
 */

/* SPLAY_HEAD：定义伸展树的头（容器）结构体 name。
 * 展开后 name 只含一个指针字段 sph_root，指向树根节点（type 类型）。
 * 用法：在全局或模块级放置一个 struct name 作为树头。 */
#define SPLAY_HEAD(name, type)                                                 \
	struct name {                                                          \
		struct type *sph_root; /* root of the tree */                  \
	}

/* SPLAY_INITIALIZER：给出可在编译期初始化树头的初始化式（等价于把根置 NULL）。
 * 用法：定义静态全局树头时 struct name head = SPLAY_INITIALIZER(head); */
#define SPLAY_INITIALIZER(root)                                                \
	{                                                                      \
		NULL                                                           \
	}

/* SPLAY_INIT：在运行期把树头 root 的根指针清空。
 * 参数 root 为指向头结构体的指针；无返回值。
 * 坑：对已挂有节点的树调用会让原有节点全部失联（节点内存泄漏）。 */
#define SPLAY_INIT(root)                                                       \
	do {                                                                   \
		(root)->sph_root = NULL;                                       \
	} while (0)

/* SPLAY_ENTRY：在宿主节点结构体里内联一个伸展树节点。
 * 展开后含 spe_left / spe_right 两个指向 type 的指针，侵入式挂在宿主上。
 * field 即该匿名结构体在宿主里的字段名，后续宏作为访问路径使用。 */
#define SPLAY_ENTRY(type)                                                      \
	struct {                                                               \
		struct type *spe_left;  /* left element */                     \
		struct type *spe_right; /* right element */                    \
	}

/* SPLAY 基本访问宏：按字段 field 读取节点 elm 的左 / 右子指针，
 * 或读取头 head 的根指针、判断树是否为空（根为 NULL）。
 * 参数 elm 节点、head 头、field 节点字段名；SPLAY_EMPTY 返回布尔值。 */
#define SPLAY_LEFT(elm, field)		(elm)->field.spe_left
#define SPLAY_RIGHT(elm, field)		(elm)->field.spe_right
#define SPLAY_ROOT(head)		(head)->sph_root
#define SPLAY_EMPTY(head)		(SPLAY_ROOT(head) == NULL)

/* SPLAY_ROTATE_RIGHT / SPLAY_ROTATE_LEFT：单旋转原语，供伸展算法内部使用。
 * 前提（见下方英文注释）：tmp 已持有 head 根节点的左子（右旋）或右子（左旋）。
 * 效果：把 tmp 提升为新的根，原根下沉为 tmp 的子；原地改写指针，无返回值。 */
/* SPLAY_ROTATE_{LEFT,RIGHT} expect that tmp hold SPLAY_{RIGHT,LEFT} */
#define SPLAY_ROTATE_RIGHT(head, tmp, field)                                   \
	do {                                                                   \
		SPLAY_LEFT((head)->sph_root, field) = SPLAY_RIGHT(tmp, field); \
		SPLAY_RIGHT(tmp, field) = (head)->sph_root;                    \
		(head)->sph_root = tmp;                                        \
	} while (0)

#define SPLAY_ROTATE_LEFT(head, tmp, field)                                    \
	do {                                                                   \
		SPLAY_RIGHT((head)->sph_root, field) = SPLAY_LEFT(tmp, field); \
		SPLAY_LEFT(tmp, field) = (head)->sph_root;                     \
		(head)->sph_root = tmp;                                        \
	} while (0)

/* SPLAY_LINKLEFT / SPLAY_LINKRIGHT：自顶向下 splay 的"拆链"步骤。
 * 把当前根挂到左 / 右临时链的链尾，并让链尾游标 tmp 前进到刚挂上的节点。
 * 副作用：改写 head->sph_root 与参数 tmp 的指向；仅供 SPLAY_GENERATE 内部使用。 */
#define SPLAY_LINKLEFT(head, tmp, field)                                       \
	do {                                                                   \
		SPLAY_LEFT(tmp, field) = (head)->sph_root;                     \
		tmp = (head)->sph_root;                                        \
		(head)->sph_root = SPLAY_LEFT((head)->sph_root, field);        \
	} while (0)

#define SPLAY_LINKRIGHT(head, tmp, field)                                      \
	do {                                                                   \
		SPLAY_RIGHT(tmp, field) = (head)->sph_root;                    \
		tmp = (head)->sph_root;                                        \
		(head)->sph_root = SPLAY_RIGHT((head)->sph_root, field);       \
	} while (0)

/* SPLAY_ASSEMBLE：自顶向下 splay 的最后"组装"步骤。
 * 把左临时链 left、右临时链 right 重新接回以 head 当前根为中心的两侧，
 * node 为哨兵节点。副作用：原地改写 head 根以及 node / left / right 的左右指针。 */
#define SPLAY_ASSEMBLE(head, node, left, right, field)                         \
	do {                                                                   \
		SPLAY_RIGHT(left, field) =                                     \
			SPLAY_LEFT((head)->sph_root, field);                   \
		SPLAY_LEFT(right, field) =                                     \
			SPLAY_RIGHT((head)->sph_root, field);                  \
		SPLAY_LEFT((head)->sph_root, field) =                          \
			SPLAY_RIGHT(node, field);                              \
		SPLAY_RIGHT((head)->sph_root, field) =                         \
			SPLAY_LEFT(node, field);                               \
	} while (0)

/* 以下 SPLAY_PROTOTYPE 与 SPLAY_GENERATE 成对使用：前者生成原型与内联只读函数，
 * 后者生成真正的增删与伸展实体；两者必须传入一致的 name / type / field / cmp。 */
/* Generates prototypes and inline functions */

/* SPLAY_PROTOTYPE：为树 name 生成函数原型和"只读"内联函数。
 * 生成 name_SPLAY / name_SPLAY_MINMAX / name_SPLAY_INSERT / name_SPLAY_REMOVE
 * 的函数声明，以及内联的 FIND（按 cmp 相等查找，命中后即 splay 到根）、
 * NEXT（中序后继）、MIN_MAX（取最小 / 最大并 splay）。
 * cmp(a,b) 约定：a 小于 b 返回负、a 大于 b 返回正、相等返回 0。
 * 需与 SPLAY_GENERATE 配套；type 为宿主结构体类型，field 为 SPLAY_ENTRY 字段名。 */
#define SPLAY_PROTOTYPE(name, type, field, cmp)                                \
	void name##_SPLAY(struct name *, struct type *);                       \
	void name##_SPLAY_MINMAX(struct name *, int);                          \
	struct type *name##_SPLAY_INSERT(struct name *, struct type *);        \
	struct type *name##_SPLAY_REMOVE(struct name *, struct type *);        \
                                                                               \
	/* Finds the node with the same key as elm */                          \
	static __inline struct type *name##_SPLAY_FIND(struct name *head,      \
						       struct type *elm)       \
	{                                                                      \
		if (SPLAY_EMPTY(head))                                         \
			return (NULL);                                         \
		name##_SPLAY(head, elm);                                       \
		if ((cmp)(elm, (head)->sph_root) == 0)                         \
			return (head->sph_root);                               \
		return (NULL);                                                 \
	}                                                                      \
                                                                               \
	static __inline struct type *name##_SPLAY_NEXT(struct name *head,      \
						       struct type *elm)       \
	{                                                                      \
		name##_SPLAY(head, elm);                                       \
		if (SPLAY_RIGHT(elm, field) != NULL) {                         \
			elm = SPLAY_RIGHT(elm, field);                         \
			while (SPLAY_LEFT(elm, field) != NULL) {               \
				elm = SPLAY_LEFT(elm, field);                  \
			}                                                      \
		} else                                                         \
			elm = NULL;                                            \
		return (elm);                                                  \
	}                                                                      \
                                                                               \
	static __inline struct type *name##_SPLAY_MIN_MAX(struct name *head,   \
							  int val)             \
	{                                                                      \
		name##_SPLAY_MINMAX(head, val);                                \
		return (SPLAY_ROOT(head));                                     \
	}

/* SPLAY_GENERATE：为树 name 生成插入、删除、伸展等非内联函数实体。
 *   - name_SPLAY_INSERT：按 cmp 插入 elm；若存在相等键则返回既有节点（不插入），
 *     插入成功返回 NULL；
 *   - name_SPLAY_REMOVE：把 elm splay 到根后摘除，返回被移除节点或 NULL；
 *   - name_SPLAY：自顶向下把最接近 elm 的节点 splay 到根（内部核心操作）；
 *   - name_SPLAY_MINMAX：把最小（__comp 为负）/ 最大（__comp 为正）节点 splay 到根。
 * 每次操作都会改写树结构，这正是"访问局部性好但每次查找会写内存"的来源。 */
/* Main splay operation.
 * Moves node close to the key of elm to top
 */
#define SPLAY_GENERATE(name, type, field, cmp)                                 \
	struct type *name##_SPLAY_INSERT(struct name *head, struct type *elm)  \
	{                                                                      \
		if (SPLAY_EMPTY(head)) {                                       \
			SPLAY_LEFT(elm, field) = SPLAY_RIGHT(elm, field) =     \
				NULL;                                          \
		} else {                                                       \
			int __comp;                                            \
			name##_SPLAY(head, elm);                               \
			__comp = (cmp)(elm, (head)->sph_root);                 \
			if (__comp < 0) {                                      \
				SPLAY_LEFT(elm, field) =                       \
					SPLAY_LEFT((head)->sph_root, field);   \
				SPLAY_RIGHT(elm, field) = (head)->sph_root;    \
				SPLAY_LEFT((head)->sph_root, field) = NULL;    \
			} else if (__comp > 0) {                               \
				SPLAY_RIGHT(elm, field) =                      \
					SPLAY_RIGHT((head)->sph_root, field);  \
				SPLAY_LEFT(elm, field) = (head)->sph_root;     \
				SPLAY_RIGHT((head)->sph_root, field) = NULL;   \
			} else                                                 \
				return ((head)->sph_root);                     \
		}                                                              \
		(head)->sph_root = (elm);                                      \
		return (NULL);                                                 \
	}                                                                      \
                                                                               \
	struct type *name##_SPLAY_REMOVE(struct name *head, struct type *elm)  \
	{                                                                      \
		struct type *__tmp;                                            \
		if (SPLAY_EMPTY(head))                                         \
			return (NULL);                                         \
		name##_SPLAY(head, elm);                                       \
		if ((cmp)(elm, (head)->sph_root) == 0) {                       \
			if (SPLAY_LEFT((head)->sph_root, field) == NULL) {     \
				(head)->sph_root =                             \
					SPLAY_RIGHT((head)->sph_root, field);  \
			} else {                                               \
				__tmp = SPLAY_RIGHT((head)->sph_root, field);  \
				(head)->sph_root =                             \
					SPLAY_LEFT((head)->sph_root, field);   \
				name##_SPLAY(head, elm);                       \
				SPLAY_RIGHT((head)->sph_root, field) = __tmp;  \
			}                                                      \
			return (elm);                                          \
		}                                                              \
		return (NULL);                                                 \
	}                                                                      \
                                                                               \
	void name##_SPLAY(struct name *head, struct type *elm)                 \
	{                                                                      \
		struct type __node, *__left, *__right, *__tmp;                 \
		int __comp;                                                    \
                                                                               \
		SPLAY_LEFT(&__node, field) = SPLAY_RIGHT(&__node, field) =     \
			NULL;                                                  \
		__left = __right = &__node;                                    \
                                                                               \
		while ((__comp = (cmp)(elm, (head)->sph_root))) {              \
			if (__comp < 0) {                                      \
				__tmp = SPLAY_LEFT((head)->sph_root, field);   \
				if (__tmp == NULL)                             \
					break;                                 \
				if ((cmp)(elm, __tmp) < 0) {                   \
					SPLAY_ROTATE_RIGHT(head, __tmp,        \
							   field);             \
					if (SPLAY_LEFT((head)->sph_root,       \
						       field)                  \
					    == NULL)                           \
						break;                         \
				}                                              \
				SPLAY_LINKLEFT(head, __right, field);          \
			} else if (__comp > 0) {                               \
				__tmp = SPLAY_RIGHT((head)->sph_root, field);  \
				if (__tmp == NULL)                             \
					break;                                 \
				if ((cmp)(elm, __tmp) > 0) {                   \
					SPLAY_ROTATE_LEFT(head, __tmp, field); \
					if (SPLAY_RIGHT((head)->sph_root,      \
							field)                 \
					    == NULL)                           \
						break;                         \
				}                                              \
				SPLAY_LINKRIGHT(head, __left, field);          \
			}                                                      \
		}                                                              \
		SPLAY_ASSEMBLE(head, &__node, __left, __right, field);         \
	}                                                                      \
                                                                               \
	/* Splay with either the minimum or the maximum element                \
	 * Used to find minimum or maximum element in tree.                    \
	 */                                                                    \
	void name##_SPLAY_MINMAX(struct name *head, int __comp)                \
	{                                                                      \
		struct type __node, *__left, *__right, *__tmp;                 \
                                                                               \
		SPLAY_LEFT(&__node, field) = SPLAY_RIGHT(&__node, field) =     \
			NULL;                                                  \
		__left = __right = &__node;                                    \
                                                                               \
		while (1) {                                                    \
			if (__comp < 0) {                                      \
				__tmp = SPLAY_LEFT((head)->sph_root, field);   \
				if (__tmp == NULL)                             \
					break;                                 \
				if (__comp < 0) {                              \
					SPLAY_ROTATE_RIGHT(head, __tmp,        \
							   field);             \
					if (SPLAY_LEFT((head)->sph_root,       \
						       field)                  \
					    == NULL)                           \
						break;                         \
				}                                              \
				SPLAY_LINKLEFT(head, __right, field);          \
			} else if (__comp > 0) {                               \
				__tmp = SPLAY_RIGHT((head)->sph_root, field);  \
				if (__tmp == NULL)                             \
					break;                                 \
				if (__comp > 0) {                              \
					SPLAY_ROTATE_LEFT(head, __tmp, field); \
					if (SPLAY_RIGHT((head)->sph_root,      \
							field)                 \
					    == NULL)                           \
						break;                         \
				}                                              \
				SPLAY_LINKRIGHT(head, __left, field);          \
			}                                                      \
		}                                                              \
		SPLAY_ASSEMBLE(head, &__node, __left, __right, field);         \
	}

/* SPLAY_NEGINF / SPLAY_INF：传给 SPLAY_MINMAX 的方向哨兵值。
 * 负值表示向最小端 splay，正值表示向最大端，供 SPLAY_MIN / SPLAY_MAX 使用。 */
#define SPLAY_NEGINF	-1
#define SPLAY_INF	1

/* SPLAY 对外封装宏：按树名 name 调用，转发到生成的 name_SPLAY_ 函数。
 * INSERT / REMOVE / FIND / NEXT 分别对应插入、删除、精确查找、取中序后继。
 * SPLAY_MIN / SPLAY_MAX：取最小 / 最大节点（内部用 MINMAX 伸展到根），空树返回 NULL。 */
#define SPLAY_INSERT(name, x, y)	name##_SPLAY_INSERT(x, y)
#define SPLAY_REMOVE(name, x, y)	name##_SPLAY_REMOVE(x, y)
#define SPLAY_FIND(name, x, y)		name##_SPLAY_FIND(x, y)
#define SPLAY_NEXT(name, x, y)		name##_SPLAY_NEXT(x, y)
#define SPLAY_MIN(name, x)                                                     \
	(SPLAY_EMPTY(x) ? NULL : name##_SPLAY_MIN_MAX(x, SPLAY_NEGINF))
#define SPLAY_MAX(name, x)                                                     \
	(SPLAY_EMPTY(x) ? NULL : name##_SPLAY_MIN_MAX(x, SPLAY_INF))

/* SPLAY_FOREACH：从小到大遍历树 head，x 依次取每个节点。
 * 展开为 for 循环：x 从 SPLAY_MIN 开始，逐次取 SPLAY_NEXT 直到 NULL。
 * 坑：每次取值都会 splay 改写树结构，遍历过程中不要并发修改或释放节点。 */
#define SPLAY_FOREACH(x, name, head)                                           \
	for ((x) = SPLAY_MIN(name, head); (x) != NULL;                         \
	     (x) = SPLAY_NEXT(name, head, x))

/*
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
 */

/* ===== 红黑树（RB）部分 =====
 * 与上面纯宏的 SPLAY 不同，RB 采用"描述符 + 泛型函数"方案：算法实体在
 * lib/openbsd-tree.c 的 _rb_ 函数里，本头文件用 RB_PROTOTYPE 生成类型安全的内联
 * 包装，用 RB_GENERATE 生成描述符实例，节点通过偏移量内联在宿主结构体中。
 * 红黑树每次操作都是 O(lg n)，与访问模式无关，适合需要稳定有序集合的场景。 */

/* RB_BLACK / RB_RED：红黑树节点颜色常量，写入 struct rb_entry 的 rbt_color 字段。 */
#define RB_BLACK	0
#define RB_RED		1

/* struct rb_type：一颗具体红黑树的类型描述符，把泛型算法与具体类型绑定。
 *   t_compare：比较函数，返回 小于 0 / 0 / 大于 0，用于排序与查找；
 *   t_augment：可选的"增强"回调，在结构变化后自底向上更新节点附加信息，
 *              不需要增强时传 NULL；
 *   t_offset ：rb_entry 在宿主结构体中的字节偏移，由 offsetof 取得，
 *              _rb_ 系列函数据此从 void* 宿主指针定位到内联节点。 */
struct rb_type {
	int (*t_compare)(const void *, const void *);
	void (*t_augment)(void *);
	unsigned int t_offset; /* offset of rb_entry in type */
};

/* struct rbt_tree：红黑树的根容器，仅含指向根节点的指针 rbt_root。 */
struct rbt_tree {
	struct rb_entry *rbt_root;
};

/* struct rb_entry：侵入式红黑树节点，内联在宿主结构体里。
 *   rbt_parent / rbt_left / rbt_right：父、左、右指针；
 *   rbt_color：颜色，取值 RB_BLACK / RB_RED。 */
struct rb_entry {
	struct rb_entry *rbt_parent;
	struct rb_entry *rbt_left;
	struct rb_entry *rbt_right;
	unsigned int rbt_color;
};

/* RB_HEAD(_name, _type)：定义红黑树头结构体 _name。
 * 展开后含一个 struct rbt_tree rbh_root 字段；_type 为宿主节点类型。
 * 用法：在需要持有的位置放置 struct _name 作为树头。 */
#define RB_HEAD(_name, _type)                                                  \
	struct _name {                                                         \
		struct rbt_tree rbh_root;                                      \
	}

/* RB_ENTRY(_type)：在宿主结构体里声明一个内联红黑树节点。
 * 实际类型固定为 struct rb_entry，_type 仅用于书写可读性。
 * 用法：struct foo { ...; RB_ENTRY(foo) link; ... }; */
#define RB_ENTRY(_type)	struct rb_entry

/* _rb_init：初始化树根 rbt，把 rbt_root 置为 NULL。
 * 内部使用，由 RB_PROTOTYPE 生成的内联包装 _name_RB_INIT 转发调用。 */
static inline void _rb_init(struct rbt_tree *rbt)
{
	rbt->rbt_root = NULL;
}

/* _rb_empty：判断树 rbt 是否为空（根为 NULL），返回布尔值。
 * 内部使用，由内联包装 _name_RB_EMPTY 转发调用。 */
static inline int _rb_empty(const struct rbt_tree *rbt)
{
	return (rbt->rbt_root == NULL);
}

/* 泛型红黑树算法函数声明，实现位于 lib/openbsd-tree.c。
 * 每个函数首参 dt 为 rb_type 描述符（绑定具体类型与比较 / 偏移），
 * 以 void* 收发宿主节点指针，调用方看到的却是类型安全的 _type*。
 *   insert / remove / find / nfind：插入、删除、精确查找、返回不小于键的最近节点；
 *   root / min / max / next / prev / left / right / parent：读取结构性节点；
 *   set_left / set_right / set_parent：改写子 / 父指针（供增强等高级用法）；
 *   poison / check：调试用，写入 / 校验节点哨兵值以发现悬空引用。
 * 注意 find / nfind 的默认语义基于比较函数，不要求键相等时的节点存在。 */
void *_rb_insert(const struct rb_type *, struct rbt_tree *, void *);
void *_rb_remove(const struct rb_type *, struct rbt_tree *, void *);
void *_rb_find(const struct rb_type *, const struct rbt_tree *, const void *);
void *_rb_nfind(const struct rb_type *, const struct rbt_tree *, const void *);
void *_rb_root(const struct rb_type *, const struct rbt_tree *);
void *_rb_min(const struct rb_type *, const struct rbt_tree *);
void *_rb_max(const struct rb_type *, const struct rbt_tree *);
void *_rb_next(const struct rb_type *, void *);
void *_rb_prev(const struct rb_type *, void *);
void *_rb_left(const struct rb_type *, void *);
void *_rb_right(const struct rb_type *, void *);
void *_rb_parent(const struct rb_type *, void *);
void _rb_set_left(const struct rb_type *, void *, void *);
void _rb_set_right(const struct rb_type *, void *, void *);
void _rb_set_parent(const struct rb_type *, void *, void *);
void _rb_poison(const struct rb_type *, void *, unsigned long);
int _rb_check(const struct rb_type *, void *, unsigned long);

/* RB_INITIALIZER(_head)：可在编译期初始化红黑树头的初始化式
 * （把 rbh_root 置空），用于静态全局树头定义。 */
#define RB_INITIALIZER(_head)	{ { NULL } }

/* RB_PROTOTYPE：为树名 _name 生成类型安全的对外接口。
 *   - 声明 extern 常量 _name_RB_TYPE（描述符实例，定义在 RB_GENERATE 中）；
 *   - 生成一组 static inline 包装：INIT / INSERT / REMOVE / FIND / NFIND /
 *     ROOT / EMPTY / MIN / MAX / NEXT / PREV / LEFT / RIGHT / PARENT /
 *     SET_LEFT / SET_RIGHT / SET_PARENT / POISON / CHECK，
 *     把 _type* 与描述符转发给 _rb_ 泛型函数。
 * 用 __attribute__((__unused__)) 抑制未被使用的内联函数告警。
 * 必须与 RB_GENERATE（或 RB_GENERATE_AUGMENT）配套，且 name / type / field / cmp 一致。 */
#define RB_PROTOTYPE(_name, _type, _field, _cmp)                               \
	extern const struct rb_type *const _name##_RB_TYPE;                    \
                                                                               \
	__attribute__((__unused__)) static inline void _name##_RB_INIT(        \
		struct _name *head)                                            \
	{                                                                      \
		_rb_init(&head->rbh_root);                                     \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_INSERT(struct _name *head, struct _type *elm)      \
	{                                                                      \
		return (struct _type *)_rb_insert(_name##_RB_TYPE,             \
						  &head->rbh_root, elm);       \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_REMOVE(struct _name *head, struct _type *elm)      \
	{                                                                      \
		return (struct _type *)_rb_remove(_name##_RB_TYPE,             \
						  &head->rbh_root, elm);       \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_FIND(const struct _name *head,                     \
				 const struct _type *key)                      \
	{                                                                      \
		return (struct _type *)_rb_find(_name##_RB_TYPE,               \
						&head->rbh_root, key);         \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_NFIND(const struct _name *head,                    \
				  const struct _type *key)                     \
	{                                                                      \
		return (struct _type *)_rb_nfind(_name##_RB_TYPE,              \
						 &head->rbh_root, key);        \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_ROOT(const struct _name *head)                     \
	{                                                                      \
		return (struct _type *)_rb_root(_name##_RB_TYPE,               \
						&head->rbh_root);              \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline int _name##_RB_EMPTY(        \
		const struct _name *head)                                      \
	{                                                                      \
		return _rb_empty(&head->rbh_root);                             \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_MIN(const struct _name *head)                      \
	{                                                                      \
		return (struct _type *)_rb_min(_name##_RB_TYPE,                \
					       &head->rbh_root);               \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_MAX(const struct _name *head)                      \
	{                                                                      \
		return (struct _type *)_rb_max(_name##_RB_TYPE,                \
					       &head->rbh_root);               \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_NEXT(struct _type *elm)                            \
	{                                                                      \
		return (struct _type *)_rb_next(_name##_RB_TYPE, elm);         \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_PREV(struct _type *elm)                            \
	{                                                                      \
		return (struct _type *)_rb_prev(_name##_RB_TYPE, elm);         \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_LEFT(struct _type *elm)                            \
	{                                                                      \
		return (struct _type *)_rb_left(_name##_RB_TYPE, elm);         \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_RIGHT(struct _type *elm)                           \
	{                                                                      \
		return (struct _type *)_rb_right(_name##_RB_TYPE, elm);        \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline struct _type                 \
		*_name##_RB_PARENT(struct _type *elm)                          \
	{                                                                      \
		return (struct _type *)_rb_parent(_name##_RB_TYPE, elm);       \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline void _name##_RB_SET_LEFT(    \
		struct _type *elm, struct _type *left)                         \
	{                                                                      \
		_rb_set_left(_name##_RB_TYPE, elm, left);                      \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline void _name##_RB_SET_RIGHT(   \
		struct _type *elm, struct _type *right)                        \
	{                                                                      \
		_rb_set_right(_name##_RB_TYPE, elm, right);                    \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline void _name##_RB_SET_PARENT(  \
		struct _type *elm, struct _type *parent)                       \
	{                                                                      \
		_rb_set_parent(_name##_RB_TYPE, elm, parent);                  \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline void _name##_RB_POISON(      \
		struct _type *elm, unsigned long poison)                       \
	{                                                                      \
		_rb_poison(_name##_RB_TYPE, elm, poison);                      \
	}                                                                      \
                                                                               \
	__attribute__((__unused__)) static inline int _name##_RB_CHECK(        \
		struct _type *elm, unsigned long poison)                       \
	{                                                                      \
		return _rb_check(_name##_RB_TYPE, elm, poison);                \
	}

/* RB_GENERATE_INTERNAL：生成红黑树描述符的底层宏（被下面两个宏复用）。
 * 定义静态比较垫片 _name_RB_COMPARE（把 void* 转回 _type* 再调 _cmp），
 * 再定义 _name_RB_INFO 描述符（依次为比较函数、增强回调 _aug、rb_entry 的 offsetof）。
 * 最后把常量指针 _name_RB_TYPE 指向该描述符。_aug 为 NULL 表示不做增强。
 * 坑：_name_RB_TYPE 不是宏而是 const 指针，故声明与定义要跨文件匹配。 */
#define RB_GENERATE_INTERNAL(_name, _type, _field, _cmp, _aug)                 \
	static int _name##_RB_COMPARE(const void *lptr, const void *rptr)      \
	{                                                                      \
		const struct _type *l = lptr, *r = rptr;                       \
		return _cmp(l, r);                                             \
	}                                                                      \
	static const struct rb_type _name##_RB_INFO = {                        \
		_name##_RB_COMPARE, _aug, offsetof(struct _type, _field),      \
	};                                                                     \
	const struct rb_type *const _name##_RB_TYPE = &_name##_RB_INFO;

/* RB_GENERATE_AUGMENT：带增强(augment)回调的红黑树生成宏。
 * 先定义静态回调 _name_RB_AUGMENT（把 void* 转成 _type* 后调用户 _aug），
 * 再复用 RB_GENERATE_INTERNAL 产出描述符。
 * 用途：维护子树聚合信息（如区间计数、最大端点等），_aug(p) 在结构变化后
 * 自底向上更新节点 p 的附加字段；不需要时用下面的 RB_GENERATE。 */
#define RB_GENERATE_AUGMENT(_name, _type, _field, _cmp, _aug)                  \
	static void _name##_RB_AUGMENT(void *ptr)                              \
	{                                                                      \
		struct _type *p = ptr;                                         \
		return _aug(p);                                                \
	}                                                                      \
	RB_GENERATE_INTERNAL(_name, _type, _field, _cmp, _name##_RB_AUGMENT)

/* RB_GENERATE：普通（无增强）红黑树的生成宏。
 * 等价于以 _aug = NULL 调 RB_GENERATE_INTERNAL，产出 _name_RB_TYPE 描述符。
 * 规则：每个使用 RB_PROTOTYPE 的树，必须恰好在一个 .c 文件里写一次本宏。 */
#define RB_GENERATE(_name, _type, _field, _cmp)                                \
	RB_GENERATE_INTERNAL(_name, _type, _field, _cmp, NULL)

/* RB_* 便捷封装宏：把 _name 前缀的内联包装再包一层，调用形如 RB_INSERT(name, head, elm)。
 * 参数含义与对应的 _name_RB_ 函数一致：
 *   INIT 初始化；INSERT / REMOVE 增删；FIND 精确查找；NFIND 返回不小于键的最近节点；
 *   ROOT / EMPTY / MIN / MAX / NEXT / PREV / LEFT / RIGHT / PARENT 读取结构；
 *   SET_LEFT / SET_RIGHT / SET_PARENT 改写指针；POISON / CHECK 调试校验。
 * 注意 NFIND 返回的是第一个不小于 _key 的节点，可能并不等于 _key。 */
#define RB_INIT(_name, _head)		_name##_RB_INIT(_head)
#define RB_INSERT(_name, _head, _elm)	_name##_RB_INSERT(_head, _elm)
#define RB_REMOVE(_name, _head, _elm)	_name##_RB_REMOVE(_head, _elm)
#define RB_FIND(_name, _head, _key)	_name##_RB_FIND(_head, _key)
#define RB_NFIND(_name, _head, _key)	_name##_RB_NFIND(_head, _key)
#define RB_ROOT(_name, _head)		_name##_RB_ROOT(_head)
#define RB_EMPTY(_name, _head)		_name##_RB_EMPTY(_head)
#define RB_MIN(_name, _head)		_name##_RB_MIN(_head)
#define RB_MAX(_name, _head)		_name##_RB_MAX(_head)
#define RB_NEXT(_name, _elm)		_name##_RB_NEXT(_elm)
#define RB_PREV(_name, _elm)		_name##_RB_PREV(_elm)
#define RB_LEFT(_name, _elm)		_name##_RB_LEFT(_elm)
#define RB_RIGHT(_name, _elm)		_name##_RB_RIGHT(_elm)
#define RB_PARENT(_name, _elm)		_name##_RB_PARENT(_elm)
#define RB_SET_LEFT(_name, _elm, _l)	_name##_RB_SET_LEFT(_elm, _l)
#define RB_SET_RIGHT(_name, _elm, _r)	_name##_RB_SET_RIGHT(_elm, _r)
#define RB_SET_PARENT(_name, _elm, _p)	_name##_RB_SET_PARENT(_elm, _p)
#define RB_POISON(_name, _elm, _p)	_name##_RB_POISON(_elm, _p)
#define RB_CHECK(_name, _elm, _p)	_name##_RB_CHECK(_elm, _p)

/* RB_FOREACH：按键升序遍历红黑树。
 * 参数 _e 迭代变量（指向节点类型）、_name 树名、_head 树头。
 * 从 RB_MIN 起，反复取 RB_NEXT 直到 NULL。遍历期间不可删除当前节点。 */
#define RB_FOREACH(_e, _name, _head)                                           \
	for ((_e) = RB_MIN(_name, (_head)); (_e) != NULL;                      \
	     (_e) = RB_NEXT(_name, (_e)))

/* RB_FOREACH_SAFE：可安全删除当前节点的升序遍历。
 * 额外参数 _n 保存后继节点：循环条件里先取 RB_NEXT(_e) 存入 _n，
 * 循环体内可放心 RB_REMOVE / 释放 _e，再用 _n 继续。 */
#define RB_FOREACH_SAFE(_e, _name, _head, _n)                                  \
	for ((_e) = RB_MIN(_name, (_head));                                    \
	     (_e) != NULL && ((_n) = RB_NEXT(_name, (_e)), 1); (_e) = (_n))

/* RB_FOREACH_REVERSE：按键降序遍历红黑树（RB_MAX 起，逐次 RB_PREV）。
 * 遍历期间不可删除当前节点。 */
#define RB_FOREACH_REVERSE(_e, _name, _head)                                   \
	for ((_e) = RB_MAX(_name, (_head)); (_e) != NULL;                      \
	     (_e) = RB_PREV(_name, (_e)))

/* RB_FOREACH_REVERSE_SAFE：可安全删除当前节点的降序遍历。
 * 额外参数 _n 预先保存 RB_PREV(_e) 的结果，循环体内可删除 _e。 */
#define RB_FOREACH_REVERSE_SAFE(_e, _name, _head, _n)                          \
	for ((_e) = RB_MAX(_name, (_head));                                    \
	     (_e) != NULL && ((_n) = RB_PREV(_name, (_e)), 1); (_e) = (_n))

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TREE_H_ */
