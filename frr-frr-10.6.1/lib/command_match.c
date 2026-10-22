// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Input matching routines for CLI backend.
 *
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */

/*
 * 【文件导读】command_match.c —— 命令行输入在命令图上的匹配与补全
 *
 * 本文件实现 command_match.h 的两个入口，是整个 CLI 后端的核心算法：
 *
 *   command_match   —— 判断一整行输入能否精确命中一条命令。做法是给输入行
 *                      最前面塞一个 dummy token（对应图的 START 结点），再调用
 *                      递归的 command_match_r 沿图做深度优先搜索。命中时返回的
 *                      顺序表最后一项是 cmd_element，前面每一项是被消耗的 token
 *                      （其 arg 保存对应的输入副本）。
 *   command_complete—— 为未打完的一行生成“下一个可取词”，供 “?” 与 TAB 补全。
 *                      它按 BFS 逐词推进，维护 current / next 两个结点集合，
 *                      最后把下一步可达的 token 收集成候选列表。
 *
 * 几个关键概念：
 *   match_type（trivial / no_match / partly / exact）—— 单个 token 的匹配程度；
 *   min_match_level —— 每种 token “算作匹配”所需的最低程度（WORD 允许前缀
 *                      缩写，所以只需 partly_match）；
 *   score_precedence —— 多条路径都命中时，按类型优先级挑更具体的那个；
 *   disambiguate / disambiguate_tokens —— 两条候选路径的仲裁，平手则判歧义。
 *
 * 易踩的坑：
 *   - 图里可能有环（allowrepeat 的 token），故用 stack 记录已访问结点避免死循环；
 *   - arglist 的最后一个元素是 cmd_element 而非 token，删除要用 del_arglist 善后；
 *   - 命令行首的 "no" 由 is_neg 识别，用于 NEG_ONLY_TKN 的条件匹配。
 */

#include <zebra.h>

#include "command_match.h"
#include "memory.h"
#include "asn.h"

DEFINE_MTYPE_STATIC(LIB, CMD_MATCHSTACK, "Command Match Stack");

/*
 * TRACE_MATCHER —— 编译期打开后，会向 stdout 打印每一次 token 匹配的详细过程，
 * 排查 CLI 匹配问题时非常有用。TM 是开关常量，trace_matcher 是配套的打印宏。
 */
#ifdef TRACE_MATCHER
#define TM 1
#else
#define TM 0
#endif

#define trace_matcher(...)                                                     \
	do {                                                                   \
		if (TM)                                                        \
			fprintf(stderr, __VA_ARGS__);                          \
	} while (0);

/* matcher helper prototypes */
static int add_nexthops(struct list *, struct graph_node *,
			struct graph_node **, size_t, bool);

static enum matcher_rv command_match_r(struct graph_node *, vector,
				       unsigned int, struct graph_node **,
				       struct list **);

static int score_precedence(enum cmd_token_type);

static enum match_type min_match_level(enum cmd_token_type);

static void del_arglist(struct list *);

static struct cmd_token *disambiguate_tokens(struct cmd_token *,
					     struct cmd_token *, char *);

static struct list *disambiguate(struct list *, struct list *, vector,
				 unsigned int);

int compare_completions(const void *, const void *);

/* token matcher prototypes */
static enum match_type match_token(struct cmd_token *, char *);

static enum match_type match_ipv4(const char *);

static enum match_type match_ipv4_prefix(const char *);

static enum match_type match_ipv6_prefix(const char *, bool);

static enum match_type match_range(struct cmd_token *, const char *);

static enum match_type match_word(struct cmd_token *, const char *);

static enum match_type match_variable(struct cmd_token *, const char *);

static enum match_type match_mac(const char *, bool);

/*
 * is_neg —— 判断输入行下标 idx 处的词是否为 "no"（用于识别 “no ...” 命令）。
 * idx 越界或该位置为空时返回 false。
 */
static bool is_neg(vector vline, size_t idx)
{
	if (idx >= vector_active(vline) || !vector_slot(vline, idx))
		return false;
	return !strcmp(vector_slot(vline, idx), "no");
}

/*
 * command_match —— 命令图上的整行匹配入口。
 * 入参：cmdgraph 命令图；vline 切词后的输入；argv 出参返回命中的 token 列表；
 *       el 出参返回命中的 cmd_element。
 * 实现：先在最前面插入一个 dummy token 以喂给 START 结点，再调用 command_match_r。
 * 成功（MATCHER_OK）时，从结果表里删掉 dummy 头、取出尾部 cmd_element 并摘除，
 * 于是 *argv 变成纯 token 序列表；失败时清空 *argv 且 *el 为 NULL。
 * 注意：*el 指向启动时初始化的静态实例，切勿释放或改写。
 */
enum matcher_rv command_match(struct graph *cmdgraph, vector vline,
			      struct list **argv, const struct cmd_element **el)
{
	struct graph_node *stack[CMD_ARGC_MAX];
	enum matcher_rv status;
	*argv = NULL;

	// prepend a dummy token to match that pesky start node
	vector vvline = vector_init(vline->alloced + 1);
	vector_set_index(vvline, 0, XSTRDUP(MTYPE_TMP, "dummy"));
	memcpy(vvline->index + 1, vline->index,
	       sizeof(void *) * vline->alloced);
	vvline->active = vline->active + 1;

	struct graph_node *start = vector_slot(cmdgraph->nodes, 0);
	status = command_match_r(start, vvline, 0, stack, argv);
	if (status == MATCHER_OK) { // successful match
		struct listnode *head = listhead(*argv);
		struct listnode *tail = listtail(*argv);

		assert(head);
		assert(tail);

		// delete dummy start node
		cmd_token_del((struct cmd_token *)head->data);
		list_delete_node(*argv, head);

		// get cmd_element out of list tail
		*el = listgetdata(tail);
		list_delete_node(*argv, tail);

		// now argv is an ordered list of cmd_token matching the user
		// input, with each cmd_token->arg holding the corresponding
		// input
		assert(*el);
	} else if (*argv) {
		del_arglist(*argv);
		*argv = NULL;
	}

	if (!*el) {
		trace_matcher("No match\n");
	} else {
		trace_matcher("Matched command\n->string %s\n->desc %s\n",
			      (*el)->string, (*el)->doc);
	}

	// free the leader token we alloc'd
	XFREE(MTYPE_TMP, vector_slot(vvline, 0));
	// free vector
	vector_free(vvline);

	return status;
}

/*
 * command_match_r —— 匹配的递归主体（本文件核心）。
 * 参数：start 当前结点；vline 输入；n 待匹配输入的下标；stack 已访问结点栈；
 *       currbest 出参，累积当前找到的最佳匹配表。
 * 步骤：用 match_token 判断当前结点能否吃下 vline[n]（低于 min_match_level 即失败）；
 *   通过后压栈；取出所有可直达的 nexthop（add_nexthops）；
 *   若输入已到尾，则寻找 END_TKN 收尾（出现多于一个 END 即判为歧义）；
 *   否则对每个后继递归，并用 disambiguate 在多条候选之间仲裁、选出最优；
 *   最后把当前 token 复制一份（arg = 输入）插到结果表头。
 * 返回 MATCHER_OK / INCOMPLETE / AMBIGUOUS / NO_MATCH。
 * 下方英文注释对算法有详尽说明，可对照阅读。
 */
/**
 * Builds an argument list given a DFA and a matching input line.
 *
 * First the function determines if the node it is passed matches the first
 * token of input. If it does not, it returns NULL (MATCHER_NO_MATCH). If it
 * does match, then it saves the input token as the head of an argument list.
 *
 * The next step is to see if there is further input in the input line. If
 * there is not, the current node's children are searched to see if any of them
 * are leaves (type END_TKN). If this is the case, then the bottom of the
 * recursion stack has been reached, the leaf is pushed onto the argument list,
 * the current node is pushed, and the resulting argument list is
 * returned (MATCHER_OK). If it is not the case, NULL is returned, indicating
 * that there is no match for the input along this path (MATCHER_INCOMPLETE).
 *
 * If there is further input, then the function recurses on each of the current
 * node's children, passing them the input line minus the token that was just
 * matched. For each child, the return value of the recursive call is
 * inspected. If it is null, then there is no match for the input along the
 * subgraph headed by that child. If it is not null, then there is at least one
 * input match in that subgraph (more on this in a moment).
 *
 * If a recursive call on a child returns a non-null value, then it has matched
 * the input given it on the subgraph that starts with that child. However, due
 * to the flexibility of the grammar, it is sometimes the case that two or more
 * child graphs match the same input (two or more of the recursive calls have
 * non-NULL return values). This is not a valid state, since only one true
 * match is possible. In order to resolve this conflict, the function keeps a
 * reference to the child node that most specifically matches the input. This
 * is done by assigning each node type a precedence. If a child is found to
 * match the remaining input, then the precedence values of the current
 * best-matching child and this new match are compared. The node with higher
 * precedence is kept, and the other match is discarded. Due to the recursive
 * nature of this function, it is only necessary to compare the precedence of
 * immediate children, since all subsequent children will already have been
 * disambiguated in this way.
 *
 * In the event that two children are found to match with the same precedence,
 * then the input is ambiguous for the passed cmd_element and NULL is returned.
 *
 * @param[in] start the start node.
 * @param[in] vline the vectorized input line.
 * @param[in] n the index of the first input token.
 * @return A linked list of n elements. The first n-1 elements are pointers to
 * struct cmd_token and represent the sequence of tokens matched by the input.
 * The ->arg field of each token points to a copy of the input matched on it.
 * The final nth element is a pointer to struct cmd_element, which is the
 * command that was matched.
 *
 * If no match was found, the return value is NULL.
 */
static enum matcher_rv command_match_r(struct graph_node *start, vector vline,
				       unsigned int n,
				       struct graph_node **stack,
				       struct list **currbest)
{
	assert(n < vector_active(vline));

	enum matcher_rv status = MATCHER_NO_MATCH;

	// get the minimum match level that can count as a full match
	struct cmd_token *copy, *token = start->data;
	enum match_type minmatch = min_match_level(token->type);

	/* check history/stack of tokens
	 * this disallows matching the same one more than once if there is a
	 * circle in the graph (used for keyword arguments) */
	if (n == CMD_ARGC_MAX)
		return MATCHER_NO_MATCH;
	if (!token->allowrepeat)
		for (size_t s = 0; s < n; s++)
			if (stack[s] == start)
				return MATCHER_NO_MATCH;

	// get the current operating input token
	char *input_token = vector_slot(vline, n);

#ifdef TRACE_MATCHER
	fprintf(stdout, "\"%-20s\" matches \"%-30s\" ? ", input_token,
		token->text);
	enum match_type mt = match_token(token, input_token);
	fprintf(stdout, "type: %d ", token->type);
	fprintf(stdout, "min: %d - ", minmatch);
	switch (mt) {
	case trivial_match:
		fprintf(stdout, "trivial_match ");
		break;
	case no_match:
		fprintf(stdout, "no_match ");
		break;
	case partly_match:
		fprintf(stdout, "partly_match ");
		break;
	case exact_match:
		fprintf(stdout, "exact_match ");
		break;
	}
	if (mt >= minmatch)
		fprintf(stdout, " MATCH");
	fprintf(stdout, "\n");
#endif

	// if we don't match this node, die
	if (match_token(token, input_token) < minmatch)
		return MATCHER_NO_MATCH;

	stack[n] = start;

	// pointers for iterating linklist
	struct listnode *ln;
	struct graph_node *gn;

	// get all possible nexthops
	struct list *next = list_new();
	add_nexthops(next, start, NULL, 0, is_neg(vline, 1));

	// determine the best match
	for (ALL_LIST_ELEMENTS_RO(next, ln, gn)) {
		// if we've matched all input we're looking for END_TKN
		if (n + 1 == vector_active(vline)) {
			struct cmd_token *tok = gn->data;
			if (tok->type == END_TKN) {
				// if more than one END_TKN in the follow set
				if (*currbest) {
					status = MATCHER_AMBIGUOUS;
					break;
				} else {
					status = MATCHER_OK;
				}
				*currbest = list_new();
				// node should have one child node with the
				// element
				struct graph_node *leaf =
					vector_slot(gn->to, 0);
				// last node in the list will hold the
				// cmd_element; this is important because
				// list_delete() expects that all nodes have
				// the same data type, so when deleting this
				// list the last node must be manually deleted
				struct cmd_element *el = leaf->data;
				listnode_add(*currbest, el);
				(*currbest)->del =
					(void (*)(void *)) & cmd_token_del;
				// do not break immediately; continue walking
				// through the follow set to ensure that there
				// is exactly one END_TKN
			}
			continue;
		}

		// else recurse on candidate child node
		struct list *result = NULL;
		enum matcher_rv rstat =
			command_match_r(gn, vline, n + 1, stack, &result);

		// save the best match
		if (result && *currbest) {
			// pick the best of two matches
			struct list *newbest =
				disambiguate(*currbest, result, vline, n + 1);

			// current best and result are ambiguous
			if (!newbest)
				status = MATCHER_AMBIGUOUS;
			// current best is still the best, but ambiguous
			else if (newbest == *currbest
				 && status == MATCHER_AMBIGUOUS)
				status = MATCHER_AMBIGUOUS;
			// result is better, but also ambiguous
			else if (newbest == result
				 && rstat == MATCHER_AMBIGUOUS)
				status = MATCHER_AMBIGUOUS;
			// one or the other is superior and not ambiguous
			else
				status = MATCHER_OK;

			// delete the unnecessary result
			struct list *todelete =
				((newbest && newbest == result) ? *currbest
								: result);
			del_arglist(todelete);

			*currbest = newbest ? newbest : *currbest;
		} else if (result) {
			status = rstat;
			*currbest = result;
		} else if (!*currbest) {
			status = MAX(rstat, status);
		}
	}
	if (*currbest) {
		// copy token, set arg and prepend to currbest
		token = start->data;
		copy = cmd_token_dup(token);
		copy->arg = XSTRDUP(MTYPE_CMD_ARG, input_token);
		listnode_add_before(*currbest, (*currbest)->head, copy);
	} else if (n + 1 == vector_active(vline) && status == MATCHER_NO_MATCH)
		status = MATCHER_INCOMPLETE;

	// cleanup
	list_delete(&next);

	return status;
}

/*
 * stack_del —— 匹配栈元素的析构回调，供链表删除时释放由 MTYPE_CMD_MATCHSTACK 分配的结点栈。
 */
static void stack_del(void *val)
{
	XFREE(MTYPE_CMD_MATCHSTACK, val);
}

/*
 * command_complete —— 生成“下一个可取词”的候选。
 * 入参：graph 命令图；vline 输入；completions 出参返回候选 token 列表。
 * 做法：以 BFS 逐词推进，current 为上一轮命中的结点集合，next 为它们一跳可达的集合；
 *   对每个当前 token 调用 match_token，按 trivial / partly / exact / no_match
 *   决定是否把其后继加入 next。全部输入处理完后，把 next 里的 token 收进 *completions。
 *   若整行都走完且还有后继则返回 MATCHER_OK。
 * 注意：中途把 gstack 这类“结点栈”当链表元素存放，删除时统一走 stack_del。
 */
enum matcher_rv command_complete(struct graph *graph, vector vline,
				 struct list **completions)
{
	// pointer to next input token to match
	char *input_token;
	bool neg = is_neg(vline, 0);

	struct list *
		current =
		       list_new(), // current nodes to match input token against
		*next = list_new(); // possible next hops after current input
				    // token
	current->del = next->del = stack_del;

	// pointers used for iterating lists
	struct graph_node **gstack, **newstack;
	struct listnode *node;

	// add all children of start node to list
	struct graph_node *start = vector_slot(graph->nodes, 0);
	add_nexthops(next, start, &start, 0, neg);

	unsigned int idx;
	for (idx = 0; idx < vector_active(vline) && next->count > 0; idx++) {
		list_delete(&current);
		current = next;
		next = list_new();
		next->del = stack_del;

		input_token = vector_slot(vline, idx);

		int exact_match_exists = 0;
		for (ALL_LIST_ELEMENTS_RO(current, node, gstack))
			if (!exact_match_exists)
				exact_match_exists =
					(match_token(gstack[0]->data,
						     input_token)
					 == exact_match);
			else
				break;

		for (ALL_LIST_ELEMENTS_RO(current, node, gstack)) {
			struct cmd_token *token = gstack[0]->data;

			if (token->attr & CMD_ATTR_HIDDEN)
				continue;

			enum match_type minmatch = min_match_level(token->type);
			trace_matcher("\"%s\" matches \"%s\" (%d) ? ",
				      input_token, token->text, token->type);

			unsigned int last_token =
				(vector_active(vline) - 1 == idx);
			enum match_type matchtype =
				match_token(token, input_token);
			switch (matchtype) {
			// occurs when last token is whitespace
			case trivial_match:
				trace_matcher("trivial_match\n");
				assert(last_token);
				newstack = XMALLOC(MTYPE_CMD_MATCHSTACK,
						   sizeof(struct graph_node *));
				/* we're not recursing here, just the first
				 * element is OK */
				newstack[0] = gstack[0];
				listnode_add(next, newstack);
				break;
			case partly_match:
				trace_matcher("partly_match\n");
				if (exact_match_exists && !last_token)
					break;
				fallthrough;
			case exact_match:
				trace_matcher("exact_match\n");
				if (last_token) {
					newstack = XMALLOC(
						MTYPE_CMD_MATCHSTACK,
						sizeof(struct graph_node *));
					/* same as above, not recursing on this
					 */
					newstack[0] = gstack[0];
					listnode_add(next, newstack);
				} else if (matchtype >= minmatch)
					add_nexthops(next, gstack[0], gstack,
						     idx + 1, neg);
				break;
			case no_match:
				trace_matcher("no_match\n");
				break;
			}
		}
	}

	/* Variable summary
	 * -----------------------------------------------------------------
	 * token    = last input token processed
	 * idx      = index in `command` of last token processed
	 * current  = set of all transitions from the previous input token
	 * next     = set of all nodes reachable from all nodes in `matched`
	 */

	enum matcher_rv mrv = idx == vector_active(vline) && next->count
				      ? MATCHER_OK
				      : MATCHER_NO_MATCH;

	*completions = NULL;
	if (!MATCHER_ERROR(mrv)) {
		// extract cmd_token into list
		*completions = list_new();
		for (ALL_LIST_ELEMENTS_RO(next, node, gstack)) {
			listnode_add(*completions, gstack[0]->data);
		}
	}

	list_delete(&current);
	list_delete(&next);

	return mrv;
}

/*
 * add_nexthops —— 收集从 node 出发、一跳可达的“实际 token 结点”。
 * 结构 token（FORK / JOIN / START，即 >= SPECIAL_TKN 且非 END_TKN）被当作透明，
 * 直接穿透继续下探；END_TKN 与可变化 token 作为叶子加入 list。
 * stack / stackpos 给出来路，用于跳过已访问过的、allowrepeat 为假的结点；
 * neg 控制 NEG_ONLY_TKN 是否参与（对应 “no” 命令）。
 * 若传入非空 stack，则加入 list 的是“结点栈”副本（栈顶为当前结点），
 * 否则直接加结点指针。返回加入的元素个数。
 */
/**
 * Adds all children that are reachable by one parser hop to the given list.
 * special tokens except END_TKN are treated as transparent.
 *
 * @param[in] list to add the nexthops to
 * @param[in] node to start calculating nexthops from
 * @param[in] stack listing previously visited nodes, if non-NULL.
 * @param[in] stackpos how many valid entries are in stack
 * @return the number of children added to the list
 *
 * NB: non-null "stack" means that new stacks will be added to "list" as
 * output, instead of direct node pointers!
 */
static int add_nexthops(struct list *list, struct graph_node *node,
			struct graph_node **stack, size_t stackpos, bool neg)
{
	int added = 0;
	struct graph_node *child;
	struct graph_node **nextstack;
	for (unsigned int i = 0; i < vector_active(node->to); i++) {
		child = vector_slot(node->to, i);
		size_t j;
		struct cmd_token *token = child->data;
		if (!token->allowrepeat && stack) {
			for (j = 0; j < stackpos; j++)
				if (child == stack[j])
					break;
			if (j != stackpos)
				continue;
		}

		if (token->type == NEG_ONLY_TKN && !neg)
			continue;

		if (token->type >= SPECIAL_TKN && token->type != END_TKN) {
			added +=
				add_nexthops(list, child, stack, stackpos, neg);
		} else {
			if (stack) {
				nextstack = XMALLOC(
					MTYPE_CMD_MATCHSTACK,
					(stackpos + 1)
						* sizeof(struct graph_node *));
				nextstack[0] = child;
				memcpy(nextstack + 1, stack,
				       stackpos * sizeof(struct graph_node *));

				listnode_add(list, nextstack);
			} else
				listnode_add(list, child);
			added++;
		}
	}

	return added;
}

/*
 * min_match_level —— 各 token 类型“算作匹配”所需的最低匹配程度。
 * START 放行一切（连 no_match 也接受，便于递归起步）；WORD 允许前缀缩写，
 * 故为 partly_match；其余类型都要求 exact_match。
 */
/**
 * Determines the node types for which a partial match may count as a full
 * match. Enables command abbrevations.
 *
 * @param[in] type node type
 * @return minimum match level needed to for a token to fully match
 */
static enum match_type min_match_level(enum cmd_token_type type)
{
	switch (type) {
	// anything matches a start node, for the sake of recursion
	case START_TKN:
		return no_match;
	// allowing words to partly match enables command abbreviation
	case WORD_TKN:
		return partly_match;
	case RANGE_TKN:
	case IPV4_TKN:
	case IPV4_PREFIX_TKN:
	case IPV6_TKN:
	case IPV6_PREFIX_TKN:
	case MAC_TKN:
	case MAC_PREFIX_TKN:
	case FORK_TKN:
	case JOIN_TKN:
	case END_TKN:
	case NEG_ONLY_TKN:
	case VARIABLE_TKN:
	case ASNUM_TKN:
		return exact_match;
	}

	assert(!"Reached end of function we should never hit");
	return no_match;
}

/*
 * score_precedence —— 给 token 类型打分，用于仲裁“哪条路径更具体”。
 * 分值越小越具体：IP / 前缀 / 数字区间为 2，AS 号与 WORD 为 3，变量为 4，
 * 结构 token 为 10（最不具体）。多条路径都命中时保留分值更小者。
 */
/**
 * Assigns precedence scores to node types.
 *
 * @param[in] type node type to score
 * @return precedence score
 */
static int score_precedence(enum cmd_token_type type)
{
	switch (type) {
	// some of these are mutually exclusive, so they share
	// the same precedence value
	case IPV4_TKN:
	case IPV4_PREFIX_TKN:
	case IPV6_TKN:
	case IPV6_PREFIX_TKN:
	case MAC_TKN:
	case MAC_PREFIX_TKN:
	case RANGE_TKN:
		return 2;
	case ASNUM_TKN:
	case WORD_TKN:
		return 3;
	case VARIABLE_TKN:
		return 4;
	case JOIN_TKN:
	case START_TKN:
	case END_TKN:
	case NEG_ONLY_TKN:
	case SPECIAL_TKN:
		return 10;
	}

	assert(!"Reached end of function we should never hit");
	return -1;
}

/*
 * disambiguate_tokens —— 在两个 token 候选之间取更优者。
 * 类型不同则按 score_precedence 取分小者；分值相同视为无法区分，返回 NULL。
 * 类型相同时取 match_type 更高（更精确）的那个；仍相同则返回 NULL（歧义）。
 */
/**
 * Picks the better of two possible matches for a token.
 *
 * @param[in] first candidate node matching token
 * @param[in] second candidate node matching token
 * @param[in] token the token being matched
 * @return the best-matching node, or NULL if the two are entirely ambiguous
 */
static struct cmd_token *disambiguate_tokens(struct cmd_token *first,
					     struct cmd_token *second,
					     char *input_token)
{
	// if the types are different, simply go off of type precedence
	if (first->type != second->type) {
		int firstprec = score_precedence(first->type);
		int secndprec = score_precedence(second->type);
		if (firstprec != secndprec)
			return firstprec < secndprec ? first : second;
		else
			return NULL;
	}

	// if they're the same, return the more exact match
	enum match_type fmtype = match_token(first, input_token);
	enum match_type smtype = match_token(second, input_token);
	if (fmtype != smtype)
		return fmtype > smtype ? first : second;

	return NULL;
}

/*
 * disambiguate —— 在两条完整匹配路径（token 列表）之间取更优者。
 * 要求两条列表等长（长度应等于 vline 剩余 token 数加一）。逐 token 用
 * disambiguate_tokens 比较，谁先在某一位胜出就返回谁的列表；全程不分胜负返回 NULL。
 */
/**
 * Picks the better of two possible matches for an input line.
 *
 * @param[in] first candidate list of cmd_token matching vline
 * @param[in] second candidate list of cmd_token matching vline
 * @param[in] vline the input line being matched
 * @param[in] n index into vline to start comparing at
 * @return the best-matching list, or NULL if the two are entirely ambiguous
 */
static struct list *disambiguate(struct list *first, struct list *second,
				 vector vline, unsigned int n)
{
	assert(first != NULL);
	assert(second != NULL);
	// doesn't make sense for these to be inequal length
	assert(first->count == second->count);
	assert(first->count == vector_active(vline) - n + 1);

	struct listnode *fnode = listhead_unchecked(first),
			*snode = listhead_unchecked(second);
	struct cmd_token *ftok = listgetdata(fnode), *stok = listgetdata(snode),
			 *best = NULL;

	// compare each token, if one matches better use that one
	for (unsigned int i = n; i < vector_active(vline); i++) {
		char *token = vector_slot(vline, i);
		if ((best = disambiguate_tokens(ftok, stok, token)))
			return best == ftok ? first : second;
		fnode = listnextnode(fnode);
		snode = listnextnode(snode);
		ftok = listgetdata(fnode);
		stok = listgetdata(snode);
	}

	return NULL;
}

/*
 * del_arglist —— 删除一个匹配结果表（arglist）。
 * 该表的尾部存的是 cmd_element（不是 token，且是全局唯一实例不可复制），
 * 因此先把尾结点的 data 置空、摘掉尾结点，再把其余元素按通常方式删除，避免误释放。
 */
/*
 * Deletion function for arglist.
 *
 * Since list->del for arglists expects all listnode->data to hold cmd_token,
 * but arglists have cmd_element as the data for the tail, this function
 * manually deletes the tail before deleting the rest of the list as usual.
 *
 * The cmd_element at the end is *not* a copy. It is the one and only.
 *
 * @param list the arglist to delete
 */
static void del_arglist(struct list *list)
{
	// manually delete last node
	struct listnode *tail = listtail(list);
	tail->data = NULL;
	list_delete_node(list, tail);

	// delete the rest of the list as usual
	list_delete(&list);
}

/*
 * 以下为一组“token 级”匹配函数：每个负责一种 cmd_token_type 的输入校验，
 * 统一返回 enum match_type（no_match / partly_match / exact_match）。
 */
/*---------- token level matching functions ----------*/

/*
 * match_token —— token 级匹配的总调度：按 token->type 分派到具体匹配函数。
 * 输入为 NULL 时返回 trivial_match（空输入匹配一切）。结构 token（END / FORK /
 * JOIN / START / NEG_ONLY）一律返回 no_match，因为它们不消耗用户输入。
 */
static enum match_type match_token(struct cmd_token *token, char *input_token)
{
	// nothing trivially matches everything
	if (!input_token)
		return trivial_match;

	switch (token->type) {
	case WORD_TKN:
		return match_word(token, input_token);
	case IPV4_TKN:
		return match_ipv4(input_token);
	case IPV4_PREFIX_TKN:
		return match_ipv4_prefix(input_token);
	case IPV6_TKN:
		return match_ipv6_prefix(input_token, false);
	case IPV6_PREFIX_TKN:
		return match_ipv6_prefix(input_token, true);
	case RANGE_TKN:
		return match_range(token, input_token);
	case VARIABLE_TKN:
		return match_variable(token, input_token);
	case MAC_TKN:
		return match_mac(input_token, false);
	case MAC_PREFIX_TKN:
		return match_mac(input_token, true);
	case ASNUM_TKN:
		return asn_str2asn_match(input_token);
	case END_TKN:
	case FORK_TKN:
	case JOIN_TKN:
	case START_TKN:
	case NEG_ONLY_TKN:
		return no_match;
	}

	assert(!"Reached end of function we should never hit");
	return no_match;
}

/*
 * IPv4 匹配允许出现的字符集：地址用前者；前缀额外允许斜杠，用后者。
 */
#define IPV4_ADDR_STR   "0123456789."
#define IPV4_PREFIX_STR "0123456789./"

/*
 * match_ipv4 —— 校验输入是否是点分十进制的 IPv4 地址（如 10.0.0.1）。
 * 逐段解析：点号不超过 3 个、每段 1~3 位数字、数值 <= 255、不允许前导零（"0" 除外）。
 * 段数不足 4 视为 partly_match（可继续输入），满 4 段且格式正确为 exact_match。
 */
static enum match_type match_ipv4(const char *str)
{
	const char *sp;
	int dots = 0, nums = 0;
	char buf[4];

	for (;;) {
		memset(buf, 0, sizeof(buf));
		sp = str;
		while (*str != '\0') {
			if (*str == '.') {
				if (dots >= 3)
					return no_match;

				if (*(str + 1) == '.')
					return no_match;

				if (*(str + 1) == '\0')
					return partly_match;

				dots++;
				break;
			}
			if (!isdigit((unsigned char)*str))
				return no_match;

			str++;
		}

		if (str - sp > 3)
			return no_match;

		memcpy(buf, sp, str - sp);

		int v = atoi(buf);

		if (v > 255)
			return no_match;
		if (v > 0 && buf[0] == '0')
			return no_match;

		nums++;

		if (*str == '\0')
			break;

		str++;
	}

	if (nums < 4)
		return partly_match;

	return exact_match;
}

/*
 * match_ipv4_prefix —— 校验输入是否是 IPv4 前缀（如 10.0.0.0/24）。
 * 前半段与 match_ipv4 类似地解析点分地址；第 4 段之后可跟 “/掩码”，掩码为十进制
 * 且不得超过 32（IPV4_MAX_BITLEN）。地址不完整或掩码缺位返回 partly_match。
 */
static enum match_type match_ipv4_prefix(const char *str)
{
	const char *sp;
	int dots = 0;
	char buf[4];

	for (;;) {
		memset(buf, 0, sizeof(buf));
		sp = str;
		while (*str != '\0' && *str != '/') {
			if (*str == '.') {
				if (dots == 3)
					return no_match;

				if (*(str + 1) == '.' || *(str + 1) == '/')
					return no_match;

				if (*(str + 1) == '\0')
					return partly_match;

				dots++;
				break;
			}

			if (!isdigit((unsigned char)*str))
				return no_match;

			str++;
		}

		if (str - sp > 3)
			return no_match;

		memcpy(buf, sp, str - sp);

		int v = atoi(buf);

		if (v > 255)
			return no_match;
		if (v > 0 && buf[0] == '0')
			return no_match;

		if (dots == 3) {
			if (*str == '/') {
				if (*(str + 1) == '\0')
					return partly_match;

				str++;
				break;
			} else if (*str == '\0')
				return partly_match;
		}

		if (*str == '\0')
			return partly_match;

		str++;
	}

	sp = str;
	while (*str != '\0') {
		if (!isdigit((unsigned char)*str))
			return no_match;

		str++;
	}

	if (atoi(sp) > IPV4_MAX_BITLEN)
		return no_match;

	return exact_match;
}

/*
 * IPv6 匹配字符集（地址 / 前缀），以及 match_ipv6_prefix 状态机的状态常量：
 * START 起始、COLON 单冒号后、DOUBLE 双冒号(::)后、ADDR 十六进制段内、
 * DOT 内嵌 IPv4 点分、SLASH 出现斜杠、MASK 进入掩码解析。
 */
#define IPV6_ADDR_STR   "0123456789abcdefABCDEF:."
#define IPV6_PREFIX_STR "0123456789abcdefABCDEF:./"
#define STATE_START     1
#define STATE_COLON     2
#define STATE_DOUBLE    3
#define STATE_ADDR      4
#define STATE_DOT       5
#define STATE_SLASH     6
#define STATE_MASK      7

/*
 * match_ipv6_prefix —— 校验输入是 IPv6 地址（prefix=false）还是前缀（prefix=true）。
 * 用状态机（STATE_START / COLON / DOUBLE / ADDR / DOT / SLASH / MASK）逐字符解析，
 * 统计冒号数与段数，校验 “::” 只出现一次、每段不超过 4 位十六进制等规则。
 * prefix=false 时最终用 inet_pton 复核；prefix=true 时还需 “/” 后接合法掩码
 * （0 ~ 128，IPV6_MAX_BITLEN）。不完整返回 partly_match，非法返回 no_match。
 */
static enum match_type match_ipv6_prefix(const char *str, bool prefix)
{
	int state = STATE_START;
	int colons = 0, nums = 0, double_colon = 0;
	int mask;
	const char *sp = NULL, *start = str;
	char *endptr = NULL;

	if (str == NULL)
		return partly_match;

	if (strspn(str, prefix ? IPV6_PREFIX_STR : IPV6_ADDR_STR)
	    != strlen(str))
		return no_match;

	while (*str != '\0' && state != STATE_MASK) {
		switch (state) {
		case STATE_START:
			if (*str == ':') {
				if (*(str + 1) != ':' && *(str + 1) != '\0')
					return no_match;
				colons--;
				state = STATE_COLON;
			} else {
				sp = str;
				state = STATE_ADDR;
			}

			continue;
		case STATE_COLON:
			colons++;
			if (*(str + 1) == '/')
				return no_match;
			else if (*(str + 1) == ':')
				state = STATE_DOUBLE;
			else {
				sp = str + 1;
				state = STATE_ADDR;
			}
			break;
		case STATE_DOUBLE:
			if (double_colon)
				return no_match;

			if (*(str + 1) == ':')
				return no_match;
			else {
				if (*(str + 1) != '\0' && *(str + 1) != '/')
					colons++;
				sp = str + 1;

				if (*(str + 1) == '/')
					state = STATE_SLASH;
				else
					state = STATE_ADDR;
			}

			double_colon++;
			nums += 1;
			break;
		case STATE_ADDR:
			if (*(str + 1) == ':' || *(str + 1) == '.'
			    || *(str + 1) == '\0' || *(str + 1) == '/') {
				if (str - sp > 3)
					return no_match;

				for (; sp <= str; sp++)
					if (*sp == '/')
						return no_match;

				nums++;

				if (*(str + 1) == ':')
					state = STATE_COLON;
				else if (*(str + 1) == '.') {
					if (colons || double_colon)
						state = STATE_DOT;
					else
						return no_match;
				} else if (*(str + 1) == '/')
					state = STATE_SLASH;
			}
			break;
		case STATE_DOT:
			state = STATE_ADDR;
			break;
		case STATE_SLASH:
			if (*(str + 1) == '\0')
				return partly_match;

			state = STATE_MASK;
			break;
		default:
			break;
		}

		if (nums > 11)
			return no_match;

		if (colons > 7)
			return no_match;

		str++;
	}

	if (!prefix) {
		struct sockaddr_in6 sin6_dummy;
		int ret = inet_pton(AF_INET6, start, &sin6_dummy.sin6_addr);
		return ret == 1 ? exact_match : partly_match;
	}

	if (state < STATE_MASK)
		return partly_match;

	mask = strtol(str, &endptr, 10);
	if (*endptr != '\0')
		return no_match;

	if (mask < 0 || mask > IPV6_MAX_BITLEN)
		return no_match;

	return exact_match;
}

/*
 * match_range —— 校验输入是否落在 token->min / token->max 指定的整数区间内。
 * 用 strtoll 解析，若尾随非数字字符或数值越界则 no_match，否则 exact_match。
 */
static enum match_type match_range(struct cmd_token *token, const char *str)
{
	assert(token->type == RANGE_TKN);

	char *endptr = NULL;
	long long val;

	val = strtoll(str, &endptr, 10);
	if (*endptr != '\0')
		return no_match;

	if (val < token->min || val > token->max)
		return no_match;
	else
		return exact_match;
}

/*
 * match_word —— 字面词的匹配（支持前缀缩写）。
 * 输入为空 -> partly_match；输入是完整词的前缀（更短）-> partly_match；
 * 等长且完全相同 -> exact_match；其余 no_match。
 */
static enum match_type match_word(struct cmd_token *token, const char *word)
{
	assert(token->type == WORD_TKN);

	// if the passed token is 0 length, partly match
	if (!strlen(word))
		return partly_match;

	// if the passed token is strictly a prefix of the full word, partly
	// match
	if (strlen(word) < strlen(token->text))
		return !strncmp(token->text, word, strlen(word)) ? partly_match
								 : no_match;

	// if they are the same length and exactly equal, exact match
	else if (strlen(word) == strlen(token->text))
		return !strncmp(token->text, word, strlen(word)) ? exact_match
								 : no_match;

	return no_match;
}

/*
 * match_variable —— 变量型 token（VARIABLE_TKN）匹配任意非空输入，恒为 exact_match。
 * 注意其“是否为合法值”（如接口名是否存在）由补全 / 校验阶段负责，匹配层不做判断。
 */
static enum match_type match_variable(struct cmd_token *token, const char *word)
{
	assert(token->type == VARIABLE_TKN);
	return exact_match;
}

/* MAC 地址允许出现的字符：十六进制数字与冒号。 */
#define MAC_CHARS "ABCDEFabcdef0123456789:"

/*
 * match_mac —— 校验输入是 MAC 地址（prefix=false）还是 MAC 前缀（prefix=true）。
 * 期望形如 “xx:xx:xx:xx:xx:xx”，共 12 位十六进制数字与 5 个冒号；前缀形式可再跟
 * “/掩码”，掩码为十进制且不超过 48。地址未写完返回 partly_match，格式非法 no_match。
 */
static enum match_type match_mac(const char *word, bool prefix)
{
	/* 6 2-digit hex numbers separated by 5 colons */
	size_t mac_explen = 6 * 2 + 5;
	/* '/' + 2-digit integer */
	size_t mask_len = 1 + 2;
	unsigned int i;
	char *eptr;
	unsigned int maskval;

	/* length check */
	if (strlen(word) > mac_explen + (prefix ? mask_len : 0))
		return no_match;

	/* address check */
	for (i = 0; i < mac_explen; i++) {
		if (word[i] == '\0' || !strchr(MAC_CHARS, word[i]))
			break;
		if (((i + 1) % 3 == 0) != (word[i] == ':'))
			return no_match;
	}

	/* incomplete address */
	if (i < mac_explen && word[i] == '\0')
		return partly_match;
	else if (i < mac_explen)
		return no_match;

	/* mask check */
	if (prefix && word[i] == '/') {
		if (word[++i] == '\0')
			return partly_match;

		maskval = strtoul(&word[i], &eptr, 10);
		if (*eptr != '\0' || maskval > 48)
			return no_match;
	} else if (prefix && word[i] == '\0') {
		return partly_match;
	} else if (prefix) {
		return no_match;
	}

	return exact_match;
}
