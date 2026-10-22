// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP network related header
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_network.h —— BGP 邻居 TCP 连接建立的对外接口
 * ============================================================================
 * 【解决什么问题】BGP 跑在 TCP 179 之上，邻居会话的“建连”这一层（建监听 socket、
 * 发起主动连接、绑定源地址、TCP-MD5 签名、TTL 安全、监听记录的管理）全部落在
 * bgp_network.c；本头文件把这些能力暴露给 bgpd 其它模块（主要是 bgp_fsm.c 的状态机
 * 和 bgpd.c 的初始化路径）调用。
 *
 * 【两种建连方向，一句话区分】
 *   - 主动连接(active / outgoing)：本端向邻居 IP:179 发起 connect() —— 入口是
 *     bgp_connect()，由 FSM 在 Connect 状态下调用。
 *   - 被动监听(passive / listen range)：本端 bind+listen 一个 socket，等邻居连过来
 *     —— 入口是 bgp_socket()，accept 后由 bgp_accept() 回调处理。listen range 还能用
 *     网段一次性覆盖“一段地址”的邻居。
 *   谁先谁后由 FSM 的 Connect / Active 状态与配置（neighbor ... passive、
 *   listen range …）共同决定；两边同时连上就是“连接冲突”，用 connection collision
 *   resolution（doppelganger 机制）择一存活。
 *
 * 【关键数据结构】struct bgp_listener —— 一个“监听记录”，一个监听 socket 对应一条，
 *   它们汇总在全局 bm->listen_sockets 链表里。字段含义见下方逐个注释。注意它同时
 *   承担两个职责：既是“网络监听点”，又用来记录“这个监听属于哪个 BGP 实例 / VRF”
 *   （bgp 字段用于把新 accept 的连接派发到正确的实例）。
 *
 * 【阅读建议】本头文件先看 struct bgp_listener 的字段，再按“会话生命周期”读函数：
 *   启动期建监听(bgp_socket) → 收到连接(bgp_accept，实现在 .c) → 主动建连
 *   (bgp_connect) → 连接建立后取本地地址(bgp_getsockname)；MD5 / TTL / MSS /
 *   update-source 是围绕这三个主流程的“socket 选项设置”辅助函数。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_NETWORK_H
#define _QUAGGA_BGP_NETWORK_H

/* 【即 socket 收发缓冲区目标大小】bgp_socket_set_buffer_size 会把它与当前
 * SO_SNDBUF / SO_RCVBUF 比较，不足则上调到此值，避免 BGP 大表同步时收发缓冲过小。
 * 64 KiB。 */
#define BGP_SOCKET_SNDBUF_SIZE 65536

/* 【一个监听点/一个监听 socket 的记录】
 *   fd    : 处于 listen 状态的套接字描述符（accept 就是从这个 fd 上取新连接）。
 *   su    : 本监听绑定的本地地址（含地址族与端口），accept 到同类连接即归它管。
 *   event : 挂在这个 fd 上的“可读事件”（读就绪即表示有新连接），事件回调是 bgp_accept。
 *   bgp   : 反查指针——若该监听属于某个非默认 VRF 的 BGP 实例则指过去；默认 VRF 下
 *           为 NULL（此时新连接需要靠 SO_BINDTODEVICE 再反查实例）。
 *   name  : 该监听对应的 BGP 实例名（VRF 名）字符串副本，用于日志与匹配。 */
struct bgp_listener {
	int fd;
	union sockunion su;
	struct event *event;
	struct bgp *bgp;
	char *name;
};

/* 【调试/展示】在 vty 上打印所有监听点（Name / fd / 地址）——show 命令用。 */
extern void bgp_dump_listener_info(struct vty *vty);
/* 【建监听】为某 BGP 实例在 port 上、绑定 address（可为 NULL 表示通配）创建监听
 * socket；内部会同时尝试 IPv4 / IPv6，逐个 bgp_listener() 注册。 */
extern int bgp_socket(struct bgp *bgp, unsigned short port,
		      const char *address);
/* 【关闭】只关掉属于指定 VRF 的监听 socket（VRF 删除时调用，配合 netns 后端）。 */
extern void bgp_close_vrf_socket(struct bgp *bgp);
/* 【关闭】关闭默认 VRF 的监听 socket（进程退出清理用）。 */
extern void bgp_close(void);
/* 【主动建连】为某连接发起 TCP connect，返回结果枚举(connect_result)，由 FSM 据此
 * 在 Connect 状态里推进或退避重试。 */
extern enum connect_result bgp_connect(struct peer_connection *connection);
/* 【建连后取地址】TCP 建立后取本地地址/端口并交给 zebra 计算下一跳。 */
extern int bgp_getsockname(struct peer_connection *connection);

/* 【TCP-MD5 签名（RFC 2385）】前缀版：对某个“网段前缀”的邻居在当前监听 socket 上
 * 设置/清除 MD5 口令（listen range 场景，一个 socket 覆盖一段地址）。 */
extern int bgp_md5_set_prefix(struct bgp *bgp, struct prefix *p,
			      const char *password);
extern int bgp_md5_unset_prefix(struct bgp *bgp, struct prefix *p);
/* 【TCP-MD5】连接版：按该连接的地址给监听 socket 设置 / 清除 MD5 口令。 */
extern int bgp_md5_set(struct peer_connection *connection);
extern int bgp_md5_unset(struct peer_connection *connection);
/* 【TTL 安全 / GTSM】给连接的 socket 设置 TTL / MinTTL，实现 RFC 5082 GTSM 校验。 */
extern int bgp_set_socket_ttl(struct peer_connection *connection);
/* 【TCP MSS】按邻居配置给监听 socket 设置 TCP 最大段大小（多邻居取最小值）。 */
extern int bgp_tcp_mss_set(struct peer *peer);
/* 【update-source 求址】给定出口接口与目的地址，挑出最合适的本端源地址。 */
extern int bgp_update_address(struct interface *ifp, const union sockunion *dst,
			      union sockunion *addr);

#endif /* _QUAGGA_BGP_NETWORK_H */
