// SPDX-License-Identifier: GPL-2.0-or-later
/* Distribute list functions
 * Copyright (C) 1998, 1999 Kunihiro Ishiguro
 */

/*
 * ============================ 文件导读 ============================
 * distribute.c —— FRR distribute-list（分发/过滤列表）框架的实现
 *
 * 一、职责
 *   实现 distribute.h 声明的框架：为某个 VRF 维护「接口 -> 该接口 in/out
 *   两个方向的过滤列表」的哈希表，提供增删查、CLI 解析、配置展示，并把
 *   YANG（northbound）配置回调桥接到同一套逻辑上。
 *
 * 二、核心数据结构
 *   - struct distribute      一条记录：ifname + list[4] + prefix[4]。
 *   - struct distribute_ctx  上下文：disthash（ifname -> distribute）+ 加/删
 *     两个 hook + vrf。
 *   - enum distribute_type   {V4,V6} x {IN,OUT} 四象限，作为数组下标。
 *   - ifname 为 NULL 的记录是一条「全局默认」过滤项（对所有接口生效）。
 *
 * 三、内存类型（MTYPE）
 *   本文件定义了四个 static 内存类型：DISTRIBUTE_CTX（上下文）、
 *   DISTRIBUTE（记录）、DISTRIBUTE_IFNAME（接口名字符串）、
 *   DISTRIBUTE_NAME（列表名字符串），便于分类统计与排错。
 *
 * 四、与其他模块的关系
 *   - 依赖 hash.h 的通用哈希（用 ifname 作 key）、filter.h 的 filter_type。
 *   - CLI 解析用 command.h；YANG 回调用 northbound 的 nb_cb_* 与 lyd_* 接口。
 *   - 各协议（rip/ospf 等）创建 distribute_ctx 并挂 hook，把过滤接入收发。
 *
 * 五、阅读顺序
 *   先看生命周期与哈希三件套（new / free / free_if_empty / lookup / get /
 *   hash_make / cmp）-> set / unset / prefix_set / prefix_unset -> 方向换算
 *   distribute_direction -> parser / no_parser -> 展示 print / show / write
 *   -> northbound 一段（create_helper / destroy / leaf_update / 各 group_*）
 *   -> 末尾 Setup/Cleanup 的 delete / ctx_create。
 *
 * 六、易踩的坑
 *   1. distribute_free_if_empty 会在四个象限全空时把记录从哈希里删掉。
 *   2. distribute_lookup / distribute_get 用「临时 key + XSTRDUP」做哈希查
 *      找，查完立刻 XFREE，别在这两处漏内存。
 *   3. YANG 回调只在 NB_EV_APPLY 事件才真正改数据，其它事件直接返回。
 *   4. leaf_update 里判定前缀列表靠节点名首字母是否为 'p'（prefix），略隐晦。
 * ================================================================
 */
/* FRR 超级头。 */
#include <zebra.h>

/* 通用哈希表 API 与 struct hash_bucket（遍历哈希用）。 */
#include "hash.h"
/* 接口结构 struct interface。 */
#include "if.h"
/* enum filter_type（permit/deny）与访问列表机制。 */
#include "filter.h"
/* CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED 等命令返回值。 */
#include "command.h"
/* 本文件要实现的接口。 */
#include "distribute.h"
/* FRR 内存类型（MTYPE）与 XCALLOC / XSTRDUP / XFREE。 */
#include "memory.h"

/* 四个本文件私有内存类型：上下文、记录本体、接口名字符串、列表名字符串。 */
DEFINE_MTYPE_STATIC(LIB, DISTRIBUTE_CTX, "Distribute ctx");
DEFINE_MTYPE_STATIC(LIB, DISTRIBUTE, "Distribute list");
DEFINE_MTYPE_STATIC(LIB, DISTRIBUTE_IFNAME, "Dist-list ifname");
DEFINE_MTYPE_STATIC(LIB, DISTRIBUTE_NAME, "Dist-list name");

/* 分配并清零一个 distribute 记录（所有 list/prefix/ifname 均为 NULL）。 */
static struct distribute *distribute_new(void)
{
	return XCALLOC(MTYPE_DISTRIBUTE, sizeof(struct distribute));
}

/* Free distribute object. */
/* 释放一条记录：依次释放 ifname、四个 list 名、四个 prefix 名，最后释放
 * 记录本体。用各自的 MTYPE 释放，保证内存统计准确。 */
static void distribute_free(struct distribute *dist)
{
	int i = 0;

	XFREE(MTYPE_DISTRIBUTE_IFNAME, dist->ifname);

	for (i = 0; i < DISTRIBUTE_MAX; i++) {
		XFREE(MTYPE_DISTRIBUTE_NAME, dist->list[i]);
	}

	for (i = 0; i < DISTRIBUTE_MAX; i++) {
		XFREE(MTYPE_DISTRIBUTE_NAME, dist->prefix[i]);
	}

	XFREE(MTYPE_DISTRIBUTE, dist);
}

/* 若记录四个象限的 list/prefix 全为空，则把它从哈希表移除并释放；否则
 * 保持不变。用于 set/unset 后清理不再需要的空记录。 */
static void distribute_free_if_empty(struct distribute_ctx *ctx,
				     struct distribute *dist)
{
	int i;

	for (i = 0; i < DISTRIBUTE_MAX; i++)
		if (dist->list[i] != NULL || dist->prefix[i] != NULL)
			return;

	hash_release(ctx->disthash, dist);
	distribute_free(dist);
}

/* Lookup interface's distribute list. */
/* 按接口名查记录。哈希比较只看 ifname，因此构造一个临时 key（ifname 用
 * XSTRDUP 复制后立刻释放）去 hash_lookup。ifname 为 NULL 时查的是「全局
 * 默认」记录。找不到返回 NULL。 */
struct distribute *distribute_lookup(struct distribute_ctx *ctx,
				     const char *ifname)
{
	struct distribute key;
	struct distribute *dist;

	/* temporary reference */
	key.ifname = (ifname) ? XSTRDUP(MTYPE_DISTRIBUTE_IFNAME, ifname) : NULL;

	dist = hash_lookup(ctx->disthash, &key);

	XFREE(MTYPE_DISTRIBUTE_IFNAME, key.ifname);

	return dist;
}

/* 挂接「新增」钩子：把协议提供的回调存进 ctx。 */
void distribute_list_add_hook(struct distribute_ctx *ctx,
			      void (*func)(struct distribute_ctx *ctx,
					   struct distribute *))
{
	ctx->distribute_add_hook = func;
}

/* 挂接「删除」钩子。 */
void distribute_list_delete_hook(struct distribute_ctx *ctx,
				 void (*func)(struct distribute_ctx *ctx,
					      struct distribute *))
{
	ctx->distribute_delete_hook = func;
}

/* hash_get 的分配回调：按临时项 arg 复制出一条新记录（ifname 深拷贝）。 */
static void *distribute_hash_alloc(struct distribute *arg)
{
	struct distribute *dist;

	dist = distribute_new();
	if (arg->ifname)
		dist->ifname = XSTRDUP(MTYPE_DISTRIBUTE_IFNAME, arg->ifname);
	else
		dist->ifname = NULL;
	return dist;
}

/* Make new distribute list and push into hash. */
/* 取或建记录：hash_get 用临时 key 查找，命中则返回既有项，未命中则调用
 * distribute_hash_alloc 新建并插入。同样用临时 XSTRDUP 的 ifname，用完释放。 */
static struct distribute *distribute_get(struct distribute_ctx *ctx,
					 const char *ifname)
{
	struct distribute key;
	struct distribute *ret;

	/* temporary reference */
	key.ifname = (ifname) ? XSTRDUP(MTYPE_DISTRIBUTE_IFNAME, ifname) : NULL;

	ret = hash_get(ctx->disthash, &key,
		       (void *(*)(void *))distribute_hash_alloc);

	XFREE(MTYPE_DISTRIBUTE_IFNAME, key.ifname);

	return ret;
}

/* 哈希函数：以 ifname 字符串算哈希；ifname 为 NULL 时返回 0。 */
static unsigned int distribute_hash_make(const void *arg)
{
	const struct distribute *dist = arg;

	return dist->ifname ? string_hash_make(dist->ifname) : 0;
}

/* If two distribute-list have same value then return 1 else return
   0. This function is used by hash package. */
/* 哈希比较：两个记录 ifname 相等（含都为 NULL 的情形）即视为同一个 key。 */
static bool distribute_cmp(const struct distribute *dist1,
			  const struct distribute *dist2)
{
	if (dist1->ifname && dist2->ifname)
		if (strcmp(dist1->ifname, dist2->ifname) == 0)
			return true;
	if (!dist1->ifname && !dist2->ifname)
		return true;
	return false;
}

/* Set access-list name to the distribute list. */
/* 为某接口某象限设置 access-list 名：先取出/新建记录，替换对应 list[type]
 * （旧的先释放），再调用「新增」钩子通知协议把过滤应用到接口。 */
static void distribute_list_set(struct distribute_ctx *ctx,
				const char *ifname, enum distribute_type type,
				const char *alist_name)
{
	struct distribute *dist;

	dist = distribute_get(ctx, ifname);

	XFREE(MTYPE_DISTRIBUTE_NAME, dist->list[type]);
	dist->list[type] = XSTRDUP(MTYPE_DISTRIBUTE_NAME, alist_name);

	/* Apply this distribute-list to the interface. */
	(ctx->distribute_add_hook)(ctx, dist);
}

/* Unset distribute-list.  If matched distribute-list exist then
   return 1. */
/* 取消某接口某象限的 access-list：记录不存在、该象限本就为空、或名字对不
 * 上，都返回 0（未做任何事）；匹配则释放该名字、调「删除」钩子，并在记录
 * 全空时清掉它，返回 1。 */
static int distribute_list_unset(struct distribute_ctx *ctx,
				 const char *ifname,
				 enum distribute_type type,
				 const char *alist_name)
{
	struct distribute *dist;

	dist = distribute_lookup(ctx, ifname);
	if (!dist)
		return 0;

	if (!dist->list[type])
		return 0;
	if (strcmp(dist->list[type], alist_name) != 0)
		return 0;

	XFREE(MTYPE_DISTRIBUTE_NAME, dist->list[type]);

	/* Apply this distribute-list to the interface. */
	(ctx->distribute_delete_hook)(ctx, dist);

	/* If all dist are NULL, then free distribute list. */
	distribute_free_if_empty(ctx, dist);
	return 1;
}

/* Set access-list name to the distribute list. */
/* 与 distribute_list_set 类似，但设置的是 prefix-list 名（写 prefix[type]）。 */
static void distribute_list_prefix_set(struct distribute_ctx *ctx,
				       const char *ifname,
				       enum distribute_type type,
				       const char *plist_name)
{
	struct distribute *dist;

	dist = distribute_get(ctx, ifname);

	XFREE(MTYPE_DISTRIBUTE_NAME, dist->prefix[type]);
	dist->prefix[type] = XSTRDUP(MTYPE_DISTRIBUTE_NAME, plist_name);

	/* Apply this distribute-list to the interface. */
	(ctx->distribute_add_hook)(ctx, dist);
}

/* Unset distribute-list.  If matched distribute-list exist then
   return 1. */
/* 取消 prefix-list：语义与 distribute_list_unset 一致，操作对象是 prefix[]。 */
static int distribute_list_prefix_unset(struct distribute_ctx *ctx,
					const char *ifname,
					enum distribute_type type,
					const char *plist_name)
{
	struct distribute *dist;

	dist = distribute_lookup(ctx, ifname);
	if (!dist)
		return 0;

	if (!dist->prefix[type])
		return 0;
	if (strcmp(dist->prefix[type], plist_name) != 0)
		return 0;

	XFREE(MTYPE_DISTRIBUTE_NAME, dist->prefix[type]);

	/* Apply this distribute-list to the interface. */
	(ctx->distribute_delete_hook)(ctx, dist);

	/* If all dist are NULL, then free distribute list. */
	distribute_free_if_empty(ctx, dist);
	return 1;
}

/* 把 CLI 的方向字符串与地址族折算成四象限枚举：dir 首字母 'i' 为入、'o'
 * 为出（这正是命令里 in / out 的简写）。其它值触发 assert 和
 * __builtin_unreachable，让编译器知道不会走到这里。 */
static enum distribute_type distribute_direction(const char *dir, bool v4)
{
	if (dir[0] == 'i') {
		if (v4)
			return DISTRIBUTE_V4_IN;
		else
			return DISTRIBUTE_V6_IN;
	} else if (dir[0] == 'o') {
		if (v4)
			return DISTRIBUTE_V4_OUT;
		else
			return DISTRIBUTE_V6_OUT;
	}

	assert(!"Expecting in or out only, fix your code");

	__builtin_unreachable();
}

/* 配置解析入口：先由方向与族算出象限 type，再按 prefix 标志选 set 版本
 * （prefix-list 用 distribute_list_prefix_set，否则 distribute_list_set），
 * 用函数指针调用。总返回 CMD_SUCCESS。 */
int distribute_list_parser(struct distribute_ctx *ctx, bool prefix, bool v4,
			   const char *dir, const char *list, const char *ifname)
{
	enum distribute_type type = distribute_direction(dir, v4);

	void (*distfn)(struct distribute_ctx *, const char *,
		       enum distribute_type, const char *) =
		prefix ? &distribute_list_prefix_set : &distribute_list_set;

	distfn(ctx, ifname, type, list);

	return CMD_SUCCESS;
}


/* no 形式解析入口：按 prefix 选 unset 版本执行；返回 0 表示该配置本就不
 * 存在，此时若有 vty 就打印提示，并返回 CMD_WARNING_CONFIG_FAILED，否则
 * 返回 CMD_SUCCESS。 */
int distribute_list_no_parser(struct distribute_ctx *ctx, struct vty *vty,
			      bool prefix, bool v4, const char *dir,
			      const char *list, const char *ifname)
{
	enum distribute_type type = distribute_direction(dir, v4);
	int ret;

	int (*distfn)(struct distribute_ctx *, const char *,
		      enum distribute_type, const char *) =
		prefix ? &distribute_list_prefix_unset : &distribute_list_unset;


	ret = distfn(ctx, ifname, type, list);
	if (!ret) {
		if (vty)
			vty_out(vty, "distribute list doesn't exist\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	return CMD_SUCCESS;
}

/* 打印某象限的一个过滤名：tab[type] 有值就输出（多个之间用逗号连接，
 * prefix-list 前加 "(prefix-list) "），返回 1 表示至少打印了一个；否则
 * 返回传入的 has_print 保持不变。辅助 config_show_distribute。 */
static int distribute_print(struct vty *vty, char *tab[], int is_prefix,
			    enum distribute_type type, int has_print)
{
	if (tab[type]) {
		vty_out(vty, "%s %s%s", has_print ? "," : "",
			is_prefix ? "(prefix-list) " : "", tab[type]);
		return 1;
	}
	return has_print;
}

/* 展示分发配置（show 命令）。先打印「全局默认」记录（ifname 为 NULL 的那
 * 条）的出方向过滤，再遍历哈希逐接口打印出方向；入方向重复同样的两段。
 * 遍历哈希用 size/index[]/->next 的手写桶循环。 */
int config_show_distribute(struct vty *vty, struct distribute_ctx *dist_ctxt)
{
	unsigned int i;
	int has_print = 0;
	struct hash_bucket *mp;
	struct distribute *dist;

	/* Output filter configuration. */
	dist = distribute_lookup(dist_ctxt, NULL);
	vty_out(vty, "  Outgoing update filter list for all interface is");
	has_print = 0;
	if (dist) {
		has_print = distribute_print(vty, dist->list, 0,
					     DISTRIBUTE_V4_OUT, has_print);
		has_print = distribute_print(vty, dist->prefix, 1,
					     DISTRIBUTE_V4_OUT, has_print);
		has_print = distribute_print(vty, dist->list, 0,
					     DISTRIBUTE_V6_OUT, has_print);
		has_print = distribute_print(vty, dist->prefix, 1,
					     DISTRIBUTE_V6_OUT, has_print);
	}
	if (has_print)
		vty_out(vty, "\n");
	else
		vty_out(vty, " not set\n");

	for (i = 0; i < dist_ctxt->disthash->size; i++)
		for (mp = dist_ctxt->disthash->index[i]; mp; mp = mp->next) {
			dist = mp->data;
			if (dist->ifname) {
				vty_out(vty, "    %s filtered by",
					dist->ifname);
				has_print = 0;
				has_print = distribute_print(vty, dist->list, 0,
							     DISTRIBUTE_V4_OUT,
							     has_print);
				has_print = distribute_print(
					vty, dist->prefix, 1, DISTRIBUTE_V4_OUT,
					has_print);
				has_print = distribute_print(vty, dist->list, 0,
							     DISTRIBUTE_V6_OUT,
							     has_print);
				has_print = distribute_print(
					vty, dist->prefix, 1, DISTRIBUTE_V6_OUT,
					has_print);
				if (has_print)
					vty_out(vty, "\n");
				else
					vty_out(vty, " nothing\n");
			}
		}


	/* Input filter configuration. */
	dist = distribute_lookup(dist_ctxt, NULL);
	vty_out(vty, "  Incoming update filter list for all interface is");
	has_print = 0;
	if (dist) {
		has_print = distribute_print(vty, dist->list, 0,
					     DISTRIBUTE_V4_IN, has_print);
		has_print = distribute_print(vty, dist->prefix, 1,
					     DISTRIBUTE_V4_IN, has_print);
		has_print = distribute_print(vty, dist->list, 0,
					     DISTRIBUTE_V6_IN, has_print);
		has_print = distribute_print(vty, dist->prefix, 1,
					     DISTRIBUTE_V6_IN, has_print);
	}
	if (has_print)
		vty_out(vty, "\n");
	else
		vty_out(vty, " not set\n");

	for (i = 0; i < dist_ctxt->disthash->size; i++)
		for (mp = dist_ctxt->disthash->index[i]; mp; mp = mp->next) {
			dist = mp->data;
			if (dist->ifname) {
				vty_out(vty, "    %s filtered by",
					dist->ifname);
				has_print = 0;
				has_print = distribute_print(vty, dist->list, 0,
							     DISTRIBUTE_V4_IN,
							     has_print);
				has_print = distribute_print(
					vty, dist->prefix, 1, DISTRIBUTE_V4_IN,
					has_print);
				has_print = distribute_print(vty, dist->list, 0,
							     DISTRIBUTE_V6_IN,
							     has_print);
				has_print = distribute_print(
					vty, dist->prefix, 1, DISTRIBUTE_V6_IN,
					has_print);
				if (has_print)
					vty_out(vty, "\n");
				else
					vty_out(vty, " nothing\n");
			}
		}
	return 0;
}

/* Configuration write function. */
/* 把分发配置写成 vty 配置行（running-config 的一部分）。遍历哈希每条记录，
 * 分别输出所有 list[j] 与 prefix[j]：由下标 j 反推出方向（out/in）与是否
 * 加 "ipv6 " 前缀，拼成 "distribute-list ..."/"distribute-list prefix ..."
 * 命令行，返回写了多少行。 */
int config_write_distribute(struct vty *vty,
			    struct distribute_ctx *dist_ctxt)
{
	unsigned int i;
	int j;
	int output, v6;
	struct hash_bucket *mp;
	int write = 0;

	for (i = 0; i < dist_ctxt->disthash->size; i++)
		for (mp = dist_ctxt->disthash->index[i]; mp; mp = mp->next) {
			struct distribute *dist;

			dist = mp->data;

			for (j = 0; j < DISTRIBUTE_MAX; j++)
				if (dist->list[j]) {
					output = j == DISTRIBUTE_V4_OUT
						 || j == DISTRIBUTE_V6_OUT;
					v6 = j == DISTRIBUTE_V6_IN
					     || j == DISTRIBUTE_V6_OUT;
					vty_out(vty,
						" %sdistribute-list %s %s %s\n",
						v6 ? "ipv6 " : "",
						dist->list[j],
						output ? "out" : "in",
						dist->ifname ? dist->ifname
							     : "");
					write++;
				}

			for (j = 0; j < DISTRIBUTE_MAX; j++)
				if (dist->prefix[j]) {
					output = j == DISTRIBUTE_V4_OUT
						 || j == DISTRIBUTE_V6_OUT;
					v6 = j == DISTRIBUTE_V6_IN
					     || j == DISTRIBUTE_V6_OUT;
					vty_out(vty,
						" %sdistribute-list prefix %s %s %s\n",
						v6 ? "ipv6 " : "",
						dist->prefix[j],
						output ? "out" : "in",
						dist->ifname ? dist->ifname
							     : "");
					write++;
				}
		}
	return write;
}

/* Northbound 区块：YANG 配置回调，把 CLI/northbound 的改动桥接到上面的
 * set/unset 逻辑。 */
/* ---------- */
/* Northbound */
/* ---------- */

/* YANG 创建回调的辅助：创建分发项节点时，把 ctx 记为 dnode 的「运行态
 * 关联对象」，后续 modify/destroy 回调可据此取回 ctx。 */
int group_distribute_list_create_helper(
	struct nb_cb_create_args *args, struct distribute_ctx *ctx)
{
	nb_running_set_entry(args->dnode, ctx);
	return NB_OK;
}

/* 下面这些回调作用的 YANG 路径（以 ripd 为例）：分发列表下 in/out 各挂
 * access-list 或 prefix-list 叶子。 */
/*
 * XPath: /frr-ripd:ripd/instance/distribute-lists/distribute-list/{in,out}/{access,prefix}-list
 */

/* 前向声明：真正干活的下层函数，定义见下方。 */
static int distribute_list_leaf_update(const struct lyd_node *dnode,
				       int ip_version, bool no);

/* YANG 销毁回调：仅在 NB_EV_APPLY 事件执行（其它事件直接返回）。因为框架
 * 不保存分发项的地址族，只能把 in/out 下的 access-list / prefix-list 四个
 * 叶子都各按 v4 与 v6 尝试删除一遍（不存在的会被 unset 静默跳过），最后
 * 解除 dnode 的运行态关联。 */
int group_distribute_list_destroy(struct nb_cb_destroy_args *args)
{
	struct lyd_node *dnode;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	/*
	 * We don't keep the IP version of distribute-list anywhere, so we're
	 * trying to remove both. If one doesn't exist, it's simply skipped by
	 * the remove function.
	 */

	dnode = yang_dnode_get(args->dnode, "in/access-list");
	if (dnode) {
		distribute_list_leaf_update(dnode, 4, true);
		distribute_list_leaf_update(dnode, 6, true);
	}
	dnode = yang_dnode_get(args->dnode, "in/prefix-list");
	if (dnode) {
		distribute_list_leaf_update(dnode, 4, true);
		distribute_list_leaf_update(dnode, 6, true);
	}
	dnode = yang_dnode_get(args->dnode, "out/access-list");
	if (dnode) {
		distribute_list_leaf_update(dnode, 4, true);
		distribute_list_leaf_update(dnode, 6, true);
	}
	dnode = yang_dnode_get(args->dnode, "out/prefix-list");
	if (dnode) {
		distribute_list_leaf_update(dnode, 4, true);
		distribute_list_leaf_update(dnode, 6, true);
	}

	nb_running_unset_entry(args->dnode);
	return NB_OK;
}

/* 叶子节点的统一处理：从节点树逐级取父节点定位到 list / dir / 接口 key，
 * 从运行态取回 ctx；据节点名首字母判断是 access-list 还是 prefix-list
 * （'p' 开头即 prefix）；再据 no 调 no_parser 或 parser，名字与接口名都用
 * lyd_get_value 从 YANG 节点取出。 */
static int distribute_list_leaf_update(const struct lyd_node *dnode,
				       int ip_version, bool no)
{
	struct distribute_ctx *ctx;
	struct lyd_node *dir_node = lyd_parent(dnode);
	struct lyd_node *list_node = lyd_parent(dir_node);
	struct lyd_node *intf_key = lyd_child(list_node);
	bool ipv4 = ip_version == 4 ? true : false;
	bool prefix;

	ctx = nb_running_get_entry_non_rec(list_node, NULL, false);

	prefix = dnode->schema->name[0] == 'p' ? true : false;
	if (no)
		distribute_list_no_parser(ctx, NULL, prefix, ipv4,
					  dir_node->schema->name,
					  lyd_get_value(dnode),
					  lyd_get_value(intf_key));
	else
		distribute_list_parser(ctx, prefix, ipv4,
				       dir_node->schema->name,
				       lyd_get_value(dnode),
				       lyd_get_value(intf_key));
	return NB_OK;
}

/* 叶子修改回调：仅 NB_EV_APPLY 时生效，转调 leaf_update（no=false）。 */
static int distribute_list_leaf_modify(struct nb_cb_modify_args *args,
				       int ip_version)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;
	return distribute_list_leaf_update(args->dnode, ip_version, false);
}

/* 叶子销毁回调：仅 NB_EV_APPLY 时生效，转调 leaf_update（no=true）。 */
static int distribute_list_leaf_destroy(struct nb_cb_destroy_args *args,
					int ip_version)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;
	return distribute_list_leaf_update(args->dnode, ip_version, true);
}

/* 下面四个 group_* 是 YANG 回调的实际入口，只是把地址族（4 / 6）固定后
 * 转调通用实现；IPv4 与 IPv6 各一套 modify+destroy。 */
int group_distribute_list_ipv4_modify(struct nb_cb_modify_args *args)
{
	return distribute_list_leaf_modify(args, 4);
}
int group_distribute_list_ipv4_destroy(struct nb_cb_destroy_args *args)
{
	return distribute_list_leaf_destroy(args, 4);
}
int group_distribute_list_ipv6_modify(struct nb_cb_modify_args *args)
{
	return distribute_list_leaf_modify(args, 6);
}
int group_distribute_list_ipv6_destroy(struct nb_cb_destroy_args *args)
{
	return distribute_list_leaf_destroy(args, 6);
}

/* 叶子节点的 cli_show：把一条分发配置还原成可读命令行。同样按节点名首
 * 字母判断 access / prefix，并据 ip_version 决定是否加 "ipv6 "，输出
 * "distribute-list [prefix ]<name> <in|out> <ifname>"。 */
static int distribute_list_leaf_cli_show(struct vty *vty,
					 const struct lyd_node *dnode,
					 int ip_version)
{
	struct lyd_node *dir_node = lyd_parent(dnode);
	struct lyd_node *list_node = lyd_parent(dir_node);
	struct lyd_node *intf_key = lyd_child(list_node);
	bool ipv6 = ip_version == 6 ? true : false;
	bool prefix;

	prefix = dnode->schema->name[0] == 'p' ? true : false;
	vty_out(vty,
		" %sdistribute-list %s%s %s %s\n",
		ipv6 ? "ipv6 " : "",
		prefix ? "prefix " : "",
		lyd_get_value(dnode),
		dir_node->schema->name,
		lyd_get_value(intf_key));

	return NB_OK;
}

/* IPv4 / IPv6 的 cli_show 包装：固定地址族后转调同一实现。 */
void group_distribute_list_ipv4_cli_show(struct vty *vty,
					 const struct lyd_node *dnode,
					 bool show_defaults)
{
	distribute_list_leaf_cli_show(vty, dnode, 4);
}
void group_distribute_list_ipv6_cli_show(struct vty *vty,
					 const struct lyd_node *dnode,
					 bool show_defaults)
{
	distribute_list_leaf_cli_show(vty, dnode, 6);
}

/* 创建与销毁区块：上下文的生命周期管理。 */
/* ------------- */
/* Setup/Cleanup */
/* ------------- */

/* 销毁分发上下文：清空并释放 disthash（每条记录经 distribute_free），
 * 再释放 ctx 本体。参数是二级指针。 */
void distribute_list_delete(struct distribute_ctx **ctx)
{
	hash_clean_and_free(&(*ctx)->disthash,
			    (void (*)(void *))distribute_free);

	XFREE(MTYPE_DISTRIBUTE_CTX, (*ctx));
}

/* Initialize distribute list container */
/* 创建分发上下文：分配 ctx，记下 vrf，并建哈希表（key 用
 * distribute_hash_make，比较用 distribute_cmp），返回之。 */
struct distribute_ctx *distribute_list_ctx_create(struct vrf *vrf)
{
	struct distribute_ctx *ctx;

	ctx = XCALLOC(MTYPE_DISTRIBUTE_CTX, sizeof(struct distribute_ctx));
	ctx->vrf = vrf;
	ctx->disthash =
		hash_create(distribute_hash_make,
			    (bool (*)(const void *, const void *))distribute_cmp,
			    NULL);
	return ctx;
}
