// SPDX-License-Identifier: GPL-2.0-or-later
/* Router advertisement
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * ============================================================
 * 文件导读：sockopt.h —— socket 选项封装的对外接口声明
 * ============================================================
 *
 * 这个文件在 FRR 里干什么
 *   FRR 各守护进程都要设置底层 socket 的各种选项（收发缓冲区大小、
 *   IPv6 跳数 / 流量类别、组播接口、IP_TOS、IP_HDRINCL、TCP MD5 签名、
 *   TCP MSS、TCP keepalive、IP_TRANSPARENT 等）。这些 setsockopt 调用
 *   在不同操作系统上参数类型 / 可用性差异极大（Linux 与 BSD / Solaris
 *   分支众多），直接散落在业务代码里既难读又难维护。sockopt.h / sockopt.c
 *   把这些零散的 socket 选项封装成一组平台无关的辅助函数，并把平台
 *   差异用 #ifdef 收拢在 sockopt.c 内。本头文件只暴露统一接口。
 *
 * 核心内容概览
 *   1. 收发缓冲区大小：setsockopt_so_recvbuf / setsockopt_so_sendbuf 及
 *      对应的 getsockopt_so_recvbuf / getsockopt_so_sendbuf。
 *   2. IPv6 专用选项：pktinfo、组播 / 单播跳数、hoplimit、multicast_loop、
 *      tclass（IPv6 版的 TOS）。
 *   3. 控制消息（cmsg）大小宏：SOPT_SIZE_CMSG_*，用于按地址族计算
 *      读取 ifindex 的辅助数据缓冲区大小，屏蔽 Linux in_pktinfo 与
 *      BSD recvif 的差异。
 *   4. IPv4 组播 / TOS：setsockopt_ipv4_multicast_if、setsockopt_ipv4_multicast、
 *      setsockopt_ipv4_tos 等（含 Linux / BSD 双实现）。
 *   5. ifindex 收发：setsockopt_ifindex / getsockopt_ifindex 的统一封装。
 *   6. IP_HDRINCL 头字段字节序转换：sockopt_iphdrincl_swab_htosys / _systoh。
 *   7. TCP 相关：sockopt_tcp_rtt、TCP MD5 签名（含扩展前缀版）、TCP MSS 设置 /
 *      读取、TCP keepalive 参数。
 *   8. IP_TRANSPARENT：sockopt_ip_transparent。
 *
 * 与其他模块的关系
 *   依赖 sockunion.h（地址族无关地址表示）。被 bgpd / ospfd / isisd /
 *   zebra 等几乎所有需要自建 socket 的守护进程使用，尤其是 BGP 的
 *   TCP MD5 鉴权与各协议的组播收发路径。
 *
 * 阅读顺序建议
 *   先读 sockopt.c 顶部各平台宏说明，再看本文件的接口声明；重点函数是
 *   setsockopt_ifindex / getsockopt_ifindex 与 sockopt_tcp_signature。
 * ============================================================
 */

#ifndef _ZEBRA_SOCKOPT_H
#define _ZEBRA_SOCKOPT_H

#include "sockunion.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 收发缓冲区大小封装。size 为期望的字节数。
 *   setsockopt_so_recvbuf / setsockopt_so_sendbuf：
 *     设置 SO_RCVBUF / SO_SNDBUF，返回 0 成功、-1 失败（失败时打印日志）。
 *   getsockopt_so_recvbuf / getsockopt_so_sendbuf：
 *     读取当前缓冲区大小并返回该值（-1 表示读取失败）。
 * 坑：Linux 内核对 SO_RCVBUF / SO_SNDBUF 有上下限裁剪，设置值与
 *   getsockopt 读回值可能不同（内核会加倍 / 取整并受 rmem_max 限制）。
 */
extern int setsockopt_so_recvbuf(int sock, int size);
extern int setsockopt_so_sendbuf(const int sock, int size);
extern int getsockopt_so_sendbuf(const int sock);
extern int getsockopt_so_recvbuf(const int sock);

/*
 * IPv6 专用 socket 选项封装（参数 val 语义随选项而定）：
 *   setsockopt_ipv6_pktinfo       —— 开关（1 收 / 0 停收）IPV6_RECVPKTINFO，
 *                                    用于接收本机目的地址 / 收包接口信息。
 *   setsockopt_ipv6_multicast_hops—— IPV6_MULTICAST_HOPS（组播 TTL）。
 *   setsockopt_ipv6_unicast_hops  —— IPV6_UNICAST_HOPS（单播跳数）。
 *   setsockopt_ipv6_hoplimit      —— IPV6_HOPLIMIT（辅助数据中的跳数限制）。
 *   setsockopt_ipv6_multicast_loop—— IPV6_MULTICAST_LOOP（组播回环开关）。
 *   setsockopt_ipv6_tclass        —— IPV6_TCLASS（IPv6 版 TOS / DSCP）。
 * 统一返回 0 成功、-1 失败；失败仅记录日志不中断调用方。
 */
extern int setsockopt_ipv6_pktinfo(int sock, int val);
extern int setsockopt_ipv6_multicast_hops(int sock, int val);
extern int setsockopt_ipv6_unicast_hops(int sock, int val);
extern int setsockopt_ipv6_hoplimit(int sock, int val);
extern int setsockopt_ipv6_multicast_loop(int sock, int val);
extern int setsockopt_ipv6_tclass(int sock, int tclass);

/*
 * IPv6 版 pktinfo 控制消息大小（struct in6_pktinfo 就是其净荷）。
 * 不带 CMSG_SPACE 对齐，调用方通常还需自行按需补齐对齐。
 */
#define SOPT_SIZE_CMSG_PKTINFO_IPV6() (sizeof(struct in6_pktinfo));

/*
 * Size defines for control messages used to get ifindex.  We define
 * values for each method, and define a macro that can be used by code
 * that is unaware of which method is in use.
 * These values are without any alignment needed (see CMSG_SPACE in RFC3542).
 */
/*
 * 计算 IPv4 控制消息（cmsg）缓冲区大小，用于接收“收包接口 ifindex / 目的地址”。
 * 不同平台暴露 ifindex 的机制不同：
 *   - Linux 用 IP_PKTINFO，随包附带 struct in_pktinfo；
 *   - BSD / Solaris 用 IP_RECVIF，随包附带 struct sockaddr_dl；
 *   - 都没有时退化为一个 char * 的占位大小。
 * 这里按平台条件宏选择方案；下面再定义与平台无关的 SOPT_SIZE_CMSG_IFINDEX_IPV4
 * 与 SOPT_SIZE_CMSG_IFINDEX(af) 供上层统一使用。
 * 注意 RFC3542 的 CMSG_SPACE 已含对齐，故注释里说“不需要额外对齐”。
 */
#if defined(IP_PKTINFO)
/* Linux in_pktinfo. */
#define SOPT_SIZE_CMSG_PKTINFO_IPV4()  (CMSG_SPACE(sizeof(struct in_pktinfo)))
/* XXX This should perhaps be defined even if IP_PKTINFO is not. */
#define SOPT_SIZE_CMSG_PKTINFO(af)                                             \
  ((af == AF_INET) ? SOPT_SIZE_CMSG_PKTINFO_IPV4() \
                   : SOPT_SIZE_CMSG_PKTINFO_IPV6()
#endif /* IP_PKTINFO */

/*
 * BSD / Solaris 平台：用 IP_RECVIF 附带 struct sockaddr_dl 来获得收包接口。
 * 只定义 IPv4 的取值大小宏 SOPT_SIZE_CMSG_RECVIF_IPV4。
 */
#if defined(IP_RECVIF)
/* BSD/Solaris */

#define SOPT_SIZE_CMSG_RECVIF_IPV4()	(sizeof(struct sockaddr_dl))
#endif /* IP_RECVIF */

/*
 * 与平台无关的“IPv4 ifindex 控制消息大小”宏：
 *   优先用 IP_PKTINFO（Linux），其次用 IP_RECVIF（BSD / Solaris），
 *   两者都不支持时用一个指针大小占位。上层不必关心具体平台。
 */
/* SOPT_SIZE_CMSG_IFINDEX_IPV4 - portable type */
#if defined(SOPT_SIZE_CMSG_PKTINFO)
#define SOPT_SIZE_CMSG_IFINDEX_IPV4() SOPT_SIZE_CMSG_PKTINFO_IPV4()
#elif defined(SOPT_SIZE_CMSG_RECVIF_IPV4)
#define SOPT_SIZE_CMSG_IFINDEX_IPV4() SOPT_SIZE_CMSG_RECVIF_IPV4()
#else  /* Nothing available */
#define SOPT_SIZE_CMSG_IFINDEX_IPV4() (sizeof(char *))
#endif /* SOPT_SIZE_CMSG_IFINDEX_IPV4 */

/*
 * 按地址族统一取 ifindex 控制消息大小：IPv4 走上面选出的方案，
 * IPv6 恒为 struct in6_pktinfo 大小。
 */
#define SOPT_SIZE_CMSG_IFINDEX(af)                                             \
  (((af) == AF_INET) : SOPT_SIZE_CMSG_IFINDEX_IPV4() \
                    ? SOPT_SIZE_CMSG_PKTINFO_IPV6())

/*
 * If not defined then define the value for `TCP_MD5SIG_MAXKEYLEN`. This seems
 * to be unavailable for NetBSD 8, FreeBSD 11 and FreeBSD 12.
 *
 * The value below was copied from `linux/tcp.h` from the Linux kernel headers.
 */
/*
 * TCP MD5 签名（RFC 2385）的密钥最大长度。某些平台（NetBSD 8、
 * FreeBSD 11 / 12）头文件里没定义该宏，这里补一个 80 的缺省值，
 * 数值抄自 Linux 内核头文件 linux/tcp.h。
 */
#ifndef TCP_MD5SIG_MAXKEYLEN
#define TCP_MD5SIG_MAXKEYLEN 80
#endif

/*
 * IPv4 组播 / 接口绑定封装：
 *   setsockopt_ipv4_multicast_if   —— 设置 IP_MULTICAST_IF，by 接口地址
 *                                     （Linux 走 in_addr 版；若给了 ifindex
 *                                     在部分平台走 IP_MULTICAST_IF 变体）。
 *   setsockopt_ipv4_multicast      —— 通用组播选项设置（optname 指定具体
 *                                     选项，如加入 / 离开组），mcast_addr 为
 *                                     组播地址的 host order 值。
 *   setsockopt_ipv4_multicast_loop —— IP_MULTICAST_LOOP 开关（val 为 0 / 1）。
 * 返回 0 成功、-1 失败（仅记录日志）。
 */
extern int setsockopt_ipv4_multicast_if(int sock, struct in_addr if_addr,
					ifindex_t ifindex);
extern int setsockopt_ipv4_multicast(int sock, int optname,
				     struct in_addr if_addr,
				     unsigned int mcast_addr,
				     ifindex_t ifindex);
extern int setsockopt_ipv4_multicast_loop(int sock, uint8_t val);

/*
 * 设置 IPv4 IP_TOS（DSCP / 优先级）。tos 为完整 TOS 字节值。
 * 返回 0 成功、-1 失败（失败记录日志）。
 */
extern int setsockopt_ipv4_tos(int sock, int tos);

/*
 * 请求 / 读取收包接口索引（ifindex），屏蔽底层机制差异。
 *   setsockopt_ifindex(af, sock, val) —— 按地址族打开对应选项
 *        （IPv4：IP_PKTINFO / IP_RECVIF；IPv6：IPV6_RECVPKTINFO），
 *        以便后续从辅助数据中取得 ifindex。
 *   getsockopt_ifindex(af, msgh)      —— 从已接收消息的 msgh->msg_control
 *        中解析出收包接口索引，找不到返回 0（IFINDEX_INTERNAL）。
 */
/* Ask for, and get, ifindex, by whatever method is supported. */
extern int setsockopt_ifindex(int af, int sock, ifindex_t val);
extern ifindex_t getsockopt_ifindex(int af, struct msghdr *msgh);

/*
 * 在主机字节序与 IP_HDRINCL 期望的“系统字节序”之间转换 IPv4 头字段
 * （主要是 ip_len / ip_off 等）。Linux 要求 ip_len / ip_off 用主机序，
 * 而 BSD 要求网络序，于是用这两个函数做双向适配：
 *   _swab_htosys：host -> system
 *   _swab_systoh：system -> host
 * 仅在各自平台宏生效时才真正做转换（见 sockopt.c）。
 */
/* swab the fields in iph between the host order and system order expected
 * for IP_HDRINCL.
 */
extern void sockopt_iphdrincl_swab_htosys(struct ip *iph);
extern void sockopt_iphdrincl_swab_systoh(struct ip *iph);

/*
 * 读取 TCP 的 RTT 估计值（TCP_INFO 的 tcpi_rtt，单位微秒）。
 * 返回 RTT（微秒），失败返回 -1。用于需要 RTT 估计的协议逻辑。
 */
extern int sockopt_tcp_rtt(int sock);

/*
 * TCP MD5 签名（RFC 2385）。为指定对端地址启用 TCP_MD5SIG，使该 TCP
 * 连接上所有报文带 MD5 摘要，从而对 BGP 邻居做 TCP 层鉴权。
 *   sock     —— 已建立的（或监听）socket。
 *   su       —— 对端地址（union sockunion，v4 / v6 均可）。
 *   password —— 共享密钥，NULL 或空串表示清除该对端的 MD5 配置。
 * 返回 0 成功、-1 失败。注意：设置需在连接建立前或对已建立连接重设；
 * 各平台 option 结构体不同（Linux tcp_md5sig / BSD 变体）。
 */
/*
 * TCP MD5 signature option. This option allows TCP MD5 to be enabled on
 * addresses.
 *
 * sock
 *    Socket to enable option on.
 *
 * su
 *    Sockunion specifying address to enable option on.
 *
 * password
 *    MD5 auth password
 */
extern int sockopt_tcp_signature(int sock, union sockunion *su,
				 const char *password);

/*
 * 扩展 TCP MD5 签名（TCP_MD5SIG_EXT）。相比基础版，支持按“前缀”而非单
 * 一地址启用 MD5（例如整段 BGP peer 网段），以及 TCP_MD5SIG_FLAG_PREFIX。
 *   sock      —— socket。
 *   su        —— 地址或前缀。
 *   prefixlen —— 0 表示 su 是纯地址，回退到非扩展模式；否则为前缀掩码长度。
 *   password  —— 共享密钥，NULL 表示清除。
 * 返回 0 成功、-1 失败。仅 Linux 支持该扩展选项。
 */
/*
 * Extended TCP MD5 signature option. This option allows TCP MD5 to be enabled
 * on prefixes.
 *
 * sock
 *    Socket to enable option on.
 *
 * su
 *    Sockunion specifying address (or prefix) to enable option on.
 *
 * prefixlen
 *    0    - su is an address; fall back to non-extended mode
 *    Else - su is a prefix; prefixlen is the mask length
 *
 * password
 *    MD5 auth password
 */
extern int sockopt_tcp_signature_ext(int sock, union sockunion *su,
				     uint16_t prefixlen, const char *password);

/*
 * 设置 TCP 最大段大小（TCP_MAXSEG）。影响 SYN 阶段协商的 MSS，
 * 可用于限制大包 / 规避分片。tcp_maxseg 为期望的 MSS 字节数。
 * 返回 0 成功、-1 失败。
 */
/*
 * set TCP max segment size. This option allows user to configure
 * max segment size for TCP session
 *
 * sock
 *    Socket to enable option on.
 *
 * tcp_maxseg
 *    value used for TCP segment size negotiation during SYN
 */
extern int sockopt_tcp_mss_set(int sock, int tcp_maxseg);

/*
 * 读取当前 TCP 最大段大小（TCP_MAXSEG）。返回 MSS 值，失败返回 -1。
 * 注意读回值可能是对端通告后的实际生效值。
 */
/*
 * get TCP max segment size. This option allows user to get
 * the segment size for TCP session
 *
 * sock
 *    Socket to get max segement size.
 */
extern int sockopt_tcp_mss_get(int sock);

/*
 * 配置 TCP keepalive（SO_KEEPALIVE 及 TCP_KEEPIDLE / TCP_KEEPINTVL /
 * TCP_KEEPCNT）。对 BGP 这类长连接很关键：连接静默后靠保活探测及时
 * 发现对端失效。三个参数单位均为秒 / 次数。返回 0 成功、-1 失败。
 * 注意：仅在 Linux 上会同时设置三个子选项，其他平台分支见 .c。
 */
/*
 * Configure TCP keepalive for a given socket
 *
 * sock
 *   Socket to enable keepalive option on.
 *
 * keepalive_idle
 *   number of seconds a connection needs to be idle
 *   before sending out keep-alive proves
 *
 * keepalive_intvl
 *   number of seconds between TCP keep-alive probes
 *
 * keepalive_probes
 *   max number of probers to send before giving up
 *   and killing tcp connection
 */
extern int setsockopt_tcp_keepalive(int sock, uint16_t keepalive_idle,
				    uint16_t keepalive_intvl,
				    uint16_t keepalive_probes);

/*
 * 为 socket 打开 IP_TRANSPARENT（Linux 透明代理，允许绑定非本机地址）。
 * 无返回值；仅在支持该选项的平台生效（否则为空操作）。常用于 BGP
 * 需要以任意源地址建立连接的场景。
 */
/*
 * Set IP_TRANSPARENT option to socket
 *
 * sock
 *    Socket to enable option on.
 */
extern void sockopt_ip_transparent(int sock);

#ifdef __cplusplus
}
#endif

#endif /*_ZEBRA_SOCKOPT_H */
