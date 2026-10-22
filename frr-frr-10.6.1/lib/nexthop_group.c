// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nexthop Group structure definition.
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *                    Donald Sharp
 */
/*
 * 文件导读：nexthop_group.c —— 下一跳组（nexthop group）的实现。
 *
 * 本文件实现 nexthop_group.h 声明的能力，并额外实现“命名 nexthop group”
 * 的配置命令、RB 树管理与事件回调。
 *
 * 一、两类概念
 *   1) 匿名 nexthop_group：路由临时持有的一组等价出口，核心是成员链表与
 *      规范排序、比较、哈希、复制；
 *   2) 命名 nexthop group（nexthop_group_cmd，以下简称 nhgc）：用户可用
 *      “nexthop-group NAME”配置并命名的一组下一跳，按名字挂在 RB 树
 *      （nhgc_entries）上，可被策略路由等多处引用。
 *
 * 二、规范排序与相等
 *   成员用 _nexthop_add 追加、nexthop_group_add_sorted 保序插入。相等判定
 *   （nexthop_group_equal）与哈希（nexthop_group_hash）都“假定已排序”，
 *   顺序不一致会导致比较结果错误。
 *
 * 三、回调机制
 *   nhg_hooks 保存上层注册的 5 个回调（new / modify / add_nexthop /
 *   del_nexthop / delete），在命名组发生相应变化时被调用，供 zebra 等同步
 *   转发面。
 *
 * 四、易踩的坑
 *   - nexthop_group 的哈希 / 相等对顺序敏感，务必先排序；
 *   - nexthop_group_delete 接受“指针的指针”，调用后 *nhg 被置 NULL；
 *   - 命名组涉及引用计数与 VRF / 接口状态变化，需配合 enable/disable_vrf
 *     与 interface_state_change 使用。
 */

#include <zebra.h>

#include <vrf.h>
#include <sockunion.h>
#include <nexthop.h>
#include <nexthop_group.h>
#include <nexthop_group_private.h>
#include <vty.h>
#include <command.h>
#include <jhash.h>
#include "lib/json.h"

#include "lib/nexthop_group_clippy.c"

DEFINE_MTYPE_STATIC(LIB, NEXTHOP_GROUP, "Nexthop Group");

/*
 * Internal struct used to hold nhg config strings
 */
/* 内部结构：临时保存从配置命令解析出的“单条 nexthop 配置串”的各字段，
 * 待校验 / 归一后再构造真正的 struct nexthop。
 *   nhvrf_name  VRF 名；addr  网关地址；intf  出接口名；
 *   onlink      是否 onlink；labels  标签配置串；vni  VXLAN VNI；
 *   weight      权重；backup_str 备份索引串。
 */
struct nexthop_hold {
	char *nhvrf_name;
	union sockunion *addr;
	char *intf;
	bool onlink;
	char *labels;
	vni_t vni;
	uint32_t weight;
	char *backup_str;
};

/* 上层（通常 zebra）注册到命名 nexthop group 的一组事件回调：
 *   new         新建组；modify      仅改属性（如 resilience）；
 *   add_nexthop 新增成员；del_nexthop 删除成员；delete 删除组。
 */
struct nexthop_group_hooks {
	void (*new)(const char *name);
	void (*modify)(const struct nexthop_group_cmd *nhgc);
	void (*add_nexthop)(const struct nexthop_group_cmd *nhg,
			    const struct nexthop *nhop);
	void (*del_nexthop)(const struct nexthop_group_cmd *nhg,
			    const struct nexthop *nhop);
	void (*delete)(const char *name);
};

/* 全局唯一的回调表实例；默认全为 NULL（未注册即不通知）。 */
static struct nexthop_group_hooks nhg_hooks;

/* RB 树（红黑树）按组名排序的实现：先声明比较函数，再用 RB_GENERATE 生成
 * 插入 / 查找 / 删除等模板代码，类型为 nhgc_entry_head。
 */
static inline int
nexthop_group_cmd_compare(const struct nexthop_group_cmd *nhgc1,
			  const struct nexthop_group_cmd *nhgc2);
RB_GENERATE(nhgc_entry_head, nexthop_group_cmd, nhgc_entry,
	    nexthop_group_cmd_compare)

/* 全部命名 nexthop group 的 RB 树根。 */
static struct nhgc_entry_head nhgc_entries;

/* RB 树比较函数：直接按组名 strcmp 排序。 */
static inline int
nexthop_group_cmd_compare(const struct nexthop_group_cmd *nhgc1,
			  const struct nexthop_group_cmd *nhgc2)
{
	return strcmp(nhgc1->name, nhgc2->name);
}

/* 返回 nexthop_group 顶层链表的尾节点；空组返回 NULL。只走顶层 next 链，
 * 不进入递归解析链。
 */
static struct nexthop *nexthop_group_tail(const struct nexthop_group *nhg)
{
	struct nexthop *nexthop = nhg->nexthop;

	while (nexthop && nexthop->next)
		nexthop = nexthop->next;

	return nexthop;
}

/* 统计组内 nexthop 总数（用 ALL_NEXTHOPS_PTR，含递归解析成员）。 */
uint16_t nexthop_group_nexthop_num(const struct nexthop_group *nhg)
{
	struct nexthop *nhop;
	uint16_t num = 0;

	for (ALL_NEXTHOPS_PTR(nhg, nhop))
		num++;

	return num;
}

/* 只统计顶层 nexthop 数量，不含递归解析成员。 */
uint16_t nexthop_group_nexthop_num_no_recurse(const struct nexthop_group *nhg)
{
	struct nexthop *nhop;
	uint16_t num = 0;

	for (nhop = nhg->nexthop; nhop; nhop = nhop->next)
		num++;

	return num;
}

/* 统计组内 ACTIVE 状态的 nexthop 数量（含递归解析成员）。 */
uint16_t nexthop_group_active_nexthop_num(const struct nexthop_group *nhg)
{
	struct nexthop *nhop;
	uint16_t num = 0;

	for (ALL_NEXTHOPS_PTR(nhg, nhop)) {
		if (CHECK_FLAG(nhop->flags, NEXTHOP_FLAG_ACTIVE))
			num++;
	}

	return num;
}

/* 统计组内已置 FIB 标志的 nexthop 数量（含递归解析成员）。 */
uint16_t nexthop_group_fib_nexthop_num(const struct nexthop_group *nhg)
{
	struct nexthop *nhop;
	uint16_t num = 0;

	for (ALL_NEXTHOPS_PTR(nhg, nhop)) {
		if (CHECK_FLAG(nhop->flags, NEXTHOP_FLAG_FIB))
			num++;
	}

	return num;
}

/* 组内是否有任一成员带标签栈（nh_label 非空）。找到即返回 true。 */
bool nexthop_group_has_label(const struct nexthop_group *nhg)
{
	struct nexthop *nhop;

	for (ALL_NEXTHOPS_PTR(nhg, nhop)) {
		if (nhop->nh_label)
			return true;
	}

	return false;
}

/* 在 nhg 顶层链表中查找与 nh“完整相等”（nexthop_same）的成员，返回其指针；
 * 找不到返回 NULL。只查顶层链，不查递归解析链。
 */
struct nexthop *nexthop_exists(const struct nexthop_group *nhg,
			       const struct nexthop *nh)
{
	struct nexthop *nexthop;

	for (nexthop = nhg->nexthop; nexthop; nexthop = nexthop->next) {
		if (nexthop_same(nh, nexthop))
			return nexthop;
	}

	return NULL;
}

/* 在 nhg 中按“配置语义”查找 nexthop：只比较 vrf、type、网关 / 接口 /
 * 黑洞类型，忽略标签、权重、源地址等。这与 nexthop_same 的完整比较不同，
 * 用于配置增删时定位成员。返回匹配成员，找不到返回 NULL。
 */
/*
 * Helper that locates a nexthop in an nhg config list. Note that
 * this uses a specific matching / equality rule that's different from
 * the complete match performed by 'nexthop_same()'.
 */
static struct nexthop *nhg_nh_find(const struct nexthop_group *nhg,
				   const struct nexthop *nh)
{
	struct nexthop *nexthop;
	int ret;

	/* We compare: vrf, gateway, and interface */

	for (nexthop = nhg->nexthop; nexthop; nexthop = nexthop->next) {

		/* Compare vrf and type */
		if (nexthop->vrf_id != nh->vrf_id)
			continue;
		if (nexthop->type != nh->type)
			continue;

		/* Compare gateway */
		switch (nexthop->type) {
		case NEXTHOP_TYPE_IPV4:
		case NEXTHOP_TYPE_IPV6:
			ret = nexthop_g_addr_cmp(nexthop->type,
						 &nexthop->gate, &nh->gate);
			if (ret != 0)
				continue;
			break;
		case NEXTHOP_TYPE_IPV4_IFINDEX:
		case NEXTHOP_TYPE_IPV6_IFINDEX:
			ret = nexthop_g_addr_cmp(nexthop->type,
						 &nexthop->gate, &nh->gate);
			if (ret != 0)
				continue;
			fallthrough;
		case NEXTHOP_TYPE_IFINDEX:
			if (nexthop->ifindex != nh->ifindex)
				continue;
			break;
		case NEXTHOP_TYPE_BLACKHOLE:
			if (nexthop->bh_type != nh->bh_type)
				continue;
			break;
		}

		return nexthop;
	}

	return NULL;
}

/* 两组相等判定的公共前置检查：NULL 规则（一方为空不相等、同一指针相等）、
 * 成员个数是否相同（用传入的计数函数决定是否含递归）。返回 true 表示“有
 * 可能相等，需继续逐成员比较”。注意：只做前置判断，不做逐成员比较。
 */
static bool nexthop_group_equal_common(
	const struct nexthop_group *nhg1, const struct nexthop_group *nhg2,
	uint16_t (*nexthop_group_nexthop_num_func)(const struct nexthop_group *nhg))
{
	if (nhg1 && !nhg2)
		return false;

	if (!nhg1 && nhg2)
		return false;

	if (nhg1 == nhg2)
		return true;

	if (nexthop_group_nexthop_num_func(nhg1)
	    != nexthop_group_nexthop_num_func(nhg2))
		return false;

	return true;
}

/* 比较两组是否相等（忽略递归解析链）：先做公共前置检查，再按顶层 next 链
 * 逐成员 nexthop_same。前提：两组均已按规范顺序排列。
 */
/* This assumes ordered */
bool nexthop_group_equal_no_recurse(const struct nexthop_group *nhg1,
				    const struct nexthop_group *nhg2)
{
	struct nexthop *nh1 = NULL;
	struct nexthop *nh2 = NULL;

	if (!nexthop_group_equal_common(nhg1, nhg2,
					&nexthop_group_nexthop_num_no_recurse))
		return false;

	for (nh1 = nhg1->nexthop, nh2 = nhg2->nexthop; nh1 || nh2;
	     nh1 = nh1->next, nh2 = nh2->next) {
		if (nh1 && !nh2)
			return false;
		if (!nh1 && nh2)
			return false;
		if (!nexthop_same(nh1, nh2))
			return false;
	}

	return true;
}

/* 比较两组是否相等（含递归解析链）：公共前置检查后，用 nexthop_next 遍历
 * （会下钻解析链）逐成员 nexthop_same。前提：两组均已按规范顺序排列。
 */
/* This assumes ordered */
bool nexthop_group_equal(const struct nexthop_group *nhg1,
			 const struct nexthop_group *nhg2)
{
	struct nexthop *nh1 = NULL;
	struct nexthop *nh2 = NULL;

	if (!nexthop_group_equal_common(nhg1, nhg2, &nexthop_group_nexthop_num))
		return false;

	for (nh1 = nhg1->nexthop, nh2 = nhg2->nexthop; nh1 || nh2;
	     nh1 = nexthop_next(nh1), nh2 = nexthop_next(nh2)) {
		if (nh1 && !nh2)
			return false;
		if (!nh1 && nh2)
			return false;
		if (!nexthop_same(nh1, nh2))
			return false;
	}

	return true;
}
/* 分配并零初始化一个空 nexthop_group（XCALLOC，失败即终止）。 */
struct nexthop_group *nexthop_group_new(void)
{
	return XCALLOC(MTYPE_NEXTHOP_GROUP, sizeof(struct nexthop_group));
}

/* 深拷贝 from 到已存在的 to：拷贝 resilience 参数，并用 copy_nexthops 复制
 * 整条成员链（含递归信息，rparent 以 NULL 起）。to 原有成员会被覆盖，调用
 * 者应先确保 to 为空或已释放。
 */
void nexthop_group_copy(struct nexthop_group *to,
			const struct nexthop_group *from)
{
	to->nhgr = from->nhgr;
	/* Copy everything, including recursive info */
	copy_nexthops(&to->nexthop, from->nexthop, NULL);
}

/* 删除整组：释放成员与结构体，并把 *nhg 置 NULL。对 *nhg 为 NULL 安全
 * （直接返回），因此可用于“可能为空”的场景。
 */
void nexthop_group_delete(struct nexthop_group **nhg)
{
	/* OK to call with NULL group */
	if ((*nhg) == NULL)
		return;

	if ((*nhg)->nexthop)
		nexthops_free((*nhg)->nexthop);

	XFREE(MTYPE_NEXTHOP_GROUP, *nhg);
}

/* 把 nexthop 追加到链表 *target 的末尾（遍历到最后一个节点再挂）。*target
 * 为空时把它作为新头。注意：它不维护 prev 指针，也不保序。
 */
/* Add nexthop to the end of a nexthop list.  */
void _nexthop_add(struct nexthop **target, struct nexthop *nexthop)
{
	struct nexthop *last;

	for (last = *target; last && last->next; last = last->next)
		;
	if (last)
		last->next = nexthop;
	else
		*target = nexthop;
	nexthop->prev = last;
}

/* 把 nexthop 按 nexthop_cmp 升序插入到已排序链表 *head 的正确位置，并维护
 * prev / next 双向指针。要求 nexthop->next 为空（assert 校验）。
 */
/* Add nexthop to sorted list of nexthops */
static void _nexthop_add_sorted(struct nexthop **head,
				struct nexthop *nexthop)
{
	struct nexthop *position, *prev;

	assert(!nexthop->next);

	for (position = *head, prev = NULL; position;
	     prev = position, position = position->next) {
		if (nexthop_cmp(position, nexthop) > 0) {
			nexthop->next = position;
			nexthop->prev = prev;

			if (nexthop->prev)
				nexthop->prev->next = nexthop;
			else
				*head = nexthop;

			position->prev = nexthop;
			return;
		}
	}

	nexthop->prev = prev;
	if (prev)
		prev->next = nexthop;
	else
		*head = nexthop;
}

/* 按规范顺序把 nexthop 加入 nhg。优化：先与尾节点比较，若比尾大则直接追加
 * （假定链表已排序），否则退回 _nexthop_add_sorted 逐位插入。要求 nexthop
 * 的 next 为空。
 */
void nexthop_group_add_sorted(struct nexthop_group *nhg,
			      struct nexthop *nexthop)
{
	struct nexthop *tail;

	assert(!nexthop->next);

	/* Try to just append to the end first;
	 * trust the list is already sorted
	 */
	tail = nexthop_group_tail(nhg);

	if (tail && (nexthop_cmp(tail, nexthop) < 0)) {
		tail->next = nexthop;
		nexthop->prev = tail;

		return;
	}

	_nexthop_add_sorted(&nhg->nexthop, nexthop);
}

/* 从 nhg 顶层链表中删除与 nh 相同的成员（按 nexthop_same 查找），并从链表
 * 摘除、把 nh 的 prev / next 置空。要求能找到该成员（assert），否则崩溃。
 */
/* Delete nexthop from a nexthop list.  */
void _nexthop_del(struct nexthop_group *nhg, struct nexthop *nh)
{
	struct nexthop *nexthop;

	for (nexthop = nhg->nexthop; nexthop; nexthop = nexthop->next) {
		if (nexthop_same(nh, nexthop))
			break;
	}

	assert(nexthop);

	if (nexthop->prev)
		nexthop->prev->next = nexthop->next;
	else
		nhg->nexthop = nexthop->next;

	if (nexthop->next)
		nexthop->next->prev = nexthop->prev;

	nh->prev = NULL;
	nh->next = NULL;
}

/* 无条件地把 nexthop 从其所在链表摘除（不做查找 / 相等判断）：修正前后节点
 * 的指针，若它是头节点则更新 nhg->nexthop（用 assert 校验一致性），最后把
 * 它的 prev / next 置空。
 */
/* Unlink a nexthop from the list it's on, unconditionally */
static void nexthop_unlink(struct nexthop_group *nhg, struct nexthop *nexthop)
{

	if (nexthop->prev)
		nexthop->prev->next = nexthop->next;
	else {
		assert(nhg->nexthop == nexthop);
		assert(nexthop->prev == NULL);
		nhg->nexthop = nexthop->next;
	}

	if (nexthop->next)
		nexthop->next->prev = nexthop->prev;

	nexthop->prev = NULL;
	nexthop->next = NULL;
}

/* 把链表 nh 中的每个 nexthop 复制（nexthop_dup）并保序插入 nhg。优化：若
 * 源链表本身有序，与尾比较后直接追加即可，避免逐位比较。
 */
/*
 * Copy a list of nexthops in 'nh' to an nhg, enforcing canonical sort order
 */
void nexthop_group_copy_nh_sorted(struct nexthop_group *nhg,
				  const struct nexthop *nh)
{
	struct nexthop *nexthop, *tail;
	const struct nexthop *nh1;

	/* We'll try to append to the end of the new list;
	 * if the original list in nh is already sorted, this eliminates
	 * lots of comparison operations.
	 */
	tail = nexthop_group_tail(nhg);

	for (nh1 = nh; nh1; nh1 = nh1->next) {
		nexthop = nexthop_dup(nh1, NULL);

		if (tail && (nexthop_cmp(tail, nexthop) < 0)) {
			tail->next = nexthop;
			nexthop->prev = tail;

			tail = nexthop;
			continue;
		}

		_nexthop_add_sorted(&nhg->nexthop, nexthop);

		if (tail == NULL)
			tail = nexthop;
	}
}

/* 复制链表 nh 并按原顺序追加到 *tnh（_nexthop_add），不做排序。rparent 作为
 * 复制出的每个 nexthop 的递归父指针。
 */
/* Copy a list of nexthops, no effort made to sort or order them. */
void copy_nexthops(struct nexthop **tnh, const struct nexthop *nh,
		   struct nexthop *rparent)
{
	struct nexthop *nexthop;
	const struct nexthop *nh1;

	for (nh1 = nh; nh1; nh1 = nh1->next) {
		nexthop = nexthop_dup(nh1, rparent);
		_nexthop_add(tnh, nexthop);
	}
}

/* 对整组求哈希，但只遍历顶层 next 链（不含递归解析成员）。逐个成员用
 * nexthop_hash 后以 jhash_1word 累积。结果依赖成员顺序。
 */
uint32_t nexthop_group_hash_no_recurse(const struct nexthop_group *nhg)
{
	struct nexthop *nh;
	uint32_t key = 0;

	/*
	 * We are not interested in hashing over any recursively
	 * resolved nexthops
	 */
	for (nh = nhg->nexthop; nh; nh = nh->next)
		key = jhash_1word(nexthop_hash(nh), key);

	return key;
}

/* 对整组求哈希（用 ALL_NEXTHOPS_PTR，含递归解析成员）。逐个成员用
 * nexthop_hash 后以 jhash_1word 累积。结果依赖成员顺序。
 */
uint32_t nexthop_group_hash(const struct nexthop_group *nhg)
{
	struct nexthop *nh;
	uint32_t key = 0;

	for (ALL_NEXTHOPS_PTR(nhg, nh))
		key = jhash_1word(nexthop_hash(nh), key);

	return key;
}

/* 遍历组内每个 nexthop（含递归成员），若与“它之前的某个成员”nexthop_same
 * 相等，则给它打上 NEXTHOP_FLAG_DUPLICATE。进入时先清除本节点旧标志再判断，
 * 因此可反复调用而结果稳定。副作用：修改成员 flags。
 */
void nexthop_group_mark_duplicates(struct nexthop_group *nhg)
{
	struct nexthop *nexthop, *prev;

	for (ALL_NEXTHOPS_PTR(nhg, nexthop)) {
		UNSET_FLAG(nexthop->flags, NEXTHOP_FLAG_DUPLICATE);
		for (ALL_NEXTHOPS_PTR(nhg, prev)) {
			if (prev == nexthop)
				break;
			if (nexthop_same(nexthop, prev)) {
				SET_FLAG(nexthop->flags,
					 NEXTHOP_FLAG_DUPLICATE);
				break;
			}
		}
	}
}

/* 删除命名组 nhgc 中的全部 nexthop（含递归成员）：逐个从链表摘除、调用
 * del_nexthop 回调（若注册）、再释放。用 nexthop_next 保存下一个节点，避免
 * 释放后解引用。
 */
static void nhgc_delete_nexthops(struct nexthop_group_cmd *nhgc)
{
	struct nexthop *nexthop;

	nexthop = nhgc->nhg.nexthop;
	while (nexthop) {
		struct nexthop *next = nexthop_next(nexthop);

		_nexthop_del(&nhgc->nhg, nexthop);
		if (nhg_hooks.del_nexthop)
			nhg_hooks.del_nexthop(nhgc, nexthop);

		nexthop_free(nexthop);

		nexthop = next;
	}
}

/* 按名字在 RB 树中查找命名组，返回其 nexthop_group_cmd；找不到返回 NULL。
 * 内部构造一个临时 key 做 RB_FIND，名字会按 NHGC_NAME_SIZE 截断。
 */
struct nexthop_group_cmd *nhgc_find(const char *name)
{
	struct nexthop_group_cmd find;

	strlcpy(find.name, name, sizeof(find.name));

	return RB_FIND(nhgc_entry_head, &nhgc_entries, &find);
}

/* 字符串比较助手：先处理 NULL（双 NULL 相等、单 NULL 有固定大小关系），
 * 再 strcmp。用于对可空字符串字段排序。
 */
static int nhgc_cmp_helper(const char *a, const char *b)
{
	if (!a && !b)
		return 0;

	if (a && !b)
		return -1;

	if (!a && b)
		return 1;

	return strcmp(a, b);
}

/* sockunion 地址比较助手：NULL 处理同 nhgc_cmp_helper，之后用 sockunion_cmp
 * 比较实际地址。
 */
static int nhgc_addr_cmp_helper(const union sockunion *a, const union sockunion *b)
{
	if (!a && !b)
		return 0;

	if (a && !b)
		return -1;

	if (!a && b)
		return 1;

	return sockunion_cmp(a, b);
}

/* 两个 nexthop_hold（配置项）的排序比较：依次比较地址 → 接口 → VRF →
 * onlink（注意 onlink 是降序：nh2 - nh1）→ 标签串。用于 nhg_list 的排序 / 去重。
 */
static int nhgl_cmp(struct nexthop_hold *nh1, struct nexthop_hold *nh2)
{
	int ret;

	ret = nhgc_addr_cmp_helper(nh1->addr, nh2->addr);
	if (ret)
		return ret;

	ret = nhgc_cmp_helper(nh1->intf, nh2->intf);
	if (ret)
		return ret;

	ret = nhgc_cmp_helper(nh1->nhvrf_name, nh2->nhvrf_name);
	if (ret)
		return ret;

	ret = ((int)nh2->onlink) - ((int)nh1->onlink);
	if (ret)
		return ret;

	return nhgc_cmp_helper(nh1->labels, nh2->labels);
}

/* 释放一个 nexthop_hold 配置项：释放 intf / nhvrf_name / addr / labels 及
 * 结构体本身。作为 nhg_list 的 del 回调使用。
 */
static void nhgl_delete(struct nexthop_hold *nh)
{
	XFREE(MTYPE_TMP, nh->intf);

	XFREE(MTYPE_TMP, nh->nhvrf_name);

	if (nh->addr)
		sockunion_free(nh->addr);

	XFREE(MTYPE_TMP, nh->labels);

	XFREE(MTYPE_TMP, nh);
}

/* 按名字获取命名组，不存在则新建：分配 nexthop_group_cmd、登记 QOBJ、插入
 * RB 树、初始化内部的 nhg_list（带 nhgl_cmp / nhgl_delete 回调），并在新建
 * 时触发 nhg_hooks.new。返回该命名组。
 */
static struct nexthop_group_cmd *nhgc_get(const char *name)
{
	struct nexthop_group_cmd *nhgc;

	nhgc = nhgc_find(name);
	if (!nhgc) {
		nhgc = XCALLOC(MTYPE_TMP, sizeof(*nhgc));
		strlcpy(nhgc->name, name, sizeof(nhgc->name));

		QOBJ_REG(nhgc, nexthop_group_cmd);
		RB_INSERT(nhgc_entry_head, &nhgc_entries, nhgc);

		nhgc->nhg_list = list_new();
		nhgc->nhg_list->cmp = (int (*)(void *, void *))nhgl_cmp;
		nhgc->nhg_list->del = (void (*)(void *))nhgl_delete;

		if (nhg_hooks.new)
			nhg_hooks.new(name);
	}

	return nhgc;
}

/* 删除命名组：先删除其全部 nexthop，触发 nhg_hooks.delete，再从 RB 树移除、
 * 删除 nhg_list、注销 QOBJ，最后释放结构体。
 */
static void nhgc_delete(struct nexthop_group_cmd *nhgc)
{
	nhgc_delete_nexthops(nhgc);

	if (nhg_hooks.delete)
		nhg_hooks.delete(nhgc->name);

	RB_REMOVE(nhgc_entry_head, &nhgc_entries, nhgc);

	list_delete(&nhgc->nhg_list);

	QOBJ_UNREG(nhgc);
	XFREE(MTYPE_TMP, nhgc);
}

/* 为 nexthop_group_cmd 生成 QOBJ 类型登记（供内省 / 引用计数框架使用）。 */
DEFINE_QOBJ_TYPE(nexthop_group_cmd);

/* 命令 “nexthop-group NAME”：创建（或获取）命名组并进入其子模式。
 * 注意本文件仅提供独立命令实现，实际命令注册在 nexthop_group_init 中完成，
 * 故用 DEFUN_NOSH。副作用：可能新建命名组并触发 new 回调。
 */
DEFUN_NOSH(nexthop_group, nexthop_group_cmd, "nexthop-group NHGNAME",
	   "Enter into the nexthop-group submode\n"
	   "Specify the NAME of the nexthop-group\n")
{
	const char *nhg_name = argv[1]->arg;
	struct nexthop_group_cmd *nhgc = NULL;

	nhgc = nhgc_get(nhg_name);
	VTY_PUSH_CONTEXT(NH_GROUP_NODE, nhgc);

	return CMD_SUCCESS;
}

/* 命令 “no nexthop-group NAME”：按名查找并删除命名组；不存在则静默成功。 */
DEFUN_NOSH(no_nexthop_group, no_nexthop_group_cmd, "no nexthop-group NHGNAME",
	   NO_STR
	   "Delete the nexthop-group\n"
	   "Specify the NAME of the nexthop-group\n")
{
	const char *nhg_name = argv[2]->arg;
	struct nexthop_group_cmd *nhgc = NULL;

	nhgc = nhgc_find(nhg_name);
	if (nhgc)
		nhgc_delete(nhgc);

	return CMD_SUCCESS;
}

/* 命令 “backup-group NAME”：把当前命名组的备份组名设为 name。 */
DEFPY(nexthop_group_backup, nexthop_group_backup_cmd,
      "backup-group WORD$name",
      "Specify a group name containing backup nexthops\n"
      "The name of the backup group\n")
{
	VTY_DECLVAR_CONTEXT(nexthop_group_cmd, nhgc);

	strlcpy(nhgc->backup_list_name, name, sizeof(nhgc->backup_list_name));

	return CMD_SUCCESS;
}

/* 命令 “no backup-group [NAME]”：清空当前命名组的备份组名。 */
DEFPY(no_nexthop_group_backup, no_nexthop_group_backup_cmd,
      "no backup-group [WORD$name]",
      NO_STR
      "Clear group name containing backup nexthops\n"
      "The name of the backup group\n")
{
	VTY_DECLVAR_CONTEXT(nexthop_group_cmd, nhgc);

	nhgc->backup_list_name[0] = 0;

	return CMD_SUCCESS;
}

/* 命令 “resilient buckets ... idle-timer ... unbalanced-timer ...”：设置当前
 * 命名组的韧性参数（桶数、空闲与失衡定时器），并触发 modify 回调。
 */
DEFPY(nexthop_group_resilience,
      nexthop_group_resilience_cmd,
      "resilient buckets (1-256) idle-timer (1-4294967295) unbalanced-timer (1-4294967295)",
      "A resilient Nexthop Group\n"
      "Buckets in the Hash for this Group\n"
      "Number of buckets\n"
      "The Idle timer for this Resilient Nexthop Group in seconds\n"
      "Number of seconds of Idle time\n"
      "The length of time that the Nexthop Group can be unbalanced\n"
      "Number of seconds of Unbalanced time\n")
{
	VTY_DECLVAR_CONTEXT(nexthop_group_cmd, nhgc);

	nhgc->nhg.nhgr.buckets = buckets;
	nhgc->nhg.nhgr.idle_timer = idle_timer;
	nhgc->nhg.nhgr.unbalanced_timer = unbalanced_timer;

	if (nhg_hooks.modify)
		nhg_hooks.modify(nhgc);

	return CMD_SUCCESS;
}

/* 命令 “no resilient ...”：把当前命名组的韧性参数全部清零。 */
DEFPY(no_nexthop_group_resilience,
      no_nexthop_group_resilience_cmd,
      "no resilient [buckets (1-256) idle-timer (1-4294967295) unbalanced-timer (1-4294967295)]",
      NO_STR
      "A resilient Nexthop Group\n"
      "Buckets in the Hash for this Group\n"
      "Number of buckets\n"
      "The Idle timer for this Resilient Nexthop Group in seconds\n"
      "Number of seconds of Idle time\n"
      "The length of time that the Nexthop Group can be unbalanced\n"
      "Number of seconds of Unbalanced time\n")
{
	VTY_DECLVAR_CONTEXT(nexthop_group_cmd, nhgc);

	nhgc->nhg.nhgr.buckets = 0;
	nhgc->nhg.nhgr.idle_timer = 0;
	nhgc->nhg.nhgr.unbalanced_timer = 0;

	return CMD_SUCCESS;
}

/* 保存一条 nexthop 的“配置信息”：分配 nexthop_hold，按需深拷贝各字符串 /
 * 地址，填入 onlink / weight / backup_str，再按序插入 nhgc->nhg_list。
 * 保存的是原始配置（可能尚未生效），与已解析的 struct nexthop 并行存在。
 */
static void nexthop_group_save_nhop(struct nexthop_group_cmd *nhgc,
				    const char *nhvrf_name,
				    const union sockunion *addr,
				    const char *intf, bool onlink,
				    const char *labels, const uint32_t weight,
				    const char *backup_str)
{
	struct nexthop_hold *nh;

	nh = XCALLOC(MTYPE_TMP, sizeof(*nh));

	if (nhvrf_name)
		nh->nhvrf_name = XSTRDUP(MTYPE_TMP, nhvrf_name);
	if (intf)
		nh->intf = XSTRDUP(MTYPE_TMP, intf);
	if (addr)
		nh->addr = sockunion_dup(addr);
	if (labels)
		nh->labels = XSTRDUP(MTYPE_TMP, labels);

	nh->onlink = onlink;

	nh->weight = weight;

	if (backup_str)
		nh->backup_str = XSTRDUP(MTYPE_TMP, backup_str);

	listnode_add_sort(nhgc->nhg_list, nh);
}

/* 从 nhgc->nhg_list 中按 vrf + 地址 + 接口（仅此三项）找到并删除对应的
 * nexthop_hold 配置项。找不到则静默返回。它只改配置列表，不动实际 nexthop。
 */
/*
 * Remove config info about a nexthop from group 'nhgc'. Note that we
 * use only a subset of the available attributes here to determine
 * a 'match'.
 * Note that this doesn't change the list of nexthops, only the config
 * information.
 */
static void nexthop_group_unsave_nhop(struct nexthop_group_cmd *nhgc,
				      const char *nhvrf_name,
				      const union sockunion *addr,
				      const char *intf)
{
	struct nexthop_hold *nh;
	struct listnode *node;

	for (ALL_LIST_ELEMENTS_RO(nhgc->nhg_list, node, nh)) {
		if (nhgc_cmp_helper(nhvrf_name, nh->nhvrf_name) == 0
		    && nhgc_addr_cmp_helper(addr, nh->addr) == 0
		    && nhgc_cmp_helper(intf, nh->intf) == 0)
			break;
	}

	/*
	 * Something has gone seriously wrong, fail gracefully
	 */
	if (!nh)
		return;

	list_delete_node(nhgc->nhg_list, node);
	nhgl_delete(nh);
}

/* 由配置串构造一个 nexthop：解析 vrf 名（缺省用默认 VRF）、接口名（未就绪
 * 会失败）、onlink、地址（IPv4 / IPv6，决定 type）、标签串（mpls_str2label，
 * 结果经 lbl_ret 回传）、VNI（EVPN 场景）、weight、备份索引串。
 * 返回 true 表示合法；false 可能是暂时性失败（如接口未就绪）或致命错误
 * （标签解析失败，此时 *lbl_ret < 0）。
 */
/*
 * Parse the config strings we support for a single nexthop. This gets used
 * in a couple of different ways, and we distinguish between transient
 * failures - such as a still-unprocessed interface - and fatal errors
 * from label-string parsing.
 */
static bool nexthop_group_parse_nexthop(struct nexthop *nhop,
					const union sockunion *addr,
					const char *intf, bool onlink,
					const char *name, const char *labels,
					vni_t vni, int *lbl_ret,
					uint32_t weight, const char *backup_str)
{
	int ret = 0;
	struct vrf *vrf;
	int num;
	uint8_t labelnum = 0;

	memset(nhop, 0, sizeof(*nhop));

	if (name)
		vrf = vrf_lookup_by_name(name);
	else
		vrf = vrf_lookup_by_id(VRF_DEFAULT);

	if (!vrf)
		return false;

	nhop->vrf_id = vrf->vrf_id;

	if (intf) {
		nhop->ifindex = ifname2ifindex(intf, vrf->vrf_id);
		if (nhop->ifindex == IFINDEX_INTERNAL)
			return false;
	}

	if (onlink)
		SET_FLAG(nhop->flags, NEXTHOP_FLAG_ONLINK);

	if (addr) {
		if (addr->sa.sa_family == AF_INET) {
			nhop->gate.ipv4.s_addr = addr->sin.sin_addr.s_addr;
			if (intf)
				nhop->type = NEXTHOP_TYPE_IPV4_IFINDEX;
			else
				nhop->type = NEXTHOP_TYPE_IPV4;
		} else {
			nhop->gate.ipv6 = addr->sin6.sin6_addr;
			if (intf)
				nhop->type = NEXTHOP_TYPE_IPV6_IFINDEX;
			else
				nhop->type = NEXTHOP_TYPE_IPV6;
		}
	} else
		nhop->type = NEXTHOP_TYPE_IFINDEX;

	if (labels) {
		mpls_label_t larray[MPLS_MAX_LABELS];

		ret = mpls_str2label(labels, &labelnum, larray);

		/* Return label parse result */
		if (lbl_ret)
			*lbl_ret = ret;

		if (ret < 0)
			return false;
		else if (labelnum > 0)
			nexthop_add_labels(nhop, ZEBRA_LSP_NONE, labelnum,
					   larray);
	} else if (vni) {
		mpls_label_t label = MPLS_INVALID_LABEL;

		vni2label(vni, &label);
		nexthop_add_labels(nhop, ZEBRA_LSP_EVPN, 1, &label);
	}

	nhop->weight = weight;

	if (backup_str) {
		/* Parse backup indexes */
		ret = nexthop_str2backups(backup_str,
					  &num, nhop->backup_idx);
		if (ret == 0) {
			SET_FLAG(nhop->flags, NEXTHOP_FLAG_HAS_BACKUP);
			nhop->backup_num = num;
		} else
			return false;
	}

	return true;
}

/* 便捷封装：把一个 nexthop_hold 的各字段喂给 nexthop_group_parse_nexthop
 * 构造出 nexthop（lbl_ret 传 NULL，即不关心标签解析细节）。返回是否合法。
 */
/*
 * Wrapper to parse the strings in a 'nexthop_hold'
 */
static bool nexthop_group_parse_nhh(struct nexthop *nhop,
				    const struct nexthop_hold *nhh)
{
	return (nexthop_group_parse_nexthop(
		nhop, nhh->addr, nhh->intf, nhh->onlink, nhh->nhvrf_name,
		nhh->labels, nhh->vni, NULL, nhh->weight, nhh->backup_str));
}

/* 核心命令 “[no] nexthop <ADDR|INTERFACE> [onlink] [nexthop-vrf ...|label ...|
 * vni ...|weight ...|backup-idx ...]”：向当前命名组增删一条 nexthop。
 * 流程：先校验备份串，再解析出 nhop（可能因接口未就绪而“暂时非法”）；
 * 拒绝无接口的 v6 链路本地地址；对标签解析错误给出具体提示；随后总是移除
 * 旧配置信息与旧 nexthop，若是新增则把合法的 nhop 挂入组并保存配置，最后
 * 触发相应回调。返回 CMD_SUCCESS 或 CMD_WARNING_CONFIG_FAILED。
 */
DEFPY(ecmp_nexthops, ecmp_nexthops_cmd,
      "[no] nexthop\
        <\
	  <A.B.C.D|X:X::X:X>$addr [INTERFACE$intf [onlink$onlink]]\
	  |INTERFACE$intf\
	>\
	[{ \
	   nexthop-vrf NAME$vrf_name \
	   |label WORD \
	   |vni (1-16777215) \
           |weight (1-4294967295) \
           |backup-idx WORD \
	}]",
      NO_STR
      "Specify one of the nexthops in this ECMP group\n"
      "v4 Address\n"
      "v6 Address\n"
      "Interface to use\n"
      "Treat nexthop as directly attached to the interface\n"
      "Interface to use\n"
      "If the nexthop is in a different vrf tell us\n"
      "The nexthop-vrf Name\n"
      "Specify label(s) for this nexthop\n"
      "One or more labels in the range (16-1048575) separated by '/'\n"
      "Specify VNI(s) for this nexthop\n"
      "VNI in the range (1-16777215)\n"
      "Weight to be used by the nexthop for purposes of ECMP\n"
      "Weight value to be used\n"
      "Specify backup nexthop indexes in another group\n"
      "One or more indexes in the range (0-254) separated by ','\n")
{
	VTY_DECLVAR_CONTEXT(nexthop_group_cmd, nhgc);
	struct nexthop nhop;
	struct nexthop *nh;
	int lbl_ret = 0;
	bool legal;
	int num;
	uint8_t backups[NEXTHOP_MAX_BACKUPS];
	bool yes = !no;

	/* Pre-parse backup string to validate */
	if (backup_idx) {
		lbl_ret = nexthop_str2backups(backup_idx, &num, backups);
		if (lbl_ret < 0) {
			vty_out(vty, "%% Invalid backups\n");
			return CMD_WARNING_CONFIG_FAILED;
		}
	}

	legal = nexthop_group_parse_nexthop(&nhop, addr, intf, !!onlink,
					    vrf_name, label, vni, &lbl_ret,
					    weight, backup_idx);

	if (nhop.type == NEXTHOP_TYPE_IPV6
	    && IN6_IS_ADDR_LINKLOCAL(&nhop.gate.ipv6)) {
		vty_out(vty,
			"Specified a v6 LL with no interface, rejecting\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	/* Handle label-string errors */
	if (!legal && lbl_ret < 0) {
		switch (lbl_ret) {
		case -1:
			vty_out(vty, "%% Malformed label(s)\n");
			break;
		case -2:
			vty_out(vty,
				"%% Cannot use reserved label(s) (%d-%d)\n",
				MPLS_LABEL_RESERVED_MIN,
				MPLS_LABEL_RESERVED_MAX);
			break;
		case -3:
			vty_out(vty,
				"%% Too many labels. Enter %d or fewer\n",
				MPLS_MAX_LABELS);
			break;
		}
		return CMD_WARNING_CONFIG_FAILED;
	}

	/* Look for an existing nexthop in the config. Note that the test
	 * here tests only some attributes - it's not a complete comparison.
	 * Note that we've got two kinds of objects to manage: 'nexthop_hold'
	 * that represent config that may or may not be valid (yet), and
	 * actual nexthops that have been validated and parsed.
	 */
	nh = nhg_nh_find(&nhgc->nhg, &nhop);

	/* Always attempt to remove old config info. */
	nexthop_group_unsave_nhop(nhgc, vrf_name, addr, intf);

	/* Remove any existing nexthop, for delete and replace cases. */
	if (nh) {
		nexthop_unlink(&nhgc->nhg, nh);

		if (nhg_hooks.del_nexthop)
			nhg_hooks.del_nexthop(nhgc, nh);

		nexthop_free(nh);
	}
	if (yes) {
		/* Add/replace case: capture nexthop if valid, and capture
		 * config info always.
		 */
		if (legal) {
			nh = nexthop_new();

			memcpy(nh, &nhop, sizeof(nhop));
			_nexthop_add(&nhgc->nhg.nexthop, nh);
		}

		/* Save config always */
		nexthop_group_save_nhop(nhgc, vrf_name, addr, intf, !!onlink,
					label, weight, backup_idx);

		if (legal && nhg_hooks.add_nexthop)
			nhg_hooks.add_nexthop(nhgc, nh);
	}

	return CMD_SUCCESS;
}

/* cmd_node：定义 nexthop-group 子模式（NH_GROUP_NODE），其配置回显由
 * nexthop_group_write 完成（此处先声明）。
 */
static int nexthop_group_write(struct vty *vty);
static struct cmd_node nexthop_group_node = {
	.name = "nexthop-group",
	.node = NH_GROUP_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-nh-group)# ",
	.config_write = nexthop_group_write,
};

/* 以简化格式输出单条 nexthop 的“nexthop ...”部分（不含 vrf / label / weight
 * 等后缀）：按类型打印接口名或地址 + 接口名，黑洞打印 drop。altifname 非空
 * 时用作接口名（常用于重命名场景）。
 */
void nexthop_group_write_nexthop_simple(struct vty *vty,
					const struct nexthop *nh,
					char *altifname)
{
	char *ifname;

	vty_out(vty, "nexthop ");

	if (altifname)
		ifname = altifname;
	else
		ifname = (char *)ifindex2ifname(nh->ifindex, nh->vrf_id);

	switch (nh->type) {
	case NEXTHOP_TYPE_IFINDEX:
		vty_out(vty, "%s", ifname);
		break;
	case NEXTHOP_TYPE_IPV4:
		vty_out(vty, "%pI4", &nh->gate.ipv4);
		break;
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		vty_out(vty, "%pI4 %s", &nh->gate.ipv4, ifname);
		break;
	case NEXTHOP_TYPE_IPV6:
		vty_out(vty, "%pI6", &nh->gate.ipv6);
		break;
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		vty_out(vty, "%pI6 %s", &nh->gate.ipv6, ifname);
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		vty_out(vty, "%s", "drop");
		break;
	}
}

/* 以完整配置格式输出单条 nexthop：先调 _simple 打印主体，再补 nexthop-vrf /
 * label / weight / backup-idx 等后缀，最后换行。
 */
void nexthop_group_write_nexthop(struct vty *vty, const struct nexthop *nh)
{
	struct vrf *vrf;
	int i;

	nexthop_group_write_nexthop_simple(vty, nh, NULL);

	if (nh->vrf_id != VRF_DEFAULT) {
		vrf = vrf_lookup_by_id(nh->vrf_id);
		vty_out(vty, " nexthop-vrf %s", VRF_LOGNAME(vrf));
	}

	if (nh->nh_label && nh->nh_label->num_labels > 0) {
		char buf[200];

		mpls_label2str(nh->nh_label->num_labels, nh->nh_label->label,
			       buf, sizeof(buf), nh->nh_label_type, 0);
		vty_out(vty, " label %s", buf);
	}

	if (nh->weight)
		vty_out(vty, " weight %u", nh->weight);

	if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_HAS_BACKUP)) {
		vty_out(vty, " backup-idx %d", nh->backup_idx[0]);

		for (i = 1; i < nh->backup_num; i++)
			vty_out(vty, ",%d", nh->backup_idx[i]);
	}

	vty_out(vty, "\n");
}

/* 把单条 nexthop 以 JSON 形式追加到对象 j：按类型输出 nexthop 地址 / 接口，
 * 非默认 VRF 时输出 targetVrf，另有 label、weight、backupIdx（仅 HAS_BACKUP
 * 时）。无返回值。
 */
void nexthop_group_json_nexthop(json_object *j, const struct nexthop *nh)
{
	struct vrf *vrf;
	json_object *json_backups = NULL;
	int i;

	switch (nh->type) {
	case NEXTHOP_TYPE_IFINDEX:
		json_object_string_add(j, "nexthop",
				       ifindex2ifname(nh->ifindex, nh->vrf_id));
		break;
	case NEXTHOP_TYPE_IPV4:
		json_object_string_addf(j, "nexthop", "%pI4", &nh->gate.ipv4);
		break;
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		json_object_string_addf(j, "nexthop", "%pI4", &nh->gate.ipv4);
		json_object_string_add(j, "vrfId",
				       ifindex2ifname(nh->ifindex, nh->vrf_id));
		break;
	case NEXTHOP_TYPE_IPV6:
		json_object_string_addf(j, "nexthop", "%pI6", &nh->gate.ipv6);
		break;
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		json_object_string_addf(j, "nexthop", "%pI6", &nh->gate.ipv6);
		json_object_string_add(j, "vrfId",
				       ifindex2ifname(nh->ifindex, nh->vrf_id));
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		break;
	}

	if (nh->vrf_id != VRF_DEFAULT) {
		vrf = vrf_lookup_by_id(nh->vrf_id);
		/* When VRF is not initialized or unknown, show the target VRF as none */
		json_object_string_add(j, "targetVrf", vrf ? vrf->name : "-");
	}

	if (nh->nh_label && nh->nh_label->num_labels > 0) {
		char buf[200];

		mpls_label2str(nh->nh_label->num_labels, nh->nh_label->label,
			       buf, sizeof(buf), nh->nh_label_type, 0);
		json_object_string_add(j, "label", buf);
	}

	if (nh->weight)
		json_object_int_add(j, "weight", nh->weight);

	if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_HAS_BACKUP)) {
		json_backups = json_object_new_array();
		for (i = 0; i < nh->backup_num; i++)
			json_object_array_add(
				json_backups,
				json_object_new_int(nh->backup_idx[i]));

		json_object_object_add(j, "backupIdx", json_backups);
	}
}

/* 把一条 nexthop_hold 的“配置原文”回显为一行 “nexthop ...”，字段顺序为
 * addr / intf / onlink / nexthop-vrf / label / vni / weight / backup-idx。
 * 输出的是配置列表里的原始信息，而非解析后的 nexthop。
 */
static void nexthop_group_write_nexthop_internal(struct vty *vty,
						 const struct nexthop_hold *nh)
{
	vty_out(vty, "nexthop");

	if (nh->addr)
		vty_out(vty, " %pSU", nh->addr);

	if (nh->intf)
		vty_out(vty, " %s", nh->intf);

	if (nh->onlink)
		vty_out(vty, " onlink");

	if (nh->nhvrf_name)
		vty_out(vty, " nexthop-vrf %s", nh->nhvrf_name);

	if (nh->labels)
		vty_out(vty, " label %s", nh->labels);

	if (nh->vni)
		vty_out(vty, " vni %u", nh->vni);

	if (nh->weight)
		vty_out(vty, " weight %u", nh->weight);

	if (nh->backup_str)
		vty_out(vty, " backup-idx %s", nh->backup_str);

	vty_out(vty, "\n");
}

/* 当前运行配置中所有命名组的回显（cmd_node 的 config_write 回调）：遍历 RB
 * 树，逐组输出 “nexthop-group NAME”、resilient 行、backup-group 行与各条
 * nexthop 配置，最后 “exit” 与分隔符。始终返回 1。
 */
static int nexthop_group_write(struct vty *vty)
{
	struct nexthop_group_cmd *nhgc;
	struct nexthop_hold *nh;

	RB_FOREACH (nhgc, nhgc_entry_head, &nhgc_entries) {
		struct listnode *node;

		vty_out(vty, "nexthop-group %s\n", nhgc->name);

		if (nhgc->nhg.nhgr.buckets)
			vty_out(vty,
				" resilient buckets %u idle-timer %u unbalanced-timer %u\n",
				nhgc->nhg.nhgr.buckets,
				nhgc->nhg.nhgr.idle_timer,
				nhgc->nhg.nhgr.unbalanced_timer);

		if (nhgc->backup_list_name[0])
			vty_out(vty, " backup-group %s\n",
				nhgc->backup_list_name);

		for (ALL_LIST_ELEMENTS_RO(nhgc->nhg_list, node, nh)) {
			vty_out(vty, " ");
			nexthop_group_write_nexthop_internal(vty, nh);
		}

		vty_out(vty, "exit\n");
		vty_out(vty, "!\n");
	}

	return 1;
}

/* VRF 创建 / 就绪时调用：遍历所有命名组，对属于该 VRF 且此前因 VRF 缺失而
 * 未生效的配置项，构造 nexthop 并补挂进组（若尚不存在），触发 add_nexthop
 * 回调。用于“配置先于 VRF 存在”的补挂场景。
 */
void nexthop_group_enable_vrf(struct vrf *vrf)
{
	struct nexthop_group_cmd *nhgc;
	struct nexthop_hold *nhh;

	RB_FOREACH (nhgc, nhgc_entry_head, &nhgc_entries) {
		struct listnode *node;

		for (ALL_LIST_ELEMENTS_RO(nhgc->nhg_list, node, nhh)) {
			struct nexthop nhop;
			struct nexthop *nh;

			if (!nexthop_group_parse_nhh(&nhop, nhh))
				continue;

			nh = nexthop_exists(&nhgc->nhg, &nhop);

			if (nh)
				continue;

			if (nhop.vrf_id != vrf->vrf_id)
				continue;

			nh = nexthop_new();

			memcpy(nh, &nhop, sizeof(nhop));
			_nexthop_add(&nhgc->nhg.nexthop, nh);

			if (nhg_hooks.add_nexthop)
				nhg_hooks.add_nexthop(nhgc, nh);
		}
	}
}

/* VRF 删除时调用：遍历所有命名组，把属于该 VRF 的已生效 nexthop 从组中摘除、
 * 触发 del_nexthop 回调并释放。与 enable_vrf 互为逆操作。
 */
void nexthop_group_disable_vrf(struct vrf *vrf)
{
	struct nexthop_group_cmd *nhgc;
	struct nexthop_hold *nhh;

	RB_FOREACH (nhgc, nhgc_entry_head, &nhgc_entries) {
		struct listnode *node;

		for (ALL_LIST_ELEMENTS_RO(nhgc->nhg_list, node, nhh)) {
			struct nexthop nhop;
			struct nexthop *nh;

			if (!nexthop_group_parse_nhh(&nhop, nhh))
				continue;

			nh = nexthop_exists(&nhgc->nhg, &nhop);

			if (!nh)
				continue;

			if (nh->vrf_id != vrf->vrf_id)
				continue;

			_nexthop_del(&nhgc->nhg, nh);

			if (nhg_hooks.del_nexthop)
				nhg_hooks.del_nexthop(nhgc, nh);

			nexthop_free(nh);
		}
	}
}

/* 接口状态变化（up/down 或 ifindex 变更）时调用：遍历所有命名组，
 *   接口 up：把引用该接口、此前未生效的配置项补挂进组；
 *   接口 down：把引用该接口（按 oldifindex 匹配）的 nexthop 摘除并释放。
 * 会触发 add_nexthop / del_nexthop 回调。
 */
void nexthop_group_interface_state_change(struct interface *ifp,
					  ifindex_t oldifindex)
{
	struct nexthop_group_cmd *nhgc;
	struct nexthop_hold *nhh;

	RB_FOREACH (nhgc, nhgc_entry_head, &nhgc_entries) {
		struct listnode *node;
		struct nexthop *nh;

		if (if_is_up(ifp)) {
			for (ALL_LIST_ELEMENTS_RO(nhgc->nhg_list, node, nhh)) {
				struct nexthop nhop;

				if (!nexthop_group_parse_nhh(&nhop, nhh))
					continue;

				switch (nhop.type) {
				case NEXTHOP_TYPE_IPV4:
				case NEXTHOP_TYPE_IPV6:
				case NEXTHOP_TYPE_BLACKHOLE:
					continue;
				case NEXTHOP_TYPE_IFINDEX:
				case NEXTHOP_TYPE_IPV4_IFINDEX:
				case NEXTHOP_TYPE_IPV6_IFINDEX:
					break;
				}
				nh = nexthop_exists(&nhgc->nhg, &nhop);

				if (nh)
					continue;

				if (ifp->ifindex != nhop.ifindex)
					continue;

				nh = nexthop_new();

				memcpy(nh, &nhop, sizeof(nhop));
				_nexthop_add(&nhgc->nhg.nexthop, nh);

				if (nhg_hooks.add_nexthop)
					nhg_hooks.add_nexthop(nhgc, nh);
			}
		} else {
			struct nexthop *next_nh;

			for (nh = nhgc->nhg.nexthop; nh; nh = next_nh) {
				next_nh = nh->next;
				switch (nh->type) {
				case NEXTHOP_TYPE_IPV4:
				case NEXTHOP_TYPE_IPV6:
				case NEXTHOP_TYPE_BLACKHOLE:
					continue;
				case NEXTHOP_TYPE_IFINDEX:
				case NEXTHOP_TYPE_IPV4_IFINDEX:
				case NEXTHOP_TYPE_IPV6_IFINDEX:
					break;
				}

				if (oldifindex != nh->ifindex)
					continue;

				_nexthop_del(&nhgc->nhg, nh);

				if (nhg_hooks.del_nexthop)
					nhg_hooks.del_nexthop(nhgc, nh);

				nexthop_free(nh);
			}
		}
	}
}

/* 命令补全回调：遍历 RB 树，把所有命名组的名字加入候选集 comps，供
 * “nexthop-group <TAB>” 之类的补全使用。
 */
static void nhg_name_autocomplete(vector comps, struct cmd_token *token)
{
	struct nexthop_group_cmd *nhgc;

	RB_FOREACH (nhgc, nhgc_entry_head, &nhgc_entries) {
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, nhgc->name));
	}
}

/* 命令变量处理器表：把 token 名 NHGNAME 绑定到 nhg_name_autocomplete，
 * 末尾用 NULL 终止（CLI 框架约定）。
 */
static const struct cmd_variable_handler nhg_name_handlers[] = {
	{.tokenname = "NHGNAME", .completions = nhg_name_autocomplete},
	{.completions = NULL}};

/* 初始化命名 nexthop group 子系统：初始化 RB 树、注册补全处理器、安装
 * nexthop-group 子模式相关的所有 cli 命令，并保存上层传入的事件回调（未传
 * 的回调保持为空，即不通知）。此函数应在启动时调用一次。
 */
void nexthop_group_init(void (*new)(const char *name),
			void (*modify)(const struct nexthop_group_cmd *nhgc),
			void (*add_nexthop)(const struct nexthop_group_cmd *nhg,
					    const struct nexthop *nhop),
			void (*del_nexthop)(const struct nexthop_group_cmd *nhg,
					    const struct nexthop *nhop),
			void (*delete)(const char *name))
{
	RB_INIT(nhgc_entry_head, &nhgc_entries);

	cmd_variable_handler_register(nhg_name_handlers);

	install_node(&nexthop_group_node);
	install_element(CONFIG_NODE, &nexthop_group_cmd);
	install_element(CONFIG_NODE, &no_nexthop_group_cmd);

	install_default(NH_GROUP_NODE);
	install_element(NH_GROUP_NODE, &nexthop_group_backup_cmd);
	install_element(NH_GROUP_NODE, &no_nexthop_group_backup_cmd);
	install_element(NH_GROUP_NODE, &ecmp_nexthops_cmd);

	install_element(NH_GROUP_NODE, &nexthop_group_resilience_cmd);
	install_element(NH_GROUP_NODE, &no_nexthop_group_resilience_cmd);

	memset(&nhg_hooks, 0, sizeof(nhg_hooks));

	if (new)
		nhg_hooks.new = new;
	if (modify)
		nhg_hooks.modify = modify;
	if (add_nexthop)
		nhg_hooks.add_nexthop = add_nexthop;
	if (del_nexthop)
		nhg_hooks.del_nexthop = del_nexthop;
	if (delete)
		nhg_hooks.delete = delete;
}
