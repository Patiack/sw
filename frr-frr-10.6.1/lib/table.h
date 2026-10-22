// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Routing Table
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * ============================================================================
 * 文件导读：table.h —— 路由表（radix / 前缀树）接口
 * ============================================================================
 * 本头文件声明路由表 struct route_table 与节点 struct route_node。
 * route_node 本质是一棵按前缀比特位组织的二叉 radix 树（Patricia 风格），
 * 支持最长前缀匹配、按前缀顺序遍历，是 FRR 各协议装路由的通用容器。
 *
 * 核心数据结构：
 *   - struct route_node   节点：p 是前缀，link[2] 是左右孩子，lock 是引用计数
 *   - struct route_table  表：top 是根，hash 是并行哈希（加速精确匹配），
 *                         delegate 负责节点创建 / 销毁，count 统计节点数
 *   - route_table_iter_t  按前缀顺序遍历的迭代器（可暂停 / 继续）
 *
 * 关键约定：
 *   - 用 route_lock_node / route_unlock_node 管理节点引用计数；减到 0 自动删除
 *   - delegate 机制允许客户端自定义节点内存管理（如带额外字段的派生节点）
 *   - 内部字段（link / parent / lock）外部不应直接访问，见 table_internal 宏
 *
 * 与其他模块的关系：
 *   - 依赖 prefix.h 的 struct prefix；依赖 hash.h；依赖 typesafe.h 的 rn_hash_node
 *   - 常被 zebra / 各路由协议用作 RIB 存储
 *
 * 阅读顺序：
 *   struct route_node -> struct route_table -> route_table_delegate_t
 *   -> 迭代器 -> route_node_get / lookup / match -> lock / unlock 内联函数
 * ============================================================================
 */
#ifndef _ZEBRA_TABLE_H
#define _ZEBRA_TABLE_H

#include "memory.h"
#include "hash.h"
#include "prefix.h"
#include "typesafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 声明 route_node 的内存类型，供 memory.h 记账使用。 */
DECLARE_MTYPE(ROUTE_NODE);

/*
 * 前向声明：结构体的完整定义在下方或 table.c 中给出。
 */
/*
 * Forward declarations.
 */
struct route_node;
struct route_table;

/*
 * route_table_delegate_t：路由表“委托”函数向量。
 * 客户端可提供自定义的 create_node / destroy_node，从而为节点附加私有字段
 * 或改用不同的内存池。默认委托见 table.c 的 route_table_get_default_delegate。
 */
/*
 * route_table_delegate_t
 *
 * Function vector that can be used by a client to customize the
 * behavior of one or more route tables.
 */
typedef const struct route_table_delegate_t_ route_table_delegate_t;

/*
 * 函数指针：创建路由节点。委托负责分配并返回一个新的 route_node。
 */
typedef struct route_node *(*route_table_create_node_func_t)(
	route_table_delegate_t *, struct route_table *);

/*
 * 函数指针：销毁路由节点。委托负责释放节点的全部内存。
 */
typedef void (*route_table_destroy_node_func_t)(route_table_delegate_t *,
						struct route_table *,
						struct route_node *);

/*
 * 委托结构体：把节点的“创建 / 销毁”策略从表逻辑中解耦，
 * 便于不同客户端复用同一套 radix 代码但使用各自的内存管理。
 */
struct route_table_delegate_t_ {
	route_table_create_node_func_t create_node;
	route_table_destroy_node_func_t destroy_node;
};

/*
 * 声明一个 typesafe 哈希的头部类型 rn_hash_node。
 * 路由表用它做“前缀到节点”的精确匹配加速；其实现由 tablesafe.h 展开。
 */
PREDECL_HASH(rn_hash_node);

/*
 * 路由表顶层结构。
 *   top      —— radix 树根节点
 *   hash     —— 并行哈希表，加速精确匹配（前缀 -> 节点）
 *   delegate —— 节点创建 / 销毁委托
 *   cleanup  —— 节点删除时的回调（可选）
 *   count    —— 节点总数
 *   info     —— 使用者的私有数据
 */
/* Routing table top structure. */
struct route_table {
	struct route_node *top;
	struct rn_hash_node_head hash;

	/*
	 * Delegate that performs certain functions for this table.
	 */
	route_table_delegate_t *delegate;
	void (*cleanup)(struct route_table *, struct route_node *);

	unsigned long count;

	/*
	 * User data.
	 */
	void *info;
};

/*
 * 访问控制：table_rdonly / table_internal 用于把“本应只读”的节点字段
 * 在非 table.c 的编译单元里标记为 const（甚至 deprecated），以便检出越权访问。
 *   - table_rdonly  ：仅提示只读
 *   - table_internal：额外加 deprecated 属性，越界访问时编译告警
 * 在 table.c 内部（FRR_COMPILING_TABLE_C）两宏都展开为原样。
 */
/*
 * node->link is really internal to the table code and should not be
 * accessed by outside code.  We don't have any writers (yay), though some
 * readers are left to be fixed.
 *
 * rationale: we need to add a hash table in parallel, to speed up
 * exact-match lookups.
 *
 * same really applies for node->parent, though that's less of an issue.
 * table->link should be - and is - NEVER written by outside code
 */
#ifdef FRR_COMPILING_TABLE_C
#define table_rdonly(x)		x
#define table_internal(x)	x
#else
#define table_rdonly(x)		const x
#define table_internal(x)                                                      \
	const x __attribute__(                                                 \
		(deprecated("this should only be accessed by lib/table.c")))
/* table_internal is for node->link and node->lock, once we have done
 * something about remaining accesses */
#endif

/*
 * 注意：C 的 const 并不保证“只读”，编译器可能据此优化；
 * 因此这些访问控制只在开发模式（DEV_BUILD）下启用，发布版本会去掉 const。
 */
/* so... the problem with this is that "const" doesn't mean "readonly".
 * It in fact may allow the compiler to optimize based on the assumption
 * that the value doesn't change.  Hence, since the only purpose of this
 * is to aid in development, don't put the "const" in release builds.
 *
 * (I haven't seen this actually break, but GCC and LLVM are getting ever
 * more aggressive in optimizing...)
 */
#ifndef DEV_BUILD
#undef table_rdonly
#define table_rdonly(x) x
#endif

/*
 * 路由节点字段宏：集中定义 route_node 的全部字段，供派生节点结构体复用
 * （派生结构体把本宏放在自身字段之前，即可获得同样的前缀 / 树链 / 锁字段）。
 * 各字段含义见下方 struct route_node 注释。
 */
/*
 * Macro that defines all fields in a route node.
 */
#define ROUTE_NODE_FIELDS                                                      \
	/* Actual prefix of this radix. */                                     \
	struct prefix p;                                                       \
                                                                               \
	/* Tree link. */                                                       \
	struct route_table *table_rdonly(table);                               \
	struct route_node *table_rdonly(parent);                               \
	struct route_node *table_rdonly(link[2]);                              \
                                                                               \
	/* Lock of this radix */                                               \
	unsigned int table_rdonly(lock);                                       \
                                                                               \
	struct rn_hash_node_item nodehash;                                     \
	/* Each node of route. */                                              \
	void *info;                                                            \


/*
 * 单条路由条目（radix 树节点）。
 *   p         —— 该节点代表的前缀（含地址族 / 前缀长度）
 *   table     —— 所属路由表
 *   parent    —— 父节点
 *   link[2]   —— 左 / 右孩子（宏 l_left / l_right 分别指代 link[0] / link[1]）
 *   lock      —— 引用计数，减到 0 时节点被自动删除
 *   nodehash  —— 用于并行哈希表的挂链项
 *   info      —— 使用者私有数据（如 RIB 条目）
 */
/* Each routing entry. */
struct route_node {
	ROUTE_NODE_FIELDS

/*
 * 便捷宏：link[0] 为左子树，link[1] 为右子树。
 */
#define l_left   link[0]
#define l_right  link[1]
};

/* 前向声明：路由表迭代器结构体的别名。 */
typedef struct route_table_iter_t_ route_table_iter_t;

/*
 * 迭代器状态机：INIT（未开始）-> ITERATING（遍历中）-> PAUSED（暂停）
 * -> DONE（结束）。route_table_iter_next 依据当前状态决定下一步取哪个节点。
 */
typedef enum {
	RT_ITER_STATE_INIT,
	RT_ITER_STATE_ITERATING,
	RT_ITER_STATE_PAUSED,
	RT_ITER_STATE_DONE
} route_table_iter_state_t;

/*
 * route_table_iter_t
 *
 * Structure that holds state for iterating over a route table.
 */
/*
 * 路由表迭代器：持有遍历状态。
 *   state        —— 当前状态（见 route_table_iter_state_t）
 *   table        —— 被遍历的表（调用者须保证其生命周期长于迭代器）
 *   current      —— 当前所在节点
 *   pause_prefix —— 暂停时记录的上一个已处理前缀，用于恢复遍历
 */
struct route_table_iter_t_ {

	route_table_iter_state_t state;

	/*
	 * Routing table that we are iterating over. The caller must ensure
	 * that that table outlives the iterator.
	 */
	struct route_table *table;

	/*
	 * The node that the iterator is currently on.
	 */
	struct route_node *current;

	/*
	 * The last prefix that the iterator processed before it was paused.
	 */
	struct prefix pause_prefix;
};

/* 函数原型声明。 */
/* Prototypes. */
/*
 * route_table_init：用默认委托创建一张路由表。
 * 返回：新路由表指针。
 */
extern struct route_table *route_table_init(void);

/*
 * route_table_init_with_delegate：用指定委托创建路由表（自定义节点内存管理）。
 */
extern struct route_table *
route_table_init_with_delegate(route_table_delegate_t *delegate);

/*
 * route_table_get_default_delegate：返回内置默认委托的指针。
 */
extern route_table_delegate_t *route_table_get_default_delegate(void);

/*
 * route_table_get_info：读取路由表的私有数据指针。
 */
static inline void *route_table_get_info(struct route_table *table)
{
	return table->info;
}

/*
 * route_table_set_info：设置路由表的私有数据指针。
 */
static inline void route_table_set_info(struct route_table *table, void *d)
{
	table->info = d;
}

/*
 * route_table_finish：销毁整张路由表并释放其全部节点。
 */
extern void route_table_finish(struct route_table *table);
/*
 * route_top：返回树中最左的节点，即前缀序遍历的起点。
 */
extern struct route_node *route_top(struct route_table *table);
/*
 * route_next：按前缀顺序返回下一个节点，可用作简单的前序遍历循环。
 * 返回 NULL 表示遍历结束。
 */
extern struct route_node *route_next(struct route_node *node);
/*
 * route_next_until：同 route_next，但遍历到 limit 节点处停止。
 */
extern struct route_node *route_next_until(struct route_node *node,
					   const struct route_node *limit);
/*
 * route_node_get：查找节点，不存在则创建并插入（“取或建”）。
 * 参数：pu 是前缀的只读联合指针。
 * 返回：命中的或新建的节点（新建后引用计数为 1）。
 */
extern struct route_node *route_node_get(struct route_table *table,
					 union prefixconstptr pu);
/*
 * route_node_lookup：精确查找前缀对应的节点，不创建。
 * 返回：命中节点；未命中返回 NULL。
 */
extern struct route_node *route_node_lookup(struct route_table *table,
					    union prefixconstptr pu);
/*
 * route_node_lookup_maynull：同 route_node_lookup，但允许前缀指针为 NULL 的查询。
 */
extern struct route_node *route_node_lookup_maynull(struct route_table *table,
						    union prefixconstptr pu);
/*
 * route_node_match：最长前缀匹配（LPM），返回覆盖 pu 的最具体节点。
 * 返回：匹配到的节点；无匹配返回 NULL。
 */
extern struct route_node *route_node_match(struct route_table *table,
					   union prefixconstptr pu);

/*
 * route_table_count：返回表中节点总数。
 */
extern unsigned long route_table_count(struct route_table *table);

/*
 * route_node_create：用委托创建一个节点（默认实现会清零并设置 table / p）。
 */
extern struct route_node *route_node_create(route_table_delegate_t *delegate,
					    struct route_table *table);
/*
 * route_node_delete：删除节点（从树中摘除并释放）。通常由 unlock 到 0 间接触发。
 */
extern void route_node_delete(struct route_node *node);
/*
 * route_node_destroy：仅释放节点内存，交由委托处理。
 */
extern void route_node_destroy(route_table_delegate_t *delegate,
			       struct route_table *table,
			       struct route_node *node);

/*
 * route_table_get_next：返回严格大于 pu 的下一个前缀所对应的节点，用于迭代器恢复。
 */
extern struct route_node *route_table_get_next(struct route_table *table,
					       union prefixconstptr pu);
/*
 * route_table_prefix_iter_cmp：比较两个前缀在“迭代顺序”上的先后。
 * 返回：负数 / 0 / 正数，含义同 strcmp。
 */
extern int route_table_prefix_iter_cmp(const struct prefix *p1,
				       const struct prefix *p2);

/*
 * 迭代器 API 总览：
 *   iter_init    —— 初始化迭代器并绑定到表（此时尚未取第一个节点）
 *   iter_pause   —— 暂停，记录当前位置以便之后恢复
 *   iter_cleanup —— 结束迭代并释放内部资源
 */
/*
 * Iterator functions.
 */
/*
 * route_table_iter_init：初始化迭代器：状态置为 INIT 并绑定目标表（不取节点）。
 */
extern void route_table_iter_init(route_table_iter_t *iter,
				  struct route_table *table);
/*
 * route_table_iter_pause：暂停迭代，把当前前缀记入 pause_prefix；之后 iter_next 会自动恢复。
 */
extern void route_table_iter_pause(route_table_iter_t *iter);
/*
 * route_table_iter_cleanup：结束迭代：清理迭代器内部状态（状态置为 DONE）。
 */
extern void route_table_iter_cleanup(route_table_iter_t *iter);

/*
 * Inline functions.
 */

/* Lock node. */
/*
 * route_lock_node：给节点加一次引用（lock++），并返回该节点。
 * 用途：持有节点期间防止其被自动删除。
 */
static inline struct route_node *route_lock_node(struct route_node *node)
{
	(*(unsigned *)&node->lock)++;
	return node;
}

/* Unlock node. */
/*
 * route_unlock_node：解除一次引用（lock--）；当计数降为 0 时自动删除节点。
 * 坑：引用计数必须成对，否则会提前删除节点或造成内存泄漏。
 */
static inline void route_unlock_node(struct route_node *node)
{
	assert(node->lock > 0);
	(*(unsigned *)&node->lock)--;

	if (node->lock == 0)
		route_node_delete(node);
}

/*
 * route_node_get_lock_count：返回节点当前引用计数（调试 / 断言用）。
 */
static inline unsigned int route_node_get_lock_count(struct route_node *node)
{
	return node->lock;
}

/*
 * route_table_iter_next：按当前状态推进迭代器，返回下一个节点。
 *   INIT      -> 从 route_top 取第一个节点
 *   ITERATING -> 取 current 的下一个
 *   PAUSED    -> 从 pause_prefix 之后恢复
 *   DONE      -> 返回 NULL
 * 取到节点后状态转为 ITERATING，取不到则转 DONE。
 */
/*
 * route_table_iter_next
 *
 * Get the next node in the tree.
 */
static inline struct route_node *route_table_iter_next(route_table_iter_t *iter)
{
	struct route_node *node;

	switch (iter->state) {

	case RT_ITER_STATE_INIT:

		/*
		 * We're just starting the iteration.
		 */
		node = route_top(iter->table);
		break;

	case RT_ITER_STATE_ITERATING:
		node = route_next(iter->current);
		break;

	case RT_ITER_STATE_PAUSED:

		/*
		 * Start with the node following pause_prefix.
		 */
		node = route_table_get_next(iter->table, &iter->pause_prefix);
		break;

	case RT_ITER_STATE_DONE:
		return NULL;

	default:
		/* Suppress uninitialized variable warning */
		node = NULL;
		assert(0);
	}

	iter->current = node;
	if (node)
		iter->state = RT_ITER_STATE_ITERATING;
	else
		iter->state = RT_ITER_STATE_DONE;

	return node;
}

/*
 * route_table_iter_is_done：迭代已结束（状态为 DONE）时返回真。
 */
/*
 * route_table_iter_is_done
 *
 * Returns true if the iteration is complete.
 */
static inline int route_table_iter_is_done(route_table_iter_t *iter)
{
	return iter->state == RT_ITER_STATE_DONE;
}

/*
 * route_table_iter_started：迭代已开始（状态不再是 INIT）时返回真。
 */
/*
 * route_table_iter_started
 *
 * Returns true if this iterator has started iterating over the tree.
 */
static inline int route_table_iter_started(route_table_iter_t *iter)
{
	return iter->state != RT_ITER_STATE_INIT;
}

#ifdef _FRR_ATTRIBUTE_PRINTFRR
/*
 * 注册 printfrr 扩展：支持用 %pRN 直接格式化 struct route_node（打印其前缀）。
 */
#pragma FRR printfrr_ext "%pRN"  (struct route_node *)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_TABLE_H */
