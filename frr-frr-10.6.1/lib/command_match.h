// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Input matching routines for CLI backend.
 *
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */

/*
 * 【文件导读】command_match.h —— 命令图上的“匹配”与“补全”接口
 *
 * 命令行后端把用户输入切成一串 token（vline），拿到某个节点已构建好的命令图
 * （cmdgraph）后，由本模块负责两件事：
 *   command_match   —— 判断整行输入能否命中图中的某条命令；命中则回传
 *                      匹配到的 cmd_element 以及按序命中的 token 列表 argv。
 *   command_complete—— 为未输入完的一行生成“下一个可取词”的候选列表，
 *                      供 “?” 与 TAB 补全使用。
 *
 * 两者都从图的 START_TKN 出发，沿有向边逐 token 前进：
 *   - 完全命中并到达 END_TKN  -> MATCHER_OK；
 *   - 只吃掉部分输入          -> MATCHER_INCOMPLETE；
 *   - 同时命中多条且无法区分  -> MATCHER_AMBIGUOUS；
 *   - 完全对不上              -> MATCHER_NO_MATCH。
 *
 * 易踩的坑：
 *   - command_match 返回的 element 是启动时初始化的静态实例，不能释放或改写；
 *     而 argv 里每个 token 的 arg 字段是输入副本，可以安全地改。
 *   - command_complete 的语义依赖 vline 末元素是否为 NULL：为 NULL 表示
 *     “上一个词已完整、后面该接什么”，非 NULL 则表示“当前词还没打完”。
 */

#ifndef _ZEBRA_COMMAND_MATCH_H
#define _ZEBRA_COMMAND_MATCH_H

#include "graph.h"
#include "linklist.h"
#include "command.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * enum matcher_rv —— 匹配/补全的整体结论。
 * MATCHER_OK 完全匹配；MATCHER_INCOMPLETE 输入尚不完整；
 * MATCHER_AMBIGUOUS 存在歧义；MATCHER_NO_MATCH 完全无法匹配。
 */
/* matcher result value */
enum matcher_rv {
	MATCHER_NO_MATCH,
	MATCHER_INCOMPLETE,
	MATCHER_AMBIGUOUS,
	MATCHER_OK,
};

/*
 * enum match_type —— 单个 token 与用户输入的匹配程度。
 * trivial_match 输入为空；no_match 对不上；partly_match 命中但不完整；
 * exact_match 完全命中。补全逻辑据此决定是列出候选还是直接补全。
 */
/* completion match types */
enum match_type {
	trivial_match, // the input is null
	no_match,      // the input does not match
	partly_match,  // the input matches but is incomplete
	exact_match    // the input matches and is complete
};

/*
 * MATCHER_ERROR —— 判断某个 matcher_rv 是否属于“错误/未成功”一类
 * （不完整 / 无匹配 / 歧义）。解析末尾常用它做统一错误检查。
 */
/* Defines which matcher_rv values constitute an error. Should be used with
 * matcher_rv return values to do basic error checking.
 */
#define MATCHER_ERROR(matcher_rv)                                              \
	((matcher_rv) == MATCHER_INCOMPLETE                                    \
	 || (matcher_rv) == MATCHER_NO_MATCH                                   \
	 || (matcher_rv) == MATCHER_AMBIGUOUS)

/*
 * command_match —— 尝试为一整行用户输入找到精确匹配的命令。
 *
 * cmdgraph：待匹配的命令图；vline：已切词的用户输入向量。
 * 若成功命中，*argv 指向按序命中的 token 列表（每个 token 的 arg 是该词副本，
 * 可安全修改/释放），*element 指向命中的 cmd_element（是启动时的静态实例，
 * 不可释放或改写）。返回值给出整体匹配结论，MATCHER_ERROR 为真时 *element 为 NULL。
 */
/**
 * Attempt to find an exact command match for a line of user input.
 *
 * @param[in] cmdgraph command graph to match against
 * @param[in] vline vectorized input string
 * @param[out] argv pointer to argument list if successful match, NULL
 * otherwise. The elements of this list are pointers to struct cmd_token
 * and represent the sequence of tokens matched by the input. The ->arg
 * field of each token points to a copy of the input matched on it. These
 * may be safely deleted or modified.
 * @param[out] element pointer to matched cmd_element if successful match,
 * or NULL when MATCHER_ERROR(rv) is true. The cmd_element may *not* be
 * safely deleted or modified; it is the instance initialized on startup.
 * @return matcher status
 */
enum matcher_rv command_match(struct graph *cmdgraph, vector vline,
			      struct list **argv,
			      const struct cmd_element **element);

/*
 * command_complete —— 为一行未打完的用户输入生成候选补全。
 *
 * cmdgraph：待匹配的命令图；vline：已切词的用户输入；*completions 回传
 * 可接受的“下一个 token”列表。注意 vline 末元素是否为 NULL 会改变语义：
 * 为 NULL 表示该词已完整、需补“后面能接什么”；非 NULL 表示当前词尚未打完，
 * 返回它的所有可能续写（例如输入 “en” 会给出 “encapv4”/“encapv6” 等）。
 */
/**
 * Compiles possible completions for a given line of user input.
 *
 * @param[in] start the start node of the DFA to match against
 * @param[in] vline vectorized input string
 * @param[out] completions pointer to list of cmd_token representing
 * acceptable next inputs, or NULL when MATCHER_ERROR(rv) is true.
 * The elements of this list are pointers to struct cmd_token and take on a
 * variety of forms depending on the passed vline. If the last element in vline
 * is NULL, all previous elements are considered to be complete words (the case
 * when a space is the last token of the line) and completions are generated
 * based on what could follow that input. If the last element in vline is not
 * NULL and each sequential element matches the corresponding tokens of one or
 * more commands exactly (e.g. 'encapv4' and not 'en') the same result is
 * generated. If the last element is not NULL and the best possible match is a
 * partial match, then the result generated will be all possible continuations
 * of that element (e.g. 'encapv4', 'encapv6', etc for input 'en').
 * @return matcher status
 */
enum matcher_rv command_complete(struct graph *cmdgraph, vector vline,
				 struct list **completions);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_COMMAND_MATCH_H */
