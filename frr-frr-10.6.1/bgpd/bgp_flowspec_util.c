// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP FlowSpec Utilities
 * Portions:
 *     Copyright (C) 2017 ChinaTelecom SDN Group
 *     Copyright (C) 2018 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_flowspec_util.c —— FlowSpec 分量的通用解码器(本模块核心)
 * ============================================================================
 * 【解决的问题】FlowSpec 的 NLRI 是一串匹配分量(component)。每个分量的编码分
 * 三类：数值型(端口/协议/DSCP 等)、前缀型(目的/源前缀)、位掩码型(TCP flags、
 * fragment)。本文件用统一的「解码函数 + mode 参数」把同一个分量编码同时服务于
 * 三件不同的事：校验合法性、渲染成字符串/JSON、转成内部 PBR 语义(见
 * bgp_flowspec_util.h 的 enum bgp_flowspec_util_nlri_t)。
 *
 * 【三个核心解码器，各自的编码规则】
 *   1) bgp_flowspec_op_decode —— 数值型分量。编码 = 1 字节操作符 + 变长数值。
 *      操作符字节按位拆开(靠 hex2bin 展开成 8 个 bit)：bit1 是“与前一项的组合
 *      方式”(0=OR, 1=AND)，bit2/bit3 拼出数值长度(字节数 = 1 左移 (2*b2+b3))，
 *      bit5=小于、bit6=大于、bit7=等于、bit0=结束标志(1 表示本分量到此为止)。
 *      同一分量内可连续出现多个“操作符 + 数值”，直到 bit0 置位。
 *   2) bgp_flowspec_ip_address —— 前缀型分量。编码 = 1 字节前缀长度 + IPv6 时
 *      额外 1 字节偏移 + 按位数紧凑排列的地址字节(不补零到整字节)。
 *   3) bgp_flowspec_bitmask_decode —— 位掩码型分量。编码结构同 op_decode，
 *      但语义是“一组位的集合”(详见其函数注释)。
 *
 * 【三个要注意的坑】
 *   ★ result 是 void *：同一函数在字符串模式写 char 缓冲、在 CONVERT 模式写
 *     struct bgp_pbr_match_val 或 struct prefix、在 JSON/校验模式不写。调用方
 *     必须按 mode 提供对应类型，编译器不会帮你查。
 *   ★ *error 双重身份：出错时是负的错误码；成功时被复用为“解出多少个条目”
 *     (loop 计数)——所以读 *error 前必须先确认它非负。
 *   ★ BGP_PBR_MATCH_VAL_MAX 上限：单个分量最多解码这么多条目，超过报 -2 并
 *     停止转换(见 CONVERT 分支里对 *error == -2 的短路)。错误码 -2 与普通错误
 *     -1 的语义不同，读代码时留意区分。
 *
 * 【阅读建议】hex2bin / hexstr2num(基础) → bgp_flowspec_op_decode(数值) →
 *            bgp_flowspec_ip_address(前缀) → bgp_flowspec_bitmask_decode(位掩码)
 *            → bgp_flowspec_match_rules_fill(把分量翻译成 PBR 语义)。
 * ============================================================================
 */

#include "zebra.h"

#include "lib/printfrr.h"

#include "prefix.h"
#include "lib_errors.h"

#include "bgp_route.h"
#include "bgp_table.h"
#include "bgp_flowspec_util.h"
#include "bgp_flowspec_private.h"
#include "bgp_pbr.h"
#include "bgp_errors.h"

/* 【辅助】hex2bin —— 把 1 字节展开成 8 个 bit(高位在前)写进 bin[0..7]。
 * 之所以“展开成 bit 数组”，是因为 FlowSpec 数值分量的操作符字节把若干语义位
 * 挤在一个字节里，逐位判断比写掩码更直观(见 bgp_flowspec_op_decode)。 */
static void hex2bin(uint8_t *hex, int *bin)
{
	int remainder = *hex;
	int i = 0;

	while (remainder >= 1 && i < 8) {
		bin[7-i] = remainder % 2;
		remainder = remainder / 2;
		i++;
	}
	for (; i < 8; i++)
		bin[7-i] = 0;
}

/* 【辅助】hexstr2num —— 把 len 个字节拼成一个大端 uint64。用于把分量里的
 * “变长数值”还原成整数(len 由操作符里的长度字段决定，取值 1/2/4/8 字节)。 */
static uint64_t hexstr2num(uint8_t *hexstr, int len)
{
	int i = 0;
	uint64_t num = 0;

	for (i = 0; i < len; i++)
		num = hexstr[i] + 256 * num;
	return num;
}

/* 【辅助】bgp_flowspec_call_non_opaque_decode —— op_decode 的薄封装。
 * 固定用 CONVERT_TO_NON_OPAQUE 模式，把结果写进 mval 并把解出的条目数回填到
 * *match_num(这里是“借用” error 出参承载条目数)。解码失败时打日志并保留负值。
 * 返回消耗的字节数。 */
/* call bgp_flowspec_op_decode
 * returns offset
 */
static int bgp_flowspec_call_non_opaque_decode(uint8_t *nlri_content, int len,
					       struct bgp_pbr_match_val *mval,
					       uint8_t *match_num, int *error)
{
	int ret;

	ret = bgp_flowspec_op_decode(
			     BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE,
			     nlri_content,
			     len,
			     mval, error);
	if (*error < 0)
		flog_err(EC_BGP_FLOWSPEC_PACKET,
			 "%s: flowspec_op_decode error %d", __func__, *error);
	else
		*match_num = *error;
	return ret;
}


/* 【函数】bgp_flowspec_contains_prefix —— 反查：给定地址(input)是否命中这条
 * FlowSpec 规则(pfs)里所匹配的目的/源前缀。
 * 注意它只关心前缀类分量：其它分量(端口/协议等)这里只做“跳过式”校验以正确
 * 推进解析指针，不参与匹配判断。
 *   prefix_check 为真时要求前缀长度也相等；否则只比较地址本体。
 * 命中返回 true。 */
bool bgp_flowspec_contains_prefix(const struct prefix *pfs,
				  struct prefix *input, int prefix_check)
{
	uint32_t offset = 0;
	int type;
	int ret = 0, error = 0;
	uint8_t *nlri_content = (uint8_t *)pfs->u.prefix_flowspec.ptr;
	size_t len = pfs->u.prefix_flowspec.prefixlen;
	afi_t afi = family2afi(pfs->u.prefix_flowspec.family);
	struct prefix compare;

	error = 0;
	/* 遍历分量，只挑出前缀类分量来比对；其余分量走 VALIDATE_ONLY 只为推进
	 * offset。error 一旦为负立即停。 */
	while (offset + 1 < len && error >= 0) {
		/* 取分量类型并推过类型字节。 */
		type = nlri_content[offset];
		offset++;
		switch (type) {
		case FLOWSPEC_DEST_PREFIX:
		case FLOWSPEC_SRC_PREFIX:
			/* 把前缀分量解成本地 prefix，再与输入地址比对。 */
			memset(&compare, 0, sizeof(compare));
			ret = bgp_flowspec_ip_address(
					BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE,
					nlri_content+offset,
					len - offset,
					&compare, &error,
					afi, NULL);
			if (ret <= 0)
				break;
			/* 【匹配判定】先按 prefix_check 比较前缀长度，再比较族，
			 * 最后比较地址本体；相等即返回 true(命中)。 */
			if (prefix_check &&
			    compare.prefixlen != input->prefixlen)
				break;
			if (compare.family != input->family)
				break;
			if ((input->family == AF_INET) &&
			    IPV4_ADDR_SAME(&input->u.prefix4,
					   &compare.u.prefix4))
				return true;
			if ((input->family == AF_INET6) &&
			    IPV6_ADDR_SAME(&input->u.prefix6.s6_addr,
					   &compare.u.prefix6.s6_addr))
				return true;
			break;
		case FLOWSPEC_FLOW_LABEL:
			/* flow-label 是 v6 专有分量；v4 下判错。 */
			if (afi == AFI_IP) {
				error = -1;
				continue;
			}
			ret = bgp_flowspec_op_decode(BGP_FLOWSPEC_VALIDATE_ONLY,
						     nlri_content+offset,
						     len - offset,
						     NULL, &error);
			break;
		case FLOWSPEC_IP_PROTOCOL:
		case FLOWSPEC_PORT:
		case FLOWSPEC_DEST_PORT:
		case FLOWSPEC_SRC_PORT:
		case FLOWSPEC_ICMP_TYPE:
		case FLOWSPEC_ICMP_CODE:
			ret = bgp_flowspec_op_decode(BGP_FLOWSPEC_VALIDATE_ONLY,
						     nlri_content+offset,
						     len - offset,
						     NULL, &error);
			break;
		case FLOWSPEC_FRAGMENT:
		case FLOWSPEC_TCP_FLAGS:
			ret = bgp_flowspec_bitmask_decode(
						BGP_FLOWSPEC_VALIDATE_ONLY,
						nlri_content+offset,
						len - offset,
						NULL, &error);
			break;
		case FLOWSPEC_PKT_LEN:
		case FLOWSPEC_DSCP:
			ret = bgp_flowspec_op_decode(
						BGP_FLOWSPEC_VALIDATE_ONLY,
						nlri_content + offset,
						len - offset, NULL,
						&error);
			break;
		default:
			error = -1;
			break;
		}
		/* 推进到下一分量(用各分支返回的消耗长度)。 */
		offset += ret;
	}
	return false;
}

/*
 * handle the flowspec address src/dst or generic address NLRI
 * return number of bytes analysed ( >= 0).
 */
/* 【函数】bgp_flowspec_ip_address —— 解析 FlowSpec 的地址类分量(目的/源前缀
 * 或通用地址 NLRI)，并按调用者要求的 type 输出结果。
 * 参数：nlri_ptr / max_len 为待解析的原始 NLRI 缓冲；result 为输出目标
 *   (视 type 为字符串或 struct prefix)；error 出参，出错置负；afi 决定地址族；
 *   ipv6_offset 非空时回填 IPv6 特有的偏移字节。
 * 返回：已解析消耗的字节数(大于等于 0)；出错时返回已推进的偏移量并置
 *   error 为 -1。
 * 要点：先读前缀长度字节，IPv6 还需额外一个 offset 字节；随后对 prefixlen
 *   与缓冲长度做越界 / 溢出防御性检查(psize 必须落在 struct prefix 内)；
 *   仅 CONVERT_TO_NON_OPAQUE 分支才会把结果拷贝给调用者的 prefix。 */
int bgp_flowspec_ip_address(enum bgp_flowspec_util_nlri_t type,
			    uint8_t *nlri_ptr,
			    uint32_t max_len,
			    void *result, int *error,
			    afi_t afi,
			    uint8_t *ipv6_offset)
{
	char *display = (char *)result; /* for return_string */
	struct prefix *prefix = (struct prefix *)result;
	uint32_t offset = 0;
	struct prefix prefix_local;
	int psize;
	uint8_t prefix_offset = 0;

	/* Need at least 1 byte for prefixlen. */
	if (max_len < 1) {
		*error = -1;
		return 0;
	}

	*error = 0;
	memset(&prefix_local, 0, sizeof(prefix_local));
	/* read the prefix length */
	prefix_local.prefixlen = nlri_ptr[offset];
	psize = PSIZE(prefix_local.prefixlen);
	offset++;
	prefix_local.family = afi2family(afi);
	if (prefix_local.family == AF_INET6) {
		/* Need one more byte for IPv6 offset. */
		if (offset >= max_len) {
			*error = -1;
			return offset;
		}

		prefix_offset = nlri_ptr[offset];
		if (ipv6_offset)
			*ipv6_offset = prefix_offset;
		offset++;
	}
	/* Prefix length check. */
	if (prefix_local.prefixlen > prefix_blen(&prefix_local) * 8) {
		*error = -1;
		return offset;
	}
	/* When packet overflow occur return immediately. */
	if (psize + offset > max_len) {
		*error = -1;
		return offset;
	}
	/* Defensive coding, double-check
	 * the psize fits in a struct prefix
	 */
	if (psize > (ssize_t)sizeof(prefix_local.u)) {
		*error = -1;
		return offset;
	}

	memcpy(&prefix_local.u.prefix, &nlri_ptr[offset], psize);
	offset += psize;
	switch (type) {
	case BGP_FLOWSPEC_RETURN_STRING:
		if (prefix_local.family == AF_INET6) {
			int ret;

			ret = snprintfrr(
				display, BGP_FLOWSPEC_STRING_DISPLAY_MAX,
				"%pFX/off %u", &prefix_local, prefix_offset);
			if (ret < 0) {
				*error = -1;
				break;
			}
		} else
			prefix2str(&prefix_local, display,
				   BGP_FLOWSPEC_STRING_DISPLAY_MAX);
		break;
	case BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE:
		if (prefix)
			prefix_copy(prefix, &prefix_local);
		break;
	case BGP_FLOWSPEC_VALIDATE_ONLY:
	case BGP_FLOWSPEC_RETURN_JSON:
		break;
	}
	return offset;
}

/*
 * handle the flowspec operator NLRI
 * return number of bytes analysed
 * if there is an error, the passed error param is used to give error:
 * -1 if decoding error,
 * if result is a string, its assumed length
 *  is BGP_FLOWSPEC_STRING_DISPLAY_MAX
 */
/* 【函数】bgp_flowspec_op_decode —— 解析 FlowSpec 的“操作符 + 数值”分量，
 * 典型用于端口、协议、报文长度、DSCP、ICMP type/code、flow-label 等。
 * 参数：nlri_ptr / max_len 为原始缓冲；result 视 type 为输出(字符串或
 *   struct bgp_pbr_match_val 数组)；error 出参。
 * 返回：消耗的字节数；出错时置 error 为负数；成功时把 error 复用为本次解析
 *   出的条目数(loop)，调用者据此得到 match 项的数量。
 * 要点：一个分量内可含多个操作数，用 AND / OR 位串联，do-while 保证至少执行
 *   一次；首个元素不允许置 AND 位；同一项不能同时是小于和大于；条目数超过
 *   BGP_PBR_MATCH_VAL_MAX 时置 error=-2 并停止转换。 */
int bgp_flowspec_op_decode(enum bgp_flowspec_util_nlri_t type,
			   uint8_t *nlri_ptr,
			   uint32_t max_len,
			   void *result, int *error)
{
	int op[8];
	int len, value_size;
	uint64_t value;
	int loop = 0;
	char *ptr = (char *)result; /* for return_string */
	uint32_t offset = 0;
	int len_string = BGP_FLOWSPEC_STRING_DISPLAY_MAX;
	int len_written;
	struct bgp_pbr_match_val *mval = (struct bgp_pbr_match_val *)result;

	*error = 0;

	/* do/while executes at least once, so guard first deref */
	if (max_len < 1) {
		*error = -1;
		return 0;
	}

	do {
		if (loop >= BGP_PBR_MATCH_VAL_MAX) {
			*error = -2;
			return offset;
		}

		if (offset >= max_len) {
			*error = -1;
			break;
		}

		hex2bin(&nlri_ptr[offset], op);
		offset++;
		len = 2*op[2]+op[3];
		value_size = 1 << len;

		if (offset > max_len || (uint32_t)value_size > (max_len - offset)) {
			*error = -1;
			break;
		}

		value = hexstr2num(&nlri_ptr[offset], value_size);
		/* can not be < and > at the same time */
		if (op[5] == 1 && op[6] == 1)
			*error = -1;
		/* if first element, AND bit can not be set */
		if (op[1] == 1 && loop == 0)
			*error = -1;
		switch (type) {
		case BGP_FLOWSPEC_RETURN_STRING:
			if (loop) {
				len_written = snprintf(ptr, len_string,
						      ", ");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			if (op[5] == 1) {
				len_written = snprintf(ptr, len_string,
						       "<");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			if (op[6] == 1) {
				len_written = snprintf(ptr, len_string,
						      ">");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			if (op[7] == 1) {
				len_written = snprintf(ptr, len_string,
						       "=");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			len_written = snprintf(ptr, len_string, " %" PRIu64 " ",
					       (unsigned long long)value);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		case BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE:
			/* limitation: stop converting */
			if (*error == -2)
				break;
			if (value > UINT16_MAX) {
				*error = -1;
				break;
			}
			mval->value = value;
			if (op[5] == 1)
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_LESS_THAN);
			if (op[6] == 1)
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_GREATER_THAN);
			if (op[7] == 1)
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_EQUAL_TO);
			if (op[1] == 1)
				mval->unary_operator = OPERATOR_UNARY_AND;
			else
				mval->unary_operator = OPERATOR_UNARY_OR;
			mval++;
			break;
		case BGP_FLOWSPEC_VALIDATE_ONLY:
		case BGP_FLOWSPEC_RETURN_JSON:
			/* no action */
			break;
		}
		offset += value_size;
		loop++;
	} while (op[0] == 0 && offset < max_len - 1);
	if (offset > max_len)
		*error = -1;
	/* use error parameter to count the number of entries */
	if (*error == 0)
		*error = loop;
	return offset;
}


/*
 * handle the flowspec tcpflags or fragment field
 * return number of bytes analysed
 * if there is an error, the passed error param is used to give error:
 * -1 if decoding error,
 * if result is a string, its assumed length
 *  is BGP_FLOWSPEC_STRING_DISPLAY_MAX
 */
/* 【函数】bgp_flowspec_bitmask_decode —— 解析 FlowSpec 的位掩码类分量
 * (tcp-flags / fragment)；结构类似 op_decode，但比较语义为位运算。
 * 参数：nlri_ptr / max_len 为原始缓冲；result 视 type 为输出(字符串或
 *   struct bgp_pbr_match_val 数组)；error 出参。
 * 返回：消耗的字节数；出错时置 error 为负数；成功时把 error 复用为解析出的
 *   条目数。
 * 要点：字符串模式下以“包含”“取反”等符号表达掩码比较；“不同”位在 CONVERT
 *   模式下会同时置 LESS_THAN 与 GREATER_THAN 两个比较标志；条目数超上限时
 *   置 error=-2 并中断后续转换。 */
int bgp_flowspec_bitmask_decode(enum bgp_flowspec_util_nlri_t type,
				 uint8_t *nlri_ptr,
				 uint32_t max_len,
				 void *result, int *error)
{
	int op[8];
	int len, value_size, loop = 0;
	char *ptr = (char *)result; /* for return_string */
	struct bgp_pbr_match_val *mval = (struct bgp_pbr_match_val *)result;
	uint32_t offset = 0;
	int len_string = BGP_FLOWSPEC_STRING_DISPLAY_MAX;
	int len_written;
	uint64_t value;

	*error = 0;

	/* do/while executes at least once, so guard first deref */
	if (max_len < 1) {
		*error = -1;
		return 0;
	}

	do {
		if (loop >= BGP_PBR_MATCH_VAL_MAX) {
			*error = -2;
			return offset;
		}

		hex2bin(&nlri_ptr[offset], op);
		/* if first element, AND bit can not be set */
		if (op[1] == 1 && loop == 0)
			*error = -1;
		offset++;
		len = 2 * op[2] + op[3];
		value_size = 1 << len;

		if (offset > max_len || (uint32_t)value_size > (max_len - offset)) {
			*error = -1;
			break;
		}

		value = hexstr2num(&nlri_ptr[offset], value_size);
		switch (type) {
		case BGP_FLOWSPEC_RETURN_STRING:
			if (op[1] == 1 && loop != 0) {
				len_written = snprintf(ptr, len_string,
						       ",&");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			} else if (op[1] == 0 && loop != 0) {
				len_written = snprintf(ptr, len_string,
						      ",|");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			if (op[7] == 1) {
				len_written = snprintf(ptr, len_string,
					       "= ");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			} else {
				len_written = snprintf(ptr, len_string,
						       "∋ ");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			if (op[6] == 1) {
				len_written = snprintf(ptr, len_string,
					       "! ");
				if (len_written > 0 && len_written < len_string) {
					len_string -= len_written;
					ptr += len_written;
				}
			}
			len_written = snprintf(ptr, len_string, "%" PRIu64,
					       (unsigned long long)value);
			if (len_written > 0 && len_written < len_string) {
				len_string -= len_written;
				ptr += len_written;
			}
			break;
		case BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE:
			/* limitation: stop converting */
			if (*error == -2)
				break;
			if (value > UINT16_MAX) {
				*error = -1;
				break;
			}
			mval->value = value;
			if (op[6] == 1) {
				/* different from */
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_LESS_THAN);
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_GREATER_THAN);
			} else
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_EQUAL_TO);
			if (op[7] == 1)
				SET_FLAG(mval->compare_operator,
					 OPERATOR_COMPARE_EXACT_MATCH);
			if (op[1] == 1)
				mval->unary_operator =
					OPERATOR_UNARY_AND;
			else
				mval->unary_operator =
					OPERATOR_UNARY_OR;
			mval++;
			break;
		case BGP_FLOWSPEC_VALIDATE_ONLY:
		case BGP_FLOWSPEC_RETURN_JSON:
			/* no action */
			break;
		}
		offset += value_size;
		loop++;
	} while (op[0] == 0 && offset < max_len - 1);
	if (offset > max_len)
		*error = -1;
	/* use error parameter to count the number of entries */
	if (*error == 0)
		*error = loop;
	return offset;
}

/* 【函数】bgp_flowspec_match_rules_fill —— 把一条 FlowSpec NLRI 解析并填充到
 * bgp_pbr_entry_main(bpem)中，供后续下发到数据面(PBR / iptables)使用。
 * 参数：nlri_content / len 为原始 NLRI；bpem 为输出结构；afi 为地址族。
 * 返回：error —— 解析过程中的错误码，0 表示成功。
 * 要点：按分量类型逐一解析；前缀分量写入 dst/src_prefix 并置 match_bitmask，
 *   但若前缀是 0.0.0.0 或全零 IPv6，则改置 match_bitmask_iprule(表示“任意到
 *   任意”的 ip rule，可能无需 ipset 过滤)；flow-label 为 v6 专有，v4 下恒判错。
 *   最后依据各 match_*_num 是否非零，决定 bpem->type 为 IPSET / IPRULE /
 *   UNDEFINED。 */
int bgp_flowspec_match_rules_fill(uint8_t *nlri_content, int len,
				  struct bgp_pbr_entry_main *bpem,
				  afi_t afi)
{
	int offset = 0, error = 0;
	struct prefix *prefix;
	struct bgp_pbr_match_val *mval;
	uint8_t *match_num;
	uint8_t bitmask = 0;
	int ret = 0, type;
	uint8_t *prefix_offset;

	while (offset < len - 1 && error >= 0) {
		type = nlri_content[offset];
		offset++;
		switch (type) {
		case FLOWSPEC_DEST_PREFIX:
		case FLOWSPEC_SRC_PREFIX:
			bitmask = 0;
			if (type == FLOWSPEC_DEST_PREFIX) {
				SET_FLAG(bitmask, PREFIX_DST_PRESENT);
				prefix = &bpem->dst_prefix;
				prefix_offset = &bpem->dst_prefix_offset;
			} else {
				SET_FLAG(bitmask, PREFIX_SRC_PRESENT);
				prefix = &bpem->src_prefix;
				prefix_offset = &bpem->src_prefix_offset;
			}
			ret = bgp_flowspec_ip_address(
					BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE,
					nlri_content + offset,
					len - offset,
					prefix, &error,
					afi, prefix_offset);
			if (error < 0)
				flog_err(EC_BGP_FLOWSPEC_PACKET,
					 "%s: flowspec_ip_address error %d",
					 __func__, error);
			else {
				/* if src or dst address is 0.0.0.0,
				 * ignore that rule
				 */
				if (prefix->family == AF_INET
				    && prefix->u.prefix4.s_addr == INADDR_ANY)
					SET_FLAG(bpem->match_bitmask_iprule,
						 bitmask);
				else if (prefix->family == AF_INET6
					 && !memcmp(&prefix->u.prefix6,
						    &in6addr_any,
						    sizeof(struct in6_addr)))
					SET_FLAG(bpem->match_bitmask_iprule,
						 bitmask);
				else
					SET_FLAG(bpem->match_bitmask, bitmask);
			}
			offset += ret;
			break;
		case FLOWSPEC_FLOW_LABEL:
			if (afi == AFI_IP) {
				error = -1;
				continue;
			}
			match_num = &(bpem->match_flowlabel_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->flow_label);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_IP_PROTOCOL:
			match_num = &(bpem->match_protocol_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->protocol);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_PORT:
			match_num = &(bpem->match_port_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->port);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_DEST_PORT:
			match_num = &(bpem->match_dst_port_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->dst_port);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_SRC_PORT:
			match_num = &(bpem->match_src_port_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->src_port);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_ICMP_TYPE:
			match_num = &(bpem->match_icmp_type_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->icmp_type);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_ICMP_CODE:
			match_num = &(bpem->match_icmp_code_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->icmp_code);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_PKT_LEN:
			match_num =
				&(bpem->match_packet_length_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->packet_length);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_DSCP:
			match_num = &(bpem->match_dscp_num);
			mval = (struct bgp_pbr_match_val *)
				&(bpem->dscp);
			offset += bgp_flowspec_call_non_opaque_decode(
							nlri_content + offset,
							len - offset,
							mval, match_num,
							&error);
			break;
		case FLOWSPEC_TCP_FLAGS:
			ret = bgp_flowspec_bitmask_decode(
					BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE,
					nlri_content + offset,
					len - offset,
					&bpem->tcpflags, &error);
			if (error < 0)
				flog_err(
					EC_BGP_FLOWSPEC_PACKET,
					"%s: flowspec_tcpflags_decode error %d",
					__func__, error);
			else
				bpem->match_tcpflags_num = error;
			/* contains the number of slots used */
			offset += ret;
			break;
		case FLOWSPEC_FRAGMENT:
			ret = bgp_flowspec_bitmask_decode(
					BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE,
					nlri_content + offset,
					len - offset, &bpem->fragment,
					&error);
			if (error < 0)
				flog_err(
					EC_BGP_FLOWSPEC_PACKET,
					"%s: flowspec_fragment_type_decode error %d",
					__func__, error);
			else
				bpem->match_fragment_num = error;
			offset += ret;
			break;
		default:
			flog_err(EC_LIB_DEVELOPMENT, "%s: unknown type %d",
				 __func__, type);
		}
	}
	if (bpem->match_packet_length_num || bpem->match_fragment_num
	    || bpem->match_tcpflags_num || bpem->match_dscp_num
	    || bpem->match_icmp_code_num || bpem->match_icmp_type_num
	    || bpem->match_port_num || bpem->match_src_port_num
	    || bpem->match_dst_port_num || bpem->match_protocol_num
	    || bpem->match_bitmask || bpem->match_flowlabel_num)
		bpem->type = BGP_PBR_IPSET;
	else if (CHECK_FLAG(bpem->match_bitmask_iprule, PREFIX_SRC_PRESENT) ||
		 CHECK_FLAG(bpem->match_bitmask_iprule, PREFIX_DST_PRESENT))
		/* the extracted policy rule may not need an
		 * iptables/ipset filtering. check this may not be
		 * a standard ip rule : permit any to any ( eg)
		 */
		bpem->type = BGP_PBR_IPRULE;
	else
		bpem->type = BGP_PBR_UNDEFINED;
	return error;
}

/* return 1 if FS entry invalid or no NH IP */
/* 【函数】bgp_flowspec_get_first_nh —— 从一条 FlowSpec 路由(bgp_path_info)
 * 中取出第一条 redirect-ip 动作的下一跳地址，写入 p。
 * 参数：bgp / pi 为路由来源；p 为输出前缀(含地址族与前缀长度)；afi 为地址族。
 * 返回：true 表示该条目无效或没有下一跳 IP；false 表示已成功取到下一跳。
 * 要点：先把目的前缀构造为 bgp_pbr_entry_main 并校验(失败即返回 true)，
 *   再遍历 actions 查找 ACTION_REDIRECT_IP；取到的地址按 afi 写入 p，
 *   IPv4 用 zr.redirect_ip_v4，IPv6 用 zr.redirect_ip_v6 并置满位前缀长度。 */
bool bgp_flowspec_get_first_nh(struct bgp *bgp, struct bgp_path_info *pi,
			       struct prefix *p, afi_t afi)
{
	struct bgp_pbr_entry_main api;
	int i;
	struct bgp_dest *dest = pi->net;
	struct bgp_pbr_entry_action *api_action;

	memset(&api, 0, sizeof(api));
	if (bgp_pbr_build_and_validate_entry(bgp_dest_get_prefix(dest), pi,
					     &api)
	    < 0)
		return true;
	for (i = 0; i < api.action_num; i++) {
		api_action = &api.actions[i];
		if (api_action->action != ACTION_REDIRECT_IP)
			continue;
		p->family = afi2family(afi);
		if (afi == AFI_IP) {
			p->prefixlen = IPV4_MAX_BITLEN;
			p->u.prefix4 = api_action->u.zr.redirect_ip_v4;
		} else {
			p->prefixlen = IPV6_MAX_BITLEN;
			memcpy(&p->u.prefix6, &api_action->u.zr.redirect_ip_v6,
			       sizeof(struct in6_addr));
		}
		return false;
	}
	return true;
}
