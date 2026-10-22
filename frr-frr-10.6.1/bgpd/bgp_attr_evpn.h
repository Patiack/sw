// SPDX-License-Identifier: GPL-2.0-or-later
/* E-VPN attribute handling structure file
 * Copyright (C) 2016 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_attr_evpn.h —— EVPN type-5 路由的“overlay index”承载结构
 * ============================================================================
 * 【背景：type-5 的下一跳为什么需要一个 index】EVPN 的 type-5(IP Prefix) 路由
 * 通告的是一个 IP 前缀，但它的下一跳(nexthop)往往**并非一个直连 IP**，而是一个
 * 逻辑实体：可能是某个网关 IP、某个多宿主以太网段(ESI)，或对端的路由器 MAC。
 * 为了把这几种“非 IP 的下一跳”随路由一起带走，路由属性里额外携带了 overlay
 * index 字段；本文件就是 bgpd 收下之后存放它的地方。
 *
 * 【四种 overlay index（本文件核心，务必分清）】枚举有四个值，但结构体只装两种
 * 数据，这是最容易看错的一处：
 *   OVERLAY_INDEX_TYPE_NONE  : 无 index，下一跳就是普通 IP，按常规路径处理。
 *   OVERLAY_INDEX_GATEWAY_IP : 下一跳是网关 IP，数据存在 gw_ip。
 *   OVERLAY_INDEX_ESI        : 下一跳是一个多宿主以太网段，数据存在 eth_s_id。
 *   OVERLAY_INDEX_MAC        : 下一跳是 MAC，但 MAC **不存在本结构里**，而是放在
 *                              RMAC 属性(Router MAC 扩展团体)中，本字段只起
 *                              “告诉你去 RMAC 里找”的指示作用。
 *
 * 【关于 refcnt 与 bgp_route_evpn_same】本结构可被**多方共享**：refcnt 记录引用
 * 份数，bgp_route_evpn_same 判断两条路由的 overlay 信息是否等价（等价即可共用
 * 同一份对象）。改动引用逻辑时要两者一起考虑，否则会出现悬空或重复释放。
 *
 * 【阅读建议】enum overlay_index_type → struct bgp_route_evpn → 文件末尾那批
 *            attr 提取函数(bgp_attr_rmac / bgp_attr_mac_mobility_seqnum 等)；
 *            实现主要落在 bgp_attr.c 与 bgp_attr_evpn.c。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_ATTR_EVPN_H
#define _QUAGGA_BGP_ATTR_EVPN_H

/* 【宏】eth_tag（以太网标签，即 VLAN ID 的承载字段）的**最大值**。
 * 它在线格式里是 4 字节（type-2 路由头部），所以上界是 0xffffffff；虽然实际
 * 配置里 VLAN 只用到 12 位，但协议字段宽度决定了解码校验要按 4 字节来。 */
#define MAX_ET 0xffffffff

struct attr;

/* 【枚举】overlay index 的四态（语义与“只装两种数据”的坑见文件头导读）。
 * 取值为 0~3 且 NONE 排在首位——凡是将结构体清零的路径，默认就是“无 index”，
 * 因此判断“是否有 overlay index”时不要只看 type != 0 之外的东西。 */
enum overlay_index_type {
	OVERLAY_INDEX_TYPE_NONE,
	OVERLAY_INDEX_GATEWAY_IP,
	OVERLAY_INDEX_ESI,
	OVERLAY_INDEX_MAC,
};

/*
 * Structure to store ovrelay index for EVPN type-5 route
 * This structure stores ESI and Gateway IP overlay index.
 * MAC overlay index is stored in the RMAC attribute.
 */
/* 【结构】bgp_route_evpn —— 一条 type-5 路由的 overlay 信息（可共享，见 refcnt）。
 *   refcnt   : 引用计数。为什么需要——同一个 overlay 信息会被多条路由(不同
 *              前缀、不同 VNI)共用，做成共享对象避免重复存储与重复比较。
 *              释放必须走引用计数递减，不能直接 free。
 *   type     : 指明下面哪个字段有效（MAC 除外，见文件头导读）。
 *   eth_s_id : ESI，10 字节的以太网段标识（type 3 路由里多宿主场景的主键）。
 *              不要与 MAC 混淆：ESI 表示“一段/一组链路”，MAC 表示“一个口”。
 *   gw_ip    : 网关 IP。用 struct ipaddr 而非 in_addr，因为**可能是 IPv6**
 *              （EVPN 支持 v6 网关），所以这里必须能装两种地址族。 */
struct bgp_route_evpn {
	unsigned long refcnt;
	enum overlay_index_type type;
	esi_t eth_s_id;
	struct ipaddr gw_ip;
};

/* 【函数】ESI 字符串互换：
 *   str2esi    : 把 "00:11:22:33:44:55:66:77:88:99" 这类配置字符串解析成 10 字节
 *                esi_t。返回 true 表示成功。
 *   ecom_mac2str: 把一个扩展团体里承载的 MAC(6 字节) 打印成可读字符串。返回的是
 *                **静态/复用的字符串**（读实现确认），不要释放也不要跨调用保存。 */
extern bool str2esi(const char *str, esi_t *id);
extern char *ecom_mac2str(char *ecom_mac);

/* 【函数】把 overlay 信息**写入** attr：
 *   bgp_add_routermac_ecom : 给属性追加/更新 RMAC(Router MAC)扩展团体——这是
 *                            OVERLAY_INDEX_MAC 那种下一跳的落点。
 *   bgp_build_evpn_prefix  : 按 type 与 eth_tag 拼出一个 EVPN 前缀（用于构造
 *                            NLRI，type 不同前缀长度与字段不同）。
 *                            eth_tag 用 uint32_t，对应线格式的 4 字节字段。 */
extern void bgp_add_routermac_ecom(struct attr *attr,
				   struct ethaddr *routermac);
extern int bgp_build_evpn_prefix(int type, uint32_t eth_tag,
				 struct prefix *dst);
/* 【函数】从 attr 里**取出**信息（都只读 attr，不修改）：
 *   bgp_attr_rmac                 : 取 Router MAC，存在返回 true 并填 rmac。
 *   bgp_attr_mac_mobility_seqnum  : 取 MAC Mobility 序号——MAC 漂移场景下用序号
 *                                   比较新旧（大者胜），是“MAC 漂移判定”的依据。
 *   bgp_attr_default_gw           : 判断这条路由是否携带“默认网关”语义的扩展团体
 *                                   （用于通告网关能力）。
 * 这三者都按扩展团体类型在属性里查找，找不到时的返回约定各不相同，用前读实现。 */
extern bool bgp_attr_rmac(struct attr *attr, struct ethaddr *rmac);
extern uint32_t bgp_attr_mac_mobility_seqnum(struct attr *attr);
extern void bgp_attr_default_gw(struct attr *attr);

/* 【函数】两个 IPv6 邻接相关：
 *   bgp_attr_evpn_na_flag   : 读 NA(Neighbor Advertisement，RFC 9136 的 IPv6
 *                             邻接通告)扩展团体，把其中的 P(Proxy)标志回填到
 *                             调用方传进来的 *proxy——注意是**出参**，不是返回值。
 *   bgp_attr_df_pref_from_ec: 从扩展团体里取 DF(指定转发者)选举用的**优先级**，
 *                             同时通过 *alg 出参带出选举算法。返回值为优先级，
 *                             类型 uint16_t。                                 */
extern void bgp_attr_evpn_na_flag(struct attr *attr, bool *proxy);
extern uint16_t bgp_attr_df_pref_from_ec(struct attr *attr, uint8_t *alg);


/* 【函数】bgp_route_evpn_same —— 判断两个 overlay 信息是否等价（用于共享/去重，
 * 见文件头导读）。入参是 const 指针，不会修改。比较时要注意 type 已在内部影响
 * “该比哪个字段”，传 NULL 的行为读实现确认。 */
extern bool bgp_route_evpn_same(const struct bgp_route_evpn *e1,
				const struct bgp_route_evpn *e2);
#endif /* _QUAGGA_BGP_ATTR_EVPN_H */
