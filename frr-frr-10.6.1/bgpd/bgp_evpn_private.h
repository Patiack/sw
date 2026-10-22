// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP EVPN internal definitions
 * Copyright (C) 2017 Cumulus Networks, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_evpn_private.h —— EVPN 子系统的**内部**数据结构、宏与静态工具
 * ============================================================================
 * 【职责与定位】
 *   本文件只在 bgpd 内部使用(bgp_evpn.c / bgp_evpn_mh.c / bgp_evpn_vty.c 等)。
 *   它定义 EVPN 的核心内存对象与其字段布局，以及大量 inline 小工具(前缀构造、
 *   扩展团体编码、标志位查询)。对外只暴露 bgp_evpn.h。
 *
 * 【核心数据结构 / 关键概念】
 *   - struct bgpevpn：一个 EVI(此处即一个 L2 VNI / MAC-VRF)的运行时对象，
 *     是 EVPN 的“中枢”。含：VNI 号、租户 VRF、SVI、一批 VNI_FLAG_* 标志、
 *     RD(prd)、originator_ip(Type-3)、BUM 组播、Import/Export RT 列表、
 *     MAC 表(mac_table)、IP 表(ip_table)、remote_ip_hash(网关 IP 递归查表)、
 *     ES-EVI 红黑树与本地 ES 列表、zebra L2VNI 封装项、BUM 泛洪控制等。
 *   - Import RT 到 VNI / VRF 的映射：irt_node(vni_hash 里一个 RT → 多个 VNI)、
 *     vrf_irt_node(一个 RT → 多个 VRF)。这是“按 RT 导入”的索引结构。
 *   - struct bgp_evpn_info：默认 EVPN 实例的全局参数(DAD 去重、SoO、PIP 等)。
 *   - struct evpn_remote_ip：remote_ip_hash 的条目(一个 IP → 一组 MAC/IP 路径)，
 *     用于以网关 IP 作 overlay index 的递归下一跳解析。
 *   - struct vrf_route_target：L3 VRF 上 RT 的包装(带自动/通配标志)。
 *
 * 【本文件在主流程中的位置】
 *   bgp_evpn.c 定义并操作这些对象；bgp_evpn_vty.c 读写它们以支持命令；
 *   bgp_evpn_mh.c 使用 bgpevpn 里的 ES-EVI 结构做多归属。凡“EVPN 某条路由
 *   落到哪个 VNI/VRF”的问题，答案都在这里的表(ip_table/mac_table/irt)里。
 *
 * 【易错点 / 阅读建议】
 *   1) 别把 L2 VNI(bgpevpn, MAC-VRF) 与 L3 VNI(挂在 struct bgp->l3vni) 混为一谈：
 *      bgpevpn 表示二层；三层相关字段是 bgp_vrf 指针指向的那个 struct bgp。
 *   2) VNI_FLAG_* 是核心标志位(CFGD/LIVE/RD_CFGD/IMPRT_CFGD/EXPRT_CFGD/
 *      USE_TWO_LABELS/ADD)，读任何 VNI 分支前先弄清当前这些位。
 *   3) 两个 encode_*_extcomm 系列直接按字节手搓扩展团体，字段偏移是协议规定，
 *      看时对照 RFC 里的扩展团体格式。
 *   建议阅读顺序：bgpevpn → 其 inline 工具(is_vni_xxx / bgpevpn_get_xxx / link 族)
 *     → bgp_evpn_info → irt_node/vrf_irt_node → encode_*_extcomm →
 *       build_evpn_*_prefix 与 evpn_type*_copy → 末尾 extern 声明。
 * ============================================================================
 */

#ifndef _BGP_EVPN_PRIVATE_H
#define _BGP_EVPN_PRIVATE_H

#include "vxlan.h"
#include "zebra.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_ecommunity.h"

/* 【常量】RT_ADDRSTRLEN —— 格式化一条 Route Target(RT)字符串所需的最大长度。 */
#define RT_ADDRSTRLEN 28

/* 【常量】EVPN_ROUTE_PREFIXLEN —— 一个 EVPN 前缀结构(union evpn_addr)的**位**数。
 * EVPN 在 BGP 中把整条路由前缀统一按固定长度处理(不同于 IPv4 变长)，故用
 * “结构体字节数 * 8”表示统一的前缀长度。 */
/* EVPN prefix lengths. This represents the sizeof struct evpn_addr
 * in bits  */
#define EVPN_ROUTE_PREFIXLEN (sizeof(struct evpn_addr) * 8)

/* 【常量】BGP_EVPN_PREFIX_RD_LEN —— 打印 EVPN 前缀(含 RD)时 RD 缓冲区长度。 */
/* EVPN route RD buffer length */
#define BGP_EVPN_PREFIX_RD_LEN 100

/* 【常量】各类 EVPN 路由的“固定部分”字节数(用于校验/解码)：
 *   Type-1(EAD)：固定 25 字节 = RD(8) + ESI(10) + eth-tag(4) + VNI(3)。
 *   Type-4(ES)：IPv4 时 23 字节 = RD(8)+ESI(10)+ip-len(1)+ip(4)；
 *                IPv6 时 35 字节 = RD(8)+ESI(10)+ip-len(1)+ip(16)。
 * 这些常量在编解码里用于判断长度是否合法 / 分配缓冲。 */
/* packet sizes for EVPN routes */
/* Type-1 route should be 25 bytes
 *  RD (8), ESI (10), eth-tag (4), vni (3)
 */
#define BGP_EVPN_TYPE1_PSIZE 25
/* Type-4 route should be either 23 or 35 bytes
 *  RD (8), ESI (10), ip-len (1), ip (4 or 16)
 */
#define BGP_EVPN_TYPE4_V4_PSIZE 23
#define BGP_EVPN_TYPE4_V6_PSIZE 35

/* 【表】bgp_evpn_route_type_str —— EVPN 路由类型 → 可读字符串的映射表，
 * 供日志/调试打印使用。各类型对应关系：
 *   BGP_EVPN_AD_ROUTE=Ethernet Auto-Discovery(Type-1)、
 *   BGP_EVPN_MAC_IP_ROUTE=MAC/IP Advertisement(Type-2)、
 *   BGP_EVPN_IMET_ROUTE=Inclusive Multicast Ethernet Tag(Type-3, BUM)、
 *   BGP_EVPN_ES_ROUTE=Ethernet Segment(Type-4)、
 *   BGP_EVPN_IP_PREFIX_ROUTE=IP Prefix(Type-5)。末项 {0} 为结束哨兵。 */
static const struct message bgp_evpn_route_type_str[] = { { BGP_EVPN_AD_ROUTE, "AD" },
							  { BGP_EVPN_MAC_IP_ROUTE, "MACIP" },
							  { BGP_EVPN_IMET_ROUTE, "IMET" },
							  { BGP_EVPN_ES_ROUTE, "ES" },
							  { BGP_EVPN_IP_PREFIX_ROUTE, "IP-PREFIX" },
							  { 0 } };

/* 【结构】bgp_es_evi_rb_head —— 一个按 ESI 组织的红黑树，保存“本 VNI 上参与
 * 的 ES-EVI 条目”。RB_HEAD 定义树头类型，RB_PROTOTYPE 声明该树的 compare/
 * insert/remove/find 等函数。比较函数 bgp_es_evi_rb_cmp 在 bgp_evpn_mh.c。 */
RB_HEAD(bgp_es_evi_rb_head, bgp_evpn_es_evi);
RB_PROTOTYPE(bgp_es_evi_rb_head, bgp_evpn_es_evi, rb_node,
		bgp_es_evi_rb_cmp);
/* 【结构·核心】bgpevpn —— 一个 EVI(此处即一个 L2 VNI / MAC-VRF)的运行时对象。
 * EVPN 的绝大多数语义都围绕它：本端在该 VNI 学到的 MAC/IP(mac_table)、
 * 该 VNI 参与的三层(ip_table + bgp_vrf)、RD/RT、BUM 泛洪、多归属 ES-EVI 等。
 * 生命周期：由 bgp_evpn_new 创建、bgp_evpn_free 释放，存于 bgp 的 vni_hash。
 * 字段逐条见下方各注释。 */
/* 英文原文：描述 EVI 哈希表——当前仅支持 VxLAN 封装，故一个 EVI 对应一个 L2 VNI；
 * VNI 不由 BGP 创建，而是系统上别处创建后 BGP 经 zebra 得知，或预先配置待“上线”。 */
/*
 * Hash table of EVIs. Right now, the only type of EVI supported is with
 * VxLAN encapsulation, hence each EVI corresponds to a L2 VNI.
 * The VNIs are not "created" through BGP but through some other interface
 * on the system. This table stores VNIs that BGP comes to know as present
 * on the system (through interaction with zebra) as well as pre-configured
 * VNIs (which need to be defined in the system to become "live").
 */
struct bgpevpn {
	/* VNI 号(即本 EVI 的标识；也是哈希 bgp->vni_hash 的键)。 */
	vni_t vni;
	/* 本 VNI 所属租户 VRF 的 vrf_id；用于关联到三层(bgp_vrf)。 */
	vrf_id_t tenant_vrf_id;
	/* 该 VNI 的 SVI(二层网关接口)的 ifindex；0 表示无/未就绪。 */
	ifindex_t svi_ifindex;
	/* VNI 状态/配置标志位，见下面 VNI_FLAG_* 。 */
	uint32_t flags;
/* 【标志位 VNI_FLAG_* 详解】
 *   CFGD(0x1)           : VNI 由用户显式配置(configured)。
 *   LIVE(0x2)           : VNI 已“上线”(系统实际存在，可学习/转发)。
 *   RD_CFGD(0x4)        : RD 由用户手工配置(否则按规则自动派生)。
 *   IMPRT_CFGD(0x8)     : Import RT 为用户配置。
 *   EXPRT_CFGD(0x10)    : Export RT 为用户配置。
 *   USE_TWO_LABELS(0x20): Type-2 需同时携带 L2VNI 与 L3VNI 两个标签。
 *   ADD(0x40)           : L2VNI 处于添加流程中的标记。
 * 读任何依赖这些位的分支前，先明确当前位组合。 */
#define VNI_FLAG_CFGD              0x1  /* VNI is user configured */
#define VNI_FLAG_LIVE              0x2  /* VNI is "live" */
#define VNI_FLAG_RD_CFGD           0x4  /* RD is user configured. */
#define VNI_FLAG_IMPRT_CFGD        0x8  /* Import RT is user configured */
#define VNI_FLAG_EXPRT_CFGD        0x10 /* Export RT is user configured */
/* 英文：若需要，为本 VPN 同时挂 L2-VNI 与 L3-VNI(即双标签) */
/* Attach both L2-VNI and L3-VNI if needed for this VPN */
#define VNI_FLAG_USE_TWO_LABELS 0x20
#define VNI_FLAG_ADD		0x40 /* L2VNI Add */

	/* 回指该 VNI 所属的三层 VRF 实例；非 NULL 表示本 L2VNI 已关联到一个
	 * L3VNI/IP-VRF，此时 Type-2 可能需要双标签(VNI_FLAG_USE_TWO_LABELS)。
	 * 持有该 bgp 的一个引用(bgp_lock 获取)。 */
	struct bgp *bgp_vrf; /* back pointer to the vrf instance */

	/* 是否为本 VNI 通告“网关(gateway) MAC/IP”——即默认网关的 MAC/IP，
	 * 供 ARP/ND 抑制与默认网关可达使用。 */
					   /* Flag to indicate if we are
					    * advertising the g/w mac ip for
					    * this VNI*/
	uint8_t advertise_gw_macip;

	/* 是否为本 VNI 通告“子网(subnet)”——即把本端网段以 Type-5 等前缀
	 * 形式发出去。 */
	/* Flag to indicate if we are
	 * advertising subnet for this VNI */
	uint8_t advertise_subnet;

	/* 是否为本 VNI 通告 SVI 的 MAC/IP(二层网关的 MAC/IP，用于 ARP/ND 抑制)。 */
	/* Flag to indicate if we are advertising the svi mac ip for this VNI*/
	uint8_t advertise_svi_macip;

	/* 自动派生 RD 时所用的编号(当未手工配置 RD 时，用它生成唯一 RD)。 */
	/* Id for deriving the RD
	 * automatically for this VNI */
	uint16_t rd_id;

	/* 本 VNI 的 RD(Route Distinguisher，8 字节)；prd_pretty 是其可读字符串
	 * (分配在别处，便于打印)。EVPN 每条路由的 NLRI 前缀都带 RD。 */
	/* RD for this VNI. */
	struct prefix_rd prd;
	char *prd_pretty;

	/* Type-3(IMET)中的 originator IP 字段：本端在该 VNI 上用于 BUM/IMET
	 * 的源地址(通常是 VTEP IP)。 */
	/* Route type 3 field */
	struct ipaddr originator_ip;

	/* BUM(广播/未知单播/组播)泛洪使用的组播组地址(如 PIM-SM MDT group)。 */
	/* PIM-SM MDT group for BUM flooding */
	struct in_addr mcast_grp;

	/* Import/Export Route Target 列表(struct ecommunity * 的 list)。
	 * import_rtl 决定哪些对端路由能被导入本 VNI；export_rtl 附加到本端
	 * 发出的路由上，供对端按 RT 导入。 */
	/* Import and Export RTs. */
	struct list *import_rtl;
	struct list *export_rtl;

	/* 以网关 IP 作 overlay index 的 EVPN 路由需做递归查表，而该需要本 VNI 中
	 * 存在对应网关 IP 的远端 MAC/IP 条目。remote_ip_hash 维护“远端 MAC/IP 路由
	 * 收到的地址 → 其路径列表”的哈希(键值类型 struct evpn_remote_ip)，用于在
	 * 本 EVI 内高效完成网关 IP 的递归下一跳解析。 */
	/*
	 * EVPN route that uses gateway IP overlay index as its nexthop
	 * needs to do a recursive lookup.
	 * A remote MAC/IP entry should be present for the gateway IP.
	 * Maintain a hash of the addresses received via remote MAC/IP routes
	 * for efficient gateway IP recursive lookup in this EVI
	 */
	struct hash *remote_ip_hash;

	/* 本 VNI 的两张 EVPN 路由表：ip_table 存 Type-2 带 IP、Type-5 等 IP 相关；
	 * mac_table 以 MAC 为键存 Type-2。二者共同承载该 VNI 的 EVPN 路由。 */
	/* Route tables for EVPN routes for
	 * this VNI. */
	struct bgp_table *ip_table;
	struct bgp_table *mac_table;

	/* 本 VNI 参与的 ES-EVI 红黑树(按 ESI 组织)，多归属相关，实现见 bgp_evpn_mh.c。 */
	/* RB tree of ES-EVIs */
	struct bgp_es_evi_rb_head es_evi_rb_tree;

	/* 本 VNI 上的本地 ES 列表。 */
	/* List of local ESs */
	struct list *local_es_evi_list;

	/* 来自 zebra 的 L2 VNI 封装项(接口/桥等系统信息)，随该 VNI 一并维护。 */
	struct zebra_l2_vni_item zl2vni;

	/* BUM 泛洪控制模式(如 PIM-SM、静态/组播等)，见 enum vxlan_flood_control。 */
	enum vxlan_flood_control vxlan_flood_ctrl;

	/* QOBJ 对象字段(用于统一的对象生命周期/调试设施，见 lib/qobj.h)。 */
	QOBJ_FIELDS;
};

/* 为 bgpevpn 登记 QOBJ 类型(统一对象生命周期/调试设施)。 */
DECLARE_QOBJ_TYPE(bgpevpn);

/* 以 zl2vni 成员把 bgpevpn 组织成链表类型 zebra_l2_vni，便于遍历 L2 VNI。 */
DECLARE_LIST(zebra_l2_vni, struct bgpevpn, zl2vni);

/* 【结构】irt_node —— Import RT → VNI 的映射项(存于 bgp 的 vni_hash)。
 * 语义：一个 Import RT 可能被多个 VNI 使用；收到带该 RT 的路由即应导入到这些
 * VNI。故用“RT + 引用它的 VNI 列表”表示。键为 rt，值为 vnis。 */
/* 英文原文：所有 VNI 的 Import RT 维护在一个哈希表里，每个 RT 链接到所有会
 * 导入匹配该 RT 的路由的 VNI。 */
/* Mapping of Import RT to VNIs.
 * The Import RTs of all VNIs are maintained in a hash table with each
 * RT linking to all VNIs that will import routes matching this RT.
 */
struct irt_node {
	/* 该映射的 RT 值(哈希键)。 */
	/* RT */
	struct ecommunity_val rt;

	/* 引用此 RT 的 VNI 列表(struct list 元素为 struct bgpevpn *)。 */
	/* List of VNIs importing routes matching this RT. */
	struct list *vnis;
};

/* 【结构】vrf_irt_node —— Import RT → VRF 的映射项(三层用)。
 * 与 irt_node 同构，只是匹配到的实体是 VRF(IP-VRF，即 struct bgp *)。 */
/* 英文原文：所有 VRF 的 Import RT 也维护在哈希表里，每个 RT 链接到所有会导入
 * 匹配该 RT 的路由的 VRF。 */
/* Mapping of Import RT to VRFs.
 * The Import RTs of all VRFss are maintained in a hash table with each
 * RT linking to all VRFs that will import routes matching this RT.
 */
struct vrf_irt_node {
	/* 该映射的 RT 值(哈希键)。 */
	/* RT */
	struct ecommunity_val rt;

	/* 引用此 RT 的 VRF 列表(struct list 元素为 struct bgp *)。 */
	/* List of VNIs importing routes matching this RT. */
	struct list *vrfs;
};


/* 【常量】RT 的处理类型：某 RT 作为 Import / Export / 两者兼有。
 * 用于在批量变更 RT 时指示按哪个方向处理。 */
#define RT_TYPE_IMPORT 1
#define RT_TYPE_EXPORT 2
#define RT_TYPE_BOTH   3

/* 【常量】DAD(Duplicate Address Detection，重复地址检测)默认参数，
 * 对应 RFC7432 的 MAC 移动性去重：
 *   DEFAULT_TIME=180s        : 检测时间窗口(M)。
 *   DEFAULT_MAX_MOVES=5      : 窗口内允许的最大移动次数(N)，超过判为重复。
 *   DEFAULT_AUTO_RECOVERY_TIME=1800s : 冻结后的自动恢复时间。
 * 这些可被用户通过命令覆盖(见 bgp_evpn_vty.c)。 */
#define EVPN_DAD_DEFAULT_TIME 180 /* secs */
#define EVPN_DAD_DEFAULT_MAX_MOVES 5 /* default from RFC 7432 */
#define EVPN_DAD_DEFAULT_AUTO_RECOVERY_TIME 1800 /* secs */

/* 【结构】bgp_evpn_info —— 默认 EVPN 实例的全局参数(挂于 bgp->evpn_info)。
 * 汇总了全局性的 EVPN 行为开关：DAD 去重、SVI MAC/IP 通告、MAC-VRF SoO、
 * 以及 PIP(Proxy IP，代理 IP / 分布式任播网关)相关参数。逐字段见下。 */
struct bgp_evpn_info {
	/* 是否启用重复地址检测(DAD, MAC 移动性去重)。 */
	/* enable disable dup detect */
	bool dup_addr_detect;

	/* DAD 检测时间窗口(M，秒)，见 EVPN_DAD_DEFAULT_TIME。 */
	/* Detection time(M) */
	int dad_time;
	/* DAD 窗口内允许的最大移动次数(N)，超过判为重复。 */
	/* Detection max moves(N) */
	uint32_t dad_max_moves;
	/* 是否“永久冻结”(检测到重复后一直冻结，不自动恢复)。 */
	/* Permanent freeze */
	bool dad_freeze;
	/* 冻结后的自动恢复时间(秒)，见 EVPN_DAD_DEFAULT_AUTO_RECOVERY_TIME。 */
	/* Recovery time */
	uint32_t dad_freeze_time;

	/* 是否通告 SVI 的 MAC/IP(Type-2)，全局开关。 */
	/* EVPN enable - advertise svi macip routes */
	int advertise_svi_macip;

	/* MAC-VRF Site-of-Origin(SoO)扩展团体：
	 *  - 附加到所有从 L2VNI 导出的路由；
	 *  - 携带匹配 SoO 的 Type-2/3 路由不会被导入到 L2VNI(防环)；
	 *  - 携带匹配 SoO 的 Type-2/5 路由不会被导入到 L3VNI。
	 * 这是多归属/同源环路规避的关键机制。 */
	/* MAC-VRF Site-of-Origin
	 * - added to all routes exported from L2VNI
	 * - Type-2/3 routes with matching SoO not imported into L2VNI
	 * - Type-2/5 routes with matching SoO not imported into L3VNI
	 */
	struct ecommunity *soo;

	/* PIP(Proxy IP / 任播网关)功能开关。 */
	/* PIP feature knob */
	bool advertise_pip;
	/* PIP 使用的 IP(系统 IP)，以及其静态配置值与从 zebra 学到的值。 */
	/* PIP IP (sys ip) */
	struct ipaddr pip_ip;
	struct ipaddr pip_ip_static;
	/* PIP 使用的 MAC(系统 MAC)，同样含静态配置值与从 zebra 学到的值。 */
	/* PIP MAC (sys MAC) */
	struct ethaddr pip_rmac;
	struct ethaddr pip_rmac_static;
	struct ethaddr pip_rmac_zebra;
	/* 该 PIP MAC 是否为任播(anycast)。 */
	bool is_anycast_mac;
};

/* 【结构】evpn_remote_ip —— remote_ip_hash 的一个条目。
 *   addr           : 远端 Type-2 路由里出现的 IP(网关 IP)哈希键。
 *   macip_path_list: 携带该 IP 的 MAC/IP 路径列表，用于递归解析/更新。 */
/* 英文原文：本结构定义 remote_ip_hash 里的一个条目 */
/* This structure defines an entry in remote_ip_hash */
struct evpn_remote_ip {
	/* 远端学到的 IP 地址(哈希键)。 */
	struct ipaddr addr;
	/* 携带此 IP 的 MAC/IP 路径列表(gateway-IP 递归解析用)。 */
	struct list *macip_path_list;
};

/* 【结构】vrf_route_target —— L3 VRF 上单个 RT 的包装(带自动/通配标志)。
 * flags 说明该 RT 的来源/匹配方式(见 BGP_VRF_RT_*)，ecom 是实际 RT 值。 */
/* 英文原文：L3 RT 的包装结构 */
/*
 * Wrapper struct for l3 RT's
 */
struct vrf_route_target {
	/* 按配置决定 RT 如何被处理(AUTO=自动派生，WILD=通配匹配)。 */
	/* flags based on config to determine how RTs are handled */
	uint8_t flags;
#define BGP_VRF_RT_AUTO (1 << 0)
#define BGP_VRF_RT_WILD (1 << 1)

	/* 实际 RT 扩展团体值。 */
	struct ecommunity *ecom;
};

/* 【函数】is_vrf_rd_configured —— 该 VRF 的 RD 是否被用户手工配置
 * (查 vrf_flags 的 BGP_VRF_RD_CFGD 位)。 */
static inline int is_vrf_rd_configured(struct bgp *bgp_vrf)
{
	return (CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_RD_CFGD));
}

/* 【函数】bgp_evpn_vrf_rd_matches_existing —— 比较给定 RD 与该 VRF 现有 RD
 * (vrf_prd)是否相同(按 ECOMMUNITY_SIZE=8 字节 memcmp)。返回 0 表示相同。 */
static inline int bgp_evpn_vrf_rd_matches_existing(struct bgp *bgp_vrf,
						   struct prefix_rd *prd)
{
	return (memcmp(&bgp_vrf->vrf_prd.val, prd->val, ECOMMUNITY_SIZE) == 0);
}

/* 【函数】bgpevpn_get_l3vni —— 取该 VNI 关联的 L3VNI(来自 bgp_vrf->l3vni)；
 * 未关联 VRF 时返回 0。 */
static inline vni_t bgpevpn_get_l3vni(struct bgpevpn *vpn)
{
	return vpn->bgp_vrf ? vpn->bgp_vrf->l3vni : 0;
}

/* 【函数】bgpevpn_get_rmac —— 取该 VNI 所属 VRF 的路由器 MAC(rmac)写入出参；
 * 先把出参清零，未关联 VRF 则保持全 0。 */
static inline void bgpevpn_get_rmac(struct bgpevpn *vpn, struct ethaddr *rmac)
{
	memset(rmac, 0, sizeof(struct ethaddr));
	if (!vpn->bgp_vrf)
		return;
	memcpy(rmac, &vpn->bgp_vrf->rmac, sizeof(struct ethaddr));
}

/* 【函数】bgpevpn_get_vrf_export_rtl —— 取该 VNI 所属 VRF 的 Export RT 列表；
 * 未关联 VRF 返回 NULL。 */
static inline struct list *bgpevpn_get_vrf_export_rtl(struct bgpevpn *vpn)
{
	if (!vpn->bgp_vrf)
		return NULL;

	return vpn->bgp_vrf->vrf_export_rtl;
}

/* 【函数】bgpevpn_get_vrf_import_rtl —— 取该 VNI 所属 VRF 的 Import RT 列表；
 * 未关联 VRF 返回 NULL。 */
static inline struct list *bgpevpn_get_vrf_import_rtl(struct bgpevpn *vpn)
{
	if (!vpn->bgp_vrf)
		return NULL;

	return vpn->bgp_vrf->vrf_import_rtl;
}

/* 【函数声明】对某 VNI 上所有 ES-EVI 持有的 VRF 引用做 加/减(见 bgp_evpn_mh.c)。 */
extern void bgp_evpn_es_evi_vrf_ref(struct bgpevpn *vpn);
extern void bgp_evpn_es_evi_vrf_deref(struct bgpevpn *vpn);

/* 【函数】bgpevpn_unlink_from_l3vni —— 把该 L2VNI 从它关联的 L3VNI/VRF 上摘除。
 * 步骤：若无关联则直接返回；清 USE_TWO_LABELS 位；从 bgp_vrf->l2vnis 删本节点；
 * 递减 ES-EVI 的 VRF 引用；最后 bgp_unlock 释放对 vrf 实例的引用并把 bgp_vrf
 * 置 NULL。 */
static inline void bgpevpn_unlink_from_l3vni(struct bgpevpn *vpn)
{
	/* bail if vpn is not associated to bgp_vrf */
	if (!vpn->bgp_vrf)
		return;

	UNSET_FLAG(vpn->flags, VNI_FLAG_USE_TWO_LABELS);
	listnode_delete(vpn->bgp_vrf->l2vnis, vpn);

	bgp_evpn_es_evi_vrf_deref(vpn);

	/* remove the backpointer to the vrf instance */
	bgp_unlock(vpn->bgp_vrf);
	vpn->bgp_vrf = NULL;
}

/* 【函数】bgpevpn_link_to_l3vni —— 尝试把该 L2VNI 关联到其租户 VRF 的 L3VNI。
 * 步骤：已关联则返回；按 tenant_vrf_id 查 bgp 实例，无则返回；该 VRF 无 l3vni
 * 则返回；否则 bgp_lock 后把 vpn 挂入 bgp_vrf->l2vnis；若该 VRF 不是
 * “仅三层前缀(PREFIX_ROUTES_ONLY)”，置 USE_TWO_LABELS(双标签)；最后对 ES-EVI
 * 增加 VRF 引用。
 * 易错点：这里是“尽力关联”，条件不满足静默返回，不报错。 */
static inline void bgpevpn_link_to_l3vni(struct bgpevpn *vpn)
{
	struct bgp *bgp_vrf = NULL;

	/* bail if vpn is already associated to vrf */
	if (vpn->bgp_vrf)
		return;

	/* bail if VRF still doesn't exist */
	bgp_vrf = bgp_lookup_by_vrf_id(vpn->tenant_vrf_id);
	if (!bgp_vrf)
		return;

	/* or if there is no l3vni */
	if (!bgp_vrf->l3vni)
		return;

	/* associate the vpn to the bgp_vrf instance */
	vpn->bgp_vrf = bgp_lock(bgp_vrf);
	listnode_add_sort(bgp_vrf->l2vnis, vpn);

	/*
	 * check if we are advertising two labels for this vpn
	 */
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY))
		SET_FLAG(vpn->flags, VNI_FLAG_USE_TWO_LABELS);

	bgp_evpn_es_evi_vrf_ref(vpn);
}

/* 【函数】is_vni_configured —— 该 VNI 是否被用户配置(VNI_FLAG_CFGD)。 */
static inline int is_vni_configured(struct bgpevpn *vpn)
{
	return (CHECK_FLAG(vpn->flags, VNI_FLAG_CFGD));
}

/* 【函数】is_vni_live —— 该 VNI 是否已上线(VNI_FLAG_LIVE)。 */
static inline int is_vni_live(struct bgpevpn *vpn)
{
	return (CHECK_FLAG(vpn->flags, VNI_FLAG_LIVE));
}

/* 【函数】is_l3vni_live —— 该 VRF 的 L3VNI 是否已就绪：需同时有 l3vni 号
 * 与 l3vni_svi_ifindex(接口已就绪)。 */
static inline int is_l3vni_live(struct bgp *bgp_vrf)
{
	return (bgp_vrf->l3vni && bgp_vrf->l3vni_svi_ifindex);
}

/* 【函数】is_rd_configured —— 该 VNI 的 RD 是否被用户配置(VNI_FLAG_RD_CFGD)。 */
static inline int is_rd_configured(struct bgpevpn *vpn)
{
	return (CHECK_FLAG(vpn->flags, VNI_FLAG_RD_CFGD));
}

/* 【函数】bgp_evpn_rd_matches_existing —— 比较给定 RD 与该 VNI 现有 RD
 * (vpn->prd)是否相同(8 字节 memcmp)；返回 0 表示相同。 */
static inline int bgp_evpn_rd_matches_existing(struct bgpevpn *vpn,
					       struct prefix_rd *prd)
{
	return (memcmp(&vpn->prd.val, prd->val, ECOMMUNITY_SIZE) == 0);
}

/* 【函数】is_import_rt_configured —— 该 VNI 的 Import RT 是否被用户配置。 */
static inline int is_import_rt_configured(struct bgpevpn *vpn)
{
	return (CHECK_FLAG(vpn->flags, VNI_FLAG_IMPRT_CFGD));
}

/* 【函数】is_export_rt_configured —— 该 VNI 的 Export RT 是否被用户配置。 */
static inline int is_export_rt_configured(struct bgpevpn *vpn)
{
	return (CHECK_FLAG(vpn->flags, VNI_FLAG_EXPRT_CFGD));
}

/* ===== EVPN 扩展团体的“手搓字节”编码器 =====
 * 这一组函数把各种 EVPN 专用扩展团体写进 struct ecommunity_val(8 字节)。
 * val[0] 通常是 ECOMMUNITY_ENCODE_EVPN(或 OPAQUE)，val[1] 是子类型，
 * 之后按各子类型协议规定的布局填充。看时对照 RFC7432 / RFC9136 的团体格式。 */
/* 【函数】encode_es_rt_extcomm —— ES-Import Route Target(ES-Import RT)：
 * 类型 EVPN / 子类型 ES_IMPORT_RT，值 = 6 字节 MAC。用于让同 ES 的 PE 互相导入。 */
static inline void encode_es_rt_extcomm(struct ecommunity_val *eval,
					struct ethaddr *mac)
{
	memset(eval, 0, sizeof(struct ecommunity_val));
	eval->val[0] = ECOMMUNITY_ENCODE_EVPN;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_ES_IMPORT_RT;
	memcpy(&eval->val[2], mac, ETH_ALEN);
}

/* 【函数】encode_df_elect_extcomm —— DF Election 扩展团体(RFC8584 风格)：
 * 类型 EVPN / 子类型 DF_ELECTION，val[2]=算法(EVPN_MH_DF_ALG_PREF)，
 * val[6..7]=大端偏好值 pref。多归属 DF 选举用。 */
static inline void encode_df_elect_extcomm(struct ecommunity_val *eval,
					   uint16_t pref)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_EVPN;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_DF_ELECTION;
	eval->val[2] = EVPN_MH_DF_ALG_PREF;
	eval->val[6] = (pref >> 8) & 0xff;
	eval->val[7] = pref & 0xff;
}

/* 【函数】encode_esi_label_extcomm —— ESI Label(A-A 别名标签)扩展团体：
 * 类型 EVPN / 子类型 ESI_LABEL；single_active 为真时置 val[2] 的 bit0
 * (表示 single-active 模式)。 */
static inline void encode_esi_label_extcomm(struct ecommunity_val *eval,
					bool single_active)
{
	memset(eval, 0, sizeof(struct ecommunity_val));
	eval->val[0] = ECOMMUNITY_ENCODE_EVPN;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_ESI_LABEL;
	if (single_active)
		eval->val[2] |= (1 << 0);
}

/* 【函数】encode_rmac_extcomm —— Router's MAC 扩展团体(RFC9135)：
 * 类型 EVPN / 子类型 ROUTERMAC，值 = 6 字节路由器 MAC。为 Type-2/5 提供
 * 作为下一跳使用的路由器 MAC(overlay index 用)。 */
static inline void encode_rmac_extcomm(struct ecommunity_val *eval,
				       struct ethaddr *rmac)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_EVPN;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_ROUTERMAC;
	memcpy(&eval->val[2], rmac, ETH_ALEN);
}

/* 【函数】encode_default_gw_extcomm —— 默认网关扩展团体：类型为 OPAQUE(注意
 * 不是 EVPN)，子类型 DEF_GW，无额外值。表示“默认网关”语义。 */
static inline void encode_default_gw_extcomm(struct ecommunity_val *eval)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_OPAQUE;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_DEF_GW;
}

/* 【函数】encode_mac_mobility_extcomm —— MAC Mobility 扩展团体(RFC7432)：
 * 类型 EVPN / 子类型 MACMOBILITY；static_mac 为真则 val[2] 置 STICKY 标志；
 * val[4..7] 为大端序列号 seq(用于 MAC 移动性比较，配合 DAD)。 */
static inline void encode_mac_mobility_extcomm(int static_mac, uint32_t seq,
					       struct ecommunity_val *eval)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_EVPN;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY;
	if (static_mac)
		eval->val[2] = ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY_FLAG_STICKY;
	eval->val[4] = (seq >> 24) & 0xff;
	eval->val[5] = (seq >> 16) & 0xff;
	eval->val[6] = (seq >> 8) & 0xff;
	eval->val[7] = seq & 0xff;
}

/* 【函数】encode_na_flag_extcomm —— ND(IPv6 邻居发现)相关标志扩展团体：
 * 类型 EVPN / 子类型 ND；na_flag 置 ROUTER_FLAG 位，proxy 置 PROXY_FLAG 位。
 * (IPv6 的代理 ND 抑制使用。) */
static inline void encode_na_flag_extcomm(struct ecommunity_val *eval,
					  bool na_flag, bool proxy)
{
	memset(eval, 0, sizeof(*eval));
	eval->val[0] = ECOMMUNITY_ENCODE_EVPN;
	eval->val[1] = ECOMMUNITY_EVPN_SUBTYPE_ND;
	if (na_flag)
		eval->val[2] |= ECOMMUNITY_EVPN_SUBTYPE_ND_ROUTER_FLAG;
	if (proxy)
		eval->val[2] |= ECOMMUNITY_EVPN_SUBTYPE_PROXY_FLAG;
}

/* ===== EVPN 前缀构造/拆解工具(struct prefix_evpn <-> struct prefix / 各类型) ===== */
/* 【函数】ip_prefix_from_type5_prefix —— 从 Type-5 前缀提取出普通的 IP 前缀
 * (family/prefixlen/地址)，前缀长度取 ip_prefix_length。 */
static inline void ip_prefix_from_type5_prefix(const struct prefix_evpn *evp,
					       struct prefix *ip)
{
	memset(ip, 0, sizeof(struct prefix));
	if (is_evpn_prefix_ipaddr_v4(evp)) {
		ip->family = AF_INET;
		ip->prefixlen = evp->prefix.prefix_addr.ip_prefix_length;
		memcpy(&(ip->u.prefix4), &(evp->prefix.prefix_addr.ip.ip),
		       IPV4_MAX_BYTELEN);
	} else if (is_evpn_prefix_ipaddr_v6(evp)) {
		ip->family = AF_INET6;
		ip->prefixlen = evp->prefix.prefix_addr.ip_prefix_length;
		memcpy(&(ip->u.prefix6), &(evp->prefix.prefix_addr.ip.ip),
		       IPV6_MAX_BYTELEN);
	}
}

/* 【函数】is_evpn_prefix_default —— 该前缀是否“EVPN 默认路由”：
 * Type-5 且 ip_prefix_length==0(即 0.0.0.0/0 或 ::/0 的 Type-5)。 */
static inline bool is_evpn_prefix_default(const struct prefix *evp)
{
	if (evp->family != AF_EVPN)
		return false;

	/*
	 * EVPN default type-5 route
	 * RD:[5]:[0]:[0.0.0.0/0]/352 or RD:[5]:[0]:[::/0]/352
	 */
	if ((evp->u.prefix_evpn.route_type == BGP_EVPN_IP_PREFIX_ROUTE) &&
	    (evp->u.prefix_evpn.prefix_addr.ip_prefix_length == 0))
		return true;

	return false;
}

/* 【函数】ip_prefix_from_type2_prefix —— 从 Type-2 前缀的 IP 部分构造普通
 * IP 前缀，前缀长度取该地址族的满长(IPV4/IPV6_MAX_BITLEN，即 /32 或 /128)。 */
static inline void ip_prefix_from_type2_prefix(const struct prefix_evpn *evp,
					       struct prefix *ip)
{
	memset(ip, 0, sizeof(struct prefix));
	if (is_evpn_prefix_ipaddr_v4(evp)) {
		ip->family = AF_INET;
		ip->prefixlen = IPV4_MAX_BITLEN;
		memcpy(&(ip->u.prefix4), &(evp->prefix.macip_addr.ip.ip),
		       IPV4_MAX_BYTELEN);
	} else if (is_evpn_prefix_ipaddr_v6(evp)) {
		ip->family = AF_INET6;
		ip->prefixlen = IPV6_MAX_BITLEN;
		memcpy(&(ip->u.prefix6), &(evp->prefix.macip_addr.ip.ip),
		       IPV6_MAX_BYTELEN);
	}
}

/* 【函数】ip_prefix_from_evpn_prefix —— 按 route_type 分派：Type-2 走
 * ip_prefix_from_type2_prefix，Type-5 走 ip_prefix_from_type5_prefix。
 * 其它类型不处理(出参保持不变)。 */
static inline void ip_prefix_from_evpn_prefix(const struct prefix_evpn *evp,
					      struct prefix *ip)
{
	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
		ip_prefix_from_type2_prefix(evp, ip);
	else if (evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE)
		ip_prefix_from_type5_prefix(evp, ip);
}

/* 【函数】build_evpn_type2_prefix —— 由 MAC(必填)与 IP(可空)构造一条 Type-2
 * 前缀：family=AF_EVPN，prefixlen=EVPN_ROUTE_PREFIXLEN，route_type=MAC_IP。
 * 注意先把 ip 的 ipa_type 置 IPADDR_NONE 再整体拷贝，故当 ip 为空(IPADDR_NONE)
 * 时得到“仅 MAC”的 Type-2。 */
static inline void build_evpn_type2_prefix(struct prefix_evpn *p,
					   struct ethaddr *mac,
					   struct ipaddr *ip)
{
	memset(p, 0, sizeof(struct prefix_evpn));
	p->family = AF_EVPN;
	p->prefixlen = EVPN_ROUTE_PREFIXLEN;
	p->prefix.route_type = BGP_EVPN_MAC_IP_ROUTE;
	memcpy(&p->prefix.macip_addr.mac.octet, mac->octet, ETH_ALEN);
	p->prefix.macip_addr.ip.ipa_type = IPADDR_NONE;
	memcpy(&p->prefix.macip_addr.ip, ip, sizeof(*ip));
}

/* 【函数】build_type5_prefix_from_ip_prefix —— 由普通 IP 前缀构造 Type-5 前缀：
 * route_type=IP_PREFIX，ip_prefix_length 取原前缀长度，并填地址(按 v4/v6)。 */
static inline void
build_type5_prefix_from_ip_prefix(struct prefix_evpn *evp,
				  const struct prefix *ip_prefix)
{
	struct ipaddr ip;

	memset(&ip, 0, sizeof(struct ipaddr));
	if (ip_prefix->family == AF_INET) {
		ip.ipa_type = IPADDR_V4;
		memcpy(&ip.ipaddr_v4, &ip_prefix->u.prefix4,
		       sizeof(struct in_addr));
	} else {
		ip.ipa_type = IPADDR_V6;
		memcpy(&ip.ipaddr_v6, &ip_prefix->u.prefix6,
		       sizeof(struct in6_addr));
	}

	memset(evp, 0, sizeof(struct prefix_evpn));
	evp->family = AF_EVPN;
	evp->prefixlen = EVPN_ROUTE_PREFIXLEN;
	evp->prefix.route_type = BGP_EVPN_IP_PREFIX_ROUTE;
	evp->prefix.prefix_addr.ip_prefix_length = ip_prefix->prefixlen;
	evp->prefix.prefix_addr.ip.ipa_type = ip.ipa_type;
	memcpy(&evp->prefix.prefix_addr.ip, &ip, sizeof(struct ipaddr));
}

/* 【函数】build_evpn_type3_prefix —— 由 originator IP 构造 Type-3(IMET)前缀：
 * route_type=IMET，按 v4/v6 设 ip_prefix_length 为满长，ip=originator_ip。 */
static inline void build_evpn_type3_prefix(struct prefix_evpn *p,
					   struct ipaddr *originator_ip)
{
	memset(p, 0, sizeof(struct prefix_evpn));
	p->family = AF_EVPN;
	p->prefixlen = EVPN_ROUTE_PREFIXLEN;
	p->prefix.route_type = BGP_EVPN_IMET_ROUTE;
	if (IS_IPADDR_V4(originator_ip))
		p->prefix.imet_addr.ip_prefix_length = IPV4_MAX_BITLEN;
	else if (IS_IPADDR_V6(originator_ip))
		p->prefix.imet_addr.ip_prefix_length = IPV6_MAX_BITLEN;
	p->prefix.imet_addr.ip = *originator_ip;
}

/* 【函数】build_evpn_type4_prefix —— 由 ESI 与 originator IP 构造 Type-4(ES)前缀：
 * route_type=ES_ROUTE，按 IP 类型设 ip_prefix_length(无 IP 则 IPADDR_NONE)，
 * 填 ip 与 esi。 */
static inline void build_evpn_type4_prefix(struct prefix_evpn *p, esi_t *esi,
					   struct ipaddr originator_ip)
{
	memset(p, 0, sizeof(struct prefix_evpn));
	p->family = AF_EVPN;
	p->prefixlen = EVPN_ROUTE_PREFIXLEN;
	p->prefix.route_type = BGP_EVPN_ES_ROUTE;
	/* Set IP prefix length and address based on originator_ip type */
	if (IS_IPADDR_V4(&originator_ip))
		p->prefix.es_addr.ip_prefix_length = IPV4_MAX_BITLEN;
	else if (IS_IPADDR_V6(&originator_ip))
		p->prefix.es_addr.ip_prefix_length = IPV6_MAX_BITLEN;
	else
		p->prefix.es_addr.ip_prefix_length = IPADDR_NONE;

	p->prefix.es_addr.ip = originator_ip;
	memcpy(&p->prefix.es_addr.esi, esi, sizeof(esi_t));
}

/* 【函数】build_evpn_type1_prefix —— 由 eth_tag/ESI/originator IP 构造 Type-1(EAD)
 * 前缀：route_type=AD，eth_tag=入参，按 IP 类型填地址(无 IP 时置 IPADDR_NONE 并清零)，
 * 填 esi。 */
static inline void build_evpn_type1_prefix(struct prefix_evpn *p, uint32_t eth_tag, esi_t *esi,
					   struct ipaddr originator_ip)
{
	memset(p, 0, sizeof(struct prefix_evpn));
	p->family = AF_EVPN;
	p->prefixlen = EVPN_ROUTE_PREFIXLEN;
	p->prefix.route_type = BGP_EVPN_AD_ROUTE;
	p->prefix.ead_addr.eth_tag = eth_tag;
	/* Set IP address and type based on originator_ip */
	if (IS_IPADDR_V4(&originator_ip)) {
		SET_IPADDR_V4(&p->prefix.ead_addr.ip);
		IPV4_ADDR_COPY(&p->prefix.ead_addr.ip.ipaddr_v4, &originator_ip.ipaddr_v4);
	} else if (IS_IPADDR_V6(&originator_ip)) {
		SET_IPADDR_V6(&p->prefix.ead_addr.ip);
		IPV6_ADDR_COPY(&p->prefix.ead_addr.ip.ipaddr_v6, &originator_ip.ipaddr_v6);
	} else {
		/* IPADDR_NONE - should not happen, but handle gracefully */
		p->prefix.ead_addr.ip.ipa_type = IPADDR_NONE;
		memset(&p->prefix.ead_addr.ip.ipaddr_v4, 0,
		       sizeof(p->prefix.ead_addr.ip.ipaddr_v4));
	}
	memcpy(&p->prefix.ead_addr.esi, esi, sizeof(esi_t));
}

/* 【函数】evpn_type1_prefix_global_copy —— 由 VNI 版 Type-1 前缀构造“全局版”：
 * 先整体拷贝，再把 EAD 前缀中的 VTEP IP 清零(global 表不含 VTEP IP)，但**保留
 * ipa_type** 以维持地址族信息；同时把 frag_id 置 0。 */
static inline void evpn_type1_prefix_global_copy(struct prefix_evpn *global_p,
		const struct prefix_evpn *vni_p)
{
	memcpy(global_p, vni_p, sizeof(*global_p));
	/* EAD prefix in global table doesn't include VTEP IP - zero it out
	 * but preserve ipa_type to maintain address family information
	 */
	if (IS_IPADDR_V4(&vni_p->prefix.ead_addr.ip)) {
		global_p->prefix.ead_addr.ip.ipa_type = IPADDR_V4;
		global_p->prefix.ead_addr.ip.ipaddr_v4.s_addr = INADDR_ANY;
	} else if (IS_IPADDR_V6(&vni_p->prefix.ead_addr.ip)) {
		global_p->prefix.ead_addr.ip.ipa_type = IPADDR_V6;
		/* Use standard IPv6 "any" address (::) via IN6ADDR_ANY_INIT */
		global_p->prefix.ead_addr.ip.ipaddr_v6 = (struct in6_addr)IN6ADDR_ANY_INIT;
	} else {
		global_p->prefix.ead_addr.ip.ipa_type = IPADDR_NONE;
		/* ipa_type is IPADDR_NONE, zero everything */
		memset(&global_p->prefix.ead_addr.ip, 0, sizeof(struct ipaddr));
	}
	global_p->prefix.ead_addr.frag_id = 0;
}

/* 【函数】evpn_type1_prefix_vni_ip_copy —— 反向：由全局版 Type-1 前缀 + 属性 attr
 * 构造 VNI 版，即把 VTEP IP 从 attr 的下一跳信息里恢复出来。按 attr->mp_nexthop_len
 * 判断地址族：IPv4 nexthop 取 attr->nexthop；IPv6 nexthop 取 mp_nexthop_global；
 * 其它情况置 IPADDR_NONE。 */
/* 英文原文：全局表中的 EAD 前缀不含 VTEP-IP，故需要为 VNI 另建一份拷贝 */
/* EAD prefix in the global table doesn't include the VTEP-IP so
 * we need to create a different copy for the VNI
 */
static inline struct prefix_evpn *
evpn_type1_prefix_vni_ip_copy(struct prefix_evpn *vni_p,
			      const struct prefix_evpn *global_p,
			      const struct attr *attr)
{
	memcpy(vni_p, global_p, sizeof(*vni_p));
	/* Extract originator IP from attr based on nexthop length */
	if (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4 ||
	    attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4) {
		/* IPv4 nexthop */
		SET_IPADDR_V4(&vni_p->prefix.ead_addr.ip);
		IPV4_ADDR_COPY(&vni_p->prefix.ead_addr.ip.ipaddr_v4, &attr->nexthop);
	} else if (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL ||
		   attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL ||
		   attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL ||
		   attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL) {
		/* IPv6 nexthop - use global address */
		SET_IPADDR_V6(&vni_p->prefix.ead_addr.ip);
		IPV6_ADDR_COPY(&vni_p->prefix.ead_addr.ip.ipaddr_v6, &attr->mp_nexthop_global);
	} else {
		/* IPADDR_NONE - should not happen, but handle gracefully */
		vni_p->prefix.ead_addr.ip.ipa_type = IPADDR_NONE;
		memset(&vni_p->prefix.ead_addr.ip, 0, sizeof(vni_p->prefix.ead_addr.ip));
	}

	return vni_p;
}

/* 【函数】evpn_type2_prefix_global_copy —— 由 VNI 版 Type-2 前缀构造全局版：
 * 整体拷贝后，若给了 mac/ip 则覆盖 MAC/IP 字段。 */
static inline void evpn_type2_prefix_global_copy(
	struct prefix_evpn *global_p, const struct prefix_evpn *vni_p,
	const struct ethaddr *mac, const struct ipaddr *ip)
{
	memcpy(global_p, vni_p, sizeof(*global_p));

	if (mac)
		global_p->prefix.macip_addr.mac = *mac;

	if (ip)
		global_p->prefix.macip_addr.ip = *ip;
}

/* 【函数】evpn_type2_prefix_vni_ip_copy —— 由全局版构造“仅 IP”的 VNI 版 Type-2
 * 前缀：拷贝后把 MAC 清零(只保留 IP)。 */
static inline void
evpn_type2_prefix_vni_ip_copy(struct prefix_evpn *vni_p,
			      const struct prefix_evpn *global_p)
{
	memcpy(vni_p, global_p, sizeof(*vni_p));
	memset(&vni_p->prefix.macip_addr.mac, 0, sizeof(struct ethaddr));
}

/* 【函数】evpn_type2_prefix_vni_mac_copy —— 由全局版构造“仅 MAC”的 VNI 版 Type-2
 * 前缀：拷贝后把 IP 清零(只保留 MAC)。 */
static inline void
evpn_type2_prefix_vni_mac_copy(struct prefix_evpn *vni_p,
			       const struct prefix_evpn *global_p)
{
	memcpy(vni_p, global_p, sizeof(*vni_p));
	memset(&vni_p->prefix.macip_addr.ip, 0, sizeof(struct ipaddr));
}

/* 【函数】evpn_type2_path_info_get_mac —— 取某 Type-2 路径关联的 MAC(存于
 * extra->evpn->vni_info.mac)。断言 extra 与其 evpn 子结构存在，故调用前须确保。 */
/* Get MAC of path_info prefix */
static inline struct ethaddr *
evpn_type2_path_info_get_mac(const struct bgp_path_info *local_pi)
{
	assert(local_pi->extra && local_pi->extra->evpn);
	return &local_pi->extra->evpn->vni_info.mac;
}

/* 【函数】evpn_type2_path_info_get_ip —— 取某 Type-2 路径关联的 IP(存于
 * extra->evpn->vni_info.ip)。同样带断言。 */
/* Get IP of path_info prefix */
static inline struct ipaddr *
evpn_type2_path_info_get_ip(const struct bgp_path_info *local_pi)
{
	assert(local_pi->extra && local_pi->extra->evpn);
	return &local_pi->extra->evpn->vni_info.ip;
}

/* 【函数】evpn_type2_path_info_set_mac —— 设置某 Type-2 路径关联的 MAC。 */
/* Set MAC of path_info prefix */
static inline void evpn_type2_path_info_set_mac(struct bgp_path_info *local_pi,
						const struct ethaddr mac)
{
	assert(local_pi->extra && local_pi->extra->evpn);
	local_pi->extra->evpn->vni_info.mac = mac;
}

/* 【函数】evpn_type2_path_info_set_ip —— 设置某 Type-2 路径关联的 IP。 */
/* Set IP of path_info prefix */
static inline void evpn_type2_path_info_set_ip(struct bgp_path_info *local_pi,
					       const struct ipaddr ip)
{
	assert(local_pi->extra && local_pi->extra->evpn);
	local_pi->extra->evpn->vni_info.ip = ip;
}

/* 【函数】is_evpn_type2_dest_ipaddr_none —— 该 dest(须为 Type-2)携带的 IP 是否
 * 为空(仅 MAC 的 Type-2)。断言 route_type 为 MAC_IP。 */
/* Is the IP empty for the RT's dest? */
static inline bool is_evpn_type2_dest_ipaddr_none(const struct bgp_dest *dest)
{
	const struct prefix_evpn *evp =
		(const struct prefix_evpn *)bgp_dest_get_prefix(dest);

	assert(evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE);
	return is_evpn_prefix_ipaddr_none(evp);
}

/* 【函数】evpn_default_originate_set —— 该 bgp 是否配置了“默认路由发起”
 * (default-originate)：afi 为 IP 时查 IPV4 对应位，IP6 时查 IPV6 对应位。 */
static inline int evpn_default_originate_set(struct bgp *bgp, afi_t afi,
					     safi_t safi)
{
	if (afi == AFI_IP &&
	    CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
		       BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV4))
		return 1;
	else if (afi == AFI_IP6 &&
		 CHECK_FLAG(bgp->af_flags[AFI_L2VPN][SAFI_EVPN],
			    BGP_L2VPN_EVPN_DEFAULT_ORIGINATE_IPV6))
		return 1;
	return 0;
}

/* 【函数】es_get_system_mac —— 从 ESI 中提取系统 MAC：对 Type-1/Type-3 形态的
 * ESI，系统 MAC 位于 esi->val[1] 起的 6 字节。 */
static inline void es_get_system_mac(esi_t *esi,
				     struct ethaddr *mac)
{
	/*
	 * for type-1 and type-3 ESIs,
	 * the system mac starts at val[1]
	 */
	memcpy(mac, &esi->val[1], ETH_ALEN);
}

/* 【函数】bgp_evpn_is_svi_macip_enabled —— 是否开启“通告 SVI 的 MAC/IP”：
 * 全局开关(evpn_info->advertise_svi_macip)或本 VNI 级开关任一为真即可。 */
static inline bool bgp_evpn_is_svi_macip_enabled(struct bgpevpn *vpn)
{
	struct bgp *bgp_evpn = NULL;

	bgp_evpn = bgp_get_evpn();

	return (bgp_evpn->evpn_info->advertise_svi_macip ||
		vpn->advertise_svi_macip);
}

/* 【函数】bgp_evpn_is_path_local —— 该路径是否“本端静态产生”：来源为自身
 * (peer==peer_self)且 type==ZEBRA_ROUTE_BGP 且 sub_type==BGP_ROUTE_STATIC。 */
static inline bool bgp_evpn_is_path_local(struct bgp *bgp,
		struct bgp_path_info *pi)
{
	return (pi->peer == bgp->peer_self
			&& pi->type == ZEBRA_ROUTE_BGP
			&& pi->sub_type == BGP_ROUTE_STATIC);
}

/* ===== bgp_evpn.c 内部使用、但对同模块其它文件可见的函数声明 ===== */
/* 【函数】bgp_evpn_install_uninstall_default_route —— 安装(add=true)/卸载 EVPN
 * 默认路由(Type-5 默认路由)，originator 为触发者。 */
extern void bgp_evpn_install_uninstall_default_route(struct bgp *bgp_vrf, afi_t afi, safi_t safi,
						     struct bgp_path_info *originator, bool add);
/* 【函数】evpn_rt_delete_auto —— 从自动 RT 链表中删除与 vni 相关的自动 RT
 * (is_l3 指明三层还是二层)。 */
extern void evpn_rt_delete_auto(struct bgp *bgp, vni_t vni, struct list *rtl,
				bool is_l3);
/* 【函数】为 VRF 增加/删除 Export RT：configure_export_rt_for_vrf(手工增加一个)、
 * configure_export_auto_rt_for_vrf(加入自动派生 RT)、unconfigure_*(对应删除)。 */
extern void bgp_evpn_configure_export_rt_for_vrf(struct bgp *bgp_vrf,
						 struct ecommunity *ecomadd);
extern void bgp_evpn_configure_export_auto_rt_for_vrf(struct bgp *bgp_vrf);
extern void bgp_evpn_unconfigure_export_rt_for_vrf(struct bgp *bgp_vrf,
						   struct ecommunity *ecomdel);
extern void bgp_evpn_unconfigure_export_auto_rt_for_vrf(struct bgp *bgp_vrf);
/* 【函数】为 VRF 增加/删除 Import RT：configure_import_rt_for_vrf(ecomadd，
 * is_wildcard 表示通配匹配)、configure_import_auto_rt_for_vrf(自动 Import RT)、
 * unconfigure_import_*(对应删除)。 */
extern void bgp_evpn_configure_import_rt_for_vrf(struct bgp *bgp_vrf,
						 struct ecommunity *ecomadd,
						 bool is_wildcard);
extern void bgp_evpn_configure_import_auto_rt_for_vrf(struct bgp *bgp_vrf);
extern void bgp_evpn_unconfigure_import_rt_for_vrf(struct bgp *bgp_vrf,
						   struct ecommunity *ecomdel);
extern void bgp_evpn_unconfigure_import_auto_rt_for_vrf(struct bgp *bgp_vrf);
/* 【函数】bgp_evpn_handle_export_rt_change —— 某 VNI 的 Export RT 改变后，
 * 重新导出/撤销相关路由(影响发给对端的路由内容)。 */
extern int bgp_evpn_handle_export_rt_change(struct bgp *bgp,
					    struct bgpevpn *vpn);
/* 【函数】bgp_evpn_handle_autort_change —— 自动 RT(autort)配置变化后的整体处理。 */
extern void bgp_evpn_handle_autort_change(struct bgp *bgp);
/* 【函数】bgp_evpn_handle_vrf_rd_change —— VRF 的 RD 变化处理；withdraw 为真先撤销。 */
extern void bgp_evpn_handle_vrf_rd_change(struct bgp *bgp_vrf, int withdraw);
/* 【函数】bgp_evpn_handle_rd_change —— VNI 的 RD 变化处理；withdraw 为真先撤销。 */
extern void bgp_evpn_handle_rd_change(struct bgp *bgp, struct bgpevpn *vpn,
				      int withdraw);
/* 【函数】bgp_evpn_handle_global_macvrf_soo_change —— 全局 MAC-VRF SoO 变化处理。 */
void bgp_evpn_handle_global_macvrf_soo_change(struct bgp *bgp,
					      struct ecommunity *new_soo);
/* 【函数】bgp_evpn_install_routes / _uninstall_routes —— 对某 VNI 下所有 EVPN
 * 路由批量安装到内核 / 从内核卸载。 */
extern int bgp_evpn_install_routes(struct bgp *bgp, struct bgpevpn *vpn);
extern int bgp_evpn_uninstall_routes(struct bgp *bgp, struct bgpevpn *vpn);
/* 【函数】bgp_evpn_map_vrf_to_its_rts / unmap_vrf_from_its_rts —— 把 VRF 登记到
 * 其 Import RT 索引(建 vrf_irt_node)，或反向摘除。 */
extern void bgp_evpn_map_vrf_to_its_rts(struct bgp *bgp_vrf);
extern void bgp_evpn_unmap_vrf_from_its_rts(struct bgp *bgp_vrf);
/* 【函数】bgp_evpn_map_vni_to_its_rts / unmap_vni_from_its_rts —— 把 VNI 登记到
 * 其 Import RT 索引(建 irt_node)，或反向摘除。 */
extern void bgp_evpn_map_vni_to_its_rts(struct bgp *bgp, struct bgpevpn *vpn);
extern void bgp_evpn_unmap_vni_from_its_rts(struct bgp *bgp,
					    struct bgpevpn *vpn);
/* 【函数】bgp_evpn_derive_auto_rt_import / _export —— 为该 VNI 自动派生
 * Import / Export RT(基于 ASN 与 VNI，见 EVPN_AUTORT_VXLAN)。 */
extern void bgp_evpn_derive_auto_rt_import(struct bgp *bgp,
					   struct bgpevpn *vpn);
extern void bgp_evpn_derive_auto_rt_export(struct bgp *bgp,
					   struct bgpevpn *vpn);
/* 【函数】bgp_evpn_derive_auto_rd / _for_vrf —— 为 VNI / VRF 自动派生 RD。 */
extern void bgp_evpn_derive_auto_rd(struct bgp *bgp, struct bgpevpn *vpn);
extern void bgp_evpn_derive_auto_rd_for_vrf(struct bgp *bgp);
/* 【函数】bgp_evpn_lookup_vni —— 在 bgp 的 VNI 表中按 VNI 号查找(哈希 bgp->vni_hash)。 */
extern struct bgpevpn *bgp_evpn_lookup_vni(struct bgp *bgp, vni_t vni);
/* 【函数】bgp_evpn_new —— 新建一个 bgpevpn(VNI 运行时对象)并初始化其字段/表。
 * 参数(originator_ip/tenant_vrf_id/mcast_grp/svi_ifindex)在结构体注释里已说明。 */
extern struct bgpevpn *bgp_evpn_new(struct bgp *bgp, vni_t vni,
		struct ipaddr *originator_ip,
		vrf_id_t tenant_vrf_id,
		struct in_addr mcast_grp,
		ifindex_t svi_ifindex);
/* 【函数】bgp_evpn_free —— 释放一个 bgpevpn(引用计数归零时)。 */
extern void bgp_evpn_free(struct bgp *bgp, struct bgpevpn *vpn);
/* 【函数】bgp_evpn_lookup_l3vni_l2vni_table —— 某 VNI 是否出现在 L3VNI 的 L2VNI
 * 关联表中(即是否作为该三层的二层成员存在)。 */
extern bool bgp_evpn_lookup_l3vni_l2vni_table(vni_t vni);
/* 【函数】update_routes_for_vni —— 重新处理某 VNI 下所有路由(如属性/状态变化后
 * 的批量刷新)。 */
extern int update_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn);
/* 【函数】delete_evpn_route_entry —— 删除 EVPN 表中某 dest 上由 originator 对应的
 * 路径项(按 addpath_id 区分)；返回被删路径或 NULL。 */
extern struct bgp_path_info *delete_evpn_route_entry(struct bgp *bgp, afi_t afi, safi_t safi,
						     struct bgp_dest *dest,
						     const struct bgp_path_info *originator,
						     uint32_t addpaht_id);
/* 【函数】vni_list_cmp —— VNI 列表的排序比较函数(按 VNI 顺序)。 */
int vni_list_cmp(void *p1, void *p2);
/* 【函数】evpn_route_select_install —— 对某 VNI 中某 dest 做最佳路径选择并安装
 * 到内核(路由选择+下发的一体化入口)。 */
extern int evpn_route_select_install(struct bgp *bgp, struct bgpevpn *vpn,
				     struct bgp_dest *dest,
				     struct bgp_path_info *pi);
/* ===== 全局 EVPN 表 / VNI 内 IP 表、MAC 表的节点(dest)获取与查找 =====
 * 命名约定：“_get” 按需创建节点， “_lookup” 仅查找(不存在返回 NULL)。
 * global_* 针对全局(默认实例)EVPN 表；vni_* 针对某 VNI 的 IP/MAC 表。 */
/* 【函数】bgp_evpn_global_node_get —— 在全局 EVPN 表中获取(必要时创建)某 EVPN
 * 前缀对应的节点。local_pi 用于把 VNI 维度信息并入全局键。 */
extern struct bgp_dest *
bgp_evpn_global_node_get(struct bgp_table *table, afi_t afi, safi_t safi,
			 const struct prefix_evpn *evp, struct prefix_rd *prd,
			 const struct bgp_path_info *local_pi);
/* 【函数】bgp_evpn_global_node_lookup —— 在全局 EVPN 表中查找(不创建)某前缀节点。 */
extern struct bgp_dest *bgp_evpn_global_node_lookup(
	struct bgp_table *table, safi_t safi, const struct prefix_evpn *evp,
	struct prefix_rd *prd, const struct bgp_path_info *local_pi);
extern struct bgp_dest *
/* 【函数】bgp_evpn_vni_ip_node_get —— 在某 VNI 的 IP 表中获取(必要时创建)evp 节点。 */
bgp_evpn_vni_ip_node_get(struct bgp_table *const table,
			 const struct prefix_evpn *evp,
			 const struct bgp_path_info *parent_pi);
extern struct bgp_dest *
/* 【函数】bgp_evpn_vni_ip_node_lookup —— 在某 VNI 的 IP 表中查找(不创建)evp 节点。 */
bgp_evpn_vni_ip_node_lookup(const struct bgp_table *const table,
			    const struct prefix_evpn *evp,
			    const struct bgp_path_info *parent_pi);
extern struct bgp_dest *
/* 【函数】bgp_evpn_vni_mac_node_get —— 在某 VNI 的 MAC 表中获取(必要时创建)evp 节点。 */
bgp_evpn_vni_mac_node_get(struct bgp_table *const table,
			  const struct prefix_evpn *evp,
			  const struct bgp_path_info *parent_pi);
extern struct bgp_dest *
/* 【函数】bgp_evpn_vni_mac_node_lookup —— 在某 VNI 的 MAC 表中查找(不创建)evp 节点。 */
bgp_evpn_vni_mac_node_lookup(const struct bgp_table *const table,
			     const struct prefix_evpn *evp,
			     const struct bgp_path_info *parent_pi);
extern struct bgp_dest *
/* 【函数】bgp_evpn_vni_node_get —— 按前缀 p 的类型在 VNI 内选表(IP 或 MAC)获取节点。 */
bgp_evpn_vni_node_get(struct bgpevpn *vpn, const struct prefix_evpn *p,
		      const struct bgp_path_info *parent_pi);
extern struct bgp_dest *
/* 【函数】bgp_evpn_vni_node_lookup —— 按前缀 p 的类型在 VNI 内选表查找节点(不创建)。 */
bgp_evpn_vni_node_lookup(const struct bgpevpn *vpn, const struct prefix_evpn *p,
			 const struct bgp_path_info *parent_pi);

/* 【函数】bgp_evpn_import_route_in_vrfs —— 把某 EVPN 路径批量导入/撤销导入到
 * 所有匹配的 VRF(import 为非 0 导入，否则撤销)。 */
extern void bgp_evpn_import_route_in_vrfs(struct bgp_path_info *pi, int import);
/* 【函数】bgp_evpn_update_type2_route_entry —— 新增/更新某 VNI 下一 dest(rn)的
 * 本端 Type-2 路径(local_pi)；caller 是调用者名，仅用于调试/日志定位来源。 */
extern void bgp_evpn_update_type2_route_entry(struct bgp *bgp,
					      struct bgpevpn *vpn,
					      struct bgp_dest *rn,
					      struct bgp_path_info *local_pi,
					      const char *caller);
/* 【函数】bgp_evpn_route_entry_install_if_vrf_match —— 若该 EVPN 路径的 RT 与
 * 指定 VRF 匹配，则安装(install)/卸载该 VRF 中的对应路由项。 */
extern int bgp_evpn_route_entry_install_if_vrf_match(struct bgp *bgp_vrf,
						     struct bgp_path_info *pi,
						     int install);
/* 【函数】bgp_evpn_import_type2_route —— 对某 Type-2 路径做导入/撤销导入
 * (import 为非 0 导入)。 */
extern void bgp_evpn_import_type2_route(struct bgp_path_info *pi, int import);
/* 【函数】bgp_evpn_xxport_delete_ecomm —— 释放一个用于导入/导出的 ecommunity
 * (list 释放回调)。 */
extern void bgp_evpn_xxport_delete_ecomm(void *val);
/* 【函数】bgp_evpn_route_target_cmp —— 比较两个 ecommunity(作为 RT)是否相等
 * (list 排序/查找用)。 */
extern int bgp_evpn_route_target_cmp(struct ecommunity *ecom1,
				     struct ecommunity *ecom2);
/* 【函数】bgp_evpn_handle_deferred_bestpath_for_vnis —— 处理“延迟的最佳路径
 * 计算”对 VNI 的影响：先前因批量变更延迟的选择在此统一执行(cnt 为待处理计数)。 */
extern void bgp_evpn_handle_deferred_bestpath_for_vnis(struct bgp *bgp, uint16_t cnt);
/* 【函数】bgp_deferred_path_selection —— 对某表执行延迟的最佳路径选择；
 * evpn_select 指示是否按 EVPN 语义选择。返回处理计数。 */
extern uint16_t bgp_deferred_path_selection(struct bgp *bgp, afi_t afi, safi_t safi,
					    struct bgp_table *table, uint16_t cnt,
					    struct bgpevpn *vpn, bool evpn_select);

#endif /* _BGP_EVPN_PRIVATE_H */
