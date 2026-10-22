// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2019  David Lamparter, for NetDEF, Inc.
 */

/*
 * ============================================================================
 * 【文件导读】lib/printfrr.h —— FRR 扩展 printf 的格式符与声明
 * ============================================================================
 * 职责：
 *   声明 FRR 自研的 "printfrr" 格式化 API。它在标准 printf 之上扩展了
 *   以大写字母结尾的格式符（如 %pFB、%pVA、%pHX ...），用于打印 fbuf、
 *   va_list、二进制、时间等标准 printf 无法直接处理的类型，同时保留
 *   标准格式串的宽度、精度、对齐等能力。
 *
 * 核心数据结构：
 *   struct fbuf         —— 输出缓冲区游标：记录缓冲起止、当前写位置，
 *                         以及可选的"输出位置记录"数组。
 *   struct fmt_outpos   —— 记录某段输出在缓冲中的起止偏移。
 *   struct printfrr_ext —— 一个扩展格式化器的描述（匹配串 + 回调）。
 *   struct printfrr_eargs      —— 传给扩展回调的上下文（fmt 位置、精度、宽度、标志）。
 *   struct va_format           —— 封装 fmt 与 va_list 指针，配合 %pVA 使用。
 *   本文件不含全局变量。
 *
 * 与其它模块的关系：
 *   - 依赖 compiler.h 的 PRINTFRR 属性做格式串静态检查。
 *   - 依赖 memory.h 的 memtype 与 MTYPE_TMP 做内存分配（asprintfrr 系列）。
 *   - lib/printfrr.c 提供函数实现；各扩展格式化器分散在 lib/ 其它文件里
 *     （strformat.c、printf/glue.c、prefix.c、sockunion.c 等），
 *     用 printfrr_ext_autoreg_p / _i 在启动时自动注册。
 *
 * 阅读顺序：
 *   1) fbuf / fmt_outpos / printfrr_eargs / printfrr_ext 结构体；
 *   2) 各格式化函数声明（bprintfrr / snprintfrr / asprintfrr ...）；
 *   3) printfrr 便捷宏；
 *   4) printfrr_ext_char / printfrr_ext_len 与 printfrr_ext_reg 及自动注册宏；
 *   5) fbuf 辅助函数 bputs / bputch / bputhex；
 *   6) va_format 与末尾的 #pragma FRR 格式符登记表，再是 FMT_NSTD 系列宏。
 *
 * 易踩的坑：
 *   - bprintfrr 不写结尾 NUL；snprintfrr 系列才补 NUL。
 *   - 返回值一律是"假如缓冲够大本应写入的长度"，与缓冲大小无关；
 *     扩展回调若少报长度会导致 asnprintf 系列出错。
 *   - 扩展格式符必须以大写字母结尾；这是性能与可读性的折中约定。
 *   - printfrr_ext_reg 不做加锁，只应在单线程阶段（如启动期）调用。
 *   - 文件内临时宏 at / atn / atm 使用后立即 #undef，避免污染包含者。
 * ============================================================================
 */

#ifndef _FRR_PRINTFRR_H
#define _FRR_PRINTFRR_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#include "compiler.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// struct fmt_outpos：记录一段输出在结果缓冲中的起始/结束偏移（字节）。
// 用于 fbuf 的 outpos 数组，便于扩展定位自己写出的片段。
struct fmt_outpos {
	unsigned int off_start, off_end;
};

// struct fbuf：printfrr 的输出缓冲游标。
//   buf / pos / len —— 缓冲起点、当前写位置、缓冲容量（字节）；
//   outpos / outpos_n / outpos_i —— 可选的"输出位置记录"数组及其容量与游标，
//   用于记录某些片段被写到缓冲的哪个区间。
struct fbuf {
	char *buf;
	char *pos;
	size_t len;

	struct fmt_outpos *outpos;
	size_t outpos_n, outpos_i;
};

// 宏 at：把 printfrr 自有属性映射为标准 PRINTFRR 编译期格式检查属性。
// 这里临时起个短名 at，用完在文件下方 #undef 掉，避免污染包含者。
#define at(a, b) PRINTFRR(a, b)
// 宏 atn：在 at 基础上追加 nonnull(1)（第 1 个参数不可为 NULL）
// 与 _RET_NONNULL（返回值不可为 NULL）属性。
#define atn(a, b) \
	at(a, b) __attribute__((nonnull(1) _RET_NONNULL))
// 宏 atm：在 atn 基础上再追加 malloc 属性，声明返回值是"新鲜"分配的内存，
// 便于编译器做别名分析（提示该指针不与其它指针别名）。
#define atm(a, b) \
	atn(a, b) __attribute__((malloc))

/* return value is length needed for the full string (excluding \0) in all
 * cases.  The functions write as much as they can, but continue regardless,
 * so the return value is independent of buffer length.  Both bprintfrr and
 * snprintf also accept NULL as output buffer.
 */

/* bprintfrr does NOT null terminate! use sparingly (only provided since it's
 * the most direct interface) - useful for incrementally building long text
 * (call bprintfrr repeatedly with the same buffer)
 */
// vbprintfrr / bprintfrr：把格式化文本写入 fbuf，不写结尾 NUL。
// 返回"若缓冲足够大本应写入的总长度"，与缓冲大小无关，便于先探长度再分配。
// at(2, 0) / at(2, 3) 指明格式串在第 2 个参数，va_list 或可变参数从第 3 起。
ssize_t vbprintfrr(struct fbuf *out, const char *fmt, va_list) at(2, 0);
ssize_t  bprintfrr(struct fbuf *out, const char *fmt, ...)     at(2, 3);

/* these do null terminate like their snprintf cousins */
// vsnprintfrr / snprintfrr：与 snprintf 类似，会补结尾 NUL，但额外支持
// FRR 扩展格式符。返回值同样与缓冲大小无关（本应写入的长度）。
ssize_t vsnprintfrr(char *out, size_t sz, const char *fmt, va_list) at(3, 0);
ssize_t  snprintfrr(char *out, size_t sz, const char *fmt, ...)     at(3, 4);

/* c = continue / concatenate (append at the end of the string)
 * return value is would-be string length (regardless of buffer length),
 * i.e. includes already written chars */
// vcsnprintfrr / csnprintfrr：c = continue，在已有字符串末尾继续追加。
// 返回的是"假如够大则整个字符串应有多长"，即已计入此前已写入的字符数。
ssize_t vcsnprintfrr(char *out, size_t sz, const char *fmt, va_list) at(3, 0);
ssize_t  csnprintfrr(char *out, size_t sz, const char *fmt, ...)     at(3, 4);

/* memory allocations don't fail in FRR, so you always get something here.
 * (in case of error, returns a strdup of the format string) */
// vasprintfrr / asprintfrr：分配一块内存返回格式化结果（FRR 里分配不失败）。
// 出错时退化为对格式串做 strdup。mt 指定内存类型，带 atm 属性（返回值是新分配内存）。
char *vasprintfrr(struct memtype *mt, const char *fmt, va_list) atm(2, 0);
char  *asprintfrr(struct memtype *mt, const char *fmt, ...)     atm(2, 3);

/* try to use provided buffer (presumably from stack), allocate if it's too
 * short.  Must call XFREE(mt, return value) if return value != out.
 */
// vasnprintfrr / asnprintfrr：优先使用调用方提供的缓冲（通常是栈缓冲）；
// 若太短则另行分配。当返回值 != out 时，必须用 XFREE(mt, 返回值) 释放。带 atn 属性。
char *vasnprintfrr(struct memtype *mt, char *out, size_t sz,
		   const char *fmt, va_list) atn(4, 0);
char  *asnprintfrr(struct memtype *mt, char *out, size_t sz,
		   const char *fmt, ...)     atn(4, 5);

// 宏 printfrr：便捷地把格式化结果打印到 stdout。
// 内部用 256 字节栈缓冲调用 asnprintfrr；若结果发生了堆分配则用 XFREE 释放。
// 注意它只输出到 stdout，通用日志应改用 zlog_* 系列。
#define printfrr(fmt, ...)                                                     \
	do {                                                                   \
		char buf[256], *out;                                           \
		out = asnprintfrr(MTYPE_TMP, buf, sizeof(buf), fmt,            \
				  ##__VA_ARGS__);                              \
		fputs(out, stdout);                                            \
		if (out != buf)                                                \
			XFREE(MTYPE_TMP, out);                                 \
	} while (0)

// 用完即弃：取消文件内临时定义的短名属性宏 at / atn / atm，
// 避免影响后续包含本头文件的其它代码。
#undef at
#undef atm
#undef atn

/* extension specs must start with a capital letter (this is a restriction
 * for both performance's and human understanding's sake.)
 *
 * Note that the entire thing mostly works because a letter directly following
 * a %p print specifier is extremely unlikely to occur (why would you want to
 * print "0x12345678HELLO"?)  Normally, you'd expect spacing or punctuation
 * after a placeholder.  That also means that neither of those works well for
 * extension purposes, e.g. "%p{foo}" is reasonable to see actually used.
 *
 * TODO: would be nice to support a "%pF%dF" specifier that consumes 2
 * arguments, e.g. to pass an integer + a list of known values...  can be
 * done, but a bit tricky.
 */
// 宏 printfrr_ext_char：判断字符 ch 是否为大写字母 A..Z。
// 约定 printfrr 扩展格式符必须以大写字母结尾，本宏用于识别之。
#define printfrr_ext_char(ch) ((ch) >= 'A' && (ch) <= 'Z')

struct printfrr_eargs;

// struct printfrr_ext：描述一个扩展格式化器。
//   match[8]  —— 紧致的匹配串（如 "%pFB"），内嵌以避免缓存行抖动；
//   print_ptr —— 处理指针类参数（对应 %p...）的回调，可为空；
//   print_int —— 处理整数类参数（对应 %d...）的回调，可为空；
// 两个回调都可给出；某一项匹配不上时引擎会继续搜索其它条目
// （例如 %pX 与 %dX 可以分属两条不同的注册项）。
// 回调返回"假如缓冲够大本应写入的字节数"；返回 -1 表示出错、回退默认 %p / %d 处理。
// 回调可通过 *info->fmt 读取并消费 %pXY 之后的额外标志字符（惯用小写字母或数字）。
struct printfrr_ext {
	/* embedded string to minimize cache line pollution */
	char match[8];

	/* both can be given, if not the code continues searching
	 * (you can do %pX and %dX in 2 different entries)
	 *
	 * return value: number of bytes that would be printed if the buffer
	 * was large enough.  be careful about not under-reporting this;
	 * otherwise asnprintf() & co. will get broken.  Returning -1 means
	 * something went wrong & default %p/%d handling should be executed.
	 *
	 * to consume extra input flags after %pXY, increment *fmt.  It points
	 * at the first character after %pXY at entry.  Convention is to make
	 * those flags lowercase letters or numbers.
	 */
	ssize_t (*print_ptr)(struct fbuf *buf, struct printfrr_eargs *info,
			     const void *);
	ssize_t (*print_int)(struct fbuf *buf, struct printfrr_eargs *info,
			     uintmax_t);
};

/* additional information passed to extended formatters */

// struct printfrr_eargs：传给扩展回调的上下文信息。
//   fmt       —— 当前在格式串中的位置（指向扩展格式符之后），消费额外标志时前移；
//   precision —— %.1234x / %.*x 的精度，未指定为 -1
//                （与 %p 同用时标准不兼容，会引发 GCC / clang 告警）；
//   width     —— %1234x / %*x 的宽度，未指定为 0；被扩展消费时应置 0 以抑制标准填充；
//   alt_repr  —— %#x "替代表示"标志；
//   leftadj   —— %-x 左对齐标志，仅当扩展把 width 置 0 时才需自己处理。
struct printfrr_eargs {
	/* position in the format string.  Points to directly after the
	 * extension specifier.  Increment when consuming extra "flag
	 * characters".
	 */
	const char *fmt;

	/* %.1234x / %.*x
	 * not POSIX compatible when used with %p, will cause warnings from
	 * GCC & clang.  Usable with %d.  Not used by the printfrr() itself
	 * for extension specifiers, so essentially available as a "free"
	 * parameter.  -1 if not specified.  Value in the format string
	 * cannot be negative, but negative values can be passed with %.*x
	 */
	int precision;

	/* %1234x / %*x
	 * regular width specification.  Internally handled by printfrr(), set
	 * to 0 if consumed by the extension in order to suppress standard
	 * width/padding behavior.  0 if not specified.
	 *
	 * NB: always positive, even if a negative value is passed in with
	 * %*x.  (The sign is used for the - flag.)
	 */
	int width;

	/* %#x
	 * "alternate representation" flag, not POSIX compatible when used
	 * with %p or %d, will cause warnings from GCC & clang.  Not used by
	 * printfrr() itself for extension specifiers.
	 */
	bool alt_repr;

	/* %-x
	 * left-pad flag.  Internally handled by printfrr() if width is
	 * nonzero.  Only use if the extension sets width to 0.
	 */
	bool leftadj;
};

/* for any extension that needs a buffer length */

// printfrr_ext_len（static inline）：给"需要缓冲长度"的扩展算出一个长度：
//   有精度则用精度；否则若有宽度则用宽度，并顺手把 width 置 -1（表示已被消费）；
//   都没有则返回 -1。返回值语义与其它 printfrr 函数一致。
static inline ssize_t printfrr_ext_len(struct printfrr_eargs *ea)
{
	ssize_t rv;

	if (ea->precision >= 0)
		rv = ea->precision;
	else if (ea->width >= 0) {
		rv = ea->width;
		ea->width = -1;
	} else
		rv = -1;

	return rv;
}

/* no locking - must be called when single threaded (e.g. at startup.)
 * this restriction hopefully won't be a huge bother considering normal usage
 * scenarios...
 */
// printfrr_ext_reg：注册一个扩展格式化器（把 printfrr_ext 登记进引擎的查找表）。
// 无锁，故只应在单线程阶段（如启动期）调用。
void printfrr_ext_reg(const struct printfrr_ext *ext);

// 宏 printfrr_ext_autoreg_p：声明并自动注册一个"指针类"扩展（对应 %p...）。
//   先后置声明 print_fn；再构造 static 的 printfrr_ext（填 match 与 print_ptr）；
//   最后定义一个带 constructor 属性的函数，在程序装载时自动调用 printfrr_ext_reg。
//   末尾 MACRO_REQUIRE_SEMICOLON() 保证调用处书写分号。
#define printfrr_ext_autoreg_p(matchs, print_fn)                               \
	static ssize_t print_fn(struct fbuf *, struct printfrr_eargs *,        \
				const void *);                                 \
	static const struct printfrr_ext _printext_##print_fn = {              \
		.match = matchs,                                               \
		.print_ptr = print_fn,                                         \
	};                                                                     \
	static void _printreg_##print_fn(void) __attribute__((constructor));   \
	static void _printreg_##print_fn(void)                                 \
	{                                                                      \
		printfrr_ext_reg(&_printext_##print_fn);                       \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON()

// 宏 printfrr_ext_autoreg_i：同上，但注册的是"整数类"扩展（填 print_int，对应 %d...）。
#define printfrr_ext_autoreg_i(matchs, print_fn)                               \
	static ssize_t print_fn(struct fbuf *, struct printfrr_eargs *,        \
				uintmax_t);                                    \
	static const struct printfrr_ext _printext_##print_fn = {              \
		.match = matchs,                                               \
		.print_int = print_fn,                                         \
	};                                                                     \
	static void _printreg_##print_fn(void) __attribute__((constructor));   \
	static void _printreg_##print_fn(void)                                 \
	{                                                                      \
		printfrr_ext_reg(&_printext_##print_fn);                       \
	}                                                                      \
	MACRO_REQUIRE_SEMICOLON()

/* fbuf helper functions - note all 3 of these return the length that would
 * be written regardless of how much space was available in the buffer, as
 * needed for implementing printfrr extensions.  (They also accept NULL buf
 * for that.)
 */

// bputs（static inline）：把以 NUL 结尾的字符串写入 fbuf，最多写到缓冲末尾。
// 返回本应写入的长度 strlen(str)，与缓冲大小无关；buf 可为 NULL（此时仅返回长度）。
static inline ssize_t bputs(struct fbuf *buf, const char *str)
{
	size_t len = strlen(str);
	size_t ncopy;

	if (!buf)
		return len;

	ncopy = MIN(len, (size_t)(buf->buf + buf->len - buf->pos));
	memcpy(buf->pos, str, ncopy);
	buf->pos += ncopy;

	return len;
}

// bputch（static inline）：写入单个字符；空间不足则丢弃，但仍返回 1（本应写入 1 字节）。
static inline ssize_t bputch(struct fbuf *buf, char ch)
{
	if (buf && buf->pos < buf->buf + buf->len)
		*buf->pos++ = ch;
	return 1;
}

// bputhex（static inline）：以两位小写十六进制写入一个字节，始终返回 2。
static inline ssize_t bputhex(struct fbuf *buf, uint8_t val)
{
	static const char hexch[] = "0123456789abcdef";

	if (buf && buf->pos < buf->buf + buf->len)
		*buf->pos++ = hexch[(val >> 4) & 0xf];
	if (buf && buf->pos < buf->buf + buf->len)
		*buf->pos++ = hexch[val & 0xf];
	return 2;
}

/* %pVA extension, equivalent to Linux kernel %pV */

// struct va_format：配合 %pVA 扩展使用，把格式串 fmt 与 va_list 指针打包传递，
// 使得"格式串 + 参数"可作为数据先携带、稍后再展开。相当于 Linux 内核的 %pV。
struct va_format {
	const char *fmt;
	va_list *va;
};

// 下面这组 #pragma FRR printfrr_ext 是给编译器/静态检查用的"格式符登记表"：
// 告诉 FRR 的格式检查器"某个扩展格式符对应哪种参数类型"，从而在编译期校验调用。
// 本文件只登记最核心的几个（其余扩展在各自治文件里另行登记）。逐个说明：
//   %pFB  对应 struct fbuf *        —— 输出另一个 fbuf 已格式化的内容；
//   %pVA  对应 struct va_format *   —— 展开 va_format 里的 fmt / va（等价内核 %pV）；
//   %pHX  对应 signed / unsigned char * 或 void * —— 按十六进制打印内存字节串；
//   %pHS  对应 signed / unsigned char * 或 void * —— hexdump 的文本列：可打印字节原样输出、其余替换为点号；
//   %pSE  对应 char *               —— 打印字符串并按需要转义（escape）不可见字符；
//   %pSQ  对应 char *               —— 打印字符串并用引号包裹（quote）；
//   %pSA  对应 const char **        —— 打印以 NULL 结尾的字符串数组；
//   %pTS  对应 struct timespec *    —— 按 timespec 格式化时间；
//   %pTV  对应 struct timeval *     —— 按 timeval 格式化时间；
//   %pTT  对应 time_t *             —— 按 time_t 格式化时间。
#ifdef _FRR_ATTRIBUTE_PRINTFRR
// clang-format off
#pragma FRR printfrr_ext "%pFB" (struct fbuf *)
#pragma FRR printfrr_ext "%pVA" (struct va_format *)

#pragma FRR printfrr_ext "%pHX" (signed char *)
#pragma FRR printfrr_ext "%pHX" (unsigned char *)
#pragma FRR printfrr_ext "%pHX" (void *)
#pragma FRR printfrr_ext "%pHS" (signed char *)
#pragma FRR printfrr_ext "%pHS" (unsigned char *)
#pragma FRR printfrr_ext "%pHS" (void *)

#pragma FRR printfrr_ext "%pSE" (char *)
#pragma FRR printfrr_ext "%pSQ" (char *)
#pragma FRR printfrr_ext "%pSA" (const char **)

#pragma FRR printfrr_ext "%pTS" (struct timespec *)
#pragma FRR printfrr_ext "%pTV" (struct timeval *)
#pragma FRR printfrr_ext "%pTT" (time_t *)
// clang-format on
#endif

/* when using non-ISO-C compatible extension specifiers... */

// 宏 FMT_NSTD_BEGIN：成对使用，开始一段"关闭格式告警"的区域。
// 宏 FMT_NSTD_END  ：结束上述区域。
// 当编译器支持 _FRR_ATTRIBUTE_PRINTFRR（FRR 自带的格式检查）时，这两个宏是空操作，
// 因为 FRR 自己的检查器认识扩展格式符；否则用 GCC 的 diagnostic push / pop 临时压掉
// -Wformat，避免标准检查器对 %pXY 等非标准格式符报警。
#ifdef _FRR_ATTRIBUTE_PRINTFRR
#define FMT_NSTD_BEGIN
#define FMT_NSTD_END
#else /* !_FRR_ATTRIBUTE_PRINTFRR */
#define FMT_NSTD_BEGIN \
	_Pragma("GCC diagnostic push")                                         \
	_Pragma("GCC diagnostic ignored \"-Wformat\"")                         \
	/* end */
#define FMT_NSTD_END \
	_Pragma("GCC diagnostic pop")                                          \
	/* end */
#endif

// 宏 FMT_NSTD：把表达式 expr 包进一个语句表达式，在其求值前后套上
// FMT_NSTD_BEGIN / FMT_NSTD_END，从而只对该表达式抑制格式告警，类型不变。
// 用于书写含非标准扩展格式符的 printfrr 调用而不触发 -Wformat。
#define FMT_NSTD(expr)                                                         \
	({                                                                     \
		FMT_NSTD_BEGIN                                                 \
		typeof(expr) _v;                                               \
		_v = expr;                                                     \
		FMT_NSTD_END                                                   \
		_v;                                                            \
	})                                                                     \
	/* end */

#ifdef __cplusplus
}
#endif

#endif
