// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP flap dampening
 * Copyright (C) 2001 IP Infusion Inc.
 */

/* ============================================================================
 * 【文件导读】bgp_damp.c —— BGP 路由抖动抑制（flap dampening）的实现
 * ============================================================================
 * 【它是什么】bgp_damp.h 里接口的实现。原理、术语与两个核心结构见头文件导读：
 * 给每条路由维护 penalty（惩罚分），超阈值即抑制(suppress)，衰减到阈值以下再复用。
 *
 * 【复用链表是本文件的骨架】被抑制的路由按“预计可复用时间”散列到 reuse_list 数组
 * 的某个桶（桶下标由 penalty 经 reuse_index 表算出）。定时器 bgp_reuse_timer 每
 * DELTA_REUSE 秒只处理当前桶（reuse_offset 轮转），把“遍历全部被抑制路由”的开销
 * 摊薄成每次只处理一个桶——这是理解整套流程的关键。尚未能入桶（或未抑制）的挂在
 * no_reuse_list。下面那些 reuselist 的 add/del/switch 与 info 的 claim/unclaim 就是在
 * 这些桶之间搬运条目。
 *
 * 【配置挂载点优先级】get_active_bdc_from_pi 决定某条路由用哪套参数：先看 peer 自身
 * 的 PEER_FLAG_CONFIG_DAMPENING，再看所属 peer-group，最后看 bgp 实例的
 * BGP_CONFIG_DAMPENING（neighbor 覆盖 bgp）。
 *
 * 【三条主路径（按调用时机看最清楚）】
 *   路由 down ：bgp_damp_withdraw —— 加惩罚分、必要时进入抑制、挂到复用桶。
 *   路由 up   ：bgp_damp_update   —— 衰减后若已低于复用阈值则解除抑制。
 *   到期检查 ：bgp_reuse_timer    —— 逐桶衰减，够低者复用并发更新，否则改桶重挂。
 *
 * 【状态标志】被抑制的路由带 BGP_PATH_DAMPED；同时保持历史(history)态的路由带
 * BGP_PATH_HISTORY。清空抑制时二者都要清，注意别只清一个。
 *
 * 【阅读顺序】reuselist 辅助 → claim/unclaim → get_active_bdc → 索引与入/出桶 →
 * decay → timer → withdraw/update/free → 参数计算与 enable/disable → 各 show/write
 * → peer 级版本。
 * ============================================================================
 */

#include <zebra.h>
#include <math.h>

#include "prefix.h"
#include "memory.h"
#include "command.h"
#include "log.h"
#include "frrevent.h"
#include "queue.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_damp.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_vty.h"

/* 【函数】把一条抑制记录插入链表头部（SLIST_INSERT_HEAD）。纯链表操作，不涉及
 * config/index 维护，调用方需自行保证二者一致。 */
static void bgp_reuselist_add(struct reuselist *list, struct bgp_damp_info *info)
{
	assert(info);
	SLIST_INSERT_HEAD(list, info, entry);
}

/* 【函数】从链表摘除一条抑制记录（SLIST_REMOVE）。纯链表操作。 */
static void bgp_reuselist_del(struct reuselist *list, struct bgp_damp_info *info)
{
	assert(info);
	SLIST_REMOVE(list, info, bgp_damp_info, entry);
}

/* 【函数】把条目从 source 链表移到 target 链表头部。用于复用定时器里“重新定桶”：
 * 一条路由 penalty 变了，需要从旧桶挪到新桶。 */
static void bgp_reuselist_switch(struct reuselist *source,
				 struct bgp_damp_info *info,
				 struct reuselist *target)
{
	assert(source && target && info);
	SLIST_REMOVE(source, info, bgp_damp_info, entry);
	SLIST_INSERT_HEAD(target, info, entry);
}

/* 【函数】把抑制记录从它当前所在的桶摘除，并把 config 置 NULL（表示当前未挂靠任何
 * 配置）。index 为哨兵值时从 no_reuse_list 摘；否则从 reuse_list[bdi->index] 摘
 * （list 非空时用传入的 list，避免重复计算桶地址）。 */
static void bgp_damp_info_unclaim(struct bgp_damp_info *bdi,
				  struct reuselist *list)
{
	assert(bdi && bdi->config);
	if (bdi->index == BGP_DAMP_NO_REUSE_LIST_INDEX)
		bgp_reuselist_del(&bdi->config->no_reuse_list, bdi);
	else
		bgp_reuselist_del(list ? list
				       : &bdi->config->reuse_list[bdi->index],
				  bdi);
	bdi->config = NULL;
}

/* 【函数】把抑制记录挂靠到配置 bdc：原本无 config 时直接指过去；原本已挂在别的配置
 * 上（配置被切换，如参数改动）则先 unclaim 再改挂，并同步 afi/safi。
 * 注意它只改“归属”，不负责把条目放进新配置的桶——那由调用方后续调用
 * bgp_reuselist_add 完成。 */
static void bgp_damp_info_claim(struct bgp_damp_info *bdi,
				struct bgp_damp_config *bdc)
{
	assert(bdc && bdi);
	if (bdi->config == NULL) {
		bdi->config = bdc;
		return;
	}
	bgp_damp_info_unclaim(bdi, NULL);
	bdi->config = bdc;
	bdi->afi = bdc->afi;
	bdi->safi = bdc->safi;
}

/* 【函数】选出对某条路由生效的 dampening 配置，优先级：peer 自身 →
 * 所属 peer-group → bgp 实例。pi 为 NULL 返回 NULL；三处均未启用则返回 NULL
 * （表示该路由不受抑制）。判断 peer-group 前先 peer_group_active。 */
struct bgp_damp_config *get_active_bdc_from_pi(struct bgp_path_info *pi,
					       afi_t afi, safi_t safi)
{
	if (!pi)
		return NULL;
	if (CHECK_FLAG(pi->peer->af_flags[afi][safi],
		       PEER_FLAG_CONFIG_DAMPENING))
		return &pi->peer->damp[afi][safi];
	if (peer_group_active(pi->peer))
		if (CHECK_FLAG(pi->peer->group->conf->af_flags[afi][safi],
			       PEER_FLAG_CONFIG_DAMPENING))
			return &pi->peer->group->conf->damp[afi][safi];
	if (CHECK_FLAG(pi->peer->bgp->af_flags[afi][safi], BGP_CONFIG_DAMPENING))
		return &pi->peer->bgp->damp[afi][safi];
	return NULL;
}

/* 【函数】由 penalty 算出应挂在哪个复用桶：先按 (penalty/reuse_limit - 1)*scale_factor
 * 查 reuse_index 表得到“距当前偏移的桶数”，再加 reuse_offset 取模得到实际桶下标。
 * 越界时截到最后一个索引。等价于把 penalty 映射成“还要多久可复用”的时间分档。
 * reuse_limit 已断言非 0（防除零）。 */
/* Calculate reuse list index by penalty value.  */
static int bgp_reuse_index(int penalty, struct bgp_damp_config *bdc)
{
	unsigned int i;
	unsigned int index;

	/*
	 * reuse_limit can't be zero, this is for Coverity
	 * to bypass division by zero test.
	 */
	assert(bdc->reuse_limit);

	i = (int)(((double)penalty / bdc->reuse_limit - 1.0)
		  * bdc->scale_factor);

	if (i >= bdc->reuse_index_size)
		i = bdc->reuse_index_size - 1;

	index = bdc->reuse_index[i] - bdc->reuse_index[0];

	return (bdc->reuse_offset + index) % bdc->reuse_list_size;
}

/* 【函数】把抑制记录加入合适的复用桶：先 claim 到 bdc，据 penalty 算桶下标，再插入
 * 该桶链表。 */
/* Add BGP dampening information to reuse list.  */
static void bgp_reuse_list_add(struct bgp_damp_info *bdi,
			       struct bgp_damp_config *bdc)
{
	bgp_damp_info_claim(bdi, bdc);
	bdi->index = bgp_reuse_index(bdi->penalty, bdc);
	bgp_reuselist_add(&bdc->reuse_list[bdi->index], bdi);
}

/* 【函数】把抑制记录从复用桶里删除（即 unclaim）。 */
/* Delete BGP dampening information from reuse list.  */
static void bgp_reuse_list_delete(struct bgp_damp_info *bdi)
{
	bgp_damp_info_unclaim(bdi, NULL);
}

/* 【函数】把抑制记录放入 no_reuse_list（未抑制或暂不入桶的条目），并把 index 置为
 * 哨兵 BGP_DAMP_NO_REUSE_LIST_INDEX。 */
static void bgp_no_reuse_list_add(struct bgp_damp_info *bdi,
				  struct bgp_damp_config *bdc)
{
	bgp_damp_info_claim(bdi, bdc);
	bdi->index = BGP_DAMP_NO_REUSE_LIST_INDEX;
	bgp_reuselist_add(&bdc->no_reuse_list, bdi);
}

/* 【函数】把抑制记录从 no_reuse_list 删除（即 unclaim）。 */
static void bgp_no_reuse_list_delete(struct bgp_damp_info *bdi)
{
	bgp_damp_info_unclaim(bdi, NULL);
}

/* 【函数】按经过的时间 tdiff（秒）衰减 penalty 并返回新值：用预计算的 decay_array
 * 查表——tdiff 除以刻度 DELTA_T 得下标 i，返回 penalty*decay_array[i]。
 * tdiff 不足一个刻度(i==0)原值返回；下标超出数组上限（已超过最大抑制时间）返回 0。 */
/* Return decayed penalty value.  */
int bgp_damp_decay(time_t tdiff, int penalty, struct bgp_damp_config *bdc)
{
	unsigned int i;

	i = (int)((double)tdiff / DELTA_T);

	if (i == 0)
		return penalty;

	if (i >= bdc->decay_array_size)
		return 0;

	return (int)(penalty * bdc->decay_array[i]);
}

/* 【函数】复用定时器回调（RFC2439 4.8.7）：每 DELTA_REUSE 秒跑一次，每次只处理当前
 * reuse_offset 指向的桶，随后偏移加一对桶数取模（轮转）。先把 t_reuse 置 NULL 并重挂
 * 定时器（保持周期）。对桶内每条记录：
 *   1) 按 t_now - t_updated 衰减 penalty 并刷新 t_updated；
 *   2) 若 penalty < reuse_limit：解除抑制（清 BGP_PATH_DAMPED）；若上次是 UPDATE 记录
 *      还要把路由重新纳入聚合并 bgp_process 触发更新；penalty 已低于阈值一半则直接
 *      bgp_damp_info_free，否则转入 no_reuse_list 等彻底衰减；
 *   3) 否则 penalty 仍高，按新值重算桶下标并移动到新桶。
 * 处理完断言临时桶 plist 已空（证明每条都被正确搬走）。 */
/* Handler of reuse timer event.  Each route in the current reuse-list
   is evaluated.  RFC2439 Section 4.8.7.  */
static void bgp_reuse_timer(struct event *t)
{
	struct bgp_damp_info *bdi, *bdi_next;
	struct reuselist plist;
	struct bgp *bgp;
	time_t t_now, t_diff;
	struct bgp_damp_config *bdc = EVENT_ARG(t);

	bdc->t_reuse = NULL;
	event_add_timer(bm->master, bgp_reuse_timer, bdc, DELTA_REUSE,
			&bdc->t_reuse);

	t_now = monotime(NULL);

	/* 1.  save a pointer to the current queue head and zero the list head
	 * list head entry. */
	assert(bdc->reuse_offset < bdc->reuse_list_size);
	plist = bdc->reuse_list[bdc->reuse_offset];
	SLIST_INIT(&bdc->reuse_list[bdc->reuse_offset]);

	/* 2.  set offset = modulo reuse-list-size ( offset + 1 ), thereby
	   rotating the circular queue of list-heads.  */
	bdc->reuse_offset = (bdc->reuse_offset + 1) % bdc->reuse_list_size;
	assert(bdc->reuse_offset < bdc->reuse_list_size);

	/* 3. if ( the saved list head pointer is non-empty ) */
	SLIST_FOREACH_SAFE (bdi, &plist, entry, bdi_next) {
		bgp = bdi->path->peer->bgp;

		/* Set t-diff = t-now - t-updated.  */
		t_diff = t_now - bdi->t_updated;

		/* Set figure-of-merit = figure-of-merit * decay-array-ok
		 * [t-diff] */
		bdi->penalty = bgp_damp_decay(t_diff, bdi->penalty, bdc);

		/* Set t-updated = t-now.  */
		bdi->t_updated = t_now;

		/* if (figure-of-merit < reuse).  */
		if (bdi->penalty < bdc->reuse_limit) {
			/* Reuse the route.  */
			bgp_path_info_unset_flag(bdi->dest, bdi->path,
						 BGP_PATH_DAMPED);
			bdi->suppress_time = 0;

			if (bdi->lastrecord == BGP_RECORD_UPDATE) {
				bgp_path_info_unset_flag(bdi->dest, bdi->path,
							 BGP_PATH_HISTORY);
				bgp_aggregate_increment(
					bgp, bgp_dest_get_prefix(bdi->dest),
					bdi->path, bdi->afi, bdi->safi);
				bgp_process(bgp, bdi->dest, bdi->path, bdi->afi,
					    bdi->safi);
			}

			if (bdi->penalty <= bdc->reuse_limit / 2.0) {
				bgp_damp_info_free(bdi, &plist, 1);
			} else {
				bdi->index = BGP_DAMP_NO_REUSE_LIST_INDEX;
				bgp_reuselist_switch(&plist, bdi,
						     &bdc->no_reuse_list);
			}
		} else {
			/* Re-insert into another list (See RFC2439 Section
			 * 4.8.6).  */
			bdi->index = bgp_reuse_index(bdi->penalty, bdc);
			bgp_reuselist_switch(&plist, bdi,
					     &bdc->reuse_list[bdi->index]);
		}
	}

	assert(SLIST_EMPTY(&plist));
}

/* 【函数】路由变为不可达（down）时调用（RFC2439 4.8.2），是“加惩罚分”的主入口。
 * 先取生效配置，未配置则返回 BGP_DAMP_USED。
 *   - 无历史记录：新建 bgp_damp_info，初始 penalty = DEFAULT_PENALTY（若仅属性变化
 *     attr_change 则减半），flap=1，挂到 no_reuse_list。
 *   - 已有记录：若配置变了先 claim 到新配置并重挂；衰减后累加 penalty，封顶到 ceiling，
 *     flap 自增。
 * 随后置 lastrecord=WITHDRAW、标历史态；若已在抑制中则按需重挂桶并返回
 * BGP_DAMP_SUPPRESSED；否则当 penalty 达到 suppress 阈值就进入抑制（标 BGP_PATH_DAMPED、
 * 记 suppress_time、从 no_reuse_list 转入复用桶）。
 * 返回状态宏（USED / SUPPRESSED），调用方据此决定是否真的撤回通告。 */
/* A route becomes unreachable (RFC2439 Section 4.8.2).  */
int bgp_damp_withdraw(struct bgp_path_info *path, struct bgp_dest *dest,
		      afi_t afi, safi_t safi, int attr_change)
{
	time_t t_now;
	struct bgp_damp_info *bdi = NULL;
	unsigned int last_penalty = 0;
	struct bgp_damp_config *bdc;

	bdc = get_active_bdc_from_pi(path, afi, safi);
	if (!bdc)
		return BGP_DAMP_USED;

	t_now = monotime(NULL);
	/* Processing Unreachable Messages.  */
	if (path->extra)
		bdi = path->extra->damp_info;

	if (bdi == NULL) {
		/* If there is no previous stability history. */

		/* RFC2439 said:
		   1. allocate a damping structure.
		   2. set figure-of-merit = 1.
		   3. withdraw the route.  */

		bdi = XCALLOC(MTYPE_BGP_DAMP_INFO,
			      sizeof(struct bgp_damp_info));
		bdi->path = path;
		bdi->dest = dest;
		bdi->penalty =
			(attr_change ? DEFAULT_PENALTY / 2 : DEFAULT_PENALTY);
		bdi->flap = 1;
		bdi->start_time = t_now;
		bdi->suppress_time = 0;
		bdi->index = BGP_DAMP_NO_REUSE_LIST_INDEX;
		bdi->afi = afi;
		bdi->safi = safi;
		(bgp_path_info_extra_get(path))->damp_info = bdi;
		bgp_no_reuse_list_add(bdi, bdc);
	} else {
		if (bdi->config != bdc) {
			bgp_damp_info_claim(bdi, bdc);
			if (bdi->index == BGP_DAMP_NO_REUSE_LIST_INDEX)
				bgp_reuselist_add(&bdc->no_reuse_list, bdi);
			else
				bgp_reuselist_add(&bdc->reuse_list[bdi->index],
						  bdi);
		}
		last_penalty = bdi->penalty;

		/* 1. Set t-diff = t-now - t-updated.  */
		bdi->penalty = (bgp_damp_decay(t_now - bdi->t_updated,
					       bdi->penalty, bdc)
				+ (attr_change ? DEFAULT_PENALTY / 2
					       : DEFAULT_PENALTY));

		if (bdi->penalty > bdc->ceiling)
			bdi->penalty = bdc->ceiling;

		bdi->flap++;
	}

	assert((dest == bdi->dest) && (path == bdi->path));

	bdi->lastrecord = BGP_RECORD_WITHDRAW;
	bdi->t_updated = t_now;

	/* Make this route as historical status.  */
	bgp_path_info_set_flag(dest, path, BGP_PATH_HISTORY);

	/* Remove the route from a reuse list if it is on one.  */
	if (CHECK_FLAG(bdi->path->flags, BGP_PATH_DAMPED)) {
		/* If decay rate isn't equal to 0, reinsert brn. */
		if (bdi->penalty != last_penalty) {
			bgp_reuse_list_delete(bdi);
			bgp_reuse_list_add(bdi, bdc);
		}
		return BGP_DAMP_SUPPRESSED;
	}

	/* If not suppressed before, do annonunce this withdraw and
	   insert into reuse_list.  */
	if (bdi->penalty >= bdc->suppress_value) {
		bgp_path_info_set_flag(dest, path, BGP_PATH_DAMPED);
		bdi->suppress_time = t_now;
		bgp_no_reuse_list_delete(bdi);
		bgp_reuse_list_add(bdi, bdc);
	}
	return BGP_DAMP_USED;
}

/* 【函数】路由变为可达（up）时调用：对记录做衰减并判定能否恢复。无 damp_info 直接返回
 * BGP_DAMP_USED。否则衰减 penalty：
 *   未被抑制且已低于 suppress 阈值 → 保持 USED；
 *   曾被抑制且已低于 reuse 阈值 → 解除抑制（清 DAMPED、移入 no_reuse_list、清
 *       suppress_time），返回 USED；
 *   其余 → 仍抑制，返回 BGP_DAMP_SUPPRESSED。
 * 最后：penalty 仍在复用阈值一半以上则更新 t_updated，否则直接 free 掉记录。
 * 同时清除历史态标志。返回状态宏。 */
int bgp_damp_update(struct bgp_path_info *path, struct bgp_dest *dest,
		    afi_t afi, safi_t safi)
{
	time_t t_now;
	struct bgp_damp_info *bdi;
	int status;
	struct bgp_damp_config *bdc;

	bdc = get_active_bdc_from_pi(path, afi, safi);
	assert(bdc);

	if (!path->extra || !((bdi = path->extra->damp_info)))
		return BGP_DAMP_USED;

	t_now = monotime(NULL);
	bgp_path_info_unset_flag(dest, path, BGP_PATH_HISTORY);

	bdi->lastrecord = BGP_RECORD_UPDATE;
	bdi->penalty =
		bgp_damp_decay(t_now - bdi->t_updated, bdi->penalty, bdc);

	if (!CHECK_FLAG(bdi->path->flags, BGP_PATH_DAMPED)
	    && (bdi->penalty < bdc->suppress_value))
		status = BGP_DAMP_USED;
	else if (CHECK_FLAG(bdi->path->flags, BGP_PATH_DAMPED)
		 && (bdi->penalty < bdc->reuse_limit)) {
		bgp_path_info_unset_flag(dest, path, BGP_PATH_DAMPED);
		bgp_reuse_list_delete(bdi);
		bgp_no_reuse_list_add(bdi, bdc);
		bdi->suppress_time = 0;
		status = BGP_DAMP_USED;
	} else
		status = BGP_DAMP_SUPPRESSED;

	if (bdi->penalty > bdc->reuse_limit / 2.0)
		bdi->t_updated = t_now;
	else
		bgp_damp_info_free(bdi, NULL, 0);

	return status;
}

/* 【函数】释放一条抑制记录：先从所在桶摘除（unclaim），清空 path 上的 damp_info 指针
 * 与 HISTORY/DAMPED 标志；若上次记录是 WITHDRAW 且 withdraw 为真，还要从聚合计数里
 * 扣除、把路由标记删除并 bgp_process。最后释放结构体。
 * 前置条件：bdi->path / bdi->dest 等反向指针仍需有效。 */
void bgp_damp_info_free(struct bgp_damp_info *bdi, struct reuselist *list,
			int withdraw)
{
	assert(bdi);

	afi_t afi = bdi->afi;
	safi_t safi = bdi->safi;
	struct bgp_path_info *bpi = bdi->path;
	struct bgp_dest *dest = bdi->dest;
	struct bgp *bgp = bpi->peer->bgp;
	const struct prefix *p = bgp_dest_get_prefix(bdi->dest);

	bgp_damp_info_unclaim(bdi, list);

	bpi->extra->damp_info = NULL;
	bgp_path_info_unset_flag(dest, bpi, BGP_PATH_HISTORY | BGP_PATH_DAMPED);
	if (bdi->lastrecord == BGP_RECORD_WITHDRAW && withdraw) {
		bgp_aggregate_decrement(bgp, p, bpi, afi, SAFI_UNICAST);
		bgp_path_info_mark_for_delete(dest, bpi);
		bgp_process(bgp, dest, bpi, afi, safi);
	}

	XFREE(MTYPE_BGP_DAMP_INFO, bdi);
}

/* 【函数】据可配参数（半衰期、复用/抑制阈值、最大抑制时间）计算并填充整套派生量：
 *   - ceiling：惩罚上限 = reuse_limit * 2^(max_suppress_time / half_life)。
 *   - decay_array：按 DELTA_T 刻度预计算的衰减系数表（array[1] 为一个刻度的衰减比，
 *     其后逐项累乘），使运行时衰减只需查表。
 *   - reuse_list / reuse_list_size：按 max_suppress_time 与 DELTA_REUSE 决定桶数
 *     （上限 REUSE_LIST_SIZE）。
 *   - reuse_index 与 scale_factor：把 penalty 映射到桶下标的索引表。
 * 这几个数组由 bgp_damp_info_clean 统一释放；重复调用会覆盖旧指针，故切换参数前
 * 必须先 clean。 */
static void bgp_damp_parameter_set(time_t hlife, unsigned int reuse,
				   unsigned int sup, time_t maxsup,
				   struct bgp_damp_config *bdc)
{
	double reuse_max_ratio;
	unsigned int i;
	double j;

	bdc->suppress_value = sup;
	bdc->half_life = hlife;
	bdc->reuse_limit = reuse;
	bdc->max_suppress_time = maxsup;

	/* Initialize params per bgp_damp_config. */
	bdc->reuse_index_size = REUSE_ARRAY_SIZE;

	bdc->ceiling = (int)(bdc->reuse_limit
			     * (pow(2, (double)bdc->max_suppress_time
					       / bdc->half_life)));

	/* Decay-array computations */
	bdc->decay_array_size = ceil((double)bdc->max_suppress_time / DELTA_T);
	bdc->decay_array = XMALLOC(MTYPE_BGP_DAMP_ARRAY,
				   sizeof(double) * (bdc->decay_array_size));
	bdc->decay_array[0] = 1.0;
	bdc->decay_array[1] =
		exp((1.0 / ((double)bdc->half_life / DELTA_T)) * log(0.5));

	/* Calculate decay values for all possible times */
	for (i = 2; i < bdc->decay_array_size; i++)
		bdc->decay_array[i] =
			bdc->decay_array[i - 1] * bdc->decay_array[1];

	/* Reuse-list computations */
	i = ceil((double)bdc->max_suppress_time / DELTA_REUSE) + 1;
	if (i > REUSE_LIST_SIZE || i == 0)
		i = REUSE_LIST_SIZE;
	bdc->reuse_list_size = i;

	bdc->reuse_list =
		XCALLOC(MTYPE_BGP_DAMP_ARRAY,
			bdc->reuse_list_size * sizeof(struct reuselist));
	/* Reuse-array computations */
	bdc->reuse_index = XCALLOC(MTYPE_BGP_DAMP_ARRAY,
				   sizeof(int) * bdc->reuse_index_size);

	reuse_max_ratio = (double)bdc->ceiling / bdc->reuse_limit;
	j = (exp((double)bdc->max_suppress_time / bdc->half_life) * log10(2.0));
	if (reuse_max_ratio > j && j != 0)
		reuse_max_ratio = j;

	bdc->scale_factor =
		(double)bdc->reuse_index_size / (reuse_max_ratio - 1);

	for (i = 0; i < bdc->reuse_index_size; i++) {
		bdc->reuse_index[i] =
			(int)(((double)bdc->half_life / DELTA_REUSE)
			      * log10(1.0
				      / (bdc->reuse_limit
					 * (1.0
					    + ((double)i / bdc->scale_factor))))
			      / log10(0.5));
	}
}

/* 【函数】在 bgp 实例级启用 dampening。若已启用且四个参数完全相同则直接返回 0（幂等）；
 * 参数有变则先 disable 再重建。置 BGP_CONFIG_DAMPENING 标志，调 bgp_damp_parameter_set
 * 算派生参数，记录 afi/safi，并注册复用定时器（周期 DELTA_REUSE）。返回 0。 */
int bgp_damp_enable(struct bgp *bgp, afi_t afi, safi_t safi, time_t half,
		    unsigned int reuse, unsigned int suppress, time_t max)
{
	struct bgp_damp_config *bdc = &bgp->damp[afi][safi];

	if (CHECK_FLAG(bgp->af_flags[afi][safi], BGP_CONFIG_DAMPENING)) {
		if (bdc->half_life == half && bdc->reuse_limit == reuse
		    && bdc->suppress_value == suppress
		    && bdc->max_suppress_time == max)
			return 0;
		bgp_damp_disable(bgp, afi, safi);
	}

	SET_FLAG(bgp->af_flags[afi][safi], BGP_CONFIG_DAMPENING);
	bgp_damp_parameter_set(half, reuse, suppress, max, bdc);
	bdc->afi = afi;
	bdc->safi = safi;

	/* Register reuse timer.  */
	event_add_timer(bm->master, bgp_reuse_timer, bdc, DELTA_REUSE,
			&bdc->t_reuse);

	return 0;
}

/* 【函数】清空某配置下的全部抑制信息：遍历每个复用桶与 no_reuse_list，逐条 free
 * （对处于 UPDATE 记录的要先补聚合计数并 bgp_process），随后释放 decay_array、
 * reuse_index、reuse_list 三个数组并清零对应 size，最后取消复用定时器。
 * 这是 disable 与删除实例/邻居时的统一清理路径。 */
/* Clean all the bgp_damp_info stored in reuse_list and no_reuse_list. */
void bgp_damp_info_clean(struct bgp *bgp, struct bgp_damp_config *bdc,
			 afi_t afi, safi_t safi)
{
	struct bgp_damp_info *bdi;
	struct reuselist *list;
	unsigned int i;

	bdc->reuse_offset = 0;
	for (i = 0; i < bdc->reuse_list_size; ++i) {
		list = &bdc->reuse_list[i];
		while ((bdi = SLIST_FIRST(list)) != NULL) {
			if (bdi->lastrecord == BGP_RECORD_UPDATE) {
				bgp_aggregate_increment(bgp,
							bgp_dest_get_prefix(
								bdi->dest),
							bdi->path, bdi->afi,
							bdi->safi);
				bgp_process(bgp, bdi->dest, bdi->path, bdi->afi,
					    bdi->safi);
			}
			bgp_damp_info_free(bdi, list, 1);
		}
	}

	while ((bdi = SLIST_FIRST(&bdc->no_reuse_list)) != NULL)
		bgp_damp_info_free(bdi, &bdc->no_reuse_list, 1);

	/* Free decay array */
	XFREE(MTYPE_BGP_DAMP_ARRAY, bdc->decay_array);
	bdc->decay_array_size = 0;

	/* Free reuse index array */
	XFREE(MTYPE_BGP_DAMP_ARRAY, bdc->reuse_index);
	bdc->reuse_index_size = 0;

	XFREE(MTYPE_BGP_DAMP_ARRAY, bdc->reuse_list);
	bdc->reuse_list_size = 0;

	event_cancel(&bdc->t_reuse);
}

/* 【函数】在 bgp 实例级关闭 dampening：若未启用则直接返回 0；否则取消复用定时器、
 * 调 bgp_damp_info_clean 清理全部记录，并清 BGP_CONFIG_DAMPENING 标志。返回 0。
 * 注：删除 bgp 实例时也走这里释放内存。 */
/* Disable route flap dampening for a bgp instance.
 *
 * Please note that this function also gets used to free memory when deleting a
 * bgp instance.
 */
int bgp_damp_disable(struct bgp *bgp, afi_t afi, safi_t safi)
{
	struct bgp_damp_config *bdc;

	bdc = &bgp->damp[afi][safi];
	if (!bdc)
		return 0;

	/* If it wasn't enabled, there's nothing to do. */
	if (!CHECK_FLAG(bgp->af_flags[afi][safi], BGP_CONFIG_DAMPENING))
		return 0;

	/* Cancel reuse event. */
	event_cancel(&bdc->t_reuse);

	/* Clean BGP dampening information.  */
	bgp_damp_info_clean(bgp, bdc, afi, safi);

	UNSET_FLAG(bgp->af_flags[afi][safi], BGP_CONFIG_DAMPENING);

	return 0;
}

/* 【函数】把实例级 dampening 配置写回 running-config。全为默认值时写
 * `bgp dampening`；仅半衰期非默认写带一个参数的形式；否则写全部四个参数。
 * 注意单位换算：半衰期与最大抑制时间对外以分钟显示（内部以秒存储）。 */
void bgp_config_write_damp(struct vty *vty, struct bgp *bgp, afi_t afi,
			   safi_t safi)
{
	struct bgp_damp_config *bdc;

	bdc = &bgp->damp[afi][safi];
	if (bdc->half_life == DEFAULT_HALF_LIFE * 60 &&
	    bdc->reuse_limit == DEFAULT_REUSE &&
	    bdc->suppress_value == DEFAULT_SUPPRESS &&
	    bdc->max_suppress_time == bdc->half_life * 4)
		vty_out(vty, "  bgp dampening\n");
	else if (bdc->half_life != DEFAULT_HALF_LIFE * 60 &&
		 bdc->reuse_limit == DEFAULT_REUSE &&
		 bdc->suppress_value == DEFAULT_SUPPRESS &&
		 bdc->max_suppress_time == bdc->half_life * 4)
		vty_out(vty, "  bgp dampening %lld\n", bdc->half_life / 60LL);
	else
		vty_out(vty, "  bgp dampening %lld %d %d %lld\n",
			bdc->half_life / 60LL, bdc->reuse_limit,
			bdc->suppress_value, bdc->max_suppress_time / 60LL);
}

/* 【函数】据当前 penalty 估算“还需多久衰减到 reuse 阈值以下”，返回格式化字符串
 * （或写入 json）。penalty 已不高于 reuse_limit 时时间为 0（输出 00:00:00 / 0）。
 * 按结果时长分档格式化：时分秒 / hh:mm:ss / Ndhhmm / wwdhhh；use_json 为真时把毫秒
 * 数放入 json 字段 reuseTimerMsecs。返回 buf（json 模式下也返回 buf）。 */
static const char *bgp_get_reuse_time(struct bgp_damp_config *bdc,
				      unsigned int penalty, char *buf,
				      size_t len, bool use_json,
				      json_object *json)
{
	time_t reuse_time = 0;
	struct tm tm;
	int time_store = 0;

	if (penalty > bdc->reuse_limit) {
		reuse_time = (int)(DELTA_T *
				   ((log((double)bdc->reuse_limit / penalty)) /
				    (log(bdc->decay_array[1]))));

		if (reuse_time > bdc->max_suppress_time)
			reuse_time = bdc->max_suppress_time;

		gmtime_r(&reuse_time, &tm);
	} else
		reuse_time = 0;

	/* Making formatted timer strings. */
	if (reuse_time == 0) {
		if (use_json)
			json_object_int_add(json, "reuseTimerMsecs", 0);
		else
			snprintf(buf, len, "00:00:00");
	} else if (reuse_time < ONE_DAY_SECOND) {
		if (use_json) {
			time_store = (3600000 * tm.tm_hour)
				     + (60000 * tm.tm_min)
				     + (1000 * tm.tm_sec);
			json_object_int_add(json, "reuseTimerMsecs",
					    time_store);
		} else
			snprintf(buf, len, "%02d:%02d:%02d", tm.tm_hour,
				 tm.tm_min, tm.tm_sec);
	} else if (reuse_time < ONE_WEEK_SECOND) {
		if (use_json) {
			time_store = (86400000 * tm.tm_yday)
				     + (3600000 * tm.tm_hour)
				     + (60000 * tm.tm_min)
				     + (1000 * tm.tm_sec);
			json_object_int_add(json, "reuseTimerMsecs",
					    time_store);
		} else
			snprintf(buf, len, "%dd%02dh%02dm", tm.tm_yday,
				 tm.tm_hour, tm.tm_min);
	} else {
		if (use_json) {
			time_store =
				(604800000 * tm.tm_yday / 7)
				+ (86400000
				   * (tm.tm_yday - ((tm.tm_yday / 7) * 7)))
				+ (3600000 * tm.tm_hour) + (60000 * tm.tm_min)
				+ (1000 * tm.tm_sec);
			json_object_int_add(json, "reuseTimerMsecs",
					    time_store);
		} else
			snprintf(buf, len, "%02dw%dd%02dh", tm.tm_yday / 7,
				 tm.tm_yday - ((tm.tm_yday / 7) * 7),
				 tm.tm_hour);
	}

	return buf;
}

/* 【函数】在 show 输出里打印某条路由的 dampening 信息：重算当前 penalty，输出 penalty
 * 与抖动次数及起始时长；若正被抑制且不处于历史态，再附“reuse in ...”。json_path
 * 非空时改写为 json 字段。path->extra 为空、未启用或无记录时直接返回。 */
void bgp_damp_info_vty(struct vty *vty, struct bgp *bgp,
		       struct bgp_path_info *path, afi_t afi, safi_t safi,
		       json_object *json_path)
{
	struct bgp_damp_info *bdi;
	time_t t_now, t_diff;
	char timebuf[BGP_UPTIME_LEN] = {};
	int penalty;
	struct bgp_damp_config *bdc = &bgp->damp[afi][safi];

	if (!path->extra)
		return;

	/* BGP dampening information.  */
	bdi = path->extra->damp_info;

	/* If dampening is not enabled or there is no dampening information,
	   return immediately.  */
	if (!CHECK_FLAG(path->peer->bgp->af_flags[afi][safi],
			BGP_CONFIG_DAMPENING) ||
	    !bdi)
		return;

	/* Calculate new penalty.  */
	t_now = monotime(NULL);
	t_diff = t_now - bdi->t_updated;
	penalty = bgp_damp_decay(t_diff, bdi->penalty, bdc);

	if (json_path) {
		json_object_int_add(json_path, "dampeningPenalty", penalty);
		json_object_int_add(json_path, "dampeningFlapCount", bdi->flap);
		peer_uptime(bdi->start_time, timebuf, BGP_UPTIME_LEN, 1,
			    json_path);

		if (CHECK_FLAG(path->flags, BGP_PATH_DAMPED)
		    && !CHECK_FLAG(path->flags, BGP_PATH_HISTORY))
			bgp_get_reuse_time(bdc, penalty, timebuf,
					   BGP_UPTIME_LEN, 1, json_path);
	} else {
		vty_out(vty,
			"      Dampinfo: penalty %d, flapped %d times in %s",
			penalty, bdi->flap,
			peer_uptime(bdi->start_time, timebuf, BGP_UPTIME_LEN, 0,
				    json_path));

		if (CHECK_FLAG(path->flags, BGP_PATH_DAMPED)
		    && !CHECK_FLAG(path->flags, BGP_PATH_HISTORY))
			vty_out(vty, ", reuse in %s",
				bgp_get_reuse_time(bdc, penalty, timebuf,
						   BGP_UPTIME_LEN, 0,
						   json_path));

		vty_out(vty, "\n");
	}
}


/* 【函数】供外部（如 show 的每个前缀行）取该路由的“可复用时间”字符串：取生效配置、
 * 校验使能与记录存在后，重算 penalty 并交给 bgp_get_reuse_time 格式化。
 * 返回 buf 或 NULL（未配置 / 无记录）。 */
const char *bgp_damp_reuse_time_vty(struct vty *vty, struct bgp_path_info *path,
				    char *timebuf, size_t len, afi_t afi,
				    safi_t safi, bool use_json,
				    json_object *json)
{
	struct bgp_damp_info *bdi;
	time_t t_now, t_diff;
	int penalty;
	struct bgp_damp_config *bdc;

	bdc = get_active_bdc_from_pi(path, afi, safi);
	if (!bdc)
		return NULL;

	if (!path->extra)
		return NULL;

	/* BGP dampening information.  */
	bdi = path->extra->damp_info;

	/* If dampening is not enabled or there is no dampening information,
	   return immediately.  */
	if (!CHECK_FLAG(path->peer->bgp->af_flags[afi][safi],
			BGP_CONFIG_DAMPENING) ||
	    !bdi)
		return NULL;

	/* Calculate new penalty.  */
	t_now = monotime(NULL);
	t_diff = t_now - bdi->t_updated;
	penalty = bgp_damp_decay(t_diff, bdi->penalty, bdc);

	return bgp_get_reuse_time(bdc, penalty, timebuf, len, use_json, json);
}


/* 【函数】打印单个 (afi, safi) 的 dampening 参数（半衰期、复用/抑制阈值、最大抑制时间、
 * 最大抑制上限）：use_json 时输出 json，否则按行文本。若该 afi/safi 未启用，非 json
 * 模式打印一行提示。恒返回 CMD_SUCCESS。 */
static int bgp_print_dampening_parameters(struct bgp *bgp, struct vty *vty,
					  afi_t afi, safi_t safi, bool use_json)
{
	if (CHECK_FLAG(bgp->af_flags[afi][safi], BGP_CONFIG_DAMPENING)) {
		struct bgp_damp_config *bdc = &bgp->damp[afi][safi];

		if (use_json) {
			json_object *json = json_object_new_object();

			json_object_int_add(json, "halfLifeSecs",
					    bdc->half_life);
			json_object_int_add(json, "reusePenalty",
					    bdc->reuse_limit);
			json_object_int_add(json, "suppressPenalty",
					    bdc->suppress_value);
			json_object_int_add(json, "maxSuppressTimeSecs",
					    bdc->max_suppress_time);
			json_object_int_add(json, "maxSuppressPenalty",
					    bdc->ceiling);

			vty_json(vty, json);
		} else {
			vty_out(vty, "Half-life time: %lld min\n",
				(long long)bdc->half_life / 60);
			vty_out(vty, "Reuse penalty: %d\n", bdc->reuse_limit);
			vty_out(vty, "Suppress penalty: %d\n",
				bdc->suppress_value);
			vty_out(vty, "Max suppress time: %lld min\n",
				(long long)bdc->max_suppress_time / 60);
			vty_out(vty, "Max suppress penalty: %u\n",
				bdc->ceiling);
			vty_out(vty, "\n");
		}
	} else if (!use_json)
		vty_out(vty, "dampening not enabled for %s\n",
			get_afi_safi_str(afi, safi, false));

	return CMD_SUCCESS;
}

/* 【函数】show 入口：取默认 BGP 实例（无实例或隐藏则警告并返回 CMD_WARNING）。
 * 未指定 afi-all 就只打印传入的 (afi, safi)；否则按 IP / IP6 或全部 AFI/SAFI 逐个
 * 调 bgp_print_dampening_parameters。返回 CMD_SUCCESS / CMD_WARNING。 */
int bgp_show_dampening_parameters(struct vty *vty, afi_t afi, safi_t safi,
				  uint16_t show_flags)
{
	struct bgp *bgp;
	bool use_json = CHECK_FLAG(show_flags, BGP_SHOW_OPT_JSON);

	bgp = bgp_get_default();

	if (bgp == NULL || IS_BGP_INSTANCE_HIDDEN(bgp)) {
		vty_out(vty, "No BGP process is configured\n");
		return CMD_WARNING;
	}

	if (!CHECK_FLAG(show_flags, BGP_SHOW_OPT_AFI_ALL))
		return bgp_print_dampening_parameters(bgp, vty, afi, safi,
						      use_json);

	if (CHECK_FLAG(show_flags, BGP_SHOW_OPT_AFI_IP)
	    || CHECK_FLAG(show_flags, BGP_SHOW_OPT_AFI_IP6)) {
		afi = CHECK_FLAG(show_flags, BGP_SHOW_OPT_AFI_IP) ? AFI_IP
								  : AFI_IP6;
		FOREACH_SAFI (safi) {
			if (strmatch(get_afi_safi_str(afi, safi, true),
				     "Unknown"))
				continue;

			if (!use_json)
				vty_out(vty, "\nFor address family: %s\n\n",
					get_afi_safi_str(afi, safi, false));

			bgp_print_dampening_parameters(bgp, vty, afi, safi,
						       use_json);
		}
	} else {
		FOREACH_AFI_SAFI (afi, safi) {
			if (strmatch(get_afi_safi_str(afi, safi, true),
				     "Unknown"))
				continue;

			if (!use_json)
				vty_out(vty, "\nFor address family: %s\n",
					get_afi_safi_str(afi, safi, false));

			bgp_print_dampening_parameters(bgp, vty, afi, safi,
						       use_json);
		}
	}
	return CMD_SUCCESS;
}

/* 【函数】在 peer（或 peer-group）级启用 dampening：逻辑同实例级（幂等检查 → 参数变则
 * 先 disable → 置 PEER_FLAG_CONFIG_DAMPENING → 算参数 → 注册复用定时器），但作用于
 * peer->damp[afi][safi]。peer 为 NULL 直接返回。本函数返回 void。 */
void bgp_peer_damp_enable(struct peer *peer, afi_t afi, safi_t safi, time_t half,
			  unsigned int reuse, unsigned int suppress, time_t max)
{
	struct bgp_damp_config *bdc;

	if (!peer)
		return;
	bdc = &peer->damp[afi][safi];
	if (peer_af_flag_check(peer, afi, safi, PEER_FLAG_CONFIG_DAMPENING)) {
		if (bdc->half_life == half && bdc->reuse_limit == reuse &&
		    bdc->suppress_value == suppress &&
		    bdc->max_suppress_time == max)
			return;
		bgp_peer_damp_disable(peer, afi, safi);
	}
	SET_FLAG(peer->af_flags[afi][safi], PEER_FLAG_CONFIG_DAMPENING);
	bgp_damp_parameter_set(half, reuse, suppress, max, bdc);
	bdc->afi = afi;
	bdc->safi = safi;
	event_add_timer(bm->master, bgp_reuse_timer, bdc, DELTA_REUSE,
			&bdc->t_reuse);
}

/* 【函数】在 peer 级关闭 dampening：未启用则返回；否则清理该 peer 的记录（调
 * bgp_damp_info_clean）并清 PEER_FLAG_CONFIG_DAMPENING 标志。删除 peer / peer-group 时
 * 也走这里释放内存。 */
/* Disable route flap dampening for a peer.
 *
 * Please note that this function also gets used to free memory when deleting a
 * peer or peer group.
 */
void bgp_peer_damp_disable(struct peer *peer, afi_t afi, safi_t safi)
{
	struct bgp_damp_config *bdc;

	if (!peer_af_flag_check(peer, afi, safi, PEER_FLAG_CONFIG_DAMPENING))
		return;
	bdc = &peer->damp[afi][safi];
	if (!bdc)
		return;
	bgp_damp_info_clean(peer->bgp, bdc, afi, safi);
	UNSET_FLAG(peer->af_flags[afi][safi], PEER_FLAG_CONFIG_DAMPENING);
}

/* 【函数】把 peer 级 dampening 配置写回 running-config，规则同实例级（默认值 / 仅半衰
 * 期 / 全参数三种形式），前缀为 `neighbor X dampening ...`。 */
void bgp_config_write_peer_damp(struct vty *vty, struct peer *peer, afi_t afi,
				safi_t safi)
{
	struct bgp_damp_config *bdc;

	bdc = &peer->damp[afi][safi];
	if (bdc->half_life == DEFAULT_HALF_LIFE * 60 &&
	    bdc->reuse_limit == DEFAULT_REUSE &&
	    bdc->suppress_value == DEFAULT_SUPPRESS &&
	    bdc->max_suppress_time == bdc->half_life * 4)
		vty_out(vty, "  neighbor %s dampening\n", peer->host);
	else if (bdc->half_life != DEFAULT_HALF_LIFE * 60 &&
		 bdc->reuse_limit == DEFAULT_REUSE &&
		 bdc->suppress_value == DEFAULT_SUPPRESS &&
		 bdc->max_suppress_time == bdc->half_life * 4)
		vty_out(vty, "  neighbor %s dampening %lld\n", peer->host,
			bdc->half_life / 60LL);
	else
		vty_out(vty, "  neighbor %s dampening %lld %d %d %lld\n",
			peer->host, bdc->half_life / 60LL, bdc->reuse_limit,
			bdc->suppress_value, bdc->max_suppress_time / 60LL);
}

/* 【函数】打印单个 peer 在某 (afi, safi) 的 dampening 参数（文本，或填充传入的 json）。
 * peer 为 NULL 返回；未启用时非 json 模式打印一行提示。 */
static void bgp_print_peer_dampening_parameters(struct vty *vty,
						struct peer *peer, afi_t afi,
						safi_t safi, bool use_json,
						json_object *json)
{
	struct bgp_damp_config *bdc;

	if (!peer)
		return;
	if (CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_CONFIG_DAMPENING)) {
		bdc = &peer->damp[afi][safi];
		if (!bdc)
			return;
		if (use_json) {
			json_object_int_add(json, "halfLifeSecs",
					    bdc->half_life);
			json_object_int_add(json, "reusePenalty",
					    bdc->reuse_limit);
			json_object_int_add(json, "suppressPenalty",
					    bdc->suppress_value);
			json_object_int_add(json, "maxSuppressTimeSecs",
					    bdc->max_suppress_time);
			json_object_int_add(json, "maxSuppressPenalty",
					    bdc->ceiling);
		} else {
			vty_out(vty, "Half-life time: %lld min\n",
				(long long)bdc->half_life / 60);
			vty_out(vty, "Reuse penalty: %d\n", bdc->reuse_limit);
			vty_out(vty, "Suppress penalty: %d\n",
				bdc->suppress_value);
			vty_out(vty, "Max suppress time: %lld min\n",
				(long long)bdc->max_suppress_time / 60);
			vty_out(vty, "Max suppress penalty: %u\n", bdc->ceiling);
			vty_out(vty, "\n");
		}
	} else if (!use_json)
		vty_out(vty, "neighbor dampening not enabled for %s\n",
			get_afi_safi_str(afi, safi, false));
}

/* 【函数】show 某 peer 的 dampening 参数：use_json 时新建含 addressFamily 的 json 对象，
 * 填充后美化打印再释放；否则打印“For address family”标题后调
 * bgp_print_peer_dampening_parameters。 */
void bgp_show_peer_dampening_parameters(struct vty *vty, struct peer *peer,
					afi_t afi, safi_t safi, bool use_json)
{
	json_object *json;

	if (use_json) {
		json = json_object_new_object();
		json_object_string_add(json, "addressFamily",
				       get_afi_safi_str(afi, safi, false));
		bgp_print_peer_dampening_parameters(vty, peer, afi, safi, true,
						    json);
		vty_out(vty, "%s\n",
			json_object_to_json_string_ext(json,
						       JSON_C_TO_STRING_PRETTY));
		json_object_free(json);
	} else {
		vty_out(vty, "\nFor address family: %s\n",
			get_afi_safi_str(afi, safi, false));
		bgp_print_peer_dampening_parameters(vty, peer, afi, safi, false,
						    NULL);
	}
}
