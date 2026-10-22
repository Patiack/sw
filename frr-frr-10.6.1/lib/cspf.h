// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Constraints Shortest Path First algorithms definition - cspf.h
 *
 * Author: Olivier Dugeon <olivier.dugeon@orange.com>
 *
 * Copyright (C) 2022 Orange http://www.orange.com
 *
 * This file is part of Free Range Routing (FRR).
 */

#ifndef _FRR_CSPF_H_
#define _FRR_CSPF_H_

#include "typesafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This file defines the different structure used for Path Computation with
 * various constrained. Up to now, standard metric, TE metric, delay and
 * bandwidth constraints are supported.
 * All proposed algorithms used the same principle:
 *  - A pruning function that keeps only links that meet constraints
 *  - A priority Queue that keeps the shortest on-going computed path
 *  - A main loop over all vertices to find the shortest path
 */

/*
 * ============================================================================
 * cspf.h —— 约束最短路径优先（CSPF）算法的数据结构与接口声明
 * ============================================================================
 *
 * 本文件定义 pathd / IGP 计算 MPLS-TE 显式路径所需的数据结构与对外 API，
 * 与 cspf.c 一一对应。
 *
 * 核心类型：
 *   - enum path_status   路径计算状态（失败 / 无源 / 无目的 / 同源同目 …）
 *   - enum path_type     路径类型：RSVP-TE、SR-TE、SRv6-TE
 *   - enum metric_type   度量类型：标准度量、TE 度量、时延
 *   - struct constraints 计算一路径所用的约束集合
 *   - struct c_path      一条候选路径（同时挂在优先队列与 processed 树中）
 *   - struct v_node      已访问顶点记录
 *   - struct cspf        算法工作区（组合以上结构）
 *
 * 三棵辅助红黑树：
 *   - pqueue    NONUNIQ 优先队列，按 weight 排序取最小
 *   - processed 按目的 key 唯一的路径集合
 *   - visited   按 key 唯一的已访问顶点集合
 *
 * 对外接口：cspf_new / cspf_init / cspf_init_v4 / cspf_init_v6 /
 *           cspf_clean / cspf_del / compute_p2p_path / cpath_del
 *
 * 阅读顺序建议：先看 struct constraints 与 struct c_path，再看 struct cspf，
 *               最后看文件末尾的函数声明（实现见 cspf.c）。
 * ============================================================================
 */

/*
 * MAX_COST - 代价 / 权重的最大值，用作“无穷大”上界。
 * 路径初始 weight 与累计 cost 都用它表示尚未确定或不可达。
 */
#define MAX_COST	0xFFFFFFFF

/* Status of the path */
/*
 * enum path_status - 路径计算状态，取值即计算失败原因或进度。
 * FAILED 计算失败；NO_SOURCE 无源顶点；NO_DESTINATION 无目的顶点；
 * SAME_SRC_DST 源目相同；IN_PROGRESS 计算进行中；SUCCESS 成功。
 */
enum path_status {
	FAILED = 0,
	NO_SOURCE,
	NO_DESTINATION,
	SAME_SRC_DST,
	IN_PROGRESS,
	SUCCESS
};
/*
 * enum path_type - 要计算的路径类型：RSVP_TE=1、SR_TE、SRV6_TE。
 * 决定 prune_edge 中是否要求 Adj-SID 等 SR 相关属性。
 */
enum path_type {RSVP_TE = 1, SR_TE, SRV6_TE};
/*
 * enum metric_type - 约束使用的度量类型：CSPF_METRIC=1（IGP 标准度量）、
 * CSPF_TE_METRIC（TE 度量）、CSPF_DELAY（时延）。0 为无效值。
 */
enum metric_type {CSPF_METRIC = 1, CSPF_TE_METRIC, CSPF_DELAY};

/* Constrained metrics structure */
/*
 * struct constraints - 一次约束路径计算的输入约束集合。
 * cost   路径可接受的最大累计代价（超过即剪枝）
 * ctype  度量类型，决定累加哪种 link 属性
 * bw     最小带宽要求（0 表示不约束）
 * cos    服务等级 Class of Service，用于取未预留带宽
 * type   路径类型（RSVP-TE / SR-TE / SRv6-TE）
 * family 地址族 AF_INET 或 AF_INET6
 */
struct constraints {
	uint32_t cost;		/* total cost (metric) of the path */
	enum metric_type ctype;	/* Metric Type: standard, TE or Delay */
	float bw;		/* bandwidth of the path */
	uint8_t cos;		/* Class of Service of the path */
	enum path_type type;	/* RSVP-TE or SR-TE path */
	uint8_t family;		/* AF_INET or AF_INET6 address family */
};

/* Priority Queue for Constrained Path Computation */
/*
 * 优先队列红黑树声明，元素类型为 struct c_path，允许 weight 重复。
 * 具体 DECLARE 见下方 q_cmp 之后。
 */
PREDECL_RBTREE_NONUNIQ(pqueue);

/* Processed Path for Constrained Path Computation */
/*
 * 已处理路径红黑树声明，按目的顶点 key 唯一（每个顶点至多一条最优路径）。
 */
PREDECL_RBTREE_UNIQ(processed);

/* Constrained Path structure */
/*
 * struct c_path - 一条“到某目的顶点”的候选路径。
 * 它同时挂入两棵红黑树：q_itm 用于优先队列按 weight 排序，
 * p_itm 用于 processed 按 dst 去重。
 * weight 是累计代价；edges 是组成该路径的 ls_edge 列表；
 * status 记录该路径当前状态。
 */
struct c_path {
	struct pqueue_item q_itm;    /* entry in the Priority Queue */
	uint32_t weight;             /* Weight to sort path in Priority Queue */
	struct processed_item p_itm; /* entry in the Processed RB Tree */
	uint64_t dst;                /* Destination vertex key of this path */
	struct list *edges;          /* List of Edges that compose this path */
	enum path_status status;     /* status of the computed path */
};

/*
 * q_cmp - 优先队列比较函数：按 weight 升序，权重小者优先弹出。
 */
macro_inline int q_cmp(const struct c_path *p1, const struct c_path *p2)
{
	return numcmp(p1->weight, p2->weight);
}
/*
 * 实例化 pqueue 类型：元素 struct c_path，挂接字段 q_itm，比较函数 q_cmp。
 */
DECLARE_RBTREE_NONUNIQ(pqueue, struct c_path, q_itm, q_cmp);

/*
 * p_cmp - processed 树比较函数：按目的顶点 key（dst）唯一。
 */
macro_inline int p_cmp(const struct c_path *p1, const struct c_path *p2)
{
	return numcmp(p1->dst, p2->dst);
}
/*
 * 实例化 processed 类型：元素 struct c_path，挂接字段 p_itm，比较函数 p_cmp。
 */
DECLARE_RBTREE_UNIQ(processed, struct c_path, p_itm, p_cmp);

/* List of visited node */
/*
 * 已访问顶点红黑树声明（按顶点 key 唯一），用于防止成环。
 */
PREDECL_RBTREE_UNIQ(visited);
/*
 * struct v_node - 已访问顶点记录。
 * key 为顶点 key；vertex 指向对应的 ls_vertex，用于后续取该顶点的出边。
 */
struct v_node {
	struct visited_item item; /* entry in the Processed RB Tree */
	uint64_t key;
	struct ls_vertex *vertex;
};

/*
 * v_cmp - visited 树比较函数：按顶点 key 唯一。
 */
macro_inline int v_cmp(const struct v_node *p1, const struct v_node *p2)
{
	return numcmp(p1->key, p2->key);
}
/*
 * 实例化 visited 类型：元素 struct v_node，挂接字段 item，比较函数 v_cmp。
 */
DECLARE_RBTREE_UNIQ(visited, struct v_node, item, v_cmp);

/* Path Computation algorithms structure */
/*
 * struct cspf - CSPF 算法工作区。
 * pqueue    优先队列，保存待扩展的候选路径
 * processed 已生成的所有候选路径（按 dst 唯一）
 * visited   已确定的顶点集合
 * csts      本次计算的约束
 * path      当前正在扩展的路径
 * pdst      指向目的顶点的路径，用于读取最终结果
 */
struct cspf {
	struct pqueue_head pqueue;       /* Priority Queue */
	struct processed_head processed; /* Paths that have been processed */
	struct visited_head visited;     /* Vertices that have been visited */
	struct constraints csts;         /* Constraints of the path */
	struct c_path *path;             /* Current Computed Path */
	struct c_path *pdst;             /* Computed Path to the destination */
};

/**
 * Create a new CSPF structure. Memory is dynamically allocated.
 *
 * @return	pointer to the new cspf structure
 */
/*
 * cspf_new - 新建一个空的 CSPF 工作区（需用 cspf_del 释放）。
 */
extern struct cspf *cspf_new(void);

/**
 * Initialize CSPF structure prior to compute a constrained path. If CSPF
 * structure is NULL, a new CSPF is dynamically allocated prior to the
 * configuration itself.
 *
 * @param algo	CSPF structure, may be null if a new CSPF must be created
 * @param src	Source vertex of the requested path
 * @param dst	Destination vertex of the requested path
 * @param csts	Constraints of the requested path
 *
 * @return	pointer to the initialized CSPF structure
 */
/*
 * cspf_init - 以源 / 目的顶点和约束初始化 CSPF；algo 为 NULL 时自动新建。
 */
extern struct cspf *cspf_init(struct cspf *algo, const struct ls_vertex *src,
			      const struct ls_vertex *dst,
			      struct constraints *csts);

/**
 * Initialize CSPF structure prior to compute a constrained path. If CSPF
 * structure is NULL, a new CSPF is dynamically allocated prior to the
 * configuration itself. This function starts by searching source and
 * destination vertices from the IPv4 addresses in the provided TED.
 *
 * @param algo	CSPF structure, may be null if a new CSPF must be created
 * @param ted	Traffic Engineering Database
 * @param src	Source IPv4 address of the requested path
 * @param dst	Destination IPv4 address of the requested path
 * @param csts	Constraints of the requested path
 *
 * @return	pointer to the initialized CSPF structure
 */
/*
 * cspf_init_v4 - 在 TED 中按 IPv4 地址定位源 / 目的顶点后初始化 CSPF。
 */
extern struct cspf *cspf_init_v4(struct cspf *algo, struct ls_ted *ted,
				 const struct in_addr src,
				 const struct in_addr dst,
				 struct constraints *csts);

/**
 * Initialize CSPF structure prior to compute a constrained path. If CSPF
 * structure is NULL, a new CSPF is dynamically allocated prior to the
 * configuration itself. This function starts by searching source and
 * destination vertices from the IPv6 addresses in the provided TED.
 *
 * @param algo	CSPF structure, may be null if a new CSPF must be created
 * @param ted	Traffic Engineering Database
 * @param src	Source IPv6 address of the requested path
 * @param dst	Destination IPv6 address of the requested path
 * @param csts	Constraints of the requested path
 *
 * @return	pointer to the initialized CSPF structure
 */
/*
 * cspf_init_v6 - 在 TED 中按 IPv6 地址定位源 / 目的顶点后初始化 CSPF。
 */
extern struct cspf *cspf_init_v6(struct cspf *algo, struct ls_ted *ted,
				 const struct in6_addr src,
				 const struct in6_addr dst,
				 struct constraints *csts);

/**
 * Clean CSPF structure. Reset all internal list and priority queue for latter
 * initialization of the CSPF structure and new path computation.
 *
 * @param algo	CSPF structure
 */
/*
 * cspf_clean - 重置 CSPF 内部状态（清队列与树），以便复用同一工作区。
 */
extern void cspf_clean(struct cspf *algo);

/**
 * Delete CSPF structure, internal list and priority queue.
 *
 * @param algo	CSPF structure
 */
/*
 * cspf_del - 释放 CSPF 工作区及其全部内部结构。
 */
extern void cspf_del(struct cspf *algo);

/**
 * Compute point-to-point constrained path. cspf_init() function must be call
 * prior to call this function.
 *
 * @param algo	CSPF structure
 * @param ted	Traffic Engineering Database
 *
 * @return	Constrained Path with status to indicate computation success
 */
/*
 * compute_p2p_path - 计算点到点约束路径；须先调用 cspf_init*。
 * 返回带状态（SUCCESS / FAILED 等）的 c_path。
 */
extern struct c_path *compute_p2p_path(struct cspf *algo, struct ls_ted *ted);

/*
 * cpath_del - 释放一条约束路径及其边列表。
 */
extern void cpath_del(struct c_path *path);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_CSPF_H_ */
