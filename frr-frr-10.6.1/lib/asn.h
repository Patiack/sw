// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AS number structure
 * Copyright 2022 6WIND
 */

/* ============================================================================
 * 【文件导读】asn.h —— 自治系统号(AS Number)的表示与格式化接口
 * ============================================================================
 * 【这个文件干什么】定义 AS 号的类型别名、书写风格(asnotation)枚举，以及
 * 字符串与 AS 号互转、多种格式化的对外接口。FRR 各协议(bgpd 等)解析、显示
 * AS 号都走这里，保证全系统对 AS 号的解析与显示口径一致。
 *
 * 【核心概念】AS 号按 RFC 既可用 asplain(纯十进制，如 65536)，也可用
 * asdot(点分写法，如 1.1)表示。asdot 中 1.1 等于 1 乘 65536 再加 1，即 65536。
 * 枚举 asnotation_mode 描述“本次要用哪种风格”：
 *   ASNOTATION_PLAIN    —— 纯十进制
 *   ASNOTATION_DOT      —— 点分写法(高位非零)
 *   ASNOTATION_DOTPLUS  —— 点分写法(高位为零时的 0.x 形式)
 *   ASNOTATION_UNDEFINED —— 未定义
 *
 * 【关键数据类型】as_t 就是 uint32_t，是全模块统一的 AS 号内存表示。
 *
 * 【主要接口分组】
 *   解析：asn_str2asn / asn_str2asn_parse / asn_str2asn_match /
 *         asn_str2asn_notation
 *   格式化：asn_asn2asplain / asn_asn2string / asn_asn2json /
 *           asn_asn2json_array / asn_mode2str
 *   打印扩展：%pASP(asplain) / %pASD(asdot) / %pASE(asdot-plus)，
 *             配合 ASN_FORMAT 宏供各调用点挑选格式。
 *
 * 【与其他模块的关系】asn.c 是本文件的实现；command_match.h 提供 match_type
 * (命令补全时的匹配结果)，json 相关头文件提供 JSON 输出能力。
 * 【阅读顺序】类型与枚举 → 解析接口 → 格式化接口 → printfrr 打印扩展宏。
 * ============================================================================
 */
#ifndef _FRR_ASN_H
#define _FRR_ASN_H

#include "zebra.h"
#include "command_match.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 【宏】AS 号字符串缓冲的最大长度。
 * %u 形式的 uint32 最多 10 位数字，加结尾 NUL 共 11 字节，12 字节足够容纳
 * asplain 写法；调用方按此尺寸开栈/静态缓冲即可覆盖所有场景。 */
#define ASN_STRING_MAX_SIZE	12

/* 【枚举】AS 号的书写风格(notation)。决定 asn.c 各格式化函数输出成
 * 纯十进制、点分，还是点分(高位为零的 0.x)形式。取值含义见文件导读。 */
enum asnotation_mode {
	ASNOTATION_PLAIN = 0,
	ASNOTATION_DOT,
	ASNOTATION_DOTPLUS,
	ASNOTATION_UNDEFINED,
};

/* 【类型别名】AS 号的统一内存表示：无符号 32 位整数。
 * 全系统传递 AS 号一律用 as_t，避免各处混用不同宽度导致截断。 */
typedef uint32_t as_t;

/* 【解析接口】把字符串解析成 as_t 形式的 AS 号。
 *   asn_str2asn         : 完整解析一个 AS 号串，成功返回 true，否则 false。
 *   asn_str2asn_parse   : 解析一个前缀，返回指向剩余字符串的指针，便于从一
 *                         长串里连续取出多个 AS 号；是否解析成功经 found_ptr 回填。
 *   asn_str2asn_match   : 命令补全用，返回 match_type(exact / partly / no_match)。
 *   asn_str2asn_notation: 解析 AS 号的同时，回填识别出的书写风格。
 * 【易踩的坑】asn_str2asn_parse 返回的是“剩余串”指针，不是解析结果本身；
 * 结果写入调用方传入的 asn 指针。 */
extern bool asn_str2asn(const char *asstring, as_t *asn);
extern const char *asn_asn2asplain(as_t asn);
extern const char *asn_str2asn_parse(const char *asstring, as_t *asn,
				     bool *found_ptr);
extern enum match_type asn_str2asn_match(const char *str);
extern bool asn_str2asn_notation(const char *asstring, as_t *asn,
				 enum asnotation_mode *asnotation);
extern const char *asn_mode2str(enum asnotation_mode asnotation);

/* 【前置声明】FRR 用 json-c 的 json_object 输出 JSON；这里只前置声明指针
 * 类型，避免在头文件里 include 整个 json 头文件造成依赖扩散。 */
struct json_object;
/* 【JSON 输出接口】把 AS 号写入 JSON。
 *   asn_asn2json_array: 追加(数组元素)到一个 JSON 数组 jseg_list。
 *   asn_asn2json      : 以属性名 attr 写入一个 JSON 对象。
 * 两者都会依 asnotation 决定：能安全用十进制表示时写字面整数，否则退回点分
 * 字符串，从而保证 JSON 消费者拿到的语义与显示风格一致。 */
void asn_asn2json_array(struct json_object *jseg_list, as_t asn,
			enum asnotation_mode asnotation);
void asn_asn2json(struct json_object *jseg_list, const char *attr, as_t asn,
		  enum asnotation_mode asnotation);
/* 【格式化接口】把 as_t 写入调用方提供的 buf(长度 len)，按 asnotation 决定
 * 用十进制还是点分写法，返回 buf 本身便于链式使用。
 * 【易踩的坑】buf 由调用方保证足够大(至少 ASN_STRING_MAX_SIZE)，本函数不做
 * 越界保护；传入的 as 是“指向 as_t 的指针”，不是 as_t 本身。 */
extern char *asn_asn2string(const as_t *as, char *buf, size_t len,
			    enum asnotation_mode asnotation);
/* 【打印扩展注册】为 printfrr 注册三个 AS 号格式说明符(见下方 %pASP / %pASD /
 * %pASE)，由 _FRR_ATTRIBUTE_PRINTFRR 宏在支持时展开为 #pragma。 */
/* display AS in appropriate format */
#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pASP"  (as_t *)
#pragma FRR printfrr_ext "%pASD"  (as_t *)
#pragma FRR printfrr_ext "%pASE"  (as_t *)
#endif

/* 【宏】按书写风格 mode 选用 printfrr 的 AS 号格式串：
 * 点分用 %pASD，点分-plus 用 %pASE，其余(含 PLAIN 与 UNDEFINED)用 %pASP。 */
#define ASN_FORMAT(mode)  \
	((mode == ASNOTATION_DOT) ? "%pASD" :	    \
	 ((mode == ASNOTATION_DOTPLUS) ? "%pASE" :	\
	  "%pASP"))
/* 【宏】同 ASN_FORMAT，但用于表格打印对齐：把宽度固定为 11 列(%11pAS*)，
 * 使多行 AS 号纵向对齐。 */
#define ASN_FORMAT_SPACE(mode)                                                 \
	((mode == ASNOTATION_DOT)                                              \
		 ? "%11pASD"                                                   \
		 : ((mode == ASNOTATION_DOTPLUS) ? "%11pASE" : "%11pASP"))

/* 【测试辅助】开关“是否放宽 AS 0 与 AS 0.0 的合法性校验”。
 * 默认为 false(禁止 0 号)；个别单元测试需要构造 0 号时才临时打开。仅测试
 * 代码使用，生产路径切勿调用。 */
/* for test */
extern void asn_relax_as_zero(bool relax);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_ASN_H */
