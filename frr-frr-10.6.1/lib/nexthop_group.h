// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nexthop Group structure definition.
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *                    Donald Sharp
 */

/*
 * 文件导读：nexthop_group.h —— “下一跳组”（nexthop group）的公共定义。
 *
 * 一、什么是 nexthop group
 *   一条去往某前缀的路由，其等价多路径（ECMP）由多个 nexthop 组成；把这些
 *   nexthop 按规范排序（canonical sort order）串成一条链表，就是 nexthop
 *   group。分组的意义在于：可以整体比较、哈希、复制与去重，并在转发表里
 *   作为一组等价出口同时下发。
 *
 * 二、规范排序（canonical sort order）
 *   为让“同一组成员”无论以何种顺序插入都得到相同结果，成员必须按固定顺序
 *   排列。nexthop_group_add_sorted / nexthop_group_copy_nh_sorted 都强制这一
 *   顺序。哈希与相等判定都建立在“已排序”这一前提上。
 *
 * 三、哈希与相等
 *   nexthop_group_hash 对整组求哈希（供哈希表去重）；nexthop_group_equal
 *   判定两组是否相等。注意这些函数都假设两组均已按规范顺序排列。
 *
 * 四、命名分组与回调（nexthop_group_cmd）
 *   除路由临时持有的匿名组外，FRR 还维护“命名 nexthop group”，用 RB 树按
 *   名字检索；通过 nexthop_group_init 注册 create / modify / add / del /
 *   destroy 回调，供上层感知变化。nexthop_group_cmd 内嵌一个 nexthop_group。
 *
 * 五、迭代宏
 *   ALL_NEXTHOPS / ALL_NEXTHOPS_PTR 用于同时遍历顶层链与递归解析链，是这段
 *   代码里最常用的遍历方式。
 */

#ifndef __NEXTHOP_GROUP__
#define __NEXTHOP_GROUP__

#include <vty.h>

#ifdef __cplusplus
extern "C" {
#endif

/* nexthop 组的“弹性（resilience）”参数，主要供 EVPN 多归属等场景调节
 * 哈希桶数量与失衡检测定时器：
 *   buckets             哈希桶数；
 *   idle_timer          空闲时长阈值（毫秒 / 秒，视使用处）；
 *   unbalanced_timer    判定流量失衡的时间阈值；
 *   unbalanced_time     记录最近一次失衡时刻。
 */
struct nhg_resilience {
	uint16_t buckets;
	uint32_t idle_timer;
	uint32_t unbalanced_timer;
	uint64_t unbalanced_time;
};

/* 一个 nexthop group：nexthop 为已按规范顺序排列的成员链表头，
 * nhgr 为该组的弹性参数。链表可能同时包含顶层成员与递归解析出的成员，
 * 遍历请使用 ALL_NEXTHOPS 宏。
 */
/*
 * What is a nexthop group?
 *
 * A nexthop group is a collection of nexthops that make up
 * the ECMP path for the route.
 *
 * This module provides a proper abstraction to this idea.
 */
struct nexthop_group {
	struct nexthop *nexthop;

	struct nhg_resilience nhgr;
};

/* 分配并初始化一个新的空 nexthop group（堆分配，失败返回 NULL）。 */
struct nexthop_group *nexthop_group_new(void);
/* 删除整组并释放其成员；参数为“指针的指针”，释放后会把 *nhg 置空，
 * 避免悬垂指针。
 */
void nexthop_group_delete(struct nexthop_group **nhg);

/* 深拷贝：把 from 的成员（含递归信息）复制到已存在的 to 中，to 原有成员
 * 会被先清空。不做内存分配失败检查于调用者，失败返回时 to 可能不完整。
 */
void nexthop_group_copy(struct nexthop_group *to,
			const struct nexthop_group *from);

/* 把一条 nexthop 链表 nh 复制进 nhg，并在复制过程中强制按规范顺序排列。 */
/*
 * Copy a list of nexthops in 'nh' to an nhg, enforcing canonical sort order
 */
void nexthop_group_copy_nh_sorted(struct nexthop_group *nhg,
				  const struct nexthop *nh);

/* 把链表 nh（含递归）复制到 *tnh 指向的链表尾部；rparent 用作拷贝出的
 * 递归父指针。不排序。
 */
void copy_nexthops(struct nexthop **tnh, const struct nexthop *nh,
		   struct nexthop *rparent);

/* 对整组求哈希，供哈希表 / 去重使用；_no_recurse 版本忽略递归解析链，
 * 只对顶层成员求哈希。两者都假设组已按规范顺序排列。
 */
uint32_t nexthop_group_hash_no_recurse(const struct nexthop_group *nhg);
uint32_t nexthop_group_hash(const struct nexthop_group *nhg);
/* 在组内标记重复成员（给重复者打 NEXTHOP_FLAG_DUPLICATE），用于避免把
 * 等价的多个出口重复下发。会修改 nhg 内成员的 flags。
 */
void nexthop_group_mark_duplicates(struct nexthop_group *nhg);

/* 把 nexthop 按规范顺序插入 nhg 链表中的正确位置（不是简单追加）。 */
/* Add a nexthop to a list, enforcing the canonical sort order. */
void nexthop_group_add_sorted(struct nexthop_group *nhg,
			      struct nexthop *nexthop);

/* 迭代宏：ALL_NEXTHOPS 的 head 是“结构体”（如 route 里的 nexthop_group），
 * ALL_NEXTHOPS_PTR 的 head 是“指针”。它们借助 nexthop_next() 自动下钻到
 * 递归解析链，因此能一次遍历顶层与解析出的全部 nexthop。用法：
 *   struct nexthop *nhop;
 *   ALL_NEXTHOPS(route->nexthop_group, nhop) { ... }
 * 注意宏展开为 for 的三个子句，不能作为普通表达式使用。
 */
/* The following for loop allows to iterate over the nexthop
 * structure of routes.
 *
 * head:      The pointer to the first nexthop in the chain.
 *
 * nexthop:   The pointer to the current nexthop, either in the
 *            top-level chain or in a resolved chain.
 */
#define ALL_NEXTHOPS(head, nhop)					\
	(nhop) = (head.nexthop);					\
	(nhop);								\
	(nhop) = nexthop_next(nhop)

#define ALL_NEXTHOPS_PTR(head, nhop)					\
	(nhop) = ((head)->nexthop);					\
	(nhop);								\
	(nhop) = nexthop_next(nhop)


/* 命名 nexthop group 的名字（及其备份组名字）最大长度。 */
#define NHGC_NAME_SIZE 80

/* 一个“命名 nexthop group”的管理结构：
 *   nhgc_entry          挂入按名字排序的 RB 树（nhgc_entry_head）的节点；
 *   name                组名；
 *   backup_list_name    关联的备份组名（若配置了备份成员）；
 *   nhg                 内嵌的 nexthop_group（实际成员）；
 *   nhg_list            该组被哪些上层对象引用的列表；
 *   QOBJ_FIELDS         用于内省 / 引用计数的队列对象字段。
 */
struct nexthop_group_cmd {

	RB_ENTRY(nexthop_group_cmd) nhgc_entry;

	char name[NHGC_NAME_SIZE];

	/* Name of group containing backup nexthops (if set) */
	char backup_list_name[NHGC_NAME_SIZE];

	struct nexthop_group nhg;

	struct list *nhg_list;

	QOBJ_FIELDS;
};
RB_HEAD(nhgc_entry_head, nexthp_group_cmd);
RB_PROTOTYPE(nhgc_entry_head, nexthop_group_cmd, nhgc_entry,
	     nexthop_group_cmd_compare)
DECLARE_QOBJ_TYPE(nexthop_group_cmd);

/* 初始化命名 nexthop group 子系统，并注册一组回调，让调用者能感知组的
 * 创建 / 修改 / 成员增删 / 销毁。各回调参数见下方英文说明，允许传 NULL
 * 表示不关心该事件。
 */
/*
 * Initialize nexthop_groups.  If you are interested in when
 * a nexthop_group is added/deleted/modified, then set the
 * appropriate callback functions to handle it in your
 * code
 *
 * create - The creation of the nexthop group
 * modify - Modification of the nexthop group when not changing a nexthop
 *          ( resilience as an example )
 * add_nexthop - A nexthop is added to the NHG
 * del_nexthop - A nexthop is deleted from the NHG
 * destroy - The NHG is deleted
 */
void nexthop_group_init(
	void (*create)(const char *name),
	void (*modify)(const struct nexthop_group_cmd *nhgc),
	void (*add_nexthop)(const struct nexthop_group_cmd *nhgc,
			    const struct nexthop *nhop),
	void (*del_nexthop)(const struct nexthop_group_cmd *nhgc,
			    const struct nexthop *nhop),
	void (*destroy)(const char *name));

/* 某 VRF 创建时，遍历所有命名组，把属于该 VRF 的接口类 nexthop 激活。 */
void nexthop_group_enable_vrf(struct vrf *vrf);
/* 某 VRF 删除时，遍历所有命名组，禁用属于该 VRF 的接口类 nexthop。 */
void nexthop_group_disable_vrf(struct vrf *vrf);
/* 接口状态变化（含 ifindex 变更，oldifindex 为旧值）时，更新所有命名组中
 * 引用该接口的 nexthop 的 up / down 与 ifindex。
 */
void nexthop_group_interface_state_change(struct interface *ifp,
					  ifindex_t oldifindex);

/* 在 nhg 中查找与 nh 相等的成员，返回该成员指针；找不到返回 NULL。 */
extern struct nexthop *nexthop_exists(const struct nexthop_group *nhg,
				      const struct nexthop *nh);
/* 判定两组是否相等（忽略递归解析链）。前提：两组均已按规范顺序排列，
 * 否则结果不可靠。
 */
/* This assumes ordered */
extern bool nexthop_group_equal_no_recurse(const struct nexthop_group *nhg1,
					   const struct nexthop_group *nhg2);

/* 判定两组是否相等（含递归解析链）。同样要求已排序。 */
/* This assumes ordered */
extern bool nexthop_group_equal(const struct nexthop_group *nhg1,
				const struct nexthop_group *nhg2);

/* 按名字在命名组 RB 树中查找，返回其 nexthop_group_cmd；找不到返回 NULL。 */
extern struct nexthop_group_cmd *nhgc_find(const char *name);

/* 以简化格式把单个 nexthop 写到 vty；altifname 可指定替代接口名（非空时
 * 用它替代接口名显示）。用于命名组的配置回显。
 */
extern void nexthop_group_write_nexthop_simple(struct vty *vty,
					       const struct nexthop *nh,
					       char *altifname);
/* 以标准配置格式把单个 nexthop 写到 vty。 */
extern void nexthop_group_write_nexthop(struct vty *vty,
					const struct nexthop *nh);

/* 把单个 nexthop 追加为 JSON 对象 j 的一个数组元素。 */
struct json_object;
extern void nexthop_group_json_nexthop(struct json_object *j,
				       const struct nexthop *nh);

/* 返回该组内 nexthop 总数（含递归解析出的成员）。 */
/* Return the number of nexthops in this nhg */
extern uint16_t nexthop_group_nexthop_num(const struct nexthop_group *nhg);
/* 返回该组内处于 active 状态的 nexthop 数。 */
extern uint16_t nexthop_group_active_nexthop_num(const struct nexthop_group *nhg);
/* 返回该组内已标记 FIB（可硬件下发）的 nexthop 数。 */
extern uint16_t nexthop_group_fib_nexthop_num(const struct nexthop_group *nhg);

/* 判定 NHG 是否为“单成员”：有且仅有一个顶层 nexthop（next 为空）。
 * 注意只看顶层链，不含递归解析成员。
 */
/* Return TRUE if the NHG is Singleton (has only one nexthop) */
#define NHG_IS_SINGLETON(nhg) ((nhg)->nexthop && !(nhg)->nexthop->next)

/* 组内是否有任一成员带标签栈（MPLS 场景常用）。 */
extern bool nexthop_group_has_label(const struct nexthop_group *nhg);
/* 只统计顶层 nexthop 数量，不含递归解析成员。 */
extern uint16_t nexthop_group_nexthop_num_no_recurse(const struct nexthop_group *nhg);

#ifdef __cplusplus
}
#endif

#endif
