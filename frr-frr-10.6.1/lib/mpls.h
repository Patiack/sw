// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPLS definitions
 * Copyright 2015 Cumulus Networks, Inc.
 */

/*
 * 文件导读（lib/mpls.h）
 *
 * 本文件定义 MPLS 标签相关的基础类型、常量与编解码工具。MPLS 通过固定
 * 20 位的标签在网络中转发报文，本文件把标签栈项（LSE）在内存中的整数
 * 表示与各字段（label / EXP / BOS / TTL）之间的位运算集中封装，供 zebra
 * 与各协议使用。
 *
 * 核心概念：
 *   - mpls_lse_t：一个 32 位的“标签栈项”，按 RFC 3032 编码 label(20)、
 *     EXP(3)、BOS(1)、TTL(8)；MPLS_LS_* 为各字段的掩码与移位量，
 *     MPLS_LABEL_* 为提取各字段的宏。
 *   - mpls_label_t：单独一个标签值（20 位有效），MPLS_INVALID_LABEL 作为
 *     “未设置”哨兵（因 0 是合法标签，需额外一位区分）。
 *   - struct mpls_label_stack：变长标签栈，num_labels 指定标签个数。
 *   - enum lsp_types_t：LSP 类型（静态 / LDP / BGP / SR / SR-TE / EVPN 等），
 *     影响标签的打印方式。
 *
 * 主要函数：vni2label / label2vni（VNI 与标签互转）、mpls_lse_encode /
 * mpls_lse_decode（LSE 与主机序互转）、label2str（标签转可读串，区分保留
 * 值）、mpls_str2label / mpls_label2str（标签栈与字符串互转）。
 *
 * 与其他模块的关系：定义被 zebra 的 MPLS 转发面、ldpd、bgpd 的标签路由
 * 及 SR 相关代码共享。
 *
 * 阅读顺序：标签常量 -> LSE 位域与提取宏 -> 类型定义 -> LSP 类型枚举 ->
 * 各内联工具函数 -> 两个非内联的字符串转换函数。
 */

#ifndef _QUAGGA_MPLS_H
#define _QUAGGA_MPLS_H

#include <zebra.h>
#include <vxlan.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MPLS_LABEL_MAX
#undef MPLS_LABEL_MAX
#endif

/* CLI 帮助文本：提示用户输入一个或多个标签，范围 16-1048575，以 '/' 分隔。 */
#define MPLS_LABEL_HELPSTR                                                     \
	"Specify label(s) for this route\nOne or more "                        \
	"labels in the range (16-1048575) separated by '/'\n"

/* 知名保留标签取值（见 RFC 3032 等）：如 IPv4 显式空标签 0、路由器告警 1、
 * IPv6 显式空标签 2、隐式空标签 3、ELI 7、GAL 13、OAM 告警 14、扩展 15；
 * MPLS_LABEL_NONE 为内部哨兵值表示“无标签”。 */
/* Well-known MPLS label values (RFC 3032 etc). */
#define MPLS_LABEL_IPV4_EXPLICIT_NULL  0       /* [RFC3032] */
#define MPLS_LABEL_ROUTER_ALERT        1       /* [RFC3032] */
#define MPLS_LABEL_IPV6_EXPLICIT_NULL  2       /* [RFC3032] */
#define MPLS_LABEL_IMPLICIT_NULL       3       /* [RFC3032] */
#define MPLS_LABEL_ELI                 7       /* [RFC6790] */
#define MPLS_LABEL_GAL                 13      /* [RFC5586] */
#define MPLS_LABEL_OAM_ALERT           14      /* [RFC3429] */
#define MPLS_LABEL_EXTENSION           15      /* [RFC7274] */
#define MPLS_LABEL_MAX                 1048575
#define MPLS_LABEL_VALUE_MASK          0x000FFFFF
#define MPLS_LABEL_NONE                0xFFFFFFFF /* for internal use only */

/* 标签取值范围：0-15 为保留段，16-1048575 为可用（非保留）段。 */
/* Minimum and maximum label values */
#define MPLS_LABEL_RESERVED_MIN            0
#define MPLS_LABEL_RESERVED_MAX            15
#define MPLS_LABEL_UNRESERVED_MIN          16
#define MPLS_LABEL_UNRESERVED_MAX          1048575
#define MPLS_LABEL_BASE_ANY                0

/* SRGB（分段路由全局块）默认标签范围。即便 SRGB 允许各路由器使用不同
 * 标签空间，若希望全网统一，也需一个公共范围；因 Cisco 的 SRGB 常从
 * 16000 起、Juniper 用于 OSPF 的 SRGB 到 16384 止，取 8000 为起点可同时
 * 兼容两家。下方四个宏给出默认最小 / 最大标签及默认块大小范围。 */
/* Default min and max SRGB label range */
/* Even if the SRGB allows to manage different Label space between routers,
 * if an operator want to use the same SRGB for all its router, we must fix
 * a common range. However, Cisco start its SRGB at 16000 and Juniper ends
 * its SRGB at 16384 for OSPF. Thus, by fixing the minimum SRGB label to
 * 8000 we could deal with both Cisco and Juniper.
 */
#define MPLS_DEFAULT_MIN_SRGB_LABEL        8000
#define MPLS_DEFAULT_MAX_SRGB_LABEL        50000
#define MPLS_DEFAULT_MIN_SRGB_SIZE         5000
#define MPLS_DEFAULT_MAX_SRGB_SIZE         20000

/* Maximum # labels that can be pushed. */
/* 单条路由最多可压入的标签个数。 */
#define MPLS_MAX_LABELS                    16

/* 判定标签是否落在保留段（0-15）。 */
#define IS_MPLS_RESERVED_LABEL(label) (label <= MPLS_LABEL_RESERVED_MAX)

/* 判定标签是否落在可用（非保留）段（16-1048575）。 */
#define IS_MPLS_UNRESERVED_LABEL(label)                                        \
	(label >= MPLS_LABEL_UNRESERVED_MIN                                    \
	 && label <= MPLS_LABEL_UNRESERVED_MAX)

/* Definitions for a MPLS label stack entry (RFC 3032). This encodes the
 * label, EXP, BOS and TTL fields.
 */
/* 一个 MPLS 标签栈项（LSE），32 位整数，编码 label / EXP / BOS / TTL。 */
typedef unsigned int mpls_lse_t;

/* LSE 内各字段的掩码与移位量：label 占高 20 位、EXP 3 位、S(BOS) 1 位、
 * TTL 8 位。 */
#define MPLS_LS_LABEL_MASK             0xFFFFF000
#define MPLS_LS_LABEL_SHIFT            12
#define MPLS_LS_EXP_MASK               0x00000E00
#define MPLS_LS_EXP_SHIFT              9
#define MPLS_LS_S_MASK                 0x00000100
#define MPLS_LS_S_SHIFT                8
#define MPLS_LS_TTL_MASK               0x000000FF
#define MPLS_LS_TTL_SHIFT              0

/* 从 LSE 中提取各字段：标签值、EXP、BOS 位、TTL。 */
#define MPLS_LABEL_VALUE(lse)                                                  \
	((lse & MPLS_LS_LABEL_MASK) >> MPLS_LS_LABEL_SHIFT)
#define MPLS_LABEL_EXP(lse) ((lse & MPLS_LS_EXP_MASK) >> MPLS_LS_EXP_SHIFT)
#define MPLS_LABEL_BOS(lse) ((lse & MPLS_LS_S_MASK) >> MPLS_LS_S_SHIFT)
#define MPLS_LABEL_TTL(lse) ((lse & MPLS_LS_TTL_MASK) >> MPLS_LS_TTL_SHIFT)

/* 判定该 LSE 是否为栈底（BOS 位为 1）。 */
#define IS_MPLS_LABEL_BOS(ls)          (MPLS_LABEL_BOS(ls) == 1)

#define MPLS_LABEL_LEN_BITS            20

/* MPLS label value as a 32-bit (mostly we only care about the label value). */
/* 单独一个 MPLS 标签值（20 位有效）的 32 位表示。 */
typedef unsigned int mpls_label_t;

/* 变长 MPLS 标签栈：num_labels 给出有效标签个数，随后紧随一个或多个
 * 标签值（尾部 label[0] 为柔性数组写法）。 */
struct mpls_label_stack {
	uint8_t num_labels;
	uint8_t reserved[3];
	mpls_label_t label[0]; /* 1 or more labels */
};

/* The MPLS explicit-null label is 0 which means when you memset a mpls_label_t
 * to zero you have set that variable to explicit-null which was probably not
 * your intent. The work-around is to use one bit to indicate if the
 * mpls_label_t has been set by the user. MPLS_INVALID_LABEL has this bit clear
 * so that we can use MPLS_INVALID_LABEL to initialize mpls_label_t variables.
 */
/* 无效标签哨兵值。MPLS 显式空标签为 0，若把 mpls_label_t 清零会被误当作
 * 显式空标签；故用一个单独的“已设置”位区分。MPLS_INVALID_LABEL 该位为 0，
 * 可用它初始化 mpls_label_t 变量。 */
#define MPLS_INVALID_LABEL                 0xFFFDFFFF

/* LSP 类型：标识该标签由何种协议 / 机制建立，影响打印与处理方式。 */
/* LSP types. */
enum lsp_types_t {
	ZEBRA_LSP_NONE = 0,   /* No LSP. */
	ZEBRA_LSP_STATIC = 1, /* Static LSP. */
	ZEBRA_LSP_LDP = 2,    /* LDP LSP. */
	ZEBRA_LSP_BGP = 3,    /* BGP LSP. */
	ZEBRA_LSP_OSPF_SR = 4,/* OSPF Segment Routing LSP. */
	ZEBRA_LSP_ISIS_SR = 5,/* IS-IS Segment Routing LSP. */
	ZEBRA_LSP_SHARP = 6,  /* Identifier for test protocol */
	ZEBRA_LSP_SRTE = 7,   /* SR-TE LSP */
	ZEBRA_LSP_EVPN = 8,  /* EVPN VNI Label */
};

/* Functions for basic label operations. */

/* 把 VNI（VXLAN 网络标识）的低 24 位写入标签的低 3 字节。 */
static inline void vni2label(vni_t vni, mpls_label_t *label)
{
	uint8_t *tag = (uint8_t *)label;

	assert(tag);

	tag[0] = (vni >> 16) & 0xFF;
	tag[1] = (vni >> 8) & 0xFF;
	tag[2] = vni & 0xFF;
}

/* 从标签的低 3 字节取回 VNI，是 vni2label 的逆操作。 */
static inline vni_t label2vni(const mpls_label_t *label)
{
	uint8_t *tag = (uint8_t *)label;
	vni_t vni;

	assert(tag);

	vni = ((uint32_t)*tag++ << 16);
	vni |= (uint32_t)*tag++ << 8;
	vni |= (uint32_t)(*tag & 0xFF);

	return vni;
}

/* 由各字段组装出一个 LSE，并转成网络字节序（Netlink 接口要求此格式）。 */
/* Encode a label stack entry from fields; convert to network byte-order as
 * the Netlink interface expects MPLS labels to be in this format.
 */
static inline mpls_lse_t mpls_lse_encode(mpls_label_t label, uint32_t ttl,
					 uint32_t exp, uint32_t bos)
{
	mpls_lse_t lse;
	lse = htonl((label << MPLS_LS_LABEL_SHIFT) | (exp << MPLS_LS_EXP_SHIFT)
		    | (bos ? (1 << MPLS_LS_S_SHIFT) : 0)
		    | (ttl << MPLS_LS_TTL_SHIFT));
	return lse;
}

/* 从 LSE 中解析出各字段，先转主机字节序；通常只对 Netlink 收上来的消息
 * 调用。 */
/* Extract the fields from a label stack entry after converting to host-byte
 * order. This is expected to be called only for messages received over the
 * Netlink interface.
 */
static inline void mpls_lse_decode(mpls_lse_t lse, mpls_label_t *label,
				   uint32_t *ttl, uint32_t *exp, uint32_t *bos)
{
	mpls_lse_t local_lse;

	local_lse = ntohl(lse);
	*label = MPLS_LABEL_VALUE(local_lse);
	*exp = MPLS_LABEL_EXP(local_lse);
	*bos = MPLS_LABEL_BOS(local_lse);
	*ttl = MPLS_LABEL_TTL(local_lse);
}

/* Invalid label index value (when used with BGP Prefix-SID). Should
 * match the BGP definition.
 */
/* 无效标签索引哨兵（用于 BGP Prefix-SID），应与 BGP 侧定义一致。 */
#define MPLS_INVALID_LABEL_INDEX   0xFFFFFFFF

/* 把标签转成可读字符串，特殊处理各保留值；对 EVPN 类型则按 VNI 打印。 */
/* Printable string for labels (with consideration for reserved values). */
static inline char *label2str(mpls_label_t label, enum lsp_types_t type,
			      char *buf, size_t len)
{
	if (type == ZEBRA_LSP_EVPN) {
		snprintf(buf, len, "%u", label2vni(&label));
		return (buf);
	}

	switch (label) {
	case MPLS_LABEL_IPV4_EXPLICIT_NULL:
		strlcpy(buf, "IPv4 Explicit Null", len);
		return (buf);
	case MPLS_LABEL_ROUTER_ALERT:
		strlcpy(buf, "Router Alert", len);
		return (buf);
	case MPLS_LABEL_IPV6_EXPLICIT_NULL:
		strlcpy(buf, "IPv6 Explicit Null", len);
		return (buf);
	case MPLS_LABEL_IMPLICIT_NULL:
		strlcpy(buf, "implicit-null", len);
		return (buf);
	case MPLS_LABEL_ELI:
		strlcpy(buf, "Entropy Label Indicator", len);
		return (buf);
	case MPLS_LABEL_GAL:
		strlcpy(buf, "Generic Associated Channel", len);
		return (buf);
	case MPLS_LABEL_OAM_ALERT:
		strlcpy(buf, "OAM Alert", len);
		return (buf);
	case MPLS_LABEL_EXTENSION:
		strlcpy(buf, "Extension", len);
		return (buf);
	default:
		if (label < 16)
			snprintf(buf, len, "Reserved (%u)", label);
		else
			snprintf(buf, len, "%u", label);
		return buf;
	}
}

/*
 * String to label conversion, labels separated by '/'.
 */
/* 字符串转标签栈：按 '/' 分隔解析出多个标签，写回 num_labels 与 labels。 */
int mpls_str2label(const char *label_str, uint8_t *num_labels,
		   mpls_label_t *labels);

/* Generic string buffer for label-stack-to-str */
/* 标签栈转字符串时所用的通用缓冲区长度。 */
#define MPLS_LABEL_STRLEN 1024

/*
 * Label to string conversion, labels in string separated by '/'.
 */
/* 标签栈转字符串：多个标签以 '/' 分隔，pretty 控制输出风格（如与保留值
 * 名称）。返回 buf。 */
char *mpls_label2str(uint8_t num_labels, const mpls_label_t *labels, char *buf,
		     int len, enum lsp_types_t type, int pretty);

#ifdef __cplusplus
}
#endif

#endif
