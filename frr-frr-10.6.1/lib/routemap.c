// SPDX-License-Identifier: GPL-2.0-or-later
/* Route map function.
 * Copyright (C) 1998, 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：routemap.c —— FRR 路由策略框架（route-map）的核心实现。
 *
 * 本文件实现 route-map 的“通用引擎”。具体某条 match / set 关键字如何解析与
 * 执行，由各协议注册的规则类别（struct route_map_rule_cmd）与 northbound hook
 * 提供；本文件只负责框架本身：子句管理、规则增删、依赖跟踪、事件通知，以及核心
 * 的 route_map_apply 匹配流程。
 *
 * 关键流程：
 *   1) 配置解析：northbound / CLI 通过 route_map_add_match / route_map_add_set
 *      把一条规则编译后挂到子句的 match_list 或 set_list 上。
 *   2) 路由处理：协议调用 route_map_apply → route_map_apply_ext，按 pref 递增
 *      遍历子句；每个子句先跑完 match（任一返回 NOMATCH 即视为本子句不命中），
 *      命中后执行 set，再依据 exitpolicy 决定 exit / goto / next。
 *   3) call 与依赖：子句可通过 call 调用别的 route-map（递归深度受限），也可通过
 *      prefix-list 等外部对象建立依赖；对象变化时经 route_map_notify_dependencies
 *      联动重算。
 *   4) 事件与优化：变更事件经 route_map_event_queue 聚合；对仅含 prefix-list
 *      match 的子句会维护 ipv4_prefix_table / ipv6_prefix_table 以加速匹配。
 *
 * 阅读建议：先看 route_map_apply_ext 的匹配循环，再看 index / rule 的增删与编译，
 * 最后看依赖跟踪与事件通知部分。
 */

#include <zebra.h>

#include "linklist.h"
#include "memory.h"
#include "command.h"
#include "vector.h"
#include "prefix.h"
#include "vty.h"
#include "routemap.h"
#include "command.h"
#include "log.h"
#include "hash.h"
#include "libfrr.h"
#include "lib_errors.h"
#include "table.h"
#include "json.h"
#include "jhash.h"

#include "lib/routemap_clippy.c"

/*
 * DEFINE_MTYPE：定义本模块使用的内存类型。ROUTE_MAP 为 route-map 结构，
 * ROUTE_MAP_NAME 为名字字符串，ROUTE_MAP_INDEX 为子句，ROUTE_MAP_RULE 为规则，
 * ROUTE_MAP_RULE_STR 为规则的字符串副本，ROUTE_MAP_COMPILED 为编译后的规则值，
 * ROUTE_MAP_DEP / ROUTE_MAP_DEP_DATA 为依赖表及其数据。
 */
DEFINE_MTYPE_STATIC(LIB, ROUTE_MAP, "Route map");
DEFINE_MTYPE(LIB, ROUTE_MAP_NAME, "Route map name");
DEFINE_MTYPE_STATIC(LIB, ROUTE_MAP_INDEX, "Route map index");
DEFINE_MTYPE(LIB, ROUTE_MAP_RULE, "Route map rule");
DEFINE_MTYPE_STATIC(LIB, ROUTE_MAP_RULE_STR, "Route map rule str");
DEFINE_MTYPE(LIB, ROUTE_MAP_COMPILED, "Route map compiled");
DEFINE_MTYPE_STATIC(LIB, ROUTE_MAP_DEP, "Route map dependency");
DEFINE_MTYPE_STATIC(LIB, ROUTE_MAP_DEP_DATA, "Route map dependency data");

/*
 * DEFINE_QOBJ_TYPE：为 route_map_index 与 route_map 启用 qobj 引用计数 / 调试
 * 支持（对应头文件里的 QOBJ_FIELDS 与 DECLARE_QOBJ_TYPE）。
 */
DEFINE_QOBJ_TYPE(route_map_index);
DEFINE_QOBJ_TYPE(route_map);

/*
 * rmap_cmd_name_cmp：规则命令哈希表的比较函数，按规则名 cmd->str 字典序比较。
 */
static int rmap_cmd_name_cmp(const struct route_map_rule_cmd_proxy *a,
			     const struct route_map_rule_cmd_proxy *b)
{
	return strcmp(a->cmd->str, b->cmd->str);
}

/*
 * rmap_cmd_name_hash：规则命令哈希表的哈希函数，对规则名做 jhash。
 */
static uint32_t rmap_cmd_name_hash(const struct route_map_rule_cmd_proxy *item)
{
	return jhash(item->cmd->str, strlen(item->cmd->str), 0xbfd69320);
}

/*
 * DECLARE_HASH：声明以规则名索引的哈希表类型 rmap_cmd_name，元素为
 * route_map_rule_cmd_proxy，使用上面定义的比较与哈希函数。用于按名字快速查找
 * 已注册的 match / set 规则类别。
 */
DECLARE_HASH(rmap_cmd_name, struct route_map_rule_cmd_proxy, itm,
	     rmap_cmd_name_cmp, rmap_cmd_name_hash);

/*
 * 全局规则类别表：rmap_match_cmds 保存所有已注册的 match 规则类别，
 * rmap_set_cmds 保存所有已注册的 set 规则类别。各协议通过 route_map_install_match
 * / route_map_install_set 注册后，规则即进入这两张表。
 */
static struct rmap_cmd_name_head rmap_match_cmds[1] = {
	INIT_HASH(rmap_match_cmds[0]),
};
static struct rmap_cmd_name_head rmap_set_cmds[1] = {
	INIT_HASH(rmap_set_cmds[0]),
};

/*
 * 用于识别“按前缀列表匹配”这类 match 规则的关键字前缀。
 * IS_RULE_IPv4_PREFIX_LIST / IS_RULE_IPv6_PREFIX_LIST 判断规则名是否以
 * ip address prefix-list / ipv6 address prefix-list 开头，从而决定是否走前缀表
 * 优化路径。
 */
#define IPv4_PREFIX_LIST "ip address prefix-list"
#define IPv6_PREFIX_LIST "ipv6 address prefix-list"

#define IS_RULE_IPv4_PREFIX_LIST(S)                                            \
	(strncmp(S, IPv4_PREFIX_LIST, strlen(IPv4_PREFIX_LIST)) == 0)
#define IS_RULE_IPv6_PREFIX_LIST(S)                                            \
	(strncmp(S, IPv6_PREFIX_LIST, strlen(IPv6_PREFIX_LIST)) == 0)

/*
 * route_map_pentry_dep：prefix-list 依赖的登记项。记录被依赖的具体 entry
 * （pentry）、其所属列表名 plist_name，以及关注的事件 event。
 */
struct route_map_pentry_dep {
	struct prefix_list_entry *pentry;
	const char *plist_name;
	route_map_event_t event;
};

static void route_map_pfx_tbl_update(route_map_event_t event,
				     struct route_map_index *index, afi_t afi,
				     const char *plist_name);
static void route_map_pfx_table_add_default(afi_t afi,
					    struct route_map_index *index);
static void route_map_pfx_table_del_default(afi_t afi,
					    struct route_map_index *index);
static void route_map_add_plist_entries(afi_t afi,
					struct route_map_index *index,
					const char *plist_name,
					struct prefix_list_entry *entry);
static void route_map_del_plist_entries(afi_t afi,
					struct route_map_index *index,
					const char *plist_name,
					struct prefix_list_entry *entry);

/*
 * 本文件内部的前置声明：前缀表更新、前缀表默认项增删、prefix-list entry 增删、
 * 依赖哈希获取，以及释放 route-map 的内部函数。
 */
static struct hash *route_map_get_dep_hash(route_map_event_t event);
static void route_map_free_map(struct route_map *map);

/*
 * rmap_match_set_hook：全局 hook 表的单例定义（其类型与字段声明见 routemap.h）。
 */
struct route_map_match_set_hooks rmap_match_set_hook;

/*
 * 以下是一组 hook 注册函数（route_map_xxx_hook），每个函数体只有一行：把协议传入
 * 的处理函数 func 保存进 rmap_match_set_hook 的对应字段。各函数的语义与参数含义
 * 见 routemap.h 中同名声明上方的注释。
 */
/*
 * route_map_match_interface_hook：把“match interface”的处理函数 func 存入
 * rmap_match_set_hook.match_interface。
 */
/* match interface */
void route_map_match_interface_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_interface = func;
}

/*
 * route_map_no_match_interface_hook：把“no match interface”的处理函数 func 存入
 * rmap_match_set_hook.no_match_interface。
 */
/* no match interface */
void route_map_no_match_interface_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_interface = func;
}

/*
 * route_map_match_ip_address_hook：把“match ip address”的处理函数 func 存入
 * rmap_match_set_hook.match_ip_address。
 */
/* match ip address */
void route_map_match_ip_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ip_address = func;
}

/*
 * route_map_no_match_ip_address_hook：把“no match ip address”的处理函数 func
 * 存入 rmap_match_set_hook.no_match_ip_address。
 */
/* no match ip address */
void route_map_no_match_ip_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ip_address = func;
}

/*
 * route_map_match_ip_address_prefix_list_hook：把“match ip address prefix-list”的
 * 处理函数 func 存入 rmap_match_set_hook.match_ip_address_prefix_list。
 */
/* match ip address prefix list */
void route_map_match_ip_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ip_address_prefix_list = func;
}

/*
 * route_map_no_match_ip_address_prefix_list_hook：把“no match ip address
 * prefix-list”的处理函数 func 存入对应字段。
 */
/* no match ip address prefix list */
void route_map_no_match_ip_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ip_address_prefix_list = func;
}

/*
 * route_map_match_ip_next_hop_hook：把“match ip next-hop”的处理函数 func 存入
 * rmap_match_set_hook.match_ip_next_hop。
 */
/* match ip next hop */
void route_map_match_ip_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ip_next_hop = func;
}

/*
 * route_map_no_match_ip_next_hop_hook：把“no match ip next-hop”的处理函数 func
 * 存入 rmap_match_set_hook.no_match_ip_next_hop。
 */
/* no match ip next hop */
void route_map_no_match_ip_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ip_next_hop = func;
}

/*
 * route_map_match_ipv6_next_hop_hook：把“match ipv6 next-hop”的处理函数 func
 * 存入 rmap_match_set_hook.match_ipv6_next_hop。
 */
/* match ipv6 next-hop */
void route_map_match_ipv6_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ipv6_next_hop = func;
}

/*
 * route_map_no_match_ipv6_next_hop_hook：把“no match ipv6 next-hop”的处理函数
 * func 存入 rmap_match_set_hook.no_match_ipv6_next_hop。
 */
/* no match ipv6 next-hop */
void route_map_no_match_ipv6_next_hop_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ipv6_next_hop = func;
}

/*
 * route_map_match_ip_next_hop_prefix_list_hook：把“match ip next-hop prefix-list”
 * 的处理函数 func 存入 rmap_match_set_hook.match_ip_next_hop_prefix_list。
 */
/* match ip next hop prefix list */
void route_map_match_ip_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ip_next_hop_prefix_list = func;
}

/*
 * route_map_no_match_ip_next_hop_prefix_list_hook：把“no match ip next-hop
 * prefix-list”的处理函数 func 存入对应字段。
 */
/* no match ip next hop prefix list */
void route_map_no_match_ip_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ip_next_hop_prefix_list = func;
}

/*
 * route_map_match_ip_next_hop_type_hook：把“match ip next-hop type”的处理函数
 * func 存入 rmap_match_set_hook.match_ip_next_hop_type。
 */
/* match ip next-hop type */
void route_map_match_ip_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ip_next_hop_type = func;
}

/*
 * route_map_no_match_ip_next_hop_type_hook：把“no match ip next-hop type”的处理
 * 函数 func 存入对应字段。
 */
/* no match ip next-hop type */
void route_map_no_match_ip_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ip_next_hop_type = func;
}

/*
 * route_map_match_ipv6_address_hook：把“match ipv6 address”的处理函数 func 存入
 * rmap_match_set_hook.match_ipv6_address。
 */
/* match ipv6 address */
void route_map_match_ipv6_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ipv6_address = func;
}

/*
 * route_map_no_match_ipv6_address_hook：把“no match ipv6 address”的处理函数 func
 * 存入 rmap_match_set_hook.no_match_ipv6_address。
 */
/* no match ipv6 address */
void route_map_no_match_ipv6_address_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ipv6_address = func;
}


/*
 * route_map_match_ipv6_address_prefix_list_hook：把“match ipv6 address
 * prefix-list”的处理函数 func 存入对应字段。
 */
/* match ipv6 address prefix list */
void route_map_match_ipv6_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ipv6_address_prefix_list = func;
}

/*
 * route_map_no_match_ipv6_address_prefix_list_hook：把“no match ipv6 address
 * prefix-list”的处理函数 func 存入对应字段。
 */
/* no match ipv6 address prefix list */
void route_map_no_match_ipv6_address_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ipv6_address_prefix_list = func;
}

/*
 * route_map_match_ipv6_next_hop_type_hook：把“match ipv6 next-hop type”的处理函数
 * func 存入 rmap_match_set_hook.match_ipv6_next_hop_type。
 */
/* match ipv6 next-hop type */
void route_map_match_ipv6_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ipv6_next_hop_type = func;
}

/*
 * route_map_no_match_ipv6_next_hop_type_hook：把“no match ipv6 next-hop type”的
 * 处理函数 func 存入对应字段。
 */
/* no match ipv6 next-hop type */
void route_map_no_match_ipv6_next_hop_type_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ipv6_next_hop_type = func;
}

/*
 * route_map_match_ipv6_next_hop_prefix_list_hook：把“match ipv6 next-hop
 * prefix-list”的处理函数 func 存入对应字段。
 */
/* match ipv6 next-hop prefix-list */
void route_map_match_ipv6_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_ipv6_next_hop_prefix_list = func;
}

/*
 * route_map_no_match_ipv6_next_hop_prefix_list_hook：把“no match ipv6 next-hop
 * prefix-list”的处理函数 func 存入对应字段。
 */
/* no match ipv6 next-hop prefix-list */
void route_map_no_match_ipv6_next_hop_prefix_list_hook(int (*func)(
	struct route_map_index *index, const char *command, const char *arg,
	route_map_event_t type, char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_ipv6_next_hop_prefix_list = func;
}

/*
 * route_map_match_metric_hook：把“match metric”的处理函数 func 存入
 * rmap_match_set_hook.match_metric。
 */
/* match metric */
void route_map_match_metric_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_metric = func;
}

/*
 * route_map_no_match_metric_hook：把“no match metric”的处理函数 func 存入
 * rmap_match_set_hook.no_match_metric。
 */
/* no match metric */
void route_map_no_match_metric_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_metric = func;
}

/*
 * route_map_match_tag_hook：把“match tag”的处理函数 func 存入
 * rmap_match_set_hook.match_tag。
 */
/* match tag */
void route_map_match_tag_hook(int (*func)(struct route_map_index *index,
					  const char *command, const char *arg,
					  route_map_event_t type,
					  char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.match_tag = func;
}

/*
 * route_map_no_match_tag_hook：把“no match tag”的处理函数 func 存入
 * rmap_match_set_hook.no_match_tag。
 */
/* no match tag */
void route_map_no_match_tag_hook(int (*func)(
	struct route_map_index *index, const char *command,
	const char *arg, route_map_event_t type,
	char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_match_tag = func;
}

/*
 * route_map_set_srte_color_hook：把“set sr-te color”的处理函数 func 存入
 * rmap_match_set_hook.set_srte_color。
 */
/* set sr-te color */
void route_map_set_srte_color_hook(int (*func)(struct route_map_index *index,
					       const char *command,
					       const char *arg,
					       char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.set_srte_color = func;
}

/*
 * route_map_no_set_srte_color_hook：把“no set sr-te color”的处理函数 func 存入
 * rmap_match_set_hook.no_set_srte_color。
 */
/* no set sr-te color */
void route_map_no_set_srte_color_hook(int (*func)(struct route_map_index *index,
						  const char *command,
						  const char *arg,
						  char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_set_srte_color = func;
}

/*
 * route_map_set_ip_nexthop_hook：把“set ip next-hop”的处理函数 func 存入
 * rmap_match_set_hook.set_ip_nexthop。
 */
/* set ip nexthop */
void route_map_set_ip_nexthop_hook(int (*func)(struct route_map_index *index,
					       const char *command,
					       const char *arg,
					       char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.set_ip_nexthop = func;
}

/*
 * route_map_no_set_ip_nexthop_hook：把“no set ip next-hop”的处理函数 func 存入
 * rmap_match_set_hook.no_set_ip_nexthop。
 */
/* no set ip nexthop */
void route_map_no_set_ip_nexthop_hook(int (*func)(struct route_map_index *index,
						  const char *command,
						  const char *arg,
						  char *errmsg,
						  size_t errmsg_len))
{
	rmap_match_set_hook.no_set_ip_nexthop = func;
}

/*
 * route_map_set_ipv6_nexthop_local_hook：把“set ipv6 next-hop local”的处理函数
 * func 存入 rmap_match_set_hook.set_ipv6_nexthop_local。
 */
/* set ipv6 nexthop local */
void route_map_set_ipv6_nexthop_local_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.set_ipv6_nexthop_local = func;
}

/*
 * route_map_no_set_ipv6_nexthop_local_hook：把“no set ipv6 next-hop local”的
 * 处理函数 func 存入对应字段。
 */
/* no set ipv6 nexthop local */
void route_map_no_set_ipv6_nexthop_local_hook(
	int (*func)(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_set_ipv6_nexthop_local = func;
}

/*
 * route_map_set_metric_hook：把“set metric”的处理函数 func 存入
 * rmap_match_set_hook.set_metric。
 */
/* set metric */
void route_map_set_metric_hook(int (*func)(struct route_map_index *index,
					   const char *command,
					   const char *arg,
					   char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.set_metric = func;
}

/*
 * route_map_no_set_metric_hook：把“no set metric”的处理函数 func 存入
 * rmap_match_set_hook.no_set_metric。
 */
/* no set metric */
void route_map_no_set_metric_hook(int (*func)(struct route_map_index *index,
					      const char *command,
					      const char *arg,
					      char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_set_metric = func;
}
/*
 * route_map_set_min_metric_hook：把“set min-metric”的处理函数 func 存入
 * rmap_match_set_hook.set_min_metric。
 */
/* set min-metric */
void route_map_set_min_metric_hook(int (*func)(struct route_map_index *index,
					       const char *command,
					       const char *arg, char *errmsg,
					       size_t errmsg_len))
{
	rmap_match_set_hook.set_min_metric = func;
}

/*
 * route_map_no_set_min_metric_hook：把“no set min-metric”的处理函数 func 存入
 * rmap_match_set_hook.no_set_min_metric。
 */
/* no set min-metric */
void route_map_no_set_min_metric_hook(int (*func)(struct route_map_index *index,
						  const char *command,
						  const char *arg, char *errmsg,
						  size_t errmsg_len))
{
	rmap_match_set_hook.no_set_min_metric = func;
}
/*
 * route_map_set_max_metric_hook：把“set max-metric”的处理函数 func 存入
 * rmap_match_set_hook.set_max_metric。
 */
/* set max-metric */
void route_map_set_max_metric_hook(int (*func)(struct route_map_index *index,
					       const char *command,
					       const char *arg, char *errmsg,
					       size_t errmsg_len))
{
	rmap_match_set_hook.set_max_metric = func;
}

/*
 * route_map_no_set_max_metric_hook：把“no set max-metric”的处理函数 func 存入
 * rmap_match_set_hook.no_set_max_metric。
 */
/* no set max-metric */
void route_map_no_set_max_metric_hook(int (*func)(struct route_map_index *index,
						  const char *command,
						  const char *arg, char *errmsg,
						  size_t errmsg_len))
{
	rmap_match_set_hook.no_set_max_metric = func;
}

/*
 * route_map_set_tag_hook：把“set tag”的处理函数 func 存入
 * rmap_match_set_hook.set_tag。
 */
/* set tag */
void route_map_set_tag_hook(int (*func)(struct route_map_index *index,
					const char *command, const char *arg,
					char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.set_tag = func;
}

/*
 * route_map_no_set_tag_hook：把“no set tag”的处理函数 func 存入
 * rmap_match_set_hook.no_set_tag。
 */
/* no set tag */
void route_map_no_set_tag_hook(int (*func)(struct route_map_index *index,
					   const char *command,
					   const char *arg,
					   char *errmsg, size_t errmsg_len))
{
	rmap_match_set_hook.no_set_tag = func;
}

/*
 * generic_match_add：各协议 match hook 的通用实现。把 command / arg 交给
 * route_map_add_match 编译并挂载；根据编译结果向 errmsg 写错误信息，返回
 * CMD_SUCCESS 或 CMD_WARNING_CONFIG_FAILED。
 */
int generic_match_add(struct route_map_index *index,
		      const char *command, const char *arg,
		      route_map_event_t type,
		      char *errmsg, size_t errmsg_len)
{
	enum rmap_compile_rets ret;

	ret = route_map_add_match(index, command, arg, type);
	switch (ret) {
	case RMAP_RULE_MISSING:
		snprintf(errmsg, errmsg_len, "%% [%s] Can't find rule.",
			 frr_protonameinst);
		return CMD_WARNING_CONFIG_FAILED;
	case RMAP_COMPILE_ERROR:
		snprintf(errmsg, errmsg_len,
			 "%% [%s] Argument form is unsupported or malformed.",
			 frr_protonameinst);
		return CMD_WARNING_CONFIG_FAILED;
	case RMAP_COMPILE_SUCCESS:
		/*
		 * Nothing to do here move along
		 */
		break;
	}

	return CMD_SUCCESS;
}

/*
 * generic_match_delete：各协议 no match hook 的通用实现。
 * 当事件不是直接删除（type != RMAP_EVENT_MATCH_DELETED）时，需先取出该 match 的
 * 依赖对象名（arg 为空则从 index 反查），并复制 route-map 名，以便删除后通过依赖
 * 机制通知受影响方；随后调用 route_map_delete_match 并按结果返回。
 * 坑：dep_name / rmap_name 是临时分配的，函数末尾必须 XFREE，避免泄漏。
 */
int generic_match_delete(struct route_map_index *index,
			 const char *command, const char *arg,
			 route_map_event_t type,
			 char *errmsg, size_t errmsg_len)
{
	enum rmap_compile_rets ret;
	int retval = CMD_SUCCESS;
	char *dep_name = NULL;
	const char *tmpstr;
	char *rmap_name = NULL;

	if (type != RMAP_EVENT_MATCH_DELETED) {
		/* ignore the mundane, the types without any dependency */
		if (arg == NULL) {
			if ((tmpstr = route_map_get_match_arg(index, command))
			    != NULL)
				dep_name =
					XSTRDUP(MTYPE_ROUTE_MAP_RULE, tmpstr);
		} else {
			dep_name = XSTRDUP(MTYPE_ROUTE_MAP_RULE, arg);
		}
		rmap_name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, index->map->name);
	}

	ret = route_map_delete_match(index, command, dep_name, type);
	switch (ret) {
	case RMAP_RULE_MISSING:
		snprintf(errmsg, errmsg_len, "%% [%s] Can't find rule.",
			 frr_protonameinst);
		retval = CMD_WARNING_CONFIG_FAILED;
		break;
	case RMAP_COMPILE_ERROR:
		snprintf(errmsg, errmsg_len,
			 "%% [%s] Argument form is unsupported or malformed.",
			 frr_protonameinst);
		retval = CMD_WARNING_CONFIG_FAILED;
		break;
	case RMAP_COMPILE_SUCCESS:
		/*
		 * Nothing to do here
		 */
		break;
	}

	XFREE(MTYPE_ROUTE_MAP_RULE, dep_name);
	XFREE(MTYPE_ROUTE_MAP_NAME, rmap_name);

	return retval;
}

/*
 * generic_set_add：各协议 set hook 的通用实现。调用 route_map_add_set 编译并
 * 挂载 set 规则，按结果返回 CMD_SUCCESS 或 CMD_WARNING_CONFIG_FAILED。
 */
int generic_set_add(struct route_map_index *index,
		    const char *command, const char *arg,
		    char *errmsg, size_t errmsg_len)
{
	enum rmap_compile_rets ret;

	ret = route_map_add_set(index, command, arg);
	switch (ret) {
	case RMAP_RULE_MISSING:
		snprintf(errmsg, errmsg_len,
			 "%% [%s] Can't find rule.", frr_protonameinst);
		return CMD_WARNING_CONFIG_FAILED;
	case RMAP_COMPILE_ERROR:
		snprintf(errmsg, errmsg_len,
			 "%% [%s] Argument form is unsupported or malformed.",
			frr_protonameinst);
		return CMD_WARNING_CONFIG_FAILED;
	case RMAP_COMPILE_SUCCESS:
		break;
	}

	return CMD_SUCCESS;
}

/*
 * generic_set_delete：各协议 no set hook 的通用实现。调用 route_map_delete_set
 * 删除规则，按结果返回 CMD_SUCCESS 或 CMD_WARNING_CONFIG_FAILED。
 */
int generic_set_delete(struct route_map_index *index,
		       const char *command, const char *arg,
		       char *errmsg, size_t errmsg_len)
{
	enum rmap_compile_rets ret;

	ret = route_map_delete_set(index, command, arg);
	switch (ret) {
	case RMAP_RULE_MISSING:
			snprintf(errmsg, errmsg_len, "%% [%s] Can't find rule.",
				 frr_protonameinst);
		return CMD_WARNING_CONFIG_FAILED;
	case RMAP_COMPILE_ERROR:
			snprintf(errmsg, errmsg_len,
				 "%%  [%s] Argument form is unsupported or malformed.",
				 frr_protonameinst);
		return CMD_WARNING_CONFIG_FAILED;
	case RMAP_COMPILE_SUCCESS:
		break;
	}

	return CMD_SUCCESS;
}


/* Master list of route map. */
struct route_map_list route_map_master = {NULL, NULL, NULL, NULL, NULL};
struct hash *route_map_master_hash = NULL;

/*
 * route_map_hash_key_make：route_map_master_hash 的键哈希函数，按 route-map 名字
 * 生成哈希键。
 */
static unsigned int route_map_hash_key_make(const void *p)
{
	const struct route_map *map = p;
	return string_hash_make(map->name);
}

/*
 * route_map_hash_cmp：route_map_master_hash 的比较函数，判断两个 route_map 的名字
 * 是否相等。
 */
static bool route_map_hash_cmp(const void *p1, const void *p2)
{
	const struct route_map *map1 = p1;
	const struct route_map *map2 = p2;

	if (!strcmp(map1->name, map2->name))
		return true;

	return false;
}

/*
 * route_map_upd8_type：依赖更新操作的类型——ROUTE_MAP_ADD 表示新增依赖，
 * ROUTE_MAP_DEL 表示删除依赖。
 */
enum route_map_upd8_type {
	ROUTE_MAP_ADD = 1,
	ROUTE_MAP_DEL,
};

/*
 * route_map_dep_type：route-map 可依赖的外部对象类型，每种类型对应
 * route_map_dep_hash[] 中的一个哈希表：RMAP 表示依赖别的 route-map（call），
 * CLIST / ECLIST / LCLIST / PLIST / ASPATH / FILTER 分别表示依赖 community-list、
 * extcommunity-list、large-community-list、prefix-list、as-path-list、filter-list。
 */
/* all possible route-map dependency types */
enum route_map_dep_type {
	ROUTE_MAP_DEP_RMAP = 1,
	ROUTE_MAP_DEP_CLIST,
	ROUTE_MAP_DEP_ECLIST,
	ROUTE_MAP_DEP_LCLIST,
	ROUTE_MAP_DEP_PLIST,
	ROUTE_MAP_DEP_ASPATH,
	ROUTE_MAP_DEP_FILTER,
	ROUTE_MAP_DEP_MAX,
};

/*
 * route_map_dep：一个被依赖对象（dep_name）到“依赖它的 route-map 集合”的映射。
 * dep_rmap_hash 保存依赖此对象的各 route-map 及其引用计数；this_hash 回指它所在
 * 的那个全局依赖哈希表。
 */
struct route_map_dep {
	char *dep_name;
	struct hash *dep_rmap_hash;
	struct hash *this_hash; /* ptr to the hash structure this is part of */
};

/*
 * route_map_dep_data：某个 route-map 依赖某对象的登记数据。rname 为 route-map 名，
 * refcnt 为该 route-map 中依赖同一对象的子句数量（用于去重与正确释放）。
 */
struct route_map_dep_data {
	/* Route-map name.
	 */
	char *rname;
	/* Count of number of sequences of this
	 * route-map that depend on the same entity.
	 */
	uint16_t  refcnt;
};

/*
 * route_map_dep_hash：按外部对象类型分别维护的依赖表数组，下标为
 * enum route_map_dep_type。
 */
/* Hashes maintaining dependency between various sublists used by route maps */
static struct hash *route_map_dep_hash[ROUTE_MAP_DEP_MAX];

/*
 * 本文件内部前置声明：依赖表键哈希、清理某 route-map 的全部引用、以及规则删除。
 */
static unsigned int route_map_dep_hash_make_key(const void *p);
static void route_map_clear_all_references(char *rmap_name);
static void route_map_rule_delete(struct route_map_rule_list *,
				  struct route_map_rule *);

/*
 * rmap_debug：route-map 调试位掩码的全局定义（声明见 routemap.h）。
 */
uint32_t rmap_debug;

/*
 * route_map_new：分配并初始化一个 route_map 结构，复制名字并注册 qobj。
 * name 必须非空。仅做分配，不挂入全局链表 / 哈希（由 route_map_add 完成）。
 */
/* New route map allocation. Please note route map's name must be
   specified. */
static struct route_map *route_map_new(const char *name)
{
	struct route_map *new;

	new = XCALLOC(MTYPE_ROUTE_MAP, sizeof(struct route_map));
	new->name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, name);
	QOBJ_REG(new, route_map);
	return new;
}

/*
 * route_map_add：创建一个 route-map 并挂入全局链表与哈希表。
 * 关键点：若同名 route-map 已存在于哈希中（处于“删后重建”的过程），则继承其
 * to_be_processed 标志并释放旧对象，避免丢失尚未处理的更新状态。
 * 随后执行 add_hook 并通知 CALL_ADDED 依赖；首次创建时初始化 IPv4 / IPv6 前缀表。
 * 返回新对象。
 */
/* Add new name to route_map. */
static struct route_map *route_map_add(const char *name)
{
	struct route_map *map, *exist;
	struct route_map_list *list;

	map = route_map_new(name);
	list = &route_map_master;

	/*
	 * Add map to the hash
	 *
	 * If the map already exists in the hash, then we know that
	 * FRR is now in a sequence of delete/create.
	 * All FRR needs to do here is set the to_be_processed
	 * bit (to inherit from the old one
	 */
	exist = hash_release(route_map_master_hash, map);
	if (exist) {
		map->to_be_processed = exist->to_be_processed;
		route_map_free_map(exist);
	}
	hash_get(route_map_master_hash, map, hash_alloc_intern);

	/* Add new entry to the head of the list to match how it is added in the
	 * hash table. This is to ensure that if the same route-map has been
	 * created more than once and then marked for deletion (which can happen
	 * if prior deletions haven't completed as BGP hasn't yet done the
	 * route-map processing), the order of the entities is the same in both
	 * the list and the hash table. Otherwise, since there is nothing to
	 * distinguish between the two entries, the wrong entry could get freed.
	 * TODO: This needs to be re-examined to handle it better - e.g., revive
	 * a deleted entry if the route-map is created again.
	 */
	map->prev = NULL;
	map->next = list->head;
	if (list->head)
		list->head->prev = map;
	list->head = map;
	if (!list->tail)
		list->tail = map;

	/* Execute hook. */
	if (route_map_master.add_hook) {
		(*route_map_master.add_hook)(name);
		route_map_notify_dependencies(name, RMAP_EVENT_CALL_ADDED);
	}

	if (!map->ipv4_prefix_table)
		map->ipv4_prefix_table = route_table_init();

	if (!map->ipv6_prefix_table)
		map->ipv6_prefix_table = route_table_init();

	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Add route-map %s", name);
	return map;
}

/*
 * route_map_free_map：真正释放 route_map——先删除其所有子句，再注销 qobj、
 * 从全局链表摘除、释放两张前缀表与哈希项，最后释放名字与结构体。
 * 注意：本函数预期在 delete_hook 之后的后处理阶段调用，内部不会再触发 delete_hook。
 */
/* this is supposed to be called post processing by
 * the delete hook function. Don't invoke delete_hook
 * again in this routine.
 */
static void route_map_free_map(struct route_map *map)
{
	struct route_map_list *list;
	struct route_map_index *index;

	if (map == NULL)
		return;

	while ((index = map->head) != NULL)
		route_map_index_delete(index, 0);

	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Deleting route-map %s", map->name);

	list = &route_map_master;

	QOBJ_UNREG(map);

	if (map->next)
		map->next->prev = map->prev;
	else
		list->tail = map->prev;

	if (map->prev)
		map->prev->next = map->next;
	else
		list->head = map->next;

	route_table_finish(map->ipv4_prefix_table);
	route_table_finish(map->ipv6_prefix_table);

	hash_release(route_map_master_hash, map);
	XFREE(MTYPE_ROUTE_MAP_NAME, map->name);
	XFREE(MTYPE_ROUTE_MAP, map);
}

/*
 * route_map_delete：删除 route-map 的对外入口。删除全部子句、清空依赖、置 deleted
 * 标志，并触发 delete_hook 与 CALL_DELETED 依赖通知。
 * 若该 map 仍处于“待处理”状态（to_be_processed），则不立即释放，留待
 * walk_update_list 后处理时回收。
 */
/* Route map delete from list. */
void route_map_delete(struct route_map *map)
{
	struct route_map_index *index;
	char *name;

	while ((index = map->head) != NULL)
		route_map_index_delete(index, 0);

	name = map->name;
	map->head = NULL;

	/* Clear all dependencies */
	route_map_clear_all_references(name);
	map->deleted = true;
	/* Execute deletion hook. */
	if (route_map_master.delete_hook) {
		(*route_map_master.delete_hook)(name);
		route_map_notify_dependencies(name, RMAP_EVENT_CALL_DELETED);
	}

	if (!map->to_be_processed) {
		route_map_free_map(map);
	}
}

/*
 * route_map_lookup_by_name：按名字在全局哈希中查找 route-map。name 为 NULL 或找到
 * 的对象已被删除（deleted）时返回 NULL。内部用临时 tmp_map 作查询键，靠 memset
 * 清零以保证 deleted 字段为 false。
 */
/* Lookup route map by route map name string. */
struct route_map *route_map_lookup_by_name(const char *name)
{
	struct route_map *map;
	struct route_map tmp_map;

	if (!name)
		return NULL;

	// map.deleted is false via memset
	memset(&tmp_map, 0, sizeof(tmp_map));
	tmp_map.name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, name);
	map = hash_lookup(route_map_master_hash, &tmp_map);
	XFREE(MTYPE_ROUTE_MAP_NAME, tmp_map.name);

	if (map && map->deleted)
		return NULL;

	return map;
}

/*
 * route_map_lookup_warn_noexist：查找 route-map，未找到且当前处于 vty shell 时打印
 * “does not exist”提示，便于命令解析时报错；返回查得对象或 NULL。
 */
/* Simple helper to warn if route-map does not exist. */
struct route_map *route_map_lookup_warn_noexist(struct vty *vty, const char *name)
{
	struct route_map *route_map = route_map_lookup_by_name(name);

	if (!route_map)
		if (vty_shell_serv(vty))
			vty_out(vty, "The route-map '%s' does not exist.\n", name);

	return route_map;
}

/*
 * route_map_mark_updated：把 route-map 标记为 to_be_processed。
 * 先用正常查找（deleted=false）；找不到再用 deleted=true 的临时键重试，以覆盖
 * “已被标记删除但重建 / 处理尚未完成”的场景。成功返回 0；name 为 NULL 或未找到
 * 返回 -1。
 */
int route_map_mark_updated(const char *name)
{
	struct route_map *map;
	int ret = -1;
	struct route_map tmp_map;

	if (!name)
		return (ret);

	map = route_map_lookup_by_name(name);

	/* If we did not find the routemap with deleted=false try again
	 * with deleted=true
	 */
	if (!map) {
		memset(&tmp_map, 0, sizeof(tmp_map));
		tmp_map.name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, name);
		tmp_map.deleted = true;
		map = hash_lookup(route_map_master_hash, &tmp_map);
		XFREE(MTYPE_ROUTE_MAP_NAME, tmp_map.name);
	}

	if (map) {
		map->to_be_processed = true;
		ret = 0;
	}

	return (ret);
}

/*
 * route_map_clear_updated：清除某 route-map 的 to_be_processed 标记；若它此前已被
 * 标记删除，则此刻真正释放它。
 */
static void route_map_clear_updated(struct route_map *map)
{
	if (map) {
		map->to_be_processed = false;
		if (map->deleted)
			route_map_free_map(map);
	}
}

/*
 * route_map_get：按名字取得 route-map，不存在则创建并返回（get-or-create）。
 */
/* Lookup route map.  If there isn't route map create one and return
   it. */
struct route_map *route_map_get(const char *name)
{
	struct route_map *map;

	map = route_map_lookup_by_name(name);
	if (map == NULL)
		map = route_map_add(name);

	return map;
}

/*
 * route_map_walk_update_list：遍历全局链表，对所有被标记为 to_be_processed 的
 * route-map 调用 route_map_update_fn 处理，然后清除标记。
 * 坑：处理回调可能释放当前节点，因此必须先把下一个节点 nnode 记下来再处理当前节点。
 */
void route_map_walk_update_list(void (*route_map_update_fn)(char *name))
{
	struct route_map *node;
	struct route_map *nnode = NULL;

	for (node = route_map_master.head; node; node = nnode) {
		if (node->to_be_processed) {
			/* DD: Should we add any thread yield code here */
			route_map_update_fn(node->name);
			nnode = node->next;
			route_map_clear_updated(node);
		} else
			nnode = node->next;
	}
}

/*
 * route_map_action_reason2str：把 route_map_apply 的退出原因枚举转换为可读字符串，
 * 用于日志与调试。
 */
static const char *route_map_action_reason2str(enum route_map_action_reason reason)
{
	switch (reason) {
	case route_map_action_none:
		return "none";
	case route_map_action_map_null:
		return "route-map is null";
	case route_map_action_no_index:
		return "no index";
	case route_map_action_next_deny:
		return "next statement is deny";
	case route_map_action_exit:
		return "exit policy";
	case route_map_action_goto_null:
		return "goto index is null";
	case route_map_action_index_deny:
		return "deny index";
	}

	return "Invalid reason";
}

/*
 * route_map_type_str：把子句类型 permit / deny / any 枚举转换为字符串；
 * RMAP_ANY 返回空串。
 */
/* Return route map's type string. */
static const char *route_map_type_str(enum route_map_type type)
{
	switch (type) {
	case RMAP_PERMIT:
		return "permit";
	case RMAP_DENY:
		return "deny";
	case RMAP_ANY:
		return "";
	}

	return "";
}

/*
 * route_map_cmd_result_str：把单条规则的执行结果枚举（match / nomatch / noop /
 * okay / error）转换为字符串，用于调试与回显。
 */
static const char *route_map_cmd_result_str(enum route_map_cmd_result_t res)
{
	switch (res) {
	case RMAP_MATCH:
		return "match";
	case RMAP_NOMATCH:
		return "no match";
	case RMAP_NOOP:
		return "noop";
	case RMAP_ERROR:
		return "error";
	case RMAP_OKAY:
		return "okay";
	}

	return "invalid";
}

/*
 * route_map_result_str：把 route_map_apply 的最终结果（deny / permit）转换为字符串，
 * 用于日志与调试。
 */
static const char *route_map_result_str(route_map_result_t res)
{
	switch (res) {
	case RMAP_DENYMATCH:
		return "deny";
	case RMAP_PERMITMATCH:
		return "permit";
	}

	return "invalid";
}

/*
 * vty_show_route_map_entry：输出单张 route-map 的完整内容（供 'show route-map' 使用）。
 * json 非空时以 JSON 结构输出（含 invoked、cpuTimeMS，以及每个子句的 match / set /
 * call / action），否则以文本输出。逐个遍历 map 的子句（index）及其 match_list /
 * set_list。注意文本模式对空 rule_str 做了保护，JSON 模式下 set 也做了保护。
 */
/* show route-map */
static void vty_show_route_map_entry(struct vty *vty, struct route_map *map,
				     json_object *json)
{
	struct route_map_index *index;
	struct route_map_rule *rule;
	json_object *json_rmap = NULL;
	json_object *json_rules = NULL;

	if (json) {
		json_rmap = json_object_new_object();
		json_object_object_add(json, map->name, json_rmap);

		json_rules = json_object_new_array();
		json_object_int_add(json_rmap, "invoked",
				    map->applied - map->applied_clear);
		json_object_boolean_add(json_rmap, "disabledOptimization",
					map->optimization_disabled);
		json_object_boolean_add(json_rmap, "processedChange",
					map->to_be_processed);
		json_object_object_add(json_rmap, "rules", json_rules);
		json_object_int_add(json_rmap, "cpuTimeMS", map->cputime / 1000);
	} else {
		vty_out(vty,
			"route-map: %s Invoked: %" PRIu64
			" (%zu milliseconds total) Optimization: %s Processed Change: %s\n",
			map->name, map->applied - map->applied_clear, map->cputime / 1000,
			map->optimization_disabled ? "disabled" : "enabled",
			map->to_be_processed ? "true" : "false");
	}

	for (index = map->head; index; index = index->next) {
		if (json) {
			json_object *json_rule;
			json_object *json_matches;
			json_object *json_sets;
			char action[BUFSIZ] = {};

			json_rule = json_object_new_object();
			json_object_array_add(json_rules, json_rule);

			json_object_int_add(json_rule, "sequenceNumber",
					    index->pref);
			json_object_string_add(json_rule, "type",
					       route_map_type_str(index->type));
			json_object_int_add(json_rule, "invoked",
					    index->applied
						    - index->applied_clear);
			json_object_int_add(json_rule, "cpuTimeMS", index->cputime / 1000);

			/* Description */
			if (index->description)
				json_object_string_add(json_rule, "description",
						       index->description);

			/* Match clauses */
			json_matches = json_object_new_array();
			json_object_object_add(json_rule, "matchClauses",
					       json_matches);
			for (rule = index->match_list.head; rule;
			     rule = rule->next) {
				char buf[BUFSIZ];

				snprintf(buf, sizeof(buf), "%s %s",
					 rule->cmd->str, rule->rule_str);
				json_array_string_add(json_matches, buf);
			}

			/* Set clauses */
			json_sets = json_object_new_array();
			json_object_object_add(json_rule, "setClauses",
					       json_sets);
			for (rule = index->set_list.head; rule;
			     rule = rule->next) {
				char buf[BUFSIZ];

				snprintf(buf, sizeof(buf), "%s %s",
					 rule->cmd->str, rule->rule_str ? rule->rule_str : "");
				json_array_string_add(json_sets, buf);
			}

			/* Call clause */
			if (index->nextrm)
				json_object_string_add(json_rule, "callClause",
						       index->nextrm);

			/* Exit Policy */
			if (index->exitpolicy == RMAP_GOTO)
				snprintf(action, sizeof(action), "Goto %d",
					 index->nextpref);
			else if (index->exitpolicy == RMAP_NEXT)
				snprintf(action, sizeof(action),
					 "Continue to next entry");
			else if (index->exitpolicy == RMAP_EXIT)
				snprintf(action, sizeof(action),
					 "Exit routemap");
			if (action[0] != '\0')
				json_object_string_add(json_rule, "action",
						       action);
		} else {
			vty_out(vty,
				" %s, sequence %d Invoked %" PRIu64 " (%zu milliseconds total)\n",
				route_map_type_str(index->type), index->pref,
				index->applied - index->applied_clear, index->cputime / 1000);

			/* Description */
			if (index->description)
				vty_out(vty, "  Description:\n    %s\n",
					index->description);

			/* Match clauses */
			vty_out(vty, "  Match clauses:\n");
			for (rule = index->match_list.head; rule;
			     rule = rule->next)
				vty_out(vty, "    %s %s\n", rule->cmd->str,
					rule->rule_str);

			/* Set clauses */
			vty_out(vty, "  Set clauses:\n");
			for (rule = index->set_list.head; rule;
			     rule = rule->next)
				vty_out(vty, "    %s %s\n", rule->cmd->str,
					rule->rule_str ? rule->rule_str : "");

			/* Call clause */
			vty_out(vty, "  Call clause:\n");
			if (index->nextrm)
				vty_out(vty, "    Call %s\n", index->nextrm);

			/* Exit Policy */
			vty_out(vty, "  Action:\n");
			if (index->exitpolicy == RMAP_GOTO)
				vty_out(vty, "    Goto %d\n", index->nextpref);
			else if (index->exitpolicy == RMAP_NEXT)
				vty_out(vty, "    Continue to next entry\n");
			else if (index->exitpolicy == RMAP_EXIT)
				vty_out(vty, "    Exit routemap\n");
		}
	}
}

/*
 * sort_route_map：list_sort 的比较回调，按 route-map 名字字典序排序。
 */
static int sort_route_map(const void **map1, const void **map2)
{
	const struct route_map *m1 = *map1;
	const struct route_map *m2 = *map2;

	return strcmp(m1->name, m2->name);
}

/*
 * vty_show_route_map：'show route-map [NAME]' 的实现。给定 name 则只显示该 route-map，
 * 否则把所有 route-map 收集到临时链表、按名字排序后逐个显示。use_json 决定输出格式；
 * 返回 vty_json 的结果。
 */
static int vty_show_route_map(struct vty *vty, const char *name, bool use_json)
{
	struct route_map *map;
	json_object *json = NULL;

	if (use_json)
		json = json_object_new_object();
	else
		vty_out(vty, "%s:\n", frr_protonameinst);

	if (name) {
		map = route_map_lookup_by_name(name);

		if (map) {
			vty_show_route_map_entry(vty, map, json);
		} else if (!use_json) {
			vty_out(vty, "%s: 'route-map %s' not found\n",
				frr_protonameinst, name);
		}
	} else {

		struct list *maplist = list_new();
		struct listnode *ln;

		for (map = route_map_master.head; map; map = map->next)
			listnode_add(maplist, map);

		list_sort(maplist, sort_route_map);

		for (ALL_LIST_ELEMENTS_RO(maplist, ln, map))
			vty_show_route_map_entry(vty, map, json);

		list_delete(&maplist);
	}

	return vty_json(vty, json);
}

/*
 * vty_show_unused_route_map：'show route-map unused' 的实现，列出所有 use_count 为 0
 * 即无协议引用的 route-map。有结果时按名字排序输出，否则打印 None。
 */
/* Unused route map details */
static int vty_show_unused_route_map(struct vty *vty, bool uj)
{
	struct list *maplist = list_new();
	struct listnode *ln;
	struct route_map *map;
	json_object *json = NULL;

	if (uj)
		json = json_object_new_object();

	for (map = route_map_master.head; map; map = map->next) {
		/* If use_count is zero, No protocol is using this routemap.
		 * so adding to the list.
		 */
		if (!map->use_count)
			listnode_add(maplist, map);
	}

	if (maplist->count > 0) {
		if (!uj)
			vty_out(vty, "\n%s:\n", frr_protonameinst);
		list_sort(maplist, sort_route_map);

		for (ALL_LIST_ELEMENTS_RO(maplist, ln, map))
			vty_show_route_map_entry(vty, map, json);
	} else {
		if (!uj)
			vty_out(vty, "\n%s: None\n", frr_protonameinst);
	}

	list_delete(&maplist);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/*
 * route_map_index_new：分配并初始化一个子句（route_map_index）。
 * 默认 exitpolicy 为 RMAP_EXIT（Cisco 风格），初始化 rhclist 队列并注册 qobj。
 */
/* New route map allocation. Please note route map's name must be
   specified. */
static struct route_map_index *route_map_index_new(void)
{
	struct route_map_index *new;

	new = XCALLOC(MTYPE_ROUTE_MAP_INDEX, sizeof(struct route_map_index));
	new->exitpolicy = RMAP_EXIT; /* Default to Cisco-style */
	TAILQ_INIT(&new->rhclist);
	QOBJ_REG(new, route_map_index);
	return new;
}

/*
 * route_map_index_delete：删除一个子句。注销 qobj、释放描述、释放 northbound hook
 * 上下文，然后逐条释放 match 规则（若规则是 IPv4 / IPv6 prefix-list，还要同步更新
 * 前缀表），再释放 set 规则，最后从所属 route-map 链表摘除并释放。
 * notify 决定是否通知依赖者。
 */
/* Free route map index. */
void route_map_index_delete(struct route_map_index *index, int notify)
{
	struct routemap_hook_context *rhc;
	struct route_map_rule *rule;

	QOBJ_UNREG(index);

	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Deleting route-map %s sequence %d",
			   index->map->name, index->pref);

	/* Free route map entry description. */
	XFREE(MTYPE_TMP, index->description);

	/* Free route map northbound hook contexts. */
	while ((rhc = TAILQ_FIRST(&index->rhclist)) != NULL)
		routemap_hook_context_free(rhc);

	/* Free route match. */
	while ((rule = index->match_list.head) != NULL) {
		if (IS_RULE_IPv4_PREFIX_LIST(rule->cmd->str))
			route_map_pfx_tbl_update(RMAP_EVENT_PLIST_DELETED,
						 index, AFI_IP, rule->rule_str);
		else if (IS_RULE_IPv6_PREFIX_LIST(rule->cmd->str))
			route_map_pfx_tbl_update(RMAP_EVENT_PLIST_DELETED,
						 index, AFI_IP6,
						 rule->rule_str);

		route_map_rule_delete(&index->match_list, rule);
	}

	/* Free route set. */
	while ((rule = index->set_list.head) != NULL)
		route_map_rule_delete(&index->set_list, rule);

	/* Remove index from route map list. */
	if (index->next)
		index->next->prev = index->prev;
	else
		index->map->tail = index->prev;

	if (index->prev)
		index->prev->next = index->next;
	else
		index->map->head = index->next;

	/* Free 'char *nextrm' if not NULL */
	XFREE(MTYPE_ROUTE_MAP_NAME, index->nextrm);

	route_map_pfx_tbl_update(RMAP_EVENT_INDEX_DELETED, index, 0, NULL);

	/* Execute event hook. */
	if (route_map_master.event_hook && notify) {
		(*route_map_master.event_hook)(index->map->name);
		route_map_notify_dependencies(index->map->name,
					      RMAP_EVENT_CALL_ADDED);
	}
	XFREE(MTYPE_ROUTE_MAP_INDEX, index);
}

/*
 * route_map_index_lookup：在 route-map 中按类型与序号查找子句。
 * type 为 RMAP_ANY 时忽略类型，只比较 pref。找到返回子句指针，否则 NULL。
 */
/* Lookup index from route map. */
static struct route_map_index *route_map_index_lookup(struct route_map *map,
						      enum route_map_type type,
						      int pref)
{
	struct route_map_index *index;

	for (index = map->head; index; index = index->next)
		if ((index->type == type || type == RMAP_ANY)
		    && index->pref == pref)
			return index;
	return NULL;
}

/*
 * route_map_index_add：新建子句并按 pref 升序插入到 route-map 子句链表的正确位置，
 * 保证匹配时从前到后遍历即为 pref 递增顺序。插入后更新前缀表并触发事件 / 依赖通知，
 * 返回新子句。
 */
/* Add new index to route map. */
static struct route_map_index *
route_map_index_add(struct route_map *map, enum route_map_type type, int pref)
{
	struct route_map_index *index;
	struct route_map_index *point;

	/* Allocate new route map inex. */
	index = route_map_index_new();
	index->map = map;
	index->type = type;
	index->pref = pref;

	/* Compare preference. */
	for (point = map->head; point; point = point->next)
		if (point->pref >= pref)
			break;

	if (map->head == NULL) {
		map->head = map->tail = index;
	} else if (point == NULL) {
		index->prev = map->tail;
		map->tail->next = index;
		map->tail = index;
	} else if (point == map->head) {
		index->next = map->head;
		map->head->prev = index;
		map->head = index;
	} else {
		index->next = point;
		index->prev = point->prev;
		if (point->prev)
			point->prev->next = index;
		point->prev = index;
	}

	route_map_pfx_tbl_update(RMAP_EVENT_INDEX_ADDED, index, 0, NULL);

	/* Execute event hook. */
	if (route_map_master.event_hook) {
		(*route_map_master.event_hook)(map->name);
		route_map_notify_dependencies(map->name, RMAP_EVENT_CALL_ADDED);
	}

	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Route-map %s add sequence %d, type: %s",
			   map->name, pref, route_map_type_str(type));

	return index;
}

/*
 * route_map_index_get：取得指定 (pref, type) 的子句；若同 pref 存在但类型不同，
 * 先删除旧子句再新建；不存在则新建。返回最终子句。
 */
/* Get route map index. */
struct route_map_index *
route_map_index_get(struct route_map *map, enum route_map_type type, int pref)
{
	struct route_map_index *index;

	index = route_map_index_lookup(map, RMAP_ANY, pref);
	if (index && index->type != type) {
		/* Delete index from route map. */
		route_map_index_delete(index, 1);
		index = NULL;
	}
	if (index == NULL)
		index = route_map_index_add(map, type, pref);
	return index;
}

/*
 * route_map_rule_new：分配并清零一个 match / set 规则结构。
 */
/* New route map rule */
static struct route_map_rule *route_map_rule_new(void)
{
	struct route_map_rule *new;

	new = XCALLOC(MTYPE_ROUTE_MAP_RULE, sizeof(struct route_map_rule));
	return new;
}

/*
 * _route_map_install_match：把 match 规则类别代理加入全局 match 命令哈希表，
 * 一般仅由 route_map_install_match 宏调用。
 */
/* Install rule command to the match list. */
void _route_map_install_match(struct route_map_rule_cmd_proxy *proxy)
{
	rmap_cmd_name_add(rmap_match_cmds, proxy);
}

/*
 * _route_map_install_set：把 set 规则类别代理加入全局 set 命令哈希表，
 * 一般仅由 route_map_install_set 宏调用。
 */
/* Install rule command to the set list. */
void _route_map_install_set(struct route_map_rule_cmd_proxy *proxy)
{
	rmap_cmd_name_add(rmap_set_cmds, proxy);
}

/*
 * route_map_lookup_match：按名字在 match 命令哈希表中查找规则类别。
 * 构造只含名字的临时 refcmd / proxy 作为查询键；找到返回 cmd，否则 NULL。
 */
/* Lookup rule command from match list. */
static const struct route_map_rule_cmd *route_map_lookup_match(const char *name)
{
	struct route_map_rule_cmd refcmd = {.str = name};
	struct route_map_rule_cmd_proxy ref = {.cmd = &refcmd};
	struct route_map_rule_cmd_proxy *res;

	res = rmap_cmd_name_find(rmap_match_cmds, &ref);
	if (res)
		return res->cmd;
	return NULL;
}

/*
 * route_map_lookup_set：按名字在 set 命令哈希表中查找规则类别，语义同上。
 */
/* Lookup rule command from set list. */
static const struct route_map_rule_cmd *route_map_lookup_set(const char *name)
{
	struct route_map_rule_cmd refcmd = {.str = name};
	struct route_map_rule_cmd_proxy ref = {.cmd = &refcmd};
	struct route_map_rule_cmd_proxy *res;

	res = rmap_cmd_name_find(rmap_set_cmds, &ref);
	if (res)
		return res->cmd;
	return NULL;
}

/*
 * route_map_rule_add：把一条规则追加到规则链表尾部。
 */
/* Add match and set rule to rule list. */
static void route_map_rule_add(struct route_map_rule_list *list,
			       struct route_map_rule *rule)
{
	rule->next = NULL;
	rule->prev = list->tail;
	if (list->tail)
		list->tail->next = rule;
	else
		list->head = rule;
	list->tail = rule;
}

/*
 * route_map_rule_delete：从规则链表摘除一条规则并释放：先调用规则类别的 func_free
 * 释放 value（若存在），再释放 rule_str 与规则结构本身。
 */
/* Delete rule from rule list. */
static void route_map_rule_delete(struct route_map_rule_list *list,
				  struct route_map_rule *rule)
{
	if (rule->cmd->func_free)
		(*rule->cmd->func_free)(rule->value);

	XFREE(MTYPE_ROUTE_MAP_RULE_STR, rule->rule_str);

	if (rule->next)
		rule->next->prev = rule->prev;
	else
		list->tail = rule->prev;
	if (rule->prev)
		rule->prev->next = rule->next;
	else
		list->head = rule->next;

	XFREE(MTYPE_ROUTE_MAP_RULE, rule);
}

/*
 * rulecmp：对 NULL 安全的 strcmp 包装。NULL 与 NULL 视为相等，NULL 与非 NULL 视为
 * 不等，其余走 strcmp。用于比较可能为空的规则参数。
 */
/* strcmp wrapper function which don't crush even argument is NULL. */
static int rulecmp(const char *dst, const char *src)
{
	if (dst == NULL) {
		if (src == NULL)
			return 0;
		else
			return 1;
	} else {
		if (src == NULL)
			return 1;
		else
			return strcmp(dst, src);
	}
	return 1;
}

/*
 * route_map_get_match_arg：返回子句中指定 match 规则已保存的参数文本（rule_str）。
 * 典型用途：删除规则时若未提供参数，可用它取回原参数以定位依赖。
 * 找不到对应规则或规则无参数时返回 NULL。
 */
/* Use this to return the already specified argument for this match. This is
 * useful to get the specified argument with a route map match rule when the
 * rule is being deleted and the argument is not provided.
 */
const char *route_map_get_match_arg(struct route_map_index *index,
				    const char *match_name)
{
	struct route_map_rule *rule;
	const struct route_map_rule_cmd *cmd;

	/* First lookup rule for add match statement. */
	cmd = route_map_lookup_match(match_name);
	if (cmd == NULL)
		return NULL;

	for (rule = index->match_list.head; rule; rule = rule->next)
		if (rule->cmd == cmd && rule->rule_str != NULL)
			return (rule->rule_str);

	return NULL;
}

/*
 * get_route_map_delete_event：把“新增 / 替换”类事件映射为对应的“删除”事件
 * （如 CALL_ADDED → CALL_DELETED、PLIST_ADDED → PLIST_DELETED）。
 * 对不应传入的事件类型会 assert(0) 中止，属于内部不变式保护。
 */
static route_map_event_t get_route_map_delete_event(route_map_event_t type)
{
	switch (type) {
	case RMAP_EVENT_CALL_ADDED:
		return RMAP_EVENT_CALL_DELETED;
	case RMAP_EVENT_PLIST_ADDED:
		return RMAP_EVENT_PLIST_DELETED;
	case RMAP_EVENT_CLIST_ADDED:
		return RMAP_EVENT_CLIST_DELETED;
	case RMAP_EVENT_ECLIST_ADDED:
		return RMAP_EVENT_ECLIST_DELETED;
	case RMAP_EVENT_LLIST_ADDED:
		return RMAP_EVENT_LLIST_DELETED;
	case RMAP_EVENT_ASLIST_ADDED:
		return RMAP_EVENT_ASLIST_DELETED;
	case RMAP_EVENT_FILTER_ADDED:
		return RMAP_EVENT_FILTER_DELETED;
	case RMAP_EVENT_SET_ADDED:
	case RMAP_EVENT_SET_DELETED:
	case RMAP_EVENT_SET_REPLACED:
	case RMAP_EVENT_MATCH_ADDED:
	case RMAP_EVENT_MATCH_DELETED:
	case RMAP_EVENT_MATCH_REPLACED:
	case RMAP_EVENT_INDEX_ADDED:
	case RMAP_EVENT_INDEX_DELETED:
	case RMAP_EVENT_CALL_DELETED:
	case RMAP_EVENT_PLIST_DELETED:
	case RMAP_EVENT_CLIST_DELETED:
	case RMAP_EVENT_ECLIST_DELETED:
	case RMAP_EVENT_LLIST_DELETED:
	case RMAP_EVENT_ASLIST_DELETED:
	case RMAP_EVENT_FILTER_DELETED:
		/* This function returns the appropriate 'deleted' event type
		 * for every 'added' event type passed to this function.
		 * This is done only for named entities used in the
		 * route-map match commands.
		 * This function is not to be invoked for any of the other event
		 * types.
		 */
		assert(0);
	}

	assert(0);
	/*
	 * Return to make c happy but if we get here something has gone
	 * terribly terribly wrong, so yes this return makes no sense.
	 */
	return RMAP_EVENT_CALL_ADDED;
}

/*
 * route_map_add_match：向子句添加 / 替换一条 match 规则，是配置解析的核心。
 * 流程：按名查得规则类别；若类别有 func_compile 则编译 match_arg 得其 value；
 * 若已有相同规则（按 rule_key 或 cmd 判定）则先删除旧的；再把新规则加入
 * match_list。期间可能更新 prefix-list 前缀表并触发 PLIST 等依赖事件。
 * 返回编译结果；查不到规则类别返回 RMAP_RULE_MISSING。
 */
/* Add match statement to route map. */
enum rmap_compile_rets route_map_add_match(struct route_map_index *index,
					   const char *match_name,
					   const char *match_arg,
					   route_map_event_t type)
{
	struct route_map_rule *rule;
	struct route_map_rule *next;
	const struct route_map_rule_cmd *cmd;
	void *compile;
	int8_t delete_rmap_event_type = 0;
	const char *rule_key;

	/* First lookup rule for add match statement. */
	cmd = route_map_lookup_match(match_name);
	if (cmd == NULL)
		return RMAP_RULE_MISSING;

	/* Next call compile function for this match statement. */
	if (cmd->func_compile) {
		compile = (*cmd->func_compile)(match_arg);
		if (compile == NULL)
			return RMAP_COMPILE_ERROR;
	} else
		compile = NULL;
	/* use the compiled results if applicable */
	if (compile && cmd->func_get_rmap_rule_key)
		rule_key = (*cmd->func_get_rmap_rule_key)
			   (compile);
	else
		rule_key = match_arg;

	/* If argument is completely same ignore it. */
	for (rule = index->match_list.head; rule; rule = next) {
		next = rule->next;
		if (rule->cmd == cmd) {
			/* If the configured route-map match rule is exactly
			 * the same as the existing configuration then,
			 * ignore the duplicate configuration.
			 */
			if (rulecmp(match_arg, rule->rule_str) == 0) {
				if (cmd->func_free)
					(*cmd->func_free)(compile);

				return RMAP_COMPILE_SUCCESS;
			}

			/* If IPv4 or IPv6 prefix-list match criteria
			 * has been delete to the route-map index, update
			 * the route-map's prefix table.
			 */
			if (IS_RULE_IPv4_PREFIX_LIST(match_name))
				route_map_pfx_tbl_update(
					RMAP_EVENT_PLIST_DELETED, index, AFI_IP,
					rule->rule_str);
			else if (IS_RULE_IPv6_PREFIX_LIST(match_name))
				route_map_pfx_tbl_update(
					RMAP_EVENT_PLIST_DELETED, index,
					AFI_IP6, rule->rule_str);

			/* Remove the dependency of the route-map on the rule
			 * that is being replaced.
			 */
			if (type >= RMAP_EVENT_CALL_ADDED) {
				delete_rmap_event_type =
					get_route_map_delete_event(type);
				route_map_upd8_dependency(
							delete_rmap_event_type,
							rule->rule_str,
							index->map->name);
			}

			route_map_rule_delete(&index->match_list, rule);
		}
	}

	/* Add new route map match rule. */
	rule = route_map_rule_new();
	rule->cmd = cmd;
	rule->value = compile;
	if (match_arg)
		rule->rule_str = XSTRDUP(MTYPE_ROUTE_MAP_RULE_STR, match_arg);
	else
		rule->rule_str = NULL;

	/* Add new route match rule to linked list. */
	route_map_rule_add(&index->match_list, rule);

	/* If IPv4 or IPv6 prefix-list match criteria
	 * has been added to the route-map index, update
	 * the route-map's prefix table.
	 */
	if (IS_RULE_IPv4_PREFIX_LIST(match_name)) {
		route_map_pfx_tbl_update(RMAP_EVENT_PLIST_ADDED, index, AFI_IP,
					 match_arg);
	} else if (IS_RULE_IPv6_PREFIX_LIST(match_name)) {
		route_map_pfx_tbl_update(RMAP_EVENT_PLIST_ADDED, index, AFI_IP6,
					 match_arg);
	}

	/* Execute event hook. */
	if (route_map_master.event_hook) {
		(*route_map_master.event_hook)(index->map->name);
		route_map_notify_dependencies(index->map->name,
					      RMAP_EVENT_CALL_ADDED);
	}
	if (type != RMAP_EVENT_MATCH_ADDED)
		route_map_upd8_dependency(type, rule_key, index->map->name);

	return RMAP_COMPILE_SUCCESS;
}

/*
 * route_map_delete_match：删除子句中匹配指定名字（及可选参数）的 match 规则。
 * match_arg 为 NULL 时按规则类别匹配，否则还需 rule_str 相等。删除前会按需取
 * rule_key 并登记删除依赖事件，删除后若涉及 prefix-list 则同步更新前缀表。
 * 找到并删除返回 RMAP_COMPILE_SUCCESS，否则 RMAP_RULE_MISSING。
 */
/* Delete specified route match rule. */
enum rmap_compile_rets route_map_delete_match(struct route_map_index *index,
					      const char *match_name,
					      const char *match_arg,
					      route_map_event_t type)
{
	struct route_map_rule *rule;
	const struct route_map_rule_cmd *cmd;
	const char *rule_key;

	cmd = route_map_lookup_match(match_name);
	if (cmd == NULL)
		return RMAP_RULE_MISSING;

	for (rule = index->match_list.head; rule; rule = rule->next)
		if (rule->cmd == cmd && (rulecmp(rule->rule_str, match_arg) == 0
					 || match_arg == NULL)) {
			/* Execute event hook. */
			if (route_map_master.event_hook) {
				(*route_map_master.event_hook)(index->map->name);
				route_map_notify_dependencies(
					index->map->name,
					RMAP_EVENT_CALL_ADDED);
			}
			if (cmd->func_get_rmap_rule_key)
				rule_key = (*cmd->func_get_rmap_rule_key)
					   (rule->value);
			else
				rule_key = match_arg;

			if (type != RMAP_EVENT_MATCH_DELETED && rule_key)
				route_map_upd8_dependency(type, rule_key,
						index->map->name);

			route_map_rule_delete(&index->match_list, rule);

			/* If IPv4 or IPv6 prefix-list match criteria
			 * has been delete from the route-map index, update
			 * the route-map's prefix table.
			 */
			if (IS_RULE_IPv4_PREFIX_LIST(match_name)) {
				route_map_pfx_tbl_update(
					RMAP_EVENT_PLIST_DELETED, index, AFI_IP,
					match_arg);
			} else if (IS_RULE_IPv6_PREFIX_LIST(match_name)) {
				route_map_pfx_tbl_update(
					RMAP_EVENT_PLIST_DELETED, index,
					AFI_IP6, match_arg);
			}

			return RMAP_COMPILE_SUCCESS;
		}
	/* Can't find matched rule. */
	return RMAP_RULE_MISSING;
}

/*
 * route_map_add_set：向子句添加一条 set 规则。同一子句内同类别（cmd）的 set 规则
 * 只允许一条，因此先删除已有的同类别规则再插入新规则（保证“设置”唯一）。
 * 若有 func_compile 则编译 set_arg，失败返回 RMAP_COMPILE_ERROR。
 * 返回 RMAP_COMPILE_SUCCESS；查不到 set 类别返回 RMAP_RULE_MISSING。
 */
/* Add route-map set statement to the route map. */
enum rmap_compile_rets route_map_add_set(struct route_map_index *index,
					 const char *set_name,
					 const char *set_arg)
{
	struct route_map_rule *rule;
	struct route_map_rule *next;
	const struct route_map_rule_cmd *cmd;
	void *compile;

	cmd = route_map_lookup_set(set_name);
	if (cmd == NULL)
		return RMAP_RULE_MISSING;

	/* Next call compile function for this match statement. */
	if (cmd->func_compile) {
		compile = (*cmd->func_compile)(set_arg);
		if (compile == NULL)
			return RMAP_COMPILE_ERROR;
	} else
		compile = NULL;

	/* Add by WJL. if old set command of same kind exist, delete it first
	   to ensure only one set command of same kind exist under a
	   route_map_index. */
	for (rule = index->set_list.head; rule; rule = next) {
		next = rule->next;
		if (rule->cmd == cmd)
			route_map_rule_delete(&index->set_list, rule);
	}

	/* Add new route map match rule. */
	rule = route_map_rule_new();
	rule->cmd = cmd;
	rule->value = compile;
	if (set_arg)
		rule->rule_str = XSTRDUP(MTYPE_ROUTE_MAP_RULE_STR, set_arg);
	else
		rule->rule_str = NULL;

	/* Add new route match rule to linked list. */
	route_map_rule_add(&index->set_list, rule);

	/* Execute event hook. */
	if (route_map_master.event_hook) {
		(*route_map_master.event_hook)(index->map->name);
		route_map_notify_dependencies(index->map->name,
					      RMAP_EVENT_CALL_ADDED);
	}
	return RMAP_COMPILE_SUCCESS;
}

/*
 * route_map_delete_set：删除子句中匹配指定名字（及可选参数）的 set 规则，
 * 找到即删除并返回 RMAP_COMPILE_SUCCESS，否则 RMAP_RULE_MISSING。
 */
/* Delete route map set rule. */
enum rmap_compile_rets route_map_delete_set(struct route_map_index *index,
					    const char *set_name,
					    const char *set_arg)
{
	struct route_map_rule *rule;
	const struct route_map_rule_cmd *cmd;

	cmd = route_map_lookup_set(set_name);
	if (cmd == NULL)
		return RMAP_RULE_MISSING;

	for (rule = index->set_list.head; rule; rule = rule->next)
		if ((rule->cmd == cmd) && (rulecmp(rule->rule_str, set_arg) == 0
					   || set_arg == NULL)) {
			route_map_rule_delete(&index->set_list, rule);
			/* Execute event hook. */
			if (route_map_master.event_hook) {
				(*route_map_master.event_hook)(index->map->name);
				route_map_notify_dependencies(
					index->map->name,
					RMAP_EVENT_CALL_ADDED);
			}
			return RMAP_COMPILE_SUCCESS;
		}
	/* Can't find matched rule. */
	return RMAP_RULE_MISSING;
}

static enum route_map_cmd_result_t
/*
 * route_map_apply_match：对一个子句的整条 match_list 求值，返回合并后的结果。
 * 规则：无 match 规则时视为 MATCH（空 match 等价于全部通过）；逐条调用规则的
 * func_apply，任一返回 NOMATCH 即整个子句不匹配；NOOP 表示该条件不适用，
 * 若此前已有其它条件 MATCH 则仍算 MATCH；全部为 NOOP 则结果为 NOOP。
 * 返回 RMAP_MATCH / RMAP_NOMATCH / RMAP_NOOP。
 */
route_map_apply_match(struct route_map_rule_list *match_list,
		      const struct prefix *prefix, void *object)
{
	enum route_map_cmd_result_t ret = RMAP_NOMATCH;
	struct route_map_rule *match;
	bool is_matched = false;


	/* Check all match rule and if there is no match rule, go to the
	   set statement. */
	if (!match_list->head)
		ret = RMAP_MATCH;
	else {
		for (match = match_list->head; match; match = match->next) {
			/*
			 * Try each match statement. If any match does not
			 * return RMAP_MATCH or RMAP_NOOP, return.
			 * Otherwise continue on to next match statement.
			 * All match statements must MATCH for
			 * end-result to be a match.
			 * (Exception:If match stmts result in a mix of
			 * MATCH/NOOP, then also end-result is a match)
			 * If all result in NOOP, end-result is NOOP.
			 */
			ret = (*match->cmd->func_apply)(match->value, prefix,
							object);

			/*
			 * If the consolidated result of func_apply is:
			 *   -----------------------------------------------
			 *   |  MATCH  | NOMATCH  |  NOOP   |  Final Result |
			 *   ------------------------------------------------
			 *   |   yes   |   yes    |  yes    |     NOMATCH   |
			 *   |   no    |   no     |  yes    |     NOOP      |
			 *   |   yes   |   no     |  yes    |     MATCH     |
			 *   |   no    |   yes    |  yes    |     NOMATCH   |
			 *   |-----------------------------------------------
			 *
			 *  Traditionally, all rules within route-map
			 *  should match for it to MATCH.
			 *  If there are noops within the route-map rules,
			 *  it follows the above matrix.
			 *
			 *   Eg: route-map rm1 permit 10
			 *         match rule1
			 *         match rule2
			 *         match rule3
			 *         ....
			 *       route-map rm1 permit 20
			 *         match ruleX
			 *         match ruleY
			 *         ...
			 */

			switch (ret) {
			case RMAP_MATCH:
				is_matched = true;
				break;

			case RMAP_NOMATCH:
				return ret;

			case RMAP_NOOP:
				if (is_matched)
					ret = RMAP_MATCH;
				break;

			case RMAP_OKAY:
			case RMAP_ERROR:
				break;
			}

		}
	}
	return ret;
}

/*
 * route_map_get_index_list：在前缀表（table）中查找与 prefix 匹配的节点，
 * 沿节点向上回溯到第一个带 info（即子句链表）的节点并返回该链表。
 * rn 为入 / 出参：首次传入 *rn 为空，函数会做匹配并加锁；命中父节点时返回其 info。
 * 找不到返回 NULL。坑：涉及 route_node 的 lock / unlock，调用方需注意 *rn 的生命周期。
 */
static struct list *route_map_get_index_list(struct route_node **rn,
					     const struct prefix *prefix,
					     struct route_table *table)
{
	struct route_node *tmp_rn = NULL;

	if (!(*rn)) {
		*rn = route_node_match(table, prefix);

		if (!(*rn))
			return NULL;

		if ((*rn)->info)
			return (struct list *)((*rn)->info);

		/* If rn->info is NULL, get the parent.
		 * Store the rn in tmp_rn and unlock it later.
		 */
		tmp_rn = *rn;
	}

	do {
		*rn = (*rn)->parent;
		if (tmp_rn)
			route_unlock_node(tmp_rn);

		if (!(*rn))
			break;

		if ((*rn)->info) {
			route_lock_node(*rn);
			return (struct list *)((*rn)->info);
		}
	} while (!(*rn)->info);

	return NULL;
}

/*
 * route_map_get_index：为 prefix / object 找出“最佳匹配”的子句，是匹配优化的核心。
 * 若禁用优化则直接返回 map->head（线性从头扫）。否则按 address family 选择 IPv4 /
 * IPv6 前缀表，用 LPM（最长前缀匹配）逐级向上找到候选子句链表，取其中 pref 最小且
 * match 命中的子句作为 best_index；同时用 match_ret 回传合并后的匹配结果（需考虑
 * NOOP 与已有 best_index 的情形）。返回 best_index（可能为 NULL）。
 * 坑：过程中对 route_node 有 lock / unlock 配对，各分支必须正确解锁。
 */
/*
 * This function returns the route-map index that best matches the prefix.
 */
static struct route_map_index *
route_map_get_index(struct route_map *map, const struct prefix *prefix,
		    void *object, enum route_map_cmd_result_t *match_ret)
{
	enum route_map_cmd_result_t ret = RMAP_NOMATCH;
	struct list *candidate_rmap_list = NULL;
	struct route_node *rn = NULL;
	struct listnode *ln = NULL, *nn = NULL;
	struct route_map_index *index = NULL, *best_index = NULL;
	struct route_map_index *head_index = NULL;
	struct route_table *table = NULL;

	/* Route-map optimization relies on LPM lookups of the prefix to reduce
	 * the amount of route-map clauses a given prefix needs to be processed
	 * against. These LPM trees are IPv4/IPv6-specific and prefix->family
	 * must be AF_INET or AF_INET6 in order for the lookup to succeed. So if
	 * the AF doesn't line up with the LPM trees, skip the optimization.
	 */
	if (map->optimization_disabled) {
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP_DETAIL)))
			zlog_debug(
				"Skipping route-map optimization for route-map: %s, pfx: %pFX, family: %d",
				map->name, prefix, prefix->family);
		return map->head;
	}

	if (prefix->family == AF_INET)
		table = map->ipv4_prefix_table;
	else
		table = map->ipv6_prefix_table;

	do {
		candidate_rmap_list =
			route_map_get_index_list(&rn, prefix, table);
		if (!rn)
			break;

		/* If the index at the head of the list is of seq higher
		 * than that in best_index, ignore the list and get the
		 * parent node's list.
		 */
		head_index = (struct route_map_index *)(listgetdata(
			listhead(candidate_rmap_list)));
		if (best_index && head_index
		    && (best_index->pref < head_index->pref)) {
			route_unlock_node(rn);
			continue;
		}

		for (ALL_LIST_ELEMENTS(candidate_rmap_list, ln, nn, index)) {
			/* If the index is of seq higher than that in
			 * best_index, ignore the list and get the parent
			 * node's list.
			 */
			if (best_index && (best_index->pref < index->pref))
				break;

			ret = route_map_apply_match(&index->match_list, prefix,
						    object);

			if (ret == RMAP_MATCH) {
				*match_ret = ret;
				best_index = index;
				break;
			} else if (ret == RMAP_NOOP) {
				/*
				 * If match_ret is denymatch, even if we see
				 * more noops, we retain this return value and
				 * return this eventually if there are no
				 * matches.
				 * If a best match route-map index already
				 * exists, do not reset the match_ret.
				 */
				if (!best_index && (*match_ret != RMAP_NOMATCH))
					*match_ret = ret;
			} else {
				/*
				 * ret is RMAP_NOMATCH.
				 * If a best match route-map index already
				 * exists, do not reset the match_ret.
				 */
				if (!best_index)
					*match_ret = ret;
			}
		}

		route_unlock_node(rn);

	} while (rn);

	return best_index;
}

/*
 * route_map_candidate_list_cmp：候选子句链表的排序比较函数，按子句 pref 升序。
 */
static int route_map_candidate_list_cmp(struct route_map_index *idx1,
					struct route_map_index *idx2)
{
	return idx1->pref - idx2->pref;
}

/*
 * route_map_pfx_table_add_default：把一个子句加入 route-map 前缀表的“默认路由”
 * 节点（prefixlen 0）。当某子句引用的 prefix-list 尚不存在时，它应匹配所有前缀，
 * 故挂在默认节点。节点无 info 时新建候选链表（按 pref 排序去重）并挂上。
 */
/*
 * This function adds the route-map index into the default route's
 * route-node in the route-map's IPv4/IPv6 prefix-table.
 */
static void route_map_pfx_table_add_default(afi_t afi,
					    struct route_map_index *index)
{
	struct route_node *rn = NULL;
	struct list *rmap_candidate_list = NULL;
	struct prefix p;
	bool updated_rn = false;
	struct route_table *table = NULL;

	memset(&p, 0, sizeof(p));
	p.family = afi2family(afi);
	p.prefixlen = 0;

	if (p.family == AF_INET)
		table = index->map->ipv4_prefix_table;
	else
		table = index->map->ipv6_prefix_table;

	/* Add default route to table */
	rn = route_node_get(table, &p);

	if (!rn)
		return;

	if (!rn->info) {
		rmap_candidate_list = list_new();
		rmap_candidate_list->cmp =
			(int (*)(void *, void *))route_map_candidate_list_cmp;
		rn->info = rmap_candidate_list;
	} else {
		rmap_candidate_list = (struct list *)rn->info;
		updated_rn = true;
	}

	listnode_add_sort_nodup(rmap_candidate_list, index);
	if (updated_rn)
		route_unlock_node(rn);
}

/*
 * route_map_pfx_table_del_default：从前缀表的默认路由节点移除一个子句；
 * 节点候选链表清空后释放链表并清空 rn->info。
 */
/*
 * This function removes the route-map index from the default route's
 * route-node in the route-map's IPv4/IPv6 prefix-table.
 */
static void route_map_pfx_table_del_default(afi_t afi,
					    struct route_map_index *index)
{
	struct route_node *rn = NULL;
	struct list *rmap_candidate_list = NULL;
	struct prefix p;
	struct route_table *table = NULL;

	memset(&p, 0, sizeof(p));
	p.family = afi2family(afi);
	p.prefixlen = 0;

	if (p.family == AF_INET)
		table = index->map->ipv4_prefix_table;
	else
		table = index->map->ipv6_prefix_table;

	/* Remove RMAP index from default route in table */
	rn = route_node_lookup(table, &p);
	if (!rn || !rn->info)
		return;

	rmap_candidate_list = (struct list *)rn->info;

	listnode_delete(rmap_candidate_list, index);

	if (listcount(rmap_candidate_list) == 0) {
		list_delete(&rmap_candidate_list);
		rn->info = NULL;
		route_unlock_node(rn);
	}
	route_unlock_node(rn);
}

/*
 * route_map_pfx_table_add：把子句 index 加入前缀表 table 中 pentry->prefix 对应的
 * 路由节点（候选链表按 pref 排序去重）。用于 prefix-list 中某条 entry 变化时维护
 * 前缀表。
 */
/*
 * This function adds the route-map index to the route-node for
 * the prefix-entry in the route-map's IPv4/IPv6 prefix-table.
 */
static void route_map_pfx_table_add(struct route_table *table,
				    struct route_map_index *index,
				    struct prefix_list_entry *pentry)
{
	struct route_node *rn = NULL;
	struct list *rmap_candidate_list = NULL;
	bool updated_rn = false;

	rn = route_node_get(table, &pentry->prefix);
	if (!rn)
		return;

	if (!rn->info) {
		rmap_candidate_list = list_new();
		rmap_candidate_list->cmp =
			(int (*)(void *, void *))route_map_candidate_list_cmp;
		rn->info = rmap_candidate_list;
	} else {
		rmap_candidate_list = (struct list *)rn->info;
		updated_rn = true;
	}

	listnode_add_sort_nodup(rmap_candidate_list, index);
	if (updated_rn)
		route_unlock_node(rn);
}

/*
 * route_map_pfx_table_del：从前缀表 table 中 pentry->prefix 对应的路由节点移除
 * 子句 index；节点候选链表清空后释放。
 */
/*
 * This function removes the route-map index from the route-node for
 * the prefix-entry in the route-map's IPv4/IPv6 prefix-table.
 */
static void route_map_pfx_table_del(struct route_table *table,
				    struct route_map_index *index,
				    struct prefix_list_entry *pentry)
{
	struct route_node *rn = NULL;
	struct list *rmap_candidate_list = NULL;

	rn = route_node_lookup(table, &pentry->prefix);
	if (!rn || !rn->info)
		return;

	rmap_candidate_list = (struct list *)rn->info;

	listnode_delete(rmap_candidate_list, index);

	if (listcount(rmap_candidate_list) == 0) {
		list_delete(&rmap_candidate_list);
		rn->info = NULL;
		route_unlock_node(rn);
	}
	route_unlock_node(rn);
}

/*
 * route_map_is_ip_pfx_list_rule_present：检查子句的 match_list 中是否存在 IPv4
 * prefix-list 规则，存在返回 true。
 */
/* This function checks for the presence of an IPv4 prefix-list
 * match rule in the given route-map index.
 */
static bool route_map_is_ip_pfx_list_rule_present(struct route_map_index *index)
{
	struct route_map_rule_list *match_list = NULL;
	struct route_map_rule *rule = NULL;

	match_list = &index->match_list;
	for (rule = match_list->head; rule; rule = rule->next)
		if (IS_RULE_IPv4_PREFIX_LIST(rule->cmd->str))
			return true;

	return false;
}

/*
 * route_map_is_ipv6_pfx_list_rule_present：检查子句的 match_list 中是否存在 IPv6
 * prefix-list 规则，存在返回 true。
 */
/* This function checks for the presence of an IPv6 prefix-list
 * match rule in the given route-map index.
 */
static bool
route_map_is_ipv6_pfx_list_rule_present(struct route_map_index *index)
{
	struct route_map_rule_list *match_list = NULL;
	struct route_map_rule *rule = NULL;

	match_list = &index->match_list;
	for (rule = match_list->head; rule; rule = rule->next)
		if (IS_RULE_IPv6_PREFIX_LIST(rule->cmd->str))
			return true;

	return false;
}

/*
 * route_map_add_plist_entries：维护前缀表的“添加”侧，具体步骤见函数上方原注释：
 * 未给 plist_name 时先从子句反查 prefix-list 名；找到 prefix-list 则把子句从默认
 * 节点移除并为其每条 entry 建路由节点挂入；找不到则挂在默认节点。entry 非空时只
 * 处理该 entry，否则遍历整个 prefix-list。
 */
/* This function does the following:
 * 1) If plist_name is not present, search for a IPv4 or IPv6 prefix-list
 *    match clause (based on the afi passed to this foo) and get the
 *    prefix-list name.
 * 2) Look up the prefix-list using the name.
 * 3) If the prefix-list is not found then, add the index to the IPv4/IPv6
 *    default-route's node in the trie (based on the afi passed to this foo).
 * 4) If the prefix-list is found then, remove the index from the IPv4/IPv6
 *    default-route's node in the trie (based on the afi passed to this foo).
 * 5) If a prefix-entry is passed then, create a route-node for this entry and
 *    add this index to the route-node.
 * 6) If prefix-entry is not passed then, for every prefix-entry in the
 *    prefix-list, create a route-node for this entry and
 *    add this index to the route-node.
 */
static void route_map_add_plist_entries(afi_t afi,
					struct route_map_index *index,
					const char *plist_name,
					struct prefix_list_entry *entry)
{
	struct route_map_rule_list *match_list = NULL;
	struct route_map_rule *match = NULL;
	struct prefix_list *plist = NULL;
	struct prefix_list_entry *pentry = NULL;
	bool plist_rule_is_present = false;

	if (!plist_name) {
		match_list = &index->match_list;

		for (match = match_list->head; match; match = match->next) {
			if (afi == AFI_IP) {
				if (IS_RULE_IPv4_PREFIX_LIST(match->cmd->str)) {
					plist_rule_is_present = true;
					break;
				}
			} else {
				if (IS_RULE_IPv6_PREFIX_LIST(match->cmd->str)) {
					plist_rule_is_present = true;
					break;
				}
			}
		}

		if (plist_rule_is_present)
			plist = prefix_list_lookup(afi, match->rule_str);
	} else {
		plist = prefix_list_lookup(afi, plist_name);
	}

	if (!plist) {
		route_map_pfx_table_add_default(afi, index);
		return;
	}

	/* Default entry should be deleted only if the first entry of the
	 * prefix-list is created.
	 */
	if (entry) {
		if (plist->count == 1)
			route_map_pfx_table_del_default(afi, index);
	} else {
		route_map_pfx_table_del_default(afi, index);
	}

	if (entry) {
		if (afi == AFI_IP) {
			route_map_pfx_table_add(index->map->ipv4_prefix_table,
						index, entry);
		} else {
			route_map_pfx_table_add(index->map->ipv6_prefix_table,
						index, entry);
		}
	} else {
		for (pentry = plist->head; pentry; pentry = pentry->next) {
			if (afi == AFI_IP) {
				route_map_pfx_table_add(
					index->map->ipv4_prefix_table, index,
					pentry);
			} else {
				route_map_pfx_table_add(
					index->map->ipv6_prefix_table, index,
					pentry);
			}
		}
	}
}

/*
 * route_map_del_plist_entries：维护前缀表的“删除”侧，是 route_map_add_plist_entries
 * 的逆操作：未给 plist_name 时从子句反查 prefix-list 名；找不到 prefix-list 则把
 * 子句从默认节点移除；找到则把子句从其各 entry 对应的路由节点移除。
 */
/* This function does the following:
 * 1) If plist_name is not present, search for a IPv4 or IPv6 prefix-list
 *    match clause (based on the afi passed to this foo) and get the
 *    prefix-list name.
 * 2) Look up the prefix-list using the name.
 * 3) If the prefix-list is not found then, delete the index from the IPv4/IPv6
 *    default-route's node in the trie (based on the afi passed to this foo).
 * 4) If a prefix-entry is passed then, remove this index from the route-node
 *    for the prefix in this prefix-entry.
 * 5) If prefix-entry is not passed then, for every prefix-entry in the
 *    prefix-list, remove this index from the route-node
 *    for the prefix in this prefix-entry.
 */
static void route_map_del_plist_entries(afi_t afi,
					struct route_map_index *index,
					const char *plist_name,
					struct prefix_list_entry *entry)
{
	struct route_map_rule_list *match_list = NULL;
	struct route_map_rule *match = NULL;
	struct prefix_list *plist = NULL;
	struct prefix_list_entry *pentry = NULL;
	bool plist_rule_is_present = false;

	if (!plist_name) {
		match_list = &index->match_list;

		for (match = match_list->head; match; match = match->next) {
			if (afi == AFI_IP) {
				if (IS_RULE_IPv4_PREFIX_LIST(match->cmd->str)) {
					plist_rule_is_present = true;
					break;
				}
			} else {
				if (IS_RULE_IPv6_PREFIX_LIST(match->cmd->str)) {
					plist_rule_is_present = true;
					break;
				}
			}
		}

		if (plist_rule_is_present)
			plist = prefix_list_lookup(afi, match->rule_str);
	} else {
		plist = prefix_list_lookup(afi, plist_name);
	}

	if (!plist) {
		route_map_pfx_table_del_default(afi, index);
		return;
	}

	if (entry) {
		if (afi == AFI_IP) {
			route_map_pfx_table_del(index->map->ipv4_prefix_table,
						index, entry);
		} else {
			route_map_pfx_table_del(index->map->ipv6_prefix_table,
						index, entry);
		}
	} else {
		for (pentry = plist->head; pentry; pentry = pentry->next) {
			if (afi == AFI_IP) {
				route_map_pfx_table_del(
					index->map->ipv4_prefix_table, index,
					pentry);
			} else {
				route_map_pfx_table_del(
					index->map->ipv6_prefix_table, index,
					pentry);
			}
		}
	}
}

/*
 * route_map_trie_update：处理“某个 match 命令增删了 prefix-list”的情形，据此更新
 * route-map 的前缀表。因为同一子句可能同时有 IPv4 / IPv6 前缀列表规则，需要交错
 * 处理以免优化树不一致（IPv4 变化会影响 IPv6 默认项，反之亦然）。
 */
/*
 * This function handles the cases where a prefix-list is added/removed
 * as a match command from a particular route-map index.
 * It updates the prefix-table of the route-map accordingly.
 */
static void route_map_trie_update(afi_t afi, route_map_event_t event,
				  struct route_map_index *index,
				  const char *plist_name)
{
	if (event == RMAP_EVENT_PLIST_ADDED) {
		if (afi == AFI_IP) {
			if (!route_map_is_ipv6_pfx_list_rule_present(index)) {
				route_map_pfx_table_del_default(AFI_IP6, index);
				route_map_add_plist_entries(afi, index,
							    plist_name, NULL);
			} else {
				route_map_del_plist_entries(AFI_IP6, index,
							    NULL, NULL);
			}
		} else {
			if (!route_map_is_ip_pfx_list_rule_present(index)) {
				route_map_pfx_table_del_default(AFI_IP, index);
				route_map_add_plist_entries(afi, index,
							    plist_name, NULL);
			} else {
				route_map_del_plist_entries(AFI_IP, index, NULL,
							    NULL);
			}
		}
	} else if (event == RMAP_EVENT_PLIST_DELETED) {
		if (afi == AFI_IP) {
			route_map_del_plist_entries(afi, index, plist_name,
						    NULL);

			/* If IPv6 prefix-list match rule is not present,
			 * add this index to the IPv4 default route's trie
			 * node.
			 * Also, add this index to the trie nodes created
			 * for each of the prefix-entries within the IPv6
			 * prefix-list, if the IPv6 prefix-list match rule
			 * is present. Else, add this index to the IPv6
			 * default route's trie node.
			 */
			if (!route_map_is_ipv6_pfx_list_rule_present(index))
				route_map_pfx_table_add_default(afi, index);

			route_map_add_plist_entries(AFI_IP6, index, NULL, NULL);
		} else {
			route_map_del_plist_entries(afi, index, plist_name,
						    NULL);

			/* If IPv4 prefix-list match rule is not present,
			 * add this index to the IPv6 default route's trie
			 * node.
			 * Also, add this index to the trie nodes created
			 * for each of the prefix-entries within the IPv4
			 * prefix-list, if the IPv4 prefix-list match rule
			 * is present. Else, add this index to the IPv4
			 * default route's trie node.
			 */
			if (!route_map_is_ip_pfx_list_rule_present(index))
				route_map_pfx_table_add_default(afi, index);

			route_map_add_plist_entries(AFI_IP, index, NULL, NULL);
		}
	}
}

/*
 * route_map_pfx_tbl_update：前缀表更新的总入口，按事件类型分派：
 * INDEX_ADDED / INDEX_DELETED 直接增删两个 family 的默认项；其余（PLIST 增删）
 * 转交 route_map_trie_update。index 为 NULL 时直接返回。
 */
/*
 * This function handles the cases where a route-map index and
 * prefix-list is added/removed.
 * It updates the prefix-table of the route-map accordingly.
 */
static void route_map_pfx_tbl_update(route_map_event_t event,
				     struct route_map_index *index, afi_t afi,
				     const char *plist_name)
{
	if (!index)
		return;

	if (event == RMAP_EVENT_INDEX_ADDED) {
		route_map_pfx_table_add_default(AFI_IP, index);
		route_map_pfx_table_add_default(AFI_IP6, index);
		return;
	}

	if (event == RMAP_EVENT_INDEX_DELETED) {
		route_map_pfx_table_del_default(AFI_IP, index);
		route_map_pfx_table_del_default(AFI_IP6, index);

		return;
	}

	/* Handle prefix-list match rule addition/deletion.
	 */
	route_map_trie_update(afi, event, index, plist_name);
}

/*
 * route_map_pentry_update：处理 prefix-list 中“单条 entry 增删”对前缀表的影响。
 * 按 entry 的 family 决定 afi：新增时把子句挂到该 entry 对应节点；删除时移除，
 * 若该 prefix-list 已空或仅剩一条，则回退为挂在默认节点。
 */
/*
 * This function handles the cases where a new prefix-entry is added to
 * a prefix-list or, an existing prefix-entry is removed from the prefix-list.
 * It updates the prefix-table of the route-map accordingly.
 */
static void route_map_pentry_update(route_map_event_t event,
				    const char *plist_name,
				    struct route_map_index *index,
				    struct prefix_list_entry *pentry)
{
	struct prefix_list *plist = NULL;
	afi_t afi;
	unsigned char family = pentry->prefix.family;

	if (family == AF_INET) {
		afi = AFI_IP;
		plist = prefix_list_lookup(AFI_IP, plist_name);
	} else {
		afi = AFI_IP6;
		plist = prefix_list_lookup(AFI_IP6, plist_name);
	}

	if (event == RMAP_EVENT_PLIST_ADDED) {
		if (afi == AFI_IP) {
			if (!route_map_is_ipv6_pfx_list_rule_present(index))
				route_map_add_plist_entries(afi, index,
							    plist_name, pentry);
		} else {
			if (!route_map_is_ip_pfx_list_rule_present(index))
				route_map_add_plist_entries(afi, index,
							    plist_name, pentry);
		}
	} else if (event == RMAP_EVENT_PLIST_DELETED) {
		route_map_del_plist_entries(afi, index, plist_name, pentry);

		if (plist == NULL || plist->count == 1) {
			if (afi == AFI_IP) {
				if (!route_map_is_ipv6_pfx_list_rule_present(
					    index))
					route_map_pfx_table_add_default(afi,
									index);
			} else {
				if (!route_map_is_ip_pfx_list_rule_present(
					    index))
					route_map_pfx_table_add_default(afi,
									index);
			}
		}
	}
}

/*
 * route_map_pentry_process_dependency：哈希遍历回调。对依赖某个 prefix-list 的每个
 * route-map（dep_data），遍历其所有子句的 match 规则，凡是引用同一 prefix-list 名
 * 且 family 匹配的，就对该 entry 的变化调用 route_map_pentry_update 更新前缀表。
 */
static void route_map_pentry_process_dependency(struct hash_bucket *bucket,
						void *data)
{
	char *rmap_name = NULL;
	struct route_map *rmap = NULL;
	struct route_map_index *index = NULL;
	struct route_map_rule_list *match_list = NULL;
	struct route_map_rule *match = NULL;
	struct route_map_dep_data *dep_data = NULL;
	struct route_map_pentry_dep *pentry_dep =
		(struct route_map_pentry_dep *)data;
	unsigned char family = pentry_dep->pentry->prefix.family;

	dep_data = (struct route_map_dep_data *)bucket->data;
	if (!dep_data)
		return;

	rmap_name = dep_data->rname;
	rmap = route_map_lookup_by_name(rmap_name);
	if (!rmap || !rmap->head)
		return;

	for (index = rmap->head; index; index = index->next) {
		match_list = &index->match_list;

		if (!match_list)
			continue;

		for (match = match_list->head; match; match = match->next) {
			if (rulecmp(match->rule_str, pentry_dep->plist_name) == 0) {
				if (IS_RULE_IPv4_PREFIX_LIST(match->cmd->str)
				    && family == AF_INET) {
					route_map_pentry_update(
						pentry_dep->event,
						pentry_dep->plist_name, index,
						pentry_dep->pentry);
				} else if (IS_RULE_IPv6_PREFIX_LIST(
						   match->cmd->str)
					   && family == AF_INET6) {
					route_map_pentry_update(
						pentry_dep->event,
						pentry_dep->plist_name, index,
						pentry_dep->pentry);
				}
			}
		}
	}
}

/*
 * route_map_notify_pentry_dependencies：当某 prefix-list 的单条 entry 发生变化时
 * （affected_name 为列表名），向所有依赖它的 route-map 派发通知：查得依赖对象后，
 * 遍历其依赖集合，对每个依赖者执行 process_dependency 更新前缀表。
 */
void route_map_notify_pentry_dependencies(const char *affected_name,
					  struct prefix_list_entry *pentry,
					  route_map_event_t event)
{
	struct route_map_dep *dep = NULL;
	struct hash *upd8_hash = NULL;
	struct route_map_pentry_dep pentry_dep;

	if (!affected_name || !pentry)
		return;

	upd8_hash = route_map_get_dep_hash(event);
	if (!upd8_hash)
		return;

	dep = hash_lookup(upd8_hash, (void *)affected_name);
	if (dep) {
		if (!dep->this_hash)
			dep->this_hash = upd8_hash;

		memset(&pentry_dep, 0, sizeof(pentry_dep));
		pentry_dep.pentry = pentry;
		pentry_dep.plist_name = affected_name;
		pentry_dep.event = event;

		hash_iterate(dep->dep_rmap_hash,
			     route_map_pentry_process_dependency,
			     (void *)&pentry_dep);
	}
}

/*
 * route_map_apply_ext：把 route-map 应用到一条路由（对象）上的总入口，是理解整个
 * 匹配流程的关键函数。
 * 参数：map 目标 route-map；prefix 参与前缀匹配；match_object 参与匹配；set_object
 * 承载 set 结果（可与 match_object 相同）；pref 为出参，命中返回 index->pref，
 * 未命中返回 65536。
 * 流程：先做递归深度保护（超过 RMAP_RECURSION_LIMIT 直接 deny）；map 为空或无子句
 * 返回 deny；EVPN 前缀跳过优化直接从 head 起扫，否则用 route_map_get_index 做 LPM
 * 优化选出起始子句；随后按 pref 递增遍历子句：NOMATCH 记为 deny 并继续，NOOP 保持原
 * 返回值继续，MATCH 且为 permit 时执行全部 set、按需 call 目标 route-map，再依
 * exitpolicy 决定 exit / next / goto；若为 deny 则立即 deny 结束。
 * 返回 RMAP_PERMITMATCH 或 RMAP_DENYMATCH；副作用：更新统计计数与耗时。
 */
/* Apply route map's each index to the object.

   The matrix for a route-map looks like this:
   (note, this includes the description for the "NEXT"
   and "GOTO" frobs now

	   |   Match   |   No Match   | No op
	   |-----------|--------------|-------
    permit |   action  |     cont     | cont.
	   |           | default:deny | default:permit
    -------------------+-----------------------
	   |   deny    |     cont     | cont.
    deny   |           | default:deny | default:permit
	   |-----------|--------------|--------

   action)
      -Apply Set statements, accept route
      -If Call statement is present jump to the specified route-map, if it
	 denies the route we finish.
      -If NEXT is specified, goto NEXT statement
      -If GOTO is specified, goto the first clause where pref > nextpref
      -If nothing is specified, do as Cisco and finish
   deny)
      -Route is denied by route-map.
   cont)
      -Goto Next index

   If we get no matches after we've processed all updates, then the route
   is dropped too.

   Some notes on the new "CALL", "NEXT" and "GOTO"
     call WORD        - If this clause is matched, then the set statements
			are executed and then we jump to route-map 'WORD'. If
			this route-map denies the route, we finish, in other
   case we
			do whatever the exit policy (EXIT, NEXT or GOTO) tells.
     on-match next    - If this clause is matched, then the set statements
			are executed and then we drop through to the next clause
     on-match goto n  - If this clause is matched, then the set statements
			are executed and then we goto the nth clause, or the
			first clause greater than this. In order to ensure
			route-maps *always* exit, you cannot jump backwards.
			Sorry ;)

   We need to make sure our route-map processing matches the above
*/
route_map_result_t route_map_apply_ext(struct route_map *map,
				       const struct prefix *prefix,
				       void *match_object, void *set_object,
				       int *pref)
{
	static int recursion = 0;
	enum route_map_cmd_result_t match_ret = RMAP_NOMATCH;
	route_map_result_t ret = RMAP_PERMITMATCH;
	struct route_map_index *index = NULL;
	struct route_map_rule *set = NULL;
	bool skip_match_clause = false;
	RUSAGE_T mbefore, mafter;
	RUSAGE_T ibefore, iafter;
	unsigned long cputime;
	enum route_map_action_reason reason = route_map_action_none;

	if (recursion > RMAP_RECURSION_LIMIT) {
		if (map)
			map->applied++;

		flog_warn(
			EC_LIB_RMAP_RECURSION_LIMIT,
			"route-map recursion limit (%d) reached, discarding route",
			RMAP_RECURSION_LIMIT);
		recursion = 0;
		return RMAP_DENYMATCH;
	}

	if (map == NULL || map->head == NULL) {
		if (map)
			map->applied++;
		ret = RMAP_DENYMATCH;
		reason = route_map_action_map_null;
		goto route_map_apply_end;
	}

	map->applied++;

	GETRUSAGE(&mbefore);
	ibefore = mbefore;

	if (prefix->family == AF_EVPN) {
		index = map->head;
	} else {
		skip_match_clause = true;
		index = route_map_get_index(map, prefix, match_object,
					    &match_ret);
	}

	if (index) {
		index->applied++;

		GETRUSAGE(&iafter);
		event_consumed_time(&iafter, &ibefore, &cputime);
		index->cputime += cputime;
		ibefore = iafter;

		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
			zlog_debug(
				"Best match route-map: %s, sequence: %d for pfx: %pFX, result: %s",
				map->name, index->pref, prefix,
				route_map_cmd_result_str(match_ret));
	} else {
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
			zlog_debug(
				"No best match sequence for pfx: %pFX in route-map: %s, result: %s",
				prefix, map->name,
				route_map_cmd_result_str(match_ret));
		/*
		 * No index matches this prefix. Return deny unless,
		 * match_ret = RMAP_NOOP.
		 */
		if (match_ret == RMAP_NOOP)
			ret = RMAP_PERMITMATCH;
		else
			ret = RMAP_DENYMATCH;
		reason = route_map_action_no_index;
		goto route_map_apply_end;
	}

	for (; index; index = index->next) {
		if (!skip_match_clause) {
			index->applied++;
			/* Apply this index. */
			match_ret = route_map_apply_match(&index->match_list,
							  prefix, match_object);
			if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP))) {
				zlog_debug(
					"Route-map: %s, sequence: %d, prefix: %pFX, result: %s",
					map->name, index->pref, prefix,
					route_map_cmd_result_str(match_ret));
			}
		} else
			skip_match_clause = false;


		/* Now we apply the matrix from above */
		if (match_ret == RMAP_NOOP)
			/*
			 * Do not change the return value. Retain the previous
			 * return value. Previous values can be:
			 * 1)permitmatch (if a nomatch was never
			 * seen before in this route-map.)
			 * 2)denymatch (if a nomatch was seen earlier in one
			 * of the previous sequences)
			 */

			/*
			 * 'cont' from matrix - continue to next route-map
			 * sequence
			 */
			continue;
		else if (match_ret == RMAP_NOMATCH) {

			/*
			 * The return value is now changed to denymatch.
			 * So from here on out, even if we see more noops,
			 * we retain this return value and return this
			 * eventually if there are no matches.
			 */
			ret = RMAP_DENYMATCH;

			/*
			 * 'cont' from matrix - continue to next route-map
			 * sequence
			 */
			continue;
		} else if (match_ret == RMAP_MATCH) {
			if (index->type == RMAP_PERMIT)
			/* 'action' */
			{
				/* Match succeeded, rmap is of type permit */
				ret = RMAP_PERMITMATCH;

				/* permit+match must execute sets */
				for (set = index->set_list.head; set;
				     set = set->next)
					/*
					 * set cmds return RMAP_OKAY or
					 * RMAP_ERROR. We do not care if
					 * set succeeded or not. So, ignore
					 * return code.
					 */
					(void)(*set->cmd->func_apply)(
						set->value, prefix, set_object);

				/* Call another route-map if available */
				if (index->nextrm) {
					struct route_map *nextrm =
						route_map_lookup_by_name(
							index->nextrm);

					if (nextrm) /* Target route-map found,
						       jump to it */
					{
						recursion++;
						ret = route_map_apply_ext(
							nextrm, prefix,
							match_object,
							set_object, NULL);
						recursion--;
					}

					/* If nextrm returned 'deny', finish. */
					if (ret == RMAP_DENYMATCH) {
						reason = route_map_action_next_deny;
						goto route_map_apply_end;
					}
				}

				switch (index->exitpolicy) {
				case RMAP_EXIT:
					reason = route_map_action_exit;
					goto route_map_apply_end;
				case RMAP_NEXT:
					continue;
				case RMAP_GOTO: {
					/* Find the next clause to jump to */
					struct route_map_index *next =
						index->next;
					int nextpref = index->nextpref;

					while (next && next->pref < nextpref) {
						index = next;
						next = next->next;
					}
					if (next == NULL) {
						/* No clauses match! */
						reason = route_map_action_goto_null;
						goto route_map_apply_end;
					}
				}
				}
			} else if (index->type == RMAP_DENY)
			/* 'deny' */
			{
				ret = RMAP_DENYMATCH;
				reason = route_map_action_index_deny;
				goto route_map_apply_end;
			}
		}
		GETRUSAGE(&iafter);
		event_consumed_time(&iafter, &ibefore, &cputime);
		index->cputime += cputime;
		ibefore = iafter;
	}

route_map_apply_end:
	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Route-map: %s, prefix: %pFX, result: %s, reason: %s",
			   (map ? map->name : "null"), prefix, route_map_result_str(ret),
			   route_map_action_reason2str(reason));

	if (pref) {
		if (index != NULL && ret == RMAP_PERMITMATCH)
			*pref = index->pref;
		else
			*pref = 65536;
	}

	if (map) {
		GETRUSAGE(&mbefore);
		GETRUSAGE(&mafter);
		event_consumed_time(&mafter, &mbefore, &cputime);
		map->cputime += cputime;
	}

	return (ret);
}

/*
 * route_map_add_hook：注册 route-map 创建时的回调到 route_map_master.add_hook。
 */
void route_map_add_hook(void (*func)(const char *))
{
	route_map_master.add_hook = func;
}

/*
 * route_map_delete_hook：注册 route-map 删除时的回调到 route_map_master.delete_hook。
 */
void route_map_delete_hook(void (*func)(const char *))
{
	route_map_master.delete_hook = func;
}

/*
 * route_map_event_hook：注册 route-map 变更事件回调到 route_map_master.event_hook。
 */
void route_map_event_hook(void (*func)(const char *name))
{
	route_map_master.event_hook = func;
}

/*
 * 以下是 route-map 依赖表（dependency）的辅助函数与依赖处理例程。
 */
/* Routines for route map dependency lists and dependency processing */
/*
 * route_map_rmap_hash_cmp：比较两个 route_map_dep_data 的 route-map 名是否相同，
 * 用作依赖集合哈希的比较函数。
 */
static bool route_map_rmap_hash_cmp(const void *p1, const void *p2)
{
	return strcmp(((const struct route_map_dep_data *)p1)->rname,
		      ((const struct route_map_dep_data *)p2)->rname)
	       == 0;
}

/*
 * route_map_dep_hash_cmp：比较依赖对象的名字（p1 为 route_map_dep，p2 为字符串），
 * 用作依赖表哈希的比较函数。
 */
static bool route_map_dep_hash_cmp(const void *p1, const void *p2)
{

	return (strcmp(((const struct route_map_dep *)p1)->dep_name,
		       (const char *)p2)
		== 0);
}

/*
 * route_map_clear_reference：哈希遍历回调，从某个依赖对象的依赖集合中移除指定
 * route-map（arg）的引用并释放其 dep_data；若该依赖对象已无人依赖，则连同自身从
 * 依赖表移除并释放。
 */
static void route_map_clear_reference(struct hash_bucket *bucket, void *arg)
{
	struct route_map_dep *dep = bucket->data;
	struct route_map_dep_data *dep_data = NULL, tmp_dep_data;

	memset(&tmp_dep_data, 0, sizeof(tmp_dep_data));
	tmp_dep_data.rname = arg;
	dep_data = hash_release(dep->dep_rmap_hash, &tmp_dep_data);
	if (dep_data) {
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
			zlog_debug("Clearing reference for %s to %s count: %d",
				   dep->dep_name, tmp_dep_data.rname,
				   dep_data->refcnt);

		XFREE(MTYPE_ROUTE_MAP_NAME, dep_data->rname);
		XFREE(MTYPE_ROUTE_MAP_DEP_DATA, dep_data);
	}
	if (!dep->dep_rmap_hash->count) {
		dep = hash_release(dep->this_hash, (void *)dep->dep_name);
		hash_free(dep->dep_rmap_hash);
		XFREE(MTYPE_ROUTE_MAP_NAME, dep->dep_name);
		XFREE(MTYPE_ROUTE_MAP_DEP, dep);
	}
}

/*
 * route_map_clear_all_references：清除某 route-map 在所有类型依赖表中的引用，遍历
 * 每种依赖类型分别处理。
 */
static void route_map_clear_all_references(char *rmap_name)
{
	int i;

	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Clearing references for %s", rmap_name);

	for (i = 1; i < ROUTE_MAP_DEP_MAX; i++) {
		hash_iterate(route_map_dep_hash[i], route_map_clear_reference,
			     (void *)rmap_name);
	}
}

/*
 * route_map_dep_data_hash_make_key：取 route_map_dep_data 中的 rname 字段
 * 计算哈希键值。用于 dep_rmap_hash 表中以「引用者名称」为键查找。
 */
static unsigned int route_map_dep_data_hash_make_key(const void *p)
{
	const struct route_map_dep_data *dep_data = p;

	return string_hash_make(dep_data->rname);
}

/*
 * route_map_dep_hash_alloc：为哈希表分配并初始化一个 route_map_dep 条目。
 * dep_name 由字符串复制而来；内部 sub_hash（dep_rmap_hash）用于存放所有
 * 依赖该对象的 route-map 名称集合；this_hash 记录自己所属的 dep 哈希。
 * 作为 hash_get 的 alloc 回调使用。
 */
static void *route_map_dep_hash_alloc(void *p)
{
	char *dep_name = (char *)p;
	struct route_map_dep *dep_entry;

	dep_entry = XCALLOC(MTYPE_ROUTE_MAP_DEP, sizeof(struct route_map_dep));
	dep_entry->dep_name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, dep_name);
	dep_entry->dep_rmap_hash =
		hash_create_size(8, route_map_dep_data_hash_make_key,
				 route_map_rmap_hash_cmp, "Route Map Dep Hash");
	dep_entry->this_hash = NULL;

	return dep_entry;
}

/*
 * route_map_name_hash_alloc：为 dep_rmap_hash 分配一个 route_map_dep_data 条目，
 * 深拷贝 rname。参数 p 是一个临时的 route_map_dep_data（键结构）。作为 hash_get
 * 的 alloc 回调使用。
 */
static void *route_map_name_hash_alloc(void *p)
{
	struct route_map_dep_data *dep_data = NULL, *tmp_dep_data = NULL;

	dep_data = XCALLOC(MTYPE_ROUTE_MAP_DEP_DATA,
			   sizeof(struct route_map_dep_data));
	tmp_dep_data = p;
	dep_data->rname = XSTRDUP(MTYPE_ROUTE_MAP_NAME, tmp_dep_data->rname);
	return dep_data;
}

/*
 * route_map_dep_hash_make_key：外层 dep 哈希表的键函数，p 直接就是被依赖
 * 对象的名字字符串（如 prefix-list 名），对其取字符串哈希。
 */
static unsigned int route_map_dep_hash_make_key(const void *p)
{
	return (string_hash_make((char *)p));
}

/*
 * route_map_print_dependency：hash_iterate 回调，打印单条依赖关系
 * （被依赖对象 dep_name 被哪个 route-map rname 依赖）。仅用于 debug 输出。
 */
static void route_map_print_dependency(struct hash_bucket *bucket, void *data)
{
	struct route_map_dep_data *dep_data = bucket->data;
	char *rmap_name = dep_data->rname;
	char *dep_name = data;

	zlog_debug("%s: Dependency for %s: %s", __func__, dep_name, rmap_name);
}

/*
 * route_map_dep_update：在指定 dep 哈希表中新增或删除一条「dep_name（被依赖对象）
 * 被 rmap_name（route-map）依赖」的记录。
 * - 对 *_ADDED 类事件：确保 dep 条目存在，并对 (dep, rmap) 的 refcnt 加一；
 * - 对 *_DELETED 类事件：refcnt 减一，减到 0 时释放该 dep_data；若 dep 下已无
 *   任何引用者，则连 dep 条目一并释放；
 * - 对于 SET / MATCH / INDEX 类事件本函数不维护关系（仅打印）。
 * 返回 0 正常，-1 表示分配失败。
 */
static int route_map_dep_update(struct hash *dephash, const char *dep_name,
				const char *rmap_name, route_map_event_t type)
{
	struct route_map_dep *dep = NULL;
	char *dname, *rname;
	int ret = 0;
	struct route_map_dep_data *dep_data = NULL, *ret_dep_data = NULL;
	struct route_map_dep_data tmp_dep_data;

	dname = XSTRDUP(MTYPE_ROUTE_MAP_NAME, dep_name);
	rname = XSTRDUP(MTYPE_ROUTE_MAP_NAME, rmap_name);

	switch (type) {
	case RMAP_EVENT_PLIST_ADDED:
	case RMAP_EVENT_CLIST_ADDED:
	case RMAP_EVENT_ECLIST_ADDED:
	case RMAP_EVENT_ASLIST_ADDED:
	case RMAP_EVENT_LLIST_ADDED:
	case RMAP_EVENT_CALL_ADDED:
	case RMAP_EVENT_FILTER_ADDED:
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
			zlog_debug("Adding dependency for filter %s in route-map %s",
				   dep_name, rmap_name);
		dep = (struct route_map_dep *)hash_get(
			dephash, dname, route_map_dep_hash_alloc);
		if (!dep) {
			ret = -1;
			goto out;
		}

		if (!dep->this_hash)
			dep->this_hash = dephash;

		memset(&tmp_dep_data, 0, sizeof(tmp_dep_data));
		tmp_dep_data.rname = rname;
		dep_data = hash_lookup(dep->dep_rmap_hash, &tmp_dep_data);
		if (!dep_data)
			dep_data = hash_get(dep->dep_rmap_hash, &tmp_dep_data,
					    route_map_name_hash_alloc);

		dep_data->refcnt++;
		break;
	case RMAP_EVENT_PLIST_DELETED:
	case RMAP_EVENT_CLIST_DELETED:
	case RMAP_EVENT_ECLIST_DELETED:
	case RMAP_EVENT_ASLIST_DELETED:
	case RMAP_EVENT_LLIST_DELETED:
	case RMAP_EVENT_CALL_DELETED:
	case RMAP_EVENT_FILTER_DELETED:
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
			zlog_debug("Deleting dependency for filter %s in route-map %s",
				   dep_name, rmap_name);
		dep = (struct route_map_dep *)hash_get(dephash, dname, NULL);
		if (!dep) {
			goto out;
		}

		memset(&tmp_dep_data, 0, sizeof(tmp_dep_data));
		tmp_dep_data.rname = rname;
		dep_data = hash_lookup(dep->dep_rmap_hash, &tmp_dep_data);
		/*
		 * If dep_data is NULL then something has gone seriously
		 * wrong in route-map handling.  Note it and prevent
		 * the crash.
		 */
		if (!dep_data) {
			zlog_warn(
				"route-map dependency for route-map %s: %s is not correct",
				rmap_name, dep_name);
			goto out;
		}

		dep_data->refcnt--;

		if (!dep_data->refcnt) {
			ret_dep_data = hash_release(dep->dep_rmap_hash,
						    &tmp_dep_data);
			if (ret_dep_data) {
				XFREE(MTYPE_ROUTE_MAP_NAME,
				      ret_dep_data->rname);
				XFREE(MTYPE_ROUTE_MAP_DEP_DATA, ret_dep_data);
			}
		}

		if (!dep->dep_rmap_hash->count) {
			dep = hash_release(dephash, dname);
			hash_free(dep->dep_rmap_hash);
			XFREE(MTYPE_ROUTE_MAP_NAME, dep->dep_name);
			XFREE(MTYPE_ROUTE_MAP_DEP, dep);
		}
		break;
	case RMAP_EVENT_SET_ADDED:
	case RMAP_EVENT_SET_DELETED:
	case RMAP_EVENT_SET_REPLACED:
	case RMAP_EVENT_MATCH_ADDED:
	case RMAP_EVENT_MATCH_DELETED:
	case RMAP_EVENT_MATCH_REPLACED:
	case RMAP_EVENT_INDEX_ADDED:
	case RMAP_EVENT_INDEX_DELETED:
		break;
	}

	if (dep) {
		if (CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP))
			hash_iterate(dep->dep_rmap_hash,
				     route_map_print_dependency, dname);
	}

out:
	XFREE(MTYPE_ROUTE_MAP_NAME, rname);
	XFREE(MTYPE_ROUTE_MAP_NAME, dname);
	return ret;
}

/*
 * route_map_get_dep_hash：根据事件类型返回其对应的依赖哈希表
 * （如 PLIST 事件对应 ROUTE_MAP_DEP_PLIST）。SET/MATCH/INDEX 等不涉及依赖
 * 的事件返回 NULL，调用者据此跳过依赖处理。
 */
static struct hash *route_map_get_dep_hash(route_map_event_t event)
{
	struct hash *upd8_hash = NULL;

	switch (event) {
	case RMAP_EVENT_PLIST_ADDED:
	case RMAP_EVENT_PLIST_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_PLIST];
		break;
	case RMAP_EVENT_CLIST_ADDED:
	case RMAP_EVENT_CLIST_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_CLIST];
		break;
	case RMAP_EVENT_ECLIST_ADDED:
	case RMAP_EVENT_ECLIST_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_ECLIST];
		break;
	case RMAP_EVENT_ASLIST_ADDED:
	case RMAP_EVENT_ASLIST_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_ASPATH];
		break;
	case RMAP_EVENT_LLIST_ADDED:
	case RMAP_EVENT_LLIST_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_LCLIST];
		break;
	case RMAP_EVENT_CALL_ADDED:
	case RMAP_EVENT_CALL_DELETED:
	case RMAP_EVENT_MATCH_ADDED:
	case RMAP_EVENT_MATCH_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_RMAP];
		break;
	case RMAP_EVENT_FILTER_ADDED:
	case RMAP_EVENT_FILTER_DELETED:
		upd8_hash = route_map_dep_hash[ROUTE_MAP_DEP_FILTER];
		break;
	/*
	 * Should we actually be ignoring these?
	 * I am not sure but at this point in time, let
	 * us get them into this switch and we can peel
	 * them into the appropriate place in the future
	 */
	case RMAP_EVENT_SET_ADDED:
	case RMAP_EVENT_SET_DELETED:
	case RMAP_EVENT_SET_REPLACED:
	case RMAP_EVENT_MATCH_REPLACED:
	case RMAP_EVENT_INDEX_ADDED:
	case RMAP_EVENT_INDEX_DELETED:
		upd8_hash = NULL;
		break;
	}
	return (upd8_hash);
}

/*
 * route_map_process_dependency：hash_iterate 回调。对依赖被改动对象的每个
 * route-map，调用 master 的 event_hook 通知其上层的协议模块需要重新应用
 * route-map（例如重算过滤结果）。
 */
static void route_map_process_dependency(struct hash_bucket *bucket, void *data)
{
	struct route_map_dep_data *dep_data = NULL;
	char *rmap_name = NULL;

	dep_data = bucket->data;
	rmap_name = dep_data->rname;

	if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
		zlog_debug("Notifying %s of dependency", rmap_name);
	if (route_map_master.event_hook)
		(*route_map_master.event_hook)(rmap_name);
}

/*
 * route_map_upd8_dependency：对外接口，更新依赖关系。根据事件类型取到对应
 * 的 dep 哈希（NULL 则忽略），随后调用 route_map_dep_update 维护引用计数。
 * 对 CALL_ADDED / CALL_DELETED 事件额外触发 master 的 add_hook / delete_hook，
 * 用于 route-map 之间「call」引用的联动维护。
 */
void route_map_upd8_dependency(route_map_event_t type, const char *arg,
			       const char *rmap_name)
{
	struct hash *upd8_hash = NULL;

	if ((upd8_hash = route_map_get_dep_hash(type))) {
		route_map_dep_update(upd8_hash, arg, rmap_name, type);

		if (type == RMAP_EVENT_CALL_ADDED) {
			/* Execute hook. */
			if (route_map_master.add_hook)
				(*route_map_master.add_hook)(rmap_name);
		} else if (type == RMAP_EVENT_CALL_DELETED) {
			/* Execute hook. */
			if (route_map_master.delete_hook)
				(*route_map_master.delete_hook)(rmap_name);
		}
	}
}

/*
 * route_map_notify_dependencies：当被依赖对象（如某个 prefix-list）发生变化时，
 * 通知所有依赖它的 route-map。affected_name 为变更对象名，event 指出其类别
 * （决定查哪张 dep 表）。找到对应 dep 后遍历其 dep_rmap_hash，对每个引用者
 * 调用 event_hook。affected_name 为空或找不到 dep 表则静默返回。
 */
void route_map_notify_dependencies(const char *affected_name,
				   route_map_event_t event)
{
	struct route_map_dep *dep;
	struct hash *upd8_hash;
	char *name;

	if (!affected_name)
		return;

	name = XSTRDUP(MTYPE_ROUTE_MAP_NAME, affected_name);

	if ((upd8_hash = route_map_get_dep_hash(event)) == NULL) {
		XFREE(MTYPE_ROUTE_MAP_NAME, name);
		return;
	}

	dep = hash_lookup(upd8_hash, name);
	if (dep) {
		if (!dep->this_hash)
			dep->this_hash = upd8_hash;

		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)))
			zlog_debug("Filter %s updated", dep->dep_name);
		hash_iterate(dep->dep_rmap_hash, route_map_process_dependency,
			     (void *)event);
	}

	XFREE(MTYPE_ROUTE_MAP_NAME, name);
}

/*
 * clear_route_map_helper：清零单个 route-map 及其所有 sub-clause（index）的
 * 统计计数。applied/applied_clear 保存匹配次数，cputime 保存累计 CPU 时间；
 * 「_clear」字段用于实现 clear 命令的增量显示（clear 后再次 show 只显示新计数）。
 */
/* VTY related functions. */
static void clear_route_map_helper(struct route_map *map)
{
	struct route_map_index *index;

	map->applied_clear = map->applied;
	map->cputime = 0;
	for (index = map->head; index; index = index->next) {
		index->applied_clear = index->applied;
		index->cputime = 0;
	}
}

/*
 * rmap_clear_counters：CLI "clear route-map counters [NAME]" 的实现。
 * 给了 rmapname 则只清零该 route-map（不存在则报错返回），否则遍历所有
 * route-map 全部清零。统计只影响显示，不改变匹配行为。
 */
DEFPY (rmap_clear_counters,
       rmap_clear_counters_cmd,
       "clear route-map counters [RMAP_NAME$rmapname]",
       CLEAR_STR
       "route-map information\n"
       "counters associated with the specified route-map\n"
       "route-map name\n")
{
	struct route_map *map;

	if (rmapname) {
		map = route_map_lookup_by_name(rmapname);

		if (map)
			clear_route_map_helper(map);
		else {
			vty_out(vty, "%s: 'route-map %s' not found\n",
				frr_protonameinst, rmapname);
			return CMD_SUCCESS;
		}
	} else {
		for (map = route_map_master.head; map; map = map->next)
			clear_route_map_helper(map);
	}

	return CMD_SUCCESS;

}

/*
 * rmap_show_name：CLI "show route-map [WORD] [json]" 的实现。解析可选的
 * route-map 名与 json 标志，转交 vty_show_route_map 输出。
 */
DEFUN_NOSH (rmap_show_name,
            rmap_show_name_cmd,
            "show route-map [WORD] [json]",
            SHOW_STR
            "route-map information\n"
            "route-map name\n"
            JSON_STR)
{
	bool uj = use_json(argc, argv);
	int idx = 0;
	const char *name = NULL;

	if (argv_find(argv, argc, "WORD", &idx))
		name = argv[idx]->arg;

	return vty_show_route_map(vty, name, uj);
}

/*
 * rmap_show_unused：CLI "show route-map-unused [json]" 的实现。列出所有未被
 * 任何协议引用的 route-map（use_count 为 0），便于清理无用配置。
 */
DEFPY_NOSH (rmap_show_unused,
       rmap_show_unused_cmd,
       "show route-map-unused [json$json]",
       SHOW_STR
       "unused route-map information\n"
       JSON_STR)
{
	bool uj = use_json(argc, argv);

	return vty_show_unused_route_map(vty, uj);
}

/*
 * debug_rmap：CLI "debug route-map [detail]" 的实现。设置 DEBUG_ROUTEMAP 位；
 * 带 detail 时额外置 DEBUG_ROUTEMAP_DETAIL 位，输出更详细的逐条规则信息。
 */
DEFPY (debug_rmap,
       debug_rmap_cmd,
       "debug route-map [detail]$detail",
       DEBUG_STR
       "Debug option set for route-maps\n"
       "Detailed output\n")
{
	if (!detail)
		SET_FLAG(rmap_debug, DEBUG_ROUTEMAP);
	else
		SET_FLAG(rmap_debug, DEBUG_ROUTEMAP | DEBUG_ROUTEMAP_DETAIL);

	return CMD_SUCCESS;
}

/*
 * no_debug_rmap：CLI "no debug route-map [detail]" 的实现，清除对应的 debug 位。
 */
DEFPY (no_debug_rmap,
       no_debug_rmap_cmd,
       "no debug route-map [detail]$detail",
       NO_STR
       DEBUG_STR
       "Debug option set for route-maps\n"
       "Detailed output\n")
{
	if (!detail)
		UNSET_FLAG(rmap_debug, DEBUG_ROUTEMAP);
	else
		UNSET_FLAG(rmap_debug, DEBUG_ROUTEMAP | DEBUG_ROUTEMAP_DETAIL);

	return CMD_SUCCESS;
}

/*
 * rmap_debug_node：route-map debug 命令节点（RMAP_DEBUG_NODE），其
 * config_write 回调把当前 debug 状态写入配置，实现重启后保留 debug 设置。
 */
/* Debug node. */
static int rmap_config_write_debug(struct vty *vty);
static struct cmd_node rmap_debug_node = {
	.name = "route-map debug",
	.node = RMAP_DEBUG_NODE,
	.prompt = "",
	.config_write = rmap_config_write_debug,
};

/*
 * route_map_show_debug：向 vty 输出当前 debug route-map 状态（供 "show running"
 * 等场景调用）。仅在 DEBUG_ROUTEMAP 置位时打印。
 */
void route_map_show_debug(struct vty *vty)
{
	if (CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP))
		vty_out(vty, "debug route-map\n");
}

/*
 * rmap_config_write_debug：rmap_debug_node 的 config_write 回调。若 debug
 * 处于开启状态则输出 "debug route-map" 并返回写入行数 1，否则返回 0。
 */
/* Configuration write function. */
static int rmap_config_write_debug(struct vty *vty)
{
	int write = 0;

	if (CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP)) {
		vty_out(vty, "debug route-map\n");
		write++;
	}

	return write;
}

/* Common route map rules */

/*
 * route_map_rule_tag_compile：把 CLI 传入的 tag 字符串编译成 route_tag_t。
 * 解析失败（空串、有剩余字符、溢出、超过 ROUTE_TAG_MAX）返回 NULL。
 * 成功则返回按 MTYPE_ROUTE_MAP_COMPILED 分配的 tag，由 route_map_rule_tag_free 释放。
 */
void *route_map_rule_tag_compile(const char *arg)
{
	unsigned long int tmp;
	char *endptr;
	route_tag_t *tag;

	errno = 0;
	tmp = strtoul(arg, &endptr, 0);
	if (arg[0] == '\0' || *endptr != '\0' || errno || tmp > ROUTE_TAG_MAX)
		return NULL;

	tag = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(*tag));
	*tag = tmp;

	return tag;
}

/*
 * route_map_rule_tag_free：释放 route_map_rule_tag_compile 分配的内存。
 */
void route_map_rule_tag_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/*
 * route_map_finish：模块退出时的全局清理。清空 match/set 命令注册表（pop 出
 * 全部条目后再 fini/init），清空三个 hook，删除所有 route-map，释放各依赖哈希
 * 与 master 哈希。调用后框架回到「已初始化且为空」状态，可再次 route_map_init。
 */
void route_map_finish(void)
{
	int i;
	struct route_map_rule_cmd_proxy *proxy;

	/* these 2 hash tables have INIT_HASH initializers, so the "default"
	 * state is "initialized & empty" => fini() followed by init() to
	 * return to that same state
	 */
	while ((proxy = rmap_cmd_name_pop(rmap_match_cmds)))
		(void)proxy;
	rmap_cmd_name_fini(rmap_match_cmds);
	rmap_cmd_name_init(rmap_match_cmds);

	while ((proxy = rmap_cmd_name_pop(rmap_set_cmds)))
		(void)proxy;
	rmap_cmd_name_fini(rmap_set_cmds);
	rmap_cmd_name_init(rmap_set_cmds);

	/*
	 * All protocols are setting these to NULL
	 * by default on shutdown( route_map_finish )
	 * Why are we making them do this work?
	 */
	route_map_master.add_hook = NULL;
	route_map_master.delete_hook = NULL;
	route_map_master.event_hook = NULL;

	/* cleanup route_map */
	while (route_map_master.head) {
		struct route_map *map = route_map_master.head;
		map->to_be_processed = false;
		route_map_delete(map);
	}

	for (i = 1; i < ROUTE_MAP_DEP_MAX; i++) {
		hash_free(route_map_dep_hash[i]);
		route_map_dep_hash[i] = NULL;
	}

	hash_free(route_map_master_hash);
	route_map_master_hash = NULL;
}

/*
 * route_map_counter_increment：协议附着（引用）某 route-map 时使用计数加一。
 * map 为 NULL 时安全返回。
 */
/* Increment the use_count counter while attaching the route map */
void route_map_counter_increment(struct route_map *map)
{
	if (map)
		map->use_count++;
}

/*
 * route_map_counter_decrement：协议解除引用时使用计数减一；计数已为 0 时直接
 * 返回，防止下溢为负。
 */
/* Decrement the use_count counter while detaching the route map. */
void route_map_counter_decrement(struct route_map *map)
{
	if (map) {
		if (map->use_count <= 0)
			return;
		map->use_count--;
	}
}

/*
 * show_route_map_pfx_tbl：隐藏的调试命令 "show route-map NAME prefix-table"。
 * 转储某 route-map 内部的 ipv4/ipv6 前缀表（LPM trie）：每个前缀节点旁列出
 * 依赖它的 index 列表，用于排查前缀表优化中引用计数的正确性。
 */
DEFUN_HIDDEN(show_route_map_pfx_tbl, show_route_map_pfx_tbl_cmd,
	     "show route-map RMAP_NAME prefix-table",
	     SHOW_STR
	     "route-map\n"
	     "route-map name\n"
	     "internal prefix-table\n")
{
	const char *rmap_name = argv[2]->arg;
	struct route_map *rmap = NULL;
	struct route_table *rm_pfx_tbl4 = NULL;
	struct route_table *rm_pfx_tbl6 = NULL;
	struct route_node *rn = NULL, *prn = NULL;
	struct list *rmap_index_list = NULL;
	struct listnode *ln = NULL, *nln = NULL;
	struct route_map_index *index = NULL;
	uint8_t len = 54;

	vty_out(vty, "%s:\n", frr_protonameinst);
	rmap = route_map_lookup_by_name(rmap_name);
	if (rmap) {
		rm_pfx_tbl4 = rmap->ipv4_prefix_table;
		if (rm_pfx_tbl4) {
			vty_out(vty, "\n%s%43s%s\n", "IPv4 Prefix", "",
				"Route-map Index List");
			vty_out(vty, "%s%39s%s\n", "_______________", "",
				"____________________");
			for (rn = route_top(rm_pfx_tbl4); rn;
			     rn = route_next(rn)) {
				vty_out(vty, "    %pRN (%d)\n", rn,
					route_node_get_lock_count(rn));

				vty_out(vty, "(P) ");
				prn = rn->parent;
				if (prn) {
					vty_out(vty, "%pRN\n", prn);
				}

				vty_out(vty, "\n");
				rmap_index_list = (struct list *)rn->info;
				if (!rmap_index_list
				    || !listcount(rmap_index_list))
					vty_out(vty, "%*s%s\n", len, "", "-");
				else
					for (ALL_LIST_ELEMENTS(rmap_index_list,
							       ln, nln,
							       index)) {
						vty_out(vty, "%*s%s seq %d\n",
							len, "",
							index->map->name,
							index->pref);
					}
				vty_out(vty, "\n");
			}
		}

		rm_pfx_tbl6 = rmap->ipv6_prefix_table;
		if (rm_pfx_tbl6) {
			vty_out(vty, "\n%s%43s%s\n", "IPv6 Prefix", "",
				"Route-map Index List");
			vty_out(vty, "%s%39s%s\n", "_______________", "",
				"____________________");
			for (rn = route_top(rm_pfx_tbl6); rn;
			     rn = route_next(rn)) {
				vty_out(vty, "    %pRN (%d)\n", rn,
					route_node_get_lock_count(rn));

				vty_out(vty, "(P) ");
				prn = rn->parent;
				if (prn) {
					vty_out(vty, "%pRN\n", prn);
				}

				vty_out(vty, "\n");
				rmap_index_list = (struct list *)rn->info;
				if (!rmap_index_list
				    || !listcount(rmap_index_list))
					vty_out(vty, "%*s%s\n", len, "", "-");
				else
					for (ALL_LIST_ELEMENTS(rmap_index_list,
							       ln, nln,
							       index)) {
						vty_out(vty, "%*s%s seq %d\n",
							len, "",
							index->map->name,
							index->pref);
					}
				vty_out(vty, "\n");
			}
		}
	}

	vty_out(vty, "\n");
	return CMD_SUCCESS;
}

/*
 * route_map_init_new：route-map 框架的一次性初始化。创建 master 哈希与各依赖
 * 哈希表、清 debug 位、安装 debug 节点与命令。in_backend 为真时（如 mgmtd 后端
 * 进程）不注册 CLI 命令（route_map_cli_init）。
 */
/* Initialization of route map vector. */
void route_map_init_new(bool in_backend)
{
	int i;

	route_map_master_hash =
		hash_create_size(8, route_map_hash_key_make, route_map_hash_cmp,
				 "Route Map Master Hash");

	for (i = 1; i < ROUTE_MAP_DEP_MAX; i++)
		route_map_dep_hash[i] = hash_create_size(
			8, route_map_dep_hash_make_key, route_map_dep_hash_cmp,
			"Route Map Dep Hash");

	UNSET_FLAG(rmap_debug, DEBUG_ROUTEMAP);

	if (!in_backend) {
		/* we do not want to handle config commands in the backend */
		route_map_cli_init();
	}

	/* Install route map top node. */
	install_node(&rmap_debug_node);

	/* Install route map commands. */
	install_element(CONFIG_NODE, &debug_rmap_cmd);
	install_element(CONFIG_NODE, &no_debug_rmap_cmd);

	/* Install show command */
	install_element(ENABLE_NODE, &rmap_clear_counters_cmd);

	install_element(ENABLE_NODE, &rmap_show_name_cmd);
	install_element(ENABLE_NODE, &rmap_show_unused_cmd);

	install_element(ENABLE_NODE, &debug_rmap_cmd);
	install_element(ENABLE_NODE, &no_debug_rmap_cmd);

	install_element(ENABLE_NODE, &show_route_map_pfx_tbl_cmd);
}

/*
 * route_map_init：route_map_init_new 的包装，供普通（非后端）进程调用，
 * 会同时注册 CLI 命令。
 */
void route_map_init(void)
{
	route_map_init_new(false);
}
