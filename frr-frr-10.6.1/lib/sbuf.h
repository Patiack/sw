// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Simple string buffer
 *
 * Copyright (C) 2017 Christian Franke
 */
#ifndef SBUF_H
#define SBUF_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * sbuf provides a simple string buffer. One application where this comes
 * in handy is the parsing of binary data: If there is an error in the parsing
 * process due to invalid input data, printing an error message explaining what
 * went wrong is definitely useful. However, just printing the actual error,
 * without any information about the previous parsing steps, is usually not very
 * helpful.
 * Using sbuf, the parser can log the whole parsing process into a buffer using
 * a printf like API. When an error occurs, all the information about previous
 * parsing steps is there in the log, without any need for backtracking, and can
 * be used to give a detailed and useful error description.
 * When parsing completes successfully without any error, the log can just be
 * discarded unless debugging is turned on, to not spam the log.
 *
 * For the described usecase, the code would look something like this:
 *
 * int sbuf_example(..., char **parser_log)
 * {
 *         struct sbuf logbuf;
 *
 *         sbuf_init(&logbuf, NULL, 0);
 *         sbuf_push(&logbuf, 0, "Starting parser\n");
 *
 *         int rv = do_parse(&logbuf, ...);
 *
 *         *parser_log = sbuf_buf(&logbuf);
 *
 *         return 1;
 * }
 *
 * In this case, sbuf_example uses a string buffer with undefined size, which
 * will
 * be allocated on the heap by sbuf. The caller of sbuf_example is expected to
 * free
 * the string returned in parser_log.
 */

/* ============================================================================
 * 【文件导读】sbuf.h —— “简单字符串缓冲”的接口与结构体
 * ============================================================================
 * 【先读上面那段长注释】它是作者写的使用说明书，核心场景一句话概括：
 * **解析器边解析边把过程写进 sbuf，出错时整段日志已就绪，成功时丢弃**。
 * 这样定位错误时既有“错在哪”，也有“之前发生了什么”，且不需要回溯重放。
 *
 * 【两种模式的接口差别(使用时必须先决定)】
 *   动态: sbuf_init(&s, NULL, 0)   —— 由 sbuf 自己 malloc，用完必须 sbuf_free。
 *   固定: sbuf_init(&s, buf, n)    —— 用调用方的缓冲区，**绝不扩容**，写满会断言。
 * 头文件示例代码用的是动态模式，并在最后把 sbuf_buf() 得到的字符串**交出
 * 所有权**(由调用方 free)。这就是为什么 sbuf_free 与“交出去的字符串”是两回事。
 *
 * 【结构体字段一览】
 *   fixed : 是否固定模式(决定 free 与 realloc 行为，全文件的分水岭)。
 *   buf   : 缓冲区基址(内部指针，勿长期缓存——push 可能使其失效)。
 *   size  : 缓冲区总容量(字节，含结尾 NUL)。
 *   pos   : 当前已写入长度(下次追加的偏移)。
 *   indent: 预留的缩进字段；注意 **当前实现里 sbuf_push 用的是自己的形参
 *           indent，并未读写本字段**，可视为历史遗留/预留。
 *
 * 【接口清单与配对关系】
 *   sbuf_init   —— 建
 *   sbuf_reset  —— 清空内容、保留容量(复用场景)
 *   sbuf_buf    —— 取只读指针(注意失效时机)
 *   sbuf_free   —— 拆(动态模式才真正释放)
 *   sbuf_push   —— 写(变参，带缩进；fmt 参数受 PRINTFRR(3,4) 编译期检查)
 * ============================================================================
 */
#define SBUF_DEFAULT_SIZE 8192

/* 【结构体】sbuf 的全部状态。字段含义见上方文件导读。
 * 注意 indent 字段在当前实现中**未被 sbuf_push 使用**(它用的是形参)，
 * 阅读 .c 时不要被它误导。 */
struct sbuf {
	bool fixed;
	char *buf;
	size_t size;
	size_t pos;
	int indent;
};

/* 【接口】初始化：size > 0 为固定模式，size == 0 为动态模式(细节见 sbuf.c)。 */
void sbuf_init(struct sbuf *dest, char *buf, size_t size);
/* 【接口】清空内容并保留容量与模式，用于复用同一 sbuf 处理多轮解析。 */
void sbuf_reset(struct sbuf *buf);
/* 【接口】取内部缓冲区的只读指针；**后续 push 可能使其失效**，勿长期缓存。 */
const char *sbuf_buf(struct sbuf *buf);
/* 【接口】释放；仅动态模式真正 free，固定模式下是空操作。 */
void sbuf_free(struct sbuf *buf);
#include "lib/log.h"
/* 【接口】追加一行(printf 风格 + 缩进)。
 * PRINTFRR(3, 4) 是 FRR 的格式串检查宏：告诉编译器“第 3 个参数是格式串、
 * 第 4 个起是变参”，从而在编译期校验实参类型——FRR 自定义的 %pFX 等扩展格式
 * 也靠它识别。注意第 3 个参数是 buffer+indent+format 里的 format(它自己另占
 * 一处)，这正是写成 (3,4) 而非 (2,3) 的原因，改动参数顺序务必同步修改。 */
void sbuf_push(struct sbuf *buf, int indent, const char *format, ...)
	PRINTFRR(3, 4);

#ifdef __cplusplus
}
#endif

#endif
