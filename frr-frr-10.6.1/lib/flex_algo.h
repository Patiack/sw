// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 *
 * flex_algo.h: Flexible Algorithm library
 *
 * Authors
 * -------
 * Hiroki Shirokura
 * Masakazu Asama
 * Louis Scalbert
 */

#ifndef _FRR_FLEX_ALGO_H
#define _FRR_FLEX_ALGO_H

#include "admin_group.h"
#include "linklist.h"
#include "prefix.h"
#include "segment_routing.h"

/*
 * ============================================================================
 * flex_algo.h —— IGP Flexible Algorithm（RFC 9350）公共定义与工具接口
 * ============================================================================
 *
 * 本文件是 IS-IS 与 OSPF 共用的一份 Flexible Algorithm（Flex-Algo，灵活算法）
 * 抽象：一份 Flex-Algo 定义（FAD）由算法号、度量类型、计算类型、优先级、
 * 亲和属性（extended admin-group）等构成；各 IGP 通过 void *data 挂接私有
 * 数据。
 *
 * 核心类型：
 *   - enum flex_algo_metric_type 度量类型：IGP、最小单向链路时延、TE 默认
 *   - struct flex_algo           单个算法的定义与本地状态
 *   - struct flex_algos          算法数据库：flex_algo 列表 + 分配 / 释放回调
 *
 * 分配器回调机制：struct flex_algos 保存 allocator / releaser，由各 IGP 提供，
 * 用于在创建 / 销毁 flex_algo 时同步管理 IGP 私有数据（fa->data）。
 *
 * 与其它模块关系：依赖 admin_group（亲和属性）、segment_routing（SR 算法号
 * 范围 SR_ALGORITHM_FLEX_MIN / MAX）、linklist、prefix。
 *
 * 阅读顺序：结构体 flex_algo -> flex_algos -> 文件末尾工具函数声明，
 *           实现见 flex_algo.c。
 * ============================================================================
 */

/*
 * Flex-Algo 定义的默认优先级（RFC9350 规定缺省为 128）。
 * 多个路由器宣告同一算法定义冲突时，优先级高者胜出。
 */
#define FLEX_ALGO_PRIO_DEFAULT 128

/*
 * 计算类型：目前仅支持基于 SPF 的算法（取值为 0）。
 */
#define CALC_TYPE_SPF 0

/* flex-algo definition flags */

/* M-flag (aka. prefix-metric)
 * Flex-Algorithm specific prefix and ASBR metric MUST be used
 */
/*
 * FAD_FLAG_M - Flex-Algo 定义标志位中的 M-flag（即 prefix-metric）。
 * 置位表示算法特定的前缀 / ASBR 度量必须被使用（RFC9350）。
 * 该位位于 flags 字节的最高位。
 */
#define FAD_FLAG_M 0x80

/*
 * Metric Type values from RFC9350 section 5.1
 */
/*
 * enum flex_algo_metric_type - 度量类型（RFC9350 第 5.1 节）。
 * MT_IGP=0 使用 IGP 度量；MT_MIN_UNI_LINK_DELAY=1 使用最小单向链路时延；
 * MT_TE_DEFAULT=2 使用 TE 默认度量。
 */
enum flex_algo_metric_type {
	MT_IGP = 0,
	MT_MIN_UNI_LINK_DELAY = 1,
	MT_TE_DEFAULT = 2,
};


/* Flex-Algo data about a given algorithm.
 * It includes the definition and some local data.
 */
/*
 * struct flex_algo - 某个 Flex-Algo 的完整信息（定义 + 本地数据）。
 * 定义部分：algorithm 算法号、metric_type 度量类型、calc_type 计算类型、
 *           priority 优先级、flags 标志、三组 admin_group 亲和属性、
 *           exclude_srlg 与 unsupported_subtlv 能力标记。
 * 本地部分：advertise_definition 是否需对外通告、dataplanes 使用的数据面、
 *           state 是否本地启用、data 指向 IGP 私有扩展数据。
 */
struct flex_algo {
	/* Flex-Algo definition */
	uint8_t algorithm;
	enum flex_algo_metric_type metric_type;
	uint8_t calc_type;
	uint8_t priority;
	uint8_t flags;

	/* extended admin-groups */
	struct admin_group admin_group_exclude_any;
	struct admin_group admin_group_include_any;
	struct admin_group admin_group_include_all;

	/* Exclude SRLG Sub-TLV is not yet supported by IS-IS
	 * True if a Exclude SRLG Sub-TLV has been found
	 */
	bool exclude_srlg;

	/* True if an unsupported sub-TLV other Exclude SRLG
	 * has been received.
	 * A router that receives an unsupported definition
	 * that is elected must not participate in the algorithm.
	 * This boolean prevents future sub-TLV from being considered
	 * as supported.
	 */
	bool unsupported_subtlv;

	/* Flex-Algo local data */

	/* True if the local definition must be advertised */
	bool advertise_definition;

	/* which dataplane must be used for the algorithm */
#define FLEX_ALGO_SR_MPLS 0x01
#define FLEX_ALGO_SRV6	  0x02
#define FLEX_ALGO_IP	  0x04
	uint8_t dataplanes;

	/* True if the Algorithm is locally enabled (ie. a definition has been
	 * found and is supported).
	 */
	bool state;

	/*
	 * This property can be freely extended among different routing
	 * protocols. Since Flex-Algo is an IGP protocol agnostic, both IS-IS
	 * and OSPF can implement Flex-Algo. The struct flex_algo thus provides
	 * the general data structure of Flex-Algo, and the value of extending
	 * it with the IGP protocol is provided by this property.
	 */
	void *data;
};

/*
 * flex_algo_allocator_t - IGP 提供的分配回调：创建 flex_algo 时调用，
 * 返回存放 IGP 私有数据的指针（赋给 fa->data）。
 */
typedef void *(*flex_algo_allocator_t)(void *arg);
/*
 * flex_algo_releaser_t - IGP 提供的释放回调：销毁 flex_algo 时调用，
 * 释放 fa->data 指向的私有数据。
 */
typedef void (*flex_algo_releaser_t)(void *data);

/*
 * struct flex_algos - Flex-Algo 数据库。
 * flex_algos 是一条 struct flex_algo 列表；
 * allocator / releaser 为可选的 IGP 回调，用于管理各算法的 fa->data。
 */
struct flex_algos {
	flex_algo_allocator_t allocator;
	flex_algo_releaser_t releaser;
	struct list *flex_algos;
};

/*
 * Flex-Algo Utilities
 */
/*
 * flex_algos_alloc - 创建 Flex-Algo 数据库，登记 IGP 的分配 / 释放回调。
 */
struct flex_algos *flex_algos_alloc(flex_algo_allocator_t allocator, flex_algo_releaser_t releaser);
/*
 * flex_algos_free - 释放数据库：逐条释放算法，再释放列表与结构体本身。
 */
void flex_algos_free(struct flex_algos *flex_algos);
/*
 * flex_algo_alloc - 新建一个算法定义并加入数据库；调用 allocator 初始化
 * 私有数据，并初始化三组 admin_group。
 */
struct flex_algo *flex_algo_alloc(struct flex_algos *flex_algos, uint8_t algorithm, void *arg);
/*
 * flex_algo_free - 从数据库移除并释放算法；调用 releaser 释放私有数据，
 * 并终止三组 admin_group。
 */
void flex_algo_free(struct flex_algos *flex_algos, struct flex_algo *fa);
/*
 * flex_algo_lookup - 按算法号在数据库中查找 flex_algo，未找到返回 NULL。
 */
struct flex_algo *flex_algo_lookup(struct flex_algos *flex_algos, uint8_t algorithm);
/*
 * flex_algo_definition_cmp - 比较两份 Flex-Algo 定义（FAD）是否一致。
 * 逐字段比较 algorithm / calc_type / metric_type / flags / exclude_srlg /
 * unsupported_subtlv 以及三组亲和属性，全部相等返回 true。
 */
bool flex_algo_definition_cmp(struct flex_algo *fa1, struct flex_algo *fa2);
/*
 * flex_algo_id_valid - 判断 SR 算法号是否落在 Flex-Algo 合法区间
 * （RFC9350 第 4 节，即 SR_ALGORITHM_FLEX_MIN 到 SR_ALGORITHM_FLEX_MAX）。
 */
bool flex_algo_id_valid(uint16_t algorithm);
/*
 * flex_algo_metric_type_print - 把度量类型打印成字符串（igp / delay / te），
 * 写入调用方提供的缓冲区，并返回该缓冲区。
 */
char *flex_algo_metric_type_print(char *type_str, size_t sz,
				  enum flex_algo_metric_type metric_type);

/*
 * flex_algo_get_state - 返回指定算法是否本地启用；算法不存在返回 false。
 */
bool flex_algo_get_state(struct flex_algos *flex_algos, uint8_t algorithm);

/*
 * flex_algo_set_state - 设置指定算法的本地启用状态；算法不存在则忽略。
 */
void flex_algo_set_state(struct flex_algos *flex_algos, uint8_t algorithm, bool state);
#endif /* _FRR_FLEX_ALGO_H */
