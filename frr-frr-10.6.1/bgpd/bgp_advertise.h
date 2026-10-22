// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP advertisement and adjacency
 * Copyright (C) 1996, 97, 98, 99, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_advertise.h —— 出向通告(adj_out)与入向记录(adj_in)的数据模型
 * ============================================================================
 * 职责与定位：这是“同一前缀、同一批属性、向多个 peer 通告”这件事的数据结构层。
 *   它和 bgp_updgrp*.c 配套：updgrp 负责“按 peer 分组、批量编码”，本文件定义的是
 *   “某个前缀当前向某个组通告了什么”——即增量更新(是否需要发/撤)的判断依据。
 *
 * 三个层次（务必分清）：
 *   struct bgp_adj_out       —— 【出向，挂在 bgp_dest 上】一个前缀 × 一个 subgroup
 *                               的一条出向记录：记录了“我已经向这个组通告了哪个
 *                               attr / 哪些标签 / 哪个 addpath_tx_id”。前缀的内容
 *                               一变，就拿新 attr 与这里记录的旧 attr 比，决定要发
 *                               增量更新还是撤回。bgp_dest->adj_out 是一棵由它组成
 *                               的红黑树(RB)，按 subgroup 索引。
 *   struct bgp_advertise     —— 【待办条目，挂在 update_subgroup 的 FIFO 上】
 *                               “这个组的这个前缀还有事没做”的工作项；它把
 *                               (dest, subgroup, 新 attr) 组成一条，等 subgroup
 *                               的更新队列被消费时统一编码成 bpacket。
 *   struct bgp_advertise_attr—— 【属性聚合】把“同一个待通告 attr”绑定到一批
 *                               bgp_advertise 上，用 refcnt + 哈希复用，使相同属性
 *                               的批量更新只编码一次（与 bgp_attr_intern 同一思路）。
 *
 *   struct bgp_adj_in        —— 【入向，挂在 bgp_dest 上】记录“某 peer 曾经通告过
 *                               这个前缀”（peer/attr 快照）。它是 soft-reconfig
 *                               (软重配) 的基础：不改配置、不重收报文，仅凭这份
 *                               记录就能重新算一遍该 peer 带来的路由。
 *
 * 记忆要点：adj_out = “我发出去过什么”；adj_in = “我收到过什么”。
 * 两者都挂在 bgp_dest 上，都是 per-prefix 的。
 *
 * 建议阅读顺序：struct bgp_adj_out → struct bgp_advertise → struct bgp_advertise_attr
 *               → struct bgp_adj_in（再看 bgp_advertise.c 里的增删与 intern 逻辑）。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_ADVERTISE_H
#define _QUAGGA_BGP_ADVERTISE_H

#include "lib/typesafe.h"

PREDECL_DLIST(bgp_adv_fifo);

struct update_subgroup;
struct bgp_advertise;

PREDECL_DLIST(bgp_advertise_attr_fifo);

struct bgp_advertise_attr;

/* BGP advertise attribute.  */
/* ===========================================================================
 * struct bgp_advertise —— 一条“待通告工作项”（更新队列里的一个待办）
 * ---------------------------------------------------------------------------
 * 生命周期：当某前缀的最佳路径/属性发生变化，需要通知某个 update_subgroup 时，
 * 就取/建该 (dest, subgroup) 的 bgp_adj_out，并创建/复用它对应的 bgp_advertise，
 * 把它挂到 subgroup 的更新 FIFO 上；subgroup 被调度发送时再消费这些条目。
 * 它是“控制面变更”与“实际组包发送（bgp_updgrp_packet.c）”之间的缓冲。
 *
 * 成员含义：
 *   fifo  : 挂在 update_subgroup 的更新队列(bgp_adv_fifo)上的钩子——这是“待办”。
 *   item  : 挂在所属 bgp_advertise_attr 上的钩子（便于按属性批量处理）。
 *   dest  : 本条目对应的前缀节点。
 *   adj   : 反指回该前缀的出向记录 bgp_adj_out（1 对 1）。
 *   baa   : 所属的属性聚合对象 bgp_advertise_attr（同属性共享）。
 *   pathi : 要通告的那条路径 bgp_path_info（取 attr/标签/need-update 标志等）。
 * ===========================================================================
 */
struct bgp_advertise {
	/* FIFO for advertisement.  */
	struct bgp_adv_fifo_item fifo;

	/* FIFO for this item in the bgp_advertise_attr fifo */
	struct bgp_advertise_attr_fifo_item item;

	/* Prefix information.  */
	struct bgp_dest *dest;

	/* Reference pointer.  */
	struct bgp_adj_out *adj;

	/* Advertisement attribute.  */
	struct bgp_advertise_attr *baa;

	/* BGP info.  */
	struct bgp_path_info *pathi;
};

DECLARE_DLIST(bgp_advertise_attr_fifo, struct bgp_advertise, item);
DECLARE_DLIST(bgp_adv_fifo, struct bgp_advertise, fifo);

/* BGP advertise attribute.  */
/* ===========================================================================
 * struct bgp_advertise_attr —— 一批“属性相同的待通告项”的聚合体
 * ---------------------------------------------------------------------------
 * 目的：把使用同一个 attr 的多条 bgp_advertise 归到一起，这样在组包时只需为
 * 该 attr 编码一次 UPDATE 公共部分（AS_PATH/NEXT_HOP/团体等）即可复用于多个前缀，
 * 是 update-group 批量编码的前提。通过 bgp_advertise_attr_intern()/哈希复用，
 * 相同 attr 的对象全局唯一，靠 refcnt 决定何时释放。
 *
 * 成员：
 *   fifo   : 所有共享本 attr 的 bgp_advertise 的双向链表头。
 *   refcnt : 引用计数（同 attr 被多少条目使用），归零才真正释放。
 *   attr   : 【只读】被共享的属性对象（interned attr，绝不可就地修改）。
 * ===========================================================================
 */
struct bgp_advertise_attr {
	/* Head of advertisement pointer. */
	struct bgp_advertise_attr_fifo_head fifo;

	/* Reference counter.  */
	unsigned long refcnt;

	/* Attribute pointer to be announced.  */
	struct attr *attr;
};

/* BGP adjacency out.  */
/* ===========================================================================
 * struct bgp_adj_out —— 【出向】某个前缀向某个 update_subgroup 的通告状态
 * ---------------------------------------------------------------------------
 * 存放位置：bgp_dest->adj_out（红黑树，键 = subgroup），一个前缀 × 一个 subgroup
 * 一条。它回答的核心问题是：“这个前缀，我已经向这个组通告成什么样了？”
 *   - 对比这里记录的 attr 与新算出的最佳 attr：相同 → 无需发；
 *     不同 → 需要发增量更新(bgp_adj_out_set)或撤回(bgp_adj_out_unset)。
 *   - addpath_tx_id 用于 Add-Path（RFC7911）：本端为该前缀分配的发送侧路径 ID，
 *     同一前缀可向同一 peer 通告多条路径，各自一个 ID。
 *   - labels 记录已通告的 MPLS 标签栈（VPN/标签单播），变更时也要重发。
 *
 * 成员：
 *   adj_entry        : 红黑树节点（RB_ENTRY），是 bgp_dest->adj_out 的元素钩子。
 *   subgroup         : 该出向记录所属的更新子组（树键，由它决定“发给哪些 peer”）。
 *   subgrp_adj_train : 挂在 subgroup 的 adj 队列上的链钩（该组的所有出向记录排队）。
 *   dest             : 反指前缀节点。
 *   addpath_tx_id    : Add-Path 发送侧路径 ID（RFC7911）。
 *   attr             : 当前已通告出去的属性（旧值，用它与新值比较）。
 *   labels           : 已通告的 VPN 标签栈信息。
 *   adv              : 【待办】尚未发送的 bgp_advertise 条目；NULL 表示已同步/无待办。
 * ===========================================================================
 */
struct bgp_adj_out {
	/* RB Tree of adjacency entries */
	RB_ENTRY(bgp_adj_out) adj_entry;

	/* Advertised subgroup.  */
	struct update_subgroup *subgroup;

	/* Threading that makes the adj part of subgroup's adj queue */
	TAILQ_ENTRY(bgp_adj_out) subgrp_adj_train;

	/* Prefix information.  */
	struct bgp_dest *dest;

	uint32_t addpath_tx_id;

	/* Advertised attribute.  */
	struct attr *attr;

	/* VPN label information */
	struct bgp_labels *labels;

	/* Advertisement information.  */
	struct bgp_advertise *adv;
};

RB_HEAD(bgp_adj_out_rb, bgp_adj_out);
RB_PROTOTYPE(bgp_adj_out_rb, bgp_adj_out, adj_entry,
	     bgp_adj_out_compare);

/* BGP adjacency in. */
/* ===========================================================================
 * struct bgp_adj_in —— 【入向】某个前缀上“某 peer 曾通告过它”的记录
 * ---------------------------------------------------------------------------
 * 存放位置：bgp_dest->adj_in（单向链表，由本头的 BGP_ADJ_IN_ADD/DEL 维护）。
 * 一条 = (前缀, peer, addpath_rx_id)。它保存的是该 peer 最近一次通告该前缀时的
 * 属性与标签快照，用于 **soft-reconfig（软重配）** 与 **Route Refresh**：
 * 当 inbound 策略/路由映射改变时，不必向对端重发 Route Refresh 请求、也不必重收
 * 报文，直接遍历 adj_in 用快照重新跑一遍入向策略即可重建 bgp_path_info。
 * 注意：在“保存全部路径”之外，adj_in 的存在本身也代表该 peer 通告过该前缀。
 *
 * 成员：
 *   next/prev   : 前缀内链表钩子。
 *   peer        : 来源 peer。
 *   attr        : 该 peer 通告时的属性快照（interned，视为只读）。
 *   labels      : 该 peer 通告时携带的 VPN 标签栈。
 *   uptime      : 收到时间（monotime），供 show 与抖动判断使用。
 *   addpath_rx_id: Add-Path 接收侧路径 ID（区分同一 peer 的多条路径）。
 * ===========================================================================
 */
struct bgp_adj_in {
	/* Linked list pointer.  */
	struct bgp_adj_in *next;
	struct bgp_adj_in *prev;

	/* Received peer.  */
	struct peer *peer;

	/* Received attribute.  */
	struct attr *attr;

	/* VPN label information */
	struct bgp_labels *labels;

	/* timestamp (monotime) */
	time_t uptime;

	/* Addpath identifier */
	uint32_t addpath_rx_id;
};

/* BGP advertisement list.  */
/* 历史遗留：早期 FRR/Quagga 用这个结构做“更新/撤回”的同步队列（IBGP 同步
 * 时代），现在主流路径已改为 update-group + bgp_advertise FIFO，此结构仅在
 * 少数残留路径中使用，阅读时可基本忽略。 */
struct bgp_synchronize {
	struct bgp_adv_fifo_head update;
	struct bgp_adv_fifo_head withdraw;
};

/* BGP adjacency linked list.  */
/* 一组通用的“侵入式双向链表增删”宏：N 是宿主结构(如 bgp_dest)，A 是被插入的
 * 节点，TYPE 是宿主内链表头字段名(如 adj_in)。用宏参数化字段名，避免为每种
 * 链表重写一遍。
 *   BGP_PATH_INFO_ADD：头插；先把 A 接到当前表头之前，再让表头指向 A。
 *   BGP_PATH_INFO_DEL：摘除；维护前驱的 next 或表头本身。
 * 注意：这两个宏**不释放 A**，也不做锁处理，调用方负责生命周期。
 * BGP_ADJ_IN_ADD/DEL 是它们针对 bgp_dest->adj_in 的具名封装。 */
#define BGP_PATH_INFO_ADD(N, A, TYPE)                                          \
	do {                                                                   \
		(A)->prev = NULL;                                              \
		(A)->next = (N)->TYPE;                                         \
		if ((N)->TYPE)                                                 \
			(N)->TYPE->prev = (A);                                 \
		(N)->TYPE = (A);                                               \
	} while (0)

#define BGP_PATH_INFO_DEL(N, A, TYPE)                                          \
	do {                                                                   \
		if ((A)->next)                                                 \
			(A)->next->prev = (A)->prev;                           \
		if ((A)->prev)                                                 \
			(A)->prev->next = (A)->next;                           \
		else                                                           \
			(N)->TYPE = (A)->next;                                 \
	} while (0)

#define BGP_ADJ_IN_ADD(N, A) BGP_PATH_INFO_ADD(N, A, adj_in)
#define BGP_ADJ_IN_DEL(N, A) BGP_PATH_INFO_DEL(N, A, adj_in)

/* Prototypes.  */
/* 【入向 / 出向两个查询族】
 *   bgp_adj_out_lookup : 查“本端是否已向该 peer（其 subgroup）通告过该前缀”，
 *                        用于避免重复通告 / 判断撤回应发给谁。addpath_tx_id 限定
 *                        具体那条 Add-Path 路径。
 *   bgp_adj_in_set/unset/remove : 入向记录(adj_in)的建立、删除与摘链——在收到
 *                        某 peer 的 UPDATE 时建立，在对端撤回或 peer 关闭时删除。
 *                        注意 unset 会拿 dest 的地址（**dest）以便在 adj_in 清空
 *                        后可能联动处理 dest 的锁。
 *
 * 【bgp_advertise / bgp_advertise_attr 生命周期】
 *   *_new / *_free              : 裸对象分配与释放（不涉及哈希/计数）。
 *   bgp_advertise_attr_intern   : 按 attr 在哈希中查/建，refcnt++（同属性全局唯一）。
 *   bgp_advertise_attr_unintern : refcnt--，归零则交给 free 真正释放。
 *   bgp_advertise_add/delete    : 把 bgp_advertise 条目挂入/摘出其属性聚合体 baa。
 * 阅读提示：intern/unintern 的执行时机与持有者，决定了 attr 何时可被真正释放；
 * 这是理解“属性共享”与内存泄漏排查的关键。 */
extern bool bgp_adj_out_lookup(struct peer *peer, struct bgp_dest *dest,
			       uint32_t addpath_tx_id);
extern void bgp_adj_in_set(struct bgp_dest *dest, struct peer *peer,
			   struct attr *attr, uint32_t addpath_id,
			   struct bgp_labels *labels);
extern bool bgp_adj_in_unset(struct bgp_dest **dest, struct peer *peer,
			     uint32_t addpath_id);
extern void bgp_adj_in_remove(struct bgp_dest **dest, struct bgp_adj_in *bai);

extern unsigned int bgp_advertise_attr_hash_key(const void *p);
extern bool bgp_advertise_attr_hash_cmp(const void *p1, const void *p2);
extern void bgp_advertise_add(struct bgp_advertise_attr *baa,
			      struct bgp_advertise *adv);
extern struct bgp_advertise *bgp_advertise_new(void);
extern void bgp_advertise_free(struct bgp_advertise *adv);
extern struct bgp_advertise_attr *bgp_advertise_attr_intern(struct hash *hash,
							    struct attr *attr);
extern struct bgp_advertise_attr *bgp_advertise_attr_new(void);
extern void bgp_advertise_delete(struct bgp_advertise_attr *baa,
				 struct bgp_advertise *adv);
extern void bgp_advertise_attr_unintern(struct hash *hash,
					struct bgp_advertise_attr *baa);
extern void bgp_advertise_attr_free(struct bgp_advertise_attr *baa);

#endif /* _QUAGGA_BGP_ADVERTISE_H */
