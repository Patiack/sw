// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Conditional advertisement
 * Copyright (C) 2020  Samsung R&D Institute India - Bangalore.
 *			Madhurilatha Kuruganti
 */

/* ============================================================================
 * 【文件导读】bgp_conditional_adv.c —— 条件通告（conditional advertisement）
 * ============================================================================
 * 【它是什么】bgp_conditional_adv.h 里接口的实现：让“是否把某组路由通告给邻居”
 * 取决于“本机 BGP 表里是否存在另一组路由”。术语、配置形态与“为何必须轮询”见
 * 头文件导读（advertise-map / exist-map / non-exist-map、轮询周期宏）。
 *
 * 【两个关键判断函数】
 *   bgp_check_rmap_prefixes_in_bgp_table : 在整张 BGP 表上跑 condition-map，只要能
 *       命中一条即返回 RMAP_PERMITMATCH，用来回答“条件成立没有”。
 *   bgp_conditional_adv_routes          : 按 advertise-map 决定发/撤哪些路由，并
 *       真正调用子组通告接口把更新推给邻居。
 *
 * 【核心状态机（timer 回调里体现）】每个 (neighbor, afi, safi) 维护 advmap.update_type，
 *   取值 UPDATE_TYPE_ADVERTISE 或 UPDATE_TYPE_WITHDRAW。exist 与 non-exist 两种语义
 *   对“condition-map 是否命中”的映射是**相反**的，改动这段务必对照 condition 标志。
 *
 * 【两个变更标志】peer->advmap_table_change 表示“入方向 BGP 表变了”（按 peer 维护）；
 *   peer->advmap_config_change[afi][safi] 表示“该邻居的 map 配置变了”。定时器只在
 *   这两个标志有变化时才真正重算并发送，避免空转。
 *
 * 【计数与生命周期】bgp->condition_filter_count 统计全进程有多少个 advertise-map 在用，
 *   从 0 变 1 时启动轮询定时器，从 1 变 0 时取消——因此 enable/disable 必须成对。
 *
 * 【阅读顺序】bgp_check_rmap_prefixes_in_bgp_table → bgp_conditional_adv_routes →
 *   bgp_conditional_adv_timer（主逻辑）→ enable/disable → filter_update →
 *   peer_advertise_map_set/unset。
 * ============================================================================
 */

#include <zebra.h>

#include "bgpd/bgp_conditional_adv.h"
#include "bgpd/bgp_vty.h"

/* 【函数】在整张 BGP 表上评估 condition-map（route-map）：遍历每个目的前缀的每条
 * path，构造临时 path_info 后 route_map_apply，只要有一条命中 RMAP_PERMITMATCH 就
 * 立即返回（并 bgp_dest_unlock_node 释放当前节点）——用来回答“条件是否成立”。
 * 返回 RMAP_DENYMATCH 表示一条都没命中。副作用：往返遍历会加/解锁 dest 引用，
 * 提前 return 前必须解锁配对；每次循环后 bgp_attr_flush 释放临时属性。 */
static route_map_result_t
bgp_check_rmap_prefixes_in_bgp_table(struct bgp_table *table,
				     struct route_map *rmap)
{
	struct attr dummy_attr = {0};
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	struct bgp_path_info path = {0};
	struct bgp_path_info_extra path_extra = {0};
	const struct prefix *dest_p;
	route_map_result_t ret = RMAP_DENYMATCH;

	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		dest_p = bgp_dest_get_prefix(dest);
		assert(dest_p);

		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
			dummy_attr = *pi->attr;

			/* Fill temp path_info */
			prep_for_rmap_apply(&path, &path_extra, dest, pi, pi->peer, NULL,
					    &dummy_attr);

			RESET_FLAG(dummy_attr.rmap_change_flags);

			ret = route_map_apply(rmap, dest_p, &path);
			bgp_attr_flush(&dummy_attr);

			if (ret == RMAP_PERMITMATCH) {
				bgp_dest_unlock_node(dest);
				bgp_cond_adv_debug(
					"%s: Condition map routes present in BGP table",
					__func__);

				return ret;
			}
		}
	}

	bgp_cond_adv_debug("%s: Condition map routes not present in BGP table",
			   __func__);

	return ret;
}

/* 【函数】核心发送逻辑：按 advertise-map（rmap）向 peer 通告或撤回路由。流程：找
 * peer_af 与子组（AF 未协商则直接返回）→ 置子组重解析标志并清零 pscount → 遍历整表，
 * 对每条 path 跑 advertise-map，命中且被 bgp_check_selected 选中才处理 → 依据
 * update_type 决定发还是撤：
 *   UPDATE_TYPE_ADVERTISE：subgroup_announce_check 通过后用 bgp_adj_out_set_subgroup
 *       发出；失败需 bgp_attr_flush 释放临时 attr。
 *   UPDATE_TYPE_WITHDRAW：用 bgp_adj_out_unset_subgroup 撤回；但对开启 default-originate
 *       的 peer 遇默认路由不显式撤回，以免撤掉 default-originate 产生的默认路由。
 * 收尾清子组重解析标志。遍历中重置 rmap_change_flags，避免上次的修改痕迹影响判定。 */
static void bgp_conditional_adv_routes(struct peer *peer, afi_t afi,
				       safi_t safi, struct bgp_table *table,
				       struct route_map *rmap,
				       enum update_type update_type)
{
	bool addpath_capable;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	struct bgp_path_info path;
	struct peer_af *paf;
	const struct prefix *dest_p;
	struct update_subgroup *subgrp;
	struct attr advmap_attr = {0}, attr = {0};
	struct bgp_path_info_extra path_extra = {0};
	route_map_result_t ret;

	paf = peer_af_find(peer, afi, safi);
	if (!paf)
		return;

	subgrp = PAF_SUBGRP(paf);
	/* Ignore if subgroup doesn't exist (implies AF is not negotiated) */
	if (!subgrp)
		return;

	subgrp->pscount = 0;
	SET_FLAG(subgrp->sflags, SUBGRP_STATUS_TABLE_REPARSING);

	bgp_cond_adv_debug("%s: %s routes to/from %s for %s", __func__,
			   update_type == UPDATE_TYPE_ADVERTISE ? "Advertise"
								: "Withdraw",
			   peer->host, get_afi_safi_str(afi, safi, false));

	addpath_capable = bgp_addpath_encode_tx(peer, afi, safi);

	SET_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES);
	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		dest_p = bgp_dest_get_prefix(dest);
		assert(dest_p);

		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
			advmap_attr = *pi->attr;

			/* Fill temp path_info */
			prep_for_rmap_apply(&path, &path_extra, dest, pi, pi->peer, NULL,
					    &advmap_attr);

			RESET_FLAG(advmap_attr.rmap_change_flags);

			ret = route_map_apply(rmap, dest_p, &path);
			if (ret != RMAP_PERMITMATCH ||
			    !bgp_check_selected(pi, peer, addpath_capable, afi,
						safi)) {
				bgp_attr_flush(&advmap_attr);
				continue;
			}

			/* Skip route-map checks in
			 * subgroup_announce_check while executing from
			 * the conditional advertise scanner process.
			 * otherwise when route-map is also configured
			 * on same peer, routes in advertise-map may not
			 * be advertised as expected.
			 */
			if (update_type == UPDATE_TYPE_ADVERTISE &&
			    subgroup_announce_check(dest, pi, subgrp, dest_p,
						    &attr, &advmap_attr)) {
				if (!bgp_adj_out_set_subgroup(dest, subgrp,
							      &attr, pi))
					bgp_attr_flush(&attr);
			} else {
				/* If default originate is enabled for
				 * the peer, do not send explicit
				 * withdraw. This will prevent deletion
				 * of default route advertised through
				 * default originate.
				 */
				if (CHECK_FLAG(peer->af_flags[afi][safi],
					       PEER_FLAG_DEFAULT_ORIGINATE) &&
				    is_default_prefix(dest_p))
					break;

				bgp_adj_out_unset_subgroup(dest, subgrp,
					bgp_addpath_id_for_peer(
						peer, afi, safi,
						&pi->tx_addpath));

				bgp_attr_flush(&advmap_attr);
			}
		}
	}
	UNSET_FLAG(subgrp->sflags, SUBGRP_STATUS_TABLE_REPARSING);
}

/* 【函数】条件通告的定时器回调（周期性执行，周期为 bgp->condition_check_period）。
 * 先把自己重挂到 master 以保持周期触发，随后：
 *   1) 扫所有 peer，只要任一 peer 的 advmap_table_change 置位，就把 advmap_table_changed
 *      记真（用于决定本轮是否值得重算）；
 *   2) 遍历每个已建立、配置了 advertise-map 的 peer 的每个 (afi, safi)：
 *      - 用 condition-map 跑 bgp_check_rmap_prefixes_in_bgp_table 得条件结果 ret；
 *      - 依 condition(EXIST / NON-EXIST) 与 ret 推导 update_type（两种语义映射相反）；
 *      - 若 update_type 相比子组记录有变，则 update_group_adjust_peer 调整更新组；
 *      - 若此邻居 map 配置变了，先发一次常规更新（split + subgroup_announce_table）；
 *      - 最后调 bgp_conditional_adv_routes 按条件发/撤路由。
 *   3) 清 peer->advmap_table_change。
 * 注意 table 用 pfx_rcd_safi 取：labeled-unicast 的路由实际装在 unicast 表里，需换算。 */
/* Handler of conditional advertisement timer event.
 * Each route in the condition-map is evaluated.
 */
static void bgp_conditional_adv_timer(struct event *t)
{
	afi_t afi;
	safi_t safi;
	int pfx_rcd_safi;
	struct bgp *bgp = NULL;
	struct peer *peer = NULL;
	struct peer_af *paf = NULL;
	struct bgp_table *table = NULL;
	struct bgp_filter *filter = NULL;
	struct listnode *node, *nnode = NULL;
	struct update_subgroup *subgrp = NULL;
	route_map_result_t ret;
	bool advmap_table_changed = false;

	bgp = EVENT_ARG(t);
	assert(bgp);

	event_add_timer(bm->master, bgp_conditional_adv_timer, bgp,
			bgp->condition_check_period, &bgp->t_condition_check);

	/* loop through each peer and check if we have peers with
	 * advmap_table_change attribute set, to make sure we send
	 * conditional advertisements properly below.
	 * peer->advmap_table_change is added on incoming BGP UPDATES,
	 * but here it's used for outgoing UPDATES, hence we need to
	 * check if at least one peer got advmap_table_change.
	 */
	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
		if (peer->advmap_table_change) {
			advmap_table_changed = true;
			break;
		}
	}

	/* loop through each peer and advertise or withdraw routes if
	 * advertise-map is configured and prefix(es) in condition-map
	 * does exist(exist-map)/not exist(non-exist-map) in BGP table
	 * based on condition(exist-map or non-exist map)
	 */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		if (!peer_is_config_node(peer))
			continue;

		if (!peer_established(peer->connection))
			continue;

		FOREACH_AFI_SAFI (afi, safi) {
			if (!peer->afc_nego[afi][safi])
				continue;

			/* labeled-unicast routes are installed in the unicast
			 * table so in order to display the correct PfxRcd value
			 * we must look at SAFI_UNICAST
			 */
			pfx_rcd_safi = (safi == SAFI_LABELED_UNICAST)
					       ? SAFI_UNICAST
					       : safi;

			table = bgp->rib[afi][pfx_rcd_safi];
			if (!table)
				continue;

			filter = &peer->filter[afi][safi];

			if (!filter->advmap.aname || !filter->advmap.cname
			    || !filter->advmap.amap || !filter->advmap.cmap)
				continue;

			if (!peer->advmap_config_change[afi][safi] &&
			    !advmap_table_changed)
				continue;

			if (BGP_DEBUG(cond_adv, COND_ADV)) {
				if (peer->advmap_table_change)
					zlog_debug(
						"%s: %s - routes changed in BGP table.",
						__func__, peer->host);
				if (peer->advmap_config_change[afi][safi])
					zlog_debug(
						"%s: %s for %s - advertise/condition map configuration is changed.",
						__func__, peer->host,
						get_afi_safi_str(afi, safi,
								 false));
			}

			/* cmap (route-map attached to exist-map or
			 * non-exist-map) map validation
			 */
			ret = bgp_check_rmap_prefixes_in_bgp_table(
				table, filter->advmap.cmap);

			/* Derive conditional advertisement status from
			 * condition and return value of condition-map
			 * validation.
			 */
			if (filter->advmap.condition == CONDITION_EXIST)
				filter->advmap.update_type =
					(ret == RMAP_PERMITMATCH)
						? UPDATE_TYPE_ADVERTISE
						: UPDATE_TYPE_WITHDRAW;
			else
				filter->advmap.update_type =
					(ret == RMAP_PERMITMATCH)
						? UPDATE_TYPE_WITHDRAW
						: UPDATE_TYPE_ADVERTISE;

			/*
			 * Update condadv update type so
			 * subgroup_announce_check() can properly apply
			 * outbound policy according to advertisement state
			 */
			paf = peer_af_find(peer, afi, safi);
			if (paf && (SUBGRP_PEER(PAF_SUBGRP(paf))
					    ->filter[afi][safi]
					    .advmap.update_type !=
				    filter->advmap.update_type)) {
				/* Handle change to peer advmap */
				bgp_cond_adv_debug(
					"%s: advmap.update_type changed for peer %s, adjusting update_group.",
					__func__, peer->host);

				update_group_adjust_peer(paf);
			}

			/* Send regular update as per the existing policy.
			 * There is a change in route-map, match-rule, ACLs,
			 * or route-map filter configuration on the same peer.
			 */
			if (peer->advmap_config_change[afi][safi]) {

				bgp_cond_adv_debug(
					"%s: Configuration is changed on peer %s for %s, send the normal update first.",
					__func__, peer->host,
					get_afi_safi_str(afi, safi, false));
				if (paf) {
					update_subgroup_split_peer(paf, NULL);
					subgrp = paf->subgroup;

					if (subgrp && subgrp->update_group)
						subgroup_announce_table(
							paf->subgroup, NULL);
				}
				peer->advmap_config_change[afi][safi] = false;
			}

			/* Send update as per the conditional advertisement */
			bgp_conditional_adv_routes(peer, afi, safi, table,
						   filter->advmap.amap,
						   filter->advmap.update_type);
		}
		peer->advmap_table_change = false;
	}
}

/* 【函数】为 (peer, afi, safi) 激活条件通告：置 advmap_config_change 标志以触发首次
 * 重算；condition_filter_count 自增，**从 0 变 1**（首个使用者）时用 0 延迟立即启动
 * 轮询定时器。已有其他邻居在用则只计数、不重复建定时器。 */
void bgp_conditional_adv_enable(struct peer *peer, afi_t afi, safi_t safi)
{
	struct bgp *bgp = peer->bgp;

	assert(bgp);

	/* This flag is used to monitor conditional routes status in BGP table,
	 * and advertise/withdraw routes only when there is a change in BGP
	 * table w.r.t conditional routes
	 */
	peer->advmap_config_change[afi][safi] = true;

	/* advertise-map is already configured on at least one of its
	 * neighbors (AFI/SAFI). So just increment the counter.
	 */
	if (++bgp->condition_filter_count > 1) {
		bgp_cond_adv_debug("%s: condition_filter_count %d", __func__,
				   bgp->condition_filter_count);

		return;
	}

	/* Register for conditional routes polling timer */
	if (!event_is_scheduled(bgp->t_condition_check))
		event_add_timer(bm->master, bgp_conditional_adv_timer, bgp, 0,
				&bgp->t_condition_check);
}

/* 【函数】关闭 (peer, afi, safi) 的条件通告：condition_filter_count 自减，**减到 0**
 * （最后一个使用者）时取消轮询定时器。仍有其他使用者则只计数。必须与 enable 成对
 * 调用，否则会残留定时器。 */
void bgp_conditional_adv_disable(struct peer *peer, afi_t afi, safi_t safi)
{
	struct bgp *bgp = peer->bgp;

	assert(bgp);

	/* advertise-map is not configured on any of its neighbors or
	 * it is configured on more than one neighbor(AFI/SAFI).
	 * So there's nothing to do except decrementing the counter.
	 */
	if (--bgp->condition_filter_count != 0) {
		bgp_cond_adv_debug("%s: condition_filter_count %d", __func__,
				   bgp->condition_filter_count);

		return;
	}

	/* Last filter removed. So cancel conditional routes polling thread. */
	event_cancel(&bgp->t_condition_check);
}

/* 【函数】set/unset 的公共底层：把 advertise-map / condition-map 真正写进或从
 * peer->filter[afi][safi].advmap 中清除。filter_exists 记录原本是否已配置。
 *   set=false（删除）：释放旧名字、递减旧 amap 引用计数；若原本存在，则调
 *       bgp_conditional_adv_disable，再把 advmap 结构整体清零。
 *   set=true（写入）：复制新名字、保存 amap/cmap 对象与 condition 标志，amap 引用
 *       计数加一，置 advmap_config_change；若原本不存在则置初始 update_type 为
 *       ADVERTISE 并调 bgp_conditional_adv_enable。
 * 两种路径最后都调 peer_on_policy_change，让邻居按新策略重算并重发路由。 */
static void peer_advertise_map_filter_update(struct peer *peer, afi_t afi,
					     safi_t safi, const char *amap_name,
					     struct route_map *amap,
					     const char *cmap_name,
					     struct route_map *cmap,
					     bool condition, bool set)
{
	struct bgp_filter *filter;
	bool filter_exists = false;

	filter = &peer->filter[afi][safi];

	/* advertise-map is already configured. */
	if (filter->advmap.aname) {
		filter_exists = true;
		XFREE(MTYPE_BGP_FILTER_NAME, filter->advmap.aname);
		XFREE(MTYPE_BGP_FILTER_NAME, filter->advmap.cname);
	}

	route_map_counter_decrement(filter->advmap.amap);

	/* Removed advertise-map configuration */
	if (!set) {
		memset(&filter->advmap, 0, sizeof(filter->advmap));

		/* decrement condition_filter_count delete timer if
		 * this is the last advertise-map to be removed.
		 */
		if (filter_exists)
			bgp_conditional_adv_disable(peer, afi, safi);

		/* Process peer route updates. */
		peer_on_policy_change(peer, afi, safi, 1);

		return;
	}

	/* Update filter data with newly configured values. */
	filter->advmap.aname = XSTRDUP(MTYPE_BGP_FILTER_NAME, amap_name);
	filter->advmap.cname = XSTRDUP(MTYPE_BGP_FILTER_NAME, cmap_name);
	filter->advmap.amap = amap;
	filter->advmap.cmap = cmap;
	filter->advmap.condition = condition;
	route_map_counter_increment(filter->advmap.amap);
	peer->advmap_config_change[afi][safi] = true;

	/* Increment condition_filter_count and/or create timer. */
	if (!filter_exists) {
		filter->advmap.update_type = UPDATE_TYPE_ADVERTISE;
		bgp_conditional_adv_enable(peer, afi, safi);
	}

	/* Process peer route updates. */
	peer_on_policy_change(peer, afi, safi, 1);
}

/* 【函数】配置入口：`neighbor ... advertise-map A <exist-map|non-exist-map> C`。
 * 先对本 peer 调 filter_update(..., true) 落配置；若是普通 peer（非 peer-group）则置
 * override 标志（PEER_FT_ADVERTISE_MAP）后返回，跳过组成员处理；若 peer 是
 * peer-group，则对组内**未显式覆盖**该配置的每个成员同样落配置。恒返回 0。 */
/* Set advertise-map to the peer. */
int peer_advertise_map_set(struct peer *peer, afi_t afi, safi_t safi,
			   const char *advertise_name,
			   struct route_map *advertise_map,
			   const char *condition_name,
			   struct route_map *condition_map, bool condition)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Set configuration on peer. */
	peer_advertise_map_filter_update(peer, afi, safi, advertise_name,
					 advertise_map, condition_name,
					 condition_map, condition, true);

	/* Check if handling a regular peer & Skip peer-group mechanics. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Set override-flag and process peer route updates. */
		SET_FLAG(peer->filter_override[afi][safi][RMAP_OUT],
			 PEER_FT_ADVERTISE_MAP);
		return 0;
	}

	/*
	 * Set configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][RMAP_OUT],
			       PEER_FT_ADVERTISE_MAP))
			continue;

		/* Set configuration on peer-group member. */
		peer_advertise_map_filter_update(
			member, afi, safi, advertise_name, advertise_map,
			condition_name, condition_map, condition, true);
	}

	return 0;
}

/* 【函数】删除配置入口：`no neighbor ... advertise-map ...`。若本 peer 未配置过则直接
 * 返回 0；否则先**无条件**清 override 标志；若 peer 是组成员则改为从 peer-group 继承
 * 配置（回退到组配置），否则对本 peer 调 filter_update(..., false) 真正删除；最后对
 * peer-group 内未覆盖该配置的成员逐一执行删除。恒返回 0。 */
/* Unset advertise-map from the peer. */
int peer_advertise_map_unset(struct peer *peer, afi_t afi, safi_t safi,
			     const char *advertise_name,
			     struct route_map *advertise_map,
			     const char *condition_name,
			     struct route_map *condition_map, bool condition)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* advertise-map is not configured */
	if (!peer->filter[afi][safi].advmap.aname)
		return 0;

	/* Unset override-flag unconditionally. */
	UNSET_FLAG(peer->filter_override[afi][safi][RMAP_OUT],
		   PEER_FT_ADVERTISE_MAP);

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      filter[afi][safi].advmap.aname,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  filter[afi][safi].advmap.amap);
	} else
		peer_advertise_map_filter_update(
			peer, afi, safi, advertise_name, advertise_map,
			condition_name, condition_map, condition, false);

	/* Check if handling a regular peer and skip peer-group mechanics. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Process peer route updates. */
		bgp_cond_adv_debug("%s: Send normal update to %s for %s",
				   __func__, peer->host,
				   get_afi_safi_str(afi, safi, false));

		return 0;
	}

	/*
	 * Remove configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][RMAP_OUT],
			       PEER_FT_ADVERTISE_MAP))
			continue;
		/* Remove configuration on peer-group member. */
		peer_advertise_map_filter_update(
			member, afi, safi, advertise_name, advertise_map,
			condition_name, condition_map, condition, false);

		/* Process peer route updates. */
		bgp_cond_adv_debug("%s: Send normal update to %s for %s ",
				   __func__, member->host,
				   get_afi_safi_str(afi, safi, false));
	}

	return 0;
}
