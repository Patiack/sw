// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Interface function.
 * Copyright (C) 1997, 1999 Kunihiro Ishiguro
 *
 * zebra 接口管理核心文件：
 *   - 维护 struct interface 生命周期（创建/查找/更新/删除）及其私有扩展
 *     struct zebra_if（if_zebra_new_hook/if_zebra_delete_hook）；
 *   - 处理接口 up/down、标志变化、地址（connected）增删、直连路由
 *     安装撤销，并向各路由协议广播接口/地址事件（redistribute）；
 *   - 承接 dplane（netlink dump/事件）送来的接口上下文：字段更新、
 *     VRF 归属变更、二层信息（bridge/vxlan/bond）、protodown 同步等
 *     （zebra_if_addr_update_ctx/zebra_if_update_ctx/zebra_if_dplane_ifp_handling）；
 *   - dplane 结果回调 zebra_if_dplane_result()（内核 link 事件在此分流给
 *     CTC 卸载层）；
 *   - vtysh `show interface` 系列命令与接口配置命令（shutdown/multicast/
 *     link-detect/地址安装等）的实现。
 */

#include <zebra.h>

#include "if.h"
#include "lib_errors.h"
#include "vty.h"
#include "sockunion.h"
#include "prefix.h"
#include "command.h"
#include "memory.h"
#include "ioctl.h"
#include "connected.h"
#include "log.h"
#include "zclient.h"
#include "vrf.h"

#include "zebra/rtadv.h"
#include "zebra_ns.h"
#include "zebra_vrf.h"
#include "zebra/interface.h"
#include "zebra/rib.h"
#include "zebra/rt.h"
#include "zebra/zebra_router.h"
#include "zebra/redistribute.h"
#include "zebra/debug.h"
#include "zebra/zebra_ptm.h"
#include "zebra/rt_netlink.h"
#include "zebra/if_netlink.h"
#include "zebra/zebra_vxlan.h"
#include "zebra/zebra_errors.h"
#include "zebra/zebra_evpn_mh.h"
#include "zebra/zebra_trace.h"

/* ctcdplane: 接口/VRF 事件转发（定义在 zebra/ctcdplane/ctc_dplane.c）。
 * 本 FRR 版本没有出方向的 DPLANE_OP_INTF_* 事件（dplane_intf_add() 零调用者、
 * dplane_intf_update() 只服务 protodown），内核 link 事件的 ctx 经
 * dplane_provider_enqueue_to_zebra() 直达本文件的 dplane 结果处理函数，
 * provider 收不到，故在此显式转发给 CTC 卸载层。 */
extern void ctc_dplane_intf_event(struct zebra_dplane_ctx *ctx);

DEFINE_MTYPE_STATIC(ZEBRA, ZINFO, "Zebra Interface Information");

#define ZEBRA_PTM_SUPPORT

DEFINE_HOOK(zebra_if_extra_info, (struct vty * vty, json_object *json_if, struct interface *ifp),
	    (vty, json_if, ifp));

DEFINE_MTYPE_STATIC(ZEBRA, ZIF_DESC, "Intf desc");

static void if_down_del_nbr_connected(struct interface *ifp);

/* zebra 配置三态（IF_ZEBRA_DATA_UNSPEC/ON/OFF）转可读字符串，
 * 供 show interface 打印 shutdown/multicast/mpls 等配置项状态 */
static const char *if_zebra_data_state(uint8_t state)
{
	switch (state) {
	case IF_ZEBRA_DATA_UNSPEC:
		return "Not specified by CLI";
	case IF_ZEBRA_DATA_ON:
		return "Enabled by CLI";
	case IF_ZEBRA_DATA_OFF:
		return "Disabled by CLI";
	}

	return "STATE IS WRONG DEV ESCAPE";
}

/* 接口速率（带宽）轮询定时器回调：从内核读 speed，变化则更新
 * ifp->speed/bandwidth 并向协议重发 IF_ADD；未变化或报 UNKNOWN 时
 * 每 5s 重试，最长 4 分钟后放弃（桥/loopback 等虚拟口可能永远无速率）；
 * READ 错误（VRF/接口不存在）直接停止本轮 */
static void if_zebra_speed_update(struct event *event)
{
	struct interface *ifp = EVENT_ARG(event);
	struct zebra_if *zif = ifp->info;
	uint32_t new_speed;
	bool changed = false;
	int error = 0;

	/* 记录本轮查询发生过的总次数（仅用于统计/调试） */
	zif->speed_checked++;

	/* 向内核/驱动查询当前速率，error 带回失败原因 */
	new_speed = kernel_get_speed(ifp, &error);

	/* error may indicate vrf not available or
	 * interfaces not available.
	 * note that loopback & virtual interfaces can return 0 as speed
	 */
	if (error == INTERFACE_SPEED_ERROR_READ)
		return;

	/* 速率发生变化：更新 ifp 状态并全量重发 IF_ADD，
	 * 让所有协议重新拿到新带宽 */
	if (new_speed != ifp->speed) {
		zlog_info("%s: %s old speed: %u new speed: %u", __func__,
			  ifp->name, ifp->speed, new_speed);
		if_update_state_speed(ifp, new_speed);
		if_add_update(ifp);
		changed = true;
	}

	/* 有变化或速率暂不可知时续订 5s 后重试；
	 * 超过 4 分钟仍 UNKNOWN 则不再尝试 */
	if (changed || error == INTERFACE_SPEED_ERROR_UNKNOWN) {
#define SPEED_UPDATE_SLEEP_TIME 5
#define SPEED_UPDATE_COUNT_MAX (4 * 60 / SPEED_UPDATE_SLEEP_TIME)
		/*
		 * Some interfaces never actually have an associated speed
		 * with them ( I am looking at you bridges ).
		 * So instead of iterating forever, let's give the
		 * system 4 minutes to try to figure out the speed
		 * if after that it it's probably never going to become
		 * useful.
		 * Since I don't know all the wonderful types of interfaces
		 * that may come into existence in the future I am going
		 * to not update the system to keep track of that.  This
		 * is far simpler to just stop trying after 4 minutes
		 */
		/* 重试次数已达上限（4 分钟）仍读不到：放弃 */
		if (error == INTERFACE_SPEED_ERROR_UNKNOWN &&
		    zif->speed_update_count == SPEED_UPDATE_COUNT_MAX)
			return;

		/* 记数并重新挂 5s 定时器；ignore_late_timer 允许
		 * 进程退出时迟到回调直接丢弃 */
		zif->speed_update_count++;
		event_add_timer(zrouter.master, if_zebra_speed_update, ifp,
				SPEED_UPDATE_SLEEP_TIME, &zif->speed_update);
		event_ignore_late_timer(zif->speed_update);
	}
}

/* 路由节点销毁回调：node->info 是 connected 链表，先整体释放链表
 * 再销毁节点（ipv4_subnets 树专用 delegate） */
static void zebra_if_node_destroy(route_table_delegate_t *delegate,
				  struct route_table *table,
				  struct route_node *node)
{
	/* 节点上挂的 connected 链表先整体释放 */
	if (node->info)
		list_delete((struct list **)&node->info);
	/* 再交回默认委托销毁节点本身 */
	route_node_destroy(delegate, table, node);
}

/* ipv4_subnets 红黑树的节点委托：销毁时连带释放节点上的
 * connected 链表，避免地址表项泄漏 */
route_table_delegate_t zebra_if_table_delegate = {
	.create_node = route_node_create,
	.destroy_node = zebra_if_node_destroy};

/* Called when new interface is added. */
/* 接口创建钩子（if.c 的 if_create 调用）：分配并初始化 zebra 私有
 * struct zebra_if，默认配置均为“未指定”，初始化 nhg 依赖树/PTM/
 * rtadv/EVPN-MH/地址树，并延后 15s 启动速率探测（刚 up 时速率
 * 可能不准） */
static int if_zebra_new_hook(struct interface *ifp)
{
	struct zebra_if *zebra_if;

	/* 分配 zebra 私有结构并反向挂钩 ifp */
	zebra_if = XCALLOC(MTYPE_ZINFO, sizeof(struct zebra_if));
	zebra_if->ifp = ifp;

	/* CLI 尚未配置的项目一律置“未指定”，后续 if_add_update
	 * 依此决定是否沿用内核原值 */
	zebra_if->multicast = IF_ZEBRA_DATA_UNSPEC;
	zebra_if->mpls_config = IF_ZEBRA_DATA_UNSPEC;
	zebra_if->shutdown = IF_ZEBRA_DATA_UNSPEC;

	/* 默认 network namespace 未知 */
	zebra_if->link_nsid = NS_UNKNOWN;

	/* 初始化依赖本接口的 nexthop 组红黑树 */
	nhg_connected_tree_init(&zebra_if->nhg_dependents);

	/* PTM 私有区与全局 ptm 开关初始状态 */
	zebra_ptm_if_init(zebra_if);

	ifp->ptm_enable = zebra_ptm_get_enable_state();

	/* IPv6 RA（router advertisement）私有配置 */
	rtadv_if_init(zebra_if);

	/* EVPN 多归属（M-H）私有区 */
	zebra_evpn_mh_if_init(zebra_if);

	/* RFC 5549 用：缓存由 IPv6 链路本地地址逆推出的邻居 MAC */
	memset(&zebra_if->neigh_mac[0], 0, 6);

	/* Initialize installed address chains tree. */
	zebra_if->ipv4_subnets =
		route_table_init_with_delegate(&zebra_if_table_delegate);

	ifp->info = zebra_if;

	/* 归零速率探测统计并延后 15s 首查：平台可能在接口刚
	 * 就绪时报出错误速率，等稳定后再问一次更可靠 */
	zebra_if->speed_update_count = 0;
	zebra_if->speed_checked = 0;
	event_add_timer(zrouter.master, if_zebra_speed_update, ifp, 15,
			&zebra_if->speed_update);
	event_ignore_late_timer(zebra_if->speed_update);

	return 0;
}

/* 接口 down 时遍历依赖本接口的所有 nexthop 组，触发重新有效性
 * 检查（nhg 会因此撤销/重选存活路径） */
static void if_down_nhg_dependents(const struct interface *ifp)
{
	struct nhg_connected *rb_node_dep = NULL;
	struct zebra_if *zif = (struct zebra_if *)ifp->info;

	/* 逐个通知依赖本接口的 nhg 重新评估有效性 */
	frr_each (nhg_connected_tree, &zif->nhg_dependents, rb_node_dep) {
		frrtrace(2, frr_zebra, if_down_nhg_dependents, ifp, rb_node_dep->nhe);
		zebra_nhg_check_valid(rb_node_dep->nhe);
	}
}

/* 接口销毁前释放 nhg 依赖：摘除 nhe->ifp 反向指针并重新检查有效性；
 * 仅剩 KEEP_AROUND 保活引用的 nhg 提前减引用，加速老化回收 */
static void if_nhg_dependents_release(const struct interface *ifp)
{
	struct nhg_connected *rb_node_dep = NULL;
	struct zebra_if *zif = (struct zebra_if *)ifp->info;

	frr_each(nhg_connected_tree, &zif->nhg_dependents, rb_node_dep) {
		/* 抹掉 nhe 对已消失接口的反向指针后重新检查 */
		rb_node_dep->nhe->ifp = NULL; /* Null it out */
		zebra_nhg_check_valid(rb_node_dep->nhe);
		/* 仅剩“保活残留”引用且无其他使用者：提前减引用
		 * 让其进入老化队列，不必再等一个撤销周期 */
		if (CHECK_FLAG(rb_node_dep->nhe->flags,
			       NEXTHOP_GROUP_KEEP_AROUND) &&
		    rb_node_dep->nhe->refcnt == 1)
			zebra_nhg_decrement_ref(rb_node_dep->nhe);
	}
}

/* Called when interface is deleted. */
/* 接口销毁钩子：若 zebra 自己置过 protodown 则先撤销内核侧标记
 * （避免接口消失后 protodown 残留），随后依次释放地址树、rtadv、
 * bond 成员表、bridge/EVPN/MAC 资源，解除 nhg 依赖与 NS 容器挂钩，
 * 取消速率定时器，最后释放 zebra_if 本体 */
static int if_zebra_delete_hook(struct interface *ifp)
{
	struct zebra_if *zebra_if;
	struct zebra_l2info_bond *bond;

	if (ifp->info) {
		zebra_if = ifp->info;

		/* If we set protodown, clear our reason now from the kernel */
		if (ZEBRA_IF_IS_PROTODOWN(zebra_if) && zebra_if->protodown_rc &&
		    !ZEBRA_IF_IS_PROTODOWN_ONLY_EXTERNAL(zebra_if))
			zebra_if_update_protodown_rc(ifp, true,
						     (zebra_if->protodown_rc &
						      ~ZEBRA_PROTODOWN_ALL));

		/* 释放已安装地址链树（先清链表内容再 finish） */
		/* Free installed address chains tree. */
		if (zebra_if->ipv4_subnets)
			route_table_finish(zebra_if->ipv4_subnets);

		/* 释放 RA 配置与定时器 */
		rtadv_if_fini(zebra_if);

		/* 释放 bond 成员接口链表 */
		bond = &zebra_if->bond_info;
		if (bond && bond->mbr_zifs)
			list_delete(&bond->mbr_zifs);

		/* 依次清理：bridge 挂钩、EVPN 实例关联、该口 MAC 表项 */
		zebra_l2_bridge_if_cleanup(ifp);
		zebra_evpn_if_cleanup(zebra_if);
		zebra_evpn_mac_ifp_del(ifp);

		/* 解除并释放 nhg 依赖树 */
		if_nhg_dependents_release(ifp);
		nhg_connected_tree_free(&zebra_if->nhg_dependents);

		/* 从所属 network namespace 的接口容器摘除 */
		zebra_ns_unlink_ifp(ifp);

		/* 释放接口描述字符串 */
		XFREE(MTYPE_ZIF_DESC, zebra_if->desc);

		/* 取消速率轮询定时器并释放 zebra_if 本体 */
		event_cancel(&zebra_if->speed_update);

		XFREE(MTYPE_ZINFO, zebra_if);
	}

	return 0;
}

/* Look up an interface by identifier within a NS */
/* 按 ifindex 在指定网络命名空间内查接口（NS 隔离：同一 ifindex
 * 在不同 NS 可能对应不同接口对象） */
struct interface *if_lookup_by_index_per_ns(struct zebra_ns *ns,
					    uint32_t ifindex)
{
	struct interface *ifp = NULL;

	/* NS 接口容器以 ifindex 为键直查 */
	ifp = zebra_ns_lookup_ifp(ns, ifindex);

	return ifp;
}

/* Look up an interface by name within a NS */
/* 按接口名在指定 NS 内查接口 */
struct interface *if_lookup_by_name_per_ns(struct zebra_ns *ns,
					   const char *ifname)
{
	struct interface *ifp;

	/* NS 接口容器按名字查找 */
	ifp = zebra_ns_lookup_ifp_name(ns, ifname);

	return ifp;
}

/* 按 NS id + ifindex 查接口：先定位 zns 再转发，zns 不存在返回 NULL */
struct interface *if_lookup_by_index_per_nsid(ns_id_t ns_id, uint32_t ifindex)
{
	struct zebra_ns *zns;

	/* 先由 ns_id 定位命名空间，再按 ifindex 查 */
	zns = zebra_ns_lookup(ns_id);
	return zns ? if_lookup_by_index_per_ns(zns, ifindex) : NULL;
}

/* ifindex 转接口名（查不到返回 "unknown"，日志/show 用） */
const char *ifindex2ifname_per_ns(struct zebra_ns *zns, unsigned int ifindex)
{
	struct interface *ifp;

	return ((ifp = if_lookup_by_index_per_ns(zns, ifindex)) != NULL)
		       ? ifp->name
		       : "unknown";
}

/* Tie an interface address to its derived subnet list of addresses. */
/* 把接口地址挂到“掩码后子网”对应的地址链表（ipv4_subnets 树）：
 * 同一子网内第一个地址为主地址，后续地址标 SECONDARY（多地址
 * 场景），返回链表长度。为直连路由/地址事件的 primary/secondary
 * 语义提供索引 */
int if_subnet_add(struct interface *ifp, struct connected *ifc)
{
	struct route_node *rn;
	struct zebra_if *zebra_if;
	struct prefix cp;
	struct list *addr_list;

	assert(ifp && ifp->info && ifc);
	zebra_if = ifp->info;

	/* Get address derived subnet node and associated address list, while
	   marking
	   address secondary attribute appropriately. */
	/* 取地址掩码后的子网前缀，在 ipv4_subnets 树上找/建节点 */
	cp = *CONNECTED_PREFIX(ifc);
	apply_mask(&cp);
	rn = route_node_get(zebra_if->ipv4_subnets, &cp);

	/* 链已有地址：新地址标 SECONDARY；链为空：新建链表并锁节点，
	 * 本地址即主地址 */
	if ((addr_list = rn->info))
		SET_FLAG(ifc->flags, ZEBRA_IFA_SECONDARY);
	else {
		UNSET_FLAG(ifc->flags, ZEBRA_IFA_SECONDARY);
		rn->info = addr_list = list_new();
		route_lock_node(rn);
	}

	/* 地址追加到链表尾部 */
	/* Tie address at the tail of address list. */
	listnode_add(addr_list, ifc);

	/* 返回链表长度（>1 说明是多地址中的 secondary） */
	/* Return list element count. */
	return (addr_list->count);
}

/* Untie an interface address from its derived subnet list of addresses. */
/* 从子网地址链表中摘除地址：链表非空时，若删的是主地址则把链表
 * 首个地址提升为 primary 并先删后加地重发地址事件（协议侧看到
 * 属性变化）；链表空则释放节点。注：Linux 删主地址会连带删所有
 * secondary，内核无法绕过（XXX 注释） */
int if_subnet_delete(struct interface *ifp, struct connected *ifc)
{
	struct route_node *rn;
	struct zebra_if *zebra_if;
	struct list *addr_list;
	struct prefix cp;

	assert(ifp && ifp->info && ifc);
	zebra_if = ifp->info;

	/* 同样按掩码后子网定位节点；lookup 自带一次锁，
	 * 找不到或无地址链则告警返回 -1 */
	cp = *CONNECTED_PREFIX(ifc);
	apply_mask(&cp);

	/* Get address derived subnet node. */
	rn = route_node_lookup(zebra_if->ipv4_subnets, &cp);
	if (!(rn && rn->info)) {
		flog_warn(EC_ZEBRA_REMOVE_ADDR_UNKNOWN_SUBNET,
			  "Trying to remove an address from an unknown subnet. (please report this bug)");
		return -1;
	}
	/* 抵消 lookup 的节点锁 */
	route_unlock_node(rn);

	/* Untie address from subnet's address list. */
	addr_list = rn->info;

	/* Deleting an address that is not registered is a bug.
	 * In any case, we shouldn't decrement the lock counter if the address
	 * is unknown. */
	/* 地址不在链上属于程序 bug，不能再解节点锁 */
	if (!listnode_lookup(addr_list, ifc)) {
		flog_warn(
			EC_ZEBRA_REMOVE_UNREGISTERED_ADDR,
			"Trying to remove an address from a subnet where it is not currently registered. (please report this bug)");
		return -1;
	}

	/* 从链表摘除，并抵消 route_node_get 时新建链的节点锁 */
	listnode_delete(addr_list, ifc);
	route_unlock_node(rn);

	/* 链仍非空：需要处理主地址提升 */
	/* Return list element count, if not empty. */
	if (addr_list->count) {
		/* 删掉的是主地址：先把新链首地址重发一次 delete/add
		 * 事件，让协议侧感知“主地址换了”并去掉 SECONDARY 标记 */
		if (!CHECK_FLAG(ifc->flags, ZEBRA_IFA_SECONDARY)) {
			ifc = listgetdata(
				(struct listnode *)listhead(addr_list));
			zebra_interface_address_delete_update(ifp, ifc);
			UNSET_FLAG(ifc->flags, ZEBRA_IFA_SECONDARY);
			/* XXX: Linux kernel removes all the secondary addresses
			 * when the primary
			 * address is removed. We could try to work around that,
			 * though this is
			 * non-trivial. */
			zebra_interface_address_add_update(ifp, ifc);
		}

		return addr_list->count;
	}

	/* 链已空：释放链表并置空 info，再解锁节点（触发节点销毁） */
	/* Otherwise, free list and route node. */
	list_delete(&addr_list);
	rn->info = NULL;
	route_unlock_node(rn);

	return 0;
}

#ifndef HAVE_NETLINK
/* if_flags_mangle: A place for hacks that require mangling
 * or tweaking the interface flags.
 *
 * ******************** Solaris flags hacks **************************
 *
 * Solaris IFF_UP flag reflects only the primary interface as the
 * routing socket only sends IFINFO for the primary interface.  Hence
 * ~IFF_UP does not per se imply all the logical interfaces are also
 * down - which we only know of as addresses. Instead we must determine
 * whether the interface really is up or not according to how many
 * addresses are still attached. (Solaris always sends RTM_DELADDR if
 * an interface, logical or not, goes ~IFF_UP).
 *
 * Ie, we mangle IFF_UP to *additionally* reflect whether or not there
 * are addresses left in struct connected, not just the actual underlying
 * IFF_UP flag.
 *
 * We must hence remember the real state of IFF_UP, which we do in
 * struct zebra_if.primary_state.
 *
 * Setting IFF_UP within zebra to administratively shutdown the
 * interface will affect only the primary interface/address on Solaris.
 ************************End Solaris flags hacks ***********************
 */
static void if_flags_mangle(struct interface *ifp, uint64_t *newflags)
{
	/* 非 netlink 平台的空实现：保留接口供平台补丁改写 flags */
	return;
}

/* Update the flags field of the ifp with the new flag set provided.
 * Take whatever actions are required for any changes in flags we care
 * about.
 *
 * newflags should be the raw value, as obtained from the OS.
 */
/* 非 netlink 平台的接口 up/down 处理入口：若当前 operative，
 * 更新 flags 后检查是否转为 inoperative 并触发 if_down；反之则
 * 检查是否转 up 并触发 if_up（带直连重装） */
void if_flags_update(struct interface *ifp, uint64_t newflags)
{
	/* 先过一遍平台特定的 flags 修正（当前为空实现） */
	if_flags_mangle(ifp, &newflags);

	if (if_is_no_ptm_operative(ifp)) {
		/* operative -> inoperative? */
		/* 当前是 up：写入新 flags，若 IFF_UP 被清则触发 if_down */
		ifp->flags = newflags;
		if (!if_is_operative(ifp))
			if_down(ifp);
	} else {
		/* inoperative -> operative? */
		/* 当前是 down：写入新 flags，若变 up 则触发 if_up
		 * 并重装直连 */
		ifp->flags = newflags;
		if (if_is_operative(ifp))
			if_up(ifp, true);
	}
}
#endif

/* Wake up configured address if it is not in current kernel
   address. */
/* 接口激活时补装“已配置但尚未下发”的地址（ZEBRA_IFC_CONFIGURED
 * 且未 QUEUED）：必要时先置 IFF_UP|IFF_RUNNING，再经 dplane 下发
 * 地址并标 QUEUED；后续地址/子网事件的协议广播由内核回环通知
 * 触发（见 dplane_intf_addr_set） */
void if_addr_wakeup(struct interface *ifp)
{
	struct connected *ifc;
	struct prefix *p;
	enum zebra_dplane_result dplane_res;

	/* 遍历接口全部 connected 地址，只处理“配置过但尚未下发”的 */
	frr_each_safe (if_connected, ifp->connected, ifc) {
		p = ifc->address;

		if (CHECK_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED)
		    && !CHECK_FLAG(ifc->conf, ZEBRA_IFC_QUEUED)) {
			/* Address check. */
			/* IPv4：接口未 up 时先强拉 IFF_UP|IFF_RUNNING
			 * （首次发现接口存在时就顺手拉起并配地址） */
			if (p->family == AF_INET) {
				if (!if_is_up(ifp)) {
					/* Assume zebra is configured like
					 * following:
					 *
					 *   interface gre0
					 *    ip addr 192.0.2.1/24
					 *   !
					 *
					 * As soon as zebra becomes first aware
					 * that gre0 exists in the
					 * kernel, it will set gre0 up and
					 * configure its addresses.
					 *
					 * (This may happen at startup when the
					 * interface already exists
					 * or during runtime when the interface
					 * is added to the kernel)
					 *
					 * XXX: RUNNING is not a settable flag
					 * on any system
					 * I (paulj) am aware of.
					*/
					if_set_flags(ifp, IFF_UP | IFF_RUNNING);
					if_refresh(ifp);
				}

				/* 经 dplane 下发地址；失败则记错并跳过，
				 * 成功则标 QUEUED，后续由内核回环通知
				 * 再广播给协议并挂入子网链 */
				dplane_res = dplane_intf_addr_set(ifp, ifc);
				if (dplane_res ==
				    ZEBRA_DPLANE_REQUEST_FAILURE) {
					flog_err_sys(
						EC_ZEBRA_IFACE_ADDR_ADD_FAILED,
						"Can't set interface's address: %s",
						dplane_res2str(dplane_res));
					continue;
				}

				SET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);
				/* The address will be advertised to zebra
				 * clients when the notification
				 * from the kernel has been received.
				 * It will also be added to the interface's
				 * subnet list then. */
			}
			/* IPv6：与 IPv4 同构，未 up 先拉接口（同上注释） */
			if (p->family == AF_INET6) {
				if (!if_is_up(ifp)) {
					/* See long comment above */
					if_set_flags(ifp, IFF_UP | IFF_RUNNING);
					if_refresh(ifp);
				}


				/* 下发 IPv6 地址，失败跳过，成功标 QUEUED，
				 * 广播同样留给内核回环通知 */
				dplane_res = dplane_intf_addr_set(ifp, ifc);
				if (dplane_res ==
				    ZEBRA_DPLANE_REQUEST_FAILURE) {
					flog_err_sys(
						EC_ZEBRA_IFACE_ADDR_ADD_FAILED,
						"Can't set interface's address: %s",
						dplane_res2str(dplane_res));
					continue;
				}

				SET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);
				/* The address will be advertised to zebra
				 * clients when the notification
				 * from the kernel has been received. */
			}
		}
	}
}

/* Handle interface addition */
/* 接口新增/重现的统一入口：先挂入所属 NS 的 ifp 容器（VRF 尚未
 * 使能时退回 NS_DEFAULT），应用 multicast 配置、PTM 状态，向协议
 * 广播 IF_ADD；首次置 ACTIVE 时：若配置了 shutdown 则不拉起，否则
 * if_addr_wakeup 补装地址并按 mpls_config 同步接口 MPLS 状态 */
void if_add_update(struct interface *ifp)
{
	struct zebra_if *if_data;
	struct zebra_ns *zns;
	struct zebra_vrf *zvrf = ifp->vrf->info;

	/* VRF 尚未使能时接口可能先于 VRF 建立，退回默认 NS */
	if (zvrf->zns)
		zns = zvrf->zns;
	else
		zns = zebra_ns_lookup(NS_DEFAULT);

	/* 挂入 NS 接口容器并取私有数据 */
	zebra_ns_link_ifp(zns, ifp);
	if_data = ifp->info;
	assert(if_data);

	/* CLI 明确配置过 multicast 时覆盖内核原值 */
	if (if_data->multicast == IF_ZEBRA_DATA_ON)
		if_set_flags(ifp, IFF_MULTICAST);
	else if (if_data->multicast == IF_ZEBRA_DATA_OFF)
		if_unset_flags(ifp, IFF_MULTICAST);

	/* 同步 PTM 配置到 ifp，再向所有协议广播 IF_ADD */
	zebra_ptm_if_set_ptm_state(ifp, if_data);

	zebra_interface_add_update(ifp);

	/* dummy 设备标记，协议可据此过滤 */
	if (IS_ZEBRA_IF_DUMMY(ifp))
		SET_FLAG(ifp->status, ZEBRA_INTERFACE_DUMMY);

	/* 首次激活（或 DEL 后重现）才需要拉起动作 */
	if (!CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		SET_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE);

		/* CLI 配了 shutdown：保持关闭，不补装地址直接返回 */
		if (if_data->shutdown == IF_ZEBRA_DATA_ON) {
			if (IS_ZEBRA_DEBUG_KERNEL) {
				zlog_debug(
					"interface %s vrf %s(%u) index %d is shutdown. Won't wake it up.",
					ifp->name, ifp->vrf->name,
					ifp->vrf->vrf_id, ifp->ifindex);
			}

			frrtrace(2, frr_zebra, if_add_del_update, ifp, 2);

			return;
		}

		/* 补装已配置未下发的地址，并按 CLI 配置同步接口
		 * MPLS 开关到硬件/内核 */
		if_addr_wakeup(ifp);

		/* CLI 明确配过 mpls 时，按配置向数据面同步接口 MPLS
		 * 使能状态（on 启用 / off 关闭 encapsulation） */
		if (if_data->mpls_config == IF_ZEBRA_DATA_ON)
			dplane_intf_mpls_modify_state(ifp, true);
		else if (if_data->mpls_config == IF_ZEBRA_DATA_OFF)
			dplane_intf_mpls_modify_state(ifp, false);

		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"interface %s vrf %s(%u) index %d becomes active.",
				ifp->name, ifp->vrf->name, ifp->vrf->vrf_id,
				ifp->ifindex);

	} else {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("interface %s vrf %s(%u) index %d is added.",
				   ifp->name, ifp->vrf->name, ifp->vrf->vrf_id,
				   ifp->ifindex);
	}

	/* 记录 trace 点：1=接口新增/激活完成（2 为 shutdown 提前返回） */
	frrtrace(2, frr_zebra, if_add_del_update, ifp, 1);
}

/* Install connected routes corresponding to an interface. */
/* 接口 up 时重装直连：对内核已存在的地址（ZEBRA_IFC_REAL）广播
 * 地址添加事件，并 connected_up 安装直连路由到 RIB/内核 */
static void if_install_connected(struct interface *ifp)
{
	struct connected *ifc;

	/* 逐地址：内核已实际存在的重发地址 ADD 事件，
	 * 并重新安装直连路由 */
	frr_each (if_connected, ifp->connected, ifc) {
		if (CHECK_FLAG(ifc->conf, ZEBRA_IFC_REAL))
			zebra_interface_address_add_update(ifp, ifc);

		connected_up(ifp, ifc);
	}
}

/* Uninstall connected routes corresponding to an interface. */
/* 接口 down 时撤销直连：广播地址删除事件 + connected_down 撤销
 * 直连路由（地址对象保留，供再次 up 时重装） */
static void if_uninstall_connected(struct interface *ifp)
{
	struct connected *ifc;

	/* 逐地址广播删除事件并撤销直连路由（地址对象保留） */
	frr_each_safe (if_connected, ifp->connected, ifc) {
		zebra_interface_address_delete_update(ifp, ifc);
		connected_down(ifp, ifc);
	}
}

/* Uninstall and delete connected routes corresponding to an interface. */
/* TODO - Check why IPv4 handling here is different from install or if_down */
/* 接口删除时彻底清理地址：v4 按子网链表逐个释放（secondary 先删、
 * 主地址后删，防止内核连带删除引发事件乱序），非用户配置的地址
 * 连带 connected_free；v6 直接逐个处理。因为先销毁 ifc 再收内核通知，
 * 地址删除事件必须在此显式补发（XXX 注释） */
static void if_delete_connected(struct interface *ifp)
{
	struct connected *ifc, *ifc_next;
	struct prefix cp;
	struct route_node *rn;
	struct zebra_if *zebra_if;

	zebra_if = ifp->info;

	/* 逐地址处理；IPv4 要按子网链整体拆，避免主/次乱序 */
	for (ifc = if_connected_first(ifp->connected); ifc; ifc = ifc_next) {
		ifc_next = if_connected_next(ifp->connected, ifc);

		/* 取掩码后子网前缀，能查到 v4 子网节点则走子网链拆分 */
		cp = *CONNECTED_PREFIX(ifc);
		apply_mask(&cp);

		if (cp.family == AF_INET
		    && (rn = route_node_lookup(zebra_if->ipv4_subnets, &cp))) {
			struct listnode *anode;
			struct listnode *next;
			struct listnode *first;
			struct list *addr_list;

			route_unlock_node(rn);
			addr_list = (struct list *)rn->info;

			/* 从链首的下一个（secondary）开始遍历，最后才处理
			   主地址 first，保证删除顺序为“先次后主” */
			/* Remove addresses, secondaries first. */
			first = listhead(addr_list);
			if (first)
				for (anode = first->next; anode || first;
				     anode = next) {
					/* 轮转到链首（主地址）：取完即结束 */
					if (!anode) {
						anode = first;
						first = NULL;
					}
					next = anode->next;

					ifc = listgetdata(anode);
					connected_down(ifp, ifc);

					/* XXX: We have to send notifications
					 * here explicitly, because we destroy
					 * the ifc before receiving the
					 * notification about the address being
					 * deleted.
					 */
					/* 撤直连 + 显式补发地址删除事件（见 XXX） */
					zebra_interface_address_delete_update(
						ifp, ifc);

					/* 地址已不在内核/不在下发队列 */
					UNSET_FLAG(ifc->conf, ZEBRA_IFC_REAL);
					UNSET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);

					/* 从子网链摘除并解对应节点锁 */
					/* Remove from subnet chain. */
					list_delete_node(addr_list, anode);
					route_unlock_node(rn);

					/* 非用户配置的地址：从接口地址链表删除
					 * 并释放对象（若正好是后继节点需先重取，
					 * 防止迭代野指针） */
					if (!CHECK_FLAG(ifc->conf,
							ZEBRA_IFC_CONFIGURED)) {
						if (ifc == ifc_next)
							ifc_next = if_connected_next(
								ifp->connected,
								ifc);

						if_connected_del(ifp->connected,
								 ifc);
						connected_free(&ifc);
					}
				}

			/* 子网链全部拆完：释放链表并置空 info，再解锁
			   节点（锁计数归零后节点销毁） */
			/* Free chain list and respective route node. */
			list_delete(&addr_list);
			rn->info = NULL;
			route_unlock_node(rn);
		} else if (cp.family == AF_INET6) {
			/* IPv6 无子网链概念：逐个直接撤直连、发删除
			 * 事件、清 REAL/QUEUED，非配置地址连带释放 */
			connected_down(ifp, ifc);

			zebra_interface_address_delete_update(ifp, ifc);

			UNSET_FLAG(ifc->conf, ZEBRA_IFC_REAL);
			UNSET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);

			if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED)) {
				if_connected_del(ifp->connected, ifc);
				connected_free(&ifc);
			}
		}
	}
}

/* Handle an interface delete event */
/* 接口删除事件入口：接口仍 up 则拒绝（防误删，报错返回）；置
 * INACTIVE 后依次：撤销直连、若仍在 VRF 中移回 default VRF（仅
 * vrf-lite 后端，netns 避免同名冲突）、向协议广播 IF_DEL、解除 NS
 * 挂钩；广播后才重置 ifindex（客户端可能需要旧值处理删除）；最后
 * 复位二层/EVPN 属性，无用户配置时真正 if_delete 释放对象。
 * 入参为二级指针：EVPN 路径可能在本调用中释放 ifp，调用方需读
 * 回结果判空 */
void if_delete_update(struct interface **pifp)
{
	struct zebra_if *zif;
	struct interface *ifp = *pifp;

	/* 接口仍 up 就被删除：异常时序，报错并拒绝处理 */
	if (if_is_up(ifp)) {
		flog_err(
			EC_LIB_INTERFACE,
			"interface %s vrf %s(%u) index %d is still up while being deleted.",
			ifp->name, ifp->vrf->name, ifp->vrf->vrf_id,
			ifp->ifindex);
		return;
	}

	/* 已经处于 INACTIVE：幂等直接返回 */
	if (!CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE))
		return;

	/* Mark interface as inactive */
	UNSET_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE);

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("interface %s vrf %s(%u) index %d is now inactive.",
			   ifp->name, ifp->vrf->name, ifp->vrf->vrf_id,
			   ifp->ifindex);

	frrtrace(2, frr_zebra, if_add_del_update, ifp, 0);

	/* 彻底清理全部地址（v4 按子网链、v6 逐个） */
	/* Delete connected routes from the kernel. */
	if_delete_connected(ifp);

	/* if the ifp is in a vrf, move it to default so vrf can be deleted if
	 * desired. This operation is not done for netns implementation to avoid
	 * collision with interface with the same name in the default vrf (can
	 * occur with this implementation whereas it is not possible with
	 * vrf-lite).
	 */
	/* vrf-lite 后端才移回 default VRF；netns 同名会冲突 */
	if (ifp->vrf->vrf_id && !vrf_is_backend_netns())
		if_handle_vrf_change(ifp, VRF_DEFAULT);

	/* 先向协议广播 IF_DEL（此时 ifindex 仍有效），再摘 NS 挂钩 */
	/* Send out notification on interface delete. */
	zebra_interface_delete_update(ifp);

	zebra_ns_unlink_ifp(ifp);

	/* 广播完才重置 ifindex，供客户端用旧值收尾 */
	/* Update ifindex after distributing the delete message.  This is in
	   case any client needs to have the old value of ifindex available
	   while processing the deletion.  Each client daemon is responsible
	   for setting ifindex to IFINDEX_INTERNAL after processing the
	   interface deletion message. */
	if_set_index(ifp, IFINDEX_INTERNAL);

	UNSET_FLAG(ifp->status, ZEBRA_INTERFACE_VRF_LOOPBACK);

	/* 复位二层/EVPN 属性，让接口对象若存活则回到“干净”状态 */
	/* Reset some zebra interface params to default values. */
	zif = ifp->info;
	if (zif) {
		zebra_evpn_if_cleanup(zif);
		zif->zif_type = ZEBRA_IF_OTHER;
		zif->zif_slave_type = ZEBRA_IF_SLAVE_NONE;
		memset(&zif->l2info, 0, sizeof(union zebra_l2if_info));
		memset(&zif->brslave_info, 0,
		       sizeof(struct zebra_l2info_brslave));
		zebra_evpn_mac_ifp_del(ifp);
	}

	/* 无任何用户配置挂在对象上：真正释放 ifp（注意传二级指针，
	 * 调用方需判 *pifp 是否已为 NULL） */
	if (!ifp->configured) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("interface %s is being deleted from the system",
				   ifp->name);
		if_delete(pifp);
	}
}

/* VRF change for an interface */
/* 接口 VRF 归属变更（迁入/迁出/删除时移回 default）：先撤直连、
 * 清 RFC 5549 邻居与 RA 学习邻居，向旧 VRF 发 IF_DEL 通知；VRF 设备
 * 本身到此为止（协议自行重建）；普通成员口切到新 VRF 后再发 IF_ADD，
 * 协议侧整体视图为“旧接口消失 + 新接口出现” */
void if_handle_vrf_change(struct interface *ifp, vrf_id_t vrf_id)
{
	vrf_id_t old_vrf_id;

	/* 先记住旧 VRF，后续通知参数需要对照 */
	old_vrf_id = ifp->vrf->vrf_id;

	/* 撤销全部直连路由/地址事件 */
	/* Uninstall connected routes. */
	if_uninstall_connected(ifp);

	/* 清掉 RFC 5549 建的 IPv4 邻居表项与 RA 学习的邻居地址 */
	/* Delete any IPv4 neighbors created to implement RFC 5549 */
	if_nbr_ipv6ll_to_ipv4ll_neigh_del_all(ifp);

	/* Delete all neighbor addresses learnt through IPv6 RA */
	if_down_del_nbr_connected(ifp);

	/* 向协议发“旧 VRF 下接口删除”通知（DELETE） */
	/* Send out notification on interface VRF change. */
	/* This is to issue a DELETE, as appropriate. */
	zebra_interface_vrf_update_del(ifp, vrf_id);

	/* VRF 设备本身切 VRF 无意义，到此为止由协议自行重建 */
	if (if_is_vrf(ifp))
		return;

	/* 把接口挂到新 VRF 对象上 */
	/* update VRF */
	if_update_to_new_vrf(ifp, vrf_id);

	/* 向新 VRF 发接口 ADD 通知，完成“删旧加新”迁移 */
	/* Send out notification on interface VRF change. */
	/* This is to issue an ADD, if needed. */
	zebra_interface_vrf_update_add(ifp, old_vrf_id);
}

/* 从 IPv6 链路本地地址按 EUI-64 逆推 MAC（翻转第 8 字节的 0x02），
 * BGP unnumbered 的 v6ll->v4ll 邻居映射依赖此推导 */
static void ipv6_ll_address_to_mac(struct in6_addr *address, uint8_t *mac)
{
	/* EUI-64：取地址后 8 字节去中间 02:00:5e，首字节翻转 U/L 位 */
	mac[0] = address->s6_addr[8] ^ 0x02;
	mac[1] = address->s6_addr[9];
	mac[2] = address->s6_addr[10];
	mac[3] = address->s6_addr[13];
	mac[4] = address->s6_addr[14];
	mac[5] = address->s6_addr[15];
}

/* 为 BGP unnumbered（RFC 5549）维护“169.254.x.x + MAC”的 IPv4 邻居
 * 表项：netlink 无 update 类型，先删后加；重加时强制安装（接口
 * flap 后若不置 PERMANENT/REACHABLE，内核会重新解析残留表项并
 * 失败废弃）；同时记录 neigh_mac/v6 地址及“本接口是否建过表项”，
 * 供外部误删后重建与接口删除时回收 */
void if_nbr_mac_to_ipv4ll_neigh_update(struct interface *ifp,
				       char mac[6],
				       struct in6_addr *address,
				       int add)
{
	struct zebra_vrf *zvrf = ifp->vrf->info;
	struct zebra_if *zif = ifp->info;
	char buf[16] = "169.254.0.1";
	struct in_addr ipv4_ll;
	ns_id_t ns_id;

	/* RFC 5549 约定的固定 v4ll 地址（同一邻居共用，靠 ifindex 区分） */
	inet_pton(AF_INET, buf, &ipv4_ll);

	/* 表项操作要落到接口所属 NS 中执行 */
	ns_id = zvrf->zns->ns_id;

	/*
	 * Remove and re-add any existing neighbor entry for this address,
	 * since Netlink doesn't currently offer update message types.
	 */
	/* 先删旧表项（force=true），为重新添加让路 */
	kernel_neigh_update(0, ifp->ifindex, (void *)&ipv4_ll.s_addr, mac, 6,
			    ns_id, AF_INET, true);

	/* Add new neighbor entry.
	 *
	 * We force installation even if current neighbor entry is the same.
	 * Since this function is used to refresh our MAC entries after an
	 * interface flap, if we don't force in our custom entries with their
	 * state set to PERMANENT or REACHABLE then the kernel will attempt to
	 * resolve our leftover entries, fail, mark them unreachable and then
	 * they'll be useless to us.
	 */
	/* add=1 时强制重新安装；add=0 则只删不建（删除场景） */
	if (add)
		kernel_neigh_update(add, ifp->ifindex, (void *)&ipv4_ll.s_addr,
				    mac, 6, ns_id, AF_INET, true);

	/* 缓存本次使用的 MAC，供外部误删后重建对照 */
	memcpy(&zif->neigh_mac[0], &mac[0], 6);

	/*
	 * We need to note whether or not we originated a v6
	 * neighbor entry for this interface.  So that when
	 * someone unwisely accidentally deletes this entry
	 * we can shove it back in.
	 */
	/* 记录“本接口自建过表项”及对应 v6 地址，并累计变更计数 */
	zif->v6_2_v4_ll_neigh_entry = !!add;
	memcpy(&zif->v6_2_v4_ll_addr6, address, sizeof(*address));

	zvrf->neigh_updates++;
}

/* 由 v6 链路本地地址推导 MAC 后转接 v4ll 邻居表项维护 */
void if_nbr_ipv6ll_to_ipv4ll_neigh_update(struct interface *ifp,
					  struct in6_addr *address, int add)
{

	char mac[6];

	/* 先由 v6ll 逆推 MAC，再转接表项增删 */
	ipv6_ll_address_to_mac(address, (uint8_t *)mac);
	if_nbr_mac_to_ipv4ll_neigh_update(ifp, mac, address, add);
}

/* 接口 up 时：为所有 RA 学到的 v6 邻居补建 v4ll 映射表项 */
static void if_nbr_ipv6ll_to_ipv4ll_neigh_add_all(struct interface *ifp)
{
	if (listhead(ifp->nbr_connected)) {
		struct nbr_connected *nbr_connected;
		struct listnode *node;

		/* 逐 RA 学习邻居：add=1 补建 v4ll 映射 */
		for (ALL_LIST_ELEMENTS_RO(ifp->nbr_connected, node,
					  nbr_connected))
			if_nbr_ipv6ll_to_ipv4ll_neigh_update(
				ifp, &nbr_connected->address->u.prefix6, 1);
	}
}

/* 接口 down/VRF 变更时：删除全部 v4ll 映射邻居表项（add=0 只删不建） */
void if_nbr_ipv6ll_to_ipv4ll_neigh_del_all(struct interface *ifp)
{
	if (listhead(ifp->nbr_connected)) {
		struct nbr_connected *nbr_connected;
		struct listnode *node;

		/* 逐 RA 学习邻居：add=0 只删不建 */
		for (ALL_LIST_ELEMENTS_RO(ifp->nbr_connected, node,
					  nbr_connected))
			if_nbr_ipv6ll_to_ipv4ll_neigh_update(
				ifp, &nbr_connected->address->u.prefix6, 0);
	}
}

/* 接口 down 时释放全部通过 IPv6 RA 学到的邻居地址（nbr_connected） */
static void if_down_del_nbr_connected(struct interface *ifp)
{
	struct nbr_connected *nbr_connected;
	struct listnode *node, *nnode;

	/* 安全遍历：逐个摘链并释放邻居地址对象 */
	for (ALL_LIST_ELEMENTS(ifp->nbr_connected, node, nnode,
			       nbr_connected)) {
		listnode_delete(ifp->nbr_connected, nbr_connected);
		nbr_connected_free(nbr_connected);
	}
}

/* bond 成员口 up/down 会改变 bond 聚合带宽：成员状态变化后若 bond
 * 尚未排速率更新定时器，则延后 1s 触发一次（避免风暴） */
static void if_handle_bond_speed_change(struct interface *ifp)
{
	struct zebra_if *zif = ifp->info;
	struct zebra_l2info_bondslave *part_of_bond;

	/* 非 bond 成员口直接忽略 */
	if (!IS_ZEBRA_IF_BOND_SLAVE(ifp))
		return;

	part_of_bond = &zif->bondslave_info;

	/* 切到所属 bond 的 zif：若尚未排队则延后 1s 触发速率重查，
	 * 多个成员同时变化会合并为一次 */
	if (part_of_bond->bond_if) {
		zif = part_of_bond->bond_if->info;

		if (!event_is_scheduled(zif->speed_update))
			event_add_timer(zrouter.master, if_zebra_speed_update, part_of_bond->bond_if, 1,
					&zif->speed_update);
	}
}

/* Interface is up. */
/* 接口 up 统一处理：记录 up 统计；PTM 未通过则不广播（等待 PTM
 * 就绪）；依次：向协议发 IF_UP、重建 v4ll 邻居、启动 RA、重装直连
 * 路由（install_connected 可控）、重同步因 down 被删的 nhg、按接口
 * 类型联动 EVPN/VxLAN（vxlan/SVI/macvlan up、ES 状态、MH 上行口），
 * 立即重探速率、补装 pending 地址、联动 bond 带宽，最后触发内核
 * RIB 更新（等待被延迟的内核路由重选） */
void if_up(struct interface *ifp, bool install_connected)
{
	struct zebra_if *zif;
	struct interface *link_if;

	zif = ifp->info;
	/* 记录 up 次数与最后 up 时间（show interface 统计） */
	zif->up_count++;
	frr_timestamp(2, zif->up_last, sizeof(zif->up_last));

	/* Notify the protocol daemons. */
	/* PTM 使能且未通过检查：不广播 up，等 PTM 就绪再补 */
	if (ifp->ptm_enable && (ifp->ptm_status == ZEBRA_PTM_STATUS_DOWN)) {
		flog_warn(EC_ZEBRA_PTM_NOT_READY,
			  "%s: interface %s hasn't passed ptm check",
			  __func__, ifp->name);
		return;
	}
	/* 向协议广播 IF_UP */
	zebra_interface_up_update(ifp);

	/* 重建 RFC 5549 v4ll 邻居表项，并启动 IPv6 RA 发送 */
	if_nbr_ipv6ll_to_ipv4ll_neigh_add_all(ifp);

	rtadv_if_up(zif);

	/* 重装直连路由（接口删除重建场景可能由调用方接管） */
	/* Install connected routes to the kernel. */
	if (install_connected)
		if_install_connected(ifp);

	/*
	 * Interface associated NHG's have been deleted on
	 * interface down events, now that this interface
	 * is coming back up, let's resync the zebra -> dplane
	 * nhg's so that they can be continued to be used.
	 */
	/* 重新同步依赖本接口的 nhg（down 期间已被撤销） */
	zebra_interface_nhg_reinstall(ifp);

	/* Handle interface up for specific types for EVPN. Non-VxLAN interfaces
	 * are checked to see if (remote) neighbor entries need to be installed
	 * on them for ARP suppression.
	 */
	/* 按接口类型联动 EVPN/VxLAN：隧道口、SVI（bridge/vlan）、
	 * macvlan 各自重建对应的二层转发/抑制表项 */
	if (IS_ZEBRA_IF_VXLAN(ifp))
		zebra_vxlan_if_up(ifp);
	else if (IS_ZEBRA_IF_BRIDGE(ifp)) {
		link_if = ifp;
		zebra_vxlan_svi_up(ifp, link_if);
	} else if (IS_ZEBRA_IF_VLAN(ifp)) {
		link_if = zif->link;
		if (link_if)
			zebra_vxlan_svi_up(ifp, link_if);
	} else if (IS_ZEBRA_IF_MACVLAN(ifp)) {
		zebra_vxlan_macvlan_up(ifp);
	}

	/* EVPN 以太网段（ES）与 MH 上行口的运行态同步 */
	if (zif->es_info.es)
		zebra_evpn_es_if_oper_state_change(zif, true /*up*/);

	if (zif->flags & ZIF_FLAG_EVPN_MH_UPLINK)
		zebra_evpn_mh_uplink_oper_update(zif);

	/* up 后立即重探一次速率（link 可能已换速），补装
	 * 待下发地址，联动 bond 带宽重算 */
	event_add_timer(zrouter.master, if_zebra_speed_update, ifp, 0,
			&zif->speed_update);
	event_ignore_late_timer(zif->speed_update);

	if_addr_wakeup(ifp);

	if_handle_bond_speed_change(ifp);

	/* 触发一次内核路由重同步：down 期间被拒收的内核路由
	 * 在本接口上重新选型安装 */
	rib_update_handle_vrf_all(RIB_UPDATE_KERNEL, ZEBRA_ROUTE_KERNEL);
}

/* Interface goes down.  We have to manage different behavior of based
   OS. */
/* 接口 down 统一处理：记录 down 统计；停 RA、通知依赖本接口的
 * nhg 重新检查有效性，联动 EVPN/VxLAN 表项净化（ARP 抑制的远程
 * 邻居）、ES/上行口状态变更，再向协议发 IF_DOWN、撤销直连、删
 * v4ll 映射与 RA 学习邻居、联动 bond 带宽，最后触发内核 RIB
 * 更新（接口 down 引发的路由重选） */
void if_down(struct interface *ifp)
{
	struct zebra_if *zif;
	struct interface *link_if;

	zif = ifp->info;
	/* 记录 down 次数与最后 down 时间（show interface 统计） */
	zif->down_count++;
	/* 记录最后一次 down 的时刻字符串 */
	frr_timestamp(2, zif->down_last, sizeof(zif->down_last));

	/* 停发 RA，并通知依赖本接口的 nhg 重新检查有效性 */
	rtadv_stop_ra(ifp, true);
	if_down_nhg_dependents(ifp);

	/* Handle interface down for specific types for EVPN. Non-VxLAN
	 * interfaces
	 * are checked to see if (remote) neighbor entries need to be purged
	 * for ARP suppression.
	 */
	/* 按接口类型净化 EVPN/VxLAN 表项（远程邻居/抑制条目） */
	if (IS_ZEBRA_IF_VXLAN(ifp))
		zebra_vxlan_if_down(ifp);
	else if (IS_ZEBRA_IF_BRIDGE(ifp)) {
		link_if = ifp;
		zebra_vxlan_svi_down(ifp, link_if);
	} else if (IS_ZEBRA_IF_VLAN(ifp)) {
		link_if = zif->link;
		if (link_if)
			zebra_vxlan_svi_down(ifp, link_if);
	} else if (IS_ZEBRA_IF_MACVLAN(ifp)) {
		zebra_vxlan_macvlan_down(ifp);
	}

	/* ES 置 down、MH 上行口重新计算聚合可用性 */
	if (zif->es_info.es)
		zebra_evpn_es_if_oper_state_change(zif, false /*up*/);

	if (zif->flags & ZIF_FLAG_EVPN_MH_UPLINK)
		zebra_evpn_mh_uplink_oper_update(zif);

	/* 广播 IF_DOWN → 撤直连 → 删 v4ll 映射表项与 RA 学习邻居 */
	/* Notify to the protocol daemons. */
	zebra_interface_down_update(ifp);

	/* Uninstall connected routes from the kernel. */
	/* 撤销本接口全部直连路由 */
	if_uninstall_connected(ifp);

	/* 清空本接口建立的 v6ll->v4ll 邻居映射表项（RFC 5549） */
	if_nbr_ipv6ll_to_ipv4ll_neigh_del_all(ifp);

	/* Delete all neighbor addresses learnt through IPv6 RA */
	/* 删除经 IPv6 RA 学习到的所有邻居地址（nbr_connected） */
	if_down_del_nbr_connected(ifp);

	/* bond 带宽随成员变化重算；最后触发内核 RIB 更新，
	 * 让走本接口的路由尽快重选 */
	if_handle_bond_speed_change(ifp);

	/* 全 VRF 触发一次内核路由重选更新（接口 down 事件驱动） */
	rib_update_handle_vrf_all(RIB_UPDATE_INTERFACE_DOWN, ZEBRA_ROUTE_KERNEL);
}

/* 接口属性刷新入口：非 Linux 平台需回读内核 flags（Linux 由 netlink
 * 事件直接驱动，此处为空操作） */
void if_refresh(struct interface *ifp)
{
#ifndef GNU_LINUX
	if_get_flags(ifp);
#endif
}

/* 记录子接口的父链路：保存父口 nsid/ifindex 并关联父接口对象
 * （父口未到时 link 暂为 NULL，由 dump 结束后的 fixup 补齐） */
void zebra_if_update_link(struct interface *ifp, ifindex_t link_ifindex,
			  ns_id_t ns_id)
{
	struct zebra_if *zif;

	/* 保存父口 nsid/ifindex 并尝试关联父接口对象，
	 * 父口尚未到达时 link 暂为 NULL，留给 fixup 补齐 */
	zif = (struct zebra_if *)ifp->info;
	zif->link_nsid = ns_id;
	zif->link_ifindex = link_ifindex;
	zif->link = if_lookup_by_index_per_ns(zebra_ns_lookup(ns_id),
					      link_ifindex);
}

/*
 * Callback for per-ns link fixup iteration
 */
/* 逐接口补齐三类上层->下层关联：bond 成员映射到 bond、
 * SVI/子口的 link 指向父口、VLAN<->SVI 的 EVPN 接入广播域映射 */
static int zif_link_fixup_cb(struct interface *ifp, void *arg)
{
	struct zebra_if *zif;

	zif = ifp->info;
	/* bond 成员：记录了 bond ifindex 但尚未映射时补映射 */
	/* update bond-member to bond linkages */
	if ((IS_ZEBRA_IF_BOND_SLAVE(ifp)) &&
	    (zif->bondslave_info.bond_ifindex != IFINDEX_INTERNAL) &&
	    !zif->bondslave_info.bond_if) {
		if (IS_ZEBRA_DEBUG_EVPN_MH_ES || IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("bond mbr %s map to bond %d", zif->ifp->name,
				   zif->bondslave_info.bond_ifindex);
		zebra_l2_map_slave_to_bond(zif, ifp->vrf->vrf_id);
	}

	/* SVI/子口：link 缺失时按记录的 link_ifindex 补指父口 */
	/* update SVI linkages */
	if ((zif->link_ifindex != IFINDEX_INTERNAL) && !zif->link) {
		zif->link = if_lookup_by_index_per_nsid(zif->link_nsid,
							zif->link_ifindex);
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("interface %s/%d's lower fixup to %s/%d",
				   ifp->name, ifp->ifindex,
				   zif->link ? zif->link->name : "unk",
				   zif->link_ifindex);
	}

	/* VLAN 口：刷新 VLAN<=>SVI 的 EVPN 接入广播域映射 */
	/* Update VLAN<=>SVI map */
	if (IS_ZEBRA_IF_VLAN(ifp))
		zebra_evpn_acc_bd_svi_set(zif, NULL,
					  !!if_is_operative(ifp));

	return NS_WALK_CONTINUE;
}

/*
 * during initial link dump kernel does not order lower devices before
 * upper devices so we need to fixup link dependencies at the end of dump
 */
/* 全量 dump 结束时内核不保证先报下层设备再报上层设备，遍历本 NS
 * 全部接口统一补齐 link 依赖（见 zif_link_fixup_cb） */
void zebra_if_update_all_links(struct zebra_ns *zns)
{
	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("fixup link dependencies");

	zebra_ns_ifp_walk(zns, zif_link_fixup_cb, NULL);
}

/* protodown 请求去重判断：接口尚无真实 ifindex（FRR 信息不足）、
 * 目标状态与当前/已排队（SET/UNSET 到 dplane）状态及原因位图完全
 * 一致时返回 true，避免重复向数据面下发 protodown 请求 */
static bool if_ignore_set_protodown(const struct interface *ifp, bool new_down,
				    uint32_t new_protodown_rc)
{
	struct zebra_if *zif;
	bool old_down, old_set_down, old_unset_down;

	zif = ifp->info;

	/*
	 * FRR does not have enough data to make this request
	 */
	/* 尚无真实 ifindex：FRR 信息不足，无法判断/下发 */
	if (ifp->ifindex == IFINDEX_INTERNAL)
		return true;

	/* Current state as we know it */
	/* 采集当前已知状态：实际 protodown、以及 SET/UNSET 请求
	 * 是否已排队到 dplane 尚未确认 */
	old_down = !!(ZEBRA_IF_IS_PROTODOWN(zif));
	old_set_down = !!CHECK_FLAG(zif->flags, ZIF_FLAG_SET_PROTODOWN);
	old_unset_down = !!CHECK_FLAG(zif->flags, ZIF_FLAG_UNSET_PROTODOWN);

	/* 原因位图相同才有必要继续比较状态 */
	if (new_protodown_rc == zif->protodown_rc) {
		/* Early return if already down & reason bitfield matches */
		/* 目标状态与当前实际状态一致：忽略 */
		if (new_down == old_down) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"Ignoring request to set protodown %s for interface %s (%u): protodown %s is already set (reason bitfield: old 0x%x new 0x%x)",
					new_down ? "on" : "off", ifp->name,
					ifp->ifindex, new_down ? "on" : "off",
					zif->protodown_rc, new_protodown_rc);

			frrtrace(5, frr_zebra, if_protodown, ifp, new_down, zif->protodown_rc,
				 new_protodown_rc, 2);

			return true;
		}

		/* Early return if already set queued & reason bitfield matches
		 */
		/* 目标 down 的 SET 请求已在队列中：忽略重复请求 */
		if (new_down && old_set_down) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"Ignoring request to set protodown %s for interface %s (%u): protodown %s is already queued to dplane (reason bitfield: old 0x%x new 0x%x)",
					new_down ? "on" : "off", ifp->name,
					ifp->ifindex, new_down ? "on" : "off",
					zif->protodown_rc, new_protodown_rc);

			frrtrace(5, frr_zebra, if_protodown, ifp, new_down, zif->protodown_rc,
				 new_protodown_rc, 3);

			return true;
		}

		/* Early return if already unset queued & reason bitfield
		 * matches */
		/* 目标 up 的 UNSET 请求已在队列中：同样忽略 */
		if (!new_down && old_unset_down) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"Ignoring request to set protodown %s for interface %s (%u): protodown %s is already queued to dplane (reason bitfield: old 0x%x new 0x%x)",
					new_down ? "on" : "off", ifp->name,
					ifp->ifindex, new_down ? "on" : "off",
					zif->protodown_rc, new_protodown_rc);

			frrtrace(5, frr_zebra, if_protodown, ifp, new_down, zif->protodown_rc,
				 new_protodown_rc, 4);

			return true;
		}
	}

	return false;
}

/* 整体替换 protodown 原因位图并触发数据面同步：置 SET/UNSET
 * PROTODOWN 标志后经 dplane_intf_update 下发内核（异步），回环事件
 * 收敛真实状态；非 netlink 平台不支持。已同态/已排队则返回 1 */
int zebra_if_update_protodown_rc(struct interface *ifp, bool new_down,
				 uint32_t new_protodown_rc)
{
	struct zebra_if *zif;

	zif = ifp->info;

	/* Check if we already have this state or it's queued */
	/* 同态或已在队列：直接返回 1 不重复下发 */
	if (if_ignore_set_protodown(ifp, new_down, new_protodown_rc))
		return 1;

	zlog_info(
		"Setting protodown %s - interface %s (%u): reason bitfield change from 0x%x --> 0x%x",
		new_down ? "on" : "off", ifp->name, ifp->ifindex,
		zif->protodown_rc, new_protodown_rc);

	frrtrace(5, frr_zebra, if_protodown, ifp, new_down, zif->protodown_rc, new_protodown_rc, 1);

	/* 整体替换原因位图，并按目标方向置 SET/UNSET 排队标志 */
	zif->protodown_rc = new_protodown_rc;

	if (new_down)
		SET_FLAG(zif->flags, ZIF_FLAG_SET_PROTODOWN);
	else
		SET_FLAG(zif->flags, ZIF_FLAG_UNSET_PROTODOWN);

	/* 经 dplane 异步下发，真实状态等内核回环事件收敛 */
#ifdef HAVE_NETLINK
	dplane_intf_update(ifp);
#else
	zlog_warn("Protodown is not supported on this platform");
#endif
	return 0;
}

/* 单一原因位置/清的便捷入口：按位图 OR/AND-NOT 合并后转接
 * update_protodown_rc（供 EVPN/VRRP 等协议模块调用） */
int zebra_if_set_protodown(struct interface *ifp, bool new_down,
			   enum protodown_reasons new_reason)
{
	struct zebra_if *zif;
	uint32_t new_protodown_rc;

	zif = ifp->info;

	/* 置位用 OR、清位用 AND-NOT，合并出新位图后转接统一入口 */
	if (new_down)
		new_protodown_rc = zif->protodown_rc | new_reason;
	else
		new_protodown_rc = zif->protodown_rc & ~new_reason;

	return zebra_if_update_protodown_rc(ifp, new_down, new_protodown_rc);
}

/*
 * Handle an interface events based on info in a dplane context object.
 * This runs in the main pthread, using the info in the context object to
 * modify an interface.
 */
/* 按 dplane 上下文处理接口地址事件（zebra 主线程）：把内核回环
 * 通知里的地址属性同步到 connected 对象 */
static void zebra_if_addr_update_ctx(struct zebra_dplane_ctx *ctx,
				     struct interface *ifp)
{
	uint8_t flags = 0;
	const char *label = NULL;
	uint32_t metric = METRIC_MAX;
	const struct prefix *addr, *dest = NULL;
	enum dplane_op_e op;

	/* 接口对象已消失：丢弃事件 */
	if (!ifp)
		return;

	/* 取操作类型与地址本体 */
	op = dplane_ctx_get_op(ctx);
	addr = dplane_ctx_get_intf_addr(ctx);

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("%s: %s: ifindex %s(%u), addr %pFX", __func__,
			   dplane_op2str(dplane_ctx_get_op(ctx)), ifp->name,
			   ifp->ifindex, addr);

	/* Handle tentative IPv6 addresses:
	 * - On UP interfaces: skip tentative addresses (DAD in progress)
	 * - On DOWN interfaces: accept tentative addresses (DAD cannot run)
	 */
	/* IPv6 tentative 地址（DAD 重复地址检测中）处理策略：接口 up
	 * 时 DAD 正在进行，结果未定先跳过；接口 down 时 DAD 永远跑不完，
	 * 直接接受该地址以免永久丢失 */
	if (op == DPLANE_OP_INTF_ADDR_ADD && addr->family == AF_INET6 &&
	    dplane_ctx_intf_is_tentative(ctx)) {
		/* up 接口：tentative 直接丢弃本次 ADD */
		if (if_is_operative(ifp)) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("%s: %s: Tentative addr %pFX on UP interface %s, skipping",
					   __func__,
					   dplane_op2str(op), addr, ifp->name);
			return;
		}
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: %s: Tentative addr %pFX on DOWN interface %s - accepted (DAD cannot run)",
				   __func__, dplane_op2str(op), addr,
				   ifp->name);
	}

	/* 对端/广播地址：前缀长为 0 表示没有，归一化为 NULL */
	/* Is there a peer or broadcast address? */
	dest = dplane_ctx_get_intf_dest(ctx);
	if (dest->prefixlen == 0)
		dest = NULL;

	/* ctx 标记为点对点地址：记 PEER 属性 */
	if (dplane_ctx_intf_is_connected(ctx))
		SET_FLAG(flags, ZEBRA_IFA_PEER);

	/* Flags. */
	/* 从 ctx 提取地址属性：点对点（peer）、secondary（同子网非首
	 * 地址）、noprefixroute（不自动生成直连路由）、接口别名 label
	 * （与接口名相同时视为无 label）、地址 metric */
	if (dplane_ctx_intf_is_secondary(ctx))
		SET_FLAG(flags, ZEBRA_IFA_SECONDARY);

	if (dplane_ctx_intf_is_noprefixroute(ctx))
		SET_FLAG(flags, ZEBRA_IFA_NOPREFIXROUTE);

	/* Label? */
	/* 取地址别名；与接口名相同的内核默认 label 视同没有 */
	if (dplane_ctx_intf_has_label(ctx))
		label = dplane_ctx_get_intf_label(ctx);

	if (label && strcmp(ifp->name, label) == 0)
		label = NULL;

	metric = dplane_ctx_get_intf_metric(ctx);

	/* Register interface address to the interface. */
	/* 同步到 connected 对象：ADD 走 connected_add（内部会广播地址
	 * 事件并安装直连）；DEL 按 v4 点对点/普通两种形式或 v6 删除，
	 * 非 CONFIGURED 的由 connected 自行释放 */
	/* IPv4：ADD 走 connected_add；DEL 区分点对点（带对端地址）
	 * 与普通两种形式 */
	if (addr->family == AF_INET) {
		if (op == DPLANE_OP_INTF_ADDR_ADD)
			connected_add_ipv4(
				ifp, flags, &addr->u.prefix4, addr->prefixlen,
				dest ? &dest->u.prefix4 : NULL, label, metric);
		else if (CHECK_FLAG(flags, ZEBRA_IFA_PEER)) {
			/* Delete with a peer address */
			connected_delete_ipv4(ifp, flags, &addr->u.prefix4,
					      addr->prefixlen,
					      &dest->u.prefix4);
		} else
			connected_delete_ipv4(ifp, flags, &addr->u.prefix4,
					      addr->prefixlen, NULL);
	}

	/* IPv6：ADD/DEL 直接对应 connected 增删 */
	if (addr->family == AF_INET6) {
		if (op == DPLANE_OP_INTF_ADDR_ADD) {
			connected_add_ipv6(ifp, flags, &addr->u.prefix6,
					   dest ? &dest->u.prefix6 : NULL,
					   addr->prefixlen, label, metric);
		} else
			connected_delete_ipv6(ifp, &addr->u.prefix6, NULL,
					      addr->prefixlen);
	}

	/*
	 * Linux kernel does not send route delete on interface down/addr del
	 * so we have to re-process routes it owns (i.e. kernel routes)
	 */
	/* 地址删除：Linux 在接口 down/删地址时不会发对应的路由删除
	 * 通知，需触发 RIB 重新处理内核路由（撤销失效的内核路由） */
	if (op != DPLANE_OP_INTF_ADDR_ADD)
		rib_update(RIB_UPDATE_KERNEL);
}

/* 处理出方向接口更新请求（protodown）的 dplane 完成结果：内核
 * 操作成功则把 ZIF_FLAG_PROTODOWN 同步为 ctx 携带的真实状态；
 * 无论成败都清掉 SET/UNSET 排队标志（允许后续新请求通过去重
 * 判断），失败仅记日志不重试（由发起方协议模块重新触发） */
static void zebra_if_update_ctx(struct zebra_dplane_ctx *ctx,
				struct interface *ifp)
{
	enum zebra_dplane_result dp_res;
	struct zebra_if *zif;
	bool pd_reason_val;
	bool down;

	if (!ifp) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: Can't find ifp", __func__);

		return;
	}

	/* 取 dplane 回传结果：成功/失败、protodown 目标态与原因值 */
	dp_res = dplane_ctx_get_status(ctx);
	pd_reason_val = dplane_ctx_get_intf_pd_reason_val(ctx);
	down = dplane_ctx_intf_is_protodown(ctx);

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("%s: %s: if %s(%u) ctx-protodown %s ctx-reason %d",
			   __func__, dplane_op2str(dplane_ctx_get_op(ctx)),
			   ifp->name, ifp->ifindex, down ? "on" : "off",
			   pd_reason_val);

	zif = ifp->info;
	/* 私有数据已释放（接口正在删除）：不再同步，直接返回 */
	if (!zif) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: if %s(%u) zebra info pointer is NULL",
				   __func__, ifp->name, ifp->ifindex);

		frrtrace(5, frr_zebra, if_upd_ctx_dplane_result, ifp, down, pd_reason_val,
			 dplane_ctx_get_op(ctx), 1);

		return;
	}

	/* 内核操作失败：不同步真实状态，只清排队标志后收尾 */
	if (dp_res != ZEBRA_DPLANE_REQUEST_SUCCESS) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: if %s(%u) dplane update failed",
				   __func__, ifp->name, ifp->ifindex);

		frrtrace(5, frr_zebra, if_upd_ctx_dplane_result, ifp, down, pd_reason_val,
			 dplane_ctx_get_op(ctx), 2);

		goto done;
	}

	frrtrace(5, frr_zebra, if_upd_ctx_dplane_result, ifp, down, pd_reason_val,
		 dplane_ctx_get_op(ctx), 0);

	/* Update our info */
	/* 成功：把本地 PROTODOWN 状态对齐内核实际生效值 */
	COND_FLAG(zif->flags, ZIF_FLAG_PROTODOWN, down);

	/* 无论成败都清 SET/UNSET 排队标志，解除后续去重封锁 */
	done:
	/* Clear our dplane flags */
	UNSET_FLAG(zif->flags, ZIF_FLAG_SET_PROTODOWN);
	UNSET_FLAG(zif->flags, ZIF_FLAG_UNSET_PROTODOWN);
}
/*
 * Handle netconf change from a dplane context object; runs in the main
 * pthread so it can update zebra data structs.
 */
/*
 * zebra_if_netconf_update_ctx：
 *   根据数据平面上下文，处理 netconf 变更。
 *
 *   该函数运行在 zebra 主线程中，因此可以直接更新 zebra 内部数据结构。
 *
 *   处理对象分三类（由 ifindex 决定）：
 *     1. DPLANE_NETCONF_IFINDEX_ALL：
 *          所有接口的全局默认设置（zrouter.all_*）
 *     2. DPLANE_NETCONF_IFINDEX_DEFAULT：
 *          新接口的默认设置（zrouter.default_*）
 *     3. 具体接口：
 *          更新该接口对应的 zebra_if 结构（zif->*）
 *
 *   涉及的三类 netconf 状态：
 *     - mpls：MPLS 输入是否启用（仅针对具体接口，无 v4/v6 之分）
 *     - mcast：多播转发是否启用（按 afi 区分 v4/v6）
 *     - linkdown：接口 down 时是否丢弃路由（按 afi 区分 v4/v6）
 *
 * 参数：
 *   ctx      数据平面上下文，包含 afi、netconf 状态、ifindex 等
 *   ifp      根据 ifindex 查找到的接口（可能为 NULL）
 *   ifindex  接口索引；也可能是特殊值 ALL / DEFAULT
 */
static void zebra_if_netconf_update_ctx(struct zebra_dplane_ctx *ctx,
					struct interface *ifp,
					ifindex_t ifindex)
{
	struct zebra_if *zif = NULL;             /* 接口的 zebra 扩展信息 */
	afi_t afi;                               /* 地址族 AFI_IP / AFI_IP6 */
	enum dplane_netconf_status_e mpls, mcast_on, linkdown; /* 三类 netconf 状态 */
	bool *mcast_set, *linkdown_set;          /* 指向要更新的目标布尔变量 */

	/*
	 * 如果不是特殊 ifindex，但 ifp 又找不到，则无法更新具体接口。
	 * 记录调试日志并直接返回。
	 */
	if (!ifp && ifindex != -1 && ifindex != -2) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: Can't find ifp(%u)", __func__, ifindex);

		return;
	}

	/* 从上下文中提取 afi 及三类 netconf 状态 */
	afi = dplane_ctx_get_afi(ctx);
	mpls = dplane_ctx_get_netconf_mpls(ctx);
	linkdown = dplane_ctx_get_netconf_linkdown(ctx);
	mcast_on = dplane_ctx_get_netconf_mcast(ctx);

	/*
	 * 根据 ifindex 的值，选择要更新的目标变量：
	 */
	if (ifindex == DPLANE_NETCONF_IFINDEX_ALL) {
		/*
		 * 全局所有接口的默认设置。
		 * 根据 afi 分别指向 zrouter 中 v4 或 v6 的全局变量。
		 */
		if (afi == AFI_IP) {
			mcast_set = &zrouter.all_mc_forwardingv4;
			linkdown_set = &zrouter.all_linkdownv4;
		} else {
			mcast_set = &zrouter.all_mc_forwardingv6;
			linkdown_set = &zrouter.all_linkdownv6;
		}
	} else if (ifindex == DPLANE_NETCONF_IFINDEX_DEFAULT) {
		/*
		 * 新接口的默认设置。
		 * 根据 afi 分别指向 zrouter 中 v4 或 v6 的默认变量。
		 */
		if (afi == AFI_IP) {
			mcast_set = &zrouter.default_mc_forwardingv4;
			linkdown_set = &zrouter.default_linkdownv4;
		} else {
			mcast_set = &zrouter.default_mc_forwardingv6;
			linkdown_set = &zrouter.default_linkdownv6;
		}
	} else {
		/*
		 * 具体接口：需要更新该接口对应的 zebra_if 结构。
		 */
		zif = ifp->info;

		/* 如果 zebra_if 信息为空，说明接口尚未正确初始化，直接返回 */
		if (!zif) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"%s: if %s(%u) zebra info pointer is NULL",
					__func__, ifp ? ifp->name : "(null)",
					ifp ? ifp->ifindex : ifindex);
			return;
		}

		/* 根据 afi，选择接口上对应的多播/ linkdown 字段 */
		if (afi == AFI_IP) {
			mcast_set = &zif->v4mcast_on;
			linkdown_set = &zif->linkdown;
		} else {
			mcast_set = &zif->v6mcast_on;
			linkdown_set = &zif->linkdownv6;
		}

		/*
		 * mpls netconf data is neither v4 or v6 it's AF_MPLS!
		 */
		/*
		 * 注意：MPLS 的 netconf 数据不分 v4/v6，它属于 AF_MPLS。
		 * 因此无论 afi 是 v4 还是 v6，都统一更新 zif->mpls。
		 */
		if (mpls == DPLANE_NETCONF_STATUS_ENABLED) {
			zif->mpls = true;

			/*
			 * 通知 zebra 的 MPLS 子系统：某个接口开启了 MPLS 输入。
			 * 这通常用于在必要时初始化 MPLS 相关的资源。
			 */
			zebra_mpls_turned_on();
		} else if (mpls == DPLANE_NETCONF_STATUS_DISABLED)
			zif->mpls = false;
	}

	/*
	 * 更新 linkdown 目标变量。
	 * 仅当状态明确为 ENABLED 或 DISABLED 时才更新，
	 * 其他状态（如 UNSET）保持原值不变。
	 */
	if (linkdown == DPLANE_NETCONF_STATUS_ENABLED)
		*linkdown_set = true;
	else if (linkdown == DPLANE_NETCONF_STATUS_DISABLED)
		*linkdown_set = false;

	/*
	 * 更新 mcast（多播转发）目标变量。
	 * 同样只在状态明确时更新。
	 */
	if (mcast_on == DPLANE_NETCONF_STATUS_ENABLED)
		*mcast_set = true;
	else if (mcast_on == DPLANE_NETCONF_STATUS_DISABLED)
		*mcast_set = false;

	/* 调试日志：打印更新后的 netconf 状态 */
	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug(
			"%s: afi: %d if %s, ifindex %d, mpls %s mc_forwarding: %s linkdown %s",
			__func__, afi, ifp ? ifp->name : "Global",
			ifp ? ifp->ifindex : ifindex,
			(zif ? (zif->mpls ? "ON" : "OFF") : "OFF"),
			(*mcast_set ? "ON" : "OFF"),
			(*linkdown_set ? "ON" : "OFF"));
}

/* VRF 设备生命周期处理（由 dplane 的 INTF_UPDATE/DELETE 事件触发）：
 * DELETE 直接 vrf_delete；新增/变更先做 tableid 冲突检查（同一路由表
 * id 对应到不同 VRF 属于配置错误，zebra 直接退出），再 vrf_update
 * 创建/刷新 VRF、记录内核真实 table_id（必须先于 vrf_enable，否则
 * 延迟安装的静态路由拿不到正确表 id），最后 vrf_enable 激活 */
static void interface_vrf_change(enum dplane_op_e op, ifindex_t ifindex,
				 const char *name, uint32_t tableid,
				 ns_id_t ns_id)
{
	struct vrf *vrf;
	struct zebra_vrf *zvrf = NULL;

	if (op == DPLANE_OP_INTF_DELETE) {
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("DPLANE_OP_INTF_DELETE for VRF %s(%u)", name,
				   ifindex);

		/* 按 ifindex 当作 vrf_id 查找，命中则删除 VRF 对象 */
		vrf = vrf_lookup_by_id((vrf_id_t)ifindex);
		if (!vrf) {
			flog_warn(EC_ZEBRA_VRF_NOT_FOUND,
				  "%s(%u): vrf not found", name, ifindex);
			return;
		}

		frrtrace(4, frr_zebra, if_vrf_change, ifindex, name, tableid, 0);
		vrf_delete(vrf);
	} else {
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug(
				"DPLANE_OP_INTF_UPDATE for VRF %s(%u) table %u",
				name, ifindex, tableid);

		/*
		 * For a given tableid, if there already exists a vrf and it
		 * is different from the current vrf to be operated, then there
		 * is a misconfiguration and zebra will exit.
		 */
		vrf_id_t exist_id = zebra_vrf_lookup_by_table(tableid, ns_id);

		/* 非默认表或就是默认 VRF 名：才需做冲突检查 */
		if (exist_id != VRF_DEFAULT || strmatch(name, VRF_DEFAULT_NAME)) {
			vrf = vrf_lookup_by_id(exist_id);

			/* 新 id 与已存表都查不到对应对象：非法创建，退出 */
			if (!vrf_lookup_by_id((vrf_id_t)ifindex) && !vrf) {
				flog_err(EC_ZEBRA_VRF_NOT_FOUND,
					 "VRF %s id %u does not exist", name,
					 ifindex);
				frr_exit_with_buffer_flush(-1);
			}

			/* 同一 tableid 已属于另一个不同名 VRF：冲突退出 */
			if (vrf && strcmp(name, vrf->name)) {
				flog_err(EC_ZEBRA_VRF_MISCONFIGURED,
					 "VRF %s id %u table id overlaps existing vrf %s(%d), misconfiguration exiting",
					 name, ifindex, vrf->name, vrf->vrf_id);
				frr_exit_with_buffer_flush(-1);
			}
		}

		frrtrace(4, frr_zebra, if_vrf_change, ifindex, name, tableid, 1);
		/* 创建/刷新 VRF 对象 */
		vrf = vrf_update((vrf_id_t)ifindex, name);
		if (!vrf) {
			flog_err(EC_LIB_INTERFACE, "VRF %s id %u not created",
				 name, ifindex);
			return;
		}

		/*
		 * This is the only place that we get the actual kernel table_id
		 * being used.  We need it to set the table_id of the routes
		 * we are passing to the kernel.... And to throw some totally
		 * awesome parties. that too.
		 *
		 * At this point we *must* have a zvrf because the vrf_create
		 * callback creates one.  We *must* set the table id
		 * before the vrf_enable because of( at the very least )
		 * static routes being delayed for installation until
		 * during the vrf_enable callbacks.
		 */
		/* 记录内核真实 table_id，必须先于 vrf_enable */
		zvrf = (struct zebra_vrf *)vrf->info;
		zvrf->table_id = tableid;

		/* 激活 VRF（触发延迟安装静态路由等 enable 回调） */
		if (!vrf_enable(vrf)) {
			flog_err(EC_LIB_INTERFACE,
				 "Failed to enable VRF %s id %u", name,
				 ifindex);
			return;
		}
	}
}

/*
 *  Note: on netlink systems, there should be a 1-to-1 mapping
 * between interface names and ifindex values.
 */
/* 为接口设置新 ifindex（netlink 平台 name<->ifindex 应一一对应）：
 * 若该 ifindex 已属于另一接口，视为改名事件——旧名字对象走删除
 * 流程（旧接口仍 up 时额外告警，结果不可靠）；占用保留内部值
 * 则只报错不删 */
static void set_ifindex(struct interface *ifp, ifindex_t ifi_index,
			struct zebra_ns *zns)
{
	struct interface *oifp;

	/* 看新 ifindex 当前是否已属于另一个接口 */
	oifp = if_lookup_by_index_per_ns(zns, ifi_index);
	if ((oifp != NULL) && (oifp != ifp)) {
		/* 占用者是保留内部值：只报错不删 */
		if (ifi_index == IFINDEX_INTERNAL)
			flog_err(
				EC_LIB_INTERFACE,
				"Netlink is setting interface %s ifindex to reserved internal value %u",
				ifp->name, ifi_index);
		else {
			/* 否则视为改名：旧名字对象走删除流程，
			 * 旧接口仍 up 时额外告警（结果不可靠） */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"interface index %d was renamed from %s to %s",
					ifi_index, oifp->name, ifp->name);
			if (if_is_up(oifp))
				flog_err(
					EC_LIB_INTERFACE,
					"interface rename detected on up interface: index %d was renamed from %s to %s, results are uncertain!",
					ifi_index, oifp->name, ifp->name);
			if_delete_update(&oifp);
		}
	}
	/* 无冲突则直接把新 ifindex 写入接口 */
	if_set_index(ifp, ifi_index);
}

/* 设置接口主/从属类型；主类型变化时重新初始化 EVPN ES 信息
 * （如变为 bond 需按 bond 建立 ES）；bond 变非 bond 不处理（异常场景） */
static inline void zebra_if_set_ziftype(struct interface *ifp,
					enum zebra_iftype zif_type,
					enum zebra_slave_iftype zif_slave_type)
{
	struct zebra_if *zif;

	zif = (struct zebra_if *)ifp->info;
	/* 从属类型直接覆盖，无需联动 */
	zif->zif_slave_type = zif_slave_type;

	/* 主类型变化才需重新初始化 EVPN ES（如新变 bond） */
	if (zif->zif_type != zif_type) {
		zif->zif_type = zif_type;
		/* If the if_type has been set to bond initialize ES info
		 * against it. XXX - note that we don't handle the case where
		 * a zif changes from bond to non-bond; it is really
		 * an unexpected/error condition.
		 */
		zebra_evpn_if_init(zif);
	}
}

/* 同步接口硬件地址（MAC）：全零 MAC 视为无效（长度置 0），
 * 避免协议/展示层拿到假地址 */
static void interface_update_hw_addr(struct zebra_dplane_ctx *ctx,
				     struct interface *ifp)
{
	uint8_t hw_addr[INTERFACE_HWADDR_MAX];
	uint i, hw_addr_len;

	/* 先从 ctx 拷出完整硬件地址 */
	hw_addr_len = dplane_ctx_get_ifp_hw_addr_len(ctx);
	memcpy(hw_addr, dplane_ctx_get_ifp_hw_addr(ctx), hw_addr_len);

	/* 扫描是否全零：全零视为无效地址，长度置 0 */
	for (i = 0; i < hw_addr_len; i++)
		if (hw_addr[i] != 0)
			break;

	if (i == hw_addr_len)
		hw_addr_len = 0;

	/* 写回接口硬件地址状态 */
	if_update_state_hw_addr(ifp, hw_addr, hw_addr_len);
}

/* 按接口类型把 ctx 携带的二层信息同步到 zebra 二层表：bridge 建/清
 * bridge_if、vlan 更新父口映射并联动 EVPN 接入广播域 SVI、vxlan/gre
 * 更新隧道信息并在父链路已知时补 link；add 区分新增与删除 */
static void interface_update_l2info(struct zebra_dplane_ctx *ctx,
				    struct interface *ifp,
				    enum zebra_iftype zif_type, int add,
				    ns_id_t link_nsid)
{
	const struct zebra_l2info_vxlan *vxlan_info;
	const struct zebra_l2info_gre *gre_info;

	/* 按主类型分发到对应二层表维护例程 */
	switch (zif_type) {
	case ZEBRA_IF_BRIDGE:
		/* bridge：建/更新 bridge_if */
		zebra_l2_bridge_add_update(ifp,
					   dplane_ctx_get_ifp_bridge_info(ctx));
		break;
	case ZEBRA_IF_VLAN:
		/* vlan：更新父口映射并联动 EVPN 接入广播域 SVI */
		zebra_l2_vlanif_update(ifp, dplane_ctx_get_ifp_vlan_info(ctx));
		zebra_evpn_acc_bd_svi_set(ifp->info, NULL,
					  !!if_is_operative(ifp));
		break;
	case ZEBRA_IF_VXLAN:
		/* vxlan：更新隧道信息，父链路已知时补 link */
		vxlan_info = dplane_ctx_get_ifp_vxlan_info(ctx);
		zebra_l2_vxlanif_add_update(ifp, vxlan_info, add);
		if (link_nsid != NS_UNKNOWN && vxlan_info->ifindex_link)
			zebra_if_update_link(ifp, vxlan_info->ifindex_link,
					     link_nsid);
		break;
	case ZEBRA_IF_GRE:
	case ZEBRA_IF_IP6GRE:
	case ZEBRA_IF_GRETAP:
	case ZEBRA_IF_IP6GRETAP:
		/* gre/ip6gre/gretap：同步隧道信息并在父链路已知时补 link */
		gre_info = dplane_ctx_get_ifp_gre_info(ctx);
		zebra_l2_greif_add_update(ifp, gre_info, add);
		if (link_nsid != NS_UNKNOWN && gre_info->ifindex_link)
			zebra_if_update_link(ifp, gre_info->ifindex_link,
					     link_nsid);
		break;
	case ZEBRA_IF_OTHER:
	case ZEBRA_IF_VRF:
	case ZEBRA_IF_MACVLAN:
	case ZEBRA_IF_VETH:
	case ZEBRA_IF_BOND:
	case ZEBRA_IF_DUMMY:
		break;
	}
}

/* 内核回报的 protodown 原因位图是否“只有 FRR 自己置的那一位”
 * （用于识别回环事件是自己造成的，而非外部原因） */
static bool is_if_protodown_reason_only_frr(uint32_t rc_bitfield)
{
	uint8_t frr_protodown_r_bit = if_netlink_get_frr_protodown_r_bit();

	return (rc_bitfield == (((uint32_t)1) << frr_protodown_r_bit));
}

/* 处理内核回环的 protodown 状态变化：非 FRR 单方面原因引起的外部
 * protodown 记 EXTERNAL 位（回环收敛）；状态真正变化时同步
 * ZIF_FLAG_PROTODOWN。若本口是 EVPN ES bond 成员：内核可能在我们
 * 重配前自行清掉 protodown，需把旧状态“ reinstat（重申）”回 dplane
 * （已在排队中或撤销原因是外部时除外），保证 FRR 意图不被丢失 */
static void interface_if_protodown(struct interface *ifp, bool protodown,
				   uint32_t rc_bitfield)
{
	struct zebra_if *zif = ifp->info;
	bool old_protodown, reason_extern;

	reason_extern = !!CHECK_FLAG(zif->protodown_rc,
				     ZEBRA_PROTODOWN_EXTERNAL);
	/*
	 * Set our reason code to note it wasn't us.
	 * If the reason we got from the kernel is ONLY frr though, don't
	 * set it.
	 */
	COND_FLAG(zif->protodown_rc, ZEBRA_PROTODOWN_EXTERNAL,
		  protodown && rc_bitfield &&
			  !is_if_protodown_reason_only_frr(rc_bitfield));


	old_protodown = !!ZEBRA_IF_IS_PROTODOWN(zif);
	if (protodown == old_protodown)
		return;

	if (IS_ZEBRA_DEBUG_EVPN_MH_ES || IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("interface %s dplane change, protodown %s curr reason_extern %u",
			   ifp->name, protodown ? "on" : "off", reason_extern);

	/* Set protodown, respectively */
	COND_FLAG(zif->flags, ZIF_FLAG_PROTODOWN, protodown);

	/* 仅对 EVPN ES bond 成员需要“重申”：内核可能抢在 FRR
	 * 重配前自行清掉 protodown，需把旧意图重新排入 dplane */
	if (zebra_evpn_is_es_bond_member(ifp)) {
		/* Check it's not already being sent to the dplane first */
		/* 目标 down 但 SET 已在队列：不重复下发，返回 */
		if (protodown &&
		    CHECK_FLAG(zif->flags, ZIF_FLAG_SET_PROTODOWN)) {
			if (IS_ZEBRA_DEBUG_EVPN_MH_ES || IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"bond mbr %s protodown on recv'd but already sent protodown on to the dplane",
					ifp->name);

			frrtrace(5, frr_zebra, if_protodown, ifp, old_protodown, 0, 0, 6);

			return;
		}

		if (!protodown &&
		    CHECK_FLAG(zif->flags, ZIF_FLAG_UNSET_PROTODOWN)) {
			if (IS_ZEBRA_DEBUG_EVPN_MH_ES || IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"bond mbr %s protodown off recv'd but already sent protodown off to the dplane",
					ifp->name);

			frrtrace(5, frr_zebra, if_protodown, ifp, old_protodown, 0, 0, 7);

			return;
		}

		/* 目标 up 但 UNSET 已在队列：同样不重复下发 */
		if (!protodown && reason_extern) {
			if (IS_ZEBRA_DEBUG_EVPN_MH_ES || IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("bond member %s has protodown reason external and clear the reason, skip reinstall.",
					   ifp->name);

			frrtrace(5, frr_zebra, if_protodown, ifp, old_protodown, zif->protodown_rc,
				 0, 10);

			return;
		}

		if (IS_ZEBRA_DEBUG_EVPN_MH_ES || IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"bond mbr %s reinstate protodown %s in the dplane",
				ifp->name, old_protodown ? "on" : "off");

		frrtrace(5, frr_zebra, if_protodown, ifp, old_protodown, 0, 0, 8);

		if (old_protodown)
			SET_FLAG(zif->flags, ZIF_FLAG_SET_PROTODOWN);
		else
			SET_FLAG(zif->flags, ZIF_FLAG_UNSET_PROTODOWN);

		dplane_intf_update(zif->ifp);
	}

	frrtrace(5, frr_zebra, if_protodown, ifp, old_protodown, 0, 0, 5);
}

/* 接口删除前的 protodown 残留清理：仅清 FRR 自己的原因位（保留
 * EXTERNAL），并下发一次 protodown 更新使内核侧状态收敛 */
static void if_sweep_protodown(struct zebra_if *zif)
{
	bool protodown;

	protodown = !!ZEBRA_IF_IS_PROTODOWN(zif);

	if (!protodown)
		return;

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("interface %s sweeping protodown %s reason 0x%x",
			   zif->ifp->name, protodown ? "on" : "off",
			   zif->protodown_rc);

	frrtrace(5, frr_zebra, if_protodown, zif->ifp, protodown, zif->protodown_rc, 0, 9);

	/* Only clear our reason codes, leave external if it was set */
	UNSET_FLAG(zif->protodown_rc, ZEBRA_PROTODOWN_ALL);
	dplane_intf_update(zif->ifp);
}

/* 同步 vxlan 接口的 VLAN<->VNI 映射表：按 bridge vlan 的 range
 * begin/end 标志展开成逐个 (vid, vni) 条目建哈希表，整体替换
 * 接口的 VNI 表（SVD 模式下由内核维护映射） */
static void
interface_bridge_vxlan_vlan_vni_map_update(struct zebra_dplane_ctx *ctx,
					   struct interface *ifp)
{
	const struct zebra_vxlan_vni_array *vniarray =
		dplane_ctx_get_ifp_vxlan_vni_array(ctx);
	struct zebra_vxlan_vni vni_start, vni_end;
	struct hash *vni_table = NULL;
	struct zebra_vxlan_vni vni;
	vni_t vni_id;
	vlanid_t vid;
	int i;

	if (vniarray == NULL)
		return;

	memset(&vni_start, 0, sizeof(vni_start));
	memset(&vni_end, 0, sizeof(vni_end));

	/* 展开 range：遇 BEGIN 记起点，遇 END 记终点，单条（非 range）
	   则起=终，后续统一按 [start..end] 逐个生成映射条目 */
	for (i = 0; i < vniarray->count; i++) {
		uint16_t flags = vniarray->vnis[i].flags;

		if (flags & DPLANE_BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			vni_start = vniarray->vnis[i];
			continue;
		}

		if (flags & DPLANE_BRIDGE_VLAN_INFO_RANGE_END)
			vni_end = vniarray->vnis[i];

		/* 不是 range 结尾（包含单条情形）：起终点都取当前项 */
		if (!(flags & DPLANE_BRIDGE_VLAN_INFO_RANGE_END)) {
			vni_start = vniarray->vnis[i];
			vni_end = vniarray->vnis[i];
		}

		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug(
				"Vlan-Vni(%d:%d-%d:%d) update for VxLAN IF %s(%u)",
				vni_start.access_vlan, vni_end.access_vlan,
				vni_start.vni, vni_end.vni, ifp->name,
				ifp->ifindex);

		/* 首次遇到条目时懒建 VNI 哈希表 */
		if (!vni_table)
			vni_table = zebra_vxlan_vni_table_create();

		/* 从起始 (vid,vni) 逐递直到终点，逐个插入 (vid,vni) 映射 */
		for (vid = vni_start.access_vlan, vni_id = vni_start.vni;
		     vid <= vni_end.access_vlan; vid++, vni_id++) {

			memset(&vni, 0, sizeof(vni));
			vni.vni = vni_id;
			vni.access_vlan = vid;
			/* hash_get 命中则复用，未命中则按 alloc 新建条目 */
			(void)hash_get(vni_table, &vni, zebra_vxlan_vni_alloc);
		}

		/* 重置起终点，准备下一个 range（若有） */
		memset(&vni_start, 0, sizeof(vni_start));
		memset(&vni_end, 0, sizeof(vni_end));
	}

	if (vni_table)
		/* 整体替换接口的 VNI 表（建过表才替换） */
		zebra_vxlan_if_vni_table_add_update(ifp, vni_table);
}

/* 同步 vxlan 接口二层信息：SVD 口先更新 VLAN-VNI 映射表，再从
 * bridge vlan info 中提取 PVID 作为接入 VLAN（no_afspec/no_bridge_vlan_info
 * 标志表示内核未携带相应属性，直接跳过） */
static void interface_bridge_vxlan_update(struct zebra_dplane_ctx *ctx,
					  struct interface *ifp)
{
	struct zebra_if *zif = ifp->info;
	const struct zebra_dplane_bridge_vlan_info *bvinfo;

	/* 内核未携带 afspec 属性：无信息可同步，直接返回 */
	if (dplane_ctx_get_ifp_no_afspec(ctx))
		return;

	/* SVD 模式：先刷新 VLAN<->VNI 映射表 */
	if (IS_ZEBRA_VXLAN_IF_SVD(zif))
		interface_bridge_vxlan_vlan_vni_map_update(ctx, ifp);

	/* 无 bridge vlan info 或不含 PVID 标志：不取接入 VLAN */
	if (dplane_ctx_get_ifp_no_bridge_vlan_info(ctx))
		return;

	bvinfo = dplane_ctx_get_ifp_bridge_vlan_info(ctx);

	if (!(bvinfo->flags & DPLANE_BRIDGE_VLAN_INFO_PVID))
		return;

	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("Access VLAN %u for VxLAN IF %s(%u)", bvinfo->vid,
			   ifp->name, ifp->ifindex);

	frrtrace(2, frr_zebra, if_br_vxlan_upd, ifp, bvinfo->vid);
	zebra_l2_vxlanif_update_access_vlan(ifp, bvinfo->vid);
}

/* 重建 bridge 成员的 VLAN 位图：先用新位图替换旧位图，按内核
 * 上报的 vlan range（begin/end 或单 vid）逐个置位；无 vlan info
 * 时恢复旧位图（避免误清） */
static void interface_bridge_vlan_update(struct zebra_dplane_ctx *ctx,
					 struct interface *ifp)
{
	struct zebra_if *zif = ifp->info;
	const struct zebra_dplane_bridge_vlan_info_array *bvarray;
	struct zebra_dplane_bridge_vlan_info bvinfo;
	bitfield_t old_vlan_bitmap;
	uint16_t vid_range_start = 0;
	int32_t i;

	/* cache the old bitmap addrs */
	old_vlan_bitmap = zif->vlan_bitmap;
	/* create a new bitmap space for re-eval */
	bf_init(zif->vlan_bitmap, IF_VLAN_BITMAP_MAX);

	/* Could we have multiple bridge vlan infos? */
	bvarray = dplane_ctx_get_ifp_bridge_vlan_info_array(ctx);
	if (!bvarray) {
		bf_free(zif->vlan_bitmap);
		zif->vlan_bitmap = old_vlan_bitmap;

		return;
	}

	for (i = 0; i < bvarray->count; i++) {
		bvinfo = bvarray->array[i];

		if (bvinfo.flags & DPLANE_BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			vid_range_start = bvinfo.vid;
			continue;
		}

		if (!(bvinfo.flags & DPLANE_BRIDGE_VLAN_INFO_RANGE_END))
			vid_range_start = bvinfo.vid;

		zebra_vlan_bitmap_compute(ifp, vid_range_start, bvinfo.vid);
	}

	zebra_vlan_mbr_re_eval(ifp, old_vlan_bitmap);
	bf_free(old_vlan_bitmap);
}

/* bridge 类接口二层信息处理入口：vxlan 口走 VLAN-VNI/PVID 同步；
 * 其他接口仅在自身维护 vlan 位图（bf 已初始化，即关心 VLAN 集合
 * 的 bridge 成员口）时重建位图 */
static void interface_bridge_handling(struct zebra_dplane_ctx *ctx,
				      struct interface *ifp,
				      enum zebra_iftype zif_type)
{
	struct zebra_if *zif;

	if (!ifp) {
		zlog_warn("Cannot find bridge if %s(%u)",
			  dplane_ctx_get_ifname(ctx),
			  dplane_ctx_get_ifindex(ctx));
		return;
	}

	if (IS_ZEBRA_IF_VXLAN(ifp))
		return interface_bridge_vxlan_update(ctx, ifp);

	/*
	 * build vlan bitmap associated with this interface if that
	 * device type is interested in the vlans
	 */
	zif = ifp->info;
	if (bf_is_inited(zif->vlan_bitmap))
		interface_bridge_vlan_update(ctx, ifp);
}

/*
 * zebra_if_dplane_ifp_handling：
 *   处理数据平面下发的接口（LINK）变更上下文。
 *
 *   该函数由 zebra_if_dplane_result() 在 op 为 INTF_INSTALL/UPDATE/DELETE
 *   且结果为 QUEUED（即来自 inbound 路径）时调用。
 *
 *   它根据操作类型，将内核上报的接口信息同步到 zebra：
 *     - DPLANE_OP_INTF_DELETE：删除接口，并处理桥接/VxLAN/聚合/VRF 等特殊情况
 *     - 其他（新增/更新）：
 *         · 如果接口尚不存在或未激活，则创建接口并初始化其各类属性
 *         · 如果接口 VRF 归属发生变化，则处理 VRF 切换
 *         · 否则视为普通更新，刷新接口状态、L2 信息、上下线状态等
 *
 *   同时，会处理接口描述（description）的更新。
 *
 * 参数：
 *   ctx  数据平面上下文，包含接口名、ns_id、ifindex、类型、flags、MTU 等
 */
static void zebra_if_dplane_ifp_handling(struct zebra_dplane_ctx *ctx)
{
	enum dplane_op_e op = dplane_ctx_get_op(ctx);         /* 操作类型 */
	const char *name = dplane_ctx_get_ifname(ctx);        /* 接口名称 */
	ns_id_t ns_id = dplane_ctx_get_ns_id(ctx);            /* 命名空间 ID */
	ifindex_t ifindex = dplane_ctx_get_ifindex(ctx);      /* 接口索引 */
	ifindex_t bond_ifindex = dplane_ctx_get_ifp_bond_ifindex(ctx); /* 聚合接口索引 */
	uint32_t tableid = dplane_ctx_get_ifp_table_id(ctx);  /* 接口对应的路由表 ID（VRF 用） */
	enum zebra_iftype zif_type = dplane_ctx_get_ifp_zif_type(ctx); /* zebra 接口类型 */
	struct interface *ifp;                                /* zebra 接口对象 */
	struct zebra_ns *zns;                                 /* zebra 命名空间 */

	/* 根据 ns_id 查找命名空间 */
	zns = zebra_ns_lookup(ns_id);
	if (!zns) {
		/* 命名空间不存在，说明系统状态异常，记录错误并返回 */
		flog_err(EC_ZEBRA_NS_NO_DEFAULT, "Where is our namespace?");
		return;
	}

	/* 调试日志：打印操作类型、接口名、接口索引 */
	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("%s for %s(%u)", dplane_op2str(op), name, ifindex);

	/* 按接口名在命名空间中查找接口对象 */
	ifp = if_lookup_by_name_per_ns(zns, name);

	if (op == DPLANE_OP_INTF_DELETE) {
		/*
		 * ============================
		 * 删除接口分支（INTF_DELETE）
		 * ============================
		 */
		/* Delete interface notification from kernel */
		/* 如果接口在 zebra 中不存在，则无需处理，直接返回 */
		if (ifp == NULL) {
			if (IS_ZEBRA_DEBUG_EVENT)
				zlog_debug(
					"Delete LINK received for unknown interface %s(%u)",
					name, ifindex);
			return;
		}

		/* 上报 trace：接口处理（删除） */
		frrtrace(3, frr_zebra, if_dplane_ifp_handling, name, ifindex, 0);

		/*
		 * 如果该接口是聚合（bond）接口，通知 L2 子系统该聚合接口已删除。
		 */
		if (IS_ZEBRA_IF_BOND(ifp))
			zebra_l2if_update_bond(ifp, false);

		/*
		 * 如果该接口是聚合从接口（bond slave），通知 L2 子系统该从接口已移除。
		 */
		if (IS_ZEBRA_IF_BOND_SLAVE(ifp))
			zebra_l2if_update_bond_slave(ifp, bond_ifindex, false);

		/* Special handling for bridge or VxLAN interfaces. */
		/*
		 * 桥接接口或 VxLAN 接口有各自专门的删除逻辑：
		 *   - 桥接接口：删除桥接相关 L2 信息
		 *   - VxLAN 接口：删除 VxLAN 相关 L2 信息
		 */
		if (IS_ZEBRA_IF_BRIDGE(ifp))
			zebra_l2_bridge_del(ifp);
		else if (IS_ZEBRA_IF_VXLAN(ifp))
			zebra_l2_vxlanif_del(ifp);

		/* 执行接口删除，并通知上层（客户端、其他子系统） */
		if_delete_update(&ifp);

		/*
		 * 如果该接口是 VRF 接口（且后端不是 netns），
		 * 需要额外处理 VRF 与接口的关系变化（VRF 设备被删除）。
		 */
		if (zif_type == ZEBRA_IF_VRF && !vrf_is_backend_netns())
			interface_vrf_change(op, ifindex, name, tableid, ns_id);
	} else {
		/*
		 * ============================
		 * 新增/更新接口分支（INTF_INSTALL/UPDATE）
		 * ============================
		 */
		ifindex_t master_ifindex, bridge_ifindex, link_ifindex; /* 主接口/桥/链路索引 */
		enum zebra_slave_iftype zif_slave_type;                 /* 从接口类型 */
		uint8_t bypass;                                          /* bond slave bypass 标志 */
		uint64_t flags;                                          /* 接口 flags */
		vrf_id_t vrf_id;                                         /* 接口 VRF ID */
		uint32_t mtu;                                            /* 接口 MTU */
		ns_id_t link_nsid;                                       /* 链路所在命名空间 */
		struct zebra_if *zif;                                    /* zebra 接口扩展信息 */
		bool protodown, protodown_set, startup;                  /* protodown 状态与启动标志 */
		uint32_t rc_bitfield;                                    /* protodown 原因位域 */
		uint8_t old_hw_addr[INTERFACE_HWADDR_MAX];               /* 旧 MAC 地址，用于判断是否变化 */
		char *desc;                                              /* 接口描述字符串 */
		uint8_t family;                                          /* 接口所属地址族（link family） */
		uint64_t change_flags;                                   /* 变更标志（来自 netlink 变更掩码） */

		/* If VRF, create or update the VRF structure itself. */
		/*
		 * 如果该接口是 VRF 接口（且后端不是 netns），
		 * 需要创建或更新对应的 VRF 结构。
		 */
		if (zif_type == ZEBRA_IF_VRF && !vrf_is_backend_netns())
			interface_vrf_change(op, ifindex, name, tableid, ns_id);

		/* 从上下文中提取各类接口字段 */
		master_ifindex = dplane_ctx_get_ifp_master_ifindex(ctx);
		zif_slave_type = dplane_ctx_get_ifp_zif_slave_type(ctx);
		bridge_ifindex = dplane_ctx_get_ifp_bridge_ifindex(ctx);
		bond_ifindex = dplane_ctx_get_ifp_bond_ifindex(ctx);
		bypass = dplane_ctx_get_ifp_bypass(ctx);
		flags = dplane_ctx_get_ifp_flags(ctx);
		vrf_id = dplane_ctx_get_ifp_vrf_id(ctx);
		mtu = dplane_ctx_get_ifp_mtu(ctx);
		link_ifindex = dplane_ctx_get_ifp_link_ifindex(ctx);
		link_nsid = dplane_ctx_get_ifp_link_nsid(ctx);
		protodown_set = dplane_ctx_get_ifp_protodown_set(ctx);
		protodown = dplane_ctx_get_ifp_protodown(ctx);
		rc_bitfield = dplane_ctx_get_ifp_rc_bitfield(ctx);
		startup = dplane_ctx_get_ifp_startup(ctx);
		desc = dplane_ctx_get_ifp_desc(ctx);
		family = dplane_ctx_get_ifp_family(ctx);
		change_flags = dplane_ctx_get_ifp_change_flags(ctx);

#ifndef AF_BRIDGE
		/*
		 * Work around to make free bsd happy at the moment
		 */
		/*
		 * 在缺少 AF_BRIDGE 定义的系统（如 FreeBSD）上，临时定义它，
		 * 便于后续判断桥接接口。
		 */
#define AF_BRIDGE 7
#endif
		/*
		 * 如果该接口属于 AF_BRIDGE 地址族，交给桥接专用处理函数，
		 * 因为桥接接口的属性（端口、VLAN 等）与普通接口不同。
		 */
		if (family == AF_BRIDGE)
			return interface_bridge_handling(ctx, ifp, zif_type);

		/*
		 * 根据接口是否存在且是否已激活，进入不同处理路径：
		 *   1) 接口不存在或未激活 → 新增接口
		 *   2) 接口存在但 VRF 发生变化 → VRF 切换
		 *   3) 接口存在且 VRF 未变 → 普通更新
		 */
		if (ifp == NULL ||
		    !CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
			/*
			 * ============================
			 * 新增接口分支
			 * ============================
			 */
			/* Add interface notification from kernel */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("RTM_NEWLINK ADD for %s(%u) vrf_id %u type %d sl_type %d master %u flags 0x%llx",
					   name, ifindex, vrf_id, zif_type, zif_slave_type,
					   master_ifindex, (unsigned long long)flags);

			/* 上报 trace：新增接口 */
			frrtrace(8, frr_zebra, if_dplane_ifp_handling_new, name, ifindex, vrf_id,
				 zif_type, zif_slave_type, master_ifindex, flags, 0);

			if (ifp == NULL) {
				/* unknown interface */
				/* 接口在 zebra 中完全不存在，按名称创建 */
				ifp = if_get_by_name(name, vrf_id, NULL);
			} else {
				/* pre-configured interface, learnt now */
				/*
				 * 接口在 zebra 中已存在但尚未激活（例如预先配置），
				 * 现在从内核学到。如果 VRF 不一致，则更新到新 VRF。
				 */
				if (ifp->vrf->vrf_id != vrf_id)
					if_update_to_new_vrf(ifp, vrf_id);
			}

			/* 获取接口的 zebra 扩展信息 */
			zif = ifp->info;

			/* Update interface information. */
			/* 更新接口基本信息：ifindex、flags、MTU、metric、speed、txqlen 等 */
			set_ifindex(ifp, ifindex, zns);
			ifp->flags = flags;
			if_update_state_mtu(ifp, mtu);
			if_update_state_mtu6(ifp, mtu);
			if_update_state_metric(ifp, 0);
			if_update_state_speed(ifp, kernel_get_speed(ifp, NULL));
			ifp->ptm_status = ZEBRA_PTM_STATUS_UNKNOWN;
			ifp->txqlen = dplane_ctx_get_intf_txqlen(ctx);

			/* Set interface type */
			/* 设置接口类型和从接口类型 */
			zebra_if_set_ziftype(ifp, zif_type, zif_slave_type);

			/*
			 * 如果是 VRF 接口，标记为 VRF 环回接口。
			 */
			if (IS_ZEBRA_IF_VRF(ifp))
				SET_FLAG(ifp->status,
					 ZEBRA_INTERFACE_VRF_LOOPBACK);

			/* Update link. */
			/* 更新链路信息（link_ifindex、link_nsid） */
			zebra_if_update_link(ifp, link_ifindex, link_nsid);

			/* 更新底层类型和硬件地址 */
			ifp->ll_type = dplane_ctx_get_ifp_zltype(ctx);
			interface_update_hw_addr(ctx, ifp);

			/* Update interface type */
			/* 再次更新接口类型（历史遗留，冗余但无害） */
			ifp->zif_type = zif_type;

			/* Inform clients, install any configured addresses. */
			/*
			 * 通知客户端（如路由协议守护进程）新接口已添加，
			 * 并安装已配置的地址。
			 */
			if_add_update(ifp);

			/*
			 * Extract and save L2 interface information, take
			 * additional actions.
			 */
			/*
			 * 提取并保存 L2 接口信息（桥接/VxLAN 等），并执行相应动作。
			 * 第三个参数 1 表示这是新增接口。
			 */
			interface_update_l2info(ctx, ifp, zif_type, 1,
						link_nsid);

			/* 根据接口类型，更新 L2 子系统状态 */
			if (IS_ZEBRA_IF_BOND(ifp))
				zebra_l2if_update_bond(ifp, true);
			if (IS_ZEBRA_IF_BRIDGE_SLAVE(ifp))
				zebra_l2if_update_bridge_slave(
					ifp, bridge_ifindex, ns_id,
					ZEBRA_BRIDGE_NO_ACTION);
			else if (IS_ZEBRA_IF_BOND_SLAVE(ifp))
				zebra_l2if_update_bond_slave(ifp, bond_ifindex,
							     !!bypass);

			/*
			 * 如果设置了 protodown，则更新接口 protodown 状态。
			 * 启动阶段需要扫描并清除过期的 protodown 状态。
			 */
			if (protodown_set) {
				interface_if_protodown(ifp, protodown,
						       rc_bitfield);
				if (startup)
					if_sweep_protodown(zif);
			}

			/* 桥接接口调试日志：打印是否为 VLAN-aware */
			if (IS_ZEBRA_IF_BRIDGE(ifp)) {
				if (IS_ZEBRA_DEBUG_KERNEL)
					zlog_debug(
						"RTM_NEWLINK ADD for %s(%u), vlan-aware %d",
						name, ifp->ifindex,
						IS_ZEBRA_IF_BRIDGE_VLAN_AWARE(
							zif));
			}
			// if_update_state(ifp);
		} else if (ifp->vrf->vrf_id != vrf_id) {
			/*
			 * ============================
			 * VRF 变更分支
			 * ============================
			 */
			/* VRF change for an interface. */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"RTM_NEWLINK vrf-change for %s(%u) vrf_id %u -> %u",
					name, ifp->ifindex, ifp->vrf->vrf_id,
					vrf_id);

			/* 上报 trace：VRF 变更 */
			frrtrace(4, frr_zebra, if_dplane_ifp_handling_vrf_change, name,
				 ifp->ifindex, ifp->vrf->vrf_id, vrf_id);

			/* 处理接口的 VRF 切换（从旧 VRF 迁出，进入新 VRF） */
			if_handle_vrf_change(ifp, vrf_id);
		} else {
			/*
			 * ============================
			 * 普通接口更新分支
			 * ============================
			 */
			bool was_bridge_slave, was_bond_slave;
			uint8_t chgflags = ZEBRA_BRIDGE_NO_ACTION; /* 桥接变更标志 */

			zif = ifp->info;

			/* Interface update. */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("RTM_NEWLINK update for %s(%u) sl_type %d master %u flags 0x%llx",
					   name, ifp->ifindex, zif_slave_type, master_ifindex,
					   (unsigned long long)flags);

			/* 上报 trace：接口更新 */
			frrtrace(8, frr_zebra, if_dplane_ifp_handling_new, name, ifindex, vrf_id,
				 zif_type, zif_slave_type, master_ifindex, flags, 1);

			/*
			 * Interface promiscuity changes trigger spurious routing updates on HBN
			 * uplink interfaces, causing route flushes and traffic disruption.
			 *
			 * Check if only promiscuity flag changed (from netlink change mask)
			 */
			/*
			 * 接口的混杂模式（PROMISC）变化会在 HBN 上行接口上引发
			 * 虚假的路由更新，导致路由刷新和流量中断。
			 *
			 * 如果本次变更仅涉及 IFF_PROMISC，则只更新 flags，
			 * 不再触发路由通知，直接返回。
			 */
			if (change_flags == IFF_PROMISC) {
				ifp->flags = flags;
				/* Flags updated above, skip routing notifications */
				if (IS_ZEBRA_DEBUG_KERNEL)
					zlog_debug("%s: PROMISC-only update for %s(%u), no routing notification",
						   __func__, name, ifp->ifindex);
				return;
			}

			/* 更新接口基本属性：ifindex、MTU、metric、txqlen */
			set_ifindex(ifp, ifindex, zns);
			if_update_state_mtu(ifp, mtu);
			if_update_state_mtu6(ifp, mtu);
			if_update_state_metric(ifp, 0);
			ifp->txqlen = dplane_ctx_get_intf_txqlen(ctx);

			/*
			 * Update interface type - NOTE: Only slave_type can
			 * change.
			 */
			/*
			 * 更新接口类型。注意：对于已存在的接口，只有
			 * slave_type 可能发生变化，zif_type 不应改变。
			 *
			 * 记录旧状态（是否桥接从接口、是否 bond 从接口），
			 * 用于后续比较，决定是否需要通知 L2 子系统。
			 */
			was_bridge_slave = IS_ZEBRA_IF_BRIDGE_SLAVE(ifp);
			was_bond_slave = IS_ZEBRA_IF_BOND_SLAVE(ifp);
			zebra_if_set_ziftype(ifp, zif_type, zif_slave_type);

			/* 保存旧 MAC 地址，稍后用于判断 MAC 是否变化 */
			memcpy(old_hw_addr, ifp->hw_addr, INTERFACE_HWADDR_MAX);

			/* Update link. */
			/* 更新链路信息 */
			zebra_if_update_link(ifp, link_ifindex, link_nsid);

			/* 更新底层类型和硬件地址 */
			ifp->ll_type = dplane_ctx_get_ifp_zltype(ctx);
			interface_update_hw_addr(ctx, ifp);

			/* Update interface type */
			ifp->zif_type = zif_type;

			/* 如果有 protodown 设置，更新接口 protodown 状态 */
			if (protodown_set)
				interface_if_protodown(ifp, protodown,
						       rc_bitfield);

			/*
			 * 根据接口当前是否处于 PTM 可操作状态，分别处理：
			 *   - PTM 可操作：判断是 UP 还是 DOWN，必要时通知客户端
			 *   - 非 PTM 可操作：根据 flags 判断是否 UP/DOWN，并处理
			 */
			if (if_is_no_ptm_operative(ifp)) {
				ifp->flags = flags;
				bool is_up = if_is_operative(ifp);

				if (!if_is_no_ptm_operative(ifp) ||
				    CHECK_FLAG(zif->flags,
					       ZIF_FLAG_PROTODOWN)) {
					/*
					 * 接口变为 DOWN：
					 *   - 记录调试日志
					 *   - 调用 if_down() 通知客户端
					 *   - 触发路由更新
					 */
					if (IS_ZEBRA_DEBUG_KERNEL)
						zlog_debug(
							"Intf %s(%u) has gone DOWN",
							name, ifp->ifindex);
					frrtrace(3, frr_zebra, if_dplane_ifp_handling, name,
						 ifp->ifindex, 1);
					if_down(ifp);
					rib_update(RIB_UPDATE_KERNEL);
				} else if (if_is_operative(ifp)) {
					bool mac_updated = false;

					/*
					 * Must notify client daemons of new
					 * interface status.
					 */
					/*
					 * 接口处于 UP 状态，需要通知客户端。
					 */
					if (IS_ZEBRA_DEBUG_KERNEL)
						zlog_debug(
							"Intf %s(%u) PTM up, notifying clients",
							name, ifp->ifindex);
					frrtrace(3, frr_zebra, if_dplane_ifp_handling, name,
						 ifp->ifindex, 2);
					if_up(ifp, is_up);

					/*
					 * Update EVPN VNI when SVI MAC change
					 */
					/*
					 * 如果 MAC 地址发生变化，标记 mac_updated。
					 * 对于 VLAN SVI 接口或桥接接口，MAC 变化需要
					 * 额外通知 EVPN 或桥接子系统。
					 */
					if (memcmp(old_hw_addr, ifp->hw_addr,
						   INTERFACE_HWADDR_MAX))
						mac_updated = true;
					if (IS_ZEBRA_IF_VLAN(ifp) &&
					    mac_updated) {
						struct interface *link_if;

						/* 查找关联的链路接口，通知 VxLAN SVI 上线 */
						link_if = if_lookup_by_index_per_ns(
							zebra_ns_lookup(
								NS_DEFAULT),
							link_ifindex);
						if (link_if)
							zebra_vxlan_svi_up(
								ifp, link_if);
					} else if (mac_updated &&
						   IS_ZEBRA_IF_BRIDGE(ifp)) {
						/* 桥接接口 MAC 变化，设置桥接变更标志 */
						zlog_debug(
							"Intf %s(%u) bridge changed MAC address",
							name, ifp->ifindex);
						frrtrace(3, frr_zebra, if_dplane_ifp_handling,
							 name, ifp->ifindex, 3);
						chgflags =
							ZEBRA_BRIDGE_MASTER_MAC_CHANGE;
					}
				}
			} else {
				ifp->flags = flags;
				if (if_is_operative(ifp) &&
				    !CHECK_FLAG(zif->flags,
						ZIF_FLAG_PROTODOWN)) {
					/*
					 * 接口变为 UP：
					 *   - 记录调试日志
					 *   - 调用 if_up() 通知客户端
					 *   - 桥接接口设置 MASTER_UP 标志
					 */
					if (IS_ZEBRA_DEBUG_KERNEL)
						zlog_debug(
							"Intf %s(%u) has come UP",
							name, ifp->ifindex);
					frrtrace(3, frr_zebra, if_dplane_ifp_handling, name,
						 ifp->ifindex, 4);
					if_up(ifp, true);
					if (IS_ZEBRA_IF_BRIDGE(ifp))
						chgflags =
							ZEBRA_BRIDGE_MASTER_UP;
				} else {
					/*
					 * 接口变为 DOWN：
					 *   - 记录调试日志
					 *   - 调用 if_down() 通知客户端
					 *   - 触发路由更新
					 */
					if (IS_ZEBRA_DEBUG_KERNEL)
						zlog_debug(
							"Intf %s(%u) has gone DOWN",
							name, ifp->ifindex);
					frrtrace(3, frr_zebra, if_dplane_ifp_handling, name,
						 ifp->ifindex, 5);
					if_down(ifp);
					rib_update(RIB_UPDATE_KERNEL);
				}
			}

			/*
			 * Extract and save L2 interface information, take
			 * additional actions.
			 */
			/*
			 * 提取并保存 L2 接口信息。第三个参数 0 表示这是更新操作。
			 */
			interface_update_l2info(ctx, ifp, zif_type, 0,
						link_nsid);

			/* 根据接口类型，更新 L2 子系统状态 */
			if (IS_ZEBRA_IF_BRIDGE(ifp))
				zebra_l2if_update_bridge(ifp, chgflags);
			if (IS_ZEBRA_IF_BOND(ifp))
				zebra_l2if_update_bond(ifp, true);
			/*
			 * 桥接从接口：如果当前是从接口，或者之前是从接口，
			 * 都需要通知 L2 子系统，以处理主接口变更或从属关系解除。
			 */
			if (IS_ZEBRA_IF_BRIDGE_SLAVE(ifp) || was_bridge_slave)
				zebra_l2if_update_bridge_slave(
					ifp, bridge_ifindex, ns_id, chgflags);
			else if (IS_ZEBRA_IF_BOND_SLAVE(ifp) || was_bond_slave)
				zebra_l2if_update_bond_slave(ifp, bond_ifindex,
							     !!bypass);

			/* 桥接接口调试日志 */
			if (IS_ZEBRA_IF_BRIDGE(ifp)) {
				if (IS_ZEBRA_DEBUG_KERNEL)
					zlog_debug(
						"RTM_NEWLINK update for %s(%u), vlan-aware %d",
						name, ifp->ifindex,
						IS_ZEBRA_IF_BRIDGE_VLAN_AWARE(
							zif));
			}
		}

		/*
		 * 处理接口描述（description）：
		 * 释放旧的描述字符串，如果新描述非空则复制保存。
		 * 无论新增还是更新分支，最后都会执行。
		 */
		zif = ifp->info;
		if (zif) {
			XFREE(MTYPE_ZIF_DESC, zif->desc);
			if (desc[0])
				zif->desc = XSTRDUP(MTYPE_ZIF_DESC, desc);
		}
	}
}

/*
 * zebra_if_dplane_result：
 *   处理数据平面返回的接口相关操作结果。
 *
 *   该函数在 zebra 主线程上下文中被调用（由 rib_process_dplane_results()
 *   按操作码分发），负责根据不同的操作类型，将接口操作结果应用到 zebra
 *   内部的接口状态、地址、netconf 等数据结构中。
 *
 *   主要流程：
 *     1. 从上下文中提取命名空间 ID、操作类型、接口索引、结果状态。
 *     2. 打印调试日志（如开启 dplane/kernel 调试）。
 *     3. 根据 ns_id 查找对应的 zebra 命名空间 zns。
 *     4. 根据 ifindex 查找对应的接口 ifp。
 *     5. 按操作类型分发到具体的处理函数：
 *          - 接口地址新增/删除：zebra_if_addr_update_ctx()
 *          - 接口安装/更新/删除：可能触发 CTC 卸载与接口处理逻辑
 *          - 接口 netconf 变更：zebra_if_netconf_update_ctx()
 *
 * 参数：
 *   ctx  数据平面上下文，包含操作类型、ns_id、ifindex、结果状态等信息
 */
void zebra_if_dplane_result(struct zebra_dplane_ctx *ctx)
{
	struct zebra_ns *zns;              /* 对应的 zebra 命名空间 */
	struct interface *ifp;             /* 根据 ifindex 查找到的接口 */
	ns_id_t ns_id;                     /* 命名空间 ID */
	enum dplane_op_e op;               /* 数据平面操作类型 */
	enum zebra_dplane_result dp_res;   /* 数据平面请求结果状态 */
	ifindex_t ifindex;                 /* 接口索引 */

	/* 从上下文中提取命名空间 ID */
	ns_id = dplane_ctx_get_ns_id(ctx);

	/* 从上下文中提取请求结果状态（成功/失败/已排队等） */
	dp_res = dplane_ctx_get_status(ctx);

	/* 从上下文中提取操作类型（如 INTF_ADDR_ADD、INTF_INSTALL 等） */
	op = dplane_ctx_get_op(ctx);

	/* 从上下文中提取接口索引 */
	ifindex = dplane_ctx_get_ifindex(ctx);

	/* 调试日志：打印上下文地址、操作类型、接口索引、结果状态 */
	if (IS_ZEBRA_DEBUG_DPLANE_DETAIL || IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("Intf dplane ctx %p, op %s, ifindex (%u), result %s",
			   ctx, dplane_op2str(op), ifindex,
			   dplane_res2str(dp_res));

	/* 根据 ns_id 查找 zebra 命名空间 */
	zns = zebra_ns_lookup(ns_id);
	if (zns == NULL) {
		/* No ns - deleted maybe? */
		/*
		 * 找不到对应的命名空间，可能该命名空间已经被删除。
		 * 记录调试信息并直接返回。
		 */
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: can't find zns id %u", __func__, ns_id);

		return;
	}

	/* 在指定命名空间中，根据接口索引查找接口 */
	ifp = if_lookup_by_index_per_ns(zns, ifindex);

	/* 上报 trace，便于问题定位 */
	frrtrace(4, frr_zebra, if_dplane_result, op, dp_res, ns_id, ifp);

	/*
	 * 根据操作类型分发到具体的处理逻辑：
	 */
	if (op == DPLANE_OP_INTF_ADDR_ADD || op == DPLANE_OP_INTF_ADDR_DEL) {
		/*
		 * 接口地址新增/删除操作：
		 * 调用 zebra_if_addr_update_ctx() 将地址变更应用到接口上。
		 * 该函数会处理地址的添加、删除、更新等。
		 */
		zebra_if_addr_update_ctx(ctx, ifp);
	} else if (op == DPLANE_OP_INTF_INSTALL ||
		   op == DPLANE_OP_INTF_UPDATE || op == DPLANE_OP_INTF_DELETE) {
		/*
		 * Queued from the dplane means it is something
		 * that we need to handle( create/delete the
		 * interface as needed )
		 */
		/*
		 * 接口安装/更新/删除操作：
		 * 如果结果状态是 ZEBRA_DPLANE_REQUEST_QUEUED，
		 * 说明该上下文是从数据平面入队到 zebra 主线程处理的
		 * （即来自 inbound 路径），需要 zebra 主动创建/删除接口
		 * 或处理接口归属变化。
		 */
		if (dp_res == ZEBRA_DPLANE_REQUEST_QUEUED) {
			/* 转发给 CTC 卸载层：这些 ctx 走的是 inbound
			 * (dplane_provider_enqueue_to_zebra)，CTC provider 的
			 * process 回调收不到，无法自行处理 VRF 设备创建/删除
			 * 与接口 VRF 归属变化。必须在 ifp_handling() 之前调用：
			 * DELETE 时 ifp 与 zif_type 尚未被重置。 */
			/*
			 * 说明：
			 *   - 这些上下文来自数据平面的 inbound 路径
			 *     （dplane_provider_enqueue_to_zebra），
			 *     CTC（某个 provider 插件）的 process 回调接收不到，
			 *     所以无法自行处理 VRF 设备创建/删除以及接口 VRF 归属变化。
			 *   - 必须在 zebra_if_dplane_ifp_handling() 之前调用，
			 *     因为 DELETE 操作时，ifp 和 zif_type 尚未被重置，
			 *     CTC 需要用到这些信息。
			 */
			ctc_dplane_intf_event(ctx);

			/* 处理接口层面的实际操作（创建/删除接口等） */
			zebra_if_dplane_ifp_handling(ctx);
		} else
			/*
			 * 非 QUEUED 状态：说明是 zebra 主动下发的请求
			 * （outbound 路径）返回的结果，调用
			 * zebra_if_update_ctx() 更新接口状态。
			 */
			zebra_if_update_ctx(ctx, ifp);
	} else if (op == DPLANE_OP_INTF_NETCONFIG) {
		/*
		 * 接口 netconf 变更操作：
		 * 调用 zebra_if_netconf_update_ctx() 更新接口的 netconf 信息
		 * （例如 IPv4/IPv6 转发、rp_filter 等）。
		 */
		zebra_if_netconf_update_ctx(ctx, ifp, ifindex);
	}
}

/* Dump if address information to vty. */
/* 打印单个接口地址（connected）：前缀/广播地址/虚地址（secondary）/
 * label 别名等，JSON 与文本两分支 */
static void connected_dump_vty(struct vty *vty, json_object *json,
			       struct connected *connected)
{
	struct prefix *p;
	json_object *json_addr = NULL;

	/* Print interface address. */
	p = connected->address;

	if (json) {
		json_addr = json_object_new_object();
		json_object_array_add(json, json_addr);
		json_object_string_addf(json_addr, "address", "%pFX", p);
	} else {
		vty_out(vty, "  %s %pFX", prefix_family_str(p), p);
	}

	/* If there is destination address, print it. */
	if (CONNECTED_PEER(connected) && connected->destination) {
		if (json) {
			json_object_string_addf(json_addr, "peer", "%pFX",
						connected->destination);
		} else {
			vty_out(vty, " peer %pFX", connected->destination);
		}
	}

	if (json)
		json_object_boolean_add(
			json_addr, "secondary",
			CHECK_FLAG(connected->flags, ZEBRA_IFA_SECONDARY));
	else if (CHECK_FLAG(connected->flags, ZEBRA_IFA_SECONDARY))
		vty_out(vty, " secondary");

	if (json)
		json_object_boolean_add(json_addr, "noPrefixRoute",
					CHECK_FLAG(connected->flags, ZEBRA_IFA_NOPREFIXROUTE));
	else if (CHECK_FLAG(connected->flags, ZEBRA_IFA_NOPREFIXROUTE))
		vty_out(vty, " noprefixroute");

	if (json)
		json_object_boolean_add(
			json_addr, "unnumbered",
			CHECK_FLAG(connected->flags, ZEBRA_IFA_UNNUMBERED));
	else if (CHECK_FLAG(connected->flags, ZEBRA_IFA_UNNUMBERED))
		vty_out(vty, " unnumbered");

	if (connected->label) {
		if (json)
			json_object_string_add(json_addr, "label",
					       connected->label);
		else
			vty_out(vty, " %s", connected->label);
	}

	if (!json)
		vty_out(vty, "\n");
}

/* Dump interface neighbor address information to vty. */
/* 打印通过 IPv6 RA 学到的邻居地址（nbr_connected，RFC 5549 场景） */
static void nbr_connected_dump_vty(struct vty *vty, json_object *json,
				   struct nbr_connected *connected)
{
	struct prefix *p;
	char buf[PREFIX2STR_BUFFER];

	/* Print interface address. */
	p = connected->address;
	if (json)
		json_array_string_add(json, prefix2str(p, buf, sizeof(buf)));
	else
		vty_out(vty, "  %s %pFX\n", prefix_family_str(p), p);
}

/* zebra “从属接口”类型转字符串（slave 不存在时返回空串） */
static const char *
zebra_zifslavetype_2str(enum zebra_slave_iftype zif_slave_type)
{
	switch (zif_slave_type) {
	case ZEBRA_IF_SLAVE_BRIDGE:
		return "Bridge";
	case ZEBRA_IF_SLAVE_VRF:
		return "Vrf";
	case ZEBRA_IF_SLAVE_BOND:
		return "Bond";
	case ZEBRA_IF_SLAVE_OTHER:
		return "Other";
	case ZEBRA_IF_SLAVE_NONE:
		return "None";
	}
	return "None";
}

/* zebra 接口主类型转字符串（show 输出中的 "Type" 列） */
static const char *zebra_ziftype_2str(enum zebra_iftype zif_type)
{
	switch (zif_type) {
	case ZEBRA_IF_OTHER:
		return "Other";

	case ZEBRA_IF_BRIDGE:
		return "Bridge";

	case ZEBRA_IF_VLAN:
		return "Vlan";

	case ZEBRA_IF_VXLAN:
		return "Vxlan";

	case ZEBRA_IF_VRF:
		return "VRF";

	case ZEBRA_IF_VETH:
		return "VETH";

	case ZEBRA_IF_BOND:
		return "bond";

	case ZEBRA_IF_MACVLAN:
		return "macvlan";

	case ZEBRA_IF_GRE:
		return "GRE";

	case ZEBRA_IF_IP6GRE:
		return "IP6GRE";

	case ZEBRA_IF_GRETAP:
		return "GRETAP";

	case ZEBRA_IF_IP6GRETAP:
		return "IP6GRETAP";

	case ZEBRA_IF_DUMMY:
		return "dummy";

	default:
		return "Unknown";
	}
}

/* Interface's brief information print out to vty interface. */
/* 按 VRF 打印全部接口的摘要表（show interface [vrf ALL] 的非 JSON
 * 分支）：表头 + 逐口 Name/Index/Mtu/Flags/状态等一行式概要 */
static void ifs_dump_brief_vty(struct vty *vty, struct vrf *vrf)
{
	struct connected *connected;
	struct listnode *node;
	struct route_node *rn;
	struct zebra_if *zebra_if;
	struct prefix *p;
	struct interface *ifp;
	bool print_header = true;

	FOR_ALL_INTERFACES (vrf, ifp) {
		bool first_pfx_printed = false;

		/* 首个接口前打印一次表头 */
		if (print_header) {
			vty_out(vty, "%-16s%-8s%-16s%s\n", "Interface",
				"Status", "VRF", "Addresses");
			vty_out(vty, "%-16s%-8s%-16s%s\n", "---------",
				"------", "---", "---------");
			print_header = false; /* We have at least 1 iface */
		}
		zebra_if = ifp->info;

		/* 列：接口名 / up-down 状态 / VRF 名 */
		vty_out(vty, "%-16s", ifp->name);

		if (if_is_up(ifp))
			vty_out(vty, "%-8s", "up");
		else
			vty_out(vty, "%-8s", "down");

		vty_out(vty, "%-16s", vrf->name);

		/* v4：遍历子网树，每个子网只打印首个主地址；地址多于
		   一个时用“+ ”标记，后续行先补空对齐到地址列 */
		for (rn = route_top(zebra_if->ipv4_subnets); rn;
		     rn = route_next(rn)) {
			if (!rn->info)
				continue;
			uint32_t list_size = listcount((struct list *)rn->info);

			for (ALL_LIST_ELEMENTS_RO((struct list *)rn->info, node,
						  connected)) {
				if (!CHECK_FLAG(connected->flags,
						ZEBRA_IFA_SECONDARY)) {
					p = connected->address;
					if (first_pfx_printed) {
						/* padding to prepare row only
						 * for ip addr */
						vty_out(vty, "%-40s", "");
						if (list_size > 1)
							vty_out(vty, "+ ");
						vty_out(vty, "%pFX\n", p);
					} else {
						if (list_size > 1)
							vty_out(vty, "+ ");
						vty_out(vty, "%pFX\n", p);
					}
					first_pfx_printed = true;
					break;
				}
			}
		}

		uint32_t v6_list_size = 0;
		/* 先统计内核已存在的 v6 主地址个数（用于“+”标记） */
		frr_each (if_connected, ifp->connected, connected) {
			if (CHECK_FLAG(connected->conf, ZEBRA_IFC_REAL)
				&& (connected->address->family == AF_INET6))
				v6_list_size++;
		}
		/* v6：打印首个非 secondary 地址，对齐方式与 v4 一致 */
		frr_each (if_connected, ifp->connected, connected) {
			if (!CHECK_FLAG(connected->flags, ZEBRA_IFA_SECONDARY) &&
			    (connected->address->family == AF_INET6)) {
				p = connected->address;
				if (first_pfx_printed) {
					vty_out(vty, "%-40s", "");
					if (v6_list_size > 1)
						vty_out(vty, "+ ");
					vty_out(vty, "%pFX\n", p);
				} else {
					if (v6_list_size > 1)
						vty_out(vty, "+ ");
					vty_out(vty, "%pFX\n", p);
				}
				first_pfx_printed = true;
				break;
			}
		}
		/* 整个接口一个地址都没打：补换行保持行结构 */
		if (!first_pfx_printed)
			vty_out(vty, "\n");
	}
	vty_out(vty, "\n");
}

/* 同上概要表的 JSON 分支：每接口一个对象挂入 json_ifs 数组 */
static void ifs_dump_brief_vty_json(json_object *json, struct vrf *vrf)
{
	struct connected *connected;
	struct interface *ifp;

	FOR_ALL_INTERFACES (vrf, ifp) {
		json_object *json_if;
		json_object *json_addrs;

		json_if = json_object_new_object();
		json_object_object_add(json, ifp->name, json_if);

		json_object_string_add(json_if, "status",
				       if_is_up(ifp) ? "up" : "down");
		json_object_string_add(json_if, "vrfName", vrf->name);

		json_addrs = json_object_new_array();
		json_object_object_add(json_if, "addresses", json_addrs);
		frr_each (if_connected, ifp->connected, connected) {
			if (!CHECK_FLAG(connected->flags, ZEBRA_IFA_SECONDARY)) {
				char buf[PREFIX2STR_BUFFER];

				json_array_string_add(
					json_addrs,
					prefix2str(connected->address, buf,
						   sizeof(buf)));
			}
		}
	}
}

/* protodown 原因位图逐位展开为可读字符串（多位逗号分隔）：
 * 写入调用方提供的 pd_buf，返回其指针供日志/show 内嵌使用 */
const char *zebra_protodown_rc_str(uint32_t protodown_rc, char *pd_buf,
				   uint32_t pd_buf_len)
{
	pd_buf[0] = '\0';
	size_t len;

	strlcat(pd_buf, "(", pd_buf_len);

	/* 逐位检查已知 protodown 原因，存在则追加对应可读名字（带逗号） */
	if (CHECK_FLAG(protodown_rc, ZEBRA_PROTODOWN_EXTERNAL))
		strlcat(pd_buf, "external,", pd_buf_len);

	if (CHECK_FLAG(protodown_rc, ZEBRA_PROTODOWN_EVPN_STARTUP_DELAY))
		strlcat(pd_buf, "startup-delay,", pd_buf_len);

	if (CHECK_FLAG(protodown_rc, ZEBRA_PROTODOWN_EVPN_UPLINK_DOWN))
		strlcat(pd_buf, "uplinks-down,", pd_buf_len);

	if (CHECK_FLAG(protodown_rc, ZEBRA_PROTODOWN_VRRP))
		strlcat(pd_buf, "vrrp,", pd_buf_len);

	if (CHECK_FLAG(protodown_rc, ZEBRA_PROTODOWN_SHARP))
		strlcat(pd_buf, "sharp,", pd_buf_len);

	len = strnlen(pd_buf, pd_buf_len);

	/* Remove trailing comma */
	/* 去掉末尾多余逗号再补右括号 */
	if (pd_buf[len - 1] == ',')
		pd_buf[len - 1] = '\0';

	strlcat(pd_buf, ")", pd_buf_len);

	return pd_buf;
}

/* 本接口是否应展示 protodown 信息（VRF 设备/loopback 等不适用） */
static inline bool if_is_protodown_applicable(struct interface *ifp)
{
	if (IS_ZEBRA_IF_BOND(ifp))
		return false;

	return true;
}

/* 打印单个 VNI 条目（vni<->vlan 映射一行） */
static void zebra_vxlan_if_vni_dump_vty(struct vty *vty,
					struct zebra_vxlan_vni *vni)
{
	char str[INET6_ADDRSTRLEN];

	vty_out(vty, "\n  VxLAN Id %u", vni->vni);
	if (vni->access_vlan)
		vty_out(vty, " Access VLAN Id %u\n", vni->access_vlan);

	if (vni->mcast_grp.s_addr != INADDR_ANY)
		vty_out(vty, "  Mcast Group %s",
			inet_ntop(AF_INET, &vni->mcast_grp, str, sizeof(str)));
}

/* VNI 哈希表逐桶回调：打印每个 (vni, access vlan) 映射 */
static void zebra_vxlan_if_vni_hash_dump_vty(struct hash_bucket *bucket,
					     void *ctxt)
{
	struct vty *vty;
	struct zebra_vxlan_vni *vni;

	vni = (struct zebra_vxlan_vni *)bucket->data;
	vty = (struct vty *)ctxt;

	zebra_vxlan_if_vni_dump_vty(vty, vni);
}

/* 打印 vxlan 接口详情：封装源地址/VNI/接入 VLAN，SVD 模式额外
 * 输出内核维护的 VLAN-VNI 映射表 */
static void zebra_vxlan_if_dump_vty(struct vty *vty, struct zebra_if *zebra_if)
{
	struct zebra_l2info_vxlan *vxlan_info;
	struct zebra_vxlan_vni_info *vni_info;

	vxlan_info = &zebra_if->l2info.vxl;
	vni_info = &vxlan_info->vni_info;

	if (!ipaddr_is_zero(&vxlan_info->vtep_ip))
		vty_out(vty, "  VTEP IP: %pIA", &vxlan_info->vtep_ip);

	if (vxlan_info->ifindex_link && (vxlan_info->link_nsid != NS_UNKNOWN)) {
		struct interface *ifp;

		ifp = if_lookup_by_index_per_ns(
			zebra_ns_lookup(vxlan_info->link_nsid),
			vxlan_info->ifindex_link);
		vty_out(vty, " Link Interface %s",
			ifp == NULL ? "Unknown" : ifp->name);
	}

	if (IS_ZEBRA_VXLAN_IF_VNI(zebra_if)) {
		zebra_vxlan_if_vni_dump_vty(vty, &vni_info->vni);
	} else {
		hash_iterate(vni_info->vni_table,
			     zebra_vxlan_if_vni_hash_dump_vty, vty);
	}

	vty_out(vty, "\n");
}

/* Interface's information print out to vty interface. */
/* 打印单个接口的完整详情（show interface WORD 的非 JSON 分支）：
 * 地址/邻居地址、MTU/flags/bandwidth、上/下线统计、protodown 及原因、
 * 二层信息（bridge/vlan/vxlan/bond/EVPN ES）、link 父口、描述等 */
static void if_dump_vty(struct vty *vty, struct interface *ifp)
{
	struct connected *connected;
	struct nbr_connected *nbr_connected;
	struct listnode *node;
	struct route_node *rn;
	struct zebra_if *zebra_if;
	char pd_buf[ZEBRA_PROTODOWN_RC_STR_LEN];

	zebra_if = ifp->info;

	/* 首行：接口名 + up/down，line protocol 按是否启用检测分情况 */
	vty_out(vty, "Interface %s is ", ifp->name);
	if (if_is_up(ifp)) {
		vty_out(vty, "up, line protocol ");

		/* 启用 link 检测：看 running 位判协议 up/down；否则提示已禁用 */
		if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION)) {
			if (if_is_running(ifp))
				vty_out(vty, "is up\n");
			else
				vty_out(vty, "is down\n");
		} else {
			vty_out(vty, "detection is disabled\n");
		}
	} else {
		vty_out(vty, "down\n");
	}

	/* 链路 up/down 次数与最后时间、PTM 状态、所属 VRF */
	vty_out(vty, "  Link ups:   %5u    last: %s\n", zebra_if->up_count,
		zebra_if->up_last[0] ? zebra_if->up_last : "(never)");
	vty_out(vty, "  Link downs: %5u    last: %s\n", zebra_if->down_count,
		zebra_if->down_last[0] ? zebra_if->down_last : "(never)");

	zebra_ptm_show_status(vty, NULL, ifp);

	vty_out(vty, "  vrf: %s\n", ifp->vrf->name);

	if (ifp->desc)
		vty_out(vty, "  Description: %s\n", ifp->desc);
	if (zebra_if->desc)
		vty_out(vty, "  OS Description: %s\n", zebra_if->desc);

	if (ifp->ifindex == IFINDEX_INTERNAL) {
		/* 伪接口（无真实 ifindex）/未激活：只打到此处 */
		vty_out(vty, "  pseudo interface\n");
		return;
	} else if (!CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		vty_out(vty, "  index %d inactive interface\n", ifp->ifindex);
		return;
	}

	vty_out(vty, "  index %d metric %d mtu %d speed %u txqlen %u",
		ifp->ifindex, ifp->metric, ifp->mtu, ifp->speed, ifp->txqlen);
	/* v6 MTU 与 v4 不同时单独列出 */
	if (ifp->mtu6 != ifp->mtu)
		vty_out(vty, "mtu6 %d ", ifp->mtu6);
	vty_out(vty, "\n  flags: %s\n", if_flag_dump(ifp->flags));

	vty_out(vty, "  MPLS %s %s\n", zebra_if->mpls ? "enabled" : "",
		if_zebra_data_state(zebra_if->multicast));

	if (zebra_if->linkdown)
		vty_out(vty, "  Ignore all v4 routes with linkdown\n");
	if (zebra_if->linkdownv6)
		vty_out(vty, "  Ignore all v6 routes with linkdown\n");

	if (zebra_if->v4mcast_on)
		vty_out(vty, "  v4 Multicast forwarding is on\n");
	if (zebra_if->v6mcast_on)
		vty_out(vty, "  v6 Multicast forwarding is on\n");

	vty_out(vty, "  Multicast config is %s\n", if_zebra_data_state(zebra_if->multicast));

	vty_out(vty, "  Shutdown config is %s\n", if_zebra_data_state(zebra_if->shutdown));
	if (CHECK_FLAG(zebra_if->rtadv.ra_configured, BGP_RA_CONFIGURED))
		vty_out(vty, "  BGP has configured RA\n");

	/* Hardware address. */
	/* 硬件地址类型与 MAC（逐字节冒号分隔） */
	vty_out(vty, "  Type: %s\n", if_link_type_str(ifp->ll_type));
	if (ifp->hw_addr_len != 0) {
		int i;

		vty_out(vty, "  HWaddr: ");
		for (i = 0; i < ifp->hw_addr_len; i++)
			vty_out(vty, "%s%02x", i == 0 ? "" : ":",
				ifp->hw_addr[i]);
		vty_out(vty, "\n");
	}

	/* Bandwidth in Mbps */
	if (ifp->bandwidth != 0) {
		vty_out(vty, "  bandwidth %u Mbps", ifp->bandwidth);
		vty_out(vty, "\n");
	}

	/* 逐子网链打印全部 v4 地址 */
	for (rn = route_top(zebra_if->ipv4_subnets); rn; rn = route_next(rn)) {
		if (!rn->info)
			continue;

		for (ALL_LIST_ELEMENTS_RO((struct list *)rn->info, node,
					  connected))
			connected_dump_vty(vty, NULL, connected);
	}

	/* 再打印全部 v6 地址 */
	frr_each (if_connected, ifp->connected, connected) {
		if (connected->address->family == AF_INET6)
			connected_dump_vty(vty, NULL, connected);
	}

	vty_out(vty, "  Interface Type %s\n",
		zebra_ziftype_2str(zebra_if->zif_type));
	vty_out(vty, "  Interface Slave Type %s\n",
		zebra_zifslavetype_2str(zebra_if->zif_slave_type));

	/* 按接口类型输出各自二层详情：bridge/vlan/vxlan/gre */
	if (IS_ZEBRA_IF_BRIDGE(ifp)) {
		vty_out(vty, "  Bridge VLAN-aware: %s\n",
			IS_ZEBRA_IF_BRIDGE_VLAN_AWARE(zebra_if) ? "yes" : "no");
	} else if (IS_ZEBRA_IF_VLAN(ifp)) {
		struct zebra_l2info_vlan *vlan_info;

		vlan_info = &zebra_if->l2info.vl;
		vty_out(vty, "  VLAN Id %u\n", vlan_info->vid);
	} else if (IS_ZEBRA_IF_VXLAN(ifp)) {
		zebra_vxlan_if_dump_vty(vty, zebra_if);
	} else if (IS_ZEBRA_IF_GRE(ifp) || IS_ZEBRA_IF_GRETAP(ifp) || IS_ZEBRA_IF_IP6GRE(ifp) ||
		   IS_ZEBRA_IF_IP6GRETAP(ifp)) {
		struct zebra_l2info_gre *gre_info;

		gre_info = &zebra_if->l2info.gre;
		if (gre_info->vtep_ip.s_addr != INADDR_ANY) {
			vty_out(vty, "  VTEP IP: %pI4", &gre_info->vtep_ip);
			if (gre_info->vtep_ip_remote.s_addr != INADDR_ANY)
				vty_out(vty, " , remote %pI4",
					&gre_info->vtep_ip_remote);
			vty_out(vty, "\n");
		}
		if (gre_info->ifindex_link &&
		    (gre_info->link_nsid != NS_UNKNOWN)) {
			struct interface *nifp;

			nifp = if_lookup_by_index_per_ns(
				zebra_ns_lookup(gre_info->link_nsid),
				gre_info->ifindex_link);
			vty_out(vty, "  Link Interface %s\n",
				nifp == NULL ? "Unknown" :
				nifp->name);
		}
	}

	/* bridge/bond 从接口：展示其主接口（名字或仅 ifindex） */
	if (IS_ZEBRA_IF_BRIDGE_SLAVE(ifp)) {
		struct zebra_l2info_brslave *br_slave;

		br_slave = &zebra_if->brslave_info;
		if (br_slave->bridge_ifindex != IFINDEX_INTERNAL) {
			if (br_slave->br_if)
				vty_out(vty, "  Master interface: %s\n",
					br_slave->br_if->name);
			else
				vty_out(vty, "  Master ifindex: %u\n",
					br_slave->bridge_ifindex);
		}
	}

	if (IS_ZEBRA_IF_BOND_SLAVE(ifp)) {
		struct zebra_l2info_bondslave *bond_slave;

		bond_slave = &zebra_if->bondslave_info;
		if (bond_slave->bond_ifindex != IFINDEX_INTERNAL) {
			if (bond_slave->bond_if)
				vty_out(vty, "  Master interface: %s\n",
					bond_slave->bond_if->name);
			else
				vty_out(vty, "  Master ifindex: %u\n",
					bond_slave->bond_ifindex);
		}
	}

	if (zebra_if->flags & ZIF_FLAG_LACP_BYPASS)
		vty_out(vty, "  LACP bypass: on\n");

	/* EVPN ES 信息、protodown 状态及原因位图 */
	zebra_evpn_if_es_print(vty, NULL, zebra_if);
	vty_out(vty, "  protodown: %s %s\n",
		(ZEBRA_IF_IS_PROTODOWN(zebra_if)) ? "on" : "off",
		if_is_protodown_applicable(ifp) ? "" : "(n/a)");
	if (zebra_if->protodown_rc)
		vty_out(vty, "  protodown reasons: %s\n",
			zebra_protodown_rc_str(zebra_if->protodown_rc, pd_buf,
					       sizeof(pd_buf)));

	/* 父接链（子口/SVI 的下层设备）：已知名字则打印名字 */
	if (zebra_if->link_ifindex != IFINDEX_INTERNAL) {
		if (zebra_if->link)
			vty_out(vty, "  Parent interface: %s\n", zebra_if->link->name);
		else
			vty_out(vty, "  Parent ifindex: %d\n", zebra_if->link_ifindex);
	}

	/* 若启用 TE：逐项打印已设置的链路管理参数 */
	if (HAS_LINK_PARAMS(ifp)) {
		int i;
		struct if_link_params *iflp = ifp->link_params;
		vty_out(vty, "  Traffic Engineering Link Parameters:\n");
		if (IS_PARAM_SET(iflp, LP_TE_METRIC))
			vty_out(vty, "    TE metric %u\n", iflp->te_metric);
		if (IS_PARAM_SET(iflp, LP_MAX_BW))
			vty_out(vty, "    Maximum Bandwidth %g (Byte/s)\n",
				iflp->max_bw);
		if (IS_PARAM_SET(iflp, LP_MAX_RSV_BW))
			vty_out(vty,
				"    Maximum Reservable Bandwidth %g (Byte/s)\n",
				iflp->max_rsv_bw);
		if (IS_PARAM_SET(iflp, LP_UNRSV_BW)) {
			vty_out(vty,
				"    Unreserved Bandwidth per Class Type in Byte/s:\n");
			for (i = 0; i < MAX_CLASS_TYPE; i += 2)
				vty_out(vty,
					"      [%d]: %g (Bytes/sec),\t[%d]: %g (Bytes/sec)\n",
					i, iflp->unrsv_bw[i], i + 1,
					iflp->unrsv_bw[i + 1]);
		}

		if (IS_PARAM_SET(iflp, LP_ADM_GRP))
			vty_out(vty, "    Administrative Group:%u\n",
				iflp->admin_grp);
		if (IS_PARAM_SET(iflp, LP_DELAY)) {
			vty_out(vty, "    Link Delay Average: %u (micro-sec.)",
				iflp->av_delay);
			if (IS_PARAM_SET(iflp, LP_MM_DELAY)) {
				vty_out(vty, " Min:  %u (micro-sec.)",
					iflp->min_delay);
				vty_out(vty, " Max:  %u (micro-sec.)",
					iflp->max_delay);
			}
			vty_out(vty, "\n");
		}
		if (IS_PARAM_SET(iflp, LP_DELAY_VAR))
			vty_out(vty,
				"    Link Delay Variation %u (micro-sec.)\n",
				iflp->delay_var);
		if (IS_PARAM_SET(iflp, LP_PKT_LOSS))
			vty_out(vty, "    Link Packet Loss %f (in %%)\n",
				(double)iflp->pkt_loss * LOSS_PRECISION);
		if (IS_PARAM_SET(iflp, LP_AVA_BW))
			vty_out(vty, "    Available Bandwidth %g (Byte/s)\n",
				iflp->ava_bw);
		if (IS_PARAM_SET(iflp, LP_RES_BW))
			vty_out(vty, "    Residual Bandwidth %g (Byte/s)\n",
				iflp->res_bw);
		if (IS_PARAM_SET(iflp, LP_USE_BW))
			vty_out(vty, "    Utilized Bandwidth %g (Byte/s)\n",
				iflp->use_bw);
		if (IS_PARAM_SET(iflp, LP_RMT_AS))
			vty_out(vty, "    Neighbor ASBR IP: %pI4 AS: %u \n",
				&iflp->rmt_ip, iflp->rmt_as);
	}

	/* 额外信息钩子（供其他模块扩展），再列 RA 学到的邻居地址 */
	hook_call(zebra_if_extra_info, vty, NULL, ifp);

	if (listhead(ifp->nbr_connected))
		vty_out(vty, "  Neighbor address(s):\n");
	for (ALL_LIST_ELEMENTS_RO(ifp->nbr_connected, node, nbr_connected))
		nbr_connected_dump_vty(vty, NULL, nbr_connected);

#ifdef HAVE_PROC_NET_DEV
	/* Statistics print out using proc file system. */
	vty_out(vty,
		"    %lu input packets (%lu multicast), %lu bytes, %lu dropped\n",
		ifp->stats.rx_packets, ifp->stats.rx_multicast,
		ifp->stats.rx_bytes, ifp->stats.rx_dropped);

	vty_out(vty,
		"    %lu input errors, %lu length, %lu overrun, %lu CRC, %lu frame\n",
		ifp->stats.rx_errors, ifp->stats.rx_length_errors,
		ifp->stats.rx_over_errors, ifp->stats.rx_crc_errors,
		ifp->stats.rx_frame_errors);

	vty_out(vty, "    %lu fifo, %lu missed\n", ifp->stats.rx_fifo_errors,
		ifp->stats.rx_missed_errors);

	vty_out(vty, "    %lu output packets, %lu bytes, %lu dropped\n",
		ifp->stats.tx_packets, ifp->stats.tx_bytes,
		ifp->stats.tx_dropped);

	vty_out(vty,
		"    %lu output errors, %lu aborted, %lu carrier, %lu fifo, %lu heartbeat\n",
		ifp->stats.tx_errors, ifp->stats.tx_aborted_errors,
		ifp->stats.tx_carrier_errors, ifp->stats.tx_fifo_errors,
		ifp->stats.tx_heartbeat_errors);

	vty_out(vty, "    %lu window, %lu collisions\n",
		ifp->stats.tx_window_errors, ifp->stats.collisions);
#endif /* HAVE_PROC_NET_DEV */

#ifdef HAVE_NET_RT_IFLIST
	/* Statistics print out using sysctl (). */
	vty_out(vty,
		"    input packets %llu, bytes %llu, dropped %llu, multicast packets %llu\n",
		(unsigned long long)ifp->stats.ifi_ipackets,
		(unsigned long long)ifp->stats.ifi_ibytes,
		(unsigned long long)ifp->stats.ifi_iqdrops,
		(unsigned long long)ifp->stats.ifi_imcasts);

	vty_out(vty, "    input errors %llu\n",
		(unsigned long long)ifp->stats.ifi_ierrors);

	vty_out(vty,
		"    output packets %llu, bytes %llu, multicast packets %llu\n",
		(unsigned long long)ifp->stats.ifi_opackets,
		(unsigned long long)ifp->stats.ifi_obytes,
		(unsigned long long)ifp->stats.ifi_omcasts);

	vty_out(vty, "    output errors %llu\n",
		(unsigned long long)ifp->stats.ifi_oerrors);

	vty_out(vty, "    collisions %llu\n",
		(unsigned long long)ifp->stats.ifi_collisions);
#endif /* HAVE_NET_RT_IFLIST */
}

/*
 * Create hierarchical JSON structure for VNI information
 * This creates a nested object keyed by VNI ID to support both:
 * - TVD (Traditional VxLAN Device): single VLAN-to-VNI mapping
 * - SVD (Single VxLAN Device): multiple VLAN-to-VNI mappings
 * The VNI ID is used as the key, allowing multiple VNI entries
 * to coexist under the "vxlanId" parent object.
 */
/* JSON 分支：单 VNI 条目输出为对象字段 */
static void zebra_vxlan_if_vni_dump_vty_json(json_object *json_if,
					     struct zebra_vxlan_vni *vni)
{
	json_object *json_vni;
	char vni_str[VNI_STR_LEN];

	json_vni = json_object_new_object();
	snprintf(vni_str, sizeof(vni_str), "%u", vni->vni);
	if (vni->access_vlan)
		json_object_int_add(json_vni, "accessVlanId", vni->access_vlan);
	if (vni->mcast_grp.s_addr != INADDR_ANY)
		json_object_string_addf(json_vni, "mcastGroup", "%pI4", &vni->mcast_grp);
	json_object_object_add(json_if, vni_str, json_vni);
}

/* JSON 分支：VNI 哈希表逐桶回调（追加到 vni 数组） */
static void zebra_vxlan_if_vni_hash_dump_vty_json(struct hash_bucket *bucket,
						  void *ctxt)
{
	json_object *json_if;
	struct zebra_vxlan_vni *vni;

	vni = (struct zebra_vxlan_vni *)bucket->data;
	json_if = (json_object *)ctxt;

	zebra_vxlan_if_vni_dump_vty_json(json_if, vni);
}

/* JSON 分支：vxlan 接口详情（封装信息 + VNI 表） */
static void zebra_vxlan_if_dump_vty_json(json_object *json_if,
					 struct zebra_if *zebra_if)
{
	struct zebra_l2info_vxlan *vxlan_info;
	struct zebra_vxlan_vni_info *vni_info;
	json_object *json_vnis = NULL;

	vxlan_info = &zebra_if->l2info.vxl;
	vni_info = &vxlan_info->vni_info;

	if (!ipaddr_is_zero(&vxlan_info->vtep_ip))
		json_object_string_addf(json_if, "vtepIp", "%pIA", &vxlan_info->vtep_ip);

	if (vxlan_info->ifindex_link && (vxlan_info->link_nsid != NS_UNKNOWN)) {
		struct interface *ifp;

		ifp = if_lookup_by_index_per_ns(
			zebra_ns_lookup(vxlan_info->link_nsid),
			vxlan_info->ifindex_link);
		json_object_string_add(json_if, "linkInterface",
				       ifp == NULL ? "Unknown" : ifp->name);
	}

	json_vnis = json_object_new_object();
	if (IS_ZEBRA_VXLAN_IF_VNI(zebra_if))
		zebra_vxlan_if_vni_dump_vty_json(json_vnis, &vni_info->vni);
	else
		hash_iterate(vni_info->vni_table, zebra_vxlan_if_vni_hash_dump_vty_json, json_vnis);

	json_object_object_add(json_if, "vxlanId", json_vnis);
}

/* 单接口详情的 JSON 分支（与 if_dump_vty 同构，输出挂入 json_if）；
 * 末尾调用 zebra_if_extra_info 钩子让 EVPN/MLAG 等模块补充自有信息 */
static void if_dump_vty_json(struct vty *vty, struct interface *ifp,
			     json_object *json)
{
	struct connected *connected;
	struct nbr_connected *nbr_connected;
	struct listnode *node;
	struct route_node *rn;
	struct zebra_if *zebra_if;
	char pd_buf[ZEBRA_PROTODOWN_RC_STR_LEN];
	char buf[BUFSIZ];
	json_object *json_if;
	json_object *json_addrs;

	json_if = json_object_new_object();
	json_object_object_add(json, ifp->name, json_if);

	if (if_is_up(ifp)) {
		json_object_string_add(json_if, "administrativeStatus", "up");

		if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION)) {
			json_object_string_add(json_if, "operationalStatus",
					       if_is_running(ifp) ? "up"
								  : "down");
			json_object_boolean_add(json_if, "linkDetection", true);
		} else {
			json_object_boolean_add(json_if, "linkDetection",
						false);
		}
	} else {
		json_object_string_add(json_if, "administrativeStatus", "down");
	}

	zebra_if = ifp->info;

	json_object_int_add(json_if, "linkUps", zebra_if->up_count);
	json_object_int_add(json_if, "linkDowns", zebra_if->down_count);
	if (zebra_if->up_last[0])
		json_object_string_add(json_if, "lastLinkUp",
				       zebra_if->up_last);
	if (zebra_if->down_last[0])
		json_object_string_add(json_if, "lastLinkDown",
				       zebra_if->down_last);

	zebra_ptm_show_status(vty, json_if, ifp);

	json_object_string_add(json_if, "vrfName", ifp->vrf->name);

	if (ifp->desc)
		json_object_string_add(json_if, "description", ifp->desc);
	if (zebra_if->desc)
		json_object_string_add(json_if, "OsDescription",
				       zebra_if->desc);

	json_object_boolean_add(json_if, "mplsEnabled", zebra_if->mpls);
	json_object_boolean_add(json_if, "linkDown", zebra_if->linkdown);
	json_object_boolean_add(json_if, "linkDownV6", zebra_if->linkdownv6);
	json_object_boolean_add(json_if, "mcForwardingV4", zebra_if->v4mcast_on);
	json_object_boolean_add(json_if, "mcForwardingV6", zebra_if->v6mcast_on);
	json_object_boolean_add(json_if, "bgpRAConfigured",
				CHECK_FLAG(zebra_if->rtadv.ra_configured, BGP_RA_CONFIGURED));

	json_object_string_add(json_if, "multicastConfig", if_zebra_data_state(zebra_if->multicast));

	json_object_string_add(json_if, "shutdownConfig", if_zebra_data_state(zebra_if->shutdown));

	json_object_string_add(json_if, "mplsConfig", if_zebra_data_state(zebra_if->mpls_config));
	json_object_int_add(json_if, "speedChecked", zebra_if->speed_checked);


	if (ifp->ifindex == IFINDEX_INTERNAL) {
		json_object_boolean_add(json_if, "pseudoInterface", true);
		return;
	} else if (!CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		json_object_int_add(json_if, "index", ifp->ifindex);
		return;
	}

	json_object_boolean_add(json_if, "pseudoInterface", false);
	json_object_int_add(json_if, "index", ifp->ifindex);
	json_object_int_add(json_if, "metric", ifp->metric);
	json_object_int_add(json_if, "mtu", ifp->mtu);
	if (ifp->mtu6 != ifp->mtu)
		json_object_int_add(json_if, "mtu6", ifp->mtu6);
	json_object_int_add(json_if, "speed", ifp->speed);
	json_object_int_add(json_if, "txqlen", ifp->txqlen);
	json_object_string_add(json_if, "flags", if_flag_dump(ifp->flags));

	/* Hardware address. */
	json_object_string_add(json_if, "type", if_link_type_str(ifp->ll_type));
	if (ifp->hw_addr_len != 0) {
		char hwbuf[BUFSIZ];

		hwbuf[0] = '\0';
		for (int i = 0; i < ifp->hw_addr_len; i++) {
			snprintf(buf, sizeof(buf), "%s%02x", i == 0 ? "" : ":",
				 ifp->hw_addr[i]);
			strlcat(hwbuf, buf, sizeof(hwbuf));
		}
		json_object_string_add(json_if, "hardwareAddress", hwbuf);
	}

	/* Bandwidth in Mbps */
	if (ifp->bandwidth != 0)
		json_object_int_add(json_if, "bandwidth", ifp->bandwidth);


	/* IP addresses. */
	json_addrs = json_object_new_array();
	json_object_object_add(json_if, "ipAddresses", json_addrs);

	for (rn = route_top(zebra_if->ipv4_subnets); rn; rn = route_next(rn)) {
		if (!rn->info)
			continue;

		for (ALL_LIST_ELEMENTS_RO((struct list *)rn->info, node,
					  connected))
			connected_dump_vty(vty, json_addrs, connected);
	}

	frr_each (if_connected, ifp->connected, connected) {
		if (connected->address->family == AF_INET6)
			connected_dump_vty(vty, json_addrs, connected);
	}

	json_object_string_add(json_if, "interfaceType",
			       zebra_ziftype_2str(zebra_if->zif_type));
	json_object_string_add(
		json_if, "interfaceSlaveType",
		zebra_zifslavetype_2str(zebra_if->zif_slave_type));

	if (IS_ZEBRA_IF_BRIDGE(ifp)) {
		struct zebra_l2info_bridge *bridge_info;

		bridge_info = &zebra_if->l2info.br;
		json_object_boolean_add(json_if, "bridgeVlanAware",
					bridge_info->bridge.vlan_aware);
	} else if (IS_ZEBRA_IF_VLAN(ifp)) {
		struct zebra_l2info_vlan *vlan_info;

		vlan_info = &zebra_if->l2info.vl;
		json_object_int_add(json_if, "vlanId", vlan_info->vid);
	} else if (IS_ZEBRA_IF_VXLAN(ifp)) {
		zebra_vxlan_if_dump_vty_json(json_if, zebra_if);

	} else if (IS_ZEBRA_IF_GRE(ifp) || IS_ZEBRA_IF_GRETAP(ifp) || IS_ZEBRA_IF_IP6GRE(ifp) ||
		   IS_ZEBRA_IF_IP6GRETAP(ifp)) {
		struct zebra_l2info_gre *gre_info;

		gre_info = &zebra_if->l2info.gre;
		if (gre_info->vtep_ip.s_addr != INADDR_ANY) {
			json_object_string_addf(json_if, "vtepIp", "%pI4",
						&gre_info->vtep_ip);
			if (gre_info->vtep_ip_remote.s_addr != INADDR_ANY)
				json_object_string_addf(
					json_if, "vtepRemoteIp", "%pI4",
					&gre_info->vtep_ip_remote);
		}
		if (gre_info->ifindex_link
		    && (gre_info->link_nsid != NS_UNKNOWN)) {
			struct interface *nifp;

			nifp = if_lookup_by_index_per_ns(
				zebra_ns_lookup(gre_info->link_nsid),
				gre_info->ifindex_link);
			json_object_string_add(json_if, "linkInterface",
					       nifp == NULL ? "Unknown" : nifp->name);
		}
	}

	if (IS_ZEBRA_IF_BRIDGE_SLAVE(ifp)) {
		struct zebra_l2info_brslave *br_slave;

		br_slave = &zebra_if->brslave_info;
		if (br_slave->bridge_ifindex != IFINDEX_INTERNAL) {
			if (br_slave->br_if)
				json_object_string_add(json_if,
						       "masterInterface",
						       br_slave->br_if->name);
			else
				json_object_int_add(json_if, "masterIfindex",
						    br_slave->bridge_ifindex);
		}
	}

	if (IS_ZEBRA_IF_BOND_SLAVE(ifp)) {
		struct zebra_l2info_bondslave *bond_slave;

		bond_slave = &zebra_if->bondslave_info;
		if (bond_slave->bond_ifindex != IFINDEX_INTERNAL) {
			if (bond_slave->bond_if)
				json_object_string_add(
					json_if, "masterInterface",
					bond_slave->bond_if->name);
			else
				json_object_int_add(json_if, "masterIfindex",
						    bond_slave->bond_ifindex);
		}
	}

	json_object_boolean_add(
		json_if, "lacpBypass",
		CHECK_FLAG(zebra_if->flags, ZIF_FLAG_LACP_BYPASS));

	zebra_evpn_if_es_print(vty, json_if, zebra_if);

	if (if_is_protodown_applicable(ifp)) {
		json_object_string_add(
			json_if, "protodown",
			(ZEBRA_IF_IS_PROTODOWN(zebra_if)) ? "on" : "off");
		if (zebra_if->protodown_rc)
			json_object_string_add(
				json_if, "protodownReason",
				zebra_protodown_rc_str(zebra_if->protodown_rc,
						       pd_buf, sizeof(pd_buf)));
	}

	if (zebra_if->link_ifindex != IFINDEX_INTERNAL) {
		if (zebra_if->link)
			json_object_string_add(json_if, "parentInterface",
					       zebra_if->link->name);
		else
			json_object_int_add(json_if, "parentIfindex",
					    zebra_if->link_ifindex);
	}

	if (HAS_LINK_PARAMS(ifp)) {
		struct if_link_params *iflp = ifp->link_params;
		json_object *json_te;

		json_te = json_object_new_object();
		json_object_object_add(
			json_if, "trafficEngineeringLinkParameters", json_te);

		if (IS_PARAM_SET(iflp, LP_TE_METRIC))
			json_object_int_add(json_te, "teMetric",
					    iflp->te_metric);
		if (IS_PARAM_SET(iflp, LP_MAX_BW))
			json_object_double_add(json_te, "maximumBandwidth",
					       iflp->max_bw);
		if (IS_PARAM_SET(iflp, LP_MAX_RSV_BW))
			json_object_double_add(json_te,
					       "maximumReservableBandwidth",
					       iflp->max_rsv_bw);
		if (IS_PARAM_SET(iflp, LP_UNRSV_BW)) {
			json_object *json_bws;

			json_bws = json_object_new_object();
			json_object_object_add(json_te, "unreservedBandwidth",
					       json_bws);
			for (unsigned int i = 0; i < MAX_CLASS_TYPE; ++i) {
				char buf_ct[64];

				snprintf(buf_ct, sizeof(buf_ct), "classType%u",
					 i);
				json_object_double_add(json_bws, buf_ct,
						       iflp->unrsv_bw[i]);
			}
		}

		if (IS_PARAM_SET(iflp, LP_ADM_GRP))
			json_object_int_add(json_te, "administrativeGroup",
					    iflp->admin_grp);
		if (IS_PARAM_SET(iflp, LP_DELAY)) {
			json_object_int_add(json_te, "linkDelayAverage",
					    iflp->av_delay);
			if (IS_PARAM_SET(iflp, LP_MM_DELAY)) {
				json_object_int_add(json_te, "linkDelayMinimum",
						    iflp->min_delay);
				json_object_int_add(json_te, "linkDelayMaximum",
						    iflp->max_delay);
			}
		}
		if (IS_PARAM_SET(iflp, LP_DELAY_VAR))
			json_object_int_add(json_te, "linkDelayVariation",
					    iflp->delay_var);
		if (IS_PARAM_SET(iflp, LP_PKT_LOSS))
			json_object_double_add(json_te, "linkPacketLoss",
					       (double)iflp->pkt_loss *
						       LOSS_PRECISION);
		if (IS_PARAM_SET(iflp, LP_AVA_BW))
			json_object_double_add(json_te, "availableBandwidth",
					       iflp->ava_bw);
		if (IS_PARAM_SET(iflp, LP_RES_BW))
			json_object_double_add(json_te, "residualBandwidth",
					       iflp->res_bw);
		if (IS_PARAM_SET(iflp, LP_USE_BW))
			json_object_double_add(json_te, "utilizedBandwidth",
					       iflp->use_bw);
		if (IS_PARAM_SET(iflp, LP_RMT_AS))
			json_object_string_addf(json_te, "neighborAsbrIp",
						"%pI4", &iflp->rmt_ip);
		json_object_int_add(json_te, "neighborAsbrAs", iflp->rmt_as);
	}

	hook_call(zebra_if_extra_info, vty, json_if, ifp);

	if (listhead(ifp->nbr_connected)) {
		json_object *json_nbr_addrs;

		json_nbr_addrs = json_object_new_array();
		json_object_object_add(json_if, "neighborIpAddresses",
				       json_nbr_addrs);

		for (ALL_LIST_ELEMENTS_RO(ifp->nbr_connected, node,
					  nbr_connected))
			nbr_connected_dump_vty(vty, json_nbr_addrs,
					       nbr_connected);
	}

#ifdef HAVE_PROC_NET_DEV
	json_object_int_add(json_if, "inputPackets", stats.rx_packets);
	json_object_int_add(json_if, "inputBytes", ifp->stats.rx_bytes);
	json_object_int_add(json_if, "inputDropped", ifp->stats.rx_dropped);
	json_object_int_add(json_if, "inputMulticastPackets",
			    ifp->stats.rx_multicast);
	json_object_int_add(json_if, "inputErrors", ifp->stats.rx_errors);
	json_object_int_add(json_if, "inputLengthErrors",
			    ifp->stats.rx_length_errors);
	json_object_int_add(json_if, "inputOverrunErrors",
			    ifp->stats.rx_over_errors);
	json_object_int_add(json_if, "inputCrcErrors",
			    ifp->stats.rx_crc_errors);
	json_object_int_add(json_if, "inputFrameErrors",
			    ifp->stats.rx_frame_errors);
	json_object_int_add(json_if, "inputFifoErrors",
			    ifp->stats.rx_fifo_errors);
	json_object_int_add(json_if, "inputMissedErrors",
			    ifp->stats.rx_missed_errors);
	json_object_int_add(json_if, "outputPackets", ifp->stats.tx_packets);
	json_object_int_add(json_if, "outputBytes", ifp->stats.tx_bytes);
	json_object_int_add(json_if, "outputDroppedPackets",
			    ifp->stats.tx_dropped);
	json_object_int_add(json_if, "outputErrors", ifp->stats.tx_errors);
	json_object_int_add(json_if, "outputAbortedErrors",
			    ifp->stats.tx_aborted_errors);
	json_object_int_add(json_if, "outputCarrierErrors",
			    ifp->stats.tx_carrier_errors);
	json_object_int_add(json_if, "outputFifoErrors",
			    ifp->stats.tx_fifo_errors);
	json_object_int_add(json_if, "outputHeartbeatErrors",
			    ifp->stats.tx_heartbeat_errors);
	json_object_int_add(json_if, "outputWindowErrors",
			    ifp->stats.tx_window_errors);
	json_object_int_add(json_if, "collisions", ifp->stats.collisions);
#endif /* HAVE_PROC_NET_DEV */

#ifdef HAVE_NET_RT_IFLIST
	json_object_int_add(json_if, "inputPackets", ifp->stats.ifi_ipackets);
	json_object_int_add(json_if, "inputBytes", ifp->stats.ifi_ibytes);
	json_object_int_add(json_if, "inputDropd", ifp->stats.ifi_iqdrops);
	json_object_int_add(json_if, "inputMulticastPackets",
			    ifp->stats.ifi_imcasts);
	json_object_int_add(json_if, "inputErrors", ifp->stats.ifi_ierrors);
	json_object_int_add(json_if, "outputPackets", ifp->stats.ifi_opackets);
	json_object_int_add(json_if, "outputBytes", ifp->stats.ifi_obytes);
	json_object_int_add(json_if, "outputMulticastPackets",
			    ifp->stats.ifi_omcasts);
	json_object_int_add(json_if, "outputErrors", ifp->stats.ifi_oerrors);
	json_object_int_add(json_if, "collisions", ifp->stats.ifi_collisions);
#endif /* HAVE_NET_RT_IFLIST */
}

/* 周期刷新全局接口统计缓存（if_stat_refresh，供 bandwidth/speed
 * 展示与路由 metric 自动计算） */
static void interface_update_stats(void)
{
#ifdef HAVE_PROC_NET_DEV
	/* If system has interface statistics via proc file system, update
	   statistics. */
	ifstat_update_proc();
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
	ifstat_update_sysctl();
#endif /* HAVE_NET_RT_IFLIST */
}

#include "zebra/interface_clippy.c"
/* Show all interfaces to vty. */
/* `show interface vrf NAME [brief] [json]`：指定 VRF 内全部接口详情
 * 或概要（brief），VRF 不存在时按文本/JSON 分别报缺失 */
DEFPY(show_interface, show_interface_cmd,
      "show interface vrf NAME$vrf_name [brief$brief] [json$uj]",
      SHOW_STR
      "Interface status and configuration\n"
      VRF_CMD_HELP_STR
      "Interface status and configuration summary\n"
      JSON_STR)
{
	struct vrf *vrf;
	struct interface *ifp;
	json_object *json = NULL;

	/* 先刷新全局统计缓存，再按名定位 VRF */
	interface_update_stats();

	vrf = vrf_lookup_by_name(vrf_name);
	/* VRF 不存在：json 输出空对象，文本报 not found */
	if (!vrf) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "%% VRF %s not found\n", vrf_name);
		return CMD_WARNING;
	}

	if (uj)
		json = json_object_new_object();

	if (brief) {
		if (json)
			ifs_dump_brief_vty_json(json, vrf);
		else
			ifs_dump_brief_vty(vty, vrf);
	} else {
		FOR_ALL_INTERFACES (vrf, ifp) {
			if (json)
				if_dump_vty_json(vty, ifp, json);
			else
				if_dump_vty(vty, ifp);
		}
	}

	/* 收集到 json 对象则统一经 vty_json 输出 */
	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}


/* Show all interfaces to vty. */
/* `show interface vrf all [brief] [json]`：遍历所有 VRF 逐个输出
 * 接口详情/概要 */
DEFPY (show_interface_vrf_all,
       show_interface_vrf_all_cmd,
       "show interface [vrf all] [brief$brief] [json$uj]",
       SHOW_STR
       "Interface status and configuration\n"
       VRF_ALL_CMD_HELP_STR
       "Interface status and configuration summary\n"
       JSON_STR)
{
	struct vrf *vrf;
	struct interface *ifp;
	json_object *json = NULL;

	interface_update_stats();

	if (uj)
		json = json_object_new_object();

	/* All interface print. */
	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		if (brief) {
			if (json)
				ifs_dump_brief_vty_json(json, vrf);
			else
				ifs_dump_brief_vty(vty, vrf);
		} else {
			FOR_ALL_INTERFACES (vrf, ifp) {
				if (json)
					if_dump_vty_json(vty, ifp, json);
				else
					if_dump_vty(vty, ifp);
			}
		}
	}

	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* Show specified interface to vty. */

/* `show interface IFNAME vrf NAME [json]`：指定 VRF 内按名查单个
 * 接口并输出详情 */
DEFPY (show_interface_name_vrf,
       show_interface_name_vrf_cmd,
       "show interface IFNAME$ifname vrf NAME$vrf_name [json$uj]",
       SHOW_STR
       "Interface status and configuration\n"
       "Interface name\n"
       VRF_CMD_HELP_STR
       JSON_STR)
{
	struct interface *ifp;
	struct vrf *vrf;
	json_object *json = NULL;

	interface_update_stats();

	vrf = vrf_lookup_by_name(vrf_name);
	if (!vrf) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "%% VRF %s not found\n", vrf_name);
		return CMD_WARNING;
	}

	ifp = if_lookup_by_name_vrf(ifname, vrf);
	if (ifp == NULL) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "%% Can't find interface %s\n", ifname);
		return CMD_WARNING;
	}

	if (uj)
		json = json_object_new_object();

	if (json)
		if_dump_vty_json(vty, ifp, json);
	else
		if_dump_vty(vty, ifp);

	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* Show specified interface to vty. */
/* `show interface IFNAME [vrf all] [json]`：跨 VRF 按名查接口；
 * vrf-lite 后端命中唯一即止，netns 后端可能同名多份——多于一个
 * 时报错要求指定 VRF 名 */
DEFPY (show_interface_name_vrf_all,
       show_interface_name_vrf_all_cmd,
       "show interface IFNAME$ifname [vrf all] [json$uj]",
       SHOW_STR
       "Interface status and configuration\n"
       "Interface name\n"
       VRF_ALL_CMD_HELP_STR
       JSON_STR)
{
	struct interface *ifp = NULL;
	struct interface *ifptmp;
	struct vrf *vrf;
	json_object *json = NULL;
	int count = 0;

	interface_update_stats();

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		/* 逐 VRF 按名查；vrf-lite 后端命中唯一即止，
		 * netns 后端可能同名多份，故继续累加 count */
		ifptmp = if_lookup_by_name_vrf(ifname, vrf);
		if (ifptmp) {
			ifp = ifptmp;
			count++;
			if (!vrf_is_backend_netns())
				break;
		}
	}

	/* 一个都没找到：报 not found */
	if (ifp == NULL) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "%% Can't find interface %s\n", ifname);
		return CMD_WARNING;
	}
	/* 跨 netns 命中多个同名接口：歧义，要求指定 VRF 名 */
	if (count > 1) {
		if (uj) {
			vty_out(vty, "{}\n");
		} else {
			vty_out(vty,
				"%% There are multiple interfaces with name %s\n",
				ifname);
			vty_out(vty, "%% You must specify the VRF name\n");
		}
		return CMD_WARNING;
	}

	if (uj)
		json = json_object_new_object();

	if (json)
		if_dump_vty_json(vty, ifp, json);
	else
		if_dump_vty(vty, ifp);

	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 接口描述一览表：接口名/管理状态(Status)/协议态(Protocol，依赖
 * link-detect 配置)/描述；zebra 侧描述(zif->desc，内核 ifp->desc 之外)
 * 另起一行对齐展示 */
static void if_show_description(struct vty *vty, struct vrf *vrf)
{
	struct interface *ifp;

	vty_out(vty, "Interface       Status  Protocol  Description\n");
	FOR_ALL_INTERFACES (vrf, ifp) {
		int len;
		struct zebra_if *zif;
		bool intf_desc;

		intf_desc = false;

		/* 列 1：接口名，按 16 列对齐补空 */
		len = vty_out(vty, "%s", ifp->name);
		vty_out(vty, "%*s", (16 - len), " ");

		/* 列 2/3：管理态与协议态；协议态依赖 link-detect，
		   未启用检测时无法得知协议态给 unknown */
		if (if_is_up(ifp)) {
			vty_out(vty, "up      ");
			if (CHECK_FLAG(ifp->status,
				       ZEBRA_INTERFACE_LINKDETECTION)) {
				if (if_is_running(ifp))
					vty_out(vty, "up        ");
				else
					vty_out(vty, "down      ");
			} else {
				vty_out(vty, "unknown   ");
			}
		} else {
			vty_out(vty, "down    down      ");
		}

		/* 先打用户配置的 ifp->desc；若有 zebra 侧内核描述
		   zif->desc 则另起一行对齐补打 */
		if (ifp->desc) {
			intf_desc = true;
			vty_out(vty, "%s", ifp->desc);
		}
		zif = ifp->info;
		if (zif && zif->desc) {
			vty_out(vty, "%s%s",
				intf_desc
					? "\n                                  "
					: "",
				zif->desc);
		}

		vty_out(vty, "\n");
	}
}

/* `show interface description vrf NAME`：指定 VRF 的描述一览表 */
DEFUN (show_interface_desc,
       show_interface_desc_cmd,
       "show interface description vrf NAME",
       SHOW_STR
       "Interface status and configuration\n"
       "Interface description\n"
       VRF_CMD_HELP_STR)
{
	struct vrf *vrf;

	vrf = vrf_lookup_by_name(argv[4]->arg);
	if (!vrf) {
		vty_out(vty, "%% VRF %s not found\n", argv[4]->arg);
		return CMD_WARNING;
	}

	if_show_description(vty, vrf);

	return CMD_SUCCESS;
}


/* `show interface description [vrf all]`：所有非空 VRF 的描述一览表 */
DEFUN (show_interface_desc_vrf_all,
       show_interface_desc_vrf_all_cmd,
       "show interface description [vrf all]",
       SHOW_STR
       "Interface status and configuration\n"
       "Interface description\n"
       VRF_ALL_CMD_HELP_STR)
{
	struct vrf *vrf;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name)
		if (!RB_EMPTY(if_name_head, &vrf->ifaces_by_name)) {
			vty_out(vty, "\n\tVRF %s(%u)\n\n", VRF_LOGNAME(vrf),
				vrf->vrf_id);
			if_show_description(vty, vrf);
		}

	return CMD_SUCCESS;
}

/* ARP 开关：接口未激活时忽略；enable=清 IFF_NOARP，disable=置
 * IFF_NOARP（影响内核地址解析行为，仅记日志不回滚配置） */
void if_arp(struct interface *ifp, bool enable)
{
	int ret;

	if (!CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE))
		return;

	if (enable)
		ret = if_unset_flags(ifp, IFF_NOARP);
	else
		ret = if_set_flags(ifp, IFF_NOARP);

	if (ret < 0) {
		zlog_debug("Can't %sset noarp flag on interface %s",
			   enable ? "" : "un", ifp->name);
		return;
	}

	if_refresh(ifp);
}

/* 开启接口组播：激活时置 IFF_MULTICAST 并刷新；无论是否激活都记
 * 配置 ON（接口下次激活时由 if_add_update 重新应用） */
int if_multicast_set(struct interface *ifp)
{
	struct zebra_if *if_data;

	/* 仅当接口已激活才操作内核 flags */
	if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		if (if_set_flags(ifp, IFF_MULTICAST) < 0) {
			zlog_debug("Can't set multicast flag on interface %s",
				   ifp->name);
			return -1;
		}
		if_refresh(ifp);
	}
	/* 无论当前是否激活都记配置 ON，下次激活时重新应用 */
	if_data = ifp->info;
	if_data->multicast = IF_ZEBRA_DATA_ON;

	return 0;
}

/* 关闭接口组播：激活时清 IFF_MULTICAST 并刷新；配置记 OFF */
int if_multicast_unset(struct interface *ifp)
{
	struct zebra_if *if_data;

	/* 激活时清 IFF_MULTICAST；失败仅记错返回 */
	if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		if (if_unset_flags(ifp, IFF_MULTICAST) < 0) {
			zlog_debug("Can't unset multicast flag on interface %s",
				   ifp->name);
			return -1;
		}
		if_refresh(ifp);
	}
	/* 记配置 OFF */
	if_data = ifp->info;
	if_data->multicast = IF_ZEBRA_DATA_OFF;

	return 0;
}

/* link-detect 开关：启用后接口 operative 取决于内核载波（IFF_RUNNING），
 * 当前实际已无载波则立即 if_down；禁用后视接口恒可用，可能直接
 * if_up。FIXME：不改 up 状态时事件可能延迟上报 */
void if_linkdetect(struct interface *ifp, bool detect)
{
	int if_was_operative;

	/* 先记住切换前的 operative 状态，用于判断是否需触发事件 */
	if_was_operative = if_is_no_ptm_operative(ifp);
	if (detect) {
		/* 启用检测：operative 需再看载波，因此可能转 down */
		SET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);

		/* When linkdetection is enabled, if might come down */
		/* 原本可用、启用后已无载波：立即 if_down */
		if (!if_is_no_ptm_operative(ifp) && if_was_operative)
			if_down(ifp);
	} else {
		/* 禁用检测：视接口恒可用，因此可能直接转 up */
		UNSET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);

		/* Interface may come up after disabling link detection */
		/* 原本因无载波不可用、禁用后 if_is_operative 为真：if_up */
		if (if_is_operative(ifp) && !if_was_operative)
			if_up(ifp, true);
	}
	/* FIXME: Will defer status change forwarding if interface
	   does not come down! */
}

/* 管理性 shutdown：先按 RFC 4861 发 RA lifetime=0 再清 IFF_UP，
 * 刷新后协议侧收 IF_DOWN；配置记 shutdown=ON（接口重现时
 * if_add_update 据此不再拉起） */
int if_shutdown(struct interface *ifp)
{
	struct zebra_if *if_data;

	if (ifp->ifindex != IFINDEX_INTERNAL) {
		/* send RA lifetime of 0 before stopping. rfc4861/6.2.5 */
		/* 有真实 ifindex：先按 RFC 4861 发 lifetime=0 RA，再清 IFF_UP */
		rtadv_stop_ra(ifp, false);
		if (if_unset_flags(ifp, IFF_UP) < 0) {
			zlog_debug("Can't shutdown interface %s", ifp->name);
			return -1;
		}
		if_refresh(ifp);
	}
	/* 记 shutdown=ON，接口重现时 if_add_update 不再拉起 */
	if_data = ifp->info;
	if_data->shutdown = IF_ZEBRA_DATA_ON;

	return 0;
}

/* 取消 shutdown：置 IFF_UP|IFF_RUNNING 并刷新，随后 if_addr_wakeup
 * 补装 down 期间被内核移走的地址（典型如 Linux 的 IPv6 地址）；
 * 配置记 shutdown=OFF */
int if_no_shutdown(struct interface *ifp)
{
	struct zebra_if *if_data;

	if (ifp->ifindex != IFINDEX_INTERNAL) {
		if (if_set_flags(ifp, IFF_UP | IFF_RUNNING) < 0) {
			zlog_debug("Can't up interface %s", ifp->name);
			return -1;
		}
		if_refresh(ifp);

		/* Some addresses (in particular, IPv6 addresses on Linux) get
		 * removed when the interface goes down. They need to be
		 * readded.
		 */
		/* 补装 down 期间被内核移走的地址（典型如 Linux IPv6） */
		if_addr_wakeup(ifp);
	}

	/* 记 shutdown=OFF */
	if_data = ifp->info;
	if_data->shutdown = IF_ZEBRA_DATA_OFF;

	return 0;
}

/* link-params（TE 属性）赋值公共入口：值变化或首次设置时写入并
 * 置对应 PARAM 位（uint32/float 两型，调用方传字段指针） */
void link_param_cmd_set_uint32(struct interface *ifp, uint32_t *field,
			       uint32_t type, uint32_t value)
{
	/* Update field as needed */
	/* 首次设置或值变化才写入，并置对应 PARAM 位 */
	if (IS_PARAM_UNSET(ifp->link_params, type) || *field != value) {
		*field = value;
		SET_PARAM(ifp->link_params, type);
	}
}

void link_param_cmd_set_float(struct interface *ifp, float *field,
			      uint32_t type, float value)
{
	/* Update field as needed */
	/* float 版本同上：变化或首次才写入并置位 */
	if (IS_PARAM_UNSET(ifp->link_params, type) || *field != value) {
		*field = value;
		SET_PARAM(ifp->link_params, type);
	}
}

/* 删除单个 link-param：只清 PARAM 位（保留字段值，与 YANG
 * 默认值比较时需注意） */
void link_param_cmd_unset(struct interface *ifp, uint32_t type)
{
	if (ifp->link_params == NULL)
		return;

	/* Unset field */
	UNSET_PARAM(ifp->link_params, type);
}

/* CLI/YANG 安装 IPv4 接口地址：已存在相同 connected 则复用，否则
 * 新建（含点对点 destination 与 label 别名）并挂入接口链表；标记
 * CONFIGURED；接口激活且未 shutdown 时置 pending 地址所需 flags
 * 后经 dplane 下发内核并标 QUEUED，协议广播/子网链表由内核回环
 * 通知补齐（见 zebra_if_addr_update_ctx） */
void if_ip_address_install(struct interface *ifp, struct prefix *prefix,
			   const char *label, struct prefix *pp)
{
	struct zebra_if *if_data;
	struct connected *ifc;

	if_data = ifp->info;

	/* 已存在相同 connected 则复用，否则新建（含点对点 destination
	 * 与 label 别名）并挂入接口链表尾部 */
	ifc = connected_check_ptp(ifp, prefix, pp);
	/* 不存在则新建 connected：拷贝地址前缀，有 pp 则记点对点
	 * destination，有 label 则拷别名，最后挂入接口链表尾 */
	if (!ifc) {
		ifc = connected_new();
		ifc->ifp = ifp;

		/* Address. */
		ifc->address = prefix_new();
		prefix_copy(ifc->address, prefix);

		/* 点对点：置 PEER 并保存对端地址 */
		if (pp) {
			SET_FLAG(ifc->flags, ZEBRA_IFA_PEER);
			ifc->destination = prefix_new();
			prefix_copy(ifc->destination, pp);
		}

		/* Label. */
		if (label)
			ifc->label = XSTRDUP(MTYPE_CONNECTED_LABEL, label);

		/* Add to linked list. */
		if_connected_add_tail(ifp->connected, ifc);
	}

	/* This address is configured from zebra. */
	/* 标记为“用户配置”地址（区别于内核自发学习） */
	if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED))
		SET_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED);

	/* In case of this route need to install kernel. */
	/* 尚未下发 + 接口激活 + 未 shutdown 才真正下发内核 */
	if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_QUEUED) &&
	    CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE) &&
	    !(if_data && if_data->shutdown == IF_ZEBRA_DATA_ON)) {
		/* Some system need to up the interface to set IP address. */
		/* 部分系统需先拉起接口才能配地址 */
		if (!if_is_up(ifp)) {
			if_set_flags(ifp, IFF_UP | IFF_RUNNING);
			if_refresh(ifp);
		}

		/* 经 dplane 下发地址并标 QUEUED，广播/子网链留给回环 */
		dplane_intf_addr_set(ifp, ifc);

		SET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);
		/* The address will be advertised to zebra clients when the
		 * notification
		 * from the kernel has been received.
		 * It will also be added to the subnet chain list, then. */
	}
}

/* 撤销 IPv4 接口地址：清 CONFIGURED；若从未真正下发（未 QUEUED）
 * 或接口已不激活，直接释放 connected；否则经 dplane 删内核地址，
 * 对象保留等待内核回环事件收敛 */
void if_ip_address_uninstall(struct interface *ifp, struct prefix *prefix,
			     struct prefix *pp)
{
	struct connected *ifc;

	ifc = connected_check_ptp(ifp, prefix, pp);
	assert(ifc);

	UNSET_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED);

	/* This is not real address or interface is not active. */
	/* 从未真正下发或未激活：直接释放 connected */
	if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_QUEUED)
	    || !CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		if_connected_del(ifp->connected, ifc);
		connected_free(&ifc);
		return;
	}

	/* This is real route. */
	/* 已下发：经 dplane 删内核地址，标回未 QUEUED 等回环收敛 */
	dplane_intf_addr_unset(ifp, ifc);

	UNSET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);
}

/* IPv6 地址安装：与 v4 同构，但不支持 label/点对点（v6 语义无此概念） */
void if_ipv6_address_install(struct interface *ifp, struct prefix *prefix)
{
	struct zebra_if *if_data;
	struct connected *ifc;

	if_data = ifp->info;

	ifc = connected_check(ifp, prefix);
	/* v6 无点对点/label：不存在则仅新建地址并挂链 */
	if (!ifc) {
		ifc = connected_new();
		ifc->ifp = ifp;

		/* Address. */
		ifc->address = prefix_new();
		prefix_copy(ifc->address, prefix);

		/* Add to linked list. */
		if_connected_add_tail(ifp->connected, ifc);
	}

	/* This address is configured from zebra. */
	/* 标记为用户配置地址 */
	if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED))
		SET_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED);

	/* In case of this route need to install kernel. */
	/* 未下发 + 激活 + 未 shutdown 才下发；必要时先拉起接口 */
	if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_QUEUED) &&
	    CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE) &&
	    !(if_data && if_data->shutdown == IF_ZEBRA_DATA_ON)) {
		/* Some system need to up the interface to set IP address. */
		if (!if_is_up(ifp)) {
			if_set_flags(ifp, IFF_UP | IFF_RUNNING);
			if_refresh(ifp);
		}

		/* 经 dplane 下发 v6 地址并标 QUEUED */
		dplane_intf_addr_set(ifp, ifc);

		SET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);
		/* The address will be advertised to zebra clients when the
		 * notification
		 * from the kernel has been received. */
	}
}

/* IPv6 地址撤销：与 v4 同构（无点对点分支） */
void if_ipv6_address_uninstall(struct interface *ifp, struct prefix *prefix)
{
	struct connected *ifc;

	ifc = connected_check(ifp, prefix);
	assert(ifc);

	UNSET_FLAG(ifc->conf, ZEBRA_IFC_CONFIGURED);

	/* This is not real address or interface is not active. */
	/* v6 同构（无点对点）：未下发/未激活则直接释放 */
	if (!CHECK_FLAG(ifc->conf, ZEBRA_IFC_QUEUED)
	    || !CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		if_connected_del(ifp->connected, ifc);
		connected_free(&ifc);
		return;
	}

	/* This is real route. */
	/* 已下发：经 dplane 删内核地址 */
	dplane_intf_addr_unset(ifp, ifc);

	UNSET_FLAG(ifc->conf, ZEBRA_IFC_QUEUED);
}

/* Allocate and initialize interface vector. */
/* 接口模块初始化（zebra 启动时调用）：注册 if_add/if_del 钩子
 * （分配/释放 zebra_if 私有数据），安装 show interface 系列命令 */
void zebra_if_init(void)
{
	/* Initialize interface and new hook. */
	hook_register_prio(if_add, 0, if_zebra_new_hook);
	hook_register_prio(if_del, 0, if_zebra_delete_hook);

	install_element(VIEW_NODE, &show_interface_cmd);
	install_element(VIEW_NODE, &show_interface_vrf_all_cmd);
	install_element(VIEW_NODE, &show_interface_name_vrf_cmd);
	install_element(VIEW_NODE, &show_interface_name_vrf_all_cmd);

	install_element(ENABLE_NODE, &show_interface_desc_cmd);
	install_element(ENABLE_NODE, &show_interface_desc_vrf_all_cmd);
}
