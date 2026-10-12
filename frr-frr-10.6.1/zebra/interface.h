// SPDX-License-Identifier: GPL-2.0-or-later

/* Interface function header.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

#ifndef _ZEBRA_INTERFACE_H
#define _ZEBRA_INTERFACE_H

#ifndef __linux__
#include <net/if_dl.h>
#endif

#include "redistribute.h"
#include "vrf.h"
#include "hook.h"
#include "bitfield.h"

#include "zebra/zebra_l2.h"
#include "zebra/zebra_l2_bridge_if.h"
#include "zebra/zebra_nhg_private.h"
#include "zebra/zebra_router.h"
#include "zebra/rtadv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* For interface configuration. */
/* 接口级 zebra 配置三态：未配置（继承默认）/显式开启/显式关闭 */
#define IF_ZEBRA_DATA_UNSPEC 0
#define IF_ZEBRA_DATA_ON 1
#define IF_ZEBRA_DATA_OFF 2

/* VLAN 位图最大容量：一个接口最多可关联 4096 个 VLAN（802.1Q 标签空间） */
#define IF_VLAN_BITMAP_MAX 4096

/* Zebra "slave" interface type */
/* zebra 视角下的“从属接口”类型：描述一个接口挂载/隶属于哪个上层设备。
 * 由内核 netlink 的 IFLA_MASTER 等信息解析而来，决定 bridge/bond/vrf
 * 成员关系的管理与转发表项联动。 */
enum zebra_slave_iftype {
	ZEBRA_IF_SLAVE_NONE,   /* Not a slave */
	ZEBRA_IF_SLAVE_VRF,    /* Member of a VRF */
	ZEBRA_IF_SLAVE_BRIDGE, /* Member of a bridge */
	ZEBRA_IF_SLAVE_BOND,   /* Bond member */
	ZEBRA_IF_SLAVE_OTHER,  /* Something else - e.g., bond slave */
};

/* Ethernet segment info used for setting up EVPN multihoming */
struct zebra_evpn_es;
/* EVPN 多归属（EVPN Multihoming）以太网段（ES）接口配置信息。
 * 用于 type-3 ESI 的本地生成：ESI = 0 + sysmac(6B) + local-id(4B)，
 * lid 在同一 sysmac 下必须唯一；df_pref 影响指定转发器（DF）选举，
 * flags 携带旁路（bypass）等本地策略。 */
struct zebra_es_if_info {
	/* type-3 esi config */
	struct ethaddr sysmac;
	uint32_t lid; /* local-id; has to be unique per-ES-sysmac */

	esi_t esi;

	uint16_t df_pref;
	uint8_t flags;
#define ZIF_CFG_ES_FLAG_BYPASS (1 << 0) /* DF 选举旁路：本 PE 恒不参战 */

	struct zebra_evpn_es *es; /* local ES */
};

/* struct zebra_if 的私有标志位集合（可多选，见 enum zebra_if_flags） */
enum zebra_if_flags {
	/* device has been configured as an uplink for
	 * EVPN multihoming
	 */
	ZIF_FLAG_EVPN_MH_UPLINK = (1 << 0),           /* 配置为 EVPN 多归属上行口 */
	ZIF_FLAG_EVPN_MH_UPLINK_OPER_UP = (1 << 1),   /* 上行口运行态为 up */

	/* Dataplane protodown-on */
	ZIF_FLAG_PROTODOWN = (1 << 2),
	/* Dataplane protodown-on Queued to the dplane */
	ZIF_FLAG_SET_PROTODOWN = (1 << 3),
	/* Dataplane protodown-off Queued to the dplane */
	ZIF_FLAG_UNSET_PROTODOWN = (1 << 4),

	/* LACP bypass state is set by the dataplane on a bond member
	 * and inherited by the bond (if one or more bond members are in
	 * a bypass state the bond is placed in a bypass state)
	 */
	ZIF_FLAG_LACP_BYPASS = (1 << 5)               /* LACP 旁路：成员带起则 bond 整体带起 */
};

/* 接口当前是否处于 protodown（协议层致 down）状态 */
#define ZEBRA_IF_IS_PROTODOWN(zif) ((zif)->flags & ZIF_FLAG_PROTODOWN)
/* protodown 是否仅由外部（数据面/驱动）原因引起：撤销时只清外部位 */
#define ZEBRA_IF_IS_PROTODOWN_ONLY_EXTERNAL(zif)                               \
	((zif)->protodown_rc == ZEBRA_PROTODOWN_EXTERNAL)

/* `zebra' daemon local interface structure. */
/* zebra 守护进程私有的接口扩展信息，挂在通用 struct interface 的
 * ifp->info 上（接口创建时分配、销毁时释放）。承载所有“内核接口
 * 模型没有、但路由协议/vtysh 配置需要”的内容：管理配置（shutdown/
 * multicast/rtadv 等）、接口分类（zif_type/Slave 类型）、二层信息、
 * EVPN ES、protodown 原因位图、上/下线统计、依赖本接口的 nexthop 等。
 * 各字段的读写集中在 interface.c 与 if_netlink.c。 */
struct zebra_if {
	/* back pointer to the interface */
	struct interface *ifp;

	/* Event timer to batch  ICMPv6 join requests */
	/* 批量合并 ICMPv6 组播加入请求的定时器，避免高频 join 冲击内核 */
	struct event *icmpv6_join_timer;

	enum zebra_if_flags flags;

	/* Shutdown configuration. */
	/* 管理配置三态（IF_ZEBRA_DATA_*）：是否 shutdown 本接口 */
	uint8_t shutdown;

	/* Multicast configuration. */
	/* 管理配置三态：是否允许接口收发组播（影响 mroutes/IGMP join） */
	uint8_t multicast;

	/* MPLS status. */
	/* 内核实际状态：该接口当前是否使能 MPLS（AF_MPLS 转发面） */
	bool mpls;

	/* MPLS configuration */
	/* 管理配置三态：用户是否要求在该接口开启/关闭 MPLS */
	uint8_t mpls_config;

	/* Linkdown status */
	/* zebra 内部“管理员期望 down”状态（v4/v6 分开），
	 * 与内核 IFF_LOWER_UP 共同决定是否撤销直连路由/邻居 */
	bool linkdown, linkdownv6;

	/* Is Multicast Forwarding on? */
	/* 内核 ipv4/ipv6 multicast forwarding sysctl 当前是否打开 */
	bool v4mcast_on, v6mcast_on;

	/* Router advertise configuration. */
	/* IPv6 RA（rtadv）使能配置 */
	uint8_t rtadv_enable;

	/* Installed addresses chains tree. */
	/* 本接口已安装 IPv4 地址（前缀）红黑树：connected 的索引，
	 * 供地址重复检查与直连路由/邻接表项联动 */
	struct route_table *ipv4_subnets;

	/* Nexthops pointing to this interface */
	/**
	 * Any nexthop that we get should have an
	 * interface. When an interface goes down,
	 * we will use this list to update the nexthops
	 * pointing to it with that info.
	 */
	/* 引用本接口的所有 nexthop/nhg 依赖树：接口 down 时据此
	 * 批量通知相关下一跳失效（zebra_nhg 联动） */
	struct nhg_connected_tree_head nhg_dependents;

	/* Information about up/down changes */
	/* 接口上/下线次数与最近一次时间戳（show interface 统计用） */
	unsigned int up_count;
	char up_last[FRR_TIMESTAMP_LEN];
	unsigned int down_count;
	char down_last[FRR_TIMESTAMP_LEN];

	/* RA 协议参数（interval/prefix 等）及 RA/RS 报文收发计数 */
	struct rtadvconf rtadv;
	unsigned int ra_sent, ra_rcvd;
	unsigned int rs_rcvd;

#ifdef HAVE_STRUCT_SOCKADDR_DL
	/* BSD sockaddr_dl（链路层地址）：sdl_storage 只为占位从不访问，
	 * 实际使用 sdl，但 sizeof 按 sdl_storage 计，符合 C 别名规则 */
	union {
		/* note that sdl_storage is never accessed, it only exists to
		 * make space.
		 * all actual uses refer to sdl - but use sizeof(sdl_storage)!
		 * this fits
		 * best with C aliasing rules. */
		struct sockaddr_dl sdl;
		struct sockaddr_storage sdl_storage;
	};
#endif

	/* ptm enable configuration */
	/* PTM（Profile-based Tunnel Multicast）使能配置三态 */
	uint8_t ptm_enable;

	/* Zebra interface and "slave" interface type */
	/* 接口主类型（VRF/BRIDGE/VLAN/VXLAN/BOND...）与从属类型，
	 * 由 if_netlink 从内核属性解析后填入 */
	enum zebra_iftype zif_type;
	enum zebra_slave_iftype zif_slave_type;

	/* Additional L2 info, depends on zif_type */
	/* 二层附加信息（按 zif_type 选用：bridge 的 fdb、vxlan 的
	 * 封装源地址、vlan 的父口/VID 等），与内核联动的缓存副本 */
	union zebra_l2if_info l2info;

	/* For members of a bridge, link to bridge. */
	/* Note: If additional fields become necessary, this can be modified to
	 * be a pointer to a dynamically allocd struct.
	 */
	/* bridge 成员口的桥关联信息（桥 ifindex 等） */
	struct zebra_l2info_brslave brslave_info;

	/* bond 成员口信息（所属 bond、LACP bypass 等）与 bond 自身信息 */
	struct zebra_l2info_bondslave bondslave_info;
	struct zebra_l2info_bond bond_info;

	/* ethernet segment */
	/* EVPN 多归属以太网段配置（见 struct zebra_es_if_info） */
	struct zebra_es_if_info es_info;

	/* bitmap of vlans associated with this interface */
	/* 本接口下挂的 VLAN 集合位图（vid -> 子接口存在性） */
	bitfield_t vlan_bitmap;

	/* An interface can be error-disabled if a protocol (such as EVPN or
	 * VRRP) detects a problem with keeping it operationally-up.
	 * If any of the protodown bits are set protodown-on is programmed
	 * in the dataplane. This results in a carrier/L1 down on the
	 * physical device.
	 */
	/* protodown 原因位图（ZEBRA_PROTODOWN_*）：EVPN/VRRP 等协议
	 * 检测到问题时置位，任一非零位都会向数据面下发 protodown-on，
	 * 物理设备表现为载波/L1 down */
	uint32_t protodown_rc;

	/* list of zebra_mac entries using this interface as destination */
	/* 以本接口为目的口的静态 MAC 表项列表 */
	struct list *mac_list;

	/* Link fields - for sub-interfaces. */
	/* 子接口（如 vlan 子口）的父链路信息：父口所在 NS 与 ifindex，
	 * link 指向父接口对象（zebra_if_update_link 维护） */
	ns_id_t link_nsid;
	ifindex_t link_ifindex;
	struct interface *link;

	/* 接口速率探测失败时的两个哨兵返回值 */
#define INTERFACE_SPEED_ERROR_READ    -1 /* sysctl/内核读取失败 */
#define INTERFACE_SPEED_ERROR_UNKNOWN -2 /* 无法获知速率 */

	/* 速率变化防抖：连续更新计数与延迟定时器（带宽联动 metric） */
	uint8_t speed_update_count;
	struct event *speed_update;
	uint32_t speed_checked;

	/*
	 * Does this interface have a v6 to v4 ll neighbor entry
	 * for bgp unnumbered?
	 */
	/* BGP 未编号（unnumbered）场景：是否已为本接口创建
	 * “IPv6 链路本地地址 -> IPv4 ll”映射邻居表项，并缓存
	 * 对应的 MAC 与 v6 地址，供接口删除时回收 */
	bool v6_2_v4_ll_neigh_entry;
	char neigh_mac[6];
	struct in6_addr v6_2_v4_ll_addr6;

	/* Linkage for per-vrf/per-NS ifp container */
	/* 接口在“每 VRF/每命名空间”ifp 容器中的链表节点 */
	struct ifp_tree_link *ns_tree_link;

	/* The description of the interface */
	/* 接口描述字符串（vtysh `description` 配置，动态分配） */
	char *desc;
};

/* 扩展 show interface 输出的钩子：各模块（如 EVPN/MLAG）可注册自己的
 * 附加信息打印（文本或 JSON 分支） */
DECLARE_HOOK(zebra_if_extra_info, (struct vty * vty, json_object *json_if, struct interface *ifp),
	     (vty, json_if, ifp));

/* 以下 IS_ZEBRA_IF_xxx 系列宏：从 ifp->info 取 zif_type 判断接口主类型；
 * IS_ZEBRA_IF_xxx_SLAVE 系列则判断 zif_slave_type（从属关系）。
 * 调用前需保证 ifp->info 已是 struct zebra_if（zebra 内部接口均满足） */
#define IS_ZEBRA_IF_VRF(ifp)                                                   \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_VRF)

#define IS_ZEBRA_IF_BRIDGE(ifp)                                                \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_BRIDGE)

#define IS_ZEBRA_IF_VLAN(ifp)                                                  \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_VLAN)

#define IS_ZEBRA_IF_VXLAN(ifp)                                                 \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_VXLAN)

#define IS_ZEBRA_IF_MACVLAN(ifp)                                               \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_MACVLAN)

#define IS_ZEBRA_IF_VETH(ifp)                                               \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_VETH)

#define IS_ZEBRA_IF_BOND(ifp)                                                  \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_BOND)

#define IS_ZEBRA_IF_GRE(ifp)                                               \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_GRE)

#define IS_ZEBRA_IF_IP6GRE(ifp) (((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_IP6GRE)

#define IS_ZEBRA_IF_GRETAP(ifp) (((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_GRETAP)

#define IS_ZEBRA_IF_IP6GRETAP(ifp)                                                                \
	(((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_IP6GRETAP)

#define IS_ZEBRA_IF_DUMMY(ifp) (((struct zebra_if *)(ifp->info))->zif_type == ZEBRA_IF_DUMMY)

/* 判断是否为 bridge/VRF/bond 的成员口（从属类型视角） */
#define IS_ZEBRA_IF_BRIDGE_SLAVE(ifp)					\
	(((struct zebra_if *)(ifp->info))->zif_slave_type                      \
	 == ZEBRA_IF_SLAVE_BRIDGE)

#define IS_ZEBRA_IF_VRF_SLAVE(ifp)                                             \
	(((struct zebra_if *)(ifp->info))->zif_slave_type == ZEBRA_IF_SLAVE_VRF)

#define IS_ZEBRA_IF_BOND_SLAVE(ifp)					\
	(((struct zebra_if *)(ifp->info))->zif_slave_type                      \
	 == ZEBRA_IF_SLAVE_BOND)

/* 接口子系统初始化：注册 if_by_index/if_by_name 的 VRF/NS 容器处理
 * 及各平台默认接口事件回调（zebra_if_init） */
extern void zebra_if_init(void);

/* 按 ifindex/接口名在指定网络命名空间（zns）内查接口；nsid 版本先找
 * zns 再查。接口按命名空间隔离：同一 ifindex 在不同 NS 可属于不同接口 */
extern struct interface *if_lookup_by_index_per_ns(struct zebra_ns *ns, uint32_t ifindex);
extern struct interface *if_lookup_by_name_per_ns(struct zebra_ns *ns, const char *ifname);
extern struct interface *if_lookup_by_index_per_nsid(ns_id_t nsid,
						     uint32_t ifindex);
extern const char *ifindex2ifname_per_ns(struct zebra_ns *zns, unsigned int ifindex);

/* BGP unnumbered 辅助：维护“MAC/IPv6-ll <-> IPv4-ll”映射邻居表项的
 * 增删（内核 neighbor 编程），接口销毁时 del_all 批量回收 */
extern void if_nbr_mac_to_ipv4ll_neigh_update(struct interface *fip,
					      char mac[6],
					      struct in6_addr *address,
					      int add);
extern void if_nbr_ipv6ll_to_ipv4ll_neigh_update(struct interface *ifp,
						 struct in6_addr *address,
						 int add);
extern void if_nbr_ipv6ll_to_ipv4ll_neigh_del_all(struct interface *ifp);
/* 接口生命周期事件广播：向所有路由协议（via hook/zclient）发送
 * 接口删除/新增消息。if_delete_update 接收**二级指针**，因为
 * EVPN 等路径可能在该调用中销毁 ifp（调用方需判空） */
extern void if_delete_update(struct interface **ifp);
extern void if_add_update(struct interface *ifp);
/* 接口 up/down 状态变更处理：安装/撤销直连路由、通知协议、
 * 联动 nhg 依赖表；install_connected 控制是否重装 v4/v6 直连前缀 */
extern void if_up(struct interface *ifp, bool install_connected);
extern void if_down(struct interface *ifp);

/* 重新评估接口运行态（结合 linkdown 配置与内核 IFF_* 标志），
 * 必要时触发 if_up/if_down；mtu/bandwidth 等属性变化也走此处 */
extern void if_refresh(struct interface *ifp);
/* 接口标志（IFF_UP/RUNNING/...）变化处理：差异对比后驱动刷新 */
extern void if_flags_update(struct interface *ifp, uint64_t newflags);

/* 接口地址（connected 前缀）增删：维护 ipv4_subnets 树、安装/撤销
 * 直连路由，并向协议发送接口地址通知 */
extern int if_subnet_add(struct interface *ifp, struct connected *ifc);
extern int if_subnet_delete(struct interface *ifp, struct connected *ifc);
/* 接口迁入/迁出 VRF（或 VRF id 变更）时重组：重发删除/新增事件、
 * 迁移地址与 nexthop 关联，使协议侧看到“旧接口消失+新接口出现” */
extern void if_handle_vrf_change(struct interface *ifp, vrf_id_t vrf_id);

/* 子接口 link 字段维护：按父 ifindex 关联父接口对象；all 版本在
 * 全量 dump 结束后统建一次（父口可能晚于子口到达） */
extern void zebra_if_update_link(struct interface *ifp, ifindex_t link_ifindex,
				 ns_id_t ns_id);
extern void zebra_if_update_all_links(struct zebra_ns *zns);
/**
 * Directly update entire protodown & reason code bitfield.
 */
/* 直接整体设置/撤销 protodown 及原因位图（数据面同步路径用，
 * new_down=false 时只清除指定原因位） */
extern int zebra_if_update_protodown_rc(struct interface *ifp, bool new_down,
					uint32_t new_protodown_rc);

/* YANG/CLI 展示回调：admin-group（legacy 名称）与 SRv6 链路亲和性
 * 模式/亲和位图 */
extern void cli_show_legacy_admin_group(struct vty *vty,
					const struct lyd_node *dnode,
					bool show_defaults);
extern void cli_show_affinity_mode(struct vty *vty,
				   const struct lyd_node *dnode,
				   bool show_defaults);
extern void cli_show_affinity(struct vty *vty, const struct lyd_node *dnode,
			      bool show_defaults);

/**
 * Set protodown with single reason.
 */
/* 以单一原因置位/清位 protodown：内部按位图合并后驱动数据面
 * protodown-on/off（区别于 update_protodown_rc 的整体赋值语义） */
extern int zebra_if_set_protodown(struct interface *ifp, bool down,
				  enum protodown_reasons new_reason);
/* CLI/YANG 驱动的接口地址增删（v4 可带 label/点对点地址，v6 仅前缀），
 * 经 dplane 下发内核，与 if_netlink 收到的回环事件收敛 */
extern void if_ip_address_install(struct interface *ifp, struct prefix *prefix,
				  const char *label, struct prefix *pp);
extern void if_ip_address_uninstall(struct interface *ifp,
				    struct prefix *prefix, struct prefix *pp);
extern void if_ipv6_address_install(struct interface *ifp,
				    struct prefix *prefix);
extern void if_ipv6_address_uninstall(struct interface *ifp,
				      struct prefix *prefix);
/* 接口管理配置命令实现：shutdown/no shutdown、ARP 开关、组播开关、
 * linkdetect（是否根据内核载波状态联动协议）、PTM 等 */
extern int if_shutdown(struct interface *ifp);
extern int if_no_shutdown(struct interface *ifp);
extern void if_arp(struct interface *ifp, bool enable);
extern int if_multicast_set(struct interface *ifp);
extern int if_multicast_unset(struct interface *ifp);
extern void if_linkdetect(struct interface *ifp, bool detect);
/* 地址延迟安装（接口未 up 时挂起）的唤醒：接口 up 后补装 pending 地址 */
extern void if_addr_wakeup(struct interface *ifp);

/* link-params（TE 链路属性：min/max delay、delay variation、residual/
 * available BW）命令层设置/删除：赋值 + 向 TE 数据库发变更通知；
 * type 为 LINK_PARAM_TYPE_* */
void link_param_cmd_set_uint32(struct interface *ifp, uint32_t *field,
			       uint32_t type, uint32_t value);
void link_param_cmd_set_float(struct interface *ifp, float *field,
			      uint32_t type, float value);
void link_param_cmd_unset(struct interface *ifp, uint32_t type);

/* 本接口的 nhg 依赖表是否为空（接口删除前的安全检查之一） */
/* Nexthop group connected functions */
extern bool if_nhg_dependents_is_empty(const struct interface *ifp);

/* VRF 设备新增后向协议广播 IF_ADD 消息；bond 成员关系映射：
 * slave 的二层信息指向 master，解绑时清空 */
extern void vrf_add_update(struct vrf *vrfp);
extern void zebra_l2_map_slave_to_bond(struct zebra_if *zif, vrf_id_t vrf);
extern void zebra_l2_unmap_slave_from_bond(struct zebra_if *zif);
/* protodown 原因位图转可读字符串（show interface/日志用） */
extern const char *zebra_protodown_rc_str(uint32_t protodown_rc, char *pd_buf,
					  uint32_t pd_buf_len);
/* dplane 结果回调：接口类操作（protodown 等出方向请求）完成后的
 * 后续处理；内核 link 事件也在此分流给 CTC 卸载层（ctc_dplane_intf_event） */
void zebra_if_dplane_result(struct zebra_dplane_ctx *ctx);

/* 平台适配入口（各 if_*.c 实现）：无 netlink 的平台从 /proc 或 sysctl
 * 轮询接口统计/地址列表，作为接口事件的兼容来源 */
#ifdef HAVE_PROC_NET_DEV
extern void ifstat_update_proc(void);
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
extern void ifstat_update_sysctl(void);

#endif /* HAVE_NET_RT_IFLIST */
#ifdef HAVE_PROC_NET_DEV
extern int interface_list_proc(void);
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_PROC_NET_IF_INET6
extern int ifaddr_proc_ipv6(void);
#endif /* HAVE_PROC_NET_IF_INET6 */

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_INTERFACE_H */
