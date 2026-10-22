// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Addpath TX ID selection, and related utilities
 * Copyright (C) 2018  Amazon.com, Inc. or its affiliates
 */

/*
 * 文件导读：BGP Addpath 发送侧（TX）ID 选择与相关工具
 *
 * Addpath（Additional Paths，RFC 7911）允许同一前缀向同一邻居同时通告多条
 * 路径。本模块负责“发送方向”的路径 ID（Path Identifier）管理：给本机要发出
 * 的每条路径分配一个 4 字节的 addpath TX ID，并在路径选择变化时于路径之间
 * 复用这些 ID，避免为“换一条路径发送”而多做一次 withdraw 加 update。
 *
 * 三种发送策略（见 bgp_addpath.h 的 enum bgp_addpath_strat）：
 *   - addpath-tx-all-paths：所有路径都带 addpath 发送；
 *   - addpath-tx-bestpath-per-AS：每个 AS 的最佳路径发送，依赖 deterministic-med；
 *   - addpath-tx-best-selected：发送被选中的最佳 N 条路径。
 * 每种策略、每个 AFI / SAFI 组合各有一个独立的 ID 分配器（id_alloc），且每个
 * 前缀（bgp_dest）各有自己的 ID 池，避免不同策略之间互相干扰。
 *
 * 与相邻模块的配合：
 *   - 由 bgp_route.c / bgp_evpn.c 在路由处理与最佳路径选择后调用
 *     bgp_addpath_update_ids，把 ID 从不再需要的路径转移给需要的路径；
 *   - 由 bgpd 配置层调用 bgp_addpath_set_peer_type / bgp_addpath_type_changed
 *     在邻居或邻居组改变 addpath 策略时增删分配器；
 *   - label-unicast（SAFI_LABELED_UNICAST）的 ID 统一算在 unicast 里，因此
 *     各函数入口都会先把它归一化为 SAFI_UNICAST。
 *
 * 读的顺序：先看三种策略的名字表 strat_names 与 type_changed 的计数逻辑，
 * 再看 populate 与 flush 这组“填充 / 清理”函数，最后看 update_ids 的 ID 复用。
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bgp_addpath.h"
#include "bgp_evpn.h"
#include "bgp_route.h"

/*
 * 三种发送策略对应的名字表，数组下标即 enum bgp_addpath_strat 的取值。
 * config_name 用于配置命令与日志标识，human_name 与 human_description 用于
 * 展示，type_json_name 与 id_json_name 用于 JSON 输出。
 */
static const struct bgp_addpath_strategy_names strat_names[BGP_ADDPATH_MAX] = {
	{
		.config_name = "addpath-tx-all-paths",
		.human_name = "All",
		.human_description = "Advertise all paths via addpath",
		.type_json_name = "addpathTxAllPaths",
		.id_json_name = "addpathTxIdAll"
	},
	{
		.config_name = "addpath-tx-bestpath-per-AS",
		.human_name = "Best-Per-AS",
		.human_description = "Advertise bestpath per AS via addpath",
		.type_json_name = "addpathTxBestpathPerAS",
		.id_json_name = "addpathTxIdBestPerAS"
	},
	{
		.config_name = "addpath-tx-best-selected",
		.human_name = "Best-Selected",
		.human_description = "Advertise best N selected paths via addpath",
		.type_json_name = "addpathTxBestSelectedPaths",
		.id_json_name = "addpathTxIdBestSelected"
	},
};

/*
 * 越界或未知策略时返回的兜底名字表，保证 bgp_addpath_names 永不返回 NULL。
 */
static const struct bgp_addpath_strategy_names unknown_names = {
	.config_name = "addpath-tx-unknown",
	.human_name = "Unknown-Addpath-Strategy",
	.human_description = "Unknown Addpath Strategy",
	.type_json_name = "addpathTxUnknown",
	.id_json_name = "addpathTxIdUnknown"
};

/*
 * 返回某策略对应的名字（字符串）表。strat 合法（小于 BGP_ADDPATH_MAX）时
 * 返回 strat_names 中对应项，否则返回 unknown_names，因此永不返回 NULL。
 */
/*
 * Returns a structure full of strings associated with an addpath type. Will
 * never return null.
 */
const struct bgp_addpath_strategy_names *
bgp_addpath_names(enum bgp_addpath_strat strat)
{
	if (strat < BGP_ADDPATH_MAX)
		return &(strat_names[strat]);
	else
		return &unknown_names;
};

/*
 * 判断某个 AFI / SAFI 下是否至少有一个邻居在发送 addpath（是否有任何邻居
 * 在用）。实现上只看该 AFI / SAFI 的邻居总数 total_peercount 是否大于 0。
 */
/*
 * Returns if any peer is transmitting addpaths for a given afi/safi.
 */
bool bgp_addpath_is_addpath_used(struct bgp_addpath_bgp_data *d, afi_t afi,
				 safi_t safi)
{
	return d->total_peercount[afi][safi] > 0;
}

/*
 * 初始化某个 BGP 实例级 addpath 数据：遍历所有 AFI / SAFI 组合，把每种策略
 * 的 ID 分配器指针清为 NULL、把每种策略的邻居计数与总邻居计数清 0。
 * 仅在实例创建时调用一次。
 */
/*
 * Initialize the BGP instance level data for addpath.
 */
void bgp_addpath_init_bgp_data(struct bgp_addpath_bgp_data *d)
{
	safi_t safi;
	afi_t afi;
	int i;

	FOREACH_AFI_SAFI (afi, safi) {
		for (i = 0; i < BGP_ADDPATH_MAX; i++) {
			d->id_allocators[afi][safi][i] = NULL;
			d->peercount[afi][safi][i] = 0;
		}
		d->total_peercount[afi][safi] = 0;
	}
}

/*
 * 释放与某条 BGP 路径信息（bgp_path_info）相关的 addpath 资源：遍历所有策略，
 * 若该路径某策略下持有有效 TX ID，则把它归还到对应前缀（节点）的 ID 池。
 * 路径信息被销毁时调用，避免 ID 泄漏。
 */
/*
 * Free up resources associated with BGP route info structures.
 */
void bgp_addpath_free_info_data(struct bgp_addpath_info_data *d,
			      struct bgp_addpath_node_data *nd)
{
	int i;

	for (i = 0; i < BGP_ADDPATH_MAX; i++) {
		if (d->addpath_tx_id[i] != IDALLOC_INVALID)
			idalloc_free_to_pool(&nd->free_ids[i],
					     d->addpath_tx_id[i]);
	}
}

/*
 * 返回向指定邻居、在指定 AFI / SAFI 下发送某条路由时应使用的 addpath TX ID。
 * 即根据该邻居在该 AFI / SAFI 上配置的策略，取出路径信息里对应策略的 ID。
 * label-unicast 先归一化为 unicast；邻居策略越界时返回 IDALLOC_INVALID。
 */
/*
 * Return the addpath ID used to send a particular route, to a particular peer,
 * in a particular AFI/SAFI.
 */
uint32_t bgp_addpath_id_for_peer(struct peer *peer, afi_t afi, safi_t safi,
				struct bgp_addpath_info_data *d)
{
	if (safi == SAFI_LABELED_UNICAST)
		safi = SAFI_UNICAST;

	if (peer->addpath_type[afi][safi] < BGP_ADDPATH_MAX)
		return d->addpath_tx_id[peer->addpath_type[afi][safi]];
	else
		return IDALLOC_INVALID;
}

/*
 * 判断某条路径是否在任意一种策略下已被分配了 addpath ID。只要有一个策略的
 * addpath_tx_id 非 0 就返回 true。注意这里以 0 作为“未分配”的判据，与其他
 * 地方用 IDALLOC_INVALID 的判据不同。
 */
/*
 * Returns true if the path has an assigned addpath ID for any of the addpath
 * strategies.
 */
bool bgp_addpath_info_has_ids(struct bgp_addpath_info_data *d)
{
	int i;

	for (i = 0; i < BGP_ADDPATH_MAX; i++)
		if (d->addpath_tx_id[i] != 0)
			return true;

	return false;
}

/*
 * 释放与某个 BGP 前缀（bgp_dest）相关的 ID 池资源：遍历所有策略，把该前缀
 * 节点上对应策略的 ID 池里暂存的 ID 全部排空回该 AFI / SAFI / 策略的主分配器。
 * 前缀被删除时调用。
 */
/*
 * Releases any ID's associated with the BGP prefix.
 */
void bgp_addpath_free_node_data(struct bgp_addpath_bgp_data *bd,
			      struct bgp_addpath_node_data *nd, afi_t afi,
			      safi_t safi)
{
	int i;

	for (i = 0; i < BGP_ADDPATH_MAX; i++) {
		idalloc_drain_pool(bd->id_allocators[afi][safi][i],
				   &(nd->free_ids[i]));
	}
}

/*
 * 判断某策略是否要求开启 deterministic-med（DMED）才能正常工作。
 * 只有 BGP_ADDPATH_BEST_PER_AS（每 AS 最佳）依赖 DMED，因为其路径选择
 * 结果取决于 DMED 收敛出的“每 AS 最佳路径”。
 */
/*
 * Check to see if the addpath strategy requires DMED to be configured to work.
 */
bool bgp_addpath_dmed_required(int strategy)
{
	return strategy == BGP_ADDPATH_BEST_PER_AS;
}

/*
 * 根据策略判断某条路径是否应当（带 addpath ID）发给邻居：即该路径是否属于
 * 需要发送的路径集。ALL 恒为真，BEST_PER_AS 仅当路径被标记为 DMED_SELECTED
 * 时为真，BEST_SELECTED 恒为真（由上游挑选出最佳 N 条），NONE 与越界为假。
 */
/*
 * Return true if this is a path we should advertise due to a
 * configured addpath-tx knob
 */
bool bgp_addpath_tx_path(enum bgp_addpath_strat strat, struct bgp_path_info *pi)
{
	switch (strat) {
	case BGP_ADDPATH_NONE:
		return false;
	case BGP_ADDPATH_ALL:
		return true;
	case BGP_ADDPATH_BEST_PER_AS:
		if (CHECK_FLAG(pi->flags, BGP_PATH_DMED_SELECTED))
			return true;
		else
			return false;
	case BGP_ADDPATH_BEST_SELECTED:
		return true;
	case BGP_ADDPATH_MAX:
		return false;
	}

	assert(!"Reached end of function we should never hit");
	return false;
}

/*
 * 在单个前缀（bgp_dest，不带下一级表）上清理某策略的 ID：先排空该前缀节点
 * 的 ID 池，再遍历该前缀的所有路径，把持有有效 TX ID 的路径的 ID 归还给
 * 主分配器，并把路径上的记录置为 IDALLOC_INVALID。
 * label-unicast 先归一化为 unicast。它是 bgp_addpath_flush_type 的单前缀子过程。
 */
static void bgp_addpath_flush_type_rn(struct bgp *bgp, afi_t afi, safi_t safi,
				      enum bgp_addpath_strat addpath_type,
				      struct bgp_dest *dest)
{
	struct bgp_path_info *pi;

	if (safi == SAFI_LABELED_UNICAST)
		safi = SAFI_UNICAST;

	idalloc_drain_pool(
		bgp->tx_addpath.id_allocators[afi][safi][addpath_type],
		&(dest->tx_addpath.free_ids[addpath_type]));
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		if (pi->tx_addpath.addpath_tx_id[addpath_type]
		    != IDALLOC_INVALID) {
			idalloc_free(
				bgp->tx_addpath
					.id_allocators[afi][safi][addpath_type],
				pi->tx_addpath.addpath_tx_id[addpath_type]);
			pi->tx_addpath.addpath_tx_id[addpath_type] =
				IDALLOC_INVALID;
		}
	}
}

/*
 * 清理某实例在指定 AFI / SAFI / 策略下所有前缀上的 addpath ID，并销毁该
 * 策略的 ID 分配器（置回 NULL）。当某策略从“有邻居使用”变为“无人使用”时
 * 调用，用以释放跟踪 ID 所占的内存；对未使用的策略会跳过 bestpath 后的
 * ID 处理，因此这里是释放这批数据的唯一时机。
 */
/*
 * Purge all addpath ID's on a BGP instance associated with the addpath
 * strategy, and afi/safi combination. This lets us let go of all memory held to
 * track ID numbers associated with an addpath type not in use. Since
 * post-bestpath ID processing is skipped for types not used, this is the only
 * chance to free this data.
 */
static void bgp_addpath_flush_type(struct bgp *bgp, afi_t afi, safi_t safi,
				   enum bgp_addpath_strat addpath_type)
{
	struct bgp_dest *dest, *ndest;

	if (safi == SAFI_LABELED_UNICAST)
		safi = SAFI_UNICAST;

	for (dest = bgp_table_top(bgp->rib[afi][safi]); dest;
	     dest = bgp_route_next(dest)) {
		if (safi == SAFI_MPLS_VPN || safi == SAFI_EVPN) {
			struct bgp_table *table;

			table = bgp_dest_get_bgp_table_info(dest);
			if (!table)
				continue;

			for (ndest = bgp_table_top(table); ndest;
			     ndest = bgp_route_next(ndest))
				bgp_addpath_flush_type_rn(bgp, afi, safi,
							  addpath_type, ndest);
		} else {
			bgp_addpath_flush_type_rn(bgp, afi, safi, addpath_type,
						  dest);
		}
	}

	idalloc_destroy(bgp->tx_addpath.id_allocators[afi][safi][addpath_type]);
	bgp->tx_addpath.id_allocators[afi][safi][addpath_type] = NULL;
}

/*
 * 若某条路径在该策略下需要发送，则为它从分配器申请一个 TX ID 并记录到路径
 * 信息上；不需要发送的路径保持不变。是 populate_type 的单路径子过程。
 */
/*
 * Allocate an Addpath ID for the given type on a path, if necessary.
 */
static void bgp_addpath_populate_path(struct id_alloc *allocator,
				      struct bgp_path_info *path,
				      enum bgp_addpath_strat addpath_type)
{
	if (bgp_addpath_tx_path(addpath_type, path)) {
		path->tx_addpath.addpath_tx_id[addpath_type] =
			idalloc_allocate(allocator);
	}
}

/*
 * 某策略首次被使用时的“回填”：为该 AFI / SAFI / 策略新建一个 ID 分配器，
 * 预留默认源发（default-originate）专用 ID，然后遍历该 AFI / SAFI 下所有
 * 前缀（VPN / EVPN 需再下钻一层到内层表），为需要发送的路径逐一分配 ID。
 * 之所以要回填，是因为未使用的策略平时不做 ID 计算，必须在首次启用时补上。
 * label-unicast 的分配按 unicast SAFI 进行，故入口先归一化 SAFI。
 */
/*
 * Compute addpath ID's on a BGP instance associated with the addpath strategy,
 * and afi/safi combination. Since we won't waste the time computing addpath IDs
 * for unused strategies, the first time a peer is configured to use a strategy,
 * we have to backfill the data.
 * In labeled-unicast, addpath allocations SHOULD be done in unicast SAFI.
 */
static void bgp_addpath_populate_type(struct bgp *bgp, afi_t afi, safi_t safi,
				    enum bgp_addpath_strat addpath_type)
{
	struct bgp_dest *dest, *ndest;
	char buf[200];
	struct id_alloc *allocator;

	if (safi == SAFI_LABELED_UNICAST)
		safi = SAFI_UNICAST;

	snprintf(buf, sizeof(buf), "Addpath ID Allocator %s:%d/%d",
		 bgp_addpath_names(addpath_type)->config_name, (int)afi,
		 (int)safi);
	buf[sizeof(buf) - 1] = '\0';
	zlog_info("Computing addpath IDs for addpath type %s",
		bgp_addpath_names(addpath_type)->human_name);

	bgp->tx_addpath.id_allocators[afi][safi][addpath_type] =
		idalloc_new(buf);

	idalloc_reserve(bgp->tx_addpath.id_allocators[afi][safi][addpath_type],
		BGP_ADDPATH_TX_ID_FOR_DEFAULT_ORIGINATE);

	allocator = bgp->tx_addpath.id_allocators[afi][safi][addpath_type];

	for (dest = bgp_table_top(bgp->rib[afi][safi]); dest;
	     dest = bgp_route_next(dest)) {
		struct bgp_path_info *bi;

		if (safi == SAFI_MPLS_VPN || safi == SAFI_EVPN) {
			struct bgp_table *table;

			table = bgp_dest_get_bgp_table_info(dest);
			if (!table)
				continue;

			for (ndest = bgp_table_top(table); ndest;
			     ndest = bgp_route_next(ndest))
				for (bi = bgp_dest_get_bgp_path_info(ndest); bi;
				     bi = bi->next)
					bgp_addpath_populate_path(allocator, bi,
								  addpath_type);
		} else {
			for (bi = bgp_dest_get_bgp_path_info(dest); bi;
			     bi = bi->next)
				bgp_addpath_populate_path(allocator, bi,
							  addpath_type);
		}
	}
}

/*
 * 重新统计各 AFI / SAFI / 策略的邻居计数，并在某策略从无人用变有人用（第一次
 * 启用）时调用 populate_type 回填 ID，在从有人用变无人用时调用 flush_type 清理。
 * 统计时会把所有实例的邻居、以及 EVPN type-5 多路径场景（等价于按路径全部发送）
 * 计入对应策略。注意这里传参 afi 忽略（宏遍历），只用 bgp 实例及其邻居列表。
 */
/*
 * Handle updates to a peer or group's addpath strategy. If after adjusting
 * counts a addpath strategy is in use for the first time, or no longer in use,
 * the IDs for that strategy will be populated or flushed.
 */
void bgp_addpath_type_changed(struct bgp *bgp)
{
	afi_t afi;
	safi_t safi;
	struct listnode *node, *nnode;
	struct peer *peer;
	int peer_count[AFI_MAX][SAFI_MAX][BGP_ADDPATH_MAX];
	enum bgp_addpath_strat type;

	FOREACH_AFI_SAFI(afi, safi) {
		for (type=0; type<BGP_ADDPATH_MAX; type++) {
			peer_count[afi][safi][type] = 0;
		}
		bgp->tx_addpath.total_peercount[afi][safi] = 0;
	}

	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		FOREACH_AFI_SAFI(afi, safi) {
			type = peer->addpath_type[afi][safi];
			if (type != BGP_ADDPATH_NONE) {
				peer_count[afi][safi][type] += 1;
				bgp->tx_addpath.total_peercount[afi][safi] += 1;
			}
		}
	}

	FOREACH_AFI_SAFI (afi, safi) {
		if (advertise_type5_routes_multipath(bgp, afi) && safi == SAFI_UNICAST) {
			peer_count[afi][safi][BGP_ADDPATH_ALL] += 1;
			bgp->tx_addpath.total_peercount[afi][safi] += 1;
		}
	}

	FOREACH_AFI_SAFI(afi, safi) {
		for (type=0; type<BGP_ADDPATH_MAX; type++) {
			int old = bgp->tx_addpath.peercount[afi][safi][type];
			int new = peer_count[afi][safi][type];

			bgp->tx_addpath.peercount[afi][safi][type] = new;

			if (old == 0 && new != 0) {
				bgp_addpath_populate_type(bgp, afi, safi,
					type);
			} else if (old != 0 && new == 0) {
				bgp_addpath_flush_type(bgp, afi, safi, type);
			}
		}
	}
}

/*
 * 修改某邻居或邻居组的 addpath 策略并使其生效。先记录 best-selected 的路径
 * 条数 paths，若策略未变化则直接返回；对邻居组下发的“no”配置，成员需继承
 * 组的策略。改动后调用 type_changed 更新计数与 ID。若新策略要求 deterministic-med
 * 而实例未开启，则告警、开启并重算全部最佳路径。最后按需重置邻居：组类型
 * 则逐个重置继承旧策略的成员，普通邻居则对其做一次配置重置。
 */
/*
 * Change the addpath type assigned to a peer, or peer group. In addition to
 * adjusting the counts, peer sessions will be reset as needed to make the
 * change take effect.
 */
void bgp_addpath_set_peer_type(struct peer *peer, afi_t afi, safi_t safi,
			       enum bgp_addpath_strat addpath_type, uint16_t paths)
{
	struct bgp *bgp = peer->bgp;
	enum bgp_addpath_strat old_type;
	struct listnode *node, *nnode;
	struct peer *tmp_peer;
	struct peer_group *group;

	if (safi == SAFI_LABELED_UNICAST)
		safi = SAFI_UNICAST;

	peer->addpath_best_selected[afi][safi] = paths;

	old_type = peer->addpath_type[afi][safi];
	if (addpath_type == old_type)
		return;

	if (addpath_type == BGP_ADDPATH_NONE && peer->group &&
	    !CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* A "no" config on a group member inherits group */
		addpath_type = peer->group->conf->addpath_type[afi][safi];
	}

	peer->addpath_type[afi][safi] = addpath_type;

	bgp_addpath_type_changed(bgp);

	if (addpath_type != BGP_ADDPATH_NONE) {
		if (bgp_addpath_dmed_required(addpath_type)) {
			if (!CHECK_FLAG(bgp->flags,
					BGP_FLAG_DETERMINISTIC_MED)) {
				zlog_warn(
					"%s: enabling bgp deterministic-med, this is required for addpath-tx-bestpath-per-AS",
					peer->host);
				SET_FLAG(bgp->flags,
					 BGP_FLAG_DETERMINISTIC_MED);
				bgp_recalculate_all_bestpaths(bgp);
			}
		}
	}

	zlog_info("Resetting peer %s%pBP due to change in addpath config",
		  CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP) ? "group " : "",
		  peer);

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		group = peer->group;

		/* group will be null as peer_group_delete calls peer_delete on
		 * group->conf. That peer_delete will eventuallly end up here
		 * if the group was configured to tx addpaths.
		 */
		if (group != NULL) {
			for (ALL_LIST_ELEMENTS(group->peer, node, nnode,
			     tmp_peer)) {
				if (tmp_peer->addpath_type[afi][safi] ==
				    old_type) {
					bgp_addpath_set_peer_type(
						tmp_peer, afi, safi,
						addpath_type, paths);
				}
			}
		}
	} else {
		peer_change_action(peer, afi, safi, peer_change_reset);
	}

}

/*
 * 在 bestpath 之后运行：为前缀 bn 在指定 AFI / SAFI 下重新分配各策略的 TX ID。
 * 对每种在用策略，先把不再需要发送的路径的 ID 归还到该前缀的 ID 池，再把池中
 * ID 优先分配给新需要发送的路径，最后把池中剩余 ID 排空回主分配器。
 * 该“池中转手”的设计避免了 best-per-AS 换路径时不得不先 withdraw 再 update。
 * 注意：策略邻居计数为 0 时跳过该策略；label-unicast 先归一化为 unicast。
 */
/*
 * Intended to run after bestpath. This function will take TX IDs from paths
 * that no longer need them, and give them to paths that do. This prevents
 * best-per-as updates from needing to do a separate withdraw and update just to
 * swap out which path is sent.
 */
void bgp_addpath_update_ids(struct bgp *bgp, struct bgp_dest *bn, afi_t afi,
			    safi_t safi)
{
	int i;
	struct bgp_path_info *pi;
	struct id_alloc_pool **pool_ptr;

	if (safi == SAFI_LABELED_UNICAST)
		safi = SAFI_UNICAST;

	for (i = 0; i < BGP_ADDPATH_MAX; i++) {
		struct id_alloc *alloc =
			bgp->tx_addpath.id_allocators[afi][safi][i];
		pool_ptr = &(bn->tx_addpath.free_ids[i]);

		if (bgp->tx_addpath.peercount[afi][safi][i] == 0)
			continue;

		/* Free Unused IDs back to the pool.*/
		for (pi = bgp_dest_get_bgp_path_info(bn); pi; pi = pi->next) {
			if (pi->tx_addpath.addpath_tx_id[i] != IDALLOC_INVALID
			    && !bgp_addpath_tx_path(i, pi)) {
				idalloc_free_to_pool(pool_ptr,
					pi->tx_addpath.addpath_tx_id[i]);
				pi->tx_addpath.addpath_tx_id[i] =
					IDALLOC_INVALID;
			}
		}

		/* Give IDs to paths that need them (pulling from the pool) */
		for (pi = bgp_dest_get_bgp_path_info(bn); pi; pi = pi->next) {
			if (pi->tx_addpath.addpath_tx_id[i] == IDALLOC_INVALID
			    && bgp_addpath_tx_path(i, pi)) {
				pi->tx_addpath.addpath_tx_id[i] =
					idalloc_allocate_prefer_pool(
						alloc, pool_ptr);
			}
		}

		/* Free any IDs left in the pool to the main allocator */
		idalloc_drain_pool(alloc, pool_ptr);
	}
}
