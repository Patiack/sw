// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Testing shim and API examples for the new CLI backend.
 *
 * This unit defines a number of commands in the old engine that can
 * be used to test and interact with the new engine.
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */

// ============================================================
// 【文件导读】grammar_sandbox.c
// ------------------------------------------------------------
// 职责：
//   新命令引擎(command_match / command_graph / DFA)的"命令语法调试
//   沙箱"。它用老引擎的 DEFUN 定义了一批 grammar ... 命令，让开发者
//   能在运行中的 FRR(或 vtysh，或独立的 grammar_sandbox_main)里交互
//   地：解析一段命令串、尝试补全、尝试匹配、把命令图打印出来/导出为
//   dot 文件，以及扫描全部命令节点找出"有歧义(同一串匹配多条命令)"
//   的条目。
//
// 核心数据结构：
//   - nodegraph / nodegraph_free: 全局"命令图"(有向图，节点是命令
//     token)，沙箱所有操作都针对它；
//   - struct cmd_permute_item: 枚举出的"一条完整命令串 + 对应
//     cmd_element"，用于查重；
//   - check_nodegraph() 宏: 图未初始化时统一报错返回。
//
// 与其他模块的关系：
//   - command.c / command_match.c: 提供 cmd_graph_parse /
//     command_match / command_complete / cmd_finalize_node 等；
//   - graph.c / linklist.h: 有向图与链表容器；
//   - vtysh.c: 调用 grammar_sandbox_init() 安装这些命令；
//   - grammar_sandbox_main.c: 独立可执行入口。
//
// 阅读顺序：
//   全局状态与宏 -> grammar parse/match/complete(核心三件套)
//   -> grammar show/dotfile/test docstring(观察类) -> find-ambiguous
//   (查重，含 cmd_graph_permute) -> init/access -> grammar_sandbox_init
//   -> pretty_print_graph / init_cmdgraph(底层工具)。
//
// 易踩的坑：
//   - 本文件只在开发构建(--enable-dev-build 之类)下才被编入；
//   - grammar_findambig 的 nodescan 会遍历所有命令节点，比较慢；
//   - 有些 DEFUN 只打印、不修改图(如 show / dotfile)，勿混淆。
// ============================================================

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// command.h：老引擎命令定义/安装宏(DEFUN、install_element 等)。
// graph.h：有向图容器(graph_new / graph_new_node / cmd_graph_parse 等)。
// linklist.h：链表(permute 结果、match 结果用)。
// command_match.h：新引擎匹配/补全接口(command_match/complete)。
#include "command.h"
#include "graph.h"
#include "linklist.h"
#include "command_match.h"

// GRAMMAR_STR: 所有 grammar 命令帮助文本里共用的第一行说明。
#define GRAMMAR_STR "CLI grammar sandbox\n"

// 为沙箱里动态构造的命令描述注册一个内存类型(便于 memstats 统计)。
DEFINE_MTYPE_STATIC(LIB, CMD_DESCRIPTIONS, "Command desc");

/** headers **/
// 前置声明(供下方命令体互相引用)：
//   grammar_sandbox_init 由 vtysh.c 调用；
//   pretty_print_graph 递归打印命令图；
//   init_cmdgraph 建一张空命令图(只含起始节点)。
void grammar_sandbox_init(void);
static void pretty_print_graph(struct vty *vty, struct graph_node *, int, int,
			       struct graph_node **, size_t);
static void init_cmdgraph(struct vty *, struct graph **);

/** shim interface commands **/
// nodegraph: 当前正在调试/操作的命令图。
// nodegraph_free: 指向"由沙箱创建、需要在重建时释放"的那张图
//   (例如 init/access 里新建的图)；用于避免内存泄漏。
static struct graph *nodegraph = NULL, *nodegraph_free = NULL;

// check_nodegraph(): 若命令图尚未初始化，打印提示并让调用者返回
//   CMD_WARNING。各 grammar 命令体开头统一用它做前置检查。
#define check_nodegraph()                                                      \
	do {                                                                   \
		if (!nodegraph) {                                              \
			vty_out(vty, "nodegraph not initialized\n");           \
			return CMD_WARNING;                                    \
		}                                                              \
	} while (0)

// grammar_test: 命令 "grammar parse LINE..."。
//   把用户输入的命令串当成"一条新命令"：先拼出字符串，构造一个临时
//   cmd_element(带占位 doc)，用 cmd_graph_parse 解析成一张临时图，
//   再 cmd_graph_merge 合入 nodegraph(+1 表示与已有图合并)。
DEFUN (grammar_test,
       grammar_test_cmd,
       "grammar parse LINE...",
       GRAMMAR_STR
       "parse a command\n"
       "command to pass to new parser\n")
{
	check_nodegraph();

	int idx_command = 2;
	// make a string from tokenized command line
	char *command = argv_concat(argv, argc, idx_command);

	// create cmd_element for parser
	struct cmd_element *cmd =
		XCALLOC(MTYPE_CMD_DESCRIPTIONS, sizeof(struct cmd_element));
	cmd->string = command;
	cmd->doc =
		"0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n";
	cmd->func = NULL;

	// parse the command and install it into the command graph
	struct graph *graph = graph_new();
	struct cmd_token *token = cmd_token_new(START_TKN, 0, NULL, NULL);
	graph_new_node(graph, token, (void (*)(void *)) & cmd_token_del);

	cmd_graph_parse(graph, cmd);
	cmd_graph_merge(nodegraph, graph, +1);

	return CMD_SUCCESS;
}

// grammar_test_complete: 命令 "grammar complete COMMAND..."。
//   把输入切成 token 向量，交给 command_complete 在当前图上做补全；
//   成功则按最长 token 宽度对齐打印候选文本与说明，失败打印 No match。
//   用完会释放补全结果、strvec 等临时资源。
DEFUN (grammar_test_complete,
       grammar_test_complete_cmd,
       "grammar complete COMMAND...",
       GRAMMAR_STR
       "attempt to complete input on DFA\n"
       "command to complete\n")
{
	check_nodegraph();

	int idx_command = 2;
	char *cmdstr = argv_concat(argv, argc, idx_command);
	if (!cmdstr)
		return CMD_SUCCESS;

	vector command = cmd_make_strvec(cmdstr);
	if (!command) {
		XFREE(MTYPE_TMP, cmdstr);
		return CMD_SUCCESS;
	}

	// generate completions of user input
	struct list *completions;
	enum matcher_rv result =
		command_complete(nodegraph, command, &completions);

	// print completions or relevant error message
	if (!MATCHER_ERROR(result)) {
		vector comps = completions_to_vec(completions);
		struct cmd_token *tkn;

		// calculate length of longest tkn->text in completions
		unsigned int width = 0, i = 0;
		for (i = 0; i < vector_active(comps); i++) {
			tkn = vector_slot(comps, i);
			unsigned int len = strlen(tkn->text);
			width = len > width ? len : width;
		}

		// print completions
		for (i = 0; i < vector_active(comps); i++) {
			tkn = vector_slot(comps, i);
			vty_out(vty, "  %-*s  %s\n", width, tkn->text,
				tkn->desc);
		}

		for (i = 0; i < vector_active(comps); i++)
			cmd_token_del(
				(struct cmd_token *)vector_slot(comps, i));
		vector_free(comps);
	} else
		vty_out(vty, "%% No match\n");

	// free resources
	list_delete(&completions);
	cmd_free_strvec(command);
	XFREE(MTYPE_TMP, cmdstr);

	return CMD_SUCCESS;
}

// grammar_test_match: 命令 "grammar match COMMAND..."。
//   用 command_match 尝试把输入匹配到某条命令：命中则打印该命令串、
//   每个 token 的 text 与 arg、以及函数指针；未命中则按 matcher_rv
//   归类打印 Unknown / Incomplete / Ambiguous。
DEFUN (grammar_test_match,
       grammar_test_match_cmd,
       "grammar match COMMAND...",
       GRAMMAR_STR
       "attempt to match input on DFA\n"
       "command to match\n")
{
	check_nodegraph();

	int idx_command = 2;
	if (argv[2]->arg[0] == '#')
		return CMD_SUCCESS;

	char *cmdstr = argv_concat(argv, argc, idx_command);
	if (!cmdstr)
		return CMD_SUCCESS;
	vector command = cmd_make_strvec(cmdstr);
	if (!command) {
		XFREE(MTYPE_TMP, cmdstr);
		return CMD_SUCCESS;
	}

	struct list *argvv = NULL;
	const struct cmd_element *element = NULL;
	enum matcher_rv result =
		command_match(nodegraph, command, &argvv, &element);

	// print completions or relevant error message
	if (element) {
		vty_out(vty, "Matched: %s\n", element->string);
		struct listnode *ln;
		struct cmd_token *token;
		for (ALL_LIST_ELEMENTS_RO(argvv, ln, token))
			vty_out(vty, "%s -- %s\n", token->text, token->arg);

		vty_out(vty, "func: %p\n", element->func);

		list_delete(&argvv);
	} else {
		assert(MATCHER_ERROR(result));
		switch (result) {
		case MATCHER_NO_MATCH:
			vty_out(vty, "%% Unknown command\n");
			break;
		case MATCHER_INCOMPLETE:
			vty_out(vty, "%% Incomplete command\n");
			break;
		case MATCHER_AMBIGUOUS:
			vty_out(vty, "%% Ambiguous command\n");
			break;
		case MATCHER_OK:
			vty_out(vty, "%% Unknown error\n");
			break;
		}
	}

	// free resources
	cmd_free_strvec(command);
	XFREE(MTYPE_TMP, cmdstr);

	return CMD_SUCCESS;
}

/**
 * Testing shim to test docstrings
 */
// grammar_test_doc: 命令 "grammar test docstring"。
//   用一条涵盖 <选择器|分支>、(范围)、[可选]、. VARARG 等语法的样例
//   命令，连同其 docstring，构造 cmd_element 并 cmd_graph_parse 进
//   nodegraph，用来验证 docstring 的解析与展示是否正确。
DEFUN (grammar_test_doc,
       grammar_test_doc_cmd,
       "grammar test docstring",
       GRAMMAR_STR
       "Test function for docstring\n"
       "Command end\n")
{
	check_nodegraph();

	// create cmd_element with docstring
	struct cmd_element *cmd =
		XCALLOC(MTYPE_CMD_DESCRIPTIONS, sizeof(struct cmd_element));
	cmd->string = XSTRDUP(
		MTYPE_CMD_DESCRIPTIONS,
		"test docstring <example|selector follow> (1-255) end VARIABLE [OPTION|set lol] . VARARG");
	cmd->doc = XSTRDUP(MTYPE_CMD_DESCRIPTIONS,
			   "Test stuff\n"
			   "docstring thing\n"
			   "first example\n"
			   "second example\n"
			   "follow\n"
			   "random range\n"
			   "end thingy\n"
			   "variable\n"
			   "optional variable\n"
			   "optional set\n"
			   "optional lol\n"
			   "vararg!\n");
	cmd->func = NULL;

	// parse element
	cmd_graph_parse(nodegraph, cmd);

	return CMD_SUCCESS;
}

/**
 * Debugging command to print command graph
 */
// grammar_test_show: 命令 "grammar show [doc]"。
//   从图的起始节点(vector_slot(nodegraph->nodes, 0))开始，递归调用
//   pretty_print_graph 打印整张图；带 doc 参数时一并打印 docstring。
//   只读，不修改图。栈大小受 CMD_ARGC_MAX 限制。
DEFUN (grammar_test_show,
       grammar_test_show_cmd,
       "grammar show [doc]",
       GRAMMAR_STR
       "print current accumulated DFA\n"
       "include docstrings\n")
{
	check_nodegraph();

	struct graph_node *stack[CMD_ARGC_MAX];
	pretty_print_graph(vty, vector_slot(nodegraph->nodes, 0), 0, argc >= 3,
			   stack, 0);
	return CMD_SUCCESS;
}

// grammar_test_dot: 命令 "grammar dotfile OUTNAME"。
//   把 nodegraph 导出成 Graphviz dot 文本并写入文件，便于用图形工具
//   观察命令图结构。打开文件失败时报错返回。
DEFUN (grammar_test_dot,
       grammar_test_dot_cmd,
       "grammar dotfile OUTNAME",
       GRAMMAR_STR
       "print current graph for dot\n"
       ".dot filename\n")
{
	check_nodegraph();
	FILE *ofd = fopen(argv[2]->arg, "w");

	if (!ofd) {
		vty_out(vty, "%s: %s\r\n", argv[2]->arg, strerror(errno));
		return CMD_SUCCESS;
	}

	char *dot = cmd_graph_dump_dot(nodegraph);

	fprintf(ofd, "%s", dot);
	fclose(ofd);
	XFREE(MTYPE_TMP, dot);

	return CMD_SUCCESS;
}

// struct cmd_permute_item: "枚举出的一条完整命令串 + 其来源元素"。
//   由 cmd_graph_permute 在遍历命令图走到 END_TKN 时构造，放进按 cmd
//   排序的链表中，供 find-ambiguous 检测重复(歧义)命令。
struct cmd_permute_item {
	char *cmd;
	struct cmd_element *el;
};

// cmd_permute_free: 链表删除回调，释放 item 里的命令串与 item 本身。
static void cmd_permute_free(void *arg)
{
	struct cmd_permute_item *i = arg;
	XFREE(MTYPE_TMP, i->cmd);
	XFREE(MTYPE_TMP, i);
}

// cmd_permute_cmp: 链表排序比较函数，按命令串 strcmp 比较，
//   使相同命令串在排序后相邻，便于找歧义。
static int cmd_permute_cmp(void *a, void *b)
{
	struct cmd_permute_item *aa = a, *bb = b;
	return strcmp(aa->cmd, bb->cmd);
}

// cmd_graph_permute: 递归深度优先遍历命令图，枚举出所有"从根到 END"
//   的完整命令串。
//   - 普通 token 把文本追加到 cmd；
//   - 走到 END_TKN 时封尾(去掉尾空格)并构造 cmd_permute_item 存入 out；
//   - 用 stack 记录当前路径上的节点，遇到已在栈上的节点视为成环
//     (如 vararg)，跳过以避免无限递归；
//   - 路径深度上限 CMD_ARGC_MAX。
static void cmd_graph_permute(struct list *out, struct graph_node **stack,
			      size_t stackpos, char *cmd)
{
	struct graph_node *gn = stack[stackpos];
	struct cmd_token *tok = gn->data;
	char *appendp = cmd + strlen(cmd);
	size_t j;

	if (tok->type < SPECIAL_TKN) {
		sprintf(appendp, "%s ", tok->text);
		appendp += strlen(appendp);
	} else if (tok->type == END_TKN) {
		struct cmd_permute_item *i = XMALLOC(MTYPE_TMP, sizeof(*i));
		i->el = ((struct graph_node *)vector_slot(gn->to, 0))->data;
		i->cmd = XSTRDUP(MTYPE_TMP, cmd);
		i->cmd[strlen(cmd) - 1] = '\0';
		listnode_add_sort(out, i);
		return;
	}

	if (++stackpos == CMD_ARGC_MAX)
		return;

	for (size_t i = 0; i < vector_active(gn->to); i++) {
		struct graph_node *gnext = vector_slot(gn->to, i);
		for (j = 0; j < stackpos; j++)
			if (stack[j] == gnext)
				break;
		if (j != stackpos)
			continue;

		stack[stackpos] = gnext;
		*appendp = '\0';
		cmd_graph_permute(out, stack, stackpos, cmd);
	}
}

// cmd_graph_permutations: 枚举 graph 的所有命令串，返回一个按命令串
//   排序的链表(带 cmd_permute_free 清理回调)。从起始节点开始递归。
static struct list *cmd_graph_permutations(struct graph *graph)
{
	char accumulate[2048] = "";
	struct graph_node *stack[CMD_ARGC_MAX];

	struct list *rv = list_new();
	rv->cmp = cmd_permute_cmp;
	rv->del = cmd_permute_free;
	stack[0] = vector_slot(graph->nodes, 0);
	cmd_graph_permute(rv, stack, 0, accumulate);
	return rv;
}

// cmdvec: 全局"命令节点向量"，下标即节点号，元素是 struct cmd_node。
//   find-ambiguous 的 nodescan 模式靠它遍历所有命令节点。
extern vector cmdvec;

// grammar_findambig: 命令 "grammar find-ambiguous [{printall|nodescan}]"。
//   枚举某张命令图的所有完整命令串，排序后比较相邻项：命令串相同即
//   判定为"歧义"(同一串匹配多条命令)，打印冲突双方并计数。
//   - printall: 额外打印全部命令串；
//   - nodescan: 遍历所有命令节点(用 cmdvec)，逐张图扫描；
//   最后输出歧义总数，有歧义返回 CMD_WARNING_CONFIG_FAILED。
DEFUN (grammar_findambig,
       grammar_findambig_cmd,
       "grammar find-ambiguous [{printall|nodescan}]",
       GRAMMAR_STR
       "Find ambiguous commands\n"
       "Print all permutations\n"
       "Scan all nodes\n")
{
	struct list *commands;
	struct cmd_permute_item *prev = NULL, *cur = NULL;
	struct listnode *ln;
	int i, printall, scan, scannode = 0;
	int ambig = 0;

	i = 0;
	printall = argv_find(argv, argc, "printall", &i);
	i = 0;
	scan = argv_find(argv, argc, "nodescan", &i);

	if (scan && nodegraph_free) {
		graph_delete_graph(nodegraph_free);
		nodegraph_free = NULL;
	}

	if (!scan && !nodegraph) {
		vty_out(vty, "nodegraph uninitialized\r\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	do {
		if (scan) {
			struct cmd_node *cnode =
				vector_slot(cmdvec, scannode++);
			if (!cnode)
				continue;
			cmd_finalize_node(cnode);
			nodegraph = cnode->cmdgraph;
			if (!nodegraph)
				continue;
			vty_out(vty, "scanning node %d (%s)\n", scannode - 1,
				cnode->name);
		}

		commands = cmd_graph_permutations(nodegraph);
		prev = NULL;
		for (ALL_LIST_ELEMENTS_RO(commands, ln, cur)) {
			int same = prev && !strcmp(prev->cmd, cur->cmd);
			if (printall && !same)
				vty_out(vty, "'%s' [%x]\n", cur->cmd,
					cur->el->daemon);
			if (same) {
				vty_out(vty, "'%s' AMBIGUOUS:\n", cur->cmd);
				vty_out(vty, "  %s\n   '%s'\n", prev->el->name,
					prev->el->string);
				vty_out(vty, "  %s\n   '%s'\n", cur->el->name,
					cur->el->string);
				vty_out(vty, "\n");
				ambig++;
			}
			prev = cur;
		}
		list_delete(&commands);

		vty_out(vty, "\n");
	} while (scan && scannode < LINK_PARAMS_NODE);

	vty_out(vty, "%d ambiguous commands found.\n", ambig);

	if (scan)
		nodegraph = NULL;
	return ambig == 0 ? CMD_SUCCESS : CMD_WARNING_CONFIG_FAILED;
}

// grammar_init_graph: 命令 "grammar init"。
//   释放上一次由沙箱创建的图(nodegraph_free)，再清空指针，
//   然后调用 init_cmdgraph 建一张只含起始节点的新图。
DEFUN (grammar_init_graph,
       grammar_init_graph_cmd,
       "grammar init",
       GRAMMAR_STR
       "(re)initialize graph\n")
{
	if (nodegraph_free)
		graph_delete_graph(nodegraph_free);
	nodegraph_free = NULL;

	init_cmdgraph(vty, &nodegraph);
	return CMD_SUCCESS;
}

// grammar_access: 命令 "grammar access (0-65535)"。
//   按下标从 cmdvec 取出一个真实命令节点，finalize 其命令图
//   (cmd_finalize_node)，再把它设为当前调试图 nodegraph。
//   这样就能直接对某个真实节点的图做 parse/match/show/find-ambiguous。
DEFUN (grammar_access,
       grammar_access_cmd,
       "grammar access (0-65535)",
       GRAMMAR_STR
       "access node graph\n"
       "node number\n")
{
	if (nodegraph_free)
		graph_delete_graph(nodegraph_free);
	nodegraph_free = NULL;

	struct cmd_node *cnode;

	cnode = vector_slot(cmdvec, atoi(argv[2]->arg));
	if (!cnode) {
		vty_out(vty, "%% no such node\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	vty_out(vty, "node %d\n", (int)cnode->node);
	cmd_finalize_node(cnode);
	nodegraph = cnode->cmdgraph;
	return CMD_SUCCESS;
}

/* this is called in vtysh.c to set up the testing shim */
// grammar_sandbox_init: 安装全部 grammar 调试命令到 ENABLE_NODE。
//   由 vtysh.c 调用(独立沙箱里经 lib_cmd_init 间接调用)。
void grammar_sandbox_init(void)
{
	// install all enable elements
	install_element(ENABLE_NODE, &grammar_test_cmd);
	install_element(ENABLE_NODE, &grammar_test_show_cmd);
	install_element(ENABLE_NODE, &grammar_test_dot_cmd);
	install_element(ENABLE_NODE, &grammar_test_match_cmd);
	install_element(ENABLE_NODE, &grammar_test_complete_cmd);
	install_element(ENABLE_NODE, &grammar_test_doc_cmd);
	install_element(ENABLE_NODE, &grammar_findambig_cmd);
	install_element(ENABLE_NODE, &grammar_init_graph_cmd);
	install_element(ENABLE_NODE, &grammar_access_cmd);
}

/**
 * Pretty-prints a graph, assuming it is a tree.
 *
 * @param start the node to take as the root
 * @param level indent level for recursive calls, always pass 0
 */
// pretty_print_graph: 递归打印命令图(假定它是一棵树)。
//   - 打印当前节点：token 类型名(查 tokennames)、文本、变量名，
//     desc 为真时再打印 docstring；
//   - 用 stack 记录祖先链，遇到指向祖先的边打印 "<<loop@N" 防环；
//   - 指向自身的节点(典型是 vararg)打印 "*"；
//   - 子节点为 END_TKN 时打印 "--END"；
//   - 深度到 CMD_ARGC_MAX 时打印告警并回退。
//   level 为缩进层级，size_t stackpos 为当前栈深，调用方传 0。
static void pretty_print_graph(struct vty *vty, struct graph_node *start,
			       int level, int desc, struct graph_node **stack,
			       size_t stackpos)
{
	// print this node
	char tokennum[32];
	struct cmd_token *tok = start->data;

	snprintf(tokennum, sizeof(tokennum), "%d?", tok->type);
	vty_out(vty, "%s", lookup_msg(tokennames, tok->type, NULL));
	if (tok->text)
		vty_out(vty, ":\"%s\"", tok->text);
	if (tok->varname)
		vty_out(vty, " => %s", tok->varname);
	if (desc)
		vty_out(vty, " ?'%s'", tok->desc);
	vty_out(vty, " ");

	if (stackpos == CMD_ARGC_MAX) {
		vty_out(vty, " -aborting! (depth limit)\n");
		return;
	}
	stack[stackpos++] = start;

	int numto = desc ? 2 : vector_active(start->to);
	if (numto) {
		if (numto > 1)
			vty_out(vty, "\n");
		for (unsigned int i = 0; i < vector_active(start->to); i++) {
			struct graph_node *adj = vector_slot(start->to, i);
			// if we're listing multiple children, indent!
			if (numto > 1)
				for (int j = 0; j < level + 1; j++)
					vty_out(vty, "    ");
			// if this node is a vararg, just print *
			if (adj == start)
				vty_out(vty, "*");
			else if (((struct cmd_token *)adj->data)->type
				 == END_TKN)
				vty_out(vty, "--END\n");
			else {
				size_t k;
				for (k = 0; k < stackpos; k++)
					if (stack[k] == adj) {
						vty_out(vty, "<<loop@%zu \n",
							k);
						break;
					}
				if (k == stackpos)
					pretty_print_graph(
						vty, adj,
						numto > 1 ? level + 1 : level,
						desc, stack, stackpos);
			}
		}
	} else
		vty_out(vty, "\n");
}

/** stuff that should go in command.c + command.h */
// init_cmdgraph: 建一张新的空命令图：只含一个 START_TKN 起始节点，
//   并把图指针记到 nodegraph_free，供下次 (重)初始化时释放。
//   vty 非空时打印 "initialized graph" 供交互提示。
static void init_cmdgraph(struct vty *vty, struct graph **graph)
{
	// initialize graph, add start noe
	*graph = graph_new();
	nodegraph_free = *graph;
	struct cmd_token *token = cmd_token_new(START_TKN, 0, NULL, NULL);
	graph_new_node(*graph, token, (void (*)(void *)) & cmd_token_del);
	if (vty)
		vty_out(vty, "initialized graph\n");
}
