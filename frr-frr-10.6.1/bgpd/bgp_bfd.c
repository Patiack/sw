// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * bgp_bfd.c: BGP BFD handling routines
 *
 * @copyright Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * 文件导读：BGP 与 BFD（双向转发检测）的集成
 *
 * 本模块把 BGP 邻居会话与 BFD 会话关联起来：当 BFD 检测到链路 / 转发面故障
 * （会话 Down）时，及时触发 BGP 邻居重置，从而把故障收敛时间从 BGP 的
 * Holdtime（秒级）缩短到 BFD 的毫秒级；当 BFD 恢复（会话 Up）时，可主动重新
 * 发起 BGP 连接。
 *
 * 关键数据结构：struct peer 上的 bfd_config（struct bfd_config，含 BFD 会话
 * 句柄 session、探测倍数 / 收发间隔 / profile / cbit、BFD Strict 的 hold_time
 * 及其定时器 t_hold_timer、以及是否手动配置的 manual 标志）。邻居组也持有一份
 * 组级默认配置，组内成员按“是否显式覆盖”继承组配置。
 *
 * 三种配置模型：
 *   - 直接给邻居配 bfd（manual=true）；
 *   - 邻居组配 bfd，成员继承（组配置优先用于未显式配置的项）；
 *   - 通过 profile 引用外部 bfdd 的配置模板。
 *
 * 与相邻模块的配合：由 BGP 状态机（bgp_fsm.c）在建立 / 拆除邻居时调用
 * bgp_peer_configure_bfd / bgp_peer_remove_bfd_config；BFD 会话状态变化经回调
 * bfd_session_status_update 回到本模块；lib 的 bfd.h 提供 bfd_sess_* 接口，
 * 底层由 bfdd 或内核 BFD 实现。
 *
 * 读的顺序：先看状态回调与配置应用 / 源地址更新，再看 reset / configure /
 * remove 这组生命周期函数，然后是配置写出与 show，最后是各 vty 命令与 init。
 */

#include <zebra.h>

#include "command.h"
#include "linklist.h"
#include "memory.h"
#include "prefix.h"
#include "frrevent.h"
#include "buffer.h"
#include "stream.h"
#include "vrf.h"
#include "zclient.h"
#include "bfd.h"
#include "lib/json.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgp_fsm.h"
#include "bgpd/bgp_bfd.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_network.h"
#ifndef VTYSH_EXTRACT_PL
#include "bgpd/bgp_bfd_clippy.c"
#endif

/* 分配类型：每个邻居（或组）的 BFD 配置数据结构。 */
DEFINE_MTYPE_STATIC(BGPD, BFD_CONFIG, "BFD configuration data");

/* BGP 的 zclient，用于与 bfdd 交互（在本文件由 bgp_bfd_init 使用）。 */
extern struct zclient *bgp_zclient;

/*
 * BFD Strict mode 的 Hold 定时器到期回调。被触发说明 BFD 已 Down 且等待
 * hold_time 后仍未恢复，此时把邻居标记为 BFD Down 原因并送 CEASE 通知，
 * 再向状态机投递 BGP_Stop 事件以拆除会话。event 的参数即所属 peer。
 */
/* BFD Strict mode Hold timer expire */
static void bgp_bfd_strict_holdtime_expire(struct event *event)
{
	struct peer *peer = EVENT_ARG(event);
	struct peer_connection *connection = peer->connection;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%pBP BFD Strict mode Hold timer expire for %s", peer,
			   bgp_peer_get_connection_direction_string(connection));

	peer_set_last_reset(peer, PEER_DOWN_BFD_DOWN);
	SET_FLAG(peer->sflags, PEER_STATUS_BFD_STRICT_HOLD_TIME_EXPIRED);

	if (BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status))
		bgp_notify_send(peer->connection, BGP_NOTIFY_CEASE, BGP_NOTIFY_CEASE_BFD_DOWN);

	BGP_EVENT_ADD(peer->connection, BGP_Stop);
}

/*
 * BFD 会话状态变化回调（注册到 bfd_sess_new）。arg 为所属 peer。仅关注 Down：
 * 当从 UP 变为 DOWN 时，若邻居处于 NSF（优雅重启）模式且对端 C 位已清（表示
 * 对端不期望控制面联动），则忽略这次 Down；否则若 BFD Down 事件晚于本 BGP
 * 会话建立时间，则根据是否配置了严格 holdtime 决定：配了则起 hold_time 定时器
 * 延迟拆除（Strict 模式），否则立即标记 BFD Down、送 CEASE 通知并投递 BGP_Stop。
 * 另外当 BFD 从非 UP 变为 UP 且 BGP 尚未 Established 时，若未抑制启动，则刷新
 * 下一跳跟踪并投递 BGP_Start 以主动重建邻居。
 */
static void bfd_session_status_update(struct bfd_session_params *bsp,
				      const struct bfd_session_status *bss,
				      void *arg)
{
	struct peer *peer = arg;

	if (BGP_DEBUG(bfd, BFD_LIB))
		zlog_debug("%s: neighbor %s vrf %s(%u) bfd state %s -> %s",
			   __func__, peer->conf_if ? peer->conf_if : peer->host,
			   bfd_sess_vrf(bsp), bfd_sess_vrf_id(bsp),
			   bfd_get_status_str(bss->previous_state),
			   bfd_get_status_str(bss->state));

	if (bss->state == BSS_DOWN && bss->previous_state == BSS_UP) {
		if (CHECK_FLAG(peer->sflags, PEER_STATUS_NSF_MODE)
		    && bfd_sess_cbit(bsp) && !bss->remote_cbit) {
			if (BGP_DEBUG(bfd, BFD_LIB))
				zlog_debug(
					"%s BFD DOWN message ignored in the process of graceful restart when C bit is cleared",
					peer->host);
			return;
		}

		/* Once the BFD session is UP, and later BGP session is UP,
		 * BFD notices that peer->su_local changed, and BFD session goes down.
		 * We should trigger BGP session reset if BFD session is UP
		 * only when BGP session is UP already.
		 * Otherwise, we end up resetting BGP session when BFD session is UP,
		 * when the source address is changed, e.g. 0.0.0.0 -> 10.0.0.1.
		 */
		if (bss->last_event > peer->uptime) {
			if ((CHECK_FLAG(peer->flags, PEER_FLAG_TIMER) && !peer->holdtime) ||
			    !peer->bgp->default_holdtime) {
				event_add_timer(bm->master, bgp_bfd_strict_holdtime_expire, peer,
						peer->bfd_config->hold_time,
						&peer->bfd_config->t_hold_timer);
			} else {
				peer_set_last_reset(peer, PEER_DOWN_BFD_DOWN);
				/* rfc9384 */
				if (BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status))
					bgp_notify_send(peer->connection, BGP_NOTIFY_CEASE,
							BGP_NOTIFY_CEASE_BFD_DOWN);

				BGP_EVENT_ADD(peer->connection, BGP_Stop);
			}
		}
	}

	if (bss->state == BSS_UP && bss->previous_state != BSS_UP &&
	    !peer_established(peer->connection)) {
		if (!BGP_PEER_START_SUPPRESSED(peer)) {
			bgp_fsm_nht_update(peer->connection, peer, true);
			BGP_EVENT_ADD(peer->connection, BGP_Start);
		}
	}
}

/*
 * 把 BFD 配置应用到 peer p 的 BFD 会话（并在安装前处理组继承）。若 p 是组，
 * 则先按组成员逐一下发 BFD_STRICT 标志，再对每个成员递归调用本函数后返回。
 * 若没有组配置（pg 为空或其 conf 无 bfd_config），直接以 peer 自身的定时器、
 * cbit、profile 安装会话。否则做组继承：对 cbit（默认 CP-independent 时优先
 * 用组值）、profile（peer 未指定则用组）、定时器（peer 用默认值时用组值）逐项
 * 决定取值，并处理 update-source 覆盖，最后安装会话。
 */
void bgp_peer_config_apply(struct peer *p, struct peer_group *pg)
{
	struct listnode *n;
	struct peer *pn;
	struct peer *gconfig;

	/* When called on a group, apply to all peers. */
	if (CHECK_FLAG(p->sflags, PEER_STATUS_GROUP)) {
		for (ALL_LIST_ELEMENTS_RO(p->group->peer, n, pn)) {
			if (peergroup_flag_check(pn, PEER_FLAG_BFD_STRICT))
				peer_flag_set(pn, PEER_FLAG_BFD_STRICT);
			else
				peer_flag_unset(pn, PEER_FLAG_BFD_STRICT);
			bgp_peer_config_apply(pn, pg);
		}
		return;
	}

	/* No group, just use current configuration. */
	if (pg == NULL || pg->conf->bfd_config == NULL) {
		bfd_sess_set_timers(p->bfd_config->session,
				    p->bfd_config->detection_multiplier,
				    p->bfd_config->min_rx,
				    p->bfd_config->min_tx);
		bfd_sess_set_cbit(p->bfd_config->session, p->bfd_config->cbit);
		bfd_sess_set_profile(p->bfd_config->session,
				     p->bfd_config->profile);
		bfd_sess_install(p->bfd_config->session);
		return;
	}

	/*
	 * Check if the group configuration was overwritten or apply group
	 * configuration.
	 */
	gconfig = pg->conf;

	if (CHECK_FLAG(gconfig->flags, PEER_FLAG_UPDATE_SOURCE) ||
	    CHECK_FLAG(p->flags_override, PEER_FLAG_UPDATE_SOURCE))
		bgp_peer_bfd_update_source(p);

	/*
	 * If using default control plane independent configuration,
	 * then prefer group's (e.g. it means it wasn't manually configured).
	 */
	if (!p->bfd_config->cbit)
		bfd_sess_set_cbit(p->bfd_config->session,
				  gconfig->bfd_config->cbit);
	else
		bfd_sess_set_cbit(p->bfd_config->session, p->bfd_config->cbit);

	/* If no profile was specified in peer, then use the group profile. */
	if (p->bfd_config->profile[0] == 0)
		bfd_sess_set_profile(p->bfd_config->session,
				     gconfig->bfd_config->profile);
	else
		bfd_sess_set_profile(p->bfd_config->session,
				     p->bfd_config->profile);

	/* If no specific timers were configured, then use the group timers. */
	if (p->bfd_config->detection_multiplier == BFD_DEF_DETECT_MULT
	    || p->bfd_config->min_rx == BFD_DEF_MIN_RX
	    || p->bfd_config->min_tx == BFD_DEF_MIN_TX)
		bfd_sess_set_timers(p->bfd_config->session,
				    gconfig->bfd_config->detection_multiplier,
				    gconfig->bfd_config->min_rx,
				    gconfig->bfd_config->min_tx);
	else
		bfd_sess_set_timers(p->bfd_config->session,
				    p->bfd_config->detection_multiplier,
				    p->bfd_config->min_rx,
				    p->bfd_config->min_tx);

	bfd_sess_install(p->bfd_config->session);
}

/*
 * 根据当前邻居配置更新其 BFD 会话的源 / 目的地址、接口、TTL（跳数）与 VRF，
 * 有任一项变化则在末尾重新安装会话。源地址取值：配了 update-source 则用
 * p->update_source 或按 update_if 解析出的地址，否则用会话本地地址
 * （su_local）。地址按会话已有地址族（AF_INET / AF_INET6）比较新旧值，不同
 * 才下发。TTL 依据是否多跳调整（非多跳且会话跳数大于 1 则降为 1；多跳且与
 * p->ttl 不同则设为 p->ttl）。BFD 会话所属 VRF 与邻居所在 VRF 不同时同步。
 * 组邻居直接返回；无 bfd_config 也直接返回。
 */
void bgp_peer_bfd_update_source(struct peer *p)
{
	struct bfd_session_params *session;
	const union sockunion *source = NULL;
	bool changed = false;
	int family;
	union {
		struct in_addr v4;
		struct in6_addr v6;
	} src, dst;
	struct interface *ifp;
	union sockunion addr;

	if (!p->bfd_config)
		return;

	session = p->bfd_config->session;
	/* Nothing to do for groups. */
	if (CHECK_FLAG(p->sflags, PEER_STATUS_GROUP))
		return;

	/* Figure out the correct source to use. */
	if (CHECK_FLAG(p->flags, PEER_FLAG_UPDATE_SOURCE)) {
		if (p->update_source) {
			source = p->update_source;
		} else if (p->update_if) {
			ifp = if_lookup_by_name(p->update_if, p->bgp->vrf_id);
			if (ifp) {
				sockunion_init(&addr);
				if (bgp_update_address(ifp, &p->connection->su, &addr)) {
					if (BGP_DEBUG(bfd, BFD_LIB))
						zlog_debug("%s: can't find the source address for interface %s",
							   __func__, p->update_if);
				}

				source = &addr;
			}
		}
	} else {
		source = p->connection->su_local;
	}

	/* Update peer's source/destination addresses. */
	bfd_sess_addresses(session, &family, &src.v6, &dst.v6);
	if (family == AF_INET) {
		if ((source && source->sin.sin_addr.s_addr != src.v4.s_addr) ||
		    p->connection->su.sin.sin_addr.s_addr != dst.v4.s_addr) {
			if (BGP_DEBUG(bfd, BFD_LIB))
				zlog_debug("%s: address [%pI4->%pI4] to [%pI4->%pI4]",
					   __func__, &src.v4, &dst.v4,
					   source ? &source->sin.sin_addr
						  : &src.v4,
					   &p->connection->su.sin.sin_addr);

			bfd_sess_set_ipv4_addrs(session,
						source ? &source->sin.sin_addr
						       : NULL,
						&p->connection->su.sin.sin_addr);
			changed = true;
		}
	} else {
		if ((source && memcmp(&source->sin6, &src.v6, sizeof(src.v6))) ||
		    memcmp(&p->connection->su.sin6, &dst.v6, sizeof(dst.v6))) {
			if (BGP_DEBUG(bfd, BFD_LIB))
				zlog_debug("%s: address [%pI6->%pI6] to [%pI6->%pI6]",
					   __func__, &src.v6, &dst.v6,
					   source ? &source->sin6.sin6_addr
						  : &src.v6,
					   &p->connection->su.sin6.sin6_addr);

			bfd_sess_set_ipv6_addrs(session,
						source ? &source->sin6.sin6_addr
						       : NULL,
						&p->connection->su.sin6.sin6_addr);
			changed = true;
		}
	}

	/* Update interface. */
	if (p->nexthop.ifp && bfd_sess_interface(session) == NULL) {
		if (BGP_DEBUG(bfd, BFD_LIB))
			zlog_debug("%s: interface none to %s", __func__,
				   p->nexthop.ifp->name);

		bfd_sess_set_interface(session, p->nexthop.ifp->name);
		changed = true;
	}

	/*
	 * Update TTL.
	 *
	 * Two cases:
	 * - We detected that the peer is a hop away from us (remove multi hop).
	 *   (this happens when `p->shared_network` is set to `true`)
	 * - eBGP multi hop / TTL security changed.
	 */
	if (!PEER_IS_MULTIHOP(p) && bfd_sess_hop_count(session) > 1) {
		if (BGP_DEBUG(bfd, BFD_LIB))
			zlog_debug("%s: TTL %d to 1", __func__,
				   bfd_sess_hop_count(session));

		bfd_sess_set_hop_count(session, 1);
		changed = true;
	}
	if (PEER_IS_MULTIHOP(p) && p->ttl != bfd_sess_hop_count(session)) {
		if (BGP_DEBUG(bfd, BFD_LIB))
			zlog_debug("%s: TTL %d to %d", __func__,
				   bfd_sess_hop_count(session), p->ttl);

		bfd_sess_set_hop_count(session, p->ttl);
		changed = true;
	}

	/* Update VRF. */
	if (bfd_sess_vrf_id(session) != p->bgp->vrf_id) {
		if (BGP_DEBUG(bfd, BFD_LIB))
			zlog_debug(
				"%s: VRF %s(%d) to %s(%d)", __func__,
				bfd_sess_vrf(session), bfd_sess_vrf_id(session),
				vrf_id_to_name(p->bgp->vrf_id), p->bgp->vrf_id);

		bfd_sess_set_vrf(session, p->bgp->vrf_id);
		changed = true;
	}

	if (changed)
		bfd_sess_install(session);
}

/**
 * Reset BFD configuration data structure to its defaults settings.
 */
/*
 * 把邻居的 BFD 配置恢复为默认值：探测倍数、最小收发间隔、Strict hold 时间、
 * cbit、profile 全部置默认（cbit 置 false，profile 清空）。用于“no”配置或从
 * 手动配置切回组配置时。
 */
static void bgp_peer_bfd_reset(struct peer *p)
{
	/* Set defaults. */
	p->bfd_config->detection_multiplier = BFD_DEF_DETECT_MULT;
	p->bfd_config->min_rx = BFD_DEF_MIN_RX;
	p->bfd_config->min_tx = BFD_DEF_MIN_TX;
	p->bfd_config->hold_time = BFD_DEF_STRICT_HOLD_TIME;
	p->bfd_config->cbit = false;
	p->bfd_config->profile[0] = 0;
}

/*
 * 为单个邻居建立 BFD 配置与会话。断言调用者不是组。若已有 bfd_config 说明已
 * 配置过，仅在必要时把 manual 标志提升为 true 后返回。否则分配 bfd_config，
 * 新建 BFD 会话（回调 bfd_session_status_update），重置为默认值，再按邻居当前
 * 连接的地址族下发本端 / 对端地址、VRF、跳数（多跳取 p->ttl，否则 1），若邻居
 * 有出口接口则一并下发接口名。manual 表示是否为用户显式配置。
 */
void bgp_peer_configure_bfd(struct peer *p, bool manual)
{
	/* Groups should not call this. */
	assert(!CHECK_FLAG(p->sflags, PEER_STATUS_GROUP));

	/* Already configured, skip it. */
	if (p->bfd_config) {
		/* If manually active update flag. */
		if (!p->bfd_config->manual)
			p->bfd_config->manual = manual;

		return;
	}

	/* Allocate memory for configuration overrides. */
	p->bfd_config = XCALLOC(MTYPE_BFD_CONFIG, sizeof(*p->bfd_config));
	p->bfd_config->manual = manual;

	/* Create new session and assign callback. */
	p->bfd_config->session = bfd_sess_new(bfd_session_status_update, p);
	bgp_peer_bfd_reset(p);

	/* Configure session with basic BGP peer data. */
	if (p->connection->su.sa.sa_family == AF_INET)
		bfd_sess_set_ipv4_addrs(p->bfd_config->session,
					p->connection->su_local
						? &p->connection->su_local->sin.sin_addr
						: NULL,
					&p->connection->su.sin.sin_addr);
	else
		bfd_sess_set_ipv6_addrs(p->bfd_config->session,
					p->connection->su_local
						? &p->connection->su_local->sin6.sin6_addr
						: NULL,
					&p->connection->su.sin6.sin6_addr);

	bfd_sess_set_vrf(p->bfd_config->session, p->bgp->vrf_id);
	bfd_sess_set_hop_count(p->bfd_config->session,
			       PEER_IS_MULTIHOP(p) ? p->ttl : 1);

	if (p->nexthop.ifp)
		bfd_sess_set_interface(p->bfd_config->session,
				       p->nexthop.ifp->name);
}

/*
 * 移除单个邻居的 BFD 配置与会话。断言不是组。若该邻居所属的组仍配了 BFD，
 * 则不清除配置，而是把 manual 复位、恢复默认后重新按组配置应用（组配置继续
 * 生效）。否则释放 BFD 会话并释放 bfd_config 结构。
 */
static void bgp_peer_remove_bfd(struct peer *p)
{
	/* Groups should not call this. */
	assert(!CHECK_FLAG(p->sflags, PEER_STATUS_GROUP));

	/*
	 * Peer configuration was removed, however we must check if there
	 * is still a group configuration to keep this running.
	 */
	if (p->group && p->group->conf->bfd_config) {
		p->bfd_config->manual = false;
		bgp_peer_bfd_reset(p);
		bgp_peer_config_apply(p, p->group);
		return;
	}

	if (p->bfd_config)
		bfd_sess_free(&p->bfd_config->session);

	XFREE(MTYPE_BFD_CONFIG, p->bfd_config);
}

/*
 * 为邻居组建组级 BFD 配置。断言 p 是组。已分配则返回。分配组的 bfd_config 并
 * 填入默认定时器 / 探测倍数 / hold 时间，然后对组内每个成员调用
 * bgp_peer_configure_bfd（manual=false），使成员按组配置建立会话。
 */
static void bgp_group_configure_bfd(struct peer *p)
{
	struct listnode *n;
	struct peer *pn;

	/* Peers should not call this. */
	assert(CHECK_FLAG(p->sflags, PEER_STATUS_GROUP));

	/* Already allocated: do nothing. */
	if (p->bfd_config)
		return;

	p->bfd_config = XCALLOC(MTYPE_BFD_CONFIG, sizeof(*p->bfd_config));

	/* Set defaults. */
	p->bfd_config->detection_multiplier = BFD_DEF_DETECT_MULT;
	p->bfd_config->min_rx = BFD_DEF_MIN_RX;
	p->bfd_config->min_tx = BFD_DEF_MIN_TX;
	p->bfd_config->hold_time = BFD_DEF_STRICT_HOLD_TIME;

	for (ALL_LIST_ELEMENTS_RO(p->group->peer, n, pn))
		bgp_peer_configure_bfd(pn, false);
}

/*
 * 移除邻居组的 BFD 配置。断言 p 是组；组配置已为空则返回。释放组级
 * bfd_config 后，对组内每个成员重新计算：若成员自身是手动配置则按自身配置
 * 重新应用（pg 传 NULL），否则移除其由组继承而来的 BFD 配置。
 */
static void bgp_group_remove_bfd(struct peer *p)
{
	struct listnode *n;
	struct peer *pn;

	/* Peers should not call this. */
	assert(CHECK_FLAG(p->sflags, PEER_STATUS_GROUP));

	/* Already freed: do nothing. */
	if (p->bfd_config == NULL)
		return;

	/* Free configuration and point to `NULL`. */
	XFREE(MTYPE_BFD_CONFIG, p->bfd_config);

	/* Now that it is `NULL` recalculate configuration for all peers. */
	for (ALL_LIST_ELEMENTS_RO(p->group->peer, n, pn)) {
		if (pn->bfd_config->manual)
			bgp_peer_config_apply(pn, NULL);
		else
			bgp_peer_remove_bfd(pn);
	}
}

/*
 * 移除 BFD 配置的统一入口：按对象是邻居组还是普通邻居，分别走组或单邻居的
 * 移除流程。
 */
void bgp_peer_remove_bfd_config(struct peer *p)
{
	if (CHECK_FLAG(p->sflags, PEER_STATUS_GROUP))
		bgp_group_remove_bfd(p);
	else
		bgp_peer_remove_bfd(p);
}

/*
 * 把某邻居 / 组的 BFD 相关配置写出为 vty 命令（供配置保存）。规则：组的 BFD
 * 配置总是写出，而普通邻居仅在其为手动配置时写出。随后按需写出 profile、
 * check-control-plane-failure（cbit）、以及 BFD Strict（hold_time 非默认值时带
 * 时长）。有 bfdd 时只写 “bfd” 关键字，无 bfdd 时写探测倍数与收发间隔。
 */
/*
 * bgp_bfd_peer_config_write - Write the peer BFD configuration.
 */
void bgp_bfd_peer_config_write(struct vty *vty, struct peer *peer, const char *addr)
{
	/*
	 * Always show group BFD configuration, but peer only when explicitly
	 * configured.
	 */
	if ((!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)
	     && peer->bfd_config->manual)
	    || CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
#if HAVE_BFDD > 0
		vty_out(vty, " neighbor %s bfd\n", addr);
#else
		vty_out(vty, " neighbor %s bfd %d %d %d\n", addr,
			peer->bfd_config->detection_multiplier,
			peer->bfd_config->min_rx, peer->bfd_config->min_tx);
#endif /* HAVE_BFDD */
	}

	if (peer->bfd_config->profile[0])
		vty_out(vty, " neighbor %s bfd profile %s\n", addr,
			peer->bfd_config->profile);

	if (peer->bfd_config->cbit)
		vty_out(vty, " neighbor %s bfd check-control-plane-failure\n",
			addr);

	if (peergroup_flag_check(peer, PEER_FLAG_BFD_STRICT)) {
		if (peer->bfd_config->hold_time != BFD_DEF_STRICT_HOLD_TIME)
			vty_out(vty, " neighbor %s bfd strict hold-time %u\n", addr,
				peer->bfd_config->hold_time);
		else
			vty_out(vty, " neighbor %s bfd strict\n", addr);
	}
}

/*
 * 在 vty 上展示某邻居的 BFD 会话信息，直接委托 bfd_sess_show；json_neigh 传入
 * JSON 汇聚对象以支持结构化输出。
 */
/*
 * bgp_bfd_show_info - Show the peer BFD information.
 */
void bgp_bfd_show_info(struct vty *vty, struct peer *peer, json_object *json_neigh)
{
	bfd_sess_show(vty, json_neigh, peer->bfd_config->session);
}

/*
 * 命令 “neighbor ... bfd”：为邻居或邻居组启用 BFD。解析出 peer 后，按对象
 * 类型建立组或单邻居 BFD 配置（单邻居 manual=true），再应用配置（含组继承），
 * 返回 CMD_SUCCESS。查找不到邻居返回 CMD_WARNING_CONFIG_FAILED。
 */
DEFUN (neighbor_bfd,
       neighbor_bfd_cmd,
       "neighbor <A.B.C.D|X:X::X:X|WORD> bfd",
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "Enables BFD support\n")
{
	int idx_peer = 1;
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
}

/*
 * 命令 “neighbor ... bfd (2-255) (50-60000) (50-60000)”（无 bfdd 时可用）：
 * 启用 BFD 并显式指定探测倍数、最小接收间隔、最小发送间隔。建立 / 取得 BFD
 * 配置后写入这三个值，再应用配置。适用于内核 BFD 场景。
 */
#if HAVE_BFDD == 0
DEFUN(
       neighbor_bfd_param,
       neighbor_bfd_param_cmd,
       "neighbor <A.B.C.D|X:X::X:X|WORD> bfd (2-255) (50-60000) (50-60000)",
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "Enables BFD support\n"
       "Detect Multiplier\n"
       "Required min receive interval\n"
       "Desired min transmit interval\n")
{
	int idx_peer = 1;
	int idx_number_1 = 3;
	int idx_number_2 = 4;
	int idx_number_3 = 5;
	long detection_multiplier, min_rx, min_tx;
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	detection_multiplier = strtol(argv[idx_number_1]->arg, NULL, 10);
	min_rx = strtol(argv[idx_number_2]->arg, NULL, 10);
	min_tx = strtol(argv[idx_number_3]->arg, NULL, 10);

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	peer->bfd_config->detection_multiplier = detection_multiplier;
	peer->bfd_config->min_rx = min_rx;
	peer->bfd_config->min_tx = min_tx;
	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
}
#endif

/*
 * 命令 “neighbor ... bfd (2-255) (50-60000) (50-60000)”（有 bfdd 时隐藏）：
 * 与无 bfdd 版本逻辑相同，但因为有 bfdd 统一管理定时器，该形式被隐藏、仅在
 * 兼容旧配置时接受，写入的值实际由 bfdd 或 profile 决定优先级。
 */
#if HAVE_BFDD > 0
DEFUN_HIDDEN(
       neighbor_bfd_param,
       neighbor_bfd_param_cmd,
       "neighbor <A.B.C.D|X:X::X:X|WORD> bfd (2-255) (50-60000) (50-60000)",
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "Enables BFD support\n"
       "Detect Multiplier\n"
       "Required min receive interval\n"
       "Desired min transmit interval\n")
{
	int idx_peer = 1;
	int idx_number_1 = 3;
	int idx_number_2 = 4;
	int idx_number_3 = 5;
	long detection_multiplier, min_rx, min_tx;
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	detection_multiplier = strtol(argv[idx_number_1]->arg, NULL, 10);
	min_rx = strtol(argv[idx_number_2]->arg, NULL, 10);
	min_tx = strtol(argv[idx_number_3]->arg, NULL, 10);

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	peer->bfd_config->detection_multiplier = detection_multiplier;
	peer->bfd_config->min_rx = min_rx;
	peer->bfd_config->min_tx = min_tx;
	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
}
#endif

/*
 * 命令 “[no] neighbor ... bfd strict”：开启或关闭 BFD Strict 模式（用
 * PEER_FLAG_BFD_STRICT 标志表示）。带 no 时清除标志，否则设置标志。Strict 模式
 * 下 BFD 故障需等待 hold_time 到期才拆会话。返回 peer_flag_* 的结果。
 */
DEFPY (neighbor_bfd_strict,
       neighbor_bfd_strict_cmd,
       "[no$no] neighbor <A.B.C.D|X:X::X:X|WORD>$neighbor bfd strict",
       NO_STR
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "BFD support\n"
       "Strict mode\n")
{
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, neighbor);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	if (no)
		return peer_flag_unset(peer, PEER_FLAG_BFD_STRICT);

	return peer_flag_set(peer, PEER_FLAG_BFD_STRICT);
}

/*
 * 命令 “[no] neighbor ... bfd strict hold-time [秒]”：设置 BFD Strict 模式的
 * hold 时间。先建立 / 取得 BFD 配置并取消已有 hold 定时器；带 no 时恢复默认
 * hold 时间并清除 Strict 标志，否则写入新时长并设置 Strict 标志。最后应用配置。
 * 若解析时省略秒数，hold_time 为 0（由 clippy 生成的默认值）。
 */
DEFPY (neighbor_bfd_strict_hold_time,
       neighbor_bfd_strict_hold_time_cmd,
       "[no$no] neighbor <A.B.C.D|X:X::X:X|WORD>$neighbor bfd strict hold-time ![(1-4294967295)$hold_time]",
       NO_STR
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "BFD support\n"
       "Strict mode\n"
       "BFD Hold time in seconds\n"
       "Seconds to wait before declaring BFD session down\n")
{
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, neighbor);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	event_cancel(&peer->bfd_config->t_hold_timer);

	if (no) {
		peer->bfd_config->hold_time = BFD_DEF_STRICT_HOLD_TIME;
		peer_flag_unset(peer, PEER_FLAG_BFD_STRICT);
	} else {
		peer->bfd_config->hold_time = hold_time;
		peer_flag_set(peer, PEER_FLAG_BFD_STRICT);
	}

	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
}

/*
 * 命令 “[no] neighbor ... bfd check-control-plane-failure”：设置 BFD 的 C 位
 * （cbit），即是否把数据面（转发面）状态与 BGP 控制面联动。带 no 时清除 cbit，
 * 否则设置。参数索引随是否带 no 而不同（no 时 peer 在第 2 个位置）。最后应用配置。
 */
DEFUN (neighbor_bfd_check_controlplane_failure,
       neighbor_bfd_check_controlplane_failure_cmd,
       "[no] neighbor <A.B.C.D|X:X::X:X|WORD> bfd check-control-plane-failure",
       NO_STR
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "BFD support\n"
       "Link dataplane status with BGP controlplane\n")
{
	const char *no = strmatch(argv[0]->text, "no") ? "no" : NULL;
	int idx_peer = 0;
	struct peer *peer;

	if (no)
		idx_peer = 2;
	else
		idx_peer = 1;
	peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	peer->bfd_config->cbit = no == NULL;
	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
 }

/*
 * 命令 “no neighbor ... bfd”（有 bfdd 时）：关闭并移除邻居 / 组的 BFD 配置。
 * 查出 peer 后，按是组还是普通邻居分别调用组 / 单邻居的移除流程。
 */
#if HAVE_BFDD > 0
DEFUN (no_neighbor_bfd,
       no_neighbor_bfd_cmd,
       "no neighbor <A.B.C.D|X:X::X:X|WORD> bfd",
       NO_STR
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "Disables BFD support\n")
 {
	 int idx_peer = 2;
	 struct peer *peer;

	 peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	 if (!peer)
		 return CMD_WARNING_CONFIG_FAILED;

	 if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		 bgp_group_remove_bfd(peer);
	 else
		 bgp_peer_remove_bfd(peer);

	 return CMD_SUCCESS;
 }
#endif

/*
 * 命令 “no neighbor ... bfd [探测倍数 接收 发送]”（无 bfdd 时）：关闭并移除
 * 邻居 / 组的 BFD 配置。语法允许附带上定时参数（与正向命令对应）但此处忽略
 * 其值，只做移除。按组 / 单邻居分派到对应移除流程。
 */
#if HAVE_BFDD == 0
DEFUN (no_neighbor_bfd,
       no_neighbor_bfd_cmd,
       "no neighbor <A.B.C.D|X:X::X:X|WORD> bfd [(2-255) (50-60000) (50-60000)]",
       NO_STR
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "Disables BFD support\n"
       "Detect Multiplier\n"
       "Required min receive interval\n"
       "Desired min transmit interval\n")
 {
	 int idx_peer = 2;
	 struct peer *peer;

	 peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	 if (!peer)
		 return CMD_WARNING_CONFIG_FAILED;

	 if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		 bgp_group_remove_bfd(peer);
	 else
		 bgp_peer_remove_bfd(peer);

	 return CMD_SUCCESS;
}
#endif

#if HAVE_BFDD > 0
/*
 * 命令 “neighbor ... bfd profile <名称>”（有 bfdd 时）：为邻居 / 组关联一个
 * BFD profile。建立 / 取得 BFD 配置后把 profile 名拷入配置，再应用配置。
 */
DEFUN(neighbor_bfd_profile, neighbor_bfd_profile_cmd,
      "neighbor <A.B.C.D|X:X::X:X|WORD> bfd profile BFDPROF",
      NEIGHBOR_STR
      NEIGHBOR_ADDR_STR2
      "BFD integration\n"
      BFD_PROFILE_STR
      BFD_PROFILE_NAME_STR)
{
	int idx_peer = 1, idx_prof = 4;
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	strlcpy(peer->bfd_config->profile, argv[idx_prof]->arg,
		sizeof(peer->bfd_config->profile));
	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
}

/*
 * 命令 “no neighbor ... bfd profile [名称]”（有 bfdd 时）：取消邻居 / 组的 BFD
 * profile。若该邻居尚无 bfd_config 直接返回成功；否则建立 / 取得配置后把
 * profile 清空并应用配置。
 */
DEFUN(no_neighbor_bfd_profile, no_neighbor_bfd_profile_cmd,
      "no neighbor <A.B.C.D|X:X::X:X|WORD> bfd profile [BFDPROF]",
      NO_STR
      NEIGHBOR_STR
      NEIGHBOR_ADDR_STR2
      "BFD integration\n"
      BFD_PROFILE_STR
      BFD_PROFILE_NAME_STR)
{
	int idx_peer = 2;
	struct peer *peer;

	peer = peer_and_group_lookup_vty(vty, argv[idx_peer]->arg);
	if (!peer)
		return CMD_WARNING_CONFIG_FAILED;

	if (!peer->bfd_config)
		return CMD_SUCCESS;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		bgp_group_configure_bfd(peer);
	else
		bgp_peer_configure_bfd(peer, true);

	peer->bfd_config->profile[0] = 0;
	bgp_peer_config_apply(peer, peer->group);

	return CMD_SUCCESS;
}
#endif /* HAVE_BFDD */

/*
 * BFD 集成初始化：调用库函数把 bgpd 注册为 BFD 客户端（基于 bgp_zclient 与
 * 事件循环 tm），随后在 BGP 节点下安装各 BFD 相关命令。有 bfdd 时额外安装
 * profile 相关的两条命令。
 */
void bgp_bfd_init(struct event_loop *tm)
{
	/* Initialize BFD client functions */
	bfd_protocol_integration_init(bgp_zclient, tm);

	/* "neighbor bfd" commands. */
	install_element(BGP_NODE, &neighbor_bfd_cmd);
	install_element(BGP_NODE, &neighbor_bfd_param_cmd);
	install_element(BGP_NODE, &neighbor_bfd_check_controlplane_failure_cmd);
	install_element(BGP_NODE, &neighbor_bfd_strict_cmd);
	install_element(BGP_NODE, &neighbor_bfd_strict_hold_time_cmd);
	install_element(BGP_NODE, &no_neighbor_bfd_cmd);

#if HAVE_BFDD > 0
	install_element(BGP_NODE, &neighbor_bfd_profile_cmd);
	install_element(BGP_NODE, &no_neighbor_bfd_profile_cmd);
#endif /* HAVE_BFDD */
}
