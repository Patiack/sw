// SPDX-License-Identifier: GPL-2.0-or-later
/* zebra connection and redistribute functions.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_zebra.h —— bgpd 与 zebra 之间的对外接口声明
 * ============================================================================
 * 【本文件是什么】
 *   bgp_zebra.c 的所有对外函数/宏/全局变量的声明集中于此。它定义了 bgpd 与
 *   zebra 交互的“契约面”：路由下发、下一跳跟踪登记、重分发、接口/地址查询、
 *   EVPN/VXLAN 通告、PBR/Flowspec、SRv6 与标签管理、GR 能力通告等入口。
 *
 * 【两个 zclient 句柄】
 *   bgp_zclient      ：主异步 zclient(此处 extern)，承载路由下发/事件回调；
 *   bgp_zclient_sync ：同步 zclient(见 bgp_zebra.c)，用于需要“请求-应答”式
 *                      同步交互的场景，如 label manager、table manager、SRv6 manager。
 *
 * 【关键宏】
 *   BGP_ORIGINAL_UPDATE：把一条 path 的“归属实例”写回 _bgp_orig。当该 path 是
 *     vrfleak(跨 VRF 泄漏)导入(BGP_ROUTE_IMPORTED)时取 bgp_orig(原始实例)，
 *     否则取传入实例。用在下发时确定“这条路由代表哪个 VRF”。
 *   BGP_ZEBRA_DEFAULT_NHOP_WEIGHT：加权 ECMP 时下一跳的默认权重(1)。
 *
 * 【辅助内联】bgp_zebra_announce_eligible：判断一条 path 是否“有资格”下发给
 *   zebra——须为 ZEBRA_ROUTE_BGP 且 subtype 属于 NORMAL / AGGREGATE / IMPORTED。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_ZEBRA_H
#define _QUAGGA_BGP_ZEBRA_H

/* vxlan.h：EVPN/VXLAN 相关类型(vni_t、struct bgpevpn 等)的定义。 */
#include "vxlan.h"

/* 【全局】bgp_zclient —— bgpd 的主 zclient 会话句柄(异步)。所有路由下发与 zebra
 * 事件回调都经它收发。 */
/* The global zapi session handle */
extern struct zclient *bgp_zclient;

/* 【宏】BGP_ORIGINAL_UPDATE —— 计算一条 path 真正的“归属 BGP 实例”：若是 vrfleak
 * 导入(BGP_ROUTE_IMPORTED 且带 bgp_orig)则取原始实例，否则取传入实例。 */
/* Macro to update bgp_original based on bpg_path_info */
#define BGP_ORIGINAL_UPDATE(_bgp_orig, _mpinfo, _bgp)                          \
	((_mpinfo->extra && _mpinfo->extra->vrfleak &&                        \
	  _mpinfo->extra->vrfleak->bgp_orig &&                                \
	  _mpinfo->sub_type == BGP_ROUTE_IMPORTED)                             \
		 ? (_bgp_orig = _mpinfo->extra->vrfleak->bgp_orig)            \
		 : (_bgp_orig = _bgp))

/* 【宏】BGP_ZEBRA_DEFAULT_NHOP_WEIGHT —— 加权 ECMP 时的默认下一跳权重。 */
/* Default weight for next hop, if doing weighted ECMP. */
#define BGP_ZEBRA_DEFAULT_NHOP_WEIGHT 1

/* 【内联函数】bgp_zebra_announce_eligible —— 判断一条 path 是否有资格下发给 zebra。
 * 条件：type 为 ZEBRA_ROUTE_BGP 且 subtype 属于 NORMAL / AGGREGATE / IMPORTED。
 * (从其它协议重分发进来的 subtype 不应由 BGP 反向下发。) */
/*
 * Check if the path is eligible for announcing to zebra.
 */
static inline bool bgp_zebra_announce_eligible(struct bgp_path_info *pi)
{
	return (pi->type == ZEBRA_ROUTE_BGP) &&
	       ((pi->sub_type == BGP_ROUTE_NORMAL) || (pi->sub_type == BGP_ROUTE_AGGREGATE) ||
		(pi->sub_type == BGP_ROUTE_IMPORTED));
}

/* 【初始化/销毁】bgp_zebra_init / bgp_if_init / bgp_zebra_destroy —— 建立两个 zclient、
 * 注册回调与接口 hook、进程退出时释放。init_tm_connect / tm_get_id / tm_chunk_obtained /
 * get_table_range 是 table-manager(Flowspec 表 ID 区间)相关。 */
extern void bgp_zebra_init(struct event_loop *master, unsigned short instance);
extern void bgp_if_init(void);
extern void bgp_zebra_init_tm_connect(struct bgp *bgp);
extern uint32_t bgp_zebra_tm_get_id(void);
extern bool bgp_zebra_tm_chunk_obtained(void);
extern void bgp_zebra_destroy(void);
extern int bgp_zebra_get_table_range(struct zclient *zc, uint32_t chunk_size,
				     uint32_t *start, uint32_t *end);
extern int bgp_if_update_all(void);
/* 【路由下发】bgp_zebra_route_install —— 单条路由下发/撤销入口(异步队列);
 * announce_table / _all_subtypes / withdraw_table_all_subtypes —— 整表(或任意 subtype)
 * 批量下发/撤销; update_srv6_encap_routes —— SRv6 封装路由的增删。 */
extern void bgp_zebra_route_install(struct bgp_dest *dest,
				    struct bgp_path_info *path, struct bgp *bgp,
				    bool install, struct bgpevpn *vpn,
				    bool is_sync);
extern void bgp_zebra_announce_table(struct bgp *bgp, afi_t afi, safi_t safi);

/* Announce routes of any bgp subtype of a table to zebra */
extern void bgp_zebra_announce_table_all_subtypes(struct bgp *bgp, afi_t afi,
						  safi_t safi);

/* Withdraw all entries of any subtype in a BGP instances RIB table from Zebra */
extern void bgp_zebra_withdraw_table_all_subtypes(struct bgp *bgp, afi_t afi,
						  safi_t safi);

/* withdraw or add srv6 encapsulated routes */
extern void bgp_zebra_update_srv6_encap_routes(struct bgp *bgp, afi_t afi, struct bgp *from_bgp,
					       bool add);

/* 【RA】initiate_radv / terminate_radv —— 为某 peer 启动/停止接口 IPv6 RA
 * (无编号邻居、RFC5549 ENHE 场景需要)。 */
extern void bgp_zebra_initiate_radv(struct bgp *bgp, struct peer *peer);
extern void bgp_zebra_terminate_radv(struct bgp *bgp, struct peer *peer);

/* 【实例注册】instance_register / instance_deregister —— 把某 BGP 实例(默认或 VRF)
 * 注册/注销到 zebra。 */
extern void bgp_zebra_instance_register(struct bgp *bgp);
extern void bgp_zebra_instance_deregister(struct bgp *bgp);

/* 【重分发】redist_lookup / redist_add —— 查找/新建 bgp_redist(按 afi/type/instance);
 * redistribute_set / _resend / _rmap_set / _metric_set —— 打开/重发/设置 route-map 与
 * metric; redistribute_unset / _unreg —— 取消登记; redistribute_redo —— 全量重做。
 * redistribute 采用引用计数式登记到 zebra。 */
extern void bgp_redistribute_redo(struct bgp *bgp);
extern struct bgp_redist *bgp_redist_lookup(struct bgp *bgp, afi_t afi,
					    uint8_t type,
					    unsigned short instance);
extern struct bgp_redist *bgp_redist_add(struct bgp *bgp, afi_t afi,
					 uint8_t type, unsigned short instance);
extern int bgp_redistribute_set(struct bgp *bgp, afi_t afi, int type,
				unsigned short instance, bool changed);
extern int bgp_redistribute_resend(struct bgp *bgp, afi_t afi, int type,
				   unsigned short instance);
extern bool bgp_redistribute_rmap_set(struct bgp_redist *red, const char *name,
				      struct route_map *route_map);
extern bool bgp_redistribute_metric_set(struct bgp *bgp, struct bgp_redist *red,
					afi_t afi, int type, uint32_t metric);
extern void bgp_redistribute_unset(struct bgp *bgp, afi_t afi, int type,
				   unsigned short instance);
extern int bgp_redistribute_unreg(struct bgp *bgp, afi_t afi, int type,
				  unsigned short instance);

/* 【接口/地址查找】if_lookup_by_ipv4(_exact) / by_ipv6(_exact) —— 按地址(可选精确
 * 匹配)在指定 VRF 内查找接口。 */
extern struct interface *if_lookup_by_ipv4(struct in_addr *addr,
					   vrf_id_t vrf_id);
extern struct interface *if_lookup_by_ipv4_exact(struct in_addr *addr,
						 vrf_id_t vrf_id);
extern struct interface *if_lookup_by_ipv6(struct in6_addr *addr,
					   ifindex_t ifindex, vrf_id_t vrf_id);
extern struct interface *if_lookup_by_ipv6_exact(struct in6_addr *addr,
						 ifindex_t ifindex,
						 vrf_id_t vrf_id);
/* 【EVPN/VXLAN 通告】advertise_subnet / _gw_macip / _svi_macip / _all_vni、
 * dup_addr_detection、vxlan_flood_control —— 通知 zebra 开关 EVPN 子网/网关 MACIP、
 * 学习所有 VNI、重复地址检测、BUM 洪泛控制等。 */
extern int bgp_zebra_advertise_subnet(struct bgp *bgp, int advertise,
				      vni_t vni);
extern int bgp_zebra_advertise_gw_macip(struct bgp *bgp, int advertise,
					vni_t vni);
extern int bgp_zebra_advertise_svi_macip(struct bgp *bgp, int advertise,
					 vni_t vni);
extern int bgp_zebra_advertise_all_vni(struct bgp *bgp, int advertise);
extern int bgp_zebra_dup_addr_detection(struct bgp *bgp);
extern int bgp_zebra_vxlan_flood_control(struct bgp *bgp, struct bgpevpn *evpn);

/* 【连接统计 & 下一跳推导】num_connects —— 累计连接次数;
 * bgp_zebra_nexthop_set —— 由本地/远端套接字推导邻居下一跳。 */
extern int bgp_zebra_num_connects(void);

extern bool bgp_zebra_nexthop_set(union sockunion *local,
				  union sockunion *remote,
				  struct bgp_nexthop *nexthop,
				  struct peer *peer);
/* 【PBR/Flowspec 下发】前向声明数据结构 + bgp_send_pbr_rule_action / _ipset_match /
 * _ipset_entry_match / _iptable —— 向 zebra 下发/撤销 ip rule / ipset / ipset 表项 /
 * iptable 规则。 */
struct bgp_pbr_action;
struct bgp_pbr_match;
struct bgp_pbr_rule;
struct bgp_pbr_match_entry;

extern void bgp_send_pbr_rule_action(struct bgp_pbr_action *pbra,
				     struct bgp_pbr_rule *pbr,
				     bool install);
extern void bgp_send_pbr_ipset_match(struct bgp_pbr_match *pbrim,
				     bool install);
extern void bgp_send_pbr_ipset_entry_match(struct bgp_pbr_match_entry *pbrime,
				    bool install);
extern void bgp_send_pbr_iptable(struct bgp_pbr_action *pba,
			  struct bgp_pbr_match *pbm,
			  bool install);

/* 【默认路由 & 能力通告】announce_default —— 向指定表注入默认路由;
 * send_capabilities / update / stale_timer_update —— 向 zebra 声明 GR 能力、更新进度
 * 状态、RIB stale 时间。 */
extern void bgp_zebra_announce_default(struct bgp *bgp, struct nexthop *nh,
				afi_t afi, uint32_t table_id, bool announce);
extern int bgp_zebra_send_capabilities(struct bgp *bgp, bool disable);
extern int bgp_zebra_update(struct bgp *bgp, afi_t afi, safi_t safi,
			    enum zserv_client_capabilities type);
extern int bgp_zebra_stale_timer_update(struct bgp *bgp);
/* 【SRv6 manager 辅助】get/release_locator_chunk、get_locator、request/release_srv6_sid
 * —— 通过同步 zclient 向 SRv6 manager 申请/释放 locator chunk、查询 locator、
 * 申请/释放 SID。 */
extern int bgp_zebra_srv6_manager_get_locator_chunk(const char *name);
extern int bgp_zebra_srv6_manager_release_locator_chunk(const char *name);
extern int bgp_zebra_srv6_manager_get_locator(const char *name);
extern bool bgp_zebra_request_srv6_sid(const struct srv6_sid_ctx *ctx,
				       struct in6_addr *sid_value,
				       const char *locator_name,
				       uint32_t *sid_func);
extern void bgp_zebra_release_srv6_sid(const struct srv6_sid_ctx *ctx, const char *locator_name);

/* 【标签/LSP & 标签池】send_nexthop_label —— 直接下发/撤销“标签到下一跳”的 MPLS 映射;
 * request_label_range / release_label_range —— 通过同步 zclient 向标签管理器申请/释放
 * 标签区间。 */
extern void bgp_zebra_send_nexthop_label(int cmd, mpls_label_t label,
					 ifindex_t index, vrf_id_t vrfid,
					 enum lsp_types_t ltype,
					 struct prefix *p, uint8_t num_labels,
					 mpls_label_t out_labels[]);
extern bool bgp_zebra_request_label_range(uint32_t base, uint32_t chunk_size,
					  bool label_auto);
extern void bgp_zebra_release_label_range(uint32_t start, uint32_t end);
/* 【其余导出】withdraw_actual / announce_actual —— 真正的单条路由下发/撤销编码入口;
 * process_remote_routes_for_l2vni —— 定时处理 L2VNI 远端路由; if_get_ipv6_global ——
 * 取接口第一个全局 IPv6 地址; update_fib_install_pending —— 设置/清除某前缀的 FIB
 * 安装挂起标志。 */
extern enum zclient_send_status
bgp_zebra_withdraw_actual(struct bgp_dest *dest, struct bgp_path_info *info,
			  struct bgp *bgp);
extern void bgp_zebra_process_remote_routes_for_l2vni(struct event *e);
extern int if_get_ipv6_global(struct interface *ifp, struct in6_addr *addr);
extern enum zclient_send_status
bgp_zebra_announce_actual(struct bgp_dest *dest, struct bgp_path_info *info, struct bgp *bgp);
extern void bgp_zebra_update_fib_install_pending(struct bgp_dest *dest, struct bgp *bgp,
						 bool install);
#endif /* _QUAGGA_BGP_ZEBRA_H */
