// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Socket union header.
 * Copyright (c) 1997 Kunihiro Ishiguro
 */

/*
 * ============================================================
 * 文件导读：sockunion.h —— 地址族无关的套接字地址封装（接口声明）
 * ============================================================
 *
 * 这个文件在 FRR 里干什么
 *   FRR 里到处要处理“一个套接字地址”，它可能是 IPv4、IPv6，在 OpenBSD
 *   上还可能是 MPLS 标签路由或路由标签。若各协议各写一套分支会极其
 *   繁琐。union sockunion 用同一个 union 同时容纳 sockaddr、sockaddr_in、
 *   sockaddr_in6、sockaddr_un 等，配合一组工具函数，实现：
 *     - 字符串 to 地址（str2sockunion / sockunion_str2su）
 *     - 地址 to 字符串（sockunion2str / inet_sutop）
 *     - 比较 / 哈希（sockunion_cmp / sockunion_same / sockunion_hash）
 *     - 取地址 / 长度（sockunion_get_addr / sockunion_get_addrlen）
 *     - socket 建立、bind、accept、connect、getsockname / getpeername
 *     - 一些 socket 选项封装（reuseaddr / reuseport / v6only / ttl 等）
 *
 * 核心数据结构
 *   union sockunion            —— 地址族无关地址主体。
 *   union sockaddrptr / _constptr —— 便于把任意 sockaddr 指针传给只接受
 *     某一种指针类型的函数，而不必强制类型转换（TRANSPARENT_UNION 技巧）。
 *   enum connect_result        —— connect() 的结果分类（成功 / 进行中 / 失败）。
 *
 * 与其他模块的关系
 *   全 FRR 通用。依赖 compiler.h（uniontype / TRANSPARENT_UNION）、privs.h、
 *   if.h（ifindex_t）；被 bgpd / ospfd / 等几乎所有网络代码使用。printfrr
 *   扩展 %pSU 让它能直接打印地址，%dPF / %dSO 打印协议族 / 套接字类型。
 *
 * 阅读顺序建议
 *   先看 union sockunion 与两个 transparent union，再看字符串转换两个函数
 *   的声明，最后看 socket 生命周期相关的一组声明；具体实现见 sockunion.c。
 * ============================================================
 */

#ifndef _ZEBRA_SOCKUNION_H
#define _ZEBRA_SOCKUNION_H

#include "compiler.h"

#include "privs.h"
#include "if.h"
#include <sys/un.h>
#ifdef __OpenBSD__
#include <net/route.h>
#include <netmpls/mpls.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 地址族无关的套接字地址联合体。通过 .sa.sa_family 判别实际有效成员：
 *   sa / sa_storage —— 通用视图；sa_storage 保证足够大且对齐。
 *   sin   —— AF_INET。
 *   sin6  —— AF_INET6。
 *   sun   —— AF_UNIX。
 *   smpls / rtlabel —— OpenBSD 专有（MPLS / 路由标签）。
 * 使用约定：先看 sa_family 再访问对应成员；sockunion_init 负责清零。
 */
union sockunion {
	struct sockaddr sa;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr_un sun;
#ifdef __OpenBSD__
	struct sockaddr_mpls smpls;
	struct sockaddr_rtlabel rtlabel;
#endif

	/* sockaddr_storage is guaranteed to be larger than the others */
	struct sockaddr_storage sa_storage;
};

/*
 * 透明联合体技巧：凡是“想接受任意 sockaddr 指针又不想显式强制转换”的
 * 函数，都可用 union sockaddrptr 作为形参类型。uniontype 宏（来自
 * compiler.h）把各成员统一成同一类型，从而任何 sockaddr 系列的指针都
 * 能直接传入；TRANSPARENT_UNION 是编译器属性，令该 union 在 ABI 上等同
 * 于其成员（通常是 sockaddr 指针）。
 * 下方的 const 版本 union sockaddrconstptr 用法相同，只是成员带 const。
 */
/* clang-format off */
/* for functions that want to accept any sockaddr pointer without casts */
union sockaddrptr {
	uniontype(sockaddrptr, union sockunion, su)
	uniontype(sockaddrptr, struct sockaddr, sa)
	uniontype(sockaddrptr, struct sockaddr_in, sin)
	uniontype(sockaddrptr, struct sockaddr_in6, sin6)
	uniontype(sockaddrptr, struct sockaddr_un, sun)
#ifdef __OpenBSD__
	uniontype(sockaddrptr, struct sockaddr_mpls, smpls)
	uniontype(sockaddrptr, struct sockaddr_rtlabel, rtlabel)
#endif
	uniontype(sockaddrptr, struct sockaddr_storage, sa_storage)
} TRANSPARENT_UNION;

union sockaddrconstptr {
	uniontype(sockaddrconstptr, const union sockunion, su)
	uniontype(sockaddrconstptr, const struct sockaddr, sa)
	uniontype(sockaddrconstptr, const struct sockaddr_in, sin)
	uniontype(sockaddrconstptr, const struct sockaddr_in6, sin6)
	uniontype(sockaddrconstptr, const struct sockaddr_un, sun)
#ifdef __OpenBSD__
	uniontype(sockaddrconstptr, const struct sockaddr_mpls, smpls)
	uniontype(sockaddrconstptr, const struct sockaddr_rtlabel, rtlabel)
#endif
	uniontype(sockaddrconstptr, const struct sockaddr_storage, sa_storage)
} TRANSPARENT_UNION;
/* clang-format on */

/*
 * connect() 的结果分类，供 sockunion_connect 返回：
 *   connect_error      —— 立即失败。
 *   connect_success    —— 连接已建立。
 *   connect_in_progress—— 非阻塞连接进行中（EINPROGRESS），需等可写事件
 *                         后由 getsockopt(SO_ERROR) 确认。
 */
enum connect_result { connect_error, connect_success, connect_in_progress };

/*
 * sockunion 的默认地址族：当不知道对端该用 v4 还是 v6 时，默认按 IPv6
 * 处理（套接字通常建成 IPv6，再由双栈能力兼容 v4-mapped）。
 */
/* Default address family. */
#define AF_INET_UNION AF_INET6

/*
 * 存放地址字符串的缓冲区最小长度，46 与 INET6_ADDRSTRLEN 一致
 * （足以放下 "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255" 之类的最长
 * IPv6 表示，含结尾 0）。调用方给 buf 分配时至少用这么大。
 */
/* Sockunion address string length.  Same as INET6_ADDRSTRLEN. */
#define SU_ADDRSTRLEN 46

/*
 * 便捷访问宏：
 *   sockunion_family —— 取地址族（sa_family）。
 *   sockunion2ip     —— 取 IPv4 地址的 32 位网络序值（仅当确为 v4 时有效）。
 */
#define sockunion_family(X)  (X)->sa.sa_family

#define sockunion2ip(X)      (X)->sin.sin_addr.s_addr

/*
 * 以下为 sockunion 工具函数的声明，具体实现见 sockunion.c。大致分三组：
 *   1) 字符串转换与比较 / 哈希；
 *   2) 地址原始字节访问（取地址指针 / 长度、按族设地址）；
 *   3) socket 生命周期（建 socket、bind、accept、connect、取本地 / 对端
 *      地址）以及若干 socket 选项封装（reuseaddr / reuseport / v6only /
 *      ttl / minttl）。
 */
/* Prototypes. */
/*
 * 字符串与 sockunion 互转，以及比较 / 哈希：
 *   str2sockunion  —— 解析 "a.b.c.d" 或 IPv6 文本到 su，成功返回 0、失败 -1。
 *   sockunion2str  —— 把 su 格式化为文本写入 buf（长度 len），返回 buf。
 *   sockunion_cmp  —— 比较两个地址，返回 -1 / 0 / 1（含族不同、IPv6 scope 等）。
 *   sockunion_same —— 只看地址（含 scope）是否相同，返回 1 / 0。
 *   sockunion_hash —— 计算地址哈希值，用于哈希表。
 */
extern int str2sockunion(const char *str, union sockunion *su);
extern const char *sockunion2str(const union sockunion *su, char *buf,
				 size_t len);
extern int sockunion_cmp(const union sockunion *su1, const union sockunion *su2);
extern int sockunion_same(const union sockunion *su1, const union sockunion *su2);
extern unsigned int sockunion_hash(const union sockunion *su);

/*
 * 地址原始字节访问：
 *   family2addrsize      —— 地址族对应的地址字节数（v4=4、v6=16）。
 *   sockunion_get_addrlen—— su 的地址字节数。
 *   sockunion_get_addr   —— 返回指向地址字节的指针（v4 / v6 数组）。
 *   sockunion_set        —— 用给定族、地址字节、长度设置 su（会清零后拷贝）。
 */
extern size_t family2addrsize(int family);
extern size_t sockunion_get_addrlen(const union sockunion *su);
extern const uint8_t *sockunion_get_addr(const union sockunion *);
extern void sockunion_set(union sockunion *su, int family, const uint8_t *addr, size_t bytes);

/*
 * socket 生命周期与选项封装：
 *   sockunion_str2su      —— 解析字符串并返回新分配（XMALLOC）的 sockunion，
 *                            失败返回 NULL；用完需 sockunion_free。
 *   sockunion_accept      —— accept 并在 su 中返回对端地址。
 *   sockunion_sizeof      —— su 的 sockaddr 实际长度（传给 bind 等）。
 *   sockunion_stream_socket—— 按 su 的族建一个 SOCK_STREAM 套接字。
 *   sockopt_reuseaddr / _reuseport —— 设 SO_REUSEADDR / SO_REUSEPORT。
 *   sockopt_v6only        —— 控制 IPv6 套接字的 IPV6_V6ONLY。
 *   sockunion_bind        —— bind 到 su:port（su_addr 可用于指定源地址变体）。
 *   sockopt_ttl / _minttl —— 设 IP_TTL / IP_MINTTL（IPv4）。
 *   sockunion_socket      —— 按 su 的族建一个合适类型的套接字。
 *   inet_sutop            —— 把 su 转成文本（写 str）。
 *   sockunion_connect     —— 连接 su:port，返回 enum connect_result。
 *   sockunion_getsockname / _getpeername —— 取本地 / 对端地址（新分配，需 free）。
 *   sockunion_dup         —— 复制一份 sockunion（新分配）。
 *   sockunion_free        —— 释放上述新分配的 sockunion。
 *   sockunion_init        —— 初始化（清零）一个 sockunion。
 *   sockunion_is_null     —— 判断地址是否为未指定 / 空（返回 1 / 0）。
 */
extern union sockunion *sockunion_str2su(const char *str);
extern int sockunion_accept(int sock, union sockunion *su);
extern int sockunion_sizeof(const union sockunion *su);
extern int sockunion_stream_socket(union sockunion *su);
extern int sockopt_reuseaddr(int sock);
extern int sockopt_reuseport(int sock);
extern int sockopt_v6only(int family, int sock);
extern int sockunion_bind(int sock, union sockunion *su, unsigned short port,
			  union sockunion *su_addr);
extern int sockopt_ttl(int family, int sock, int ttl);
extern int sockopt_minttl(int family, int sock, int minttl);
extern int sockunion_socket(const union sockunion *su);
extern const char *inet_sutop(const union sockunion *su, char *str);
extern enum connect_result sockunion_connect(int fd, const union sockunion *su, unsigned short port);
extern union sockunion *sockunion_getsockname(int);
extern union sockunion *sockunion_getpeername(int);
extern union sockunion *sockunion_dup(const union sockunion *);
extern void sockunion_free(union sockunion *su);
extern void sockunion_init(union sockunion *su);
extern int sockunion_is_null(const union sockunion *su);

/*
 * 注册 printfrr 自定义格式扩展（编译期检查 printf 参数类型）：
 *   %pSU —— 接受任意 sockaddr 家族指针，打印为 "地址:端口" 形式文本。
 *   %dPF —— 把 int 当作协议族（PF_INET 等）打印。
 *   %dSO —— 把 int 当作套接字类型（SOCK_STREAM 等）打印。
 * 这些扩展让日志代码能直接写 zlog_info("%pSU", su) 而无需手动转字符串。
 */
#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pSU"  (union sockunion *)
#pragma FRR printfrr_ext "%pSU"  (struct sockaddr *)
#pragma FRR printfrr_ext "%pSU"  (struct sockaddr_storage *)
#pragma FRR printfrr_ext "%pSU"  (struct sockaddr_in *)
#pragma FRR printfrr_ext "%pSU"  (struct sockaddr_in6 *)
#pragma FRR printfrr_ext "%pSU"  (struct sockaddr_un *)

/* AF_INET/PF_INET & co., using "PF" to avoid confusion with AFI/SAFI */
#pragma FRR printfrr_ext "%dPF"  (int)
/* SOCK_STREAM & co. */
#pragma FRR printfrr_ext "%dSO"  (int)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_SOCKUNION_H */
