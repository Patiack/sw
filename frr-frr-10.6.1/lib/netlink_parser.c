// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Netlink parser library module
 * Copyright (c) 2024 Cisco Systems Inc.
 */

/*
 * ============================================================================
 * 【文件导读】lib/netlink_parser.c —— netlink 消息解析库的实现
 * ============================================================================
 * 职责：
 *   实现 netlink_parser.h 声明的全套接口，负责在用户态“解析”内核发来的
 *   netlink 消息，以及“构造”要发往内核的 netlink 消息。它是 Zebra 与其
 *   他协议模块共享的底层字节流处理层：上层只管提供语义参数，如何按
 *   netlink 的 TLV 规则排布、对齐、回填长度都由本文件完成。
 *
 * 核心数据结构（均来自内核头）：
 *   struct nlmsghdr  —— 消息头，nlmsg_len 是总长（含头）。
 *   struct rtattr    —— 路由属性 TLV，rta_len 为 16 位。
 *   struct nlattr    —— 通用属性 TLV，nla_len 为 16 位。
 *   struct rtnexthop —— 多路径下一跳记录。
 *   本文件内部还用到两个“解析哨兵”宏 NLA_OK / NLA_NEXT 来遍历 nlattr。
 *
 * 与其他模块的关系：
 *   - 与 lib/netlink_parser.h 成对；被 zebra 数据面与各协议的 netlink
 *     辅助代码调用。
 *   - 仅当编译期定义 HAVE_NETLINK 时才有内容。
 *   - 出错路径会调用 zlog_err（lib/zlog.h）打印日志。
 *
 * 阅读顺序：
 *   1. 顶层宏与 include。
 *   2. 构造方向：nl_msg_init、nl_addraw_l、nl_attr_put 系列、
 *      nl_attr_nest 系列、nl_attr_rtnh 系列、nl_rta_put 系列、
 *      nl_rta_nest 系列。
 *   3. 解析方向：nl_msg_get_data、netlink_parse_rtattr、
 *      netlink_parse_buf、netlink_parse_rtattr_nested、
 *      netlink_parse_nlattr。
 *   4. 文件末尾的 NLA_OK / NLA_NEXT 宏服务于 netlink_parse_nlattr。
 *
 * 易踩的坑：
 *   - netlink 要求 4 字节对齐：所有 *_ALIGN 宏必须成对使用，len 字段
 *     回填务必用“尾指针 - 头指针”，否则上层解析会错位。
 *   - 越界防护：本文件的 put 系列都会先检查剩余空间，失败返回 false
 *     或 NULL，调用者必须逐次检查返回值。
 *   - netlink_parse_buf 只对 RTM_NEWROUTE / RTM_DELROUTE 跳过 rtmsg 头，
 *     其他消息类型需另行处理 offset。
 *   - 解析出的 tb[] 每个槽最多容纳一个属性，同 type 多实例需自行遍历。
 *   - nl_rta_nest 的返回值判断与 nl_attr_nest 不一致，阅读时留意。
 * ============================================================================
 */

#include "zebra.h"

/* 以下代码仅在编译期启用了 netlink 支持（HAVE_NETLINK）时生效。 */
/* Only used with netlink, for now */
#ifdef HAVE_NETLINK

/* 本模块自身的接口声明，以及日志设施（zlog_err 用于越界告警）。 */
#include "lib/netlink_parser.h"
#include "lib/zlog.h"

/* nl_msg_init：在调用者提供的缓冲区 buf 里就地初始化一个 netlink 消息头。
 * - 缓冲区不足以放下 nlmsghdr 时返回 NULL。
 * - 先清零头部，再设置 type / flags / seq / pid，并把 nlmsg_len 设为
 *   NLMSG_LENGTH(0)，表示“只有消息头、暂无负载”。
 */
/*
 * Init nl message in-place in a buffer
 */
struct nlmsghdr *nl_msg_init(int type, int flags, int seq, int pid,
			     uint8_t *buf, uint32_t buflen)
{
	struct nlmsghdr *n;

	if (buflen < sizeof(struct nlmsghdr))
		return NULL;

	n = (void *)buf;
	memset(n, 0, sizeof(struct nlmsghdr));

	n->nlmsg_type = type;
	n->nlmsg_flags = flags;
	n->nlmsg_seq = seq;
	n->nlmsg_pid = pid;
	n->nlmsg_len = NLMSG_LENGTH(0);

	return n;
}

/* nl_msg_get_data：读取消息头中的长度、类型、标志并写入出参。
 * n 为 NULL 时静默返回，不写任何出参。
 */
/* Access data from embedded message header */
void nl_msg_get_data(const struct nlmsghdr *n, uint16_t *ptype, uint32_t *plen,
		     uint16_t *pflags)
{
	if (n) {
		*plen = n->nlmsg_len;
		*ptype = n->nlmsg_type;
		*pflags = n->nlmsg_flags;
	}
}

/* netlink_parse_rtattr：把一条 rtattr 链解析进索引数组 tb。
 * 先把 tb[0..max] 全部清零，再逐个 RTA_OK 遍历：取 type 的低位
 * （用 NLA_TYPE_MASK 屏蔽 NLA_F_NESTED），仅当 type <= max 时记录
 * tb[type] = rta。越界 type 被安全忽略，避免写坏数组。
 */
void netlink_parse_rtattr(struct rtattr **tb, int max, struct rtattr *rta, int len)
{
	memset(tb, 0, sizeof(struct rtattr *) * (max + 1));
	/* 逐条遍历属性：RTA_OK 保证当前属性完整落在 len 范围内。 */
	while (RTA_OK(rta, len)) {
		/*
		 * The type may be &'ed with NLA_F_NESTED
		 * which puts data in the upper 8 bits of the
		 * rta_type.  Mask it off and save the actual
		 * underlying value to be placed into the array.
		 * This way we don't accidently crash in the future
		 * when the kernel sends us new data and we try
		 * to write well beyond the end of the array.
		 */
		uint16_t type = rta->rta_type & NLA_TYPE_MASK;

		if (type <= max)
			tb[type] = rta;
		rta = RTA_NEXT(rta, len);
	}
}

/* netlink_parse_buf：由缓冲区首址解析 netlink 消息头，并定位其属性区。
 * - 若 len 小于消息自称的 nlmsg_len，视为非法，返回 NULL。
 * - 对 RTM_NEWROUTE / RTM_DELROUTE 需先跳过 rtmsg 结构体再取属性；
 *   其他消息类型 offset 目前为 0（要支持需在此补分支）。
 * - 最后转调 netlink_parse_rtattr 填充 tb，并返回消息头指针。
 */
/*
 * Given a message header, locate and parse out the atttributes
 */
struct nlmsghdr *netlink_parse_buf(struct rtattr **tb, int max, void *buf,
				   size_t len)
{
	struct nlmsghdr *msg;
	struct rtattr *rta;
	uint8_t *ptr;
	uint32_t offset = 0;

	msg = buf;

	if (len < msg->nlmsg_len)
		return NULL;

	/* 计算“消息头之后、属性区之前”需要跳过的负载长度 offset。 */
	/* Figure out where the attrs start, then use the attribute
	 * parse api.
	 */
	if (msg->nlmsg_type == RTM_NEWROUTE || msg->nlmsg_type == RTM_DELROUTE)
		offset = sizeof(struct rtmsg);

	/* 提示：除上面两类消息外，其余消息类型的属性偏移需在此补充分支。 */
	/* Add clauses for the messages we want to use in this path... */

	if (offset > msg->nlmsg_len)
		return NULL;

	ptr = NLMSG_DATA(msg);
	ptr += offset;
	rta = (void *)ptr;

	netlink_parse_rtattr(tb, max, rta, msg->nlmsg_len - offset);

	return msg;
}

/* netlink_parse_rtattr_nested：解析嵌套 rtattr。
 * 把外层属性 rta 的数据区当作一行新的 rtattr 链，再交给
 * netlink_parse_rtattr 展开。
 */
/**
 * netlink_parse_rtattr_nested() - Parses a nested route attribute
 * @tb:         Pointer to array for storing rtattr in.
 * @max:        Max number to store.
 * @rta:        Pointer to rtattr to look for nested items in.
 */
void netlink_parse_rtattr_nested(struct rtattr **tb, int max, struct rtattr *rta)
{
	netlink_parse_rtattr(tb, max, RTA_DATA(rta), RTA_PAYLOAD(rta));
}

/* nl_addraw_l：把任意裸数据按对齐要求追加到消息尾部。
 * - 先检查“当前长度 + 数据长度”是否超过 maxlen，超了记日志并返回 false。
 * - 否则 memcpy 数据、把对齐填充的尾部补零，并更新 nlmsg_len。
 */
bool nl_addraw_l(struct nlmsghdr *n, unsigned int maxlen, const void *data,
		 unsigned int len)
{
	if (NLMSG_ALIGN(n->nlmsg_len) + NLMSG_ALIGN(len) > maxlen) {
		zlog_err("ERROR message exceeded bound of %d", maxlen);
		return false;
	}

	memcpy(NLMSG_TAIL(n), data, len);
	memset((uint8_t *)NLMSG_TAIL(n) + len, 0, NLMSG_ALIGN(len) - len);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + NLMSG_ALIGN(len);

	return true;
}

/* nl_attr_put：向消息追加一个 rtattr 属性（type + data[alen]）。
 * - 先算含头的 len = RTA_LENGTH(alen)，检查剩余空间，不足返回 false。
 * - 在 NLMSG_TAIL 处放置属性头，填 rta_type / rta_len，有数据则拷贝。
 * - data 为 NULL 时必须 alen == 0，否则断言失败。
 */
bool nl_attr_put(struct nlmsghdr *n, unsigned int maxlen, int type, const void *data,
		 unsigned int alen)
{
	int len;
	struct rtattr *rta;

	len = RTA_LENGTH(alen);

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen)
		return false;

	rta = (struct rtattr *)(((char *)n) + NLMSG_ALIGN(n->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = len;

	if (data)
		memcpy(RTA_DATA(rta), data, alen);
	else
		assert(alen == 0);

	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);

	return true;
}

/* nl_attr_put8：以 8 位标量数据追加属性（内部转调 nl_attr_put）。 */
bool nl_attr_put8(struct nlmsghdr *n, unsigned int maxlen, int type, uint8_t data)
{
	return nl_attr_put(n, maxlen, type, &data, sizeof(uint8_t));
}

/* nl_attr_put16：以 16 位标量数据追加属性（内部转调 nl_attr_put）。 */
bool nl_attr_put16(struct nlmsghdr *n, unsigned int maxlen, int type, uint16_t data)
{
	return nl_attr_put(n, maxlen, type, &data, sizeof(uint16_t));
}

/* nl_attr_put32：以 32 位标量数据追加属性（内部转调 nl_attr_put）。 */
bool nl_attr_put32(struct nlmsghdr *n, unsigned int maxlen, int type, uint32_t data)
{
	return nl_attr_put(n, maxlen, type, &data, sizeof(uint32_t));
}

/* nl_attr_put64：以 64 位标量数据追加属性（内部转调 nl_attr_put）。 */
bool nl_attr_put64(struct nlmsghdr *n, unsigned int maxlen, int type, uint64_t data)
{
	return nl_attr_put(n, maxlen, type, &data, sizeof(uint64_t));
}

/* nl_attr_nest：开始一个嵌套区。先在尾部落一个空的 rtattr 外壳
 * （type 置 NLA_F_NESTED 标志），成功返回外壳指针，失败返回 NULL。
 */
struct rtattr *nl_attr_nest(struct nlmsghdr *n, unsigned int maxlen, int type)
{
	struct rtattr *nest = NLMSG_TAIL(n);

	if (!nl_attr_put(n, maxlen, type, NULL, 0))
		return NULL;

	nest->rta_type |= NLA_F_NESTED;
	return nest;
}

/* nl_attr_nest_end：结束嵌套区，用“当前尾 - 嵌套起点”回填外壳长度，
 * 返回消息总长。
 */
int nl_attr_nest_end(struct nlmsghdr *n, struct rtattr *nest)
{
	nest->rta_len = (uint8_t *)NLMSG_TAIL(n) - (uint8_t *)nest;
	return n->nlmsg_len;
}

/* nl_attr_rtnh：在尾部追加一条 rtnexthop 记录并清零。
 * 空间不足返回 NULL；否则把 nlmsg_len 前移一个对齐后的 rtnexthop 大小。
 */
struct rtnexthop *nl_attr_rtnh(struct nlmsghdr *n, unsigned int maxlen)
{
	struct rtnexthop *rtnh = (struct rtnexthop *)NLMSG_TAIL(n);

	if (NLMSG_ALIGN(n->nlmsg_len) + RTNH_ALIGN(sizeof(struct rtnexthop)) > maxlen)
		return NULL;

	memset(rtnh, 0, sizeof(struct rtnexthop));
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(sizeof(struct rtnexthop));

	return rtnh;
}

/* nl_attr_rtnh_end：回填 rtnexthop 的长度（当前尾 - 记录起点），
 * 使随后追加的 rtattr 都归属于这条 nexthop。
 */
void nl_attr_rtnh_end(struct nlmsghdr *n, struct rtnexthop *rtnh)
{
	rtnh->rtnh_len = (uint8_t *)NLMSG_TAIL(n) - (uint8_t *)rtnh;
}

/* nl_rta_put：在已存在的 rtattr 内部追加子属性（手工构造嵌套）。
 * - 检查父属性剩余空间，不足则记日志并返回 false。
 * - 在父属性尾部放置子属性头并拷贝数据，最后前移父属性 rta_len。
 */
bool nl_rta_put(struct rtattr *rta, unsigned int maxlen, int type,
		const void *data, int alen)
{
	struct rtattr *subrta;
	int len = RTA_LENGTH(alen);

	if (RTA_ALIGN(rta->rta_len) + RTA_ALIGN(len) > maxlen) {
		zlog_err("ERROR max allowed bound %d exceeded for rtattr", maxlen);
		return false;
	}
	subrta = (struct rtattr *)(((char *)rta) + RTA_ALIGN(rta->rta_len));
	subrta->rta_type = type;
	subrta->rta_len = len;
	if (alen)
		memcpy(RTA_DATA(subrta), data, alen);
	rta->rta_len = NLMSG_ALIGN(rta->rta_len) + RTA_ALIGN(len);

	return true;
}

/* nl_rta_put16：以 16 位标量向 rtattr 内追加子属性。 */
bool nl_rta_put16(struct rtattr *rta, unsigned int maxlen, int type, uint16_t data)
{
	return nl_rta_put(rta, maxlen, type, &data, sizeof(uint16_t));
}

/* nl_rta_put64：以 64 位标量向 rtattr 内追加子属性。 */
bool nl_rta_put64(struct rtattr *rta, unsigned int maxlen, int type, uint64_t data)
{
	return nl_rta_put(rta, maxlen, type, &data, sizeof(uint64_t));
}

/* nl_rta_nest：在父 rtattr 内开始一个嵌套子属性（带 NLA_F_NESTED）。
 * 学习提示（疑似历史遗留）：这里用 if (nl_rta_put(...)) return NULL; 作
 * 判断，而 nl_rta_put 成功时返回 true、失败时返回 false；于是“成功反而
 * 返回 NULL、失败却继续往下走并解引用 nest”。这与 nl_attr_nest 的写法
 * （用 !nl_attr_put 判断）正好相反，阅读 / 使用时务必留意这一差异。
 */
struct rtattr *nl_rta_nest(struct rtattr *rta, unsigned int maxlen, int type)
{
	struct rtattr *nest = RTA_TAIL(rta);

	if (nl_rta_put(rta, maxlen, type, NULL, 0))
		return NULL;

	nest->rta_type |= NLA_F_NESTED;

	return nest;
}

/* nl_rta_nest_end：结束嵌套子属性并用“父尾 - 子起点”回填其长度，
 * 返回父 rtattr 的当前总长。
 */
int nl_rta_nest_end(struct rtattr *rta, struct rtattr *nest)
{
	nest->rta_len = (uint8_t *)RTA_TAIL(rta) - (uint8_t *)nest;

	return rta->rta_len;
}

/* NLA_OK：判断当前 nlattr 是否仍落在剩余长度 len 之内且自身长度合法。
 * NLA_NEXT：把 len 减去对齐后的 nla_len，并返回下一个 nlattr 指针。
 * 这两个宏是遍历通用 netlink 属性数组的标准写法。
 */
#define NLA_OK(nla, len)                                                       \
	((len) >= (int)sizeof(struct nlattr)                                   \
	 && (nla)->nla_len >= sizeof(struct nlattr)                            \
	 && (nla)->nla_len <= (len))
#define NLA_NEXT(nla, attrlen)                                                 \
	((attrlen) -= NLA_ALIGN((nla)->nla_len),                               \
	 (struct nlattr *)(((char *)(nla)) + NLA_ALIGN((nla)->nla_len)))

/* netlink_parse_nlattr：遍历 nlattr 链，把 nla_type <= max 的属性存进
 * tb[nla_type]。与本文件前面的 rtattr 版本对应，只是属性头类型不同。
 */
void netlink_parse_nlattr(struct nlattr **tb, int max, struct nlattr *nla,
			  int len)
{
	/* 逐条遍历：NLA_OK 保证当前 nlattr 完整落在剩余长度内。 */
	while (NLA_OK(nla, len)) {
		if (nla->nla_type <= max)
			tb[nla->nla_type] = nla;
		nla = NLA_NEXT(nla, len);
	}
}

/* 结束 HAVE_NETLINK 条件编译：未启用 netlink 时本文件为空。 */
#endif /* HAVE_NETLINK */
