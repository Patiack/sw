// SPDX-License-Identifier: GPL-2.0-or-later
/* setsockopt functions
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * ============================================================
 * 文件导读：sockopt.c —— socket 选项设置的平台无关封装实现
 * ============================================================
 *
 * 这个文件在 FRR 里干什么
 *   把各守护进程反复用到的 setsockopt / getsockopt 调用集中封装，并把
 *   Linux、FreeBSD / NetBSD / OpenBSD、Solaris 等平台的差异用 #ifdef
 *   收拢在函数内部。上层只需调用统一的接口名即可。
 *
 * 代码组织（自顶向下阅读）
 *   1. 平台探测宏：HAVE_BSD_STRUCT_IP_MREQ_HACK 用于判定使用 BSD 版
 *      struct ip_mreq 组播接口语义。
 *   2. SO_RCVBUF / SO_SNDBUF 设置与读取：set / getsockopt_so_*buf。
 *      设置有“失败就减半重试”的退避逻辑（内核会裁剪上限）。
 *   3. IPv6 选项：pktinfo、组播 / 单播跳数、hoplimit、multicast_loop、
 *      tclass；以及内部辅助 getsockopt_ipv6_ifindex。
 *   4. IPv4 组播：setsockopt_ipv4_multicast（三套平台实现：RFC3678 /
 *      ip_mreqn / BSD ip_mreq）、multicast_if、multicast_loop。
 *   5. ifindex 收发：setsockopt_ipv4_ifindex、setsockopt_ifindex、
 *      getsockopt_ipv4_ifindex、getsockopt_ifindex；底层靠
 *      getsockopt_cmsg_data 解析辅助数据。
 *   6. IP_HDRINCL 字节序转换：sockopt_iphdrincl_swab_htosys / _systoh。
 *   7. TCP 相关：sockopt_tcp_rtt、TCP MD5 签名（ext 为真正实现）、
 *      TCP MSS 设 / 取、TCP keepalive、IP_TRANSPARENT。
 *
 * 核心数据结构
 *   struct msghdr / struct cmsghdr —— 用于从收包辅助数据里取 ifindex；
 *   struct ip_mreq / ip_mreqn / group_req —— 三种组播接口表示；
 *   struct tcp_md5sig —— Linux TCP MD5 密钥配置载体；
 *   union sockunion —— 地址族无关的对端地址（来自 sockunion.h）。
 *
 * 与其他模块的关系
 *   被 bgpd / ospfd / ospf6d / isisd / pimd 等广泛调用；依赖 log.h 打
 *   日志、lib_errors.h 的错误码（EC_LIB_*）、sockunion.h 的地址封装。
 *
 * 阅读顺序建议
 *   先看 SO_*BUF 两个函数了解退避套路，再看 ifindex 收发函数对
 *   （setsockopt_ifindex / getsockopt_ifindex），最后看 TCP MD5 与
 *   keepalive 这类 BGP 强相关的部分。
 * ============================================================
 */

#include <zebra.h>

#ifndef __linux__
#include <net/if_dl.h>
#endif

#include "log.h"
#include "sockopt.h"
#include "sockunion.h"
#include "lib_errors.h"

/*
 * 平台探测：判定该平台使用“BSD 风格”的 struct ip_mreq 组播接口表示。
 * 具体覆盖版本区间：FreeBSD 4.4~5.0 与 5.0.22~7.0 之间、NetBSD 1.6.1
 * 及以后、OpenBSD、DragonFly、Solaris（__sun）。
 * 命中后定义 HAVE_BSD_STRUCT_IP_MREQ_HACK，供下面 setsockopt_ipv4_multicast*
 * 选择 BSD 分支实现。
 */
#if (defined(__FreeBSD__) &&                                                   \
     ((__FreeBSD_version >= 500022 && __FreeBSD_version < 700000) ||           \
      (__FreeBSD_version < 500000 && __FreeBSD_version >= 440000))) ||         \
	(defined(__NetBSD__) && defined(__NetBSD_Version__) &&                 \
	 __NetBSD_Version__ >= 106010000) ||                                   \
	defined(__OpenBSD__) || defined(__DragonFly__) || defined(__sun)
#define HAVE_BSD_STRUCT_IP_MREQ_HACK
#endif

/*
 * 设置接收缓冲区 SO_RCVBUF。
 * 参数：size 为请求字节数。
 * 返回值：实际被内核接受的 size（可能比请求值小）。
 * 退避逻辑：setsockopt 失败时把 size 逐次减半重试，直到成功或减到 0；
 *   若最终接受的 size 与请求不同，打印错误日志告知（内核有上限裁剪）。
 * 注意：即使参数名字面是“recvbuf”，Linux 下内核通常把该值翻倍后作为
 *   上限使用，读回值需用 getsockopt_so_recvbuf 获取。
 */
int setsockopt_so_recvbuf(int sock, int size)
{
	int orig_req = size;

	while (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) ==
	       -1) {
		if (size == 0)
			break;
		size /= 2;
	}

	if (size != orig_req)
		flog_err(EC_LIB_SOCKET,
			 "%s: fd %d: SO_RCVBUF set to %d (requested %d)",
			 __func__, sock, size, orig_req);

	return size;
}

/*
 * 设置发送缓冲区 SO_SNDBUF。逻辑与 setsockopt_so_recvbuf 完全对称：
 * 失败减半重试，返回内核实际接受的 size，请求值与实际不符则打印错误。
 */
int setsockopt_so_sendbuf(const int sock, int size)
{
	int orig_req = size;

	while (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) ==
	       -1) {
		if (size == 0)
			break;
		size /= 2;
	}

	if (size != orig_req)
		flog_err(EC_LIB_SOCKET,
			 "%s: fd %d: SO_SNDBUF set to %d (requested %d)",
			 __func__, sock, size, orig_req);

	return size;
}

/*
 * 读取当前发送缓冲区大小 SO_SNDBUF。
 * 返回值：读到的大小；失败返回负数并打印系统错误日志。
 * 注意：Linux 下读回值一般是用户设置值的两倍（内核内部计数方式所致）。
 */
int getsockopt_so_sendbuf(const int sock)
{
	uint32_t optval;
	socklen_t optlen = sizeof(optval);
	int ret = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&optval,
			     &optlen);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "fd %d: can't getsockopt SO_SNDBUF: %d (%s)", sock,
			     errno, safe_strerror(errno));
		return ret;
	}
	return optval;
}

/*
 * 读取当前接收缓冲区大小 SO_RCVBUF。逻辑与 getsockopt_so_sendbuf 对称，
 * 失败返回负数并打印系统错误日志。
 */
int getsockopt_so_recvbuf(const int sock)
{
	uint32_t optval;
	socklen_t optlen = sizeof(optval);
	int ret = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&optval,
			     &optlen);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "fd %d: can't getsockopt SO_RCVBUF: %d (%s)", sock,
			     errno, safe_strerror(errno));
		return ret;
	}
	return optval;
}

/*
 * 在收包消息 msgh 的辅助数据（control message）链表中，查找
 * level / type 匹配的控制消息，返回其数据区指针（CMSG_DATA）。
 * 参数：msgh 为已 recvmsg 的消息头；level / type 为要匹配的协议层与类型，
 *   例如 IPPROTO_IPV6 + IPV6_PKTINFO。
 * 返回值：找到则返回数据指针，否则返回 NULL。
 * 说明：这是各 getsockopt_*ifindex 的基础；调用方需自行按类型的结构体
 *   解释返回的内存。
 */
static void *getsockopt_cmsg_data(struct msghdr *msgh, int level, int type)
{
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msgh); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(msgh, cmsg))
		if (cmsg->cmsg_level == level && cmsg->cmsg_type == type)
			return CMSG_DATA(cmsg);

	return NULL;
}

/*
 * 打开 / 关闭 IPv6 收包信息（val 非 0 开启）。开启后 recvmsg 会附带
 * struct in6_pktinfo 辅助数据，用于取得目的地址与收包接口 ifindex。
 * 平台差异：2292bis 用 IPV6_RECVPKTINFO，RFC2292 老接口用 IPV6_PKTINFO。
 * 返回 0 成功、-1 失败（失败打印日志）。
 */
/* Set IPv6 packet info to the socket. */
int setsockopt_ipv6_pktinfo(int sock, int val)
{
	int ret;

#ifdef IPV6_RECVPKTINFO /*2292bis-01*/
	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &val,
			 sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET,
			 "can't setsockopt IPV6_RECVPKTINFO : %s",
			 safe_strerror(errno));
#else  /*RFC2292*/
	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_PKTINFO, &val, sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IPV6_PKTINFO : %s",
			 safe_strerror(errno));
#endif /* IANA_IPV6 */
	return ret;
}

/*
 * 设置 IPv6 组播跳数上限 IPV6_MULTICAST_HOPS（即组播报文 TTL）。
 * val 为跳数；返回 0 成功、-1 失败（失败打印日志）。
 */
/* Set multicast hops val to the socket. */
int setsockopt_ipv6_multicast_hops(int sock, int val)
{
	int ret;

	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &val,
			 sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IPV6_MULTICAST_HOPS");
	return ret;
}

/*
 * 设置 IPv6 单播跳数上限 IPV6_UNICAST_HOPS（即单播报文 TTL）。
 * val 为跳数；返回 0 成功、-1 失败（失败打印日志）。
 * 注意：此函数沿用了一句英文注释（与上方组播版重复），实际语义是单播。
 */
/* Set multicast hops val to the socket. */
int setsockopt_ipv6_unicast_hops(int sock, int val)
{
	int ret;

	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &val,
			 sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IPV6_UNICAST_HOPS");
	return ret;
}

/*
 * 请求内核把收包的 hop limit（跳数限制）作为辅助数据返回。
 * val 非 0 开启。2292bis 用 IPV6_RECVHOPLIMIT，RFC2292 老接口用
 * IPV6_HOPLIMIT。返回 0 成功、-1 失败（失败打印日志）。
 * 注意：这里设置的是“是否随包上报 hoplimit”，并非设置发送时的跳数上限。
 */
int setsockopt_ipv6_hoplimit(int sock, int val)
{
	int ret;

#ifdef IPV6_RECVHOPLIMIT /*2292bis-01*/
	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &val,
			 sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IPV6_RECVHOPLIMIT");
#else /*RFC2292*/
	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_HOPLIMIT, &val, sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IPV6_HOPLIMIT");
#endif
	return ret;
}

/*
 * 设置 IPv6 组播回环开关 IPV6_MULTICAST_LOOP。val 为 0 关闭回环、
 * 非 0 开启。返回 0 成功、-1 失败（失败打印日志）。
 */
/* Set multicast loop zero to the socket. */
int setsockopt_ipv6_multicast_loop(int sock, int val)
{
	int ret;

	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &val,
			 sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IPV6_MULTICAST_LOOP");
	return ret;
}

/*
 * 从已接收消息 msgh 的辅助数据中取出 IPv6 收包接口 ifindex。
 * 做法：定位 IPPROTO_IPV6 + IPV6_PKTINFO 控制消息，读取
 *   struct in6_pktinfo 的 ipi6_ifindex 字段。
 * 返回值：接口索引。
 * 坑：此实现未对 pktinfo 是否为 NULL 做判空，若未开启 pktinfo
 *   （setsockopt_ipv6_pktinfo）且内核未附带该 cmsg，会解引用空指针；
 *   与 IPv4 版 getsockopt_ipv4_ifindex 的判空处理不一致。
 */
static int getsockopt_ipv6_ifindex(struct msghdr *msgh)
{
	struct in6_pktinfo *pktinfo;

	pktinfo = getsockopt_cmsg_data(msgh, IPPROTO_IPV6, IPV6_PKTINFO);

	return pktinfo->ipi6_ifindex;
}

/*
 * 设置 IPv6 流量类别 IPV6_TCLASS（对应 IPv4 的 TOS / DSCP）。
 * tclass 为流量类别字节值。返回 0 成功、-1 失败。
 * 仅当平台定义了 IPV6_TCLASS（RFC3542）时才真正设置，否则 ret 保持 0。
 */
int setsockopt_ipv6_tclass(int sock, int tclass)
{
	int ret = 0;

#ifdef IPV6_TCLASS /* RFC3542 */
	ret = setsockopt(sock, IPPROTO_IPV6, IPV6_TCLASS, &tclass,
			 sizeof(tclass));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET,
			 "Can't set IPV6_TCLASS option for fd %d to %#x: %s",
			 sock, tclass, safe_strerror(errno));
#endif
	return ret;
}

/*
 * 以平台无关方式处理 IPv4 组播组成员操作（加入 / 离开组）。
 * 参数：
 *   sock        —— 已绑定接口的 socket。
 *   optname     —— IP_ADD_MEMBERSHIP 或 IP_DROP_MEMBERSHIP。
 *   if_addr     —— 本地接口地址（部分平台用到）。
 *   mcast_addr  —— 组播地址（host order）。
 *   ifindex     —— 接口索引。
 * 返回值：setsockopt 结果，0 成功、-1 失败。
 * 实现要点（三套平台分支按优先级选择）：
 *   1. HAVE_RFC3678：用 struct group_req + MCAST_JOIN / LEAVE_GROUP
 *      （新式接口无关 API）。
 *   2. ip_mreqn 带 imr_ifindex：用接口索引版的 struct ip_mreqn。
 *   3. HAVE_BSD_STRUCT_IP_MREQ_HACK：标准 BSD struct ip_mreq（部分平台
 *      把 ifindex 放进 imr_interface）。
 * 共同坑：4.4BSD 派生系统不允许在 DOWN 的接口上做组播操作，所以上一
 *   次 leave 可能失败而仍处于 joined 状态；为此当 JOIN 失败且 errno 为
 *   EADDRINUSE 时会先 DROP 再 JOIN 一次做恢复，若二次失败仍返回错误。
 * 注意：本函数上方保留了 kame / FRR 的历史英文说明，含义与上述一致。
 */
/*
 * Process multicast socket options for IPv4 in an OS-dependent manner.
 * Supported options are IP_{ADD,DROP}_MEMBERSHIP.
 *
 * Many operating systems have a limit on the number of groups that
 * can be joined per socket (where each group and local address
 * counts).  This impacts OSPF, which joins groups on each interface
 * using a single socket.  The limit is typically 20, derived from the
 * original BSD multicast implementation.  Some systems have
 * mechanisms for increasing this limit.
 *
 * In many 4.4BSD-derived systems, multicast group operations are not
 * allowed on interfaces that are not UP.  Thus, a previous attempt to
 * leave the group may have failed, leaving it still joined, and we
 * drop/join quietly to recover.  This may not be necessary, but aims to
 * defend against unknown behavior in that we will still return an error
 * if the second join fails.  It is not clear how other systems
 * (e.g. Linux, Solaris) behave when leaving groups on down interfaces,
 * but this behavior should not be harmful if they behave the same way,
 * allow leaves, or implicitly leave all groups joined to down interfaces.
 */
int setsockopt_ipv4_multicast(int sock, int optname, struct in_addr if_addr,
			      unsigned int mcast_addr, ifindex_t ifindex)
{
#ifdef HAVE_RFC3678
	struct group_req gr;
	struct sockaddr_in *si;
	int ret;
	memset(&gr, 0, sizeof(gr));
	si = (struct sockaddr_in *)&gr.gr_group;
	gr.gr_interface = ifindex;
	si->sin_family = AF_INET;
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
	si->sin_len = sizeof(struct sockaddr_in);
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
	si->sin_addr.s_addr = mcast_addr;
	ret = setsockopt(sock, IPPROTO_IP,
			 (optname == IP_ADD_MEMBERSHIP) ? MCAST_JOIN_GROUP
							: MCAST_LEAVE_GROUP,
			 (void *)&gr, sizeof(gr));
	if ((ret < 0) && (optname == IP_ADD_MEMBERSHIP)
	    && (errno == EADDRINUSE)) {
		setsockopt(sock, IPPROTO_IP, MCAST_LEAVE_GROUP, (void *)&gr,
			   sizeof(gr));
		ret = setsockopt(sock, IPPROTO_IP, MCAST_JOIN_GROUP,
				 (void *)&gr, sizeof(gr));
	}
	return ret;

#elif defined(HAVE_STRUCT_IP_MREQN_IMR_IFINDEX) && !defined(__FreeBSD__)
	struct ip_mreqn mreqn;
	int ret;

	assert(optname == IP_ADD_MEMBERSHIP || optname == IP_DROP_MEMBERSHIP);
	memset(&mreqn, 0, sizeof(mreqn));

	mreqn.imr_multiaddr.s_addr = mcast_addr;
	mreqn.imr_ifindex = ifindex;

	ret = setsockopt(sock, IPPROTO_IP, optname, (void *)&mreqn,
			 sizeof(mreqn));
	if ((ret < 0) && (optname == IP_ADD_MEMBERSHIP)
	    && (errno == EADDRINUSE)) {
		/* see above: handle possible problem when interface comes back
		 * up */
		zlog_info(
			"setsockopt_ipv4_multicast attempting to drop and re-add (fd %d, mcast %pI4, ifindex %u)",
			sock, &mreqn.imr_multiaddr, ifindex);
		setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *)&mreqn,
			   sizeof(mreqn));
		ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				 (void *)&mreqn, sizeof(mreqn));
	}
	return ret;

/* Example defines for another OS, boilerplate off other code in this
   function, AND handle optname as per other sections for consistency !! */
/* #elif  defined(BOGON_NIX) && EXAMPLE_VERSION_CODE > -100000 */
/* Add your favourite OS here! */

#elif defined(HAVE_BSD_STRUCT_IP_MREQ_HACK) /* #if OS_TYPE */
	/* standard BSD API */

	struct ip_mreq mreq;
	int ret;

	assert(optname == IP_ADD_MEMBERSHIP || optname == IP_DROP_MEMBERSHIP);


	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = mcast_addr;
#if !defined __OpenBSD__
	mreq.imr_interface.s_addr = htonl(ifindex);
#else
	mreq.imr_interface.s_addr = if_addr.s_addr;
#endif

	ret = setsockopt(sock, IPPROTO_IP, optname, (void *)&mreq,
			 sizeof(mreq));
	if ((ret < 0) && (optname == IP_ADD_MEMBERSHIP)
	    && (errno == EADDRINUSE)) {
		/* see above: handle possible problem when interface comes back
		 * up */
		zlog_info(
			"setsockopt_ipv4_multicast attempting to drop and re-add (fd %d, mcast %pI4, ifindex %u)",
			sock, &mreq.imr_multiaddr, ifindex);
		setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *)&mreq,
			   sizeof(mreq));
		ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				 (void *)&mreq, sizeof(mreq));
	}
	return ret;

#else
#error "Unsupported multicast API"
#endif /* #if OS_TYPE */
}

/*
 * 设置 IP_MULTICAST_IF：指定组播报文出接口。
 *   支持 ip_mreqn 的平台：用 struct ip_mreqn 只填 imr_ifindex，走索引。
 *   仅支持 BSD 的平台：用 struct in_addr，非 OpenBSD 下把 ifindex 通过
 *     htonl 塞进地址（这是历史 hack），OpenBSD 直接用 if_addr。
 * 返回 setsockopt 结果，0 成功、-1 失败。
 * 坑：用地址字段承载 ifindex 的写法是平台私有的老约定，可移植性差。
 */
/*
 * Set IP_MULTICAST_IF socket option in an OS-dependent manner.
 */
int setsockopt_ipv4_multicast_if(int sock, struct in_addr if_addr,
				 ifindex_t ifindex)
{

#ifdef HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
	struct ip_mreqn mreqn;
	memset(&mreqn, 0, sizeof(mreqn));

	mreqn.imr_ifindex = ifindex;
	return setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (void *)&mreqn,
			  sizeof(mreqn));

/* Example defines for another OS, boilerplate off other code in this
   function */
/* #elif  defined(BOGON_NIX) && EXAMPLE_VERSION_CODE > -100000 */
/* Add your favourite OS here! */
#elif defined(HAVE_BSD_STRUCT_IP_MREQ_HACK)
	struct in_addr m;

#if !defined __OpenBSD__
	m.s_addr = htonl(ifindex);
#else
	m.s_addr = if_addr.s_addr;
#endif

	return setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (void *)&m,
			  sizeof(m));
#else
#error "Unsupported multicast API"
#endif
}

/*
 * 设置 IPv4 组播回环开关 IP_MULTICAST_LOOP。val 为 0 / 1。
 * 返回 0 成功、-1 失败（失败打印日志）。
 */
int setsockopt_ipv4_multicast_loop(int sock, uint8_t val)
{
	int ret;

	ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (void *)&val,
			 sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET, "can't setsockopt IP_MULTICAST_LOOP");

	return ret;
}

/*
 * 以平台无关方式请求内核在收包辅助数据中提供“收包接口 ifindex”。
 * 参数：sock 为 socket；val 非 0 开启（Linux IP_PKTINFO / BSD IP_RECVIF）。
 * 返回值：0 成功、-1 失败。两个平台选项都没有时编译期告警并直接返回 -1。
 * 注意：这是“打开上报”，真正的取值由 getsockopt_ifindex 完成。
 */
static int setsockopt_ipv4_ifindex(int sock, ifindex_t val)
{
	int ret;

#if defined(IP_PKTINFO)
	ret = setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &val, sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET,
			 "Can't set IP_PKTINFO option for fd %d to %d: %s",
			 sock, val, safe_strerror(errno));
#elif defined(IP_RECVIF)
	ret = setsockopt(sock, IPPROTO_IP, IP_RECVIF, &val, sizeof(val));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET,
			 "Can't set IP_RECVIF option for fd %d to %d: %s", sock,
			 val, safe_strerror(errno));
#else
#warning "Neither IP_PKTINFO nor IP_RECVIF is available."
#warning "Will not be able to receive link info."
#warning "Things might be seriously broken.."
	/* XXX Does this ever happen?  Should there be a zlog_warn message here?
	 */
	ret = -1;
#endif
	return ret;
}

/*
 * 设置 IPv4 IP_TOS（服务类型 / DSCP 字节）。
 * tos 为完整 TOS 字节值；返回 0 成功、-1 失败（失败打印日志）。
 */
int setsockopt_ipv4_tos(int sock, int tos)
{
	int ret;

	ret = setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	if (ret < 0)
		flog_err(EC_LIB_SOCKET,
			 "Can't set IP_TOS option for fd %d to %#x: %s", sock,
			 tos, safe_strerror(errno));
	return ret;
}


/*
 * 统一入口：按地址族请求内核上报收包接口信息。
 *   af   —— AF_INET 走 setsockopt_ipv4_ifindex，AF_INET6 走
 *           setsockopt_ipv6_pktinfo。
 *   sock —— socket。
 *   val  —— 传给底层函数的开关值。
 * 返回值：0 成功、-1 失败；未知地址族打印开发错误并返回 -1。
 */
int setsockopt_ifindex(int af, int sock, ifindex_t val)
{
	int ret = -1;

	switch (af) {
	case AF_INET:
		ret = setsockopt_ipv4_ifindex(sock, val);
		break;
	case AF_INET6:
		ret = setsockopt_ipv6_pktinfo(sock, val);
		break;
	default:
		flog_err(EC_LIB_DEVELOPMENT,
			 "setsockopt_ifindex: unknown address family %d", af);
	}
	return ret;
}

/*
 * 从已接收消息 msgh 中解析 IPv4 收包接口 ifindex。
 *   Linux：读 IPPROTO_IP + IP_PKTINFO 的 struct in_pktinfo.ipi_ifindex。
 *   BSD：读 IPPROTO_IP + IP_RECVIF 的 struct sockaddr_dl.sdl_index。
 * 返回值：接口索引（>=1），无法确定时返回 0（表示“未找到”，由上层判断）。
 * 说明：两条分支都做了 NULL 判空，与 IPv6 版不同，是更稳妥的写法。
 */
/*
 * Requires: msgh is not NULL and points to a valid struct msghdr, which
 * may or may not have control data about the incoming interface.
 *
 * Returns the interface index (small integer >= 1) if it can be
 * determined, or else 0.
 */
static ifindex_t getsockopt_ipv4_ifindex(struct msghdr *msgh)
{
	ifindex_t ifindex;

#if defined(IP_PKTINFO)
	/* Linux pktinfo based ifindex retrieval */
	struct in_pktinfo *pktinfo;

	pktinfo = (struct in_pktinfo *)getsockopt_cmsg_data(msgh, IPPROTO_IP,
							    IP_PKTINFO);

	/* getsockopt_ifindex() will forward this, being 0 "not found" */
	if (pktinfo == NULL)
		return 0;

	ifindex = pktinfo->ipi_ifindex;

#elif defined(IP_RECVIF)

/* retrieval based on IP_RECVIF */

	/* BSD systems use a sockaddr_dl as the control message payload. */
	struct sockaddr_dl *sdl;

	/* BSD */
	sdl = (struct sockaddr_dl *)getsockopt_cmsg_data(msgh, IPPROTO_IP,
							 IP_RECVIF);
	if (sdl != NULL)
		ifindex = sdl->sdl_index;
	else
		ifindex = 0;

#else
/*
 * Neither IP_PKTINFO nor IP_RECVIF defined - warn at compile time.
 * XXX Decide if this is a core service, or if daemons have to cope.
 * Since Solaris 8 and OpenBSD seem not to provide it, it seems that
 * daemons have to cope.
 */
#warning "getsockopt_ipv4_ifindex: Neither IP_PKTINFO nor IP_RECVIF defined."
#warning "Some daemons may fail to operate correctly!"
	ifindex = 0;

#endif /* IP_PKTINFO */

	return ifindex;
}

/*
 * 统一入口：按地址族从收包消息中取出接口索引。
 *   af   —— AF_INET 走 getsockopt_ipv4_ifindex，AF_INET6 走
 *           getsockopt_ipv6_ifindex。
 *   msgh —— 已 recvmsg 的消息头。
 * 返回值：接口索引；未知地址族打印开发错误并返回 0，未找到也返回 0。
 */
/* return ifindex, 0 if none found */
ifindex_t getsockopt_ifindex(int af, struct msghdr *msgh)
{
	switch (af) {
	case AF_INET:
		return (getsockopt_ipv4_ifindex(msgh));
	case AF_INET6:
		return (getsockopt_ipv6_ifindex(msgh));
	default:
		flog_err(EC_LIB_DEVELOPMENT,
			 "getsockopt_ifindex: unknown address family %d", af);
		return 0;
	}
}

/*
 * 把 IPv4 头各字段从主机字节序转换为 IP_HDRINCL 期望的系统字节序，
 * 用于自行构造 IP 头并发送（原始套接字）的场景。
 * iph 为待转换的 IP 头。无返回值（原地修改）。
 * 细节：非 BSD 语义平台（未定义 HAVE_IP_HDRINCL_BSD_ORDER）下，ip_len
 *   与 ip_off 需要 htons；ip_id 在所有平台都做 htons。BSD 平台 ip_len /
 *   ip_off 本就是网络序，跳过。
 */
/* swab iph between order system uses for IP_HDRINCL and host order */
void sockopt_iphdrincl_swab_htosys(struct ip *iph)
{
/* BSD and derived take iph in network order, except for
 * ip_len and ip_off
 */
#ifndef HAVE_IP_HDRINCL_BSD_ORDER
	iph->ip_len = htons(iph->ip_len);
	iph->ip_off = htons(iph->ip_off);
#endif /* HAVE_IP_HDRINCL_BSD_ORDER */

	iph->ip_id = htons(iph->ip_id);
}

/*
 * sockopt_iphdrincl_swab_htosys 的逆操作：把 IP 头字段从系统字节序
 * 转回主机字节序（例如 recvmsg 收到原始 IP 包后解析）。
 * iph 为待转换的 IP 头。无返回值（原地修改）。
 */
void sockopt_iphdrincl_swab_systoh(struct ip *iph)
{
#ifndef HAVE_IP_HDRINCL_BSD_ORDER
	iph->ip_len = ntohs(iph->ip_len);
	iph->ip_off = ntohs(iph->ip_off);
#endif /* HAVE_IP_HDRINCL_BSD_ORDER */

	iph->ip_id = ntohs(iph->ip_id);
}

/*
 * 读取该 TCP socket 的平滑 RTT 估计值。
 * 做法：getsockopt(TCP_INFO) 取 tcpi_rtt（微秒），再除以 1000 换算为毫秒。
 * 返回值：RTT 毫秒值；平台无 TCP_INFO 或读取失败时返回 0。
 * 注意：返回 0 既可能是“真的接近 0”，也可能是“不可用 / 失败”，调用方
 *   需自行甄别。
 */
int sockopt_tcp_rtt(int sock)
{
#ifdef TCP_INFO
	struct tcp_info ti;
	socklen_t len = sizeof(ti);

	if (getsockopt(sock, IPPROTO_TCP, TCP_INFO, &ti, &len) != 0)
		return 0;

	return ti.tcpi_rtt / 1000;
#else
	return 0;
#endif
}

/*
 * 启用 / 清除 TCP MD5 签名（RFC 2385）的真正实现，支持前缀粒度。
 * 参数：
 *   sock      —— socket。
 *   su        —— 对端地址或前缀（union sockunion）。
 *   prefixlen —— 0 表示按地址启用（等价于非扩展模式）；>0 表示按前缀，
 *                需要平台支持 TCP_MD5SIG_EXT。
 *   password  —— 共享密钥；NULL 或空串表示清除该对端 / 前缀的 MD5 配置。
 * 返回值：0 成功；-1 出错（打印日志）；-2 表示平台不支持所请求的能力
 *   （无 TCP_MD5SIG，或要求前缀但无 TCP_MD5SIG_EXT）。
 * 关键流程（Linux 分支）：
 *   1. 用 sockunion_getsockname 取 socket 本地地址，判断族是否与 su 一致；
 *      若不一致且本地是 IPv4，则直接返回 0（不配置）；若本地 IPv6 而 sui
 *      是 IPv4，则把 su 做 v4-mapped（::ffff:a.b.c.d）后使用。
 *   2. 填 struct tcp_md5sig：地址、密钥长度、密钥内容。
 *   3. 若有前缀需求且支持 TCP_MD5SIG_EXT，则填 tcpm_prefixlen 与
 *      TCP_MD5SIG_FLAG_PREFIX，并把 optname 换成 TCP_MD5SIG_EXT。
 *   4. 调 setsockopt；errno 为 ENOENT（对端不存在）时视为成功返回 0。
 * 坑：v4-mapped 那一段作者在注释里已说明“目前看起来不可靠”，不同族
 *   的用法可能需要为 v4 / v6 各开一个 socket。非 Linux 平台的 PF_KEY
 *   操作（添加 SA / SP）尚未实现，仅设置一个开关变量。
 */
int sockopt_tcp_signature_ext(int sock, union sockunion *su, uint16_t prefixlen,
			      const char *password)
{
#ifndef HAVE_DECL_TCP_MD5SIG
	/*
	 * We have been asked to enable MD5 auth for an address, but our
	 * platform doesn't support that
	 */
	return -2;
#endif

#ifndef TCP_MD5SIG_EXT
	/*
	 * We have been asked to enable MD5 auth for a prefix, but our platform
	 * doesn't support that
	 */
	if (prefixlen > 0)
		return -2;
#endif

#if HAVE_DECL_TCP_MD5SIG
	int ret;

	int optname = TCP_MD5SIG;
#ifndef GNU_LINUX
	/*
	 * XXX Need to do PF_KEY operation here to add/remove an SA entry,
	 * and add/remove an SP entry for this peer's packet flows also.
	 */
	int md5sig = password && *password ? 1 : 0;
#else
	int keylen = password ? strlen(password) : 0;
	struct tcp_md5sig md5sig;
	union sockunion *su2, *susock;

	/* Figure out whether the socket and the sockunion are the same family..
	 * adding AF_INET to AF_INET6 needs to be v4 mapped, you'd think..
	 */
	if (!(susock = sockunion_getsockname(sock)))
		return -1;

	if (susock->sa.sa_family == su->sa.sa_family)
		su2 = su;
	else {
		/* oops.. */
		su2 = susock;

		if (su2->sa.sa_family == AF_INET) {
			sockunion_free(susock);
			return 0;
		}

		/* If this does not work, then all users of this sockopt will
		 * need to
		 * differentiate between IPv4 and IPv6, and keep separate
		 * sockets for
		 * each.
		 *
		 * Sadly, it doesn't seem to work at present. It's unknown
		 * whether
		 * this is a bug or not.
		 */
		if (su2->sa.sa_family == AF_INET6
		    && su->sa.sa_family == AF_INET) {
			su2->sin6.sin6_family = AF_INET6;
			/* V4Map the address */
			memset(&su2->sin6.sin6_addr, 0,
			       sizeof(struct in6_addr));
			su2->sin6.sin6_addr.s6_addr32[2] = htonl(0xffff);
			memcpy(&su2->sin6.sin6_addr.s6_addr32[3],
			       &su->sin.sin_addr, 4);
		}
	}

	memset(&md5sig, 0, sizeof(md5sig));
	memcpy(&md5sig.tcpm_addr, su2, sizeof(*su2));

	md5sig.tcpm_keylen = keylen;
	if (keylen)
		memcpy(md5sig.tcpm_key, password, keylen);
	sockunion_free(susock);

	/*
	 * Handle support for MD5 signatures on prefixes, if available and
	 * requested. Technically the #ifdef check below is not needed because
	 * if prefixlen > 0 and we don't have support for this feature we would
	 * have already returned by now, but leaving it there to be explicit.
	 */
#ifdef TCP_MD5SIG_EXT
	if (prefixlen > 0) {
		md5sig.tcpm_prefixlen = prefixlen;
		md5sig.tcpm_flags = TCP_MD5SIG_FLAG_PREFIX;
		optname = TCP_MD5SIG_EXT;
	}
#endif /* TCP_MD5SIG_EXT */

#endif /* GNU_LINUX */

	ret = setsockopt(sock, IPPROTO_TCP, optname, &md5sig, sizeof(md5sig));
	if (ret < 0) {
		if (ENOENT == errno)
			ret = 0;
		else
			flog_err_sys(
				EC_LIB_SYSTEM_CALL,
				"sockopt_tcp_signature: setsockopt(%d): %s",
				sock, safe_strerror(errno));
	}
	return ret;
#endif /* HAVE_TCP_MD5SIG */

	/*
	 * Making compiler happy.  If we get to this point we probably
	 * have done something really really wrong.
	 */
	return -2;
}

/*
 * TCP MD5 签名的便捷包装：prefixlen 固定为 0，即按单个对端地址启用 /
 * 清除 MD5。其余语义同 sockopt_tcp_signature_ext。
 */
int sockopt_tcp_signature(int sock, union sockunion *su, const char *password)
{
	return sockopt_tcp_signature_ext(sock, su, 0, password);
}

/*
 * 设置 TCP 最大段大小 TCP_MAXSEG。tcp_maxseg 为期望 MSS 字节数。
 * 返回值：0 成功、非 0 失败（失败打印系统错误日志）。
 * 注意：影响 TCP MSS 协商，通常需在连接建立前设置才有效。
 */
/* set TCP mss value to socket */
int sockopt_tcp_mss_set(int sock, int tcp_maxseg)
{
	int ret = 0;
	socklen_t tcp_maxseg_len = sizeof(tcp_maxseg);

	ret = setsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, &tcp_maxseg,
			 tcp_maxseg_len);
	if (ret != 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s failed: setsockopt(%d): %s", __func__, sock,
			     safe_strerror(errno));
	}

	return ret;
}

/*
 * 读取当前 TCP 最大段大小 TCP_MAXSEG（可能是对端通告后的实际生效值）。
 * 返回值：MSS 字节数；sock < 0 或读取失败时返回 0（并打印日志）。
 */
/* get TCP mss value synced by socket */
int sockopt_tcp_mss_get(int sock)
{
	int ret = 0;
	int tcp_maxseg = 0;
	socklen_t tcp_maxseg_len = sizeof(tcp_maxseg);

	if (sock < 0)
		return 0;

	ret = getsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, &tcp_maxseg,
			 &tcp_maxseg_len);
	if (ret != 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s failed: getsockopt(%d): %s", __func__, sock,
			     safe_strerror(errno));
		return 0;
	}

	return tcp_maxseg;
}

/*
 * 配置 TCP keepalive，用于及时发现对端失联（BGP 长连接尤其依赖）。
 * 参数（单位均为秒 / 次数）：
 *   keepalive_idle   —— 连接空闲多久后开始发第一个保活探测。
 *   keepalive_intvl  —— 两次探测之间的间隔。
 *   keepalive_probes —— 放弃前最多发送的探测次数。
 * 返回值：0 成功、-1 失败（任一步 setsockopt 失败即打印日志并返回 -1）。
 * 流程：先开 SO_KEEPALIVE，再依次设 TCP_KEEPIDLE / TCP_KEEPINTVL /
 *   TCP_KEEPCNT。注意 OpenBSD 只有 SO_KEEPALIVE，不设置后面三项，
 *   直接返回 0。
 */
int setsockopt_tcp_keepalive(int sock, uint16_t keepalive_idle,
			     uint16_t keepalive_intvl,
			     uint16_t keepalive_probes)
{
	int val = 1;

	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s failed: setsockopt SO_KEEPALIVE (%d): %s",
			     __func__, sock, safe_strerror(errno));
		return -1;
	}

#if defined __OpenBSD__
	return 0;
#else
	/* Send first probe after keepalive_idle seconds */
	val = keepalive_idle;
	if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) <
	    0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s failed: setsockopt TCP_KEEPIDLE (%d): %s",
			     __func__, sock, safe_strerror(errno));
		return -1;
	}

	/* Set interval between two probes */
	val = keepalive_intvl;
	if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) <
	    0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s failed: setsockopt TCP_KEEPINTVL (%d): %s",
			     __func__, sock, safe_strerror(errno));
		return -1;
	}

	/* Set maximum probes */
	val = keepalive_probes;
	if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s failed: setsockopt TCP_KEEPCNT (%d): %s",
			     __func__, sock, safe_strerror(errno));
		return -1;
	}

	return 0;
#endif
}

/*
 * 打开 IP_TRANSPARENT（Linux 透明代理选项，允许绑定非本机地址 / 以任意
 * 源地址收发）。sock 为 socket。无返回值（失败打印系统错误日志）。
 * 说明：仅在定义了 IP_TRANSPARENT 的平台生效；否则把 err 置为
 *   ENOPROTOOPT，会走下面 err < 0 分支打印一条错误日志（属预期噪声）。
 */
/* set IP_TRANSPARENT to socket */
void sockopt_ip_transparent(int sock)
{
	int err = -1;

#if defined(IP_TRANSPARENT)
	const char flag_on = 1;

	err = setsockopt(sock, SOL_IP, IP_TRANSPARENT, &flag_on, sizeof(flag_on));
#else
	err = ENOPROTOOPT;
#endif
	if (err < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL, "%s failed: setsockopt(%d, IP_TRANSPARENT): %s",
			     __func__, sock, safe_strerror(errno));
	}
}
