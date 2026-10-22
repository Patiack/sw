// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP-4 Finite State Machine
 * From RFC1771 [A Border Gateway Protocol 4 (BGP-4)]
 * Copyright (C) 1998 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_fsm.h —— 连接状态机的对外接口与一批辅助宏
 * ============================================================================
 * 职责与定位：bgp_fsm.c 的头文件。除了函数声明，这里还定义了读代码时绕不开的
 * 几个“小工具宏”，它们在本模块其它文件里被大量使用：
 *
 *   - BGP_TIMER_ON        : 给 connection 挂一个定时器（顺带做 status != Deleted 保护）。
 *   - BGP_EVENT_ADD       : 向事件队列投递一个 FSM 事件（异步，最终由 bgp_event 处理）。
 *   - BGP_UPDATE_GROUP_TIMER_ON : 出向更新定时器，带 route-adv-delay 分支。
 *   - FSM_PEER_* 返回码   : bgp_event_update() 的返回值语义，用来告诉调用者
 *                           “这次事件处理把 peer 怎么样了”（无变化/停止/换心/状态迁移）。
 *
 * 关于 GR（Graceful Restart）：本文件后半段那一大组宏与函数声明（bgp_peer_gr_*、
 * bgp_gr_*）实现的是 **GR 开关的配置状态机**，跟 RFC4271 的连接状态机是两回事，
 * 阅读时别混在一起。判断某个 peer 当前处于“重启方 / 帮助方 / 全局继承”靠的是
 * PEER_GRACEFUL_RESTART_NEW_STATE_* 标志位，而不是连接状态。
 *
 * 建议阅读顺序：enum bgp_fsm_state_progress → 前三个定时器/事件宏 → FSM_PEER_*
 *               → 函数声明（bgp_event / bgp_event_update / bgp_stop / bgp_timer_set）
 *               → 最后才是 GR 那一组。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_FSM_H
#define _QUAGGA_BGP_FSM_H

/* bgp_event_update()/FSM 动作函数的“进展”返回码：
 *   成功      = 0：本次处理无异常，状态机继续按表中 next_state 推进；
 *   失败      = -1：出错，peer 需停机（通常转 Clearing/Idle）；
 *   失败并删除 = -2：出错且这个 peer 已不可用，需要整个删除掉；
 *   成功并换心 = 1：连接碰撞处理成功，peer 的连接被“迁移”到了另一条 TCP 连接上
 *                （见 bgp_fsm.c 的 peer_xfer_conn），调用方必须按新连接继续处理。
 * 注意 1 与 0 的区别：0 是“什么都没变”，1 是“底层连接对象换了”，
 * 调用方若忽略 1 就可能继续操作一个已被替换/释放的连接。 */
enum bgp_fsm_state_progress {
	BGP_FSM_FAILURE_AND_DELETE = -2,
	BGP_FSM_FAILURE = -1,
	BGP_FSM_SUCCESS = 0,
	BGP_FSM_SUCCESS_STATE_TRANSFER = 1,
};

/* Macro for BGP read, write and timer thread.  */
/* 【宏】BGP_TIMER_ON(T, F, V) —— 在 connection 上挂一个事件定时器。
 * T = 存放定时器句柄的字段地址（如 &connection->t_connect），F = 回调函数，
 * V = 相对当前时刻的延迟(秒)。
 * 关键点：宏内先判 `connection->status != Deleted`——因为定时器回调执行期间
 * 连接可能已被标记删除，此时再挂定时器会操作已释放对象。这里是**唯一**的挂载点，
 * 全模块统一走它，就是为了不漏掉这个判断。 */
#define BGP_TIMER_ON(T, F, V)                                                  \
	do {                                                                   \
		if ((connection->status != Deleted))                           \
			event_add_timer(bm->master, (F), connection, (V),      \
					&(T));                                 \
	} while (0)

/* 【宏】BGP_EVENT_ADD(C, E) —— 向主事件循环投递一个 BGP FSM 事件。
 * C = 目标 connection，E = enum bgp_fsm_events 事件码。
 * 语义：**异步**——不是立刻改状态，而是排入事件队列，稍后由 bgp_event() 处理。
 * 因此“投递事件”与“状态真正迁移”之间存在时间差，中间可能插进别的回调；
 * 读状态机代码时务必分清“我投了什么事件”与“事件何时被消费”。
 * 同样带 status != Deleted 保护。 */
#define BGP_EVENT_ADD(C, E)                                                     \
	do {                                                                    \
		if ((C)->status != Deleted)                                     \
			event_add_event(bm->master, bgp_event, (C), (E), NULL); \
	} while (0)

/* 【宏】BGP_UPDATE_GROUP_TIMER_ON(T, F) —— 挂“出向更新”定时器。
 * 两种情形：
 *   - 开了 FIB 抑制(bgp suppress-fib-pending)且该 peer 配置了 route-adv-delay：
 *     延迟 BGP_DEFAULT_UPDATE_ADVERTISEMENT_TIME 秒再发，等 FIB 收敛，
 *     避免把还没装表的路由先通告出去（否则会引来流量黑洞）。
 *   - 否则立即(0ms)触发。
 * 注意这里用的是毫秒版 event_add_timer_msec，且延迟值乘了 1000。 */
#define BGP_UPDATE_GROUP_TIMER_ON(T, F)                                               \
	do {                                                                          \
		if (BGP_SUPPRESS_FIB_ENABLED(peer->bgp) &&                            \
		    PEER_ROUTE_ADV_DELAY(peer))                                       \
			event_add_timer_msec(bm->master, (F), connection,             \
					     (BGP_DEFAULT_UPDATE_ADVERTISEMENT_TIME * \
					      1000),                                  \
					     (T));                                    \
		else                                                                  \
			event_add_timer_msec(bm->master, (F), connection, 0,          \
					     (T));                                    \
	} while (0)

/* 定时器抖动量(毫秒)：在批量建立起连接时，给各 peer 的定时器加一点随机偏移，
 * 避免所有 peer 在同一瞬间齐步触发，导致 CPU 尖峰（惊群效应）。 */
#define BGP_MSEC_JITTER 10

/* Status codes for bgp_event_update() */
/* 【bgp_event_update() 的返回值】——告诉调用方“这次事件把 peer 怎么样了”：
 *   FSM_PEER_NOOP(0)        : 无变化（事件不适用当前状态，或动作没有副作用）。
 *   FSM_PEER_STOPPED(1)     : peer 已被停止（连接结束，可能进入 Idle）。
 *   FSM_PEER_TRANSFERRED(2) : 连接碰撞换心完成，peer 现在挂在另一条连接上。
 *   FSM_PEER_TRANSITIONED(3): 发生了状态迁移。
 * 调用方常据它决定“要不要继续持有/操作原来的 connection”。 */
#define FSM_PEER_NOOP           0
#define FSM_PEER_STOPPED        1
#define FSM_PEER_TRANSFERRED    2
#define FSM_PEER_TRANSITIONED   3

/* ========== Graceful Restart(RFC4724)相关宏：注意这是“配置状态”不是连接状态 ========== */
/* 【一组 GR 状态开关宏】围绕 peer->peer_gr_new_status_flag 的三个互斥标志位做文章：
 *   ENABLE(peer)/GR_ENABLE        : 本端作为“重启方”(RESTART)——我会优雅重启，
 *                                   请你在我重启期间保留我的路由。
 *   HELPER_ENABLE                 : 本端作为“帮助方”(HELPER)——对端重启时我保留它的路由。
 *   DISABLE                       : 两个都清掉 = 不参与 GR。
 *   GLOBAL_INHERIT_SET/UNSET      : 标成“跟随全局 GR 配置”（即邻居没显式配 GR）。
 * 实现细节：RESTART 与 HELPER 是**互斥**的，所以每个宏都是“置一个、清另一个”，
 * 不能只 SET 不 UNSET——否则会出现两个标志同时为真、语义未定义的中间态。 */
#define BGP_PEER_GR_HELPER_ENABLE(peer)	\
	do {		\
		UNSET_FLAG( \
			peer->peer_gr_new_status_flag,		\
			PEER_GRACEFUL_RESTART_NEW_STATE_RESTART);	\
		SET_FLAG( \
			peer->peer_gr_new_status_flag,	\
			PEER_GRACEFUL_RESTART_NEW_STATE_HELPER);\
	} while (0)

#define BGP_PEER_GR_ENABLE(peer)\
	do {				\
		SET_FLAG(   \
			peer->peer_gr_new_status_flag,	\
			PEER_GRACEFUL_RESTART_NEW_STATE_RESTART); \
		UNSET_FLAG( \
			peer->peer_gr_new_status_flag,	\
			PEER_GRACEFUL_RESTART_NEW_STATE_HELPER);\
	} while (0)

#define BGP_PEER_GR_DISABLE(peer)\
	do {				\
		UNSET_FLAG( \
			peer->peer_gr_new_status_flag,	\
			PEER_GRACEFUL_RESTART_NEW_STATE_RESTART);\
		UNSET_FLAG(\
			peer->peer_gr_new_status_flag, \
			PEER_GRACEFUL_RESTART_NEW_STATE_HELPER);\
	} while (0)

#define BGP_PEER_GR_GLOBAL_INHERIT_SET(peer) \
			SET_FLAG(peer->peer_gr_new_status_flag,	\
				PEER_GRACEFUL_RESTART_NEW_STATE_INHERIT)

#define BGP_PEER_GR_GLOBAL_INHERIT_UNSET(peer)	\
			UNSET_FLAG(peer->peer_gr_new_status_flag, \
				PEER_GRACEFUL_RESTART_NEW_STATE_INHERIT)

/* 【宏】BGP_PEER_GRACEFUL_RESTART_CAPABLE —— 双方都支持 GR 能力才成立。
 * 要求“我通告了 GR 能力(ADV)”且“对端通告了 GR 能力(RCV)”——能力是双向的，
 * 只看一边会误判。 */
#define BGP_PEER_GRACEFUL_RESTART_CAPABLE(peer)                                \
	(CHECK_FLAG(peer->cap, PEER_CAP_RESTART_ADV)                           \
	 && CHECK_FLAG(peer->cap, PEER_CAP_RESTART_RCV))

/* 【宏】BGP_PEER_RESTARTING_MODE —— 本 peer 此刻是否处于“重启方”角色。
 * 三个条件同时满足：配置了 PEER_FLAG_GRACEFUL_RESTART（本端要做重启方）、
 * 我发出的 GR 能力里置了 R 位(R_BIT_ADV，表示“我重启了/即将重启”)、
 * 且**对端没**置 R 位(RCV)。最后一条用来与“对端也说自己重启了”区分开——
 * 两边同时自认重启方是异常/冲突场景。 */
#define BGP_PEER_RESTARTING_MODE(peer)                                         \
	(CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART) &&                \
	 CHECK_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_ADV) &&         \
	 !CHECK_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_RCV))

/* 【宏】BGP_PEER_HELPER_MODE —— 本 peer 此刻是否处于“帮助方”角色。
 * 与 RESTARTING_MODE 镜像对称：配置了 GR_HELPER，且**对端**的 R 位收到(RCV)、
 * 我这边没置 R 位(ADV)。即“对端在重启，我来帮它保留路由”。 */
#define BGP_PEER_HELPER_MODE(peer)                                             \
	(CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART_HELPER) &&         \
	 CHECK_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_RCV) &&         \
	 !CHECK_FLAG(peer->cap, PEER_CAP_GRACEFUL_RESTART_R_BIT_ADV))

/* Prototypes. */

/*
 * Update FSM for peer based on whether we have valid nexthops or not.
 */
/* 【核心入口】按重要性排：
 *   bgp_event        : FSM 事件的统一入口（事件回调），内部转 bgp_event_update。
 *   bgp_event_update : 查 FSM 迁移表、执行动作、推进状态；返回值 FSM_PEER_*。
 *   bgp_stop         : 停机动作（发 NOTIFY / 清路由 / 转 Clearing 或 Idle）。
 *   bgp_timer_set    : 按当前状态统一设置该状态下应运行的定时器（连接/保持/重启）。
 *   bgp_fsm_change_status : 显式改状态并触发副作用（清路由、GR 通知等）。
 *   bgp_fsm_nht_update: 依据“下一跳是否有效”反过来影响 FSM（NHT 联动）。
 * 【路由通告相关】bgp_start_routeadv / bgp_adjust_routeadv —— MRAI 通告定时器；
 *   bgp_update_delay_end / bgp_maxmed_update —— 收敛期批量更新的延迟与 MED。
 * 【GR 一族】bgp_*_gr_* 是 GR 配置状态机（见文件头说明），与连接状态机无关。
 * 【HOOK】peer_backward_transition / peer_established 供 BMP 等外部模块挂钩
 *   peer 建立与状态回退事件。 */
/* 依据“下一跳是否有效”更新该 peer 的 FSM：NHT(下一跳跟踪)结果变化时被回调，
 * has_valid_nexthops 指示相关 NLRI 的下一跳是否可达，进而影响 peer 的可用性判定。 */
extern void bgp_fsm_nht_update(struct peer_connection *connection,
			       struct peer *peer, bool has_valid_nexthops);
/* FSM 事件回调入口(event 框架调用)，内部转交 bgp_event_update 处理状态迁移。 */
extern void bgp_event(struct event *event);
/* 停机事件回调：最终调用 bgp_stop_with_notify，先向对端发送 NOTIFY 再停机。 */
extern void bgp_event_stop_with_notify(struct event *event);
/* 处理一个 FSM 事件：查迁移表、执行动作、推进状态机，返回进度(bgp_fsm_state_progress)。 */
extern int bgp_event_update(struct peer_connection *connection,
			    enum bgp_fsm_events event);
/* 停机：发送 NOTIFY 并清理路由，把 peer 推进到 Clearing 或 Idle，返回停机进度。 */
extern enum bgp_fsm_state_progress bgp_stop(struct peer_connection *connection);
/* 依据当前状态统一设置该 state 下应运行的定时器(连接 / 保持 / 重连等)。 */
extern void bgp_timer_set(struct peer_connection *connection);
/* 路由通告(MRAI)定时器到期回调，触发向该 peer 的批量通告。 */
extern void bgp_routeadv_timer(struct event *event);
/* 显式把 peer 状态改为 status，并执行状态迁移副作用(清路由、GR 通知等)。 */
extern void bgp_fsm_change_status(struct peer_connection *connection,
				  enum bgp_fsm_status status);
/* peer 停机原因字符串表：以 last_reset 等为下标取值，供日志与展示使用。 */
extern const char *const peer_down_str[];
/* 结束 update-delay 收敛期：触发批量路由处理与 EOR 等收尾动作。 */
extern void bgp_update_delay_end(struct bgp *bgp);
/* 按配置触发或撤销 maxmed-on-startup(启动时提高 MED 以吸引流量)。 */
extern void bgp_maxmed_update(struct bgp *bgp);
/* 查询是否配置了 maxmed-on-startup。 */
extern bool bgp_maxmed_onstartup_configured(struct bgp *bgp);
/* 查询 maxmed-on-startup 当前是否处于生效状态。 */
extern bool bgp_maxmed_onstartup_active(struct bgp *bgp);
/* 由连接状态反推对应的 FSM 错误子码(用于产生 NOTIFY 与日志)。 */
extern int bgp_fsm_error_subcode(int status);
/* 与 bgp_stop 类似，但会携带 NOTIFY 的 code 与 sub_code 一并发送，返回停机进度。 */
extern enum bgp_fsm_state_progress
bgp_stop_with_notify(struct peer_connection *connection, uint8_t code,
		     uint8_t sub_code);
/* 启动 tier2(第二层，通常为从路由与汇总)延迟定时器，用于分层收敛。 */
extern void bgp_start_tier2_deferral_timer(struct bgp *bgp, afi_t afi, safi_t safi);

/* 初始收敛结束、退出只读模式后调用：为所有 peer 启动遵循 MRAI 的路由通告定时器。 */
/**
 * Start the route advertisement timer (that honors MRAI) for all the
 * peers. Typically called at the end of initial convergence, coming
 * out of read-only mode.
 */
extern void bgp_start_routeadv(struct bgp *bgp);

/* 判断是否需要为该 peer 提前触发通告定时器：若距上次发送已过很久，则无需再等周期
 * 到期，应立即可发，从而加速新前缀的传播。 */
/**
 * See if the route advertisement timer needs to be adjusted for a
 * peer. For example, if the last update was written to the peer a
 * long while back, we don't need to wait for the periodic advertisement
 * timer to expire to send the new set of prefixes. It should fire
 * instantly and updates should go out sooner.
 */
extern void bgp_adjust_routeadv(struct peer *peer);

#include "hook.h"
/* HOOK：供外部模块(如 BMP)挂钩 peer 的关键事件：
 *   peer_established         : peer 进入 Established 状态时触发。
 *   peer_backward_transition : peer 状态发生回退迁移时触发。 */
DECLARE_HOOK(peer_backward_transition, (struct peer *peer), (peer));
DECLARE_HOOK(peer_established, (struct peer *peer), (peer));

/* GR(优雅重启)配置与状态机一族：与连接 FSM 独立，管理本端与对端的 GR 模式。
 * 命令类(_enable / _disable / _inherit / _unset ...)修改配置并重算模式；
 * 查询类(_mode_get / print_*)读取当前模式或命令；
 * update_all 与 neighbor_graceful_restart 为对外入口；
 * action / move_to_gr_mode / flags_update 在运行期推进某个 peer 的 GR 状态。 */
int bgp_gr_update_all(struct bgp *bgp, enum global_gr_command global_gr_cmd);
int bgp_neighbor_graceful_restart(struct peer *peer,
				  enum peer_gr_command peer_gr_cmd);
unsigned int bgp_peer_gr_action(struct peer *peer, enum peer_mode old_peer_state,
				enum peer_mode new_peer_state);
void bgp_peer_move_to_gr_mode(struct peer *peer, enum peer_mode new_state);
unsigned int bgp_peer_gr_helper_enable(struct peer *peer);
unsigned int bgp_peer_gr_enable(struct peer *peer);
unsigned int bgp_peer_gr_global_inherit(struct peer *peer);
unsigned int bgp_peer_gr_disable(struct peer *peer);
enum peer_mode bgp_peer_gr_mode_get(struct peer *peer);
enum global_mode bgp_global_gr_mode_get(struct bgp *bgp);
enum peer_mode bgp_get_peer_gr_mode_from_flags(struct peer *peer);
unsigned int bgp_peer_gr_global_inherit_unset(struct peer *peer);
void bgp_peer_gr_flags_update(struct peer *peer);
const char *print_peer_gr_mode(enum peer_mode pr_mode);
const char *print_peer_gr_cmd(enum peer_gr_command pr_gr_cmd);
const char *print_global_gr_mode(enum global_mode gl_mode);
const char *print_global_gr_cmd(enum global_gr_command gl_gr_cmd);
/* 连接与 GR 收尾辅助：
 *   bgp_peer_connection_reg_with_nht : 把该连接登记到下一跳跟踪系统。
 *   bgp_gr_check_path_select         : GR 恢复时校验收敛后的路径选择是否完成。
 *   bgp_gr_start_all_deferral_timers : 为所有 AFI 与 SAFI 启动 GR 延迟定时器。
 *   bgp_rpki_cache_connected         : 查询 RPKI 缓存是否已连接(影响源验证)。 */
int bgp_peer_connection_reg_with_nht(struct peer_connection *connection);
void bgp_gr_check_path_select(struct bgp *bgp, afi_t afi, safi_t safi);
void bgp_gr_start_all_deferral_timers(struct bgp *bgp);
extern bool bgp_rpki_cache_connected(struct bgp *bgp);
#endif /* _QUAGGA_BGP_FSM_H */
