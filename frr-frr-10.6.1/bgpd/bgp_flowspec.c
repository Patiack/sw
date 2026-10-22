// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP FlowSpec for packet handling
 * Portions:
 *     Copyright (C) 2017 ChinaTelecom SDN Group
 *     Copyright (C) 2018 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_flowspec.c —— FlowSpec NLRI 的接收入口与合法性校验
 * ============================================================================
 * 【解决的问题】对端通过 MP_REACH / MP_UNREACH 发来 FlowSpec 规则
 * (AFI/SAFI = 1/133、2/133)。本文件负责在 NLRI 层把它们收下来：先逐分量做一遍
 * 格式校验，再把整条规则打包成 AF_FLOWSPEC 前缀交给公共的 bgp_update /
 * bgp_withdraw 走后续流程。真正把分量翻译成 PBR 语义在下游 bgp_pbr.c /
 * bgp_flowspec_util.c，本文件只做「收包 + 校验 + 转发」这一段。
 *
 * 【为什么单独一个解析入口】普通 NLRI 前缀格式固定，解码逻辑通用；FlowSpec 的
 * NLRI 是「长度 + 一串 TLV 式匹配分量」，且分量类型与 AFI 相关(v6 才有
 * flow-label)，所以需要一个专用校验器 bgp_fs_nlri_validate 逐分量走一遍，
 * 任何分量不合法就整条判为 BAD_FORMAT 丢弃。
 *
 * 【关键点】
 *   - 无属性(attr 为空)时按 RFC 当作隐式撤销(implicit withdraw)。
 *   - NLRI 长度有两种编码：首字节小于 240 时即长度；否则用两字节(高 4 位忽略)。
 *   - 校验通过后不在这里解析语义，而是构造 struct prefix(family 标为
 *     AF_FLOWSPEC)，把原始字节指针挂在 p.u.prefix_flowspec.ptr 上交给下游。
 *
 * 【推荐阅读顺序】bgp_fs_nlri_validate(逐分量校验) → bgp_nlri_parse_flowspec
 *                  (收包主循环)。
 * ============================================================================
 */

#include <zebra.h>
#include <math.h>

#include "prefix.h"
#include "lib_errors.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_flowspec.h"
#include "bgpd/bgp_flowspec_util.h"
#include "bgpd/bgp_flowspec_private.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"

/* 【函数】bgp_fs_nlri_validate —— 逐分量校验一条 FlowSpec NLRI 的格式合法性。
 * 它不对结果做任何转换，纯粹是“能不能解通”的试走：按类型分发到对应解码器，
 * 并把解码器返回的“消耗字节数”用来推进 offset。任何分量解不通就置 error 跳出。
 * 返回 0 表示合法，负值表示不合法(具体取值由各解码器给出)。 */
static int bgp_fs_nlri_validate(uint8_t *nlri_content, uint32_t len,
				afi_t afi)
{
	uint32_t offset = 0;
	int type;
	int ret = 0, error = 0;

	/* 逐分量遍历：offset 指向当前分量的“类型字节”。条件 offset + 1 < len
	 * 保证至少还能读到 1 字节类型；每轮用解码器报告的消耗长度推进 offset。 */
	while (offset + 1 < len) {
		/* 读 1 字节分量类型号(见 bgp_flowspec_private.h 的 FLOWSPEC 系列
		 * 宏)，随后把 offset 推过类型字节，指向该分量的编码内容。 */
		type = nlri_content[offset];
		offset++;
		/* 【分发】按分量类型选不同的解码器(VALIDATE_ONLY 模式只校验、不产出
		 * 结果)：前缀类走 ip_address；数值类走 op_decode；位掩码类(9 TCP
		 * flags、12 fragment)走 bitmask_decode；v6 flow-label 也走 op_decode
		 * 但仅 IPv6 允许(v4 出现直接判错)。 */
		switch (type) {
		case FLOWSPEC_DEST_PREFIX:
		case FLOWSPEC_SRC_PREFIX:
			ret = bgp_flowspec_ip_address(
						BGP_FLOWSPEC_VALIDATE_ONLY,
						nlri_content + offset,
						len - offset, NULL, &error,
						afi, NULL);
			break;
		case FLOWSPEC_FLOW_LABEL:
			/* flow-label 是 IPv6 专有分量；在 IPv4 下出现即格式错误。 */
			if (afi == AFI_IP)
				return -1;
			ret = bgp_flowspec_op_decode(BGP_FLOWSPEC_VALIDATE_ONLY,
						   nlri_content + offset,
						   len - offset, NULL, &error);
			break;
		case FLOWSPEC_IP_PROTOCOL:
		case FLOWSPEC_PORT:
		case FLOWSPEC_DEST_PORT:
		case FLOWSPEC_SRC_PORT:
		case FLOWSPEC_ICMP_TYPE:
		case FLOWSPEC_ICMP_CODE:
			ret = bgp_flowspec_op_decode(BGP_FLOWSPEC_VALIDATE_ONLY,
						   nlri_content + offset,
						   len - offset, NULL, &error);
			break;
		case FLOWSPEC_TCP_FLAGS:
		case FLOWSPEC_FRAGMENT:
			ret = bgp_flowspec_bitmask_decode(
						   BGP_FLOWSPEC_VALIDATE_ONLY,
						   nlri_content + offset,
						   len - offset, NULL, &error);
			break;
		case FLOWSPEC_PKT_LEN:
		case FLOWSPEC_DSCP:
			ret = bgp_flowspec_op_decode(
						BGP_FLOWSPEC_VALIDATE_ONLY,
						nlri_content + offset,
						len - offset, NULL, &error);
			break;
		/* 出现本实现不认识的类型号：视为格式错误，停止校验(0x19=25 之外的
		 * 分量按 RFC 应忽略，这里选择从严报错，读代码时留意这一策略差异)。 */
		default:
			error = -1;
			break;
		}
		/* 用解码器报告的消耗长度推进 offset；若已置错误则跳出，末尾统一返回。 */
		offset += ret;
		if (error < 0)
			break;
	}
	return error;
}

/* 【函数】bgp_nlri_parse_flowspec —— FlowSpec NLRI 的接收主入口。
 * 由公共 NLRI 分发逻辑按 AFI/SAFI 调到此处。处理 MP_REACH(更新)与
 * MP_UNREACH(撤销)两种情形：逐条取出 NLRI，先过 bgp_fs_nlri_validate 校验，
 * 再组装成 AF_FLOWSPEC 前缀交给 bgp_update / bgp_withdraw。
 * 返回 BGP_NLRI_PARSE_OK 或具体错误码。 */
int bgp_nlri_parse_flowspec(struct peer *peer, struct attr *attr,
			    struct bgp_nlri *packet, bool withdraw)
{
	uint8_t *pnt;
	uint8_t *lim;
	afi_t afi;
	safi_t safi;
	int psize = 0;
	struct prefix p;

	/* 【定位】pnt 指向本 MP 报文里 NLRI 区段的起点，lim 是其末尾；循环用
	 * pnt = pnt + psize 逐条推进，直到越过 lim。 */
	/* Start processing the NLRI - there may be multiple in the MP_REACH */
	pnt = packet->nlri;
	lim = pnt + packet->length;
	afi = packet->afi;
	safi = packet->safi;

	/*
	 * All other AFI/SAFI's treat no attribute as a implicit
	 * withdraw.  Flowspec should as well.
	 */
	/* RFC 8955：FlowSpec 与其它 AFI/SAFI 一样，MP_UNREACH 或缺失属性都按隐式
	 * 撤销处理——attr 不存在就等价于 withdraw。 */
	if (!attr)
		withdraw = true;

	/* 逐条 NLRI 处理：每条以“长度”开场，后跟 psize 字节的分量内容。 */
	for (; pnt < lim; pnt += psize) {
		/* Clear prefix structure. */
		memset(&p, 0, sizeof(p));

		/* All FlowSpec NLRI begin with length. */
		if (pnt + 1 > lim)
			return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;

		/* 【长度编码】首字节小于 240 时它本身就是长度；若大于等于 240 则说明
		 * 长度用两字节表示：取首字节低 4 位左移 8 位，再并上第二个字节
		 * (高 4 位为保留位，按规范应忽略)。 */
		psize = *pnt++;
		if (psize >= FLOWSPEC_NLRI_SIZELIMIT) {
			/* We're going to look at next octet */
			if (pnt + 1 > lim)
				return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;

			psize &= 0x0f;
			psize = psize << 8;
			psize |= *pnt++;
		}
		/* When packet overflow occur return immediately. */
		if (pnt + psize > lim) {
			flog_err(
				EC_BGP_FLOWSPEC_PACKET,
				"Flowspec NLRI length inconsistent ( size %u seen)",
				psize);
			return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;
		}

		if (psize == 0) {
			flog_err(EC_BGP_FLOWSPEC_PACKET,
				 "Flowspec NLRI length 0 which makes no sense");
			return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;
		}

		if (bgp_fs_nlri_validate(pnt, psize, afi) < 0) {
			flog_err(
				EC_BGP_FLOWSPEC_PACKET,
				"Bad flowspec format or NLRI options not supported");
			return BGP_NLRI_PARSE_ERROR_FLOWSPEC_BAD_FORMAT;
		}
		/* 【组装前缀】把校验通过的这段字节封装成 AF_FLOWSPEC 前缀：family 标
		 * 为 flowspec；外层 prefixlen 置 0；真正内容并不拷贝，而是把 ptr 直接
		 * 指向报文缓冲里的这段字节(零拷贝，生命周期依赖报文缓冲)。 */
		p.family = AF_FLOWSPEC;
		p.prefixlen = 0;
		/* Flowspec encoding is in bytes */
		p.u.prefix_flowspec.prefixlen = psize;
		p.u.prefix_flowspec.family = afi2family(afi);
		p.u.prefix_flowspec.ptr = (uintptr_t)pnt;

		/* 【调试输出】仅在开启 flowspec 调试时，把本条 NLRI 渲染成可读字符串，
		 * 并附带其扩展团体(动作)一起打到日志，便于排障。 */
		if (BGP_DEBUG(flowspec, FLOWSPEC)) {
			char return_string[BGP_FLOWSPEC_NLRI_STRING_MAX];
			char local_string[BGP_FLOWSPEC_NLRI_STRING_MAX*2+16];
			char ec_string[BGP_FLOWSPEC_NLRI_STRING_MAX];
			char *s = NULL;

			bgp_fs_nlri_get_string((unsigned char *)
					       p.u.prefix_flowspec.ptr,
					       p.u.prefix_flowspec.prefixlen,
					       return_string,
					       NLRI_STRING_FORMAT_MIN, NULL,
					       afi);
			snprintf(ec_string, sizeof(ec_string),
				 "EC{none}");
			if (attr && bgp_attr_get_ecommunity(attr)) {
				s = ecommunity_ecom2str(
					bgp_attr_get_ecommunity(attr),
					ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
				snprintf(ec_string, sizeof(ec_string),
					 "EC{%s}",
					s == NULL ? "none" : s);

				if (s)
					ecommunity_strfree(&s);
			}
			snprintf(local_string, sizeof(local_string),
				 "FS Rx %s %s %s %s", withdraw ?
				 "Withdraw":"Update",
				 afi2str(afi), return_string,
				 attr != NULL ? ec_string : "");
			zlog_info("%s", local_string);
		}
		/* 【分发】非撤销走 bgp_update，撤销走 bgp_withdraw；两者都以
		 * ZEBRA_ROUTE_BGP / BGP_ROUTE_NORMAL 交给后续流程处理。 */
		/* Process the route. */
		if (!withdraw) {
			bgp_update(peer, &p, 0, attr, afi, safi,
				   ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, NULL,
				   NULL, 0, 0, NULL);
		} else {
			bgp_withdraw(peer, &p, 0, afi, safi, ZEBRA_ROUTE_BGP,
				     BGP_ROUTE_NORMAL, NULL, NULL, 0);
		}
	}
	return BGP_NLRI_PARSE_OK;
}
