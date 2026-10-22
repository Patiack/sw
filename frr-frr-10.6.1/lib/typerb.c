// SPDX-License-Identifier: ISC AND BSD-2-Clause
/* RB-tree */

/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 */

/*
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
 */

/*
 * 文件导读：typerb.c —— 红黑树算法实现
 *
 * 本文件实现 typerb.h 声明的红黑树操作。算法源自 OpenBSD / Niels Provos 的
 * RB-tree（BSD 许可），FRR 将其重命名并剥离类型依赖，供 typesafe 框架复用。
 *
 * 层次结构：
 *   - 一组 static inline 低级辅助函数：rbe_set / rbe_set_blackred /
 *     rbe_rotate_left / rbe_rotate_right / rbe_insert_color / rbe_remove_color，
 *     负责着色、旋转、插入后与删除后的自平衡；
 *   - rbe_remove 与各 typed_rb_* 公开函数是真正的对外实现。
 *
 * 命名约定（用宏把通用名映射到类型化名，避免与其它模块冲突）：
 *   rb_entry  映射到 typed_rb_entry，rbt_tree 映射到 typed_rb_root；
 *   RBE_LEFT / RBE_RIGHT / RBE_PARENT / RBE_COLOR 访问节点字段；
 *   RBH_ROOT 访问根字段。
 *   颜色：RB_BLACK = 0，RB_RED = 1。
 *
 * 关键点：
 *   - 所有比较经函数指针 cmpfn 完成，故实现与元素类型解耦；
 *   - typed_rb_insert 发现等键时返回已存在的节点（不插入）；
 *   - 删除后返回的旧节点内存会被 memset 清零，调用方可据此判断已摘除。
 *
 * 阅读顺序建议：先读宏与 rbe_set，再读左右旋，然后 rbe_insert_color 与
 * rbe_remove_color 理解平衡，最后看 typed_rb_* 公开接口。
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "lib/typesafe.h"

/* 红黑树两种节点颜色。用整型而非 bool，便于直接赋值与位运算。 */
#define RB_BLACK	0
#define RB_RED		1

/* 通用名到类型化名的别名，令下面的算法代码与具体类型无关。 */
#define rb_entry		typed_rb_entry
#define rbt_tree		typed_rb_root

/* 节点字段访问宏：left / right / parent / color。 */
#define RBE_LEFT(_rbe)		(_rbe)->rbt_left
#define RBE_RIGHT(_rbe)		(_rbe)->rbt_right
#define RBE_PARENT(_rbe)	(_rbe)->rbt_parent
#define RBE_COLOR(_rbe)		(_rbe)->rbt_color

/* 树根字段访问宏。 */
#define RBH_ROOT(_rbt)		(_rbt)->rbt_root

/*
 * 初始化一个新节点：设父指针、左右孩子置空、颜色先着红。
 * 新插入的节点一律先为红，再交给 rbe_insert_color 修正。
 */
static inline void rbe_set(struct rb_entry *rbe, struct rb_entry *parent)
{
	RBE_PARENT(rbe) = parent;
	RBE_LEFT(rbe) = RBE_RIGHT(rbe) = NULL;
	RBE_COLOR(rbe) = RB_RED;
}

/*
 * 把 black 节点涂黑、red 节点涂红。用于插入旋转后的重新着色。
 */
static inline void rbe_set_blackred(struct rb_entry *black,
				    struct rb_entry *red)
{
	RBE_COLOR(black) = RB_BLACK;
	RBE_COLOR(red) = RB_RED;
}

/*
 * 以 rbe 为支点左旋：rbe 的右孩子上提，rbe 变其左孩子，
 * 并维护父指针与树根指针。旋转不改变中序遍历顺序。
 */
static inline void rbe_rotate_left(struct rbt_tree *rbt, struct rb_entry *rbe)
{
	struct rb_entry *parent;
	struct rb_entry *tmp;

	tmp = RBE_RIGHT(rbe);
	RBE_RIGHT(rbe) = RBE_LEFT(tmp);
	if (RBE_RIGHT(rbe) != NULL)
		RBE_PARENT(RBE_LEFT(tmp)) = rbe;

	parent = RBE_PARENT(rbe);
	RBE_PARENT(tmp) = parent;
	if (parent != NULL) {
		if (rbe == RBE_LEFT(parent))
			RBE_LEFT(parent) = tmp;
		else
			RBE_RIGHT(parent) = tmp;
	} else
		RBH_ROOT(rbt) = tmp;

	RBE_LEFT(tmp) = rbe;
	RBE_PARENT(rbe) = tmp;
}

/*
 * 以 rbe 为支点右旋，是 rbe_rotate_left 的镜像操作。
 */
static inline void rbe_rotate_right(struct rbt_tree *rbt, struct rb_entry *rbe)
{
	struct rb_entry *parent;
	struct rb_entry *tmp;

	tmp = RBE_LEFT(rbe);
	RBE_LEFT(rbe) = RBE_RIGHT(tmp);
	if (RBE_LEFT(rbe) != NULL)
		RBE_PARENT(RBE_RIGHT(tmp)) = rbe;

	parent = RBE_PARENT(rbe);
	RBE_PARENT(tmp) = parent;
	if (parent != NULL) {
		if (rbe == RBE_LEFT(parent))
			RBE_LEFT(parent) = tmp;
		else
			RBE_RIGHT(parent) = tmp;
	} else
		RBH_ROOT(rbt) = tmp;

	RBE_RIGHT(tmp) = rbe;
	RBE_PARENT(rbe) = tmp;
}

/*
 * 插入后的自平衡（红黑树性质修复）。
 * 新节点已挂到树上且为红色，本函数边向上回溯边通过变色与旋转
 * 消除"红-红冲突"，最后强制根为黑。
 * 副作用：进入时令 count 加一（把计数并入此处，插入路径只调一次）。
 */
static inline void rbe_insert_color(struct rbt_tree *rbt, struct rb_entry *rbe)
{
	struct rb_entry *parent, *gparent, *tmp;

	rbt->count++;

	while ((parent = RBE_PARENT(rbe)) != NULL
	       && RBE_COLOR(parent) == RB_RED) {
		gparent = RBE_PARENT(parent);

		if (parent == RBE_LEFT(gparent)) {
			tmp = RBE_RIGHT(gparent);
			if (tmp != NULL && RBE_COLOR(tmp) == RB_RED) {
				RBE_COLOR(tmp) = RB_BLACK;
				rbe_set_blackred(parent, gparent);
				rbe = gparent;
				continue;
			}

			if (RBE_RIGHT(parent) == rbe) {
				rbe_rotate_left(rbt, parent);
				tmp = parent;
				parent = rbe;
				rbe = tmp;
			}

			rbe_set_blackred(parent, gparent);
			rbe_rotate_right(rbt, gparent);
		} else {
			tmp = RBE_LEFT(gparent);
			if (tmp != NULL && RBE_COLOR(tmp) == RB_RED) {
				RBE_COLOR(tmp) = RB_BLACK;
				rbe_set_blackred(parent, gparent);
				rbe = gparent;
				continue;
			}

			if (RBE_LEFT(parent) == rbe) {
				rbe_rotate_right(rbt, parent);
				tmp = parent;
				parent = rbe;
				rbe = tmp;
			}

			rbe_set_blackred(parent, gparent);
			rbe_rotate_left(rbt, gparent);
		}
	}

	RBE_COLOR(RBH_ROOT(rbt)) = RB_BLACK;
}

/*
 * 删除后的自平衡。参数 parent 是 rbe 的父节点，rbe 是替换上来的孩子
 * （可能为 NULL）。当被删节点原为黑色时，其子树黑色高度失衡，
 * 本函数沿路径向上通过变色与旋转恢复平衡——这是红黑树最繁琐的部分。
 */
static inline void rbe_remove_color(struct rbt_tree *rbt,
				    struct rb_entry *parent,
				    struct rb_entry *rbe)
{
	struct rb_entry *tmp;

	while ((rbe == NULL || RBE_COLOR(rbe) == RB_BLACK)
	       && rbe != RBH_ROOT(rbt) && parent) {
		if (RBE_LEFT(parent) == rbe) {
			tmp = RBE_RIGHT(parent);
			if (RBE_COLOR(tmp) == RB_RED) {
				rbe_set_blackred(tmp, parent);
				rbe_rotate_left(rbt, parent);
				tmp = RBE_RIGHT(parent);
			}
			if ((RBE_LEFT(tmp) == NULL
			     || RBE_COLOR(RBE_LEFT(tmp)) == RB_BLACK)
			    && (RBE_RIGHT(tmp) == NULL
				|| RBE_COLOR(RBE_RIGHT(tmp)) == RB_BLACK)) {
				RBE_COLOR(tmp) = RB_RED;
				rbe = parent;
				parent = RBE_PARENT(rbe);
			} else {
				if (RBE_RIGHT(tmp) == NULL
				    || RBE_COLOR(RBE_RIGHT(tmp)) == RB_BLACK) {
					struct rb_entry *oleft;

					oleft = RBE_LEFT(tmp);
					if (oleft != NULL)
						RBE_COLOR(oleft) = RB_BLACK;

					RBE_COLOR(tmp) = RB_RED;
					rbe_rotate_right(rbt, tmp);
					tmp = RBE_RIGHT(parent);
				}

				RBE_COLOR(tmp) = RBE_COLOR(parent);
				RBE_COLOR(parent) = RB_BLACK;
				if (RBE_RIGHT(tmp))
					RBE_COLOR(RBE_RIGHT(tmp)) = RB_BLACK;

				rbe_rotate_left(rbt, parent);
				rbe = RBH_ROOT(rbt);
				break;
			}
		} else {
			tmp = RBE_LEFT(parent);
			if (RBE_COLOR(tmp) == RB_RED) {
				rbe_set_blackred(tmp, parent);
				rbe_rotate_right(rbt, parent);
				tmp = RBE_LEFT(parent);
			}

			if ((RBE_LEFT(tmp) == NULL
			     || RBE_COLOR(RBE_LEFT(tmp)) == RB_BLACK)
			    && (RBE_RIGHT(tmp) == NULL
				|| RBE_COLOR(RBE_RIGHT(tmp)) == RB_BLACK)) {
				RBE_COLOR(tmp) = RB_RED;
				rbe = parent;
				parent = RBE_PARENT(rbe);
			} else {
				if (RBE_LEFT(tmp) == NULL
				    || RBE_COLOR(RBE_LEFT(tmp)) == RB_BLACK) {
					struct rb_entry *oright;

					oright = RBE_RIGHT(tmp);
					if (oright != NULL)
						RBE_COLOR(oright) = RB_BLACK;

					RBE_COLOR(tmp) = RB_RED;
					rbe_rotate_left(rbt, tmp);
					tmp = RBE_LEFT(parent);
				}

				RBE_COLOR(tmp) = RBE_COLOR(parent);
				RBE_COLOR(parent) = RB_BLACK;
				if (RBE_LEFT(tmp) != NULL)
					RBE_COLOR(RBE_LEFT(tmp)) = RB_BLACK;

				rbe_rotate_right(rbt, parent);
				rbe = RBH_ROOT(rbt);
				break;
			}
		}
	}

	if (rbe != NULL)
		RBE_COLOR(rbe) = RB_BLACK;
}

/*
 * 删除节点 rbe 的核心实现，返回被删节点（内存已 memset 清零）。
 * 分三种情况：无左子 / 无右子 / 左右都有（用中序后继替换）。
 * 若删除的是黑节点则调用 rbe_remove_color 修复平衡，最后 count 减一。
 * 注意：参数必须是树中真实存在的节点。
 */
static inline struct rb_entry *
rbe_remove(struct rbt_tree *rbt, struct rb_entry *rbe)
{
	struct rb_entry *child, *parent, *old = rbe;
	unsigned int color;

	if (RBE_LEFT(rbe) == NULL)
		child = RBE_RIGHT(rbe);
	else if (RBE_RIGHT(rbe) == NULL)
		child = RBE_LEFT(rbe);
	else {
		struct rb_entry *tmp;

		rbe = RBE_RIGHT(rbe);
		while ((tmp = RBE_LEFT(rbe)) != NULL)
			rbe = tmp;

		child = RBE_RIGHT(rbe);
		parent = RBE_PARENT(rbe);
		color = RBE_COLOR(rbe);
		if (child != NULL)
			RBE_PARENT(child) = parent;
		if (parent != NULL) {
			if (RBE_LEFT(parent) == rbe)
				RBE_LEFT(parent) = child;
			else
				RBE_RIGHT(parent) = child;
		} else
			RBH_ROOT(rbt) = child;
		if (RBE_PARENT(rbe) == old)
			parent = rbe;
		*rbe = *old;

		tmp = RBE_PARENT(old);
		if (tmp != NULL) {
			if (RBE_LEFT(tmp) == old)
				RBE_LEFT(tmp) = rbe;
			else
				RBE_RIGHT(tmp) = rbe;
		} else
			RBH_ROOT(rbt) = rbe;

		RBE_PARENT(RBE_LEFT(old)) = rbe;
		if (RBE_RIGHT(old))
			RBE_PARENT(RBE_RIGHT(old)) = rbe;

		goto color;
	}

	parent = RBE_PARENT(rbe);
	color = RBE_COLOR(rbe);

	if (child != NULL)
		RBE_PARENT(child) = parent;
	if (parent != NULL) {
		if (RBE_LEFT(parent) == rbe)
			RBE_LEFT(parent) = child;
		else
			RBE_RIGHT(parent) = child;
	} else
		RBH_ROOT(rbt) = child;
color:
	if (color == RB_BLACK)
		rbe_remove_color(rbt, parent, child);

	rbt->count--;
	memset(old, 0, sizeof(*old));
	return (old);
}

/* 对外删除入口，直接转调 rbe_remove。 */
struct typed_rb_entry *typed_rb_remove(struct rbt_tree *rbt,
				       struct rb_entry *rbe)
{
	return rbe_remove(rbt, rbe);
}

/*
 * 对外插入入口：自根向下按 cmpfn 查找插入位置。
 * 返回 NULL 表示插入成功；若找到等键节点则返回该节点且不插入。
 * 成功插入后调用 rbe_insert_color 着色并计数。
 */
struct typed_rb_entry *typed_rb_insert(struct rbt_tree *rbt,
		struct rb_entry *rbe, int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b))
{
	struct rb_entry *tmp;
	struct rb_entry *parent = NULL;
	int comp = 0;

	tmp = RBH_ROOT(rbt);
	while (tmp != NULL) {
		parent = tmp;

		comp = cmpfn(rbe, tmp);
		if (comp < 0)
			tmp = RBE_LEFT(tmp);
		else if (comp > 0)
			tmp = RBE_RIGHT(tmp);
		else
			return tmp;
	}

	rbe_set(rbe, parent);

	if (parent != NULL) {
		if (comp < 0)
			RBE_LEFT(parent) = rbe;
		else
			RBE_RIGHT(parent) = rbe;
	} else
		RBH_ROOT(rbt) = rbe;

	rbe_insert_color(rbt, rbe);

	return NULL;
}

/* 从根向下查找与 key 同键的节点，找到返回，否则 NULL。 */
/* Finds the node with the same key as elm */
const struct rb_entry *typed_rb_find(const struct rbt_tree *rbt,
		const struct rb_entry *key,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b))
{
	const struct rb_entry *tmp = RBH_ROOT(rbt);
	int comp;

	while (tmp != NULL) {
		comp = cmpfn(key, tmp);
		if (comp < 0)
			tmp = RBE_LEFT(tmp);
		else if (comp > 0)
			tmp = RBE_RIGHT(tmp);
		else
			return tmp;
	}

	return NULL;
}

/*
 * 查找键不小于 key 的最小节点。遍历时若 key 小于当前节点，
 * 当前节点记为候选并继续往左；否则往右。返回 best 或 NULL。
 */
const struct rb_entry *typed_rb_find_gteq(const struct rbt_tree *rbt,
		const struct rb_entry *key,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b))
{
	const struct rb_entry *tmp = RBH_ROOT(rbt), *best = NULL;
	int comp;

	while (tmp != NULL) {
		comp = cmpfn(key, tmp);
		if (comp < 0) {
			best = tmp;
			tmp = RBE_LEFT(tmp);
		} else if (comp > 0)
			tmp = RBE_RIGHT(tmp);
		else
			return tmp;
	}

	return best;
}

/*
 * 查找键严格小于 key 的最大节点。key 大于当前节点则记为候选并往右，
 * 否则往左。返回 best 或 NULL。
 */
const struct rb_entry *typed_rb_find_lt(const struct rbt_tree *rbt,
		const struct rb_entry *key,
		int (*cmpfn)(
			const struct typed_rb_entry *a,
			const struct typed_rb_entry *b))
{
	const struct rb_entry *tmp = RBH_ROOT(rbt), *best = NULL;
	int comp;

	while (tmp != NULL) {
		comp = cmpfn(key, tmp);
		if (comp <= 0)
			tmp = RBE_LEFT(tmp);
		else {
			best = tmp;
			tmp = RBE_RIGHT(tmp);
		}
	}

	return best;
}

/*
 * 求中序后继：有右子树则取右子树最左节点；
 * 否则向上找"我是其左孩子"的祖先。到达最大节点时返回 NULL。
 */
struct rb_entry *typed_rb_next(const struct rb_entry *rbe_const)
{
	struct rb_entry *rbe = (struct rb_entry *)rbe_const;

	if (RBE_RIGHT(rbe) != NULL) {
		rbe = RBE_RIGHT(rbe);
		while (RBE_LEFT(rbe) != NULL)
			rbe = RBE_LEFT(rbe);
	} else {
		if (RBE_PARENT(rbe) && (rbe == RBE_LEFT(RBE_PARENT(rbe))))
			rbe = RBE_PARENT(rbe);
		else {
			while (RBE_PARENT(rbe)
			       && (rbe == RBE_RIGHT(RBE_PARENT(rbe))))
				rbe = RBE_PARENT(rbe);
			rbe = RBE_PARENT(rbe);
		}
	}

	return rbe;
}

/*
 * 求中序前驱：有左子树则取左子树最右节点；
 * 否则向上找"我是其右孩子"的祖先。到达最小节点时返回 NULL。
 */
struct rb_entry *typed_rb_prev(const struct rb_entry *rbe_const)
{
	struct rb_entry *rbe = (struct rb_entry *)rbe_const;

	if (RBE_LEFT(rbe)) {
		rbe = RBE_LEFT(rbe);
		while (RBE_RIGHT(rbe))
			rbe = RBE_RIGHT(rbe);
	} else {
		if (RBE_PARENT(rbe) && (rbe == RBE_RIGHT(RBE_PARENT(rbe))))
			rbe = RBE_PARENT(rbe);
		else {
			while (RBE_PARENT(rbe)
			       && (rbe == RBE_LEFT(RBE_PARENT(rbe))))
				rbe = RBE_PARENT(rbe);
			rbe = RBE_PARENT(rbe);
		}
	}

	return rbe;
}

/* 沿左孩子一路到底即为最小节点；空树返回 NULL。 */
struct rb_entry *typed_rb_min(const struct rbt_tree *rbt)
{
	struct rb_entry *rbe = RBH_ROOT(rbt);
	struct rb_entry *parent = NULL;

	while (rbe != NULL) {
		parent = rbe;
		rbe = RBE_LEFT(rbe);
	}

	return parent;
}

/* 沿右孩子一路到底即为最大节点；空树返回 NULL。 */
struct rb_entry *typed_rb_max(const struct rbt_tree *rbt)
{
	struct rb_entry *rbe = RBH_ROOT(rbt);
	struct rb_entry *parent = NULL;

	while (rbe != NULL) {
		parent = rbe;
		rbe = RBE_RIGHT(rbe);
	}

	return parent;
}

/*
 * 判断节点 rbe 是否属于树 rbt：从 rbe 一路向上找根，
 * 若最终根指针等于 rbt->rbt_root 则属于。复杂度 O(树高)。
 * 注意：若 rbe 不在任何树中或树结构被破坏，行为未定义。
 */
bool typed_rb_member(const struct typed_rb_root *rbt,
		     const struct typed_rb_entry *rbe)
{
	while (rbe->rbt_parent)
		rbe = rbe->rbt_parent;
	return rbe == rbt->rbt_root;
}
