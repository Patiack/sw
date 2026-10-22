// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR string processing utilities.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 *                     Quentin Young
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 */

/*
 * 文件导读：frrstr.h —— FRR 字符串处理工具集
 *
 * 本头文件声明了 libfrr 中通用的字符串与字符串向量处理函数，被各守护进程与 vtysh 广泛使用。
 *
 * 内容大致分三类：
 *   分割与拼接：frrstr_split、frrstr_split_vec 把串拆成词；frrstr_join、frrstr_join_vec 反向拼接。
 *   向量工具：frrstr_filter_vec 用正则过滤行、frrstr_strvec_free 释放字符串向量。
 *   匹配与小工具：frrstr_replace、frrstr_startswith、frrstr_endswith、all_digit、frrstr_hex、
 *                 frrstr_skip_over_char、frrstr_back_to_char。
 *
 * 内存约定：frrstr_split、frrstr_join、frrstr_replace 返回的串都以 MTYPE_TMP 分配，
 * 由调用者释放；frrstr_skip_over_char 与 frrstr_back_to_char 只返回原串内的指针，不分配内存。
 *
 * 依赖关系：依赖 vector.h 提供的字符串向量，实现全部在 frrstr.c；正则部分通过不透明类型
 * struct frregex 屏蔽 libc 与 libpcre 系列之间的差异。
 */
#ifndef _FRRSTR_H_
#define _FRRSTR_H_

#include <sys/types.h>
#include <stdbool.h>

#include "vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* encapsulate libc/libpcre/libpcre2 regex variability with an opaque type */
struct frregex;

/* frrstr_split / frrstr_split_vec：把字符串按分隔符拆成词。
 * 关键参数：string 待拆分串；delimiter 分隔符集合（用法同 strsep）。
 * frrstr_split 通过 result 传出词数组、argc 传出词数；frrstr_split_vec 返回 vector。
 * 返回：每个词以 MTYPE_TMP 分配，数组与词由调用者释放；空白被忽略、分隔符不保留。 */
/*
 * Tokenizes a string, storing tokens in a vector. Whitespace is ignored.
 * Delimiter characters are not included.
 *
 * string
 *    The string to split
 *
 * delimiter
 *    Delimiter string, as used in strsep()
 *
 * Returns:
 *    The split string. Each token is allocated with MTYPE_TMP.
 */
void frrstr_split(const char *string, const char *delimiter, char ***result,
		  int *argc);
vector frrstr_split_vec(const char *string, const char *delimiter);

/* frrstr_join / frrstr_join_vec：把字符串指针数组拼成一个字符串。
 * 关键参数：parts 指针数组；argc 数组长度；join 段间连接串（NULL 表示不插入）。
 * 返回：以 MTYPE_TMP 分配的新串，需调用者释放。 */
/*
 * Concatenate string array into a single string.
 *
 * argv
 *    array of string pointers to concatenate
 *
 * argc
 *    array length
 *
 * join
 *    string to insert between each part, or NULL for nothing
 *
 * Returns:
 *    the joined string, allocated with MTYPE_TMP
 */
char *frrstr_join(const char **parts, int argc, const char *join);
char *frrstr_join_vec(vector v, const char *join);

/* frrstr_filter_vec：过滤字符串向量，删除所有不匹配给定正则的行，就地对 v 修改。
 * 关键参数：v 待过滤向量；filter 用 struct frregex 表示的正则。 */
/*
 * Filter string vector.
 * Removes lines that do not contain a match for the provided regex.
 *
 * v
 *    The vector to filter.
 *
 * filter
 *    Regex to filter with.
 */
void frrstr_filter_vec(vector v, struct frregex *filter);

/* frrstr_strvec_free：释放字符串向量，假定每个元素都以 MTYPE_TMP 分配，
 * 逐个释放元素后再释放向量本身。 */
/*
 * Free allocated string vector.
 * Assumes each item is allocated with MTYPE_TMP.
 *
 * v
 *    the vector to free
 */
void frrstr_strvec_free(vector v);

/* frrstr_replace：在 str 中把所有出现的 find 子串替换为 replace，返回新串（原串不改）。
 * 关键参数：str 源串；find 被替换子串；replace 替换目标串。
 * 返回：以 MTYPE_TMP 分配的新串，需调用者释放。
 * 坑：replace 比 find 长时做 N+1 次分配（N 为出现次数），否则只分配 1 次。 */
/*
 * Given a string, replaces all occurrences of a substring with a different
 * string. The result is a new string. The original string is not modified.
 *
 * If 'replace' is longer than 'find', this function performs N+1 allocations,
 * where N is the number of times 'find' occurs in 'str'. If 'replace' is equal
 * in length or shorter than 'find', only 1 allocation is performed.
 *
 * str
 *    String to perform replacement on.
 *
 * find
 *    Substring to replace.
 *
 * replace
 *    String to replace 'find' with.
 *
 * Returns:
 *    A new string, allocated with MTYPE_TMP, that is the result of performing
 *    the replacement on 'str'. This must be freed by the caller.
 */
char *frrstr_replace(const char *str, const char *find, const char *replace);

/* frrstr_startswith：前缀匹配，str 是否以 prefix 开头，是返回 true 否则 false */
/*
 * Prefix match for string.
 *
 * str
 *    string to check for prefix match
 *
 * prefix
 *    prefix to look for
 *
 * Returns:
 *   true if str starts with prefix, false otherwise
 */
bool frrstr_startswith(const char *str, const char *prefix);

/* frrstr_endswith：后缀匹配，str 是否以 suffix 结尾，是返回 true 否则 false */
/*
 * Suffix match for string.
 *
 * str
 *    string to check for suffix match
 *
 * suffix
 *    suffix to look for
 *
 * Returns:
 *   true if str ends with suffix, false otherwise
 */
bool frrstr_endswith(const char *str, const char *suffix);

/* all_digit：判断字符串是否只由数字字符组成，全是数字返回 1 否则返回 0 */
/*
 * Check the string only contains digit characters.
 *
 * str
 *    string to check for digits
 *
 * Returns:
 *    1 str only contains digit characters, 0 otherwise
 */
int all_digit(const char *str);

/* frrstr_hex：把字符串的十六进制表示写进 buff。
 * 关键参数：buff 目标缓冲（至少 2*num+1 字节）；bufsiz 缓冲大小；str 源数据；num 处理字节数。
 * 返回：指向 buff 的指针。坑：调用者须保证缓冲足够大，否则溢出。 */
/*
 * Copy the hexadecimal representation of the string to a buffer.
 *
 * buff
 *    Buffer to copy result into with size of at least (2 * num) + 1.
 *
 * bufsiz
 *    Size of destination buffer.
 *
 * str
 *    String to represent as hexadecimal.
 *
 * num
 *    Number of characters to copy.
 *
 * Returns:
 *    Pointer to buffer containing resulting hexadecimal representation.
 */
char *frrstr_hex(char *buff, size_t bufsiz, const uint8_t *str, size_t num);

/* frrstr_skip_over_char：从 s 起跳过 skipc 字符，尊重引号与反斜杠转义
 * （引号内的 skipc 不算）。返回指向串内位置的指针，不分配内存。 */
/*
 * Advance past a given char `skipc` in a string, while honoring quoting and
 * backslash escapes (i.e., ignore `skipc` which occur in quoted sections).
 */
const char *frrstr_skip_over_char(const char *s, int skipc);

/* frrstr_back_to_char：从串尾向前回退到 toc 字符，尊重引号与转义
 * （引号内或被转义的 toc 忽略）。返回指向串内位置的指针，不分配内存。 */
/*
 * Advance back from end to a given char `toc` in a string, while honoring
 * quoting and backslash escapes. `toc` chars inside quote or escaped are
 * ignored.
 */
const char *frrstr_back_to_char(const char *s, int toc);

#ifdef __cplusplus
}
#endif

#endif /* _FRRSTR_H_ */
