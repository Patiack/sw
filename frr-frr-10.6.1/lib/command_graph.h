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
 * 【文件导读】command_graph.h —— 命令图（command graph）的数据契约
 *
 * 本头文件定义“把一条命令字符串编译成一张 token 有向图”所依赖的类型与接口，
 * 由 command_graph.c 实现。理解 FRR 命令匹配的关键，是分清三层抽象：
 *
 *   cmd_token —— 命令的最小单元。它记录该位置的“类型”（WORD / VARIABLE /
 *                数字区间 / IP 前缀 ...）、显示文本 text、帮助 desc，以及运行时
 *                被用户输入的哪一个词命中（arg）。
 *   graph_node —— 图的一个结点，携带一个 cmd_token；由通用 lib/graph.h 提供。
 *   graph     —— 整条/多条命令合并成的一张有向图，起点是 START_TKN，
 *                终点是 END_TKN。命令串里的可选分支 <a|b> 会被编译成
 *                FORK_TKN / JOIN_TKN 包住的子图（并行分支）。
 *
 * 编译流程（cmd_graph_parse）：把 cmd_element.string 按空格拆成词，逐词判定
 * 类型生成 cmd_token，再把 token 按顺序串成 graph_node 加入 graph。
 *
 * 运行期：command_match.c 从 START 出发沿边做“token 匹配 / 补全候选生成”，
 * 到达 END 且全行消耗完即为完全匹配。
 *
 * 易踩的坑：
 *   - FORK/JOIN 是“配对的隐形 token”，只用于分支结构，不会消耗用户输入；
 *   - SPECIAL_TKN 只是 FORK_TKN 的别名，用于区分“管道类 token”与“可变化 token”；
 *   - IS_VARYING_TOKEN 判断的是“[VARIABLE_TKN, FORK_TKN) 区间”，即真正
 *     可能吃用户输入的 token，START/END/FORK/JOIN 都不在其中。
 */

#ifndef _FRR_COMMAND_GRAPH_H
#define _FRR_COMMAND_GRAPH_H

#include <stdbool.h>
#include <stdint.h>

#include "memory.h"
#include "vector.h"
#include "graph.h"
#include "xref.h"

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_MTYPE(CMD_ARG);

struct vty;

/*
 * enum cmd_token_type —— token 的类型，决定它能匹配/承载什么数据。
 *
 * 前半段是可变化、会消耗用户输入的类型：WORD（字面词）、VARIABLE（任意串）、
 * RANGE（整数区间）、IPV4 / IPV6 及其 PREFIX、MAC 及其 PREFIX、ASNUM（as-dot）。
 * 后半段是“管道/结构”类型：FORK 与 JOIN 标记可选分支子图的开始/结束，
 * START 是本行首 token，END 是本行末 token，NEG_ONLY 只在 “no ...” 命令里匹配。
 * CMD_ELEMENT_TKN 仅 clippy（Python 绑定）构建时存在。
 * SPECIAL_TKN 等于 FORK_TKN，用于把管道类 token 与可变化 token 区分开。
 */
/**
 * Types for tokens.
 *
 * The type determines what kind of data the token can match (in the
 * matching use case) or hold (in the argv use case).
 */
/* clang-format off */
enum cmd_token_type {
	WORD_TKN,        // words
	VARIABLE_TKN,    // almost anything
	RANGE_TKN,       // integer range
	IPV4_TKN,        // IPV4 addresses
	IPV4_PREFIX_TKN, // IPV4 network prefixes
	IPV6_TKN,        // IPV6 prefixes
	IPV6_PREFIX_TKN, // IPV6 network prefixes
	MAC_TKN,         // Ethernet address
	MAC_PREFIX_TKN,  // Ethernet address w/ CIDR mask
	ASNUM_TKN,       // AS dot format

	/* plumbing types */
	FORK_TKN,  // marks subgraph beginning
	JOIN_TKN,  // marks subgraph end
	START_TKN, // first token in line
	END_TKN,   // last token in line
	NEG_ONLY_TKN,    // filter token, match if "no ..." command

#ifdef BUILDING_CLIPPY
	CMD_ELEMENT_TKN, // python bindings only
#endif
	SPECIAL_TKN = FORK_TKN,
};
/* clang-format on */

/* IS_VARYING_TOKEN —— 判断某类型是否为“可变化”token，即真正会消耗/匹配用户输入的那类。 */
#define IS_VARYING_TOKEN(x) ((x) >= VARIABLE_TKN && (x) < FORK_TKN)

/*
 * 命令属性位（cmd_element.attr / cmd_token.attr）。
 * CMD_ATTR_YANG 表示命令由 YANG 模型驱动；HIDDEN 表示不在 “?” 中列出；
 * DEPRECATED 表示即将废弃（隐含 HIDDEN）；NOSH 表示 vtysh 不应转发该命令。
 */
/* Command attributes */
enum {
	CMD_ATTR_YANG = (1 << 0),
	CMD_ATTR_HIDDEN = (1 << 1),
	CMD_ATTR_DEPRECATED = (1 << 2),
	CMD_ATTR_NOSH = (1 << 3),
};

/*
 * enum varname_src —— token 变量名的来源，决定补全/命名时如何处理 varname。
 * VARNAME_NONE 无名字；AUTO 由系统按位置自动生成；VAR、TEXT、EXPLICIT 分别
 * 对应来自变量展开、命令串文本、显式指定的三种来源。clippy 依赖它判断命名策略。
 */
enum varname_src {
	VARNAME_NONE = 0,
	VARNAME_AUTO,
	VARNAME_VAR,
	VARNAME_TEXT,
	VARNAME_EXPLICIT,
};

/*
 * struct cmd_token —— 命令图结点携带的 token 描述。
 *
 * type 决定匹配语义；attr 是上面那组属性位；allowrepeat 允许该 token 被重复匹配。
 * text/desc 用于显示与帮助；min/max 仅对 RANGE 有效；arg 保存运行时命中它的
 * 用户输入；varname/varname_src 用于把 token 绑定到命名变量；forkjoin 指向
 * 与之配对的 FORK 或 JOIN 结点（分支子图的另一半）；refcnt 为引用计数。
 */
/* Command token struct. */
struct cmd_token {
	enum cmd_token_type type; // token type
	uint8_t attr;		  // token attributes
	bool allowrepeat; // matcher allowed to match token repetitively?
	uint8_t varname_src;
	uint32_t refcnt;

	char *text;	 // token text
	char *desc;	 // token description
	long long min, max; // for ranges
	char *arg;	  // user input that matches this token
	char *varname;

	struct graph_node *forkjoin; // paired FORK/JOIN for JOIN/FORK
};

/*
 * struct cmd_element —— 一条命令的完整定义，由 DEFUN / DEFPY 宏静态生成。
 * string 是命令模板串（含 token 占位符）；doc 是逐行帮助；func 命中后执行；
 * daemon 指明命令属于哪个守护进程（vtysh 转发用）；attr 为属性位；
 * name 为源码中的符号名（调试用）；xref 记录命令的源码位置。
 */
/* Structure of command element. */
struct cmd_element {
	const char *string; /* Command specification by string. */
	const char *doc;    /* Documentation of this command. */
	int daemon;	 /* Daemon to which this command belong. */
	uint32_t attr;       /* Command attributes */

	/* handler function for command */
	int (*func)(const struct cmd_element *, struct vty *, int,
		    struct cmd_token *[]);

	const char *name; /* symbol name for debugging */
	struct xref xref;
};

/* text for <cr> command */
/* CMD_CR_TEXT —— 回车（空命令）token 的显示文本。 */
#define CMD_CR_TEXT "<cr>"

/*
 * cmd_token 的构造/析构与命名接口。
 * cmd_token_new：按类型/属性/文本/描述创建一个 token（refcnt 初始为 1）。
 * cmd_token_dup：复制一个 token（用于命令图合并时共享结点）。
 * cmd_token_del：引用计数减一，归零则释放。
 * cmd_token_varname_set / _seqappend / _join：设置或按位置/合并规则生成 varname，
 * 供补全与 clippy 定位具名参数使用。
 */
/* memory management for cmd_token */
extern struct cmd_token *cmd_token_new(enum cmd_token_type type, uint8_t attr, const char *text,
				       const char *desc);
extern struct cmd_token *cmd_token_dup(struct cmd_token *token);
extern void cmd_token_del(struct cmd_token *token);
extern void cmd_token_varname_set(struct cmd_token *token, const char *varname);
extern void cmd_token_varname_seqappend(struct graph_node *n);
extern void cmd_token_varname_join(struct graph_node *n, const char *varname);

/*
 * enum cmd_graph_parse_errors —— cmd_graph_parse 的返回错误位。
 * DOCSTRING_MISSING 表示帮助串行数少于 token 数；DOCSTRING_EXTRA 表示多于。
 * 二者都是命令定义写错的信号（帮助串必须与 token 一一对应）。
 */
enum cmd_graph_parse_errors {
	CMD_GRAPH_PARSE_DOCSTRING_MISSING = (1 << 0),
	CMD_GRAPH_PARSE_DOCSTRING_EXTRA = (1 << 1),
};

/*
 * 命令图构建接口。
 * cmd_graph_parse：把一条 cmd_element 编译进 graph，返回解析错误位（0 表示成功）。
 * cmd_graph_names：为图中结点补全 varname（供补全/日志使用）。
 * cmd_graph_merge：把图 n 按 direction 合并进 old，实现多条命令共用一张图。
 */
extern uint32_t cmd_graph_parse(struct graph *graph, const struct cmd_element *cmd);
extern void cmd_graph_names(struct graph *graph);
extern void cmd_graph_merge(struct graph *old, struct graph *n,
			    int direction);
/*
 * cmd_graph_node_print_cb —— 把单个图结点格式化成 DOT 片段，供调试导出使用。
 */
/*
 * Print callback for DOT dumping.
 *
 * See graph.h for more details.
 */
extern void cmd_graph_node_print_cb(struct graph_node *gn, struct buffer *buf);
/*
 * cmd_graph_dump_dot —— 把整张命令图导出为 Graphviz DOT 文本，返回用
 * MTYPE_TMP 分配的字符串（调用方负责释放），主要用于调试与可视化。
 */
/*
 * Dump command graph to DOT.
 *
 * cmdgraph
 *    A command graph to dump
 *
 * Returns:
 *    String allocated with MTYPE_TMP representing this graph
 */
char *cmd_graph_dump_dot(struct graph *cmdgraph);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_COMMAND_GRAPH_H */
