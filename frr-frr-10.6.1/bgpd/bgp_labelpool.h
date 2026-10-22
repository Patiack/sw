// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Label Pool - Manage label chunk allocations from zebra asynchronously
 *
 * Copyright (C) 2018 LabN Consulting, L.L.C.
 */

/*
 * ============================================================================
 * 【文件导读】bgp_labelpool.h —— BGP 标签池的对外接口与核心数据结构
 * ============================================================================
 * 解决的问题：MPLS 转发需要本机“出标签”。20 位的标签空间由 zebra 的标签管理器
 * 统一管理，各进程不能自行乱取，必须向 zebra 申请一段连续的 **label chunk**，
 * 再自己在这段里分配。bgp_labelpool 就是 bgpd 侧的“标签池”：
 *   - 按需成批向 zebra 申请 chunk；
 *   - 在进程内部把 chunk 里的标签按用途分配出去（L3VPN 出标签、BGP-LU、
 *     每下一跳共享标签等）；
 *   - 用 ledger / 引用计数记账，保证“还有人用就不释放”，全部释放后把 chunk
 *     归还 zebra；
 *   - 与 zebra 的连接是异步的：断线时允许继续用手上已分配的标签，重连后把这
 *     些标签全部作废并重新分配。
 *
 * 【对外接口分两类】
 *   1. 申请 / 释放：bgp_lp_get（异步申请，分配完成后调 cbfunc），以及
 *      bgp_lu_lp_release / bgp_vpn_lp_release / bgp_vpn_nh_lp_release（按类型释放）。
 *   2. 事件回调（由 bgp_zebra 在收到消息时调用）：bgp_lp_event_chunk（收到 chunk）、
 *      bgp_lp_event_zebra_up / _down（连接状态变化）。
 *
 * 【关键数据结构】
 *   struct labelpool —— 模块总状态（ledger / inuse / chunks / 待处理请求队列等）。
 *   struct bgp_label_per_nexthop_cache —— “每下一跳共享一个出标签”的缓存（以下一跳
 *      为键的 RB-tree），用于 L3VPN 出标签按下一跳聚合，多条路径共享同一标签。
 *
 * 建议阅读顺序：LP_TYPE_* → struct labelpool → 申请 / 释放接口 → 每下一跳缓存的接口。
 * ============================================================================
 */

#ifndef _FRR_BGP_LABELPOOL_H
#define _FRR_BGP_LABELPOOL_H

#include <zebra.h>

#include "mpls.h"

/* 【用途分类】bgp_lp_get 的 type 参数，用来区分同一个标签被谁申请、便于调试归类；
 * 也参与释放时的类型校验（见 .c 的 bgp_lp_release）。按需在末尾追加新取值即可。 */
/*
 * Types used in bgp_lp_get for debug tracking; add more as needed
 */
#define LP_TYPE_VRF	0x00000001
#define LP_TYPE_BGP_LU	0x00000002
#define LP_TYPE_NEXTHOP 0x00000003
#define LP_TYPE_BGP_L3VPN_BIND 0x00000004

/* 【前置声明】lp_fifo —— 请求 FIFO 的侵入式链表类型（元素见 .c 的 struct lp_fifo）。
 * 用侵入式链表是为了让 FIFO 元素能被稳定地引用 / 搬运而不额外分配节点。 */
PREDECL_LIST(lp_fifo);

/* 【结构】struct labelpool —— 标签池模块的全部状态（全局单实例，见 .c 里的 static lp）。
 *   ledger  : 所有“已被受理的标签请求”，键=labelid(请求者唯一标识)，值=struct lp_lcb。
 *             即使标签尚未真正分配也会在此登记；处理重复请求、释放校验都靠它。
 *   inuse   : 所有“当前已分配出去”的标签，键=标签值，值=labelid，用于快速反查。
 *   chunks  : zebra 已授予的连续标签段(lp_chunk)链表，分配 / 释放就在这些段里做。
 *   requests: FIFO，装着“已受理但本池暂无空闲标签、正等 zebra 给新 chunk”的请求。
 *   callback_q : 延迟回调工作队列——把标签分配结果通过 cbfunc 通知请求者，避免在
 *                消息处理路径里直接回调（防止重入）。
 *   pending_count  : 已向 zebra 申请但尚未到账的标签总数（估算还需不需要再申请）。
 *   reconnect_count: zebra 重连次数（诊断用）。
 *   next_chunksize : 下次向 zebra 申请的 chunk 大小，逐次翻倍、有上限（见 .c 常量）。 */
struct labelpool {
	struct skiplist		*ledger;	/* all requests */
	struct skiplist		*inuse;		/* individual labels */
	struct list		*chunks;	/* granted by zebra */
	struct lp_fifo_head	requests;	/* blocked on zebra */
	struct work_queue	*callback_q;
	uint32_t		pending_count;	/* requested from zebra */
	uint32_t reconnect_count;		/* zebra reconnections */
	uint32_t next_chunksize;		/* request this many labels */
};

/* 【函数】bgp_lp_init —— 初始化标签池：建 ledger / inuse 跳表、chunks 链表、requests
 * FIFO 及回调工作队列。bgpd 启动时调用。pool 由调用方分配，模块用全局指针 lp 记住它。 */
extern void bgp_lp_init(struct event_loop *master, struct labelpool *pool);
extern void bgp_lp_finish(void);
/* 【函数】bgp_lp_get —— 向标签池申请一个标签（异步）。
 *   type    : 用途（LP_TYPE_*）；
 *   labelid : 请求者唯一标识，作为回调回传的句柄；
 *   vrf_id  : 所属 VRF；
 *   cbfunc  : 分配完成 / 失效时的回调（allocated=false 表示标签失效）。
 * 若本池有空闲标签，会立刻排队回调；否则登记请求，必要时向 zebra 申请新 chunk。
 * 注意：同一个 labelid 的重复申请会被识别、不会重复分配（见 .c 的 ledger 逻辑）。 */
extern void bgp_lp_get(int type, void *labelid, vrf_id_t vrf_id,
		       int (*cbfunc)(mpls_label_t label, void *labelid, bool allocated));

struct bgp_dest;
/* 【函数】bgp_lu_lp_release / bgp_vpn_lp_release / bgp_vpn_nh_lp_release —— 三种用途
 * 各自的释放接口：按 (label, labelid, type) 释放，并顺手清空请求者状态里的标签字段
 * （如 policy->tovpn_label）。释放时会校验类型，防止误释放别人的标签。 */
void bgp_lu_lp_release(struct bgp_dest *dest, mpls_label_t label);
struct vpn_policy;
void bgp_vpn_lp_release(struct vpn_policy *policy, mpls_label_t label);
struct bgp_mplsvpn_nh_label_bind_cache;
void bgp_vpn_nh_lp_release(struct bgp_mplsvpn_nh_label_bind_cache *bmnc,
			   mpls_label_t label);

/* 【事件回调】由 bgp_zebra 在收到 zebra 消息时调用：
 *   bgp_lp_event_chunk      : zebra 授予了连续标签段 [first,last]，收纳为新 chunk。
 *   bgp_lp_event_zebra_down : 与 zebra 断连——允许继续使用已分配标签（当前是空实现）。
 *   bgp_lp_event_zebra_up   : 与 zebra 重连——作废全部旧标签并请求重新分配。
 *   bgp_lp_vty_init         : 安装 show bgp labelpool ... 相关命令。 */
extern void bgp_lp_event_chunk(uint32_t first, uint32_t last);
extern void bgp_lp_event_zebra_down(void);
extern void bgp_lp_event_zebra_up(void);
extern void bgp_lp_vty_init(void);

struct bgp_label_per_nexthop_cache;
PREDECL_RBTREE_UNIQ(bgp_label_per_nexthop_cache);

/* 【函数】bgp_label_per_nexthop_cache_cmp —— 该 RB-tree 的比较函数，按下一跳前缀比较。 */
extern int
bgp_label_per_nexthop_cache_cmp(const struct bgp_label_per_nexthop_cache *a,
				const struct bgp_label_per_nexthop_cache *b);

/* 【结构】struct bgp_label_per_nexthop_cache —— 按“下一跳”聚合的出标签缓存。
 * 场景：L3VPN 里同一个下一跳的多个 VPN 前缀可共享同一个出标签，避免逐前缀申请。
 * 以 nexthop 为键组织成 RB-tree（见下方 DECLARE_RBTREE_UNIQ）。
 *   nexthop   : 树键，被缓存的下一跳地址。
 *   label     : 为这个下一跳计算 / 申请到的出标签。
 *   path_count: 当前引用该缓存的路径数（引用计数语义：归零即可释放）。
 *   nh        : 下一跳解析结果（来自 NHT），用于取出接口等信息下发给 zebra。
 *   paths     : 使用该标签的路径(bgp_path_info)链表，便于 detail 显示与失效处理。
 *   last_update: 上次更新时间，show 显示用。
 *   tree      : 所属缓存树的反向指针，释放时从树中摘除用。 */
struct bgp_label_per_nexthop_cache {

	/* RB-tree entry. */
	struct bgp_label_per_nexthop_cache_item entry;

	/* the nexthop is the key of the list */
	struct prefix nexthop;

	/* calculated label */
	mpls_label_t label;

	/* number of path_vrfs */
	unsigned int path_count;

	/* back pointer to bgp instance */
	struct bgp *to_bgp;

	/* copy a nexthop resolution from bgp nexthop tracking
	 * used to extract the interface nexthop
	 */
	struct nexthop *nh;

	/* list of path_vrfs using it */
	LIST_HEAD(path_lists, bgp_path_info) paths;

	time_t last_update;

	/* Back pointer to the cache tree this entry belongs to. */
	struct bgp_label_per_nexthop_cache_head *tree;
};

/* 【树类型】以 nexthop 为键的唯一 RB-tree。头类型 struct bgp_label_per_nexthop_cache_head
 * 由本宏展开声明，用于在 struct bgp 里按 afi 各挂一棵树。 */
DECLARE_RBTREE_UNIQ(bgp_label_per_nexthop_cache,
		    struct bgp_label_per_nexthop_cache, entry,
		    bgp_label_per_nexthop_cache_cmp);

/* 【函数】每下一跳标签缓存族（L3VPN 出标签按下一跳聚合）：
 *   bgp_label_per_nexthop_free : 释放：先通知 zebra 删除该标签并归还标签池，
 *                                再从树中摘除并释放内存。
 *   bgp_label_per_nexthop_new  : 新建并挂入指定树（键 = nexthop）。
 *   bgp_label_per_nexthop_find : 按 nexthop 查找。
 *   bgp_label_per_nexthop_init : 安装 show bgp ... label-nexthop 命令。 */
void bgp_label_per_nexthop_free(struct bgp_label_per_nexthop_cache *blnc);

struct bgp_label_per_nexthop_cache *
bgp_label_per_nexthop_new(struct bgp_label_per_nexthop_cache_head *tree,
			  struct prefix *nexthop);
struct bgp_label_per_nexthop_cache *
bgp_label_per_nexthop_find(struct bgp_label_per_nexthop_cache_head *tree,
			   struct prefix *nexthop);
void bgp_label_per_nexthop_init(void);
#endif /* _FRR_BGP_LABELPOOL_H */
