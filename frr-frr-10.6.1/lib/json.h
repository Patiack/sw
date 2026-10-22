// SPDX-License-Identifier: GPL-2.0-or-later
/* json-c wrapper
 * Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * 文件导读：json.h —— FRR 的 JSON 封装接口
 *
 * 一、这个文件做什么
 *   声明 FRR 对 json-c 的封装接口：一批 json_object 便捷函数（往对象里塞
 *   字符串 / 整数 / 浮点 / 布尔 / 数组）、printfrr 风格的变体、以及把对象
 *   输出到 vty 的 frr_json_vty_out 系列。实现见 lib/json.c。
 *
 * 二、核心内容
 *   - JSON_FOREACH：遍历 json 对象键值对的宏。
 *   - JSON_OBJECT_NEW_ARRAY：把 C 数组元素逐个转换后打包成 json 数组。
 *   - use_json：判断命令是否请求 JSON 输出。
 *   - 一组 add / addv / addf：添加键值对及其格式化变体。
 *   - 增量输出控制：frr_json_set_open 与 frr_json_set_complete。
 *
 * 三、约定与坑
 *   - 变体函数参数顺序特殊：addv / addf 把 child 放在 key 之前（见文件内注释）。
 *   - json_object_object_get 已被禁用，请改用带 ex 后缀的版本。
 *   - 为兼容旧版 json-c，这里补定义了 JSON_C_TO_STRING_NOSLASHESCAPE。
 *
 * 四、阅读顺序
 *   先看 JSON_FOREACH 与两个宏，再看 use_json、各 add 系列声明，最后看 vty
 *   输出与增量输出接口。
 */

#ifndef _QUAGGA_JSON_H
#define _QUAGGA_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "command.h"
#include "printfrr.h"
#include <json-c/json.h>

/*
 * JSON_FOREACH — FRR 风格的 json 对象迭代宏。
 * 用法：JSON_FOREACH(obj, it, it_end) { ... 用 json_object_iter_peek_value(it) ... }
 * jo：被遍历的 json 对象；joi / join：迭代器起点与终点（由宏内部初始化）。
 * 底层使用 json-c 的对象迭代器，只能用于 object（字典）类型。
 */

/*
 * FRR style JSON iteration.
 * Usage: JSON_FOREACH(...) { ... }
 */
#define JSON_FOREACH(jo, joi, join)                                            \
	/* struct json_object *jo; */                                          \
	/* struct json_object_iterator joi; */                                 \
	/* struct json_object_iterator join; */                                \
	for ((joi) = json_object_iter_begin((jo)),                             \
	    (join) = json_object_iter_end((jo));                               \
	     json_object_iter_equal(&(joi), &(join)) == 0;                     \
	     json_object_iter_next(&(joi)))

/*
 * JSON_OBJECT_NEW_ARRAY — 把 C 数组打包成 json 数组。
 * json_func：逐个元素的转换函数（如 json_object_new_string）；
 * fields：C 数组;n：元素个数。
 * 展开为一个语句表达式，返回新建的 json 数组对象。
 */
#define JSON_OBJECT_NEW_ARRAY(json_func, fields, n)                            \
	({                                                                     \
		struct json_object *_json_array = json_object_new_array();     \
		for (int _i = 0; _i < (n); _i++)                               \
			json_object_array_add(_json_array,                     \
					      (json_func)((fields)[_i]));      \
		(_json_array);                                                 \
	})

/*
 * use_json — 判断命令末尾是否带 "json" 关键字（决定输出 JSON 还是文本）。
 * argc / argv：解析后的命令 token 数组。返回 true 表示应输出 JSON。
 */
extern bool use_json(const int argc, struct cmd_token *argv[]);
/*
 * json_object_string_add — 向对象添加字符串键值对（obj、key、值 s）。
 */
extern void json_object_string_add(struct json_object *obj, const char *key,
				   const char *s);
/*
 * json_object_int_add — 向对象添加 64 位整数键值对（值 i）。
 */
extern void json_object_int_add(struct json_object *obj, const char *key,
				int64_t i);
/*
 * json_object_boolean_add — 按 val 添加布尔键值对（true / false 皆可）。
 */
void json_object_boolean_add(struct json_object *obj, const char *key,
			     bool val);

/*
 * json_object_double_add — 向对象添加双精度浮点键值对（值 i）。
 */
extern void json_object_double_add(struct json_object *obj, const char *key,
				   double i);
/*
 * json_object_boolean_false_add — 添加值为 false 的布尔键值对。
 */
extern void json_object_boolean_false_add(struct json_object *obj,
					  const char *key);
/*
 * json_object_boolean_true_add — 添加值为 true 的布尔键值对。
 */
extern void json_object_boolean_true_add(struct json_object *obj,
					 const char *key);
/*
 * json_object_lock — 增加对象引用计数（保留一份引用），返回同一对象指针。
 */
extern struct json_object *json_object_lock(struct json_object *obj);
/*
 * json_object_free — 减少对象引用计数，归零时释放对象及其子对象。
 */
extern void json_object_free(struct json_object *obj);
/*
 * json_array_string_add — 向 json 数组追加一个字符串元素。
 */
extern void json_array_string_add(json_object *json, const char *str);

/*
 * 以下为 printfrr 风格的可变参数助手：v 后缀接 va_list，f 后缀接省略号参数。
 * 它们内部最终调用对应的非格式化版本。
 */

/* printfrr => json helpers */

/*
 * json_object_string_addv — 值为按格式串生成的字符串（v 版，接收 va_list）。
 */
PRINTFRR(3, 0)
extern void json_object_string_addv(struct json_object *obj, const char *key,
				    const char *fmt, va_list args);
/*
 * json_object_string_addf — 值为按格式串生成的字符串（f 版，接收省略号参数）。
 */
PRINTFRR(3, 4)
static inline void json_object_string_addf(struct json_object *obj,
					   const char *key, const char *fmt,
					   ...)
{
	va_list args;

	va_start(args, fmt);
	json_object_string_addv(obj, key, fmt, args);
	va_end(args);
}

/*
 * json_array_string_addv — 向数组追加一个按格式串生成的字符串（v 版）。
 */
PRINTFRR(2, 0)
extern void json_array_string_addv(json_object *json, const char *fmt,
				   va_list args);
/*
 * json_array_string_addf — 向数组追加一个按格式串生成的字符串（f 版）。
 */
PRINTFRR(2, 3)
static inline void json_array_string_addf(struct json_object *obj,
					  const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	json_array_string_addv(obj, fmt, args);
	va_end(args);
}

/*
 * json_object_new_stringv — 用格式串新建一个 json 字符串对象（v 版）。
 */
PRINTFRR(1, 0)
extern struct json_object *json_object_new_stringv(const char *fmt,
						   va_list args);
/*
 * json_object_new_stringf — 用格式串新建一个 json 字符串对象（f 版）。
 */
PRINTFRR(1, 2)
static inline struct json_object *json_object_new_stringf(const char *fmt, ...)
{
	struct json_object *ret;
	va_list args;

	va_start(args, fmt);
	ret = json_object_new_stringv(fmt, args);
	va_end(args);

	return ret;
}

/*
 * 注意：以下 addv / addf 变体的参数顺序与非变体版本不同，原因是可变参数必须
 * 位于参数表尾部：非变体为 (parent, key, child)，而变体为 (parent, child, key, ...)。
 */

/* NOTE: argument order differs! (due to varargs)
 *   json_object_object_add(parent, key, child)
 *   json_object_object_addv(parent, child, key, va)
 *   json_object_object_addf(parent, child, key, ...)
 * (would be weird to have the child inbetween the format string and args)
 */
/*
 * json_object_object_addv — 用格式串生成键名，把 child 挂到 parent 下（v 版）。
 */
PRINTFRR(3, 0)
extern void json_object_object_addv(struct json_object *parent,
				    struct json_object *child,
				    const char *keyfmt, va_list args);
/*
 * json_object_object_addf — 用格式串生成键名，把 child 挂到 parent 下（f 版）。
 */
PRINTFRR(3, 4)
static inline void json_object_object_addf(struct json_object *parent,
					   struct json_object *child,
					   const char *keyfmt, ...)
{
	va_list args;

	va_start(args, keyfmt);
	json_object_object_addv(parent, child, keyfmt, args);
	va_end(args);
}

/*
 * JSON_STR — CLI 帮助文本里描述 "json" 关键字的固定字符串。
 */
#define JSON_STR "JavaScript Object Notation\n"

/*
 * JSON_C_TO_STRING_NOSLASHESCAPE — 让 json-c 输出时不转义正斜杠（配合前缀
 * 形如 20.0.14.0/24 的键）。较新 json-c 已内置该宏，此处仅为兼容旧版本补定义。
 */

/* NOTE: json-c lib has following commit 316da85 which
 * handles escape of forward slash.
 * This allows prefix  "20.0.14.0\/24":{
 * to  "20.0.14.0/24":{ some platforms do not have
 * latest copy of json-c where defining below macro.
 */

#ifndef JSON_C_TO_STRING_NOSLASHESCAPE

/**
  * Don't escape forward slashes.
  */
#define JSON_C_TO_STRING_NOSLASHESCAPE (1<<4)
#endif

/*
 * 屏蔽并禁用已废弃的 json_object_object_get：一旦被调用会在编译期报错，
 * 促使改用带 ex 后缀的替代接口。
 */
/* This api is deprecated, don't use it, use the _ex() version instead */
#ifndef json_object_object_get
#define json_object_object_get THIS IS DEPRECATED
#endif

/*
 * frr_json_vty_out — 把 json 对象以美化排版形式输出到 vty 并释放。
 * frr_json_vty_out_bare — 紧凑（非美化）形式的 vty 输出。
 */
/* FRR print/string-output function for vty output */
void frr_json_vty_out(struct vty *vty, struct json_object *jobj);
void frr_json_vty_out_bare(struct vty *vty, struct json_object *jobj);

/*
 * frr_vty_json_dump — 把 json 树的层次轮廓（键名与类型）打印到 vty，供调试。
 */
/* Dump an outline of the json hierarchy to vty */
void frr_vty_json_dump(struct vty *vty, struct json_object *jobj);

/*
 * frr_json_set_open — 标记对象（集合）尚未完成，暂不输出集合结束符，因为可能
 * 还会继续追加子项。是增量输出的起点。
 */

/*
 * Flag that an object (a collection) is not yet complete; don't emit end-of-collection
 * text yet, as more children may be added.
 */
void frr_json_set_open(struct json_object *jobj);

/*
 * frr_json_set_complete — 标记对象已完成：下一次输出时会写出集合结束符，
 * 并释放该对象及其子对象。必须与 frr_json_set_open 成对使用。
 */

/*
 * Indicate that an object is complete; during the next output function call,
 * emit end-of-collection text, and free the object and its children.
 */
void frr_json_set_complete(struct json_object *jobj);

#ifdef __cplusplus
}
#endif

#endif /* _QUAGGA_JSON_H */
