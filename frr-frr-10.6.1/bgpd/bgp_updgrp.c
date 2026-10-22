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
/* ============================================================================
 * 【文件导读】bgp_updgrp.c —— update-group 主控：分组的建立/查找/分裂/合并/迁移
 * ============================================================================
 * 职责与定位：
 *   这是 update-group（出向更新聚合）机制的中枢。它不负责编码报文本身，而是回答
 *   “哪些 peer 的出向内容相同、可以共享同一批预编码报文”这个组织性难题，并维护
 *   三层对象（update_group / update_subgroup / peer_af）的生命周期与增删改。
 *   编码报文的活由 bgp_updgrp_packet.c 干，维护 adj/adv 通告队列的活由
 *   bgp_updgrp_adv.c 干；本文件是它们的“组织结构提供者”。
 *
 * 三条核心主线：
 *   1. 分组（grouping）：用 updgrp_hash_key_make / updgrp_hash_cmp 把 peer 映射到
 *      update_group。哈希键由 PEER_UPDGRP_FLAGS / AF_FLAGS / CAP_FLAGS 等“影响报文的
 *      配置与能力”拼成，只有这些完全一致的 peer 才落到同一 group（共享报文的前提）。
 *   2. 子组（subgroup）：同一 group 内，处于不同“表同步点”的 peer 不能共用报文，
 *      于是有 update_subgroup。update_group_adjust_peer() 是新 peer 入组/换组的总入口，
 *      update_subgroup_split_peer() 负责把落后者分裂出来，merge 系列负责追平后并回去。
 *   3. 策略变更（policy）：route-map / filter-list / prefix-list / distribute-list 内容
 *      变化时，update_group_policy_update() 遍历所有 group，标记 needs-refresh 并触发
 *      重新通告。
 *
 * 关键路径：
 *   上游调用者：bgp_route.c（选路变化→group_announce_route，定义在 adv.c）、bgp_fsm.c
 *   （邻居状态迁移→update_group_adjust_peer_afs）、bgp_vty.c（show 命令→update_group_show*）、
 *   bgp_routemap/filter 模块（策略变更→update_group_policy_update）。
 *   下游被调用：bgp_updgrp_packet.c 的 bpacket_* 与 subgroup_*_packet；
 *   bgp_updgrp_adv.c 的 subgroup_announce_* / bgp_adj_out_*。
 *
 * 关键函数清单（建议阅读顺序）：
 *   1) updgrp_hash_key_make / updgrp_hash_cmp —— 分组的“判据”，先读它弄清谁能合并。
 *   2) update_group_find / update_group_create —— 按 peer_af 找/建 update_group。
 *   3) update_group_adjust_peer —— 新 peer 入组/换组总入口，串联下面这些子组操作。
 *   4) update_subgroup_split_peer —— 分裂：落后 peer 独立成组。
 *   5) update_subgroup_check_merge / _merge —— 合并：追平后并回。
 *   6) update_subgroup_add_peer / _remove_peer —— 子组成员的增删及 bpacket 挂接。
 *   7) peer_af_announce_route —— 立即向某 peer 刷新路由（必要时合并同组 peer）。
 *   8) updgrp_policy_update_walkcb / update_group_policy_update —— 策略变更触发重通告。
 *
 * 建议阅读顺序：先 1、2（分组判据与查找），再 3（入组总入口），再 4、5（分裂/合并），
 *   最后 7、8（触发通告与策略变更）。
 * ============================================================================
 */

#include <zebra.h>

#include "prefix.h"
#include "frrevent.h"
#include "buffer.h"
#include "stream.h"
#include "command.h"
#include "sockunion.h"
#include "network.h"
#include "memory.h"
#include "filter.h"
#include "routemap.h"
#include "log.h"
#include "plist.h"
#include "linklist.h"
#include "workqueue.h"
#include "hash.h"
#include "jhash.h"
#include "queue.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_addpath.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_updgrp.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_filter.h"
#include "bgpd/bgp_io.h"
#include "bgpd/bgp_trace.h"

/********************
 * PRIVATE FUNCTIONS
 ********************/
/* ========== 1. 对象身份/初始化：id 分配、sync 流与哈希的创建销毁 ========== */

/**
 * assign a unique ID to update group and subgroup. Mostly for display/
 * debugging purposes. It's a 64-bit space - used leisurely without a
 * worry about its wrapping and about filling gaps. While at it, timestamp
 * the creation.
 */
/* 【update_group_checkin】给 update_group 分配全局唯一 id 并记录创建时间(monotime)。
 * 参数 updgrp：刚创建的 update_group（尚未 checkin）。
 * 返回：无。
 * 调用时机：update_group_create() 中把新组加入哈希表后调用。
 * 易误解点：id 来自全局 bm->updgrp_idspace 单调递增，仅用于 show/调试，不参与任何
 *   逻辑判断；它是 64 位，代码“随意”使用、不担心回绕或补洞。 */
static void update_group_checkin(struct update_group *updgrp)
{
	updgrp->id = ++bm->updgrp_idspace;
	updgrp->uptime = monotime(NULL);
}

/* 【update_subgroup_checkin】给 update_subgroup 分配全局唯一 id 并记录创建时间。
 * 参数 subgrp：刚创建的子组；updgrp：其父组（本函数当前未使用，仅保持签名对称）。
 * 返回：无。
 * 调用时机：update_subgroup_create() 中创建子组后立即调用。
 * 易误解点：id 来自 bm->subgrp_idspace，与 update_group id 是两套独立空间。 */
static void update_subgroup_checkin(struct update_subgroup *subgrp,
				    struct update_group *updgrp)
{
	subgrp->id = ++bm->subgrp_idspace;
	subgrp->uptime = monotime(NULL);
}

/* 【sync_init】初始化子组的“同步”基础设施：sync 结构(两个 adv FIFO)、广告属性哈希、
 *   以及两个报文编码用的流 work/scratch。
 * 参数 subgrp：目标子组；updgrp：其父组（用于取代表 peer 拿接口 MTU）。
 * 返回：无。
 * 调用时机：update_subgroup_create() 创建子组时。
 * 关键点：
 *   - sync->update / sync->withdraw 是两个待编码通告的 FIFO（adv 队列）；subgroup_*_packet
 *     从这里取内容编码成 bpacket。
 *   - work 是“主属性流”，scratch 是“MP_REACH_NLRI 流”，二者最后拼接（见 packet.c）。
 *   - work 故意开得比 max_packet_size 大 BGP_MAX_PACKET_SIZE_OVERFLOW 字节，这样构造
 *     UPDATE 时对每个属性不必逐字节做边界检查（可能溢出部分最终会被截断/丢弃）。 */
static void sync_init(struct update_subgroup *subgrp,
		      struct update_group *updgrp)
{
	struct peer *peer = UPDGRP_PEER(updgrp);

	subgrp->sync =
		XCALLOC(MTYPE_BGP_SYNCHRONISE, sizeof(struct bgp_synchronize));
	bgp_adv_fifo_init(&subgrp->sync->update);
	bgp_adv_fifo_init(&subgrp->sync->withdraw);

	subgrp->hash =
		hash_create(bgp_advertise_attr_hash_key,
			    bgp_advertise_attr_hash_cmp, "BGP SubGroup Hash");

	/* We use a larger buffer for subgrp->work in the event that:
	 * - We RX a BGP_UPDATE where the attributes alone are just
	 *   under 4096 or 65535 (if Extended Message capability negotiated).
	 * - The user configures an outbound route-map that does many as-path
	 *   prepends or adds many communities.  At most they can have
	 * CMD_ARGC_MAX
	 *   args in a route-map so there is a finite limit on how large they
	 * can
	 *   make the attributes.
	 *
	 * Having a buffer with BGP_MAX_PACKET_SIZE_OVERFLOW allows us to avoid
	 * bounds
	 * checking for every single attribute as we construct an UPDATE.
	 */
	subgrp->work = stream_new(peer->max_packet_size
				  + BGP_MAX_PACKET_SIZE_OVERFLOW);
	subgrp->scratch = stream_new(peer->max_packet_size);
}

/* 【sync_delete】sync_init 的逆操作：释放 sync、广告属性哈希、work/scratch 两个流。
 * 参数 subgrp：待销毁的子组。
 * 返回：无。
 * 调用时机：update_subgroup_delete() 销毁子组时。
 * 关键点：hash_clean_and_free 用 bgp_advertise_attr_free 逐个释放 hash 中缓存的
 *   广告属性（baa）；work/scratch 置 NULL 防悬垂。 */
static void sync_delete(struct update_subgroup *subgrp)
{
	XFREE(MTYPE_BGP_SYNCHRONISE, subgrp->sync);
	hash_clean_and_free(&subgrp->hash,
			    (void (*)(void *))bgp_advertise_attr_free);

	if (subgrp->work)
		stream_free(subgrp->work);
	subgrp->work = NULL;
	if (subgrp->scratch)
		stream_free(subgrp->scratch);
	subgrp->scratch = NULL;
}

/* ========== 2. 代表 peer 配置拷贝：conf_copy / conf_release / peer2_updgrp_copy ========== */
/**
 * conf_copy
 *
 * copy only those fields that are relevant to update group match
 */
/* 【conf_copy】把“影响出向报文内容/分组判据”的字段从真实 peer 拷到 update_group 的
 *   代表 peer(conf)。
 * 参数 dst：目标（典型是 updgrp->conf，或哈希查找时的临时 tmp_conf）；
 *      src：源（真实 peer）；afi/safi：本组所在的地址族。
 * 返回：无。
 * 调用时机：updgrp_hash_alloc（建组）、update_group_find（临时构造查找键）、
 *      peer2_updgrp_copy（建组前）都会调用。
 * 关键点：
 *   - 只拷“与报文相关”的子集，而不是整个 struct peer；这也是为什么哈希键
 *     (updgrp_hash_key_make) 能仅从 conf 上算出来。
 *   - 对 name/字符串成员一律 XSTRDUP（浅拷贝指针会指向真实 peer 的字符串，
 *     释放时二次 free），因此必须配对调用 conf_release。
 *   - soo 是做 ecommunity_dup，default_rmap/filter 各个 name 都各自 strdup。 */
static void conf_copy(struct peer *dst, struct peer *src, afi_t afi,
		      safi_t safi)
{
	struct bgp_filter *srcfilter;
	struct bgp_filter *dstfilter;

	srcfilter = &src->filter[afi][safi];
	dstfilter = &dst->filter[afi][safi];

	dst->bgp = src->bgp;
	dst->sort = src->sort;
	dst->sub_sort = src->sub_sort;
	dst->as = src->as;
	dst->v_routeadv = src->v_routeadv;
	dst->flags = src->flags;
	dst->af_flags[afi][safi] = src->af_flags[afi][safi];
	dst->pmax_out[afi][safi] = src->pmax_out[afi][safi];
	dst->max_packet_size = src->max_packet_size;
	XFREE(MTYPE_BGP_PEER_HOST, dst->host);

	dst->host = XSTRDUP(MTYPE_BGP_PEER_HOST, src->host);
	dst->cap = src->cap;
	dst->af_cap[afi][safi] = src->af_cap[afi][safi];
	dst->afc_nego[afi][safi] = src->afc_nego[afi][safi];
	dst->orf_plist[afi][safi] = src->orf_plist[afi][safi];
	dst->addpath_type[afi][safi] = src->addpath_type[afi][safi];
	dst->addpath_best_selected[afi][safi] =
		src->addpath_best_selected[afi][safi];
	dst->addpath_paths_limit[afi][safi] =
		src->addpath_paths_limit[afi][safi];
	dst->local_as = src->local_as;
	dst->change_local_as = src->change_local_as;
	dst->shared_network = src->shared_network;
	dst->local_role = src->local_role;

	if (src->soo[afi][safi]) {
		ecommunity_free(&dst->soo[afi][safi]);
		dst->soo[afi][safi] = ecommunity_dup(src->soo[afi][safi]);
	}

	memcpy(&(dst->nexthop), &(src->nexthop), sizeof(struct bgp_nexthop));

	dst->group = src->group;

	if (src->default_rmap[afi][safi].name) {
		dst->default_rmap[afi][safi].name =
			XSTRDUP(MTYPE_ROUTE_MAP_NAME,
				src->default_rmap[afi][safi].name);
		dst->default_rmap[afi][safi].map =
			src->default_rmap[afi][safi].map;
	}

	if (DISTRIBUTE_OUT_NAME(srcfilter)) {
		DISTRIBUTE_OUT_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, DISTRIBUTE_OUT_NAME(srcfilter));
		DISTRIBUTE_OUT(dstfilter) = DISTRIBUTE_OUT(srcfilter);
	}

	if (PREFIX_LIST_OUT_NAME(srcfilter)) {
		PREFIX_LIST_OUT_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, PREFIX_LIST_OUT_NAME(srcfilter));
		PREFIX_LIST_OUT(dstfilter) = PREFIX_LIST_OUT(srcfilter);
	}

	if (FILTER_LIST_OUT_NAME(srcfilter)) {
		FILTER_LIST_OUT_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, FILTER_LIST_OUT_NAME(srcfilter));
		FILTER_LIST_OUT(dstfilter) = FILTER_LIST_OUT(srcfilter);
	}

	if (ROUTE_MAP_OUT_NAME(srcfilter)) {
		ROUTE_MAP_OUT_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, ROUTE_MAP_OUT_NAME(srcfilter));
		ROUTE_MAP_OUT(dstfilter) = ROUTE_MAP_OUT(srcfilter);
	}

	if (UNSUPPRESS_MAP_NAME(srcfilter)) {
		UNSUPPRESS_MAP_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, UNSUPPRESS_MAP_NAME(srcfilter));
		UNSUPPRESS_MAP(dstfilter) = UNSUPPRESS_MAP(srcfilter);
	}

	if (ADVERTISE_MAP_NAME(srcfilter)) {
		ADVERTISE_MAP_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, ADVERTISE_MAP_NAME(srcfilter));
		ADVERTISE_MAP(dstfilter) = ADVERTISE_MAP(srcfilter);
		ADVERTISE_CONDITION(dstfilter) = ADVERTISE_CONDITION(srcfilter);
	}

	if (CONDITION_MAP_NAME(srcfilter)) {
		CONDITION_MAP_NAME(dstfilter) = XSTRDUP(
			MTYPE_BGP_FILTER_NAME, CONDITION_MAP_NAME(srcfilter));
		CONDITION_MAP(dstfilter) = CONDITION_MAP(srcfilter);
	}

	dstfilter->advmap.update_type = srcfilter->advmap.update_type;
}

/**
 * since we did a bunch of XSTRDUP's in conf_copy, time to free them up
 */
/* 【conf_release】释放 conf_copy 所做的全部 XSTRDUP/ecommunity_dup 副本。
 * 参数 src：conf_copy 填过的 peer；afi/safi：地址族。
 * 返回：无。
 * 调用时机：update_group_find/update_group_create 用完临时 tmp_conf 后；
 *      update_group_delete 删除组时对 updgrp->conf。
 * 易误解点：必须与 conf_copy 严格配对，漏配会内存泄漏，重复调用会 double free。 */
static void conf_release(struct peer *src, afi_t afi, safi_t safi)
{
	struct bgp_filter *srcfilter;

	srcfilter = &src->filter[afi][safi];

	XFREE(MTYPE_ROUTE_MAP_NAME, src->default_rmap[afi][safi].name);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->dlist[FILTER_OUT].name);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->plist[FILTER_OUT].name);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->aslist[FILTER_OUT].name);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->map[RMAP_OUT].name);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->usmap.name);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->advmap.aname);

	XFREE(MTYPE_BGP_FILTER_NAME, srcfilter->advmap.cname);

	XFREE(MTYPE_BGP_PEER_HOST, src->host);

	ecommunity_free(&src->soo[afi][safi]);
}

/* 【peer2_updgrp_copy】用某个 peer_af 的信息初始化一个 update_group：记录 afi/safi/afid/bgp，
 *   并把 peer 的配置拷到 updgrp->conf（代表 peer）。
 * 参数 updgrp：待填充的组（或其栈上临时体 tmp）；paf：来源 peer_af。
 * 返回：无。
 * 调用时机：update_group_find / update_group_create 构造“查找键/新组”时。
 * 关键点：本函数只负责填 afi/safi/afid/bgp 与 conf_copy，不插入哈希表。 */
static void peer2_updgrp_copy(struct update_group *updgrp, struct peer_af *paf)
{
	struct peer *src;
	struct peer *dst;

	if (!updgrp || !paf)
		return;

	src = paf->peer;
	dst = updgrp->conf;
	if (!src || !dst)
		return;

	updgrp->afi = paf->afi;
	updgrp->safi = paf->safi;
	updgrp->afid = paf->afid;
	updgrp->bgp = src->bgp;

	conf_copy(dst, src, paf->afi, paf->safi);
}

/* ========== 3. update_group 哈希表：alloc / key / cmp ========== */
/**
 * auxiliary functions to maintain the hash table.
 * - updgrp_hash_alloc - to create a new entry, passed to hash_get
 * - updgrp_hash_key_make - makes the key for update group search
 * - updgrp_hash_cmp - compare two update groups.
 */
/* 【updgrp_hash_alloc】哈希表的“分配新条目”回调，供 hash_get 在未命中时调用以创建新组。
 * 参数 p：指向栈上临时 update_group 模板（其 conf 指向栈上的 tmp_conf）。
 * 返回：新分配的 update_group（conf 是一份独立堆副本）。
 * 调用时机：hash_get 找不到匹配组、需要新建时。
 * 关键点：先 memcpy 整个模板再“重新分配”conf 与 conf->connection 并再次 conf_copy，
 *   目的是让哈希表长期持有的组拥有自己独立的 conf 内存，而不是引用调用者的栈内存。 */
static void *updgrp_hash_alloc(void *p)
{
	struct update_group *updgrp;
	const struct update_group *in;

	in = (const struct update_group *)p;
	updgrp = XCALLOC(MTYPE_BGP_UPDGRP, sizeof(struct update_group));
	memcpy(updgrp, in, sizeof(struct update_group));
	updgrp->conf = XCALLOC(MTYPE_BGP_PEER, sizeof(struct peer));
	updgrp->conf->connection = XCALLOC(MTYPE_BGP_PEER_CONNECTION,
					   sizeof(struct peer_connection));
	conf_copy(updgrp->conf, in->conf, in->afi, in->safi);
	return updgrp;
}

/* 【updgrp_hash_key_make】根据代表 peer(conf) 计算 update_group 的哈希键。
 * 参数 p：指向 update_group（其 conf 为待计算的代表 peer）。
 * 返回：uint32_t 哈希值。
 * 调用时机：hash_get / hash_lookup 对 paf->peer->bgp->update_groups[afid] 操作时由哈希表回调。
 * 关键点（这是“谁能和谁合并成一组”的判据核心）：
 *   - 键由一系列“影响出向报文内容/策略”的字段 jhash 累加而成：peer_sort(IBGP/EBGP)、
 *     PEER_UPDGRP_FLAGS、PEER_UPDGRP_AF_FLAGS、addpath 类型/上限、CAP/AF_CAP 标志、
 *     MRAI(v_routeadv)、local-as、最大报文长度、pmax_out、peer-group 名、各出向过滤
 *     (route-map/distribute/prefix/filter/unsuppress/advmap)名称、default-originate
 *     route-map 名、shared-network IPv6、lonesoul/ORF/max-prefix-out(独占一组)、
 *     graceful-shutdown、local_role、Link-Local NH 能力、AIGP、SOO、IPv6 NH-local 等。
 *   - 只要键不同就一定落成不同组；键相同还需 updgrp_hash_cmp 二次确认（防哈希碰撞）。
 *   - 【维护铁律】凡新增一个影响出向报文的标志，必须同时加进 PEER_UPDGRP_* 集合、
 *     这里的键、以及 cmp 函数，否则会把本不该合并的 peer 错误合并，产生错发漏发。
 *   - 结尾的 zlog_debug 只是打印键构成，注意注释里反复强调“改了键要同步改调试输出”。 */
/**
 * The hash value for a peer is computed from the following variables:
 * v = f(
 *       1. IBGP (1) or EBGP (2)
 *       2. FLAGS based on configuration:
 *             LOCAL_AS_NO_PREPEND
 *             LOCAL_AS_REPLACE_AS
 *       3. AF_FLAGS based on configuration:
 *             Refer to definition in bgp_updgrp.h
 *       4. (AF-independent) Capability flags:
 *             AS4_RCV capability
 *       5. (AF-dependent) Capability flags:
 *             ORF_PREFIX_SM_RCV (peer can send prefix ORF)
 *       6. MRAI
 *       7. peer-group name
 *       8. Outbound route-map name (neighbor route-map <> out)
 *       9. Outbound distribute-list name (neighbor distribute-list <> out)
 *       10. Outbound prefix-list name (neighbor prefix-list <> out)
 *       11. Outbound as-list name (neighbor filter-list <> out)
 *       12. Unsuppress map name (neighbor unsuppress-map <>)
 *       13. default rmap name (neighbor default-originate route-map <>)
 *       14. encoding both global and link-local nexthop?
 *       15. If peer is configured to be a lonesoul, peer ip address
 *       16. Local-as should match, if configured.
 *       17. maximum-prefix-out
 *       18. Local-role should also match, if configured.
 *       19. Add-Path best selected paths count should match as well
 *      )
 */
static unsigned int updgrp_hash_key_make(const void *p)
{
	const struct update_group *updgrp;
	const struct peer *peer;
	const struct bgp_filter *filter;
	uint64_t flags;
	uint32_t key;
	afi_t afi;
	safi_t safi;

	/*
	 * IF YOU ADD AN ADDITION TO THE HASH KEY TO ENSURE
	 * THAT THE UPDATE GROUP CALCULATION IS CORRECT THEN
	 * PLEASE ADD IT TO THE DEBUG OUTPUT TOO AT THE BOTTOM
	 */
#define SEED1 999331
#define SEED2 2147483647
#define SEED3 4258594758

	updgrp = p;
	peer = updgrp->conf;
	afi = updgrp->afi;
	safi = updgrp->safi;
	flags = peer->af_flags[afi][safi];
	filter = &peer->filter[afi][safi];

	key = 0;

	/* 【关键行】逐个把“判据字段”jhash 进 key；顺序无关，但字段集合即分组语义。
	 * SEED1/2/3 用于给不同字符串来源做区分，降低跨字段碰撞概率。 */
	/* `remote-as auto` technically uses identical peer->sort.
	 * After OPEN message is parsed, this is updated accordingly, but
	 * we need to call the peer_sort() here also to properly create
	 * separate subgroups.
	 */
	key = jhash_1word(peer_sort((struct peer *)peer), key);
	key = jhash_1word(peer->sub_sort, key); /* OAD */
	key = jhash_1word((peer->flags & PEER_UPDGRP_FLAGS), key);
	key = jhash_1word((flags & PEER_UPDGRP_AF_FLAGS), key);
	key = jhash_1word((uint32_t)peer->addpath_type[afi][safi], key);
	key = jhash_1word(peer->addpath_best_selected[afi][safi], key);
	key = jhash_1word(peer->addpath_paths_limit[afi][safi].receive, key);
	key = jhash_1word(peer->addpath_paths_limit[afi][safi].send, key);
	key = jhash_1word((peer->cap & PEER_UPDGRP_CAP_FLAGS), key);
	key = jhash_1word((peer->af_cap[afi][safi] & PEER_UPDGRP_AF_CAP_FLAGS),
			  key);
	key = jhash_1word(peer->v_routeadv, key);
	key = jhash_1word(peer->change_local_as, key);
	key = jhash_1word(peer->max_packet_size, key);
	key = jhash_1word(peer->pmax_out[afi][safi], key);


	if (CHECK_FLAG(peer->flags, PEER_FLAG_AS_LOOP_DETECTION))
		key = jhash_2words(peer->as,
				   CHECK_FLAG(peer->flags,
					      PEER_FLAG_AS_LOOP_DETECTION),
				   key);
	if (peer->group)
		key = jhash_1word(jhash(peer->group->name,
					strlen(peer->group->name), SEED1),
				  key);

	if (filter->map[RMAP_OUT].name)
		key = jhash_1word(jhash(filter->map[RMAP_OUT].name,
					strlen(filter->map[RMAP_OUT].name),
					SEED1),
				  key);

	if (filter->dlist[FILTER_OUT].name)
		key = jhash_1word(jhash(filter->dlist[FILTER_OUT].name,
					strlen(filter->dlist[FILTER_OUT].name),
					SEED1),
				  key);

	if (filter->plist[FILTER_OUT].name)
		key = jhash_1word(jhash(filter->plist[FILTER_OUT].name,
					strlen(filter->plist[FILTER_OUT].name),
					SEED1),
				  key);

	if (filter->aslist[FILTER_OUT].name)
		key = jhash_1word(jhash(filter->aslist[FILTER_OUT].name,
					strlen(filter->aslist[FILTER_OUT].name),
					SEED1),
				  key);

	if (filter->usmap.name)
		key = jhash_1word(jhash(filter->usmap.name,
					strlen(filter->usmap.name), SEED1),
				  key);

	if (filter->advmap.aname)
		key = jhash_1word(jhash(filter->advmap.aname,
					strlen(filter->advmap.aname), SEED1),
				  key);

	if (filter->advmap.update_type)
		key = jhash_1word(filter->advmap.update_type, key);

	if (peer->default_rmap[afi][safi].name)
		key = jhash_1word(
			jhash(peer->default_rmap[afi][safi].name,
			      strlen(peer->default_rmap[afi][safi].name),
			      SEED1),
			key);

	/* If peer is on a shared network and is exchanging IPv6 prefixes,
	 * it needs to include link-local address. That's different from
	 * non-shared-network peers (nexthop encoded with 32 bytes vs 16
	 * bytes). We create different update groups to take care of that.
	 */
	key = jhash_1word(
		(peer->shared_network && peer_afi_active_nego(peer, AFI_IP6)),
		key);
	/*
	 * There are certain peers that must get their own update-group:
	 * - lonesoul peers
	 * - peers that negotiated ORF
	 * - maximum-prefix-out is set
	 */
	/* 【关键行】把 peer->host 混入 key：这三类 peer 必须独占一个 update_group
	 * （lonesoul=强制单独；协商了 ORF 收发者需独立；配了 maximum-prefix-out 者
	 *  因可能中途停发而需独立），用地址区分可保证它们互不合并。 */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_LONESOUL)
	    || CHECK_FLAG(peer->af_cap[afi][safi], PEER_CAP_ORF_PREFIX_SM_RCV)
	    || CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_MAX_PREFIX_OUT))
		key = jhash_1word(jhash(peer->host, strlen(peer->host), SEED2),
				  key);

	if (CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_SHUTDOWN))
		key = jhash_1word(jhash(peer->host, strlen(peer->host), SEED3), key);

	/*
	 * Multiple sessions with the same neighbor should get their own
	 * update-group if they have different roles.
	 */
	key = jhash_1word(peer->local_role, key);

	/* If the peer has disabled Link-Local Next Hop capability, but we
	 * send it, it's not taken into consideration and we always merge both
	 * peers into a single update-group. Make sure peer has its own update-group
	 * if it has disabled (received) Link-Local Next Hop capability.
	 */
	key = jhash_2words(!!CHECK_FLAG(peer->cap, PEER_CAP_LINK_LOCAL_RCV),
			   !!CHECK_FLAG(peer->cap, PEER_CAP_LINK_LOCAL_ADV), key);

	/* Neighbors configured with the AIGP attribute are put in a separate
	 * update group from other neighbors.
	 */
	key = jhash_1word((peer->flags & PEER_FLAG_AIGP), key);

	if (peer->soo[afi][safi]) {
		const char *soo_str = ecommunity_str(peer->soo[afi][safi]);

		key = jhash_1word(jhash(soo_str, strlen(soo_str), SEED1), key);
	}

	if (afi == AFI_IP6 &&
	    (CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_NEXTHOP_LOCAL_UNCHANGED)))
		key = jhash(&peer->nexthop.v6_global, IPV6_MAX_BYTELEN, key);

	/*
	 * ANY NEW ITEMS THAT ARE ADDED TO THE key, ENSURE DEBUG
	 * STATEMENT STAYS UP TO DATE
	 */
	if (bgp_debug_neighbor_events(peer)) {
		zlog_debug("%pBP Update Group Hash: sort: %d sub_sort: %d UpdGrpFlags: %ju UpdGrpAFFlags: %ju",
			   peer, peer->sort, peer->sub_sort,
			   (intmax_t)CHECK_FLAG(peer->flags, PEER_UPDGRP_FLAGS),
			   (intmax_t)CHECK_FLAG(flags, PEER_UPDGRP_AF_FLAGS));
		zlog_debug("%pBP Update Group Hash: addpath: %u UpdGrpCapFlag: %ju UpdGrpCapAFFlag: %u route_adv: %u change local as: %u, as_path_loop_detection: %d",
			   peer, (uint32_t)peer->addpath_type[afi][safi],
			   (intmax_t)CHECK_FLAG(peer->cap,
						PEER_UPDGRP_CAP_FLAGS),
			   CHECK_FLAG(peer->af_cap[afi][safi],
				      PEER_UPDGRP_AF_CAP_FLAGS),
			   peer->v_routeadv, peer->change_local_as,
			   !!CHECK_FLAG(peer->flags,
					PEER_FLAG_AS_LOOP_DETECTION));
		zlog_debug("%pBP Update Group Hash: addpath paths-limit: (send %u, receive %u)",
			   peer, peer->addpath_paths_limit[afi][safi].send,
			   peer->addpath_paths_limit[afi][safi].receive);
		zlog_debug("%pBP Update Group Hash: Link-Local Next Hop capability:%s%s", peer,
			   CHECK_FLAG(peer->cap, PEER_CAP_LINK_LOCAL_RCV) ? " received" : "",
			   CHECK_FLAG(peer->cap, PEER_CAP_LINK_LOCAL_ADV) ? " advertised" : "");
		zlog_debug(
			"%pBP Update Group Hash: max packet size: %u pmax_out: %u Peer Group: %s rmap out: %s",
			peer, peer->max_packet_size, peer->pmax_out[afi][safi],
			peer->group ? peer->group->name : "(NONE)",
			ROUTE_MAP_OUT_NAME(filter) ? ROUTE_MAP_OUT_NAME(filter)
						   : "(NONE)");
		zlog_debug(
			"%pBP Update Group Hash: dlist out: %s plist out: %s aslist out: %s usmap out: %s advmap: %s %d",
			peer,
			DISTRIBUTE_OUT_NAME(filter)
				? DISTRIBUTE_OUT_NAME(filter)
				: "(NONE)",
			PREFIX_LIST_OUT_NAME(filter)
				? PREFIX_LIST_OUT_NAME(filter)
				: "(NONE)",
			FILTER_LIST_OUT_NAME(filter)
				? FILTER_LIST_OUT_NAME(filter)
				: "(NONE)",
			UNSUPPRESS_MAP_NAME(filter)
				? UNSUPPRESS_MAP_NAME(filter)
				: "(NONE)",
			ADVERTISE_MAP_NAME(filter) ? ADVERTISE_MAP_NAME(filter)
						   : "(NONE)",
			filter->advmap.update_type);
		zlog_debug(
			"%pBP Update Group Hash: default rmap: %s shared network and afi active network: %d",
			peer,
			peer->default_rmap[afi][safi].name
				? peer->default_rmap[afi][safi].name
				: "(NONE)",
			peer->shared_network &&
				peer_afi_active_nego(peer, AFI_IP6));
		zlog_debug("%pBP Update Group Hash: Lonesoul: %d ORF prefix: %u max prefix out: %ju",
			   peer, !!CHECK_FLAG(peer->flags, PEER_FLAG_LONESOUL),
			   CHECK_FLAG(peer->af_cap[afi][safi],
				      PEER_CAP_ORF_PREFIX_SM_RCV),
			   (intmax_t)CHECK_FLAG(peer->af_flags[afi][safi],
						PEER_FLAG_MAX_PREFIX_OUT));
		zlog_debug(
			"%pBP Update Group Hash: local role: %u AIGP: %d SOO: %s",
			peer, peer->local_role,
			!!CHECK_FLAG(peer->flags, PEER_FLAG_AIGP),
			peer->soo[afi][safi]
				? ecommunity_str(peer->soo[afi][safi])
				: "(NONE)");
		zlog_debug("%pBP Update Group Hash: IPv6 nexthop-local unchanged: %d IPv6 global %pI6",
			   peer,
			   afi == AFI_IP6 && (CHECK_FLAG(peer->af_flags[afi][safi],
							 PEER_FLAG_NEXTHOP_LOCAL_UNCHANGED)),
			   &peer->nexthop.v6_global);

		zlog_debug("%pBP Update Group Hash key: %u", peer, key);
	}
	return key;
}

/* 【updgrp_hash_cmp】哈希表碰撞时的精确相等比较：判断两个 update_group 是否可视为同一组。
 * 参数 p1/p2：两个 update_group。
 * 返回：true=相同（可复用同一组），false=不同。
 * 调用时机：hash_lookup/hash_get 在哈希键相同时做的“二次判等”。
 * 关键点：这里必须与 updgrp_hash_key_make 的字段集合**严格对应**——凡是进键的字段，
 *   这里都要做全量比较（键用位掩码，这里也用同样的位掩码与字符串 strcmp），否则哈希
 *   碰撞时可能把不同配置的 peer 误判为同组。逐项：sort、PEER_UPDGRP_FLAGS、local-as、
 *   pmax_out、AF_FLAGS、addpath 类型、CAP/AF_CAP、AS-loop 时 remote-as、v_routeadv、
 *   peer-group 指针、local_role、各出向过滤名、shared_network、lonesoul/ORF 的地址、
 *   IPv6 NH-local。 */
static bool updgrp_hash_cmp(const void *p1, const void *p2)
{
	const struct update_group *grp1;
	const struct update_group *grp2;
	const struct peer *pe1;
	const struct peer *pe2;
	uint64_t flags1;
	uint64_t flags2;
	const struct bgp_filter *fl1;
	const struct bgp_filter *fl2;
	afi_t afi;
	safi_t safi;

	if (!p1 || !p2)
		return false;

	grp1 = p1;
	grp2 = p2;
	pe1 = grp1->conf;
	pe2 = grp2->conf;
	afi = grp1->afi;
	safi = grp1->safi;
	flags1 = pe1->af_flags[afi][safi];
	flags2 = pe2->af_flags[afi][safi];
	fl1 = &pe1->filter[afi][safi];
	fl2 = &pe2->filter[afi][safi];

	/* put EBGP and IBGP peers in different update groups */
	if (pe1->sort != pe2->sort)
		return false;

	/* check peer flags */
	if ((pe1->flags & PEER_UPDGRP_FLAGS)
	    != (pe2->flags & PEER_UPDGRP_FLAGS))
		return false;

	/* If there is 'local-as' configured, it should match. */
	if (pe1->change_local_as != pe2->change_local_as)
		return false;

	if (pe1->pmax_out[afi][safi] != pe2->pmax_out[afi][safi])
		return false;

	/* flags like route reflector client */
	if ((flags1 & PEER_UPDGRP_AF_FLAGS) != (flags2 & PEER_UPDGRP_AF_FLAGS))
		return false;

	if (pe1->addpath_type[afi][safi] != pe2->addpath_type[afi][safi])
		return false;

	if ((pe1->cap & PEER_UPDGRP_CAP_FLAGS)
	    != (pe2->cap & PEER_UPDGRP_CAP_FLAGS))
		return false;

	/* For aspath loop detection, the remote-as should match */
	if (CHECK_FLAG(pe1->flags, PEER_FLAG_AS_LOOP_DETECTION) && (pe1->as != pe2->as))
		return false;

	if ((pe1->af_cap[afi][safi] & PEER_UPDGRP_AF_CAP_FLAGS)
	    != (pe2->af_cap[afi][safi] & PEER_UPDGRP_AF_CAP_FLAGS))
		return false;

	if (pe1->v_routeadv != pe2->v_routeadv)
		return false;

	if (pe1->group != pe2->group)
		return false;

	/* Roles can affect filtering */
	if (pe1->local_role != pe2->local_role)
		return false;

	/* route-map names should be the same */
	if ((fl1->map[RMAP_OUT].name && !fl2->map[RMAP_OUT].name)
	    || (!fl1->map[RMAP_OUT].name && fl2->map[RMAP_OUT].name)
	    || (fl1->map[RMAP_OUT].name && fl2->map[RMAP_OUT].name
		&& strcmp(fl1->map[RMAP_OUT].name, fl2->map[RMAP_OUT].name)))
		return false;

	if ((fl1->dlist[FILTER_OUT].name && !fl2->dlist[FILTER_OUT].name)
	    || (!fl1->dlist[FILTER_OUT].name && fl2->dlist[FILTER_OUT].name)
	    || (fl1->dlist[FILTER_OUT].name && fl2->dlist[FILTER_OUT].name
		&& strcmp(fl1->dlist[FILTER_OUT].name,
			  fl2->dlist[FILTER_OUT].name)))
		return false;

	if ((fl1->plist[FILTER_OUT].name && !fl2->plist[FILTER_OUT].name)
	    || (!fl1->plist[FILTER_OUT].name && fl2->plist[FILTER_OUT].name)
	    || (fl1->plist[FILTER_OUT].name && fl2->plist[FILTER_OUT].name
		&& strcmp(fl1->plist[FILTER_OUT].name,
			  fl2->plist[FILTER_OUT].name)))
		return false;

	if ((fl1->aslist[FILTER_OUT].name && !fl2->aslist[FILTER_OUT].name)
	    || (!fl1->aslist[FILTER_OUT].name && fl2->aslist[FILTER_OUT].name)
	    || (fl1->aslist[FILTER_OUT].name && fl2->aslist[FILTER_OUT].name
		&& strcmp(fl1->aslist[FILTER_OUT].name,
			  fl2->aslist[FILTER_OUT].name)))
		return false;

	if ((fl1->usmap.name && !fl2->usmap.name)
	    || (!fl1->usmap.name && fl2->usmap.name)
	    || (fl1->usmap.name && fl2->usmap.name
		&& strcmp(fl1->usmap.name, fl2->usmap.name)))
		return false;

	if ((fl1->advmap.aname && !fl2->advmap.aname)
	    || (!fl1->advmap.aname && fl2->advmap.aname)
	    || (fl1->advmap.aname && fl2->advmap.aname
		&& strcmp(fl1->advmap.aname, fl2->advmap.aname)))
		return false;

	if (fl1->advmap.update_type != fl2->advmap.update_type)
		return false;

	if ((pe1->default_rmap[afi][safi].name
	     && !pe2->default_rmap[afi][safi].name)
	    || (!pe1->default_rmap[afi][safi].name
		&& pe2->default_rmap[afi][safi].name)
	    || (pe1->default_rmap[afi][safi].name
		&& pe2->default_rmap[afi][safi].name
		&& strcmp(pe1->default_rmap[afi][safi].name,
			  pe2->default_rmap[afi][safi].name)))
		return false;

	if ((afi == AFI_IP6) && (pe1->shared_network != pe2->shared_network))
		return false;

	if ((CHECK_FLAG(pe1->flags, PEER_FLAG_LONESOUL) ||
	     CHECK_FLAG(pe1->af_cap[afi][safi], PEER_CAP_ORF_PREFIX_SM_RCV)) &&
	    !sockunion_same(&pe1->connection->su, &pe2->connection->su))
		return false;

	if (afi == AFI_IP6 &&
	    (CHECK_FLAG(flags1, PEER_FLAG_NEXTHOP_LOCAL_UNCHANGED) ||
	     CHECK_FLAG(flags2, PEER_FLAG_NEXTHOP_LOCAL_UNCHANGED)) &&
	    !IPV6_ADDR_SAME(&pe1->nexthop.v6_global, &pe2->nexthop.v6_global))
		return false;

	return true;
}

/* 【peer_lonesoul_or_not】设置/清除某 peer 的 PEER_FLAG_LONESOUL（“独占一组”标志）。
 * 参数 peer：目标 peer；set：1=设为 lonesoul，0=取消。
 * 返回：无。
 * 调用时机：配置 `neighbor ... solo` 之类的命令经 update_group_adjust_soloness 调用。
 * 关键点：LONESOUL 是分组判据之一，状态变化后必须 update_group_adjust_peer_afs() 重新
 *   计算分组/迁移 peer——否则 peer 会滞留在错误的组内。状态未变则提前返回避免空转。 */
static void peer_lonesoul_or_not(struct peer *peer, int set)
{
	/* no change in status? */
	if (set == (CHECK_FLAG(peer->flags, PEER_FLAG_LONESOUL) > 0))
		return;

	if (set)
		SET_FLAG(peer->flags, PEER_FLAG_LONESOUL);
	else
		UNSET_FLAG(peer->flags, PEER_FLAG_LONESOUL);

	update_group_adjust_peer_afs(peer);
}

/*
 * subgroup_total_packets_enqueued
 *
 * Returns the total number of packets enqueued to a subgroup.
 */
/* 【subgroup_total_packets_enqueued】返回该子组“累计已入队”的报文序号。
 * 参数 subgrp：目标子组。
 * 返回：末位 bpacket（哨兵）的 ver-1，即累计报文数/序号。
 * 调用时机：show 命令统计展示。
 * 关键点：bpacket 用单调递增的 ver 序号；队列尾部始终有一个 buffer==NULL 的哨兵，
 *   其 ver 比最后一条真实报文大 1，所以减 1 才是“已入队总数”。 */
static unsigned int
subgroup_total_packets_enqueued(struct update_subgroup *subgrp)
{
	struct bpacket *pkt;

	pkt = bpacket_queue_last(SUBGRP_PKTQ(subgrp));

	return pkt->ver - 1;
}

/* 【update_group_show_walkcb】`show bgp update-group` 的遍历回调：把一个组的详情
 *   （代表 peer 配置、每个子组的统计、成员 peer、报文队列水位等）输出到 vty 或 JSON。
 * 参数 updgrp：当前组；arg：struct updwalk_context（含 vty/uj/subgrp_id 过滤等）。
 * 返回：UPDWALK_CONTINUE（继续遍历）。
 * 调用时机：update_group_show() 经 update_group_af_walk 遍历时。
 * 易误解点：ctx->subgrp_id 非 0 时只显示匹配的子组；不匹配时直接返回 CMD_SUCCESS
 *   （注释说因为是 walk 回调，不能在这里报错，只能静默返回）。 */
static int update_group_show_walkcb(struct update_group *updgrp, void *arg)
{
	struct updwalk_context *ctx = arg;
	struct vty *vty;
	struct update_subgroup *subgrp;
	struct peer_af *paf;
	struct bgp_filter *filter;
	struct peer *peer = UPDGRP_PEER(updgrp);
	int match = 0;
	json_object *json_updgrp = NULL;
	json_object *json_subgrps = NULL;
	json_object *json_subgrp = NULL;
	json_object *json_time = NULL;
	json_object *json_subgrp_time = NULL;
	json_object *json_subgrp_event = NULL;
	json_object *json_peers = NULL;
	json_object *json_pkt_info = NULL;
	time_t epoch_tbuf, tbuf;
	char timebuf[32];

	if (!ctx)
		return CMD_SUCCESS;

	if (ctx->subgrp_id) {
		UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
			if (ctx->subgrp_id && (ctx->subgrp_id != subgrp->id))
				continue;
			else {
				match = 1;
				break;
			}
		}
	} else {
		match = 1;
	}

	if (!match) {
		/* Since this routine is invoked from a walk, we cannot signal
		 * any */
		/* error here, can only return. */
		return CMD_SUCCESS;
	}

	vty = ctx->vty;

	if (ctx->uj) {
		json_updgrp = json_object_new_object();
		/* Display json o/p */
		tbuf = monotime(NULL);
		tbuf -= updgrp->uptime;
		epoch_tbuf = time(NULL) - tbuf;
		json_time = json_object_new_object();
		json_object_int_add(json_time, "epoch", epoch_tbuf);
		json_object_string_add(json_time, "epochString",
				       time_to_string_json(updgrp->uptime, timebuf));
		json_object_object_add(json_updgrp, "groupCreateTime",
				       json_time);
		json_object_string_add(json_updgrp, "afi",
				       afi2str(updgrp->afi));
		json_object_string_add(json_updgrp, "safi",
				       safi2str(updgrp->safi));
	} else {
		vty_out(vty, "Update-group %" PRIu64 ":\n", updgrp->id);
		vty_out(vty, "  Created: %s", time_to_string(updgrp->uptime, timebuf));
	}

	filter = &updgrp->conf->filter[updgrp->afi][updgrp->safi];
	if (filter->map[RMAP_OUT].name) {
		if (ctx->uj)
			json_object_string_add(json_updgrp, "outRouteMap",
					       filter->map[RMAP_OUT].name);
		else
			vty_out(vty, "  Outgoing route map: %s\n",
				filter->map[RMAP_OUT].name);
	}

	if (ctx->uj)
		json_object_int_add(json_updgrp, "minRouteAdvInt",
				    updgrp->conf->v_routeadv);
	else
		vty_out(vty, "  MRAI value (seconds): %d\n",
			updgrp->conf->v_routeadv);

	if (updgrp->conf->change_local_as) {
		if (ctx->uj) {
			json_object_int_add(json_updgrp, "localAs",
					    updgrp->conf->change_local_as);
			json_object_boolean_add(
				json_updgrp, "noPrepend",
				CHECK_FLAG(updgrp->conf->flags,
					   PEER_FLAG_LOCAL_AS_NO_PREPEND));
			json_object_boolean_add(
				json_updgrp, "replaceLocalAs",
				CHECK_FLAG(updgrp->conf->flags,
					   PEER_FLAG_LOCAL_AS_REPLACE_AS));
			json_object_boolean_add(json_updgrp, "dualAs",
						CHECK_FLAG(updgrp->conf->flags,
							   PEER_FLAG_DUAL_AS));
		} else {
			vty_out(vty, "  Local AS %u%s%s%s\n",
				updgrp->conf->change_local_as,
				CHECK_FLAG(updgrp->conf->flags,
					   PEER_FLAG_LOCAL_AS_NO_PREPEND)
					? " no-prepend"
					: "",
				CHECK_FLAG(updgrp->conf->flags,
					   PEER_FLAG_LOCAL_AS_REPLACE_AS)
					? " replace-as"
					: "",
				CHECK_FLAG(updgrp->conf->flags,
					   PEER_FLAG_DUAL_AS)
					? " dual-as"
					: "");
		}
	}
	if (ctx->uj)
		json_subgrps = json_object_new_array();
	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		if (ctx->subgrp_id && (ctx->subgrp_id != subgrp->id))
			continue;
		if (ctx->uj) {
			json_subgrp = json_object_new_object();
			json_object_int_add(json_subgrp, "subGroupId",
					    subgrp->id);
			tbuf = monotime(NULL);
			tbuf -= subgrp->uptime;
			epoch_tbuf = time(NULL) - tbuf;
			json_subgrp_time = json_object_new_object();
			json_object_int_add(json_subgrp_time, "epoch",
					    epoch_tbuf);
			json_object_string_add(json_subgrp_time, "epochString",
					       time_to_string_json(subgrp->uptime, timebuf));
			json_object_object_add(json_subgrp, "groupCreateTime",
					       json_subgrp_time);
		} else {
			vty_out(vty, "\n");
			vty_out(vty, "  Update-subgroup %" PRIu64 ":\n",
				subgrp->id);
			vty_out(vty, "    Created: %s", time_to_string(subgrp->uptime, timebuf));
		}

		if (subgrp->split_from.update_group_id
		    || subgrp->split_from.subgroup_id) {
			if (ctx->uj) {
				json_object_int_add(
					json_subgrp, "splitGroupId",
					subgrp->split_from.update_group_id);
				json_object_int_add(
					json_subgrp, "splitSubGroupId",
					subgrp->split_from.subgroup_id);
			} else {
				vty_out(vty,
					"    Split from group id: %" PRIu64
					"\n",
					subgrp->split_from.update_group_id);
				vty_out(vty,
					"    Split from subgroup id: %" PRIu64
					"\n",
					subgrp->split_from.subgroup_id);
			}
		}

		if (ctx->uj) {
			json_subgrp_event = json_object_new_object();
			json_object_int_add(json_subgrp_event, "joinEvents",
					    subgrp->join_events);
			json_object_int_add(json_subgrp_event, "pruneEvents",
					    subgrp->prune_events);
			json_object_int_add(json_subgrp_event, "mergeEvents",
					    subgrp->merge_events);
			json_object_int_add(json_subgrp_event, "splitEvents",
					    subgrp->split_events);
			json_object_int_add(json_subgrp_event, "switchEvents",
					    subgrp->updgrp_switch_events);
			json_object_int_add(json_subgrp_event,
					    "peerRefreshEvents",
					    subgrp->peer_refreshes_combined);
			json_object_int_add(json_subgrp_event,
					    "mergeCheckEvents",
					    subgrp->merge_checks_triggered);
			json_object_object_add(json_subgrp, "statistics",
					       json_subgrp_event);
			json_object_int_add(json_subgrp, "coalesceTime",
					    (UPDGRP_INST(subgrp->update_group))
						    ->coalesce_time);
			json_object_int_add(json_subgrp, "version",
					    subgrp->version);
			json_pkt_info = json_object_new_object();
			json_object_int_add(
				json_pkt_info, "qeueueLen",
				bpacket_queue_length(SUBGRP_PKTQ(subgrp)));
			json_object_int_add(
				json_pkt_info, "queuedTotal",
				subgroup_total_packets_enqueued(subgrp));
			json_object_int_add(
				json_pkt_info, "queueHwmLen",
				bpacket_queue_hwm_length(SUBGRP_PKTQ(subgrp)));
			json_object_int_add(
				json_pkt_info, "totalEnqueued",
				subgroup_total_packets_enqueued(subgrp));
			json_object_object_add(json_subgrp, "packetQueueInfo",
					       json_pkt_info);
			json_object_int_add(json_subgrp, "adjListCount",
					    subgrp->adj_count);
			json_object_boolean_add(
				json_subgrp, "needsRefresh",
				CHECK_FLAG(subgrp->flags,
					   SUBGRP_FLAG_NEEDS_REFRESH));
		} else {
			vty_out(vty, "    Join events: %u\n",
				subgrp->join_events);
			vty_out(vty, "    Prune events: %u\n",
				subgrp->prune_events);
			vty_out(vty, "    Merge events: %u\n",
				subgrp->merge_events);
			vty_out(vty, "    Split events: %u\n",
				subgrp->split_events);
			vty_out(vty, "    Update group switch events: %u\n",
				subgrp->updgrp_switch_events);
			vty_out(vty, "    Peer refreshes combined: %u\n",
				subgrp->peer_refreshes_combined);
			vty_out(vty, "    Merge checks triggered: %u\n",
				subgrp->merge_checks_triggered);
			vty_out(vty, "    Coalesce Time: %u%s\n",
				(UPDGRP_INST(subgrp->update_group))
					->coalesce_time,
				subgrp->t_coalesce ? "(Running)" : "");
			vty_out(vty, "    Version: %" PRIu64 "\n",
				subgrp->version);
			vty_out(vty, "    Packet queue length: %d\n",
				bpacket_queue_length(SUBGRP_PKTQ(subgrp)));
			vty_out(vty, "    Total packets enqueued: %u\n",
				subgroup_total_packets_enqueued(subgrp));
			vty_out(vty, "    Packet queue high watermark: %d\n",
				bpacket_queue_hwm_length(SUBGRP_PKTQ(subgrp)));
			vty_out(vty, "    Adj-out list count: %u\n",
				subgrp->adj_count);
			vty_out(vty, "    Advertise list: %s\n",
				advertise_list_is_empty(subgrp) ? "empty"
								: "not empty");
			vty_out(vty, "    Flags: %s\n",
				CHECK_FLAG(subgrp->flags,
					   SUBGRP_FLAG_NEEDS_REFRESH)
					? "R"
					: "");
			if (peer)
				vty_out(vty, "    Max packet size: %d\n",
					peer->max_packet_size);
		}
		if (subgrp->peer_count > 0) {
			if (ctx->uj) {
				json_peers = json_object_new_array();
				SUBGRP_FOREACH_PEER (subgrp, paf) {
					json_object *jpeer =
						json_object_new_string(
							paf->peer->host);
					json_object_array_add(json_peers, jpeer);
				}
				json_object_object_add(json_subgrp, "peers",
						       json_peers);
			} else {
				vty_out(vty, "    Peers:\n");
				SUBGRP_FOREACH_PEER (subgrp, paf)
					vty_out(vty, "      - %s\n",
						paf->peer->host);
			}
		}

		if (ctx->uj)
			json_object_array_add(json_subgrps, json_subgrp);
	}

	if (ctx->uj) {
		json_object_object_add(json_updgrp, "subGroup", json_subgrps);
		json_object_object_addf(ctx->json_updategrps, json_updgrp,
					"%" PRIu64, updgrp->id);
	}

	return UPDWALK_CONTINUE;
}

/*
 * Helper function to show the packet queue for each subgroup of update group.
 * Will be constrained to a particular subgroup id if id !=0
 */
/* 【updgrp_show_packet_queue_walkcb】`show ... packet-queue` 的回调：对组内每个子组
 *   打印其 bpacket 队列。
 * 参数 updgrp：当前组；arg：updwalk_context（vty/subgrp_id 过滤）。
 * 返回：UPDWALK_CONTINUE。
 * 调用时机：update_group_show_packet_queue() 遍历时。 */
static int updgrp_show_packet_queue_walkcb(struct update_group *updgrp,
					   void *arg)
{
	struct updwalk_context *ctx = arg;
	struct update_subgroup *subgrp;
	struct vty *vty;

	vty = ctx->vty;
	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		if (ctx->subgrp_id && (ctx->subgrp_id != subgrp->id))
			continue;
		vty_out(vty, "update group %" PRIu64 ", subgroup %" PRIu64 "\n",
			updgrp->id, subgrp->id);
		bpacket_queue_show_vty(SUBGRP_PKTQ(subgrp), vty);
	}
	return UPDWALK_CONTINUE;
}

/*
 * Show the packet queue for each subgroup of update group. Will be
 * constrained to a particular subgroup id if id !=0
 */
/* 【update_group_show_packet_queue】`show bgp update-group packet-queue` 的入口骨架：
 *   填好 updwalk_context 后 af-walk 遍历输出。参数 id 用于只显示某个子组(0=全部)。 */
void update_group_show_packet_queue(struct bgp *bgp, afi_t afi, safi_t safi,
				    struct vty *vty, uint64_t id)
{
	struct updwalk_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.vty = vty;
	ctx.subgrp_id = id;
	ctx.flags = 0;
	update_group_af_walk(bgp, afi, safi, updgrp_show_packet_queue_walkcb,
			     &ctx);
}

/* 【update_group_find】按 peer_af 在 (afi,safi) 哈希表中查找已存在的 update_group。
 * 参数 paf：待查的 peer_af。
 * 返回：匹配的 update_group，找不到或 peer 未建立时返回 NULL。
 * 调用时机：update_group_adjust_peer() 决定复用还是新建组时。
 * 关键点：
 *   - 先在栈上构造一个临时 update_group tmp（conf 指向栈上 tmp_conf），用
 *     peer2_updgrp_copy 填好分组判据，再 hash_lookup。
 *   - 查完必须 conf_release(&tmp_conf,...) 释放 conf_copy 里 strdup 出来的字符串，
 *     否则每次查找都泄漏。 */
static struct update_group *update_group_find(struct peer_af *paf)
{
	struct update_group *updgrp;
	struct update_group tmp;
	struct peer tmp_conf;
	struct peer_connection tmp_connection;

	if (!peer_established((PAF_PEER(paf))->connection))
		return NULL;

	memset(&tmp, 0, sizeof(tmp));
	memset(&tmp_conf, 0, sizeof(tmp_conf));
	memset(&tmp_connection, 0, sizeof(struct peer_connection));

	tmp.conf = &tmp_conf;
	tmp_conf.connection = &tmp_connection;

	peer2_updgrp_copy(&tmp, paf);

	updgrp = hash_lookup(paf->peer->bgp->update_groups[paf->afid], &tmp);
	conf_release(&tmp_conf, paf->afi, paf->safi);
	return updgrp;
}

/* 【update_group_create】为 peer_af 创建（或复用）一个 update_group 并登记全局统计。
 * 参数 paf：待入组的 peer_af。
 * 返回：哈希表中的 update_group（可能是新建的，也可能 hash_get 命中了已存在组）。
 * 调用时机：update_group_adjust_peer() 在 update_group_find 返回 NULL 后调用。
 * 关键点：hash_get 未命中时用 updgrp_hash_alloc 生成拥有独立 conf 内存的新组；
 *   命中时直接返回已有组（此时 hash_get 内部不会调用 alloc）。用完释放临时 conf。 */
static struct update_group *update_group_create(struct peer_af *paf)
{
	struct update_group *updgrp;
	struct update_group tmp;
	struct peer tmp_conf;
	struct peer_connection tmp_connection;

	memset(&tmp, 0, sizeof(tmp));
	memset(&tmp_conf, 0, sizeof(tmp_conf));
	memset(&tmp_connection, 0, sizeof(tmp_connection));

	tmp.conf = &tmp_conf;
	tmp_conf.connection = &tmp_connection;
	peer2_updgrp_copy(&tmp, paf);

	updgrp = hash_get(paf->peer->bgp->update_groups[paf->afid], &tmp,
			  updgrp_hash_alloc);
	update_group_checkin(updgrp);

	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("create update group %" PRIu64, updgrp->id);
	frrtrace(2, frr_bgp, ug_create_delete, 1, updgrp->id);

	UPDGRP_GLOBAL_STAT(updgrp, updgrps_created) += 1;

	conf_release(&tmp_conf, paf->afi, paf->safi);
	return updgrp;
}

/* 【update_group_delete】从哈希表移除并彻底释放一个 update_group。
 * 参数 updgrp：待删除的组（应已无任何子组）。
 * 返回：无。
 * 调用时机：update_group_remove_subgroup() 发现组内已无子组时。
 * 关键点：释放顺序——先 hash_release 出表，再 conf_release 释放 conf 内字符串，
 *   再 free conf 内 host/ifname/connection 与 conf 本身，最后 free updgrp。 */
static void update_group_delete(struct update_group *updgrp)
{
	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("delete update group %" PRIu64, updgrp->id);
	frrtrace(2, frr_bgp, ug_create_delete, 2, updgrp->id);

	UPDGRP_GLOBAL_STAT(updgrp, updgrps_deleted) += 1;

	hash_release(updgrp->bgp->update_groups[updgrp->afid], updgrp);
	conf_release(updgrp->conf, updgrp->afi, updgrp->safi);

	XFREE(MTYPE_BGP_PEER_HOST, updgrp->conf->host);

	XFREE(MTYPE_BGP_PEER_IFNAME, updgrp->conf->ifname);

	XFREE(MTYPE_BGP_PEER_CONNECTION, updgrp->conf->connection);
	XFREE(MTYPE_BGP_PEER, updgrp->conf);
	XFREE(MTYPE_BGP_UPDGRP, updgrp);
}

/* 【update_group_add_subgroup】把一个子组挂到组内链表头部并回填 back-pointer。
 * 参数 updgrp：父组；subgrp：子组。
 * 返回：无。
 * 调用时机：update_subgroup_create 新建子组时；update_subgroup_split_peer 换组时重新挂接。 */
static void update_group_add_subgroup(struct update_group *updgrp,
				      struct update_subgroup *subgrp)
{
	if (!updgrp || !subgrp)
		return;

	LIST_INSERT_HEAD(&(updgrp->subgrps), subgrp, updgrp_train);
	subgrp->update_group = updgrp;
}

/* 【update_group_remove_subgroup】从组内链表摘下子组并清空 back-pointer；
 *   若摘完组内已空，则连带删除该 update_group。
 * 参数 updgrp：父组；subgrp：子组。
 * 返回：无。
 * 调用时机：update_subgroup_delete 销毁子组时；update_subgroup_split_peer 换组时。 */
static void update_group_remove_subgroup(struct update_group *updgrp,
					 struct update_subgroup *subgrp)
{
	if (!updgrp || !subgrp)
		return;

	LIST_REMOVE(subgrp, updgrp_train);
	subgrp->update_group = NULL;
	if (LIST_EMPTY(&(updgrp->subgrps)))
		update_group_delete(updgrp);
}

/* ========== 4. update_subgroup 生命周期与成员增删 ========== */
/* 【update_subgroup_create】新建一个 update_subgroup 并挂到父组。
 * 参数 updgrp：父 update_group。
 * 返回：新子组。
 * 调用时机：update_group_adjust_peer / update_subgroup_split_peer 需要为 peer 单开一组时。
 * 关键点：
 *   - v_coalesce 初始化为实例的 coalesce_time——新子组不立即通告，先等合并延迟，
 *     让更多邻居并进来（见 bgp_updgrp.h 的合并延迟公式）。
 *   - bpacket_queue_add(q, NULL, NULL) 入队一个 buffer==NULL 的“哨兵”空包：它是队列
 *     尾部占位，peer 先挂在哨兵上表示“不接收当前队列里的报文”，新报文入队时哨兵被
 *     填充、其后再生新哨兵。 */
static struct update_subgroup *
update_subgroup_create(struct update_group *updgrp)
{
	struct update_subgroup *subgrp;

	subgrp = XCALLOC(MTYPE_BGP_UPD_SUBGRP, sizeof(struct update_subgroup));
	update_subgroup_checkin(subgrp, updgrp);
	subgrp->v_coalesce = (UPDGRP_INST(updgrp))->coalesce_time;
	sync_init(subgrp, updgrp);
	bpacket_queue_init(SUBGRP_PKTQ(subgrp));
	bpacket_queue_add(SUBGRP_PKTQ(subgrp), NULL, NULL);
	TAILQ_INIT(&(subgrp->adjq));
	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("create subgroup u%" PRIu64 ":s%" PRIu64, updgrp->id,
			   subgrp->id);
	frrtrace(3, frr_bgp, ug_subgroup_create_delete, 1, updgrp->id, subgrp->id);

	update_group_add_subgroup(updgrp, subgrp);

	UPDGRP_INCR_STAT(updgrp, subgrps_created);

	return subgrp;
}

/* 【update_subgroup_delete】销毁子组：取消定时器、清空报文队列与 adj 表、释放 sync，
 *   从父组摘下，最后 free 自身。
 * 参数 subgrp：待销毁子组（其成员应已全部移出）。
 * 返回：无。
 * 调用时机：update_subgroup_check_delete() 判定组内无 peer 后。
 * 关键点：subgroup_clear_table 释放所有 bgp_adj_out；update_group_remove_subgroup 可能
 *   连锁删除已空的父 update_group。 */
static void update_subgroup_delete(struct update_subgroup *subgrp)
{
	if (!subgrp)
		return;

	if (subgrp->update_group)
		UPDGRP_INCR_STAT(subgrp->update_group, subgrps_deleted);

	event_cancel(&subgrp->t_merge_check);
	event_cancel(&subgrp->t_coalesce);

	bpacket_queue_cleanup(SUBGRP_PKTQ(subgrp));
	subgroup_clear_table(subgrp);

	sync_delete(subgrp);

	if (BGP_DEBUG(update_groups, UPDATE_GROUPS) && subgrp->update_group)
		zlog_debug("delete subgroup u%" PRIu64 ":s%" PRIu64,
			   subgrp->update_group->id, subgrp->id);
	if (subgrp->update_group)
		frrtrace(3, frr_bgp, ug_subgroup_create_delete, 2, subgrp->update_group->id,
			 subgrp->id);

	update_group_remove_subgroup(subgrp->update_group, subgrp);

	XFREE(MTYPE_BGP_UPD_SUBGRP, subgrp);
}

/* 【update_subgroup_inherit_info】把来源子组的 sflags 状态位继承给新子组。
 * 参数 to：新子组；from：旧子组。
 * 返回：无。
 * 调用时机：update_subgroup_split_peer 分裂时拷贝状态。
 * 易误解点：只继承 sflags（如 default-originate、force-updates 等“状态”标志），
 *   不继承 version/adjq 等需要另行处理的内容。 */
void update_subgroup_inherit_info(struct update_subgroup *to,
				  struct update_subgroup *from)
{
	if (!to || !from)
		return;

	to->sflags = from->sflags;
}

/*
 * update_subgroup_check_delete
 *
 * Delete a subgroup if it is ready to be deleted.
 *
 * Returns true if the subgroup was deleted.
 */
/* 【update_subgroup_check_delete】若子组已无成员则删除之。
 * 参数 subgrp：待检查子组。
 * 返回：true=已删除；false=还有人，未删。
 * 调用时机：remove_peer 系列、merge 之后。
 * 关键点：删除是“按需触发”的——子组只有在最后一个 peer 离开时才会消失。 */
static bool update_subgroup_check_delete(struct update_subgroup *subgrp)
{
	if (!subgrp)
		return false;

	if (!LIST_EMPTY(&(subgrp->peers)))
		return false;

	update_subgroup_delete(subgrp);

	return true;
}

/*
 * update_subgroup_add_peer
 *
 * @param send_enqueued_packets If true all currently enqueued packets will
 *                              also be sent to the peer.
 */
/* 【update_subgroup_add_peer】把一个 peer_af 加入子组，并决定它从队列的哪一条开始接收。
 * 参数 subgrp：目标子组；paf：待加 peer；send_enqueued_pkts：是否连当前已入队的
 *     报文也发给它（1=从头 bpacket_queue_first 开始，0=只从尾部哨兵开始，即跳过现存队列）。
 * 返回：无。
 * 调用时机：update_group_adjust_peer（新 peer 入组）、update_subgroup_split_peer、
 *     update_subgroup_merge（合并时传 0，避免向新成员重发旧报文造成无谓的撤销+再通告）。
 * 关键点：bpacket_add_peer 会把 paf->next_pkt_to_send 指到所选报文；peer 出队即沿队列前进。 */
static void update_subgroup_add_peer(struct update_subgroup *subgrp,
				     struct peer_af *paf,
				     int send_enqueued_pkts)
{
	struct bpacket *pkt;

	if (!subgrp || !paf)
		return;

	LIST_INSERT_HEAD(&(subgrp->peers), paf, subgrp_train);
	paf->subgroup = subgrp;
	subgrp->peer_count++;

	if (bgp_debug_peer_updout_enabled(paf->peer->host)) {
		UPDGRP_PEER_DBG_EN(subgrp->update_group);
	}

	SUBGRP_INCR_STAT(subgrp, join_events);

	if (send_enqueued_pkts) {
		pkt = bpacket_queue_first(SUBGRP_PKTQ(subgrp));
	} else {

		/*
		 * Hang the peer off of the last, placeholder, packet in the
		 * queue. This means it won't see any of the packets that are
		 * currently the queue.
		 */
		pkt = bpacket_queue_last(SUBGRP_PKTQ(subgrp));
		assert(pkt->buffer == NULL);
	}

	bpacket_add_peer(pkt, paf);

	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("peer %s added to subgroup s%" PRIu64,
				paf->peer->host, subgrp->id);
	frrtrace(7, frr_bgp, ug_subgroup_add_remove_peer, 1, paf->peer->host, paf->afi, paf->safi,
		 paf->afid, subgrp->id, subgrp->peer_count);
}

/*
 * update_subgroup_remove_peer_internal
 *
 * Internal function that removes a peer from a subgroup, but does not
 * delete the subgroup. A call to this function must almost always be
 * followed by a call to update_subgroup_check_delete().
 *
 * @see update_subgroup_remove_peer
 */
/* 【update_subgroup_remove_peer_internal】把 peer_af 从子组摘除，但**不**删除子组。
 * 参数 subgrp：源子组；paf：待移除 peer。
 * 返回：无。
 * 调用时机：update_subgroup_remove_peer、update_subgroup_merge 移出成员时。
 * 关键点：调用者通常随后要调用 update_subgroup_check_delete() 判空删除（此处不删）；
 *   bpacket_queue_remove_peer 会把该 peer 从队列各报文的 peers 链表中摘掉。 */
static void update_subgroup_remove_peer_internal(struct update_subgroup *subgrp,
						 struct peer_af *paf)
{
	assert(subgrp && paf && subgrp->update_group);

	if (bgp_debug_peer_updout_enabled(paf->peer->host)) {
		UPDGRP_PEER_DBG_DIS(subgrp->update_group);
	}

	bpacket_queue_remove_peer(paf);
	LIST_REMOVE(paf, subgrp_train);
	paf->subgroup = NULL;
	subgrp->peer_count--;

	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("peer %s deleted from subgroup s%"
			   PRIu64 " peer cnt %d",
			   paf->peer->host, subgrp->id, subgrp->peer_count);
	frrtrace(7, frr_bgp, ug_subgroup_add_remove_peer, 2, paf->peer->host, paf->afi, paf->safi,
		 paf->afid, subgrp->id, subgrp->peer_count);
	SUBGRP_INCR_STAT(subgrp, prune_events);
}

/*
 * update_subgroup_remove_peer
 */
/* 【update_subgroup_remove_peer】对外接口：从子组移除 peer，随后做删空检查与合并检查。
 * 参数 subgrp：源子组；paf：待移除 peer。
 * 返回：无。
 * 调用时机：peer 会话 down / 配置变更导致 peer 离开子组时。
 * 关键点：移除 peer 可能使队列中某些报文失去全部接收者而被压缩删除，因此随后尝试
 *   update_subgroup_check_merge——条件可能已满足，可把本子组并回别的子组。 */
void update_subgroup_remove_peer(struct update_subgroup *subgrp,
				 struct peer_af *paf)
{
	if (!subgrp || !paf)
		return;

	update_subgroup_remove_peer_internal(subgrp, paf);

	if (update_subgroup_check_delete(subgrp))
		return;

	/*
	 * The deletion of the peer may have caused some packets to be
	 * deleted from the subgroup packet queue. Check if the subgroup can
	 * be merged now.
	 */
	update_subgroup_check_merge(subgrp, "removed peer from subgroup");
}

/* ========== 5. 子组查找 / 分裂 / 合并（split & merge 状态机） ========== */
/* 【update_subgroup_find】在组内找一个“可直接接收该新 peer”的空闲子组（版本已追平的）。
 * 参数 updgrp：父组；paf：待入组 peer（其 subgroup 必须为空）。
 * 返回：可用的子组，或 NULL（需新建）。
 * 调用时机：update_group_adjust_peer 为非立即通告的 peer 找容身之所时。
 * 关键点：候选子组需满足 version==0（因为新 peer 从版本 0 起步）、未处 default-originate
 *   状态、且不 needs-refresh（needs-refresh 的子组版本号无意义，不能直接并入）。
 *   注：paf->subgroup 非空时 assert(0)——本函数约定只处理“新入场”的 peer。 */
static struct update_subgroup *update_subgroup_find(struct update_group *updgrp,
						    struct peer_af *paf)
{
	struct update_subgroup *subgrp = NULL;
	uint64_t version;

	if (paf->subgroup) {
		assert(0);
		return NULL;
	} else
		version = 0;

	if (!peer_established(PAF_PEER(paf)->connection))
		return NULL;

	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		if (subgrp->version != version
		    || CHECK_FLAG(subgrp->sflags,
				  SUBGRP_STATUS_DEFAULT_ORIGINATE))
			continue;

		/*
		 * The version number is not meaningful on a subgroup that needs
		 * a refresh.
		 */
		if (update_subgroup_needs_refresh(subgrp))
			continue;

		break;
	}

	return subgrp;
}

/*
 * update_subgroup_ready_for_merge
 *
 * Returns true if this subgroup is in a state that allows it to be
 * merged into another subgroup.
 */
/* 【update_subgroup_ready_for_merge】判断子组当前是否“已完全静止”，可以参与合并。
 * 参数 subgrp：待判子组。
 * 返回：true=可合并；false=暂不可。
 * 调用时机：check_merge / can_merge_into / trigger_merge_check 前置判断。
 * 关键点：三个“必须为空/不脏”条件——bpacket 队列已排空(无待发编码报文)、adv FIFO
 *   已排空(无待编码通告)、未置 needs-refresh（否则无法比对 adj_out 是否一致）。 */
static bool update_subgroup_ready_for_merge(struct update_subgroup *subgrp)
{

	/*
	 * Not ready if there are any encoded packets waiting to be written
	 * out to peers.
	 */
	if (!bpacket_queue_is_empty(SUBGRP_PKTQ(subgrp)))
		return false;

	/*
	 * Not ready if there enqueued updates waiting to be encoded.
	 */
	if (!advertise_list_is_empty(subgrp))
		return false;

	/*
	 * Don't attempt to merge a subgroup that needs a refresh. For one,
	 * we can't determine if the adj_out of such a group matches that of
	 * another group.
	 */
	if (update_subgroup_needs_refresh(subgrp))
		return false;

	return true;
}

/*
 * update_subgrp_can_merge_into
 *
 * Returns true if the first subgroup can merge into the second
 * subgroup.
 */
/* 【update_subgroup_can_merge_into】判断 subgrp 能否并入 target。
 * 参数 subgrp：源子组；target：目标子组。
 * 返回：非 0=可并入，0=不可。
 * 调用时机：update_subgroup_check_merge 挑选目标时。
 * 关键点：合并前提是两者“状态一致”——version 相同(追到同一 BRIB 点)、default-originate
 *   状态相同、adj_count 相同(通告前缀集合规模一致)，且 target 本身处于可合并静止态。 */
static int update_subgroup_can_merge_into(struct update_subgroup *subgrp,
					  struct update_subgroup *target)
{

	if (subgrp == target)
		return 0;

	/*
	 * Both must have processed the BRIB to the same point in order to
	 * be merged.
	 */
	if (subgrp->version != target->version)
		return 0;

	if (CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_DEFAULT_ORIGINATE)
	    != CHECK_FLAG(target->sflags, SUBGRP_STATUS_DEFAULT_ORIGINATE))
		return 0;

	if (subgrp->adj_count != target->adj_count)
		return 0;

	return update_subgroup_ready_for_merge(target);
}

/*
 * update_subgroup_merge
 *
 * Merge the first subgroup into the second one.
 */
/* 【update_subgroup_merge】把 subgrp 的全部 peer 迁到 target，然后删除空掉的 subgrp。
 * 参数 subgrp：源子组；target：目标子组；reason：触发原因（仅用于日志/trace）。
 * 返回：无。
 * 调用时机：update_subgroup_check_merge 选定 target 后。
 * 关键点：
 *   - 先 assert 两者 adj_count 相等（前提由 can_merge_into 保证）。
 *   - 逐个 remove_peer_internal + add_peer(target, paf, 0)：传 0 表示新成员“跳过当前
 *     target 队列里的旧报文”，避免对 peer 造成"先撤销再广告"的抖动。
 *   - 迁完后 check_delete(subgrp) 必定成功（组已空），assert 结果。 */
static void update_subgroup_merge(struct update_subgroup *subgrp,
				  struct update_subgroup *target,
				  const char *reason)
{
	struct peer_af *paf;
	int result;
	int peer_count;

	assert(subgrp->adj_count == target->adj_count);

	peer_count = subgrp->peer_count;

	while (1) {
		paf = LIST_FIRST(&subgrp->peers);
		if (!paf)
			break;

		update_subgroup_remove_peer_internal(subgrp, paf);

		/*
		 * Add the peer to the target subgroup, while making sure that
		 * any currently enqueued packets won't be sent to it. Enqueued
		 * packets could, for example, result in an unnecessary withdraw
		 * followed by an advertise.
		 */
		update_subgroup_add_peer(target, paf, 0);
	}

	SUBGRP_INCR_STAT(target, merge_events);

	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("u%" PRIu64 ":s%" PRIu64 " (%d peers) merged into u%" PRIu64
			   ":s%" PRIu64 ", trigger: %s",
			   subgrp->update_group ? subgrp->update_group->id : 0, subgrp->id,
			   peer_count, target->update_group ? target->update_group->id : 0,
			   target->id, reason ? reason : "unknown");
	frrtrace(6, frr_bgp, ug_subgroup_merge, subgrp->update_group ? subgrp->update_group->id : 0,
		 subgrp->id, peer_count, target->update_group ? target->update_group->id : 0,
		 target->id, reason ? reason : "unknown");

	result = update_subgroup_check_delete(subgrp);
	assert(result);
}

/*
 * update_subgroup_check_merge
 *
 * Merge this subgroup into another subgroup if possible.
 *
 * Returns true if the subgroup has been merged. The subgroup pointer
 * should not be accessed in this case.
 */
/* 【update_subgroup_check_merge】尝试把 subgrp 并入同组内合适的 target；是合并的总入口。
 * 参数 subgrp：源子组；reason：触发原因（日志用）。
 * 返回：true=已合并（此时 subgrp 已被删除，调用者不得再访问其指针！）；false=未合并。
 * 调用时机：remove_peer 后、packet 出队后(bpacket_queue_advance_peer)、announce 完表后等。
 * 【重要】返回 true 时 subgrp 已被 free，调用方必须视其为悬垂指针。 */
bool update_subgroup_check_merge(struct update_subgroup *subgrp,
				 const char *reason)
{
	struct update_subgroup *target;

	if (!update_subgroup_ready_for_merge(subgrp))
		return false;

	/*
	 * Look for a subgroup to merge into.
	 */
	UPDGRP_FOREACH_SUBGRP (subgrp->update_group, target) {
		if (update_subgroup_can_merge_into(subgrp, target))
			break;
	}

	if (!target)
		return false;

	update_subgroup_merge(subgrp, target, reason);
	return true;
}

/*
* update_subgroup_merge_check_thread_cb
*/
/* 【update_subgroup_merge_check_thread_cb】合并检查的定时器回调（0ms 定时器）。
 * 参数 event：其 arg 是 update_subgroup。
 * 返回：无。
 * 调用时机：update_subgroup_trigger_merge_check 排定的定时器到期。
 * 关键点：先清 t_merge_check，再在“干净上下文”里调 check_merge，避免在遍历中途
 *   直接调用导致的迭代器失效。 */
static void update_subgroup_merge_check_thread_cb(struct event *event)
{
	struct update_subgroup *subgrp;

	subgrp = EVENT_ARG(event);

	subgrp->t_merge_check = NULL;

	update_subgroup_check_merge(subgrp, "triggered merge check");
}

/*
 * update_subgroup_trigger_merge_check
 *
 * Triggers a call to update_subgroup_check_merge() on a clean context.
 *
 * @param force If true, the merge check will be triggered even if the
 *              subgroup doesn't currently look ready for a merge.
 *
 * Returns true if a merge check will be performed shortly.
 */
/* 【update_subgroup_trigger_merge_check】把一个合并检查“推迟到干净上下文”执行（避免在
 *   链/表遍历中途直接合并引起的迭代失效）。
 * 参数 subgrp：目标子组；force：true 则即使当前不 ready 也排队检查。
 * 返回：true=已排定检查；false=未排定。
 * 调用时机：subgroup_announce_table 收尾、packet 发送推进等。
 * 关键点：已有 t_merge_check 时直接返回（去重）；否则用 0ms 定时器把回调放到事件循环，
 *   让链/表结构稳定后再执行。 */
bool update_subgroup_trigger_merge_check(struct update_subgroup *subgrp,
					 int force)
{
	if (subgrp->t_merge_check)
		return true;

	if (!force && !update_subgroup_ready_for_merge(subgrp))
		return false;

	subgrp->t_merge_check = NULL;
	event_add_timer_msec(bm->master, update_subgroup_merge_check_thread_cb,
			     subgrp, 0, &subgrp->t_merge_check);

	SUBGRP_INCR_STAT(subgrp, merge_checks_triggered);

	return true;
}

/* ========== 6. 分裂后的状态拷贝 + 策略变更处理 ========== */
/*
 * update_subgroup_copy_adj_out
 *
 * Helper function that clones the adj out (state about advertised
 * routes) from one subgroup to another. It assumes that the adj out
 * of the target subgroup is empty.
 */
/* 【update_subgroup_copy_adj_out】把 source 子组的“已通告前缀快照”(adj_out)克隆一份到
 *   dest 子组（用于分裂时保留落后 peer 已通告状态）。
 * 参数 source：源子组；dest：目标（其 adj_out 必须为空）。
 * 返回：无。
 * 调用时机：update_subgroup_split_peer 分裂时。
 * 关键点：为每条 adj 在 dest 里新建条目，并 bgp_attr_intern 复用属性（引用计数），
 *   最后同步 scount（已发送前缀计数）。 */
static void update_subgroup_copy_adj_out(struct update_subgroup *source,
					 struct update_subgroup *dest)
{
	struct bgp_adj_out *aout, *aout_copy;

	SUBGRP_FOREACH_ADJ (source, aout) {
		/*
		 * Copy the adj out.
		 */
		aout_copy = bgp_adj_out_alloc(dest, aout->dest,
					      aout->addpath_tx_id);
		aout_copy->attr =
			aout->attr ? bgp_attr_intern(aout->attr) : NULL;
	}

	dest->scount = source->scount;
}

/*
 * update_subgroup_copy_packets
 *
 * Copy packets after and including the given packet to the subgroup
 *  'dest'.
 *
 * Returns the number of packets copied.
 */
/* 【update_subgroup_copy_packets】从给定报文（含）起，把后续所有已编码 bpacket 复制到
 *   dest 子组队列（分裂时把落后 peer 尚未发送的报文一并带走）。
 * 参数 dest：目标子组；pkt：起始报文（典型是 paf->next_pkt_to_send）。
 * 返回：复制的报文条数。
 * 调用时机：update_subgroup_split_peer 分裂时。
 * 关键点：用 stream_dup 深拷贝 buffer，并连同 arr（逐 peer 改写偏移表）一起 bpacket_queue_add；
 *   循环遇 pkt->buffer==NULL（哨兵）即止。 */
static int update_subgroup_copy_packets(struct update_subgroup *dest,
					struct bpacket *pkt)
{
	int count;

	count = 0;
	while (pkt && pkt->buffer) {
		bpacket_queue_add(SUBGRP_PKTQ(dest), stream_dup(pkt->buffer),
				  &pkt->arr);
		count++;
		pkt = bpacket_next(pkt);
	}

	return count;
}

/* 【updgrp_prefix_list_update】策略变更：若变更的 prefix-list 名等于本组出向 prefix-list，
 *   重新 lookup 并更新代表 peer 上缓存的指针。
 * 参数 updgrp：目标组；name：变更的 prefix-list 名。
 * 返回：true=确实更新了本组的出向 prefix-list。
 * 调用时机：updgrp_policy_update_walkcb 分派。 */
static bool updgrp_prefix_list_update(struct update_group *updgrp,
				      const char *name)
{
	struct peer *peer;
	struct bgp_filter *filter;

	peer = UPDGRP_PEER(updgrp);
	filter = &peer->filter[UPDGRP_AFI(updgrp)][UPDGRP_SAFI(updgrp)];

	if (PREFIX_LIST_OUT_NAME(filter)
	    && (strcmp(name, PREFIX_LIST_OUT_NAME(filter)) == 0)) {
		PREFIX_LIST_OUT(filter) = prefix_list_lookup(
			UPDGRP_AFI(updgrp), PREFIX_LIST_OUT_NAME(filter));
		return true;
	}
	return false;
}

/* 【updgrp_filter_list_update】策略变更：若变更的 filter-list(AS-path) 名等于本组出向
 *   filter-list，则重新 lookup 更新指针。参数/返回/时机同 updgrp_prefix_list_update。 */
static bool updgrp_filter_list_update(struct update_group *updgrp,
				      const char *name)
{
	struct peer *peer;
	struct bgp_filter *filter;

	peer = UPDGRP_PEER(updgrp);
	filter = &peer->filter[UPDGRP_AFI(updgrp)][UPDGRP_SAFI(updgrp)];

	if (FILTER_LIST_OUT_NAME(filter)
	    && (strcmp(name, FILTER_LIST_OUT_NAME(filter)) == 0)) {
		FILTER_LIST_OUT(filter) =
			as_list_lookup(FILTER_LIST_OUT_NAME(filter));
		return true;
	}
	return false;
}

/* 【updgrp_distribute_list_update】策略变更：若变更的 distribute-list 名等于本组出向
 *   distribute-list，则重新 lookup 更新指针。参数/返回/时机同前。 */
static bool updgrp_distribute_list_update(struct update_group *updgrp,
					  const char *name)
{
	struct peer *peer;
	struct bgp_filter *filter;

	peer = UPDGRP_PEER(updgrp);
	filter = &peer->filter[UPDGRP_AFI(updgrp)][UPDGRP_SAFI(updgrp)];

	if (DISTRIBUTE_OUT_NAME(filter)
	    && (strcmp(name, DISTRIBUTE_OUT_NAME(filter)) == 0)) {
		DISTRIBUTE_OUT(filter) = access_list_lookup(
			UPDGRP_AFI(updgrp), DISTRIBUTE_OUT_NAME(filter));
		return true;
	}
	return false;
}

/* 【updgrp_route_map_update】策略变更：处理出向 route-map / unsuppress-map /
 *   default-originate route-map 的更新（三者都可能是 route-map）。
 * 参数 updgrp：目标组；name：变更的 route-map 名；def_rmap_changed：出参，若改的是
 *   default-originate 用的 route-map 则置 1（调用方据此触发 default 重通告）。
 * 返回：本组出向 route-map 或 unsuppress-map 被更新则返回 1，否则 0。
 * 调用时机：updgrp_policy_update_walkcb 分派。
 * 关键点：仅更新“指针”（route_map_lookup_by_name），内容变化由随后的重新通告体现。 */
static int updgrp_route_map_update(struct update_group *updgrp,
				   const char *name, int *def_rmap_changed)
{
	struct peer *peer;
	struct bgp_filter *filter;
	int changed = 0;
	afi_t afi;
	safi_t safi;

	peer = UPDGRP_PEER(updgrp);
	afi = UPDGRP_AFI(updgrp);
	safi = UPDGRP_SAFI(updgrp);
	filter = &peer->filter[afi][safi];

	if (ROUTE_MAP_OUT_NAME(filter)
	    && (strcmp(name, ROUTE_MAP_OUT_NAME(filter)) == 0)) {
		ROUTE_MAP_OUT(filter) = route_map_lookup_by_name(name);

		changed = 1;
	}

	if (UNSUPPRESS_MAP_NAME(filter)
	    && (strcmp(name, UNSUPPRESS_MAP_NAME(filter)) == 0)) {
		UNSUPPRESS_MAP(filter) = route_map_lookup_by_name(name);
		changed = 1;
	}

	/* process default-originate route-map */
	if (peer->default_rmap[afi][safi].name
	    && (strcmp(name, peer->default_rmap[afi][safi].name) == 0)) {
		peer->default_rmap[afi][safi].map =
			route_map_lookup_by_name(name);
		if (def_rmap_changed)
			*def_rmap_changed = 1;
	}
	return changed;
}

/*
 * hash iteration callback function to process a policy change for an
 * update group. Check if the changed policy matches the updgrp's
 * outbound route-map or unsuppress-map or default-originate map or
 * filter-list or prefix-list or distribute-list.
 * Trigger update generation accordingly.
 */
/* 【updgrp_policy_update_walkcb】策略变更处理的核心回调：对每个 update_group 检查变更的
 *   策略是否属于它的出向策略，若是则更新指针并触发重新通告。
 * 参数 updgrp：当前组；arg：updwalk_context（含 policy_type/name/route_update/start_event）。
 * 返回：UPDWALK_CONTINUE。
 * 调用时机：update_group_policy_update → update_group_walk 遍历全部组时。
 * 关键点（状态机）：
 *   - policy_event_start_flag（交互式改策略的“开始”事件）：只把所有子组置 needs-refresh，
 *     阻止中途与别的子组过早合并；等真正改完（结束事件）才重通告。
 *   - def_changed：default-originate 用的 route-map 变了——若该 map 还在库里则是“更新”
 *     流程(清 DEFAULT_ORIGINATE 标志后重发)，否则是“显式撤销”流程(subgroup_default_originate
 *     传 true)。 */
static int updgrp_policy_update_walkcb(struct update_group *updgrp, void *arg)
{
	struct updwalk_context *ctx = arg;
	struct update_subgroup *subgrp;
	int changed = 0;
	int def_changed = 0;

	if (!updgrp || !ctx || !ctx->policy_name)
		return UPDWALK_CONTINUE;

	switch (ctx->policy_type) {
	case BGP_POLICY_ROUTE_MAP:
		changed = updgrp_route_map_update(updgrp, ctx->policy_name,
						  &def_changed);
		break;
	case BGP_POLICY_FILTER_LIST:
		changed = updgrp_filter_list_update(updgrp, ctx->policy_name);
		break;
	case BGP_POLICY_PREFIX_LIST:
		changed = updgrp_prefix_list_update(updgrp, ctx->policy_name);
		break;
	case BGP_POLICY_DISTRIBUTE_LIST:
		changed =
			updgrp_distribute_list_update(updgrp, ctx->policy_name);
		break;
	default:
		break;
	}

	/* If not doing route update, return after updating "config" */
	if (!ctx->policy_route_update)
		return UPDWALK_CONTINUE;

	/* If nothing has changed, return after updating "config" */
	if (!changed && !def_changed)
		return UPDWALK_CONTINUE;

	/*
	 * If something has changed, at the beginning of a route-map
	 * modification
	 * event, mark each subgroup's needs-refresh bit. For one, it signals to
	 * whoever that the subgroup needs a refresh. Second, it prevents
	 * premature
	 * merge of this subgroup with another before a complete (outbound)
	 * refresh.
	 */
	if (ctx->policy_event_start_flag) {
		UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
			update_subgroup_set_needs_refresh(subgrp, 1);
		}
		return UPDWALK_CONTINUE;
	}

	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		/* Avoid suppressing duplicate routes later
		 * when processing in subgroup_announce_table().
		 */
		SET_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES);

		if (changed) {
			if (bgp_debug_update(NULL, NULL, updgrp, 0))
				zlog_debug(
					"u%" PRIu64 ":s%" PRIu64" announcing routes upon policy %s (type %d) change",
					updgrp->id, subgrp->id,
					ctx->policy_name, ctx->policy_type);

			frrtrace(5, frr_bgp, upd_announce_route_on_policy_change, 0, updgrp->id,
				 subgrp->id, ctx->policy_name, ctx->policy_type);

			subgroup_announce_route(subgrp);
		}
		if (def_changed) {
			if (bgp_debug_update(NULL, NULL, updgrp, 0))
				zlog_debug(
					"u%" PRIu64 ":s%" PRIu64" announcing default upon default routemap %s change",
					updgrp->id, subgrp->id,
					ctx->policy_name);

			frrtrace(5, frr_bgp, upd_announce_route_on_policy_change, 1, updgrp->id,
				 subgrp->id, ctx->policy_name, ctx->policy_type);

			if (route_map_lookup_by_name(ctx->policy_name)) {
				/*
				 * When there is change in routemap, this flow
				 * is triggered. the routemap is still present
				 * in lib, hence its a update flow. The flag
				 * needs to be unset.
				 */
				UNSET_FLAG(subgrp->sflags,
					   SUBGRP_STATUS_DEFAULT_ORIGINATE);
				subgroup_default_originate(subgrp, false);
			} else {
				/*
				 * This is a explicit withdraw, since the
				 * routemap is not present in routemap lib. need
				 * to pass `true` for withdraw arg.
				 */
				subgroup_default_originate(subgrp, true);
			}
		}
		update_subgroup_set_needs_refresh(subgrp, 0);
	}
	return UPDWALK_CONTINUE;
}

/* 【update_group_walkcb】哈希表遍历的“桶级”回调：把 bucket 里的 update_group 取出，
 *   转调上层传入的 updgrp_walkcb 业务回调。
 * 参数 bucket：哈希桶；arg：updwalk_context（其 cb/context 为真正业务回调与参数）。
 * 返回：业务回调的返回值（UPDWALK_CONTINUE / UPDWALK_ABORT）。
 * 调用时机：hash_walk 遍历某 (afi,safi) 的 update_groups 时。 */
static int update_group_walkcb(struct hash_bucket *bucket, void *arg)
{
	struct update_group *updgrp = bucket->data;
	struct updwalk_context *wctx = arg;
	int ret = (*wctx->cb)(updgrp, wctx->context);
	return ret;
}

/********************
 * PUBLIC FUNCTIONS
 ********************/
/* ========== 7. 对外接口：策略变更触发、分裂、初始化/销毁 ========== */

/*
 * trigger function when a policy (route-map/filter-list/prefix-list/
 * distribute-list etc.) content changes. Go through all the
 * update groups and process the change.
 *
 * bgp: the bgp instance
 * ptype: the type of policy that got modified, see bgpd.h
 * pname: name of the policy
 * route_update: flag to control if an automatic update generation should
 *                occur
 * start_event: flag that indicates if it's the beginning of the change.
 *             Esp. when the user is changing the content interactively
 *             over multiple statements. Useful to set dirty flag on
 *             update groups.
 */
/* 【update_group_policy_update】策略(routemap/filter-list/prefix-list/distribute-list)内容
 *   变化时的对外入口：把变更信息打包进 updwalk_context，遍历所有 update_group 处理。
 * 参数 bgp：实例；ptype：策略类型(见 bgpd.h)；pname：策略名；route_update：是否要自动
 *     触发重通告；start_event：是否为交互式修改的“开始”事件（打 dirty/needs-refresh）。
 * 返回：无。
 * 调用时机：lib 层 route-map/plist 等模块在策略变更时回调 bgpd。 */
void update_group_policy_update(struct bgp *bgp, enum bgp_policy_type ptype,
				const char *pname, bool route_update,
				int start_event)
{
	struct updwalk_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.policy_type = ptype;
	ctx.policy_name = pname;
	ctx.policy_route_update = route_update;
	ctx.policy_event_start_flag = start_event;
	ctx.flags = 0;

	update_group_walk(bgp, updgrp_policy_update_walkcb, &ctx);
}

/*
 * update_subgroup_split_peer
 *
 * Ensure that the given peer is in a subgroup of its own in the
 * specified update group.
 */
/* 【update_subgroup_split_peer】确保 peer 在指定组内“独占一个子组”（分裂/换组）。
 * 参数 paf：待分裂 peer；updgrp：目标组（NULL 表示沿用当前组）。
 * 返回：无。
 * 调用时机：update_group_adjust_peer（换组/立即通告）、peer_af_announce_route（单播刷新）。
 * 关键点（两条路径）：
 *   - 若 peer 原本独占子组(peer_count==1)：直接把整个子组搬到目标组（省去拷贝）；
 *     目标组未变则直接返回；搬完置 needs-refresh（子组状态虽自洽，但未必与新组内其他
 *     子组一致，合并前必须先完全刷新）。
 *   - 否则：新建子组、继承 sflags、记录 split_from、拷贝 adj_out 与“从 next_pkt_to_send
 *     起的未发报文”，置 needs-refresh，再从旧组移除 peer 并加入新组(send_enqueued=1)。
 *   注意“立即通告”语义：分离后该 peer 不与别人共享，其 coalesce 由调用方置 0。
 */
void update_subgroup_split_peer(struct peer_af *paf,
				struct update_group *updgrp)
{
	struct update_subgroup *old_subgrp, *subgrp;
	uint64_t old_id;


	old_subgrp = paf->subgroup;

	if (!updgrp)
		updgrp = old_subgrp->update_group;

	/*
	 * If the peer is alone in its subgroup, reuse the existing
	 * subgroup.
	 */
	if (old_subgrp->peer_count == 1) {
		if (updgrp == old_subgrp->update_group)
			return;

		subgrp = old_subgrp;
		old_id = old_subgrp->update_group->id;

		if (bgp_debug_peer_updout_enabled(paf->peer->host)) {
			UPDGRP_PEER_DBG_DIS(old_subgrp->update_group);
		}

		update_group_remove_subgroup(old_subgrp->update_group,
					     old_subgrp);
		update_group_add_subgroup(updgrp, subgrp);

		if (bgp_debug_peer_updout_enabled(paf->peer->host)) {
			UPDGRP_PEER_DBG_EN(updgrp);
		}
		if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
			zlog_debug("u%" PRIu64 ":s%" PRIu64" peer %s moved to u%" PRIu64 ":s%" PRIu64,
				   old_id, subgrp->id, paf->peer->host,
				   updgrp->id, subgrp->id);
		frrtrace(6, frr_bgp, ug_subgroup_split_peer, old_id, subgrp->id,
			 old_subgrp->peer_count, paf->peer->host, updgrp->id, subgrp->id);

		/*
		 * The state of the subgroup (adj_out, advs, packet queue etc)
		 * is consistent internally, but may not be identical to other
		 * subgroups in the new update group even if the version number
		 * matches up. Make sure a full refresh is done before the
		 * subgroup is merged with another.
		 */
		update_subgroup_set_needs_refresh(subgrp, 1);

		SUBGRP_INCR_STAT(subgrp, updgrp_switch_events);
		return;
	}

	/*
	 * Create a new subgroup under the specified update group, and copy
	 * over relevant state to it.
	 */
	subgrp = update_subgroup_create(updgrp);
	update_subgroup_inherit_info(subgrp, old_subgrp);

	subgrp->split_from.update_group_id = old_subgrp->update_group->id;
	subgrp->split_from.subgroup_id = old_subgrp->id;

	/*
	 * Copy out relevant state from the old subgroup.
	 */
	update_subgroup_copy_adj_out(paf->subgroup, subgrp);
	update_subgroup_copy_packets(subgrp, paf->next_pkt_to_send);

	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("u%" PRIu64 ":s%" PRIu64" peer %s split and moved into u%" PRIu64":s%" PRIu64,
			   paf->subgroup->update_group->id, paf->subgroup->id,
			   paf->peer->host, updgrp->id, subgrp->id);
	frrtrace(6, frr_bgp, ug_subgroup_split_peer, paf->subgroup->update_group->id,
		 paf->subgroup->id, old_subgrp->peer_count, paf->peer->host, updgrp->id,
		 subgrp->id);

	SUBGRP_INCR_STAT(paf->subgroup, split_events);

	/*
	 * Since queued advs were left behind, this new subgroup needs a
	 * refresh.
	 */
	update_subgroup_set_needs_refresh(subgrp, 1);

	/*
	 * Remove peer from old subgroup, and add it to the new one.
	 */
	update_subgroup_remove_peer(paf->subgroup, paf);

	update_subgroup_add_peer(subgrp, paf, 1);
}

/* 【update_bgp_group_init】为 BGP 实例的每个 (afi,safi) 创建 update_group 哈希表。
 * 参数 bgp：实例。
 * 返回：无。
 * 调用时机：BGP 实例初始化时（bgp_create）。
 * 关键点：哈希函数用 updgrp_hash_key_make / updgrp_hash_cmp；AF_FOREACH 覆盖所有 afid。 */
void update_bgp_group_init(struct bgp *bgp)
{
	int afid;

	AF_FOREACH (afid)
		bgp->update_groups[afid] =
			hash_create(updgrp_hash_key_make, updgrp_hash_cmp,
				    "BGP Update Group Hash");
}

/* 【update_bgp_group_free】释放实例的全部 update_group 哈希表。
 * 参数 bgp：实例。
 * 返回：无。
 * 调用时机：BGP 实例销毁时。
 * 关键点：hash_free 只释放哈希表结构本身（此处假定组与子组已随 peer 拆除而清空）。 */
void update_bgp_group_free(struct bgp *bgp)
{
	int afid;

	AF_FOREACH (afid) {
		if (bgp->update_groups[afid]) {
			hash_free(bgp->update_groups[afid]);
			bgp->update_groups[afid] = NULL;
		}
	}
}

/* 【update_group_show】`show bgp update-group` 的入口：遍历指定 (afi,safi) 的所有组输出；
 *   JSON 模式下还把整个结果挂到 VRF 名下输出。
 * 参数 bgp：实例；afi/safi：地址族；vty：输出；subgrp_id：只显示某子组(0=全部)；uj：JSON。 */
void update_group_show(struct bgp *bgp, afi_t afi, safi_t safi, struct vty *vty,
		       uint64_t subgrp_id, bool uj)
{
	struct updwalk_context ctx;
	json_object *json_vrf_obj = NULL;

	memset(&ctx, 0, sizeof(ctx));
	ctx.vty = vty;
	ctx.subgrp_id = subgrp_id;
	ctx.uj = uj;

	if (uj) {
		ctx.json_updategrps = json_object_new_object();
		json_vrf_obj = json_object_new_object();
	}

	update_group_af_walk(bgp, afi, safi, update_group_show_walkcb, &ctx);

	if (uj) {
		const char *vname;

		if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT)
			vname = VRF_DEFAULT_NAME;
		else
			vname = bgp->name;
		json_object_object_add(json_vrf_obj, vname,
				       ctx.json_updategrps);
		vty_json(vty, json_vrf_obj);
	}
}

/*
 * update_group_show_stats
 *
 * Show global statistics about update groups.
 */
/* 【update_group_show_stats】打印实例级 update-group 全局统计（建/删组与子组数、
 *   join/prune/merge/split/switch 事件、合并的 peer-refresh 次数、merge-check 次数）。
 * 参数 bgp：实例；vty：输出。
 * 返回：无。调用时机：`show bgp ... update-group` 统计段。 */
void update_group_show_stats(struct bgp *bgp, struct vty *vty)
{
	vty_out(vty, "Update groups created: %u\n",
		bgp->update_group_stats.updgrps_created);
	vty_out(vty, "Update groups deleted: %u\n",
		bgp->update_group_stats.updgrps_deleted);
	vty_out(vty, "Update subgroups created: %u\n",
		bgp->update_group_stats.subgrps_created);
	vty_out(vty, "Update subgroups deleted: %u\n",
		bgp->update_group_stats.subgrps_deleted);
	vty_out(vty, "Join events: %u\n", bgp->update_group_stats.join_events);
	vty_out(vty, "Prune events: %u\n",
		bgp->update_group_stats.prune_events);
	vty_out(vty, "Merge events: %u\n",
		bgp->update_group_stats.merge_events);
	vty_out(vty, "Split events: %u\n",
		bgp->update_group_stats.split_events);
	vty_out(vty, "Update group switch events: %u\n",
		bgp->update_group_stats.updgrp_switch_events);
	vty_out(vty, "Peer route refreshes combined: %u\n",
		bgp->update_group_stats.peer_refreshes_combined);
	vty_out(vty, "Merge checks triggered: %u\n",
		bgp->update_group_stats.merge_checks_triggered);
}

/*
 * update_group_adjust_peer
 */
/* 【update_group_adjust_peer】把 peer_af 放入“正确的 update_group/subgroup”——本文件的
 *   核心入口，负责新 peer 入组、已入组 peer 的原地保留或换组。
 * 参数 paf：待调整 peer_af。
 * 返回：无。
 * 调用时机：邻居建立/能力协商完成、配置变更(update_group_adjust_peer_afs)、lonesoul
 *   变化等任何可能改变分组判据的时刻。
 * 关键点（决策树）：
 *   - 前置门槛：peer 已 Established、是配置节点、该 AF 已协商成功(afc_nego)，否则直接返回。
 *   - 用 update_group_find 找目标组，找不到则 update_group_create。
 *   - old_subgrp 存在（peer 已在某子组）时：
 *       · peer_immediate_announce（GR Helper 等需立即通告）→ split 出独立组并置 coalesce=0。
 *       · 目标组未变 → 原地不动。
 *       · 目标组变了 → split 到新组。
 *   - old_subgrp 不存在（新入场）时：立即通告则新建独立组(coalesce=0)，否则找一个空闲
 *     子组(update_subgroup_find)或新建，最后 add_peer(send_enqueued=1)。
 *   【对照 RFC】立即通告的判据见 bgp_updgrp.h peer_immediate_announce()（GR/NSF Helper）。 */
void update_group_adjust_peer(struct peer_af *paf)
{
	struct update_group *updgrp;
	struct update_subgroup *subgrp, *old_subgrp;
	struct peer *peer;

	if (!paf)
		return;

	peer = PAF_PEER(paf);
	if (!peer_established(peer->connection)) {
		return;
	}

	if (!peer_is_config_node(peer)) {
		return;
	}

	if (!peer->afc_nego[paf->afi][paf->safi]) {
		return;
	}

	updgrp = update_group_find(paf);
	if (!updgrp)
		updgrp = update_group_create(paf);

	old_subgrp = paf->subgroup;

	if (old_subgrp) {
		/*
		 * If we need to announce immediately, put peer in its
		 * own group and set its coalesce timer to 0.
		 */
		if (peer_immediate_announce(peer, paf->afi, paf->safi)) {
			update_subgroup_split_peer(paf, updgrp);
			subgrp = paf->subgroup;
			event_cancel(&subgrp->t_coalesce);
			subgrp->v_coalesce = 0;
			return;
		}

		/*
		 * If the update group of the peer is unchanged, the peer can
		 * stay
		 * in its existing subgroup and we're done.
		 */
		if (old_subgrp->update_group == updgrp)
			return;

		/*
		 * The peer is switching between update groups. Put it in its
		 * own subgroup under the new update group.
		 */
		update_subgroup_split_peer(paf, updgrp);
		return;
	}

	/*
	 * If we need to announce immediately, put peer in its
	 * own group and set its coalesce timer to 0.
	 */
	if (peer_immediate_announce(peer, paf->afi, paf->safi)) {
		subgrp = update_subgroup_create(updgrp);
		event_cancel(&subgrp->t_coalesce);
		subgrp->v_coalesce = 0;
	} else {
		subgrp = update_subgroup_find(updgrp, paf);
		if (!subgrp)
			subgrp = update_subgroup_create(updgrp);
	}

	update_subgroup_add_peer(subgrp, paf, 1);
	if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
		zlog_debug("u%" PRIu64 ":s%" PRIu64 " add peer %s", updgrp->id,
			   subgrp->id, paf->peer->host);

	return;
}

/* 【update_group_adjust_soloness】对外接口：设置/清除某 peer（或其所属 peer-group 全体成员）
 *   的 lonesoul 状态，并触发路由重通告。
 * 参数 peer：目标 peer；set：1=设 lonesoul，0=清除。
 * 返回：恒为 0。
 * 调用时机：用户的 `neighbor ... solo` 之类的配置命令。
 * 关键点：若 peer 是 peer-group 本身，则对其全部成员逐个处理（peer_flag_set/unset +
 *   peer_lonesoul_or_not + 重通告）。 */
int update_group_adjust_soloness(struct peer *peer, int set)
{
	struct peer_group *group;
	struct listnode *node, *nnode;

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_lonesoul_or_not(peer, set);
		if (peer_established(peer->connection))
			bgp_announce_route_all(peer);
	} else {
		if (set)
			peer_flag_set(peer, PEER_FLAG_LONESOUL);
		else
			peer_flag_unset(peer, PEER_FLAG_LONESOUL);

		group = peer->group;
		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
			peer_lonesoul_or_not(peer, set);
			if (peer_established(peer->connection))
				bgp_announce_route_all(peer);
		}
	}
	return 0;
}

/*
 * update_subgroup_rib
 */
/* 【update_subgroup_rib】返回该子组所在 (afi,safi) 的 BGP RIB 表。
 * 参数 subgrp：子组。
 * 返回：bgp->rib[afi][safi]，实例为空则 NULL。
 * 调用时机：subgroup_announce_route 遍历 VPN/ENCAP/EVPN 的多级表时取顶层 RIB。 */
struct bgp_table *update_subgroup_rib(struct update_subgroup *subgrp)
{
	struct bgp *bgp;

	bgp = SUBGRP_INST(subgrp);
	if (!bgp)
		return NULL;

	return bgp->rib[SUBGRP_AFI(subgrp)][SUBGRP_SAFI(subgrp)];
}

/* 【update_group_af_walk】遍历某 (afi,safi) 的所有 update_group，逐个调用 cb。
 * 参数 bgp：实例；afi/safi：地址族；cb：业务回调；ctx：回调参数。
 * 返回：无。
 * 调用时机：所有 show 命令与策略变更。
 * 关键点：先 afindex 把 (afi,safi) 转成 afid，越界(BGP_AF_MAX)则直接返回；
 *   update_group_walkcb 是薄封装，把 hash 桶拆成 update_group 再转调 cb。 */
void update_group_af_walk(struct bgp *bgp, afi_t afi, safi_t safi,
			  updgrp_walkcb cb, void *ctx)
{
	struct updwalk_context wctx;
	int afid;

	if (!bgp)
		return;
	afid = afindex(afi, safi);
	if (afid >= BGP_AF_MAX)
		return;

	memset(&wctx, 0, sizeof(wctx));
	wctx.cb = cb;
	wctx.context = ctx;

	if (bgp->update_groups[afid])
		hash_walk(bgp->update_groups[afid], update_group_walkcb, &wctx);
}

/* 【update_group_walk】对实例的所有 (afi,safi) 逐族调用 update_group_af_walk。 */
void update_group_walk(struct bgp *bgp, updgrp_walkcb cb, void *ctx)
{
	afi_t afi;
	safi_t safi;

	FOREACH_AFI_SAFI (afi, safi) {
		update_group_af_walk(bgp, afi, safi, cb, ctx);
	}
}

/* 【update_group_default_originate_route_map_walkcb】`default-originate route-map` 变更后
 *   的重通告回调：对每个配了 default route-map 的子组，清 DEFAULT_ORIGINATE 标志并重新
 *   生成默认路由通告。
 * 参数 updgrp：当前组；arg：未用。
 * 返回：UPDWALK_CONTINUE。
 * 调用时机：update_group_refresh_default_originate_route_map 遍历时。 */
static int
update_group_default_originate_route_map_walkcb(struct update_group *updgrp,
						void *arg)
{
	struct update_subgroup *subgrp;
	struct peer *peer;
	afi_t afi;
	safi_t safi;

	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		peer = SUBGRP_PEER(subgrp);
		afi = SUBGRP_AFI(subgrp);
		safi = SUBGRP_SAFI(subgrp);

		if (peer->default_rmap[afi][safi].name) {
			/*
			 * When there is change in routemap this flow will
			 * be triggered. We need to unset the Flag to ensure
			 * the update flow gets triggered.
			 */
			UNSET_FLAG(subgrp->sflags,
				   SUBGRP_STATUS_DEFAULT_ORIGINATE);
			subgroup_default_originate(subgrp, false);
		}
	}

	return UPDWALK_CONTINUE;
}

/* 【update_group_refresh_default_originate_route_map】定时器回调：遍历所有组刷新
 *   default-originate 路由。
 * 参数 event：其 arg 为 struct bgp。
 * 返回：无。
 * 调用时机：default-originate 的 route-map 内容变化后由事件循环触发（去抖）。
 * 关键点：跑完遍历后取消 t_rmap_def_originate_eval 事件，避免重复。 */
void update_group_refresh_default_originate_route_map(struct event *event)
{
	struct bgp *bgp;
	char reason[] = "refresh default-originate route-map";

	bgp = EVENT_ARG(event);
	update_group_walk(bgp, update_group_default_originate_route_map_walkcb,
			  reason);
	event_cancel(&bgp->t_rmap_def_originate_eval);
}

/* ========== 8. 立即通告 / 触发写 / 调试位 / Add-Path 判据 ========== */
/*
 * peer_af_announce_route
 *
 * Refreshes routes out to a peer_af immediately.
 *
 * If the combine parameter is true, then this function will try to
 * gather other peers in the subgroup for which a route announcement
 * is pending and efficiently announce routes to all of them.
 *
 * For now, the 'combine' option has an effect only if all peers in
 * the subgroup have a route announcement pending.
 */
/* 【peer_af_announce_route】立即向某 peer 刷新（重通告）路由。
 * 参数 paf：目标 peer；combine：是否尝试把同子组内也在等通告的 peer 合并为一次通告
 *     （省算力，但仅当子组内“所有其他 peer 都恰好有待通告”时才合并，见下）。
 * 返回：无。
 * 调用时机：路由策略/会话相关事件需要立即刷新出向时（如 route-refresh 响应、入向策略变更）。
 * 关键点：
 *   - combine 时先扫描子组：只要有一个 peer 没在等通告(其 t_announce_route 为空)，就不合并。
 *   - 不合并则 split 出单 peer 子组后 subgroup_announce_route 仅对自己通告。
 *   - 合并则先停掉其他 peer 的 announce-route 定时器，再对整个子组通告一次，并累加
 *     peer_refreshes_combined 统计。 */
void peer_af_announce_route(struct peer_af *paf, int combine)
{
	struct update_subgroup *subgrp;
	struct peer_af *cur_paf;
	int all_pending;

	subgrp = paf->subgroup;
	all_pending = 0;

	if (combine) {
		/*
		 * If there are other peers in the old subgroup that also need
		 * routes to be announced, pull them into the peer's new
		 * subgroup.
		 * Combine route announcement with other peers if possible.
		 *
		 * For now, we combine only if all peers in the subgroup have an
		 * announcement pending.
		 */
		all_pending = 1;

		SUBGRP_FOREACH_PEER (subgrp, cur_paf) {
			if (cur_paf == paf)
				continue;

			if (cur_paf->t_announce_route)
				continue;

			all_pending = 0;
			break;
		}
	}
	/*
	 * Announce to the peer alone if we were not asked to combine peers,
	 * or if some peers don't have a route announcement pending.
	 */
	if (!combine || !all_pending) {
		update_subgroup_split_peer(paf, NULL);
		subgrp = paf->subgroup;

		assert(subgrp && subgrp->update_group);
		if (bgp_debug_update(paf->peer, NULL, subgrp->update_group, 0))
			zlog_debug("u%" PRIu64 ":s%" PRIu64" %s announcing routes",
				   subgrp->update_group->id, subgrp->id,
				   paf->peer->host);
		frrtrace(3, frr_bgp, upd_peer_af_announce_route, subgrp->update_group->id,
			 subgrp->id, paf->peer->host);

		subgroup_announce_route(paf->subgroup);
		return;
	}

	/*
	 * We will announce routes the entire subgroup.
	 *
	 * First stop refresh timers on all the other peers.
	 */
	SUBGRP_FOREACH_PEER (subgrp, cur_paf) {
		if (cur_paf == paf)
			continue;

		bgp_stop_announce_route_timer(cur_paf);
	}

	if (bgp_debug_update(paf->peer, NULL, subgrp->update_group, 0))
		zlog_debug("u%" PRIu64 ":s%" PRIu64" announcing routes to %s, combined into %d peers",
			   subgrp->update_group->id, subgrp->id,
			   paf->peer->host, subgrp->peer_count);
	frrtrace(4, frr_bgp, upd_peer_af_announce_route_combined, subgrp->update_group->id,
		 subgrp->id, paf->peer->host, subgrp->peer_count);

	subgroup_announce_route(subgrp);

	SUBGRP_INCR_STAT_BY(subgrp, peer_refreshes_combined,
			    subgrp->peer_count - 1);
}

/* 【subgroup_trigger_write】为子组内每个已建立会话的 peer 排一个 0ms 的
 *   bgp_generate_updgrp_packets 任务，促使其把子组队列里的 bpacket 拉入各自 obuf 写出。
 * 参数 subgrp：子组。
 * 返回：无。
 * 调用时机：新报文入队后（subgroup_default_*_packet）、需要立即推送时。
 * 关键点：bgp_generate_updgrp_packets 定义在 bgp_packet.c，它会做“逐 peer reformat + 写出”； */
/*   这里不区分 peer，全子组统一触发。 */
void subgroup_trigger_write(struct update_subgroup *subgrp)
{
	struct peer_af *paf;

	/*
	 * For each peer in the subgroup, schedule a job to pull packets from
	 * the subgroup output queue into their own output queue. This action
	 * will trigger a write job on the I/O thread.
	 */
	SUBGRP_FOREACH_PEER (subgrp, paf) {
		struct peer_connection *connection = paf->peer->connection;

		if (peer_established(connection))
			event_add_timer_msec(bm->master,
					     bgp_generate_updgrp_packets,
					     connection, 0,
					     &connection->t_generate_updgrp_packets);
	}
}

/* 【update_group_clear_update_dbg】遍历回调：清零某组的 peer 调试计数(num_dbg_en_peers)。
 * 调用时机：清除 `debug bgp updates` 相关状态时。返回 UPDWALK_CONTINUE。 */
int update_group_clear_update_dbg(struct update_group *updgrp, void *arg)
{
	UPDGRP_PEER_DBG_OFF(updgrp);
	return UPDWALK_CONTINUE;
}

/* Return true if we should addpath encode NLRI to this peer */
/* 【bgp_addpath_encode_tx】判断是否应对该 peer 做 Add-Path 出向编码（RFC7911）。
 * 需同时“我方通告了 Add-Path 能力(TX_ADV)”且“对方也通告了(RX_RCV)”才算协商成立。 */
bool bgp_addpath_encode_tx(struct peer *peer, afi_t afi, safi_t safi)
{
	return (CHECK_FLAG(peer->af_cap[afi][safi], PEER_CAP_ADDPATH_AF_TX_ADV)
		&& CHECK_FLAG(peer->af_cap[afi][safi],
			      PEER_CAP_ADDPATH_AF_RX_RCV));
}

/* 【bgp_addpath_capable】判断某条路径 bpi 是否应作为 Add-Path 出向编码的对象。
 * 对 LABELED-UNICAST，沿用其对应 UNICAST 的 addpath 配置判定。
 * 依赖 peer->addpath_type[afi][safi]（SendType）与路径标志，见 bgp_addpath_tx_path()。 */
bool bgp_addpath_capable(struct bgp_path_info *bpi, struct peer *peer,
			 afi_t afi, safi_t safi)
{
	return (bgp_addpath_tx_path(peer->addpath_type[afi][safi], bpi) ||
		(safi == SAFI_LABELED_UNICAST &&
		 bgp_addpath_tx_path(peer->addpath_type[afi][SAFI_UNICAST],
				     bpi)));
}

/* 【bgp_check_selected】判断该路径是否应被通告给 peer：要么是最佳路径(BGP_PATH_SELECTED)，
 *   要么在启用了 addpath 时属于该 peer 需要额外通告的集合。 */
bool bgp_check_selected(struct bgp_path_info *bpi, struct peer *peer,
			bool addpath_capable, afi_t afi, safi_t safi)
{
	return (CHECK_FLAG(bpi->flags, BGP_PATH_SELECTED) ||
		(addpath_capable && bgp_addpath_capable(bpi, peer, afi, safi)));
}
