// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP network related functions
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_network.c —— BGP 邻居 TCP 建连(主动连接 / 被动监听)与 socket 选项
 * ============================================================================
 * 【解决什么问题】BGP 跑在 TCP 179 之上，会话能不能起来，先看 TCP 这一层。本文件把
 * “邻居建连”这件事全部包办，分成两条路径：
 *   1) 主动连接(active / outgoing)：本端 connect() 到邻居 —— 入口 bgp_connect()。
 *   2) 被动监听(passive / listen range)：本端 bind+listen 一个 socket，等邻居连进来，
 *      读就绪回调 bgp_accept()；建监听 socket 的入口是 bgp_socket() → bgp_listener()；
 *      listen range 让一个监听 socket 覆盖某网段的多个邻居。
 *   除建连本身外，本文件还负责围绕 socket 的一票选项：源地址绑定(update-source /
 *   出口接口)、TCP-MD5 签名、TTL / GTSM(TTL 安全)、TCP MSS、收发缓冲、TCP keepalive、
 *   以及“同一邻居两条连接同时存在时谁活”的连接冲突处理。
 *
 * 【两条路径怎么配合(阅读主线)】FSM 状态机(bgp_fsm.c)驱动会话：Connect 状态下调
 * bgp_connect() 发起连接；连接建立后(Established 前)取本地地址交给 zebra 算下一跳。
 * 被动侧由 bgp_socket() 在启动 / 配置时建好监听，之后每次有新连接就回调 bgp_accept()，
 * 在里面完成“实例判定 → 各种准入检查 → 连接冲突消解 → 进入 FSM”。两条路径最终都汇到
 * FSM 的事件队列，本文件只负责“把 TCP 连接摆平”，不负责会话状态迁移。
 *
 * 【关键数据结构】
 *   struct bgp_listener(定义在 bgp_network.h)：一个监听 socket 一条记录，汇总在全局
 *        bm->listen_sockets 链表。本文件负责创建(bgp_listener)、销毁(bgp_close /
 *        bgp_close_vrf_socket)与遍历(改 MD5 / MSS 时按地址族找对应监听)。
 *   struct peer_connection：一条(每方向)连接实例，本文件操作的 fd / su(对端地址) /
 *        su_local / su_remote / status 都挂在它上面。
 *   doppelganger(分身)：同一邻居同时有 outgoing 与 incoming 两条连接时，新建一条作为
 *        “分身”，最终按连接冲突规则择优存活 —— 见 bgp_accept 里对 peer->doppelganger
 *        的处理。
 *
 * 【为什么这么绕】TCP 建连是异步的(非阻塞 socket + 事件循环)，且要兼容 IPv4 / IPv6、
 * 多 VRF、netns、md5、gtsm 等诸多组合，所以每个 socket 选项都要“按地址族 / 实例”分别
 * 设置；accept 侧还要做一堆准入判定(动态邻居、RPKI、shutdown、状态、AF 激活等)，这些
 * 是本文件读起来最长的部分。
 *
 * 【推荐阅读顺序】bgp_socket(建监听) → bgp_listener(注册一条监听) → bgp_accept(收连接
 * 回调，本文件最长) → bgp_connect(主动建连) → bgp_update_source / bgp_get_bound_name
 * (源地址与绑定) → 其余 MD5 / TTL / MSS 小函数。
 * ============================================================================
 */

#include <zebra.h>

#include "frrevent.h"
#include "sockunion.h"
#include "sockopt.h"
#include "memory.h"
#include "log.h"
#include "if.h"
#include "prefix.h"
#include "command.h"
#include "privs.h"
#include "linklist.h"
#include "network.h"
#include "queue.h"
#include "hash.h"
#include "filter.h"
#include "ns.h"
#include "lib_errors.h"
#include "nexthop.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_open.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_network.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_nht.h"
#include "bgpd/bgp_trace.h"
#include "bgpd/bgp_vty.h"

extern struct zebra_privs_t bgpd_privs;

/* 【前置声明】bgp_get_bound_name 实现在文件后半段；accept / connect 路径上都要用
 * 它来算出“这个连接该 bind 到哪个设备名(VRF / 接口)”，故先在此声明。 */
static char *bgp_get_bound_name(struct peer_connection *connection);

/* 【函数】bgp_dump_listener_info —— 打印全局所有监听 socket(show 命令用)。
 * 遍历 bm->listen_sockets，逐个列出名字(默认 VRF 显示 VRF_DEFAULT_NAME)、fd、地址。
 * 纯展示，不改任何状态。 */
void bgp_dump_listener_info(struct vty *vty)
{
	struct listnode *node;
	struct bgp_listener *listener;

	vty_out(vty, "Name             fd Address\n");
	vty_out(vty, "---------------------------\n");
	for (ALL_LIST_ELEMENTS_RO(bm->listen_sockets, node, listener))
		vty_out(vty, "%-16s %d %pSU\n",
			listener->name ? listener->name : VRF_DEFAULT_NAME,
			listener->fd, &listener->su);
}

/* 【函数】bgp_md5_set_socket —— 在指定 socket 上设置 / 清除 TCP-MD5 签名(RFC 2385)。
 * 参数 su 是对端地址(会先清掉端口号，因为签名只按地址匹配)，prefixlen 是网段长度：
 *   - 完整地址(前缀长度 = 32 / 128)：走非扩展签名 sockopt_tcp_signature；
 *   - 网段(前缀长度较小，listen range 场景)：走扩展签名 sockopt_tcp_signature_ext。
 * password 为空即“清除签名”。平台不支持 MD5(HAVE_DECL_TCP_MD5SIG 未定义)时本函数不做
 * 任何事，ret 保持 -1，由调用方按需告警。 */
/*
 * Set MD5 key for the socket, for the given IPv4 peer address.
 * If the password is NULL or zero-length, the option will be disabled.
 */
static int bgp_md5_set_socket(int socket, union sockunion *su,
			      uint16_t prefixlen, const char *password)
{
	int ret = -1;
	int en = ENOSYS;
#if HAVE_DECL_TCP_MD5SIG
	union sockunion su2;
#endif /* HAVE_TCP_MD5SIG */

	assert(socket >= 0);

#if HAVE_DECL_TCP_MD5SIG
	/* Ensure there is no extraneous port information. */
	/* 【清端口】复制一份再清零端口，避免把带端口的地址交给内核去做签名匹配。 */
	memcpy(&su2, su, sizeof(union sockunion));
	if (su2.sa.sa_family == AF_INET)
		su2.sin.sin_port = 0;
	else
		su2.sin6.sin6_port = 0;

	/* For addresses, use the non-extended signature functionality */
	/* 【签名分岔】整地址用普通签名；带前缀长度的网段用扩展签名。 */
	if ((su2.sa.sa_family == AF_INET && prefixlen == IPV4_MAX_BITLEN)
	    || (su2.sa.sa_family == AF_INET6 && prefixlen == IPV6_MAX_BITLEN))
		ret = sockopt_tcp_signature(socket, &su2, password);
	else
		ret = sockopt_tcp_signature_ext(socket, &su2, prefixlen,
						password);
	en = errno;
#endif /* HAVE_TCP_MD5SIG */

	if (ret < 0) {
		switch (ret) {
		case -2:
			flog_warn(
				EC_BGP_NO_TCP_MD5,
				"Unable to set TCP MD5 option on socket for peer %pSU (sock=%d): This platform does not support MD5 auth for prefixes",
				su, socket);
			break;
		default:
			flog_warn(
				EC_BGP_NO_TCP_MD5,
				"Unable to set TCP MD5 option on socket for peer %pSU (sock=%d): %s",
				su, socket, safe_strerror(en));
		}
	}

	return ret;
}

/* 【函数】bgp_md5_set_connect —— bgp_connect 的辅助：给“主动连接”用的 socket 设 MD5。
 * 因为 setsockopt(TCP_MD5SIG) 需要特权，这里用 frr_with_privs 临时提权后再调
 * bgp_md5_set_socket。仅当平台支持 MD5 时才做事。 */
/* Helper for bgp_connect */
static int bgp_md5_set_connect(int socket, union sockunion *su,
			       uint16_t prefixlen, const char *password)
{
	int ret = -1;

#if HAVE_DECL_TCP_MD5SIG
	frr_with_privs(&bgpd_privs) {
		ret = bgp_md5_set_socket(socket, su, prefixlen, password);
	}
#endif /* HAVE_TCP_MD5SIG */

	return ret;
}

/* 【函数】bgp_md5_set_password —— 按连接的对端地址，在“匹配的监听 socket”上设置 /
 * 清除 MD5 口令。注意：**只处理被动监听用的 socket**；主动连接的 socket 由 bgp_connect
 * 自己设(见上面的 bgp_md5_set_connect)。遍历 bm->listen_sockets，找出与该连接同地址族、
 * 且属于同一 BGP 实例(默认 VRF 或同实例)的那条监听，设好就 break。 */
static int bgp_md5_set_password(struct peer_connection *connection,
				const char *password)
{
	struct listnode *node;
	int ret = 0;
	struct bgp_listener *listener;
	struct peer *peer = connection->peer;

	/*
	 * Set or unset the password on the listen socket(s). Outbound
	 * connections are taken care of in bgp_connect() below.
	 */
	frr_with_privs(&bgpd_privs) {
		for (ALL_LIST_ELEMENTS_RO(bm->listen_sockets, node, listener))
			if (listener->su.sa.sa_family ==
			    connection->su.sa.sa_family) {
				uint16_t prefixlen =
					connection->su.sa.sa_family == AF_INET
						? IPV4_MAX_BITLEN
						: IPV6_MAX_BITLEN;

				/* 【实例匹配】监听 socket 已记录非默认 VRF 的 BGP 实例时，
				 * 必须与 peer 所属实例一致；默认 VRF 的监听(bgp 为 NULL)
				 * 只服务默认 VRF 的 peer。不匹配就跳过这条监听。 */
				/*
				 * if we have stored a BGP vrf instance in the
				 * listener it must match the bgp instance in
				 * the peer otherwise the peer bgp instance
				 * must be the default vrf or a view instance
				 */
				if (!listener->bgp) {
					if (peer->bgp->vrf_id != VRF_DEFAULT)
						continue;
				} else if (listener->bgp != peer->bgp)
					continue;

				ret = bgp_md5_set_socket(listener->fd,
							 &connection->su,
							 prefixlen, password);
				break;
			}
	}
	return ret;
}

/* 【函数】bgp_md5_set_prefix —— 前缀版 MD5 设置。给“listen range”用：某个监听 socket
 * 覆盖一段地址，为其中一个网络前缀 p 设置 / 清除 MD5。只改监听 socket(被动侧)。
 * 匹配规则见下方英文注释：同地址族，且属于默认 VRF 或同一 BGP 实例。 */
int bgp_md5_set_prefix(struct bgp *bgp, struct prefix *p, const char *password)
{
	int ret = 0;
	union sockunion su;
	struct listnode *node;
	struct bgp_listener *listener;

	/* Set or unset the password on the listen socket(s). */
	frr_with_privs(&bgpd_privs) {
		for (ALL_LIST_ELEMENTS_RO(bm->listen_sockets, node, listener))
			/* 【监听匹配】family 一致，且(默认 VRF 且监听无实例)或(同实例)。 */
			/* Match listener socket for the incoming BGP instance if:
			 * 1. Address family matches (IPv4/IPv6)
			 * 2. AND either:
			 *    - incoming BGP instance is in default VRF
			 *      and listener has no BGP instance(default vrf)
			 *    - OR listener's BGP instance matches the
			 *      incoming BGP instance(non-default vrf)
			 * [Note: listener->bgp is always NULL for default VRF.
			 *        listener socket could be shared among multiple
			 *        BGP instances within the same VRF.]
			 */
			if (listener->su.sa.sa_family == p->family &&
			    ((bgp->vrf_id == VRF_DEFAULT && !listener->bgp) ||
			     (listener->bgp == bgp))) {
				prefix2sockunion(p, &su);
				ret = bgp_md5_set_socket(listener->fd, &su,
							 p->prefixlen,
							 password);
				break;
			}
	}

	return ret;
}

/* 【函数】bgp_md5_unset_prefix —— 前缀版清除 MD5：等价于密码传 NULL 的 set。 */
int bgp_md5_unset_prefix(struct bgp *bgp, struct prefix *p)
{
	return bgp_md5_set_prefix(bgp, p, NULL);
}

/* 【函数】bgp_md5_set —— 连接版设置 MD5：取该 peer 配置的口令，交给
 * bgp_md5_set_password 去匹配监听 socket。 */
int bgp_md5_set(struct peer_connection *connection)
{
	/* Set the password from listen socket. */
	return bgp_md5_set_password(connection, connection->peer->password);
}

/* 【函数】bgp_update_setsockopt_tcp_keepalive —— 若 BGP 实例配置了 TCP keepalive
 * (idle 非 0)，就按 idle / intvl / probes 三参数给 socket 打开 keepalive。bgp 为空
 * 表示未启用，直接返回；设置失败只记日志，不影响建连。 */
static void bgp_update_setsockopt_tcp_keepalive(struct bgp *bgp, int fd)
{
	if (!bgp)
		return;
	if (bgp->tcp_keepalive_idle != 0) {
		int ret;

		ret = setsockopt_tcp_keepalive(fd, bgp->tcp_keepalive_idle,
					       bgp->tcp_keepalive_intvl,
					       bgp->tcp_keepalive_probes);
		if (ret < 0)
			flog_err(EC_LIB_SOCKET,
				 "Can't set TCP keepalive on socket %d, idle %u intvl %u probes %u",
				 fd, bgp->tcp_keepalive_idle, bgp->tcp_keepalive_intvl,
				 bgp->tcp_keepalive_probes);
	}
}

/* 【函数】bgp_md5_unset —— 连接版清除 MD5：密码传 NULL，仍走监听 socket 路径。 */
int bgp_md5_unset(struct peer_connection *connection)
{
	/* Unset the password from listen socket. */
	return bgp_md5_set_password(connection, NULL);
}

/* 【函数】bgp_set_socket_ttl —— 给连接的 socket 设置出向 TTL 与 GTSM 的 MinTTL。
 * 两种模式：
 *   - 普通 TTL：只设出向 TTL = peer->ttl。
 *   - GTSM(TTL 安全, RFC 5082)：gtsm_hops 非 0 时，出向 TTL 固定为 MAXTTL(255)，
 *     并设 MinTTL = MAXTTL + 1 - gtsm_hops，即“只接受跳数不超过 gtsm_hops 的邻居”。
 * 任意一次 setsockopt 失败都返回非 0(调用方据此判定并把连接标记为 socket 错误)。 */
int bgp_set_socket_ttl(struct peer_connection *connection)
{
	int ret = 0;
	struct peer *peer = connection->peer;

	if (!peer->gtsm_hops) {
		ret = sockopt_ttl(connection->su.sa.sa_family, connection->fd,
				  peer->ttl);
		if (ret) {
			flog_err(
				EC_LIB_SOCKET,
				"%s: Can't set TxTTL on peer (rtrid %pI4) socket, err = %d",
				__func__, &peer->remote_id, errno);
			return ret;
		}
	} else {
		/* 【GTSM 分支】Linux 上只设 minttl 不设 ttl 会干扰出向 TTL，故两者都设。 */
		/* On Linux, setting minttl without setting ttl seems to mess
		   with the
		   outgoing ttl. Therefore setting both.
		*/
		ret = sockopt_ttl(connection->su.sa.sa_family, connection->fd,
				  MAXTTL);
		if (ret) {
			flog_err(
				EC_LIB_SOCKET,
				"%s: Can't set TxTTL on peer (rtrid %pI4) socket, err = %d",
				__func__, &peer->remote_id, errno);
			return ret;
		}
		/* 【MinTTL】由跳数上限反推“最小可接受 TTL”，内核据此丢弃 TTL 过小的包。 */
		ret = sockopt_minttl(connection->su.sa.sa_family, connection->fd,
				     MAXTTL + 1 - peer->gtsm_hops);
		if (ret) {
			flog_err(
				EC_LIB_SOCKET,
				"%s: Can't set MinTTL on peer (rtrid %pI4) socket, err = %d",
				__func__, &peer->remote_id, errno);
			return ret;
		}
	}

	return ret;
}

/* 【函数】bgp_get_instance_for_inc_conn —— 判定“这条到来的连接属于哪个 BGP 实例”。
 * 为什么要判：多个 VRF 可能复用同一套 IP 地址空间，光看对端 IP 不足以确定实例。
 * 办法：读连接 socket 上由 SO_BINDTODEVICE 记录的“对端绑定的设备名”，它可能是
 *   一个 VRF 设备(直接匹配实例名)，也可能是一个接口(再按 VRF 反查所属实例)。
 * 成功时通过 bgp_inst 出参加回实例；失败返回非 0(调用方会关掉该连接)。 */
/*
 * Obtain the BGP instance that the incoming connection should be processed
 * against. This is important because more than one VRF could be using the
 * same IP address space. The instance is got by obtaining the device to
 * which the incoming connection is bound to. This could either be a VRF
 * or it could be an interface, which in turn determines the VRF.
 */
static int bgp_get_instance_for_inc_conn(int sock, struct bgp **bgp_inst)
{
#ifndef SO_BINDTODEVICE
	/* only Linux has SO_BINDTODEVICE, but we're in Linux-specific code here
	 * anyway since the assumption is that the interface name returned by
	 * getsockopt() is useful in identifying the VRF, particularly with
	 * Linux's
	 * VRF l3master device.  The whole mechanism is specific to Linux, so...
	 * when other platforms add VRF support, this will need handling here as
	 * well.  (or, some restructuring) */
	*bgp_inst = bgp_get_default();
	return !*bgp_inst;

#else
	char name[VRF_NAMSIZ + 1];
	socklen_t name_len = VRF_NAMSIZ;
	struct bgp *bgp;
	int rc;
	struct listnode *node, *nnode;

	*bgp_inst = NULL;
	name[0] = '\0';
	/* 【读绑定设备名】取内核记录的 SO_BINDTODEVICE 值，即对端被绑定到的设备名。 */
	rc = getsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, name, &name_len);
	if (rc != 0) {
#if defined(HAVE_CUMULUS)
		flog_err(EC_LIB_SOCKET,
			 "[Error] BGP SO_BINDTODEVICE get failed (%s), sock %d",
			 safe_strerror(errno), sock);
		return -1;
#endif
	}

	/* 【无设备名 ⇒ 默认实例】空字符串代表连接没有绑定到具体 VRF 设备。 */
	if (!strlen(name)) {
		*bgp_inst = bgp_get_default();
		return 0; /* default instance. */
	}

	/* 【按实例名匹配】设备名可能就是某个 BGP 实例(VRF)名。 */
	/* First try match to instance; if that fails, check for interfaces. */
	bgp = bgp_lookup_by_name(name);
	if (bgp) {
		if (!bgp->vrf_id) // unexpected
			return -1;
		*bgp_inst = bgp;
		return 0;
	}

	/* 【退回接口匹配】设备名不是实例名时，按接口名在各实例的 VRF 里查其归属。 */
	/* TODO - This will be optimized once interfaces move into the NS */
	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		struct interface *ifp;

		if (bgp->inst_type == BGP_INSTANCE_TYPE_VIEW)
			continue;

		ifp = if_lookup_by_name(name, bgp->vrf_id);
		if (ifp) {
			*bgp_inst = bgp;
			return 0;
		}
	}

	/* We didn't match to either an instance or an interface. */
	return -1;
#endif
}

/* 【函数】bgp_tcp_mss_set —— 给“监听 socket”设置 TCP MSS。
 * 因为一个监听 socket(尤其是 listen range)可能服务多个邻居，而每个邻居的 MSS 可以
 * 不同，取所有配了 MSS 的邻居里的**最小值**作为该监听 socket 的 MSS。只有当本邻居
 * 处于被动模式时才在监听 socket 上设置(主动连接的 MSS 由 bgp_connect 按各自 socket
 * 设置)。 */
int bgp_tcp_mss_set(struct peer *peer)
{
	struct listnode *node;
	int ret = 0;
	struct bgp_listener *listener;
	uint32_t min_mss = 0;
	struct peer *p;

	/* 【求最小值】遍历本实例所有邻居，取配置了 MSS 的邻居中的最小 tcp_mss。 */
	for (ALL_LIST_ELEMENTS_RO(peer->bgp->peer, node, p)) {
		if (!CHECK_FLAG(p->flags, PEER_FLAG_TCP_MSS))
			continue;

		if (!p->tcp_mss)
			continue;

		if (!min_mss)
			min_mss = p->tcp_mss;

		min_mss = MIN(min_mss, p->tcp_mss);
	}

	frr_with_privs(&bgpd_privs) {
		for (ALL_LIST_ELEMENTS_RO(bm->listen_sockets, node, listener)) {
			if (listener->su.sa.sa_family !=
			    peer->connection->su.sa.sa_family)
				continue;

			if (!listener->bgp) {
				if (peer->bgp->vrf_id != VRF_DEFAULT)
					continue;
			} else if (listener->bgp != peer->bgp)
				continue;

			/* Set TCP MSS per listener only if there is at least
			 * one peer that is in passive mode. Otherwise, TCP MSS
			 * is set per socket via bgp_connect().
			 */
			if (CHECK_FLAG(peer->flags, PEER_FLAG_PASSIVE))
				sockopt_tcp_mss_set(listener->fd, min_mss);

			break;
		}
	}

	return ret;
}

/* 【函数】bgp_socket_set_buffer_size —— 把 socket 的收发缓冲区调到至少
 * bm->socket_buffer(即 BGP_SOCKET_SNDBUF_SIZE)。只往上调、不往下压：当前值已够大就
 * 不动，避免把系统放大过的缓冲区改小。大表同步时缓冲太小会导致抖动。 */
static void bgp_socket_set_buffer_size(const int fd)
{
	if (getsockopt_so_sendbuf(fd) < (int)bm->socket_buffer)
		setsockopt_so_sendbuf(fd, bm->socket_buffer);
	if (getsockopt_so_recvbuf(fd) < (int)bm->socket_buffer)
		setsockopt_so_recvbuf(fd, bm->socket_buffer);
}

/* 【函数】bgp_peer_active2str —— 把“邻居是否可建立连接”的枚举翻译成调试字符串，
 * 供 accept 拒绝连接时打日志。仅用于日志，无副作用。 */
static const char *bgp_peer_active2str(enum bgp_peer_active active)
{
	switch (active) {
	case BGP_PEER_ACTIVE:
		return "active";
	case BGP_PEER_CONNECTION_UNSPECIFIED:
		return "unspecified connection";
	case BGP_PEER_BFD_DOWN:
		return "BFD down";
	case BGP_PEER_BFD_ADMIN_DOWN:
		return "BFD administrative shutdown";
	case BGP_PEER_AF_UNCONFIGURED:
		return "no AF activated";
	}

	assert(!"We should never get here this is a dev escape");
	return "ERROR";
}

/* 【函数】bgp_accept —— 被动侧的“读就绪”回调：监听 socket 上有新连接时被调用。
 * 这是本文件最长、最核心的函数，主线是“把一条到来的 TCP 连接，经一系列准入判定，
 * 变成 FSM 的一个 TCP_connection_open 事件”。步骤概览：
 *   1) 重新注册事件(事件回调需重挂才能继续监听)；accept() 取连接并置为非阻塞；
 *   2) 判定这条连接属于哪个 BGP 实例(VRF netns 场景直接取 listener->bgp，否则反查)；
 *   3) 设收发缓冲 / keepalive；按对端地址查是否已配置该 peer；
 *   4) 未命中时尝试“动态邻居(dynamic neighbor)”，命中则直接拉起会话；
 *   5) 已命中则做一串准入检查(动态邻居未收尾、admin shutdown、Clearing/Deleted 状态、
 *      AF 未激活、start 被抑制、router-id 未设、RPKI 严格模式等)；
 *   6) 通过后处理“连接冲突”：若已有连接(doppelganger)，新建一条作为分身并让新连接
 *      接管，旧连接交由 FSM 关闭(谁最终存活由状态机裁决)；
 *   7) 最后把 TCP_connection_open(或带延迟的变体)投递为 FSM 事件。
 * 注意 2) 之前的任一步失败都直接 close(bgp_sock) 返回，不做状态迁移。 */
/* Accept bgp connection. */
static void bgp_accept(struct event *event)
{
	int bgp_sock;
	int accept_sock;
	union sockunion su;
	struct bgp_listener *listener = EVENT_ARG(event);
	struct peer *doppelganger, *peer;
	struct peer_connection *connection, *incoming;
	char buf[SU_ADDRSTRLEN];
	struct bgp *bgp = NULL;
	enum bgp_peer_active active;

	sockunion_init(&su);

	bgp = bgp_lookup_by_name(listener->name);

	/* Register accept event. */
	accept_sock = EVENT_FD(event);
	if (accept_sock < 0) {
		flog_err_sys(EC_LIB_SOCKET,
			     "[Error] BGP accept socket fd is negative: %d",
			     accept_sock);
		return;
	}

	event_add_read(bm->master, bgp_accept, listener, accept_sock,
		       &listener->event);

	/* Accept client connection. */
	bgp_sock = sockunion_accept(accept_sock, &su);
	int save_errno = errno;
	if (bgp_sock < 0) {
		if (save_errno == EINVAL) {
			struct vrf *vrf =
				bgp ? vrf_lookup_by_id(bgp->vrf_id) : NULL;

			/*
			 * It appears that sometimes, when VRFs are deleted on
			 * the system, it takes a little while for us to get
			 * notified about that. In the meantime we endlessly
			 * loop on accept(), because the socket, having been
			 * bound to a now-deleted VRF device, is in some weird
			 * state which causes accept() to fail.
			 *
			 * To avoid this, if we see accept() fail with EINVAL,
			 * we cancel ourselves and trust that when the VRF
			 * deletion notification comes in the event handler for
			 * that will take care of cleaning us up.
			 */
			flog_err_sys(
				EC_LIB_SOCKET,
				"[Error] accept() failed with error \"%s\" on BGP listener socket %d for BGP instance in VRF \"%s\"; refreshing socket",
				safe_strerror(save_errno), accept_sock,
				VRF_LOGNAME(vrf));
			event_cancel(&listener->event);
		} else {
			flog_err_sys(
				EC_LIB_SOCKET,
				"[Error] BGP socket accept failed (%s); retrying",
				safe_strerror(save_errno));
		}
		return;
	}
	set_nonblocking(bgp_sock);

	/* 【实例归属】netns 场景下监听记录里已带实例(listener->bgp)；否则要按设备名反查。 */
	/* Obtain BGP instance this connection is meant for.
	 * - if it is a VRF netns sock, then BGP is in listener structure
	 * - otherwise, the bgp instance need to be demultiplexed
	 */
	if (listener->bgp)
		bgp = listener->bgp;
	else if (bgp_get_instance_for_inc_conn(bgp_sock, &bgp)) {
		if (bgp_debug_neighbor_events(NULL))
			zlog_debug(
				"[Event] Could not get instance for incoming conn from %s",
				inet_sutop(&su, buf));
		close(bgp_sock);
		return;
	}

	bgp_socket_set_buffer_size(bgp_sock);

	/* Set TCP keepalive when TCP keepalive is enabled */
	bgp_update_setsockopt_tcp_keepalive(bgp, bgp_sock);

	/* Check remote IP address */
	peer = peer_lookup(bgp, &su);

	/* 【未配置该 peer】尝试按“动态邻居”匹配(如 listen range / dynamic neighbor 创建
	 * 的临时 peer)；若匹配成功就地为它建立会话。 */
	if (!peer) {
		struct peer *dynamic_peer = peer_lookup_dynamic_neighbor(bgp, &su);

		if (dynamic_peer) {
			if (peergroup_flag_check(dynamic_peer, PEER_FLAG_RPKI_STRICT) &&
			    !bgp_rpki_cache_connected(dynamic_peer->bgp)) {
				if (bgp_debug_neighbor_events(dynamic_peer))
					zlog_debug("[Event] Incoming BGP connection rejected from %s due to RPKI cache not connected (strict mode)",
						   dynamic_peer->host);
				peer_delete(dynamic_peer);
				close(bgp_sock);
				return;
			}

			incoming = dynamic_peer->connection;
			/* Dynamic neighbor has been created, let it proceed */
			incoming->fd = bgp_sock;

			incoming->su_local = sockunion_getsockname(incoming->fd);
			incoming->su_remote = sockunion_dup(&su);

			if (bgp_set_socket_ttl(incoming) < 0) {
				peer_set_last_reset(dynamic_peer, PEER_DOWN_SOCKET_ERROR);
				flog_err(EC_BGP_TTL_SECURITY_FAIL,
					 "%s: Unable to set min/max TTL on peer %s (dynamic), error received: %s(%d)",
					 __func__, dynamic_peer->host, safe_strerror(errno), errno);
				frrtrace(3, frr_bgp, bgp_err_str, dynamic_peer->host,
					 dynamic_peer->flags, 1);

				incoming->fd = -1;
				close(bgp_sock);
				return;
			}

			/* Set the user configured MSS to TCP socket */
			if (CHECK_FLAG(dynamic_peer->flags, PEER_FLAG_TCP_MSS))
				sockopt_tcp_mss_set(bgp_sock, dynamic_peer->tcp_mss);

			frr_with_privs (&bgpd_privs) {
				vrf_bind(dynamic_peer->bgp->vrf_id, bgp_sock,
					 bgp_get_bound_name(incoming));
			}
			bgp_peer_connection_reg_with_nht(incoming);
			bgp_fsm_change_status(incoming, Active);
			event_cancel(&incoming->t_start);

			if (peer_active(incoming) == BGP_PEER_ACTIVE) {
				if (CHECK_FLAG(dynamic_peer->flags, PEER_FLAG_TIMER_DELAYOPEN))
					BGP_EVENT_ADD(incoming, TCP_connection_open_w_delay);
				else
					BGP_EVENT_ADD(incoming, TCP_connection_open);
			}

			return;
		}
	} else {
		if (CHECK_FLAG(peer->flags, PEER_FLAG_DYNAMIC_NEIGHBOR)) {
			zlog_debug("Received an open connection for a peering %s that we have not fully closed down yet",
				   peer->host);
			close(bgp_sock);

			return;
		}
	}

	/* 【未配置且非动态】既无静态配置、也不匹配任何动态邻居 ⇒ 拒绝该连接。 */
	if (!peer) {
		if (bgp_debug_neighbor_events(NULL)) {
			zlog_debug(
				"[Event] %s connection rejected(%s:%u:%s) - not configured and not valid for dynamic",
				inet_sutop(&su, buf), bgp->name_pretty, bgp->as,
				VRF_LOGNAME(vrf_lookup_by_id(bgp->vrf_id)));
		}
		close(bgp_sock);
		return;
	}

	/* 【准入检查】从这里到进入 FSM 之前是一连串“能不能收这条连接”的判定，任一不过
	 * 就 close 并返回。下面每段前的英文注释说明了具体原因。 */
	/* bgp pointer may be null, but since we have a peer data structure we know we have it */
	bgp = peer->bgp;
	connection = peer->connection;
	if (CHECK_FLAG(peer->flags, PEER_FLAG_SHUTDOWN) ||
	    CHECK_FLAG(peer->bgp->flags, BGP_FLAG_SHUTDOWN)) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug(
				"[Event] connection from %s rejected(%s:%u:%s) due to admin shutdown",
				inet_sutop(&su, buf), bgp->name_pretty, bgp->as,
				VRF_LOGNAME(vrf_lookup_by_id(bgp->vrf_id)));
		close(bgp_sock);
		return;
	}

	/* 【状态检查】Clearing(清理中) / Deleted 状态不接受新连接，否则会产生错误的
	 * 状态迁移(如连接又回到 Established，随后又冒出 Clearing_Completed)。 */
	/*
	 * Do not accept incoming connections in Clearing state. This can result
	 * in incorrect state transitions - e.g., the connection goes back to
	 * Established and then the Clearing_Completed event is generated. Also,
	 * block incoming connection in Deleted state.
	 */
	if (connection->status == Clearing || connection->status == Deleted) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("[Event] Closing incoming conn for %s (%p) state %d", peer->host,
				   peer, connection->status);
		close(bgp_sock);
		return;
	}

	/* 【地址族检查】至少激活了一个地址族(AF)才允许收连接；否则 reject。 */
	/* Check that at least one AF is activated for the peer. */
	active = peer_active(connection);
	if (active != BGP_PEER_ACTIVE) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s - incoming conn rejected - %s", peer->host,
				   bgp_peer_active2str(active));
		close(bgp_sock);
		return;
	}

	/* 【连接抑制】达到最大前缀数 / 重启定时器仍在跑 / 被 shutdown / router-id 未设
	 * 等情况下不重连(具体见下方英文注释)。 */
	/* Do not try to reconnect if the peer reached maximum
	 * prefixes, restart timer is still running or the peer
	 * is shutdown, or BGP identifier is not set (0.0.0.0).
	 */
	if (BGP_PEER_START_SUPPRESSED(peer)) {
		if (bgp_debug_neighbor_events(peer)) {
			if (peer->shut_during_cfg)
				zlog_debug("[Event] Incoming BGP connection rejected from %s due to configuration being currently read in",
					   peer->host);
			else
				zlog_debug("[Event] Incoming BGP connection rejected from %s due to maximum-prefix or shutdown",
					   peer->host);
		}
		close(bgp_sock);
		return;
	}

	/* 【router-id 检查】本端 BGP identifier 还是 0.0.0.0 时不能建会话。 */
	if (peer->bgp->router_id.s_addr == INADDR_ANY) {
		zlog_warn("[Event] Incoming BGP connection rejected from %s due missing BGP identifier, set it with `bgp router-id`",
			  peer->host);
		peer_set_last_reset(peer, PEER_DOWN_ROUTER_ID_ZERO);
		close(bgp_sock);
		return;
	}

	/* 【RPKI 检查】严格模式下 RPKI 缓存未连接则拒绝，避免放行未校验的路由。 */
	if (peergroup_flag_check(peer, PEER_FLAG_RPKI_STRICT) &&
	    !bgp_rpki_cache_connected(peer->bgp)) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("[Event] Incoming BGP connection rejected from %s due to RPKI cache not connected (strict mode)",
				   peer->host);
		peer_set_last_reset(peer, PEER_DOWN_RPKI_DOWN);
		close(bgp_sock);
		return;
	}

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("[Event] connection from %s fd %d, active peer status %d fd %d",
			   inet_sutop(&su, buf), bgp_sock, connection->status, connection->fd);

	/* 【连接冲突消解：旧连接侧】peer 已有 doppelganger(分身)说明此前已有一条连接。
	 * 这里先把旧分身删掉(下面会新建一个“分身”承接这条新连接)——谁最终存活由 FSM
	 * 依据 router-id / 连接方向等规则裁决，本函数只负责摆好两条候选。 */
	if (peer->doppelganger) {
		/* We have an existing connection. Kill the existing one and run
		   with this one.
		*/
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("[Event] New active connection from peer %s, Killing previous active connection",
				   peer->host);
		peer_delete(peer->doppelganger);
	}

	/* 【新建分身】为这条新连接创建一个 INCOMING 方向的 peer 作为 doppelganger：把原
	 * peer 的配置复制过来(peer_xfer_config)，处理 GR(优雅重启)标志，然后让两者互指。
	 * 至此同一邻居名下有了“旧连接 + 新连接”两条候选，交给 FSM 裁决。 */
	doppelganger = peer_create(&su, peer->conf_if, bgp, peer->local_as, peer->as,
				   peer->as_type, NULL, false, NULL, CONNECTION_INCOMING);

	incoming = doppelganger->connection;

	peer_xfer_config(doppelganger, peer);
	bgp_peer_gr_flags_update(doppelganger);

	BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA(bgp, bgp->peer);

	if (bgp_peer_gr_mode_get(doppelganger) == PEER_DISABLE) {
		UNSET_FLAG(doppelganger->sflags, PEER_STATUS_NSF_MODE);

		if (CHECK_FLAG(doppelganger->sflags, PEER_STATUS_NSF_WAIT)) {
			peer_nsf_stop(doppelganger);
		}
	}

	doppelganger->doppelganger = peer;
	peer->doppelganger = doppelganger;

	incoming->fd = bgp_sock;
	incoming->su_local = sockunion_getsockname(incoming->fd);
	incoming->su_remote = sockunion_dup(&su);

	if (bgp_set_socket_ttl(incoming) < 0)
		if (bgp_debug_neighbor_events(doppelganger))
			zlog_debug("[Event] Unable to set min/max TTL on peer %s, Continuing",
				   doppelganger->host);

	frr_with_privs(&bgpd_privs) {
		vrf_bind(bgp->vrf_id, bgp_sock, bgp_get_bound_name(incoming));
	}
	bgp_peer_connection_reg_with_nht(incoming);
	bgp_fsm_change_status(incoming, Active);
	event_cancel(&incoming->t_start); /* created in peer_create() */

	/* 【NSF 处理】若原连接已 Established 且处于 NSF(不中断转发)模式，则丢弃原连接、
	 * 状态退回 Connect，让新连接接管(见下方英文注释)。 */
	/* Make dummy peer until read Open packet. */
	if (peer_established(connection) && CHECK_FLAG(peer->sflags, PEER_STATUS_NSF_MODE)) {
		/* If we have an existing established connection with graceful
		 * restart
		 * capability announced with one or more address families, then
		 * drop
		 * existing established connection and move state to connect.
		 */
		peer_set_last_reset(peer, PEER_DOWN_NSF_CLOSE_SESSION);

		if (CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART) ||
		    CHECK_FLAG(peer->flags, PEER_FLAG_GRACEFUL_RESTART_HELPER))
			SET_FLAG(peer->sflags, PEER_STATUS_NSF_WAIT);

		bgp_event_update(connection, TCP_connection_closed);
	}

	/* 【投递 FSM 事件】把“TCP 已建立”送进状态机；配置了 delayopen 则用延迟变体
	 * (等一个探测期再发 OPEN)。 */
	if (peer_active(incoming) == BGP_PEER_ACTIVE) {
		if (CHECK_FLAG(doppelganger->flags, PEER_FLAG_TIMER_DELAYOPEN))
			BGP_EVENT_ADD(incoming, TCP_connection_open_w_delay);
		else
			BGP_EVENT_ADD(incoming, TCP_connection_open);
	}

	/*
	 * If we are doing nht for a peer that is v6 LL based
	 * massage the event system to make things happy
	 */
	bgp_nht_interface_events(doppelganger);
}

/* bgp_get_bound_name: 计算 BGP 连接 socket 在 bind 时应使用的“绑定名”。
 * 参数 connection 为对端连接，内部从 connection->peer 取出 peer。
 * 返回一个字符串: 有特殊绑定时返回接口名或 VRF 名；无需绑定时返回 NULL。
 * 处理优先级: IPv6 且配置了 conf_if(未编号或链路本地) 时优先用接口名；
 *   否则用 ifname；再否则 VIEW 实例返回 NULL；最后回退到 bgp->name(VRF 名)。
 * 注意: 默认 VRF 且未指定接口或 VRF 时直接返回 NULL，表示普通 bind。
 */
/* BGP socket bind. */
static char *bgp_get_bound_name(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;

	if ((peer->bgp->vrf_id == VRF_DEFAULT) && !peer->ifname
	    && !peer->conf_if)
		return NULL;

	if (connection->su.sa.sa_family != AF_INET &&
	    connection->su.sa.sa_family != AF_INET6)
		return NULL; // unexpected

	/* For IPv6 peering, interface (unnumbered or link-local with interface)
	 * takes precedence over VRF. For IPv4 peering, explicit interface or
	 * VRF are the situations to bind.
	 */
	if (connection->su.sa.sa_family == AF_INET6 && peer->conf_if)
		return peer->conf_if;

	if (peer->ifname)
		return peer->ifname;

	if (peer->bgp->inst_type == BGP_INSTANCE_TYPE_VIEW)
		return NULL;

	return peer->bgp->name;
}

/* bgp_update_address: 在接口 ifp 的已连接地址中，挑选与目的地址 dst“最长公共前缀”的那个，
 *   写回 addr 作为本端源地址。
 * 参数 ifp 接口；dst 目的地址；addr 为出参，保存选中的本地地址。
 * 返回 0 表示成功找到并写出 addr；返回 1 表示对方地址非法，或没有同地址族的可用地址。
 * 实现: 遍历 if_connected，跳过地址族不同的项，用 prefix_common_bits 比较取公共位最多者。
 */
int bgp_update_address(struct interface *ifp, const union sockunion *dst,
			      union sockunion *addr)
{
	struct prefix *p, *sel, d;
	struct connected *connected;
	int common;

	if (!sockunion2hostprefix(dst, &d))
		return 1;

	sel = NULL;
	common = -1;

	frr_each (if_connected, ifp->connected, connected) {
		p = connected->address;
		if (p->family != d.family)
			continue;
		if (prefix_common_bits(p, &d) > common) {
			sel = p;
			common = prefix_common_bits(sel, &d);
		}
	}

	if (!sel)
		return 1;

	prefix2sockunion(sel, addr);
	return 0;
}

/* bgp_update_source: 按 peer 配置的源接口或源地址，对已创建的连接 socket 执行 bind。
 * 参数 connection 为对端连接，从中取 peer。
 * 返回 0 成功；返回 -1 失败(接口不存在，或接口上找不到匹配的源地址)。
 * 两种来源: update_if 指定接口名(先查接口再选地址)；update_source 指定源 IP。
 * 副作用: 直接改变 connection->fd 的绑定地址，不改动 fd 本身。
 */
/* Update source selection.  */
static int bgp_update_source(struct peer_connection *connection)
{
	struct interface *ifp;
	union sockunion addr;
	int ret = 0;
	struct peer *peer = connection->peer;

	sockunion_init(&addr);

	/* Source is specified with interface name.  */
	if (peer->update_if) {
		ifp = if_lookup_by_name(peer->update_if, peer->bgp->vrf_id);
		if (!ifp)
			return -1;

		if (bgp_update_address(ifp, &connection->su, &addr))
			return -1;

		ret = sockunion_bind(connection->fd, &addr, 0, &addr);
	}

	/* Source is specified with IP address.  */
	if (peer->update_source)
		ret = sockunion_bind(connection->fd, peer->update_source, 0,
				     peer->update_source);

	return ret;
}

/* bgp_connect: BGP 主动向对端发起 TCP 连接，创建并初始化连接 socket。
 * 参数 connection 为待连接的对端连接(内含 peer 及目的 sockunion)。
 * 返回 enum connect_result: 通常为 connect_error，或底层 sockunion_connect 的返回结果。
 * 步骤: 校验 router-id 与目的地址；在特权域内创建 socket；依次设置非阻塞、MSS、
 *   ip-transparent、缓冲区、TCP keepalive、TTL、reuseaddr 与 reuseport、TOS、MD5 认证；
 *   执行 update-source 绑定；最后 sockunion_connect 并记录 su_remote 与 su_local。
 * 易错点: passive 模式会直接投递 TCP_connection_open_failed 事件并返回 connect_error；
 *   若读写线程已然开启会触发断言失败。socket 创建失败会重置会话状态。
 */
/* BGP try to connect to the peer.  */
enum connect_result bgp_connect(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;

	assert(!CHECK_FLAG(connection->thread_flags, PEER_THREAD_WRITES_ON));
	assert(!CHECK_FLAG(connection->thread_flags, PEER_THREAD_READS_ON));

	if (peer->bgp->router_id.s_addr == INADDR_ANY) {
		peer_set_last_reset(peer, PEER_DOWN_ROUTER_ID_ZERO);
		zlog_warn("%s: BGP identifier is missing for peer %s, set it with `bgp router-id`",
			  __func__, peer->host);
		return connect_error;
	}

	if (peer->conf_if && BGP_CONNECTION_SU_UNSPEC(connection)) {
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("Peer address not learnt: Returning from connect");
		return connect_error;
	}
	frr_with_privs(&bgpd_privs) {
		/* Make socket for the peer. */
		connection->fd =
			vrf_sockunion_socket(&connection->su, peer->bgp->vrf_id,
					     bgp_get_bound_name(connection));
		connection->dir = CONNECTION_OUTGOING;
	}
	if (connection->fd < 0) {
		peer_set_last_reset(peer, PEER_DOWN_SOCKET_ERROR);
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s: Failure to create socket for connection to %s, error received: %s(%d)",
				   __func__, peer->host, safe_strerror(errno),
				   errno);
		frrtrace(3, frr_bgp, bgp_err_str, peer->host, peer->flags, 2);
		return connect_error;
	}

	set_nonblocking(connection->fd);

	/* Set the user configured MSS to TCP socket */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_TCP_MSS))
		sockopt_tcp_mss_set(connection->fd, peer->tcp_mss);

	/* Neighbor's bind() as a source address when ip transparent is used */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_IP_TRANSPARENT) &&
	    CHECK_FLAG(peer->flags, PEER_FLAG_UPDATE_SOURCE)) {
		frr_with_privs (&bgpd_privs) {
			sockopt_ip_transparent(connection->fd);
		}
	}

	bgp_socket_set_buffer_size(connection->fd);

	/* Set TCP keepalive when TCP keepalive is enabled */
	bgp_update_setsockopt_tcp_keepalive(peer->bgp, connection->fd);

	if (bgp_set_socket_ttl(peer->connection) < 0) {
		peer_set_last_reset(peer, PEER_DOWN_SOCKET_ERROR);
		if (bgp_debug_neighbor_events(peer))
			zlog_debug("%s: Failure to set socket ttl for connection to %s, error received: %s(%d)",
				   __func__, peer->host, safe_strerror(errno),
				   errno);

		return connect_error;
	}

	sockopt_reuseaddr(connection->fd);
	sockopt_reuseport(connection->fd);

#ifdef IPTOS_PREC_INTERNETCONTROL
	frr_with_privs(&bgpd_privs) {
		if (sockunion_family(&connection->su) == AF_INET)
			setsockopt_ipv4_tos(connection->fd, bm->ip_tos);
		else if (sockunion_family(&connection->su) == AF_INET6)
			setsockopt_ipv6_tclass(connection->fd, bm->ip_tos);
	}
#endif

	if (peer->password) {
		uint16_t prefixlen = peer->connection->su.sa.sa_family == AF_INET
					     ? IPV4_MAX_BITLEN
					     : IPV6_MAX_BITLEN;

		if (!BGP_CONNECTION_SU_UNSPEC(connection))
			bgp_md5_set(connection);

		bgp_md5_set_connect(connection->fd, &connection->su, prefixlen,
				    peer->password);
	}

	/* Update source bind. */
	if (bgp_update_source(connection) < 0) {
		peer_set_last_reset(peer, PEER_DOWN_SOCKET_ERROR);
		return connect_error;
	}

	/* If the peer is passive mode, force to move to Active mode. */
	if (CHECK_FLAG(peer->flags, PEER_FLAG_PASSIVE)) {
		BGP_EVENT_ADD(connection, TCP_connection_open_failed);
		return connect_error;
	}

	if (bgp_debug_neighbor_events(peer))
		zlog_debug("%s [Event] Connect start to %s fd %d", peer->host,
			   peer->host, connection->fd);

	/* Connect to the remote peer. */
	enum connect_result res;

	res = sockunion_connect(connection->fd, &connection->su, htons(peer->port));

	if (connection->su_remote)
		sockunion_free(connection->su_remote);

	connection->su_remote = sockunion_dup(&connection->su);
	switch (connection->su.sa.sa_family) {
	case AF_INET:
		connection->su_remote->sin.sin_port = htons(peer->port);
		break;
	case AF_INET6:
		connection->su_remote->sin6.sin6_port = htons(peer->port);
		break;
	}

	if (connection->su_local)
		sockunion_free(connection->su_local);
	connection->su_local = sockunion_getsockname(connection->fd);

	return res;
}

/* bgp_getsockname: TCP 建连完成后，结合本地与远端地址解析并记录 peer 的下一跳信息。
 * 参数 connection 为对端连接，使用其 su_local 与 su_remote，结果写入 peer->nexthop。
 * 返回 0 表示成功；返回 -1 表示下一跳解析失败(会记录日志并复位连接)。
 * 特例: 若解析失败但配置了 ip-transparent 且指定了 update_source，则按配置放行返回 0。
 */
/* After TCP connection is established.  Get local address and port. */
int bgp_getsockname(struct peer_connection *connection)
{
	struct peer *peer = connection->peer;

	if (!bgp_zebra_nexthop_set(connection->su_local, connection->su_remote, &peer->nexthop,
				   peer)) {
		/* no nexthop, but the admin has enforced the source address with an ip-transparent mode
		 * so let's honor the configuration.
		 */
		if (CHECK_FLAG(peer->flags, PEER_FLAG_IP_TRANSPARENT) &&
		    CHECK_FLAG(peer->flags, PEER_FLAG_UPDATE_SOURCE))
			return 0;
		flog_err(EC_BGP_NH_UPD,
			 "%s: nexthop_set failed, local: %pSUp remote: %pSUp update_if: %s resetting connection - intf %s",
			 peer->host, connection->su_local, connection->su_remote,
			 peer->update_if ? peer->update_if : "(None)",
			 peer->nexthop.ifp ? peer->nexthop.ifp->name : "(Unknown)");
		return -1;
	}
	return 0;
}


/* bgp_listener: 把一个已创建的监听 socket 绑定并投入监听，登记为 BGP 监听器。
 * 参数 sock 已打开的 socket；sa 与 salen 指定绑定地址；bgp 归属的 BGP 实例。
 * 返回 0 成功；返回负值表示 bind 或 listen 失败(已记录日志)。
 * 步骤: 设置 reuseaddr 与 reuseport、TOS、v6only；bind；listen；
 *   分配并填充 bgp_listener 结构，注册 bgp_accept 的可读事件，加入 bm->listen_sockets。
 * 注意: 仅当 bgp->vrf_id 非默认 VRF 时，listener->bgp 才回指该实例，用于后续区分主 socket。
 */
static int bgp_listener(int sock, struct sockaddr *sa, socklen_t salen,
			struct bgp *bgp)
{
	struct bgp_listener *listener;
	int ret, en;

	sockopt_reuseaddr(sock);
	sockopt_reuseport(sock);

	frr_with_privs(&bgpd_privs) {

#ifdef IPTOS_PREC_INTERNETCONTROL
		if (sa->sa_family == AF_INET)
			setsockopt_ipv4_tos(sock, bm->ip_tos);
		else if (sa->sa_family == AF_INET6)
			setsockopt_ipv6_tclass(sock, bm->ip_tos);
#endif

		sockopt_v6only(sa->sa_family, sock);

		ret = bind(sock, sa, salen);
		en = errno;
	}

	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET, "bind: %s", safe_strerror(en));
		return ret;
	}

	ret = listen(sock, SOMAXCONN);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET, "listen: %s", safe_strerror(errno));
		return ret;
	}

	listener = XCALLOC(MTYPE_BGP_LISTENER, sizeof(*listener));
	listener->fd = sock;
	listener->name = XSTRDUP(MTYPE_BGP_LISTENER, bgp->name);

	/* this socket is in a vrf record bgp back pointer */
	if (bgp->vrf_id != VRF_DEFAULT)
		listener->bgp = bgp;

	memcpy(&listener->su, sa, salen);
	event_add_read(bm->master, bgp_accept, listener, sock, &listener->event);
	listnode_add(bm->listen_sockets, listener);

	return 0;
}

/* bgp_socket: 为 BGP 实例建立服务端监听 socket(同时支持 IPv4 与 IPv6)。
 * 参数 bgp 实例；port 监听端口；address 绑定地址(可为 NULL 表示通配)。
 * 返回 0 成功；返回 -1 表示 getaddrinfo 失败或该配置不允许建立 socket。
 * 流程: 通过 getaddrinfo 解析地址列表；逐项创建 socket、设置 TTL(为 ttl-security 预留)、
 *   调用 bgp_listener 绑定监听；清理 addrinfo 并统计成功个数。
 * 易错点: 若 NO_ZEBRA 且非默认 VRF 会直接返回 -1；非 VRF 实例若一个可用地址都没有，
 *   会记录错误并调用 exit(-1) 终止进程。
 */
/* IPv6 supported version of BGP server socket setup.  */
int bgp_socket(struct bgp *bgp, unsigned short port, const char *address)
{
	struct addrinfo *ainfo;
	struct addrinfo *ainfo_save;
	static const struct addrinfo req = {
		.ai_family = AF_UNSPEC,
		.ai_flags = AI_PASSIVE,
		.ai_socktype = SOCK_STREAM,
	};
	int ret, count;
	char port_str[BUFSIZ];

	snprintf(port_str, sizeof(port_str), "%d", port);
	port_str[sizeof(port_str) - 1] = '\0';

	frr_with_privs(&bgpd_privs) {
		ret = vrf_getaddrinfo(address, port_str, &req, &ainfo_save,
				      bgp->vrf_id);
	}
	if (ret != 0) {
		flog_err_sys(EC_LIB_SOCKET, "getaddrinfo: %s",
			     gai_strerror(ret));
		return -1;
	}
	if (bgp_option_check(BGP_OPT_NO_ZEBRA) &&
	    bgp->vrf_id != VRF_DEFAULT) {
		freeaddrinfo(ainfo_save);
		return -1;
	}
	count = 0;
	for (ainfo = ainfo_save; ainfo; ainfo = ainfo->ai_next) {
		int sock;

		if (ainfo->ai_family != AF_INET && ainfo->ai_family != AF_INET6)
			continue;

		frr_with_privs(&bgpd_privs) {
			sock = vrf_socket(ainfo->ai_family,
					  ainfo->ai_socktype,
					  ainfo->ai_protocol,
					  bgp->vrf_id,
					  (bgp->inst_type
					   == BGP_INSTANCE_TYPE_VRF
					   ? bgp->name : NULL));
		}
		if (sock < 0) {
			flog_err_sys(EC_LIB_SOCKET, "socket: %s",
				     safe_strerror(errno));
			continue;
		}

		/* if we intend to implement ttl-security, this socket needs
		 * ttl=255 */
		ret = sockopt_ttl(ainfo->ai_family, sock, MAXTTL);
		if (ret < 0) {
			flog_err_sys(EC_LIB_SOCKET, "%s: Can't set TTL on socket %d err = %s",
				     __func__, sock, safe_strerror(errno));
			close(sock);
			continue;
		}

		ret = bgp_listener(sock, ainfo->ai_addr, ainfo->ai_addrlen,
				   bgp);
		if (ret == 0)
			++count;
		else
			close(sock);
	}
	freeaddrinfo(ainfo_save);
	if (count == 0 && bgp->inst_type != BGP_INSTANCE_TYPE_VRF) {
		flog_err(
			EC_LIB_SOCKET,
			"%s: no usable addresses please check other programs usage of specified port %d",
			__func__, port);
		flog_err_sys(EC_LIB_SOCKET, "%s: Program cannot continue",
			     __func__);
		exit(-1);
	}

	return 0;
}

/* bgp_close_vrf_socket: 关闭属于指定 VRF 实例的监听 socket，并释放其监听器资源。
 * 参数 bgp 目标实例；仅应在 netns 后端的 VRF socket 上调用。
 * 无返回值；对 bgp 或 bm->listen_sockets 为空的情况直接返回。
 * 遍历 bm->listen_sockets，匹配 listener->bgp==bgp 的项: 取消事件、关闭 fd、
 *   从链表摘除并释放 name 与 listener 结构。
 */
/* this function closes vrf socket
 * this should be called only for vrf socket with netns backend
 */
void bgp_close_vrf_socket(struct bgp *bgp)
{
	struct listnode *node, *next;
	struct bgp_listener *listener;

	if (!bgp)
		return;

	if (bm->listen_sockets == NULL)
		return;

	for (ALL_LIST_ELEMENTS(bm->listen_sockets, node, next, listener)) {
		if (listener->bgp == bgp) {
			event_cancel(&listener->event);
			close(listener->fd);
			listnode_delete(bm->listen_sockets, listener);
			XFREE(MTYPE_BGP_LISTENER, listener->name);
			XFREE(MTYPE_BGP_LISTENER, listener);
		}
	}
}

/* bgp_close: 关闭并释放所有“主”监听 socket(即非任何 VRF 归属的监听器)。
 * 无参数，无返回值；bm->listen_sockets 为空时直接返回。
 * 遍历 bm->listen_sockets，跳过 listener->bgp 非空的项(那些属于 VRF，由另一函数负责)，
 *   对其余项取消事件、关闭 fd、从链表摘除并释放 name 与 listener 结构。
 */
/* this function closes main socket
 */
void bgp_close(void)
{
	struct listnode *node, *next;
	struct bgp_listener *listener;

	if (bm->listen_sockets == NULL)
		return;

	for (ALL_LIST_ELEMENTS(bm->listen_sockets, node, next, listener)) {
		if (listener->bgp)
			continue;
		event_cancel(&listener->event);
		close(listener->fd);
		listnode_delete(bm->listen_sockets, listener);
		XFREE(MTYPE_BGP_LISTENER, listener->name);
		XFREE(MTYPE_BGP_LISTENER, listener);
	}
}
