// SPDX-License-Identifier: GPL-2.0-or-later
/* EVPN header for multihoming procedures
 *
 * Copyright (C) 2019 Cumulus Networks
 * Anuradha Karuppiah
 *
 */
/* ============================================================================
 * 【文件导读】bgp_evpn_mh.h —— EVPN 多归属(EVPN Multihoming, EVPN-MH)的数据结构总表
 * ============================================================================
 * 【职责与定位】本文件只声明数据结构与对外接口，是 EVPN-MH 功能的“骨架定义”。
 *   它回答三个层面的问题：
 *     1) “一个接入段(ES)有哪些角色？”—— 本地/远端 ES、ES 上的 VTEP、ES 上的 VNI、
 *        ES 上的 VRF、ES 分片(frag) 等对象都在这里定义；
 *     2) “这些对象怎么挂起来？”—— BGPD 全局用一棵 RB 树(es_rb_tree)按 ESI 索引
 *        所有 ES；每个 ES 再用若干 list 挂起级联的 VTEP / EVI / VRF / MACIP 路径；
 *     3) “跨文件怎么协作？”—— 文件尾部一大段 extern 声明把接口暴露给
 *        bgp_evpn_mh.c、bgp_evpn.c、bgp_zebra.c 等。
 *
 * 【核心数据结构 / 关键概念】（自底向上理解更顺）
 *   - ESI (Ethernet Segment Identifier)：一个接入段的唯一标识；同一主机多条链路
 *     接入多台 PE 时共享同一 ESI。zero_esi (全 0) 表示无效 / 不属于任何 ES。
 *   - bgp_evpn_es        ：核心对象，代表一个 ES。既可能是本地(来自 zebra 配置)、
 *                          也可能是远端(被某个远端 ES-EVI 引用而隐式创建)，甚至
 *                          同时是两者。ES 级别的 Type-1/Type-4 路由存在它的
 *                          route_table 里。
 *   - bgp_evpn_es_frag   ：ES 分片。一个 ES 可能产生多条 EAD-per-ES 路由，每条
 *                          路由需要一个独立 RD，分片就是“一组共用一个 RD 的
 *                          ES-EVI 集合”的载体。
 *   - bgp_evpn_es_vtep   ：挂在 ES 上的对端 PE(VTEP)。DF 选举信息(df_alg / df_pref)
 *                          随 ESR(Type-4) 路由从对端学来，存在这里。
 *   - bgp_evpn_es_evi    ：ES-per-EVI，即“某个 ES 在某个 L2-VNI 上的视角”。
 *   - bgp_evpn_es_evi_vtep：更细一层，标记某 PE 在该 EVI 上是否发了 EAD-per-ES /
 *                          EAD-per-EVI。
 *   - bgp_evpn_es_vrf    ：ES-per-VRF，为对称 IRB 的 L3 NHG(下一跳组)服务。
 *   - bgp_evpn_nh        ：由 EVPN Type-2 导入 VRF 表时按下一跳(VTEP)建立的下一跳
 *                          对象，首个路径引用时创建、最后一个释放时删除。
 *   - bgp_evpn_mh_info    ：整个 MH 子系统的“总控块”，存在 bgp_master 里，通过宏
 *                          bgp_mh_info 访问。
 *
 * 【本文件在主流程中的位置】
 *   入口/出口主要在 bgp_evpn_mh.c（本地 ES 增删来自 bgp_zebra.c 的 zebra 通知、
 *   远端 Type-1/Type-4 路由来自 bgp_evpn.c 的报文处理）。本头文件的 extern 声明
 *   就是这些跨文件调用的契约；inline 小函数用于 attr 上 ES 相关标志位的查询。
 *
 * 【易错点 / 阅读建议】
 *   1) 本地/远端的两套标志位分别记在 es->flags(BGP_EVPNES_ 前缀) 与
 *      attr->es_flags(ATTR_ES_ 前缀) 上，含义不同，别混。
 *   2) 同一个 ES 可以同时带 BGP_EVPNES_LOCAL 与 BGP_EVPNES_REMOTE 两种 flag。
 *   3) 建议阅读顺序：zero_esi 与 bgp_evpn_is_esi_valid → struct bgp_evpn_es
 *      → es_frag / es_vtep / es_evi / es_vrf / nh → bgp_evpn_mh_info
 *      → 末尾 inline 查询族 → extern 接口清单。
 * ============================================================================
 */

#ifndef _FRR_BGP_EVPN_MH_H
#define _FRR_BGP_EVPN_MH_H

#include "vxlan.h"
#include "bgpd.h"
#include "bgp_evpn.h"
#include "bgp_evpn_private.h"

/* 【杂项常量组】以下宏多为“特殊以太网标签(ETH_TAG) 取值”“字符串缓冲区大小”“周期
 * 定时器间隔”等，散落各处、用途单一，逐条说明见下。 */
/* ES 级(EAD-per-ES)自动派生(AD, Auto-Derive) 以太网标签的特殊取值：0xffffffff 表示
 * “这是一个 ES 级别、不针对具体 EVI 的标签”，与下面的 EVI 级 0 形成对照。 */
#define BGP_EVPN_AD_ES_ETH_TAG 0xffffffff
/* EVI 级(EAD-per-EVI) 自动派生以太网标签取 0——即按 EVI 本身派生，不额外附加标签。 */
#define BGP_EVPN_AD_EVI_ETH_TAG 0

/* 不一致性(inconsistency)描述字符串的缓冲区大小。 */
#define BGP_EVPNES_INCONS_STR_SZ 80
/* 打印“一个 ES 上所有 VTEP 标志位”所需的缓冲区大小 = 单个标志串长度 * 最多 VTEP 数。 */
#define BGP_EVPN_VTEPS_FLAG_STR_SZ (BGP_EVPN_FLAG_STR_SZ * ES_VTEP_MAX_CNT)

/* 后台一致性检查的周期(秒)。 */
#define BGP_EVPN_CONS_CHECK_INTERVAL 60

/* L3 NHG(下一跳组) 是否默认对 ES 启用。 */
#define BGP_EVPN_MH_USE_ES_L3NHG_DEF true

/* 【分片容量】每个 ES 分片(bgp_evpn_es_frag)最多容纳多少个 ES-EVI。达到上限会
 * 切开成新的分片（进而得到新的 RD 与新的 EAD-per-ES 路由）。128 为经验值。 */
/* XXX - tune this */
#define BGP_EVPN_MAX_EVI_PER_ES_FRAG 128

/* 【结构】bgp_evpn_es_frag —— ES 分片：一个 ES 上“共用同一 RD 的一组 ES-EVI”。
 *   为什么需要分片：一个 ES 可能承载大量 EVI，若把所有 EVI 信息塞进一条
 *   EAD-per-ES(Type-1) 路由，报文会过大；于是按 BGP_EVPN_MAX_EVI_PER_ES_FRAG
 *   切分，每片生成一条 EAD-per-ES，并用独立 RD 区分（对端才能同时收到多条而
 *   不把它们当成同一前缀的重复）。
 *   字段：
 *     es               : 反向指回所属 ES(父对象)。
 *     rd_id            : 该分片自动派生 RD 时用的编号（派生算法见 .c 中 RD 生成处）。
 *     prd              : 本分片实际使用的 RD。
 *     es_listnode      : 把本分片挂进 es->es_frag_list 的链表节点。
 *     es_evi_frag_list : 归属本分片的 ES-EVI 列表。
 */
/* An ES can result in multiple EAD-per-ES route. Each EAD fragment is
 * associated with an unique RD
 */
struct bgp_evpn_es_frag {
	/* frag is associated with a parent ES */
	struct bgp_evpn_es *es;

	/* Id for deriving the RD automatically for this ES fragment */
	uint16_t rd_id;
	/* RD for this ES fragment */
	struct prefix_rd prd;

	/* Memory used for linking bgp_evpn_es_frag to
	 * bgp_evpn_es->es_frag_list
	 */
	struct listnode es_listnode;

	/* List of ES-EVIs associated with this fragment */
	struct list *es_evi_frag_list;
};

/* 【结构】bgp_evpn_es —— 核心对象：一个以太网接入段(Ethernet Segment)。
 *   同一台主机(或一组链路)接入多台 PE 时，各 PE 用同一个 ESI 标识这个段；本对象
 *   就是 bgpd 对该段的本地视图。它同时是“本地 ES 状态机”和“远端 ES 信息”的载体：
 *     - 本地 ES：由 zebra 配置触发创建，带 BGP_EVPNES_LOCAL，进入 local_es_list；
 *     - 远端 ES：被某个远端 ES-EVI 引用时隐式创建，带 BGP_EVPNES_REMOTE；
 *     - 二者可并存（本地 ES 通常也会有远端 PE 与之组成同一 ES）。
 *
 *   flags 位（BGP_EVPNES_ 前缀，与 attr 上的 ATTR_ES_ 是两套不同标志，勿混）：
 *     LOCAL           : 由 zebra 配置创建（本地）。
 *     REMOTE          : 被远端 ES-EVI 引用而隐式创建。
 *     OPER_UP         : 本地 ES 链路 oper-up。
 *     ADV_EVI         : 允许生成 EAD-EVI 路由。
 *     CONS_CHECK_PEND : 有待执行的一致性检查。
 *     BYPASS          : ES 处于 LACP bypass 模式——此时不通告 EAD-ES 与 ESR。
 *     BGP_EVPN_FLAG_STR_SZ : 仅用于打印 flags 的缓冲区所需 bit 数（含结尾 NUL）。
 *
 *   关键字段：
 *     esi / esi_str   : ESI 原始值与其点分十六进制字符串（日志/CLI 常用字符串形式）。
 *     rb_node         : 挂进 bgp_mh_info->es_rb_tree 的 RB 树节点（以 ESI 为键）。
 *     es_listnode     : [LOCAL] 挂进 local_es_list 的链表节点。
 *     pend_es_listnode: 挂进 pend_es_list（待处理/周期处理队列）的链表节点。
 *     es_frag_list    : [LOCAL] 本 ES 的所有分片(bgp_evpn_es_frag)。
 *     es_base_frag    : 基分片——拿不到特定分片时使用的默认分片。
 *     originator_ip   : [LOCAL] 本端发起源 IP（写进路由属性用）。
 *     route_table     : [LOCAL] 本 ESI 的路由表，存放本/远端 Type-4 与本地 Type-1。
 *     es_vtep_list    : 本 ES 上挂着的对端 PE(bgp_evpn_es_vtep) 列表。
 *     es_evi_list     : 本 ES 关联的 ES-EVI 列表。
 *     es_vrf_list     : 本 ES 关联的 ES-VRF 列表。
 *     macip_evi_path_list   : 以本 ES 为目的地的 MAC-IP VNI 路径（仅本地/zebra 加入者）。
 *     macip_global_path_list: 以本 ES 为目的地的全局表 MAC-IP 路径（仅非本地/导入者）。
 *     remote_es_evi_cnt : 引用本 ES 的远端 VNI 个数（掉到 0 可考虑释放远端 ES）。
 *     inconsistencies   : 不一致性位图，目前只有 INCONS_VTEP_LIST 一位——存在某个
 *                         EVI 的 VTEP 列表与本 ES 的 VTEP 列表不一致。
 *     incons_evi_vtep_cnt : 上述 VTEP 列表不一致的 ES-EVI 计数。
 *     df_pref           : 本端在 BUM-DF 选举中的偏好值（随 ESR 通告）。
 *
 *   易错点：es->route_table 只放“本端相关的 ES 级路由”（本地/远端 ESR 与本地
 *   EAD-per-ES）；远端 EAD-per-ES 与所有 EAD-per-EVI 都进 VNI 路由表，不进这里。
 */
/* Ethernet Segment entry -
 * - Local and remote ESs are maintained in a global RB tree,
 *   bgp_mh_info->es_rb_tree using ESI as key
 * - Local ESs are received from zebra (BGP_EVPNES_LOCAL)
 * - Remotes ESs are implicitly created (by reference) by a remote ES-EVI
 *   (BGP_EVPNES_REMOTE)
 * - An ES can be simultaneously LOCAL and REMOTE; in fact all LOCAL ESs are
 *   expected to have REMOTE ES peers.
 */
struct bgp_evpn_es {
	/* Ethernet Segment Identifier */
	esi_t esi;
	char esi_str[ESI_STR_LEN];

	/* es flags */
	uint32_t flags;
	/* created via zebra config */
#define BGP_EVPNES_LOCAL           (1 << 0)
	/* created implicitly by a remote ES-EVI reference */
#define BGP_EVPNES_REMOTE          (1 << 1)
	/* local ES link is oper-up */
#define BGP_EVPNES_OPER_UP         (1 << 2)
	/* enable generation of EAD-EVI routes */
#define BGP_EVPNES_ADV_EVI         (1 << 3)
	/* consistency checks pending */
#define BGP_EVPNES_CONS_CHECK_PEND (1 << 4)
	/* ES is in LACP bypass mode - don't advertise EAD-ES or ESR */
#define BGP_EVPNES_BYPASS (1 << 5)
	/* bits needed for printing the flags + null */
#define BGP_EVPN_FLAG_STR_SZ 7

	/* memory used for adding the es to bgp->es_rb_tree */
	RB_ENTRY(bgp_evpn_es) rb_node;

	/* [EVPNES_LOCAL] memory used for linking the es to
	 * bgp_mh_info->local_es_list
	 */
	struct listnode es_listnode;

	/* memory used for linking the es to "processing" pending list
	 * bgp_mh_info->pend_es_list
	 */
	struct listnode pend_es_listnode;

	/* [EVPNES_LOCAL] List of RDs for this ES (bgp_evpn_es_frag) */
	struct list *es_frag_list;
	struct bgp_evpn_es_frag *es_base_frag;

	/* [EVPNES_LOCAL] originator ip address  */
	struct ipaddr originator_ip;

	/* [EVPNES_LOCAL] Route table for EVPN routes for this ESI-
	 * - Type-4 local and remote routes
	 * - Type-1 local routes
	 */
	struct bgp_table *route_table;

	/* list of PEs (bgp_evpn_es_vtep) attached to the ES */
	struct list *es_vtep_list;

	/* List of ES-EVIs associated with this ES */
	struct list *es_evi_list;

	/* List of ES-VRFs associated with this ES */
	struct list *es_vrf_list;

	/* List of MAC-IP VNI paths using this ES as destination -
	 * element is bgp_path_info_extra->es_info
	 * Note: Only local/zebra-added MACIP paths in the VNI
	 * routing table are linked to this list
	 */
	struct list *macip_evi_path_list;

	/* List of MAC-IP paths in the global routing table using this
	 * ES as destination - data is bgp_path_info_extra->es_info
	 * Note: Only non-local/imported MACIP paths in the global
	 * routing table are linked to this list
	 */
	struct list *macip_global_path_list;

	/* Number of remote VNIs referencing this ES */
	uint32_t remote_es_evi_cnt;

	uint32_t inconsistencies;
	/* there are one or more EVIs whose VTEP list doesn't match
	 * with the ES's VTEP list
	 */
#define BGP_EVPNES_INCONS_VTEP_LIST (1 << 0)

	/* number of es-evi entries whose VTEP list doesn't match
	 * with the ES's
	 */
	uint32_t incons_evi_vtep_cnt;

	/* preference config for BUM-DF election. advertised via the ESR. */
	uint16_t df_pref;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(bgp_evpn_es);
RB_HEAD(bgp_es_rb_head, bgp_evpn_es);
RB_PROTOTYPE(bgp_es_rb_head, bgp_evpn_es, rb_node, bgp_es_rb_cmp);

/* 【结构】bgp_evpn_es_vtep —— 挂在某个 ES 上的一台对端 PE(VTEP)。
 *   信息来源：从该 PE 收到的路由——ESR(Type-4) 提供 DF 选举参数；EAD-ES 与
 *   EAD-EVI(Type-1) 表明其活跃性。
 *   字段：
 *     es         : 反向指回父 ES。
 *     vtep_ip    : 该 PE 的 VTEP 地址；vtep_str 是其可打印形式。
 *     flags      : ESR    —— 收到过该 PE 的 Type-4 路由；
 *                  ACTIVE —— 已收齐 EAD-ES 与 EAD-EVI，可作为下一跳使用。
 *     evi_cnt    : 有多少个 ES-EVI 把本 VTEP 当作活跃路径引用（引用计数）。
 *     df_alg     : 该 PE 在 DF 选举中使用的算法（随 ESR 学来）。
 *     df_pref    : 该 PE 的 DF 选举偏好值（随 ESR 学来）。
 *     es_listnode: 把本项挂进 es->es_vtep_list 的链表节点。
 */
/* PE attached to an ES */
struct bgp_evpn_es_vtep {
	struct bgp_evpn_es *es; /* parent ES */
	struct ipaddr vtep_ip;

	char vtep_str[INET6_ADDRSTRLEN];

	uint32_t flags;
	/* Rxed a Type4 route from this PE */
#define BGP_EVPNES_VTEP_ESR	   (1 << 0)
	/* Active (rxed EAD-ES and EAD-EVI) and can be included as
	 * a nexthop
	 */
#define BGP_EVPNES_VTEP_ACTIVE (1 << 1)

	uint32_t evi_cnt; /* es_evis referencing this vtep as an active path */

	/* Algorithm and preference for DF election. Rxed via the ESR */
	uint8_t df_alg;
	uint16_t df_pref;

	/* memory used for adding the entry to es->es_vtep_list */
	struct listnode es_listnode;
};

/* 【结构】bgp_evpn_es_vrf —— ES-per-VRF 元素，用于管理 L3 NHG(下一跳组)。
 *   何时创建：当一个 ES-EVI 关联到某个租户 VRF 时隐式创建；它是“ES 与租户 VRF
 *   的交集”，为对称 IRB 场景下把主机路由的下一跳聚合成 NHG 服务。
 *   字段：
 *     es          : 反向指回父 ES。
 *     bgp_vrf     : 关联的租户 VRF 实例。
 *     flags       : NHG_ACTIVE——只有 ES 上存在活跃 VTEP 且该 VRF 有有效 L3-VNI 时
 *                   才可激活 NHG。
 *     rb_node     : 挂进 bgp_vrf->es_vrf_rb_tree 的 RB 节点（每 VRF 一棵）。
 *     es_listnode : 挂进 es->es_vrf_list 的链表节点。
 *     nhg_id      : IPv4 下一跳组 ID。
 *     v6_nhg_id   : IPv6 下一跳组 ID。
 *     ref_cnt     : 关联到本 ES-VRF 的 ES-EVI 个数（引用计数）。
 */
/* ES-VRF element needed for managing L3 NHGs. It is implicitly created
 * when an ES-EVI is associated with a tenant VRF
 */
struct bgp_evpn_es_vrf {
	struct bgp_evpn_es *es;
	struct bgp *bgp_vrf;

	uint32_t flags;
/* NHG can only be activated if there are active VTEPs in the ES and
 * there is a valid L3-VNI associated with the VRF
 */
#define BGP_EVPNES_VRF_NHG_ACTIVE (1 << 0)

	/* memory used for adding the es_vrf to
	 * es_vrf->bgp_vrf->es_vrf_rb_tree
	 */
	RB_ENTRY(bgp_evpn_es_vrf) rb_node;

	/* memory used for linking the es_vrf to es_vrf->es->es_vrf_list */
	struct listnode es_listnode;

	uint32_t nhg_id;
	uint32_t v6_nhg_id;

	/* Number of ES-EVI entries associated with this ES-VRF */
	uint32_t ref_cnt;
};

/* 【结构】bgp_evpn_es_evi —— ES-per-EVI：某个 ES 在某个 L2-VNI 上的视图。
 *   组织方式：按 L2-VNI 存放在 vpn->es_evi_rb_tree；同时也链进父 ES 的
 *   es->es_evi_list。本地 ES-EVI 由 zebra 按配置创建，并链进 per-VNI 的
 *   local_es_evi_list 便于快速访问；远端 ES-EVI 则在遇到引用它的
 *   bgp_evpn_es_evi_vtep 时隐式创建。
 *   flags 位：
 *     EVI_LOCAL           : 本地 ES-EVI，由 zebra 创建。
 *     EVI_REMOTE          : 由 BGP 导入的远端 VTEP 创建。
 *     EVI_INCONS_VTEP_LIST: 本 EVI 的 VTEP 列表与父 ES 的不一致。
 *   字段：
 *     es               : 父 ES。
 *     es_frag          : 所属分片（仅对 EVI_LOCAL 有意义）。
 *     vpn              : 所属 L2-VNI 实例。
 *     rb_node          : 挂进 vpn->es_evi_rb_tree 的 RB 节点。
 *     l2vni_listnode   : 挂进 vpn->local_es_evi_list 的链表节点。
 *     es_listnode      : 挂进 es->es_evi_list 的链表节点。
 *     es_frag_listnode : 挂进 es_frag->es_evi_frag_list 的链表节点。
 *     es_evi_vtep_list : 本 EVI 上挂着的对端 PE(bgp_evpn_es_evi_vtep) 列表。
 *     es_vrf           : 关联的 ES-VRF（用于 L3 NHG），可能为 NULL。
 */
/* ES per-EVI info
 * - ES-EVIs are maintained per-L2-VNI (vpn->es_evi_rb_tree)
 * - ES-EVIs are also linked to the parent ES (es->es_evi_list)
 * - Local ES-EVIs are created by zebra (via config). They are linked to a
 *   per-VNI list (vpn->local_es_evi_list) for quick access
 * - Remote ES-EVIs are created implicitly when a bgp_evpn_es_evi_vtep
 *   references it.
 */
struct bgp_evpn_es_evi {
	struct bgp_evpn_es *es;
	/* Only applicableif EVI_LOCAL */
	struct bgp_evpn_es_frag *es_frag;
	struct bgpevpn *vpn;

	/* ES-EVI flags */
	uint32_t flags;
/* local ES-EVI, created by zebra */
#define BGP_EVPNES_EVI_LOCAL            (1 << 0)
/* created via a remote VTEP imported by BGP */
#define BGP_EVPNES_EVI_REMOTE           (1 << 1)
#define BGP_EVPNES_EVI_INCONS_VTEP_LIST (1 << 2)

	/* memory used for adding the es_evi to es_evi->vpn->es_evi_rb_tree */
	RB_ENTRY(bgp_evpn_es_evi) rb_node;
	/* memory used for linking the es_evi to
	 * es_evi->vpn->local_es_evi_list
	 */
	struct listnode l2vni_listnode;
	/* memory used for linking the es_evi to
	 * es_evi->es->es_evi_list
	 */
	struct listnode es_listnode;

	/* memory used for linking the es_evi to
	 * es_evi->es_frag->es_evi_frag_list
	 */
	struct listnode es_frag_listnode;
	/* list of PEs (bgp_evpn_es_evi_vtep) attached to the ES for this VNI */
	struct list *es_evi_vtep_list;

	struct bgp_evpn_es_vrf *es_vrf;
};

/* 【结构】bgp_evpn_es_evi_vtep —— 某台对端 PE 在“某 ES 的某个 EVI”上的挂接点。
 *   创建时机：一条 EAD-per-ES 或 EAD-per-EVI(Type-1) 路由被导入该 VNI 时创建。
 *   flags 位：
 *     EAD_PER_ES : 收到过该 PE 的 EAD-per-ES 路由。
 *     EAD_PER_EVI: 收到过该 PE 的 EAD-per-EVI 路由。
 *     ACTIVE     : 该 VTEP 已活跃——会促成上层 es_vtep 的建立。
 *     EAD        : 上面两个 EAD 位的合成掩码。
 *   字段：
 *     es_evi          : 父 ES-EVI。
 *     vtep_ip         : 该 PE 的 VTEP 地址。
 *     es_evi_listnode : 挂进 es_evi->es_evi_vtep_list 的链表节点。
 *     es_vtep         : 指向 ES 级别的对应 es_vtep（活跃时建立，可能为 NULL）。
 */
/* PE attached to an ES for a VNI. This entry is created when an EAD-per-ES
 * or EAD-per-EVI Type1 route is imported into the VNI.
 */
struct bgp_evpn_es_evi_vtep {
	struct bgp_evpn_es_evi *es_evi; /* parent ES-EVI */
	struct ipaddr vtep_ip;

	uint32_t flags;
	/* Rxed an EAD-per-ES route from the PE */
#define BGP_EVPN_EVI_VTEP_EAD_PER_ES  (1 << 0) /* rxed EAD-per-ES */
	/* Rxed an EAD-per-EVI route from the PE */
#define BGP_EVPN_EVI_VTEP_EAD_PER_EVI (1 << 1) /* rxed EAD-per-EVI */
	/* VTEP is active i.e. will result in the creation of an es-vtep */
#define BGP_EVPN_EVI_VTEP_ACTIVE      (1 << 2)
#define BGP_EVPN_EVI_VTEP_EAD         (BGP_EVPN_EVI_VTEP_EAD_PER_ES |\
		BGP_EVPN_EVI_VTEP_EAD_PER_EVI)

	/* memory used for adding the entry to es_evi->es_evi_vtep_list */
	struct listnode es_evi_listnode;
	struct bgp_evpn_es_vtep *es_vtep;
};

/* 【结构】bgp_evpn_nh —— 由 EVPN 下一跳(VTEP) 建立的“VRF 侧下一跳对象”。
 *   创建时机：一条从 EVPN Type-2 路由导入 VRF 表的路径首次引用该下一跳时创建，
 *   最后一个引用释放时删除（首引用建、末引用删）。它把 RMAC(路由 MAC) 与下一跳
 *   聚合到 VRF 名下，为下发 zebra 的 FDB / NHG 做准备。
 *   字段：
 *     bgp_vrf : 反向指回所属 VRF。
 *     ip      : 下一跳/VTEP 的 IP；nh_str 是其可打印形式。
 *     rmac    : 到达该下一跳所需的路由 MAC(Remote MAC)。
 *     ref_pi  : 当前用于提取 RMAC 的参考路径(bgp_path_info)。
 *     pi_list : 使用本下一跳的 VRF 路径列表。
 *     flags   : READY_FOR_ZEBRA——已就绪、可以下发给 zebra。
 */
/* A nexthop is created when a path (imported from an EVPN type-2 route)
 * is added to the VRF route table using that nexthop.
 * It is added on first pi reference and removed on last pi deref.
 */
struct bgp_evpn_nh {
	/* backpointer to the VRF */
	struct bgp *bgp_vrf;
	/* nexthop/VTEP IP */
	struct ipaddr ip;
	/* description for easy logging */
	char nh_str[INET6_ADDRSTRLEN];
	struct ethaddr rmac;
	/* pi from which we are pulling the nh RMAC */
	struct bgp_path_info *ref_pi;
	/* List of VRF paths using this nexthop */
	struct list *pi_list;
	uint8_t flags;
#define BGP_EVPN_NH_READY_FOR_ZEBRA (1 << 0)
};

/* 【结构】bgp_evpn_mh_info —— EVPN-MH 子系统的总控块，存在 bgp_master 里，用宏
 *   bgp_mh_info(即 bm->mh_info) 访问。
 *   字段：
 *     es_rb_tree     : 全局 ES RB 树（以 ESI 为键）——本地与远端 ES 都在此。
 *     local_es_list  : 本地 ES 列表。
 *     pend_es_list   : 需要待处理/周期处理的 ES 列表。
 *     t_cons_check   : 后台一致性检查的周期定时器。
 *     ---- 以下为优化/互操作配置开关 ----
 *     ead_evi_adv_for_down_links : ES 链路 down 时也生成 EAD-EVI（可用于抑制
 *                                  链路抖动引发的更新风暴）。
 *     consistency_checking       : 是否开启 ES 一致性检查。
 *     host_routes_use_l3nhg      : 对称 IRB 中主机路由是否使用 L3 NHG。
 *     enable_ead_evi_rx          : 是否等待 EAD-per-EVI 才激活远端 ES-PE（关掉表示
 *                                  收到 EAD-per-ES 即激活，兼容不发 EAD-EVI 的厂商）。
 *     enable_ead_evi_tx          : 是否发送 EAD-EVI 通告。
 *     suppress_l3_ecomm_on_inactive_es : 本地 ES 非活跃时，通告 MAC-IP 不带 L3
 *                                  ecommunity(扩展团体)。
 *     bgp_evpn_nh_setup          : 是否在 bgpd 中建立 EVPN PE 下一跳及其 RMAC。
 *     ead_es_export_rtl          : 若配置了全局 export RT，则用它（而非 L2-VNI 的
 *                                  RT）发送 EAD-per-ES 路由。
 *     evi_per_es_frag            : 每个 ES 分片容纳的 EVI 数（构造 EAD-per-ES 用）。
 */
/* multihoming information stored in bgp_master */
#define bgp_mh_info (bm->mh_info)
struct bgp_evpn_mh_info {
	/* RB tree of Ethernet segments (used for EVPN-MH)  */
	struct bgp_es_rb_head es_rb_tree;
	/* List of local ESs */
	struct list *local_es_list;
	/* List of ESs with pending/periodic processing */
	struct list *pend_es_list;
	/* periodic timer for running background consistency checks */
	struct event *t_cons_check;

	/* config knobs for optimizing or interop */
	/* Generate EAD-EVI routes even if the ES is oper-down. This can be
	 * enabled as an optimization to avoid a storm of updates when an ES
	 * link flaps.
	 */
	bool ead_evi_adv_for_down_links;
	/* Enable ES consistency checking */
	bool consistency_checking;
	/* Use L3 NHGs for host routes in symmetric IRB */
	bool host_routes_use_l3nhg;
	/* Some vendors are not generating the EAD-per-EVI route. This knob
	 * can be turned off to activate a remote ES-PE when the EAD-per-ES
	 * route is rxed i.e. not wait on the EAD-per-EVI route
	 */
	bool enable_ead_evi_rx;
#define BGP_EVPN_MH_EAD_EVI_RX_DEF true
	/* Skip EAD-EVI advertisements by turning off this knob */
	bool enable_ead_evi_tx;
#define BGP_EVPN_MH_EAD_EVI_TX_DEF true
	/* If the Local ES is inactive we advertise the MAC-IP without the
	 * L3 ecomm
	 */
	bool suppress_l3_ecomm_on_inactive_es;
	/* Setup EVPN PE nexthops and their RMAC in bgpd */
	bool bgp_evpn_nh_setup;

	/* If global export-rts are configured that is used for sending
	 * sending the ead-per-es route instead of the L2-VNI(s) RTs
	 */
	struct list *ead_es_export_rtl;

	/* Number of EVIs in an ES fragment - used of EAD-per-ES route
	 * construction
	 */
	uint32_t evi_per_es_frag;
};

/****************************************************************************/
/* 【inline 查询族】以下一组 static inline 函数是对 ES 标志与 attr 上 ES 相关标志的
 * 轻量查询封装。读法统一为：入参 attr 可能为 NULL，故大多做空值保护并返回安全的
 * 默认值（false / 0 / zero_esi）；返回类型的细微差别(0/1 与 true/false)按原样保留。 */
/* 【函数】bgp_evpn_is_es_local —— 该 ES 是否本地(由 zebra 配置创建)。
 *   注意只检查 LOCAL 位；远端 ES(仅 REMOTE) 返回 0。 */
static inline int bgp_evpn_is_es_local(struct bgp_evpn_es *es)
{
	return CHECK_FLAG(es->flags, BGP_EVPNES_LOCAL) ? 1 : 0;
}

/* 【全局量】zero_esi —— 全 0 的 ESI，代表“无效/无 ES”。作为默认返回值使用。 */
extern esi_t *zero_esi;
/* 【函数】bgp_evpn_is_esi_valid —— 判断某 ESI 是否非全 0，即是否真正指向一个 ES。 */
static inline bool bgp_evpn_is_esi_valid(esi_t *esi)
{
	return !!memcmp(esi, zero_esi, sizeof(esi_t));
}

/* 【函数】bgp_evpn_attr_get_esi —— 取属性上的 ESI；attr 为 NULL 时返回 zero_esi。 */
static inline esi_t *bgp_evpn_attr_get_esi(struct attr *attr)
{
	return attr ? &attr->esi : zero_esi;
}

/* 【函数】bgp_evpn_attr_is_sync —— 该属性是否处于 MM(mass-withdraw) 同步状态：
 *   对端把本路由标记为 PEER_PROXY 或 PEER_ACTIVE 之一即视为同步。 */
static inline bool bgp_evpn_attr_is_sync(struct attr *attr)
{
	return attr ? !!(attr->es_flags &
		(ATTR_ES_PEER_PROXY | ATTR_ES_PEER_ACTIVE)) : false;
}

/* 【函数】bgp_evpn_attr_get_sync_seq —— 取 MM 同步序号，用于判断同步信息的新旧；
 *   attr 为 NULL 返回 0。 */
static inline uint32_t bgp_evpn_attr_get_sync_seq(struct attr *attr)
{
	return attr ?  attr->mm_sync_seqnum : 0;
}

/* 【函数】bgp_evpn_attr_is_active_on_peer —— 对端是否把该 ES 置为活跃
 *   (PEER_ACTIVE)。 */
static inline bool bgp_evpn_attr_is_active_on_peer(struct attr *attr)
{
	return attr ?
		!!(attr->es_flags & ATTR_ES_PEER_ACTIVE) : false;
}

/* 【函数】bgp_evpn_attr_is_router_on_peer —— 对端是否把该 ES 视为“有路由器
 *   (router)”类型(PEER_ROUTER)。 */
static inline bool bgp_evpn_attr_is_router_on_peer(struct attr *attr)
{
	return attr ?
		!!(attr->es_flags & ATTR_ES_PEER_ROUTER) : false;
}

/* 【函数】bgp_evpn_attr_is_proxy —— 该属性是否为代理通告(PROXY_ADVERT)。 */
static inline bool bgp_evpn_attr_is_proxy(struct attr *attr)
{
	return attr ? !!(attr->es_flags & ATTR_ES_PROXY_ADVERT) : false;
}

/* 【函数】bgp_evpn_attr_is_local_es —— 该属性是否来自本地 ES(IS_LOCAL)。 */
static inline bool bgp_evpn_attr_is_local_es(struct attr *attr)
{
	return attr ? !!(attr->es_flags & ATTR_ES_IS_LOCAL) : false;
}

/* 【函数】bgp_evpn_local_es_is_active —— 本地 ES 是否活跃：
 *   = 链路 OPER_UP 且 未处于 BYPASS 模式。 */
static inline bool bgp_evpn_local_es_is_active(struct bgp_evpn_es *es)
{
	return (es->flags & BGP_EVPNES_OPER_UP)
	       && !(es->flags & BGP_EVPNES_BYPASS);
}

/****************************************************************************/
/* 【对外接口清单】以下为本文件暴露给其他模块的函数声明。按功能分组浏览更清晰：
 *   - ES 路由表：在 per-ES 路由表里安装/卸载 Type-4(ESR) 与本地 Type-1(EAD-per-ES)；
 *   - Type-1/Type-4 报文处理入口：收到远端 EAD / ESR 路由时被 bgp_evpn.c 调用；
 *   - 本地 ES / ES-EVI 生命周期：由 zebra 通知驱动；
 *   - 远端 ES-EVI 生命周期：由导入的远端路由驱动；
 *   - 模块与 per-VNI/VRF 的初始化/收尾；
 *   - CLI / show 输出：供 bgp_evpn_vty.c 使用；
 *   - 路径(path) 与 ES/NH 的关联、L3 NHG 相关辅助、运行时开关等。
 * 各条具体语义见下方注释。 */
/* 【ES 级路由表操作】bgp_evpn_es_route_install_uninstall —— 在 ES 路由表里安装
 * (install 非 0) 或卸载 (install 为 0) 一条 Type-1/Type-4 路由，内部会重跑最优
 * 路径选择并相应增删 ES 上的 es_vtep。 */
extern int bgp_evpn_es_route_install_uninstall(struct bgp *bgp,
		struct bgp_evpn_es *es, afi_t afi, safi_t safi,
		struct prefix_evpn *evp, struct bgp_path_info *pi,
		int install);
/* 【本地 EAD 路由维护】update_type1_routes_for_evi —— 某 VNI 的配置/拓扑变化后
 * 重新生成该 EVI 的 Type-1(EAD-per-EVI) 路由；delete_global_ead_evi_routes ——
 * 从全局表删除该 VNI 的 EAD-per-EVI 路由。 */
extern void update_type1_routes_for_evi(struct bgp *bgp, struct bgpevpn *vpn);
extern int delete_global_ead_evi_routes(struct bgp *bgp, struct bgpevpn *vpn);
/* 【MH 路由更新】bgp_evpn_mh_route_update —— 多归属路由(ES 相关的 Type-1/2/4) 的
 * 统一更新入口：根据 ES/VNI 与属性决定安装/更新/删除对应路由。route_changed
 * 为出参，报告路由是否发生了变化。 */
extern int bgp_evpn_mh_route_update(struct bgp *bgp, struct bgp_evpn_es *es,
				    struct bgpevpn *vpn, afi_t afi, safi_t safi,
				    struct bgp_dest *dest, struct attr *attr,
				    struct bgp_path_info **ri,
				    int *route_changed);
/* 【Type-1 路由处理入口】bgp_evpn_type1_route_process —— 收到远端 EAD-per-ES /
 * EAD-per-EVI 时的解析与落表入口。 */
int bgp_evpn_type1_route_process(struct peer *peer, afi_t afi, safi_t safi,
		struct attr *attr, uint8_t *pfx, int psize,
		uint32_t addpath_id);
/* 【Type-4 路由处理入口】bgp_evpn_type4_route_process —— 收到远端 ESR(以太网段
 * 路由) 时的解析与落表入口，携带 DF 选举所需的 df_alg / df_pref。 */
int bgp_evpn_type4_route_process(struct peer *peer, afi_t afi, safi_t safi,
		struct attr *attr, uint8_t *pfx, int psize,
		uint32_t addpath_id);
/* 【本地 ES 增删】bgp_evpn_local_es_add —— zebra 通知“某 ES 上/下线”：创建或更新
 * 本地 ES，记录 originator_ip、oper_up、df_pref、bypass。bgp_evpn_local_es_del ——
 * 删除该本地 ES。返回 0 表示成功。 */
extern int bgp_evpn_local_es_add(struct bgp *bgp, esi_t *esi, struct ipaddr originator_ip,
				 bool oper_up, uint16_t df_pref, bool bypass);
extern int bgp_evpn_local_es_del(struct bgp *bgp, esi_t *esi);
/* 【本地 ES-EVI 增删】zebra 通知“某 ES 加入/退出某 L2-VNI”：创建或删除 local
 * ES-EVI(BGP_EVPNES_EVI_LOCAL)。返回 0 表示成功。 */
extern int bgp_evpn_local_es_evi_add(struct bgp *bgp, esi_t *esi, vni_t vni);
extern int bgp_evpn_local_es_evi_del(struct bgp *bgp, esi_t *esi, vni_t vni);
/* 【远端 ES-EVI 生命周期】由导入的远端路由驱动：_add 在导入远端 Type-1 路由时
 * 创建远端 ES-EVI(必要时连带创建远端 ES)；_del 在路由撤销时移除。返回 zebra 发送
 * 状态(zclient_send_status)。 */
extern enum zclient_send_status
bgp_evpn_remote_es_evi_add(struct bgp *bgp, struct bgpevpn *vpn,
			   const struct prefix_evpn *p,
			   struct bgp_path_info *pi);
extern enum zclient_send_status
bgp_evpn_remote_es_evi_del(struct bgp *bgp, struct bgpevpn *vpn,
			   const struct prefix_evpn *p,
			   struct bgp_path_info *pi);
/* 【模块初始化/收尾】bgp_evpn_mh_init/finish —— 进程启动/退出时初始化或销毁 MH
 * 子系统(建 ES RB 树、各 list、一致性检查定时器)；bgp_evpn_vni_es_init/cleanup ——
 * per-VNI 的 ES-EVI RB 树与本地列表的初始化/清理。 */
extern void bgp_evpn_mh_init(void);
extern void bgp_evpn_mh_finish(void);
void bgp_evpn_vni_es_init(struct bgpevpn *vpn);
void bgp_evpn_vni_es_cleanup(struct bgpevpn *vpn);
/* 【CLI / show 输出】供 bgp_evpn_vty.c 的 show 命令调用；uj 为 true 时输出 JSON，
 * detail 为 true 时输出详细信息。 */
void bgp_evpn_es_show_esi(struct vty *vty, esi_t *esi, bool uj);
void bgp_evpn_es_show(struct vty *vty, bool uj, bool detail);
void bgp_evpn_es_evi_show_vni(struct vty *vty, vni_t vni,
		bool uj, bool detail);
void bgp_evpn_es_evi_show(struct vty *vty, bool uj, bool detail);
/* 【查询】bgp_evpn_es_find —— 按 ESI 在全局 ES 树中查找 ES，找不到返回 NULL。 */
struct bgp_evpn_es *bgp_evpn_es_find(const esi_t *esi);
/* 【per-VRF 初始化】bgp_evpn_vrf_es_init —— 建该 VRF 的 es_vrf_rb_tree 等。 */
extern void bgp_evpn_vrf_es_init(struct bgp *bgp_vrf);
/* 【查询】bgp_evpn_is_esi_local_and_non_bypass —— 该 ESI 是否对应“本地且未处于
 * BYPASS 模式”的 ES，即可用于本地转发的本地 ES。 */
extern bool bgp_evpn_is_esi_local_and_non_bypass(esi_t *esi);
/* 【ES-VRF 引用计数】bgp_evpn_es_vrf_ref / deref —— ES-EVI 关联到某个租户 VRF 时
 * 增减该 ES-VRF 的引用计数(ref_cnt)，归零后释放。 */
extern void bgp_evpn_es_vrf_deref(struct bgp_evpn_es_evi *es_evi);
extern void bgp_evpn_es_vrf_ref(struct bgp_evpn_es_evi *es_evi,
				struct bgp *bgp_vrf);
/* 【路径(path) 与 ES/NH 的关联】
 *   bgp_evpn_path_mh_info_free : 释放路径上的 MH 附加信息(含 es_info / nh_info)。
 *   bgp_evpn_path_es_link      : 把路径与它所用的 ES 关联起来(pi->extra->es_info)。
 *   bgp_evpn_path_es_use_nhg   : 判断某路径能否用 L3 NHG 转发，并给出 NHG ID。 */
extern void bgp_evpn_path_mh_info_free(struct bgp_path_mh_info *mh_info);
extern void bgp_evpn_path_es_link(struct bgp_path_info *pi, vni_t vni,
				  esi_t *esi);
extern bool bgp_evpn_path_es_use_nhg(struct bgp *bgp_vrf,
				     struct bgp_path_info *pi, uint32_t *nhg_p);
/* 【ES-VRF 的 show】bgp_evpn_es_vrf_show / _show_esi —— 分别按 ES 对象或 ESI 输出
 * ES-VRF(NHG) 信息。 */
extern void bgp_evpn_es_vrf_show(struct vty *vty, bool uj,
				 struct bgp_evpn_es *es);
extern void bgp_evpn_es_vrf_show_esi(struct vty *vty, esi_t *esi, bool uj);
/* 【运行时开关】bgp_evpn_switch_ead_evi_rx —— 用户切换 enable_ead_evi_rx 配置时
 * 即时生效：重新评估远端 ES-PE 的活跃性。 */
extern void bgp_evpn_switch_ead_evi_rx(void);
/* 【L3 属性判断】bgp_evpn_es_add_l3_attrs_ok —— 该 ESI 对应的本地 ES 是否允许在
 * 通告的 MAC-IP 上附加 L3 属性(如用于对称 IRB 的 L3 ecommunity)。 */
extern bool bgp_evpn_es_add_l3_attrs_ok(esi_t *esi);
/* 【L3 NHG 判定】bgp_evpn_es_vrf_use_nhg —— 判断某 (ES, VRF) 是否应/可使用 L3 NHG：
 * 通过出参返回是否使用(use_l3nhg)、NHG 是否活跃(is_l3nhg_active)，以及对应的
 * es_vrf 对象(es_vrf_p)。 */
extern void bgp_evpn_es_vrf_use_nhg(struct bgp *bgp_vrf, esi_t *esi,
				    bool *use_l3nhg, bool *is_l3nhg_active,
				    struct bgp_evpn_es_vrf **es_vrf_p);
/* 【EVPN 下一跳管理】bgp_evpn_nh_init / finish —— per-VRF 下一跳表初始化/清理；
 * bgp_evpn_nh_show —— 输出下一跳信息；bgp_evpn_path_nh_add / del —— 路径引用/释放
 * 某个下一跳(首个引用建、末个引用删)。 */
extern void bgp_evpn_nh_init(struct bgp *bgp_vrf);
extern void bgp_evpn_nh_finish(struct bgp *bgp_vrf);
extern void bgp_evpn_nh_show(struct vty *vty, bool uj);
extern void bgp_evpn_path_nh_add(struct bgp *bgp_vrf, struct bgp_path_info *pi);
extern void bgp_evpn_path_nh_del(struct bgp *bgp_vrf, struct bgp_path_info *pi);
/* 【配置】bgp_evpn_mh_config_ead_export_rt —— 配置/取消用于 EAD-per-ES 的全局
 * export RT 列表(ecom，del 表示删除)。 */
extern void bgp_evpn_mh_config_ead_export_rt(struct bgp *bgp,
					     struct ecommunity *ecom, bool del);
extern void bgp_evpn_local_es_evi_uninstall_local_routes_in_vrfs(struct bgp_evpn_es *es,
								 struct bgp_evpn_es_evi *es_evi);

/* 【辅助】bgp_evpn_vtep_ip_to_attr_nh —— 把 VTEP IP 写入 attr 的下一跳字段：
 * v4 用 attr->nexthop / mp_nexthop_global_in，v6 用 mp_nexthop_global。 */
void bgp_evpn_vtep_ip_to_attr_nh(const struct ipaddr *vtep_ip, struct attr *attr);
/* 【运行时开关(文件尾重复声明)】切换“是否发送 EAD-per-EVI”。 */
extern void bgp_evpn_switch_ead_evi_rx(void);
extern void bgp_evpn_switch_ead_evi_tx(void);

#endif /* _FRR_BGP_EVPN_MH_H */
