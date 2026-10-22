// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP-4, BGP-4+ daemon program
 * Copyright (C) 1996, 97, 98, 99, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgpd.c —— bgpd 的“主控与对象生命周期中心”：管理 BGP 实例（default/
 *                    VRF/view）、peer/peer-group 的创建-配置-激活-停止-删除，
 *                    以及大量单点配置的 SET/UNSET/GET 辅助函数。
 * ============================================================================
 * 职责与定位：
 *   本文件是 bgpd 进程的中枢。bgp_packet.c/bgp_route.c 等“跑数据面”的文件依赖
 *   这里维护出来的数据结构与全局状态。可以把它理解为四个层面：
 *     (1) 进程级全局对象与选项：bgp_master（进程单例，含所有 bgp 实例）、
 *         bgp_option_*（NORIB、deterministic-med 等全局行为开关）、
 *         全局队列/钩子（bgp_inst_delete、bgp_instance_state、bgp_routerid_update）。
 *     (2) BGP 实例（struct bgp）生命周期：bgp_create/bgp_get/bgp_delete/bgp_free，
 *         对应 default 实例、每个 VRF 实例、以及 view 实例（name != NULL && !vrf_id）。
 *     (3) peer（struct peer）与 peer-group（struct peer_group）生命周期：
 *         peer_new/peer_create/peer_delete、peer_activate/peer_deactivate、
 *         peer-group 的创建/绑定/展开（peer_group2peer_config_copy*）。
 *     (4) 海量“单个配置项”的 SET/UNSET/GET 辅助函数：timers、update-source、
 *         password、route-map、prefix-list、maximum-prefix、ttl-security 等。
 *         这些函数由 bgp_vty.c 的 CLI 命令回调调用，内部大多通过
 *         peer_flag_modify / peer_af_flag_modify “三件套”改标志并触发副作用。
 *
 * 三条必须理解的主线：
 *   A. 实例模型：一个 bgpd 进程有唯一 struct bgp_master bgp_master（静态），导出为
 *      全局指针 bm。bm->bgp 是所有实例的链表。增删实例都要走 bgp_create/bgp_delete，
 *      它们是 VRF 增删、`router bgp` 配置、view 实例的核心。
 *   B. peer 与 peer-group 的“继承三件套”：每个 peer 有一组 uint64_t 标志——
 *      flags（生效值）、flags_override（“本 peer 显式覆盖了组值”的位）、
 *      flags_invert（“组值被取反”的位）。peer_group2peer_config_copy* 把组值
 *      按 `(group_flags ^ flags_invert) & ~flags_override` 的公式合并进成员 peer；
 *      peer_flag_modify/peer_af_flag_modify 在成员上改标志时会同步修改后两者，
 *      以保证“组后续变更仍能正确传播、而成员的显式设置不被覆盖”。这是本文件
 *      最容易被误读、也最容易出 bug 的机制，务必对照 bgpd.h 的结构体注释阅读。
 *   C. peer 的两层引用计数：struct peer 自身与 struct peer_connection 都有 lock
 *      计数（peer_lock/peer_unlock、connection 由 peer 持有）。删除 peer 时先
 *      断开连接、再等 IO 线程释放引用，最后 peer_free。乱序释放会导致 UAF。
 *
 * 关键路径：
 *   上游调用者：bgp_vty.c 的 CLI 命令回调（绝大部分 SET/UNSET）；
 *              bgp_main.c/bgp_zebra.c 的进程与 VRF 事件；
 *              bgp_fsm.c 状态机在建立/断开连接时回调 peer_* 辅助函数。
 *   本文件调用的下游：bgp_fsm.c（peer_start/peer_stop/peer_xfer_*）、
 *              bgp_route.c（bestpath 重算）、bgp_updgrp.c（group 重建）、
 *              bgp_zebra.c（实例上下线）、bgp_nht.c、bgp_bfd.c 等。
 *
 * 关键函数清单（建议阅读顺序）：
 *   1) bgp_option_set/unset/check、bgp_router_id_set、bgp_confederation_*  ——全局配置
 *   2) peer_af_create/find/delete、peer_cmp、peer_calc_sort               ——peer 基础模型
 *   3) peer_flag_inherit、peer_af_flag_inherit                            ——继承三件套（读公式）
 *   4) peer_new、peer_xfer_config、peer_create                            ——peer 诞生
 *   5) peer_group2peer_config_copy_af、peer_group2peer_config_copy        ——peer-group 展开
 *   6) peer_activate/peer_deactivate、peer_flag_modify、peer_af_flag_modify ——激活与标志改动（含副作用）
 *   7) bgp_create、bgp_get、bgp_delete、bgp_free                          ——实例生命周期
 *   8) peer_delete、peer_group_delete、peer_group_bind                   ——peer/组的销毁与绑定
 *   9) bgp_startup_timer_expire、peer_unshut_after_cfg、bgp_init           ——启动/收尾流程
 *  10) bgp_clearing_batch_*、bgp_process_conn_error、bgp_enqueue_conn_err  ——批量清理与连接错误队列
 *
 * 建议阅读顺序：
 *   先读 (2)(3) 建立 peer 的数据模型与继承语义，再读 (4)(5) 看 peer 如何被创建并被
 *   peer-group 展开，随后 (6) 理解一次配置改动如何推进到 FSM/路由，(7) 理解实例
 *   的生死，(8) 收尾销毁细节；最后 (9)(10) 看进程启动与批量操作。海量的单项
 *   SET/UNSET（peer_timers_set ……）可最后按需查阅，它们的骨架高度一致。
 * ============================================================================
 */

#include <zebra.h>

#include "prefix.h"
#include "frrevent.h"
#include "buffer.h"
#include "stream.h"
#include "ringbuf.h"
#include "command.h"
#include "sockunion.h"
#include "sockopt.h"
#include "network.h"
#include "memory.h"
#include "filter.h"
#include "routemap.h"
#include "log.h"
#include "plist.h"
#include "linklist.h"
#include "workqueue.h"
#include "queue.h"
#include "zclient.h"
#include "bfd.h"
#include "hash.h"
#include "jhash.h"
#include "table.h"
#include "lib/json.h"
#include "lib/sockopt.h"
#include "frr_pthread.h"
#include "bitfield.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_dump.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_community_alias.h"
#include "bgpd/bgp_conditional_adv.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_regex.h"
#include "bgpd/bgp_clist.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_open.h"
#include "bgpd/bgp_filter.h"
#include "bgpd/bgp_nexthop.h"
#include "bgpd/bgp_damp.h"
#include "bgpd/bgp_mplsvpn.h"
#ifdef ENABLE_BGP_VNC
#include "bgpd/rfapi/bgp_rfapi_cfg.h"
#include "bgpd/rfapi/rfapi_backend.h"
#endif
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_network.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_mpath.h"
#include "bgpd/bgp_nht.h"
#include "bgpd/bgp_nhg.h"
#include "bgpd/bgp_updgrp.h"
#include "bgpd/bgp_bfd.h"
#include "bgpd/bgp_memory.h"
#include "bgpd/bgp_evpn_vty.h"
#include "bgpd/bgp_keepalives.h"
#include "bgpd/bgp_io.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_flowspec.h"
#include "bgpd/bgp_labelpool.h"
#include "bgpd/bgp_pbr.h"
#include "bgpd/bgp_addpath.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_evpn_mh.h"
#include "bgpd/bgp_mac.h"
#include "bgpd/bgp_trace.h"
#include "bgpd/bgp_srv6.h"

/* ========== 0. 内存类型 / QOBJ / 钩子 / 全局单例声明 ========== */

/* 定义一个仅供本文件使用的内存分配类型（用于 peer 发送的 shutdown 消息）。
 * FRR 用 MTYPE 给每类分配打标签，便于 memstats 统计与内存泄漏定位。 */
DEFINE_MTYPE_STATIC(BGPD, PEER_TX_SHUTDOWN_MSG, "Peer shutdown message (TX)");
/* QOBJ 是 FRR 的“对象注册”机制：把 bgp_master/bgp/peer 注册进自省框架，
 * 便于 `show ... json` 或调试时按类型枚举存活对象。 */
DEFINE_QOBJ_TYPE(bgp_master);
DEFINE_QOBJ_TYPE(bgp);
DEFINE_QOBJ_TYPE(peer);
/* 三个可挂载点（hook）：
 *  - bgp_inst_delete     : 某 bgp 实例即将被删除时，通知订阅者做清理；
 *  - bgp_instance_state  : 实例状态（up/down）变化；
 *  - bgp_routerid_update : router-id 变更（withdraw 表示是否需撤回旧下一跳）。
 * 典型订阅者来自 RFAPI/EVPN 等可选模块，用于解耦。 */
DEFINE_HOOK(bgp_inst_delete, (struct bgp *bgp), (bgp));
DEFINE_HOOK(bgp_instance_state, (struct bgp *bgp), (bgp));
DEFINE_HOOK(bgp_routerid_update, (struct bgp *bgp, bool withdraw), (bgp, withdraw));

/* Peers with connection error/failure, per bgp instance */
/* 每个 bgp 实例内的一张“连接错误 peer”双链表：当某个 peer 的连接建立/读写出错
 * （如 TCP 被 RST、握手失败）时，把其 peer_connection 挂到这里，由主线程的
 * bgp_process_conn_error() 稍后统一处理（批量 reschedule），避免在 IO 线程里做重活。 */
DECLARE_DLIST(bgp_peer_conn_errlist, struct peer_connection, conn_err_link);

/* List of info about peers that are being cleared from BGP RIBs in a batch */
/* “批量清理（clearing batch）”机制：一次 clear 操作会把多个 peer 要从 RIB 中
 * 摘除的路由集中处理，避免逐条 walk 造成 O(N^2)。下面两个容器分别保存
 * “被清理的 peer 集合信息”与“待处理的 dest（前缀节点）”。 */
DECLARE_DLIST(bgp_clearing_info, struct bgp_clearing_info, link);

/* List of dests that need to be processed in a clearing batch */
DECLARE_LIST(bgp_clearing_destlist, struct bgp_clearing_dest, link);

/* Hash of peers in clearing info object */
/* 清理批次内用于“快速判断某 peer 是否在本批”的哈希表，键为 peer 指针。 */
static int peer_clearing_hash_cmp(const struct peer *p1, const struct peer *p2);
static uint32_t peer_clearing_hashfn(const struct peer *p1);

/* 用上面的比较/哈希函数声明一个以 peer 指针为键的哈希表类型 bgp_clearing_hash。 */
DECLARE_HASH(bgp_clearing_hash, struct peer, clear_hash_link,
	     peer_clearing_hash_cmp, peer_clearing_hashfn);

/* BGP process wide configuration.  */
/* 进程级单例：整个 bgpd 只有一个 bgp_master，静态分配，随进程存活。 */
static struct bgp_master bgp_master;

/* BGP process wide configuration pointer to export.  */
/* 对外导出的全局指针，指向上面的 bgp_master；全代码库通过 bm 访问进程级状态。 */
struct bgp_master *bm;

/* BGP community-list.  */
struct community_list_handler *bgp_clist;

/* 等价多路径（ECMP）最大条数，默认 MULTIPATH_NUM，可由配置修改。 */
unsigned int multipath_num = MULTIPATH_NUM;

/* Number of bgp instances configured for suppress fib config */
/* 统计有多少个实例开启了 suppress-fib 配置，用于全局决策（如是否延迟上报 FIB）。 */
unsigned int bgp_suppress_fib_count;

/* 前置声明（下面才定义，但被更前面的函数引用） */
static void bgp_if_finish(struct bgp *bgp);
static void peer_drop_dynamic_neighbor(struct peer *peer);

static void peer_vpn_change_bestpath(struct peer *peer, afi_t afi);

extern struct zclient *bgp_zclient;

/*
 * 判断“除 bgp 之外，是否还存在其它需要监听 179 端口的实例”。
 *
 * 背景：同 vrf_id 下可能有多个实例（例如 default 与某个 view），它们共享同一
 * BGP 监听套接字。当要销毁某个实例、决定“是否可以关掉监听套接字”时，必须确认
 * 同 vrf 下没有别的实例仍需监听，否则会误关别人的 socket。
 *
 * @bgp      : 即将被销毁/检查的实例（会被 skip）。
 * @return   : true 表示仍有其它实例需要监听，socket 不能关。
 *
 * 注意：只有设置了 BGP_FLAG_VRF_MAY_LISTEN 的实例才真正需要监听；v4/v6 监听
 * 是否开启另由 vrf 配置决定，这里只做“是否还有同伴”的粗判。
 */
static bool bgp_has_remaining_instances(const struct bgp *bgp)
{
	struct listnode *node;
	struct bgp *bgp_next;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_next)) {
		if (bgp_next == bgp)
			continue;
		if (bgp->vrf_id != bgp_next->vrf_id)
			continue;
		if (!CHECK_FLAG(bgp->flags, BGP_FLAG_VRF_MAY_LISTEN))
			continue;
		return true;
	}

	return false;
}

/* handle main socket creation or deletion */
/*
 * 统一管理“主 BGP 监听套接字”的创建/关闭（幂等）。
 *
 * 调用时机：bgp_create()（create=true）与 bgp_delete()/bgp_instance_down()
 * （create=false）中，由实例生命周期驱动。
 *
 * @create : true=确保监听套接字存在；false=在确认无其它实例需要后关闭。
 * @bgp    : 触发本次检查的实例（用于判断是否还有“同伴实例”）。
 * @return : 0 成功；BGP_ERR_INVALID_VALUE 表示 bind/listen 失败。
 *
 * 关键点：
 *  - bgp_server_main_created 是函数内静态标志，实现“进程只监一次”的幂等性；
 *    注意它是函数级 static，与本文件其它监听（如 vrf 级）无关。
 *  - bm->addresses 非空表示用户显式配置了 `bgp listen address`，需逐地址监听；
 *    为空则监听通配地址（address=NULL）。
 *  - 关闭分支用 bgp_has_remaining_instances() 兜底：只要还有实例需要监听就不关。
 */
static int bgp_check_main_socket(bool create, struct bgp *bgp)
{
	static int bgp_server_main_created;
	struct listnode *node;
	char *address;

	if (create) {
		if (bgp_server_main_created)
			return 0;
		if (list_isempty(bm->addresses)) {
			if (bgp_socket(bgp, bm->port, NULL) < 0)
				return BGP_ERR_INVALID_VALUE;
		} else {
			for (ALL_LIST_ELEMENTS_RO(bm->addresses, node, address))
				if (bgp_socket(bgp, bm->port, address) < 0)
					return BGP_ERR_INVALID_VALUE;
		}
		bgp_server_main_created = 1;
		return 0;
	}
	if (!bgp_server_main_created)
		return 0;
	if (bgp_has_remaining_instances(bgp))
		return 0;

	bgp_close();
	bgp_server_main_created = 0;
	return 0;
}

/*
 * 复位一个 peer 的 BGP 会话：删除其 doppelganger（“分身”peer），并向 FSM 注入
 * BGP_Stop 事件，把连接从当前状态打回 Idle/Active。
 *
 * @peer : 目标 peer。
 *
 * 关于 doppelganger（分身）：当一个未配置的 peer 主动发起连接时，bgpd 会为它
 * 创建一个临时 peer（未配置节点），即 doppelganger。此处复位“已配置的 peer”时，
 * 若存在这样一个临时分身，需要先把分身删掉，避免残留。
 * 注意条件：分身的连接状态不能是 Deleted、且分身本身不是配置节点，才删除。
 */
void bgp_session_reset(struct peer *peer)
{
	if (peer->doppelganger && (peer->doppelganger->connection->status != Deleted) &&
	    !peer_is_config_node(peer->doppelganger))
		peer_delete(peer->doppelganger);

	BGP_EVENT_ADD(peer->connection, BGP_Stop);
}

/*
 * During session reset, we may delete the doppelganger peer, which would
 * be the next node to the current node. If the session reset was invoked
 * during walk of peer list, we would end up accessing the freed next
 * node. This function moves the next node along.
 */
/*
 * bgp_session_reset 的“遍历安全版”。
 *
 * 当调用者正在遍历 peer 链表时复位会话，而 doppelganger 恰好是“当前节点的
 * 下一个节点”，则 peer_delete(doppelganger) 会把迭代要访问的 next 释放掉，
 * 造成 UAF。此版本在删除分身之前，如果发现 *nnode 正指向该分身，就先把
 * *nnode 前移到再下一个节点，从而保证调用者的遍历不踩到已释放内存。
 *
 * @peer  : 目标 peer。
 * @nnode : 调用者迭代游标的地址（指向“下一个节点”的指针的指针）；可为 NULL。
 */
void bgp_session_reset_safe(struct peer *peer, struct listnode **nnode)
{
	struct listnode *n;
	struct peer *npeer;

	n = (nnode) ? *nnode : NULL;
	npeer = (n) ? listgetdata(n) : NULL;

	if (peer->doppelganger && (peer->doppelganger->connection->status != Deleted) &&
	    !peer_is_config_node(peer->doppelganger)) {
		if (peer->doppelganger == npeer)
			/* nnode and *nnode are confirmed to be non-NULL here */
			*nnode = (*nnode)->next;
		peer_delete(peer->doppelganger);
	}

	BGP_EVENT_ADD(peer->connection, BGP_Stop);
}

/* ========== 1. 进程级全局选项（bgp_option_*）与 no-rib 运行时切换 ========== */

/* BGP global flag manipulation.  */
/*
 * 设置进程级全局选项位。目前合法位：
 *   BGP_OPT_NO_FIB    —— 不向 Zebra 安装 FIB（route installation）；
 *   BGP_OPT_NO_LISTEN —— 不监听 179 端口；
 *   BGP_OPT_NO_ZEBRA  —— 完全不与 Zebra 交互（常用于单测/离线场景）。
 * 注意：unset 允许的位比 set 少（NO_LISTEN 不可通过此 API 取消），这是刻意的，
 * 因为 NO_LISTEN 语义上属于单向开关。
 *
 * @flag   : 选项位（BGP_OPT_*）。
 * @return : 0 成功；BGP_ERR_INVALID_FLAG 表示非法位。
 */
int bgp_option_set(int flag)
{
	switch (flag) {
	case BGP_OPT_NO_FIB:
	case BGP_OPT_NO_LISTEN:
	case BGP_OPT_NO_ZEBRA:
		SET_FLAG(bm->options, flag);
		break;
	default:
		return BGP_ERR_INVALID_FLAG;
	}
	return 0;
}

/* 清除进程级全局选项位（详见 bgp_option_set）。允许清除 NO_ZEBRA / NO_FIB。 */
int bgp_option_unset(int flag)
{
	switch (flag) {
	case BGP_OPT_NO_ZEBRA:
	case BGP_OPT_NO_FIB:
		UNSET_FLAG(bm->options, flag);
		break;
	default:
		return BGP_ERR_INVALID_FLAG;
	}
	return 0;
}

/* 查询某个全局选项位是否置位。返回非 0 表示已设置。 */
int bgp_option_check(int flag)
{
	return CHECK_FLAG(bm->options, flag);
}

/* set the bgp no-rib option during runtime and remove installed routes */
/*
 * 运行时开启 no-rib：置 NO_FIB 位，并把所有已安装到 Zebra 的路由撤回。
 *
 * 流程：幂等检查 → 置位 → 遍历所有实例的所有 afi/safi → 对支持 FIB 更新的 safi
 * 调用 bgp_zebra_withdraw_table_all_subtypes() 撤回整表。
 * 注意：bgp_fibupd_safi() 过滤掉“不该由本进程安装 FIB”的 safi（如 EVPN 某些类型），
 * 避免误操作导致崩溃。日志用于提示运维“路由已从 RIB 撤下”。
 */
void bgp_option_norib_set_runtime(void)
{
	struct bgp *bgp;
	struct listnode *node;
	afi_t afi;
	safi_t safi;

	if (bgp_option_check(BGP_OPT_NO_FIB))
		return;

	bgp_option_set(BGP_OPT_NO_FIB);

	zlog_info("Disabled BGP route installation to RIB (Zebra)");

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp)) {
		FOREACH_AFI_SAFI (afi, safi) {
			/*
			 * Stop a crash, more work is needed
			 * here to properly add/remove these types of
			 * routes from zebra.
			 */
			if (!bgp_fibupd_safi(safi))
				continue;

			bgp_zebra_withdraw_table_all_subtypes(bgp, afi, safi);
		}
	}

	zlog_info("All routes have been withdrawn from RIB (Zebra)");
}

/* unset the bgp no-rib option during runtime and announce routes to Zebra */
/*
 * 运行时关闭 no-rib：清 NO_FIB 位，并把所有路由重新通告给 Zebra。
 * 与 set_runtime 对称；同样用 bgp_fibupd_safi() 过滤 safi。
 */
void bgp_option_norib_unset_runtime(void)
{
	struct bgp *bgp;
	struct listnode *node;
	afi_t afi;
	safi_t safi;

	if (!bgp_option_check(BGP_OPT_NO_FIB))
		return;

	bgp_option_unset(BGP_OPT_NO_FIB);

	zlog_info("Enabled BGP route installation to RIB (Zebra)");

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp)) {
		FOREACH_AFI_SAFI (afi, safi) {
			/*
			 * Stop a crash, more work is needed
			 * here to properly add/remove these types
			 * of routes from zebra
			 */
			if (!bgp_fibupd_safi(safi))
				continue;

			bgp_zebra_announce_table_all_subtypes(bgp, afi, safi);
		}
	}

	zlog_info("All routes have been installed in RIB (Zebra)");
}

/* ========== 2. bgp 实例级配置：config 位、router-id、cluster-id、timers、联邦等 ========== */

/* Internal function to set BGP structure configuration flag.  */
/* 置 bgp->config 位（记录“该实例做过哪些配置”，供 show running / 状态判断用）。 */
static void bgp_config_set(struct bgp *bgp, int config)
{
	SET_FLAG(bgp->config, config);
}

/* 清 bgp->config 位，与 bgp_config_set 对称。 */
static void bgp_config_unset(struct bgp *bgp, int config)
{
	UNSET_FLAG(bgp->config, config);
}

/* 查询 bgp->config 位。返回非 0 表示该项已被配置。 */
static int bgp_config_check(struct bgp *bgp, int config)
{
	return CHECK_FLAG(bgp->config, config);
}

/* Set BGP router identifier; distinguish between explicit config and other
 * cases.
 */
/*
 * 设置实例的 BGP Router ID（既用于本地标识，也用于 EVPN RD、VPN 下一跳等）。
 *
 * @bgp      : 目标实例。
 * @id       : 新的 router-id。
 * @is_config: true 表示来自用户的显式 `bgp router-id` 配置（区别于从 Zebra
 *             自动同步来的地址）；该标志会透传给 vpn_handle_router_id_update，
 *             影响 VPN 路由的 RD/下一跳处理方式。
 * @return   : 恒为 0。
 *
 * 顺序很重要：先把“旧 RID 相关”的路由撤回（EVPN/VPN/钩子，withdraw=true），
 * 再更新 bgp->router_id 并同步到所有 peer 的 local_id，最后再按新 RID 重新
 * 下发（withdraw=false）。这是典型的“先撤后加”以保证一致性。
 * 每个 peer 还会记录 last_reset 原因 PEER_DOWN_RID_CHANGE。
 */
static int bgp_router_id_set(struct bgp *bgp, const struct in_addr *id,
			     bool is_config)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if (IPV4_ADDR_SAME(&bgp->router_id, id))
		return 0;

	/* EVPN uses router id in RD, withdraw them */
	if (is_evpn_enabled())
		bgp_evpn_handle_router_id_update(bgp, true);

	vpn_handle_router_id_update(bgp, true, is_config);

	hook_call(bgp_routerid_update, bgp, true);

	IPV4_ADDR_COPY(&bgp->router_id, id);

	/* Set all peer's local identifier with this value. */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		IPV4_ADDR_COPY(&peer->local_id, id);

		peer_set_last_reset(peer, PEER_DOWN_RID_CHANGE);

		peer_notify_config_change(peer->connection);
	}

	/* EVPN uses router id in RD, update them */
	if (is_evpn_enabled())
		bgp_evpn_handle_router_id_update(bgp, false);

	vpn_handle_router_id_update(bgp, false, is_config);

	hook_call(bgp_routerid_update, bgp, false);
	return 0;
}

/*
 * 由 Zebra 触发的 router-id “抬升”：当 Zebra 报告某 VRF 的 router-id 候选
 * （通常是该 VRF 下选出的最优 IPv4 地址）时，本函数把它同步到对应实例。
 *
 * @vrf_id     : 哪个 VRF 的 router-id 变了。
 * @router_id  : 新候选地址（可能为 NULL，此时沿用实例已缓存的 router_id_zebra）。
 *
 * 关键规则：
 *  - 若用户显式配置了 router-id（bgp->router_id_static 非 0），则不受 Zebra 影响。
 *  - 仅当该实例当前没有已建立的 peer 会话（established_peers==0）时才自动更新，
 *    避免会话中途改 RID 造成抖动。
 *  - default VRF 的 RID 变化影响所有 view（inst_type != VRF 的实例一起更新）。
 *  - 若旧 RID 为 0（INADDR_ANY），说明是首次确定，is_config 传 true。
 */
void bgp_router_id_zebra_bump(vrf_id_t vrf_id, const struct prefix *router_id)
{
	struct listnode *node, *nnode;
	struct bgp *bgp;
	struct in_addr *addr = NULL;

	if (router_id != NULL)
		addr = (struct in_addr *)&(router_id->u.prefix4);

	if (vrf_id == VRF_DEFAULT) {
		/* Router-id change for default VRF has to also update all
		 * views. */
		for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
			if (bgp->inst_type == BGP_INSTANCE_TYPE_VRF)
				continue;

			if (addr)
				bgp->router_id_zebra = *addr;
			else
				addr = &bgp->router_id_zebra;

			if (!bgp->router_id_static.s_addr) {
				/* Router ID is updated if there are no active
				 * peer sessions
				 */
				if (bgp->established_peers == 0) {
					if (BGP_DEBUG(zebra, ZEBRA))
						zlog_debug(
							"RID change : vrf %s(%u), RTR ID %pI4",
							bgp->name_pretty,
							bgp->vrf_id, addr);
					/*
					 * if old router-id was 0x0, set flag
					 * to use this new value
					 */
					bgp_router_id_set(bgp, addr,
							  (bgp->router_id.s_addr
							   == INADDR_ANY)
								  ? true
								  : false);
				}
			}
		}
	} else {
		bgp = bgp_lookup_by_vrf_id(vrf_id);
		if (bgp) {
			if (addr)
				bgp->router_id_zebra = *addr;
			else
				addr = &bgp->router_id_zebra;

			if (!bgp->router_id_static.s_addr) {
				/* Router ID is updated if there are no active
				 * peer sessions
				 */
				if (bgp->established_peers == 0) {
					if (BGP_DEBUG(zebra, ZEBRA))
						zlog_debug(
							"RID change : vrf %s(%u), RTR ID %pI4",
							bgp->name_pretty,
							bgp->vrf_id, addr);
					/*
					 * if old router-id was 0x0, set flag
					 * to use this new value
					 */
					bgp_router_id_set(bgp, addr,
							  (bgp->router_id.s_addr
							   == INADDR_ANY)
								  ? true
								  : false);
				}
			}

		}
	}
}

/*
 * 设置“静态/显式配置”的 router-id（来自 CLI `bgp router-id A.B.C.D`）。
 * 记入 bgp->router_id_static 后，若 id 非 0 则用它作为实际 RID；若为 0（取消配置）
 * 则回退到从 Zebra 学到的 router_id_zebra。is_config 恒为 true。
 */
void bgp_router_id_static_set(struct bgp *bgp, struct in_addr id)
{
	bgp->router_id_static = id;
	bgp_router_id_set(bgp,
			  id.s_addr != INADDR_ANY ? &id : &bgp->router_id_zebra,
			  true /* is config */);
}

/*
 * 全局开关“等待 FIB 安装完成”（wait-for-fib）：开启后，BGP 通告某路由时会等待
 * Zebra 确认 FIB 安装结果，再把该路由通告给 peer（用于避免“黑洞”窗口）。
 *
 * @set : true=开启，false=关闭。
 *
 * 计数与广播：bm->wait_for_fib 是全局期望值；bgp_suppress_fib_count 记录有多少实例
 * 要求 suppress。只有“从 0 变 1”或“变回 0”时才向 Zebra 发
 * ZEBRA_ROUTE_NOTIFY_REQUEST 请求（因为 Zebra 侧只需知道全局是否有人要）。
 * 变更后还会重置所有 peer 会话（peer_notify_config_change），因为已安装的路由
 * 可能需要在新的 notify 模式下重来一遍。
 */
void bm_wait_for_fib_set(bool set)
{
	bool send_msg = false;
	struct bgp *bgp;
	struct peer *peer;
	struct listnode *next, *node;

	if (bm->wait_for_fib == set)
		return;

	bm->wait_for_fib = set;
	if (set) {
		if (bgp_suppress_fib_count == 0)
			send_msg = true;
		bgp_suppress_fib_count++;
	} else {
		bgp_suppress_fib_count--;
		if (bgp_suppress_fib_count == 0)
			send_msg = true;
	}

	if (send_msg && bgp_zclient)
		zebra_route_notify_send(ZEBRA_ROUTE_NOTIFY_REQUEST,
					bgp_zclient, set);

	/*
	 * If this is configed at a time when peers are already set
	 * FRR needs to reset the connection(s) as that some installs
	 * may have already happened in some shape fashion or form
	 * let's just start over
	 */
	for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, bgp)) {
		for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
			peer_set_last_reset(peer, PEER_DOWN_SUPPRESS_FIB_PENDING);

			if (!BGP_IS_VALID_STATE_FOR_NOTIF(
				    peer->connection->status))
				continue;

			peer_notify_config_change(peer->connection);
		}
	}
}

/* Set the suppress fib pending for the bgp configuration */
/*
 * 设置某实例的“suppress-fib-pending”标志（BGP_FLAG_SUPPRESS_FIB_PENDING）。
 * 与 bm_wait_for_fib_set 类似，但作用于单个实例，并维护全局计数
 * bgp_suppress_fib_count；同样只在计数跨越 0 时向 Zebra 发一次
 * ZEBRA_ROUTE_NOTIFY_REQUEST。view 实例无意义，直接返回。
 * 变更后重置该实例所有 peer 会话。
 */
void bgp_suppress_fib_pending_set(struct bgp *bgp, bool set)
{
	bool send_msg = false;
	struct peer *peer;
	struct listnode *node;

	if (bgp->inst_type == BGP_INSTANCE_TYPE_VIEW)
		return;

	/* Do nothing if already in a desired state */
	if (set == !!CHECK_FLAG(bgp->flags, BGP_FLAG_SUPPRESS_FIB_PENDING))
		return;

	if (set) {
		SET_FLAG(bgp->flags, BGP_FLAG_SUPPRESS_FIB_PENDING);
		/* Send msg to zebra for the first instance of bgp enabled
		 * with suppress fib
		 */
		if (bgp_suppress_fib_count == 0)
			send_msg = true;
		bgp_suppress_fib_count++;
	} else {
		UNSET_FLAG(bgp->flags, BGP_FLAG_SUPPRESS_FIB_PENDING);
		bgp_suppress_fib_count--;

		/* Send msg to zebra if there are no instances enabled
		 * with suppress fib
		 */
		if (bgp_suppress_fib_count == 0)
			send_msg = true;
	}
	/* Send route notify request to RIB */
	if (send_msg) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("Sending ZEBRA_ROUTE_NOTIFY_REQUEST");

		if (bgp_zclient)
			zebra_route_notify_send(ZEBRA_ROUTE_NOTIFY_REQUEST,
					bgp_zclient, set);
	}

	/*
	 * If this is configed at a time when peers are already set
	 * FRR needs to reset the connection as that some installs
	 * may have already happened in some shape fashion or form
	 * let's just start over
	 */
	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
		peer_set_last_reset(peer, PEER_DOWN_SUPPRESS_FIB_PENDING);

		if (!BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status))
			continue;

		peer_notify_config_change(peer->connection);
	}
}

/* BGP's cluster-id control. */
/*
 * 设置路由反射器（RR）的 cluster-id。cluster-id 用于 RR 防环：RR 给反射出去的
 * 路由打上 CLUSTER_LIST，同一 cluster 的 RR 收到含本 cluster-id 的路由就不再反射。
 * 设置后必须重置所有 IBGP 会话（因为 IBGP 路由的反射行为改变），并记录原因
 * PEER_DOWN_CLID_CHANGE。幂等：值未变则直接返回。
 */
void bgp_cluster_id_set(struct bgp *bgp, struct in_addr *cluster_id)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if (bgp_config_check(bgp, BGP_CONFIG_CLUSTER_ID)
	    && IPV4_ADDR_SAME(&bgp->cluster_id, cluster_id))
		return;

	IPV4_ADDR_COPY(&bgp->cluster_id, cluster_id);
	bgp_config_set(bgp, BGP_CONFIG_CLUSTER_ID);

	/* Clear all IBGP peer. */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		if (peer->sort != BGP_PEER_IBGP)
			continue;

		peer_set_last_reset(peer, PEER_DOWN_CLID_CHANGE);

		peer_notify_config_change(peer->connection);
	}
}

/*
 * 取消 cluster-id 配置：清零并清 config 位，随后重置所有 IBGP 会话。
 * 与 bgp_cluster_id_set 对称；未配置时直接返回。
 */
void bgp_cluster_id_unset(struct bgp *bgp)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if (!bgp_config_check(bgp, BGP_CONFIG_CLUSTER_ID))
		return;

	bgp->cluster_id.s_addr = 0;
	bgp_config_unset(bgp, BGP_CONFIG_CLUSTER_ID);

	/* Clear all IBGP peer. */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		if (peer->sort != BGP_PEER_IBGP)
			continue;

		peer_set_last_reset(peer, PEER_DOWN_CLID_CHANGE);

		peer_notify_config_change(peer->connection);
	}
}

/* BGP timer configuration.  */
/*
 * 设置实例级默认 BGP 定时器（keepalive / holdtime / connect-retry / delayopen），
 * 这些值是新建 peer 时的默认值（peer 可再单独覆盖）。
 *
 * @vty          : 若来自 CLI，非 NULL，用于打印告警（可为 NULL）。
 * @bgp          : 目标实例。
 * @keepalive    : 建议的 keepalive 间隔（秒）。
 * @holdtime     : 保持时间（秒）；keepalive 通常应为其 1/3。
 * @connect_retry: 连接重试间隔（秒）。
 * @delayopen    : DelayOpen 时间（秒）。
 *
 * 关键点：若 keepalive 大于 holdtime/3，会告警并把它压到 holdtime/3；但若
 * keepalive 为 0（表示采用 holdtime/3 的“默认推导”，见 default_keepalive 变量），
 * 则该变量保留推导值。最终写入 bgp->default_* 字段。
 */
void bgp_timers_set(struct vty *vty, struct bgp *bgp, uint32_t keepalive,
		    uint32_t holdtime, uint32_t connect_retry,
		    uint32_t delayopen)
{
	uint32_t default_keepalive = holdtime / 3;

	if (keepalive > default_keepalive) {
		if (vty)
			vty_out(vty,
				"%% keepalive value %u is larger than 1/3 of the holdtime, setting to %u\n",
				keepalive, default_keepalive);
	} else {
		default_keepalive = keepalive;
	}

	bgp->default_keepalive = default_keepalive;
	bgp->default_holdtime = holdtime;
	bgp->default_connect_retry = connect_retry;
	bgp->default_delayopen = delayopen;
}

/* mostly for completeness - CLI uses its own defaults */
/* 恢复实例级默认定时器到编译期默认值（DFLT_BGP_*）。 */
void bgp_timers_unset(struct bgp *bgp)
{
	bgp->default_keepalive = DFLT_BGP_KEEPALIVE;
	bgp->default_holdtime = DFLT_BGP_HOLDTIME;
	bgp->default_connect_retry = DFLT_BGP_CONNECT_RETRY;
	bgp->default_delayopen = BGP_DEFAULT_DELAYOPEN;
}

/*
 * 设置实例级 TCP keepalive 三参数（作用于底层 socket 的 SO_KEEPALIVE 细项）：
 * @keepalive_idle   : 空闲多久后开始探测（秒）。
 * @keepalive_intvl  : 探测间隔（秒）。
 * @keepalive_probes : 探测失败几次后判定连接断开。
 * 这些参数在 peer 建连时通过 bgp_socket 相关逻辑下发到套接字。
 */
void bgp_tcp_keepalive_set(struct bgp *bgp, uint16_t keepalive_idle,
			   uint16_t keepalive_intvl, uint16_t keepalive_probes)
{
	bgp->tcp_keepalive_idle = keepalive_idle;
	bgp->tcp_keepalive_intvl = keepalive_intvl;
	bgp->tcp_keepalive_probes = keepalive_probes;
}

/* 清零 TCP keepalive 参数（0 表示不启用/由系统默认）。 */
void bgp_tcp_keepalive_unset(struct bgp *bgp)
{
	bgp->tcp_keepalive_idle = 0;
	bgp->tcp_keepalive_intvl = 0;
	bgp->tcp_keepalive_probes = 0;
}

/* BGP confederation configuration.  */
/*
 * 设置本 AS 所属联邦（confederation）的编号（confed_id）。
 *
 * 联邦：把一个大 AS 拆成多个子 AS 对外呈现为一个大 AS。对本实例来说：
 *  - 成员 AS 被视为“内部”（IBGP 语义，保留 NEXT_HOP/LOCAL_PREF 等）；
 *  - 非成员外部 AS 才是真正的 EBGP。
 *
 * @bgp    : 目标实例。
 * @as     : 联邦编号（0 视为无效直接返回）。
 * @as_str : 该编号的“漂亮”字符串形式（用于展示，随 as-notation 变化），存副本。
 *
 * 会话处理逻辑依据“之前是否已处于联邦”：
 *  - already_confed==true（只是改编号）：仅重置远程 AS 为纯 EBGP 的会话。
 *  - already_confed==false（首次进入联邦）：重置所有非 IBGP 会话。
 * 每个被影响的 EBGP peer 会被更新 local_as，并复位其会话。
 */
void bgp_confederation_id_set(struct bgp *bgp, as_t as, const char *as_str)
{
	struct peer *peer;
	struct listnode *node, *nnode;
	int already_confed;

	if (as == 0)
		return;

	/* Remember - were we doing confederation before? */
	already_confed = bgp_config_check(bgp, BGP_CONFIG_CONFEDERATION);
	bgp->confed_id = as;
	if (bgp->confed_id_pretty)
		XFREE(MTYPE_BGP_NAME, bgp->confed_id_pretty);
	bgp->confed_id_pretty = XSTRDUP(MTYPE_BGP_NAME, as_str);
	bgp_config_set(bgp, BGP_CONFIG_CONFEDERATION);

	/* If we were doing confederation already, this is just an external
	   AS change.  Just Reset EBGP sessions, not CONFED sessions.  If we
	   were not doing confederation before, reset all EBGP sessions.  */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		enum bgp_peer_sort ptype = peer_sort(peer);

		/* We're looking for peers who's AS is not local or part of our
		   confederation.  */
		if (already_confed) {
			if (ptype == BGP_PEER_EBGP) {
				peer->local_as = as;
				if (peer_notify_config_change(peer->connection))
					peer_set_last_reset(peer, PEER_DOWN_CONFED_ID_CHANGE);
				else
					bgp_session_reset_safe(peer, &nnode);
			}
		} else {
			/* Not doing confederation before, so reset every
			   non-local
			   session */
			if (ptype != BGP_PEER_IBGP) {
				/* Reset the local_as to be our EBGP one */
				if (ptype == BGP_PEER_EBGP)
					peer->local_as = as;
				if (peer_notify_config_change(peer->connection))
					peer_set_last_reset(peer, PEER_DOWN_CONFED_ID_CHANGE);
				else
					bgp_session_reset_safe(peer, &nnode);
			}
		}
	}
	return;
}

/*
 * 取消联邦编号：confed_id 清零、释放字符串、清 config 位，并把所有非 IBGP
 * peer 的 local_as 恢复为本实例的 bgp->as，同时复位其会话。
 */
void bgp_confederation_id_unset(struct bgp *bgp)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	bgp->confed_id = 0;
	XFREE(MTYPE_BGP_NAME, bgp->confed_id_pretty);
	bgp_config_unset(bgp, BGP_CONFIG_CONFEDERATION);

	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		/* We're looking for peers who's AS is not local */
		if (peer_sort(peer) != BGP_PEER_IBGP) {
			peer->local_as = bgp->as;
			peer_set_last_reset(peer, PEER_DOWN_CONFED_ID_CHANGE);
			if (!peer_notify_config_change(peer->connection))
				bgp_session_reset_safe(peer, &nnode);
		}
	}
}

/* Is an AS part of the confed or not? */
/* 判断给定 AS 号是否属于本实例的联邦成员集合（线性扫描 confed_peers 数组）。 */
bool bgp_confederation_peers_check(struct bgp *bgp, as_t as)
{
	int i;

	if (!bgp)
		return false;

	for (i = 0; i < bgp->confed_peers_cnt; i++)
		if (bgp->confed_peers[i].as == as)
			return true;

	return false;
}

/* Add an AS to the confederation set.  */
/*
 * 把一个成员 AS 加入联邦集合：以 XREALLOC 在 confed_peers 数组尾部追加一项
 * （as + as_pretty 字符串副本）。已存在则幂等返回。
 * 若本实例已启用联邦：把那些远程 AS 恰等于 as 的 peer 的 local_as 改回本实例
 * 的 bgp->as（因为该 AS 现在是“内部”），重算 peer_sort，并复位其会话。
 */
void bgp_confederation_peers_add(struct bgp *bgp, as_t as, const char *as_str)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if (!bgp)
		return;

	if (bgp_confederation_peers_check(bgp, as))
		return;

	bgp->confed_peers = XREALLOC(MTYPE_BGP_CONFED_LIST, bgp->confed_peers,
				     (bgp->confed_peers_cnt + 1) *
					     sizeof(struct as_confed));

	bgp->confed_peers[bgp->confed_peers_cnt].as = as;
	bgp->confed_peers[bgp->confed_peers_cnt].as_pretty =
		XSTRDUP(MTYPE_BGP_NAME, as_str);
	bgp->confed_peers_cnt++;

	if (bgp_config_check(bgp, BGP_CONFIG_CONFEDERATION)) {
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			if (peer->as == as) {
				peer->local_as = bgp->as;
				(void)peer_sort(peer);
				if (peer_notify_config_change(peer->connection))
					peer_set_last_reset(peer, PEER_DOWN_CONFED_PEER_CHANGE);
				else
					bgp_session_reset_safe(peer, &nnode);
			}
		}
	}
}

/* Delete an AS from the confederation set.  */
/*
 * 从联邦集合中删除成员 AS as：先释放其 as_pretty 字符串，再把后续元素整体
 * 前移一位（数组删除），最后缩小/释放 confed_peers 数组。
 * 若本实例已启用联邦：把远程 AS 等于 as 的 peer 的 local_as 改为 confed_id
 * （该 AS 现在变回“外部”），重算排序并复位其会话。
 */
void bgp_confederation_peers_remove(struct bgp *bgp, as_t as)
{
	int i;
	int j;
	struct peer *peer;
	struct listnode *node, *nnode;

	if (!bgp)
		return;

	if (!bgp_confederation_peers_check(bgp, as))
		return;

	for (i = 0; i < bgp->confed_peers_cnt; i++)
		if (bgp->confed_peers[i].as == as) {
			XFREE(MTYPE_BGP_NAME, bgp->confed_peers[i].as_pretty);
			for (j = i + 1; j < bgp->confed_peers_cnt; j++) {
				bgp->confed_peers[j - 1].as =
					bgp->confed_peers[j].as;
				bgp->confed_peers[j - 1].as_pretty =
					bgp->confed_peers[j].as_pretty;
			}
		}

	bgp->confed_peers_cnt--;

	if (bgp->confed_peers_cnt == 0) {
		if (bgp->confed_peers)
			XFREE(MTYPE_BGP_CONFED_LIST, bgp->confed_peers);
		bgp->confed_peers = NULL;
	} else
		bgp->confed_peers = XREALLOC(
			MTYPE_BGP_CONFED_LIST, bgp->confed_peers,
			bgp->confed_peers_cnt * sizeof(struct as_confed));

	/* Now reset any peer who's remote AS has just been removed from the
	   CONFED */
	if (bgp_config_check(bgp, BGP_CONFIG_CONFEDERATION)) {
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			if (peer->as == as) {
				peer->local_as = bgp->confed_id;
				(void)peer_sort(peer);
				if (peer_notify_config_change(peer->connection))
					peer_set_last_reset(peer, PEER_DOWN_CONFED_PEER_CHANGE);
				else
					bgp_session_reset_safe(peer, &nnode);
			}
		}
	}
}

/* Local preference configuration.  */
/* 设置实例默认 LOCAL_PREF（用于本地始发路由；默认值见 BGP_DEFAULT_LOCAL_PREF）。 */
void bgp_default_local_preference_set(struct bgp *bgp, uint32_t local_pref)
{
	if (!bgp)
		return;

	bgp->default_local_pref = local_pref;
}

/* 恢复默认 LOCAL_PREF。 */
void bgp_default_local_preference_unset(struct bgp *bgp)
{
	if (!bgp)
		return;

	bgp->default_local_pref = BGP_DEFAULT_LOCAL_PREF;
}

/* Local preference configuration.  */
/*
 * 设置“子组（subgroup）报文队列上限”：当某 update-subgroup 待发报文积压超过
 * 该阈值时，FRR 会把该子组的 peer 拆到新子组/或触发热路径，防止某慢 peer 拖垮
 * 共享打包。属于 bgp_updgrp 性能调优参数，默认见
 * BGP_DEFAULT_SUBGROUP_PKT_QUEUE_MAX。
 */
void bgp_default_subgroup_pkt_queue_max_set(struct bgp *bgp,
					    uint32_t queue_size)
{
	if (!bgp)
		return;

	bgp->default_subgroup_pkt_queue_max = queue_size;
}

/* 恢复子组报文队列上限默认值。 */
void bgp_default_subgroup_pkt_queue_max_unset(struct bgp *bgp)
{
	if (!bgp)
		return;
	bgp->default_subgroup_pkt_queue_max =
		BGP_DEFAULT_SUBGROUP_PKT_QUEUE_MAX;
}

/* Listen limit configuration.  */
/*
 * 设置动态邻居（listen range）自动建立的 peer 数量上限，防止伪造连接耗尽资源。
 */
void bgp_listen_limit_set(struct bgp *bgp, int listen_limit)
{
	if (!bgp)
		return;

	bgp->dynamic_neighbors_limit = listen_limit;
}

/* 恢复动态邻居数量上限默认值（BGP_DYNAMIC_NEIGHBORS_LIMIT_DEFAULT）。 */
void bgp_listen_limit_unset(struct bgp *bgp)
{
	if (!bgp)
		return;

	bgp->dynamic_neighbors_limit = BGP_DYNAMIC_NEIGHBORS_LIMIT_DEFAULT;
}

/*
 * 把“线格式/IANA 定义的” AFI/SAFI 数值映射为 FRR 内部枚举（afi_t/safi_t）。
 * 用于解析对端 OPEN/UPDATE 里的能力与 MP_REACH 等字段（线上是 IANA 数值）。
 * 任一映射失败（得到 AFI_MAX/SAFI_MAX）返回 -1，调用者据此判定“不支持该族”。
 */
int bgp_map_afi_safi_iana2int(iana_afi_t pkt_afi, iana_safi_t pkt_safi,
			      afi_t *afi, safi_t *safi)
{
	/* Map from IANA values to internal values, return error if
	 * values are unrecognized.
	 */
	*afi = afi_iana2int(pkt_afi);
	*safi = safi_iana2int(pkt_safi);
	if (*afi == AFI_MAX || *safi == SAFI_MAX)
		return -1;

	return 0;
}

/*
 * 上函数的逆映射：把 FRR 内部 AFI/SAFI 枚举转回 IANA 数值，用于组包/编码。
 * 内部枚举非法（AFI_MAX/SAFI_MAX）时返回 -1。
 */
int bgp_map_afi_safi_int2iana(afi_t afi, safi_t safi, iana_afi_t *pkt_afi,
			      iana_safi_t *pkt_safi)
{
	/* Map from internal values to IANA values, return error if
	 * internal values are bad (unexpected).
	 */
	if (afi == AFI_MAX || safi == SAFI_MAX)
		return -1;
	*pkt_afi = afi_int2iana(afi);
	*pkt_safi = safi_int2iana(safi);
	return 0;
}

/* ========== 3. peer_af（peer 的地址族激活单元）与 peer 排序/哈希 ========== */

/*
 * 为一个 peer 创建其“某地址族（afi,safi）激活单元” struct peer_af。
 *
 * peer_af 是 (peer × afi × safi) 维度的激活记录：保存该 peer 在该地址族下是否
 * 激活、通告/接收相关标志、以及指向 update-group 的成员关系。peer 用定长数组
 * peer_af_array[BGP_AF_MAX] 按 afindex(afi,safi) 索引。
 *
 * @peer : 目标 peer。
 * @afi/@safi : 地址族。
 * @return : 新 peer_af 指针；peer 为空或 afid 越界返回 NULL。
 *
 * 副作用：分配 peer_af 存入 peer->peer_af_array[afid]，并把所在实例的
 * bgp->af_peer_count[afi][safi] 计数 +1。
 * 断言 peer_af_array[afid] 必须为空——即同一族不可重复创建。
 */
struct peer_af *peer_af_create(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer_af *af;
	int afid;
	struct bgp *bgp;

	if (!peer)
		return NULL;

	afid = afindex(afi, safi);
	if (afid >= BGP_AF_MAX)
		return NULL;

	bgp = peer->bgp;
	assert(peer->peer_af_array[afid] == NULL);

	/* Allocate new peer af */
	af = XCALLOC(MTYPE_BGP_PEER_AF, sizeof(struct peer_af));

	peer->peer_af_array[afid] = af;
	af->afi = afi;
	af->safi = safi;
	af->afid = afid;
	af->peer = peer;
	bgp->af_peer_count[afi][safi]++;

	return af;
}

/* 查找 peer 在某地址族下的 peer_af；未激活/越界则返回 NULL。 */
struct peer_af *peer_af_find(struct peer *peer, afi_t afi, safi_t safi)
{
	int afid;

	if (!peer)
		return NULL;

	afid = afindex(afi, safi);
	if (afid >= BGP_AF_MAX)
		return NULL;

	return peer->peer_af_array[afid];
}

/*
 * 销毁 peer 在某地址族的 peer_af 并清理相关资源：
 *  1) 取消该 peer 在该族上可能挂起的“软重配”任务（bgp_soft_reconfig_table_task_cancel）；
 *  2) 停止该族的 announce-route 定时器（bgp_stop_announce_route_timer）；
 *  3) 若已加入 update-subgroup，则从子组中摘除（update_subgroup_remove_peer）——
 *     这一步很关键，否则子组会持有悬空 peer_af 指针；
 *  4) 递减实例的 af_peer_count，清空数组槽并释放内存。
 * @return : 0 成功；peer 为空/越界/未激活返回 -1。
 * 调用时机：peer_deactivate 或 peer_delete 时对每个已激活族调用。
 */
int peer_af_delete(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer_af *af;
	int afid;
	struct bgp *bgp;

	if (!peer)
		return -1;

	afid = afindex(afi, safi);
	if (afid >= BGP_AF_MAX)
		return -1;

	af = peer->peer_af_array[afid];
	if (!af)
		return -1;

	bgp = peer->bgp;
	bgp_soft_reconfig_table_task_cancel(bgp, bgp->rib[afi][safi], peer);

	bgp_stop_announce_route_timer(af);

	if (PAF_SUBGRP(af)) {
		if (BGP_DEBUG(update_groups, UPDATE_GROUPS))
			zlog_debug("u%" PRIu64 ":s%" PRIu64 " remove peer %s",
				   af->subgroup->update_group->id,
				   af->subgroup->id, peer->host);
	}


	update_subgroup_remove_peer(af->subgroup, af);

	if (bgp->af_peer_count[afi][safi])
		bgp->af_peer_count[afi][safi]--;

	peer->peer_af_array[afid] = NULL;
	XFREE(MTYPE_BGP_PEER_AF, af);
	return 0;
}

/* Peer comparison function for sorting.  */
/*
 * peer 的排序比较函数，用于 bgp->peer 链表按“配置展示顺序”排序。
 * 排序键优先级（决定 show running 与遍历顺序）：
 *   1) 有 peer-group 的排在无组的之前（组内再按成员）；
 *   2) 同组内：有 conf_if（接口邻居）的排在前，按接口名比较；
 *   3) 不同组：按组名 strcmp；
 *   4) 兜底按连接的对端地址 sockunion_cmp。
 * 返回值语义与 strcmp 一致（<0/0/>0）。
 */
int peer_cmp(struct peer *p1, struct peer *p2)
{
	if (p1->group && !p2->group)
		return -1;

	if (!p1->group && p2->group)
		return 1;

	if (p1->group == p2->group) {
		if (p1->conf_if && !p2->conf_if)
			return -1;

		if (!p1->conf_if && p2->conf_if)
			return 1;

		if (p1->conf_if && p2->conf_if)
			return if_cmp_name_func(p1->conf_if, p2->conf_if);
	} else
		return strcmp(p1->group->name, p2->group->name);

	return sockunion_cmp(&p1->connection->su, &p2->connection->su);
}

/* 连接哈希表的键生成：以对端地址（sockunion）为哈希键。 */
static unsigned int connection_hash_key_make(const void *p)
{
	const struct peer_connection *connection = p;

	return sockunion_hash(&connection->su);
}

/*
 * 连接哈希表的相等判定：地址相同、且两个 peer 的“是否配置节点”属性一致。
 * 之所以要比较 peer_is_config_node，是为了让“已配置 peer 的连接”与
 * “动态邻居/分身（未配置）的连接”即使地址相同也互不覆盖（各自独立一条）。
 */
static bool connection_hash_same(const void *p1, const void *p2)
{
	const struct peer_connection *c1 = p1;
	const struct peer_connection *c2 = p2;
	const struct peer *peer1 = c1->peer;
	const struct peer *peer2 = c2->peer;

	return (sockunion_same(&c1->su, &c2->su) &&
		peer_is_config_node(peer1) == peer_is_config_node(peer2));
}

/*
 * 【继承三件套核心】把 peer-group 的某单个标志位“继承”到成员 peer 上。
 *
 * 背景模型（务必配合 bgpd.h 的 peer 结构注释理解）：
 *   - peer->flags              : peer 当前“生效”的一组标志位；
 *   - peer->flags_override     : 记录“哪些位是本 peer 显式设置的、不随组变化”；
 *   - peer->flags_invert       : 记录“哪些位相对组值被取反（invert）”；
 *   - peer->group->conf->flags : 组的基准值（同样有 flags_invert）。
 * 组合规则（组值 g，其中 g' = g 再叠加组的 invert）：
 *   若 本 peer 未 override：peer->flags = (组值或取反)；否则保留本 peer 自身值。
 *
 * 本函数做的事（针对单个 flag）：
 *   1) 非组成员直接返回（无从继承）。
 *   2) 清除 peer->flags_override 中的该位——表示“重新回到跟随组”的状态。
 *   3) 计算组值 group_val，并按本地/组的 invert 决定写入的是组值还是其反值。
 *
 * 注意条件 `!组invert && 本地invert`：只有当“组没把该位取反、而本地把它取反了”
 * 时，才写成 !group_val；否则直接写成组值。这就是 flags_invert 的语义。
 */
void peer_flag_inherit(struct peer *peer, uint64_t flag)
{
	bool group_val;

	/* Skip if peer is not a peer-group member. */
	if (!peer_group_active(peer))
		return;

	/* Unset override flag to signal inheritance from peer-group. */
	UNSET_FLAG(peer->flags_override, flag);

	/*
	 * Inherit flag state from peer-group. If the flag of the peer-group is
	 * not being inverted, the peer must inherit the inverse of the current
	 * peer-group flag state.
	 */
	group_val = CHECK_FLAG(peer->group->conf->flags, flag);
	if (!CHECK_FLAG(peer->group->conf->flags_invert, flag)
	    && CHECK_FLAG(peer->flags_invert, flag))
		COND_FLAG(peer->flags, flag, !group_val);
	else
		COND_FLAG(peer->flags, flag, group_val);
}

/* 查询 peer 在某地址族下的某个 af 级标志是否置位。返回非 0 表示已设置。 */
bool peer_af_flag_check(struct peer *peer, afi_t afi, safi_t safi,
			uint64_t flag)
{
	return !!CHECK_FLAG(peer->af_flags[afi][safi], flag);
}

/*
 * 【继承三件套核心·地址族版】与 peer_flag_inherit 完全同构，但作用于“每地址族的
 * 标志集合”：peer->af_flags[afi][safi] / af_flags_override[afi][safi] /
 * af_flags_invert[afi][safi]，基准来自 peer->group->conf->af_flags[afi][safi]。
 * 语义：清除本 peer 在该族该 flag 上的 override，令其重新跟随组值（含 invert）。
 */
void peer_af_flag_inherit(struct peer *peer, afi_t afi, safi_t safi,
			  uint64_t flag)
{
	bool group_val;

	/* Skip if peer is not a peer-group member. */
	if (!peer_group_active(peer))
		return;

	/* Unset override flag to signal inheritance from peer-group. */
	UNSET_FLAG(peer->af_flags_override[afi][safi], flag);

	/*
	 * Inherit flag state from peer-group. If the flag of the peer-group is
	 * not being inverted, the peer must inherit the inverse of the current
	 * peer-group flag state.
	 */
	group_val = CHECK_FLAG(peer->group->conf->af_flags[afi][safi], flag);
	if (!CHECK_FLAG(peer->group->conf->af_flags_invert[afi][safi], flag)
	    && CHECK_FLAG(peer->af_flags_invert[afi][safi], flag))
		COND_FLAG(peer->af_flags[afi][safi], flag, !group_val);
	else
		COND_FLAG(peer->af_flags[afi][safi], flag, group_val);
}

/* Check peer's AS number and determines if this peer is IBGP or EBGP */
/*
 * 根据 AS 关系计算 peer 的类型（IBGP / EBGP / CONFED / INTERNAL / UNSPECIFIED），
 * 这是 BGP 一切决策的基础分类（影响下一跳处理、AS_PATH、LOCAL_PREF 传播等）。
 *
 * 局部 AS 取 change_local_as（peer 显式 `local-as`）优先，否则 peer->local_as。
 * 判定要点：
 *  - peer-group 自身：as_type 含 INTERNAL→IBGP；含 EXTERNAL→EBGP；指定 AS 则与
 *    局部 AS 比较；否则回退看组内第一个成员；都无信息返回 INTERNAL。
 *  - 普通 peer + 联邦：有一套专门的联邦判定（同一 AS 时区分 confed_id），
 *    若远程 AS 是联邦成员返回 CONFED，否则 EBGP。
 *  - 无联邦：as_type==AS_UNSPECIFIED 时可能从组的 AS 信息推断；明确 INTERNAL/EXTERNAL
 *    直接给结论；最后按“局部分配 0→INTERNAL，否则同 AS→IBGP，不同→EBGP”。
 * 注意返回 BGP_PEER_UNSPECIFIED 表示“信息不足”，调用者需据此跳过某些处理。
 */
static inline enum bgp_peer_sort peer_calc_sort(struct peer *peer)
{
	struct bgp *bgp;
	as_t local_as;

	bgp = peer->bgp;

	if (peer->change_local_as)
		local_as = peer->change_local_as;
	else
		local_as = peer->local_as;

	/* Peer-group */
	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (CHECK_FLAG(peer->as_type, AS_INTERNAL))
			return BGP_PEER_IBGP;

		if (CHECK_FLAG(peer->as_type, AS_EXTERNAL))
			return BGP_PEER_EBGP;

		else if (peer->as_type == AS_SPECIFIED && peer->as) {
			assert(bgp);
			return (local_as == peer->as ? BGP_PEER_IBGP
						     : BGP_PEER_EBGP);
		}

		else {
			struct peer *peer1;

			assert(peer->group);
			peer1 = listnode_head(peer->group->peer);

			if (peer1) {
				/* AS_AUTO without INTERNAL/EXTERNAL bits means
				 * the type is not yet resolved (no session up).
				 * Treat as unestablished so it doesn't block
				 * other members from being configured.
				 */
				if (peer1->as_type == AS_AUTO)
					return BGP_PEER_INTERNAL;
				return peer1->sort;
			}
		}
		return BGP_PEER_INTERNAL;
	}

	/* Normal peer */
	if (bgp && CHECK_FLAG(bgp->config, BGP_CONFIG_CONFEDERATION)) {
		if (local_as == 0)
			return BGP_PEER_INTERNAL;

		if (local_as == peer->as) {
			if (bgp->as == bgp->confed_id) {
				if (local_as == bgp->as)
					return BGP_PEER_IBGP;
				else
					return BGP_PEER_EBGP;
			} else {
				if (local_as == bgp->confed_id)
					return BGP_PEER_EBGP;
				else
					return BGP_PEER_IBGP;
			}
		}

		if (bgp_confederation_peers_check(bgp, peer->as))
			return BGP_PEER_CONFED;

		return BGP_PEER_EBGP;
	} else {
		if (peer->as_type == AS_UNSPECIFIED) {
			/* check if in peer-group with AS information */
			if (peer->group
			    && (peer->group->conf->as_type != AS_UNSPECIFIED)) {
				if (peer->group->conf->as_type
				    == AS_SPECIFIED) {
					if (local_as == peer->group->conf->as)
						return BGP_PEER_IBGP;
					else
						return BGP_PEER_EBGP;
				} else if (CHECK_FLAG(peer->group->conf->as_type,
						      AS_INTERNAL))
					return BGP_PEER_IBGP;
				else
					return BGP_PEER_EBGP;
			}
			/* no AS information anywhere, let caller know */
			return BGP_PEER_UNSPECIFIED;
		} else if (peer->as_type != AS_SPECIFIED) {
			if (CHECK_FLAG(peer->as_type, AS_INTERNAL))
				return BGP_PEER_IBGP;
			else if (CHECK_FLAG(peer->as_type, AS_EXTERNAL))
				return BGP_PEER_EBGP;
		}

		return (local_as == 0 ? BGP_PEER_INTERNAL
				      : local_as == peer->as ? BGP_PEER_IBGP
							     : BGP_PEER_EBGP);
	}
}

/* Calculate and cache the peer "sort" */
/* 重新计算并“缓存”peer 的类型到 peer->sort，再返回。凡可能影响分类的配置变更
 * （改 AS、local-as、联邦等）后都必须调用它刷新缓存。 */
enum bgp_peer_sort peer_sort(struct peer *peer)
{
	peer->sort = peer_calc_sort(peer);
	return peer->sort;
}

/* 只读取缓存值 peer->sort 而不重算（要求此前已调用过 peer_sort）。 */
enum bgp_peer_sort peer_sort_lookup(struct peer *peer)
{
	return peer->sort;
}

/*
 * Mutex will be freed in peer_connection_free
 * this is a convenience function to reduce cut-n-paste
 */
/*
 * 释放一个连接的收发缓冲：ibuf/obuf（stream_fifo）、ibuf_work（ringbuf 工作缓冲）
 * 以及当前正在解析中的 curr stream。这些缓冲会被 IO 线程访问，所以释放前用
 * connection->io_mtx 加锁。注意：mutex 本身不在这里销毁（在 peer_connection_free
 * 里销毁），本函数只清缓冲。
 */
void bgp_peer_connection_buffers_free(struct peer_connection *connection)
{
	frr_with_mutex (&connection->io_mtx) {
		if (connection->ibuf) {
			stream_fifo_free(connection->ibuf);
			connection->ibuf = NULL;
		}

		if (connection->obuf) {
			stream_fifo_free(connection->obuf);
			connection->obuf = NULL;
		}

		if (connection->ibuf_work) {
			ringbuf_del(connection->ibuf_work);
			connection->ibuf_work = NULL;
		}
	}

	if (connection->curr) {
		stream_free(connection->curr);
		connection->curr = NULL;
	}
}

/*
 * 彻底释放一个 peer_connection：先释放缓冲，再销毁 io_mtx，清零内存后 XFREE，
 * 并把调用者持有的指针置空（注意此处 `connection = NULL` 只置空局部形参副本，
 * 真正的调用者指针需自行处理——这是该函数的易误解点）。
 */
void bgp_peer_connection_free(struct peer_connection **connection)
{
	bgp_peer_connection_buffers_free(*connection);
	pthread_mutex_destroy(&(*connection)->io_mtx);

	memset(*connection, 0, sizeof(struct peer_connection));
	XFREE(MTYPE_BGP_PEER_CONNECTION, *connection);

	connection = NULL;
}

/* 把连接方向枚举转成可读字符串，用于日志/show 输出。 */
const char *bgp_peer_get_connection_direction_string(const struct peer_connection *connection)
{
	switch (connection->dir) {
	case UNKNOWN:
		return "Unknown";
	case CONNECTION_INCOMING:
		return "Incoming";
	case CONNECTION_OUTGOING:
		return "Outgoing";
	case ESTABLISHED:
		return "Established";
	}

	assert(!"DEV Escape: Expected switch to take care of this state");
	return "DEV ESCAPE";
}

/*
 * 分配并初始化一个 peer_connection（连接对象）。一个 peer 拥有一个 connection，
 * 它承载套接字 fd、收发 FIFO、FSM 状态（status/ostatus）、各类定时器等。
 *
 * @peer : 归属的 peer（反向指针）。
 * @su   : 对端地址（可为 NULL；如接口邻居在确定地址前）。
 * @dir  : 连接方向（incoming/outgoing）。
 * @return : 新连接对象（XCALLOC 已清零，再逐项初始化）。
 *
 * 关键点：ibuf/obuf 是 stream_fifo（收发报文队列），io_mtx 保护它们与 IO 线程
 * 并发；ibuf_work 用“1.5 倍最大报文”的 ringbuf，是为了在收 UPDATE 并按出向
 * route-map 追加大量属性时留出余量（避免逐属性边界检查）。初始 status=Idle。
 */
struct peer_connection *bgp_peer_connection_new(struct peer *peer, const union sockunion *su,
						enum connection_direction dir)
{
	struct peer_connection *connection;

	connection = XCALLOC(MTYPE_BGP_PEER_CONNECTION,
			     sizeof(struct peer_connection));

	connection->dir = dir;

	if (su)
		connection->su = *su;

	connection->peer = peer;
	connection->fd = -1;

	connection->ibuf = stream_fifo_new();
	connection->obuf = stream_fifo_new();
	pthread_mutex_init(&connection->io_mtx, NULL);

	/* We use a larger buffer for peer->obuf_work in the event that:
	 * - We RX a BGP_UPDATE where the attributes alone are just
	 *   under BGP_EXTENDED_MESSAGE_MAX_PACKET_SIZE.
	 * - The user configures an outbound route-map that does many as-path
	 *   prepends or adds many communities. At most they can have
	 *   CMD_ARGC_MAX args in a route-map so there is a finite limit on how
	 *   large they can make the attributes.
	 *
	 * Having a buffer with BGP_MAX_PACKET_SIZE_OVERFLOW allows us to avoid
	 * bounds checking for every single attribute as we construct an
	 * UPDATE.
	 */
	connection->ibuf_work =
		ringbuf_new(BGP_MAX_PACKET_SIZE + BGP_MAX_PACKET_SIZE / 2);

	connection->status = Idle;
	connection->ostatus = Idle;

	return connection;
}

/*
 * 真正释放 struct peer 的底层函数（仅供 peer_unlock 在其引用计数归零时调用）。
 *
 * 前提：connection->status 必须是 Deleted（已由 peer_delete/bgp_stop 走过停机流程），
 * 否则断言失败——这是防止在连接仍活跃时误释放的保护。
 *
 * 主要动作：
 *  1) 取消防卫性地再停一遍所有 IO/定时器（本应在 bgp_stop 已做，这里“以防万一”）；
 *     尤其是 t_generate_updgrp_packets 这类发送调度定时器。
 *  2) 删除该 peer 关联的“connected nexthop”路由（仅对已配置且非动态邻居）。
 *  3) 释放所有 per-afi 过滤器名、shutdown 消息、host/hostname/域名/接口名、
 *     update-source/if、notify 数据、清理工作队列、conf_if、soft_version、
 *     BFD 配置、addpath 类型、local-as/as 美化字符串等。
 *  4) 释放 connection（bgp_peer_connection_free 内部会销毁 io_mtx）。
 *  5) bgp_unlock(peer->bgp)：peer 持有实例的一个引用，此处归还。
 *  6) 清零整块内存后 XFREE——防止悬垂指针被误用。
 *
 * 注意：peer->lock 计数归零前的对象生命周期管理见 peer_lock/peer_unlock。
 */
static void peer_free(struct peer *peer)
{
	afi_t afi;
	safi_t safi;
	struct peer_connection *connection = peer->connection;

	assert(connection->status == Deleted);

	QOBJ_UNREG(peer);

	/* this /ought/ to have been done already through bgp_stop earlier,
	 * but just to be sure..
	 */
	bgp_timer_set(connection);
	bgp_reads_off(connection);
	bgp_writes_off(connection);
	event_cancel(&connection->t_start);
	event_cancel(&connection->t_connect);
	event_cancel(&connection->t_holdtime);
	event_cancel(&connection->t_routeadv);
	event_cancel(&connection->t_delayopen);
	event_cancel(&connection->t_connect_check_r);
	event_cancel(&connection->t_connect_check_w);
	event_cancel(&connection->t_stop_with_notify);
	event_cancel(&connection->t_gr_restart);
	event_cancel(&connection->t_gr_stale);
	event_cancel(&connection->t_pmax_restart);
	event_cancel(&connection->t_generate_updgrp_packets);
	event_cancel(&connection->t_stop_with_notify);

	event_cancel_event_ready(bm->master, connection);
	assert(!connection->t_write);
	assert(!connection->t_read);

	/* Free connected nexthop, if present */
	if (peer_is_config_node(peer) && !peer_dynamic_neighbor(peer))
		bgp_delete_connected_nexthop(family2afi(connection->su.sa.sa_family), peer);

	FOREACH_AFI_SAFI (afi, safi) {
		if (peer->filter[afi][safi].advmap.aname)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      peer->filter[afi][safi].advmap.aname);
		if (peer->filter[afi][safi].advmap.cname)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      peer->filter[afi][safi].advmap.cname);
	}

	XFREE(MTYPE_PEER_TX_SHUTDOWN_MSG, peer->tx_shutdown_message);

	XFREE(MTYPE_PEER_DESC, peer->desc);
	XFREE(MTYPE_BGP_PEER_HOST, peer->host);
	XFREE(MTYPE_BGP_PEER_HOST, peer->hostname);
	XFREE(MTYPE_BGP_PEER_HOST, peer->domainname);
	XFREE(MTYPE_BGP_PEER_IFNAME, peer->ifname);

	/* Update source configuration.  */
	if (peer->update_source) {
		sockunion_free(peer->update_source);
		peer->update_source = NULL;
	}

	XFREE(MTYPE_PEER_UPDATE_SOURCE, peer->update_if);

	XFREE(MTYPE_BGP_NOTIFICATION, peer->notify.data);
	memset(&peer->notify, 0, sizeof(struct bgp_notify));

	if (peer->clear_node_queue)
		work_queue_free_and_null(&peer->clear_node_queue);

	XFREE(MTYPE_PEER_CONF_IF, peer->conf_if);

	XFREE(MTYPE_BGP_SOFT_VERSION, peer->soft_version);

	/* Remove BFD configuration. */
	if (peer->bfd_config)
		bgp_peer_remove_bfd_config(peer);

	FOREACH_AFI_SAFI (afi, safi)
		bgp_addpath_set_peer_type(peer, afi, safi, BGP_ADDPATH_NONE, 0);

	if (peer->change_local_as_pretty)
		XFREE(MTYPE_BGP_NAME, peer->change_local_as_pretty);
	if (peer->as_pretty)
		XFREE(MTYPE_BGP_NAME, peer->as_pretty);

	bgp_peer_connection_free(&peer->connection);

	bgp_unlock(peer->bgp);

	stream_free(peer->last_reset_cause);

	memset(peer, 0, sizeof(struct peer));

	XFREE(MTYPE_BGP_PEER, peer);
}

/* increase reference count on a struct peer */
/*
 * peer 引用计数 +1（配合 peer_unlock 使用）。@name 仅用于 trace 溯源调试。
 * 断言 peer 非空且计数不为负；返回同一 peer 便于链式使用。
 */
struct peer *peer_lock_with_caller(const char *name, struct peer *peer)
{
	frrtrace(2, frr_bgp, bgp_peer_lock, peer, name);
	assert(peer && (peer->lock >= 0));

	peer->lock++;

	return peer;
}

/* decrease reference count on a struct peer
 * struct peer is freed and NULL returned if last reference
 */
/*
 * peer 引用计数 -1。若计数归零则调用 peer_free 真正释放对象并返回 NULL；
 * 否则返回 peer 本身。调用者必须用返回值覆盖其保存的指针（可能已被释放）。
 */
struct peer *peer_unlock_with_caller(const char *name, struct peer *peer)
{
	frrtrace(2, frr_bgp, bgp_peer_unlock, peer, name);
	assert(peer && (peer->lock > 0));

	peer->lock--;

	if (peer->lock == 0) {
		peer_free(peer);
		return NULL;
	}

	return peer;
}
/* BGP GR changes */

/*
 * 初始化实例的“全局 Graceful Restart（GR）”状态机与相关参数。
 *
 * 实现方式：在栈上构造一张二维状态迁移表 local_GLOBAL_GR_FSM[MODE][EVENT]，
 * 再 memcpy 到 bgp->GLOBAL_GR_FSM。表按“当前模式”×“收到的命令”给出“下一模式”：
 *   MODE:   GLOBAL_HELPER / GLOBAL_GR / GLOBAL_DISABLE / GLOBAL_INVALID
 *   EVENT:  GLOBAL_GR_cmd / no_Global_GR_cmd / GLOBAL_DISABLE_cmd / no_Global_Disable_cmd
 * 例如“在 HELPER 模式收到 GLOBAL_GR_cmd → 进入 GLOBAL_GR”。
 * GLOBAL_INVALID 是吸收态（其余全为 INVALID）。
 *
 * 之后按 bm（进程级）的 GR 配置继承初始化初始模式与各时间参数：
 *   BM_FLAG_GR_RESTARTER       → 初始 GR（本机作为 restarter，重启后要保留转发表）
 *   BM_FLAG_GR_DISABLED        → 初始 DISABLE
 *   否则                        → 初始 HELPER（作为 helper 帮助邻居重启）
 * 并把进程级非默认的 restart/stalepath/select-defer/rib-stale 时间、GR preserve-fwd、
 * ipv6-no-auto-ra 等下沉到实例；最后把 zebra GR 状态设为 DISABLE。
 */
int bgp_global_gr_init(struct bgp *bgp)
{
	int local_GLOBAL_GR_FSM[BGP_GLOBAL_GR_MODE][BGP_GLOBAL_GR_EVENT_CMD] = {
		/* GLOBAL_HELPER Mode  */
		{
		/*Event -> */
		/*GLOBAL_GR_cmd*/  /*no_Global_GR_cmd*/
			GLOBAL_GR,      GLOBAL_INVALID,
		/*GLOBAL_DISABLE_cmd*/ /*no_Global_Disable_cmd*/
			GLOBAL_DISABLE, GLOBAL_INVALID
		},
		/* GLOBAL_GR Mode */
		{
		/*Event -> */
		/*GLOBAL_GR_cmd*/ /*no_Global_GR_cmd*/
			GLOBAL_GR,  GLOBAL_HELPER,
		/*GLOBAL_DISABLE_cmd*/ /*no_Global_Disable_cmd*/
			GLOBAL_DISABLE,  GLOBAL_INVALID
		},
		/* GLOBAL_DISABLE Mode  */
		{
		/*Event -> */
		/*GLOBAL_GR_cmd */	/*no_Global_GR_cmd*/
			GLOBAL_GR,      GLOBAL_INVALID,
		/*GLOBAL_DISABLE_cmd*//*no_Global_Disable_cmd*/
			GLOBAL_DISABLE,	GLOBAL_HELPER
		},
		/* GLOBAL_INVALID Mode  */
		{
		/*Event -> */
		/*GLOBAL_GR_cmd*/	/*no_Global_GR_cmd*/
			GLOBAL_INVALID, GLOBAL_INVALID,
		/*GLOBAL_DISABLE_cmd*/ /*no_Global_Disable_cmd*/
			GLOBAL_INVALID, GLOBAL_INVALID
		}
	};
	memcpy(bgp->GLOBAL_GR_FSM, local_GLOBAL_GR_FSM,
					sizeof(local_GLOBAL_GR_FSM));

	/* Inherit any BGP-wide configuration. */
	if (CHECK_FLAG(bm->flags, BM_FLAG_GR_RESTARTER))
		bgp->global_gr_present_state = GLOBAL_GR;
	else if (CHECK_FLAG(bm->flags, BM_FLAG_GR_DISABLED))
		bgp->global_gr_present_state = GLOBAL_DISABLE;
	else
		bgp->global_gr_present_state = GLOBAL_HELPER;

	if (bm->restart_time != BGP_DEFAULT_RESTART_TIME)
		bgp->restart_time = bm->restart_time;
	if (bm->stalepath_time != BGP_DEFAULT_STALEPATH_TIME)
		bgp->stalepath_time = bm->stalepath_time;
	if (bm->select_defer_time != BGP_DEFAULT_SELECT_DEFERRAL_TIME)
		bgp->select_defer_time = bm->select_defer_time;
	if (bm->rib_stale_time != BGP_DEFAULT_RIB_STALE_TIME)
		bgp->rib_stale_time = bm->rib_stale_time;
	if (CHECK_FLAG(bm->flags, BM_FLAG_GR_PRESERVE_FWD))
		SET_FLAG(bgp->flags, BGP_FLAG_GR_PRESERVE_FWD);
	if (CHECK_FLAG(bm->flags, BM_FLAG_IPV6_NO_AUTO_RA))
		SET_FLAG(bgp->flags, BGP_FLAG_IPV6_NO_AUTO_RA);

	bgp->present_zebra_gr_state = ZEBRA_GR_DISABLE;

	if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
		zlog_debug("%s: Global GR state is %s", bgp->name_pretty,
			   print_global_gr_mode(bgp->global_gr_present_state));

	return BGP_GR_SUCCESS;
}

/*
 * 初始化单个 peer 的 GR 状态机（PEER_GR_FSM）。同样是“栈上构造二维表再 memcpy”：
 * 表元素不仅给出下一模式，还附带一个“动作回调”（如 bgp_peer_gr_action），
 * 以及 NULL 表示无动作。状态包括：
 *   PEER_HELPER / PEER_GR / PEER_DISABLE / PEER_INVALID / PEER_GLOBAL_INHERIT
 * 事件包括：PEER_GR_cmd / no_PEER_GR_cmd / PEER_DISABLE_cmd / no_PEER_DISABLE_cmd /
 *          PEER_HELPER_cmd / no_PEER_HELPER_cmd
 * 其中 PEER_GLOBAL_INHERIT 表示“跟随实例级 GR 模式”。
 * 初始化完成后，把 peer 置为 PEER_GLOBAL_INHERIT 模式（走 bgp_peer_move_to_gr_mode）。
 */
int bgp_peer_gr_init(struct peer *peer)
{
	struct bgp_peer_gr local_Peer_GR_FSM[BGP_PEER_GR_MODE]
					[BGP_PEER_GR_EVENT_CMD] = {
	{
	/*	PEER_HELPER Mode	*/
	/* Event-> */ /* PEER_GR_CMD */ /* NO_PEER_GR_CMD */
		{ PEER_GR, bgp_peer_gr_action }, {PEER_INVALID, NULL },
	/* Event-> */ /* PEER_DISABLE_CMD */ /* NO_PEER_DISABLE_CMD */
		{PEER_DISABLE, bgp_peer_gr_action }, {PEER_INVALID, NULL },
	/* Event-> */ /* PEER_HELPER_cmd */ /* NO_PEER_HELPER_CMD */
		{ PEER_HELPER, NULL }, {PEER_GLOBAL_INHERIT,
						bgp_peer_gr_action }
	},
	{
	/*	PEER_GR Mode	*/
	/* Event-> */ /* PEER_GR_CMD */ /* NO_PEER_GR_CMD */
		{ PEER_GR, NULL }, { PEER_GLOBAL_INHERIT,
						bgp_peer_gr_action },
	/* Event-> */ /* PEER_DISABLE_CMD */ /* NO_PEER_DISABLE_CMD */
		{PEER_DISABLE, bgp_peer_gr_action }, { PEER_INVALID, NULL },
	/* Event-> */ /* PEER_HELPER_cmd */ /* NO_PEER_HELPER_CMD */
		{ PEER_HELPER, bgp_peer_gr_action }, { PEER_INVALID, NULL }
	},
	{
	/*	PEER_DISABLE Mode	*/
	/* Event-> */ /* PEER_GR_CMD */ /* NO_PEER_GR_CMD */
		{ PEER_GR, bgp_peer_gr_action }, { PEER_INVALID, NULL },
	/* Event-> */ /* PEER_DISABLE_CMD */ /* NO_PEER_DISABLE_CMD */
		{ PEER_DISABLE, NULL }, { PEER_GLOBAL_INHERIT,
						bgp_peer_gr_action },
	/* Event-> */ /* PEER_HELPER_cmd */  /* NO_PEER_HELPER_CMD */
		{ PEER_HELPER, bgp_peer_gr_action }, { PEER_INVALID, NULL }
	},
	{
	/*	PEER_INVALID Mode	*/
	/* Event-> */ /* PEER_GR_CMD */  /* NO_PEER_GR_CMD */
		{ PEER_INVALID, NULL }, { PEER_INVALID, NULL },
	/* Event-> */ /* PEER_DISABLE_CMD */  /* NO_PEER_DISABLE_CMD */
		{ PEER_INVALID, NULL }, { PEER_INVALID, NULL },
	/* Event-> */ /* PEER_HELPER_cmd */  /* NO_PEER_HELPER_CMD */
		{ PEER_INVALID, NULL }, { PEER_INVALID, NULL },
	},
	{
	/*	PEER_GLOBAL_INHERIT Mode	*/
	/* Event-> */ /* PEER_GR_CMD */		/* NO_PEER_GR_CMD */
		{ PEER_GR, bgp_peer_gr_action }, { PEER_GLOBAL_INHERIT, NULL },
	/* Event-> */ /* PEER_DISABLE_CMD */     /* NO_PEER_DISABLE_CMD */
		{ PEER_DISABLE, bgp_peer_gr_action }, { PEER_GLOBAL_INHERIT, NULL },
	/* Event-> */ /* PEER_HELPER_cmd */     /* NO_PEER_HELPER_CMD */
		{ PEER_HELPER, bgp_peer_gr_action }, { PEER_GLOBAL_INHERIT, NULL }
	}
	};
	memcpy(&peer->PEER_GR_FSM, local_Peer_GR_FSM, sizeof(local_Peer_GR_FSM));
	bgp_peer_move_to_gr_mode(peer, PEER_GLOBAL_INHERIT);

	return BGP_GR_SUCCESS;
}

/* 初始化实例的 SRv6（Segment Routing over IPv6）相关字段：
 * 默认封装行为（H.Encaps）、清空 locator 名、创建 locator_chunks 与 functions
 * 两个链表（各带元素释放回调）、srv6_only=true、清空按 afi 的 unicast 结构。
 * 这些在 bgp_create 时被调用，实际配置由 CLI 填充。 */
static void bgp_srv6_init(struct bgp *bgp)
{
	bgp->srv6_encap_behavior = SRV6_HEADEND_BEHAVIOR_H_ENCAPS;
	memset(bgp->srv6_locator_name, 0, sizeof(bgp->srv6_locator_name));
	bgp->srv6_locator_chunks = list_new();
	bgp->srv6_locator_chunks->del = srv6_locator_chunk_list_free;
	bgp->srv6_functions = list_new();
	bgp->srv6_functions->del = (void (*)(void *))srv6_function_free;
	bgp->srv6_only = true;
	memset(bgp->srv6_unicast, 0, sizeof(bgp->srv6_unicast));
}

/*
 * 释放实例的所有 SRv6 资源（在 bgp_free 时调用，作为 bgp_srv6_init 的逆操作）。
 * 逐个 afi 释放：VPN 策略中的 tovpn_sid_locator / tovpn_zebra_vrf_sid_last_sent /
 * tovpn_sid（需先 sid_unregister 注销）/ tovpn_sid_explicit，以及 per-afi
 * srv6_unicast 的 locator/sid 等；再释放全局 tovpn_* 与两个链表，
 * 最后释放 srv6_locator。注意 sid_unregister 必须在 XFREE 之前调用，否则会
 * 在 SID 注册表中留下悬垂引用。
 */
static void bgp_srv6_cleanup(struct bgp *bgp)
{
	for (afi_t afi = AFI_IP; afi < AFI_MAX; afi++) {
		if (bgp->vpn_policy[afi].tovpn_sid_locator != NULL) {
			srv6_locator_free(
				bgp->vpn_policy[afi].tovpn_sid_locator);
			bgp->vpn_policy[afi].tovpn_sid_locator = NULL;
		}
		if (bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent != NULL)
			XFREE(MTYPE_BGP_SRV6_SID,
			      bgp->vpn_policy[afi].tovpn_zebra_vrf_sid_last_sent);
		if (bgp->vpn_policy[afi].tovpn_sid != NULL) {
			sid_unregister(bgp, bgp->vpn_policy[afi].tovpn_sid);
			XFREE(MTYPE_BGP_SRV6_SID,
			      bgp->vpn_policy[afi].tovpn_sid);
		}
		if (bgp->vpn_policy[afi].tovpn_sid_explicit)
			XFREE(MTYPE_BGP_SRV6_SID, bgp->vpn_policy[afi].tovpn_sid_explicit);

		if (bgp->srv6_unicast[afi].sid_locator) {
			srv6_locator_free(bgp->srv6_unicast[afi].sid_locator);
			bgp->srv6_unicast[afi].sid_locator = NULL;
		}
		if (bgp->srv6_unicast[afi].zebra_sid_last_sent)
			XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].zebra_sid_last_sent);
		if (bgp->srv6_unicast[afi].sid)
			XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].sid);
		if (bgp->srv6_unicast[afi].sid_explicit)
			XFREE(MTYPE_BGP_SRV6_SID, bgp->srv6_unicast[afi].sid_explicit);
	}

	if (bgp->tovpn_sid_locator != NULL) {
		srv6_locator_free(bgp->tovpn_sid_locator);
		bgp->tovpn_sid_locator = NULL;
	}
	if (bgp->tovpn_zebra_vrf_sid_last_sent != NULL)
		XFREE(MTYPE_BGP_SRV6_SID, bgp->tovpn_zebra_vrf_sid_last_sent);
	if (bgp->tovpn_sid != NULL) {
		sid_unregister(bgp, bgp->tovpn_sid);
		XFREE(MTYPE_BGP_SRV6_SID, bgp->tovpn_sid);
	}
	if (bgp->tovpn_sid_explicit != NULL) {
		XFREE(MTYPE_BGP_SRV6_SID, bgp->tovpn_sid_explicit);
	}

	if (bgp->srv6_locator_chunks)
		list_delete(&bgp->srv6_locator_chunks);
	if (bgp->srv6_functions)
		list_delete(&bgp->srv6_functions);

	srv6_locator_free(bgp->srv6_locator);
	bgp->srv6_locator = NULL;
}

/* 判断实例是否配置了 SRv6 locator（以 locator 名非空为准）。 */
bool bgp_srv6_locator_is_configured(struct bgp *bgp)
{
	return (bgp->srv6_locator_name[0] != '\0');
}

/**
 * Return the SRv6 locator used for exported path from bgp_vrf
 *
 * @param bgp_vrf BGP VRF instance
 * @param bgp default BGP instance
 * @return srv6_locator
 * If bgp_vrf has any locator available, return it
 * otherwise fallback to the default VRF.
 */
/* 查找用于“导出路径”的 SRv6 locator：优先用 VRF 实例自己的，回退到 default 实例的。 */
struct srv6_locator *bgp_srv6_locator_lookup(struct bgp *bgp_vrf, struct bgp *bgp)
{
	if (bgp_vrf && bgp_vrf->srv6_locator)
		return bgp_vrf->srv6_locator;
	if (bgp && bgp->srv6_locator)
		return bgp->srv6_locator;
	return NULL;
}

/* Allocate new peer object, implicitly locked.  */
/*
 * 分配一个全新的 struct peer 并做默认初始化（“带锁返回”：初始 lock=1）。
 *
 * @bgp  : 归属实例（必须非空）。
 * @su   : 对端地址（可为 NULL）。
 * @dir  : 连接方向。
 * @return : 新 peer，已隐含持有一次引用（调用者最终要 peer_unlock）。
 *
 * 要点：
 *  - peer->bgp = bgp_lock(bgp)：peer 持有实例引用，防止实例在 peer 存活时被释放。
 *  - peer = peer_lock(peer)：置初始引用计数为 1。
 *  - 默认 per-afi 标志：发送 community/ext/large-community，且同时在
 *    af_flags_invert 里置位——这是 FRR “默认开启但用 invert 表达”的惯用法，
 *    使 show running 语义正确（见默认开启的字段都成对设置 flags 与 flags_invert）。
 *  - L2VPN/EVPN 默认 nexthop-unchanged。
 *  - 依据实例 bgp->flags 继承若干进程/实例级能力（enforce-first-as、soft-version
 *    新旧能力、link-local 能力、dynamic capability），enforce-first-as 亦用 invert 表达。
 *  - 初始化 peer 的 GR 状态机；从 services 库取 bgp 端口（默认 179）。
 *  - 最后 QOBJ_REG 注册对象。
 */
struct peer *peer_new(struct bgp *bgp, union sockunion *su, enum connection_direction dir)
{
	afi_t afi;
	safi_t safi;
	struct peer *peer;
	struct servent *sp;

	/* bgp argument is absolutely required */
	assert(bgp);

	/* Allocate new peer. */
	peer = XCALLOC(MTYPE_BGP_PEER, sizeof(struct peer));

	/* Create buffers. */
	peer->connection = bgp_peer_connection_new(peer, su, dir);

	/* Set default value. */
	peer->v_start = BGP_INIT_START_TIMER;
	peer->v_connect = bgp->default_connect_retry;
	peer->cur_event = peer->last_event = peer->last_major_event = 0;
	peer->bgp = bgp_lock(bgp);
	peer = peer_lock(peer); /* initial reference */
	peer->local_role = ROLE_UNDEFINED;
	peer->remote_role = ROLE_UNDEFINED;
	peer->password = NULL;
	peer->max_packet_size = BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE;
	peer->last_reset = PEER_DOWN_NONE;
	peer->down_last_reset = PEER_DOWN_NONE;

	/* Set default flags. */
	FOREACH_AFI_SAFI (afi, safi) {
		SET_FLAG(peer->af_flags[afi][safi], PEER_FLAG_SEND_COMMUNITY);
		SET_FLAG(peer->af_flags[afi][safi], PEER_FLAG_SEND_EXT_COMMUNITY);
		SET_FLAG(peer->af_flags[afi][safi],
			 PEER_FLAG_SEND_LARGE_COMMUNITY);

		SET_FLAG(peer->af_flags_invert[afi][safi],
			 PEER_FLAG_SEND_COMMUNITY);
		SET_FLAG(peer->af_flags_invert[afi][safi], PEER_FLAG_SEND_EXT_COMMUNITY);
		SET_FLAG(peer->af_flags_invert[afi][safi],
			 PEER_FLAG_SEND_LARGE_COMMUNITY);
		peer->addpath_type[afi][safi] = BGP_ADDPATH_NONE;
		peer->addpath_best_selected[afi][safi] = 0;
		peer->addpath_paths_limit[afi][safi].receive = 0;
		peer->addpath_paths_limit[afi][safi].send = 0;
		peer->soo[afi][safi] = NULL;
	}

	/* set nexthop-unchanged for l2vpn evpn by default */
	SET_FLAG(peer->af_flags[AFI_L2VPN][SAFI_EVPN],
		 PEER_FLAG_NEXTHOP_UNCHANGED);

	SET_FLAG(peer->sflags, PEER_STATUS_CAPABILITY_OPEN);

	/* By default this is enabled, thus we need to mark it as
	 * inverted in order to display correctly in the configuration.
	 */
	if (CHECK_FLAG(bgp->flags, BGP_FLAG_ENFORCE_FIRST_AS)) {
		SET_FLAG(peer->flags_invert, PEER_FLAG_ENFORCE_FIRST_AS);
		SET_FLAG(peer->flags, PEER_FLAG_ENFORCE_FIRST_AS);
	}

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_SOFT_VERSION_CAPABILITY_OLD))
		peer_flag_set(peer, PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD);

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_SOFT_VERSION_CAPABILITY_NEW))
		peer_flag_set(peer, PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW);

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_LINK_LOCAL_CAPABILITY))
		peer_flag_set(peer, PEER_FLAG_CAPABILITY_LINK_LOCAL);

	if (CHECK_FLAG(bgp->flags, BGP_FLAG_DYNAMIC_CAPABILITY))
		peer_flag_set(peer, PEER_FLAG_DYNAMIC_CAPABILITY);

	SET_FLAG(peer->flags_invert, PEER_FLAG_CAPABILITY_FQDN);
	SET_FLAG(peer->flags, PEER_FLAG_CAPABILITY_FQDN);

	/* Initialize per peer bgp GR FSM */
	bgp_peer_gr_init(peer);

	/* Get service port number.  */
	sp = getservbyname("bgp", "tcp");
	peer->port = (sp == NULL) ? BGP_PORT_DEFAULT : ntohs(sp->s_port);

	QOBJ_REG(peer, peer);
	return peer;
}

/*
 * This function is invoked when a duplicate peer structure associated with
 * a neighbor is being deleted. If this about-to-be-deleted structure is
 * the one with all the config, then we have to copy over the info.
 */
/*
 * 把一个 peer（peer_src）的配置“整体搬运”到另一个 peer（peer_dst）。
 *
 * 典型场景：doppelganger（临时分身）即将被删除时，把分身上的配置转移到正式
 * peer 上；以及 peer-group 配置向成员复制时也复用本函数。
 *
 * 注意：搬运时会把 peer_dst 的 PEER_FLAG_CONFIG_NODE 清掉（分身绝不能成为
 * “配置节点”），并搬运 flags/af_flags/定时器/密码/update-source/ttl/gtsm 等，
 * 对每个 peer_src 上存在的 peer_af，在 peer_dst 上按需创建对应 peer_af。
 * 字符串字段（password/update_source/update_if/ifname）在覆盖前先释放旧值，
 * 内存所有权随之转移（dup 而非别名）。
 */
void peer_xfer_config(struct peer *peer_dst, struct peer *peer_src)
{
	struct peer_af *paf;
	afi_t afi;
	safi_t safi;
	int afidx;

	assert(peer_src);
	assert(peer_dst);

	/* The following function is used by both peer group config copy to
	 * individual peer and when we transfer config
	 */
	if (peer_src->change_local_as)
		peer_dst->change_local_as = peer_src->change_local_as;

	/* peer flags apply */
	peer_dst->flags = peer_src->flags;
	/*
	 * The doppelganger *must* not have a config node stored
	 */
	UNSET_FLAG(peer_dst->flags, PEER_FLAG_CONFIG_NODE);
	peer_dst->peer_gr_present_state = peer_src->peer_gr_present_state;
	peer_dst->peer_gr_new_status_flag = peer_src->peer_gr_new_status_flag;

	peer_dst->local_as = peer_src->local_as;
	peer_dst->port = peer_src->port;
	/* copy tcp_mss value */
	peer_dst->tcp_mss = peer_src->tcp_mss;
	(void)peer_sort(peer_dst);
	peer_dst->sub_sort = peer_src->sub_sort;
	peer_dst->rmap_type = peer_src->rmap_type;
	peer_dst->local_role = peer_src->local_role;

	peer_dst->max_packet_size = peer_src->max_packet_size;

	/* Timers */
	peer_dst->holdtime = peer_src->holdtime;
	peer_dst->keepalive = peer_src->keepalive;
	peer_dst->connect = peer_src->connect;
	peer_dst->delayopen = peer_src->delayopen;
	peer_dst->v_holdtime = peer_src->v_holdtime;
	peer_dst->v_keepalive = peer_src->v_keepalive;
	peer_dst->routeadv = peer_src->routeadv;
	peer_dst->v_routeadv = peer_src->v_routeadv;
	peer_dst->v_delayopen = peer_src->v_delayopen;

	/* password apply */
	if (peer_src->password) {
		XFREE(MTYPE_PEER_PASSWORD, peer_dst->password);
		peer_dst->password =
			XSTRDUP(MTYPE_PEER_PASSWORD, peer_src->password);
	}

	FOREACH_AFI_SAFI (afi, safi) {
		peer_dst->afc[afi][safi] = peer_src->afc[afi][safi];
		peer_dst->af_flags[afi][safi] = peer_src->af_flags[afi][safi];
		peer_dst->allowas_in[afi][safi] =
			peer_src->allowas_in[afi][safi];
		peer_dst->weight[afi][safi] = peer_src->weight[afi][safi];
		peer_dst->addpath_type[afi][safi] =
			peer_src->addpath_type[afi][safi];
		peer_dst->addpath_paths_limit[afi][safi] =
			peer_src->addpath_paths_limit[afi][safi];
	}

	for (afidx = BGP_AF_START; afidx < BGP_AF_MAX; afidx++) {
		paf = peer_src->peer_af_array[afidx];
		if (paf != NULL) {
			if (!peer_af_find(peer_dst, paf->afi, paf->safi))
				peer_af_create(peer_dst, paf->afi, paf->safi);
		}
	}

	/* update-source apply */
	if (peer_src->update_source) {
		if (peer_dst->update_source)
			sockunion_free(peer_dst->update_source);
		XFREE(MTYPE_PEER_UPDATE_SOURCE, peer_dst->update_if);
		peer_dst->update_source =
			sockunion_dup(peer_src->update_source);
	} else if (peer_src->update_if) {
		XFREE(MTYPE_PEER_UPDATE_SOURCE, peer_dst->update_if);
		if (peer_dst->update_source) {
			sockunion_free(peer_dst->update_source);
			peer_dst->update_source = NULL;
		}
		peer_dst->update_if =
			XSTRDUP(MTYPE_PEER_UPDATE_SOURCE, peer_src->update_if);
	}

	if (peer_src->ifname) {
		XFREE(MTYPE_BGP_PEER_IFNAME, peer_dst->ifname);

		peer_dst->ifname =
			XSTRDUP(MTYPE_BGP_PEER_IFNAME, peer_src->ifname);
	}
	peer_dst->ttl = peer_src->ttl;
	peer_dst->gtsm_hops = peer_src->gtsm_hops;
}

/*
 * 对“接口邻居（BGP unnumbered）”推导对端 IPv4 地址。
 * 原理：若本端接口地址是 /30 或 /31 点对点网段，则对端地址可由本端地址唯一推出：
 *   /30：网内形如 x.1/x.2 两端，若本端 %4==1 则对端 +1，==2 则 -1；
 *   /31：RFC3021 两端，本端偶数则对端 +1，奇数则 -1。
 * 推导成功则写入 connection->su 并返回 1；否则返回 0（并可能打印调试）。
 * @ifp : 找到的本端接口。
 */
static int bgp_peer_conf_if_to_su_update_v4(struct peer_connection *connection,
					    struct interface *ifp)
{
	struct connected *ifc;
	struct prefix p;
	uint32_t addr;

	/* If our IPv4 address on the interface is /30 or /31, we can derive the
	 * IPv4 address of the other end.
	 */
	frr_each (if_connected, ifp->connected, ifc) {
		if (ifc->address && (ifc->address->family == AF_INET)) {
			prefix_copy(&p, CONNECTED_PREFIX(ifc));
			if (p.prefixlen == 30) {
				connection->su.sa.sa_family = AF_INET;
				addr = ntohl(p.u.prefix4.s_addr);
				if (addr % 4 == 1)
					connection->su.sin.sin_addr.s_addr =
						htonl(addr + 1);
				else if (addr % 4 == 2)
					connection->su.sin.sin_addr.s_addr =
						htonl(addr - 1);
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
				connection->su.sin.sin_len =
					sizeof(struct sockaddr_in);
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
				return 1;
			} else if (p.prefixlen == 31) {
				connection->su.sa.sa_family = AF_INET;
				addr = ntohl(p.u.prefix4.s_addr);
				if (addr % 2 == 0)
					connection->su.sin.sin_addr.s_addr =
						htonl(addr + 1);
				else
					connection->su.sin.sin_addr.s_addr =
						htonl(addr - 1);
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
				connection->su.sin.sin_len =
					sizeof(struct sockaddr_in);
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
				return 1;
			} else if (bgp_debug_neighbor_events(connection->peer))
				zlog_debug("%s: IPv4 interface address is not /30 or /31, v4 session not started",
					   connection->peer->conf_if);
		}
	}

	return 0;
}

/*
 * 对接口邻居推导对端 IPv6 link-local 地址：若接口上学到了邻居地址
 * （ifp->nbr_connected，来自 RA 的源地址），取其首个写入 connection->su，
 * 设置 scope_id 为接口 ifindex，返回 true；否则 false。
 */
static bool bgp_peer_conf_if_to_su_update_v6(struct peer_connection *connection,
					     struct interface *ifp)
{
	struct nbr_connected *ifc_nbr;

	/* Have we learnt the peer's IPv6 link-local address? */
	if (ifp->nbr_connected
	    && (ifc_nbr = listnode_head(ifp->nbr_connected))) {
		connection->su.sa.sa_family = AF_INET6;
		memcpy(&connection->su.sin6.sin6_addr,
		       &ifc_nbr->address->u.prefix, sizeof(struct in6_addr));
#ifdef SIN6_LEN
		connection->su.sin6.sin6_len = sizeof(struct sockaddr_in6);
#endif
		connection->su.sin6.sin6_scope_id = ifp->ifindex;
		return true;
	}

	return false;
}

/*
 * Set or reset the peer address socketunion structure based on the
 * learnt/derived peer address. If the address has changed, update the
 * password on the listen socket, if needed.
 */
/*
 * 根据接口邻居推导出的地址，更新（或重置）connection 的对端地址 sockunion，
 * 并在需要时同步监听套接字上的 MD5 密码。
 *
 * 流程：仅对 conf_if 型 peer 生效（否则返回）→ 记住旧 su → 查接口 →
 * 先试 v4（除非 v6only），失败再试 v6 → 若拿到地址且此前无地址，安装 MD5；
 * 若拿不到地址，则卸载 MD5 并把 su 置 AF_UNSPEC。
 * 最后：若 su 发生变化，需在 bgp->peerhash（连接哈希）与 bgp->peer 链表中
 * “先摘旧键、再插新键”——因为哈希键就是对端地址。注意代码用 listnode_lookup
 * 判断 peer 是否仍在链表中（删除路径下它可能已被摘除），避免误加回。
 */
void bgp_peer_conf_if_to_su_update(struct peer_connection *connection)
{
	struct interface *ifp;
	int prev_family;
	int peer_addr_updated = 0;
	struct listnode *node;
	union sockunion old_su;
	struct peer *peer = connection->peer;

	/*
	 * This function is only ever needed when FRR an interface
	 * based peering, so this simple test will tell us if
	 * we are in an interface based configuration or not
	 */
	if (!peer->conf_if)
		return;

	old_su = connection->su;

	prev_family = connection->su.sa.sa_family;
	if ((ifp = if_lookup_by_name(peer->conf_if, peer->bgp->vrf_id))) {
		peer->ifp = ifp;
		/* If BGP unnumbered is not "v6only", we first see if we can
		 * derive the
		 * peer's IPv4 address.
		 */
		if (!CHECK_FLAG(peer->flags, PEER_FLAG_IFPEER_V6ONLY))
			peer_addr_updated =
				bgp_peer_conf_if_to_su_update_v4(connection,
								 ifp);

		/* If "v6only" or we can't derive peer's IPv4 address, see if
		 * we've
		 * learnt the peer's IPv6 link-local address. This is from the
		 * source
		 * IPv6 address in router advertisement.
		 */
		if (!peer_addr_updated)
			peer_addr_updated =
				bgp_peer_conf_if_to_su_update_v6(connection,
								 ifp);
	}
	/* If we could derive the peer address, we may need to install the
	 * password
	 * configured for the peer, if any, on the listen socket. Otherwise,
	 * mark
	 * that peer's address is not available and uninstall the password, if
	 * needed.
	 */
	if (peer_addr_updated) {
		if (CHECK_FLAG(peer->flags, PEER_FLAG_PASSWORD)
		    && prev_family == AF_UNSPEC)
			bgp_md5_set(connection);
	} else {
		if (CHECK_FLAG(peer->flags, PEER_FLAG_PASSWORD)
		    && prev_family != AF_UNSPEC)
			bgp_md5_unset(connection);
		connection->su.sa.sa_family = AF_UNSPEC;
		memset(&connection->su.sin6.sin6_addr, 0,
		       sizeof(struct in6_addr));
	}

	/*
	 * If they are the same, nothing to do here, move along
	 */
	if (!sockunion_same(&old_su, &connection->su)) {
		union sockunion new_su = connection->su;
		struct bgp *bgp = peer->bgp;

		/*
		 * Our peer structure is stored in the bgp->peerhash
		 * release it before we modify anything in both the
		 * hash and the list.  But *only* if the peer
		 * is in the bgp->peerhash as that on deletion
		 * we call bgp_stop which calls this function :(
		 * so on deletion let's remove from the list first
		 * and then do the deletion preventing this from
		 * being added back on the list below when we
		 * fail to remove it up here.
		 */

		/*
		 * listnode_lookup just scans the list
		 * for the peer structure so it's safe
		 * to use without modifying the su
		 */
		node = listnode_lookup(bgp->peer, peer);
		if (node) {
			/*
			 * Let's reset the peer->su release and
			 * reset it and put it back.  We have to
			 * do this because hash_release will
			 * scan through looking for a matching
			 * su if needed.
			 */
			connection->su = old_su;
			hash_release(peer->bgp->connectionhash, connection);
			listnode_delete(peer->bgp->peer, peer);

			connection->su = new_su;
			(void)hash_get(peer->bgp->connectionhash, connection, hash_alloc_intern);
			listnode_add_sort(peer->bgp->peer, peer);
		}
	}
}

/*
 * 对某实例某地址族的整张 RIB 强制重算最佳路径。
 * 遍历 RIB 表：对 SAFI_MPLS_VPN / SAFI_ENCAP / SAFI_EVPN 这类两级表，先取
 * 一级 dest（按 RD/VRF 分组），再对二级表中每条 path 调 bgp_process()；
 * 其它 safi 直接对一级表的每条 path 调 bgp_process()。
 * 调用时机：影响全表选路的配置变更（如 router-id、addpath 等）之后。
 */
void bgp_recalculate_afi_safi_bestpaths(struct bgp *bgp, afi_t afi, safi_t safi)
{
	struct bgp_dest *dest, *ndest;
	struct bgp_path_info *pi, *next;
	struct bgp_table *table;

	for (dest = bgp_table_top(bgp->rib[afi][safi]); dest;
	     dest = bgp_route_next(dest)) {
		table = bgp_dest_get_bgp_table_info(dest);

		if (!table)
			continue;

		/* Special handling for 2-level routing
		 * tables. */
		if (safi == SAFI_MPLS_VPN || safi == SAFI_ENCAP
		    || safi == SAFI_EVPN) {
			for (ndest = bgp_table_top(table); ndest;
			     ndest = bgp_route_next(ndest)) {
				for (pi = bgp_dest_get_bgp_path_info(ndest);
				     (pi != NULL) && (next = pi->next, 1);
				     pi = next)
					bgp_process(bgp, ndest, pi, afi, safi);
			}
		} else {
			for (pi = bgp_dest_get_bgp_path_info(dest);
			     (pi != NULL) && (next = pi->next, 1); pi = next)
				bgp_process(bgp, dest, pi, afi, safi);
		}
	}
}

/* Force a bestpath recalculation for all prefixes.  This is used
 * when 'bgp bestpath' commands are entered.
 */
/* 对所有 afi/safi 强制重算最佳路径（进入 `bgp bestpath ...` 系列命令时使用）。 */
void bgp_recalculate_all_bestpaths(struct bgp *bgp)
{
	afi_t afi;
	safi_t safi;

	FOREACH_AFI_SAFI (afi, safi) {
		bgp_recalculate_afi_safi_bestpaths(bgp, afi, safi);
	}
}

/*
 * Create new BGP peer.
 *
 * conf_if and su are mutually exclusive if configuring from the cli.
 * If we are handing a doppelganger, then we *must* pass in both
 * the original peer's su and conf_if, so that we can appropriately
 * track the bgp->peerhash( ie we don't want to remove the current
 * one from the config ).
 */
/*
 * 创建一个“完整”的 BGP peer：在 peer_new 基础上补齐地址/AS/定时器/激活等，并
 * 挂入实例的 peer 链表与 connection 哈希。
 *
 * 参数要点：
 *  - su 与 conf_if 互斥（CLI 配置时二选一）；处理 doppelganger 时两者都要传，以便
 *    正确维护 bgp->peerhash。
 *  - config_node=true 表示为“显式配置的邻居”（打 PEER_FLAG_CONFIG_NODE），
 *    doppelganger/动态邻居则为 false。
 *  - group：若该 peer 属于某 peer-group，记录之。
 *  - as_str：AS 的“漂亮”形式（as-notation）；仅对“指定 AS”保留。
 *
 * 关键副作用：
 *  - listnode_add_sort(bgp->peer, peer) 与 hash_get(connectionhash) 完成登记；
 *  - 依据 bgp->default_af 自动激活对应地址族（仅 IPv4 unicast 默认开启，其余靠
 *    `bgp default ...` 或显式 activate）；
 *  - 依据 bgp->autoshutdown 决定是否直接置 SHUTDOWN；
 *  - 若创建前后 active 状态由“非活跃”变为“活跃”，启动 peer 的定时器
 *    （bgp_timer_set），把 FSM 推进。
 *  - 依据 peer 数量动态调整 update-group 合并定时器（heuristic_coalesce）。
 * 返回值：新 peer（持有引用，调用者拥有）。
 */
struct peer *peer_create(union sockunion *su, const char *conf_if, struct bgp *bgp, as_t local_as,
			 as_t remote_as, enum peer_asn_type as_type, struct peer_group *group,
			 bool config_node, const char *as_str, enum connection_direction dir)
{
	enum bgp_peer_active active;
	struct peer *peer;
	char buf[SU_ADDRSTRLEN];
	afi_t afi;
	safi_t safi;

	peer = peer_new(bgp, su, dir);
	if (conf_if) {
		peer->conf_if = XSTRDUP(MTYPE_PEER_CONF_IF, conf_if);
		if (!su)
			bgp_peer_conf_if_to_su_update(peer->connection);
		XFREE(MTYPE_BGP_PEER_HOST, peer->host);
		peer->host = XSTRDUP(MTYPE_BGP_PEER_HOST, conf_if);
	} else if (su) {
		sockunion2str(su, buf, SU_ADDRSTRLEN);
		XFREE(MTYPE_BGP_PEER_HOST, peer->host);
		peer->host = XSTRDUP(MTYPE_BGP_PEER_HOST, buf);
	}
	peer->local_as = local_as;
	peer->as = remote_as;
	/* internal and external values do not use as_pretty */
	if (as_str && asn_str2asn(as_str, NULL))
		peer->as_pretty = XSTRDUP(MTYPE_BGP_NAME, as_str);
	peer->as_type = as_type;
	peer->local_id = bgp->router_id;
	peer->v_holdtime = bgp->default_holdtime;
	peer->v_keepalive = bgp->default_keepalive;
	peer->v_routeadv = (peer_sort(peer) == BGP_PEER_IBGP)
				   ? BGP_DEFAULT_IBGP_ROUTEADV
				   : BGP_DEFAULT_EBGP_ROUTEADV;
	if (bgp_config_inprocess())
		peer->shut_during_cfg = true;

	peer = peer_lock(peer); /* bgp peer list reference */
	peer->group = group;
	listnode_add_sort(bgp->peer, peer);

	if (config_node)
		SET_FLAG(peer->flags, PEER_FLAG_CONFIG_NODE);

	(void)hash_get(bgp->connectionhash, peer->connection, hash_alloc_intern);

	/* Adjust update-group coalesce timer heuristics for # peers. */
	if (bgp->heuristic_coalesce) {
		long ct = BGP_DEFAULT_SUBGROUP_COALESCE_TIME
			  + (bgp->peer->count
			     * BGP_PEER_ADJUST_SUBGROUP_COALESCE_TIME);
		bgp->coalesce_time = MIN(BGP_MAX_SUBGROUP_COALESCE_TIME, ct);
	}

	active = peer_active(peer->connection);
	if (active != BGP_PEER_ACTIVE) {
		if (peer->connection->su.sa.sa_family == AF_UNSPEC)
			peer->last_reset = PEER_DOWN_NBR_ADDR;
		else
			peer->last_reset = PEER_DOWN_NOAFI_ACTIVATED;
	}

	/* Last read and reset time set */
	peer->readtime = peer->resettime = monotime(NULL);

	/* Default TTL set. */
	peer->ttl = (peer->sort == BGP_PEER_IBGP) ? MAXTTL : BGP_DEFAULT_TTL;

	/* Default configured keepalives count for shutdown rtt command */
	peer->rtt_keepalive_conf = 1;

	/* If 'bgp default <afi>-<safi>' is configured, then activate the
	 * neighbor for the corresponding address family. IPv4 Unicast is
	 * the only address family enabled by default without expliict
	 * configuration.
	 */
	FOREACH_AFI_SAFI (afi, safi) {
		if (bgp->default_af[afi][safi]) {
			peer->afc[afi][safi] = 1;
			peer_af_create(peer, afi, safi);
		}
	}

	/* auto shutdown if configured */
	if (bgp->autoshutdown)
		peer_flag_set(peer, PEER_FLAG_SHUTDOWN);
	/* Set up peer's events and timers. */
	else if (active != BGP_PEER_ACTIVE && peer_active(peer->connection) == BGP_PEER_ACTIVE) {
		if (peer->last_reset == PEER_DOWN_NOAFI_ACTIVATED)
			peer->last_reset = PEER_DOWN_NONE;
		bgp_timer_set(peer->connection);
	}

	bgp_peer_gr_flags_update(peer);
	BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA(bgp, bgp->peer);

	return peer;
}

/* Make accept BGP peer. This function is only called from the test code */
/* 创建一个“接受型”peer（仅测试代码使用）：不做 AS 配置等，只登记进链表与哈希。 */
struct peer *peer_create_accept(struct bgp *bgp, union sockunion *su)
{
	struct peer *peer;

	peer = peer_new(bgp, su, UNKNOWN);

	peer = peer_lock(peer); /* bgp peer list reference */
	listnode_add_sort(bgp->peer, peer);
	(void)hash_get(bgp->connectionhash, peer->connection, hash_alloc_intern);

	return peer;
}

/*
 * Return true if we have a peer configured to use this afi/safi
 */
/* 判断本实例是否存在“已配置的、且激活了该 afi/safi”的 peer。
 * 用于决定某地址族是否还有存在意义（例如是否可清理该族的表/监听）。 */
bool bgp_afi_safi_peer_exists(struct bgp *bgp, afi_t afi, safi_t safi)
{
	struct listnode *node;
	struct peer *peer;

	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
		if (!peer_is_config_node(peer))
			continue;

		if (peer->afc[afi][safi])
			return true;
	}

	return false;
}

/* Change peer's AS number.  */
/*
 * 修改 peer 的远程 AS（及 as_type / as_pretty），并处理随之而来的连锁影响。
 *
 * 步骤：
 *  1) 若 peer 不是 peer-group 自身：标记 last_reset 为 REMOTE_AS_CHANGE 并复位会话
 *     （AS 改变必须重建会话以重新协商）。
 *  2) 记下旧类型 origtype，写入新 as/as_type，维护 as_pretty 字符串所有权。
 *  3) 重算 local_as：若实例处于联邦且该 AS 非联邦成员亦非本 AS，则 local_as 用
 *     confed_id（“对外仍是大 AS”），否则用 bgp->as。
 *  4) 重算类型 newtype 并同步相关默认值：
 *     - advertise-interval（v_routeadv）按 IBGP/EBGP 默认（未显式配置时）；
 *     - TTL：变 IBGP → MAXTTL，由 IBGP 变其它 → 默认 TTL；
 *     - 不再是 IBGP 时清掉所有 AFI/SAFI 上的 REFLECTOR_CLIENT（反射器客户）。
 */
void peer_as_change(struct peer *peer, as_t as, enum peer_asn_type as_type,
		    const char *as_str)
{
	enum bgp_peer_sort origtype, newtype;

	/* Stop peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_set_last_reset(peer, PEER_DOWN_REMOTE_AS_CHANGE);
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);
	}
	origtype = peer_sort_lookup(peer);
	peer->as = as;
	if (as_type == AS_SPECIFIED && as_str) {
		if (peer->as_pretty)
			XFREE(MTYPE_BGP_NAME, peer->as_pretty);
		peer->as_pretty = XSTRDUP(MTYPE_BGP_NAME, as_str);
	} else if (peer->as_type == AS_UNSPECIFIED && peer->as_pretty)
		XFREE(MTYPE_BGP_NAME, peer->as_pretty);
	peer->as_type = as_type;

	if (bgp_config_check(peer->bgp, BGP_CONFIG_CONFEDERATION)
	    && !bgp_confederation_peers_check(peer->bgp, as)
	    && peer->bgp->as != as)
		peer->local_as = peer->bgp->confed_id;
	else
		peer->local_as = peer->bgp->as;

	newtype = peer_sort(peer);
	/* Advertisement-interval reset */
	if (!CHECK_FLAG(peer->flags, PEER_FLAG_ROUTEADV)) {
		peer->v_routeadv = (newtype == BGP_PEER_IBGP)
					   ? BGP_DEFAULT_IBGP_ROUTEADV
					   : BGP_DEFAULT_EBGP_ROUTEADV;
	}

	/* TTL reset */
	if (newtype == BGP_PEER_IBGP)
		peer->ttl = MAXTTL;
	else if (origtype == BGP_PEER_IBGP)
		peer->ttl = BGP_DEFAULT_TTL;

	/* reflector-client reset */
	if (newtype != BGP_PEER_IBGP) {
		UNSET_FLAG(peer->af_flags[AFI_IP][SAFI_UNICAST],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP][SAFI_MULTICAST],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP][SAFI_LABELED_UNICAST],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP][SAFI_MPLS_VPN],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP][SAFI_ENCAP],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP][SAFI_FLOWSPEC],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP6][SAFI_UNICAST],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP6][SAFI_MULTICAST],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP6][SAFI_LABELED_UNICAST],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP6][SAFI_MPLS_VPN],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP6][SAFI_ENCAP],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_IP6][SAFI_FLOWSPEC],
			   PEER_FLAG_REFLECTOR_CLIENT);
		UNSET_FLAG(peer->af_flags[AFI_L2VPN][SAFI_EVPN],
			   PEER_FLAG_REFLECTOR_CLIENT);
	}
}

/* If peer does not exist, create new one.  If peer already exists,
   set AS number to the peer.  */
/*
 * “设置邻居远程 AS”的统一入口：peer 已存在则改 AS，不存在则创建。
 * 这是 `neighbor X remote-as Y` 命令的落地函数。
 *
 * @as : 既是输入（期望的 AS），也可能被回填为“实际生效的 AS”（见下）。
 * @return : 0 成功；其它为 BGP_ERR_* 错误码。
 *
 * 分支要点：
 *  - 动态邻居（listen range 自动创建）不允许改 AS，返回 BGP_ERR_INVALID_FOR_DYNAMIC_PEER。
 *  - peer 属于 peer-group 时：若组已有 AS/类型，则成员不能自行指定，返回
 *    BGP_ERR_PEER_GROUP_MEMBER 并把 *as 回填为组 AS；否则校验成员 AS 与组类型
 *    是否冲突（IBGP/EBGP 与指定 AS 的矛盾），冲突则 BGP_ERR_PEER_GROUP_PEER_TYPE_DIFFERENT。
 *  - 已存在且确需改动时才调用 peer_as_change。
 *  - 不存在：conf_if 型（接口邻居）必须先有接口配置，否则 BGP_ERR_NO_INTERFACE_CONFIG；
 *    首建时按联邦规则决定 local_as，最后 peer_create 创建（config_node=true）。
 */
int peer_remote_as(struct bgp *bgp, union sockunion *su, const char *conf_if,
		   as_t *as, enum peer_asn_type as_type, const char *as_str)
{
	struct peer *peer;
	as_t local_as;

	if (conf_if)
		peer = peer_lookup_by_conf_if(bgp, conf_if);
	else
		peer = peer_lookup(bgp, su);

	if (peer) {
		/* Not allowed for a dynamic peer. */
		if (peer_dynamic_neighbor(peer)) {
			*as = peer->as;
			return BGP_ERR_INVALID_FOR_DYNAMIC_PEER;
		}

		/* When this peer is a member of peer-group.  */
		if (peer->group) {
			/* peer-group already has AS number/internal/external */
			if (peer->group->conf->as || peer->group->conf->as_type != AS_UNSPECIFIED) {
				/* Return peer group's AS number.  */
				*as = peer->group->conf->as;
				return BGP_ERR_PEER_GROUP_MEMBER;
			}

			enum bgp_peer_sort peer_sort_type =
				peer_sort(peer->group->conf);

			/* Explicit AS numbers used, compare AS numbers */
			if (as_type == AS_SPECIFIED) {
				if (((peer_sort_type == BGP_PEER_IBGP)
				    && (bgp->as != *as))
				    || ((peer_sort_type == BGP_PEER_EBGP)
				    && (bgp->as == *as))) {
					*as = peer->as;
					return BGP_ERR_PEER_GROUP_PEER_TYPE_DIFFERENT;
				}
			} else {
				/* internal/external used, compare as-types */
				if (((peer_sort_type == BGP_PEER_IBGP) &&
				     !CHECK_FLAG(as_type, AS_INTERNAL)) ||
				    ((peer_sort_type == BGP_PEER_EBGP) &&
				     !CHECK_FLAG(as_type, AS_EXTERNAL))) {
					*as = peer->as;
					return BGP_ERR_PEER_GROUP_PEER_TYPE_DIFFERENT;
				}
			}
		}

		/* Existing peer's AS number change. */
		if (((peer->as_type == AS_SPECIFIED) && peer->as != *as)
		    || (peer->as_type != as_type))
			peer_as_change(peer, *as, as_type, as_str);
	} else {
		if (conf_if)
			return BGP_ERR_NO_INTERFACE_CONFIG;

		/* If the peer is not part of our confederation, and its not an
		   iBGP peer then spoof the source AS */
		if (bgp_config_check(bgp, BGP_CONFIG_CONFEDERATION) &&
		    !bgp_confederation_peers_check(bgp, *as) && *as &&
		    bgp->as != *as)
			local_as = bgp->confed_id;
		else
			local_as = bgp->as;

		peer_create(su, conf_if, bgp, local_as, *as, as_type, NULL, true, as_str,
			    CONNECTION_OUTGOING);
	}

	return 0;
}

/* 把 BGP Role（RFC9234）枚举转成 CLI 用的字符串（provider/rs-server/...）。 */
const char *bgp_get_name_by_role(uint8_t role)
{
	switch (role) {
	case ROLE_PROVIDER:
		return "provider";
	case ROLE_RS_SERVER:
		return "rs-server";
	case ROLE_RS_CLIENT:
		return "rs-client";
	case ROLE_CUSTOMER:
		return "customer";
	case ROLE_PEER:
		return "peer";
	case ROLE_UNDEFINED:
		return "undefined";
	}
	return "unknown";
}

/* 返回实例的 AS 记法模式（asplain/asdot/asdot+）；bgp 为空时回退 asplain。 */
enum asnotation_mode bgp_get_asnotation(struct bgp *bgp)
{
	if (!bgp)
		return ASNOTATION_PLAIN;
	return bgp->asnotation;
}

/*
 * 【peer-group 展开·地址族版】把组在某 (afi,safi) 上的全部配置按“继承三件套”
 * 合并到成员 peer。
 *
 * 合并标志位的公式（本文件最关键的一段，务必理解）：
 *   flags_tmp = conf->af_flags[afi][safi] & ~pflags_ovrd;   // 组的基准值，挖掉本地 override
 *   flags_tmp ^= conf->af_flags_invert ^ peer->af_flags_invert; // 叠加组/本地两层取反
 *   UNSET_FLAG(flags_tmp, pflags_ovrd);                     // 再确保 override 位不被组值影响
 *   peer->af_flags = (peer->af_flags & pflags_ovrd) | flags_tmp; // override 位保留自身，其余取合并值
 * 等价于：对本 peer 显式设置过的位保持其自身值，其它位跟随“组值经 invert 修正后的结果”。
 *
 * 随后对每种“非标志型”属性（max-prefix、allowas-in、soo、weight、default-rmap、
 * 各类 in/out 过滤器、unsuppress-map、条件通告等）逐个判断：只要本 peer 在该属性上
 * 没有 override（对应 PEER_FT_* 位未置），就用组的设置覆盖之。
 * PEER_ATTR_INHERIT 是“标量/指针属性继承”的宏，PEER_STR_ATTR_INHERIT 是字符串字段
 * 继承（会重新 strdup，管理内存）——注意这里覆盖不会破坏 override 语义。
 *
 * 调用时机：peer-group 配置变更、peer 加入组、组内配置重算时（见 bgp_vty.c）。
 */
static void peer_group2peer_config_copy_af(struct peer_group *group,
					   struct peer *peer, afi_t afi,
					   safi_t safi)
{
	int in = FILTER_IN;
	int out = FILTER_OUT;
	uint64_t flags_tmp;
	uint64_t pflags_ovrd;
	uint8_t *pfilter_ovrd;
	struct peer *conf;

	conf = group->conf;
	pflags_ovrd = peer->af_flags_override[afi][safi];
	pfilter_ovrd = &peer->filter_override[afi][safi][in];

	/* peer af_flags apply */
	flags_tmp = conf->af_flags[afi][safi] & ~pflags_ovrd;
	flags_tmp ^= conf->af_flags_invert[afi][safi]
		     ^ peer->af_flags_invert[afi][safi];
	UNSET_FLAG(flags_tmp, pflags_ovrd);

	UNSET_FLAG(peer->af_flags[afi][safi], ~pflags_ovrd);
	SET_FLAG(peer->af_flags[afi][safi], flags_tmp);
	SET_FLAG(peer->af_flags_invert[afi][safi],
		 conf->af_flags_invert[afi][safi]);

	/* maximum-prefix */
	if (!CHECK_FLAG(pflags_ovrd, PEER_FLAG_MAX_PREFIX)) {
		PEER_ATTR_INHERIT(peer, group, pmax[afi][safi]);
		PEER_ATTR_INHERIT(peer, group, pmax_threshold[afi][safi]);
		PEER_ATTR_INHERIT(peer, group, pmax_restart[afi][safi]);
	}

	/* maximum-prefix-out */
	if (!CHECK_FLAG(pflags_ovrd, PEER_FLAG_MAX_PREFIX_OUT))
		PEER_ATTR_INHERIT(peer, group, pmax_out[afi][safi]);

	/* allowas-in */
	if (!CHECK_FLAG(pflags_ovrd, PEER_FLAG_ALLOWAS_IN))
		PEER_ATTR_INHERIT(peer, group, allowas_in[afi][safi]);

	/* soo */
	if (!CHECK_FLAG(pflags_ovrd, PEER_FLAG_SOO))
		PEER_ATTR_INHERIT(peer, group, soo[afi][safi]);

	/* weight */
	if (!CHECK_FLAG(pflags_ovrd, PEER_FLAG_WEIGHT))
		PEER_ATTR_INHERIT(peer, group, weight[afi][safi]);

	/* default-originate route-map */
	if (!CHECK_FLAG(pflags_ovrd, PEER_FLAG_DEFAULT_ORIGINATE)) {
		PEER_STR_ATTR_INHERIT(peer, group, default_rmap[afi][safi].name,
				      MTYPE_ROUTE_MAP_NAME);
		PEER_ATTR_INHERIT(peer, group, default_rmap[afi][safi].map);
	}

	/* inbound filter apply */
	if (!CHECK_FLAG(pfilter_ovrd[in], PEER_FT_DISTRIBUTE_LIST)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].dlist[in].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].dlist[in].alist);
	}

	if (!CHECK_FLAG(pfilter_ovrd[in], PEER_FT_PREFIX_LIST)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].plist[in].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].plist[in].plist);
	}

	if (!CHECK_FLAG(pfilter_ovrd[in], PEER_FT_FILTER_LIST)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].aslist[in].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].aslist[in].aslist);
	}

	if (!CHECK_FLAG(pfilter_ovrd[RMAP_IN], PEER_FT_ROUTE_MAP)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].map[in].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].map[RMAP_IN].map);
	}

	/* outbound filter apply */
	if (!CHECK_FLAG(pfilter_ovrd[out], PEER_FT_DISTRIBUTE_LIST)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].dlist[out].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].dlist[out].alist);
	}

	if (!CHECK_FLAG(pfilter_ovrd[out], PEER_FT_PREFIX_LIST)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].plist[out].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].plist[out].plist);
	}

	if (!CHECK_FLAG(pfilter_ovrd[out], PEER_FT_FILTER_LIST)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].aslist[out].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].aslist[out].aslist);
	}

	if (!CHECK_FLAG(pfilter_ovrd[RMAP_OUT], PEER_FT_ROUTE_MAP)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].map[RMAP_OUT].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].map[RMAP_OUT].map);
	}

	/* nondirectional filter apply */
	if (!CHECK_FLAG(pfilter_ovrd[0], PEER_FT_UNSUPPRESS_MAP)) {
		PEER_STR_ATTR_INHERIT(peer, group, filter[afi][safi].usmap.name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group, filter[afi][safi].usmap.map);
	}

	/* Conditional Advertisements */
	if (!CHECK_FLAG(pfilter_ovrd[RMAP_OUT], PEER_FT_ADVERTISE_MAP)) {
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].advmap.aname,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group, filter[afi][safi].advmap.amap);
		PEER_STR_ATTR_INHERIT(peer, group,
				      filter[afi][safi].advmap.cname,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, group, filter[afi][safi].advmap.cmap);
		PEER_ATTR_INHERIT(peer, group,
				  filter[afi][safi].advmap.condition);
	}

	if (peer->addpath_type[afi][safi] == BGP_ADDPATH_NONE) {
		peer->addpath_type[afi][safi] = conf->addpath_type[afi][safi];
		bgp_addpath_type_changed(conf->bgp);
	}
}

/*
 * 在单个（非 peer-group）peer 上激活一个地址族。
 *
 * 约束：同一 afi 下 SAFI_UNICAST 与 SAFI_LABELED_UNICAST 不能同时激活
 * （返回 BGP_ERR_PEER_SAFI_CONFLICT）；peer-group 不能走本函数。
 * 幂等：已激活则直接 0。
 *
 * 激活后（afc 置 1、创建 peer_af、若属于组则先继承组配置）：
 *  - 若激活使 peer 由“非活跃”变为“活跃”，启动定时器把 FSM 推进；
 *  - 否则记录 last_reset=AF_ACTIVATE，并分两种情形：
 *      · 会话已建立且有动态能力：发 MP capability（动态激活该族），若对端也已通告
 *        该族则标记已协商并立即通告路由；
 *      · 无动态能力：通知对端配置变更（通常触发会话重建）。
 *  - 最后若存在 doppelganger，也通知它配置变更（避免其建立时覆盖本族激活）。
 */
static int peer_activate_af(struct peer *peer, afi_t afi, safi_t safi)
{
	enum bgp_peer_active active;
	struct peer *other;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		flog_err(EC_BGP_PEER_GROUP, "%s was called for peer-group %s",
			 __func__, peer->host);
		return 1;
	}

	/* Do not activate a peer for both SAFI_UNICAST and SAFI_LABELED_UNICAST
	 */
	if ((safi == SAFI_UNICAST && peer->afc[afi][SAFI_LABELED_UNICAST])
	    || (safi == SAFI_LABELED_UNICAST && peer->afc[afi][SAFI_UNICAST]))
		return BGP_ERR_PEER_SAFI_CONFLICT;

	/* Nothing to do if we've already activated this peer */
	if (peer->afc[afi][safi])
		return 0;

	if (peer_af_create(peer, afi, safi) == NULL)
		return 1;

	active = peer_active(peer->connection);
	peer->afc[afi][safi] = 1;

	if (peer->group)
		peer_group2peer_config_copy_af(peer->group, peer, afi, safi);

	if (active != BGP_PEER_ACTIVE && peer_active(peer->connection) == BGP_PEER_ACTIVE) {
		bgp_timer_set(peer->connection);
	} else {
		peer_set_last_reset(peer, PEER_DOWN_AF_ACTIVATE);

		if (peer_established(peer->connection)) {
			if (CHECK_FLAG(peer->cap, PEER_CAP_DYNAMIC_RCV)) {
				peer->afc_adv[afi][safi] = 1;
				bgp_capability_send(peer, afi, safi,
						    CAPABILITY_CODE_MP,
						    CAPABILITY_ACTION_SET);
				if (peer->afc_recv[afi][safi]) {
					peer->afc_nego[afi][safi] = 1;
					bgp_announce_route(peer, afi, safi,
							   false);
				}
			} else {
				peer_notify_config_change(peer->connection);
			}
		}
		peer_notify_config_change(peer->connection);

		/*
		 * If we are turning on a AFI/SAFI locally and we've
		 * started bringing a peer up, we need to tell
		 * the other peer to restart because we might loose
		 * configuration here because when the doppelganger
		 * gets to a established state due to how
		 * we resolve we could just overwrite the afi/safi
		 * activation.
		 */
		other = peer->doppelganger;
		if (other)
			peer_notify_config_change(other->connection);
	}

	return 0;
}

/* Activate the peer or peer group for specified AFI and SAFI.  */
/*
 * 激活 peer 或 peer-group 的某地址族。这是 `neighbor X activate` /
 * `address-family ... neighbor X activate` 的落地函数。
 *
 * peer-group 情形：先给组自身 afc 置位，再对每个成员调用 peer_activate_af。
 * 非组情形：直接对自身调用。
 *
 * 额外的“首次激活”副作用（仅当不是 SAFI 冲突时）：
 *  - 对 SAFI_LABELED_UNICAST 或 SAFI_MPLS_VPN，若是本 afi 下第一次有 peer 激活，
 *    打开 bgp->allocate_mpls_labels 并重算最佳路径以触发标签分配；
 *  - 对 SAFI_FLOWSPEC，连接 Zebra 的 table manager。
 * @return : 0 或各 peer_activate_af 的返回位或（含 BGP_ERR_PEER_SAFI_CONFLICT）。
 */
int peer_activate(struct peer *peer, afi_t afi, safi_t safi)
{
	int ret = 0;
	struct peer_group *group;
	struct listnode *node, *nnode;
	struct peer *tmp_peer;
	struct bgp *bgp;
	safi_t safi_check;

	/* Nothing to do if we've already activated this peer */
	if (peer->afc[afi][safi])
		return ret;

	bgp = peer->bgp;

	/* This is a peer-group so activate all of the members of the
	 * peer-group as well */
	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {

		/* Do not activate a peer for both SAFI_UNICAST and
		 * SAFI_LABELED_UNICAST */
		if ((safi == SAFI_UNICAST
		     && peer->afc[afi][SAFI_LABELED_UNICAST])
		    || (safi == SAFI_LABELED_UNICAST
			&& peer->afc[afi][SAFI_UNICAST]))
			return BGP_ERR_PEER_SAFI_CONFLICT;

		peer->afc[afi][safi] = 1;
		group = peer->group;

		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, tmp_peer)) {
			SET_FLAG(ret, peer_activate_af(tmp_peer, afi, safi));
		}
	} else {
		SET_FLAG(ret, peer_activate_af(peer, afi, safi));
	}

	/* If this is the first peer to be activated for this
	 * afi/labeled-unicast or afi/mpls-vpn, recalc bestpaths to trigger
	 * label allocation */
	if (safi == SAFI_LABELED_UNICAST)
		safi_check = SAFI_UNICAST;
	else
		safi_check = safi;
	if (ret != BGP_ERR_PEER_SAFI_CONFLICT &&
	    (safi == SAFI_LABELED_UNICAST || safi == SAFI_MPLS_VPN) &&
	    !bgp->allocate_mpls_labels[afi][safi_check]) {

		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"peer(s) are now active for %s, allocate MPLS labels",
				safi2str(safi));
		bgp->allocate_mpls_labels[afi][safi_check] = 1;
		bgp_recalculate_afi_safi_bestpaths(bgp, afi, safi_check);
	}

	if (safi == SAFI_FLOWSPEC) {
		/* connect to table manager */
		bgp_zebra_init_tm_connect(bgp);
	}
	return ret;
}

/*
 * 在单个（非 peer-group）peer 上停用某地址族。@return : true 表示“出错或应中止上级循环”。
 * 步骤：清 afc → peer_af_delete 释放 peer_af（失败报错返回 true）→
 * 若会话已建立：记 last_reset=NEIGHBOR_DELETE；有动态能力则发 MP capability
 * （UNSET）撤该族、清该族路由与计数，否则通知配置变更（重建会话）。
 */
static bool non_peergroup_deactivate_af(struct peer *peer, afi_t afi,
					safi_t safi)
{
	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		flog_err(EC_BGP_PEER_GROUP, "%s was called for peer-group %s",
			 __func__, peer->host);
		return true;
	}

	/* Nothing to do if we've already deactivated this peer */
	if (!peer->afc[afi][safi])
		return false;

	/* De-activate the address family configuration. */
	peer->afc[afi][safi] = 0;

	if (peer_af_delete(peer, afi, safi) != 0) {
		flog_err(EC_BGP_PEER_DELETE,
			 "couldn't delete af structure for peer %s(%s, %s)",
			 peer->host, afi2str(afi), safi2str(safi));
		return true;
	}

	if (peer_established(peer->connection)) {
		peer_set_last_reset(peer, PEER_DOWN_NEIGHBOR_DELETE);

		if (CHECK_FLAG(peer->cap, PEER_CAP_DYNAMIC_RCV)) {
			peer->afc_adv[afi][safi] = 0;
			peer->afc_nego[afi][safi] = 0;

			if (peer_active_nego(peer)) {
				bgp_capability_send(peer, afi, safi,
						    CAPABILITY_CODE_MP,
						    CAPABILITY_ACTION_UNSET);
				bgp_clear_route(peer, afi, safi);
				peer->pcount[afi][safi] = 0;
			} else {
				peer_notify_config_change(peer->connection);
			}
		} else
			peer_notify_config_change(peer->connection);
	}

	return false;
}

/*
 * 停用 peer 或 peer-group 的某地址族（peer_activate 的逆操作）。
 * peer-group 情形：先清组自身 afc，再对每个成员调 non_peergroup_deactivate_af。
 * 关键副作用：对 SAFI_LABELED_UNICAST / SAFI_MPLS_VPN，若这是最后一个激活该族的
 * peer（!bgp_afi_safi_peer_exists），则关闭 allocate_mpls_labels 并重算最佳路径
 * 以触发标签回收。@return : 各子调用返回值按位或。
 */
int peer_deactivate(struct peer *peer, afi_t afi, safi_t safi)
{
	int ret = 0;
	struct peer_group *group;
	struct peer *tmp_peer;
	struct listnode *node, *nnode;
	struct bgp *bgp;
	safi_t safi_check;

	/* Nothing to do if we've already de-activated this peer */
	if (!peer->afc[afi][safi])
		return ret;

	/* This is a peer-group so de-activate all of the members of the
	 * peer-group as well */
	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer->afc[afi][safi] = 0;
		group = peer->group;

		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, tmp_peer)) {
			SET_FLAG(ret, non_peergroup_deactivate_af(tmp_peer, afi,
								  safi));
		}
	} else {
		SET_FLAG(ret, non_peergroup_deactivate_af(peer, afi, safi));
	}

	bgp = peer->bgp;

	/* If this is the last peer to be deactivated for this
	 * afi/labeled-unicast or afi/mpls-vpn, recalc bestpaths to trigger
	 * label deallocation */
	if (safi == SAFI_LABELED_UNICAST)
		safi_check = SAFI_UNICAST;
	else
		safi_check = safi;
	if ((safi == SAFI_LABELED_UNICAST || safi == SAFI_MPLS_VPN) &&
	    bgp->allocate_mpls_labels[afi][safi_check] &&
	    !bgp_afi_safi_peer_exists(bgp, afi, safi)) {

		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"peer(s) are no longer active for %s, deallocate MPLS labels",
				safi2str(safi));
		bgp->allocate_mpls_labels[afi][safi_check] = 0;
		bgp_recalculate_afi_safi_bestpaths(bgp, afi, safi_check);
	}
	return ret;
}

/*
 * 停止 peer 的 NSF（Non-Stop Forwarding，即 GR helper 侧）处理：
 * 清除 NSF_WAIT/NSF_MODE 标志、清空 per-afi 的 nsf 计数与 LLGR stale 定时器，
 * 取消 GR restart/stale 定时器，并清空该 peer 的全部路由（bgp_clear_route_all）。
 * 调用时机：正常结束 GR helper 会话或 peer 删除时。
 */
void peer_nsf_stop(struct peer *peer)
{
	afi_t afi;
	safi_t safi;

	UNSET_FLAG(peer->sflags, PEER_STATUS_NSF_WAIT);
	UNSET_FLAG(peer->sflags, PEER_STATUS_NSF_MODE);

	FOREACH_AFI_SAFI_NSF (afi, safi) {
		peer->nsf[afi][safi] = 0;
		event_cancel(&peer->t_llgr_stale[afi][safi]);
	}

	if (peer->connection->t_gr_restart) {
		event_cancel(&peer->connection->t_gr_restart);
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%pBP graceful restart timer stopped", peer);
	}
	if (peer->connection->t_gr_stale) {
		event_cancel(&peer->connection->t_gr_stale);
		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%pBP graceful restart stalepath timer stopped",
				peer);
	}
	bgp_clear_route_all(peer);
}

/* Delete peer from configuration.
 *
 * The peer is moved to a dead-end "Deleted" neighbour-state, to allow
 * it to "cool off" and refcounts to hit 0, at which state it is freed.
 *
 * This function /should/ take care to be idempotent, to guard against
 * it being called multiple times through stray events that come in
 * that happen to result in this function being called again.  That
 * said, getting here for a "Deleted" peer is a bug in the neighbour
 * FSM.
 */
/*
 * 从配置中删除一个 peer（`no neighbor X` / 接口删除 / 动态邻居回收等的核心）。
 *
 * 设计要点：peer 不是立即释放，而是先被推进到“Deleted”这个死胡同状态，
 * 等待引用计数归零（最后一次 peer_unlock）时才在 peer_free 中真释放。这样能
 * 容忍“稍后才到达的事件/回调”仍持有引用的情况。函数应尽量幂等。
 *
 * 主要步骤（顺序敏感）：
 *  1) 取消软重配任务；停 keepalive/read/write IO，并断言各类 IO/keepalive 已关。
 *  2) 从实例的“连接错误队列”中摘除（加锁）。
 *  3) 若处于 NSF，先 peer_nsf_stop；置 PEER_FLAG_DELETE 作为删除中标记。
 *  4) 移除 BFD 配置；对每族关闭 dampening（必须在从 peer-group 摘除之前）。
 *  5) 若属于 peer-group：从 group->peer 链表摘除并 peer_unlock（组引用）。
 *     ——务必先保存 group 指针再 unlock（见下方 CID 109492 注释）。
 *  6) 记 last_reset=NEIGHBOR_DELETE；直接调 bgp_stop 撤路由（不能用 BGP_EVENT_ADD，
 *     因为事件会在 peer 结构删除后才执行）。
 *  7) 断开 doppelganger 互指；把 FSM 状态置 Deleted；从 NHT 解绑。
 *  8) 处理 password（若是配置节点且非组则卸载 MD5）。
 *  9) bgp_timer_set（此时会把 Deleted peer 的所有定时器停掉）。
 * 10) 从 bgp->peer 链表与 connectionhash 摘除并 peer_unlock（链表引用）。
 * 11) 释放 su_local/su_remote、各族的过滤器名、default_rmap 名、soo 等。
 * 12) 对每族 peer_af_delete；释放 hostname/domainname/soft_version。
 * 13) 最后 peer_unlock(初始引用)，若计数为 0 则由 peer_free 释放。
 */
int peer_delete(struct peer *peer)
{
	int i;
	afi_t afi;
	safi_t safi;
	struct bgp *bgp;
	struct bgp_filter *filter;
	struct listnode *pn;
	int accept_peer;

	assert(peer->connection->status != Deleted);

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s: peer %pBP", __func__, peer);

	bgp = peer->bgp;
	accept_peer = !peer_is_config_node(peer);

	bgp_soft_reconfig_table_task_cancel(bgp, NULL, peer);

	bgp_keepalives_off(peer->connection);
	bgp_reads_off(peer->connection);
	bgp_writes_off(peer->connection);
	event_cancel_event_ready(bm->master, peer->connection);
	assert(!CHECK_FLAG(peer->connection->thread_flags,
			   PEER_THREAD_WRITES_ON));
	assert(!CHECK_FLAG(peer->connection->thread_flags,
			   PEER_THREAD_READS_ON));
	assert(!CHECK_FLAG(peer->thread_flags, PEER_THREAD_KEEPALIVES_ON));

	/* Ensure the peer is removed from the connection error list */
	frr_with_mutex (&bgp->peer_errs_mtx) {
		if (bgp_peer_conn_errlist_anywhere(peer->connection))
			bgp_peer_conn_errlist_del(&bgp->peer_conn_errlist,
						  peer->connection);
	}

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_NSF_WAIT))
		peer_nsf_stop(peer);

	SET_FLAG(peer->flags, PEER_FLAG_DELETE);

	/* Remove BFD settings. */
	if (peer->bfd_config)
		bgp_peer_remove_bfd_config(peer);

	/* Delete peer route flap dampening configuration. This needs to happen
	 * before removing the peer from peer groups.
	 */
	FOREACH_AFI_SAFI (afi, safi)
		if (peer_af_flag_check(peer, afi, safi,
				       PEER_FLAG_CONFIG_DAMPENING))
			bgp_peer_damp_disable(peer, afi, safi);

	/* If this peer belongs to peer group, clear up the
	   relationship.  */
	if (peer->group) {
		/* CID 109492: Save peer->group pointer BEFORE peer_unlock().
		 * peer_unlock() can free the peer structure if refcount reaches
		 * zero, making any subsequent access to peer->group a
		 * use-after-free bug. We must:
		 * 1. Save group pointer while peer is valid
		 * 2. Delete from group->peer list
		 * 3. Unlock peer LAST (may free peer structure)
		 * This prevents NULL dereference if peer_unlock() returns NULL.
		 */
		/* 中文要点：peer_unlock() 可能触发 peer_free 释放 peer，因此必须先把
		 * group 指针存到局部变量，之后再从 group->peer 摘除成员，最后才 unlock——
		 * 否则 peer->group 访问就是 use-after-free（CVE 类静态扫描 CID 109492）。 */
		struct peer_group *group = peer->group;

		if (peer_dynamic_neighbor(peer))
			peer_drop_dynamic_neighbor(peer);

		if ((pn = listnode_lookup(group->peer, peer))) {
			list_delete_node(group->peer, pn);
			peer_unlock(peer); /* group->peer list reference */
		}
		peer->group = NULL;
	}

	/* Withdraw all information from routing table.  We can not use
	 * BGP_EVENT_ADD (peer, BGP_Stop) at here.  Because the event is
	 * executed after peer structure is deleted.
	 */
	peer_set_last_reset(peer, PEER_DOWN_NEIGHBOR_DELETE);
	bgp_stop(peer->connection);
	UNSET_FLAG(peer->flags, PEER_FLAG_DELETE);

	if (peer->doppelganger) {
		peer->doppelganger->doppelganger = NULL;
		peer->doppelganger = NULL;
	}

	bgp_fsm_change_status(peer->connection, Deleted);

	/* Remove from NHT */
	if (peer_is_config_node(peer))
		bgp_unlink_nexthop_by_peer(peer);

	/* Password configuration */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_PASSWORD)) {
		XFREE(MTYPE_PEER_PASSWORD, peer->password);
		if (!accept_peer && !BGP_CONNECTION_SU_UNSPEC(peer->connection) &&
		    !CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
			bgp_md5_unset(peer->connection);
	}

	bgp_timer_set(peer->connection); /* stops all timers for Deleted */

	/* Delete from all peer list. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)
	    && (pn = listnode_lookup(bgp->peer, peer))) {
		/*
		 * Removing from the list node first because
		 * peer_unlock *can* call peer_delete( I know,
		 * I know ).  So let's remove it and in
		 * the su recalculate function we'll ensure
		 * it's in there or not.
		 */
		list_delete_node(bgp->peer, pn);
		hash_release(bgp->connectionhash, peer->connection);
		peer_unlock(peer); /* bgp peer list reference */
	}

	/* Local and remote addresses. */
	if (peer->connection->su_local) {
		sockunion_free(peer->connection->su_local);
		peer->connection->su_local = NULL;
	}

	if (peer->connection->su_remote) {
		sockunion_free(peer->connection->su_remote);
		peer->connection->su_remote = NULL;
	}

	/* Free filter related memory.  */
	FOREACH_AFI_SAFI (afi, safi) {
		filter = &peer->filter[afi][safi];

		for (i = FILTER_IN; i < FILTER_MAX; i++) {
			XFREE(MTYPE_BGP_FILTER_NAME, filter->dlist[i].name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->plist[i].name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->aslist[i].name);
		}

		for (i = RMAP_IN; i < RMAP_MAX; i++) {
			XFREE(MTYPE_BGP_FILTER_NAME, filter->map[i].name);
		}

		XFREE(MTYPE_BGP_FILTER_NAME, filter->usmap.name);
		XFREE(MTYPE_ROUTE_MAP_NAME, peer->default_rmap[afi][safi].name);
		ecommunity_free(&peer->soo[afi][safi]);
	}

	FOREACH_AFI_SAFI (afi, safi)
		peer_af_delete(peer, afi, safi);

	XFREE(MTYPE_BGP_PEER_HOST, peer->hostname);
	XFREE(MTYPE_BGP_PEER_HOST, peer->domainname);
	XFREE(MTYPE_BGP_SOFT_VERSION, peer->soft_version);

	peer_unlock(peer); /* initial reference */

	return 0;
}

/* ========== 4. peer-group（配置模板）：创建/查找/展开/销毁 ========== */

/* peer-group 排序比较：按组名 strcmp，使 bgp->group 链表有序。 */
static int peer_group_cmp(struct peer_group *g1, struct peer_group *g2)
{
	return strcmp(g1->name, g2->name);
}

/* Peer group configuration. */
/* 分配一个零初始化的 peer_group 结构。 */
static struct peer_group *peer_group_new(void)
{
	return XCALLOC(MTYPE_PEER_GROUP, sizeof(struct peer_group));
}

/* 释放 peer_group 结构本身（成员/配置的清理由 peer_group_delete 负责）。 */
static void peer_group_free(struct peer_group *group)
{
	XFREE(MTYPE_PEER_GROUP, group);
}

/* 按名字在实例的 group 链表中查找 peer-group（线性扫描）。 */
struct peer_group *peer_group_lookup(struct bgp *bgp, const char *name)
{
	struct peer_group *group;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group)) {
		if (strcmp(group->name, name) == 0)
			return group;
	}
	return NULL;
}

/*
 * 获取（存在则返回，不存在则创建）一个 peer-group。
 *
 * 关键设计：peer-group 的“配置载体”是一个特殊的 peer 对象 group->conf，
 * 它带有 PEER_STATUS_GROUP 标志，所有组级配置都写在它身上；真正的成员 peer 通过
 * peer_group2peer_config_copy* 从这里继承配置。group->conf 本身不参与会话。
 *
 * 创建时：按 bgp->default_af 给 conf 预置 afc（使新建成员默认继承这些族的激活），
 * 初始化 listen_range（动态邻居范围，按 afi 各一个链表）、host/name、AS_UNSPECIFIED、
 * TTL/GTSM、EBGP 的 advertise-interval，并加入 bgp->group 链表（有序）。
 */
struct peer_group *peer_group_get(struct bgp *bgp, const char *name)
{
	struct peer_group *group;
	afi_t afi;
	safi_t safi;

	group = peer_group_lookup(bgp, name);
	if (group)
		return group;

	group = peer_group_new();
	group->bgp = bgp;
	XFREE(MTYPE_PEER_GROUP_HOST, group->name);
	group->name = XSTRDUP(MTYPE_PEER_GROUP_HOST, name);
	group->peer = list_new();
	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		group->listen_range[afi] = list_new();
	group->conf = peer_new(bgp, NULL, UNKNOWN);
	FOREACH_AFI_SAFI (afi, safi) {
		if (bgp->default_af[afi][safi])
			group->conf->afc[afi][safi] = 1;
	}
	XFREE(MTYPE_BGP_PEER_HOST, group->conf->host);
	group->conf->host = XSTRDUP(MTYPE_BGP_PEER_HOST, name);
	group->conf->group = group;
	group->conf->as = 0;
	group->conf->as_type = AS_UNSPECIFIED;
	group->conf->ttl = BGP_DEFAULT_TTL;
	group->conf->gtsm_hops = BGP_GTSM_HOPS_DISABLED;
	group->conf->v_routeadv = BGP_DEFAULT_EBGP_ROUTEADV;
	SET_FLAG(group->conf->sflags, PEER_STATUS_GROUP);
	listnode_add_sort(bgp->group, group);

	return group;
}

/*
 * 【peer-group 展开·全局版】把组的“整机属性”（非 per-afi）按继承三件套合并到成员 peer。
 *
 * 与 _af 版同构的核心公式：
 *   peer->flags = (peer->flags & override) | (((conf->flags & ~override) ^ conf_invert ^ peer_invert) & ~override)
 * 即：本 peer 显式 override 的位保留自身；其余位 = 组值经两层 invert 修正后的结果。
 * 同时把 conf->flags_invert 复制到 peer->flags_invert（保持 invert 状态一致）。
 *
 * 之后逐项按 override 判定继承：remote-as、local-as、ttl、gtsm、各类 timer
 * （holdtime/keepalive/connect/delayopen/routeadv）、各能力位（ENHE、soft-version
 * 新旧、dynamic、link-local）、password、update-source/if、role、GR 标志、BFD、
 * tcp-mss 等。凡未 override 的项一律改为组值。
 * 注意 config_node 需要重新打回 PEER_FLAG_CONFIG_NODE（上面的 UNSET 可能清掉了它）。
 *
 * 调用时机：peer-group 配置变更或 peer 加入组时（peer_group_bind 等）。
 */
static void peer_group2peer_config_copy(struct peer_group *group,
					struct peer *peer)
{
	uint64_t flags_tmp;
	struct peer *conf;
	bool config_node = peer_is_config_node(peer);

	conf = group->conf;

	/* remote-as */
	if (conf->as)
		peer->as = conf->as;

	/* local-as */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_LOCAL_AS))
		peer->change_local_as = conf->change_local_as;

	/* If peer-group has configured TTL then override it */
	if (conf->ttl != BGP_DEFAULT_TTL)
		peer->ttl = conf->ttl;

	/* GTSM hops */
	peer->gtsm_hops = conf->gtsm_hops;

	/* peer flags apply */
	flags_tmp = CHECK_FLAG(conf->flags, ~peer->flags_override);
	flags_tmp ^= conf->flags_invert ^ peer->flags_invert;
	UNSET_FLAG(flags_tmp, peer->flags_override);

	UNSET_FLAG(peer->flags, ~peer->flags_override);
	SET_FLAG(peer->flags, flags_tmp);
	SET_FLAG(peer->flags_invert, conf->flags_invert);

	if (config_node)
		SET_FLAG(peer->flags, PEER_FLAG_CONFIG_NODE);

	/* peer timers apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_TIMER)) {
		PEER_ATTR_INHERIT(peer, group, holdtime);
		PEER_ATTR_INHERIT(peer, group, keepalive);
	}

	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_TIMER_CONNECT)) {
		PEER_ATTR_INHERIT(peer, group, connect);
		if (CHECK_FLAG(conf->flags, PEER_FLAG_TIMER_CONNECT))
			peer->v_connect = conf->connect;
		else
			peer->v_connect = peer->bgp->default_connect_retry;
	}

	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_TIMER_DELAYOPEN)) {
		PEER_ATTR_INHERIT(peer, group, delayopen);
		if (CHECK_FLAG(conf->flags, PEER_FLAG_TIMER_DELAYOPEN))
			peer->v_delayopen = conf->delayopen;
		else
			peer->v_delayopen = peer->bgp->default_delayopen;
	}

	/* advertisement-interval apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_ROUTEADV)) {
		PEER_ATTR_INHERIT(peer, group, routeadv);
		if (CHECK_FLAG(conf->flags, PEER_FLAG_ROUTEADV))
			peer->v_routeadv = conf->routeadv;
		else
			peer->v_routeadv = (peer_sort(peer) == BGP_PEER_IBGP)
						   ? BGP_DEFAULT_IBGP_ROUTEADV
						   : BGP_DEFAULT_EBGP_ROUTEADV;
	}

	/* capability extended-nexthop apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_CAPABILITY_ENHE))
		if (CHECK_FLAG(conf->flags, PEER_FLAG_CAPABILITY_ENHE))
			SET_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE);

	/* capability software-version apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD))
		if (CHECK_FLAG(conf->flags, PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD))
			SET_FLAG(peer->flags, PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD);
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW))
		if (CHECK_FLAG(conf->flags, PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW))
			SET_FLAG(peer->flags, PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW);

	/* capability dynamic apply */
	if (!CHECK_FLAG(peer->flags_override,
			PEER_FLAG_DYNAMIC_CAPABILITY))
		if (CHECK_FLAG(conf->flags, PEER_FLAG_DYNAMIC_CAPABILITY))
			SET_FLAG(peer->flags,
				 PEER_FLAG_DYNAMIC_CAPABILITY);

	/* capability link-local apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_CAPABILITY_LINK_LOCAL))
		if (CHECK_FLAG(conf->flags, PEER_FLAG_CAPABILITY_LINK_LOCAL))
			SET_FLAG(peer->flags, PEER_FLAG_CAPABILITY_LINK_LOCAL);

	/* password apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_PASSWORD))
		PEER_STR_ATTR_INHERIT(peer, group, password,
				      MTYPE_PEER_PASSWORD);

	if (!BGP_CONNECTION_SU_UNSPEC(peer->connection))
		bgp_md5_set(peer->connection);

	/* update-source apply */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_UPDATE_SOURCE)) {
		if (conf->update_source) {
			XFREE(MTYPE_PEER_UPDATE_SOURCE, peer->update_if);
			PEER_SU_ATTR_INHERIT(peer, group, update_source);
		} else if (conf->update_if) {
			sockunion_free(peer->update_source);
			PEER_STR_ATTR_INHERIT(peer, group, update_if,
					      MTYPE_PEER_UPDATE_SOURCE);
		}
	}

	/* role */
	PEER_ATTR_INHERIT(peer, group, local_role);

	/* Update GR flags for the peer. */
	PEER_ATTR_INHERIT(peer, group, peer_gr_new_status_flag);
	bgp_peer_gr_flags_update(peer);

	/* Apply BFD settings from group to peer if it exists. */
	if (conf->bfd_config) {
		bgp_peer_configure_bfd(peer, false);
		bgp_peer_config_apply(peer, group);
	}
	/* peer tcp-mss */
	if (!CHECK_FLAG(peer->flags_override, PEER_FLAG_TCP_MSS))
		PEER_ATTR_INHERIT(peer, group, tcp_mss);
}

/* Peer group's remote AS configuration.  */
/*
 * 设置 peer-group 的远程 AS：先改组配置载体 group->conf 的 AS，再把组内每个成员的
 * AS 一并改掉（改 AS 必须全体一致）。
 *
 * 对 BGP unnumbered（接口邻居）成员：在组重新配置 AS 时重新发起 RA（因为
 * peer_group_remote_as_delete 会终止 RA，需要恢复）。
 * 成员 AS/类型确实变化时：调用 peer_as_change 并注入 BGP_Start 事件触发 FSM 重新
 * 建立会话（连接在 start 定时器到期后发起）；若 peer 处于“启动被抑制”状态则不加事件。
 * @return : 0；-1 表示组不存在。
 */
int peer_group_remote_as(struct bgp *bgp, const char *group_name, as_t *as,
			 enum peer_asn_type as_type, const char *as_str)
{
	struct peer_group *group;
	struct peer *peer;
	struct listnode *node, *nnode;

	group = peer_group_lookup(bgp, group_name);
	if (!group)
		return -1;

	if ((as_type == group->conf->as_type) && (group->conf->as == *as))
		return 0;


	/* When we setup peer-group AS number all peer group member's AS
	   number must be updated to same number.  */
	peer_as_change(group->conf, *as, as_type, as_str);

	for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
		/*
		 * Re-initiate RA for BGP unnumbered peers when peer group gets
		 * remote-as configured. This ensures RA is restored after
		 * peer_group_remote_as_delete() terminated it
		 */
		if (peer->conf_if && peer->ifp &&
		    CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE))
			bgp_zebra_initiate_radv(peer->bgp, peer);

		if (((peer->as_type == AS_SPECIFIED) && peer->as != *as) ||
		    (peer->as_type != as_type)) {
			peer_as_change(peer, *as, as_type, as_str);
			if (bgp_debug_neighbor_events(peer))
				zlog_debug("%s peer %s set to as_type %u curr status %s trigger BGP_Start",
					   __func__, peer->host, peer->as_type,
					   lookup_msg(bgp_status_msg,
						      peer->connection->status, NULL));
			/* Start Peer FSM to form neighbor using new as,
			 * NOTE: the connection is triggered upon start
			 * timer expiry.
			 */
			if (!BGP_PEER_START_SUPPRESSED(peer))
				BGP_EVENT_ADD(peer->connection, BGP_Start);
		}
	}

	return 0;
}

/*
 * 向对端发送 CEASE/CONFIG_CHANGE 通知，要求对端因本地配置变更而重建会话。
 * @return : true 表示“已发送通知”（会话状态允许发通知）；false 表示状态不允许
 *           （此时调用者通常改用 bgp_session_reset 直接复位）。
 * 这个“有通知就通知、没有就复位”的模式在下文大量出现，是配置变更触发会话重建的统一手法。
 */
bool peer_notify_config_change(struct peer_connection *connection)
{
	if (BGP_IS_VALID_STATE_FOR_NOTIF(connection->status)) {
		bgp_notify_send(connection, BGP_NOTIFY_CEASE, BGP_NOTIFY_CEASE_CONFIG_CHANGE);
		return true;
	}

	return false;
}

/* 发送 CEASE/PEER_UNCONFIG 通知：告知对端本端已删除该邻居配置。 */
void peer_notify_unconfig(struct peer_connection *connection)
{
	if (BGP_IS_VALID_STATE_FOR_NOTIF(connection->status))
		bgp_notify_send(connection, BGP_NOTIFY_CEASE, BGP_NOTIFY_CEASE_PEER_UNCONFIG);
}

/*
 * 发送 CEASE/ADMIN_SHUTDOWN 通知（配置关闭邻居时）。特殊点：若本 peer 支持
 * Graceful Restart，则“跳过”发送该通知（并打印调试）——因为 GR 语义下我们希望
 * 对端保留路由而不是立即拆除，直接发 shutdown 会破坏 GR。
 */
static void peer_notify_shutdown(struct peer *peer)
{
	if (BGP_PEER_GRACEFUL_RESTART_CAPABLE(peer)) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%pBP configured Graceful-Restart, skipping shutdown notification",
				peer);
		return;
	}

	if (BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status))
		bgp_notify_send(peer->connection, BGP_NOTIFY_CEASE,
				BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
}

/* 对整个 peer-group 的成员（及其 doppelganger）发 PEER_UNCONFIG 通知。
 * 对 doppelganger 先解除其 group 关联再通知，避免其仍被视作组成员。 */
void peer_group_notify_unconfig(struct peer_group *group)
{
	struct peer *peer, *other;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
		other = peer->doppelganger;
		if (other && other->connection->status != Deleted) {
			other->group = NULL;
			peer_notify_unconfig(other->connection);
		}
		peer_notify_unconfig(peer->connection);
	}
}

/*
 * 删除整个 peer-group（`no neighbor <group>`）。
 *
 * 步骤：对所有成员（含各自 doppelganger，先解除其 group 关联）先终止 RA（若 ENHE）
 * 再 peer_delete → 删除成员链表 → 逐个释放各 afi 的 listen_range 前缀并删表 →
 * 释放组名 → 移除 conf 上的 BFD 配置 → 解除 conf->group 关联后 peer_delete(group->conf)
 * （组配置载体也是一个 peer，需要正常销毁）→ 从 bgp->group 链表摘除 → 释放组结构。
 */
int peer_group_delete(struct peer_group *group)
{
	struct bgp *bgp;
	struct peer *peer;
	struct prefix *prefix;
	struct peer *other;
	struct listnode *node, *nnode;
	afi_t afi;

	bgp = group->bgp;

	for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
		other = peer->doppelganger;

		if (CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE))
			bgp_zebra_terminate_radv(bgp, peer);

		peer_delete(peer);
		if (other && other->connection->status != Deleted) {
			other->group = NULL;
			peer_delete(other);
		}
	}
	list_delete(&group->peer);

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		for (ALL_LIST_ELEMENTS(group->listen_range[afi], node, nnode,
				       prefix)) {
			prefix_free(&prefix);
		}
		list_delete(&group->listen_range[afi]);
	}

	XFREE(MTYPE_PEER_GROUP_HOST, group->name);
	group->name = NULL;

	if (group->conf->bfd_config)
		bgp_peer_remove_bfd_config(group->conf);

	group->conf->group = NULL;
	peer_delete(group->conf);

	/* Delete from all peer_group list. */
	listnode_delete(bgp->group, group);

	peer_group_free(group);

	return 0;
}

/*
 * 取消 peer-group 的远程 AS（`no neighbor <group> remote-as`）。
 * 对每个成员：先终止 RA（若 ENHE），再把成员 AS 复位为 0/AS_UNSPECIFIED
 * （peer_as_change 会顺带复位会话）；最后把组 conf 的 AS 也清空。
 * 若组本就未配置 AS 则直接返回。
 */
int peer_group_remote_as_delete(struct peer_group *group)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if ((group->conf->as_type == AS_UNSPECIFIED)
	    || ((!group->conf->as) && (group->conf->as_type == AS_SPECIFIED)))
		return 0;

	for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
		if (CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE))
			bgp_zebra_terminate_radv(peer->bgp, peer);

		/* reset existing peer connection */
		peer_as_change(peer, 0, AS_UNSPECIFIED, NULL);
	}

	group->conf->as = 0;
	group->conf->as_type = AS_UNSPECIFIED;

	return 0;
}

/*
 * 为 peer-group 添加一个“动态邻居监听范围”（`bgp listen range <prefix> peer-group X`）。
 * 前提：组必须已配置远程 AS，否则返回 BGP_ERR_PEER_GROUP_NO_REMOTE_AS。
 * 去重（相同前缀直接返回，不关心重叠）；把 prefix 副本加入对应 afi 的 listen_range；
 * 若组配置了 password，则为该范围在监听套接字上安装 MD5。
 */
int peer_group_listen_range_add(struct peer_group *group, struct prefix *range)
{
	struct prefix *prefix;
	struct listnode *node, *nnode;
	afi_t afi;

	afi = family2afi(range->family);

	/* Group needs remote AS configured. */
	if (group->conf->as_type == AS_UNSPECIFIED)
		return BGP_ERR_PEER_GROUP_NO_REMOTE_AS;

	/* Ensure no duplicates. Currently we don't care about overlaps. */
	for (ALL_LIST_ELEMENTS(group->listen_range[afi], node, nnode, prefix)) {
		if (prefix_same(range, prefix))
			return 0;
	}

	prefix = prefix_new();
	prefix_copy(prefix, range);
	listnode_add(group->listen_range[afi], prefix);

	/* Update passwords for new ranges */
	if (group->conf->password)
		bgp_md5_set_prefix(group->bgp, prefix, group->conf->password);

	return 0;
}

/*
 * 删除 peer-group 的某个动态邻居监听范围。
 * 先按前缀找到对应 range（找不到返回 BGP_ERR_DYNAMIC_NEIGHBORS_RANGE_NOT_FOUND）；
 * 然后回收“由该范围创建的动态邻居”：凡 peer_dynamic_neighbor(peer) 且其地址落在
 * 该前缀内者，打印调试并 peer_delete；最后从链表摘除该前缀、卸载其 MD5、释放前缀。
 */
int peer_group_listen_range_del(struct peer_group *group, struct prefix *range)
{
	struct prefix *prefix, prefix2;
	struct listnode *node, *nnode;
	struct peer *peer;
	afi_t afi;

	afi = family2afi(range->family);

	/* Identify the listen range. */
	for (ALL_LIST_ELEMENTS(group->listen_range[afi], node, nnode, prefix)) {
		if (prefix_same(range, prefix))
			break;
	}

	if (!prefix)
		return BGP_ERR_DYNAMIC_NEIGHBORS_RANGE_NOT_FOUND;

	/* Dispose off any dynamic neighbors that exist due to this listen range
	 */
	for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
		if (!peer_dynamic_neighbor(peer))
			continue;

		if (sockunion2hostprefix(&peer->connection->su, &prefix2) &&
		    prefix_match(prefix, &prefix2)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug(
					"Deleting dynamic neighbor %s group %s upon delete of listen range %pFX",
					peer->host, group->name, prefix);
			peer_delete(peer);
		}
	}

	/* Get rid of the listen range */
	listnode_delete(group->listen_range[afi], prefix);

	/* Remove passwords for deleted ranges */
	if (group->conf->password)
		bgp_md5_unset_prefix(group->bgp, prefix);

	prefix_free(&prefix);
	return 0;
}

/* Bind specified peer to peer group.  */
/*
 * 把一个 peer 绑定到 peer-group（`neighbor X peer-group Y`）。
 *
 * @su / @peer : 定位 peer（传 peer 直接用，否则按 su 查）。
 * @group      : 目标组。
 * @as         : 出错时回填“实际 AS”供调用者提示。
 * @return     : 0 成功；BGP_ERR_PEER_GROUP_CANT_CHANGE（已在别的组）、
 *               BGP_ERR_PEER_GROUP_PEER_TYPE_DIFFERENT（类型不一致）、
 *               BGP_ERR_PEER_GROUP_NO_REMOTE_AS（建新 peer 但组无 AS）等。
 *
 * peer 已存在分支：
 *  - 已在同组 → 直接 0；在别组 → 不允许换组。
 *  - 若 peer 未指定 AS，则从组继承 as/as_type/排序。
 *  - 校验类型一致性：当组未显式指定 AS 且成员已定性时，成员类型须与组一致
 *    （组为 INTERNAL 时，第一个成员反过来给组定性 → first_member=1）。
 *  - 调 peer_group2peer_config_copy 及按族 _af 版完成继承；对组已激活而成员未激活
 *    的族补齐激活，反之停用。
 *  - 维护 bgp->peer 排序与 group->peer 链表（注意这里要重新 peer_lock 一次作为
 *    group->peer 的引用）。first_member 时还要按组成员类型校准组的 advertise-interval
 *    与 TTL。
 *  - 最后打 CONFIG_NODE 标志并复位会话使新配置生效。
 *
 * peer 不存在分支：创建新 peer（继承组 as），加入 group->peer，做组配置继承与族激活，
 * 并在活跃时启动定时器。
 */
int peer_group_bind(struct bgp *bgp, union sockunion *su, struct peer *peer,
		    struct peer_group *group, as_t *as)
{
	int first_member = 0;
	afi_t afi;
	safi_t safi;
	enum bgp_peer_sort ptype, gtype;

	/* Lookup the peer.  */
	if (!peer)
		peer = peer_lookup(bgp, su);

	/* The peer exist, bind it to the peer-group */
	if (peer) {
		/* When the peer already belongs to a peer-group, check the
		 * consistency.  */
		if (peer_group_active(peer)) {

			/* The peer is already bound to the peer-group,
			 * nothing to do
			 */
			if (strcmp(peer->group->name, group->name) == 0)
				return 0;
			else
				return BGP_ERR_PEER_GROUP_CANT_CHANGE;
		}

		/* The peer has not specified a remote-as, inherit it from the
		 * peer-group */
		if (peer->as_type == AS_UNSPECIFIED) {
			peer->as_type = group->conf->as_type;
			peer->as = group->conf->as;
			peer->sort = group->conf->sort;
			peer->sub_sort = group->conf->sub_sort;
		}

		ptype = peer_sort(peer);
		if (!group->conf->as && ptype != BGP_PEER_UNSPECIFIED) {
			gtype = peer_sort(group->conf);
			if ((gtype != BGP_PEER_INTERNAL) && (gtype != ptype)) {
				if (as)
					*as = peer->as;
				return BGP_ERR_PEER_GROUP_PEER_TYPE_DIFFERENT;
			}

			if (gtype == BGP_PEER_INTERNAL)
				first_member = 1;
		}

		peer_group2peer_config_copy(group, peer);

		FOREACH_AFI_SAFI (afi, safi) {
			if (group->conf->afc[afi][safi]) {
				peer->afc[afi][safi] = 1;

				if (peer_af_find(peer, afi, safi)
				    || peer_af_create(peer, afi, safi)) {
					peer_group2peer_config_copy_af(
						group, peer, afi, safi);
				}
			} else if (peer->afc[afi][safi])
				peer_deactivate(peer, afi, safi);
		}

		if (peer->group) {
			assert(group && peer->group == group);
		} else {
			listnode_delete(bgp->peer, peer);

			peer->group = group;
			listnode_add_sort(bgp->peer, peer);

			peer = peer_lock(peer); /* group->peer list reference */
			listnode_add(group->peer, peer);
		}

		if (first_member) {
			gtype = peer_sort(group->conf);
			/* Advertisement-interval reset */
			if (!CHECK_FLAG(group->conf->flags,
					PEER_FLAG_ROUTEADV)) {
				group->conf->v_routeadv =
					(gtype == BGP_PEER_IBGP)
						? BGP_DEFAULT_IBGP_ROUTEADV
						: BGP_DEFAULT_EBGP_ROUTEADV;
			}

			/* ebgp-multihop reset */
			if (gtype == BGP_PEER_IBGP)
				group->conf->ttl = MAXTTL;
		}

		SET_FLAG(peer->flags, PEER_FLAG_CONFIG_NODE);

		peer_set_last_reset(peer, PEER_DOWN_RMAP_BIND);

		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);
	}

	/* Create a new peer. */
	else {
		if ((group->conf->as_type == AS_SPECIFIED)
		    && (!group->conf->as)) {
			return BGP_ERR_PEER_GROUP_NO_REMOTE_AS;
		}

		peer = peer_create(su, NULL, bgp, bgp->as, group->conf->as, group->conf->as_type,
				   group, true, NULL, CONNECTION_OUTGOING);

		peer = peer_lock(peer); /* group->peer list reference */
		listnode_add(group->peer, peer);

		peer_group2peer_config_copy(group, peer);

		/* If the peer-group is active for this afi/safi then activate
		 * for this peer */
		FOREACH_AFI_SAFI (afi, safi) {
			if (group->conf->afc[afi][safi]) {
				peer->afc[afi][safi] = 1;

				if (!peer_af_find(peer, afi, safi))
					peer_af_create(peer, afi, safi);

				peer_group2peer_config_copy_af(group, peer, afi,
							       safi);
			} else if (peer->afc[afi][safi])
				peer_deactivate(peer, afi, safi);
		}

		/* Set up peer's events and timers. */
		if (peer_active(peer->connection) == BGP_PEER_ACTIVE)
			bgp_timer_set(peer->connection);
	}

	return 0;
}

/*
 * 启动（startup）定时器到期回调：仅打印调试并清空 bgp->t_startup 句柄。
 * 该定时器用于实例启动后延迟一段时间再开始建立邻居（给 IGP/接口就绪留时间），
 * 到期本身不做额外动作——因为真正的“开始”由各 peer 的 FSM 在定时器到期后推进，
 * 这里主要起“解除抑制、标记就绪”的作用。
 */
static void bgp_startup_timer_expire(struct event *event)
{
	struct bgp *bgp;

	bgp = EVENT_ARG(event);

	if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
		zlog_debug("%s: Startup timer expired", bgp->name_pretty);

	bgp->t_startup = NULL;
}

/*
 * On shutdown we call the cleanup function which
 * does a free of the link list nodes,  free up
 * the data we are pointing at too.
 */
/* import/export VRF 名链表的元素释放回调：释放字符串副本（MTYPE_TMP）。 */
static void bgp_vrf_string_name_delete(void *data)
{
	char *vname = data;

	XFREE(MTYPE_TMP, vname);
}

/* ========== 5. bgp 实例生命周期：bgp_create / bgp_get / bgp_delete / bgp_free ========== */

/* BGP instance creation by `router bgp' commands. */
/*
 * 创建一个 BGP 实例（struct bgp）并完成默认初始化。是 `router bgp ASN [vrf X|view Y]`
 * 的落地函数，也是 VRF 自动建实例路径的核心。
 *
 * @as         : AS 号。
 * @name       : VRF 名或 view 名（default 实例为 NULL）。
 * @inst_type  : BGP_INSTANCE_TYPE_DEFAULT / VRF / VIEW。
 * @as_pretty  : AS 的“漂亮”形式（按 asnotation），NULL 则自动生成 asplain。
 * @asnotation : AS 记法；UNDEFINED 时从 as_pretty 解析。
 * @bgp_old    : hidden 模式下复用的已有实例（不重新分配）。
 * @hidden     : true 表示这是一次“隐藏/内部”创建（复用 bgp_old，跳过对外可见的
 *               初始化，如实际 rib 表/list、QOBJ 注册、EVPN/SRV6 init 等）。
 * @return     : 新实例（注意：此处并未加入 bm->bgp 链表，由调用者 bgp_get 负责）。
 *
 * 关键初始化（详见行内注释）：
 *  - as/as_pretty/asnotation；default 实例顺带兼任 EVPN 实例（bm->bgp_evpn）；
 *  - 创建 peer_self（“Static announcement”虚拟 peer，供本机始发路由/聚合/BMP LOC-RIB）；
 *  - peer 链表（cmp=peer_cmp）与 connectionhash；
 *  - 按 afi/safi 初始化 static_routes/aggregate/rib 表、maximum-paths、GR 信息；
 *  - 各类默认值（timers、local-pref、restart/select-defer/stale 时间、动态邻居上限、
 *    链路带宽、addpath、条件路由检查周期等）；
 *  - VPN 策略结构（import/export VRF 名链表，元素释放回调 bgp_vrf_string_name_delete）；
 *  - startup 定时器、name_pretty、读写配额（wpkt/rpkt_quanta）、default_af 默认仅 IPv4
 *    unicast；
 *  - update-group 初始化、RD id 分配、EVPN/PBR/SRV6 init、全局 GR 初始化；
 *  - peer 错误队列与 clearing 链表的初始化（含错误队列互斥锁）。
 */
static struct bgp *bgp_create(as_t *as, const char *name,
			      enum bgp_instance_type inst_type,
			      const char *as_pretty,
			      enum asnotation_mode asnotation,
			      struct bgp *bgp_old, bool hidden)
{
	struct bgp *bgp;
	afi_t afi;
	safi_t safi;

	if (hidden)
		bgp = bgp_old;
	else
		bgp = XCALLOC(MTYPE_BGP, sizeof(struct bgp));

	bgp->as = *as;

	if (bgp->as_pretty)
		XFREE(MTYPE_BGP_NAME, bgp->as_pretty);
	if (as_pretty)
		bgp->as_pretty = XSTRDUP(MTYPE_BGP_NAME, as_pretty);
	else
		bgp->as_pretty = XSTRDUP(MTYPE_BGP_NAME, asn_asn2asplain(*as));

	if (asnotation != ASNOTATION_UNDEFINED) {
		bgp->asnotation = asnotation;
		SET_FLAG(bgp->config, BGP_CONFIG_ASNOTATION);
	} else
		asn_str2asn_notation(bgp->as_pretty, NULL, &bgp->asnotation);

	if (hidden)
		goto peer_init;

	if (BGP_DEBUG(zebra, ZEBRA)) {
		if (inst_type == BGP_INSTANCE_TYPE_DEFAULT)
			zlog_debug("Creating Default VRF, AS %s",
				   bgp->as_pretty);
		else
			zlog_debug("Creating %s %s, AS %s",
				   (inst_type == BGP_INSTANCE_TYPE_VRF)
					   ? "VRF"
					   : "VIEW",
				   name, bgp->as_pretty);
	}

	/* Default the EVPN VRF to the default one */
	if (inst_type == BGP_INSTANCE_TYPE_DEFAULT && !bgp_master.bgp_evpn) {
		bgp_lock(bgp);
		bm->bgp_evpn = bgp;
	}

	bgp_lock(bgp);

	bgp->allow_martian = false;
	bgp_process_queue_init(bgp);
	bgp->heuristic_coalesce = true;
	bgp->inst_type = inst_type;
	bgp->vrf_id = (inst_type == BGP_INSTANCE_TYPE_DEFAULT) ? VRF_DEFAULT
							       : VRF_UNKNOWN;
	bgp->peer_self = peer_new(bgp, NULL, UNKNOWN);
	XFREE(MTYPE_BGP_PEER_HOST, bgp->peer_self->host);
	bgp->peer_self->host =
		XSTRDUP(MTYPE_BGP_PEER_HOST, "Static announcement");
	XFREE(MTYPE_BGP_PEER_HOST, bgp->peer_self->hostname);
	if (cmd_hostname_get())
		bgp->peer_self->hostname =
			XSTRDUP(MTYPE_BGP_PEER_HOST, cmd_hostname_get());

	XFREE(MTYPE_BGP_PEER_HOST, bgp->peer_self->domainname);
	if (cmd_domainname_get())
		bgp->peer_self->domainname =
			XSTRDUP(MTYPE_BGP_PEER_HOST, cmd_domainname_get());
	/* for BMP LOC-RIB */
	/* copy bgp as to peer as */
	bgp->peer_self->local_as = bgp->as;
	/* enable AS4B encoding */
	SET_FLAG(bgp->peer_self->cap, PEER_CAP_AS4_RCV);
	SET_FLAG(bgp->peer_self->cap, PEER_CAP_AS4_ADV);

	bgp->peer = list_new();

peer_init:
	bgp->peer->cmp = (int (*)(void *, void *))peer_cmp;
	bgp->connectionhash = hash_create(connection_hash_key_make, connection_hash_same,
					  "BGP Peer Hash");
	bgp->connectionhash->max_size = BGP_PEER_MAX_HASH_SIZE;

	if (!hidden)
		bgp->group = list_new();
	bgp->group->cmp = (int (*)(void *, void *))peer_group_cmp;

	FOREACH_AFI_SAFI (afi, safi) {
		if (!hidden) {
			bgp->static_routes[afi][safi] = bgp_table_init(bgp, afi, safi);
			bgp->aggregate[afi][safi] = bgp_table_init(bgp, afi,
								   safi);
			bgp->rib[afi][safi] = bgp_table_init(bgp, afi, safi);
		}

		/* Enable maximum-paths */
		bgp_maximum_paths_set(bgp, afi, safi, BGP_PEER_EBGP,
				      multipath_num, 0);
		bgp_maximum_paths_set(bgp, afi, safi, BGP_PEER_IBGP,
				      multipath_num, 0);
		/* Initialize graceful restart info */
		memset(&bgp->gr_info[afi][safi], 0, sizeof(struct graceful_restart_info));
	}

	bgp->v_update_delay = bm->v_update_delay;
	bgp->v_establish_wait = bm->v_establish_wait;
	bgp->default_local_pref = BGP_DEFAULT_LOCAL_PREF;
	bgp->default_subgroup_pkt_queue_max =
		BGP_DEFAULT_SUBGROUP_PKT_QUEUE_MAX;
	bgp_tcp_keepalive_unset(bgp);
	if (!hidden)
		bgp_timers_unset(bgp);
	bgp->default_min_holdtime = 0;
	bgp->restart_time = BGP_DEFAULT_RESTART_TIME;
	bgp->stalepath_time = BGP_DEFAULT_STALEPATH_TIME;
	bgp->select_defer_time = BGP_DEFAULT_SELECT_DEFERRAL_TIME;
	bgp->rib_stale_time = BGP_DEFAULT_RIB_STALE_TIME;
	bgp->dynamic_neighbors_limit = BGP_DYNAMIC_NEIGHBORS_LIMIT_DEFAULT;
	bgp->dynamic_neighbors_count = 0;
	bgp->lb_ref_bw = BGP_LINK_BW_REF_BW;
	bgp->lb_handling = BGP_LINK_BW_ECMP;
	bgp->reject_as_sets = true;
	bgp->condition_check_period = DEFAULT_CONDITIONAL_ROUTES_POLL_TIME;
	bgp_addpath_init_bgp_data(&bgp->tx_addpath);
	bgp->fast_convergence = false;
	bgp->llgr_stale_time = BGP_DEFAULT_LLGR_STALE_TIME;
	bgp->rmap_def_originate_eval_timer = 0;

#ifdef ENABLE_BGP_VNC
	if (inst_type != BGP_INSTANCE_TYPE_VRF && !hidden) {
		bgp->rfapi = bgp_rfapi_new(bgp);
		assert(bgp->rfapi);
		assert(bgp->rfapi_cfg);
	}
#endif /* ENABLE_BGP_VNC */

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		bgp->vpn_policy[afi].bgp = bgp;
		bgp->vpn_policy[afi].afi = afi;
		bgp->vpn_policy[afi].tovpn_label = MPLS_LABEL_NONE;
		bgp->vpn_policy[afi].tovpn_zebra_vrf_label_last_sent =
			MPLS_LABEL_NONE;

		if (!bgp->vpn_policy[afi].import_vrf)
			bgp->vpn_policy[afi].import_vrf = list_new();
		bgp->vpn_policy[afi].import_vrf->del =
			bgp_vrf_string_name_delete;
		if (!hidden) {
			bgp->vpn_policy[afi].export_vrf = list_new();
			bgp->vpn_policy[afi].export_vrf->del =
				bgp_vrf_string_name_delete;
		}
		SET_FLAG(bgp->af_flags[afi][SAFI_MPLS_VPN],
			 BGP_VPNVX_RETAIN_ROUTE_TARGET_ALL);
	}

	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		bgp_label_per_nexthop_cache_init(
			&bgp->mpls_labels_per_nexthop[afi]);

	bgp_mplsvpn_nh_label_bind_cache_init(&bgp->mplsvpn_nh_label_bind);

	if (name && !bgp->name)
		bgp->name = XSTRDUP(MTYPE_BGP_NAME, name);

	event_add_timer(bm->master, bgp_startup_timer_expire, bgp,
			bgp->restart_time, &bgp->t_startup);

	/* printable name we can use in debug messages */
	if (inst_type == BGP_INSTANCE_TYPE_DEFAULT && !hidden) {
		bgp->name_pretty = XSTRDUP(MTYPE_BGP_NAME, "VRF default");
	} else if (!hidden) {
		const char *n;
		int len;

		if (bgp->name)
			n = bgp->name;
		else
			n = "?";

		len = 4 + 1 + strlen(n) + 1;	/* "view foo\0" */

		bgp->name_pretty = XCALLOC(MTYPE_BGP_NAME, len);
		snprintf(bgp->name_pretty, len, "%s %s",
			(bgp->inst_type == BGP_INSTANCE_TYPE_VRF)
				? "VRF"
				: "VIEW",
			n);
	}

	atomic_store_explicit(&bgp->wpkt_quanta, BGP_WRITE_PACKET_MAX,
			      memory_order_relaxed);
	atomic_store_explicit(&bgp->rpkt_quanta, BGP_READ_PACKET_MAX,
			      memory_order_relaxed);
	bgp->coalesce_time = BGP_DEFAULT_SUBGROUP_COALESCE_TIME;
	bgp->default_af[AFI_IP][SAFI_UNICAST] = true;

	if (!hidden)
		QOBJ_REG(bgp, bgp);

	update_bgp_group_init(bgp);

	if (!hidden) {
		/* assign a unique rd id for auto derivation of vrf's RD */
		bf_assign_index(bm->rd_idspace, bgp->vrf_rd_id);

		bgp_evpn_init(bgp);
		bgp_evpn_vrf_es_init(bgp);
		bgp_pbr_init(bgp);
		bgp_srv6_init(bgp);
	}

	/*initialize global GR FSM */
	bgp_global_gr_init(bgp);

	memset(&bgp->ebgprequirespolicywarning, 0,
	       sizeof(bgp->ebgprequirespolicywarning));

	/* Init peer connection error info */
	pthread_mutex_init(&bgp->peer_errs_mtx, NULL);
	bgp_peer_conn_errlist_init(&bgp->peer_conn_errlist);
	bgp_clearing_info_init(&bgp->clearing_list);

	return bgp;
}

/* Return the "default VRF" instance of BGP. */
/* 返回 default 实例（inst_type==DEFAULT）；不存在则 NULL。 */
struct bgp *bgp_get_default(void)
{
	struct bgp *bgp;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp))
		if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT)
			return bgp;
	return NULL;
}

/* Lookup BGP entry. */
/* 按 (AS, name) 查找实例；name 的比较要求二者同为 NULL 或同为相等字符串。 */
struct bgp *bgp_lookup(as_t as, const char *name)
{
	struct bgp *bgp;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp))
		if (bgp->as == as
		    && ((bgp->name == NULL && name == NULL)
			|| (bgp->name && name && strcmp(bgp->name, name) == 0)))
			return bgp;
	return NULL;
}

/* Lookup BGP structure by view name. */
/*
 * 按名字查找实例（VRF 名或 view 名）。@filter_auto=true 时跳过“自动创建”的实例
 * （带 BGP_VRF_AUTO 标志，通常由 Zebra 的 VRF 事件自动建），使 CLI 查询只命中
 * 用户显式配置的实例。
 */
struct bgp *bgp_lookup_by_name_filter(const char *name, bool filter_auto)
{
	struct bgp *bgp;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		if (filter_auto && CHECK_FLAG(bgp->vrf_flags, BGP_VRF_AUTO))
			continue;
		if ((bgp->name == NULL && name == NULL)
		    || (bgp->name && name && strcmp(bgp->name, name) == 0))
			return bgp;
	}
	return NULL;
}

/* 按名字查找实例（过滤掉自动创建的实例，见 bgp_lookup_by_name_filter）。 */
struct bgp *bgp_lookup_by_name(const char *name)
{
	return bgp_lookup_by_name_filter(name, true);
}

/* Lookup BGP instance based on VRF id. */
/* Note: Only to be used for incoming messages from Zebra. */
/*
 * 按 vrf_id 找实例：先查 lib 的 VRF（vrf_lookup_by_id），再取 vrf->info（本实例）。
 * 注释强调：仅用于处理来自 Zebra 的消息（因为它依赖 vrf->info 这一“外部”关联）。
 */
struct bgp *bgp_lookup_by_vrf_id(vrf_id_t vrf_id)
{
	struct vrf *vrf;

	/* Lookup VRF (in tree) and follow link. */
	vrf = vrf_lookup_by_id(vrf_id);
	if (!vrf)
		return NULL;
	return (vrf->info) ? (struct bgp *)vrf->info : NULL;
}

/* Sets the BGP instance where EVPN is enabled */
/* 设置“EVPN 实例”指针 bm->bgp_evpn，并正确转移引用计数：
 * 先对旧实例 bgp_unlock，指向新实例后对其 bgp_lock。 */
void bgp_set_evpn(struct bgp *bgp)
{
	if (bm->bgp_evpn == bgp)
		return;

	/* First, release the reference count we hold on the instance */
	if (bm->bgp_evpn)
		bgp_unlock(bm->bgp_evpn);

	bm->bgp_evpn = bgp;

	/* Increase the reference count on this new VRF */
	if (bm->bgp_evpn)
		bgp_lock(bm->bgp_evpn);
}

/* Returns the BGP instance where EVPN is enabled, if any */
/* 返回 EVPN 实例指针（若无则 NULL）。 */
struct bgp *bgp_get_evpn(void)
{
	return bm->bgp_evpn;
}

/* handle socket creation or deletion, if necessary
 * this is called for all new BGP instances
 */
/*
 * 按实例类型处理“监听套接字”的创建/关闭。
 *
 * @bgp        : 目标实例。
 * @vrf        : 关联的 VRF（VRF 实例需要）。
 * @old_vrf_id : 变更前 vrf_id（用于判断是否需要重建 socket）。
 * @create     : true=创建/保持；false=关闭。
 * @return     : 0 或 BGP_ERR_INVALID_VALUE。
 *
 * 分两条路径：
 *  - VRF 实例：create=false 时关闭 vrf socket 并返回；create=true 时要求 vrf 非空、
 *    实例带 BGP_FLAG_VRF_MAY_LISTEN、vrf_id 已确定（非 UNKNOWN）；若 vrf_id 变了
 *    先关旧 socket 再按 bm->addresses 逐地址（或通配）监听。
 *  - 非 VRF（default/view）：委托给 bgp_check_main_socket 统一处理主监听套接字。
 */
int bgp_handle_socket(struct bgp *bgp, struct vrf *vrf, vrf_id_t old_vrf_id,
		      bool create)
{
	struct listnode *node;
	char *address;

	/* Create BGP server socket, if listen mode not disabled */
	if (!bgp || bgp_option_check(BGP_OPT_NO_LISTEN))
		return 0;
	if (bgp->inst_type == BGP_INSTANCE_TYPE_VRF) {
		/*
		 * suppress vrf socket
		 */
		if (!create) {
			bgp_close_vrf_socket(bgp);
			return 0;
		}
		if (vrf == NULL)
			return BGP_ERR_INVALID_VALUE;
		if (!CHECK_FLAG(bgp->flags, BGP_FLAG_VRF_MAY_LISTEN))
			return 0;
		/* do nothing
		 * if vrf_id did not change
		 */
		if (vrf->vrf_id == old_vrf_id)
			return 0;
		if (old_vrf_id != VRF_UNKNOWN) {
			/* look for old socket. close it. */
			bgp_close_vrf_socket(bgp);
		}
		/* if backend is not yet identified ( VRF_UNKNOWN) then
		 *   creation will be done later
		 */
		if (vrf->vrf_id == VRF_UNKNOWN)
			return 0;
		if (list_isempty(bm->addresses)) {
			if (bgp_socket(bgp, bm->port, NULL) < 0)
				return BGP_ERR_INVALID_VALUE;
		} else {
			for (ALL_LIST_ELEMENTS_RO(bm->addresses, node, address))
				if (bgp_socket(bgp, bm->port, address) < 0)
					return BGP_ERR_INVALID_VALUE;
		}
		return 0;
	} else
		return bgp_check_main_socket(create, bgp);
}

/*
 * 按 (AS, name, inst_type) 查找既有实例，并处理“同一 name 但 AS 变化”的边界情况。
 * 这是 bgp_get 的前半段，也是把“自动创建（hidden/VRF_AUTO）”实例转正的关键。
 *
 * @bgp_val    : 输出，命中的实例（若有）。
 * @as         : 输入期望 AS；在不允许改动的场景会被回填为实际 AS。
 * @force_config: true 表示这是一次显式配置（不受 BGP_VRF_AUTO 过滤影响）。
 * @return     : BGP_SUCCESS（未命中，调用者需新建）、BGP_INSTANCE_EXISTS（命中可复用）、
 *               BGP_ERR_INSTANCE_MISMATCH（AS 或类型不符）。
 *
 * 逻辑：
 *  - 按 name（或 default）查找；命中后：
 *    · 若实例是 hidden（内部）且指定了 AS → 标记 hidden，允许“实化”它；
 *    · AS 不同：对 hidden 或 VRF_AUTO 实例允许改 AS（hidden 走 bgp_create 复用实化，
 *      非 hidden 直接改 as 并视情况清 VRF_AUTO），把实例所有 peer 的 local_as 更新
 *      并返回 EXISTS；
 *    · 否则 AS 不匹配 → 回填实际 AS 返回 MISMATCH；
 *    · inst_type 不匹配 → MISMATCH；
 *    · 命中 hidden 时用 bgp_create 实化它。
 *  - 未命中：*bgp_val=NULL，返回 BGP_SUCCESS。
 */
int bgp_lookup_by_as_name_type(struct bgp **bgp_val, as_t *as, const char *as_pretty,
			       enum asnotation_mode asnotation, const char *name,
			       enum bgp_instance_type inst_type, bool force_config)
{
	struct bgp *bgp;
	struct peer *peer = NULL;
	struct listnode *node, *nnode;
	bool hidden = false;

	/* Multiple instance check. */
	if (name)
		bgp = bgp_lookup_by_name_filter(name, !force_config);
	else
		bgp = bgp_get_default();

	if (bgp) {
		if (IS_BGP_INSTANCE_HIDDEN(bgp) && *as != AS_UNSPECIFIED)
			hidden = true;
		/* Handle AS number change */
		if (bgp->as != *as) {
			if (hidden || CHECK_FLAG(bgp->vrf_flags, BGP_VRF_AUTO)) {
				if (hidden) {
					bgp_create(as, name, inst_type,
						   as_pretty, asnotation, bgp,
						   hidden);
					UNSET_FLAG(bgp->flags,
						   BGP_FLAG_INSTANCE_HIDDEN);
					UNSET_FLAG(bgp->flags, BGP_FLAG_DELETE_IN_PROGRESS);
				} else {
					bgp->as = *as;
					if (force_config == false)
						UNSET_FLAG(bgp->vrf_flags, BGP_VRF_AUTO);
				}

				/* Set all peer's local AS with this ASN */
				for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode,
						       peer))
					peer->local_as = *as;
				*bgp_val = bgp;
				return BGP_INSTANCE_EXISTS;
			}

			*as = bgp->as;
			*bgp_val = bgp;
			return BGP_ERR_INSTANCE_MISMATCH;
		}
		if (bgp->inst_type != inst_type)
			return BGP_ERR_INSTANCE_MISMATCH;
		if (hidden)
			bgp_create(as, name, inst_type, as_pretty, asnotation,
				   bgp, hidden);
		*bgp_val = bgp;
		return BGP_INSTANCE_EXISTS;
	}
	*bgp_val = NULL;

	return BGP_SUCCESS;
}

/* Called from VTY commands. */
/*
 * 获取或创建 BGP 实例（`router bgp` 命令的入口）。
 * @return : BGP_CREATED（新建）、BGP_INSTANCE_EXISTS（已存在可复用）或 BGP_ERR_*。
 *
 * 流程：先 bgp_lookup_by_as_name_type 尝试命中；命中即返回。未命中则：
 *  - bgp_create 新建；
 *  - view 实例强制 vrf_id=VRF_DEFAULT（view 不隶属具体 VRF，但 vrf socket 代码需要
 *    一个可用的默认值）；
 *  - 初始化 router-id、地址表（bgp_address_init）、tip hash、scan；
 *  - default/VRF 实例关联 lib 的 VRF 结构（bgp_vrf_lookup_by_instance_type + link）；
 *  - bgp_handle_socket 创建监听；加入 bm->bgp 链表；
 *  - 若该实例对 Zebra 可见，则向 Zebra 注册实例。
 */
int bgp_get(struct bgp **bgp_val, as_t *as, const char *name,
	    enum bgp_instance_type inst_type, const char *as_pretty,
	    enum asnotation_mode asnotation)
{
	struct bgp *bgp;
	struct vrf *vrf = NULL;
	int ret = 0;

	ret = bgp_lookup_by_as_name_type(bgp_val, as, as_pretty, asnotation, name, inst_type, false);
	if (ret || *bgp_val)
		return ret;

	bgp = bgp_create(as, name, inst_type, as_pretty, asnotation, NULL,
			 false);

	/*
	 * view instances will never work inside of a vrf
	 * as such they must always be in the VRF_DEFAULT
	 * Also we must set this to something useful because
	 * of the vrf socket code needing an actual useful
	 * default value to send to the underlying OS.
	 *
	 * This code is currently ignoring vrf based
	 * code using the -Z option( and that is probably
	 * best addressed elsewhere in the code )
	 */
	if (inst_type == BGP_INSTANCE_TYPE_VIEW)
		bgp->vrf_id = VRF_DEFAULT;

	bgp_router_id_set(bgp, &bgp->router_id_zebra, true);
	bgp_address_init(bgp);
	bgp_tip_hash_init(bgp);
	bgp_scan_init(bgp);
	*bgp_val = bgp;

	bgp->t_rmap_def_originate_eval = NULL;

	/* If Default instance or VRF, link to the VRF structure, if present. */
	if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT
	    || bgp->inst_type == BGP_INSTANCE_TYPE_VRF) {
		vrf = bgp_vrf_lookup_by_instance_type(bgp);
		if (vrf)
			bgp_vrf_link(bgp, vrf);
	}
	/* BGP server socket already processed if BGP instance
	 * already part of the list
	 */
	bgp_handle_socket(bgp, vrf, VRF_UNKNOWN, true);
	listnode_add(bm->bgp, bgp);

	if (IS_BGP_INST_KNOWN_TO_ZEBRA(bgp)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("%s: Registering BGP instance %s to zebra",
				   __func__, bgp->name_pretty);
		bgp_zebra_instance_register(bgp);
	}

	return BGP_CREATED;
}

/*
 * 在 zclient 的重分布位图/实例集合中置位或清位（set=true/false）：
 *  - instance 非 0：走 redist_add_instance/redist_del_instance（按实例号区分）；
 *  - instance 为 0：走 vrf_bitmap_set/unset（按 vrf_id 区分）。
 * 目的是告诉 Zebra“本进程要接收该 (afi,type[,instance]) 的路由”。
 */
static void bgp_zclient_set_redist(afi_t afi, int type, unsigned short instance,
				   vrf_id_t vrf_id, bool set)
{
	if (instance) {
		if (set)
			redist_add_instance(&bgp_zclient->mi_redist[afi][type],
					    instance);
		else
			redist_del_instance(&bgp_zclient->mi_redist[afi][type],
					    instance);
	} else {
		if (set)
			vrf_bitmap_set(&bgp_zclient->redist[afi][type], vrf_id);
		else
			vrf_bitmap_unset(&bgp_zclient->redist[afi][type], vrf_id);
	}
}

/* 对实例已配置的所有 (afi,route-type,instance) 重分布项，统一在 zclient 位图/集合
 * 中置位或清位（实例上下线时调用，向 Zebra 声明本 VRF 是否参与各类型重分布）。 */
static void bgp_set_redist_vrf_bitmaps(struct bgp *bgp, bool set)
{
	afi_t afi;
	int i;
	struct list *red_list;
	struct listnode *node;
	struct bgp_redist *red;

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++) {

			red_list = bgp->redist[afi][i];
			if (!red_list)
				continue;

			for (ALL_LIST_ELEMENTS_RO(red_list, node, red))
				bgp_zclient_set_redist(afi, i, red->instance,
						       bgp->vrf_id, set);
		}
	}
}

/*
 * Make BGP instance "up". Applies only to VRFs (non-default) and
 * implies the VRF has been learnt from Zebra.
 */
/*
 * 把 BGP 实例“上线”（仅对非 default 的 VRF 实例有意义，表示该 VRF 已从 Zebra 学到）。
 * 动作：通知 BMP（hook bgp_instance_state）→ 置重分布位图 → 向 Zebra 注册实例 →
 * 对未被抑制的 peer 注入 BGP_Start（踢活已配置的邻居）→ bgp_static_add 处理已配置的
 * `network` 静态路由。
 */
void bgp_instance_up(struct bgp *bgp)
{
	struct peer *peer;
	struct listnode *node, *next;

	/* notify BMP of instance state changed */
	hook_call(bgp_instance_state, bgp);

	bgp_set_redist_vrf_bitmaps(bgp, true);

	/* Register with zebra. */
	bgp_zebra_instance_register(bgp);

	/* Kick off any peers that may have been configured. */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, next, peer)) {
		if (!BGP_PEER_START_SUPPRESSED(peer))
			BGP_EVENT_ADD(peer->connection, BGP_Start);
	}

	/* Process any networks that have been configured. */
	bgp_static_add(bgp);
}

/*
 * Make BGP instance "down". Applies only to VRFs (non-default) and
 * implies the VRF has been deleted by Zebra.
 */
/*
 * 把 BGP 实例“下线”（仅对非 default 的 VRF 实例有意义，表示 VRF 已被 Zebra 删除）。
 * 动作：清理 EVPN 实例状态 → 通知 BMP → 取消 default-originate 求值定时器 →
 * 把各 peer 拉下（能发通知的发 ADMIN_SHUTDOWN，否则直接复位）→ 清除 network 与
 * 重分布路由 → 清理已注册的 nexthop → 向 Zebra 反注册 → 清重分布位图。
 */
void bgp_instance_down(struct bgp *bgp)
{
	struct peer *peer;
	struct listnode *node;
	struct listnode *next;

	/* Cleanup evpn instance state */
	bgp_evpn_instance_down(bgp);

	/* notify BMP of instance state changed */
	hook_call(bgp_instance_state, bgp);

	/* Stop timers. */
	if (bgp->t_rmap_def_originate_eval)
		event_cancel(&bgp->t_rmap_def_originate_eval);

	/* Bring down peers, so corresponding routes are purged. */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, next, peer)) {
		if (BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status))
			bgp_notify_send(peer->connection, BGP_NOTIFY_CEASE,
					BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
		else
			bgp_session_reset(peer);
	}

	/* Purge network and redistributed routes. */
	bgp_purge_static_redist_routes(bgp);

	/* Cleanup registered nexthops (flags) */
	bgp_cleanup_nexthops(bgp);

	bgp_zebra_instance_deregister(bgp);

	bgp_set_redist_vrf_bitmaps(bgp, false);
}

/* Delete BGP instance. */
/*
 * 删除一个 BGP 实例（`no router bgp`、VRF 删除、进程退出等的核心）。
 *
 * 复杂点在于实例牵涉大量“悬挂引用”：Zebra FIFO 中待处理的 dest、EVPN 待安装的
 * L2 VNI、VPN 泄漏、SRv6 SID/标签、批量清理批次、各定时器、peer/peer-group、
 * GR 信息、聚合、路由表、Zebra 注册、VRF 关联、EVPN 指针等。本函数按“先解除
 * 外部引用、再逐个内部资源”的顺序拆解它们。特别注意：
 *  - 依次 drain Zebra 的 announce FIFO 与 EVPN L2 VNI FIFO 中属于本实例的项，
 *    并做 bgp_path_info_unlock / bgp_dest_unlock_node，防止泄漏与悬垂；
 *  - 若有挂起的 clearing_end 事件，会额外持有一个 bgp 引用，需在此 bgp_unlock 一次；
 *  - peer/peer-group 逐个 peer_delete；对 GR-capable peer 跳过 unconfig 通知；
 *  - 隐藏实例（IS_BGP_INSTANCE_HIDDEN）不加入/移除 bm->bgp 可见链表，也推迟某些释放；
 *  - 最后 listnode_delete(bm->bgp) 摘除可见性，处理 socket、VRF 解绑、EVPN 指针、
 *    释放进程队列并 bgp_unlock(初始引用)，真正内存释放在引用归零后由 bgp_free 完成。
 * @return : 0。
 */
int bgp_delete(struct bgp *bgp)
{
	struct peer *peer;
	struct peer_group *group;
	struct listnode *node, *next;
	struct vrf *vrf;
	afi_t afi;
	safi_t safi;
	int i;
	uint32_t vni_count;
	struct bgpevpn *vpn = NULL;
	struct bgp_dest *dest = NULL;
	struct bgp_dest *dest_next = NULL;
	struct bgp_table *dest_table = NULL;
	struct graceful_restart_info *gr_info;
	struct bgp *bgp_default = bgp_get_default();
	struct bgp_clearing_info *cinfo;
	struct peer_connection *connection;
	uint32_t b_ann_cnt = 0, b_l2_cnt = 0;
	uint32_t a_ann_cnt = 0, a_l2_cnt = 0;

	assert(bgp);

	/*
	 * Iterate the pending dest list and remove all the dest pertaining to
	 * the bgp under delete.
	 */
	b_ann_cnt = zebra_announce_count(&bm->zebra_announce_head);
	for (dest = zebra_announce_first(&bm->zebra_announce_head); dest;
	     dest = dest_next) {
		dest_next = zebra_announce_next(&bm->zebra_announce_head, dest);
		dest_table = bgp_dest_table(dest);
		if (dest_table->bgp == bgp) {
			zebra_announce_del(&bm->zebra_announce_head, dest);
			bgp_path_info_unlock(dest->za_bgp_pi);
			bgp_dest_unlock_node(dest);
		}
	}

	/*
	 * Pop all VPNs yet to be processed for remote routes install if the
	 * bgp-evpn instance is getting deleted
	 */
	if (bgp == bgp_get_evpn()) {
		b_l2_cnt = zebra_l2_vni_count(&bm->zebra_l2_vni_head);
		vni_count = b_l2_cnt;
		while (vni_count) {
			vpn = zebra_l2_vni_pop(&bm->zebra_l2_vni_head);
			UNSET_FLAG(vpn->flags, VNI_FLAG_ADD);
			vni_count--;
		}
	}

	if (BGP_DEBUG(zebra, ZEBRA)) {
		a_ann_cnt = zebra_announce_count(&bm->zebra_announce_head);
		a_l2_cnt = zebra_l2_vni_count(&bm->zebra_l2_vni_head);
		zlog_debug("FIFO Cleanup Count during BGP %s deletion :: Zebra Announce - before %u after %u :: BGP L2_VNI - before %u after %u",
			   bgp->name_pretty, b_ann_cnt, a_ann_cnt, b_l2_cnt, a_l2_cnt);
	}

	/* Cleanup for peer connection batching */
	while ((cinfo = bgp_clearing_info_first(&bgp->clearing_list)) != NULL)
		bgp_clearing_batch_completed(cinfo);

	bgp_soft_reconfig_table_task_cancel(bgp, NULL, NULL);

	/* make sure we withdraw any exported routes */
	vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN, AFI_IP, bgp_default, bgp);
	vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN, AFI_IP6, bgp_default, bgp);

	bgp_vpn_leak_unimport(bgp);

	/*
	 * Release SRv6 SIDs, like it's done in `vpn_leak_postchange()`
	 * and bgp_sid_vpn_export_cmd/af_sid_vpn_export_cmd/af_sid_export
	 * commands.
	 */
	bgp->tovpn_sid_index = 0;
	UNSET_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_AUTO);
	UNSET_FLAG(bgp->vrf_flags, BGP_VRF_TOVPN_SID_EXPLICIT);
	delete_vrf_tovpn_sid_per_vrf(bgp_default, bgp);
	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		bgp->vpn_policy[afi].tovpn_sid_index = 0;
		UNSET_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_SID_AUTO);
		delete_vrf_tovpn_sid_per_af(bgp_default, bgp, afi);

		vpn_leak_zebra_vrf_sid_withdraw(bgp, afi);

		/* SRv6 unicast */
		bgp_srv6_unicast_delete(bgp_default, afi);
	}

	/* release auto vpn labels */
	bgp_vpn_release_label(bgp, AFI_IP, true);
	bgp_vpn_release_label(bgp, AFI_IP6, true);

	/* release manual vpn labels */
	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		if (!CHECK_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_LABEL_MANUAL_REG))
			continue;
		bgp_zebra_release_label_range(bgp->vpn_policy[afi].tovpn_label,
					      bgp->vpn_policy[afi].tovpn_label);
		UNSET_FLAG(bgp->vpn_policy[afi].flags, BGP_VPN_POLICY_TOVPN_LABEL_MANUAL_REG);
	}

	hook_call(bgp_inst_delete, bgp);

	FOREACH_AFI_SAFI (afi, safi)
		event_cancel(&bgp->t_revalidate[afi][safi]);

	event_cancel(&bgp->t_condition_check);
	event_cancel(&bgp->t_startup);
	event_cancel(&bgp->t_maxmed_onstartup);
	event_cancel(&bgp->t_update_delay);
	event_cancel(&bgp->t_establish_wait);

	/* If the clearing event is scheduled, there's an extra ref to
	 * this 'bgp' - ensure we unlock.
	 */
	if (event_is_scheduled(bgp->clearing_end)) {
		assert(bgp->lock > 1);
		bgp_unlock(bgp);
	}

	event_cancel(&bgp->clearing_end);

	/* Set flag indicating bgp instance delete in progress */
	SET_FLAG(bgp->flags, BGP_FLAG_DELETE_IN_PROGRESS);

	/* Delete the graceful restart info */
	FOREACH_AFI_SAFI (afi, safi) {
		struct event *t;

		gr_info = &bgp->gr_info[afi][safi];
		if (!gr_info)
			continue;
		t = gr_info->t_select_deferral;
		if (t) {
			void *info = EVENT_ARG(t);

			XFREE(MTYPE_TMP, info);
		}
		event_cancel(&gr_info->t_select_deferral);

		t = gr_info->t_route_select;
		if (t) {
			void *info = EVENT_ARG(t);

			XFREE(MTYPE_TMP, info);
		}
		event_cancel(&gr_info->t_route_select);
	}

	/* Delete route flap dampening configuration */
	FOREACH_AFI_SAFI (afi, safi) {
		bgp_damp_disable(bgp, afi, safi);
	}

	if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT &&
	    (bgp_table_top(bgp->rib[AFI_IP][SAFI_MPLS_VPN]) ||
	     bgp_table_top(bgp->rib[AFI_IP6][SAFI_MPLS_VPN]))) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"Marking the deleting default bgp instance as hidden");
		SET_FLAG(bgp->flags, BGP_FLAG_INSTANCE_HIDDEN);
	}

	if (BGP_DEBUG(zebra, ZEBRA)) {
		if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT)
			zlog_debug("Deleting Default VRF");
		else
			zlog_debug("Deleting %s %s",
				   (bgp->inst_type == BGP_INSTANCE_TYPE_VRF)
					   ? "VRF"
					   : "VIEW",
				   bgp->name);
	}

	/* unmap from RT list */
	if (!IS_BGP_INSTANCE_HIDDEN(bgp) || bm->terminating)
		bgp_evpn_vrf_delete(bgp);

	/* unmap bgp vrf label */
	vpn_leak_zebra_vrf_label_withdraw(bgp, AFI_IP);
	vpn_leak_zebra_vrf_label_withdraw(bgp, AFI_IP6);

	/* Stop timers. */
	if (bgp->t_rmap_def_originate_eval)
		event_cancel(&bgp->t_rmap_def_originate_eval);

	/* Inform peers we're going down. */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, next, peer))
		peer_notify_shutdown(peer);

	/* Delete static routes (networks). */
	bgp_static_delete(bgp);

	/* Unset redistribution. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
			if (i != ZEBRA_ROUTE_BGP)
				bgp_redistribute_unset(bgp, afi, i, 0);

	/* Clear list of peers with connection errors - each
	 * peer will need to check again, in case the io pthread is racing
	 * with us, but this batch cleanup should make the per-peer check
	 * cheaper.
	 */
	frr_with_mutex (&bgp->peer_errs_mtx) {
		do {
			connection = bgp_peer_conn_errlist_pop(
				&bgp->peer_conn_errlist);
		} while (connection != NULL);
	}

	/* Free peers and peer-groups. */
	for (ALL_LIST_ELEMENTS(bgp->group, node, next, group))
		peer_group_delete(group);

	while (listcount(bgp->peer)) {
		peer = listnode_head(bgp->peer);
		if (peer->ifp || CHECK_FLAG(peer->flags, PEER_FLAG_CAPABILITY_ENHE))
			bgp_zebra_terminate_radv(peer->bgp, peer);

		if (BGP_PEER_GRACEFUL_RESTART_CAPABLE(peer)) {
			if (bgp_debug_neighbor_events(peer))
				zlog_debug("%pBP configured Graceful-Restart, skipping unconfig notification",
					   peer);
			peer_delete(peer);
		} else {
			peer_notify_unconfig(peer->connection);
			peer_delete(peer);
		}
	}

	if (bgp->peer_self && (!IS_BGP_INSTANCE_HIDDEN(bgp) || bm->terminating)) {
		peer_delete(bgp->peer_self);
		bgp->peer_self = NULL;
	}

	update_bgp_group_free(bgp);

	/* Cancel peer connection errors event */
	event_cancel(&bgp->t_conn_errors);

	/* Cleanup for peer connection batching */
	while ((cinfo = bgp_clearing_info_pop(&bgp->clearing_list)) != NULL)
		bgp_clearing_batch_completed(cinfo);

/* TODO - Other memory may need to be freed - e.g., NHT */

#ifdef ENABLE_BGP_VNC
	if (!IS_BGP_INSTANCE_HIDDEN(bgp) || bm->terminating)
		rfapi_delete(bgp);
#endif

	/* Free memory allocated with aggregate address configuration. */
	FOREACH_AFI_SAFI (afi, safi) {
		struct bgp_aggregate *aggregate = NULL;

		for (dest = bgp_table_top(bgp->aggregate[afi][safi]);
		     dest; dest = bgp_route_next(dest)) {
			aggregate = bgp_dest_get_bgp_aggregate_info(dest);
			if (aggregate == NULL)
				continue;

			bgp_dest_set_bgp_aggregate_info(dest, NULL);
			bgp_free_aggregate_info(aggregate);
		}
	}

	bgp_cleanup_routes(bgp);

	for (afi = 0; afi < AFI_MAX; ++afi) {
		if (!bgp->vpn_policy[afi].import_redirect_rtlist)
			continue;
		ecommunity_free(
				&bgp->vpn_policy[afi]
				.import_redirect_rtlist);
		bgp->vpn_policy[afi].import_redirect_rtlist = NULL;
	}

	/* Free any memory allocated to holding routemap references */
	for (afi = 0; afi < AFI_MAX; ++afi) {
		for (enum vpn_policy_direction dir = 0;
		     dir < BGP_VPN_POLICY_DIR_MAX; ++dir) {
			if (bgp->vpn_policy[afi].rmap_name[dir])
				XFREE(MTYPE_ROUTE_MAP_NAME,
				      bgp->vpn_policy[afi].rmap_name[dir]);
			bgp->vpn_policy[afi].rmap[dir] = NULL;
		}
	}

	/* Deregister from Zebra, if needed */
	if (IS_BGP_INST_KNOWN_TO_ZEBRA(bgp) && !IS_BGP_INSTANCE_HIDDEN(bgp)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"%s: deregistering this bgp %s instance from zebra",
				__func__, bgp->name);
		bgp_zebra_instance_deregister(bgp);
	}

	if (!IS_BGP_INSTANCE_HIDDEN(bgp) || bm->terminating) {
		/* Remove visibility via the master list -
		 * there may however still be routes to be processed
		 * still referencing the struct bgp.
		 */
		listnode_delete(bm->bgp, bgp);
		/* Free interfaces in this instance. */
		bgp_if_finish(bgp);
	}

	vrf = bgp_vrf_lookup_by_instance_type(bgp);
	bgp_handle_socket(bgp, vrf, VRF_UNKNOWN, false);
	if (vrf && (!IS_BGP_INSTANCE_HIDDEN(bgp) || bm->terminating))
		bgp_vrf_unlink(bgp, vrf);

	/* Update EVPN VRF pointer */
	if (bm->bgp_evpn == bgp) {
		if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT)
			bgp_set_evpn(NULL);
		else
			bgp_set_evpn(bgp_get_default());
	}

	if (!IS_BGP_INSTANCE_HIDDEN(bgp) || bm->terminating) {
		if (bgp->process_queue)
			work_queue_free_and_null(&bgp->process_queue);
		bgp_unlock(bgp); /* initial reference */
	} else {
		for (afi = AFI_IP; afi < AFI_MAX; afi++) {
			enum vpn_policy_direction dir;

			if (bgp->vpn_policy[afi].import_vrf)
				list_delete(&bgp->vpn_policy[afi].import_vrf);

			dir = BGP_VPN_POLICY_DIR_FROMVPN;
			if (bgp->vpn_policy[afi].rtlist[dir])
				ecommunity_free(
					&bgp->vpn_policy[afi].rtlist[dir]);
		}
	}

	/* Clean BGP address family parameters */
	bgp_mh_info->enable_ead_evi_rx = BGP_EVPN_MH_EAD_EVI_RX_DEF;
	bgp_evpn_switch_ead_evi_rx();
	bgp_mh_info->enable_ead_evi_tx = BGP_EVPN_MH_EAD_EVI_TX_DEF;
	/*
	 * Don't call bgp_evpn_switch_ead_evi_tx() here - routes are cleaned
	 * up during BGP instance teardown
	 */
	bgp_mh_info->evi_per_es_frag = BGP_EVPN_MAX_EVI_PER_ES_FRAG;

	bgp_address_family_distance_delete();

	return 0;
}

/*
 * 真正释放 struct bgp（在引用计数归零时由 bgp_unlock 调用；是 bgp_delete 的收尾）。
 * 假设 peer/group 已被 bgp_delete 清空。
 * 动作：QOBJ_UNREG → 删除 group/peer 链表与 connectionhash → 对每族释放两级路由表、
 * static_routes/aggregate/rib 表，释放 table_map 与 adv_cmd_rmap 的 route-map 名（并
 * route_map_counter_decrement）→ 释放 scan/address/tip hash → 归还 RD id →
 * EVPN/PBR cleanup → 释放 VPN 策略的 import/export VRF 链表与 rtlist、
 * tovpn_rd_pretty → SRv6 cleanup → 取消联邦配置 → 销毁错误队列与互斥锁 →
 * 释放联邦成员名、as_pretty/name/name_pretty/snmp_stats/confed_peers →
 * 释放 meta queue → 最后 XFREE 整块 bgp。
 */
void bgp_free(struct bgp *bgp)
{
	afi_t afi;
	safi_t safi;
	struct bgp_table *table;
	struct bgp_dest *dest;
	struct bgp_rmap *rmap;

	QOBJ_UNREG(bgp);

	list_delete(&bgp->group);
	list_delete(&bgp->peer);

	if (bgp->connectionhash) {
		hash_free(bgp->connectionhash);
		bgp->connectionhash = NULL;
	}

	FOREACH_AFI_SAFI (afi, safi) {
		/* Special handling for 2-level routing tables. */
		if (safi == SAFI_MPLS_VPN || safi == SAFI_ENCAP
		    || safi == SAFI_EVPN) {
			for (dest = bgp_table_top(bgp->rib[afi][safi]); dest;
			     dest = bgp_route_next(dest)) {
				table = bgp_dest_get_bgp_table_info(dest);
				bgp_table_finish(&table);
			}
		}
		if (bgp->static_routes[afi][safi])
			bgp_table_finish(&bgp->static_routes[afi][safi]);
		if (bgp->aggregate[afi][safi])
			bgp_table_finish(&bgp->aggregate[afi][safi]);
		if (bgp->rib[afi][safi])
			bgp_table_finish(&bgp->rib[afi][safi]);
		rmap = &bgp->table_map[afi][safi];
		XFREE(MTYPE_ROUTE_MAP_NAME, rmap->name);
		/* Free advertise command route-map */
		rmap = &bgp->adv_cmd_rmap[afi][safi];
		if (rmap->name) {
			XFREE(MTYPE_ROUTE_MAP_NAME, rmap->name);
			route_map_counter_decrement(rmap->map);
		}
	}

	bgp_scan_finish(bgp);
	bgp_address_destroy(bgp);
	bgp_tip_hash_destroy(bgp);

	/* release the auto RD id */
	bf_release_index(bm->rd_idspace, bgp->vrf_rd_id);

	bgp_evpn_cleanup(bgp);
	bgp_pbr_cleanup(bgp);

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		enum vpn_policy_direction dir;

		if (bgp->vpn_policy[afi].import_vrf)
			list_delete(&bgp->vpn_policy[afi].import_vrf);
		if (bgp->vpn_policy[afi].export_vrf)
			list_delete(&bgp->vpn_policy[afi].export_vrf);

		dir = BGP_VPN_POLICY_DIR_FROMVPN;
		if (bgp->vpn_policy[afi].rtlist[dir])
			ecommunity_free(&bgp->vpn_policy[afi].rtlist[dir]);
		dir = BGP_VPN_POLICY_DIR_TOVPN;
		if (bgp->vpn_policy[afi].rtlist[dir])
			ecommunity_free(&bgp->vpn_policy[afi].rtlist[dir]);
		if (bgp->vpn_policy[afi].tovpn_rd_pretty)
			XFREE(MTYPE_BGP_NAME,
			      bgp->vpn_policy[afi].tovpn_rd_pretty);
	}
	bgp_srv6_cleanup(bgp);
	bgp_confederation_id_unset(bgp);

	bgp_peer_conn_errlist_init(&bgp->peer_conn_errlist);
	pthread_mutex_destroy(&bgp->peer_errs_mtx);

	for (int i = 0; i < bgp->confed_peers_cnt; i++)
		XFREE(MTYPE_BGP_NAME, bgp->confed_peers[i].as_pretty);

	XFREE(MTYPE_BGP_NAME, bgp->as_pretty);
	XFREE(MTYPE_BGP_NAME, bgp->name);
	XFREE(MTYPE_BGP_NAME, bgp->name_pretty);
	XFREE(MTYPE_BGP_NAME, bgp->snmp_stats);
	XFREE(MTYPE_BGP_CONFED_LIST, bgp->confed_peers);

	bgp_meta_queue_free(bgp->mq);
	bgp->mq = NULL;

	XFREE(MTYPE_BGP, bgp);
}

/* ========== 6. peer 查找：按接口名 / 主机名 / 地址；动态邻居 ========== */

/*
 * 按接口名（conf_if）查找“已配置的”peer。
 * @bgp 为空时，遍历所有实例查找。仅匹配 peer_is_config_node（排除动态/分身）。
 */
struct peer *peer_lookup_by_conf_if(struct bgp *bgp, const char *conf_if)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if (!conf_if)
		return NULL;

	if (bgp != NULL) {
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer))
			if (peer->conf_if && !strcmp(peer->conf_if, conf_if) &&
			    peer_is_config_node(peer))
				return peer;
	} else if (bm->bgp != NULL) {
		struct listnode *bgpnode, *nbgpnode;

		for (ALL_LIST_ELEMENTS(bm->bgp, bgpnode, nbgpnode, bgp))
			for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer))
				if (peer->conf_if && !strcmp(peer->conf_if, conf_if) &&
				    peer_is_config_node(peer))
					return peer;
	}
	return NULL;
}

/* 按主机名（FQDN）查找“已配置的”peer；bgp 为空时遍历所有实例。 */
struct peer *peer_lookup_by_hostname(struct bgp *bgp, const char *hostname)
{
	struct peer *peer;
	struct listnode *node, *nnode;

	if (!hostname)
		return NULL;

	if (bgp != NULL) {
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer))
			if (peer->hostname && !strcmp(peer->hostname, hostname) &&
			    peer_is_config_node(peer))
				return peer;
	} else if (bm->bgp != NULL) {
		struct listnode *bgpnode, *nbgpnode;

		for (ALL_LIST_ELEMENTS(bm->bgp, bgpnode, nbgpnode, bgp))
			for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer))
				if (peer->hostname && !strcmp(peer->hostname, hostname) &&
				    peer_is_config_node(peer))
					return peer;
	}
	return NULL;
}

/*
 * 按对端地址查找“已配置的”peer（利用 connectionhash 做 O(1) 查找）。
 *
 * 技巧：栈上构造一个临时 peer+connection，把其 su 设为待查地址、并置
 * tmp_peer.flags 的 PEER_FLAG_CONFIG_NODE——因为 connection_hash_same 会比较
 * “是否配置节点”，这样就能精确命中配置节点、而跳过同地址的 doppelganger/动态邻居。
 * @bgp 为空时遍历所有实例的 connectionhash。
 * 返回命中的 peer（connection->peer）；未命中返回 NULL。
 */
struct peer *peer_lookup(struct bgp *bgp, union sockunion *su)
{
	struct peer tmp_peer;
	struct peer_connection connection;
	struct peer_connection *c = NULL;

	memset(&connection, 0, sizeof(struct peer_connection));
	memset(&tmp_peer, 0, sizeof(struct peer));
	tmp_peer.connection = &connection;
	connection.peer = &tmp_peer;

	/*
	 * We do not want to find the doppelganger peer so search for the peer
	 * in
	 * the hash that has PEER_FLAG_CONFIG_NODE
	 */
	SET_FLAG(tmp_peer.flags, PEER_FLAG_CONFIG_NODE);

	connection.su = *su;

	if (bgp != NULL) {
		c = hash_lookup(bgp->connectionhash, &connection);
	} else if (bm->bgp != NULL) {
		struct listnode *bgpnode, *nbgpnode;

		for (ALL_LIST_ELEMENTS(bm->bgp, bgpnode, nbgpnode, bgp)) {
			c = hash_lookup(bgp->connectionhash, &connection);
			if (c)
				break;
		}
	}

	return c ? c->peer : NULL;
}

/*
 * 为“动态邻居”（匹配 listen range 的入向连接）创建并绑定一个 peer。
 * 这里不调用 peer_group_bind（后者会做多余的检查/复位，性能次优），而是直接：
 * peer_create（用组的 as/as_type，config_node=true）→ peer_lock 并加入 group->peer →
 * peer_group2peer_config_copy 继承整机配置 → 对组已激活的族补 peer_af 与 _af 继承、
 * 对组未激活的族停用 → 置 PEER_FLAG_DYNAMIC_NEIGHBOR（同时保留 CONFIG_NODE 语义）。
 * @return : 新 peer。
 */
struct peer *peer_create_bind_dynamic_neighbor(struct bgp *bgp,
					       union sockunion *su,
					       struct peer_group *group)
{
	struct peer *peer;
	afi_t afi;
	safi_t safi;

	/* Create peer first; we've already checked group config is valid. */
	peer = peer_create(su, NULL, bgp, bgp->as, group->conf->as, group->conf->as_type, group,
			   true, NULL, CONNECTION_INCOMING);
	if (!peer)
		return NULL;

	/* Link to group */
	peer = peer_lock(peer);
	listnode_add(group->peer, peer);

	peer_group2peer_config_copy(group, peer);

	/*
	 * Bind peer for all AFs configured for the group. We don't call
	 * peer_group_bind as that is sub-optimal and does some stuff we don't
	 * want.
	 */
	FOREACH_AFI_SAFI (afi, safi) {
		if (!group->conf->afc[afi][safi]) {
			if (peer->afc[afi][safi])
				peer_deactivate(peer, afi, safi);
			continue;
		}
		peer->afc[afi][safi] = 1;

		if (!peer_af_find(peer, afi, safi))
			peer_af_create(peer, afi, safi);

		peer_group2peer_config_copy_af(group, peer, afi, safi);
	}

	/* Mark as dynamic, but also as a "config node" for other things to
	 * work. */
	SET_FLAG(peer->flags, PEER_FLAG_DYNAMIC_NEIGHBOR);

	return peer;
}

/* 判断给定地址 prefix 是否落在本组的某个 listen range 内（按 afi 选对应链表，
 * 用 prefix_match 做包含判断）；命中返回该 range 前缀，否则 NULL。 */
struct prefix *
peer_group_lookup_dynamic_neighbor_range(struct peer_group *group,
					 struct prefix *prefix)
{
	struct listnode *node, *nnode;
	struct prefix *range;
	afi_t afi;

	afi = family2afi(prefix->family);

	if (group->listen_range[afi])
		for (ALL_LIST_ELEMENTS(group->listen_range[afi], node, nnode,
				       range))
			if (prefix_match(range, prefix))
				return range;

	return NULL;
}

/*
 * 在实例（或所有实例，当 bgp==NULL）的所有 peer-group 中，找出哪个 listen range
 * 覆盖给定地址 prefix。
 * @listen_range : 输出参数，命中的范围前缀（未命中为 NULL）。
 * @return : 命中的组（且必须同时命中 range 才返回非空）。
 */
struct peer_group *
peer_group_lookup_dynamic_neighbor(struct bgp *bgp, struct prefix *prefix,
				   struct prefix **listen_range)
{
	struct prefix *range = NULL;
	struct peer_group *group = NULL;
	struct listnode *node, *nnode;

	*listen_range = NULL;
	if (bgp != NULL) {
		for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group))
			if ((range = peer_group_lookup_dynamic_neighbor_range(
				     group, prefix)))
				break;
	} else if (bm->bgp != NULL) {
		struct listnode *bgpnode, *nbgpnode;

		for (ALL_LIST_ELEMENTS(bm->bgp, bgpnode, nbgpnode, bgp))
			for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group))
				if ((range = peer_group_lookup_dynamic_neighbor_range(
					     group, prefix)))
					goto found_range;
	}

found_range:
	*listen_range = range;
	return (group && range) ? group : NULL;
}

/*
 * 处理一个“入向连接是否应作为动态邻居接纳”。
 * 步骤：把源地址转 prefix → 找匹配的 (group, listen_range) → 校验组所在实例存在 →
 * 检查动态邻居计数是否已达上限（dynamic_neighbors_limit）→ 组未被 shutdown →
 * 组至少激活了一个地址族 → 才 peer_create_bind_dynamic_neighbor 创建并计数 +1；
 * 若正好到达上限则打印警告。
 * @return : 新建的 peer；任一条件不满足返回 NULL（连接将被拒绝）。
 */
struct peer *peer_lookup_dynamic_neighbor(struct bgp *bgp, union sockunion *su)
{
	struct peer_group *group;
	struct bgp *gbgp;
	struct peer *peer;
	struct prefix prefix;
	struct prefix *listen_range;
	int dncount;

	if (!sockunion2hostprefix(su, &prefix))
		return NULL;

	/* See if incoming connection matches a configured listen range. */
	group = peer_group_lookup_dynamic_neighbor(bgp, &prefix, &listen_range);

	if (!group)
		return NULL;


	gbgp = group->bgp;

	if (!gbgp)
		return NULL;

	if (bgp_debug_neighbor_events(NULL))
		zlog_debug(
			"Dynamic Neighbor %pFX matches group %s listen range %pFX",
			&prefix, group->name, listen_range);

	/* Are we within the listen limit? */
	dncount = gbgp->dynamic_neighbors_count;

	if (dncount >= gbgp->dynamic_neighbors_limit) {
		if (bgp_debug_neighbor_events(NULL))
			zlog_debug(
				"Dynamic Neighbor %pFX rejected - at limit %d",
				&prefix, gbgp->dynamic_neighbors_limit);
		return NULL;
	}

	/* Ensure group is not disabled. */
	if (CHECK_FLAG(group->conf->flags, PEER_FLAG_SHUTDOWN)) {
		if (bgp_debug_neighbor_events(NULL))
			zlog_debug(
				"Dynamic Neighbor %pFX rejected - group %s disabled",
				&prefix, group->name);
		return NULL;
	}

	/* Check that at least one AF is activated for the group. */
	if (!peer_group_af_configured(group)) {
		if (bgp_debug_neighbor_events(NULL))
			zlog_debug(
				"Dynamic Neighbor %pFX rejected - no AF activated for group %s",
				&prefix, group->name);
		return NULL;
	}

	/* Create dynamic peer and bind to associated group. */
	peer = peer_create_bind_dynamic_neighbor(gbgp, su, group);
	assert(peer);

	gbgp->dynamic_neighbors_count = ++dncount;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s Dynamic Neighbor added, group %s count %d",
			   peer->host, group->name, dncount);

	if (dncount == gbgp->dynamic_neighbors_limit) {
		zlog_warn("Dynamic Neighbor %s added as last connection. Peer-group %s reached maximum listen limit %d",
			  peer->host, group->name,
			  gbgp->dynamic_neighbors_limit);
	}
	return peer;
}

/* 动态邻居被回收时递减其所在实例的 dynamic_neighbors_count（并打印调试）。 */
static void peer_drop_dynamic_neighbor(struct peer *peer)
{
	int dncount = -1;
	if (peer->group->bgp) {
		dncount = peer->group->bgp->dynamic_neighbors_count;
		if (dncount)
			peer->group->bgp->dynamic_neighbors_count = --dncount;
	}
	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s dropped from group %s, count %d", peer->host,
			   peer->group->name, dncount);
}

/*
 * 把 peer 配置为“丢弃”的属性类型号拼成字符串写入 buf（用于 show/config 输出）。
 * 遍历 1..BGP_ATTR_MAX，收集 peer->discard_attrs[i] 为真的类型号，空格分隔。
 * @return : true 表示 buf 中有内容（至少配置了一个丢弃属性）；false 表示无。
 */
bool bgp_path_attribute_discard(struct peer *peer, char *buf, size_t size)
{
	if (!buf)
		return false;

	buf[0] = '\0';

	for (unsigned int i = 1; i <= BGP_ATTR_MAX; i++) {
		if (peer->discard_attrs[i])
			snprintf(buf + strlen(buf), size - strlen(buf), "%s%d",
				 (strlen(buf) > 0) ? " " : "", i);
	}

	if (strlen(buf) > 0)
		return true;

	return false;
}

/* 与 bgp_path_attribute_discard 类似，但收集的是“按撤销处理”的属性类型号
 * （peer->withdraw_attrs[]），用于输出配置。@return : buf 是否非空。 */
bool bgp_path_attribute_treat_as_withdraw(struct peer *peer, char *buf,
					  size_t size)
{
	if (!buf)
		return false;

	buf[0] = '\0';

	for (unsigned int i = 1; i <= BGP_ATTR_MAX; i++) {
		if (peer->withdraw_attrs[i])
			snprintf(buf + strlen(buf), size - strlen(buf), "%s%d",
				 (strlen(buf) > 0) ? " " : "", i);
	}

	if (strlen(buf) > 0)
		return true;

	return false;
}

/* If peer is configured at least one address family return 1. */
/* ========== 7. peer 活跃/协商状态判定 ========== */

/*
 * 判定 peer 是否“可运行”：地址未定（AF_UNSPEC）→ UNSPECIFIED；
 * 若启用 BFD strict 且 BFD 会话 admin-down/down → 对应状态；
 * 至少激活了一个地址族（含 EVPN）→ BGP_PEER_ACTIVE；否则 AF_UNCONFIGURED。
 * 该结果决定是否启动/保持 peer 的定时器与连接。
 */
enum bgp_peer_active peer_active(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;

	if (BGP_CONNECTION_SU_UNSPEC(connection))
		return BGP_PEER_CONNECTION_UNSPECIFIED;

	if (peer->bfd_config) {
		/* Only if `neighbor X bfd strict` is enabled */
		if (peergroup_flag_check(peer, PEER_FLAG_BFD_STRICT)) {
			if (bfd_session_is_admin_down(peer->bfd_config->session))
				return BGP_PEER_BFD_ADMIN_DOWN;
			else if (bfd_session_is_down(peer->bfd_config->session))
				return BGP_PEER_BFD_DOWN;
		}
	}

	if (peer->afc[AFI_IP][SAFI_UNICAST] || peer->afc[AFI_IP][SAFI_MULTICAST]
	    || peer->afc[AFI_IP][SAFI_LABELED_UNICAST]
	    || peer->afc[AFI_IP][SAFI_MPLS_VPN] || peer->afc[AFI_IP][SAFI_ENCAP]
	    || peer->afc[AFI_IP][SAFI_FLOWSPEC]
	    || peer->afc[AFI_IP6][SAFI_UNICAST]
	    || peer->afc[AFI_IP6][SAFI_MULTICAST]
	    || peer->afc[AFI_IP6][SAFI_LABELED_UNICAST]
	    || peer->afc[AFI_IP6][SAFI_MPLS_VPN]
	    || peer->afc[AFI_IP6][SAFI_ENCAP]
	    || peer->afc[AFI_IP6][SAFI_FLOWSPEC]
	    || peer->afc[AFI_L2VPN][SAFI_EVPN])
		return BGP_PEER_ACTIVE;

	return BGP_PEER_AF_UNCONFIGURED;
}

/* If peer is negotiated at least one address family return 1. */
/* 判断是否至少有一个地址族“已协商成功”（afc_nego），即有可用的多协议会话。 */
bool peer_active_nego(struct peer *peer)
{
	if (peer->afc_nego[AFI_IP][SAFI_UNICAST]
	    || peer->afc_nego[AFI_IP][SAFI_MULTICAST]
	    || peer->afc_nego[AFI_IP][SAFI_LABELED_UNICAST]
	    || peer->afc_nego[AFI_IP][SAFI_MPLS_VPN]
	    || peer->afc_nego[AFI_IP][SAFI_ENCAP]
	    || peer->afc_nego[AFI_IP][SAFI_FLOWSPEC]
	    || peer->afc_nego[AFI_IP6][SAFI_UNICAST]
	    || peer->afc_nego[AFI_IP6][SAFI_MULTICAST]
	    || peer->afc_nego[AFI_IP6][SAFI_LABELED_UNICAST]
	    || peer->afc_nego[AFI_IP6][SAFI_MPLS_VPN]
	    || peer->afc_nego[AFI_IP6][SAFI_ENCAP]
	    || peer->afc_nego[AFI_IP6][SAFI_FLOWSPEC]
	    || peer->afc_nego[AFI_L2VPN][SAFI_EVPN])
		return true;
	return false;
}

/* If peer received at least one address family MP, return true */
/* 判断是否至少收到对端一个地址族的 MP 能力通告（afc_recv）。 */
bool peer_afc_received(struct peer *peer)
{
	afi_t afi;
	safi_t safi;

	FOREACH_AFI_SAFI (afi, safi)
		if (peer->afc_recv[afi][safi])
			return true;

	return false;
}

/* If peer advertised at least one address family MP, return true */
/* 判断本端是否至少向对端通告了一个地址族的 MP 能力（afc_adv）。 */
bool peer_afc_advertised(struct peer *peer)
{
	afi_t afi;
	safi_t safi;

	FOREACH_AFI_SAFI (afi, safi)
		if (peer->afc_adv[afi][safi])
			return true;

	return false;
}

/*
 * 当某 peer 的“影响 VPN 选路”的配置变化时，重新触发该 peer 相关 VPN 前缀的
 * valid 标志计算与最佳路径：
 * 遍历 VPN RIB 的两级表（外层按 RD，内层按前缀），对每个 dest 内“属于本 peer”的
 * path，调用 bgp_update_check_valid_flags 重算 valid（注意 reflector-client 时
 * 传 NULL next-hop 前缀，语义不同），命中后置 BGP_NODE_USER_CLEAR 并 bgp_process。
 */
static void peer_vpn_change_bestpath(struct peer *peer, afi_t afi)
{
	struct bgp_dest *dest, *dest_vpn;
	struct bgp_path_info *pi;
	const struct prefix *bgp_nht_param_prefix;
	const struct prefix *p;
	struct bgp_table *table;
	bool match_peer;

	for (dest_vpn = bgp_table_top(peer->bgp->rib[afi][SAFI_MPLS_VPN]); dest_vpn;
	     dest_vpn = bgp_route_next(dest_vpn)) {
		table = bgp_dest_get_bgp_table_info(dest_vpn);
		if (!table)
			continue;
		for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
			p = bgp_dest_get_prefix(dest);
			if (CHECK_FLAG(peer->af_flags[afi][SAFI_MPLS_VPN],
				       PEER_FLAG_REFLECTOR_CLIENT))
				bgp_nht_param_prefix = NULL;
			else
				bgp_nht_param_prefix = p;
			match_peer = false;
			for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
				if (pi->peer == NULL || pi->peer != peer)
					continue;
				match_peer = true;
				bgp_update_check_valid_flags(peer->bgp, peer, dest, p, afi,
							     SAFI_MPLS_VPN, pi, pi->attr,
							     bgp_nht_param_prefix,
							     peergroup_af_flag_check(peer, afi,
										     SAFI_MPLS_VPN,
										     PEER_FLAG_ACCEPT_OWN));
			}
			if (match_peer) {
				SET_FLAG(dest->flags, BGP_NODE_USER_CLEAR);
				bgp_process(peer->bgp, dest, bgp_dest_get_bgp_path_info(dest), afi,
					    SAFI_MPLS_VPN);
			}
		}
	}
}

/* ========== 8. 配置变更→动作：peer_change_action 与 flag 三件套 ========== */

/*
 * 依据“变更类型”对已建立的 peer 执行相应动作。这是所有 SET/UNSET 的“后置副作用”，
 * 决定了改一个配置到底是“软出（重发）”还是“硬重置会话”。
 *
 * @type:
 *  - peer_change_reset    : 双向硬重置（删除 doppelganger + 发 CONFIG_CHANGE 通知）。
 *  - peer_change_reset_in : 只重置入向。有动态 refresh 能力则发 ROUTE_REFRESH，
 *                           否则等同硬重置。
 *  - peer_change_reset_out: 出向刷新。置子组 SUBGRP_STATUS_FORCE_UPDATES、
 *                           调整组的 peer、并对该族重新 bgp_announce_route（重发）。
 *  - peer_change_best_path: 触发最佳路径重算（VPN 时调用 peer_vpn_change_bestpath，
 *                           并同样走 reset_out 的“强制更新+通告”路径）。
 *  - peer_change_none     : 不做会话动作。
 * 前置：peer-group 自身直接返回；会话未建立（非 Established）也返回。
 */
void peer_change_action(struct peer *peer, afi_t afi, safi_t safi,
			       enum peer_change_type type)
{
	struct peer_af *paf;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return;

	if (!peer_established(peer->connection))
		return;

	if (type == peer_change_reset) {
		/* If we're resetting session, we've to delete both peer struct
		 */
		if ((peer->doppelganger) &&
		    (peer->doppelganger->connection->status != Deleted) &&
		    (!CHECK_FLAG(peer->doppelganger->flags,
				 PEER_FLAG_CONFIG_NODE)))
			peer_delete(peer->doppelganger);

		peer_notify_config_change(peer->connection);
	} else if (type == peer_change_reset_in) {
		if (CHECK_FLAG(peer->cap, PEER_CAP_REFRESH_RCV))
			bgp_route_refresh_send(peer, afi, safi, 0, 0, 0,
					       BGP_ROUTE_REFRESH_NORMAL);
		else {
			if ((peer->doppelganger) &&
			    (peer->doppelganger->connection->status != Deleted) &&
			    (!CHECK_FLAG(peer->doppelganger->flags,
					 PEER_FLAG_CONFIG_NODE)))
				peer_delete(peer->doppelganger);

			peer_notify_config_change(peer->connection);
		}
	}
	if (type == peer_change_best_path && safi == SAFI_MPLS_VPN)
		/* re-run best path on incoming BGP updated from peer */
		peer_vpn_change_bestpath(peer, afi);
	if (type == peer_change_reset_out || type == peer_change_best_path) {
		paf = peer_af_find(peer, afi, safi);
		if (paf && paf->subgroup)
			SET_FLAG(paf->subgroup->sflags,
				 SUBGRP_STATUS_FORCE_UPDATES);

		update_group_adjust_peer(paf);
		bgp_announce_route(peer, afi, safi, false);
	}
}

/* 描述“某个 peer 标志位在被修改时应该触发什么动作”的映射表项。 */
struct peer_flag_action {
	/* Peer's flag.  */
	/* 被描述的标志位（可含多个位，用 CHECK_FLAG 匹配）。 */
	uint64_t flag;

	/* This flag can be set for peer-group member.  */
	/* 该标志是否“不允许用于 peer-group 成员”。 */
	uint8_t not_for_member;

	/* Action when the flag is changed.  */
	/* 该位变化后要执行的 peer_change_* 动作。 */
	enum peer_change_type type;
};

/*
 * “整机级” peer 标志 → 变更动作 的映射表。第二列 not_for_member 全为 0，表示这些
 * 标志可用于 peer-group 成员。第三列决定该标志被修改后触发何种会话动作：
 * 多数 reset（重置）、部分 none（无需动会话，例如纯展示/能力开关）、个别 reset_in/out。
 */
static const struct peer_flag_action peer_flag_action_list[] = {
	{ PEER_FLAG_PASSIVE, 0, peer_change_reset },
	{ PEER_FLAG_SHUTDOWN, 0, peer_change_reset },
	{ PEER_FLAG_RTT_SHUTDOWN, 0, peer_change_none },
	{ PEER_FLAG_DONT_CAPABILITY, 0, peer_change_none },
	{ PEER_FLAG_OVERRIDE_CAPABILITY, 0, peer_change_none },
	{ PEER_FLAG_STRICT_CAP_MATCH, 0, peer_change_none },
	{ PEER_FLAG_DYNAMIC_CAPABILITY, 0, peer_change_none },
	{ PEER_FLAG_DISABLE_CONNECTED_CHECK, 0, peer_change_reset },
	{ PEER_FLAG_CAPABILITY_ENHE, 0, peer_change_reset },
	{ PEER_FLAG_ENFORCE_FIRST_AS, 0, peer_change_reset_in },
	{ PEER_FLAG_IFPEER_V6ONLY, 0, peer_change_reset },
	{ PEER_FLAG_ROUTEADV, 0, peer_change_none },
	{ PEER_FLAG_TIMER, 0, peer_change_none },
	{ PEER_FLAG_TIMER_CONNECT, 0, peer_change_none },
	{ PEER_FLAG_TIMER_DELAYOPEN, 0, peer_change_none },
	{ PEER_FLAG_PASSWORD, 0, peer_change_none },
	{ PEER_FLAG_LOCAL_AS, 0, peer_change_reset },
	{ PEER_FLAG_LOCAL_AS_NO_PREPEND, 0, peer_change_reset },
	{ PEER_FLAG_LOCAL_AS_REPLACE_AS, 0, peer_change_reset },
	{ PEER_FLAG_DUAL_AS, 0, peer_change_reset },
	{ PEER_FLAG_UPDATE_SOURCE, 0, peer_change_none },
	{ PEER_FLAG_DISABLE_LINK_BW_ENCODING_IEEE, 0, peer_change_none },
	{ PEER_FLAG_EXTENDED_OPT_PARAMS, 0, peer_change_reset },
	{ PEER_FLAG_ROLE_STRICT_MODE, 0, peer_change_none },
	{ PEER_FLAG_ROLE, 0, peer_change_none },
	{ PEER_FLAG_PORT, 0, peer_change_reset },
	{ PEER_FLAG_AIGP, 0, peer_change_none },
	{ PEER_FLAG_GRACEFUL_SHUTDOWN, 0, peer_change_none },
	{ PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD, 0, peer_change_none },
	{ PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW, 0, peer_change_none },
	{ PEER_FLAG_CAPABILITY_FQDN, 0, peer_change_none },
	{ PEER_FLAG_AS_LOOP_DETECTION, 0, peer_change_reset },
	{ PEER_FLAG_EXTENDED_LINK_BANDWIDTH, 0, peer_change_none },
	{ PEER_FLAG_LONESOUL, 0, peer_change_reset_out },
	{ PEER_FLAG_TCP_MSS, 0, peer_change_none },
	{ PEER_FLAG_CAPABILITY_LINK_LOCAL, 0, peer_change_none },
	{ PEER_FLAG_BFD_STRICT, 0, peer_change_none },
	{ PEER_FLAG_RPKI_STRICT, 0, peer_change_none },
	{ PEER_FLAG_SEND_NHC_ATTRIBUTE, 0, peer_change_none },
	{ PEER_FLAG_IP_TRANSPARENT, 0, peer_change_reset },
	{ 0, 0, 0 }
};

/*
 * “每地址族” peer 标志 → 变更动作 的映射表。第二列 not_for_member 多为 1，表示
 * 这些族级标志通常应配置在 peer-group 上（若直接配到成员上会有告警/限制语义）。
 * 典型：SEND_* 用 reset_out（只需重新组包重发），MAX_PREFIX 系列用 none。
 */
static const struct peer_flag_action peer_af_flag_action_list[] = {
	{ PEER_FLAG_SEND_COMMUNITY, 1, peer_change_reset_out },
	{ PEER_FLAG_SEND_EXT_COMMUNITY, 1, peer_change_reset_out },
	{ PEER_FLAG_SEND_LARGE_COMMUNITY, 1, peer_change_reset_out },
	{ PEER_FLAG_NEXTHOP_SELF, 1, peer_change_reset_out },
	{ PEER_FLAG_REFLECTOR_CLIENT, 1, peer_change_reset },
	{ PEER_FLAG_RSERVER_CLIENT, 1, peer_change_reset },
	{ PEER_FLAG_SOFT_RECONFIG, 0, peer_change_reset_in },
	{ PEER_FLAG_AS_PATH_UNCHANGED, 1, peer_change_reset_out },
	{ PEER_FLAG_NEXTHOP_UNCHANGED, 1, peer_change_reset_out },
	{ PEER_FLAG_MED_UNCHANGED, 1, peer_change_reset_out },
	{ PEER_FLAG_DEFAULT_ORIGINATE, 0, peer_change_none },
	{ PEER_FLAG_REMOVE_PRIVATE_AS, 1, peer_change_reset_out },
	{ PEER_FLAG_ALLOWAS_IN, 0, peer_change_reset_in },
	{ PEER_FLAG_ALLOWAS_IN_ORIGIN, 0, peer_change_reset_in },
	{ PEER_FLAG_ORF_PREFIX_SM, 1, peer_change_reset },
	{ PEER_FLAG_ORF_PREFIX_RM, 1, peer_change_reset },
	{ PEER_FLAG_MAX_PREFIX, 0, peer_change_none },
	{ PEER_FLAG_MAX_PREFIX_WARNING, 0, peer_change_none },
	{ PEER_FLAG_MAX_PREFIX_FORCE, 0, peer_change_none },
	{ PEER_FLAG_MAX_PREFIX_OUT, 0, peer_change_none },
	{ PEER_FLAG_NEXTHOP_LOCAL_UNCHANGED, 0, peer_change_reset_out },
	{ PEER_FLAG_FORCE_NEXTHOP_SELF, 1, peer_change_reset_out },
	{ PEER_FLAG_REMOVE_PRIVATE_AS_ALL, 1, peer_change_reset_out },
	{ PEER_FLAG_REMOVE_PRIVATE_AS_REPLACE, 1, peer_change_reset_out },
	{ PEER_FLAG_AS_OVERRIDE, 1, peer_change_reset_out },
	{ PEER_FLAG_REMOVE_PRIVATE_AS_ALL_REPLACE, 1, peer_change_reset_out },
	{ PEER_FLAG_WEIGHT, 0, peer_change_reset_in },
	{ PEER_FLAG_DISABLE_ADDPATH_RX, 0, peer_change_reset },
	{ PEER_FLAG_SOO, 0, peer_change_reset },
	{ PEER_FLAG_ACCEPT_OWN, 0, peer_change_reset },
	{ PEER_FLAG_SEND_EXT_COMMUNITY_RPKI, 1, peer_change_reset_out },
	{ PEER_FLAG_ADDPATH_RX_PATHS_LIMIT, 0, peer_change_none },
	{ PEER_FLAG_CONFIG_ENCAPSULATION_SRV6, 0, peer_change_best_path },
	{ PEER_FLAG_CONFIG_ENCAPSULATION_SRV6_RELAX, 0, peer_change_best_path },
	{ PEER_FLAG_CONFIG_ENCAPSULATION_MPLS, 0, peer_change_best_path },
	{ 0, 0, 0 }
};

/* Proper action set. */
/*
 * 在动作表 action_list 中查找与 flag 匹配的所有项，归并出“最终要执行的动作”。
 * 归并规则（多标志位可能同时命中，取“最强”的）：
 *   reset_in+reset_out → reset；仅 reset_in → reset_in；仅 reset_out → reset_out；
 *   仅 best_path → best_path；都没有 → none。
 * 同时把任一命中项的 not_for_member 汇总到 action->not_for_member。
 * @return : 是否找到匹配项（未找到则调用者报 BGP_ERR_INVALID_FLAG）。
 */
static int peer_flag_action_set(const struct peer_flag_action *action_list,
				int size, struct peer_flag_action *action,
				uint64_t flag)
{
	int i;
	int found = 0;
	int reset_in = 0;
	int reset_out = 0;
	int bestpath_rerun = 0;
	const struct peer_flag_action *match = NULL;

	/* Check peer's frag action.  */
	for (i = 0; i < size; i++) {
		match = &action_list[i];

		if (match->flag == 0)
			break;

		if (CHECK_FLAG(match->flag, flag)) {
			found = 1;

			if (match->type == peer_change_reset_in)
				reset_in = 1;
			if (match->type == peer_change_reset_out)
				reset_out = 1;
			if (match->type == peer_change_reset) {
				reset_in = 1;
				reset_out = 1;
			}
			if (match->type == peer_change_best_path)
				bestpath_rerun = 1;
			if (match->not_for_member)
				action->not_for_member = 1;
		}
	}

	/* Set peer clear type.  */
	if (reset_in && reset_out)
		action->type = peer_change_reset;
	else if (reset_in)
		action->type = peer_change_reset_in;
	else if (reset_out)
		action->type = peer_change_reset_out;
	else if (bestpath_rerun)
		action->type = peer_change_best_path;
	else
		action->type = peer_change_none;

	return found;
}

/*
 * 当“整机级”标志发生变化后，对面向该 peer 的会话执行实际动作：
 *  - 纯能力类标志（动态/ENHE/FQDN/soft-version/DONT_/OVERRIDE_/STRICT_/LINK_LOCAL）
 *    只记录 last_reset=CAPABILITY_CHANGE；PASSIVE 记 PASSIVE_CHANGE；
 *    DISABLE_CONNECTED_CHECK 记 MULTIHOP_CHANGE。
 *  - SHUTDOWN 置位时：停 NSF、清 PREFIX_OVERFLOW、停 maximum-prefix 重启定时器，
 *    并在会话可发通知时发 CEASE/ADMIN_SHUTDOWN（带可选自定义消息，消息可能继承自
 *    peer-group 的 conf->tx_shutdown_message），否则直接复位；
 *    SHUTDOWN 清除时：复位 v_start 并注入 BGP_Stop 以重新开始。
 *  - ENHE 且有动态能力：仅记 CAPABILITY_CHANGE（靠动态能力协商，不拆会话）。
 *  - 其它：尝试发 CONFIG_CHANGE 通知，不成功则直接 bgp_session_reset。
 */
static void peer_flag_modify_action(struct peer *peer, uint64_t flag)
{
	if (flag == PEER_FLAG_DYNAMIC_CAPABILITY || flag == PEER_FLAG_CAPABILITY_ENHE ||
	    flag == PEER_FLAG_CAPABILITY_FQDN || flag == PEER_FLAG_CAPABILITY_SOFT_VERSION_OLD ||
	    flag == PEER_FLAG_CAPABILITY_SOFT_VERSION_NEW || flag == PEER_FLAG_DONT_CAPABILITY ||
	    flag == PEER_FLAG_OVERRIDE_CAPABILITY || flag == PEER_FLAG_STRICT_CAP_MATCH ||
	    flag == PEER_FLAG_CAPABILITY_LINK_LOCAL)
		peer_set_last_reset(peer, PEER_DOWN_CAPABILITY_CHANGE);
	else if (flag == PEER_FLAG_PASSIVE)
		peer_set_last_reset(peer, PEER_DOWN_PASSIVE_CHANGE);
	else if (flag == PEER_FLAG_DISABLE_CONNECTED_CHECK)
		peer_set_last_reset(peer, PEER_DOWN_MULTIHOP_CHANGE);

	if (flag == PEER_FLAG_SHUTDOWN) {
		if (CHECK_FLAG(peer->flags, flag)) {
			if (CHECK_FLAG(peer->sflags, PEER_STATUS_NSF_WAIT))
				peer_nsf_stop(peer);

			UNSET_FLAG(peer->sflags, PEER_STATUS_PREFIX_OVERFLOW);

			if (peer->connection->t_pmax_restart) {
				event_cancel(&peer->connection->t_pmax_restart);
				if (bgp_debug_neighbor_events(peer))
					zlog_debug(
						"%pBP Maximum-prefix restart timer canceled",
						peer);
			}

			if (BGP_IS_VALID_STATE_FOR_NOTIF(
				    peer->connection->status)) {
				char *msg = peer->tx_shutdown_message;
				size_t msglen;
				uint8_t msgbuf[BGP_ADMIN_SHUTDOWN_MSG_LEN + 1];

				if (!msg && peer_group_active(peer))
					msg = peer->group->conf
						      ->tx_shutdown_message;
				msglen = msg ? strlen(msg) : 0;
				if (msglen > BGP_ADMIN_SHUTDOWN_MSG_LEN)
					msglen = BGP_ADMIN_SHUTDOWN_MSG_LEN;

				if (msglen) {
					msgbuf[0] = msglen;
					memcpy(msgbuf + 1, msg, msglen);

					bgp_notify_send_with_data(
						peer->connection,
						BGP_NOTIFY_CEASE,
						BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN,
						msgbuf, msglen + 1);
				} else
					bgp_notify_send(peer->connection,
							BGP_NOTIFY_CEASE,
							BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
			} else
				bgp_session_reset(peer);
		} else {
			peer->v_start = BGP_INIT_START_TIMER;
			BGP_EVENT_ADD(peer->connection, BGP_Stop);
		}
	} else if (CHECK_FLAG(peer->cap, PEER_CAP_DYNAMIC_RCV) &&
		   CHECK_FLAG(peer->cap, PEER_CAP_DYNAMIC_ADV) &&
		   flag == PEER_FLAG_CAPABILITY_ENHE) {
		peer_set_last_reset(peer, PEER_DOWN_CAPABILITY_CHANGE);
	} else if (!peer_notify_config_change(peer->connection))
		bgp_session_reset(peer);
}

/* Enable global administrative shutdown of all peers of BGP instance */
/*
 * 开启实例级“管理性关闭”（`bgp shutdown`）：对该实例所有 peer 逐一
 * 记录 last_reset=USER_SHUTDOWN，向可发通知的 peer 发 CEASE/ADMIN_SHUTDOWN
 * （msg 非空则作为 RFC4486 关闭消息携带，长度截断到 BGP_ADMIN_SHUTDOWN_MSG_LEN），
 * 复位 v_start 并注入 BGP_Stop；最后置实例 BGP_FLAG_SHUTDOWN。
 * 已处于关闭状态则直接返回（幂等）。
 */
void bgp_shutdown_enable(struct bgp *bgp, const char *msg)
{
	struct peer *peer;
	struct listnode *node;
	/* length(1) + message(N) */
	uint8_t data[BGP_ADMIN_SHUTDOWN_MSG_LEN + 1];

	/* do nothing if already shut down */
	if (CHECK_FLAG(bgp->flags, BGP_FLAG_SHUTDOWN))
		return;

	/* informational log message */
	zlog_info("Enabled administrative shutdown on BGP instance AS %u",
		  bgp->as);

	/* iterate through peers of BGP instance */
	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
		peer_set_last_reset(peer, PEER_DOWN_USER_SHUTDOWN);

		/* continue, if peer is already in administrative shutdown. */
		if (CHECK_FLAG(peer->flags, PEER_FLAG_SHUTDOWN))
			continue;

		/* send a RFC 4486 notification message if necessary */
		if (BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status)) {
			if (msg) {
				size_t datalen = strlen(msg);

				if (datalen > BGP_ADMIN_SHUTDOWN_MSG_LEN)
					datalen = BGP_ADMIN_SHUTDOWN_MSG_LEN;

				data[0] = datalen;
				memcpy(data + 1, msg, datalen);

				bgp_notify_send_with_data(peer->connection,
							  BGP_NOTIFY_CEASE,
							  BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN,
							  data, datalen + 1);
			} else {
				bgp_notify_send(peer->connection,
						BGP_NOTIFY_CEASE,
						BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
			}
		}

		/* reset start timer to initial value */
		peer->v_start = BGP_INIT_START_TIMER;

		/* trigger a RFC 4271 ManualStop event */
		BGP_EVENT_ADD(peer->connection, BGP_Stop);
	}

	/* set the BGP instances shutdown flag */
	SET_FLAG(bgp->flags, BGP_FLAG_SHUTDOWN);
}

/* Disable global administrative shutdown of all peers of BGP instance */
/*
 * 关闭实例级管理性关闭（`no bgp shutdown`）：重启 startup 定时器（让 peer 重新
 * 发送 R-bit/重建）、清 BGP_FLAG_SHUTDOWN、对每个 peer 调 bgp_timer_set 重新拉起
 * 其 FSM 定时器。未处于关闭状态则直接返回。
 */
void bgp_shutdown_disable(struct bgp *bgp)
{
	const struct listnode *node;
	struct peer *peer;

	/* do nothing if not shut down. */
	if (!CHECK_FLAG(bgp->flags, BGP_FLAG_SHUTDOWN))
		return;

	/* informational log message */
	zlog_info("Disabled administrative shutdown on BGP instance AS %u",
		  bgp->as);

	/* Reactive the startup timer so peers send R-bit. */
	event_cancel(&bgp->t_startup);
	event_add_timer(bm->master, bgp_startup_timer_expire, bgp, bgp->restart_time,
			&bgp->t_startup);

	/* clear the BGP instances shutdown flag */
	UNSET_FLAG(bgp->flags, BGP_FLAG_SHUTDOWN);

	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer))
		bgp_timer_set(peer->connection);
}

/* Change specified peer flag. */
/*
 * 【继承三件套·整机级】修改/查询一个 peer 标志位（set=1 置位，set=0 清位）。
 * 这是所有整机级 peer_flag_set/unset 的底层实现，直接操作 flags/flags_override/
 * flags_invert 三个数组，并触发动作与向组成员传播。
 *
 * 关键语义（注意 invert 的交错）：
 *  - invert = 本 peer 当前是否对该位取反。
 *  - “期望状态已达成”时**不动 flags**，而是调整 flags_override 后返回：
 *      · set 且当前已置位 → flags_override = !invert（set 使其“显式化”）；
 *      · unset 且当前未置位 → flags_override = invert。
 *  - 真正需要改动时：若 peer 是组成员且 set==invert，则“回到跟随组”（peer_flag_inherit），
 *    否则直接 COND_FLAG 置/清。随后依据 set!=invert 更新 flags_override。
 *  - 若是对“组自身”调用（PEER_STATUS_GROUP），继续走后面的成员传播循环：对每个
 *    未 override 该位的成员，按其自身 invert 计算目标值（member_invert），并执行动作。
 * 冲突检查：STRICT_CAP_MATCH 与 OVERRIDE_CAPABILITY 不能同时为真。
 * @return : 0 或 BGP_ERR_INVALID_FLAG / BGP_ERR_PEER_FLAG_CONFLICT。
 */
static int peer_flag_modify(struct peer *peer, uint64_t flag, int set)
{
	int found;
	int size;
	bool invert, member_invert;
	struct peer *member;
	struct listnode *node, *nnode;
	struct peer_flag_action action;

	memset(&action, 0, sizeof(struct peer_flag_action));
	size = sizeof(peer_flag_action_list) / sizeof(struct peer_flag_action);

	invert = CHECK_FLAG(peer->flags_invert, flag);
	found = peer_flag_action_set(peer_flag_action_list, size, &action,
				     flag);

	/* Abort if no flag action exists. */
	if (!found)
		return BGP_ERR_INVALID_FLAG;

	/* Check for flag conflict: STRICT_CAP_MATCH && OVERRIDE_CAPABILITY */
	if (set && CHECK_FLAG(peer->flags | flag, PEER_FLAG_STRICT_CAP_MATCH)
	    && CHECK_FLAG(peer->flags | flag, PEER_FLAG_OVERRIDE_CAPABILITY))
		return BGP_ERR_PEER_FLAG_CONFLICT;

	/* Handle flag updates where desired state matches current state. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (set && CHECK_FLAG(peer->flags, flag)) {
			COND_FLAG(peer->flags_override, flag, !invert);
			return 0;
		}

		if (!set && !CHECK_FLAG(peer->flags, flag)) {
			COND_FLAG(peer->flags_override, flag, invert);
			return 0;
		}
	}

	/* Inherit from peer-group or set/unset flags accordingly. */
	if (peer_group_active(peer) && set == invert)
		peer_flag_inherit(peer, flag);
	else
		COND_FLAG(peer->flags, flag, set);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Update flag override state accordingly. */
		COND_FLAG(peer->flags_override, flag, set != invert);

		/*
		 * For the extended next-hop encoding flag we need to turn RAs
		 * on if flag is being set, but only turn RAs off if the flag
		 * is being unset on this peer and if this peer is a member of a
		 * peer-group, the peer-group also doesn't have the flag set.
		 */
		if (flag == PEER_FLAG_CAPABILITY_ENHE) {
			if (set) {
				bgp_zebra_initiate_radv(peer->bgp, peer);
			} else if (peer_group_active(peer)) {
				if (!CHECK_FLAG(peer->group->conf->flags,
						flag) &&
				    !peer->conf_if)
					bgp_zebra_terminate_radv(peer->bgp,
								 peer);
			} else
				bgp_zebra_terminate_radv(peer->bgp, peer);
		}

		if ((flag == PEER_FLAG_SHUTDOWN) && set)
			peer_set_last_reset(peer, PEER_DOWN_USER_SHUTDOWN);

		/* Execute flag action on peer. */
		if (action.type == peer_change_reset)
			peer_flag_modify_action(peer, flag);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Update peer-group members, unless they are explicitly overriding
	 * peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, flag))
			continue;

		/* Check if only member without group is inverted. */
		member_invert =
			CHECK_FLAG(member->flags_invert, flag) && !invert;

		/* Skip peers with equivalent configuration. */
		if (set != member_invert && CHECK_FLAG(member->flags, flag))
			continue;

		if (set == member_invert && !CHECK_FLAG(member->flags, flag))
			continue;

		/* Update flag on peer-group member. */
		COND_FLAG(member->flags, flag, set != member_invert);

		if (flag == PEER_FLAG_CAPABILITY_ENHE && !member->conf_if)
			set ? bgp_zebra_initiate_radv(member->bgp, member)
			    : bgp_zebra_terminate_radv(member->bgp, member);

		if ((flag == PEER_FLAG_SHUTDOWN) && set)
			peer_set_last_reset(member, PEER_DOWN_USER_SHUTDOWN);

		/* Execute flag action on peer-group member. */
		if (action.type == peer_change_reset)
			peer_flag_modify_action(member, flag);
	}

	return 0;
}

/* 置位一个整机级 peer 标志（peer_flag_modify 的薄封装）。 */
int peer_flag_set(struct peer *peer, uint64_t flag)
{
	return peer_flag_modify(peer, flag, 1);
}

/* 清除一个整机级 peer 标志（peer_flag_modify 的薄封装）。 */
int peer_flag_unset(struct peer *peer, uint64_t flag)
{
	return peer_flag_modify(peer, flag, 0);
}

/*
 * 【继承三件套·地址族版】修改一个 peer 在某 (afi,safi) 上的族级标志。
 * 与 peer_flag_modify 完全同构，只是作用在 af_flags[afi][safi] /
 * af_flags_override / af_flags_invert 上；此外有一段“EVPN / route-server 的隐含
 * NEXTHOP_UNCHANGED 处理”：
 *  - EVPN：设置 NEXTHOP_SELF/FORCE_NEXTHOP_SELF 时清 NEXTHOP_UNCHANGED，取消时恢复；
 *  - RSERVER_CLIENT：置位则强制 NEXTHOP_UNCHANGED，清位则取消。
 * 特殊校验：REFLECTOR_CLIENT 仅允许 IBGP；REMOVE_PRIVATE_AS / AS_OVERRIDE 不允许 IBGP。
 * 会话动作中：SOFT_RECONFIG 清除会 bgp_clear_adj_in；ORF 前缀标志在有动态能力时不拆会话。
 * 组成员传播逻辑同整机版（按 member 的 invert 计算目标值并执行动作）。
 * @return : 0 或 BGP_ERR_INVALID_FLAG / BGP_ERR_NOT_INTERNAL_PEER /
 *           BGP_ERR_REMOVE_PRIVATE_AS / BGP_ERR_AS_OVERRIDE。
 */
static int peer_af_flag_modify(struct peer *peer, afi_t afi, safi_t safi,
			       uint64_t flag, bool set)
{
	int found;
	int size;
	bool invert, member_invert;
	struct peer *member;
	struct listnode *node, *nnode;
	struct peer_flag_action action;
	enum bgp_peer_sort ptype;

	memset(&action, 0, sizeof(struct peer_flag_action));
	size = sizeof(peer_af_flag_action_list)
	       / sizeof(struct peer_flag_action);

	invert = CHECK_FLAG(peer->af_flags_invert[afi][safi], flag);
	found = peer_flag_action_set(peer_af_flag_action_list, size, &action,
				     flag);

	/* Abort if flag action exists. */
	if (!found)
		return BGP_ERR_INVALID_FLAG;

	ptype = peer_sort(peer);
	/* Special check for reflector client.  */
	if (CHECK_FLAG(flag, PEER_FLAG_REFLECTOR_CLIENT) &&
	    ptype != BGP_PEER_IBGP)
		return BGP_ERR_NOT_INTERNAL_PEER;

	/* Special check for remove-private-AS.  */
	if (CHECK_FLAG(flag, PEER_FLAG_REMOVE_PRIVATE_AS) &&
	    ptype == BGP_PEER_IBGP)
		return BGP_ERR_REMOVE_PRIVATE_AS;

	/* as-override is not allowed for IBGP peers */
	if (CHECK_FLAG(flag, PEER_FLAG_AS_OVERRIDE) && ptype == BGP_PEER_IBGP)
		return BGP_ERR_AS_OVERRIDE;

	/* Handle flag updates where desired state matches current state. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (set && CHECK_FLAG(peer->af_flags[afi][safi], flag)) {
			COND_FLAG(peer->af_flags_override[afi][safi], flag,
				  !invert);
			return 0;
		}

		if (!set && !CHECK_FLAG(peer->af_flags[afi][safi], flag)) {
			COND_FLAG(peer->af_flags_override[afi][safi], flag,
				  invert);
			return 0;
		}
	}

	/*
	 * For EVPN we implicitly set the NEXTHOP_UNCHANGED flag,
	 * if we are setting/unsetting flags which conflict with this flag
	 * handle accordingly
	 */
	if (afi == AFI_L2VPN && safi == SAFI_EVPN) {
		if (set) {

			/*
			 * if we are setting NEXTHOP_SELF, we need to unset the
			 * NEXTHOP_UNCHANGED flag
			 */
			if (CHECK_FLAG(flag, PEER_FLAG_NEXTHOP_SELF) ||
			    CHECK_FLAG(flag, PEER_FLAG_FORCE_NEXTHOP_SELF))
				UNSET_FLAG(peer->af_flags[afi][safi],
					   PEER_FLAG_NEXTHOP_UNCHANGED);
		} else {

			/*
			 * if we are unsetting NEXTHOP_SELF, we need to set the
			 * NEXTHOP_UNCHANGED flag to reset the defaults for EVPN
			 */
			if (CHECK_FLAG(flag, PEER_FLAG_NEXTHOP_SELF) ||
			    CHECK_FLAG(flag, PEER_FLAG_FORCE_NEXTHOP_SELF))
				SET_FLAG(peer->af_flags[afi][safi],
					 PEER_FLAG_NEXTHOP_UNCHANGED);
		}
	}

	/*
	 * If the peer is a route server client let's not
	 * muck with the nexthop on the way out the door
	 */
	if (CHECK_FLAG(flag, PEER_FLAG_RSERVER_CLIENT)) {
		if (set)
			SET_FLAG(peer->af_flags[afi][safi],
				 PEER_FLAG_NEXTHOP_UNCHANGED);
		else
			UNSET_FLAG(peer->af_flags[afi][safi],
				   PEER_FLAG_NEXTHOP_UNCHANGED);
	}

	/* Inherit from peer-group or set/unset flags accordingly. */
	if (peer_group_active(peer) && set == invert)
		peer_af_flag_inherit(peer, afi, safi, flag);
	else
		COND_FLAG(peer->af_flags[afi][safi], flag, set);

	/* Execute action when peer is established.  */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP) &&
	    peer_established(peer->connection)) {
		if (!set && flag == PEER_FLAG_SOFT_RECONFIG)
			bgp_clear_adj_in(peer, afi, safi);
		else {
			if (flag == PEER_FLAG_REFLECTOR_CLIENT)
				peer_set_last_reset(peer, PEER_DOWN_RR_CLIENT_CHANGE);
			else if (flag == PEER_FLAG_RSERVER_CLIENT)
				peer_set_last_reset(peer, PEER_DOWN_RS_CLIENT_CHANGE);
			else if (flag == PEER_FLAG_ORF_PREFIX_SM)
				peer_set_last_reset(peer, PEER_DOWN_CAPABILITY_CHANGE);
			else if (flag == PEER_FLAG_ORF_PREFIX_RM)
				peer_set_last_reset(peer, PEER_DOWN_CAPABILITY_CHANGE);

			/* We should not reset the session if
			 * dynamic capability is enabled and we
			 * are changing the ORF prefix flags.
			 */
			if ((CHECK_FLAG(peer->cap, PEER_CAP_DYNAMIC_RCV) &&
			     CHECK_FLAG(peer->cap, PEER_CAP_DYNAMIC_ADV)) &&
			    (flag == PEER_FLAG_ORF_PREFIX_RM ||
			     flag == PEER_FLAG_ORF_PREFIX_SM))
				action.type = peer_change_none;

			peer_change_action(peer, afi, safi, action.type);
		}
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		COND_FLAG(peer->af_flags_override[afi][safi], flag,
			  set != invert);
	} else {
		/*
		 * Update peer-group members, unless they are explicitly
		 * overriding peer-group configuration.
		 */
		for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode,
				       member)) {
			/* Skip peers with overridden configuration. */
			if (CHECK_FLAG(member->af_flags_override[afi][safi],
				       flag))
				continue;

			/* Check if only member without group is inverted. */
			member_invert =
				CHECK_FLAG(member->af_flags_invert[afi][safi],
					   flag)
				&& !invert;

			/* Skip peers with equivalent configuration. */
			if (set != member_invert
			    && CHECK_FLAG(member->af_flags[afi][safi], flag))
				continue;

			if (set == member_invert
			    && !CHECK_FLAG(member->af_flags[afi][safi], flag))
				continue;

			/* Update flag on peer-group member. */
			COND_FLAG(member->af_flags[afi][safi], flag,
				  set != member_invert);

			/* Execute flag action on peer-group member. */
			if (peer_established(member->connection)) {
				if (!set && flag == PEER_FLAG_SOFT_RECONFIG)
					bgp_clear_adj_in(member, afi, safi);
				else {
					if (flag == PEER_FLAG_REFLECTOR_CLIENT)
						peer_set_last_reset(member,
								    PEER_DOWN_RR_CLIENT_CHANGE);
					else if (flag == PEER_FLAG_RSERVER_CLIENT)
						peer_set_last_reset(member,
								    PEER_DOWN_RS_CLIENT_CHANGE);
					else if (flag == PEER_FLAG_ORF_PREFIX_SM)
						peer_set_last_reset(member,
								    PEER_DOWN_CAPABILITY_CHANGE);
					else if (flag == PEER_FLAG_ORF_PREFIX_RM)
						peer_set_last_reset(member,
								    PEER_DOWN_CAPABILITY_CHANGE);

					/* We should not reset the session if
					 * dynamic capability is enabled and we
					 * are changing the ORF prefix flags.
					 */
					if ((CHECK_FLAG(peer->cap,
							PEER_CAP_DYNAMIC_RCV) &&
					     CHECK_FLAG(peer->cap,
							PEER_CAP_DYNAMIC_ADV)) &&
					    (flag == PEER_FLAG_ORF_PREFIX_RM ||
					     flag == PEER_FLAG_ORF_PREFIX_SM))
						action.type = peer_change_none;

					peer_change_action(member, afi, safi,
							   action.type);
				}
			}
		}
	}

	return 0;
}

/* 置位某族级 peer 标志（peer_af_flag_modify 的薄封装）。 */
int peer_af_flag_set(struct peer *peer, afi_t afi, safi_t safi, uint64_t flag)
{
	return peer_af_flag_modify(peer, afi, safi, flag, 1);
}

/* 清除某族级 peer 标志（peer_af_flag_modify 的薄封装）。 */
int peer_af_flag_unset(struct peer *peer, afi_t afi, safi_t safi, uint64_t flag)
{
	return peer_af_flag_modify(peer, afi, safi, flag, 0);
}


/* ========== 9. peer 单项配置 SET/UNSET 辅助（timer/source/password/filter/...） ========== */
/*
 * 设置本端在发送 shutdown 通知时携带的 admin-shutdown 文本（RFC 8203）。
 * 参数：peer 目标对端；msg 文本，NULL 表示清空。
 * 说明：先释放旧串再 XSTRDUP 新串，msg 为 NULL 时置 NULL。
 */
void peer_tx_shutdown_message_set(struct peer *peer, const char *msg)
{
	XFREE(MTYPE_PEER_TX_SHUTDOWN_MSG, peer->tx_shutdown_message);
	peer->tx_shutdown_message =
		msg ? XSTRDUP(MTYPE_PEER_TX_SHUTDOWN_MSG, msg) : NULL;
}

/* 清空 shutdown 通知文本（unset 子命令）。 */
void peer_tx_shutdown_message_unset(struct peer *peer)
{
	XFREE(MTYPE_PEER_TX_SHUTDOWN_MSG, peer->tx_shutdown_message);
}


/* EBGP multihop configuration. */
/*
 * 配置 eBGP multihop（ebgp-multihop <ttl>）。
 * 参数：peer 目标（普通对端或 peer-group）；ttl 跳数（255=MAXTTL 即不限，默认 1）。
 * 返回：0 成功；BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK 表示与 GTSM(ttl-security) 冲突。
 * 关键：multihop 与 ttl-security 互斥——ttl 不为 MAXTTL 时会检查本端/GTSM 配置，
 *       避免两者同时生效造成语义冲突。
 * 副作用：修改 ttl 后触发会话重置（或通知 change）并对 BFD 重新应用源/TTL。
 */
int peer_ebgp_multihop_set(struct peer *peer, int ttl)
{
	struct peer_group *group;
	struct listnode *node, *nnode;
	struct peer *member;

	if (peer->sort == BGP_PEER_IBGP || peer->conf_if)
		return 0;

	/* is there anything to do? */
	if (peer->ttl == ttl)
		return 0;

	/* see comment in peer_ttl_security_hops_set() */
	if (ttl != MAXTTL) {
		if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
			group = peer->group;
			if (group->conf->gtsm_hops != BGP_GTSM_HOPS_DISABLED)
				return BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK;

			for (ALL_LIST_ELEMENTS(group->peer, node, nnode, member)) {
				if (member->sort == BGP_PEER_IBGP)
					continue;

				if (member->gtsm_hops != BGP_GTSM_HOPS_DISABLED)
					return BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK;
			}
		} else {
			if (peer->gtsm_hops != BGP_GTSM_HOPS_DISABLED)
				return BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK;
		}
	}

	peer->ttl = ttl;

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (peer->sort != BGP_PEER_IBGP) {
			if (!peer_notify_config_change(peer->connection))
				bgp_session_reset(peer);

			/* Reconfigure BFD peer with new TTL. */
			if (peer->bfd_config)
				bgp_peer_bfd_update_source(peer);
		}
	} else {
		group = peer->group;
		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, member)) {
			if (member->sort == BGP_PEER_IBGP)
				continue;

			member->ttl = group->conf->ttl;

			if (!peer_notify_config_change(member->connection))
				bgp_session_reset(member);

			/* Reconfigure BFD peer with new TTL. */
			if (member->bfd_config)
				bgp_peer_bfd_update_source(member);
		}
	}
	return 0;
}

/*
 * 取消 eBGP multihop，把 TTL 恢复为 peer-group 继承值或 BGP_DEFAULT_TTL(1)。
 * 返回：0 成功；TTLHACK 冲突错误。
 * 若所属 peer-group 仍配置了 TTL，则成员恢复为组 TTL，否则回落到默认 1。
 */
int peer_ebgp_multihop_unset(struct peer *peer)
{
	struct peer *member;
	struct peer_group *group;
	struct listnode *node, *nnode;
	int ttl;

	if (peer->sort == BGP_PEER_IBGP)
		return 0;

	if (peer->gtsm_hops != BGP_GTSM_HOPS_DISABLED && peer->ttl != MAXTTL)
		return BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK;

	if (peer_group_active(peer))
		ttl = peer->group->conf->ttl;
	else
		ttl = BGP_DEFAULT_TTL;

	if (ttl == peer->ttl)
		return 0;

	peer->ttl = ttl;

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);

		/* Reconfigure BFD peer with new TTL. */
		if (peer->bfd_config)
			bgp_peer_bfd_update_source(peer);
	} else {
		group = peer->group;
		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, member)) {
			if (member->sort == BGP_PEER_IBGP)
				continue;

			member->ttl = BGP_DEFAULT_TTL;

			if (member->connection->fd >= 0) {
				if (!peer_notify_config_change(member->connection))
					bgp_session_reset(member);
			}

			/* Reconfigure BFD peer with new TTL. */
			if (member->bfd_config)
				bgp_peer_bfd_update_source(member);
		}
	}
	return 0;
}

/* Set Open Policy Role and check its correctness */
/*
 * 配置 BGP Open Policy Role（RFC 9234，local-role provider/rs-client/customer）。
 * 参数：peer 目标（普通对端或 peer-group）；role 角色值；strict_mode 是否开启严格模式。
 * 返回：CMD_SUCCESS；BGP_ERR_INVALID_INTERNAL_ROLE（iBGP 不支持角色）。
 * 关键：角色仅允许在 eBGP 上配置；strict mode 变化可能触发会话重置以发出 Role Mismatch 通知。
 * 副作用：对 peer-group 会遍历全部成员同步设置角色与 strict 标志。
 */
int peer_role_set(struct peer *peer, uint8_t role, bool strict_mode)
{
	struct peer *member;
	struct listnode *node, *nnode;

	peer_flag_set(peer, PEER_FLAG_ROLE);

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (peer->sort != BGP_PEER_EBGP)
			return BGP_ERR_INVALID_INTERNAL_ROLE;

		if (peer->local_role == role) {
			if (CHECK_FLAG(peer->flags,
				       PEER_FLAG_ROLE_STRICT_MODE) &&
			    !strict_mode)
				/* TODO: Is session restart needed if it was
				 * down?
				 */
				UNSET_FLAG(peer->flags,
					   PEER_FLAG_ROLE_STRICT_MODE);
			if (!CHECK_FLAG(peer->flags,
					PEER_FLAG_ROLE_STRICT_MODE) &&
			    strict_mode) {
				SET_FLAG(peer->flags,
					 PEER_FLAG_ROLE_STRICT_MODE);
				/* Restart session to throw Role Mismatch
				 * Notification
				 */
				if (peer->remote_role == ROLE_UNDEFINED)
					bgp_session_reset(peer);
			}
		} else {
			peer->local_role = role;
			if (strict_mode)
				SET_FLAG(peer->flags,
					 PEER_FLAG_ROLE_STRICT_MODE);
			else
				UNSET_FLAG(peer->flags,
					   PEER_FLAG_ROLE_STRICT_MODE);
		}

		return CMD_SUCCESS;
	}

	peer->local_role = role;
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		if (member->sort != BGP_PEER_EBGP)
			return BGP_ERR_INVALID_INTERNAL_ROLE;

		if (member->local_role == role) {
			if (CHECK_FLAG(member->flags,
				       PEER_FLAG_ROLE_STRICT_MODE) &&
			    !strict_mode)
				/* TODO: Is session restart needed if it was
				 * down?
				 */
				UNSET_FLAG(member->flags,
					   PEER_FLAG_ROLE_STRICT_MODE);
			if (!CHECK_FLAG(member->flags,
					PEER_FLAG_ROLE_STRICT_MODE) &&
			    strict_mode) {
				SET_FLAG(peer->flags,
					 PEER_FLAG_ROLE_STRICT_MODE);
				SET_FLAG(member->flags,
					 PEER_FLAG_ROLE_STRICT_MODE);
				/* Restart session to throw Role Mismatch
				 * Notification
				 */
				if (member->remote_role == ROLE_UNDEFINED)
					bgp_session_reset(member);
			}
		} else {
			member->local_role = role;

			if (strict_mode) {
				SET_FLAG(peer->flags,
					 PEER_FLAG_ROLE_STRICT_MODE);
				SET_FLAG(member->flags,
					 PEER_FLAG_ROLE_STRICT_MODE);
			} else {
				UNSET_FLAG(member->flags,
					   PEER_FLAG_ROLE_STRICT_MODE);
			}
		}
	}

	return CMD_SUCCESS;
}

/* 清除对端角色配置（回落为 ROLE_UNDEFINED）。 */
int peer_role_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	peer_flag_unset(peer, PEER_FLAG_ROLE);

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return peer_role_set(peer, ROLE_UNDEFINED, 0);

	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member))
		peer_role_set(member, ROLE_UNDEFINED, 0);

	return CMD_SUCCESS;
}

/* Neighbor description. */
/* 设置对端描述文本（仅为本地展示，不影响协议）。 */
void peer_description_set(struct peer *peer, const char *desc)
{
	XFREE(MTYPE_PEER_DESC, peer->desc);

	peer->desc = XSTRDUP(MTYPE_PEER_DESC, desc);
}

/* 清除对端描述文本。 */
void peer_description_unset(struct peer *peer)
{
	XFREE(MTYPE_PEER_DESC, peer->desc);
}

/* Neighbor update-source. */
/*
 * 设置 update-source 为接口名（neighbor x update-source <ifname>）。
 * 参数：peer 目标；ifname 源接口名。
 * 作用：TCP 连接本地源地址取自该接口地址；对 peer-group 会下发到未覆盖该配置的成员。
 * 副作用：清空原有 update_source 地址；重置会话并重配 BFD 源。
 */
int peer_update_source_if_set(struct peer *peer, const char *ifname)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_UPDATE_SOURCE);
	if (peer->update_if) {
		if (strcmp(peer->update_if, ifname) == 0)
			return 0;
		XFREE(MTYPE_PEER_UPDATE_SOURCE, peer->update_if);
	}
	peer->update_if = XSTRDUP(MTYPE_PEER_UPDATE_SOURCE, ifname);
	sockunion_free(peer->update_source);
	peer->update_source = NULL;

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_set_last_reset(peer, PEER_DOWN_UPDATE_SOURCE_CHANGE);
		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);

		/* Apply new source configuration to BFD session. */
		if (peer->bfd_config)
			bgp_peer_bfd_update_source(peer);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_UPDATE_SOURCE))
			continue;

		/* Skip peers with the same configuration. */
		if (member->update_if) {
			if (strcmp(member->update_if, ifname) == 0)
				continue;
			XFREE(MTYPE_PEER_UPDATE_SOURCE, member->update_if);
		}

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_UPDATE_SOURCE);
		member->update_if = XSTRDUP(MTYPE_PEER_UPDATE_SOURCE, ifname);
		sockunion_free(member->update_source);
		member->update_source = NULL;
		peer_set_last_reset(member, PEER_DOWN_UPDATE_SOURCE_CHANGE);

		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(member->connection))
			bgp_session_reset(member);

		/* Apply new source configuration to BFD session. */
		if (member->bfd_config)
			bgp_peer_bfd_update_source(member);
	}

	return 0;
}

/*
 * 设置 update-source 为具体源地址（neighbor x update-source <addr>）。
 * 参数：peer 目标；su 源地址。
 * 作用：与接口版互为互斥资源配置，设置地址会清空 update_if。
 * 副作用：重置会话、重配 BFD 源；peer-group 场景下发到未覆盖的成员。
 */
void peer_update_source_addr_set(struct peer *peer, const union sockunion *su)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_UPDATE_SOURCE);
	if (peer->update_source) {
		if (sockunion_cmp(peer->update_source, su) == 0)
			return;
		sockunion_free(peer->update_source);
	}
	peer->update_source = sockunion_dup(su);
	XFREE(MTYPE_PEER_UPDATE_SOURCE, peer->update_if);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_set_last_reset(peer, PEER_DOWN_UPDATE_SOURCE_CHANGE);
		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);

		/* Apply new source configuration to BFD session. */
		if (peer->bfd_config)
			bgp_peer_bfd_update_source(peer);

		/* Skip peer-group mechanics for regular peers. */
		return;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_UPDATE_SOURCE))
			continue;

		/* Skip peers with the same configuration. */
		if (member->update_source) {
			if (sockunion_cmp(member->update_source, su) == 0)
				continue;
			sockunion_free(member->update_source);
		}

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_UPDATE_SOURCE);
		member->update_source = sockunion_dup(su);
		XFREE(MTYPE_PEER_UPDATE_SOURCE, member->update_if);
		peer_set_last_reset(member, PEER_DOWN_UPDATE_SOURCE_CHANGE);

		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(member->connection))
			bgp_session_reset(member);

		/* Apply new source configuration to BFD session. */
		if (member->bfd_config)
			bgp_peer_bfd_update_source(member);
	}
}

/*
 * 取消 update-source 配置。
 * 关键：若对端是 peer-group 成员，则"继承"回组配置（三件套 + PEER_SU_ATTR_INHERIT），
 *       并特别判断继承后的源地址是否与当前一致，一致则跳过会话重置以减小抖动。
 * 非成员则清除标志与地址/接口配置。
 */
void peer_update_source_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;
	bool src_unchanged = false;

	if (!CHECK_FLAG(peer->flags, PEER_FLAG_UPDATE_SOURCE))
		return;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		/* Don't reset peer if the update_source we'll inherit from
		 * the peer-group matches the peer's existing update_source
		 */
		src_unchanged =
			(peer->update_source &&
			 peer->group->conf->update_source &&
			 sockunion_cmp(peer->update_source,
				       peer->group->conf->update_source) == 0);

		peer_flag_inherit(peer, PEER_FLAG_UPDATE_SOURCE);
		PEER_SU_ATTR_INHERIT(peer, peer->group, update_source);
		PEER_STR_ATTR_INHERIT(peer, peer->group, update_if,
				      MTYPE_PEER_UPDATE_SOURCE);

		if (src_unchanged)
			return;
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_UPDATE_SOURCE);
		sockunion_free(peer->update_source);
		peer->update_source = NULL;
		XFREE(MTYPE_PEER_UPDATE_SOURCE, peer->update_if);
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_set_last_reset(peer, PEER_DOWN_UPDATE_SOURCE_CHANGE);
		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);

		/* Apply new source configuration to BFD session. */
		if (peer->bfd_config)
			bgp_peer_bfd_update_source(peer);

		/* Skip peer-group mechanics for regular peers. */
		return;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_UPDATE_SOURCE))
			continue;

		/* Skip peers with the same configuration. */
		if (!CHECK_FLAG(member->flags, PEER_FLAG_UPDATE_SOURCE)
		    && !member->update_source && !member->update_if)
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_UPDATE_SOURCE);
		sockunion_free(member->update_source);
		member->update_source = NULL;
		XFREE(MTYPE_PEER_UPDATE_SOURCE, member->update_if);
		peer_set_last_reset(member, PEER_DOWN_UPDATE_SOURCE_CHANGE);

		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(member->connection))
			bgp_session_reset(member);

		/* Apply new source configuration to BFD session. */
		if (member->bfd_config)
			bgp_peer_bfd_update_source(member);
	}
}

/*
 * 配置 default-originate（按 AFI/SAFI），多用于向对端注入默认路由。
 * 参数：peer 目标；afi/safi 地址族；rmap 可选 route-map 名；route_map 已解析指针。
 * 关键：default-originate 是 af_flags（按地址族粒度）。
 *       改 route-map 时先 UNSET subgrp 的 SUBGRP_STATUS_DEFAULT_ORIGINATE，
 *       迫使重新生成并下发默认路由 UPDATE；route_map 引用计数增/减配对。
 * 副作用：已 Established 的对端会立即触发 update-group 调整与 announce。
 */
int peer_default_originate_set(struct peer *peer, afi_t afi, safi_t safi,
			       const char *rmap, struct route_map *route_map)
{
	struct peer *member;
	struct listnode *node, *nnode;
	struct update_subgroup *subgrp;

	/* Set flag and configuration on peer. */
	peer_af_flag_set(peer, afi, safi, PEER_FLAG_DEFAULT_ORIGINATE);

	subgrp = peer_subgroup(peer, afi, safi);

	if (rmap) {
		if (!peer->bgp->rmap_def_originate_eval_timer)
			peer->bgp->rmap_def_originate_eval_timer =
				RMAP_DEFAULT_ORIGINATE_EVAL_TIMER;

		if (!peer->default_rmap[afi][safi].name
		    || strcmp(rmap, peer->default_rmap[afi][safi].name) != 0) {
			struct route_map *map = NULL;

			if (peer->default_rmap[afi][safi].name) {
				map = route_map_lookup_by_name(
					peer->default_rmap[afi][safi].name);
				XFREE(MTYPE_ROUTE_MAP_NAME,
				      peer->default_rmap[afi][safi].name);
			}

			/*
			 * When there is a change in route-map policy,
			 * this flow gets triggered. Since, the default
			 * route is already originated, the flag is set.
			 * The flag should be unset here,
			 * to trigger the flow of sending update message.
			 */
			if (subgrp)
				UNSET_FLAG(subgrp->sflags,
					   SUBGRP_STATUS_DEFAULT_ORIGINATE);

			route_map_counter_decrement(map);
			peer->default_rmap[afi][safi].name =
				XSTRDUP(MTYPE_ROUTE_MAP_NAME, rmap);
			peer->default_rmap[afi][safi].map = route_map;
			route_map_counter_increment(route_map);
		}
	} else if (!rmap) {
		struct route_map *map = NULL;

		if (peer->default_rmap[afi][safi].name) {
			map = route_map_lookup_by_name(
				peer->default_rmap[afi][safi].name);
			XFREE(MTYPE_ROUTE_MAP_NAME,
			      peer->default_rmap[afi][safi].name);
		}

		/*
		 * This is triggered in case of route-map deletion.
		 * The flag needs to be unset, to trigger the flow
		 * of sending an update message.
		 */
		if (subgrp)
			UNSET_FLAG(subgrp->sflags,
				   SUBGRP_STATUS_DEFAULT_ORIGINATE);

		route_map_counter_decrement(map);
		peer->default_rmap[afi][safi].name = NULL;
		peer->default_rmap[afi][safi].map = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Update peer route announcements. */
		if (peer_established(peer->connection) &&
		    peer->afc_nego[afi][safi]) {
			update_group_adjust_peer(peer_af_find(peer, afi, safi));
			bgp_default_originate(peer, afi, safi, false);
			bgp_announce_route(peer, afi, safi, false);
		}

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_DEFAULT_ORIGINATE))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->af_flags[afi][safi],
			 PEER_FLAG_DEFAULT_ORIGINATE);
		if (rmap) {
			struct route_map *map = NULL;

			if (!member->bgp->rmap_def_originate_eval_timer)
				member->bgp->rmap_def_originate_eval_timer =
					RMAP_DEFAULT_ORIGINATE_EVAL_TIMER;

			if (member->default_rmap[afi][safi].name) {
				map = route_map_lookup_by_name(
					member->default_rmap[afi][safi].name);
				XFREE(MTYPE_ROUTE_MAP_NAME,
				      member->default_rmap[afi][safi].name);
			}

			route_map_counter_decrement(map);
			member->default_rmap[afi][safi].name =
				XSTRDUP(MTYPE_ROUTE_MAP_NAME, rmap);
			member->default_rmap[afi][safi].map = route_map;
			route_map_counter_increment(route_map);
		}

		/* Update peer route announcements. */
		if (peer_established(member->connection) &&
		    member->afc_nego[afi][safi]) {
			update_group_adjust_peer(
				peer_af_find(member, afi, safi));
			bgp_default_originate(member, afi, safi, false);
			bgp_announce_route(member, afi, safi, false);
		}
	}

	return 0;
}

/*
 * 取消 default-originate。成员对端继承回 peer-group 配置，否则清除标志与 route-map。
 */
int peer_default_originate_unset(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_af_flag_inherit(peer, afi, safi,
				     PEER_FLAG_DEFAULT_ORIGINATE);
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      default_rmap[afi][safi].name,
				      MTYPE_ROUTE_MAP_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  default_rmap[afi][safi].map);
	} else {
		struct route_map *map = NULL;

		/* Otherwise remove flag and configuration from peer. */
		peer_af_flag_unset(peer, afi, safi,
				   PEER_FLAG_DEFAULT_ORIGINATE);
		if (peer->default_rmap[afi][safi].name) {
			map = route_map_lookup_by_name(
				peer->default_rmap[afi][safi].name);
			XFREE(MTYPE_ROUTE_MAP_NAME,
			      peer->default_rmap[afi][safi].name);
		}
		route_map_counter_decrement(map);
		peer->default_rmap[afi][safi].name = NULL;
		peer->default_rmap[afi][safi].map = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Update peer route announcements. */
		if (peer_established(peer->connection) &&
		    peer->afc_nego[afi][safi]) {
			update_group_adjust_peer(peer_af_find(peer, afi, safi));
			bgp_default_originate(peer, afi, safi, true);
			bgp_announce_route(peer, afi, safi, false);
		}

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		struct route_map *map;

		map = NULL;

		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_DEFAULT_ORIGINATE))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->af_flags[afi][safi],
			   PEER_FLAG_DEFAULT_ORIGINATE);
		if (member->default_rmap[afi][safi].name) {
			map = route_map_lookup_by_name(
				member->default_rmap[afi][safi].name);
			XFREE(MTYPE_ROUTE_MAP_NAME,
			      member->default_rmap[afi][safi].name);
		}
		route_map_counter_decrement(map);
		member->default_rmap[afi][safi].name = NULL;
		member->default_rmap[afi][safi].map = NULL;

		/* Update peer route announcements. */
		if (peer_established(member->connection) &&
		    member->afc_nego[afi][safi]) {
			update_group_adjust_peer(peer_af_find(member, afi, safi));
			bgp_default_originate(member, afi, safi, true);
			bgp_announce_route(member, afi, safi, false);
		}
	}

	return 0;
}

/* 设置对端 BGP 端口（默认 179），并置 PEER_FLAG_PORT 标志。 */
void peer_port_set(struct peer *peer, uint16_t port)
{
	peer->port = port;
	peer_flag_set(peer, PEER_FLAG_PORT);
}

/* 取消自定义端口，恢复默认端口 BGP_PORT_DEFAULT(179)。 */
void peer_port_unset(struct peer *peer)
{
	peer->port = BGP_PORT_DEFAULT;
	peer_flag_unset(peer, PEER_FLAG_PORT);
}

/* Set the TCP-MSS value in the peer structure,
 * This gets applied only after connection reset
 * So this value will be used in bgp_connect.
 */
/*
 * 设置 TCP-MSS（neighbor x tcp-mss）。仅在下次建连（bgp_connect）时生效。
 * 对 peer-group 成员下发（跳过已被 override 的成员）。
 */
void peer_tcp_mss_set(struct peer *peer, uint32_t tcp_mss)
{
	struct peer *member;
	struct listnode *node, *nnode;

	peer_flag_set(peer, PEER_FLAG_TCP_MSS);
	peer->tcp_mss = tcp_mss;

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		bgp_tcp_mss_set(peer);
		return;
	}

	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_TCP_MSS))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_TCP_MSS);
		PEER_ATTR_INHERIT(member, peer->group, tcp_mss);
		bgp_tcp_mss_set(member);
	}
}

/* Reset the TCP-MSS value in the peer structure,
 * This gets applied only after connection reset
 * So this value will be used in bgp_connect.
 */
/* 取消 TCP-MSS：成员继承组值，否则清零并恢复默认。 */
void peer_tcp_mss_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_TCP_MSS);
		PEER_ATTR_INHERIT(peer, peer->group, tcp_mss);
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_TCP_MSS);
		peer->tcp_mss = 0;
	}

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		bgp_tcp_mss_set(peer);
		return;
	}

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_TCP_MSS))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_TCP_MSS);
		member->tcp_mss = 0;
		bgp_tcp_mss_set(member);
	}
}

/*
 * Helper function that is called after the name of the policy
 * being used by a peer has changed (AF specific). Automatically
 * initiates inbound or outbound processing as needed.
 */
/*
 * 策略（route-map/prefix-list/distribute-list 等）变更后的统一善后处理（按地址族）。
 * 参数：outbound 非 0 表示出方向策略变化，0 表示入方向。
 * 出方向：调整 update-group 并重发路由（announce）。
 * 入方向：优先用本地 soft-reconfig，若不支持则发 Route-Refresh 请求对端重发。
 */
void peer_on_policy_change(struct peer *peer, afi_t afi, safi_t safi,
			   int outbound)
{
	if (outbound) {
		update_group_adjust_peer(peer_af_find(peer, afi, safi));
		if (peer_established(peer->connection))
			bgp_announce_route(peer, afi, safi, false);
	} else {
		if (!peer_established(peer->connection))
			return;

		if (bgp_soft_reconfig_in(peer, afi, safi))
			return;

		if (CHECK_FLAG(peer->cap, PEER_CAP_REFRESH_RCV))
			bgp_route_refresh_send(peer, afi, safi, 0, 0, 0,
					       BGP_ROUTE_REFRESH_NORMAL);
	}
}


/* neighbor weight. */
/*
 * 设置入方向权重（按 AFI/SAFI）。weight 为本地属性，影响 best-path 选路。
 * 变化时调用 peer_on_policy_change(...,0) 触发入方向重处理。
 */
int peer_weight_set(struct peer *peer, afi_t afi, safi_t safi, uint16_t weight)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Set flag and configuration on peer. */
	peer_af_flag_set(peer, afi, safi, PEER_FLAG_WEIGHT);
	if (peer->weight[afi][safi] != weight) {
		peer->weight[afi][safi] = weight;
		peer_on_policy_change(peer, afi, safi, 0);
	}

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_WEIGHT))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->af_flags[afi][safi], PEER_FLAG_WEIGHT);
		if (member->weight[afi][safi] != weight) {
			member->weight[afi][safi] = weight;
			peer_on_policy_change(member, afi, safi, 0);
		}
	}

	return 0;
}

/* 取消权重配置：成员继承组值，否则清 0 并触发入方向重处理。 */
int peer_weight_unset(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (!CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_WEIGHT))
		return 0;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_af_flag_inherit(peer, afi, safi, PEER_FLAG_WEIGHT);
		PEER_ATTR_INHERIT(peer, peer->group, weight[afi][safi]);

		peer_on_policy_change(peer, afi, safi, 0);
		return 0;
	}

	/* Remove flag and configuration from peer. */
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_WEIGHT);
	peer->weight[afi][safi] = 0;
	peer_on_policy_change(peer, afi, safi, 0);

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_WEIGHT))
			continue;

		/* Skip peers where flag is already disabled. */
		if (!CHECK_FLAG(member->af_flags[afi][safi], PEER_FLAG_WEIGHT))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->af_flags[afi][safi], PEER_FLAG_WEIGHT);
		member->weight[afi][safi] = 0;
		peer_on_policy_change(member, afi, safi, 0);
	}

	return 0;
}

/*
 * 设置 KEEPALIVE/HOLDTIME 定时器（neighbor x timers <ka> <hold>）。
 * 参数校验：keepalive/holdtime 不得超过 UINT16_MAX；holdtime 为 0(禁用)或 >=3。
 * 关键：keepalive 取 min(给定值, holdtime/3)，即 RFC 4271 建议值。
 * 说明：此处只更新值，实际定时器在 FSM 中按需启动。
 */
int peer_timers_set(struct peer *peer, uint32_t keepalive, uint32_t holdtime)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (keepalive > UINT16_MAX)
		return BGP_ERR_INVALID_VALUE;

	if (holdtime > UINT16_MAX)
		return BGP_ERR_INVALID_VALUE;

	if (holdtime < 3 && holdtime != 0)
		return BGP_ERR_INVALID_VALUE;

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_TIMER);
	peer->holdtime = holdtime;
	peer->keepalive = (keepalive < holdtime / 3 ? keepalive : holdtime / 3);

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_TIMER))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_TIMER);
		PEER_ATTR_INHERIT(member, peer->group, holdtime);
		PEER_ATTR_INHERIT(member, peer->group, keepalive);
	}

	return 0;
}

/* 取消定时器配置：成员继承组值，否则清零（后续走默认/协商逻辑）。 */
int peer_timers_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_TIMER);
		PEER_ATTR_INHERIT(peer, peer->group, holdtime);
		PEER_ATTR_INHERIT(peer, peer->group, keepalive);
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_TIMER);
		peer->holdtime = 0;
		peer->keepalive = 0;
	}

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_TIMER))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_TIMER);
		member->holdtime = 0;
		member->keepalive = 0;
	}

	return 0;
}

/*
 * 设置连接重试定时器（neighbor x timers connect <sec>），connect 为秒。
 * 校验：不得超过 UINT16_MAX。
 * 关键：仅未 Established 时，先 BGP_Stop 再 BGP_Start 以立即按新周期重连。
 */
int peer_timers_connect_set(struct peer *peer, uint32_t connect)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (connect > UINT16_MAX)
		return BGP_ERR_INVALID_VALUE;

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_TIMER_CONNECT);
	peer->connect = connect;
	peer->v_connect = connect;

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (!peer_established(peer->connection)) {
			if (peer_active(peer->connection) == BGP_PEER_ACTIVE)
				BGP_EVENT_ADD(peer->connection, BGP_Stop);
			BGP_EVENT_ADD(peer->connection, BGP_Start);
		}
		return 0;
	}
	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_TIMER_CONNECT))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_TIMER_CONNECT);
		member->connect = connect;
		member->v_connect = connect;

		if (!peer_established(member->connection)) {
			if (peer_active(member->connection) == BGP_PEER_ACTIVE)
				BGP_EVENT_ADD(member->connection, BGP_Stop);
			BGP_EVENT_ADD(member->connection, BGP_Start);
		}
	}

	return 0;
}

/* 取消连接重试定时器：成员继承组值，否则回落到 bgp 级 default_connect_retry。 */
int peer_timers_connect_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_TIMER_CONNECT);
		PEER_ATTR_INHERIT(peer, peer->group, connect);
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_TIMER_CONNECT);
		peer->connect = 0;
	}

	/* Set timer with fallback to default value. */
	if (peer->connect)
		peer->v_connect = peer->connect;
	else
		peer->v_connect = peer->bgp->default_connect_retry;

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (!peer_established(peer->connection)) {
			if (peer_active(peer->connection) == BGP_PEER_ACTIVE)
				BGP_EVENT_ADD(peer->connection, BGP_Stop);
			BGP_EVENT_ADD(peer->connection, BGP_Start);
		}
		return 0;
	}
	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_TIMER_CONNECT))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_TIMER_CONNECT);
		member->connect = 0;
		member->v_connect = peer->bgp->default_connect_retry;

		if (!peer_established(member->connection)) {
			if (peer_active(member->connection) == BGP_PEER_ACTIVE)
				BGP_EVENT_ADD(member->connection, BGP_Stop);
			BGP_EVENT_ADD(member->connection, BGP_Start);
		}
	}

	return 0;
}

/*
 * 设置路由通告间隔 routeadv（neighbor x advertisement-interval <sec>，上限 600）。
 * 作用：iBGP 抑制同组内频繁宣告，降低收敛时的 UPDATE 风暴。
 * 变化后调整 update-group 并向已建立对端重发全部路由。
 */
int peer_advertise_interval_set(struct peer *peer, uint32_t routeadv)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (routeadv > 600)
		return BGP_ERR_INVALID_VALUE;

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_ROUTEADV);
	peer->routeadv = routeadv;
	peer->v_routeadv = routeadv;

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Update peer route announcements. */
		update_group_adjust_peer_afs(peer);
		if (peer_established(peer->connection))
			bgp_announce_route_all(peer);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_ROUTEADV))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_ROUTEADV);
		member->routeadv = routeadv;
		member->v_routeadv = routeadv;

		/* Update peer route announcements. */
		update_group_adjust_peer_afs(member);
		if (peer_established(member->connection))
			bgp_announce_route_all(member);
	}

	return 0;
}

/* 取消通告间隔：成员继承组值，否则按 iBGP/eBGP 取各自默认值。 */
int peer_advertise_interval_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_ROUTEADV);
		PEER_ATTR_INHERIT(peer, peer->group, routeadv);
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_ROUTEADV);
		peer->routeadv = 0;
	}

	/* Set timer with fallback to default value. */
	if (peer->routeadv)
		peer->v_routeadv = peer->routeadv;
	else
		peer->v_routeadv = (peer->sort == BGP_PEER_IBGP)
					   ? BGP_DEFAULT_IBGP_ROUTEADV
					   : BGP_DEFAULT_EBGP_ROUTEADV;

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Update peer route announcements. */
		update_group_adjust_peer_afs(peer);
		if (peer_established(peer->connection))
			bgp_announce_route_all(peer);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_ROUTEADV))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_ROUTEADV);
		member->routeadv = 0;
		member->v_routeadv = (member->sort == BGP_PEER_IBGP)
					     ? BGP_DEFAULT_IBGP_ROUTEADV
					     : BGP_DEFAULT_EBGP_ROUTEADV;

		/* Update peer route announcements. */
		update_group_adjust_peer_afs(member);
		if (peer_established(member->connection))
			bgp_announce_route_all(member);
	}

	return 0;
}

/* set the peers RFC 4271 DelayOpen session attribute flag and DelayOpenTimer
 * interval
 */
/*
 * 设置 RFC 4271 DelayOpen 定时器（neighbor x timers delayopen <sec>）。
 * 作用：被动方在 DelayOpenTimer 到期前不主动发 OPEN，延缓会话建立。
 */
int peer_timers_delayopen_set(struct peer *peer, uint32_t delayopen)
{
	struct peer *member;
	struct listnode *node;

	/* Set peers session attribute flag and timer interval. */
	peer_flag_set(peer, PEER_FLAG_TIMER_DELAYOPEN);
	peer->delayopen = delayopen;
	peer->v_delayopen = delayopen;

	/* Skip group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/* Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS_RO(peer->group->peer, node, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override,
			       PEER_FLAG_TIMER_DELAYOPEN))
			continue;

		/* Set session attribute flag and timer intervals on peer-group
		 * member.
		 */
		SET_FLAG(member->flags, PEER_FLAG_TIMER_DELAYOPEN);
		member->delayopen = delayopen;
		member->v_delayopen = delayopen;
	}

	return 0;
}

/* unset the peers RFC 4271 DelayOpen session attribute flag and reset the
 * DelayOpenTimer interval to the default value.
 */
/* 取消 DelayOpen 定时器，恢复默认值并将运行值清零。 */
int peer_timers_delayopen_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_TIMER_DELAYOPEN);
		PEER_ATTR_INHERIT(peer, peer->group, delayopen);
	} else {
		/* Otherwise remove session attribute flag and set timer
		 * interval to default value.
		 */
		peer_flag_unset(peer, PEER_FLAG_TIMER_DELAYOPEN);
		peer->delayopen = peer->bgp->default_delayopen;
	}

	/* Set timer value to zero */
	peer->v_delayopen = 0;

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/* Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS_RO(peer->group->peer, node, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override,
			       PEER_FLAG_TIMER_DELAYOPEN))
			continue;

		/* Remove session attribute flag, reset the timer interval to
		 * the default value and set the timer value to zero.
		 */
		UNSET_FLAG(member->flags, PEER_FLAG_TIMER_DELAYOPEN);
		member->delayopen = peer->bgp->default_delayopen;
		member->v_delayopen = 0;
	}

	return 0;
}

/* neighbor interface */
/* 设置未编号(unnumbered)邻居的接口名（neighbor IFNAME interface remote-as ...）。 */
void peer_interface_set(struct peer *peer, const char *str)
{
	XFREE(MTYPE_BGP_PEER_IFNAME, peer->ifname);
	peer->ifname = XSTRDUP(MTYPE_BGP_PEER_IFNAME, str);
}

/* 清除未编号邻居接口名。 */
void peer_interface_unset(struct peer *peer)
{
	XFREE(MTYPE_BGP_PEER_IFNAME, peer->ifname);
}

/* Allow-as in.  */
/*
 * 配置 allowas-in（允许 AS_PATH 中出现本 AS 号，即放宽 AS 环路检查）。
 * 参数：afi/safi 地址族；allow_num 允许的出现次数(1..10)；origin 为 1 表示 "origin" 变体(允许来自始发)。
 * 校验：非 origin 时 allow_num 必须 1..10。
 * 说明：同时清零/设置 allowas_in[...] 与相应标志位，并触发出方向重处理。
 */
int peer_allowas_in_set(struct peer *peer, afi_t afi, safi_t safi,
			int allow_num, int origin)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (!origin && (allow_num < 1 || allow_num > 10))
		return BGP_ERR_INVALID_VALUE;

	/* Set flag and configuration on peer. */
	peer_af_flag_set(peer, afi, safi, PEER_FLAG_ALLOWAS_IN);
	if (origin) {
		if (peer->allowas_in[afi][safi] != 0
		    || !CHECK_FLAG(peer->af_flags[afi][safi],
				   PEER_FLAG_ALLOWAS_IN_ORIGIN)) {
			peer_af_flag_set(peer, afi, safi,
					 PEER_FLAG_ALLOWAS_IN_ORIGIN);
			peer->allowas_in[afi][safi] = 0;
			peer_on_policy_change(peer, afi, safi, 0);
		}
	} else {
		if (peer->allowas_in[afi][safi] != allow_num
		    || CHECK_FLAG(peer->af_flags[afi][safi],
				  PEER_FLAG_ALLOWAS_IN_ORIGIN)) {

			peer_af_flag_unset(peer, afi, safi,
					   PEER_FLAG_ALLOWAS_IN_ORIGIN);
			peer->allowas_in[afi][safi] = allow_num;
			peer_on_policy_change(peer, afi, safi, 0);
		}
	}

	/* Skip peer-group mechanics for regular peers. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Set flag and configuration on all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_ALLOWAS_IN))
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->af_flags[afi][safi], PEER_FLAG_ALLOWAS_IN);
		if (origin) {
			if (member->allowas_in[afi][safi] != 0
			    || !CHECK_FLAG(member->af_flags[afi][safi],
					   PEER_FLAG_ALLOWAS_IN_ORIGIN)) {
				SET_FLAG(member->af_flags[afi][safi],
					 PEER_FLAG_ALLOWAS_IN_ORIGIN);
				member->allowas_in[afi][safi] = 0;
				peer_on_policy_change(member, afi, safi, 0);
			}
		} else {
			if (member->allowas_in[afi][safi] != allow_num
			    || CHECK_FLAG(member->af_flags[afi][safi],
					  PEER_FLAG_ALLOWAS_IN_ORIGIN)) {
				UNSET_FLAG(member->af_flags[afi][safi],
					   PEER_FLAG_ALLOWAS_IN_ORIGIN);
				member->allowas_in[afi][safi] = allow_num;
				peer_on_policy_change(member, afi, safi, 0);
			}
		}
	}

	return 0;
}

/* 取消 allowas-in：成员继承组值，否则清除标志与计数并触发出方向重处理。 */
int peer_allowas_in_unset(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Skip peer if flag is already disabled. */
	if (!CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_ALLOWAS_IN))
		return 0;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_af_flag_inherit(peer, afi, safi, PEER_FLAG_ALLOWAS_IN);
		peer_af_flag_inherit(peer, afi, safi,
				     PEER_FLAG_ALLOWAS_IN_ORIGIN);
		PEER_ATTR_INHERIT(peer, peer->group, allowas_in[afi][safi]);
		peer_on_policy_change(peer, afi, safi, 0);

		return 0;
	}

	/* Remove flag and configuration from peer. */
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_ALLOWAS_IN);
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_ALLOWAS_IN_ORIGIN);
	peer->allowas_in[afi][safi] = 0;
	peer_on_policy_change(peer, afi, safi, 0);

	/* Skip peer-group mechanics if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Remove flags and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_ALLOWAS_IN))
			continue;

		/* Remove flags and configuration on peer-group member. */
		UNSET_FLAG(member->af_flags[afi][safi], PEER_FLAG_ALLOWAS_IN);
		UNSET_FLAG(member->af_flags[afi][safi],
			   PEER_FLAG_ALLOWAS_IN_ORIGIN);
		member->allowas_in[afi][safi] = 0;
		peer_on_policy_change(member, afi, safi, 0);
	}

	return 0;
}

/*
 * 配置 local-as（neighbor x local-as <as> [no-prepend] [replace-as] [dual-as]）。
 * 参数：as 本地 AS 号；no_prepend 不向 AS_PATH 前插 local-as；replace_as 前插 local-as 时替换真实 AS；
 *       dual_as 同时接受真实 AS 与 local-as 的会话（RFC 7705 思科风格）；as_str 文本形式。
 * 返回：BGP_ERR_CANNOT_HAVE_LOCAL_AS_SAME_AS（与进程 AS 相同则无意义）。
 * 关键：local-as 影响 AS_PATH 生成与 peer_sort(排序/eBGP判定)，故末尾调用 peer_sort(peer) 重算；
 *       NO_PREPEND/REPLACE_AS/DUAL_AS 三个子标志用 peer_flag_modify 统一维护。
 * 副作用：对 peer-group 成员同步设置（跳过被 override 或已相同的成员）。
 */
int peer_local_as_set(struct peer *peer, as_t as, bool no_prepend,
		      bool replace_as, bool dual_as, const char *as_str)
{
	bool old_no_prepend, old_replace_as, old_dual_as;
	struct bgp *bgp = peer->bgp;
	struct peer *member;
	struct listnode *node, *nnode;
	bool same_as_str = false;

	if (bgp->as == as)
		return BGP_ERR_CANNOT_HAVE_LOCAL_AS_SAME_AS;

	/* Save previous flag states. */
	old_no_prepend =
		!!CHECK_FLAG(peer->flags, PEER_FLAG_LOCAL_AS_NO_PREPEND);
	old_replace_as =
		!!CHECK_FLAG(peer->flags, PEER_FLAG_LOCAL_AS_REPLACE_AS);
	old_dual_as = !!CHECK_FLAG(peer->flags, PEER_FLAG_DUAL_AS);

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_LOCAL_AS);
	peer_flag_modify(peer, PEER_FLAG_LOCAL_AS_NO_PREPEND, no_prepend);
	peer_flag_modify(peer, PEER_FLAG_LOCAL_AS_REPLACE_AS, replace_as);
	peer_flag_modify(peer, PEER_FLAG_DUAL_AS, dual_as);

	same_as_str = as_str && peer->change_local_as_pretty &&
		      !strcmp(as_str, peer->change_local_as_pretty);

	if (peer->change_local_as == as && old_no_prepend == no_prepend &&
	    old_replace_as == replace_as && old_dual_as == dual_as && same_as_str)
		return 0;
	peer->change_local_as = as;
	if (as_str) {
		if (peer->change_local_as_pretty)
			XFREE(MTYPE_BGP_NAME, peer->change_local_as_pretty);
		peer->change_local_as_pretty = XSTRDUP(MTYPE_BGP_NAME, as_str);
	}

	(void)peer_sort(peer);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
		return 0;

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_LOCAL_AS))
			continue;

		/* Skip peers with the same configuration. */
		old_no_prepend = CHECK_FLAG(member->flags,
					    PEER_FLAG_LOCAL_AS_NO_PREPEND);
		old_replace_as = CHECK_FLAG(member->flags,
					    PEER_FLAG_LOCAL_AS_REPLACE_AS);
		old_dual_as = !!CHECK_FLAG(member->flags, PEER_FLAG_DUAL_AS);
		if (member->change_local_as == as &&
		    CHECK_FLAG(member->flags, PEER_FLAG_LOCAL_AS) &&
		    old_no_prepend == no_prepend &&
		    old_replace_as == replace_as && old_dual_as == dual_as)
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_LOCAL_AS);
		COND_FLAG(member->flags, PEER_FLAG_LOCAL_AS_NO_PREPEND,
			  no_prepend);
		COND_FLAG(member->flags, PEER_FLAG_LOCAL_AS_REPLACE_AS,
			  replace_as);
		COND_FLAG(member->flags, PEER_FLAG_DUAL_AS, dual_as);
		member->change_local_as = as;
		if (as_str) {
			if (member->change_local_as_pretty)
				XFREE(MTYPE_BGP_NAME, member->change_local_as_pretty);
			member->change_local_as_pretty = XSTRDUP(MTYPE_BGP_NAME,
								 as_str);
		}
	}

	return 0;
}

/*
 * 取消 local-as：成员继承组值，否则清除 LOCAL_AS 及相关子标志并清 AS 号。
 * 副作用：普通对端重置会话（LOCAL_AS_CHANGE），peer-group 则逐个成员处理。
 */
int peer_local_as_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (!CHECK_FLAG(peer->flags, PEER_FLAG_LOCAL_AS))
		return 0;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_LOCAL_AS);
		peer_flag_inherit(peer, PEER_FLAG_LOCAL_AS_NO_PREPEND);
		peer_flag_inherit(peer, PEER_FLAG_LOCAL_AS_REPLACE_AS);
		peer_flag_inherit(peer, PEER_FLAG_DUAL_AS);
		PEER_ATTR_INHERIT(peer, peer->group, change_local_as);
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_LOCAL_AS);
		peer_flag_unset(peer, PEER_FLAG_LOCAL_AS_NO_PREPEND);
		peer_flag_unset(peer, PEER_FLAG_LOCAL_AS_REPLACE_AS);
		peer_flag_unset(peer, PEER_FLAG_DUAL_AS);
		peer->change_local_as = 0;
		XFREE(MTYPE_BGP_NAME, peer->change_local_as_pretty);
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_set_last_reset(peer, PEER_DOWN_LOCAL_AS_CHANGE);
		/* Send notification or stop peer depending on state. */
		if (!peer_notify_config_change(peer->connection))
			BGP_EVENT_ADD(peer->connection, BGP_Stop);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_LOCAL_AS))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_LOCAL_AS);
		UNSET_FLAG(member->flags, PEER_FLAG_LOCAL_AS_NO_PREPEND);
		UNSET_FLAG(member->flags, PEER_FLAG_LOCAL_AS_REPLACE_AS);
		UNSET_FLAG(member->flags, PEER_FLAG_DUAL_AS);
		member->change_local_as = 0;
		XFREE(MTYPE_BGP_NAME, member->change_local_as_pretty);
		peer_set_last_reset(member, PEER_DOWN_LOCAL_AS_CHANGE);

		/* Send notification or stop peer depending on state. */
		if (!peer_notify_config_change(member->connection))
			bgp_session_reset(member);
	}

	return 0;
}

/* Set password for authenticating with the peer. */
/*
 * 设置 TCP MD5 认证密码（neighbor x password <pw>）。
 * 参数：password 明文密码，长度须在 PEER_PASSWORD_MINLEN..MAXLEN。
 * 返回：BGP_SUCCESS；BGP_ERR_INVALID_VALUE（长度非法）；BGP_ERR_TCPSIG_FAILED（内核 TCP-MD5 安装失败）。
 * 关键：对普通对端调用 bgp_md5_set 把密码装到 socket；对 peer-group 还会把密码应用到
 *       listen_range 的通配前缀（动态邻居）。
 */
int peer_password_set(struct peer *peer, const char *password)
{
	struct peer *member;
	struct listnode *node, *nnode;
	int len = password ? strlen(password) : 0;
	int ret = BGP_SUCCESS;

	if ((len < PEER_PASSWORD_MINLEN) || (len > PEER_PASSWORD_MAXLEN))
		return BGP_ERR_INVALID_VALUE;

	/* Set flag and configuration on peer. */
	peer_flag_set(peer, PEER_FLAG_PASSWORD);
	if (peer->password && strcmp(peer->password, password) == 0)
		return 0;
	XFREE(MTYPE_PEER_PASSWORD, peer->password);
	peer->password = XSTRDUP(MTYPE_PEER_PASSWORD, password);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		peer_set_last_reset(peer, PEER_DOWN_PASSWORD_CHANGE);
		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);

		/*
		 * Attempt to install password on socket and skip peer-group
		 * mechanics.
		 */
		if (BGP_CONNECTION_SU_UNSPEC(peer->connection))
			return BGP_SUCCESS;
		return (bgp_md5_set(peer->connection) >= 0)
			       ? BGP_SUCCESS
			       : BGP_ERR_TCPSIG_FAILED;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_PASSWORD))
			continue;

		/* Skip peers with the same password. */
		if (member->password && strcmp(member->password, password) == 0)
			continue;

		/* Set flag and configuration on peer-group member. */
		SET_FLAG(member->flags, PEER_FLAG_PASSWORD);
		if (member->password)
			XFREE(MTYPE_PEER_PASSWORD, member->password);
		member->password = XSTRDUP(MTYPE_PEER_PASSWORD, password);

		peer_set_last_reset(member, PEER_DOWN_PASSWORD_CHANGE);
		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(member->connection))
			bgp_session_reset(member);

		/* Attempt to install password on socket. */
		if (!BGP_CONNECTION_SU_UNSPEC(member->connection) &&
		    bgp_md5_set(member->connection) < 0)
			ret = BGP_ERR_TCPSIG_FAILED;
	}

	/* Set flag and configuration on all peer-group listen ranges */
	struct listnode *ln;
	struct prefix *lr;

	for (ALL_LIST_ELEMENTS_RO(peer->group->listen_range[AFI_IP], ln, lr))
		bgp_md5_set_prefix(peer->bgp, lr, password);
	for (ALL_LIST_ELEMENTS_RO(peer->group->listen_range[AFI_IP6], ln, lr))
		bgp_md5_set_prefix(peer->bgp, lr, password);

	return ret;
}

/* 取消 TCP MD5 密码：成员继承组值，否则清除标志并 bgp_md5_unset 卸载；同时清 listen_range。 */
int peer_password_unset(struct peer *peer)
{
	struct peer *member;
	struct listnode *node, *nnode;

	if (!CHECK_FLAG(peer->flags, PEER_FLAG_PASSWORD))
		return 0;

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_flag_inherit(peer, PEER_FLAG_PASSWORD);
		PEER_STR_ATTR_INHERIT(peer, peer->group, password,
				      MTYPE_PEER_PASSWORD);
	} else {
		/* Otherwise remove flag and configuration from peer. */
		peer_flag_unset(peer, PEER_FLAG_PASSWORD);
		XFREE(MTYPE_PEER_PASSWORD, peer->password);
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(peer->connection))
			bgp_session_reset(peer);

		/* Attempt to uninstall password on socket. */
		if (!BGP_CONNECTION_SU_UNSPEC(peer->connection))
			bgp_md5_unset(peer->connection);
		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->flags_override, PEER_FLAG_PASSWORD))
			continue;

		/* Remove flag and configuration on peer-group member. */
		UNSET_FLAG(member->flags, PEER_FLAG_PASSWORD);
		XFREE(MTYPE_PEER_PASSWORD, member->password);

		/* Send notification or reset peer depending on state. */
		if (!peer_notify_config_change(member->connection))
			bgp_session_reset(member);

		/* Attempt to uninstall password on socket. */
		if (!BGP_CONNECTION_SU_UNSPEC(member->connection))
			bgp_md5_unset(member->connection);
	}

	/* Set flag and configuration on all peer-group listen ranges */
	struct listnode *ln;
	struct prefix *lr;

	for (ALL_LIST_ELEMENTS_RO(peer->group->listen_range[AFI_IP], ln, lr))
		bgp_md5_unset_prefix(peer->bgp, lr);
	for (ALL_LIST_ELEMENTS_RO(peer->group->listen_range[AFI_IP6], ln, lr))
		bgp_md5_unset_prefix(peer->bgp, lr);

	return 0;
}


/* Set distribute list to the peer. */
/*
 * 设置 distribute-list（标准 ACL 形式的前缀过滤，neighbor x distribute-list <name> in|out）。
 * 参数：afi/safi；direct 方向（FILTER_IN/FILTER_OUT）；name ACL 名。
 * 返回：BGP_ERR_INVALID_VALUE（方向非法）；BGP_ERR_PEER_FILTER_CONFLICT（同方向已有 prefix-list）。
 * 关键：同一方向 distribute-list 与 prefix-list 互斥。
 * 维护：置 filter_override[...][direct] 的 PEER_FT_DISTRIBUTE_LIST 位，标记本端显式覆盖。
 */
int peer_distribute_set(struct peer *peer, afi_t afi, safi_t safi, int direct,
			const char *name)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != FILTER_IN && direct != FILTER_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Set configuration on peer. */
	filter = &peer->filter[afi][safi];
	if (filter->plist[direct].name)
		return BGP_ERR_PEER_FILTER_CONFLICT;
	if (filter->dlist[direct].name)
		XFREE(MTYPE_BGP_FILTER_NAME, filter->dlist[direct].name);
	filter->dlist[direct].name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
	filter->dlist[direct].alist = access_list_lookup(afi, name);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Set override-flag and process peer route updates. */
		SET_FLAG(peer->filter_override[afi][safi][direct],
			 PEER_FT_DISTRIBUTE_LIST);
		peer_on_policy_change(peer, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set configuration on all peer-group members, un less they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_DISTRIBUTE_LIST))
			continue;

		/* Set configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->dlist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->dlist[direct].name);
		filter->dlist[direct].name =
			XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
		filter->dlist[direct].alist = access_list_lookup(afi, name);

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);
	}

	return 0;
}

/* 取消 distribute-list：无条件清 override 位；成员继承组配置，否则移除本地配置。 */
int peer_distribute_unset(struct peer *peer, afi_t afi, safi_t safi, int direct)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != FILTER_IN && direct != FILTER_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Unset override-flag unconditionally. */
	UNSET_FLAG(peer->filter_override[afi][safi][direct],
		   PEER_FT_DISTRIBUTE_LIST);

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      filter[afi][safi].dlist[direct].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  filter[afi][safi].dlist[direct].alist);
	} else {
		/* Otherwise remove configuration from peer. */
		filter = &peer->filter[afi][safi];
		if (filter->dlist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->dlist[direct].name);
		filter->dlist[direct].name = NULL;
		filter->dlist[direct].alist = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Process peer route updates. */
		peer_on_policy_change(peer, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_DISTRIBUTE_LIST))
			continue;

		/* Remove configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->dlist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->dlist[direct].name);
		filter->dlist[direct].name = NULL;
		filter->dlist[direct].alist = NULL;

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);
	}

	return 0;
}

/* Update distribute list. */
/*
 * ACL（distribute-list 引用的访问列表）内容变更后的刷新回调。
 * 作用：遍历所有 bgp 实例/peer/peer-group，把 filter->dlist[direct].alist 指针
 *       重新指向最新的 access_list（名为空则置 NULL），并通知 update-group 策略更新。
 * 触发时机：access-list 定义被增删改时由 ACL 模块回调。
 */
static void peer_distribute_update(struct access_list *access)
{
	afi_t afi;
	safi_t safi;
	int direct;
	struct listnode *mnode, *mnnode;
	struct listnode *node, *nnode;
	struct bgp *bgp;
	struct peer *peer;
	struct peer_group *group;
	struct bgp_filter *filter;

	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {
		if (access->name)
			update_group_policy_update(bgp,
						   BGP_POLICY_DISTRIBUTE_LIST,
						   access->name, true, 0);
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			FOREACH_AFI_SAFI (afi, safi) {
				filter = &peer->filter[afi][safi];

				for (direct = FILTER_IN; direct < FILTER_MAX;
				     direct++) {
					if (filter->dlist[direct].name)
						filter->dlist[direct]
							.alist = access_list_lookup(
							afi,
							filter->dlist[direct]
								.name);
					else
						filter->dlist[direct].alist =
							NULL;
				}
			}
		}
		for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group)) {
			FOREACH_AFI_SAFI (afi, safi) {
				filter = &group->conf->filter[afi][safi];

				for (direct = FILTER_IN; direct < FILTER_MAX;
				     direct++) {
					if (filter->dlist[direct].name)
						filter->dlist[direct]
							.alist = access_list_lookup(
							afi,
							filter->dlist[direct]
								.name);
					else
						filter->dlist[direct].alist =
							NULL;
				}
			}
		}
#ifdef ENABLE_BGP_VNC
		vnc_prefix_list_update(bgp);
#endif
	}
}

/* Set prefix list to the peer. */
/*
 * 设置 prefix-list（neighbor x prefix-list <name> in|out）。
 * 参数：afi/safi；direct 方向；name 前缀列表名。
 * 返回：BGP_ERR_INVALID_VALUE；BGP_ERR_PEER_FILTER_CONFLICT（同方向已有 distribute-list）。
 * 维护：置 PEER_FT_PREFIX_LIST override 位；plist 指针指向已解析的 prefix_list。
 */
int peer_prefix_list_set(struct peer *peer, afi_t afi, safi_t safi, int direct,
			 const char *name)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != FILTER_IN && direct != FILTER_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Set configuration on peer. */
	filter = &peer->filter[afi][safi];
	if (filter->dlist[direct].name)
		return BGP_ERR_PEER_FILTER_CONFLICT;
	if (filter->plist[direct].name)
		XFREE(MTYPE_BGP_FILTER_NAME, filter->plist[direct].name);
	filter->plist[direct].name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
	filter->plist[direct].plist = prefix_list_lookup(afi, name);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Set override-flag and process peer route updates. */
		SET_FLAG(peer->filter_override[afi][safi][direct],
			 PEER_FT_PREFIX_LIST);
		peer_on_policy_change(peer, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_PREFIX_LIST))
			continue;

		/* Set configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->plist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->plist[direct].name);
		filter->plist[direct].name =
			XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
		filter->plist[direct].plist = prefix_list_lookup(afi, name);

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);
	}

	return 0;
}

/* 取消 prefix-list：清 override 位；成员继承组配置，否则移除本地配置。 */
int peer_prefix_list_unset(struct peer *peer, afi_t afi, safi_t safi,
			   int direct)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != FILTER_IN && direct != FILTER_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Unset override-flag unconditionally. */
	UNSET_FLAG(peer->filter_override[afi][safi][direct],
		   PEER_FT_PREFIX_LIST);

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      filter[afi][safi].plist[direct].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  filter[afi][safi].plist[direct].plist);
	} else {
		/* Otherwise remove configuration from peer. */
		filter = &peer->filter[afi][safi];
		if (filter->plist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->plist[direct].name);
		filter->plist[direct].name = NULL;
		filter->plist[direct].plist = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Process peer route updates. */
		peer_on_policy_change(peer, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_PREFIX_LIST))
			continue;

		/* Remove configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->plist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->plist[direct].name);
		filter->plist[direct].name = NULL;
		filter->plist[direct].plist = NULL;

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);
	}

	return 0;
}

/* Update prefix-list list. */
/*
 * prefix-list 内容变更后的刷新回调：重指向各 peer/peer-group 的 plist 指针，
 * 并对启用了 ORF（Outbound Route Filtering）的对端触发 soft-in 重新过滤。
 */
static void peer_prefix_list_update(struct prefix_list *plist)
{
	struct listnode *mnode, *mnnode;
	struct listnode *node, *nnode;
	struct bgp *bgp;
	struct peer *peer;
	struct peer_group *group;
	struct bgp_filter *filter;
	afi_t afi;
	safi_t safi;
	int direct;

	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {

		/*
		 * Update the prefix-list on update groups.
		 */
		update_group_policy_update(
			bgp, BGP_POLICY_PREFIX_LIST,
			plist ? prefix_list_name(plist) : NULL, true, 0);

		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			FOREACH_AFI_SAFI (afi, safi) {
				filter = &peer->filter[afi][safi];

				for (direct = FILTER_IN; direct < FILTER_MAX;
				     direct++) {
					if (filter->plist[direct].name)
						filter->plist[direct]
							.plist = prefix_list_lookup(
							afi,
							filter->plist[direct]
								.name);
					else
						filter->plist[direct].plist =
							NULL;
				}

				/* If we touch prefix-list, we need to process
				 * new updates. This is important for ORF to
				 * work correctly.
				 */
				if (CHECK_FLAG(peer->af_cap[afi][safi],
					       PEER_CAP_ORF_PREFIX_SM_ADV) &&
				    CHECK_FLAG(peer->af_cap[afi][safi],
					       PEER_CAP_ORF_PREFIX_RM_RCV))
					peer_clear_soft(
						peer, afi, safi,
						BGP_CLEAR_SOFT_IN_ORF_PREFIX);
			}
		}
		for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group)) {
			FOREACH_AFI_SAFI (afi, safi) {
				filter = &group->conf->filter[afi][safi];

				for (direct = FILTER_IN; direct < FILTER_MAX;
				     direct++) {
					if (filter->plist[direct].name)
						filter->plist[direct]
							.plist = prefix_list_lookup(
							afi,
							filter->plist[direct]
								.name);
					else
						filter->plist[direct].plist =
							NULL;
				}
			}
		}
	}
}

/*
 * 设置 filter-list（按 AS_PATH 的 AS 正则列表过滤，neighbor x filter-list <name> in|out）。
 * 参数：afi/safi；direct；name 是 AS-path access-list 名。
 * 维护：置 PEER_FT_FILTER_LIST override 位；aslist 指向解析后的列表。
 */
int peer_aslist_set(struct peer *peer, afi_t afi, safi_t safi, int direct,
		    const char *name)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != FILTER_IN && direct != FILTER_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Set configuration on peer. */
	filter = &peer->filter[afi][safi];
	if (filter->aslist[direct].name)
		XFREE(MTYPE_BGP_FILTER_NAME, filter->aslist[direct].name);
	filter->aslist[direct].name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
	filter->aslist[direct].aslist = as_list_lookup(name);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Set override-flag and process peer route updates. */
		SET_FLAG(peer->filter_override[afi][safi][direct],
			 PEER_FT_FILTER_LIST);
		peer_on_policy_change(peer, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_FILTER_LIST))
			continue;

		/* Set configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->aslist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->aslist[direct].name);
		filter->aslist[direct].name =
			XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
		filter->aslist[direct].aslist = as_list_lookup(name);

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);
	}

	return 0;
}

/* 取消 filter-list：清 override 位；成员继承组配置，否则移除本地配置。 */
int peer_aslist_unset(struct peer *peer, afi_t afi, safi_t safi, int direct)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != FILTER_IN && direct != FILTER_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Unset override-flag unconditionally. */
	UNSET_FLAG(peer->filter_override[afi][safi][direct],
		   PEER_FT_FILTER_LIST);

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      filter[afi][safi].aslist[direct].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  filter[afi][safi].aslist[direct].aslist);
	} else {
		/* Otherwise remove configuration from peer. */
		filter = &peer->filter[afi][safi];
		if (filter->aslist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->aslist[direct].name);
		filter->aslist[direct].name = NULL;
		filter->aslist[direct].aslist = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Process peer route updates. */
		peer_on_policy_change(peer, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_FILTER_LIST))
			continue;

		/* Remove configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->aslist[direct].name)
			XFREE(MTYPE_BGP_FILTER_NAME,
			      filter->aslist[direct].name);
		filter->aslist[direct].name = NULL;
		filter->aslist[direct].aslist = NULL;

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == FILTER_OUT) ? 1 : 0);
	}

	return 0;
}

/*
 * AS-path access-list 内容变更后的统一刷新：重指向各 peer/peer-group 的 aslist 指针，
 * 并对“入方向 filter-list 名称匹配该列表”的已建立对端触发策略重处理。
 */
static void peer_aslist_update(const char *aslist_name)
{
	afi_t afi;
	safi_t safi;
	int direct;
	struct listnode *mnode, *mnnode;
	struct listnode *node, *nnode;
	struct bgp *bgp;
	struct peer *peer;
	struct peer_group *group;
	struct bgp_filter *filter;

	/* Coverity: aslist_name is validated admin config, not user input */
	/* coverity[PW.NON_CONST_PRINTF_FORMAT_STRING] */
	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {
		update_group_policy_update(bgp, BGP_POLICY_FILTER_LIST,
					   aslist_name, true, 0);

		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			FOREACH_AFI_SAFI (afi, safi) {
				filter = &peer->filter[afi][safi];

				for (direct = FILTER_IN; direct < FILTER_MAX;
				     direct++) {
					if (filter->aslist[direct].name)
						filter->aslist[direct]
							.aslist = as_list_lookup(
							filter->aslist[direct]
								.name);
					else
						filter->aslist[direct].aslist =
							NULL;
			}

			/* Route re-eval for the peer */
			if (filter->aslist[FILTER_IN].name &&
			    strmatch(filter->aslist[FILTER_IN].name, aslist_name) &&
			    peer_established(peer->connection))
				peer_on_policy_change(peer, afi, safi, 0);
	}
		}
		for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group)) {
			FOREACH_AFI_SAFI (afi, safi) {
				filter = &group->conf->filter[afi][safi];

				for (direct = FILTER_IN; direct < FILTER_MAX;
				     direct++) {
					if (filter->aslist[direct].name)
						filter->aslist[direct]
							.aslist = as_list_lookup(
							filter->aslist[direct]
								.name);
					else
						filter->aslist[direct].aslist =
							NULL;
				}

				/* Trigger route re-eval for established group members */
				if (!filter->aslist[FILTER_IN].name)
					continue;

				if (!strmatch(filter->aslist[FILTER_IN].name, aslist_name))
					continue;
				/* Notify group members */
				struct peer *member;
				struct listnode *pnode, *pnnode;

				for (ALL_LIST_ELEMENTS(group->peer, pnode, pnnode, member)) {
					if (peer_established(member->connection))
						peer_on_policy_change(member, afi, safi, 0);
				}
			}
		}
	}
}

/* AS-path access-list 新增时的回调：刷新依赖并通知 route-map 依赖关系更新。 */
static void peer_aslist_add(char *aslist_name)
{
	peer_aslist_update(aslist_name);
	route_map_notify_dependencies(aslist_name, RMAP_EVENT_ASLIST_ADDED);
}

/* AS-path access-list 删除时的回调。 */
static void peer_aslist_del(const char *aslist_name)
{
	peer_aslist_update(aslist_name);
	route_map_notify_dependencies(aslist_name, RMAP_EVENT_ASLIST_DELETED);
}


/*
 * 设置 route-map（neighbor x route-map <name> in|out）。
 * 参数：afi/safi；direct 方向（RMAP_IN/RMAP_OUT）；name 名称；route_map 已解析指针。
 * 返回：BGP_ERR_INVALID_VALUE（方向非法）；重复同名则直接返回 0（幂等）。
 * 关键：route-map 有引用计数——替换前对旧 map 递减、对新 map 递增。
 * 维护：置 PEER_FT_ROUTE_MAP override 位。
 */
int peer_route_map_set(struct peer *peer, afi_t afi, safi_t safi, int direct,
		       const char *name, struct route_map *route_map)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;
	struct route_map *map = NULL;

	if (direct != RMAP_IN && direct != RMAP_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Set configuration on peer. */
	filter = &peer->filter[afi][safi];
	if (filter->map[direct].name) {
		/* If the neighbor is configured with the same route-map
		 * again then, ignore the duplicate configuration.
		 */
		if (strcmp(filter->map[direct].name, name) == 0)
			return 0;

		map = route_map_lookup_by_name(filter->map[direct].name);
		XFREE(MTYPE_BGP_FILTER_NAME, filter->map[direct].name);
	}
	route_map_counter_decrement(map);
	filter->map[direct].name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
	filter->map[direct].map = route_map;
	route_map_counter_increment(route_map);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Set override-flag and process peer route updates. */
		SET_FLAG(peer->filter_override[afi][safi][direct],
			 PEER_FT_ROUTE_MAP);
		peer_on_policy_change(peer, afi, safi,
				      (direct == RMAP_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		map = NULL;
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_ROUTE_MAP))
			continue;

		/* Set configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->map[direct].name) {
			map = route_map_lookup_by_name(filter->map[direct].name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->map[direct].name);
		}
		route_map_counter_decrement(map);
		filter->map[direct].name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
		filter->map[direct].map = route_map;
		route_map_counter_increment(route_map);

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == RMAP_OUT) ? 1 : 0);
	}
	return 0;
}

/* Unset route-map from the peer. */
/* 取消 route-map：清 override 位；成员继承组配置，否则移除并递减旧 map 引用计数。 */
int peer_route_map_unset(struct peer *peer, afi_t afi, safi_t safi, int direct)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	if (direct != RMAP_IN && direct != RMAP_OUT)
		return BGP_ERR_INVALID_VALUE;

	/* Unset override-flag unconditionally. */
	UNSET_FLAG(peer->filter_override[afi][safi][direct], PEER_FT_ROUTE_MAP);

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      filter[afi][safi].map[direct].name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  filter[afi][safi].map[direct].map);
	} else {
		struct route_map *map = NULL;

		/* Otherwise remove configuration from peer. */
		filter = &peer->filter[afi][safi];

		if (filter->map[direct].name) {
			map = route_map_lookup_by_name(filter->map[direct].name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->map[direct].name);
		}
		route_map_counter_decrement(map);
		filter->map[direct].name = NULL;
		filter->map[direct].map = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Process peer route updates. */
		peer_on_policy_change(peer, afi, safi,
				      (direct == RMAP_OUT) ? 1 : 0);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		struct route_map *map;

		map = NULL;

		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][direct],
			       PEER_FT_ROUTE_MAP))
			continue;

		/* Remove configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->map[direct].name) {
			map = route_map_lookup_by_name(filter->map[direct].name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->map[direct].name);
		}
		route_map_counter_decrement(map);
		filter->map[direct].name = NULL;
		filter->map[direct].map = NULL;

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi,
				      (direct == RMAP_OUT) ? 1 : 0);
	}

	return 0;
}

/* Set unsuppress-map to the peer. */
/*
 * 设置 unsuppress-map（对从本对端学到的、被 aggregate 抑制的明细路由做选择性重通告）。
 * 维护 usmap（单方向字段，override 用下标 0），route-map 引用计数配对增减。
 * 变化后按出方向（outbound=1）触发策略重处理。
 */
int peer_unsuppress_map_set(struct peer *peer, afi_t afi, safi_t safi,
			    const char *name, struct route_map *route_map)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	/* Set configuration on peer. */
	filter = &peer->filter[afi][safi];
	if (filter->usmap.name)
		XFREE(MTYPE_BGP_FILTER_NAME, filter->usmap.name);
	route_map_counter_decrement(filter->usmap.map);
	filter->usmap.name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
	filter->usmap.map = route_map;
	route_map_counter_increment(route_map);

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Set override-flag and process peer route updates. */
		SET_FLAG(peer->filter_override[afi][safi][0],
			 PEER_FT_UNSUPPRESS_MAP);
		peer_on_policy_change(peer, afi, safi, 1);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		struct route_map *map;

		map = NULL;

		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][0],
			       PEER_FT_UNSUPPRESS_MAP))
			continue;

		/* Set configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->usmap.name) {
			map = route_map_lookup_by_name(filter->usmap.name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->usmap.name);
		}
		route_map_counter_decrement(map);
		filter->usmap.name = XSTRDUP(MTYPE_BGP_FILTER_NAME, name);
		filter->usmap.map = route_map;
		route_map_counter_increment(route_map);

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi, 1);
	}

	return 0;
}

/* Unset route-map from the peer. */
/* 取消 unsuppress-map：清 override 位；成员继承组配置，否则移除并递减引用计数。 */
int peer_unsuppress_map_unset(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer *member;
	struct bgp_filter *filter;
	struct listnode *node, *nnode;

	/* Unset override-flag unconditionally. */
	UNSET_FLAG(peer->filter_override[afi][safi][0], PEER_FT_UNSUPPRESS_MAP);

	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		PEER_STR_ATTR_INHERIT(peer, peer->group,
				      filter[afi][safi].usmap.name,
				      MTYPE_BGP_FILTER_NAME);
		PEER_ATTR_INHERIT(peer, peer->group,
				  filter[afi][safi].usmap.map);
	} else {
		struct route_map *map = NULL;

		/* Otherwise remove configuration from peer. */
		filter = &peer->filter[afi][safi];
		if (filter->usmap.name) {
			map = route_map_lookup_by_name(filter->usmap.name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->usmap.name);
		}
		route_map_counter_decrement(map);
		filter->usmap.name = NULL;
		filter->usmap.map = NULL;
	}

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Process peer route updates. */
		peer_on_policy_change(peer, afi, safi, 1);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Remove configuration on all peer-group members, unless they are
	 * explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		struct route_map *map;

		map = NULL;

		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->filter_override[afi][safi][0],
			       PEER_FT_UNSUPPRESS_MAP))
			continue;

		/* Remove configuration on peer-group member. */
		filter = &member->filter[afi][safi];
		if (filter->usmap.name) {
			map = route_map_lookup_by_name(filter->usmap.name);
			XFREE(MTYPE_BGP_FILTER_NAME, filter->usmap.name);
		}
		route_map_counter_decrement(map);
		filter->usmap.name = NULL;
		filter->usmap.map = NULL;

		/* Process peer route updates. */
		peer_on_policy_change(member, afi, safi, 1);
	}

	return 0;
}

/*
 * 清除 maximum-prefix 溢出状态：取消重启定时器并把对端拉回 BGP_Start 尝试重连。
 * 返回 true 表示确有溢出被清除（供调用方判断），否则 false。
 */
static bool peer_maximum_prefix_clear_overflow(struct peer *peer)
{
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_PREFIX_OVERFLOW))
		return false;

	UNSET_FLAG(peer->sflags, PEER_STATUS_PREFIX_OVERFLOW);
	if (peer->connection->t_pmax_restart) {
		event_cancel(&peer->connection->t_pmax_restart);
		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"%pBP Maximum-prefix restart timer cancelled",
				peer);
	}
	BGP_EVENT_ADD(peer->connection, BGP_Start);
	return true;
}

/*
 * 设置 maximum-prefix（对端接收前缀数上限，超限告警/告警+断开/强制重启）。
 * 参数：max 上限；threshold 告警百分比；warning 仅告警不中断；restart 超限后自动重启等待秒；force 立即生效。
 * 关键：warning 与 force 映射到 PEER_FLAG_MAX_PREFIX_WARNING / _FORCE 两个 af 标志。
 * 副作用：已 Established 且有该族能力时立即复核是否超限；未建立则清除历史溢出状态。
 */
int peer_maximum_prefix_set(struct peer *peer, afi_t afi, safi_t safi,
			    uint32_t max, uint8_t threshold, int warning,
			    uint16_t restart, bool force)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Set flags and configuration on peer. */
	peer_af_flag_set(peer, afi, safi, PEER_FLAG_MAX_PREFIX);

	if (force)
		peer_af_flag_set(peer, afi, safi, PEER_FLAG_MAX_PREFIX_FORCE);
	else
		peer_af_flag_unset(peer, afi, safi, PEER_FLAG_MAX_PREFIX_FORCE);

	if (warning)
		peer_af_flag_set(peer, afi, safi, PEER_FLAG_MAX_PREFIX_WARNING);
	else
		peer_af_flag_unset(peer, afi, safi,
				   PEER_FLAG_MAX_PREFIX_WARNING);

	peer->pmax[afi][safi] = max;
	peer->pmax_threshold[afi][safi] = threshold;
	peer->pmax_restart[afi][safi] = restart;

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Re-check if peer violates maximum-prefix. */
		if ((peer_established(peer->connection)) &&
		    (peer->afc[afi][safi]))
			bgp_maximum_prefix_overflow(peer, afi, safi, 1);
		else if (!peer_established(peer->connection))
			peer_maximum_prefix_clear_overflow(peer);

		/* Skip peer-group mechanics for regular peers. */
		return 0;
	}

	/*
	 * Set flags and configuration on all peer-group members, unless they
	 * are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_MAX_PREFIX))
			continue;

		/* Set flag and configuration on peer-group member. */
		member->pmax[afi][safi] = max;
		member->pmax_threshold[afi][safi] = threshold;
		member->pmax_restart[afi][safi] = restart;

		if (force)
			SET_FLAG(member->af_flags[afi][safi],
				 PEER_FLAG_MAX_PREFIX_FORCE);
		else
			UNSET_FLAG(member->af_flags[afi][safi],
				   PEER_FLAG_MAX_PREFIX_FORCE);

		if (warning)
			SET_FLAG(member->af_flags[afi][safi],
				 PEER_FLAG_MAX_PREFIX_WARNING);
		else
			UNSET_FLAG(member->af_flags[afi][safi],
				   PEER_FLAG_MAX_PREFIX_WARNING);

		/* Re-check if peer violates maximum-prefix. */
		if ((peer_established(member->connection)) &&
		    (member->afc[afi][safi]))
			bgp_maximum_prefix_overflow(member, afi, safi, 1);
		else if (!peer_established(member->connection))
			peer_maximum_prefix_clear_overflow(member);
	}

	return 0;
}

/* 取消 maximum-prefix：成员继承组配置，否则清标志与三个数组项。 */
int peer_maximum_prefix_unset(struct peer *peer, afi_t afi, safi_t safi)
{
	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_af_flag_inherit(peer, afi, safi, PEER_FLAG_MAX_PREFIX);
		peer_af_flag_inherit(peer, afi, safi,
				     PEER_FLAG_MAX_PREFIX_FORCE);
		peer_af_flag_inherit(peer, afi, safi,
				     PEER_FLAG_MAX_PREFIX_WARNING);
		PEER_ATTR_INHERIT(peer, peer->group, pmax[afi][safi]);
		PEER_ATTR_INHERIT(peer, peer->group, pmax_threshold[afi][safi]);
		PEER_ATTR_INHERIT(peer, peer->group, pmax_restart[afi][safi]);

		/* Trigger peer FSM to form neighborship using updated config */
		peer_maximum_prefix_clear_overflow(peer);

		return 0;
	}

	/* Remove flags and configuration from peer. */
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_MAX_PREFIX);
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_MAX_PREFIX_FORCE);
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_MAX_PREFIX_WARNING);
	peer->pmax[afi][safi] = 0;
	peer->pmax_threshold[afi][safi] = 0;
	peer->pmax_restart[afi][safi] = 0;

	/*
	 * Remove flags and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		struct peer *member;
		struct listnode *node;

		for (ALL_LIST_ELEMENTS_RO(peer->group->peer, node, member)) {
			/* Skip peers with overridden configuration. */
			if (CHECK_FLAG(member->af_flags_override[afi][safi],
				       PEER_FLAG_MAX_PREFIX))
				continue;

			/* Remove flag and configuration on peer-group member.
			 */
			UNSET_FLAG(member->af_flags[afi][safi],
				   PEER_FLAG_MAX_PREFIX);
			UNSET_FLAG(member->af_flags[afi][safi],
				   PEER_FLAG_MAX_PREFIX_FORCE);
			UNSET_FLAG(member->af_flags[afi][safi],
				   PEER_FLAG_MAX_PREFIX_WARNING);
			member->pmax[afi][safi] = 0;
			member->pmax_threshold[afi][safi] = 0;
			member->pmax_restart[afi][safi] = 0;

			peer_maximum_prefix_clear_overflow(member);
		}
	} else {
		peer_maximum_prefix_clear_overflow(peer);
	}

	return 0;
}

/*
 * maximum-prefix-out 变更后的辅助：把该 peer 移到正确的 update-group 并重发本族路由。
 */
void peer_maximum_prefix_out_refresh_routes(struct peer *peer, afi_t afi,
					    safi_t safi)
{
	update_group_adjust_peer(peer_af_find(peer, afi, safi));

	if (peer_established(peer->connection))
		bgp_announce_route(peer, afi, safi, false);
}

/*
 * 设置 maximum-prefix-out（对本对端“通告出去”的前缀数上限，仅作告警/抑制用）。
 * 设 PEER_FLAG_MAX_PREFIX_OUT 标志并刷新路由；peer-group 场景下发成员。
 */
int peer_maximum_prefix_out_set(struct peer *peer, afi_t afi, safi_t safi,
				uint32_t max)
{
	struct peer *member;
	struct listnode *node, *nnode;

	/* Set flag on peer and peer-group member if any */
	peer_af_flag_set(peer, afi, safi, PEER_FLAG_MAX_PREFIX_OUT);
	/* Set configuration on peer. */
	peer->pmax_out[afi][safi] = max;

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Skip peer-group mechanics for regular peers. */
		peer_maximum_prefix_out_refresh_routes(peer, afi, safi);
		return 0;
	}

	/*
	 * Set flag and configuration on all peer-group members, unless they
	 * are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS(peer->group->peer, node, nnode, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_MAX_PREFIX_OUT))
			continue;

		/* Set configuration on peer-group member. */
		member->pmax_out[afi][safi] = max;

		peer_maximum_prefix_out_refresh_routes(member, afi, safi);
	}
	return 0;
}

/* 取消 maximum-prefix-out：成员继承组配置，否则清标志与计数并刷新路由。 */
int peer_maximum_prefix_out_unset(struct peer *peer, afi_t afi, safi_t safi)
{
	struct peer *member;
	struct listnode *node;
	/* Inherit configuration from peer-group if peer is member. */
	if (peer_group_active(peer)) {
		peer_af_flag_inherit(peer, afi, safi, PEER_FLAG_MAX_PREFIX_OUT);
		PEER_ATTR_INHERIT(peer, peer->group, pmax_out[afi][safi]);

		peer_maximum_prefix_out_refresh_routes(peer, afi, safi);
		return 0;
	}

	/* Remove flag and configuration from peer. */
	peer_af_flag_unset(peer, afi, safi, PEER_FLAG_MAX_PREFIX_OUT);
	peer->pmax_out[afi][safi] = 0;

	/* Check if handling a regular peer. */
	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Skip peer-group mechanics for regular peers. */
		peer_maximum_prefix_out_refresh_routes(peer, afi, safi);
		return 0;
	}

	/*
	 * Remove flag and configuration from all peer-group members, unless
	 * they are explicitly overriding peer-group configuration.
	 */
	for (ALL_LIST_ELEMENTS_RO(peer->group->peer, node, member)) {
		/* Skip peers with overridden configuration. */
		if (CHECK_FLAG(member->af_flags_override[afi][safi],
			       PEER_FLAG_MAX_PREFIX_OUT))
			continue;

		/* Remove flag and configuration on peer-group member.
		 */
		UNSET_FLAG(member->af_flags[afi][safi],
			   PEER_FLAG_MAX_PREFIX_OUT);
		member->pmax_out[afi][safi] = 0;

		peer_maximum_prefix_out_refresh_routes(member, afi, safi);
	}
	return 0;
}

/*
 * 判断对端（或其 peer-group 及全部成员）是否配置了 eBGP multihop（非默认 TTL 且非 iBGP）。
 * 返回 1 表示已配置，0 表示未配置。用于 ttl-security 与 multihop 的互斥检查。
 */
int is_ebgp_multihop_configured(struct peer *peer)
{
	struct peer_group *group;
	struct listnode *node, *nnode;
	struct peer *peer1;

	if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		group = peer->group;
		if ((peer_sort(peer) != BGP_PEER_IBGP)
		    && (group->conf->ttl != BGP_DEFAULT_TTL))
			return 1;

		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer1)) {
			if ((peer_sort(peer1) != BGP_PEER_IBGP)
			    && (peer1->ttl != BGP_DEFAULT_TTL))
				return 1;
		}
	} else {
		if ((peer_sort(peer) != BGP_PEER_IBGP)
		    && (peer->ttl != BGP_DEFAULT_TTL))
			return 1;
	}
	return 0;
}

/* Set # of hops between us and BGP peer. */
/*
 * 配置 GTSM / ttl-security hops（RFC 5082），通过设置 socket 的 min-TTL 防伪造。
 * 参数：gtsm_hops 允许的跳数。
 * 返回：0 成功；BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK（与 ebgp-multihop 冲突）。
 * 关键：首次启用（DISABLED→非0）且非 iBGP 时，会把 peer 的 TTL 设为 MAXTTL 以配合 GTSM；
 *       随后对已建立/待建立连接调用 sockopt_minttl(fd, MAXTTL+1-gtsm_hops)。
 * 注意：对 peer-group 会同时设置 group->conf 与所有成员；doppelganger 连接也要设。
 */
int peer_ttl_security_hops_set(struct peer *peer, int gtsm_hops)
{
	struct peer_group *group;
	struct peer *gpeer;
	struct listnode *node, *nnode;
	int ret;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s: set gtsm_hops to %d for %s", __func__,
			   gtsm_hops, peer->host);

	/* We cannot configure ttl-security hops when ebgp-multihop is already
	   set.  For non peer-groups, the check is simple.  For peer-groups,
	   it's
	   slightly messy, because we need to check both the peer-group
	   structure
	   and all peer-group members for any trace of ebgp-multihop
	   configuration
	   before actually applying the ttl-security rules.  Cisco really made a
	   mess of this configuration parameter, and OpenBGPD got it right.
	*/

	if ((peer->gtsm_hops == BGP_GTSM_HOPS_DISABLED)
	    && (peer->sort != BGP_PEER_IBGP)) {
		if (is_ebgp_multihop_configured(peer))
			return BGP_ERR_NO_EBGP_MULTIHOP_WITH_TTLHACK;

		if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
			peer->gtsm_hops = gtsm_hops;

			/* Calling ebgp multihop also resets the session.
			 * On restart, NHT will get setup correctly as will the
			 * min & max ttls on the socket. The return value is
			 * irrelevant.
			 */
			ret = peer_ebgp_multihop_set(peer, MAXTTL);

			if (ret != 0)
				return ret;
		} else {
			group = peer->group;
			group->conf->gtsm_hops = gtsm_hops;
			for (ALL_LIST_ELEMENTS(group->peer, node, nnode,
					       gpeer)) {
				gpeer->gtsm_hops = group->conf->gtsm_hops;

				/* Calling ebgp multihop also resets the
				 * session.
				 * On restart, NHT will get setup correctly as
				 * will the
				 * min & max ttls on the socket. The return
				 * value is
				 * irrelevant.
				 */
				peer_ebgp_multihop_set(gpeer, MAXTTL);
			}
		}
	} else {
		/* Post the first gtsm setup or if its ibgp, maxttl setting
		 * isn't
		 * necessary, just set the minttl.
		 */
		if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
			peer->gtsm_hops = gtsm_hops;

			if (peer->connection->fd >= 0)
				sockopt_minttl(peer->connection->su.sa.sa_family,
					       peer->connection->fd,
					       MAXTTL + 1 - gtsm_hops);
			if ((peer->connection->status < Established) &&
			    peer->doppelganger &&
			    (peer->doppelganger->connection->fd >= 0))
				sockopt_minttl(peer->connection->su.sa.sa_family,
					       peer->doppelganger->connection->fd,
					       MAXTTL + 1 - gtsm_hops);
		} else {
			group = peer->group;
			group->conf->gtsm_hops = gtsm_hops;
			for (ALL_LIST_ELEMENTS(group->peer, node, nnode,
					       gpeer)) {
				struct peer_connection *connection =
					gpeer->connection;
				gpeer->gtsm_hops = group->conf->gtsm_hops;

				/* Change setting of existing peer
				 *   established then change value (may break
				 * connectivity)
				 *   not established yet (teardown session and
				 * restart)
				 *   no session then do nothing (will get
				 * handled by next connection)
				 */
				if (connection->fd >= 0 &&
				    gpeer->gtsm_hops != BGP_GTSM_HOPS_DISABLED)
					sockopt_minttl(connection->su.sa.sa_family,
						       connection->fd,
						       MAXTTL + 1 -
							       gpeer->gtsm_hops);
				if ((connection->status < Established) &&
				    gpeer->doppelganger &&
				    (gpeer->doppelganger->connection->fd >= 0))
					sockopt_minttl(connection->su.sa.sa_family,
						       gpeer->doppelganger
							       ->connection->fd,
						       MAXTTL + 1 - gtsm_hops);
			}
		}
	}

	return 0;
}

/* 取消 GTSM：成员恢复组值，否则置 DISABLED；并把 socket min-TTL 复位为 0。 */
int peer_ttl_security_hops_unset(struct peer *peer)
{
	struct peer_group *group;
	struct listnode *node, *nnode;
	int ret = 0;

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s: set gtsm_hops to zero for %s", __func__,
			   peer->host);

	/* if a peer-group member, then reset to peer-group default rather than
	 * 0 */
	if (peer_group_active(peer))
		peer->gtsm_hops = peer->group->conf->gtsm_hops;
	else
		peer->gtsm_hops = BGP_GTSM_HOPS_DISABLED;

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		/* Invoking ebgp_multihop_set will set the TTL back to the
		 * original
		 * value as well as restting the NHT and such. The session is
		 * reset.
		 */
		if (peer->sort == BGP_PEER_EBGP)
			ret = peer_ebgp_multihop_unset(peer);
		else {
			if (peer->connection->fd >= 0)
				sockopt_minttl(peer->connection->su.sa.sa_family,
					       peer->connection->fd, 0);

			if ((peer->connection->status < Established) &&
			    peer->doppelganger &&
			    (peer->doppelganger->connection->fd >= 0))
				sockopt_minttl(peer->connection->su.sa.sa_family,
					       peer->doppelganger->connection->fd,
					       0);
		}
	} else {
		group = peer->group;
		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
			peer->gtsm_hops = BGP_GTSM_HOPS_DISABLED;
			if (peer->sort == BGP_PEER_EBGP)
				ret = peer_ebgp_multihop_unset(peer);
			else {
				if (peer->connection->fd >= 0)
					sockopt_minttl(peer->connection->su.sa
							       .sa_family,
						       peer->connection->fd, 0);

				if ((peer->connection->status < Established) &&
				    peer->doppelganger &&
				    (peer->doppelganger->connection->fd >= 0))
					sockopt_minttl(peer->connection->su.sa
							       .sa_family,
						       peer->doppelganger
							       ->connection->fd,
						       0);
			}
		}
	}

	return ret;
}

/* ========== 10. 会话清除（clear/soft-clear）与运行时长展示 ========== */
/*
 * 清零对端的收发报文统计计数器（open/update/keepalive/notify/refresh/dynamic-cap）。
 * 这些计数器是原子变量，供 IO 线程并发更新，这里用 relaxed 存储清零。
 */
static void peer_reset_message_stats(struct peer *peer)
{
	if (peer) {
		atomic_store_explicit(&peer->open_in, 0, memory_order_relaxed);
		atomic_store_explicit(&peer->open_out, 0, memory_order_relaxed);
		atomic_store_explicit(&peer->update_in, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->update_out, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->keepalive_in, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->keepalive_out, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->notify_in, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->notify_out, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->refresh_in, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->refresh_out, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->dynamic_cap_in, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&peer->dynamic_cap_out, 0,
				      memory_order_relaxed);
	}
}

/* Helper function to resend some BGP capabilities that are uncontrolled.
 * For instance, FQDN capability, that can't be turned off, but let's say
 * we changed the hostname, we need to resend it.
 */
/*
 * 重发"不可关闭"的能力（如 FQDN 能力）——当主机名等变化时需要重新通告。
 */
static void peer_clear_capabilities(struct peer *peer, afi_t afi, safi_t safi)
{
	bgp_capability_send(peer, afi, safi, CAPABILITY_CODE_FQDN,
			    CAPABILITY_ACTION_SET);
}

/*
 * If peer clear is invoked in a loop for all peers on the BGP instance,
 * it may end up freeing the doppelganger, and if this was the next node
 * to the current node, we would end up accessing the freed next node.
 * Pass along additional parameter which can be updated if next node
 * is freed; only required when walking the peer list on BGP instance.
 */
/*
 * 硬清除一个对端会话（clear ip bgp / clear bgp neighbor）。
 * 参数：nnode 用于返回/更新"下一个待遍历节点"，防止删除 doppelganger 后访问已释放的 next 指针。
 * 关键：若处于 shutdown 状态则跳过；优先清除 maximum-prefix 溢出（此时不算真正清会话）；
 *       否则给对端发 CEASE/ADMIN_RESET 通知，做不到则安全重置会话（bgp_session_reset_safe）。
 */
int peer_clear(struct peer *peer, struct listnode **nnode)
{
	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s: peer %pBP", __func__, peer);

	if (!CHECK_FLAG(peer->flags, PEER_FLAG_SHUTDOWN)
	    || !CHECK_FLAG(peer->bgp->flags, BGP_FLAG_SHUTDOWN)) {
		if (peer_maximum_prefix_clear_overflow(peer))
			return 0;

		peer->v_start = BGP_INIT_START_TIMER;
		if (BGP_IS_VALID_STATE_FOR_NOTIF(peer->connection->status))
			bgp_notify_send(peer->connection, BGP_NOTIFY_CEASE,
					BGP_NOTIFY_CEASE_ADMIN_RESET);
		else
			bgp_session_reset_safe(peer, nnode);
	}
	return 0;
}

/*
 * 软清除（clear ip bgp ... soft [in|out|both] / message-stats / capabilities）。
 * 参数：afi/safi；stype 清除类型。
 * 返回：0；BGP_ERR_AF_UNCONFIGURED（该族未激活）；BGP_ERR_SOFT_RECONFIG_UNCONFIGURED（无 RR 能力且未配置 soft-reconfig-in）。
 * 关键分派：
 *   - SOFT_OUT/BOTH：删除 subgroup 的 DEFAULT_ORIGINATE 标志后重发路由。
 *   - SOFT_IN_ORF_PREFIX：向对端发 ORF 前缀的 Route-Refresh。
 *   - SOFT_IN/BOTH/ORF：优先用本地 Adj-RIB-In 重新过滤，否则发 Route-Refresh。
 *   - MESSAGE_STATS：清零报文统计；CAPABILITIES：重发能力。
 */
int peer_clear_soft(struct peer *peer, afi_t afi, safi_t safi,
		    enum bgp_clear_type stype)
{
	struct peer_af *paf;

	if (!peer_established(peer->connection))
		return 0;

	if (!peer->afc[afi][safi])
		return BGP_ERR_AF_UNCONFIGURED;

	peer->rtt = sockopt_tcp_rtt(peer->connection->fd);

	if (stype == BGP_CLEAR_SOFT_OUT || stype == BGP_CLEAR_SOFT_BOTH) {
		/* Clear the "neighbor x.x.x.x default-originate" flag */
		paf = peer_af_find(peer, afi, safi);
		if (paf && paf->subgroup
		    && CHECK_FLAG(paf->subgroup->sflags,
				  SUBGRP_STATUS_DEFAULT_ORIGINATE))
			UNSET_FLAG(paf->subgroup->sflags,
				   SUBGRP_STATUS_DEFAULT_ORIGINATE);

		bgp_announce_route(peer, afi, safi, false);
	}

	if (stype == BGP_CLEAR_SOFT_IN_ORF_PREFIX) {
		if (CHECK_FLAG(peer->af_cap[afi][safi],
			       PEER_CAP_ORF_PREFIX_SM_ADV) &&
		    CHECK_FLAG(peer->af_cap[afi][safi],
			       PEER_CAP_ORF_PREFIX_RM_RCV)) {
			struct bgp_filter *filter = &peer->filter[afi][safi];
			uint8_t prefix_type;

			if (CHECK_FLAG(peer->af_cap[afi][safi],
				       PEER_CAP_ORF_PREFIX_RM_RCV))
				prefix_type = ORF_TYPE_PREFIX;

			if (filter->plist[FILTER_IN].plist) {
				if (CHECK_FLAG(peer->af_sflags[afi][safi],
					       PEER_STATUS_ORF_PREFIX_SEND))
					bgp_route_refresh_send(
						peer, afi, safi, prefix_type,
						REFRESH_DEFER, 1,
						BGP_ROUTE_REFRESH_NORMAL);
				bgp_route_refresh_send(
					peer, afi, safi, prefix_type,
					REFRESH_IMMEDIATE, 0,
					BGP_ROUTE_REFRESH_NORMAL);
			} else {
				if (CHECK_FLAG(peer->af_sflags[afi][safi],
					       PEER_STATUS_ORF_PREFIX_SEND))
					bgp_route_refresh_send(
						peer, afi, safi, prefix_type,
						REFRESH_IMMEDIATE, 1,
						BGP_ROUTE_REFRESH_NORMAL);
				else
					bgp_route_refresh_send(
						peer, afi, safi, 0, 0, 0,
						BGP_ROUTE_REFRESH_NORMAL);
			}
			return 0;
		}
	}

	if (stype == BGP_CLEAR_SOFT_IN || stype == BGP_CLEAR_SOFT_BOTH
	    || stype == BGP_CLEAR_SOFT_IN_ORF_PREFIX) {
		/* If neighbor has soft reconfiguration inbound flag.
		   Use Adj-RIB-In database. */
		if (!bgp_soft_reconfig_in(peer, afi, safi)) {
			/* If neighbor has route refresh capability, send route
			   refresh
			   message to the peer. */
			if (CHECK_FLAG(peer->cap, PEER_CAP_REFRESH_RCV))
				bgp_route_refresh_send(
					peer, afi, safi, 0, 0, 0,
					BGP_ROUTE_REFRESH_NORMAL);
			else
				return BGP_ERR_SOFT_RECONFIG_UNCONFIGURED;
		}
	}

	if (stype == BGP_CLEAR_MESSAGE_STATS)
		peer_reset_message_stats(peer);

	if (stype == BGP_CLEAR_CAPABILITIES)
		peer_clear_capabilities(peer, afi, safi);

	return 0;
}

/* Display peer uptime.*/
/*
 * 格式化对端建立时长，写入 buf（或 json）。
 * 参数：uptime2 建立时刻（单调时钟），为 0 表示从未建立→输出 "never"。
 * 格式：<1天→HH:MM:SS；<1周→NdHHhMMm；<1年→NNwDdHHh；否则→YYyWWwDd。
 * use_json 为真时同时填充 peerUptime/peerUptimeMsec/peerUptimeEstablishedEpoch。
 */
char *peer_uptime(time_t uptime2, char *buf, size_t len, bool use_json,
		  json_object *json)
{
	time_t uptime1, epoch_tbuf;
	struct tm tm;

	/* If there is no connection has been done before print `never'. */
	if (uptime2 == 0) {
		if (use_json) {
			json_object_string_add(json, "peerUptime", "never");
			json_object_int_add(json, "peerUptimeMsec", 0);
		} else
			snprintf(buf, len, "never");
		return buf;
	}

	/* Get current time. */
	uptime1 = monotime(NULL);
	uptime1 -= uptime2;
	gmtime_r(&uptime1, &tm);

	if (uptime1 < ONE_DAY_SECOND)
		snprintf(buf, len, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
			 tm.tm_sec);
	else if (uptime1 < ONE_WEEK_SECOND)
		snprintf(buf, len, "%dd%02dh%02dm", tm.tm_yday, tm.tm_hour,
			 tm.tm_min);
	else if (uptime1 < ONE_YEAR_SECOND)
		snprintf(buf, len, "%02dw%dd%02dh", tm.tm_yday / 7,
			 tm.tm_yday - ((tm.tm_yday / 7) * 7), tm.tm_hour);
	else
		snprintf(buf, len, "%02dy%02dw%dd", tm.tm_year - 70,
			 tm.tm_yday / 7,
			 tm.tm_yday - ((tm.tm_yday / 7) * 7));

	if (use_json) {
		epoch_tbuf = time(NULL) - uptime1;
		json_object_string_add(json, "peerUptime", buf);
		json_object_int_add(json, "peerUptimeMsec", uptime1 * 1000);
		json_object_int_add(json, "peerUptimeEstablishedEpoch",
				    epoch_tbuf);
	}

	return buf;
}

/* ========== 11. 进程级初始化/退出、专用线程、CLI 自动补全 ========== */
/*
 * 初始化进程级单例 bgp_master（bgpd 启动时仅调用一次）。
 * 参数：master 事件循环；buffer_size socket 收发缓冲；addresses 监听地址列表。
 * 关键：memset 整个 bgp_master 后设 bm=&bgp_master；初始化连接 FIFO、互斥锁、
 *       zebra 通告队列、rd_idspace 位图（0 号占位使 id 从 1 起）、MPLS 标签池，
 *       各默认定时器/参数，最后注册 QOBJ 供自省。
 */
void bgp_master_init(struct event_loop *master, const int buffer_size,
		     struct list *addresses)
{
	qobj_init();

	memset(&bgp_master, 0, sizeof(bgp_master));

	bm = &bgp_master;

	/* Initialize the peer connection FIFO list */
	peer_connection_fifo_init(&bm->connection_fifo);
	pthread_mutex_init(&bm->peer_connection_mtx, NULL);

	zebra_announce_init(&bm->zebra_announce_head);
	zebra_l2_vni_init(&bm->zebra_l2_vni_head);
	bm->bgp = list_new();
	bm->listen_sockets = list_new();
	bm->port = BGP_PORT_DEFAULT;
	bm->addresses = addresses;
	bm->master = master;
	bm->start_time = monotime(NULL);
	bm->t_rmap_update = NULL;
	bm->rmap_update_timer = RMAP_DEFAULT_UPDATE_TIMER;
	bm->v_update_delay = BGP_UPDATE_DELAY_DEFAULT;
	bm->v_establish_wait = BGP_UPDATE_DELAY_DEFAULT;
	bm->terminating = false;
	bm->socket_buffer = buffer_size;
	bm->wait_for_fib = false;
	bm->ip_tos = IPTOS_PREC_INTERNETCONTROL;
	bm->inq_limit = BM_DEFAULT_Q_LIMIT;
	bm->outq_limit = BM_DEFAULT_Q_LIMIT;
	bm->t_bgp_sync_label_manager = NULL;
	bm->t_bgp_start_label_manager = NULL;
	bm->t_bgp_zebra_route = NULL;
	bm->restart_time = BGP_DEFAULT_RESTART_TIME;
	bm->stalepath_time = BGP_DEFAULT_STALEPATH_TIME;
	bm->select_defer_time = BGP_DEFAULT_SELECT_DEFERRAL_TIME;
	bm->rib_stale_time = BGP_DEFAULT_RIB_STALE_TIME;
	bm->t_bgp_zebra_l2_vni = NULL;

	bm->peer_clearing_batch_id = 1;
	/* TODO -- make these configurable */
	bm->peer_conn_errs_dequeue_limit = BGP_CONN_ERROR_DEQUEUE_MAX;
	bm->peer_clearing_batch_max_dests = BGP_CLEARING_BATCH_MAX_DESTS;

	bgp_mac_init();
	/* init the rd id space.
	   assign 0th index in the bitfield,
	   so that we start with id 1
	 */
	bf_init(bm->rd_idspace, UINT16_MAX);
	bf_assign_zero_index(bm->rd_idspace);

	/* mpls label dynamic allocation pool */
	bgp_lp_init(bm->master, &bm->labelpool);

	bgp_nhg_init();
	bgp_evpn_mh_init();
	QOBJ_REG(bm, bgp_master);
}

/*
 * Free up connected routes and interfaces for a BGP instance. Invoked upon
 * instance delete (non-default only) or BGP exit.
 */
/*
 * 释放某个 BGP 实例关联的直连路由与接口（实例删除或 bgpd 退出时调用）。
 * 注意：VIEW 类型实例无 vrf，直接返回；对 vrf 内所有接口的 connected 逐个删除。
 */
static void bgp_if_finish(struct bgp *bgp)
{
	struct vrf *vrf;
	struct interface *ifp;

	vrf = bgp_vrf_lookup_by_instance_type(bgp);

	if (bgp->inst_type == BGP_INSTANCE_TYPE_VIEW || !vrf)
		return;

	FOR_ALL_INTERFACES (vrf, ifp) {
		struct connected *c;

		frr_each_safe (if_connected, ifp->connected, c)
			bgp_connected_delete(bgp, c);
	}
}

/* CLI 自动补全：列出所有 VRF 名与该进程已创建的所有 VIEW 实例名。 */
static void bgp_viewvrf_autocomplete(vector comps, struct cmd_token *token)
{
	struct vrf *vrf = NULL;
	struct listnode *next;
	struct bgp *bgp;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, vrf->name));

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, bgp)) {
		if (bgp->inst_type != BGP_INSTANCE_TYPE_VIEW)
			continue;

		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, bgp->name));
	}
}

/* CLI 自动补全：列出所有实例的 AS 号（去重，只显示每个 AS 的首次出现）。 */
static void bgp_instasn_autocomplete(vector comps, struct cmd_token *token)
{
	struct listnode *next, *next2;
	struct bgp *bgp, *bgp2;
	char buf[ASN_STRING_MAX_SIZE];

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, next, bgp)) {
		/* deduplicate */
		for (ALL_LIST_ELEMENTS_RO(bm->bgp, next2, bgp2)) {
			if (bgp2->as == bgp->as)
				break;
			if (bgp2 == bgp)
				break;
		}
		if (bgp2 != bgp)
			continue;

		snprintf(buf, sizeof(buf), "%s", bgp->as_pretty);
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, buf));
	}
}

/* CLI 变量处理器：把令牌 VIEWVRFNAME / instasn 绑定到对应补全函数。 */
static const struct cmd_variable_handler bgp_viewvrf_var_handlers[] = {
	{.tokenname = "VIEWVRFNAME", .completions = bgp_viewvrf_autocomplete},
	{.varname = "instasn", .completions = bgp_instasn_autocomplete},
	{.completions = NULL},
};

/* 专用 IO 线程句柄：负责报文的收/发与编解码（把重活从主线程搬走）。 */
struct frr_pthread *bgp_pth_io;
/* 专用 Keepalive 线程句柄：独立处理 KEEPALIVE 定时与发送。 */
struct frr_pthread *bgp_pth_ka;

/*
 * 创建（但不启动）BGP 的两个专用 pthread：IO 线程与 Keepalive 线程。
 * assert 保证只初始化一次；具体启动在 bgp_pthreads_run。
 */
static void bgp_pthreads_init(void)
{
	assert(!bgp_pth_io);
	assert(!bgp_pth_ka);

	struct frr_pthread_attr io = {
		.start = frr_pthread_attr_default.start,
		.stop = frr_pthread_attr_default.stop,
	};
	struct frr_pthread_attr ka = {
		.start = bgp_keepalives_start,
		.stop = bgp_keepalives_stop,
	};
	bgp_pth_io = frr_pthread_new(&io, "BGP I/O thread", "bgpd_io");
	bgp_pth_ka = frr_pthread_new(&ka, "BGP Keepalives thread", "bgpd_ka");
}

/* 启动两个专用线程并等待其进入运行态（wait_running）。 */
void bgp_pthreads_run(void)
{
	frr_pthread_run(bgp_pth_io, NULL);
	frr_pthread_run(bgp_pth_ka, NULL);

	/* Wait until threads are ready. */
	frr_pthread_wait_running(bgp_pth_io);
	frr_pthread_wait_running(bgp_pth_ka);
}

/* 停止全部由 frr_pthread 管理的线程（进程退出时调用）。 */
void bgp_pthreads_finish(void)
{
	frr_pthread_stop_all();
}

/*
 * 配置装载结束（bgp_config_end 钩子）后的收尾：把装载期间被挂起的 "config-pending hold" 对端
 * 释放（shut_during_cfg 置 false 并按需重启会话），并处理 Graceful Restart 相关状态上报。
 * 关键：若重启中且所有对端都 admin down，向 zebra 发 UPDATE_PENDING / UPDATE_COMPLETE；
 *       否则为所有支持 GR 的 afi/safi 启动 select-deferral 定时器。
 */
static int peer_unshut_after_cfg(struct bgp *bgp)
{
	struct listnode *node;
	struct peer *peer;
	bool all_peers_are_admin_down = true;
	bool gr_cfgd_at_nbr = false;

	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
		/* This peer is admin up */
		if (!CHECK_FLAG(peer->flags, PEER_FLAG_SHUTDOWN))
			all_peers_are_admin_down = false;

		if (CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART))
			gr_cfgd_at_nbr = true;

		if (!peer->shut_during_cfg)
			continue;

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s: released from config-pending hold",
				   peer->host);

		peer->shut_during_cfg = false;
		if (peer_active(peer->connection) == BGP_PEER_ACTIVE &&
		    peer->connection->status != Established) {
			if (peer->connection->status != Idle)
				BGP_EVENT_ADD(peer->connection, BGP_Stop);
			BGP_EVENT_ADD(peer->connection, BGP_Start);
		}
	}

	/*
	 * If bgp shutdown is configured, all the peers will be set to admin
	 * down. If there are no bgp peers configured then
	 * all_peers_are_admin_down will be true.
	 *
	 * if All bgp peers are admin down or if there are no BGP peers
	 * configured or if bgp shutdown is configured then set this boolean to
	 * true.
	 */
	all_peers_are_admin_down = (all_peers_are_admin_down ||
				    CHECK_FLAG(bgp->flags, BGP_FLAG_SHUTDOWN));

	enum global_mode global_gr_mode = bgp_global_gr_mode_get(bgp);

	if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
		zlog_debug("GR %s: All peers in %s are ADMIN down %d. BGP in GR %d, GR mode %s, gr is %sconfigured at nbr",
			   __func__, bgp->name_pretty, all_peers_are_admin_down,
			   bgp_in_graceful_restart(), print_global_gr_mode(global_gr_mode),
			   (gr_cfgd_at_nbr) ? "" : "not ");

	frrtrace(5, frr_bgp, gr_bgp_state, bgp->name_pretty, all_peers_are_admin_down,
		 bgp_in_graceful_restart(), global_gr_mode, gr_cfgd_at_nbr);

	/*
	 * If BGP is not in GR and startup timer is not running
	 * OR
	 * If this VRF doesn't have GR configured at global and neighbor level
	 * then return
	 */
	if ((!bgp_in_graceful_restart() && !bgp->t_startup) ||
	    (global_gr_mode != GLOBAL_GR && !gr_cfgd_at_nbr))
		return 0;

	/*
	 * If BGP is restarting gracefully, if the mode is GLOBAL_GR
	 * and if there are no BGP peers configured/all peers are admin down,
	 * then send the UPDATE_PENDING and UPDATE_COMPLETE to zebra.
	 */
	if (all_peers_are_admin_down) {
		if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
			zlog_debug("GR %s: All peers in %s are ADMIN down. Sending update_pending and complete to zebra",
				   __func__, bgp->name_pretty);

		/* Send GR capability to zebra for this VRF */
		bgp_zebra_send_capabilities(bgp, false);

		afi_t afi;
		safi_t safi;

		FOREACH_AFI_SAFI_NSF (afi, safi) {
			if (!bgp_gr_supported_for_afi_safi(afi, safi))
				continue;
			/* Inform zebra */
			bgp_zebra_update(bgp, afi, safi, ZEBRA_CLIENT_ROUTE_UPDATE_PENDING);
			bgp_zebra_update(bgp, afi, safi, ZEBRA_CLIENT_ROUTE_UPDATE_COMPLETE);
		}
	} else {
		/* start select-deferral-timer for all GR supported afi safi */
		bgp_gr_start_all_deferral_timers(bgp);
	}

	return 0;
}

/*
 * bgpd 进程初始化总入口（在主线程、进入事件循环前调用一次）。
 * 参数：instance 实例号（用于 zebra 会话标识）。
 * 动作：注册配置结束钩子、启动专用线程、初始化 zebra/VNC、安装 VTY 命令、
 *       初始化各子系统（attr/label/route/route-map/vpn/evpn/flowspec/BFD 等），
 *       并把 ACL / AS-path list / prefix-list 的增删钩子接到对应 update 回调。
 */
void bgp_init(unsigned short instance)
{
	hook_register(bgp_config_end, peer_unshut_after_cfg);

	/* allocates some vital data structures used by peer commands in
	 * vty_init */

	/* pre-init pthreads */
	bgp_pthreads_init();

	/* Init zebra. */
	bgp_zebra_init(bm->master, instance);

#ifdef ENABLE_BGP_VNC
	vnc_zebra_init(bm->master);
#endif

	/* BGP VTY commands installation.  */
	bgp_vty_init();

	/* BGP inits. */
	bgp_attr_init();
	bgp_labels_init();
	bgp_debug_init();
	bgp_community_alias_init();
	bgp_dump_init();
	bgp_route_init();
	bgp_route_map_init();
	bgp_scan_vty_init();
	bgp_mplsvpn_init();
#ifdef ENABLE_BGP_VNC
	rfapi_init();
#endif
	bgp_ethernetvpn_init();
	bgp_flowspec_vty_init();

	/* Access list initialize. */
	access_list_init();
	access_list_add_hook(peer_distribute_update);
	access_list_delete_hook(peer_distribute_update);

	/* Filter list initialize. */
	bgp_filter_init();
	as_list_add_hook(peer_aslist_add);
	as_list_delete_hook(peer_aslist_del);

	/* Prefix list initialize.*/
	prefix_list_init();
	prefix_list_add_hook(peer_prefix_list_update);
	prefix_list_delete_hook(peer_prefix_list_update);

	/* Community list initialize. */
	bgp_clist = community_list_init();

	/* BFD init */
	bgp_bfd_init(bm->master);

	bgp_lp_vty_init();

	bgp_label_per_nexthop_init();
	bgp_mplsvpn_nexthop_init();

	cmd_variable_handler_register(bgp_viewvrf_var_handlers);
}

/*
 * bgpd 进程退出清理（bgp_master_init 的逆操作）。
 * 关键顺序：先关闭监听 socket（防止对端收到 unconfig 后重连），再遍历所有实例/对端
 *           发送 PEER_UNCONFIG 通知（对具备 GR 能力的对端跳过，让其能平滑重启），
 *           最后清理监听列表与各类定时器、MAC/VNC 子系统。
 */
void bgp_terminate(void)
{
	struct bgp *bgp;
	struct peer *peer;
	struct listnode *node, *nnode;
	struct listnode *mnode, *mnnode;

	QOBJ_UNREG(bm);

	/* Close the listener sockets first as this prevents peers from
	 * attempting
	 * to reconnect on receiving the peer unconfig message. In the presence
	 * of a large number of peers this will ensure that no peer is left with
	 * a dangling connection
	 */

	bgp_close();
	/* reverse bgp_master_init */
	for (ALL_LIST_ELEMENTS(bm->bgp, mnode, mnnode, bgp)) {
		bgp_close_vrf_socket(bgp);
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			if (BGP_PEER_GRACEFUL_RESTART_CAPABLE(peer)) {
				if (bgp_debug_neighbor_events(peer))
					zlog_debug(
						"%pBP configured Graceful-Restart, skipping unconfig notification",
						peer);
				continue;
			}
			peer_notify_unconfig(peer->connection);
		}
	}

	if (bm->listen_sockets)
		list_delete(&bm->listen_sockets);

	event_cancel(&bm->t_rmap_update);
	event_cancel(&bm->t_bgp_sync_label_manager);
	event_cancel(&bm->t_bgp_start_label_manager);
	event_cancel(&bm->t_bgp_zebra_route);
	event_cancel(&bm->t_bgp_zebra_l2_vni);

	bgp_mac_finish();
#ifdef ENABLE_BGP_VNC
	rfapi_terminate();
#endif
}

/*
 * 按用户输入（地址/接口名/主机名/peer-group 名）在指定实例内查找 peer，用于 VTY 命令。
 * 参数：bgp 目标实例；ip_str 输入串；use_json 出错输出格式。
 * 查找顺序：sockunion 地址 → peer_lookup_by_conf_if（接口名）→ peer_lookup_by_hostname（FQDN）
 *           → peer_group_lookup（组名，取其首个成员）。
 * 返回：peer 指针；找不到则输出错误并返回 NULL。
 */
struct peer *peer_lookup_in_view(struct vty *vty, struct bgp *bgp,
				 const char *ip_str, bool use_json)
{
	int ret;
	struct peer *peer;
	union sockunion su;
	struct peer_group *group;

	/* Get peer sockunion. */
	ret = str2sockunion(ip_str, &su);
	if (ret < 0) {
		peer = peer_lookup_by_conf_if(bgp, ip_str);
		if (!peer) {
			peer = peer_lookup_by_hostname(bgp, ip_str);

			if (!peer) {
				group = peer_group_lookup(bgp, ip_str);
				if (group)
					peer = listnode_head(group->peer);
			}

			if (!peer) {
				if (use_json) {
					json_object *json_no = NULL;
					json_no = json_object_new_object();
					json_object_string_add(
						json_no,
						"malformedAddressOrName",
						ip_str);
					vty_json(vty, json_no);
				} else
					vty_out(vty,
						"%% Malformed address or name: %s\n",
						ip_str);
				return NULL;
			}
		}
		return peer;
	}

	/* Peer structure lookup. */
	peer = peer_lookup(bgp, &su);
	if (!peer) {
		if (use_json) {
			json_object *json_no = NULL;
			json_no = json_object_new_object();
			json_object_string_add(json_no, "warning",
					       "No such neighbor in this view/vrf");
			vty_json(vty, json_no);
		} else
			vty_out(vty, "No such neighbor in this view/vrf\n");
		return NULL;
	}

	return peer;
}

/* ========== 12. Graceful Restart 运行配置应用与对端启动 ========== */
/*
 * 启动/重载时根据运行配置（含各邻居的 GR 标志）决定是否向 zebra 声明本实例的 GR 能力。
 * 若检测到任一对端配置了 GR 而 zebra 侧当前为 DISABLE，则发送 capabilities(enable)；
 * 反之若已无 GR 对端而 zebra 侧为 ENABLE，则发送 capabilities(disable)。
 */
void bgp_gr_apply_running_config(void)
{
	struct peer *peer = NULL;
	struct bgp *bgp = NULL;
	struct listnode *node, *nnode;
	bool gr_router_detected = false;

	if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
		zlog_debug("[BGP_GR] %s called !", __func__);

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
			bgp_peer_gr_flags_update(peer);
			if (CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART))
				gr_router_detected = true;
		}

		if (gr_router_detected
		    && bgp->present_zebra_gr_state == ZEBRA_GR_DISABLE) {
			bgp_zebra_send_capabilities(bgp, true);
		} else if (!gr_router_detected
			   && bgp->present_zebra_gr_state == ZEBRA_GR_ENABLE) {
			bgp_zebra_send_capabilities(bgp, false);
		}

		gr_router_detected = false;
	}
}

/* 对全部实例的所有对端（未被抑制启用的）下发 BGP_Start 事件，统一驱动 FSM 起会话。 */
void bgp_gr_start_peers(void)
{
	struct peer *peer = NULL;
	struct bgp *bgp = NULL;
	struct listnode *node, *nnode;
	struct listnode *node2, *nnode2;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		for (ALL_LIST_ELEMENTS(bgp->peer, node2, nnode2, peer)) {
			if (!BGP_PEER_START_SUPPRESSED(peer))
				BGP_EVENT_ADD(peer->connection, BGP_Start);
		}
	}
}

/* ========== 13. 批量清理（clearing batch）：把一次 clear 的多个 peer 合并处理 ========== */
/* Hash of peers in clearing info object */
/* 清理批次 peer 哈希的键比较：直接按指针大小比较（同一 peer 只有唯一实例）。 */
static int peer_clearing_hash_cmp(const struct peer *p1, const struct peer *p2)
{
	if (p1 == p2)
		return 0;
	else if (p1 < p2)
		return -1;
	else
		return 1;
}

/* 清理批次 peer 哈希函数：取指针低 32 位。 */
static uint32_t peer_clearing_hashfn(const struct peer *p1)
{
	return (uint32_t)((intptr_t)p1 & 0xffffffffULL);
}

/*
 * Free a clearing batch: this really just does the memory cleanup; the
 * clearing code is expected to manage the peer, dest, table, etc refcounts
 */
/*
 * 释放一个清理批次结构（仅做内存回收，peer/dest 的引用计数由清理流程自身负责）。
 * 若批次仍在 clearing_list 上则摘除，清空 dest 链表，销毁 peer 哈希，最后释放结构体；
 * 并把 *pinfo 置空（通过 XFREE 宏完成）。
 */
static void bgp_clearing_batch_free(struct bgp *bgp,
				    struct bgp_clearing_info **pinfo)
{
	struct bgp_clearing_info *cinfo = *pinfo;
	struct bgp_clearing_dest *destinfo;

	if (bgp_clearing_info_anywhere(cinfo))
		bgp_clearing_info_del(&bgp->clearing_list, cinfo);

	while ((destinfo = bgp_clearing_destlist_pop(&cinfo->destlist)) != NULL)
		XFREE(MTYPE_CLEARING_BATCH, destinfo);

	bgp_clearing_hash_fini(&cinfo->peers);

	XFREE(MTYPE_CLEARING_BATCH, *pinfo);
}

/*
 * Done with a peer that was part of a clearing batch
 */
/*
 * 单个 peer 在清理批次中处理完毕：清 CLEARING_BATCH 标志，向 FSM 投递 Clearing_Completed
 * 事件让会话继续推进，并释放批次为其持有的那次 peer_lock 引用。
 */
static void bgp_clearing_peer_done(struct peer *peer)
{
	UNSET_FLAG(peer->flags, PEER_FLAG_CLEARING_BATCH);

	/* Tickle FSM to start moving again */
	BGP_EVENT_ADD(peer->connection, Clearing_Completed);

	peer_unlock(peer); /* bgp_clear_route */
}

/*
 * Initialize a new batch struct for clearing peer(s) from the RIB
 */
/*
 * 开始一个新的清理批次：分配并初始化 bgp_clearing_info，置 OPEN 标志后挂到实例 clearing_list 头。
 * 若已有批次结束事件在跑则直接返回（避免重复开启）。
 */
void bgp_clearing_batch_begin(struct bgp *bgp)
{
	struct bgp_clearing_info *cinfo;

	if (event_is_scheduled(bgp->clearing_end))
		return;

	cinfo = XCALLOC(MTYPE_CLEARING_BATCH, sizeof(struct bgp_clearing_info));

	cinfo->bgp = bgp;
	cinfo->id = bm->peer_clearing_batch_id++;

	/* Init hash of peers and list of dests */
	bgp_clearing_hash_init(&cinfo->peers);
	bgp_clearing_destlist_init(&cinfo->destlist);

	/* Batch is open for more peers */
	SET_FLAG(cinfo->flags, BGP_CLEARING_INFO_FLAG_OPEN);

	bgp_clearing_info_add_head(&bgp->clearing_list, cinfo);
}

/*
 * Close a batch of clearing peers, and begin working on the RIB
 */
/*
 * 关闭当前清理批次并启动 RIB 遍历。
 * 关键：清 OPEN 标志；若批内无 peer 直接释放；否则调用 bgp_clear_route_batch 遍历 RIB 收集待清 dest，
 *       遍历结束或无事可做时该函数会自行释放批次。
 */
static void bgp_clearing_batch_end(struct bgp *bgp)
{
	struct bgp_clearing_info *cinfo;

	if (event_is_scheduled(bgp->clearing_end))
		return;

	cinfo = bgp_clearing_info_first(&bgp->clearing_list);
	if (!cinfo)
		return; /* Nothing to do */

	assert(CHECK_FLAG(cinfo->flags, BGP_CLEARING_INFO_FLAG_OPEN));

	/* Batch is closed */
	UNSET_FLAG(cinfo->flags, BGP_CLEARING_INFO_FLAG_OPEN);

	/* If we have no peers to examine, just discard the batch info */
	if (bgp_clearing_hash_count(&cinfo->peers) == 0) {
		bgp_clearing_batch_free(bgp, &cinfo);
		return;
	}

	/* Do a RIB walk for the current batch. If it finds dests/prefixes
	 * to work on, this will schedule a task to process
	 * the dests/prefixes in the batch.
	 * NB this will free the batch if it finishes, or if there was no work
	 * to do.
	 */
	bgp_clear_route_batch(cinfo);
}

/* 批次结束定时器回调：执行 batch_end 并释放 start 时持有的 bgp 引用计数。 */
static void bgp_clearing_batch_end_event(struct event *event)
{
	struct bgp *bgp = EVENT_ARG(event);

	bgp_clearing_batch_end(bgp);
	bgp_unlock(bgp);
}

/*
 * 安排"批次结束"定时器（100ms 后触发），把同一时刻的多个清理请求合并到一批处理。
 * 首次调度时对 bgp 加锁（bgp_lock），由回调末尾的 bgp_unlock 配对释放。
 */
void bgp_clearing_batch_end_event_start(struct bgp *bgp)
{
	if (!event_is_scheduled(bgp->clearing_end))
		bgp_lock(bgp);

	event_cancel(&bgp->clearing_end);
	event_add_timer_msec(bm->master, bgp_clearing_batch_end_event, bgp, 100, &bgp->clearing_end);
}

/* Check whether a dest's peer is relevant to a clearing batch */
/* 判断某 peer 是否属于该清理批次（在批次 peer 哈希中查得即相关）。 */
bool bgp_clearing_batch_check_peer(struct bgp_clearing_info *cinfo,
				   const struct peer *peer)
{
	struct peer *p;

	p = bgp_clearing_hash_find(&cinfo->peers, peer);
	return (p != NULL);
}

/*
 * Check whether a clearing batch has any dests to process
 */
/* 判断批次中是否还有待处理的 dest（dest 链表非空）。 */
bool bgp_clearing_batch_dests_present(struct bgp_clearing_info *cinfo)
{
	return (bgp_clearing_destlist_count(&cinfo->destlist) > 0);
}

/*
 * Done with a peer clearing batch; deal with refcounts, free memory
 */
/*
 * 清理批次全部处理完毕：取消调度事件，对剩余 peer 逐个做完结处理（含解锁），
 * 释放所有 dest 节点并相应解锁 table，最后释放批次结构。
 */
void bgp_clearing_batch_completed(struct bgp_clearing_info *cinfo)
{
	struct peer *peer;
	struct bgp_dest *dest;
	struct bgp_clearing_dest *destinfo;
	struct bgp_table *table;
	uint32_t idx = 0;

	if (bgp_debug_neighbor_events(NULL))
		zlog_debug("%s: cinfo id %#x, %p, total %u", __func__, cinfo->id, cinfo,
			   cinfo->total_counter);

	/* Ensure event is not scheduled */
	event_cancel_event(bm->master, &cinfo->t_sched);

	/* Remove all peers and un-ref */
	while ((peer = bgp_clearing_hash_pop_all(&cinfo->peers, &idx)) != NULL)
		bgp_clearing_peer_done(peer);

	/* Remove any dests/prefixes and unlock */
	destinfo = bgp_clearing_destlist_pop(&cinfo->destlist);
	while (destinfo) {
		dest = destinfo->dest;
		XFREE(MTYPE_CLEARING_BATCH, destinfo);

		table = bgp_dest_table(dest);
		bgp_dest_unlock_node(dest);
		bgp_table_unlock(table);

		destinfo = bgp_clearing_destlist_pop(&cinfo->destlist);
	}

	/* Free memory */
	bgp_clearing_batch_free(cinfo->bgp, &cinfo);
}

/*
 * Add a prefix/dest to a clearing batch
 */
/* 把一个待清理的 dest（前缀节点）加入批次的 dest 链表尾部。 */
void bgp_clearing_batch_add_dest(struct bgp_clearing_info *cinfo,
				 struct bgp_dest *dest)
{
	struct bgp_clearing_dest *destinfo;

	destinfo = XCALLOC(MTYPE_CLEARING_BATCH,
			   sizeof(struct bgp_clearing_dest));

	destinfo->dest = dest;
	bgp_clearing_destlist_add_tail(&cinfo->destlist, destinfo);
}

/*
 * Return the next dest for batch clear processing
 */
/* 从批次 dest 链表弹出一个 dest 返回给调用方处理；为空则返回 NULL。 */
struct bgp_dest *bgp_clearing_batch_next_dest(struct bgp_clearing_info *cinfo)
{
	struct bgp_clearing_dest *destinfo;
	struct bgp_dest *dest = NULL;

	destinfo = bgp_clearing_destlist_pop(&cinfo->destlist);
	if (destinfo) {
		dest = destinfo->dest;
		XFREE(MTYPE_CLEARING_BATCH, destinfo);
	}

	return dest;
}

/* If a clearing batch is available for 'peer', add it and return 'true',
 * else return 'false'.
 */
/*
 * 尝试把一个 peer 加入当前处于 OPEN 状态的清理批次。
 * 成功（批次可用）则对 peer_lock 加引用、加入哈希并置 CLEARING_BATCH 标志，返回 true；
 * 否则返回 false 表示当前无可用批次（调用方可能需另起批次或直接处理）。
 */
bool bgp_clearing_batch_add_peer(struct bgp *bgp, struct peer *peer)
{
	struct bgp_clearing_info *cinfo;

	cinfo = bgp_clearing_info_first(&bgp->clearing_list);

	if (cinfo && CHECK_FLAG(cinfo->flags, BGP_CLEARING_INFO_FLAG_OPEN)) {
		if (!CHECK_FLAG(peer->flags, PEER_FLAG_CLEARING_BATCH)) {
			/* Add a peer ref */
			peer_lock(peer);

			bgp_clearing_hash_add(&cinfo->peers, peer);
			SET_FLAG(peer->flags, PEER_FLAG_CLEARING_BATCH);

			if (bgp_debug_neighbor_events(peer))
				zlog_debug("%s: peer %pBP batched in %#x", __func__,
					   peer, cinfo->id);
		}
		return true;
	}

	return false;
}

/* ========== 14. IO 线程连接错误处理（错误入队/出队/重排 + 主线程兜底） ========== */
/*
 * Task callback in the main pthread to handle socket errors
 * encountered in the io pthread. We avoid having the io pthread try
 * to enqueue fsm events or mess with the peer struct.
 */
/*
 * 主线程中处理 IO 线程上报的 socket 错误（IO 线程不直接操作 peer/FSM，避免锁与竞态）。
 * 参数：event 携带 bgp 实例。
 * 关键：加锁摘取一个 connection，逐个出队：若连接已 Established，按是否处于 NSF/GR 模式
 *       设置 last_reset 原因（NSF_CLOSE_SESSION 或 CLOSE_SESSION），再驱动 FSM 处理。
 *       若错误列表非空则启动清理批次以批量处理 peer。
 */
static void bgp_process_conn_error(struct event *event)
{
	struct bgp *bgp;
	struct peer *peer;
	struct peer_connection *connection;
	uint32_t counter = 0;
	size_t list_count = 0;
	bool more_p = false;

	bgp = EVENT_ARG(event);

	frr_with_mutex (&bgp->peer_errs_mtx) {
		connection = bgp_peer_conn_errlist_pop(&bgp->peer_conn_errlist);

		list_count =
			bgp_peer_conn_errlist_count(&bgp->peer_conn_errlist);
	}

	/* If we have multiple peers with errors, try to batch some
	 * clearing work.
	 */
	if (list_count > 0)
		bgp_clearing_batch_begin(bgp);

	/* Dequeue peers from the error list */
	while (connection != NULL) {
		peer = connection->peer;

		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s [Event] BGP error %d on fd %d",
				   peer->host, connection->connection_errcode,
				   connection->fd);

		/* Closed connection or error on the socket */
		if (peer_established(connection)) {
			if ((CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART)
			     || CHECK_FLAG(peer->flags,
					   PEER_FLAG_GRACEFUL_RESTART_HELPER))
			    && CHECK_FLAG(peer->sflags, PEER_STATUS_NSF_MODE)) {
				peer_set_last_reset(peer, PEER_DOWN_NSF_CLOSE_SESSION);
				SET_FLAG(peer->sflags, PEER_STATUS_NSF_WAIT);
			} else
				peer_set_last_reset(peer, PEER_DOWN_CLOSE_SESSION);
		}

		/* No need for keepalives, if enabled */
		bgp_keepalives_off(peer->connection);

		/* Drive into state-machine changes */
		bgp_event_update(connection, connection->connection_errcode);

		counter++;
		if (counter >= bm->peer_conn_errs_dequeue_limit)
			break;

		connection = bgp_dequeue_conn_err(bgp, &more_p);
	}

	/* Reschedule event if necessary */
	if (more_p)
		bgp_conn_err_reschedule(bgp);

	/* Done with a clearing batch */
	if (list_count > 0)
		bgp_clearing_batch_end(bgp);

	if (bgp_debug_neighbor_events(NULL))
		zlog_debug("%s: dequeued and processed %d peers", __func__,
			   counter);
}

/*
 * Enqueue a connection with an error to be handled in the main pthread;
 * this is called from the io pthread.
 */
/*
 * 由 IO 线程调用：把一个出错的连接入队到 bgp 的错误链表，并调度主线程处理事件。
 * 加锁保护链表；用 anywhere 判断避免重复入队；把 errcode 记到 connection 上。
 */
int bgp_enqueue_conn_err(struct bgp *bgp, struct peer_connection *connection,
			 int errcode)
{
	frr_with_mutex (&bgp->peer_errs_mtx) {
		connection->connection_errcode = errcode;

		/* Careful not to double-enqueue */
		if (!bgp_peer_conn_errlist_anywhere(connection)) {
			bgp_peer_conn_errlist_add_tail(&bgp->peer_conn_errlist,
						       connection);
		}
	}
	/* Ensure an event is scheduled */
	event_add_event(bm->master, bgp_process_conn_error, bgp, 0,
			&bgp->t_conn_errors);
	return 0;
}

/*
 * Dequeue a connection that encountered a connection error; signal whether there
 * are more queued peers.
 */
/*
 * 从错误链表弹出一个 connection，并通过 more_p 返回链表是否仍有剩余（供决定是否重排事件）。
 */
struct peer_connection *bgp_dequeue_conn_err(struct bgp *bgp, bool *more_p)
{
	struct peer_connection *connection = NULL;
	bool more = false;

	frr_with_mutex (&bgp->peer_errs_mtx) {
		connection = bgp_peer_conn_errlist_pop(&bgp->peer_conn_errlist);

		if (bgp_peer_conn_errlist_const_first(
			    &bgp->peer_conn_errlist) != NULL)
			more = true;
	}

	if (more_p)
		*more_p = more;

	return connection;
}

/*
 * Reschedule the connection error event - probably after processing
 * some of the peers on the list.
 */
/* 重新调度连接错误处理事件（处理完一批对端后，若仍有剩余则再次触发）。 */
void bgp_conn_err_reschedule(struct bgp *bgp)
{
	event_add_event(bm->master, bgp_process_conn_error, bgp, 0,
			&bgp->t_conn_errors);
}

/* ========== 15. 杂项：printfrr 扩展（%pBP）与火星地址类型转换 ========== */
/*
 * 注册 printfrr 扩展格式符 %pBP（打印 struct peer 的可读信息），
 * 使日志中 "%pBP" 能统一输出对端标识。注册宏本身在文件加载时自动执行。
 */
printfrr_ext_autoreg_p("BP", printfrr_bp);
/*
 * %pBP 的实现：peer 为空输出 "(null)"；优先输出 host(hostname)；
 * 无 host 时回退到接口名 conf_if 或 sockunion 地址。
 */
static ssize_t printfrr_bp(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct peer *peer = ptr;

	if (!peer)
		return bputs(buf, "(null)");

	if (!peer->host) {
		if (peer->conf_if)
			return bprintfrr(buf, "%s", peer->conf_if);
		return bprintfrr(buf, "%pSU", &peer->connection->su);
	}

	return bprintfrr(buf, "%s(%s)", peer->host,
			 peer->hostname ? peer->hostname : "Unknown");
}

/* "火星"（非法/本端自身）地址类型到可读字符串的映射表（用于日志/show）。 */
const struct message bgp_martian_type_str[] = {
	{BGP_MARTIAN_IF_IP, "Self Interface IP"},
	{BGP_MARTIAN_TUN_IP, "Self Tunnel IP"},
	{BGP_MARTIAN_IF_MAC, "Self Interface MAC"},
	{BGP_MARTIAN_RMAC, "Self RMAC"},
	{BGP_MARTIAN_SOO, "Self Site-of-Origin"},
	{0}};

/* 把 bgp_martian_type 枚举转成字符串；未知类型返回 "Unknown Martian Type"。 */
const char *bgp_martian_type2str(enum bgp_martian_type mt)
{
	return lookup_msg(bgp_martian_type_str, mt, "Unknown Martian Type");
}
