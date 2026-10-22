// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * clippy (CLI preparator in python) wrapper for FRR command_graph
 * Copyright (C) 2016-2017  David Lamparter for NetDEF, Inc.
 */

/* note: this wrapper is intended to be used as build-time helper.  while
 * it should be generally correct and proper, there may be the occasional
 * memory leak or SEGV for things that haven't been well-tested.
 */

/* This file is "exempt" from having
#include "config.h"
 * as the first include statement because Python.h also does environment
 * setup & these trample over each other.
 */

/*
 * 【文件导读】command_py.c —— 命令树(command_graph)的 Python 绑定模块 _clippy
 * =====================================================================
 * 【是什么】这是构建期辅助工具的 CPython C 扩展：把 FRR 的“命令图(command_graph)”
 *   暴露成 Python 对象,供 clippy(用 Python 写的 CLI 预处理 / 命令生成器) 遍历、
 *   改写命令图并经 cmd_graph_merge 合并,最终生成命令表。它不是运行时组件,只作为
 *   构建工具被 Python 侧 import。
 *
 * 【为什么这么绕】CLI 定义的解析与图构造逻辑(词法 / 语法 / 合并)全在 C 里
 *   (command_graph.c),而“怎么把图变成代码”的复杂策略写在 Python 里。本文件就是
 *   两者之间的桥:把 C 指针包成 Python 对象,让 Python 能安全地读字段 / 走边。
 *
 * 【两个 Python 类型】
 *   _clippy.GraphNode : 包装 struct graph_node,提供 next/prev/join/fork 等方法,
 *     以及 type/idx/text/desc/min/max/varname 等只读属性。
 *   _clippy.Graph     : 包装 struct graph,可 len()、下标取值、first()、merge(),
 *     创建方式为 Graph(cmddef, doc, name)(即 tp_new = graph_parse)。
 *
 * 【包装的缓冲与所有权(核心设计,见文中两段英文注释)】
 *   - 节点包装是缓冲的:同一节点多次获取返回同一个 Python 对象(按 idx 缓存在
 *     nodewrappers 里),避免重复构造;
 *   - 节点持有其所属 graph 的引用(防 graph 先被回收);graph 反向不引用节点,
 *     以免循环引用;
 *   - 节点实例只能从 graph 获取,不能直接 new(refuse_new)。
 *
 * 【字段拷贝约定】为便于用 PyMemberDef 直接读取,部分字段(如 allowrepeat、type)
 *   被拷贝进包装对象;其余按需通过 getset 从底层 cmd_token 现取。
 *
 * 【易踩的坑】文件头英文注释自述:这是构建期工具,未充分测试,偶发内存泄漏 / 崩溃
 *   属已知情况。另外本文件被特意豁免“必须先 include config.h”的约定,因为 Python.h
 *   也会做环境初始化,二者会相互干扰。
 *
 * 【阅读顺序】两个包装结构体 -> 类型构造 / 销毁 -> node 属性 getter -> node 方法
 *   -> graph 的序列 / 方法 -> graph_parse 入口 -> 模块初始化 command_py_init。
 * =====================================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Python.h>
#include "structmember.h"
#include <string.h>
#include <stdlib.h>

#include "command_graph.h"
#include "clippy.h"

struct wrap_graph;
/* 【前置声明】graph_to_pyobj*:把 C 的 graph_node 包装成 Python 对象。
 *   graph_to_pyobj     : 先按节点指针在 graph->nodes 中查得 idx,再转调 *_idx。
 *   graph_to_pyobj_idx : 直接按 idx 取 / 建包装(内部缓存路径,见文件后部)。 */
static PyObject *graph_to_pyobj(struct wrap_graph *graph,
				struct graph_node *gn);
static PyObject *graph_to_pyobj_idx(struct wrap_graph *wgraph, size_t i);

/*
 * nodes are wrapped as follows:
 *  - instances can only be acquired from a graph
 *  - the same node will return the same wrapper object (they're buffered
 *    through "idx")
 *  - a reference is held onto the graph
 *  - fields are copied for easy access with PyMemberDef
 */
/* 【Python 对象】_clippy.GraphNode 的实例布局(以 PyObject_HEAD 打头)。
 *   allowrepeat : 从 cmd_token 拷贝来的“是否允许重复”标志(便于直接读)。
 *   type        : 节点类型字符串(如 "WORD_TKN"),由 graph_to_pyobj_idx 判定填充。
 *   node        : 指向底层 struct graph_node(不拥有)。
 *   wgraph      : 所属 Graph 包装(持其引用,防提前回收)。
 *   idx         : 该节点在 graph->nodes 向量中的下标,既是身份标识也是缓存键。 */
struct wrap_graph_node {
	PyObject_HEAD

		bool allowrepeat;
	const char *type;

	struct graph_node *node;
	struct wrap_graph *wgraph;
	size_t idx;
};

/*
 * graphs are wrapped as follows:
 *  - they can only be created by parsing a definition string
 *  - there's a table here for the wrapped nodes (nodewrappers), indexed
 *    by "idx" (corresponds to node's position in graph's table of nodes)
 *  - graphs do NOT hold references to nodes (would be circular)
 */
/* 【Python 对象】_clippy.Graph 的实例布局。
 *   definition      : 创建时传入的命令定义字符串(副本)。
 *   graph           : 底层 struct graph(拥有,释放时 graph_delete_graph)。
 *   n_nodewrappers  : nodewrappers 数组当前长度。
 *   errors          : cmd_graph_parse 返回的错误计数(0 表示无错)。
 *   nodewrappers    : 节点包装对象的缓存数组,下标与 graph->nodes 对齐;
 *                     存的是“借来的”指针(不额外持引用,靠节点自持 graph 引用维持)。
 * 【易踩的坑】graph 不持节点引用以避免循环;节点包装被回收时会把对应槽位置 NULL。 */
struct wrap_graph {
	PyObject_HEAD

		char *definition;
	struct graph *graph;
	size_t n_nodewrappers;
	unsigned long errors;
	struct wrap_graph_node **nodewrappers;
};

/* 【构造函数】禁用直接 new:_clippy.GraphNode / Graph 只能由其它对象创建,
 * 直接实例化会抛 ValueError。用途是保证包装对象始终与真实 C 对象关联。 */
static PyObject *refuse_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyErr_SetString(PyExc_ValueError,
			"cannot create instances of this type");
	return NULL;
}

/* 【辅助宏】用成员名 / 类型生成一个只读 PyMemberDef。
 * 说明:把 name 与其类型名以 # 字符串化,填充偏移(offsetof)与 READONLY;
 *   偏移基址是包装对象 wrap_graph_node。宏名在每处使用后都会 #undef 以便复用。
 * 【易踩的坑】文档串里附带的类型名仅用于显示,不做真实类型校验。 */
#define member(name, type)                                                     \
	{                                                                      \
		(char *)#name, type, offsetof(struct wrap_graph_node, name),   \
			READONLY, (char *)#name " (" #type ")"                 \
	}
/* 【成员表】GraphNode 可直接读取的拷贝式成员:type(T_STRING)、idx(T_ULONG)。
 * 其余属性(attr/text/desc/min/max/varname 等)走 getset_graph_node 现取。 */
static PyMemberDef members_graph_node[] = {
	/* clang-format off */
	member(type, T_STRING),
	member(idx, T_ULONG),
	{},
	/* clang-format on */
};
#undef member

/* 【getset-getter】按字段偏移从底层 cmd_token 读出一个 const char * 并转 Python 字符串。
 * 参数 self : 包装对象;poffset : 字段在 cmd_token 内的偏移(由 member 宏给出)。
 * 返回：PyUnicode 或 None(值为 NULL 时)。
 * 【易踩的坑】忽略 poffset 越界情形;只对只读、偏移正确的字段安全。 */
static PyObject *graph_node_get_str(PyObject *self, void *poffset)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	void *offset = (char *)wrap->node->data + (ptrdiff_t)poffset;
	const char *val = *(const char **)offset;

	if (!val)
		Py_RETURN_NONE;
	return PyUnicode_FromString(val);
}

/* 【getset-getter】按偏移从 cmd_token 读出 bool 并转 Python bool(语义同 graph_node_get_str)。 */
static PyObject *graph_node_get_bool(PyObject *self, void *poffset)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	void *offset = (char *)wrap->node->data + (ptrdiff_t)poffset;
	bool val = *(bool *)offset;

	return PyBool_FromLong(val);
}

/* 【getset-getter】按偏移从 cmd_token 读出 long long 并转 Python int(语义同上)。 */
static PyObject *graph_node_get_ll(PyObject *self, void *poffset)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	void *offset = (char *)wrap->node->data + (ptrdiff_t)poffset;
	long long val = *(long long *)offset;

	return PyLong_FromLongLong(val);
}

/* 【getset-getter】按偏移从 cmd_token 读出 uint8_t 并转 Python int(语义同上)。 */
static PyObject *graph_node_get_u8(PyObject *self, void *poffset)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	void *offset = (char *)wrap->node->data + (ptrdiff_t)poffset;
	uint8_t val = *(uint8_t *)offset;

	return PyLong_FromUnsignedLong(val);
}

/* clang-format off */
/* 【辅助宏】生成一个 PyGetSetDef:属性名 name + getter graph_node_get_<variant>,
 * 偏移取 struct cmd_token 中同名成员的偏移。
 * 说明:与前面生成 memberDef 的 member 宏不同,这里产出的是 getset(动态取值),
 *   故偏移基址是 cmd_token 而非包装对象;宏名用后同样 #undef。 */
#define member(name, variant)                                                  \
	{                                                                      \
		(char *)#name,                                                 \
		graph_node_get_##variant,                                      \
		NULL,                                                          \
		(char *)#name " (" #variant ")",                               \
		(void *)offsetof(struct cmd_token, name),                      \
	}
/* clang-format on */

/* 【getset 表】GraphNode 的动态属性:attr / allowrepeat / varname_src / text /
 * desc / min / max / varname,均由对应 getter 从底层 cmd_token 现取。 */
static PyGetSetDef getset_graph_node[] = {
	/* clang-format off */
	member(attr, u8),
	member(allowrepeat, bool),
	member(varname_src, u8),
	member(text, str),
	member(desc, str),
	member(min, ll),
	member(max, ll),
	member(varname, str),
	{},
	/* clang-format on */
};
#undef member

/*
 * node.next() -- returns list of all "next" nodes.
 * this will include circles if the graph has them.
 */
/* 【方法】node.next():返回本节点所有“出边”指向的节点列表(graph->to 向量)。
 * 参数 self / args(无参)。
 * 返回：GraphNode 列表;若本节点是 CMD_ELEMENT_TKN(命令终点)则返回空列表。
 * 【易踩的坑】图里若有环,列表会包含环上的节点(源码注释已说明);列表按
 *   vector_active 取长度,可能含空洞(槽位为 NULL 的项)。 */
static PyObject *graph_node_next(PyObject *self, PyObject *args)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	PyObject *pylist;

	if (wrap->node->data &&
	    ((struct cmd_token *)wrap->node->data)->type == CMD_ELEMENT_TKN)
		return PyList_New(0);
	pylist = PyList_New(vector_active(wrap->node->to));
	for (size_t i = 0; i < vector_active(wrap->node->to); i++) {
		struct graph_node *gn = vector_slot(wrap->node->to, i);

		PyList_SetItem(pylist, i, graph_to_pyobj(wrap->wgraph, gn));
	}
	return pylist;
};

/* 【方法】node.prev():返回所有“入边”来源节点列表(graph->from 向量)。
 * 若本节点是 START_TKN(起点)则返回空列表。
 * 其余同 graph_node_next(含环风险 / 空洞槽位)。 */
static PyObject *graph_node_prev(PyObject *self, PyObject *args)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	PyObject *pylist;

	if (wrap->node->data &&
	    ((struct cmd_token *)wrap->node->data)->type == START_TKN)
		return PyList_New(0);
	pylist = PyList_New(vector_active(wrap->node->from));
	for (size_t i = 0; i < vector_active(wrap->node->from); i++) {
		struct graph_node *gn = vector_slot(wrap->node->from, i);

		PyList_SetItem(pylist, i, graph_to_pyobj(wrap->wgraph, gn));
	}
	return pylist;
};

/*
 * node.join() -- return FORK's JOIN node or None
 */
/* 【方法】node.join():若本节点是 FORK_TKN(分叉),返回其对应的 JOIN(汇合)节点。
 * 返回：汇合节点包装;若本节点非 FORK、是 END_TKN、或无数据,则返回 None。 */
static PyObject *graph_node_join(PyObject *self, PyObject *args)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	struct cmd_token *tok;

	if (!wrap->node->data
	    || ((struct cmd_token *)wrap->node->data)->type == END_TKN)
		Py_RETURN_NONE;

	tok = wrap->node->data;
	if (tok->type != FORK_TKN)
		Py_RETURN_NONE;

	return graph_to_pyobj(wrap->wgraph, tok->forkjoin);
};

/* 【方法】node.fork():若本节点是 JOIN_TKN(汇合),返回其对应的 FORK(分叉)节点。
 * 返回：分叉节点包装;否则 None。 */
static PyObject *graph_node_fork(PyObject *self, PyObject *args)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)self;
	struct cmd_token *tok;

	if (!wrap->node->data ||
	    ((struct cmd_token *)wrap->node->data)->type == END_TKN)
		Py_RETURN_NONE;

	tok = wrap->node->data;
	if (tok->type != JOIN_TKN)
		Py_RETURN_NONE;

	return graph_to_pyobj(wrap->wgraph, tok->forkjoin);
};

/* 【方法表】GraphNode 暴露给 Python 的方法:next / prev / join / fork。 */
static PyMethodDef methods_graph_node[] = {
	{ "next", graph_node_next, METH_NOARGS, "outbound graph edge list" },
	{ "prev", graph_node_prev, METH_NOARGS, "inbound graph edge list" },
	{ "join", graph_node_join, METH_NOARGS, "outbound join node" },
	{ "fork", graph_node_fork, METH_NOARGS, "inbound fork node" },
	{}
};

/* 【析构】GraphNode 包装对象被回收时调用(tp_free)。
 * 参数 arg : 被回收的包装对象。
 * 副作用：把 graph 的 nodewrappers 中对应槽位置 NULL(解除缓存),
 *   并 Py_DECREF 释放对所属 graph 的引用。
 * 断言：idx 必须在范围内。 */
static void graph_node_wrap_free(void *arg)
{
	struct wrap_graph_node *wrap = arg;

	assert(wrap->idx < wrap->wgraph->n_nodewrappers);
	wrap->wgraph->nodewrappers[wrap->idx] = NULL;
	Py_DECREF(wrap->wgraph);
}

/* 【repr】返回调试用表示,形如 <_clippy.GraphNode 0x.. [idx] TYPE>。 */
static PyObject *repr_graph_node(PyObject *arg)
{
	struct wrap_graph_node *wrap = (struct wrap_graph_node *)arg;

	return PyUnicode_FromFormat("<_clippy.GraphNode %p [%zu] %s>",
				    wrap->node, wrap->idx, wrap->type);
}

/* 【类型对象】_clippy.GraphNode 的 PyTypeObject 定义:
 *   tp_new = refuse_new(禁止直接实例化);
 *   tp_free = graph_node_wrap_free;tp_members / tp_getset / tp_methods 见上;
 *   tp_repr = repr_graph_node。 */
static PyTypeObject typeobj_graph_node = {
	PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_clippy.GraphNode",
	.tp_basicsize = sizeof(struct wrap_graph_node),
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = "struct graph_node *",
	.tp_new = refuse_new,
	.tp_free = graph_node_wrap_free,
	.tp_members = members_graph_node,
	.tp_getset = getset_graph_node,
	.tp_methods = methods_graph_node,
	.tp_repr = repr_graph_node,
};

/* 【包装-按指针】把 graph_node 指针包装成 Python 对象(方法返回值 / 外部入口)。
 * 参数 wgraph, gn。
 * 返回：包装对象;若该节点不在图中则 PyErr 置 ValueError 并返回 NULL。
 * 实现：先在 graph->nodes 里线性查找 gn 得到 idx,再转调 graph_to_pyobj_idx。 */
static PyObject *graph_to_pyobj(struct wrap_graph *wgraph,
				struct graph_node *gn)
{
	size_t i;

	for (i = 0; i < vector_active(wgraph->graph->nodes); i++)
		if (vector_slot(wgraph->graph->nodes, i) == gn)
			break;
	if (i == vector_active(wgraph->graph->nodes)) {
		PyErr_SetString(PyExc_ValueError, "cannot find node in graph");
		return NULL;
	}

	return graph_to_pyobj_idx(wgraph, i);
}

/* 【包装-按下标】(核心)按 idx 取得 / 创建节点包装。
 * 参数 wgraph, i(节点下标)。
 * 返回：包装对象。
 * 逻辑：
 *   1) 若 i 超出已缓存长度,realloc 扩容 nodewrappers 并把新槽清零;
 *   2) 若槽内已有包装,直接 Py_INCREF 复用(保证同一节点同一对象);
 *   3) 否则新建包装,填 idx / wgraph / node,并 Py_INCREF(wgraph) 持有图引用;
 *   4) 依据底层 cmd_token 的 type 判定出 type 字符串(见下方 switch)。
 * 【易踩的坑】realloc 未查返回值;未知 token 类型的 type 会回退为 "???"。 */
static PyObject *graph_to_pyobj_idx(struct wrap_graph *wgraph, size_t i)
{
	struct wrap_graph_node *wrap;
	struct graph_node *gn = vector_slot(wgraph->graph->nodes, i);

	if (i >= wgraph->n_nodewrappers) {
		wgraph->nodewrappers =
			realloc(wgraph->nodewrappers,
				(i + 1) * sizeof(wgraph->nodewrappers[0]));
		memset(wgraph->nodewrappers + wgraph->n_nodewrappers, 0,
		       sizeof(wgraph->nodewrappers[0]) *
			       (i + 1 - wgraph->n_nodewrappers));
		wgraph->n_nodewrappers = i + 1;
	}
	if (wgraph->nodewrappers[i]) {
		PyObject *obj = (PyObject *)wgraph->nodewrappers[i];
		Py_INCREF(obj);
		return obj;
	}

	wrap = (struct wrap_graph_node *)typeobj_graph_node.tp_alloc(
		&typeobj_graph_node, 0);
	if (!wrap)
		return NULL;
	wgraph->nodewrappers[i] = wrap;
	Py_INCREF(wgraph);

	wrap->idx = i;
	wrap->wgraph = wgraph;
	wrap->node = gn;
	wrap->type = "NULL";
	wrap->allowrepeat = false;
	if (gn->data) {
		struct cmd_token *tok = gn->data;
		switch (tok->type) {
/* 【辅助宏】为 switch 生成一个 case:命中 token 类型 x 时把 wrap->type 设为 "x"
 * (字符串化)并 break。宏末尾刻意不加分号,调用处也不加分号。 */
#define item(x)                                                                \
	case x:                                                                \
		wrap->type = #x;                                               \
		break /* no semicolon */

			item(WORD_TKN);	       // words
			item(VARIABLE_TKN);    // almost anything
			item(RANGE_TKN);       // integer range
			item(IPV4_TKN);	       // IPV4 addresses
			item(IPV4_PREFIX_TKN); // IPV4 network prefixes
			item(IPV6_TKN);	       // IPV6 prefixes
			item(IPV6_PREFIX_TKN); // IPV6 network prefixes
			item(MAC_TKN);	       // MAC address
			item(MAC_PREFIX_TKN);  // MAC address with mask
			item(ASNUM_TKN);       // ASNUM

			/* plumbing types */
			item(FORK_TKN);
			item(JOIN_TKN);
			item(START_TKN);
			item(END_TKN);
			item(NEG_ONLY_TKN);
			item(CMD_ELEMENT_TKN);
#undef item
		default:
			wrap->type = "???";
		}
	}

	return (PyObject *)wrap;
}

/* 【辅助宏】生成 Graph 的只读 PyMemberDef(与前面的同名宏同形,
 * 只是偏移基址改为 struct wrap_graph)。宏名用后 #undef。 */
#define member(name, type)                                                     \
	{                                                                      \
		(char *)#name, type, offsetof(struct wrap_graph, name),        \
			READONLY, (char *)#name " (" #type ")"                 \
	}
/* 【成员表】Graph 直接暴露的只读成员:definition(T_STRING)、errors(T_ULONG)。 */
static PyMemberDef members_graph[] = {
	member(definition, T_STRING),
	member(errors, T_ULONG),
	{},
};
#undef member

/* graph.first() - root node */
/* 【方法】graph.first():返回图中的第一个节点(命令起点 START_TKN)的包装。 */
static PyObject *graph_first(PyObject *self, PyObject *args)
{
	struct wrap_graph *gwrap = (struct wrap_graph *)self;
	struct graph_node *gn = vector_slot(gwrap->graph->nodes, 0);
	return graph_to_pyobj(gwrap, gn);
};

/* 【前置声明】graph_merge 的声明(定义在文件后部),供方法表引用。 */
static PyObject *graph_merge(PyObject *self, PyObject *args);

/* 【方法表】Graph 暴露的方法:first / merge。 */
static PyMethodDef methods_graph[] = {
	{ "first", graph_first, METH_NOARGS, "first graph node" },
	{ "merge", graph_merge, METH_VARARGS, "merge graphs" },
	{}
};

/* 【前置声明】graph_parse(tp_new)的声明,供 typeobj_graph 引用。 */
static PyObject *graph_parse(PyTypeObject *type, PyObject *args,
			     PyObject *kwds);

/* 【析构】Graph 包装对象被回收时调用(tp_free)。
 * 参数 arg : 被回收的 Graph 包装。
 * 副作用：graph_delete_graph 释放整张图,再 free 缓存数组与定义串。
 * 注意：不逐个 DECREF 节点包装(节点已各自持图引用,由引用计数自然回收)。 */
static void graph_wrap_free(void *arg)
{
	struct wrap_graph *wgraph = arg;

	graph_delete_graph(wgraph->graph);
	free(wgraph->nodewrappers);
	free(wgraph->definition);
}

/* 【序列-len】返回节点数(vector_active(graph->nodes)),支持 len(Graph)。 */
static Py_ssize_t graph_length(PyObject *self)
{
	struct wrap_graph *gwrap = (struct wrap_graph *)self;

	return vector_active(gwrap->graph->nodes);
}

/* 【序列-下标】支持 graph[i] 取值。
 * 参数 idx(负值不特殊处理)。
 * 返回：第 idx 个节点的包装;越界则抛 IndexError。 */
static PyObject *graph_item(PyObject *self, Py_ssize_t idx)
{
	struct wrap_graph *gwrap = (struct wrap_graph *)self;

	if (idx >= vector_active(gwrap->graph->nodes))
		return PyErr_Format(PyExc_IndexError,
				    "index %zd past graph size %u", idx,
				    vector_active(gwrap->graph->nodes));

	return graph_to_pyobj_idx(gwrap, idx);
}

/* 【序列协议】把 Graph 实现为 Python 序列:sq_length=graph_length, sq_item=graph_item。 */
static PySequenceMethods seq_graph = {
	.sq_length = graph_length,
	.sq_item = graph_item,
};

/* 【类型对象】_clippy.Graph 的 PyTypeObject:
 *   tp_new = graph_parse(即 Graph(cmddef, doc, name));
 *   tp_free = graph_wrap_free;tp_as_sequence = &seq_graph;
 *   tp_members / tp_methods 见上。 */
static PyTypeObject typeobj_graph = {
	PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_clippy.Graph",
	.tp_basicsize = sizeof(struct wrap_graph),
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = "struct graph *",
	.tp_new = graph_parse,
	.tp_free = graph_wrap_free,
	.tp_members = members_graph,
	.tp_methods = methods_graph,
	.tp_as_sequence = &seq_graph,
};

/* 【方法】graph.merge(other):把另一张图并入本图(cmd_graph_merge)。
 * 参数 args : 单个 Graph 对象(用 "O!" 强制类型校验)。
 * 返回：None。
 * 【易踩的坑】原地修改本图 self,other 不变;类型不符会抛 TypeError。 */
static PyObject *graph_merge(PyObject *self, PyObject *args)
{
	PyObject *py_other;
	struct wrap_graph *gwrap = (struct wrap_graph *)self;
	struct wrap_graph *gother;

	if (!PyArg_ParseTuple(args, "O!", &typeobj_graph, &py_other))
		return NULL;

	gother = (struct wrap_graph *)py_other;
	cmd_graph_merge(gwrap->graph, gother->graph, +1);
	Py_RETURN_NONE;
}

/* top call / entrypoint for python code */
/* 【入口】构造 Graph(cmddef, doc=None, name=None)——Python 侧创建图的唯一入口(tp_new)。
 * 参数(关键字)cmddef / doc / name(允许 cmddef 为 None)。
 * 返回：新的 Graph 包装。
 * 逻辑：
 *   1) 新建空图并加一个 START_TKN 起点节点;
 *   2) 若给了 def:构造临时 struct cmd_element,用 cmd_graph_parse 解析成图并记录
 *      errors,cmd_graph_names 补名;把最后一个节点(暂为指向栈上 cmd 的占位)替换成
 *      CMD_ELEMENT_TKN,并 strdup 保存定义串;
 *   3) 否则定义串记作 "NULL"。
 * 【易踩的坑】解析失败不抛异常,而是把错误数存进 gwrap->errors 供 Python 侧检查;
 *   last->data 的替换务必在 cmd 仍是栈对象、尚未失效之前完成。 */
static PyObject *graph_parse(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	const char *def, *doc = NULL, *name = NULL;
	struct wrap_graph *gwrap;
	static const char *const kwnames[] = { "cmddef", "doc", "name", NULL };

	gwrap = (struct wrap_graph *)typeobj_graph.tp_alloc(&typeobj_graph, 0);
	if (!gwrap)
		return NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "z|ss", (char **)kwnames,
					 &def, &doc, &name))
		return NULL;

	struct graph *graph = graph_new();
	struct cmd_token *token = cmd_token_new(START_TKN, 0, NULL, NULL);
	graph_new_node(graph, token, (void (*)(void *)) & cmd_token_del);

	if (def) {
		struct cmd_element cmd = { .string = def, .doc = doc };
		struct graph_node *last;

		gwrap->errors = cmd_graph_parse(graph, &cmd);
		cmd_graph_names(graph);

		last = vector_slot(graph->nodes,
				   vector_active(graph->nodes) - 1);
		assert(last->data == &cmd);

		last->data = cmd_token_new(CMD_ELEMENT_TKN, 0, name, def);
		last->del = (void (*)(void *))cmd_token_del;

		gwrap->definition = strdup(def);
	} else {
		gwrap->definition = strdup("NULL");
	}

	gwrap->graph = graph;
	return (PyObject *)gwrap;
}

/* 【模块方法表】_clippy 模块级方法:parse(clippy_parse,解析一个 C 文件)。 */
static PyMethodDef clippy_methods[] = {
	{"parse", clippy_parse, METH_VARARGS, "Parse a C file"},
	{NULL, NULL, 0, NULL}};

/* 【版本适配】以下按 Python 主版本切换模块创建与 initret 宏:
 *   Py3 分支实现见其下;Py2 分支在 #else 之后。 */
#if PY_MAJOR_VERSION >= 3
/* 【模块定义】(Python3)_clippy 的 PyModuleDef:名字 "_clippy",方法表 clippy_methods,
 * 无 docstring;m_size = -1 表示无 per-module 状态(不支持子解释器)。 */
static struct PyModuleDef pymoddef_clippy = {
	PyModuleDef_HEAD_INIT,
	"_clippy",
	NULL, /* docstring */
	-1,
	clippy_methods,
};
/* 【Py3 宏】modcreate() 用 PyModule_Create 建模块;initret(v) 直接 return v。 */
#define modcreate() PyModule_Create(&pymoddef_clippy)
/* 【Py3 宏】initret:初始化成功直接返回模块对象。 */
#define initret(val) return val;
#else
/* 【Py2 宏】modcreate() 用 Py_InitModule 建模块。 */
#define modcreate() Py_InitModule("_clippy", clippy_methods)
/* 【Py2 宏】initret:模块初始化函数返回 void;失败(v 为空)则 Py_FatalError 终止。 */
#define initret(val)                                                           \
	do {                                                                   \
		if (!val)                                                      \
			Py_FatalError("initialization failure");               \
		return;                                                        \
	} while (0)
#endif

#pragma GCC diagnostic ignored "-Wstrict-aliasing"
/* 【模块初始化】_clippy 的 PyMODINIT_FUNC 初始化函数(Python 导入本模块时调用)。
 * 返回：模块对象(Py3)/ 无(Py2,见 initret)。
 * 逻辑：PyType_Ready 两个类型 -> 创建模块 -> 注入 CMD_ATTR_* 与
 *   CMD_GRAPH_PARSE_* 等整型常量 -> 注册 GraphNode / Graph 两个类 ->
 *   elf_py_init(注册 ELF 相关绑定)。
 * 副作用：任一失败即以 initret(NULL) 终止初始化。
 * 说明：上方 pragma 关闭 -Wstrict-aliasing,因为包装层里大量指针强转。 */
PyMODINIT_FUNC command_py_init(void)
{
	PyObject *pymod;

	if (PyType_Ready(&typeobj_graph_node) < 0)
		initret(NULL);
	if (PyType_Ready(&typeobj_graph) < 0)
		initret(NULL);

	pymod = modcreate();
	if (!pymod)
		initret(NULL);

	if (PyModule_AddIntMacro(pymod, CMD_ATTR_YANG) ||
	    PyModule_AddIntMacro(pymod, CMD_ATTR_HIDDEN) ||
	    PyModule_AddIntMacro(pymod, CMD_ATTR_DEPRECATED) ||
	    PyModule_AddIntMacro(pymod, CMD_ATTR_NOSH) ||
	    PyModule_AddIntMacro(pymod, CMD_GRAPH_PARSE_DOCSTRING_MISSING) ||
	    PyModule_AddIntMacro(pymod, CMD_GRAPH_PARSE_DOCSTRING_EXTRA))
		initret(NULL);

	Py_INCREF(&typeobj_graph_node);
	PyModule_AddObject(pymod, "GraphNode", (PyObject *)&typeobj_graph_node);
	Py_INCREF(&typeobj_graph);
	PyModule_AddObject(pymod, "Graph", (PyObject *)&typeobj_graph);
	if (!elf_py_init(pymod))
		initret(NULL);
	initret(pymod);
}
