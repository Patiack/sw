// SPDX-License-Identifier: GPL-2.0-or-later
/* bgpd memory type declarations
 *
 * Copyright (C) 2015  David Lamparter
 */

/* ============================================================================
 * 【文件导读】bgp_memory.h —— bgpd 全体内存类型(MTYPE)的集中声明
 * ============================================================================
 * 【解决什么问题】FRR 自建了一套带“归类标签”的内存分配：每块用 qmalloc /
 * qcalloc / XCALLOC 申请的内存都要先指定一个 memtype 作为标签。这样 CLI 的
 * show memory（以及进程退出时的内存泄漏检查）就能按标签统计“某模块当前占用
 * 多少、峰值多少、累计分配多少次”，排障与查泄漏全靠它。标签分两级：
 *   memgroup(MGROUP) : 大分组，本文件里只有一个 BGPD，代表整个 bgpd 守护进程。
 *   memtype (MTYPE)  : 具体分类，下面每行一个，例如 BGP_PEER / ATTR / BGP_TABLE。
 *
 * 【为什么声明与定义要分开】标签要在“谁用谁 include”的多个 .c 文件里可见，
 * 于是约定：声明集中放本头文件，定义只在单个 .c(bgp_memory.c) 出现。分工是：
 *   DECLARE_MGROUP / DECLARE_MTYPE (本文件, .h) : 仅声明 extern 符号，不产生实体。
 *   DEFINE_MGROUP  / DEFINE_MTYPE  (bgp_memory.c): 真正定义内存实体 + 注册构造函数，
 *       程序启动时把标签挂进内存管理器链表，纳入全局统计。
 *
 * 【宏展开后的真正东西】(关键，阅读时请脑补展开)
 *   DECLARE_MTYPE(BGP_PEER) 展开为:
 *       extern struct memtype MTYPE_BGP_PEER[1];
 *     即 MTYPE_BGP_PEER 是“含 1 个元素的 struct memtype 数组”。用数组而非标量，
 *     是为了让同一符号既能当指针(数组退化为指针)传给 qmalloc，又能当实体定义，
 *     省掉一次显式取地址。
 *   DECLARE_MGROUP(BGPD) 展开为:
 *       extern struct memgroup _mg_BGPD;
 *     注意 MGROUP 不加 MTYPE_ 前缀，它对应的是 _mg_ 前缀的 memgroup 结构。
 *   **没有 mt_count 之类的手写计数**：计数是 struct memtype 里的那几个
 *   atomic_size_t 字段(n_alloc / size / total / ...)，由 qmalloc 与 qfree 在运行时
 *   原子累加更新，源码中看不到手工自增。
 *
 * 【和使用者的约定】本文件每声明一个 MTYPE_XXX，bgp_memory.c 里就对应一行
 * DEFINE_MTYPE(BGPD, XXX, "描述文字")；两边 XXX 必须一一对应，否则链接期报
 * 未定义符号。全 bgpd 用到的标签都在本文件列出，可视为“bgpd 内存全景图”。
 *
 * 【阅读建议】先看本文件的标签清单(了解 bgpd 会分配哪些类别)，再翻到
 * bgp_memory.c 看宏如何展开注册；想深入机制去 lib/memory.h 看 struct memtype
 * 与 qmalloc 的原子计数实现。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_MEMORY_H
#define _QUAGGA_BGP_MEMORY_H

#include "memory.h"

/* 【组】BGPD：整个 bgpd 的内存大分组；下面所有 MTYPE 都挂在这个组下。 */
DECLARE_MGROUP(BGPD);
/* 【进程实例与配置类】BGP 实例本体、名称、监听 socket、邻居(peer)及其连接/主机名/
 * 接口名、peer-group、描述、密码、peer 的地址族配置、更新组/子组、报文缓冲。 */
DECLARE_MTYPE(BGP);
DECLARE_MTYPE(BGP_NAME);
DECLARE_MTYPE(BGP_LISTENER);
DECLARE_MTYPE(BGP_PEER);
DECLARE_MTYPE(BGP_PEER_CONNECTION);
DECLARE_MTYPE(BGP_PEER_HOST);
DECLARE_MTYPE(BGP_PEER_IFNAME);
DECLARE_MTYPE(PEER_GROUP);
DECLARE_MTYPE(PEER_GROUP_HOST);
DECLARE_MTYPE(PEER_DESC);
DECLARE_MTYPE(PEER_PASSWORD);
DECLARE_MTYPE(BGP_PEER_AF);
DECLARE_MTYPE(BGP_UPDGRP);
DECLARE_MTYPE(BGP_UPD_SUBGRP);
DECLARE_MTYPE(BGP_PACKET);
DECLARE_MTYPE(ATTR);
DECLARE_MTYPE(AS_PATH);
DECLARE_MTYPE(AS_SEG);
DECLARE_MTYPE(AS_SEG_DATA);
DECLARE_MTYPE(AS_STR);

/* 【路由表与路由节点】BGP 表/节点、单条路由、路由的附加信息(EXTRA 各变体分别对应
 * EVPN / flowspec / VRF 泄漏 / VNC 的额外数据)、connected 与 static 路由、通告
 * 记录(ADVERTISE)与入/出向邻接记录(ADJ_IN / ADJ_OUT)、多路径信息(MPATH_INFO)。 */
DECLARE_MTYPE(BGP_TABLE);
DECLARE_MTYPE(BGP_NODE);
DECLARE_MTYPE(BGP_ROUTE);
DECLARE_MTYPE(BGP_ROUTE_EXTRA);
DECLARE_MTYPE(BGP_ROUTE_EXTRA_EVPN);
DECLARE_MTYPE(BGP_ROUTE_EXTRA_FS);
DECLARE_MTYPE(BGP_ROUTE_EXTRA_VRFLEAK);
DECLARE_MTYPE(BGP_ROUTE_EXTRA_VNC);
DECLARE_MTYPE(BGP_CONN);
DECLARE_MTYPE(BGP_STATIC);
DECLARE_MTYPE(BGP_ADVERTISE_ATTR);
DECLARE_MTYPE(BGP_ADVERTISE);
DECLARE_MTYPE(BGP_SYNCHRONISE);
DECLARE_MTYPE(BGP_ADJ_IN);
DECLARE_MTYPE(BGP_ADJ_OUT);
DECLARE_MTYPE(BGP_MPATH_INFO);

/* 【AS 路径过滤】as-path 访问列表(AS_LIST)、AS 过滤器(AS_FILTER)及其字符串缓存。 */
DECLARE_MTYPE(AS_LIST);
DECLARE_MTYPE(AS_FILTER);
DECLARE_MTYPE(AS_FILTER_STR);

/* 【团体别名】community alias（给团体值起可读别名，供配置/显示用）。 */
DECLARE_MTYPE(COMMUNITY_ALIAS);

/* 【团体属性】标准团体(COMMUNITY)、扩展团体(ECOMMUNITY)、大团体(LCOMMUNITY)各自的
 * 集合、单值(VAL)与显示字符串(STR)；下方 COMMUNITY_LIST 是团体列表过滤器的相关项。 */
DECLARE_MTYPE(COMMUNITY);
DECLARE_MTYPE(COMMUNITY_VAL);
DECLARE_MTYPE(COMMUNITY_STR);

DECLARE_MTYPE(ECOMMUNITY);
DECLARE_MTYPE(ECOMMUNITY_VAL);
DECLARE_MTYPE(ECOMMUNITY_STR);

/* 【团体列表过滤器】community-list 本体、名称、条目、配置与处理句柄。 */
DECLARE_MTYPE(COMMUNITY_LIST);
DECLARE_MTYPE(COMMUNITY_LIST_NAME);
DECLARE_MTYPE(COMMUNITY_LIST_ENTRY);
DECLARE_MTYPE(COMMUNITY_LIST_CONFIG);
DECLARE_MTYPE(COMMUNITY_LIST_HANDLER);

/* 【集群与处理队列】路由反射器集群列表(CLUSTER)、BGP 处理队列、路由清理节点队列。 */
DECLARE_MTYPE(CLUSTER);
DECLARE_MTYPE(CLUSTER_VAL);

DECLARE_MTYPE(BGP_PROCESS_QUEUE);
DECLARE_MTYPE(BGP_CLEAR_NODE_QUEUE);

/* 【中转属性】transit(未知/中转路径属性)及其单值。 */
DECLARE_MTYPE(TRANSIT);
DECLARE_MTYPE(TRANSIT_VAL);

/* 【调试过滤器】BGP debug 过滤器及其字符串。 */
DECLARE_MTYPE(BGP_DEBUG_FILTER);
DECLARE_MTYPE(BGP_DEBUG_STR);

/* 【管理距离/下一跳/联盟/接口绑定/衰减/正则/聚合/本端地址】BGP 的一批零散配置与
 * 运行态结构：距离值、下一跳缓存、联盟列表、update-source 接口、接口绑定、
 * 路由衰减(dampening)相关、正则、聚合体、本端地址(BGP_ADDR / TIP_ADDR)。 */
DECLARE_MTYPE(BGP_DISTANCE);
DECLARE_MTYPE(BGP_NEXTHOP_CACHE);
DECLARE_MTYPE(BGP_CONFED_LIST);
DECLARE_MTYPE(PEER_UPDATE_SOURCE);
DECLARE_MTYPE(PEER_CONF_IF);
DECLARE_MTYPE(BGP_DAMP_INFO);
DECLARE_MTYPE(BGP_DAMP_ARRAY);
DECLARE_MTYPE(BGP_DAMP_REUSELIST);
DECLARE_MTYPE(BGP_REGEXP);
DECLARE_MTYPE(BGP_AGGREGATE);
DECLARE_MTYPE(BGP_ADDR);
DECLARE_MTYPE(TIP_ADDR);

/* 【重分发与过滤器名/转储串/ENCAP TLV】重分发配置、过滤器名、MRT dump 用的字符串、
 * 以及 MP-BGP 封装属性(ENCAP)的 TLV。 */
DECLARE_MTYPE(BGP_REDIST);
DECLARE_MTYPE(BGP_FILTER_NAME);
DECLARE_MTYPE(BGP_DUMP_STR);
DECLARE_MTYPE(ENCAP_TLV);

/* 【标签栈】MPLS 标签数组(如收到的 label stack)。 */
DECLARE_MTYPE(BGP_LABELS);

/* 【TEA 选项】Traffic Engineering Action 选项及其取值(由 SR-Policy 等使用)。 */
DECLARE_MTYPE(BGP_TEA_OPTIONS);
DECLARE_MTYPE(BGP_TEA_OPTIONS_VALUE);

/* 【大团体】Large Community(32 位 AS / 值 / 值)的集合、显示串与单值。 */
DECLARE_MTYPE(LCOMMUNITY);
DECLARE_MTYPE(LCOMMUNITY_STR);
DECLARE_MTYPE(LCOMMUNITY_VAL);

/* 【EVPN 多归属(MH)与 ES】EVPN 多归属信息、以太网段(ES)及其分片/每 EVI/每 VRF 维度
 * 的数据、路径的 ES / MH / 下一跳信息、EVPN 下一跳、ES-EVI-VTEP 关联等。 */
DECLARE_MTYPE(BGP_EVPN_MH_INFO);
DECLARE_MTYPE(BGP_EVPN_ES);
DECLARE_MTYPE(BGP_EVPN_ES_FRAG);
DECLARE_MTYPE(BGP_EVPN_ES_EVI);
DECLARE_MTYPE(BGP_EVPN_ES_VRF);
DECLARE_MTYPE(BGP_EVPN_ES_VTEP);
DECLARE_MTYPE(BGP_EVPN_PATH_ES_INFO);
DECLARE_MTYPE(BGP_EVPN_PATH_MH_INFO);
DECLARE_MTYPE(BGP_EVPN_PATH_NH_INFO);
DECLARE_MTYPE(BGP_EVPN_NH);
DECLARE_MTYPE(BGP_EVPN_ES_EVI_VTEP);

DECLARE_MTYPE(BGP_EVPN);
/* 【EVPN 导入 RT】从 EVPN 路由导入 VRF 时用的 Route-Target 匹配项(VPN 与 VRF 两套)。 */
DECLARE_MTYPE(BGP_EVPN_IMPORT_RT);
DECLARE_MTYPE(BGP_EVPN_VRF_IMPORT_RT);

/* 【SRv6】SRv6 L3 服务 / VPN 服务、SID、Function 相关结构。 */
DECLARE_MTYPE(BGP_SRV6_L3SERVICE);
DECLARE_MTYPE(BGP_SRV6_VPN);
DECLARE_MTYPE(BGP_SRV6_SID);
DECLARE_MTYPE(BGP_SRV6_FUNCTION);

/* 【EVPN 远端 IP】EVPN 远端 IP 哈希表条目。 */
DECLARE_MTYPE(EVPN_REMOTE_IP);

/* 【Notification 报文】对端发来的 NOTIFICATION 内容(错误码/子码/数据)的快照。 */
DECLARE_MTYPE(BGP_NOTIFICATION);

/* 【软件版本串】对端 SOFTWARE_VERSION 能力里收到的版本字符串。 */
DECLARE_MTYPE(BGP_SOFT_VERSION);

/* 【EVPN Overlay】EVPN 的 Overlay 索引(把 IP 映射到 overlay 索引，用于哈希)。 */
DECLARE_MTYPE(BGP_EVPN_OVERLAY);

/* 【NHC】BGP Nexthop-tracking / Nexthop Cache 相关结构及其 TLV。 */
DECLARE_MTYPE(BGP_NHC);
DECLARE_MTYPE(BGP_NHC_TLV);
DECLARE_MTYPE(BGP_NHC_TLV_VAL);

/* 【清理批处理】clear 路由时把待处理节点攒成一批的批量队列元素。 */
DECLARE_MTYPE(CLEARING_BATCH);

#endif /* _QUAGGA_BGP_MEMORY_H */
