// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 *
 * flex_algo.c: Flexible Algorithm library
 *
 * Authors
 * -------
 * Hiroki Shirokura
 * Masakazu Asama
 * Louis Scalbert
 */

/*
 * ============================================================================
 * flex_algo.c —— IGP Flexible Algorithm（RFC 9350）公共库实现
 * ============================================================================
 *
 * 本文件实现 flex_algo.h 中声明的工具函数，供 IS-IS 与 OSPF 共用：
 * 负责 Flex-Algo 数据库与其内各算法对象的“增删查”，以及定义比较、
 * 算法号合法性判断、度量类型格式化、本地启用状态读写等通用逻辑。
 *
 * 设计要点：
 *   - 数据库 struct flex_algos 内部用一条 linklist 保存所有 struct flex_algo
 *   - 通过 allocator / releaser 回调把 IGP 私有数据（fa->data）的生命周期
 *     与算法对象绑定，实现协议无关的公共层
 *   - 所有对象分配使用本文件的 MTYPE（FLEX_ALGO_DATABASE / FLEX_ALGO），
 *     便于内存统计
 *
 * 与其它模块关系：实现声明于 flex_algo.h；数据结构与 admin_group、
 * segment_routing 相关；被 isisd / ospfd 的 flex-algo 代码调用。
 *
 * 阅读顺序：flex_algos_alloc / free -> flex_algo_alloc / free ->
 *           flex_algo_lookup -> definition_cmp -> 其余工具函数。
 * ============================================================================
 */

#include "zebra.h"

#include "flex_algo.h"

/*
 * FLEX_ALGO_DATABASE - 数据库（struct flex_algos）的专用内存类型。
 * FLEX_ALGO          - 单个算法（struct flex_algo）的专用内存类型。
 */
DEFINE_MTYPE_STATIC(LIB, FLEX_ALGO_DATABASE, "Flex-Algo database");
DEFINE_MTYPE_STATIC(LIB, FLEX_ALGO, "Flex-Algo algorithm information");

/*
 * flex_algos_alloc - 创建 Flex-Algo 数据库
 * @allocator 可选：创建算法时用于分配 IGP 私有数据，可为 NULL
 * @releaser  可选：销毁算法时用于释放 IGP 私有数据，可为 NULL
 * 返回：新分配的 struct flex_algos（内部 flex_algos 列表已初始化）
 * 副作用：分配内存并 list_new()；需用 flex_algos_free() 释放。
 */
struct flex_algos *flex_algos_alloc(flex_algo_allocator_t allocator,
				    flex_algo_releaser_t releaser)
{
	struct flex_algos *flex_algos;

	flex_algos =
		XCALLOC(MTYPE_FLEX_ALGO_DATABASE, sizeof(struct flex_algos));
	flex_algos->flex_algos = list_new();
	flex_algos->allocator = allocator;
	flex_algos->releaser = releaser;
	return flex_algos;
}

/*
 * flex_algos_free - 释放整个 Flex-Algo 数据库
 * @flex_algos 待释放的数据库
 * 副作用：遍历列表逐条调用 flex_algo_free，再删列表并释放自身。
 * 坑：调用后传入指针即失效，调用方需自行置空。
 */
void flex_algos_free(struct flex_algos *flex_algos)
{
	struct listnode *node, *nnode;
	struct flex_algo *fa;

	for (ALL_LIST_ELEMENTS(flex_algos->flex_algos, node, nnode, fa))
		flex_algo_free(flex_algos, fa);
	list_delete(&flex_algos->flex_algos);
	XFREE(MTYPE_FLEX_ALGO_DATABASE, flex_algos);
}

/*
 * flex_algo_alloc - 新建一个 Flex-Algo 定义并加入数据库
 * @flex_algos 数据库
 * @algorithm  算法号
 * @arg        透传给数据库 allocator 的参数
 * 返回：新分配的 struct flex_algo（已加入数据库列表）
 * 副作用：从 FLEX_ALGO 分配；若数据库有 allocator 则调用它初始化 fa->data；
 *         初始化三组 admin_group。
 * 注意：本函数不检查算法号是否重复，重复由调用方保证。
 */
struct flex_algo *flex_algo_alloc(struct flex_algos *flex_algos,
				  uint8_t algorithm, void *arg)
{
	struct flex_algo *fa;

	fa = XCALLOC(MTYPE_FLEX_ALGO, sizeof(struct flex_algo));
	fa->algorithm = algorithm;
	if (flex_algos->allocator)
		fa->data = flex_algos->allocator(arg);
	admin_group_init(&fa->admin_group_exclude_any);
	admin_group_init(&fa->admin_group_include_any);
	admin_group_init(&fa->admin_group_include_all);
	listnode_add(flex_algos->flex_algos, fa);
	return fa;
}

/*
 * flex_algo_free - 释放并移除一个 Flex-Algo 定义
 * @flex_algos 数据库
 * @fa         待释放的算法
 * 副作用：若数据库有 releaser 则先释放 fa->data；终止三组 admin_group；
 *         从列表移除并释放结构体。
 */
void flex_algo_free(struct flex_algos *flex_algos, struct flex_algo *fa)
{
	if (flex_algos->releaser)
		flex_algos->releaser(fa->data);
	admin_group_term(&fa->admin_group_exclude_any);
	admin_group_term(&fa->admin_group_include_any);
	admin_group_term(&fa->admin_group_include_all);
	listnode_delete(flex_algos->flex_algos, fa);
	XFREE(MTYPE_FLEX_ALGO, fa);
}

/**
 * @brief Look up the local flex-algo object by its algorithm number.
 * @param algorithm flex-algo algorithm number
 * @param area area pointer of flex-algo
 * @return local flex-algo object if exist, else NULL
 */
/*
 * flex_algo_lookup - 按算法号查找本地算法对象
 * @flex_algos 数据库
 * @algorithm  算法号
 * 返回：找到则返回对应的 struct flex_algo，否则 NULL
 * 复杂度：线性遍历数据库列表（算法数量很小，无需索引）。
 */
struct flex_algo *flex_algo_lookup(struct flex_algos *flex_algos,
				   uint8_t algorithm)
{
	struct listnode *node;
	struct flex_algo *fa;

	for (ALL_LIST_ELEMENTS_RO(flex_algos->flex_algos, node, fa))
		if (fa->algorithm == algorithm)
			return fa;
	return NULL;
}

/**
 * @brief Compare two Flex-Algo Definitions (FAD)
 * @param Flex algo 1
 * @param Flex algo 2
 * @return true if the definition is equal, else false
 */
/*
 * flex_algo_definition_cmp - 比较两份 Flex-Algo 定义是否一致
 * @fa1 算法定义 1
 * @fa2 算法定义 2
 * 返回：完全一致返回 true，否则 false
 * 比较字段：algorithm / calc_type / metric_type / exclude_srlg / flags /
 *           unsupported_subtlv，以及 exclude_any / include_all / include_any
 *           三组 admin_group。
 * 说明：比较的是“定义”，不含 state、dataplanes 等本地运行数据。
 */
bool flex_algo_definition_cmp(struct flex_algo *fa1, struct flex_algo *fa2)
{
	if (fa1->algorithm != fa2->algorithm)
		return false;
	if (fa1->calc_type != fa2->calc_type)
		return false;
	if (fa1->metric_type != fa2->metric_type)
		return false;
	if (fa1->exclude_srlg != fa2->exclude_srlg)
		return false;
	if (fa1->flags != fa2->flags)
		return false;
	if (fa1->unsupported_subtlv != fa2->unsupported_subtlv)
		return false;

	if (!admin_group_cmp(&fa1->admin_group_exclude_any,
			     &fa2->admin_group_exclude_any))
		return false;
	if (!admin_group_cmp(&fa1->admin_group_include_all,
			     &fa2->admin_group_include_all))
		return false;
	if (!admin_group_cmp(&fa1->admin_group_include_any,
			     &fa2->admin_group_include_any))
		return false;

	return true;
}

/**
 * Check SR Algorithm is Flex-Algo
 * according to RFC9350 section 4
 *
 * @param algorithm SR Algorithm
 */
/*
 * flex_algo_id_valid - 判断 SR 算法号是否属于 Flex-Algo 区间
 * @algorithm SR 算法号
 * 返回：在 [SR_ALGORITHM_FLEX_MIN, SR_ALGORITHM_FLEX_MAX] 内为 true
 * 依据：RFC9350 第 4 节。
 */
bool flex_algo_id_valid(uint16_t algorithm)
{
	return algorithm >= SR_ALGORITHM_FLEX_MIN &&
	       algorithm <= SR_ALGORITHM_FLEX_MAX;
}

/*
 * flex_algo_metric_type_print - 把度量类型格式化为字符串
 * @type_str    输出缓冲区
 * @sz         缓冲区大小
 * @metric_type 度量类型
 * 返回：传入的 type_str 缓冲区
 * 映射：MT_IGP -> "igp"，MT_MIN_UNI_LINK_DELAY -> "delay"，MT_TE_DEFAULT -> "te"。
 * 坑：若 metric_type 不属于任何已列分支，缓冲区内容保持不变。
 */
char *flex_algo_metric_type_print(char *type_str, size_t sz,
				  enum flex_algo_metric_type metric_type)
{
	switch (metric_type) {
	case MT_IGP:
		snprintf(type_str, sz, "igp");
		break;
	case MT_MIN_UNI_LINK_DELAY:
		snprintf(type_str, sz, "delay");
		break;
	case MT_TE_DEFAULT:
		snprintf(type_str, sz, "te");
		break;
	}
	return type_str;
}

/*
 * flex_algo_get_state - 查询指定算法是否在本地启用
 * @flex_algos 数据库
 * @algorithm  算法号
 * 返回：算法存在则返回其 state，不存在返回 false。
 */
bool flex_algo_get_state(struct flex_algos *flex_algos, uint8_t algorithm)
{
	struct flex_algo *fa = flex_algo_lookup(flex_algos, algorithm);

	if (!fa)
		return false;

	return fa->state;
}

/*
 * flex_algo_set_state - 设置指定算法的本地启用状态
 * @flex_algos 数据库
 * @algorithm  算法号
 * @state      目标状态
 * 副作用：仅修改 fa->state；算法不存在则静默返回。
 */
void flex_algo_set_state(struct flex_algos *flex_algos, uint8_t algorithm,
			 bool state)
{
	struct flex_algo *fa = flex_algo_lookup(flex_algos, algorithm);

	if (!fa)
		return;

	fa->state = state;
}
