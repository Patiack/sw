// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CLI graph handling
 *
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 * Copyright (C) 1997, 98, 99 Kunihiro Ishiguro
 * Copyright (C) 2013 by Open Source Routing.
 * Copyright (C) 2013 by Internet Systems Consortium, Inc. ("ISC")
 */

/*
 * 【文件导读】command_graph.c —— 把命令字符串编译成命令图（token 有向图）
 *
 * 本文件实现 command_graph.h 声明的接口。所谓“编译”，就是把 cmd_element.string
 * 里的一串词（如 "show ip route A.B.C.D"）翻译成 graph.h 里那种通用有向图：
 *
 *   - 每个词变成一个 cmd_token，按词性判定 type 与 text；
 *   - 词序连成一条链，两端分别是 START_TKN 与 END_TKN；
 *   - 命令串里的可选分支 <a|b|...> 被展开成：一个 FORK_TKN 分叉出若干支，
 *     各支再汇入一个 JOIN_TKN，FORK 与 JOIN 通过 forkjoin 字段互相指认；
 *   - 数条命令共享前缀时，用 cmd_graph_merge 把它们并进同一张图，
 *     公共前缀只保留一份（节点引用计数 refcnt 记录被几条命令用到）。
 *
 * 图的“相等 / 合并”判定集中在 cmd_nodes_equal（深比较，FORK 会比整个子图）
 * 与 cmd_merge_nodes（递归合并，遇 END_TKN 表示新命令更短 / 更长而做增删）。
 * cmd_loopstop 用来在遍历时避开回边，避免沿可重复的边无限递归。
 *
 * varname 相关函数（cmd_token_varname_*）为可变化 token 起名字，供补全与
 * clippy 的具名参数使用；命名优先级由 varname_src 控制。
 *
 * 易踩的坑：
 *   - FORK / JOIN / START / END 是结构 token，不消耗用户输入，见 IS_VARYING_TOKEN；
 *   - cmd_graph_merge 里大量直接操作 graph 内部邻接表，改动需同时维护 refcnt；
 *   - 文件尾部 DOT 导出在 BUILDING_CLIPPY 构建下被整段裁掉。
 */

#include <zebra.h>

#include "command_graph.h"

/*
 * 内存类型（MTYPE）声明：token 本体、显示文本、帮助、参数、变量名分别记账，
 * 便于 show memory 时观察命令树各部分的开销。
 */
DEFINE_MTYPE_STATIC(LIB, CMD_TOKENS, "Command Tokens");
DEFINE_MTYPE_STATIC(LIB, CMD_DESC, "Command Token Text");
DEFINE_MTYPE_STATIC(LIB, CMD_TEXT, "Command Token Help");
DEFINE_MTYPE(LIB, CMD_ARG, "Command Argument");
DEFINE_MTYPE_STATIC(LIB, CMD_VAR, "Command Argument Name");

/*
 * cmd_token_new —— 分配并初始化一个 cmd_token。
 * 入参：type 词性、attr 属性位、text 显示文本、desc 帮助文本（后两者可为 NULL）。
 * 返回：refcnt = 1、arg = NULL、allowrepeat = false 的新 token。
 * 注意：text / desc 会被复制一份，调用方传入的字符串无需长期存活。
 */
struct cmd_token *cmd_token_new(enum cmd_token_type type, uint8_t attr,
				const char *text, const char *desc)
{
	struct cmd_token *token =
		XCALLOC(MTYPE_CMD_TOKENS, sizeof(struct cmd_token));
	token->type = type;
	token->attr = attr;
	token->text = text ? XSTRDUP(MTYPE_CMD_TEXT, text) : NULL;
	token->desc = desc ? XSTRDUP(MTYPE_CMD_DESC, desc) : NULL;
	token->refcnt = 1;
	token->arg = NULL;
	token->allowrepeat = false;
	token->varname = NULL;

	return token;
}

/*
 * cmd_token_del —— 释放一个 cmd_token 及其附属字符串。
 * 入参 token 可为 NULL（此时直接返回）。
 * 注意：本函数不做引用计数判断，直接释放全部字段与结构体。
 */
void cmd_token_del(struct cmd_token *token)
{
	if (!token)
		return;

	XFREE(MTYPE_CMD_TEXT, token->text);
	XFREE(MTYPE_CMD_DESC, token->desc);
	XFREE(MTYPE_CMD_ARG, token->arg);
	XFREE(MTYPE_CMD_VAR, token->varname);

	XFREE(MTYPE_CMD_TOKENS, token);
}

/*
 * cmd_token_dup —— 深拷贝一个 token（含 min / max、text、desc、arg、varname）。
 * 用于命令图合并时在旧图上新增一份等价结点。返回新分配的副本，调用方负责释放。
 */
struct cmd_token *cmd_token_dup(struct cmd_token *token)
{
	struct cmd_token *copy =
		cmd_token_new(token->type, token->attr, NULL, NULL);
	copy->max = token->max;
	copy->min = token->min;
	copy->text = token->text ? XSTRDUP(MTYPE_CMD_TEXT, token->text) : NULL;
	copy->desc = token->desc ? XSTRDUP(MTYPE_CMD_DESC, token->desc) : NULL;
	copy->arg = token->arg ? XSTRDUP(MTYPE_CMD_ARG, token->arg) : NULL;
	copy->varname =
		token->varname ? XSTRDUP(MTYPE_CMD_VAR, token->varname) : NULL;

	return copy;
}

/*
 * cmd_token_varname_do —— 真正写入 token->varname 的内部实现。
 * 若已有更高优先级的命名（token->varname_src >= varname_src）则不覆盖，
 * 从而保证显式命名优先于文本 / 自动命名。
 * 写入时把 - + * : 等符号替换成下划线、并统一转小写，使其可安全用作标识符。
 */
static void cmd_token_varname_do(struct cmd_token *token, const char *varname,
				 uint8_t varname_src)
{
	if (token->varname_src >= varname_src)
		return;

	XFREE(MTYPE_CMD_VAR, token->varname);

	size_t len = strlen(varname), i;
	token->varname = XMALLOC(MTYPE_CMD_VAR, len + 1);
	token->varname_src = varname_src;

	for (i = 0; i < len; i++)
		switch (varname[i]) {
		case '-':
		case '+':
		case '*':
		case ':':
			token->varname[i] = '_';
			break;
		default:
			token->varname[i] = tolower((unsigned char)varname[i]);
		}
	token->varname[len] = '\0';
}

/*
 * cmd_token_varname_set —— 为 token 设置变量名。
 * 传入 varname 时按显式来源（VARNAME_EXPLICIT）设置；未传时，若 token 是可变化
 * 类型且文本不是 WORD / NAME，则用其文本作名字（VARNAME_TEXT）。
 */
void cmd_token_varname_set(struct cmd_token *token, const char *varname)
{
	if (varname) {
		cmd_token_varname_do(token, varname, VARNAME_EXPLICIT);
		return;
	}
	if (token->type == VARIABLE_TKN) {
		if (strcmp(token->text, "WORD") && strcmp(token->text, "NAME"))
			cmd_token_varname_do(token, token->text, VARNAME_TEXT);
	}
}

/*
 * cmd_token_varname_fork —— 分支命名的递归辅助。
 * 从 FORK 结点出发，为各分支上尚未命名的可变化 token 用（FORK 之前的）前驱词
 * 文本命名；遇嵌套 FORK 继续下钻，保证 <...|...> 内每个占位符都有名字。
 */
static void cmd_token_varname_fork(struct graph_node *node,
				   struct cmd_token *prevtoken)
{
	for (size_t i = 0; i < vector_active(node->to); i++) {
		struct graph_node *next = vector_slot(node->to, i);
		struct cmd_token *nexttoken = next->data;

		if (nexttoken->type == FORK_TKN) {
			cmd_token_varname_fork(next, prevtoken);
			continue;
		}
		if (nexttoken->varname)
			continue;
		if (!IS_VARYING_TOKEN(nexttoken->type))
			continue;

		cmd_token_varname_do(nexttoken, prevtoken->text, VARNAME_TEXT);
	}
}

/*
 * cmd_token_varname_join —— 从 JOIN 结点反向（沿 from 边）回溯命名。
 * 把 varname 传给 JOIN 之前的、非结构性的 token（type < SPECIAL_TKN），
 * 遇嵌套 JOIN 递归处理。用于让分支汇合点的变量名向前传播。
 */
void cmd_token_varname_join(struct graph_node *join, const char *varname)
{
	if (!varname)
		return;

	for (size_t i = 0; i < vector_active(join->from); i++) {
		struct graph_node *prev = vector_slot(join->from, i);
		struct cmd_token *token = prev->data;

		if (token->type == JOIN_TKN)
			cmd_token_varname_join(prev, varname);
		else if (token->type < SPECIAL_TKN)
			cmd_token_varname_do(token, varname, VARNAME_EXPLICIT);
	}
}

/*
 * cmd_token_varname_seqappend —— 按“顺序追加”规则给 token 起名。
 * 先跳过不消耗输入的结构 token（FORK 等）；若恰有一个前驱且它是 WORD，
 * 就用该词命名；若当前是 FORK 则转调 cmd_token_varname_fork 给整块分支命名。
 * 自身是 WORD、或前驱不唯一 / 非 WORD 的情况直接放弃命名。
 */
void cmd_token_varname_seqappend(struct graph_node *node)
{
	struct graph_node *prevnode = node;
	struct cmd_token *token = node->data;
	struct cmd_token *prevtoken;

	if (token->type == WORD_TKN)
		return;

	do {
		if (vector_active(prevnode->from) != 1)
			return;

		prevnode = vector_slot(prevnode->from, 0);
		prevtoken = prevnode->data;
	} while (prevtoken->type == FORK_TKN);

	if (prevtoken->type != WORD_TKN)
		return;

	if (token->type == FORK_TKN)
		cmd_token_varname_fork(node, prevtoken);
	else
		cmd_token_varname_do(token, prevtoken->text, VARNAME_TEXT);
}

/*
 * cmd_nodes_link —— 判断结点 from 是否有一条直接指向 to 的边（只查一层邻接表）。
 */
static bool cmd_nodes_link(struct graph_node *from, struct graph_node *to)
{
	for (size_t i = 0; i < vector_active(from->to); i++)
		if (vector_slot(from->to, i) == to)
			return true;
	return false;
}

static bool cmd_nodes_equal(struct graph_node *ga, struct graph_node *gb);

/* returns a single node to be excluded as "next" from iteration
 * - for JOIN_TKN, never continue back to the FORK_TKN
 * - in all other cases, don't try the node itself (in case of "...")
 */
/*
 * cmd_loopstop —— 求遍历时的“停止结点”，防止沿回边无限递归。
 * 对 JOIN_TKN 返回它配对的 FORK_TKN；其他情况返回结点自身
 * （这样形如可重复的自环边不会被跟进去）。原英文注释见下方。
 */
static inline struct graph_node *cmd_loopstop(struct graph_node *gn)
{
	struct cmd_token *tok = gn->data;
	if (tok->type == JOIN_TKN)
		return tok->forkjoin;
	else
		return gn;
}

/*
 * cmd_subgraph_equal —— 比较两个 FORK 之后的子图是否等价。
 * 要求出度相同，并逐条边在对方子图中找到等价结点（cmd_nodes_equal 递归），
 * 以 a_join 作为子图边界。用于判定 fork 内部结构是否完全一致。
 * 注意内层循环取的是 vector_slot(gb->to, i)（沿用 i 而非 j 的历史写法），
 * 阅读时需留意其配对逻辑。
 */
static bool cmd_subgraph_equal(struct graph_node *ga, struct graph_node *gb,
			       struct graph_node *a_join)
{
	size_t i, j;
	struct graph_node *a_fork, *b_fork;
	a_fork = cmd_loopstop(ga);
	b_fork = cmd_loopstop(gb);

	if (vector_active(ga->to) != vector_active(gb->to))
		return false;
	for (i = 0; i < vector_active(ga->to); i++) {
		struct graph_node *cga = vector_slot(ga->to, i);

		for (j = 0; j < vector_active(gb->to); j++) {
			struct graph_node *cgb = vector_slot(gb->to, i);

			if (cga == a_fork && cgb != b_fork)
				continue;
			if (cga == a_fork && cgb == b_fork)
				break;

			if (cmd_nodes_equal(cga, cgb)) {
				if (cga == a_join)
					break;
				if (cmd_subgraph_equal(cga, cgb, a_join))
					break;
			}
		}
		if (j == vector_active(gb->to))
			return false;
	}
	return true;
}

/*
 * cmd_nodes_equal —— 两个结点（及其子图）的深比较。
 * 先比类型、allowrepeat、文本（结构 token 不比文本）、自环、varname；
 * RANGE 再比 min / max；FORK 则进一步比 forkjoin 的两条连接关系，并对整个
 * 子图递归比较（cmd_subgraph_equal）。它是合并时判断“能否共用前缀”的核心。
 */
/* deep compare -- for FORK_TKN, the entire subgraph is compared.
 * this is what's needed since we're not currently trying to partially
 * merge subgraphs */
static bool cmd_nodes_equal(struct graph_node *ga, struct graph_node *gb)
{
	struct cmd_token *a = ga->data, *b = gb->data;

	if (a->type != b->type || a->allowrepeat != b->allowrepeat)
		return false;
	if (a->type < SPECIAL_TKN && strcmp(a->text, b->text))
		return false;
	/* one a ..., the other not. */
	if (cmd_nodes_link(ga, ga) != cmd_nodes_link(gb, gb))
		return false;
	if (!a->varname != !b->varname)
		return false;
	if (a->varname && strcmp(a->varname, b->varname))
		return false;

	switch (a->type) {
	case RANGE_TKN:
		return a->min == b->min && a->max == b->max;

	case FORK_TKN:
		/* one is keywords, the other just option or selector ... */
		if (cmd_nodes_link(a->forkjoin, ga)
		    != cmd_nodes_link(b->forkjoin, gb))
			return false;
		if (cmd_nodes_link(ga, a->forkjoin)
		    != cmd_nodes_link(gb, b->forkjoin))
			return false;
		return cmd_subgraph_equal(ga, gb, a->forkjoin);

	case VARIABLE_TKN:
	case IPV4_TKN:
	case IPV4_PREFIX_TKN:
	case IPV6_PREFIX_TKN:
	case IPV6_TKN:
	case MAC_TKN:
	case MAC_PREFIX_TKN:
	case JOIN_TKN:
	case START_TKN:
	case END_TKN:
	case NEG_ONLY_TKN:
	case WORD_TKN:
	case ASNUM_TKN:
#ifdef BUILDING_CLIPPY
	case CMD_ELEMENT_TKN:
#endif
		return true;
	}

	assert(!"Reached end of function we should never hit");
	return false;
}

/*
 * cmd_fork_bump_attr —— 把属性位 attr 下推到 FORK 分支内的所有结点。
 * 合并两条命令时取二者属性的较小值（更保守）传播，保证 HIDDEN 等属性在整个
 * 共享子图上都生效。stop / join 作为边界不再继续下探。
 */
static void cmd_fork_bump_attr(struct graph_node *gn, struct graph_node *join,
			       uint8_t attr)
{
	size_t i;
	struct cmd_token *tok = gn->data;
	struct graph_node *stop = cmd_loopstop(gn);

	tok->attr = attr;
	for (i = 0; i < vector_active(gn->to); i++) {
		struct graph_node *next = vector_slot(gn->to, i);
		if (next == stop || next == join)
			continue;
		cmd_fork_bump_attr(next, join, attr);
	}
}

/* move an entire subtree from the temporary graph resulting from
 * parse() into the permanent graph for the command node.
 *
 * this touches rather deeply into the graph code unfortunately.
 */
/*
 * cmd_reparent_tree —— 把一棵子树从 fromgraph 迁到 tograph。
 * 合并命令图时，new 里“旧图没有”的那部分子树会被整体搬进旧图。
 * 遍历较为激进、可能重复访问同一结点，因此仅在旧图中确实存在该结点时才重挂。
 */
static void cmd_reparent_tree(struct graph *fromgraph, struct graph *tograph,
			      struct graph_node *node)
{
	struct graph_node *stop = cmd_loopstop(node);
	size_t i;

	for (i = 0; i < vector_active(fromgraph->nodes); i++)
		if (vector_slot(fromgraph->nodes, i) == node) {
			/* agressive iteration punching through subgraphs - may
			 * hit some
			 * nodes twice.  reparent only if found on old graph */
			vector_unset(fromgraph->nodes, i);
			vector_set(tograph->nodes, node);
			break;
		}

	for (i = 0; i < vector_active(node->to); i++) {
		struct graph_node *next = vector_slot(node->to, i);
		if (next != stop)
			cmd_reparent_tree(fromgraph, tograph, next);
	}
}

/*
 * cmd_free_recur —— 递归释放从 node 起、到 stop 为止的子树。
 * 对 JOIN 结点会先沿其 forkjoin 找到配对的一半一并释放，避免留下悬空结点。
 */
static void cmd_free_recur(struct graph *graph, struct graph_node *node,
			   struct graph_node *stop)
{
	struct graph_node *next, *nstop;

	for (size_t i = vector_active(node->to); i; i--) {
		next = vector_slot(node->to, i - 1);
		if (next == stop)
			continue;
		nstop = cmd_loopstop(next);
		if (nstop != next)
			cmd_free_recur(graph, next, nstop);
		cmd_free_recur(graph, nstop, stop);
	}
	graph_delete_node(graph, node);
}

/*
 * cmd_free_node —— 释放单个结点；若它是 JOIN，则先递归释放其整块子图。
 */
static void cmd_free_node(struct graph *graph, struct graph_node *node)
{
	struct cmd_token *tok = node->data;
	if (tok->type == JOIN_TKN)
		cmd_free_recur(graph, tok->forkjoin, node);
	graph_delete_node(graph, node);
}

/*
 * cmd_merge_nodes —— 命令图合并的递归实现（old 与 new 处于等价位置）。
 * direction = 1 表示安装：把 new 合进 old，多出的分支挂到 old；
 * direction = -1 表示卸载：反向操作，删除 old 中多余的结点。
 * 步骤：结点引用计数按 direction 增减；对 new 的每条后继，在 old 中找等价后继：
 *   找到     -> 递归合并；若该后继是 END_TKN，则按方向增 / 删终点；
 *   没找到且 direction 为 1 -> 把该子树从 new 整体重挂到 old。
 * 末尾若引用计数归零则释放 old 结点。
 */
/* recursive graph merge.  call with
 *   old ~= new
 * (which holds true for old == START_TKN, new == START_TKN)
 */
static void cmd_merge_nodes(struct graph *oldgraph, struct graph *newgraph,
			    struct graph_node *old, struct graph_node *new,
			    int direction)
{
	struct cmd_token *tok;
	struct graph_node *old_skip, *new_skip;
	old_skip = cmd_loopstop(old);
	new_skip = cmd_loopstop(new);

	assert(direction == 1 || direction == -1);

	tok = old->data;
	tok->refcnt += direction;

	size_t j, i;
	for (j = 0; j < vector_active(new->to); j++) {
		struct graph_node *cnew = vector_slot(new->to, j);
		if (cnew == new_skip)
			continue;

		for (i = 0; i < vector_active(old->to); i++) {
			struct graph_node *cold = vector_slot(old->to, i);
			if (cold == old_skip)
				continue;

			if (cmd_nodes_equal(cold, cnew)) {
				struct cmd_token *told = cold->data,
						 *tnew = cnew->data;

				if (told->type == END_TKN) {
					if (direction < 0) {
						graph_delete_node(
							oldgraph,
							vector_slot(cold->to,
								    0));
						graph_delete_node(oldgraph,
								  cold);
					} else
						/* force no-match handling to
						 * install END_TKN */
						i = vector_active(old->to);
					break;
				}

				/* the entire fork compared as equal, we
				 * continue after it. */
				if (told->type == FORK_TKN) {
					if (tnew->attr < told->attr
					    && direction > 0)
						cmd_fork_bump_attr(
							cold, told->forkjoin,
							tnew->attr);
					/* XXX: no reverse bump on uninstall */
					told = (cold = told->forkjoin)->data;
					tnew = (cnew = tnew->forkjoin)->data;
				}
				if (tnew->attr < told->attr)
					told->attr = tnew->attr;

				cmd_merge_nodes(oldgraph, newgraph, cold, cnew,
						direction);
				break;
			}
		}
		/* nothing found => add new to old */
		if (i == vector_active(old->to) && direction > 0) {
			graph_remove_edge(new, cnew);

			cmd_reparent_tree(newgraph, oldgraph, cnew);

			graph_add_edge(old, cnew);
		}
	}

	if (!tok->refcnt)
		cmd_free_node(oldgraph, old);
}

/*
 * cmd_graph_merge —— 命令图合并入口：从各自第一个结点（START_TKN）开始递归合并。
 * direction 语义见 cmd_merge_nodes。
 */
void cmd_graph_merge(struct graph *old, struct graph *new, int direction)
{
	assert(vector_active(old->nodes) >= 1);
	assert(vector_active(new->nodes) >= 1);

	cmd_merge_nodes(old, new, vector_slot(old->nodes, 0),
			vector_slot(new->nodes, 0), direction);
}

/*
 * cmd_graph_names —— 给图开头的可选 “no” 生成变量名。
 * 识别形如 START -> FORK[ 空 | "no" ] 的结构，把其中的 "no" 词命名为 "no"
 * （VARNAME_AUTO），便于 “no ...” 命令的处理与补全。
 */
void cmd_graph_names(struct graph *graph)
{
	struct graph_node *start;

	assert(vector_active(graph->nodes) >= 1);
	start = vector_slot(graph->nodes, 0);

	/* apply varname on initial "[no]" */
	do {
		if (vector_active(start->to) != 1)
			break;

		struct graph_node *first = vector_slot(start->to, 0);
		struct cmd_token *tok = first->data;
		/* looking for an option with 2 choices, nothing or "no" */
		if (tok->type != FORK_TKN || vector_active(first->to) != 2)
			break;

		struct graph_node *next0 = vector_slot(first->to, 0);
		struct graph_node *next1 = vector_slot(first->to, 1);
		/* one needs to be empty */
		if (next0 != tok->forkjoin && next1 != tok->forkjoin)
			break;

		struct cmd_token *tok0 = next0->data;
		struct cmd_token *tok1 = next1->data;
		/* the other one needs to be "no" (only one will match here) */
		if ((tok0->type == WORD_TKN && !strcmp(tok0->text, "no")))
			cmd_token_varname_do(tok0, "no", VARNAME_AUTO);
		if ((tok1->type == WORD_TKN && !strcmp(tok1->text, "no")))
			cmd_token_varname_do(tok1, "no", VARNAME_AUTO);
	} while (0);
}

/*
 * 以下 DOT 导出代码只在非 clippy 构建下编译；clippy 通过 Python 直接读图，不需要它。
 */
#ifndef BUILDING_CLIPPY

#include "command.h"
#include "log.h"

/*
 * cmd_graph_node_print_cb —— DOT 导出时单个结点的格式化回调（调试用）。
 * 按 token 类型着色、标注属性（(d) 废弃 / (h) 隐藏）、输出文本以及到后继的边。
 * 静态标志 wasend 用来让 END 结点只打印一次（多个前驱共享同一个 end 结点）。
 * 注意 color 在个别类型分支下未赋值，故末尾按其给定初值兜底。
 */
void cmd_graph_node_print_cb(struct graph_node *gn, struct buffer *buf)
{
	static bool wasend;

	char nbuf[512];
	struct cmd_token *tok = gn->data;
	const char *color = NULL;

	if (wasend) {
		wasend = false;
		return;
	}

	if (tok->type == END_TKN) {
		wasend = true;
		return;
	}

	snprintf(nbuf, sizeof(nbuf), "  n%p [ shape=box, label=<", gn);
	buffer_putstr(buf, nbuf);
	snprintf(nbuf, sizeof(nbuf), "<b>%s</b>",
		 lookup_msg(tokennames, tok->type, NULL));
	buffer_putstr(buf, nbuf);
	if (tok->attr & CMD_ATTR_DEPRECATED)
		buffer_putstr(buf, " (d)");
	/* DEPRECATED implies HIDDEN, don't print both */
	else if (tok->attr & CMD_ATTR_HIDDEN)
		buffer_putstr(buf, " (h)");
	if (tok->text) {
		if (tok->type == WORD_TKN)
			snprintf(
				nbuf, sizeof(nbuf),
				"<br/>\"<font color=\"#0055ff\" point-size=\"11\"><b>%s</b></font>\"",
				tok->text);
		else
			snprintf(nbuf, sizeof(nbuf), "<br/>%s", tok->text);
		buffer_putstr(buf, nbuf);
	}

	switch (tok->type) {
	case START_TKN:
		color = "#ccffcc";
		break;
	case FORK_TKN:
		color = "#aaddff";
		break;
	case JOIN_TKN:
		color = "#ddaaff";
		break;
	case NEG_ONLY_TKN:
		color = "#ffddaa";
		break;
	case WORD_TKN:
		color = "#ffffff";
		break;
	case RANGE_TKN:
	case IPV4_TKN:
	case IPV4_PREFIX_TKN:
	case IPV6_TKN:
	case IPV6_PREFIX_TKN:
	case MAC_TKN:
	case MAC_PREFIX_TKN:
	case END_TKN:
	case VARIABLE_TKN:
	case ASNUM_TKN:
		color = "#ffffff";
		break;
	}

	/*
	 * Some compilers have the mistaken belief that we can
	 * get here without initializing color.
	 */
	snprintf(nbuf, sizeof(nbuf),
		 ">, style = filled, fillcolor = \"%s\" ];\n", color);
	buffer_putstr(buf, nbuf);

	for (unsigned int i = 0; i < vector_active(gn->to); i++) {
		struct graph_node *adj = vector_slot(gn->to, i);

		if (((struct cmd_token *)adj->data)->type == END_TKN) {
			snprintf(nbuf, sizeof(nbuf), "  n%p -> end%p;\n", gn,
				 adj);
			buffer_putstr(buf, nbuf);
			snprintf(
				nbuf, sizeof(nbuf),
				"  end%p [ shape=box, label=<end>, style = filled, fillcolor = \"#ffddaa\" ];\n",
				adj);
		} else
			snprintf(nbuf, sizeof(nbuf), "  n%p -> n%p;\n", gn,
				 adj);

		buffer_putstr(buf, nbuf);
	}
}

/*
 * cmd_graph_dump_dot —— 从图的第一个结点开始，把整张命令图导出为 DOT 文本。
 * 返回由底层 graph_dump_dot 分配（MTYPE_TMP）的字符串，调用方负责释放。
 */
char *cmd_graph_dump_dot(struct graph *cmdgraph)
{
	struct graph_node *start = vector_slot(cmdgraph->nodes, 0);

	return graph_dump_dot(cmdgraph, start, cmd_graph_node_print_cb);
}

#endif /* BUILDING_CLIPPY */
