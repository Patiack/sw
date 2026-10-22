// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2019  David Lamparter, for NetDEF, Inc.
 */

/* ============================================================================
 * 【文件导读】printf/glue.c —— 把 FreeBSD printf 实现胶合到 FRR
 * ============================================================================
 * 【它在 FRR 里干什么】printf/ 目录是 FRR 从 FreeBSD 引入的整份 printf 实现，
 * 本文件是最外层的适配层：把 vfprintf.c 里的核心格式化函数（vbprintfrr）包装成
 * FRR 对外使用的一组 API（bprintfrr / snprintfrr / asnprintfrr ...），并注册 FRR
 * 自定义的 %p 扩展转换符。
 *
 * 【核心概念】
 *   - struct fbuf：FRR 的格式化输出缓冲描述（buf 起点 / pos 当前写位置 / len 容量），
 *     所有输出都写进 fbuf，而不是 FILE。
 *   - 命名规律：
 *       v 前缀   = 接收 va_list（va_start / va_end 由调用者负责）；
 *       带 a     = asprintf 风格，空间不足时用 qmalloc 在堆上分配并返回指针；
 *       带 c     = 追加式（csnprintfrr），在已有字符串尾部继续格式化；
 *       末尾 rr  = FRR 扩展版（支持自定义 %p 转换）。
 *   - printfrr_ext_reg / printfrr_extp / printfrr_exti：%p 扩展的注册与派发。
 *
 * 【与其它模块的关系】依赖 printfrr.h（对外接口）与 printflocal.h（内部常量）；
 * 真正的格式化主循环在 vfprintf.c，位置参数解析在 printf-pos.c。
 *
 * 【阅读顺序】先看 snprintfrr / bprintfrr 等包装函数理解对外 API，再看底部的
 * printfrr_ext_reg 及两个内置扩展（FB / VA）理解 %p 扩展机制。
 * ============================================================================
 */

/* 若使用 autoconf 生成的 config.h，必须先于其它头包含，以便特性宏生效。 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* 系统头：wchar.h 供宽字符输出转换使用。 */
#include <sys/types.h>
#include <string.h>
#include <wchar.h>

/* printfrr.h：FRR printf 对外接口（fbuf、printfrr_ext、各 API 原型）；
 * printflocal.h：内部常量与 union arg 等本地声明。
 */
#include "printfrr.h"
#include "printflocal.h"

/* 【函数】bprintfrr —— 向 fbuf（可为 NULL，即丢弃输出）格式化输出，返回写入或本应
 * 写入的字符数（负数表示出错）。可变参数入口，内部转调 vbprintfrr。
 * 副作用：out 非空时其 pos 前进；out 为 NULL 时仅计算长度。
 */
ssize_t bprintfrr(struct fbuf *out, const char *fmt, ...)
{
	ssize_t ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vbprintfrr(out, fmt, ap);
	va_end(ap);
	return ret;
}

/* 【函数】vsnprintfrr —— snprintf 的 va_list 版：写入定长缓冲 out(outsz) 并保证 NUL
 * 结尾。返回本应写入的长度（可能 >= outsz，表示被截断，遵循 C99 语义）。
 * 注意：out 或 outsz 为 0 时不写，fb 置 NULL 只算长度。
 */
ssize_t vsnprintfrr(char *out, size_t outsz, const char *fmt, va_list ap)
{
	struct fbuf fbb = { .buf = out, .pos = out, .len = outsz - 1, };
	struct fbuf *fb = (out && outsz) ? &fbb : NULL;
	ssize_t ret;

	ret = vbprintfrr(fb, fmt, ap);
	if (fb)
		fb->pos[0] = '\0';
	return ret;
}

/* 【函数】snprintfrr —— snprintf 的 FRR 扩展版（可变参数），委托 vsnprintfrr。
 * 语义同 vsnprintfrr：始终 NUL 结尾，返回本应写入的长度。
 */
ssize_t snprintfrr(char *out, size_t outsz, const char *fmt, ...)
{
	struct fbuf fbb = { .buf = out, .pos = out, .len = outsz - 1, };
	struct fbuf *fb = (out && outsz) ? &fbb : NULL;
	ssize_t ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vbprintfrr(fb, fmt, ap);
	va_end(ap);
	if (fb)
		fb->pos[0] = '\0';
	return ret;
}

/* 【函数】vcsnprintfrr —— “拼接(concatenate)式 snprintfrr”：先用 strnlen 算出 out
 * 中已有字符串长度 pos，从该位置继续格式化追加，最后补 NUL。返回“追加部分”长度
 * （不含已有前缀），出错返回负值；out/outsz 为 0 时等价于只计算追加长度。
 * 用于逐段拼装配置 / 日志文本。
 */
ssize_t vcsnprintfrr(char *out, size_t outsz, const char *fmt, va_list ap)
{
	if (!out || !outsz)
		return vbprintfrr(NULL, fmt, ap);

	struct fbuf fbb = { .buf = out, .pos = out, .len = outsz - 1, };
	ssize_t ret;
	size_t pos;

	pos = strnlen(out, outsz);
	fbb.pos += pos;

	ret = vbprintfrr(&fbb, fmt, ap);
	fbb.pos[0] = '\0';
	return ret >= 0 ? ret + (ssize_t)pos : ret;
}

/* 【函数】csnprintfrr —— vcsnprintfrr 的可变参数包装。 */
ssize_t csnprintfrr(char *out, size_t outsz, const char *fmt, ...)
{
	ssize_t ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vcsnprintfrr(out, outsz, fmt, ap);
	va_end(ap);
	return ret;
}

/* 【函数】vasnprintfrr —— asprintf 风格：先用调用者给的 out(outsz) 试写，若空间不够
 * （len >= outsz-1）则用 qmalloc 按 mt 类型分配 len+1 字节并重写一遍，返回实际缓冲
 * 指针（可能等于 out，也可能是新分配的堆内存，由调用者负责释放）。
 * 关键点：内部 va_copy 后对格式串二次求值——这正是 printfrr_va 扩展需要 va_copy
 * 保护入参 va_list 的原因。格式串非法(len<0)时退化为直接 qstrdup(mt, fmt)。
 */
char *vasnprintfrr(struct memtype *mt, char *out, size_t outsz, const char *fmt,
		   va_list ap)
{
	struct fbuf fb = { .buf = out, .pos = out, .len = outsz - 1, };
	ssize_t len;
	va_list ap2;
	char *ret = out;

	va_copy(ap2, ap);
	len = vbprintfrr(&fb, fmt, ap);
	if (len < 0) {
		va_end(ap2);
		/* error = malformed format string => try something useful */
		return qstrdup(mt, fmt);
	}

	if ((size_t)len >= outsz - 1) {
		ret = qmalloc(mt, len + 1);
		fb.buf = fb.pos = ret;
		fb.len = len;

		vbprintfrr(&fb, fmt, ap2);
	}

	va_end(ap2);
	ret[len] = '\0';
	return ret;
}

/* 【函数】asnprintfrr —— vasnprintfrr 的可变参数包装。 */
char *asnprintfrr(struct memtype *mt, char *out, size_t outsz, const char *fmt,
		  ...)
{
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = vasnprintfrr(mt, out, outsz, fmt, ap);
	va_end(ap);
	return ret;
}

/* 【函数】vasprintfrr —— 纯 asprintf 风格：先用 256 字节栈缓冲，不够时内部的
 * vasnprintfrr 会堆分配；若结果仍在栈缓冲（未分配），再 qstrdup 拷到堆，保证返回值
 * 总是堆内存，便于调用者 qfree。返回堆指针，失败可能为 NULL。
 */
char *vasprintfrr(struct memtype *mt, const char *fmt, va_list ap)
{
	char buf[256];
	char *ret;

	ret = vasnprintfrr(mt, buf, sizeof(buf), fmt, ap);

	if (ret == buf)
		ret = qstrdup(mt, ret);
	return ret;
}

/* 【函数】asprintfrr —— vasprintfrr 的可变参数包装。 */
char *asprintfrr(struct memtype *mt, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = vasnprintfrr(mt, buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (ret == buf)
		ret = qstrdup(mt, ret);
	return ret;
}

/* Q: WTF?
 * A: since printf should be reasonably fast (think debugging logs), the idea
 *    here is to keep things close by each other in a cacheline.  That's why
 *    ext_quick just has the first 2 characters of an extension, and we do a
 *    nice linear continuous sweep.  Only if we find something, we go do more
 *    expensive things.
 *
 * Q: doesn't this need a mutex/lock?
 * A: theoretically, yes, but that's quite expensive and I rather elide that
 *    necessity by putting down some usage rules.  Just call this at startup
 *    while singlethreaded and all is fine.  Ideally, just use constructors
 *    (and make sure dlopen() doesn't mess things up...)
 */
/* 【扩展转换符表】FRR 自定义 %p 扩展机制的静态存储：
 *   MAXEXT           : 扩展项上限。
 *   struct ext_quick : 仅存扩展名前 2 字符，与 ext_offsets 一同驻留相近 cacheline，
 *                      使热路径的线性扫描尽量快。
 *   ext_offsets[26]  : 按首字母 'A'~'Z' 记录每段在 entries / exts 中的起始下标，
 *                      把每次查找范围缩到同首字母的那一段。
 *   entries / exts   : 已注册扩展的键（前 2 字符）与扩展指针，均按首字母有序排列。
 * 设计取舍见下方英文注释：为速度不做加锁，要求注册只在启动单线程阶段完成。
 */
#define MAXEXT 64

struct ext_quick {
	char fmt[2];
};

static uint8_t ext_offsets[26] __attribute__((aligned(32)));
static struct ext_quick entries[MAXEXT] __attribute__((aligned(64)));
static const struct printfrr_ext *exts[MAXEXT] __attribute__((aligned(64)));

/* 【函数】printfrr_ext_reg —— 注册一个 %p 扩展（如 "FB"、"VA"）。按 match 前两字符在
 * 有序表中定位插入点，用 memmove 挪开后续元素腾位后写入 entries 与 exts，并同步
 * 增大 ext_offsets 中该首字母之后各段的下标。首字符非字母则直接忽略。
 * 副作用：就地修改全局表；非线程安全，只应在启动期调用。表满(MAXEXT)时静默丢弃。
 */
void printfrr_ext_reg(const struct printfrr_ext *ext)
{
	uint8_t o;
	ptrdiff_t i;

	if (!printfrr_ext_char(ext->match[0]))
		return;

	o = ext->match[0] - 'A';
	for (i = ext_offsets[o];
			i < MAXEXT && entries[i].fmt[0] &&
			memcmp(entries[i].fmt, ext->match, 2) < 0;
			i++)
		;
	if (i == MAXEXT)
		return;
	for (o++; o <= 'Z' - 'A'; o++)
		ext_offsets[o]++;

	memmove(entries + i + 1, entries + i,
			(MAXEXT - i - 1) * sizeof(entries[0]));
	memmove(exts + i + 1, exts + i,
			(MAXEXT - i - 1) * sizeof(exts[0]));

	memcpy(entries[i].fmt, ext->match, 2);
	exts[i] = ext;
}

/* 【函数】printfrr_extp —— %p 指针型扩展的派发：以 fmt 前两字符在该首字母所在的
 * ext_offsets 段内线性查找，命中则把 ea->fmt 前移 match 长度并调用该扩展的
 * print_ptr 回调。一旦遇到排序位置已越过的项即返回 -1（表示不处理，交回默认逻辑）。
 * 返回扩展写入的字节数，或 -1。
 */
ssize_t printfrr_extp(struct fbuf *buf, struct printfrr_eargs *ea,
		      const void *ptr)
{
	const char *fmt = ea->fmt;
	const struct printfrr_ext *ext;
	size_t i;

	for (i = ext_offsets[fmt[0] - 'A']; i < MAXEXT; i++) {
		if (!entries[i].fmt[0] || entries[i].fmt[0] > fmt[0])
			return -1;
		if (entries[i].fmt[1] && entries[i].fmt[1] != fmt[1])
			continue;
		ext = exts[i];
		if (!ext->print_ptr)
			continue;
		if (strncmp(ext->match, fmt, strlen(ext->match)))
			continue;
		ea->fmt += strlen(ext->match);
		return ext->print_ptr(buf, ea, ptr);
	}
	return -1;
}

/* 【函数】printfrr_exti —— %p 整型扩展的派发：逻辑同 printfrr_extp，但只匹配并调用
 * 扩展的 print_int 回调（用于 %d / %u 类整型参数的自定义处理）。返回写入字节数或 -1。
 */
ssize_t printfrr_exti(struct fbuf *buf, struct printfrr_eargs *ea,
		      uintmax_t num)
{
	const char *fmt = ea->fmt;
	const struct printfrr_ext *ext;
	size_t i;

	for (i = ext_offsets[fmt[0] - 'A']; i < MAXEXT; i++) {
		if (!entries[i].fmt[0] || entries[i].fmt[0] > fmt[0])
			return -1;
		if (entries[i].fmt[1] && entries[i].fmt[1] != fmt[1])
			continue;
		ext = exts[i];
		if (!ext->print_int)
			continue;
		if (strncmp(ext->match, fmt, strlen(ext->match)))
			continue;
		ea->fmt += strlen(ext->match);
		return ext->print_int(buf, ea, num);
	}
	return -1;
}

/* 【扩展】%pFB —— 输出一个 struct fbuf 的内容（把其中已格式化的缓冲区原样拷入当前
 * 输出）。下面的宏在启动时自动注册该扩展；printfrr_fb 是其实现：ptr 为 NULL 时输出
 * "NULL"，否则按 out 剩余空间取较小值拷贝，返回值始终是源缓冲长度（即使被截断）。
 */
printfrr_ext_autoreg_p("FB", printfrr_fb);
static ssize_t printfrr_fb(struct fbuf *out, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct fbuf *in = ptr;
	ptrdiff_t copy_len;

	if (!in)
		return bputs(out, "NULL");

	if (out) {
		copy_len = MIN(in->pos - in->buf,
			       out->buf + out->len - out->pos);
		if (copy_len > 0) {
			memcpy(out->pos, in->buf, copy_len);
			out->pos += copy_len;
		}
	}

	return in->pos - in->buf;
}

/* 【扩展】%pVA —— 输出一个 struct va_format（内含格式串及其 va_list），用于把“格式串
 * + 参数”作为数据传递后再展开。注册宏在启动时自动登记；实现前先 va_copy 出 ap，避免
 * 消耗入参的 va_list（因为 asnprintfrr 可能对同一格式串二次求值）。ptr 或内部
 * fmt / va 为空时输出 "NULL"。
 */
printfrr_ext_autoreg_p("VA", printfrr_va);
static ssize_t printfrr_va(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct va_format *vaf = ptr;
	va_list ap;
	ssize_t s;

	if (!vaf || !vaf->fmt || !vaf->va)
		return bputs(buf, "NULL");

	/* make sure we don't alter the data passed in - especially since
	 * bprintfrr (and thus this) might be called on the same format twice,
	 * when allocating a larger buffer in asnprintfrr()
	 */
	va_copy(ap, *vaf->va);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	/* can't format check this */
	s = vbprintfrr(buf, vaf->fmt, ap);
#pragma GCC diagnostic pop
	va_end(ap);

	return s;
}
