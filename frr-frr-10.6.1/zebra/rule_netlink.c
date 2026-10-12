// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zebra Policy Based Routing (PBR) interaction with the kernel using
 * netlink.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 */

#include <zebra.h>

#ifdef HAVE_NETLINK

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "if.h"
#include "prefix.h"
#include "vrf.h"

#include <linux/fib_rules.h>
#include "zebra/zserv.h"
#include "zebra/zebra_ns.h"
#include "zebra/zebra_vrf.h"
#include "zebra/rt.h"
#include "zebra/interface.h"
#include "zebra/debug.h"
#include "zebra/rtadv.h"
#include "zebra/kernel_netlink.h"
#include "zebra/rule_netlink.h"
#include "zebra/zebra_pbr.h"
#include "zebra/zebra_errors.h"
#include "zebra/zebra_dplane.h"
#include "zebra/zebra_trace.h"
#include "lib/netlink_parser.h"

/* definitions */

/* static function declarations */

/* Private functions */


/*
 * netlink_rule_msg_encode
 *
 * Encodes netlink RTM_ADDRULE/RTM_DELRULE message to buffer buf of size buflen.
 *
 * Returns -1 on failure, 0 when the msg doesn't fit entirely in the buffer
 * or the number of bytes written to buf.
 */
static ssize_t netlink_rule_msg_encode(
	int cmd, const struct zebra_dplane_ctx *ctx, uint32_t filter_bm,
	uint32_t priority, uint32_t table, const struct prefix *src_ip,
	const struct prefix *dst_ip, uint32_t fwmark, uint8_t dsfield,
	uint8_t ip_protocol, void *buf, size_t buflen)
{
	uint8_t protocol = RTPROT_ZEBRA;
	int family;
	int bytelen;
	struct {
		struct nlmsghdr n;
		struct fib_rule_hdr frh;
		char buf[];
	} *req = buf;

	const char *ifname = dplane_ctx_rule_get_ifname(ctx);

	if (buflen < sizeof(*req))
		return 0;
	memset(req, 0, sizeof(*req));

	/* Assume ipv4 if no src/dst set, we only support ipv4/ipv6 */
	if (PREFIX_FAMILY(src_ip))
		family = PREFIX_FAMILY(src_ip);
	else if (PREFIX_FAMILY(dst_ip))
		family = PREFIX_FAMILY(dst_ip);
	else
		family = AF_INET;

	bytelen = (family == AF_INET ? 4 : 16);

	req->n.nlmsg_type = cmd;
	req->n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req->n.nlmsg_flags = NLM_F_REQUEST;

	req->frh.family = family;
	req->frh.action = FR_ACT_TO_TBL;

	if (!nl_attr_put(&req->n, buflen, FRA_PROTOCOL, &protocol,
			 sizeof(protocol)))
		return 0;

	/* rule's pref # */
	if (!nl_attr_put32(&req->n, buflen, FRA_PRIORITY, priority))
		return 0;

	/* interface on which applied */
	if (!nl_attr_put(&req->n, buflen, FRA_IFNAME, ifname,
			 strlen(ifname) + 1))
		return 0;

	/* source IP, if specified */
	if (filter_bm & PBR_FILTER_SRC_IP) {
		req->frh.src_len = src_ip->prefixlen;
		if (!nl_attr_put(&req->n, buflen, FRA_SRC, &src_ip->u.prefix,
				 bytelen))
			return 0;
	}

	/* destination IP, if specified */
	if (filter_bm & PBR_FILTER_DST_IP) {
		req->frh.dst_len = dst_ip->prefixlen;
		if (!nl_attr_put(&req->n, buflen, FRA_DST, &dst_ip->u.prefix,
				 bytelen))
			return 0;
	}

	/* fwmark, if specified */
	if (filter_bm & PBR_FILTER_FWMARK) {
		if (!nl_attr_put32(&req->n, buflen, FRA_FWMARK, fwmark))
			return 0;
	}

	/* dsfield, if specified; mask off the ECN bits */
	if (filter_bm & PBR_FILTER_DSCP)
		req->frh.tos = dsfield & PBR_DSFIELD_DSCP;

	/* protocol to match on */
	if (filter_bm & PBR_FILTER_IP_PROTOCOL)
		nl_attr_put8(&req->n, buflen, FRA_IP_PROTO, ip_protocol);

	/* Route table to use to forward, if filter criteria matches. */
	if (table < 256)
		req->frh.table = table;
	else {
		req->frh.table = RT_TABLE_UNSPEC;
		if (!nl_attr_put32(&req->n, buflen, FRA_TABLE, table))
			return 0;
	}

	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug(
			"Tx %s family %s IF %s Pref %u Fwmark %u Src %pFX Dst %pFX Table %u",
			nl_msg_type_to_str(cmd), nl_family_to_str(family),
			ifname, priority, fwmark, src_ip, dst_ip, table);

	return NLMSG_ALIGN(req->n.nlmsg_len);
}

static ssize_t netlink_rule_msg_encoder(struct zebra_dplane_ctx *ctx, void *buf,
					size_t buflen)
{
	int cmd = RTM_NEWRULE;

	if (dplane_ctx_get_op(ctx) == DPLANE_OP_RULE_DELETE)
		cmd = RTM_DELRULE;

	return netlink_rule_msg_encode(
		cmd, ctx, dplane_ctx_rule_get_filter_bm(ctx),
		dplane_ctx_rule_get_priority(ctx),
		dplane_ctx_rule_get_table(ctx), dplane_ctx_rule_get_src_ip(ctx),
		dplane_ctx_rule_get_dst_ip(ctx),
		dplane_ctx_rule_get_fwmark(ctx),
		dplane_ctx_rule_get_dsfield(ctx),
		dplane_ctx_rule_get_ipproto(ctx), buf, buflen);
}

static ssize_t netlink_oldrule_msg_encoder(struct zebra_dplane_ctx *ctx,
					   void *buf, size_t buflen)
{
	return netlink_rule_msg_encode(
		RTM_DELRULE, ctx, dplane_ctx_rule_get_old_filter_bm(ctx),
		dplane_ctx_rule_get_old_priority(ctx),
		dplane_ctx_rule_get_old_table(ctx),
		dplane_ctx_rule_get_old_src_ip(ctx),
		dplane_ctx_rule_get_old_dst_ip(ctx),
		dplane_ctx_rule_get_old_fwmark(ctx),
		dplane_ctx_rule_get_old_dsfield(ctx),
		dplane_ctx_rule_get_old_ipproto(ctx), buf, buflen);
}

/*
 * Identify valid rule actions for netlink - other actions can't be installed
 */
static bool nl_rule_valid_action(uint32_t action)
{
	if (action == PBR_ACTION_TABLE)
		return true;
	else
		return false;
}

/* Public functions */

enum netlink_msg_status
netlink_put_rule_update_msg(struct nl_batch *bth, struct zebra_dplane_ctx *ctx)
{
	enum dplane_op_e op;
	enum netlink_msg_status ret;
	struct pbr_rule rule = {};

	op = dplane_ctx_get_op(ctx);
	if (!(op == DPLANE_OP_RULE_ADD || op == DPLANE_OP_RULE_UPDATE
	      || op == DPLANE_OP_RULE_DELETE)) {
		flog_err(
			EC_ZEBRA_PBR_RULE_UPDATE,
			"Context received for kernel rule update with incorrect OP code (%u)",
			op);
		return FRR_NETLINK_ERROR;
	}

	/* TODO -- special handling for rules that include actions that
	 * netlink cannot install. Some of the rule attributes are not
	 * available in netlink: only try to install valid actions.
	 */
	dplane_ctx_rule_get(ctx, &rule, NULL);
	if (!nl_rule_valid_action(rule.action.flags)) {
		if (IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("%s: skip invalid action %#x", __func__,
				   rule.action.flags);
		return 0;
	}

	ret = netlink_batch_add_msg(bth, ctx, netlink_rule_msg_encoder, false);

	/**
	 * Delete the old one.
	 *
	 * Don't care about this result right?
	 */
	if (op == DPLANE_OP_RULE_UPDATE)
		netlink_batch_add_msg(bth, ctx, netlink_oldrule_msg_encoder,
				      true);

	return ret;
}

/*
 * netlink_rule_change：
 *   解析并处理内核发来的策略路由（PBR）规则变更消息。
 *
 *   主要流程：
 *     1. 校验消息类型（RTM_NEWRULE / RTM_DELRULE）。
 *     2. 校验消息长度和地址族（仅处理 IPv4 / IPv6）。
 *     3. 解析规则属性（优先级、源/目的地址、表、接口、协议等）。
 *     4. 根据消息类型分别处理：
 *          - RTM_NEWRULE：启动阶段清理本进程遗留规则；运行时不处理。
 *          - RTM_DELRULE：在接口存在的前提下，通知 zebra 删除 PBR 规则。
 *
 * 参数：
 *   h        当前 netlink 消息头
 *   ns_id    消息所属的网络命名空间 ID
 *   startup  是否处于启动阶段
 *
 * 返回值：
 *   0    成功处理或有意忽略
 *  -1   消息格式错误
 *   其他 由 kernel_pbr_rule_del() 等函数返回的结果
 */
int netlink_rule_change(struct nlmsghdr *h, ns_id_t ns_id, int startup)
{
	struct zebra_ns *zns;                    /* zebra 命名空间 */
	struct fib_rule_hdr *frh;                /* 规则消息体 */
	struct rtattr *tb[FRA_MAX + 1];          /* 解析后的规则属性表 */
	int len;                                 /* 属性区长度 */
	char *ifname;                            /* 接口名称 */
	struct zebra_pbr_rule rule = {};         /* zebra 内部的 PBR 规则结构 */
	uint8_t proto = 0;                       /* 规则协议（如 RTPROT_ZEBRA） */
	uint8_t ip_proto = 0;                    /* IP 协议号（如 TCP/UDP） */

	/* 上报 trace，便于问题定位 */
	frrtrace(3, frr_zebra, netlink_rule_change, h, ns_id, startup);

	/* Basic validation followed by extracting attributes. */
	/*
	 * 校验消息类型：只处理 RTM_NEWRULE 和 RTM_DELRULE。
	 * 其他类型直接忽略。
	 */
	if (h->nlmsg_type != RTM_NEWRULE && h->nlmsg_type != RTM_DELRULE)
		return 0;

	/*
	 * 计算属性区长度：
	 *   h->nlmsg_len                          整个 netlink 消息长度
	 *   NLMSG_LENGTH(sizeof(fib_rule_hdr))    消息头 + fib_rule_hdr 长度
	 * 差值即为属性区域（rtattr 数组）的字节数。
	 */
	len = h->nlmsg_len - NLMSG_LENGTH(sizeof(struct fib_rule_hdr));

	/* 长度非法，消息格式错误 */
	if (len < 0) {
		flog_err(EC_ZEBRA_NETLINK_LENGTH_ERROR,
			 "%s: Message received from netlink is of a broken size: %d %zu", __func__,
			 h->nlmsg_len, (size_t)NLMSG_LENGTH(sizeof(struct fib_rule_hdr)));
		return -1;
	}

	/* 获取 netlink 消息数据部分，即 fib_rule_hdr */
	frh = NLMSG_DATA(h);

	/*
	 * 校验地址族：
	 *   - AF_INET / AF_INET6：处理
	 *   - RTNL_FAMILY_IPMR / RTNL_FAMILY_IP6MR：多播路由规则，忽略
	 *   - 其他：告警并忽略
	 */
	if (frh->family != AF_INET && frh->family != AF_INET6) {
		if (frh->family == RTNL_FAMILY_IPMR
		    || frh->family == RTNL_FAMILY_IP6MR) {
			/* 多播路由规则，目前不支持，记录调试信息并忽略 */
			if (IS_ZEBRA_DEBUG_KERNEL)
				zlog_debug(
					"Received rule netlink that we are ignoring for family %u, rule change: %u",
					frh->family, h->nlmsg_type);
			return 0;
		}
		/* 其他未知地址族，记录告警并忽略 */
		flog_warn(
			EC_ZEBRA_NETLINK_INVALID_AF,
			"Invalid address family: %u received from kernel rule change: %u",
			frh->family, h->nlmsg_type);
		return 0;
	}

	/*
	 * 只处理 FR_ACT_TO_TBL（转发到路由表）类型的规则，
	 * 其他 action（如 FR_ACT_BLACKHOLE、FR_ACT_PROHIBIT 等）忽略。
	 */
	if (frh->action != FR_ACT_TO_TBL)
		return 0;

	/* 解析 netlink 属性到 tb 数组 */
	memset(tb, 0, sizeof(tb));
	netlink_parse_rtattr(tb, FRA_MAX, RTM_RTA(frh), len);

	/* 提取规则优先级（FRA_PRIORITY） */
	if (tb[FRA_PRIORITY])
		rule.rule.priority = *(uint32_t *)RTA_DATA(tb[FRA_PRIORITY]);

	/* 提取源地址过滤条件（FRA_SRC） */
	if (tb[FRA_SRC]) {
		if (frh->family == AF_INET)
			/* IPv4：拷贝 4 字节源地址 */
			memcpy(&rule.rule.filter.src_ip.u.prefix4,
			       RTA_DATA(tb[FRA_SRC]), 4);
		else
			/* IPv6：拷贝 16 字节源地址 */
			memcpy(&rule.rule.filter.src_ip.u.prefix6,
			       RTA_DATA(tb[FRA_SRC]), 16);
		/* 设置源地址前缀长度、地址族，并标记过滤位图 */
		rule.rule.filter.src_ip.prefixlen = frh->src_len;
		rule.rule.filter.src_ip.family = frh->family;
		rule.rule.filter.filter_bm |= PBR_FILTER_SRC_IP;
	}

	/* 提取目的地址过滤条件（FRA_DST） */
	if (tb[FRA_DST]) {
		if (frh->family == AF_INET)
			/* IPv4：拷贝 4 字节目的地址 */
			memcpy(&rule.rule.filter.dst_ip.u.prefix4,
			       RTA_DATA(tb[FRA_DST]), 4);
		else
			/* IPv6：拷贝 16 字节目的地址 */
			memcpy(&rule.rule.filter.dst_ip.u.prefix6,
			       RTA_DATA(tb[FRA_DST]), 16);
		/* 设置目的地址前缀长度、地址族，并标记过滤位图 */
		rule.rule.filter.dst_ip.prefixlen = frh->dst_len;
		rule.rule.filter.dst_ip.family = frh->family;
		rule.rule.filter.filter_bm |= PBR_FILTER_DST_IP;
	}

	/*
	 * 提取目标路由表 ID：
	 *   优先使用 FRA_TABLE 属性，否则使用 fib_rule_hdr 中的 table 字段。
	 */
	if (tb[FRA_TABLE])
		rule.rule.action.table = *(uint32_t *)RTA_DATA(tb[FRA_TABLE]);
	else
		rule.rule.action.table = frh->table;

	/* TBD: We don't care about rules not specifying an IIF. */
	/*
	 * 目前不关心未指定入接口（IIF）的规则，直接忽略。
	 */
	if (tb[FRA_IFNAME] == NULL)
		return 0;

	/* 提取规则协议（FRA_PROTOCOL），如 RTPROT_ZEBRA */
	if (tb[FRA_PROTOCOL])
		proto = *(uint8_t *)RTA_DATA(tb[FRA_PROTOCOL]);

	/* 提取 IP 协议号（FRA_IP_PROTO），如 TCP/UDP/ICMP */
	if (tb[FRA_IP_PROTO])
		ip_proto = *(uint8_t *)RTA_DATA(tb[FRA_IP_PROTO]);

	/* 提取接口名并拷贝到规则结构中 */
	ifname = (char *)RTA_DATA(tb[FRA_IFNAME]);
	strlcpy(rule.ifname, ifname, sizeof(rule.ifname));

	if (h->nlmsg_type == RTM_NEWRULE) {
		/*
		 * ============================
		 * 新增规则分支（RTM_NEWRULE）
		 * ============================
		 */
		/*
		 * If we see a rule at startup we created, delete it now.
		 * It should have been flushed on a previous shutdown.
		 */
		/*
		 * 如果启动阶段发现本进程（RTPROT_ZEBRA）之前创建的残留规则，
		 * 立即删除它。正常情况下，这些规则应该在之前的关闭过程中
		 * 被清理，但可能由于异常退出而残留。
		 */
		if (startup && proto == RTPROT_ZEBRA) {
			enum zebra_dplane_result ret;

			/* 通过数据平面删除该残留规则 */
			ret = dplane_pbr_rule_delete(&rule);

			zlog_debug(
				"%s: %s leftover rule: family %s IF %s Pref %u Src %pFX Dst %pFX Table %u ip-proto: %u",
				__func__,
				((ret == ZEBRA_DPLANE_REQUEST_FAILURE)
					 ? "Failed to remove"
					 : "Removed"),
				nl_family_to_str(frh->family), rule.ifname,
				rule.rule.priority, &rule.rule.filter.src_ip,
				&rule.rule.filter.dst_ip,
				rule.rule.action.table, ip_proto);
		}

		/* TBD */
		/*
		 * 目前不处理运行时的 RTM_NEWRULE 消息。
		 * 原因：正常情况下，规则的添加是由 zebra 主动发起的，
		 * 内核通知只是用来同步外部添加的规则。对于新增规则，
		 * 目前尚未实现同步逻辑，留待后续扩展。
		 */
		return 0;
	}

	/* ============================
	 * 删除规则分支（RTM_DELRULE）
	 * ============================
	 */
	zns = zebra_ns_lookup(ns_id);

	/* If we don't know the interface, we don't care. */
	/*
	 * 如果该接口在 zebra 中不存在，说明不是我们关心的接口，忽略该规则删除。
	 */
	if (!if_lookup_by_name_per_ns(zns, ifname))
		return 0;

	/* 调试日志：打印收到的规则删除消息 */
	if (IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug(
			"Rx %s family %s IF %s Pref %u Src %pFX Dst %pFX Table %u ip-proto: %u",
			nl_msg_type_to_str(h->nlmsg_type),
			nl_family_to_str(frh->family), rule.ifname,
			rule.rule.priority, &rule.rule.filter.src_ip,
			&rule.rule.filter.dst_ip, rule.rule.action.table,
			ip_proto);

	/*
	 * 通知 zebra 内核删除对应的 PBR 规则。
	 * kernel_pbr_rule_del() 会根据规则内容在内核 PBR 规则表中
	 * 找到并删除对应条目。
	 */
	return kernel_pbr_rule_del(&rule);
}

/*
 * Request rules from the kernel
 */
static int netlink_request_rules(struct zebra_ns *zns, int family, int type)
{
	struct {
		struct nlmsghdr n;
		struct fib_rule_hdr frh;
		char buf[NL_PKT_BUF_SIZE];
	} req;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_type = type;
	req.n.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct fib_rule_hdr));
	req.frh.family = family;

	return netlink_request(&zns->netlink_cmd, &req);
}

/*
 * Get to know existing PBR rules in the kernel - typically called at startup.
 */
/*
 * netlink_rules_read：
 *   从内核读取已有的 PBR（策略路由）规则，通常在启动阶段调用。
 *
 *   该函数在 zebra 启动流程中被调用（由 zebra_ns_startup_continue() 在
 *   ZEBRA_DPLANE_ADDRESSES_READ 阶段触发），用于：
 *     1. 构造数据平面信息 dp_info（命令 socket 上下文）。
 *     2. 向内核请求 IPv4 规则并解析。
 *     3. 向内核请求 IPv6 规则并解析。
 *
 *   解析回调 netlink_rule_change() 会把每条规则更新到 zebra 内部的
 *   PBR 数据结构中，确保启动后 zebra 掌握内核中已有的策略路由规则。
 *
 * 参数：
 *   zns  zebra 命名空间
 *
 * 返回值：
 *   < 0  请求发送失败或解析过程中出错
 *   0    解析成功
 */
int netlink_rules_read(struct zebra_ns *zns)
{
	int ret;                              /* 通用返回值 */
	struct zebra_dplane_info dp_info;     /* 数据平面信息，携带命名空间上下文 */

	/*
	 * 从 zns 中提取关键信息填充 dp_info。
	 * 第三个参数 true 表示这是命令 socket（is_cmd），
	 * 影响后续错误处理和消息分发逻辑。
	 */
	zebra_dplane_info_from_zns(&dp_info, zns, true);

	/*
	 * 第一步：请求 IPv4 规则。
	 *
	 * 参数说明：
	 *   zns           zebra 命名空间，提供命令 socket
	 *   AF_INET       地址族为 IPv4
	 *   RTM_GETRULE   netlink 消息类型，表示获取策略路由规则
	 *
	 * 该请求会触发内核 dump 当前所有 IPv4 规则。
	 * 发送失败时直接返回错误码。
	 */
	ret = netlink_request_rules(zns, AF_INET, RTM_GETRULE);
	if (ret < 0)
		return ret;

	/*
	 * 解析内核返回的 IPv4 规则消息。
	 *
	 * 参数说明：
	 *   netlink_rule_change   处理规则变更消息的回调
	 *   &zns->netlink_cmd     命令 socket 结构，用于接收响应
	 *   &dp_info              数据平面信息，会传递给回调
	 *   0                     读取数量限制，0 表示尽可能多地读取
	 *   true                  启动阶段标志（startup = true）
	 *
	 * 回调 netlink_rule_change() 会解析每条规则消息，
	 * 提取规则属性并更新 zebra 内部的 PBR 规则表。
	 */
	ret = netlink_parse_info(netlink_rule_change, &zns->netlink_cmd,
				 &dp_info, 0, true);
	if (ret < 0)
		return ret;

	/*
	 * 第二步：请求 IPv6 规则。
	 *
	 * 参数说明：
	 *   zns           zebra 命名空间，提供命令 socket
	 *   AF_INET6      地址族为 IPv6
	 *   RTM_GETRULE   netlink 消息类型，表示获取策略路由规则
	 *
	 * 该请求会触发内核 dump 当前所有 IPv6 规则。
	 */
	ret = netlink_request_rules(zns, AF_INET6, RTM_GETRULE);
	if (ret < 0)
		return ret;

	/*
	 * 解析内核返回的 IPv6 规则消息，处理方式与 IPv4 相同。
	 * netlink_rule_change() 会根据消息中的地址族分别处理
	 * IPv4 或 IPv6 规则。
	 */
	ret = netlink_parse_info(netlink_rule_change, &zns->netlink_cmd,
				 &dp_info, 0, true);

	/* 返回最后一次解析的结果（成功时为 0） */
	return ret;
}

#endif /* HAVE_NETLINK */
