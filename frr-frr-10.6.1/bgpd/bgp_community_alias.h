// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP community, large-community aliasing.
 *
 * Copyright (C) 2021 Donatas Abraitis <donatas.abraitis@gmail.com>
 */
/* ============================================================================
 * 【文件导读】bgp_community_alias.h —— 团体(community)的“别名”机制
 * ============================================================================
 * 【解决什么问题】BGP 团体是裸数字（如 65001:100、或大团体 1:2:3）。运维记忆和
 * 沟通起来极不方便，于是 FRR 允许给常用团体起**别名**（如把 65001:100 叫
 * "BLACKHOLE" 或 "CUSTOMER-X"），配置里就能用别名代替数字。
 *
 * 【用的是“双向映射”而不是一张表】本文件维护 **两张哈希表**：
 *   bgp_ca_alias_hash  ： alias → community（按别名查团体），用于配置里解析别名；
 *   （另一张由实现持有）：community → alias（按团体查别名），用于回显/日志里把
 *                        数字显示成人类可读的名字。
 * 因此才有成对的 lookup/insert/delete 函数：**community 版**与 **alias 版**，
 * 名字里带 `community` 的是“以 community 为键”的那张表，别搞混。
 * 一个别名对应一个团体、反之亦然——这是**一对一**关系，把同一个别名映射到两个
 * 不同团体（或反之）属于配置冲突，实现里会做检查。
 *
 * 【三个查询/转换接口的差别（最容易用错的地方）】
 *   bgp_community2alias : 给**数值团体字符串**，返回别名字符串（找不到返回入参或 NULL）。
 *   bgp_alias2community : 给**别名**，返回数值团体字符串。
 *   bgp_alias2community_str : 与上一个相近，但按**字符串参数**处理——配置解析里
 *                        需要“无论用户写的是别名还是裸数字都能正常处理”，这个函数
 *                        负责这种兼容转换（返回值可能是新分配的字符串，注意释放）。
 *
 * 【配置回显】bgp_community_alias_write 把两张表写成 `bgp community alias` 命令，
 * 供 `show running-config` 使用。
 *
 * 【补全】command_completion_setup 把已配置的别名注册进 CLI 补全，让 Tab 能提示。
 *
 * 【阅读建议】struct community_alias → 成对的 lookup/insert/delete → 三个转换
 *            函数 → write/completion_setup。实现在 bgp_community_alias.c。
 * ============================================================================
 */

#include "bgpd/bgp_lcommunity.h"

#ifndef FRR_BGP_COMMUNITY_ALIAS_H
#define FRR_BGP_COMMUNITY_ALIAS_H

/* 【结构】community_alias —— 一条“团体 ↔ 别名”映射。**同一个结构体被两张表
 * 共用**（同一份数据插入两次，键分别是 community 和 alias），这样两个方向都能
 * 用哈希查；因此不要为两个方向各建一份对象，否则会出现副本不一致。
 *   community[LCOMMUNITY_SIZE * 3] : 团体的**可读字符串**形式。为什么乘 3——要能
 *       同时容纳“标准团体(如 65535:65535)”“扩展团体(RT/SoO 这类，更长)”以及
 *       大团体(如 65535:65535:65535)的最长写法，留足余量避免截断。
 *       注意这里存的是**字符串**而不是二进制团体值，比较走字符串比较。
 *   alias[BUFSIZ] : 用户起的别名（人类可读、用于配置与回显，长度上限宽松）。 */
struct community_alias {
	/* Human readable community string */
	char community[LCOMMUNITY_SIZE * 3];

	/* Human readable community alias */
	char alias[BUFSIZ];
};

/* 【全局】bgp_ca_alias_hash —— 其中一张表（alias → community）的句柄。另一张表
 * （community → alias）由实现文件内部持有，不对外暴露——所以外部代码通常只用
 * 下面这些函数而**不应直接操作这张哈希表**。
 * init/finish：两张表的创建与销毁（进程级各一次）。 */
extern struct hash *bgp_ca_alias_hash;
extern void bgp_community_alias_init(void);
extern void bgp_community_alias_finish(void);
/* 【函数】四个 lookup（两个方向 × 是否关心命名），返回命中的表项或 NULL：
 *   bgp_ca_alias_lookup     : 以 ca 的 **alias** 为键，在 alias 表里查——
 *                             典型用法“别名是否已存在”。
 *   bgp_ca_community_lookup : 以 ca 的 **community** 为键，在 community 表里查——
 *                             典型用法“这个团体是否已配过别名”。
 * 命名容易混：函数名里的 "ca_alias" 指**第一张表**，"ca_community" 指**第二张表**；
 * 判断依据是“用结构体里哪个字段当键”，不是“查到了什么”。
 * 另外两个 lookup（bgp_community2alias / bgp_alias2community）见文件末尾。 */
extern struct community_alias *bgp_ca_alias_lookup(struct community_alias *ca);
extern struct community_alias *
bgp_ca_community_lookup(struct community_alias *ca);
/* 【函数】两张表的插入与删除（**成对**调用才能保证两方向一致）。
 *   insert：把同一条 ca 分别插进 community 表与 alias 表。只插一张会导致
 *           “正向能查到、反向查不到”，这类不一致极难排查，务必成对。
 *   delete：从两张表分别摘除。删除时注意**先确认没有别处仍在使用**该表项
 *           （若插入时是同一份对象、由哈希表持有，则这里负责释放）。
 * 配置 `bgp community alias A B` 时插入；`no bgp community alias A B` 或改指
 * 向时先 delete 再 insert（避免一个别名同时挂在两个团体上）。 */
extern void bgp_ca_community_insert(struct community_alias *ca);
extern void bgp_ca_alias_insert(struct community_alias *ca);
extern void bgp_ca_community_delete(struct community_alias *ca);
extern void bgp_ca_alias_delete(struct community_alias *ca);
/* 【函数】配置回显：把两张表里的映射写成 `bgp community alias ...` 命令，供
 * `show running-config` 使用（顺序：先 alias 再 community，保证回显可被重新解析）。 */
extern int bgp_community_alias_write(struct vty *vty);
/* 【函数】三个转换（差别见文件头导读，最常用错的地方）：
 *   bgp_community2alias   : 数值团体字符串 → 别名（**显示用**：把 65001:100 打印成名字）。
 *   bgp_alias2community   : 别名 → 数值团体字符串（**配置解析用**）。
 *   bgp_alias2community_str: 同上但入参是配置里**原样**的字符串——因为用户可能
 *        直接写裸数字而不写别名，这个函数要能兼容两种情况；注意其返回值可能是
 *        **新分配的字符串**（查看实现确认），需要时记得释放。
 * 前两个返回的通常是表内字符串（const，不要释放、不要改）。 */
extern const char *bgp_community2alias(char *community);
extern const char *bgp_alias2community(char *alias);
extern char *bgp_alias2community_str(const char *str);
/* 【函数】把已配置的别名注册为 CLI 补全候选，使用户在配置里打别名时能 Tab 补全。
 * 配置增删别名后需要重新调用它刷新补全列表。 */
extern void bgp_community_alias_command_completion_setup(void);

#endif /* FRR_BGP_COMMUNITY_ALIAS_H */
