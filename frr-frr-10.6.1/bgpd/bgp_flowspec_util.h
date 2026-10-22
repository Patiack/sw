// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Flowspec header for utilities
 * Copyright (C) 2018 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_flowspec_util.h —— FlowSpec 分量的“通用解码器”
 * ============================================================================
 * 【本文件解决的核心问题：一份分量要干三件不同的事】FlowSpec NLRI 里每个分量
 * （端口、协议、前缀……）的编码方式各不相同，但处理流程是一样的三步：
 *   ① 校验格式是否合法；② 换算成人类可读的字符串或 JSON 供展示；
 *   ③ 换算出内部 PBR 语义（非 opaque 的数值）供下发 zebra。
 * 与其写三套解析，FRR 用**同一个解码函数 + 一个 mode 参数**切换这三件事——
 * 这就是下面 enum bgp_flowspec_util_nlri_t 的意义，也是本文件最需要理解的设计。
 *
 * 【由此带来的两个阅读难点】
 *   ★ result 是 void *：因为同一个函数在 RETURN_STRING 时要写 char 缓冲、在
 *     RETURN_JSON 时要写 json 对象、在 CONVERT_TO_NON_OPAQUE 时要写内部结构体。
 *     **调用方必须按自己传的 mode 提供对应类型的对象**，类型不匹配编译器不会报错，
 *     这是本模块最容易出错误的接口形态。
 *   ★ mode 与 bgp_flowspec.h 里的 NLRI_STRING_FORMAT_* 是**两套不同的东西**：
 *     那边决定“字符串要多详细”，这边决定“解码结果往哪去”。名字相近，切勿混用。
 *
 * 【error 出参】多个函数带 int *error：返回负数表示“没解出”时的**具体原因**，
 * 解析错误信息以便日志定位；调用时不要传 NULL，否则错在哪就丢了。
 *
 * 【阅读建议】先看 enum 四个 mode 的含义 → bgp_flowspec_op_decode（分量值解码，
 *            最核心）→ ip_address / bitmask_decode（两种特殊编码）→
 *            match_rules_fill（转内部 PBR 语义）。实现在 bgp_flowspec_util.c。
 * ============================================================================
 */

#ifndef _FRR_BGP_FLOWSPEC_UTIL_H
#define _FRR_BGP_FLOWSPEC_UTIL_H

#include "zclient.h"

/* 【缓冲长度】把分量渲染成字符串时的缓冲上限（与 bgp_flowspec.h 里的
 * BGP_FLOWSPEC_NLRI_STRING_MAX 同为 512 但用途不同：那个针对整条 NLRI，
 * 这个针对工具函数层面的显示串）。 */
#define BGP_FLOWSPEC_STRING_DISPLAY_MAX 512

/* 【枚举】解码模式——决定“同一个解码函数这次要产出什么”（详见文件头导读）：
 *   VALIDATE_ONLY(=0)           : 只走到“能解通”，不产出结果。用于收包时快速
 *                                 判合法，不做转换开销。
 *   RETURN_STRING(=1)           : 生成人读字符串，result 需指向 char 缓冲。
 *   CONVERT_TO_NON_OPAQUE(=2)   : 转成内部可下发的语义值，result 指向内部结构。
 *   RETURN_JSON(=3)             : 生成 JSON，result 指向 json 对象。
 * 注意 0 是“只校验”而不是“无模式”，用 0 做默认值等于选择了“什么都不产出”。 */
enum bgp_flowspec_util_nlri_t {
	BGP_FLOWSPEC_VALIDATE_ONLY = 0,
	BGP_FLOWSPEC_RETURN_STRING = 1,
	BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE = 2,
	BGP_FLOWSPEC_RETURN_JSON = 3,
};


/* 【函数】bgp_flowspec_op_decode —— 解码“数值型分量”的**运算符 + 值**部分。
 * FlowSpec 里数值分量不是简单写个数字，而是「运算符(比较/与或等) + 数值」，
 * 且数值长度可变（1/2/4/8 字节）。本函数负责把这段解出来。
 *   nlri_ptr / max_len : 待解字节与**剩余长度上限**——必须传，长度可变的分量
 *                        全靠它防越界。
 *   result             : 按 type 决定的目标对象（见文件头导读的 void * 陷阱）。
 *   error              : 失败原因出参。
 * 返回值为本次**消耗掉的字节数**（用于推进解析指针），负值表示出错。 */
extern int bgp_flowspec_op_decode(enum bgp_flowspec_util_nlri_t type,
				  uint8_t *nlri_ptr,
				  uint32_t max_len,
				  void *result, int *error);

/* 【函数】bgp_flowspec_ip_address —— 解码前缀类分量（目的/源前缀）。
 * 前缀分量编码特殊：先 1 字节前缀长度，再是**按位数紧凑排列**的地址字节
 * （不补零到整字节），还带偏移量语义，所以单独一个函数处理。
 *   ipv6_offset : 出参，IPv6 下的偏移信息（v6 的编码与 v4 不同，读实现）。
 *   afi         : 决定按 v4 还是 v6 解释——同一段字节两种解释结果完全不同。
 * 返回消耗字节数，负值为错误。 */
extern int bgp_flowspec_ip_address(enum bgp_flowspec_util_nlri_t type,
				   uint8_t *nlri_ptr,
				   uint32_t max_len,
				   void *result, int *error,
				   afi_t afi, uint8_t *ipv6_offset);

/* 【函数】bgp_flowspec_bitmask_decode —— 解码“位掩码型分量”：
 *   TCP flags(9)、fragment(12) 这类不是一个数值，而是**一组位的集合**，
 *   编码上还带“与/非”之类的位运算语义，与普通数值分量不同。
 * 参数含义与 op_decode 一致（含 result 的 void * 陷阱与 error 出参）。
 * 返回消耗字节数，负值为错误。 */
extern int bgp_flowspec_bitmask_decode(enum bgp_flowspec_util_nlri_t type,
					uint8_t *nlri_ptr,
					uint32_t max_len,
					void *result, int *error);

struct bgp_pbr_entry_main;
/* 【函数】bgp_flowspec_match_rules_fill —— 把整条 NLRI 的**全部分量**解析并填充
 * 进内部的 PBR 条目结构(bpem)。这是“从 BGP 收到 → 转成可下发策略”的关键一跳，
 * 后续由 bgp_pbr.c 送去 zebra。
 *   nlri_content / len : 原始 NLRI 字节与长度。
 *   bpem               : 调用方提供的目标结构（前置声明在此避免头文件互相包含）。
 *   afi                : v4 / v6 解释。
 * 返回填好的分量处理情况（负值为错误，具体约定读实现）。 */
extern int bgp_flowspec_match_rules_fill(uint8_t *nlri_content, int len,
					 struct bgp_pbr_entry_main *bpem,
					 afi_t afi);

/* 【函数】bgp_flowspec_contains_prefix —— 判断：FlowSpec 规则里所匹配的目的前缀
 * 是否覆盖（包含）给定地址。
 *   pfs           : FlowSpec 规则前缀（其内部编码即上面说的紧凑前缀）。
 *   input         : 待判断的地址。
 *   prefix_check  : 非 0 时按**前缀包含**判断（输入当作前缀），否则按“输入是否为
 *                   规则前缀内的一个具体地址”判断——两种语义差别很大。
 * 返回 true 表示命中。用于“某 IP 匹配了哪些 FlowSpec 规则”的反查。 */
extern bool bgp_flowspec_contains_prefix(const struct prefix *pfs,
					 struct prefix *input,
					 int prefix_check);

/* 【函数】bgp_flowspec_get_first_nh —— 取这条 FlowSpec 路径的**首个下一跳**。
 * 成功时把结果写进出参 nh 并返回 true。名字里的 "first" 是因为前缀可能由多个
 * 部分构成（多路径/多段），这个接口只取最前面那一个，不返回全部。 */
extern bool bgp_flowspec_get_first_nh(struct bgp *bgp, struct bgp_path_info *pi,
				      struct prefix *nh, afi_t afi);

#endif /* _FRR_BGP_FLOWSPEC_UTIL_H */
