// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP community, large-community aliasing.
 *
 * Copyright (C) 2021 Donatas Abraitis <donatas.abraitis@gmail.com>
 */

/* ============================================================================
 * 【文件导读】bgp_community_alias.c —— 团体(community)别名机制的实现
 * ============================================================================
 * 【它是什么】bgp_community_alias.h 里声明的接口的实现：维护“团体数值字符串 ↔
 * 人类可读别名”的**双向**映射，让配置与回显都能用别名。术语与结构见头文件注释。
 *
 * 【两张哈希表】本文件同时持有两张表，区别在“拿哪个字段当键”：
 *   bgp_ca_alias_hash     ：以 alias 为键（alias → community），对外可访问；
 *   bgp_ca_community_hash ：以 community 为键（community → alias），文件内 static。
 * 同一条 struct community_alias 会被**分别插入两张表各一次**（两份拷贝、键字段
 * 不同），这就是“同一份数据、两种组织”。因此 insert / delete 必须成对调用，只做
 * 一半会造成“正向能查、反向查不到”的不一致，且极难排查。
 *
 * 【四个函数的命名坑】函数名里的 ca_alias 指“用 alias 字段当键的那张表”，
 * ca_community 指“用 community 字段当键的那张表”——**判据是拿哪个字段当键**，
 * 不是“查到了什么内容”，不要按字面含义理解。
 *
 * 【三个转换接口】bgp_community2alias（数值 → 别名，显示用）、bgp_alias2community
 * （别名 → 数值，解析用）、bgp_alias2community_str（把整串“别名或裸数字”的团体串
 * 还原成纯数值串，返回**新分配**的字符串，需释放）。前两个返回的是表内字符串
 * （const，不可释放、不可改写）。
 *
 * 【典型数据流】配置里写别名 → bgp_alias2community_str 还原成数值 → 存入团体的
 * 数值表示；回显 / 日志里把数值 → bgp_community2alias 还原成别名显示。
 *
 * 【阅读顺序】两张表的 hash 回调 → init / finish → 成行的 insert / delete / lookup
 *            → 三个转换 → write → 补全。
 * ============================================================================
 */

#include "zebra.h"

#include "memory.h"
#include "lib/jhash.h"
#include "frrstr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_community_alias.h"

/* 【全局】两张映射表：前者以 alias 为键（对外可见），后者以 community 为键
 * （文件内私有）。二者在 init 中创建、在 finish 中销毁。 */
struct hash *bgp_ca_alias_hash;
static struct hash *bgp_ca_community_hash;

/* 【函数】以 community 为键的那张表的 hash 回调：对 ca->community 字符串做 jhash
 * （种子 0）。注意哈希按字符串常量长度 sizeof(ca->community) 计算，字符串较短时
 * 会连同零填充一起参与哈希，只要比较函数一致即可，无碍正确性。 */
static unsigned int bgp_ca_community_hash_key(const void *p)
{
	const struct community_alias *ca = p;

	return jhash(ca->community, sizeof(ca->community), 0);
}

/* 【函数】以 community 为键的表的比较回调：两表项的 community 字符串相等即同一键。 */
static bool bgp_ca_community_hash_cmp(const void *p1, const void *p2)
{
	const struct community_alias *ca1 = p1;
	const struct community_alias *ca2 = p2;

	return (strcmp(ca1->community, ca2->community) == 0);
}

/* 【函数】以 alias 为键的那张表的 hash 回调：对 ca->alias 做 jhash（种子 0）。 */
static unsigned int bgp_ca_alias_hash_key(const void *p)
{
	const struct community_alias *ca = p;

	return jhash(ca->alias, sizeof(ca->alias), 0);
}

/* 【函数】以 alias 为键的表的比较回调：两表项的 alias 字符串相等即同一键。 */
static bool bgp_ca_alias_hash_cmp(const void *p1, const void *p2)
{
	const struct community_alias *ca1 = p1;
	const struct community_alias *ca2 = p2;

	return (strcmp(ca1->alias, ca2->alias) == 0);
}

/* 【函数】hash 分配回调：为新键分配一份 community_alias 并整体拷贝（深拷贝）。
 * hash_get 需要新建表项时会调用它（两张表共用这一个回调，各存一份拷贝）。
 * 注意局部变量声明写成 struct communtiy_alias（源码原样拼写，疑为 community_alias
 * 的笔误）；因其仅作 void 指针中转、不被解引用，故照常编译，此处保持原样。 */
static void *bgp_community_alias_alloc(void *p)
{
	const struct community_alias *ca = p;
	struct communtiy_alias *new;

	new = XCALLOC(MTYPE_COMMUNITY_ALIAS, sizeof(struct community_alias));
	memcpy(new, ca, sizeof(struct community_alias));

	return new;
}

/* 【函数】进程级初始化：创建两张哈希表（community 键表与 alias 键表），分别注册
 * 各自的键/比较回调。bgpd 启动时调用一次。 */
void bgp_community_alias_init(void)
{
	bgp_ca_community_hash = hash_create(bgp_ca_community_hash_key,
					       bgp_ca_community_hash_cmp,
					       "BGP community alias (community)");
	bgp_ca_alias_hash =
		hash_create(bgp_ca_alias_hash_key, bgp_ca_alias_hash_cmp,
			    "BGP community alias (alias)");
}

/* 【函数】单条表项的释放回调，供 hash_clean_and_free 逐项清理时使用。 */
static void bgp_ca_free(void *ca)
{
	XFREE(MTYPE_COMMUNITY_ALIAS, ca);
}

/* 【函数】进程级收尾：清空并释放两张哈希表（逐项用 bgp_ca_free 释放表项）。 */
void bgp_community_alias_finish(void)
{
	hash_clean_and_free(&bgp_ca_community_hash, bgp_ca_free);
	hash_clean_and_free(&bgp_ca_alias_hash, bgp_ca_free);
}

/* 【函数】回显单条映射的回调：按 `bgp community alias <community> <alias>` 的格式
 * 输出一行，供 running-config 使用。 */
static void bgp_community_alias_show_iterator(struct hash_bucket *hb,
					      struct vty *vty)
{
	struct community_alias *ca = hb->data;

	vty_out(vty, "bgp community alias %s %s\n", ca->community, ca->alias);
}

/* 【函数】把 community 键表里的全部映射写成配置命令输出（遍历哈希并逐项回显）。
 * 恒返回 1 表示有内容需写入配置。 */
int bgp_community_alias_write(struct vty *vty)
{
	hash_iterate(bgp_ca_community_hash,
		     (void (*)(struct hash_bucket *,
			       void *))bgp_community_alias_show_iterator,
		     vty);
	return 1;
}

/* 【函数】把一条映射插入 community 键表（hash_get 不存在则用 alloc 回调新建）。
 * 必须与 bgp_ca_alias_insert 成对调用，否则两方向不一致。 */
void bgp_ca_community_insert(struct community_alias *ca)
{
	(void)hash_get(bgp_ca_community_hash, ca, bgp_community_alias_alloc);
}

/* 【函数】把一条映射插入 alias 键表。与 bgp_ca_community_insert 成对使用。 */
void bgp_ca_alias_insert(struct community_alias *ca)
{
	(void)hash_get(bgp_ca_alias_hash, ca, bgp_community_alias_alloc);
}

/* 【函数】从 community 键表删除一条映射并释放其表项。与 bgp_ca_alias_delete 成对
 * 使用。注意 hash_release 找不到时返回 NULL，XFREE 对 NULL 是安全的。 */
void bgp_ca_community_delete(struct community_alias *ca)
{
	struct community_alias *data = hash_release(bgp_ca_community_hash, ca);

	XFREE(MTYPE_COMMUNITY_ALIAS, data);
}

/* 【函数】从 alias 键表删除一条映射并释放其表项。与 bgp_ca_community_delete 成对
 * 使用。 */
void bgp_ca_alias_delete(struct community_alias *ca)
{
	struct community_alias *data = hash_release(bgp_ca_alias_hash, ca);

	XFREE(MTYPE_COMMUNITY_ALIAS, data);
}

/* 【函数】以 ca->community 为键在 community 键表里查找，返回命中的表项或 NULL。 */
struct community_alias *bgp_ca_community_lookup(struct community_alias *ca)
{
	return hash_lookup(bgp_ca_community_hash, ca);
}

/* 【函数】以 ca->alias 为键在 alias 键表里查找，返回命中的表项或 NULL。 */
struct community_alias *bgp_ca_alias_lookup(struct community_alias *ca)
{
	return hash_lookup(bgp_ca_alias_hash, ca);
}

/* 【函数】数值团体字符串 → 别名（显示用）。查不到同名的 community 表项时**原样
 * 返回入参 community**（这样调用方总能拿到可打印的字符串）。返回的是表内字符串
 * 或入参，均为 const，调用方不应释放或修改。 */
const char *bgp_community2alias(char *community)
{
	struct community_alias ca;
	struct community_alias *find;

	memset(&ca, 0, sizeof(ca));
	strlcpy(ca.community, community, sizeof(ca.community));

	find = bgp_ca_community_lookup(&ca);
	if (find)
		return find->alias;

	return community;
}

/* 【函数】别名 → 数值团体字符串（配置解析用）。查不到该别名时**原样返回入参
 * alias**（兼容用户直接写裸数字的情况）。返回表内字符串或入参，均 const。 */
const char *bgp_alias2community(char *alias)
{
	struct community_alias ca;
	struct community_alias *find;

	memset(&ca, 0, sizeof(ca));
	strlcpy(ca.alias, alias, sizeof(ca.alias));

	find = bgp_ca_alias_lookup(&ca);
	if (find)
		return find->community;

	return alias;
}

/* 【函数】把一整串（可能含空格分隔的多个）团体，逐个用 bgp_alias2community 还原
 * 成数值，再用空格拼回一个字符串。别名与裸数字混写也能正确处理。返回**新分配**
 * 的字符串（由 frrstr_join 产生，用 MTYPE_TMP），调用方负责释放；中途中转的
 * 分词数组也会被释放。供 bgp_clist.c 做 expanded 正则匹配前还原别名使用。 */
/* Communities structs have `->str` which is used
 * for vty outputs and extended BGP community lists
 * with regexp.
 * This is a helper to convert already aliased version
 * of communities into numerical-only format.
 */
char *bgp_alias2community_str(const char *str)
{
	char **aliases;
	char *comstr;
	int num, i;

	frrstr_split(str, " ", &aliases, &num);
	const char *communities[num];

	for (i = 0; i < num; i++)
		communities[i] = bgp_alias2community(aliases[i]);

	comstr = frrstr_join(communities, num, " ");

	for (i = 0; i < num; i++)
		XFREE(MTYPE_TMP, aliases[i]);
	XFREE(MTYPE_TMP, aliases);

	return comstr;
}

/* 【函数】hash 遍历回调：把每个 alias 复制一份塞进 vector（CLI 补全候选）。
 * data 指向 vector 指针。返回 1 表示继续遍历。 */
static int bgp_community_alias_vector_walker(struct hash_bucket *bucket,
					     void *data)
{
	vector *comps = data;
	struct community_alias *alias = bucket->data;

	vector_set(*comps, XSTRDUP(MTYPE_COMPLETION, alias->alias));

	return 1;
}

/* 【函数】CLI 补全：把已配置的全部别名收集为补全候选（用于别名令牌的 Tab 提示）。 */
static void bgp_community_alias_cmd_completion(vector comps,
					       struct cmd_token *token)
{
	hash_walk(bgp_ca_alias_hash, bgp_community_alias_vector_walker, &comps);
}

/* 【数据】补全处理器注册表：别名令牌（varname alias_name 与 tokenname
 * ALIAS_NAME 两种触发方式）都指向同一个补全回调，末尾用 {.completions = NULL}
 * 作结束哨兵。 */
static const struct cmd_variable_handler community_alias_handlers[] = {
	{.varname = "alias_name",
	 .completions = bgp_community_alias_cmd_completion},
	{.tokenname = "ALIAS_NAME",
	 .completions = bgp_community_alias_cmd_completion},
	{.completions = NULL}};

/* 【函数】把上面的补全表注册进 CLI 框架，使别名支持 Tab 补全。启动时调用一次。 */
void bgp_community_alias_command_completion_setup(void)
{
	cmd_variable_handler_register(community_alias_handlers);
}
