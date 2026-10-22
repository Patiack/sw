// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRv6 definitions
 * Copyright (C) 2020  Hiroki Shirokura, LINE Corporation
 */

/*
 * ============================================================================
 * 文件导读：lib/srv6.h —— SRv6（Segment Routing over IPv6）公共头文件
 * ============================================================================
 * 本文件为整个 FRR 提供 SRv6 相关的公共类型定义、常量、内联工具函数与
 * 对外接口声明，被 zebra、bgpd、isisd、ospfd 等守护进程共同引用。
 *
 * 核心内容：
 *   1) 基本常量：SID 数量上限、SRH 头长度、默认 SRv6 接口名等；
 *   2) 行为枚举：Headend 行为（H.Insert / H.Encaps 等）、End 端点行为
 *      （End / End.X / End.DT6 等）以及 flavor（PSP / USP / USD / NEXT-CSID）；
 *   3) 数据结构：locator（SID 前缀空间）、locator chunk（分配出的片段）、
 *      SID format（未压缩 与 uSID 压缩两种格式）、SID context 等；
 *   4) 工具宏 与 内联函数：flavor 位操作、SID 比较与拷贝、行为码点换算、
 *      各枚举转可读字符串。
 *
 * 与其他模块的关系：
 *   - srv6.c 负责实现本文件声明的非内联函数；
 *   - prefix.h 提供 prefix_ipv6 前缀结构，vrf.h 提供 VRF 与 ifindex 辅助；
 *   - zebra 通过 ZAPI（zclient）在各守护进程间同步 locator 与 SID 信息。
 *
 * 建议阅读顺序：
 *   1) 顶部常量 与 sid2str 宏；
 *   2) 各行为枚举（headend / seg6local / flavor）；
 *   3) locator、sid_format、sid_ctx 结构体；
 *   4) 末尾的内联工具函数 与 函数声明。
 */
#ifndef _FRR_SRV6_H
#define _FRR_SRV6_H

#include <zebra.h>
#include "prefix.h"
#include "vrf.h"

#include <arpa/inet.h>
#include <netinet/in.h>

/*
 * 基础容量常量：
 *   SRV6_MAX_SIDS  单个 SID 列表可容纳的最大 SID 数；
 *   SRV6_MAX_SEGS  SRH 段列表中最大段数；
 *   SRV6_LOCNAME_SIZE  locator 名字缓冲区长度；
 *   SRH_BASE_HEADER_LENGTH / SRH_SEGMENT_LENGTH  SRH 头 与 单段的基础字节数，
 *   用于编解码时计算报文长度。
 */
#define SRV6_MAX_SIDS	  16
#define SRV6_MAX_SEGS	  8
#define SRV6_LOCNAME_SIZE 256
#define SRH_BASE_HEADER_LENGTH 8
#define SRH_SEGMENT_LENGTH     16

#define SRV6_SID_FORMAT_NAME_SIZE 512

#define DEFAULT_SRV6_IFNAME "sr0"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * sid2str：把 in6_addr 形式的 SID 转成点分十六进制字符串。
 * 参数：sid 指向 in6_addr；str 输出缓冲区；size 缓冲区长度。
 * 返回 inet_ntop 的返回值（成功为非 NULL，失败为 NULL）。
 * 注意：调用者需保证 str / size 足够，本宏不做长度校验。
 */
#define sid2str(sid, str, size) \
	inet_ntop(AF_INET6, sid, str, size)

/* SRv6 flavors manipulation macros */
#define CHECK_SRV6_FLV_OP(OPS,OP)      ((OPS) & (1 << OP))
#define SET_SRV6_FLV_OP(OPS,OP)        (OPS) |= (1 << OP)
#define UNSET_SRV6_FLV_OP(OPS,OP)      (OPS) &= ~(1 << OP)
#define RESET_SRV6_FLV_OP(OPS)         (OPS) = 0

/* SRv6 Flavors default values */
#define ZEBRA_DEFAULT_SEG6_LOCAL_FLV_LCBLOCK_LEN 32
#define ZEBRA_DEFAULT_SEG6_LOCAL_FLV_LCNODE_FN_LEN 16

/* SR Policy Headend Behaviors as per RFC 8986 section #5 */
enum srv6_headend_behavior {
	SRV6_HEADEND_BEHAVIOR_H_INSERT,
	SRV6_HEADEND_BEHAVIOR_H_ENCAPS,
	SRV6_HEADEND_BEHAVIOR_H_ENCAPS_RED,
	SRV6_HEADEND_BEHAVIOR_H_ENCAPS_L2,
	SRV6_HEADEND_BEHAVIOR_H_ENCAPS_L2_RED,
};

/*
 * seg6_mode_t：SRv6 报文的封装模式。
 *   INLINE  直接在原报文基础上插入 SRH（不另加外层 IPv6 头）；
 *   ENCAP   用外层 IPv6 头 + SRH 封装原报文；
 *   L2ENCAP 面向二层报文的封装（对应 H.Encaps.L2）。
 */
enum seg6_mode_t {
	INLINE,
	ENCAP,
	L2ENCAP,
};

/*
 * seg6local_action_t：本地 SID（locally instantiated SID）的 Endpoint 行为动作。
 * 取值遵循 Linux 内核 seg6local 的 ZEBRA_SEG6_LOCAL_ACTION_* 定义，例如
 * END（结束）、END_X（跨链路）、END_DX6/DX4（解封装后按 IPv6 / IPv4 转发）、
 * END_DT6/DT4/DT46（解封装后查指定表的 IPv6 / IPv4 / 双栈路由）。
 * 该枚举贯穿 zebra 与 bgpd / isisd 的 SID 通告与编程路径。
 */
enum seg6local_action_t {
	ZEBRA_SEG6_LOCAL_ACTION_UNSPEC       = 0,
	ZEBRA_SEG6_LOCAL_ACTION_END          = 1,
	ZEBRA_SEG6_LOCAL_ACTION_END_X        = 2,
	ZEBRA_SEG6_LOCAL_ACTION_END_T        = 3,
	ZEBRA_SEG6_LOCAL_ACTION_END_DX2      = 4,
	ZEBRA_SEG6_LOCAL_ACTION_END_DX6      = 5,
	ZEBRA_SEG6_LOCAL_ACTION_END_DX4      = 6,
	ZEBRA_SEG6_LOCAL_ACTION_END_DT6      = 7,
	ZEBRA_SEG6_LOCAL_ACTION_END_DT4      = 8,
	ZEBRA_SEG6_LOCAL_ACTION_END_B6       = 9,
	ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP = 10,
	ZEBRA_SEG6_LOCAL_ACTION_END_BM       = 11,
	ZEBRA_SEG6_LOCAL_ACTION_END_S        = 12,
	ZEBRA_SEG6_LOCAL_ACTION_END_AS       = 13,
	ZEBRA_SEG6_LOCAL_ACTION_END_AM       = 14,
	ZEBRA_SEG6_LOCAL_ACTION_END_BPF      = 15,
	ZEBRA_SEG6_LOCAL_ACTION_END_DT46     = 16,
};

/* Flavor operations for SRv6 End* Behaviors */
enum seg6local_flavor_op {
	ZEBRA_SEG6_LOCAL_FLV_OP_UNSPEC       = 0,
	/* PSP Flavor as per RFC 8986 section #4.16.1 */
	ZEBRA_SEG6_LOCAL_FLV_OP_PSP          = 1,
	/* USP Flavor as per RFC 8986 section #4.16.2 */
	ZEBRA_SEG6_LOCAL_FLV_OP_USP          = 2,
	/* USD Flavor as per RFC 8986 section #4.16.3 */
	ZEBRA_SEG6_LOCAL_FLV_OP_USD          = 3,
	/* NEXT-C-SID Flavor as per draft-ietf-spring-srv6-srh-compression-03
	   section 4.1 */
	ZEBRA_SEG6_LOCAL_FLV_OP_NEXT_CSID    = 4,
};

/*
 * SRV6_SEG_STRLEN：将 SID 列表格式化为字符串时的建议缓冲区长度。
 */
#define SRV6_SEG_STRLEN 1024

/*
 * struct seg6_segs：定长数组形式的 SID 序列（最多 256 个 IPv6 地址）。
 * num_segs 记录实际有效个数，segs 为地址数组，常用于 SNMP / 展示路径。
 */
struct seg6_segs {
	size_t num_segs;
	struct in6_addr segs[256];
};

/* flavors psp, usd, usp, next-csid */
#define SRV6_FLAVORS_STRLEN 50

/*
 * struct seg6local_flavors_info：某个 End* 行为附带的 flavor 集合与长度信息。
 * flv_ops 是按位或的 ZEBRA_SEG6_LOCAL_FLV_OP_*；
 * lcblock_len / lcnode_func_len 记录 Locator-Block 与 Locator-Node Function
 * 的位长（uSID 压缩场景下使用）。
 */
struct seg6local_flavors_info {
	/* Flavor operations */
	uint32_t flv_ops;

	/* Locator-Block length, expressed in bits */
	uint8_t lcblock_len;
	/* Locator-Node Function length, expressed in bits */
	uint8_t lcnode_func_len;
};

/*
 * struct seg6_seg_stack：SRv6 Headend 行为的段栈（segment list）。
 * encap_behavior 为封装行为（H.Insert / H.Encaps 等）；
 * num_segs 为段数；seg 为柔性数组，实际存放 1 个或多个 in6_addr。
 * 注意：这是变长结构，分配时需按 num_segs 追加空间。
 */
struct seg6_seg_stack {
	enum srv6_headend_behavior encap_behavior;
	uint8_t num_segs;
	struct in6_addr seg[0]; /* 1 or more segs */
};

/*
 * struct seg6local_context：执行某个 End* 行为所需的上下文参数。
 * 依据不同 action 使用其中不同字段：nh4/nh6 为下一跳地址，table 为查表号，
 * ifindex 为出接口，flv 为 flavor 信息，block/node/function/argument_len 描述
 * SID 结构划分长度。
 */
struct seg6local_context {
	struct in_addr nh4;
	struct in6_addr nh6;
	uint32_t table;
	ifindex_t ifindex;
	struct seg6local_flavors_info flv;
	uint8_t block_len;
	uint8_t node_len;
	uint8_t function_len;
	uint8_t argument_len;
};

/*
 * struct srv6_locator：一个 SRv6 locator（SID 前缀空间）的完整描述。
 * prefix 为该 locator 的 IPv6 前缀；block/node/function/argument_bits_length
 * 描述 SID 各段的位长；algonum 为关联的 IGP 算法号；current 记录已分配到的
 * SID 位置（用于顺序分配）；status_up 表示在内核中是否已生效；
 * chunks 保存从本 locator 切分出的 chunk 列表；
 * flags 见下方 SRV6_LOCATOR_* 宏；sid_format 指向所用 SID 格式；
 * sid_block 指向父级 SID block。带 QOBJ_FIELDS 以支持对象登记。
 */
struct srv6_locator {
	char name[SRV6_LOCNAME_SIZE];
	struct prefix_ipv6 prefix;

	/*
	 * Bit length of SRv6 locator described in
	 * draft-ietf-bess-srv6-services-05#section-3.2.1
	 */
	uint8_t block_bits_length;
	uint8_t node_bits_length;
	uint8_t function_bits_length;
	uint8_t argument_bits_length;

	int algonum;
	uint64_t current;
	bool status_up;
	struct list *chunks;

	uint8_t flags;
#define SRV6_LOCATOR_USID (1 << 0) /* The SRv6 Locator is a uSID Locator */
#define SRV6_LOCATOR_PSP  (1 << 1) /* The SRv6 Locator has the PSP flavor */
#define SRV6_LOCATOR_F3216 (1 << 2) /* The SRv6 Locator has F3216 format */
#define SRV6_LOCATOR_F4816 (1 << 3) /* The SRv6 Locator has F4816 format */

	/* Pointer to the SID format. */
	struct srv6_sid_format *sid_format;

	/* Pointer to the parent SID block of the locator. */
	void *sid_block;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(srv6_locator);

/*
 * 声明 srv6_locator 为可登记对象类型（QOBJ），配合 DEFINE_QOBJ_TYPE 使用，
 * 便于调试 / 泄漏排查时按类型统计对象。
 */
/*
 * struct srv6_locator_chunk：从 locator 中切分并分配给某个守护进程 / 实例的
 * SID 片段。locator_name + prefix 标识它；四位 bits_length 描述 SID 结构；
 * keep / proto / instance / session_id 供 Zclient 通信时携带归属信息，
 * flags 复用 SRV6_LOCATOR_* 位。
 */
struct srv6_locator_chunk {
	char locator_name[SRV6_LOCNAME_SIZE];
	struct prefix_ipv6 prefix;

	/*
	 * Bit length of SRv6 locator described in
	 * draft-ietf-bess-srv6-services-05#section-3.2.1
	 */
	uint8_t block_bits_length;
	uint8_t node_bits_length;
	uint8_t function_bits_length;
	uint8_t argument_bits_length;

	/*
	 * For Zclient communication values
	 */
	uint8_t keep;
	uint8_t proto;
	uint16_t instance;
	uint32_t session_id;

	uint8_t flags;
};

/*
 * SRv6 Endpoint Behavior codepoints, as defined by IANA in
 * https://www.iana.org/assignments/segment-routing/segment-routing.xhtml
 */
/*
 * enum srv6_endpoint_behavior_codepoint：IANA 定义的 SRv6 Endpoint Behavior
 * 码点（见上方链接）。这些码点随 BGP 通告（如 IPv6 前缀 SID / L3VPN）传递，
 * 用于让接收端知晓该 SID 应执行哪种本地行为；含 uSID（uN / uA / uDT*）等变体。
 */
enum srv6_endpoint_behavior_codepoint {
	SRV6_ENDPOINT_BEHAVIOR_RESERVED         = 0x0000,
	SRV6_ENDPOINT_BEHAVIOR_END              = 0x0001,
	SRV6_ENDPOINT_BEHAVIOR_END_PSP          = 0x0002,
	SRV6_ENDPOINT_BEHAVIOR_END_X            = 0x0005,
	SRV6_ENDPOINT_BEHAVIOR_END_X_PSP        = 0x0006,
	SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS    = 0x000E,
	SRV6_ENDPOINT_BEHAVIOR_END_DT6          = 0x0012,
	SRV6_ENDPOINT_BEHAVIOR_END_DT4          = 0x0013,
	SRV6_ENDPOINT_BEHAVIOR_END_DT46         = 0x0014,
	SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS_RED = 0x001B,
	SRV6_ENDPOINT_BEHAVIOR_END_PSP_USD      = 0x001D,
	SRV6_ENDPOINT_BEHAVIOR_END_X_PSP_USD    = 0x0021,
	SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID    = 0x002B,
	SRV6_ENDPOINT_BEHAVIOR_END_X_NEXT_CSID  = 0x0034,
	SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID_PSP   = 0x002C,
	SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID_PSP_USD   = 0x0030,
	SRV6_ENDPOINT_BEHAVIOR_END_X_NEXT_CSID_PSP = 0x0035,
	SRV6_ENDPOINT_BEHAVIOR_END_X_NEXT_CSID_PSP_USD = 0x0039,
	SRV6_ENDPOINT_BEHAVIOR_END_DT6_USID     = 0x003E,
	SRV6_ENDPOINT_BEHAVIOR_END_DT4_USID     = 0x003F,
	SRV6_ENDPOINT_BEHAVIOR_END_DT46_USID    = 0x0040,
	SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS_NEXT_CSID = 0x005D,
	SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS_RED_NEXT_CSID = 0x005E,
	SRV6_ENDPOINT_BEHAVIOR_OPAQUE           = 0xFFFF,
};

/*
 * srv6_behavior_codepoint_get：根据基础码点与 locator flags 换算实际码点。
 * 参数：_codepoint 基础 Endpoint 行为码点；_flags locator 的 SRV6_LOCATOR_* 标志。
 * 返回：换算后的码点。当基础为 END 且带 uSID / PSP 标志时，返回对应的
 * uN / uN PSP 码点；其余情况原样返回 _codepoint。
 * 用途：让同一份配置在不同 flavor 组合下得到正确的通告码点。
 */
macro_inline uint16_t srv6_behavior_codepoint_get(enum srv6_endpoint_behavior_codepoint _codepoint,
						  uint8_t _flags)
{
	if (_codepoint == SRV6_ENDPOINT_BEHAVIOR_END) {
		if (CHECK_FLAG(_flags, SRV6_LOCATOR_USID)) {
			if (CHECK_FLAG(_flags, SRV6_LOCATOR_PSP))
				return SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID_PSP;
			else
				return SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID;
		}
		if (CHECK_FLAG(_flags, SRV6_LOCATOR_PSP))
			return SRV6_ENDPOINT_BEHAVIOR_END_PSP;
		return SRV6_ENDPOINT_BEHAVIOR_END;
	}
	return _codepoint;
}

/*
 * SRV6_BEHAVIOR_CODEPOINT：srv6_behavior_codepoint_get 的宏封装，便于调用点书写。
 */
#define SRV6_BEHAVIOR_CODEPOINT(_codepoint, _flags)                                                \
	srv6_behavior_codepoint_get((_codepoint), (_flags))

/*
 * Return true if next-csid behavior is used, false otherwise
 */
static inline bool seg6local_has_next_csid(const struct seg6local_context *ctx)
{
	const struct seg6local_flavors_info *flv_info = &ctx->flv;

	return CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_NEXT_CSID);
}

/*
 * Convert SRv6 endpoint behavior codepoints to human-friendly string.
 */
static inline const char *
srv6_endpoint_behavior_codepoint2str(enum srv6_endpoint_behavior_codepoint behavior)
{
	switch (behavior) {
	case SRV6_ENDPOINT_BEHAVIOR_RESERVED:
		return "Reserved";
	case SRV6_ENDPOINT_BEHAVIOR_END:
		return "End";
	case SRV6_ENDPOINT_BEHAVIOR_END_PSP:
		return "End PSP";
	case SRV6_ENDPOINT_BEHAVIOR_END_PSP_USD:
		return "End PSP/USD";
	case SRV6_ENDPOINT_BEHAVIOR_END_X:
		return "End.X";
	case SRV6_ENDPOINT_BEHAVIOR_END_X_PSP:
		return "End.X PSP";
	case SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS:
		return "End.B6.Encaps";
	case SRV6_ENDPOINT_BEHAVIOR_END_X_PSP_USD:
		return "End.X PSP/USD";
	case SRV6_ENDPOINT_BEHAVIOR_END_DT6:
		return "End.DT6";
	case SRV6_ENDPOINT_BEHAVIOR_END_DT4:
		return "End.DT4";
	case SRV6_ENDPOINT_BEHAVIOR_END_DT46:
		return "End.DT46";
	case SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS_RED:
		return "End.B6.Encaps.Red";
	case SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID:
		return "uN";
	case SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID_PSP:
		return "uN PSP";
	case SRV6_ENDPOINT_BEHAVIOR_END_NEXT_CSID_PSP_USD:
		return "uN PSP/USD";
	case SRV6_ENDPOINT_BEHAVIOR_END_X_NEXT_CSID:
		return "uA";
	case SRV6_ENDPOINT_BEHAVIOR_END_X_NEXT_CSID_PSP:
		return "uA PSP";
	case SRV6_ENDPOINT_BEHAVIOR_END_X_NEXT_CSID_PSP_USD:
		return "uA PSP/USD";
	case SRV6_ENDPOINT_BEHAVIOR_END_DT6_USID:
		return "uDT6";
	case SRV6_ENDPOINT_BEHAVIOR_END_DT4_USID:
		return "uDT4";
	case SRV6_ENDPOINT_BEHAVIOR_END_DT46_USID:
		return "uDT46";
	case SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS_NEXT_CSID:
		return "uB6.Encaps";
	case SRV6_ENDPOINT_BEHAVIOR_END_B6_ENCAPS_RED_NEXT_CSID:
		return "uB6.Encaps.Red";
	case SRV6_ENDPOINT_BEHAVIOR_OPAQUE:
		return "Opaque";
	}

	return "Unspec";
}

/*
 * struct nexthop_srv6：挂在 nexthop 上的 SRv6 信息，区分两种角色：
 *   - seg6local_action / seg6local_ctx 描述本地 SID（Endpoint 行为）；
 *   - seg6_segs 描述 Headend 行为所需的段栈。
 * 同一 nexthop 通常只使用其中一类。
 */
struct nexthop_srv6 {
	/* SRv6 localsid info for Endpoint-behaviour */
	enum seg6local_action_t seg6local_action;
	struct seg6local_context seg6local_ctx;

	/* SRv6 Headend-behaviour */
	struct seg6_seg_stack *seg6_segs;
};

/* SID format type */
/*
 * enum srv6_sid_format_type：SID 格式类型。
 *   UNCOMPRESSED  未压缩的经典 SRv6 SID；
 *   USID          压缩的 Micro-segment（uSID）SID。
 */
enum srv6_sid_format_type {
	SRV6_SID_FORMAT_TYPE_UNSPEC = 0,
	/* SRv6 SID uncompressed format */
	SRV6_SID_FORMAT_TYPE_UNCOMPRESSED = 1,
	/* SRv6 SID compressed uSID format */
	SRV6_SID_FORMAT_TYPE_USID = 2,
};

/* SRv6 SID format */
/*
 * struct srv6_sid_format：一种 SID 分配格式的定义。
 * name 为格式名；type 区分未压缩 / uSID；block/node/function/argument_len
 * 为对应部分的位长；config 联合体按类型保存分配区间：
 *   usid.lib_start 起始 Local ID Block，elib / wlib / ewlib 各类区间；
 *   uncompressed.explicit_start 显式区间起点。
 * 带 QOBJ_FIELDS 以支持对象登记。
 */
struct srv6_sid_format {
	/* Name of the format */
	char name[SRV6_SID_FORMAT_NAME_SIZE];

	/* Format type: uncompressed vs compressed */
	enum srv6_sid_format_type type;

	/*
	 * Lengths of block/node/function/argument parts of the SIDs allocated
	 * using this format
	 */
	uint8_t block_len;
	uint8_t node_len;
	uint8_t function_len;
	uint8_t argument_len;

	union {
		/* Configuration settings for compressed uSID format type */
		struct {
			/* Start of the Local ID Block (LIB) range */
			uint32_t lib_start;

			/* Start/End of the Explicit LIB range */
			uint32_t elib_start;
			uint32_t elib_end;

			/* Start/End of the Wide LIB range */
			uint32_t wlib_start;
			uint32_t wlib_end;

			/* Start/End of the Explicit Wide LIB range */
			uint32_t ewlib_start;
		} usid;

		/* Configuration settings for uncompressed format type */
		struct {
			/* Start of the Explicit range */
			uint32_t explicit_start;
		} uncompressed;
	} config;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(srv6_sid_format);

/* Context for an SRv6 SID */
/*
 * struct srv6_sid_ctx：描述一个 SRv6 SID 的上下文（用于对象管理 / 编解码）。
 * behavior 为本地行为；nh4/nh6 为下一跳；vrf_id 与 ifindex 为所属 VRF 与接口。
 */
struct srv6_sid_ctx {
	/* Behavior associated with the SID */
	enum seg6local_action_t behavior;

	/* Behavior-specific attributes */
	struct in_addr nh4;
	struct in6_addr nh6;
	vrf_id_t vrf_id;
	ifindex_t ifindex;
};

/*
 * srv6_headend_behavior2str：Headend 行为枚举转字符串。
 * 参数：behavior 行为枚举；running_conf 为 true 时输出下划线形式（用于配置回显，
 * 如 H_Insert），否则输出带点形式（如 H.Insert）。
 * 返回静态字符串，未知取值返回 "unknown"。
 */
static inline const char *srv6_headend_behavior2str(enum srv6_headend_behavior behavior,
						    bool running_conf)
{
	switch (behavior) {
	case SRV6_HEADEND_BEHAVIOR_H_INSERT:
		return running_conf ? "H_Insert" : "H.Insert";
	case SRV6_HEADEND_BEHAVIOR_H_ENCAPS:
		return running_conf ? "H_Encaps" : "H.Encaps";
	case SRV6_HEADEND_BEHAVIOR_H_ENCAPS_RED:
		return running_conf ? "H_Encaps_Red" : "H.Encaps.Red";
	case SRV6_HEADEND_BEHAVIOR_H_ENCAPS_L2:
		return running_conf ? "H_Encaps_L2" : "H.Encaps.L2";
	case SRV6_HEADEND_BEHAVIOR_H_ENCAPS_L2_RED:
		return running_conf ? "H_Encaps_L2_Red" : "H.Encaps.L2.Red";
	}

	return "unknown";
}

/*
 * seg6_mode2str：封装模式枚举转字符串（INLINE / ENCAP / L2ENCAP）。
 * 返回静态字符串，未知取值返回 "unknown"。
 */
static inline const char *seg6_mode2str(enum seg6_mode_t mode)
{
	switch (mode) {
	case INLINE:
		return "INLINE";
	case ENCAP:
		return "ENCAP";
	case L2ENCAP:
		return "L2ENCAP";
	default:
		return "unknown";
	}
}

/*
 * sid_same：判断两个 SID（in6_addr）是否相同。
 * 参数：a / b 可为 NULL。二者均 NULL 视为相同；仅一个为 NULL 视为不同；
 * 否则按字节 memcmp。
 * 返回 true 表示相同。
 */
static inline bool sid_same(
		const struct in6_addr *a,
		const struct in6_addr *b)
{
	if (!a && !b)
		return true;
	else if (!(a && b))
		return false;
	else
		return memcmp(a, b, sizeof(struct in6_addr)) == 0;
}

/*
 * sid_diff：sid_same 的取反，判断两个 SID 是否不同。
 */
static inline bool sid_diff(
		const struct in6_addr *a,
		const struct in6_addr *b)
{
	return !sid_same(a, b);
}


/*
 * sid_zero：判断段栈的首个 SID 是否为全零地址。
 * 参数：a 段栈指针（内部 assert 非空）。
 * 返回 true 表示首个 SID 为 ::。
 */
static inline bool sid_zero(const struct seg6_seg_stack *a)
{
	struct in6_addr zero = {};

	assert(a);

	return sid_same(&a->seg[0], &zero);
}

/*
 * sid_zero_ipv6：判断一个 IPv6 地址是否为全零（::）。
 * 返回 true 表示全零。
 */
static inline bool sid_zero_ipv6(const struct in6_addr *a)
{
	struct in6_addr zero = {};

	return sid_same(&a[0], &zero);
}

/*
 * sid_copy：把源 SID 拷贝到目的 SID（等价于拷贝一个 in6_addr）。
 * 返回 memcpy 的返回值（即 dst）。
 */
static inline void *sid_copy(struct in6_addr *dst,
		const struct in6_addr *src)
{
	return memcpy(dst, src, sizeof(struct in6_addr));
}

/*
 * seg6local_action2str_with_next_csid：把本地行为动作转为可读字符串。
 * 参数：action 为 ZEBRA_SEG6_LOCAL_ACTION_*；has_next_csid 为 true 时优先
 * 返回 uSID 风格的短名（uN / uA / uDT6 等），否则返回经典 End* 名称。
 * 返回静态字符串。
 */
const char *seg6local_action2str_with_next_csid(uint32_t action, bool has_next_csid);

/*
 * seg6local_action2str：把本地行为动作转为可读字符串（不含 uSID 短名）。
 * 等价于 seg6local_action2str_with_next_csid(action, false)。
 */
const char *
seg6local_action2str(uint32_t action);

/*
 * seg6local_context2str：把 seg6local 上下文按行为格式化为可读字符串。
 * 参数：str / size 输出缓冲区；ctx 上下文；action 行为动作。
 * 返回 str。不同 action 会拼接 nh6 / nh4 / table / oif 等不同信息。
 */
const char *seg6local_context2str(char *str, size_t size,
				  const struct seg6local_context *ctx,
				  uint32_t action);

/* 前向声明 JSON 对象类型，避免头文件直接依赖 lib/json.h。 */
struct json_object;

/*
 * seg6local_context2json：把 seg6local 上下文按行为写入 JSON 对象。
 * 参数：ctx 上下文；action 行为；json 目标对象。
 * 会附加 flavors 数组，并按 action 附加 nh6 / nh4 / table / interfaceName 等。
 */
void seg6local_context2json(const struct seg6local_context *ctx, uint32_t action,
			    struct json_object *json);
/*
 * srv6_sid_structure2json：把 SID 结构（block/node/function/argument 位长）
 * 写入 JSON 对象。
 */
void srv6_sid_structure2json(const struct seg6local_context *ctx, struct json_object *json);

/*
 * srv6_sid_ctx2str：把 struct srv6_sid_ctx 格式化为可读字符串。
 * 参数：str / size 输出缓冲区；ctx SID 上下文。
 * 返回 str。内部先写入行为名，再按行为拼接 nh6 / nh4 / vrf_id 等。
 * 注意：size 由调用者保证足够。
 */
static inline const char *srv6_sid_ctx2str(char *str, size_t size,
					   const struct srv6_sid_ctx *ctx)
{
	int len = 0;

	len += snprintf(str + len, size - len, "%s",
			seg6local_action2str(ctx->behavior));

	switch (ctx->behavior) {
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
		break;

	case ZEBRA_SEG6_LOCAL_ACTION_END:
		snprintf(str + len, size - len, " USP");
		break;

	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		snprintfrr(str + len, size - len, " nh6 %pI6", &ctx->nh6);
		break;

	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		snprintfrr(str + len, size - len, " nh4 %pI4", &ctx->nh4);
		break;

	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		snprintf(str + len, size - len, " vrf_id %u (%s)", ctx->vrf_id,
			 vrf_id_to_name(ctx->vrf_id));
		break;

	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
	case ZEBRA_SEG6_LOCAL_ACTION_END_BPF:
	default:
		snprintf(str + len, size - len, " unknown(%s)", __func__);
	}

	return str;
}

/*
 * ietf_srv6_types_info：ietf-srv6-types YANG 模块信息。本模块仅提供类型定义，
 * 无配置回调（ignore_cfg_cbs），供其他模块引用其 typedef。
 */
extern const struct frr_yang_module_info ietf_srv6_types_info;

/*
 * snprintf_seg6_segs：把 SID 序列（struct seg6_segs）格式化为逗号分隔字符串。
 * 参数：str / size 输出缓冲区；segs 待格式化的 SID 序列。
 * 返回写入字符串的长度（不含结尾 NUL）。
 */
int snprintf_seg6_segs(char *str,
		size_t size, const struct seg6_segs *segs);

/*
 * 以下为 locator 生命周期与 JSON 导出接口：
 *   srv6_locator_alloc           按名字分配 locator（内部初始化 chunks 列表）；
 *   srv6_locator_chunk_alloc     分配一个空 chunk；
 *   srv6_locator_free            释放 locator 及其 chunks；
 *   srv6_locator_chunk_list_free 列表删除回调，释放单个 chunk；
 *   srv6_locator_chunk_free      释放 chunk 指针并置空；
 *   srv6_locator_copy            逐字段拷贝 locator；
 *   srv6_locator_chunk_json            导出 chunk 的精简 JSON；
 *   srv6_locator_json                  导出 locator 的精简 JSON；
 *   srv6_locator_detailed_json         导出 locator 的详细 JSON；
 *   srv6_locator_chunk_detailed_json   导出 chunk 的详细 JSON。
 */
extern struct srv6_locator *srv6_locator_alloc(const char *name);
extern struct srv6_locator_chunk *srv6_locator_chunk_alloc(void);
extern void srv6_locator_free(struct srv6_locator *locator);
extern void srv6_locator_chunk_list_free(void *data);
extern void srv6_locator_chunk_free(struct srv6_locator_chunk **chunk);
extern void srv6_locator_copy(struct srv6_locator *copy,
			      const struct srv6_locator *locator);
struct json_object *srv6_locator_chunk_json(const struct srv6_locator_chunk *chunk);
struct json_object *srv6_locator_json(const struct srv6_locator *loc);
struct json_object *srv6_locator_detailed_json(const struct srv6_locator *loc);
struct json_object *
srv6_locator_chunk_detailed_json(const struct srv6_locator_chunk *chunk);

/*
 * SID format 生命周期接口：
 *   srv6_sid_format_alloc  按名字分配 SID format；
 *   srv6_sid_format_free   释放 SID format；
 *   delete_srv6_sid_format 列表删除回调（参数为 void *），转调 free。
 */
extern struct srv6_sid_format *srv6_sid_format_alloc(const char *name);
extern void srv6_sid_format_free(struct srv6_sid_format *format);
extern void delete_srv6_sid_format(void *format);

/*
 * SID context 生命周期接口：
 *   srv6_sid_ctx_alloc  按行为 / 下一跳 / VRF 分配 SID 上下文（nh4 / nh6 / vrf_id
 *                       为 NULL 或 0 时使用默认空值）；
 *   srv6_sid_ctx_free   释放 SID 上下文。
 */
extern struct srv6_sid_ctx *srv6_sid_ctx_alloc(enum seg6local_action_t behavior,
					       struct in_addr *nh4,
					       struct in6_addr *nh6,
					       vrf_id_t vrf_id);
extern void srv6_sid_ctx_free(struct srv6_sid_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
