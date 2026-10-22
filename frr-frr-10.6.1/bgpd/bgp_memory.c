// SPDX-License-Identifier: GPL-2.0-or-later
/* bgpd memory type definitions
 *
 * Copyright (C) 2015  David Lamparter
 */

/* ============================================================================
 * 【文件导读】bgp_memory.c —— bgpd 内存类型(MTYPE)的定义与注册
 * ============================================================================
 * 本文件是 bgp_memory.h 的“定义侧”：头文件只声明 extern 标签，这里用宏把每个
 * 标签真正定义出来，并在程序启动时注册进 FRR 的内存统计框架。每行
 *   DEFINE_MTYPE(BGPD, XXX, "描述")
 * 展开后大致做三件事(详见 lib/memory.h 的 DEFINE_MTYPE_ATTR)：
 *   1) 定义一个 struct memtype 实体 MTYPE_XXX[1]，带上名称与初始为 0 的统计字段；
 *   2) 生成一个构造函数(_CONSTRUCTOR 优先级 1001)，启动时把 MTYPE_XXX 挂进 BGPD
 *      组的 types 链表——qmalloc / qfree 就是靠这层链表把字节数累加到对应标签；
 *   3) 生成一个析构函数，卸载时把标签从链表摘除。
 * DEFINE_MGROUP(BGPD, "bgpd") 同理，定义组实体 _mg_BGPD 并挂进全局 mgroups 链。
 *
 * 【为什么计数“看不见”】本文件没有任何自增逻辑：struct memtype 里是
 * atomic_size_t 字段，真正的累加发生在 lib/memory.c 的 qmalloc / qfree 中，本文件
 * 只负责“注册标签”。它读起来像张表，却是全 bgpd 内存计量的地基。
 *
 * 【维护提示】紧跟随后的原文注释说“本文件是临时的，定义将来应移回各自使用处”，
 * 即历史上每个 .c 自己 DEFINE_MTYPE_STATIC，后来为集中管理先挪到这里。改标签
 * 描述只需改本表；增删标签要同时改 bgp_memory.h，否则链接期报未定义符号。
 *
 * 【阅读建议】与 bgp_memory.h 对照阅读：.h 是目录、.c 是内容；机制细节去
 * lib/memory.h 与 lib/memory.c。
 * ============================================================================
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bgp_memory.h"

/* this file is temporary in nature;  definitions should be moved to the
 * files they're used in */
/* 【说明】上面的英文注释是本文件的原始备注(标签定义暂集中于此，将来可能迁回
 * 各使用文件)；此处不删改它，只补中文。下面开始逐个定义标签。 */

/* 【组定义】定义 BGPD 组实体 _mg_BGPD，显示名为 "bgpd"，并挂到全局组链表上。 */
DEFINE_MGROUP(BGPD, "bgpd");
/* 【标签定义】以下每行定义一个内存标签：第 2 参是标签名、第 3 参是可读描述，
 * 与 bgp_memory.h 的 DECLARE_MTYPE 一一对应；分组含义同头文件。 */
DEFINE_MTYPE(BGPD, BGP, "BGP instance");
DEFINE_MTYPE(BGPD, BGP_NAME, "BGP Name data");
DEFINE_MTYPE(BGPD, BGP_LISTENER, "BGP listen socket details");
DEFINE_MTYPE(BGPD, BGP_PEER, "BGP peer");
DEFINE_MTYPE(BGPD, BGP_PEER_CONNECTION, "BGP peer connection");
DEFINE_MTYPE(BGPD, BGP_PEER_HOST, "BGP peer hostname");
DEFINE_MTYPE(BGPD, BGP_PEER_IFNAME, "BGP peer ifname");
DEFINE_MTYPE(BGPD, PEER_GROUP, "Peer group");
DEFINE_MTYPE(BGPD, PEER_GROUP_HOST, "BGP Peer group hostname");
DEFINE_MTYPE(BGPD, PEER_DESC, "Peer description");
DEFINE_MTYPE(BGPD, PEER_PASSWORD, "Peer password string");
DEFINE_MTYPE(BGPD, BGP_PEER_AF, "BGP peer af");
DEFINE_MTYPE(BGPD, BGP_UPDGRP, "BGP update group");
DEFINE_MTYPE(BGPD, BGP_UPD_SUBGRP, "BGP update subgroup");
DEFINE_MTYPE(BGPD, BGP_PACKET, "BGP packet");
DEFINE_MTYPE(BGPD, ATTR, "BGP attribute");
DEFINE_MTYPE(BGPD, AS_PATH, "BGP aspath");
DEFINE_MTYPE(BGPD, AS_SEG, "BGP aspath seg");
DEFINE_MTYPE(BGPD, AS_SEG_DATA, "BGP aspath segment data");
DEFINE_MTYPE(BGPD, AS_STR, "BGP aspath str");

/* 【路由表与路由节点】表/节点、单条路由、路由附加信息各变体、connected 与 static
 * 路由、通告与入/出向邻接记录、多路径信息(分组含义见 bgp_memory.h)。 */
DEFINE_MTYPE(BGPD, BGP_TABLE, "BGP table");
DEFINE_MTYPE(BGPD, BGP_NODE, "BGP node");
DEFINE_MTYPE(BGPD, BGP_ROUTE, "BGP route");
DEFINE_MTYPE(BGPD, BGP_ROUTE_EXTRA, "BGP ancillary route info");
DEFINE_MTYPE(BGPD, BGP_ROUTE_EXTRA_EVPN, "BGP extra info for EVPN");
DEFINE_MTYPE(BGPD, BGP_ROUTE_EXTRA_FS, "BGP extra info for flowspec");
DEFINE_MTYPE(BGPD, BGP_ROUTE_EXTRA_VRFLEAK, "BGP extra info for vrf leaking");
DEFINE_MTYPE(BGPD, BGP_ROUTE_EXTRA_VNC, "BGP extra info for vnc");
DEFINE_MTYPE(BGPD, BGP_CONN, "BGP connected");
DEFINE_MTYPE(BGPD, BGP_STATIC, "BGP static");
DEFINE_MTYPE(BGPD, BGP_ADVERTISE_ATTR, "BGP adv attr");
DEFINE_MTYPE(BGPD, BGP_ADVERTISE, "BGP adv");
DEFINE_MTYPE(BGPD, BGP_SYNCHRONISE, "BGP synchronise");
DEFINE_MTYPE(BGPD, BGP_ADJ_IN, "BGP adj in");
DEFINE_MTYPE(BGPD, BGP_ADJ_OUT, "BGP adj out");
DEFINE_MTYPE(BGPD, BGP_MPATH_INFO, "BGP multipath info");

/* 【AS 路径过滤】as-path 列表、AS 过滤器及其字符串。 */
DEFINE_MTYPE(BGPD, AS_LIST, "BGP AS list");
DEFINE_MTYPE(BGPD, AS_FILTER, "BGP AS filter");
DEFINE_MTYPE(BGPD, AS_FILTER_STR, "BGP AS filter str");

/* 【团体别名】community alias。 */
DEFINE_MTYPE(BGPD, COMMUNITY_ALIAS, "community alias");

/* 【团体属性】标准/扩展/大团体 各自的集合、单值与显示字符串。 */
DEFINE_MTYPE(BGPD, COMMUNITY, "community");
DEFINE_MTYPE(BGPD, COMMUNITY_VAL, "community val");
DEFINE_MTYPE(BGPD, COMMUNITY_STR, "community str");

DEFINE_MTYPE(BGPD, ECOMMUNITY, "extcommunity");
DEFINE_MTYPE(BGPD, ECOMMUNITY_VAL, "extcommunity val");
DEFINE_MTYPE(BGPD, ECOMMUNITY_STR, "extcommunity str");

DEFINE_MTYPE(BGPD, COMMUNITY_LIST, "community-list");
DEFINE_MTYPE(BGPD, COMMUNITY_LIST_NAME, "community-list name");
DEFINE_MTYPE(BGPD, COMMUNITY_LIST_ENTRY, "community-list entry");
DEFINE_MTYPE(BGPD, COMMUNITY_LIST_CONFIG, "community-list config");
DEFINE_MTYPE(BGPD, COMMUNITY_LIST_HANDLER, "community-list handler");

/* 【集群与处理队列】集群列表、BGP 处理队列、清理节点队列。 */
DEFINE_MTYPE(BGPD, CLUSTER, "Cluster list");
DEFINE_MTYPE(BGPD, CLUSTER_VAL, "Cluster list val");

DEFINE_MTYPE(BGPD, BGP_PROCESS_QUEUE, "BGP Process queue");
DEFINE_MTYPE(BGPD, BGP_CLEAR_NODE_QUEUE, "BGP node clear queue");

/* 【中转属性】transit 属性及其单值。 */
DEFINE_MTYPE(BGPD, TRANSIT, "BGP transit attr");
DEFINE_MTYPE(BGPD, TRANSIT_VAL, "BGP transit val");

/* 【调试过滤器】debug 过滤器及其字符串。 */
DEFINE_MTYPE(BGPD, BGP_DEBUG_FILTER, "BGP debug filter");
DEFINE_MTYPE(BGPD, BGP_DEBUG_STR, "BGP debug filter string");

/* 【管理距离/下一跳/联盟/接口绑定/衰减/正则/聚合/本端地址】一批零散配置与运行态。 */
DEFINE_MTYPE(BGPD, BGP_DISTANCE, "BGP distance");
DEFINE_MTYPE(BGPD, BGP_NEXTHOP_CACHE, "BGP nexthop");
DEFINE_MTYPE(BGPD, BGP_CONFED_LIST, "BGP confed list");
DEFINE_MTYPE(BGPD, PEER_UPDATE_SOURCE, "BGP peer update interface");
DEFINE_MTYPE(BGPD, PEER_CONF_IF, "BGP peer config interface");
DEFINE_MTYPE(BGPD, BGP_DAMP_INFO, "Dampening info");
DEFINE_MTYPE(BGPD, BGP_DAMP_ARRAY, "BGP Dampening array");
DEFINE_MTYPE(BGPD, BGP_DAMP_REUSELIST, "BGP Dampening reuse list");
DEFINE_MTYPE(BGPD, BGP_REGEXP, "BGP regexp");
DEFINE_MTYPE(BGPD, BGP_AGGREGATE, "BGP aggregate");
DEFINE_MTYPE(BGPD, BGP_ADDR, "BGP own address");
DEFINE_MTYPE(BGPD, TIP_ADDR, "BGP own tunnel-ip address");

/* 【重分发与过滤器名/转储串/ENCAP TLV】。 */
DEFINE_MTYPE(BGPD, BGP_REDIST, "BGP redistribution");
DEFINE_MTYPE(BGPD, BGP_FILTER_NAME, "BGP Filter Information");
DEFINE_MTYPE(BGPD, BGP_DUMP_STR, "BGP Dump String Information");
DEFINE_MTYPE(BGPD, ENCAP_TLV, "ENCAP TLV");

/* 【标签栈】MPLS 标签数组。 */
DEFINE_MTYPE(BGPD, BGP_LABELS, "BGP LABELS");

DEFINE_MTYPE(BGPD, BGP_TEA_OPTIONS, "BGP TEA Options");
DEFINE_MTYPE(BGPD, BGP_TEA_OPTIONS_VALUE, "BGP TEA Options Value");

/* 【大团体】Large Community 集合、显示串、单值。 */
DEFINE_MTYPE(BGPD, LCOMMUNITY, "Large Community");
DEFINE_MTYPE(BGPD, LCOMMUNITY_STR, "Large Community display string");
DEFINE_MTYPE(BGPD, LCOMMUNITY_VAL, "Large Community value");

/* 【EVPN 与多归属】EVPN 信息、MH 信息、ES 及其分片/每 EVI/每 VRF 数据、路径的
 * MH / ES / NH 信息、EVPN 下一跳、ES-EVI-VTEP 关联等。 */
DEFINE_MTYPE(BGPD, BGP_EVPN, "BGP EVPN Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_MH_INFO, "BGP EVPN MH Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_ES_VTEP, "BGP EVPN ES VTEP");
DEFINE_MTYPE(BGPD, BGP_EVPN_PATH_MH_INFO, "BGP EVPN PATH MH Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_PATH_ES_INFO, "BGP EVPN PATH ES Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_PATH_NH_INFO, "BGP EVPN PATH NH Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_NH, "BGP EVPN Nexthop");
DEFINE_MTYPE(BGPD, BGP_EVPN_ES_EVI_VTEP, "BGP EVPN ES-EVI VTEP");
DEFINE_MTYPE(BGPD, BGP_EVPN_ES, "BGP EVPN ESI Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_ES_FRAG, "BGP EVPN ES Fragment Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_ES_EVI, "BGP EVPN ES-per-EVI Information");
DEFINE_MTYPE(BGPD, BGP_EVPN_ES_VRF, "BGP EVPN ES-per-VRF Information");
/* 【EVPN 导入 RT】VPN 与 VRF 两套 Route-Target 匹配项。 */
DEFINE_MTYPE(BGPD, BGP_EVPN_IMPORT_RT, "BGP EVPN Import RT");
DEFINE_MTYPE(BGPD, BGP_EVPN_VRF_IMPORT_RT, "BGP EVPN VRF Import RT");

/* 【SRv6】SRv6 L3 服务 / VPN 服务、SID、Function。 */
DEFINE_MTYPE(BGPD, BGP_SRV6_L3SERVICE, "BGP prefix-sid SRV6 L3 Service");
DEFINE_MTYPE(BGPD, BGP_SRV6_VPN, "BGP prefix-sid srv6 vpn service");
DEFINE_MTYPE(BGPD, BGP_SRV6_SID, "BGP srv6 segment-id");
DEFINE_MTYPE(BGPD, BGP_SRV6_FUNCTION, "BGP srv6 function");
/* 【EVPN 远端 IP】远端 IP 哈希表条目。 */
DEFINE_MTYPE(BGPD, EVPN_REMOTE_IP, "BGP EVPN Remote IP hash entry");

/* 【Notification 报文】对端 NOTIFICATION 内容快照。 */
DEFINE_MTYPE(BGPD, BGP_NOTIFICATION, "BGP Notification Message");

/* 【软件版本串】对端 SOFTWARE_VERSION 能力里的版本字符串。 */
DEFINE_MTYPE(BGPD, BGP_SOFT_VERSION, "Software Version");

/* 【EVPN Overlay】overlay 索引映射。 */
DEFINE_MTYPE(BGPD, BGP_EVPN_OVERLAY, "BGP EVPN Overlay");

/* 【NHC】Nexthop Cache 相关结构及其 TLV。 */
DEFINE_MTYPE(BGPD, BGP_NHC, "BGP NHC");
DEFINE_MTYPE(BGPD, BGP_NHC_TLV, "BGP NHC TLV");
DEFINE_MTYPE(BGPD, BGP_NHC_TLV_VAL, "BGP NHC TLV value");
