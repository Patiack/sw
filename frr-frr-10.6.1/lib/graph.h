// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Graph data structure.
 *
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */

#ifndef _ZEBRA_COMMAND_GRAPH_H
#define _ZEBRA_COMMAND_GRAPH_H

#include <stdbool.h>
#include "vector.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * graph.h —— 通用有向图数据结构的接口声明
 * ============================================================================
 *
 * 与 graph.c 对应：声明一张轻量有向图（无权重）的图 / 结点结构与操作接口。
 *
 * 核心类型：
 *   - struct graph      一张图，只有 nodes 一个向量（保存全部结点）
 *   - struct graph_node 一个结点：from（指向本结点的邻居）、to（本结点指向的
 *                       邻居）、data（用户数据）、del（数据析构回调）
 *
 * 接口一览：graph_new / graph_delete_graph / graph_new_node /
 *           graph_delete_node / graph_add_edge / graph_remove_edge /
 *           graph_find_node / graph_has_edge / graph_dfs / graph_dump_dot
 *
 * 注意：邻接关系分别记录在 from 与 to 两个向量里，增删边需两者同步维护
 *       （实现中已保证）；结点删除会自动清理相关边。
 *
 * 阅读顺序：struct graph / graph_node -> 各函数声明（实现见 graph.c）。
 * ============================================================================
 */

/*
 * struct graph - 一张图，nodes 向量保存图中所有 struct graph_node。
 */
struct graph {
	vector nodes;
};

/*
 * struct graph_node - 图中的一个结点（有向图的顶点）。
 * from 指向“有边指向本结点”的邻居集合；to 指向“本结点指向”的邻居集合。
 * data 为关联的用户数据；del 为删除结点时对 data 调用的析构回调。
 */
struct graph_node {
	vector from; // nodes which have edges to this node
	vector to;   // nodes which this node has edges to

	void *data;		 // node data
	void (*del)(void *data); // deletion callback
};

/*
 * graph_new - 创建一张空图。
 */
struct graph *graph_new(void);

/**
 * Creates a new node.
 *
 * @struct graph the graph this node exists in
 * @param[in] data this node's data
 * @param[in] del data deletion callback
 * @return the new node
 */
/*
 * graph_new_node - 在图中新建一个结点。
 * 返回新结点；data 为用户数据，del 为其析构回调。
 */
struct graph_node *graph_new_node(struct graph *graph, void *data,
				  void (*del)(void *));

/**
 * Deletes a node.
 *
 * Before deletion, this function removes all edges to and from this node from
 * any neighbor nodes.
 *
 * If *data and *del are non-null, the following call is made:
 *   (*node->del) (node->data);
 *
 * @param[in] graph the graph this node belongs to
 * @param[out] node pointer to node to delete
 */
/*
 * graph_delete_node - 删除结点及其全部关联边。
 * 删前会断开所有指向本结点与由本结点发出的边；若 data 与 del 均非空
 * 会调用 del(data)。
 */
void graph_delete_node(struct graph *graph, struct graph_node *node);

/**
 * Makes a directed edge between two nodes.
 *
 * @param[in] from
 * @param[in] to
 * @return to
 */
/*
 * graph_add_edge - 在 from 与 to 之间建立一条有向边，返回 to。
 */
struct graph_node *graph_add_edge(struct graph_node *from,
				  struct graph_node *to);

/**
 * Removes a directed edge between two nodes.
 *
 * @param[in] from
 * @param[in] to
 */
/*
 * graph_remove_edge - 删除 from 到 to 的有向边。
 */
void graph_remove_edge(struct graph_node *from, struct graph_node *to);

/**
 * Deletes a graph.
 * Calls graph_delete_node on each node before freeing the graph struct itself.
 *
 * @param graph the graph to delete
 */
/*
 * graph_delete_graph - 删除整张图；先逐结点 graph_delete_node，再释放图结构。
 */
void graph_delete_graph(struct graph *graph);

/*
 * Finds a node in the graph.
 *
 * @param[in] graph the graph to search in
 * @param[in] data the data to key off
 * @return the first graph node whose data pointer matches `data`
 */
/*
 * graph_find_node - 按数据指针在图中查找结点，返回首个 data 匹配者。
 */
struct graph_node *graph_find_node(struct graph *graph, void *data);


/*
 * Determines whether two nodes have a directed edge between them.
 *
 * @param from
 * @param to
 * @return whether there is a directed edge from `from` to `to`.
 */
/*
 * graph_has_edge - 判断 from 到 to 是否存在有向边。
 */
bool graph_has_edge(struct graph_node *from, struct graph_node *to);

/*
 * Depth-first search.
 *
 * Performs a depth-first traversal of the given graph, visiting each node
 * exactly once and calling the user-provided callback for each visit.
 *
 * @param graph the graph to operate on
 * @param start the node to take as the root
 * @param dfs_cb callback called for each node visited in the traversal
 * @param arg argument to provide to dfs_cb
 */
/*
 * graph_dfs - 深度优先遍历；每个结点仅访问一次并对每次访问调用 dfs_cb。
 */
void graph_dfs(struct graph *graph, struct graph_node *start,
	       void (*dfs_cb)(struct graph_node *, void *), void *arg);

#ifndef BUILDING_CLIPPY
/*
 * Clippy relies on a small subset of sources in lib/, but it cannot link
 * libfrr since clippy itself is required to build libfrr. Instead it directly
 * includes the sources it needs. One of these is the command graph
 * implementation, which wraps this graph implementation. Since we need to use
 * the buffer.[ch] sources here, which indirectly rely on most of libfrr, we
 * have to ignore them when compiling clippy to avoid build dependency issues.
 *
 * TODO: Fix clippy build.
 */

/*
 * Default node printer for use with graph_dump_dot.
 *
 * @param gn the node to print
 * @param buf the buffer to print into
 */
/*
 * graph_dump_dot_default_print_cb - graph_dump_dot 使用的默认结点打印回调。
 */
void graph_dump_dot_default_print_cb(struct graph_node *gn, struct buffer *buf);

/*
 * Prints a graph in the DOT language.
 *
 * The generated output is produced from a depth-first traversal of the graph.
 *
 * @param graph the graph to print
 * @param start the node to take as the root
 * @param pcb callback called for each node in the traversal that should
 *        print the node in the DOT language. Passing NULL for this argument
 *        will use the default printer. See graph_dump_dot_default_print_cb for
 *        an example.
 * @return representation of graph in DOT language, allocated with MTYPE_TMP.
 *         Caller is responsible for freeing this string.
 */
/*
 * graph_dump_dot - 以 DOT 语言输出图（基于深度优先遍历）。
 * pcb 为每个结点的打印回调，传 NULL 使用默认打印器；返回用 MTYPE_TMP
 * 分配的字符串，由调用方负责释放。
 */
char *graph_dump_dot(struct graph *graph, struct graph_node *start,
		     void (*pcb)(struct graph_node *, struct buffer *buf));

#endif /* BUILDING_CLIPPY */

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_COMMAND_GRAPH_H */
