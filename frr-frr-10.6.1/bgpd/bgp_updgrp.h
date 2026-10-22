// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * bgp_updgrp.c: BGP update group structures
 *
 * @copyright Copyright (C) 2014 Cumulus Networks, Inc.
 *
 * @author Avneesh Sachdev <avneesh@sproute.net>
 * @author Rajesh Varadarajan <rajesh@sproute.net>
 * @author Pradosh Mohapatra <pradosh@sproute.net>
 */

#ifndef _QUAGGA_BGP_UPDGRP_H
#define _QUAGGA_BGP_UPDGRP_H
/* ============================================================================
 * 【文件导读】bgp_updgrp.h —— Update Group：向邻居发路由的“报文共享”体系
 * ============================================================================
 * 这是 BGP 可扩展性最关键的设计，务必理解它解决的问题：
 *   若有 R 条路由、P 个邻居，朴素做法要为每个 (路由, 邻居) 组合生成一份 UPDATE，
 *   代价 ~ O(R × P)，在百万路由 × 千邻居下不可接受。FRR 的对策是**按出向策略
 *   把邻居分组**：同一组内所有邻居看到的路由集合与属性几乎相同，于是同一条路由
 *   只编码“一份报文正文”，交给整组共享，发送前只按邻居微调个别字段（如下一跳）。
 *
 *  三层结构（层层收窄）：
 *    struct update_group   —— “出向策略完全相同”的 peer_af 集合，每 (afi,safi) 一个。
 *                             分组依据是下面 PEER_UPDGRP_* 这些“影响报文的标志”。
 *    struct update_subgroup —— update_group 里“当前处于同一同步点、可共用同一批报文”
 *                             的子集。某邻居落后（如刚建会话在追路由）会被“分裂”成
 *                             独立子组，追平后再“合并”回去。
 *    struct bpacket        —— 一份真正编码好的 UPDATE 字节流 + 要发给哪些 peer_af。
 *
 *  关键对象：
 *    peer_af               —— “peer × (afi,safi)”，是 peer 与 update-group 的连接点。
 *    bgp_adj_out           —— 某前缀在某子组的“已通告快照”，增量通告据此决定发/撤。
 *    bpacket_attr_vec      —— 报文里“需按 peer 改写”的字段偏移表（典型是下一跳）。
 *
 *  实现分散在三个 .c：
 *    bgp_updgrp.c        —— 分组/子组的建立、分裂、合并、遍历。
 *    bgp_updgrp_adv.c    —— 通告队列(adj)与选路结果如何变成待发内容。
 *    bgp_updgrp_packet.c —— 把待发内容编码成 bpacket 并入队、按 peer 改写、发送。
 *
 *  阅读建议：先看 struct update_group / update_subgroup / bpacket 三者关系，再带着
 *  “为什么会分裂/合并”去看 bgp_updgrp.c 的 split/merge 逻辑。
 * ============================================================================
 */

#include "bgp_advertise.h"

/*
 * The following three heuristic constants determine how long advertisement to
 * a subgroup will be delayed after it is created. The intent is to allow
 * transient changes in peer state (primarily session establishment) to settle,
 * so that more peers can be grouped together and benefit from sharing
 * advertisement computations with the subgroup.
 *
 * These values have a very large impact on initial convergence time; any
 * changes should be accompanied by careful performance testing at all scales.
 *
 * The coalesce time 'C' for a new subgroup within a particular BGP instance
 * 'B' with total number of known peers 'P', established or not, is computed as
 * follows:
 *
 * C = MIN(BGP_MAX_SUBGROUP_COALESCE_TIME,
 *         BGP_DEFAULT_SUBGROUP_COALESCE_TIME +
 *         (P*BGP_PEER_ADJUST_SUBGROUP_COALESCE_TIME))
 */
/* 【合并延迟(coalesce)】新建子组后不立刻通告，先“等一小会儿”，让邻居状态的
 * 瞬变（主要是会话刚建立）稳定下来，好让更多邻居被并进同一子组共享计算。
 * 这个时长的计算见上面英文公式：随实例内已知邻居数 P 线性增大并设上限。
 * 注意：它对**初始收敛时间**影响极大，改动必须做全规模的性能测试。 */
#define BGP_DEFAULT_SUBGROUP_COALESCE_TIME 1000
#define BGP_MAX_SUBGROUP_COALESCE_TIME 10000
#define BGP_PEER_ADJUST_SUBGROUP_COALESCE_TIME 50

/* 决定“两个 peer 能否放进同一个 update_group”的关键配置集合：
 * 只有这些影响**出向报文内容**的标志/能力完全一致，才允许共享报文。
 *   PEER_UPDGRP_FLAGS        : peer 级标志（如 local-as 不预置、AS 环路检测、NHC）。
 *   PEER_UPDGRP_AF_FLAGS     : AF 级标志（send-community、next-hop-self、RR 客户端、
 *                              default-originate、remove-private-as、封装(SRv6/MPLS) 等）。
 *   PEER_UPDGRP_CAP_FLAGS    : 能力标志（AS4）。
 *   PEER_UPDGRP_AF_CAP_FLAGS : AF 级能力（ORF、Add-Path、Extended Nexthop）。
 * 分组哈希就是对“代表 peer(update_group->conf)”上这些位算出来的；新增一个会影响
 * 报文内容的标志时，**必须**把它加进对应集合，否则会把本该分开的 peer 错误地合并。 */
#define PEER_UPDGRP_FLAGS                                                                         \
	(PEER_FLAG_LOCAL_AS_NO_PREPEND | PEER_FLAG_LOCAL_AS_REPLACE_AS |                          \
	 PEER_FLAG_AS_LOOP_DETECTION | PEER_FLAG_SEND_NHC_ATTRIBUTE)

#define PEER_UPDGRP_AF_FLAGS                                                                      \
	(PEER_FLAG_SEND_COMMUNITY | PEER_FLAG_SEND_EXT_COMMUNITY |                                \
	 PEER_FLAG_SEND_EXT_COMMUNITY_RPKI | PEER_FLAG_SEND_LARGE_COMMUNITY |                     \
	 PEER_FLAG_DEFAULT_ORIGINATE | PEER_FLAG_REFLECTOR_CLIENT | PEER_FLAG_RSERVER_CLIENT |    \
	 PEER_FLAG_NEXTHOP_SELF | PEER_FLAG_NEXTHOP_UNCHANGED | PEER_FLAG_FORCE_NEXTHOP_SELF |    \
	 PEER_FLAG_AS_PATH_UNCHANGED | PEER_FLAG_MED_UNCHANGED |                                  \
	 PEER_FLAG_NEXTHOP_LOCAL_UNCHANGED | PEER_FLAG_REMOVE_PRIVATE_AS |                        \
	 PEER_FLAG_REMOVE_PRIVATE_AS_ALL | PEER_FLAG_REMOVE_PRIVATE_AS_REPLACE |                  \
	 PEER_FLAG_REMOVE_PRIVATE_AS_ALL_REPLACE | PEER_FLAG_AS_OVERRIDE |                        \
	 PEER_FLAG_CONFIG_ENCAPSULATION_SRV6 | PEER_FLAG_CONFIG_ENCAPSULATION_SRV6_RELAX |        \
	 PEER_FLAG_CONFIG_ENCAPSULATION_MPLS)

#define PEER_UPDGRP_CAP_FLAGS (PEER_CAP_AS4_RCV)

#define PEER_UPDGRP_AF_CAP_FLAGS                                               \
	(PEER_CAP_ORF_PREFIX_SM_RCV | PEER_CAP_ADDPATH_AF_TX_ADV |             \
	 PEER_CAP_ADDPATH_AF_RX_RCV | PEER_CAP_ENHE_AF_NEGO)

enum bpacket_attr_vec_type { BGP_ATTR_VEC_NH = 0, BGP_ATTR_VEC_MAX };

typedef struct {
	uint32_t flags;
	unsigned long offset;
} bpacket_attr_vec;

#define BPKT_ATTRVEC_FLAGS_UPDATED        (1 << 0)
#define BPKT_ATTRVEC_FLAGS_RMAP_NH_PEER_ADDRESS   (1 << 1)
#define BPKT_ATTRVEC_FLAGS_RMAP_NH_UNCHANGED   (1 << 3)
#define BPKT_ATTRVEC_FLAGS_RMAP_IPV4_NH_CHANGED   (1 << 4)
#define BPKT_ATTRVEC_FLAGS_RMAP_IPV6_GNH_CHANGED  (1 << 5)
#define BPKT_ATTRVEC_FLAGS_RMAP_IPV6_LNH_CHANGED  (1 << 6)
#define BPKT_ATTRVEC_FLAGS_RMAP_VPNV4_NH_CHANGED  (1 << 7)
#define BPKT_ATTRVEC_FLAGS_RMAP_VPNV6_GNH_CHANGED (1 << 8)

typedef struct bpacket_attr_vec_arr {
	bpacket_attr_vec entries[BGP_ATTR_VEC_MAX];
} bpacket_attr_vec_arr;

/* bpacket —— 一份“已编码好的 UPDATE 报文” + 要发给哪些 peer_af。
 * 这是报文共享的精髓：同组 peer 策略一致，同一条路由生成的报文正文基本相同，
 * 于是只编码一份，让多个 peer 共享；发送前用 bpacket_reformat_for_peer() 只按 peer
 * 微调少数字段（典型是下一跳），避免为每个 peer 重复序列化整条报文。
 *   buffer : 编码后的报文字节流；
 *   peers  : 接收者列表（peer_af）；
 *   arr    : “需按 peer 改写”的属性位置向量表（如下一跳字段在 buffer 中的偏移）；
 *   ver    : 版本/序号，用于判断报文是否过期。 */
struct bpacket {
	/* for being part of an update subgroup's message list */
	TAILQ_ENTRY(bpacket) pkt_train;

	/* list of peers (well, peer_afs) that the packet needs to be sent to */
	LIST_HEAD(pkt_peer_list, peer_af) peers;

	struct stream *buffer;
	bpacket_attr_vec_arr arr;

	unsigned int ver;
};

struct bpacket_queue {
	TAILQ_HEAD(pkt_queue, bpacket) pkts;

	unsigned int conf_max_count;
	unsigned int curr_count;
	unsigned int hwm_count;
	unsigned int max_count_reached_count;
};

/* update_group —— “出向策略完全相同”的一批 peer_af 的集合（每个 (afi,safi) 一个）。
 * 分组依据是上面的 PEER_UPDGRP_* 标志：只要所有会影响出向报文内容的配置与能力都
 * 一致，就能共用同一份报文。conf 是一个“代表 peer”，其配置用于计算分组哈希。
 * 一个 update_group 下可挂多个 update_subgroup。 */
struct update_group {
	/* back pointer to the BGP instance */
	struct bgp *bgp;

	/* list of subgroups that belong to the update group */
	LIST_HEAD(subgrp_list, update_subgroup) subgrps;

	/* lazy way to store configuration common to all peers
	   hash function will compute from this data */
	struct peer *conf;

	afi_t afi;
	safi_t safi;
	int afid;

	uint64_t id;
	time_t uptime;

	uint32_t join_events;
	uint32_t prune_events;
	uint32_t merge_events;
	uint32_t updgrp_switch_events;
	uint32_t peer_refreshes_combined;
	uint32_t adj_count;
	uint32_t split_events;
	uint32_t merge_checks_triggered;

	uint32_t subgrps_created;
	uint32_t subgrps_deleted;

	uint32_t num_dbg_en_peers;
};

/*
 * Shorthand for a global statistics counter.
 */
#define UPDGRP_GLOBAL_STAT(updgrp, stat)                                       \
	((updgrp)->bgp->update_group_stats.stat)

/*
 * Add the given value to a counter on an update group and the bgp
 * instance.
 */
#define UPDGRP_INCR_STAT_BY(updgrp, stat, value)                               \
	do {                                                                   \
		(updgrp)->stat += (value);                                     \
		UPDGRP_GLOBAL_STAT(updgrp, stat) += (value);                   \
	} while (0)

/*
 * Increment a counter on a update group and its parent structures.
 */
#define UPDGRP_INCR_STAT(subgrp, stat) UPDGRP_INCR_STAT_BY(subgrp, stat, 1)

/* update_subgroup —— update_group 里“当前处于同一同步点、可共用同一批报文”的子集。
 * 为什么要有子组：即使配置相同，若某 peer 落后于最新表版本（如刚建立、还在追路由），
 * 就暂时不能与其他 peer 共用报文，于是被“分裂(split)”成独立子组；追平后再
 * “合并(merge)”回去。相关操作：update_subgroup_split_peer() /
 * update_subgroup_check_merge()。
 *   work/scratch : 两个流，分别编码 MP_REACH_NLRI 与其它属性，最后拼接成完整报文，
 *                  这样 NLRI 能高效打包（见 bpacket_reformat_for_peer）。
 *   adjq         : 本子组“已通告前缀”的快照（bgp_adj_out）——增量通告的依据。
 *   pkt_queue    : 待发送的 bpacket 队列。
 *   t_coalesce / v_coalesce : 合并延迟定时器与其时长（见文件头的 coalesce 公式）。
 *   version / adj_version : 已追平的表版本 / 记录 adj 变化的版本。 */
struct update_subgroup {
	/* back pointer to the parent update group */
	struct update_group *update_group;

	/* list of peers that belong to the subgroup */
	LIST_HEAD(peer_list, peer_af) peers;
	int peer_count;

	/* for being part of an update group's subgroup list */
	LIST_ENTRY(update_subgroup) updgrp_train;

	struct bpacket_queue pkt_queue;

	/*
	 * List of adj-out structures for this subgroup.
	 * It essentially represents the snapshot of every prefix that
	 * has been advertised to the members of the subgroup
	 */
	TAILQ_HEAD(adjout_queue, bgp_adj_out) adjq;

	/* packet buffer for update generation */
	struct stream *work;

	/* We use a separate stream to encode MP_REACH_NLRI for efficient
	 * NLRI packing. peer->obuf_work stores all the other attributes. The
	 * actual packet is then constructed by concatenating the two.
	 */
	struct stream *scratch;

	/* synchronization list and time */
	struct bgp_synchronize *sync;

	/* send prefix count */
	uint32_t scount;

	/* send prefix count prior to packet update */
	uint32_t pscount;

	/* announcement attribute hash */
	struct hash *hash;

	struct event *t_coalesce;
	uint32_t v_coalesce;

	struct event *t_merge_check;

	/* table version that the subgroup has caught up to. */
	uint64_t version;

	/* version maintained to record adj changes */
	uint64_t adj_version;

	time_t uptime;

	/*
	 * Identifying information about the subgroup that this subgroup was
	 * split
	 * from, if any.
	 */
	struct {
		uint64_t update_group_id;
		uint64_t subgroup_id;
	} split_from;

	uint32_t join_events;
	uint32_t prune_events;

	/*
	 * This is bumped up when another subgroup merges into this one.
	 */
	uint32_t merge_events;
	uint32_t updgrp_switch_events;
	uint32_t peer_refreshes_combined;
	uint32_t adj_count;
	uint32_t split_events;
	uint32_t merge_checks_triggered;

	uint64_t id;

	uint16_t sflags;
/* 【子组状态标志】update_subgroup.sflags 的取值：
 *   DEFAULT_ORIGINATE   : 本子组正在做默认路由始发（default-originate）。
 *   FORCE_UPDATES       : 强制重发所有通告。
 *   TABLE_REPARSING     : 正在重解析路由表。
 *   PEER_DEFAULT_ORIGINATED : 已为某 peer 记为默认路由始发，用于 SNT 计数的
 *                             生成与删除阶段，避免在更新阶段重复增减。 */
#define SUBGRP_STATUS_DEFAULT_ORIGINATE (1 << 0)
#define SUBGRP_STATUS_FORCE_UPDATES (1 << 1)
#define SUBGRP_STATUS_TABLE_REPARSING (1 << 2)
/*
 * This flag has been added to ensure that the SNT counters
 * gets incremented and decremented only during the creation
 * and deletion workflows of default originate,
 * not during the update workflow.
 */
#define SUBGRP_STATUS_PEER_DEFAULT_ORIGINATED (1 << 3)

	uint16_t flags;
/* 【子组标志】update_subgroup.flags 的取值：NEEDS_REFRESH 表示该子组需要重新
 * 生成待发内容（见 update_subgroup_set_needs_refresh）。 */
#define SUBGRP_FLAG_NEEDS_REFRESH (1 << 0)
};

/*
 * Add the given value to the specified counter on a subgroup and its
 * parent structures.
 */
/* 【统计宏】把计数值同时累加到子组本身与其父级结构（update_group 及实例）。
 *   SUBGRP_INCR_STAT_BY : 累加给定 value；SUBGRP_INCR_STAT 累加 1；
 *   SUBGRP_DECR_STAT    : 累减 1（实现见 UPDGRP_INCR_STAT_BY）。 */
#define SUBGRP_INCR_STAT_BY(subgrp, stat, value)                               \
	do {                                                                   \
		(subgrp)->stat += (value);                                     \
		if ((subgrp)->update_group)                                    \
			UPDGRP_INCR_STAT_BY((subgrp)->update_group, stat,      \
					    value);                            \
	} while (0)

/*
 * Increment a counter on a subgroup and its parent structures.
 */
#define SUBGRP_INCR_STAT(subgrp, stat) SUBGRP_INCR_STAT_BY(subgrp, stat, 1)

/*
 * Decrement a counter on a subgroup and its parent structures.
 */
#define SUBGRP_DECR_STAT(subgrp, stat) SUBGRP_INCR_STAT_BY(subgrp, stat, -1)

/* 【类型】update group 遍历回调：对每个 update_group 调用一次，返回
 * HASHWALK_CONTINUE 继续或 HASHWALK_ABORT 中止。ctx 为调用方透传的上下文。 */
typedef int (*updgrp_walkcb)(struct update_group *updgrp, void *ctx);

/* really a private structure */
/* 【结构】遍历 update_group 时携带的上下文（私有结构）。记录 vty / json 输出
 * 目标、过滤条件（id、policy 类型与名）、回调函数与其 context、以及标志位，
 * 供 update_group 的 walk 系列在遍历中共享。 */
struct updwalk_context {
	struct vty *vty;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	uint64_t updgrp_id;
	uint64_t subgrp_id;
	enum bgp_policy_type policy_type;
	const char *policy_name;
	int policy_event_start_flag;
	bool policy_route_update;
	updgrp_walkcb cb;
	void *context;
	uint8_t flags;
	bool uj;
	json_object *json_updategrps;

/* 【遍历标志】updwalk_context.flags 取值：ADVQUEUE 表示遍历通告队列，
 * ADVERTISED 表示遍历“已通告”集合。 */
#define UPDWALK_FLAGS_ADVQUEUE   (1 << 0)
#define UPDWALK_FLAGS_ADVERTISED (1 << 1)
};

/* 【遍历返回宏】等价于通用哈希遍历的继续 / 中止常量，供 updgrp_walkcb 返回。 */
#define UPDWALK_CONTINUE HASHWALK_CONTINUE
#define UPDWALK_ABORT HASHWALK_ABORT

/* 【peer_af 访问宏】PAF 即 peer_af，是 peer 与 update-group 的连接点：
 *   PAF_PEER / PAF_SUBGRP 取其所属 peer 与子组；PAF_UPDGRP 再经子组反查更新组；
 *   PAF_PKTQ 取该 peer 所在子组的报文队列。 */
#define PAF_PEER(p)        ((p)->peer)
#define PAF_SUBGRP(p)      ((p)->subgroup)
#define PAF_UPDGRP(p)      ((p)->subgroup->update_group)
#define PAF_PKTQ(f)        SUBGRP_PKTQ((f)->subgroup)

/* 【update_group 访问宏】从 update_group 取常用字段：
 *   PEER / INST 取代表 peer 与所属 bgp 实例；AFI / SAFI 取出向地址族；
 *   AFFLAGS 取代表 peer 在 (afi,safi) 上的 AF 标志；
 *   DBG_ON / PEER_DBG_EN / PEER_DBG_DIS / OFF 维护“本组启用了调试的 peer 数”。 */
#define UPDGRP_PEER(u)     ((u)->conf)
#define UPDGRP_AFI(u)      ((u)->afi)
#define UPDGRP_SAFI(u)     ((u)->safi)
#define UPDGRP_INST(u)     ((u)->bgp)
#define UPDGRP_AFFLAGS(u) ((u)->conf->af_flags[UPDGRP_AFI(u)][UPDGRP_SAFI(u)])
#define UPDGRP_DBG_ON(u)   ((u)->num_dbg_en_peers)
#define UPDGRP_PEER_DBG_EN(u)  (((u)->num_dbg_en_peers)++)
#define UPDGRP_PEER_DBG_DIS(u) (((u)->num_dbg_en_peers)--)
#define UPDGRP_PEER_DBG_OFF(u) (u)->num_dbg_en_peers = 0

/* 【subgroup 访问宏】从 update_subgroup 取信息（多经其 update_group 转发）：
 *   AFI / SAFI / PEER / INST / AFFLAGS 同 update_group；PCOUNT 为成员 peer 数；
 *   PFIRST 取首个成员 peer_af；PKTQ 取报文队列；UPDGRP 取父更新组。 */
#define SUBGRP_AFI(s)      UPDGRP_AFI((s)->update_group)
#define SUBGRP_SAFI(s)     UPDGRP_SAFI((s)->update_group)
#define SUBGRP_PEER(s)     UPDGRP_PEER((s)->update_group)
#define SUBGRP_PCOUNT(s)   ((s)->peer_count)
#define SUBGRP_PFIRST(s)   LIST_FIRST(&((s)->peers))
#define SUBGRP_PKTQ(s)     &((s)->pkt_queue)
#define SUBGRP_INST(s)     UPDGRP_INST((s)->update_group)
#define SUBGRP_AFFLAGS(s)  UPDGRP_AFFLAGS((s)->update_group)
#define SUBGRP_UPDGRP(s)   ((s)->update_group)

/*
 * Walk all subgroups in an update group.
 */
/* 【遍历宏】update-group 与 subgroup 的链表遍历封装：
 *   UPDGRP_FOREACH_SUBGRP 遍历某更新组的所有子组；
 *   SUBGRP_FOREACH_PEER  遍历某子组的所有成员 peer_af；
 *   SUBGRP_FOREACH_ADJ   遍历某子组的出向通告记录（adjq）；
 *   带 _SAFE 的版本允许在遍历中删除当前元素（多传一个临时迭代器）。 */
#define UPDGRP_FOREACH_SUBGRP(updgrp, subgrp)                                  \
	LIST_FOREACH (subgrp, &((updgrp)->subgrps), updgrp_train)

#define UPDGRP_FOREACH_SUBGRP_SAFE(updgrp, subgrp, tmp_subgrp)                 \
	LIST_FOREACH_SAFE (subgrp, &((updgrp)->subgrps), updgrp_train,         \
			   tmp_subgrp)

#define SUBGRP_FOREACH_PEER(subgrp, paf)                                       \
	LIST_FOREACH (paf, &(subgrp->peers), subgrp_train)

#define SUBGRP_FOREACH_PEER_SAFE(subgrp, paf, temp_paf)                        \
	LIST_FOREACH_SAFE (paf, &(subgrp->peers), subgrp_train, temp_paf)

#define SUBGRP_FOREACH_ADJ(subgrp, adj)                                        \
	TAILQ_FOREACH (adj, &(subgrp->adjq), subgrp_adj_train)

#define SUBGRP_FOREACH_ADJ_SAFE(subgrp, adj, adj_temp)                         \
	TAILQ_FOREACH_SAFE (adj, &(subgrp->adjq), subgrp_adj_train, adj_temp)

/* Prototypes.  */
/* bgp_updgrp.c */
/* 【原型：bgp_updgrp.c】分组与子组的建立、调整、分裂、合并、遍历与显示。
 *   init / free     : 初始化与释放实例级的更新组体系；
 *   *_adjust_peer   : 按 peer 当前状态重新归组或判断孤组；
 *   split / merge   : 子组的分裂与合并检查；
 *   *_walk          : 遍历并回调；policy_update 在出向策略变化时重算。 */
extern void update_bgp_group_init(struct bgp *bgp);

extern void update_group_show(struct bgp *bgp, afi_t afi, safi_t safi,
			      struct vty *vty, uint64_t subgrp_id, bool uj);
extern void update_group_show_stats(struct bgp *bgp, struct vty *vty);
extern void update_group_adjust_peer(struct peer_af *paf);
extern int update_group_adjust_soloness(struct peer *peer, int set);

extern void update_subgroup_remove_peer(struct update_subgroup *subgrp, struct peer_af *paf);
extern struct bgp_table *update_subgroup_rib(struct update_subgroup *);
extern void update_subgroup_split_peer(struct peer_af *paf, struct update_group *updgrp);
extern bool update_subgroup_check_merge(struct update_subgroup *subgrp, const char *reason);
extern bool update_subgroup_trigger_merge_check(struct update_subgroup *subgrp, int force);
extern void update_group_policy_update(struct bgp *bgp,
				       enum bgp_policy_type ptype,
				       const char *pname, bool route_update,
				       int start_event);
extern void update_group_af_walk(struct bgp *bgp, afi_t afi, safi_t safi,
				 updgrp_walkcb cb, void *ctx);
extern void update_group_walk(struct bgp *bgp, updgrp_walkcb cb, void *ctx);
extern void
update_group_refresh_default_originate_route_map(struct event *event);
extern void update_group_start_advtimer(struct bgp *bgp);

extern void update_subgroup_inherit_info(struct update_subgroup *to,
					 struct update_subgroup *from);

/* bgp_updgrp_packet.c */
/* 【原型：bgp_updgrp_packet.c】报文(bpacket)与报文队列的分配、入队、取出、
 * 按 peer 改写与发送，以及子组的更新与撤回报文生成。bpacket 是“已编码好的
 * 一份 UPDATE”，可被同组多个 peer 共享，发送前仅按 peer 改写少数字段。 */
extern struct bpacket *bpacket_alloc(void);
extern void bpacket_free(struct bpacket *pkt);
extern void bpacket_queue_init(struct bpacket_queue *q);
extern void bpacket_queue_cleanup(struct bpacket_queue *q);
extern void bpacket_queue_sanity_check(struct bpacket_queue *q);
extern struct bpacket *bpacket_queue_add(struct bpacket_queue *q,
					 struct stream *s,
					 struct bpacket_attr_vec_arr *vecarr);
struct bpacket *bpacket_queue_remove(struct bpacket_queue *q);
extern struct bpacket *bpacket_queue_first(struct bpacket_queue *q);
struct bpacket *bpacket_queue_last(struct bpacket_queue *q);
unsigned int bpacket_queue_length(struct bpacket_queue *q);
unsigned int bpacket_queue_hwm_length(struct bpacket_queue *q);
bool bpacket_queue_is_full(struct bgp *bgp, struct bpacket_queue *q);
extern void bpacket_queue_advance_peer(struct peer_af *paf);
extern void bpacket_queue_remove_peer(struct peer_af *paf);
extern void bpacket_add_peer(struct bpacket *pkt, struct peer_af *paf);
unsigned int bpacket_queue_virtual_length(struct peer_af *paf);
extern void bpacket_queue_show_vty(struct bpacket_queue *q, struct vty *vty);
bool subgroup_packets_to_build(struct update_subgroup *subgrp);
extern struct bpacket *subgroup_update_packet(struct update_subgroup *s);
extern struct bpacket *subgroup_withdraw_packet(struct update_subgroup *s);
extern struct stream *bpacket_reformat_for_peer(struct bpacket *pkt,
						struct peer_af *paf);
extern void bpacket_attr_vec_arr_reset(struct bpacket_attr_vec_arr *vecarr);
extern void bpacket_attr_vec_arr_set_vec(struct bpacket_attr_vec_arr *vecarr,
					 enum bpacket_attr_vec_type type,
					 struct stream *s, struct attr *attr);
extern void subgroup_default_update_packet(struct update_subgroup *subgrp,
					   struct attr *attr,
					   struct peer *from);
extern void subgroup_default_withdraw_packet(struct update_subgroup *subgrp);

/* bgp_updgrp_adv.c */
/* 【原型：bgp_updgrp_adv.c】选路结果如何变成待发内容：
 *   维护出向记录（adj out）的建立与取消，把路由通告进子组；
 *   announce 系列与 default_originate 触发实际通告；
 *   show 系列供 show 命令展示通告队列、已通告集合、报文队列。 */
extern struct bgp_advertise *
bgp_advertise_clean_subgroup(struct update_subgroup *subgrp,
			     struct bgp_adj_out *adj);
extern void update_group_show_adj_queue(struct bgp *bgp, afi_t afi, safi_t safi,
					struct vty *vty, uint64_t id);
extern void update_group_show_advertised(struct bgp *bgp, afi_t afi,
					 safi_t safi, struct vty *vty,
					 uint64_t id);
extern void update_group_show_packet_queue(struct bgp *bgp, afi_t afi,
					   safi_t safi, struct vty *vty,
					   uint64_t id);
extern void subgroup_announce_route(struct update_subgroup *subgrp);
extern void subgroup_announce_all(struct update_subgroup *subgrp);

extern void subgroup_default_originate(struct update_subgroup *subgrp,
				       bool withdraw);
extern void group_announce_route(struct bgp *bgp, afi_t afi, safi_t safi,
				 struct bgp_dest *dest,
				 struct bgp_path_info *pi);
extern void subgroup_clear_table(struct update_subgroup *subgrp);
extern void update_group_announce(struct bgp *bgp);
extern void update_group_announce_rrclients(struct bgp *bgp);
extern void peer_af_announce_route(struct peer_af *paf, int combine);
extern struct bgp_adj_out *bgp_adj_out_alloc(struct update_subgroup *subgrp,
					     struct bgp_dest *dest,
					     uint32_t addpath_tx_id);
extern void bgp_adj_out_remove_subgroup(struct bgp_dest *dest,
					struct bgp_adj_out *adj,
					struct update_subgroup *subgrp);
extern bool bgp_adj_out_set_subgroup(struct bgp_dest *dest,
				     struct update_subgroup *subgrp,
				     struct attr *attr,
				     struct bgp_path_info *path);
extern void bgp_adj_out_unset_subgroup(struct bgp_dest *dest,
				       struct update_subgroup *subgrp,
				       uint32_t addpath_tx_id);
void subgroup_announce_table(struct update_subgroup *subgrp,
			     struct bgp_table *table);
extern void subgroup_trigger_write(struct update_subgroup *subgrp);

/* 【原型：杂项】清除调试计数、释放实例级更新组、以及 Add-Path 发送侧的判断
 * （是否编码 Add-Path、路径是否被选中、是否具备 Add-Path 能力）。 */
extern int update_group_clear_update_dbg(struct update_group *updgrp,
					 void *arg);

extern void update_bgp_group_free(struct bgp *bgp);
extern bool bgp_addpath_encode_tx(struct peer *peer, afi_t afi, safi_t safi);
extern bool bgp_check_selected(struct bgp_path_info *bpi, struct peer *peer,
			       bool addpath_capable, afi_t afi, safi_t safi);
extern bool bgp_addpath_capable(struct bgp_path_info *bpi, struct peer *peer,
				afi_t afi, safi_t safi);

/*
 * Inline functions
 */

/* 【函数】bpacket_queue_is_empty —— 队列是否只剩哨兵元素（即为空）。 */
/*
 * bpacket_queue_is_empty
 */
static inline int bpacket_queue_is_empty(struct bpacket_queue *queue)
{

	/*
	 * The packet queue is empty if it only contains a sentinel.
	 */
	if (queue->curr_count != 1)
		return 0;

	assert(bpacket_queue_first(queue)->buffer == NULL);
	return 1;
}

/* 【函数】bpacket_next —— 取队列中给定报文的后继报文。 */
/*
 * bpacket_next
 *
 * Returns the packet after the given packet in a bpacket queue.
 */
static inline struct bpacket *bpacket_next(struct bpacket *pkt)
{
	return TAILQ_NEXT(pkt, pkt_train);
}

/* 【函数】update_group_adjust_peer_afs —— 对某 peer 的所有 (afi,safi) 重新
 * 调整其分组（遍历其 peer_af_array）。 */
/*
 * update_group_adjust_peer_afs
 *
 * Adjust all peer_af structures for the given peer.
 */
static inline void update_group_adjust_peer_afs(struct peer *peer)
{
	struct peer_af *paf;
	int afidx;

	for (afidx = BGP_AF_START; afidx < BGP_AF_MAX; afidx++) {
		paf = peer->peer_af_array[afidx];
		if (paf != NULL)
			update_group_adjust_peer(paf);
	}
}

/* 【函数】update_group_remove_peer_afs —— 把某 peer 的所有 peer_af 从其子组
 * 移除（先停通告定时器再调 update_subgroup_remove_peer）。 */
/*
 * update_group_remove_peer_afs
 *
 * Remove all peer_af structures for the given peer from their subgroups.
 */
static inline void update_group_remove_peer_afs(struct peer *peer)
{
	struct peer_af *paf;
	int afidx;

	for (afidx = BGP_AF_START; afidx < BGP_AF_MAX; afidx++) {
		paf = peer->peer_af_array[afidx];
		if (paf != NULL) {
			bgp_stop_announce_route_timer(paf);
			update_subgroup_remove_peer(PAF_SUBGRP(paf), paf);
		}
	}
}

/* 【函数】update_subgroup_needs_refresh —— 子组是否被标记为需要重新生成。 */
/*
 * update_subgroup_needs_refresh
 */
static inline int
update_subgroup_needs_refresh(const struct update_subgroup *subgrp)
{
	if (CHECK_FLAG(subgrp->flags, SUBGRP_FLAG_NEEDS_REFRESH))
		return 1;
	else
		return 0;
}

/* 【函数】update_subgroup_set_needs_refresh —— 置位或清除子组的 NEEDS_REFRESH 标志。 */
/*
 * update_subgroup_set_needs_refresh
 */
static inline void
update_subgroup_set_needs_refresh(struct update_subgroup *subgrp, int value)
{
	if (value)
		SET_FLAG(subgrp->flags, SUBGRP_FLAG_NEEDS_REFRESH);
	else
		UNSET_FLAG(subgrp->flags, SUBGRP_FLAG_NEEDS_REFRESH);
}

/* 【函数】peer_subgroup —— 查某 peer 在给定 (afi,safi) 上所属的更新子组
 * （无则返回 NULL）。 */
static inline struct update_subgroup *peer_subgroup(struct peer *peer,
						    afi_t afi, safi_t safi)
{
	struct peer_af *paf;

	paf = peer_af_find(peer, afi, safi);
	if (paf)
		return PAF_SUBGRP(paf);
	return NULL;
}

/* 【函数】bgp_announce_peer —— 对某 peer 的所有地址族立即触发通告。 */
/*
 * update_group_adjust_peer_afs
 *
 * Adjust all peer_af structures for the given peer.
 */
static inline void bgp_announce_peer(struct peer *peer)
{
	struct peer_af *paf;
	int afidx;

	for (afidx = BGP_AF_START; afidx < BGP_AF_MAX; afidx++) {
		paf = peer->peer_af_array[afidx];
		if (paf != NULL)
			subgroup_announce_all(PAF_SUBGRP(paf));
	}
}

/* 【函数】advertise_list_is_empty —— 子组的待发更新与待撤回队列是否都为空。 */
/**
 * advertise_list_is_empty
 */
static inline int advertise_list_is_empty(struct update_subgroup *subgrp)
{
	if (bgp_adv_fifo_count(&subgrp->sync->update) ||
	    bgp_adv_fifo_count(&subgrp->sync->withdraw))
		return 0;

	return 1;
}

/* 【函数】peer_immediate_announce —— 是否需要对某 peer 立即通告而不做合并：
 * 情形一为重启路由器（收到全部 EOR 之后立即），情形二为作为 Helper 路由器
 * 处理刚建起的会话（GR 相关）。 */
/*
 * Immediate announce or coalesce multiple peers?
 */
static inline bool peer_immediate_announce(struct peer *peer, afi_t afi, safi_t safi)
{
	/*
	 * Immediate announce:
	 *  a) We are restarting router - here, immediate means *after*
	 *     we get all needed EORs
	 *  b) Peering comes up and we're acting as a Helper router for
	 *     the peer
	 */
	if (CHECK_FLAG(peer->af_sflags[afi][safi], PEER_STATUS_GR_WAIT_EOR) ||
	    (peer->nsf[afi][safi] && peer->connection->t_gr_stale))
		return true;
	return false;
}
#endif /* _QUAGGA_BGP_UPDGRP_H */
