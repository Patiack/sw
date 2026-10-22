// SPDX-License-Identifier: GPL-2.0-or-later
/* E-VPN header for packet handling
 * Copyright (C) 2016 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_evpn.h —— EVPN 对内核/其它模块暴露的公开接口(含少量 inline 工具)
 * ============================================================================
 * 【职责与定位】
 *   EVPN(Ethernet VPN, RFC7432 及后续 RFC9136/RFC8365/RFC9135)把 MAC/IP 作为
 *   NLRI 在 BGP 中传播，用于数据中心 VXLAN 场景的 MAC 学习与 ARP/ND 抑制。
 *   本头文件是 EVPN 子系统的**对外 API 契约**：只声明“外部(bgp_route.c、
 *   bgp_zebra.c、bgp_packet.c、命令层等)可以调用什么”，具体实现落在
 *   bgp_evpn.c / bgp_evpn_mh.c / bgp_evpn_vty.c / bgp_evpn_private.h。
 *   真正常用的内部数据结构(bgpevpn 等)在 bgp_evpn_private.h 里，本文件不暴露。
 *
 * 【核心概念(阅读实现前先建立)】
 *   - 路由类型：Type-1 EAD(含 A-A 别名)、Type-2 MAC/IP、Type-3 IMET(BUM 泛洪)、
 *     Type-4 Ethernet Segment、Type-5 IP Prefix。本文件大量函数按类型分派。
 *   - L2 VNI / L3 VNI：MAC-VRF(二层) / IP-VRF(三层)，分别对应 MAC 表和 IP 表。
 *   - 标签：EVPN 这里携带的是 VNI(见 bgp_label.h)；Type-2 可能带两个标签
 *     (L2VNI + L3VNI，VNI_FLAG_USE_TWO_LABELS)。
 *   - RD / RT / ESI / Ethernet Tag 是路由键或扩展团体，见 bgp_evpn_private.h。
 *   - 多归属(ESI/DF 选举/别名)的状态机在 bgp_evpn_mh.c，不在本批文件内。
 *
 * 【本文件在主流程中的位置】
 *   上游(bgp_route.c 收到 UPDATE 后)→ bgp_nlri_parse_evpn()(在 bgp_evpn.c)
 *   导入/导出类(bgp_evpn_import_route / export_type5_route)被 bgp_route.c 的
 *   重分布/导入逻辑调用；本地 MAC/IP 学习来自 bgp_zebra.c(zebra 事件)→
 *   bgp_evpn_local_macip_add/del；下发给内核走 evpn_zebra_install/uninstall。
 *
 * 【易错点 / 阅读建议】
 *   1) EVPN_ENABLED()/is_evpn_enabled() 判定的是“默认实例(evpn bgp)是否开启
 *      advertise_all_vni”，不是“本 VRF 是否开了 EVPN”。
 *   2) is_route_parent_evpn() 判断的是“某条 imported 路由的**源头父路由**是否
 *      来自 L2VPN/EVPN 表”——用于决定导出/泄漏时的特殊处理，别与“本路由是否
 *      EVPN 路由”混淆。
 *   3) 建议阅读顺序：宏与 EVPN_ENABLED → is_evpn_enabled →
 *      advertise_type5_routes_bestpath/multipath → is_route_parent_evpn 族 →
 *      末尾一大片 extern 声明(按 导入/导出 / 本地事件 / zebra 下发 / 查询 分组看)。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_EVPN_H
#define _QUAGGA_BGP_EVPN_H

#include "vxlan.h"
#include "bgpd.h"

/* 【常量】EVPN_ROUTE_STRLEN —— 把一条 EVPN 路由格式化成字符串(buf)时预留的
 * 缓冲区大小。EVPN 路由字符串里可能同时含 MAC、IPv6、RD、标签等，故取值 200，
 * 远大于 MAC + IPv6 字符串本身(英文注释已说明)。 */
#define EVPN_ROUTE_STRLEN 200 /* Must be >> MAC + IPv6 strings. */
/* 【常量】EVPN_AUTORT_VXLAN —— 自动派生 Route Target(RT)时使用的“半个 RT”基值。
 * VXLAN 场景的自动 RT 形如 <asn或ip>:<VNI>，这里的 0x10000000(=268435456)
 * 用作 RT 中低位的基数，避免与手工配置冲突；配合 VNI 一起算出唯一 RT。
 * 参见 bgp_evpn.c 的 form_auto_rt：RFC8365 模式下以 SET_FLAG(vni, EVPN_AUTORT_VXLAN)
 * 把它并入 VNI(对 VNI 置位)，再随低 16 位 AS 一起编码成 RT。 */
#define EVPN_AUTORT_VXLAN 0x10000000

/* 【宏】EVPN_ENABLED —— 语义等价于“该 bgp 实例的 advertise_all_vni 是否置位”。
 * 注意这是在**传入的 bgp 实例**上取标志位，配合下面的 is_evpn_enabled() 使用。 */
#define EVPN_ENABLED(bgp)  (bgp)->advertise_all_vni
/* 【函数】is_evpn_enabled —— 取全局 EVPN(默认实例)是否使能。
 * 步骤：bgp_get_evpn() 拿到 EVPN 默认实例，若存在再看 EVPN_ENABLED。
 * 返回：非 0 表示 EVPN 已使能，0 表示未使能(或实例不存在)。
 * 调用时机：任何需要“EVPN 全局开了没”的快速判断处。 */
static inline int is_evpn_enabled(void)
{
	struct bgp *bgp = NULL;

	bgp = bgp_get_evpn();
	return bgp ? EVPN_ENABLED(bgp) : 0;
}

/* 【函数】advertise_type5_routes_bestpath —— 判断“本 VRF 是否应把其最佳路径
 * 以 Type-5(IP Prefix)发布到 EVPN”。用于 IP-VRF 到 EVPN 的单路径公布(bestpath)。
 * 参数：bgp_vrf 该 VRF 的 bgp 实例；afi 地址族(AFI_IP / AFI_IP6)。
 * 返回：1 表示要发布；0 表示不发布。
 * 关键分支：
 *   - 先要求 bgp_vrf->l3vni 非 0(没有三层 VNI 就没有 IP-VRF，直接返回 0)；
 *   - 再按 afi 查 AFI_L2VPN/SAFI_EVPN 下的 af_flags 里是否置了
 *     BGP_L2VPN_EVPN_ADV_IPV4_UNICAST / _IPV6_UNICAST 位。
 * 注意：只影响“用 bestpath”还是“用 multipath”那条路径的选择，见下一个函数。 */
static inline int advertise_type5_routes_bestpath(const struct bgp *bgp_vrf, afi_t afi)
{
	uint16_t flags = bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN];

	if (!bgp_vrf->l3vni)
		return 0;

	if (afi == AFI_IP && CHECK_FLAG(flags, BGP_L2VPN_EVPN_ADV_IPV4_UNICAST))
		return 1;
	if (afi == AFI_IP6 && CHECK_FLAG(flags, BGP_L2VPN_EVPN_ADV_IPV6_UNICAST))
		return 1;

	return 0;
}

/* 【函数】advertise_type5_routes_multipath —— 与 bestpath 版对称：判断“本 VRF
 * 是否应把**等价多路径(ECMP)**以 Type-5 发布到 EVPN”，即带网关 IP(overlay index)
 * 的多路径公布。
 * 返回：1 表示要按 multipath 发布；0 表示不发布。
 * 差异点：查的位是 BGP_L2VPN_EVPN_ADV_IPV4_UNICAST_GW_IP / _IPV6_UNICAST_GW_IP
 *   (带 _GW_IP 后缀)，即“以网关 IP 作为 overlay index 的多路径”。
 * 与上一个函数的关系：两个开关各自独立，可分别控制单路径与多路径的发布方式。 */
static inline int advertise_type5_routes_multipath(const struct bgp *bgp_vrf, afi_t afi)
{
	uint16_t flags = bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN];

	if (!bgp_vrf->l3vni)
		return 0;

	if (afi == AFI_IP && CHECK_FLAG(flags, BGP_L2VPN_EVPN_ADV_IPV4_UNICAST_GW_IP))
		return 1;
	if (afi == AFI_IP6 && CHECK_FLAG(flags, BGP_L2VPN_EVPN_ADV_IPV6_UNICAST_GW_IP))
		return 1;

	return 0;
}

/* 【函数】get_route_parent_evpn —— 沿 vrfleak 链回溯，取到“根父路由”。
 * 背景：一条从 EVPN 导入到 VRF 的路由(sub_type=BGP_ROUTE_IMPORTED)会通过
 *   extra->vrfleak->parent 指向它的来源路由；来源本身可能又是被导入的，于是
 *   形成一条链。本函数一路往上找到最顶端的父 pi。
 * 参数：ri 一条 bgp_path_info。
 * 返回：根父 pi 指针；若 ri 不是 IMPORTED / 没有 extra / 没有 vrfleak / 没有
 *   parent，则返回 NULL。
 * 关键分支：循环条件要求当前 parent 也带 vrfleak->parent 才继续上溯，否则停。
 * 易错点：完全背包式的“有父才有链”，任一环缺失即提前返回 NULL，调用方要判空。 */
/* Flag if the route's parent is a EVPN route. */
static inline struct bgp_path_info *
get_route_parent_evpn(struct bgp_path_info *ri)
{
	struct bgp_path_info *parent_ri;

	/* If not imported (or doesn't have a parent), bail. */
	if (ri->sub_type != BGP_ROUTE_IMPORTED || !ri->extra ||
	    !ri->extra->vrfleak || !ri->extra->vrfleak->parent)
		return NULL;

	/* Determine parent recursively */
	for (parent_ri = ri->extra->vrfleak->parent;
	     parent_ri->extra && parent_ri->extra->vrfleak &&
	     parent_ri->extra->vrfleak->parent;
	     parent_ri = parent_ri->extra->vrfleak->parent)
		;

	return parent_ri;
}

/* 【函数】is_route_parent_evpn —— 判断某条路由的“根父路由”是否位于
 * L2VPN/EVPN 表中，即“它是不是由 EVPN 路由导入而来的”。
 * 步骤：先用 get_route_parent_evpn() 取根父；取其 net(dest) 与所属 table；
 *   若 table 的 afi==AFI_L2VPN 且 safi==SAFI_EVPN 则返回 1。
 * 返回：1 是；0 否(无父/dest/table/或 family 不匹配)。
 * 用途：导出到 VRF、判断是否需要特殊 SoO/martian 处理时的“来源判别”。 */
/* Flag if the route's parent is a EVPN route. */
static inline int is_route_parent_evpn(struct bgp_path_info *ri)
{
	struct bgp_path_info *parent_ri;
	struct bgp_table *table;
	struct bgp_dest *dest;

	parent_ri = get_route_parent_evpn(ri);
	if (!parent_ri)
		return 0;

	/* See if of family L2VPN/EVPN */
	dest = parent_ri->net;
	if (!dest)
		return 0;
	table = bgp_dest_table(dest);
	if (table &&
	    table->afi == AFI_L2VPN &&
	    table->safi == SAFI_EVPN)
		return 1;
	return 0;
}

/* 【宏】IS_PATH_IMPORTED_FROM_EVPN_TABLE —— 该 pi 是否“从 EVPN 表导入而来”。
 * 复合条件：sub_type 为 IMPORTED 且其根父路由确在 EVPN 表中(见上一函数)。 */
#define IS_PATH_IMPORTED_FROM_EVPN_TABLE(pi)                                                      \
	(pi->sub_type == BGP_ROUTE_IMPORTED && is_route_parent_evpn(pi))

/* 【宏】IS_L2VPN_AFI_IN_NON_DEFAULT_VRF —— 判断“当前 bgpd 上下文是不是某个
 * 非默认实例的 L2VPN/EVPN 表”。用于区分“默认 EVPN 实例”(bgp_get_evpn())与
 * “租户 VRF 里的 L2VPN 视图”，二者对同一 afi/safi 语义不同、处理路径也不同。 */
#define IS_L2VPN_AFI_IN_NON_DEFAULT_VRF(bgp, afi, safi)                                           \
	(afi == AFI_L2VPN && safi == SAFI_EVPN && bgp != bgp_get_evpn())

/* 【函数】is_pi_family_evpn —— 该 pi 所属表是否就是 L2VPN/EVPN 族。
 * 只是对通用工具 is_pi_family_matching(pi, AFI_L2VPN, SAFI_EVPN) 的语义包装。 */
/* Flag if the route path's family is EVPN. */
static inline bool is_pi_family_evpn(struct bgp_path_info *pi)
{
	return is_pi_family_matching(pi, AFI_L2VPN, SAFI_EVPN);
}

/* 【函数】evpn_resolve_overlay_index —— 全局开关查询：EVPN 是否启用“overlay index
 * 递归解析”。启用后，以网关 IP(overlay index)作下一跳的 EVPN 路由需要做递归
 * 查表解析(见 remote_ip_hash)；关闭则不做。取默认 EVPN 实例上的 resolve_overlay_index。
 * 返回：true 启用；false 未启用或 EVPN 实例不存在。 */
static inline bool evpn_resolve_overlay_index(void)
{
	struct bgp *bgp = NULL;

	bgp = bgp_get_evpn();
	return bgp ? bgp->resolve_overlay_index : false;
}

/* ===== 对外 API 声明。按用途分组阅读：Type-5 公布 / 编解码 / 导入导出 /
 *       本地事件(zebra) / zebra 下发 / 查询与辅助 ===== */
/* 【组·Type-5(IP Prefix) 的公布与撤销】把 VRF 里的 IP 前缀以 Type-5 形式
 * 发布到 EVPN(或撤销)。originator 是触发发布的原始路径，p 是前缀，src_attr 是
 * 源属性，afi/safi 指示地址族，addpath_id 用于 ADD-PATH 场景区分同一前缀的多条。 */
extern void bgp_evpn_advertise_type5_route(struct bgp *bgp_vrf, struct bgp_path_info *originator,
					   const struct prefix *p, struct attr *src_attr,
					   afi_t afi, safi_t safi, uint32_t addpath_id);
extern void bgp_evpn_withdraw_type5_route(struct bgp *bgp_vrf,
					  const struct bgp_path_info *originator,
					  const struct prefix *p, afi_t afi, safi_t safi,
					  uint32_t addpath_id);
extern void bgp_evpn_withdraw_type5_routes(struct bgp *bgp_vrf, afi_t afi,
					   safi_t safi);
extern void bgp_evpn_advertise_type5_routes(struct bgp *bgp_vrf, afi_t afi,
					    safi_t safi);
/* 【撤销】bgp_evpn_withdraw_type5_route 撤销单条；_routes 撤销该 afi/safi 下
 * 该 VRF 的全部 Type-5(常在某开关关闭/VRF 删除时整批撤销)。 */
/* 【函数】bgp_evpn_vrf_delete —— VRF 被删除时清理该 VRF 相关的 EVPN 状态
 * (撤销 Import/Export RT 映射、清理 IP-VRF 相关表等)。 */
extern void bgp_evpn_vrf_delete(struct bgp *bgp_vrf);
/* 【函数】bgp_evpn_handle_router_id_update —— Router-ID 变更时的联动处理；
 * withdraw 为真表示先撤销旧的再重建(Type-5 的 RD/RT 与 router-id 相关)。 */
extern void bgp_evpn_handle_router_id_update(struct bgp *bgp, int withdraw);
/* 【函数】bgp_evpn_label2str —— 把 EVPN 携带的标签栈(VNI)格式化到 buf。
 * label 是标签数组，num_labels 为个数(通常 1 个 L2VNI 或 2 个含 L3VNI)。 */
extern char *bgp_evpn_label2str(mpls_label_t *label, uint8_t num_labels,
				char *buf, int len);
/* 【函数】bgp_evpn_route2json —— 把一条 EVPN 前缀序列化为 JSON(供 show ... json
 * 命令使用)，按 route_type 决定输出哪些字段。 */
extern void bgp_evpn_route2json(const struct prefix_evpn *p, json_object *json);
/* 【函数】bgp_evpn_encode_prefix —— 把一条 EVPN 前缀编码进 BGP 报文体 stream。
 * p 是要编码的前缀(含 route_type)，prd 是 RD，label 是要随附的 VNI 标签栈，
 * addpath_capable/addpath_tx_id 处理 ADD-PATH 的路径标识。收端对应
 * bgp_nlri_parse_evpn()。 */
extern void bgp_evpn_encode_prefix(struct stream *s, const struct prefix *p,
				   const struct prefix_rd *prd,
				   mpls_label_t *label, uint8_t num_labels,
				   struct attr *attr, bool addpath_capable,
				   uint32_t addpath_tx_id);
/* 【函数】bgp_nlri_parse_evpn —— 收到对端 UPDATE 后解析 EVPN NLRI 的入口。
 * 逐条解出前缀(带 RD/标签等)，非 withdraw 走 bgp_update，withdraw 走 bgp_withdraw。 */
extern int bgp_nlri_parse_evpn(struct peer *peer, struct attr *attr,
			       struct bgp_nlri *packet, bool withdraw);
/* 【函数】bgp_evpn_import_route / _unimport_route —— 把一条 EVPN 路由按匹配的
 * Route Target 导入到本地 VNI/VRF 表(或撤销导入)。返回值为处理结果状态码。
 * 这是“RT 匹配 → 落到 MAC 表/IP 表”的核心入口。 */
extern int bgp_evpn_import_route(struct bgp *bgp, afi_t afi, safi_t safi,
				 const struct prefix *p,
				 struct bgp_path_info *ri);
extern int bgp_evpn_unimport_route(struct bgp *bgp, afi_t afi, safi_t safi,
				   const struct prefix *p,
				   struct bgp_path_info *ri);
/* 【函数】bgp_evpn_export_type5_route / _unexport_type5_route —— 把一条普通
 * IP 路由取出并“导出为 Type-5 的前身”(生成可写入 EVPN 表的 rt/属性)，或反向取消。
 * dest/pi 是源 IP 表里的目的与路径。 */
extern void bgp_evpn_export_type5_route(struct bgp *bgp, struct bgp_dest *dest,
					struct bgp_path_info *pi, afi_t afi, safi_t safi);
extern void bgp_evpn_unexport_type5_route(struct bgp *bgp, const struct bgp_dest *dest,
					  const struct bgp_path_info *pi, afi_t afi, safi_t safi);
/* 【函数】bgp_reimport_evpn_routes_upon_macvrf_soo_change —— MAC-VRF 的
 * Site-of-Origin(SoO)改变后，所有 EVPN 路由需要按新 SoO 重新判定“能否导入
 * 本端 L2VNI/L3VNI”。old_soo/new_soo 用于 diff 与批量修正。 */
extern void
bgp_reimport_evpn_routes_upon_macvrf_soo_change(struct bgp *bgp,
						struct ecommunity *old_soo,
						struct ecommunity *new_soo);
/* 【函数】bgp_reimport_evpn_routes_upon_martian_change —— 当“martian(火星/
 * 不可用)地址过滤器”配置变化时，重新判定已导入 EVPN 路由的合法性。
 * 注：martian(过滤)与 import 的先后顺序会影响最终结果，阅读时留意二者次序。 */
extern void bgp_reimport_evpn_routes_upon_martian_change(
	struct bgp *bgp, enum bgp_martian_type martian_type, void *old_martian,
	void *new_martian);
/* 【函数】bgp_filter_evpn_routes_upon_martian_change —— 仅做过滤(不重导入)，
 * 按 martian 类型遍历并摘除不合规的路由。 */
extern void
bgp_filter_evpn_routes_upon_martian_change(struct bgp *bgp,
					   enum bgp_martian_type martian_type);
/* 【组·本地事件：来自 zebra 的 MAC/IP、VNI 增删通知】
 * 这些函数由 bgp_zebra.c 在收到 zebra 的 MAC 学习/老化和 L2VNI/L3VNI 上下线
 * 事件时调用，是本端(而非对端)产生 Type-2/Type-3 路由的源头。 */
/* 【函数】bgp_evpn_local_macip_del —— 本端某 VNI 下 MAC/IP 老化/删除；
 * state 表示删除原因(如老化、移动等，供 DAD 等逻辑参考)。 */
extern int bgp_evpn_local_macip_del(struct bgp *bgp, vni_t vni,
				    struct ethaddr *mac, struct ipaddr *ip,
					int state);
/* 【函数】bgp_evpn_local_macip_add —— 本端学习到 MAC(可含 IP)，生成/更新
 * Type-2 路由。flags 携带“是否 sticky/代理”等(见 EVPN_MACIP_ADD_FLAG_*)；
 * seq 用于 MAC 移动性(DAD)序列号；esi 表示该 MAC 所在的多归属段(可空)。
 * 内存/引用要点：内部会 bgp_attr_intern 并可能触发向对端通告。 */
extern int bgp_evpn_local_macip_add(struct bgp *bgp, vni_t vni,
				    struct ethaddr *mac, struct ipaddr *ip,
				    uint8_t flags, uint32_t seq, esi_t *esi);
/* 【函数】bgp_evpn_local_l3vni_add —— 本端 L3VNI(IP-VRF 对应的 VNI)上线：
 * 绑定 VRF、记录路由器 MAC(rmac)/虚拟路由器 rmac(vrr_rmac)、originator_ip、
 * filter(是否过滤)、svi_ifindex、is_anycast_mac(是否任播 MAC)。 */
extern int bgp_evpn_local_l3vni_add(vni_t vni, vrf_id_t vrf_id,
				    struct ethaddr *rmac,
				    struct ethaddr *vrr_rmac,
				    struct ipaddr *originator_ip, int filter,
				    ifindex_t svi_ifindex, bool is_anycast_mac);
/* 【函数】bgp_evpn_local_l3vni_del —— 本端 L3VNI 下线，清理对应 IP-VRF 绑定。 */
extern int bgp_evpn_local_l3vni_del(vni_t vni, vrf_id_t vrf_id);
/* 【函数】bgp_evpn_instance_down —— EVPN 实例整体 down 时的处理(通常来自
 * BGP 实例/VRF 状态变化)，做关联清理。 */
extern void bgp_evpn_instance_down(struct bgp *bgp);
/* 【函数】bgp_evpn_local_vni_del —— 本端 L2VNI 下线：撤销本端在该 VNI 的
 * MAC/IP(即撤销 Type-2/3)并清理 VNI 状态。 */
extern int bgp_evpn_local_vni_del(struct bgp *bgp, vni_t vni);
/* 【函数】bgp_evpn_local_vni_add —— 本端 L2VNI 上线：记录 originator_ip
 * (构造 Type-3 IMET 用)、所属租户 VRF(tenant_vrf_id)、BUM 组播地址(mcast_grp)、
 * SVI 接口索引。是 L2VNI 进入“live”并开始学习/发布的前提。 */
extern int bgp_evpn_local_vni_add(struct bgp *bgp, vni_t vni,
				  struct ipaddr *originator_ip,
				  vrf_id_t tenant_vrf_id,
				  struct in_addr mcast_grp,
				  ifindex_t svi_ifindex);
/* 【函数】bgp_evpn_flood_control_change —— 泛洪控制模式(如 PIM-SM MDT、
 * 静态 BUM 等)变化时更新各 VNI 的 BUM 处理。 */
extern void bgp_evpn_flood_control_change(struct bgp *bgp);
/* 【函数】bgp_evpn_cleanup_on_disable —— EVPN 被关闭(no advertise-all-vni)
 * 时的清理：撤销已发布的本地路由、清理 VNI 表等，但保留可复用的基础状态。 */
extern void bgp_evpn_cleanup_on_disable(struct bgp *bgp);
/* 【函数】bgp_evpn_cleanup —— 进程/实例退出时的彻底清理(哈希表、内存池等)。 */
extern void bgp_evpn_cleanup(struct bgp *bgp);
/* 【函数】bgp_evpn_init —— EVPN 子系统初始化：建表(如 vni_hash、VNI->RT
 * 映射)、注册回调，在 bgp 实例启动 EVPN 时调用。 */
extern void bgp_evpn_init(struct bgp *bgp);
/* 【组·查询与辅助工具】 */
/* 【函数】bgp_evpn_get_type5_prefixlen —— 取 Type-5 的有效前缀长度(去掉
 * 非前缀部分，按 IP 地址族返回 IPV4/IPV6 前缀 bit 数)。 */
extern int bgp_evpn_get_type5_prefixlen(const struct prefix *pfx);
/* 【函数】bgp_evpn_is_prefix_nht_supported —— 该前缀是否支持 NHT(下一跳追踪)。
 * 用于决定是否为某 EVPN 下一跳建立/使用 NHT。 */
extern bool bgp_evpn_is_prefix_nht_supported(const struct prefix *pfx);
/* 【函数】update_advertise_vrf_routes —— 触发该 VRF 的路由向 EVPN 的重新发布
 * (如相关配置变动后)。 */
extern void update_advertise_vrf_routes(struct bgp *bgp_vrf);
/* 【函数】bgp_evpn_show_remote_ip_hash —— 遍历 remote_ip_hash 的回调，用于
 * show 命令输出(参数为 hash_bucket + 上下文)。 */
extern void bgp_evpn_show_remote_ip_hash(struct hash_bucket *bucket,
					 void *args);
/* 【函数】bgp_evpn_show_vni_svi_hash —— 同上，遍历 VNI/SVI 哈希用于 show。 */
extern void bgp_evpn_show_vni_svi_hash(struct hash_bucket *bucket, void *args);
/* 【函数】bgp_evpn_is_gateway_ip_resolved —— 判断某下一跳缓存(bnc)对应的
 * 网关 IP 是否已在 remote_ip_hash 中解析到对应 Type-2(供 overlay index 递归)。 */
extern bool bgp_evpn_is_gateway_ip_resolved(struct bgp_nexthop_cache *bnc);
/* 【函数】bgp_evpn_handle_resolve_overlay_index_set/unset —— 当全局
 * resolve_overlay_index 开关被打开/关闭时，遍历哈希对每条相关路由补偿处理。 */
extern void
bgp_evpn_handle_resolve_overlay_index_set(struct hash_bucket *bucket,
					  void *arg);
extern void
bgp_evpn_handle_resolve_overlay_index_unset(struct hash_bucket *bucket,
					    void *arg);
/* 【函数】bgp_evpn_path_info_labels_get_l3vni —— 在标签栈里找出 L3VNI 那个
 * 标签(双标签场景)；num_labels 见 VNI_FLAG_USE_TWO_LABELS。 */
extern mpls_label_t *bgp_evpn_path_info_labels_get_l3vni(mpls_label_t *labels,
							 uint8_t num_labels);
/* 【函数】bgp_evpn_path_info_get_l3vni —— 从一条路径的标签中取 L3VNI；
 * 取不到(如单标签/非三层)返回 0。 */
extern vni_t bgp_evpn_path_info_get_l3vni(const struct bgp_path_info *pi);
/* 【函数】bgp_evpn_mpath_has_dvni —— 多路径集合里是否含(远端)目的 VNI 的路径，
 * 影响 Type-5 多路径发布的判定。 */
extern bool bgp_evpn_mpath_has_dvni(const struct bgp *bgp_vrf,
				    struct bgp_path_info *mpinfo);
/* 【函数】is_route_injectable_into_evpn(_non_supp) —— 判断一条普通路由是否
 * 可注入 EVPN(如是否带有效标签、是否聚合抑制等)。_non_supp 变体对应“非聚合
 * 抑制”场景。 */
extern bool is_route_injectable_into_evpn(struct bgp_path_info *pi);
extern bool is_route_injectable_into_evpn_non_supp(struct bgp_path_info *pi);
/* 【函数】bgp_aggr_supp_withdraw_from_evpn —— 当聚合抑制生效时，把原先已注入
 * EVPN 的成员路由撤销(from EVPN)。 */
extern void bgp_aggr_supp_withdraw_from_evpn(struct bgp *bgp, afi_t afi,
					     safi_t safi);

/* 【组·向 zebra 下发/撤销 EVPN 转发表项】 */
/* 【函数】evpn_zebra_install/uninstall —— 把某条 EVPN 路由(以 vpn 标识所属
 * VNI、p 前缀、pi 路径)安装/卸载到 zebra(进而编程内核 FDB/邻接)。
 * 返回 zclient_send_status 表示发送结果。 */
extern enum zclient_send_status evpn_zebra_install(struct bgp *bgp,
						   struct bgpevpn *vpn,
						   const struct prefix_evpn *p,
						   struct bgp_path_info *pi);
extern enum zclient_send_status
evpn_zebra_uninstall(struct bgp *bgp, struct bgpevpn *vpn,
		     const struct prefix_evpn *p, struct bgp_path_info *pi,
		     bool is_sync);
/* 【函数】bgp_evpn_skip_vrf_import_of_local_es —— 判断是否应“跳过”把某条
 * EVPN 路由导入 VRF：典型场景是本端自身 Ethernet Segment 上的路由环路规避。 */
bool bgp_evpn_skip_vrf_import_of_local_es(struct bgp *bgp_vrf, const struct prefix_evpn *evp,
					  struct bgp_path_info *pi, int install);
/* 【函数】uninstall_evpn_route_entry_in_vrf —— 在某个 VRF 里卸载一条由 EVPN
 * 导入的路由项(parent_pi 是来源父路径)。 */
int uninstall_evpn_route_entry_in_vrf(struct bgp *bgp_vrf, const struct prefix_evpn *evp,
				      struct bgp_path_info *parent_pi);
/* 【函数】bgp_zebra_evpn_pop_items_from_announce_fifo —— 从 VNI 的待通告 FIFO
 * 中批量取出待通告项(发给 zebra 的异步队列)。 */
extern void bgp_zebra_evpn_pop_items_from_announce_fifo(struct bgpevpn *vpn);
/* 【函数】install_uninstall_routes_for_vni —— 对某 VNI 下所有 EVPN 路由批量
 * 安装(install=true)或卸载(false)到内核。 */
extern int install_uninstall_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn, bool install);
/* 【函数】bgp_evpn_fill_rmac_nh_to_attr —— 为 Type-5 属性填充“路由器 MAC(rmac)
 * 作为下一跳”的相关字段，结合 evp 与 vtep_ip。 */
extern void bgp_evpn_fill_rmac_nh_to_attr(struct bgp *bgp_vrf, struct attr *attr,
					  struct prefix_evpn *evp, struct ipaddr *vtep_ip);
#endif /* _QUAGGA_BGP_EVPN_H */
