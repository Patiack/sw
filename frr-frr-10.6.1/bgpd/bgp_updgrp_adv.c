// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * bgp_updgrp_adv.c: BGP update group advertisement and adjacency
 *                   maintenance
 *
 *
 * @copyright Copyright (C) 2014 Cumulus Networks, Inc.
 *
 * @author Avneesh Sachdev <avneesh@sproute.net>
 * @author Rajesh Varadarajan <rajesh@sproute.net>
 * @author Pradosh Mohapatra <pradosh@sproute.net>
 */
/* ============================================================================
 * 【文件导读】bgp_updgrp_adv.c —— 出向通告状态机：把“选路结果”变成“待发内容”
 * ============================================================================
 * 职责与定位：
 *   本文件处于 update-group 主线的“内容生产”环节。它维护每个 update_subgroup 上
 *   “已经通告给成员的前缀快照”(bgp_adj_out / adj) 和“待发送的通告/撤销队列”
 *   (bgp_advertise / adv)，并实现“根据 RIB 变化决定向某子组通告(advertise)还是撤销
 *   (withdraw)”的核心逻辑。它不负责编码字节流（那是 bgp_updgrp_packet.c），而是把
 *   该发什么、该撤什么组织进子组的 sync->update / sync->withdraw 两个 FIFO，供编码器取用。
 *
 * 两个关键数据结构（务必先理解）：
 *   bgp_adj_out (adj) —— 某前缀在某子组的“已通告快照”：记录该前缀本子组上次发出去的属性
 *       (adj->attr)、关联的待发通告(adj->adv)、标签(adj->labels)、addpath_tx_id。
 *       它挂在两层索引里：dest->adj_out 的红黑树(RB, 按 subgroup+addpath_id 排序) 与
 *       subgrp->adjq 的尾队列。用于判断“同一条路由是否重复／是否需要撤销”。
 *   bgp_advertise (adv) —— 一条“待编码发送”的通告或撤销请求，挂在 baa 的 fifo 或
 *       sync->withdraw 上；编码后由 packet.c 消费并回填 adj->attr。
 *
 * 关键路径：
 *   上游：bgp_route.c（选路变化 → group_announce_route → group_announce_route_walkcb）；
 *        bgp_fsm.c / peer_af_announce_route（同组重通告 → subgroup_announce_route）；
 *        bgp_route.c default-originate 相关（subgroup_default_originate）。
 *   下游：bgp_updgrp_packet.c 的 subgroup_update_packet / subgroup_withdraw_packet 从
 *        sync FIFO 取 adv 编码成 bpacket。
 *
 * 关键函数清单（建议阅读顺序）：
 *   1) bgp_adj_out_alloc / adj_free / adj_lookup —— adj 的分配、释放、按 (子组,addpath) 查找。
 *   2) bgp_adj_out_set_subgroup —— 决定并排队一次“通告”（含重复抑制）。
 *   3) bgp_adj_out_unset_subgroup —— 决定并排队一次“撤销”。
 *   4) subgroup_announce_table / subgroup_announce_route —— 遍历 RIB 重通告。
 *   5) subgroup_default_originate —— default-originate 的特殊处理（含 route-map）。
 *   6) group_announce_route / group_announce_route_walkcb —— 单条路由变化的入口与分发。
 *   7) subgroup_coalesce_timer / subgroup_announce_all —— 合并延迟定时器与首次通告。
 *
 * 建议阅读顺序：先 1（adj 模型），再 2、3（增/撤），再 4、5（整表重通告与默认路由），
 *   最后 6、7（事件入口与定时器）。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "memory.h"
#include "prefix.h"
#include "hash.h"
#include "frrevent.h"
#include "queue.h"
#include "routemap.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_updgrp.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_addpath.h"
#include "bgpd/bgp_nhc.h"
#include "bgpd/bgp_trace.h"


/********************
 * PRIVATE FUNCTIONS
 ********************/
/* ========== 1. adj 模型：比较、红黑树生成、查找、释放 ========== */
/* 【bgp_adj_out_compare】红黑树 dest->adj_out 的节点排序比较函数：先按所属子组指针、
 *   再按 addpath_tx_id 排序。
 * 参数 o1/o2：两个 bgp_adj_out。
 * 返回：<0 / 0 / >0。
 * 关键点：同一目的前缀下，每个子组、每个 addpath_tx_id 组合至多一个 adj。
 *   不支持 addpath 的子组 addpath_tx_id 恒为 0。 */
static int bgp_adj_out_compare(const struct bgp_adj_out *o1,
			       const struct bgp_adj_out *o2)
{
	if (o1->subgroup < o2->subgroup)
		return -1;

	if (o1->subgroup > o2->subgroup)
		return 1;

	if (o1->addpath_tx_id < o2->addpath_tx_id)
		return -1;

	if (o1->addpath_tx_id > o2->addpath_tx_id)
		return 1;

	return 0;
}
/* 用上面的比较函数实例化名为 bgp_adj_out_rb 的红黑树类型（成员是 adj_entry 字段）。 */
RB_GENERATE(bgp_adj_out_rb, bgp_adj_out, adj_entry, bgp_adj_out_compare);

/* 【adj_lookup】在某目的前缀 dest 的 adj_out 红黑树里查找指定 (子组, addpath_tx_id) 的 adj。
 * 参数 dest：目的前缀节点；subgrp：子组；addpath_tx_id：Add-Path 出向 ID（非 addpath 传 0）。
 * 返回：命中的 bgp_adj_out，或 NULL。
 * 调用时机：set/unset/remove 通告前判断该前缀是否已在本子组有快照。 */
static inline struct bgp_adj_out *adj_lookup(struct bgp_dest *dest,
					     struct update_subgroup *subgrp,
					     uint32_t addpath_tx_id)
{
	struct bgp_adj_out lookup;

	if (!dest || !subgrp)
		return NULL;

	/* update-groups that do not support addpath will pass 0 for
	 * addpath_tx_id. */
	lookup.subgroup = subgrp;
	lookup.addpath_tx_id = addpath_tx_id;

	return RB_FIND(bgp_adj_out_rb, &dest->adj_out, &lookup);
}

/* 【adj_free】释放一个 adj 及其全部挂接关系。
 * 参数 adj：待释放对象。
 * 返回：无。
 * 关键点（三处解链 + 一次 unlock，顺序敏感）：
 *   - 先释放标签(labels 解引用计数)；
 *   - 从子组 adjq 尾队列移除并减 adj_count；
 *   - 从 dest 红黑树移除；
 *   - bgp_dest_unlock_node 解除分配时对 dest 的引用计数（配对 bgp_adj_out_alloc 里的 lock）。 */
static void adj_free(struct bgp_adj_out *adj)
{
	bgp_labels_unintern(&adj->labels);

	TAILQ_REMOVE(&(adj->subgroup->adjq), adj, subgrp_adj_train);
	SUBGRP_DECR_STAT(adj->subgroup, adj_count);

	RB_REMOVE(bgp_adj_out_rb, &adj->dest->adj_out, adj);
	bgp_dest_unlock_node(adj->dest);

	XFREE(MTYPE_BGP_ADJ_OUT, adj);
}

/* 【subgrp_announce_addpath_best_selected】当子组启用 Add-Path 且配置为
 *   BGP_ADDPATH_BEST_SELECTED 时，为同一前缀挑出“最佳 N 条”(N=addpath_best_selected
 *   与 paths-limit 取小)非 Selected 路径，作为额外通告对象（RFC7911 的 send best N）。
 * 参数 dest：目的前缀；subgrp：目标子组。
 * 返回：无。
 * 调用时机：subgroup_announce_table 遍历到 dest 且 addpath_capable 时。
 * 关键点：
 *   - 第一段循环用 bgp_path_info_cmp 反复挑出“次优”路径，凑够 paths_limit 条放入 list。
 *   - 第二段循环遍历全部 path_info：被选中的走 subgroup_process_announce_selected，
 *     未选中且非 Selected 的用 NULL(表示撤销/不选)处理；Selected 路径跳过交给后面主流程。
 *   - 非 BEST_SELECTED 模式则按 paths_limit 顺序通告（超出上限的用 NULL 处理）。 */
static void
subgrp_announce_addpath_best_selected(struct bgp_dest *dest,
				      struct update_subgroup *subgrp)
{
	afi_t afi = SUBGRP_AFI(subgrp);
	safi_t safi = SUBGRP_SAFI(subgrp);
	struct peer *peer = SUBGRP_PEER(subgrp);
	enum bgp_path_selection_reason reason;
	char pfx_buf[PREFIX2STR_BUFFER] = {};
	int paths_eq = 0;
	struct list *list = list_new();
	struct bgp_path_info *pi = NULL;
	uint16_t paths_count = 0;
	uint16_t paths_limit = peer->addpath_paths_limit[afi][safi].receive;

	if (peer->addpath_type[afi][safi] == BGP_ADDPATH_BEST_SELECTED) {
		paths_limit =
			paths_limit
				? MIN(paths_limit,
				      peer->addpath_best_selected[afi][safi])
				: peer->addpath_best_selected[afi][safi];

		while (paths_count++ < paths_limit) {
			struct bgp_path_info *exist = NULL;

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				if (listnode_lookup(list, pi))
					continue;

				if (CHECK_FLAG(pi->flags, BGP_PATH_SELECTED))
					continue;

				if (bgp_path_info_cmp(peer->bgp, pi, exist,
						      &paths_eq, NULL, 0,
						      pfx_buf, afi, safi,
						      &reason))
					exist = pi;
			}

			if (exist)
				listnode_add(list, exist);
		}
	}

	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		uint32_t id = bgp_addpath_id_for_peer(peer, afi, safi,
						      &pi->tx_addpath);

		if (peer->addpath_type[afi][safi] ==
		    BGP_ADDPATH_BEST_SELECTED) {
			if (listnode_lookup(list, pi))
				subgroup_process_announce_selected(
					subgrp, pi, dest, afi, safi, id);
			else {
				if (CHECK_FLAG(pi->flags, BGP_PATH_SELECTED))
					continue;

				subgroup_process_announce_selected(
					subgrp, NULL, dest, afi, safi, id);
			}
		} else {
			/* No Paths-Limit involved */
			if (!paths_limit) {
				subgroup_process_announce_selected(subgrp, pi,
								   dest, afi,
								   safi, id);
				continue;
			}

			/* If we have Paths-Limit capability, we MUST
			 * not send more than the number of paths expected
			 * by the peer.
			 */
			if (paths_count++ < paths_limit)
				subgroup_process_announce_selected(subgrp, pi,
								   dest, afi,
								   safi, id);
			else
				subgroup_process_announce_selected(subgrp, NULL,
								   dest, afi,
								   safi, id);
		}
	}

	if (list)
		list_delete(&list);
}

/* 【subgrp_withdraw_stale_addpath】Add-Path 场景下，撤销本子组里“已不再存在对应路径”的
 *   陈旧通告：遍历 dest 下本子组的所有 adj，若其 addpath_tx_id 已不对应任何现存 path，
 *   就对该 id 发一次撤销（NULL 表示撤销）。
 * 参数 ctx：遍历上下文（含 dest）；subgrp：目标子组。
 * 返回：无。
 * 调用时机：group_announce_route_walkcb 处理 addpath 能力子组、或 coalesce 定时器在跑时。 */
static void subgrp_withdraw_stale_addpath(struct updwalk_context *ctx,
					  struct update_subgroup *subgrp)
{
	struct bgp_adj_out *adj, *adj_next;
	uint32_t id;
	struct bgp_path_info *pi;
	afi_t afi = SUBGRP_AFI(subgrp);
	safi_t safi = SUBGRP_SAFI(subgrp);
	struct peer *peer = SUBGRP_PEER(subgrp);

	/* Look through all of the paths we have advertised for this rn and send
	 * a withdraw for the ones that are no longer present */
	RB_FOREACH_SAFE (adj, bgp_adj_out_rb, &ctx->dest->adj_out, adj_next) {
		if (adj->subgroup != subgrp)
			continue;

		for (pi = bgp_dest_get_bgp_path_info(ctx->dest); pi;
		     pi = pi->next) {
			id = bgp_addpath_id_for_peer(peer, afi, safi,
						     &pi->tx_addpath);

			if (id == adj->addpath_tx_id) {
				break;
			}
		}

		if (!pi) {
			subgroup_process_announce_selected(subgrp, NULL,
							   ctx->dest, afi, safi,
							   adj->addpath_tx_id);
		}
	}
}

/* 【group_announce_route_walkcb】单条路由变化的核心分发回调：对某 update_group 的每个子组，
 *   决定是通告新路径(ctx->pi)、撤销(ctx->pi==NULL)、还是处理 addpath 的多路径变化。
 * 参数 updgrp：当前组；arg：updwalk_context（ctx->dest=目的前缀，ctx->pi=新选中路径或 NULL）。
 * 返回：UPDWALK_CONTINUE。
 * 调用时机：group_announce_route → update_group_af_walk 遍历时（有路由变化）。
 * 关键点：
 *   - addpath_capable 分支：先处理陈旧 addpath 撤销，再发 best-selected，最后才处理
 *     最佳路径(ctx->pi)——顺序保证“advertised”输出里展示的是最佳路径属性。
 *   - 非 addpath 分支：若子组 coalesce 定时器在跑，通常只做撤销(避免过早通告触发额外报文)，
 *     除非有新路径；否则按 ctx->pi 有/无决定通告或对已有 adj 撤销。
 *   - 无论哪支，最后都调用 bgp_notify_conditional_adv_scanner 通知条件通告模块。 */
static int group_announce_route_walkcb(struct update_group *updgrp, void *arg)
{
	struct updwalk_context *ctx = arg;
	struct update_subgroup *subgrp;
	afi_t afi;
	safi_t safi;
	struct peer *peer;
	struct bgp_adj_out *adj, *adj_next;
	bool addpath_capable;

	afi = UPDGRP_AFI(updgrp);
	safi = UPDGRP_SAFI(updgrp);
	peer = UPDGRP_PEER(updgrp);
	addpath_capable = bgp_addpath_encode_tx(peer, afi, safi);

	if (BGP_DEBUG(update, UPDATE_OUT))
		zlog_debug("%s: afi=%s, safi=%s, p=%pBD", __func__,
			   afi2str(afi), safi2str(safi), ctx->dest);

	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		/* An update-group that uses addpath */
		if (addpath_capable) {
			/* Send withdrawals without waiting for coalesting timer
			 * to expire.
			 */
			if (subgrp->t_coalesce) {
				subgrp_withdraw_stale_addpath(ctx, subgrp);

				goto done;
			}

			subgrp_withdraw_stale_addpath(ctx, subgrp);
			subgrp_announce_addpath_best_selected(ctx->dest, subgrp);

			/* Process the bestpath last so the
			 * "show [ip] bgp neighbor x.x.x.x advertised" output shows
			 * the attributes from the bestpath.
			 */
			if (ctx->pi)
				subgroup_process_announce_selected(
					subgrp, ctx->pi, ctx->dest, afi, safi,
					bgp_addpath_id_for_peer(peer, afi, safi,
								&ctx->pi->tx_addpath));
		} else {
			/* Send withdrawals without waiting for coalesting timer
			 * to expire.
			 */
			if (subgrp->t_coalesce) {
				if (!ctx->pi || CHECK_FLAG(ctx->pi->flags, BGP_PATH_UNUSEABLE)) {
					RB_FOREACH_SAFE (adj, bgp_adj_out_rb, &ctx->dest->adj_out,
							 adj_next) {
						if (adj->subgroup == subgrp) {
							subgroup_process_announce_selected(
								subgrp, NULL, ctx->dest, afi, safi,
								adj->addpath_tx_id);
						}
					}
				}

				goto done;
			}

			if (ctx->pi) {
				subgroup_process_announce_selected(
					subgrp, ctx->pi, ctx->dest, afi, safi,
					bgp_addpath_id_for_peer(peer, afi, safi,
								&ctx->pi->tx_addpath));
			} else {
				RB_FOREACH_SAFE (adj, bgp_adj_out_rb, &ctx->dest->adj_out,
						 adj_next) {
					if (adj->subgroup == subgrp) {
						subgroup_process_announce_selected(subgrp, NULL,
										   ctx->dest, afi,
										   safi,
										   adj->addpath_tx_id);
					}
				}
			}
		}

done:
		/* Notify BGP Conditional advertisement */
		bgp_notify_conditional_adv_scanner(subgrp);
	}

	return UPDWALK_CONTINUE;
}

/* 【subgrp_show_adjq_vty】把一个子组的 adj 快照打印成表（用于
 *   `show bgp ... update-group ... adj-queue / advertised`）。
 * 参数 subgrp：子组；vty：输出；flags：UPDWALK_FLAGS_ADVQUEUE 显示待发队列内容、
 *     UPDWALK_FLAGS_ADVERTISED 显示已通告内容。
 * 返回：无。
 * 关键点：遍历 RIB，对每个 dest 的 adj_out 红黑树里属于本子组的 adj，分别打印其待发属性
 *   (adj->adv->baa->attr) 或已通告属性(adj->attr)，并统计条数。表头只打印一次。 */
static void subgrp_show_adjq_vty(struct update_subgroup *subgrp,
				 struct vty *vty, uint8_t flags)
{
	struct bgp_table *table;
	struct bgp_adj_out *adj;
	unsigned long output_count;
	struct bgp_dest *dest;
	int header1 = 1;
	struct bgp *bgp;
	int header2 = 1;

	bgp = SUBGRP_INST(subgrp);
	if (!bgp)
		return;

	table = bgp->rib[SUBGRP_AFI(subgrp)][SUBGRP_SAFI(subgrp)];

	output_count = 0;

	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		const struct prefix *dest_p = bgp_dest_get_prefix(dest);

		RB_FOREACH (adj, bgp_adj_out_rb, &dest->adj_out) {
			if (adj->subgroup != subgrp)
				continue;

			if (header1) {
				vty_out(vty,
					"BGP table version is %" PRIu64
					", local router ID is %pI4\n",
					table->version, &bgp->router_id);
				vty_out(vty, BGP_SHOW_SCODE_HEADER);
				vty_out(vty, BGP_SHOW_OCODE_HEADER);
				header1 = 0;
			}
			if (header2) {
				vty_out(vty, BGP_SHOW_HEADER);
				header2 = 0;
			}
			if ((flags & UPDWALK_FLAGS_ADVQUEUE) && adj->adv &&
			    adj->adv->baa) {
				route_vty_out_tmp(vty, bgp, dest, dest_p,
						  adj->adv->baa->attr,
						  SUBGRP_SAFI(subgrp), 0, NULL,
						  false);
				output_count++;
			}
			if ((flags & UPDWALK_FLAGS_ADVERTISED) && adj->attr) {
				route_vty_out_tmp(vty, bgp, dest, dest_p,
						  adj->attr, SUBGRP_SAFI(subgrp),
						  0, NULL, false);
				output_count++;
			}
		}
	}
	if (output_count != 0)
		vty_out(vty, "\nTotal number of prefixes %ld\n", output_count);
}

/* 【updgrp_show_adj_walkcb】遍历组的每个子组，逐个调用 subgrp_show_adjq_vty 打印 adj 表。
 * 返回：UPDWALK_CONTINUE。调用时机：updgrp_show_adj 遍历时。 */
static int updgrp_show_adj_walkcb(struct update_group *updgrp, void *arg)
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
		subgrp_show_adjq_vty(subgrp, vty, ctx->flags);
	}
	return UPDWALK_CONTINUE;
}

/* 【updgrp_show_adj】show adj 表的骨架：填 ctx 后 af-walk 遍历（flags 决定显示队列/已通告）。 */
static void updgrp_show_adj(struct bgp *bgp, afi_t afi, safi_t safi,
			    struct vty *vty, uint64_t id, uint8_t flags)
{
	struct updwalk_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.vty = vty;
	ctx.subgrp_id = id;
	ctx.flags = flags;

	update_group_af_walk(bgp, afi, safi, updgrp_show_adj_walkcb, &ctx);
}

/* 【subgroup_coalesce_timer】合并延迟(coalesce)定时器到期回调：真正执行一次全表通告。
 * 参数 event：其 arg 为 update_subgroup。
 * 返回：无。
 * 调用时机：subgroup_announce_all 排定的合并延迟到期（见 bgp_updgrp.h 的 coalesce 公式）。
 * 关键点：
 *   - 清 t_coalesce、v_coalesce=0（此后按常规逻辑不再延迟）。
 *   - subgroup_announce_route 执行通告，这会启动成员的 route-adv 定时器(MRAI)。
 *   - 但初始更新希望“比 MRAI 更快”，且无路由可发时这里也是触发 EOR 的手段：若未处于
 *     update-delay、且未启用 suppress-fib，则对每个成员取消旧 t_routeadv 并用 0 时延重启
 *     bgp_routeadv_timer，促成立即写出（含 EOR）。 */
static void subgroup_coalesce_timer(struct event *event)
{
	struct update_subgroup *subgrp;
	struct bgp *bgp;
	safi_t safi;

	subgrp = EVENT_ARG(event);
	if (bgp_debug_update(NULL, NULL, subgrp->update_group, 0))
		zlog_debug("u%" PRIu64 ":s%" PRIu64" announcing routes upon coalesce timer expiry(%u ms)",
			   (SUBGRP_UPDGRP(subgrp))->id, subgrp->id,
			   subgrp->v_coalesce);

	frrtrace(3, frr_bgp, upd_announce_route_on_coalesce_timer_expiry,
		 (SUBGRP_UPDGRP(subgrp))->id, subgrp->id, subgrp->v_coalesce);

	subgrp->t_coalesce = NULL;
	subgrp->v_coalesce = 0;
	bgp = SUBGRP_INST(subgrp);
	subgroup_announce_route(subgrp);
	safi = SUBGRP_SAFI(subgrp);

	/* While the announce_route() may kick off the route advertisement timer
	 * for
	 * the members of the subgroup, we'd like to send the initial updates
	 * much
	 * faster (i.e., without enforcing MRAI). Also, if there were no routes
	 * to
	 * announce, this is the method currently employed to trigger the EOR.
	 */
	if (!bgp_update_delay_active(SUBGRP_INST(subgrp)) &&
	    !(bgp_fibupd_safi(safi) && BGP_SUPPRESS_FIB_ENABLED(bgp))) {

		struct peer_af *paf;
		struct peer *peer;

		SUBGRP_FOREACH_PEER (subgrp, paf) {
			peer = PAF_PEER(paf);
			struct peer_connection *connection = peer->connection;

			event_cancel(&connection->t_routeadv);
			BGP_TIMER_ON(connection->t_routeadv, bgp_routeadv_timer,
				     0);
		}
	}
}

/* 【update_group_announce_walkcb】对所有子组强制全量重通告（用于 `clear bgp ... soft` /
 *   全局重通告）：先置 FORCE_UPDATES（避免 subgroup_announce_table 里的重复抑制吞掉更新），
 *   再 subgroup_announce_all。
 * 返回：UPDWALK_CONTINUE。调用时机：update_group_announce 遍历时。 */
static int update_group_announce_walkcb(struct update_group *updgrp, void *arg)
{
	struct update_subgroup *subgrp;

	UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
		/* Avoid suppressing duplicate routes later
		 * when processing in subgroup_announce_table().
		 */
		SET_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES);

		subgroup_announce_all(subgrp);
	}

	return UPDWALK_CONTINUE;
}

/* 【update_group_announce_rrc_walkcb】仅对“路由反射器客户端”子组重通告（RR 场景）：
 *   只有代表 peer 配了 PEER_FLAG_REFLECTOR_CLIENT 的组才 subgroup_announce_all。
 * 返回：UPDWALK_CONTINUE。调用时机：update_group_announce_rrclients 遍历时。 */
static int update_group_announce_rrc_walkcb(struct update_group *updgrp,
					    void *arg)
{
	struct update_subgroup *subgrp;
	afi_t afi;
	safi_t safi;
	struct peer *peer;

	afi = UPDGRP_AFI(updgrp);
	safi = UPDGRP_SAFI(updgrp);
	peer = UPDGRP_PEER(updgrp);

	/* Only announce if this is a group of route-reflector-clients */
	if (CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_REFLECTOR_CLIENT)) {
		UPDGRP_FOREACH_SUBGRP (updgrp, subgrp) {
			subgroup_announce_all(subgrp);
		}
	}

	return UPDWALK_CONTINUE;
}

/********************
 * PUBLIC FUNCTIONS
 ********************/
/* ========== 2. adj 的分配/释放/增删通告 ========== */

/**
 * Allocate an adj-out object. Do proper initialization of its fields,
 * primarily its association with the subgroup and the prefix.
 */
/* 【bgp_adj_out_alloc】分配一个 adj 并把它挂进两处索引，建立与子组、前缀的关联。
 * 参数 subgrp：所属子组；dest：目的前缀；addpath_tx_id：Add-Path 出向 ID。
 * 返回：新 adj。
 * 调用时机：bgp_adj_out_set_subgroup（首次向某前缀发通告）、
 *   update_subgroup_copy_adj_out（分裂时克隆）。
 * 关键点：bgp_dest_lock_node(dest) 增加对前缀节点的引用，配对释放见 adj_free；
 *   adj 同时入 dest 的红黑树与 subgrp->adjq 尾队列，并增 adj_count。 */
struct bgp_adj_out *bgp_adj_out_alloc(struct update_subgroup *subgrp,
				      struct bgp_dest *dest,
				      uint32_t addpath_tx_id)
{
	struct bgp_adj_out *adj;

	adj = XCALLOC(MTYPE_BGP_ADJ_OUT, sizeof(struct bgp_adj_out));
	adj->subgroup = subgrp;
	adj->addpath_tx_id = addpath_tx_id;

	RB_INSERT(bgp_adj_out_rb, &dest->adj_out, adj);
	bgp_dest_lock_node(dest);
	adj->dest = dest;

	TAILQ_INSERT_TAIL(&(subgrp->adjq), adj, subgrp_adj_train);
	SUBGRP_INCR_STAT(subgrp, adj_count);
	return adj;
}


/* 【bgp_advertise_clean_subgroup】清理 adj 当前关联的待发通告 adv，并返回“同属性 baa 队列里
 *   的下一个候选 adv”（供编码器接着处理同属性的其它前缀）。
 * 参数 subgrp：子组；adj：其 adj->adv 将被清理。
 * 返回：该属性桶 fifo 中的下一个 adv，或 NULL。
 * 调用时机：见下方 bgp_adj_out_set/unset —— 替换旧通告、重复抑制、撤销后清理时。
 * 关键点：
 *   - adv->baa 非空（通告类）时：从 baa 的 fifo 删除本 adv，取下一候选，并把 baa 反引用
 *     计数减一(bgp_advertise_attr_unintern)——共享同一属性的多条前缀共用一个 baa。
 *   - adv->baa 为空（撤销类）时：fhead 指向 sync->withdraw。
 *   - 最后从对应 FIFO 删除 adv 并释放它，adj->adv 置 NULL。 */
struct bgp_advertise *
bgp_advertise_clean_subgroup(struct update_subgroup *subgrp,
			     struct bgp_adj_out *adj)
{
	struct bgp_advertise *adv;
	struct bgp_advertise_attr *baa;
	struct bgp_advertise *next;
	struct bgp_adv_fifo_head *fhead;

	adv = adj->adv;
	baa = adv->baa;
	next = NULL;

	if (baa) {
		fhead = &subgrp->sync->update;

		/* Unlink myself from advertise attribute FIFO.  */
		bgp_advertise_delete(baa, adv);

		/* Fetch next advertise candidate. */
		next = bgp_advertise_attr_fifo_first(&baa->fifo);

		/* Unintern BGP advertise attribute.  */
		bgp_advertise_attr_unintern(subgrp->hash, baa);
	} else
		fhead = &subgrp->sync->withdraw;


	/* Unlink myself from advertisement FIFO.  */
	bgp_adv_fifo_del(fhead, adv);

	/* Free memory.  */
	bgp_advertise_free(adj->adv);
	adj->adv = NULL;

	return next;
}

/* 【bgp_adj_out_set_subgroup】决定并排队一次“向本子组通告某前缀”的操作（核心函数）。
 * 参数 dest：目的前缀；subgrp：目标子组；attr：出向属性；path：被通告的路径(bgp_path_info)。
 * 返回：true=已入队待发；false=被抑制/失败。
 * 调用时机：group_announce_route_walkcb / subgroup_announce_table / subgroup_default_originate
 *   在确定要通告某前缀时。
 * 关键点（重复抑制与版本语义）：
 *   - 若 dest/subgrp/addpath_id 已有 adj 则复用，否则 bgp_adj_out_alloc 新建；两种情况都
 *     在“表重解析期”累加 pscount(已发前缀计数)。
 *   - 重复抑制：在 BGP_FLAG_SUPPRESS_DUPLICATES 开启、且未强制更新、且新属性与“上次已发或
 *     已排队”的属性完全相同（含标签相同）时，判定为重复 UPDATE 而不重发；但为了最终能让
 *     子组合并，仍需把 subgrp->version 提升到 dest->version（否则版本对不齐永远不能合并）。
 *   - 否则清理旧通告，新建 adv，`bgp_path_info_lock(path)` 引用被通告路径，把属性 bgp_attr_intern
 *     进 baa(广告属性)，记标签，挂入 baa 的 fifo 与 sync->update FIFO。
 *   - 若 update FIFO 原本为空，则触发成员 peer 的 MRAI 调整(bgp_adjust_routeadv)——第一个
 *     更新入队意味着需要尽快让 socket 写出。 */
bool bgp_adj_out_set_subgroup(struct bgp_dest *dest,
			      struct update_subgroup *subgrp, struct attr *attr,
			      struct bgp_path_info *path)
{
	struct bgp_adj_out *adj;
	struct bgp_advertise *adv;
	struct peer *peer;
	afi_t afi;
	safi_t safi;
	struct peer *adv_peer;
	struct peer_af *paf;
	struct bgp *bgp;
	struct attr *attr_new;
	bool debug;

	peer = SUBGRP_PEER(subgrp);
	afi = SUBGRP_AFI(subgrp);
	safi = SUBGRP_SAFI(subgrp);
	bgp = SUBGRP_INST(subgrp);

	if (DISABLE_BGP_ANNOUNCE)
		return false;

	debug = BGP_DEBUG(update, UPDATE_OUT);

	/* Look for adjacency information. */
	adj = adj_lookup(
		dest, subgrp,
		bgp_addpath_id_for_peer(peer, afi, safi, &path->tx_addpath));

	if (adj) {
		if (CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_TABLE_REPARSING))
			subgrp->pscount++;
	} else {
		adj = bgp_adj_out_alloc(
			subgrp, dest,
			bgp_addpath_id_for_peer(peer, afi, safi,
						&path->tx_addpath));
		if (!adj)
			return false;

		subgrp->pscount++;
	}

	/* Check if we are sending the same route. This is needed to
	 * avoid duplicate UPDATES. For instance, filtering communities
	 * at egress, neighbors will see duplicate UPDATES despite
	 * the route wasn't changed actually.
	 * Do not suppress BGP UPDATES for route-refresh.
	 *
	 * It's a duplicate UPDATE if the new attribute is the same as
	 * the one that was sent out, or the one that has been queued.
	 */
	attr_new = bgp_attr_intern(attr);

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_SUPPRESS_DUPLICATES) &&
	    !CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES) &&
	    ((attr_new == adj->attr) ||
	     (adj->adv && adj->adv->baa && (attr_new == adj->adv->baa->attr))) &&
	    bgp_labels_cmp(path->extra ? path->extra->labels : NULL, adj->labels)) {
		if (debug) {
			char attr_str[BUFSIZ] = {0};

			bgp_dump_attr(attr, attr_str, sizeof(attr_str));

			zlog_debug("%s suppress UPDATE %pBD w/ attr: %s, afi=%s, safi=%s",
				   peer->host, dest, attr_str, afi2str(afi), safi2str(safi));
		}

		/*
		 * If this is a duplicate of the update that was sent out,
		 * clean up the queued update, regardless whether it is a
		 * withdraw or an advertisement.
		 */
		if (adj->adv && (attr_new == adj->attr)) {
			if (debug) {
				zlog_debug("%s delete queued UPDATE %pBD, afi=%s, safi=%s",
					   peer->host, dest, afi2str(afi), safi2str(safi));
			}

			bgp_advertise_clean_subgroup(subgrp, adj);
		}

		bgp_attr_unintern(&attr_new);

		/*
		 * If BGP is skipping sending this value to it's peers
		 * the version number should be updated just like it
		 * would if it sent the data.  Why?  Because update
		 * groups will not be coalesced until such time that
		 * the version numbers are the same.
		 *
		 * Imagine a scenario with say 2 peers and they come
		 * up and are placed in the same update group.  Then
		 * a new peer comes up a bit later.  Then a prefix is
		 * flapped that we decide for the first 2 peers are
		 * mapped to and we decide not to send the data to
		 * it.  Then unless more network changes happen we
		 * will never be able to coalesce the 3rd peer down
		 */
		subgrp->version = MAX(subgrp->version, dest->version);
		return false;
	}

	if (adj->adv)
		bgp_advertise_clean_subgroup(subgrp, adj);
	adj->adv = bgp_advertise_new();

	adv = adj->adv;
	adv->dest = dest;
	assert(adv->pathi == NULL);
	/* bgp_path_info adj_out reference */
	adv->pathi = bgp_path_info_lock(path);

	adv->baa = bgp_advertise_attr_intern(subgrp->hash, attr_new);
	adv->adj = adj;
	if (path->extra)
		adj->labels = bgp_labels_intern(path->extra->labels);
	else
		adj->labels = NULL;

	/* Add new advertisement to advertisement attribute list. */
	bgp_advertise_add(adv->baa, adv);

	if (debug) {
		zlog_debug("%s queue UPDATE %pBD, afi=%s, safi=%s", peer->host, dest, afi2str(afi),
			   safi2str(safi));
	}

	/*
	 * If the update adv list is empty, trigger the member peers'
	 * mrai timers so the socket writes can happen.
	 */
	if (!bgp_adv_fifo_count(&subgrp->sync->update)) {
		SUBGRP_FOREACH_PEER (subgrp, paf) {
			/* If there are no routes in the withdraw list, set
			 * the flag PEER_STATUS_ADV_DELAY which will allow
			 * more routes to be sent in the update message
			 */
			if (bgp_fibupd_safi(safi) &&
			    BGP_SUPPRESS_FIB_ENABLED(bgp)) {
				adv_peer = PAF_PEER(paf);
				if (!bgp_adv_fifo_count(
						&subgrp->sync->withdraw))
					SET_FLAG(adv_peer->thread_flags,
						 PEER_THREAD_SUBGRP_ADV_DELAY);
				else
					UNSET_FLAG(adv_peer->thread_flags,
						PEER_THREAD_SUBGRP_ADV_DELAY);
			}
			bgp_adjust_routeadv(PAF_PEER(paf));
		}
	}

	bgp_adv_fifo_add_tail(&subgrp->sync->update, adv);

	subgrp->version = MAX(subgrp->version, dest->version);

	return true;
}

/* 【bgp_adj_out_unset_subgroup】决定并排队一次“从本子组撤销某前缀”的操作（核心函数）。
 * 参数 dest：目的前缀；subgrp：目标子组；addpath_tx_id：Add-Path 出向 ID。
 * 返回：无。
 * 调用时机：某前缀对某子组不再可通告（路由撤销/变为不可用/策略变为拒绝）时。
 * 关键点：
 *   - 找不到 adj 说明本就没通告过，直接按版本更新收尾。
 *   - 若已启 default-originate 且该前缀就是默认前缀，则不撤销（避免把 default-originate
 *     产生的默认路由删掉）——直接 return。
 *   - adj->attr 非空（确实发过）时：新建一个撤销类 adv 挂入 sync->withdraw；若 withdraw
 *     FIFO 原本为空则触发成员 MRAI 调整。adj->attr 为空则说明尚无真实通告，直接 adj_free。
 *   - 版本推进与 set 同理，保证最终可合并。 */
void bgp_adj_out_unset_subgroup(struct bgp_dest *dest, struct update_subgroup *subgrp,
				uint32_t addpath_tx_id)
{
	struct bgp_adj_out *adj;
	struct bgp_advertise *adv;
	afi_t afi;
	safi_t safi;
	struct peer *peer;
	struct peer_af *paf;

	if (DISABLE_BGP_ANNOUNCE)
		return;

	/* Lookup existing adjacency */
	adj = adj_lookup(dest, subgrp, addpath_tx_id);
	if (adj != NULL) {
		/* Clean up previous advertisement.  */
		if (adj->adv)
			bgp_advertise_clean_subgroup(subgrp, adj);

		/* If default originate is enabled and the route is default
		 * route, do not send withdraw. This will prevent deletion of
		 * the default route at the peer.
		 */
		if (CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_DEFAULT_ORIGINATE)
		    && is_default_prefix(bgp_dest_get_prefix(dest)))
			return;

		if (adj->attr) {
			/* We need advertisement structure.  */
			adj->adv = bgp_advertise_new();
			adv = adj->adv;
			adv->dest = dest;
			adv->adj = adj;

			/*
			 * If the update withdraw list is empty, trigger the MRAI
			 * timer for socket writes.
			 */
			if (!bgp_adv_fifo_count(&subgrp->sync->withdraw)) {
				SUBGRP_FOREACH_PEER(subgrp, paf)
					bgp_adjust_routeadv(PAF_PEER(paf));
			}

			/* Add to synchronization entry for withdraw
			 * announcement.  */
			bgp_adv_fifo_add_tail(&subgrp->sync->withdraw, adv);

			if (BGP_DEBUG(update, UPDATE_OUT)) {
				peer = SUBGRP_PEER(subgrp);
				afi = SUBGRP_AFI(subgrp);
				safi = SUBGRP_SAFI(subgrp);

				zlog_debug("%s queue UPDATE (withdraw) %pBD, afi=%s, safi=%s",
					   peer->host, dest, afi2str(afi), safi2str(safi));
			}
		} else {
			/* Free allocated information.  */
			adj_free(adj);
		}
		if (!CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_TABLE_REPARSING))
			subgrp->pscount--;
	}

	subgrp->version = MAX(subgrp->version, dest->version);
}

/* 【bgp_adj_out_remove_subgroup】彻底移除某 adj：释放属性引用、清理待发通告、释放 adj 本体。
 * 参数 dest：目的前缀；adj：待移除；subgrp：所属子组。
 * 返回：无。
 * 调用时机：撤销通告并编码完成后（withdraw_packet）、销毁子组时（subgroup_clear_table）。 */
void bgp_adj_out_remove_subgroup(struct bgp_dest *dest, struct bgp_adj_out *adj,
				 struct update_subgroup *subgrp)
{
	if (adj->attr)
		bgp_attr_unintern(&adj->attr);

	if (adj->adv)
		bgp_advertise_clean_subgroup(subgrp, adj);

	adj_free(adj);
}

/*
 * Go through all the routes and clean up the adj/adv structures corresponding
 * to the subgroup.
 */
/* 【subgroup_clear_table】清空子组所有 adj/adv（遍历 adjq 尾队列，逐个移除）。
 * 参数 subgrp：目标子组。
 * 返回：无。
 * 调用时机：update_subgroup_delete 销毁子组时；需整体重置出向状态时。
 * 关键点：用 _SAFE 版本遍历，因为每条移除都会从队列摘下当前节点。 */
void subgroup_clear_table(struct update_subgroup *subgrp)
{
	struct bgp_adj_out *aout, *taout;

	SUBGRP_FOREACH_ADJ_SAFE (subgrp, aout, taout)
		bgp_adj_out_remove_subgroup(aout->dest, aout, subgrp);
}

/*
 * subgroup_announce_table
 */
/* 【subgroup_announce_table】遍历一张 RIB 表，向本子组通告其中所有应通告的前缀（全量/首次通告）。
 * 参数 subgrp：目标子组；table：待遍历表（NULL 则用该子组实例的 rib[afi][safi_rib]）。
 * 返回：无。
 * 调用时机：subgroup_announce_route 对普通 AF 直接调用；对 VPN/ENCAP/EVPN 逐“路由表”
 *   对每个下层表调用。
 * 关键点：
 *   - safi==LABELED_UNICAST 时实际取 UNICAST 的 RIB。
 *   - 非 VPN/ENCAP/EVPN 且配了 default-originate 时，先 subgroup_default_originate。
 *   - 处理每个 dest：addpath 时先 best-selected；再用 bgp_check_selected 过滤应通告路径
 *     （最佳路径或 addpath 需额外通告者），对配了 default-originate 的默认前缀不显式通告。
 *   - 全程置 SUBGRP_STATUS_TABLE_REPARSING 标志（用于 set/unset 里区分“表重解析计数”）。
 *   - 结束后把 subgrp->version 推进到 table->version（保证即使无路由也能与表版本对齐、
 *     便于合并），并触发一次合并检查。 */
void subgroup_announce_table(struct update_subgroup *subgrp,
			     struct bgp_table *table)
{
	struct bgp_dest *dest;
	struct bgp_path_info *ri;
	struct peer *peer;
	afi_t afi;
	safi_t safi;
	safi_t safi_rib;
	bool addpath_capable;

	peer = SUBGRP_PEER(subgrp);
	afi = SUBGRP_AFI(subgrp);
	safi = SUBGRP_SAFI(subgrp);
	addpath_capable = bgp_addpath_encode_tx(peer, afi, safi);

	if (safi == SAFI_LABELED_UNICAST)
		safi_rib = SAFI_UNICAST;
	else
		safi_rib = safi;

	if (!table)
		table = peer->bgp->rib[afi][safi_rib];

	if (safi != SAFI_MPLS_VPN && safi != SAFI_ENCAP && safi != SAFI_EVPN
	    && CHECK_FLAG(peer->af_flags[afi][safi],
			  PEER_FLAG_DEFAULT_ORIGINATE))
		subgroup_default_originate(subgrp, false);

	subgrp->pscount = 0;
	SET_FLAG(subgrp->sflags, SUBGRP_STATUS_TABLE_REPARSING);

	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {

		if (addpath_capable)
			subgrp_announce_addpath_best_selected(dest, subgrp);

		for (ri = bgp_dest_get_bgp_path_info(dest); ri; ri = ri->next) {

			if (!bgp_check_selected(ri, peer, addpath_capable, afi,
						safi_rib))
				continue;

			/* If default originate is enabled for
			 * the peer, do not send explicit
			 * withdraw. This will prevent deletion
			 * of default route advertised through
			 * default originate
			 */
			if (CHECK_FLAG(peer->af_flags[afi][safi],
				       PEER_FLAG_DEFAULT_ORIGINATE) &&
			    is_default_prefix(bgp_dest_get_prefix(dest)))
				break;

			if (CHECK_FLAG(ri->flags, BGP_PATH_SELECTED))
				subgroup_process_announce_selected(
					subgrp, ri, dest, afi, safi_rib,
					bgp_addpath_id_for_peer(
						peer, afi, safi_rib,
						&ri->tx_addpath));
		}
	}
	UNSET_FLAG(subgrp->sflags, SUBGRP_STATUS_TABLE_REPARSING);

	/*
	 * We walked through the whole table -- make sure our version number
	 * is consistent with the one on the table. This should allow
	 * subgroups to merge sooner if a peer comes up when the route node
	 * with the largest version is no longer in the table. This also
	 * covers the pathological case where all routes in the table have
	 * now been deleted.
	 */
	subgrp->version = MAX(subgrp->version, table->version);

	/*
	 * Start a task to merge the subgroup if necessary.
	 */
	update_subgroup_trigger_merge_check(subgrp, 0);
}

/*
 * subgroup_announce_route
 *
 * Refresh all routes out to a subgroup.
 */
/* 【subgroup_announce_route】向本子组重通告全部路由（“刷新”语义的总入口）。
 * 参数 subgrp：目标子组。
 * 返回：无。
 * 调用时机：coalesce 定时器到期、peer_af_announce_route、策略变更后等。
 * 关键点：
 *   - 清 needs-refresh 标志（本次即为刷新）。
 *   - 若子组只有一个 peer 且该 peer 处于 ORF_WAIT_REFRESH（等对端 ROUTE-REFRESH），则暂缓通告
 *     （首次通告要等 ORF/Route-Refresh，RFC2918）。
 *   - 普通 AF 直接 subgroup_announce_table；VPN/ENCAP/EVPN 需逐下层表遍历，无 RIB 则仅清
 *     FORCE_UPDATES 后返回。
 *   - 结束时清 FORCE_UPDATES 标志。 */
void subgroup_announce_route(struct update_subgroup *subgrp)
{
	struct bgp_dest *dest;
	struct bgp_table *table;
	struct peer *onlypeer;
	bool force_update;

	if (update_subgroup_needs_refresh(subgrp)) {
		update_subgroup_set_needs_refresh(subgrp, 0);
	}

	/*
	 * First update is deferred until ORF or ROUTE-REFRESH is received
	 */
	onlypeer = ((SUBGRP_PCOUNT(subgrp) == 1) ? (SUBGRP_PFIRST(subgrp))->peer
						 : NULL);
	if (onlypeer && CHECK_FLAG(onlypeer->af_sflags[SUBGRP_AFI(subgrp)]
						      [SUBGRP_SAFI(subgrp)],
				   PEER_STATUS_ORF_WAIT_REFRESH))
		return;

	force_update = !!CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES);

	if (SUBGRP_SAFI(subgrp) != SAFI_MPLS_VPN
	    && SUBGRP_SAFI(subgrp) != SAFI_ENCAP
	    && SUBGRP_SAFI(subgrp) != SAFI_EVPN)
		subgroup_announce_table(subgrp, NULL);
	else {
		struct bgp_table *rib = update_subgroup_rib(subgrp);

		if (!rib) {
			if (force_update)
				UNSET_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES);
			return;
		}

		for (dest = bgp_table_top(rib); dest; dest = bgp_route_next(dest)) {
			table = bgp_dest_get_bgp_table_info(dest);
			if (!table)
				continue;
			subgroup_announce_table(subgrp, table);
		}
	}

	if (force_update)
		UNSET_FLAG(subgrp->sflags, SUBGRP_STATUS_FORCE_UPDATES);
}

/* 【subgroup_default_originate】处理 `neighbor ... default-originate`：向本子组通告或撤销
 *   默认路由(0.0.0.0/0 或 ::/0)。
 * 参数 subgrp：目标子组；withdraw：true=撤销默认路由，false=通告（可能因 route-map deny 转为撤销）。
 * 返回：无。
 * 调用时机：配置 default-originate 或其 route-map 变化（update_group_policy_update 等）、
 *   subgroup_announce_table 发现配了 default-originate。
 * 关键点：
 *   - 仅 IPv4/IPv6 单播处理；update-delay 期间直接返回。
 *   - 有 default route-map 时，遍历 RIB 用 route_map_apply_ext 找出“匹配且优先级最高”的一条，
 *     用其属性作为默认路由属性(new_attr)；若全部 deny 则退化为撤销。
 *   - 与本地已有默认路由(通过 redistribute/network 学到)的交互：撤销分支里若本地 RIB 还有
 *     默认路由，则改为通告该路由；通告分支里先清掉旧的 default-originate adj 避免重复。
 *   - 置/清 SUBGRP_STATUS_DEFAULT_ORIGINATE 标志，最终调用 subgroup_default_update_packet /
 *     subgroup_default_withdraw_packet 生成报文。
 *   - graceful-shutdown 场景给默认路由附加 GSHUT community。 */
void subgroup_default_originate(struct update_subgroup *subgrp, bool withdraw)
{
	struct bgp *bgp;
	struct attr attr = { 0 };
	struct attr *new_attr = &attr;
	struct aspath *aspath;
	struct prefix p;
	struct peer *from;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	struct peer *peer;
	struct bgp_adj_out *adj;
	route_map_result_t ret = RMAP_DENYMATCH;
	route_map_result_t new_ret = RMAP_DENYMATCH;
	afi_t afi;
	safi_t safi;
	safi_t safi_rib;
	int pref = 65536;
	int new_pref = 0;

	if (!subgrp)
		return;

	peer = SUBGRP_PEER(subgrp);
	afi = SUBGRP_AFI(subgrp);
	safi = SUBGRP_SAFI(subgrp);

	if (!(afi == AFI_IP || afi == AFI_IP6))
		return;

	if (safi == SAFI_LABELED_UNICAST)
		safi_rib = SAFI_UNICAST;
	else
		safi_rib = safi;

	bgp = peer->bgp;
	from = bgp->peer_self;

	if (bgp_update_delay_active(peer->bgp))
		return;

	bgp_attr_default_set(&attr, bgp, BGP_ORIGIN_IGP);

	/* make coverity happy */
	assert(attr.aspath);

	aspath = attr.aspath;

	bgp_attr_set_med(&attr, 0);

	if ((afi == AFI_IP6) || peer_cap_enhe(peer, afi, safi)) {
		/* IPv6 global nexthop must be included. */
		attr.mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL;

		/* If the peer is on shared nextwork and we have link-local
		   nexthop set it. */
		if (peer->shared_network
		    && !IN6_IS_ADDR_UNSPECIFIED(&peer->nexthop.v6_local))
			attr.mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL;
	}

	if (peer->default_rmap[afi][safi].name) {
		struct bgp_path_info tmp_pi = {0};

		tmp_pi.peer = bgp->peer_self;

		SET_FLAG(bgp->peer_self->rmap_type, PEER_RMAP_TYPE_DEFAULT);

		/* Iterate over the RIB to see if we can announce
		 * the default route. We announce the default
		 * route only if route-map has a match.
		 */
		for (dest = bgp_table_top(bgp->rib[afi][safi_rib]); dest;
		     dest = bgp_route_next(dest)) {
			if (!bgp_dest_has_bgp_path_info_data(dest))
				continue;

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				struct attr tmp_attr = attr;

				tmp_pi.attr = &tmp_attr;

				new_ret = route_map_apply_ext(
					peer->default_rmap[afi][safi].map,
					bgp_dest_get_prefix(dest), pi, &tmp_pi,
					&new_pref);

				if (new_ret == RMAP_PERMITMATCH) {
					if (new_pref < pref) {
						pref = new_pref;
						bgp_attr_flush(new_attr);
						new_attr = bgp_attr_intern(
							tmp_pi.attr);
					}
					bgp_attr_flush(tmp_pi.attr);
					subgroup_announce_reset_nhop(
						(peer_cap_enhe(peer, afi, safi)
							 ? AF_INET6
							 : AF_INET),
						new_attr);
					ret = new_ret;
				} else
					bgp_attr_flush(&tmp_attr);
			}
		}
		bgp->peer_self->rmap_type = 0;

		if (ret == RMAP_DENYMATCH) {
			/*
			 * If its a implicit withdraw due to routemap
			 * deny operation need to set the flag back.
			 * This is a conversion of update flow to
			 * withdraw flow.
			 */
			if (!withdraw &&
			    (!CHECK_FLAG(subgrp->sflags,
					 SUBGRP_STATUS_DEFAULT_ORIGINATE)))
				SET_FLAG(subgrp->sflags,
					 SUBGRP_STATUS_DEFAULT_ORIGINATE);
			withdraw = true;
		}
	}

	/* Check if the default route is in local BGP RIB which is
	 * installed through redistribute or network command
	 */
	memset(&p, 0, sizeof(p));
	p.family = afi2family(afi);
	p.prefixlen = 0;
	dest = bgp_safi_node_lookup(bgp->rib[afi][safi_rib], safi_rib, &p,
				    NULL);

	if (withdraw) {
		/* Withdraw the default route advertised using default
		 * originate
		 */
		if (CHECK_FLAG(subgrp->sflags, SUBGRP_STATUS_DEFAULT_ORIGINATE))
			subgroup_default_withdraw_packet(subgrp);
		UNSET_FLAG(subgrp->sflags, SUBGRP_STATUS_DEFAULT_ORIGINATE);

		/* If default route is present in the local RIB, advertise the
		 * route
		 */
		if (dest) {
			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				if (!CHECK_FLAG(pi->flags, BGP_PATH_SELECTED))
					continue;

				if (subgroup_announce_check(dest, pi, subgrp,
							    bgp_dest_get_prefix(
								    dest),
							    &attr, NULL)) {
					if (!bgp_adj_out_set_subgroup(dest,
								      subgrp,
								      &attr, pi))
						bgp_attr_flush(&attr);
				} else
					bgp_attr_flush(&attr);
			}
			bgp_dest_unlock_node(dest);
		}
	} else {
		if (!CHECK_FLAG(subgrp->sflags,
				SUBGRP_STATUS_DEFAULT_ORIGINATE)) {

			/* The 'neighbor x.x.x.x default-originate' default will
			 * act as an
			 * implicit withdraw for any previous UPDATEs sent for
			 * 0.0.0.0/0 so
			 * clear adj_out for the 0.0.0.0/0 prefix in the BGP
			 * table.
			 */
			if (dest) {
				/* Remove the adjacency for the previously
				 * advertised default route
				 */
				adj = adj_lookup(
				       dest, subgrp,
				       BGP_ADDPATH_TX_ID_FOR_DEFAULT_ORIGINATE);
				if (adj != NULL) {
					/* Clean up previous advertisement.  */
					if (adj->adv)
						bgp_advertise_clean_subgroup(
							subgrp, adj);

					/* Free allocated information.  */
					adj_free(adj);
				}
				bgp_dest_unlock_node(dest);
			}

			/* Advertise the default route */
			if (bgp_in_graceful_shutdown(bgp) ||
			    (CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_SHUTDOWN)))
				bgp_attr_add_gshut_community(new_attr);

			SET_FLAG(subgrp->sflags,
				 SUBGRP_STATUS_DEFAULT_ORIGINATE);
			subgroup_default_update_packet(subgrp, new_attr, from);
		}
	}

	aspath_unintern(&aspath);
}

/*
 * Announce the BGP table to a subgroup.
 *
 * At startup, we try to optimize route announcement by coalescing the
 * peer-up events. This is done only the first time - from then on,
 * subgrp->v_coalesce will be set to zero and the normal logic
 * prevails.
 */
/* 【subgroup_announce_all】向子组通告 BGP 全表；带“启动优化”的合并延迟。
 * 参数 subgrp：目标子组。
 * 返回：无。
 * 调用时机：bootstrap/peer 首次建立、bgp_announce 系列。
 * 关键点：v_coalesce 非 0（首次）时先 arm coalesce 定时器等一会儿（把更多 peer 聚进来共享
 *   通告计算）；v_coalesce 为 0（后续）则立即 subgroup_announce_route。 */
void subgroup_announce_all(struct update_subgroup *subgrp)
{
	if (!subgrp)
		return;

	/*
	 * If coalesce timer value is not set, announce routes immediately.
	 */
	if (!subgrp->v_coalesce) {
		if (bgp_debug_update(NULL, NULL, subgrp->update_group, 0))
			zlog_debug("u%" PRIu64 ":s%" PRIu64" announcing all routes",
				   subgrp->update_group->id, subgrp->id);

		frrtrace(2, frr_bgp, upd_announce_all_routes, subgrp->update_group->id, subgrp->id);

		subgroup_announce_route(subgrp);
		return;
	}

	/*
	 * We should wait for the coalesce timer. Arm the timer if not done.
	 */
	if (!subgrp->t_coalesce) {
		event_add_timer_msec(bm->master, subgroup_coalesce_timer,
				     subgrp, subgrp->v_coalesce,
				     &subgrp->t_coalesce);
	}
}

/*
 * Go through all update subgroups and set up the adv queue for the
 * input route.
 */
/* 【group_announce_route】单条路由变化的对外入口：把 (dest, pi) 打包进 ctx，遍历该
 *   (afi,safi) 的所有 update_group 分发（触发通告或撤销）。
 * 参数 bgp：实例；afi/safi：地址族；dest：目的前缀；pi：新的最佳路径(或 NULL 表示撤销)。
 * 返回：无。
 * 调用时机：bgp_route.c 选路完成后（bgp_process 的 update 阶段）。
 * 关键点：若启用了 suppress-fib，则要等 FIB 状态确认后才通告，此时先返回。 */
void group_announce_route(struct bgp *bgp, afi_t afi, safi_t safi,
			  struct bgp_dest *dest, struct bgp_path_info *pi)
{
	struct updwalk_context ctx;
	ctx.pi = pi;
	ctx.dest = dest;

	/* If suppress fib is enabled, the route will be advertised when
	 * FIB status is received
	 */
	if (!bgp_check_advertise(bgp, dest, safi))
		return;

	update_group_af_walk(bgp, afi, safi, group_announce_route_walkcb, &ctx);
}

/* 【update_group_show_adj_queue】show 待发队列(adv-queue)：以 UPDWALK_FLAGS_ADVQUEUE 调 updgrp_show_adj。 */
void update_group_show_adj_queue(struct bgp *bgp, afi_t afi, safi_t safi,
				 struct vty *vty, uint64_t id)
{
	updgrp_show_adj(bgp, afi, safi, vty, id, UPDWALK_FLAGS_ADVQUEUE);
}

/* 【update_group_show_advertised】show 已通告内容(advertised)：以 UPDWALK_FLAGS_ADVERTISED 调 updgrp_show_adj。 */
void update_group_show_advertised(struct bgp *bgp, afi_t afi, safi_t safi,
				  struct vty *vty, uint64_t id)
{
	updgrp_show_adj(bgp, afi, safi, vty, id, UPDWALK_FLAGS_ADVERTISED);
}

/* 【update_group_announce】对所有组强制重通告（遍历调 update_group_announce_walkcb）。 */
void update_group_announce(struct bgp *bgp)
{
	update_group_walk(bgp, update_group_announce_walkcb, NULL);
}

/* 【update_group_announce_rrclients】仅对 RR 客户端组重通告（遍历调 update_group_announce_rrc_walkcb）。 */
void update_group_announce_rrclients(struct bgp *bgp)
{
	update_group_walk(bgp, update_group_announce_rrc_walkcb, NULL);
}
