// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2019  David Lamparter, for NetDEF, Inc.
 */

/*
 * ============================================================
 * 【文件导读】strformat.c —— FRR 自研格式化(printfrr)扩展实现
 * ============================================================
 *
 * 一、职责
 *   实现并注册一批 printfrr 格式化扩展(%pXXX)，让 FRR 的日志 /
 *   show 输出能直接打印十六进制转储、需转义的字符串、字符串数组
 *   以及各种时间格式，而不必到处手写临时代码。
 *
 * 二、核心概念
 *   - printfrr_ext_autoreg_p：注册“指针型”扩展的宏，第二个参数是
 *     格式化函数，首个参数 match 决定 %p 后匹配的串，如 "HX"。
 *   - struct fbuf 与 bputch / bputs / bputhex：输出缓冲与写原语。
 *   - struct printfrr_eargs(ea)：本次格式化的精度 / 宽度 / 当前
 *     fmt 指针；扩展可继续消费 ea->fmt 中的子说明符。
 *   - printfrr_ext_len(ea)：把精度或宽度解释为“输入字节长度”。
 *
 * 三、本文件注册的扩展
 *   - HX 十六进制转储(c 用冒号分隔 / n 无分隔)；
 *   - HS 十六进制转储的 ASCII 旁列；
 *   - SE 转义字符串(ESC_ALL)；SQ 引号化 / 转义(n / q / s)；
 *   - SA 字符串数组(d 表示动态数组 darr)；
 *   - TS / TV / TT 分别格式化 timespec / timeval / time_t。
 *
 * 四、阅读顺序
 *   先读 HX / HS(最简逐字节示例)，再读 escape_flags 与 bquote、
 *   SE、SQ，然后 SA，最后是 printfrr_time 及其派生函数。
 *
 * 五、易踩的坑
 *   - 所有回调返回“本应写入的长度”，与 buf 是否够长无关。
 *   - 扩展通过 ea->fmt++ 消费子说明符，读取顺序敏感。
 *   - 时间分绝对(日历)与相对(时长)两类，由 TIMEFMT_ABSOLUTE 与
 *     TIMEFMT_ANCHORS 的异或决定走 abstime 还是 reltime。
 *   - 负数相对时间要拆成 tv_sec 与 tv_nsec 规范化，易算错。
 * ============================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "compiler.h"

#include <string.h>
#include <ctype.h>
#include <time.h>

/* darr.h 动态数组；printfrr.h 扩展框架；monotime.h 时间与 TIMEFMT_* 标志 */
#include "darr.h"
#include "printfrr.h"
#include "monotime.h"

/* 注册指针型扩展 "HX"：用户写 %p HX 时调用 printfrr_hexdump */
printfrr_ext_autoreg_p("HX", printfrr_hexdump);
/* 十六进制转储实现：把 ptr 指向的 input_len 字节逐字节以两位十六进
 * 制输出；默认空格分隔，fmt 为 'c' 用冒号、为 'n' 不用分隔符 */
static ssize_t printfrr_hexdump(struct fbuf *buf, struct printfrr_eargs *ea,
				const void *ptr)
{
	ssize_t ret = 0;
	ssize_t input_len = printfrr_ext_len(ea);
	char sep = ' ';
	const uint8_t *pos, *end;

	if (ea->fmt[0] == 'c') {
		ea->fmt++;
		sep = ':';
	} else if (ea->fmt[0] == 'n') {
		ea->fmt++;
		sep = '\0';
	}

	if (input_len < 0)
		return 0;

	for (pos = ptr, end = pos + input_len; pos < end; pos++) {
		if (sep && pos != ptr)
			ret += bputch(buf, sep);
		ret += bputhex(buf, *pos);
	}

	return ret;
}

/* string analog for hexdumps / the "this." in ("74 68 69 73 0a  |this.|") */

/* 注册指针型扩展 "HS"：hexdump 的字符串旁列 */
printfrr_ext_autoreg_p("HS", printfrr_hexdstr);
/* 字符串旁列实现：可打印 ASCII(0x20~0x7e)原样输出，其余以 '.' 代替 */
static ssize_t printfrr_hexdstr(struct fbuf *buf, struct printfrr_eargs *ea,
				const void *ptr)
{
	ssize_t ret = 0;
	ssize_t input_len = printfrr_ext_len(ea);
	const uint8_t *pos, *end;

	if (input_len < 0)
		return 0;

	for (pos = ptr, end = pos + input_len; pos < end; pos++) {
		if (*pos >= 0x20 && *pos < 0x7f)
			ret += bputch(buf, *pos);
		else
			ret += bputch(buf, '.');
	}

	return ret;
}

/* 转义标志位：控制 bquote 对哪类字符加反斜杠或改用转义序列 */
enum escape_flags {
	ESC_N_R_T	= (1 << 0),	/* use \n \r \t instead of \x0a ...*/
	ESC_SPACE	= (1 << 1),	/* \  */
	ESC_BACKSLASH	= (1 << 2),	/* \\ */
	ESC_DBLQUOTE	= (1 << 3),	/* \" */
	ESC_SGLQUOTE	= (1 << 4),	/* \' */
	ESC_BACKTICK	= (1 << 5),	/* \` */
	ESC_DOLLAR	= (1 << 6),	/* \$ */
	ESC_CLBRACKET	= (1 << 7),	/* \] for RFC5424 syslog */
	ESC_OTHER	= (1 << 8),	/* remaining non-alpha */

	ESC_ALL = ESC_N_R_T | ESC_SPACE | ESC_BACKSLASH | ESC_DBLQUOTE
		| ESC_SGLQUOTE | ESC_DOLLAR | ESC_OTHER,
	ESC_QUOTSTRING = ESC_N_R_T | ESC_BACKSLASH | ESC_DBLQUOTE,
	/* if needed: ESC_SHELL = ... */
};

/*
 * 转义核心：按 flags 决定对哪类字符加反斜杠；字母数字与一批安全
 * 符号直接输出，\r\n\t 可转成 \r\n\t 序列，其余不可打印字节输出
 * 为 \xHH 形式。返回应写入长度。
 */
static ssize_t bquote(struct fbuf *buf, const uint8_t *pos, size_t len,
		      unsigned int flags)
{
	ssize_t ret = 0;
	const uint8_t *end = pos + len;

	for (; pos < end; pos++) {
		/* here's to hoping this might be a bit faster... */
		if (__builtin_expect(!!isalnum(*pos), 1)) {
			ret += bputch(buf, *pos);
			continue;
		}

		switch (*pos) {
		case '%':
		case '+':
		case ',':
		case '-':
		case '.':
		case '/':
		case ':':
		case '@':
		case '_':
			ret += bputch(buf, *pos);
			continue;

		case '\r':
			if (!(flags & ESC_N_R_T))
				break;
			ret += bputch(buf, '\\');
			ret += bputch(buf, 'r');
			continue;
		case '\n':
			if (!(flags & ESC_N_R_T))
				break;
			ret += bputch(buf, '\\');
			ret += bputch(buf, 'n');
			continue;
		case '\t':
			if (!(flags & ESC_N_R_T))
				break;
			ret += bputch(buf, '\\');
			ret += bputch(buf, 't');
			continue;

		case ' ':
			if (flags & ESC_SPACE)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		case '\\':
			if (flags & ESC_BACKSLASH)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		case '"':
			if (flags & ESC_DBLQUOTE)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		case '\'':
			if (flags & ESC_SGLQUOTE)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		case '`':
			if (flags & ESC_BACKTICK)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		case '$':
			if (flags & ESC_DOLLAR)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		case ']':
			if (flags & ESC_CLBRACKET)
				ret += bputch(buf, '\\');
			ret += bputch(buf, *pos);
			continue;

		/* remaining: !#&'()*;<=>?[^{|}~ */

		default:
			if (*pos >= 0x20 && *pos < 0x7f) {
				if (flags & ESC_OTHER)
					ret += bputch(buf, '\\');
				ret += bputch(buf, *pos);
				continue;
			}
		}
		ret += bputch(buf, '\\');
		ret += bputch(buf, 'x');
		ret += bputhex(buf, *pos);
	}

	return ret;
}

/* 注册指针型扩展 "SE"(escape)：以 ESC_ALL 规则转义字符串 */
printfrr_ext_autoreg_p("SE", printfrr_escape);
/* 转义实现：fmt 为 'n' 时空指针输出空串，否则 NULL 输出 "(null)"；
 * len<0 时用 strlen 求长，最终交给 bquote */
static ssize_t printfrr_escape(struct fbuf *buf, struct printfrr_eargs *ea,
			       const void *vptr)
{
	ssize_t len = printfrr_ext_len(ea);
	const uint8_t *ptr = vptr;
	bool null_is_empty = false;

	if (ea->fmt[0] == 'n') {
		null_is_empty = true;
		ea->fmt++;
	}

	if (!ptr) {
		if (null_is_empty)
			return 0;
		return bputs(buf, "(null)");
	}

	if (len < 0)
		len = strlen((const char *)ptr);

	return bquote(buf, ptr, len, ESC_ALL);
}

/* 注册指针型扩展 "SQ"(quote)：引号化 / 转义字符串 */
printfrr_ext_autoreg_p("SQ", printfrr_quote);
/* 引号化实现：先解析子修饰 n(空指针视为空串)、q(两侧加双引号)、
 * s(采用 syslog RFC5424 风格转义)，再按选定标志调用 bquote */
static ssize_t printfrr_quote(struct fbuf *buf, struct printfrr_eargs *ea,
			      const void *vptr)
{
	ssize_t len = printfrr_ext_len(ea);
	const uint8_t *ptr = vptr;
	ssize_t ret = 0;
	bool null_is_empty = false;
	bool do_quotes = false;
	unsigned int flags = ESC_QUOTSTRING;

	while (ea->fmt[0]) {
		switch (ea->fmt[0]) {
		case 'n':
			null_is_empty = true;
			ea->fmt++;
			continue;
		case 'q':
			do_quotes = true;
			ea->fmt++;
			continue;
		case 's':
			flags |= ESC_CLBRACKET;
			flags &= ~ESC_N_R_T;
			ea->fmt++;
			continue;
		}
		break;
	}

	if (!ptr) {
		if (null_is_empty)
			return bputs(buf, do_quotes ? "\"\"" : "");
		return bputs(buf, "(null)");
	}

	if (len < 0)
		len = strlen((const char *)ptr);

	if (do_quotes)
		ret += bputch(buf, '"');
	ret += bquote(buf, ptr, len, flags);
	if (do_quotes)
		ret += bputch(buf, '"');
	return ret;
}

/* 注册指针型扩展 "SA"(string array)：把字符串数组用逗号逐个连接 */
printfrr_ext_autoreg_p("SA", printfrr_str_array);
/* 数组实现：fmt 为 'd' 表示动态数组(darr)，长度由 darr_len 求得；
 * 固定数组在首个 NULL 处停止，darr 里的 NULL 则输出 "(null)" */
static ssize_t printfrr_str_array(struct fbuf *buf, struct printfrr_eargs *ea, const void *vptr)
{
	ssize_t len = printfrr_ext_len(ea);
	char const *const *sarr = vptr;
	bool is_darr = false;
	ssize_t olen = 0;
	const char *s;
	int i;

	if (!sarr)
		return bputs(buf, "");

	if (ea->fmt[0] == 'd') {
		ea->fmt++;
		is_darr = true;
		if (len < 0)
			len = darr_len((char **)sarr);
	}

	for (i = 0; len < 0 || i < len; i++) {
		s = sarr[i];
		if (!s) {
			if (is_darr)
				s = "(null)";
			else
				break;
		}
		if (i > 0)
			olen += bputch(buf, ',');
		olen += bputs(buf, s);
	}
	return olen;
}

/* 时间格式化的两条实现路径：绝对(日历)与相对(时长)，见下方定义 */
static ssize_t printfrr_abstime(struct fbuf *buf, struct printfrr_eargs *ea,
				const struct timespec *ts, unsigned int flags);
static ssize_t printfrr_reltime(struct fbuf *buf, struct printfrr_eargs *ea,
				const struct timespec *ts, unsigned int flags);

/*
 * 时间格式化入口：先(若非 PRESELECT)解析输入类型 I / M / R 与锚点
 * 方向 s / u，再按“绝对 XOR 锚点”选择走 abstime 还是 reltime 路径，
 * 使结果语义一致。返回应写入长度。
 */
ssize_t printfrr_time(struct fbuf *buf, struct printfrr_eargs *ea,
		      const struct timespec *ts, unsigned int flags)
{
	bool have_abs, have_anchor;

	if (!(flags & TIMEFMT_PRESELECT)) {
		switch (ea->fmt[0]) {
		case 'I':
			/* no bit set */
			break;
		case 'M':
			flags |= TIMEFMT_MONOTONIC;
			break;
		case 'R':
			flags |= TIMEFMT_REALTIME;
			break;
		default:
			return bputs(buf,
				     "{invalid time format input specifier}");
		}
		ea->fmt++;

		if (ea->fmt[0] == 's') {
			flags |= TIMEFMT_SINCE;
			ea->fmt++;
		} else if (ea->fmt[0] == 'u') {
			flags |= TIMEFMT_UNTIL;
			ea->fmt++;
		}
	}

	have_abs = !!(flags & TIMEFMT_ABSOLUTE);
	have_anchor = !!(flags & TIMEFMT_ANCHORS);

	if (have_abs ^ have_anchor)
		return printfrr_abstime(buf, ea, ts, flags);
	else
		return printfrr_reltime(buf, ea, ts, flags);
}

/*
 * 输出小数秒：按 precision 取 tv_nsec 的高位数字(如 .123)；精度
 * <=0 或要求整秒(TIMEFMT_SECONDS)时不输出；精度上限截到 9 位。
 */
static ssize_t do_subsec(struct fbuf *buf, const struct timespec *ts,
			 int precision, unsigned int flags)
{
	unsigned long long frac;

	if (precision <= 0 || (flags & TIMEFMT_SECONDS))
		return 0;

	frac = ts->tv_nsec;
	if (precision > 9)
		precision = 9;
	for (int i = precision; i < 9; i++)
		frac /= 10;
	return bprintfrr(buf, ".%0*llu", precision, frac);
}

/*
 * 绝对时间(日历)格式化：解析子修饰 p(用空格分隔日期与时间)与
 * i(ISO8601 的 T 分隔)，把输入统一换算成 realtime 时间点后输出；
 * 支持 realtime / monotonic / since / until 四种输入解释。
 */
static ssize_t printfrr_abstime(struct fbuf *buf, struct printfrr_eargs *ea,
				const struct timespec *ts, unsigned int flags)
{
	struct timespec real_ts[1];
	struct tm tm;
	char cbuf[32] = ""; /* manpage says 26 for ctime_r */
	ssize_t ret = 0;
	int precision = ea->precision;

	while (ea->fmt[0]) {
		char ch = *ea->fmt++;

		switch (ch) {
		case 'p':
			flags |= TIMEFMT_SPACE;
			continue;
		case 'i':
			flags |= TIMEFMT_ISO8601;
			continue;
		}

		ea->fmt--;
		break;
	}

	if (flags & TIMEFMT_SKIP)
		return 0;
	if (!ts)
		return bputch(buf, '-');

	if (flags & TIMEFMT_REALTIME)
		*real_ts = *ts;
	else if (flags & TIMEFMT_MONOTONIC) {
		struct timespec mono_now[1];

		clock_gettime(CLOCK_REALTIME, real_ts);
		clock_gettime(CLOCK_MONOTONIC, mono_now);

		timespecsub(real_ts, mono_now, real_ts);
		timespecadd(real_ts, ts, real_ts);
	} else {
		clock_gettime(CLOCK_REALTIME, real_ts);

		if (flags & TIMEFMT_SINCE)
			timespecsub(real_ts, ts, real_ts);
		else /* flags & TIMEFMT_UNTIL */
			timespecadd(real_ts, ts, real_ts);
	}

	localtime_r(&real_ts->tv_sec, &tm);

	if (flags & TIMEFMT_ISO8601) {
		if (flags & TIMEFMT_SPACE)
			strftime(cbuf, sizeof(cbuf), "%Y-%m-%d %H:%M:%S", &tm);
		else
			strftime(cbuf, sizeof(cbuf), "%Y-%m-%dT%H:%M:%S", &tm);
		ret += bputs(buf, cbuf);

		if (precision == -1)
			precision = 3;
		ret += do_subsec(buf, real_ts, precision, flags);
	} else {
		size_t len;

		asctime_r(&tm, cbuf);

		len = strlen(cbuf);
		if (!len)
			/* WTF. */
			return 0;
		if (cbuf[len - 1] == '\n')
			cbuf[len - 1] = '\0';

		ret += bputs(buf, cbuf);
	}
	return ret;
}

/*
 * 相对时间(时长)格式化：解析子修饰 p(空格) t(basic) d(十进制秒)
 * m(MM:SS) h(HH:MM:SS) x(负值 / 零显示占位横线)，按需换算成
 * 周 / 天 / 时 / 分 / 秒并选择输出样式。
 */
static ssize_t printfrr_reltime(struct fbuf *buf, struct printfrr_eargs *ea,
				const struct timespec *ts, unsigned int flags)
{
	struct timespec real_ts[1];
	ssize_t ret = 0;
	const char *space = "";
	const char *dashes = "-";
	int precision = ea->precision;

	while (ea->fmt[0]) {
		char ch = *ea->fmt++;

		switch (ch) {
		case 'p':
			flags |= TIMEFMT_SPACE;
			space = " ";
			continue;
		case 't':
			flags |= TIMEFMT_BASIC;
			continue;
		case 'd':
			flags |= TIMEFMT_DECIMAL;
			continue;
		case 'm':
			flags |= TIMEFMT_MMSS;
			dashes = "--:--";
			continue;
		case 'h':
			flags |= TIMEFMT_HHMMSS;
			dashes = "--:--:--";
			continue;
		case 'x':
			flags |= TIMEFMT_DASHES;
			continue;
		}

		ea->fmt--;
		break;
	}

	if (flags & TIMEFMT_SKIP)
		return 0;
	if (!ts)
		return bputch(buf, '-');

	if (flags & TIMEFMT_ABSOLUTE) {
		struct timespec anchor[1];

		if (flags & TIMEFMT_REALTIME)
			clock_gettime(CLOCK_REALTIME, anchor);
		else
			clock_gettime(CLOCK_MONOTONIC, anchor);
		if (flags & TIMEFMT_UNTIL)
			timespecsub(ts, anchor, real_ts);
		else /* flags & TIMEFMT_SINCE */
			timespecsub(anchor, ts, real_ts);
	} else
		*real_ts = *ts;

	if (real_ts->tv_sec == 0 && real_ts->tv_nsec == 0 &&
	    (flags & TIMEFMT_DASHES))
		return bputs(buf, dashes);

	if (real_ts->tv_sec < 0) {
		if (flags & TIMEFMT_DASHES)
			return bputs(buf, dashes);

		/* -0.3s is { -1s + 700ms } */
		real_ts->tv_sec = -real_ts->tv_sec - 1;
		real_ts->tv_nsec = 1000000000L - real_ts->tv_nsec;
		if (real_ts->tv_nsec >= 1000000000L) {
			real_ts->tv_sec++;
			real_ts->tv_nsec -= 1000000000L;
		}

		/* all formats have a - make sense in front */
		ret += bputch(buf, '-');
	}

	if (flags & TIMEFMT_DECIMAL) {
		ret += bprintfrr(buf, "%lld", (long long)real_ts->tv_sec);
		if (precision == -1)
			precision = 3;
		ret += do_subsec(buf, real_ts, precision, flags);
		return ret;
	}

	/* these divisions may be slow on embedded boxes, hence only do the
	 * ones we need, plus the ?: zero check to hopefully skip zeros fast
	 */
	lldiv_t min_sec = lldiv(real_ts->tv_sec, 60);

	if (flags & TIMEFMT_MMSS) {
		ret += bprintfrr(buf, "%02lld:%02lld", min_sec.quot,
				 min_sec.rem);
		ret += do_subsec(buf, real_ts, precision, flags);
		return ret;
	}

	lldiv_t hour_min = min_sec.quot ? lldiv(min_sec.quot, 60) : (lldiv_t){};

	if (flags & TIMEFMT_HHMMSS) {
		ret += bprintfrr(buf, "%02lld:%02lld:%02lld", hour_min.quot,
				 hour_min.rem, min_sec.rem);
		ret += do_subsec(buf, real_ts, precision, flags);
		return ret;
	}

	lldiv_t day_hour =
		hour_min.quot ? lldiv(hour_min.quot, 24) : (lldiv_t){};
	lldiv_t week_day =
		day_hour.quot ? lldiv(day_hour.quot, 7) : (lldiv_t){};

	/* if sub-second precision is not supported, return */
	if (flags & TIMEFMT_BASIC) {
		/* match frrtime_to_interval (without space flag) */
		if (week_day.quot)
			ret += bprintfrr(buf, "%lldw%s%lldd%s%02lldh",
					 week_day.quot, space, week_day.rem,
					 space, day_hour.rem);
		else if (day_hour.quot)
			ret += bprintfrr(buf, "%lldd%s%02lldh%s%02lldm",
					 day_hour.quot, space, day_hour.rem,
					 space, hour_min.rem);
		else
			ret += bprintfrr(buf, "%02lld:%02lld:%02lld",
					 hour_min.quot, hour_min.rem,
					 min_sec.rem);
		/* no sub-seconds here */
		return ret;
	}

	/* default format */
	if (week_day.quot)
		ret += bprintfrr(buf, "%lldw%s", week_day.quot, space);
	if (week_day.rem || week_day.quot)
		ret += bprintfrr(buf, "%lldd%s", week_day.rem, space);

	ret += bprintfrr(buf, "%02lld:%02lld:%02lld", day_hour.rem,
			 hour_min.rem, min_sec.rem);

	if (precision == -1)
		precision = 3;
	ret += do_subsec(buf, real_ts, precision, flags);
	return ret;
}

/* 注册指针型扩展 "TS"(timespec)：直接格式化 struct timespec */
printfrr_ext_autoreg_p("TS", printfrr_ts);
/* timespec 扩展实现：vptr 即 struct timespec 指针，交给 printfrr_time */
static ssize_t printfrr_ts(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *vptr)
{
	const struct timespec *ts = vptr;

	return printfrr_time(buf, ea, ts, 0);
}

/* 注册指针型扩展 "TV"(timeval)：先转成 timespec 再格式化 */
printfrr_ext_autoreg_p("TV", printfrr_tv);
/* timeval 扩展实现：NULL 直接交给 printfrr_time 处理(输出 '-')，
 * 否则 tv_usec*1000 得到 nsec */
static ssize_t printfrr_tv(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *vptr)
{
	const struct timeval *tv = vptr;
	struct timespec ts;

	if (!tv)
		return printfrr_time(buf, ea, NULL, 0);

	ts.tv_sec = tv->tv_sec;
	ts.tv_nsec = tv->tv_usec * 1000;
	return printfrr_time(buf, ea, &ts, 0);
}

/* 注册指针型扩展 "TT"(time_t)：以 TIMEFMT_SECONDS 输出整秒 */
printfrr_ext_autoreg_p("TT", printfrr_tt);
/* time_t 扩展实现：无论 NULL 与否都带 TIMEFMT_SECONDS 调用
 * printfrr_time，只显示秒精度 */
static ssize_t printfrr_tt(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *vptr)
{
	const time_t *tt = vptr;
	struct timespec ts;

	if (!tt)
		return printfrr_time(buf, ea, NULL, TIMEFMT_SECONDS);

	ts.tv_sec = *tt;
	ts.tv_nsec = 0;
	return printfrr_time(buf, ea, &ts, TIMEFMT_SECONDS);
}
