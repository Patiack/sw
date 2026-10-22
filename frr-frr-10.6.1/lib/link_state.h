// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Link State Database definition - ted.h
 *
 * Author: Olivier Dugeon <olivier.dugeon@orange.com>
 *
 * Copyright (C) 2020 Orange http://www.orange.com
 *
 * This file is part of Free Range Routing (FRR).
 */

/*
 * ============================ 文件导读 ============================
 * 本文件定义了一套通用的“链路状态数据库(LSDB)”抽象模型，供多种
 * 上层协议复用：RSVP-TE、BGP-LS、PCEP 等。数据库的填充者通常是
 * 链路状态路由协议(OSPF 或 IS-IS)，它们把流量工程(TE)信息承载在
 * 链路状态属性里。相关标准见 RFC3630(OSPF-TE)、RFC5305(IS-IS-TE)；
 * BGP-LS 的封装见 RFC7752。
 *
 * 核心抽象只有三种，正好对应 BGP-LS 所处理的三种对象：
 *   - ls_node       节点(node)：一台路由器或伪节点的整体信息
 *   - ls_attributes 属性(attributes)：一条链路或邻接的信息
 *   - ls_prefix     前缀(prefix)：一个可达前缀的信息
 *
 * 这三种结构中，每种都内嵌一个 ls_node_id 类型的 adv 字段，表示
 * “是谁广告了这条链路状态”；并各用一个位掩码 flags 标记“哪些字段
 * 是有效的”。上层协议可以只填充自己支持的字段，其余保持未设置，
 * 读取方先查 flags 再决定是否信任该字段。
 *
 * ls_node_id 是跨协议的节点标识，用联合体(union)同时容纳两种编码：
 *   - OSPF：IPv4 Router-ID 加 Area-ID
 *   - IS-IS：ISO System-ID(6 字节) 加 Level(L1 / L2)
 * 由 origin 字段区分当前生效的是哪一种编码。
 *
 * 与各协议私有数据的挂载方式：本库自身不解析任何协议报文，只提供
 * 容器与比较、复制等通用操作。ospfd / isisd 负责把各自的私有结构体
 * 转换成这里的 ls_node / ls_attributes / ls_prefix，并通过 zclient
 * 的 opaque 消息机制注册进来，由本库统一维护入 LSDB，再供 BGP-LS
 * 等消费者导出。因此本文件中看不到任何 OSPF / IS-IS 私有类型，
 * 只保留通用字段与 flags。
 * ================================================================
 */

#ifndef _FRR_LINK_STATE_H_
#define _FRR_LINK_STATE_H_

#include "admin_group.h"
#include "typesafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This file defines the model used to implement a Link State Database
 * suitable to be used by various protocol like RSVP-TE, BGP-LS, PCEP ...
 * This database is normally fulfill by the link state routing protocol,
 * commonly OSPF or ISIS, carrying Traffic Engineering information within
 * Link State Attributes. See, RFC3630.(OSPF-TE) and RFC5305 (ISIS-TE).
 *
 * At least, 3 types of Link State structure are defined:
 *  - Link State Node that groups all information related to a node
 *  - Link State Attributes that groups all information related to a link
 *  - Link State Prefix that groups all information related to a prefix
 *
 * These 3 types of structures are those handled by BGP-LS (see RFC7752).
 *
 * Each structure, in addition to the specific parameters, embed the node
 * identifier which advertises the Link State and a bit mask as flags to
 * indicates which parameters are valid i.e. for which the value corresponds
 * to a Link State information convey by the routing protocol.
 * Node identifier is composed of the route id as IPv4 address plus the area
 * id for OSPF and the ISO System id plus the IS-IS level for IS-IS.
 */

/* external reference */
struct zapi_opaque_reg_info;
struct zclient;

/*
 * 通用常量：
 *   MAX_NAME_LENGTH  节点 / 链路名称缓冲区的最大字节数(含结尾 0)
 *   ISO_SYS_ID_LEN   IS-IS ISO System-ID 的固定长度(6 字节)
 */
/* Link State Common definitions */
#define MAX_NAME_LENGTH		256
#define ISO_SYS_ID_LEN		6

/*
 * 节点类型枚举。用于 ls_node.type，表示这个节点在图中的角色：
 *   NONE     未知类型(默认值)
 *   STANDARD 普通内部节点(P 或 PE)
 *   ABR      OSPF 区域边界路由器
 *   ASBR     OSPF 自治系统边界路由器
 *   RMT_ASBR 远端 ASBR，表示跨域对端
 *   PSEUDO   伪节点，代表广播网段本身
 */
/* Type of Node */
enum ls_node_type {
	NONE = 0,	/* Unknown */
	STANDARD,	/* a P or PE node */
	ABR,		/* an Array Border Node */
	ASBR,		/* an Autonomous System Border Node */
	RMT_ASBR,	/* Remote ASBR */
	PSEUDO		/* a Pseudo Node */
};

/*
 * 链路状态信息的来源，用于 ls_node_id.origin，决定 id 联合体里
 * 实际填的是 ip 分支还是 iso 分支：
 *   UNKNOWN 未知
 *   ISIS_L1 IS-IS Level-1
 *   ISIS_L2 IS-IS Level-2
 *   OSPFv2  OSPFv2
 *   DIRECT  直连(手工配置)
 *   STATIC  静态(手工配置)
 */
/* Origin of the Link State information */
enum ls_origin { UNKNOWN = 0, ISIS_L1, ISIS_L2, OSPFv2, DIRECT, STATIC };

/*
 * 跨协议的节点标识。origin 决定联合体 id 中哪个分支有效：
 *   - origin 为 OSPFv2 时用 id.ip：addr(OSPF Router-ID) 加 area_id
 *   - origin 为 ISIS_L1 / ISIS_L2 时用 id.iso：sys_id 加 level
 * 该结构常按值传递(如 ls_node_id_same)，注意其大小与对齐；
 * iso 分支末尾的 padding 仅是占位对齐字段，没有实际语义。
 */
/**
 * Link State Node Identifier as:
 *  - IPv4 address + Area ID for OSPF
 *  - ISO System ID + ISIS Level for ISIS
 */
struct ls_node_id {
	enum ls_origin origin;		/* Origin of the LS information */
	union {
		struct {
			struct in_addr addr;		/* OSPF Router IS */
			struct in_addr area_id;		/* OSPF Area ID */
		} ip;
		struct {
			uint8_t sys_id[ISO_SYS_ID_LEN];	/* ISIS System ID */
			uint8_t level;			/* ISIS Level */
			uint8_t padding;
		} iso;
	} id;
};

/**
 * Check if two Link State Node IDs are equal. Note that this routine has the
 * same return value sense as '==' (which is different from a comparison).
 *
 * @param i1	First Link State Node Identifier
 * @param i2	Second Link State Node Identifier
 * @return	1 if equal, 0 otherwise
 */
extern int ls_node_id_same(struct ls_node_id i1, struct ls_node_id i2);

/*
 * 本库支持的 SR(段路由)算法数量。ls_node.algo 数组按此长度分配，
 * 下标分别对应算法 0(SPF)与算法 1(Strict SPF)的取值。
 */
/* Supported number of algorithm by the link-state library */
#define LIB_LS_SR_ALGO_COUNT 2

/*
 * 节点参数的“有效性位掩码”定义，按位与 ls_node.flags 相与即可判断
 * 某个字段是否已被填充。每一位对应 ls_node 里的一组成员，例如
 * LS_NODE_NAME 对应 name，LS_NODE_SR 对应 srgb / srlb / algo 等
 * Segment Routing Global Block 信息，LS_NODE_SRV6 对应 srv6_msd 等。
 * LS_NODE_UNSET 表示所有位都为 0，即没有任何有效字段。
 */
/* Link State flags to indicate which Node parameters are valid */
#define LS_NODE_UNSET		0x0000
#define LS_NODE_NAME		0x0001
#define LS_NODE_ROUTER_ID	0x0002
#define LS_NODE_ROUTER_ID6	0x0004
#define LS_NODE_FLAG		0x0008
#define LS_NODE_TYPE		0x0010
#define LS_NODE_AS_NUMBER	0x0020
#define LS_NODE_SR		0x0040
#define LS_NODE_SRLB		0x0080
#define LS_NODE_MSD		0x0100
#define LS_NODE_SRV6		0x0200

/*
 * 链路状态节点：一台路由器(或伪节点)的完整画像。
 *   flags      有效性位掩码，标记下面哪些字段可信(见 LS_NODE_* 宏)
 *   adv        广告该节点的节点标识(OSPF 或 IS-IS)
 *   name       节点名称，仅 IS-IS 使用
 *   router_id  IPv4 Router-ID；router_id6 为 IPv6 Router-ID
 *   node_flag  IS-IS / OSPF 的原始节点标志位
 *   type       节点类型(见 enum ls_node_type)
 *   as_number  本端或邻居的 AS 号
 *   srgb/srlb  SRGB 与 SRLB 段路由标签块；algo 为支持的 SR 算法
 *   msd        最大栈深(Maximum Stack Depth)
 *   srv6_*     BGP-LS SRv6 扩展(draft-ietf-idr-bgpls-srv6-ext)
 * 结构体定长，构造时整体清零，因此未填字段为 0；调用方必须结合
 * flags 判断字段有效性，不能单凭字段值是否为 0 下结论。
 */
/* Link State Node structure */
struct ls_node {
	uint16_t flags;			/* Flag for parameters validity */
	struct ls_node_id adv;		/* Adv. Router of this Link State */
	char name[MAX_NAME_LENGTH];	/* Name of the Node (IS-IS only) */
	struct in_addr router_id;	/* IPv4 Router ID */
	struct in6_addr router_id6;	/* IPv6 Router ID */
	uint8_t node_flag;		/* IS-IS or OSPF Node flag */
	enum ls_node_type type;		/* Type of Node */
	uint32_t as_number;		/* Local or neighbor AS number */
	struct ls_srgb {		/* Segment Routing Global Block */
		uint32_t lower_bound;		/* MPLS label lower bound */
		uint32_t range_size;		/* MPLS label range size */
		uint8_t flag;			/* IS-IS SRGB flags */
	} srgb;
	struct ls_srlb {		/* Segment Routing Local Block */
		uint32_t lower_bound;		/* MPLS label lower bound */
		uint32_t range_size;		/* MPLS label range size */
	} srlb;
	uint8_t algo[LIB_LS_SR_ALGO_COUNT]; /* Segment Routing Algorithms */
	uint8_t msd;			/* Maximum Stack Depth */

	uint16_t srv6_cap_flags; /* draft-ietf-idr-bgpls-srv6-ext, 3.1., flags field */
	struct ls_srv6_msd { /* draft-ietf-idr-bgpls-srv6-ext, 3.2. */
		uint8_t max_seg_left_msd;
		uint8_t max_end_pop_msd;
		uint8_t max_h_encaps_msd;
		uint8_t max_end_d_msd;
	} srv6_msd;
};

/*
 * 链路属性(Attributes)参数的“有效性位掩码”，与 ls_attributes.flags
 * 配合使用。每一位对应结构体里的一组成员：standard 下的 TE 度量、
 * 管理组、本端与邻居地址、带宽等，extended 下的时延 / 抖动 / 丢包
 * 与可用带宽等，以及各种邻接 SID 与 SRLG。
 * LS_ATTR_UNSET 表示无任何有效字段。
 */
/* Link State flags to indicate which Attribute parameters are valid */
#define LS_ATTR_UNSET		0x00000000U
#define LS_ATTR_NAME		0x00000001U
#define LS_ATTR_METRIC		0x00000002U
#define LS_ATTR_TE_METRIC	0x00000004U
#define LS_ATTR_ADM_GRP		0x00000008U
#define LS_ATTR_LOCAL_ADDR	0x00000010U
#define LS_ATTR_NEIGH_ADDR	0x00000020U
#define LS_ATTR_LOCAL_ADDR6	0x00000040U
#define LS_ATTR_NEIGH_ADDR6	0x00000080U
#define LS_ATTR_LOCAL_ID	0x00000100U
#define LS_ATTR_NEIGH_ID	0x00000200U
#define LS_ATTR_MAX_BW		0x00000400U
#define LS_ATTR_MAX_RSV_BW	0x00000800U
#define LS_ATTR_UNRSV_BW	0x00001000U
#define LS_ATTR_REMOTE_AS	0x00002000U
#define LS_ATTR_REMOTE_ADDR	0x00004000U
#define LS_ATTR_REMOTE_ADDR6	0x00008000U
#define LS_ATTR_DELAY		0x00010000U
#define LS_ATTR_MIN_MAX_DELAY	0x00020000U
#define LS_ATTR_JITTER		0x00040000U
#define LS_ATTR_PACKET_LOSS	0x00080000U
#define LS_ATTR_AVA_BW		0x00100000U
#define LS_ATTR_RSV_BW		0x00200000U
#define LS_ATTR_USE_BW		0x00400000U
#define LS_ATTR_ADJ_SID		0x01000000U
#define LS_ATTR_BCK_ADJ_SID	0x02000000U
#define LS_ATTR_ADJ_SID6	0x04000000U
#define LS_ATTR_BCK_ADJ_SID6	0x08000000U
#define LS_ATTR_SRLG		0x10000000U
#define LS_ATTR_EXT_ADM_GRP	0x20000000U
#define LS_ATTR_ADJ_SRV6SID	0x40000000U
#define LS_ATTR_BCK_ADJ_SRV6SID 0x80000000U

/*
 * 链路状态属性：描述一条链路或一个邻接的 TE 信息集合。
 *   flags      有效性位掩码(见 LS_ATTR_* 宏)
 *   adv        广告该属性的节点标识
 *   name       链路名称，可能为空
 *   metric     IGP 标准度量
 *   standard   标准 TE 度量组：te_metric、admin_group、本端与邻居的
 *              IPv4 / IPv6 地址、本地与远端标识、最大带宽、最大可
 *              预留带宽，以及各 CT 的未预留带宽 unrsv_bw 等
 *   extended   扩展 TE 度量组：单向时延(平均 / 最小 / 最大)、抖动、
 *              丢包率，以及可用 / 预留 / 已用带宽
 *   adj_sid[]  4 个邻接 SID，按 IPv4 / IPv6 与 Primary / Backup 组合
 *   adj_srv6_sid[] 2 个 SRv6 邻接 SID(IS-IS)
 *   srlgs      共享风险链路组(SRLG)列表；srlg_len 为元素个数
 * unrsv_bw 固定 8 个元素，分别对应 8 个 Class Type(CT0..CT7)。
 */
/* Link State Attributes */
struct ls_attributes {
	uint32_t flags;			/* Flag for parameters validity */
	struct ls_node_id adv;		/* Adv. Router of this Link State */
	char name[MAX_NAME_LENGTH];	/* Name of the Edge. Could be null */
	uint32_t metric;		/* IGP standard metric */
	struct ls_standard {		/* Standard TE metrics */
		uint32_t te_metric;		/* Traffic Engineering metric */
		uint32_t admin_group;		/* Administrative Group */
		struct in_addr local;		/* Local IPv4 address */
		struct in_addr remote;		/* Remote IPv4 address */
		struct in6_addr local6;		/* Local IPv6 address */
		struct in6_addr remote6;	/* Remote IPv6 address */
		uint32_t local_id;		/* Local Identifier */
		uint32_t remote_id;		/* Remote Identifier */
		float max_bw;			/* Maximum Link Bandwidth */
		float max_rsv_bw;		/* Maximum Reservable BW */
		float unrsv_bw[8];		/* Unreserved BW per CT (8) */
		uint32_t remote_as;		/* Remote AS number */
		struct in_addr remote_addr;	/* Remote IPv4 address */
		struct in6_addr remote_addr6;	/* Remote IPv6 address */
	} standard;
	struct ls_extended {		/* Extended TE Metrics */
		uint32_t delay;		/* Unidirectional average delay */
		uint32_t min_delay;	/* Unidirectional minimum delay */
		uint32_t max_delay;	/* Unidirectional maximum delay */
		uint32_t jitter;	/* Unidirectional delay variation */
		uint32_t pkt_loss;	/* Unidirectional packet loss */
		float ava_bw;		/* Available Bandwidth */
		float rsv_bw;		/* Reserved Bandwidth */
		float used_bw;		/* Utilized Bandwidth */
	} extended;
	struct admin_group ext_admin_group; /* Extended Admin. Group */
	/*
	 * adj_sid 数组的下标常量：IPv4 / IPv6 与 Primary / Backup 的
	 * 四种组合，共 LS_ADJ_MAX(4) 个。
	 */
#define ADJ_PRI_IPV4	0
#define ADJ_BCK_IPV4	1
#define ADJ_PRI_IPV6	2
#define ADJ_BCK_IPV6	3
#define LS_ADJ_MAX	4
	struct ls_adjacency {		/* (LAN)-Adjacency SID for OSPF */
		uint32_t sid;		/* SID as MPLS label or index */
		uint8_t flags;		/* Flags */
		uint8_t weight;		/* Administrative weight */
		union {
			struct in_addr addr;	/* Neighbor @IP for OSPF */
			uint8_t sysid[ISO_SYS_ID_LEN]; /* or Sys-ID for ISIS */
		} neighbor;
	} adj_sid[4];		/* IPv4/IPv6 & Primary/Backup (LAN)-Adj. SID */
	/*
	 * adj_srv6_sid 数组的下标常量：SRv6 目前只有 IPv6 且区分
	 * Primary / Backup，共 ADJ_SRV6_MAX(2) 个。
	 */
#define ADJ_SRV6_PRI_IPV6 0
#define ADJ_SRV6_BCK_IPV6 1
#define ADJ_SRV6_MAX	  2
	struct ls_srv6_adjacency {	    /* Adjacency SID for IS-IS */
		struct in6_addr sid;	    /* SID as IPv6 address */
		uint8_t flags;		    /* Flags */
		uint8_t weight;		    /* Administrative weight */
		uint16_t endpoint_behavior; /* Endpoint Behavior */
		union {
			uint8_t sysid[ISO_SYS_ID_LEN]; /* Sys-ID for ISIS */
		} neighbor;
	} adj_srv6_sid[2];
	uint32_t *srlgs;	/* List of Shared Risk Link Group */
	uint8_t srlg_len;	/* number of SRLG in the list */
};

/*
 * 前缀(Prefix)参数的“有效性位掩码”，作用同 LS_NODE_* 与 LS_ATTR_*，
 * 但 ls_prefix.flags 只有一个字节，故用 0x01..0x20 表示各字段是否
 * 有效：IGP 标志、路由标签、扩展标签、度量、SR SID、SRv6 SID。
 * LS_PREF_UNSET 表示无任何有效字段。
 */
/* Link State flags to indicate which Prefix parameters are valid */
#define LS_PREF_UNSET		0x00
#define LS_PREF_IGP_FLAG	0x01
#define LS_PREF_ROUTE_TAG	0x02
#define LS_PREF_EXTENDED_TAG	0x04
#define LS_PREF_METRIC		0x08
#define LS_PREF_SR		0x10
#define LS_PREF_SRV6		0x20

/*
 * 链路状态前缀：一个可达前缀(IPv4 / IPv6)及其附带属性。
 *   flags        有效性位掩码(见 LS_PREF_* 宏)
 *   adv          广告该前缀的节点标识
 *   pref         通用前缀结构(地址加掩码长度)
 *   igp_flag     IGP 前缀标志
 *   route_tag    IGP 路由标签；extended_tag 为扩展路由标签(64 位)
 *   metric       该前缀的路由度量
 *   sr           SR SID(标签或索引)、SID 标志位与算法
 *   srv6         SRv6 SID(IPv6 地址)、绑定的端点行为与标志位
 */
/* Link State Prefix */
struct ls_prefix {
	uint8_t flags;			/* Flag for parameters validity */
	struct ls_node_id adv;		/* Adv. Router of this Link State */
	struct prefix pref;		/* IPv4 or IPv6 prefix */
	uint8_t igp_flag;		/* IGP Flags associated to the prefix */
	uint32_t route_tag;		/* IGP Route Tag */
	uint64_t extended_tag;		/* IGP Extended Route Tag */
	uint32_t metric;		/* Route metric for this prefix */
	struct ls_sid {
		uint32_t sid;		/* Segment Routing ID */
		uint8_t sid_flag;	/* Segment Routing Flags */
		uint8_t algo;		/* Algorithm for Segment Routing */
	} sr;
	struct ls_srv6_sid {
		struct in6_addr sid; /* Segment Routing ID */
		uint16_t behavior;   /* Endpoint behavior bound to the SID */
		uint8_t flags;	     /* Flags */
	} srv6;
};

/*
 * 新建一个链路状态节点(结构体动态分配)。
 *   adv    必填，广告该节点的节点标识(OSPF 或 IS-IS)
 *   rid    节点自身的 IPv4 Router-ID(可为 0)
 *   rid6   节点自身的 IPv6 Router-ID(可为 0)
 * 返回已初始化(清零并对 rid / rid6 赋值)的新 ls_node；其余字段由
 * 调用方按需填充，并用 flags 标记有效性。用 ls_node_del 释放。
 */
/**
 * Create a new Link State Node. Structure is dynamically allocated.
 *
 * @param adv	Mandatory Link State Node ID i.e. advertise router information
 * @param rid	Router ID as IPv4 address
 * @param rid6	Router ID as IPv6 address
 *
 * @return	New Link State Node
 */
extern struct ls_node *ls_node_new(struct ls_node_id adv, struct in_addr rid,
				   struct in6_addr rid6);

/*
 * 释放链路状态节点。node 必须是 ls_node_new 之类的分配结果；函数内
 * 部 free 后会将调用者的指针置为 NULL，切勿对同一指针重复调用。
 */
/**
 * Remove Link State Node. Data structure is freed.
 *
 * @param node	      Pointer to a valid Link State Node structure
 */
extern void ls_node_del(struct ls_node *node);

/*
 * 判断两个节点是否“相等”：比较广告节点标识、Router-ID、名称、类型
 * 等关键字段，而非比较指针。返回语义与 '==' 相同——相等返回 1，
 * 否则返回 0。
 */
/**
 * Check if two Link State Nodes are equal. Note that this routine has the same
 * return value sense as '==' (which is different from a comparison).
 *
 * @param n1	First Link State Node to be compare
 * @param n2	Second Link State Node to be compare
 *
 * @return	1 if equal, 0 otherwise
 */
extern int ls_node_same(struct ls_node *n1, struct ls_node *n2);

/*
 * 新建一组链路状态属性(动态分配)。
 *   adv       必填，广告该属性的节点标识
 *   local     本端 IPv4 地址
 *   local6    本端 IPv6 地址
 *   local_id  本端标识
 * 要求 local / local6 / local_id 中至少有一个有效且不为 0，否则
 * 返回 NULL。用 ls_attributes_del 释放。
 */
/**
 * Create a new Link State Attributes. Structure is dynamically allocated.
 * At least one of parameters MUST be valid and not equal to 0.
 *
 * @param adv		Mandatory Link State Node ID i.e. advertise router ID
 * @param local		Local IPv4 address
 * @param local6	Local Ipv6 address
 * @param local_id	Local Identifier
 *
 * @return		New Link State Attributes
 */
extern struct ls_attributes *ls_attributes_new(struct ls_node_id adv,
					       struct in_addr local,
					       struct in6_addr local6,
					       uint32_t local_id);

/*
 * 若属性中已分配 SRLG 列表(srlgs)，则释放该列表并把 srlgs 置空、
 * srlg_len 清零；不会释放属性结构体本身。
 */
/**
 * Remove SRLGs from Link State Attributes if defined.
 *
 * @param attr	Pointer to a valid Link State Attribute structure
 */
extern void ls_attributes_srlg_del(struct ls_attributes *attr);

/*
 * 释放一组链路状态属性：先释放内部 SRLG 列表，再 free 结构体本身，
 * 并把指针置为 NULL。
 */
/**
 * Remove Link State Attributes. Data structure is freed.
 *
 * @param attr	Pointer to a valid Link State Attribute structure
 */
extern void ls_attributes_del(struct ls_attributes *attr);

/*
 * 判断两组属性是否相等：比较本端与邻居地址、度量、带宽等字段(仅
 * 在对应 flags 位有效时才比较)。返回语义同 '=='——相等返回 1，
 * 否则返回 0。
 */
/**
 * Check if two Link State Attributes are equal. Note that this routine has the
 * same return value sense as '==' (which is different from a comparison).
 *
 * @param a1	First Link State Attributes to be compare
 * @param a2	Second Link State Attributes to be compare
 *
 * @return	1 if equal, 0 otherwise
 */
extern int ls_attributes_same(struct ls_attributes *a1,
			      struct ls_attributes *a2);

/*
 * 新建一个链路状态前缀(动态分配)。
 *   adv  必填，广告该前缀的节点标识
 *   p    必填，前缀(地址加掩码长度)
 * 返回已初始化的新 ls_prefix，用 ls_prefix_del 释放。
 */
/**
 * Create a new Link State Prefix. Structure is dynamically allocated.
 *
 * @param adv	Mandatory Link State Node ID i.e. advertise router ID
 * @param p	Mandatory Prefix
 *
 * @return	New Link State Prefix
 */
extern struct ls_prefix *ls_prefix_new(struct ls_node_id adv, struct prefix *p);

/*
 * 释放链路状态前缀结构体，并把调用者的指针置为 NULL。
 */
/**
 * Remove Link State Prefix. Data Structure is freed.
 *
 * @param pref	Pointer to a valid Link State Attribute Prefix.
 */
extern void ls_prefix_del(struct ls_prefix *pref);

/*
 * 判断两个前缀是否相等：前缀本身与关键属性一致即相等。返回语义同
 * '=='——相等返回 1，否则返回 0。
 */
/**
 * Check if two Link State Prefix are equal. Note that this routine has the
 * same return value sense as '==' (which is different from a comparison).
 *
 * @param p1	First Link State Prefix to be compare
 * @param p2	Second Link State Prefix to be compare
 *
 * @return	1 if equal, 0 otherwise
 */
extern int ls_prefix_same(struct ls_prefix *p1, struct ls_prefix *p2);

/*
 * 图(Graph)模型：叠加在链路状态数据库之上的一层“有向图”视图，便于
 * 实现路径计算(例如 CSPF)。记为 G(V, E):
 *   - V(vertex) 网络节点
 *   - E(edge)   节点之间的链路；边是有向的，因此一条双向链路要用
 *               两条方向相反的边来建模
 *   - P(prefix) 前缀列表，附加在广告该前缀的顶点上
 * 每个顶点维护自己的出边表(outgoing_edges)与入边表(incoming_edges)；
 * 每条边记录其源顶点与目的顶点。顶点与边都以唯一的 64 位 key 标识。
 */
/**
 * In addition a Graph model is defined as an overlay on top of link state
 * database in order to ease Path Computation algorithm implementation.
 * Denoted G(V, E), a graph is composed by a list of Vertices (V) which
 * represents the network Node and a list of Edges (E) which represents node
 * Link. An additional list of prefixes (P) is also added.
 * A prefix (P) is also attached to the Vertex (V) which advertise it.
 *
 * Vertex (V) contains the list of outgoing Edges (E) that connect this Vertex
 * with its direct neighbors and the list of incoming Edges (E) that connect
 * the direct neighbors to this Vertex. Indeed, the Edge (E) is unidirectional,
 * thus, it is necessary to add 2 Edges to model a bidirectional relation
 * between 2 Vertices.
 *
 * Edge (E) contains the source and destination Vertex that this Edge
 * is connecting.
 *
 * A unique Key is used to identify both Vertices and Edges within the Graph.
 * An easy way to build this key is to used the IP address: i.e. loopback
 * address for Vertices and link IP address for Edges.
 *
 *      --------------     ---------------------------    --------------
 *      | Connected  |---->| Connected Edge Va to Vb |--->| Connected  |
 *  --->|  Vertex    |     ---------------------------    |  Vertex    |---->
 *      |            |                                    |            |
 *      | - Key (Va) |                                    | - Key (Vb) |
 *  <---| - Vertex   |     ---------------------------    | - Vertex   |<----
 *      |            |<----| Connected Edge Vb to Va |<---|            |
 *      --------------     ---------------------------    --------------
 *
 */

/*
 * 顶点 / 边 / 子网的同步状态：
 *   UNSET  未设置(初始值)
 *   NEW    新加入
 *   UPDATE 已更新
 *   DELETE 待删除
 *   SYNC   已与数据源同步
 *   ORPHAN 孤立(其依赖对象已消失，等待清理)
 *
 * 元素类型：
 *   GENERIC 通用(未归类)
 *   VERTEX  顶点
 *   EDGE    边
 *   SUBNET  子网 / 前缀
 */
enum ls_status { UNSET = 0, NEW, UPDATE, DELETE, SYNC, ORPHAN };
enum ls_type { GENERIC = 0, VERTEX, EDGE, SUBNET };

/*
 * 图顶点：对应一个 ls_node。除节点指针外，还维护三张以链表组织的
 * 关系表——入边、出边与广告前缀。entry 是挂到 RB 树上的钩子
 * (由 PREDECL_RBTREE_UNIQ 生成)，key 为唯一键(通常由节点派生)。
 * 注意：顶点不拥有 node 的所有权，ls_vertex_del 不会释放 node。
 */
/* Link State Vertex structure */
PREDECL_RBTREE_UNIQ(vertices);
struct ls_vertex {
	enum ls_type type;		/* Link State Type */
	enum ls_status status;		/* Status of the Vertex in the TED */
	struct vertices_item entry;	/* Entry in RB Tree */
	uint64_t key;			/* Unique Key identifier */
	struct ls_node *node;		/* Link State Node */
	struct list *incoming_edges;	/* List of incoming Link State links */
	struct list *outgoing_edges;	/* List of outgoing Link State links */
	struct list *prefixes;		/* List of advertised prefix */
};

/*
 * 边的键：按地址族区分(family 为 AF_INET / AF_INET6 / AF_LOCAL)。
 * 联合体 k 依据 family 选用对应成员——IPv4 地址、IPv6 地址，或
 * 本地链路标识 link_id。这是边参与 RB 树排序与查找的依据。
 */
/* Link State Edge Key structure */
struct ls_edge_key {
	uint8_t family;
	union {
		struct in_addr addr;
		struct in6_addr addr6;
		uint64_t link_id;
	} k;
};

/*
 * 图边：对应一组 ls_attributes，表示一条有向链路。source 与
 * destination 指向两端顶点，attributes 指向链路 TE 属性。
 * 同样只借用指针，不拥有 attributes 与两端顶点的所有权。
 */
/* Link State Edge structure */
PREDECL_RBTREE_UNIQ(edges);
struct ls_edge {
	enum ls_type type;		/* Link State Type */
	enum ls_status status;		/* Status of the Edge in the TED */
	struct edges_item entry;	/* Entry in RB tree */
	struct ls_edge_key key;		/* Unique Key identifier */
	struct ls_attributes *attributes;	/* Link State attributes */
	struct ls_vertex *source;	/* Pointer to the source Vertex */
	struct ls_vertex *destination;	/* Pointer to the destination Vertex */
};

/*
 * 子网：对应一个 ls_prefix，表示图上的一个前缀。key 直接使用
 * struct prefix(支持 IPv4 / IPv6)；vertex 是指回广告该前缀的顶点的
 * 反向指针。
 */
/* Link State Subnet structure */
PREDECL_RBTREE_UNIQ(subnets);
struct ls_subnet {
	enum ls_type type;		/* Link State Type */
	enum ls_status status;		/* Status of the Subnet in the TED */
	struct subnets_item entry;	/* Entry in RB tree */
	struct prefix key;		/* Unique Key identifier */
	struct ls_prefix *ls_pref;	/* Link State Prefix */
	struct ls_vertex *vertex;	/* Back pointer to the Vertex owner */
};

/*
 * 顶点 / 边 / 子网三棵 RB 树的比较函数与树声明。比较函数返回
 * 负 / 0 / 正，语义同 memcmp / strcmp；返回 0 即视为同一元素。
 */
/* Declaration of Vertices, Edges and Prefixes RB Trees */
/*
 * 顶点比较：直接按 64 位 key 的数值大小比较(见 numcmp)。
 */
macro_inline int vertex_cmp(const struct ls_vertex *node1,
			    const struct ls_vertex *node2)
{
	return numcmp(node1->key, node2->key);
}
DECLARE_RBTREE_UNIQ(vertices, struct ls_vertex, entry, vertex_cmp);

/*
 * 边比较：先比地址族 family，再按族比较地址(IPv4 比 4 字节、IPv6
 * 比 16 字节)或本地链路标识。这里用的是 memcmp 而非数值比较，
 * 因此地址是按网络字节序逐字节比较的。
 */
macro_inline int edge_cmp(const struct ls_edge *edge1,
			  const struct ls_edge *edge2)
{
	if (edge1->key.family != edge2->key.family)
		return numcmp(edge1->key.family, edge2->key.family);

	switch (edge1->key.family) {
	case AF_INET:
		return memcmp(&edge1->key.k.addr, &edge2->key.k.addr, 4);
	case AF_INET6:
		return memcmp(&edge1->key.k.addr6, &edge2->key.k.addr6, 16);
	case AF_LOCAL:
		return numcmp(edge1->key.k.link_id, edge2->key.k.link_id);
	default:
		return 0;
	}
}
DECLARE_RBTREE_UNIQ(edges, struct ls_edge, entry, edge_cmp);

/*
 * 子网比较：先比地址族，再比掩码长度，最后比地址。注意前缀是按
 * “主机部分也参与比较”的方式区分的——10.0.0.1/24 与 10.0.0.2/24
 * 会被判定为不同条目。
 */
/*
 * Prefix comparison are done to the host part so, 10.0.0.1/24
 * and 10.0.0.2/24 are considered different
 */
macro_inline int subnet_cmp(const struct ls_subnet *a,
			    const struct ls_subnet *b)
{
	if (a->key.family != b->key.family)
		return numcmp(a->key.family, b->key.family);

	if (a->key.prefixlen != b->key.prefixlen)
		return numcmp(a->key.prefixlen, b->key.prefixlen);

	if (a->key.family == AF_INET)
		return memcmp(&a->key.u.val, &b->key.u.val, 4);

	return memcmp(&a->key.u.val, &b->key.u.val, 16);
}
DECLARE_RBTREE_UNIQ(subnets, struct ls_subnet, entry, subnet_cmp);

/*
 * TED(Traffic Engineering Database)：一个 LSDB 与图的顶层容器。
 *   key        唯一标识
 *   name       图名称，可能为空
 *   as_number  所建模网络的 AS 号
 *   self       指向本 FRR 实例自身顶点
 *   vertices / edges / subnets  三棵 RB 树，分别存放顶点 / 边 / 子网
 * 一个进程可持有多个 TED(例如按 IGP 实例或 AS 区分)。
 */
/* Link State TED Structure */
struct ls_ted {
	uint32_t key;			/* Unique identifier */
	char name[MAX_NAME_LENGTH];	/* Name of this graph. Could be null */
	uint32_t as_number;		/* AS number of the modeled network */
	struct ls_vertex *self;		/* Vertex of the FRR instance */
	struct vertices_head vertices;	/* List of Vertices */
	struct edges_head edges;	/* List of Edges */
	struct subnets_head subnets;	/* List of Subnets */
};

/*
 * 通用链路状态元素：在不确定对象究竟是顶点 / 边 / 子网的场合，用它
 * 统一传递。type 标明种类，data 指向真正的 payload(ls_vertex /
 * ls_edge / ls_subnet 之一)。
 */
/* Generic Link State Element */
struct ls_element {
	enum ls_type type;		/* Link State Element Type */
	enum ls_status status;		/* Link State Status in the TED */
	void *data;			/* Link State payload */
};

/*
 * 依据一个 ls_node 向 TED 新增顶点(顶点结构动态分配)。若已存在相同
 * 键的顶点则不会重复添加。ted 为图数据库，node 为源节点。
 * 返回新顶点；出错时返回 NULL。顶点不接管 node 的所有权。
 */
/**
 * Add new vertex to the Link State DB. Vertex is created from the Link State
 * Node. Vertex data structure is dynamically allocated.
 *
 * @param ted	Traffic Engineering Database structure
 * @param node	Link State Node
 *
 * @return	New Vertex or NULL in case of error
 */
extern struct ls_vertex *ls_vertex_add(struct ls_ted *ted,
				       struct ls_node *node);

/*
 * 删除一个顶点：清空它的入边 / 出边 / 前缀三张内部链表并释放顶点
 * 结构体，但不会释放其 ls_node。vertex 为 NULL 或不在库中时函数
 * 不改变数据库。注意：只是解除引用，并不删除边与子网本身，因为
 * 它们可能还连着其它顶点。
 */
/**
 * Delete Link State Vertex. This function clean internal Vertex lists (incoming
 * and outgoing Link State Edge and Link State Subnet). Vertex Data structure
 * is freed but not the Link State Node. Link State DB is not modified if Vertex
 * is NULL or not found in the Data Base. Note that referenced to Link State
 * Edges & SubNets are not removed as they could be connected to other Vertices.
 *
 * @param ted		Traffic Engineering Database structure
 * @param vertex	Link State Vertex to be removed
 */
extern void ls_vertex_del(struct ls_ted *ted, struct ls_vertex *vertex);

/*
 * 删除顶点，并在 ls_vertex_del() 的基础上额外释放关联的 ls_node。
 * 仅在确认该 node 不再被其它对象引用时使用，避免悬空指针。
 */
/**
 * Delete Link State Vertex as ls_vertex_del() but also removed associated
 * Link State Node.
 *
 * @param ted		Traffic Engineering Database structure
 * @param vertex	Link State Vertex to be removed
 */
extern void ls_vertex_del_all(struct ls_ted *ted, struct ls_vertex *vertex);

/*
 * 用给定的 ls_node 更新(或创建)对应顶点。若已有键匹配的顶点则更新
 * 其内容，否则新建一个。返回更新后的顶点，出错返回 NULL。
 */
/**
 * Update Vertex with the Link State Node. A new vertex is created if no one
 * corresponds to the Link State Node.
 *
 * @param ted	Link State Data Base
 * @param node	Link State Node to be updated
 *
 * @return	Updated Link State Vertex or Null in case of error
 */
extern struct ls_vertex *ls_vertex_update(struct ls_ted *ted,
					  struct ls_node *node);

/*
 * 清理顶点：删除挂在该顶点下所有状态为 ORPHAN 的边与子网。若
 * zclient 非 NULL，还会通过它向 zebra 发送链路状态更新(zapi opaque
 * 消息)，以便把变化传播出去。
 */
/**
 * Clean Vertex structure by removing all Edges and Subnets marked as ORPHAN
 * from this vertex. Link State Update message is sent if zclient is not NULL.
 *
 * @param ted		Link State Data Base
 * @param vertex	Link State Vertex to be cleaned
 * @param zclient	Reference to Zebra Client
 */
extern void ls_vertex_clean(struct ls_ted *ted, struct ls_vertex *vertex,
			    struct zclient *zclient);

/*
 * 把 IS-IS 的 ISO System-ID(6 字节)转换为 64 位无符号整数，用作
 * 顶点 key。结果按本机字节序解释(架构相关)，因此不同字节序的
 * 主机上得到的整数值可能不同；其目的是在同一主机内保证一致与唯一。
 */
/**
 * This function convert the ISIS ISO system ID into a 64 bits unsigned integer
 * following the architecture dependent byte order.
 *
 * @param sysid The ISO system ID
 * @return	Key as 64 bits unsigned integer
 */
extern uint64_t sysid_to_key(const uint8_t sysid[ISO_SYS_ID_LEN]);

/*
 * 按唯一 key 在 TED 中查找顶点。找到返回顶点指针，否则返回 NULL。
 */
/**
 * Find Vertex in the Link State DB by its unique key.
 *
 * @param ted	Link State Data Base
 * @param key	Vertex Key different from 0
 *
 * @return	Vertex if found, NULL otherwise
 */
extern struct ls_vertex *ls_find_vertex_by_key(struct ls_ted *ted,
					       const uint64_t key);

/*
 * 按链路状态节点标识(ls_node_id)查找顶点：内部先由该标识推导出顶点
 * key，再复用按 key 查找的逻辑。
 */
/**
 * Find Vertex in the Link State DB by its Link State Node.
 *
 * @param ted	Link State Data Base
 * @param nid	Link State Node ID
 *
 * @return	Vertex if found, NULL otherwise
 */
extern struct ls_vertex *ls_find_vertex_by_id(struct ls_ted *ted,
					      struct ls_node_id nid);

/*
 * 判断两个顶点是否相等(比较其标识与关键字段，而非指针)。返回语义同
 * '=='——相等返回 1，否则返回 0。
 */
/**
 * Check if two Vertices are equal. Note that this routine has the same return
 * value sense as '==' (which is different from a comparison).
 *
 * @param v1	First vertex to compare
 * @param v2	Second vertex to compare
 *
 * @return	1 if equal, 0 otherwise
 */
extern int ls_vertex_same(struct ls_vertex *v1, struct ls_vertex *v2);

/*
 * 依据一组 ls_attributes 向 TED 新增边(边结构动态分配)。返回新边；
 * 出错返回 NULL。边不接管 attributes 的所有权。
 */
/**
 * Add new Edge to the Link State DB. Edge is created from the Link State
 * Attributes. Edge data structure is dynamically allocated.
 *
 * @param ted		Link State Data Base
 * @param attributes	Link State attributes
 *
 * @return		New Edge or NULL in case of error
 */
extern struct ls_edge *ls_edge_add(struct ls_ted *ted,
				   struct ls_attributes *attributes);

/*
 * 更新已存在边的 ls_attributes 信息；若库中无对应边则新建一条。返回
 * 更新后的边，出错返回 NULL。
 */
/**
 * Update the Link State Attributes information of an existing Edge. If there is
 * no corresponding Edge in the Link State Data Base, a new Edge is created.
 *
 * @param ted		Link State Data Base
 * @param attributes	Link State Attributes
 *
 * @return		Updated Link State Edge, or NULL in case of error
 */
extern struct ls_edge *ls_edge_update(struct ls_ted *ted,
				      struct ls_attributes *attributes);

/*
 * 判断两条边是否相等(比较其 key，即两端地址 / 标识信息)。返回语义同
 * '=='——相等返回 1，否则返回 0。
 */
/**
 * Check if two Edges are equal. Note that this routine has the same return
 * value sense as '==' (which is different from a comparison).
 *
 * @param e1	First edge to compare
 * @param e2	Second edge to compare
 *
 * @return	1 if equal, 0 otherwise
 */
extern int ls_edge_same(struct ls_edge *e1, struct ls_edge *e2);

/*
 * 从 TED 删除一条边：释放边结构体但保留其 ls_attributes。edge 为
 * NULL 或不在库中时不做修改。
 */
/**
 * Remove Edge from the Link State DB. Edge data structure is freed but not the
 * Link State Attributes data structure. Link State DB is not modified if Edge
 * is NULL or not found in the Data Base.
 *
 * @param ted	Link State Data Base
 * @param edge	Edge to be removed
 */
extern void ls_edge_del(struct ls_ted *ted, struct ls_edge *edge);

/*
 * 删除边，并一并释放其关联的 ls_attributes。edge 为 NULL 或不在库中
 * 时不做修改。
 */
/**
 * Remove Edge and associated Link State Attributes from the Link State DB.
 * Link State DB is not modified if Edge is NULL or not found.
 *
 * @param ted	Link State Data Base
 * @param edge	Edge to be removed
 */
extern void ls_edge_del_all(struct ls_ted *ted, struct ls_edge *edge);

/*
 * 按 ls_edge_key 查找边。找到返回边指针，否则返回 NULL。
 */
/**
 * Find Edge in the Link State Data Base by Edge key.
 *
 * @param ted	Link State Data Base
 * @param key	Edge key
 *
 * @return	Edge if found, NULL otherwise
 */
extern struct ls_edge *ls_find_edge_by_key(struct ls_ted *ted,
					   const struct ls_edge_key key);

/*
 * 按属性的源信息(本端 IPv4 / IPv6 地址或本地标识)构造 key 来查找边。
 */
/**
 * Find Edge in the Link State Data Base by the source (local IPv4 or IPv6
 * address or local ID) informations of the Link State Attributes
 *
 * @param ted		Link State Data Base
 * @param attributes	Link State Attributes
 *
 * @return		Edge if found, NULL otherwise
 */
extern struct ls_edge *
ls_find_edge_by_source(struct ls_ted *ted, struct ls_attributes *attributes);

/*
 * 按属性的目的信息(远端 IPv4 / IPv6 地址或远端标识)构造 key 来查找边。
 */
/**
 * Find Edge in the Link State Data Base by the destination (remote IPv4 or IPv6
 * address of remote ID) information of the Link State Attributes
 *
 * @param ted		Link State Data Base
 * @param attributes	Link State Attributes
 *
 * @return		Edge if found, NULL otherwise
 */
extern struct ls_edge *
ls_find_edge_by_destination(struct ls_ted *ted,
			    struct ls_attributes *attributes);

/*
 * 依据 ls_prefix 向 TED 新增子网(子网结构动态分配)。返回新子网。
 */
/**
 * Add new Subnet to the Link State DB. Subnet is created from the Link State
 * prefix. Subnet data structure is dynamically allocated.
 *
 * @param ted	Link State Data Base
 * @param pref	Link State Prefix
 *
 * @return	New Subnet
 */
extern struct ls_subnet *ls_subnet_add(struct ls_ted *ted,
				       struct ls_prefix *pref);

/*
 * 更新已存在子网的 ls_prefix 信息；无对应子网则新建。返回更新后的
 * 子网，出错返回 NULL。
 */
/**
 * Update the Link State Prefix information of an existing Subnet. If there is
 * no corresponding Subnet in the Link State Data Base, a new Subnet is created.
 *
 * @param ted	Link State Data Base
 * @param pref	Link State Prefix
 *
 * @return	Updated Link State Subnet, or NULL in case of error
 */
extern struct ls_subnet *ls_subnet_update(struct ls_ted *ted,
					  struct ls_prefix *pref);

/*
 * 判断两个子网是否相等(比较其前缀 key)。返回语义同 '=='——相等返回
 * 1，否则返回 0。
 */
/**
 * Check if two Subnets are equal. Note that this routine has the same return
 * value sense as '==' (which is different from a comparison).
 *
 * @param s1	First subnet to compare
 * @param s2	Second subnet to compare
 *
 * @return	1 if equal, 0 otherwise
 */
extern int ls_subnet_same(struct ls_subnet *s1, struct ls_subnet *s2);

/*
 * 删除子网：释放子网结构体但保留其 ls_prefix。subnet 为 NULL 或不在
 * 库中时不做修改。
 */
/**
 * Remove Subnet from the Link State DB. Subnet data structure is freed but
 * not the Link State prefix data structure. Link State DB is not modified
 * if Subnet is NULL or not found in the Data Base.
 *
 * @param ted		Link State Data Base
 * @param subnet	Subnet to be removed
 */
extern void ls_subnet_del(struct ls_ted *ted, struct ls_subnet *subnet);

/*
 * 删除子网，并一并释放其关联的 ls_prefix。subnet 为 NULL 或不在库中
 * 时不做修改。
 */
/**
 * Remove Subnet and the associated Link State Prefix from the Link State DB.
 * Link State DB is not modified if Subnet is NULL or not found.
 *
 * @param ted		Link State Data Base
 * @param subnet	Subnet to be removed
 */
extern void ls_subnet_del_all(struct ls_ted *ted, struct ls_subnet *subnet);

/*
 * 按 struct prefix 查找子网。找到返回子网指针，否则返回 NULL。
 */
/**
 * Find Subnet in the Link State Data Base by prefix.
 *
 * @param ted		Link State Data Base
 * @param prefix	Link State Prefix
 *
 * @return		Subnet if found, NULL otherwise
 */
extern struct ls_subnet *ls_find_subnet(struct ls_ted *ted,
					const struct prefix *prefix);

/*
 * 新建一个链路状态数据库(TED)。key 为必填且不能为 0；name 可为
 * NULL；asn 为 AS 号，未知时传 0。返回新数据库，出错返回 NULL。
 */
/**
 * Create a new Link State Data Base.
 *
 * @param key	Unique key of the data base. Must be different from 0
 * @param name	Name of the data base (may be NULL)
 * @param asn	AS Number for this data base. 0 if unknown
 *
 * @return	New Link State Database or NULL in case of error
 */
extern struct ls_ted *ls_ted_new(const uint32_t key, const char *name,
				 uint32_t asn);

/*
 * 删除数据库结构体本身，但不删除其中的顶点 / 边 / 子网(调用方需先
 * 用 ls_ted_del_all 或逐个清理)。
 */
/**
 * Delete existing Link State Data Base. Vertices, Edges, and Subnets are not
 * removed.
 *
 * @param ted	Link State Data Base
 */
extern void ls_ted_del(struct ls_ted *ted);

/*
 * 删除所有顶点 / 边 / 子网，再删除数据库本身。注意形参是二级指针
 * (struct ls_ted **)，函数会把调用者的指针置为 NULL。
 */
/**
 * Delete all Link State Vertices, Edges and SubNets and the Link State DB.
 *
 * @param ted	Link State Data Base
 */
extern void ls_ted_del_all(struct ls_ted **ted);

/*
 * 清理数据库：删除所有状态为 ORPHAN 的顶点 / 边 / 子网。
 */
/**
 * Clean Link State Data Base by removing all Vertices, Edges and SubNets marked
 * as ORPHAN.
 *
 * @param ted	Link State Data Base
 */
extern void ls_ted_clean(struct ls_ted *ted);

/*
 * 用给定边连接源顶点与目的顶点，即把边分别登记到两端顶点的入边 /
 * 出边链表。src / dst 为 NULL 时不建立对应方向的连接；edge 不能为
 * NULL。
 */
/**
 * Connect Source and Destination Vertices by given Edge. Only non NULL source
 * and destination vertices are connected.
 *
 * @param src	Link State Source Vertex
 * @param dst	Link State Destination Vertex
 * @param edge	Link State Edge. Must not be NULL
 */
extern void ls_connect_vertices(struct ls_vertex *src, struct ls_vertex *dst,
				struct ls_edge *edge);

/*
 * 把一条边连接到某个顶点，并指明它是该顶点的出边(source 为真)还是
 * 入边(source 为假)。vertex 与 edge 都不能为 NULL。
 */
/**
 * Connect Link State Edge to the Link State Vertex which could be a Source or
 * a Destination Vertex.
 *
 * @param vertex	Link State Vertex to be connected. Must not be NULL
 * @param edge		Link State Edge connection. Must not be NULL
 * @param source	True for a Source, false for a Destination Vertex
 */
extern void ls_connect(struct ls_vertex *vertex, struct ls_edge *edge,
		       bool source);

/*
 * 把一条边从某个顶点的入边或出边链表中断开(与 ls_connect 相反)。
 */
/**
 * Disconnect Link State Edge from the Link State Vertex which could be a
 * Source or a Destination Vertex.
 *
 * @param vertex	Link State Vertex to be connected. Must not be NULL
 * @param edge		Link State Edge connection. Must not be NULL
 * @param source	True for a Source, false for a Destination Vertex
 */
extern void ls_disconnect(struct ls_vertex *vertex, struct ls_edge *edge,
			  bool source);

/*
 * 把一条边同时从它的源顶点与目的顶点断开(两端都做 disconnect)。
 */
/**
 * Disconnect Link State Edge from both Source and Destination Vertex.
 *
 * @param edge		Link State Edge to be disconnected
 */
extern void ls_disconnect_edge(struct ls_edge *edge);


/*
 * 链路状态消息(LS Message)：路由协议(OSPF / IS-IS)经 zebra 的 ZAPI
 * Opaque 通道把链路状态信息转交给其它守护进程(如 bgpd)时使用的载体。
 * 由四部分组成——消息事件(event)、元素类型(type)、已知时的远端节点
 * 标识(remote_id)、以及数据(data)。一条消息一次只装载一个元素(节点、
 * 属性或前缀之一)。
 */
/**
 * The Link State Message is defined to convey Link State parameters from
 * the routing protocol (OSPF or IS-IS) to other daemons e.g. BGP.
 *
 * The structure is composed of:
 *  - Event of the message:
 *    - Sync: Send the whole LS DB following a request
 *    - Add: Send the a new Link State element
 *    -  Update: Send an update of an existing Link State element
 *    - Delete: Indicate that the given Link State element is removed
 *  - Type of Link State element: Node, Attribute or Prefix
 *  - Remote node id when known
 *  - Data: Node, Attributes or Prefix
 *
 * A Link State Message can carry only one Link State Element (Node, Attributes
 * of Prefix) at once, and only one Link State Message is sent through ZAPI
 * Opaque Link State type at once.
 */

/*
 * 消息事件取值：UNDEF 未定义、SYNC 全量同步(应请求发送整个 LSDB)、
 * ADD 新增、UPDATE 更新、DELETE 删除某个链路状态元素。
 */
/* ZAPI Opaque Link State Message Event */
#define LS_MSG_EVENT_UNDEF	0
#define LS_MSG_EVENT_SYNC	1
#define LS_MSG_EVENT_ADD	2
#define LS_MSG_EVENT_UPDATE	3
#define LS_MSG_EVENT_DELETE	4

/*
 * 消息承载的元素类型：NODE 节点、ATTRIBUTES 属性、PREFIX 前缀。
 */
/* ZAPI Opaque Link State Message sub-Type */
#define LS_MSG_TYPE_NODE	1
#define LS_MSG_TYPE_ATTRIBUTES	2
#define LS_MSG_TYPE_PREFIX	3

/*
 * 链路状态消息体。event 与 type 取上面两组的枚举值；remote_id 为已知
 * 的远端节点标识；data 是联合体，按 type 选用 node / attr / prefix
 * 之一。注意：若在此容器中新增成员结构体，需同步更新
 * get_max_ls_msg_size() 辅助函数(见其说明)。
 */
/* Link State Message */
struct ls_message {
	uint8_t event;		/* Message Event: Sync, Add, Update, Delete */
	uint8_t type;		/* Message Data Type: Node, Attribute, Prefix */
	struct ls_node_id remote_id;	/* Remote Link State Node ID */

	/* Please update the get_max_ls_msg_size() helper if you add a new
	 * struct to this message container.
	 */
	union {
		struct ls_node *node;		/* Link State Node */
		struct ls_attributes *attr;	/* Link State Attributes */
		struct ls_prefix *prefix;	/* Link State Prefix */
	} data;
};

/*
 * 向 zebra 的 OPAQUE API 注册本守护进程：server 为真时注册为服务端
 * (持有 LSDB、响应同步请求)，为假时注册为客户端(消费链路状态信息)。
 * 成功返回 0，失败返回 -1。
 */
/**
 * Register Link State daemon as a server or client for Zebra OPAQUE API.
 *
 * @param zclient  Zebra client structure
 * @param server   Register daemon as a server (true) or as a client (false)
 *
 * @return	   0 if success, -1 otherwise
 */
extern int ls_register(struct zclient *zclient, bool server);

/*
 * 从 zebra 的 OPAQUE API 注销本守护进程(与 ls_register 对应)。成功
 * 返回 0，失败返回 -1。
 */
/**
 * Unregister Link State daemon as a server or client for Zebra OPAQUE API.
 *
 * @param zclient  Zebra client structure
 * @param server   Unregister daemon as a server (true) or as a client (false)
 *
 * @return	   0 if success, -1 otherwise
 */
extern int ls_unregister(struct zclient *zclient, bool server);

/*
 * 以客户端身份发送一条 SYNC 请求，向服务端索取完整的链路状态数据库。
 * 成功返回 0，失败返回 -1。
 */
/**
 * Send Link State SYNC message to request the complete Link State Database.
 *
 * @param zclient	Zebra client
 *
 * @return		0 if success, -1 otherwise
 */
extern int ls_request_sync(struct zclient *zclient);

/*
 * 从数据流中解析出一条链路状态消息：收到 ZAPI Opaque 的 Link State
 * 类型消息后调用。返回新分配的消息(内含已分配的元素)，出错返回
 * NULL；解析出的消息需用 ls_delete_msg 释放。
 */
/**
 * Parse Link State Message from stream. Used this function once receiving a
 * new ZAPI Opaque message of type Link State.
 *
 * @param s	Stream buffer. Must not be NULL.
 *
 * @return	New Link State Message or NULL in case of error
 */
extern struct ls_message *ls_parse_msg(struct stream *s);

/*
 * 释放一条链路状态消息：连同其内部的元素结构体一起释放，并把调用者
 * 的指针置为 NULL。
 */
/**
 * Delete existing message. Data structure is freed.
 *
 * @param msg	Link state message to be deleted
 */
extern void ls_delete_msg(struct ls_message *msg);

/*
 * 把链路状态消息封装为 ZAPI Opaque(Link State 类型)发送出去。dst 非
 * NULL 时单播给指定守护进程，为 NULL 时广播给所有已注册进程。
 * 成功返回 0，失败返回 -1。
 */
/**
 * Send Link State Message as new ZAPI Opaque message of type Link State.
 * If destination is not NULL, message is sent as Unicast otherwise it is
 * broadcast to all registered daemon.
 *
 * @param zclient	Zebra Client
 * @param msg		Link State Message to be sent
 * @param dst		Destination daemon for unicast message,
 *			NULL for broadcast message
 *
 * @return		0 on success, -1 otherwise
 */
extern int ls_send_msg(struct zclient *zclient, struct ls_message *msg,
		       struct zapi_opaque_reg_info *dst);

/*
 * 由顶点生成链路状态消息。msg 为 NULL 时动态新建一条消息，否则填充
 * 传入的消息。返回消息指针(msg 为 NULL 时即新分配的那条)。
 */
/**
 * Create a new Link State Message from a Link State Vertex. If Link State
 * Message is NULL, a new data structure is dynamically allocated.
 *
 * @param msg		Link State Message to be filled or NULL
 * @param vertex	Link State Vertex. Must not be NULL
 *
 * @return		New Link State Message msg parameter is NULL or pointer
 *			to the provided Link State Message
 */
extern struct ls_message *ls_vertex2msg(struct ls_message *msg,
					struct ls_vertex *vertex);

/*
 * 由边生成链路状态消息。msg 为 NULL 时动态新建，否则填充传入的消息。
 * 返回消息指针(msg 为 NULL 时即新分配的那条)。
 */
/**
 * Create a new Link State Message from a Link State Edge. If Link State
 * Message is NULL, a new data structure is dynamically allocated.
 *
 * @param msg		Link State Message to be filled or NULL
 * @param edge		Link State Edge. Must not be NULL
 *
 * @return		New Link State Message msg parameter is NULL or pointer
 *			to the provided Link State Message
 */
extern struct ls_message *ls_edge2msg(struct ls_message *msg,
				      struct ls_edge *edge);

/*
 * 由子网生成链路状态消息。msg 为 NULL 时动态新建，否则填充传入的
 * 消息。返回消息指针(msg 为 NULL 时即新分配的那条)。
 */
/**
 * Create a new Link State Message from a Link State Subnet. If Link State
 * Message is NULL, a new data structure is dynamically allocated.
 *
 * @param msg		Link State Message to be filled or NULL
 * @param subnet	Link State Subnet. Must not be NULL
 *
 * @return		New Link State Message msg parameter is NULL or pointer
 *			to the provided Link State Message
 */
extern struct ls_message *ls_subnet2msg(struct ls_message *msg,
					struct ls_subnet *subnet);

/*
 * 把链路状态消息转换为顶点，并按消息事件(SYNC / ADD / UPDATE /
 * DELETE)更新 TED。delete 为真表示该顶点应被删除，此时返回 NULL。
 * 成功返回对应顶点，其它情况返回 NULL。
 */
/**
 * Convert Link State Message into Vertex and update TED accordingly to
 * the message event: SYNC, ADD, UPDATE or DELETE.
 *
 * @param ted		Link State Database
 * @param msg		Link State Message
 * @param delete	True to delete the Link State Vertex from the Database,
 *                      False otherwise. If true, return value is NULL in case
 *                      of deletion.
 *
 * @return	Vertex if success, NULL otherwise or if Vertex is removed
 */
extern struct ls_vertex *ls_msg2vertex(struct ls_ted *ted,
				       struct ls_message *msg, bool delete);

/*
 * 把链路状态消息转换为边，并按消息事件更新 TED。delete 为真表示该边
 * 应被删除，此时返回 NULL。成功返回对应边，其它情况返回 NULL。
 */
/**
 * Convert Link State Message into Edge and update TED accordingly to
 * the message event: SYNC, ADD, UPDATE or DELETE.
 *
 * @param ted		Link State Database
 * @param msg		Link State Message
 * @param delete	True to delete the Link State Edge from the Database,
 *                      False otherwise. If true, return value is NULL in case
 *                      of deletion.
 *
 * @return	Edge if success, NULL otherwise or if Edge is removed
 */
extern struct ls_edge *ls_msg2edge(struct ls_ted *ted, struct ls_message *msg,
				   bool delete);

/*
 * 把链路状态消息转换为子网，并按消息事件更新 TED。delete 为真表示该
 * 子网应被删除，此时返回 NULL。成功返回对应子网，其它情况返回 NULL。
 */
/**
 * Convert Link State Message into Subnet and update TED accordingly to
 * the message event: SYNC, ADD, UPDATE or DELETE.
 *
 * @param ted		Link State Database
 * @param msg		Link State Message
 * @param delete	True to delete the Link State Subnet from the Database,
 *                      False otherwise. If true, return value is NULL in case
 *                      of deletion.
 *
 * @return	Subnet if success, NULL otherwise or if Subnet is removed
 */
extern struct ls_subnet *ls_msg2subnet(struct ls_ted *ted,
				       struct ls_message *msg, bool delete);

/*
 * 把链路状态消息转换为通用元素(顶点 / 边 / 子网之一)并按事件更新
 * TED。这是上面的分类型转换函数的统一入口，依据 msg 的类型字段分派。
 */
/**
 * Convert Link State Message into Link State element (Vertex, Edge or Subnet)
 * and update TED accordingly to the message event: SYNC, ADD, UPDATE or DELETE.
 *
 * @param ted		Link State Database
 * @param msg		Link State Message
 * @param delete	True to delete the Link State Element from the Database,
 *                      False otherwise. If true, return value is NULL in case
 *                      of deletion.
 *
 * @return	Element if success, NULL otherwise or if Element is removed
 */
extern struct ls_element *ls_msg2ted(struct ls_ted *ted, struct ls_message *msg,
				     bool delete);

/*
 * 直接从数据流解析出链路状态元素(顶点 / 边 / 子网)并按事件更新 TED，
 * 相当于 ls_parse_msg 与 ls_msg2ted 的组合，省去显式管理中间消息对象。
 */
/**
 * Convert stream buffer into Link State element (Vertex, Edge or Subnet) and
 * update TED accordingly to the message event: SYNC, ADD, UPDATE or DELETE.
 *
 * @param ted		Link State Database
 * @param s		Stream buffer
 * @param delete	True to delete the Link State Element from the Database,
 *                      False otherwise. If true, return value is NULL in case
 *                      of deletion.
 *
 * @return	Element if success, NULL otherwise or if Element is removed
 */
extern struct ls_element *ls_stream2ted(struct ls_ted *ted, struct stream *s,
					bool delete);

/*
 * 把整个链路状态数据库按“顶点、边、子网”的顺序发送给指定目的守护
 * 进程，用于响应一次 LSDB 同步请求。成功返回 0，失败返回 -1。
 */
/**
 * Send all the content of the Link State Data Base to the given destination.
 * Link State content is sent is this order: Vertices, Edges, Subnet.
 * This function must be used when a daemon request a Link State Data Base
 * Synchronization.
 *
 * @param ted		Link State Data Base. Must not be NULL
 * @param zclient	Zebra Client. Must not be NULL
 * @param dst		Destination FRR daemon. Must not be NULL
 *
 * @return		0 on success, -1 otherwise
 */
extern int ls_sync_ted(struct ls_ted *ted, struct zclient *zclient,
		       struct zapi_opaque_reg_info *dst);

struct json_object;
struct vty;
/*
 * 输出单个顶点的信息。vty 与 json 同时指定时以 JSON 输出为准；verbose
 * 为真时输出更多细节。这是给 CLI / 北向接口复用的展示函数。
 */
/**
 * Show Link State Vertex information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param vertex	Link State Vertex to show. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_vertex(struct ls_vertex *vertex, struct vty *vty,
			   struct json_object *json, bool verbose);

/*
 * 遍历并输出数据库中所有顶点的信息(逐个调用 ls_show_vertex 的语义)。
 */
/**
 * Show all Link State Vertices information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param ted		Link State Data Base. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_vertices(struct ls_ted *ted, struct vty *vty,
			     struct json_object *json, bool verbose);

/*
 * 输出单条边的信息。vty 与 json 同时指定时以 JSON 输出为准；verbose
 * 为真时输出更多细节。
 */
/**
 * Show Link State Edge information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param edge		Link State Edge to show. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_edge(struct ls_edge *edge, struct vty *vty,
			 struct json_object *json, bool verbose);

/*
 * 遍历并输出数据库中所有边的信息(逐个调用 ls_show_edge 的语义)。
 */
/**
 * Show all Link State Edges information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param ted		Link State Data Base. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_edges(struct ls_ted *ted, struct vty *vty,
			  struct json_object *json, bool verbose);

/*
 * 输出单个子网(前缀)的信息。vty 与 json 同时指定时以 JSON 输出为准；
 * verbose 为真时输出更多细节。
 */
/**
 * Show Link State Subnets information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param subnet	Link State Subnet to show. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_subnet(struct ls_subnet *subnet, struct vty *vty,
			   struct json_object *json, bool verbose);

/*
 * 遍历并输出数据库中所有子网的信息(逐个调用 ls_show_subnet 的语义)。
 */
/**
 * Show all Link State Subnet information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param ted		Link State Data Base. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_subnets(struct ls_ted *ted, struct vty *vty,
			    struct json_object *json, bool verbose);

/*
 * 输出整个数据库(TED)的信息：通常是依次展示全部顶点、边与子网。
 */
/**
 * Show Link State Data Base information. If both vty and json are specified,
 * Json format output supersedes standard vty output.
 *
 * @param ted		Link State Data Base to show. Must not be NULL
 * @param vty		Pointer to vty output, could be NULL
 * @param json		Pointer to json output, could be NULL
 * @param verbose	Set to true for more detail
 */
extern void ls_show_ted(struct ls_ted *ted, struct vty *vty,
			struct json_object *json, bool verbose);

/*
 * 以调试为目的，打印数据库中所有元素的内部细节(通常输出到日志而非
 * vty)。仅在排障时使用。
 */
/**
 * Dump all Link State Data Base elements for debugging purposes
 *
 * @param ted	Link State Data Base. Must not be NULL
 *
 */
extern void ls_dump_ted(struct ls_ted *ted);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_LINK_STATE_H_ */
