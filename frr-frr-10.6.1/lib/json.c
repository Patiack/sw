// SPDX-License-Identifier: GPL-2.0-or-later
/* json-c wrapper
 * Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * 文件导读：json.c —— FRR 内嵌 JSON 库的 C 封装实现
 *
 * 一、这个文件做什么
 *   本文件实现 lib/json.h 中声明的一批函数。FRR 内部并不到处直接调用 json-c，
 *   而是通过这一层封装来统一：构造 json 对象（字符串 / 整数 / 浮点 / 布尔 /
 *   数组）、把对象序列化成文本输出到 vty（终端），以及在 show ... json 命令里
 *   支持"边生成边输出"的增量输出模式。
 *
 * 二、核心概念
 *   1. json_object：json-c 提供的树节点，可为 object(dict)、array、string、
 *      int、double、boolean、null 等类型，用 json_object_get_type() 判断。
 *   2. use_json()：判断一条 CLI 命令末尾是否带 "json" 关键字，决定输出走 JSON
 *      还是走人类可读文本。
 *   3. 增量输出标志（本文件自定义）：挂在 json 对象 userdata 上的位标志，共
 *      STARTED / OPEN / COLLECTION / CLOSED 四种，用于控制何时输出集合的开始、
 *      分隔符与结束，从而支持"打开-追加-完成"的流式输出。
 *
 * 三、与其他模块的关系
 *   - lib/json.h：本文件接口的声明，并定义 JSON_FOREACH 等宏。
 *   - lib/printbuf.h、json-c/printbuf.h：增量输出时使用的字符缓冲。
 *   - lib/vty.h：frr_json_vty_out / frr_json_vty_out_bare 把结果写到 vty。
 *   - lib/printfrr.h：vasnprintfrr 用于格式化生成 key 和字符串值。
 *
 * 四、阅读顺序
 *   先看 use_json 与各 *_add 系列（简单封装），再看 frr_json_obj_to_vty
 *   （增量输出的核心递归函数），最后看 vty_json_dump_helper 与
 *   frr_json_set_open / frr_json_set_complete 这一对状态控制函数。
 */

#include <zebra.h>

#include "command.h"
#include "lib/json.h"
#include "json-c/printbuf.h"

/*
 * use_json — 判断当前 CLI 命令是否请求 JSON 输出。
 * 参数 argc / argv：解析后的命令 token 数组（argv[argc-1] 通常是最后一个词元）。
 * 返回 true 表示命令末尾带 "json" 关键字，调用方应改为输出 JSON。
 * 约定：json 关键字必须位于整行命令的最后；不在最后则不会被识别。
 * 无副作用。
 */

/*
 * This function assumes that the json keyword
 * is the *last* keyword on the line no matter
 * what.
 */
bool use_json(const int argc, struct cmd_token *argv[])
{
	if (argc == 0)
		return false;

	if (argv[argc - 1]->arg && strmatch(argv[argc - 1]->text, "json"))
		return true;

	return false;
}

/*
 * json_object_new_stringv — 用 printfrr 格式串新建一个 json 字符串对象。
 * fmt / args：printfrr 风格的格式串与可变参数（va_list）。
 * 先格式到栈缓冲 buf，超出则改用堆分配；返回新建的 json 字符串对象。
 * 注意：若 text 不再是栈缓冲（说明发生了堆分配），须 XFREE 释放。
 */
struct json_object *json_object_new_stringv(const char *fmt, va_list args)
{
	struct json_object *ret;
	char *text, buf[256];

	text = vasnprintfrr(MTYPE_TMP, buf, sizeof(buf), fmt, args);
	ret = json_object_new_string(text);

	if (text != buf)
		XFREE(MTYPE_TMP, text);
	return ret;
}

/*
 * json_array_string_add — 向 json 数组追加一个字符串元素。
 * json：目标数组对象；str：要加入的字符串（会被复制）。
 * 无返回值；直接在数组尾部追加。
 */
void json_array_string_add(json_object *json, const char *str)
{
	json_object_array_add(json, json_object_new_string(str));
}

/*
 * json_array_string_addv — 向 json 数组追加一个按格式串生成的字符串元素。
 * json：目标数组；fmt / args：printfrr 格式串与参数。
 * 无返回值；内部复用 json_object_new_stringv。
 */
void json_array_string_addv(json_object *json, const char *fmt, va_list args)
{
	json_object_array_add(json, json_object_new_stringv(fmt, args));
}

/*
 * json_object_string_add — 向 json 对象添加一个字符串键值对。
 * obj：目标对象；key：键名；s：字符串值。
 * 无返回值；等价于 json_object_object_add(obj, key, new_string(s))。
 * 注意：已存在的同名 key 会被替换（json-c 会释放旧值）。
 */
void json_object_string_add(struct json_object *obj, const char *key,
			    const char *s)
{
	json_object_object_add(obj, key, json_object_new_string(s));
}

/*
 * json_object_string_addv — 键值对版：值为按格式串生成的字符串。
 * obj / key：目标对象与键名；fmt / args：printfrr 格式串与参数。
 */
void json_object_string_addv(struct json_object *obj, const char *key,
			     const char *fmt, va_list args)
{
	json_object_object_add(obj, key, json_object_new_stringv(fmt, args));
}

/*
 * json_object_object_addv — 用格式串生成键名，把 child 挂到 parent 之下。
 * parent：父对象；child：子对象（所有权转移给 parent）；
 * keyfmt / args：printfrr 风格键名格式与参数。
 * 注意：参数顺序与 json_object_object_add 不同（child 在前、key 在后），
 *       这是为了适配可变参数；键名若为堆分配需 XFREE 释放。
 */
void json_object_object_addv(struct json_object *parent,
			     struct json_object *child, const char *keyfmt,
			     va_list args)
{
	char *text, buf[256];

	text = vasnprintfrr(MTYPE_TMP, buf, sizeof(buf), keyfmt, args);
	json_object_object_add(parent, text, child);

	if (text != buf)
		XFREE(MTYPE_TMP, text);
}

/*
 * json_object_int_add — 添加一个 64 位整数键值对。
 * obj / key：目标对象与键名；i：整数值。
 */
void json_object_int_add(struct json_object *obj, const char *key, int64_t i)
{
	json_object_object_add(obj, key, json_object_new_int64(i));
}

/*
 * json_object_double_add — 添加一个双精度浮点键值对。
 * obj / key：目标对象与键名；i：浮点值。
 */
void json_object_double_add(struct json_object *obj, const char *key, double i)
{
	json_object_object_add(obj, key, json_object_new_double(i));
}

/*
 * json_object_boolean_false_add — 添加值为 false 的布尔键值对。
 * obj / key：目标对象与键名。
 */
void json_object_boolean_false_add(struct json_object *obj, const char *key)
{
	json_object_object_add(obj, key, json_object_new_boolean(0));
}

/*
 * json_object_boolean_true_add — 添加值为 true 的布尔键值对。
 * obj / key：目标对象与键名。
 */
void json_object_boolean_true_add(struct json_object *obj, const char *key)
{
	json_object_object_add(obj, key, json_object_new_boolean(1));
}

/*
 * json_object_boolean_add — 按 val 添加布尔键值对（true / false 皆可）。
 * obj / key：目标对象与键名；val：布尔值。
 */
void json_object_boolean_add(struct json_object *obj, const char *key, bool val)
{
	json_object_object_add(obj, key, json_object_new_boolean(val));
}

/*
 * json_object_lock — 增加 json 对象的引用计数（即 json_object_get）。
 * 返回同一对象指针；用于在别处保留引用以免被提前释放。
 */
struct json_object *json_object_lock(struct json_object *obj)
{
	return json_object_get(obj);
}

/*
 * json_object_free — 减少引用计数，计数归零时释放对象及其子对象。
 * 对应 json_object_put；不再使用时调用，避免内存泄漏。
 */
void json_object_free(struct json_object *obj)
{
	json_object_put(obj);
}

/*
 * 增量（流式）JSON 输出支持
 * 依赖较新版本 libjson-c 提供的 userdata / 预格式化接口，老版本不支持
 * （见文件后半部的 #else 降级分支）。
 * 用法：先 frr_json_set_open() 标记对象未完成，多次追加子对象后，再调用
 * frr_json_set_complete() 标记完成；frr_json_vty_out() 输出时会把已完成的
 * 子项吐出并释放。
 */

/*
 * Incremental json output support: depends on some apis that may not be present
 * in older libjson-c releases.
 */

#if JSON_C_MAJOR_VERSION > 0 || JSON_C_MINOR_VERSION > 12

/*
 * 增量 JSON 输出所用的标志位取值
 */

/*
 * Flags values used in incremental json output
 */

/*
 * FRR_JSON_STARTED：该对象已开始输出，集合起始符（左花括号或左方括号）已写入。
 */
/* Output of object has started; start-of-collection strings have been emitted */
#define FRR_JSON_STARTED   0x01
/*
 * FRR_JSON_OPEN：对象仍不完整，暂不输出集合结束符，等待更多子项追加。
 */
/* Object output is incomplete; don't emit end-of-collection strings yet */
#define FRR_JSON_OPEN      0x02
/*
 * FRR_JSON_COLLECTION：对象已开始输出，后续子项之间需要写入逗号分隔。
 */
/* Object output has started, may need comma or separator */
#define FRR_JSON_COLLECTION  0x04
/*
 * FRR_JSON_CLOSED：对象此前为 OPEN，现已完成，输出时应补上集合结束符并释放。
 */
/* Object was OPEN, but is now complete; emit end-of-collection strings */
#define FRR_JSON_CLOSED      0x08

/*
 * frr_json_get_flags — 取出挂在 json 对象 userdata 上的 FRR 标志位。
 * 返回 int 形式的标志（userdata 被复用为 intptr_t 存放标志）。
 */
/* Helper to return json object userdata as flags int */
static int frr_json_get_flags(struct json_object *jobj)
{
	void *pflags;

	pflags = json_object_get_userdata(jobj);
	return (int)(intptr_t)pflags;
}

/*
 * frr_json_set_data — 把 FRR 标志位写回 json 对象的 userdata。
 * jobj：目标对象；iflags：要写入的标志位整数。
 * 副作用：覆盖该对象原有的 userdata。
 */

/*
 * Helper to set or update FRR userdata in a json object.
 */
static void frr_json_set_data(struct json_object *jobj, intptr_t iflags)
{
	void *flags = (void *)iflags;

	json_object_set_userdata(jobj, flags, NULL);
}

/*
 * frr_json_indent — 按缩进层级向输出缓冲写入空白（美化输出用）。
 * pb：输出缓冲；level：层级；flags：json 输出标志。
 * 若设置 PRETTY_TAB 则用制表符，否则每层 2 个空格；非 PRETTY 时不做任何事。
 */
/* Helper to indent string output */
static void frr_json_indent(struct printbuf *pb, int level, int flags)
{
	if (flags & JSON_C_TO_STRING_PRETTY) {
		if (flags & JSON_C_TO_STRING_PRETTY_TAB)
			printbuf_memset(pb, -1, '\t', level);
		else
			printbuf_memset(pb, -1, ' ', level * 2);
	}
}

/*
 * frr_json_hex_chars — 转义控制字符时使用的十六进制字符表（含大小写）。
 */
static const char *frr_json_hex_chars = "0123456789abcdefABCDEF";

/*
 * frr_json_escape_str — 把字符串按 JSON 规则转义后写入输出缓冲。
 * pb：输出缓冲；str / len：待转义字符串及其长度；flags：输出标志。
 * 处理退格、换行、回车、制表、换页、双引号、反斜杠、正斜杠，以及所有
 * 小于空格的字符（转成 Unicode 转义形式）。
 * 若设置 JSON_C_TO_STRING_NOSLASHESCAPE 则正斜杠不转义。
 * 返回 0（仅示意调用成功，不是错误码）。
 */
/* Helper for string escaping */
static int frr_json_escape_str(struct printbuf *pb, const char *str, size_t len, int flags)
{
	size_t pos = 0, start_offset = 0;
	unsigned char c;

	while (len) {
		--len;
		c = str[pos];
		switch (c) {
		case '\b':
		case '\n':
		case '\r':
		case '\t':
		case '\f':
		case '"':
		case '\\':
		case '/':
			if ((flags & JSON_C_TO_STRING_NOSLASHESCAPE) && c == '/') {
				pos++;
				break;
			}

			if (pos > start_offset)
				printbuf_memappend(pb, str + start_offset, pos - start_offset);

			if (c == '\b')
				printbuf_memappend(pb, "\\b", 2);
			else if (c == '\n')
				printbuf_memappend(pb, "\\n", 2);
			else if (c == '\r')
				printbuf_memappend(pb, "\\r", 2);
			else if (c == '\t')
				printbuf_memappend(pb, "\\t", 2);
			else if (c == '\f')
				printbuf_memappend(pb, "\\f", 2);
			else if (c == '"')
				printbuf_memappend(pb, "\\\"", 2);
			else if (c == '\\')
				printbuf_memappend(pb, "\\\\", 2);
			else if (c == '/')
				printbuf_memappend(pb, "\\/", 2);

			start_offset = ++pos;
			break;
		default:
			if (c < ' ') {
				char sbuf[7];

				if (pos > start_offset)
					printbuf_memappend(pb, str + start_offset,
							   pos - start_offset);
				snprintf(sbuf, sizeof(sbuf), "\\u00%c%c",
					 frr_json_hex_chars[c >> 4],
					 frr_json_hex_chars[c & 0xf]);
				printbuf_memappend_fast(pb, sbuf, (int)sizeof(sbuf) - 1);
				start_offset = ++pos;
			} else {
				pos++;
			}
		}
	}
	if (pos > start_offset)
		printbuf_memappend(pb, str + start_offset, pos - start_offset);
	return 0;
}

/*
 * frr_json_obj_to_vty — 增量输出的核心递归函数。
 * vty：输出目标；jobj：当前容器对象；pb：中间输出缓冲（顶层传 NULL 则新建）；
 * level：当前层级（用于缩进）；jflags：json 输出标志。
 * 行为：遍历 jobj 的子项，单值类型直接输出并移除；遇到仍为 OPEN 的子容器则
 *       递归进入而不删除；遇到 CLOSED 的子容器则先递归输出再删除。
 * 副作用：会通过 vty_out 输出内容、释放已完成的子对象；顶层调用时新建并释放 pb。
 * 返回 1（恒表示成功）。
 * 注意：只应传入 object 或 array 类型，否则会触发 assert。
 */

/*
 * FRR custom json output for vty. Examine the children of 'jobj'.
 * Print singleton types and mark complete. Recurse into child collections
 * and determine whether the child is complete. If so, free/remove it; if
 * not, return.
 */
static int frr_json_obj_to_vty(struct vty *vty, struct json_object *jobj,
			       struct printbuf *pb, int level, int jflags)
{
	int parent_flags, child_flags;
	const char *key;
	size_t idx;
	struct json_object *jval;
	struct lh_table *lhtable;
	struct lh_entry *lhentry;
	bool children_p = false;
	enum json_type jtype;
	const char *str;
	bool pb_top = false;
	bool increment;

	/* Check object's FRR flags */
	parent_flags = frr_json_get_flags(jobj);
	jtype = json_object_get_type(jobj);

	/* We only expect to be here for object/dict and array types */
	assert(jtype == json_type_object || jtype == json_type_array);

	/* At top-level we might need to allocate a buffer */
	if (pb == NULL) {
		pb = printbuf_new();
		pb_top = true;
	}

	/* If needed, emit the "start" json marker */
	if (!CHECK_FLAG(parent_flags, FRR_JSON_STARTED)) {
		if (jtype == json_type_array)
			printbuf_strappend(pb, "[");
		else if (jtype == json_type_object)
			printbuf_strappend(pb, "{" /*}*/);

		SET_FLAG(parent_flags, FRR_JSON_STARTED);
		frr_json_set_data(jobj, parent_flags);
	}

	/* Iterate through the children of 'jobj', output singletons,
	 * recurse into collections.
	 */
	if (jtype == json_type_object) {
		lhtable = json_object_get_object(jobj);
		lhentry = lhtable->head;
		while (lhentry) {
			jval = (struct json_object *)lh_entry_v(lhentry);
			key = (char *)lh_entry_k(lhentry);

			child_flags = frr_json_get_flags(jval);

			if (CHECK_FLAG(parent_flags, FRR_JSON_COLLECTION))
				children_p = true;

			/* Print key for new child */
			if (!CHECK_FLAG(child_flags, FRR_JSON_STARTED)) {
				if (children_p)
					printbuf_strappend(pb, ",");

				SET_FLAG(parent_flags, FRR_JSON_COLLECTION);
				frr_json_set_data(jobj, parent_flags);

				if (jflags & JSON_C_TO_STRING_PRETTY)
					printbuf_strappend(pb, "\n");
				if (jflags & JSON_C_TO_STRING_SPACED &&
				    !(jflags & JSON_C_TO_STRING_PRETTY))
					printbuf_strappend(pb, " ");

				frr_json_indent(pb, level + 1, jflags);

				printbuf_strappend(pb, "\"");
				frr_json_escape_str(pb, key, strlen(key), jflags);
				printbuf_strappend(pb, "\"");

				if (jflags & JSON_C_TO_STRING_SPACED)
					printbuf_strappend(pb, ": ");
				else
					printbuf_strappend(pb, ":");
			}

			if (CHECK_FLAG(child_flags, FRR_JSON_OPEN)) {
				/* Recurse into child */
				frr_json_obj_to_vty(vty, jval, pb, level + 1, jflags);
				lhentry = lhentry->next;
			} else if (CHECK_FLAG(child_flags, FRR_JSON_CLOSED)) {
				/* Recurse into child */
				frr_json_obj_to_vty(vty, jval, pb, level + 1, jflags);

				/* Remove child after output */
				lh_table_delete_entry(lhtable, lhentry);
				lhentry = lhtable->head;
			} else {
				/* Flush any pending output */
				vty_out(vty, "%s", pb->buf);
				printbuf_reset(pb);

				/* Use child object's buffer, flush vty output */
				str = json_object_to_json_string_ext(jval, jflags);
				vty_out(vty, "%s", str);

				/* Remove child after output */
				lh_table_delete_entry(lhtable, lhentry);
				lhentry = lhtable->head;
			}
		}

		/* Close json output if object is not "OPEN" */
		if (!CHECK_FLAG(parent_flags, FRR_JSON_OPEN) ||
		    CHECK_FLAG(parent_flags, FRR_JSON_CLOSED)) {
			if ((jflags & JSON_C_TO_STRING_PRETTY) && children_p) {
				printbuf_strappend(pb, "\n");
				frr_json_indent(pb, level, jflags);
			}
			if (jflags & JSON_C_TO_STRING_SPACED && !(jflags & JSON_C_TO_STRING_PRETTY))
				printbuf_strappend(pb, /*{*/ " }");
			else
				printbuf_strappend(pb, /*{*/ "}");
		}
	} else {
		/* Array/list type */
		idx = 0;
		while (idx < json_object_array_length(jobj)) {
			increment = true;
			jval = json_object_array_get_idx(jobj, idx);

			if (children_p)
				printbuf_strappend(pb, ",");
			children_p = true;

			if (jflags & JSON_C_TO_STRING_PRETTY)
				printbuf_strappend(pb, "\n");
			if (jflags & JSON_C_TO_STRING_SPACED &&
			    !(jflags & JSON_C_TO_STRING_PRETTY))
				printbuf_strappend(pb, " ");
			frr_json_indent(pb, level + 1, jflags);

			if (jval == NULL) {
				printbuf_strappend(pb, "null");
				continue;
			}

			child_flags = frr_json_get_flags(jval);
			if (CHECK_FLAG(child_flags, FRR_JSON_OPEN)) {
				/* Recurse into child */
				frr_json_obj_to_vty(vty, jval, pb, level + 1, jflags);
			} else if (CHECK_FLAG(child_flags, FRR_JSON_CLOSED)) {
				/* Recurse into child */
				frr_json_obj_to_vty(vty, jval, pb, level + 1, jflags);

				/* Remove child after output */
				json_object_array_del_idx(jobj, idx, 1);
				increment = false;
			} else {
				/* Flush any pending output */
				vty_out(vty, "%s", pb->buf);
				printbuf_reset(pb);

				/* Use child object's buffer, flush vty output */
				str = json_object_to_json_string_ext(jval, jflags);
				vty_out(vty, "%s", str);

				/* Remove child after output */
				json_object_array_del_idx(jobj, idx, 1);
				increment = false;
			}

			if (increment)
				idx++;
		}

		/* Close jobj output if not set OPEN */
		if (!CHECK_FLAG(parent_flags, FRR_JSON_OPEN) ||
		    CHECK_FLAG(parent_flags, FRR_JSON_CLOSED)) {
			if ((jflags & JSON_C_TO_STRING_PRETTY) && children_p) {
				printbuf_strappend(pb, "\n");
				frr_json_indent(pb, level, jflags);
			}

			if (jflags & JSON_C_TO_STRING_SPACED && !(jflags & JSON_C_TO_STRING_PRETTY))
				printbuf_strappend(pb, " ]");
			else
				printbuf_strappend(pb, "]");
		}
	}

	/* Flush and free intermediate output buffer */
	if (pb_top) {
		vty_out(vty, "%s\n", pb->buf);
		printbuf_free(pb);
	}

	return 1;
}

/*
 * json_vty_helper — frr_json_vty_out 与其 bare 变体共用的输出助手。
 * vty：输出目标；jobj：待输出对象；flags：json 输出标志。
 * 若对象处于增量状态（OPEN / CLOSED）则走 frr_json_obj_to_vty，
 * 否则整棵树一次性序列化输出；两种情况都会释放 jobj。
 */
/* Common helper for frr json output */
static void json_vty_helper(struct vty *vty, struct json_object *jobj, int flags)
{
	int frrflags;
	const char *text;

	frrflags = frr_json_get_flags(jobj);

	/* We have special handling when we're supporting incremental output. */
	if (CHECK_FLAG(frrflags, FRR_JSON_OPEN) || CHECK_FLAG(frrflags, FRR_JSON_CLOSED)) {
		frr_json_obj_to_vty(vty, jobj, NULL, 0, flags);

		/* If we're done with incremental output, free the top-level object */
		if (CHECK_FLAG(frrflags, FRR_JSON_CLOSED))
			json_object_free(jobj);
	} else {
		/* Just produce the entire output using the json lib */
		text = json_object_to_json_string_ext(jobj, flags);
		vty_out(vty, "%s\n", text);
		json_object_free(jobj);
	}
}

/*
 * frr_json_vty_out — 把 json 对象以"美化排版"形式输出到 vty 并释放。
 * 使用的标志为 PRETTY 加 NOSLASHESCAPE。
 */

/*
 * FRR print/string-output function for vty output
 */
void frr_json_vty_out(struct vty *vty, struct json_object *jobj)
{
	int flags;

	flags = JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE;
	json_vty_helper(vty, jobj, flags);
}

/*
 * frr_json_vty_out_bare — 紧凑（非美化）形式的 vty 输出，仅带 NOSLASHESCAPE。
 */

/*
 * No-pretty/bare output variant
 */
void frr_json_vty_out_bare(struct vty *vty, struct json_object *jobj)
{
	int flags;

	flags = JSON_C_TO_STRING_NOSLASHESCAPE;
	json_vty_helper(vty, jobj, flags);
}

/*
 * vty_json_dump_helper — 递归打印 json 树轮廓（键名、类型与标志位）。
 * vty：输出目标；jobj：当前对象；level：缩进层级。
 * 只关心 object 与 array 类型；每个容器打印一行形如 OBJ(O X S C) 的标志，
 * 并递归其子容器。用于调试 show json 内部结构。
 */

/*
 * Helper function to dump an outline of the json tree
 */
static void vty_json_dump_helper(struct vty *vty, struct json_object *jobj, int level)
{
	int flags;
	size_t idx;
	const char *key;
	struct json_object *jval;
	struct lh_table *lhtable;
	struct lh_entry *lhentry;
	enum json_type jtype;
	const char *typestr;

	jtype = json_object_get_type(jobj);

	/* We're only interested in object/dict and array types */
	if (jtype != json_type_object && jtype != json_type_array)
		return;

	if (jtype == json_type_array)
		typestr = "ARRAY";
	else
		typestr = "OBJ";

	flags = frr_json_get_flags(jobj);
	vty_out(vty, "%*c%s (%s%s%s%s)\n", level * 2, ' ', typestr,
		CHECK_FLAG(flags, FRR_JSON_OPEN) ? "O" : "",
		CHECK_FLAG(flags, FRR_JSON_CLOSED) ? "X" : "",
		CHECK_FLAG(flags, FRR_JSON_STARTED) ? "S" : "",
		CHECK_FLAG(flags, FRR_JSON_COLLECTION) ? "C" : "");

	level += 1;

	/* Iterate through the children of 'jobj', recurse into collections. */
	if (jtype == json_type_object) {
		lhtable = json_object_get_object(jobj);
		lhentry = lhtable->head;
		for (; lhentry; lhentry = lhentry->next) {
			jval = (struct json_object *)lh_entry_v(lhentry);
			key = (char *)lh_entry_k(lhentry);

			/* We're only interested in object/dict and array types */
			jtype = json_object_get_type(jval);
			if (jtype != json_type_object && jtype != json_type_array)
				continue;

			/* Print child's key */
			vty_out(vty, "%*c%s: ", level * 2, ' ', key);

			/* Print child and its children */
			vty_json_dump_helper(vty, jval, level);
		}
	} else {
		/* Array/list type */
		for (idx = 0; idx < json_object_array_length(jobj); idx++) {
			/* We're only interested in object/dict and array types */
			jval = json_object_array_get_idx(jobj, idx);
			if (jval == NULL)
				continue;

			jtype = json_object_get_type(jval);
			if (jtype != json_type_object && jtype != json_type_array)
				continue;

			/* Print child's key */
			vty_out(vty, "%*c[%zu] ", level * 2, ' ', idx);

			/* Print child and its children */
			vty_json_dump_helper(vty, jval, level);
		}
	}
}

/*
 * frr_vty_json_dump — 从根对象开始打印整棵 json 树的轮廓。
 */

/*
 * Dump an outline of the json hierarchy to vty: print out the keys and types
 * of container objects at each level.
 */
void frr_vty_json_dump(struct vty *vty, struct json_object *jobj)
{
	/* Recurse down the tree... */
	vty_json_dump_helper(vty, jobj, 0);
}

/*
 * Flag that an object (a collection) is not yet complete; don't emit end-of-collection
 * text yet, as more children may be added.
 */

/*
 * frr_json_set_open — 标记对象尚未完成（增量输出），暂不输出集合结束符。
 * jobj：目标对象；副作用：在 userdata 中置上 FRR_JSON_OPEN 标志。
 */
void frr_json_set_open(struct json_object *jobj)
{
	int frrflags;

	frrflags = frr_json_get_flags(jobj);
	SET_FLAG(frrflags, FRR_JSON_OPEN);

	frr_json_set_data(jobj, frrflags);
}

/*
 * Indicate that an object is complete; during the next output function call,
 * emit end-of-collection text, and free the object
 */

/*
 * frr_json_set_complete — 标记对象已完成：把 OPEN 转成 CLOSED；下一次
 * frr_json_vty_out 会输出集合结束符并释放该对象。
 * jobj：目标对象；副作用：更新 userdata 中的标志位。
 * 注意：只有原本为 OPEN 的对象才会被置上 CLOSED。
 */
void frr_json_set_complete(struct json_object *jobj)
{
	int frrflags;

	frrflags = frr_json_get_flags(jobj);

	if (CHECK_FLAG(frrflags, FRR_JSON_OPEN))
		SET_FLAG(frrflags, FRR_JSON_CLOSED);

	UNSET_FLAG(frrflags, FRR_JSON_OPEN);

	frr_json_set_data(jobj, frrflags);
}

/*
 * 以下为 libjson-c 版本较老、不支持增量输出接口时的降级实现：
 * 通过给对象挂自定义序列化器来模拟 OPEN / CLOSED 语义。
 */
#else /* Older libjson-c version, no incremental support */

/*
 * FRR_OPEN_STR — 降级实现中用来标记"未完成"对象的序列化特殊值。
 */
#define FRR_OPEN_STR  "frr"
/*
 * FRR_CLOSE_STR — 降级实现中用来标记"已完成"对象的序列化特殊值。
 */
#define FRR_CLOSE_STR " "

/*
 * json_vty_helper（降级实现）— 借用对象序列化出的文本是否等于 FRR_OPEN_STR
 * 来判断该对象是否仍处于"未完成"状态；未完成则不输出，完成则整棵输出并释放。
 */
static void json_vty_helper(struct vty *vty, struct json_object *jobj, int flags)
{
	struct printbuf *pb;
	const char *text;

	pb = printbuf_new();

	json_object_userdata_to_json_string(jobj, pb, 0, 0);

	/* Ignore 'open' object; emit complete output otherwise */
	if (strcmp(pb->buf, FRR_OPEN_STR) != 0) {
		/* Just produce the entire output using the json lib */
		text = json_object_to_json_string_ext(jobj, flags);
		vty_out(vty, "%s\n", text);
		json_object_free(jobj);
	}

	printbuf_free(pb);
}

/*
 * frr_json_vty_out（降级实现）— 美化排版输出到 vty。
 */
void frr_json_vty_out(struct vty *vty, struct json_object *jobj)
{
	json_vty_helper(vty, jobj,
			JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
}

/*
 * frr_json_vty_out_bare（降级实现）— 紧凑输出到 vty。
 */
void frr_json_vty_out_bare(struct vty *vty, struct json_object *jobj)
{
	json_vty_helper(vty, jobj, JSON_C_TO_STRING_NOSLASHESCAPE);
}

/*
 * frr_vty_json_dump（降级实现）— 老版本无法展示树轮廓，仅输出空对象。
 */
void frr_vty_json_dump(struct vty *vty, struct json_object *jobj)
{
	vty_out(vty, "{}\n");
}

/*
 * frr_jobj_del_func — json 对象 userdata 的销毁回调，释放堆分配的标记字符串。
 * 注意：删除函数（第三个参数）为 NULL 时，userdata 不会被释放。
 */
/* Delete helper function for json object userdata */
static void frr_jobj_del_func(struct json_object *jobj, void *data)
{
	if (data)
		free(data);
}

/*
 * Flag that an object (a collection) is not yet complete; don't emit end-of-collection
 * text yet, as more children may be added.
 */

/*
 * frr_json_set_open（降级实现）— 给对象挂一个特殊序列化器，把它标记为
 * "未完成"，序列化时被识别为 OPEN，从而暂不输出结束符。
 */
void frr_json_set_open(struct json_object *jobj)
{
	char *str = malloc(4);

	strlcpy(str, FRR_OPEN_STR, 4);
	json_object_set_serializer(jobj, NULL, str, frr_jobj_del_func);
}

/*
 * Indicate that an object is complete; during the next output function call,
 * emit end-of-collection text, and free the object and its children.
 */

/*
 * frr_json_set_complete（降级实现）— 用特殊序列化器把对象标记为"已完成"，
 * 序列化时按正常对象输出，并释放其子对象。
 */
void frr_json_set_complete(struct json_object *jobj)
{
	char *str = malloc(4);

	strlcpy(str, FRR_CLOSE_STR, 4);
	json_object_set_serializer(jobj, NULL, str, frr_jobj_del_func);
}

#endif /* libjson-c version dependencies */
