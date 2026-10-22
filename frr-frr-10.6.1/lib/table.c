// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Routing Table functions.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * ============================================================================
 * 文件导读：table.c —— 路由表（radix / 前缀树）实现
 * ============================================================================
 * 本文件实现 table.h 声明的路由表：一棵按前缀比特位组织的前缀树（radix tree），
 * 并额外维护一张哈希表 rn_hash_node 以加速“精确前缀”查找。
 *
 * 树的形态：
 *   - 每个节点持有一个前缀 p，左右子树 link[0] / link[1] 由 p 之后的下一比特决定
 *   - 查找 / 插入时沿比特逐层下降；分叉处可能插入“公共前缀”节点（route_common）
 *   - 节点带引用计数 lock，降为 0 时自动删除并回收；父节点若成为空壳也会被删除
 *
 * 两个加速点：
 *   - 精确匹配走哈希 rn_hash_node（prefix_cmp 比较，prefix_hash_key 取键）
 *   - 最长前缀匹配 route_node_match 仍沿树下降
 *
 * 关键内部函数：
 *   route_common     求两前缀的最长公共前缀
 *   set_link         按比特把子节点挂到父节点
 *   route_node_get   取或建节点（含公共前缀节点的插入）
 *   route_table_free 迭代式整树释放（避免递归爆栈）
 *
 * 与其他模块的关系：
 *   - 依赖 prefix.h 做前缀比较 / 掩码 / 取比特；依赖 typesafe.h 的 DECLARE_HASH
 *   - delegate（table.h）负责节点内存管理；cleanup 回调在节点释放前调用
 *
 * 阅读顺序：
 *   route_common / set_link -> route_node_get -> route_node_match / lookup
 *   -> route_node_delete -> route_top / route_next -> 迭代器三函数
 * ============================================================================
 */
#define FRR_COMPILING_TABLE_C

#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "memory.h"
#include "sockunion.h"
#include "libfrr_trace.h"

DEFINE_MTYPE_STATIC(LIB, ROUTE_TABLE, "Route table");
DEFINE_MTYPE(LIB, ROUTE_NODE, "Route node");

static void route_table_free(struct route_table *);

/*
 * 哈希相等比较函数：直接比较前缀（prefix_cmp），返回 0 表示相等。
 * 供并行哈希 rn_hash_node 在哈希冲突时判定两项是否相同。
 */
static int route_table_hash_cmp(const struct route_node *a,
				const struct route_node *b)
{
	return prefix_cmp(&a->p, &b->p);
}

/*
 * 展开 typesafe 哈希 rn_hash_node 的头 / 项类型与操作函数
 * （初始化 / 查找 / 增 / 删 / 结束），节点上的挂链项是字段 nodehash。
 */
DECLARE_HASH(rn_hash_node, struct route_node, nodehash, route_table_hash_cmp,
	     prefix_hash_key);
/*
 * route_table_init_with_delegate：分配并初始化一张路由表。
 * 参数：delegate 节点创建 / 销毁委托（不能为空）。
 * 返回：新表指针；同时初始化内部并行哈希以及 count / top 等字段。
 */
/*
 * route_table_init_with_delegate
 */
struct route_table *
route_table_init_with_delegate(route_table_delegate_t *delegate)
{
	struct route_table *rt;

	rt = XCALLOC(MTYPE_ROUTE_TABLE, sizeof(struct route_table));
	rt->delegate = delegate;
	rn_hash_node_init(&rt->hash);
	return rt;
}

/*
 * route_table_finish：对外接口，销毁整张表（内部转调 route_table_free）。
 */
void route_table_finish(struct route_table *rt)
{
	route_table_free(rt);
}

/*
 * route_node_new：通过委托分配一个（尚未设置前缀的）新节点。
 */
/* Allocate new route node. */
static struct route_node *route_node_new(struct route_table *table)
{
	return table->delegate->create_node(table->delegate, table);
}

/*
 * route_node_set：分配新节点并设置前缀、所属表，同时加入并行哈希。
 * 返回：新节点（引用计数为 0，需调用者加锁）。
 */
/* Allocate new route node with prefix set. */
static struct route_node *route_node_set(struct route_table *table,
					 const struct prefix *prefix)
{
	struct route_node *node;

	node = route_node_new(table);

	prefix_copy(&node->p, prefix);
	node->table = table;

	rn_hash_node_add(&node->table->hash, node);

	return node;
}

/*
 * route_node_free：释放节点：先调用表级 cleanup 回调，再交给委托 destroy_node。
 * 注意：cleanup 可能间接释放整张表（见 route_node_delete 中的脆弱代码警告）。
 */
/* Free route node. */
static void route_node_free(struct route_table *table, struct route_node *node)
{
	if (table->cleanup)
		table->cleanup(table, node);
	table->delegate->destroy_node(table->delegate, table, node);
}

/*
 * route_table_free：释放整张路由表及其全部节点。
 * 采用迭代（非递归）后序遍历：反复下降到叶子再回收，避免深树递归爆栈。
 * 每个节点先从哈希摘下再 route_node_free；最后断言 count 已归零。
 */
/* Free route table. */
static void route_table_free(struct route_table *rt)
{
	struct route_node *tmp_node;
	struct route_node *node;

	if (rt == NULL)
		return;

	node = rt->top;

	/* Bulk deletion of nodes remaining in this table.  This function is not
	   called until workers have completed their dependency on this table.
	   A final route_unlock_node() will not be called for these nodes. */
	while (node) {
		if (node->l_left) {
			node = node->l_left;
			continue;
		}

		if (node->l_right) {
			node = node->l_right;
			continue;
		}

		tmp_node = node;
		node = node->parent;

		tmp_node->table->count--;
		tmp_node->lock =
			0; /* to cause assert if unlocked after this */
		rn_hash_node_del(&rt->hash, tmp_node);
		route_node_free(rt, tmp_node);

		if (node != NULL) {
			if (node->l_left == tmp_node)
				node->l_left = NULL;
			else
				node->l_right = NULL;
		} else {
			break;
		}
	}

	assert(rt->count == 0);

	rn_hash_node_fini(&rt->hash);
	XFREE(MTYPE_ROUTE_TABLE, rt);
	return;
}

/*
 * 掩码查表：maskbit[n] 给出“前 n 个比特为 1”的字节掩码，用于把非前缀位清零。
 */
/* Utility mask array. */
static const uint8_t maskbit[] = {0x00, 0x80, 0xc0, 0xe0, 0xf0,
				  0xf8, 0xfc, 0xfe, 0xff};

/*
 * route_common：计算两个前缀 n 与 p 的最长公共前缀，写入 new。
 * 参数：n 为树中已有节点前缀，p 为目标前缀，new 为输出。
 * 特例：FLOWSPEC 家族直接返回 p（不做逐位比较）。
 * 副作用：仅写 new，不改动 n / p。
 */
/* Common prefix route genaration. */
static void route_common(const struct prefix *n, const struct prefix *p,
			 struct prefix *new)
{
	int i;
	uint8_t diff;
	uint8_t mask;
	const uint8_t *np;
	const uint8_t *pp;
	uint8_t *newp;

	if (n->family == AF_FLOWSPEC)
		return prefix_copy(new, p);
	np = (const uint8_t *)&n->u.prefix;
	pp = (const uint8_t *)&p->u.prefix;

	newp = &new->u.prefix;

	for (i = 0; i < p->prefixlen / 8; i++) {
		if (np[i] == pp[i])
			newp[i] = np[i];
		else
			break;
	}

	new->prefixlen = i * 8;

	if (new->prefixlen != p->prefixlen) {
		diff = np[i] ^ pp[i];
		mask = 0x80;
		while (new->prefixlen < p->prefixlen && !(mask & diff)) {
			mask >>= 1;
			new->prefixlen++;
		}
		newp[i] = np[i] & maskbit[new->prefixlen % 8];
	}
}

/*
 * set_link：把子节点 new 挂到父节点 node 下。
 * 方向由 new 前缀在 node->prefixlen 位置上的比特决定（link[bit]），并回填 parent。
 */
static void set_link(struct route_node *node, struct route_node *new)
{
	unsigned int bit = prefix_bit(&new->p.u.prefix, node->p.prefixlen);

	node->link[bit] = new;
	new->parent = node;
}

/*
 * route_node_match：最长前缀匹配（LPM）。
 * 沿树下降，记录沿途“有 info 的最深节点”作为匹配结果。
 * 返回：匹配节点（已加锁）；无匹配返回 NULL。
 * 坑：只有 info 非 NULL 的节点才算“有效路由”。
 */
/* Find matched prefix. */
struct route_node *route_node_match(struct route_table *table,
				    union prefixconstptr pu)
{
	const struct prefix *p = pu.p;
	struct route_node *node;
	struct route_node *matched;

	matched = NULL;
	node = table->top;

	/* Walk down tree.  If there is matched route then store it to
	   matched. */
	while (node && node->p.prefixlen <= p->prefixlen
	       && prefix_match(&node->p, p)) {
		if (node->info)
			matched = node;

		if (node->p.prefixlen == p->prefixlen)
			break;

		node = node->link[prefix_bit(&p->u.prefix, node->p.prefixlen)];
	}

	/* If matched route found, return it. */
	if (matched)
		return route_lock_node(matched);

	return NULL;
}

/*
 * route_node_lookup：精确查找前缀对应的节点。
 * 先把查询前缀 apply_mask 归一化，再查并行哈希。
 * 返回：命中且 info 非 NULL 的节点（已加锁）；否则 NULL。
 */
/* Lookup same prefix node.  Return NULL when we can't find route. */
struct route_node *route_node_lookup(struct route_table *table,
				     union prefixconstptr pu)
{
	struct route_node rn, *node;
	prefix_copy(&rn.p, pu.p);
	apply_mask(&rn.p);

	node = rn_hash_node_find(&table->hash, &rn);
	return (node && node->info) ? route_lock_node(node) : NULL;
}

/*
 * route_node_lookup_maynull：同 route_node_lookup，但不要求 info 非 NULL
 * （即允许返回“占位 / 待填充”的节点）。
 * 返回：命中节点（已加锁）；否则 NULL。
 */
/* Lookup same prefix node.  Return NULL when we can't find route. */
struct route_node *route_node_lookup_maynull(struct route_table *table,
					     union prefixconstptr pu)
{
	struct route_node rn, *node;
	prefix_copy(&rn.p, pu.p);
	apply_mask(&rn.p);

	node = rn_hash_node_find(&table->hash, &rn);
	return node ? route_lock_node(node) : NULL;
}

/*
 * route_node_get：取或建节点（插入路由的核心）。
 * 流程：先查哈希（命中且 info 非空则加锁返回）；否则沿树下降直到分叉点，
 *       必要时插入“公共前缀”节点并重新挂链，最终为新前缀建节点。
 * 返回：目标前缀节点（已加锁）。
 * 副作用：可能新增 1~2 个节点并更新 table->count；FLOWSPEC 时会释放 p。
 */
/* Add node to routing table. */
struct route_node *route_node_get(struct route_table *table,
				  union prefixconstptr pu)
{
	if (frrtrace_enabled(frr_libfrr, route_node_get)) {
		char buf[PREFIX2STR_BUFFER];
		prefix2str(pu, buf, sizeof(buf));
		frrtrace(2, frr_libfrr, route_node_get, table, buf);
	}

	struct route_node search;
	struct prefix *p = &search.p;

	prefix_copy(p, pu.p);
	apply_mask(p);

	struct route_node *new;
	struct route_node *node;
	struct route_node *match;
	uint16_t prefixlen = p->prefixlen;
	const uint8_t *prefix = &p->u.prefix;

	node = rn_hash_node_find(&table->hash, &search);
	if (node && node->info) {
		if (p->family == AF_FLOWSPEC)
			prefix_flowspec_ptr_free(p);
		return route_lock_node(node);
	}

	match = NULL;
	node = table->top;
	while (node && node->p.prefixlen <= prefixlen
	       && prefix_match(&node->p, p)) {
		if (node->p.prefixlen == prefixlen) {
			if (p->family == AF_FLOWSPEC)
				prefix_flowspec_ptr_free(p);

			return route_lock_node(node);
		}
		match = node;
		node = node->link[prefix_bit(prefix, node->p.prefixlen)];
	}

	if (node == NULL) {
		new = route_node_set(table, p);
		if (match)
			set_link(match, new);
		else
			table->top = new;
	} else {
		new = route_node_new(table);
		route_common(&node->p, p, &new->p);
		new->p.family = p->family;
		new->table = table;
		set_link(new, node);
		rn_hash_node_add(&table->hash, new);

		if (match)
			set_link(match, new);
		else
			table->top = new;

		if (new->p.prefixlen != p->prefixlen) {
			match = new;
			new = route_node_set(table, p);
			set_link(match, new);
			table->count++;
		}
	}
	table->count++;
	route_lock_node(new);

	if (p->family == AF_FLOWSPEC)
		prefix_flowspec_ptr_free(p);

	return new;
}

/*
 * route_node_delete：从树和哈希中删除节点。
 * 前提：node->lock 必须为 0；若同时有左右孩子则直接返回（不可删）。
 * 把唯一孩子上提替代自己，更新 count，摘哈希，释放自己；
 * 若父节点变成无引用空壳，则递归删除父节点。
 * 脆弱点：route_node_free 可能顺带释放整张表，见函数内英文 WARNING。
 */
/* Delete node from the routing table. */
void route_node_delete(struct route_node *node)
{
	struct route_node *child;
	struct route_node *parent;

	assert(node->lock == 0);

	if (node->l_left && node->l_right)
		return;

	if (node->l_left)
		child = node->l_left;
	else
		child = node->l_right;

	parent = node->parent;

	if (child)
		child->parent = parent;

	if (parent) {
		if (parent->l_left == node)
			parent->l_left = child;
		else
			parent->l_right = child;
	} else
		node->table->top = child;

	node->table->count--;

	rn_hash_node_del(&node->table->hash, node);

	/* WARNING: FRAGILE CODE!
	 * route_node_free may have the side effect of free'ing the entire
	 * table.
	 * this is permitted only if table->count got decremented to zero above,
	 * because in that case parent will also be NULL, so that we won't try
	 * to
	 * delete a now-stale parent below.
	 *
	 * cf. srcdest_srcnode_destroy() in zebra/zebra_rib.c */

	route_node_free(node->table, node);

	/* If parent node is stub then delete it also. */
	if (parent && parent->lock == 0)
		route_node_delete(parent);
}

/*
 * route_top：返回树中最左节点（前缀序遍历起点）并加锁；空表返回 NULL。
 */
/* Get first node and lock it.  This function is useful when one wants
   to lookup all the node exist in the routing table. */
struct route_node *route_top(struct route_table *table)
{
	/* If there is no node in the routing table return NULL. */
	if (table->top == NULL)
		return NULL;

	/* Lock the top node and return it. */
	route_lock_node(table->top);
	return table->top;
}

/*
 * route_next：前序遍历取下一个节点：先解锁当前 node，再给下一个加锁返回。
 * 顺序：左孩子 -> 右孩子 -> 回溯找“尚未访问的右兄弟”。
 * 返回：下一个节点；遍历结束返回 NULL。
 * 坑：会释放对当前节点的引用，故须先保存 next 指针。
 */
/* Unlock current node and lock next node then return it. */
struct route_node *route_next(struct route_node *node)
{
	struct route_node *next;
	struct route_node *start;

	/* Node may be deleted from route_unlock_node so we have to preserve
	   next node's pointer. */

	if (node->l_left) {
		next = node->l_left;
		route_lock_node(next);
		route_unlock_node(node);
		return next;
	}
	if (node->l_right) {
		next = node->l_right;
		route_lock_node(next);
		route_unlock_node(node);
		return next;
	}

	start = node;
	while (node->parent) {
		if (node->parent->l_left == node && node->parent->l_right) {
			next = node->parent->l_right;
			route_lock_node(next);
			route_unlock_node(start);
			return next;
		}
		node = node->parent;
	}
	route_unlock_node(start);
	return NULL;
}

/*
 * route_next_until：同 route_next，但回溯到 limit 节点即停止（用于限定子树范围）。
 */
/* Unlock current node and lock next node until limit. */
struct route_node *route_next_until(struct route_node *node,
				    const struct route_node *limit)
{
	struct route_node *next;
	struct route_node *start;

	/* Node may be deleted from route_unlock_node so we have to preserve
	   next node's pointer. */

	if (node->l_left) {
		next = node->l_left;
		route_lock_node(next);
		route_unlock_node(node);
		return next;
	}
	if (node->l_right) {
		next = node->l_right;
		route_lock_node(next);
		route_unlock_node(node);
		return next;
	}

	start = node;
	while (node->parent && node != limit) {
		if (node->parent->l_left == node && node->parent->l_right) {
			next = node->parent->l_right;
			route_lock_node(next);
			route_unlock_node(start);
			return next;
		}
		node = node->parent;
	}
	route_unlock_node(start);
	return NULL;
}

/*
 * route_table_count：返回表中节点总数。
 */
unsigned long route_table_count(struct route_table *table)
{
	return table->count;
}

/*
 * route_node_create：默认委托的节点创建函数：清零分配一个 route_node。
 * 返回：新节点指针。
 */
/**
 * route_node_create
 *
 * Default function for creating a route node.
 */
struct route_node *route_node_create(route_table_delegate_t *delegate,
				     struct route_table *table)
{
	struct route_node *node;
	node = XCALLOC(MTYPE_ROUTE_NODE, sizeof(struct route_node));
	return node;
}

/*
 * route_node_destroy：默认委托的节点销毁函数：释放节点内存。
 */
/**
 * route_node_destroy
 *
 * Default function for destroying a route node.
 */
void route_node_destroy(route_table_delegate_t *delegate,
			struct route_table *table, struct route_node *node)
{
	XFREE(MTYPE_ROUTE_NODE, node);
}

/*
 * 默认委托：使用 route_node_create / route_node_destroy 做节点的默认内存管理。
 */
/*
 * Default delegate.
 */
static route_table_delegate_t default_delegate = {
	.create_node = route_node_create,
	.destroy_node = route_node_destroy};

/*
 * route_table_get_default_delegate：返回默认委托的指针。
 */
route_table_delegate_t *route_table_get_default_delegate(void)
{
	return &default_delegate;
}

/*
 * route_table_init：用默认委托创建路由表。
 * 返回：新表指针。
 */
/*
 * route_table_init
 */
struct route_table *route_table_init(void)
{
	return route_table_init_with_delegate(&default_delegate);
}

/*
 * route_table_prefix_iter_cmp：按“迭代顺序”比较两个前缀，体现前缀树的排序语义。
 * 返回：p1 在 p2 之前为 -1，相等为 0，之后为 +1（与 strcmp 同向）。
 * 语义：若一个前缀包含另一个，则短者在前；否则比较公共前缀后的第一个比特。
 */
/**
 * route_table_prefix_iter_cmp
 *
 * Compare two prefixes according to the order in which they appear in
 * an iteration over a tree.
 *
 * @return -1 if p1 occurs before p2 (p1 < p2)
 *          0 if the prefixes are identical (p1 == p2)
 *         +1 if p1 occurs after p2 (p1 > p2)
 */
int route_table_prefix_iter_cmp(const struct prefix *p1,
				const struct prefix *p2)
{
	struct prefix common_space;
	struct prefix *common = &common_space;

	if (p1->prefixlen <= p2->prefixlen) {
		if (prefix_match(p1, p2)) {

			/*
			 * p1 contains p2, or is equal to it.
			 */
			return (p1->prefixlen == p2->prefixlen) ? 0 : -1;
		}
	} else {

		/*
		 * Check if p2 contains p1.
		 */
		if (prefix_match(p2, p1))
			return 1;
	}

	route_common(p1, p2, common);
	assert(common->prefixlen < p1->prefixlen);
	assert(common->prefixlen < p2->prefixlen);

	/*
	 * Both prefixes are longer than the common prefix.
	 *
	 * We need to check the bit after the common prefixlen to determine
	 * which one comes later.
	 */
	if (prefix_bit(&p1->u.prefix, common->prefixlen)) {

		/*
		 * We branch to the right to get to p1 from the common prefix.
		 */
		assert(!prefix_bit(&p2->u.prefix, common->prefixlen));
		return 1;
	}

	/*
	 * We branch to the right to get to p2 from the common prefix.
	 */
	assert(prefix_bit(&p2->u.prefix, common->prefixlen));
	return -1;
}

/*
 * route_get_subtree_next：返回“node 子树之后”的第一个节点（迭代顺序）。
 * 做法：沿父链上溯，找到第一个拥有右兄弟的祖先，返回其右兄弟；无则 NULL。
 */
/*
 * route_get_subtree_next
 *
 * Helper function that returns the first node that follows the nodes
 * in the sub-tree under 'node' in iteration order.
 */
static struct route_node *route_get_subtree_next(struct route_node *node)
{
	while (node->parent) {
		if (node->parent->l_left == node && node->parent->l_right)
			return node->parent->l_right;

		node = node->parent;
	}

	return NULL;
}

/*
 * route_table_get_next_internal：找出迭代顺序中“严格大于前缀 p”的第一个节点。
 * 沿树下降，区分四种情形：命中等长、p 在子树内、子树在 p 之前、互不包含。
 * 返回：目标节点（不加锁，由调用者处理）；无则 NULL。
 */
/**
 * route_table_get_next_internal
 *
 * Helper function to find the node that occurs after the given prefix in
 * order of iteration.
 *
 * @see route_table_get_next
 */
static struct route_node *
route_table_get_next_internal(struct route_table *table,
			      const struct prefix *p)
{
	struct route_node *node, *tmp_node;
	int cmp;

	node = table->top;

	while (node) {
		int match;

		if (node->p.prefixlen < p->prefixlen)
			match = prefix_match(&node->p, p);
		else
			match = prefix_match(p, &node->p);

		if (match) {
			if (node->p.prefixlen == p->prefixlen) {

				/*
				 * The prefix p exists in the tree, just return
				 * the next
				 * node.
				 */
				route_lock_node(node);
				node = route_next(node);
				if (node)
					route_unlock_node(node);

				return (node);
			}

			if (node->p.prefixlen > p->prefixlen) {

				/*
				 * Node is in the subtree of p, and hence
				 * greater than p.
				 */
				return node;
			}

			/*
			 * p is in the sub-tree under node.
			 */
			tmp_node = node->link[prefix_bit(&p->u.prefix,
							 node->p.prefixlen)];

			if (tmp_node) {
				node = tmp_node;
				continue;
			}

			/*
			 * There are no nodes in the direction where p should
			 * be. If
			 * node has a right child, then it must be greater than
			 * p.
			 */
			if (node->l_right)
				return node->l_right;

			/*
			 * No more children to follow, go upwards looking for
			 * the next
			 * node.
			 */
			return route_get_subtree_next(node);
		}

		/*
		 * Neither node prefix nor 'p' contains the other.
		 */
		cmp = route_table_prefix_iter_cmp(&node->p, p);
		if (cmp > 0) {

			/*
			 * Node follows p in iteration order. Return it.
			 */
			return node;
		}

		assert(cmp < 0);

		/*
		 * Node and the subtree under it come before prefix p in
		 * iteration order. Prefix p and its sub-tree are not present in
		 * the tree. Go upwards and find the first node that follows the
		 * subtree. That node will also succeed p.
		 */
		return route_get_subtree_next(node);
	}

	return NULL;
}

/*
 * route_table_get_next：对外接口：返回严格大于 pu 的下一个节点并给其加锁。
 * 内部转调 route_table_get_next_internal 并断言顺序正确。
 */
/**
 * route_table_get_next
 *
 * Find the node that occurs after the given prefix in order of
 * iteration.
 */
struct route_node *route_table_get_next(struct route_table *table,
					union prefixconstptr pu)
{
	const struct prefix *p = pu.p;
	struct route_node *node;

	node = route_table_get_next_internal(table, p);
	if (node) {
		assert(route_table_prefix_iter_cmp(&node->p, p) > 0);
		route_lock_node(node);
	}
	return node;
}

/*
 * route_table_iter_init：初始化迭代器：清零、状态置 INIT、绑定目标表。
 */
/*
 * route_table_iter_init
 */
void route_table_iter_init(route_table_iter_t *iter, struct route_table *table)
{
	memset(iter, 0, sizeof(*iter));
	iter->state = RT_ITER_STATE_INIT;
	iter->table = table;
}

/*
 * route_table_iter_pause：暂停迭代：记录当前前缀到 pause_prefix，解锁当前节点。
 * 之后再次调用 iter_next 会从该前缀之后恢复；仅在 ITERATING 状态有效。
 */
/*
 * route_table_iter_pause
 *
 * Pause an iteration over the table. This allows the iteration to be
 * resumed point after arbitrary additions/deletions from the table.
 * An iteration can be resumed by just calling route_table_iter_next()
 * on the iterator.
 */
void route_table_iter_pause(route_table_iter_t *iter)
{
	switch (iter->state) {

	case RT_ITER_STATE_INIT:
	case RT_ITER_STATE_PAUSED:
	case RT_ITER_STATE_DONE:
		return;

	case RT_ITER_STATE_ITERATING:

		/*
		 * Save the prefix that we are currently at. The next call to
		 * route_table_iter_next() will return the node after this
		 * prefix
		 * in the tree.
		 */
		prefix_copy(&iter->pause_prefix, &iter->current->p);
		route_unlock_node(iter->current);
		iter->current = NULL;
		iter->state = RT_ITER_STATE_PAUSED;
		return;

	default:
		assert(0);
	}
}

/*
 * route_table_iter_cleanup：释放迭代器持有的资源（解锁当前节点并把状态置 DONE）。
 */
/*
 * route_table_iter_cleanup
 *
 * Release any resources held by the iterator.
 */
void route_table_iter_cleanup(route_table_iter_t *iter)
{
	if (iter->state == RT_ITER_STATE_ITERATING) {
		route_unlock_node(iter->current);
		iter->current = NULL;
	}
	assert(!iter->current);

	/*
	 * Set the state to RT_ITER_STATE_DONE to make any
	 * route_table_iter_next() calls on this iterator return NULL.
	 */
	iter->state = RT_ITER_STATE_DONE;
}
