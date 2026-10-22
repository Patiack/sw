// SPDX-License-Identifier: GPL-2.0-or-later
/* route-map for interface.
 * Copyright (C) 1999 Kunihiro Ishiguro
 * Copyright (C) 2023 LabN Consulting, L.L.C.
 */

/*
 * if_rmap.c —— 接口级 route-map 绑定表的实现
 *
 * 本文件实现 if_rmap.h 里声明的那套「接口 + 方向 -> route-map 名」数据结构，
 * 主要给 RIP / RIPNG 使用（`route-map ... <in|out> IFNAME`）。
 *
 * 数据组织：
 *   每个 struct if_rmap_ctx 持有一个 hash 表（ifrmaphash），
 *   key 是接口名字符串，value 是 struct if_rmap，
 *   记录内 routemap[IF_RMAP_IN] / routemap[IF_RMAP_OUT] 分别存两个方向的 map 名。
 *
 * 配置路径（northbound / YANG）：
 *   CLI 命令 -> if_route_map_handler 计算 xpath 并 enqueue 变更
 *           -> northbound 触发 if_rmap_yang_modify_cb / if_rmap_yang_destroy_cb
 *           -> if_rmap_set / if_rmap_unset 真正读写 hash 表
 *           -> 调用 ctx 上的 add / delete 钩子通知使用方（如 ripd 重建过滤）。
 *
 * 内存：
 *   记录里的 ifname、routemap[] 字符串都用 MTYPE_IF_RMAP_NAME 拷贝持有，
 *   释放记录时必须先释放这些字符串，避免泄漏。
 */

#include <zebra.h>

#include "hash.h"
#include "command.h"
#include "memory.h"
#include "if.h"
#include "if_rmap.h"
#include "northbound_cli.h"

#include "lib/if_rmap_clippy.c"

/*
 * 以下四个内存类型说明：
 * IF_RMAP_CTX      : struct if_rmap_ctx 本体。
 * IF_RMAP_CTX_NAME : ctx->name 字符串。
 * IF_RMAP          : struct if_rmap 本体。
 * IF_RMAP_NAME     : 记录里的 ifname 与 routemap[] 字符串。
 */
DEFINE_MTYPE_STATIC(LIB, IF_RMAP_CTX, "Interface route map container");
DEFINE_MTYPE_STATIC(LIB, IF_RMAP_CTX_NAME,
		    "Interface route map container name");
DEFINE_MTYPE_STATIC(LIB, IF_RMAP, "Interface route map");
DEFINE_MTYPE_STATIC(LIB, IF_RMAP_NAME, "I.f. route map name");

/*
 * if_rmap_new —— 分配并清零一条 struct if_rmap。
 * 初始时 ifname 与两个方向的 routemap 均为 NULL，由调用方按需填充（如 if_rmap_hash_alloc）。
 */
static struct if_rmap *if_rmap_new(void)
{
	struct if_rmap *new;

	new = XCALLOC(MTYPE_IF_RMAP, sizeof(struct if_rmap));

	return new;
}

/*
 * if_rmap_free —— 释放一条记录：先释放 ifname 与两个方向的 routemap 字符串，
 * 再释放结构本体。XFREE 对 NULL 安全，所以未配置的方向不会出问题。
 * 注意 ifname 是 const char *，此处需要强转掉 const 才能交给 XFREE。
 */
static void if_rmap_free(struct if_rmap *if_rmap)
{
	char *no_const_ifname = (char *)if_rmap->ifname;

	XFREE(MTYPE_IF_RMAP_NAME, no_const_ifname);

	XFREE(MTYPE_IF_RMAP_NAME, if_rmap->routemap[IF_RMAP_IN]);
	XFREE(MTYPE_IF_RMAP_NAME, if_rmap->routemap[IF_RMAP_OUT]);

	XFREE(MTYPE_IF_RMAP, if_rmap);
}

/*
 * if_rmap_lookup —— 按接口名查记录，只查不建。
 * 用栈上临时 key（只填 ifname）去 hash_lookup；命中返回记录指针，未命中返回 NULL。
 */
struct if_rmap *if_rmap_lookup(struct if_rmap_ctx *ctx, const char *ifname)
{
	struct if_rmap key = {.ifname = ifname};
	struct if_rmap *if_rmap;

	if_rmap = hash_lookup(ctx->ifrmaphash, &key);

	return if_rmap;
}

/*
 * if_rmap_hook_add —— 注册记录新增 / 改动回调。
 * 直接覆盖 ctx->if_rmap_add_hook，一个 ctx 同时只有一个回调生效。
 */
void if_rmap_hook_add(struct if_rmap_ctx *ctx,
		      void (*func)(struct if_rmap_ctx *ctx, struct if_rmap *))
{
	ctx->if_rmap_add_hook = func;
}

/*
 * if_rmap_hook_delete —— 注册 route-map 删除回调。
 * 同样覆盖式写入 ctx->if_rmap_delete_hook。
 */
void if_rmap_hook_delete(struct if_rmap_ctx *ctx,
			 void (*func)(struct if_rmap_ctx *ctx,
				      struct if_rmap *))
{
	ctx->if_rmap_delete_hook = func;
}

/*
 * if_rmap_hash_alloc —— hash_get 的「缺省节点分配」回调。
 * arg 是指向临时 key（含 ifname）的指针；这里新建一条记录并复制 ifname。
 * 只复制 ifname，不复制 routemap（新记录两个方向都为空）。
 */
static void *if_rmap_hash_alloc(void *arg)
{
	struct if_rmap *ifarg = (struct if_rmap *)arg;
	struct if_rmap *if_rmap;

	if_rmap = if_rmap_new();
	if_rmap->ifname = XSTRDUP(MTYPE_IF_RMAP_NAME, ifarg->ifname);

	return if_rmap;
}

/*
 * if_rmap_get —— 取记录，取不到就建。
 * hash_get 命中则返回已有记录；未命中则调用 if_rmap_hash_alloc 新建并插入。
 * 保证返回值非 NULL（hash_get 在分配失败时会直接终止进程）。
 */
static struct if_rmap *if_rmap_get(struct if_rmap_ctx *ctx, const char *ifname)
{
	struct if_rmap key = {.ifname = ifname};
	struct if_rmap *ret;

	ret = hash_get(ctx->ifrmaphash, &key, if_rmap_hash_alloc);

	return ret;
}

/*
 * if_rmap_hash_make —— hash 表散列函数，直接对接口名做字符串 hash。
 * data 实际是 struct if_rmap *，用它的 ifname 计算桶号。
 */
static unsigned int if_rmap_hash_make(const void *data)
{
	const struct if_rmap *if_rmap = data;

	return string_hash_make(if_rmap->ifname);
}

/*
 * if_rmap_hash_cmp —— hash 表的相等判断，按接口名逐字节比较。
 * arg1 可能是表内记录，也可能是查询用的临时 key，两者都只用到 ifname 字段。
 */
static bool if_rmap_hash_cmp(const void *arg1, const void *arg2)
{
	const struct if_rmap *if_rmap1 = arg1;
	const struct if_rmap *if_rmap2 = arg2;

	return strcmp(if_rmap1->ifname, if_rmap2->ifname) == 0;
}

/*
 * if_rmap_set —— 设置某接口某方向的 route-map 名。
 * type 只能是 IF_RMAP_IN 或 IF_RMAP_OUT（否则断言失败）。
 * 流程：取（或新建）记录 -> 释放旧 map 名 -> 复制新 map 名 -> 触发 add 钩子。
 * 注意：即使新旧名字相同也会重建字符串并回调钩子，钩子实现需能容忍重复通知。
 */
static void if_rmap_set(struct if_rmap_ctx *ctx, const char *ifname,
			enum if_rmap_type type, const char *routemap_name)
{
	struct if_rmap *if_rmap = if_rmap_get(ctx, ifname);

	assert(type == IF_RMAP_IN || type == IF_RMAP_OUT);
	XFREE(MTYPE_IF_RMAP_NAME, if_rmap->routemap[type]);
	if_rmap->routemap[type] = XSTRDUP(MTYPE_IF_RMAP_NAME, routemap_name);

	if (ctx->if_rmap_add_hook)
		(ctx->if_rmap_add_hook)(ctx, if_rmap);
}

/*
 * if_rmap_unset —— 取消某接口某方向的 route-map 配置。
 * 记录不存在、或该方向本就为空，直接返回（幂等）。
 * 否则释放该方向的 map 名 -> 触发 delete 钩子 -> 若两个方向都空了，
 * 再把记录从 hash 表摘除并整体释放（避免留下空壳记录）。
 * 注意钩子是在记录仍留在表中时被调用的（可能还有另一方向）。
 */
static void if_rmap_unset(struct if_rmap_ctx *ctx, const char *ifname,
			  enum if_rmap_type type)
{
	struct if_rmap *if_rmap = if_rmap_lookup(ctx, ifname);

	if (!if_rmap)
		return;

	assert(type == IF_RMAP_IN || type == IF_RMAP_OUT);
	if (!if_rmap->routemap[type])
		return;

	XFREE(MTYPE_IF_RMAP_NAME, if_rmap->routemap[type]);

	if (ctx->if_rmap_delete_hook)
		ctx->if_rmap_delete_hook(ctx, if_rmap);

	if (if_rmap->routemap[IF_RMAP_IN] == NULL &&
	    if_rmap->routemap[IF_RMAP_OUT] == NULL) {
		hash_release(ctx->ifrmaphash, if_rmap);
		if_rmap_free(if_rmap);
	}
}

/*
 * if_route_map_handler —— route-map 命令的公共处理逻辑。
 * vty      : 当前命令行。
 * no       : true 表示这是 no 形式（删除），false 表示设置。
 * dir      : 本次方向字符串，"in" 或 "out"。
 * other_dir: 相反方向的字符串，删除时用来判断另一个方向是否还存在。
 * ifname   : 接口名。
 * route_map: route-map 名（删除时可为 NULL）。
 * 流程：算出目标叶子节点的 xpath，再 enqueue 变更并 apply。
 * 删除的细节：若删除后本接口两方向都空了，则改为删除整个列表项节点
 * （if-route-map[interface=...]），而不是只删叶子，否则会残留空列表项。
 * 返回 nb_cli_apply_changes 的结果（0 成功，非 0 表示命令处理失败）。
 */
static int if_route_map_handler(struct vty *vty, bool no, const char *dir,
				const char *other_dir, const char *ifname,
				const char *route_map)
{
	enum nb_operation op = no ? NB_OP_DESTROY : NB_OP_MODIFY;
	const struct lyd_node *dnode;
	char xpath[XPATH_MAXLEN];

	if (!no) {
		snprintf(
			xpath, sizeof(xpath),
			"./if-route-maps/if-route-map[interface='%s']/%s-route-map",
			ifname, dir);
	} else {
		/*
		 * If we are deleting the last policy for this interface,
		 * (i.e., no `in` or `out` policy). delete the interface list
		 * node instead.
		 */
		dnode = yang_dnode_get(vty->candidate_config->dnode,
				       VTY_CURR_XPATH);
		if (yang_dnode_existsf(
			    dnode,
			    "./if-route-maps/if-route-map[interface='%s']/%s-route-map",
			    ifname, other_dir)) {
			snprintf(
				xpath, sizeof(xpath),
				"./if-route-maps/if-route-map[interface='%s']/%s-route-map",
				ifname, dir);
		} else {
			/* both dir will be empty so delete the list node */
			snprintf(xpath, sizeof(xpath),
				 "./if-route-maps/if-route-map[interface='%s']",
				 ifname);
		}
	}
	nb_cli_enqueue_change(vty, xpath, op, route_map);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * if_ipv4_route_map —— ipv4 版设置命令：route-map ROUTE-MAP <in|out> IFNAME。
 * in 决定方向（选中 in 则为 "in"，否则 "out"），其余交给 if_route_map_handler。
 */
DEFPY_YANG(if_ipv4_route_map, if_ipv4_route_map_cmd,
	   "route-map ROUTE-MAP <in$in|out> IFNAME",
	   "Route map set\n"
	   "Route map name\n"
	   "Route map set for input filtering\n"
	   "Route map set for output filtering\n" INTERFACE_STR)
{
	const char *dir = in ? "in" : "out";
	const char *other_dir = in ? "out" : "in";

	return if_route_map_handler(vty, false, dir, other_dir, ifname,
				    route_map);
}

/*
 * no_if_ipv4_route_map —— ipv4 版删除命令：no route-map [ROUTE-MAP] <in|out> IFNAME。
 * no 位置传 true；ROUTE-MAP 可选，删除时不强制给出名字。
 */
DEFPY_YANG(no_if_ipv4_route_map, no_if_ipv4_route_map_cmd,
	   "no route-map [ROUTE-MAP] <in$in|out> IFNAME",
	   NO_STR
	   "Route map set\n"
	   "Route map name\n"
	   "Route map set for input filtering\n"
	   "Route map set for output filtering\n" INTERFACE_STR)
{
	const char *dir = in ? "in" : "out";
	const char *other_dir = in ? "out" : "in";

	return if_route_map_handler(vty, true, dir, other_dir, ifname,
				    route_map);
}

/*
 * CLI infra requires new handlers for ripngd
 */
/*
 * if_ipv6_route_map —— ipv6 版设置命令（RIPNG 用）。
 * 语义与 v4 版完全一致，仅命令安装的节点不同。
 */
DEFPY_YANG(if_ipv6_route_map, if_ipv6_route_map_cmd,
	   "route-map ROUTE-MAP <in$in|out> IFNAME",
	   "Route map set\n"
	   "Route map name\n"
	   "Route map set for input filtering\n"
	   "Route map set for output filtering\n" INTERFACE_STR)
{
	const char *dir = in ? "in" : "out";
	const char *other_dir = in ? "out" : "in";

	return if_route_map_handler(vty, false, dir, other_dir, ifname,
				    route_map);
}

/*
 * no_if_ipv6_route_map —— ipv6 版删除命令（RIPNG 用），语义同 v4 删除命令。
 */
DEFPY_YANG(no_if_ipv6_route_map, no_if_ipv6_route_map_cmd,
	   "no route-map [ROUTE-MAP] <in$in|out> IFNAME",
	   NO_STR
	   "Route map set\n"
	   "Route map name\n"
	   "Route map set for input filtering\n"
	   "Route map set for output filtering\n" INTERFACE_STR)
{
	const char *dir = in ? "in" : "out";
	const char *other_dir = in ? "out" : "in";

	return if_route_map_handler(vty, true, dir, other_dir, ifname,
				    route_map);
}

/*
 * cli_show_if_route_map —— 展示回调：打印一个 if-route-map 节点对应的配置。
 * dnode 是 if-route-map 列表项，其下可能有 in-route-map / out-route-map 叶子；
 * 哪个存在就打印哪一行。show_defaults 当前未使用。
 */
void cli_show_if_route_map(struct vty *vty, const struct lyd_node *dnode,
			   bool show_defaults)
{
	if (yang_dnode_exists(dnode, "in-route-map"))
		vty_out(vty, " route-map %s in %s\n",
			yang_dnode_get_string(dnode, "in-route-map"),
			yang_dnode_get_string(dnode, "interface"));
	if (yang_dnode_exists(dnode, "out-route-map"))
		vty_out(vty, " route-map %s out %s\n",
			yang_dnode_get_string(dnode, "out-route-map"),
			yang_dnode_get_string(dnode, "interface"));
}

/*
 * if_rmap_yang_modify_cb —— YANG 叶子（route-map 名）发生增删改时的回调。
 * dnode 指向该叶子节点，"../interface" 取到所属接口名。
 * del 为真则 unset（type 指明方向），否则 set 上新的 map 名。
 */
void if_rmap_yang_modify_cb(struct if_rmap_ctx *ctx,
			    const struct lyd_node *dnode,
			    enum if_rmap_type type, bool del)
{

	const char *mapname = yang_dnode_get_string(dnode, NULL);
	const char *ifname = yang_dnode_get_string(dnode, "../interface");

	if (del)
		if_rmap_unset(ctx, ifname, type);
	else
		if_rmap_set(ctx, ifname, type, mapname);
}

/*
 * if_rmap_yang_destroy_cb —— YANG if-route-map 列表项整体被删除时的回调。
 * 直接从 dnode 取 "interface"，把该接口 in / out 两个方向都 unset（幂等）。
 */
void if_rmap_yang_destroy_cb(struct if_rmap_ctx *ctx,
			     const struct lyd_node *dnode)
{
	const char *ifname = yang_dnode_get_string(dnode, "interface");
	if_rmap_unset(ctx, ifname, IF_RMAP_IN);
	if_rmap_unset(ctx, ifname, IF_RMAP_OUT);
}

/*
 * if_rmap_ctx_delete —— 销毁容器。
 * 先用 if_rmap_free 清空并释放 hash 表中的每条记录，再释放 name 与 ctx 本体。
 */
void if_rmap_ctx_delete(struct if_rmap_ctx *ctx)
{
	hash_clean_and_free(&ctx->ifrmaphash, (void (*)(void *))if_rmap_free);
	XFREE(MTYPE_IF_RMAP_CTX_NAME, ctx->name);
	XFREE(MTYPE_IF_RMAP_CTX, ctx);
}

/*
 * if_rmap_ctx_create —— 新建容器。
 * name 可选（vrf 名或其它的标识串），会被复制一份保存在 ctx->name。
 * 同时建一个初始容量 4 的 hash 表，指定散列与比较函数。
 * 返回新建的 ctx；调用方负责用 if_rmap_ctx_delete 释放。
 */
/* name is optional: either vrf name, or other */
struct if_rmap_ctx *if_rmap_ctx_create(const char *name)
{
	struct if_rmap_ctx *ctx;

	ctx = XCALLOC(MTYPE_IF_RMAP_CTX, sizeof(struct if_rmap_ctx));

	ctx->name = XSTRDUP(MTYPE_IF_RMAP_CTX_NAME, name);
	ctx->ifrmaphash =
		hash_create_size(4, if_rmap_hash_make, if_rmap_hash_cmp,
				 "Interface Route-Map Hash");
	return ctx;
}

/*
 * if_rmap_init —— 安装 CLI 命令。
 * 在 RIP_NODE 下安装 v4 的 set / no 命令；在 RIPNG_NODE 下安装 v6 的 set / no 命令；
 * 其它节点不做任何事。
 */
void if_rmap_init(int node)
{
	if (node == RIP_NODE) {
		install_element(RIP_NODE, &if_ipv4_route_map_cmd);
		install_element(RIP_NODE, &no_if_ipv4_route_map_cmd);
	} else if (node == RIPNG_NODE) {
		install_element(RIPNG_NODE, &if_ipv6_route_map_cmd);
		install_element(RIPNG_NODE, &no_if_ipv6_route_map_cmd);
	}
}

/*
 * if_rmap_terminate —— 终止钩子，当前为空实现（无全局状态需要回收）。
 */
void if_rmap_terminate(void)
{
}
