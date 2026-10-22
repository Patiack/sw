// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * bgp_bfd.h: BGP BFD definitions and structures
 *
 * @copyright Copyright (C) 2015 Cumulus Networks, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_bfd.h —— BGP 与 BFD 联动（把会话/链路故障探测从秒级降到毫秒级）
 * ============================================================================
 * 【为什么要 BFD】BGP 的 Hold Timer 最小只能到几秒（秒级），链路故障时靠它收敛
 * 太慢。BFD(Bidirectional Forwarding Detection, RFC5880) 用高频双向探测在
 * 毫秒级发现故障，再由 BGP 据此立刻拆会话/切路由。BGP 只是 BFD 的**使用者**：
 * 真正的会话建立与探测在 bfdd 守护进程里，本文件负责的是“哪个 neighbor 要开
 * BFD、单跳还是多跳、源地址变了怎么办”。
 *
 * 【单跳 vs 多跳：本文件的核心判据 PEER_IS_MULTIHOP】
 *   BFD 控制报文按 (目的地址, 源地址, 出接口, VRF) 区分会话，单跳与多跳走**不同的
 *   端口与传输方式**（多跳控制报文走 UDP 4784 且 TTL=255），因此必须先判定。
 *   BGP 这里的规则（见下面的宏）：iBGP 且非共享网段，或显式配了 ebgp-multihop，
 *   就算多跳；其余（eBGP 直连单跳）算单跳。
 *   判错会导致 BFD 会话根本建不起来——这是排障时第一个要看的点。
 *
 * 【配置生效的时机（本项目最容易被绕晕的地方）】
 *   BFD 配置可以配在 **neighbor 上**，也可以配在 **peer-group 上**；group 的配置
 *   需要显式“套用”到 peer 才生效，且改完还要触发一次应用。所以本文件里出现了
 *   成对的函数：bgp_peer_configure_bfd()（建会话）+ bgp_peer_config_apply()
 *   （把配置真正应用下去，group 场景要传 group 指针）。改配置后忘了 apply，
 *   是新写 BGP 代码时最常见的“配置看起来生效了其实没生效”。
 *
 * 建议阅读顺序：PEER_IS_MULTIHOP 宏 → bgp_bfd_init → bgp_peer_configure_bfd /
 *               bgp_peer_config_apply → bgp_peer_remove_bfd_config →
 *               bgp_peer_bfd_update_source（update-source 变化）。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_BFD_H
#define _QUAGGA_BGP_BFD_H

/* 【宏】PEER_IS_MULTIHOP —— 判断该 peer 的 BFD 应按“多跳”模式处理。
 * 两个成立条件（或关系）：
 *   1) iBGP 且 !shared_network：iBGP 邻居通常不在同一网段（本端可能是环回口），
 *      无法假定时直连，故按多跳。shared_network 表示它确实在同一共享网段上
 *      （如 iBGP 用接口地址建邻），此时可以算单跳。
 *   2) 配置了 ebgp-multihop：eBGP 显式允许多跳，那就是多跳。
 * 其余情况（eBGP 且直连、未配 multihop）→ 单跳。 */
#define PEER_IS_MULTIHOP(peer)                                                 \
	((((peer)->sort == BGP_PEER_IBGP) && !(peer)->shared_network)          \
	 || is_ebgp_multihop_configured((peer)))

/* 【函数】bgp_bfd_init —— 进程级初始化：注册 BFD 相关的命令(vty)与 BFD 事件钩子。
 * 由 bgp_main.c 在启动时调用一次，tm 为 bgpd 的事件循环（BFD 事件挂在它上面）。 */
extern void bgp_bfd_init(struct event_loop *tm);

/* 【函数】bgp_bfd_peer_config_write —— 把某 peer 的 BFD 配置写成命令形式输出，
 * 用于 `show running-config`。addr 是邻居地址的字符串形式（写配置时直接用，
 * 避免每次重新格式化）。 */
extern void bgp_bfd_peer_config_write(struct vty *vty, struct peer *peer, const char *addr);

/**
 * 【函数】bgp_bfd_show_info —— `show bfd peers` 类命令的实现：输出该邻居的 BFD
 * 会话状态（是否建立、探测间隔、抖动原因等）。json_neigh 非空时输出 JSON 格式。
 * Show BFD information helper.
 *
 * \param vty the VTY pointer.
 * \param peer the BGP configuration pointer.
 * \param use_json unused.
 * \param json_neigh JSON object when called as JSON command.
 */
extern void bgp_bfd_show_info(struct vty *vty, struct peer *peer, json_object *json_neigh);

/**
 * 【函数】bgp_peer_config_apply —— 把 BFD(及其它随 peer 走的)配置**真正应用**到 peer。
 * 两种调用场景（原注释里提到的例外很关键）：
 *   普通场景：p 是要生效的 peer，pg = p->group（从组里取配置）；
 *   特例：在新组配置拷贝给成员的路径上(peer_group2peer_config_copy)，pg 传的是
 *      **新组**而不是 p->group——因为此时要应用的就是新组的配置。
 * 忘调这个函数 = 配置改了但 BFD 会话没跟着变。
 * When called on a group it applies configuration to all peers in that group,
 * otherwise just applies the configuration to a single peer.
 *
 * This function should be called when configuration changes either on group
 * or peer.
 *
 * \param p the BGP peer pointer.
 * \param pg the BGP group to copy configuration from (it is usually
 *           `p->group` exception when copying new group configuration
 *           see `peer_group2peer_config_copy` function case).
 */
extern void bgp_peer_config_apply(struct peer *p, struct peer_group *pg);

/**
 * 【函数】bgp_peer_configure_bfd —— 为 peer 申请并配置 BFD 会话（幂等：已配过就
 * 什么都不做）。manual 区分“用户手工开启”与“从 peer-group 继承到”——两者的差别
 * 在于 peer 自己没显式配过时组配置才生效，去掉组配置时的回退行为也不同。
 * 注意：**只调用它不会让配置立即生效**，需要接着调用
 * bgp_peer_config_apply()（原注释特别强调这点）。
 * Allocates and configure BFD session for peer. If it is already configured,
 * then it does nothing.
 *
 * Always call `bgp_peer_config_apply` afterwards if you need the changes
 * immediately applied.
 */
extern void bgp_peer_configure_bfd(struct peer *p, bool manual);

/**
 * 【函数】bgp_peer_remove_bfd_config —— 摘掉 peer(或其所在组)上的 BFD 配置，
 * 并把已建立的 BFD 会话注销掉。no neighbor X bfd / 换组 / 删邻居时都会走到这里。
 * Removes BFD configuration from either peer or peer group.
 */
extern void bgp_peer_remove_bfd_config(struct peer *p);

/**
 * 【函数】bgp_peer_bfd_update_source —— 处理 `neighbor X update-source Y` 改动。
 * 为什么需要专门处理：BFD 会话是按 (源地址,目的地址,出接口,VRF) 唯一标识的，
 * 源地址一变，旧会话就作废了，必须注销旧会话并按新源地址重建，否则会一直
 * 挂在“会话已存在但探测的是旧源地址”的错误状态。
 * Special function to handle the case of changing source address. This
 * happens when the peer/group is configured with `neighbor X update-source Y`.
 */
extern void bgp_peer_bfd_update_source(struct peer *p);

#endif /* _QUAGGA_BGP_BFD_H */
