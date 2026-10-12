// SPDX-License-Identifier: GPL-2.0-or-later
/* Kernel communication using netlink interface.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

#include <zebra.h>
#include <fcntl.h>

#ifdef HAVE_NETLINK
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/filter.h>

#include "linklist.h"
#include "if.h"
#include "log.h"
#include "prefix.h"
#include "connected.h"
#include "table.h"
#include "memory.h"
#include "rib.h"
#include "frrevent.h"
#include "privs.h"
#include "nexthop.h"
#include "vrf.h"
#include "mpls.h"
#include "lib_errors.h"
#include "hash.h"
#include "lib/netlink_parser.h"

#include "zebra/zebra_router.h"
#include "zebra/zebra_ns.h"
#include "zebra/zebra_vrf.h"
#include "zebra/rt.h"
#include "zebra/debug.h"
#include "zebra/kernel_netlink.h"
#include "zebra/rt_netlink.h"
#include "zebra/if_netlink.h"
#include "zebra/rule_netlink.h"
#include "zebra/tc_netlink.h"
#include "zebra/netconf_netlink.h"
#include "zebra/zebra_errors.h"
#include "zebra/ge_netlink.h"
#include "zebra/zebra_trace.h"

#ifndef SO_RCVBUFFORCE
#define SO_RCVBUFFORCE  (33)
#endif

/* Hack for GNU libc version 2. */
#ifndef MSG_TRUNC
#define MSG_TRUNC      0x20
#endif /* MSG_TRUNC */

#ifndef RTNL_FAMILY_IP6MR
#define RTNL_FAMILY_IP6MR 129
#endif

#ifndef RTPROT_MROUTED
#define RTPROT_MROUTED 17
#endif

#define NL_DEFAULT_BATCH_BUFSIZE (16 * NL_PKT_BUF_SIZE)

/*
 * We limit the batch's size to a number smaller than the length of the
 * underlying buffer since the last message that wouldn't fit the batch would go
 * over the upper boundary and then it would have to be encoded again into a new
 * buffer. If the difference between the limit and the length of the buffer is
 * big enough (bigger than the biggest Netlink message) then this situation
 * won't occur.
 */
#define NL_DEFAULT_BATCH_SEND_THRESHOLD (15 * NL_PKT_BUF_SIZE)

static const struct message nlmsg_str[] = {
	{ RTM_NEWROUTE, "RTM_NEWROUTE" },
	{ RTM_DELROUTE, "RTM_DELROUTE" },
	{ RTM_GETROUTE, "RTM_GETROUTE" },
	{ RTM_NEWLINK, "RTM_NEWLINK" },
	{ RTM_SETLINK, "RTM_SETLINK" },
	{ RTM_DELLINK, "RTM_DELLINK" },
	{ RTM_GETLINK, "RTM_GETLINK" },
	{ RTM_NEWADDR, "RTM_NEWADDR" },
	{ RTM_DELADDR, "RTM_DELADDR" },
	{ RTM_GETADDR, "RTM_GETADDR" },
	{ RTM_NEWNEIGH, "RTM_NEWNEIGH" },
	{ RTM_DELNEIGH, "RTM_DELNEIGH" },
	{ RTM_GETNEIGH, "RTM_GETNEIGH" },
	{ RTM_NEWRULE, "RTM_NEWRULE" },
	{ RTM_DELRULE, "RTM_DELRULE" },
	{ RTM_GETRULE, "RTM_GETRULE" },
	{ RTM_NEWNEXTHOP, "RTM_NEWNEXTHOP" },
	{ RTM_DELNEXTHOP, "RTM_DELNEXTHOP" },
	{ RTM_GETNEXTHOP, "RTM_GETNEXTHOP" },
	{ RTM_NEWNETCONF, "RTM_NEWNETCONF" },
	{ RTM_DELNETCONF, "RTM_DELNETCONF" },
	{ RTM_NEWTUNNEL, "RTM_NEWTUNNEL" },
	{ RTM_DELTUNNEL, "RTM_DELTUNNEL" },
	{ RTM_GETTUNNEL, "RTM_GETTUNNEL" },
	{ RTM_NEWQDISC, "RTM_NEWQDISC" },
	{ RTM_DELQDISC, "RTM_DELQDISC" },
	{ RTM_GETQDISC, "RTM_GETQDISC" },
	{ RTM_NEWTCLASS, "RTM_NEWTCLASS" },
	{ RTM_DELTCLASS, "RTM_DELTCLASS" },
	{ RTM_GETTCLASS, "RTM_GETTCLASS" },
	{ RTM_NEWTFILTER, "RTM_NEWTFILTER" },
	{ RTM_DELTFILTER, "RTM_DELTFILTER" },
	{ RTM_GETTFILTER, "RTM_GETTFILTER" },
	{ RTM_NEWVLAN, "RTM_NEWVLAN" },
	{ RTM_DELVLAN, "RTM_DELVLAN" },
	{ RTM_GETVLAN, "RTM_GETVLAN" },
	{ RTM_NEWCHAIN, "RTM_NEWCHAIN" },
	{ RTM_DELCHAIN, "RTM_DELCHAIN" },
	{ RTM_GETCHAIN, "RTM_GETCHAIN" },
	{ 0 }
};

static const struct message rtproto_str[] = {
	{RTPROT_REDIRECT, "redirect"},
	{RTPROT_KERNEL, "kernel"},
	{RTPROT_BOOT, "boot"},
	{RTPROT_STATIC, "static"},
	{RTPROT_GATED, "GateD"},
	{RTPROT_RA, "router advertisement"},
	{RTPROT_MRT, "MRT"},
	{RTPROT_ZEBRA, "Zebra"},
#ifdef RTPROT_BIRD
	{RTPROT_BIRD, "BIRD"},
#endif /* RTPROT_BIRD */
	{RTPROT_MROUTED, "mroute"},
	{RTPROT_BGP, "BGP"},
	{RTPROT_OSPF, "OSPF"},
	{RTPROT_ISIS, "IS-IS"},
	{RTPROT_RIP, "RIP"},
	{RTPROT_RIPNG, "RIPNG"},
	{RTPROT_ZSTATIC, "static"},
	{0}};

static const struct message family_str[] = {{AF_INET, "ipv4"},
					    {AF_INET6, "ipv6"},
					    {AF_BRIDGE, "bridge"},
					    {RTNL_FAMILY_IPMR, "ipv4MR"},
					    {RTNL_FAMILY_IP6MR, "ipv6MR"},
					    {0}};

static const struct message rttype_str[] = {{RTN_UNSPEC, "none"},
					    {RTN_UNICAST, "unicast"},
					    {RTN_LOCAL, "local"},
					    {RTN_BROADCAST, "broadcast"},
					    {RTN_ANYCAST, "anycast"},
					    {RTN_MULTICAST, "multicast"},
					    {RTN_BLACKHOLE, "blackhole"},
					    {RTN_UNREACHABLE, "unreachable"},
					    {RTN_PROHIBIT, "prohibited"},
					    {RTN_THROW, "throw"},
					    {RTN_NAT, "nat"},
					    {RTN_XRESOLVE, "resolver"},
					    {0}};

extern struct event_loop *master;

extern struct zebra_privs_t zserv_privs;

DEFINE_MTYPE_STATIC(ZEBRA, NL_BUF, "Zebra Netlink buffers");

/* Hashtable and mutex to allow lookup of nlsock structs by socket/fd value.
 * We have both the main and dplane pthreads using these structs, so we have
 * to protect the hash with a lock.
 */
static struct hash *nlsock_hash;
pthread_mutex_t nlsock_mutex;

/* Lock and unlock wrappers for nlsock hash */
#define NLSOCK_LOCK() pthread_mutex_lock(&nlsock_mutex)
#define NLSOCK_UNLOCK() pthread_mutex_unlock(&nlsock_mutex)

size_t nl_batch_tx_bufsize;
char *nl_batch_tx_buf;

_Atomic uint32_t nl_batch_bufsize = NL_DEFAULT_BATCH_BUFSIZE;
_Atomic uint32_t nl_batch_send_threshold = NL_DEFAULT_BATCH_SEND_THRESHOLD;

struct nl_batch {
	void *buf;
	size_t bufsiz;
	size_t limit;

	void *buf_head;
	size_t curlen;
	size_t msgcnt;

	const struct zebra_dplane_info *zns;

	struct dplane_ctx_list_head ctx_list;

	/*
	 * Pointer to the queue of completed contexts outbound back
	 * towards the dataplane module.
	 */
	struct dplane_ctx_list_head *ctx_out_q;
};

int netlink_config_write_helper(struct vty *vty)
{
	uint32_t size =
		atomic_load_explicit(&nl_batch_bufsize, memory_order_relaxed);
	uint32_t threshold = atomic_load_explicit(&nl_batch_send_threshold,
						  memory_order_relaxed);

	if (size != NL_DEFAULT_BATCH_BUFSIZE
	    || threshold != NL_DEFAULT_BATCH_SEND_THRESHOLD)
		vty_out(vty, "zebra kernel netlink batch-tx-buf %u %u\n", size,
			threshold);

	if (if_netlink_frr_protodown_r_bit_is_set())
		vty_out(vty, "zebra protodown reason-bit %u\n",
			if_netlink_get_frr_protodown_r_bit());

	return 0;
}

void netlink_set_batch_buffer_size(uint32_t size, uint32_t threshold, bool set)
{
	if (!set) {
		size = NL_DEFAULT_BATCH_BUFSIZE;
		threshold = NL_DEFAULT_BATCH_SEND_THRESHOLD;
	}

	atomic_store_explicit(&nl_batch_bufsize, size, memory_order_relaxed);
	atomic_store_explicit(&nl_batch_send_threshold, threshold,
			      memory_order_relaxed);
}

int netlink_talk_filter(struct nlmsghdr *h, ns_id_t ns_id, int startup)
{
	/*
	 * This is an error condition that must be handled during
	 * development.
	 *
	 * The netlink_talk_filter function is used for communication
	 * down the netlink_cmd pipe and we are expecting
	 * an ack being received.  So if we get here
	 * then we did not receive the ack and instead
	 * received some other message in an unexpected
	 * way.
	 */
	zlog_debug("%s: ignoring message type 0x%04x(%s) NS %u", __func__,
		   h->nlmsg_type, nl_msg_type_to_str(h->nlmsg_type), ns_id);
	return 0;
}

static int netlink_recvbuf(struct nlsock *nl, uint32_t newsize)
{
	uint32_t oldsize;
	socklen_t newlen = sizeof(newsize);
	socklen_t oldlen = sizeof(oldsize);
	int ret;

	ret = getsockopt(nl->sock, SOL_SOCKET, SO_RCVBUF, &oldsize, &oldlen);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET,
			     "Can't get %s receive buffer size: %s", nl->name,
			     safe_strerror(errno));
		return -1;
	}

	/* Try force option (linux >= 2.6.14) and fall back to normal set */
	frr_with_privs(&zserv_privs) {
		ret = setsockopt(nl->sock, SOL_SOCKET, SO_RCVBUFFORCE,
				 &rcvbufsize, sizeof(rcvbufsize));
	}
	if (ret < 0)
		ret = setsockopt(nl->sock, SOL_SOCKET, SO_RCVBUF, &rcvbufsize,
				 sizeof(rcvbufsize));
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET,
			     "Can't set %s receive buffer size: %s", nl->name,
			     safe_strerror(errno));
		return -1;
	}

	ret = getsockopt(nl->sock, SOL_SOCKET, SO_RCVBUF, &newsize, &newlen);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET,
			     "Can't get %s receive buffer size: %s", nl->name,
			     safe_strerror(errno));
		return -1;
	}
	return 0;
}

static const char *group2str(uint32_t group)
{
	switch (group) {
	case RTNLGRP_TUNNEL:
		return "RTNLGRP_TUNNEL";
	default:
		return "UNKNOWN";
	}
}

/* Make socket for Linux netlink interface. */
/*
 * netlink_socket：
 *   创建一个 Linux netlink socket，并完成：
 *     1. 在指定网络命名空间中创建 socket
 *     2. 设置传统 32 位多播组位图 nl_groups
 *     3. 对无法放入 nl_groups 的额外多播组调用 setsockopt 订阅
 *     4. bind 到 netlink 地址
 *     5. 获取内核分配的本地 nl_pid 等信息
 *     6. 分配接收缓冲区并保存到 nlsock 结构
 *
 * 参数：
 *   nl             输出参数，保存创建好的 socket、地址、缓冲区等信息
 *   groups         传统多播组位图，直接赋给 sockaddr_nl.nl_groups
 *   ext_groups     额外多播组 ID 数组，通常用于编号 >= 32 的组
 *   ext_group_size ext_groups 数组长度
 *   ns_id          网络命名空间 ID
 *   nl_family      netlink 协议族，例如 NETLINK_ROUTE、NETLINK_GENERIC
 *
 * 返回值：
 *   成功返回 0；失败返回 -1，并关闭已创建的 socket。
 */
static int netlink_socket(struct nlsock *nl, unsigned long groups,
			  uint32_t ext_groups[], uint8_t ext_group_size,
			  ns_id_t ns_id, int nl_family)
{
	int ret;                 /* 通用返回值，用于 bind、getsockname 等 */
	struct sockaddr_nl snl;  /* netlink socket 地址结构 */
	int sock;                /* 新创建的 socket 文件描述符 */
	int namelen;             /* getsockname 使用的地址长度 */

	/*
	 * 在提升权限的情况下创建 socket。
	 * netlink socket 的创建和绑定可能需要 CAP_NET_ADMIN 等权限。
	 * frr_with_privs 宏会在代码块内临时切换到 zserv_privs 权限集。
	 */
	frr_with_privs(&zserv_privs) {
		/*
		 * 在指定网络命名空间中创建 netlink socket。
		 *   AF_NETLINK  地址族
		 *   SOCK_RAW    原始套接字
		 *   nl_family   netlink 协议族，如 NETLINK_ROUTE
		 *   ns_id       目标网络命名空间 ID
		 */
		sock = ns_socket(AF_NETLINK, SOCK_RAW, nl_family, ns_id);

		/* socket 创建失败，记录错误并返回 -1 */
		if (sock < 0) {
			flog_err(EC_LIB_SOCKET, "Can't open %s socket: %s", nl->name,
				 safe_strerror(errno));
			return -1;
		}

		/* 清空 netlink 地址结构，避免未初始化字段 */
		memset(&snl, 0, sizeof(snl));

		/* 设置地址族为 AF_NETLINK */
		snl.nl_family = AF_NETLINK;

		/*
		 * 设置多播组位图。
		 * 这是传统方式，最多只能表示 32 个组。
		 */
		snl.nl_groups = groups;

		/*
		 * 如果调用方传入了额外多播组数组，则逐个订阅。
		 * 这些组通常编号 >= 32，无法放入 nl_groups 位图，
		 * 必须通过 setsockopt(NETLINK_ADD_MEMBERSHIP) 订阅。
		 */
		if (ext_group_size) {
			uint8_t i;

			for (i = 0; i < ext_group_size; i++) {
#if defined SOL_NETLINK
				/*
				 * 使用 SOL_NETLINK 级别的 NETLINK_ADD_MEMBERSHIP
				 * 选项订阅单个多播组。
				 * ext_groups[i] 是要订阅的组 ID。
				 */
				ret = setsockopt(sock, SOL_NETLINK,
						 NETLINK_ADD_MEMBERSHIP,
						 &ext_groups[i],
						 sizeof(ext_groups[i]));

				/*
				 * 如果订阅失败，通常说明当前内核不支持该组，
				 * 或者不支持 NETLINK_ADD_MEMBERSHIP 选项。
				 * 这里只记录 notice，不终止整个 socket 创建流程。
				 */
				if (ret < 0) {
					zlog_notice(
						"can't setsockopt NETLINK_ADD_MEMBERSHIP for group %s(%u), this linux kernel does not support it: %s(%d)",
						group2str(ext_groups[i]),
						ext_groups[i],
						safe_strerror(errno), errno);
				}
#else
				/*
				 * 如果编译环境没有定义 SOL_NETLINK，
				 * 则无法使用 NETLINK_ADD_MEMBERSHIP。
				 * 这里只记录 notice，并继续后续流程。
				 */
				zlog_notice(
					"Unable to use NETLINK_ADD_MEMBERSHIP via SOL_NETLINK for %s(%u) since the linux kernel does not support the socket option",
					group2str(ext_groups[i]),
					ext_groups[i]);
#endif
			}
		}

		/*
		 * 将 socket 绑定到 netlink 地址结构。
		 * 对于 netlink socket，bind 可以使多播组订阅生效，
		 * 同时让内核为该 socket 分配本地 nl_pid。
		 * 这里 snl.nl_pid 默认为 0，表示由内核自动分配。
		 */
		/* Bind the socket to the netlink structure for anything. */
		ret = bind(sock, (struct sockaddr *)&snl, sizeof(snl));
	}

	/*
	 * 检查 bind 是否成功。
	 * 注意：ret 是在 frr_with_privs 代码块内由 bind 设置的。
	 */
	if (ret < 0) {
		flog_err(EC_LIB_SOCKET, "Can't bind %s socket to group 0x%x: %s", nl->name,
			 snl.nl_groups, safe_strerror(errno));
		close(sock);
		return -1;
	}

	/*
	 * 获取 socket 的本地地址信息。
	 * 多个 netlink socket 会有不同的 nl_pid，因此需要通过 getsockname
	 * 取回内核分配的 nl_pid，并保存到 nl->snl 中。
	 */
	/* multiple netlink sockets will have different nl_pid */
	namelen = sizeof(snl);
	ret = getsockname(sock, (struct sockaddr *)&snl, (socklen_t *)&namelen);

	/*
	 * 如果 getsockname 失败，或者返回的地址长度不是 sizeof(snl)，
	 * 说明获取 socket 名称失败，关闭 socket 并返回 -1。
	 */
	if (ret < 0 || namelen != sizeof(snl)) {
		flog_err_sys(EC_LIB_SOCKET, "Can't get %s socket name: %s",
			     nl->name, safe_strerror(errno));
		close(sock);
		return -1;
	}

	/* 将获取到的 netlink 地址信息保存到 nlsock 结构 */
	nl->snl = snl;

	/* 保存 socket 文件描述符 */
	nl->sock = sock;

	/* 设置默认接收缓冲区长度 */
	nl->buflen = NL_RCV_PKT_BUF_SIZE;

	/* 分配接收缓冲区内存，用于后续读取 netlink 消息 */
	nl->buf = XMALLOC(MTYPE_NL_BUF, nl->buflen);

	/* 返回 0 表示成功；此时 ret 通常为 getsockname 的成功返回值 0 */
	return ret;
}

/*
 * Dispatch an incoming netlink message; used by the zebra main pthread's
 * netlink event reader.
 */
/*
 * netlink_information_fetch：
 *   分发传入的 netlink 消息。该函数由 zebra 主线程的 netlink 事件读取器调用，
 *   通常作为 netlink_parse_info() 的 filter 回调。
 *
 * 参数：
 *   h       当前 netlink 消息头
 *   ns_id   消息所属的网络命名空间 ID
 *   startup 是否处于启动阶段，会传递给具体的处理函数
 *
 * 返回值：
 *   通常返回 0 表示成功；具体处理函数可能返回负值表示错误。
 */
static int netlink_information_fetch(struct nlmsghdr *h, ns_id_t ns_id,
				     int startup)
{
	/*
	 * When we handle new message types here
	 * because we are starting to install them
	 * then lets check the netlink_install_filter
	 * and see if we should add the corresponding
	 * allow through entry there.
	 * Probably not needed to do but please
	 * think about it.
	 */
	/*
	 * 注意：
	 * 如果将来在这里新增处理的消息类型，并且开始安装这些类型，
	 * 那么请检查 netlink_install_filter()，看看是否需要在过滤器中
	 * 添加相应的允许通过条目。可能并不总是需要，但请考虑这一点。
	 */
	switch (h->nlmsg_type) {
	/*
	 * 路由相关消息：
	 *   RTM_NEWROUTE  新增路由
	 *   RTM_DELROUTE  删除路由
	 * 两者都交给 netlink_route_change() 统一处理。
	 */
	case RTM_NEWROUTE:
		return netlink_route_change(h, ns_id, startup);
	case RTM_DELROUTE:
		return netlink_route_change(h, ns_id, startup);

	/*
	 * 策略路由规则相关消息：
	 *   RTM_NEWRULE  新增规则
	 *   RTM_DELRULE  删除规则
	 * 交给 netlink_rule_change() 处理。
	 */
	case RTM_NEWRULE:
		return netlink_rule_change(h, ns_id, startup);
	case RTM_DELRULE:
		return netlink_rule_change(h, ns_id, startup);

	/*
	 * nexthop 对象相关消息：
	 *   RTM_NEWNEXTHOP  新增 nexthop
	 *   RTM_DELNEXTHOP  删除 nexthop
	 * 交给 netlink_nexthop_change() 处理。
	 */
	case RTM_NEWNEXTHOP:
		return netlink_nexthop_change(h, ns_id, startup);
	case RTM_DELNEXTHOP:
		return netlink_nexthop_change(h, ns_id, startup);

	/*
	 * 流量控制队列规程（qdisc）相关消息：
	 *   RTM_NEWQDISC  新增 qdisc
	 *   RTM_DELQDISC  删除 qdisc
	 * 交给 netlink_qdisc_change() 处理。
	 */
	case RTM_NEWQDISC:
	case RTM_DELQDISC:
		return netlink_qdisc_change(h, ns_id, startup);

	/*
	 * 流量控制类（tclass）相关消息：
	 *   RTM_NEWTCLASS  新增 tclass
	 *   RTM_DELTCLASS  删除 tclass
	 * 交给 netlink_tclass_change() 处理。
	 */
	case RTM_NEWTCLASS:
	case RTM_DELTCLASS:
		return netlink_tclass_change(h, ns_id, startup);

	/*
	 * 流量控制过滤器（tfilter）相关消息：
	 *   RTM_NEWTFILTER  新增 tfilter
	 *   RTM_DELTFILTER  删除 tfilter
	 * 交给 netlink_tfilter_change() 处理。
	 */
	case RTM_NEWTFILTER:
	case RTM_DELTFILTER:
		return netlink_tfilter_change(h, ns_id, startup);

	/* Messages we may receive, but ignore */
	/*
	 * 以下消息可能会收到，但当前选择忽略，直接返回 0。
	 * 这些是链（chain）相关消息：
	 *   RTM_NEWCHAIN  新增链
	 *   RTM_DELCHAIN  删除链
	 *   RTM_GETCHAIN  获取链
	 */
	case RTM_NEWCHAIN:
	case RTM_DELCHAIN:
	case RTM_GETCHAIN:
		return 0;

	/* Messages handled in the dplane thread */
	/*
	 * 以下消息由数据平面（dplane）线程处理，不在主线程这里处理。
	 * 这里直接返回 0，表示已收到但无需进一步操作。
	 *
	 * 包括：
	 *   链路：RTM_NEWLINK、RTM_DELLINK
	 *   地址：RTM_NEWADDR、RTM_DELADDR
	 *   netconf：RTM_NEWNETCONF、RTM_DELNETCONF
	 *   隧道：RTM_NEWTUNNEL、RTM_DELTUNNEL、RTM_GETTUNNEL
	 *   VLAN：RTM_NEWVLAN、RTM_DELVLAN
	 *   邻居：RTM_NEWNEIGH、RTM_DELNEIGH、RTM_GETNEIGH
	 */
	case RTM_NEWLINK:
	case RTM_DELLINK:
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_NEWNETCONF:
	case RTM_DELNETCONF:
	case RTM_NEWTUNNEL:
	case RTM_DELTUNNEL:
	case RTM_GETTUNNEL:
	case RTM_NEWVLAN:
	case RTM_DELVLAN:
	case RTM_NEWNEIGH:
	case RTM_DELNEIGH:
	case RTM_GETNEIGH:
		return 0;
	default:
		/*
		 * If we have received this message then
		 * we have made a mistake during development
		 * and we need to write some code to handle
		 * this message type or not ask for
		 * it to be sent up to us
		 */
		/*
		 * 默认分支：
		 * 如果收到了未在此处处理的消息类型，说明开发过程中可能遗漏了
		 * 对该类型的处理，或者不应该向本 socket 订阅该消息。
		 * 这里记录错误日志，然后返回 0。
		 */
		flog_err(EC_ZEBRA_UNKNOWN_NLMSG,
			 "Unknown netlink nlmsg_type %s(%d) vrf %u",
			 nl_msg_type_to_str(h->nlmsg_type), h->nlmsg_type,
			 ns_id);
		break;
	}
	return 0;
}

/*
 * Dispatch an incoming netlink message; used by the dataplane pthread's
 * netlink event reader code.
 */
/*
 * dplane_netlink_information_fetch：
 *   分发传入的 netlink 消息。该函数由数据平面 pthread 的 netlink 事件读取器调用，
 *   通常作为 netlink_parse_info() 的 filter 回调（见 kernel_dplane_read()）。
 *
 *   它只处理数据平面线程负责的消息类型，例如地址、netconf、链路、VLAN、邻居等。
 *   主线程中的 netlink_information_fetch() 处理的是另一组消息（如路由、规则等）。
 *
 * 参数：
 *   h       当前 netlink 消息头
 *   ns_id   消息所属的网络命名空间 ID
 *   startup 是否处于启动阶段，会传递给具体的处理函数
 *
 * 返回值：
 *   通常返回 0 表示成功；具体处理函数可能返回负值表示错误。
 */
static int dplane_netlink_information_fetch(struct nlmsghdr *h, ns_id_t ns_id,
					    int startup)
{
	/*
	 * Dispatch the incoming messages that the dplane pthread handles
	 */
	/*
	 * 根据 netlink 消息类型进行分发。
	 * 这里只列出数据平面线程需要处理的消息类型。
	 */
	switch (h->nlmsg_type) {
	/*
	 * 接口地址相关消息：
	 *   RTM_NEWADDR  新增接口地址
	 *   RTM_DELADDR  删除接口地址
	 * 交给 netlink_interface_addr_dplane() 处理，
	 * 该函数专门用于数据平面线程中的地址变化处理。
	 */
	case RTM_NEWADDR:
	case RTM_DELADDR:
		return netlink_interface_addr_dplane(h, ns_id, startup);

	/*
	 * netconf 相关消息：
	 *   RTM_NEWNETCONF  新增 netconf 配置
	 *   RTM_DELNETCONF  删除 netconf 配置
	 * 交给 netlink_netconf_change() 处理。
	 */
	case RTM_NEWNETCONF:
	case RTM_DELNETCONF:
		return netlink_netconf_change(h, ns_id, startup);

	/* TODO -- other messages for the dplane socket and pthread */
	/*
	 * TODO：这里还可以添加其他由数据平面 socket 和 pthread 处理的消息类型。
	 */

	/*
	 * 链路相关消息：
	 *   RTM_NEWLINK  新增链路
	 *   RTM_DELLINK  删除链路
	 * 交给 netlink_link_change() 处理。
	 */
	case RTM_NEWLINK:
	case RTM_DELLINK:
		return netlink_link_change(h, ns_id, startup);

	/*
	 * VLAN 相关消息：
	 *   RTM_NEWVLAN  新增 VLAN
	 *   RTM_DELVLAN  删除 VLAN
	 * 交给 netlink_vlan_change() 处理。
	 */
	case RTM_NEWVLAN:
	case RTM_DELVLAN:
		return netlink_vlan_change(h, ns_id, startup);

	/*
	 * 邻居相关消息：
	 *   RTM_NEWNEIGH  新增邻居
	 *   RTM_DELNEIGH  删除邻居
	 *   RTM_GETNEIGH  获取邻居（通常用于 dump 请求的响应）
	 * 交给 netlink_neigh_change() 处理。
	 * 注意：此函数没有 startup 参数。
	 */
	case RTM_NEWNEIGH:
	case RTM_DELNEIGH:
	case RTM_GETNEIGH:
		return netlink_neigh_change(h, ns_id);
	default:
		/*
		 * 其他未在此处列出的消息类型，数据平面线程不处理，
		 * 直接跳出 switch，最终返回 0。
		 */
		break;
	}

	/* 对于未处理的消息类型，返回 0 表示成功忽略 */
	return 0;
}

/*
 * kernel_read：
 *   netlink 监听 socket 的可读事件回调函数。
 *
 *   当 zns->netlink.sock 上有数据可读时，FRR 事件循环会调用此函数。
 *   它主要完成：
 *     1. 从事件对象中取出关联的 zebra_ns 实例
 *     2. 构造数据平面信息 dp_info
 *     3. 调用 netlink_parse_info 读取并分发 netlink 消息
 *     4. 重新注册读事件，保证下一次 socket 可读时继续回调
 */
static void kernel_read(struct event *event)
{
	/*
	 * 从事件对象中取出注册时传入的参数。
	 * 在 kernel_init() 中注册读事件时，传入的 arg 是 zns，
	 * 因此这里通过 EVENT_ARG(event) 还原为 struct zebra_ns *。
	 */
	struct zebra_ns *zns = (struct zebra_ns *)EVENT_ARG(event);

	/*
	 * 数据平面信息结构。
	 * 用于在解析 netlink 消息时携带命名空间、socket 等上下文，
	 * 并传递给后续的消息处理回调。
	 */
	struct zebra_dplane_info dp_info;

	/* Capture key info from ns struct */
	/*
	 * 从 zns 中提取关键信息填充 dp_info。
	 * 第三个参数 false 通常表示不需要以“内核 socket”模式填充，
	 * 或者不设置某些仅内核使用的标志；具体语义由该函数实现决定。
	 */
	zebra_dplane_info_from_zns(&dp_info, zns, false);

	/*
	 * 解析 netlink 消息。
	 *
	 * 参数说明：
	 *   netlink_information_fetch
	 *       处理每条 netlink 消息的回调函数，负责根据消息类型
	 *       分发到路由、链路、地址、邻居等具体处理逻辑。
	 *
	 *   &zns->netlink
	 *       监听的 netlink socket 结构，包含 socket fd、接收缓冲区等。
	 *
	 *   &dp_info
	 *       数据平面信息，会传递给消息处理回调，用于标识命名空间等。
	 *
	 *   5
	 *       最多处理 5 条 netlink 消息，避免单次事件回调占用过长时间。
	 *
	 *   false
	 *       非阻塞/不等待模式；具体由 netlink_parse_info() 解释。
	 *       注意 socket 本身已在 kernel_init() 中设置为 O_NONBLOCK。
	 */
	netlink_parse_info(netlink_information_fetch, &zns->netlink, &dp_info,
			   5, false);

	/*
	 * 重新注册读事件。
	 *
	 * FRR 的 event_add_read() 注册的事件在触发后通常会被移除，
	 * 因此需要再次注册，保证下一次 socket 可读时继续调用 kernel_read。
	 *
	 * 参数：
	 *   zrouter.master       主事件循环
	 *   kernel_read          可读回调函数
	 *   zns                  回调参数
	 *   zns->netlink.sock    要监听的 netlink socket fd
	 *   &zns->t_netlink      保存事件句柄，便于后续管理
	 */
	event_add_read(zrouter.master, kernel_read, zns, zns->netlink.sock,
		       &zns->t_netlink);
}

/*
 * Called by the dplane pthread to read incoming OS messages and dispatch them.
 */
/*
 * kernel_dplane_read：
 *   由数据平面 pthread 调用，从 netlink socket 读取 OS 发来的消息并进行分发。
 *
 *   工作流程：
 *     1. 根据传入的 zebra_dplane_info 中的 socket fd，
 *        在全局 netlink socket 表中查找对应的 struct nlsock。
 *     2. 调用 netlink_parse_info()，使用数据平面专用的消息处理回调
 *        dplane_netlink_information_fetch 来解析并分发消息。
 *     3. 最多读取 5 批消息，startup 为 false，表示非启动阶段。
 *
 * 参数：
 *   info  zebra 数据平面信息，包含命名空间 ID、netlink socket fd 等
 *
 * 返回值：
 *   总是返回 0。
 */
int kernel_dplane_read(struct zebra_dplane_info *info)
{
	/*
	 * 根据 info->sock（netlink socket 文件描述符）查找对应的 nlsock 结构。
	 * 该结构保存了 socket 的接收缓冲区、名称、地址等信息，
	 * 是 netlink_parse_info() 解析消息所必需的。
	 *
	 * 注意：此处未检查 nl 是否为 NULL，通常意味着调用方保证该 socket
	 * 已经通过 kernel_netlink_nlsock_insert() 注册到全局表中。
	 */
	struct nlsock *nl = kernel_netlink_nlsock_lookup(info->sock);

	/*
	 * 解析并分发 netlink 消息。
	 *
	 * 参数说明：
	 *   dplane_netlink_information_fetch
	 *       数据平面专用的消息处理回调，负责根据消息类型
	 *       分发到 dplane 相关的处理逻辑。
	 *
	 *   nl
	 *       从全局表中查找到的 netlink socket 结构，包含接收缓冲区等。
	 *
	 *   info
	 *       zebra 数据平面信息，会传递给回调，用于标识命名空间等。
	 *
	 *   5
	 *       本次最多读取 5 批消息，避免单次调用占用过长时间。
	 *
	 *   false
	 *       非启动阶段，该标志会传递给回调函数。
	 */
	netlink_parse_info(dplane_netlink_information_fetch, nl, info, 5,
			   false);

	/* 读取与分发完成，返回 0 */
	return 0;
}

/*
 * Filter out messages from self that occur on listener socket,
 * caused by our actions on the command socket(s)
 *
 * When we add new Netlink message types we probably
 * do not need to add them here as that we are filtering
 * on the routes we actually care to receive( which is rarer
 * then the normal course of operations).  We are intentionally
 * allowing some messages from ourselves through
 * ( I'm looking at you Interface based netlink messages )
 * so that we only have to write one way to handle incoming
 * address add/delete and xxxNETCONF changes.
 */
static void netlink_install_filter(int sock, uint32_t pid, uint32_t dplane_pid)
{
	/*
	 * BPF_JUMP instructions and where you jump to are based upon
	 * 0 as being the next statement.  So count from 0.  Writing
	 * this down because every time I look at this I have to
	 * re-remember it.
	 */
	struct sock_filter filter[] = {
		/*
		 * Logic:
		 *   if (nlmsg_pid == pid ||
		 *       nlmsg_pid == dplane_pid) {
		 *       if (the incoming nlmsg_type ==
		 *           RTM_NEWADDR || RTM_DELADDR || RTM_NEWNETCONF ||
		 *           RTM_DELNETCONF)
		 *           keep this message
		 *       else
		 *           skip this message
		 *   } else
		 *       keep this netlink message
		 */
		/*
		 * 0: Load the nlmsg_pid into the BPF register
		 */
		BPF_STMT(BPF_LD | BPF_ABS | BPF_W,
			 offsetof(struct nlmsghdr, nlmsg_pid)),
		/*
		 * 1: Compare to pid
		 */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htonl(pid), 1, 0),
		/*
		 * 2: Compare to dplane pid
		 */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htonl(dplane_pid), 0, 6),
		/*
		 * 3: Load the nlmsg_type into BPF register
		 */
		BPF_STMT(BPF_LD | BPF_ABS | BPF_H,
			 offsetof(struct nlmsghdr, nlmsg_type)),
		/*
		 * 4: Compare to RTM_NEWADDR
		 */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(RTM_NEWADDR), 4, 0),
		/*
		 * 5: Compare to RTM_DELADDR
		 */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(RTM_DELADDR), 3, 0),
		/*
		 * 6: Compare to RTM_NEWNETCONF
		 */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(RTM_NEWNETCONF), 2,
			 0),
		/*
		 * 7: Compare to RTM_DELNETCONF
		 */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(RTM_DELNETCONF), 1,
			 0),
		/*
		 * 8: This is the end state of we want to skip the
		 *    message
		 */
		BPF_STMT(BPF_RET | BPF_K, 0),
		/* 9: This is the end state of we want to keep
		 *     the message
		 */
		BPF_STMT(BPF_RET | BPF_K, 0xffff),
	};

	struct sock_fprog prog = {
		.len = array_size(filter), .filter = filter,
	};

	if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog))
	    < 0)
		flog_err_sys(EC_LIB_SOCKET, "Can't install socket filter: %s",
			     safe_strerror(errno));
}

/*
 * Please note, the assumption with this function is that the
 * flags passed in that are bit masked with type, we are implicitly
 * assuming that this is handling the NLA_F_NESTED ilk.
 */
void netlink_parse_rtattr_flags(struct rtattr **tb, int max, struct rtattr *rta,
				int len, unsigned short flags)
{
	unsigned short type;

	memset(tb, 0, sizeof(struct rtattr *) * (max + 1));
	while (RTA_OK(rta, len)) {
		type = rta->rta_type & ~flags;
		if ((type <= max) && (!tb[type]))
			tb[type] = rta;
		rta = RTA_NEXT(rta, len);
	}
}

const char *nl_msg_type_to_str(uint16_t msg_type)
{
	return lookup_msg(nlmsg_str, msg_type, "");
}

const char *nl_rtproto_to_str(uint8_t rtproto)
{
	return lookup_msg(rtproto_str, rtproto, "");
}

const char *nl_family_to_str(uint8_t family)
{
	return lookup_msg(family_str, family, "");
}

const char *nl_rttype_to_str(uint8_t rttype)
{
	return lookup_msg(rttype_str, rttype, "");
}

static void netlink_parse_extended_ack(struct nlmsghdr *h)
{
	struct nlattr *tb[NLMSGERR_ATTR_MAX + 1] = {};
	const struct nlmsgerr *err = (const struct nlmsgerr *)NLMSG_DATA(h);
	const struct nlmsghdr *err_nlh = NULL;
	/* Length not including nlmsghdr */
	uint32_t len = 0;
	/* Inner error netlink message length */
	uint32_t inner_len = 0;
	const char *msg = NULL;
	uint32_t off = 0;

	if (!(h->nlmsg_flags & NLM_F_CAPPED))
		inner_len = (uint32_t)NLMSG_PAYLOAD(&err->msg, 0);

	len = (uint32_t)(NLMSG_PAYLOAD(h, sizeof(struct nlmsgerr)) - inner_len);

	netlink_parse_nlattr(tb, NLMSGERR_ATTR_MAX, ERR_NLA(err, inner_len),
			     len);

	if (tb[NLMSGERR_ATTR_MSG])
		msg = (const char *)NLA_DATA(tb[NLMSGERR_ATTR_MSG]);

	if (tb[NLMSGERR_ATTR_OFFS]) {
		off = *(uint32_t *)NLA_DATA(tb[NLMSGERR_ATTR_OFFS]);

		if (off > h->nlmsg_len) {
			flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
				 "Invalid offset for NLMSGERR_ATTR_OFFS");
		} else if (!(h->nlmsg_flags & NLM_F_CAPPED)) {
			/*
			 * Header of failed message
			 * we are not doing anything currently with it
			 * but noticing it for later.
			 */
			err_nlh = &err->msg;
			zlog_debug("%s: Received %s extended Ack", __func__,
				   nl_msg_type_to_str(err_nlh->nlmsg_type));
		}
	}

	if (msg && *msg != '\0') {
		bool is_err = !!err->error;

		if (is_err)
			flog_err(EC_ZEBRA_NETLINK_EXTENDED_ERROR, "Extended Error: %s", msg);
		else
			flog_warn(EC_ZEBRA_NETLINK_EXTENDED_WARNING,
				  "Extended Warning: %s", msg);
	}
}

/*
 * netlink_send_msg - send a netlink message of a certain size.
 *
 * Returns -1 on error. Otherwise, it returns the number of bytes sent.
 */
static ssize_t netlink_send_msg(const struct nlsock *nl, void *buf,
				size_t buflen)
{
	struct sockaddr_nl snl = {};
	struct iovec iov = {};
	struct msghdr msg = {};
	ssize_t status;
	int save_errno = 0;

	iov.iov_base = buf;
	iov.iov_len = buflen;
	msg.msg_name = &snl;
	msg.msg_namelen = sizeof(snl);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	snl.nl_family = AF_NETLINK;

	/* Send message to netlink interface. */
	frr_with_privs(&zserv_privs) {
		status = sendmsg(nl->sock, &msg, 0);
		save_errno = errno;
	}

	if (IS_ZEBRA_DEBUG_KERNEL_MSGDUMP_SEND) {
		zlog_debug("%s: >> netlink message dump [sent]", __func__);
		frrtrace(2, frr_zebra, netlink_send_msg, nl, msg);
#ifdef NETLINK_DEBUG
		nl_dump(buf, buflen);
#else
		zlog_hexdump(buf, buflen);
#endif /* NETLINK_DEBUG */
	}

	if (status == -1) {
		flog_err_sys(EC_LIB_SOCKET, "%s error: %s", __func__,
			     safe_strerror(save_errno));
		return -1;
	}

	return status;
}

/*
 * netlink_recv_msg - receive a netlink message.
 *
 * Returns -1 on error, 0 if read would block or the number of bytes received.
 */
static int netlink_recv_msg(struct nlsock *nl, struct msghdr *msg)
{
	struct iovec iov;
	int status;

	iov.iov_base = nl->buf;
	iov.iov_len = nl->buflen;
	msg->msg_iov = &iov;
	msg->msg_iovlen = 1;

	do {
		int bytes;

		bytes = recv(nl->sock, NULL, 0, MSG_PEEK | MSG_TRUNC);

		if (bytes >= 0 && (size_t)bytes > nl->buflen) {
			nl->buf = XREALLOC(MTYPE_NL_BUF, nl->buf, bytes);
			nl->buflen = bytes;
			iov.iov_base = nl->buf;
			iov.iov_len = nl->buflen;
		}

		status = recvmsg(nl->sock, msg, 0);
	} while (status == -1 && errno == EINTR);

	if (status == -1) {
		if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EMSGSIZE)
			return 0;
		flog_err(EC_ZEBRA_RECVMSG_OVERRUN, "%s recvmsg overrun: %s",
			 nl->name, safe_strerror(errno));
		/*
		 * In this case we are screwed. There is no good way to recover
		 * zebra at this point.
		 */
		frr_exit_with_buffer_flush(-1);
	}

	if (status == 0) {
		flog_err_sys(EC_LIB_SOCKET, "%s EOF", nl->name);
		return -1;
	}

	if (msg->msg_namelen != sizeof(struct sockaddr_nl)) {
		flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
			 "%s sender address length error: length %d", nl->name,
			 msg->msg_namelen);
		return -1;
	}

	if (IS_ZEBRA_DEBUG_KERNEL_MSGDUMP_RECV) {
		zlog_debug("%s: << netlink message dump [recv]", __func__);
#ifdef NETLINK_DEBUG
		nl_dump(nl->buf, status);
#else
		zlog_hexdump(nl->buf, status);
#endif /* NETLINK_DEBUG */
	}

	return status;
}

/*
 * netlink_parse_error - parse a netlink error message
 *
 * Returns 1 if this message is acknowledgement, 0 if this error should be
 * ignored, -1 otherwise.
 */
static int netlink_parse_error(const struct nlsock *nl, struct nlmsghdr *h,
			       bool is_cmd, bool startup)
{
	struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(h);
	int errnum = err->error;
	int msg_type = err->msg.nlmsg_type;

	if (h->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
		flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
			 "%s error: message truncated", nl->name);
		return -1;
	}

	/*
	 * Parse the extended information before we actually handle it. At this
	 * point in time we do not do anything other than report the issue.
	 */
	if (h->nlmsg_flags & NLM_F_ACK_TLVS)
		netlink_parse_extended_ack(h);

	/* If the error field is zero, then this is an ACK. */
	if (err->error == 0) {
		if (IS_ZEBRA_DEBUG_KERNEL) {
			zlog_debug("%s: %s ACK: type=%s(%u), seq=%u, pid=%u",
				   __func__, nl->name,
				   nl_msg_type_to_str(err->msg.nlmsg_type),
				   err->msg.nlmsg_type, err->msg.nlmsg_seq,
				   err->msg.nlmsg_pid);
		}

		return 1;
	}

	/*
	 * Deal with errors that occur because of races in link handling
	 * or types are not supported in kernel.
	 */
	if (is_cmd &&
	    ((msg_type == RTM_DELROUTE &&
	      (-errnum == ENODEV || -errnum == ESRCH)) ||
	     (msg_type == RTM_NEWROUTE &&
	      (-errnum == ENETDOWN || -errnum == EEXIST)) ||
	     ((msg_type == RTM_NEWTUNNEL || msg_type == RTM_DELTUNNEL ||
	       msg_type == RTM_GETTUNNEL) &&
	      (-errnum == EOPNOTSUPP)))) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: error: %s type=%s(%u), seq=%u, pid=%u",
				   nl->name, safe_strerror(-errnum),
				   nl_msg_type_to_str(msg_type), msg_type,
				   err->msg.nlmsg_seq, err->msg.nlmsg_pid);
		return 0;
	}

	/*
	 * We see RTM_DELNEIGH when shutting down an interface with an IPv4
	 * link-local.  The kernel should have already deleted the neighbor so
	 * do not log these as an error.
	 */
	if (msg_type == RTM_DELNEIGH
	    || (is_cmd && msg_type == RTM_NEWROUTE
		&& (-errnum == ESRCH || -errnum == ENETUNREACH))) {
		/*
		 * This is known to happen in some situations, don't log as
		 * error.
		 */
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s error: %s, type=%s(%u), seq=%u, pid=%u",
				   nl->name, safe_strerror(-errnum),
				   nl_msg_type_to_str(msg_type), msg_type,
				   err->msg.nlmsg_seq, err->msg.nlmsg_pid);
	} else {
		if ((msg_type != RTM_GETNEXTHOP && msg_type != RTM_GETVLAN) ||
		    !startup)
			flog_err(EC_ZEBRA_UNEXPECTED_MESSAGE,
				 "%s error: %s, type=%s(%u), seq=%u, pid=%u",
				 nl->name, safe_strerror(-errnum),
				 nl_msg_type_to_str(msg_type), msg_type,
				 err->msg.nlmsg_seq, err->msg.nlmsg_pid);
	}

	return -1;
}

/*
 * netlink_parse_info：
 *   从 netlink socket 接收消息，并将每条消息交给指定的回调函数处理。
 *
 * 参数：
 *   filter  消息处理回调函数，原型为 int (*)(struct nlmsghdr *, ns_id_t, int)
 *   nl      netlink socket 信息，包含 fd、接收缓冲区等
 *   zns     zebra 数据平面信息，包含命名空间 ID、是否为命令 socket 等
 *   count   本次最多读取多少条消息；0 表示尽可能多地读取
 *   startup 是否处于启动阶段，会传递给 filter 回调
 *
 * 返回值：
 *   成功时返回 ret（通常为 0）；发生错误时返回 -1 或 filter 返回的错误码。
 */
int netlink_parse_info(int (*filter)(struct nlmsghdr *, ns_id_t, int),
		       struct nlsock *nl, const struct zebra_dplane_info *zns,
		       int count, bool startup)
{
	int status;      /* netlink_recv_msg 的返回值，表示接收到的数据长度 */
	int ret = 0;     /* 整体返回值，初始为 0 */
	int error;       /* 调用 filter 后的返回值 */
	int read_in = 0; /* 已经读取的消息批次数，用于和 count 比较 */

	/* 外层循环：每次从 socket 接收一批数据，直到没有数据或达到 count 限制 */
	while (1) {
		struct sockaddr_nl snl; /* 发送方 netlink 地址，用于判断是否来自内核 */
		struct msghdr msg = {.msg_name = (void *)&snl,
				     .msg_namelen = sizeof(snl)}; /* 接收消息头 */
		struct nlmsghdr *h; /* 指向当前正在解析的 netlink 消息头 */

		/* 如果指定了 count，且已读取批次数达到 count，则直接返回成功 */
		if (count && read_in >= count)
			return 0;

		/* 从 netlink socket 接收一条消息（可能包含多个 nlmsghdr） */
		status = netlink_recv_msg(nl, &msg);
		if (status == -1)
			return -1;       /* 接收错误 */
		else if (status == 0)
			break;           /* 没有更多数据可读，跳出循环 */

		read_in++; /* 成功读取一批数据，计数加一 */

		/*
		 * 遍历接收缓冲区中的 nlmsghdr 链表。
		 * NLMSG_OK 检查当前消息是否完整且合法；
		 * NLMSG_NEXT 移动到下一条消息，并更新剩余长度 status。
		 */
		for (h = (struct nlmsghdr *)nl->buf;
		     (status >= 0 && NLMSG_OK(h, (unsigned int)status));
		     h = NLMSG_NEXT(h, status)) {
			/* Finish of reading. */
			/* 遇到 NLMSG_DONE，表示多部分消息结束，直接返回 */
			if (h->nlmsg_type == NLMSG_DONE)
				return ret;

			/* Error handling. */
			/* 遇到 NLMSG_ERROR，表示内核返回了错误或 ACK */
			if (h->nlmsg_type == NLMSG_ERROR) {
				int err = netlink_parse_error(
					nl, h, zns->is_cmd, startup);

				/*
				 * err == 1 通常表示这是一个 ACK，不是真正的错误。
				 * 如果该消息不是多部分消息的一部分，则直接返回 0；
				 * 否则继续处理后续消息。
				 */
				if (err == 1) {
					if (!(h->nlmsg_flags & NLM_F_MULTI))
						return 0;
					continue;
				} else
					return err; /* 真正的错误，直接返回错误码 */
			}

			/*
			 * What is the right thing to do?  The kernel
			 * is telling us that the dump request was interrupted
			 * and we more than likely are out of luck and have
			 * missed data from the kernel.  At this point in time
			 * lets just note that this is happening.
			 */
			/*
			 * 如果内核标记了 NLM_F_DUMP_INTR，表示 dump 请求被中断，
			 * 我们可能已经丢失了部分内核数据。这里只记录错误日志。
			 */
			if (h->nlmsg_flags & NLM_F_DUMP_INTR)
				flog_err(
					EC_ZEBRA_NETLINK_BAD_SEQUENCE,
					"netlink recvmsg: The Dump request was interrupted");

			/* OK we got netlink message. */
			/* 如果开启了内核调试，打印当前消息的详细信息 */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"%s: %s type %s(%u), len=%d, seq=%u, pid=%u",
					__func__, nl->name,
					nl_msg_type_to_str(h->nlmsg_type),
					h->nlmsg_type, h->nlmsg_len,
					h->nlmsg_seq, h->nlmsg_pid);

			/*
			 * Ignore messages that maybe sent from
			 * other actors besides the kernel
			 */
			/*
			 * 只处理来自内核的消息。snl.nl_pid == 0 表示内核。
			 * 如果发送方 PID 不是 0，则忽略该消息。
			 */
			if (snl.nl_pid != 0) {
				zlog_debug("Ignoring message from pid %u",
					   snl.nl_pid);
				continue;
			}

			/*
			 * 调用上层传入的 filter 回调函数处理这条消息。
			 * 参数：消息头 h、命名空间 ID、是否启动阶段。
			 * 如果 filter 返回负值，记录调试信息，并保存错误码到 ret。
			 */
			error = (*filter)(h, zns->ns_id, startup);
			if (error < 0) {
				zlog_debug("%s filter function error",
					   nl->name);
				ret = error;
			}
		}

		/* After error care. */
		/*
		 * 检查接收消息时是否发生了截断。
		 * MSG_TRUNC 表示消息太长，缓冲区不够，数据被截断。
		 * 记录错误并继续下一次接收。
		 */
		if (msg.msg_flags & MSG_TRUNC) {
			flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
				 "%s error: message truncated", nl->name);
			continue;
		}

		/*
		 * 如果遍历完消息后 status 仍然非零，说明缓冲区中还有未处理的数据，
		 * 这通常意味着消息格式异常或长度错误。
		 * 记录错误并返回 -1。
		 */
		if (status) {
			flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
				 "%s error: data remnant size %d", nl->name,
				 status);
			return -1;
		}
	}

	/* 正常退出循环（没有更多数据），返回累计的错误码 ret（通常为 0） */
	return ret;
}

/*
 * netlink_talk_info
 *
 * sendmsg() to netlink socket then recvmsg().
 * Calls netlink_parse_info to parse returned data
 *
 * filter   -> The filter to read final results from kernel
 * nlmsghdr -> The data to send to the kernel
 * dp_info -> The dataplane and netlink socket information
 * startup  -> Are we reading in under startup conditions
 *             This is passed through eventually to filter.
 */
static int netlink_talk_info(int (*filter)(struct nlmsghdr *, ns_id_t,
					   int startup),
			     struct nlmsghdr *n,
			     struct zebra_dplane_info *dp_info, bool startup)
{
	struct nlsock *nl;

	nl = kernel_netlink_nlsock_lookup(dp_info->sock);
	n->nlmsg_seq = dp_info->seq;
	n->nlmsg_pid = nl->snl.nl_pid;

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug(
			"netlink_talk: %s type %s(%u), len=%d seq=%u flags 0x%x",
			nl->name, nl_msg_type_to_str(n->nlmsg_type),
			n->nlmsg_type, n->nlmsg_len, n->nlmsg_seq,
			n->nlmsg_flags);

	if (netlink_send_msg(nl, n, n->nlmsg_len) == -1)
		return -1;

	/*
	 * Get reply from netlink socket.
	 * The reply should either be an acknowledgement or an error.
	 */
	return netlink_parse_info(filter, nl, dp_info, 0, startup);
}

/*
 * Synchronous version of netlink_talk_info. Converts args to suit the
 * common version, which is suitable for both sync and async use.
 */
int netlink_talk(int (*filter)(struct nlmsghdr *, ns_id_t, int startup),
		 struct nlmsghdr *n, struct nlsock *nl, struct zebra_ns *zns,
		 bool startup)
{
	struct zebra_dplane_info dp_info;

	/* Increment sequence number before capturing snapshot of ns socket
	 * info.
	 */
	nl->seq++;

	/* Capture info in intermediate info struct */
	zebra_dplane_info_from_zns(&dp_info, zns, (nl == &(zns->netlink_cmd)));

	return netlink_talk_info(filter, n, &dp_info, startup);
}

/*
 * Synchronous version of netlink_talk_info. Converts args to suit the
 * common version, which is suitable for both sync and async use.
 */
int ge_netlink_talk(int (*filter)(struct nlmsghdr *, ns_id_t, int startup),
		    struct nlmsghdr *n, struct zebra_ns *zns, bool startup)
{
	struct zebra_dplane_info dp_info;

	if (zns->ge_netlink_cmd.sock < 0)
		return -1;

	/* Increment sequence number before capturing snapshot of ns socket
	 * info.
	 */
	zns->ge_netlink_cmd.seq = zebra_router_get_next_sequence();

	/* Capture info in intermediate info struct */
	dp_info.ns_id = zns->ns_id;

	dp_info.is_cmd = true;
	dp_info.sock = zns->ge_netlink_cmd.sock;
	dp_info.seq = zns->ge_netlink_cmd.seq;

	return netlink_talk_info(filter, n, &dp_info, startup);
}

/* Issue request message to kernel via netlink socket. GET messages
 * are issued through this interface.
 */
int netlink_request(struct nlsock *nl, void *req)
{
	struct nlmsghdr *n = (struct nlmsghdr *)req;

	/* Check netlink socket. */
	if (nl->sock < 0) {
		flog_err_sys(EC_LIB_SOCKET, "%s socket isn't active.",
			     nl->name);
		return -1;
	}

	/* Fill common fields for all requests. */
	n->nlmsg_pid = nl->snl.nl_pid;
	n->nlmsg_seq = ++nl->seq;

	if (netlink_send_msg(nl, req, n->nlmsg_len) == -1)
		return -1;

	return 0;
}

static int nl_batch_read_resp(struct nl_batch *bth, struct nlsock *nl)
{
	struct nlmsghdr *h;
	struct sockaddr_nl snl;
	struct msghdr msg = {};
	int status, seq;
	struct zebra_dplane_ctx *ctx;
	bool ignore_msg;

	msg.msg_name = (void *)&snl;
	msg.msg_namelen = sizeof(snl);

	/*
	 * The responses are not batched, so we need to read and process one
	 * message at a time.
	 */
	while (true) {
		status = netlink_recv_msg(nl, &msg);
		/*
		 * status == -1 is a full on failure somewhere
		 * since we don't know where the problem happened
		 * we must mark all as failed
		 *
		 * Else we mark everything as worked
		 *
		 */
		if (status == -1 || status == 0) {
			while ((ctx = dplane_ctx_dequeue(&(bth->ctx_list))) !=
			       NULL) {
				if (status == -1)
					dplane_ctx_set_status(
						ctx,
						ZEBRA_DPLANE_REQUEST_FAILURE);
				dplane_ctx_enqueue_tail(bth->ctx_out_q, ctx);
			}
			return status;
		}

		h = (struct nlmsghdr *)nl->buf;
		ignore_msg = false;
		seq = h->nlmsg_seq;
		/*
		 * Find the corresponding context object. Received responses are
		 * in the same order as requests we sent, so we can simply
		 * iterate over the context list and match responses with
		 * requests at same time.
		 */
		while (true) {
			ctx = dplane_ctx_get_head(&(bth->ctx_list));
			if (ctx == NULL) {
				/*
				 * This is a situation where we have gotten
				 * into a bad spot.  We need to know that
				 * this happens( does it? )
				 */
				flog_err(EC_ZEBRA_NETLINK_NO_CONTEXT,
					 "%s:WARNING Received netlink Response for an error and no Contexts to associate with it",
					 __func__);
				break;
			}

			/*
			 * 'update' context objects take two consecutive
			 * sequence numbers.
			 */
			if (dplane_ctx_is_update(ctx) &&
			    dplane_ctx_get_ns(ctx)->seq + 1 == seq) {
				/*
				 * This is the situation where we get a response
				 * to a message that should be ignored.
				 */
				ignore_msg = true;
				break;
			}

			ctx = dplane_ctx_dequeue(&(bth->ctx_list));
			dplane_ctx_enqueue_tail(bth->ctx_out_q, ctx);

			/* We have found corresponding context object. */
			if (dplane_ctx_get_ns(ctx)->seq == seq)
				break;

			if (dplane_ctx_get_ns(ctx)->seq > seq)
				zlog_warn(
					"%s:WARNING Received %u is less than any context on the queue ctx->seq %u",
					__func__, seq,
					dplane_ctx_get_ns(ctx)->seq);
		}

		if (ignore_msg) {
			/*
			 * If we ignore the message due to an update
			 * above we should still fricking decode the
			 * message for our operator to understand
			 * what is going on
			 */
			int err = netlink_parse_error(nl, h, bth->zns->is_cmd,
						      false);

			zlog_debug("%s: netlink error message seq=%d %d",
				   __func__, h->nlmsg_seq, err);
			continue;
		}

		/*
		 * We received a message with the sequence number that isn't
		 * associated with any dplane context object.
		 */
		if (ctx == NULL) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"%s: skipping unassociated response, seq number %d NS %u",
					__func__, h->nlmsg_seq,
					bth->zns->ns_id);
			continue;
		}

		if (h->nlmsg_type == NLMSG_ERROR) {
			int err = netlink_parse_error(nl, h, bth->zns->is_cmd,
						      false);

			if (err == -1)
				dplane_ctx_set_status(
					ctx, ZEBRA_DPLANE_REQUEST_FAILURE);

			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("%s: netlink error message seq=%d ",
					   __func__, h->nlmsg_seq);
			continue;
		}

		/*
		 * If we get here then we did not receive neither the ack nor
		 * the error and instead received some other message in an
		 * unexpected way.
		 */
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: ignoring message type 0x%04x(%s) NS %u",
				   __func__, h->nlmsg_type,
				   nl_msg_type_to_str(h->nlmsg_type),
				   bth->zns->ns_id);
	}

	return 0;
}

static void nl_batch_reset(struct nl_batch *bth)
{
	bth->buf_head = bth->buf;
	bth->curlen = 0;
	bth->msgcnt = 0;
	bth->zns = NULL;

	dplane_ctx_q_init(&(bth->ctx_list));
}

static void nl_batch_init(struct nl_batch *bth,
			  struct dplane_ctx_list_head *ctx_out_q)
{
	/*
	 * If the size of the buffer has changed, free and then allocate a new
	 * one.
	 */
	size_t bufsize =
		atomic_load_explicit(&nl_batch_bufsize, memory_order_relaxed);
	if (bufsize != nl_batch_tx_bufsize) {
		if (nl_batch_tx_buf)
			XFREE(MTYPE_NL_BUF, nl_batch_tx_buf);

		nl_batch_tx_buf = XCALLOC(MTYPE_NL_BUF, bufsize);
		nl_batch_tx_bufsize = bufsize;
	}

	bth->buf = nl_batch_tx_buf;
	bth->bufsiz = bufsize;
	bth->limit = atomic_load_explicit(&nl_batch_send_threshold,
					  memory_order_relaxed);

	bth->ctx_out_q = ctx_out_q;

	nl_batch_reset(bth);
}

static void nl_batch_send(struct nl_batch *bth)
{
	struct zebra_dplane_ctx *ctx;
	bool err = false;

	if (bth->curlen != 0 && bth->zns != NULL) {
		struct nlsock *nl =
			kernel_netlink_nlsock_lookup(bth->zns->sock);

		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: %s, batch size=%zu, msg cnt=%zu",
				   __func__, nl->name, bth->curlen,
				   bth->msgcnt);

		if (netlink_send_msg(nl, bth->buf, bth->curlen) == -1)
			err = true;

		if (!err) {
			if (nl_batch_read_resp(bth, nl) == -1)
				err = true;
		}
	}

	/* Move remaining contexts to the outbound queue. */
	while (true) {
		ctx = dplane_ctx_dequeue(&(bth->ctx_list));
		if (ctx == NULL)
			break;

		if (err)
			dplane_ctx_set_status(ctx,
					      ZEBRA_DPLANE_REQUEST_FAILURE);

		dplane_ctx_enqueue_tail(bth->ctx_out_q, ctx);
	}

	nl_batch_reset(bth);
}

enum netlink_msg_status netlink_batch_add_msg(
	struct nl_batch *bth, struct zebra_dplane_ctx *ctx,
	ssize_t (*msg_encoder)(struct zebra_dplane_ctx *, void *, size_t),
	bool ignore_res)
{
	int seq;
	ssize_t size;
	struct nlmsghdr *msgh;
	struct nlsock *nl = kernel_netlink_nlsock_lookup(dplane_ctx_get_ns_sock(ctx));

	if (!nl || nl->sock < 0 || !nl->buf)
		return FRR_NETLINK_ERROR;

	size = (*msg_encoder)(ctx, bth->buf_head, bth->bufsiz - bth->curlen);

	/*
	 * If there was an error while encoding the message (other than buffer
	 * overflow) then return an error.
	 */
	if (size < 0)
		return FRR_NETLINK_ERROR;

	/*
	 * If the message doesn't fit entirely in the buffer then send the batch
	 * and retry.
	 */
	if (size == 0) {
		nl_batch_send(bth);
		size = (*msg_encoder)(ctx, bth->buf_head,
				      bth->bufsiz - bth->curlen);
		/*
		 * If the message doesn't fit in the empty buffer then just
		 * return an error.
		 */
		if (size <= 0)
			return FRR_NETLINK_ERROR;
	}

	seq = dplane_ctx_get_ns(ctx)->seq;

	if (ignore_res)
		seq++;

	msgh = (struct nlmsghdr *)bth->buf_head;
	msgh->nlmsg_seq = seq;
	msgh->nlmsg_pid = nl->snl.nl_pid;

	bth->zns = dplane_ctx_get_ns(ctx);
	bth->buf_head = ((char *)bth->buf_head) + size;
	bth->curlen += size;
	bth->msgcnt++;

	return FRR_NETLINK_QUEUED;
}

static enum netlink_msg_status nl_put_msg(struct nl_batch *bth,
					  struct zebra_dplane_ctx *ctx)
{
	switch (dplane_ctx_get_op(ctx)) {

	case DPLANE_OP_ROUTE_INSTALL:
	case DPLANE_OP_ROUTE_UPDATE:
	case DPLANE_OP_ROUTE_DELETE:
		return netlink_put_route_update_msg(bth, ctx);

	case DPLANE_OP_NH_INSTALL:
	case DPLANE_OP_NH_UPDATE:
	case DPLANE_OP_NH_DELETE:
		return netlink_put_nexthop_update_msg(bth, ctx);

	case DPLANE_OP_LSP_INSTALL:
	case DPLANE_OP_LSP_UPDATE:
	case DPLANE_OP_LSP_DELETE:
		return netlink_put_lsp_update_msg(bth, ctx);

	case DPLANE_OP_PW_INSTALL:
	case DPLANE_OP_PW_UNINSTALL:
		return netlink_put_pw_update_msg(bth, ctx);

	case DPLANE_OP_ADDR_INSTALL:
	case DPLANE_OP_ADDR_UNINSTALL:
		return netlink_put_address_update_msg(bth, ctx);

	case DPLANE_OP_MAC_INSTALL:
	case DPLANE_OP_MAC_DELETE:
		return netlink_put_mac_update_msg(bth, ctx);

	case DPLANE_OP_NEIGH_INSTALL:
	case DPLANE_OP_NEIGH_UPDATE:
	case DPLANE_OP_NEIGH_DELETE:
	case DPLANE_OP_VTEP_ADD:
	case DPLANE_OP_VTEP_DELETE:
	case DPLANE_OP_NEIGH_DISCOVER:
	case DPLANE_OP_NEIGH_IP_INSTALL:
	case DPLANE_OP_NEIGH_IP_DELETE:
	case DPLANE_OP_NEIGH_TABLE_UPDATE:
		return netlink_put_neigh_update_msg(bth, ctx);

	case DPLANE_OP_RULE_ADD:
	case DPLANE_OP_RULE_DELETE:
	case DPLANE_OP_RULE_UPDATE:
		return netlink_put_rule_update_msg(bth, ctx);

	case DPLANE_OP_SYS_ROUTE_ADD:
	case DPLANE_OP_SYS_ROUTE_DELETE:
	case DPLANE_OP_ROUTE_NOTIFY:
	case DPLANE_OP_LSP_NOTIFY:
	case DPLANE_OP_BR_PORT_UPDATE:
		return FRR_NETLINK_SUCCESS;

	case DPLANE_OP_IPTABLE_ADD:
	case DPLANE_OP_IPTABLE_DELETE:
	case DPLANE_OP_IPSET_ADD:
	case DPLANE_OP_IPSET_DELETE:
	case DPLANE_OP_IPSET_ENTRY_ADD:
	case DPLANE_OP_IPSET_ENTRY_DELETE:
	case DPLANE_OP_STARTUP_STAGE:
	case DPLANE_OP_VLAN_INSTALL:
		return FRR_NETLINK_ERROR;

	case DPLANE_OP_GRE_SET:
		return netlink_put_gre_set_msg(bth, ctx);

	case DPLANE_OP_INTF_ADDR_ADD:
	case DPLANE_OP_INTF_ADDR_DEL:
	case DPLANE_OP_NONE:
		return FRR_NETLINK_ERROR;

	case DPLANE_OP_INTF_NETCONFIG:
		return netlink_put_intf_netconfig(bth, ctx);

	case DPLANE_OP_INTF_INSTALL:
	case DPLANE_OP_INTF_UPDATE:
	case DPLANE_OP_INTF_DELETE:
		return netlink_put_intf_update_msg(bth, ctx);

	case DPLANE_OP_TC_QDISC_INSTALL:
	case DPLANE_OP_TC_QDISC_UNINSTALL:
		return netlink_put_tc_qdisc_update_msg(bth, ctx);
	case DPLANE_OP_TC_CLASS_ADD:
	case DPLANE_OP_TC_CLASS_DELETE:
	case DPLANE_OP_TC_CLASS_UPDATE:
		return netlink_put_tc_class_update_msg(bth, ctx);
	case DPLANE_OP_TC_FILTER_ADD:
	case DPLANE_OP_TC_FILTER_DELETE:
	case DPLANE_OP_TC_FILTER_UPDATE:
		return netlink_put_tc_filter_update_msg(bth, ctx);

	case DPLANE_OP_SRV6_ENCAP_SRCADDR_SET:
		return netlink_put_sr_tunsrc_set_msg(bth, ctx);
	}

	return FRR_NETLINK_ERROR;
}

/*
 * kernel_update_multi：
 *   批量处理数据平面上下文（dplane_ctx），通过 netlink 批量发送给内核。
 *
 *   主要流程：
 *     1. 初始化一个 netlink 批处理结构 batch，以及一个已处理上下文列表 handled_list。
 *     2. 从传入的 ctx_list 中逐个取出上下文。
 *     3. 若批处理中的命名空间与当前上下文所属命名空间不同，则先发送当前批处理。
 *     4. 默认将上下文状态设为成功，然后调用 nl_put_msg() 将消息加入批处理。
 *     5. 若加入失败，则将上下文状态改为失败。
 *     6. 当批处理累积的消息长度超过限制时，立即发送批处理。
 *     7. 所有上下文处理完毕后，发送剩余的批处理。
 *     8. 最后将处理过的上下文重新放回 ctx_list，供调用者检查状态。
 *
 * 参数：
 *   ctx_list  待处理的数据平面上下文链表头。函数执行后，该链表会被替换为
 *             已处理的上下文列表（顺序可能不变，但每个 ctx 的状态已更新）。
 */
void kernel_update_multi(struct dplane_ctx_list_head *ctx_list)
{
	/* netlink 批处理结构，用于累积多个 netlink 消息后一次性发送 */
	struct nl_batch batch;

	/* 当前正在处理的 dplane 上下文 */
	struct zebra_dplane_ctx *ctx;

	/* 存放已处理上下文的链表，最终会合并回 ctx_list */
	struct dplane_ctx_list_head handled_list;

	/* nl_put_msg() 的返回值，表示 netlink 消息的构造状态 */
	enum netlink_msg_status res;

	/* 初始化已处理上下文列表为空 */
	dplane_ctx_q_init(&handled_list);

	/*
	 * 初始化 netlink 批处理结构。
	 * 第二个参数 &handled_list 通常用于让批处理结构内部引用该列表，
	 * 后续 batch.ctx_list 可能就指向 handled_list，用于收集已处理的上下文。
	 */
	nl_batch_init(&batch, &handled_list);

	/* 循环从 ctx_list 中取出每个上下文进行处理 */
	while (true) {
		/* 从链表头部取出一个上下文，若为空则退出循环 */
		ctx = dplane_ctx_dequeue(ctx_list);
		if (ctx == NULL)
			break;

		/*
		 * 如果当前批处理已经关联了某个命名空间（batch.zns != NULL），
		 * 并且该命名空间与当前上下文的命名空间不一致，
		 * 则必须先发送当前批处理。
		 * 因为一个 netlink 批处理不能混合不同命名空间的消息。
		 */
		if (batch.zns != NULL
		    && batch.zns->ns_id != dplane_ctx_get_ns(ctx)->ns_id)
			nl_batch_send(&batch);

		/*
		 * Assume all messages will succeed and then mark only the ones
		 * that failed.
		 */
		/*
		 * 先假设所有消息都会成功，将上下文状态设为成功。
		 * 后续如果 nl_put_msg() 返回错误，再改为失败。
		 */
		dplane_ctx_set_status(ctx, ZEBRA_DPLANE_REQUEST_SUCCESS);

		/*
		 * 将当前上下文的消息放入 netlink 批处理中。
		 * 返回值 res 表示消息构造状态：
		 *   FRR_NETLINK_ERROR 表示构造失败，其他值表示成功或部分成功。
		 */
		res = nl_put_msg(&batch, ctx);

		/*
		 * 将当前上下文加入批处理的上下文列表（即 handled_list），
		 * 以便后续统一处理或返回给调用者。
		 */
		dplane_ctx_enqueue_tail(&(batch.ctx_list), ctx);

		/* 如果消息构造失败，则将上下文状态改为失败 */
		if (res == FRR_NETLINK_ERROR)
			dplane_ctx_set_status(ctx,
					      ZEBRA_DPLANE_REQUEST_FAILURE);

		/*
		 * 如果当前批处理累积的消息长度超过了预设的限制（batch.limit），
		 * 则立即发送批处理，避免单个 netlink 消息过大。
		 */
		if (batch.curlen > batch.limit)
			nl_batch_send(&batch);
	}

	/* 循环结束后，发送剩余的批处理消息 */
	nl_batch_send(&batch);

	/*
	 * 此时 ctx_list 已经被取空，重新初始化为空链表。
	 * 然后将 handled_list 中已处理的上下文追加回 ctx_list，
	 * 这样调用者可以通过 ctx_list 访问所有已处理的上下文及其状态。
	 */
	dplane_ctx_q_init(ctx_list);
	dplane_ctx_list_append(ctx_list, &handled_list);
}

struct nlsock *kernel_netlink_nlsock_lookup(int sock)
{
	struct nlsock lookup, *retval;

	lookup.sock = sock;

	NLSOCK_LOCK();
	retval = hash_lookup(nlsock_hash, &lookup);
	NLSOCK_UNLOCK();

	return retval;
}

/* Insert nlsock entry into hash */
static void kernel_netlink_nlsock_insert(struct nlsock *nls)
{
	NLSOCK_LOCK();
	(void)hash_get(nlsock_hash, nls, hash_alloc_intern);
	NLSOCK_UNLOCK();
}

/* Remove nlsock entry from hash */
static void kernel_netlink_nlsock_remove(struct nlsock *nls)
{
	NLSOCK_LOCK();
	(void)hash_release(nlsock_hash, nls);
	NLSOCK_UNLOCK();
}

static uint32_t kernel_netlink_nlsock_key(const void *arg)
{
	const struct nlsock *nl = arg;

	return nl->sock;
}

static bool kernel_netlink_nlsock_hash_equal(const void *arg1, const void *arg2)
{
	const struct nlsock *nl1 = arg1;
	const struct nlsock *nl2 = arg2;

	if (nl1->sock == nl2->sock)
		return true;

	return false;
}

/* Exported interface function.  This function simply calls
   netlink_socket (). */
/*
 * kernel_init：
 * 初始化某个 zebra 命名空间（zebra_ns）所需的各类 netlink socket。
 * 包括：
 *   1. 路由/规则/nexthop/TC 等监听 socket
 *   2. 命令发送 socket
 *   3. 数据平面出站 socket
 *   4. 数据平面入站 socket
 *   5. Generic Netlink 命令 socket
 * 同时完成多播组订阅、扩展 ACK 设置、非阻塞设置、接收缓冲区设置、
 * 过滤器安装，以及读事件注册。
 */
void kernel_init(struct zebra_ns *zns)
{
	/* groups：主监听 socket 要订阅的 netlink 多播组位图
	 * dplane_groups：数据平面入站 socket 要订阅的多播组位图
	 * ext_groups：无法放入 32 位 nl_groups 的额外多播组，后续通过 setsockopt 订阅
	 */
	uint32_t groups, dplane_groups, ext_groups;

#if defined SOL_NETLINK
	/* one：setsockopt 的布尔值 1
	 * ret：setsockopt 返回值
	 * grp：NETLINK_ADD_MEMBERSHIP 使用的组 ID
	 */
	int one, ret, grp;
#endif

	/*
	 * Initialize netlink sockets
	 *
	 * If RTMGRP_XXX exists use that, but at some point
	 * I think the kernel developers realized that
	 * keeping track of all the different values would
	 * lead to confusion, so we need to convert the
	 * RTNLGRP_XXX to a bit position for ourself
	 *
	 *
	 * NOTE: If the bit is >= 32, you must use setsockopt(). Those
	 * groups are added further below after SOL_NETLINK is verified to
	 * exist.
	 */
	/*
	 * 说明：
	 * RTMGRP_XXX 是旧式多播组位掩码，可直接按位或。
	 * RTNLGRP_XXX 是较新的枚举组 ID，从 1 开始编号。
	 * 因此这里用：
	 *     (uint32_t)1 << (RTNLGRP_XXX - 1)
	 * 将其转换为对应的位位置。
	 *
	 * 如果某个组的位位置 >= 32，就不能放进 32 位的 nl_groups 中，
	 * 必须使用 setsockopt(NETLINK_ADD_MEMBERSHIP) 单独订阅。
	 * netlink（主线程） 只关心：路由变化、规则、nexthop、TC。
	 */
	groups = RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE | RTMGRP_IPV4_MROUTE |
		 ((uint32_t)1 << (RTNLGRP_IPV4_RULE - 1)) |
		 ((uint32_t)1 << (RTNLGRP_IPV6_RULE - 1)) |
		 ((uint32_t)1 << (RTNLGRP_NEXTHOP - 1)) | ((uint32_t)1 << (RTNLGRP_TC - 1));
	/*
	 * groups 订阅内容包括：
	 *   RTMGRP_IPV4_ROUTE   IPv4 路由变化
	 *   RTMGRP_IPV6_ROUTE   IPv6 路由变化
	 *   RTMGRP_IPV4_MROUTE  IPv4 多播路由
	 *   RTNLGRP_IPV4_RULE   IPv4 规则
	 *   RTNLGRP_IPV6_RULE   IPv6 规则
	 *   RTNLGRP_NEXTHOP     nexthop 对象
	 *   RTNLGRP_TC          流量控制
	 *   netlink_dplane_in（数据平面线程） 关心：链路、邻居、接口地址、netconf。
	 */

	dplane_groups = (RTMGRP_LINK | RTMGRP_NEIGH | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
			 ((uint32_t)1 << (RTNLGRP_IPV4_NETCONF - 1)) |
			 ((uint32_t)1 << (RTNLGRP_IPV6_NETCONF - 1)) |
			 ((uint32_t)1 << (RTNLGRP_MPLS_NETCONF - 1)));
	/*
	 * dplane_groups 用于数据平面入站 socket，订阅：
	 *   RTMGRP_LINK           链路状态变化
	 *   RTMGRP_NEIGH          邻居表变化
	 *   RTMGRP_IPV4_IFADDR    IPv4 接口地址变化
	 *   RTMGRP_IPV6_IFADDR    IPv6 接口地址变化
	 *   RTNLGRP_IPV4_NETCONF  IPv4 netconf 变化
	 *   RTNLGRP_IPV6_NETCONF  IPv6 netconf 变化
	 *   RTNLGRP_MPLS_NETCONF  MPLS netconf 变化
	 */

	/* Use setsockopt for > 31 group */
	/*
	 * RTNLGRP_TUNNEL 可能需要通过 setsockopt 订阅。
	 * 这里先保存到 ext_groups，后面传给 netlink_socket()，
	 * 由 netlink_socket() 内部完成额外组订阅。
	 */
	ext_groups = RTNLGRP_TUNNEL;

	/* 设置监听 socket 名称，包含命名空间 ID，便于日志区分 */
	snprintf(zns->netlink.name, sizeof(zns->netlink.name),
		 "netlink-listen (NS %u)", zns->ns_id);
	/* 先置为 -1，表示尚未创建 */
	zns->netlink.sock = -1;

	/*
	 * 创建 NETLINK_ROUTE 监听 socket：
	 *   &zns->netlink     目标 netlink socket 结构
	 *   groups            要订阅的多播组位图
	 *   &ext_groups       额外多播组数组
	 *   1                 额外多播组数量
	 *   zns->ns_id        网络命名空间 ID
	 *   NETLINK_ROUTE     使用路由 netlink 协议
	 * 失败则记录错误并退出进程。
	 */
	if (netlink_socket(&zns->netlink, groups, &ext_groups, 1, zns->ns_id,
			   NETLINK_ROUTE) < 0) {
		flog_err(EC_LIB_SOCKET, "Failure to create %s socket", zns->netlink.name);
		frr_exit_with_buffer_flush(-1);
	}

	/* 将创建成功的监听 socket 插入全局 netlink socket 链表，便于统一管理 */
	kernel_netlink_nlsock_insert(&zns->netlink);

	/* 设置命令 socket 名称 */
	snprintf(zns->netlink_cmd.name, sizeof(zns->netlink_cmd.name),
		 "netlink-cmd (NS %u)", zns->ns_id);
	zns->netlink_cmd.sock = -1;

	/*
	 * 创建 NETLINK_ROUTE 命令 socket：
	 * 该 socket 主要用于向内核发送命令。
	 * 参数 groups=0、ext_groups=0、数量=0，表示不订阅任何多播组。
	 */
	if (netlink_socket(&zns->netlink_cmd, 0, 0, 0, zns->ns_id,
			   NETLINK_ROUTE) < 0) {
		flog_err(EC_LIB_SOCKET, "Failure to create %s socket", zns->netlink_cmd.name);
		frr_exit_with_buffer_flush(-1);
	}

	/* 插入全局链表 */
	kernel_netlink_nlsock_insert(&zns->netlink_cmd);

	/* Outbound socket for dplane programming of the host OS. */
	/*
	 * 数据平面出站 socket：
	 * 用于 zebra 数据平面向宿主 OS 下发编程命令。
	 */
	snprintf(zns->netlink_dplane_out.name,
		 sizeof(zns->netlink_dplane_out.name), "netlink-dp (NS %u)",
		 zns->ns_id);
	zns->netlink_dplane_out.sock = -1;

	/* 创建 NETLINK_ROUTE 出站 socket，不订阅多播组 */
	if (netlink_socket(&zns->netlink_dplane_out, 0, 0, 0, zns->ns_id,
			   NETLINK_ROUTE) < 0) {
		flog_err(EC_LIB_SOCKET, "Failure to create %s socket",
			 zns->netlink_dplane_out.name);
		frr_exit_with_buffer_flush(-1);
	}

	/* 插入全局链表 */
	kernel_netlink_nlsock_insert(&zns->netlink_dplane_out);

	/* Inbound socket for OS events coming to the dplane. */
	/*
	 * 数据平面入站 socket：
	 * 用于接收 OS 发往数据平面的 netlink 事件。
	 * 它需要订阅 dplane_groups 中的多播组。
	 */
	snprintf(zns->netlink_dplane_in.name,
		 sizeof(zns->netlink_dplane_in.name), "netlink-dp-in (NS %u)",
		 zns->ns_id);
	zns->netlink_dplane_in.sock = -1;

	if (netlink_socket(&zns->netlink_dplane_in, dplane_groups, 0, 0,
			   zns->ns_id, NETLINK_ROUTE) < 0) {
		flog_err(EC_LIB_SOCKET, "Failure to create %s socket", zns->netlink_dplane_in.name);
		frr_exit_with_buffer_flush(-1);
	}

	/* 插入全局链表 */
	kernel_netlink_nlsock_insert(&zns->netlink_dplane_in);

	/* Generic Netlink socket. */
	/*
	 * Generic Netlink 命令 socket：
	 * 用于 generic netlink 通信，例如某些内核子系统。
	 * 创建失败只告警，不退出，因为 generic netlink 可能不是强制的。
	 */
	snprintf(zns->ge_netlink_cmd.name, sizeof(zns->ge_netlink_cmd.name),
		 "generic-netlink-cmd (NS %u)", zns->ns_id);
	zns->ge_netlink_cmd.sock = -1;

	if (netlink_socket(&zns->ge_netlink_cmd, 0, 0, 0, zns->ns_id,
			   NETLINK_GENERIC) < 0) {
		zlog_warn("Failure to create %s socket",
			  zns->ge_netlink_cmd.name);
	}

	/* 只有创建成功才插入全局链表 */
	if (zns->ge_netlink_cmd.sock >= 0)
		kernel_netlink_nlsock_insert(&zns->ge_netlink_cmd);

	/*
	 * SOL_NETLINK is not available on all platforms yet
	 * apparently.  It's in bits/socket.h which I am not
	 * sure that we want to pull into our build system.
	 */
	/*
	 * SOL_NETLINK 并非所有平台都支持。
	 * 因此下面这些 setsockopt 操作都放在条件编译中。
	 */
#if defined SOL_NETLINK

	/*
	 * setsockopt multicast group subscriptions that don't fit in nl_groups
	 */
	/*
	 * 订阅无法放入 nl_groups 位图的多播组。
	 * NETLINK_ADD_MEMBERSHIP 的参数是“组 ID”，不是位图。
	 */
	grp = RTNLGRP_BRVLAN;
	ret = setsockopt(zns->netlink_dplane_in.sock, SOL_NETLINK,
			 NETLINK_ADD_MEMBERSHIP, &grp, sizeof(grp));

	if (ret < 0)
		zlog_notice(
			"Registration for RTNLGRP_BRVLAN Membership failed : %d %s",
			errno, safe_strerror(errno));
	/*
	 * 为数据平面入站 socket 订阅桥接 VLAN 事件。
	 * 失败只记录 notice，因为这不是致命错误。
	 */

	/*
	 * Let's tell the kernel that we want to receive extended
	 * ACKS over our command socket(s)
	 */
	/*
	 * 让内核在命令 socket 上返回扩展 ACK。
	 * 扩展 ACK 可以提供更详细的错误信息。
	 */
	one = 1;
	ret = setsockopt(zns->netlink_cmd.sock, SOL_NETLINK, NETLINK_EXT_ACK,
			 &one, sizeof(one));

	if (ret < 0)
		zlog_notice("Registration for extended cmd ACK failed : %d %s",
			    errno, safe_strerror(errno));

	/* 对数据平面出站 socket 同样启用扩展 ACK */
	one = 1;
	ret = setsockopt(zns->netlink_dplane_out.sock, SOL_NETLINK,
			 NETLINK_EXT_ACK, &one, sizeof(one));

	if (ret < 0)
		zlog_notice("Registration for extended dp ACK failed : %d %s",
			    errno, safe_strerror(errno));

	/* 如果 generic netlink 命令 socket 创建成功，也启用扩展 ACK */
	if (zns->ge_netlink_cmd.sock >= 0) {
		one = 1;
		ret = setsockopt(zns->ge_netlink_cmd.sock, SOL_NETLINK,
				 NETLINK_EXT_ACK, &one, sizeof(one));
		if (ret < 0)
			flog_err(EC_ZEBRA_NETLINK_EXT_ACK_FAILED,
				 "Registration for extended generic netlink cmd ACK failed : %d %s",
				 errno, safe_strerror(errno));
	}

	/*
	 * Trim off the payload of the original netlink message in the
	 * acknowledgment. This option is available since Linux 4.2, so if
	 * setsockopt fails, ignore the error.
	 */
	/*
	 * NETLINK_CAP_ACK：
	 * 在 ACK 中截掉原始 netlink 消息的 payload，只保留必要信息，
	 * 从而减小 ACK 包大小。
	 * 该选项从 Linux 4.2 开始可用；如果 setsockopt 失败，忽略即可。
	 */
	one = 1;
	ret = setsockopt(zns->netlink_dplane_out.sock, SOL_NETLINK,
			 NETLINK_CAP_ACK, &one, sizeof(one));
	if (ret < 0)
		zlog_notice(
			"Registration for reduced ACK packet size failed, probably running an early kernel");
#endif

	/* Register kernel socket. */
	/*
	 * 将内核 socket 设置为非阻塞模式。
	 * 这些 socket 会被集成到 FRR 事件循环中，不能阻塞事件循环。
	 */
	if (fcntl(zns->netlink.sock, F_SETFL, O_NONBLOCK) < 0)
		flog_err_sys(EC_LIB_SOCKET, "Can't set %s socket flags: %s",
			     zns->netlink.name, safe_strerror(errno));

	/* 命令 socket 设为非阻塞 */
	if (fcntl(zns->netlink_cmd.sock, F_SETFL, O_NONBLOCK) < 0)
		flog_err(EC_LIB_SOCKET, "Can't set %s socket error: %s(%d)", zns->netlink_cmd.name,
			 safe_strerror(errno), errno);

	/* 数据平面出站 socket 设为非阻塞 */
	if (fcntl(zns->netlink_dplane_out.sock, F_SETFL, O_NONBLOCK) < 0)
		flog_err(EC_LIB_SOCKET, "Can't set %s socket error: %s(%d)",
			 zns->netlink_dplane_out.name, safe_strerror(errno), errno);

	/* 数据平面入站 socket 设为非阻塞 */
	if (fcntl(zns->netlink_dplane_in.sock, F_SETFL, O_NONBLOCK) < 0)
		flog_err(EC_LIB_SOCKET, "Can't set %s socket error: %s(%d)",
			 zns->netlink_dplane_in.name, safe_strerror(errno), errno);

	/* generic netlink 命令 socket 如果存在，也设为非阻塞 */
	if (zns->ge_netlink_cmd.sock >= 0) {
		if (fcntl(zns->ge_netlink_cmd.sock, F_SETFL, O_NONBLOCK) < 0)
			flog_err(EC_LIB_SOCKET, "Can't set %s socket error: %s(%d)",
				 zns->ge_netlink_cmd.name, safe_strerror(errno), errno);
	}

	/* Set receive buffer size if it's set from command line */
	/*
	 * 如果命令行或全局配置设置了 rcvbufsize，
	 * 则为各个 netlink socket 设置接收缓冲区大小。
	 */
	if (rcvbufsize) {
		netlink_recvbuf(&zns->netlink, rcvbufsize);
		netlink_recvbuf(&zns->netlink_cmd, rcvbufsize);
		netlink_recvbuf(&zns->netlink_dplane_out, rcvbufsize);
		netlink_recvbuf(&zns->netlink_dplane_in, rcvbufsize);

		if (zns->ge_netlink_cmd.sock >= 0)
			netlink_recvbuf(&zns->ge_netlink_cmd, rcvbufsize);
	}

	/* Set filter for inbound sockets, to exclude events we've generated
	 * ourselves.
	 */
	/*
	 * 为入站 socket 安装过滤器，排除本进程自己发出的事件，
	 * 避免命令 socket 或出站 socket 发出的 netlink 消息又被自己收到。
	 *
	 * 参数中的 nl_pid 是 netlink 本地端口 ID。
	 * 这里传入：
	 *   zns->netlink_cmd.snl.nl_pid       命令 socket 的端口 ID
	 *   zns->netlink_dplane_out.snl.nl_pid 数据平面出站 socket 的端口 ID
	 */
	netlink_install_filter(zns->netlink.sock, zns->netlink_cmd.snl.nl_pid,
			       zns->netlink_dplane_out.snl.nl_pid);

	/* 对数据平面入站 socket 也安装同样的过滤器 */
	netlink_install_filter(zns->netlink_dplane_in.sock,
			       zns->netlink_cmd.snl.nl_pid,
			       zns->netlink_dplane_out.snl.nl_pid);

	/* 初始化 netlink 读事件句柄 */
	zns->t_netlink = NULL;

	/*
	 * 将主监听 socket 注册到 FRR 事件循环：
	 *   zrouter.master     事件主循环
	 *   kernel_read        可读回调函数
	 *   zns                回调参数
	 *   zns->netlink.sock  监听的 fd
	 *   &zns->t_netlink    保存事件句柄
	 */
	event_add_read(zrouter.master, kernel_read, zns, zns->netlink.sock,
		       &zns->t_netlink);

	/* 初始化路由 netlink 相关状态 */
	rt_netlink_init();

	/* 初始化 generic netlink 相关状态 */
	ge_netlink_init(zns);
}

/* Helper to clean up an nlsock */
static void kernel_nlsock_fini(struct nlsock *nls)
{
	if (nls && nls->sock >= 0) {
		kernel_netlink_nlsock_remove(nls);
		close(nls->sock);
		nls->sock = -1;
		XFREE(MTYPE_NL_BUF, nls->buf);
		nls->buflen = 0;
	}
}

void kernel_terminate(struct zebra_ns *zns, bool complete)
{
	event_cancel(&zns->t_netlink);

	kernel_nlsock_fini(&zns->netlink);

	kernel_nlsock_fini(&zns->netlink_cmd);

	kernel_nlsock_fini(&zns->netlink_dplane_in);

	kernel_nlsock_fini(&zns->ge_netlink_cmd);

	/* During zebra shutdown, we need to leave the dataplane socket
	 * around until all work is done.
	 */
	if (complete)
		kernel_nlsock_fini(&zns->netlink_dplane_out);
}

/*
 * Global init for platform-/OS-specific things
 */
void kernel_router_init(void)
{
	/* Init nlsock hash and lock */
	pthread_mutex_init(&nlsock_mutex, NULL);
	nlsock_hash = hash_create_size(8, kernel_netlink_nlsock_key,
				       kernel_netlink_nlsock_hash_equal,
				       "Netlink Socket Hash");
}

/*
 * Global deinit for platform-/OS-specific things
 */
void kernel_router_terminate(void)
{
	XFREE(MTYPE_NL_BUF, nl_batch_tx_buf);

	pthread_mutex_destroy(&nlsock_mutex);

	hash_free(nlsock_hash);
	nlsock_hash = NULL;
}

#endif /* HAVE_NETLINK */
