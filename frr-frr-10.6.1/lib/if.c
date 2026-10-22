// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Interface functions.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 */

/*
 * if.c —— 接口（interface）对象模型与「按名字 / 索引查、建、删接口」的核心实现
 *
 * 本文件实现 if.h 声明的接口管理逻辑，是 lib/ 中所有守护进程共享的基础设施。
 * 数据库：每个 VRF（struct vrf）内维护两棵红黑树——
 *   ifaces_by_name  （键为接口名 name_entry）
 *   ifaces_by_index （键为 ifindex index_entry）
 * 所有接口都挂在某个 VRF 下，因此任何查找都要先确定「哪个 VRF」。
 *
 * 生命周期：
 *   创建：if_new 分配结构（ifindex=IFINDEX_INTERNAL，名字为空）-> if_set_name
 *         起名 -> 挂进名字 / 索引树；对外通常经由 if_get_by_name。
 *   删除：if_delete_retain 只标记为内部接口并保留结构（避免丢配置），
 *         if_delete 才真正从表里摘除并释放。
 *
 * 几类查找入口（务必区分）：
 *   if_lookup_by_name / _vrf  : 按名字查，只查不建。
 *   if_lookup_by_index        : 按 ifindex 查，只查不建。
 *   if_get_by_name            : 按名字取，不存在则新建（有副作用！）。
 *   if_get_vrf_loopback       : 取 VRF 的环回接口（可懒创建）。
 *   *_all_vrf / _next         : 跨 VRF 或取「下一个」的变体。
 *
 * 状态同步：
 *   if_update_state* 系列把接口属性写进 YANG state 树（受 if_notify_oper_changes
 *   开关控制）；via_zapi 系列封装了守护进程处理 ZAPI 事件时要触发的 hook。
 *
 * 易踩的坑：
 *   改 ifindex 必须走 if_set_index()；名字建好后不要改；lookup 与 get 语义不同。
 */

#include <zebra.h>

#include <net/if.h>

#ifdef GNU_LINUX
#include <linux/if.h>
#endif /* GNU_LINUX */

#include "linklist.h"
#include "vector.h"
#include "lib_errors.h"
#include "vty.h"
#include "command.h"
#include "vrf.h"
#include "if.h"
#include "sockunion.h"
#include "prefix.h"
#include "memory.h"
#include "table.h"
#include "buffer.h"
#include "log.h"
#include "northbound_cli.h"
#include "admin_group.h"
#include "lib/if_clippy.c"


/*
 * if_notify_oper_changes —— 由拥有者（通常是 zebra）设置的全局开关，
 * 为真时才把接口 oper 状态变化写入 YANG state 通知订阅者。
 */
/* Set by the owner (zebra). */
bool if_notify_oper_changes;

/*
 * 各内存类型说明：
 * IF / IFDESC               : struct interface 本体 / 其 desc 描述串。
 * CONNECTED / NBR_CONNECTED : 直连地址 / 邻居直连地址。
 * CONNECTED_LABEL           : connected 的 label 串（非 static，跨文件可见）。
 * IF_LINK_PARAMS            : TE 链路参数结构。
 */
DEFINE_MTYPE_STATIC(LIB, IF, "Interface");
DEFINE_MTYPE_STATIC(LIB, IFDESC, "Intf Desc");
DEFINE_MTYPE_STATIC(LIB, CONNECTED, "Connected");
DEFINE_MTYPE_STATIC(LIB, NBR_CONNECTED, "Neighbor Connected");
DEFINE_MTYPE(LIB, CONNECTED_LABEL, "Connected interface label");
DEFINE_MTYPE_STATIC(LIB, IF_LINK_PARAMS, "Informational Link Parameters");

/*
 * 前向声明与红黑树实例化：
 *   if_set_name / if_lookup_by_ifindex / if_lookup_by_index_all_vrf : 本文件内先声明
 *     后定义，供更靠前的函数引用。
 *   RB_GENERATE 为名字树（用 if_cmp_func）与索引树（用 if_cmp_index_func）生成实现。
 */
static void if_set_name(struct interface *ifp, const char *name);
static struct interface *if_lookup_by_ifindex(ifindex_t ifindex,
					      vrf_id_t vrf_id);
static struct interface *if_lookup_by_index_all_vrf(ifindex_t ifindex);
static int if_cmp_index_func(const struct interface *ifp1,
			     const struct interface *ifp2);
RB_GENERATE(if_name_head, interface, name_entry, if_cmp_func);
RB_GENERATE(if_index_head, interface, index_entry, if_cmp_index_func);

/*
 * 实例化接口对象调试类型，以及 if_add / if_del、if_real / if_unreal、
 * if_up / if_down 三对 hook 的具体实现（声明见 if.h）。
 * DEFINE_HOOK 生成 hook_call 函数，DEFINE_KOOH 生成反向调用函数。
 */
DEFINE_QOBJ_TYPE(interface);

DEFINE_HOOK(if_add, (struct interface *ifp), (ifp));
DEFINE_KOOH(if_del, (struct interface *ifp), (ifp));

DEFINE_HOOK(if_real, (struct interface *ifp), (ifp));
DEFINE_KOOH(if_unreal, (struct interface *ifp), (ifp));

DEFINE_HOOK(if_up, (struct interface *ifp), (ifp));
DEFINE_KOOH(if_down, (struct interface *ifp), (ifp));

/* Compare interface names, returning an integer greater than, equal to, or
 * less than 0, (following the strcmp convention), according to the
 * relationship between ifp1 and ifp2.  Interface names consist of an
 * alphabetic prefix and a numeric suffix.  The primary sort key is
 * lexicographic by name, and then numeric by number.  No number sorts
 * before all numbers.  Examples: de0 < de1, de100 < fxp0 < xl0, devpty <
 * devpty0, de0 < del0
 */
/*
 * if_cmp_name_func —— 接口名比较函数，遵循 strcmp 约定（<0 / 0 / >0）。
 * 名字被看作「字母前缀 + 数字后缀」：先按字母部分字典序比较，字母相同再按数字
 * 大小比较；「没有数字」排在所有数字之前（故 de0 < del0）。
 * 例子：de0 < de1，de100 < fxp0 < xl0，devpty < devpty0。
 * 数字相等时还会比较数字串长度，以区分 foo-1 与 foo-001。
 * 它是名字红黑树的排序键，保证遍历接口时按“自然序”排列。
 */
int if_cmp_name_func(const char *p1, const char *p2)
{
	unsigned int l1, l2;
	long int x1, x2;
	int res;

	while (*p1 && *p2) {
		char *tmp1, *tmp2;

		/* look up to any number */
		l1 = strcspn(p1, "0123456789");
		l2 = strcspn(p2, "0123456789");

		/* name lengths are different -> compare names */
		if (l1 != l2)
			return (strcmp(p1, p2));

		/* Note that this relies on all numbers being less than all
		 * letters, so
		 * that de0 < del0.
		 */
		res = strncmp(p1, p2, l1);

		/* names are different -> compare them */
		if (res)
			return res;

		/* with identical name part, go to numeric part */
		p1 += l1;
		p2 += l1;

		if (!*p1 && !*p2)
			return 0;
		if (!*p1)
			return -1;
		if (!*p2)
			return 1;

		x1 = strtol(p1, &tmp1, 10);
		x2 = strtol(p2, &tmp2, 10);

		/* let's compare numbers now */
		if (x1 < x2)
			return -1;
		if (x1 > x2)
			return 1;

		/* Compare string if numbers are equal (distinguish foo-1 from foo-001) */
		l1 = strspn(p1, "0123456789");
		l2 = strspn(p2, "0123456789");
		if (l1 != l2)
			return (strcmp(p1, p2));

		/* Continue to parse the rest of the string */
		p1 = (const char *)tmp1;
		p2 = (const char *)tmp2;

		/* numbers were equal, lets do it again..
		(it happens with name like "eth123.456:789") */
	}
	if (*p1)
		return 1;
	if (*p2)
		return -1;
	return 0;
}

/*
 * if_cmp_func —— 红黑树通用比较入口，转调 if_cmp_name_func 比较两个接口的名字。
 */
int if_cmp_func(const struct interface *ifp1, const struct interface *ifp2)
{
	return if_cmp_name_func(ifp1->name, ifp2->name);
}

/*
 * if_cmp_index_func —— if_index_head 树的比较函数，按 ifindex 数值比较。
 */
static int if_cmp_index_func(const struct interface *ifp1,
			     const struct interface *ifp2)
{
	if (ifp1->ifindex == ifp2->ifindex)
		return 0;
	else if (ifp1->ifindex > ifp2->ifindex)
		return 1;
	else
		return -1;
}

/*
 * ifp_connected_free —— 链表释放回调：把 arg 当作 connected 并释放。
 * 包一层是因为 connected_free 需要双指针，而链表回调只给单指针。
 */
static void ifp_connected_free(void *arg)
{
	struct connected *c = arg;

	connected_free(&c);
}

/*
 * if_new —— 分配并初始化一个 struct interface（不设名字）。
 * 断言 vrf 非空；ifindex 置 IFINDEX_INTERNAL、名字置空；初始化 connected 链表；
 * 建立 nbr_connected 链表并设其释放回调；默认打开 link-detection；注册 QOBJ。
 * 返回新接口，名字随后由 if_set_name 设置。
 */
/* Create new interface structure. */
static struct interface *if_new(struct vrf *vrf)
{
	struct interface *ifp;

	assert(vrf);

	ifp = XCALLOC(MTYPE_IF, sizeof(struct interface));

	ifp->ifindex = IFINDEX_INTERNAL;
	ifp->name[0] = '\0';

	ifp->vrf = vrf;

	if_connected_init(ifp->connected);

	ifp->nbr_connected = list_new();
	ifp->nbr_connected->del = (void (*)(void *))nbr_connected_free;

	/* Enable Link-detection by default */
	SET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);

	QOBJ_REG(ifp, interface);
	return ifp;
}

/*
 * if_new_via_zapi —— 守护进程收到「新增接口」ZAPI 时调用，触发 if_real hook。
 */
void if_new_via_zapi(struct interface *ifp)
{
	hook_call(if_real, ifp);
}

/*
 * if_destroy_via_zapi —— 守护进程收到「销毁接口」ZAPI 时调用。
 * 先触发 if_unreal hook；把当前 ifindex 存进 oldifindex 后置回 IFINDEX_INTERNAL；
 * 若该接口并非用户配置的（!configured）则直接 if_delete 释放，否则保留结构。
 */
void if_destroy_via_zapi(struct interface *ifp)
{
	hook_call(if_unreal, ifp);

	ifp->oldifindex = ifp->ifindex;
	if_set_index(ifp, IFINDEX_INTERNAL);

	if (!ifp->configured)
		if_delete(&ifp);
}

/*
 * if_up_via_zapi / if_down_via_zapi —— 收到接口 up / down 的 ZAPI 时调用，
 * 分别触发 if_up / if_down hook。
 */
void if_up_via_zapi(struct interface *ifp)
{
	hook_call(if_up, ifp);
}

/*
 * if_down_via_zapi —— 收到接口 down 的 ZAPI 时调用，触发 if_down hook。
 */
void if_down_via_zapi(struct interface *ifp)
{
	hook_call(if_down, ifp);
}

/*
 * if_update_state_metric —— 更新接口 metric。
 * 值没变直接返回；否则写回字段，并在开启通知且已有 state 节点时更新 YANG。
 */
void if_update_state_metric(struct interface *ifp, uint32_t metric)
{
	if (ifp->metric == metric)
		return;
	ifp->metric = metric;
	if (ifp->state && if_notify_oper_changes)
		nb_op_updatef(ifp->state, "metric", "%u", ifp->metric);
}

/*
 * if_update_state_mtu —— 更新 IPv4 MTU，语义同 metric（无变化则跳过）。
 */
void if_update_state_mtu(struct interface *ifp, uint mtu)
{
	if (ifp->mtu == mtu)
		return;
	ifp->mtu = mtu;
	if (ifp->state && if_notify_oper_changes)
		nb_op_updatef(ifp->state, "mtu", "%u", ifp->mtu);
}

/*
 * if_update_state_mtu6 —— 更新 IPv6 MTU。
 */
void if_update_state_mtu6(struct interface *ifp, uint mtu)
{
	if (ifp->mtu6 == mtu)
		return;
	ifp->mtu6 = mtu;
	if (ifp->state && if_notify_oper_changes)
		nb_op_updatef(ifp->state, "mtu6", "%u", ifp->mtu6);
}

/*
 * if_update_state_hw_addr —— 更新硬件地址与长度。
 * 长度与内容都相同时直接返回；否则 memcpy 后同步到 YANG 的 phy-address。
 */
void if_update_state_hw_addr(struct interface *ifp, const uint8_t *hw_addr, uint len)
{
	if (len == (uint)ifp->hw_addr_len && (len == 0 || !memcmp(hw_addr, ifp->hw_addr, len)))
		return;
	memcpy(ifp->hw_addr, hw_addr, len);
	ifp->hw_addr_len = len;
	if (ifp->state && if_notify_oper_changes)
		nb_op_updatef(ifp->state, "phy-address", "%pEA", (struct ethaddr *)ifp->hw_addr);
}

/*
 * if_update_state_speed —— 更新接口速率。
 */
void if_update_state_speed(struct interface *ifp, uint32_t speed)
{
	if (ifp->speed == speed)
		return;
	ifp->speed = speed;
	if (ifp->state && if_notify_oper_changes)
		nb_op_updatef(ifp->state, "speed", "%u", ifp->speed);
}

/*
 * if_update_state —— 一次性把接口的关键属性刷新进 YANG state 节点。
 * 注：顶部那行“整体替换”的调用被注释掉了，因为当前还没有 patch 支持，
 * 逐字段更新可避免产生重复的 REPLACE 消息。
 */
void if_update_state(struct interface *ifp)
{
	struct lyd_node *state = ifp->state;

	if (!state || !if_notify_oper_changes)
		return;

	/*
	 * Remove top level container update when we have patch support, for now
	 * this keeps us from generating 6 separate REPLACE messages though.
	 */
	// nb_op_update(state, ".", NULL);
	nb_op_updatef(state, "if-index", "%d", ifp->ifindex);
	nb_op_updatef(state, "mtu", "%u", ifp->mtu);
	nb_op_updatef(state, "mtu6", "%u", ifp->mtu);
	nb_op_updatef(state, "speed", "%u", ifp->speed);
	nb_op_updatef(state, "metric", "%u", ifp->metric);
	nb_op_updatef(state, "phy-address", "%pEA", (struct ethaddr *)ifp->hw_addr);
}

/*
 * if_update_state_remove —— 从 YANG state 树删除该接口的 state 节点并释放本地副本。
 * 未开通知或接口无名字则直接返回。
 * netns 后端时接口名带 "vrfname:ifname" 前缀，普通后端只用接口名。
 */
static void if_update_state_remove(struct interface *ifp)
{
	if (!if_notify_oper_changes || ifp->name[0] == 0)
		return;

	if (vrf_is_backend_netns())
		nb_op_update_delete_pathf(NULL, "/frr-interface:lib/interface[name=\"%s:%s\"]/state",
					  ifp->vrf->name, ifp->name);
	else
		nb_op_update_delete_pathf(NULL, "/frr-interface:lib/interface[name=\"%s\"]/state",
					  ifp->name);
	if (ifp->state) {
		lyd_free_all(ifp->state);
		ifp->state = NULL;
	}
}

/*
 * if_update_state_add —— 在 YANG state 树中新建该接口的 state 节点并记到 ifp->state。
 * 未开通知或接口无名字则直接返回（保持 ifp->state 为空）。
 * netns 后端同样使用 "vrfname:ifname" 组合名。
 */
static void if_update_state_add(struct interface *ifp)
{
	if (!if_notify_oper_changes || ifp->name[0] == 0)
		return;

	if (vrf_is_backend_netns())
		ifp->state = nb_op_update_pathf(NULL,
						"/frr-interface:lib/interface[name=\"%s:%s\"]/state",
						NULL, ifp->vrf->name, ifp->name);
	else
		ifp->state = nb_op_update_pathf(NULL,
						"/frr-interface:lib/interface[name=\"%s\"]/state",
						NULL, ifp->name);
}

/*
 * if_create_name —— 在给定 vrf 内新建一个带名字的接口。
 * 流程：if_new 分配 -> if_set_name 设定名字（挂进名字树）-> 若在通知且已有 state
 * 则刷新 YANG state -> 触发 if_add hook。返回新接口。
 * 这是「按名字新建接口」的底层入口，if_get_by_name 找不到时最终走到这里。
 */
static struct interface *if_create_name(const char *name, struct vrf *vrf)
{
	struct interface *ifp;

	ifp = if_new(vrf);

	if_set_name(ifp, name);

	if (if_notify_oper_changes && ifp->state)
		if_update_state(ifp);

	hook_call(if_add, ifp);

	return ifp;
}

/* Create new interface structure. */
/*
 * if_update_to_new_vrf —— 把一个已有接口迁移到新的 VRF。
 * 步骤：从旧 VRF 摘除（名字 / 索引树）并删除其 state 节点；vrf_get 拿到目标 VRF；
 * 更新 ifp->vrf；在新 VRF 内重新插入名字 / 索引树并重建 state。
 * 注意：只有非空名字才动名字树，只有非内部 ifindex 才动索引树。
 */
void if_update_to_new_vrf(struct interface *ifp, vrf_id_t vrf_id)
{
	struct vrf *old_vrf, *vrf;

	/* remove interface from old master vrf list */
	old_vrf = ifp->vrf;

	if (ifp->name[0] != '\0') {
		IFNAME_RB_REMOVE(old_vrf, ifp);
		if_update_state_remove(ifp);
	}

	if (ifp->ifindex != IFINDEX_INTERNAL)
		IFINDEX_RB_REMOVE(old_vrf, ifp);

	vrf = vrf_get(vrf_id, NULL);
	ifp->vrf = vrf;

	if (ifp->name[0] != '\0') {
		IFNAME_RB_INSERT(vrf, ifp);
		if_update_state_add(ifp);
		if_update_state(ifp);
	}

	if (ifp->ifindex != IFINDEX_INTERNAL)
		IFINDEX_RB_INSERT(vrf, ifp);
}


/* Delete interface structure. */
/*
 * if_delete_retain —— 删除接口内容但保留结构体本身。
 * 触发 if_del hook、注销 QOBJ，释放直连地址链表与邻居直连地址链表。
 * 结构仍留在接口表里，以便保留挂在其上的配置（伪接口语义）。
 */
void if_delete_retain(struct interface *ifp)
{
	struct connected *ifc;

	hook_call(if_del, ifp);
	QOBJ_UNREG(ifp);

	/* Free connected address list */
	while ((ifc = if_connected_pop(ifp->connected)))
		ifp_connected_free(ifc);

	/* Free connected nbr address list */
	list_delete_all_node(ifp->nbr_connected);
}

/* Delete and free interface structure. */
/*
 * if_delete —— 真正删除并释放接口。
 * 先从名字 / 索引两棵树摘除，再 if_delete_retain 释放内容，最后释放 connected
 * 容器、nbr_connected 链表、link_params、desc 与结构本体，并把 *ifp 置空。
 * 参数为双指针，保证调用方手里的指针失效。
 */
void if_delete(struct interface **ifp)
{
	struct interface *ptr = *ifp;
	struct vrf *vrf = ptr->vrf;

	IFNAME_RB_REMOVE(vrf, ptr);
	if (ptr->ifindex != IFINDEX_INTERNAL)
		IFINDEX_RB_REMOVE(vrf, ptr);

	if_delete_retain(ptr);

	if_connected_fini(ptr->connected);
	list_delete(&ptr->nbr_connected);

	if_link_params_free(ptr);

	XFREE(MTYPE_IFDESC, ptr->desc);

	if_update_state_remove(ptr);

	XFREE(MTYPE_IF, ptr);
	*ifp = NULL;
}

/* Used only internally to check within VRF only */
/*
 * if_lookup_by_ifindex —— 仅在指定 VRF 内按 ifindex 查找（内部使用）。
 * vrf_lookup_by_id 找不到 VRF 则返回 NULL；用栈上临时 if_tmp 只填 ifindex 去树里找。
 */
static struct interface *if_lookup_by_ifindex(ifindex_t ifindex,
					      vrf_id_t vrf_id)
{
	struct vrf *vrf;
	struct interface if_tmp;

	vrf = vrf_lookup_by_id(vrf_id);
	if (!vrf)
		return NULL;

	if_tmp.ifindex = ifindex;
	return RB_FIND(if_index_head, &vrf->ifaces_by_index, &if_tmp);
}

/* Interface existence check by index. */
/*
 * if_lookup_by_index —— 按 ifindex 查接口，行为依 VRF 后端不同：
 *   NETNS 后端    : 在本 VRF（vrf_id）内查。
 *   VRF_LITE 后端 : 跨所有 VRF 查（if_lookup_by_index_all_vrf）。
 * 其它后端返回 NULL。
 */
struct interface *if_lookup_by_index(ifindex_t ifindex, vrf_id_t vrf_id)
{
	switch (vrf_get_backend()) {
	case VRF_BACKEND_NETNS:
		return(if_lookup_by_ifindex(ifindex, vrf_id));
	case VRF_BACKEND_VRF_LITE:
		return(if_lookup_by_index_all_vrf(ifindex));
	}
	return NULL;
}

/* Interface existence check by index. */
/*
 * if_vrf_lookup_by_index_next —— 在给定 VRF 内取「ifindex 比参数更大的下一个接口」，
 * ifindex 传 0 时取最小 ifindex 的接口。
 * 会跳过 VRF 设备自身（if_is_vrf），常用于遍历接口或分配索引。
 */
struct interface *if_vrf_lookup_by_index_next(ifindex_t ifindex,
					      vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	struct interface *tmp_ifp;
	bool found = false;

	if (!vrf)
		return NULL;

	if (ifindex == 0) {
		tmp_ifp = RB_MIN(if_index_head, &vrf->ifaces_by_index);
		/* skip the vrf interface */
		if (tmp_ifp && if_is_vrf(tmp_ifp))
			ifindex = tmp_ifp->ifindex;
		else
			return tmp_ifp;
	}

	RB_FOREACH (tmp_ifp, if_index_head, &vrf->ifaces_by_index) {
		if (found) {
			/* skip the vrf interface */
			if (tmp_ifp && if_is_vrf(tmp_ifp))
				continue;
			else
				return tmp_ifp;
		}
		if (tmp_ifp->ifindex == ifindex)
			found = true;
	}
	return NULL;
}

/*
 * ifindex2ifname —— 按 ifindex 取接口名；查不到返回字符串 "unknown"。
 * 只用内部数据，不产生系统调用。
 */
const char *ifindex2ifname(ifindex_t ifindex, vrf_id_t vrf_id)
{
	struct interface *ifp;

	return ((ifp = if_lookup_by_index(ifindex, vrf_id)) != NULL)
		       ? ifp->name
		       : "unknown";
}

/*
 * ifname2ifindex —— 按名字取 ifindex；查不到返回 IFINDEX_INTERNAL。
 */
ifindex_t ifname2ifindex(const char *name, vrf_id_t vrf_id)
{
	struct interface *ifp;

	return ((ifp = if_lookup_by_name(name, vrf_id)) != NULL)
		       ? ifp->ifindex
		       : IFINDEX_INTERNAL;
}

/* Interface existence check by interface name. */
/*
 * if_lookup_by_name —— 在给定 vrf_id 内按名字查接口，只查不建。
 * name 为空或长度达到 IFNAMSIZ（非法 / 截断）时返回 NULL；
 * 用栈上临时 if_tmp 拷贝名字后到名字树查找。
 */
struct interface *if_lookup_by_name(const char *name, vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	struct interface if_tmp;

	if (!vrf || !name || strnlen(name, IFNAMSIZ) == IFNAMSIZ)
		return NULL;

	strlcpy(if_tmp.name, name, sizeof(if_tmp.name));
	return RB_FIND(if_name_head, &vrf->ifaces_by_name, &if_tmp);
}

/*
 * if_lookup_by_name_vrf —— 直接以 struct vrf * 为上下文按名字查接口，只查不建。
 * 相比 if_lookup_by_name 省去一次 vrf_lookup_by_id。
 */
struct interface *if_lookup_by_name_vrf(const char *name, struct vrf *vrf)
{
	struct interface if_tmp;

	if (!name || strnlen(name, IFNAMSIZ) == IFNAMSIZ)
		return NULL;

	strlcpy(if_tmp.name, name, sizeof(if_tmp.name));
	return RB_FIND(if_name_head, &vrf->ifaces_by_name, &if_tmp);
}

/*
 * if_lookup_by_name_all_vrf —— 跨所有 VRF 按名字查接口，返回第一个命中者。
 * 遍历 vrfs_by_name 树，逐个调用 if_lookup_by_name_vrf。
 */
static struct interface *if_lookup_by_name_all_vrf(const char *name)
{
	struct vrf *vrf;
	struct interface *ifp;

	if (!name || strnlen(name, IFNAMSIZ) == IFNAMSIZ)
		return NULL;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		ifp = if_lookup_by_name_vrf(name, vrf);
		if (ifp)
			return ifp;
	}

	return NULL;
}

/*
 * if_lookup_by_index_all_vrf —— 跨所有 VRF 按 ifindex 查接口。
 * ifindex 为 IFINDEX_INTERNAL 直接返回 NULL；遍历 vrfs_by_id 树逐个在 VRF 内查。
 */
static struct interface *if_lookup_by_index_all_vrf(ifindex_t ifindex)
{
	struct vrf *vrf;
	struct interface *ifp;

	if (ifindex == IFINDEX_INTERNAL)
		return NULL;

	RB_FOREACH (vrf, vrf_id_head, &vrfs_by_id) {
		ifp = if_lookup_by_ifindex(ifindex, vrf->vrf_id);
		if (ifp)
			return ifp;
	}

	return NULL;
}

/* Lookup interface by IP address.
 *
 * supersedes if_lookup_exact_address(), which didn't care about up/down
 * state.  but all users we have either only care if the address is local
 * (=> use if_address_is_local() please), or care about UP interfaces before
 * anything else
 *
 * to accept only UP interfaces, check if_is_up() on the returned ifp.
 */
/*
 * if_lookup_address_local —— 按 IP 地址查「拥有该本地地址」的接口。
 * 只支持 AF_INET / AF_INET6。遍历 VRF 内所有接口的直连地址做完全匹配；
 * 优先返回状态 UP 的接口，若都不 UP 则返回第一个命中的（best_down），都没有返回 NULL。
 * 用途：判断一个地址是否本机地址（if_address_is_local 即基于它）。
 */
struct interface *if_lookup_address_local(const void *src, int family,
					  vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	struct interface *ifp, *best_down = NULL;
	struct prefix *p;
	struct connected *c;

	if (family != AF_INET && family != AF_INET6)
		return NULL;

	FOR_ALL_INTERFACES (vrf, ifp) {
		frr_each (if_connected, ifp->connected, c) {
			p = c->address;

			if (!p || p->family != family)
				continue;

			if (family == AF_INET) {
				if (!IPV4_ADDR_SAME(&p->u.prefix4,
						    (struct in_addr *)src))
					continue;
			} else if (family == AF_INET6) {
				if (!IPV6_ADDR_SAME(&p->u.prefix6,
						    (struct in6_addr *)src))
					continue;
			}

			if (if_is_up(ifp))
				return ifp;
			if (!best_down)
				best_down = ifp;
		}
	}
	return best_down;
}

/* Lookup interface by IP address. */
/*
 * if_lookup_address —— 按 IP 地址查「包含该地址」的直连地址条目（最具体前缀优先）。
 * 把入参构造成全位数前缀 addr，遍历所有接口的直连地址，用 prefix_match 判断包含关系，
 * 保留 prefixlen 最大的那个条目返回（做最长前缀匹配）。family 非法时断言失败。
 */
struct connected *if_lookup_address(const void *matchaddr, int family,
				    vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	struct prefix addr;
	int bestlen = 0;
	struct interface *ifp;
	struct connected *c;
	struct connected *match;

	if (family == AF_INET) {
		addr.family = AF_INET;
		addr.u.prefix4 = *((struct in_addr *)matchaddr);
		addr.prefixlen = IPV4_MAX_BITLEN;
	} else if (family == AF_INET6) {
		addr.family = AF_INET6;
		addr.u.prefix6 = *((struct in6_addr *)matchaddr);
		addr.prefixlen = IPV6_MAX_BITLEN;
	} else
		assert(!"Attempted lookup of family not supported");

	match = NULL;

	FOR_ALL_INTERFACES (vrf, ifp) {
		frr_each (if_connected, ifp->connected, c) {
			if (c->address && (c->address->family == family) &&
			    prefix_match(CONNECTED_PREFIX(c), &addr) &&
			    (c->address->prefixlen > bestlen)) {
				bestlen = c->address->prefixlen;
				match = c;
			}
		}
	}
	return match;
}

/*
 * if_lookup_prefix —— 按前缀精确查接口：某接口上有与该前缀完全相同的直连地址则返回该接口。
 * 比较用 prefix_cmp（含长度），属精确匹配。
 */
/* Lookup interface by prefix */
struct interface *if_lookup_prefix(const struct prefix *prefix, vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	struct interface *ifp;
	struct connected *c;

	FOR_ALL_INTERFACES (vrf, ifp) {
		frr_each (if_connected, ifp->connected, c) {
			if (prefix_cmp(c->address, prefix) == 0) {
				return ifp;
			}
		}
	}
	return NULL;
}

/*
 * if_lookup_by_hwaddr —— 按硬件地址查接口，可能命中多个。
 * result 为输出参数，命中时用 MTYPE_TMP 分配一个接口指针数组并填入，
 * 由调用方负责释放；返回命中个数（0 时 result 不被写）。
 */
size_t if_lookup_by_hwaddr(const uint8_t *hw_addr, size_t addrsz,
			   struct interface ***result, vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);

	struct list *rs = list_new();
	struct interface *ifp;

	FOR_ALL_INTERFACES (vrf, ifp) {
		if (ifp->hw_addr_len == (int)addrsz
		    && !memcmp(hw_addr, ifp->hw_addr, addrsz))
			listnode_add(rs, ifp);
	}

	if (rs->count) {
		*result = XCALLOC(MTYPE_TMP,
				  sizeof(struct interface *) * rs->count);
		list_to_array(rs, (void **)*result, rs->count);
	}

	int count = rs->count;

	list_delete(&rs);

	return count;
}

/* Get the VRF loopback interface, i.e. the loopback on the default VRF
 * or the VRF interface.
 */
/*
 * if_get_vrf_loopback —— 取指定 VRF 的环回接口（默认 VRF 的 lo，或 VRF 接口本身）。
 * 遍历该 VRF 接口，返回第一个 if_is_loopback 为真的；没有返回 NULL。只查不建。
 */
struct interface *if_get_vrf_loopback(vrf_id_t vrf_id)
{
	struct interface *ifp = NULL;
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);

	FOR_ALL_INTERFACES (vrf, ifp)
		if (if_is_loopback(ifp))
			return ifp;

	return NULL;
}

/* Get interface by name if given name interface doesn't exist create
   one. */
/*
 * if_get_by_name —— 按名字取接口，不存在则新建（注意有副作用）。
 * 依 VRF 后端分两条路：
 *   NETNS    : 在 vrf_id 对应 VRF 内查；命中则返回（若 VRF 归属变了且非 VRF_UNKNOWN
 *              则先迁到新 VRF）；未命中则继续往下新建。
 *   VRF_LITE : 先跨 VRF 查（if_lookup_by_name_all_vrf），命中则返回并同样按需迁移；
 *              未命中则取（必要时建）目标 VRF 再往下新建。
 * 新建走 if_create_name。
 */
struct interface *if_get_by_name(const char *name, vrf_id_t vrf_id,
				 const char *vrf_name)
{
	struct interface *ifp = NULL;
	struct vrf *vrf = NULL;

	switch (vrf_get_backend()) {
	case VRF_BACKEND_NETNS:
		vrf = vrf_get(vrf_id, vrf_name);
		assert(vrf);

		ifp = if_lookup_by_name_vrf(name, vrf);
		if (ifp) {
			/* If it came from the kernel or by way of zclient,
			 * believe it and update the ifp accordingly.
			 */
			if (ifp->vrf->vrf_id != vrf_id && vrf_id != VRF_UNKNOWN)
				if_update_to_new_vrf(ifp, vrf_id);

			return ifp;
		}

		break;
	case VRF_BACKEND_VRF_LITE:
		ifp = if_lookup_by_name_all_vrf(name);
		if (ifp) {
			/* If it came from the kernel or by way of zclient,
			 * believe it and update the ifp accordingly.
			 */
			if (ifp->vrf->vrf_id != vrf_id && vrf_id != VRF_UNKNOWN)
				if_update_to_new_vrf(ifp, vrf_id);

			return ifp;
		}

		vrf = vrf_get(vrf_id, vrf_name);
		assert(vrf);

		break;
	}

	return if_create_name(name, vrf);
}

/*
 * if_set_index —— 设置接口的 ifindex（唯一正确的改索引方式）。
 * 若目标索引已被同 VRF 内别的接口占用，直接返回 -1（避免插入冲突）；
 * 已有旧索引则先从索引树移除，改字段后（按需通知 YANG）再插入新索引。
 * 返回 0 成功，-1 失败。
 */
int if_set_index(struct interface *ifp, ifindex_t ifindex)
{
	if (ifp->ifindex == ifindex)
		return 0;

	/*
	 * If there is already an interface with this ifindex, we will collide
	 * on insertion, so don't even try.
	 */
	if (if_lookup_by_ifindex(ifindex, ifp->vrf->vrf_id))
		return -1;

	if (ifp->ifindex != IFINDEX_INTERNAL)
		IFINDEX_RB_REMOVE(ifp->vrf, ifp);

	ifp->ifindex = ifindex;

	if (if_notify_oper_changes)
		nb_op_updatef(ifp->state, "if-index", "%d", ifp->ifindex);

	if (ifp->ifindex != IFINDEX_INTERNAL) {
		/*
		 * This should never happen, since we checked if there was
		 * already an interface with the desired ifindex at the top of
		 * the function. Nevertheless.
		 */
		if (IFINDEX_RB_INSERT(ifp->vrf, ifp))
			return -1;
	}

	return 0;
}

/*
 * if_set_name —— 设置 / 修改接口名。
 * 名字相同直接返回；已有旧名字则从名字树移除并删 state；strlcpy 新名字；
 * 非空名字再插回名字树并重建 state。名字是接口在名字树里的键，改名务必走这里。
 */
static void if_set_name(struct interface *ifp, const char *name)
{
	if (if_cmp_name_func(ifp->name, name) == 0)
		return;

	if (ifp->name[0] != '\0') {
		IFNAME_RB_REMOVE(ifp->vrf, ifp);
		if_update_state_remove(ifp);
	}

	strlcpy(ifp->name, name, sizeof(ifp->name));

	if (ifp->name[0] != '\0') {
		IFNAME_RB_INSERT(ifp->vrf, ifp);
		if_update_state_add(ifp);
	}
}

/* Does interface up ? */
/*
 * if_is_up —— 接口管理 / 载波是否 UP（检查 IFF_UP 标志位）。
 */
int if_is_up(const struct interface *ifp)
{
	return ifp->flags & IFF_UP;
}

/* Is interface running? */
/*
 * if_is_running —— 接口是否 RUNNING（IFF_RUNNING）。
 */
int if_is_running(const struct interface *ifp)
{
	return ifp->flags & IFF_RUNNING;
}

/* Is the interface operative, eg. either UP & RUNNING
   or UP & !ZEBRA_INTERFACE_LINK_DETECTION and
   if ptm checking is enabled, then ptm check has passed */
/*
 * if_is_operative —— 接口是否「可工作」。
 * 需要 IFF_UP；且满足其一：(IFF_RUNNING 且有下层链路、PTM 通过或未启用 PTM)，
 * 或者该接口未开启 link-detection（ZEBRA_INTERFACE_LINKDETECTION）。
 */
int if_is_operative(const struct interface *ifp)
{
	return ((ifp->flags & IFF_UP) &&
		(((ifp->flags & IFF_RUNNING)
#ifdef IFF_LOWER_UP
		  && (ifp->flags & IFF_LOWER_UP)
#endif /* IFF_LOWER_UP */
		  && (ifp->ptm_status || !ifp->ptm_enable)) ||
		 !CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION)));
}

/* Is the interface operative, eg. either UP & RUNNING
   or UP & !ZEBRA_INTERFACE_LINK_DETECTION, without PTM check */
/*
 * if_is_no_ptm_operative —— 同 if_is_operative，但不做 PTM 检查。
 */
int if_is_no_ptm_operative(const struct interface *ifp)
{
	return ((ifp->flags & IFF_UP) &&
		(((ifp->flags & IFF_RUNNING)
#ifdef IFF_LOWER_UP
		  && (ifp->flags & IFF_LOWER_UP)
#endif /* IFF_LOWER_UP */
			  ) ||
		 !CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION)));
}

/* Is this loopback interface ? */
/*
 * if_is_loopback_exact —— 是否「精确的」环回接口。
 * 只要 IFF_LOOPBACK / IFF_NOXMIT / IFF_VIRTUAL 任一位置位即算。
 * 注意不同平台这些标志语义可能有差异（见函数内原有注释）。
 */
int if_is_loopback_exact(const struct interface *ifp)
{
	/* XXX: Do this better, eg what if IFF_WHATEVER means X on platform M
	 * but Y on platform N?
	 */
	return (ifp->flags & (IFF_LOOPBACK | IFF_NOXMIT | IFF_VIRTUAL));
}

/* Check interface is VRF */
/*
 * if_is_vrf —— 该接口是否 VRF 设备（按 ZEBRA_INTERFACE_VRF_LOOPBACK 状态位判断）。
 */
int if_is_vrf(const struct interface *ifp)
{
	return CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_VRF_LOOPBACK);
}

/* Should this interface be treated as a loopback? */
/*
 * if_is_loopback —— 是否应把它当作环回接口看待：精确环回或 VRF 设备都算。
 */
bool if_is_loopback(const struct interface *ifp)
{
	if (if_is_loopback_exact(ifp) || if_is_vrf(ifp))
		return true;

	return false;
}

/* Does this interface support broadcast ? */
/*
 * if_is_broadcast —— 接口是否支持广播（IFF_BROADCAST）。
 */
int if_is_broadcast(const struct interface *ifp)
{
	return ifp->flags & IFF_BROADCAST;
}

/* Does this interface support pointopoint ? */
/*
 * if_is_pointopoint —— 接口是否点对点（IFF_POINTOPOINT）。
 */
int if_is_pointopoint(const struct interface *ifp)
{
	return ifp->flags & IFF_POINTOPOINT;
}

/* Does this interface support multicast ? */
/*
 * if_is_multicast —— 接口是否支持组播（IFF_MULTICAST）。
 */
int if_is_multicast(const struct interface *ifp)
{
	return ifp->flags & IFF_MULTICAST;
}

/* Printout flag information into log */
/*
 * if_flag_dump —— 把接口 flags 位转成 "<UP,BROADCAST,...>" 形式的可读串。
 * 内部用静态缓冲 logbuf 保存结果，因此返回值在下一次调用前有效（非线程安全）。
 */
const char *if_flag_dump(unsigned long flag)
{
	int separator = 0;
	static char logbuf[BUFSIZ];

#define IFF_OUT_LOG(X, STR)                                                    \
	if (flag & (X)) {                                                      \
		if (separator)                                                 \
			strlcat(logbuf, ",", sizeof(logbuf));                  \
		else                                                           \
			separator = 1;                                         \
		strlcat(logbuf, STR, sizeof(logbuf));                          \
	}

	strlcpy(logbuf, "<", BUFSIZ);
	IFF_OUT_LOG(IFF_UP, "UP");
#ifdef IFF_LOWER_UP
	IFF_OUT_LOG(IFF_LOWER_UP, "LOWER_UP");
#endif /* IFF_LOWER_UP */
	IFF_OUT_LOG(IFF_BROADCAST, "BROADCAST");
	IFF_OUT_LOG(IFF_DEBUG, "DEBUG");
	IFF_OUT_LOG(IFF_LOOPBACK, "LOOPBACK");
	IFF_OUT_LOG(IFF_POINTOPOINT, "POINTOPOINT");
	IFF_OUT_LOG(IFF_NOTRAILERS, "NOTRAILERS");
	IFF_OUT_LOG(IFF_RUNNING, "RUNNING");
	IFF_OUT_LOG(IFF_NOARP, "NOARP");
	IFF_OUT_LOG(IFF_PROMISC, "PROMISC");
	IFF_OUT_LOG(IFF_ALLMULTI, "ALLMULTI");
	IFF_OUT_LOG(IFF_OACTIVE, "OACTIVE");
	IFF_OUT_LOG(IFF_SIMPLEX, "SIMPLEX");
	IFF_OUT_LOG(IFF_LINK0, "LINK0");
	IFF_OUT_LOG(IFF_LINK1, "LINK1");
	IFF_OUT_LOG(IFF_LINK2, "LINK2");
	IFF_OUT_LOG(IFF_MULTICAST, "MULTICAST");
	IFF_OUT_LOG(IFF_NOXMIT, "NOXMIT");
	IFF_OUT_LOG(IFF_NORTEXCH, "NORTEXCH");
	IFF_OUT_LOG(IFF_VIRTUAL, "VIRTUAL");
	IFF_OUT_LOG(IFF_IPV4, "IPv4");
	IFF_OUT_LOG(IFF_IPV6, "IPv6");

	strlcat(logbuf, ">", sizeof(logbuf));

	return logbuf;
#undef IFF_OUT_LOG
}

/* For debugging */
/*
 * if_dump —— 调试用：把一个接口及其 VRF / 索引 / metric / MTU / flags 打到日志。
 */
static void if_dump(const struct interface *ifp)
{
	const struct connected *c;

	frr_each (if_connected_const, ifp->connected, c)
		zlog_info(
			"Interface %s vrf %s(%u) index %d metric %d mtu %d mtu6 %d %s",
			ifp->name, ifp->vrf->name, ifp->vrf->vrf_id,
			ifp->ifindex, ifp->metric, ifp->mtu, ifp->mtu6,
			if_flag_dump(ifp->flags));
}

/* Interface printing for all interface. */
/*
 * if_dump_all —— 调试用：遍历所有 VRF 的所有接口逐个 if_dump。
 */
void if_dump_all(void)
{
	struct vrf *vrf;
	void *ifp;

	RB_FOREACH (vrf, vrf_id_head, &vrfs_by_id)
		FOR_ALL_INTERFACES (vrf, ifp)
			if_dump(ifp);
}

/* Allocate connected structure. */
/*
 * connected_new —— 分配（清零）一个直连地址条目。
 */
struct connected *connected_new(void)
{
	return XCALLOC(MTYPE_CONNECTED, sizeof(struct connected));
}

/* Allocate nbr connected structure. */
/*
 * nbr_connected_new —— 分配（清零）一个邻居直连地址条目。
 */
struct nbr_connected *nbr_connected_new(void)
{
	return XCALLOC(MTYPE_NBR_CONNECTED, sizeof(struct nbr_connected));
}

/* Free connected structure. */
/*
 * connected_free —— 释放直连地址条目：释放 address / destination 前缀与 label，
 * 释放结构并把 *connected 置空（双指针）。
 */
void connected_free(struct connected **connected)
{
	struct connected *ptr = *connected;

	prefix_free(&ptr->address);
	prefix_free(&ptr->destination);

	XFREE(MTYPE_CONNECTED_LABEL, ptr->label);

	XFREE(MTYPE_CONNECTED, ptr);
	*connected = NULL;
}

/* Free nbr connected structure. */
/*
 * nbr_connected_free —— 释放邻居直连地址条目：有 address 则释放前缀，再释放结构。
 */
void nbr_connected_free(struct nbr_connected *connected)
{
	if (connected->address)
		prefix_free(&connected->address);

	XFREE(MTYPE_NBR_CONNECTED, connected);
}

/* If same interface nbr address already exists... */
/*
 * nbr_connected_check —— 查接口上是否已有该前缀的邻居直连地址条目，有则返回，无返回 NULL。
 * 名字虽叫 check，但只查不建（真正需要“无则建”时由调用方处理）。
 */
struct nbr_connected *nbr_connected_check(struct interface *ifp,
					  struct prefix *p)
{
	struct nbr_connected *ifc;
	struct listnode *node;

	for (ALL_LIST_ELEMENTS_RO(ifp->nbr_connected, node, ifc))
		if (prefix_same(ifc->address, p))
			return ifc;

	return NULL;
}

/* count the number of connected addresses that are in the given family */
/*
 * connected_count_by_family —— 统计接口上属于指定地址族（AF_INET / AF_INET6）的
 * 直连地址个数。
 */
unsigned int connected_count_by_family(struct interface *ifp, int family)
{
	struct connected *connected;
	unsigned int cnt = 0;

	frr_each (if_connected, ifp->connected, connected)
		if (connected->address->family == family)
			cnt++;

	return cnt;
}

/*
 * connected_lookup_prefix_exact —— 在接口上按前缀精确查找直连地址（prefix_same），
 * 找到返回条目，否则 NULL。
 */
struct connected *connected_lookup_prefix_exact(struct interface *ifp,
						const struct prefix *p)
{
	struct connected *ifc;

	frr_each (if_connected, ifp->connected, ifc) {
		if (prefix_same(ifc->address, p))
			return ifc;
	}
	return NULL;
}

/*
 * connected_delete_by_prefix —— 在接口上按前缀精确匹配删除直连地址条目。
 * 用 frr_each_safe 边遍历边删；命中则从链表摘除并返回该条目（交给调用方释放），
 * 未命中返回 NULL。
 */
struct connected *connected_delete_by_prefix(struct interface *ifp,
					     struct prefix *p)
{
	struct connected *ifc;

	/* In case of same prefix come, replace it with new one. */
	frr_each_safe (if_connected, ifp->connected, ifc) {
		if (prefix_same(ifc->address, p)) {
			if_connected_del(ifp->connected, ifc);
			return ifc;
		}
	}
	return NULL;
}

/* Find the address on our side that will be used when packets
   are sent to dst. */
/*
 * connected_lookup_prefix —— 在接口上按前缀做「最长前缀匹配」查直连地址。
 * 遍历直连地址，要求族相同且 prefix_match（含对端的前缀 CONNECTED_PREFIX），
 * 取 prefixlen 最大的命中条目返回（作为发送选路时源地址选择的依据）。
 */
struct connected *connected_lookup_prefix(struct interface *ifp,
					  const struct prefix *addr)
{
	struct connected *c;
	struct connected *match;

	match = NULL;

	frr_each (if_connected, ifp->connected, c) {
		if (c->address && (c->address->family == addr->family)
		    && prefix_match(CONNECTED_PREFIX(c), addr)
		    && (!match
			|| (c->address->prefixlen > match->address->prefixlen)))
			match = c;
	}
	return match;
}

/*
 * connected_add_by_prefix —— 给接口新增一个直连地址。
 * 新建条目、拷贝 address 前缀；若给了 destination 也拷贝一份；挂到链表尾并返回。
 * 不做重复检测，调用方通常先用 lookup 判重。
 */
struct connected *connected_add_by_prefix(struct interface *ifp,
					  struct prefix *p,
					  struct prefix *destination)
{
	struct connected *ifc;

	/* Allocate new connected address. */
	ifc = connected_new();
	ifc->ifp = ifp;

	/* Fetch interface address */
	ifc->address = prefix_new();
	memcpy(ifc->address, p, sizeof(struct prefix));

	/* Fetch dest address */
	if (destination) {
		ifc->destination = prefix_new();
		memcpy(ifc->destination, destination, sizeof(struct prefix));
	}

	/* Add connected address to the interface. */
	if_connected_add_tail(ifp->connected, ifc);
	return ifc;
}

/*
 * connected_get_linklocal —— 取接口上的 IPv6 链路本地地址条目。
 * 遍历直连地址，遇到 AF_INET6 且是链路本地（IN6_IS_ADDR_LINKLOCAL）即返回；
 * 没找到时返回 NULL（注意循环结束时 c 为 NULL）。
 */
struct connected *connected_get_linklocal(struct interface *ifp)
{
	struct connected *c = NULL;

	frr_each (if_connected, ifp->connected, c) {
		if (c->address->family == AF_INET6
		    && IN6_IS_ADDR_LINKLOCAL(&c->address->u.prefix6))
			break;
	}
	return c;
}

/*
 * if_terminate —— 清空给定 VRF 下的所有接口：不断取名字树根节点并 if_delete，
 * 直到名字树为空。
 */
void if_terminate(struct vrf *vrf)
{
	struct interface *ifp;

	while (!RB_EMPTY(if_name_head, &vrf->ifaces_by_name)) {
		ifp = RB_ROOT(if_name_head, &vrf->ifaces_by_name);
		if (!ifp)
			break;
		if_delete(&ifp);
	}
}

/*
 * if_link_type_str —— 把链路层类型枚举（enum zebra_link_type）转成可读字符串；
 * 未识别的类型返回 NULL。
 */
const char *if_link_type_str(enum zebra_link_type llt)
{
	switch (llt) {
#define llts(T,S) case (T): return (S)
		llts(ZEBRA_LLT_UNKNOWN, "Unknown");
		llts(ZEBRA_LLT_ETHER, "Ethernet");
		llts(ZEBRA_LLT_EETHER, "Experimental Ethernet");
		llts(ZEBRA_LLT_AX25, "AX.25 Level 2");
		llts(ZEBRA_LLT_PRONET, "PROnet token ring");
		llts(ZEBRA_LLT_IEEE802, "IEEE 802.2 Ethernet/TR/TB");
		llts(ZEBRA_LLT_ARCNET, "ARCnet");
		llts(ZEBRA_LLT_APPLETLK, "AppleTalk");
		llts(ZEBRA_LLT_DLCI, "Frame Relay DLCI");
		llts(ZEBRA_LLT_ATM, "ATM");
		llts(ZEBRA_LLT_METRICOM, "Metricom STRIP");
		llts(ZEBRA_LLT_IEEE1394, "IEEE 1394 IPv4");
		llts(ZEBRA_LLT_EUI64, "EUI-64");
		llts(ZEBRA_LLT_INFINIBAND, "InfiniBand");
		llts(ZEBRA_LLT_SLIP, "SLIP");
		llts(ZEBRA_LLT_CSLIP, "Compressed SLIP");
		llts(ZEBRA_LLT_SLIP6, "SLIPv6");
		llts(ZEBRA_LLT_CSLIP6, "Compressed SLIPv6");
		llts(ZEBRA_LLT_RSRVD, "Reserved");
		llts(ZEBRA_LLT_ADAPT, "Adapt");
		llts(ZEBRA_LLT_ROSE, "ROSE packet radio");
		llts(ZEBRA_LLT_X25, "CCITT X.25");
		llts(ZEBRA_LLT_PPP, "PPP");
		llts(ZEBRA_LLT_CHDLC, "Cisco HDLC");
		llts(ZEBRA_LLT_RAWHDLC, "Raw HDLC");
		llts(ZEBRA_LLT_LAPB, "LAPB");
		llts(ZEBRA_LLT_IPIP, "IPIP Tunnel");
		llts(ZEBRA_LLT_IPIP6, "IPIP6 Tunnel");
		llts(ZEBRA_LLT_FRAD, "FRAD");
		llts(ZEBRA_LLT_SKIP, "SKIP vif");
		llts(ZEBRA_LLT_LOOPBACK, "Loopback");
		llts(ZEBRA_LLT_LOCALTLK, "Localtalk");
		llts(ZEBRA_LLT_FDDI, "FDDI");
		llts(ZEBRA_LLT_SIT, "IPv6-in-IPv4 SIT");
		llts(ZEBRA_LLT_IPDDP, "IP-in-DDP tunnel");
		llts(ZEBRA_LLT_IPGRE, "GRE over IP");
		llts(ZEBRA_LLT_IP6GRE, "GRE over IPv6");
		llts(ZEBRA_LLT_PIMREG, "PIMSM registration");
		llts(ZEBRA_LLT_HIPPI, "HiPPI");
		llts(ZEBRA_LLT_ECONET, "Acorn Econet");
		llts(ZEBRA_LLT_IRDA, "IrDA");
		llts(ZEBRA_LLT_FCPP, "Fibre-Channel PtP");
		llts(ZEBRA_LLT_FCAL, "Fibre-Channel Arbitrated Loop");
		llts(ZEBRA_LLT_FCPL, "Fibre-Channel Public Loop");
		llts(ZEBRA_LLT_FCFABRIC, "Fibre-Channel Fabric");
		llts(ZEBRA_LLT_IEEE802_TR, "IEEE 802.2 Token Ring");
		llts(ZEBRA_LLT_IEEE80211, "IEEE 802.11");
		llts(ZEBRA_LLT_IEEE80211_RADIOTAP, "IEEE 802.11 Radiotap");
		llts(ZEBRA_LLT_IEEE802154, "IEEE 802.15.4");
		llts(ZEBRA_LLT_IEEE802154_PHY, "IEEE 802.15.4 Phy");
#undef llts
	}
	return NULL;
}

/*
 * if_link_params_cmp —— 比较两份 TE 链路参数是否相等，返回 bool。
 * 见 if.h 对 struct if_link_params 的说明（改动结构时要同步本函数）。
 */
bool if_link_params_cmp(struct if_link_params *iflp1,
			struct if_link_params *iflp2)
{
	struct if_link_params iflp1_copy, iflp2_copy;

	/* Extended admin-groups in if_link_params contain pointers.
	 * They cannot be compared with memcpy.
	 * Make copies of if_link_params without ext. admin-groups
	 * and compare separately the ext. admin-groups.
	 */
	memcpy(&iflp1_copy, iflp1, sizeof(struct if_link_params));
	memset(&iflp1_copy.ext_admin_grp, 0, sizeof(struct admin_group));

	memcpy(&iflp2_copy, iflp2, sizeof(struct if_link_params));
	memset(&iflp2_copy.ext_admin_grp, 0, sizeof(struct admin_group));

	if (memcmp(&iflp1_copy, &iflp2_copy, sizeof(struct if_link_params)))
		return false;

	if (!admin_group_cmp(&iflp1->ext_admin_grp, &iflp2->ext_admin_grp))
		return false;

	return true;
}

/*
 * if_link_params_copy —— 把 src 的链路参数拷进 dst。
 * 因 ext_admin_grp 内含指针，不能直接整块 memcpy 覆盖：先备份 dst 的 admin_group，
 * 整块拷贝后再恢复，最后用 admin_group_copy 深拷贝其内容。
 */
void if_link_params_copy(struct if_link_params *dst, struct if_link_params *src)
{
	struct admin_group dst_ag;

	/* backup the admin_group structure that contains a pointer */
	memcpy(&dst_ag, &dst->ext_admin_grp, sizeof(struct admin_group));
	/* copy the if_link_params structure */
	memcpy(dst, src, sizeof(struct if_link_params));
	/* restore the admin_group structure */
	memcpy(&dst->ext_admin_grp, &dst_ag, sizeof(struct admin_group));
	/* copy src->ext_admin_grp data to dst->ext_admin_grp data memory */
	admin_group_copy(&dst->ext_admin_grp, &src->ext_admin_grp);
}

/*
 * if_link_params_get —— 返回接口当前挂的链路参数指针（可能为 NULL），只取不建。
 */
struct if_link_params *if_link_params_get(struct interface *ifp)
{
	return ifp->link_params;
}

/*
 * if_link_params_enable —— 确保接口有链路参数并填好默认带宽。
 * 先 init（不存在则新建）；按接口带宽（无则 DEFAULT_BANDWIDTH）算出 default_bw，
 * 据此设 max_bw / max_rsv_bw 与各优先级未预留带宽；若接口有 metric 则同步为
 * te_metric。返回该参数结构。
 */
struct if_link_params *if_link_params_enable(struct interface *ifp)
{
	struct if_link_params *iflp;
	int i;

	iflp = if_link_params_init(ifp);

	/* Compute default bandwidth based on interface */
	iflp->default_bw =
		((ifp->bandwidth ? ifp->bandwidth : DEFAULT_BANDWIDTH)
		 * TE_MEGA_BIT / TE_BYTE);

	/* Set Max, Reservable and Unreserved Bandwidth */
	iflp->max_bw = iflp->default_bw;
	iflp->max_rsv_bw = iflp->default_bw;
	for (i = 0; i < MAX_CLASS_TYPE; i++)
		iflp->unrsv_bw[i] = iflp->default_bw;

	/* Update Link parameters status */
	iflp->lp_status = LP_MAX_BW | LP_MAX_RSV_BW | LP_UNRSV_BW;

	/* Set TE metric equal to standard metric only if it is set */
	if (ifp->metric != 0) {
		iflp->te_metric = ifp->metric;
		iflp->lp_status |= LP_TE_METRIC;
	}

	/* Finally attach newly created Link Parameters */
	ifp->link_params = iflp;

	return iflp;
}

/*
 * if_link_params_init —— 为接口准备一个链路参数结构。
 * 已有则直接返回；否则分配并初始化扩展管理组，挂到 ifp->link_params 后返回。
 */
struct if_link_params *if_link_params_init(struct interface *ifp)
{
	struct if_link_params *iflp = if_link_params_get(ifp);

	if (iflp)
		return iflp;

	iflp = XCALLOC(MTYPE_IF_LINK_PARAMS, sizeof(struct if_link_params));

	admin_group_init(&iflp->ext_admin_grp);

	ifp->link_params = iflp;

	return iflp;
}

/*
 * if_link_params_free —— 释放接口的链路参数；没有则直接返回。
 * 先终止扩展管理组再释放结构。
 */
void if_link_params_free(struct interface *ifp)
{
	if (!ifp->link_params)
		return;

	admin_group_term(&ifp->link_params->ext_admin_grp);
	XFREE(MTYPE_IF_LINK_PARAMS, ifp->link_params);
}

/* ----------- CLI commands ----------- */

/* Guess the VRF of an interface. */
/*
 * vrfname_by_ifname —— 在 netns 后端下，按接口名在所有 VRF 中查找。
 * 命中则把 VRF 名写到 *vrfname；返回值是命中的个数（用于判断唯一性）。
 */
static int vrfname_by_ifname(const char *ifname, const char **vrfname)
{
	struct vrf *vrf;
	struct interface *ifp;
	int count = 0;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		FOR_ALL_INTERFACES (vrf, ifp) {
			if (strmatch(ifp->name, ifname)) {
				*vrfname = vrf->name;
				count++;
			}
		}
	}

	return count;
}

/*
 * XPath: /frr-interface:lib/interface
 */
/*
 * interface 命令 —— 进入接口配置模式：interface IFNAME [vrf NAME]。
 * 计算对应接口的 xpath（netns 后端用 name='vrf:ifname'，否则 name='ifname'），
 * 先 enqueue 一个 CREATE 再 apply；成功后压入 INTERFACE_NODE xpath，
 * 并（为兼容旧命令）用 qobj 取到 ifp 压入上下文。
 * netns 且未指定 vrf 时，若该名字在系统中唯一则用其所属 VRF，否则用默认 VRF。
 */
DEFPY_YANG_NOSH (interface,
       interface_cmd,
       "interface IFNAME [vrf NAME$vrf_name]",
       "Select an interface to configure\n"
       "Interface's name\n"
       VRF_CMD_HELP_STR)
{
	char xpath_list[XPATH_MAXLEN];
	struct interface *ifp;
	struct vrf *vrf;
	int ret, count;

	if (vrf_is_backend_netns()) {
		/*
		 * For backward compatibility, if the VRF name is not specified
		 * and there is exactly one interface with this name in the
		 * system, use its VRF. Otherwise fallback to the default VRF.
		 */
		if (!vrf_name) {
			count = vrfname_by_ifname(ifname, &vrf_name);
			if (count != 1)
				vrf_name = VRF_DEFAULT_NAME;
		}

		snprintf(xpath_list, XPATH_MAXLEN,
			 "/frr-interface:lib/interface[name='%s:%s']", vrf_name,
			 ifname);
	} else {
		snprintf(xpath_list, XPATH_MAXLEN,
			 "/frr-interface:lib/interface[name='%s']", ifname);
	}

	nb_cli_enqueue_change(vty, ".", NB_OP_CREATE, NULL);
	ret = nb_cli_apply_changes_clear_pending(vty, "%s", xpath_list);
	if (ret == CMD_SUCCESS) {
		VTY_PUSH_XPATH(INTERFACE_NODE, xpath_list);

		/*
		 * For backward compatibility with old commands we still need
		 * to use the qobj infrastructure. This can be removed once
		 * all interface-level commands are converted to the new
		 * northbound model.
		 */
		if (vrf_is_backend_netns()) {
			vrf = vrf_lookup_by_name(vrf_name);
			if (vrf)
				ifp = if_lookup_by_name_vrf(ifname, vrf);
			else
				ifp = NULL;
		} else {
			ifp = if_lookup_by_name_all_vrf(ifname);
		}
		if (ifp)
			VTY_PUSH_CONTEXT(INTERFACE_NODE, ifp);
	}

	return ret;
}

/*
 * no_interface 命令 —— no interface IFNAME [vrf NAME]：删除伪接口的配置。
 * 同样先算 xpath，enqueue 一个 DESTROY 并 apply。
 */
DEFPY_YANG (no_interface,
       no_interface_cmd,
       "no interface IFNAME [vrf NAME$vrf_name]",
       NO_STR
       "Delete a pseudo interface's configuration\n"
       "Interface's name\n"
       VRF_CMD_HELP_STR)
{
	char xpath_list[XPATH_MAXLEN];
	int count;

	if (vrf_is_backend_netns()) {
		/*
		 * For backward compatibility, if the VRF name is not specified
		 * and there is exactly one interface with this name in the
		 * system, use its VRF. Otherwise fallback to the default VRF.
		 */
		if (!vrf_name) {
			count = vrfname_by_ifname(ifname, &vrf_name);
			if (count != 1)
				vrf_name = VRF_DEFAULT_NAME;
		}

		snprintf(xpath_list, XPATH_MAXLEN,
			 "/frr-interface:lib/interface[name='%s:%s']", vrf_name,
			 ifname);
	} else {
		snprintf(xpath_list, XPATH_MAXLEN,
			 "/frr-interface:lib/interface[name='%s']", ifname);
	}

	nb_cli_enqueue_change(vty, ".", NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, "%s", xpath_list);
}

/*
 * netns_ifname_split —— 把 netns 后端的组合名 "vrfname:ifname" 拆开。
 * xpath 里第一个冒号作分隔符：前半写入 vrfname，后半写入 ifname。
 * 断言必须是 netns 后端且确实含冒号。
 */
static void netns_ifname_split(const char *xpath, char *ifname, char *vrfname)
{
	char *delim;
	int len;

	assert(vrf_is_backend_netns());

	delim = strchr(xpath, ':');
	assert(delim);

	len = delim - xpath;
	memcpy(vrfname, xpath, len);
	vrfname[len] = 0;

	strlcpy(ifname, delim + 1, XPATH_MAXLEN);
}

/*
 * cli_show_interface —— northbound 展示回调：打印接口配置头 "interface ..."。
 * netns 后端下用 netns_ifname_split 拆出名字与 VRF，非默认 VRF 才追加 " vrf ..."。
 */
static void cli_show_interface(struct vty *vty, const struct lyd_node *dnode,
			       bool show_defaults)
{
	vty_out(vty, "!\n");

	if (vrf_is_backend_netns()) {
		char ifname[XPATH_MAXLEN];
		char vrfname[XPATH_MAXLEN];

		netns_ifname_split(yang_dnode_get_string(dnode, "name"),
				   ifname, vrfname);

		vty_out(vty, "interface %s", ifname);
		if (!strmatch(vrfname, VRF_DEFAULT_NAME))
			vty_out(vty, " vrf %s", vrfname);
	} else {
		const char *ifname = yang_dnode_get_string(dnode, "name");

		vty_out(vty, "interface %s", ifname);
	}

	vty_out(vty, "\n");
}

/*
 * cli_show_interface_end —— 接口配置块结束时的展示回调，输出 "exit"。
 */
static void cli_show_interface_end(struct vty *vty,
				   const struct lyd_node *dnode)
{
	vty_out(vty, "exit\n");
}

/*
 * cli_cmp_interface —— 配置排序比较函数：按接口名（用 if_cmp_name_func）。
 */
static int cli_cmp_interface(const struct lyd_node *dnode1,
			     const struct lyd_node *dnode2)
{
	const char *ifname1 = yang_dnode_get_string(dnode1, "name");
	const char *ifname2 = yang_dnode_get_string(dnode2, "name");

	return if_cmp_name_func(ifname1, ifname2);
}

/*
 * if_vty_config_start —— VTY 配置输出辅助：输出接口配置头 "interface NAME"，
 * netns 且非默认 VRF 时追加 " vrf xxx"。使用 vty_frame 缓存。
 */
void if_vty_config_start(struct vty *vty, struct interface *ifp)
{
	vty_frame(vty, "!\n");
	vty_frame(vty, "interface %s", ifp->name);

	if (vrf_is_backend_netns() && strcmp(ifp->vrf->name, VRF_DEFAULT_NAME))
		vty_frame(vty, " vrf %s", ifp->vrf->name);

	vty_frame(vty, "\n");
}

/*
 * if_vty_config_end —— 结束接口配置输出：输出 "exit\n!\n"。
 */
void if_vty_config_end(struct vty *vty)
{
	vty_endframe(vty, "exit\n!\n");
}

/*
 * XPath: /frr-interface:lib/interface/description
 */
/*
 * interface_desc 命令 —— description LINE...：设置接口描述。
 * 把多词参数合并成一个串，enqueue 到 ./description（MODIFY）并 apply，用完释放临时串。
 */
DEFPY_YANG (interface_desc,
       interface_desc_cmd,
       "description LINE...",
       "Interface specific description\n"
       "Characters describing this interface\n")
{
	char *desc;
	int ret;

	desc = argv_concat(argv, argc, 1);
	nb_cli_enqueue_change(vty, "./description", NB_OP_MODIFY, desc);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, desc);

	return ret;
}

/*
 * no_interface_desc 命令 —— no description：删除接口描述（enqueue DESTROY ./description）。
 */
DEFPY_YANG  (no_interface_desc,
	no_interface_desc_cmd,
	"no description",
	NO_STR
	"Interface specific description\n")
{
	nb_cli_enqueue_change(vty, "./description", NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * cli_show_interface_desc —— 展示接口描述：输出 " description <值>"。
 */
static void cli_show_interface_desc(struct vty *vty,
				    const struct lyd_node *dnode,
				    bool show_defaults)
{
	vty_out(vty, " description %s\n", yang_dnode_get_string(dnode, NULL));
}

/* Interface autocomplete. */
/*
 * if_autocomplete —— CLI 补全：把所有 VRF 下所有接口名加入补全候选。
 */
static void if_autocomplete(vector comps, struct cmd_token *token)
{
	struct interface *ifp;
	struct vrf *vrf;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		FOR_ALL_INTERFACES (vrf, ifp) {
			vector_set(comps, XSTRDUP(MTYPE_COMPLETION, ifp->name));
		}
	}
}

/*
 * if_var_handlers —— 变量处理器：为 "interface" / IFNAME / INTERFACE 等 token 提供补全。
 */
static const struct cmd_variable_handler if_var_handlers[] = {
	{/* "interface NAME" */
	 .varname = "interface",
	 .completions = if_autocomplete},
	{.tokenname = "IFNAME", .completions = if_autocomplete},
	{.tokenname = "INTERFACE", .completions = if_autocomplete},
	{.completions = NULL}};

/*
 * interface_node —— INTERFACE_NODE 的 CLI 节点定义：
 * 提示符 "%s(config-if)# "，父节点 CONFIG_NODE。
 */
static struct cmd_node interface_node = {
	.name = "interface",
	.node = INTERFACE_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-if)# ",
};

/*
 * if_config_write_single —— yang_dnode_iterate 的回调：写出单个接口节点的配置命令，
 * 恒返回 YANG_ITER_CONTINUE 以继续遍历。
 */
static int if_config_write_single(const struct lyd_node *dnode, void *arg)
{
	nb_cli_show_dnode_cmds(arg, dnode, false);

	return YANG_ITER_CONTINUE;
}

/*
 * if_nb_config_write —— 接口的配置写出函数（供 config_write 使用）：
 * 遍历 running config 里的所有 interface 节点逐个写出。
 */
static int if_nb_config_write(struct vty *vty)
{
	yang_dnode_iterate(if_config_write_single, vty, running_config->dnode,
			   "/frr-interface:lib/interface");
	return 1;
}

/*
 * if_cmd_init —— 初始化接口相关 CLI。
 * 注册补全变量处理器；把调用方给的 config_write 挂到 interface_node；安装节点；
 * 在 CONFIG_NODE 装 interface / no interface 命令，在 INTERFACE_NODE 装默认命令与
 * description / no description。
 */
void if_cmd_init(int (*config_write)(struct vty *))
{
	cmd_variable_handler_register(if_var_handlers);

	interface_node.config_write = config_write;
	install_node(&interface_node);

	install_element(CONFIG_NODE, &interface_cmd);
	install_element(CONFIG_NODE, &no_interface_cmd);

	install_default(INTERFACE_NODE);
	install_element(INTERFACE_NODE, &interface_desc_cmd);
	install_element(INTERFACE_NODE, &no_interface_desc_cmd);
}

/*
 * if_cmd_init_default —— 用默认写出函数 if_nb_config_write 初始化接口 CLI。
 */
void if_cmd_init_default(void)
{
	if_cmd_init(if_nb_config_write);
}

/* ------- Northbound callbacks ------- */

/*
 * XPath: /frr-interface:lib/interface
 */
/*
 * lib_interface_create —— northbound 回调：创建 interface 列表项。
 * VALIDATE：校验接口名（netns 下还要校验 VRF 名）长度是否超限。
 * APPLY   ：netns 下拆分 name 后按部件 if_get_by_name；否则直接用 name 与默认 VRF。
 *           得到 ifp 后标记 configured 并把 ifp 绑到 running 条目供后续回调取用。
 */
static int lib_interface_create(struct nb_cb_create_args *args)
{
	const char *ifname;
	struct interface *ifp;

	ifname = yang_dnode_get_string(args->dnode, "name");

	switch (args->event) {
	case NB_EV_VALIDATE:
		if (vrf_is_backend_netns()) {
			char ifname_ns[XPATH_MAXLEN];
			char vrfname_ns[XPATH_MAXLEN];

			netns_ifname_split(ifname, ifname_ns, vrfname_ns);

			if (strlen(ifname_ns) > 16) {
				snprintf(
					args->errmsg, args->errmsg_len,
					"Maximum interface name length is 16 characters");
				return NB_ERR_VALIDATION;
			}
			if (strlen(vrfname_ns) > 36) {
				snprintf(
					args->errmsg, args->errmsg_len,
					"Maximum VRF name length is 36 characters");
				return NB_ERR_VALIDATION;
			}
		} else {
			if (strlen(ifname) > 16) {
				snprintf(
					args->errmsg, args->errmsg_len,
					"Maximum interface name length is 16 characters");
				return NB_ERR_VALIDATION;
			}
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		if (vrf_is_backend_netns()) {
			char ifname_ns[XPATH_MAXLEN];
			char vrfname_ns[XPATH_MAXLEN];

			netns_ifname_split(ifname, ifname_ns, vrfname_ns);

			ifp = if_get_by_name(ifname_ns, VRF_UNKNOWN,
					     vrfname_ns);
		} else {
			ifp = if_get_by_name(ifname, VRF_UNKNOWN,
					     VRF_DEFAULT_NAME);
		}

		if (!ifp) {
			snprintf(args->errmsg, args->errmsg_len, "failed to create interface '%s'",
				 ifname);
			return NB_ERR_RESOURCE;
		}

		ifp->configured = true;
		nb_running_set_entry(args->dnode, ifp);
		break;
	}

	return NB_OK;
}

/*
 * lib_interface_destroy —— northbound 回调：删除 interface 列表项。
 * VALIDATE：仅允许删除非 ACTIVE（未在内核生效）的接口。
 * APPLY   ：解绑 running 条目、清 configured、if_delete 释放；若其 VRF 已不启用则删 VRF。
 */
static int lib_interface_destroy(struct nb_cb_destroy_args *args)
{
	struct interface *ifp;
	struct vrf *vrf;

	switch (args->event) {
	case NB_EV_VALIDATE:
		ifp = nb_running_get_entry(args->dnode, NULL, true);
		if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
			snprintf(args->errmsg, args->errmsg_len,
				 "only inactive interfaces can be deleted");
			return NB_ERR_VALIDATION;
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		ifp = nb_running_unset_entry(args->dnode);
		vrf = ifp->vrf;

		ifp->configured = false;
		if_delete(&ifp);

		if (!vrf_is_enabled(vrf))
			vrf_delete(vrf);
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface
 */
/*
 * lib_interface_get_next —— northbound 遍历回调：取下一个接口。
 * 首次（list_entry 为 NULL）取第一个 VRF 的第一个接口；否则在同一 VRF 内取 RB_NEXT，
 * 没有则切到下一个 VRF 的第一个接口，直到没有 VRF 返回 NULL。
 */
static const void *lib_interface_get_next(struct nb_cb_get_next_args *args)
{
	struct vrf *vrf;
	struct interface *pif = (struct interface *)args->list_entry;

	if (args->list_entry == NULL) {
		vrf = RB_MIN(vrf_name_head, &vrfs_by_name);
		assert(vrf);
		pif = RB_MIN(if_name_head, &vrf->ifaces_by_name);
	} else {
		vrf = pif->vrf;
		pif = RB_NEXT(if_name_head, pif);
		/* if no more interfaces, switch to next vrf */
		while (pif == NULL) {
			vrf = RB_NEXT(vrf_name_head, vrf);
			if (!vrf)
				return NULL;
			pif = RB_MIN(if_name_head, &vrf->ifaces_by_name);
		}
	}

	return pif;
}

/*
 * lib_interface_get_keys —— northbound 回调：给出列表项的键（接口名）。
 * netns 后端返回 "vrfname:ifname"，否则只返回 ifname。
 */
static int lib_interface_get_keys(struct nb_cb_get_keys_args *args)
{
	const struct interface *ifp = args->list_entry;

	args->keys->num = 1;

	if (vrf_is_backend_netns())
		snprintf(args->keys->key[0], sizeof(args->keys->key[0]),
			 "%s:%s", ifp->vrf->name, ifp->name);
	else
		snprintf(args->keys->key[0], sizeof(args->keys->key[0]), "%s",
			 ifp->name);

	return NB_OK;
}

static const void *
/*
 * lib_interface_lookup_entry —— northbound 回调：按键查找列表项。
 * netns 后端先拆出 VRF 名再在该 VRF 内按接口名查；否则跨所有 VRF 按名字查。
 */
lib_interface_lookup_entry(struct nb_cb_lookup_entry_args *args)
{
	if (vrf_is_backend_netns()) {
		char ifname[XPATH_MAXLEN];
		char vrfname[XPATH_MAXLEN];
		struct vrf *vrf;

		netns_ifname_split(args->keys->key[0], ifname, vrfname);

		vrf = vrf_lookup_by_name(vrfname);

		return vrf ? if_lookup_by_name(ifname, vrf->vrf_id) : NULL;
	} else {
		return if_lookup_by_name_all_vrf(args->keys->key[0]);
	}
}

/*
 * XPath: /frr-interface:lib/interface/description
 */
/*
 * lib_interface_description_modify —— northbound 回调：设置接口描述。
 * 仅在 APPLY 阶段动作：释放旧 desc，复制新描述到 ifp->desc。
 */
static int lib_interface_description_modify(struct nb_cb_modify_args *args)
{
	struct interface *ifp;
	const char *description;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ifp = nb_running_get_entry(args->dnode, NULL, true);
	XFREE(MTYPE_IFDESC, ifp->desc);
	description = yang_dnode_get_string(args->dnode, NULL);
	ifp->desc = XSTRDUP(MTYPE_IFDESC, description);

	return NB_OK;
}

/*
 * lib_interface_description_destroy —— northbound 回调：删除接口描述（APPLY 阶段释放 desc）。
 */
static int lib_interface_description_destroy(struct nb_cb_destroy_args *args)
{
	struct interface *ifp;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ifp = nb_running_get_entry(args->dnode, NULL, true);
	XFREE(MTYPE_IFDESC, ifp->desc);

	return NB_OK;
}

/*
 * __return_ok —— 固定的 northbound 回调桩：恒返回 NB_OK（用于无需处理的叶子）。
 */
static enum nb_error __return_ok(const struct nb_node *nb_node, const void *list_entry,
				 struct lyd_node *parent)
{
	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface/vrf
 */
/*
 * lib_interface_vrf_get —— state 回调：把接口所属 VRF 名写进 vrf 叶子。
 */
static enum nb_error lib_interface_vrf_get(const struct nb_node *nb_node, const void *list_entry,
					   struct lyd_node *parent)
{
	const struct lysc_node *snode = nb_node->snode;
	const struct interface *ifp = list_entry;

	if (lyd_new_term(parent, snode->module, snode->name, ifp->vrf->name, LYD_NEW_PATH_UPDATE,
			 NULL))
		return NB_ERR_RESOURCE;
	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface/state/if-index
 */
/*
 * lib_interface_state_if_index_get —— state 回调：写出接口 ifindex（32 位整数）。
 */
static enum nb_error lib_interface_state_if_index_get(const struct nb_node *nb_node,
						      const void *list_entry,
						      struct lyd_node *parent)
{
	const struct lysc_node *snode = nb_node->snode;
	const struct interface *ifp = list_entry;
	int32_t value = ifp->ifindex;

	if (yang_new_term_bin(parent, snode->module, snode->name, &value, sizeof(value),
			      LYD_NEW_PATH_UPDATE, NULL))
		return NB_ERR_RESOURCE;
	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface/state/mtu[6]
 */
/*
 * lib_interface_state_mtu_get —— state 回调：写出接口 MTU（IPv4 / IPv6 共用，按节点名区分）。
 */
static enum nb_error lib_interface_state_mtu_get(const struct nb_node *nb_node,
						 const void *list_entry, struct lyd_node *parent)
{
	const struct lysc_node *snode = nb_node->snode;
	const struct interface *ifp = list_entry;
	uint32_t value = ifp->mtu;

	if (yang_new_term_bin(parent, snode->module, snode->name, &value, sizeof(value),
			      LYD_NEW_PATH_UPDATE, NULL))
		return NB_ERR_RESOURCE;
	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface/state/speed
 */
/*
 * lib_interface_state_speed_get —— state 回调：写出接口速率。
 */
static enum nb_error lib_interface_state_speed_get(const struct nb_node *nb_node,
						   const void *list_entry, struct lyd_node *parent)
{
	const struct lysc_node *snode = nb_node->snode;
	const struct interface *ifp = list_entry;
	uint32_t value = ifp->speed;

	if (yang_new_term_bin(parent, snode->module, snode->name, &value, sizeof(value),
			      LYD_NEW_PATH_UPDATE, NULL))
		return NB_ERR_RESOURCE;
	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface/state/metric
 */
/*
 * lib_interface_state_metric_get —— state 回调：写出接口 metric。
 */
static enum nb_error lib_interface_state_metric_get(const struct nb_node *nb_node,
						    const void *list_entry, struct lyd_node *parent)
{
	const struct lysc_node *snode = nb_node->snode;
	const struct interface *ifp = list_entry;
	uint32_t value = ifp->metric;

	if (yang_new_term_bin(parent, snode->module, snode->name, &value, sizeof(value),
			      LYD_NEW_PATH_UPDATE, NULL))
		return NB_ERR_RESOURCE;
	return NB_OK;
}

/*
 * XPath: /frr-interface:lib/interface/state/phy-address
 */
/*
 * lib_interface_state_phy_address_get_elem —— state 回调：把接口硬件地址输出为 MAC 类型。
 * 只取前 ETH_ALEN(6) 字节构造 ethaddr。
 */
static struct yang_data *
lib_interface_state_phy_address_get_elem(struct nb_cb_get_elem_args *args)
{
	const struct interface *ifp = args->list_entry;
	struct ethaddr macaddr;

	memcpy(&macaddr.octet, ifp->hw_addr, ETH_ALEN);

	return yang_data_new_mac(args->xpath, &macaddr);
}

/* clang-format off */

/* cli_show callbacks are kept here for daemons not yet converted to mgmtd */
/*
 * frr_interface_info —— frr-interface 模块的 northbound 回调表（非 mgmtd 守护进程用）。
 * 上面的 cli_show 回调集中放在这里注册。
 */
const struct frr_yang_module_info frr_interface_info = {
	.name = "frr-interface",
	.nodes = {
		{
			.xpath = "/frr-interface:lib/interface",
			.cbs = {
				.create = lib_interface_create,
				.destroy = lib_interface_destroy,
				.cli_show = cli_show_interface,
				.cli_show_end = cli_show_interface_end,
				.cli_cmp = cli_cmp_interface,
				.get_next = lib_interface_get_next,
				.get_keys = lib_interface_get_keys,
				.lookup_entry = lib_interface_lookup_entry,
			},
		},
		{
			.xpath = "/frr-interface:lib/interface/description",
			.cbs = {
				.modify = lib_interface_description_modify,
				.destroy = lib_interface_description_destroy,
				.cli_show = cli_show_interface_desc,
			},
		},
		{
			.xpath = "/frr-interface:lib/interface/vrf",
			.cbs = {
				.get = lib_interface_vrf_get,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/if-index",
			.cbs = {
				.get = lib_interface_state_if_index_get,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/mtu",
			.cbs = {
				.get = lib_interface_state_mtu_get,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/mtu6",
			.cbs = {
				.get = lib_interface_state_mtu_get,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/speed",
			.cbs = {
				.get = lib_interface_state_speed_get,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/metric",
			.cbs = {
				.get = lib_interface_state_metric_get,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/flags",
			.cbs = {
				.get = __return_ok,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/type",
			.cbs = {
				.get = __return_ok,
			}
		},
		{
			.xpath = "/frr-interface:lib/interface/state/phy-address",
			.cbs = {
				.get_elem = lib_interface_state_phy_address_get_elem,
			}
		},
		{
			.xpath = NULL,
		},
	}
};

/*
 * frr_interface_cli_info —— frr-interface 模块「仅 CLI」版回调表（ignore_cfg_cbs=true），
 * 只注册展示类回调，供已迁移到 mgmtd 的守护进程使用。
 */
const struct frr_yang_module_info frr_interface_cli_info = {
	.name = "frr-interface",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = "/frr-interface:lib/interface",
			.cbs = {
				.cli_show = cli_show_interface,
				.cli_show_end = cli_show_interface_end,
				.cli_cmp = cli_cmp_interface,
			},
		},
		{
			.xpath = "/frr-interface:lib/interface/description",
			.cbs = {
				.cli_show = cli_show_interface_desc,
			},
		},
		{
			.xpath = NULL,
		},
	}
};
