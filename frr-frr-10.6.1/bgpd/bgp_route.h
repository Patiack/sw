// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP routing information base
 * Copyright (C) 1996, 97, 98, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_route.h —— RIB 的“路径”模型与路由处理队列
 * ============================================================================
 * 与 bgp_table.h 配套：bgp_table.h 定义了“表/节点”，本文件定义挂在节点上的
 * “路径”struct bgp_path_info（一个前缀学到的一条路由），以及围绕它的各种辅助
 * 结构和处理队列。阅读顺序建议：
 *   1) struct bgp_path_info        —— 一条路径（核心）。
 *   2) struct bgp_path_info_extra  —— 路径的“懒分配”附加信息（省内存的关键设计）。
 *   3) struct bgp_static / bgp_aggregate —— network 静态路由与聚合配置。
 *   4) struct meta_queue + MQ_SIZE —— 路由处理的两级优先级队列。
 *   5) 末尾一大片 bgp_* 函数原型（真正的实现在 bgp_route.c）。
 *
 * 一条路由从收到到下发的大致链路（贯穿本文件的原型）：
 *   报文解析 → bgp_update/bgp_withdraw 写入 RIB(生成/更新 bgp_path_info)
 *   → bgp_process 把 dest 排入 process_queue → bgp_process_main_one 触发
 *   → bgp_best_selection 选出最优/ECMP → 变化则经 bgp_zebra 下发、并通知
 *   update-group 向邻居通告。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_ROUTE_H
#define _QUAGGA_BGP_ROUTE_H

#include <stdbool.h>

#include "hook.h"
#include "queue.h"
#include "nexthop.h"
#include "typesafe.h"
#include "bgp_table.h"
#include "bgp_addpath_types.h"
#include "bgp_rpki.h"

struct bgp_nexthop_cache;
struct bgp_route_evpn;

/* show ip bgp 的“过滤/展示模式”。传给 bgp_show_table()，决定遍历 RIB 时筛什么：
 * 按正则、前缀列表、访问列表、AS 列表、route-map、按邻居、按团体/大团体/扩展团体、
 * 按 RPKI 校验状态等。新的展示需求通常就是往这里加一个类型。 */
enum bgp_show_type {
	bgp_show_type_normal,
	bgp_show_type_regexp,
	bgp_show_type_prefix_list,
	bgp_show_type_access_list,
	bgp_show_type_filter_list,
	bgp_show_type_route_map,
	bgp_show_type_neighbor,
	bgp_show_type_cidr_only,
	bgp_show_type_prefix_longer,
	bgp_show_type_community_alias,
	bgp_show_type_community_all,
	bgp_show_type_community,
	bgp_show_type_community_exact,
	bgp_show_type_community_list,
	bgp_show_type_community_list_exact,
	bgp_show_type_extcommunity,
	bgp_show_type_extcommunity_exact,
	bgp_show_type_lcommunity_all,
	bgp_show_type_lcommunity,
	bgp_show_type_lcommunity_exact,
	bgp_show_type_lcommunity_list,
	bgp_show_type_lcommunity_list_exact,
	bgp_show_type_flap_statistics,
	bgp_show_type_flap_neighbor,
	bgp_show_type_dampend_paths,
	bgp_show_type_damp_neighbor,
	bgp_show_type_detail,
	bgp_show_type_rpki,
	bgp_show_type_prefix_version,
	bgp_show_type_self_originated,
};

enum bgp_show_adj_route_type {
	bgp_show_adj_route_advertised,
	bgp_show_adj_route_received,
	bgp_show_adj_route_filtered,
	bgp_show_adj_route_bestpath,
};


#define BGP_SHOW_SCODE_HEADER                                                  \
	"Status codes:  s suppressed, d damped, "                              \
	"h history, u unsorted, * valid, > best, = multipath,\n"               \
	"               i internal, r RIB-failure, S Stale, R Removed\n"
#define BGP_SHOW_OCODE_HEADER                                                  \
	"Origin codes:  i - IGP, e - EGP, ? - incomplete\n"
#define BGP_SHOW_NCODE_HEADER "Nexthop codes: @NNN nexthop's vrf id, < announce-nh-self\n"
#define BGP_SHOW_RPKI_HEADER                                                   \
	"RPKI validation codes: V valid, I invalid, N Not found\n\n"
#define BGP_SHOW_HEADER "     Network          Next Hop            Metric LocPrf Weight Path\n"
#define BGP_SHOW_HEADER_WIDE "     Network                                      Next Hop                                  Metric LocPrf Weight Path\n"

/* Maximum number of sids we can process or send with a prefix. */
#define BGP_MAX_SIDS 6

/* Maximum buffer length for storing BGP best path selection reason */
#define BGP_MAX_SELECTION_REASON_STR_BUF 32

/* Error codes for handling NLRI */
#define BGP_NLRI_PARSE_OK 0
#define BGP_NLRI_PARSE_ERROR_PREFIX_OVERFLOW -1
#define BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW -2
#define BGP_NLRI_PARSE_ERROR_PREFIX_LENGTH -3
#define BGP_NLRI_PARSE_ERROR_PACKET_LENGTH -4
#define BGP_NLRI_PARSE_ERROR_LABEL_LENGTH -5
#define BGP_NLRI_PARSE_ERROR_EVPN_MISSING_TYPE -6
#define BGP_NLRI_PARSE_ERROR_EVPN_TYPE2_SIZE -7
#define BGP_NLRI_PARSE_ERROR_EVPN_TYPE3_SIZE -8
#define BGP_NLRI_PARSE_ERROR_EVPN_TYPE4_SIZE -9
#define BGP_NLRI_PARSE_ERROR_EVPN_TYPE5_SIZE -10
#define BGP_NLRI_PARSE_ERROR_FLOWSPEC_IPV6_NOT_SUPPORTED -11
#define BGP_NLRI_PARSE_ERROR_FLOWSPEC_BAD_FORMAT -13
#define BGP_NLRI_PARSE_ERROR_ADDRESS_FAMILY -14
#define BGP_NLRI_PARSE_ERROR_EVPN_TYPE1_SIZE -15
#define BGP_NLRI_PARSE_ERROR -32

/* 1. local MAC-IP/type-2 paths in the VNI routing table are linked to the
 * destination ES
 * 2. remote MAC-IP paths in the global routing table are linked to the
 * destination ES
 */
struct bgp_path_es_info {
	/* back pointer to the route */
	struct bgp_path_info *pi;
	vni_t vni;
	/* destination ES */
	struct bgp_evpn_es *es;
	/* memory used for linking the path to the destination ES */
	struct listnode es_listnode;
	uint8_t flags;
/* Path is linked to the VNI list */
#define BGP_EVPN_PATH_ES_INFO_VNI_LIST (1 << 0)
/* Path is linked to the global list */
#define BGP_EVPN_PATH_ES_INFO_GLOBAL_LIST (1 << 1)
};

/* IP paths imported into the VRF from an EVPN route source
 * are linked to the nexthop/VTEP IP
 */
struct bgp_path_evpn_nh_info {
	/* back pointer to the route */
	struct bgp_path_info *pi;
	struct bgp_evpn_nh *nh;
	/* memory used for linking the path to the nexthop */
	struct listnode nh_listnode;
};

struct bgp_path_mh_info {
	struct bgp_path_es_info *es_info;
	struct bgp_path_evpn_nh_info *nh_info;
};

struct bgp_sid_info {
	struct in6_addr sid;
	uint8_t loc_block_len;
	uint8_t loc_node_len;
	uint8_t func_len;
	uint8_t arg_len;
	uint8_t transposition_len;
	uint8_t transposition_offset;
};

/* new structure for EVPN */
struct bgp_path_info_extra_evpn {
#define BGP_EVPN_MACIP_TYPE_SVI_IP (1 << 0)
	/* af specific flags */
	uint16_t af_flags;
	union {
		struct ethaddr mac; /* MAC set here for VNI IP table */
		struct ipaddr ip;   /* IP set here for VNI MAC table */
	} vni_info;
	/* Destination Ethernet Segment links for EVPN MH */
	struct bgp_path_mh_info *mh_info;

	struct bgp_path_info *type5_originator;
};

/* new structure for flowspec*/
struct bgp_path_info_extra_fs {
	/* presence of FS pbr firewall based entry */
	struct list *bgp_fs_pbr;
	/* presence of FS pbr iprule based entry */
	struct list *bgp_fs_iprule;
};

/* new structure for vrfleak*/
struct bgp_path_info_extra_vrfleak {
	void *parent; /* parent from global table */
	/*
	 * Original bgp instance for imported routes. Needed for:
	 * 1. Find all routes from a specific vrf for deletion
	 * 2. vrf context of original nexthop
	 *
	 * Store pointer to bgp instance rather than bgp->vrf_id because
	 * bgp->vrf_id is not always valid (or may change?).
	 *
	 * Set to NULL if route is not imported from another bgp instance.
	 */
	struct bgp *bgp_orig;
	/*
	 * Original bgp session to know if the session is a
	 * connected EBGP session or not
	 */
	struct peer *peer_orig;
	/*
	 * Nexthop in context of original bgp instance. Needed
	 * for label resolution of core mpls routes exported to a vrf.
	 * Set nexthop_orig.family to 0 if not valid.
	 */
	struct prefix nexthop_orig;
};

#ifdef ENABLE_BGP_VNC
struct bgp_path_info_extra_vnc {
	union {
		struct {
			void *rfapi_handle; /* export: NVE advertising this
					       route */
			struct list *local_nexthops; /* optional, for static
							routes */
		} export;

		struct {
			struct event *timer;
			void *hme; /* encap monitor, if this is a VPN route */
			struct prefix_rd
				rd; /* import: route's route-distinguisher */
			uint8_t un_family; /* family of cached un address, 0 if
					     unset */
			union {
				struct in_addr addr4;
				struct in6_addr addr6;
			} un; /* cached un address */
			time_t create_time;
			struct prefix aux_prefix; /* AFI_L2VPN: the IP addr,
						     if family set */
		} import;
	} vnc;
};
#endif

/* Ancillary information to struct bgp_path_info,
 * used for uncommonly used data (aggregation, MPLS, etc.)
 * and lazily allocated to save memory.
 */
/* ===========================================================================
 * struct bgp_path_info_extra —— bgp_path_info 的“附加信息”，懒分配
 * ---------------------------------------------------------------------------
 * 设计动机：绝大多数路径用不到聚合/MPLS/EVPN 等数据。若把它们直接塞进
 * bgp_path_info，会让每条路径都变“胖”，在数百万路由规模下内存开销巨大。于是把
 * 这些不常用数据集中到本结构，只在首次需要时（bgp_path_info_extra_get）分配。
 * 这里存的是各类“子结构指针”，各自按需再分配：
 *   damp_info        —— 路由抖动抑制状态；
 *   aggr_suppressors —— 抑制本路径的聚合项列表；
 *   igpmetric        —— 解析下一跳得到的 IGP 度量；
 *   labels           —— MPLS 标签/VNI 栈；
 *   evpn / flowspec / vrfleak / vnc —— 各特性的专属数据（按需）。
 * 读代码时若看到某字段总要“先判 extra 再取”，就是这个懒分配设计所致。
 * ===========================================================================
 */
struct bgp_path_info_extra {
	/* Pointer to dampening structure.  */
	struct bgp_damp_info *damp_info;

	/** List of aggregations that suppress this path. */
	struct list *aggr_suppressors;

	/* Nexthop reachability check.  */
	uint32_t igpmetric;

	/* MPLS label(s) - VNI(s) for EVPN-VxLAN  */
	struct bgp_labels *labels;

	/* timestamp of the rib installation */
	time_t bgp_rib_uptime;

	/*For EVPN*/
	struct bgp_path_info_extra_evpn *evpn;

#ifdef ENABLE_BGP_VNC
	struct bgp_path_info_extra_vnc *vnc;
#endif

	/* For flowspec*/
	struct bgp_path_info_extra_fs *flowspec;

	/* For vrf leaking*/
	struct bgp_path_info_extra_vrfleak *vrfleak;
};

struct bgp_mplsvpn_label_nh {
	/* For nexthop per label linked list */
	LIST_ENTRY(bgp_path_info) label_nh_thread;

	/* Back pointer to the bgp label per nexthop structure */
	struct bgp_label_per_nexthop_cache *label_nexthop_cache;
};

struct bgp_mplsvpn_nh_label_bind {
	/* For mplsvpn nexthop label bind linked list */
	LIST_ENTRY(bgp_path_info) nh_label_bind_thread;

	/* Back pointer to the bgp mplsvpn nexthop label bind structure */
	struct bgp_mplsvpn_nh_label_bind_cache *nh_label_bind_cache;
};

/* ===========================================================================
 * struct bgp_path_info —— 某个前缀下的“一条路径”
 * ---------------------------------------------------------------------------
 * 一个 bgp_dest（前缀）下挂着一条 bgp_path_info 链表：每个 peer 通告来的一条
 * 路径就是链表中一项；聚合/导入也可能生成新的项。bestpath 就是在这条链表上比较
 * 出一个“最优”，其余等价项成为 multipath。
 *
 * 关键成员：
 *   next/prev            : 同前缀下的路径链表。
 *   nh_thread            : 【复用】挂到 nexthop 的“使用该下一跳的路径”链表上，
 *                          使下一跳可达性变化时能快速找到受影响的所有路径。
 *                          （注意与 next/prev 不是同一个链表，勿混淆）
 *   net                  : 反向指回所属 bgp_dest。
 *   nexthop              : 指向下一跳缓存 bgp_nexthop_cache（NHT 结果）。
 *   peer / from          : 谁通告的 / 实际来源（路由反射、导入时二者可能不同）。
 *   attr                 : 属性（AS_PATH、NEXT_HOP、MED…），是 RFC4271 UPDATE 的
 *                          语义载体，定义在 bgp_attr.h。
 *   extra                : 【懒分配】不常用信息（聚合/MPLS/EVPN/VRF 泄漏/Flowspec…），
 *                          需要时才分配，显著省内存。见 struct bgp_path_info_extra。
 *   flags (BGP_PATH_*)   : 路径状态位，最重要的几个：
 *                            VALID      —— 可参与选路（如下一跳可达）；
 *                            SELECTED   —— 被选为最佳路径；
 *                            MULTIPATH  —— 是 ECMP 成员（注意：不设在最优路径上）；
 *                            HISTORY/DAMPED/REMOVED —— 不可用状态（见
 *                            BGP_PATH_UNUSEABLE / BGP_PATH_HOLDDOWN 宏）。
 *   type/sub_type        : 路由来源（BGP/static/aggregate/redistribute/imported）
 *                          及 BGP 子类型。
 *   reason               : 它作为最优时“因何胜出”（见 bgp_path_selection_reason）。
 *   addpath_rx_id/tx_addpath : Add-Path(RFC7911) 的收发标识。
 *   lock                 : 引用计数，配合 bgp_path_info_lock/unlock。
 * ===========================================================================
 */
struct bgp_path_info {
	/* For linked list. */
	struct bgp_path_info *next;
	struct bgp_path_info *prev;

	/* Hash linkage for pi_hash in bgp_table */
	struct bgp_pi_hash_item pi_hash_link;

	/* For nexthop linked list */
	LIST_ENTRY(bgp_path_info) nh_thread;

	/* Back pointer to the prefix node */
	struct bgp_dest *net;

	/* Back pointer to the nexthop structure */
	struct bgp_nexthop_cache *nexthop;

	/* Peer structure.  */
	struct peer *peer;

	/* From peer structure */
	struct peer *from;

	/* Attribute structure.  */
	struct attr *attr;

	/* Extra information */
	struct bgp_path_info_extra *extra;

	/* Uptime.  */
	time_t uptime;

	/* reference count */
	int lock;

	/* BGP information status.  */
	uint32_t flags;
#define BGP_PATH_IGP_CHANGED (1 << 0)
#define BGP_PATH_DAMPED (1 << 1)
#define BGP_PATH_HISTORY (1 << 2)
#define BGP_PATH_SELECTED (1 << 3)
#define BGP_PATH_VALID (1 << 4)
#define BGP_PATH_ATTR_CHANGED (1 << 5)
#define BGP_PATH_DMED_CHECK (1 << 6)
#define BGP_PATH_DMED_SELECTED (1 << 7)
#define BGP_PATH_STALE (1 << 8)
#define BGP_PATH_REMOVED (1 << 9)
#define BGP_PATH_COUNTED (1 << 10)
/*
 * A BGP_PATH_MULTIPATH flag is not set on the best path
 * it is set on every other node that is part of ECMP
 * for that particular dest
 */
#define BGP_PATH_MULTIPATH (1 << 11)
#define BGP_PATH_MULTIPATH_CHG (1 << 12)
#define BGP_PATH_RIB_ATTR_CHG (1 << 13)
#define BGP_PATH_ANNC_NH_SELF (1 << 14)
#define BGP_PATH_LINK_BW_CHG (1 << 15)
#define BGP_PATH_ACCEPT_OWN (1 << 16)
#define BGP_PATH_MPLSVPN_LABEL_NH (1 << 17)
#define BGP_PATH_MPLSVPN_NH_LABEL_BIND (1 << 18)
#define BGP_PATH_UNSORTED (1 << 19)
/*
 * BGP_PATH_MULTIPATH_NEW is set on those bgp_path_info
 * nodes that we have decided should possibly be in the
 * ecmp path for a particular dest.  This flag is
 * removed when the bgp_path_info's are looked at to
 * decide on whether or not a bgp_path_info is on
 * the actual ecmp path.
 */
#define BGP_PATH_MULTIPATH_NEW (1 << 20)

	/* BGP route type.  This can be static, RIP, OSPF, BGP etc.  */
	uint8_t type;

	/* When above type is BGP.  This sub type specify BGP sub type
	   information.  */
	uint8_t sub_type;
#define BGP_ROUTE_NORMAL       0
#define BGP_ROUTE_STATIC       1
#define BGP_ROUTE_AGGREGATE    2
#define BGP_ROUTE_REDISTRIBUTE 3
#ifdef ENABLE_BGP_VNC
# define BGP_ROUTE_RFP          4
#endif
#define BGP_ROUTE_IMPORTED     5        /* from another bgp instance/safi */

	unsigned short instance;

	enum bgp_path_selection_reason reason;

	/* Addpath identifiers */
	uint32_t addpath_rx_id;
	struct bgp_addpath_info_data tx_addpath;

	union {
		struct bgp_mplsvpn_label_nh blnc;
		struct bgp_mplsvpn_nh_label_bind bmnc;
	} mplsvpn;
};

/* Structure used in BGP path selection */
struct bgp_path_info_pair {
	struct bgp_path_info *old;
	struct bgp_path_info *new;
};

/* BGP static route configuration. */
/* network / 静态路由配置（对应 CLI：network A.B.C.D/M ...）。
 * 它是 bgp->static_routes 表里 dest->info 指向的数据（见 bgp_table.h 的“多态”）。 */
struct bgp_static {
	/* Backdoor configuration.  */
	int backdoor;

	/* Label index configuration; applies to LU prefixes. */
	uint32_t label_index;
#define BGP_INVALID_LABEL_INDEX   0xFFFFFFFF

	/* Import check status.  */
	uint8_t valid;

	uint16_t encap_tunneltype;

	/* IGP metric. */
	uint32_t igpmetric;

	/* IGP nexthop. */
	struct in_addr igpnexthop;

	/* Atomic set reference count (ie cause of pathlimit) */
	uint32_t atomic;

	/* BGP redistribute route-map.  */
	struct {
		char *name;
		struct route_map *map;
	} rmap;

	/* Route Distinguisher */
	struct prefix_rd prd;
	char *prd_pretty;

	/* MPLS label.  */
	mpls_label_t label;

	/* EVPN */
	esi_t *eth_s_id;
	struct ethaddr *router_mac;
	struct prefix gatewayIp;
};

/* Aggreagete address:
 *
 *  advertise-map  Set condition to advertise attribute
 *  as-set         Generate AS set path information
 *  attribute-map  Set attributes of aggregate
 *  route-map      Set parameters of aggregate
 *  summary-only   Filter more specific routes from updates
 *  suppress-map   Conditionally filter more specific routes from updates
 *  <cr>
 */
/* aggregate-address 聚合配置（对应 CLI：aggregate-address A.B.C.D/M ...）。
 * 它是 bgp->aggregate 表里 dest->info 指向的数据。要点：
 *   summary_only : 只通告聚合路由，抑制所有更具体的明细；
 *   as_set       : 生成 AS_SET（把明细的 AS 汇成 {…} 形式）；
 *   suppress_map : 有条件地抑制明细；
 *   community_hash / ecommunity_hash / lcommunity_hash / aspath_hash :
 *       分别汇总所有被聚合明细中出现的团体/AS_PATH，用于 as_set 与聚合团体计算；
 *   count 等     : 被聚合明细的计数与 origin 分类计数。
 * 聚合路由的生成/更新逻辑在 bgp_route.c 的 bgp_aggregate_* 系列。 */
struct bgp_aggregate {
	/* Summary-only flag. */
	uint8_t summary_only;

	/* AS set generation. */
	uint8_t as_set;

	/* Optional modify flag to override ORIGIN */
	uint8_t origin;

	/** Are there MED mismatches? */
	bool med_mismatched;
	/* MED matching state. */
	/** Did we get the first MED value? */
	bool med_initialized;
	/** Match only equal MED. */
	bool match_med;

	/* Route-map for aggregated route. */
	struct {
		char *name;
		struct route_map *map;
	} rmap;

	/* Suppress-count. */
	unsigned long count;

	/* Count of routes of origin type incomplete under this aggregate. */
	unsigned long incomplete_origin_count;

	/* Count of routes of origin type egp under this aggregate. */
	unsigned long egp_origin_count;

	/* Hash containing the communities of all the
	 * routes under this aggregate.
	 */
	struct hash *community_hash;

	/* Hash containing the extended communities of all the
	 * routes under this aggregate.
	 */
	struct hash *ecommunity_hash;

	/* Hash containing the large communities of all the
	 * routes under this aggregate.
	 */
	struct hash *lcommunity_hash;

	/* Hash containing the AS-Path of all the
	 * routes under this aggregate.
	 */
	struct hash *aspath_hash;

	/* Aggregate route's community. */
	struct community *community;

	/* Aggregate route's extended community. */
	struct ecommunity *ecommunity;

	/* Aggregate route's large community. */
	struct lcommunity *lcommunity;

	/* Aggregate route's as-path. */
	struct aspath *aspath;

	/** MED value found in current group. */
	uint32_t med_matched_value;

	/**
	 * Test if aggregated address MED of all route match, otherwise
	 * returns `false`. This macro will also return `true` if MED
	 * matching is disabled.
	 */
#define AGGREGATE_MED_VALID(aggregate)                                         \
	(((aggregate)->match_med && !(aggregate)->med_mismatched)              \
	 || !(aggregate)->match_med)

	/** Suppress map route map name (`NULL` when disabled). */
	char *suppress_map_name;
	/** Suppress map route map pointer. */
	struct route_map *suppress_map;
};

#define BGP_NEXTHOP_AFI_FROM_NHLEN(nhlen)                                      \
	((nhlen) < IPV4_MAX_BYTELEN                                            \
		 ? 0                                                           \
		 : ((nhlen) < IPV6_MAX_BYTELEN ? AFI_IP : AFI_IP6))

#define BGP_ATTR_MP_NEXTHOP_LEN_IP6(attr)                                      \
	((attr)->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL ||               \
	 (attr)->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL ||        \
	 (attr)->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL ||              \
	 (attr)->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL)

#define BGP_ATTR_NEXTHOP_AFI_IP6(attr)                                         \
	(!CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP)) &&          \
	 BGP_ATTR_MP_NEXTHOP_LEN_IP6(attr))

#define BGP_PATH_COUNTABLE(BI)                                                 \
	(!CHECK_FLAG((BI)->flags, BGP_PATH_HISTORY)                            \
	 && !CHECK_FLAG((BI)->flags, BGP_PATH_REMOVED))

/* Flags which indicate a route is unusable in some form */
#define BGP_PATH_UNUSEABLE                                                     \
	(BGP_PATH_HISTORY | BGP_PATH_DAMPED | BGP_PATH_REMOVED)
/* Macro to check BGP information is alive or not.  Sadly,
 * not equivalent to just checking previous, because of the
 * sense of the additional VALID flag.
 */
#define BGP_PATH_HOLDDOWN(BI)                                                  \
	(!CHECK_FLAG((BI)->flags, BGP_PATH_VALID)                              \
	 || CHECK_FLAG((BI)->flags, BGP_PATH_UNUSEABLE))

#define DISTRIBUTE_IN_NAME(F)   ((F)->dlist[FILTER_IN].name)
#define DISTRIBUTE_IN(F)        ((F)->dlist[FILTER_IN].alist)
#define DISTRIBUTE_OUT_NAME(F)  ((F)->dlist[FILTER_OUT].name)
#define DISTRIBUTE_OUT(F)       ((F)->dlist[FILTER_OUT].alist)

#define PREFIX_LIST_IN_NAME(F)  ((F)->plist[FILTER_IN].name)
#define PREFIX_LIST_IN(F)       ((F)->plist[FILTER_IN].plist)
#define PREFIX_LIST_OUT_NAME(F) ((F)->plist[FILTER_OUT].name)
#define PREFIX_LIST_OUT(F)      ((F)->plist[FILTER_OUT].plist)

#define FILTER_LIST_IN_NAME(F)  ((F)->aslist[FILTER_IN].name)
#define FILTER_LIST_IN(F)       ((F)->aslist[FILTER_IN].aslist)
#define FILTER_LIST_OUT_NAME(F) ((F)->aslist[FILTER_OUT].name)
#define FILTER_LIST_OUT(F)      ((F)->aslist[FILTER_OUT].aslist)

#define ROUTE_MAP_IN_NAME(F)    ((F)->map[RMAP_IN].name)
#define ROUTE_MAP_IN(F)         ((F)->map[RMAP_IN].map)
#define ROUTE_MAP_OUT_NAME(F)   ((F)->map[RMAP_OUT].name)
#define ROUTE_MAP_OUT(F)        ((F)->map[RMAP_OUT].map)

#define UNSUPPRESS_MAP_NAME(F)  ((F)->usmap.name)
#define UNSUPPRESS_MAP(F)       ((F)->usmap.map)

#define ADVERTISE_MAP_NAME(F)	((F)->advmap.aname)
#define ADVERTISE_MAP(F)	((F)->advmap.amap)

#define ADVERTISE_CONDITION(F)	((F)->advmap.condition)

#define CONDITION_MAP_NAME(F)	((F)->advmap.cname)
#define CONDITION_MAP(F)	((F)->advmap.cmap)

/* path PREFIX (addpath rxid NUMBER) */
#define PATH_ADDPATH_STR_BUFFER PREFIX2STR_BUFFER + 32

#define BGP_PATH_INFO_NUM_LABELS(pi)                                           \
	((pi) && (pi)->extra && (pi)->extra->labels                            \
		 ? (pi)->extra->labels->num_labels                             \
		 : 0)

enum bgp_path_type {
	BGP_PATH_SHOW_ALL,
	BGP_PATH_SHOW_BESTPATH,
	BGP_PATH_SHOW_MULTIPATH
};

/* meta-queue structure:
 * sub-queue 0: soo routes
 * sub-queue 1: other routes
 */
#define MQ_SIZE 3

/* For checking that an object has already queued in some sub-queue */
#define MQ_BIT_MASK ((1 << MQ_SIZE) - 1)

/* ===========================================================================
 * struct meta_queue —— 路由处理的“二级优先级队列”
 * ---------------------------------------------------------------------------
 * BGP 收到/撤掉路由后不立即算 bestpath，而是把受影响的前缀节点(dest)入队，稍后
 * 批量处理。队列是两级的：
 *   bgp->process_queue (work_queue) → 从中取出 → bgp->mq (本结构) 的子队列。
 * 子队列按优先级分（见 enum meta_queue_indexes）：
 *   META_QUEUE_EARLY_ROUTE : 需优先处理的（如 SOO/VRF 泄漏相关）；
 *   META_QUEUE_OTHER_ROUTE : 普通路由；
 *   META_QUEUE_EOIU_MARKER : update-delay 到期的 EOIU 标记（见 bgp_eoiu_info）。
 * 每个 bgp_dest 用 flags 中的 MQ 位记录“已排入哪个子队列”，MQ_BIT_MASK 用于一次
 * 性检查/清除这些位，避免同一 dest 被重复入队（对应 bgp_dest 的
 * BGP_NODE_PROCESS_SCHEDULED 之类的标志）。
 * ===========================================================================
 */
struct meta_queue {
	STAILQ_HEAD(bgp_dest_queue, bgp_dest) * subq[MQ_SIZE];
	uint32_t size; /* sum of lengths of all subqueues */
};

/*
 * When the update-delay expires, BGP inserts an EOIU (End-Of-Initial-Update) marker
 * into the BGP_PROCESS_QUEUE_EOIU_MARKER meta queue. This meta queue holds only
 * bgp_dest structures. To process the EOIU marker, we need to call bgp_process_main_one()
 * on the corresponding BGP instance. Since the marker itself isn't a real route
 * (a dummy dest is created for this) and doesn't inherently carry the BGP instance pointer,
 * we store the struct bgp pointer in the dest->info field. This ensures that, when processing
 * the EOIU marker, we have the necessary context (the relevant BGP instance) available.
 */
/* update-delay 到期后，BGP 会往 META_QUEUE_EOIU_MARKER 子队列塞一个 EOIU
 * (End-Of-Initial-Update) 标记。该标记本身不是真实路由（只是一个占位的假 dest），
 * 且不携带 bgp 实例指针，于是把 struct bgp 存进 dest->info，处理该标记时就能拿到
 * 正确的实例上下文。（这正是 bgp_dest->info 多态的一个实例。） */
struct bgp_eoiu_info {
	struct bgp *bgp;
};

/*
 * Meta Q's specific names
 */
enum meta_queue_indexes {
	META_QUEUE_EARLY_ROUTE,
	META_QUEUE_OTHER_ROUTE,
	META_QUEUE_EOIU_MARKER,
};

static inline void bgp_bump_version(struct bgp_dest *dest)
{
	dest->version = bgp_table_next_version(bgp_dest_table(dest));
}

static inline int bgp_fibupd_safi(safi_t safi)
{
	if (safi == SAFI_UNICAST || safi == SAFI_MULTICAST
	    || safi == SAFI_LABELED_UNICAST
	    || safi == SAFI_FLOWSPEC)
		return 1;
	return 0;
}

/* Flag if the route path's family matches params. */
static inline bool is_pi_family_matching(struct bgp_path_info *pi,
					 afi_t afi, safi_t safi)
{
	struct bgp_table *table;
	struct bgp_dest *dest;

	dest = pi->net;
	if (!dest)
		return false;
	table = bgp_dest_table(dest);
	if (table &&
	    table->afi == afi &&
	    table->safi == safi)
		return true;
	return false;
}

static inline void prep_for_rmap_apply(struct bgp_path_info *dst_pi,
				       struct bgp_path_info_extra *dst_pie, struct bgp_dest *dest,
				       struct bgp_path_info *src_pi, struct peer *peer,
				       struct peer *from, struct attr *attr)
{
	memset(dst_pi, 0, sizeof(struct bgp_path_info));
	dst_pi->peer = peer;
	dst_pi->from = from;
	dst_pi->attr = attr;
	dst_pi->net = dest;
	dst_pi->flags = src_pi->flags;
	dst_pi->type = src_pi->type;
	dst_pi->sub_type = src_pi->sub_type;
	if (src_pi->extra) {
		memcpy(dst_pie, src_pi->extra,
		       sizeof(struct bgp_path_info_extra));
		dst_pi->extra = dst_pie;
	}
}

static inline bool bgp_check_advertise(struct bgp *bgp, struct bgp_dest *dest,
				       safi_t safi)
{
	if (!bgp_fibupd_safi(safi))
		return true;

	return (!(BGP_SUPPRESS_FIB_ENABLED(bgp) &&
		  CHECK_FLAG(dest->flags, BGP_NODE_FIB_INSTALL_PENDING) &&
		 (!bgp_option_check(BGP_OPT_NO_FIB))));
}

/*
 * If we have a fib result and it failed to install( or was withdrawn due
 * to better admin distance we need to send down the wire a withdrawal.
 * This function assumes that bgp_check_advertise was already returned
 * as good to go.
 */
static inline bool bgp_check_withdrawal(struct bgp *bgp, struct bgp_dest *dest,
					safi_t safi)
{
	struct bgp_path_info *pi, *selected = NULL;

	if (!bgp_fibupd_safi(safi) || !BGP_SUPPRESS_FIB_ENABLED(bgp))
		return false;

	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		if (CHECK_FLAG(pi->flags, BGP_PATH_SELECTED)) {
			selected = pi;
			continue;
		}

		if (pi->sub_type != BGP_ROUTE_NORMAL)
			return true;
	}

	/*
	 * pi is selected and bgp is dealing with a static route
	 * ( ie a network statement of some sort ).  FIB installed
	 * is irrelevant
	 *
	 * I am not sure what the above for loop is wanted in this
	 * manner at this point.  But I do know that if I have
	 * a static route that is selected and it's the one
	 * being checked for should I withdrawal we do not
	 * want to withdraw the route on installation :)
	 */
	if (selected && selected->sub_type == BGP_ROUTE_STATIC)
		return false;

	if (CHECK_FLAG(dest->flags, BGP_NODE_FIB_INSTALLED))
		return false;

	return true;
}

/* called before bgp_process() */
DECLARE_HOOK(bgp_process,
	     (struct bgp *bgp, afi_t afi, safi_t safi, struct bgp_dest *bn,
	      struct peer *peer, bool withdraw),
	     (bgp, afi, safi, bn, peer, withdraw));

/* called when a route is updated in the rib */
DECLARE_HOOK(bgp_route_update,
	     (struct bgp *bgp, afi_t afi, safi_t safi, struct bgp_dest *bn,
	      struct bgp_path_info *old_route, struct bgp_path_info *new_route),
	     (bgp, afi, safi, bn, old_route, new_route));

extern int bgp_pi_hash_cmp(const struct bgp_path_info *p1, const struct bgp_path_info *p2);
extern uint32_t bgp_pi_hash_hashfn(const struct bgp_path_info *pi);

DECLARE_HASH(bgp_pi_hash, struct bgp_path_info, pi_hash_link, bgp_pi_hash_cmp, bgp_pi_hash_hashfn);

/* BGP show options */
#define BGP_SHOW_OPT_JSON (1 << 0)
#define BGP_SHOW_OPT_WIDE (1 << 1)
#define BGP_SHOW_OPT_AFI_ALL (1 << 2)
#define BGP_SHOW_OPT_AFI_IP (1 << 3)
#define BGP_SHOW_OPT_AFI_IP6 (1 << 4)
#define BGP_SHOW_OPT_ESTABLISHED (1 << 5)
#define BGP_SHOW_OPT_FAILED (1 << 6)
#define BGP_SHOW_OPT_JSON_DETAIL (1 << 7)
#define BGP_SHOW_OPT_TERSE (1 << 8)
#define BGP_SHOW_OPT_ROUTES_DETAIL (1 << 9)
#define BGP_SHOW_OPT_INTERNAL_DATA (1 << 10)

/* Prototypes. */
extern void bgp_rib_remove(struct bgp_dest *dest, struct bgp_path_info *pi,
			   struct peer *peer, afi_t afi, safi_t safi);
extern void bgp_process_queue_init(struct bgp *bgp);
extern void bgp_route_init(void);
extern void bgp_route_finish(void);
extern void bgp_cleanup_routes(struct bgp *bgp);
extern void bgp_free_aggregate_info(struct bgp_aggregate *aggregate);
extern void bgp_announce_route(struct peer *peer, afi_t afi, safi_t safi,
			       bool force);
extern void bgp_stop_announce_route_timer(struct peer_af *paf);
extern void bgp_announce_route_all(struct peer *peer);
extern void bgp_default_originate(struct peer *peer, afi_t afi, safi_t safi,
				  bool withdraw);
extern void bgp_soft_reconfig_table_task_cancel(const struct bgp *bgp,
						const struct bgp_table *table,
						const struct peer *peer);

/*
 * If this peer is configured for soft reconfig in then do the work
 * and return true.  If it is not return false; and do nothing
 */
extern bool bgp_soft_reconfig_in(struct peer *peer, afi_t afi, safi_t safi);
extern void bgp_clear_route(struct peer *peer, afi_t afi, safi_t safi);
extern void bgp_clear_route_all(struct peer *peer);
/* Clear routes for a batch of peers */
void bgp_clear_route_batch(struct bgp_clearing_info *cinfo);

extern void bgp_clear_adj_in(struct peer *peer, afi_t afi, safi_t safi);
extern void bgp_clear_stale_route(struct peer *peer, afi_t afi, safi_t safi);
extern void bgp_set_stale_route(struct peer *peer, afi_t afi, safi_t safi);
extern bool bgp_outbound_policy_exists(struct peer *peer, struct bgp_filter *filter);
extern bool bgp_inbound_policy_exists(struct peer *peer, struct bgp_filter *filter);

extern struct bgp_dest *bgp_afi_node_get(struct bgp_table *table, afi_t afi,
					 safi_t safi, const struct prefix *p,
					 struct prefix_rd *prd);
extern struct bgp_path_info *bgp_path_info_lock(struct bgp_path_info *path);
extern struct bgp_path_info *bgp_path_info_unlock(struct bgp_path_info *path);
extern bool bgp_path_info_nexthop_changed(struct bgp_path_info *pi,
					  struct peer *to, afi_t afi);
extern struct bgp_path_info *
bgp_get_imported_bpi_ultimate(struct bgp_path_info *info);
extern void bgp_path_info_add(struct bgp_dest *dest, struct bgp_path_info *pi);
extern void bgp_path_info_extra_free(struct bgp_path_info_extra **extra);
extern struct bgp_dest *bgp_path_info_reap(struct bgp_dest *dest,
					   struct bgp_path_info *pi);
extern void bgp_path_info_mark_for_delete(struct bgp_dest *dest, struct bgp_path_info *pi);
extern struct bgp_path_info_extra *
bgp_path_info_extra_get(struct bgp_path_info *path);
extern struct bgp_path_info_extra *bgp_evpn_path_info_extra_get(struct bgp_path_info *path);
extern bool bgp_path_info_has_valid_label(const struct bgp_path_info *path);
extern void bgp_path_info_set_flag(struct bgp_dest *dest,
				   struct bgp_path_info *path, uint32_t flag);
extern void bgp_path_info_unset_flag(struct bgp_dest *dest,
				     struct bgp_path_info *path, uint32_t flag);
extern void bgp_path_info_path_with_addpath_rx_str(struct bgp_path_info *pi,
						   char *buf, size_t buf_len);
extern bool bgp_path_info_labels_same(const struct bgp_path_info *bpi,
				      const mpls_label_t *label, uint32_t n);

extern int bgp_nlri_parse_ip(struct peer *peer, struct attr *attr, struct bgp_nlri *packet);

extern bool bgp_maximum_prefix_overflow(struct peer *peer, afi_t afi, safi_t safi, int always);

extern void bgp_redistribute_add(struct bgp *bgp, struct prefix *p,
				 const union g_addr *nexthop, ifindex_t ifindex,
				 enum nexthop_types_t nhtype, uint8_t distance,
				 enum blackhole_type bhtype, uint32_t metric,
				 uint8_t type, unsigned short instance,
				 route_tag_t tag);
extern void bgp_redistribute_delete(struct bgp *bgp, struct prefix *p, uint8_t type,
				    unsigned short instance);
extern void bgp_redistribute_withdraw(struct bgp *bgp, afi_t afi, int type,
				      unsigned short instance);

extern void bgp_static_add(struct bgp *bgp);
extern void bgp_static_delete(struct bgp *bgp);
extern void bgp_address_family_distance_delete(void);
extern void bgp_static_redo_import_check(struct bgp *bgp);
extern void bgp_purge_static_redist_routes(struct bgp *bgp);
extern void bgp_static_update(struct bgp *bgp, const struct prefix *p,
			      struct bgp_static *s, afi_t afi, safi_t safi);
extern void bgp_static_withdraw(struct bgp *bgp, const struct prefix *p,
				afi_t afi, safi_t safi, struct prefix_rd *prd);

extern int bgp_static_set(struct vty *vty, bool negate, const char *ip_str,
			  const char *rd_str, const char *label_str, afi_t afi,
			  safi_t safi, const char *rmap, int backdoor,
			  uint32_t label_index, int evpn_type, const char *esi,
			  const char *gwip, const char *ethtag,
			  const char *routermac);

/* this is primarily for MPLS-VPN */
extern void bgp_update(struct peer *peer, const struct prefix *p,
		       uint32_t addpath_id, struct attr *attr, afi_t afi,
		       safi_t safi, int type, int sub_type,
		       struct prefix_rd *prd, mpls_label_t *label,
		       uint8_t num_labels, int soft_reconfig,
		       struct bgp_route_evpn *evpn);
extern void bgp_withdraw(struct peer *peer, const struct prefix *p,
			 uint32_t addpath_id, afi_t afi, safi_t safi, int type,
			 int sub_type, struct prefix_rd *prd,
			 mpls_label_t *label, uint8_t num_labels);
extern void bgp_update_check_valid_flags(struct bgp *bgp, struct peer *peer, struct bgp_dest *dest,
					 const struct prefix *p, afi_t afi, safi_t safi,
					 struct bgp_path_info *pi, struct attr *attr_new,
					 const struct prefix *bgp_nht_param_prefix,
					 bool accept_own);

/*
 * Add a route to be processed for bgp bestpath through the bgp
 * workqueue.  This route is added to the end of all other routes
 * queued for processing
 *
 * bgp_process_early adds the route for processing at the beginning
 * of the current queue for processing.
 */
/* bgp_process 把 dest 排到“队尾”（迟处理），bgp_process_early 插到“队首”（尽快
 * 处理）。凡是修改了 RIB 内容（加/删/改 path_info、改属性、下一跳变化）的地方，都不
 * 应直接算 bestpath，而应调用这两个函数把节点排队，由主循环在 bgp_process_main_one
 * 里统一选路——这是 BGP 把海量更新“批处理 + 去重”的核心手段。 */
extern void bgp_process(struct bgp *bgp, struct bgp_dest *dest,
			struct bgp_path_info *pi, afi_t afi, safi_t safi);

extern void bgp_process_early(struct bgp *bgp, struct bgp_dest *dest,
			      struct bgp_path_info *pi, afi_t afi, safi_t safi);

/* 向处理队列加入一个“初始更新结束(EOR)”标记元素：它只是队列元素，其 bgp 节点为空。
 * 用于在处理队列中标记某个批次的收尾。 */
/*
 * Add an end-of-initial-update marker to the process queue. This is just a
 * queue element with NULL bgp node.
 */
extern void bgp_add_eoiu_mark(struct bgp *bgp);
/* 配置写出：table-map(把 BGP 表映射进主 RIB 时套用的 route-map)。 */
extern void bgp_config_write_table_map(struct vty *vty, struct bgp *bgp, afi_t afi, safi_t safi);
/* 配置写出：network 语句(本地注入的前缀与掩码及可选 route-map)。 */
extern void bgp_config_write_network(struct vty *vty, struct bgp *bgp, afi_t afi, safi_t safi);
/* 配置写出：distance(各路由来源的管理距离 AD)。 */
extern void bgp_config_write_distance(struct vty *vty, struct bgp *bgp, afi_t afi, safi_t safi);

/* 删除某前缀上的聚合配置与聚合项。 */
extern void bgp_aggregate_delete(struct bgp *bgp, const struct prefix *p,
				 afi_t afi, safi_t safi,
				 struct bgp_aggregate *aggregate);
/* 按聚合配置处理一条路由：判断是否被聚合覆盖并生成或维护聚合路由，返回是否处理成功。 */
extern bool bgp_aggregate_route(struct bgp *bgp, const struct prefix *p,
				afi_t afi, safi_t safi,
				struct bgp_aggregate *aggregate);
/* 聚合计数递增：某条明细路径加入时更新所属聚合的路径计数与属性来源。 */
extern void bgp_aggregate_increment(struct bgp *bgp, const struct prefix *p,
				    struct bgp_path_info *path, afi_t afi,
				    safi_t safi);
/* 聚合计数递减：某条明细路径移除时更新所属聚合，必要时撤销聚合路由。 */
extern void bgp_aggregate_decrement(struct bgp *bgp, const struct prefix *p,
				    struct bgp_path_info *path, afi_t afi,
				    safi_t safi);

/* 依据 distance 配置计算该前缀与来源的管理距离值，返回取整后的距离。 */
extern uint8_t bgp_distance_apply(const struct prefix *p,
				  struct bgp_path_info *path, afi_t afi,
				  safi_t safi, struct bgp *bgp);

/* 从 vty 当前命令串推断 AFI(ipv4 或 ipv6)。 */
extern afi_t bgp_node_afi(struct vty *vty);
/* 从 vty 当前命令串推断 SAFI(unicast / multicast 等)。 */
extern safi_t bgp_node_safi(struct vty *vty);

/* 构造一个 bgp_path_info 节点(type / sub_type / instance / peer / attr / dest)，
 * 是向 dest 挂路径的统一工厂函数。 */
extern struct bgp_path_info *info_make(int type, int sub_type,
				       unsigned short instance,
				       struct peer *peer, struct attr *attr,
				       struct bgp_dest *dest);

/* 展示(show)单条路径：p / path / attr 等，display 控制打印级别，可输出到 json_paths；
 * wide 为宽格式，rd_str 用于 VPN 的 RD 列。 */
extern void route_vty_out(struct vty *vty, const struct prefix *p, struct bgp_path_info *path,
			  int display, struct attr *attr, safi_t safi, json_object *json_paths,
			  bool wide, char *rd_str);
/* 展示带标签聚合信息的单条路径(tag)，用于聚合场景的标签展示。 */
extern void route_vty_out_tag(struct vty *vty, const struct prefix *p,
			      struct bgp_path_info *path, int display,
			      safi_t safi, json_object *json);
/* 展示一条“临时”路径(其 attr 不挂在 dest 上)，用于调试或展示临时计算出的路由。 */
extern void route_vty_out_tmp(struct vty *vty, struct bgp *bgp,
			      struct bgp_dest *dest, const struct prefix *p,
			      struct attr *attr, safi_t safi, bool use_json,
			      json_object *json_ar, bool wide);
/* 展示 EVPN overlay 相关信息(如二层属性与 ESI 等)。 */
extern void route_vty_out_overlay(struct vty *vty, const struct prefix *p,
				  struct bgp_path_info *path, int display,
				  json_object *json);

/* 条件通告扫描：按配置的条件(conditional advertisement)重新评估某 subgroup 的通告
 * (存在性路由发生变化时触发)。 */
extern void bgp_notify_conditional_adv_scanner(struct update_subgroup *subgrp);

/* 对某 subgroup 中被选中的最佳路径执行通告处理(编码并发送该前缀)。 */
extern void subgroup_process_announce_selected(struct update_subgroup *subgrp,
					       struct bgp_path_info *selected,
					       struct bgp_dest *dest, afi_t afi,
					       safi_t safi,
					       uint32_t addpath_tx_id);

/* 通告前的检查与属性调整钩子：对将要通告给 subgroup 的路径做过滤与属性改写，
 * 返回是否允许通告，post_attr 传出改写后的属性。 */
extern bool subgroup_announce_check(struct bgp_dest *dest,
				    struct bgp_path_info *pi,
				    struct update_subgroup *subgrp,
				    const struct prefix *p, struct attr *attr,
				    struct attr *post_attr);

/* 在(encap 与 VPN 场景的)bgp_table 中查找某 SAFI 下带 RD 的前缀节点(dest)。 */
/* for encap/vpn */
extern struct bgp_dest *bgp_safi_node_lookup(struct bgp_table *table,
					     safi_t safi,
					     const struct prefix *p,
					     struct prefix_rd *prd);
/* 把此前被移出或暂存的路径信息 path 恢复到 dest(用于撤销抑制等回滚场景)。 */
extern void bgp_path_info_restore(struct bgp_dest *dest,
				  struct bgp_path_info *path);

/* 兼容版路径比较：逻辑同 bgp_path_info_cmp 但接口不同(返回 int)，供旧调用点使用，
 * 判定新路径相对现有路径的选路结果并写出原因。 */
extern int bgp_path_info_cmp_compatible(struct bgp *bgp,
					struct bgp_path_info *new,
					struct bgp_path_info *exist,
					char *pfx_buf, afi_t afi, safi_t safi,
					enum bgp_path_selection_reason *reason);
/* 给属性添加 LLGR(Long-Lived GR)团体，用于超长优雅重启场景的标记。 */
extern void bgp_attr_add_llgr_community(struct attr *attr);
/* 给属性添加 GSHUT(graceful shutdown)团体，表示本端正在优雅退出。 */
extern void bgp_attr_add_gshut_community(struct attr *attr);

/* 核心选路：对 dest 上所有 path_info 按 BGP 选路规则选出最佳(可含多路径)，结果写入
 * result；mpath_cfg 提供最大多路径配置，afi 与 safi 供展示与区分。 */
extern void bgp_best_selection(struct bgp *bgp, struct bgp_dest *dest,
			       struct bgp_maxpaths_cfg *mpath_cfg,
			       struct bgp_path_info_pair *result, afi_t afi,
			       safi_t safi);
/* 清除 dest 上记录“发给 zebra 的路由是否发生变化”的标志位。 */
extern void bgp_zebra_clear_route_change_flags(struct bgp_dest *dest);
/* 判断被选中的路径是否需要向 zebra 重新下发(比对变化标志)。 */
extern bool bgp_zebra_has_route_changed(struct bgp_path_info *selected);

/* 展示详细路由(如 show ip bgp 加前缀)的表头部分：前缀、RD、AFI 与 SAFI 等；
 * incremental_print 控制增量展示，local_table 表示本地表。 */
extern void route_vty_out_detail_header(struct vty *vty, struct bgp *bgp,
					struct bgp_dest *dest,
					const struct prefix *p,
					const struct prefix_rd *prd, afi_t afi,
					safi_t safi, json_object *json,
					bool incremental_print,
					bool local_table);
/* 展示详细路由的正文：单条 path 的完整属性与选路信息，show_opts 控制展示范围。 */
extern void route_vty_out_detail(struct vty *vty, struct bgp *bgp, struct bgp_dest *bn,
				 const struct prefix *p, struct bgp_path_info *path, afi_t afi,
				 safi_t safi, enum rpki_states, json_object *json_paths,
				 struct attr *attr, uint16_t show_opts);
/* 展示带 RD 的表的完整内容(如 show ip bgp vpnv4 rd ...)，按 type 选择过滤条件。 */
extern int bgp_show_table_rd(struct vty *vty, struct bgp *bgp, afi_t afi, safi_t safi,
			     struct bgp_table *table, struct prefix_rd *prd,
			     enum bgp_show_type type, void *output_arg,
			     uint16_t show_flags);
/* 执行此前被延迟的路径选择(批处理收敛后统一选路)。 */
extern void bgp_do_deferred_path_selection(struct bgp *bgp, afi_t afi, safi_t safi);
/* GR 期间：dest 的“待安装到 FIB 计数”加一，用于判断 GR 安装是否完成。 */
extern void bgp_dest_increment_gr_fib_install_pending_count(struct bgp_dest *dest);
/* GR 期间：dest 的“待安装到 FIB 计数”减一。 */
extern void bgp_dest_decrement_gr_fib_install_pending_count(struct bgp_dest *dest);
/* GR 延迟完成通知：某 AFI 与 SAFI 的 GR 延迟定时器到期后的收尾处理。 */
extern void bgp_process_gr_deferral_complete(struct bgp *bgp, afi_t afi, safi_t safi);
/* 判断并(在处理时)丢弃“火星”下一跳(如本机地址、组播、非法前缀)的路由更新，
 * 返回该下一跳是否属于火星地址。 */
extern bool bgp_update_martian_nexthop(struct bgp *bgp, afi_t afi, safi_t safi,
				       uint8_t type, uint8_t stype,
				       struct attr *attr, struct bgp_dest *dest);
/* EVPN 专用路径比较：在 EVPN 场景下比较新与旧路径，paths_eq 输出二者是否等价。 */
extern int bgp_evpn_path_info_cmp(struct bgp *bgp, struct bgp_path_info *new,
				  struct bgp_path_info *exist, int *paths_eq,
				  bool debug);
/* 切换某聚合的“抑制(suppressed)”状态：suppress 为真表示抑制其明细通告。 */
extern void bgp_aggregate_toggle_suppressed(struct bgp_aggregate *aggregate,
					    struct bgp *bgp,
					    const struct prefix *p, afi_t afi,
					    safi_t safi, bool suppress);
/* 通告前把属性里的下一跳重置(如重置为自表下一跳)，family 指示地址族。 */
extern void subgroup_announce_reset_nhop(uint8_t family, struct attr *attr);
/* 把选路原因枚举转成可读字符串(用于展示与日志)。 */
const char *
bgp_path_selection_reason2str(enum bgp_path_selection_reason reason);
/* 判断某路径是否处于被抑制状态(即不向对端通告)。 */
extern bool bgp_path_suppressed(struct bgp_path_info *pi);
/* 判断对该 peer 在某 AFI 与 SAFI 下，接收方向是否需要按 ADD-PATH 解码(带 path id)。 */
extern bool bgp_addpath_encode_rx(struct peer *peer, afi_t afi, safi_t safi);
/* 从 dest 取出其所属表的 RD(路由区分符)，safi 辅助定位。 */
extern const struct prefix_rd *bgp_rd_from_dest(const struct bgp_dest *dest,
						safi_t safi);
/* 释放一条路径信息(带调用者名字用于诊断)；通常经宏 bgp_path_info_free 调用。 */
extern void bgp_path_info_free_with_caller(const char *caller,
					   struct bgp_path_info *path);
/* 把路径信息 pi 挂到 dest；通常经宏 bgp_path_info_add 调用。 */
extern void bgp_path_info_add_with_caller(const char *caller,
					  struct bgp_dest *dest,
					  struct bgp_path_info *pi);
/* 释放一个聚合结构。 */
extern void bgp_aggregate_free(struct bgp_aggregate *aggregate);
/* BGP 选路比较的核心：比较 new 与 exist 两条路径，输出 paths_eq(是否等价)并给出
 * 选路原因 reason；mpath_cfg 提供最大多路径配置。 */
extern int bgp_path_info_cmp(struct bgp *bgp, struct bgp_path_info *new,
			     struct bgp_path_info *exist, int *paths_eq,
			     struct bgp_maxpaths_cfg *mpath_cfg, bool debug,
			     char *pfx_buf, afi_t afi, safi_t safi,
			     enum bgp_path_selection_reason *reason);
/* 便捷宏：自动带上 __func__ 作为 caller 调用 _with_caller 版本，便于定位调用点。 */
#define bgp_path_info_add(A, B)                                                \
	bgp_path_info_add_with_caller(__func__, (A), (B))
/* 便捷宏：自动带上 __func__ 调用 bgp_path_info_free_with_caller。 */
#define bgp_path_info_free(B) bgp_path_info_free_with_caller(__func__, (B))
/* 释放处理用的 meta 队列。 */
extern void bgp_meta_queue_free(struct meta_queue *mq);
/* 处理“队首优先”的路由(由 bgp_process_early 入队的 dest)。 */
extern int early_route_process(struct bgp *bgp, struct bgp_dest *dest);
/* 处理普通(队尾)入队的路由。 */
extern int other_route_process(struct bgp *bgp, struct bgp_dest *dest);
/* 处理 EOR 标记队列元素(由 bgp_add_eoiu_mark 入队)。 */
extern int eoiu_marker_process(struct bgp *bgp, struct bgp_dest *dest);
/* 计算属性在本 bgp 下的 MED 取值(考虑 missing-med 默认策略等)。 */
extern uint32_t bgp_med_value(struct attr *attr, struct bgp *bgp);
/* 设置或清除 dest 的延迟(defer)标志，delete 指示是否为删除路径场景。 */
extern int bgp_dest_set_defer_flag(struct bgp_dest *dest, bool delete);
/* 处理队列中单个 dest 的主函数：执行选路并驱动与 zebra 的同步等。 */
extern void bgp_process_main_one(struct bgp *bgp, struct bgp_dest *dest, afi_t afi, safi_t safi);
#endif /* _QUAGGA_BGP_ROUTE_H */
