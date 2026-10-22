// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR string processing utilities.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 *                     Quentin Young
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 */

/*
 * 文件导读：frrstr.c —— frrstr.h 中字符串工具的实现
 *
 * 本文件实现 frrstr.h 声明的字符串与字符串向量工具，是守护进程与 vtysh 处理
 * 配置及命令输出时的高频底层。要点如下：
 *
 *   frrstr_split / frrstr_split_vec：用 strsep 按分隔符拆词。split 先把原串复制一份
 *     （strsep 会改动原串），结果数组动态扩容；split_vec 在其基础上转成 vector。
 *   拼接 frrstr_join / frrstr_join_vec：先算出总长度一次性分配，再逐段拷贝并插入分隔串，
 *     避免反复 realloc。
 *   frrstr_replace：在副本上做子串替换，用 memmove 腾挪尾部；替换串更长时每轮按需扩容。
 *   frrstr_skip_over_char / frrstr_back_to_char：带引号与反斜杠转义感知的字符查找，
 *     frrstr_back_to_char 的反向扫描正是反复调用 forward 版实现的。
 *
 * 内存约定：所有返回的串、数组、词都以 MTYPE_TMP 分配，调用者负责释放；
 * 本文件只负责分配，释放交由调用方（见 frrstr_strvec_free 等）。
 *
 * 依赖：memory.h 提供 MTYPE 与 XSTRDUP / XMALLOC 等，vector.h 提供向量，
 * frregex_real.h 暴露 struct frregex 的真实定义（头文件里它是不透明类型）。
 */
#include "zebra.h"

#include "frrstr.h"
#include "memory.h"
#include "vector.h"

#include "frregex_real.h"

/* 实现：按 delimiter 把 string 拆成词数组。
 * 做法：先 XSTRDUP 复制一份（strsep 会就地改写），初始 4 个槽位，满了就翻倍扩容；
 *       每取出一个词再 XSTRDUP 一份存入。
 * 返回：*result 指向词数组、*argc 为词数；string 为 NULL 时直接返回（不改输出）。
 * 坑：调用者须释放数组与每个词；strsep 往往在末尾多产生一个空词，argc 可能比预期多。 */
void frrstr_split(const char *string, const char *delimiter, char ***result,
		  int *argc)
{
	if (!string)
		return;

	unsigned int sz = 4, idx = 0;
	char *copy, *copystart;
	*result = XCALLOC(MTYPE_TMP, sizeof(char *) * sz);
	copystart = copy = XSTRDUP(MTYPE_TMP, string);
	*argc = 0;

	const char *tok = NULL;

	while (copy) {
		tok = strsep(&copy, delimiter);
		(*result)[idx] = XSTRDUP(MTYPE_TMP, tok);
		if (++idx == sz)
			*result = XREALLOC(MTYPE_TMP, *result,
					   (sz *= 2) * sizeof(char *));
		(*argc)++;
	}

	XFREE(MTYPE_TMP, copystart);
}

/* 实现：frrstr_split 的向量封装。
 * 先拆成数组，再 array_to_vector 转成 vector，随后释放临时数组。
 * 返回：vector；string 为 NULL 时返回 NULL。
 * 坑：vector 内每个元素仍需调用者按 MTYPE_TMP 释放。 */
vector frrstr_split_vec(const char *string, const char *delimiter)
{
	char **result;
	int argc;

	if (!string)
		return NULL;

	frrstr_split(string, delimiter, &result, &argc);

	vector v = array_to_vector((void **)result, argc);

	XFREE(MTYPE_TMP, result);

	return v;
}

/* 实现：把 parts 数组用 join 连接成新串。
 * 做法：先累加各段长度与分隔符总长算出 len，一次性 XMALLOC，再逐段 memcpy，
 *       最后补结束符；只有最后一段之后不插分隔符。
 * 返回：新串（MTYPE_TMP）；argc 为 0 或 len 为 0 时返回 NULL。
 * 坑：parts[i] 不能为 NULL，否则 strlen 崩溃。 */
char *frrstr_join(const char **parts, int argc, const char *join)
{
	int i;
	char *str;
	char *p;
	size_t len = 0;
	size_t joinlen = join ? strlen(join) : 0;

	if (!argc)
		return NULL;

	for (i = 0; i < argc; i++)
		len += strlen(parts[i]);
	len += argc * joinlen + 1;

	if (!len)
		return NULL;

	p = str = XMALLOC(MTYPE_TMP, len);

	for (i = 0; i < argc; i++) {
		size_t arglen = strlen(parts[i]);

		memcpy(p, parts[i], arglen);
		p += arglen;
		if (i + 1 != argc && join) {
			memcpy(p, join, joinlen);
			p += joinlen;
		}
	}

	*p = '\0';

	return str;
}

/* 实现：frrstr_join 的向量封装。
 * 先把 vector 转成临时指针数组，调用 frrstr_join，再释放临时数组。
 * 返回：拼接后的新串（MTYPE_TMP），由调用者释放。 */
char *frrstr_join_vec(vector v, const char *join)
{
	char **argv;
	int argc;

	vector_to_array(v, (void ***)&argv, &argc);

	char *ret = frrstr_join((const char **)argv, argc, join);

	XFREE(MTYPE_TMP, argv);

	return ret;
}

/* 实现：就地从向量 v 中删除不匹配 filter 的元素。
 * 做法：逐个 vector_slot 用 regexec 测试；不匹配则 XFREE 该元素并 vector_unset 置空。
 * 关键参数：filter 的 struct frregex 真实定义由 frregex_real.h 给出，real 是真正的正则对象。
 * 坑：只释放元素并置空，向量容量不变；空槽由 vector_active 的语义自然跳过。 */
void frrstr_filter_vec(vector v, struct frregex *filter)
{
	regmatch_t ignored[1];

	for (unsigned int i = 0; i < vector_active(v); i++) {
		if (regexec(&filter->real, vector_slot(v, i), 0, ignored, 0)) {
			XFREE(MTYPE_TMP, vector_slot(v, i));
			vector_unset(v, i);
		}
	}
}

/* 实现：释放字符串向量。
 * 做法：遍历每个槽位 XFREE（按 MTYPE_TMP），最后 vector_free 释放向量本身。
 * 参数：v 为 NULL 时直接返回。 */
void frrstr_strvec_free(vector v)
{
	unsigned int i;
	char *cp;

	if (!v)
		return;

	for (i = 0; i < vector_active(v); i++) {
		cp = vector_slot(v, i);
		XFREE(MTYPE_TMP, cp);
	}

	vector_free(v);
}

/* 实现：把 str 中所有 find 替换为 replace，返回新串（不改原串）。
 * 做法：先复制副本 nustr；循环 strstr 定位 find；若替换串更长则按需 XREALLOC 扩容，
 *       再用 memmove 把尾部（含结束符）左右腾挪，最后 memcpy 写入 replace。
 * 返回：新串（MTYPE_TMP），由调用者释放。
 * 坑：find 为空串时 strstr 会一直命中，可能导致死循环，调用者需保证 find 非空。 */
char *frrstr_replace(const char *str, const char *find, const char *replace)
{
	char *ch;
	char *nustr = XSTRDUP(MTYPE_TMP, str);

	size_t findlen = strlen(find);
	size_t repllen = strlen(replace);

	while ((ch = strstr(nustr, find))) {
		if (repllen > findlen) {
			size_t nusz = strlen(nustr) + repllen - findlen + 1;
			nustr = XREALLOC(MTYPE_TMP, nustr, nusz);
			ch = strstr(nustr, find);
		}

		size_t nustrlen = strlen(nustr);
		size_t taillen = (nustr + nustrlen) - (ch + findlen);

		memmove(ch + findlen + (repllen - findlen), ch + findlen,
			taillen + 1);
		memcpy(ch, replace, repllen);
	}

	return nustr;
}

/* 实现：判断 str 是否以 prefix 开头。
 * 返回：true 否则 false；任一参数为 NULL 或 prefix 比 str 长时返回 false。 */
bool frrstr_startswith(const char *str, const char *prefix)
{
	if (!str || !prefix)
		return false;

	size_t lenstr = strlen(str);
	size_t lenprefix = strlen(prefix);

	if (lenprefix > lenstr)
		return false;

	return strncmp(str, prefix, lenprefix) == 0;
}

/* 实现：判断 str 是否以 suffix 结尾。
 * 返回：true 否则 false；任一参数为 NULL 或 suffix 比 str 长时返回 false。 */
bool frrstr_endswith(const char *str, const char *suffix)
{
	if (!str || !suffix)
		return false;

	size_t lenstr = strlen(str);
	size_t lensuffix = strlen(suffix);

	if (lensuffix > lenstr)
		return false;

	return strncmp(&str[lenstr - lensuffix], suffix, lensuffix) == 0;
}

/* 实现：判断字符串是否全为数字字符。
 * 返回：全是数字返回 1，遇到非数字返回 0（空串视为全数字，返回 1）。 */
int all_digit(const char *str)
{
	for (; *str != '\0'; str++)
		if (!isdigit((unsigned char)*str))
			return 0;
	return 1;
}


/* 实现：把 str 的前 num 个字节以十六进制写入 buff。
 * 做法：逐字节 snprintf 成两位小写十六进制，再用 strlcat 追加（自动截断防溢出）。
 * 返回：buff 指针；bufsiz 为 0 时直接返回 buff（此时 buff[0] 未被写入）。 */
char *frrstr_hex(char *buff, size_t bufsiz, const uint8_t *str, size_t num)
{
	if (bufsiz == 0)
		return buff;

	char tmp[3];

	buff[0] = '\0';

	for (size_t i = 0; i < num; i++) {
		snprintf(tmp, sizeof(tmp), "%02x", (unsigned char)str[i]);
		strlcat(buff, tmp, bufsiz);
	}

	return buff;
}

/* 实现：从 s 起跳过 skipc 字符，同时识别引号与反斜杠转义。
 * 做法：遇到反斜杠则跳过下一个字符；处于引号内时忽略 skipc，直到遇到配对的引号；
 *       在引号外遇到 skipc 即返回其后一个字符的位置。
 * 返回：指向 skipc 之后的指针；未找到或反斜杠结尾返回 NULL。
 * 坑：不分配内存，返回的是原串内指针。 */
const char *frrstr_skip_over_char(const char *s, int skipc)
{
	int c, quote = 0;

	while ((c = *s++)) {
		if (c == '\\') {
			if (!*s++)
				return NULL;
			continue;
		}
		if (quote) {
			if (c == quote)
				quote = 0;
			continue;
		}
		if (c == skipc)
			return s;
		if (c == '"' || c == '\'')
			quote = c;
	}
	return NULL;
}

/* 实现：从串尾向前回退到 toc 字符，同样尊重引号与转义。
 * 做法：若串中不含引号/反斜杠，直接用 strrchr 定位；否则反复调用
 *       frrstr_skip_over_char 前向扫描，用 prev 记住每次命中之前的位置。
 * 返回：指向目标位置的指针；空串返回 NULL，找不到返回 NULL。 */
/*
 * Advance backward in string until reaching the char `toc`
 * if beginning of string is reached w/o finding char return NULL
 *
 * /foo/bar'baz/booz'/foo
 */
const char *frrstr_back_to_char(const char *s, int toc)
{
	const char *next = s;
	const char *prev = NULL;

	if (s[0] == 0)
		return NULL;
	if (!strpbrk(s, "'\"\\"))
		return strrchr(s, toc);
	while ((next = frrstr_skip_over_char(next, toc)))
		prev = next - 1;
	return prev;
}

