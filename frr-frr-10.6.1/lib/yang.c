// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * 文件导读：libyang 封装层（yang.c / yang.h）
 *
 * 本文件是 FRR 对通用 YANG 库 libyang 的封装层，位于整个配置模型体系的底座。
 * 层次关系自上而下大致为：
 *   1) northbound 层：北向接口，定义 callback 与配置事务
 *   2) yang.c（本文件）：管理 libyang 上下文、加载内嵌 YANG 模块、遍历编译后的
 *      schema 节点树、提供“路径 / 数据”之间互转的底层工具
 *   3) yang_wrappers.c：在 yang.c 之上提供“取值 / 设值”的便捷包装，屏蔽 libyang
 *      繁杂的类型转换细节，供各守护进程直接调用
 *   4) yang_translator.c：处理不同 YANG 模型（厂商私有模型与 FRR 原生模型）
 *      之间的配置转换
 *
 * 关键概念：
 *   - ly_native_ctx：全局唯一的 libyang 上下文，所有原生模块都加载其中
 *   - yang_module_embed：把编译期嵌入的 YANG 源码注册进来，供 libyang 查找
 *   - 编译后的 schema 节点 lysc_node 与运行时数据节点 lyd_node 是两个层次，
 *     本文件大量在两者间来回切换，读代码时务必分清楚
 *   - 遍历接口 yang_snodes_iterate 系列采用“回调加返回值控制”的机制，返回
 *     YANG_ITER_CONTINUE / YANG_ITER_STOP 决定是否继续
 *
 * 阅读建议：先看 yang_module_load 系列理解模块如何加载，再看 snodes_iterate
 * 系列理解 schema 遍历，最后看 path 与 data 的转换工具。
 */
#include <zebra.h>

#include "darr.h"
#include "debug.h"
#include "log.h"
#include "lib_errors.h"
#include "yang.h"
#include "yang_translator.h"
#include <libyang/version.h>
#include "northbound.h"
#include "frrstr.h"
#include "darr.h"

#include "lib/config_paths.h"

/*
 * DEFINE_MTYPE_STATIC - 声明本文件私有的内存类型标签。
 * 便于内存统计（show memory）与越界 / 泄漏排错，标签是本文件独享的。
 */
DEFINE_MTYPE_STATIC(LIB, YANG_MODULE, "YANG module");
DEFINE_MTYPE_STATIC(LIB, YANG_DATA, "YANG data structure");

/*
 * yang_lyd_find_xpath3 - 兼容包装宏，屏蔽 libyang 2.x 到 3.x 的 API 差异。
 * libyang 3 的 lyd_find_xpath3 需要额外的 LY_VALUE_JSON 与 prefix_data 参数，
 * 老版本则不需要；这里把两者统一成同一套参数签名，调用方无需关心版本。
 * LYD_NEW_VAL_OUTPUT 同理，旧版本用 LYD_NEW_PATH_OUTPUT 顶替。
 */
/* Safe to remove after libyang 2.2.8 */
#if (LY_VERSION_MAJOR < 3)
#define yang_lyd_find_xpath3(ctx_node, tree, xpath, format, prefix_data, vars, \
			     set)                                              \
	lyd_find_xpath3(ctx_node, tree, xpath, vars, set)

#ifndef LYD_NEW_VAL_OUTPUT
#define LYD_NEW_VAL_OUTPUT LYD_NEW_PATH_OUTPUT
#endif

#else
#define yang_lyd_find_xpath3(ctx_node, tree, xpath, format, prefix_data, vars, \
			     set)                                              \
	lyd_find_xpath3(ctx_node, tree, xpath, LY_VALUE_JSON, NULL, vars, set)
#endif

/* libyang container. */
/*
 * ly_native_ctx - 全局唯一的 libyang 上下文，所有原生模块都加载于此。
 * 整个进程共享，由 yang 初始化阶段建立，贯穿配置解析与校验过程。
 */
struct ly_ctx *ly_native_ctx;

/*
 * embeds / embedupd - 内嵌 YANG 模块的单链表与“尾指针的地址”。
 * embeds 指向链首，embedupd 指向最后一个节点的 next 字段，便于 O(1) 追加。
 */
static struct yang_module_embed *embeds, **embedupd = &embeds;

/*
 * yang_module_embed - 注册一个内嵌的 YANG 模块。
 * @embed: 调用方分配（通常是静态）的嵌入描述结构，本函数接管其 next 字段。
 * 作用：把编译期嵌进二进制的 YANG 模型串成单链表，供 imp_clb 回调查找。
 * 副作用：修改全局链表尾指针 embedupd，并把 embed 挂到链尾。
 * 坑：embed 的生命周期必须长于 libyang 上下文，否则会悬垂。
 */
void yang_module_embed(struct yang_module_embed *embed)
{
	embed->next = NULL;
	*embedupd = embed;
	embedupd = &embed->next;
}

/*
 * yang_module_imp_clb - libyang 的“模块导入回调”。
 * @mod_name / @mod_rev：被请求的模块名与版本号
 * @submod_name / @submod_rev：被请求的子模块名与版本号（可能为 NULL）
 * @format：输出，告知 libyang 数据是 YANG 还是 YIN 格式
 * @module_data：输出，指向内嵌的模型文本
 * @free_module_data：输出，需要释放数据时给出的释放函数
 * 返回：匹配到则 LY_SUCCESS，否则 LY_ENOTFOUND。
 * 作用：libyang 加载模块时若找不到本地文件，就回调这里索取内嵌模型。
 * 坑：ietf-inet-types / ietf-yang-types / ietf-yang-metadata 由 libyang 内置，
 *     这里必须直接返回 NOTFOUND，不能重复提供，否则会冲突。
 */
static LY_ERR yang_module_imp_clb(const char *mod_name, const char *mod_rev,
				  const char *submod_name,
				  const char *submod_rev, void *user_data,
				  LYS_INFORMAT *format,
				  const char **module_data,
				  void (**free_module_data)(void *, void *))
{
	struct yang_module_embed *e;

	if (!strcmp(mod_name, "ietf-inet-types") ||
	    !strcmp(mod_name, "ietf-yang-types") ||
	    !strcmp(mod_name, "ietf-yang-metadata"))
		/* libyang has these built in, don't try finding them here */
		return LY_ENOTFOUND;

	for (e = embeds; e; e = e->next) {
		if (e->sub_mod_name && submod_name) {
			if (strcmp(e->sub_mod_name, submod_name))
				continue;

			if (submod_rev && strcmp(e->sub_mod_rev, submod_rev))
				continue;
		} else {
			if (strcmp(e->mod_name, mod_name))
				continue;

			if (mod_rev && strcmp(e->mod_rev, mod_rev))
				continue;
		}

		*format = e->format;
		*module_data = e->data;
		return LY_SUCCESS;
	}

	/* We get here for indirect modules like ietf-inet-types */
	zlog_debug(
		"YANG model \"%s@%s\" \"%s@%s\"not embedded, trying external file",
		mod_name, mod_rev ? mod_rev : "*",
		submod_name ? submod_name : "*", submod_rev ? submod_rev : "*");

	return LY_ENOTFOUND;
}

/*
 * frr_native_modules - FRR 原生 YANG 模块清单。
 * 这些模块随 FRR 源码一起编译并内嵌，yang_module_load_all 会全部加载。
 * 用 clang-format off 包裹，逐行一项列出，避免被格式化工具重排。
 */
/* clang-format off */
static const char *const frr_native_modules[] = {
	"frr-affinity-map",
	"frr-host",
	"frr-interface",
	"frr-isisd",
	"frr-logging",
	"frr-nexthop",
	"frr-pathd",
	"frr-ripd",
	"frr-ripngd",
	"frr-route-map",
	"frr-routing",
	"frr-vrf",
	"frr-vrrpd",
	"frr-zebra",
	"ietf-syslog-types",
};
/* clang-format on */

/* Generate the yang_modules tree. */
/*
 * yang_module_compare - 红黑树比较函数。
 * 按模块名字符串比较，用于 yang_modules 这棵 RB 树的排序与查找。
 */
static inline int yang_module_compare(const struct yang_module *a,
				      const struct yang_module *b)
{
	return strcmp(a->name, b->name);
}
/*
 * RB_GENERATE - 由宏生成红黑树的一整套操作函数（查找 / 插入 / 删除 / 遍历）。
 * 参数依次为：树类型名、节点类型、节点内链接字段 entry、比较函数。
 */
RB_GENERATE(yang_modules, yang_module, entry, yang_module_compare)

struct yang_modules yang_modules = RB_INITIALIZER(&yang_modules);

/*
 * yang_module_load - 加载一个 YANG 模块并登记到全局 RB 树。
 * @module_name：模块名，例如 frr-interface
 * @features：要启用的 feature 数组（以 NULL 结尾），传 NULL 表示不启用
 * 返回：新分配的 yang_module 结构，已插入全局 yang_modules 树。
 * 副作用：加载失败或模块已存在时打印错误并 exit(1)，属于启动期致命错误。
 * 坑：module->name 直接指向调用方传入的字符串，调用方要保证其长期有效。
 */
struct yang_module *yang_module_load(const char *module_name,
				     const char **features)
{
	struct yang_module *module;
	const struct lys_module *module_info;

	if (features)
		DEBUGD(&nb_dbg_events, "yang: loading module %s features: %pSA", module_name,
		       features);
	else
		DEBUGD(&nb_dbg_events, "yang: loading module %s", module_name);

	module_info = ly_ctx_load_module(ly_native_ctx, module_name, NULL, features);
	if (!module_info) {
		flog_err(EC_LIB_YANG_MODULE_LOAD,
			 "%s: failed to load data model: %s", __func__,
			 module_name);
		exit(1);
	}

	module = XCALLOC(MTYPE_YANG_MODULE, sizeof(*module));
	module->name = module_name;
	module->info = module_info;

	if (RB_INSERT(yang_modules, &yang_modules, module) != NULL) {
		flog_err(EC_LIB_YANG_MODULE_LOADED_ALREADY,
			 "%s: YANG module is loaded already: %s", __func__,
			 module_name);
		exit(1);
	}

	return module;
}

/*
 * yang_module_load_all - 加载全部 FRR 原生模块。
 * 对所有模块统一启用所有 feature（通配符 “星号”），启动时调用一次即可。
 */
void yang_module_load_all(void)
{
	static const char * const all_features[] = { "*", NULL };

	for (size_t i = 0; i < array_size(frr_native_modules); i++)
		yang_module_load(frr_native_modules[i], (const char **)all_features);
}

/*
 * yang_module_find - 按模块名在 RB 树中查找已加载模块。
 * 返回：找到则返回 yang_module 指针，否则 NULL。
 */
struct yang_module *yang_module_find(const char *module_name)
{
	struct yang_module s;

	s.name = module_name;
	return RB_FIND(yang_modules, &yang_modules, &s);
}

/*
 * yang_snodes_iterate_subtree - 递归遍历 schema 节点子树。
 * @snode：起始节点
 * @module：若不为 NULL，则只遍历属于该模块的节点
 * @cb：每到一个节点就调用的回调，其返回值控制继续或停止
 * @flags：YANG_ITER_* 过滤标志，例如跳过非 presence 容器、跳过 list key
 * @arg：透传给回调的上下文
 * 返回：YANG_ITER_* 结果；遇到 STOP 会立即向上返回。
 * 坑：对叶子按 flags 过滤 list key，对容器过滤非 presence 容器，这些判断都在
 *     真正下钻之前做出，漏看容易困惑为何某些节点没有被访问到。
 */
int yang_snodes_iterate_subtree(const struct lysc_node *snode,
				const struct lys_module *module,
				yang_iterate_cb cb, uint16_t flags, void *arg)
{
	const struct lysc_node *child;
	int ret = YANG_ITER_CONTINUE;

	if (module && snode->module != module)
		goto next;

	switch (snode->nodetype) {
	case LYS_CONTAINER:
		if (CHECK_FLAG(flags, YANG_ITER_FILTER_NPCONTAINERS)) {
			if (!CHECK_FLAG(snode->flags, LYS_PRESENCE))
				goto next;
		}
		break;
	case LYS_LEAF:
		if (CHECK_FLAG(flags, YANG_ITER_FILTER_LIST_KEYS)) {
			/* Ignore list keys. */
			if (lysc_is_key(snode))
				goto next;
		}
		break;
	case LYS_INPUT:
	case LYS_OUTPUT:
		if (CHECK_FLAG(flags, YANG_ITER_FILTER_INPUT_OUTPUT))
			goto next;
		break;
	default:
		assert(snode->nodetype != LYS_AUGMENT
		       && snode->nodetype != LYS_GROUPING
		       && snode->nodetype != LYS_USES);
		break;
	}

	ret = (*cb)(snode, arg);
	if (ret == YANG_ITER_STOP)
		return ret;

next:
	/*
	 * YANG leafs and leaf-lists can't have child nodes.
	 */
	if (CHECK_FLAG(snode->nodetype, LYS_LEAF | LYS_LEAFLIST))
		return YANG_ITER_CONTINUE;

	LY_LIST_FOR (lysc_node_child(snode), child) {
		ret = yang_snodes_iterate_subtree(child, module, cb, flags,
						  arg);
		if (ret == YANG_ITER_STOP)
			return ret;
	}
	LY_LIST_FOR ((const struct lysc_node *)lysc_node_notifs(snode), child) {
		ret = yang_snodes_iterate_subtree(child, module, cb, flags, arg);
		if (ret == YANG_ITER_STOP)
			return ret;
	}
	LY_LIST_FOR ((const struct lysc_node *)lysc_node_actions(snode), child) {
		ret = yang_snodes_iterate_subtree(child, module, cb, flags, arg);
		if (ret == YANG_ITER_STOP)
			return ret;
	}
	return ret;
}

/*
 * yang_snodes_iterate - 遍历所有已实现模块的 schema 节点树。
 * @module：若不为 NULL，则只对指定模块的节点调用回调
 * @cb / @flags / @arg：含义同 yang_snodes_iterate_subtree
 * 返回：YANG_ITER_CONTINUE 或 YANG_ITER_STOP。
 * 作用：从内部（内置）模块之后开始迭代，逐模块遍历其数据树、rpc 树与
 *       notification 树，是配置 schema 遍历的统一入口。
 * 坑：只处理 implemented 为真的模块，仅被 import 而未实现的模块会被跳过。
 */
int yang_snodes_iterate(const struct lys_module *module, yang_iterate_cb cb,
			uint16_t flags, void *arg)
{
	const struct lys_module *module_iter;
	uint32_t idx = 0;
	int ret = YANG_ITER_CONTINUE;

	idx = ly_ctx_internal_modules_count(ly_native_ctx);
	while ((module_iter = ly_ctx_get_module_iter(ly_native_ctx, &idx))) {
		struct lysc_node *snode;

		if (!module_iter->implemented)
			continue;

		LY_LIST_FOR (module_iter->compiled->data, snode) {
			ret = yang_snodes_iterate_subtree(snode, module, cb,
							  flags, arg);
			if (ret == YANG_ITER_STOP)
				return ret;
		}
		if (module_iter->compiled->rpcs) {
			LY_LIST_FOR (&module_iter->compiled->rpcs->node, snode) {
				ret = yang_snodes_iterate_subtree(snode, module,
								  cb, flags,
								  arg);
				if (ret == YANG_ITER_STOP)
					return ret;
			}
		}

		if (module_iter->compiled->notifs) {
			LY_LIST_FOR (&module_iter->compiled->notifs->node,
				     snode) {
				ret = yang_snodes_iterate_subtree(snode, module,
								  cb, flags,
								  arg);
				if (ret == YANG_ITER_STOP)
					return ret;
			}
		}
	}

	return ret;
}

/*
 * yang_snode_get_path - 生成 schema 节点的路径字符串。
 * @snode：schema 节点
 * @type：路径类型，YANG_PATH_SCHEMA 生成 schema 路径，YANG_PATH_DATA 生成数据路径
 * @xpath：输出缓冲
 * @xpath_len：缓冲长度
 * 副作用：遇到未知类型时打印错误并 exit(1)。
 * 坑：schema 路径与数据路径对 list key 的表示方式不同，选错会导致后续查找失败。
 */
void yang_snode_get_path(const struct lysc_node *snode,
			 enum yang_path_type type, char *xpath,
			 size_t xpath_len)
{
	switch (type) {
	case YANG_PATH_SCHEMA:
		(void)lysc_path(snode, LYSC_PATH_LOG, xpath, xpath_len);
		break;
	case YANG_PATH_DATA:
		(void)lysc_path(snode, LYSC_PATH_DATA, xpath, xpath_len);
		break;
	default:
		flog_err(EC_LIB_DEVELOPMENT, "%s: unknown yang path type: %u",
			 __func__, type);
		exit(1);
	}
}

/*
 * yang_resolve_snode_xpath - 把 xpath 解析成一组 schema 节点。
 * @ly_ctx：libyang 上下文
 * @xpath：待解析的路径
 * @snodes：输出，darr 动态数组，存放解析到的节点指针
 * @simple：输出，true 表示是简单路径（只匹配单个节点），false 表示复杂查询
 * 返回：LY_SUCCESS 或错误码（含 LY_ENOTFOUND）。
 * 策略：先用 lys_find_path 当简单路径处理；失败再用 lys_find_xpath 处理带谓词
 *       等复杂情况的查询。
 * 坑：simple 为 false 时结果可能含多个节点，调用方需要遍历 snodes 数组。
 */
LY_ERR yang_resolve_snode_xpath(struct ly_ctx *ly_ctx, const char *xpath,
				const struct lysc_node ***snodes, bool *simple)
{
	struct lysc_node *snode;
	struct ly_set *set;
	LY_ERR err;

	/* lys_find_path will not resolve complex xpaths */
	snode = (struct lysc_node *)lys_find_path(ly_ctx, NULL, xpath, 0);
	if (snode) {
		*darr_append(*snodes) = snode;
		*simple = true;
		return LY_SUCCESS;
	}

	/* Try again to catch complex query cases */
	err = lys_find_xpath(ly_native_ctx, NULL, xpath, 0, &set);
	if (err)
		return err;
	if (!set->count) {
		ly_set_free(set, NULL);
		return LY_ENOTFOUND;
	}

	*simple = false;
	darr_ensure_i(*snodes, set->count - 1);
	memcpy(*snodes, set->snodes, set->count * sizeof(set->snodes[0]));
	ly_set_free(set, NULL);
	return LY_SUCCESS;
}


/*
 * yang_find_snode - 按 xpath 在 schema 中查找单个节点。
 * @options：预留带选项查找用，当前实现未使用
 * 返回：找到的 schema 节点，找不到返回 NULL。
 * 注意：只处理简单路径，复杂查询请改用 yang_resolve_snode_xpath。
 */
struct lysc_node *yang_find_snode(struct ly_ctx *ly_ctx, const char *xpath,
				  uint32_t options)
{
	struct lysc_node *snode;

	snode = (struct lysc_node *)lys_find_path(ly_ctx, NULL, xpath, 0);

	return snode;
}

/*
 * yang_snode_real_parent - 找到最近的“真实父节点”。
 * 向上回溯，跳过普通（非 presence）容器，返回第一个 presence 容器或 list。
 * 返回：找到则返回该父节点，否则 NULL。
 * 用途：定位配置实例真正挂载的位置，例如 list 内某个 leaf 的实例父。
 */
struct lysc_node *yang_snode_real_parent(const struct lysc_node *snode)
{
	struct lysc_node *parent = snode->parent;

	while (parent) {
		switch (parent->nodetype) {
		case LYS_CONTAINER:
			if (CHECK_FLAG(parent->flags, LYS_PRESENCE))
				return parent;
			break;
		case LYS_LIST:
			return parent;
		default:
			break;
		}
		parent = parent->parent;
	}

	return NULL;
}

/*
 * yang_snode_parent_list - 找到最近的 list 类型祖先节点。
 * 返回：最近的 list 父节点，没有则 NULL。
 */
struct lysc_node *yang_snode_parent_list(const struct lysc_node *snode)
{
	struct lysc_node *parent = snode->parent;

	while (parent) {
		switch (parent->nodetype) {
		case LYS_LIST:
			return parent;
		default:
			break;
		}
		parent = parent->parent;
	}

	return NULL;
}

/*
 * yang_snode_is_typeless_data - 判断节点是否“无类型的叶子数据”。
 * 返回：leaf 且类型为 empty 时为 true；leaf-list 为 false；其它节点类型为 true。
 * 用途：北向接口借它区分需要装载或调用回调的叶子与纯结构节点。
 */
bool yang_snode_is_typeless_data(const struct lysc_node *snode)
{
	const struct lysc_node_leaf *sleaf;

	switch (snode->nodetype) {
	case LYS_LEAF:
		sleaf = (struct lysc_node_leaf *)snode;
		if (sleaf->type->basetype == LY_TYPE_EMPTY)
			return true;
		return false;
	case LYS_LEAFLIST:
		return false;
	default:
		return true;
	}
}

/*
 * yang_snode_get_default - 取 schema 上声明的默认值。
 * @snode：schema 节点
 * 返回：默认值字符串，无默认值则 NULL。
 * 注意：libyang 4 起直接返回 schema 中的字符串，未保证是规范化形式；旧版本通过
 *       lyd_value_get_canonical 取规范值。leaf-list 的默认值暂未支持。
 */
const char *yang_snode_get_default(const struct lysc_node *snode)
{
	const struct lysc_node_leaf *sleaf;

	switch (snode->nodetype) {
	case LYS_LEAF:
		sleaf = (const struct lysc_node_leaf *)snode;
#if (LY_VERSION_MAJOR < 4)
		return sleaf->dflt ? lyd_value_get_canonical(sleaf->module->ctx, sleaf->dflt)
				   : NULL;
#else
		/* NOTE: this is value in the schema, not necessarily the canonical form */
		return sleaf->dflt.str;
#endif
	case LYS_LEAFLIST:
		/* TODO: check leaf-list default values */
		return NULL;
	default:
		return NULL;
	}
}

/*
 * yang_snode_get_type - 取叶子或 leaf-list 的最终类型。
 * 返回：节点类型；若为 leafref 会一路解引用到真实类型；非叶子返回 NULL。
 * 坑：leafref 可能链式嵌套，本函数用循环逐层剥开。
 */
const struct lysc_type *yang_snode_get_type(const struct lysc_node *snode)
{
	struct lysc_node_leaf *sleaf = (struct lysc_node_leaf *)snode;
	struct lysc_type *type;

	if (!CHECK_FLAG(sleaf->nodetype, LYS_LEAF | LYS_LEAFLIST))
		return NULL;

	type = sleaf->type;
	while (type->basetype == LY_TYPE_LEAFREF)
		type = ((struct lysc_type_leafref *)type)->realtype;

	return type;
}

/*
 * yang_snode_num_keys - 统计 list 的 key 数量。
 * 返回：key 个数；非 list 节点返回 0。
 */
unsigned int yang_snode_num_keys(const struct lysc_node *snode)
{
	const struct lysc_node_leaf *skey;
	uint count = 0;

	if (!CHECK_FLAG(snode->nodetype, LYS_LIST))
		return 0;

	/* Walk list of children */
	LY_FOR_KEYS (snode, skey) {
		count++;
	}
	return count;
}

/*
 * yang_dnode_get_path - 生成数据节点的标准路径字符串。
 * 返回：写入缓冲的路径（或 NULL）。
 * 注意：返回 libyang 标准格式，list 的 key 会带谓词。
 */
char *yang_dnode_get_path(const struct lyd_node *dnode, char *xpath,
			  size_t xpath_len)
{
	return lyd_path(dnode, LYD_PATH_STD, xpath, xpath_len);
}

/*
 * yang_dnode_get - 按相对 xpath 查找单个数据节点。
 * @dnode：起始数据节点
 * @xpath：相对路径，开头的 “./” 会被自动跳过
 * 返回：恰好匹配一个节点时返回它；匹配 0 个或超过 1 个时返回 NULL。
 * 副作用：匹配到多个时打印告警；出错时不再崩溃（历史上会 assert）。
 * 坑：解析失败与“找不到”都返回 NULL，调用方无法区分，需要自行判断。
 */
struct lyd_node *yang_dnode_get(const struct lyd_node *dnode, const char *xpath)
{
	struct ly_set *set = NULL;
	struct lyd_node *dnode_ret = NULL;

	/*
	 * XXX a lot of the code uses this for style I guess. It shouldn't, as
	 * it adds to the xpath parsing complexity in libyang.
	 */
	if (xpath[0] == '.' && xpath[1] == '/')
		xpath += 2;

	if (lyd_find_xpath(dnode, xpath, &set)) {
		/*
		 * Commenting out the below assert failure as it crashes mgmtd
		 * when bad xpath is passed.
		 *
		 * assert(0);  XXX replicates old libyang1 base code
		 */
		goto exit;
	}
	if (set->count == 0)
		goto exit;

	if (set->count > 1) {
		flog_warn(EC_LIB_YANG_DNODE_NOT_FOUND,
			  "%s: found %u elements (expected 0 or 1) [xpath %s]",
			  __func__, set->count, xpath);
		goto exit;
	}

	dnode_ret = set->dnodes[0];

exit:
	ly_set_free(set, NULL);

	return dnode_ret;
}

/*
 * yang_dnode_getf - 带格式化参数的 yang_dnode_get 变体。
 * 用 vsnprintf 把可变参数拼成 xpath 后再调用 yang_dnode_get。
 * 坑：拼接缓冲固定为 XPATH_MAXLEN，超长会被截断。
 */
struct lyd_node *yang_dnode_getf(const struct lyd_node *dnode,
				 const char *xpath_fmt, ...)
{
	va_list ap;
	char xpath[XPATH_MAXLEN];

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	return yang_dnode_get(dnode, xpath);
}

/*
 * yang_dnode_exists - 判断相对 xpath 是否至少匹配一个数据节点。
 * 返回：存在则 true，否则 false。
 * 注意：与 yang_dnode_get 一样会跳过开头的 “./”。
 */
bool yang_dnode_exists(const struct lyd_node *dnode, const char *xpath)
{
	struct ly_set *set = NULL;
	bool exists = false;

	if (xpath[0] == '.' && xpath[1] == '/')
		xpath += 2;
	if (lyd_find_xpath(dnode, xpath, &set))
		return false;
	exists = set->count > 0;
	ly_set_free(set, NULL);
	return exists;
}

/*
 * yang_dnode_existsf - 带格式化参数的 yang_dnode_exists 变体。
 */
bool yang_dnode_existsf(const struct lyd_node *dnode, const char *xpath_fmt,
			...)
{
	va_list ap;
	char xpath[XPATH_MAXLEN];

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	return yang_dnode_exists(dnode, xpath);
}

/*
 * yang_dnode_iterate - 对匹配 xpath 的每个数据节点执行回调。
 * @cb：回调，返回 YANG_ITER_STOP 则提前结束
 * @arg：透传上下文
 * @dnode：起始数据节点
 * @xpath_fmt：格式化 xpath
 * 副作用：xpath 解析失败时 assert(0) 并直接返回。
 */
void yang_dnode_iterate(yang_dnode_iter_cb cb, void *arg,
			const struct lyd_node *dnode, const char *xpath_fmt,
			...)
{
	va_list ap;
	char xpath[XPATH_MAXLEN];
	struct ly_set *set;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	if (lyd_find_xpath(dnode, xpath, &set)) {
		assert(0); /* XXX libyang2: ly1 code asserted success */
		return;
	}
	for (unsigned int i = 0; i < set->count; i++) {
		int ret;

		ret = (*cb)(set->dnodes[i], arg);
		if (ret == YANG_ITER_STOP)
			break;
	}

	ly_set_free(set, NULL);
}

/*
 * yang_dnode_count - 统计匹配 xpath 的数据节点个数。
 * 返回：节点数量；xpath 解析失败时 assert(0) 并返回 0。
 */
uint32_t yang_dnode_count(const struct lyd_node *dnode, const char *xpath_fmt,
			  ...)
{
	va_list ap;
	char xpath[XPATH_MAXLEN];
	struct ly_set *set;
	uint32_t count;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	if (lyd_find_xpath(dnode, xpath, &set)) {
		assert(0);
		return 0;
	}

	count = set->count;

	ly_set_free(set, NULL);

	return count;
}

/*
 * yang_dnode_is_default - 判断节点当前取值是否等于默认值。
 * @xpath：可选的相对路径；非 NULL 时先定位到该节点
 * 返回：leaf 用 lyd_is_default 判断（empty 类型恒为 false）；非 presence 容器视为
 *       default（返回 true）；presence 容器、leaf-list、其它节点返回 false。
 * 坑：xpath 定位后 assert(dnode)，找不到节点会直接断言崩溃。
 */
bool yang_dnode_is_default(const struct lyd_node *dnode, const char *xpath)
{
	const struct lysc_node *snode;
	struct lysc_node_leaf *sleaf;

	if (xpath)
		dnode = yang_dnode_get(dnode, xpath);

	assert(dnode);
	snode = dnode->schema;
	switch (snode->nodetype) {
	case LYS_LEAF:
		sleaf = (struct lysc_node_leaf *)snode;
		if (sleaf->type->basetype == LY_TYPE_EMPTY)
			return false;
		return lyd_is_default(dnode);
	case LYS_LEAFLIST:
		/* TODO: check leaf-list default values */
		return false;
	case LYS_CONTAINER:
		if (CHECK_FLAG(snode->flags, LYS_PRESENCE))
			return false;
		return true;
	default:
		return false;
	}
}

/*
 * yang_dnode_is_defaultf - 带格式化参数的 yang_dnode_is_default 变体。
 * @xpath_fmt 为 NULL 时等价于直接判断传入的节点本身。
 */
bool yang_dnode_is_defaultf(const struct lyd_node *dnode, const char *xpath_fmt,
			    ...)
{
	if (!xpath_fmt)
		return yang_dnode_is_default(dnode, NULL);
	else {
		va_list ap;
		char xpath[XPATH_MAXLEN];

		va_start(ap, xpath_fmt);
		vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
		va_end(ap);

		return yang_dnode_is_default(dnode, xpath);
	}
}

/*
 * yang_dnode_is_default_recursive - 递归判断整棵子树是否全为默认值。
 * 返回：自身与所有后代都是默认值时 true，否则 false。
 * 注意：叶子与 leaf-list 是递归终点；容器需要深度优先遍历全部子孙。
 */
bool yang_dnode_is_default_recursive(const struct lyd_node *dnode)
{
	struct lyd_node *root, *dnode_iter;

	if (!yang_dnode_is_default(dnode, NULL))
		return false;

	if (CHECK_FLAG(dnode->schema->nodetype, LYS_LEAF | LYS_LEAFLIST))
		return true;

	LY_LIST_FOR (lyd_child(dnode), root) {
		LYD_TREE_DFS_BEGIN (root, dnode_iter) {
			if (!yang_dnode_is_default(dnode_iter, NULL))
				return false;

			LYD_TREE_DFS_END(root, dnode_iter);
		}
	}

	return true;
}

/*
 * yang_dnode_change_leaf - 修改叶子节点的取值。
 * @dnode：必须是 leaf 节点
 * @value：新的字符串值
 * 副作用：断言节点为 leaf；lyd_change_term 的返回结果此处未检查。
 */
void yang_dnode_change_leaf(struct lyd_node *dnode, const char *value)
{
	assert(dnode->schema->nodetype == LYS_LEAF);
	lyd_change_term(dnode, value);
}

/*
 * yang_dnode_new - 新建一棵空的、已通过校验的数据树。
 * @config_only：true 表示只保留配置数据（不含状态数据）
 * 返回：新建的根节点。
 * 副作用：校验失败时打印错误并 exit(1)，理论上不应发生。
 */
struct lyd_node *yang_dnode_new(struct ly_ctx *ly_ctx, bool config_only)
{
	struct lyd_node *dnode = NULL;
	int options = config_only ? LYD_VALIDATE_NO_STATE : 0;

	if (lyd_validate_all(&dnode, ly_ctx, options, NULL) != 0) {
		/* Should never happen. */
		flog_err(EC_LIB_LIBYANG, "%s: lyd_validate() failed", __func__);
		exit(1);
	}

	return dnode;
}

/*
 * yang_dnode_dup - 深拷贝数据节点及其兄弟节点。
 * 返回：拷贝出的新树；内部断言复制必须成功，否则崩溃。
 */
struct lyd_node *yang_dnode_dup(const struct lyd_node *dnode)
{
	struct lyd_node *dup = NULL;
	LY_ERR err;
	err = lyd_dup_siblings(dnode, NULL,
			       LYD_DUP_RECURSIVE | LYD_DUP_WITH_FLAGS, &dup);
	assert(!err);
	return dup;
}

/*
 * yang_dnode_free - 释放数据节点所在的整棵树。
 * 会先回溯到根节点，再整树释放，因此传入任意层级节点都能安全清理。
 */
void yang_dnode_free(struct lyd_node *dnode)
{
	while (dnode->parent)
		dnode = lyd_parent(dnode);
	lyd_free_all(dnode);
}

/*
 * yang_dnode_rpc_output_add - 向 rpc 输出树中新增一个叶子路径及其值。
 * @output：rpc 输出数据树
 * @xpath：要添加的节点路径
 * @value：取值字符串
 * 副作用：断言创建成功；用于北向接口构造操作返回数据。
 */
void yang_dnode_rpc_output_add(struct lyd_node *output, const char *xpath,
			       const char *value)
{
	LY_ERR err;

	err = lyd_new_path(output, ly_native_ctx, xpath, value,
			   LYD_NEW_VAL_OUTPUT | LYD_NEW_PATH_UPDATE, NULL);
	assert(err == LY_SUCCESS);
}

/*
 * yang_state_new - 确保 state 路径存在并返回对应的数据节点。
 * @tree：起始树，可为 NULL
 * @path：状态数据路径
 * @value：需要新建时写入的值
 * 返回：路径对应的数据节点。
 * 策略：先用 yang_new_path2 尝试创建；因节点可能已存在、或返回值可能是新建的父
 *       容器而非目标本身，之后统一用 lyd_find_path 精确定位。
 */
struct lyd_node *yang_state_new(struct lyd_node *tree, const char *path, const char *value)
{
	struct lyd_node *dnode, *parent;
	LY_ERR err;

	err = yang_new_path2(tree, ly_native_ctx, path, value, 0, 0, LYD_NEW_PATH_UPDATE, &parent,
			     &dnode);
	assert(err == LY_SUCCESS);

	/*
	 * If the node exists and isn't updated returned dnode will be NULL, so
	 * we need to find it. But even if returned it can be the first newly
	 * created node (could be container of path) not the actual path dnode.
	 * So we always find.
	 */
	err = lyd_find_path(tree ?: parent, path, false, &dnode);
	assert(err == LY_SUCCESS);

	return dnode;
}

/*
 * yang_state_delete - 删除 state 数据树中的指定路径或整棵树。
 * @tree：起始树；为 NULL 时直接返回
 * @path：要删除的路径；为 NULL 则释放整棵树
 * 副作用：路径不存在时只记录日志并返回；存在则用 lyd_free_tree 释放该子树。
 */
void yang_state_delete(struct lyd_node *tree, const char *path)
{
	LY_ERR err;

	if (!tree)
		return;

	if (path) {
		err = lyd_find_path(tree, path, false, &tree);
		if (err != LY_SUCCESS) {
			zlog_info("State %s has already been deleted", path);
			return;
		}
	}
	lyd_free_tree(tree);
}

PRINTFRR(2, 0)
/*
 * yang_state_new_vpathf - 路径用 va_list 传入的 yang_state_new 变体。
 * @path_fmt：格式化路径模板
 * @ap：已启动的可变参数表
 * 内部用 darr_vsprintf 拼接路径，用完立即 darr_free 释放。
 */
struct lyd_node *yang_state_new_vpathf(struct lyd_node *tree, const char *path_fmt,
				       const char *value, va_list ap)
{
	struct lyd_node *dnode;
	char *path;

	path = darr_vsprintf(path_fmt, ap);
	dnode = yang_state_new(tree, path, value);
	darr_free(path);

	return dnode;
}

/*
 * yang_state_new_pathf - 路径为可变参数的 yang_state_new 变体。
 * 转发给 yang_state_new_vpathf；只有 @path_fmt 参与格式化，@value 是固定参数。
 */
struct lyd_node *yang_state_new_pathf(struct lyd_node *tree, const char *path_fmt,
				      const char *value, ...)
{
	struct lyd_node *dnode;
	va_list ap;

	va_start(ap, value);
	dnode = yang_state_new_vpathf(tree, path_fmt, value, ap);
	va_end(ap);

	return dnode;
}

PRINTFRR(2, 0)
/*
 * yang_state_delete_vpathf - 路径用 va_list 传入的 yang_state_delete 变体。
 */
void yang_state_delete_vpathf(struct lyd_node *tree, const char *path_fmt, va_list ap)
{
	char *path;

	path = darr_vsprintf(path_fmt, ap);
	yang_state_delete(tree, path);
	darr_free(path);
}

/*
 * yang_state_delete_pathf - 路径为可变参数的 yang_state_delete 变体。
 */
void yang_state_delete_pathf(struct lyd_node *tree, const char *path_fmt, ...)
{
	va_list ap;

	va_start(ap, path_fmt);
	yang_state_delete_vpathf(tree, path_fmt, ap);
	va_end(ap);
}

PRINTFRR(3, 0)
/*
 * yang_state_vnewf - 取值用 va_list 格式化的 yang_state_new 变体。
 * @path：完整路径（不参与格式化），@val_fmt 与 @ap 负责生成取值字符串。
 */
struct lyd_node *yang_state_vnewf(struct lyd_node *tree, const char *path, const char *val_fmt,
				  va_list ap)
{
	struct lyd_node *dnode;
	char *value;

	value = darr_vsprintf(val_fmt, ap);
	dnode = yang_state_new(tree, path, value);
	darr_free(value);

	return dnode;
}

/*
 * yang_state_newf - 取值为可变参数的 yang_state_new 变体。
 * 转发给 yang_state_vnewf。
 */
struct lyd_node *yang_state_newf(struct lyd_node *tree, const char *path, const char *val_fmt, ...)
{
	struct lyd_node *dnode;
	va_list ap;

	va_start(ap, val_fmt);
	dnode = yang_state_vnewf(tree, path, val_fmt, ap);
	va_end(ap);

	return dnode;
}

/*
 * yang_data_new - 分配一个 yang_data（xpath 加取值字符串）。
 * @xpath：数据路径，会拷入固定长度字段（超长会被截断）
 * @value：取值字符串，为 NULL 时 value 字段留空
 * 返回：新分配的结构；value 用 strdup 复制一份。
 */
struct yang_data *yang_data_new(const char *xpath, const char *value)
{
	struct yang_data *data;

	data = XCALLOC(MTYPE_YANG_DATA, sizeof(*data));
	strlcpy(data->xpath, xpath, sizeof(data->xpath));
	if (value)
		data->value = strdup(value);

	return data;
}

/*
 * yang_data_free - 释放一个 yang_data。
 * 先释放内部的 value，再用 XFREE 归还整个结构。
 */
void yang_data_free(struct yang_data *data)
{
	if (data->value)
		free(data->value);
	XFREE(MTYPE_YANG_DATA, data);
}

/*
 * yang_data_list_new - 创建用于存放 yang_data 的链表。
 * 把 del 回调设为 yang_data_free，使链表销毁时自动释放各元素。
 */
struct list *yang_data_list_new(void)
{
	struct list *list;

	list = list_new();
	list->del = (void (*)(void *))yang_data_free;

	return list;
}

/*
 * yang_data_list_find - 在链表中按 xpath 查找 yang_data。
 * @xpath_fmt：格式化路径
 * 返回：匹配的节点，找不到返回 NULL。
 * 注意：用 strmatch 完整比较 xpath 字符串。
 */
struct yang_data *yang_data_list_find(const struct list *list,
				      const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	struct yang_data *data;
	struct listnode *node;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	for (ALL_LIST_ELEMENTS_RO(list, node, data))
		if (strmatch(data->xpath, xpath))
			return data;

	return NULL;
}

/* Make libyang log its errors using FRR logging infrastructure. */
/*
 * ly_zlog_cb - 把 libyang 的日志接入 FRR 的 logging 框架。
 * @level：libyang 日志级别，映射到 FRR 的 LOG_ERR / LOG_WARNING / LOG_DEBUG
 * @msg：日志正文
 * @data_path / @schema_path / @line：libyang 3 起额外提供的定位信息
 * 副作用：调用 zlog 输出日志。
 * 注意：schema_path 与 line 参数仅在 libyang 3 及以上版本存在，用条件编译隔离。
 */
static void ly_zlog_cb(LY_LOG_LEVEL level, const char *msg, const char *data_path
#if (LY_VERSION_MAJOR >= 3)
		       ,
		       const char *schema_path, uint64_t line
#endif
)
{
	int priority = LOG_ERR;

	switch (level) {
	case LY_LLERR:
		priority = LOG_ERR;
		break;
	case LY_LLWRN:
		priority = LOG_WARNING;
		break;
	case LY_LLVRB:
	case LY_LLDBG:
		priority = LOG_DEBUG;
		break;
	}

	if (data_path)
		zlog(priority, "libyang: %s (%s)", msg, data_path);
#if (LY_VERSION_MAJOR >= 3)
	else if (schema_path)
		zlog(priority, "libyang %s (%s)\n", msg, schema_path);
	else if (line)
		zlog(priority, "libyang %s (line %" PRIu64 ")\n", msg, line);
#endif
	else
		zlog(priority, "libyang: %s", msg);
}

/*
 * yang_parse_data - 把 XML 或 JSON 数据解析成 lyd 数据树。
 * @xpath：当 @as_subtree 为真时，作为数据子树的根路径
 * @format：输入格式（XML / JSON）
 * @as_subtree：是否把数据当作以 xpath 为根的子树来解析
 * @is_oper：是否为操作数据（影响校验选项）
 * @validate：是否执行最终校验
 * @data：输入文本
 * @tree：输出，解析出的数据树
 * 返回：LY_SUCCESS 或错误码；失败时释放已分配的部分并把 *tree 置为 NULL。
 * 坑：解析选项为 STRICT 加 ONLY，是否校验由 validate 单独控制。
 */
LY_ERR yang_parse_data(const char *xpath, LYD_FORMAT format, bool as_subtree, bool is_oper,
		       bool validate, const char *data, struct lyd_node **tree)
{
	struct ly_in *in = NULL;
	struct lyd_node *subtree = NULL;
	uint32_t parse_options = LYD_PARSE_STRICT | LYD_PARSE_ONLY;
	uint32_t validate_options = LYD_VALIDATE_PRESENT;
	LY_ERR err;

	err = ly_in_new_memory(data, &in);
	if (err != LY_SUCCESS)
		return err;

	if (as_subtree) {
		struct lyd_node *parent;

		/*
		 * Create the subtree branch from root using the xpath. This
		 * will be used below to parse the data rooted at the subtree --
		 * a common YANG JSON technique (vs XML which starts all
		 * data trees from the root).
		 */
		err = yang_new_path2(NULL, ly_native_ctx, xpath, NULL, 0, 0, 0, &parent, &subtree);
		if (err != LY_SUCCESS)
			goto done;
		err = lyd_find_path(parent, xpath, false, &subtree);
		if (err != LY_SUCCESS)
			goto done;
	}

	if (is_oper)
		validate_options |= LYD_VALIDATE_OPERATIONAL;

#ifdef LYD_VALIDATE_NOT_FINAL
	if (!validate)
		validate_options |= LYD_VALIDATE_NOT_FINAL;
#endif

	err = lyd_parse_data(ly_native_ctx, subtree, in, format, parse_options, validate_options,
			     tree);
	if (err == LY_SUCCESS && subtree)
		*tree = subtree;
done:
	ly_in_free(in, 0);
	if (err != LY_SUCCESS) {
		if (*tree)
			lyd_free_all(*tree);
		else if (subtree)
			lyd_free_all(subtree);
		*tree = NULL;
	}
	return err;
}

/*
 * yang_parse_op - 解析 rpc / action / notification 数据。
 * @ctx：libyang 上下文
 * @parent：rpc 或 action 的父节点
 * @in：输入流
 * @format：输入格式
 * @data_type：操作类型（rpc / reply / notification 等）
 * @tree / @op：输出树与操作节点
 * 返回：libyang 错误码。
 * 注意：libyang 4 对 LYB 二进制格式会追加“跳过上下文检查”的解析选项。
 */
static LY_ERR yang_parse_op(const struct ly_ctx *ctx, struct lyd_node *parent, struct ly_in *in,
			    LYD_FORMAT format, enum lyd_type data_type, struct lyd_node **tree,
			    struct lyd_node **op)
{
	return lyd_parse_op(ctx, parent, in, format, data_type,
#if (LY_VERSION_MAJOR >= 4)
			    format == LYD_LYB ? LYD_PARSE_LYB_SKIP_CTX_CHECK : 0,
#endif
			    tree, op);
}

/*
 * yang_parse_notification - 解析一条通知数据。
 * @xpath：期望在解析结果中匹配的通知路径
 * @format：输入格式
 * @data：输入文本
 * @notif：输出，匹配到的通知节点
 * 返回：LY_SUCCESS / LY_ENOTFOUND / 其它错误码。
 * 副作用：出错时释放中间树与集合，并打印日志。
 * 坑：用 LYD_TYPE_NOTIF_YANG 解析；集合为空说明数据里没有该通知。
 */
LY_ERR yang_parse_notification(const char *xpath, LYD_FORMAT format,
			       const char *data, struct lyd_node **notif)
{
	struct lyd_node *tree;
	struct ly_set *set = NULL;
	struct ly_in *in = NULL;
	LY_ERR err;

	err = ly_in_new_memory(data, &in);
	if (err) {
		zlog_err("Failed to initialize ly_in: %s", ly_last_errmsg());
		return err;
	}

	err = yang_parse_op(ly_native_ctx, NULL, in, format, LYD_TYPE_NOTIF_YANG, &tree, NULL);
	ly_in_free(in, 0);
	if (err) {
		zlog_err("Failed to parse notification: %s", ly_last_errmsg());
		return err;
	}

	err = yang_lyd_find_xpath3(NULL, tree, xpath, LY_VALUE_JSON, NULL, NULL,
				   &set);
	if (err) {
		zlog_err("Failed to parse notification: %s", ly_last_errmsg());
		lyd_free_all(tree);
		return err;
	}
	if (set->count == 0) {
		zlog_err("Notification not found in the parsed tree: %s", xpath);
		ly_set_free(set, NULL);
		lyd_free_all(tree);
		return LY_ENOTFOUND;
	}
	*notif = set->dnodes[0];
	ly_set_free(set, NULL);
	return LY_SUCCESS;
}

/*
 * yang_parse_restconf_rpc - 解析 RESTCONF 风格的 rpc / action 数据。
 * @xpath：rpc / action 的路径，也用于定位其 schema 节点
 * @format：输入格式
 * @data：输入文本，可为 NULL（此时只构造操作节点树）
 * @reply：true 表示解析的是回复数据
 * @rpc：输出，操作节点树
 * 返回：LY_ERR；schema 不是 rpc 或 action 时返回 LY_ENOTFOUND。
 * 坑：出错统一走 done 标签释放；成功时把 dnode 交给调用方。
 */
LY_ERR yang_parse_restconf_rpc(const char *xpath, LYD_FORMAT format, const char *data, bool reply,
			       struct lyd_node **rpc)
{
	const struct lysc_node *snode;
	struct lyd_node *dnode = NULL;
	struct ly_in *in = NULL;
	LY_ERR err;

	snode = lys_find_path(ly_native_ctx, NULL, xpath, 0);
	if (!snode) {
		zlog_err("Failed to find RPC/action schema node: %s", xpath);
		return LY_ENOTFOUND;
	}
	if (snode->nodetype != LYS_RPC && snode->nodetype != LYS_ACTION) {
		zlog_err("Node '%s' is not an RPC/action", xpath);
		return LY_ENOTFOUND;
	}
	/* Get the tree for the RPC/Action */
	err = yang_new_path2(NULL, ly_native_ctx, xpath, NULL, 0, 0, 0, NULL, &dnode);
	if (err) {
		zlog_err("Failed to create parent node for action: %s", ly_last_errmsg());
		goto done;
	}

	if (!data)
		goto done;

	err = ly_in_new_memory(data, &in);
	if (err) {
		zlog_err("Failed to initialize ly_in: %s", ly_last_errmsg());
		goto done;
	}

	err = yang_parse_op(ly_native_ctx, dnode, in, format,
			    reply ? LYD_TYPE_REPLY_RESTCONF : LYD_TYPE_RPC_RESTCONF, NULL, NULL);
	ly_in_free(in, 0);
	if (err) {
		zlog_err("Failed to parse RPC/action: %s", ly_last_errmsg());
		goto done;
	}
done:
	if (err)
		lyd_free_all(dnode);
	else
		*rpc = dnode;
	return err;
}

/*
 * yang_parse_rpc - 解析 YANG 原生风格的 rpc / action 数据。
 * @xpath：rpc / action 路径
 * @format / @data / @reply：同 yang_parse_restconf_rpc
 * @rpc：输出，解析出的操作节点
 * 返回：LY_ERR。
 * 差异：action 需先构造其父节点（用 yang_xpath_pop_node 去掉路径末段）再解析，
 *       rpc 则无需父节点；使用 LYD_TYPE_RPC_YANG / REPLY_YANG 类型。
 */
LY_ERR yang_parse_rpc(const char *xpath, LYD_FORMAT format, const char *data, bool reply,
		      struct lyd_node **rpc)
{
	const struct lysc_node *snode;
	struct lyd_node *parent = NULL;
	struct ly_in *in = NULL;
	LY_ERR err;

	snode = lys_find_path(ly_native_ctx, NULL, xpath, 0);
	if (!snode) {
		zlog_err("Failed to find RPC/action schema node: %s", xpath);
		return LY_ENOTFOUND;
	}

	/* If it's an action, create its parent */
	if (snode->nodetype == LYS_ACTION) {
		char *parent_xpath = XSTRDUP(MTYPE_TMP, xpath);

		if (yang_xpath_pop_node(parent_xpath) != NB_OK) {
			XFREE(MTYPE_TMP, parent_xpath);
			zlog_err("Invalid action xpath: %s", xpath);
			return LY_EINVAL;
		}

		err = yang_new_path2(NULL, ly_native_ctx, parent_xpath, NULL, 0, 0, 0, NULL,
				     &parent);
		XFREE(MTYPE_TMP, parent_xpath);
		if (err) {
			zlog_err("Failed to create parent node for action: %s",
				 ly_last_errmsg());
			return err;
		}
	} else if (snode->nodetype != LYS_RPC) {
		zlog_err("Schema node is not an RPC/action: %s", xpath);
		return LY_EINVAL;
	}

	err = ly_in_new_memory(data, &in);
	if (err) {
		lyd_free_all(parent);
		zlog_err("Failed to initialize ly_in: %s", ly_last_errmsg());
		return err;
	}

	err = yang_parse_op(ly_native_ctx, parent, in, format,
			    reply ? LYD_TYPE_REPLY_YANG : LYD_TYPE_RPC_YANG, NULL, rpc);
	ly_in_free(in, 0);
	if (err) {
		lyd_free_all(parent);
		zlog_err("Failed to parse RPC/action: %s", ly_last_errmsg());
		return err;
	}

	return LY_SUCCESS;
}

/*
 * yang_print_darr - libyang 的打印回调，把输出追加进 darr 动态缓冲。
 * @arg：指向 darr 指针的地址
 * @buf / @count：待写入的数据与长度
 * 返回：实际写入的字节数（等于 count）。
 */
static ssize_t yang_print_darr(void *arg, const void *buf, size_t count)
{
	uint8_t *dst = darr_append_n(*(uint8_t **)arg, count);

	memcpy(dst, buf, count);
	return count;
}

/*
 * yang_print_tree_append - 把数据树序列化后追加到 darr 缓冲。
 * @darr：目标 darr 缓冲的地址
 * @root：数据树根
 * @format：输出格式
 * @options：打印选项；LYB 二进制格式下会去掉 SHRINK（受格式限制）
 * 返回：LY_ERR；非二进制格式时会在末尾补一个 NUL 终止符。
 */
LY_ERR yang_print_tree_append(uint8_t **darr, const struct lyd_node *root,
			      LYD_FORMAT format, uint32_t options)
{
	LY_ERR err;

	/* Can't use binary+shrink due to required restrictions */
	if (format == LYD_LYB)
		options &= ~LYD_PRINT_SHRINK;

	err = lyd_print_clb(yang_print_darr, darr, root, format, options);
	if (err)
		zlog_err("Failed to save yang tree: %s", ly_last_errmsg());
	else if (format != LYD_LYB)
		*darr_append(*darr) = 0;
	return err;
}

/*
 * yang_print_tree - 把数据树序列化成新分配的 darr 字符串。
 * @root / @format / @options：含义同 yang_print_tree_append
 * 返回：darr 缓冲；失败返回 NULL。
 */
uint8_t *yang_print_tree(const struct lyd_node *root, LYD_FORMAT format,
			 uint32_t options)
{
	uint8_t *darr = NULL;

	if (yang_print_tree_append(&darr, root, format, options))
		return NULL;
	return darr;
}

/*
 * yang_convert_lyd_format - 在 XML / JSON / LYB 之间转换数据格式。
 * @data：输入数据（非 LYB 时必须 NUL 结尾）
 * @data_len：输入长度
 * @in_format / @out_format：输入与输出格式（输出不支持 LYB）
 * @shrink：是否启用 SHRINK 紧凑输出
 * 返回：新分配的字符串；失败返回 NULL。
 * 坑：输入输出格式相同时直接拷贝返回，不做解析。
 */
char *yang_convert_lyd_format(const char *data, size_t data_len,
			      LYD_FORMAT in_format, LYD_FORMAT out_format,
			      bool shrink)
{
	struct lyd_node *tree = NULL;
	uint32_t parse_options = LYD_PARSE_ONLY;
	uint32_t options = LYD_PRINT_WD_EXPLICIT | LYD_PRINT_WITHSIBLINGS;
	uint8_t *result = NULL;
	LY_ERR err;

	assert(out_format != LYD_LYB);

	if (in_format != LYD_LYB && (!data_len || data[data_len - 1] != 0)) {
		zlog_err("Corrupt input data, no NUL terminating byte");
		return NULL;
	}

	if (in_format == out_format)
		return darr_strdup((const char *)data);

#ifdef LYD_PARSE_LYB_SKIP_CTX_CHECK
	if (in_format == LYD_LYB)
		parse_options |= LYD_PARSE_LYB_SKIP_CTX_CHECK;
#endif
	err = lyd_parse_data_mem(ly_native_ctx, (const char *)data, in_format, parse_options, 0,
				 &tree);

	if (err) {
		flog_err_sys(EC_LIB_LIBYANG,
			     "cannot parse input data to convert: %s",
			     ly_last_errmsg());
		return NULL;
	}

	if (shrink)
		options |= LYD_PRINT_SHRINK;

	/* Take a guess at the initial capacity based on input data size */
	darr_ensure_cap(result, data_len);
	err = yang_print_tree_append(&result, tree, out_format, options);
	lyd_free_all(tree);
	if (err) {
		darr_free(result);
		return NULL;
	}
	return (char *)result;
}

/*
 * yang_print_errors - 把 libyang 上下文中缓存的错误格式化进缓冲。
 * @ly_ctx：上下文
 * @buf / @buf_len：输出缓冲与长度
 * 返回：可读的错误字符串（无错误时为空串）。
 * 副作用：调用 ly_err_clean 清空上下文中的错误。
 * 注意：不同 libyang 版本的错误项里路径字段名不同，用宏 data_path 做了兼容。
 */
const char *yang_print_errors(struct ly_ctx *ly_ctx, char *buf, size_t buf_len)
{
	const struct ly_err_item *ei;

	ei = ly_err_first(ly_ctx);
	if (!ei)
		return "";

	strlcpy(buf, "YANG error(s):\n", buf_len);
#if (LY_VERSION_MAJOR < 3)
#define data_path path
#else
#define data_path data_path
#endif
	for (; ei; ei = ei->next) {
		if (ei->data_path) {
			strlcat(buf, " Path: ", buf_len);
			strlcat(buf, ei->data_path, buf_len);
			strlcat(buf, "\n", buf_len);
		}
		strlcat(buf, " Error: ", buf_len);
		strlcat(buf, ei->msg, buf_len);
		strlcat(buf, "\n", buf_len);
	}
#undef data_path

	ly_err_clean(ly_ctx, NULL);

	return buf;
}

/*
 * yang_debugging_set - 开关 libyang 的调试日志。
 * @enable：true 打开最详细日志并启用全部调试分组，false 只保留错误级别。
 */
void yang_debugging_set(bool enable)
{
	if (enable) {
		ly_log_level(LY_LLDBG);
		ly_log_dbg_groups(0xFF);
	} else {
		ly_log_level(LY_LLERR);
		ly_log_dbg_groups(0);
	}
}

/*
 * yang_ctx_new_setup - 创建并配置一个新的 libyang 上下文。
 * @embedded_modules：是否注册内嵌模块的导入回调
 * @explicit_compile：是否延迟到显式调用时才编译模块
 * @load_library：是否允许从系统 YANG 库搜索模块
 * 返回：新上下文；失败返回 NULL。
 * 坑：模型目录不可访问时会退化为不设搜索路径；选项按 libyang 版本条件添加。
 */
struct ly_ctx *yang_ctx_new_setup(bool embedded_modules, bool explicit_compile, bool load_library)
{
	struct ly_ctx *ctx = NULL;
	const char *yang_models_path = YANG_MODELS_PATH;
	uint options;
	LY_ERR err;

	if (access(yang_models_path, R_OK | X_OK)) {
		yang_models_path = NULL;
		if (errno == ENOENT)
			zlog_info("yang model directory \"%s\" does not exist",
				  YANG_MODELS_PATH);
		else
			flog_err_sys(EC_LIB_LIBYANG,
				     "cannot access yang model directory \"%s\"",
				     YANG_MODELS_PATH);
	}

	options = LY_CTX_DISABLE_SEARCHDIR_CWD;
#if (LY_VERSION_MAJOR >= 4)
	options |= LY_CTX_LYB_HASHES;
#endif
	if (!load_library)
		options |= LY_CTX_NO_YANGLIBRARY;
	if (explicit_compile)
		options |= LY_CTX_EXPLICIT_COMPILE;
	err = ly_ctx_new(yang_models_path, options, &ctx);
	if (err)
		return NULL;

	if (embedded_modules)
		ly_ctx_set_module_imp_clb(ctx, yang_module_imp_clb, NULL);

	return ctx;
}

/*
 * yang_init - 初始化 libyang 与全局原生上下文。
 * @embedded_modules / @defer_compile / @load_library：透传给 yang_ctx_new_setup
 * 副作用：设置日志回调与日志选项，创建 ly_native_ctx（失败则 exit(1)），并调用
 *         yang_translator_init 初始化翻译层。
 */
void yang_init(bool embedded_modules, bool defer_compile, bool load_library)
{
	/* Initialize libyang global parameters that affect all containers. */
	ly_set_log_clb(ly_zlog_cb
#if (LY_VERSION_MAJOR < 3)
		       ,
		       1
#endif
	);
	ly_log_options(LY_LOLOG | LY_LOSTORE);

	/* Initialize libyang container for native models. */
	ly_native_ctx = yang_ctx_new_setup(embedded_modules, defer_compile, load_library);
	if (!ly_native_ctx) {
		flog_err(EC_LIB_LIBYANG, "%s: ly_ctx_new() failed", __func__);
		exit(1);
	}

	yang_translator_init();
}

/*
 * yang_init_loading_complete - 所有模块加载完成后统一编译。
 * 副作用：编译失败打印错误并 exit(1)。采用延迟编译时必须显式调用。
 */
void yang_init_loading_complete(void)
{
	/* Compile everything */
	if (ly_ctx_compile(ly_native_ctx) != LY_SUCCESS) {
		flog_err(EC_LIB_YANG_MODULE_LOAD,
			 "%s: failed to compile loaded modules: %s", __func__,
			 ly_errmsg(ly_native_ctx));
		exit(1);
	}
}

/*
 * yang_terminate - 释放 YANG 子系统的全局资源。
 * 先终止翻译层，再清空模块 RB 树，最后销毁 libyang 上下文。
 * 注意：不逐个调用 ly_ctx_remove_module（它会连带移除依赖它的模块），改由
 *       ly_ctx_destroy 统一回收内存。
 */
void yang_terminate(void)
{
	struct yang_module *module;

	yang_translator_terminate();

	while (!RB_EMPTY(yang_modules, &yang_modules)) {
		module = RB_ROOT(yang_modules, &yang_modules);

		/*
		 * We shouldn't call ly_ctx_remove_module() here because this
		 * function also removes other modules that depend on it.
		 *
		 * ly_ctx_destroy() will release all memory for us.
		 */
		RB_REMOVE(yang_modules, &yang_modules, module);
		XFREE(MTYPE_YANG_MODULE, module);
	}

	ly_ctx_destroy(ly_native_ctx);
}

/*
 * yang_dnode_get_parent - 沿父链查找指定名字的祖先节点。
 * @dnode：起点
 * @name：目标节点的 schema 名字
 * 返回：首个名字匹配的 list 或容器祖先；找不到返回 NULL。
 */
const struct lyd_node *yang_dnode_get_parent(const struct lyd_node *dnode,
					     const char *name)
{
	const struct lyd_node *orig_dnode = dnode;

	while (orig_dnode) {
		switch (orig_dnode->schema->nodetype) {
		case LYS_LIST:
		case LYS_CONTAINER:
			if (!strcmp(orig_dnode->schema->name, name))
				return orig_dnode;
			break;
		default:
			break;
		}

		orig_dnode = lyd_parent(orig_dnode);
	}

	return NULL;
}

/*
 * yang_is_last_list_dnode - 判断节点是否是同级同类 list 中的“最后一个”。
 * 返回：true 表示它前后相邻的兄弟都不是同类 list 节点（即该实例前后无同类）。
 * 用途：配合 yang_is_last_level_dnode 判断某个子树能否安全地整体上提。
 */
bool yang_is_last_list_dnode(const struct lyd_node *dnode)
{
	return (((dnode->next == NULL)
	     || (dnode->next
		 && (strcmp(dnode->next->schema->name, dnode->schema->name)
		     != 0)))
	    && dnode->prev
	    && ((dnode->prev == dnode)
		|| (strcmp(dnode->prev->schema->name, dnode->schema->name)
		    != 0)));
}

/*
 * yang_is_last_level_dnode - 判断 list 节点是否处于其 key 层级的末尾位置。
 * 返回：list 时依据前置 key 叶子链推断；容器恒为 true；其它返回 false。
 * 注意：内部含有作者自述“难以理解”的 key 位置推断逻辑，改动需谨慎。
 */
bool yang_is_last_level_dnode(const struct lyd_node *dnode)
{
	const struct lyd_node *parent;
	const struct lyd_node *key_leaf;
	uint8_t keys_size;

	switch (dnode->schema->nodetype) {
	case LYS_LIST:
		assert(dnode->parent);
		parent = lyd_parent(dnode);
		uint snode_num_keys = yang_snode_num_keys(parent->schema);
		/* XXX libyang2: q: really don't understand this code. */
		key_leaf = dnode->prev;
		for (keys_size = 1; keys_size < snode_num_keys; keys_size++)
			key_leaf = key_leaf->prev;
		if (key_leaf->prev == dnode)
			return true;
		break;
	case LYS_CONTAINER:
		return true;
	default:
		break;
	}

	return false;
}

/*
 * yang_get_subtree_with_no_sibling - 上找出“没有兄弟”的最外层子树节点。
 * @dnode：起点，非 list 时直接返回自身
 * 返回：能安全独立表示该 list 实例的最外层祖先节点。
 * 逻辑：逐层上溯，只有当前节点是同类 list 的最后一个、父链属于同一模块、且途经
 *       容器是非 presence 容器时，才继续上提，否则停下。
 */
const struct lyd_node *
yang_get_subtree_with_no_sibling(const struct lyd_node *dnode)
{
	bool parent = true;
	const struct lyd_node *node;

	node = dnode;
	if (node->schema->nodetype != LYS_LIST)
		return node;

	while (parent) {
		switch (node->schema->nodetype) {
		case LYS_CONTAINER:
			if (!CHECK_FLAG(node->schema->flags, LYS_PRESENCE)) {
				if (node->parent
				    && (node->parent->schema->module
					== dnode->schema->module))
					node = lyd_parent(node);
				else
					parent = false;
			} else
				parent = false;
			break;
		case LYS_LIST:
			if (yang_is_last_list_dnode(node)
			    && yang_is_last_level_dnode(node)) {
				if (node->parent
				    && (node->parent->schema->module
					== dnode->schema->module))
					node = lyd_parent(node);
				else
					parent = false;
			} else
				parent = false;
			break;
		default:
			parent = false;
			break;
		}
	}
	return node;
}

/*
 * yang_get_list_pos - 返回数据节点在其同级 list 中的位置索引。
 * 直接包装 libyang 的 lyd_list_pos。
 */
uint32_t yang_get_list_pos(const struct lyd_node *node)
{
	return lyd_list_pos(node);
}

/*
 * yang_get_list_elements_count - 统计同级中同一 schema 的 list 实例数。
 * @node：起始数据节点
 * 返回：从该节点起（含）向后遍历，schema 相同的兄弟节点个数。
 * 注意：从传入节点开始计数，不包含它前面的兄弟。
 */
uint32_t yang_get_list_elements_count(const struct lyd_node *node)
{
	unsigned int count;
	const struct lysc_node *schema;

	if (!node
	    || ((node->schema->nodetype != LYS_LIST)
		&& (node->schema->nodetype != LYS_LEAFLIST))) {
		return 0;
	}

	schema = node->schema;
	count = 0;
	do {
		if (node->schema == schema)
			++count;
		node = node->next;
	} while (node);
	return count;
}
/*
 * yang_get_key_pred_strlen - 预估所有 key 谓词串的总长度。
 * @snode：list 的 schema 节点
 * @keys：各 key 的取值
 * 返回：拼成“方括号 名字 等号 值 方括号”形式所需的总字节数（不含结尾 NUL）。
 */
int yang_get_key_pred_strlen(const struct lysc_node *snode, const struct yang_list_keys *keys)
{
	const struct lysc_node_leaf *skey;
	size_t len = 0;
	size_t i = 0;

	LY_FOR_KEYS (snode, skey) {
		/* [%s='%s'] */
		len += 5 + strlen(skey->name) + strlen(keys->key[i]);
		i++;
	}
	return len;
}

/*
 * yang_get_key_preds - 把各 key 的谓词写入字符串缓冲。
 * @s：输出缓冲
 * @snode：list 的 schema 节点
 * @keys：各 key 的取值
 * @space：缓冲可用空间
 * 返回：写入的 key 个数；空间不足时把 len 顶到 space（即截断）。
 * 副作用：断言实际写入的 key 数量与 keys->num 一致。
 */
int yang_get_key_preds(char *s, const struct lysc_node *snode, const struct yang_list_keys *keys, ssize_t space)
{
	const struct lysc_node_leaf *skey;
	ssize_t len2, len = 0;
	ssize_t i = 0;

	LY_FOR_KEYS (snode, skey) {
		assert(i < keys->num);
		len2 = snprintf(s + len, space - len, "[%s='%s']", skey->name,
				keys->key[i]);
		if (len2 > space - len)
			len = space;
		else
			len += len2;
		i++;
	}

	assert(i == keys->num);
	return i;
}

/*
 * yang_get_node_keys - 从 list 数据节点中读出各 key 的取值。
 * @node：list 数据节点
 * @keys：输出结构，填充 key 数组与数量
 * 返回：NB_OK；某个 key 取值为空时返回 NB_ERR。
 */
int yang_get_node_keys(struct lyd_node *node, struct yang_list_keys *keys)
{
	struct lyd_node *child = lyd_child(node);

	keys->num = 0;
	for (; child && lysc_is_key(child->schema); child = child->next) {
		const char *value = lyd_get_value(child);

		if (!value)
			return NB_ERR;
		strlcpy(keys->key[keys->num], value,
			sizeof(keys->key[keys->num]));
		keys->num++;
	}
	return NB_OK;
}

/*
 * yang_xpath_pop_node - 就地去掉 xpath 的最后一段节点。
 * @xpath：原地修改的路径字符串
 * 返回：NB_OK，或路径为空 / 未找到斜杠分隔时 NB_ERR_NOT_FOUND。
 * 副作用：把末段起始处的斜杠改写为字符串结束符。
 * 用途：例如为 action 构造其父节点路径。
 */
int yang_xpath_pop_node(char *xpath)
{
	int len = strlen(xpath);
	bool abs = xpath[0] == '/';
	char *slash;

	/* "//" or "/" => NULL */
	if (abs && (len == 1 || (len == 2 && xpath[1] == '/')))
		return NB_ERR_NOT_FOUND;

	slash = (char *)frrstr_back_to_char(xpath, '/');
	/* "/foo/bar/" or "/foo/bar//" => "/foo " */
	if (slash && slash == &xpath[len - 1]) {
		xpath[--len] = 0;
		slash = (char *)frrstr_back_to_char(xpath, '/');
		if (slash && slash == &xpath[len - 1]) {
			xpath[--len] = 0;
			slash = (char *)frrstr_back_to_char(xpath, '/');
		}
	}
	if (!slash)
		return NB_ERR_NOT_FOUND;
	*slash = 0;
	return NB_OK;
}

/*
 * 以下为“未来函数”区：部分功能在当前要求的 libyang 版本中尚未提供（或存在缺陷），
 * FRR 自行给出等价实现，并用条件编译包裹，将来可切回上游实现。
 */
/*
 * ------------------------
 * Libyang Future Functions
 * ------------------------
 *
 * All these functions are implemented in libyang versions (perhaps unreleased)
 * beyond what we require currently so we must supply the functionality.
 */

/*
 * Safe to remove after libyang v2.1.xxx is required (.144 has a bug so
 * something > .144) https://github.com/CESNET/libyang/issues/2149
 */
/*
 * yang_lyd_new_list - 新建一个 list 数据节点（兼容旧版 libyang）。
 * @parent：父数据节点
 * @snode：list 的 schema 节点
 * @list_keys：各 key 取值
 * @node：输出，新建的节点
 * 返回：LY_ERR。
 * 坑：旧版 lyd_new_list 不支持变长 key，这里用 switch 展开 0 到 8 个 key 的调用；
 *       HAVE_LYD_NEW_LIST3 分支被 #if 0 关闭，说明该上游修复在此环境尚不可用。
 */
LY_ERR yang_lyd_new_list(struct lyd_node *parent, const struct lysc_node *snode,
			 const struct yang_list_keys *list_keys, struct lyd_node **node)
{
#if defined(HAVE_LYD_NEW_LIST3) && 0
	LY_ERR err;
	const char *keys[LIST_MAXKEYS];

	assert(list_keys->num <= LIST_MAXKEYS);
	for (int i = 0; i < list_keys->num; i++)
		keys[i] = list_keys->key[i];

	err = lyd_new_list3(parent, snode->module, snode->name, keys, NULL, 0, node);
	return err;
#else
	struct lyd_node *pnode = parent;
	const char(*keys)[LIST_MAXKEYLEN] = list_keys->key;

	assert(list_keys->num <= 8);
	switch (list_keys->num) {
	case 0:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node);
	case 1:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0]);
	case 2:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1]);
	case 3:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1], keys[2]);
	case 4:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1], keys[2], keys[3]);
	case 5:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1], keys[2], keys[3],
				    keys[4]);
	case 6:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1], keys[2], keys[3],
				    keys[4], keys[5]);
	case 7:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1], keys[2], keys[3],
				    keys[4], keys[5], keys[6]);
	case 8:
		return lyd_new_list(pnode, snode->module, snode->name, false,
				    node, keys[0], keys[1], keys[2], keys[3],
				    keys[4], keys[5], keys[6], keys[7]);
	}
	_Static_assert(LIST_MAXKEYS == 8, "max key mismatch in switch unroll");
	/*NOTREACHED*/
	return LY_EINVAL;
#endif
}


/*
 * Safe to remove after libyang v2.1.144 is required
 */
/*
 * yang_lyd_trim_xpath - 裁剪数据树，只保留匹配 xpath 的节点及其祖先。
 * @root：数据树根指针的地址（可能被改写）
 * @xpath：保留目标的路径
 * 返回：LY_ERR。
 * 策略：若 libyang 已提供 lyd_trim_xpath 则直接调用；否则自行实现：先标记要保留的
 *       节点（用 priv 指针区分“命中”与“祖先”），再深度优先删除未标记的子树。
 * 坑：回退实现假设数据节点的 priv 为空，会借用 priv 做临时标记。
 */
LY_ERR yang_lyd_trim_xpath(struct lyd_node **root, const char *xpath)
{
	LY_ERR err;
#ifdef HAVE_LYD_TRIM_XPATH
	err = lyd_trim_xpath(root, xpath, NULL);
	if (err) {
		flog_err_sys(EC_LIB_LIBYANG,
			     "cannot obtain specific result for xpath \"%s\": %s",
			     xpath, yang_ly_strerrcode(err));
		return err;
	}
	return LY_SUCCESS;
#else
	struct lyd_node *node, *sib;
	struct lyd_node **remove = NULL;
	struct ly_set *set = NULL;
	uint32_t i;

	*root = lyd_first_sibling(*root);

	err = yang_lyd_find_xpath3(NULL, *root, xpath, LY_VALUE_JSON, NULL,
				   NULL, &set);
	if (err) {
		flog_err_sys(EC_LIB_LIBYANG,
			     "cannot obtain specific result for xpath \"%s\": %s",
			     xpath, yang_ly_strerrcode(err));
		return err;
	}
	/*
	 * Mark keepers and sweep deleting non-keepers.
	 *
	 * NOTE: We assume the data-nodes have NULL priv pointers and use that
	 * for our mark.
	 */

	/* Mark */
	for (i = 0; i < set->count; i++) {
		for (node = set->dnodes[i]; node; node = lyd_parent(node)) {
			if (node->priv)
				break;
			if (node == set->dnodes[i])
				node->priv = (void *)2;
			else
				node->priv = (void *)1;
		}
	}

	darr_ensure_cap(remove, 128);
	LY_LIST_FOR(*root, sib) {
		LYD_TREE_DFS_BEGIN (sib, node) {
			/*
			 * If this is a direct matching node then include its
			 * subtree which won't be marked and would otherwise
			 * be removed.
			 */
			if (node->priv == (void *)2)
				LYD_TREE_DFS_continue = 1;
			else if (!node->priv) {
				*darr_append(remove) = node;
				LYD_TREE_DFS_continue = 1;
			}
			LYD_TREE_DFS_END(sib, node);
		}
	}
	darr_foreach_i (remove, i) {
		if (remove[i] == *root) {
			assert(*root);
			*root = (*root)->next;
		}
		lyd_free_tree(remove[i]);
	}
	darr_free(remove);

	ly_set_free(set, NULL);

	return LY_SUCCESS;
#endif
}

/* Can be replaced by `lyd_parse_data` with libyang >= 2.1.156 */
/*
 * yang_lyd_parse_data - 解析数据，并修正旧版 libyang 的返回值缺陷。
 * @ctx / @parent / @in / @format / @parse_options / @validate_options / @tree：同
 *   lyd_parse_data 的对应参数
 * 返回：LY_ERR。
 * 背景：2.1.156 之前，当 parent 非 NULL 且仅解析（LYD_PARSE_ONLY）时，上游不会回填
 *       *tree；这里退而遍历 parent 的子节点，找出带 LYD_NEW 标志的那个作为结果。
 */
LY_ERR yang_lyd_parse_data(const struct ly_ctx *ctx, struct lyd_node *parent,
			   struct ly_in *in, LYD_FORMAT format,
			   uint32_t parse_options, uint32_t validate_options,
			   struct lyd_node **tree)
{
	struct lyd_node *child;
	LY_ERR err;

	err = lyd_parse_data(ctx, parent, in, format, parse_options,
			     validate_options, tree);
	if (err)
		return err;

	if (!parent || !(parse_options & LYD_PARSE_ONLY))
		return LY_SUCCESS;

	/*
	 * Versions prior to 2.1.156 don't return `tree` if `parent` is not NULL
	 * and validation is disabled (`LYD_PARSE_ONLY`). To work around this,
	 * go through the children and find the one with `LYD_NEW` flag set.
	 */
	*tree = NULL;

	LY_LIST_FOR (lyd_child_no_keys(parent), child) {
		if (child->flags & LYD_NEW) {
			*tree = child;
			break;
		}
	}

	assert(tree);

	return LY_SUCCESS;
}


/*
 * 以下用宏与包装函数抹平不同 libyang 版本间 NBC（节点操作）API 的差异：
 * 先取消同名宏定义，再用 LY_SZ 统一“字节 / 比特”长度单位，最后逐版本适配。
 */
/*
 * Handle NBC API changes between versions of Libyang
 */

#undef lyd_new_term_bin
#undef lyd_change_term_bin
#undef lyd_new_path2
#undef lyd_new_ext_term

#if (LY_VERSION_MAJOR >= 4)
#define LY_SZ(x) ((x)*8)
#else
#define LY_SZ(x) (x)
#endif

/*
 * yang_new_term_bin - 新建一个二进制叶子（抹平长度单位差异）。
 * @size：以字节计的长度，内部经 LY_SZ 转成当前版本所需单位
 * 返回：LY_ERR。
 */
LY_ERR yang_new_term_bin(struct lyd_node *parent, const struct lys_module *module,
			 const char *name, const void *value, uint32_t size, uint32_t options,
			 struct lyd_node **node)
{
	return lyd_new_term_bin(parent, module, name, value, LY_SZ(size), options, node);
}

/*
 * yang_change_term_bin - 修改二进制叶子的取值。
 * @size：以字节计的长度，内部经 LY_SZ 转换
 * 返回：LY_ERR。
 */
LY_ERR yang_change_term_bin(struct lyd_node *term, const void *value, uint32_t size)
{
	return lyd_change_term_bin(term, value, LY_SZ(size));
}

/*
 * yang_new_path2 - 按路径新建或更新一条数据节点链（带版本兼容）。
 * @path：目标路径
 * @value / @size / @value_type：节点取值及其类型
 * @options：创建选项
 * @new_parent / @new_node：输出，新建的父节点与目标节点
 * 返回：LY_ERR。
 */
LY_ERR yang_new_path2(struct lyd_node *parent, const struct ly_ctx *ctx, const char *path,
		      const void *value, uint32_t size, LYD_ANYDATA_VALUETYPE value_type,
		      uint32_t options, struct lyd_node **new_parent, struct lyd_node **new_node)
{
	return lyd_new_path2(parent, ctx, path, value, LY_SZ(size), value_type, options,
			     new_parent, new_node);
}

#if (LY_VERSION_MAJOR >= 3) && (LY_VERSION_MAJOR < 5)
/*
 * yang_new_ext_term - 在扩展实例下新建叶子（仅在 libyang 3 到 4 之间提供）。
 * 返回：LY_ERR。函数体用条件编译限定版本范围。
 */
LY_ERR yang_new_ext_term(const struct lysc_ext_instance *ext, const char *name, const void *value,
			 uint32_t size, uint32_t options, struct lyd_node **node)
{
	return lyd_new_ext_term(ext, name, value, LY_SZ(size), options, node);
}
#endif
#undef LY_SZ

/*
 * Safe to remove after libyang v2.1.128 is required
 */
/*
 * yang_ly_strerrcode - 把 libyang 错误码转成可读字符串。
 * @err：LY_ERR 错误码
 * 返回：错误描述字符串。
 * 注意：libyang 已提供 ly_strerrcode 时直接转发，否则用本地 switch 翻译。
 */
const char *yang_ly_strerrcode(LY_ERR err)
{
#ifdef HAVE_LY_STRERRCODE
	return ly_strerrcode(err);
#else
	switch (err) {
	case LY_SUCCESS:
		return "ok";
	case LY_EMEM:
		return "out of memory";
	case LY_ESYS:
		return "system error";
	case LY_EINVAL:
		return "invalid value given";
	case LY_EEXIST:
		return "item exists";
	case LY_ENOTFOUND:
		return "item not found";
	case LY_EINT:
		return "operation interrupted";
	case LY_EVALID:
		return "validation failed";
	case LY_EDENIED:
		return "access denied";
	case LY_EINCOMPLETE:
		return "incomplete";
	case LY_ERECOMPILE:
		return "compile error";
	case LY_ENOT:
		return "not";
	case LY_EPLUGIN:
	case LY_EOTHER:
		return "other";
	default:
		return "unknown";
	}
#endif
}

/*
 * Safe to remove after libyang v2.1.128 is required
 */
/*
 * yang_ly_strvecode - 把 libyang 校验错误码转成可读字符串。
 * @vecode：LY_VECODE 校验错误码
 * 返回：错误描述字符串。
 * 注意：libyang 已提供 ly_strvecode 时直接转发，否则用本地 switch 翻译。
 */
const char *yang_ly_strvecode(LY_VECODE vecode)
{
#ifdef HAVE_LY_STRVECODE
	return ly_strvecode(vecode);
#else
	switch (vecode) {
	case LYVE_SUCCESS:
		return "";
	case LYVE_SYNTAX:
		return "syntax";
	case LYVE_SYNTAX_YANG:
		return "yang-syntax";
	case LYVE_SYNTAX_YIN:
		return "yin-syntax";
	case LYVE_REFERENCE:
		return "reference";
	case LYVE_XPATH:
		return "xpath";
	case LYVE_SEMANTICS:
		return "semantics";
	case LYVE_SYNTAX_XML:
		return "xml-syntax";
	case LYVE_SYNTAX_JSON:
		return "json-syntax";
	case LYVE_DATA:
		return "data";
	case LYVE_OTHER:
		return "other";
	default:
		return "unknown";
	}
#endif
}
