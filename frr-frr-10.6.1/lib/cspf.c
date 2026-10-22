// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Constraints Shortest Path First algorithms - cspf.c
 *
 * Author: Olivier Dugeon <olivier.dugeon@orange.com>
 *
 * Copyright (C) 2022 Orange http://www.orange.com
 *
 * This file is part of Free Range Routing (FRR).
 */

/*
 * ============================================================================
 * cspf.c —— 约束最短路径优先（Constraint-based SPF）算法实现
 * ============================================================================
 *
 * 本文件在 FRR 中的定位：
 *   为 MPLS-TE（RSVP-TE 以及 SR-TE / SRv6-TE）的显式路径计算提供核心算法。
 *   输入是从 IGP（ISIS / OSPF）经 link-state 模块汇聚而来的 TED（流量工程
 *   数据库，struct ls_ted），输出是一条满足给定约束、端点间的最短路径
 *   （struct c_path，由一串 ls_edge 组成）。
 *
 * 核心数据结构：
 *   - struct cspf        算法工作区：优先队列 + 已处理路径红黑树 + 已访问顶点树
 *   - struct c_path      一条“到某目的顶点的候选路径”，含累计权重与边列表
 *   - struct v_node      已访问顶点，用于防止在图中成环
 *   - struct constraints 约束集合（度量类型、带宽、CoS、地址族、路径类型）
 *
 * 与其它模块的关系：
 *   - 依赖 link_state.c / link_state.h 提供的 ls_ted、ls_vertex、ls_edge、
 *     ls_attributes 等拓扑对象
 *   - 依赖 lib/linklist、lib/table、lib/prefix 等基础库
 *   - 上层（pathd 以及各 IGP 客户端）通过 cspf_new / cspf_init* /
 *     compute_p2p_path 调用本文件
 *
 * 算法总览（三者同源）：
 *   1) prune_edge()        剪枝：剔除不满足约束的边
 *   2) relax_constraints() 松弛：把更优的候选路径压入优先队列
 *   3) compute_p2p_path()  主循环：不断弹出最小代价路径，直到队列为空
 *
 * 阅读顺序建议：
 *   cspf_new -> cspf_init* -> prune_edge -> relax_constraints -> compute_p2p_path
 * ============================================================================
 */

#include <zebra.h>

#include "if.h"
#include "linklist.h"
#include "log.h"
#include "hash.h"
#include "memory.h"
#include "prefix.h"
#include "table.h"
#include "stream.h"
#include "printfrr.h"
#include "link_state.h"
#include "cspf.h"

/* Link State Memory allocation */
/*
 * PCA 即 Path Computation Algorithms，本文件专用的内存类型。
 * DEFINE_MTYPE_STATIC 声明一个仅在本编译单元可见的内存类型，
 * 便于内存统计时区分 CSPF 的分配来源。
 */
DEFINE_MTYPE_STATIC(LIB, PCA, "Path Computation Algorithms");

/**
 * Create new Constrained Path. Memory is dynamically allocated.
 *
 * @param key	Vertex key of the destination of this path
 *
 * @return	Pointer to a new Constrained Path structure
 */
/*
 * cpath_new - 新建一条约束路径（Constrained Path）
 * @key  目的顶点的 key（0 表示非法，返回 NULL）
 * 返回：成功返回新分配的 struct c_path，失败返回 NULL
 * 副作用：从 MTYPE_PCA 分配内存，并 list_new() 创建空的边列表；
 *         初始 weight 设为 MAX_COST，status 设为 IN_PROGRESS。
 * 注意：调用方最终需用 cpath_del() 释放，否则内存泄漏。
 */
static struct c_path *cpath_new(uint64_t key)
{
	struct c_path *path;

	/* Sanity Check */
	if (key == 0)
		return NULL;

	path = XCALLOC(MTYPE_PCA, sizeof(struct c_path));
	path->dst = key;
	path->status = IN_PROGRESS;
	path->edges = list_new();
	path->weight = MAX_COST;

	return path;
}

/**
 * Copy src Constrained Path into dst Constrained Path. A new Constrained Path
 * structure is dynamically allocated if dst is NULL. If src is NULL, the
 * function return the dst disregarding if it is NULL or not.
 *
 * @param dest	Destination Constrained Path structure
 * @param src	Source Constrained Path structure
 *
 * @return	Pointer to the destination Constrained Path structure
 */
/*
 * cpath_copy - 把 src 路径复制到 dest 路径
 * @dest 目标路径，可为 NULL（此时内部新分配一个）
 * @src  源路径，为 NULL 时直接原样返回 dest
 * 返回：指向目标路径的指针
 * 副作用：若 dest 已存在会先释放其旧的 edges 列表；edges 通过 list_dup
 *         深拷贝出一份新的节点列表（列表里的边对象本身不被复制）。
 * 坑：当 src 为 NULL 时不会释放 / 清理 dest，调用方需自行注意。
 */
static struct c_path *cpath_copy(struct c_path *dest, const struct c_path *src)
{
	struct c_path *new_path;

	if (!src)
		return dest;

	if (!dest) {
		new_path = XCALLOC(MTYPE_PCA, sizeof(struct c_path));
	} else {
		new_path = dest;
		if (dest->edges)
			list_delete(&new_path->edges);
	}

	new_path->dst = src->dst;
	new_path->weight = src->weight;
	new_path->edges = list_dup(src->edges);
	new_path->status = src->status;

	return new_path;
}

/**
 * Delete Constrained Path structure. Previous allocated memory is freed.
 *
 * @param path	Constrained Path structure to be deleted
 */
/*
 * cpath_del - 释放约束路径结构及其边列表
 * @path 待释放的路径，为 NULL 时直接返回
 * 副作用：list_delete 释放 edges，再 XFREE 释放结构体本身。
 */
void cpath_del(struct c_path *path)
{
	if (!path)
		return;

	if (path->edges)
		list_delete(&path->edges);

	XFREE(MTYPE_PCA, path);
	path = NULL;
}

/**
 * Replace the list of edges in the next Constrained Path by the list of edges
 * in the current Constrained Path.
 *
 * @param next_path	next Constrained Path structure
 * @param cur_path	current Constrained Path structure
 */
/*
 * cpath_replace - 用 cur_path 的边列表替换 next_path 的边列表
 * @next_path 待更新的路径（其旧 edges 会被释放）
 * @cur_path  提供新边列表的路径
 * 副作用：next_path->edges 被释放后重新用 list_dup 复制 cur_path->edges。
 * 注意：只替换边列表，不改变 next_path 的 weight / dst / status。
 */
static void cpath_replace(struct c_path *next_path, struct c_path *cur_path)
{

	if (next_path->edges)
		list_delete(&next_path->edges);

	next_path->edges = list_dup(cur_path->edges);
}

/**
 * Create a new Visited Node structure from the provided Vertex. Structure is
 * dynamically allocated.
 *
 * @param vertex	Vertex structure
 *
 * @return		Pointer to the new Visited Node structure
 */
/*
 * vnode_new - 由顶点结构创建“已访问节点”记录
 * @vertex 图顶点（ls_vertex），为 NULL 时返回 NULL
 * 返回：新分配的 struct v_node，记录 key 与顶点指针
 * 副作用：从 MTYPE_PCA 分配内存，需用 vnode_del() 释放。
 */
static struct v_node *vnode_new(struct ls_vertex *vertex)
{
	struct v_node *vnode;

	if (!vertex)
		return NULL;

	vnode = XCALLOC(MTYPE_PCA, sizeof(struct v_node));
	vnode->vertex = vertex;
	vnode->key = vertex->key;

	return vnode;
}

/**
 * Delete Visited Node structure. Previous allocated memory is freed.
 *
 * @param vnode		Visited Node structure to be deleted
 */
/*
 * vnode_del - 释放已访问节点结构
 * @vnode 待释放节点，为 NULL 时直接返回
 * 副作用：仅释放 v_node 本身，不触碰其引用的 vertex。
 */
static void vnode_del(struct v_node *vnode)
{
	if (!vnode)
		return;

	XFREE(MTYPE_PCA, vnode);
	vnode = NULL;
}

/**
 * Search Vertex in TED by IPv4 address. The function search vertex by browsing
 * the subnets table. It allows to find not only vertex by router ID, but also
 * vertex by interface IPv4 address.
 *
 * @param ted	Traffic Engineering Database
 * @param ipv4	IPv4 address
 *
 * @return	Vertex if found, NULL otherwise
 */
/*
 * get_vertex_by_ipv4 - 在 TED 中按 IPv4 地址查找顶点
 * @ted  流量工程数据库
 * @ipv4 待查找的 IPv4 地址
 * 返回：找到则返回对应 ls_vertex，否则 NULL
 * 原理：遍历 ted->subnets，用 prefix_same 匹配子网前缀，从而既能按
 *       router-id 也能按接口地址命中顶点。
 */
static struct ls_vertex *get_vertex_by_ipv4(struct ls_ted *ted,
					    struct in_addr ipv4)
{
	struct ls_subnet *subnet;
	struct prefix p;

	p.family = AF_INET;
	p.u.prefix4 = ipv4;

	frr_each (subnets, &ted->subnets, subnet) {
		if (subnet->key.family != AF_INET)
			continue;
		p.prefixlen = subnet->key.prefixlen;
		if (prefix_same(&subnet->key, &p))
			return subnet->vertex;
	}

	return NULL;
}

/**
 * Search Vertex in TED by IPv6 address. The function search vertex by browsing
 * the subnets table. It allows to find not only vertex by router ID, but also
 * vertex by interface IPv6 address.
 *
 * @param ted	Traffic Engineering Database
 * @param ipv6	IPv6 address
 *
 * @return	Vertex if found, NULL otherwise
 */
/*
 * get_vertex_by_ipv6 - 在 TED 中按 IPv6 地址查找顶点
 * @ted  流量工程数据库
 * @ipv6 待查找的 IPv6 地址
 * 返回：找到则返回对应 ls_vertex，否则 NULL
 * 原理：遍历 ted->subnets，用 prefix_cmp==0 匹配 AF_INET6 子网。
 */
static struct ls_vertex *get_vertex_by_ipv6(struct ls_ted *ted,
					    struct in6_addr ipv6)
{
	struct ls_subnet *subnet;
	struct prefix p;

	p.family = AF_INET6;
	p.u.prefix6 = ipv6;

	frr_each (subnets, &ted->subnets, subnet) {
		if (subnet->key.family != AF_INET6)
			continue;
		p.prefixlen = subnet->key.prefixlen;
		if (prefix_cmp(&subnet->key, &p) == 0)
			return subnet->vertex;
	}

	return NULL;
}

/*
 * cspf_new - 申请并初始化一个 CSPF 工作区
 * 返回：新分配的 struct cspf（零初始化）
 * 副作用：初始化 processed / visited 两棵红黑树与 pqueue 优先队列；
 *         path 与 pdst 置为 NULL。
 * 注意：使用完需用 cspf_del() 释放。
 */
struct cspf *cspf_new(void)
{
	struct cspf *algo;

	/* Allocate New CSPF structure */
	algo = XCALLOC(MTYPE_PCA, sizeof(struct cspf));

	/* Initialize RB-Trees */
	processed_init(&algo->processed);
	visited_init(&algo->visited);
	pqueue_init(&algo->pqueue);

	algo->path = NULL;
	algo->pdst = NULL;

	return algo;
}

/*
 * cspf_init - 用源 / 目的顶点与约束初始化 CSPF 工作区
 * @algo CSPF 工作区，为 NULL 时内部新建
 * @src  源顶点（可为 NULL）
 * @dst  目的顶点（可为 NULL）
 * @csts 约束集合（为 NULL 直接返回 NULL）
 * 返回：初始化好的 CSPF 指针
 * 副作用：为 src 建一条 weight=0 的起点路径放入 processed 与 pqueue；
 *         为 dst 建一条目的路径加入 processed；复制约束到 algo->csts。
 * 坑：若 src 为 NULL，则 algo->path 保持为 NULL，后续计算会返回 NO_SOURCE。
 */
struct cspf *cspf_init(struct cspf *algo, const struct ls_vertex *src,
		       const struct ls_vertex *dst, struct constraints *csts)
{
	struct cspf *new_algo;
	struct c_path *psrc;

	if (!csts)
		return NULL;

	if (!algo)
		new_algo = cspf_new();
	else
		new_algo = algo;

	/* Initialize Processed Path and Priority Queue with Src & Dst */
	if (src) {
		psrc = cpath_new(src->key);
		psrc->weight = 0;
		processed_add(&new_algo->processed, psrc);
		pqueue_add(&new_algo->pqueue, psrc);
		new_algo->path = psrc;
	}
	if (dst) {
		new_algo->pdst = cpath_new(dst->key);
		processed_add(&new_algo->processed, new_algo->pdst);
	}

	memcpy(&new_algo->csts, csts, sizeof(struct constraints));

	return new_algo;
}

/*
 * cspf_init_v4 - 按 IPv4 地址初始化 CSPF（先查 TED 定位顶点）
 * @algo CSPF 工作区，可为 NULL
 * @ted  流量工程数据库（为 NULL 则原样返回 algo）
 * @src  源 IPv4 地址
 * @dst  目的 IPv4 地址
 * @csts 约束集合（family 会被强制设为 AF_INET）
 * 返回：初始化好的 CSPF 指针
 * 坑：若地址在 TED 中查不到，对应顶点为 NULL，最终 compute 会报
 *     NO_SOURCE / NO_DESTINATION。
 */
struct cspf *cspf_init_v4(struct cspf *algo, struct ls_ted *ted,
			  const struct in_addr src, const struct in_addr dst,
			  struct constraints *csts)
{
	struct ls_vertex *vsrc;
	struct ls_vertex *vdst;
	struct cspf *new_algo;

	/* Sanity Check */
	if (!ted)
		return algo;

	if (!algo)
		new_algo = cspf_new();
	else
		new_algo = algo;

	/* Got Source and Destination Vertex from TED */
	vsrc = get_vertex_by_ipv4(ted, src);
	vdst = get_vertex_by_ipv4(ted, dst);
	csts->family = AF_INET;

	return cspf_init(new_algo, vsrc, vdst, csts);
}

/*
 * cspf_init_v6 - 按 IPv6 地址初始化 CSPF（先查 TED 定位顶点）
 * @algo CSPF 工作区，可为 NULL
 * @ted  流量工程数据库（为 NULL 则原样返回 algo）
 * @src  源 IPv6 地址
 * @dst  目的 IPv6 地址
 * @csts 约束集合（family 会被强制设为 AF_INET6）
 * 返回：初始化好的 CSPF 指针
 */
struct cspf *cspf_init_v6(struct cspf *algo, struct ls_ted *ted,
			  const struct in6_addr src, const struct in6_addr dst,
			  struct constraints *csts)
{
	struct ls_vertex *vsrc;
	struct ls_vertex *vdst;
	struct cspf *new_algo;

	/* Sanity Check */
	if (!ted)
		return algo;

	if (!algo)
		new_algo = cspf_new();
	else
		new_algo = algo;

	/* Got Source and Destination Vertex from TED */
	vsrc = get_vertex_by_ipv6(ted, src);
	vdst = get_vertex_by_ipv6(ted, dst);
	csts->family = AF_INET6;

	return cspf_init(new_algo, vsrc, vdst, csts);
}

/*
 * cspf_clean - 清空 CSPF 内部状态以便复用
 * @algo CSPF 工作区，为 NULL 直接返回
 * 副作用：清空优先队列、processed 路径树、visited 顶点树，并释放对应
 *         的 c_path / v_node；随后清空约束与 path / pdst 指针。
 * 注意：只重置内容，不释放 cspf 结构本身（红黑树头保留可用）。
 */
void cspf_clean(struct cspf *algo)
{
	struct c_path *path;
	struct v_node *vnode;

	if (!algo)
		return;

	/* Normally, Priority Queue is empty. Clean it in case of. */
	if (pqueue_count(&algo->pqueue)) {
		frr_each_safe (pqueue, &algo->pqueue, path) {
			pqueue_del(&algo->pqueue, path);
		}
	}

	/* Empty Processed Path tree and associated Path */
	if (processed_count(&algo->processed)) {
		frr_each_safe (processed, &algo->processed, path) {
			processed_del(&algo->processed, path);
			if (path == algo->pdst)
				algo->pdst = NULL;
			cpath_del(path);
		}
	}

	/* Empty visited Vertex tree and associated Node */
	if (visited_count(&algo->visited)) {
		frr_each_safe (visited, &algo->visited, vnode) {
			visited_del(&algo->visited, vnode);
			vnode_del(vnode);
		}
	}

	if (algo->pdst)
		cpath_del(algo->pdst);

	memset(&algo->csts, 0, sizeof(struct constraints));
	algo->path = NULL;
	algo->pdst = NULL;
}

/*
 * cspf_del - 彻底销毁 CSPF 工作区
 * @algo 待销毁的 CSPF，为 NULL 直接返回
 * 副作用：先 cspf_clean 清内容，再 fini 三棵树，最后释放结构体本身。
 */
void cspf_del(struct cspf *algo)
{
	if (!algo)
		return;

	/* Empty Priority Queue and Processes Path */
	cspf_clean(algo);

	/* Then, reset Priority Queue, Processed Path and Visited RB-Tree */
	pqueue_fini(&algo->pqueue);
	processed_fini(&algo->processed);
	visited_fini(&algo->visited);

	XFREE(MTYPE_PCA, algo);
	algo = NULL;
}

/**
 * Prune Edge if constraints are not met by testing Edge Attributes against
 * given constraints and cumulative cost of the given constrained path.
 *
 * @param path	On-going Computed Path with cumulative cost constraints
 * @param edge	Edge to be validate against Constraints
 * @param csts	Constraints for this path
 *
 * @return	True if Edge should be prune, false if Edge is valid
 */
/*
 * prune_edge - 判断某条边是否应被剪枝（不满足约束）
 * @path 当前已累计 cost 的候选路径
 * @edge 待校验的边
 * @csts 约束集合
 * 返回：true 表示应剪掉（边不合法或超约束），false 表示边可用
 * 校验内容：边与目的顶点的合法性、地址族与路径类型（SR-TE 要求 Adj-SID）、
 *           度量（standard / TE / delay）的累计值不超 cost、带宽满足
 *           最大带宽与未预留带宽（按 CoS 取）。
 * 坑：任一参数为 NULL 都直接判为剪枝。
 */
static bool prune_edge(const struct c_path *path, const struct ls_edge *edge,
		       const struct constraints *csts)
{
	struct ls_vertex *dst;
	struct ls_attributes *attr;

	/* Check that Path, Edge and Constraints are valid */
	if (!path || !edge || !csts)
		return true;

	/* Check that Edge has a valid destination */
	if (!edge->destination)
		return true;
	dst = edge->destination;

	/* Check that Edge has valid attributes */
	if (!edge->attributes)
		return true;
	attr = edge->attributes;

	/* Check that Edge belongs to the requested Address Family and type */
	if (csts->family == AF_INET) {
		if (IPV4_NET0(attr->standard.local.s_addr))
			return true;
		if (csts->type == SR_TE)
			if (!CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID) ||
			    !CHECK_FLAG(dst->node->flags, LS_NODE_SR))
				return true;
	}
	if (csts->family == AF_INET6) {
		if (IN6_IS_ADDR_UNSPECIFIED(&attr->standard.local6))
			return true;
		if (csts->type == SR_TE)
			if (!CHECK_FLAG(attr->flags, LS_ATTR_ADJ_SID6) ||
			    !CHECK_FLAG(dst->node->flags, LS_NODE_SR))
				return true;
	}

	/*
	 * Check that total cost, up to this edge, respects the initial
	 * constraints
	 */
	switch (csts->ctype) {
	case CSPF_METRIC:
		if (!CHECK_FLAG(attr->flags, LS_ATTR_METRIC))
			return true;
		if ((attr->metric + path->weight) > csts->cost)
			return true;
		break;

	case CSPF_TE_METRIC:
		if (!CHECK_FLAG(attr->flags, LS_ATTR_TE_METRIC))
			return true;
		if ((attr->standard.te_metric + path->weight) > csts->cost)
			return true;
		break;

	case CSPF_DELAY:
		if (!CHECK_FLAG(attr->flags, LS_ATTR_DELAY))
			return true;
		if ((attr->extended.delay + path->weight) > csts->cost)
			return true;
		break;
	}

	/* If specified, check that Edge meet Bandwidth constraint */
	if (csts->bw > 0.0) {
		if (attr->standard.max_bw < csts->bw ||
		    attr->standard.max_rsv_bw < csts->bw ||
		    attr->standard.unrsv_bw[csts->cos] < csts->bw)
			return true;
	}

	/* All is fine. We can consider this Edge valid, so not to be prune */
	return false;
}

/**
 * Relax constraints of the current path up to the destination vertex of the
 * provided Edge. This function progress in the network topology by validating
 * the next vertex on the computed path. If Vertex has not already been visited,
 * list of edges of the current path is augmented with this edge if the new cost
 * is lower than prior path up to this vertex. Current path is re-inserted in
 * the Priority Queue with its new cost i.e. current cost + edge cost.
 *
 * @param algo	CSPF structure
 * @param edge	Next Edge to be added to the current computed path
 *
 * @return	True if current path reach destination, false otherwise
 */
/*
 * relax_constraints - 松弛：尝试经 edge 抵达其目的顶点并更新候选路径
 * @algo CSPF 工作区
 * @edge 从当前路径目的顶点出发的一条边
 * 返回：若新路径目的即最终目的顶点则 true，否则 false
 * 逻辑：跳过已访问顶点以防成环；在 processed 中查该目的顶点的路径，
 *       若“当前 cost + 边 cost”更小则更新其 weight 与边列表，并重新
 *       压入优先队列（先删旧条目再插入以保证堆有序）。
 * 坑：优先队列条目无法原地更新权重，必须先删后插。
 */
static bool relax_constraints(struct cspf *algo, struct ls_edge *edge)
{

	struct c_path pkey = {};
	struct c_path *next_path;
	struct v_node vnode = {};
	uint32_t total_cost = MAX_COST;

	/* Verify that we have a current computed path */
	if (!algo->path)
		return false;

	/* Verify if we have not visited the next Vertex to avoid loop */
	vnode.key = edge->destination->key;
	if (visited_member(&algo->visited, &vnode)) {
		return false;
	}

	/*
	 * Get Next Computed Path from next vertex key
	 * or create a new one if it has not yet computed.
	 */
	pkey.dst = edge->destination->key;
	next_path = processed_find(&algo->processed, &pkey);
	if (!next_path) {
		next_path = cpath_new(pkey.dst);
		processed_add(&algo->processed, next_path);
	}

	/*
	 * Add or update the Computed Path in the Priority Queue if total cost
	 * is lower than cost associated to this next Vertex. This could occurs
	 * if we process a Vertex that as not yet been visited in the Graph
	 * or if we found a shortest path up to this Vertex.
	 */
	switch (algo->csts.ctype) {
	case CSPF_METRIC:
		total_cost = edge->attributes->metric + algo->path->weight;
		break;
	case CSPF_TE_METRIC:
		total_cost = edge->attributes->standard.te_metric +
			     algo->path->weight;
		break;
	case CSPF_DELAY:
		total_cost =
			edge->attributes->extended.delay + algo->path->weight;
		break;
	default:
		break;
	}
	if (total_cost < next_path->weight) {
		/*
		 * It is not possible to directly update the q_path in the
		 * Priority Queue. Indeed, if we modify the path weight, the
		 * Priority Queue must be re-ordered. So, we need fist to remove
		 * the q_path if it is present in the Priority Queue, then,
		 * update the Path, in particular the Weight, and finally
		 * (re-)insert it in the Priority Queue.
		 */
		struct c_path *path;
		frr_each_safe (pqueue, &algo->pqueue, path) {
			if (path->dst == pkey.dst) {
				pqueue_del(&algo->pqueue, path);
				break;
			}
		}
		next_path->weight = total_cost;
		cpath_replace(next_path, algo->path);
		listnode_add(next_path->edges, edge);
		pqueue_add(&algo->pqueue, next_path);
	}

	/* Return True if we reach the destination */
	return (next_path->dst == algo->pdst->dst);
}

/*
 * compute_p2p_path - 计算点到点约束路径（带剪枝的 Dijkstra 变体）
 * @algo CSPF 工作区（须先经 cspf_init* 初始化）
 * @ted  流量工程数据库（用于按 key 找顶点及其出边）
 * 返回：最优路径（struct c_path）；失败时返回一条 status=FAILED 的空路径，
 *       调用方需自行 cpath_del。
 * 状态取值：NO_SOURCE / NO_DESTINATION / SAME_SRC_DST / SUCCESS / FAILED
 * 副作用：结束时调用 cspf_clean() 清空 algo 内部状态。
 * 主循环：不断从优先队列弹出最小权重路径，对其目的顶点打 visited 标记，
 *         再遍历其所有出边依次 prune + relax；若抵达目的顶点且 cost 更优
 *         则更新 optim_path。
 */
struct c_path *compute_p2p_path(struct cspf *algo, struct ls_ted *ted)
{
	struct listnode *node;
	struct ls_vertex *vertex;
	struct ls_edge *edge;
	struct c_path *optim_path;
	struct v_node *vnode;
	uint32_t cur_cost;

	optim_path = cpath_new(0xFFFFFFFFFFFFFFFF);
	optim_path->status = FAILED;

	/* Check that all is correctly initialized */
	if (!algo)
		return optim_path;

	if (!algo->csts.ctype)
		return optim_path;

	if (!algo->pdst) {
		optim_path->status = NO_DESTINATION;
		return optim_path;
	}

	if (!algo->path) {
		optim_path->status = NO_SOURCE;
		return optim_path;
	}

	if (algo->pdst->dst == algo->path->dst) {
		optim_path->status = SAME_SRC_DST;
		return optim_path;
	}

	optim_path->dst = algo->pdst->dst;
	optim_path->status = IN_PROGRESS;

	/*
	 * Process all Connected Vertex until priority queue becomes empty.
	 * Connected Vertices are added into the priority queue when
	 * processing the next Connected Vertex: see relax_constraints()
	 */
	cur_cost = MAX_COST;
	while (pqueue_count(&algo->pqueue) != 0) {
		/* Got shortest current Path from the Priority Queue */
		algo->path = pqueue_pop(&algo->pqueue);

		/* Add destination Vertex of this path to the visited RB Tree */
		vertex = ls_find_vertex_by_key(ted, algo->path->dst);
		if (!vertex)
			continue;
		vnode = vnode_new(vertex);
		visited_add(&algo->visited, vnode);

		/* Process all outgoing links from this Vertex */
		for (ALL_LIST_ELEMENTS_RO(vertex->outgoing_edges, node, edge)) {
			/*
			 * Skip Connected Edges that must be prune i.e.
			 * Edges that not satisfy the given constraints,
			 * in particular the Bandwidth, TE Metric and Delay.
			 */
			if (prune_edge(algo->path, edge, &algo->csts))
				continue;

			/*
			 * Relax constraints and check if we got a shorter
			 * candidate path
			 */
			if (relax_constraints(algo, edge) &&
			    algo->pdst->weight < cur_cost) {
				cur_cost = algo->pdst->weight;
				cpath_copy(optim_path, algo->pdst);
				optim_path->status = SUCCESS;
			}
		}
	}

	/*
	 * The priority queue is empty => all the possible (vertex, path)
	 * elements have been explored. The optim_path contains the optimal
	 * path if it exists. Otherwise an empty path with status failed is
	 * returned.
	 */
	if (optim_path->status == IN_PROGRESS ||
	    listcount(optim_path->edges) == 0)
		optim_path->status = FAILED;
	cspf_clean(algo);

	return optim_path;
}
