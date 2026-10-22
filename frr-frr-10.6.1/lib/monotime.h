// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2017  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读（lib/monotime.h）
 *
 * 本文件提供“单调时钟”相关接口与一批 timespec / timeval 的便利宏。
 *
 * 背景：wall-clock（realtime）会因 NTP 校时、手动改时间而跳变，不适合
 * 测量时间间隔或做定时器；单调时钟（CLOCK_MONOTONIC）只单调递增，故
 * FRR 的定时器与耗时统计统一基于它。本文件返回的“时间”多为单调时间的
 * 秒数，可与 realtime 相互换算用于展示。
 *
 * 核心内容：
 *   - TIMESPEC_TO_TIMEVAL / TIMEVAL_TO_TIMESPEC：两种时间结构互转；
 *   - timespecadd / timespecsub：timespec 加减（glibc 缺失故自带）；
 *   - monotime：取当前单调时间；monotime_since / monotime_until：相对某
 *     参考点已过去 / 尚余多少微秒；monotime_to_realtime：单调时间换算成
 *     realtime；
 *   - time_to_string / time_to_string_json / frrtime_to_interval：单调时间
 *     转可读字符串；
 *   - enum TIMEFMT_*：给 printfrr_time 用的格式与输入解释标志；
 *   - printfrr_time：底层格式化函数。
 *
 * 阅读顺序：时间结构互转宏 -> 加减宏 -> monotime 系列 -> 字符串化函数 ->
 * TIMEFMT 标志与 printfrr_time。
 */

#ifndef _FRR_MONOTIME_H
#define _FRR_MONOTIME_H

#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fbuf;
struct printfrr_eargs;

/* timespec 转 timeval（纳秒换算成微秒）。BSD / Linux libc 通常已提供，
 * 故仅在缺失时定义。 */
#ifndef TIMESPEC_TO_TIMEVAL
/* should be in sys/time.h on BSD & Linux libcs */
#define TIMESPEC_TO_TIMEVAL(tv, ts)                                            \
	do {                                                                   \
		(tv)->tv_sec = (ts)->tv_sec;                                   \
		(tv)->tv_usec = (ts)->tv_nsec / 1000;                          \
	} while (0)
#endif
/* timeval 转 timespec（微秒换算成纳秒）；缺失时定义。 */
#ifndef TIMEVAL_TO_TIMESPEC
/* should be in sys/time.h on BSD & Linux libcs */
#define TIMEVAL_TO_TIMESPEC(tv, ts)                                            \
	do {                                                                   \
		(ts)->tv_sec = (tv)->tv_sec;                                   \
		(ts)->tv_nsec = (tv)->tv_usec * 1000;                          \
	} while (0)
#endif

/* Linux/glibc is sadly missing these timespec helpers */
/* timespec 相加：逐字段相加并处理纳秒进位。Linux / glibc 缺此辅助，故自带。 */
#ifndef timespecadd
#define timespecadd(tsp, usp, vsp)                                             \
	do {                                                                   \
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;                 \
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;              \
		if ((vsp)->tv_nsec >= 1000000000L) {                           \
			(vsp)->tv_sec++;                                       \
			(vsp)->tv_nsec -= 1000000000L;                         \
		}                                                              \
	} while (0)
#endif

/* timespec 相减：逐字段相减并处理纳秒借位。 */
#ifndef timespecsub
#define timespecsub(tsp, usp, vsp)                                             \
	do {                                                                   \
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;                 \
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;              \
		if ((vsp)->tv_nsec < 0) {                                      \
			(vsp)->tv_sec--;                                       \
			(vsp)->tv_nsec += 1000000000L;                         \
		}                                                              \
	} while (0)
#endif

/* 取当前单调时间。若非空，tvo 回填对应的 timeval；返回值仅秒部分。 */
static inline time_t monotime(struct timeval *tvo)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	if (tvo) {
		TIMESPEC_TO_TIMEVAL(tvo, &ts);
	}
	return ts.tv_sec;
}

/* 常用秒数常量：一天 / 一周 / 一年（按 365 天）的秒数。 */
#define ONE_DAY_SECOND (60 * 60 * 24)
#define ONE_WEEK_SECOND (ONE_DAY_SECOND * 7)
#define ONE_YEAR_SECOND (ONE_DAY_SECOND * 365)

/* 下面两个函数返回微秒数（而非 time_t）。二者互为相反数形式，同时保留
 * 是为了让调用处语义更清晰。 */
/* the following two return microseconds, not time_t!
 *
 * also, they're negative forms of each other, but having both makes the
 * code more readable
 */
/* 自参考时刻 ref 到当前已过去的微秒数；out 非空时回填时间差。 */
static inline int64_t monotime_since(const struct timeval *ref,
				     struct timeval *out)
{
	struct timeval tv;
	monotime(&tv);
	timersub(&tv, ref, &tv);
	if (out)
		*out = tv;
	return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* 从当前到参考时刻 ref 尚余的微秒数；out 非空时回填时间差。 */
static inline int64_t monotime_until(const struct timeval *ref,
				     struct timeval *out)
{
	struct timeval tv;
	monotime(&tv);
	timersub(ref, &tv, &tv);
	if (out)
		*out = tv;
	return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* 把给定的单调时间 mono 换算成墙钟（realtime）时间：先用当前 realtime 减去
 * 自 mono 起经过的时长。realout 非空时回填，返回值仅秒部分。 */
static inline time_t monotime_to_realtime(const struct timeval *mono,
					  struct timeval *realout)
{
	struct timeval delta, real;

	monotime_since(mono, &delta);
	gettimeofday(&real, NULL);

	timersub(&real, &delta, &real);
	if (realout)
		*realout = real;
	return real.tv_sec;
}

/* Char buffer size for time-to-string api */
/* 时间转字符串所需的缓冲长度。 */
#define MONOTIME_STRLEN 32

/* 把单调时间 ts 转为墙钟可读字符串：换算成 realtime 后经 ctime_r 格式化。
 * 返回 buf。 */
static inline char *time_to_string(time_t ts, char *buf)
{
	struct timeval tv;
	time_t tbuf;

	monotime(&tv);
	tbuf = time(NULL) - (tv.tv_sec - ts);

	return ctime_r(&tbuf, buf);
}

/* time_to_string 的包装：去掉结尾换行，供 JSON 输出使用。 */
/* A wrapper for time_to_string() which removes newline at the end.
 * This is needed for JSON outputs, where newline is not expected.
 */
static inline char *time_to_string_json(time_t ts, char *buf)
{
	size_t len;

	time_to_string(ts, buf);
	len = strlen(buf);

	if (len && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	return buf;
}

/* 把时间间隔转成对人友好的字符串，用于 CLI 展示：小于一天显示 hh:mm:ss，
 * 小于一周显示 Ndhhhmm，否则显示周数 / 天数 / 小时。 */
/* Convert interval to human-friendly string, used in cli output e.g. */
static inline const char *frrtime_to_interval(time_t t, char *buf,
					      size_t buflen)
{
	struct tm tm;

	gmtime_r(&t, &tm);

	if (t < ONE_DAY_SECOND)
		snprintf(buf, buflen, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
			 tm.tm_sec);
	else if (t < ONE_WEEK_SECOND)
		snprintf(buf, buflen, "%dd%02dh%02dm", tm.tm_yday, tm.tm_hour,
			 tm.tm_min);
	else
		snprintf(buf, buflen, "%02dw%dd%02dh", tm.tm_yday / 7,
			 tm.tm_yday - ((tm.tm_yday / 7) * 7), tm.tm_hour);
	return buf;
}

/* printfrr_time 的格式与输入解释标志。低位为输出格式控制（秒 / 预选 /
 * 跳过 / 空格），中位为输入时间语义（realtime / 单调 / since / until），
 * 高位为日历与区间格式选择；TIMEFMT_ABSOLUTE 等为组合出的便捷别名。 */
enum {
	/* n/a - input was seconds precision, don't print any fractional */
	TIMEFMT_SECONDS = (1 << 0),
	/* caller is directly invoking printfrr_time and has pre-specified
	 * I/Iu/Is/M/Mu/Ms/R/Ru/Rs (for printing timers)
	 */
	TIMEFMT_PRESELECT = (1 << 1),
	/* don't print any output - this is needed for invoking printfrr_time
	 * from another printfrr extensions to skip over flag characters
	 */
	TIMEFMT_SKIP = (1 << 2),
	/* use spaces in appropriate places */
	TIMEFMT_SPACE = (1 << 3),

	/* input interpretations: */
	TIMEFMT_REALTIME = (1 << 8),
	TIMEFMT_MONOTONIC = (1 << 9),
	TIMEFMT_SINCE = (1 << 10),
	TIMEFMT_UNTIL = (1 << 11),

	TIMEFMT_ABSOLUTE = TIMEFMT_REALTIME | TIMEFMT_MONOTONIC,
	TIMEFMT_ANCHORS = TIMEFMT_SINCE | TIMEFMT_UNTIL,

	/* calendaric formats: */
	TIMEFMT_ISO8601 = (1 << 16),

	/* interval formats: */
	/* 't' - use [t]raditional 3-block format */
	TIMEFMT_BASIC = (1 << 24),
	/* 'm' - select mm:ss */
	TIMEFMT_MMSS = (1 << 25),
	/* 'h' - select hh:mm:ss */
	TIMEFMT_HHMMSS = (1 << 26),
	/* 'd' - print as decimal number of seconds */
	TIMEFMT_DECIMAL = (1 << 27),
	/* 'mx'/'hx' - replace zero value with "--:--" or "--:--:--" */
	TIMEFMT_DASHES = (1 << 31),

	/* helpers for reference */
	TIMEFMT_TIMER_DEADLINE =
		TIMEFMT_PRESELECT | TIMEFMT_MONOTONIC | TIMEFMT_UNTIL,
	TIMEFMT_TIMER_INTERVAL = TIMEFMT_PRESELECT,
};

/* 底层时间格式化函数，供 printfrr 的时间扩展调用：把 ts 按 flags 渲染到
 * fbuf。返回写入长度。 */
extern ssize_t printfrr_time(struct fbuf *buf, struct printfrr_eargs *ea,
			     const struct timespec *ts, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_MONOTIME_H */
