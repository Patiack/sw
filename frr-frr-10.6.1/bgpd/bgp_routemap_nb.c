// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020        Vmware
 *                           Sarita Patra
 */


/* ============================================================================
 * 【文件导读】bgp_routemap_nb.c —— BGP route-map 的 NB(YANG) 节点与回调映射表
 * ============================================================================
 * 【职责与定位】本文件只有一个实质内容：定义 frr_bgp_route_map_info——一张
 *   “YANG 节点 xpath 到 NB 回调函数”的注册表。FRR 的 northbound 框架加载
 *   frr-bgp-route-map 模块时读这张表，把每个配置节点绑定到对应回调。回调本体全部
 *   实现在 bgp_routemap_nb_config.c，原型在 bgp_routemap_nb.h。本文件不写逻辑。
 *
 * 【表的结构】frr_bgp_route_map_info.nodes 是一个以 { .xpath = NULL } 结尾的数组，
 *   每个元素 = 一个 xpath + 一个 .cbs 回调集合。回调键含义见下：
 *     .modify       —— 该节点值被修改时调用（叶子参数最常见）。
 *     .destroy      —— 节点被删除时调用。
 *     .create       —— 节点被首次创建时调用（无参开关或列表项用）。
 *     .apply_finish —— 整棵子树配置应用完毕后调用一次，用于汇总（如把 comm-list
 *                     的各个元素组装成内部 list）。
 *
 * 【xpath 的共同前缀】都形如
 *   /frr-route-map:lib/route-map/entry/<match-condition 或 set-action>/rmap-.../
 *   frr-bgp-route-map:<参数名>
 *   即：框架通用模型 frr-route-map 定义 entry 及其 match 子句 / set 子句骨架，
 *   参数本身由本 BGP 模块 frr-bgp-route-map 以 augment 方式扩展。
 *
 * 【易错点 / 阅读建议】表里元素的**书写顺序**并不严格等于 xpath 的分类顺序：
 *   例如 extcommunity-limit 条目（其 xpath 属于 match-condition）被排在若干
 *   set-action 条目之间；阅读时请以每个元素的 xpath 为准，不要靠相邻位置判断它
 *   属于 match 还是 set。这些 xpath 必须与 YANG 模型严格一致，改名要同步
 *   bgp_routemap_nb.h 和 bgp_routemap_nb_config.c。建议对照 *_config.c 里任一
 *   回调实现来理解每个键何时被调用。
 * ============================================================================
 */

#include <zebra.h>

#include "lib/command.h"
#include "lib/log.h"
#include "lib/northbound.h"
#include "lib/routemap.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_routemap_nb.h"

/* 【总表】frr_bgp_route_map_info —— 本模块对 NB 框架暴露的全部 YANG 节点回调映射。
 * .name 是 YANG 模块名；.nodes 是节点数组，以 { .xpath = NULL } 结尾。
 * 每个节点只声明它关心的回调键（未列出的键表示该节点不需要处理该动作）。
 * 下面的元素按 xpath 分两大块：match-condition 子句、set-action 子句，
 * 但注意个别元素位置与分类不一致（见文件头导读的易错点说明）。 */
/* clang-format off */
const struct frr_yang_module_info frr_bgp_route_map_info = {
	.name = "frr-bgp-route-map",
	.nodes = {
		{
			/* 【分组：match-condition 下 BGP 扩展参数】本块元素的 xpath 均位于
			 * .../entry/match-condition/rmap-match-condition/frr-bgp-route-map:<参数>，
			 * 即 route-map 某 entry 的 match 子句里、BGP 认识的匹配参数。参数名与实现
			 * 一一对应：vpn-dataplane、local-preference、alias、script、origin、rpki、
			 * rpki-extcommunity、probability、source-vrf、source-protocol、peer 系列
			 * 地址/接口、list-name、evpn 系列、route-distinguisher、as-path-count、
			 * community-limit 等。多数参数只需 modify 与 destroy 两个回调。 */
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:vpn-dataplane",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_vpn_dataplane_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:local-preference",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_local_preference_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_local_preference_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:alias",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_alias_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_alias_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:script",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_script_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_script_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:origin",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_origin_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_origin_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:rpki",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_rpki_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_rpki_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:rpki-extcommunity",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_rpki_extcommunity_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_rpki_extcommunity_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:probability",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_probability_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_probability_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:source-vrf",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_source_vrf_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_source_vrf_destroy,
			}
		},
	    {
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:source-protocol",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_source_protocol_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_source_protocol_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-ipv4-address",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv4_address_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv4_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-interface",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_peer_interface_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_peer_interface_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-ipv6-address",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv6_address_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_peer_ipv6_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:peer-local",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_peer_local_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_peer_local_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:src-peer-ipv4-address",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv4_address_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv4_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:src-peer-interface",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_src_peer_interface_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_src_peer_interface_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:src-peer-ipv6-address",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv6_address_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_src_peer_ipv6_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:list-name",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_list_name_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_list_name_destroy,
			}
		},
		{
			/* 【分组：EVPN match】evpn-default-route（无参开关，用 create / destroy
			 * 表示是否匹配 EVPN 默认路由）、evpn-vni、evpn-route-type 等。 */
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:evpn-default-route",
			.cbs = {
				.create = lib_route_map_entry_match_condition_rmap_match_condition_evpn_default_route_create,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_evpn_default_route_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:evpn-vni",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_evpn_vni_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_evpn_vni_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:evpn-route-type",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_evpn_route_type_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_evpn_route_type_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:route-distinguisher",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_route_distinguisher_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_route_distinguisher_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:as-path-count",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_aspath_count_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_aspath_count_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:community-limit",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_community_limit_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_community_limit_destroy,
			}
		},
		{
			/* 【分组：match comm-list（可嵌套）】comm-list 是一个容器，其下可有多个
			 * comm-list-name 以及 comm-list-name-exact-match / comm-list-name-any
			 * 叶子，因此本节点带 create / destroy 且带 apply_finish——由 finish 把
			 * 各元素汇总成内部匹配用的 list。嵌套叶子见紧随其后的几条 xpath。 */
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list",
			.cbs = {
				.create = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_create,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_destroy,
				.apply_finish = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_finish,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_exact_match_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_exact_match_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_any_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_comm_list_comm_list_name_any_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:ipv4-address",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_ipv4_address_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_ipv4_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:ipv6-address",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_ipv6_address_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_ipv6_address_destroy,
			}
		},
		{
			/* 【分组：set-action 下 BGP 扩展参数】本块元素 xpath 位于
			 * .../entry/set-action/rmap-set-action/frr-bgp-route-map:<参数>，
			 * 即 route-map 命中后要执行的 set 动作。多数为 modify + destroy。
			 * 注意：紧随本元素之后的 extcommunity-limit 虽写在此处，其 xpath 却属于
			 * match-condition，请以 xpath 为准（见文件头导读的易错点）。 */
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:distance",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_distance_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_distance_destroy,
			}
		},
		{
			/* 【注意·分类】本元素 xpath 属 match-condition（不是 set-action），
			 * 只是书写位置排在上面的 set 条目之后；它表达 match extcommunity 的
			 * “数量上限限制”。 */
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/rmap-match-condition/frr-bgp-route-map:extcommunity-limit",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_rmap_match_condition_extcommunity_limit_modify,
				.destroy = lib_route_map_entry_match_condition_rmap_match_condition_extcommunity_limit_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-rt",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_rt_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_rt_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-nt",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_nt_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_nt_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-soo",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_soo_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_soo_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:ipv4-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_ipv4_address_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_ipv4_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:ipv4-nexthop",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_ipv4_nexthop_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_ipv4_nexthop_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:ipv6-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_ipv6_address_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_ipv6_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:preference",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_preference_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_preference_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:label-index",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_label_index_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_label_index_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:local-pref",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_local_pref_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_local_pref_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:weight",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_weight_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_weight_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:origin",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_origin_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_origin_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:originator-id",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_originator_id_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_originator_id_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:table",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_table_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_table_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:atomic-aggregate",
			.cbs = {
				.create = lib_route_map_entry_set_action_rmap_set_action_atomic_aggregate_create,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_atomic_aggregate_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aigp-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_aigp_metric_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_aigp_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:prepend-as-path",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_prepend_as_path_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_prepend_as_path_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:last-as",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_last_as_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_last_as_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:exclude-as-path",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_exclude_as_path_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_exclude_as_path_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:replace-as-path",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_replace_as_path_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_replace_as_path_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:community-none",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_community_none_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_community_none_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:community-string",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_community_string_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_community_string_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:large-community-none",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_large_community_none_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_large_community_none_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:large-community-string",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_large_community_string_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_large_community_string_destroy,
			}
		},
		{
			/* 【分组：set aggregator（可嵌套）】含 aggregator-asn 与 aggregator-address
			 * 两个子叶子，故本节点带 create / destroy 与 apply_finish（两叶子齐备后由
			 * finish 统一写回内部 aggregator 属性）。子叶子见紧随其后的两条 xpath。 */
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aggregator",
			.cbs = {
				.create = lib_route_map_entry_set_action_rmap_set_action_aggregator_create,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_aggregator_destroy,
				.apply_finish = lib_route_map_entry_set_action_rmap_set_action_aggregator_finish,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aggregator/aggregator-asn",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_aggregator_aggregator_asn_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:aggregator/aggregator-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_aggregator_aggregator_address_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:comm-list-name",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_comm_list_name_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_comm_list_name_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-none",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_none_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_none_destroy,
			}
		},
		{
			/* 【分组：set extcommunity-lb（链带宽扩展团体，可嵌套）】含 lb-type、bandwidth、
			 * two-octet-as-specific 等子叶子，带 create / destroy 与 apply_finish。
			 * 注意这些子叶子的 xpath 在本表中分散出现：bandwidth 紧邻其后，而
			 * two-octet-as-specific 在更下方，阅读时按 xpath 匹配即可。 */
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb",
			.cbs = {
				.create = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_create,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_destroy,
				.apply_finish = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_finish,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb/lb-type",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_lb_type_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb/bandwidth",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_bandwidth_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_bandwidth_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-color",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_color_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_extcommunity_color_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:extcommunity-lb/two-octet-as-specific",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_extcommunity_lb_two_octet_as_specific_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv4",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv4_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv4_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv6",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv6_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_evpn_gateway_ip_ipv6_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/rmap-set-action/frr-bgp-route-map:l3vpn-nexthop-encapsulation",
			.cbs = {
				.modify = lib_route_map_entry_set_action_rmap_set_action_l3vpn_nexthop_encapsulation_modify,
				.destroy = lib_route_map_entry_set_action_rmap_set_action_l3vpn_nexthop_encapsulation_destroy,
			}
		},
		{
			/* 【数组终止标记】nodes 数组到此结束；NB 框架遍历到 .xpath == NULL 即停止。 */
			.xpath = NULL,
		},
	}
};
