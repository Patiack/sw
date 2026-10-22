// SPDX-License-Identifier: GPL-2.0-or-later
/* route-map for interface.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * if_rmap.h —— 接口级 route-map 绑定表（Interface Route-Map）
 *
 * 用途：
 *   把「一个接口 + 方向（in / out）」映射到「一条 route-map 名字」。
 *   RIP / RIPNG 用它实现 `route-map ROUTE-MAP <in|out> IFNAME` 配置：
 *   对从某接口进来的路由、或从某接口发布出去的路由套用 route-map 过滤。
 *
 * 关键对象：
 *   struct if_rmap     一条记录：接口名 + in / out 两条 route-map 名。
 *   struct if_rmap_ctx 一批记录的容器（一个守护进程或一个 VRF 一份），
 *                      内含按接口名做 key 的 hash 表，以及增删钩子。
 *   enum if_rmap_type  区分方向：IF_RMAP_IN 进方向，IF_RMAP_OUT 出方向，
 *                      IF_RMAP_MAX 仅作数组长度哨兵。
 *
 * 设计要点：
 *   hash 以接口名（字符串）为 key，因此同一 ctx 内接口名唯一；
 *   记录里 routemap[] 数组按枚举下标存放，读写都要先断言方向合法；
 *   配置落地走 northbound（YANG），由 yang_modify / destroy 回调驱动
 *   本文件的 set / unset，最终调用 ctx 上的 add / delete 钩子通知使用方。
 *
 * 本文件只声明接口，实现见 if_rmap.c。
 */

#ifndef _ZEBRA_IF_RMAP_H
#define _ZEBRA_IF_RMAP_H

#include "typesafe.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lyd_node;
struct vty;

/*
 * enum if_rmap_type —— route-map 的作用方向。
 * IF_RMAP_IN  : 对从该接口收到的路由做过滤（入方向）。
 * IF_RMAP_OUT : 对该接口发出的路由做过滤（出方向）。
 * IF_RMAP_MAX : 不是真实方向，只用来当数组长度上限。
 */
enum if_rmap_type { IF_RMAP_IN, IF_RMAP_OUT, IF_RMAP_MAX };

/*
 * struct if_rmap —— 一条接口级 route-map 绑定记录。
 * ifname     : 接口名，同时充当 hash 的 key，字符串由本结构持有（会复制）。
 * routemap[] : 下标为 enum if_rmap_type，分别保存 in / out 方向的 route-map 名；
 *              为 NULL 表示该方向未配置。字符串同样由本结构持有（会复制）。
 * 注意：本结构由 if_rmap_new 分配、if_rmap_free 释放，不要在外部手工释放。
 */
struct if_rmap {
	/* Name of the interface. */
	const char *ifname;

	char *routemap[IF_RMAP_MAX];
};

/*
 * struct if_rmap_ctx —— 一批 if_rmap 记录的容器。
 * ifrmaphash          : 以接口名为 key 的 hash 表，存放本 ctx 的全部记录。
 * if_rmap_add_hook    : 记录被新增或改动时回调，通知使用方刷新策略。
 * if_rmap_delete_hook : 某方向 route-map 被删除时回调。
 * name                : ctx 名字（通常是 vrf 名或守护进程标识），可为 NULL。
 */
struct if_rmap_ctx {
	/* if_rmap */
	struct hash *ifrmaphash;

	/* Hook functions. */
	void (*if_rmap_add_hook)(struct if_rmap_ctx *ctx,
				    struct if_rmap *ifrmap);
	void (*if_rmap_delete_hook)(struct if_rmap_ctx *ctx,
				       struct if_rmap *ifrmap);

	/* naming information */
	char *name;
};

/*
 * if_rmap_ctx_create —— 新建一个 if_rmap 容器。
 * name 可为 NULL（仅作标识，不参与查找）；内部建一个初始容量 4 的 hash 表。
 * 返回新 ctx（已 XCALLOC，失败即致命），调用方负责用 if_rmap_ctx_delete 释放。
 */
extern struct if_rmap_ctx *if_rmap_ctx_create(const char *name);
/*
 * if_rmap_ctx_delete —— 释放容器：先清空并释放 hash 表里全部记录（用 if_rmap_free），
 * 再释放 name 与 ctx 本身。ctx 释放后不可再用。
 */
extern void if_rmap_ctx_delete(struct if_rmap_ctx *ctx);
/*
 * if_rmap_init —— 向指定 CLI 节点安装 route-map 命令。
 * node 取 RIP_NODE 装 ipv4 版命令，取 RIPNG_NODE 装 ipv6 版命令，其它值不做事。
 */
extern void if_rmap_init(int node);
/*
 * if_rmap_terminate —— 终止钩子，当前实现为空（无全局状态需要清理）。
 */
extern void if_rmap_terminate(void);
/*
 * if_rmap_hook_add —— 注册「记录新增 / 改动」回调，覆盖式赋值到 ctx->if_rmap_add_hook。
 * 注意：每个 ctx 只保留最后一个回调，重复注册会顶掉前一个。
 */
void if_rmap_hook_add(struct if_rmap_ctx *ctx,
		      void (*func)(struct if_rmap_ctx *ctx,
				   struct if_rmap *));
/*
 * if_rmap_hook_delete —— 注册「route-map 删除」回调，覆盖式赋值到 ctx->if_rmap_delete_hook。
 * 同样只保留最后一个回调。
 */
void if_rmap_hook_delete(struct if_rmap_ctx *ctx,
			 void (*func)(struct if_rmap_ctx *ctx,
				      struct if_rmap *));
/*
 * if_rmap_lookup —— 按接口名在 ctx 的 hash 表中查找记录。
 * 找到返回记录指针，找不到返回 NULL；只读，不创建。
 */
extern struct if_rmap *if_rmap_lookup(struct if_rmap_ctx *ctx,
				      const char *ifname);
/*
 * if_rmap_yang_modify_cb —— northbound 配置变化回调。
 * type 指明是 in 还是 out 方向；del 为真走 unset，否则走 set。
 * dnode 指向被改动的叶子（route-map 名），接口名从它的兄弟节点 "../interface" 取。
 */
extern void if_rmap_yang_modify_cb(struct if_rmap_ctx *ctx,
				   const struct lyd_node *dnode,
				   enum if_rmap_type type, bool del);
/*
 * if_rmap_yang_destroy_cb —— YANG 列表项（if-route-map）整体删除回调。
 * 一次性把该接口的 in 与 out 两个方向都 unset（不存在的方向会安全返回）。
 */
extern void if_rmap_yang_destroy_cb(struct if_rmap_ctx *ctx,
				    const struct lyd_node *dnode);
/*
 * config_write_if_rmap —— 把 ctx 内全部记录按 CLI 形式写回配置。
 * 返回写入的配置行数（供调用方判断是否有输出）。
 */
extern int config_write_if_rmap(struct vty *vty, struct if_rmap_ctx *ctx);
/*
 * cli_show_if_route_map —— northbound 配置展示回调：对一个 if-route-map 节点，
 * 分别打印存在的 in-route-map 与 out-route-map 两行配置。show_defaults 参数当前未用。
 */
void cli_show_if_route_map(struct vty *vty, const struct lyd_node *dnode,
			   bool show_defaults);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_IF_RMAP_H */
