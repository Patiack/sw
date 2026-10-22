// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Multipath
 * Copyright (C) 2010 Google Inc.
 *               2024 Nvidia Corporation
 *
 * This file is part of FRR
 */

#ifndef _FRR_BGP_MPATH_H
#define _FRR_BGP_MPATH_H

/* ============================================================================
 * 【文件导读】bgp_mpath.h —— 多路径(ECMP)与带权多路径(WECMP)
 * ============================================================================
 * 【要解决什么】BGP 本身只会选出**一条**最优路径，但实际组网中常有多条等价的
 * 可用路径，都下发到转发面就能做**负载分担**(ECMP)。本文件管两件事：
 *   ① 在选路阶段把“候选路径里哪些能一起下发”找出来（记录到下面的结构里）；
 *   ② 支持**带权**分担——按链路带宽或下一跳个数决定各路径的权重。
 *
 * 【BGP 与 IGP 在多个“等价”上的差别（理解本模块的前提）】IGP 里“等价”看度量；
 * BGP 里“多路径”还必须满足一串额外条件：AS_PATH 长度相同、MED 相同、
 * 来自同一邻居 AS、同样的 IGP 度量……具体条件由 `bestpath as-path multipath-relax`
 * 等配置放宽。所以“是不是多路径”判断相当繁琐，代码分布在 bgp_route.c 的选路
 * 与这里的 bgp_path_info_mpath_update。
 *
 * 【数据挂在哪】多路径信息用 struct bgp_path_info_mpath 挂在 **best path** 上，
 * 而不是给每条路径都挂——因为只有最优路径才需要回答“我和谁一起分担”。这是
 * 一个重要设计取舍，读 mp_dest / mp_count 等字段时先记住“它们的宿主是最优路径”。
 *
 * 【WECMP 的四种行为】见 enum bgp_wecmp_behavior：不做加权 / 按链路带宽 /
 * 按下一跳个数 / 用递归得到的带宽值。选哪种由 `bgp bestpath bandwidth` 配置决定，
 * 会影响下发到 zebra 的 nexthop 权重。
 *
 * 【阅读建议】enum bgp_wecmp_behavior → struct bgp_path_info_mpath →
 *            bgp_path_info_mpath_update（选路时更新）→ 一组访问器；实现在
 *            bgp_mpath.c。
 * ============================================================================
 */

/* 【枚举】带权 ECMP(WECMP) 的加权依据。注意第一个值 NONE=0，所以“未配置”与
 * “显式关闭”是同一个值——判断是否启用加权时用 != NONE 即可。
 *   LINK_BW            : 按链路的**带宽**加权（带宽大者分到的流量多）。
 *   NNHN_COUNT         : 按**下一跳个数**加权。
 *   USE_RECURSIVE_VALUE: 使用**递归解析**后得到的带宽数值（不是直接邻居链路的
 *                        带宽，而是被递归到的那条路由的带宽），差别在于多跳场景。 */
enum bgp_wecmp_behavior {
	BGP_WECMP_BEHAVIOR_NONE = 0,
	BGP_WECMP_BEHAVIOR_LINK_BW = 1,
	BGP_WECMP_BEHAVIOR_NNHN_COUNT = 2,
	BGP_WECMP_BEHAVIOR_USE_RECURSIVE_VALUE = 3,
};

/* Supplemental information linked to bgp_dest for keeping track of
 * multipath selections, lazily allocated to save memory
 */
/* 【结构】bgp_path_info_mpath —— 多路径选择的**补充信息**，挂在 bgp_dest 上，
 * 并且按原注释所说 **lazily allocated**（惰性分配）：没有多路径的普通前缀根本不
 * 分配它，以省内存。所以代码里到处要判 NULL，不是“忘了初始化”。
 *   mp_dest   : 指向这条多路径信息所属的 bgp_dest（回溯宿主用；因为多路径集合
 *               本身是以“前缀节点”为单位组织的）。
 *   mp_count  : **被选中的多路径条数**。原注释强调 “attached to best path”——
 *               只有挂在最优路径上时这个计数才有意义（见文件头导读）。
 *   mp_flags  : 附加标志，主要与链路带宽(Link-Bandwidth)扩展团体的存在情况有关，
 *               取值见下面两个宏；同样只在挂到最优路径时有意义。
 *   mp_attr   : **聚合后的属性**，用于对外通告多路径路由——多个下一跳要合成一条
 *               NLRI 发出（BGP 不能为同一前缀发多条带不同下一跳的通告，除非用
 *               Add-Path），所以要合成一份属性。**这是本结构最微妙的部分**：
 *               mp_attr 的生命周期与引用计数必须小心处理。
 *   cum_bw    : 所有多路径的**累计带宽**。WECMP 按带宽加权时，每条路径的权重
 *               就是自己带宽 / cum_bw。注意它是 64 位——带宽相加容易溢出 32 位。
 * 字段注释反复出现 “attached to bestpath” 是在提醒：这些值属于**最优路径**这一个
 * 宿主，不要拿某条非最优路径的指针去读它们。 */
struct bgp_path_info_mpath {
	/* Points to bgp_dest associated with this multipath info */
	struct bgp_dest *mp_dest;

	/* When attached to best path, the number of selected multipaths */
	uint16_t mp_count;

	/* Flags - relevant as noted, attached to bestpath. */
	uint16_t mp_flags;
#define BGP_MP_LB_PRESENT 0x1 /* Link-bandwidth present for >= 1 path */
#define BGP_MP_LB_ALL 0x2 /* Link-bandwidth present for all multipaths */

	/*
	 * Aggregated attribute for advertising multipath route,
	 * attached to bestpath
	 */
	struct attr *mp_attr;

	/* Cumulative bandiwdth of all multipaths - attached to bestpath. */
	uint64_t cum_bw;
};

/* Functions to support maximum-paths configuration */
/* 【函数】`maximum-paths` 配置的落地（set / unset 成对）。
 *   peertype   : 表示这条配置作用于哪类邻居（eBGP / iBGP / 全部）——整型编码，
 *                所以 set 与 unset 的参数必须**完全对应**，否则会留下配置残渣。
 *   maxpaths   : 允许并行下发的最大路径条数。
 *   clusterlen : 与**路由反射器**相关——RR 场景下多路径的判定是否把 cluster-list
 *                长度也纳入比较。这是“看起来毫不相关”的参数被放在这里的真实原因：
 *                RR 里从不同 client 学到的等价路径，cluster-list 可能不同。
 * set/unset 返回错误码（0 表示成功），调用方应检查。 */
extern int bgp_maximum_paths_set(struct bgp *bgp, afi_t afi, safi_t safi,
				 int peertype, uint16_t maxpaths,
				 bool clusterlen);
extern int bgp_maximum_paths_unset(struct bgp *bgp, afi_t afi, safi_t safi,
				   int peertype);

/* Functions used by bgp_best_selection to record current
 * multipath selections
 */
/* 【函数】bgp_path_info_nexthop_cmp —— 比较两条路径的下一跳是否“等价”，供多路径
 * 判定用。注意这不是简单的地址相等：要处理递归下一跳、IPv6 链路本地地址等情形，
 * 因此实现比想象中复杂，返回 0 表示等价。
 * 【函数】bgp_path_info_mpath_update —— 选路完成后，**重新计算这个前缀的多路径
 * 集合**。这是本模块的核心：
 *   new_best / old_best : 新旧最优路径。为什么要两个——多路径信息挂在最优路径上，
 *       最优路径变了就要把信息**迁移**过去，不能简单丢弃重建（会丢掉已通告的
 *       mp_attr 引用，导致属性泄漏或通告抖动）。
 *   num_candidates      : 参与比较的候选数（用于快速判断与上限控制）。
 *   mpath_cfg           : maximum-paths 的配置（上限、是否 relax 等）。
 * 它在 bgp_best_selection 里被调用，是“选路结果 → 多路径集合”的唯一入口。 */
extern int bgp_path_info_nexthop_cmp(struct bgp_path_info *bpi1, struct bgp_path_info *bpi2);
extern void bgp_path_info_mpath_update(struct bgp *bgp, struct bgp_dest *dest,
				       struct bgp_path_info *new_best,
				       struct bgp_path_info *old_best, uint32_t num_candidates,
				       struct bgp_maxpaths_cfg *mpath_cfg);
/* 【函数】bgp_path_info_mpath_aggregate_update —— 重新合成**对外通告用的聚合属性**
 * mp_attr（多条路径合成一条通告时要带什么属性）。与上面的 update 分开是因为：
 * 集合变化不一定导致属性变化，反之亦然；也更便于 Add-Path 等场景单独控制。 */
extern void
bgp_path_info_mpath_aggregate_update(struct bgp_path_info *new_best,
				     struct bgp_path_info *old_best);

/* Unlink and free multipath information associated with a bgp_path_info */
/* 【函数】解链并释放多路径信息。入参是**二级指针**：释放后把调用方的指针置空，
 * 防止悬空（与 aspath_unintern 同一套路）。释放时会级联处理 mp_attr 的引用。 */
extern void bgp_path_info_mpath_free(struct bgp_path_info_mpath **mpath);

/* Walk list of multipaths associated with a best path */
/* 【函数】遍历“最优路径 + 它的多路径”这一**概念上的链表**：
 *   bgp_path_info_mpath_first : 返回遍历起点——**通常就是最优路径本身**（因为它
 *       自己是这个集合的一员），读实现确认。
 *   bgp_path_info_mpath_next  : 返回下一个成员，走到末尾返回 NULL。
 * 用法就是标准循环 `for (p = first(best); p; p = next(p))`。
 * 注意这两个函数是**外部唯一应该使用的遍历方式**——多路径集合并不是一条真实的
 * 链表，而是靠在各路径之间跳转（next 可能先回到 dest 的路径链表再找下一个合格者），
 * 所以不要自己拿 bpi->next 去走，那样会混进非多路径的路径。 */
extern struct bgp_path_info *
bgp_path_info_mpath_first(struct bgp_path_info *path);
extern struct bgp_path_info *
bgp_path_info_mpath_next(struct bgp_path_info *path);

/* Accessors for multipath information */
/* 【函数】一组只读访问器——都**入参是 bgp_dest**（因为信息挂在节点上），内部会
 * 自己找到最优路径再取对应字段，因此调用方不必先拿 best path。无多路径时返回
 * 0 / NULL / NONE 之类的“空值”，调用方要能接受这些值。
 *   mpath_count : 多路径条数（注意返回 uint32_t，而结构里字段是 uint16_t，
 *                 看实现是否做了口径扩展）。
 *   mpath_attr  : 聚合属性 mp_attr（**只读用；不要自行释放**，归属在 mpath 上）。
 *   mpath_chkwtd: 返回当前生效的 WECMP 行为，并**结合该前缀的实际情况**判断——
 *                 例如配置了按带宽加权但这条路径没有带宽信息，返回值可能与全局
 *                 配置不同，所以名字里是 chk(检查) 而不是 get(读取)。
 *   mpath_cumbw : 累计带宽，用于计算各路径的分担权重。 */
extern uint32_t bgp_path_info_mpath_count(struct bgp_dest *dest);
extern struct attr *bgp_path_info_mpath_attr(struct bgp_dest *dest);
extern enum bgp_wecmp_behavior bgp_path_info_mpath_chkwtd(struct bgp *bgp, struct bgp_dest *dest);
extern uint64_t bgp_path_info_mpath_cumbw(struct bgp_dest *dest);

#endif /* _FRR_BGP_MPATH_H */
