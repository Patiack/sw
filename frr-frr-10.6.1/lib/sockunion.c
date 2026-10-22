// SPDX-License-Identifier: GPL-2.0-or-later
/* Socket union related function.
 * Copyright (c) 1997, 98 Kunihiro Ishiguro
 */

/*
 * ============================================================
 * 文件导读：sockunion.c —— 地址族无关套接字地址封装的实现
 * ============================================================
 *
 * 这个文件在 FRR 里干什么
 *   实现 sockunion.h 声明的一组工具函数：把 union sockunion（IPv4 / IPv6 /
 *   Unix 域地址的统一载体）与文本互转、比较、哈希、取地址字节，并封装
 *   与之相关的 socket 建立 / bind / accept / connect / 取名等操作。核心
 *   价值是让上层协议代码不必到处写 AF_INET / AF_INET6 分支。
 *
 * 代码组织（自顶向下阅读）
 *   1. 字符串转换：inet_sutop、str2sockunion、sockunion2str、sockunion_str2su。
 *   2. 内部辅助：sockunion_normalise_mapped（v4-mapped v6 归一化为 v4）、
 *      sockunion_log（日志用文本，遇未知族不崩）。
 *   3. socket 生命周期：sockunion_socket、sockunion_accept、sockunion_sizeof、
 *      sockunion_connect（非阻塞）、sockunion_stream_socket、sockunion_bind。
 *   4. socket 选项封装：sockopt_reuseaddr / _reuseport / _ttl / _minttl / _v6only。
 *   5. 比较 / 哈希 / 地址字节：sockunion_same、sockunion_hash、family2addrsize、
 *      sockunion_get_addrlen、sockunion_get_addr、sockunion_set。
 *   6. 取本地 / 对端地址：sockunion_getsockname、sockunion_getpeername。
 *   7. 杂项：sockunion_print、sockunion_cmp、sockunion_dup / free / init、
 *      sockunion_is_null。
 *   8. printfrr 扩展：%pSU（打印地址）、%dPF（协议族）、%dSO（套接字类型）。
 *
 * 核心数据结构
 *   union sockunion —— 地址主体（见 sockunion.h）；
 *   内部的匿名 union name（getsockname / getpeername 用 128 字节缓冲兼容
 *   各地址族）；struct fbuf / printfrr_eargs —— printfrr 扩展的输出上下文。
 *
 * 与其他模块的关系
 *   依赖 prefix.h、vty.h、sockunion.h、memory.h（MTYPE 内存类型）、log.h、
 *   jhash.h（哈希）、lib_errors.h、printfrr.h。被几乎所有自建连接的协议使用。
 *
 * 阅读顺序建议
 *   先读 str2sockunion / sockunion2str 弄清地址表示，再读 sockunion_bind /
 *   sockunion_connect 理解建连流程，最后看 printfrr 扩展与 is_null。
 * ============================================================
 */

#include <zebra.h>

#ifndef __linux__
#include <net/if_dl.h>
#endif

#include "prefix.h"
#include "vty.h"
#include "sockunion.h"
#include "memory.h"
#include "log.h"
#include "jhash.h"
#include "lib_errors.h"
#include "printfrr.h"

/*
 * 定义本文件私有的内存类型 MTYPE_SOCKUNION，供 XCALLOC / XMALLOC / XFREE
 * 记账使用（归属 LIB 模块，描述串为 "Socket union"）。所有动态分配的
 * sockunion 都应使用该类型，便于内存统计与泄漏排查。
 */
DEFINE_MTYPE_STATIC(LIB, SOCKUNION, "Socket union");

/*
 * 把 sockunion 中的地址转换为文本，写入调用方提供的 str。
 *   su  —— 输入地址（v4 / v6）。
 *   str —— 输出缓冲，需至少 INET6_ADDRSTRLEN 大小。
 * 返回值：str（便于链式使用）。
 * 注意：不处理端口，只输出纯地址；未知地址族时 str 内容不变（可能是
 *   未初始化内容，调用方应保证族已知）。
 */
const char *inet_sutop(const union sockunion *su, char *str)
{
	switch (su->sa.sa_family) {
	case AF_INET:
		inet_ntop(AF_INET, &su->sin.sin_addr, str, INET_ADDRSTRLEN);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &su->sin6.sin6_addr, str, INET6_ADDRSTRLEN);
		break;
	}
	return str;
}

/*
 * 解析文本地址到 sockunion（先试 IPv4，再试 IPv6）。
 *   str —— 形如 "1.2.3.4" 或 "2001:db8::1" 的字符串；NULL 直接失败。
 *   su  —— 输出地址，函数会先整体清零。
 * 返回值：0 成功、-1 失败。
 * 注意：只解析纯地址，不含端口；成功时按族设置 sin_family，并在支持
 *   sin_len 的平台填好长度字段。
 */
int str2sockunion(const char *str, union sockunion *su)
{
	int ret;

	if (str == NULL)
		return -1;

	memset(su, 0, sizeof(union sockunion));

	ret = inet_pton(AF_INET, str, &su->sin.sin_addr);
	if (ret > 0) /* Valid IPv4 address format. */
	{
		su->sin.sin_family = AF_INET;
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
		su->sin.sin_len = sizeof(struct sockaddr_in);
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
		return 0;
	}
	ret = inet_pton(AF_INET6, str, &su->sin6.sin6_addr);
	if (ret > 0) /* Valid IPv6 address format. */
	{
		su->sin6.sin6_family = AF_INET6;
#ifdef SIN6_LEN
		su->sin6.sin6_len = sizeof(struct sockaddr_in6);
#endif /* SIN6_LEN */
		return 0;
	}
	return -1;
}

/*
 * 把 sockunion 转换为文本（比 inet_sutop 更健壮，能处理各种异常族）。
 *   su  —— 输入地址。
 *   buf —— 输出缓冲；len 为其长度。
 * 返回值：buf。
 * 行为：AF_UNSPEC 输出 "(unspec)"，未知族输出 "(af N)"，v4 / v6 用 inet_ntop。
 * 注意：也不含端口。
 */
const char *sockunion2str(const union sockunion *su, char *buf, size_t len)
{
	switch (sockunion_family(su)) {
	case AF_UNSPEC:
		snprintf(buf, len, "(unspec)");
		return buf;
	case AF_INET:
		return inet_ntop(AF_INET, &su->sin.sin_addr, buf, len);
	case AF_INET6:
		return inet_ntop(AF_INET6, &su->sin6.sin6_addr, buf, len);
	}
	snprintf(buf, len, "(af %d)", sockunion_family(su));
	return buf;
}

/*
 * 解析文本地址并返回新分配的 sockunion。
 *   str —— 地址字符串。
 * 返回值：成功返回 XCALLOC 分配的 sockunion（调用方用 sockunion_free 释放）；
 *   解析失败返回 NULL 且已释放临时内存。
 */
union sockunion *sockunion_str2su(const char *str)
{
	union sockunion *su = XCALLOC(MTYPE_SOCKUNION, sizeof(union sockunion));

	if (!str2sockunion(str, su))
		return su;

	XFREE(MTYPE_SOCKUNION, su);
	return NULL;
}

/*
 * 把 v4-mapped 的 IPv6 地址（::ffff:a.b.c.d）就地归一化为 IPv4 地址。
 *   su —— 待处理的地址。
 * 无返回值（原地修改）。若 su 不是 AF_INET6 或不是 v4-mapped，则不做任何
 *   改动。
 * 用途：双栈 socket 上 accept / getpeername 可能拿到 v4-mapped 形式，
 *   归一起码能让上层把对端当成 IPv4 处理。
 */
/* Convert IPv4 compatible IPv6 address to IPv4 address. */
static void sockunion_normalise_mapped(union sockunion *su)
{
	struct sockaddr_in sin;

	if (su->sa.sa_family == AF_INET6
	    && IN6_IS_ADDR_V4MAPPED(&su->sin6.sin6_addr)) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = su->sin6.sin6_port;
		memcpy(&sin.sin_addr, ((char *)&su->sin6.sin6_addr) + 12, 4);
		memcpy(su, &sin, sizeof(struct sockaddr_in));
	}
}

/*
 * 生成用于日志的地址文本（内部辅助）。与 sockunion2str 类似，但对未知
 * 地址族输出 "af_unknown N " 而不是崩溃。
 *   su —— 地址；buf / len —— 输出缓冲及其长度。
 * 返回值：buf。注意：已知族时 inet_ntop 失败会返回 NULL 而 buf 可能未写。
 */
/* return sockunion structure : this function should be revised. */
static const char *sockunion_log(const union sockunion *su, char *buf,
				 size_t len)
{
	switch (su->sa.sa_family) {
	case AF_INET:
		return inet_ntop(AF_INET, &su->sin.sin_addr, buf, len);

	case AF_INET6:
		return inet_ntop(AF_INET6, &(su->sin6.sin6_addr), buf, len);

	default:
		snprintf(buf, len, "af_unknown %d ", su->sa.sa_family);
		return buf;
	}
}

/*
 * 按 su 的地址族创建一个 SOCK_STREAM 套接字。
 *   su —— 决定地址族（AF_INET / AF_INET6）。
 * 返回值：新套接字 fd；失败返回 -1 并打印错误日志。
 * 注意：与 sockunion_stream_socket 的区别是本函数不会在族为 0 时兜底默认族。
 */
/* Return socket of sockunion. */
int sockunion_socket(const union sockunion *su)
{
	int sock;

	sock = socket(su->sa.sa_family, SOCK_STREAM, 0);
	if (sock < 0) {
		char buf[SU_ADDRSTRLEN];
		flog_err(EC_LIB_SOCKET, "Can't make socket for %s : %s",
			 sockunion_log(su, buf, SU_ADDRSTRLEN),
			 safe_strerror(errno));
		return -1;
	}

	return sock;
}

/*
 * 接受一个新的连接，并把对端地址写回 su。
 *   sock —— 监听套接字；su —— 输出对端地址（内部用 sizeof(union sockunion)
 *           作为地址长度上限）。
 * 返回值：新连接 fd；失败返回 -1。
 * 副作用：对拿到的地址调用 sockunion_normalise_mapped，把 v4-mapped v6
 *   归一化成 IPv4。
 */
/* Return accepted new socket file descriptor. */
int sockunion_accept(int sock, union sockunion *su)
{
	socklen_t len;
	int client_sock;

	len = sizeof(union sockunion);
	client_sock = accept(sock, (struct sockaddr *)su, &len);

	sockunion_normalise_mapped(su);
	return client_sock;
}

/*
 * 返回 su 对应的具体 sockaddr 结构长度（供 bind / connect 传地址长度）。
 *   v4 -> sizeof(struct sockaddr_in)，v6 -> sizeof(struct sockaddr_in6)，
 *   其他族返回 0。
 * 注意：返回的是“族相关”长度而非 union 总大小，与 sockunion_accept 里
 *   用整个 union 作上限不同。
 */
/* Return sizeof union sockunion.  */
int sockunion_sizeof(const union sockunion *su)
{
	int ret;

	ret = 0;
	switch (su->sa.sa_family) {
	case AF_INET:
		ret = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		ret = sizeof(struct sockaddr_in6);
		break;
	}
	return ret;
}

/*
 * 以非阻塞方式连接对端。
 *   fd     —— 已创建的套接字（应为非阻塞）。
 *   peersu —— 对端地址（函数内部拷贝一份再填端口，不改动入参）。
 *   port   —— 对端端口（主机序，内部按族写进 sin_port / sin6_port）。
 * 返回值：enum connect_result —— connect_success（立即成功）、
 *   connect_in_progress（EINPROGRESS，需等待可写事件后再确认）、
 *   connect_error（其它错误，已打印日志）。
 * 注意：非阻塞 socket 上 in_progress 属正常，调用方通常用事件循环在
 *   可写时再用 getsockopt(SO_ERROR) 判断最终结果。
 */
/* Performs a non-blocking connect().  */
enum connect_result sockunion_connect(int fd, const union sockunion *peersu, unsigned short port)
{
	int ret;
	union sockunion su;

	memcpy(&su, peersu, sizeof(union sockunion));

	switch (su.sa.sa_family) {
	case AF_INET:
		su.sin.sin_port = port;
		break;
	case AF_INET6:
		su.sin6.sin6_port = port;
		break;
	}

	/* Call connect function. */
	ret = connect(fd, (struct sockaddr *)&su, sockunion_sizeof(&su));

	/* Immediate success */
	if (ret == 0)
		return connect_success;

	/* If connect is in progress then return 1 else it's real error. */
	if (ret < 0) {
		if (errno != EINPROGRESS) {
			char str[SU_ADDRSTRLEN];
			zlog_info("can't connect to %s fd %d : %s",
				  sockunion_log(&su, str, sizeof(str)), fd,
				  safe_strerror(errno));
			return connect_error;
		}
	}

	return connect_in_progress;
}

/*
 * 按 su 的族创建一个 SOCK_STREAM 套接字；若 su 的族为 0，则先设为默认族
 *   AF_INET_UNION（即 IPv6）。
 *   su —— 输入 / 输出：族为 0 时会被就地改写为默认族（有副作用）。
 * 返回值：新套接字 fd；失败返回 -1（打印错误日志）。
 * 与 sockunion_socket 的区别：本函数会兜底默认族，且会修改 su。
 */
/* Make socket from sockunion union. */
int sockunion_stream_socket(union sockunion *su)
{
	int sock;

	if (su->sa.sa_family == 0)
		su->sa.sa_family = AF_INET_UNION;

	sock = socket(su->sa.sa_family, SOCK_STREAM, 0);

	if (sock < 0)
		flog_err(EC_LIB_SOCKET,
			 "can't make socket sockunion_stream_socket");

	return sock;
}

/*
 * 把套接字 bind 到 su 指定的地址与端口。
 *   sock    —— 套接字。
 *   su      —— 输入 / 输出：地址；函数会就地写入端口（及 sin_len）。
 *   port    —— 端口（主机序，内部 htons）。注意是“原样覆盖”地址里的端口。
 *   su_addr —— 若为 NULL，则把地址置为通配地址（v4: INADDR_ANY，
 *              v6: in6addr_any）；非 NULL 时保持 su 自带地址不变。
 * 返回值：bind 的结果，0 成功、-1 失败（失败打印错误日志）。
 * 注意：会修改入参 su（端口 / sin_len / 可能的通配地址），调用方若需
 *   保留原地址应先自行拷贝。
 */
/* Bind socket to specified address. */
int sockunion_bind(int sock, union sockunion *su, unsigned short port,
		   union sockunion *su_addr)
{
	int size = 0;
	int ret;

	if (su->sa.sa_family == AF_INET) {
		size = sizeof(struct sockaddr_in);
		su->sin.sin_port = htons(port);
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
		su->sin.sin_len = size;
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
		if (su_addr == NULL)
			sockunion2ip(su) = htonl(INADDR_ANY);
	} else if (su->sa.sa_family == AF_INET6) {
		size = sizeof(struct sockaddr_in6);
		su->sin6.sin6_port = htons(port);
#ifdef SIN6_LEN
		su->sin6.sin6_len = size;
#endif /* SIN6_LEN */
		if (su_addr == NULL) {
#ifdef LINUX_IPV6
			memset(&su->sin6.sin6_addr, 0, sizeof(struct in6_addr));
#else
			su->sin6.sin6_addr = in6addr_any;
#endif /* LINUX_IPV6 */
		}
	}

	ret = bind(sock, (struct sockaddr *)su, size);
	if (ret < 0) {
		char buf[SU_ADDRSTRLEN];
		flog_err(EC_LIB_SOCKET, "can't bind socket for %s : %s",
			 sockunion_log(su, buf, SU_ADDRSTRLEN),
			 safe_strerror(errno));
	}

	return ret;
}

/*
 * 打开 SO_REUSEADDR（允许在 TIME_WAIT 状态或地址复用场景下重新 bind）。
 *   sock —— 套接字。
 * 返回值：0 成功、-1 失败（失败打印错误日志）。
 */
int sockopt_reuseaddr(int sock)
{
	int ret;
	int on = 1;

	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&on,
			 sizeof(on));
	if (ret < 0) {
		flog_err(
			EC_LIB_SOCKET,
			"can't set sockopt SO_REUSEADDR to socket %d errno=%d: %s",
			sock, errno, safe_strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * 打开 SO_REUSEPORT（允许多个套接字 bind 同一地址端口，常用于多进程 /
 *   多线程分担监听）。sock 为套接字。
 * 返回值：0 成功、-1 失败（失败打印错误日志）。
 * 平台差异：仅当平台定义 SO_REUSEPORT 时才真正设置；否则下面的 #else
 *   分支提供一个“总是成功”的空实现（返回 0）。
 */
#ifdef SO_REUSEPORT
int sockopt_reuseport(int sock)
{
	int ret;
	int on = 1;

	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&on,
			 sizeof(on));
	if (ret < 0) {
		flog_err(EC_LIB_SOCKET,
			 "can't set sockopt SO_REUSEPORT to socket %d", sock);
		return -1;
	}
	return 0;
}
#else
/*
 * 平台不支持 SO_REUSEPORT 时的空实现：直接返回 0（视为成功），
 * 使调用方无需再关心平台差异。
 */
int sockopt_reuseport(int sock)
{
	return 0;
}
#endif /* 0 */

/*
 * 设置出站报文的初始 TTL / 跳数限制。
 *   family —— AF_INET 设 IP_TTL（需平台定义 IP_TTL），AF_INET6 设
 *             IPV6_UNICAST_HOPS。
 *   sock   —— 套接字；ttl —— 跳数值。
 * 返回值：0 成功、-1 失败（失败打印错误日志）；其他族直接返回 0。
 */
int sockopt_ttl(int family, int sock, int ttl)
{
	int ret;

#ifdef IP_TTL
	if (family == AF_INET) {
		ret = setsockopt(sock, IPPROTO_IP, IP_TTL, (void *)&ttl,
				 sizeof(int));
		if (ret < 0) {
			flog_err(EC_LIB_SOCKET,
				 "can't set sockopt IP_TTL %d to socket %d",
				 ttl, sock);
			return -1;
		}
		return 0;
	}
#endif /* IP_TTL */
	if (family == AF_INET6) {
		ret = setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
				 (void *)&ttl, sizeof(int));
		if (ret < 0) {
			flog_err(
				EC_LIB_SOCKET,
				"can't set sockopt IPV6_UNICAST_HOPS %d to socket %d",
				ttl, sock);
			return -1;
		}
		return 0;
	}
	return 0;
}

/*
 * 设置接收报文的“最小 TTL / 跳数”（GTSM，通用 TTL 安全机制），用于
 * 只接受 TTL 足够大的邻居报文，常用于单跳 BGP 会话安全。
 *   family —— AF_INET 设 IP_MINTTL，AF_INET6 设 IPV6_MINHOPCOUNT。
 *   sock   —— 套接字；minttl —— 最小跳数阈值。
 * 返回值：0 成功、-1 失败（失败打印错误日志）。
 * 平台差异：两个选项都可能未定义；都不可用时置 errno = EOPNOTSUPP 并
 *  返回 -1。
 */
int sockopt_minttl(int family, int sock, int minttl)
{
#ifdef IP_MINTTL
	if (family == AF_INET) {
		int ret = setsockopt(sock, IPPROTO_IP, IP_MINTTL, &minttl,
				     sizeof(minttl));
		if (ret < 0)
			flog_err(
				EC_LIB_SOCKET,
				"can't set sockopt IP_MINTTL to %d on socket %d: %s",
				minttl, sock, safe_strerror(errno));
		return ret;
	}
#endif /* IP_MINTTL */
#ifdef IPV6_MINHOPCOUNT
	if (family == AF_INET6) {
		int ret = setsockopt(sock, IPPROTO_IPV6, IPV6_MINHOPCOUNT,
				     &minttl, sizeof(minttl));
		if (ret < 0)
			flog_err(
				EC_LIB_SOCKET,
				"can't set sockopt IPV6_MINHOPCOUNT to %d on socket %d: %s",
				minttl, sock, safe_strerror(errno));
		return ret;
	}
#endif

	errno = EOPNOTSUPP;
	return -1;
}

/*
 * 为 IPv6 套接字设置 IPV6_V6ONLY（只接受 IPv6，不接受 v4-mapped）。
 *   family —— AF_INET6 才生效；sock —— 套接字。
 * 返回值：0 成功、-1 失败（失败打印错误日志），其他族返回 0。
 * 注意：平台无 IPV6_V6ONLY 时直接返回 0（不设置）。
 */
int sockopt_v6only(int family, int sock)
{
	int ret, on = 1;

#ifdef IPV6_V6ONLY
	if (family == AF_INET6) {
		ret = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&on,
				 sizeof(int));
		if (ret < 0) {
			flog_err(EC_LIB_SOCKET,
				 "can't set sockopt IPV6_V6ONLY to socket %d",
				 sock);
			return -1;
		}
		return 0;
	}
#endif /* IPV6_V6ONLY */
	return 0;
}

/*
 * 判断两个地址是否“相同”（不含端口）。
 *   su1 / su2 —— 待比较地址。
 * 返回值：1 相同、0 不同。
 * 细节：族不同直接不同；IPv4 比较 4 字节地址；IPv6 比较 16 字节地址，
 *   且当两者均为链路本地地址时，还会比较 scope_id（接口索引）。注意
 *   scope 的比较只在两端 scope_id 都非 0 时进行。
 */
/* If same family and same prefix return 1. */
int sockunion_same(const union sockunion *su1, const union sockunion *su2)
{
	int ret = 0;

	if (su1->sa.sa_family != su2->sa.sa_family)
		return 0;

	switch (su1->sa.sa_family) {
	case AF_INET:
		ret = memcmp(&su1->sin.sin_addr, &su2->sin.sin_addr,
			     sizeof(struct in_addr));
		break;
	case AF_INET6:
		ret = IPV6_ADDR_CMP(&su1->sin6.sin6_addr, &su2->sin6.sin6_addr);
		if ((ret == 0) && IN6_IS_ADDR_LINKLOCAL(&su1->sin6.sin6_addr)) {
			/* compare interface indices */
			if (su1->sin6.sin6_scope_id && su2->sin6.sin6_scope_id)
				ret = (su1->sin6.sin6_scope_id
				       == su2->sin6.sin6_scope_id)
					      ? 0
					      : 1;
		}
		break;
	}
	if (ret == 0)
		return 1;
	else
		return 0;
}

/*
 * 计算地址的哈希值（供哈希表使用）。
 *   su —— 地址。
 * 返回值：哈希值；v4 用 1 个字做 jhash，v6 用 4 个字做 jhash2；
 *   其他族返回 0。
 * 注意：只按地址（不含端口 / scope）哈希，需与 sockunion_same 的语义
 *   保持一致的相等判据。
 */
unsigned int sockunion_hash(const union sockunion *su)
{
	switch (sockunion_family(su)) {
	case AF_INET:
		return jhash_1word(su->sin.sin_addr.s_addr, 0);
	case AF_INET6:
		return jhash2(su->sin6.sin6_addr.s6_addr32,
			      array_size(su->sin6.sin6_addr.s6_addr32), 0);
	}
	return 0;
}

/*
 * 地址族对应的纯地址字节数：AF_INET -> 4，AF_INET6 -> 16，其他 -> 0。
 * family 为地址族。返回值即字节数。
 */
size_t family2addrsize(int family)
{
	switch (family) {
	case AF_INET:
		return sizeof(struct in_addr);
	case AF_INET6:
		return sizeof(struct in6_addr);
	}
	return 0;
}

/*
 * 返回 su 的纯地址字节数（v4=4，v6=16，其他族=0）。内部调制 family2addrsize。
 */
size_t sockunion_get_addrlen(const union sockunion *su)
{
	return family2addrsize(sockunion_family(su));
}

/*
 * 返回指向 su 中纯地址字节的指针（v4 指向 4 字节，v6 指向 16 字节）。
 *   su —— 地址。
 * 返回值：地址起始指针；未知族返回 NULL。
 * 注意：返回的是内部指针，生命周期随 su，勿在 su 释放后使用。
 */
const uint8_t *sockunion_get_addr(const union sockunion *su)
{
	switch (sockunion_family(su)) {
	case AF_INET:
		return (const uint8_t *)&su->sin.sin_addr.s_addr;
	case AF_INET6:
		return (const uint8_t *)&su->sin6.sin6_addr;
	}
	return NULL;
}

/*
 * 用给定地址族与地址字节设置 su（仅改地址与族，不清零整个结构）。
 *   su     —— 输出地址。
 *   family —— 地址族（AF_INET / AF_INET6）。
 *   addr   —— 地址字节起始；bytes —— 其长度。
 * 无返回值。
 * 注意：若 bytes 与 family2addrsize(family) 不匹配，直接静默返回（不设置）；
 *   因此 v4 必须给 4 字节、v6 必须给 16 字节。端口 / scope 不受影响。
 */
void sockunion_set(union sockunion *su, int family, const uint8_t *addr,
		   size_t bytes)
{
	if (family2addrsize(family) != bytes)
		return;

	sockunion_family(su) = family;
	switch (family) {
	case AF_INET:
		memcpy(&su->sin.sin_addr.s_addr, addr, bytes);
		break;
	case AF_INET6:
		memcpy(&su->sin6.sin6_addr, addr, bytes);
		break;
	}
}

/*
 * 取套接字的本地地址与端口（通常在 TCP 连接建立后调用）。
 *   fd —— 套接字。
 * 返回值：新分配的 sockunion（调用方用 sockunion_free 释放）；失败或
 *   地址族非 v4 / v6 时返回 NULL（并打印错误日志）。
 * 副作用：对得到的 IPv6 地址做 v4-mapped 归一化。
 * 实现细节：本地用一个 128 字节的匿名 union 接收，兼容各行大小的
 *   sockaddr；再按实际族拷贝 sizeof 个字节到新 sockunion。
 */
/* After TCP connection is established.  Get local address and port. */
union sockunion *sockunion_getsockname(int fd)
{
	int ret;
	socklen_t len;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
		char tmp_buffer[128];
	} name;
	union sockunion *su;

	memset(&name, 0, sizeof(name));
	len = sizeof(name);

	ret = getsockname(fd, (struct sockaddr *)&name, &len);
	if (ret < 0) {
		flog_err(EC_LIB_SOCKET,
			 "Can't get local address and port by getsockname: %s",
			 safe_strerror(errno));
		return NULL;
	}

	if (name.sa.sa_family == AF_INET) {
		su = XCALLOC(MTYPE_SOCKUNION, sizeof(union sockunion));
		memcpy(su, &name, sizeof(struct sockaddr_in));
		return su;
	}
	if (name.sa.sa_family == AF_INET6) {
		su = XCALLOC(MTYPE_SOCKUNION, sizeof(union sockunion));
		memcpy(su, &name, sizeof(struct sockaddr_in6));
		sockunion_normalise_mapped(su);
		return su;
	}

	flog_err(
		EC_LIB_SOCKET,
		"Unexpected AFI received(%d) for sockunion_getsockname call for fd: %d",
		name.sa.sa_family, fd);
	return NULL;
}

/*
 * 取套接字的对端地址与端口（通常在 TCP 连接建立后调用）。
 *   fd —— 套接字。
 * 返回值：新分配的 sockunion（调用方用 sockunion_free 释放）；失败或族
 *   非 v4 / v6 时返回 NULL（并打印错误日志）。
 * 副作用：对 IPv6 对端地址做 v4-mapped 归一化。逻辑与
 *   sockunion_getsockname 对称，仅底层调用 getpeername。
 */
/* After TCP connection is established.  Get remote address and port. */
union sockunion *sockunion_getpeername(int fd)
{
	int ret;
	socklen_t len;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
		char tmp_buffer[128];
	} name;
	union sockunion *su;

	memset(&name, 0, sizeof(name));
	len = sizeof(name);
	ret = getpeername(fd, (struct sockaddr *)&name, &len);
	if (ret < 0) {
		flog_err(EC_LIB_SOCKET, "Can't get remote address and port: %s",
			 safe_strerror(errno));
		return NULL;
	}

	if (name.sa.sa_family == AF_INET) {
		su = XCALLOC(MTYPE_SOCKUNION, sizeof(union sockunion));
		memcpy(su, &name, sizeof(struct sockaddr_in));
		return su;
	}
	if (name.sa.sa_family == AF_INET6) {
		su = XCALLOC(MTYPE_SOCKUNION, sizeof(union sockunion));
		memcpy(su, &name, sizeof(struct sockaddr_in6));
		sockunion_normalise_mapped(su);
		return su;
	}

	flog_err(
		EC_LIB_SOCKET,
		"Unexpected AFI received(%d) for sockunion_getpeername call for fd: %d",
		name.sa.sa_family, fd);
	return NULL;
}

/*
 * 调试用：把地址打印到 stdout（printf）。su 为 NULL 时直接返回。
 * v4 / v6 分别用 %pI4 / %pI6 格式打印；定义 AF_LINK 的平台还会打印
 * 链路层索引。标记为 unused，避免未调用时编译告警。
 */
/* Print sockunion structure */
static void __attribute__((unused)) sockunion_print(const union sockunion *su)
{
	if (su == NULL)
		return;

	switch (su->sa.sa_family) {
	case AF_INET:
		printf("%pI4\n", &su->sin.sin_addr);
		break;
	case AF_INET6:
		printf("%pI6\n", &su->sin6.sin6_addr);
		break;
#ifdef AF_LINK
	case AF_LINK: {
		struct sockaddr_dl *sdl;

		sdl = (struct sockaddr_dl *)&(su->sa);
		printf("link#%d\n", sdl->sdl_index);
	} break;
#endif /* AF_LINK */
	default:
		printf("af_unknown %d\n", su->sa.sa_family);
		break;
	}
}

/*
 * 按“族 + 地址”排序比较两个地址（不含端口）。
 *   su1 / su2 —— 待比较地址。
 * 返回值：su1 小于 su2 返回 -1，相等返回 0，大于返回 1。
 * 规则：先比 sa_family；同族时 v4 按主机序 32 位比较，v6 用 IPV6_ADDR_CMP。
 * 注意：v6 只比地址不比 scope_id，与 sockunion_same 略有差异。
 */
int sockunion_cmp(const union sockunion *su1, const union sockunion *su2)
{
	if (su1->sa.sa_family > su2->sa.sa_family)
		return 1;
	if (su1->sa.sa_family < su2->sa.sa_family)
		return -1;

	if (su1->sa.sa_family == AF_INET) {
		if (ntohl(sockunion2ip(su1)) == ntohl(sockunion2ip(su2)))
			return 0;
		if (ntohl(sockunion2ip(su1)) > ntohl(sockunion2ip(su2)))
			return 1;
		else
			return -1;
	}
	if (su1->sa.sa_family == AF_INET6)
		return IPV6_ADDR_CMP(&su1->sin6.sin6_addr, &su2->sin6.sin6_addr);

	return 0;
}

/*
 * 复制一份 sockunion。
 *   su —— 源地址。
 * 返回值：新分配的副本（XCALLOC 后 memcpy），调用方用 sockunion_free 释放。
 */
/* Duplicate sockunion. */
union sockunion *sockunion_dup(const union sockunion *su)
{
	union sockunion *dup =
		XCALLOC(MTYPE_SOCKUNION, sizeof(union sockunion));
	memcpy(dup, su, sizeof(union sockunion));
	return dup;
}

/*
 * 释放由 sockunion_str2su / _dup / _getsockname / _getpeername 分配的
 * sockunion。su 为待释放指针（XFREE，接受 NULL）。
 */
void sockunion_free(union sockunion *su)
{
	XFREE(MTYPE_SOCKUNION, su);
}

/*
 * 初始化一个 sockunion：整体清零（等价于 memset 0，族为 AF_UNSPEC）。
 *   su —— 待初始化地址。无返回值。
 */
void sockunion_init(union sockunion *su)
{
	memset(su, 0, sizeof(union sockunion));
}

/*
 * printfrr 扩展 %pSU：把任意 sockaddr 家族指针打印为可读地址文本。
 * 支持在格式串里追加标志字母：
 *   %pSU  —— 仅地址。
 *   %pSUp —— 地址 + 端口（IPv6 会加方括号）。
 *   %pSUps / %pSUs —— 追加 scope（%scope_id，IPv6 链路本地常用）。
 *   ptr 为 NULL 时打印 "(null)"。
 * AF_UNIX 用 %pSE 打印路径（Linux 抽象命名空间以 @ 前缀表示）。
 * 返回值：写入的字节数（ssize_t）。
 * 实现要点：函数先解析格式串中紧跟的 p / s 标志位，再按地址族分支输出。
 */
printfrr_ext_autoreg_p("SU", printfrr_psu);
/*
 * %pSU 扩展的落地实现（见上方说明）。ptr 指向 union sockunion（或兼容的
 * sockaddr）；ea 携带格式化参数（含格式串游标 fmt，用于解析 p / s 标志）。
 * 返回值：写入 fbuf 的字节数。
 */
static ssize_t printfrr_psu(struct fbuf *buf, struct printfrr_eargs *ea,
			    const void *ptr)
{
	const union sockunion *su = ptr;
	bool include_port = false, include_scope = false;
	bool endflags = false;
	ssize_t ret = 0;
	char cbuf[INET6_ADDRSTRLEN];

	if (!su)
		return bputs(buf, "(null)");

	while (!endflags) {
		switch (*ea->fmt) {
		case 'p':
			ea->fmt++;
			include_port = true;
			break;
		case 's':
			ea->fmt++;
			include_scope = true;
			break;
		default:
			endflags = true;
			break;
		}
	}

	switch (sockunion_family(su)) {
	case AF_UNSPEC:
		ret += bputs(buf, "(unspec)");
		break;
	case AF_INET:
		inet_ntop(AF_INET, &su->sin.sin_addr, cbuf, sizeof(cbuf));
		ret += bputs(buf, cbuf);
		if (include_port)
			ret += bprintfrr(buf, ":%d", ntohs(su->sin.sin_port));
		break;
	case AF_INET6:
		if (include_port)
			ret += bputch(buf, '[');
		inet_ntop(AF_INET6, &su->sin6.sin6_addr, cbuf, sizeof(cbuf));
		ret += bputs(buf, cbuf);
		if (include_scope && su->sin6.sin6_scope_id)
			ret += bprintfrr(buf, "%%%u",
					 (unsigned int)su->sin6.sin6_scope_id);
		if (include_port)
			ret += bprintfrr(buf, "]:%d",
					 ntohs(su->sin6.sin6_port));
		break;
	case AF_UNIX: {
		int len;
#ifdef __linux__
		if (su->sun.sun_path[0] == '\0' && su->sun.sun_path[1]) {
			len = strnlen(su->sun.sun_path + 1,
				      sizeof(su->sun.sun_path) - 1);
			ret += bprintfrr(buf, "@%*pSE", len,
					 su->sun.sun_path + 1);
			break;
		}
#endif
		len = strnlen(su->sun.sun_path, sizeof(su->sun.sun_path));
		ret += bprintfrr(buf, "%*pSE", len, su->sun.sun_path);
		break;
	}
	default:
		ret += bprintfrr(buf, "(af %d)", sockunion_family(su));
	}

	return ret;
}

/*
 * 判断地址是否为“空 / 未指定”。
 *   su —— 地址。
 * 返回值：1 为空、0 非空。
 * 规则：AF_UNSPEC 视为空；IPv4 地址为 0.0.0.0（s_addr == 0）为空；
 *   IPv6 全 0（:: ）为空；其他族返回 0（非空）。
 */
int sockunion_is_null(const union sockunion *su)
{
	unsigned char null_s6_addr[16] = {0};

	switch (sockunion_family(su)) {
	case AF_UNSPEC:
		return 1;
	case AF_INET:
		return (su->sin.sin_addr.s_addr == 0);
	case AF_INET6:
		return !IPV6_ADDR_CMP(su->sin6.sin6_addr.s6_addr, null_s6_addr);
	default:
		return 0;
	}
}

/*
 * printfrr 扩展 %dPF：把整型值当作协议族（PF_* / AF_*）打印成符号名，
 * 例如 AF_INET、AF_INET6、AF_UNIX、AF_PACKET、AF_NETLINK；未知值打印
 * "AF_(<数值>)"。
 * 用途：日志里打印协议族时避免出现裸数字。
 * 返回值：写入字节数。
 */
printfrr_ext_autoreg_i("PF", printfrr_pf);
/*
 * %dPF 扩展的实现：val 为要打印的协议族值，按已知 PF / AF 常量输出符号名，
 * 未匹配则回退为 "AF_(<数值>)"。返回值：写入字节数。
 */
static ssize_t printfrr_pf(struct fbuf *buf, struct printfrr_eargs *ea,
			   uintmax_t val)
{
	switch (val) {
	case AF_INET:
		return bputs(buf, "AF_INET");
	case AF_INET6:
		return bputs(buf, "AF_INET6");
	case AF_UNIX:
		return bputs(buf, "AF_UNIX");
#ifdef AF_PACKET
	case AF_PACKET:
		return bputs(buf, "AF_PACKET");
#endif
#ifdef AF_NETLINK
	case AF_NETLINK:
		return bputs(buf, "AF_NETLINK");
#endif
	}
	return bprintfrr(buf, "AF_(%ju)", val);
}

/*
 * printfrr 扩展 %dSO：把整型值当作套接字类型（SOCK_*）打印成符号名，
 * 例如 SOCK_STREAM、SOCK_DGRAM、SOCK_SEQPACKET、SOCK_RAW、SOCK_PACKET；
 * 未知值打印 "SOCK_(<数值>)"。
 * 返回值：写入字节数。
 */
printfrr_ext_autoreg_i("SO", printfrr_so);
/*
 * %dSO 扩展的实现：val 为要打印的套接字类型值，按已知 SOCK_* 常量输出
 * 符号名，未匹配则回退为 "SOCK_(<数值>)"。返回值：写入字节数。
 * 说明：本函数是本文件最后一个函数。
 */
static ssize_t printfrr_so(struct fbuf *buf, struct printfrr_eargs *ea,
			   uintmax_t val)
{
	switch (val) {
	case SOCK_STREAM:
		return bputs(buf, "SOCK_STREAM");
	case SOCK_DGRAM:
		return bputs(buf, "SOCK_DGRAM");
	case SOCK_SEQPACKET:
		return bputs(buf, "SOCK_SEQPACKET");
#ifdef SOCK_RAW
	case SOCK_RAW:
		return bputs(buf, "SOCK_RAW");
#endif
#ifdef SOCK_PACKET
	case SOCK_PACKET:
		return bputs(buf, "SOCK_PACKET");
#endif
	}
	return bprintfrr(buf, "SOCK_(%ju)", val);
}
