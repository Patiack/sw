// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Graph data structure.
 *
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */
/*
 * ============================================================================
 * graph.c —— 通用有向图数据结构实现
 * ============================================================================
 *
 * 本文件实现一个轻量的有向图（无权重）库，供需要图算法的模块使用
 * （例如基于拓扑计算路径的场景）。结点不直接保存邻接关系，而是各自持有
 * 两个向量：to（出边邻接结点）与 from（入边邻接结点），结点挂载在图的
 * nodes 向量上。
 *
 * 核心数据结构（定义见 graph.h）：
 *   - struct graph      一张图，内含 nodes 向量
 *   - struct graph_node 一个结点，含 data（用户数据）、del（析构回调）
 *                       以及 to / from 两个邻接向量
 *
 * 关键约定：
 *   - 边的增删必须成对更新 from->to 与 to->from，保证图的一致性
 *   - 结点删除会自动断开其所有入边与出边
 *   - graph_dfs 采用带 visited 集合的递归遍历，可处理含环图
 *
 * 与其它模块关系：依赖 lib/vector（邻接表）、lib/buffer（DOT 导出），
 * 被 pathd / cspf 相关路径计算使用。
 *
 * 阅读顺序：graph_new / graph_delete_graph -> graph_new_node ->
 *           graph_add_edge / graph_remove_edge -> graph_dfs -> graph_dump_dot。
 * ============================================================================
 */

#include <zebra.h>
#include "graph.h"
#include "memory.h"
#include "buffer.h"

/*
 * GRAPH      - 图结构（struct graph）的内存类型。
 * GRAPH_NODE - 图结点（struct graph_node）的内存类型。
 */
DEFINE_MTYPE_STATIC(LIB, GRAPH, "Graph");
DEFINE_MTYPE_STATIC(LIB, GRAPH_NODE, "Graph Node");
/*
 * graph_new - 创建一张空图
 * 返回：新分配的 struct graph，其 nodes 向量已初始化
 * 副作用：分配内存，需用 graph_delete_graph() 释放。
 */
struct graph *graph_new(void)
{
	struct graph *graph = XCALLOC(MTYPE_GRAPH, sizeof(struct graph));
	graph->nodes = vector_init(VECTOR_MIN_SIZE);

	return graph;
}

/*
 * graph_delete_graph - 销毁整张图
 * @graph 待销毁的图
 * 副作用：逆序删除所有结点（连带其边与 del 回调），再释放 nodes 向量与图结构。
 */
void graph_delete_graph(struct graph *graph)
{
	for (unsigned int i = vector_active(graph->nodes); i--; /**/)
		graph_delete_node(graph, vector_slot(graph->nodes, i));

	vector_free(graph->nodes);
	XFREE(MTYPE_GRAPH, graph);
}

/*
 * graph_new_node - 在图里新增一个结点
 * @graph 所属图
 * @data  结点携带的用户数据，可为 NULL
 * @del   可选的用户数据析构回调（删结点时调用），可为 NULL
 * 返回：新结点指针
 * 副作用：分配结点，初始化 to / from 两个邻接向量，加入 graph->nodes。
 */
struct graph_node *graph_new_node(struct graph *graph, void *data,
				  void (*del)(void *))
{
	struct graph_node *node =
		XCALLOC(MTYPE_GRAPH_NODE, sizeof(struct graph_node));

	node->from = vector_init(VECTOR_MIN_SIZE);
	node->to = vector_init(VECTOR_MIN_SIZE);
	node->data = data;
	node->del = del;

	vector_set(graph->nodes, node);

	return node;
}

/*
 * graph_vector_remove - 从向量中删除第 ix 个元素（O(1) 交换尾元素法）
 * @v  目标向量
 * @ix 待删下标（越界则直接返回）
 * 说明：把尾元素搬到 ix 位置再收缩 active，因此不保持原有顺序。
 */
static void graph_vector_remove(vector v, unsigned int ix)
{
	if (ix >= v->active)
		return;

	/* v->active is guaranteed >= 1 because ix can't be lower than 0
	 * and v->active is > ix. */
	v->active--;
	/* if ix == v->active--, we set the item to itself, then to NULL...
	 * still correct, no check necessary. */
	v->index[ix] = v->index[v->active];
	v->index[v->active] = NULL;
}

/*
 * graph_delete_node - 从图中删除一个结点
 * @graph 所属图
 * @node  待删结点（为 NULL 直接返回）
 * 副作用：先断开其他结点指向本结点的所有边，再断本结点发出的所有边，
 *         调用 del 回调释放 data，释放 to / from 向量，从 graph->nodes
 *         移除并释放结点本身。
 * 注意：该结点与所有邻接结点之间的边会被一并清除。
 */
void graph_delete_node(struct graph *graph, struct graph_node *node)
{
	if (!node)
		return;

	// an adjacent node
	struct graph_node *adj;

	// remove all edges from other nodes to us
	for (unsigned int i = vector_active(node->from); i--; /**/) {
		adj = vector_slot(node->from, i);
		graph_remove_edge(adj, node);
	}

	// remove all edges from us to other nodes
	for (unsigned int i = vector_active(node->to); i--; /**/) {
		adj = vector_slot(node->to, i);
		graph_remove_edge(node, adj);
	}

	// if there is a deletion callback, call it
	if (node->del && node->data)
		(*node->del)(node->data);

	// free adjacency lists
	vector_free(node->to);
	vector_free(node->from);

	// remove node from graph->nodes
	for (unsigned int i = vector_active(graph->nodes); i--; /**/)
		if (vector_slot(graph->nodes, i) == node) {
			graph_vector_remove(graph->nodes, i);
			break;
		}

	// free the node itself
	XFREE(MTYPE_GRAPH_NODE, node);
}

/*
 * graph_add_edge - 添加一条有向边 from -> to
 * @from 起点结点
 * @to   终点结点
 * 返回：to 结点
 * 副作用：from->to 与 to->from 两个邻接向量各追加一项。
 * 注意：不查重，重复调用会重复添加同一条边。
 */
struct graph_node *graph_add_edge(struct graph_node *from,
				  struct graph_node *to)
{
	vector_set(from->to, to);
	vector_set(to->from, from);
	return to;
}

/*
 * graph_remove_edge - 删除一条有向边 from -> to
 * @from 起点结点
 * @to   终点结点
 * 副作用：分别从 to->from 与 from->to 向量中删除首个匹配项。
 * 注意：边不存在时无操作；若有重复边，一次只删一条。
 */
void graph_remove_edge(struct graph_node *from, struct graph_node *to)
{
	// remove from from to->from
	for (unsigned int i = vector_active(to->from); i--; /**/)
		if (vector_slot(to->from, i) == from) {
			graph_vector_remove(to->from, i);
			break;
		}
	// remove to from from->to
	for (unsigned int i = vector_active(from->to); i--; /**/)
		if (vector_slot(from->to, i) == to) {
			graph_vector_remove(from->to, i);
			break;
		}
}

/*
 * graph_find_node - 按用户数据指针查找结点
 * @graph 图
 * @data  用户数据指针
 * 返回：data 匹配的结点，否则 NULL
 * 复杂度：线性扫描 graph->nodes。
 */
struct graph_node *graph_find_node(struct graph *graph, void *data)
{
	struct graph_node *g;

	for (unsigned int i = vector_active(graph->nodes); i--; /**/) {
		g = vector_slot(graph->nodes, i);
		if (g->data == data)
			return g;
	}

	return NULL;
}

/*
 * graph_has_edge - 判断 from 到 to 是否存在边
 * @from 起点结点
 * @to   终点结点
 * 返回：存在为 true，否则 false。线性扫描 from->to。
 */
bool graph_has_edge(struct graph_node *from, struct graph_node *to)
{
	for (unsigned int i = vector_active(from->to); i--; /**/)
		if (vector_slot(from->to, i) == to)
			return true;

	return false;
}

/*
 * _graph_dfs - 深度优先遍历的递归实现
 * @graph   图
 * @start   当前结点
 * @visited 已访问集合（向量），用于去重以处理含环图
 * @dfs_cb  每访问一个结点调用的回调
 * @arg     透传给回调的参数
 * 说明：先查 visited 决定是否返回，入栈、回调，再递归所有出边。
 */
static void _graph_dfs(struct graph *graph, struct graph_node *start,
		       vector visited,
		       void (*dfs_cb)(struct graph_node *, void *), void *arg)
{
	/* check that we have not visited this node */
	for (unsigned int i = 0; i < vector_active(visited); i++) {
		if (start == vector_slot(visited, i))
			return;
	}

	/* put this node in visited stack */
	vector_ensure(visited, vector_active(visited));
	vector_set_index(visited, vector_active(visited), start);

	/* callback */
	dfs_cb(start, arg);

	/* recurse into children */
	for (unsigned int i = vector_active(start->to); i--; /**/) {
		struct graph_node *c = vector_slot(start->to, i);

		_graph_dfs(graph, c, visited, dfs_cb, arg);
	}
}

/*
 * graph_dfs - 深度优先遍历入口
 * @graph  图
 * @start  起始结点
 * @dfs_cb 访问回调
 * @arg    回调参数
 * 副作用：临时创建 visited 向量，遍历结束后释放。
 * 注意：未对 start 做 NULL 检查，调用方需保证其有效。
 */
void graph_dfs(struct graph *graph, struct graph_node *start,
	       void (*dfs_cb)(struct graph_node *, void *), void *arg)
{
	vector visited = vector_init(VECTOR_MIN_SIZE);

	_graph_dfs(graph, start, visited, dfs_cb, arg);
	vector_free(visited);
}

/*
 * 以下 graph_dump_dot* 依赖 lib/buffer；clippy（文档生成）构建时不编译，
 * 用 BUILDING_CLIPPY 宏排除整个区块。
 */
#ifndef BUILDING_CLIPPY

/*
 * graph_dump_dot_default_print_cb - graph_dump_dot 的默认打印回调
 * @gn  当前结点
 * @buf 输出缓冲
 * 作用：把 gn 的每条出边按 Graphviz DOT 语法写入 buf（形如 n<指针> -> n<指针>;）。
 */
void graph_dump_dot_default_print_cb(struct graph_node *gn, struct buffer *buf)
{
	char nbuf[64];

	for (unsigned int i = 0; i < vector_active(gn->to); i++) {
		struct graph_node *adj = vector_slot(gn->to, i);

		snprintf(nbuf, sizeof(nbuf), "    n%p -> n%p;\n", gn, adj);
		buffer_putstr(buf, nbuf);
	}
}

/*
 * graph_dump_dot - 从 start 出发把图导出为 Graphviz DOT 文本
 * @graph 图
 * @start 遍历起点
 * @pcb   可选的打印回调，为 NULL 时使用默认回调
 * 返回：以字符串形式返回 DOT 文本（调用方负责 free）
 * 副作用：内部创建并释放 buffer；通过 graph_dfs 完成遍历。
 */
char *graph_dump_dot(struct graph *graph, struct graph_node *start,
		     void (*pcb)(struct graph_node *, struct buffer *))
{
	struct buffer *buf = buffer_new(0);
	char *ret;

	pcb = (pcb) ? pcb : graph_dump_dot_default_print_cb;
	buffer_putstr(buf, "digraph {\n");

	graph_dfs(graph, start, (void (*)(struct graph_node *, void *))pcb,
		  buf);

	buffer_putstr(buf, "}\n");

	ret = buffer_getstr(buf);
	buffer_free(buf);

	return ret;
}

#endif /* BUILDING_CLIPPY */
