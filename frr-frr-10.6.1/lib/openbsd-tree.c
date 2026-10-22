// SPDX-License-Identifier: ISC AND BSD-2-Clause
/*	$OpenBSD: subr_tree.c,v 1.9 2017/06/08 03:30:52 dlg Exp $ */

/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 */

/*
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <lib/openbsd-tree.h>

/*
 * openbsd-tree.c —— 从 OpenBSD 移植的侵入式红黑树（RB tree）实现。
 *
 * 本文件做什么：
 *   实现配套头文件 lib/openbsd-tree.h 中声明的红黑树核心算法。头文件里的
 *   SPLAY 树是纯宏实现，RB 树的查找 / 插入 / 删除 / 遍历等真正逻辑都在本 .c
 *   里，经 m4 宏 RB_GENERATE / RB_PROTOTYPE 生成的包装函数被各模块调用。
 *
 * 核心数据结构（均定义在 openbsd-tree.h）：
 *   struct rb_entry  内嵌进用户结构体的红黑树节点，含 rbt_parent /
 *                    rbt_left / rbt_right 三个指针与 rbt_color 颜色位。
 *   struct rb_type   树的“类型描述符”：t_compare 比较函数、t_augment 可选
 *                    增强函数、t_offset（内嵌节点相对用户结构体起始地址的
 *                    字节偏移）。
 *   struct rbt_tree  树对象本身，只有一个 rbt_root 根指针。
 *
 * 侵入式（intrusive）的关键：
 *   rb_n2e 用“用户节点地址 + t_offset”求出内嵌 rb_entry 地址；rb_e2n 反向用
 *   “rb_entry 地址 - t_offset”还原用户节点地址。所有对外接口都以用户结构体
 *   指针（void *）为参数，内部换算成 rb_entry 再操作。
 *
 * 与其他模块的关系：
 *   bgpd / ospfd 以及 lib 里的许多有序集合都用 openbsd-tree.h 的 RB_HEAD /
 *   RB_PROTOTYPE / RB_GENERATE 宏声明自己的树类型，最终调用到本文件导出的
 *   _rb_insert / _rb_remove / _rb_find 等函数。
 *
 * 阅读顺序建议：
 *   1) rb_n2e / rb_e2n 的地址换算；2) RBE_* 宏与 rbe_set 系列辅助函数；
 *   3) rbe_rotate_left / rbe_rotate_right 旋转；4) rbe_insert_color 插入修复；
 *   5) rbe_remove_color 删除修复；6) rbe_remove 删除主体；
 *   7) 对外 _rb_* 接口；8) _rb_poison / _rb_check 调试用途。
 */

/*
 * rb_n2e —— 由用户结构体指针求出其内嵌红黑树节点 rb_entry 的地址。
 *   t    : 类型描述符，提供 t_offset（rb_entry 相对用户结构体的字节偏移）。
 *   node : 用户结构体（数据节点）指针。
 *   返回 : 指向内嵌 rb_entry 的指针。把地址当整数加上偏移做指针运算，
 *          这是侵入式容器借助偏移换算节点地址的标准手法。
 */
static inline struct rb_entry *rb_n2e(const struct rb_type *t, void *node)
{
	unsigned long addr = (unsigned long)node;

	return ((struct rb_entry *)(addr + t->t_offset));
}

/*
 * rb_e2n —— rb_n2e 的逆运算：由内嵌 rb_entry 地址还原出用户结构体指针。
 *   t   : 类型描述符，提供 t_offset。
 *   rbe : 内嵌 rb_entry 指针。
 *   返回 : 对应的用户结构体指针。凡是要把树内部节点交还给调用者，
 *          都必须经本函数完成 rb_entry -> 用户节点的换算。
 */
static inline void *rb_e2n(const struct rb_type *t, struct rb_entry *rbe)
{
	unsigned long addr = (unsigned long)rbe;

	return ((void *)(addr - t->t_offset));
}

/*
 * RBE_LEFT / RBE_RIGHT / RBE_PARENT / RBE_COLOR —— 访问内嵌 rb_entry 四个字段
 *   的便捷宏：左子、右子、父节点、颜色。_rbe 须为指向 struct rb_entry 的
 *   表达式；这些宏既可当右值读取，也可当左值直接赋值。
 */
#define RBE_LEFT(_rbe)		(_rbe)->rbt_left
#define RBE_RIGHT(_rbe)		(_rbe)->rbt_right
#define RBE_PARENT(_rbe)	(_rbe)->rbt_parent
#define RBE_COLOR(_rbe)		(_rbe)->rbt_color

/*
 * RBH_ROOT —— 取红黑树对象 rbt 的根节点指针（struct rbt_tree.rbt_root）。
 *   既能当右值读取，也能当左值在旋转 / 插入 / 删除时改写树根。
 */
#define RBH_ROOT(_rbt)		(_rbt)->rbt_root

/*
 * rbe_set —— 把一个内嵌节点初始化为“孤立红节点”，并挂到指定父节点下。
 *   rbe    : 待初始化的 rb_entry。
 *   parent : 其父节点（可能为 NULL，表示该节点将成为树根）。
 *   副作用 : 清空左右孩子、颜色置为 RB_RED、父指针指向 parent。
 *   注意   : 这里只写好 parent 指针，尚未把它接进父节点的 left / right 槽，
 *            也不做颜色修复；调用者随后需自行挂链并调用 rbe_insert_color。
 */
static inline void rbe_set(struct rb_entry *rbe, struct rb_entry *parent)
{
	RBE_PARENT(rbe) = parent;
	RBE_LEFT(rbe) = RBE_RIGHT(rbe) = NULL;
	RBE_COLOR(rbe) = RB_RED;
}

/*
 * rbe_set_blackred —— 一次性把 black 节点染黑、red 节点染红。
 *   插入 / 删除修复中反复出现的“父黑子红”配色组合，避免写两行赋值。
 *   无返回值；直接修改两个节点的 rbt_color。
 */
static inline void rbe_set_blackred(struct rb_entry *black,
				    struct rb_entry *red)
{
	RBE_COLOR(black) = RB_BLACK;
	RBE_COLOR(red) = RB_RED;
}

/*
 * rbe_augment —— 对节点调用其类型自定义的增强函数 t_augment。
 *   用于维护“增强红黑树”里节点额外维护的聚合信息（例如子树大小、区间端点）。
 *   rbe : 内嵌节点；内部会先 rb_e2n 还原成用户节点再回调 t_augment。
 *   前提 : 调用前须确认 t->t_augment 非空（见 rbe_if_augment）。
 */
static inline void rbe_augment(const struct rb_type *t, struct rb_entry *rbe)
{
	(*t->t_augment)(rb_e2n(t, rbe));
}

/*
 * rbe_if_augment —— rbe_augment 的安全包装：仅当类型定义了 t_augment 时才回调。
 *   普通非增强树 t_augment 为 NULL，用本函数可省去每处判空，是树代码的统一
 *   入口。无返回值 / 无副作用（除回调本身）。
 */
static inline void rbe_if_augment(const struct rb_type *t, struct rb_entry *rbe)
{
	if (t->t_augment != NULL)
		rbe_augment(t, rbe);
}

/*
 * rbe_rotate_left —— 以 rbe 为轴做左旋（右子 tmp 上升为新的子树根）。
 *   t / rbt : 类型描述符与树对象。
 *   rbe     : 旋转轴（下沉的那个节点）。
 *   副作用  : 重挂 tmp、rbe 及其父指针；若 rbe 原是根则更新 RBH_ROOT。
 *             若定义了 t_augment，还会自下而上重算 rbe、tmp 及新父的聚合值。
 *   易踩坑  : 旋转不改变中序顺序，只调形状；augment 必须按“被破坏的子树”
 *             顺序重算，否则聚合信息会错。
 */
static inline void rbe_rotate_left(const struct rb_type *t,
				   struct rbt_tree *rbt, struct rb_entry *rbe)
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

	if (t->t_augment != NULL) {
		rbe_augment(t, rbe);
		rbe_augment(t, tmp);
		parent = RBE_PARENT(tmp);
		if (parent != NULL)
			rbe_augment(t, parent);
	}
}

/*
 * rbe_rotate_right —— 以 rbe 为轴做右旋（左子 tmp 上升为新的子树根），
 *   是 rbe_rotate_left 的镜像操作，字段左右互换。
 *   副作用与易踩坑同 rbe_rotate_left：重挂指针、必要时更新树根、
 *   按被破坏子树顺序重算 augment。
 */
static inline void rbe_rotate_right(const struct rb_type *t,
				    struct rbt_tree *rbt, struct rb_entry *rbe)
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

	if (t->t_augment != NULL) {
		rbe_augment(t, rbe);
		rbe_augment(t, tmp);
		parent = RBE_PARENT(tmp);
		if (parent != NULL)
			rbe_augment(t, parent);
	}
}

/*
 * rbe_insert_color —— 插入后的红黑性质修复（红黑树插入 rebalance）。
 *   新插入的节点 rbe 已在 rbe_set 里被染红；若其父也为红则违反“红节点不能有
 *   红子”的性质，需循环修复：
 *     - 叔叔为红：父与叔涂黑、祖父涂红，把当前点提升到祖父继续上溯；
 *     - 叔叔为黑且当前点是父的内侧子：先旋转成外侧（左-左 / 右-右）；
 *     - 叔叔为黑且当前点是外侧子：父涂黑、祖父涂红并绕祖父旋转，修复结束。
 *   循环结束后强制把根染黑，保证根为黑的红黑树性质。
 *   无返回值；依赖 rbe_rotate_left / rbe_rotate_right 完成旋转。
 */
static inline void rbe_insert_color(const struct rb_type *t,
				    struct rbt_tree *rbt, struct rb_entry *rbe)
{
	struct rb_entry *parent, *gparent, *tmp;

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
				rbe_rotate_left(t, rbt, parent);
				tmp = parent;
				parent = rbe;
				rbe = tmp;
			}

			rbe_set_blackred(parent, gparent);
			rbe_rotate_right(t, rbt, gparent);
		} else {
			tmp = RBE_LEFT(gparent);
			if (tmp != NULL && RBE_COLOR(tmp) == RB_RED) {
				RBE_COLOR(tmp) = RB_BLACK;
				rbe_set_blackred(parent, gparent);
				rbe = gparent;
				continue;
			}

			if (RBE_LEFT(parent) == rbe) {
				rbe_rotate_right(t, rbt, parent);
				tmp = parent;
				parent = rbe;
				rbe = tmp;
			}

			rbe_set_blackred(parent, gparent);
			rbe_rotate_left(t, rbt, gparent);
		}
	}

	RBE_COLOR(RBH_ROOT(rbt)) = RB_BLACK;
}

/*
 * rbe_remove_color —— 删除后的红黑性质修复（红黑树删除 rebalance）。
 *   parent / rbe : 被删除节点的父节点，以及顶替上来的孩子（可能为 NULL）。
 *   当“顶替节点为黑”时会丢失一个黑高，需循环向兄弟借黑：
 *     - 兄弟为红：染色并旋转，转化为兄弟为黑的情形；
 *     - 兄弟为黑且其两子皆黑：兄弟染红、上溯到父继续；
 *     - 兄弟为黑且远端子为黑：先做内侧旋转调整，再执行最终染色 + 旋转，
 *       把黑高补平后结束。
 *   循环退出后若顶替节点非空则强制染黑。注意入参 parent 可能为 NULL
 *   （原节点即根），循环条件里用 parent 兜底，防止空指针解引用。
 */
static inline void rbe_remove_color(const struct rb_type *t,
				    struct rbt_tree *rbt,
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
				rbe_rotate_left(t, rbt, parent);
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
					rbe_rotate_right(t, rbt, tmp);
					tmp = RBE_RIGHT(parent);
				}

				RBE_COLOR(tmp) = RBE_COLOR(parent);
				RBE_COLOR(parent) = RB_BLACK;
				if (RBE_RIGHT(tmp))
					RBE_COLOR(RBE_RIGHT(tmp)) = RB_BLACK;

				rbe_rotate_left(t, rbt, parent);
				rbe = RBH_ROOT(rbt);
				break;
			}
		} else {
			tmp = RBE_LEFT(parent);
			if (RBE_COLOR(tmp) == RB_RED) {
				rbe_set_blackred(tmp, parent);
				rbe_rotate_right(t, rbt, parent);
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
					rbe_rotate_left(t, rbt, tmp);
					tmp = RBE_LEFT(parent);
				}

				RBE_COLOR(tmp) = RBE_COLOR(parent);
				RBE_COLOR(parent) = RB_BLACK;
				if (RBE_LEFT(tmp) != NULL)
					RBE_COLOR(RBE_LEFT(tmp)) = RB_BLACK;

				rbe_rotate_right(t, rbt, parent);
				rbe = RBH_ROOT(rbt);
				break;
			}
		}
	}

	if (rbe != NULL)
		RBE_COLOR(rbe) = RB_BLACK;
}

static inline struct rb_entry *
/*
 * rbe_remove —— 从树中摘除指定节点，返回“真正被从树上移除”的那个节点的
 *   rb_entry 指针（目标有两个孩子时会用中序后继替换它，返回值可能与入参不同）。
 *   算法分三种情况：
 *     - 至多一个孩子：用唯一孩子顶替，直接剪掉本节点；
 *     - 两个孩子：沿右子树一路向左找到中序后继 rbe，让后继顶替老节点 old，
 *       再完成一次结构替换与 augment 重算，然后 goto color 统一做颜色修复。
 *   收尾：若被摘除节点原为黑，调用 rbe_remove_color 修复红黑性质。
 *   调用者须自行确保该节点确实在树中。
 */
rbe_remove(const struct rb_type *t, struct rbt_tree *rbt, struct rb_entry *rbe)
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

			rbe_if_augment(t, parent);
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

			rbe_if_augment(t, tmp);
		} else
			RBH_ROOT(rbt) = rbe;

		RBE_PARENT(RBE_LEFT(old)) = rbe;
		if (RBE_RIGHT(old))
			RBE_PARENT(RBE_RIGHT(old)) = rbe;

		if (t->t_augment != NULL && parent != NULL) {
			tmp = parent;
			do {
				rbe_augment(t, tmp);
				tmp = RBE_PARENT(tmp);
			} while (tmp != NULL);
		}

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

		rbe_if_augment(t, parent);
	} else
		RBH_ROOT(rbt) = child;
color:
	if (color == RB_BLACK)
		rbe_remove_color(t, rbt, parent, child);

	return (old);
}

/*
 * _rb_remove —— 对外删除接口：按用户节点 elm 找到内嵌节点并摘除。
 *   t / rbt : 类型描述符与树对象；elm 为用户结构体指针。
 *   返回    : 若确有节点被移除则返回被移除的用户节点指针，否则 NULL；
 *             当目标有两子时返回的是后继节点（与 elm 可能不同）。
 *   典型用法 : 先 _rb_find 拿到节点，再传进来删除。
 */
void *_rb_remove(const struct rb_type *t, struct rbt_tree *rbt, void *elm)
{
	struct rb_entry *rbe = rb_n2e(t, elm);
	struct rb_entry *old;

	old = rbe_remove(t, rbt, rbe);

	return (old == NULL ? NULL : rb_e2n(t, old));
}

/*
 * _rb_insert —— 对外插入接口：把 elm 插入树中。
 *   t / rbt : 类型描述符与树对象；elm 为用户结构体指针。
 *   返回    : 成功插入返回 NULL；若找到键相等的既有节点，则不插入并返回那个
 *             已存在用户节点指针（供调用者判断“重复键”）。
 *   流程    : 自根向下按 t_compare 定位叶子父节点 -> rbe_set 初始化 ->
 *             挂到父的 left / right -> rbe_insert_color 修复红黑性质。
 *   易踩坑  : 比较函数 t_compare 的第一个参数恒为“待查找 / 待插入”的 key，
 *             第二个才是树中节点；写反会导致查找方向相反。
 */
void *_rb_insert(const struct rb_type *t, struct rbt_tree *rbt, void *elm)
{
	struct rb_entry *rbe = rb_n2e(t, elm);
	struct rb_entry *tmp;
	struct rb_entry *parent = NULL;
	void *node;
	int comp = 0;

	tmp = RBH_ROOT(rbt);
	while (tmp != NULL) {
		parent = tmp;

		node = rb_e2n(t, tmp);
		comp = (*t->t_compare)(elm, node);
		if (comp < 0)
			tmp = RBE_LEFT(tmp);
		else if (comp > 0)
			tmp = RBE_RIGHT(tmp);
		else
			return (node);
	}

	rbe_set(rbe, parent);

	if (parent != NULL) {
		if (comp < 0)
			RBE_LEFT(parent) = rbe;
		else
			RBE_RIGHT(parent) = rbe;

		rbe_if_augment(t, parent);
	} else
		RBH_ROOT(rbt) = rbe;

	rbe_insert_color(t, rbt, rbe);

	return NULL;
}

/*
 * _rb_find —— 精确查找键等于 key 的节点。
 *   返回匹配的用户节点指针；无匹配返回 NULL。
 *   注意 key 的类型须与 t_compare 第一参数约定的类型一致（常为“键结构”）。
 */
/* Finds the node with the same key as elm */
void *_rb_find(const struct rb_type *t, const struct rbt_tree *rbt,
	       const void *key)
{
	struct rb_entry *tmp = RBH_ROOT(rbt);
	void *node;
	int comp;

	while (tmp != NULL) {
		node = rb_e2n(t, tmp);
		comp = (*t->t_compare)(key, node);
		if (comp < 0)
			tmp = RBE_LEFT(tmp);
		else if (comp > 0)
			tmp = RBE_RIGHT(tmp);
		else
			return (node);
	}

	return NULL;
}

/*
 * _rb_nfind —— 查找第一个键不小于 key 的节点（下界 / 后继式查找）。
 *   沿树下行时只要 key 小于当前节点就记下 res 并继续向左，试图找更接近的；
 *   命中相等立即返回。返回第一个大于等于 key 的用户节点指针，无则 NULL。
 *   用途：范围查询 / 求最小不小于某键的元素。
 */
/* Finds the first node greater than or equal to the search key */
void *_rb_nfind(const struct rb_type *t, const struct rbt_tree *rbt,
		const void *key)
{
	struct rb_entry *tmp = RBH_ROOT(rbt);
	void *node;
	void *res = NULL;
	int comp;

	while (tmp != NULL) {
		node = rb_e2n(t, tmp);
		comp = (*t->t_compare)(key, node);
		if (comp < 0) {
			res = node;
			tmp = RBE_LEFT(tmp);
		} else if (comp > 0)
			tmp = RBE_RIGHT(tmp);
		else
			return (node);
	}

	return (res);
}

/*
 * _rb_next —— 求给定节点在中序序列中的后继。
 *   elm  : 用户节点指针（必须已在树中）。
 *   返回 : 后继用户节点指针；已是最大元素则返回 NULL。
 *   逻辑 : 有右子则取右子树最左节点；否则向上回溯到“自己是父的左子”的祖辈。
 */
void *_rb_next(const struct rb_type *t, void *elm)
{
	struct rb_entry *rbe = rb_n2e(t, elm);

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

	return (rbe == NULL ? NULL : rb_e2n(t, rbe));
}

/*
 * _rb_prev —— 求给定节点在中序序列中的前驱，是 _rb_next 的镜像。
 *   elm  : 用户节点指针（必须已在树中）。
 *   返回 : 前驱用户节点指针；已是最小元素则返回 NULL。
 *   逻辑 : 有左子则取左子树最右节点；否则向上回溯到“自己是父的右子”的祖辈。
 */
void *_rb_prev(const struct rb_type *t, void *elm)
{
	struct rb_entry *rbe = rb_n2e(t, elm);

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

	return (rbe == NULL ? NULL : rb_e2n(t, rbe));
}

/*
 * _rb_root —— 返回树的根节点（用户结构体指针）；空树返回 NULL。
 */
void *_rb_root(const struct rb_type *t, const struct rbt_tree *rbt)
{
	struct rb_entry *rbe = RBH_ROOT(rbt);

	return (rbe == NULL ? rbe : rb_e2n(t, rbe));
}

/*
 * _rb_min —— 返回树中的最小元素（最左节点）用户指针；空树返回 NULL。
 */
void *_rb_min(const struct rb_type *t, const struct rbt_tree *rbt)
{
	struct rb_entry *rbe = RBH_ROOT(rbt);
	struct rb_entry *parent = NULL;

	while (rbe != NULL) {
		parent = rbe;
		rbe = RBE_LEFT(rbe);
	}

	return (parent == NULL ? NULL : rb_e2n(t, parent));
}

/*
 * _rb_max —— 返回树中的最大元素（最右节点）用户指针；空树返回 NULL。
 */
void *_rb_max(const struct rb_type *t, const struct rbt_tree *rbt)
{
	struct rb_entry *rbe = RBH_ROOT(rbt);
	struct rb_entry *parent = NULL;

	while (rbe != NULL) {
		parent = rbe;
		rbe = RBE_RIGHT(rbe);
	}

	return (parent == NULL ? NULL : rb_e2n(t, parent));
}

/*
 * _rb_left —— 返回 node 的左孩子用户指针；无左孩子返回 NULL。
 */
void *_rb_left(const struct rb_type *t, void *node)
{
	struct rb_entry *rbe = rb_n2e(t, node);
	rbe = RBE_LEFT(rbe);
	return (rbe == NULL ? NULL : rb_e2n(t, rbe));
}

/*
 * _rb_right —— 返回 node 的右孩子用户指针；无右孩子返回 NULL。
 */
void *_rb_right(const struct rb_type *t, void *node)
{
	struct rb_entry *rbe = rb_n2e(t, node);
	rbe = RBE_RIGHT(rbe);
	return (rbe == NULL ? NULL : rb_e2n(t, rbe));
}

/*
 * _rb_parent —— 返回 node 的父节点用户指针；node 为根时返回 NULL。
 */
void *_rb_parent(const struct rb_type *t, void *node)
{
	struct rb_entry *rbe = rb_n2e(t, node);
	rbe = RBE_PARENT(rbe);
	return (rbe == NULL ? NULL : rb_e2n(t, rbe));
}

/*
 * _rb_set_left —— 直接把 node 的左孩子指针设置为 left（不做平衡 / 校验）。
 *   left 为 NULL 表示清空左孩子。仅供树构建类代码使用，误用会破坏红黑性质。
 */
void _rb_set_left(const struct rb_type *t, void *node, void *left)
{
	struct rb_entry *rbe = rb_n2e(t, node);
	struct rb_entry *rbl = (left == NULL) ? NULL : rb_n2e(t, left);

	RBE_LEFT(rbe) = rbl;
}

/*
 * _rb_set_right —— 直接把 node 的右孩子指针设置为 right（不做平衡 / 校验）。
 *   right 为 NULL 表示清空右孩子。仅供树构建类代码使用，误用会破坏红黑性质。
 */
void _rb_set_right(const struct rb_type *t, void *node, void *right)
{
	struct rb_entry *rbe = rb_n2e(t, node);
	struct rb_entry *rbr = (right == NULL) ? NULL : rb_n2e(t, right);

	RBE_RIGHT(rbe) = rbr;
}

/*
 * _rb_set_parent —— 直接把 node 的父指针设置为 parent（不做平衡 / 校验）。
 *   parent 为 NULL 表示 node 将成为根。仅供树构建类代码使用。
 */
void _rb_set_parent(const struct rb_type *t, void *node, void *parent)
{
	struct rb_entry *rbe = rb_n2e(t, node);
	struct rb_entry *rbp = (parent == NULL) ? NULL : rb_n2e(t, parent);

	RBE_PARENT(rbe) = rbp;
}

/*
 * _rb_poison —— 调试用：把节点的三个指针字段全部写成 poison 特征值。
 *   用于标记“已删除 / 未初始化”的节点，配合 _rb_check 检测悬垂指针误用。
 *   poison 通常取一个不可能成为合法地址的常量（如 0xdeadbeef）。
 */
void _rb_poison(const struct rb_type *t, void *node, unsigned long poison)
{
	struct rb_entry *rbe = rb_n2e(t, node);

	RBE_PARENT(rbe) = RBE_LEFT(rbe) = RBE_RIGHT(rbe) =
		(struct rb_entry *)poison;
}

/*
 * _rb_check —— 调试用：检查节点是否仍处于 _rb_poison 写下的“毒化”状态。
 *   返回 1 表示父 / 左 / 右三个指针都等于 poison（即尚未被重新初始化），
 *   否则返回 0。用于断言节点尚未被使用或已被正确清除。
 */
int _rb_check(const struct rb_type *t, void *node, unsigned long poison)
{
	struct rb_entry *rbe = rb_n2e(t, node);

	return ((unsigned long)RBE_PARENT(rbe) == poison
		&& (unsigned long)RBE_LEFT(rbe) == poison
		&& (unsigned long)RBE_RIGHT(rbe) == poison);
}
