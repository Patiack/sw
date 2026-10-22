// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRv6 definitions
 * Copyright (C) 2025 6WIND
 * Loïc SANG <loic.sang@6wind.com>
 */
/* ============================================================================
 * 【文件导读】bgp_srv6.h —— SRv6 单播(v6 unicast)场景下的 SID 管理接口
 * ============================================================================
 * 【与 bgp_attr_srv6.h 的区别】那份文件管的是“**收到**对端通告的 Prefix-SID
 * 属性怎么存”；本文件管的是“**本端**给自己的 IPv6 单播前缀配 SID”——即把本机
 * 的 SRv6 Locator 切出一段，为需要通告的前缀分配 SID，并通过 Prefix-SID 属性
 * 发出去。两者是 SRv6-BGP 的收与发两面。
 *
 * 【SID 从哪来（两种配置方式，看 is_srv6_unicast_enabled 的三个判据）】
 *   1) 自动：`segment-routing srv6` + `sid auto`（BGP_CONFIG_SRV6_UNICAST_SID_AUTO），
 *      由 BGP 自己从 locator 里切分 SID；
 *   2) 显式 SID：用户直接指定某个 SID（srv6_unicast[afi].sid_explicit）；
 *   3) SID 索引：只给索引、由 locator 推导出完整 SID（sid_index）。
 *   三者任一成立就算“本 AFI 启用了 SRv6 SID 功能”。
 *
 * 【“注册/注销路由”是关键一环】bgp_srv6_unicast_register_route() 是“某条 BGP
 * 前缀需要 SID”的入口：它会触发向 SRv6 管理器申请/绑定 SID；路由撤销时用
 * unregister 归还。因此真正生效的时点是**前缀被选中要通告时**，而不是配置下来时。
 * 排障“配了 SRv6 但邻居没收到 Prefix-SID”时，先看这条路由有没有走到 register。
 *
 * 【阅读建议】is_srv6_unicast_enabled → register/unregister_route（核心）
 *            → announce/withdraw（面向邻居）→ sid_update/withdraw/delete 与
 *            sid_endpoint（面向本机转发表/locator）。
 * ============================================================================
 */

#ifndef _BGP_SRV6_H_
#define _BGP_SRV6_H_

/* 【函数】is_srv6_unicast_enabled —— 本 bgp 实例在该 AFI 下是否启用了 SRv6 SID
 * 功能。三个判据是“或”的关系（自动 SID 标志 / 显式 SID / SID 索引），任一成立
 * 即为 true。调用方用它来短路：整个功能没开就不必去申请 SID、也不该带 SID 通告。 */
static inline bool is_srv6_unicast_enabled(struct bgp *bgp, afi_t afi)
{
	if (CHECK_FLAG(bgp->af_flags[afi][SAFI_UNICAST], BGP_CONFIG_SRV6_UNICAST_SID_AUTO)
	    || bgp->srv6_unicast[afi].sid_explicit || bgp->srv6_unicast[afi].sid_index)
		return true;

	return false;
}

/* 【函数】配置层入口（afi 级别）
 *   ensure_afi_sid  : 确保该 AFI 的 SID 已就绪（配置变更或启用时调用，内部可能
 *                     触发向 SRv6 管理器申请）。
 *   sid_update      : 单个 SID 的内容发生了变化（如 locator 改动导致推导出的
 *                     SID 变了）——需要重新绑定并重发通告。
 *   sid_withdraw    : 撤销该 AFI 下所有 SID 的通告（功能关闭/重置时用）。
 *   delete          : 删除该 AFI 的全部 SRv6 SID 状态（实例清理用）。
 * 注意 update/withdraw/delete 是**面向本机状态与通告**的三个层次：改 → 撤 →
 * 清，别混用。 */
void bgp_srv6_unicast_ensure_afi_sid(struct bgp *bgp, afi_t afi);
void bgp_srv6_unicast_sid_withdraw(struct bgp *bgp, afi_t afi);
void bgp_srv6_unicast_sid_update(struct bgp *bgp, afi_t afi);
void bgp_srv6_unicast_delete(struct bgp *bgp, afi_t afi);
/* 【函数】bgp_srv6_unicast_sid_endpoint —— 在**接口**上安装(install=true)或卸载
 * (false)该 AFI 对应的 SID endpoint 条目。
 * 语义：SID 要能被本机处理，必须在内核/数据面注册为“本机 SID”（local SID
 * endpoint），否则报文命中该 SID 时无人处理。接口参数用于确定在哪个接口上生效
 * （locator 所在接口）。 */
void bgp_srv6_unicast_sid_endpoint(struct bgp *bgp, afi_t afi,
				   struct interface *ifp, bool install);
/* 【函数】路由级入口（前缀级别，最关键）
 *   register_route   : 某条 BGP 前缀 bpi 需要带 SID 通告 → 申请/绑定 SID。
 *                      **这是"配了 SRv6 何时真正生效"的时点**。
 *   unregister_route : 该前缀不再需要 SID（被撤销/替换）→ 归还 SID。
 * 必须成对，否则 SID 泄漏；dest 用来定位前缀，bpi 用来定位是哪条路径需要它
 * （同前缀多条路径时只有被选中的那条要带 SID）。 */
void bgp_srv6_unicast_unregister_route(struct bgp_dest *dest);
void bgp_srv6_unicast_register_route(struct bgp *bgp, afi_t afi, struct bgp_dest *dest,
				     struct bgp_path_info *bpi);
/* 【函数】面向邻居的通告控制
 *   announce : 把该 AFI 下所有已就绪的 SID 通过 Prefix-SID 属性通告出去。
 *   withdraw : 撤销这些通告。
 * 典型场景：SRv6 配置从关闭变开启（或反向）后，需要重新通告全部受影响的路由。 */
void bgp_srv6_unicast_announce(struct bgp *bgp, afi_t afi);
void bgp_srv6_unicast_withdraw(struct bgp *bgp, afi_t afi);

#endif /* _BGP_SRV6_H_ */
