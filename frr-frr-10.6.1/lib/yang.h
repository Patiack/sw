// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * 文件导读：libyang 封装层的公共接口（yang.h）
 *
 * 本头文件声明 yang.c（libyang 封装层）对外暴露的类型、宏与函数，是上层
 * northbound、各守护进程以及 yang_wrappers 共同依赖的接口面。层次关系：
 *   - yang.c 实现这些接口，直接操作 libyang 的 schema 节点与数据节点
 *   - yang_wrappers.h 在此基础之上提供“取值 / 设值”的便捷包装
 *   - yang_translator.h 提供模型之间的转换接口
 *
 * 关键类型：
 *   - struct yang_module：一个已加载的 YANG 模块及其 FRR 侧元信息
 *   - struct yang_module_embed：编译期嵌入的模块描述
 *   - struct yang_data：路径加取值字符串，用于配置与状态数据的传递
 *   - struct yang_list_keys：list 各 key 的字符串取值
 *   - enum yang_iter_flags：schema 遍历时的过滤标志
 *
 * 命名约定：yang_snode_* 操作 schema 节点，yang_dnode_* 操作数据节点，
 *   yang_data_* 与 yang_state_* 操作北向的数据容器与状态树。
 */
#ifndef _FRR_YANG_H_
#define _FRR_YANG_H_

#include "memory.h"

#include <libyang/libyang.h>
#ifdef HAVE_SYSREPO
#include <sysrepo.h>
#endif

#include "yang_wrappers.h"

#ifdef __cplusplus
/*
 * extern "C" - 以 C 链接方式导出符号，便于 C++ 代码调用本头文件中的接口。
 */
extern "C" {
#endif

#include <libyang/version.h>
/*
 * 以下用条件编译抹平 libyang 主版本之间的宏差异：打印选项改名、LYB 跳过检查选项改名，
 * 以及父节点类型在不同版本下的表示方式。
 */
#if (LY_VERSION_MAJOR >= 4)
#define LYD_PRINT_WITHSIBLINGS LYD_PRINT_SIBLINGS
#endif

#if (LY_VERSION_MAJOR >= 5)
#define LYD_PARSE_LYB_SKIP_CTX_CHECK LYD_PARSE_LYB_SKIP_MODULE_CHECK
#define LYD_API_PARENT_TYPE(p)	     (p)
#else
#define LYD_API_PARENT_TYPE(p) ((struct lyd_node_inner *)(p))
#endif

/*
 * frr_yang_module_info 前置声明：完整定义在 northbound 头文件中，这里只需要指针。
 */
struct frr_yang_module_info;

/*
 * 以下为一组长度上限常量：XPath 缓冲、list key 数量与单个 key 长度、
 * YANG 取值字符串最大长度。统一引用可避免散落的魔法数字。
 */
/* Maximum XPath length. */
#define XPATH_MAXLEN 1024

/* Maximum list key length. */
#define LIST_MAXKEYS 8

/* Maximum list key length. */
#define LIST_MAXKEYLEN 128

/* Maximum string length of an YANG value. */
#define YANG_VALUE_MAXLEN 1024

/*
 * yang_module_embed - 编译期嵌入的 YANG 模块描述。
 * 由 yang_module_embed() 串成单链表；mod_name 与 mod_rev 标识主模块，
 * sub_mod_name 与 sub_mod_rev 标识子模块，data 与 format 是模型正文及其格式。
 */
struct yang_module_embed {
	struct yang_module_embed *next;
	const char *mod_name, *mod_rev;
	const char *sub_mod_name;
	const char *sub_mod_rev;
	const char *data;
	LYS_INFORMAT format;
};

/*
 * yang_module - 一个已加载的 YANG 模块在 FRR 侧的登记项。
 * entry 用于挂到全局 RB 树；name 为模块名；info 指向 libyang 的模块对象；
 * frr_info 关联 FRR 提供的回调信息；sysrepo 相关字段仅在启用时存在。
 */
struct yang_module {
	RB_ENTRY(yang_module) entry;
	const char *name;
	const struct lys_module *info;
	const struct frr_yang_module_info *frr_info;
#ifdef HAVE_SYSREPO
	sr_subscription_ctx_t *sr_subscription;
	struct event *sr_thread;
#endif
};
/*
 * RB_HEAD 与 RB_PROTOTYPE - 声明 yang_modules 红黑树的头部类型与操作原型，
 * 其实现位于 yang.c，由 RB_GENERATE 生成。
 */
RB_HEAD(yang_modules, yang_module);
RB_PROTOTYPE(yang_modules, yang_module, entry, yang_module_compare);

/*
 * yang_data - 一条北向数据（路径加取值）。
 * xpath 为定长缓冲（超长会截断），value 为堆上分配的原始字符串或 NULL。
 */
struct yang_data {
	/* XPath identifier of the data element. */
	char xpath[XPATH_MAXLEN];

	/* Value encoded as a raw string. */
	char *value;
};

/*
 * yang_list_keys - list 各 key 的字符串取值集合。
 * num 为 key 数量（上限 LIST_MAXKEYS），key 为二维数组存放各 key 的取值。
 */
struct yang_list_keys {
	/* Number os keys (max: LIST_MAXKEYS). */
	uint8_t num;

	/* Value encoded as a raw string. */
	char key[LIST_MAXKEYS][LIST_MAXKEYLEN];
};

/*
 * yang_path_type - 路径类型：SCHEMA 表示生成 schema 路径，DATA 表示生成数据路径。
 */
enum yang_path_type {
	YANG_PATH_SCHEMA = 0,
	YANG_PATH_DATA,
};

/*
 * yang_iter_flags - 遍历过滤标志：可分别过滤掉非 presence 容器、list 的 key 叶子、
 * 以及 RPC 的输入与输出节点。
 */
enum yang_iter_flags {
	/* Filter non-presence containers. */
	YANG_ITER_FILTER_NPCONTAINERS = (1<<0),

	/* Filter list keys (leafs). */
	YANG_ITER_FILTER_LIST_KEYS = (1<<1),

	/* Filter RPC input/output nodes. */
	YANG_ITER_FILTER_INPUT_OUTPUT = (1<<2),
};

/*
 * yang_iterate_cb - schema 遍历回调原型。
 * 返回值用 YANG_ITER_* 控制继续或停止，见下方两个宏。
 */
/* Callback used by the yang_snodes_iterate_*() family of functions. */
typedef int (*yang_iterate_cb)(const struct lysc_node *snode, void *arg);

/*
 * yang_dnode_iter_cb - 数据节点遍历回调原型，含义同 yang_iterate_cb。
 */
/* Callback used by the yang_dnode_iterate() function. */
typedef int (*yang_dnode_iter_cb)(const struct lyd_node *dnode, void *arg);

/*
 * YANG_ITER_CONTINUE 与 YANG_ITER_STOP - 遍历回调的返回值：前者继续，后者停止。
 */
/* Return values of the 'yang_iterate_cb' callback. */
#define YANG_ITER_CONTINUE 0
#define YANG_ITER_STOP -1

/*
 * ly_native_ctx - 全局 libyang 上下文，定义在 yang.c，所有原生模块加载其中。
 */
/* Global libyang context for native FRR models. */
extern struct ly_ctx *ly_native_ctx;

/*
 * yang_modules - 所有已加载模块的红黑树，定义在 yang.c。
 */
/* Tree of all loaded YANG modules. */
extern struct yang_modules yang_modules;

/*
 * Create a new YANG module and load it using libyang. If the YANG module is not
 * found in the YANG_MODELS_PATH directory, the program will exit with an error.
 * Once loaded, a YANG module can't be unloaded anymore.
 *
 * module_name
 *    Name of the YANG module.
 *
 * features
 *    NULL-terminated array of feature names to enable.
 *    If NULL, all features are disabled.
 *    To enable all features, use ["*", NULL].
 *
 * Returns:
 *    Pointer to newly created YANG module.
 */
/*
 * yang_module_load - 加载一个 YANG 模块（实现见 yang.c）。
 * 失败或重复加载会 exit(1)；features 传 NULL 表示不启用任何 feature。
 */
extern struct yang_module *yang_module_load(const char *module_name,
					    const char **features);

/*
 * Load all FRR native YANG models.
 */
/*
 * yang_module_load_all - 加载全部 FRR 原生模块。
 */
extern void yang_module_load_all(void);

/*
 * Find a YANG module by its name.
 *
 * module_name
 *    Name of the YANG module.
 *
 * Returns:
 *    Pointer to YANG module if found, NULL otherwise.
 */
/*
 * yang_module_find - 按名字查找已加载模块，返回模块指针或 NULL。
 */
extern struct yang_module *yang_module_find(const char *module_name);

/*
 * Register a YANG module embedded in the binary file.  Should be called
 * from a constructor function.
 *
 * embed
 *    YANG module embedding structure to register.  (static global provided
 *    by caller.)
 */
/*
 * yang_module_embed - 注册一个内嵌模块，通常由构造函数在启动时调用。
 * 调用方需保证 embed 结构长期有效。
 */
extern void yang_module_embed(struct yang_module_embed *embed);

/*
 * Iterate recursively over all children of a schema node.
 *
 * snode
 *    YANG schema node to operate on.
 *
 * module
 *    When set, iterate over all nodes of the specified module only.
 *
 * cb
 *    Function to call with each schema node.
 *
 * flags
 *    YANG_ITER_* flags to control how the iteration is performed.
 *
 * arg
 *    Arbitrary argument passed as the second parameter in each call to 'cb'.
 *
 * Returns:
 *    The return value of the last called callback.
 */
/*
 * yang_snodes_iterate_subtree - 递归遍历某 schema 节点的子树。
 * flags 使用 YANG_ITER_*；返回最后一次回调的返回值。
 */
extern int yang_snodes_iterate_subtree(const struct lysc_node *snode,
				       const struct lys_module *module,
				       yang_iterate_cb cb, uint16_t flags,
				       void *arg);

/*
 * Iterate over all libyang schema nodes from all loaded modules of the
 * given YANG module.
 *
 * module
 *    When set, iterate over all nodes of the specified module only.
 *
 * cb
 *    Function to call with each schema node.
 *
 * flags
 *    YANG_ITER_* flags to control how the iteration is performed.
 *
 * arg
 *    Arbitrary argument passed as the second parameter in each call to 'cb'.
 *
 * Returns:
 *    The return value of the last called callback.
 */
/*
 * yang_snodes_iterate - 遍历所有已加载模块的 schema 节点，返回最后一次回调的返回值。
 */
extern int yang_snodes_iterate(const struct lys_module *module,
			       yang_iterate_cb cb, uint16_t flags, void *arg);

/*
 * Build schema path or data path of the schema node.
 *
 * snode
 *    libyang schema node to be processed.
 *
 * type
 *    Specify whether a schema path or a data path should be built.
 *
 * xpath
 *    Pointer to previously allocated buffer.
 *
 * xpath_len
 *    Size of the xpath buffer.
 */
/*
 * yang_snode_get_path - 生成 schema 节点路径，type 决定 schema 路径或数据路径。
 */
extern void yang_snode_get_path(const struct lysc_node *snode,
				enum yang_path_type type, char *xpath,
				size_t xpath_len);


/*
 * Find libyang schema node for the given xpath. Uses `lys_find_xpath`,
 * returning only the first of a set of nodes -- normally there should only
 * be one.
 *
 * ly_ctx
 *    libyang context to operate on.
 *
 * xpath
 *    XPath expression (absolute or relative) to find the schema node for.
 *
 * options
 *    Libyang findxpathoptions value (see lys_find_xpath).
 *
 * Returns:
 *    The libyang schema node if found, or NULL if not found.
 */
/*
 * yang_find_snode - 按 xpath 查找 schema 节点，返回第一个匹配节点或 NULL。
 */
extern struct lysc_node *yang_find_snode(struct ly_ctx *ly_ctx,
					 const char *xpath, uint32_t options);

/*
 * Find first parent schema node which is a presence-container or a list
 * (non-presence containers are ignored).
 *
 * snode
 *    libyang schema node to operate on.
 *
 * Returns:
 *    The parent libyang schema node if found, or NULL if not found.
 */
/*
 * yang_snode_real_parent - 找到最近的 presence 容器或 list 父节点。
 */
extern struct lysc_node *yang_snode_real_parent(const struct lysc_node *snode);

/*
 * Find first parent schema node which is a list.
 *
 * snode
 *    libyang schema node to operate on.
 *
 * Returns:
 *    The parent libyang schema node (list) if found, or NULL if not found.
 */
/*
 * yang_snode_parent_list - 找到最近的 list 类型父节点。
 */
extern struct lysc_node *yang_snode_parent_list(const struct lysc_node *snode);

/*
 * Check if the libyang schema node represents typeless data (e.g. containers,
 * leafs of type empty, etc).
 *
 * snode
 *    libyang schema node to operate on.
 *
 * Returns:
 *    true if the schema node represents typeless data, false otherwise.
 */
/*
 * yang_snode_is_typeless_data - 判断节点是否为“无类型的叶子数据”。
 */
extern bool yang_snode_is_typeless_data(const struct lysc_node *snode);

/*
 * Get the default value associated to a YANG leaf or leaf-list.
 *
 * snode
 *    libyang schema node to operate on.
 *
 * Returns:
 *    The default value if it exists, NULL otherwise.
 */
/*
 * yang_snode_get_default - 取叶子或 leaf-list 的默认值，没有则返回 NULL。
 */
extern const char *yang_snode_get_default(const struct lysc_node *snode);

/*
 * Get the type structure of a leaf of leaf-list. If the type is a leafref, the
 * final (if there is a chain of leafrefs) target's type is found.
 *
 * snode
 *    libyang schema node to operate on.
 *
 * Returns:
 *    The found type if the schema node represents a leaf or a leaf-list, NULL
 *    otherwise.
 */
/*
 * yang_snode_get_type - 取叶子或 leaf-list 的类型；leafref 会解引用到真实类型；
 * 非叶子节点返回 NULL。
 */
extern const struct lysc_type *
yang_snode_get_type(const struct lysc_node *snode);

/*
 * Get the number of key nodes for the given list.
 *
 * snode
 *    libyang (LYS_LIST) schema node to operate on.
 *
 * Returns:
 *    The number of key LYS_LEAFs as children of this list node.
 */
/*
 * yang_snode_num_keys - 统计 list 的 key 数量，非 list 返回 0。
 */
extern unsigned int yang_snode_num_keys(const struct lysc_node *snode);

/*
 * LY_FOR_KEYS - 遍历 list 各 key 叶子的循环宏，用法形如 for 语句。
 * 它从子节点开头逐个前进，遇到第一个非 key 节点即 break，因此只在 key 前缀区间内循环。
 */
#define LY_FOR_KEYS(snode, skey)                                               \
	for ((skey) = (const struct lysc_node_leaf *)lysc_node_child((snode)); \
	     (skey); (skey) = (const struct lysc_node_leaf *)((skey)->next))   \
		if (!lysc_is_key(skey)) {                                      \
			break;                                                 \
		} else


/*
 * Build data path of the data node.
 *
 * dnode
 *    libyang data node to be processed.
 *
 * xpath
 *    Pointer to previously allocated buffer or NULL.
 *
 * xpath_len
 *    Size of the xpath buffer if xpath non-NULL.
 *
 * If xpath is NULL, the returned string (if non-NULL) needs to be free()d by
 * the caller.
 */
/*
 * yang_dnode_get_path - 生成数据节点的数据路径；xpath 为 NULL 时返回需由调用方 free。
 */
extern char *yang_dnode_get_path(const struct lyd_node *dnode, char *xpath,
				size_t xpath_len);

/*
 * Find a libyang data node by its YANG data path.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath
 *    Limited XPath (absolute or relative) string. See Path in libyang
 *    documentation for restrictions.
 *
 * Returns:
 *    The libyang data node if found, or NULL if not found.
 */
/*
 * yang_dnode_get - 按 xpath 查找数据节点；恰好一个时返回它，否则返回 NULL。
 */
extern struct lyd_node *yang_dnode_get(const struct lyd_node *dnode,
				       const char *xpath);

/*
 * Find a libyang data node by its YANG data path.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath_fmt
 *    Limited XPath (absolute or relative) format string. See Path in libyang
 *    documentation for restrictions.
 *
 * ...
 *    any parameters for xpath_fmt.
 *
 * Returns:
 *    The libyang data node if found, or NULL if not found.
 */
/*
 * yang_dnode_getf - yang_dnode_get 的格式化路径变体。
 */
extern struct lyd_node *yang_dnode_getf(const struct lyd_node *dnode,
					const char *path_fmt, ...)
	PRINTFRR(2, 3);

/*
 * Check if a libyang data node exists.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath
 *    Limited XPath (absolute or relative) string. See Path in libyang
 *    documentation for restrictions.
 *
 * Returns:
 *    true if a libyang data node was found, false otherwise.
 */
/*
 * yang_dnode_exists - 判断数据节点是否存在，存在返回 true。
 */
extern bool yang_dnode_exists(const struct lyd_node *dnode, const char *xpath);

/*
 * Check if a libyang data node exists.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath_fmt
 *    Limited XPath (absolute or relative) format string. See Path in
 *    libyang documentation for restrictions.
 *
 * ...
 *    any parameters for xpath_fmt.
 *
 * Returns:
 *    true if a libyang data node was found, false otherwise.
 */
/*
 * yang_dnode_existsf - yang_dnode_exists 的格式化路径变体。
 */
extern bool yang_dnode_existsf(const struct lyd_node *dnode,
			       const char *xpath_fmt, ...) PRINTFRR(2, 3);

/*
 * Iterate over all libyang data nodes that satisfy an XPath query.
 *
 * cb
 *    Function to call with each data node.
 *
 * arg
 *    Arbitrary argument passed as the second parameter in each call to 'cb'.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath_fmt
 *    XPath expression (absolute or relative).
 *
 * ...
 *    any parameters for xpath_fmt.
 */
/*
 * yang_dnode_iterate - 对匹配 xpath 的每个数据节点执行回调，回调返回 STOP 可提前结束。
 */
void yang_dnode_iterate(yang_dnode_iter_cb cb, void *arg,
			const struct lyd_node *dnode, const char *xpath_fmt,
			...) PRINTFRR(4, 5);

/*
 * Count the number of data nodes that satisfy an XPath query.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath_fmt
 *    XPath expression (absolute or relative).
 *
 * ...
 *    any parameters for xpath_fmt.
 */
/*
 * yang_dnode_count - 统计匹配 xpath 的数据节点个数。
 */
uint32_t yang_dnode_count(const struct lyd_node *dnode, const char *xpath_fmt,
			  ...) PRINTFRR(2, 3);

/*
 * Check if the libyang data node contains a default value. Non-presence
 * containers are assumed to always contain a default value.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath
 *    Optional XPath expression (absolute or relative) to specify a different
 *    data node to operate on in the same data tree.
 *
 * Returns:
 *    true if the data node contains the default value, false otherwise.
 */
/*
 * yang_dnode_is_default - 判断数据节点取值是否为默认值；非 presence 容器恒视为默认。
 */
extern bool yang_dnode_is_default(const struct lyd_node *dnode,
				  const char *xpath);

/*
 * Check if the libyang data node contains a default value. Non-presence
 * containers are assumed to always contain a default value.
 *
 * dnode
 *    Base libyang data node to operate on.
 *
 * xpath
 *    Optional limited XPath (absolute or relative) format string. See Path in
 *    libyang documentation for restrictions.
 *
 * ...
 *    any parameters for xpath_fmt.
 *
 * Returns:
 *    true if the data node contains the default value, false otherwise.
 */
/*
 * yang_dnode_is_defaultf - yang_dnode_is_default 的格式化路径变体。
 */
extern bool yang_dnode_is_defaultf(const struct lyd_node *dnode,
				   const char *xpath_fmt, ...) PRINTFRR(2, 3);

/*
 * Check if the libyang data node and all of its children contain default
 * values. Non-presence containers are assumed to always contain a default
 * value.
 *
 * dnode
 *    libyang data node to operate on.
 *
 * Returns:
 *    true if the data node and all of its children contain default values,
 *    false otherwise.
 */
/*
 * yang_dnode_is_default_recursive - 递归判断节点及其所有子孙是否均为默认值。
 */
extern bool yang_dnode_is_default_recursive(const struct lyd_node *dnode);

/*
 * Change the value of a libyang leaf node.
 *
 * dnode
 *    libyang data node to operate on.
 *
 * value
 *    String representing the new value.
 */
/*
 * yang_dnode_change_leaf - 修改叶子节点取值，失败情况在实现里未检查。
 */
extern void yang_dnode_change_leaf(struct lyd_node *dnode, const char *value);

/*
 * Create a new libyang data node.
 *
 * ly_ctx
 *    libyang context to operate on.
 *
 * config
 *    Specify whether the data node will contain only configuration data (true)
 *    or both configuration data and state data (false).
 *
 * Returns:
 *    Pointer to newly created libyang data node.
 */
/*
 * yang_dnode_new - 新建一棵空的已校验数据树；config_only 为真时只保留配置数据。
 */
extern struct lyd_node *yang_dnode_new(struct ly_ctx *ly_ctx, bool config_only);

/*
 * Duplicate a libyang data node.
 *
 * dnode
 *    libyang data node to duplicate.
 *
 * Returns:
 *    Pointer to duplicated libyang data node.
 */
/*
 * yang_dnode_dup - 深拷贝数据节点及其兄弟节点。
 */
extern struct lyd_node *yang_dnode_dup(const struct lyd_node *dnode);

/*
 * Delete a libyang data node.
 *
 * dnode
 *    Pointer to the libyang data node that is going to be deleted along with
 *    the entire tree it belongs to.
 */
/*
 * yang_dnode_free - 释放该节点所属的整棵树。
 */
extern void yang_dnode_free(struct lyd_node *dnode);

/**
 * yang_state_new() - Create new state data.
 * @tree: subtree @path is relative to or NULL in which case @path must be
 *	  absolute.
 * @path: The path of the state node to create.
 * @value: The canonical value of the state.
 *
 * Return: The new libyang node.
 */
/*
 * yang_state_new - 确保 state 路径存在并返回对应数据节点；tree 为 NULL 时 path 需为绝对路径。
 */
extern struct lyd_node *yang_state_new(struct lyd_node *tree, const char *path, const char *value);

/**
 * yang_state_delete() - Delete state data.
 * @tree: subtree @path is relative to or NULL in which case @path must be
 *	  absolute.
 * @path: The path of the state node to delete, or NULL if @tree should just be
 *	  deleted.
 */
/*
 * yang_state_delete - 删除 state 路径或整棵树；path 为 NULL 时删除整棵树。
 */
extern void yang_state_delete(struct lyd_node *tree, const char *path);

/**
 * yang_state_new_pathf() - Create new state data.
 * @tree: subtree @path_fmt is relative to or NULL in which case @path_fmt must
 *        be absolute.
 * @path_fmt: The path format string of the state node to create.
 * @value: The canonical value of the state.
 * @...: The values to substitute into @path_fmt.
 *
 * Return: The new libyang node.
 */
/*
 * yang_state_new_pathf - 路径为可变参数的 yang_state_new 变体。
 */
extern struct lyd_node *yang_state_new_pathf(struct lyd_node *tree, const char *path_fmt,
					     const char *value, ...) PRINTFRR(2, 4);
/*
 * yang_state_new_vpathf - 路径为 va_list 的 yang_state_new 变体。
 */
extern struct lyd_node *yang_state_new_vpathf(struct lyd_node *tree, const char *path_fmt,
					      const char *value, va_list ap);
/**
 * yang_state_delete_pathf() - Delete state data.
 * @tree: subtree @path_fmt is relative to or NULL in which case @path_fmt must
 *	  be absolute.
 * @path: The path of the state node to delete.
 * @...: The values to substitute into @path_fmt.
 */
/*
 * yang_state_delete_pathf - 路径为可变参数的 yang_state_delete 变体。
 */
extern void yang_state_delete_pathf(struct lyd_node *tree, const char *path_fmt, ...) PRINTFRR(2, 3);
/*
 * yang_state_delete_vpathf - 路径为 va_list 的 yang_state_delete 变体。
 */
extern void yang_state_delete_vpathf(struct lyd_node *tree, const char *path_fmt, va_list ap);

/**
 * yang_state_newf() - Create new state data.
 * @tree: subtree @path is relative to or NULL in which case @path must be
 *	  absolute.
 * @path: The path of the state node to create.
 * @val_fmt: The value format string to set the canonical value of the state.
 * @...: The values to substitute into @val_fmt.
 *
 * Return: The new libyang node.
 */
/*
 * yang_state_newf - 取值为可变参数的 yang_state_new 变体。
 */
extern struct lyd_node *yang_state_newf(struct lyd_node *tree, const char *path,
					const char *val_fmt, ...) PRINTFRR(3, 4);

/*
 * yang_state_vnewf - 取值为 va_list 的 yang_state_new 变体。
 */
extern struct lyd_node *yang_state_vnewf(struct lyd_node *tree, const char *path,
					 const char *val_fmt, va_list ap);

/*
 * Add a libyang data node to an RPC/action output container.
 *
 * output
 *    RPC/action output container.
 *
 * xpath
 *    XPath of the data node to add, relative to the output container.
 *
 * value
 *    String representing the value of the data node.
 */
/*
 * yang_dnode_rpc_output_add - 向 rpc 或 action 的输出容器添加一个数据节点。
 */
extern void yang_dnode_rpc_output_add(struct lyd_node *output,
				      const char *xpath, const char *value);

/*
 * Create a new yang_data structure.
 *
 * xpath
 *    Data path of the YANG data.
 *
 * value
 *    String representing the value of the YANG data.
 *
 * Returns:
 *    Pointer to newly created yang_data structure.
 */
/*
 * yang_data_new - 新建一个 yang_data 结构（路径加取值）。
 */
extern struct yang_data *yang_data_new(const char *xpath, const char *value);

/*
 * Delete a yang_data structure.
 *
 * data
 *    yang_data to delete.
 */
/*
 * yang_data_free - 释放一个 yang_data 结构。
 */
extern void yang_data_free(struct yang_data *data);

/*
 * Create a new linked list of yang_data structures. The list 'del' callback is
 * initialized appropriately so that the entire list can be deleted safely with
 * list_delete_and_null().
 *
 * Returns:
 *    Pointer to newly created linked list.
 */
/*
 * yang_data_list_new - 新建 yang_data 链表；del 回调已设好，可整体安全释放。
 */
extern struct list *yang_data_list_new(void);

/*
 * Find the yang_data structure corresponding to an XPath in a list.
 *
 * list
 *    list of yang_data structures to operate on.
 *
 * xpath_fmt
 *    XPath to search for (format string).
 *
 * Returns:
 *    Pointer to yang_data if found, NULL otherwise.
 */
/*
 * yang_data_list_find - 在链表中按 xpath 查找 yang_data。
 */
extern struct yang_data *yang_data_list_find(const struct list *list,
					     const char *xpath_fmt, ...)
	PRINTFRR(2, 3);

/*
 * Create and set up a libyang context (for use by the translator)
 *
 * embedded_modules
 *    Specify whether libyang should attempt to look for embedded YANG modules.
 *
 * explicit_compile
 *    True if the caller will later call ly_ctx_compile to compile all loaded
 *    modules at once.
 * load_library
 *    Set this to have libyang to load/implement the ietf-yang-library.
 */
/*
 * yang_ctx_new_setup - 创建并配置一个 libyang 上下文（供 translator 使用）。
 */
extern struct ly_ctx *yang_ctx_new_setup(bool embedded_modules, bool explicit_compile,
					 bool load_library);

/*
 * Enable or disable libyang verbose debugging.
 *
 * enable
 *    When set to true, enable libyang verbose debugging, otherwise disable it.
 */
/*
 * yang_debugging_set - 打开或关闭 libyang 的详细调试日志。
 */
extern void yang_debugging_set(bool enable);


/*
 * Parse YANG data.
 *
 * Args:
 *	xpath: xpath of the data.
 *	format: LYD_FORMAT of input data.
 *	as_subtree: parse the data as starting at the subtree identified by xpath.
 *	is_oper: parse as operational state allows for invalid (logs warning).
 *	validate: validate the data (otherwise treat as non-final).
 *	data: input data.
 *	notif: pointer to the libyang data tree to store the parsed notification.
 *	       If the notification is not on the top level of the yang model,
 *	       the pointer to the notification node is still returned, but it's
 *	       part of the full data tree with all its parents.
 */
/*
 * yang_parse_data - 解析 YANG 数据，as_subtree 为真时把数据当作 xpath 处的子树解析；
 * is_oper 解析操作数据（允许不合法并告警），validate 决定是否做最终校验。
 */
LY_ERR yang_parse_data(const char *xpath, LYD_FORMAT format, bool as_subtree, bool is_oper,
		       bool validate, const char *data, struct lyd_node **tree);

/*
 * Parse a YANG notification.
 *
 * Args:
 *	xpath: xpath of notification.
 *	format: LYD_FORMAT of input data.
 *	data: input data.
 *	notif: pointer to the libyang data tree to store the parsed notification.
 *	       If the notification is not on the top level of the yang model,
 *	       the pointer to the notification node is still returned, but it's
 *	       part of the full data tree with all its parents.
 */
/*
 * yang_parse_notification - 解析一条 YANG 通知；通知不在顶层时仍返回其节点指针，
 * 但该节点属于含全部父节点的完整树。
 */
extern LY_ERR yang_parse_notification(const char *xpath, LYD_FORMAT format,
				      const char *data, struct lyd_node **notif);

/*
 * Parse a YANG RPC.
 *
 * Args:
 *	xpath: xpath of an RPC/action.
 *	format: LYD_FORMAT of input data.
 *	data: input data.
 *	reply: true if the data represents a reply to an RPC/action.
 *	rpc: pointer to the libyang data tree to store the parsed RPC/action.
 *	     If data represents an action, the pointer to the action node is
 *	     still returned, but it's part of the full data tree with all its
 *	     parents.
 *
 * Returns:
 *	LY_ERR from underlying calls.
 */
/*
 * yang_parse_rpc - 解析 YANG 原生风格的 rpc 或 action；reply 表示解析的是回复数据，
 * action 不在顶层时会返回含父节点的完整树中的 action 节点。
 */
LY_ERR yang_parse_rpc(const char *xpath, LYD_FORMAT format, const char *data,
		      bool reply, struct lyd_node **rpc);
/*
 * yang_parse_restconf_rpc - 解析 RESTCONF 风格的 rpc 或 action 数据。
 */
LY_ERR yang_parse_restconf_rpc(const char *xpath, LYD_FORMAT format, const char *data, bool reply,
			       struct lyd_node **rpc);

/*
 * "Print" the yang tree in `root` into dynamic sized array.
 *
 * Args:
 *	root: root of the subtree to "print" along with siblings.
 *	format: LYD_FORMAT of output (see lyd_print_mem)
 *	options: printing options (see lyd_print_mem)
 *
 * Return:
 *	A darr dynamic array with the "printed" output or NULL on failure.
 */
/*
 * yang_print_tree - 把数据树连同兄弟节点序列化进新分配的 darr；失败返回 NULL。
 */
extern uint8_t *yang_print_tree(const struct lyd_node *root, LYD_FORMAT format, uint32_t options);


/**
 * yang_convert_lyd_format() - convert one libyang format to darr string.
 * @data: data to convert.
 * @data_len: length of the data.
 * @in_format: format of the data.
 * @out_format: format to return.
 * @shrink: true to avoid pretty printing.
 *
 * Return:
 *	A darr based string or NULL for error.
 */
/*
 * yang_convert_lyd_format - 在 libyang 各格式间转换并返回 darr 字符串；出错返回 NULL。
 */
extern char *yang_convert_lyd_format(const char *data, size_t msg_len,
				     LYD_FORMAT in_format,
				     LYD_FORMAT out_format, bool shrink);

/*
 * "Print" the yang tree in `root` into an existing dynamic sized array.
 *
 * This function does not initialize or free the dynamic array, the array can
 * already existing data, the tree will be appended to this data.
 *
 * Args:
 *	darr: existing `uint8_t *`, dynamic array.
 *	root: root of the subtree to "print" along with siblings.
 *	format: LYD_FORMAT of output (see lyd_print_mem)
 *	options: printing options (see lyd_print_mem)
 *
 * Return:
 *	LY_ERR from underlying calls.
 */
/*
 * yang_print_tree_append - 把数据树序列化后追加到既有 darr；不初始化也不释放该数组。
 */
extern LY_ERR yang_print_tree_append(uint8_t **darr, const struct lyd_node *root,
				     LYD_FORMAT format, uint32_t options);

/*
 * Print libyang error messages into the provided buffer.
 *
 * ly_ctx
 *    libyang context to operate on.
 *
 * buf
 *    Buffer to store the libyang error messages.
 *
 * buf_len
 *    Size of buf.
 *
 * Returns:
 *    The provided buffer.
 */
/*
 * yang_print_errors - 把 libyang 上下文的错误信息写入给定缓冲并返回该缓冲。
 */
extern const char *yang_print_errors(struct ly_ctx *ly_ctx, char *buf,
				     size_t buf_len);

/*
 * Initialize the YANG subsystem. Should be called only once during the
 * daemon initialization process.
 *
 * embedded_modules
 *    Specify whether libyang should attempt to look for embedded YANG modules.
 * defer_compile
 *    Hold off on compiling modules until yang_init_loading_complete is called.
 * load_library
 *    Set this to have libyang to load/implement the ietf-yang-library.
 */
/*
 * yang_init - 初始化 YANG 子系统，每个守护进程启动时只应调用一次。
 */
extern void yang_init(bool embedded_modules, bool defer_compile, bool load_library);

/*
 * Should be called after yang_init and all yang_module_load()s have been done,
 * compiles all modules loaded into the yang context.
 */
/*
 * yang_init_loading_complete - 在 yang_init 与所有 yang_module_load 之后调用，统一编译模块。
 */
extern void yang_init_loading_complete(void);

/*
 * Finish the YANG subsystem gracefully. Should be called only when the daemon
 * is exiting.
 */
/*
 * yang_terminate - 优雅收尾 YANG 子系统，仅在守护进程退出时调用。
 */
extern void yang_terminate(void);

/*
 * API to return the parent dnode having a given schema-node name
 * Use case: One has to access the parent dnode's private pointer
 * for a given child node.
 * For that there is a need to find parent dnode first.
 *
 * dnode The starting node to work on
 *
 * name  The name of container/list schema-node
 *
 * Returns The dnode matched with the given name
 */
/*
 * yang_dnode_get_parent - 返回名字匹配的父数据节点；典型用途是先找到父节点再访问其私有指针。
 */
extern const struct lyd_node *
yang_dnode_get_parent(const struct lyd_node *dnode, const char *name);


/*
 * In some cases there is a need to auto delete the parent nodes
 * if the given node is last in the list.
 * It tries to delete all the parents in a given tree in a given module.
 * The use case is with static routes and route maps
 * example : ip route 1.1.1.1/32 ens33
 *           ip route 1.1.1.1/32 ens34
 * After this no ip route 1.1.1.1/32 ens34 came, now staticd
 * has to find out upto which level it has to delete the dnodes.
 * For this case it has to send delete nexthop
 * After this no ip route 1.1.1.1/32 ens33 came, now staticd has to
 * clear nexthop, path and route nodes.
 * The same scheme is required for routemaps also
 * dnode The starting node to work on
 *
 * Returns The final parent node selected for deletion
 */
/*
 * yang_get_subtree_with_no_sibling - 当节点是其所在 list 的最后一个时，上找可一并删除的最外层父节点。
 * 场景：静态路由与路由映射，删除最后一项时需逐级清理为空的 nexthop、path、route 等父节点。
 */
extern const struct lyd_node *
yang_get_subtree_with_no_sibling(const struct lyd_node *dnode);

/* To get the relative position of a node in list */
/*
 * yang_get_list_pos - 取节点在 list 中的相对位置。
 */
extern uint32_t yang_get_list_pos(const struct lyd_node *node);

/* To get the number of elements in a list
 *
 * dnode : The head of list
 * Returns : The number of dnodes present in the list
 */
/*
 * yang_get_list_elements_count - 统计 list 中的元素个数，dnode 为表头。
 */
extern uint32_t yang_get_list_elements_count(const struct lyd_node *node);

/* API to check if the given node is last node in the list */
/*
 * yang_is_last_list_dnode - 判断给定节点是否为其所在 list 的最后一个节点。
 */
bool yang_is_last_list_dnode(const struct lyd_node *dnode);

/* API to check if the given node is last node in the data tree level */
/*
 * yang_is_last_level_dnode - 判断给定节点是否处于数据树中该层的末位。
 */
bool yang_is_last_level_dnode(const struct lyd_node *dnode);

/* Create a YANG predicate string based on the keys */
/*
 * yang_get_key_preds - 依据各 key 生成 YANG 谓词字符串并写入缓冲 s。
 */
extern int yang_get_key_preds(char *s, const struct lysc_node *snode,
			      const struct yang_list_keys *keys, ssize_t space);

/* Get the length of the predicate string based on the keys */
/*
 * yang_get_key_pred_strlen - 依据各 key 计算谓词字符串所需长度。
 */
extern int yang_get_key_pred_strlen(const struct lysc_node *snode,
				    const struct yang_list_keys *keys);

/* Get YANG keys from an existing dnode */
/*
 * yang_get_node_keys - 从既有数据节点中提取各 key 取值。
 */
extern int yang_get_node_keys(struct lyd_node *node, struct yang_list_keys *keys);

/**
 * yang_xpath_pop_node() - remove the last node from xpath string
 * @xpath: an xpath string
 *
 * Return: NB_OK or NB_ERR_NOT_FOUND if nothing left to pop.
 */
/*
 * yang_xpath_pop_node - 去掉 xpath 的最后一段；没有可弹出的部分时返回 NB_ERR_NOT_FOUND。
 */
extern int yang_xpath_pop_node(char *xpath);

/**
 * yang_resolve_snodes() - Resolve an XPath to matching schema nodes.
 * @ly_ctx: libyang context to operate on.
 * @xpath: the path or XPath to resolve.
 * @snodes: [OUT] pointer for resulting dynamic array (darr) of schema node
 *          pointers.
 * @simple: [OUT] indicates if @xpath was resolvable simply or not. Non-simple
 *          means that the @xpath is not a simple path and utilizes XPath 1.0
 *          functionality beyond simple key predicates.
 *
 * This function can be used to find the schema node (or nodes) that correspond
 * to a given @xpath. If the @xpath includes non-key predicates (e.g., using
 * functions) then @simple will be set to false, and @snodes may contain more
 * than a single schema node.
 *
 * Return: a libyang error or LY_SUCCESS.
 */
/*
 * yang_resolve_snode_xpath - 把 xpath 解析为一组 schema 节点；snodes 输出 darr，
 * simple 表示是否为简单路径（含非 key 谓词时为 false，此时 snodes 可能多于一个节点）。
 */
extern LY_ERR yang_resolve_snode_xpath(struct ly_ctx *ly_ctx, const char *xpath,
				       const struct lysc_node ***snodes,
				       bool *simple);

/*
 * 以下为 libyang“未来函数”区的声明：上游尚未提供兼容实现，FRR 自带一份。
 */
/*
 * Libyang future functions
 */
/*
 * yang_ly_strerrcode - 把 libyang 错误码转成可读字符串。
 */
extern const char *yang_ly_strerrcode(LY_ERR err);
/*
 * yang_ly_strvecode - 把 libyang 校验错误码转成可读字符串。
 */
extern const char *yang_ly_strvecode(LY_VECODE vecode);
/*
 * yang_lyd_new_list - 新建 list 数据节点；keys 为各 key 取值。
 */
extern LY_ERR yang_lyd_new_list(struct lyd_node *parent, const struct lysc_node *snode,
				const struct yang_list_keys *keys, struct lyd_node **nodes);
/*
 * yang_lyd_trim_xpath - 裁剪数据树，仅保留匹配 xpath 的节点及其祖先。
 */
extern LY_ERR yang_lyd_trim_xpath(struct lyd_node **rootp, const char *xpath);
/*
 * yang_lyd_parse_data - 解析数据并修正旧版 libyang 不回填结果树的缺陷。
 */
extern LY_ERR yang_lyd_parse_data(const struct ly_ctx *ctx,
				  struct lyd_node *parent, struct ly_in *in,
				  LYD_FORMAT format, uint32_t parse_options,
				  uint32_t validate_options,
				  struct lyd_node **tree);


/*
 * 以下处理不同 libyang 版本之间 NBC（节点操作）API 的差异：被替换的老函数用同名宏
 * 禁用（直接调用会触发编译期 GCC error），强制调用方改用这里的包装函数，
 * 以免混淆不同版本的取值长度单位语义。
 */
/*
 * Handle NBC API changes between versions of Libyang
 */

/* Use this helper function to deal with change in value_size units */
/*
 * yang_new_term_bin - 新建二进制叶子，value_size 以字节计（内部按版本换算单位）。
 */
extern LY_ERR yang_new_term_bin(struct lyd_node *parent, const struct lys_module *module,
				const char *name, const void *value, uint32_t value_size,
				uint32_t options, struct lyd_node **node);
#define lyd_new_term_bin(...)                                                                     \
	_Pragma("GCC error \"Use yang_new_term_bin() instead of lyd_new_term_bin()\"")

/*
 * yang_change_term_bin - 修改二进制叶子取值，value_size_bits 为位长度。
 */
LY_ERR yang_change_term_bin(struct lyd_node *term, const void *value, uint32_t value_size_bits);
#define lyd_change_term_bin(...)                                                                  \
	_Pragma("GCC error \"Use yang_change_term_bin() instead of lyd_new_change_term_bin()\"")

/*
 * yang_new_path2 - 按路径新建或更新数据节点链，value_size_bits 为位长度。
 */
extern LY_ERR yang_new_path2(struct lyd_node *parent, const struct ly_ctx *ctx, const char *path,
			     const void *value, uint32_t value_size_bits,
			     LYD_ANYDATA_VALUETYPE value_type, uint32_t options,
			     struct lyd_node **new_parent, struct lyd_node **new_node);
#define lyd_new_path2(...) _Pragma("GCC error \"Use yang_new_path2() instead of lyd_new_path2()\"")

#if (LY_VERSION_MAJOR >= 3) && (LY_VERSION_MAJOR < 5)
/*
 * yang_new_ext_term - 在扩展实例下新建叶子（仅在 libyang 3 到 4 之间提供）。
 */
extern LY_ERR yang_new_ext_term(const struct lysc_ext_instance *ext, const char *name,
				const void *value, uint32_t value_size_bits, uint32_t options,
				struct lyd_node **node);
#define lyd_new_ext_term(...)                                                                     \
	_Pragma("GCC error \"Use yang_new_ext_term() instead of lyd_new_ext_term()\"")
#endif

#ifdef __cplusplus
}
#endif

#endif /* _FRR_YANG_H_ */
