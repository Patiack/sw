// SPDX-License-Identifier: GPL-2.0-or-later
/* Interface related header.
 * Copyright (C) 1997, 98, 99 Kunihiro Ishiguro
 */

/*
 * if.h —— FRR 接口（interface）对象模型与公共接口定义
 *
 * 核心概念：
 *   struct interface : 一个网络接口的完整内存表示，name / ifindex 是它的两个
 *                      身份标识；每个接口都归属某个 VRF（字段 vrf）。
 *   两棵查找索引树    : 按名字的 ifaces_by_name、按 ifindex 的 ifaces_by_index
 *                      （都存放在 VRF 结构里），因此查找必须带上
 *                      「哪个 VRF」这一上下文。
 *   ifindex_t        : 内核接口索引类型，内部接口用 IFINDEX_INTERNAL(0) 表示
 *                      「内核里不存在」。
 *
 * 名字与索引的坑：
 *   接口名长度固定 IFNAMSIZ，建好后不应再改（配置跟着结构走）；
 *   要改 ifindex 必须用 if_set_index()，直接赋值会破坏索引树，
 *   导致 if_lookup_by_index() 失效。
 *
 * 查找接口的几类入口（实现见 if.c）：
 *   不带 vrf 参数的老接口默认在「默认 VRF」里查；
 *   带 vrf 参数的接口在指定 VRF 里查；
 *   按名字查与按 ifindex 查走两棵不同的树。
 *   「是否按名字 / 按索引」「是否指定 VRF」的组合很多，用时务必看清函数名后缀。
 *
 * 其它约定：
 *   VRF 用 struct vrf 表示（vrf.h）；协议地址（if_connected）挂在接口下；
 *   link_params 承载 TE（流量工程）链路参数；info 留给各守护进程挂私有数据。
 */

#ifndef _ZEBRA_IF_H
#define _ZEBRA_IF_H

#include "zebra.h"
#include "linklist.h"
#include "memory.h"
#include "qobj.h"
#include "hook.h"
#include "admin_group.h"

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_MTYPE(CONNECTED_LABEL);

/* Interface link-layer type, if known. Derived from:
 *
 * net/if_arp.h on various platforms - Linux especially.
 * http://www.iana.org/assignments/arp-parameters/arp-parameters.xhtml
 *
 * Some of the more obviously defunct technologies left out.
 */
/*
 * enum zebra_link_type —— 链路层类型（镜像内核 ARP 硬件类型号）。
 * 取值对应 net/if_arp.h 的 ARPHRD_*（Linux 上）与 IANA 的 arp-parameters：
 * 如 ZEBRA_LLT_ETHER 以太网、ZEBRA_LLT_LOOPBACK 环回、ZEBRA_LLT_PPP 为 PPP。
 * 只收录常用类型，已淘汰的多数省略。
 */
enum zebra_link_type {
	ZEBRA_LLT_UNKNOWN = 0,
	ZEBRA_LLT_ETHER,
	ZEBRA_LLT_EETHER,
	ZEBRA_LLT_AX25,
	ZEBRA_LLT_PRONET,
	ZEBRA_LLT_IEEE802,
	ZEBRA_LLT_ARCNET,
	ZEBRA_LLT_APPLETLK,
	ZEBRA_LLT_DLCI,
	ZEBRA_LLT_ATM,
	ZEBRA_LLT_METRICOM,
	ZEBRA_LLT_IEEE1394,
	ZEBRA_LLT_EUI64,
	ZEBRA_LLT_INFINIBAND,
	ZEBRA_LLT_SLIP,
	ZEBRA_LLT_CSLIP,
	ZEBRA_LLT_SLIP6,
	ZEBRA_LLT_CSLIP6,
	ZEBRA_LLT_RSRVD,
	ZEBRA_LLT_ADAPT,
	ZEBRA_LLT_ROSE,
	ZEBRA_LLT_X25,
	ZEBRA_LLT_PPP,
	ZEBRA_LLT_CHDLC,
	ZEBRA_LLT_LAPB,
	ZEBRA_LLT_RAWHDLC,
	ZEBRA_LLT_IPIP,
	ZEBRA_LLT_IPIP6,
	ZEBRA_LLT_FRAD,
	ZEBRA_LLT_SKIP,
	ZEBRA_LLT_LOOPBACK,
	ZEBRA_LLT_LOCALTLK,
	ZEBRA_LLT_FDDI,
	ZEBRA_LLT_SIT,
	ZEBRA_LLT_IPDDP,
	ZEBRA_LLT_IPGRE,
	ZEBRA_LLT_IP6GRE,
	ZEBRA_LLT_PIMREG,
	ZEBRA_LLT_HIPPI,
	ZEBRA_LLT_ECONET,
	ZEBRA_LLT_IRDA,
	ZEBRA_LLT_FCPP,
	ZEBRA_LLT_FCAL,
	ZEBRA_LLT_FCPL,
	ZEBRA_LLT_FCFABRIC,
	ZEBRA_LLT_IEEE802_TR,
	ZEBRA_LLT_IEEE80211,
	ZEBRA_LLT_IEEE80211_RADIOTAP,
	ZEBRA_LLT_IEEE802154,
	ZEBRA_LLT_IEEE802154_PHY,
};

/* interface type - ones of interest. */
/*
 * enum zebra_iftype —— FRR 关心的接口种类。
 * ZEBRA_IF_OTHER 为默认；其余覆盖 VXLAN / VRF 设备 / bridge / VLAN 子接口 /
 * MACVLAN / VETH / BOND / GRE / IP6GRE / GRETAP / IP6GRETAP / DUMMY。
 * 上层据此决定对接口做哪些特殊处理（例如是否当作 VRF 设备）。
 */
enum zebra_iftype {
	ZEBRA_IF_OTHER = 0, /* Anything else */
	ZEBRA_IF_VXLAN,	    /* VxLAN interface */
	ZEBRA_IF_VRF,	    /* VRF device */
	ZEBRA_IF_BRIDGE,    /* bridge device */
	ZEBRA_IF_VLAN,	    /* VLAN sub-interface */
	ZEBRA_IF_MACVLAN,   /* MAC VLAN interface*/
	ZEBRA_IF_VETH,	    /* VETH interface*/
	ZEBRA_IF_BOND,	    /* Bond */
	ZEBRA_IF_GRE,	    /* GRE interface */
	ZEBRA_IF_IP6GRE,    /* IP6GRE interface */
	ZEBRA_IF_GRETAP,    /* GRETAP interface */
	ZEBRA_IF_IP6GRETAP, /* IP6GRETAP interface */
	ZEBRA_IF_DUMMY,	    /* Dummy interface */
};

/*
  Interface name length.

   Linux define value in /usr/include/linux/if.h.
   #define IFNAMSIZ        16

   FreeBSD define value in /usr/include/net/if.h.
   #define IFNAMSIZ        16
*/
/*
 * INTERFACE_HWADDR_MAX —— 硬件地址（MAC 等）缓冲的最大字节数（取 20）。
 * 接口名本身的长度上限则由 IFNAMSIZ(16) 限定。
 */
#define INTERFACE_HWADDR_MAX  20

/*
 * ifindex_t —— 接口索引（内核 ifindex）的类型，用有符号 int。
 * 内部 / 已删除接口的索引固定为 IFINDEX_INTERNAL(0)，表示「内核中不存在」。
 */
typedef signed int ifindex_t;

#ifdef HAVE_PROC_NET_DEV
/*
 * struct if_stats —— /proc/net/dev 风格的内核接口统计计数。
 * 仅在 HAVE_PROC_NET_DEV（Linux 等）时编译进来，供 zebra 上报接口流量。
 */
struct if_stats {
	unsigned long rx_packets;   /* total packets received       */
	unsigned long tx_packets;   /* total packets transmitted    */
	unsigned long rx_bytes;     /* total bytes received         */
	unsigned long tx_bytes;     /* total bytes transmitted      */
	unsigned long rx_errors;    /* bad packets received         */
	unsigned long tx_errors;    /* packet transmit problems     */
	unsigned long rx_dropped;   /* no space in linux buffers    */
	unsigned long tx_dropped;   /* no space available in linux  */
	unsigned long rx_multicast; /* multicast packets received   */
	unsigned long rx_compressed;
	unsigned long tx_compressed;
	unsigned long collisions;

	/* detailed rx_errors: */
	unsigned long rx_length_errors;
	unsigned long rx_over_errors;   /* receiver ring buff overflow  */
	unsigned long rx_crc_errors;    /* recved pkt with crc error    */
	unsigned long rx_frame_errors;  /* recv'd frame alignment error */
	unsigned long rx_fifo_errors;   /* recv'r fifo overrun          */
	unsigned long rx_missed_errors; /* receiver missed packet     */
	/* detailed tx_errors */
	unsigned long tx_aborted_errors;
	unsigned long tx_carrier_errors;
	unsigned long tx_fifo_errors;
	unsigned long tx_heartbeat_errors;
	unsigned long tx_window_errors;
};
#endif /* HAVE_PROC_NET_DEV */

/* Here are "non-official" architectural constants. */
/*
 * 下面是一组 TE（流量工程）相关的“非标准”常量：
 * TE_EXT_MASK / TE_EXT_ANORMAL 用于扩展管理组（RFC7308）的位掩码与标志位；
 * LOSS_PRECISION 丢包率精度；TE_MEGA_BIT、TE_BYTE 用于带宽单位换算；
 * DEFAULT_BANDWIDTH 未配置时默认带宽（单位 Mbps，会再乘 TE_BYTE）；
 * MAX_CLASS_TYPE 未预留带宽的类类型个数；MAX_PKT_LOSS 最大丢包率。
 */
#define TE_EXT_MASK             0x00FFFFFF
#define TE_EXT_ANORMAL          0x80000000
#define LOSS_PRECISION          0.000003
/* TE_MEGA_BIT and TE_BYTE are utilized to convert TE bandwidth */
#define TE_MEGA_BIT             1000000
#define TE_BYTE                 8
/* Default TE bandwidth when no value in config.
 * The value is in Mbps (will be multiplied by TE_BYTE)
 */
#define DEFAULT_BANDWIDTH 10
#define MAX_CLASS_TYPE          8
#define MAX_PKT_LOSS            50.331642

/*
 * enum affinity_mode —— 管理组（affinity）的位图宽度模式。
 * AFFINITY_MODE_EXTENDED : RFC7308 扩展管理组（字长更长）。
 * AFFINITY_MODE_STANDARD : RFC3630 / RFC5305 / RFC5329 标准管理组。
 * AFFINITY_MODE_BOTH     : 同时使用标准与扩展两种管理组。
 */
enum affinity_mode {
	/* RFC7308 Extended Administrative group */
	AFFINITY_MODE_EXTENDED = 0,
	/* RFC3630/RFC5305/RFC5329 Administrative group */
	AFFINITY_MODE_STANDARD = 1,
	/* Standard and Extended Administrative group */
	AFFINITY_MODE_BOTH = 2,
};

/*
 * Link Parameters Status:
 *  equal to 0: unset
 *  different from 0: set
 */
/*
 * Link Parameters Status 位图：标记 if_link_params 里哪些字段被设置过。
 * LP_UNSET 表示全未设置；其余每个 LP_xxx 对应一个有效字段
 * （TE metric、最大 / 可预留 / 未预留带宽、管理组、远端 AS、各类时延、
 *  丢包率、余量 / 可用 / 已用带宽、扩展管理组等）。
 */
#define LP_UNSET                0x0000
#define LP_TE_METRIC            0x0001
#define LP_MAX_BW               0x0002
#define LP_MAX_RSV_BW           0x0004
#define LP_UNRSV_BW             0x0008
#define LP_ADM_GRP              0x0010
#define LP_RMT_AS               0x0020
#define LP_DELAY                0x0040
#define LP_MM_DELAY             0x0080
#define LP_DELAY_VAR            0x0100
#define LP_PKT_LOSS             0x0200
#define LP_RES_BW               0x0400
#define LP_AVA_BW               0x0800
#define LP_USE_BW               0x1000
#define LP_EXTEND_ADM_GRP 0x2000

/*
 * 便捷宏：判断 / 设置链路参数状态位。
 * IS_LINK_PARAMS_SET 为真表示至少有一个参数被设置过；
 * SET_PARAM / UNSET_PARAM 置位 / 清位；RESET_LINK_PARAM 清零全部。
 */
#define IS_PARAM_UNSET(lp, st) !(lp->lp_status & st)
#define IS_PARAM_SET(lp, st) (lp->lp_status & st)
#define IS_LINK_PARAMS_SET(lp) (lp->lp_status != LP_UNSET)

#define SET_PARAM(lp, st) (lp->lp_status) |= (st)
#define UNSET_PARAM(lp, st) (lp->lp_status) &= ~(st)
#define RESET_LINK_PARAM(lp) (lp->lp_status = LP_UNSET)

/* Link Parameters for Traffic Engineering
 * Do not forget to update if_link_params_copy()
 * and if_link_params_cmp() when updating the structure
 */
/*
 * struct if_link_params —— TE 流量工程链路参数。
 * lp_status 是位图，标记下列各字段哪些有效（见 LP_xxx 宏）。
 * 带宽类字段单位为 “字节每秒” 语义（结合 TE_BYTE 换算）；ext_admin_grp 为扩展管理组。
 * 维护提醒：改动本结构时，务必同步更新 if_link_params_copy() 与 if_link_params_cmp()。
 */
struct if_link_params {
	uint32_t lp_status; /* Status of Link Parameters: */
	uint32_t te_metric; /* Traffic Engineering metric */
	float default_bw;
	float max_bw;			/* Maximum Bandwidth */
	float max_rsv_bw;		/* Maximum Reservable Bandwidth */
	float unrsv_bw[MAX_CLASS_TYPE]; /* Unreserved Bandwidth per Class Type
					   (8) */
	uint32_t admin_grp; /* RFC5305/RFC5329 Administrative group */
	struct admin_group ext_admin_grp; /* RFC7308 Extended Admin group */
	uint32_t rmt_as;		/* Remote AS number */
	struct in_addr rmt_ip;		/* Remote IP address */
	uint32_t av_delay;		/* Link Average Delay */
	uint32_t min_delay;		/* Link Min Delay */
	uint32_t max_delay;		/* Link Max Delay */
	uint32_t delay_var;		/* Link Delay Variation */
	uint32_t pkt_loss;		/* Link Packet Loss */
	float res_bw;			/* Residual Bandwidth */
	float ava_bw;			/* Available Bandwidth */
	float use_bw;			/* Utilized Bandwidth */
};

/*
 * INTERFACE_LINK_PARAMS_SIZE : 本结构尺寸，供分配使用。
 * HAS_LINK_PARAMS(ifp)       : 该接口是否挂了 link_params。
 */
#define INTERFACE_LINK_PARAMS_SIZE   sizeof(struct if_link_params)
#define HAS_LINK_PARAMS(ifp)  ((ifp)->link_params != NULL)

/*
 * if_connected 是一个类型安全的双向链表（typesafe DLIST），
 * 用来存放接口上「直连」的协议地址（connected address）集合。
 */
PREDECL_DLIST(if_connected);

/*
 * struct interface —— FRR 对一个网络接口的完整内存表示（核心数据结构）。
 *
 * 身份：
 *   name[IFNAMSIZ] : 接口名，同时是「按名字查找」索引树的 key；建好后不应修改，
 *                    改名字会丢失与之关联的配置。
 *   ifindex        : 内核接口索引，是「按索引查找」索引树的 key；非内核 / 已删除
 *                    接口置 IFINDEX_INTERNAL(0)。改它必须用 if_set_index()，
 *                    直接赋值会破坏索引树。
 *   oldifindex     : 改名 / 改索引前的旧值，便于与内核事件对齐。
 *   link_ifindex   : 父接口（如 VLAN 的物理口、VRF 相关）索引，无则 0。
 *
 * 归属：
 *   vrf            : 本接口所属的 VRF；所有查找都要在对应 VRF 内进行。
 *
 * 状态与属性：
 *   status         : zebra 内部状态位（ACTIVE / SUB / LINKDETECTION /
 *                    VRF_LOOPBACK / DUMMY，见下面 ZEBRA_INTERFACE_xxx）；
 *   flags / metric / speed / txqlen : 内核接口标志、度量、速率、发送队列长度；
 *   mtu / mtu6     : IPv4 / IPv6 MTU；
 *   ll_type / zif_type : 链路层类型与 FRR 接口种类；
 *   hw_addr / hw_addr_len : 硬件地址与长度；
 *   bandwidth      : 接口带宽（kbit）；
 *   link_params    : TE 链路参数（可为 NULL）；
 *   desc           : 接口描述串。
 *
 * 挂载与私有数据：
 *   connected[1]   : 直连地址集合的头（写成数组以兼容旧写法）；
 *   nbr_connected  : 邻居直连地址链表；
 *   info           : 各守护进程挂自己私有数据的指针（协议各自解释）；
 *   state          : 该接口对应的 YANG state 数据；
 *   configured     : 用户是否从 CLI 配置过该接口；
 *   QOBJ_FIELDS    : 对象调试跟踪字段。
 */
/* Interface structure */
struct interface {
	RB_ENTRY(interface) name_entry, index_entry;

	/* Interface name.  This should probably never be changed after the
	   interface is created, because the configuration info for this
	   interface
	   is associated with this structure.  For that reason, the interface
	   should also never be deleted (to avoid losing configuration info).
	   To delete, just set ifindex to IFINDEX_INTERNAL to indicate that the
	   interface does not exist in the kernel.
	 */
	char name[IFNAMSIZ];

	/* Interface index (should be IFINDEX_INTERNAL for non-kernel or
	   deleted interfaces).
	   WARNING: the ifindex needs to be changed using the if_set_index()
	   function. Failure to respect this will cause corruption in the data
	   structure used to store the interfaces and if_lookup_by_index() will
	   not work as expected.
	 */
	ifindex_t ifindex;
	ifindex_t oldifindex;

	/*
	 * ifindex of parent interface, if any
	 */
	ifindex_t link_ifindex;
#define IFINDEX_INTERNAL	0

	/* Zebra internal interface status */
	uint8_t status;
#define ZEBRA_INTERFACE_ACTIVE     (1 << 0)
#define ZEBRA_INTERFACE_SUB        (1 << 1)
#define ZEBRA_INTERFACE_LINKDETECTION (1 << 2)
#define ZEBRA_INTERFACE_VRF_LOOPBACK (1 << 3)
#define ZEBRA_INTERFACE_DUMMY (1 << 4)

	/* Interface flags. */
	uint64_t flags;

	/* Interface metric */
	uint32_t metric;

	/* Interface Speed in Mb/s */
	uint32_t speed;

	/* TX queue len */
	uint32_t txqlen;

	/* Interface MTU. */
	unsigned int mtu; /* IPv4 MTU */
	unsigned int
		mtu6; /* IPv6 MTU - probably, but not necessarily same as mtu
		       */

	/* Link-layer information and hardware address */
	enum zebra_link_type ll_type;

	/* Zebra interface type */
	enum zebra_iftype zif_type;

	uint8_t hw_addr[INTERFACE_HWADDR_MAX];
	int hw_addr_len;

	/* interface bandwidth, kbits */
	unsigned int bandwidth;

	/* Link parameters for Traffic Engineering */
	struct if_link_params *link_params;

	/* description of the interface. */
	char *desc;

	/* Connected address list. */
	struct if_connected_head connected[1];

	/* Neighbor connected address list. */
	struct list *nbr_connected;

	/* Daemon specific interface data pointer. */
	void *info;

	char ptm_enable; /* Should we look at ptm_status ? */
	char ptm_status;

/* Statistics fileds. */
#ifdef HAVE_PROC_NET_DEV
	struct if_stats stats;
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
	struct if_data stats;
#endif /* HAVE_NET_RT_IFLIST */

	struct vrf *vrf;

	struct lyd_node *state;

	/*
	 * Has the end users entered `interface XXXX` from the cli in some
	 * fashion?
	 */
	bool configured;

	QOBJ_FIELDS;
};

/*
 * if_cmp_func —— if_name_head 红黑树的比较函数（按接口名字典序），
 * 供 RB 树的排序与查找使用。
 */
extern int if_cmp_func(const struct interface *a, const struct interface *b);

/*
 * 接口的两棵红黑树（每棵都位于某个 VRF 内：vrf->ifaces_by_name /
 * ifaces_by_index）：
 *   if_name_head  : 以接口名 name_entry 为键。
 *   if_index_head : 以 ifindex index_entry 为键。
 * RB_PROTOTYPE 声明各自比较函数；DECLARE_QOBJ_TYPE 打开 QOBJ 调试支持。
 * 插入 / 删除一律用下面 IFNAME_RB_* / IFINDEX_RB_* 宏，勿直接调 RB_*，
 * 那些宏会做重复 / 缺失检测并上报 EC_LIB_INTERFACE 错误。
 */
RB_HEAD(if_name_head, interface);
RB_PROTOTYPE(if_name_head, interface, name_entry, if_cmp_func)
RB_HEAD(if_index_head, interface);
RB_PROTOTYPE(if_index_head, interface, index_entry, if_cmp_index_func)
DECLARE_QOBJ_TYPE(interface);

/*
 * IFNAME_RB_INSERT —— 把 ifp 按名字插入 vrf v 的名字树，返回冲突的旧节点。
 * 返回非 NULL 说明同名接口已存在（数据结构被破坏），会打印错误。
 */
#define IFNAME_RB_INSERT(v, ifp)                                                      \
	({                                                                            \
		struct interface *_iz =                                               \
			RB_INSERT(if_name_head, &v->ifaces_by_name, (ifp));           \
		if (_iz)                                                              \
			flog_err(                                                     \
				EC_LIB_INTERFACE,                                     \
				"%s(%s): corruption detected -- interface with this " \
				"name exists already in VRF %s!",                     \
				__func__, (ifp)->name, (ifp)->vrf->name);             \
		_iz;                                                                  \
	})

/*
 * IFNAME_RB_REMOVE —— 按名字从 vrf v 的名字树移除 ifp，返回被移除的节点。
 * 返回 NULL 说明该节点本不在树中（破坏），会打印错误。
 */
#define IFNAME_RB_REMOVE(v, ifp)                                                      \
	({                                                                            \
		struct interface *_iz =                                               \
			RB_REMOVE(if_name_head, &v->ifaces_by_name, (ifp));           \
		if (_iz == NULL)                                                      \
			flog_err(                                                     \
				EC_LIB_INTERFACE,                                     \
				"%s(%s): corruption detected -- interface with this " \
				"name doesn't exist in VRF %s!",                      \
				__func__, (ifp)->name, (ifp)->vrf->name);             \
		_iz;                                                                  \
	})


/*
 * IFINDEX_RB_INSERT —— 按 ifindex 插入 vrf v 的索引树，语义同 IFNAME_RB_INSERT。
 * 返回非 NULL 表示同 ifindex 冲突。
 */
#define IFINDEX_RB_INSERT(v, ifp)                                                     \
	({                                                                            \
		struct interface *_iz =                                               \
			RB_INSERT(if_index_head, &v->ifaces_by_index, (ifp));         \
		if (_iz)                                                              \
			flog_err(                                                     \
				EC_LIB_INTERFACE,                                     \
				"%s(%u): corruption detected -- interface with this " \
				"ifindex exists already in VRF %s!",                  \
				__func__, (ifp)->ifindex, (ifp)->vrf->name);          \
		_iz;                                                                  \
	})

/*
 * IFINDEX_RB_REMOVE —— 按 ifindex 从 vrf v 的索引树移除 ifp，返回被移除的节点。
 */
#define IFINDEX_RB_REMOVE(v, ifp)                                                     \
	({                                                                            \
		struct interface *_iz =                                               \
			RB_REMOVE(if_index_head, &v->ifaces_by_index, (ifp));         \
		if (_iz == NULL)                                                      \
			flog_err(                                                     \
				EC_LIB_INTERFACE,                                     \
				"%s(%u): corruption detected -- interface with this " \
				"ifindex doesn't exist in VRF %s!",                   \
				__func__, (ifp)->ifindex, (ifp)->vrf->name);          \
		_iz;                                                                  \
	})

/*
 * FOR_ALL_INTERFACES —— 遍历某个 VRF 的所有接口（按名字树顺序）。
 * 用法：FOR_ALL_INTERFACES (vrf, ifp) { ... }；vrf 为空时整段跳过。
 * 注意它由「if + RB_FOREACH」两条语句组成，与 else 搭配时留意悬挂 else 问题。
 */
#define FOR_ALL_INTERFACES(vrf, ifp)                                           \
	if (vrf)                                                               \
		RB_FOREACH (ifp, if_name_head, &vrf->ifaces_by_name)

/* called from the library code whenever interfaces are created/deleted
 * note: interfaces may not be fully realized at that point; also they
 * may not exist in the system (ifindex = IFINDEX_INTERNAL)
 *
 * priority values are important here, daemons should be at 0 while modules
 * can use 1000+ so they run after the daemon has initialised daemon-specific
 * interface data
 */
/*
 * if_add / if_del 钩子：库代码在接口创建 / 删除时调用。注意此时接口可能尚未
 * 完全就绪，甚至内核里并不存在（ifindex 可能为 IFINDEX_INTERNAL）。
 * 优先级很关键：守护进程用 0，模块用 1000+，以保证模块在守护进程初始化完
 * 守护进程私有的接口数据之后再执行。
 */
DECLARE_HOOK(if_add, (struct interface *ifp), (ifp));
DECLARE_KOOH(if_del, (struct interface *ifp), (ifp));

/* called (in daemons) when ZAPI tells us the interface actually exists
 * (ifindex != IFINDEX_INTERNAL)
 *
 * WARNING: these 2 hooks NEVER CALLED inside zebra!
 */
/*
 * if_real / if_unreal 钩子：在守护进程中，当 ZAPI 通知「接口真正存在」
 * （ifindex != IFINDEX_INTERNAL）或真正消失时调用。
 * 警告：在 zebra 内部这两个钩子不会被调用。
 */
DECLARE_HOOK(if_real, (struct interface *ifp), (ifp));
DECLARE_KOOH(if_unreal, (struct interface *ifp), (ifp));

/* called (in daemons) on state changes on interfaces.  Whether this is admin
 * state (= pure config) or carrier state (= hardware link plugged) depends on
 * zebra's "link-detect" configuration.  By default, it's carrier state, so
 * this won't happen until the interface actually has a link.
 *
 * WARNING: these 2 hooks NEVER CALLED inside zebra!
 */
/*
 * if_up / if_down 钩子：接口状态变化时调用（守护进程内）。
 * 触发的是管理态还是载波态取决于 zebra 的 link-detect 配置，默认是载波态，
 * 即只有真正有链路时才会发生。
 * 警告：在 zebra 内部这两个钩子不会被调用。
 */
DECLARE_HOOK(if_up, (struct interface *ifp), (ifp));
DECLARE_KOOH(if_down, (struct interface *ifp), (ifp));


/*
 * METRIC_MAX —— 度量值上限哨兵（全 1）。connected 路由的 metric 若为此值，
 * 则回退使用所属 struct interface 的 metric。
 */
#define METRIC_MAX (~0)

/*
 * struct connected —— 接口上的一个协议地址（直连地址）条目。
 * item 是它在接口 if_connected 链表里的挂接节点；ifp 指回所属接口。
 * conf 位标记配置态（见 ZEBRA_IFC_*），flags 位标记地址属性（见 ZEBRA_IFA_*）。
 * address 是网络地址；destination 为对端地址（仅当 ZEBRA_IFA_PEER 置位）。
 * label 是 Linux 上的地址标签；metric 用于设置该直连路由的开销。
 */
/* Connected address structure. */
struct connected {
	struct if_connected_item item;

	/* Attached interface. */
	struct interface *ifp;

	/* Flags for configuration. */
	uint8_t conf;
#define ZEBRA_IFC_REAL         (1 << 0)
#define ZEBRA_IFC_CONFIGURED   (1 << 1)
#define ZEBRA_IFC_QUEUED       (1 << 2)
#define ZEBRA_IFC_DOWN         (1 << 3)
	/*
	   The ZEBRA_IFC_REAL flag should be set if and only if this address
	   exists in the kernel and is actually usable. (A case where it exists
	   but is not yet usable would be IPv6 with DAD)
	   The ZEBRA_IFC_CONFIGURED flag should be set if and only if this
	   address was configured by the user from inside frr.
	   The ZEBRA_IFC_QUEUED flag should be set if and only if the address
	   exists in the kernel. It may and should be set although the
	   address might not be usable yet. (compare with ZEBRA_IFC_REAL)
	   The ZEBRA_IFC_DOWN flag is used to record that an address is
	   present, but down/unavailable.
	 */

	/* Flags for connected address. */
	uint8_t flags;
#define ZEBRA_IFA_SECONDARY    (1 << 0)
#define ZEBRA_IFA_PEER         (1 << 1)
#define ZEBRA_IFA_UNNUMBERED   (1 << 2)
#define ZEBRA_IFA_NOPREFIXROUTE (1 << 3)

	/* N.B. the ZEBRA_IFA_PEER flag should be set if and only if
	   a peer address has been configured.  If this flag is set,
	   the destination field must contain the peer address.
	 */

	/* Address of connected network. */
	struct prefix *address;

	/* Peer address, if ZEBRA_IFA_PEER is set, otherwise NULL */
	struct prefix *destination;

	/* Label for Linux 2.2.X and upper. */
	char *label;

	/*
	 * Used for setting the connected route's cost. If the metric
	 * here is set to METRIC_MAX the connected route falls back to
	 * "struct interface"
	 */
	uint32_t metric;
};

/*
 * 实例化 if_connected 链表：元素类型 struct connected，挂接字段 item。
 */
DECLARE_DLIST(if_connected, struct connected, item);

/*
 * struct nbr_connected —— 邻居直连地址条目（收到邻居通告后记录）。
 * ifp 指回接口，address 是邻居地址。
 */
/* Nbr Connected address structure. */
struct nbr_connected {
	/* Attached interface. */
	struct interface *ifp;

	/* Address of connected network. */
	struct prefix *address;
};

/* Does the destination field contain a peer address? */
/*
 * CONNECTED_PEER(C)   : 该直连地址是否带对端地址（ZEBRA_IFA_PEER）。
 * CONNECTED_PREFIX(C) : 要放进 RIB 的前缀（有对端取 destination，否则取 address）。
 * CONNECTED_ID(C)     : 用于唯一标识该地址的前缀；本地地址与对端同段时优先取对端。
 */
#define CONNECTED_PEER(C) CHECK_FLAG((C)->flags, ZEBRA_IFA_PEER)

/* Prefix to insert into the RIB */
#define CONNECTED_PREFIX(C)                                                    \
	(CONNECTED_PEER(C) ? (C)->destination : (C)->address)

/* Identifying address.  We guess that if there's a peer address, but the
   local address is in the same prefix, then the local address may be unique. */
#define CONNECTED_ID(C)                                                        \
	((CONNECTED_PEER(C) && !prefix_match((C)->destination, (C)->address))  \
		 ? (C)->destination                                            \
		 : (C)->address)

/* There are some interface flags which are only supported by some
   operating system. */

/*
 * 下列 IFF_* 是只有部分操作系统才有的接口标志；在不支持的系统上统一定义为 0，
 * 以便跨平台代码可以无条件引用它们。
 */
#ifndef IFF_NOTRAILERS
#define IFF_NOTRAILERS 0x0
#endif /* IFF_NOTRAILERS */
#ifndef IFF_OACTIVE
#define IFF_OACTIVE 0x0
#endif /* IFF_OACTIVE */
#ifndef IFF_SIMPLEX
#define IFF_SIMPLEX 0x0
#endif /* IFF_SIMPLEX */
#ifndef IFF_LINK0
#define IFF_LINK0 0x0
#endif /* IFF_LINK0 */
#ifndef IFF_LINK1
#define IFF_LINK1 0x0
#endif /* IFF_LINK1 */
#ifndef IFF_LINK2
#define IFF_LINK2 0x0
#endif /* IFF_LINK2 */
#ifndef IFF_NOXMIT
#define IFF_NOXMIT 0x0
#endif /* IFF_NOXMIT */
#ifndef IFF_NORTEXCH
#define IFF_NORTEXCH 0x0
#endif /* IFF_NORTEXCH */
#ifndef IFF_IPV4
#define IFF_IPV4 0x0
#endif /* IFF_IPV4 */
#ifndef IFF_IPV6
#define IFF_IPV6 0x0
#endif /* IFF_IPV6 */
#ifndef IFF_VIRTUAL
#define IFF_VIRTUAL 0x0
#endif /* IFF_VIRTUAL */

/* Prototypes. */
/*
 * if_cmp_name_func —— 按接口名字符串比较（供按名字排序的地方使用）。
 */
extern int if_cmp_name_func(const char *p1, const char *p2);

/*
 * Passing in VRF_UNKNOWN is a valid thing to do, unless we
 * are creating a new interface.
 *
 * This is useful for vrf route-leaking.  So more than anything
 * else think before you use VRF_UNKNOWN
 */
/*
 * if_update_to_new_vrf —— 把接口迁移到新的 VRF。
 * 注意：除非是在新建接口，否则传入 VRF_UNKNOWN 也是允许的（用于 vrf 路由泄漏），
 * 使用前请三思。
 */
extern void if_update_to_new_vrf(struct interface *ifp, vrf_id_t vrf_id);

/*
 * —— 接口查找函数族，实现见 if.c ——
 * 命名规则：带 vrf_id / vrf 参数的表示在指定 VRF 内查；by_index 按 ifindex 查，
 * by_name 按名字查；vrf 版与 id 版只是 VRF 参数形式不同。
 * if_lookup_by_index         : 按 ifindex 查接口。
 * if_vrf_lookup_by_index_next : 取「下一个」ifindex 更大的接口（用于遍历 / 分配）。
 * if_lookup_address_local    : 按地址查接口，只匹配本地地址。
 * if_lookup_address          : 按地址查直连地址条目。
 * if_lookup_prefix           : 按前缀查接口。
 * if_lookup_by_hwaddr        : 按硬件地址查接口，可能命中多个，返回个数。
 * if_address_is_local        : 内联判定地址是否属于本机（依赖上面的 local 查找）。
 */
extern struct interface *if_lookup_by_index(ifindex_t ifindex, vrf_id_t vrf_id);
extern struct interface *if_vrf_lookup_by_index_next(ifindex_t ifindex,
						     vrf_id_t vrf_id);
extern struct interface *if_lookup_address_local(const void *matchaddr,
						 int family, vrf_id_t vrf_id);
extern struct connected *if_lookup_address(const void *matchaddr, int family,
					   vrf_id_t vrf_id);
extern struct interface *if_lookup_prefix(const struct prefix *prefix,
					  vrf_id_t vrf_id);
size_t if_lookup_by_hwaddr(const uint8_t *hw_addr, size_t addrsz,
			   struct interface ***result, vrf_id_t vrf_id);

static inline bool if_address_is_local(const void *matchaddr, int family,
				       vrf_id_t vrf_id)
{
	return if_lookup_address_local(matchaddr, family, vrf_id) != NULL;
}

struct vrf;
/*
 * if_lookup_by_name_vrf : 在给定 VRF 内按名字查接口（只查不建）。
 * if_lookup_by_name     : 在给定 vrf_id 内按名字查接口（只查不建）。
 * if_get_vrf_loopback   : 取某 VRF 的环回接口。
 * if_get_by_name        : 在给定 vrf_id 内按名字取接口，不存在则创建（会建！），
 *                         vrf_name 用于创建时确定归属。
 * 提示：这是 if.c「查 / 建 / 删」的核心，务必区分 lookup（不建）与 get（会建）。
 */
extern struct interface *if_lookup_by_name_vrf(const char *name, struct vrf *vrf);
extern struct interface *if_lookup_by_name(const char *ifname, vrf_id_t vrf_id);
extern struct interface *if_get_vrf_loopback(vrf_id_t vrf_id);
extern struct interface *if_get_by_name(const char *ifname, vrf_id_t vrf_id,
					const char *vrf_name);

/*
 * if_set_index —— 设置接口的 ifindex 并把它挂到索引树上（唯一正确的改索引方式）。
 * 返回 0 表示成功。
 */
/* Sets the index and adds to index list */
extern int if_set_index(struct interface *ifp, ifindex_t ifindex);

/* Delete the interface, but do not free the structure, and leave it in the
   interface list.  It is often advisable to leave the pseudo interface
   structure because there may be configuration information attached. */
/*
 * if_delete_retain —— 删除接口但保留结构、仍留在接口表里（伪接口），
 * 以免丢失挂在它上面的配置信息。
 */
extern void if_delete_retain(struct interface *ifp);

/* Delete and free the interface structure: calls if_delete_retain and then
   deletes it from the interface list and frees the structure. */
/*
 * if_delete —— 先 if_delete_retain，再从接口表摘除并释放结构。
 * 参数是双指针，释放后会把调用方的指针置空。
 */
extern void if_delete(struct interface **ifp);

/*
 * 接口状态判定族，均返回布尔语义（非 0 为真）：
 * if_is_up               : 接口管理 / 载波是否 UP。
 * if_is_running          : 接口是否 running。
 * if_is_operative        : 接口是否可工作（综合 up 与 link-detect）。
 * if_is_no_ptm_operative : 忽略 PTM 状态的可工作判定。
 * if_is_loopback_exact   : 是否精确的环回接口。
 * if_is_vrf              : 是否 VRF 设备接口。
 * if_is_loopback         : 是否环回（bool）。
 * if_is_broadcast        : 是否广播型。
 * if_is_pointopoint      : 是否点对点型。
 * if_is_multicast        : 是否支持组播。
 */
extern int if_is_up(const struct interface *ifp);
extern int if_is_running(const struct interface *ifp);
extern int if_is_operative(const struct interface *ifp);
extern int if_is_no_ptm_operative(const struct interface *ifp);
extern int if_is_loopback_exact(const struct interface *ifp);
extern int if_is_vrf(const struct interface *ifp);
extern bool if_is_loopback(const struct interface *ifp);
extern int if_is_broadcast(const struct interface *ifp);
extern int if_is_pointopoint(const struct interface *ifp);
extern int if_is_multicast(const struct interface *ifp);
/*
 * if_terminate     : 清空某个 VRF 下的全部接口（VRF 销毁 / 进程退出时用）。
 * if_dump_all      : 打印所有接口（调试用）。
 * if_flag_dump     : 把接口 flags 位转成可读字符串。
 * if_link_type_str : 把链路层类型枚举转成可读字符串。
 */
extern void if_terminate(struct vrf *vrf);
extern void if_dump_all(void);
extern const char *if_flag_dump(unsigned long flags);
extern const char *if_link_type_str(enum zebra_link_type zlt);

/* Please use ifindex2ifname instead of if_indextoname where possible;
   ifindex2ifname uses internal interface info, whereas if_indextoname must
   make a system call. */
/*
 * ifindex2ifname —— 由 ifindex 取接口名，使用 FRR 内部信息、不产生系统调用。
 * 请优先用它而不是系统的 if_indextoname。
 */
extern const char *ifindex2ifname(ifindex_t ifindex, vrf_id_t vrf_id);

/* Please use ifname2ifindex instead of if_nametoindex where possible;
   ifname2ifindex uses internal interface info, whereas if_nametoindex must
   make a system call. */
/*
 * ifname2ifindex —— 由接口名取 ifindex，使用 FRR 内部信息、不产生系统调用。
 * 请优先用它而不是系统的 if_nametoindex。
 */
extern ifindex_t ifname2ifindex(const char *ifname, vrf_id_t vrf_id);

/* Connected address functions. */
/*
 * —— 直连地址相关 ——
 * connected_new                  : 分配一个 connected。
 * connected_free                 : 释放（双指针，释放后置空）。
 * connected_add_by_prefix        : 在接口上新增直连地址（可选对端地址）。
 * connected_delete_by_prefix     : 按前缀删除直连地址。
 * connected_lookup_prefix        : 按前缀查直连地址（模糊匹配）。
 * connected_lookup_prefix_exact  : 按前缀精确匹配查找。
 * connected_count_by_family      : 统计某地址族的直连地址个数。
 */
extern struct connected *connected_new(void);
extern void connected_free(struct connected **connected);
extern struct connected *connected_add_by_prefix(struct interface *ifp,
						 struct prefix *p,
						 struct prefix *dest);
extern struct connected *connected_delete_by_prefix(struct interface *ifp,
						    struct prefix *p);
extern struct connected *connected_lookup_prefix(struct interface *ifp,
						 const struct prefix *p);
extern struct connected *connected_lookup_prefix_exact(struct interface *ifp,
						       const struct prefix *p);
extern unsigned int connected_count_by_family(struct interface *ifp, int family);
/*
 * nbr_connected_new / nbr_connected_free : 邻居直连地址条目的分配 / 释放。
 * nbr_connected_check    : 查接口上是否已有该前缀的邻居条目，无则新建（check 兼建）。
 * connected_get_linklocal: 取接口上的 IPv6 链路本地地址条目。
 */
extern struct nbr_connected *nbr_connected_new(void);
extern void nbr_connected_free(struct nbr_connected *connected);
struct nbr_connected *nbr_connected_check(struct interface *ifp,
					  struct prefix *p);
struct connected *connected_get_linklocal(struct interface *ifp);

/* link parameters */
/*
 * —— 链路参数（TE）——
 * if_link_params_cmp    : 比较两份参数是否相等，返回 bool。
 * if_link_params_copy   : 把 src 内容拷进 dst。
 * if_link_params_get    : 接口已有 link_params 则直接返回，否则新建一个返回。
 * if_link_params_enable : 确保接口挂了 link_params（不存在则初始化）并返回。
 * if_link_params_init   : 为接口分配一个全零的 link_params。
 * if_link_params_free   : 释放接口的 link_params。
 */
bool if_link_params_cmp(struct if_link_params *iflp1,
			struct if_link_params *iflp2);
void if_link_params_copy(struct if_link_params *dst,
			 struct if_link_params *src);
struct if_link_params *if_link_params_get(struct interface *ifp);
struct if_link_params *if_link_params_enable(struct interface *ifp);
struct if_link_params *if_link_params_init(struct interface *ifp);
void if_link_params_free(struct interface *ifp);

/* Northbound. */
struct vty;
/*
 * Northbound / VTY 相关：
 * if_vty_config_start / if_vty_config_end : 进入 / 退出接口配置模式。
 * if_cmd_init         : 用给定的配置写出回调初始化接口相关 CLI。
 * if_cmd_init_default : 用默认的配置写出回调初始化接口相关 CLI。
 */
extern void if_vty_config_start(struct vty *vty, struct interface *ifp);
extern void if_vty_config_end(struct vty *vty);
extern void if_cmd_init(int (*config_write)(struct vty *));
extern void if_cmd_init_default(void);

/*
 * if_*_via_zapi —— 守护进程处理 ZAPI 事件时调用的接口封装：
 * if_new_via_zapi     : ZAPI 通知新建接口。
 * if_up_via_zapi      : ZAPI 通知接口 up。
 * if_down_via_zapi    : ZAPI 通知接口 down。
 * if_destroy_via_zapi : ZAPI 通知接口销毁。
 * 这些封装会触发对应的 hook（if_add / if_up 等）并维护状态。
 */
extern void if_new_via_zapi(struct interface *ifp);
extern void if_up_via_zapi(struct interface *ifp);
extern void if_down_via_zapi(struct interface *ifp);
extern void if_destroy_via_zapi(struct interface *ifp);

/*
 * if_update_state 系列 —— 更新接口状态并同步到 YANG state 树 / 订阅者：
 * if_update_state         : 整体状态更新。
 * if_update_state_metric  : 更新 metric。
 * if_update_state_mtu     : 更新 IPv4 MTU。
 * if_update_state_mtu6    : 更新 IPv6 MTU。
 * if_update_state_hw_addr : 更新硬件地址。
 * if_update_state_speed   : 更新速率。
 */
extern void if_update_state(struct interface *ifp);
extern void if_update_state_metric(struct interface *ifp, uint32_t metric);
extern void if_update_state_mtu(struct interface *ifp, uint mtu);
extern void if_update_state_mtu6(struct interface *ifp, uint mtu);
extern void if_update_state_hw_addr(struct interface *ifp, const uint8_t *hw_addr, uint len);
extern void if_update_state_speed(struct interface *ifp, uint32_t speed);

/*
 * if_notify_oper_changes : 全局开关，控制是否对外通知 oper 状态变化。
 * frr_interface_info     : 接口核心的 YANG 模块信息。
 * frr_interface_cli_info : 接口 CLI 相关的 YANG 模块信息。
 */
extern bool if_notify_oper_changes;
extern const struct frr_yang_module_info frr_interface_info;
extern const struct frr_yang_module_info frr_interface_cli_info;

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_IF_H */
