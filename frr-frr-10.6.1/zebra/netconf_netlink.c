// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * netconf_netlink.c - netconf interaction with the kernel using
 * netlink
 * Copyright (C) 2021  Nvidia, Inc.
 *                     Donald Sharp
 */
#include <zebra.h>
#include <fcntl.h>

#ifdef HAVE_NETLINK /* Netlink OSes only */

#include <ns.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include "linux/netconf.h"

#include "lib/lib_errors.h"
#include "zebra/zebra_ns.h"
#include "zebra/zebra_errors.h"
#include "zebra/zebra_dplane.h"
#include "zebra/kernel_netlink.h"
#include "lib/netlink_parser.h"
#include "zebra/netconf_netlink.h"
#include "zebra/debug.h"

static struct rtattr *netconf_rta(struct netconfmsg *ncm)
{
	return (struct rtattr *)((char *)ncm +
				 NLMSG_ALIGN(sizeof(struct netconfmsg)));
}

/*
 * Handle netconf update about a single interface: create dplane
 * context, and enqueue for processing in the main zebra pthread.
 */
/*
 * netlink_netconf_dplane_update：
 *   处理单个接口的 netconf 更新：构造数据平面上下文，并将其
 *   入队到 zebra 主线程处理。
 *
 *   背景：
 *     netconf 是内核提供的一组按接口、按地址族配置的转发相关选项，
 *     例如：
 *       - MPLS 输入是否启用
 *       - 多播转发是否启用
 *       - 接口 down 时是否丢弃路由（linkdown）
 *     当这些配置在内核侧发生变化时，需要通过该函数把新状态同步
 *     到 zebra 的接口数据结构中。
 *
 *   该函数运行在数据平面 pthread 中（由 netlink_netconf_change() 调用），
 *   只负责构造上下文并投递给主线程，不直接修改 zebra 内部状态。
 *
 * 参数：
 *   ns_id       网络命名空间 ID
 *   afi         地址族（AFI_IP / AFI_IP6）
 *   ifindex     接口索引
 *   mpls_on     MPLS 输入的 netconf 状态
 *   mcast_on    多播转发的 netconf 状态
 *   linkdown_on 接口 down 时是否丢弃路由的 netconf 状态
 *
 * 返回值：
 *   总是返回 0。
 */
static int
netlink_netconf_dplane_update(ns_id_t ns_id, afi_t afi, ifindex_t ifindex,
			      enum dplane_netconf_status_e mpls_on,
			      enum dplane_netconf_status_e mcast_on,
			      enum dplane_netconf_status_e linkdown_on)
{
	/* 数据平面上下文对象 */
	struct zebra_dplane_ctx *ctx;

	/* 从内存池分配一个数据平面上下文 */
	ctx = dplane_ctx_alloc();

	/*
	 * 设置上下文操作类型为 INTF_NETCONFIG，
	 * 主线程会根据该操作码分发到 zebra_if_netconf_update_ctx()。
	 */
	dplane_ctx_set_op(ctx, DPLANE_OP_INTF_NETCONFIG);

	/* 保存命名空间 ID */
	dplane_ctx_set_ns_id(ctx, ns_id);

	/* 保存地址族（AFI_IP / AFI_IP6） */
	dplane_ctx_set_afi(ctx, afi);

	/* 保存接口索引 */
	dplane_ctx_set_ifindex(ctx, ifindex);

	/*
	 * 保存 netconf 的三类状态：
	 *   - mpls：MPLS 输入是否启用
	 *   - mcast：多播转发是否启用
	 *   - linkdown：接口 down 时是否丢弃路由
	 */
	dplane_ctx_set_netconf_mpls(ctx, mpls_on);
	dplane_ctx_set_netconf_mcast(ctx, mcast_on);
	dplane_ctx_set_netconf_linkdown(ctx, linkdown_on);

	/* Enqueue ctx for main pthread to process */
	/*
	 * 将上下文入队到 zebra 主线程的处理队列。
	 * 该函数内部会通过 dg_results_cb 把上下文交给主线程，
	 * 最终触发 zebra_if_dplane_result() 中 INTF_NETCONFIG 分支的处理。
	 */
	dplane_provider_enqueue_to_zebra(ctx);

	/* 构造并投递成功，返回 0 */
	return 0;
}

/*
 * Parse and process an incoming netlink netconf update.
 */
int netlink_netconf_change(struct nlmsghdr *h, ns_id_t ns_id, int startup)
{
	struct netconfmsg *ncm;
	struct rtattr *tb[NETCONFA_MAX + 1] = {};
	int len;
	ifindex_t ifindex;
	uint32_t ival;
	afi_t afi;
	enum dplane_netconf_status_e mpls_on = DPLANE_NETCONF_STATUS_UNKNOWN;
	enum dplane_netconf_status_e mcast_on = DPLANE_NETCONF_STATUS_UNKNOWN;
	enum dplane_netconf_status_e linkdown_on =
		DPLANE_NETCONF_STATUS_UNKNOWN;

	if (h->nlmsg_type != RTM_NEWNETCONF && h->nlmsg_type != RTM_DELNETCONF)
		return 0;

	len = h->nlmsg_len - NLMSG_LENGTH(sizeof(struct netconfmsg));
	if (len < 0) {
		flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
			 "%s: Message received from netlink is of a broken size: %d, min %zu",
			 __func__, h->nlmsg_len, (size_t)NLMSG_LENGTH(sizeof(struct netconfmsg)));
		return -1;
	}

	ncm = NLMSG_DATA(h);

	/*
	 * FRR does not have an internal representation of afi_t for
	 * the MPLS Address Family that the kernel has.  So let's
	 * just call it v4.  This is ok because the kernel appears
	 * to do a good job of not sending data that is mixed/matched
	 * across families
	 */
#ifdef AF_MPLS
	if (ncm->ncm_family == AF_MPLS)
		afi = AFI_IP;
	else
#endif /* AF_MPLS */
		afi = family2afi(ncm->ncm_family);

	netlink_parse_rtattr(tb, NETCONFA_MAX, netconf_rta(ncm), len);

	if (!tb[NETCONFA_IFINDEX]) {
		flog_err(EC_ZEBRA_NETLINK_MISSING_IFINDEX,
			 "NETCONF message received from netlink without an ifindex");
		return 0;
	}

	ifindex = *(ifindex_t *)RTA_DATA(tb[NETCONFA_IFINDEX]);

	if (tb[NETCONFA_INPUT]) {
		ival = *(uint32_t *)RTA_DATA(tb[NETCONFA_INPUT]);
		if (ival != 0)
			mpls_on = DPLANE_NETCONF_STATUS_ENABLED;
		else
			mpls_on = DPLANE_NETCONF_STATUS_DISABLED;
	}

	if (tb[NETCONFA_MC_FORWARDING]) {
		ival = *(uint32_t *)RTA_DATA(tb[NETCONFA_MC_FORWARDING]);
		if (ival != 0)
			mcast_on = DPLANE_NETCONF_STATUS_ENABLED;
		else
			mcast_on = DPLANE_NETCONF_STATUS_DISABLED;
	}

	if (tb[NETCONFA_IGNORE_ROUTES_WITH_LINKDOWN]) {
		ival = *(uint32_t *)RTA_DATA(
			tb[NETCONFA_IGNORE_ROUTES_WITH_LINKDOWN]);
		if (ival != 0)
			linkdown_on = DPLANE_NETCONF_STATUS_ENABLED;
		else
			linkdown_on = DPLANE_NETCONF_STATUS_DISABLED;
	}

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug(
			"%s: interface %u is mpls on: %d multicast on: %d linkdown: %d",
			__func__, ifindex, mpls_on, mcast_on, linkdown_on);

	/* Create a dplane context and pass it along for processing */
	netlink_netconf_dplane_update(ns_id, afi, ifindex, mpls_on, mcast_on,
				      linkdown_on);

	return 0;
}

/*
 * Request info from the host OS. This only sends the request; any replies
 * are processed asynchronously.
 */
int netlink_request_netconf(int sockfd)
{
	struct nlsock *nls;
	struct {
		struct nlmsghdr n;
		struct netconfmsg ncm;
		char buf[1024];
	} req = {};

	nls = kernel_netlink_nlsock_lookup(sockfd);

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("%s: nlsock %s", __func__, nls ? nls->name : "NULL");

	if (nls == NULL)
		return -1;

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct netconfmsg));
	req.n.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_GETNETCONF;
	req.ncm.ncm_family = AF_UNSPEC;

	return netlink_request(nls, &req);
}

extern struct zebra_privs_t zserv_privs;
/*
 * Currently netconf has no ability to set from netlink.
 * So we've received a request to do this work in the data plane.
 * as such we need to set the value via the /proc system
 */
/*
 * netlink_put_intf_netconfig：
 *   通过 /proc 文件系统设置接口的 netconf 配置。
 *
 *   背景：
 *     目前内核的 netlink 接口没有提供直接设置 netconf 的能力，
 *     只能读取变更通知。因此当 zebra 需要主动设置某个接口的
 *     netconf（例如 MPLS 输入是否启用）时，只能通过写 /proc
 *     文件系统来实现。
 *
 *   该函数作为数据平面 provider 的消息构造回调，
 *   由批处理框架在发送 netlink 批处理时调用。
 *
 * 参数：
 *   bth  netlink 批处理结构（本函数不使用，因为不走 netlink）
 *   ctx  数据平面上下文，包含接口名和要设置的 netconf 值
 *
 * 返回值：
 *   FRR_NETLINK_SUCCESS  写入成功
 *   FRR_NETLINK_ERROR    参数错误或写入失败
 */
enum netlink_msg_status netlink_put_intf_netconfig(struct nl_batch *bth,
						   struct zebra_dplane_ctx *ctx)
{
	/* 从上下文中提取接口名 */
	const char *ifname = dplane_ctx_get_ifname(ctx);

	/* 从上下文中提取 MPLS 输入的 netconf 状态（启用/禁用/未指定） */
	enum dplane_netconf_status_e mpls_on = dplane_ctx_get_netconf_mpls(ctx);

	char set[64];              /* 要写入 /proc 文件的内容，例如 "1\n" */
	char mpls_proc[PATH_MAX];  /* /proc 中 MPLS 输入配置文件的路径 */
	int fd, ret = FRR_NETLINK_ERROR; /* 文件描述符及默认返回值（失败） */

	/*
	 * 构造 /proc 路径：
	 *   /proc/sys/net/mpls/conf/<ifname>/input
	 * 该文件控制该接口上 MPLS 输入的启用状态。
	 */
	snprintf(mpls_proc, sizeof(mpls_proc),
		 "/proc/sys/net/mpls/conf/%s/input", ifname);

	/*
	 * 根据 netconf 状态决定要写入的内容：
	 *   ENABLED  → "1\n"
	 *   DISABLED → "0\n"
	 *   其他值视为非法，记录开发错误并返回失败。
	 */
	if (mpls_on == DPLANE_NETCONF_STATUS_ENABLED)
		snprintf(set, sizeof(set), "1\n");
	else if (mpls_on == DPLANE_NETCONF_STATUS_DISABLED)
		snprintf(set, sizeof(set), "0\n");
	else {
		flog_err_sys(
			EC_LIB_DEVELOPMENT,
			"%s: Expected interface %s to be set to ENABLED or DISABLED was %d",
			__func__, ifname, mpls_on);
		return ret;
	}

	/*
	 * 在提升权限的上下文中写 /proc 文件。
	 * 因为 /proc/sys/net 下的文件通常需要 CAP_NET_ADMIN 权限。
	 */
	frr_with_privs (&zserv_privs) {
		/* 以只写方式打开 /proc 文件 */
		fd = open(mpls_proc, O_WRONLY);
		if (fd < 0) {
			/* 打开失败：通常表示接口不存在或 MPLS 模块未加载 */
			flog_err_sys(
				EC_LIB_SOCKET,
				"%s: Unable to open %s for writing: %s(%d)",
				__func__, mpls_proc, safe_strerror(errno),
				errno);
			return ret;
		}

		/*
		 * 写入 "0\n" 或 "1\n"，长度为 2。
		 * 写入成功则返回 FRR_NETLINK_SUCCESS。
		 * 注意：写入的是 2 个字节，包含换行符。
		 */
		if (write(fd, set, 2) == 2)
			ret = FRR_NETLINK_SUCCESS;
		else
			flog_err_sys(EC_LIB_SOCKET,
				     "%s: Unsuccessful write to %s: %s(%d)",
				     __func__, mpls_proc, safe_strerror(errno),
				     errno);

		/* 关闭文件描述符 */
		close(fd);
	}

	/* 返回写入结果 */
	return ret;
}

#endif	/* HAVE_NETLINK */
