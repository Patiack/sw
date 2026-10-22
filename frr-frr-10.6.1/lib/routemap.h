// SPDX-License-Identifier: GPL-2.0-or-later
/* Route map function.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * 文件导读：routemap.h —— FRR 通用路由策略框架（route-map）的头文件。
 *
 * route-map 是 FRR 各协议（bgpd / zebra / ospfd / ripd 等）共用的“策略引擎”。
 * 它由若干有序的“子句 / 索引”（route_map_index，即用户看到的 seq N）组成，
 * 每个子句内部又包含若干 match 规则与 set 规则。协议在处理路由时调用
 * route_map_apply()，按 pref 从小到大遍历子句，逐个执行 match；全部 match
 * 命中后执行该子句的 set，再依据 exitpolicy（permit / deny 与 goto / call /
 * next 的组合）决定最终结果与跳转走向。
 *
 * 本头文件中最关键的几个抽象：
 *   - struct route_map_rule_cmd：一类规则的“插件接口”，把规则名、apply、
 *     compile、free 以及取键函数绑在一起；各协议通过 route_map_install_match()
 *     / route_map_install_set() 注册自己的规则类别。
 *   - struct route_map_rule：一条已编译的具体规则（例如 metric 20），
 *     value 指向编译后的目标值，rule_str 用于回显。
 *   - struct route_map_index：一个子句，含 match / set 链表、pref、permit 或
 *     deny，以及 goto 目标 nextpref、call 目标 nextrm。call 允许子句嵌套调用
 *     别的 route-map，形成“依赖（dependency）”，递归深度受 RMAP_RECURSION_LIMIT
 *     限制。
 *   - struct route_map：一张 route-map（名字 + 子句链表），并缓存由 prefix-list
 *     match 派生出的 IPv4 / IPv6 前缀表，用于加速匹配。
 *
 * 事件流程：route_map_add_* / route_map_delete_* 会通过 route_map_event_queue
 * 与 route_map_event_hook 通知订阅者（例如 bgpd 重算路由），enum route_map_event_t
 * 列出了全部事件类型。头文件后半段的 IS_MATCH_xxx / IS_SET_xxx 宏把北向（yang）
 * 中的 xpath 字符串与具体 match / set 关键字对应起来，供 northbound 层使用。
 *
 * 阅读建议：先看 struct route_map_rule_cmd 与 route_map_apply 的流程，再看
 * index 的 goto / call 语义，最后看事件通知与 northbound 宏。
 */

#ifndef _ZEBRA_ROUTEMAP_H
#define _ZEBRA_ROUTEMAP_H

#include "typesafe.h"
#include "prefix.h"
#include "memory.h"
#include "qobj.h"
#include "vty.h"
#include "lib/plist.h"
#include "lib/plist_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DECLARE_MTYPE：声明本模块专用的内存类型（memory type）。
 * ROUTE_MAP_NAME 用于 route-map 名字字符串，ROUTE_MAP_RULE 用于单条规则，
 * ROUTE_MAP_COMPILED 用于编译后的规则值。使用专用 MTYPE 便于内存统计与泄漏定位。
 */
DECLARE_MTYPE(ROUTE_MAP_NAME);
DECLARE_MTYPE(ROUTE_MAP_RULE);
DECLARE_MTYPE(ROUTE_MAP_COMPILED);

/*
 * 调试位：rmap_debug 是一个位掩码。DEBUG_ROUTEMAP 打开 route-map 一般性调试，
 * DEBUG_ROUTEMAP_DETAIL 打开更详细的逐规则调试。
 */
#define DEBUG_ROUTEMAP 0x01
#define DEBUG_ROUTEMAP_DETAIL 0x02
extern uint32_t rmap_debug;

/*
 * 子句的动作类型：RMAP_PERMIT 命中即放行，RMAP_DENY 命中即拒绝，
 * RMAP_ANY 为通配（兼容性用法）。
 */
/* Route map's type. */
enum route_map_type { RMAP_PERMIT, RMAP_DENY, RMAP_ANY };

/*
 * route_map_apply 的退出原因枚举，用于向调用者解释本次匹配为何结束。
 * none 无特殊原因；map_null 整个 route-map 为空；no_index 没有可用子句；
 * next_deny 遇到下一个 deny 子句；exit 显式退出（on-match 语义）；
 * goto_null goto 目标不存在；index_deny 命中了 deny 子句。
 */
/* Route-map's action reason */
enum route_map_action_reason {
	route_map_action_none,
	route_map_action_map_null,
	route_map_action_no_index,
	route_map_action_next_deny,
	route_map_action_exit,
	route_map_action_goto_null,
	route_map_action_index_deny,
};

/*
 * route_map_apply 对外的返回值：RMAP_DENYMATCH 表示最终拒绝，
 * RMAP_PERMITMATCH 表示最终放行。
 */
typedef enum {
	RMAP_DENYMATCH,
	RMAP_PERMITMATCH
} route_map_result_t;

/*
 * 单条 match / set 规则的执行结果。
 * match 规则返回 RMAP_MATCH / RMAP_NOMATCH / RMAP_NOOP 三者之一；
 * set 规则返回 RMAP_OKAY / RMAP_ERROR。
 * 特别注意 RMAP_NOOP 表示“本规则对当前这条路由不适用”，它既不算命中也不算
 * 未命中，调用方需要单独处理，不能简单当成 match 为真。
 */
/*
 * Route-map match or set result "Eg: match evpn vni xx"
 * route-map match cmd always returns match/nomatch/noop
 *    match--> found a match
 *    nomatch--> didnt find a match
 *    noop--> not applicable
 * route-map set retuns okay/error
 *    okay --> set was successful
 *    error --> set was not successful
 */
enum route_map_cmd_result_t {
	/*
	 * route-map match cmd results
	 */
	RMAP_MATCH,
	RMAP_NOMATCH,
	RMAP_NOOP,
	/*
	 * route-map set cmd results
	 */
	RMAP_OKAY,
	RMAP_ERROR
};

/*
 * 子句匹配成功后的走向策略（对应命令 on-match next / on-match goto）：
 * RMAP_EXIT 按 permit 或 deny 立即返回结果；RMAP_GOTO 跳到 nextpref 指定的
 * 子句继续；RMAP_NEXT 继续检查下一个比当前 pref 大的子句。
 */
typedef enum { RMAP_EXIT, RMAP_GOTO, RMAP_NEXT } route_map_end_t;

/*
 * route-map 变更事件类型，用于在配置变化时通知订阅者（如 bgpd 重算策略）。
 * SET_* / MATCH_* 表示 set 或 match 规则的增删改；INDEX_* 表示子句增删；
 * CALL_* 表示 call 依赖变化；PLIST / CLIST / ECLIST / LLIST / ASLIST / FILTER
 * 表示所引用的各类列表增删，供依赖跟踪使用。
 */
typedef enum {
	RMAP_EVENT_SET_ADDED,
	RMAP_EVENT_SET_DELETED,
	RMAP_EVENT_SET_REPLACED,
	RMAP_EVENT_MATCH_ADDED,
	RMAP_EVENT_MATCH_DELETED,
	RMAP_EVENT_MATCH_REPLACED,
	RMAP_EVENT_INDEX_ADDED,
	RMAP_EVENT_INDEX_DELETED,
	RMAP_EVENT_CALL_ADDED, /* call to another routemap added */
	RMAP_EVENT_CALL_DELETED,
	RMAP_EVENT_PLIST_ADDED,
	RMAP_EVENT_PLIST_DELETED,
	RMAP_EVENT_CLIST_ADDED,
	RMAP_EVENT_CLIST_DELETED,
	RMAP_EVENT_ECLIST_ADDED,
	RMAP_EVENT_ECLIST_DELETED,
	RMAP_EVENT_LLIST_ADDED,
	RMAP_EVENT_LLIST_DELETED,
	RMAP_EVENT_ASLIST_ADDED,
	RMAP_EVENT_ASLIST_DELETED,
	RMAP_EVENT_FILTER_ADDED,
	RMAP_EVENT_FILTER_DELETED,
} route_map_event_t;

/*
 * call 嵌套调用的最大递归深度，防止 route-map A 调用 B、B 又调用 A 造成死循环。
 */
/* Depth limit in RMAP recursion using RMAP_CALL. */
#define RMAP_RECURSION_LIMIT      10

/*
 * 规则类别（插件）描述结构：把一类 match 或 set 关键字的所有回调聚合在一起。
 * str 是关键字名（如 metric、as-path）；func_apply 执行本规则并返回枚举结果；
 * func_compile 把命令行参数编译成内部值并返回；func_free 释放 func_compile
 * 产生的值；func_get_rmap_rule_key 把编译值映射到去重键（如 prefix-list 名）。
 * 各协议通过 route_map_install_match / route_map_install_set 注册本结构。
 */
/* Route map rule structure for matching and setting. */
struct route_map_rule_cmd {
	/* Route map rule name (e.g. as-path, metric) */
	const char *str;

	/* Function for value set or match. */
	enum route_map_cmd_result_t (*func_apply)(void *rule,
						  const struct prefix *prefix,
						  void *object);

	/* Compile argument and return result as void *. */
	void *(*func_compile)(const char *);

	/* Free allocated value by func_compile (). */
	void (*func_free)(void *);

	/** To get the rule key after Compilation **/
	void *(*func_get_rmap_rule_key)(void *val);
};

/*
 * 规则编译结果：SUCCESS 成功，RULE_MISSING 未找到对应规则命令，
 * COMPILE_ERROR 参数编译失败。
 */
/* Route map apply error. */
enum rmap_compile_rets {
	RMAP_COMPILE_SUCCESS,

	/* Route map rule is missing. */
	RMAP_RULE_MISSING,

	/* Route map rule can't compile */
	RMAP_COMPILE_ERROR,

};

/*
 * 一条已编译的具体规则，既用于 match 也用于 set。
 * cmd 指向规则类别回调；rule_str 保存原始文本用于回显；value 是编译后的值，
 * 其具体类型由对应 cmd 解释；next / prev 构成规则链表。
 */
/* Route map rule. This rule has both `match' rule and `set' rule. */
struct route_map_rule {
	/* Rule type. */
	const struct route_map_rule_cmd *cmd;

	/* For pretty printing. */
	char *rule_str;

	/* Pre-compiled match rule. */
	void *value;

	/* Linked list. */
	struct route_map_rule *next;
	struct route_map_rule *prev;
};

/*
 * match 或 set 规则的双向链表头尾指针。
 */
/* Route map rule list. */
struct route_map_rule_list {
	struct route_map_rule *head;
	struct route_map_rule *tail;
};

/*
 * 前置声明：match / set 子句的钩子上下文结构，完整定义见本文件后面部分。
 */
/* Forward struct declaration: the complete can be found later this file. */
struct routemap_hook_context;

/*
 * route-map 的一个序号子句（用户看到的 seq N）。
 * map 指回所属 route-map；pref 是序号，越小越先匹配；type 表示该子句是
 * permit 还是 deny；exitpolicy 决定匹配成功后 exit / goto / next；
 * nextpref 是 goto 的目标序号，nextrm 是 call 的目标 route-map 名；
 * match_list / set_list 分别是该子句的匹配规则与动作规则链表；
 * rhclist 是 match / set 的钩子上下文队列；applied / cputime 统计该子句被应用
 * 的次数与耗时。注意：call 会引入对其他 route-map 的依赖，形成嵌套调用。
 */
/* Route map index structure. */
struct route_map_index {
	struct route_map *map;
	char *description;

	/* Preference of this route map rule. */
	int pref;

	/* Route map type permit or deny. */
	enum route_map_type type;

	/* Do we follow old rules, or hop forward? */
	route_map_end_t exitpolicy;

	/* If we're using "GOTO", to where do we go? */
	int nextpref;

	/* If we're using "CALL", to which route-map do ew go? */
	char *nextrm;

	/* Matching rule list. */
	struct route_map_rule_list match_list;
	struct route_map_rule_list set_list;

	/* Make linked list. */
	struct route_map_index *next;
	struct route_map_index *prev;

	/* Keep track how many times we've try to apply */
	uint64_t applied;
	uint64_t applied_clear;
	size_t cputime;

	/* List of match/sets contexts. */
	TAILQ_HEAD(, routemap_hook_context) rhclist;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(route_map_index);

/*
 * route-map 名字的最大长度，受 yang xpath 长度限制。
 */
/* route map maximum length. Not strictly the maximum xpath length but cannot be
 * greater
 */
#define RMAP_NAME_MAXLEN XPATH_MAXLEN

/*
 * 一张完整的 route-map：名字 + 子句（index）双向链表。
 * to_be_processed 表示有变更尚未生效；deleted 表示本节点将被删除；
 * optimization_disabled 表示禁用匹配优化；applied / cputime 统计整体被应用
 * 情况；use_count 统计活跃引用数；ipv4_prefix_table / ipv6_prefix_table 缓存
 * 由 prefix-list match 派生出的前缀，用于加速匹配。
 */
/* Route map list structure. */
struct route_map {
	/* Name of route map. */
	char *name;

	/* Route map's rule. */
	struct route_map_index *head;
	struct route_map_index *tail;

	/* Make linked list. */
	struct route_map *next;
	struct route_map *prev;

	/* Maintain update info */
	bool to_be_processed; /* True if modification isn't acted on yet */
	bool deleted;         /* If 1, then this node will be deleted */
	bool optimization_disabled;

	/* How many times have we applied this route-map */
	uint64_t applied;
	uint64_t applied_clear;
	size_t cputime;

	/* Counter to track active usage of this route-map */
	uint16_t use_count;

	/* Tables to maintain IPv4 and IPv6 prefixes from
	 * the prefix-list match clause.
	 */
	struct route_table *ipv4_prefix_table;
	struct route_table *ipv6_prefix_table;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(route_map);

/*
 * 以下一组宏把北向（yang 模型）中的 xpath 字符串映射到具体的 match 关键字，
 * 供 northbound 层在解析配置时区分不同类型的 match 条件。
 * 每个宏本质上是一次 strmatch 字符串相等判断，参数 C 为待比较的 xpath。
 */
/* Route-map match conditions */
#define IS_MATCH_INTERFACE(C)                                                  \
	(strmatch(C, "frr-route-map:interface"))
#define IS_MATCH_IPv4_ADDRESS_LIST(C)                                          \
	(strmatch(C, "frr-route-map:ipv4-address-list"))
#define IS_MATCH_IPv6_ADDRESS_LIST(C)                                          \
	(strmatch(C, "frr-route-map:ipv6-address-list"))
#define IS_MATCH_IPv4_NEXTHOP_LIST(C)                                          \
	(strmatch(C, "frr-route-map:ipv4-next-hop-list"))
#define IS_MATCH_IPv6_NEXTHOP_LIST(C)                                          \
	(strmatch(C, "frr-route-map:ipv6-next-hop-list"))
#define IS_MATCH_IPv4_PREFIX_LIST(C)                                           \
	(strmatch(C, "frr-route-map:ipv4-prefix-list"))
#define IS_MATCH_IPv6_PREFIX_LIST(C)                                           \
	(strmatch(C, "frr-route-map:ipv6-prefix-list"))
#define IS_MATCH_IPv4_NEXTHOP_PREFIX_LIST(C)                                   \
	(strmatch(C, "frr-route-map:ipv4-next-hop-prefix-list"))
#define IS_MATCH_IPv6_NEXTHOP_PREFIX_LIST(C)                                   \
	(strmatch(C, "frr-route-map:ipv6-next-hop-prefix-list"))
#define IS_MATCH_IPv4_NEXTHOP_TYPE(C)                                          \
	(strmatch(C, "frr-route-map:ipv4-next-hop-type"))
#define IS_MATCH_IPv6_NEXTHOP_TYPE(C)                                          \
	(strmatch(C, "frr-route-map:ipv6-next-hop-type"))
#define IS_MATCH_METRIC(C)                                                     \
	(strmatch(C, "frr-route-map:match-metric"))
#define IS_MATCH_TAG(C) (strmatch(C, "frr-route-map:match-tag"))
/* Zebra route-map match conditions */
#define IS_MATCH_IPv4_PREFIX_LEN(C)                                            \
	(strmatch(C, "frr-zebra-route-map:ipv4-prefix-length"))
#define IS_MATCH_IPv6_PREFIX_LEN(C)                                            \
	(strmatch(C, "frr-zebra-route-map:ipv6-prefix-length"))
#define IS_MATCH_IPv4_NH_PREFIX_LEN(C)                                         \
	(strmatch(C, "frr-zebra-route-map:ipv4-next-hop-prefix-length"))
#define IS_MATCH_SRC_PROTO(C)                                                  \
	(strmatch(C, "frr-zebra-route-map:source-protocol"))
#define IS_MATCH_BGP_SRC_PROTO(C)                                              \
	(strmatch(C, "frr-bgp-route-map:source-protocol"))
#define IS_MATCH_SRC_INSTANCE(C)                                               \
	(strmatch(C, "frr-zebra-route-map:source-instance"))
/* BGP route-map match conditions */
#define IS_MATCH_VPN_DATAPLANE(C) (strmatch(C, "frr-bgp-route-map:match-vpn-dataplane"))
#define IS_MATCH_LOCAL_PREF(C)                                                 \
	(strmatch(C, "frr-bgp-route-map:match-local-preference"))
#define IS_MATCH_ALIAS(C) (strmatch(C, "frr-bgp-route-map:match-alias"))
#define IS_MATCH_SCRIPT(C) (strmatch(C, "frr-bgp-route-map:match-script"))
#define IS_MATCH_ORIGIN(C)                                                     \
	(strmatch(C, "frr-bgp-route-map:match-origin"))
#define IS_MATCH_RPKI(C) (strmatch(C, "frr-bgp-route-map:rpki"))
#define IS_MATCH_RPKI_EXTCOMMUNITY(C)                                          \
	(strmatch(C, "frr-bgp-route-map:rpki-extcommunity"))
#define IS_MATCH_PROBABILITY(C)                                                \
	(strmatch(C, "frr-bgp-route-map:probability"))
#define IS_MATCH_SRC_VRF(C)                                                    \
	(strmatch(C, "frr-bgp-route-map:source-vrf"))
#define IS_MATCH_PEER(C) (strmatch(C, "frr-bgp-route-map:peer"))
#define IS_MATCH_SRC_PEER(C) (strmatch(C, "frr-bgp-route-map:src-peer"))
#define IS_MATCH_AS_LIST(C)                                                    \
	(strmatch(C, "frr-bgp-route-map:as-path-list"))
#define IS_MATCH_ASPATH_COUNT(C) (strmatch(C, "frr-bgp-route-map:match-as-path-count"))
#define IS_MATCH_MAC_LIST(C)                                                   \
	(strmatch(C, "frr-bgp-route-map:mac-address-list"))
#define IS_MATCH_EVPN_ROUTE_TYPE(C)                                            \
	(strmatch(C, "frr-bgp-route-map:evpn-route-type"))
#define IS_MATCH_EVPN_DEFAULT_ROUTE(C)                                         \
	(strmatch(C, "frr-bgp-route-map:evpn-default-route"))
#define IS_MATCH_EVPN_VNI(C)                                                   \
	(strmatch(C, "frr-bgp-route-map:evpn-vni"))
#define IS_MATCH_EVPN_RD(C)                                                    \
	(strmatch(C, "frr-bgp-route-map:evpn-rd"))
#define IS_MATCH_ROUTE_SRC(C)                                                  \
	(strmatch(C, "frr-bgp-route-map:ip-route-source"))
#define IS_MATCH_ROUTE_SRC_PL(C)                                               \
	(strmatch(C, "frr-bgp-route-map:ip-route-source-prefix-list"))
#define IS_MATCH_COMMUNITY_LIMIT(C) (strmatch(C, "frr-bgp-route-map:match-community-limit"))
#define IS_MATCH_COMMUNITY(C)                                                  \
	(strmatch(C, "frr-bgp-route-map:match-community"))
#define IS_MATCH_LCOMMUNITY(C)                                                 \
	(strmatch(C, "frr-bgp-route-map:match-large-community"))
#define IS_MATCH_EXTCOMMUNITY(C)                                               \
	(strmatch(C, "frr-bgp-route-map:match-extcommunity"))
#define IS_MATCH_EXTCOMMUNITY_LIMIT(C) (strmatch(C, "frr-bgp-route-map:match-extcommunity-limit"))
#define IS_MATCH_IPV4_NH(C)                                                    \
	(strmatch(C, "frr-bgp-route-map:ipv4-nexthop"))
#define IS_MATCH_IPV6_NH(C)                                                    \
	(strmatch(C, "frr-bgp-route-map:ipv6-nexthop"))
#define IS_MATCH_IPV4_MULTICAST_SOURCE(C) \
	(strmatch(C, "frr-pim-route-map:ipv4-multicast-source"))
#define IS_MATCH_IPV6_MULTICAST_SOURCE(C) \
	(strmatch(C, "frr-pim-route-map:ipv6-multicast-source"))
#define IS_MATCH_IPV4_MULTICAST_SOURCE_PREFIX_LIST(C) \
	(strmatch(C, "frr-pim-route-map:ipv4-multicast-source-prefix-list"))
#define IS_MATCH_IPV6_MULTICAST_SOURCE_PREFIX_LIST(C) \
	(strmatch(C, "frr-pim-route-map:ipv6-multicast-source-prefix-list"))
#define IS_MATCH_IPV4_MULTICAST_GROUP(C) \
	(strmatch(C, "frr-pim-route-map:ipv4-multicast-group"))
#define IS_MATCH_IPV6_MULTICAST_GROUP(C) \
	(strmatch(C, "frr-pim-route-map:ipv6-multicast-group"))
#define IS_MATCH_IPV4_MULTICAST_GROUP_PREFIX_LIST(C) \
	(strmatch(C, "frr-pim-route-map:ipv4-multicast-group-prefix-list"))
#define IS_MATCH_IPV6_MULTICAST_GROUP_PREFIX_LIST(C) \
	(strmatch(C, "frr-pim-route-map:ipv6-multicast-group-prefix-list"))
#define IS_MATCH_MULTICAST_INTERFACE(C) \
	(strmatch(C, "frr-pim-route-map:multicast-interface"))

/*
 * 以下一组宏把北向（yang 模型）中的 xpath 字符串映射到具体的 set 动作关键字，
 * 用法与上面的 IS_MATCH_xxx 相同：参数 A 为待比较的 xpath，返回是否相等。
 * 覆盖 IPv4 / IPv6 下一跳、metric、tag、weight、local-pref、community、
 * extcommunity 等各类 set 动作，并按所属模块（route-map / zebra / ospf / bgp）
 * 分段列出。
 */
/* Route-map set actions */
#define IS_SET_IPv4_NH(A)                                                      \
	(strmatch(A, "frr-route-map:ipv4-next-hop"))
#define IS_SET_IPv6_NH(A)                                                      \
	(strmatch(A, "frr-route-map:ipv6-next-hop"))
#define IS_SET_METRIC(A)                                                       \
	(strmatch(A, "frr-route-map:set-metric"))
#define IS_SET_MIN_METRIC(A) (strmatch(A, "frr-route-map:set-min-metric"))
#define IS_SET_MAX_METRIC(A) (strmatch(A, "frr-route-map:set-max-metric"))
#define IS_SET_TAG(A) (strmatch(A, "frr-route-map:set-tag"))
#define IS_SET_SR_TE_COLOR(A)                                                  \
	(strmatch(A, "frr-route-map:set-sr-te-color"))
/* Zebra route-map set actions */
#define IS_SET_SRC(A)                                                          \
	(strmatch(A, "frr-zebra-route-map:src-address"))
/* OSPF route-map set actions */
#define IS_SET_METRIC_TYPE(A)                                                  \
	(strmatch(A, "frr-ospf-route-map:metric-type"))
#define IS_SET_FORWARDING_ADDR(A)                                              \
	(strmatch(A, "frr-ospf6-route-map:forwarding-address"))
/* BGP route-map_set actions */
#define IS_SET_WEIGHT(A)                                                       \
	(strmatch(A, "frr-bgp-route-map:weight"))
#define IS_SET_TABLE(A) (strmatch(A, "frr-bgp-route-map:table"))
#define IS_SET_LOCAL_PREF(A)                                                   \
	(strmatch(A, "frr-bgp-route-map:set-local-preference"))
#define IS_SET_LABEL_INDEX(A)                                                  \
	(strmatch(A, "frr-bgp-route-map:label-index"))
#define IS_SET_DISTANCE(A)                                                     \
	(strmatch(A, "frr-bgp-route-map:distance"))
#define IS_SET_ORIGIN(A)                                                       \
	(strmatch(A, "frr-bgp-route-map:set-origin"))
#define IS_SET_ATOMIC_AGGREGATE(A)                                             \
	(strmatch(A, "frr-bgp-route-map:atomic-aggregate"))
#define IS_SET_AIGP_METRIC(A) (strmatch(A, "frr-bgp-route-map:aigp-metric"))
#define IS_SET_ORIGINATOR_ID(A)                                                \
	(strmatch(A, "frr-bgp-route-map:originator-id"))
#define IS_SET_COMM_LIST_DEL(A)                                                \
	(strmatch(A, "frr-bgp-route-map:comm-list-delete"))
#define IS_SET_LCOMM_LIST_DEL(A)                                               \
	(strmatch(A, "frr-bgp-route-map:large-comm-list-delete"))
#define IS_SET_EXTCOMM_LIST_DEL(A)                                                \
	(strmatch(A, "frr-bgp-route-map:extended-comm-list-delete"))
#define IS_SET_LCOMMUNITY(A)                                                   \
	(strmatch(A, "frr-bgp-route-map:set-large-community"))
#define IS_SET_COMMUNITY(A)                                                    \
	(strmatch(A, "frr-bgp-route-map:set-community"))
#define IS_SET_EXTCOMMUNITY_NONE(A)                                            \
	(strmatch(A, "frr-bgp-route-map:set-extcommunity-none"))
#define IS_SET_EXTCOMMUNITY_RT(A)                                              \
	(strmatch(A, "frr-bgp-route-map:set-extcommunity-rt"))
#define IS_SET_EXTCOMMUNITY_NT(A)                                              \
	(strmatch(A, "frr-bgp-route-map:set-extcommunity-nt"))
#define IS_SET_EXTCOMMUNITY_SOO(A)                                             \
	(strmatch(A, "frr-bgp-route-map:set-extcommunity-soo"))
#define IS_SET_EXTCOMMUNITY_LB(A)                                              \
	(strmatch(A, "frr-bgp-route-map:set-extcommunity-lb"))
#define IS_SET_EXTCOMMUNITY_COLOR(A)                                           \
	(strmatch(A, "frr-bgp-route-map:set-extcommunity-color"))

#define IS_SET_AGGREGATOR(A)                                                   \
	(strmatch(A, "frr-bgp-route-map:aggregator"))
#define IS_SET_AS_PREPEND(A)                                                   \
	(strmatch(A, "frr-bgp-route-map:as-path-prepend"))
#define IS_SET_AS_EXCLUDE(A)                                                   \
	(strmatch(A, "frr-bgp-route-map:as-path-exclude"))
#define IS_SET_AS_REPLACE(A) (strmatch(A, "frr-bgp-route-map:as-path-replace"))
#define IS_SET_IPV6_NH_GLOBAL(A)                                               \
	(strmatch(A, "frr-bgp-route-map:ipv6-nexthop-global"))
#define IS_SET_IPV6_VPN_NH(A)                                                  \
	(strmatch(A, "frr-bgp-route-map:ipv6-vpn-address"))
#define IS_SET_IPV6_PEER_ADDR(A)                                               \
	(strmatch(A, "frr-bgp-route-map:ipv6-peer-address"))
#define IS_SET_IPV6_PREFER_GLOBAL(A)                                           \
	(strmatch(A, "frr-bgp-route-map:ipv6-prefer-global"))
#define IS_SET_IPV4_VPN_NH(A)                                                  \
	(strmatch(A, "frr-bgp-route-map:ipv4-vpn-address"))
#define IS_SET_BGP_IPV4_NH(A)                                                  \
	(strmatch(A, "frr-bgp-route-map:set-ipv4-nexthop"))
#define IS_SET_BGP_EVPN_GATEWAY_IP_IPV4(A)                                     \
	(strmatch(A, "frr-bgp-route-map:set-evpn-gateway-ip-ipv4"))
#define IS_SET_BGP_EVPN_GATEWAY_IP_IPV6(A)                                     \
	(strmatch(A, "frr-bgp-route-map:set-evpn-gateway-ip-ipv6"))
#define IS_SET_BGP_L3VPN_NEXTHOP_ENCAPSULATION(A)                              \
	(strmatch(A, "frr-bgp-route-map:set-l3vpn-nexthop-encapsulation"))

/*
 * BGP link-bandwidth（扩展团体）的带宽取值方式：
 * EXPLICIT_BANDWIDTH 使用显式给定值；CUMULATIVE_BANDWIDTH 累加各条路径带宽；
 * COMPUTED_BANDWIDTH 由系统计算得出。
 */
enum ecommunity_lb_type {
	EXPLICIT_BANDWIDTH,
	CUMULATIVE_BANDWIDTH,
	COMPUTED_BANDWIDTH
};

/*
 * route_map_init：初始化 route-map 子系统，注册 match / set 命令、安装事件
 * 钩子并注册 vty 命令（前台 daemon 用）。
 * route_map_init_new：可指定 in_backend，供只有后端（后台）实例的场景使用。
 * 二者通常在协议模块初始化时各调用一次。
 */
/* Prototypes. */
extern void route_map_init(void);
extern void route_map_init_new(bool in_backend);

/*
 * route_map_finish：仅在进程退出时调用，释放全部 route-map 资源。
 * 注意它会在做任何处理前先把各种钩子置为 NULL，避免退出过程中再触发回调。
 */
/*
 * This should only be called on shutdown
 * Additionally this function sets the hooks to NULL
 * before any processing is done.
 */
extern void route_map_finish(void);

/*
 * route_map_add_match：向指定子句添加一条 match 规则。
 * index 为目标子句；match_name 为规则名（如 metric）；match_arg 为参数文本；
 * type 为事件类型（用于变更通知）。规则会被编译后挂入 match_list。
 * 返回编译结果枚举；若同名规则已存在则按替换处理。
 */
/* Add match statement to route map. */
extern enum rmap_compile_rets route_map_add_match(struct route_map_index *index,
						  const char *match_name,
						  const char *match_arg,
						  route_map_event_t type);

/*
 * route_map_delete_match：从子句删除指定 match 规则。
 * 返回编译 / 查找结果，未找到时返回 RMAP_RULE_MISSING。
 */
/* Delete specified route match rule. */
extern enum rmap_compile_rets
route_map_delete_match(struct route_map_index *index,
		       const char *match_name, const char *match_arg,
		       route_map_event_t type);

/*
 * route_map_get_match_arg：取回子句中某条 match 规则的参数文本（rule_str），
 * 供回显或后续处理使用；规则不存在时返回 NULL。
 */
extern const char *route_map_get_match_arg(struct route_map_index *index,
					   const char *match_name);

/*
 * route_map_add_set：向指定子句添加一条 set 规则，参数为规则名与参数文本。
 * 编译后挂入 set_list，返回编译结果。
 */
/* Add route-map set statement to the route map. */
extern enum rmap_compile_rets route_map_add_set(struct route_map_index *index,
						const char *set_name,
						const char *set_arg);

/*
 * route_map_delete_set：从子句删除指定 set 规则，返回编译 / 查找结果。
 */
/* Delete route map set rule. */
extern enum rmap_compile_rets
route_map_delete_set(struct route_map_index *index,
		     const char *set_name, const char *set_arg);

/*
 * route_map_rule_cmd 被设计为 const，以免暴露可写的函数指针（安全性考虑）。
 * 因此用下面的 proxy 结构体做按名字查找的哈希项，代理指向真正的 cmd。
 */
/* struct route_map_rule_cmd is kept const in order to not have writable
 * function pointers (which is a security benefit.)  Hence, below struct is
 * used as proxy for hashing these for by-name lookup.
 */

PREDECL_HASH(rmap_cmd_name);

/*
 * 规则命令代理：itm 是哈希表项（按 cmd->str 名字哈希），cmd 指向真正的规则类别。
 */
struct route_map_rule_cmd_proxy {
	struct rmap_cmd_name_item itm;
	const struct route_map_rule_cmd *cmd;
};

/*
 * 为每个调用点自动生成一个 static 的 proxy 结构体，省去重复样板代码。
 * route_map_install_match / route_map_install_set 是便捷宏，
 * 分别把 match / set 规则类别注册进全局规则表。
 */
/* ... and just automatically create a proxy struct for each call location
 * to route_map_install_{match,set} to avoid unnecessarily added boilerplate
 * for each route-map user
 */

#define route_map_install_match(c)                                             \
	do {                                                                   \
		static struct route_map_rule_cmd_proxy proxy = {.cmd = c};     \
		_route_map_install_match(&proxy);                              \
	} while (0)

/*
 * route_map_install_set：把 set 规则类别注册进全局设置命令表，用法同上。
 */
#define route_map_install_set(c)                                               \
	do {                                                                   \
		static struct route_map_rule_cmd_proxy proxy = {.cmd = c};     \
		_route_map_install_set(&proxy);                                \
	} while (0)

/*
 * _route_map_install_match：真正把 match 规则类别加入全局匹配命令哈希表，
 * 一般只通过 route_map_install_match 宏调用。
 */
/* Install rule command to the match list. */
extern void _route_map_install_match(struct route_map_rule_cmd_proxy *proxy);

/*
 * _route_map_install_set：真正把 set 规则类别加入全局设置命令哈希表。
 * 安装某条命令时允许两种行为：解析失败返回 NULL，或本 daemon 不支持该形式时
 * 返回非空指针并在 apply 阶段自行处理（参见 ripd / ripngd / ospfd 的
 * 'set metric'）。
 */
/*
 * Install rule command to the set list.
 *
 * When installing a particular item, Allow a difference of handling
 * of bad cli inputted(return NULL) -vs- this particular daemon cannot use
 * this form of the command(return a pointer and handle it appropriately
 * in the apply command).  See 'set metric' command
 * as it is handled in ripd/ripngd and ospfd.
 */
extern void _route_map_install_set(struct route_map_rule_cmd_proxy *proxy);

/*
 * route_map_lookup_by_name：按名字查找 route-map，找不到返回 NULL。
 */
/* Lookup route map by name. */
extern struct route_map *route_map_lookup_by_name(const char *name);

/*
 * route_map_lookup_warn_noexist：按名字查找，若不存在则向 vty 打印告警，
 * 便于命令解析时给出友好提示；找到返回对象，否则返回 NULL。
 */
/* Simple helper to warn if route-map does not exist. */
struct route_map *route_map_lookup_warn_noexist(struct vty *vty, const char *name);

/*
 * route_map_apply_ext：把 route-map 应用到对象的核心入口。
 * prefix 用于需要前缀匹配的规则；match_object 参与匹配；set_object 承载 set
 * 结果（匹配与修改可能是不同对象）；pref 为出参，返回最终命中的子句序号
 * （可为 NULL）。返回 RMAP_PERMITMATCH 或 RMAP_DENYMATCH。
 * 便捷宏 route_map_apply 令 set_object 与 match_object 相同、pref 为 NULL。
 */
/* Apply route map to the object. */
extern route_map_result_t route_map_apply_ext(struct route_map *map,
					      const struct prefix *prefix,
					      void *match_object,
					      void *set_object, int *pref);
#define route_map_apply(map, prefix, object)                                   \
	route_map_apply_ext(map, prefix, object, object, NULL)

/*
 * route_map_add_hook / route_map_delete_hook：注册 route-map 增删时的回调，
 * 供各协议在 route-map 被创建或删除时做联动处理。
 */
extern void route_map_add_hook(void (*func)(const char *));
extern void route_map_delete_hook(void (*func)(const char *));

/*
 * route_map_event_hook：注册 route-map 变更事件回调，name 为发生变化的
 * route-map 名字。与 route_map_add_hook / delete_hook 不同，本回调配合事件
 * 队列使用，会在变更被聚合处理后统一分发，避免每改动一处就重算一次。
 */
/*
 * This is the callback for when something has changed about a
 * route-map.  The interested parties can register to receive
 * this data.
 *
 * name - Is the name of the changed route-map
 */
extern void route_map_event_hook(void (*func)(const char *name));
/*
 * route_map_mark_updated：把某 route-map 标记为“已更新但尚未处理”，
 * 后续由 route_map_walk_update_list 统一遍历处理，以减少重复计算。
 * route_map_walk_update_list：遍历所有待处理的 route-map 并对每个调用
 * update_fn，处理完清除待处理标记。
 */
extern int route_map_mark_updated(const char *name);
extern void route_map_walk_update_list(void (*update_fn)(char *name));
/*
 * route_map_upd8_dependency：登记 / 更新依赖关系——某 route-map 依赖了某个
 * 外部对象（由 type 与 arg 描述），当该对象变化时需要联动重算。
 * route_map_notify_dependencies：当 affected_name 对应对象发生 event 时，
 * 通知所有依赖它的 route-map。
 * route_map_notify_pentry_dependencies：与上者类似，但针对 prefix-list 的单条
 * entry（pentry），粒度更细。这也是 call 依赖之外的另一类“依赖（dependency）”。
 */
extern void route_map_upd8_dependency(route_map_event_t type, const char *arg,
				      const char *rmap_name);
extern void route_map_notify_dependencies(const char *affected_name,
					  route_map_event_t event);
extern void
route_map_notify_pentry_dependencies(const char *affected_name,
				     struct prefix_list_entry *pentry,
				     route_map_event_t event);
/*
 * generic_match_add / generic_match_delete：通用的 match 规则增删实现，
 * 通过命令名在全局 match 命令表中查找对应规则类别并执行编译 / 删除，
 * 供各协议的 hook 回调复用。返回 CMD_SUCCESS 或 CMD_WARNING，
 * 失败时通过 errmsg 回填错误文本。
 * generic_set_add / generic_set_delete：对应的通用 set 规则增删实现，语义相同。
 */
extern int generic_match_add(struct route_map_index *index,
			     const char *command, const char *arg,
			     route_map_event_t type,
			     char *errmsg, size_t errmsg_len);
extern int generic_match_delete(struct route_map_index *index,
				const char *command, const char *arg,
				route_map_event_t type,
				char *errmsg, size_t errmsg_len);

extern int generic_set_add(struct route_map_index *index,
			   const char *command, const char *arg,
			   char *errmsg, size_t errmsg_len);
extern int generic_set_delete(struct route_map_index *index,
			      const char *command, const char *arg,
			      char *errmsg, size_t errmsg_len);


/*
 * 以下是各组 match / set 命令的“钩子注册函数”。
 * route-map 内核本身并不认识 ip address、metric 等具体关键字，这些关键字的
 * 解析与执行由各协议（zebra / bgpd / ospfd 等）通过 northbound 层实现；本文件
 * 提供这些 hook 注册入口，让协议把自己的处理函数 func 绑定到对应关键字上。
 * func 的统一签名为：接收目标子句 index、命令名 command、参数 arg、事件类型
 * type，以及用于回填错误信息的 errmsg 缓冲区；返回 CMD_SUCCESS 或 CMD_WARNING。
 * 命名规律：route_map_match_xxx_hook 注册“match xxx”，
 * route_map_no_match_xxx_hook 注册“no match xxx”，set 系列同理。
 * 下面每个函数都只是把 func 存进全局 hook 结构体的对应字段。
 */
/*
 * route_map_match_interface_hook：注册“match interface”命令的处理函数 func，
 * 该条件用于按路由的入接口进行匹配。
 */
/* match interface */
extern void route_map_match_interface_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_interface_hook：注册“no match interface”命令的处理函数，
 * 用于删除按入接口匹配的条件。
 */
/* no match interface */
extern void route_map_no_match_interface_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ip_address_hook：注册“match ip address”命令的处理函数，
 * 用于按 IPv4 目的地址 / 地址列表匹配路由。
 */
/* match ip address */
extern void route_map_match_ip_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ip_address_hook：注册“no match ip address”命令的处理函数，
 * 用于删除对应的 IPv4 地址匹配条件。
 */
/* no match ip address */
extern void route_map_no_match_ip_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ip_address_prefix_list_hook：注册“match ip address
 * prefix-list”命令的处理函数，按 IPv4 前缀列表匹配。
 */
/* match ip address prefix list */
extern void route_map_match_ip_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ip_address_prefix_list_hook：注册“no match ip address
 * prefix-list”命令的处理函数。
 */
/* no match ip address prefix list */
extern void route_map_no_match_ip_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ip_next_hop_hook：注册“match ip next-hop”命令的处理函数，
 * 按 IPv4 下一跳地址匹配。
 */
/* match ip next hop */
extern void route_map_match_ip_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ip_next_hop_hook：注册“no match ip next-hop”命令的处理函数。
 */
/* no match ip next hop */
extern void route_map_no_match_ip_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ipv6_next_hop_hook：注册“match ipv6 next-hop”命令的处理函数，
 * 按 IPv6 下一跳地址匹配。
 */
/* match ipv6 next hop */
extern void route_map_match_ipv6_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ipv6_next_hop_hook：注册“no match ipv6 next-hop”命令的处理函数。
 */
/* no match ipv6 next hop */
extern void route_map_no_match_ipv6_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ip_next_hop_prefix_list_hook：注册“match ip next-hop
 * prefix-list”命令的处理函数。
 */
/* match ip next hop prefix list */
extern void route_map_match_ip_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ip_next_hop_prefix_list_hook：注册“no match ip next-hop
 * prefix-list”命令的处理函数。
 */
/* no match ip next hop prefix list */
extern void route_map_no_match_ip_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ip_next_hop_type_hook：注册“match ip next-hop type”命令的
 * 处理函数，按下一跳类型（如 blackhole / reject）匹配。
 */
/* match ip next hop type */
extern void route_map_match_ip_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ip_next_hop_type_hook：注册“no match ip next-hop type”
 * 命令的处理函数。
 */
/* no match ip next hop type */
extern void route_map_no_match_ip_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ipv6_address_hook：注册“match ipv6 address”命令的处理函数，
 * 按 IPv6 目的地址 / 地址列表匹配。
 */
/* match ipv6 address */
extern void route_map_match_ipv6_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ipv6_address_hook：注册“no match ipv6 address”命令的处理函数。
 */
/* no match ipv6 address */
extern void route_map_no_match_ipv6_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ipv6_address_prefix_list_hook：注册“match ipv6 address
 * prefix-list”命令的处理函数。
 */
/* match ipv6 address prefix list */
extern void route_map_match_ipv6_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ipv6_address_prefix_list_hook：注册“no match ipv6 address
 * prefix-list”命令的处理函数。
 */
/* no match ipv6 address prefix list */
extern void route_map_no_match_ipv6_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ipv6_next_hop_type_hook：注册“match ipv6 next-hop type”
 * 命令的处理函数。
 */
/* match ipv6 next-hop type */
extern void route_map_match_ipv6_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ipv6_next_hop_type_hook：注册“no match ipv6 next-hop type”
 * 命令的处理函数。
 */
/* no match ipv6 next-hop type */
extern void route_map_no_match_ipv6_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_ipv6_next_hop_prefix_list_hook：注册“match ipv6 next-hop
 * prefix-list”命令的处理函数。
 */
/* match ipv6 next-hop prefix-list */
extern void route_map_match_ipv6_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_ipv6_next_hop_prefix_list_hook：注册“no match ipv6 next-hop
 * prefix-list”命令的处理函数。
 */
/* no match ipv6 next-hop prefix-list */
extern void route_map_no_match_ipv6_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len));
/*
 * route_map_match_metric_hook：注册“match metric”命令的处理函数，按度量值匹配。
 */
/* match metric */
extern void route_map_match_metric_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_metric_hook：注册“no match metric”命令的处理函数。
 */
/* no match metric */
extern void route_map_no_match_metric_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_match_tag_hook：注册“match tag”命令的处理函数，按路由 tag 匹配。
 */
/* match tag */
extern void route_map_match_tag_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_no_match_tag_hook：注册“no match tag”命令的处理函数。
 */
/* no match tag */
extern void route_map_no_match_tag_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len));
/*
 * route_map_set_srte_color_hook：注册“set sr-te color”命令的处理函数，
 * 用于为路由设置 SR-TE 颜色（引流到段路由策略）。
 */
/* set sr-te color */
extern void route_map_set_srte_color_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_no_set_srte_color_hook：注册“no set sr-te color”命令的处理函数。
 */
/* no set sr-te color */
extern void route_map_no_set_srte_color_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_set_ip_nexthop_hook：注册“set ip next-hop”命令的处理函数，
 * 用于修改 IPv4 下一跳。
 */
/* set ip nexthop */
extern void route_map_set_ip_nexthop_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_no_set_ip_nexthop_hook：注册“no set ip next-hop”命令的处理函数。
 */
/* no set ip nexthop */
extern void route_map_no_set_ip_nexthop_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_set_ipv6_nexthop_local_hook：注册“set ipv6 next-hop local”命令的
 * 处理函数，用于修改 IPv6 链路本地下一跳。
 */
/* set ipv6 nexthop local */
extern void route_map_set_ipv6_nexthop_local_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_no_set_ipv6_nexthop_local_hook：注册“no set ipv6 next-hop local”
 * 命令的处理函数。
 */
/* no set ipv6 nexthop local */
extern void route_map_no_set_ipv6_nexthop_local_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_set_metric_hook：注册“set metric”命令的处理函数，修改路由度量值。
 */
/* set metric */
extern void route_map_set_metric_hook(int (*func)(struct route_map_index *index,
						  const char *command,
						  const char *arg,
						  char *errmsg,
						  size_t errmsg_len));
/*
 * route_map_no_set_metric_hook：注册“no set metric”命令的处理函数。
 */
/* no set metric */
extern void route_map_no_set_metric_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len));
/*
 * route_map_set_max_metric_hook：注册“set max-metric”命令的处理函数。
 * 注意其上方英文注释写作“set metric”，实际对应 max-metric。
 */
/* set metric */
extern void route_map_set_max_metric_hook(
	int (*func)(struct route_map_index *index, const char *command,
		    const char *arg, char *errmsg, size_t errmsg_len));
/*
 * route_map_no_set_max_metric_hook：注册“no set max-metric”命令的处理函数。
 */
/* no set metric */
extern void route_map_no_set_max_metric_hook(
	int (*func)(struct route_map_index *index, const char *command,
		    const char *arg, char *errmsg, size_t errmsg_len));
/*
 * route_map_set_min_metric_hook：注册“set min-metric”命令的处理函数。
 * 注意其上方英文注释写作“set metric”，实际对应 min-metric。
 */
/* set metric */
extern void route_map_set_min_metric_hook(
	int (*func)(struct route_map_index *index, const char *command,
		    const char *arg, char *errmsg, size_t errmsg_len));
/*
 * route_map_no_set_min_metric_hook：注册“no set min-metric”命令的处理函数。
 */
/* no set metric */
extern void route_map_no_set_min_metric_hook(
	int (*func)(struct route_map_index *index, const char *command,
		    const char *arg, char *errmsg, size_t errmsg_len));
/*
 * route_map_set_tag_hook：注册“set tag”命令的处理函数，为路由设置 tag。
 */
/* set tag */
extern void route_map_set_tag_hook(int (*func)(struct route_map_index *index,
					       const char *command,
					       const char *arg,
					       char *errmsg,
					       size_t errmsg_len));
/*
 * route_map_no_set_tag_hook：注册“no set tag”命令的处理函数。
 */
/* no set tag */
extern void route_map_no_set_tag_hook(int (*func)(struct route_map_index *index,
						  const char *command,
						  const char *arg,
						  char *errmsg,
						  size_t errmsg_len));

/*
 * route_map_rule_tag_compile：把 tag 参数文本编译为可供匹配 / 设置使用的内部值。
 * route_map_rule_tag_free：释放上述编译值。二者作为 tag 规则类别的
 * func_compile / func_free 回调。
 */
extern void *route_map_rule_tag_compile(const char *arg);
extern void route_map_rule_tag_free(void *rule);

/*
 * route_map_counter_increment / route_map_counter_decrement：增减某 route-map 的
 * 活跃引用计数 use_count；减到 0 时表示该 route-map 已无使用者，可被回收。
 */
/* Increment the route-map used counter */
extern void route_map_counter_increment(struct route_map *map);

/* Decrement the route-map used counter */
extern void route_map_counter_decrement(struct route_map *map);

/*
 * route_map_match_set_hooks：全局 hook 表，每个字段保存一类 match / set 命令的
 * 处理函数指针。各协议的 hook 注册函数（route_map_xxx_hook）把 func 写进对应
 * 字段；northbound 层最终通过这些指针把命令转发给真正实现该关键字的协议。
 * 字段命名与前面 extern 的 hook 注册函数一一对应。
 */
/* Route map hooks data structure. */
struct route_map_match_set_hooks {
	/* match interface */
	int (*match_interface)(struct route_map_index *index,
			       const char *command, const char *arg,
			       route_map_event_t type,
			       char *errmsg, size_t errmsg_len);

	/* no match interface */
	int (*no_match_interface)(struct route_map_index *index,
				  const char *command, const char *arg,
				  route_map_event_t type,
				  char *errmsg, size_t errmsg_len);

	/* match ip address */
	int (*match_ip_address)(struct route_map_index *index,
				const char *command, const char *arg,
				route_map_event_t type,
				char *errmsg, size_t errmsg_len);

	/* no match ip address */
	int (*no_match_ip_address)(struct route_map_index *index,
				   const char *command, const char *arg,
				   route_map_event_t type,
				   char *errmsg, size_t errmsg_len);

	/* match ip address prefix list */
	int (*match_ip_address_prefix_list)(struct route_map_index *index,
					    const char *command,
					    const char *arg,
					    route_map_event_t type,
					    char *errmsg, size_t errmsg_len);

	/* no match ip address prefix list */
	int (*no_match_ip_address_prefix_list)(struct route_map_index *index,
					       const char *command,
					       const char *arg,
					       route_map_event_t type,
					       char *errmsg, size_t errmsg_len);

	/* match ip next hop */
	int (*match_ip_next_hop)(struct route_map_index *index,
				 const char *command, const char *arg,
				 route_map_event_t type,
				 char *errmsg, size_t errmsg_len);

	/* no match ip next hop */
	int (*no_match_ip_next_hop)(struct route_map_index *index,
				    const char *command, const char *arg,
				    route_map_event_t type,
				    char *errmsg, size_t errmsg_len);

	/* match ipv6 next hop */
	int (*match_ipv6_next_hop)(struct route_map_index *index,
				   const char *command, const char *arg,
				   route_map_event_t type, char *errmsg,
				   size_t errmsg_len);

	/* no match ipv6 next hop */
	int (*no_match_ipv6_next_hop)(struct route_map_index *index,
				      const char *command, const char *arg,
				      route_map_event_t type, char *errmsg,
				      size_t errmsg_len);

	/* match ipv6 next hop prefix-list */
	int (*match_ipv6_next_hop_prefix_list)(struct route_map_index *index,
					       const char *command,
					       const char *arg,
					       route_map_event_t type,
					       char *errmsg, size_t errmsg_len);

	/* no match ipv6 next-hop prefix-list */
	int (*no_match_ipv6_next_hop_prefix_list)(struct route_map_index *index,
						  const char *command,
						  const char *arg,
						  route_map_event_t type,
						  char *errmsg,
						  size_t errmsg_len);

	/* match ip next hop prefix list */
	int (*match_ip_next_hop_prefix_list)(struct route_map_index *index,
					     const char *command,
					     const char *arg,
					     route_map_event_t type,
					     char *errmsg, size_t errmsg_len);

	/* no match ip next hop prefix list */
	int (*no_match_ip_next_hop_prefix_list)(struct route_map_index *index,
						const char *command,
						const char *arg,
						route_map_event_t type,
						char *errmsg,
						size_t errmsg_len);

	/* match ip next-hop type */
	int (*match_ip_next_hop_type)(struct route_map_index *index,
				      const char *command,
				      const char *arg,
				      route_map_event_t type,
				      char *errmsg,
				      size_t errmsg_len);

	/* no match ip next-hop type */
	int (*no_match_ip_next_hop_type)(struct route_map_index *index,
					 const char *command,
					 const char *arg,
					 route_map_event_t type,
					 char *errmsg,
					 size_t errmsg_len);

	/* match ipv6 address */
	int (*match_ipv6_address)(struct route_map_index *index,
				  const char *command, const char *arg,
				  route_map_event_t type,
				  char *errmsg, size_t errmsg_len);

	/* no match ipv6 address */
	int (*no_match_ipv6_address)(struct route_map_index *index,
				     const char *command, const char *arg,
				     route_map_event_t type,
				     char *errmsg, size_t errmsg_len);


	/* match ipv6 address prefix list */
	int (*match_ipv6_address_prefix_list)(struct route_map_index *index,
					      const char *command,
					      const char *arg,
					      route_map_event_t type,
					      char *errmsg, size_t errmsg_len);

	/* no match ipv6 address prefix list */
	int (*no_match_ipv6_address_prefix_list)(struct route_map_index *index,
						 const char *command,
						 const char *arg,
						 route_map_event_t type,
						 char *errmsg,
						 size_t errmsg_len);

	/* match ipv6 next-hop type */
	int (*match_ipv6_next_hop_type)(struct route_map_index *index,
					      const char *command,
					      const char *arg,
					      route_map_event_t type,
					      char *errmsg, size_t errmsg_len);

	/* no match ipv6 next-hop type */
	int (*no_match_ipv6_next_hop_type)(struct route_map_index *index,
					   const char *command, const char *arg,
					   route_map_event_t type,
					   char *errmsg, size_t errmsg_len);

	/* match metric */
	int (*match_metric)(struct route_map_index *index,
			    const char *command, const char *arg,
			    route_map_event_t type,
			    char *errmsg, size_t errmsg_len);

	/* no match metric */
	int (*no_match_metric)(struct route_map_index *index,
			       const char *command, const char *arg,
			       route_map_event_t type,
			       char *errmsg, size_t errmsg_len);

	/* match tag */
	int (*match_tag)(struct route_map_index *index,
			 const char *command, const char *arg,
			 route_map_event_t type,
			 char *errmsg, size_t errmsg_len);

	/* no match tag */
	int (*no_match_tag)(struct route_map_index *index,
			    const char *command, const char *arg,
			    route_map_event_t type,
			    char *errmsg, size_t errmsg_len);

	/* set sr-te color */
	int (*set_srte_color)(struct route_map_index *index,
			      const char *command, const char *arg,
			      char *errmsg, size_t errmsg_len);

	/* no set sr-te color */
	int (*no_set_srte_color)(struct route_map_index *index,
				 const char *command, const char *arg,
				 char *errmsg, size_t errmsg_len);

	/* set ip nexthop */
	int (*set_ip_nexthop)(struct route_map_index *index,
			      const char *command, const char *arg,
			      char *errmsg, size_t errmsg_len);

	/* no set ip nexthop */
	int (*no_set_ip_nexthop)(struct route_map_index *index,
				 const char *command, const char *arg,
				 char *errmsg, size_t errmsg_len);

	/* set ipv6 nexthop local */
	int (*set_ipv6_nexthop_local)(struct route_map_index *index,
				      const char *command, const char *arg,
				      char *errmsg, size_t errmsg_len);

	/* no set ipv6 nexthop local */
	int (*no_set_ipv6_nexthop_local)(struct route_map_index *index,
					 const char *command, const char *arg,
					 char *errmsg, size_t errmsg_len);

	/* set metric */
	int (*set_metric)(struct route_map_index *index,
			  const char *command, const char *arg,
			  char *errmsg, size_t errmsg_len);

	/* no set metric */
	int (*no_set_metric)(struct route_map_index *index,
			     const char *command, const char *arg,
			     char *errmsg, size_t errmsg_len);
	/* set min-metric */
	int (*set_min_metric)(struct route_map_index *index,
			      const char *command, const char *arg,
			      char *errmsg, size_t errmsg_len);

	/* no set min-metric */
	int (*no_set_min_metric)(struct route_map_index *index,
				 const char *command, const char *arg,
				 char *errmsg, size_t errmsg_len);

	/* set max-metric */
	int (*set_max_metric)(struct route_map_index *index,
			      const char *command, const char *arg,
			      char *errmsg, size_t errmsg_len);

	/* no set max-metric */
	int (*no_set_max_metric)(struct route_map_index *index,
				 const char *command, const char *arg,
				 char *errmsg, size_t errmsg_len);

	/* set tag */
	int (*set_tag)(struct route_map_index *index,
		       const char *command, const char *arg,
		       char *errmsg, size_t errmsg_len);

	/* no set tag */
	int (*no_set_tag)(struct route_map_index *index,
			  const char *command, const char *arg,
			  char *errmsg, size_t errmsg_len);
};

/*
 * rmap_match_set_hook：上述 hook 表的全局单例。
 */
extern struct route_map_match_set_hooks rmap_match_set_hook;

/*
 * route_map_list：所有 route-map 全局链表的头尾指针，并保存增 / 删 / 事件回调。
 * 系统里所有 route-map 都挂在这条链表上。
 */
/* Making route map list. */
struct route_map_list {
	struct route_map *head;
	struct route_map *tail;

	void (*add_hook)(const char *);
	void (*delete_hook)(const char *);
	void (*event_hook)(const char *);
};

/*
 * route_map_master：上述全局链表结构的单例。
 */
extern struct route_map_list route_map_master;

/*
 * route_map_get：按名字取得 route-map，不存在则创建（与只查找不创建的
 * route_map_lookup_by_name 相对）。
 */
extern struct route_map *route_map_get(const char *name);
/*
 * route_map_delete：删除整张 route-map 及其全部子句，并触发相应回调。
 */
extern void route_map_delete(struct route_map *map);
/*
 * route_map_index_get：在指定 route-map 中取得（不存在则创建）具有给定类型
 * type 与序号 pref 的子句（index）。
 */
extern struct route_map_index *route_map_index_get(struct route_map *map,
						   enum route_map_type type,
						   int pref);
/*
 * route_map_index_delete：删除指定子句，notify 决定是否通知依赖者。
 */
extern void route_map_index_delete(struct route_map_index *index, int notify);

/*
 * 以下属于 routemap_northbound.c 的接口：route-map 的 northbound（yang 驱动）
 * 实现。routemap_match_hook_fun / routemap_set_hook_fun 是 match / set 命令
 * 处理函数的类型别名，签名与前面 hook 注册函数中的 func 一致。
 */
/* routemap_northbound.c */
typedef int (*routemap_match_hook_fun)(struct route_map_index *rmi,
				       const char *command, const char *arg,
				       route_map_event_t event,
				       char *errmsg, size_t errmsg_len);
typedef int (*routemap_set_hook_fun)(struct route_map_index *rmi,
				     const char *command, const char *arg,
				     char *errmsg, size_t errmsg_len);
/*
 * routemap_hook_context：一次 match / set 命令解析过程中的上下文，记录目标
 * 子句 rhc_rmi、规则名 rhc_rule、事件类型 rhc_event，以及对应的 set / match 钩子
 * 函数指针。它通过 rhc_entry 串入子句的 rhclist 队列，便于在事务回滚 / 销毁时
 * 回收。
 */
struct routemap_hook_context {
	struct route_map_index *rhc_rmi;
	const char *rhc_rule;
	route_map_event_t rhc_event;
	routemap_set_hook_fun rhc_shook;
	routemap_match_hook_fun rhc_mhook;
	TAILQ_ENTRY(routemap_hook_context) rhc_entry;
};

/*
 * lib_route_map_entry_match_destroy / lib_route_map_entry_set_destroy：
 * northbound 在删除 match / set 条目时调用的销毁回调。
 */
int lib_route_map_entry_match_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_destroy(struct nb_cb_destroy_args *args);

/*
 * routemap_hook_context_insert：为某子句创建并插入一个 hook 上下文，返回之。
 * routemap_hook_context_free：释放一个 hook 上下文。
 */
struct routemap_hook_context *
routemap_hook_context_insert(struct route_map_index *rmi);
void routemap_hook_context_free(struct routemap_hook_context *rhc);

/*
 * 对外暴露的 yang 模块信息：frr_route_map_info 为 route-map 数据模型，
 * frr_route_map_cli_info 为配套的 CLI 辅助模型。二者由 northbound 框架使用。
 */
extern const struct frr_yang_module_info frr_route_map_info;
extern const struct frr_yang_module_info frr_route_map_cli_info;

/*
 * 以下属于 routemap_cli.c 的接口：route-map 的 CLI / yang 显示与配置回调。
 * *_show 系列把 yang 数据节点（dnode）渲染为面向用户的命令行文本，
 * show_defaults 决定是否显示默认值；route_map_instance_cmp 用于子句序号的比较
 * 排序；route_map_cli_init 完成 CLI 命令注册。
 */
/* routemap_cli.c */
extern int route_map_instance_cmp(const struct lyd_node *dnode1,
				  const struct lyd_node *dnode2);
extern void route_map_instance_show(struct vty *vty,
				    const struct lyd_node *dnode,
				    bool show_defaults);
extern void route_map_instance_show_end(struct vty *vty,
					const struct lyd_node *dnode);
extern void route_map_condition_show(struct vty *vty,
				     const struct lyd_node *dnode,
				     bool show_defaults);
extern void route_map_action_show(struct vty *vty, const struct lyd_node *dnode,
				  bool show_defaults);
extern void route_map_exit_policy_show(struct vty *vty,
				       const struct lyd_node *dnode,
				       bool show_defaults);
extern void route_map_call_show(struct vty *vty, const struct lyd_node *dnode,
				bool show_defaults);
extern void route_map_description_show(struct vty *vty,
				       const struct lyd_node *dnode,
				       bool show_defaults);
extern void route_map_optimization_disabled_show(struct vty *vty,
						 const struct lyd_node *dnode,
						 bool show_defaults);
extern void route_map_cli_init(void);

/*
 * route_map_show_debug：显示 route-map 相关调试开关（rmap_debug）的当前状态。
 */
extern void route_map_show_debug(struct vty *vty);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_ROUTEMAP_H */
