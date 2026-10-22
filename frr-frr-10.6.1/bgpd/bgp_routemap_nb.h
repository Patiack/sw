// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020        Vmware
 *                           Sarita Patra
 */

/* ============================================================================
 * 【文件导读】bgp_routemap_nb.h —— BGP route-map 的 northbound(NB) 配置回调原型声明
 * ============================================================================
 * 【职责与定位】本文件是 bgpd 侧 NB 配置层的**对外原型头部**：声明
 *   frr_bgp_route_map_info（本模块的 YANG 模块注册信息）以及全部
 *   lib_route_map_entry_ 前缀的回调函数原型。真正的实现在 bgp_routemap_nb_config.c。
 *
 * 【命名规则（读懂本文件的关键）】每个回调名由“YANG 节点路径”逐段拼成：
 *   lib_route_map_entry_ <match_condition 或 set_action> _rmap_
 *     <match_condition 或 set_action> _ <参数名> _ <动作>
 *   例如 lib_route_map_entry_match_condition_rmap_match_condition_origin_modify
 *   对应“route-map 某 entry 的 match 子句里的 origin 参数”。名字很长但可逐段拆读。
 *
 * 【回调动作后缀】NB 框架按“配置变更语义”调用不同后缀：
 *   _create  : 创建该配置实例（叶子或列表项首次出现）。
 *   _modify  : 修改已有值。
 *   _destroy : 删除。
 *   _finish  : apply_finish，整棵子树配置应用完后做汇总处理（如重建 list）。
 *   入参统一为 struct nb_cb_ 相应动作的 args；返回值遵循 NB 约定（如 NB_OK / NB_ERR
 *   / CMD_WARNING）。注意：本文件里 finish 类原型返回类型是 void，阅读时以 *_config.c
 *   中的实际实现为准。
 *
 * 【易错点 / 阅读建议】这些原型与 bgp_routemap_nb_config.c、以及 YANG 模型三者必须
 *   严格一一对应；若改了 YANG 节点名，这里和 *_config.c 都要同步改。建议先在
 *   *_config.c 里看一两个实现，再回来对照本文件，能立刻理解命名规律。
 *
 * 建议阅读顺序：frr_bgp_route_map_info → 任选一组 match 回调原型
 *               → 任选一组 set 回调原型 → 回 *_config.c 看实现。
 * ============================================================================
 */

#ifndef _FRR_BGP_ROUTEMAP_NB_H_
#define _FRR_BGP_ROUTEMAP_NB_H_

#ifdef __cplusplus
extern "C" {
#endif

extern const struct frr_yang_module_info frr_bgp_route_map_info;

/* prototypes */

/* 【分组一：match condition 回调】对应 YANG 中 route-map entry 下的 match 子句各参数。
 * 多数参数是“简单叶子”，只有 _modify / _destroy 两个回调；少数（如 community-list、
 * evpn default-route）还有 _create / _finish，见下方各自分组。以下按 YANG 中出现顺序
 * 罗列：vpn_dataplane、local_preference、alias、script、origin、rpki、rpki_extcommunity、
 * source_protocol、probability、source_vrf、peer 系列地址/接口、access_list、list_name、
 * evpn 系列、route_distinguisher、aspath_count、community/extcommunity 的 limit 等。
 * 注意：每个函数只声明原型，参数统一是 struct nb_cb_modify_args 或 nb_cb_destroy_args。 */
int lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_local_preference_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_local_preference_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_alias_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_alias_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_script_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_script_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_origin_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_origin_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_rpki_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_rpki_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_rpki_extcommunity_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_rpki_extcommunity_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_source_protocol_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_source_protocol_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_probability_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_probability_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_source_vrf_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_source_vrf_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv4_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv4_address_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_interface_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_interface_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv6_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv6_address_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_local_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_peer_local_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv4_address_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv4_address_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_interface_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_interface_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv6_address_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv6_address_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_access_list_num_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_access_list_num_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_access_list_num_extended_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_access_list_num_extended_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_list_name_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_list_name_destroy(struct nb_cb_destroy_args *args);
/* 【分组：match evpn 系列】针对 EVPN 路由的匹配：evpn_default_route（默认路由、
 * 用 create/destroy 表达有无）、evpn_vni、evpn_route_type、route_distinguisher。
 * 其中 default_route 无参数，用 _create / _destroy 表示这个匹配开关的开关状态。 */
int lib_route_map_entry_match_condition_rmap_match_condition_evpn_default_route_create(struct nb_cb_create_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_evpn_default_route_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_evpn_vni_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_evpn_vni_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_evpn_route_type_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_evpn_route_type_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_route_distinguisher_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_route_distinguisher_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_aspath_count_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_aspath_count_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_community_limit_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_community_limit_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_extcommunity_limit_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_extcommunity_limit_destroy(
	struct nb_cb_destroy_args *args);
/* 【分组：match community-list】comm_list 是一个子列表（可含多个 comm_list_name），
 * 故有 _create / _destroy，并在整棵子树应用完后用 _finish 统一把各 name 组装成
 * 内部匹配用的 list。其下的 comm_list_name 及其 exact_match / any 叶子各自有
 * modify / destroy 回调。 */
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_create(
	struct nb_cb_create_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_destroy(
	struct nb_cb_destroy_args *args);
void lib_route_map_entry_match_condition_rmap_match_condition_comm_list_finish(struct nb_cb_apply_finish_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_exact_match_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_exact_match_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_any_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_any_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_ipv4_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_ipv4_address_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_ipv6_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_match_condition_rmap_match_condition_ipv6_address_destroy(struct nb_cb_destroy_args *args);
/* 【分组二：set action 回调】对应 YANG 中 route-map entry 下的 set 子句各参数。
 * 多数是简单叶子（distance、preference、weight、metric/local-pref、origin、label_index、
 * ipv4_address、ipv4_nexthop、ipv6_address、table、aigp_metric 等），只有 modify / destroy。
 * 少数复合参数（aggregator、atomic_aggregate、extcommunity_lb）带 create / finish，
 * 已在下方各自分组标注。命名规律同 match：set_action_rmap_set_action_<参数名>_<动作>。 */
int lib_route_map_entry_set_action_rmap_set_action_distance_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_distance_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_rt_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_rt_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_nt_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_nt_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_soo_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_soo_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_ipv4_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_ipv4_address_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_ipv4_nexthop_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_ipv4_nexthop_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_ipv6_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_ipv6_address_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_preference_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_preference_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_label_index_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_label_index_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_local_pref_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_local_pref_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_weight_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_weight_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_origin_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_origin_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_originator_id_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_originator_id_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_table_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_table_destroy(struct nb_cb_destroy_args *args);
/* 【分组：set atomic-aggregate】无参数开关，用 create / destroy 表示“是否设置
 * atomic-aggregate 属性”（在聚合路由场景会置 BGP ATOMIC_AGGREGATE 属性位）。 */
int lib_route_map_entry_set_action_rmap_set_action_atomic_aggregate_create(struct nb_cb_create_args *args);
int lib_route_map_entry_set_action_rmap_set_action_atomic_aggregate_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_aigp_metric_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_aigp_metric_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_prepend_as_path_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_prepend_as_path_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_last_as_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_last_as_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_exclude_as_path_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_exclude_as_path_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_replace_as_path_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_replace_as_path_destroy(
	struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_community_none_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_community_none_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_community_string_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_community_string_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_large_community_none_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_large_community_none_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_large_community_string_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_large_community_string_destroy(struct nb_cb_destroy_args *args);
/* 【分组：set aggregator】含 aggregator_asn 与 aggregator_address 两个叶子，
 * 故有 _create / _destroy 与 _finish；_finish 在两叶子都配好后统一写回内部
 * aggregator 属性（AS 号 + 地址）。 */
int lib_route_map_entry_set_action_rmap_set_action_aggregator_create(
	struct nb_cb_create_args *args);
int lib_route_map_entry_set_action_rmap_set_action_aggregator_destroy(
	struct nb_cb_destroy_args *args);
void lib_route_map_entry_set_action_rmap_set_action_aggregator_finish(struct nb_cb_apply_finish_args *args);
int lib_route_map_entry_set_action_rmap_set_action_aggregator_aggregator_asn_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_aggregator_aggregator_address_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_comm_list_num_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_comm_list_num_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_comm_list_num_extended_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_comm_list_num_extended_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_comm_list_name_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_comm_list_name_destroy(struct nb_cb_destroy_args *args);
/* 【分组：set extcommunity lb（链路带宽扩展团体）】含 lb_type、bandwidth、
 * two_octet_as_specific 等叶子，有 _create / _destroy 与 _finish；_finish 负责把
 * type / bandwidth / AS 等拼成内部的 link-bandwidth extended community。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_create(
	struct nb_cb_create_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_destroy(
	struct nb_cb_destroy_args *args);
void lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_finish(struct nb_cb_apply_finish_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_lb_type_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_bandwidth_modify(struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_bandwidth_destroy(struct nb_cb_destroy_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_two_octet_as_specific_modify(struct nb_cb_modify_args *args);
/* 【回调】set extcommunity none 的 modify 与 destroy：设置或清除“无扩展团体”标志。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_none_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_none_destroy(
	struct nb_cb_destroy_args *args);
/* 【回调】set evpn gateway-ip ipv4 的 modify 与 destroy：设置或清除 EVPN 网关的
 * IPv4 地址。 */
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv4_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv4_destroy(
	struct nb_cb_destroy_args *args);
/* 【回调】set evpn gateway-ip ipv6 的 modify 与 destroy：设置或清除 EVPN 网关的
 * IPv6 地址。 */
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv6_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv6_destroy(
	struct nb_cb_destroy_args *args);
/* 【回调】set extcommunity color 的 modify 与 destroy：设置或清除着色扩展团体
 * （color community，用于 VPN 的着色与引流）。 */
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_color_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_extcommunity_color_destroy(
	struct nb_cb_destroy_args *args);
/* 【回调】set l3vpn nexthop encapsulation 的 modify 与 destroy：设置或清除
 * L3VPN 下一跳封装类型（如 SRv6 / MPLS 封装）。 */
int lib_route_map_entry_set_action_rmap_set_action_l3vpn_nexthop_encapsulation_modify(
	struct nb_cb_modify_args *args);
int lib_route_map_entry_set_action_rmap_set_action_l3vpn_nexthop_encapsulation_destroy(
	struct nb_cb_destroy_args *args);

#ifdef __cplusplus
}
#endif

#endif
