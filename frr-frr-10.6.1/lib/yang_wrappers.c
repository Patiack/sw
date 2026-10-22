// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * 文件导读：取值 与 设值便捷包装实现（yang_wrappers.c）
 *
 * 本文件实现 yang_wrappers.h 声明的便捷包装：把 libyang 的值读取、类型转换、
 * 字符串解析等细节封装成按类型分类的小函数，是 northbound 与各守护进程最常用的
 * 取值入口。它建立在 yang.c 通用接口之上，属于“取值 与 设值”这一层。
 *
 * 统一的“四件套”模式（每种类型都有）：
 *   yang_str2 加类型名     把字符串解析成该类型的值
 *   yang_data_new_类型名   把该类型的值包装成 yang_data
 *   yang_dnode_get_类型名  按 xpath 从数据树取值（内部走两个辅助函数）
 *   yang_get_default_类型名 取 schema 上声明的默认值并转成该类型
 *
 * 两个辅助函数 yang_dnode_xpath_get_value 与 yang_dnode_xpath_get_canon 负责处理
 * 可变参数路径与取值定位；宏 YANG_DNODE_GET_VALUE 与 YANG_DNODE_GET_CANON 把那段
 * 样板代码收纳起来，避免每个 getter 重复书写。
 *
 * 重要提醒：这些 getter 在取不到值时往往直接断言或 abort，属于“取不到就崩”的约定，
 * 调用方必须先确保路径一定存在。
 */
#include <zebra.h>

#include "base64.h"
#include "log.h"
#include "lib_errors.h"
#include "northbound.h"
#include "printfrr.h"
#include "nexthop.h"
#include "printfrr.h"


/*
 * YANG_DNODE_GET_ASSERT - 取值为空时的统一失败处理：记错误、打回溯、abort。
 * 用于 getter 内部，表示“本应存在的节点却取不到”，属于不可恢复的编程错误。
 */
#define YANG_DNODE_GET_ASSERT(dnode, xpath)                                    \
	do {                                                                   \
		if ((dnode) == NULL) {                                         \
			flog_err(EC_LIB_YANG_DNODE_NOT_FOUND,                  \
				 "%s: couldn't find %s", __func__, (xpath));   \
			zlog_backtrace(LOG_ERR);                               \
			abort();                                               \
		}                                                              \
	} while (0)

/* Wrappers for repetitive varargs stanzas */
/*
 * YANG_DNODE_GET_VALUE - 收纳 getter 里启动可变参数、调用取值辅助、结束可变参数的样板。
 * 展开后声明 dvalue 变量并完成赋值，因此使用它的函数里可直接引用 dvalue。
 */
#define YANG_DNODE_GET_VALUE                                                   \
	va_list ap;                                                            \
                                                                               \
	va_start(ap, xpath_fmt);                                               \
	dvalue = yang_dnode_xpath_get_value(dnode, xpath_fmt, ap);             \
	va_end(ap);


/*
 * YANG_DNODE_GET_CANON - 与 YANG_DNODE_GET_VALUE 类似，但取的是规范化字符串 canon。
 */
#define YANG_DNODE_GET_CANON                                                   \
	va_list ap;                                                            \
                                                                               \
	va_start(ap, xpath_fmt);                                               \
	canon = yang_dnode_xpath_get_canon(dnode, xpath_fmt, ap);              \
	va_end(ap);


PRINTFRR(2, 0)
static inline const char *
/*
 * yang_dnode_xpath_get_canon - 取叶子节点的规范化字符串值。
 * @xpath_fmt：可为 NULL（此时直接用 dnode）；非 NULL 则先按其定位到叶子。
 * 返回：规范值字符串；定位失败会 abort。
 */
yang_dnode_xpath_get_canon(const struct lyd_node *dnode, const char *xpath_fmt,
			   va_list ap)
{
	const struct lyd_node_term *dleaf = (const struct lyd_node_term *)dnode;

	assert(dleaf);
	if (xpath_fmt) {
		char xpath[XPATH_MAXLEN];

		vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
		dleaf = (const struct lyd_node_term *)yang_dnode_get(dnode, xpath);
		YANG_DNODE_GET_ASSERT(dleaf, xpath);
	}
	return lyd_get_value(&dleaf->node);
}

PRINTFRR(2, 0)
static inline const struct lyd_value *
/*
 * yang_dnode_xpath_get_value - 取叶子节点的 lyd_value。
 * 若是 union 类型，会深入一层取实际子类型的值。
 * 返回：值结构指针；定位失败会 abort。
 */
yang_dnode_xpath_get_value(const struct lyd_node *dnode, const char *xpath_fmt,
			   va_list ap)
{
	const struct lyd_node_term *dleaf = (const struct lyd_node_term *)dnode;
	const struct lyd_value *dvalue;

	assert(dleaf);
	if (xpath_fmt) {
		char _xpath[XPATH_MAXLEN];

		vsnprintf(_xpath, sizeof(_xpath), xpath_fmt, ap);
		dleaf = (const struct lyd_node_term *)yang_dnode_get(dnode, _xpath);
		YANG_DNODE_GET_ASSERT(dleaf, _xpath);
	}
	dvalue = &dleaf->value;

	if (dvalue->realtype->basetype == LY_TYPE_UNION)
		dvalue = &dvalue->subvalue->value;
	return dvalue;
}

/*
 * yang_get_default_value - 按 xpath 取 schema 上声明的默认值字符串。
 * 路径未知或没有默认值时打印错误并 abort。
 */
static const char *yang_get_default_value(const char *xpath)
{
	const struct lysc_node *snode;
	const char *value;

	snode = yang_find_snode(ly_native_ctx, xpath, 0);
	if (snode == NULL) {
		flog_err(EC_LIB_YANG_UNKNOWN_DATA_PATH,
			 "%s: unknown data path: %s", __func__, xpath);
		zlog_backtrace(LOG_ERR);
		abort();
	}

	value = yang_snode_get_default(snode);
	assert(value);

	return value;
}

/*
 * Primitive type: bool.
 */
/*
 * yang_str2bool - 把字符串解析成 bool（等于 "true" 即为真）。
 */
bool yang_str2bool(const char *value)
{
	return strmatch(value, "true");
}

/*
 * yang_data_new_bool - 把 bool 值包装成 yang_data（转成 "true" 或 "false"）。
 */
struct yang_data *yang_data_new_bool(const char *xpath, bool value)
{
	return yang_data_new(xpath, (value) ? "true" : "false");
}

/*
 * yang_dnode_get_bool - 按 xpath 从数据树取 bool 值；断言底层类型必须是 BOOL。
 */
bool yang_dnode_get_bool(const struct lyd_node *dnode, const char *xpath_fmt,
			 ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_BOOL);

	return dvalue->boolean;
}

/*
 * yang_get_default_bool - 取 xpath 处 schema 的默认值并解析为 bool。
 */
bool yang_get_default_bool(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2bool(value);
}

/*
 * Primitive type: dec64.
 */
/*
 * yang_str2dec64 - 把字符串解析成 dec64（double）；解析失败打印错误并 abort。
 */
double yang_str2dec64(const char *xpath, const char *value)
{
	double dbl = 0;

	if (sscanf(value, "%lf", &dbl) != 1) {
		flog_err(EC_LIB_YANG_DATA_CONVERT,
			 "%s: couldn't convert string to decimal64 [xpath %s]",
			 __func__, xpath);
		zlog_backtrace(LOG_ERR);
		abort();
	}

	return dbl;
}

/*
 * yang_data_new_dec64 - 把 double 值格式化成字符串并包装成 yang_data。
 */
struct yang_data *yang_data_new_dec64(const char *xpath, double value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%lf", value);
	return yang_data_new(xpath, value_str);
}

/*
 * yang_dnode_get_dec64 - 按 xpath 取 dec64 值。
 * dec64 在 libyang 中以整数存储，需按 fraction_digits 除以 10 的幂还原为小数。
 */
double yang_dnode_get_dec64(const struct lyd_node *dnode, const char *xpath_fmt,
			    ...)
{
	const double denom[19] = { 1e0,	 1e1,  1e2,  1e3,  1e4,	 1e5,  1e6,
				   1e7,	 1e8,  1e9,  1e10, 1e11, 1e12, 1e13,
				   1e14, 1e15, 1e16, 1e17, 1e18 };
	const struct lysc_type_dec *dectype;
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	dectype = (const struct lysc_type_dec *)dvalue->realtype;
	assert(dectype->basetype == LY_TYPE_DEC64);
	assert(dectype->fraction_digits < sizeof(denom) / sizeof(*denom));
	return (double)dvalue->dec64 / denom[dectype->fraction_digits];
}

/*
 * yang_get_default_dec64 - 取默认值并解析为 dec64。
 */
double yang_get_default_dec64(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2dec64(xpath, value);
}

/*
 * Primitive type: enum.
 */
/*
 * yang_str2enum - 按 xpath 定位枚举类型，把字符串枚举名解析成其整数值。
 * 找不到匹配的枚举名时打印错误并 abort。
 */
int yang_str2enum(const char *xpath, const char *value)
{
	const struct lysc_node *snode;
	const struct lysc_node_leaf *sleaf;
	const struct lysc_type_enum *type;
	const struct lysc_type_bitenum_item *enums;

	snode = yang_find_snode(ly_native_ctx, xpath, 0);
	if (snode == NULL) {
		flog_err(EC_LIB_YANG_UNKNOWN_DATA_PATH,
			 "%s: unknown data path: %s", __func__, xpath);
		zlog_backtrace(LOG_ERR);
		abort();
	}

	assert(snode->nodetype == LYS_LEAF);
	sleaf = (const struct lysc_node_leaf *)snode;
	type = (const struct lysc_type_enum *)sleaf->type;
	assert(type->basetype == LY_TYPE_ENUM);
	enums = type->enums;
	unsigned int count = LY_ARRAY_COUNT(enums);
	for (unsigned int i = 0; i < count; i++) {
		if (strmatch(value, enums[i].name)) {
			assert(CHECK_FLAG(enums[i].flags, LYS_SET_VALUE));
			return enums[i].value;
		}
	}

	flog_err(EC_LIB_YANG_DATA_CONVERT,
		 "%s: couldn't convert string to enum [xpath %s]", __func__,
		 xpath);
	zlog_backtrace(LOG_ERR);
	abort();
}

/*
 * yang_data_new_enum - 按 xpath 定位枚举类型，把整数值转回枚举名并包装成 yang_data。
 */
struct yang_data *yang_data_new_enum(const char *xpath, int value)
{
	const struct lysc_node *snode;
	const struct lysc_node_leaf *sleaf;
	const struct lysc_type_enum *type;
	const struct lysc_type_bitenum_item *enums;

	snode = yang_find_snode(ly_native_ctx, xpath, 0);
	if (snode == NULL) {
		flog_err(EC_LIB_YANG_UNKNOWN_DATA_PATH,
			 "%s: unknown data path: %s", __func__, xpath);
		zlog_backtrace(LOG_ERR);
		abort();
	}

	assert(snode->nodetype == LYS_LEAF);
	sleaf = (const struct lysc_node_leaf *)snode;
	type = (const struct lysc_type_enum *)sleaf->type;
	assert(type->basetype == LY_TYPE_ENUM);
	enums = type->enums;
	unsigned int count = LY_ARRAY_COUNT(enums);
	for (unsigned int i = 0; i < count; i++) {
		if (CHECK_FLAG(enums[i].flags, LYS_SET_VALUE)
		    && value == enums[i].value)
			return yang_data_new(xpath, enums[i].name);
	}

	flog_err(EC_LIB_YANG_DATA_CONVERT,
		 "%s: couldn't convert enum to string [xpath %s]", __func__,
		 xpath);
	zlog_backtrace(LOG_ERR);
	abort();
}

/*
 * yang_dnode_get_enum - 按 xpath 取枚举值（返回内部整数值）；断言类型为 ENUM 且有显式取值。
 */
int yang_dnode_get_enum(const struct lyd_node *dnode, const char *xpath_fmt,
			...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_ENUM);
	assert(dvalue->enum_item->flags & LYS_SET_VALUE);
	return dvalue->enum_item->value;
}

/*
 * yang_get_default_enum - 取默认值并解析为枚举值。
 */
int yang_get_default_enum(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2enum(xpath, value);
}

/*
 * Primitive type: int8.
 */
/*
 * int8 类型包装（上面 Primitive type: int8 一节）四件套：str2 解析、data_new 构造、
 * dnode_get 取值、get_default 取默认值。
 */
int8_t yang_str2int8(const char *value)
{
	return strtol(value, NULL, 10);
}

struct yang_data *yang_data_new_int8(const char *xpath, int8_t value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%d", value);
	return yang_data_new(xpath, value_str);
}

int8_t yang_dnode_get_int8(const struct lyd_node *dnode, const char *xpath_fmt,
			   ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_INT8);
	return dvalue->int8;
}

int8_t yang_get_default_int8(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2int8(value);
}

/*
 * Primitive type: int16.
 */
/*
 * int16 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
int16_t yang_str2int16(const char *value)
{
	return strtol(value, NULL, 10);
}

struct yang_data *yang_data_new_int16(const char *xpath, int16_t value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%d", value);
	return yang_data_new(xpath, value_str);
}

int16_t yang_dnode_get_int16(const struct lyd_node *dnode,
			     const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_INT16);
	return dvalue->int16;
}

int16_t yang_get_default_int16(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2int16(value);
}

/*
 * Primitive type: int32.
 */
/*
 * int32 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
int32_t yang_str2int32(const char *value)
{
	return strtol(value, NULL, 10);
}

struct yang_data *yang_data_new_int32(const char *xpath, int32_t value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%d", value);
	return yang_data_new(xpath, value_str);
}

int32_t yang_dnode_get_int32(const struct lyd_node *dnode,
			     const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_INT32);
	return dvalue->int32;
}

int32_t yang_get_default_int32(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2int32(value);
}

/*
 * Primitive type: int64.
 */
/*
 * int64 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
int64_t yang_str2int64(const char *value)
{
	return strtoll(value, NULL, 10);
}

struct yang_data *yang_data_new_int64(const char *xpath, int64_t value)
{
	char value_str[BUFSIZ];

	snprintfrr(value_str, sizeof(value_str), "%" PRId64, value);
	return yang_data_new(xpath, value_str);
}

int64_t yang_dnode_get_int64(const struct lyd_node *dnode,
			     const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_INT64);
	return dvalue->int64;
}

int64_t yang_get_default_int64(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2int64(value);
}

/*
 * Primitive type: uint8.
 */
/*
 * uint8 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
uint8_t yang_str2uint8(const char *value)
{
	return strtoul(value, NULL, 10);
}

struct yang_data *yang_data_new_uint8(const char *xpath, uint8_t value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%u", value);
	return yang_data_new(xpath, value_str);
}

uint8_t yang_dnode_get_uint8(const struct lyd_node *dnode,
			     const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_UINT8);
	return dvalue->uint8;
}

uint8_t yang_get_default_uint8(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2uint8(value);
}

/*
 * Primitive type: uint16.
 */
/*
 * uint16 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
uint16_t yang_str2uint16(const char *value)
{
	return strtoul(value, NULL, 10);
}

struct yang_data *yang_data_new_uint16(const char *xpath, uint16_t value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%u", value);
	return yang_data_new(xpath, value_str);
}

uint16_t yang_dnode_get_uint16(const struct lyd_node *dnode,
			       const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_UINT16);
	return dvalue->uint16;
}

uint16_t yang_get_default_uint16(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2uint16(value);
}

/*
 * Primitive type: uint32.
 */
/*
 * uint32 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
uint32_t yang_str2uint32(const char *value)
{
	return strtoul(value, NULL, 10);
}

struct yang_data *yang_data_new_uint32(const char *xpath, uint32_t value)
{
	char value_str[BUFSIZ];

	snprintf(value_str, sizeof(value_str), "%u", value);
	return yang_data_new(xpath, value_str);
}

uint32_t yang_dnode_get_uint32(const struct lyd_node *dnode,
			       const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_UINT32);
	return dvalue->uint32;
}

uint32_t yang_get_default_uint32(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2uint32(value);
}

/*
 * Primitive type: uint64.
 */
/*
 * uint64 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
uint64_t yang_str2uint64(const char *value)
{
	return strtoull(value, NULL, 10);
}

struct yang_data *yang_data_new_uint64(const char *xpath, uint64_t value)
{
	char value_str[BUFSIZ];

	snprintfrr(value_str, sizeof(value_str), "%" PRIu64, value);
	return yang_data_new(xpath, value_str);
}

uint64_t yang_dnode_get_uint64(const struct lyd_node *dnode,
			       const char *xpath_fmt, ...)
{
	const struct lyd_value *dvalue;

	YANG_DNODE_GET_VALUE;

	assert(dvalue->realtype->basetype == LY_TYPE_UINT64);
	return dvalue->uint64;
}

uint64_t yang_get_default_uint64(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	return yang_str2uint64(value);
}

/*
 * Primitive type: string.
 *
 * All string wrappers can be used with non-string types.
 */
/*
 * string 类型包装：data_new_string 构造、dnode_get_string 取值（返回库内字符串指针）、
 * dnode_get_string_buf 取到调用方缓冲、get_default_string 与 get_default_string_buf 取默认值。
 * 注意：这些包装对任意类型都可用（取的是规范化字符串），dnode_get_string 的返回值不必释放。
 */
struct yang_data *yang_data_new_string(const char *xpath, const char *value)
{
	return yang_data_new(xpath, value);
}

const char *yang_dnode_get_string(const struct lyd_node *dnode,
				  const char *xpath_fmt, ...)
{
	const char *canon;

	YANG_DNODE_GET_CANON;

	return canon;
}

void yang_dnode_get_string_buf(char *buf, size_t size,
			       const struct lyd_node *dnode,
			       const char *xpath_fmt, ...)
{
	const char *canon;

	YANG_DNODE_GET_CANON;

	if (strlcpy(buf, canon, size) >= size) {
		char xpath[XPATH_MAXLEN];

		yang_dnode_get_path(dnode, xpath, sizeof(xpath));
		flog_warn(EC_LIB_YANG_DATA_TRUNCATED,
			  "%s: value was truncated [xpath %s]", __func__,
			  xpath);
	}
}

const char *yang_get_default_string(const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	return yang_get_default_value(xpath);
}

void yang_get_default_string_buf(char *buf, size_t size, const char *xpath_fmt,
				 ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	if (strlcpy(buf, value, size) >= size)
		flog_warn(EC_LIB_YANG_DATA_TRUNCATED,
			  "%s: value was truncated [xpath %s]", __func__,
			  xpath);
}

/*
 * Primitive type: binary.
 */
/*
 * binary 类型包装：data_new_binary 把原始字节做 base64 编码后包装成 yang_data；
 * dnode_get_binary_buf 把库内 base64 字符串解码到调用方缓冲，返回实际字节数（缓冲不足会告警）。
 */
struct yang_data *yang_data_new_binary(const char *xpath, const char *value,
				       size_t len)
{
	char *value_str;
	struct base64_encodestate s;
	int cnt;
	char *c;
	struct yang_data *data;

	value_str = (char *)malloc(len * 2);
	base64_init_encodestate(&s);
	cnt = base64_encode_block(value, len, value_str, &s);
	c = value_str + cnt;
	cnt = base64_encode_blockend(c, &s);
	c += cnt;
	*c = 0;
	data = yang_data_new(xpath, value_str);
	free(value_str);
	return data;
}

size_t yang_dnode_get_binary_buf(char *buf, size_t size,
				 const struct lyd_node *dnode,
				 const char *xpath_fmt, ...)
{
	const char *canon;
	size_t cannon_len;
	size_t decode_len;
	size_t ret_len;
	size_t cnt;
	char *value_str;
	struct base64_decodestate s;

	YANG_DNODE_GET_CANON;

	cannon_len = strlen(canon);
	decode_len = cannon_len + 1;
	value_str = (char *)malloc(decode_len);
	base64_init_decodestate(&s);
	cnt = base64_decode_block(canon, cannon_len, value_str, &s);

	ret_len = size > cnt ? cnt : size;
	memcpy(buf, value_str, ret_len);
	if (size < cnt) {
		char xpath[XPATH_MAXLEN];

		yang_dnode_get_path(dnode, xpath, sizeof(xpath));
		flog_warn(EC_LIB_YANG_DATA_TRUNCATED,
			  "%s: value was truncated [xpath %s]", __func__,
			  xpath);
	}
	free(value_str);
	return ret_len;
}


/*
 * Primitive type: empty.
 */
/*
 * empty 类型包装：data_new_empty 构造无值节点；dnode_get_empty 按 xpath 判断该空节点是否存在。
 */
struct yang_data *yang_data_new_empty(const char *xpath)
{
	return yang_data_new(xpath, NULL);
}

bool yang_dnode_get_empty(const struct lyd_node *dnode, const char *xpath_fmt,
			  ...)
{
	va_list ap;
	char xpath[XPATH_MAXLEN];
	const struct lyd_node_term *dleaf;

	assert(dnode);

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	dnode = yang_dnode_get(dnode, xpath);
	if (dnode) {
		dleaf = (const struct lyd_node_term *)dnode;
		if (dleaf->value.realtype->basetype == LY_TYPE_EMPTY)
			return true;
	}

	return false;
}

/*
 * Derived type: IP prefix.
 */
/*
 * ip prefix 类型包装（前缀，含掩码）：str2 解析、data_new 构造、
 * dnode_get 取值到 prefix、get_default 取默认值。
 */
void yang_str2prefix(const char *value, union prefixptr prefix)
{
	(void)str2prefix(value, prefix.p);
	apply_mask(prefix.p);
}

struct yang_data *yang_data_new_prefix(const char *xpath,
				       union prefixconstptr prefix)
{
	char value_str[PREFIX2STR_BUFFER];

	(void)prefix2str(prefix.p, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_dnode_get_prefix(struct prefix *prefix, const struct lyd_node *dnode,
			   const char *xpath_fmt, ...)
{
	const char *canon;

	/* XXX ip_prefix is a native type now in ly2, leverage? */
	YANG_DNODE_GET_CANON;

	/*
	 * Initialize prefix to avoid static analyzer complaints about
	 * uninitialized memory.
	 */
	memset(prefix, 0, sizeof(*prefix));

	(void)str2prefix(canon, prefix);
}

void yang_get_default_prefix(union prefixptr var, const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	yang_str2prefix(value, var);
}

/*
 * Derived type: ipv4.
 */
/*
 * ipv4 类型包装（单地址）：str2 解析到 in_addr、data_new 构造、
 * dnode_get 取值到 in_addr、get_default 取默认值。
 */
void yang_str2ipv4(const char *value, struct in_addr *addr)
{
	(void)inet_pton(AF_INET, value, addr);
}

struct yang_data *yang_data_new_ipv4(const char *xpath,
				     const struct in_addr *addr)
{
	char value_str[INET_ADDRSTRLEN];

	(void)inet_ntop(AF_INET, addr, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_dnode_get_ipv4(struct in_addr *addr, const struct lyd_node *dnode,
			 const char *xpath_fmt, ...)
{
	const char *canon;

	/* XXX libyang2 IPv4 address is a native type now in ly2 */
	YANG_DNODE_GET_CANON;

	(void)inet_pton(AF_INET, canon, addr);
}

void yang_get_default_ipv4(struct in_addr *var, const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	yang_str2ipv4(value, var);
}

/*
 * Derived type: ipv4p.
 */
/*
 * ipv4p 类型包装（ipv4 前缀形式）：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
void yang_str2ipv4p(const char *value, union prefixptr prefix)
{
	struct prefix_ipv4 *prefix4 = prefix.p4;

	(void)str2prefix_ipv4(value, prefix4);
	apply_mask_ipv4(prefix4);
}

struct yang_data *yang_data_new_ipv4p(const char *xpath,
				      union prefixconstptr prefix)
{
	char value_str[PREFIX2STR_BUFFER];

	(void)prefix2str(prefix.p, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_dnode_get_ipv4p(union prefixptr prefix, const struct lyd_node *dnode,
			  const char *xpath_fmt, ...)
{
	struct prefix_ipv4 *prefix4 = prefix.p4;
	const char *canon;

	YANG_DNODE_GET_CANON;

	/* XXX libyang2: ipv4/6 address is a native type now in ly2 */
	(void)str2prefix_ipv4(canon, prefix4);
}

void yang_get_default_ipv4p(union prefixptr var, const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	yang_str2ipv4p(value, var);
}

/*
 * Derived type: ipv6.
 */
/*
 * ipv6 类型包装（单地址）：str2 解析到 in6_addr、data_new 构造、
 * dnode_get 取值到 in6_addr、get_default 取默认值。
 */
void yang_str2ipv6(const char *value, struct in6_addr *addr)
{
	(void)inet_pton(AF_INET6, value, addr);
}

struct yang_data *yang_data_new_ipv6(const char *xpath,
				     const struct in6_addr *addr)
{
	char value_str[INET6_ADDRSTRLEN];

	(void)inet_ntop(AF_INET6, addr, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_dnode_get_ipv6(struct in6_addr *addr, const struct lyd_node *dnode,
			 const char *xpath_fmt, ...)
{
	const char *canon;

	/* XXX libyang2: IPv6 address is a native type now, leverage. */
	YANG_DNODE_GET_CANON;

	(void)inet_pton(AF_INET6, canon, addr);
}

void yang_get_default_ipv6(struct in6_addr *var, const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	yang_str2ipv6(value, var);
}

/*
 * Derived type: ipv6p.
 */
/*
 * ipv6p 类型包装（ipv6 前缀形式）：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
void yang_str2ipv6p(const char *value, union prefixptr prefix)
{
	struct prefix_ipv6 *prefix6 = prefix.p6;

	(void)str2prefix_ipv6(value, prefix6);
	apply_mask_ipv6(prefix6);
}

struct yang_data *yang_data_new_ipv6p(const char *xpath,
				      union prefixconstptr prefix)
{
	char value_str[PREFIX2STR_BUFFER];

	(void)prefix2str(prefix.p, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_dnode_get_ipv6p(union prefixptr prefix, const struct lyd_node *dnode,
			  const char *xpath_fmt, ...)
{
	struct prefix_ipv6 *prefix6 = prefix.p6;
	const char *canon;

	/* XXX IPv6 address is a native type now in ly2 -- can we leverage? */
	YANG_DNODE_GET_CANON;

	(void)str2prefix_ipv6(canon, prefix6);
}

void yang_get_default_ipv6p(union prefixptr var, const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	yang_str2ipv6p(value, var);
}

/*
 * Derived type: ip.
 */
/*
 * ip 类型包装（通用 ipaddr，可承载 v4 或 v6）：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
void yang_str2ip(const char *value, struct ipaddr *ip)
{
	(void)str2ipaddr(value, ip);
}

struct yang_data *yang_data_new_ip(const char *xpath, const struct ipaddr *addr)
{
	size_t sz = IS_IPADDR_V4(addr) ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN;
	char value_str[sz];

	ipaddr2str(addr, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_dnode_get_ip(struct ipaddr *addr, const struct lyd_node *dnode,
		       const char *xpath_fmt, ...)
{
	const char *canon;

	/* XXX IPv4 address could be a plugin type now in ly2, leverage? */
	YANG_DNODE_GET_CANON;

	(void)str2ipaddr(canon, addr);
}

void yang_get_default_ip(struct ipaddr *var, const char *xpath_fmt, ...)
{
	char xpath[XPATH_MAXLEN];
	const char *value;
	va_list ap;

	va_start(ap, xpath_fmt);
	vsnprintf(xpath, sizeof(xpath), xpath_fmt, ap);
	va_end(ap);

	value = yang_get_default_value(xpath);
	yang_str2ip(value, var);
}

/*
 * mac 类型包装（以太网地址 ethaddr）：data_new 构造、str2 解析、dnode_get 取值。
 */
struct yang_data *yang_data_new_mac(const char *xpath,
				    const struct ethaddr *mac)
{
	char value_str[ETHER_ADDR_STRLEN];

	prefix_mac2str(mac, value_str, sizeof(value_str));
	return yang_data_new(xpath, value_str);
}

void yang_str2mac(const char *value, struct ethaddr *mac)
{
	(void)prefix_str2mac(value, mac);
}

void yang_dnode_get_mac(struct ethaddr *mac, const struct lyd_node *dnode,
			const char *xpath_fmt, ...)
{
	const char *canon;

	YANG_DNODE_GET_CANON;

	(void)prefix_str2mac(canon, mac);
}

/*
 * date-and-time 类型包装：data_new_date_and_time 构造（is_monotime 为真时先把单调时钟换算为真实时间）、
 * dnode_get_date_and_timespec 取 timespec、dnode_get_date_and_time 取 time_t。
 */
struct yang_data *yang_data_new_date_and_time(const char *xpath, time_t time, bool is_monotime)
{
	struct yang_data *yd;
	char *times = NULL;

	if (is_monotime) {
		struct timeval _time = { time, 0 };
		struct timeval time_real;

		monotime_to_realtime(&_time, &time_real);
		time = time_real.tv_sec;
	}

	(void)ly_time_time2str(time, NULL, &times);
	yd = yang_data_new(xpath, times);
	free(times);

	return yd;
}

/*
 * yang_dnode_get_date_and_timespec - 取 date-and-time 并解析为 struct timespec（解析失败会 abort）。
 */
struct timespec yang_dnode_get_date_and_timespec(const struct lyd_node *dnode,
						 const char *xpath_fmt, ...)
{
	const char *canon;
	struct timespec ts;
	LY_ERR err;

	YANG_DNODE_GET_CANON;

	err = ly_time_str2ts(canon, &ts);
	assert(!err);

	return ts;
}

/*
 * yang_dnode_get_date_and_time - 取 date-and-time 并解析为 time_t（解析失败会 abort）。
 */
time_t yang_dnode_get_date_and_time(const struct lyd_node *dnode,
				    const char *xpath_fmt, ...)
{
	const char *canon;
	time_t time;
	LY_ERR err;

	YANG_DNODE_GET_CANON;

	err = ly_time_str2time(canon, &time, NULL);
	assert(!err);

	return time;
}

/*
 * yang_dnode_get_bandwidth_ieee_float32 - 取 rt-types:bandwidth-ieee-float32 并解析为单精度浮点。
 */
float yang_dnode_get_bandwidth_ieee_float32(const struct lyd_node *dnode,
					    const char *xpath_fmt, ...)
{
	const char *canon;
	float value;

	YANG_DNODE_GET_CANON;

	assert(sscanf(canon, "%a", &value) == 1);

	return value;
}

/*
 * yang_dnode_get_identity - 取叶子节点的 identity 名称。
 * 仅当节点的基类型是 LY_TYPE_IDENT 时返回其名字，否则返回 NULL。
 */
const char *yang_dnode_get_identity(const struct lyd_node *dnode)
{
	const struct lyd_node_term *term = (const struct lyd_node_term *)dnode;
	const struct lysc_node_leaf *leaf = (const struct lysc_node_leaf *)dnode->schema;

	if (leaf->type->basetype == LY_TYPE_IDENT) {
		const struct lysc_ident *ident = term->value.ident;

		return ident->name;
	}

	return NULL;
}

/*
 * yang_nexthop_type2str - 把 nexthop 类型枚举转成 YANG 中使用的字符串名称（未知类型返回 "unknown"）。
 */
const char *yang_nexthop_type2str(uint32_t ntype)
{
	switch (ntype) {
	case NEXTHOP_TYPE_IFINDEX:
		return "ifindex";
		break;
	case NEXTHOP_TYPE_IPV4:
		return "ip4";
		break;
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		return "ip4-ifindex";
		break;
	case NEXTHOP_TYPE_IPV6:
		return "ip6";
		break;
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		return "ip6-ifindex";
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		return "blackhole";
		break;
	default:
		return "unknown";
		break;
	}
}


/*
 * yang_afi_safi_value2identity - 把 afi 与 safi 组合映射为 frr-routing 模块的 identity 名。
 * 无匹配组合时返回 NULL。
 */
const char *yang_afi_safi_value2identity(afi_t afi, safi_t safi)
{
	if (afi == AFI_IP && safi == SAFI_UNICAST)
		return "frr-routing:ipv4-unicast";
	if (afi == AFI_IP6 && safi == SAFI_UNICAST)
		return "frr-routing:ipv6-unicast";
	if (afi == AFI_IP && safi == SAFI_MULTICAST)
		return "frr-routing:ipv4-multicast";
	if (afi == AFI_IP6 && safi == SAFI_MULTICAST)
		return "frr-routing:ipv6-multicast";
	if (afi == AFI_IP && safi == SAFI_MPLS_VPN)
		return "frr-routing:l3vpn-ipv4-unicast";
	if (afi == AFI_IP6 && safi == SAFI_MPLS_VPN)
		return "frr-routing:l3vpn-ipv6-unicast";
	if (afi == AFI_L2VPN && safi == SAFI_EVPN)
		return "frr-routing:l2vpn-evpn";
	if (afi == AFI_IP && safi == SAFI_LABELED_UNICAST)
		return "frr-routing:ipv4-labeled-unicast";
	if (afi == AFI_IP6 && safi == SAFI_LABELED_UNICAST)
		return "frr-routing:ipv6-labeled-unicast";
	if (afi == AFI_IP && safi == SAFI_FLOWSPEC)
		return "frr-routing:ipv4-flowspec";
	if (afi == AFI_IP6 && safi == SAFI_FLOWSPEC)
		return "frr-routing:ipv6-flowspec";

	return NULL;
}

/*
 * yang_afi_safi_identity2value - 把 identity 名解析回 afi 与 safi（经出参返回）。
 * 无法识别时输出 AFI_UNSPEC 与 SAFI_UNSPEC。
 */
void yang_afi_safi_identity2value(const char *key, afi_t *afi, safi_t *safi)
{
	if (strmatch(key, "frr-routing:ipv4-unicast")) {
		*afi = AFI_IP;
		*safi = SAFI_UNICAST;
	} else if (strmatch(key, "frr-routing:ipv6-unicast")) {
		*afi = AFI_IP6;
		*safi = SAFI_UNICAST;
	} else if (strmatch(key, "frr-routing:ipv4-multicast")) {
		*afi = AFI_IP;
		*safi = SAFI_MULTICAST;
	} else if (strmatch(key, "frr-routing:ipv6-multicast")) {
		*afi = AFI_IP6;
		*safi = SAFI_MULTICAST;
	} else if (strmatch(key, "frr-routing:l3vpn-ipv4-unicast")) {
		*afi = AFI_IP;
		*safi = SAFI_MPLS_VPN;
	} else if (strmatch(key, "frr-routing:l3vpn-ipv6-unicast")) {
		*afi = AFI_IP6;
		*safi = SAFI_MPLS_VPN;
	} else if (strmatch(key, "frr-routing:ipv4-labeled-unicast")) {
		*afi = AFI_IP;
		*safi = SAFI_LABELED_UNICAST;
	} else if (strmatch(key, "frr-routing:ipv6-labeled-unicast")) {
		*afi = AFI_IP6;
		*safi = SAFI_LABELED_UNICAST;
	} else if (strmatch(key, "frr-routing:l2vpn-evpn")) {
		*afi = AFI_L2VPN;
		*safi = SAFI_EVPN;
	} else if (strmatch(key, "frr-routing:ipv4-flowspec")) {
		*afi = AFI_IP;
		*safi = SAFI_FLOWSPEC;
	} else if (strmatch(key, "frr-routing:ipv6-flowspec")) {
		*afi = AFI_IP6;
		*safi = SAFI_FLOWSPEC;
	} else {
		*afi = AFI_UNSPEC;
		*safi = SAFI_UNSPEC;
	}
}
