// SPDX-License-Identifier: GPL-2.0-or-later
/* Distribute list functions header
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

#ifndef _ZEBRA_DISTRIBUTE_H
#define _ZEBRA_DISTRIBUTE_H
/*
 * ============================ 文件导读 ============================
 * distribute.h —— FRR 「distribute-list（分发/过滤列表）」框架头文件
 *
 * 一、职责
 *   distribute-list 是各路由协议（rip/ospf/bgp 等）都有的老牌配置：在某个
 *   接口的入方向/出方向，对路由前缀用 access-list 或 prefix-list 做过滤。
 *   本框架把这些配置统一成「按 (接口, 方向, 地址族) 索引的过滤器表」，并
 *   提供挂钩（hook）机制，让协议在路由被收/发时应用过滤。
 *
 * 二、核心数据结构
 *   - enum distribute_type  四象限：{V4,V6} x {IN,OUT}，即过滤方向与地址族
 *     的组合，DISTRIBUTE_MAX 是数量。
 *   - struct distribute      一条分发记录：接口名 + 四个 in/out 的 access
 *     列表名 list[] + 四个 prefix-list 名 prefix[]。
 *   - struct distribute_ctx  某 VRF 的分发上下文：disthash（ifname -> 记录的
 *     哈希表）、加/删挂钩函数、所属 vrf。
 *
 * 三、与其他模块的关系
 *   - 依赖 filter.h 的 enum filter_type（permit/deny）与访问列表机制，
 *     if.h 的接口，northbound.h 的 YANG 回调。
 *   - 各协议模块持有自己的 distribute_ctx，通过 hook 把它接到路由收发路径。
 *   - 实现体在 distribute.c，CLI 解析与 northbound 回调也在一处。
 *
 * 四、阅读顺序
 *   先看 enum distribute_type 与 struct distribute 理解「过滤项长什么样」
 *   -> struct distribute_ctx 与钩子 -> 生命周期与查询原型
 *   -> distribute_apply_in / out（真正应用过滤）-> 解析器 -> 末尾的
 *   northbound 一组 group_* 回调。
 *
 * 五、易踩的坑
 *   1. list[] 与 prefix[] 都按 DISTRIBUTE_MAX 下标存放，下标即 distribute_type。
 *   2. 钩子是协议自定义路由过滤行为的抓手，默认无钩子时不做额外处理。
 *   3. northbound 回调需按注释成套注册，IPv4/IPv6 要各注册一套。
 * ================================================================
 */

/* FRR 超级头。 */
#include <zebra.h>
/* 接口结构 struct interface（过滤按接口区分方向）。 */
#include "if.h"
/* enum filter_type 与 access-list 机制。 */
#include "filter.h"
/* northbound（YANG 回调）相关类型声明。 */
#include "northbound.h"

/* C++ 兼容：按 C 链接约定导出符号。 */
#ifdef __cplusplus
extern "C" {
#endif

/* Distribute list types. */
/* 分发列表的「象限」：按地址族（V4/V6）与方向（入/出）组合出四种，作为
 * struct distribute 里 list[] / prefix[] 数组的下标，DISTRIBUTE_MAX 是总数。 */
enum distribute_type {
	DISTRIBUTE_V4_IN,
	DISTRIBUTE_V6_IN,
	DISTRIBUTE_V4_OUT,
	DISTRIBUTE_V6_OUT,
	DISTRIBUTE_MAX
};

/* 一条分发记录：针对某个接口，保存四个象限各自的 access-list 名与
 * prefix-list 名。同一接口在同一方向只能挂一种过滤方式（普通名在 list，
 * 前缀名在 prefix），二者择一。 */
struct distribute {
	/* Name of the interface. */
	char *ifname;

	/* Filter name of `in' and `out' */
	char *list[DISTRIBUTE_MAX];

	/* prefix-list name of `in' and `out' */
	char *prefix[DISTRIBUTE_MAX];
};

/* 某 VRF 的分发上下文：一个哈希表（ifname -> struct distribute）加两个
 * 钩子，再加所属 vrf。协议模块各持一份。 */
struct distribute_ctx {
	/* Hash of distribute list. */
	struct hash *disthash;

	/* Hook functions. */
	void (*distribute_add_hook)(struct distribute_ctx *ctx,
				    struct distribute *dist);
	void (*distribute_delete_hook)(struct distribute_ctx *ctx,
				       struct distribute *dist);

	/* vrf information */
	struct vrf *vrf;
};

/* Prototypes for distribute-list. */
/* 为某 VRF 创建一个分发上下文（并建空哈希表）。 */
extern struct distribute_ctx *distribute_list_ctx_create(struct vrf *vrf);
/* 销毁分发上下文；参数是二级指针，内部会把 *ctx 置空以避免悬垂。 */
extern void distribute_list_delete(struct distribute_ctx **ctx);
/* 注册「新增分发记录」时的钩子：协议借此在配置变化时刷新自身过滤状态。 */
extern void distribute_list_add_hook(struct distribute_ctx *ctx,
				     void (*)(struct distribute_ctx *ctx,
					      struct distribute *));
/* 注册「删除分发记录」时的钩子。 */
extern void distribute_list_delete_hook(struct distribute_ctx *ctx,
					void (*)(struct distribute_ctx *ctx,
						 struct distribute *));
/* 按接口名在分发上下文里查记录，没有则返回 NULL。 */
extern struct distribute *distribute_lookup(struct distribute_ctx *ctx,
					    const char *ifname);
/* 把分发配置写进 vty 输出（show running-config 用的那一份）。 */
extern int config_write_distribute(struct vty *vty,
				   struct distribute_ctx *ctx);
/* 把分发配置以「show 命令」的形式展示给用户。 */
extern int config_show_distribute(struct vty *vty,
				  struct distribute_ctx *ctx);

/* 对入方向路由应用某接口上的分发过滤，返回 permit / deny。 */
extern enum filter_type distribute_apply_in(struct interface *ifp, struct prefix *p);
/* 对出方向路由应用某接口上的分发过滤。 */
extern enum filter_type distribute_apply_out(struct interface *ifp, struct prefix *p);

/* 配置解析：设置某接口某方向的 access-list（prefix=false）或 prefix-list
 * （prefix=true）过滤名。v4 选地址族，dir 是 "in" / "out"。 */
extern int distribute_list_parser(struct distribute_ctx *ctx, bool prefix,
				  bool v4, const char *dir, const char *list,
				  const char *ifname);
/* 配置解析的逆操作：no 形式，取消某接口某方向的过滤设置。 */
extern int distribute_list_no_parser(struct distribute_ctx *ctx,
				     struct vty *vty, bool prefix, bool v4,
				     const char *dir, const char *list,
				     const char *ifname);

/* Northbound 区块：把 distribute-list 接入 FRR 的 YANG 配置回调框架。 */
/*
 * Northbound
 */

/* group_* 回调的说明：自己写 create 回调时，在创建分发项处调用
 * group_distribute_list_create_helper，并把 destroy 回调接到 YANG 模块
 * 信息里（或在自己的销毁函数里调用它）。 */
/*
 * Define your own create callback and then call thes helper with your
 * distribute list context when a list entry is created. Additionally, plug the
 * destroy callback into the frr_module_yang_info struct, or call it if you have
 * your own callback destroy function.
 */
/* 创建辅助：在某分发项被创建时，用它把项登记进 ctx（自带哈希与钩子处理）。 */
extern int group_distribute_list_create_helper(struct nb_cb_create_args *args,
					       struct distribute_ctx *ctx);
/* 销毁回调：删除 YANG 配置项时调用，清理对应的分发记录。 */
extern int group_distribute_list_destroy(struct nb_cb_destroy_args *args);

/* 使用说明：把你的 distribute-list 对应的所有 northbound
 * distribute_list 叶子回调都接上下面这 3 类处理函数；若要支持多协议，
 * 把本 group 在两个不同 container 下各用一次即可。 */
/*
 * Plug 3 of these handlers in for your distribute-list for all the northbound
 * distribute_list leaf callbacks. If you need multi-protocol then use the
 * grouping twice under 2 different containers.
 */
/* IPv4 分发列表：modify 处理修改回调，destroy 处理删除，cli_show 负责在
 * 配置展示时输出该节点。 */
extern int group_distribute_list_ipv4_modify(struct nb_cb_modify_args *args);
extern int group_distribute_list_ipv4_destroy(struct nb_cb_destroy_args *args);
extern void group_distribute_list_ipv4_cli_show(struct vty *vty,
						const struct lyd_node *dnode,
						bool show_defaults);
/* IPv6 分发列表：与 IPv4 三件套一一对应。 */
extern int group_distribute_list_ipv6_modify(struct nb_cb_modify_args *args);
extern int group_distribute_list_ipv6_destroy(struct nb_cb_destroy_args *args);
extern void group_distribute_list_ipv6_cli_show(struct vty *vty,
						const struct lyd_node *dnode,
						bool show_defaults);
/* 结束 extern "C" 块。 */
#ifdef __cplusplus
}
#endif

/* 结束多重包含保护 _ZEBRA_DISTRIBUTE_H。 */
#endif /* _ZEBRA_DISTRIBUTE_H */
