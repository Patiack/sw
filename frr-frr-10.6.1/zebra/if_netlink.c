// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Interface looking up by netlink.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

#include <zebra.h>

#ifdef GNU_LINUX

/* The following definition is to workaround an issue in the Linux kernel
 * header files with redefinition of 'struct in6_addr' in both
 * netinet/in.h and linux/in6.h.
 * Reference - https://sourceware.org/ml/libc-alpha/2013-01/msg00599.html
 */
#define _LINUX_IN6_H
#define _LINUX_IF_H
#define _LINUX_IP_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/if_ether.h>
#include <linux/if_bridge.h>
#include <linux/if_link.h>
#include <linux/if_tunnel.h>
#include <net/if_arp.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>

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
#include "vrf_int.h"
#include "mpls.h"
#include "lib_errors.h"

#include "vty.h"
#include "zebra/zserv.h"
#include "zebra/zebra_ns.h"
#include "zebra/zebra_vrf.h"
#include "zebra/rt.h"
#include "zebra/redistribute.h"
#include "zebra/interface.h"
#include "zebra/debug.h"
#include "zebra/rtadv.h"
#include "zebra/zebra_ptm.h"
#include "zebra/zebra_mpls.h"
#include "zebra/kernel_netlink.h"
#include "zebra/rt_netlink.h"
#include "zebra/if_netlink.h"
#include "zebra/zebra_errors.h"
#include "zebra/zebra_vxlan.h"
#include "zebra/zebra_evpn_mh.h"
#include "zebra/zebra_l2.h"
#include "zebra/netconf_netlink.h"
#include "zebra/zebra_trace.h"
#include "lib/netlink_parser.h"

extern struct zebra_privs_t zserv_privs;

/* Utility function to parse hardware link-layer address and update ifp */
static void netlink_interface_update_hw_addr(struct rtattr **tb,
					     struct zebra_dplane_ctx *ctx)
{
	if (tb[IFLA_ADDRESS]) {
		int hw_addr_len;

		hw_addr_len = RTA_PAYLOAD(tb[IFLA_ADDRESS]);

		if (hw_addr_len > INTERFACE_HWADDR_MAX)
			zlog_warn("Hardware address is too large: %d",
				  hw_addr_len);
		else
			dplane_ctx_set_ifp_hw_addr(ctx, hw_addr_len,
						   RTA_DATA(tb[IFLA_ADDRESS]));
	}
}

static enum zebra_link_type netlink_to_zebra_link_type(unsigned int hwt)
{
	switch (hwt) {
	case ARPHRD_ETHER:
		return ZEBRA_LLT_ETHER;
	case ARPHRD_EETHER:
		return ZEBRA_LLT_EETHER;
	case ARPHRD_AX25:
		return ZEBRA_LLT_AX25;
	case ARPHRD_PRONET:
		return ZEBRA_LLT_PRONET;
	case ARPHRD_IEEE802:
		return ZEBRA_LLT_IEEE802;
	case ARPHRD_ARCNET:
		return ZEBRA_LLT_ARCNET;
	case ARPHRD_APPLETLK:
		return ZEBRA_LLT_APPLETLK;
	case ARPHRD_DLCI:
		return ZEBRA_LLT_DLCI;
	case ARPHRD_ATM:
		return ZEBRA_LLT_ATM;
	case ARPHRD_METRICOM:
		return ZEBRA_LLT_METRICOM;
	case ARPHRD_IEEE1394:
		return ZEBRA_LLT_IEEE1394;
	case ARPHRD_EUI64:
		return ZEBRA_LLT_EUI64;
	case ARPHRD_INFINIBAND:
		return ZEBRA_LLT_INFINIBAND;
	case ARPHRD_SLIP:
		return ZEBRA_LLT_SLIP;
	case ARPHRD_CSLIP:
		return ZEBRA_LLT_CSLIP;
	case ARPHRD_SLIP6:
		return ZEBRA_LLT_SLIP6;
	case ARPHRD_CSLIP6:
		return ZEBRA_LLT_CSLIP6;
	case ARPHRD_RSRVD:
		return ZEBRA_LLT_RSRVD;
	case ARPHRD_ADAPT:
		return ZEBRA_LLT_ADAPT;
	case ARPHRD_ROSE:
		return ZEBRA_LLT_ROSE;
	case ARPHRD_X25:
		return ZEBRA_LLT_X25;
	case ARPHRD_PPP:
		return ZEBRA_LLT_PPP;
	case ARPHRD_CISCO:
		return ZEBRA_LLT_CHDLC;
	case ARPHRD_LAPB:
		return ZEBRA_LLT_LAPB;
	case ARPHRD_RAWHDLC:
		return ZEBRA_LLT_RAWHDLC;
	case ARPHRD_TUNNEL:
		return ZEBRA_LLT_IPIP;
	case ARPHRD_TUNNEL6:
		return ZEBRA_LLT_IPIP6;
	case ARPHRD_FRAD:
		return ZEBRA_LLT_FRAD;
	case ARPHRD_SKIP:
		return ZEBRA_LLT_SKIP;
	case ARPHRD_LOOPBACK:
		return ZEBRA_LLT_LOOPBACK;
	case ARPHRD_LOCALTLK:
		return ZEBRA_LLT_LOCALTLK;
	case ARPHRD_FDDI:
		return ZEBRA_LLT_FDDI;
	case ARPHRD_SIT:
		return ZEBRA_LLT_SIT;
	case ARPHRD_IPDDP:
		return ZEBRA_LLT_IPDDP;
	case ARPHRD_IPGRE:
		return ZEBRA_LLT_IPGRE;
	case ARPHRD_PIMREG:
		return ZEBRA_LLT_PIMREG;
	case ARPHRD_HIPPI:
		return ZEBRA_LLT_HIPPI;
	case ARPHRD_ECONET:
		return ZEBRA_LLT_ECONET;
	case ARPHRD_IRDA:
		return ZEBRA_LLT_IRDA;
	case ARPHRD_FCPP:
		return ZEBRA_LLT_FCPP;
	case ARPHRD_FCAL:
		return ZEBRA_LLT_FCAL;
	case ARPHRD_FCPL:
		return ZEBRA_LLT_FCPL;
	case ARPHRD_FCFABRIC:
		return ZEBRA_LLT_FCFABRIC;
	case ARPHRD_IEEE802_TR:
		return ZEBRA_LLT_IEEE802_TR;
	case ARPHRD_IEEE80211:
		return ZEBRA_LLT_IEEE80211;
#ifdef ARPHRD_IEEE802154
	case ARPHRD_IEEE802154:
		return ZEBRA_LLT_IEEE802154;
#endif
#ifdef ARPHRD_IP6GRE
	case ARPHRD_IP6GRE:
		return ZEBRA_LLT_IP6GRE;
#endif
#ifdef ARPHRD_IEEE802154_PHY
	case ARPHRD_IEEE802154_PHY:
		return ZEBRA_LLT_IEEE802154_PHY;
#endif

	default:
		return ZEBRA_LLT_UNKNOWN;
	}
}

static void netlink_determine_zebra_iftype(const char *kind,
					   enum zebra_iftype *zif_type)
{
	*zif_type = ZEBRA_IF_OTHER;

	if (!kind)
		return;

	if (strcmp(kind, "vrf") == 0)
		*zif_type = ZEBRA_IF_VRF;
	else if (strcmp(kind, "bridge") == 0)
		*zif_type = ZEBRA_IF_BRIDGE;
	else if (strcmp(kind, "vlan") == 0)
		*zif_type = ZEBRA_IF_VLAN;
	else if (strcmp(kind, "vxlan") == 0)
		*zif_type = ZEBRA_IF_VXLAN;
	else if (strcmp(kind, "macvlan") == 0)
		*zif_type = ZEBRA_IF_MACVLAN;
	else if (strcmp(kind, "veth") == 0)
		*zif_type = ZEBRA_IF_VETH;
	else if (strcmp(kind, "bond") == 0)
		*zif_type = ZEBRA_IF_BOND;
	else if (strcmp(kind, "team") == 0)
		*zif_type = ZEBRA_IF_BOND;
	else if (strcmp(kind, "gretap") == 0)
		*zif_type = ZEBRA_IF_GRETAP;
	else if (strcmp(kind, "ip6gretap") == 0)
		*zif_type = ZEBRA_IF_IP6GRETAP;
	else if (strcmp(kind, "gre") == 0)
		*zif_type = ZEBRA_IF_GRE;
	else if (strcmp(kind, "ip6gre") == 0)
		*zif_type = ZEBRA_IF_IP6GRE;
	else if (strcmp(kind, "dummy") == 0)
		*zif_type = ZEBRA_IF_DUMMY;
}

static void netlink_vrf_change(struct nlmsghdr *h, struct rtattr *tb,
			       uint32_t ns_id, const char *name,
			       struct zebra_dplane_ctx *ctx)
{
	struct rtattr *linkinfo[IFLA_INFO_MAX + 1];
	struct rtattr *attr[IFLA_VRF_MAX + 1];

	netlink_parse_rtattr_nested(linkinfo, IFLA_INFO_MAX, tb);

	if (!linkinfo[IFLA_INFO_DATA]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"%s: IFLA_INFO_DATA missing from VRF message: %s",
				__func__, name);

		frrtrace(2, frr_zebra, netlink_vrf_change, name, 1);

		return;
	}

	netlink_parse_rtattr_nested(attr, IFLA_VRF_MAX,
				    linkinfo[IFLA_INFO_DATA]);
	if (!attr[IFLA_VRF_TABLE]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"%s: IFLA_VRF_TABLE missing from VRF message: %s",
				__func__, name);

		frrtrace(2, frr_zebra, netlink_vrf_change, name, 2);

		return;
	}

	dplane_ctx_set_ifp_table_id(
		ctx, *(uint32_t *)RTA_DATA(attr[IFLA_VRF_TABLE]));
}

/*
 * get_iflink_speed：
 *   通过 ethtool ioctl 获取指定接口的链路速率。
 *
 *   主要流程：
 *     1. 初始化 ifreq 和 ethtool_cmd 结构。
 *     2. 在提升权限的上下文中创建 VRF 相关 socket。
 *     3. 使用 SIOCETHTOOL + ETHTOOL_GSET ioctl 获取接口速度信息。
 *     4. 从 ethtool_cmd 中组合出 32 位速度值（高 16 位 + 低 16 位）。
 *     5. 处理错误：socket 创建失败、ioctl 失败、速度未知等。
 *
 * 参数：
 *   interface  要查询的接口
 *   error      输出参数，返回错误码（可为 NULL）
 *              - 0                              成功
 *              - INTERFACE_SPEED_ERROR_READ     socket 创建失败或 ioctl 失败（ENODEV）
 *              - INTERFACE_SPEED_ERROR_UNKNOWN  速度为 UINT32_MAX，表示未知
 *
 * 返回值：
 *   接口链路速率（Mbps）；失败或未知时返回 0
 */
static uint32_t get_iflink_speed(struct interface *interface, int *error)
{
	struct ifreq ifdata;                     /* 接口请求结构，用于 ioctl */
	struct ethtool_cmd ecmd;                 /* ethtool 命令结构，携带速度等信息 */
	int sd;                                  /* socket 文件描述符 */
	int rc;                                  /* ioctl 返回值 */
	const char *ifname = interface->name;    /* 接口名称，便于日志输出 */
	uint32_t ret;                            /* 组合后的速度值 */

	/* 如果调用方提供了 error 指针，先将其置为 0（表示无错误） */
	if (error)
		*error = 0;

	/* initialize struct */
	/* 清零 ifreq 结构，避免残留数据 */
	memset(&ifdata, 0, sizeof(ifdata));

	/* set interface name */
	/* 将接口名拷贝到 ifreq 中，用于后续 ioctl 指定目标接口 */
	strlcpy(ifdata.ifr_name, ifname, sizeof(ifdata.ifr_name));

	/* initialize ethtool interface */
	/* 清零 ethtool_cmd 结构 */
	memset(&ecmd, 0, sizeof(ecmd));

	/*
	 * 设置 ethtool 命令为 ETHTOOL_GSET（获取设置）。
	 * 注释中提到 ETHTOOL_GLINK，但这里使用 GSET 是为了同时获取
	 * 速度（speed / speed_hi）等信息。
	 */
	ecmd.cmd = ETHTOOL_GSET; /* ETHTOOL_GLINK */

	/* 将 ethtool_cmd 挂到 ifreq 上，作为 ioctl 的输入/输出数据 */
	ifdata.ifr_data = (caddr_t)&ecmd;

	/* use ioctl to get speed of an interface */
	/*
	 * 在提升权限的上下文中执行 socket 创建和 ioctl。
	 * 因为 SIOCETHTOOL 通常需要 CAP_NET_ADMIN 权限。
	 */
	frr_with_privs(&zserv_privs) {
		/*
		 * 在接口所属 VRF 中创建一个 UDP socket。
		 * 使用 vrf_socket() 而非普通 socket()，以便 ioctl 能作用于
		 * 该 VRF 中的接口。
		 */
		sd = vrf_socket(PF_INET, SOCK_DGRAM, IPPROTO_IP,
				interface->vrf->vrf_id, NULL);

		/* socket 创建失败：通常意味着 VRF 有问题 */
		if (sd < 0) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("Failure to read interface %s speed: %d %s",
					   ifname, errno, safe_strerror(errno));

			/* no vrf socket creation may probably mean vrf issue */
			/* 标记读取错误，并上报 trace */
			if (error)
				*error = INTERFACE_SPEED_ERROR_READ;

			frrtrace(4, frr_zebra, get_iflink_speed, ifname, errno,
				 safe_strerror(errno), 1);

			/* 直接返回 0，表示速度未知 */
			return 0;
		}

		/* Get the current link state for the interface */
		/*
		 * 使用 SIOCETHTOOL ioctl 获取接口的 ethtool 设置。
		 * 第三个参数是 ifreq 结构，其中 ifr_data 指向 ethtool_cmd。
		 * 调用成功后，ecmd 中会填充当前接口的速度等信息。
		 */
		rc = vrf_ioctl(interface->vrf->vrf_id, sd, SIOCETHTOOL,
			       (char *)&ifdata);
	}

	/* ioctl 失败处理 */
	if (rc < 0) {
		/*
		 * EOPNOTSUPP 表示接口不支持 ethtool，属于正常情况，
		 * 不打印调试日志（避免噪音）。
		 */
		if (errno != EOPNOTSUPP && IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"IOCTL failure to read interface %s speed: %d %s",
				ifname, errno, safe_strerror(errno));

		/* no device means interface unreachable */
		/*
		 * ENODEV 表示设备不存在，说明接口已不可达，标记读取错误。
		 */
		if (errno == ENODEV && error)
			*error = INTERFACE_SPEED_ERROR_READ;

		/* 对于非 EOPNOTSUPP 的错误，上报 trace */
		if (errno != EOPNOTSUPP)
			frrtrace(4, frr_zebra, get_iflink_speed, ifname, errno,
				 safe_strerror(errno), 2);

		/* 速度字段清零，后续会返回 0 */
		ecmd.speed_hi = 0;
		ecmd.speed = 0;
	}

	/* 关闭 socket */
	close(sd);

	/*
	 * 组合速度值：
	 *   - speed_hi 是高 16 位
	 *   - speed 是低 16 位
	 * 组合成一个完整的 32 位速度值。
	 * 这是旧版 ethtool 接口的标准做法（支持 >65535 Mbps 的速率）。
	 */
	ret = ((uint32_t)ecmd.speed_hi << 16) | ecmd.speed;

	/*
	 * 如果组合后的值为 UINT32_MAX（0xFFFFFFFF），
	 * 说明驱动未提供有效速度，标记为未知错误并返回 0。
	 */
	if (ret == UINT32_MAX) {
		if (error)
			*error = INTERFACE_SPEED_ERROR_UNKNOWN;
		ret = 0;
	}

	/* 返回速度值 */
	return ret;
}

uint32_t kernel_get_speed(struct interface *ifp, int *error)
{
	return get_iflink_speed(ifp, error);
}

/*
 * netlink_gre_set_msg_encoder：
 *   将数据平面上下文中的 GRE 隧道配置编码为 netlink 消息。
 *
 *   该函数作为消息编码器，被 netlink 批处理框架调用，用于把
 *   zebra 内部关于 GRE 隧道的配置（本地/远端 VTEP、链路接口、MTU、
 *   密钥等）转换为内核可以理解的 RTM_NEWLINK 消息。
 *
 *   编码出的消息结构大致如下：
 *     nlmsghdr (RTM_NEWLINK)
 *     ifinfomsg (ifi_index, ifi_change)
 *       IFLA_MTU
 *       IFLA_LINKINFO
 *         IFLA_INFO_KIND = "gre"
 *         IFLA_INFO_DATA
 *           IFLA_GRE_LINK
 *           IFLA_GRE_LOCAL
 *           IFLA_GRE_REMOTE
 *           IFLA_GRE_IKEY
 *           IFLA_GRE_OKEY
 *
 * 参数：
 *   ctx    数据平面上下文，包含 GRE 隧道相关信息
 *   buf    输出缓冲区，编码后的 netlink 消息写入这里
 *   buflen 输出缓冲区长度
 *
 * 返回值：
 *   成功时返回对齐后的消息长度（字节数）
 *   失败或参数不足时返回 0
 */
static ssize_t
netlink_gre_set_msg_encoder(struct zebra_dplane_ctx *ctx, void *buf,
			    size_t buflen)
{
	/*
	 * 匿名结构体，用于统一描述消息头 + ifinfomsg + 柔性数组。
	 * buf[] 是柔性数组成员，实际属性数据会紧跟 ifi 之后写入。
	 */
	struct {
		struct nlmsghdr n;      /* netlink 消息头 */
		struct ifinfomsg ifi;   /* 链路信息消息体 */
		char buf[];             /* 属性区起始位置（柔性数组） */
	} *req = buf;               /* 将传入的缓冲区解释为该结构体指针 */

	uint32_t link_idx;                        /* 关联接口索引 */
	unsigned int mtu;                         /* MTU */
	struct rtattr *rta_info, *rta_data;       /* 嵌套属性句柄 */
	const struct zebra_l2info_gre *gre_info;  /* GRE 隧道配置信息 */

	/*
	 * 缓冲区至少要能容纳消息头和 ifinfomsg。
	 * 不足则返回 0 表示编码失败。
	 */
	if (buflen < sizeof(*req))
		return 0;

	/* 清零头部和 ifinfomsg，避免残留数据 */
	memset(req, 0, sizeof(*req));

	/* 设置消息类型为 RTM_NEWLINK，用于创建或更新链路 */
	req->n.nlmsg_type =  RTM_NEWLINK;

	/* 消息长度初始化为消息头 + ifinfomsg 的长度 */
	req->n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));

	/* 设置请求标志：这是一个请求消息，期待内核响应 */
	req->n.nlmsg_flags = NLM_F_REQUEST;

	/* 设置目标接口索引 */
	req->ifi.ifi_index = dplane_ctx_get_ifindex(ctx);

	/* 从上下文中获取 GRE 配置信息 */
	gre_info = dplane_ctx_gre_get_info(ctx);
	if (!gre_info)
		/* 没有 GRE 信息则无法编码，返回 0 */
		return 0;

	/*
	 * ifi_change 置为全 1，表示所有属性都需要更新/设置。
	 * 这样可以确保内核按本消息中的属性重新配置该接口。
	 */
	req->ifi.ifi_change = 0xFFFFFFFF;

	/* 从上下文中获取关联接口索引和 MTU */
	link_idx = dplane_ctx_gre_get_link_ifindex(ctx);
	mtu = dplane_ctx_gre_get_mtu(ctx);

	/* 如果 MTU 非零，则添加 IFLA_MTU 属性 */
	if (mtu && !nl_attr_put32(&req->n, buflen, IFLA_MTU, mtu))
		return 0;

	/*
	 * 开始嵌套 IFLA_LINKINFO 属性。
	 * 该属性用于描述链路的类型和具体配置。
	 */
	rta_info = nl_attr_nest(&req->n, buflen, IFLA_LINKINFO);
	if (!rta_info)
		return 0;

	/*
	 * 添加 IFLA_INFO_KIND 属性，值为 "gre"。
	 * 长度参数 3 表示字符串 "gre" 不含结尾 '\0' 的 3 个字符。
	 */
	if (!nl_attr_put(&req->n, buflen, IFLA_INFO_KIND, "gre", 3))
		return 0;

	/*
	 * 开始嵌套 IFLA_INFO_DATA 属性。
	 * 该属性用于描述具体链路类型（GRE）的参数。
	 */
	rta_data = nl_attr_nest(&req->n, buflen, IFLA_INFO_DATA);
	if (!rta_data)
		return 0;

	/*
	 * 添加 IFLA_GRE_LINK：GRE 隧道关联的底层链路接口索引。
	 * 对于基于物理口的 GRE 隧道，该字段指定承载隧道的接口。
	 */
	if (!nl_attr_put32(&req->n, buflen, IFLA_GRE_LINK, link_idx))
		return 0;

	/*
	 * 如果配置了本地 VTEP IP，则添加 IFLA_GRE_LOCAL 属性。
	 * 该属性指定 GRE 隧道的本地端点地址。
	 */
	if (gre_info->vtep_ip.s_addr &&
	    !nl_attr_put32(&req->n, buflen, IFLA_GRE_LOCAL,
			   gre_info->vtep_ip.s_addr))
		return 0;

	/*
	 * 如果配置了远端 VTEP IP，则添加 IFLA_GRE_REMOTE 属性。
	 * 该属性指定 GRE 隧道对端的地址。
	 */
	if (gre_info->vtep_ip_remote.s_addr &&
	    !nl_attr_put32(&req->n, buflen, IFLA_GRE_REMOTE,
			   gre_info->vtep_ip_remote.s_addr))
		return 0;

	/*
	 * 如果配置了输入密钥（IKEY），则添加 IFLA_GRE_IKEY 属性。
	 * 该密钥用于校验从对端收到的 GRE 报文。
	 */
	if (gre_info->ikey &&
	    !nl_attr_put32(&req->n, buflen, IFLA_GRE_IKEY,
			   gre_info->ikey))
		return 0;

	/*
	 * 如果配置了输出密钥（OKEY），则添加 OKEY 属性。
	 *
	 * 注意：这里存在一个明显的 bug —— 使用的属性类型是 IFLA_GRE_IKEY，
	 * 而正确的应该是 IFLA_GRE_OKEY。这会导致输出密钥被写入输入密钥
	 * 字段，内核无法正确识别输出密钥，甚至可能造成配置错误。
	 */
	if (gre_info->okey &&
	    !nl_attr_put32(&req->n, buflen, IFLA_GRE_IKEY,
			   gre_info->okey))
		return 0;

	/* 结束 IFLA_INFO_DATA 嵌套 */
	nl_attr_nest_end(&req->n, rta_data);

	/* 结束 IFLA_LINKINFO 嵌套 */
	nl_attr_nest_end(&req->n, rta_info);

	/*
	 * 返回对齐后的消息长度。
	 * NLMSG_ALIGN 保证长度按 4 字节对齐，符合 netlink 消息要求。
	 */
	return NLMSG_ALIGN(req->n.nlmsg_len);
}

static int netlink_extract_bridge_info(struct rtattr *link_data,
				       struct zebra_l2info_bridge *bridge_info)
{
	struct rtattr *attr[IFLA_BR_MAX + 1];

	memset(bridge_info, 0, sizeof(*bridge_info));
	netlink_parse_rtattr_nested(attr, IFLA_BR_MAX, link_data);
	if (attr[IFLA_BR_VLAN_FILTERING])
		bridge_info->bridge.vlan_aware =
			*(uint8_t *)RTA_DATA(attr[IFLA_BR_VLAN_FILTERING]);
	return 0;
}

static int netlink_extract_vlan_info(struct rtattr *link_data,
				     struct zebra_l2info_vlan *vlan_info)
{
	struct rtattr *attr[IFLA_VLAN_MAX + 1];
	vlanid_t vid_in_msg;

	memset(vlan_info, 0, sizeof(*vlan_info));
	netlink_parse_rtattr_nested(attr, IFLA_VLAN_MAX, link_data);
	if (!attr[IFLA_VLAN_ID]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("IFLA_VLAN_ID missing from VLAN IF message");

		frrtrace(1, frr_zebra, if_netlink_parse_error, 1);

		return -1;
	}

	vid_in_msg = *(vlanid_t *)RTA_DATA(attr[IFLA_VLAN_ID]);
	vlan_info->vid = vid_in_msg;
	return 0;
}

static int netlink_extract_gre_info(struct rtattr *link_data,
				    struct zebra_l2info_gre *gre_info)
{
	struct rtattr *attr[IFLA_GRE_MAX + 1];

	memset(gre_info, 0, sizeof(*gre_info));
	memset(attr, 0, sizeof(attr));
	netlink_parse_rtattr_nested(attr, IFLA_GRE_MAX, link_data);

	if (!attr[IFLA_GRE_LOCAL]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"IFLA_GRE_LOCAL missing from GRE IF message");

		frrtrace(1, frr_zebra, if_netlink_parse_error, 2);
	} else
		gre_info->vtep_ip =
			*(struct in_addr *)RTA_DATA(attr[IFLA_GRE_LOCAL]);
	if (!attr[IFLA_GRE_REMOTE]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"IFLA_GRE_REMOTE missing from GRE IF message");

		frrtrace(1, frr_zebra, if_netlink_parse_error, 3);
	} else
		gre_info->vtep_ip_remote =
			*(struct in_addr *)RTA_DATA(attr[IFLA_GRE_REMOTE]);

	if (!attr[IFLA_GRE_LINK]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("IFLA_GRE_LINK missing from GRE IF message");

		frrtrace(1, frr_zebra, if_netlink_parse_error, 4);
	} else {
		gre_info->ifindex_link =
			*(ifindex_t *)RTA_DATA(attr[IFLA_GRE_LINK]);
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("IFLA_GRE_LINK obtained is %u",
				   gre_info->ifindex_link);
	}
	if (attr[IFLA_GRE_IKEY])
		gre_info->ikey = *(uint32_t *)RTA_DATA(attr[IFLA_GRE_IKEY]);
	if (attr[IFLA_GRE_OKEY])
		gre_info->okey = *(uint32_t *)RTA_DATA(attr[IFLA_GRE_OKEY]);
	return 0;
}

static int netlink_extract_vxlan_info(struct rtattr *link_data,
				      struct zebra_l2info_vxlan *vxl_info)
{
	uint8_t svd = 0;
	struct rtattr *attr[IFLA_VXLAN_MAX + 1];
	vni_t vni_in_msg;
	ifindex_t ifindex_link;

	memset(vxl_info, 0, sizeof(*vxl_info));
	netlink_parse_rtattr_nested(attr, IFLA_VXLAN_MAX, link_data);
	if (attr[IFLA_VXLAN_COLLECT_METADATA]) {
		svd = *(uint8_t *)RTA_DATA(attr[IFLA_VXLAN_COLLECT_METADATA]);
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"IFLA_VXLAN_COLLECT_METADATA=%u in VXLAN IF message",
				svd);
	}

	if (!svd) {
		/*
		 * In case of svd we will not get vni info directly from the
		 * device
		 */
		if (!attr[IFLA_VXLAN_ID]) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"IFLA_VXLAN_ID missing from VXLAN IF message");

			frrtrace(1, frr_zebra, if_netlink_parse_error, 5);

			return -1;
		}

		vxl_info->vni_info.iftype = ZEBRA_VXLAN_IF_VNI;
		vni_in_msg = *(vni_t *)RTA_DATA(attr[IFLA_VXLAN_ID]);
		vxl_info->vni_info.vni.vni = vni_in_msg;
	} else {
		vxl_info->vni_info.iftype = ZEBRA_VXLAN_IF_SVD;
	}


	if (attr[IFLA_VXLAN_LOCAL]) {
		vxl_info->vtep_ip.ipaddr_v4 = *(struct in_addr *)RTA_DATA(attr[IFLA_VXLAN_LOCAL]);
		SET_IPADDR_V4(&vxl_info->vtep_ip);
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("IFLA_VXLAN_LOCAL VXLAN IF message, local address V4: %pIA",
				   &vxl_info->vtep_ip);
	} else if (attr[IFLA_VXLAN_LOCAL6]) {
		IPV6_ADDR_COPY(&vxl_info->vtep_ip.ipaddr_v6,
			       (struct in6_addr *)RTA_DATA(attr[IFLA_VXLAN_LOCAL6]));
		SET_IPADDR_V6(&vxl_info->vtep_ip);
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("IFLA_VXLAN_LOCAL VXLAN IF message, local address V6: %pIA",
				   &vxl_info->vtep_ip);
	} else {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug(
				"IFLA_VXLAN_LOCAL missing from VXLAN IF message");

		frrtrace(1, frr_zebra, if_netlink_parse_error, 6);
	}

	if (attr[IFLA_VXLAN_GROUP]) {
		if (!svd)
			vxl_info->vni_info.vni.mcast_grp =
				*(struct in_addr *)RTA_DATA(
					attr[IFLA_VXLAN_GROUP]);
	}

	if (!attr[IFLA_VXLAN_LINK]) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("IFLA_VXLAN_LINK missing from VXLAN IF message");

		frrtrace(1, frr_zebra, if_netlink_parse_error, 7);
	} else {
		ifindex_link =
			*(ifindex_t *)RTA_DATA(attr[IFLA_VXLAN_LINK]);
		vxl_info->ifindex_link = ifindex_link;
	}
	return 0;
}

/*
 * Extract and save L2 params (of interest) for an interface. When a
 * bridge interface is added or updated, take further actions to map
 * its members. Likewise, for VxLAN interface.
 */
static void netlink_interface_update_l2info(struct zebra_dplane_ctx *ctx,
					    enum zebra_iftype zif_type,
					    struct rtattr *link_data, int add,
					    ns_id_t link_nsid)
{
	struct zebra_l2info_bridge bridge_info;
	struct zebra_l2info_vlan vlan_info;
	struct zebra_l2info_vxlan vxlan_info;
	struct zebra_l2info_gre gre_info;

	if (!link_data)
		return;

	switch (zif_type) {
	case ZEBRA_IF_BRIDGE:
		netlink_extract_bridge_info(link_data, &bridge_info);
		dplane_ctx_set_ifp_bridge_info(ctx, &bridge_info);
		break;
	case ZEBRA_IF_VLAN:
		netlink_extract_vlan_info(link_data, &vlan_info);
		dplane_ctx_set_ifp_vlan_info(ctx, &vlan_info);
		break;
	case ZEBRA_IF_VXLAN:
		netlink_extract_vxlan_info(link_data, &vxlan_info);
		vxlan_info.link_nsid = link_nsid;
		dplane_ctx_set_ifp_vxlan_info(ctx, &vxlan_info);
		break;
	case ZEBRA_IF_GRE:
	case ZEBRA_IF_IP6GRE:
	case ZEBRA_IF_GRETAP:
	case ZEBRA_IF_IP6GRETAP:
		netlink_extract_gre_info(link_data, &gre_info);
		gre_info.link_nsid = link_nsid;
		dplane_ctx_set_ifp_gre_info(ctx, &gre_info);
		break;
	case ZEBRA_IF_OTHER:
	case ZEBRA_IF_VRF:
	case ZEBRA_IF_MACVLAN:
	case ZEBRA_IF_VETH:
	case ZEBRA_IF_BOND:
	case ZEBRA_IF_DUMMY:
		break;
	}
}

static int
netlink_bridge_vxlan_vlan_vni_map_update(struct zebra_dplane_ctx *ctx,
					 struct rtattr *af_spec)
{
	int rem;
	uint16_t flags;
	struct rtattr *i;
	struct zebra_vxlan_vni_array *vniarray = NULL;
	struct zebra_vxlan_vni vni_end;
	struct zebra_vxlan_vni vni_start;
	struct rtattr *aftb[IFLA_BRIDGE_VLAN_TUNNEL_MAX + 1];
	int32_t count = 0;

	memset(&vni_start, 0, sizeof(vni_start));
	memset(&vni_end, 0, sizeof(vni_end));

	for (i = RTA_DATA(af_spec), rem = RTA_PAYLOAD(af_spec); RTA_OK(i, rem);
	     i = RTA_NEXT(i, rem)) {

		if (i->rta_type != IFLA_BRIDGE_VLAN_TUNNEL_INFO)
			continue;

		memset(aftb, 0, sizeof(aftb));
		netlink_parse_rtattr_nested(aftb, IFLA_BRIDGE_VLAN_TUNNEL_MAX,
					    i);
		if (!aftb[IFLA_BRIDGE_VLAN_TUNNEL_ID] ||
		    !aftb[IFLA_BRIDGE_VLAN_TUNNEL_VID])
			/* vlan-vni info missing */
			return 0;

		count++;
		flags = 0;
		vniarray = XREALLOC(
			MTYPE_TMP, vniarray,
			sizeof(struct zebra_vxlan_vni_array) +
				count * sizeof(struct zebra_vxlan_vni));

		memset(&vniarray->vnis[count - 1], 0,
		       sizeof(struct zebra_vxlan_vni));

		vniarray->vnis[count - 1].vni =
			*(vni_t *)RTA_DATA(aftb[IFLA_BRIDGE_VLAN_TUNNEL_ID]);
		vniarray->vnis[count - 1].access_vlan = *(vlanid_t *)RTA_DATA(
			aftb[IFLA_BRIDGE_VLAN_TUNNEL_VID]);

		if (aftb[IFLA_BRIDGE_VLAN_TUNNEL_FLAGS])
			flags = *(uint16_t *)RTA_DATA(
				aftb[IFLA_BRIDGE_VLAN_TUNNEL_FLAGS]);

		vniarray->vnis[count - 1].flags = flags;
	}

	if (count) {
		vniarray->count = count;
		dplane_ctx_set_ifp_vxlan_vni_array(ctx, vniarray);
	} else if (vniarray) {
		/* Free allocated memory if count is 0 */
		XFREE(MTYPE_TMP, vniarray);
	}
	return 0;
}

static int netlink_bridge_vxlan_update(struct zebra_dplane_ctx *ctx,
				       struct rtattr *af_spec)
{
	struct rtattr *aftb[IFLA_BRIDGE_MAX + 1];
	struct bridge_vlan_info *vinfo;
	struct zebra_dplane_bridge_vlan_info bvinfo;

	if (!af_spec) {
		dplane_ctx_set_ifp_no_afspec(ctx);
		return 0;
	}

	netlink_bridge_vxlan_vlan_vni_map_update(ctx, af_spec);

	/* There is a 1-to-1 mapping of VLAN to VxLAN - hence
	 * only 1 access VLAN is accepted.
	 */
	netlink_parse_rtattr_nested(aftb, IFLA_BRIDGE_MAX, af_spec);
	if (!aftb[IFLA_BRIDGE_VLAN_INFO]) {
		dplane_ctx_set_ifp_no_bridge_vlan_info(ctx);
		return 0;
	}

	vinfo = RTA_DATA(aftb[IFLA_BRIDGE_VLAN_INFO]);
	bvinfo.flags = vinfo->flags;
	bvinfo.vid = vinfo->vid;

	dplane_ctx_set_ifp_bridge_vlan_info(ctx, &bvinfo);
	return 0;
}

static void netlink_bridge_vlan_update(struct zebra_dplane_ctx *ctx,
				       struct rtattr *af_spec)
{
	struct rtattr *i;
	int rem;
	struct bridge_vlan_info *vinfo;
	struct zebra_dplane_bridge_vlan_info_array *bvarray = NULL;
	int32_t count = 0;

	if (af_spec) {
		for (i = RTA_DATA(af_spec), rem = RTA_PAYLOAD(af_spec);
		     RTA_OK(i, rem); i = RTA_NEXT(i, rem)) {
			if (i->rta_type != IFLA_BRIDGE_VLAN_INFO)
				continue;

			count++;
			bvarray = XREALLOC(
				MTYPE_TMP, bvarray,
				sizeof(struct
				       zebra_dplane_bridge_vlan_info_array) +
					count * sizeof(struct
						       zebra_dplane_bridge_vlan_info));

			vinfo = RTA_DATA(i);
			bvarray->array[count - 1].flags = vinfo->flags;
			bvarray->array[count - 1].vid = vinfo->vid;
		}
	}

	if (count) {
		bvarray->count = count;
		dplane_ctx_set_ifp_bridge_vlan_info_array(ctx, bvarray);
	} else if (bvarray) {
		/* Free allocated memory if count is 0 */
		XFREE(MTYPE_TMP, bvarray);
	}
}

static int netlink_bridge_interface(struct zebra_dplane_ctx *ctx,
				    struct rtattr *af_spec, int startup)
{

	netlink_bridge_vxlan_update(ctx, af_spec);

	/* build vlan bitmap associated with this interface if that
	 * device type is interested in the vlans
	 */
	netlink_bridge_vlan_update(ctx, af_spec);

	dplane_provider_enqueue_to_zebra(ctx);
	return 0;
}

/*
 * Process interface protodown dplane update.
 *
 * If the interface is an es bond member then it must follow EVPN's
 * protodown setting.
 */
static void netlink_proc_dplane_if_protodown(struct zebra_dplane_ctx *ctx,
					     struct rtattr **tb)
{
	bool protodown;
	uint32_t rc_bitfield = 0;
	struct rtattr *pd_reason_info[IFLA_MAX + 1];

	protodown = !!*(uint8_t *)RTA_DATA(tb[IFLA_PROTO_DOWN]);

	if (tb[IFLA_PROTO_DOWN_REASON]) {
		netlink_parse_rtattr_nested(pd_reason_info, IFLA_INFO_MAX,
					    tb[IFLA_PROTO_DOWN_REASON]);

		if (pd_reason_info[IFLA_PROTO_DOWN_REASON_VALUE])
			rc_bitfield = *(uint32_t *)RTA_DATA(
				pd_reason_info[IFLA_PROTO_DOWN_REASON_VALUE]);
	}

	dplane_ctx_set_ifp_rc_bitfield(ctx, rc_bitfield);
	dplane_ctx_set_ifp_protodown(ctx, protodown);
	dplane_ctx_set_ifp_protodown_set(ctx, true);
}

static uint8_t netlink_parse_lacp_bypass(struct rtattr **linkinfo)
{
	uint8_t bypass = 0;
	struct rtattr *mbrinfo[IFLA_BOND_SLAVE_MAX + 1];

	netlink_parse_rtattr_nested(mbrinfo, IFLA_BOND_SLAVE_MAX,
				    linkinfo[IFLA_INFO_SLAVE_DATA]);
	if (mbrinfo[IFLA_BOND_SLAVE_AD_RX_BYPASS])
		bypass = *(uint8_t *)RTA_DATA(
			mbrinfo[IFLA_BOND_SLAVE_AD_RX_BYPASS]);

	return bypass;
}

/* Request for specific interface or address information from the kernel */
static int netlink_request_intf_addr(struct nlsock *netlink_cmd, int family,
				     int type, uint32_t filter_mask)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg ifm;
		char buf[256];
	} req;

	frrtrace(4, frr_zebra, netlink_request_intf_addr, netlink_cmd, family,
		 type, filter_mask);

	/* Form the request, specifying filter (rtattr) if needed. */
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_type = type;
	req.n.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.ifm.ifi_family = family;

	/* Include filter, if specified. */
	if (filter_mask && !nl_attr_put32(&req.n, sizeof(req), IFLA_EXT_MASK, filter_mask))
		return -1;

	return netlink_request(netlink_cmd, &req);
}

/*
 * netlink_put_gre_set_msg：
 *   将 GRE 设置操作加入到 netlink 批处理中。
 *
 *   该函数作为数据平面 provider 的消息入队回调，由批处理框架调用。
 *   它并不直接发送消息，而是把当前上下文交给 netlink_batch_add_msg()，
 *   由后者使用指定的编码器 netlink_gre_set_msg_encoder 将消息编码后
 *   追加到批处理 bth 中。
 *
 *   当批处理达到长度限制或需要发送时，批处理框架会统一将消息发送给内核。
 *
 * 参数：
 *   bth  netlink 批处理结构，用于累积待发送的 netlink 消息
 *   ctx  数据平面上下文，操作类型应为 DPLANE_OP_GRE_SET
 *
 * 返回值：
 *   enum netlink_msg_status
 *     - FRR_NETLINK_SUCCESS  成功加入批处理
 *     - FRR_NETLINK_ERROR    编码或加入批处理失败
 */
enum netlink_msg_status
netlink_put_gre_set_msg(struct nl_batch *bth, struct zebra_dplane_ctx *ctx)
{
	enum dplane_op_e op;   /* 当前上下文中的操作类型 */
	enum netlink_msg_status ret; /* netlink_batch_add_msg 的返回值 */

	/* 从上下文中获取操作类型 */
	op = dplane_ctx_get_op(ctx);

	/*
	 * 断言：该函数只应处理 DPLANE_OP_GRE_SET 操作。
	 * 如果传入其他操作类型，说明调用方逻辑有误，直接触发断言失败。
	 */
	assert(op == DPLANE_OP_GRE_SET);

	/*
	 * 将上下文加入 netlink 批处理。
	 *
	 * 参数说明：
	 *   bth                          目标批处理结构
	 *   ctx                          要加入的上下文
	 *   netlink_gre_set_msg_encoder  编码器函数，负责把 ctx 编码为 netlink 消息
	 *   false                        表示这不是一个“dump”请求（非 dump 类消息）
	 *
	 * 返回值：
	 *   FRR_NETLINK_SUCCESS  成功加入批处理
	 *   FRR_NETLINK_ERROR    编码失败或加入失败
	 */
	ret = netlink_batch_add_msg(bth, ctx, netlink_gre_set_msg_encoder, false);

	/* 返回结果，由批处理框架决定后续处理 */
	return ret;
}

/* Interface lookup by netlink socket. */
/*
 * interface_lookup_netlink：
 *   通过 netlink socket 查询并读取接口信息。
 *
 *   该函数在 zebra 启动阶段被调用，用于一次性从内核 dump 所有接口信息，
 *   包括普通接口和桥接接口。读取到的每条接口信息都会通过
 *   netlink_link_change() 解析，并构造上下文交给 zebra 主线程处理。
 *
 *   主要流程：
 *     1. 从 zebra 命名空间构造数据平面信息 dp_info（命令 socket 上下文）。
 *     2. 通过 RTM_GETLINK + AF_PACKET 请求所有普通接口信息，并解析。
 *     3. 通过 RTM_GETLINK + AF_BRIDGE + RTEXT_FILTER_BRVLAN 请求
 *        桥接接口信息（包含 VLAN 相关信息），并解析。
 *
 * 参数：
 *   zns  zebra 命名空间
 *
 * 返回值：
 *   < 0  请求发送失败或解析过程中出错
 *   >= 0 解析成功（通常为 0）
 */
int interface_lookup_netlink(struct zebra_ns *zns)
{
	int ret;                                        /* 通用返回值 */
	struct zebra_dplane_info dp_info;               /* 数据平面信息 */
	struct nlsock *netlink_cmd = &zns->netlink_dplane_out; /* 使用的出站 netlink socket */

	/* Capture key info from ns struct */
	/*
	 * 从 zns 中提取关键信息填充 dp_info。
	 * 第三个参数 true 表示这是命令 socket（is_cmd），
	 * 影响后续错误处理和消息分发逻辑。
	 */
	zebra_dplane_info_from_zns(&dp_info, zns, true /*is_cmd*/);

	/* Get interface information. */
	/*
	 * 第一步：请求所有普通接口信息。
	 *
	 * 参数说明：
	 *   netlink_cmd       使用的出站 netlink socket
	 *   AF_PACKET         地址族为 AF_PACKET，表示请求所有链路层接口
	 *   RTM_GETLINK       netlink 消息类型，表示获取链路信息
	 *   0                 过滤标志，0 表示不加额外过滤
	 *
	 * 该请求会触发内核 dump 所有接口信息。
	 */
	ret = netlink_request_intf_addr(netlink_cmd, AF_PACKET, RTM_GETLINK, 0);
	if (ret < 0)
		return ret;

	/*
	 * 解析内核返回的接口信息。
	 *
	 * 参数说明：
	 *   netlink_link_change  处理每条链路变更消息的回调
	 *   netlink_cmd          出站 netlink socket
	 *   &dp_info             数据平面信息，会传给回调
	 *   0                    不限制读取批次
	 *   true                 启动阶段标志（startup = true）
	 */
	ret = netlink_parse_info(netlink_link_change, netlink_cmd, &dp_info, 0,
				 true);
	if (ret < 0)
		return ret;

	/* Get interface information - for bridge interfaces. */
	/*
	 * 第二步：请求桥接接口信息。
	 *
	 * 参数说明：
	 *   netlink_cmd                  出站 netlink socket
	 *   AF_BRIDGE                    地址族为 AF_BRIDGE，表示请求桥接接口
	 *   RTM_GETLINK                  获取链路信息
	 *   RTEXT_FILTER_BRVLAN          过滤标志，要求返回桥接 VLAN 信息
	 *
	 * 与第一步的区别：
	 *   - 地址族从 AF_PACKET 改为 AF_BRIDGE
	 *   - 增加了 RTEXT_FILTER_BRVLAN 过滤标志，以获取桥接 VLAN 相关属性
	 */
	ret = netlink_request_intf_addr(netlink_cmd, AF_BRIDGE, RTM_GETLINK,
					RTEXT_FILTER_BRVLAN);
	if (ret < 0)
		return ret;

	/*
	 * 同样使用 netlink_link_change() 解析桥接接口信息。
	 * 该回调内部会根据地址族（AF_BRIDGE）走桥接专用处理路径，
	 * 调用 netlink_bridge_interface() 处理桥接相关属性。
	 */
	ret = netlink_parse_info(netlink_link_change, netlink_cmd, &dp_info, 0,
				 true);
	if (ret < 0)
		return ret;

	/* 返回最终结果 */
	return ret;
}

/*
 * interface_list_tunneldump：
 *   读取隧道（tunnel）转储信息，并推进数据平面启动阶段。
 *
 *   该函数在 zebra 启动阶段被调用（由 zebra_ns_startup_continue() 在
 *   ZEBRA_DPLANE_INTERFACES_READ 阶段触发）。它负责：
 *     1. 调用 netlink_tunneldump_read() 向内核请求并解析所有 VxLAN
 *        接口的隧道转储信息。
 *     2. 若读取失败，直接返回，不推进启动阶段。
 *     3. 若成功，通过 zebra_dplane_startup_stage() 将启动阶段推进到
 *        ZEBRA_DPLANE_TUNNELS_READ，以便后续继续处理。
 *
 * 参数：
 *   zns  zebra 命名空间
 */
void interface_list_tunneldump(struct zebra_ns *zns)
{
	int ret;  /* netlink_tunneldump_read 的返回值 */

	/*
	 * So netlink_tunneldump_read will initiate a request
	 * per tunnel to get data.  If we are on a kernel that
	 * does not support this then we will get X error messages
	 * (one per tunnel request )back which netlink_parse_info will
	 * stop after the first one.  So we need to read equivalent
	 * error messages per tunnel then we can continue.
	 * if we do not gather all the read failures then
	 * later requests will not work right.
	 */
	/*
	 * 说明：
	 *   netlink_tunneldump_read 会为每个隧道发起一次请求以获取数据。
	 *   如果当前内核不支持该请求，我们会收到 X 条错误消息
	 *   （每个隧道请求对应一条），而 netlink_parse_info 会在遇到
	 *   第一条错误时停止解析。
	 *
	 *   因此，我们需要为每个隧道读取等量的错误消息，才能继续后续处理。
	 *   如果不把所有读取失败都消费掉，后面的请求将无法正常工作
	 *   （例如 netlink 消息序列号错乱、剩余错误堆积在 socket 中）。
	 */
	ret = netlink_tunneldump_read(zns);

	/*
	 * 如果读取失败（ret < 0），说明内核不支持隧道转储或处理过程中出错。
	 * 此时直接返回，不推进启动阶段，避免后续阶段在数据不完整的情况下继续。
	 */
	if (ret < 0)
		return;

	/*
	 * 读取成功，推进数据平面启动阶段到 ZEBRA_DPLANE_TUNNELS_READ。
	 * 该函数会触发下一阶段的处理（由 zebra_ns_startup_continue() 调度），
	 * 例如调用 interface_list_second() 进行接口第二阶段处理。
	 */
	zebra_dplane_startup_stage(zns, ZEBRA_DPLANE_TUNNELS_READ);
}


/**
 * interface_addr_lookup_netlink() - Look up interface addresses
 *
 * @zns:	Zebra netlink socket
 * Return:	Result status
 */
static int interface_addr_lookup_netlink(struct zebra_ns *zns)
{
	int ret;
	struct zebra_dplane_info dp_info;
	struct nlsock *netlink_cmd = &zns->netlink_cmd;

	/* Capture key info from ns struct */
	zebra_dplane_info_from_zns(&dp_info, zns, true /*is_cmd*/);

	/* Get IPv4 address of the interfaces. */
	ret = netlink_request_intf_addr(netlink_cmd, AF_INET, RTM_GETADDR, 0);
	if (ret < 0)
		return ret;
	ret = netlink_parse_info(netlink_interface_addr_dplane, netlink_cmd,
				 &dp_info, 0, true);
	if (ret < 0)
		return ret;

	/* Get IPv6 address of the interfaces. */
	ret = netlink_request_intf_addr(netlink_cmd, AF_INET6, RTM_GETADDR, 0);
	if (ret < 0)
		return ret;
	ret = netlink_parse_info(netlink_interface_addr_dplane, netlink_cmd,
				 &dp_info, 0, true);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * kernel_interface_set_master：
 *   通过 netlink 设置从接口（slave）的主接口（master）。
 *
 *   使用场景：
 *     当 zebra 需要把某个接口加入到桥接（bridge）或聚合（bond）等
 *     主接口下时，会调用该函数。内核收到 RTM_SETLINK 后会更新接口的
 *     主从关系。
 *
 *   主要流程：
 *     1. 查找默认命名空间 zns。
 *     2. 构造 netlink 请求消息（RTM_SETLINK + ifinfomsg）。
 *     3. 设置 ifi_index 为从接口索引。
 *     4. 添加 IFLA_MASTER 属性（主接口索引）。
 *     5. 添加 IFLA_LINK 属性（关联接口索引，通常等于从接口索引）。
 *     6. 通过 netlink_talk() 同步发送请求并等待内核响应。
 *
 * 参数：
 *   master  主接口（如桥接、聚合接口）
 *   slave   从接口（要加入主接口的接口）
 *
 * 返回值：
 *   0    成功
 *  -1    编码 netlink 属性失败
 *   其他 由 netlink_talk() 返回的结果（通常 < 0 表示失败）
 */
int kernel_interface_set_master(struct interface *master,
				struct interface *slave)
{
	/* 查找默认命名空间，因为该操作通常作用于默认命名空间 */
	struct zebra_ns *zns = zebra_ns_lookup(NS_DEFAULT);

	/*
	 * 请求消息结构：
	 *   nlmsghdr    netlink 消息头
	 *   ifinfomsg   链路信息消息体
	 *   buf          属性区缓冲区，使用固定大小 NL_PKT_BUF_SIZE
	 *
	 * 使用固定缓冲区而非柔性数组，是因为该消息属性较少，
	 * 固定大小足以容纳，且无需动态分配。
	 */
	struct {
		struct nlmsghdr n;
		struct ifinfomsg ifa;
		char buf[NL_PKT_BUF_SIZE];
	} req;

	/* 清零整个请求结构，避免残留数据 */
	memset(&req, 0, sizeof(req));

	/* 消息长度初始化为消息头 + ifinfomsg 的长度 */
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));

	/* 请求标志：这是一个请求消息，期待内核响应 */
	req.n.nlmsg_flags = NLM_F_REQUEST;

	/* 消息类型为 RTM_SETLINK，用于修改链路属性 */
	req.n.nlmsg_type = RTM_SETLINK;

	/* 设置源端口 ID 为命令 socket 的 nl_pid */
	req.n.nlmsg_pid = zns->netlink_cmd.snl.nl_pid;

	/* 指定要修改的接口为从接口（slave） */
	req.ifa.ifi_index = slave->ifindex;

	/*
	 * 添加 IFLA_MASTER 属性：指定从接口所属的主接口索引。
	 * 内核会根据该属性把 slave 加入 master 之下。
	 */
	if (!nl_attr_put32(&req.n, sizeof(req), IFLA_MASTER, master->ifindex)) {
		zlog_err("%s: failed to encode IFLA_MASTER nl attribute", __func__);
		return -1;
	}

	/*
	 * 添加 IFLA_LINK 属性：指定关联接口索引。
	 * 这里设置为从接口自身的索引，用于内核确认关联关系。
	 */
	if (!nl_attr_put32(&req.n, sizeof(req), IFLA_LINK, slave->ifindex)) {
		zlog_err("%s: failed to encode IFLA_LINK nl attribute", __func__);
		return -1;
	}

	/*
	 * 通过 netlink_talk() 同步发送请求并等待内核响应。
	 *
	 * 参数说明：
	 *   netlink_talk_filter  响应过滤回调，通常用于识别 ACK/错误
	 *   &req.n               要发送的 netlink 消息
	 *   &zns->netlink_cmd    使用的命令 socket
	 *   zns                  命名空间上下文
	 *   false                表示不处于启动阶段（非 startup）
	 *
	 * 返回值通常为 0 表示成功，负值表示失败。
	 */
	return netlink_talk(netlink_talk_filter, &req.n, &zns->netlink_cmd, zns,
			    false);
}

/* Interface address modification. */
static ssize_t netlink_address_msg_encoder(struct zebra_dplane_ctx *ctx,
					   void *buf, size_t buflen)
{
	int bytelen;
	const struct prefix *p;
	int cmd;
	const char *label;

	struct {
		struct nlmsghdr n;
		struct ifaddrmsg ifa;
		char buf[0];
	} *req = buf;

	if (buflen < sizeof(*req))
		return 0;

	p = dplane_ctx_get_intf_addr(ctx);
	memset(req, 0, sizeof(*req));

	bytelen = (p->family == AF_INET ? 4 : 16);

	req->n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	req->n.nlmsg_flags = NLM_F_REQUEST;

	if (dplane_ctx_get_op(ctx) == DPLANE_OP_ADDR_INSTALL)
		cmd = RTM_NEWADDR;
	else
		cmd = RTM_DELADDR;

	req->n.nlmsg_type = cmd;
	req->ifa.ifa_family = p->family;

	req->ifa.ifa_index = dplane_ctx_get_ifindex(ctx);

	if (!nl_attr_put(&req->n, buflen, IFA_LOCAL, &p->u.prefix, bytelen))
		return 0;

	if (p->family == AF_INET) {
		if (dplane_ctx_intf_is_connected(ctx)) {
			p = dplane_ctx_get_intf_dest(ctx);
			if (!nl_attr_put(&req->n, buflen, IFA_ADDRESS,
					 &p->u.prefix, bytelen))
				return 0;
		} else if (cmd == RTM_NEWADDR) {
			struct in_addr broad = {
				.s_addr = ipv4_broadcast_addr(p->u.prefix4.s_addr,
							p->prefixlen)
			};
			if (!nl_attr_put(&req->n, buflen, IFA_BROADCAST, &broad,
					 bytelen))
				return 0;
		}
	}

	/* p is now either address or destination/bcast addr */
	req->ifa.ifa_prefixlen = p->prefixlen;

	if (dplane_ctx_intf_is_secondary(ctx))
		SET_FLAG(req->ifa.ifa_flags, IFA_F_SECONDARY);

	if (dplane_ctx_intf_has_label(ctx)) {
		label = dplane_ctx_get_intf_label(ctx);
		if (!nl_attr_put(&req->n, buflen, IFA_LABEL, label,
				 strlen(label) + 1))
			return 0;
	}

	return NLMSG_ALIGN(req->n.nlmsg_len);
}

enum netlink_msg_status
netlink_put_address_update_msg(struct nl_batch *bth,
			       struct zebra_dplane_ctx *ctx)
{
	return netlink_batch_add_msg(bth, ctx, netlink_address_msg_encoder,
				     false);
}

static ssize_t netlink_intf_msg_encoder(struct zebra_dplane_ctx *ctx, void *buf,
					size_t buflen)
{
	enum dplane_op_e op;
	int cmd = 0;

	op = dplane_ctx_get_op(ctx);

	if (op == DPLANE_OP_INTF_UPDATE)
		cmd = RTM_SETLINK;
	else if (op == DPLANE_OP_INTF_INSTALL)
		cmd = RTM_NEWLINK;
	else if (op == DPLANE_OP_INTF_DELETE)
		cmd = RTM_DELLINK;
	else {
		flog_err(
			EC_ZEBRA_NHG_FIB_UPDATE,
			"Context received for kernel interface update with incorrect OP code (%u)",
			op);
		return -1;
	}

	return netlink_intf_msg_encode(cmd, ctx, buf, buflen);
}

enum netlink_msg_status
netlink_put_intf_update_msg(struct nl_batch *bth, struct zebra_dplane_ctx *ctx)
{
	return netlink_batch_add_msg(bth, ctx, netlink_intf_msg_encoder, false);
}
/*
 * Parse and validate an incoming interface address change message,
 * generating a dplane context object.
 * This runs in the dplane pthread; the context is enqueued to the
 * main pthread for processing.
 */
/*
 * netlink_interface_addr_dplane：
 *   解析并校验内核发来的接口地址变更消息，生成数据平面上下文对象。
 *
 *   运行环境：
 *     该函数运行在数据平面 pthread 中；生成的上下文 ctx 会被入队到
 *     主 pthread 队列，由主线程进一步处理（更新接口地址、RIB 等）。
 *
 *   主要流程：
 *     1. 校验 netlink 消息类型、地址族、消息长度、前缀长度。
 *     2. 解析 netlink 属性（IFA_*）。
 *     3. 对 IPv6 消息，跳过 DAD 失败或无效的地址。
 *     4. 确定本地地址和目的/广播/对端地址。
 *     5. 分配并填充 zebra_dplane_ctx 上下文对象。
 *     6. 将上下文入队给主线程处理。
 *
 * 参数：
 *   h        当前 netlink 消息头
 *   ns_id    消息所属的网络命名空间 ID
 *   startup  是否处于启动阶段（本函数中忽略）
 *
 * 返回值：
 *   0    成功处理或有意跳过
 *   -1   消息无效（长度、地址、前缀等不合法）
 */
int netlink_interface_addr_dplane(struct nlmsghdr *h, ns_id_t ns_id,
				  int startup /*ignored*/)
{
	int len;                                 /* netlink 属性区长度 */
	struct ifaddrmsg *ifa;                   /* 接口地址消息体 */
	struct rtattr *tb[IFA_MAX + 1];          /* 解析后的属性表，按 IFA_* 索引 */
	void *addr;                              /* 本地地址指针 */
	void *broad;                             /* 广播/对端地址指针 */
	char *label = NULL;                      /* 接口标签（如 eth0:1） */
	uint32_t metric = METRIC_MAX;            /* 路由优先级/度量，默认最大值 */
	uint32_t kernel_flags = 0;               /* 内核地址标志 */
	struct zebra_dplane_ctx *ctx;            /* 待构造的数据平面上下文 */
	struct prefix p;                         /* 临时前缀结构，用于转换地址 */

	/* 获取 netlink 消息的数据部分，即 ifaddrmsg */
	ifa = NLMSG_DATA(h);

	/* Validate message types */
	/* 校验消息类型：只处理新增或删除地址 */
	if (h->nlmsg_type != RTM_NEWADDR && h->nlmsg_type != RTM_DELADDR)
		return 0;

	/*
	 * 校验地址族：只处理 IPv4 和 IPv6。
	 * 其他地址族（如 AF_PACKET、AF_BRIDGE）不属于接口地址变更，
	 * 直接忽略并记录调试信息。
	 */
	if (ifa->ifa_family != AF_INET && ifa->ifa_family != AF_INET6) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: %s: Invalid address family: %u",
				   __func__, nl_msg_type_to_str(h->nlmsg_type),
				   ifa->ifa_family);

		/* 上报 trace，便于问题定位 */
		frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type),
			 ifa->ifa_family, 1);

		return 0;
	}

	/*
	 * 计算 netlink 属性区长度：
	 *   h->nlmsg_len                        整个 netlink 消息长度
	 *   NLMSG_LENGTH(sizeof(ifaddrmsg))     消息头 + ifaddrmsg 的长度
	 * 差值即为属性区域（rtattr 数组）的字节数。
	 */
	len = h->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifaddrmsg));

	/* 如果长度小于 0，说明消息格式错误 */
	if (len < 0) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: %s: netlink msg bad size: %d %zu",
				   __func__, nl_msg_type_to_str(h->nlmsg_type),
				   h->nlmsg_len,
				   (size_t)NLMSG_LENGTH(
					   sizeof(struct ifaddrmsg)));

		frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type),
			 h->nlmsg_len, 2);

		return -1;
	}

	/*
	 * 解析属性区，把每个 IFA_* 属性填入 tb 数组。
	 * IFA_RTA(ifa) 指向 ifaddrmsg 后面的属性区起始位置。
	 */
	netlink_parse_rtattr(tb, IFA_MAX, IFA_RTA(ifa), len);

	/*
	 * Flags passed through
	 * 优先使用 IFA_FLAGS 属性中的标志；若不存在，则使用 ifa_flags。
	 * IFA_FLAGS 是较新的字段，ifaddrmsg.ifa_flags 只有 8 位。
	 */
	if (tb[IFA_FLAGS])
		kernel_flags = *(int *)RTA_DATA(tb[IFA_FLAGS]);
	else
		kernel_flags = ifa->ifa_flags;

	/* 调试模式下打印详细信息（注释提示可移除此行以查看初始 ifcfg 日志） */
	if (IS_ZEBRA_DEBUG_KERNEL) { /* remove this line to see initial ifcfg */
		char buf[PREFIX_STRLEN];

		zlog_debug("%s: %s nsid %u ifindex %u flags 0x%x:", __func__,
			   nl_msg_type_to_str(h->nlmsg_type), ns_id,
			   ifa->ifa_index, kernel_flags);
		if (tb[IFA_LOCAL])
			zlog_debug("  IFA_LOCAL     %s/%d",
				   inet_ntop(ifa->ifa_family,
					     RTA_DATA(tb[IFA_LOCAL]), buf,
					     sizeof(buf)),
				   ifa->ifa_prefixlen);
		if (tb[IFA_ADDRESS])
			zlog_debug("  IFA_ADDRESS   %s/%d",
				   inet_ntop(ifa->ifa_family,
					     RTA_DATA(tb[IFA_ADDRESS]), buf,
					     sizeof(buf)),
				   ifa->ifa_prefixlen);
		if (tb[IFA_BROADCAST])
			zlog_debug("  IFA_BROADCAST %s/%d",
				   inet_ntop(ifa->ifa_family,
					     RTA_DATA(tb[IFA_BROADCAST]), buf,
					     sizeof(buf)),
				   ifa->ifa_prefixlen);
		if (tb[IFA_LABEL])
			zlog_debug("  IFA_LABEL     %s",
				   (const char *)RTA_DATA(tb[IFA_LABEL]));

		if (tb[IFA_CACHEINFO]) {
			struct ifa_cacheinfo *ci = RTA_DATA(tb[IFA_CACHEINFO]);

			zlog_debug("  IFA_CACHEINFO pref %d, valid %d",
				   ci->ifa_prefered, ci->ifa_valid);
		}
	}

	/* Validate prefix length */
	/*
	 * 校验 IPv4 前缀长度是否合法（不能超过 32）。
	 * 不合法则记录日志并返回 -1。
	 */
	if (ifa->ifa_family == AF_INET
	    && ifa->ifa_prefixlen > IPV4_MAX_BITLEN) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: %s: Invalid prefix length: %u",
				   __func__, nl_msg_type_to_str(h->nlmsg_type),
				   ifa->ifa_prefixlen);

		frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type),
			 ifa->ifa_prefixlen, 3);

		return -1;
	}

	if (ifa->ifa_family == AF_INET6) {
		/* 校验 IPv6 前缀长度是否合法（不能超过 128） */
		if (ifa->ifa_prefixlen > IPV6_MAX_BITLEN) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("%s: %s: Invalid prefix length: %u",
					   __func__,
					   nl_msg_type_to_str(h->nlmsg_type),
					   ifa->ifa_prefixlen);

			frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type),
				 ifa->ifa_prefixlen, 4);

			return -1;
		}

		/* Only consider valid addresses; we'll not get a kernel
		 * notification till IPv6 DAD has completed, but at init
		 * time, FRR does query for and will receive all addresses.
		 */
		/*
		 * 只处理有效地址。
		 * 一般情况下，内核在 IPv6 DAD（重复地址检测）完成前不会通知；
		 * 但在初始化时，FRR 主动查询会收到所有地址（包括 DAD 失败的）。
		 *
		 * 因此这里对于 RTM_NEWADDR 且带 IFA_F_DADFAILED 标志的地址，
		 * 直接跳过，不做任何处理。
		 */
		if (h->nlmsg_type == RTM_NEWADDR
		    && (kernel_flags & IFA_F_DADFAILED)) {
			/* DAD failed - always skip these addresses */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug("%s: %s: DAD failed addr",
					   __func__,
					   nl_msg_type_to_str(h->nlmsg_type));

			frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type),
				 0, 5);

			return 0;
		}
		/* Note: IFA_F_TENTATIVE is passed via context to main pthread
		 * for handling - we cannot access interface pointers here.
		 */
		/*
		 * 注意：IFA_F_TENTATIVE（地址处于试探状态）不会在这里过滤，
		 * 而是把标志通过上下文传递给主线程处理。
		 * 因为这里运行在 dplane 线程，不能直接访问接口指针。
		 */
	}

	/* logic copied from iproute2/ip/ipaddress.c:print_addrinfo() */
	/*
	 * 以下地址处理逻辑参考 iproute2 中的 print_addrinfo()：
	 *
	 * IFA_LOCAL 和 IFA_ADDRESS 的语义在不同场景下略有不同：
	 *   - 对于普通地址，IFA_LOCAL 是本地地址，IFA_ADDRESS 通常等于 IFA_LOCAL。
	 *   - 对于点对点链路，IFA_LOCAL 是本地地址，IFA_ADDRESS 是对端地址。
	 *   - 对于广播链路，IFA_ADDRESS 是本地地址，IFA_BROADCAST 是广播地址。
	 *
	 * 因此这里做互补赋值，保证两者都可用。
	 */
	if (tb[IFA_LOCAL] == NULL)
		tb[IFA_LOCAL] = tb[IFA_ADDRESS];
	if (tb[IFA_ADDRESS] == NULL)
		tb[IFA_ADDRESS] = tb[IFA_LOCAL];

	/* local interface address */
	/* 取出本地接口地址（作为主键） */
	addr = (tb[IFA_LOCAL] ? RTA_DATA(tb[IFA_LOCAL]) : NULL);

	/* addr is primary key, SOL if we don't have one */
	/* 如果无法取得本地地址，则消息不完整，直接返回 -1 */
	if (addr == NULL) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: %s: No local interface address",
				   __func__, nl_msg_type_to_str(h->nlmsg_type));

		frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type),
			 ifa->ifa_index, 6);

		return -1;
	}

	/* Allocate a context object, now that validation is done. */
	/*
	 * 校验完成后，分配数据平面上下文对象。
	 * 上下文会被传给主线程，由主线程负责更新 zebra 内部的接口地址状态。
	 */
	ctx = dplane_ctx_alloc();

	/* 根据 netlink 消息类型设置上下文操作类型 */
	if (h->nlmsg_type == RTM_NEWADDR)
		dplane_ctx_set_op(ctx, DPLANE_OP_INTF_ADDR_ADD);
	else
		dplane_ctx_set_op(ctx, DPLANE_OP_INTF_ADDR_DEL);

	/* 保存接口索引和命名空间 ID */
	dplane_ctx_set_ifindex(ctx, ifa->ifa_index);
	dplane_ctx_set_ns_id(ctx, ns_id);

	/* Convert addr to prefix */
	/*
	 * 将本地地址转换为前缀结构，并保存到上下文中。
	 * family、prefixlen 来自 ifaddrmsg；
	 * 地址部分根据地址族分别赋值给 prefix4 或 prefix6。
	 */
	memset(&p, 0, sizeof(p));
	p.family = ifa->ifa_family;
	p.prefixlen = ifa->ifa_prefixlen;
	if (p.family == AF_INET)
		p.u.prefix4 = *(struct in_addr *)addr;
	else
		p.u.prefix6 = *(struct in6_addr *)addr;

	dplane_ctx_set_intf_addr(ctx, &p);

	/* is there a peer address? */
	/*
	 * 判断是否存在对端地址或广播地址：
	 *
	 * 1) 如果 IFA_ADDRESS 与 IFA_LOCAL 内容不同，说明是对点对点链路，
	 *    IFA_ADDRESS 是对端地址，将其作为 dest 并设置 connected 标志。
	 * 2) 否则如果存在 IFA_BROADCAST，说明是广播链路，
	 *    将其作为 dest 并设置 broadcast 标志。
	 * 3) 都不存在则 broad = NULL。
	 */
	if (tb[IFA_ADDRESS]
	    && memcmp(RTA_DATA(tb[IFA_ADDRESS]), RTA_DATA(tb[IFA_LOCAL]),
		      RTA_PAYLOAD(tb[IFA_ADDRESS]))) {
		broad = RTA_DATA(tb[IFA_ADDRESS]);
		dplane_ctx_intf_set_connected(ctx);
	} else if (tb[IFA_BROADCAST]) {
		/* seeking a broadcast address */
		broad = RTA_DATA(tb[IFA_BROADCAST]);
		dplane_ctx_intf_set_broadcast(ctx);
	} else
		broad = NULL;

	if (broad) {
		/* Convert addr to prefix */
		/*
		 * 如果存在对端/广播地址，同样转换为 prefix 结构，
		 * 并保存到上下文的 dest 字段。
		 */
		memset(&p, 0, sizeof(p));
		p.family = ifa->ifa_family;
		p.prefixlen = ifa->ifa_prefixlen;
		if (p.family == AF_INET)
			p.u.prefix4 = *(struct in_addr *)broad;
		else
			p.u.prefix6 = *(struct in6_addr *)broad;

		dplane_ctx_set_intf_dest(ctx, &p);
	}

	/* Flags. */
	/*
	 * 根据内核地址标志设置上下文中的各个布尔标志：
	 *   IFA_F_SECONDARY      从属地址（如 eth0:1）
	 *   IFA_F_NOPREFIXROUTE  不为该地址自动生成前缀路由
	 *   IFA_F_TENTATIVE      IPv6 地址处于试探状态
	 */
	if (kernel_flags & IFA_F_SECONDARY)
		dplane_ctx_intf_set_secondary(ctx);

	if (kernel_flags & IFA_F_NOPREFIXROUTE)
		dplane_ctx_intf_set_noprefixroute(ctx);

	if (kernel_flags & IFA_F_TENTATIVE)
		dplane_ctx_intf_set_tentative(ctx);

	/* Label */
	/*
	 * 如果存在 IFA_LABEL，说明是带标签的接口地址，
	 * 例如 eth0:1。将其保存到上下文中。
	 */
	if (tb[IFA_LABEL]) {
		label = (char *)RTA_DATA(tb[IFA_LABEL]);
		dplane_ctx_set_intf_label(ctx, label);
	}

	/* 如果存在 IFA_RT_PRIORITY，则以其作为地址的 metric */
	if (tb[IFA_RT_PRIORITY])
		metric = *(uint32_t *)RTA_DATA(tb[IFA_RT_PRIORITY]);

	dplane_ctx_set_intf_metric(ctx, metric);

	/* Enqueue ctx for main pthread to process */
	/*
	 * 将构造完成的上下文入队到 zebra 主线程的队列，
	 * 由主线程负责后续处理（更新接口地址、RI B 等）。
	 */
	dplane_provider_enqueue_to_zebra(ctx);
	return 0;
}

/*
 * netlink_link_change：
 *   解析并处理内核发来的链路（接口）变更消息。
 *
 *   该函数运行在数据平面 pthread 中，处理 RTM_NEWLINK / RTM_DELLINK 消息，
 *   提取接口属性（名称、类型、从属关系、MTU、flags 等），构造数据平面上下文，
 *   最终入队给 zebra 主线程做进一步处理。
 *
 * 参数：
 *   h        当前 netlink 消息头
 *   ns_id    消息所属的网络命名空间 ID
 *   startup  是否处于启动阶段
 *
 * 返回值：
 *   0    成功处理或有意忽略
 *  -1   消息格式错误
 */
int netlink_link_change(struct nlmsghdr *h, ns_id_t ns_id, int startup)
{
	int len;                                    /* 属性区长度 */
	struct ifinfomsg *ifi;                      /* 链路信息消息体 */
	struct rtattr *tb[IFLA_MAX + 1];            /* 解析后的 IFLA_* 属性表 */
	struct rtattr *linkinfo[IFLA_MAX + 1];      /* 嵌套的 linkinfo 属性表 */
	char *name = NULL;                          /* 接口名称 */
	char *kind = NULL;                          /* 接口类型（如 vrf、bridge、bond、vxlan 等） */
	char *slave_kind = NULL;                    /* 从接口类型（如 vrf、bridge、bond） */
	vrf_id_t vrf_id = VRF_DEFAULT;              /* 接口所属 VRF ID，默认 VRF_DEFAULT */
	enum zebra_iftype zif_type = ZEBRA_IF_OTHER; /* zebra 接口类型，默认 OTHER */
	enum zebra_slave_iftype zif_slave_type = ZEBRA_IF_SLAVE_NONE; /* 从接口类型，默认 NONE */
	ifindex_t bridge_ifindex = IFINDEX_INTERNAL; /* 桥接主接口索引 */
	ifindex_t bond_ifindex = IFINDEX_INTERNAL;   /* 聚合主接口索引 */
	ifindex_t link_ifindex = IFINDEX_INTERNAL;   /* 关联接口索引（IFLA_LINK） */
	ns_id_t link_nsid = ns_id;                   /* 关联接口所在命名空间，默认本 ns */
	ifindex_t master_infindex = IFINDEX_INTERNAL; /* 主接口索引（master） */
	uint8_t bypass = 0;                          /* bond slave bypass 标志 */
	uint32_t txqlen = 0;                         /* 发送队列长度 */

	/* 上报 trace，便于问题定位 */
	frrtrace(3, frr_zebra, netlink_interface, h, ns_id, startup);

	/* 获取 netlink 消息数据部分，即 ifinfomsg */
	ifi = NLMSG_DATA(h);

	/*
	 * 校验消息类型：只处理 RTM_NEWLINK 和 RTM_DELLINK。
	 * 其他类型消息打印调试日志并返回 0。
	 */
	if (!(h->nlmsg_type == RTM_NEWLINK || h->nlmsg_type == RTM_DELLINK)) {
		/* If this is not link add/delete message so print warning. */
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: wrong kernel message %s", __func__,
				   nl_msg_type_to_str(h->nlmsg_type));

		frrtrace(3, frr_zebra, netlink_msg_err, nl_msg_type_to_str(h->nlmsg_type), 0, 7);

		return 0;
	}

	/*
	 * 校验地址族：
	 *   AF_UNSPEC  普通接口
	 *   AF_BRIDGE  桥接接口
	 *   AF_INET6   IPv6 接口
	 * 其他地址族视为非法，记录告警并返回 0。
	 */
	if (!(ifi->ifi_family == AF_UNSPEC || ifi->ifi_family == AF_BRIDGE
	      || ifi->ifi_family == AF_INET6)) {
		flog_warn(
			EC_ZEBRA_UNKNOWN_FAMILY,
			"Invalid address family: %u received from kernel link change: %s",
			ifi->ifi_family, nl_msg_type_to_str(h->nlmsg_type));
		return 0;
	}

	/*
	 * 计算属性区长度：
	 *   h->nlmsg_len                       整个 netlink 消息长度
	 *   NLMSG_LENGTH(sizeof(ifinfomsg))    消息头 + ifinfomsg 的长度
	 * 差值即为属性区域（rtattr 数组）的字节数。
	 */
	len = h->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifinfomsg));
	if (len < 0) {
		flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
			 "%s: Message received from netlink is of a broken size %d %zu", __func__,
			 h->nlmsg_len, (size_t)NLMSG_LENGTH(sizeof(struct ifinfomsg)));
		return -1;
	}

	/* Looking up interface name. */
	/*
	 * 解析 IFLA_* 属性到 tb 数组。
	 * 使用 NLA_F_NESTED 标志，因为某些属性（如 IFLA_LINKINFO）是嵌套的。
	 */
	memset(linkinfo, 0, sizeof(linkinfo));
	netlink_parse_rtattr_flags(tb, IFLA_MAX, IFLA_RTA(ifi), len,
				   NLA_F_NESTED);

	/* check for wireless messages to ignore */
	/*
	 * 忽略无线（wireless）消息。
	 * IFLA_WIRELESS 表示这是无线事件，且 ifi_change 为 0 时说明
	 * 只是通知而非配置变更，直接忽略。
	 */
	if ((tb[IFLA_WIRELESS] != NULL) && (ifi->ifi_change == 0)) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: ignoring IFLA_WIRELESS message",
				   __func__);

		frrtrace(1, frr_zebra, if_netlink_parse_error, 8);

		return 0;
	}

	/* 接口名称必须存在 */
	if (tb[IFLA_IFNAME] == NULL)
		return -1;
	name = (char *)RTA_DATA(tb[IFLA_IFNAME]);

	/* Must be valid string. */
	/*
	 * 校验接口名称合法性：
	 *   - 长度至少为 2（至少一个字符 + '\0'）
	 *   - 最后一个字符必须是字符串结束符 '\0'
	 */
	len = RTA_PAYLOAD(tb[IFLA_IFNAME]);
	if (len < 2 || name[len - 1] != '\0') {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: invalid intf name", __func__);

		frrtrace(1, frr_zebra, if_netlink_parse_error, 9);

		return -1;
	}

	/*
	 * 解析 IFLA_LINKINFO 嵌套属性，获取接口类型和从接口类型。
	 *   IFLA_INFO_KIND       接口类型，如 "vrf"、"bridge"、"bond"、"vxlan"
	 *   IFLA_INFO_SLAVE_KIND 从接口类型，如 "vrf"、"bridge"、"bond"
	 */
	if (tb[IFLA_LINKINFO]) {
		netlink_parse_rtattr_nested(linkinfo, IFLA_INFO_MAX,
					    tb[IFLA_LINKINFO]);

		if (linkinfo[IFLA_INFO_KIND])
			kind = RTA_DATA(linkinfo[IFLA_INFO_KIND]);

		if (linkinfo[IFLA_INFO_SLAVE_KIND])
			slave_kind = RTA_DATA(linkinfo[IFLA_INFO_SLAVE_KIND]);

		/* 将内核接口类型字符串转换为 zebra 内部类型 */
		netlink_determine_zebra_iftype(kind, &zif_type);
	}

	/* If linking to another interface, note it. */
	/* 如果该接口关联到另一个接口（如 VxLAN 关联到物理口），记录其索引 */
	if (tb[IFLA_LINK])
		link_ifindex = *(ifindex_t *)RTA_DATA(tb[IFLA_LINK]);

	/*
	 * 如果关联接口在另一个命名空间中，记录其命名空间 ID。
	 * ns_id_get_absolute() 将相对命名空间 ID 转换为绝对 ID。
	 */
	if (tb[IFLA_LINK_NETNSID]) {
		link_nsid = *(ns_id_t *)RTA_DATA(tb[IFLA_LINK_NETNSID]);
		link_nsid = ns_id_get_absolute(ns_id, link_nsid);
	}

	/* 提取发送队列长度 */
	if (tb[IFLA_TXQLEN])
		txqlen = *(uint32_t *)RTA_DATA(tb[IFLA_TXQLEN]);

	/* 分配数据平面上下文 */
	struct zebra_dplane_ctx *ctx = dplane_ctx_alloc();

	/* 保存命名空间 ID、关联接口命名空间、zebra 接口类型 */
	dplane_ctx_set_ns_id(ctx, ns_id);
	dplane_ctx_set_ifp_link_nsid(ctx, link_nsid);
	dplane_ctx_set_ifp_zif_type(ctx, zif_type);

	/* 保存接口索引、接口名 */
	dplane_ctx_set_ifindex(ctx, ifi->ifi_index);
	dplane_ctx_set_ifname(ctx, name);

	/* 保存启动标志、地址族、txqlen */
	dplane_ctx_set_ifp_startup(ctx, startup);
	dplane_ctx_set_ifp_family(ctx, ifi->ifi_family);
	dplane_ctx_set_intf_txqlen(ctx, txqlen);

	/* We are interested in some AF_BRIDGE notifications. */
	/*
	 * 对于 AF_BRIDGE 地址族的接口变更，交给桥接专用处理函数。
	 * 因为桥接接口的属性（端口、VLAN、FDB 等）需要特殊解析。
	 */
#ifndef AF_BRIDGE
#define AF_BRIDGE 7
#endif
	if (ifi->ifi_family == AF_BRIDGE) {
		dplane_ctx_set_op(ctx, DPLANE_OP_INTF_INSTALL);
		return netlink_bridge_interface(ctx, tb[IFLA_AF_SPEC], startup);
	}

	if (h->nlmsg_type == RTM_NEWLINK) {
		/*
		 * ============================
		 * 新增链路分支（RTM_NEWLINK）
		 * ============================
		 */
		/* 保存关联接口索引 */
		dplane_ctx_set_ifp_link_ifindex(ctx, link_ifindex);

		/* 设置操作类型为接口安装 */
		dplane_ctx_set_op(ctx, DPLANE_OP_INTF_INSTALL);

		/* 设置状态为已排队，表示该上下文来自数据平面 inbound 路径 */
		dplane_ctx_set_status(ctx, ZEBRA_DPLANE_REQUEST_QUEUED);

		/* 如果存在接口别名（IFLA_IFALIAS），作为描述信息保存 */
		if (tb[IFLA_IFALIAS]) {
			dplane_ctx_set_ifp_desc(ctx,
						RTA_DATA(tb[IFLA_IFALIAS]));
		}

		/*
		 * MTU 属性必须存在，否则无法正确配置接口。
		 * 若缺失，记录调试日志并释放上下文，返回 0。
		 */
		if (!tb[IFLA_MTU]) {
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"RTM_NEWLINK for interface %s(%u) without MTU set",
					name, ifi->ifi_index);

			frrtrace(3, frr_zebra, netlink_intf_err, name, ifi->ifi_index, 2);

			dplane_ctx_fini(&ctx);
			return 0;
		}

		/* 保存 MTU */
		dplane_ctx_set_ifp_mtu(ctx, *(int *)RTA_DATA(tb[IFLA_MTU]));

		/* If VRF, create or update the VRF structure itself. */
		/*
		 * 如果该接口是 VRF 类型且后端不是 netns，则创建或更新 VRF 结构。
		 * VRF ID 直接使用接口索引。
		 */
		if (zif_type == ZEBRA_IF_VRF && !vrf_is_backend_netns()) {
			netlink_vrf_change(h, tb[IFLA_LINKINFO], ns_id, name,
					   ctx);
			vrf_id = ifi->ifi_index;
		}

		/*
		 * 处理主接口（IFLA_MASTER）属性，确定从接口类型：
		 *   - slave_kind 为 "vrf"   → 从属于 VRF
		 *   - slave_kind 为 "bridge" → 从属于桥接
		 *   - slave_kind 为 "bond"   → 从属于聚合
		 *   - 其他                   → 从属于其他类型
		 *
		 * 对于 bond slave，还需要解析 LACP bypass 标志。
		 */
		if (tb[IFLA_MASTER]) {
			if (slave_kind && (strcmp(slave_kind, "vrf") == 0)
			    && !vrf_is_backend_netns()) {
				zif_slave_type = ZEBRA_IF_SLAVE_VRF;
				master_infindex = vrf_id =
					*(uint32_t *)RTA_DATA(tb[IFLA_MASTER]);
			} else if (slave_kind
				   && (strcmp(slave_kind, "bridge") == 0)) {
				zif_slave_type = ZEBRA_IF_SLAVE_BRIDGE;
				master_infindex = bridge_ifindex =
					*(ifindex_t *)RTA_DATA(tb[IFLA_MASTER]);
			} else if (slave_kind
				   && (strcmp(slave_kind, "bond") == 0)) {
				zif_slave_type = ZEBRA_IF_SLAVE_BOND;
				master_infindex = bond_ifindex =
					*(ifindex_t *)RTA_DATA(tb[IFLA_MASTER]);
				bypass = netlink_parse_lacp_bypass(linkinfo);
			} else
				zif_slave_type = ZEBRA_IF_SLAVE_OTHER;
		}

		/* 保存从接口类型、VRF ID、主接口索引、桥接/聚合索引、bypass 标志 */
		dplane_ctx_set_ifp_zif_slave_type(ctx, zif_slave_type);
		dplane_ctx_set_ifp_vrf_id(ctx, vrf_id);
		dplane_ctx_set_ifp_master_ifindex(ctx, master_infindex);
		dplane_ctx_set_ifp_bridge_ifindex(ctx, bridge_ifindex);
		dplane_ctx_set_ifp_bond_ifindex(ctx, bond_ifindex);
		dplane_ctx_set_ifp_bypass(ctx, bypass);

		/* 将内核链路类型转换为 zebra 内部链路类型 */
		dplane_ctx_set_ifp_zltype(
			ctx, netlink_to_zebra_link_type(ifi->ifi_type));

		/* 如果 VRF 后端是 netns，则 VRF ID 等于命名空间 ID */
		if (vrf_is_backend_netns())
			dplane_ctx_set_ifp_vrf_id(ctx, ns_id);

		/* 保存接口 flags 和变更标志（只取低 20 位） */
		dplane_ctx_set_ifp_flags(ctx, ifi->ifi_flags & 0x0000fffff);
		dplane_ctx_set_ifp_change_flags(ctx, ifi->ifi_change & 0x0000fffff);

		/*
		 * 处理 protodown 属性：
		 *   如果存在 IFLA_PROTO_DOWN，则标记 protodown_set 并解析其值；
		 *   否则标记 protodown_set 为 false。
		 */
		if (tb[IFLA_PROTO_DOWN]) {
			dplane_ctx_set_ifp_protodown_set(ctx, true);
			netlink_proc_dplane_if_protodown(ctx, tb);
		} else
			dplane_ctx_set_ifp_protodown_set(ctx, false);

		/* 解析并保存硬件地址（MAC） */
		netlink_interface_update_hw_addr(tb, ctx);

		/* Extract and save L2 interface information, take
		 * additional actions. */
		/*
		 * 提取并保存 L2 接口信息（桥接/VxLAN 等），
		 * 第三个参数 1 表示这是新增操作。
		 */
		netlink_interface_update_l2info(
			ctx, zif_type, linkinfo[IFLA_INFO_DATA], 1, link_nsid);
	} else {
		/*
		 * ============================
		 * 删除链路分支（RTM_DELLINK）
		 * ============================
		 */
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("RTM_DELLINK for %s(%u), enqueuing to zebra",
				   name, ifi->ifi_index);

		/* 设置操作类型为接口删除 */
		dplane_ctx_set_op(ctx, DPLANE_OP_INTF_DELETE);
		dplane_ctx_set_status(ctx, ZEBRA_DPLANE_REQUEST_QUEUED);

		/* 保存聚合主接口索引（用于从接口删除时通知 L2 子系统） */
		dplane_ctx_set_ifp_bond_ifindex(ctx, bond_ifindex);
	}

	/* 将上下文入队到 zebra 主线程处理 */
	dplane_provider_enqueue_to_zebra(ctx);

	return 0;
}

/**
 * Interface encoding helper function.
 *
 * \param[in] cmd netlink command.
 * \param[in] ctx dataplane context (information snapshot).
 * \param[out] buf buffer to hold the packet.
 * \param[in] buflen amount of buffer bytes.
 */

ssize_t netlink_intf_msg_encode(uint16_t cmd,
				const struct zebra_dplane_ctx *ctx, void *buf,
				size_t buflen)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg ifa;
		char buf[];
	} *req = buf;

	struct rtattr *nest_protodown_reason;
	ifindex_t ifindex = dplane_ctx_get_ifindex(ctx);
	bool down = dplane_ctx_intf_is_protodown(ctx);
	bool pd_reason_val = dplane_ctx_get_intf_pd_reason_val(ctx);
	struct nlsock *nl =
		kernel_netlink_nlsock_lookup(dplane_ctx_get_ns_sock(ctx));

	if (buflen < sizeof(*req))
		return 0;

	memset(req, 0, sizeof(*req));

	if (cmd != RTM_SETLINK)
		flog_err(
			EC_ZEBRA_INTF_UPDATE_FAILURE,
			"Only RTM_SETLINK message type currently supported in dplane pthread");

	req->n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req->n.nlmsg_flags = NLM_F_REQUEST;
	req->n.nlmsg_type = cmd;
	req->n.nlmsg_pid = nl->snl.nl_pid;

	req->ifa.ifi_index = ifindex;

	if (!nl_attr_put8(&req->n, buflen, IFLA_PROTO_DOWN, down))
		return 0;
	if (!nl_attr_put32(&req->n, buflen, IFLA_LINK, ifindex))
		return 0;

	/* Reason info nest */
	nest_protodown_reason =
		nl_attr_nest(&req->n, buflen, IFLA_PROTO_DOWN_REASON);

	if (!nest_protodown_reason)
		return 0;

	if (!nl_attr_put32(&req->n, buflen, IFLA_PROTO_DOWN_REASON_MASK,
			   (1 << if_netlink_get_frr_protodown_r_bit())))
		return 0;
	if (!nl_attr_put32(&req->n, buflen, IFLA_PROTO_DOWN_REASON_VALUE,
			   ((int)pd_reason_val) << if_netlink_get_frr_protodown_r_bit()))
		return 0;

	nl_attr_nest_end(&req->n, nest_protodown_reason);

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("%s: %s, protodown=%d reason_val=%d ifindex=%u",
			   __func__, nl_msg_type_to_str(cmd), down,
			   pd_reason_val, ifindex);

	return NLMSG_ALIGN(req->n.nlmsg_len);
}

/* Interface information read by netlink. */
/*
 * interface_list：
 *   通过 netlink 读取接口信息，并推进数据平面启动阶段。
 *
 *   该函数在 zebra 启动阶段被调用，完成两件事：
 *     1. 调用 interface_lookup_netlink() 从内核 dump 所有接口信息
 *        （包括普通接口和桥接接口），并通过 netlink_link_change()
 *        解析后构造上下文交给 zebra 主线程处理。
 *     2. 通过 zebra_dplane_startup_stage() 将数据平面启动阶段推进到
 *        ZEBRA_DPLANE_INTERFACES_READ，通知主线程接口信息已读取完成，
 *        后续可以继续执行下一阶段的初始化工作。
 *
 *   启动阶段流转：
 *     （当前阶段）→ interface_list() → ZEBRA_DPLANE_INTERFACES_READ
 *     之后 zebra_ns_startup_continue() 会在 ZEBRA_DPLANE_INTERFACES_READ
 *     分支中调用 interface_list_tunneldump() 继续读取隧道信息。
 *
 * 参数：
 *   zns  zebra 命名空间
 */
void interface_list(struct zebra_ns *zns)
{
	/*
	 * 通过 netlink 读取所有接口信息。
	 *
	 * interface_lookup_netlink() 内部会：
	 *   1. 使用 AF_PACKET + RTM_GETLINK 请求所有普通接口信息并解析。
	 *   2. 使用 AF_BRIDGE + RTM_GETLINK + RTEXT_FILTER_BRVLAN 请求
	 *      桥接接口信息（含 VLAN）并解析。
	 *
	 * 每条接口信息都会通过 netlink_link_change() 构造数据平面上下文，
	 * 并调用 dplane_provider_enqueue_to_zebra() 投递给 zebra 主线程，
	 * 由 zebra_if_dplane_result() → zebra_if_dplane_ifp_handling()
	 * 创建或更新 zebra 内部的接口数据结构。
	 *
	 * 注意：本函数不检查 interface_lookup_netlink() 的返回值，
	 * 因为即使读取失败，也需要推进启动阶段，避免启动流程卡住。
	 * 失败的具体错误会在 interface_lookup_netlink() 内部记录日志。
	 */
	interface_lookup_netlink(zns);

	/*
	 * 将数据平面启动阶段推进到 ZEBRA_DPLANE_INTERFACES_READ。
	 *
	 * zebra_dplane_startup_stage() 会：
	 *   1. 记录当前命名空间的启动阶段状态。
	 *   2. 向数据平面线程或主线程投递事件，触发后续启动阶段的处理。
	 *
	 * 之后 zebra_ns_startup_continue() 会在该阶段分支中调用
	 * interface_list_tunneldump() 继续读取隧道信息。
	 */
	zebra_dplane_startup_stage(zns, ZEBRA_DPLANE_INTERFACES_READ);
}

/*
 * interface_list_second：
 *   zebra 启动流程的第二阶段，在隧道信息读取完成后执行。
 *
 *   该函数由 zebra_ns_startup_continue() 在 ZEBRA_DPLANE_TUNNELS_READ
 *   阶段调用，主要完成：
 *     1. 更新所有接口的链路关系（link）。
 *     2. 从内核读取 nexthop 信息。
 *     3. 从内核读取接口地址信息。
 *     4. 推进数据平面启动阶段到 ZEBRA_DPLANE_ADDRESSES_READ。
 *
 *   顺序要求：
 *     - 必须先更新接口链路关系，保证接口之间的 link 关系正确。
 *     - 必须先读取 nexthop，因为后续为接口地址添加路由时需要用到 nexthop。
 *     - 接口地址读取完成后，才能进入下一阶段（路由、VLAN、PBR、TC 等）。
 *
 * 参数：
 *   zns  zebra 命名空间
 */
void interface_list_second(struct zebra_ns *zns)
{
	/*
	 * 更新命名空间下所有接口的链路关系。
	 *
	 * 在之前的接口读取阶段，每个接口的 link_ifindex、link_nsid 等
	 * 信息已经保存到 zebra 的接口数据结构中，但接口之间的关联关系
	 * （例如 VxLAN 关联到物理口、VLAN 关联到父接口）需要在此统一
	 * 建立和维护。
	 *
	 * 这一步确保后续处理接口地址、路由时，链路关系是完整且正确的。
	 */
	zebra_if_update_all_links(zns);

	/* We add routes for interface address,
	 * so we need to get the nexthop info
	 * from the kernel before we can do that
	 */
	/*
	 * 说明：
	 *   在为接口地址添加路由时，需要用到 nexthop 信息。
	 *   因此必须先向内核请求并解析 nexthop，确保后续路由操作
	 *   能够正确引用这些 nexthop。
	 *
	 * netlink_nexthop_read() 会：
	 *   - 向内核发送 RTM_GETNEXTHOP 请求，dump 所有 nexthop。
	 *   - 使用 netlink_nexthop_change() 解析每条 nexthop 消息。
	 *   - 更新 zebra 内部的 nexthop 数据结构。
	 */
	netlink_nexthop_read(zns);

	/*
	 * 从内核读取接口地址信息。
	 *
	 * interface_addr_lookup_netlink() 会：
	 *   - 向内核发送 RTM_GETADDR 请求，dump 所有接口地址。
	 *   - 使用 netlink_interface_addr_dplane() 解析每条地址消息。
	 *   - 构造数据平面上下文，投递给 zebra 主线程，
	 *     最终更新接口地址数据结构。
	 *
	 * 由于此时 nexthop 已经读取完毕，地址对应的路由在后续处理中
	 * 可以正确引用 nexthop。
	 */
	interface_addr_lookup_netlink(zns);

	/*
	 * 推进数据平面启动阶段到 ZEBRA_DPLANE_ADDRESSES_READ。
	 *
	 * 该调用会通知系统接口地址信息已读取完成，后续
	 * zebra_ns_startup_continue() 会在 ZEBRA_DPLANE_ADDRESSES_READ
	 * 分支中继续读取路由、VLAN、PBR 规则、TC qdisc，并最终调用
	 * zebra_main_router_started() 启动路由清理子系统。
	 */
	zebra_dplane_startup_stage(zns, ZEBRA_DPLANE_ADDRESSES_READ);
}

/**
 * netlink_request_tunneldump() - Request all tunnels from the linux kernel
 *
 * @zns:	Zebra namespace
 * @family:	AF_* netlink family
 * @type:	RTM_* (RTM_GETTUNNEL) route type
 *
 * Return:	Result status
 */
static int netlink_request_tunneldump(struct zebra_ns *zns, int family,
				      int ifindex)
{
	struct {
		struct nlmsghdr n;
		struct tunnel_msg tmsg;
		char buf[256];
	} req;

	/* Form the request */
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct tunnel_msg));
	req.n.nlmsg_type = RTM_GETTUNNEL;
	req.n.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.tmsg.family = family;
	req.tmsg.ifindex = ifindex;

	return netlink_request(&zns->netlink_cmd, &req);
}

/* Prototype for tunneldump walker */
static int tunneldump_walk_cb(struct interface *ifp, void *arg);

/*
 * tunneldump_ctx：
 *   隧道转储（tunnel dump）遍历过程中使用的上下文结构。
 *
 *   在遍历所有接口时，通过该结构把以下信息传递给回调函数：
 *     - zns       zebra 命名空间，供发送 netlink 请求使用
 *     - dp_info   数据平面信息，携带命名空间上下文，传给解析回调
 *     - ret       累计返回值；遍历过程中若出错则记录并中止
 */
struct tunneldump_ctx {
	struct zebra_ns *zns;                   /* zebra 命名空间 */
	struct zebra_dplane_info *dp_info;      /* 数据平面信息，供 netlink_parse_info 使用 */
	int ret;                                /* 累计返回值，出错时记录 */
};

/*
 * Currently we only ask for vxlan l3svd vni information.
 * In the future this can be expanded.
 */
/*
 * netlink_tunneldump_read：
 *   读取隧道（tunnel）转储信息。
 *
 *   目前只请求 VxLAN L3VNI 相关信息，未来可以扩展支持其他类型。
 *
 *   主要流程：
 *     1. 从 zebra 命名空间构造数据平面信息 dp_info（命令 socket 上下文）。
 *     2. 初始化 tunneldump_ctx，保存 zns 和 dp_info。
 *     3. 遍历命名空间下的所有接口，对每个接口调用 tunneldump_walk_cb。
 *     4. 返回遍历过程中累计的返回值。
 *
 * 参数：
 *   zns  zebra 命名空间
 *
 * 返回值：
 *   遍历过程中记录的返回值 ctx.ret；成功时为 0，出错时为负值。
 */
int netlink_tunneldump_read(struct zebra_ns *zns)
{
	int ret = 0;                            /* 默认返回值，成功为 0 */
	struct tunneldump_ctx ctx = {};         /* 隧道转储上下文，初始化为 0 */
	struct zebra_dplane_info dp_info;       /* 数据平面信息 */

	/*
	 * 从 zns 中提取关键信息填充 dp_info。
	 * 第三个参数 true 表示这是命令 socket（is_cmd），
	 * 影响后续错误处理和消息分发逻辑。
	 */
	zebra_dplane_info_from_zns(&dp_info, zns, true /*is_cmd*/);

	/* Set up context and call iterator */
	/*
	 * 填充隧道转储上下文，供回调函数使用。
	 */
	ctx.zns = zns;
	ctx.dp_info = &dp_info;

	/*
	 * 遍历该命名空间下所有接口，对每个接口调用 tunneldump_walk_cb。
	 * 回调内部会筛选 VxLAN 接口并请求其隧道转储信息。
	 */
	zebra_ns_ifp_walk(zns, tunneldump_walk_cb, &ctx);

	/* 取出遍历过程中的返回值 */
	ret = ctx.ret;

	/* 返回结果 */
	return ret;
}

/*
 * tunneldump_walk_cb：
 *   接口遍历回调函数，对每个接口执行隧道转储请求与解析。
 *
 *   由 zebra_ns_ifp_walk() 对每个接口调用一次。
 *
 *   主要流程：
 *     1. 取出接口的 zebra_if 扩展信息。
 *     2. 只处理 VxLAN 类型接口（ZEBRA_IF_VXLAN），其他接口直接跳过。
 *     3. 向内核发送隧道转储请求（针对该接口的 ifindex）。
 *     4. 解析内核返回的消息，交给 netlink_link_change() 处理。
 *     5. 若任一步骤失败，记录错误并停止遍历。
 *
 * 参数：
 *   ifp  当前遍历到的接口
 *   arg  遍历上下文，即 struct tunneldump_ctx *
 *
 * 返回值：
 *   NS_WALK_CONTINUE  继续遍历下一个接口
 *   NS_WALK_STOP      停止遍历（发生错误）
 */
static int tunneldump_walk_cb(struct interface *ifp, void *arg)
{
	int ret;                                /* 通用返回值 */
	struct tunneldump_ctx *ctx = arg;       /* 取出遍历上下文 */
	struct zebra_if *zif;                   /* 接口的 zebra 扩展信息 */

	/* 获取接口的 zebra_if 扩展信息 */
	zif = ifp->info;

	/*
	 * 只处理 VxLAN 接口。
	 * 如果 zif 为空，或接口类型不是 VxLAN，则跳过，继续遍历下一个接口。
	 */
	if (!zif || zif->zif_type != ZEBRA_IF_VXLAN)
		goto done;

	/*
	 * 向内核发送隧道转储请求。
	 *   ctx->zns           当前命名空间
	 *   PF_BRIDGE          协议族为桥接
	 *   ifp->ifindex       指定要转储的 VxLAN 接口索引
	 *
	 * 请求失败时，记录错误并停止遍历。
	 */
	ret = netlink_request_tunneldump(ctx->zns, PF_BRIDGE, ifp->ifindex);
	if (ret < 0) {
		ctx->ret = ret;
		return NS_WALK_STOP;
	}

	/*
	 * 解析内核返回的消息。
	 *   netlink_link_change     处理 LINK 变更消息的回调
	 *   &(ctx->zns->netlink_cmd) 使用命令 socket 接收响应
	 *   ctx->dp_info            数据平面信息，传给回调
	 *   0                       不限制读取批次
	 *   true                    启动阶段标志
	 *
	 * 解析失败时，记录错误并停止遍历。
	 */
	ret = netlink_parse_info(netlink_link_change, &(ctx->zns->netlink_cmd),
				 ctx->dp_info, 0, true);

	if (ret < 0) {
		ctx->ret = ret;
		return NS_WALK_STOP;
	}

done:
	/* 继续遍历下一个接口 */
	return NS_WALK_CONTINUE;
}

static uint8_t netlink_get_dplane_vlan_state(uint8_t state)
{
	if (state == BR_STATE_LISTENING)
		return ZEBRA_DPLANE_BR_STATE_LISTENING;
	else if (state == BR_STATE_LEARNING)
		return ZEBRA_DPLANE_BR_STATE_LEARNING;
	else if (state == BR_STATE_FORWARDING)
		return ZEBRA_DPLANE_BR_STATE_FORWARDING;
	else if (state == BR_STATE_BLOCKING)
		return ZEBRA_DPLANE_BR_STATE_BLOCKING;

	return ZEBRA_DPLANE_BR_STATE_DISABLED;
}

/**
 * netlink_vlan_change() - Read in change about vlans from the kernel
 *
 * @h:		Netlink message header
 * @ns_id:	Namespace id
 * @startup:	Are we reading under startup conditions?
 *
 * Return:	Result status
 */
/*
 * netlink_vlan_change：
 *   读取并处理内核发来的 VLAN 变更消息。
 *
 *   主要流程：
 *     1. 只关心 RTM_NEWVLAN 消息（新增或状态更新）。
 *     2. 校验消息长度和地址族（必须是 AF_BRIDGE）。
 *     3. 分配数据平面上下文 ctx，设置操作类型为 DPLANE_OP_VLAN_INSTALL。
 *     4. 遍历 BRIDGE_VLANDB_ENTRY 嵌套属性，逐个解析 VLAN 条目：
 *          - 提取 VID、VLAN 范围 vrange、状态 state。
 *          - 将解析结果动态追加到 vlan_array 中。
 *     5. 如果解析到 VLAN 条目，则把 vlan_array 保存到上下文，
 *        并将上下文入队到 zebra 主线程处理。
 *     6. 否则释放相关资源。
 *
 * 参数：
 *   h        当前 netlink 消息头
 *   ns_id    网络命名空间 ID
 *   startup  是否处于启动阶段（本函数未使用）
 *
 * 返回值：
 *   0    成功处理或有意忽略
 *  -1   消息格式错误
 */
int netlink_vlan_change(struct nlmsghdr *h, ns_id_t ns_id, int startup)
{
	int len, rem;                                       /* 属性区长度、剩余长度 */
	struct br_vlan_msg *bvm;                            /* 桥接 VLAN 消息体 */
	struct bridge_vlan_info *vinfo;                     /* VLAN 信息（VID 等） */
	struct rtattr *vtb[BRIDGE_VLANDB_ENTRY_MAX + 1] = {}; /* 嵌套属性表 */
	struct rtattr *attr;                                /* 当前遍历的属性 */
	uint8_t state;                                      /* VLAN 状态 */
	uint32_t vrange;                                    /* VLAN 范围 */
	int type;                                           /* 属性类型 */
	uint32_t count = 0;                                 /* 已解析 VLAN 条目数 */
	struct zebra_dplane_ctx *ctx = NULL;                /* 数据平面上下文 */
	struct zebra_vxlan_vlan_array *vlan_array = NULL;   /* VLAN 数组，存放解析结果 */

	/* We only care about state changes for now */
	/*
	 * 目前只关心 RTM_NEWVLAN 消息（新增或状态变化）。
	 * 其他类型（例如 RTM_DELVLAN）直接忽略。
	 */
	if (!(h->nlmsg_type == RTM_NEWVLAN))
		return 0;

	/*
	 * 计算属性区长度：
	 *   h->nlmsg_len                      整个 netlink 消息长度
	 *   NLMSG_LENGTH(sizeof(br_vlan_msg)) 消息头 + br_vlan_msg 的长度
	 * 差值即为属性区域（rtattr 数组）的字节数。
	 */
	len = h->nlmsg_len - NLMSG_LENGTH(sizeof(struct br_vlan_msg));

	/* 长度非法，消息被截断或格式错误 */
	if (len < 0) {
		zlog_warn(
			"%s: Message received from netlink is of a broken size %d %zu",
			__func__, h->nlmsg_len,
			(size_t)NLMSG_LENGTH(sizeof(struct br_vlan_msg)));
		return -1;
	}

	/* 获取 netlink 消息数据部分，即 br_vlan_msg */
	bvm = NLMSG_DATA(h);

	/*
	 * 只处理 AF_BRIDGE 地址族的消息。
	 * 其他地址族（如 AF_INET、AF_INET6）不是桥接 VLAN 消息，忽略。
	 */
	if (bvm->family != AF_BRIDGE)
		return 0;

	/* 分配数据平面上下文 */
	ctx = dplane_ctx_alloc();

	/* 保存命名空间 ID */
	dplane_ctx_set_ns_id(ctx, ns_id);

	/* 设置操作类型为 VLAN 安装（VLAN 变更统一使用该操作码） */
	dplane_ctx_set_op(ctx, DPLANE_OP_VLAN_INSTALL);

	/* 保存 VLAN 消息中携带的接口索引 */
	dplane_ctx_set_vlan_ifindex(ctx, bvm->ifindex);

	/* Loop over "ALL" BRIDGE_VLANDB_ENTRY */
	/*
	 * 遍历 VLAN 消息中的所有属性，查找并解析
	 * BRIDGE_VLANDB_ENTRY 类型的嵌套属性。
	 *
	 * BRVLAN_RTA(bvm) 指向 br_vlan_msg 之后的属性区起始位置。
	 * RTA_OK / RTA_NEXT 用于逐条遍历属性。
	 */
	rem = len;
	for (attr = BRVLAN_RTA(bvm); RTA_OK(attr, rem);
	     attr = RTA_NEXT(attr, rem)) {
		vinfo = NULL;
		vrange = 0;

		/* 获取属性类型（去除 NLA_F_NESTED 等标志位） */
		type = attr->rta_type & NLA_TYPE_MASK;

		/* 只处理 BRIDGE_VLANDB_ENTRY 类型的属性 */
		if (type != BRIDGE_VLANDB_ENTRY)
			continue;

		/*
		 * 解析嵌套属性，将 VLAN 条目的子属性填入 vtb 数组。
		 * 例如 BRIDGE_VLANDB_ENTRY_INFO、BRIDGE_VLANDB_ENTRY_STATE 等。
		 */
		netlink_parse_rtattr_nested(vtb, BRIDGE_VLANDB_ENTRY_MAX, attr);

		/*
		 * VLAN 条目必须包含 INFO 属性（至少要有 VID），
		 * 否则跳过该条目。
		 */
		if (!vtb[BRIDGE_VLANDB_ENTRY_INFO])
			continue;

		/* 提取 VLAN 信息结构（包含 VID、flags 等） */
		vinfo = (struct bridge_vlan_info *)RTA_DATA(
			vtb[BRIDGE_VLANDB_ENTRY_INFO]);

		/*
		 * We only care about state info, if there is none, just ignore
		 * it.
		 */
		/*
		 * 只关心状态信息（state）。如果没有 STATE 属性，
		 * 说明该条目不含我们需要的信息，跳过。
		 */
		if (!vtb[BRIDGE_VLANDB_ENTRY_STATE])
			continue;

		/* 已解析到一个有效 VLAN 条目，计数加一 */
		count++;

		/*
		 * 动态扩容 vlan_array，使其能容纳 count 个 zebra_vxlan_vlan 元素。
		 * 数组结构为：
		 *   struct zebra_vxlan_vlan_array {
		 *       uint32_t count;
		 *       struct zebra_vxlan_vlan vlans[];
		 *   };
		 * 因此分配大小为“结构头 + count * 单元素大小”。
		 */
		vlan_array =
			XREALLOC(MTYPE_VLAN_CHANGE_ARR, vlan_array,
				 sizeof(struct zebra_vxlan_vlan_array) +
					 count * sizeof(struct zebra_vxlan_vlan));

		/* 将新添加的元素清零，避免残留垃圾数据 */
		memset(&vlan_array->vlans[count - 1], 0,
		       sizeof(struct zebra_vxlan_vlan));

		/* 提取 VLAN 状态（如 LEARNING、FORWARDING 等） */
		state = *(uint8_t *)RTA_DATA(vtb[BRIDGE_VLANDB_ENTRY_STATE]);

		/* 如果存在 VLAN 范围属性，则提取范围值 */
		if (vtb[BRIDGE_VLANDB_ENTRY_RANGE])
			vrange = *(uint32_t *)RTA_DATA(
				vtb[BRIDGE_VLANDB_ENTRY_RANGE]);

		/*
		 * 保存解析结果到 vlan_array 的当前元素：
		 *   - state：转换为 zebra 内部状态
		 *   - vid：VLAN ID
		 *   - vrange：VLAN 范围（如 10-20 中的范围长度或终止值）
		 */
		vlan_array->vlans[count - 1].state =
			netlink_get_dplane_vlan_state(state);
		vlan_array->vlans[count - 1].vid = vinfo->vid;
		vlan_array->vlans[count - 1].vrange = vrange;
	}

	if (count) {
		/*
		 * 如果解析到至少一个 VLAN 条目：
		 *   - 设置 vlan_array 中的 count
		 *   - 将 vlan_array 保存到上下文
		 *   - 将上下文入队到 zebra 主线程处理
		 */
		vlan_array->count = count;
		dplane_ctx_set_vxlan_vlan_array(ctx, vlan_array);

		if (IS_ZEBRA_DEBUG_KERNEL || IS_ZEBRA_DEBUG_VXLAN)
			zlog_debug("RTM_NEWVLAN for ifindex %u NS %u, enqueuing for zebra main",
				   bvm->ifindex, ns_id);

		/* 将上下文投递给 zebra 主线程处理 */
		dplane_provider_enqueue_to_zebra(ctx);
	} else {
		/*
		 * 没有解析到任何有效 VLAN 条目：
		 *   - 如果 vlan_array 已被分配（理论上不会走到这里，因为
		 *     count 为 0 时 vlan_array 仍为 NULL），则释放它
		 *   - 释放上下文
		 */
		if (vlan_array) {
			/* Free allocated memory if count is 0 */
			XFREE(MTYPE_VLAN_CHANGE_ARR, vlan_array);
		}
		dplane_ctx_fini(&ctx);
	}

	/* 处理完成，返回 0 */
	return 0;
}

/**
 * netlink_request_vlan() - Request vlan information from the kernel
 * @zns:	Zebra namespace
 * @family:	AF_* netlink family
 * @type:	RTM_* type
 *
 * Return:	Result status
 */
static int netlink_request_vlan(struct zebra_ns *zns, int family, int type)
{
	struct {
		struct nlmsghdr n;
		struct br_vlan_msg bvm;
		char buf[256];
	} req;

	/* Form the request, specifying filter (rtattr) if needed. */
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_type = type;
	req.n.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct br_vlan_msg));
	req.bvm.family = family;

	if (!nl_attr_put32(&req.n, sizeof(req), BRIDGE_VLANDB_DUMP_FLAGS,
			   BRIDGE_VLANDB_DUMPF_STATS)) {
		zlog_err("%s: Failed to put BRIDGE_VLANDB_DUMP_FLAGS nl attribute",
			__func__);
		return -1;
	}

	return netlink_request(&zns->netlink_cmd, &req);
}

/**
 * netlink_vlan_read() - Vlan read function using netlink interface
 *
 * @zns:	Zebra name space
 *
 * Return:	Result status
 * Only called at bootstrap time.
 */
/*
 * netlink_vlan_read：
 *   使用 netlink 接口读取 VLAN 信息。
 *
 *   使用场景：
 *     该函数仅在 zebra 启动阶段（bootstrap）被调用，用于从内核
 *     获取当前所有桥接 VLAN 信息，并更新 zebra 内部的 VLAN 数据结构。
 *
 *   主要流程：
 *     1. 从 zebra 命名空间构造数据平面信息 dp_info，标记为命令 socket。
 *     2. 通过 netlink_request_vlan() 向内核发送 RTM_GETVLAN 请求，
 *        触发内核 dump 当前所有 VLAN 信息。
 *     3. 通过 netlink_parse_info() 解析内核返回的消息，
 *        使用 netlink_vlan_change() 作为回调处理每条 VLAN 变更。
 *
 * 参数：
 *   zns  zebra 命名空间，包含 netlink socket 及命名空间相关信息
 *
 * 返回值：
 *   < 0  发送请求失败，或解析过程中出现错误
 *   >= 0 解析成功（通常为 0）
 */
int netlink_vlan_read(struct zebra_ns *zns)
{
	int ret;                              /* 通用返回值 */
	struct zebra_dplane_info dp_info;     /* 数据平面信息，用于携带命名空间等上下文 */

	/*
	 * 从 zns 中提取关键信息填充 dp_info。
	 * 第三个参数 true 表示这是一个“命令 socket”（is_cmd = true），
	 * 会影响后续错误处理与消息分发逻辑。
	 */
	zebra_dplane_info_from_zns(&dp_info, zns, true /*is_cmd*/);

	/* Get bridge vlan info */
	/*
	 * 向内核发送 RTM_GETVLAN 请求，获取桥接 VLAN 信息。
	 *
	 * 参数说明：
	 *   zns         zebra 命名空间，提供命令 socket
	 *   PF_BRIDGE   协议族为桥接，表示请求的是桥接相关的 VLAN 信息
	 *   RTM_GETVLAN netlink 消息类型，表示获取 VLAN 信息
	 *
	 * 发送失败时直接返回错误码。
	 */
	ret = netlink_request_vlan(zns, PF_BRIDGE, RTM_GETVLAN);
	if (ret < 0)
		return ret;

	/*
	 * 解析内核返回的消息。
	 *
	 * 参数说明：
	 *   netlink_vlan_change      处理每条 VLAN 变更消息的回调
	 *   &zns->netlink_cmd        命令 socket 结构，用于接收响应
	 *   &dp_info                 数据平面信息，会传递给回调
	 *   0                        读取数量限制，0 表示尽可能多地读取
	 *   1                        表示处于启动阶段（startup = true）
	 *
	 * 回调 netlink_vlan_change() 会把解析出的 VLAN 信息更新到
	 * zebra 内部的桥接/VLAN 数据结构中。
	 */
	ret = netlink_parse_info(netlink_vlan_change, &zns->netlink_cmd,
				 &dp_info, 0, 1);

	/* 返回解析结果 */
	return ret;
}

#endif /* GNU_LINUX */
