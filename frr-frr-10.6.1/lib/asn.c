// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ASN functions
 *
 * Copyright 2022 6WIND
 */
/* ============================================================================
 * 【文件导读】asn.c —— AS 号的解析与格式化实现
 * ============================================================================
 * 【这个文件干什么】实现 asn.h 声明的接口：把字符串解析成 32 位 AS 号，以及
 * 把 AS 号格式化成 asplain / asdot / dot-plus 三种写法；并注册 printfrr 打印
 * 扩展 %pASP / %pASD / %pASE。
 *
 * 【统一入口 asn_str2asn_internal】所有解析最终都走这一个静态函数，它同时
 * 负责：识别 PLAIN 与 DOT 两种输入形态、做溢出/前导零/高位范围的合法性检查、
 * 报告“只解析了部分”(partial，供命令补全判断 partly_match)，以及回填识别出
 * 的 asnotation。理解它是理解整个文件的关键。
 *
 * 【asdot 的位运算约定】asdot 的“高位.低位”中：高位占高半部(asn 右移 16 位)，
 * 低位占低半部(asn 与 0xffff)。因此 1.1 表示 65536。
 *
 * 【全局状态】relax_as_zero 是唯一的模块级可变状态，默认拒绝 AS 0，仅测试用
 * 开关(见 asn_relax_as_zero)。
 *
 * 【阅读顺序】asnotation_mode_msg → asn_str2asn_internal(核心) → 各薄封装
 * (asn_str2asn / _parse / _match / _notation) → 格式化函数 → printfrr 扩展。
 * ============================================================================
 */
#include <zebra.h>
#include "log.h"
#include "asn.h"
#include "lib/json.h"

/* 【全局状态】是否放宽 AS 0 与 AS 0.0 的校验，默认 false(视为非法)。
 * 仅由 asn_relax_as_zero() 修改，供测试构造边界用例。 */
static bool relax_as_zero;

/* 【查表数据】asnotation_mode 到可读字符串的映射表，配合 lib/log.h 的
 * lookup_msg() 使用；末尾以 {0} 作为哨兵结束，最后一项是 UNDEFINED。 */
static const struct message asnotation_mode_msg[] = {
	{ASNOTATION_PLAIN, "plain"},
	{ASNOTATION_DOT, "dot"},
	{ASNOTATION_DOTPLUS, "dot+"},
	{ASNOTATION_UNDEFINED, "undefined"},
	{0}
};

/* converts a string into an Autonomous system number
 * "1.1" => 65536
 * "65500" => 65500
 */
/* 【核心解析器】把 asstring 起始处的 AS 号解析出来，是全模块唯一解析入口。
 *   asstring : 待解析字符串(可为 NULL)。
 *   asn      : 输出参数，写回解析结果；传 NULL 表示只做合法性校验不取值。
 *   next     : 输出参数，写回“解析结束处”的指针(剩余串)；传 NULL 则要求整
 *              串被完全消耗，否则视为不合法。
 *   partial  : 输出参数，标记“只匹配了一部分”(如输入 "1."，或非 relax 下的
 *              "0" 与 "0.0")，命令补全据此返回 partly_match。
 *   mode     : 输出参数，回填本次识别出的 asnotation(PLAIN / DOT / DOTPLUS)。
 *   返回     : 解析成功 true，失败 false。
 * 【允许的写法】"65500"(PLAIN)、"1.1"(DOT，值 65536)、"0.x"(DOTPLUS，需 relax)。
 * 【拒绝的写法】空或非数字开头、前导零(如 01)、溢出(高 16 位超 UINT32、
 *   低 16 位超 UINT16)、点分高位超 16 位(AS 大于 65535)，以及非 relax 下的
 *   AS 0 与 0.0。
 * 【易踩的坑】无论成功失败，next 与 mode 都会被写回(失败时 mode 为 PLAIN)，
 * 调用方需结合返回值判断其有效性。 */
static bool asn_str2asn_internal(const char *asstring, as_t *asn,
				 const char **next, bool *partial,
				 enum asnotation_mode *mode)
{
	uint32_t high = 0, low = 0;
	uint64_t temp_val;
	const char *p = asstring;
	bool ret = false;
	uint32_t digit;
	enum asnotation_mode val = ASNOTATION_PLAIN;

	if (!asstring)
		goto end;

	if  (!isdigit((unsigned char)*p))
		goto end;

	/* leading zero is forbidden */
	if (*p == '0' && isdigit((unsigned char)*(p + 1)))
		goto end;

	temp_val = 0;
	while (isdigit((unsigned char)*p)) {
		digit = (*p) - '0';
		temp_val *= 10;
		temp_val += digit;
		if (temp_val > UINT32_MAX)
			/* overflow */
			goto end;
		p++;
	}
	high = (uint32_t)temp_val;
	if (*p == '.') { /* dot format */
		p++;

		if (*p == '\0' && partial) {
			*partial = true;
			goto end;
		}

		/* leading zero is forbidden */
		if (*p == '0' && isdigit((unsigned char)*(p + 1)))
			goto end;

		temp_val = 0;
		while (isdigit((unsigned char)*p)) {
			digit = (*p) - '0';
			temp_val *= 10;
			temp_val += digit;
			if (temp_val > UINT16_MAX)
				/* overflow */
				goto end;
			p++;
		}
		low = (uint32_t)temp_val;

		if (!next && *p != '\0' && !isdigit((unsigned char)*p))
			goto end;
		/* AS <AS4B>.<AS4B> is forbidden */
		if (high > UINT16_MAX)
			goto end;
		/* AS 0.0 is authorised for some case only */
		if (!relax_as_zero && high == 0 && low == 0) {
			if (partial)
				*partial = true;
			goto end;
		}
		if (asn)
			*asn = (high << 16) + low;
		ret = true;
		if (high == 0)
			val = ASNOTATION_DOTPLUS;
		else
			val = ASNOTATION_DOT;
		goto end;
	}
	/* AS 0 is forbidden */
	if (!relax_as_zero && high == 0)
		goto end;
	if (!asn) {
		ret = true;
		goto end;
	}
	*asn = high;
	ret = true;
 end:
	if (next)
		*next = p;
	if (mode)
		*mode = val;
	return ret;
}

/* 【格式化】把 asn 写成点分字符串“高 16 位.低 16 位”，结果写进调用方的
 * asstring(长度 len)。本函数不做范围校验，纯位运算拆分。 */
static void asn_asn2asdot(as_t asn, char *asstring, size_t len)
{
	uint16_t low, high;

	high = (asn >> 16) & 0xffff;
	low = asn & 0xffff;
	snprintf(asstring, len, "%hu.%hu", high, low);
}

/* 【薄封装】asn_str2asn_internal 的最简形式：要求整串被完整解析，且不需要
 * 部分匹配信息与 notation。成功返回 true 并把结果写入 asn。 */
bool asn_str2asn(const char *asstring, as_t *asn)
{
	return asn_str2asn_internal(asstring, asn, NULL, NULL, NULL);
}

/* 【格式化】把 asn 写成纯十进制字符串。
 * 【易踩的坑】返回值指向函数内的 static 缓冲 buf，非线程安全；也不要在两次
 * 调用之间保存该返回值，下一次调用会覆盖同一块内存。 */
const char *asn_asn2asplain(as_t asn)
{
	static char buf[ASN_STRING_MAX_SIZE];

	snprintf(buf, sizeof(buf), "%u", asn);
	return buf;
}

/* 【解析前缀】解析 asstring 起始处的 AS 号，返回指向剩余字符串的指针。
 *   asn       : 输出参数，写回解析结果。
 *   found_ptr : 输出参数，写回是否解析成功；可为 NULL。
 *   返回      : 剩余字符串指针(解析结束处)。即便解析失败也会返回指针，务必
 *               用 found_ptr 判断是否真的解析到了一个 AS 号。 */
const char *asn_str2asn_parse(const char *asstring, as_t *asn, bool *found_ptr)
{
	const char *p = NULL;
	const char **next = &p;
	bool found;

	found = asn_str2asn_internal(asstring, asn, next, NULL, NULL);
	if (found_ptr)
		*found_ptr = found;
	return *next;
}

/* 【测试辅助】设置 relax_as_zero 开关：true 允许 AS 0 与 0.0，false 恢复
 * 默认拒绝。仅供单元测试，生产路径不要调用。 */
void asn_relax_as_zero(bool relax)
{
	relax_as_zero = relax;
}

/* 【命令补全】判断 str 与一个 AS 号的匹配程度，供 CLI 补全使用。
 *   exact_match : 完整且合法地解析出一个 AS 号。
 *   partly_match: 只匹配了前缀(如 "1.")，或非 relax 下的 "0" 与 "0.0"(输入
 *                 还可能继续补全成合法值)。
 *   no_match    : 完全不像 AS 号。
 * 【易踩的坑】这里始终以“非 relax”视角判断——partial 时即便字面合法也报
 * partly_match，以驱动补全继续。 */
enum match_type asn_str2asn_match(const char *str)
{
	bool found, partial = false;

	found = asn_str2asn_internal(str, NULL, NULL, &partial, NULL);
	if (found && !partial)
		return exact_match;

	if (partial)
		return partly_match;

	return no_match;
}

/* 【解析并识别风格】解析 AS 号并把识别出的 asnotation 回填给调用方，便于
 * 上层决定后续按哪种写法显示。成功返回 true。 */
bool asn_str2asn_notation(const char *asstring, as_t *asn,
			  enum asnotation_mode *asnotation)
{
	return asn_str2asn_internal(asstring, asn, NULL, NULL, asnotation);
}

/* 【查表】把 asnotation_mode 转成可读字符串(见 asnotation_mode_msg)；
 * 未登记的取值返回后备文案 "Unrecognized AS notation mode"。 */
const char *asn_mode2str(enum asnotation_mode asnotation)
{
	return lookup_msg(asnotation_mode_msg, asnotation,
			  "Unrecognized AS notation mode");
}

/* 【JSON 输出】把一个 AS 号作为属性写入 JSON 对象。
 *   条件：当 asnotation 为 PLAIN，或为 DOT 且 asn 小于 UINT16_MAX 时，写成
 *   JSON 整数；否则写成点分字符串，避免语义丢失。
 * 【易踩的坑】拼点分串用 static 缓冲，非线程安全。 */
void asn_asn2json(json_object *json, const char *attr,
		  as_t asn, enum asnotation_mode asnotation)
{
	static char as_str[ASN_STRING_MAX_SIZE];

	if ((asnotation == ASNOTATION_PLAIN) ||
	    ((asnotation == ASNOTATION_DOT) && asn < UINT16_MAX))
		json_object_int_add(json, attr, asn);
	else {
		asn_asn2asdot(asn, as_str, sizeof(as_str));
		json_object_string_add(json, attr, as_str);
	}
}

/* 【JSON 输出】把 AS 号追加到 JSON 数组 jseg_list。取值判定同上：能安全用
 * 整数表示时用 int64，否则用点分字符串。 */
void asn_asn2json_array(json_object *jseg_list, as_t asn,
			enum asnotation_mode asnotation)
{
	static char as_str[ASN_STRING_MAX_SIZE];

	if ((asnotation == ASNOTATION_PLAIN) ||
	    ((asnotation == ASNOTATION_DOT) && asn < UINT16_MAX))
		json_object_array_add(jseg_list,
				      json_object_new_int64(asn));
	else {
		asn_asn2asdot(asn, as_str, sizeof(as_str));
		json_array_string_add(jseg_list, as_str);
	}
}

/* 【格式化】把 asn 指向的 AS 号按 asnotation 写入 buf(长度 len)，返回 buf。
 *   条件同 JSON 输出：PLAIN，或 DOT 且值小于 UINT16_MAX 时用十进制，否则点分。
 * 【易踩的坑】buf 需由调用方保证空间充足；参数 asn 是指针而非值。 */
char *asn_asn2string(const as_t *asn, char *buf, size_t len,
		     enum asnotation_mode asnotation)
{
	if ((asnotation == ASNOTATION_PLAIN) ||
	    ((asnotation == ASNOTATION_DOT) && *asn < UINT16_MAX))
		snprintf(buf, len, "%u", *asn);
	else
		asn_asn2asdot(*asn, buf, len);
	return buf;
}

/* 【printfrr 内部实现】三个 %pAS* 说明符的公共实现：空指针输出 "(null)"，
 * 否则按指定 asnotation 格式化到栈缓冲再交给 printfrr。
 *   buf / ea : printfrr 框架的输出缓冲与转义参数。
 *   ptr      : 指向 as_t 的指针(约定 %p 扩展传入的均是 as_t*)。
 *   asnotation: 目标写法。
 *   返回     : bputs 写入的字节数。
 * 【易踩的坑】as_str 开了三倍缓冲(ASN_STRING_MAX_SIZE 乘 3)，仅为满足
 * “%33pASD”这类宽度对齐标记的打印需求，不代表 AS 号真有那么长。 */
static ssize_t printfrr_asnotation(struct fbuf *buf, struct printfrr_eargs *ea,
				   const void *ptr,
				   enum asnotation_mode asnotation)
{
	/* for alignemnt up to 33 chars - %33pASD for instance - */
	char as_str[ASN_STRING_MAX_SIZE*3];
	const as_t *asn;

	if (!ptr)
		return bputs(buf, "(null)");
	asn = ptr;
	asn_asn2string(asn, as_str, sizeof(as_str), asnotation);
	return bputs(buf, as_str);
}

/* 【printfrr 扩展】注册说明符 %pASP，按 asplain(纯十进制)打印 AS 号。
 * 宏 printfrr_ext_autoreg_p 把下面的打印函数登记到 printfrr 框架。 */
printfrr_ext_autoreg_p("ASP", printfrr_asplain);
static ssize_t printfrr_asplain(struct fbuf *buf, struct printfrr_eargs *ea,
				const void *ptr)
{
	return printfrr_asnotation(buf, ea, ptr, ASNOTATION_PLAIN);
}

/* 【printfrr 扩展】注册说明符 %pASD，按 asdot(点分)打印 AS 号。 */
printfrr_ext_autoreg_p("ASD", printfrr_asdot);
static ssize_t printfrr_asdot(struct fbuf *buf, struct printfrr_eargs *ea,
				const void *ptr)
{
	return printfrr_asnotation(buf, ea, ptr, ASNOTATION_DOT);
}

/* 【printfrr 扩展】注册说明符 %pASE，按 asdot-plus(高位为零的 0.x 形式)
 * 打印 AS 号。 */
printfrr_ext_autoreg_p("ASE", printfrr_asdotplus);
static ssize_t printfrr_asdotplus(struct fbuf *buf, struct printfrr_eargs *ea,
				  const void *ptr)
{
	return printfrr_asnotation(buf, ea, ptr, ASNOTATION_DOTPLUS);
}
