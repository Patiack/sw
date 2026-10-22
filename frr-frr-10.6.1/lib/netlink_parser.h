// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Stub netlink parser library module
 * Copyright (c) 2024 Cisco Systems Inc.
 */

/*
 * ============================================================================
 * 【文件导读】lib/netlink_parser.h —— netlink 消息解析库的对外接口
 * ============================================================================
 * 职责：
 *   本头文件声明了 Zebra 与各协议守护进程（bgpd / ospfd / isisd /
 *   staticd / pimd 等）共享的 netlink 消息“解析 + 构造”工具箱。
 *   Linux 内核通过 netlink socket 把路由、链路、地址、nexthop 等
 *   信息以“消息头 + 变长 TLV 属性”的二进制格式送给用户态；本层把这套
 *   裸字节流抽象成可读写的 C 结构，供上层协议模块复用。
 *
 * 核心数据结构：
 *   struct nlmsghdr  —— netlink 消息头（type / flags / seq / pid / len）。
 *   struct rtattr    —— 传统路由属性（routing attribute，TLV）。
 *   struct nlattr    —— 通用 netlink 属性（nla_len / nla_type）。
 *   struct rtnexthop —— 路由的 nexthop 记录。
 *   上层通常用“指针数组 tb[max+1] + netlink_parse_rtattr()”来索引一条
 *   消息里的各个属性：tb[type] 直接指向对应 TLV。
 *
 * 与其他模块的关系：
 *   - 由 lib/netlink_parser.c 实现。
 *   - zebra 的 dplane_netlink.c、zebra_netlink.c 以及各协议里的
 *     netlink 辅助代码会调用这里的接口。
 *   - 依赖内核头 linux/netlink.h 与 linux/rtnetlink.h。
 *   - 仅在编译期定义了 HAVE_NETLINK 时才有内容。
 *
 * 阅读顺序：
 *   1. 先看顶部这一组宏（RTM_NHA / NLMSG_TAIL / RTA_TAIL / ...），它们
 *      是后面所有“往缓冲区里塞属性”函数的地基。
 *   2. 再看解析类原型：netlink_parse_buf 与 netlink_parse_rtattr 系列。
 *   3. 最后看构造类原型：nl_attr_nest 系列、nl_attr_put 系列、
 *      nl_rta_put 系列、nl_attr_rtnh 系列、nl_addraw_l。
 *
 * 易踩的坑：
 *   - 对齐：netlink 要求 4 字节对齐，NLMSG_ALIGN / RTA_ALIGN /
 *     NLA_ALIGN 必须成对使用，否则结构体错位。
 *   - nlmsg_len 语义：宏 NLMSG_TAIL 依赖当前 nlmsg_len 指到消息尾部，
 *     手工改过 len 后再追加属性极易越界。
 *   - NLA_F_NESTED 位会与 type 混在一起，取值时必须用 NLA_TYPE_MASK。
 *   - rtattr 与 nlattr 是两套不同的 TLV（长度字段宽度不同），不可混用。
 * ============================================================================
 */

#ifndef LIB_NETLINK_PARSER_H
#define LIB_NETLINK_PARSER_H 1

#include "lib/zebra.h"

/* 以下所有内容仅在编译时启用了 netlink 支持（HAVE_NETLINK）时生效。 */
/* Only used with netlink, for now */
#ifdef HAVE_NETLINK

/* 引入内核的 netlink 与 rtnetlink 结构定义（nlmsghdr / rtattr /
 * nlattr / rtnexthop 等结构体的原始出处）。
 */
#include "linux/netlink.h"
#include "linux/rtnetlink.h"

/* RTM_NHA：由 nhmsg 头指针算出其后第一个 rtattr 的地址，即“nexthop
 * 消息头之后紧跟的属性”起点；内部用 NLMSG_ALIGN 对 sizeof(struct
 * nhmsg) 做 4 字节对齐。
 */
#define RTM_NHA(h)                                                             \
	((struct rtattr *)(((char *)(h)) + NLMSG_ALIGN(sizeof(struct nhmsg))))

/* NLMSG_TAIL：指向 netlink 消息当前尾部，可直接在其上追加新属性。
 * 用 #ifndef 包裹，避免与内核头里的同名宏冲突。
 * 只有在 nlmsg_len 已被正确维护时结果才有效。
 */
#ifndef NLMSG_TAIL
#define NLMSG_TAIL(nmsg)                                                       \
	((struct rtattr *)(((uint8_t *)(nmsg))                                 \
			   + NLMSG_ALIGN((nmsg)->nlmsg_len)))
#endif

/* RTA_TAIL：指向某个 rtattr 当前尾部，用于在其后追加子属性。
 * 同样用 #ifndef 包裹以防与内核定义冲突。
 */
#ifndef RTA_TAIL
#define RTA_TAIL(rta)                                                          \
	((struct rtattr *)(((uint8_t *)(rta)) + RTA_ALIGN((rta)->rta_len)))
#endif

/* NLA_DATA：跳过 nlattr 头，返回其数据区指针（按 4 字节对齐）。
 * 注意与 rtattr 的 RTA_DATA 区分：两者头部布局不同，不可互换。
 */
#define NLA_DATA(nla) ((struct nlattr *)(((char *)(nla)) + NLA_LENGTH(0)))

/* ERR_NLA：在 nlmsgerr 结构之后定位内嵌的 nlattr。
 * 先跨过 nlmsgerr 头，再按 inner_len 对齐偏移到属性区。
 */
#define ERR_NLA(err, inner_len)                                                \
	((struct nlattr *)(((char *)(err))                                     \
			   + NLMSG_ALIGN(sizeof(struct nlmsgerr))              \
			   + NLMSG_ALIGN((inner_len))))

/* NLA_LENGTH：给定数据长度，算出“含 nlattr 头并做 4 字节对齐”后的总长。 */
#define NLA_LENGTH(len) (NLA_ALIGN(sizeof(struct nlattr)) + (len))

/* nl_msg_init：在调用者提供的缓冲区 buf 里就地初始化一个 netlink 消息头。
 * 参数依次为消息类型、标志、序列号、pid、缓冲区及其长度。
 * 若缓冲区连一个 nlmsghdr 都放不下则返回 NULL。
 */
/* Init nl message in-place in a buffer */
struct nlmsghdr *nl_msg_init(int type, int flags, int seq, int pid, uint8_t *buf,
			     uint32_t buflen);
/* netlink_parse_buf：把一段裸缓冲区解析成 netlink 消息，并把其中的
 * rtattr 属性填入索引数组 tb（大小为 max+1）。返回消息头指针，
 * 失败返回 NULL。是“解析 inbound 消息”的总入口。
 */
struct nlmsghdr *netlink_parse_buf(struct rtattr **tb, int max, void *buf, size_t len);
/* nl_msg_get_data：从消息头中取出长度、类型、标志三个字段并通过出参返回；
 * n 为 NULL 时不做任何事。
 */
void nl_msg_get_data(const struct nlmsghdr *n, uint16_t *ptype, uint32_t *plen,
		     uint16_t *pflags);
/* netlink_parse_rtattr：遍历 rtattr 链，把每个属性按 type 存入 tb[type]
 * （仅当 type <= max）。内部用 NLA_TYPE_MASK 屏蔽嵌套标志位。
 */
void netlink_parse_rtattr(struct rtattr **tb, int max, struct rtattr *rta, int len);
/* netlink_parse_rtattr_nested：解析“嵌套”属性——把某个 rtattr 的数据区
 * 再当作一层 rtattr 链来展开。
 */
void netlink_parse_rtattr_nested(struct rtattr **tb, int max, struct rtattr *rta);

/* netlink_parse_nlattr：与 netlink_parse_rtattr 类似，但解析的是通用
 * netlink 属性 nlattr（长度字段为 16 位）。
 */
void netlink_parse_nlattr(struct nlattr **tb, int max, struct nlattr *nla, int len);

/* nl_attr_nest：开启一个属性“嵌套区”。成功返回嵌套起点的 rtattr 指针，
 * 失败（缓冲区放不下）返回 NULL。之后用 nl_attr_nest_end 收尾。
 */
/*
 * nl_attr_nest - start an attribute nest.
 *
 * Returns a valid pointer to the beginning of the nest if the attribute
 * describing the nest could be added to the message (fits into the buffer),
 * otherwise NULL is returned.
 */
struct rtattr *nl_attr_nest(struct nlmsghdr *n, unsigned int maxlen, int type);

/* nl_attr_nest_end：结束嵌套区，回填嵌套属性的总长度，
 * 返回整条 netlink 消息的当前总长。
 */
/*
 * nl_attr_nest_end - finalize nesting of attributes.
 *
 * Updates the length field of the attribute header to include the appeneded
 * attributes. Returns a total length of the Netlink message.
 */
int nl_attr_nest_end(struct nlmsghdr *n, struct rtattr *nest);

/* nl_attr_put：向 netlink 消息追加一个 rtattr 属性（type + data[alen]）。
 * 放不下返回 false，否则 true；data 为 NULL 时要求 alen 为 0。
 * 下面 put8 / put16 / put32 / put64 是针对定长标量的便捷包装。
 */
/*
 * nl_attr_put - add an attribute to the Netlink message.
 *
 * Returns true if the attribute could be added to the message (fits into the
 * buffer), otherwise false is returned.
 */
bool nl_attr_put(struct nlmsghdr *n, unsigned int maxlen, int type, const void *data,
		 unsigned int alen);
/* nl_attr_put8：以 8 位标量数据追加一个属性的便捷包装。 */
bool nl_attr_put8(struct nlmsghdr *n, unsigned int maxlen, int type, uint8_t data);
/* nl_attr_put16：以 16 位标量数据追加一个属性的便捷包装。 */
bool nl_attr_put16(struct nlmsghdr *n, unsigned int maxlen, int type, uint16_t data);
/* nl_attr_put32：以 32 位标量数据追加一个属性的便捷包装。 */
bool nl_attr_put32(struct nlmsghdr *n, unsigned int maxlen, int type, uint32_t data);
/* nl_attr_put64：以 64 位标量数据追加一个属性的便捷包装。 */
bool nl_attr_put64(struct nlmsghdr *n, unsigned int maxlen, int type, uint64_t data);

/* nl_rta_put：在一个已存在的 rtattr 内部追加“子属性”，用于手工构造嵌套
 * 结构；放不下返回 false。put16 / put64 为其定长标量包装。
 */
/*
 * nl_rta_put - add an additional optional attribute(rtattr) to the
 * Netlink message buffer.
 *
 * Returns true if the attribute could be added to the message (fits into the
 * buffer), otherwise false is returned.
 */
bool nl_rta_put(struct rtattr *rta, unsigned int maxlen, int type, const void *data,
		int alen);
/* nl_rta_put16：以 16 位标量数据向 rtattr 内追加子属性的便捷包装。 */
bool nl_rta_put16(struct rtattr *rta, unsigned int maxlen, int type, uint16_t data);
/* nl_rta_put64：以 64 位标量数据向 rtattr 内追加子属性的便捷包装。 */
bool nl_rta_put64(struct rtattr *rta, unsigned int maxlen, int type, uint64_t data);


/* nl_attr_rtnh：向消息追加一条 rtnexthop 记录（多路径下一跳），
 * 返回该记录指针，空间不足返回 NULL。
 */
/*
 * nl_attr_rtnh - append a rtnexthop record to the Netlink message.
 *
 * Returns a valid pointer to the rtnexthop struct if it could be added to
 * the message (fits into the buffer), otherwise NULL is returned.
 */
extern struct rtnexthop *nl_attr_rtnh(struct nlmsghdr *n, unsigned int maxlen);

/* nl_attr_rtnh_end：结束一条 rtnexthop 记录，回填其长度，
 * 使随后追加的 rtattr 归属于该 nexthop。
 */
/*
 * nl_attr_rtnh_end - finalize adding a rtnexthop record.
 *
 * Updates the length field of the rtnexthop to include the appeneded
 * attributes.
 */
extern void nl_attr_rtnh_end(struct nlmsghdr *n, struct rtnexthop *rtnh);

/* nl_addraw_l：把一段裸数据按 4 字节对齐地拷入消息尾部，
 * 并保证不超过 maxlen。成功返回 true，越界返回 false。
 */
/*
 * nl_addraw_l copies raw form the netlink message buffer into netlink
 * message header pointer. It ensures the aligned data buffer does not
 * override past max length.
 * return value is 0 if its successful
 */
extern bool nl_addraw_l(struct nlmsghdr *n, unsigned int maxlen, const void *data,
			unsigned int len);
/* nl_rta_nest：在给定 rtattr 内开启一个嵌套子属性，
 * 返回嵌套起点，失败返回 NULL。
 */
/*
 * nl_rta_nest - start an additional optional attribute (rtattr) nest.
 *
 * Returns a valid pointer to the beginning of the nest if the attribute
 * describing the nest could be added to the message (fits into the buffer),
 * otherwise NULL is returned.
 */
extern struct rtattr *nl_rta_nest(struct rtattr *rta, unsigned int maxlen, int type);
/* nl_rta_nest_end：结束该嵌套子属性并回填长度，
 * 返回父 rtattr 的当前总长。
 */
/*
 * nl_rta_nest_end - finalize nesting of an aditionl optionl attributes.
 *
 * Updates the length field of the attribute header to include the appeneded
 * attributes. Returns a total length of the Netlink message.
 */
extern int nl_rta_nest_end(struct rtattr *rta, struct rtattr *nest);

/* 结束 HAVE_NETLINK 条件编译：未启用 netlink 时本头文件几乎为空。 */
#endif /* HAVE_NETLINK */

/* 结束头文件 include guard，防止本文件被重复包含。 */
#endif /* LIB_NETLINK_PARSER_H */
