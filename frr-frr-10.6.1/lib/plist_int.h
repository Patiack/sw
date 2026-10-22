// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Prefix list internal definitions.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：prefix-list（前缀列表）内部数据结构
 *
 * 本头文件只供 plist.c 内部使用，对外（命令行、BGP 模块）暴露的接口在 plist.h。
 * 这里定义前缀列表的核心存储结构，理解它们是把 plist.c 匹配引擎读通的前提。
 *
 * 核心对象分三层：
 *   prefix_master  ：按地址族（afi）管理全部前缀列表的“总账”，见 prefix.h。
 *   prefix_list    ：一条前缀列表（例如 ip prefix-list FOO seq 5 permit 10.0.0.0/8 le 24）。
 *                    名字、描述、条目双向链表头尾、匹配计数，以及一棵加速匹配的 trie。
 *   prefix_list_entry：列表里的一条表项。seq 是序号，le / ge 是长度上下界约束，
 *                    prefix 是被匹配的网络前缀，type 是 permit 或 deny。
 *
 * 关键机制：
 *   - 每条表项既挂在 name 为主键的红黑树里，也挂在 prefix_list 内部的
 *     双向链表（head / tail 经 next / prev 串联）里，供命令行顺序遍历。
 *   - trie 字段指向一棵按二进制位构建的前缀 trie（struct pltrie_table），
 *     用于把“逐条线性比较”优化为按位下推查找，见 plist.c 中的 trie 相关实现。
 *   - next_best 是“向上找最佳匹配”的链指针：当某节点未命中时，沿该链上溯
 *     到可能命中的祖先表项。
 *   - installed 记录该表项当前是否已装进 trie 或链表，用于增量增删时避免重复操作。
 *
 * 记忆要点：本文件是“数据结构”，匹配算法在 plist.c；两者分工即“定义 vs 运算”。
 */

#ifndef _QUAGGA_PLIST_INT_H
#define _QUAGGA_PLIST_INT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * struct pltrie_table 是不透明（opaque）声明。
 * 真正的定义放在 plist.c 内部，外部只需持有它的指针，
 * 这样 trie 的具体布局可以不暴露给包含本头文件的代码。
 */
struct pltrie_table;

/*
 * PREDECL_RBTREE_UNIQ(plist) 由 lib/typesafe.h 展开出 plist 专属的红黑树类型
 * （元素唯一，键由宏生成的比较函数决定）。
 * 前缀列表正是以它作为“名字 -> struct prefix_list”的全局索引容器。
 */
PREDECL_RBTREE_UNIQ(plist);

/*
 * 一条前缀列表（prefix-list）的整体描述。
 * name         ：列表名字，同时是红黑树的主键。
 * desc         ：列表描述文本，仅用于展示。
 * master       ：所属的 prefix_master（按地址族归类），用于回到全局总账。
 * count        ：当前表项数量。
 * rangecount   ：参与范围匹配（带 le / ge）的表项计数，用于快速判断是否需要范围查表。
 * plist_item   ：红黑树节点锚点，把本结构挂进 master 的树里。
 * head / tail  ：表项双向链表的头和尾，按 seq 顺序维护，命令行遍历依赖它。
 * trie         ：加速匹配用的前缀 trie 根，可为 NULL（未构建时退化为线性匹配）。
 */
struct prefix_list {
	char *name;
	char *desc;

	struct prefix_master *master;

	int count;
	int rangecount;

	struct plist_item plist_item;

	struct prefix_list_entry *head;
	struct prefix_list_entry *tail;

	struct pltrie_table *trie;
};

/* Each prefix-list's entry. */
/*
 * 前缀列表中的一条表项。
 * seq      ：序号，决定匹配顺序（小的先匹配），也是列表内排序依据。
 * le / ge  ：前缀长度上下界约束。ge 是最小长度，le 是最大长度；
 *            例如 10.0.0.0/8 ge 16 le 24 表示匹配掩码长度在 16 到 24 之间的 10/8 子网。
 * type     ：PREFIX_PERMIT 或 PREFIX_DENY，命中后对路由的处置。
 * any      ：是否为“任意前缀”表项（相当于 0.0.0.0/0 全匹配），用于快速短路。
 * prefix   ：被比较的目标前缀（含地址族、地址、掩码长度）。
 * refcnt   ：引用计数，反映有多少路由/使用者挂在此表项上。
 * hitcnt   ：命中计数，用于展示“已匹配次数”等统计。
 * pl       ：回指所属的 prefix_list。
 * next/prev：双向链表指针，串成 seq 有序的表项链。
 * next_best：向上回溯找最佳匹配用的链指针（见文件导读）。
 * installed：本表项当前是否已装入 trie 或链表，增量更新时避免重复安装。
 */
struct prefix_list_entry {
	int64_t seq;

	int le;
	int ge;

	enum prefix_list_type type;

	bool any;
	struct prefix prefix;

	unsigned long refcnt;
	unsigned long hitcnt;

	struct prefix_list *pl;

	struct prefix_list_entry *next;
	struct prefix_list_entry *prev;

	/* up the chain for best match search */
	struct prefix_list_entry *next_best;

	/* Flag to track trie/list installation status. */
	bool installed;
};

/*
 * 下面四个函数由 plist.c 实现，供本库内部（如命令行增删表项、重建匹配结构）调用：
 *   prefix_list_entry_free          ：释放单个表项结构体本身。
 *   prefix_list_entry_delete2       ：从列表/trie 中注销一条表项（第二版删除语义）。
 *   prefix_list_entry_update_start  ：开始一次批量更新，暂时停用 trie 加速。
 *   prefix_list_entry_update_finish ：结束批量更新，重建 trie 等匹配结构。
 * 成对调用 start / finish 可避免每次改动都重建 trie，提升批量操作效率。
 */
extern void prefix_list_entry_free(struct prefix_list_entry *pentry);
extern void prefix_list_entry_delete2(struct prefix_list_entry *ple);
extern void prefix_list_entry_update_start(struct prefix_list_entry *ple);
extern void prefix_list_entry_update_finish(struct prefix_list_entry *ple);

#ifdef __cplusplus
}
#endif

#endif /* _QUAGGA_PLIST_INT_H */
