// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Nexthop tracking
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_nht.h —— NHT(Next Hop Tracking，下一跳跟踪)的对外接口
 * ============================================================================
 * 【NHT 是什么、为什么要它】
 *   BGP 学来的路由能不能用，取决于它的**下一跳是否真的可达**——而这件事只有
 *   IGP/直连路由知道，即 zebra 知道。于是 bgpd 做两件事：
 *     1) 用 bgp_find_or_add_nexthop() 把一个下一跳“登记”给 zebra，订阅它的变化；
 *     2) zebra 变化时通过 bgp_nexthop_update() 回调进来，更新 bgp_nexthop_cache
 *        (简称 bnc)，再 evaluate_paths() 重算受影响的路径有效性与最佳路径。
 *   没有 NHT，BGP 会把下一跳不可达的路由当有效路由下发，形成黑洞。
 *
 * 【核心对象与关系】
 *   struct bgp_nexthop_cache (bnc，见 bgp_nexthop.h) —— 一个“下一跳”一份缓存，
 *     挂在 bgp 的 nexthop_cache 红黑树上。字段含义见 bgp_nexthop.h。
 *   bnc <-> bgp_path_info —— 通过 path_nh_map(path, bnc, make) 建立/拆除的
 *     多对多映射：多条路径可以共用同一个下一跳，所以一个 bnc 的变动会波及
 *     一串路径。这是阅读本主线时最需要抓住的“扇出”关系。
 *
 * 【分层】本头文件是 NHT 的“逻辑层”接口（bgp_nht.c）：
 *   登记/注销、zebra 回调、路径-下一跳映射、接口事件、ENHE 接口 RA 注册。
 *   而 bgp_nexthop.h/.c 是“数据结构与扫描层”（bnc 的增删查、直连/地址表、
 *   multiaccess 判断、next-hop-self 判断）。两者配合看。
 *
 * 【HOOK】bgp_nht_path_update：路径因 NHT 而变有效/无效时触发，供外部模块
 *   （如 BMP、EVPN 相关逻辑）挂钩，不用侵入 NHT 主体。
 * ============================================================================
 */

#ifndef _BGP_NHT_H
#define _BGP_NHT_H

/**
 * bgp_nexthop_update() - process a nexthop update message from Zebra.
 */
/* 【函数】bgp_nexthop_update —— zebra 通知“某下一跳的解析结果变了”的回调入口。
 * 调用时机：zebra 的 ZEBRA_NEXTHOP_UPDATE 消息到达时（bgp_zebra.c 转发进来）。
 * 参数：vrf 哪个 VRF；match 被跟踪的下一跳前缀；nhr zebra 给出的解析结果
 *       （可达性、IGP metric、实际下一跳列表、是否直连等）。
 * 处理：找到对应 bnc → 更新 flags/metric/nexthop → evaluate_paths() 重算。
 * 注意 vrf 与 bgp 实例需先对应上（多 VRF 场景下不能串台）。 */
extern void bgp_nexthop_update(struct vrf *vrf, struct prefix *match,
			       struct zapi_route *nhr);

/**
 * bgp_find_or_add_nexthop() - lookup the nexthop cache table for the bnc
 *  object. If not found, create a new object and register with ZEBRA for
 *  nexthop notification.
 * ARGUMENTS:
 *   bgp_route - BGP instance of route
 *   bgp_nexthop - BGP instance of nexthop
 *   a - afi: AFI_IP or AF_IP6
 *   safi - safi: to check which table nhs are being imported to
 *   p - path for which the nexthop object is being looked up
 *   peer - The BGP peer associated with this NHT
 *   connected - True if NH MUST be a connected route
 */
/* 【函数】bgp_find_or_add_nexthop —— 查找或新建 bnc，并在需要时向 zebra 登记。
 * 参数要点：
 *   bgp_route  / bgp_nexthop : 路由所属实例 与 下一跳所属实例 **可能不同**
 *       （典型：import vrf / MPLS-VPN 场景，路由在一个 VRF、下一跳在另一个），
 *       这是本函数最容易看错的地方，不要想当然认为二者相同。
 *   p / peer / source_pi     : 使用该下一跳的路径、对端；用于建立 path-nh 映射
 *                              以及判断是否需要 NHT（如对端自环、iBGP 多跳等）。
 *   connected                : true 表示**要求**该下一跳必须是直连路由
 *                              （eBGP 单跳场景的严格校验）。
 *   orig_prefix              : 原始前缀（EVPN/某些场景需要）。
 * 返回：0 表示“该路径的下一跳需要 NHT 且已登记/找到”；非 0 表示无需跟踪或失败。
 * 副作用：可能新建 bnc、向 zebra 发 nexthop register、建立 path_nh_map。 */
extern int bgp_find_or_add_nexthop(struct bgp *bgp_route, struct bgp *bgp_nexthop, afi_t a,
				   safi_t safi, struct bgp_path_info *p, struct peer *peer,
				   int connected, const struct prefix *orig_prefix,
				   struct bgp_path_info *source_pi);

/**
 * bgp_unlink_nexthop() - Unlink the nexthop object from the path structure.
 * ARGUMENTS:
 *   p - path structure.
 */
/* 【函数】拆除路径与下一跳的关联】
 *   bgp_unlink_nexthop(p)              : 拆掉单条路径 p 与它所用 bnc 的映射；
 *                                        若该 bnc 已无任何路径引用，会一并注销/释放。
 *   bgp_unlink_nexthop_by_peer(peer)   : 拆掉**该 peer 下所有路径**的映射——
 *                                        peer 关闭/删除时批量清理用。
 *   bgp_replace_nexthop_by_peer(from,to): 连接碰撞换心时，把旧连接上的 NHT 关联
 *                                        整体迁到新连接（否则换心后下一跳跟踪就断了）。 */
extern void bgp_unlink_nexthop(struct bgp_path_info *p);
void bgp_unlink_nexthop_by_peer(struct peer *peer);
void bgp_replace_nexthop_by_peer(struct peer_connection *from, struct peer_connection *to);
/**
 * bgp_delete_connected_nexthop() - Reset the 'peer' pointer for a connected
 * nexthop entry. If no paths reference the nexthop, it will be unregistered
 * and freed.
 * ARGUMENTS:
 *   afi - afi: AFI_IP or AF_IP6
 *   peer - Ptr to peer
 */
/* 【函数】bgp_delete_connected_nexthop —— 把某 bnc 上的 peer 指针清空。
 * 场景：peer 走了，但 bnc 可能还被别的路径用着，不能直接删。若清空后发现
 * 已无路径引用该 bnc，再注销与释放。
 * 【为什么会有 peer 指针指向 bnc】直连场景下 bnc 与 peer 绑定（记录“这个直连
 * 下一跳属于哪个邻居”），用于 multiaccess 判断和 next-hop-self 处理。 */
extern void bgp_delete_connected_nexthop(afi_t afi, struct peer *peer);

/*
 * Cleanup nexthop registration and status information for BGP nexthops
 * pertaining to this VRF. This is invoked upon VRF deletion.
 */
/* 【函数】bgp_cleanup_nexthops —— VRF/实例删除时的兜底清理：注销该实例下所有
 * 下一跳登记并清掉状态，防止把已经不存在实例的 bnc 留在树里（悬垂）。 */
extern void bgp_cleanup_nexthops(struct bgp *bgp);

/*
 * Add or remove the tracking of the bgp_path_info that
 * uses this nexthop
 */
/* 【函数】path_nh_map —— 建立或拆除 “路径 ↔ 下一跳缓存” 的映射（本主线的枢纽）。
 *   make = true  : 把 path 加入 bnc->paths 链表，bnc->path_count++；
 *   make = false : 从链表摘除并可能触发 bnc 的回收。
 * 因为一个 bnc 会被多条路径共享，bnc 的状态一变就要沿这条链表扇出到所有路径，
 * 所以这份映射的准确性直接决定“下一跳变化后哪些路由会被重算”。 */
extern void path_nh_map(struct bgp_path_info *path,
			struct bgp_nexthop_cache *bnc, bool make);
/*
 * When we actually have the connection to
 * the zebra daemon, we need to reregister
 * any nexthops we may have sitting around
 */
/* 【函数】bgp_nht_register_nexthops —— zebra 连接建立后，把之前积压的 bnc
 * 重新登记给 zebra。
 * 为什么需要：bgpd 可能先于 zebra 就绪；或 zebra 重启导致登记全丢。此时连接
 * 一恢复就要“补登记”，否则 BGP 会一直认为下一跳无状态（路由不生效）。 */
extern void bgp_nht_register_nexthops(struct bgp *bgp);

/*
 * When we have the the PEER_FLAG_CAPABILITY_ENHE flag
 * set on a peer *after* it has been brought up we need
 * to notice and setup the interface based RA,
 * this code can walk the registered nexthops and
 * register the important ones with zebra for RA.
 */
/* 【函数】ENHE(RFC5549 扩展下一跳编码)相关的接口 RA 登记/注销。
 * 场景：PEER_FLAG_CAPABILITY_ENHE 是在 peer **已经 up 之后**才被置上的（动态
 * 能力协商），此时需要回头把相关接口按 RA 重新登记给 zebra，否则对端发的
 * IPv6 下一跳 + IPv4 前缀这种组合解析不了。 */
extern void bgp_nht_reg_enhe_cap_intfs(struct peer *peer);
extern void bgp_nht_dereg_enhe_cap_intfs(struct peer *peer);
/* 【函数】evaluate_paths —— bnc 状态变化后的“扇出点”：
 * 遍历 bnc->paths 里的每条路径，重新判断其有效性/是否可参与选路，必要时
 * 触发 bgp_process() 重算最佳路径、并（通过 hook）通知外部模块。
 * 这是 NHT → 路由表 的唯一通路，排障时看“下一跳变了但路由没变”就往这里查。 */
extern void evaluate_paths(struct bgp_nexthop_cache *bnc);

/* 【函数】接口 up/down 事件对 NHT 的影响。
 * 下一跳常是“接口上的地址”，接口没了下一跳自然失效；这两个钩子把接口状态变化
 * 翻译成对应 bnc 的有效性变化，避免只依赖 zebra 的 nexthop 消息（zebra 未必
 * 每次接口抖动都发 nexthop update）。 */
extern void bgp_nht_ifp_up(struct interface *ifp);
extern void bgp_nht_ifp_down(struct interface *ifp);

/* 【函数】bgp_nht_interface_events —— peer 状态变化（尤其 up）时，重新评估该
 * peer 相关的接口事件/下一跳跟踪关系。用于 peer 从不可用变可用后“补课”。 */
extern void bgp_nht_interface_events(struct peer *peer);

/* called when a path becomes valid or invalid, because of nexthop tracking */
DECLARE_HOOK(bgp_nht_path_update, (struct bgp *bgp, struct bgp_path_info *pi, bool valid),
	     (bgp, pi, valid));

#endif /* _BGP_NHT_H */
