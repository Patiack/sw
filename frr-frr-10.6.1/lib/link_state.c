// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Link State Database - link_state.c
 *
 * Author: Olivier Dugeon <olivier.dugeon@orange.com>
 *
 * Copyright (C) 2020 Orange http://www.orange.com
 *
 * This file is part of Free Range Routing (FRR).
 */

/*
 * ============================ 文件导读 ============================
 * 本文件是 link_state.h 所定义模型的实现，提供一个协议无关的“链路
 * 状态数据库(LSDB)”及其上的图(Graph)视图。它被 OSPF / IS-IS 用作
 * 存储，被 RSVP-TE / PCEP / BGP-LS 等用作消费源。
 *
 * 数据结构分三层：
 *   1) 基础元素：ls_node(节点) / ls_attributes(属性) / ls_prefix(前缀)，
 *      由 OSPF / IS-IS 填充，各带一个 flags 位掩码标记有效字段。
 *   2) 图元素：ls_vertex(顶点，持有一个 ls_node) / ls_edge(边，持有一
 *      组 ls_attributes) / ls_subnet(子网，持有一个 ls_prefix)，分别
 *      挂在 TED 的三棵 RB 树上，并用 incoming / outgoing 链表维护拓扑。
 *   3) 容器：ls_ted 聚合上述三棵树；另有 ls_message 用于在各守护进程
 *      间搬运单个元素。
 *
 * 生命周期与所有权(易踩坑)：
 *   - ls_vertex / ls_edge / ls_subnet 只是“借用”对应的 node / attrs /
 *     prefix 指针；常规的 _del 不释放被引用对象，带 _all 后缀的版本才
 *     会连同被引用对象一起释放。
 *   - 各对象统一由 MTYPE_LS_DB 内存类型分配，便于内存统计与泄漏排查。
 *   - *_same() 的返回语义是“是否相等”(即 == 的真值)，而非比较结果。
 *
 * 跨进程交互：本文件实现了经 zebra 的 ZAPI Opaque(Link State 类型)收
 * 发 ls_message 的完整链路——注册 / 注销、请求全量同步、解析、发送，
 * 以及 msg 与图元素之间的相互转换(ls_*2msg 与 ls_msg2*)。
 *
 * 阅读顺序建议：先看基础元素的 new / del / same，再看图元素与 TED 的
 * add / update / find，最后看 ls_message 的编解码与 sync。
 * ================================================================
 */

#include <zebra.h>

#include "if.h"
#include "linklist.h"
#include "log.h"
#include "command.h"
#include "termtable.h"
#include "memory.h"
#include "prefix.h"
#include "table.h"
#include "vty.h"
#include "zclient.h"
#include "stream.h"
#include "sbuf.h"
#include "printfrr.h"
#include <lib/json.h>
#include "link_state.h"
#include "iso.h"

/*
 * 本模块统一使用的内存类型。所有动态分配都走 MTYPE_LS_DB，便于
 * show memory 统计与泄漏排查。
 */
/* Link State Memory allocation */
DEFINE_MTYPE_STATIC(LIB, LS_DB, "Link State Database");

/**
 *  Link State Node management functions
 */
/*
 * 判断两个节点标识是否相同，按值与 origin 比较：
 *   - origin 不同即不同；origin 为 UNKNOWN 时直接视为相同。
 *   - IS-IS：比较 sys_id(6 字节)与 level。
 *   - OSPF：比较 addr 与 area_id。
 * 返回 1 表示相同、0 表示不同(语义是 == 的真值，不是“比较结果”)。
 * 注意：OSPF 分支里地址或 Area-ID 任一不同就 return 1，这看起来像
 * 上游的一处笔误(按语义应为 return 0)；此处按代码原样说明，阅读时
 * 请以实际行为为准。
 */
int ls_node_id_same(struct ls_node_id i1, struct ls_node_id i2)
{
	if (i1.origin != i2.origin)
		return 0;

	if (i1.origin == UNKNOWN)
		return 1;

	if (i1.origin == ISIS_L1 || i1.origin == ISIS_L2) {
		if (memcmp(i1.id.iso.sys_id, i2.id.iso.sys_id, ISO_SYS_ID_LEN)
			    != 0
		    || (i1.id.iso.level != i2.id.iso.level))
			return 0;
	} else {
		if (!IPV4_ADDR_SAME(&i1.id.ip.addr, &i2.id.ip.addr)
		    || !IPV4_ADDR_SAME(&i1.id.ip.area_id, &i2.id.ip.area_id))
			return 1;
	}

	return 1;
}

/*
 * 新建一个链路状态节点。adv 为必填的广告标识，origin 为 UNKNOWN 时
 * 直接返回 NULL。rid / rid6 非空(未指定)时填入并置
 * LS_NODE_ROUTER_ID / LS_NODE_ROUTER_ID6 标志。若 rid 为空且 origin
 * 为 OSPF / STATIC / DIRECT，则退回用 adv 里的 OSPF Router-ID 作为
 * router_id。返回新节点(内存来自 MTYPE_LS_DB)，由 ls_node_del 释放；
 * 内部用 XCALLOC，分配失败会直接终止进程。
 */
struct ls_node *ls_node_new(struct ls_node_id adv, struct in_addr rid,
			    struct in6_addr rid6)
{
	struct ls_node *new;

	if (adv.origin == UNKNOWN)
		return NULL;

	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_node));
	new->adv = adv;
	if (!IPV4_NET0(rid.s_addr)) {
		new->router_id = rid;
		SET_FLAG(new->flags, LS_NODE_ROUTER_ID);
	} else {
		if (adv.origin == OSPFv2 || adv.origin == STATIC
		    || adv.origin == DIRECT) {
			new->router_id = adv.id.ip.addr;
			SET_FLAG(new->flags, LS_NODE_ROUTER_ID);
		}
	}
	if (!IN6_IS_ADDR_UNSPECIFIED(&rid6)) {
		new->router_id6 = rid6;
		SET_FLAG(new->flags, LS_NODE_ROUTER_ID6);
	}
	return new;
}

/*
 * 释放一个链路状态节点。node 为 NULL 时直接返回(幂等)。
 */
void ls_node_del(struct ls_node *node)
{
	if (!node)
		return;

	XFREE(MTYPE_LS_DB, node);
}

/*
 * 判断两个节点是否相等。先处理空指针与同一指针；再比较 flags 与 adv
 * 标识；最后仅对 flags 标记为有效的字段逐一比较(名称、Router-ID、
 * 节点标志、类型、AS 号、SR / SRv6 信息等)。只要 flags 不同即判为
 * 不等。返回 1 相等 / 0 不等。
 */
int ls_node_same(struct ls_node *n1, struct ls_node *n2)
{
	/* First, check pointer */
	if ((n1 && !n2) || (!n1 && n2))
		return 0;

	if (n1 == n2)
		return 1;

	/* Then, verify Flags and Origin */
	if (n1->flags != n2->flags)
		return 0;

	if (!ls_node_id_same(n1->adv, n2->adv))
		return 0;

	/* Finally, check each individual parameters that are valid */
	if (CHECK_FLAG(n1->flags, LS_NODE_NAME)
	    && (strncmp(n1->name, n2->name, MAX_NAME_LENGTH) != 0))
		return 0;
	if (CHECK_FLAG(n1->flags, LS_NODE_ROUTER_ID)
	    && !IPV4_ADDR_SAME(&n1->router_id, &n2->router_id))
		return 0;
	if (CHECK_FLAG(n1->flags, LS_NODE_ROUTER_ID6)
	    && !IPV6_ADDR_SAME(&n1->router_id6, &n2->router_id6))
		return 0;
	if (CHECK_FLAG(n1->flags, LS_NODE_FLAG)
	    && (n1->node_flag != n2->node_flag))
		return 0;
	if (CHECK_FLAG(n1->flags, LS_NODE_TYPE) && (n1->type != n2->type))
		return 0;
	if (CHECK_FLAG(n1->flags, LS_NODE_AS_NUMBER)
	    && (n1->as_number != n2->as_number))
		return 0;
	if (CHECK_FLAG(n1->flags, LS_NODE_SR)) {
		if (n1->srgb.flag != n2->srgb.flag
		    || n1->srgb.lower_bound != n2->srgb.lower_bound
		    || n1->srgb.range_size != n2->srgb.range_size)
			return 0;
		if ((n1->algo[0] != n2->algo[0])
		    || (n1->algo[1] != n2->algo[1]))
			return 0;
		if (CHECK_FLAG(n1->flags, LS_NODE_SRLB)
		    && ((n1->srlb.lower_bound != n2->srlb.lower_bound
			 || n1->srlb.range_size != n2->srlb.range_size)))
			return 0;
		if (CHECK_FLAG(n1->flags, LS_NODE_MSD) && (n1->msd != n2->msd))
			return 0;
	}
	if (CHECK_FLAG(n1->flags, LS_NODE_SRV6)) {
		if (n1->srv6_cap_flags != n2->srv6_cap_flags)
			return 0;
		if (memcmp(&n1->srv6_msd, &n2->srv6_msd, sizeof(n1->srv6_msd)))
			return 0;
	}

	/* OK, n1 & n2 are equal */
	return 1;
}

/**
 *  Link State Attributes management functions
 */
/*
 * 新建一组链路状态属性。要求 adv.origin 非 UNKNOWN，且 local / local6 /
 * local_id 三者至少有一个非空(否则释放并返回 NULL)。会初始化扩展
 * 管理组 ext_admin_group。返回新属性，由 ls_attributes_del 释放。
 */
struct ls_attributes *ls_attributes_new(struct ls_node_id adv,
					struct in_addr local,
					struct in6_addr local6,
					uint32_t local_id)
{
	struct ls_attributes *new;

	if (adv.origin == UNKNOWN)
		return NULL;

	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_attributes));
	new->adv = adv;
	if (!IPV4_NET0(local.s_addr)) {
		new->standard.local = local;
		SET_FLAG(new->flags, LS_ATTR_LOCAL_ADDR);
	}
	if (!IN6_IS_ADDR_UNSPECIFIED(&local6)) {
		new->standard.local6 = local6;
		SET_FLAG(new->flags, LS_ATTR_LOCAL_ADDR6);
	}
	if (local_id != 0) {
		new->standard.local_id = local_id;
		SET_FLAG(new->flags, LS_ATTR_LOCAL_ID);
	}

	/* Check that almost one identifier is set */
	if (!CHECK_FLAG(new->flags, LS_ATTR_LOCAL_ADDR | LS_ATTR_LOCAL_ADDR6
	    | LS_ATTR_LOCAL_ID)) {
		XFREE(MTYPE_LS_DB, new);
		return NULL;
	}

	admin_group_init(&new->ext_admin_group);

	return new;
}

/*
 * 释放属性中的 SRLG 列表，并把 srlgs 置空、srlg_len 清零，同时清除
 * LS_ATTR_SRLG 标志位。attr 为 NULL 时直接返回。
 */
void ls_attributes_srlg_del(struct ls_attributes *attr)
{
	if (!attr)
		return;

	if (attr->srlgs)
		XFREE(MTYPE_LS_DB, attr->srlgs);

	attr->srlgs = NULL;
	attr->srlg_len = 0;
	UNSET_FLAG(attr->flags, LS_ATTR_SRLG);
}

/*
 * 释放一组链路状态属性：先释放 SRLG 列表，再终止扩展管理组，最后
 * 释放结构体本身。attr 为 NULL 时直接返回。
 */
void ls_attributes_del(struct ls_attributes *attr)
{
	if (!attr)
		return;

	ls_attributes_srlg_del(attr);

	admin_group_term(&attr->ext_admin_group);

	XFREE(MTYPE_LS_DB, attr);
}

/*
 * 判断两组属性是否相等。先比指针 / flags / adv，再仅对 flags 标记为
 * 有效的字段逐一比较：名称、各类度量、管理组、本端与邻居地址、带宽、
 * 时延与抖动与丢包，以及各种邻接 SID、SRv6、SRLG 等。返回 1 / 0。
 */
int ls_attributes_same(struct ls_attributes *l1, struct ls_attributes *l2)
{
	/* First, check pointer */
	if ((l1 && !l2) || (!l1 && l2))
		return 0;

	if (l1 == l2)
		return 1;

	/* Then, verify Flags and Origin */
	if (l1->flags != l2->flags)
		return 0;

	if (!ls_node_id_same(l1->adv, l2->adv))
		return 0;

	/* Finally, check each individual parameters that are valid */
	if (CHECK_FLAG(l1->flags, LS_ATTR_NAME)
	    && strncmp(l1->name, l2->name, MAX_NAME_LENGTH) != 0)
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_METRIC) && (l1->metric != l2->metric))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_TE_METRIC)
	    && (l1->standard.te_metric != l2->standard.te_metric))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_ADM_GRP)
	    && (l1->standard.admin_group != l2->standard.admin_group))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_EXT_ADM_GRP) &&
	    !admin_group_cmp(&l1->ext_admin_group, &l2->ext_admin_group))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_LOCAL_ADDR)
	    && !IPV4_ADDR_SAME(&l1->standard.local, &l2->standard.local))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_NEIGH_ADDR)
	    && !IPV4_ADDR_SAME(&l1->standard.remote, &l2->standard.remote))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_LOCAL_ADDR6)
	    && !IPV6_ADDR_SAME(&l1->standard.local6, &l2->standard.local6))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_NEIGH_ADDR6)
	    && !IPV6_ADDR_SAME(&l1->standard.remote6, &l2->standard.remote6))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_LOCAL_ID)
	    && (l1->standard.local_id != l2->standard.local_id))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_NEIGH_ID)
	    && (l1->standard.remote_id != l2->standard.remote_id))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_MAX_BW)
	    && (l1->standard.max_bw != l2->standard.max_bw))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_MAX_RSV_BW)
	    && (l1->standard.max_rsv_bw != l2->standard.max_rsv_bw))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_UNRSV_BW)
	    && memcmp(&l1->standard.unrsv_bw, &l2->standard.unrsv_bw, 32) != 0)
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_REMOTE_AS)
	    && (l1->standard.remote_as != l2->standard.remote_as))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_REMOTE_ADDR)
	    && !IPV4_ADDR_SAME(&l1->standard.remote_addr,
			       &l2->standard.remote_addr))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_REMOTE_ADDR6)
	    && !IPV6_ADDR_SAME(&l1->standard.remote_addr6,
			       &l2->standard.remote_addr6))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_DELAY)
	    && (l1->extended.delay != l2->extended.delay))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_MIN_MAX_DELAY)
	    && ((l1->extended.min_delay != l2->extended.min_delay)
		|| (l1->extended.max_delay != l2->extended.max_delay)))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_JITTER)
	    && (l1->extended.jitter != l2->extended.jitter))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_PACKET_LOSS)
	    && (l1->extended.pkt_loss != l2->extended.pkt_loss))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_AVA_BW)
	    && (l1->extended.ava_bw != l2->extended.ava_bw))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_RSV_BW)
	    && (l1->extended.rsv_bw != l2->extended.rsv_bw))
		return 0;
	if (CHECK_FLAG(l1->flags, LS_ATTR_USE_BW)
	    && (l1->extended.used_bw != l2->extended.used_bw))
		return 0;
	for (int i = 0; i < LS_ADJ_MAX; i++) {
		if (!CHECK_FLAG(l1->flags, (LS_ATTR_ADJ_SID << i)))
			continue;
		if ((l1->adj_sid[i].sid != l2->adj_sid[i].sid)
		    || (l1->adj_sid[i].flags != l2->adj_sid[i].flags)
		    || (l1->adj_sid[i].weight != l2->adj_sid[i].weight))
			return 0;
		if (((l1->adv.origin == ISIS_L1) || (l1->adv.origin == ISIS_L2))
		    && (memcmp(&l1->adj_sid[i].neighbor.sysid,
			       &l2->adj_sid[i].neighbor.sysid, ISO_SYS_ID_LEN)
			!= 0))
			return 0;
		if (((l1->adv.origin == OSPFv2) || (l1->adv.origin == STATIC)
		     || (l1->adv.origin == DIRECT))
		    && (i < ADJ_PRI_IPV6)
		    && (!IPV4_ADDR_SAME(&l1->adj_sid[i].neighbor.addr,
					&l2->adj_sid[i].neighbor.addr)))
			return 0;
	}
	for (int i = 0; i < ADJ_SRV6_MAX; i++) {
		if (!CHECK_FLAG(l1->flags, (LS_ATTR_ADJ_SRV6SID << i)))
			continue;
		if (memcmp(&l1->adj_srv6_sid[i].sid, &l2->adj_srv6_sid[i].sid,
			   sizeof(struct in6_addr)) ||
		    (l1->adj_srv6_sid[i].flags != l2->adj_srv6_sid[i].flags) ||
		    (l1->adj_srv6_sid[i].weight != l2->adj_srv6_sid[i].weight) ||
		    (l1->adj_srv6_sid[i].endpoint_behavior !=
		     l2->adj_srv6_sid[i].endpoint_behavior))
			return 0;
		if (((l1->adv.origin == ISIS_L1) ||
		     (l1->adv.origin == ISIS_L2)) &&
		    (memcmp(&l1->adj_srv6_sid[i].neighbor.sysid,
			    &l2->adj_srv6_sid[i].neighbor.sysid,
			    ISO_SYS_ID_LEN) != 0))
			return 0;
	}
	if (CHECK_FLAG(l1->flags, LS_ATTR_SRLG)
	    && ((l1->srlg_len != l2->srlg_len)
		|| memcmp(l1->srlgs, l2->srlgs,
			  l1->srlg_len * sizeof(uint32_t))
			   != 0))
		return 0;

	/* OK, l1 & l2 are equal */
	return 1;
}

/**
 *  Link State prefix management functions
 */
/*
 * 新建一个链路状态前缀。adv.origin 为 UNKNOWN 时返回 NULL；否则把传入
 * 的前缀 p 整体复制进 new->pref。由 ls_prefix_del 释放。
 */
struct ls_prefix *ls_prefix_new(struct ls_node_id adv, struct prefix *p)
{
	struct ls_prefix *new;

	if (adv.origin == UNKNOWN)
		return NULL;

	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_prefix));
	new->adv = adv;
	new->pref = *p;

	return new;
}

/*
 * 释放一个链路状态前缀。pref 为 NULL 时直接返回。
 */
void ls_prefix_del(struct ls_prefix *pref)
{
	if (!pref)
		return;

	XFREE(MTYPE_LS_DB, pref);
}

/*
 * 判断两个前缀是否相等。先比指针 / flags / adv；再比较前缀本身
 * (prefix_same)，然后仅对 flags 标记为有效的字段逐一比较(IGP 标志、
 * 路由标签、扩展标签、度量、SR 与 SRv6 SID)。返回 1 / 0。
 */
int ls_prefix_same(struct ls_prefix *p1, struct ls_prefix *p2)
{
	/* First, check pointer */
	if ((p1 && !p2) || (!p1 && p2))
		return 0;

	if (p1 == p2)
		return 1;

	/* Then, verify Flags and Origin */
	if (p1->flags != p2->flags)
		return 0;

	if (!ls_node_id_same(p1->adv, p2->adv))
		return 0;

	/* Finally, check each individual parameters that are valid */
	if (prefix_same(&p1->pref, &p2->pref) == 0)
		return 0;
	if (CHECK_FLAG(p1->flags, LS_PREF_IGP_FLAG)
	    && (p1->igp_flag != p2->igp_flag))
		return 0;
	if (CHECK_FLAG(p1->flags, LS_PREF_ROUTE_TAG)
	    && (p1->route_tag != p2->route_tag))
		return 0;
	if (CHECK_FLAG(p1->flags, LS_PREF_EXTENDED_TAG)
	    && (p1->extended_tag != p2->extended_tag))
		return 0;
	if (CHECK_FLAG(p1->flags, LS_PREF_METRIC) && (p1->metric != p2->metric))
		return 0;
	if (CHECK_FLAG(p1->flags, LS_PREF_SR)) {
		if ((p1->sr.algo != p2->sr.algo) || (p1->sr.sid != p2->sr.sid)
		    || (p1->sr.sid_flag != p2->sr.sid_flag))
			return 0;
	}
	if (CHECK_FLAG(p1->flags, LS_PREF_SRV6)) {
		if (memcmp(&p1->srv6.sid, &p2->srv6.sid,
			   sizeof(struct in6_addr)) ||
		    (p1->srv6.flags != p2->srv6.flags) ||
		    (p1->srv6.behavior != p2->srv6.behavior))
			return 0;
	}

	/* OK, p1 & p2 are equal */
	return 1;
}

/**
 *  Link State Vertices management functions
 */
/*
 * 把 IS-IS 的 ISO System-ID(6 字节)转换为 64 位 key，供顶点比较使用。
 * 小端主机上逐字节倒序放入低地址(使数值大小与字节序无关、可稳定
 * 比较)，并把高两位字节置 0；大端主机上直接内存拷贝。
 * 之所以要这样处理，是因为 RB 树用数值比较(numcmp)比较 key。
 */
uint64_t sysid_to_key(const uint8_t sysid[ISO_SYS_ID_LEN])
{
	uint64_t key = 0;

#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t *byte = (uint8_t *)&key;

	for (int i = 0; i < ISO_SYS_ID_LEN; i++)
		byte[i] = sysid[ISO_SYS_ID_LEN - i - 1];

	byte[6] = 0;
	byte[7] = 0;
#else
	memcpy(&key, sysid, ISO_SYS_ID_LEN);
#endif

	return key;
}

/*
 * 依据 ls_node 向 TED 添加顶点。先由 adv.origin 推导 key：OSPF / STATIC
 * / DIRECT 用 ntohl(Router-ID) 取低 32 位，IS-IS 用 sysid_to_key。
 * key 为 0(含 UNKNOWN)时直接返回 NULL。随后分配顶点，建立空的入边 /
 * 出边 / 前缀三张链表(分别设置 edge_cmp 与 subnet_cmp 作比较函数)，
 * 再插入 RB 树。注意：顶点只保存 node 指针，不接管其所有权。
 */
struct ls_vertex *ls_vertex_add(struct ls_ted *ted, struct ls_node *node)
{
	struct ls_vertex *new;
	uint64_t key = 0;

	if ((ted == NULL) || (node == NULL))
		return NULL;

	/* set Key as the IPv4/Ipv6 Router ID or ISO System ID */
	switch (node->adv.origin) {
	case OSPFv2:
	case STATIC:
	case DIRECT:
		key = ((uint64_t)ntohl(node->adv.id.ip.addr.s_addr))
		      & 0xffffffff;
		break;
	case ISIS_L1:
	case ISIS_L2:
		key = sysid_to_key(node->adv.id.iso.sys_id);
		break;
	case UNKNOWN:
		key = 0;
		break;
	}

	/* Check that key is valid */
	if (key == 0)
		return NULL;

	/* Create Vertex and add it to the TED */
	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_vertex));
	if (!new)
		return NULL;

	new->key = key;
	new->node = node;
	new->status = NEW;
	new->type = VERTEX;
	new->incoming_edges = list_new();
	new->incoming_edges->cmp = (int (*)(void *, void *))edge_cmp;
	new->outgoing_edges = list_new();
	new->outgoing_edges->cmp = (int (*)(void *, void *))edge_cmp;
	new->prefixes = list_new();
	new->prefixes->cmp = (int (*)(void *, void *))subnet_cmp;
	vertices_add(&ted->vertices, new);

	return new;
}

/*
 * 从 TED 删除顶点并释放其内部链表：
 *   - 出边：全部 ls_edge_del_all(出边必然以本顶点为源)。
 *   - 入边：仅 disconnect；若断开后该边已无源顶点(悬空)则连同删除。
 *   - 子网：全部 ls_subnet_del_all。
 * 最后从 RB 树摘除并释放顶点结构体，但不释放 node。
 * 注意：本函数实际会连带删除边与子网本身(及其 attributes / prefix)，
 * 与头文件“只清链表不删边”的注释略有出入，阅读时以代码为准。
 */
void ls_vertex_del(struct ls_ted *ted, struct ls_vertex *vertex)
{
	struct listnode *node, *nnode;
	struct ls_edge *edge;
	struct ls_subnet *subnet;

	if (!ted || !vertex)
		return;

	/* Remove outgoing Edges and list */
	for (ALL_LIST_ELEMENTS(vertex->outgoing_edges, node, nnode, edge))
		ls_edge_del_all(ted, edge);
	list_delete(&vertex->outgoing_edges);

	/* Disconnect incoming Edges and remove list */
	for (ALL_LIST_ELEMENTS(vertex->incoming_edges, node, nnode, edge)) {
		ls_disconnect(vertex, edge, false);
		if (edge->source == NULL)
			ls_edge_del_all(ted, edge);
	}
	list_delete(&vertex->incoming_edges);

	/* Remove subnet and list */
	for (ALL_LIST_ELEMENTS(vertex->prefixes, node, nnode, subnet))
		ls_subnet_del_all(ted, subnet);
	list_delete(&vertex->prefixes);

	/* Then remove Vertex from Link State Data Base and free memory */
	vertices_del(&ted->vertices, vertex);
	XFREE(MTYPE_LS_DB, vertex);
}

/*
 * 删除顶点并连同其 ls_node 一起释放：先释放 node，再调用 ls_vertex_del
 * 删除顶点本身。仅在确认 node 不被其它对象引用时使用。
 */
void ls_vertex_del_all(struct ls_ted *ted, struct ls_vertex *vertex)
{
	if (!ted || !vertex)
		return;

	/* First remove associated Link State Node */
	ls_node_del(vertex->node);

	/* Then, Vertex itself */
	ls_vertex_del(ted, vertex);
}

/*
 * 用新 node 更新(或新建)顶点。按 node->adv 查找已有顶点：
 *   - 找到：新旧节点不同则先释放旧 node 再替换；相同则释放新传入的
 *     node(避免重复)，并把顶点状态置为 UPDATE。
 *   - 未找到：调用 ls_vertex_add 新建。
 * 返回更新后的顶点，出错返回 NULL。
 * 注意：本函数接管传入 node 的所有权——要么被持有，要么被释放。
 */
struct ls_vertex *ls_vertex_update(struct ls_ted *ted, struct ls_node *node)
{
	struct ls_vertex *old;

	if (node == NULL)
		return NULL;

	old = ls_find_vertex_by_id(ted, node->adv);
	if (old) {
		if (!ls_node_same(old->node, node)) {
			ls_node_del(old->node);
			old->node = node;
		} else
			ls_node_del(node);

		old->status = UPDATE;
		return old;
	}

	return ls_vertex_add(ted, node);
}

/*
 * 按 key 查找顶点：用栈上一个临时顶点作探针，交给 RB 树的 find。
 * key 为 0 时直接返回 NULL。找到返回顶点指针，否则 NULL。
 */
struct ls_vertex *ls_find_vertex_by_key(struct ls_ted *ted, const uint64_t key)
{
	struct ls_vertex vertex = {};

	if (key == 0)
		return NULL;

	vertex.key = key;
	return vertices_find(&ted->vertices, &vertex);
}

/*
 * 按节点标识查找顶点：先由 origin 推导 key(规则同 ls_vertex_add)，再用
 * 临时探针在 RB 树中查找。origin 为 UNKNOWN 或未命中时返回 NULL。
 */
struct ls_vertex *ls_find_vertex_by_id(struct ls_ted *ted,
				       struct ls_node_id nid)
{
	struct ls_vertex vertex = {};

	vertex.key = 0;
	switch (nid.origin) {
	case OSPFv2:
	case STATIC:
	case DIRECT:
		vertex.key =
			((uint64_t)ntohl(nid.id.ip.addr.s_addr)) & 0xffffffff;
		break;
	case ISIS_L1:
	case ISIS_L2:
		vertex.key = sysid_to_key(nid.id.iso.sys_id);
		break;
	case UNKNOWN:
		return NULL;
	}

	return vertices_find(&ted->vertices, &vertex);
}

/*
 * 判断两个顶点是否相等：空 / 同指针特判；key 不同即不等；node 指针
 * 相同即相等；否则比较各自的 node(ls_node_same)。返回 1 / 0。
 */
int ls_vertex_same(struct ls_vertex *v1, struct ls_vertex *v2)
{
	if ((v1 && !v2) || (!v1 && v2))
		return 0;

	if (!v1 && !v2)
		return 1;

	if (v1->key != v2->key)
		return 0;

	if (v1->node == v2->node)
		return 1;

	return ls_node_same(v1->node, v2->node);
}

/*
 * 清理顶点下所有状态为 ORPHAN 的边(入边与出边)和子网。若 zclient 非
 * NULL，则在删除前把对象状态置为 DELETE，并通过 ls_xxx2msg 加
 * ls_send_msg 广播一条删除通知，让其它守护进程同步删除。
 */
void ls_vertex_clean(struct ls_ted *ted, struct ls_vertex *vertex,
		     struct zclient *zclient)
{
	struct listnode *node, *nnode;
	struct ls_edge *edge;
	struct ls_subnet *subnet;
	struct ls_message msg;

	/* Remove Orphan Edge ... */
	for (ALL_LIST_ELEMENTS(vertex->outgoing_edges, node, nnode, edge)) {
		if (edge->status == ORPHAN) {
			if (zclient) {
				edge->status = DELETE;
				ls_edge2msg(&msg, edge);
				ls_send_msg(zclient, &msg, NULL);
			}
			ls_edge_del_all(ted, edge);
		}
	}
	for (ALL_LIST_ELEMENTS(vertex->incoming_edges, node, nnode, edge)) {
		if (edge->status == ORPHAN) {
			if (zclient) {
				edge->status = DELETE;
				ls_edge2msg(&msg, edge);
				ls_send_msg(zclient, &msg, NULL);
			}
			ls_edge_del_all(ted, edge);
		}
	}

	/* ... and Subnet from the Vertex */
	for (ALL_LIST_ELEMENTS(vertex->prefixes, node, nnode, subnet)) {
		if (subnet->status == ORPHAN) {
			if (zclient) {
				subnet->status = DELETE;
				ls_subnet2msg(&msg, subnet);
				ls_send_msg(zclient, &msg, NULL);
			}
			ls_subnet_del_all(ted, subnet);
		}
	}
}

/**
 * Link State Edges management functions
 */

/*
 * 把一条边挂接到 TED 中相应的顶点上，并在发现反向边时同时建立对端
 * 连接。逻辑：
 *   1) 按 attributes->adv 查找源顶点；找不到时临时新建一个只有标识的
 *      node / vertex，保证图中顶点完整。
 *   2) 把该边作为出边加入源顶点的 outgoing_edges，并记 edge->source。
 *   3) 再按目的信息查找反向边 dst；若找到，则把 dst 作为入边挂到源
 *      顶点、把本边作为入边挂到 dst 的源顶点，并互相设置 destination，
 *      从而把一条双向链路的两条有向边关联起来。
 */
/**
 * This function allows to connect the Edge to the vertices present in the TED.
 * A temporary vertex that corresponds to the source of this Edge i.e. the
 * advertised router, is created if not found in the Data Base. If a Edge that
 * corresponds to the reverse path is found, the Edge is attached to the
 * destination vertex as destination and reverse Edge is attached to the source
 * vertex as source.
 *
 * @param ted	Link State Data Base
 * @param edge	Link State Edge to be attached
 */
static void ls_edge_connect_to(struct ls_ted *ted, struct ls_edge *edge)
{
	struct ls_vertex *vertex = NULL;
	struct ls_node *node;
	struct ls_edge *dst;
	const struct in_addr inaddr_any = {.s_addr = INADDR_ANY};

	/* First, search if there is a Vertex that correspond to the Node ID */
	vertex = ls_find_vertex_by_id(ted, edge->attributes->adv);
	if (vertex == NULL) {
		/* Create a new temporary Node & Vertex if not found */
		node = ls_node_new(edge->attributes->adv, inaddr_any,
				   in6addr_any);
		vertex = ls_vertex_add(ted, node);
	}
	/* and attach the edge as source to the vertex */
	listnode_add_sort_nodup(vertex->outgoing_edges, edge);
	edge->source = vertex;

	/* Then search if there is a reverse Edge */
	dst = ls_find_edge_by_destination(ted, edge->attributes);
	/* attach the destination edge to the vertex */
	if (dst) {
		listnode_add_sort_nodup(vertex->incoming_edges, dst);
		dst->destination = vertex;
		/* and destination vertex to this edge */
		vertex = dst->source;
		listnode_add_sort_nodup(vertex->incoming_edges, edge);
		edge->destination = vertex;
	}
}

/*
 * 由属性推导边的 key。dst 为真取“目的”侧信息，否则取“源”侧：
 *   - 优先用 IPv4 邻居 / 本地地址(family AF_INET)；
 *   - 其次用 IPv6 邻居 / 本地地址(family AF_INET6)；
 *   - 都没有时退回用本侧与对侧标识拼成 64 位 link_id(family AF_LOCAL)。
 * 若属性为空或没有任何可用标识，返回 family 为 AF_UNSPEC 的 key，
 * 调用方据此判定该边无法建立。
 */
static struct ls_edge_key get_edge_key(struct ls_attributes *attr, bool dst)
{
	struct ls_edge_key key = {.family = AF_UNSPEC};
	struct ls_standard *std;

	if (!attr)
		return key;

	std = &attr->standard;

	if (dst) {
		if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR)) {
			/* Key is the IPv4 remote address */
			key.family = AF_INET;
			IPV4_ADDR_COPY(&key.k.addr, &std->remote);
		} else if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR6)) {
			/* or the IPv6 remote address */
			key.family = AF_INET6;
			IPV6_ADDR_COPY(&key.k.addr6, &std->remote6);
		} else if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ID)) {
			/* or Remote identifier if IP addr. are not defined */
			key.family = AF_LOCAL;
			key.k.link_id =
				(((uint64_t)std->remote_id) & 0xffffffff) |
				((uint64_t)std->local_id << 32);
		}
	} else {
		if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR)) {
			/* Key is the IPv4 local address */
			key.family = AF_INET;
			IPV4_ADDR_COPY(&key.k.addr, &std->local);
		} else if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6)) {
			/* or the 64 bits LSB of IPv6 local address */
			key.family = AF_INET6;
			IPV6_ADDR_COPY(&key.k.addr6, &std->local6);
		} else if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ID)) {
			/* or Remote identifier if IP addr. are not defined */
			key.family = AF_LOCAL;
			key.k.link_id =
				(((uint64_t)std->local_id) & 0xffffffff) |
				((uint64_t)std->remote_id << 32);
		}
	}

	return key;
}

/*
 * 依据一组 ls_attributes 向 TED 新增边。先用 get_edge_key(源侧)推导
 * key，若为 AF_UNSPEC 则返回 NULL。随后分配边、登记 attributes 与
 * key、类型置 EDGE，插入 RB 树，最后调用 ls_edge_connect_to 把边挂接
 * 到源 / 目的顶点。返回新边。注意：边自身不接管 attributes 的所有权，
 * 但 ls_edge_del_all 会一并释放它。
 */
struct ls_edge *ls_edge_add(struct ls_ted *ted,
			    struct ls_attributes *attributes)
{
	struct ls_edge *new;
	struct ls_edge_key key;

	if (attributes == NULL)
		return NULL;

	key = get_edge_key(attributes, false);
	if (key.family == AF_UNSPEC)
		return NULL;

	/* Create Edge and add it to the TED */
	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_edge));

	new->attributes = attributes;
	new->key = key;
	new->status = NEW;
	new->type = EDGE;
	edges_add(&ted->edges, new);

	/* Finally, connect Edge to Vertices */
	ls_edge_connect_to(ted, new);

	return new;
}

/*
 * 按 ls_edge_key 查找边。key.family 为 AF_UNSPEC 时返回 NULL。用栈上
 * 临时边作探针交给 RB 树查找，命中返回边指针，否则 NULL。
 */
struct ls_edge *ls_find_edge_by_key(struct ls_ted *ted,
				    const struct ls_edge_key key)
{
	struct ls_edge edge = {};

	if (key.family == AF_UNSPEC)
		return NULL;

	edge.key = key;
	return edges_find(&ted->edges, &edge);
}

/*
 * 按属性的“源”侧信息(get_edge_key(attributes,false))查找边，即本端
 * 地址 / 标识匹配。找到返回对应边，否则 NULL。
 */
struct ls_edge *ls_find_edge_by_source(struct ls_ted *ted,
				       struct ls_attributes *attributes)
{
	struct ls_edge edge = {};

	if (attributes == NULL)
		return NULL;

	edge.key = get_edge_key(attributes, false);
	if (edge.key.family == AF_UNSPEC)
		return NULL;

	return edges_find(&ted->edges, &edge);
}

/*
 * 按属性的“目的”侧信息(get_edge_key(attributes,true))查找边，即远端
 * 地址 / 标识匹配。找到返回对应边，否则 NULL。
 */
struct ls_edge *ls_find_edge_by_destination(struct ls_ted *ted,
					    struct ls_attributes *attributes)
{
	struct ls_edge edge = {};

	if (attributes == NULL)
		return NULL;

	edge.key = get_edge_key(attributes, true);
	if (edge.key.family == AF_UNSPEC)
		return NULL;

	return edges_find(&ted->edges, &edge);
}

/*
 * 用新 attributes 更新(或新建)边。先按源侧查找已有边：
 *   - 找到：新旧属性不同则释放旧 attributes 再替换；相同则释放新传入
 *     的 attributes，并把状态置为 UPDATE。
 *   - 未找到：调用 ls_edge_add 新建。
 * 返回更新后的边。注意：与 ls_vertex_update 类似，本函数接管传入
 * attributes 的所有权——要么被持有，要么被释放。
 */
struct ls_edge *ls_edge_update(struct ls_ted *ted,
			       struct ls_attributes *attributes)
{
	struct ls_edge *old;

	if (attributes == NULL)
		return NULL;

	/* First, search for an existing Edge */
	old = ls_find_edge_by_source(ted, attributes);
	if (old) {
		/* Check if attributes are similar */
		if (!ls_attributes_same(old->attributes, attributes)) {
			ls_attributes_del(old->attributes);
			old->attributes = attributes;
		} else
			ls_attributes_del(attributes);

		old->status = UPDATE;
		return old;
	}

	/* If not found, add new Edge from the attributes */
	return ls_edge_add(ted, attributes);
}

/*
 * 判断两条边是否相等：空 / 同指针特判；用 edge_cmp 比较 key；attributes
 * 指针相同即相等，否则比较 attributes(ls_attributes_same)。返回 1 / 0。
 */
int ls_edge_same(struct ls_edge *e1, struct ls_edge *e2)
{
	if ((e1 && !e2) || (!e1 && e2))
		return 0;

	if (!e1 && !e2)
		return 1;

	if (edge_cmp(e1, e2) != 0)
		return 0;

	if (e1->attributes == e2->attributes)
		return 1;

	return ls_attributes_same(e1->attributes, e2->attributes);
}

/*
 * 从 TED 删除一条边：先 ls_disconnect_edge 断开与两端顶点的连接，再从
 * RB 树摘除并释放边结构体。不释放 attributes。
 */
void ls_edge_del(struct ls_ted *ted, struct ls_edge *edge)
{
	if (!ted || !edge)
		return;

	/* Fist disconnect Edge from Vertices */
	ls_disconnect_edge(edge);
	/* Then remove it from the Data Base */
	edges_del(&ted->edges, edge);
	XFREE(MTYPE_LS_DB, edge);
}

/*
 * 删除边并连同其 ls_attributes 一起释放(先释放 attributes，再删除边
 * 本身)。
 */
void ls_edge_del_all(struct ls_ted *ted, struct ls_edge *edge)
{
	if (!ted || !edge)
		return;

	/* Remove associated Link State Attributes */
	ls_attributes_del(edge->attributes);
	/* Then Edge itself */
	ls_edge_del(ted, edge);
}

/**
 * Link State Subnet Management functions.
 */
/*
 * 依据 ls_prefix 向 TED 新增子网：分配子网、以 ls_pref->pref 作 key、
 * 类型置 SUBNET。按 ls_pref->adv 查找所属顶点，找不到时临时新建一个
 * 只有标识的 node / vertex。把子网挂到该顶点的 prefixes 链表，再插入
 * 子网 RB 树。返回新子网。注意：子网自身不接管 ls_pref 的所有权，但
 * ls_subnet_del_all 会一并释放它。
 */
struct ls_subnet *ls_subnet_add(struct ls_ted *ted,
				struct ls_prefix *ls_pref)
{
	struct ls_subnet *new;
	struct ls_vertex *vertex;
	struct ls_node *node;
	const struct in_addr inaddr_any = {.s_addr = INADDR_ANY};

	if (ls_pref == NULL)
		return NULL;

	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_subnet));
	new->ls_pref = ls_pref;
	new->key = ls_pref->pref;
	new->status = NEW;
	new->type = SUBNET;

	/* Find Vertex */
	vertex = ls_find_vertex_by_id(ted, ls_pref->adv);
	if (vertex == NULL) {
		/* Create a new temporary Node & Vertex if not found */
		node = ls_node_new(ls_pref->adv, inaddr_any, in6addr_any);
		vertex = ls_vertex_add(ted, node);
	}
	/* And attach the subnet to the corresponding Vertex */
	new->vertex = vertex;
	listnode_add_sort_nodup(vertex->prefixes, new);

	subnets_add(&ted->subnets, new);

	return new;
}

/*
 * 用新 pref 更新(或新建)子网。先按前缀查找已有子网：
 *   - 找到：新旧前缀不同则释放旧 ls_pref 再替换；相同则释放新传入的
 *     pref，并把状态置为 UPDATE。
 *   - 未找到：调用 ls_subnet_add 新建。
 * 返回更新后的子网。本函数接管传入 pref 的所有权。
 */
struct ls_subnet *ls_subnet_update(struct ls_ted *ted, struct ls_prefix *pref)
{
	struct ls_subnet *old;

	if (pref == NULL)
		return NULL;

	old = ls_find_subnet(ted, &pref->pref);
	if (old) {
		if (!ls_prefix_same(old->ls_pref, pref)) {
			ls_prefix_del(old->ls_pref);
			old->ls_pref = pref;
		} else
			ls_prefix_del(pref);

		old->status = UPDATE;
		return old;
	}

	return ls_subnet_add(ted, pref);
}

/*
 * 判断两个子网是否相等：空 / 同指针特判；比较前缀 key(prefix_same)；
 * ls_pref 指针相同即相等，否则比较 ls_prefix。返回 1 / 0。
 */
int ls_subnet_same(struct ls_subnet *s1, struct ls_subnet *s2)
{
	if ((s1 && !s2) || (!s1 && s2))
		return 0;

	if (!s1 && !s2)
		return 1;

	if (!prefix_same(&s1->key, &s2->key))
		return 0;

	if (s1->ls_pref == s2->ls_pref)
		return 1;

	return ls_prefix_same(s1->ls_pref, s2->ls_pref);
}

/*
 * 从 TED 删除子网：先从所属顶点的 prefixes 链表移除，再从子网 RB 树
 * 摘除并释放子网结构体。不释放 ls_pref。
 */
void ls_subnet_del(struct ls_ted *ted, struct ls_subnet *subnet)
{
	if (!ted || !subnet)
		return;

	/* First, disconnect Subnet from associated Vertex */
	listnode_delete(subnet->vertex->prefixes, subnet);
	/* Then delete Subnet */
	subnets_del(&ted->subnets, subnet);
	XFREE(MTYPE_LS_DB, subnet);
}

/*
 * 删除子网并连同其 ls_prefix 一起释放(先释放 ls_pref，再删除子网
 * 本身)。
 */
void ls_subnet_del_all(struct ls_ted *ted, struct ls_subnet *subnet)
{
	if (!ted || !subnet)
		return;

	/* First, remove associated Link State Subnet */
	ls_prefix_del(subnet->ls_pref);
	/* Then, delete Subnet itself */
	ls_subnet_del(ted, subnet);
}

/*
 * 按 struct prefix 查找子网：把前缀复制到栈上临时子网的 key，交给 RB
 * 树查找。prefix 为 NULL 时返回 NULL。
 */
struct ls_subnet *ls_find_subnet(struct ls_ted *ted,
				 const struct prefix *prefix)
{
	struct ls_subnet subnet = {};

	if (!prefix)
		return NULL;

	prefix_copy(&subnet.key, prefix);
	return subnets_find(&ted->subnets, &subnet);
}

/**
 * Link State TED management functions
 */
/*
 * 新建一个链路状态数据库(TED)：分配结构体、记录 key / as_number /
 * name(名称用 strlcpy 截断到 MAX_NAME_LENGTH)，并初始化三棵 RB 树。
 * 返回新 TED。注意：name 为 NULL 时 strlcpy 会写入空串。
 */
struct ls_ted *ls_ted_new(const uint32_t key, const char *name,
			  uint32_t as_number)
{
	struct ls_ted *new;

	new = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_ted));

	/* Set basic information for this ted */
	new->key = key;
	new->as_number = as_number;
	strlcpy(new->name, name, MAX_NAME_LENGTH);

	/* Initialize the various RB tree */
	vertices_init(&new->vertices);
	edges_init(&new->edges);
	subnets_init(&new->subnets);

	return new;
}

/*
 * 释放 TED 结构体本身。仅当库为空(顶点 / 边 / 子网计数均为 0)时才真
 * 正释放，并结束三棵 RB 树；否则直接返回，避免留下悬空引用。
 */
void ls_ted_del(struct ls_ted *ted)
{
	if (ted == NULL)
		return;

	/* Check that TED is empty */
	if (vertices_count(&ted->vertices) || edges_count(&ted->edges)
	    || subnets_count(&ted->subnets))
		return;

	/* Release RB Tree */
	vertices_fini(&ted->vertices);
	edges_fini(&ted->edges);
	subnets_fini(&ted->subnets);

	XFREE(MTYPE_LS_DB, ted);
}

/*
 * 删除 TED 中所有顶点 / 边 / 子网(均用 _del_all 版本，连同其 node /
 * attributes / prefix 一起释放)，再删除 TED 自身。形参是二级指针，
 * 函数会把调用者的指针置为 NULL。
 * 注意删除顺序：先顶点、再边、再子网。顶点删除时已连带处理其关联的
 * 边与子网，故后续遍历通常为空操作。
 */
void ls_ted_del_all(struct ls_ted **ted)
{
	struct ls_vertex *vertex;
	struct ls_edge *edge;
	struct ls_subnet *subnet;

	if (*ted == NULL)
		return;

	/* First remove Vertices, Edges and Subnets and associated Link State */
	frr_each_safe (vertices, &(*ted)->vertices, vertex)
		ls_vertex_del_all(*ted, vertex);
	frr_each_safe (edges, &(*ted)->edges, edge)
		ls_edge_del_all(*ted, edge);
	frr_each_safe (subnets, &(*ted)->subnets, subnet)
		ls_subnet_del_all(*ted, subnet);

	/* then remove TED itself */
	ls_ted_del(*ted);
	*ted = NULL;
}

/*
 * 删除 TED 中所有状态为 ORPHAN 的顶点 / 边 / 子网。与 ls_ted_del_all
 * 不同，只清理被标记为孤立的元素。分别在顶点 / 边 / 子网三棵树中扫描。
 */
void ls_ted_clean(struct ls_ted *ted)
{
	struct ls_vertex *vertex;
	struct ls_edge *edge;
	struct ls_subnet *subnet;

	if (ted == NULL)
		return;

	/* First, start with Vertices */
	frr_each_safe (vertices, &ted->vertices, vertex)
		if (vertex->status == ORPHAN)
			ls_vertex_del_all(ted, vertex);

	/* Then Edges */
	frr_each_safe (edges, &ted->edges, edge)
		if (edge->status == ORPHAN)
			ls_edge_del_all(ted, edge);

	/* and Subnets */
	frr_each_safe (subnets, &ted->subnets, subnet)
		if (subnet->status == ORPHAN)
			ls_subnet_del_all(ted, subnet);

}

/*
 * 把一条边连接到顶点：source 为真时作为出边加入 outgoing_edges 并设
 * edge->source；为假时作为入边加入 incoming_edges 并设 edge->destination。
 * 任一指为 NULL 时直接返回。用 listnode_add_sort_nodup 保证同一列表内
 * 不出现重复项。
 */
void ls_connect(struct ls_vertex *vertex, struct ls_edge *edge, bool source)
{
	if (vertex == NULL || edge == NULL)
		return;

	if (source) {
		listnode_add_sort_nodup(vertex->outgoing_edges, edge);
		edge->source = vertex;
	} else {
		listnode_add_sort_nodup(vertex->incoming_edges, edge);
		edge->destination = vertex;
	}
}

/*
 * 把一条边从顶点断开(ls_connect 的逆操作)：从对应链表删除，并把
 * edge->source 或 edge->destination 置 NULL。vertex / edge 为 NULL 时
 * 直接返回。
 */
void ls_disconnect(struct ls_vertex *vertex, struct ls_edge *edge, bool source)
{

	if (vertex == NULL || edge == NULL)
		return;

	if (source) {
		listnode_delete(vertex->outgoing_edges, edge);
		edge->source = NULL;
	} else {
		listnode_delete(vertex->incoming_edges, edge);
		edge->destination = NULL;
	}
}

/*
 * 用一条边连接源顶点与目的顶点：先设置 edge 的 source / destination，
 * 再分别把边加入源顶点的出边表与目的顶点的入边表。src / dst 为 NULL
 * 时跳过对应方向。
 */
void ls_connect_vertices(struct ls_vertex *src, struct ls_vertex *dst,
			 struct ls_edge *edge)
{
	if (edge == NULL)
		return;

	edge->source = src;
	edge->destination = dst;

	if (src != NULL)
		listnode_add_sort_nodup(src->outgoing_edges, edge);

	if (dst != NULL)
		listnode_add_sort_nodup(dst->incoming_edges, edge);
}

/*
 * 把一条边同时从源顶点与目的顶点断开，并把该边状态标记为 ORPHAN，
 * 等待后续 ls_vertex_clean / ls_ted_clean 清理。edge 为 NULL 时直接
 * 返回。
 */
void ls_disconnect_edge(struct ls_edge *edge)
{
	if (edge == NULL)
		return;

	ls_disconnect(edge->source, edge, true);
	ls_disconnect(edge->destination, edge, false);

	/* Mark this Edge as ORPHAN for future cleanup */
	edge->status = ORPHAN;
}

/**
 * Link State Message management functions
 */

/*
 * 注册到 zebra 的 OPAQUE API。server 为真时注册为 LINK_STATE_SYNC(持有
 * LSDB 的服务端)，否则注册为 LINK_STATE_UPDATE(消费更新的客户端)。
 * 直接透传 zebra 的返回值(0 成功 / -1 失败)。
 */
int ls_register(struct zclient *zclient, bool server)
{
	int rc;

	if (server)
		rc = zclient_register_opaque(zclient, LINK_STATE_SYNC);
	else
		rc = zclient_register_opaque(zclient, LINK_STATE_UPDATE);

	return rc;
}

/*
 * 从 zebra 的 OPAQUE API 注销(与 ls_register 对应)。透传返回值。
 */
int ls_unregister(struct zclient *zclient, bool server)
{
	int rc;

	if (server)
		rc = zclient_unregister_opaque(zclient, LINK_STATE_SYNC);
	else
		rc = zclient_unregister_opaque(zclient, LINK_STATE_UPDATE);

	return rc;
}

/*
 * 发送一条无数据的 LINK_STATE_SYNC 消息，请求服务端回送完整 LSDB。
 * 先检查发送缓冲区是否够放一个 opaque 头；不够则返回 -1。
 */
int ls_request_sync(struct zclient *zclient)
{
	/* Check buffer size */
	if (STREAM_SIZE(zclient->obuf)
	    < (ZEBRA_HEADER_SIZE + 3 * sizeof(uint32_t)))
		return -1;

	/* No data with this message */
	return zclient_send_opaque(zclient, LINK_STATE_SYNC, NULL, 0);
}

/*
 * 从数据流解析出一个 ls_node。先读 adv 与 flags，再按 flags 位逐段读取
 * 对应字段(名称、Router-ID、节点标志、类型、AS 号、SRGB、SR 算法、
 * SRLB、MSD)。STREAM_GET 系列宏失败时会 goto stream_failure，那里打印
 * 错误、释放已分配内存并返回 NULL。
 */
static struct ls_node *ls_parse_node(struct stream *s)
{
	struct ls_node *node;
	size_t len;

	node = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_node));

	STREAM_GET(&node->adv, s, sizeof(struct ls_node_id));
	STREAM_GETW(s, node->flags);
	if (CHECK_FLAG(node->flags, LS_NODE_NAME)) {
		STREAM_GETC(s, len);
		STREAM_GET(node->name, s, len);
	}
	if (CHECK_FLAG(node->flags, LS_NODE_ROUTER_ID))
		node->router_id.s_addr = stream_get_ipv4(s);
	if (CHECK_FLAG(node->flags, LS_NODE_ROUTER_ID6))
		STREAM_GET(&node->router_id6, s, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(node->flags, LS_NODE_FLAG))
		STREAM_GETC(s, node->node_flag);
	if (CHECK_FLAG(node->flags, LS_NODE_TYPE))
		STREAM_GETC(s, node->type);
	if (CHECK_FLAG(node->flags, LS_NODE_AS_NUMBER))
		STREAM_GETL(s, node->as_number);
	if (CHECK_FLAG(node->flags, LS_NODE_SR)) {
		STREAM_GETL(s, node->srgb.lower_bound);
		STREAM_GETL(s, node->srgb.range_size);
		STREAM_GETC(s, node->srgb.flag);
		STREAM_GET(node->algo, s, 2);
	}
	if (CHECK_FLAG(node->flags, LS_NODE_SRLB)) {
		STREAM_GETL(s, node->srlb.lower_bound);
		STREAM_GETL(s, node->srlb.range_size);
	}
	if (CHECK_FLAG(node->flags, LS_NODE_MSD))
		STREAM_GETC(s, node->msd);

	return node;

stream_failure:
	zlog_err("LS(%s): Could not parse Link State Node. Abort!", __func__);
	XFREE(MTYPE_LS_DB, node);
	return NULL;
}

/*
 * 从数据流解析出一组 ls_attributes。先读 adv 与 flags，再按 flags 位
 * 逐段读取各字段(名称、度量、管理组、扩展管理组、地址、带宽、时延
 * 与抖动与丢包、邻接 SID、SRv6 邻接 SID、SRLG)。扩展管理组按位图
 * 批量设置；SRLG 先读长度再逐条读取。
 * 踩坑：STREAM_GET 宏失败会 goto stream_failure；该处会先释放 SRLG
 * 列表再释放结构体，故需保证 srlgs 已被初始化为 NULL，避免误释放。
 */
static struct ls_attributes *ls_parse_attributes(struct stream *s)
{
	struct ls_attributes *attr;
	uint8_t nb_ext_adm_grp;
	uint32_t bitmap_data;
	size_t len;

	attr = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_attributes));
	admin_group_init(&attr->ext_admin_group);
	attr->srlgs = NULL;

	STREAM_GET(&attr->adv, s, sizeof(struct ls_node_id));
	STREAM_GETL(s, attr->flags);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NAME)) {
		STREAM_GETC(s, len);
		STREAM_GET(attr->name, s, len);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_METRIC))
		STREAM_GETL(s, attr->metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_TE_METRIC))
		STREAM_GETL(s, attr->standard.te_metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADM_GRP))
		STREAM_GETL(s, attr->standard.admin_group);
	if (CHECK_FLAG(attr->flags, LS_ATTR_EXT_ADM_GRP)) {
		/* Extended Administrative Group */
		STREAM_GETC(s, nb_ext_adm_grp);
		for (size_t i = 0; i < nb_ext_adm_grp; i++) {
			STREAM_GETL(s, bitmap_data);
			admin_group_bulk_set(&attr->ext_admin_group,
					     bitmap_data, i);
		}
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR))
		attr->standard.local.s_addr = stream_get_ipv4(s);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR))
		attr->standard.remote.s_addr = stream_get_ipv4(s);
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6))
		STREAM_GET(&attr->standard.local6, s, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR6))
		STREAM_GET(&attr->standard.remote6, s, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ID))
		STREAM_GETL(s, attr->standard.local_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ID))
		STREAM_GETL(s, attr->standard.remote_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_BW))
		STREAM_GETF(s, attr->standard.max_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_RSV_BW))
		STREAM_GETF(s, attr->standard.max_rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_UNRSV_BW))
		for (len = 0; len < MAX_CLASS_TYPE; len++)
			STREAM_GETF(s, attr->standard.unrsv_bw[len]);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_AS))
		STREAM_GETL(s, attr->standard.remote_as);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR))
		attr->standard.remote_addr.s_addr = stream_get_ipv4(s);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR6))
		STREAM_GET(&attr->standard.remote_addr6, s, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(attr->flags, LS_ATTR_DELAY))
		STREAM_GETL(s, attr->extended.delay);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MIN_MAX_DELAY)) {
		STREAM_GETL(s, attr->extended.min_delay);
		STREAM_GETL(s, attr->extended.max_delay);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_JITTER))
		STREAM_GETL(s, attr->extended.jitter);
	if (CHECK_FLAG(attr->flags, LS_ATTR_PACKET_LOSS))
		STREAM_GETL(s, attr->extended.pkt_loss);
	if (CHECK_FLAG(attr->flags, LS_ATTR_AVA_BW))
		STREAM_GETF(s, attr->extended.ava_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_RSV_BW))
		STREAM_GETF(s, attr->extended.rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_USE_BW))
		STREAM_GETF(s, attr->extended.used_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID)) {
		STREAM_GETL(s, attr->adj_sid[ADJ_PRI_IPV4].sid);
		STREAM_GETC(s, attr->adj_sid[ADJ_PRI_IPV4].flags);
		STREAM_GETC(s, attr->adj_sid[ADJ_PRI_IPV4].weight);
		attr->adj_sid[ADJ_PRI_IPV4].neighbor.addr.s_addr =
			stream_get_ipv4(s);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID)) {
		STREAM_GETL(s, attr->adj_sid[ADJ_BCK_IPV4].sid);
		STREAM_GETC(s, attr->adj_sid[ADJ_BCK_IPV4].flags);
		STREAM_GETC(s, attr->adj_sid[ADJ_BCK_IPV4].weight);
		attr->adj_sid[ADJ_BCK_IPV4].neighbor.addr.s_addr =
			stream_get_ipv4(s);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID6)) {
		STREAM_GETL(s, attr->adj_sid[ADJ_PRI_IPV6].sid);
		STREAM_GETC(s, attr->adj_sid[ADJ_PRI_IPV6].flags);
		STREAM_GETC(s, attr->adj_sid[ADJ_PRI_IPV6].weight);
		STREAM_GET(attr->adj_sid[ADJ_PRI_IPV6].neighbor.sysid, s,
			   ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID6)) {
		STREAM_GETL(s, attr->adj_sid[ADJ_BCK_IPV6].sid);
		STREAM_GETC(s, attr->adj_sid[ADJ_BCK_IPV6].flags);
		STREAM_GETC(s, attr->adj_sid[ADJ_BCK_IPV6].weight);
		STREAM_GET(attr->adj_sid[ADJ_BCK_IPV6].neighbor.sysid, s,
			   ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SRV6SID)) {
		STREAM_GET(&attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].sid, s,
			   sizeof(struct in6_addr));
		STREAM_GETC(s, attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].flags);
		STREAM_GETC(s, attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].weight);
		STREAM_GETW(s, attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6]
				       .endpoint_behavior);
		STREAM_GET(attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].neighbor.sysid,
			   s, ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SRV6SID)) {
		STREAM_GET(&attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].sid, s,
			   sizeof(struct in6_addr));
		STREAM_GETC(s, attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].flags);
		STREAM_GETC(s, attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].weight);
		STREAM_GETW(s, attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6]
				       .endpoint_behavior);
		STREAM_GET(attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].neighbor.sysid,
			   s, ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_SRLG)) {
		STREAM_GETC(s, len);
		attr->srlgs = XCALLOC(MTYPE_LS_DB, len*sizeof(uint32_t));
		attr->srlg_len = len;
		for (len = 0; len < attr->srlg_len; len++)
			STREAM_GETL(s, attr->srlgs[len]);
	}

	return attr;

stream_failure:
	zlog_err("LS(%s): Could not parse Link State Attributes. Abort!",
		 __func__);
	/* Clean memory allocation */
	if (attr->srlgs != NULL)
		XFREE(MTYPE_LS_DB, attr->srlgs);
	XFREE(MTYPE_LS_DB, attr);
	return NULL;

}

/*
 * 从数据流解析出一个 ls_prefix。读取 adv / flags / 前缀地址族与掩码
 * 长度，用 prefix_blen 计算前缀字节数后读取前缀，再按 flags 读取 IGP
 * 标志、路由标签、扩展标签、度量、SR 与 SRv6 SID。解析失败释放内存并
 * 返回 NULL。
 */
static struct ls_prefix *ls_parse_prefix(struct stream *s)
{
	struct ls_prefix *ls_pref;
	size_t len;

	ls_pref = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_prefix));

	STREAM_GET(&ls_pref->adv, s, sizeof(struct ls_node_id));
	STREAM_GETW(s, ls_pref->flags);
	STREAM_GETC(s, ls_pref->pref.family);
	STREAM_GETW(s, ls_pref->pref.prefixlen);
	len = prefix_blen(&ls_pref->pref);
	STREAM_GET(&ls_pref->pref.u.prefix, s, len);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_IGP_FLAG))
		STREAM_GETC(s, ls_pref->igp_flag);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_ROUTE_TAG))
		STREAM_GETL(s, ls_pref->route_tag);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_EXTENDED_TAG))
		STREAM_GETQ(s, ls_pref->extended_tag);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_METRIC))
		STREAM_GETL(s, ls_pref->metric);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_SR)) {
		STREAM_GETL(s, ls_pref->sr.sid);
		STREAM_GETC(s, ls_pref->sr.sid_flag);
		STREAM_GETC(s, ls_pref->sr.algo);
	}
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_SRV6)) {
		STREAM_GET(&ls_pref->srv6.sid, s, sizeof(struct in6_addr));
		STREAM_GETW(s, ls_pref->srv6.behavior);
		STREAM_GETC(s, ls_pref->srv6.flags);
	}

	return ls_pref;

stream_failure:
	zlog_err("LS(%s): Could not parse Link State Prefix. Abort!", __func__);
	XFREE(MTYPE_LS_DB, ls_pref);
	return NULL;
}

/*
 * 从数据流解析一条完整的链路状态消息：先读 event 与 type，再按 type
 * 分派去解析 node / attributes / prefix(attributes 类型会先读远端节点
 * 标识)。类型非法或子对象解析失败时 goto stream_failure，打印错误并
 * 释放 msg 后返回 NULL。
 * 注意：错误路径只释放 message 结构体本身，未释放本次已解析出的子对象。
 */
struct ls_message *ls_parse_msg(struct stream *s)
{
	struct ls_message *msg;

	msg = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_message));

	/* Read LS Message header */
	STREAM_GETC(s, msg->event);
	STREAM_GETC(s, msg->type);

	/* Read Message Payload */
	switch (msg->type) {
	case LS_MSG_TYPE_NODE:
		msg->data.node = ls_parse_node(s);
		break;
	case LS_MSG_TYPE_ATTRIBUTES:
		STREAM_GET(&msg->remote_id, s, sizeof(struct ls_node_id));
		msg->data.attr = ls_parse_attributes(s);
		break;
	case LS_MSG_TYPE_PREFIX:
		msg->data.prefix = ls_parse_prefix(s);
		break;
	default:
		zlog_err("Unsupported Payload");
		goto stream_failure;
	}

	if (msg->data.node == NULL || msg->data.attr == NULL
	    || msg->data.prefix == NULL)
		goto stream_failure;

	return msg;

stream_failure:
	zlog_err("LS(%s): Could not parse LS message. Abort!", __func__);
	XFREE(MTYPE_LS_DB, msg);
	return NULL;
}

/*
 * 把 ls_node 序列化进流(ls_parse_node 的逆操作)。先写 adv，再写 flags，
 * 然后按 flags 位逐段写入对应字段。名称写入时先写长度(含结尾 0)，与
 * 解析端格式保持一致。始终返回 0。
 * 注意：字段顺序必须与 ls_parse_node 严格对应，任何一侧增删字段都要
 * 同步另一侧，否则线上格式会错位。
 */
static int ls_format_node(struct stream *s, struct ls_node *node)
{
	size_t len;

	/* Push Advertise node information first */
	stream_put(s, &node->adv, sizeof(struct ls_node_id));

	/* Push Flags & Origin then Node information if there are present */
	stream_putw(s, node->flags);
	if (CHECK_FLAG(node->flags, LS_NODE_NAME)) {
		len = strlen(node->name);
		stream_putc(s, len + 1);
		stream_put(s, node->name, len);
		stream_putc(s, '\0');
	}
	if (CHECK_FLAG(node->flags, LS_NODE_ROUTER_ID))
		stream_put_ipv4(s, node->router_id.s_addr);
	if (CHECK_FLAG(node->flags, LS_NODE_ROUTER_ID6))
		stream_put(s, &node->router_id6, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(node->flags, LS_NODE_FLAG))
		stream_putc(s, node->node_flag);
	if (CHECK_FLAG(node->flags, LS_NODE_TYPE))
		stream_putc(s, node->type);
	if (CHECK_FLAG(node->flags, LS_NODE_AS_NUMBER))
		stream_putl(s, node->as_number);
	if (CHECK_FLAG(node->flags, LS_NODE_SR)) {
		stream_putl(s, node->srgb.lower_bound);
		stream_putl(s, node->srgb.range_size);
		stream_putc(s, node->srgb.flag);
		stream_put(s, node->algo, 2);
	}
	if (CHECK_FLAG(node->flags, LS_NODE_SRLB)) {
		stream_putl(s, node->srlb.lower_bound);
		stream_putl(s, node->srlb.range_size);
	}
	if (CHECK_FLAG(node->flags, LS_NODE_MSD))
		stream_putc(s, node->msd);

	return 0;
}

/*
 * 把 ls_attributes 序列化进流(ls_parse_attributes 的逆操作)。先写 adv
 * 与 flags，再按 flags 位逐段写入各字段。扩展管理组先写字数
 * (admin_group_nb_words)再逐字写偏移；SRLG 先写长度再逐条写。
 * 必须与解析端字段顺序保持严格一致。
 */
static int ls_format_attributes(struct stream *s, struct ls_attributes *attr)
{
	size_t len, nb_ext_adm_grp;

	/* Push Advertise node information first */
	stream_put(s, &attr->adv, sizeof(struct ls_node_id));

	/* Push Flags & Origin then LS attributes if there are present */
	stream_putl(s, attr->flags);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NAME)) {
		len = strlen(attr->name);
		stream_putc(s, len + 1);
		stream_put(s, attr->name, len);
		stream_putc(s, '\0');
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_METRIC))
		stream_putl(s, attr->metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_TE_METRIC))
		stream_putl(s, attr->standard.te_metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADM_GRP))
		stream_putl(s, attr->standard.admin_group);
	if (CHECK_FLAG(attr->flags, LS_ATTR_EXT_ADM_GRP)) {
		/* Extended Administrative Group */
		nb_ext_adm_grp = admin_group_nb_words(&attr->ext_admin_group);
		stream_putc(s, nb_ext_adm_grp);
		for (size_t i = 0; i < nb_ext_adm_grp; i++)
			stream_putl(s, admin_group_get_offset(
					       &attr->ext_admin_group, i));
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR))
		stream_put_ipv4(s, attr->standard.local.s_addr);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR))
		stream_put_ipv4(s, attr->standard.remote.s_addr);
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6))
		stream_put(s, &attr->standard.local6, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR6))
		stream_put(s, &attr->standard.remote6, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ID))
		stream_putl(s, attr->standard.local_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ID))
		stream_putl(s, attr->standard.remote_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_BW))
		stream_putf(s, attr->standard.max_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_RSV_BW))
		stream_putf(s, attr->standard.max_rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_UNRSV_BW))
		for (len = 0; len < MAX_CLASS_TYPE; len++)
			stream_putf(s, attr->standard.unrsv_bw[len]);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_AS))
		stream_putl(s, attr->standard.remote_as);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR))
		stream_put_ipv4(s, attr->standard.remote_addr.s_addr);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR6))
		stream_put(s, &attr->standard.remote_addr6, IPV6_MAX_BYTELEN);
	if (CHECK_FLAG(attr->flags, LS_ATTR_DELAY))
		stream_putl(s, attr->extended.delay);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MIN_MAX_DELAY)) {
		stream_putl(s, attr->extended.min_delay);
		stream_putl(s, attr->extended.max_delay);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_JITTER))
		stream_putl(s, attr->extended.jitter);
	if (CHECK_FLAG(attr->flags, LS_ATTR_PACKET_LOSS))
		stream_putl(s, attr->extended.pkt_loss);
	if (CHECK_FLAG(attr->flags, LS_ATTR_AVA_BW))
		stream_putf(s, attr->extended.ava_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_RSV_BW))
		stream_putf(s, attr->extended.rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_USE_BW))
		stream_putf(s, attr->extended.used_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID)) {
		stream_putl(s, attr->adj_sid[ADJ_PRI_IPV4].sid);
		stream_putc(s, attr->adj_sid[ADJ_PRI_IPV4].flags);
		stream_putc(s, attr->adj_sid[ADJ_PRI_IPV4].weight);
		stream_put_ipv4(
			s, attr->adj_sid[ADJ_PRI_IPV4].neighbor.addr.s_addr);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID)) {
		stream_putl(s, attr->adj_sid[ADJ_BCK_IPV4].sid);
		stream_putc(s, attr->adj_sid[ADJ_BCK_IPV4].flags);
		stream_putc(s, attr->adj_sid[ADJ_BCK_IPV4].weight);
		stream_put_ipv4(
			s, attr->adj_sid[ADJ_BCK_IPV4].neighbor.addr.s_addr);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID6)) {
		stream_putl(s, attr->adj_sid[ADJ_PRI_IPV6].sid);
		stream_putc(s, attr->adj_sid[ADJ_PRI_IPV6].flags);
		stream_putc(s, attr->adj_sid[ADJ_PRI_IPV6].weight);
		stream_put(s, attr->adj_sid[ADJ_PRI_IPV6].neighbor.sysid,
			   ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID6)) {
		stream_putl(s, attr->adj_sid[ADJ_BCK_IPV6].sid);
		stream_putc(s, attr->adj_sid[ADJ_BCK_IPV6].flags);
		stream_putc(s, attr->adj_sid[ADJ_BCK_IPV6].weight);
		stream_put(s, attr->adj_sid[ADJ_BCK_IPV6].neighbor.sysid,
			   ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SRV6SID)) {
		stream_put(s, &attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].sid,
			   sizeof(struct in6_addr));
		stream_putc(s, attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].flags);
		stream_putc(s, attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].weight);
		stream_putw(s, attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6]
				       .endpoint_behavior);
		stream_put(s,
			   attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].neighbor.sysid,
			   ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SRV6SID)) {
		stream_put(s, &attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].sid,
			   sizeof(struct in6_addr));
		stream_putc(s, attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].flags);
		stream_putc(s, attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].weight);
		stream_putw(s, attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6]
				       .endpoint_behavior);
		stream_put(s,
			   attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].neighbor.sysid,
			   ISO_SYS_ID_LEN);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_SRLG)) {
		stream_putc(s, attr->srlg_len);
		for (len = 0; len < attr->srlg_len; len++)
			stream_putl(s, attr->srlgs[len]);
	}

	return 0;
}

/*
 * 把 ls_prefix 序列化进流(ls_parse_prefix 的逆操作)：写 adv、flags、
 * 前缀族与掩码长度、前缀字节，再按 flags 写 IGP 标志、路由标签、扩展
 * 标签、度量、SR 与 SRv6 SID。需与解析端顺序严格一致。
 */
static int ls_format_prefix(struct stream *s, struct ls_prefix *ls_pref)
{
	size_t len;

	/* Push Advertise node information first */
	stream_put(s, &ls_pref->adv, sizeof(struct ls_node_id));

	/* Push Flags, Origin & Prefix then information if there are present */
	stream_putw(s, ls_pref->flags);
	stream_putc(s, ls_pref->pref.family);
	stream_putw(s, ls_pref->pref.prefixlen);
	len = prefix_blen(&ls_pref->pref);
	stream_put(s, &ls_pref->pref.u.prefix, len);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_IGP_FLAG))
		stream_putc(s, ls_pref->igp_flag);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_ROUTE_TAG))
		stream_putl(s, ls_pref->route_tag);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_EXTENDED_TAG))
		stream_putq(s, ls_pref->extended_tag);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_METRIC))
		stream_putl(s, ls_pref->metric);
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_SR)) {
		stream_putl(s, ls_pref->sr.sid);
		stream_putc(s, ls_pref->sr.sid_flag);
		stream_putc(s, ls_pref->sr.algo);
	}
	if (CHECK_FLAG(ls_pref->flags, LS_PREF_SRV6)) {
		stream_put(s, &ls_pref->srv6.sid, sizeof(struct in6_addr));
		stream_putw(s, ls_pref->srv6.behavior);
		stream_putc(s, ls_pref->srv6.flags);
	}

	return 0;
}

/*
 * 按消息类型把消息体序列化进流：先写 event 与 type，再按 type 分派。
 * attributes 类型会先写远端节点标识。类型非法时打印警告并返回 -1；
 * 成功时返回子格式函数的返回值(通常为 0)。
 */
static int ls_format_msg(struct stream *s, struct ls_message *msg)
{

	/* Prepare Link State header */
	stream_putc(s, msg->event);
	stream_putc(s, msg->type);

	/* Add Message Payload */
	switch (msg->type) {
	case LS_MSG_TYPE_NODE:
		return ls_format_node(s, msg->data.node);
	case LS_MSG_TYPE_ATTRIBUTES:
		/* Add remote node first */
		stream_put(s, &msg->remote_id, sizeof(struct ls_node_id));
		return ls_format_attributes(s, msg->data.attr);
	case LS_MSG_TYPE_PREFIX:
		return ls_format_prefix(s, msg->data.prefix);
	default:
		zlog_warn("Unsupported Payload");
		break;
	}

	return -1;
}

/*
 * 计算一条链路状态消息体的最大字节数：取 ls_node / ls_attributes /
 * ls_prefix 三者 sizeof 的最大值，供发送前做缓冲区容量检查。
 * 注意：若 ls_message 容器新增成员结构体，需同步更新本函数(头文件中
 * 也有对应提示)。
 */
/* Help to compute zapi message size based on the ls_xxx structs' data */
static size_t get_max_ls_msg_size(void)
{
	size_t max = sizeof(struct ls_node);

	if (max < sizeof(struct ls_attributes))
		max = sizeof(struct ls_attributes);

	if (max < sizeof(struct ls_prefix))
		max = sizeof(struct ls_prefix);

	return max;
}

/*
 * 把一条链路状态消息编码成 ZAPI Opaque(Link State 类型)消息并发出。
 * 校验：event 不能是 UNDEF；发送缓冲区要能容纳头加最大消息体。dst 为
 * NULL 时用 zapi_opaque_init(广播给所有已注册进程)，否则用
 * zapi_opaque_unicast_init 单播给指定进程。编码成功后回填长度并发送。
 * 成功返回 zclient_send_message 的结果，失败返回 -1。
 * 注意：msg 内容被内联编码，其生命周期仍由调用方管理。
 */
int ls_send_msg(struct zclient *zclient, struct ls_message *msg,
		struct zapi_opaque_reg_info *dst)
{
	struct stream *s;
	uint16_t flags = 0;

	/* Check if we have a valid message */
	if (msg->event == LS_MSG_EVENT_UNDEF)
		return -1;

	/* Check buffer size */
	if (STREAM_SIZE(zclient->obuf) <
	    (ZEBRA_HEADER_SIZE + sizeof(uint32_t) + get_max_ls_msg_size()))
		return -1;

	/* Init the message, then encode the data inline. */
	if (dst == NULL)
		zapi_opaque_init(zclient, LINK_STATE_UPDATE, flags);
	else
		zapi_opaque_unicast_init(zclient, LINK_STATE_UPDATE, flags,
					 dst->proto, dst->instance,
					 dst->session_id);

	s = zclient->obuf;

	/* Format Link State message */
	if (ls_format_msg(s, msg) < 0) {
		stream_reset(s);
		return -1;
	}

	/* Put length into the header at the start of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}
/*
 * 由顶点构造消息。msg 为 NULL 时新分配并清零，否则先 memset 清空复用。
 * 类型固定为 NODE，事件由顶点的 status 映射(NEW 为 ADD、UPDATE 为
 * UPDATE、DELETE 为 DELETE、SYNC 为 SYNC、UNSET 或 ORPHAN 为 UNDEF)。
 * data.node 指向顶点持有的 node，remote_id 置 UNKNOWN。返回可用的 msg。
 */
struct ls_message *ls_vertex2msg(struct ls_message *msg,
				 struct ls_vertex *vertex)
{
	/* Allocate space if needed */
	if (msg == NULL)
		msg = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_message));
	else
		memset(msg, 0, sizeof(*msg));

	msg->type = LS_MSG_TYPE_NODE;
	switch (vertex->status) {
	case NEW:
		msg->event = LS_MSG_EVENT_ADD;
		break;
	case UPDATE:
		msg->event = LS_MSG_EVENT_UPDATE;
		break;
	case DELETE:
		msg->event = LS_MSG_EVENT_DELETE;
		break;
	case SYNC:
		msg->event = LS_MSG_EVENT_SYNC;
		break;
	case UNSET:
	case ORPHAN:
		msg->event = LS_MSG_EVENT_UNDEF;
		break;
	}
	msg->data.node = vertex->node;
	msg->remote_id.origin = UNKNOWN;

	return msg;
}

/*
 * 由边构造消息：类型为 ATTRIBUTES，事件由边 status 映射，data.attr 指向
 * 边的 attributes。remote_id 取目的顶点的 adv(存在目的顶点时)，否则
 * 置 UNKNOWN。msg 为 NULL 时新分配并清零。返回可用的 msg。
 */
struct ls_message *ls_edge2msg(struct ls_message *msg, struct ls_edge *edge)
{
	/* Allocate space if needed */
	if (msg == NULL)
		msg = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_message));
	else
		memset(msg, 0, sizeof(*msg));

	msg->type = LS_MSG_TYPE_ATTRIBUTES;
	switch (edge->status) {
	case NEW:
		msg->event = LS_MSG_EVENT_ADD;
		break;
	case UPDATE:
		msg->event = LS_MSG_EVENT_UPDATE;
		break;
	case DELETE:
		msg->event = LS_MSG_EVENT_DELETE;
		break;
	case SYNC:
		msg->event = LS_MSG_EVENT_SYNC;
		break;
	case UNSET:
	case ORPHAN:
		msg->event = LS_MSG_EVENT_UNDEF;
		break;
	}
	msg->data.attr = edge->attributes;
	if (edge->destination != NULL)
		msg->remote_id = edge->destination->node->adv;
	else
		msg->remote_id.origin = UNKNOWN;

	return msg;
}

/*
 * 由子网构造消息：类型为 PREFIX，事件由子网 status 映射，data.prefix
 * 指向子网的 ls_pref，remote_id 置 UNKNOWN。msg 为 NULL 时新分配并
 * 清零。返回可用的 msg。
 */
struct ls_message *ls_subnet2msg(struct ls_message *msg,
				 struct ls_subnet *subnet)
{
	/* Allocate space if needed */
	if (msg == NULL)
		msg = XCALLOC(MTYPE_LS_DB, sizeof(struct ls_message));
	else
		memset(msg, 0, sizeof(*msg));

	msg->type = LS_MSG_TYPE_PREFIX;
	switch (subnet->status) {
	case NEW:
		msg->event = LS_MSG_EVENT_ADD;
		break;
	case UPDATE:
		msg->event = LS_MSG_EVENT_UPDATE;
		break;
	case DELETE:
		msg->event = LS_MSG_EVENT_DELETE;
		break;
	case SYNC:
		msg->event = LS_MSG_EVENT_SYNC;
		break;
	case UNSET:
	case ORPHAN:
		msg->event = LS_MSG_EVENT_UNDEF;
		break;
	}
	msg->data.prefix = subnet->ls_pref;
	msg->remote_id.origin = UNKNOWN;

	return msg;
}

/*
 * 把消息应用到 TED 的顶点并返回该顶点：
 *   SYNC / ADD  新增顶点并置相应状态；UPDATE 更新；
 *   DELETE      先按标识查找；delete 为真时直接删除(del_all)并返回
 *               NULL，否则只把状态改为 DELETE。
 * 其它事件返回 NULL。
 */
struct ls_vertex *ls_msg2vertex(struct ls_ted *ted, struct ls_message *msg,
				bool delete)
{
	struct ls_node *node = msg->data.node;
	struct ls_vertex *vertex = NULL;

	switch (msg->event) {
	case LS_MSG_EVENT_SYNC:
		vertex = ls_vertex_add(ted, node);
		if (vertex)
			vertex->status = SYNC;
		break;
	case LS_MSG_EVENT_ADD:
		vertex = ls_vertex_add(ted, node);
		if (vertex)
			vertex->status = NEW;
		break;
	case LS_MSG_EVENT_UPDATE:
		vertex = ls_vertex_update(ted, node);
		if (vertex)
			vertex->status = UPDATE;
		break;
	case LS_MSG_EVENT_DELETE:
		vertex = ls_find_vertex_by_id(ted, node->adv);
		if (vertex) {
			if (delete) {
				ls_vertex_del_all(ted, vertex);
				vertex = NULL;
			} else
				vertex->status = DELETE;
		}
		break;
	default:
		vertex = NULL;
		break;
	}

	return vertex;
}

/*
 * 把消息应用到 TED 的边并返回该边：SYNC / ADD 新增、UPDATE 更新、
 * DELETE 按源侧查找后按 delete 决定删除或仅标记。其它事件返回 NULL。
 * 注意：本函数接管传入 attributes 的所有权(新增 / 更新时)。
 */
struct ls_edge *ls_msg2edge(struct ls_ted *ted, struct ls_message *msg,
			    bool delete)
{
	struct ls_attributes *attr = msg->data.attr;
	struct ls_edge *edge = NULL;

	switch (msg->event) {
	case LS_MSG_EVENT_SYNC:
		edge = ls_edge_add(ted, attr);
		if (edge)
			edge->status = SYNC;
		break;
	case LS_MSG_EVENT_ADD:
		edge = ls_edge_add(ted, attr);
		if (edge)
			edge->status = NEW;
		break;
	case LS_MSG_EVENT_UPDATE:
		edge = ls_edge_update(ted, attr);
		if (edge)
			edge->status = UPDATE;
		break;
	case LS_MSG_EVENT_DELETE:
		edge = ls_find_edge_by_source(ted, attr);
		if (edge) {
			if (delete) {
				ls_edge_del_all(ted, edge);
				edge = NULL;
			} else
				edge->status = DELETE;
		}
		break;
	default:
		edge = NULL;
		break;
	}

	return edge;
}

/*
 * 把消息应用到 TED 的子网并返回该子网：SYNC / ADD 新增、UPDATE 更新、
 * DELETE 按前缀查找后按 delete 决定删除或仅标记。其它事件返回 NULL。
 * 注意：本函数接管传入 prefix 的所有权(新增 / 更新时)。
 */
struct ls_subnet *ls_msg2subnet(struct ls_ted *ted, struct ls_message *msg,
				bool delete)
{
	struct ls_prefix *pref = msg->data.prefix;
	struct ls_subnet *subnet = NULL;

	switch (msg->event) {
	case LS_MSG_EVENT_SYNC:
		subnet = ls_subnet_add(ted, pref);
		if (subnet)
			subnet->status = SYNC;
		break;
	case LS_MSG_EVENT_ADD:
		subnet = ls_subnet_add(ted, pref);
		if (subnet)
			subnet->status = NEW;
		break;
	case LS_MSG_EVENT_UPDATE:
		subnet = ls_subnet_update(ted, pref);
		if (subnet)
			subnet->status = UPDATE;
		break;
	case LS_MSG_EVENT_DELETE:
		subnet = ls_find_subnet(ted, &pref->pref);
		if (subnet) {
			if (delete) {
				ls_subnet_del_all(ted, subnet);
				subnet = NULL;
			} else
				subnet->status = DELETE;
		}
		break;
	default:
		subnet = NULL;
		break;
	}

	return subnet;
}

/*
 * 消息到图元素的统一分派入口：按 msg->type 调用 ls_msg2vertex /
 * ls_msg2edge / ls_msg2subnet，并把结果转成通用 ls_element 返回；未知
 * 类型返回 NULL。这是消费者处理一条收到的消息时最常用的入口。
 */
struct ls_element *ls_msg2ted(struct ls_ted *ted, struct ls_message *msg,
			       bool delete)
{
	struct ls_element *lse = NULL;

	switch (msg->type) {
	case LS_MSG_TYPE_NODE:
		lse = (struct ls_element *)ls_msg2vertex(ted, msg, delete);
		break;
	case LS_MSG_TYPE_ATTRIBUTES:
		lse = (struct ls_element *)ls_msg2edge(ted, msg, delete);
		break;
	case LS_MSG_TYPE_PREFIX:
		lse = (struct ls_element *)ls_msg2subnet(ted, msg, delete);
		break;
	default:
		lse = NULL;
		break;
	}

	return lse;
}

/*
 * 直接从数据流解析并应用到 TED：内部依次调用 ls_parse_msg、ls_msg2ted，
 * 最后释放消息。解析失败返回 NULL。
 * 注意：返回的 ls_element 指向消息内的元素；新增 / 更新时元素已被 TED
 * 接管(不会被 ls_delete_msg 释放)，被删除时返回 NULL。
 */
struct ls_element *ls_stream2ted(struct ls_ted *ted, struct stream *s,
				  bool delete)
{
	struct ls_message *msg;
	struct ls_element *lse = NULL;

	msg = ls_parse_msg(s);
	if (msg) {
		lse = ls_msg2ted(ted, msg, delete);
		ls_delete_msg(msg);
	}

	return lse;
}

/*
 * 释放一条消息。仅当 event 为 DELETE 时，才连同消息内承载的元素一起
 * 释放(node / attributes / prefix)——因为此时元素没有被 TED 接管。对
 * 于 ADD / UPDATE / SYNC，元素已交由 TED 管理，这里只释放消息结构体
 * 本身，避免重复释放或产生悬空指针。msg 为 NULL 时直接返回。
 */
void ls_delete_msg(struct ls_message *msg)
{
	if (msg == NULL)
		return;

	if (msg->event == LS_MSG_EVENT_DELETE) {
		switch (msg->type) {
		case LS_MSG_TYPE_NODE:
			ls_node_del(msg->data.node);
			break;
		case LS_MSG_TYPE_ATTRIBUTES:
			ls_attributes_del(msg->data.attr);
			break;
		case LS_MSG_TYPE_PREFIX:
			ls_prefix_del(msg->data.prefix);
			break;
		}
	}

	XFREE(MTYPE_LS_DB, msg);
}

/*
 * 把整个 TED 按“顶点、边、子网”的顺序逐个封装成消息并发送给 dst，用于
 * 响应一次同步请求。栈上复用同一条 msg，减少分配开销。始终返回 0。
 */
int ls_sync_ted(struct ls_ted *ted, struct zclient *zclient,
		struct zapi_opaque_reg_info *dst)
{
	struct ls_vertex *vertex;
	struct ls_edge *edge;
	struct ls_subnet *subnet;
	struct ls_message msg;

	/* Loop TED, start sending Node, then Attributes and finally Prefix */
	frr_each(vertices, &ted->vertices, vertex) {
		ls_vertex2msg(&msg, vertex);
		ls_send_msg(zclient, &msg, dst);
	}
	frr_each(edges, &ted->edges, edge) {
		ls_edge2msg(&msg, edge);
		ls_send_msg(zclient, &msg, dst);
	}
	frr_each(subnets, &ted->subnets, subnet) {
		ls_subnet2msg(&msg, subnet);
		ls_send_msg(zclient, &msg, dst);
	}
	return 0;
}

/**
 *  Link State Show functions
 */
/*
 * 节点来源枚举(ls_origin)到可读文本的映射表，下标必须与枚举顺序一一
 * 对应(UNKNOWN / ISIS_L1 / ISIS_L2 / OSPFv2 / DIRECT / STATIC)。
 */
static const char *const origin2txt[] = {
	"Unknown",
	"ISIS_L1",
	"ISIS_L2",
	"OSPFv2",
	"Direct",
	"Static"
};

/*
 * 节点类型枚举(ls_node_type)到可读文本的映射表，下标与枚举顺序对应
 * (NONE / STANDARD / ABR / ASBR / RMT_ASBR / PSEUDO)。
 */
static const char *const type2txt[] = {
	"Unknown",
	"Standard",
	"ABR",
	"ASBR",
	"Remote ASBR",
	"Pseudo"
};

/*
 * 元素状态枚举(ls_status)到可读文本的映射表，下标与枚举顺序对应
 * (UNSET / NEW / UPDATE / DELETE / SYNC / ORPHAN)。
 */
static const char *const status2txt[] = {
	"Unknown",
	"New",
	"Update",
	"Delete",
	"Sync",
	"Orphan"
};

/*
 * 把节点标识渲染成字符串：IS-IS 用 %pSY 打印 System-ID，其余(OSPF 等)
 * 用 %pI4 打印 IPv4 地址。返回传入的 str 缓冲区；size 为其容量。
 */
static const char *ls_node_id_to_text(struct ls_node_id lnid, char *str,
				      size_t size)
{
	if (lnid.origin == ISIS_L1 || lnid.origin == ISIS_L2)
		snprintfrr(str, size, "%pSY", lnid.id.iso.sys_id);
	else
		snprintfrr(str, size, "%pI4", &lnid.id.ip.addr);

	return str;
}

/*
 * 以 vty 文本形式打印单个顶点。verbose 为假时只打印一行摘要(出边 /
 * 入边 / 子网计数)；为真时进一步打印节点类型、AS 号、Segment Routing
 * 能力(SRGB / SRLB / 算法 / MSD)，并逐条列出出边、入边与子网。
 * 内部用 sbuf 拼接整段文本后一次性 vty_out 输出。vertex 为 NULL 时
 * 直接返回。
 */
static void ls_show_vertex_vty(struct ls_vertex *vertex, struct vty *vty,
			       bool verbose)
{
	struct listnode *node;
	struct ls_node *lsn;
	struct ls_edge *edge;
	struct ls_attributes *attr;
	struct ls_subnet *subnet;
	struct sbuf sbuf;
	uint32_t upper;

	/* Sanity Check */
	if (!vertex)
		return;

	lsn = vertex->node;

	sbuf_init(&sbuf, NULL, 0);

	sbuf_push(&sbuf, 2, "Vertex (%" PRIu64 "): %s", vertex->key, lsn->name);
	sbuf_push(&sbuf, 0, "\tRouter Id: %pI4", &lsn->router_id);
	sbuf_push(&sbuf, 0, "\tOrigin: %s", origin2txt[lsn->adv.origin]);
	sbuf_push(&sbuf, 0, "\tStatus: %s\n", status2txt[vertex->status]);
	if (!verbose) {
		sbuf_push(
			&sbuf, 0,
			"\t%d Outgoing Edges, %d Incoming Edges, %d Subnets\n",
			listcount(vertex->outgoing_edges),
			listcount(vertex->incoming_edges),
			listcount(vertex->prefixes));
		goto end;
	}

	if (CHECK_FLAG(lsn->flags, LS_NODE_TYPE))
		sbuf_push(&sbuf, 4, "Type: %s\n", type2txt[lsn->type]);
	if (CHECK_FLAG(lsn->flags, LS_NODE_AS_NUMBER))
		sbuf_push(&sbuf, 4, "AS number: %u\n", lsn->as_number);
	if (CHECK_FLAG(lsn->flags, LS_NODE_SR)) {
		sbuf_push(&sbuf, 4, "Segment Routing Capabilities:\n");
		upper = lsn->srgb.lower_bound + lsn->srgb.range_size - 1;
		sbuf_push(&sbuf, 8, "SRGB: [%d/%d]", lsn->srgb.lower_bound,
			  upper);
		if (CHECK_FLAG(lsn->flags, LS_NODE_SRLB)) {
			upper = lsn->srlb.lower_bound + lsn->srlb.range_size
				- 1;
			sbuf_push(&sbuf, 0, "\tSRLB: [%d/%d]",
				  lsn->srlb.lower_bound, upper);
		}
		sbuf_push(&sbuf, 0, "\tAlgo: ");
		for (int i = 0; i < 2; i++) {
			if (lsn->algo[i] == 255)
				continue;

			sbuf_push(&sbuf, 0,
				  lsn->algo[i] == 0 ? "SPF " : "S-SPF ");
		}
		if (CHECK_FLAG(lsn->flags, LS_NODE_MSD))
			sbuf_push(&sbuf, 0, "\tMSD: %d", lsn->msd);
		sbuf_push(&sbuf, 0, "\n");
	}

	sbuf_push(&sbuf, 4, "Outgoing Edges: %d\n",
		  listcount(vertex->outgoing_edges));
	for (ALL_LIST_ELEMENTS_RO(vertex->outgoing_edges, node, edge)) {
		if (edge->destination) {
			lsn = edge->destination->node;
			sbuf_push(&sbuf, 6, "To:\t%s(%pI4)", lsn->name,
				  &lsn->router_id);
		} else {
			sbuf_push(&sbuf, 6, "To:\t- (0.0.0.0)");
		}
		attr = edge->attributes;
		if ((CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR)))
			sbuf_push(&sbuf, 0, "\tLocal:  %pI4\tRemote: %pI4\n",
				  &attr->standard.local,
				  &attr->standard.remote);
		else if ((CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6)))
			sbuf_push(&sbuf, 0, "\tLocal:  %pI6\tRemote: %pI6\n",
				  &attr->standard.local6,
				  &attr->standard.remote6);
	}

	sbuf_push(&sbuf, 4, "Incoming Edges: %d\n",
		  listcount(vertex->incoming_edges));
	for (ALL_LIST_ELEMENTS_RO(vertex->incoming_edges, node, edge)) {
		if (edge->source) {
			lsn = edge->source->node;
			sbuf_push(&sbuf, 6, "From:\t%s(%pI4)", lsn->name,
				  &lsn->router_id);
		} else {
			sbuf_push(&sbuf, 6, "From:\t- (0.0.0.0)");
		}
		attr = edge->attributes;
		if ((CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR)))
			sbuf_push(&sbuf, 0, "\tLocal:  %pI4\tRemote: %pI4\n",
				  &attr->standard.local,
				  &attr->standard.remote);
		else if ((CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6)))
			sbuf_push(&sbuf, 0, "\tLocal:  %pI6\tRemote: %pI6\n",
				  &attr->standard.local6,
				  &attr->standard.remote6);
	}

	sbuf_push(&sbuf, 4, "Subnets: %d\n", listcount(vertex->prefixes));
	for (ALL_LIST_ELEMENTS_RO(vertex->prefixes, node, subnet))
		sbuf_push(&sbuf, 6, "Prefix:\t%pFX\n", &subnet->key);

end:
	vty_out(vty, "%s\n", sbuf_buf(&sbuf));
	sbuf_free(&sbuf);
}

/*
 * 以 JSON 形式打印单个顶点的信息。按 flags 逐项把节点标识、Router-ID、
 * 类型、AS 号、Segment Routing(SRGB / SRLB / 算法 / MSD)等加入 json
 * 对象。vertex 为 NULL 时直接返回。
 */
static void ls_show_vertex_json(struct ls_vertex *vertex,
				struct json_object *json)
{
	struct ls_node *lsn;
	json_object *jsr, *jalgo, *jobj;
	char buf[INET6_BUFSIZ];

	/* Sanity Check */
	if (!vertex)
		return;

	lsn = vertex->node;

	json_object_int_add(json, "vertex-id", vertex->key);
	json_object_string_add(json, "status", status2txt[vertex->status]);
	json_object_string_add(json, "origin", origin2txt[lsn->adv.origin]);
	if (CHECK_FLAG(lsn->flags, LS_NODE_NAME))
		json_object_string_add(json, "name", lsn->name);
	if (CHECK_FLAG(lsn->flags, LS_NODE_ROUTER_ID)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI4", &lsn->router_id);
		json_object_string_add(json, "router-id", buf);
	}
	if (CHECK_FLAG(lsn->flags, LS_NODE_ROUTER_ID6)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI6", &lsn->router_id6);
		json_object_string_add(json, "router-id-v6", buf);
	}
	if (CHECK_FLAG(lsn->flags, LS_NODE_TYPE))
		json_object_string_add(json, "vertex-type",
				       type2txt[lsn->type]);
	if (CHECK_FLAG(lsn->flags, LS_NODE_AS_NUMBER))
		json_object_int_add(json, "asn", lsn->as_number);
	if (CHECK_FLAG(lsn->flags, LS_NODE_SR)) {
		jsr = json_object_new_object();
		json_object_object_add(json, "segment-routing", jsr);
		json_object_int_add(jsr, "srgb-size", lsn->srgb.range_size);
		json_object_int_add(jsr, "srgb-lower", lsn->srgb.lower_bound);
		jalgo = json_object_new_array();
		json_object_object_add(jsr, "algorithms", jalgo);
		for (int i = 0; i < 2; i++) {
			if (lsn->algo[i] == 255)
				continue;
			jobj = json_object_new_object();

			snprintfrr(buf, 2, "%u", i);
			json_object_string_add(
				jobj, buf, lsn->algo[i] == 0 ? "SPF" : "S-SPF");
			json_object_array_add(jalgo, jobj);
		}
		if (CHECK_FLAG(lsn->flags, LS_NODE_SRLB)) {
			json_object_int_add(jsr, "srlb-size",
					    lsn->srlb.range_size);
			json_object_int_add(jsr, "srlb-lower",
					    lsn->srlb.lower_bound);
		}
		if (CHECK_FLAG(lsn->flags, LS_NODE_MSD))
			json_object_int_add(jsr, "msd", lsn->msd);
	}
}

/*
 * 顶点展示入口：有 json 时走 JSON，否则有 vty 时走文本，两者都为空则
 * 什么也不做。头文件声明“同时指定时以 JSON 为准”，此实现正体现该
 * 优先级。
 */
void ls_show_vertex(struct ls_vertex *vertex, struct vty *vty,
		    struct json_object *json, bool verbose)
{
	if (json)
		ls_show_vertex_json(vertex, json);
	else if (vty)
		ls_show_vertex_vty(vertex, vty, verbose);
}

/*
 * 打印数据库中的全部顶点：JSON 模式下放入 "vertices" 数组(每项一个
 * 对象)，文本模式下逐个 vty 输出。
 */
void ls_show_vertices(struct ls_ted *ted, struct vty *vty,
		      struct json_object *json, bool verbose)
{
	struct ls_vertex *vertex;
	json_object *jnodes, *jnode;

	if (json) {
		jnodes = json_object_new_array();
		json_object_object_add(json, "vertices", jnodes);
		frr_each (vertices, &ted->vertices, vertex) {
			jnode = json_object_new_object();
			ls_show_vertex(vertex, NULL, jnode, verbose);
			json_object_array_add(jnodes, jnode);
		}
	} else if (vty) {
		frr_each (vertices, &ted->vertices, vertex)
			ls_show_vertex(vertex, vty, NULL, verbose);
	}
}

/*
 * 把边的 key 渲染成文本：按地址族分别用 %pI4 / %pI6 打印地址，AF_LOCAL
 * 打印 link_id 数值，其它打印 "(Unknown)"。
 * 注意：返回值指向一个 4 槽位的静态环形缓冲，多次调用时按次序轮换，
 * 以避免同一 printf 中多个结果互相覆盖；若同时需要多于 4 个结果，需
 * 自行拷贝。
 */
static const char *edge_key_to_text(struct ls_edge_key key)
{
#define FORMAT_BUF_COUNT 4
	static char buf_ring[FORMAT_BUF_COUNT][INET6_BUFSIZ];
	static size_t cur_buf = 0;
	char *rv;

	rv = buf_ring[cur_buf];
	cur_buf = (cur_buf + 1) % FORMAT_BUF_COUNT;

	switch (key.family) {
	case AF_INET:
		snprintfrr(rv, INET6_BUFSIZ, "%pI4", &key.k.addr);
		break;
	case AF_INET6:
		snprintfrr(rv, INET6_BUFSIZ, "%pI6", &key.k.addr6);
		break;
	case AF_LOCAL:
		snprintfrr(rv, INET6_BUFSIZ, "%" PRIu64, key.k.link_id);
		break;
	default:
		snprintfrr(rv, INET6_BUFSIZ, "(Unknown)");
		break;
	}

	return rv;
}

/*
 * 以 vty 文本形式打印单条边。先打印边 key、本端地址、广告顶点、度量与
 * 状态；verbose 为真时逐项打印 TE 度量、管理组与扩展管理组、IPv4 与
 * IPv6 地址、标识、各类带宽、远端 AS、时延与抖动与丢包、邻接 SID、
 * SRv6 邻接 SID 与 SRLG 等。用 sbuf 拼接后一次性输出。
 */
static void ls_show_edge_vty(struct ls_edge *edge, struct vty *vty,
			     bool verbose)
{
	char admin_group_buf[ADMIN_GROUP_PRINT_MAX_SIZE];
	struct ls_attributes *attr;
	struct sbuf sbuf;
	char buf[INET6_BUFSIZ];
	int indent;

	attr = edge->attributes;
	sbuf_init(&sbuf, NULL, 0);

	sbuf_push(&sbuf, 2, "Edge (%s): ", edge_key_to_text(edge->key));
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR))
		sbuf_push(&sbuf, 0, "%pI4", &attr->standard.local);
	else if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6))
		sbuf_push(&sbuf, 0, "%pI6", &attr->standard.local6);
	else
		sbuf_push(&sbuf, 0, "%u/%u", attr->standard.local_id,
			  attr->standard.remote_id);
	ls_node_id_to_text(attr->adv, buf, INET6_BUFSIZ);
	sbuf_push(&sbuf, 0, "\tAdv. Vertex: %s", buf);
	sbuf_push(&sbuf, 0, "\tMetric: %u", attr->metric);
	sbuf_push(&sbuf, 0, "\tStatus: %s\n", status2txt[edge->status]);

	if (!verbose)
		goto end;

	sbuf_push(&sbuf, 4, "Origin: %s\n", origin2txt[attr->adv.origin]);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NAME))
		sbuf_push(&sbuf, 4, "Name: %s\n", attr->name);
	if (CHECK_FLAG(attr->flags, LS_ATTR_TE_METRIC))
		sbuf_push(&sbuf, 4, "TE Metric: %u\n",
			  attr->standard.te_metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADM_GRP))
		sbuf_push(&sbuf, 4, "Admin Group: 0x%x\n",
			  attr->standard.admin_group);
	if (CHECK_FLAG(attr->flags, LS_ATTR_EXT_ADM_GRP) &&
	    admin_group_nb_words(&attr->ext_admin_group) != 0) {
		indent = 4;
		sbuf_push(&sbuf, indent, "Ext Admin Group: %s\n",
			  admin_group_string(
				  admin_group_buf, ADMIN_GROUP_PRINT_MAX_SIZE,
				  indent + strlen("Ext Admin Group: "),
				  &attr->ext_admin_group));
		if (admin_group_buf[0] != '\0' &&
		    (sbuf.pos + strlen(admin_group_buf) +
		     SBUF_DEFAULT_SIZE / 2) < sbuf.size)
			sbuf_push(&sbuf, indent + 2, "Bit positions: %s\n",
				  admin_group_buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR))
		sbuf_push(&sbuf, 4, "Local IPv4 address: %pI4\n",
			  &attr->standard.local);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR))
		sbuf_push(&sbuf, 4, "Remote IPv4 address: %pI4\n",
			  &attr->standard.remote);
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6))
		sbuf_push(&sbuf, 4, "Local IPv6 address: %pI6\n",
			  &attr->standard.local6);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR6))
		sbuf_push(&sbuf, 4, "Remote IPv6 address: %pI6\n",
			  &attr->standard.remote6);
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ID))
		sbuf_push(&sbuf, 4, "Local Identifier: %u\n",
			  attr->standard.local_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ID))
		sbuf_push(&sbuf, 4, "Remote Identifier: %u\n",
			  attr->standard.remote_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_BW))
		sbuf_push(&sbuf, 4, "Maximum Bandwidth: %g (Bytes/s)\n",
			  attr->standard.max_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_RSV_BW))
		sbuf_push(&sbuf, 4,
			  "Maximum Reservable Bandwidth: %g (Bytes/s)\n",
			  attr->standard.max_rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_UNRSV_BW)) {
		sbuf_push(&sbuf, 4, "Unreserved Bandwidth per Class Type\n");
		for (int i = 0; i < MAX_CLASS_TYPE; i += 2)
			sbuf_push(&sbuf, 8,
				  "[%d]: %g (Bytes/sec)\t[%d]: %g (Bytes/s)\n",
				  i, attr->standard.unrsv_bw[i], i + 1,
				  attr->standard.unrsv_bw[i + 1]);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_AS))
		sbuf_push(&sbuf, 4, "Remote AS: %u\n",
			  attr->standard.remote_as);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR))
		sbuf_push(&sbuf, 4, "Remote ASBR IPv4 address: %pI4\n",
			  &attr->standard.remote_addr);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR6))
		sbuf_push(&sbuf, 4, "Remote ASBR IPv6 address: %pI6\n",
			  &attr->standard.remote_addr6);
	if (CHECK_FLAG(attr->flags, LS_ATTR_DELAY))
		sbuf_push(&sbuf, 4, "Average Link Delay: %d (micro-sec)\n",
			  attr->extended.delay);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MIN_MAX_DELAY))
		sbuf_push(&sbuf, 4, "Min/Max Link Delay: %d/%d (micro-sec)\n",
			  attr->extended.min_delay, attr->extended.max_delay);
	if (CHECK_FLAG(attr->flags, LS_ATTR_JITTER))
		sbuf_push(&sbuf, 4, "Delay Variation: %d (micro-sec)\n",
			  attr->extended.jitter);
	if (CHECK_FLAG(attr->flags, LS_ATTR_PACKET_LOSS))
		sbuf_push(&sbuf, 4, "Link Loss: %g (%%)\n",
			  (float)(attr->extended.pkt_loss * LOSS_PRECISION));
	if (CHECK_FLAG(attr->flags, LS_ATTR_AVA_BW))
		sbuf_push(&sbuf, 4, "Available Bandwidth: %g (Bytes/s)\n",
			  attr->extended.ava_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_RSV_BW))
		sbuf_push(&sbuf, 4, "Residual Bandwidth: %g (Bytes/s)\n",
			  attr->extended.rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_USE_BW))
		sbuf_push(&sbuf, 4, "Utilized Bandwidth: %g (Bytes/s)\n",
			  attr->extended.used_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID)) {
		sbuf_push(&sbuf, 4, "IPv4 Adjacency-SID: %u",
			  attr->adj_sid[ADJ_PRI_IPV4].sid);
		sbuf_push(&sbuf, 0, "\tFlags: 0x%x\tWeight: 0x%x\n",
			  attr->adj_sid[ADJ_PRI_IPV4].flags,
			  attr->adj_sid[ADJ_PRI_IPV4].weight);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID)) {
		sbuf_push(&sbuf, 4, "IPv4 Bck. Adjacency-SID: %u",
			  attr->adj_sid[ADJ_BCK_IPV4].sid);
		sbuf_push(&sbuf, 0, "\tFlags: 0x%x\tWeight: 0x%x\n",
			  attr->adj_sid[ADJ_BCK_IPV4].flags,
			  attr->adj_sid[ADJ_BCK_IPV4].weight);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID6)) {
		sbuf_push(&sbuf, 4, "IPv6 Adjacency-SID: %u",
			  attr->adj_sid[ADJ_PRI_IPV6].sid);
		sbuf_push(&sbuf, 0, "\tFlags: 0x%x\tWeight: 0x%x\n",
			  attr->adj_sid[ADJ_PRI_IPV6].flags,
			  attr->adj_sid[ADJ_PRI_IPV6].weight);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID6)) {
		sbuf_push(&sbuf, 4, "IPv6 Bck. Adjacency-SID: %u",
			  attr->adj_sid[ADJ_BCK_IPV6].sid);
		sbuf_push(&sbuf, 0, "\tFlags: 0x%x\tWeight: 0x%x\n",
			  attr->adj_sid[ADJ_BCK_IPV6].flags,
			  attr->adj_sid[ADJ_BCK_IPV6].weight);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SRV6SID)) {
		sbuf_push(&sbuf, 4, "IPv6 Adjacency-SRV6-SID: %pI6",
			  &attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].sid);
		sbuf_push(&sbuf, 0,
			  "\tFlags: 0x%x\tWeight: 0x%x\tbehavior: 0x%x\n",
			  attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].flags,
			  attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].weight,
			  attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].endpoint_behavior);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SRV6SID)) {
		sbuf_push(&sbuf, 4, "IPv6 Bck. Adjacency-SRV6-SID: %pI6",
			  &attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].sid);
		sbuf_push(&sbuf, 0,
			  "\tFlags: 0x%x\tWeight: 0x%x\tbehavior: 0x%x\n",
			  attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].flags,
			  attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].weight,
			  attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].endpoint_behavior);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_SRLG)) {
		sbuf_push(&sbuf, 4, "SRLGs: %d", attr->srlg_len);
		for (int i = 1; i < attr->srlg_len; i++) {
			if (i % 8)
				sbuf_push(&sbuf, 8, "\n%u", attr->srlgs[i]);
			else
				sbuf_push(&sbuf, 8, ", %u", attr->srlgs[i]);
		}
		sbuf_push(&sbuf, 0, "\n");
	}

end:
	vty_out(vty, "%s\n", sbuf_buf(&sbuf));
	sbuf_free(&sbuf);
}

/*
 * 扩展管理组的十六进制文本 "0x%08x" 所需缓冲长度(10 个字符加结尾 0，共 11)。
 */
#define AG_LEN 11

/*
 * 以 JSON 形式打印单条边：把边标识、状态、来源、两端顶点 key、度量与
 * 名称，以及逐项的 TE 属性(管理组与扩展管理组、地址、带宽、时延、
 * SRLG、邻接 SID 与 SRv6 邻接 SID)加入 json 对象。扩展管理组会同时
 * 输出 words(十六进制)与 bitPositions(置位下标)两种视角。
 */
static void ls_show_edge_json(struct ls_edge *edge, struct json_object *json)
{
	struct ls_attributes *attr;
	struct json_object *jte, *jbw, *jobj, *jsr = NULL, *jsrlg, *js_ext_ag,
					      *js_ext_ag_arr_word,
					      *js_ext_ag_arr_bit, *jsrv6 = NULL;
	char buf[INET6_BUFSIZ];
	char buf_ag[AG_LEN];
	uint32_t bitmap;
	size_t i;

	attr = edge->attributes;

	json_object_string_add(json, "edge-id", edge_key_to_text(edge->key));
	json_object_string_add(json, "status", status2txt[edge->status]);
	json_object_string_add(json, "origin", origin2txt[attr->adv.origin]);
	ls_node_id_to_text(attr->adv, buf, INET6_BUFSIZ);
	json_object_string_add(json, "advertised-router", buf);
	if (edge->source)
		json_object_int_add(json, "local-vertex-id", edge->source->key);
	if (edge->destination)
		json_object_int_add(json, "remote-vertex-id",
				    edge->destination->key);
	json_object_int_add(json, "metric", attr->metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NAME))
		json_object_string_add(json, "name", attr->name);
	jte = json_object_new_object();
	json_object_object_add(json, "edge-attributes", jte);
	if (CHECK_FLAG(attr->flags, LS_ATTR_TE_METRIC))
		json_object_int_add(jte, "te-metric", attr->standard.te_metric);
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADM_GRP))
		json_object_int_add(jte, "admin-group",
				    attr->standard.admin_group);
	if (CHECK_FLAG(attr->flags, LS_ATTR_EXT_ADM_GRP)) {
		js_ext_ag = json_object_new_object();
		json_object_object_add(jte, "extAdminGroup", js_ext_ag);
		js_ext_ag_arr_word = json_object_new_array();
		json_object_object_add(js_ext_ag, "words", js_ext_ag_arr_word);
		js_ext_ag_arr_bit = json_object_new_array();
		json_object_object_add(js_ext_ag, "bitPositions",
				       js_ext_ag_arr_bit);
		for (i = 0; i < admin_group_nb_words(&attr->ext_admin_group);
		     i++) {
			bitmap = admin_group_get_offset(&attr->ext_admin_group,
							i);
			snprintf(buf_ag, sizeof(buf_ag), "0x%08x", bitmap);
			json_object_array_add(js_ext_ag_arr_word,
					      json_object_new_string(buf_ag));
		}
		for (i = 0;
		     i < (admin_group_size(&attr->ext_admin_group) * WORD_SIZE);
		     i++) {
			if (admin_group_get(&attr->ext_admin_group, i))
				json_object_array_add(js_ext_ag_arr_bit,
						      json_object_new_int(i));
		}
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI4", &attr->standard.local);
		json_object_string_add(jte, "local-address", buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI4", &attr->standard.remote);
		json_object_string_add(jte, "remote-address", buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ADDR6)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI6", &attr->standard.local6);
		json_object_string_add(jte, "local-address-v6", buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ADDR6)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI6", &attr->standard.remote6);
		json_object_string_add(jte, "remote-address-v6", buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_LOCAL_ID))
		json_object_int_add(jte, "local-identifier",
				    attr->standard.local_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_NEIGH_ID))
		json_object_int_add(jte, "remote-identifier",
				    attr->standard.remote_id);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_BW))
		json_object_double_add(jte, "max-link-bandwidth",
				       attr->standard.max_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MAX_RSV_BW))
		json_object_double_add(jte, "max-resv-link-bandwidth",
				       attr->standard.max_rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_UNRSV_BW)) {
		jbw = json_object_new_array();
		json_object_object_add(jte, "unreserved-bandwidth", jbw);
		for (i = 0; i < MAX_CLASS_TYPE; i++) {
			jobj = json_object_new_object();
			snprintfrr(buf, 13, "class-type-%u", (unsigned int)i);
			json_object_double_add(jobj, buf,
					       attr->standard.unrsv_bw[i]);
			json_object_array_add(jbw, jobj);
		}
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_AS))
		json_object_int_add(jte, "remote-asn",
				    attr->standard.remote_as);
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI4",
			   &attr->standard.remote_addr);
		json_object_string_add(jte, "remote-as-address", buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_REMOTE_ADDR6)) {
		snprintfrr(buf, INET6_BUFSIZ, "%pI6",
			   &attr->standard.remote_addr6);
		json_object_string_add(jte, "remote-as-address-v6", buf);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_DELAY))
		json_object_int_add(jte, "delay", attr->extended.delay);
	if (CHECK_FLAG(attr->flags, LS_ATTR_MIN_MAX_DELAY)) {
		json_object_int_add(jte, "min-delay", attr->extended.min_delay);
		json_object_int_add(jte, "max-delay", attr->extended.max_delay);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_JITTER))
		json_object_int_add(jte, "jitter", attr->extended.jitter);
	if (CHECK_FLAG(attr->flags, LS_ATTR_PACKET_LOSS))
		json_object_double_add(
			jte, "loss", attr->extended.pkt_loss * LOSS_PRECISION);
	if (CHECK_FLAG(attr->flags, LS_ATTR_AVA_BW))
		json_object_double_add(jte, "available-bandwidth",
				       attr->extended.ava_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_RSV_BW))
		json_object_double_add(jte, "residual-bandwidth",
				       attr->extended.rsv_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_USE_BW))
		json_object_double_add(jte, "utilized-bandwidth",
				       attr->extended.used_bw);
	if (CHECK_FLAG(attr->flags, LS_ATTR_SRLG)) {
		jsrlg = json_object_new_array();
		json_object_object_add(jte, "srlgs", jsrlg);
		for (i = 1; i < attr->srlg_len; i++) {
			jobj = json_object_new_object();
			json_object_int_add(jobj, "srlg", attr->srlgs[i]);
			json_object_array_add(jsrlg, jobj);
		}
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID)) {
		jsr = json_object_new_array();
		json_object_object_add(json, "segment-routing", jsr);
		jobj = json_object_new_object();
		json_object_int_add(jobj, "adj-sid",
				    attr->adj_sid[ADJ_PRI_IPV4].sid);
		snprintfrr(buf, 6, "0x%x", attr->adj_sid[ADJ_PRI_IPV4].flags);
		json_object_string_add(jobj, "flags", buf);
		json_object_int_add(jobj, "weight",
				    attr->adj_sid[ADJ_PRI_IPV4].weight);
		json_object_array_add(jsr, jobj);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID)) {
		if (!jsr) {
			jsr = json_object_new_array();
			json_object_object_add(json, "segment-routing", jsr);
		}
		jobj = json_object_new_object();
		json_object_int_add(jobj, "adj-sid",
				    attr->adj_sid[ADJ_BCK_IPV4].sid);
		snprintfrr(buf, 6, "0x%x", attr->adj_sid[ADJ_BCK_IPV4].flags);
		json_object_string_add(jobj, "flags", buf);
		json_object_int_add(jobj, "weight",
				    attr->adj_sid[ADJ_BCK_IPV4].weight);
		json_object_array_add(jsr, jobj);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID6)) {
		jsr = json_object_new_array();
		json_object_object_add(json, "segment-routing", jsr);
		jobj = json_object_new_object();
		json_object_int_add(jobj, "adj-sid",
				    attr->adj_sid[ADJ_PRI_IPV6].sid);
		snprintfrr(buf, 6, "0x%x", attr->adj_sid[ADJ_PRI_IPV6].flags);
		json_object_string_add(jobj, "flags", buf);
		json_object_int_add(jobj, "weight",
				    attr->adj_sid[ADJ_PRI_IPV6].weight);
		json_object_array_add(jsr, jobj);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SID6)) {
		if (!jsr) {
			jsr = json_object_new_array();
			json_object_object_add(json, "segment-routing", jsr);
		}
		jobj = json_object_new_object();
		json_object_int_add(jobj, "adj-sid",
				    attr->adj_sid[ADJ_BCK_IPV6].sid);
		snprintfrr(buf, 6, "0x%x", attr->adj_sid[ADJ_BCK_IPV6].flags);
		json_object_string_add(jobj, "flags", buf);
		json_object_int_add(jobj, "weight",
				    attr->adj_sid[ADJ_BCK_IPV6].weight);
		json_object_array_add(jsr, jobj);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SRV6SID)) {
		jsrv6 = json_object_new_array();
		json_object_object_add(json, "segment-routing-ipv6", jsrv6);
		jobj = json_object_new_object();
		snprintfrr(buf, INET6_BUFSIZ, "%pI6",
			   &attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].sid);
		json_object_string_add(jobj, "adj-sid", buf);
		snprintfrr(buf, 6, "0x%x",
			   attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].flags);
		json_object_string_add(jobj, "flags", buf);
		json_object_int_add(jobj, "weight",
				    attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6].weight);
		snprintfrr(buf, 6, "0x%x",
			   attr->adj_srv6_sid[ADJ_SRV6_PRI_IPV6]
				   .endpoint_behavior);
		json_object_string_add(jobj, "endpoint-behavior", buf);
		json_object_array_add(jsr, jobj);
	}
	if (CHECK_FLAG(attr->flags, LS_ATTR_BCK_ADJ_SRV6SID)) {
		if (!jsrv6) {
			jsrv6 = json_object_new_array();
			json_object_object_add(json, "segment-routing-ipv6",
					       jsrv6);
		}
		jobj = json_object_new_object();
		snprintfrr(buf, INET6_BUFSIZ, "%pI6",
			   &attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].sid);
		json_object_string_add(jobj, "adj-sid", buf);
		snprintfrr(buf, 6, "0x%x",
			   attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].flags);
		json_object_string_add(jobj, "flags", buf);
		json_object_int_add(jobj, "weight",
				    attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6].weight);
		snprintfrr(buf, 6, "0x%x",
			   attr->adj_srv6_sid[ADJ_SRV6_BCK_IPV6]
				   .endpoint_behavior);
		json_object_string_add(jobj, "endpoint-behavior", buf);
		json_object_array_add(jsr, jobj);
	}
}

/*
 * 边展示入口：有 json 走 JSON，否则有 vty 走文本。edge 为 NULL 时直接
 * 返回。
 */
void ls_show_edge(struct ls_edge *edge, struct vty *vty,
		  struct json_object *json, bool verbose)
{
	/* Sanity Check */
	if (!edge)
		return;

	if (json)
		ls_show_edge_json(edge, json);
	else if (vty)
		ls_show_edge_vty(edge, vty, verbose);
}

/*
 * 打印数据库中所有边：JSON 模式放入 "edges" 数组，文本模式逐个输出。
 */
void ls_show_edges(struct ls_ted *ted, struct vty *vty,
		   struct json_object *json, bool verbose)
{
	struct ls_edge *edge;
	json_object *jedges, *jedge;

	if (json) {
		jedges = json_object_new_array();
		json_object_object_add(json, "edges", jedges);
		frr_each (edges, &ted->edges, edge) {
			jedge = json_object_new_object();
			ls_show_edge(edge, NULL, jedge, verbose);
			json_object_array_add(jedges, jedge);
		}
	} else if (vty) {
		frr_each (edges, &ted->edges, edge)
			ls_show_edge(edge, vty, NULL, verbose);
	}
}

/*
 * 以 vty 文本形式打印单个子网：先打印前缀、广告顶点、度量与状态；
 * verbose 为真时再打印来源、IGP 标志、路由标签、扩展标签、SR SID，以及
 * SRv6 SID(用 seg6local_action2str 转换端点行为名称)。
 */
static void ls_show_subnet_vty(struct ls_subnet *subnet, struct vty *vty,
			       bool verbose)
{
	struct ls_prefix *pref;
	struct sbuf sbuf;
	char buf[INET6_BUFSIZ];

	pref = subnet->ls_pref;
	sbuf_init(&sbuf, NULL, 0);

	sbuf_push(&sbuf, 2, "Subnet: %pFX", &subnet->key);
	ls_node_id_to_text(pref->adv, buf, INET6_BUFSIZ);
	sbuf_push(&sbuf, 0, "\tAdv. Vertex: %s", buf);
	sbuf_push(&sbuf, 0, "\tMetric: %d", pref->metric);
	sbuf_push(&sbuf, 0, "\tStatus: %s\n", status2txt[subnet->status]);

	if (!verbose)
		goto end;

	sbuf_push(&sbuf, 4, "Origin: %s\n", origin2txt[pref->adv.origin]);
	if (CHECK_FLAG(pref->flags, LS_PREF_IGP_FLAG))
		sbuf_push(&sbuf, 4, "Flags: %d\n", pref->igp_flag);

	if (CHECK_FLAG(pref->flags, LS_PREF_ROUTE_TAG))
		sbuf_push(&sbuf, 4, "Tag: %d\n", pref->route_tag);

	if (CHECK_FLAG(pref->flags, LS_PREF_EXTENDED_TAG))
		sbuf_push(&sbuf, 4, "Extended Tag: %" PRIu64 "\n",
			  pref->extended_tag);

	if (CHECK_FLAG(pref->flags, LS_PREF_SR))
		sbuf_push(&sbuf, 4, "SID: %d\tAlgorithm: %d\tFlags: 0x%x\n",
			  pref->sr.sid, pref->sr.algo, pref->sr.sid_flag);

	if (CHECK_FLAG(pref->flags, LS_PREF_SRV6))
		sbuf_push(&sbuf, 4,
			  "SIDv6: %pI6\tEndpoint behavior: %s\tFlags: 0x%x\n",
			  &pref->srv6.sid,
			  seg6local_action2str(pref->srv6.behavior),
			  pref->srv6.flags);

end:
	vty_out(vty, "%s\n", sbuf_buf(&sbuf));
	sbuf_free(&sbuf);
}

/*
 * 以 JSON 形式打印单个子网：把子网标识、状态、来源、广告顶点、所属
 * 顶点 key 与度量，以及 IGP 标志、路由标签、扩展标签、SR SID 与 SRv6
 * SID 加入 json 对象。
 */
static void ls_show_subnet_json(struct ls_subnet *subnet,
				struct json_object *json)
{
	struct ls_prefix *pref;
	json_object *jsr, *jsrv6;
	char buf[INET6_BUFSIZ];

	pref = subnet->ls_pref;

	snprintfrr(buf, INET6_BUFSIZ, "%pFX", &subnet->key);
	json_object_string_add(json, "subnet-id", buf);
	json_object_string_add(json, "status", status2txt[subnet->status]);
	json_object_string_add(json, "origin", origin2txt[pref->adv.origin]);
	ls_node_id_to_text(pref->adv, buf, INET6_BUFSIZ);
	json_object_string_add(json, "advertised-router", buf);
	if (subnet->vertex)
		json_object_int_add(json, "vertex-id", subnet->vertex->key);
	json_object_int_add(json, "metric", pref->metric);
	if (CHECK_FLAG(pref->flags, LS_PREF_IGP_FLAG)) {
		snprintfrr(buf, INET6_BUFSIZ, "0x%x", pref->igp_flag);
		json_object_string_add(json, "flags", buf);
	}
	if (CHECK_FLAG(pref->flags, LS_PREF_ROUTE_TAG))
		json_object_int_add(json, "tag", pref->route_tag);
	if (CHECK_FLAG(pref->flags, LS_PREF_EXTENDED_TAG))
		json_object_int_add(json, "extended-tag", pref->extended_tag);
	if (CHECK_FLAG(pref->flags, LS_PREF_SR)) {
		jsr = json_object_new_object();
		json_object_object_add(json, "segment-routing", jsr);
		json_object_int_add(jsr, "pref-sid", pref->sr.sid);
		json_object_int_add(jsr, "algo", pref->sr.algo);
		snprintfrr(buf, INET6_BUFSIZ, "0x%x", pref->sr.sid_flag);
		json_object_string_add(jsr, "flags", buf);
	}
	if (CHECK_FLAG(pref->flags, LS_PREF_SRV6)) {
		jsrv6 = json_object_new_object();
		json_object_object_add(json, "segment-routing-ipv6", jsrv6);
		snprintfrr(buf, INET6_BUFSIZ, "%pI6", &pref->srv6.sid);
		json_object_string_add(jsrv6, "sid", buf);
		json_object_string_add(jsrv6, "behavior",
				       seg6local_action2str(pref->srv6.behavior));
		snprintfrr(buf, INET6_BUFSIZ, "0x%x", pref->srv6.flags);
		json_object_string_add(jsrv6, "flags", buf);
	}
}

/*
 * 子网展示入口：有 json 走 JSON，否则有 vty 走文本。subnet 为 NULL 时
 * 直接返回。
 */
void ls_show_subnet(struct ls_subnet *subnet, struct vty *vty,
		    struct json_object *json, bool verbose)
{
	/* Sanity Check */
	if (!subnet)
		return;

	if (json)
		ls_show_subnet_json(subnet, json);
	else if (vty)
		ls_show_subnet_vty(subnet, vty, verbose);
}

/*
 * 打印数据库中所有子网：JSON 模式放入 "subnets" 数组，文本模式逐个
 * 输出。
 */
void ls_show_subnets(struct ls_ted *ted, struct vty *vty,
		     struct json_object *json, bool verbose)
{
	struct ls_subnet *subnet;
	json_object *jsubs, *jsub;

	if (json) {
		jsubs = json_object_new_array();
		json_object_object_add(json, "subnets", jsubs);
		frr_each (subnets, &ted->subnets, subnet) {
			jsub = json_object_new_object();
			ls_show_subnet(subnet, NULL, jsub, verbose);
			json_object_array_add(jsubs, jsub);
		}
	} else if (vty) {
		frr_each (subnets, &ted->subnets, subnet)
			ls_show_subnet(subnet, vty, NULL, verbose);
	}
}

/*
 * 打印整个 TED：JSON 模式下建立 "ted" 对象，写入名称、key 与三类计数，
 * 再分别嵌入顶点 / 边 / 子网；文本模式下打印库名与总数，并逐个输出三
 * 类元素。ted 需非空。
 */
void ls_show_ted(struct ls_ted *ted, struct vty *vty, struct json_object *json,
		 bool verbose)
{
	json_object *jted;

	if (json) {
		jted = json_object_new_object();
		json_object_object_add(json, "ted", jted);
		json_object_string_add(jted, "name", ted->name);
		json_object_int_add(jted, "key", ted->key);
		json_object_int_add(jted, "verticesCount",
				    vertices_count(&ted->vertices));
		json_object_int_add(jted, "edgesCount",
				    edges_count(&ted->edges));
		json_object_int_add(jted, "subnetsCount",
				    subnets_count(&ted->subnets));
		ls_show_vertices(ted, NULL, jted, verbose);
		ls_show_edges(ted, NULL, jted, verbose);
		ls_show_subnets(ted, NULL, jted, verbose);
		return;
	}

	if (vty) {
		vty_out(vty,
			"\n\tTraffic Engineering Database: %s (key: %d)\n\n",
			ted->name, ted->key);
		ls_show_vertices(ted, vty, NULL, verbose);
		ls_show_edges(ted, vty, NULL, verbose);
		ls_show_subnets(ted, vty, NULL, verbose);
		vty_out(vty,
			"\n\tTotal: %zu Vertices, %zu Edges, %zu Subnets\n\n",
			vertices_count(&ted->vertices),
			edges_count(&ted->edges), subnets_count(&ted->subnets));
	}
}

/*
 * 以调试日志(zlog_debug)导出整个 TED 的内部结构：逐个顶点打印其入边与
 * 出边，再逐个边打印源与目的顶点，最后逐个子网打印所属顶点。仅用于排
 * 障，输出到日志而非 vty。ted 需非空。
 */
void ls_dump_ted(struct ls_ted *ted)
{
	struct ls_vertex *vertex;
	struct ls_edge *edge;
	struct ls_subnet *subnet;
	const struct in_addr inaddr_any = {.s_addr = INADDR_ANY};

	zlog_debug("(%s) Ted init", __func__);

	/* Loop TED, start printing Node, then Attributes and finally Prefix */
	frr_each (vertices, &ted->vertices, vertex) {
		zlog_debug("    Ted node (%s %pI4 %s)",
			   vertex->node->name[0] ? vertex->node->name
						 : "no name node",
			   &vertex->node->router_id,
			   origin2txt[vertex->node->adv.origin]);
		struct listnode *lst_node;
		struct ls_edge *vertex_edge;

		for (ALL_LIST_ELEMENTS_RO(vertex->incoming_edges, lst_node,
					  vertex_edge)) {
			zlog_debug(
				"        inc edge key:%s attr key:%pI4 loc:(%pI4) rmt:(%pI4)",
				edge_key_to_text(vertex_edge->key),
				&vertex_edge->attributes->adv.id.ip.addr,
				&vertex_edge->attributes->standard.local,
				&vertex_edge->attributes->standard.remote);
		}
		for (ALL_LIST_ELEMENTS_RO(vertex->outgoing_edges, lst_node,
					  vertex_edge)) {
			zlog_debug(
				"        out edge key:%s attr key:%pI4  loc:(%pI4) rmt:(%pI4)",
				edge_key_to_text(vertex_edge->key),
				&vertex_edge->attributes->adv.id.ip.addr,
				&vertex_edge->attributes->standard.local,
				&vertex_edge->attributes->standard.remote);
		}
	}
	frr_each (edges, &ted->edges, edge) {
		zlog_debug("    Ted edge key:%s src:%pI4 dst:%pI4",
			   edge_key_to_text(edge->key),
			   edge->source ? &edge->source->node->router_id
					: &inaddr_any,
			   edge->destination
				   ? &edge->destination->node->router_id
				   : &inaddr_any);
	}
	frr_each (subnets, &ted->subnets, subnet) {
		zlog_debug("    Ted subnet key:%pFX vertex:%pI4",
			   &subnet->ls_pref->pref,
			   &subnet->vertex->node->adv.id.ip.addr);
	}
	zlog_debug("(%s) Ted end", __func__);
}
