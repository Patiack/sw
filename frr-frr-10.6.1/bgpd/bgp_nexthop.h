// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP nexthop scan
 * Copyright (C) 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_nexthop.h —— 下一跳缓存(bnc)的数据结构 + 地址/直连/自环判断
 * ============================================================================
 * 【与 bgp_nht.h 的分工】bgp_nht.h/.c 是“跟踪逻辑”（登记、zebra 回调、路径映射、
 * 重算）；本文件是“数据结构与扫描层”：bnc 的定义与增删查、直连路由表、
 * 本机地址表、multiaccess（同一网段多邻居）判断、next-hop-self / 自环判断。
 * 两者合起来才是完整的 NHT。
 *
 * 【最需要理解的结构：struct bgp_nexthop_cache (bnc)】
 *   一个“被跟踪的下一跳”对应一个 bnc，键 = (prefix, srte_color, ifindex)。
 *   - flags（BGP_NEXTHOP_VALID / REGISTERED / CONNECTED / LABELED_VALID ...）：
 *     描述这个下一跳当前“好不好用、有没有登记、是不是直连”。
 *   - change_flags（BGP_NEXTHOP_CHANGED / METRIC_CHANGED / MACIP_CHANGED）：
 *     本次 zebra 更新相对上次“哪里变了”，evaluate_paths 据此决定重算范围。
 *   - paths / path_count：所有使用该下一跳的路径（多对多，见 path_nh_map）。
 *   - nht_info：BGP 里放的是 peer session（直连场景与 peer 绑定）。
 *   - resolved_prefix：下一跳最终解析到的前缀（递归解析结果）。
 *   - last_update：zebra 上次更新时刻，用于排障“多久没更新了”。
 *
 * 【EVPN 网关 IP 下一跳的特殊性】这类下一跳要求 **L3 可达 且 有 MAC/IP 信息**
 *   才算 VALID；只有 L3 可达但 MAC/IP 未解析时置 BGP_NEXTHOP_EVPN_INCOMPLETE。
 *   文件里那张 L3×L2 可达性对照表就是这段语义的说明，值得对照看。
 *
 * 建议阅读顺序：NEXTHOP_FAMILY 宏 → struct bgp_nexthop_cache 及两处 flag 定义
 *               → bnc_new/find/free 声明 → bgp_connected_* / bgp_address_* →
 *               末尾的 self / multiaccess 判断函数。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_NEXTHOP_H
#define _QUAGGA_BGP_NEXTHOP_H

#include "if.h"
#include "queue.h"
#include "prefix.h"
#include "bgp_table.h"

/* 【宏】NEXTHOP_FAMILY —— 由 UPDATE 里下一跳字段的**编码长度**推断地址族。
 * 长度含义（关键，容易只记 IPv4=4/IPv6=16 而漏掉后面那些）：
 *   4  : IPv4 地址
 *   12 : IPv4 地址 + 8 字节 VPN 标签栈（VPNv4）
 *   16 : IPv6 全局地址
 *   24 : IPv6 全局 + 链路本地（16+16=32 里的常见子集？此处为 16+8 VPN）
 *   32 : IPv6 全局 + 链路本地（16+16）
 *   48 : IPv6 全局 + 链路本地 + VPN 标签栈
 * 其它长度 → AF_UNSPEC（非法/不支持）。解析收到意外的 nexthop_len 时，本宏返回
 * AF_UNSPEC 而不是猜，是刻意为之的错误信号。 */
#define NEXTHOP_FAMILY(nexthop_len)                                            \
	(((nexthop_len) == 4 || (nexthop_len) == 12                            \
		  ? AF_INET                                                    \
		  : ((nexthop_len) == 16 || (nexthop_len) == 24                \
				     || (nexthop_len) == 32                    \
				     || (nexthop_len) == 48                    \
			     ? AF_INET6                                        \
			     : AF_UNSPEC)))

#define BGP_MP_NEXTHOP_FAMILY NEXTHOP_FAMILY

PREDECL_RBTREE_UNIQ(bgp_nexthop_cache);

/* BGP nexthop cache value structure. */
/* 【下一跳缓存 bnc】字段说明见文件头导读；这里补充几个容易忽略的点：
 *   - ifindex_ipv6_ll : **仅当**下一跳是 IPv6 链路本地地址时有效，用来在
 *                       多接口场景下区分“同一个 fe80:: 在不同接口上”的歧义。
 *                       这也是为什么 bnc 的键里含 ifindex。
 *   - change_flags    : 与 flags 是两回事——flags 说“现在是什么状态”，
 *                       change_flags 说“这次更新相对上次变了什么”。
 *   - is_evpn_gwip_nexthop : 标记这是 EVPN 网关 IP 下一跳，其有效性判据特殊
 *                       （见下面 BGP_NEXTHOP_EVPN_INCOMPLETE 的对照表）。
 *   - srte_color      : SR-TE 的 color 扩展，参与 bnc 的键（同前缀不同 color
 *                       是不同 bnc）。 */
struct bgp_nexthop_cache {
	afi_t afi;

	/* The ifindex of the outgoing interface *if* it's a v6 LL */
	ifindex_t ifindex_ipv6_ll;

	/* RB-tree entry. */
	struct bgp_nexthop_cache_item entry;

	/* IGP route's metric. */
	uint32_t metric;

	/* Nexthop number and nexthop linked list.*/
	uint16_t nexthop_num;

	/* This flag is set to TRUE for a bnc that is gateway IP overlay index
	 * nexthop.
	 */
	bool is_evpn_gwip_nexthop;

	uint8_t change_flags;
#define BGP_NEXTHOP_CHANGED	      (1 << 0)
#define BGP_NEXTHOP_METRIC_CHANGED    (1 << 1)
#define BGP_NEXTHOP_MACIP_CHANGED     (1 << 2)

	struct nexthop *nexthop;
	time_t last_update;
	uint16_t flags;

/* 【bnc->flags 位定义】当前状态快照：
 *   VALID           : 该下一跳可用（被 IGP 解析到 / 直连 / EVPN 场景下 L3+L2 齐备）。
 *                     **只有 VALID 的路径才可能被选为最佳并下发**，是最常打交道的位。
 *   REGISTERED      : 已向 zebra 登记，正在接收它的变化通知。
 *   CONNECTED       : 该下一跳是直连路由（eBGP 单跳的严格要求）。
 *   PEER_NOTIFIED   : 已把状态变化通知给相关 peer/路径（防重复通知）。
 *   STATIC_ROUTE / STATIC_ROUTE_EXACT_MATCH : 该下一跳来自静态路由配置，
 *                     后者表示要求精确匹配（不按最长前缀放宽）。
 *   LABELED_VALID   : 带标签(MPLS)的下一跳有效——VPN/标签单播单独判据。
 *   ULTIMATE        : 递归解析到了“最终”下一跳（如 SR-TE/递归路由场景），
 *                     区别于中间解析结果。 */
/*
 * If the nexthop is EVPN gateway IP NH, VALID flag is set only if the nexthop
 * is RIB reachable as well as MAC/IP is present
 */
#define BGP_NEXTHOP_VALID             (1 << 0)
#define BGP_NEXTHOP_REGISTERED        (1 << 1)
#define BGP_NEXTHOP_CONNECTED         (1 << 2)
#define BGP_NEXTHOP_PEER_NOTIFIED     (1 << 3)
#define BGP_STATIC_ROUTE              (1 << 4)
#define BGP_STATIC_ROUTE_EXACT_MATCH  (1 << 5)
#define BGP_NEXTHOP_LABELED_VALID     (1 << 6)
#define BGP_NEXTHOP_ULTIMATE	      (1 << 7)

/*
 * This flag is added for EVPN gateway IP nexthops.
 * If the nexthop is RIB reachable, but a MAC/IP is not yet
 * resolved, this flag is set.
 * Following table explains the combination of L3 and L2 reachability w.r.t.
 * VALID and INCOMPLETE flags
 *
 *                | MACIP resolved | MACIP unresolved
 *----------------|----------------|------------------
 * L3 reachable   | VALID      = 1 | VALID      = 0
 *                | INCOMPLETE = 0 | INCOMPLETE = 1
 * ---------------|----------------|--------------------
 * L3 unreachable | VALID      = 0 | VALID      = 0
 *                | INCOMPLETE = 0 | INCOMPLETE = 0
 */
/* 第八位起不是“状态”而是“故障细分”：
 * EVPN_INCOMPLETE = L3 可达但 MAC/IP 尚未解析（即上表第二行）。
 * 之所以不把上表第一行“L3 可达且 MAC/IP 已解析”也单独设一个位，是因为那种情形
 * 已经由 VALID=1 且 INCOMPLETE=0 唯一确定——省一个位，但读代码时要靠两个位组合
 * 才能判读出“为什么这个 EVPN 下一跳不可用”。 */
#define BGP_NEXTHOP_EVPN_INCOMPLETE (1 << 8)

	uint32_t srte_color;

	/* Back pointer to the cache tree this entry belongs to. */
	struct bgp_nexthop_cache_head *tree;

	struct prefix prefix;
	struct prefix resolved_prefix;
	void *nht_info; /* In BGP, peer session */
	LIST_HEAD(path_list, bgp_path_info) paths;
	unsigned int path_count;
	struct bgp *bgp;
};

extern int bgp_nexthop_cache_compare(const struct bgp_nexthop_cache *a,
				     const struct bgp_nexthop_cache *b);
DECLARE_RBTREE_UNIQ(bgp_nexthop_cache, struct bgp_nexthop_cache, entry,
		    bgp_nexthop_cache_compare);

/* Own tunnel-ip address structure */
/* 【本机 tunnel-ip 地址表】用于 PE 侧判断“这个下一跳是不是我自己”（隧道端点），
 * 影响路由是否回注/是否算自环。refcnt：同一地址可能被多处引用，计数归零才移除。 */
struct tip_addr {
	struct ipaddr addr;
	int refcnt;
};

/* Forward declaration(s). */
struct peer;
struct update_subgroup;
struct bgp_dest;
struct attr;

/* 把 bnc 的 flags / change_flags 格式化成可读字符串输出（show 命令与日志用），
 * 缓冲区至少要 BNC_FLAG_DUMP_SIZE 字节。 */
#define BNC_FLAG_DUMP_SIZE 180
extern char *bgp_nexthop_dump_bnc_flags(struct bgp_nexthop_cache *bnc,
					char *buf, size_t len);
extern char *bgp_nexthop_dump_bnc_change_flags(struct bgp_nexthop_cache *bnc,
					       char *buf, size_t len);
/* 【直连路由表】bgp_connected_add/delete —— zebra 报告本机某个接口上新增/删除了
 * 一个直连网段时维护的内部表(bgp->connected_table)。
 * 用途：判断“某下一跳是否直连”是 eBGP 单跳合法性校验、以及决定是否走
 * connected 路径的下一跳解析的关键依据；也服务于下面 multiaccess 的判断。 */
extern void bgp_connected_add(struct bgp *bgp, struct connected *c);
extern void bgp_connected_delete(struct bgp *bgp, struct connected *c);
/* 【multiaccess 判断】核心问题：**该下一跳所连的网段上，除了我以外是否还有别的
 * BGP 邻居？**（即这个网段是“广播/共享”的）
 * 为什么重要：若存在多邻居，则本端不能把该下一跳改成自己（会误导对端），
 * 也会影响 next-hop-self 的决策与路由回注。exclude 参数用来把“发起判断的那个
 * peer”排除掉——否则它自己就会被算成“别的邻居”。
 * subgrp 版本：判定范围限定在一个更新子组内（对应“发给这一组 peer 时”的语义）。 */
extern bool bgp_subgrp_multiaccess_check_v4(struct in_addr nexthop,
					    struct update_subgroup *subgrp,
					    struct peer *exclude);
extern bool bgp_subgrp_multiaccess_check_v6(struct in6_addr nexthop,
					    struct update_subgroup *subgrp,
					    struct peer *exclude);
extern bool bgp_multiaccess_check_v4(struct in_addr nexthop, struct peer *peer);
extern bool bgp_multiaccess_check_v6(struct in6_addr nexthop,
				     struct peer *peer);
extern int bgp_config_write_scan_time(struct vty *vty);
/* 【函数】bgp_nexthop_self —— 判断“这条路由的下一跳指向我本机地址”（next-hop-self
 * 场景）。服务于**路由回注**：本机自己通告的路由又被自己收到时不应再算/再下发，
 * 否则会形成回环或错误选路。
 * 【函数】bgp_hostroute_self —— 判断前缀 p 是否命中本机地址（/32 或 /128 主机路由），
 * 同样是回注/自环防护的一部分。 */
extern bool bgp_nexthop_self(struct bgp *bgp, afi_t afi, uint8_t type,
			     uint8_t sub_type, struct attr *attr,
			     struct bgp_dest *dest);
extern bool bgp_hostroute_self(struct bgp *bgp, const struct prefix *p);
/* 【bnc 的增删查】键 = (prefix, srte_color, ifindex)，比较函数
 * bgp_nexthop_cache_compare 由 DECLARE_RBTREE_UNIQ 装配。
 *   bnc_new   : 新建并入树（不登记 zebra；登记由 NHT 逻辑层负责）。
 *   bnc_find  : 按完整键查找（找不到返回 NULL）。
 *   bnc_existing_for_prefix : 仅按 prefix 看“是否已有该前缀的 bnc”（忽略 color/
 *               ifindex）——用于判断“这个下一跳是否已在被跟踪”。
 *   bnc_free / bnc_nexthop_free : 释放 bnc 本体 / 只释放其 nexthop 链表。
 * 注意 bnc 释放前必须确保 path_count == 0（还有路径引用就不能删），
 * 这是本主线最容易出悬垂指针的地方。 */
extern struct bgp_nexthop_cache *bnc_new(struct bgp_nexthop_cache_head *tree,
					 struct prefix *prefix,
					 uint32_t srte_color,
					 ifindex_t ifindex);
extern bool bnc_existing_for_prefix(struct bgp_nexthop_cache *bnc);
extern void bnc_free(struct bgp_nexthop_cache *bnc);
extern struct bgp_nexthop_cache *bnc_find(struct bgp_nexthop_cache_head *tree,
					  struct prefix *prefix,
					  uint32_t srte_color,
					  ifindex_t ifindex);
extern void bnc_nexthop_free(struct bgp_nexthop_cache *bnc);
/* 【扫描层初始化/销毁】每个 bgp 实例创建/删除时配对调用：
 *   bgp_scan_init/finish : 初始化下一跳缓存树与直连表等；
 *   bgp_scan_vty_init    : 注册本文件相关的 show/debug 命令（进程级，只做一次）；
 *   bgp_address_init/destroy : 本机地址表（判断“下一跳是不是我”用）。
 * 由命名可知本文件历史上叫 “nexthop scan”（定期扫描下一跳）——早期 FRR 靠轮询，
 * 现在是 zebra 事件驱动的 NHT，但函数名沿用了 scan。 */
extern void bgp_scan_init(struct bgp *bgp);
extern void bgp_scan_finish(struct bgp *bgp);
extern void bgp_scan_vty_init(void);
extern void bgp_address_init(struct bgp *bgp);
extern void bgp_address_destroy(struct bgp *bgp);
/* 【tunnel-ip(隧道端点地址)表】bgp_tip_add/del 增删一个本机隧道 IP（refcnt 计数），
 * bgp_tip_hash_init/destroy 初始化销毁该表。用于 PE 场景下识别“下一跳是本机隧道
 * 端点”，参与自环/回注判断。 */
extern bool bgp_tip_add(struct bgp *bgp, struct ipaddr *tip);
extern void bgp_tip_del(struct bgp *bgp, struct ipaddr *tip);
extern void bgp_tip_hash_init(struct bgp *bgp);
extern void bgp_tip_hash_destroy(struct bgp *bgp);

extern void bgp_nexthop_show_address_hash(struct vty *vty, struct bgp *bgp);
#endif /* _QUAGGA_BGP_NEXTHOP_H */
