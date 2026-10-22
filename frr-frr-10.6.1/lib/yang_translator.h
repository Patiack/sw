// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * 文件导读：YANG 模型翻译层接口（yang_translator.h）
 *
 * FRR 可以加载“外部模型”（例如 OpenConfig、IETF 标准模型），并把这些模型的
 * 配置在外部模型与 FRR 原生模型之间双向翻译。本组文件实现这一转换，处于配置
 * 处理管线中：
 *   - 每个翻译器（yang_translator）对应一个模型家族，自带独立的 libyang 上下文
 *   - mappings 保存“外部路径 与 原生路径”的映射表，按方向分两张
 *   - 配置进入时对外部数据做 TO_NATIVE 翻译，出去时做 FROM_NATIVE 翻译
 *
 * 关键类型：yang_tmodule 记录单个模块的覆盖率统计，yang_translator 是核心句柄。
 * 翻译结果用 enum yang_translate_result 表达。
 */
#ifndef _FRR_YANG_TRANSLATOR_H_
#define _FRR_YANG_TRANSLATOR_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * YANG_TRANSLATE_TO_NATIVE 与 YANG_TRANSLATE_FROM_NATIVE - 翻译方向：外部转原生、
 * 原生转外部；YANG_TRANSLATE_MAX 是方向数量，用于给 mappings 数组定维。
 */
#define YANG_TRANSLATE_TO_NATIVE 0
#define YANG_TRANSLATE_FROM_NATIVE 1
#define YANG_TRANSLATE_MAX 2

/*
 * yang_tmodule - 单个被翻译模块的统计信息。
 * module 是外部模块，deviations 是其偏差模块，nodes_before_deviations 与
 * nodes_after_deviations 记录应用偏差前后的节点数，coverage 是映射覆盖率。
 */
struct yang_tmodule {
	const struct lys_module *module;
	const struct lys_module *deviations;
	uint32_t nodes_before_deviations;
	uint32_t nodes_after_deviations;
	double coverage;
};

/*
 * yang_translator - 一个模型家族的翻译器句柄。
 * family 为家族名（如 ietf、openconfig）；ly_ctx 是该翻译器专用的 libyang 上下文；
 * modules 是其中加载的模块链表；mappings 按方向存放外部与原生路径的映射表。
 */
struct yang_translator {
	RB_ENTRY(yang_translator) entry;
	char family[32];
	struct ly_ctx *ly_ctx;
	struct list *modules;
	struct hash *mappings[YANG_TRANSLATE_MAX];
};
/*
 * RB_HEAD 与 RB_PROTOTYPE - 声明 yang_translators 红黑树的头部与操作原型，
 * 实现由 yang_translator.c 中的 RB_GENERATE 生成。
 */
RB_HEAD(yang_translators, yang_translator);
RB_PROTOTYPE(yang_translators, yang_translator, entry, yang_translator_compare);

/*
 * yang_translate_result - 翻译结果：成功、未找到映射、或发生错误。
 */
enum yang_translate_result {
	YANG_TRANSLATE_SUCCESS,
	YANG_TRANSLATE_NOTFOUND,
	YANG_TRANSLATE_FAILURE,
};

/*
 * yang_translators - 所有已加载翻译器的红黑树，定义在 yang_translator.c。
 */
/* Tree of all loaded YANG module translators. */
extern struct yang_translators yang_translators;

/*
 * Load a YANG module translator from a JSON file.
 *
 * path
 *    Absolute path to the module translator file.
 *
 * Returns:
 *    Pointer to newly created YANG module translator, or NULL in the case of an
 *    error.
 */
/*
 * yang_translator_load - 从 JSON 文件加载一个模块翻译器，失败返回 NULL。
 */
extern struct yang_translator *yang_translator_load(const char *path);

/*
 * Unload a YANG module translator.
 *
 * translator
 *    Pointer to the YANG module translator.
 */
/*
 * yang_translator_unload - 卸载一个模块翻译器并释放其资源。
 */
extern void yang_translator_unload(struct yang_translator *translator);

/*
 * Find a YANG module translator by its family name.
 *
 * family
 *    Family of the YANG module translator (e.g. ietf, openconfig).
 *
 * Returns:
 *    Pointer to the YANG module translator if found, NULL otherwise.
 */
/*
 * yang_translator_find - 按家族名查找翻译器，找不到返回 NULL。
 */
extern struct yang_translator *yang_translator_find(const char *family);

/*
 * Translate an XPath expression.
 *
 * translator
 *    Pointer to YANG module translator.
 *
 * dir
 *    Direction of the translation (either YANG_TRANSLATE_TO_NATIVE or
 *    YANG_TRANSLATE_FROM_NATIVE).
 *
 * xpath
 *    Pointer to previously allocated buffer containing the xpath expression to
 *    be translated.
 *
 * xpath_len
 *    Size of the xpath buffer.
 *
 * Returns:
 *    - YANG_TRANSLATE_SUCCESS on success.
 *    - YANG_TRANSLATE_NOTFOUND when there's no available mapping to perform
 *      the translation.
 *    - YANG_TRANSLATE_FAILURE when an error occurred during the translation.
 */
/*
 * yang_translate_xpath - 翻译一个 xpath；返回 SUCCESS / NOTFOUND / FAILURE。
 * 结果就地写回 xpath 缓冲，因此缓冲必须可写且足够长。
 */
extern enum yang_translate_result
yang_translate_xpath(const struct yang_translator *translator, int dir,
		     char *xpath, size_t xpath_len);

/*
 * Translate an entire libyang data node.
 *
 * translator
 *    Pointer to YANG module translator.
 *
 * dir
 *    Direction of the translation (either YANG_TRANSLATE_TO_NATIVE or
 *    YANG_TRANSLATE_FROM_NATIVE).
 *
 * dnode
 *    libyang schema node we want to translate.
 *
 * Returns:
 *    - YANG_TRANSLATE_SUCCESS on success.
 *    - YANG_TRANSLATE_FAILURE when an error occurred during the translation.
 */
/*
 * yang_translate_dnode - 翻译一整棵 libyang 数据节点；出错返回 FAILURE。
 */
extern int yang_translate_dnode(const struct yang_translator *translator,
				int dir, struct lyd_node **dnode);

/*
 * Initialize the YANG module translator subsystem. Should be called only once
 * during the daemon initialization process.
 */
/*
 * yang_translator_init - 初始化翻译子系统，每个守护进程启动时只调用一次。
 */
extern void yang_translator_init(void);

/*
 * Finish the YANG module translator subsystem gracefully. Should be called only
 * when the daemon is exiting.
 */
/*
 * yang_translator_terminate - 优雅收尾翻译子系统，仅在守护进程退出时调用。
 */
extern void yang_translator_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_YANG_TRANSLATOR_H_ */
