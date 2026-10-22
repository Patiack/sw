// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Simple string buffer
 *
 * Copyright (C) 2017 Christian Franke
 */
#include <zebra.h>

/* ============================================================================
 * 【文件导读】sbuf.c —— 极简的“可自增长字符串缓冲”，主要给解析器攒日志用
 * ============================================================================
 * 【典型用途(见 sbuf.h 的长注释)】解析二进制报文时，如果中途失败，光报“哪一步
 * 错了”往往没用，得把**之前每一步**都打印出来。于是让解析过程随手把进度
 * 追加进一个 sbuf，出错时整段倒出来；成功时直接丢弃，零成本。这就是它存在的
 * 全部理由——不是通用字符串类库，而是“解析日志收集器”。
 *
 * 【两种工作模式(理解本文件的钥匙)】
 *   固定模式(fixed = true) : 调用方给了外部缓冲区，**绝不 realloc**，写满即
 *                            断言失败(见 sbuf_push 末尾的 assert)。
 *   动态模式(fixed = false): 缓冲区由本文件在堆上管理，空间不够时**成倍增长**，
 *                            由 sbuf_free() 负责释放。
 *   fixed 这个标志贯穿所有函数，凡涉及“要不要释放/能不能扩容”的地方都要回头看它。
 *
 * 【堆上分配用 MTYPE_TMP】说明作者把它归入“临时内存”类别，暗示生命周期短、
 * 不应长期持有——配合头文件里那个“解析完就 free 掉字符串”的用法约定。
 *
 * 【阅读顺序】sbuf_init(两种模式的岔路口，也是全文件最重要的一处)
 * → sbuf_reset/free(生命周期) → sbuf_push(唯一的写入入口，注意它是变参)。
 * ============================================================================
 */
#include "printfrr.h"
#include "sbuf.h"
#include "memory.h"

/* 【函数】初始化一个 sbuf。
 *   dest: 待初始化的缓冲区结构(调用方提供，通常就在栈上)。
 *   buf : 外部缓冲区；size 为 0 时该参数**被忽略**，可为 NULL。
 *   size: 外部缓冲区容量；> 0 即进入固定模式，== 0 则改由本函数在堆上分配默认大小。
 * 【要点】判据是 `size > 0` 而不是 `buf != NULL`——也就是说“传了 buf 但 size 为 0”
 *   仍会走堆分配，这个 buf 指针不会被采用。反过来“size > 0 但 buf 为 NULL”会
 *   在 buf[0]='\0' 处崩溃，调用方必须保证两者配套。
 * 【易踩的坑】固定模式下若 size==1，则此后任何一次 push 都会立刻触发 assert；
 *   小缓冲区在调试用途上意义有限。 */
void sbuf_init(struct sbuf *dest, char *buf, size_t size)
{
	dest->fixed = (size > 0);
	if (dest->fixed) {
		dest->buf = buf;
		dest->size = size;
	} else {
		dest->buf = XMALLOC(MTYPE_TMP, SBUF_DEFAULT_SIZE);
		dest->size = SBUF_DEFAULT_SIZE;
	}

	dest->pos = 0;
	dest->buf[0] = '\0';
}

/* 【函数】清空内容但保留缓冲区(逻辑上置空，不归还内存)。
 *   dest: 目标 sbuf。
 *   返回: 无。
 * 【要点】只把 pos 归零、首字节写 '\0'，**既不改 size 也不改 fixed**——所以
 *   复位后模式与容量都不变，动态模式已扩容的空间仍然留用。这是“复用同一个
 *   sbuf 处理多轮解析”时的正确姿势：比反复 free/init 省一次分配。
 * 【易踩的坑】本函数不释放在堆上分配的缓冲区；要归还内存必须另调 sbuf_free()。 */
void sbuf_reset(struct sbuf *dest)
{
	dest->pos = 0;
	dest->buf[0] = '\0';
}

/* 【函数】取出内部缓冲区的裸指针(只读使用)。
 *   返回: 指向内部 C 字符串的指针，**所有权仍属于 sbuf**。
 * 【要点】返回的是内部指针而非拷贝——所以在动态模式下，只要再调用
 *   sbuf_push() 就可能触发 XREALLOC，使这个指针**立即失效**(悬垂)。
 *   正确用法：先 push 完所有内容，最后一次取出使用/strdup。
 * 【易踩的坑】把返回值缓存下来之后继续 push，是非常典型的一个错误。 */
const char *sbuf_buf(struct sbuf *buf)
{
	return buf->buf;
}

/* 【函数】释放缓冲区(sbuf 使用完毕时调用)。
 * 【要点】只有**动态模式**(!fixed)才真正 free——固定模式的缓冲区是调用方的，
 *   本文件无权释放。这就是为什么函数一开始就判 fixed。
 * 【易踩的坑】① 本函数不把 buf 指针置 NULL，free 后结构体里的 buf 仍是旧地址
 *   (悬垂)；若要继续用必须重新 sbuf_init。② 固定模式下调用它是**空操作**，
 *   不会报错也不会释放，别误以为“调了就安全了”。③ 与 sbuf_reset 的区别：
 *   reset 保留容量，free 归还容量。 */
void sbuf_free(struct sbuf *buf)
{
	if (!buf->fixed)
		XFREE(MTYPE_TMP, buf->buf);
}

/* 【函数】往缓冲区追加一行内容(唯一的写入入口，变参 printf 风格)。
 *   buf   : 目标 sbuf。
 *   indent: 本行左侧缩进的空格数，用于把解析日志排成树状层次。
 *   format/...: 变参格式串，走 FRR 自己的 vsnprintfrr(支持 %pFX 等扩展格式)。
 *   返回: 无。
 * 【执行流程(三段)】
 *   ① 动态模式下先“预算”：用 vsnprintfrr(NULL, 0, ...) **只算长度不输出**
 *      (传 NULL/0 是 C99 起规定的探测用法)，把 缩进 + 本行 + 已有内容 + 结尾 NUL
 *      的总需求算出来，然后**成倍**扩容直到放得下。注意是在 while 里持续 ×2，
 *      一次到位，避免反复 realloc。
 *   ② 先写缩进：用 "%*s" + 空串实现“输出 indent 个空格”的小技巧。
 *   ③ 再写正文，然后两次把 pos 夹到 size 以内(防御性截断)。
 * 【设计要点：为什么先“单独写缩进”再“单独写正文”】
 *   因为缩进与正文是两次独立格式化，各自维护 pos。这样每次 push 都**恰好追加
 *   一行**，不会出现前一行内容与后一行缩进黏连的问题。
 * 【易踩的坑(本函数最容易踩的点)】
 *   ① 末尾 `if (buf->pos == buf->size) assert(!"Buffer filled up!")` ——
 *      **写满即断言崩溃**，且是故意的：宁可在这里炸，也不要静默截断日志。
 *      固定模式下这是主要的风险点；动态模式理论上不会走到(容量按需翻倍)。
 *   ② vsnprintfrr 的返回值可能是负数(格式错误)，代码只在 >= 0 时才推进 pos，
 *      这是对的；但 **步骤① 的预算分支**里若 written1/written2 为负，会直接
 *      跳过扩容——此时随后照常写入，可能撞上缓冲区不足。
 *   ③ indent 若为负数，"%*s" 的负宽度含义变为“左对齐”而非“减空格”，行为与
 *      直觉不同。调用方应保证 indent >= 0。 */
void sbuf_push(struct sbuf *buf, int indent, const char *format, ...)
{
	va_list args;
	int written;

	if (!buf->fixed) {
		int written1, written2;
		size_t new_size;

		written1 = indent;
		va_start(args, format);
		written2 = vsnprintfrr(NULL, 0, format, args);
		va_end(args);

		new_size = buf->size;
		if (written1 >= 0 && written2 >= 0) {
			while (buf->pos + written1 + written2 >= new_size)
				new_size *= 2;
			if (new_size > buf->size) {
				buf->buf =
					XREALLOC(MTYPE_TMP, buf->buf, new_size);
				buf->size = new_size;
			}
		}
	}

	written = snprintf(buf->buf + buf->pos, buf->size - buf->pos, "%*s",
			   indent, "");

	if (written >= 0)
		buf->pos += written;
	if (buf->pos > buf->size)
		buf->pos = buf->size;

	va_start(args, format);
	written = vsnprintfrr(buf->buf + buf->pos, buf->size - buf->pos,
			      format, args);
	va_end(args);

	if (written >= 0)
		buf->pos += written;
	if (buf->pos > buf->size)
		buf->pos = buf->size;

	if (buf->pos == buf->size)
		assert(!"Buffer filled up!");
}
