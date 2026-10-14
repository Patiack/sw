// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * "Sequence" lock primitive
 *
 * Copyright (C) 2015  David Lamparter <equinox@diac24.net>
 */

/*
 * 【文件导读】seqlock.c —— "序列锁"原语在各操作系统上的实现
 *
 * 一、职责
 *   实现 seqlock.h 声明的接口。核心是用原子计数器 pos 表示锁位置，并借助平台提供
 *   的等待 / 唤醒机制（futex、umtx 或 pthread condvar）让读者在序列号落后时阻塞，
 *   直到计数器推进或锁被释放。
 *
 * 二、核心结构
 *   复用 seqlock.h 的 struct seqlock：pos 是唯一的原子真相来源；非 Linux 平台额外
 *   用到 lock / wake 两个 pthread 对象。本文件用一组 wait_xxx 宏把不同平台的等待
 *   原语统一成同一套调用（wait_prep / wait_once / wait_time / wait_done /
 *   wait_poke / wait_init）。
 *
 * 三、与其他模块的关系
 *   依赖 seqlock.h 的宏与类型、frratomic.h 的 C11 原子操作。Linux 走 sys_futex，
 *   OpenBSD 走 futex()，FreeBSD 走 _umtx_op()，其余平台退化为通用的 pthread
 *   mutex + condvar 版本。被 RCU 回收路径使用。
 *
 * 四、推荐阅读顺序
 *   先看顶部"OS specific synchronization wrappers"分段，弄清每个平台把 wait_once
 *   / wait_time / wait_poke 映射成了什么；再看 fallback 空实现；最后按 seqlock_wait、
 *   seqlock_timedwait、seqlock_check、seqlock_acquire_val、seqlock_release、
 *   seqlock_init、seqlock_cur、seqlock_bump 的顺序读实现。
 *
 * 五、易踩的坑
 *   1) 所有对 pos 的访问都必须是原子操作；直接赋值会破坏并发正确性。
 *   2) 等待循环里 cal = SEQLOCK_VAL(cur) - val - 1 用无符号环绕比较来判定"读者是否
 *      领先"，assert 保证差值落在合法区间，勿随意改动这段算术。
 *   3) 置 WAITERS 位与真正进入等待之间用 compare_exchange 保证不丢唤醒，否则会因
 *      "唤醒丢失"导致永久阻塞。
 *   4) 启用 TSAN 时，futex / umtx 分支会被强制关闭、退化为 condvar 版本，以便 TSAN
 *      能理解同步关系。
 *   5) 平台差异全部由编译期宏选择，改动时务必保证各平台都有对应实现且能编过。
 */

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>

#include "seqlock.h"

/*
 * 【分段】OS 相关的同步原语封装层
 *   下面按平台把"等待 / 唤醒"抽象成统一的 wait_xxx 宏，使上层 seqlock_wait /
 *   seqlock_timedwait 的实现可以完全平台无关。可用的平台分支：Linux futex、
 *   OpenBSD futex、FreeBSD _umtx_op、以及通用 pthread 版本。
 */

/****************************************
 * OS specific synchronization wrappers *
 ****************************************/

/*
 * 【宏】__has_feature(x)
 *   兼容垫片：老版本 GCC 没有内建 __has_feature，这里统一定义为 0，使下方检测
 *   thread sanitizer 的 #if 条件在旧编译器下也能展开。新编译器（如 Clang）会提供
 *   真正的内建实现，此处的 #ifndef 不会覆盖它。
 */
#ifndef __has_feature /* not available on old GCC */
#define __has_feature(x) 0
#endif

/*
 * 【条件编译块】ThreadSanitizer 兼容处理
 *   TSAN 无法理解底层的 futex / umtx 调用，会误报大量告警，因此在启用 TSAN 时主动
 *   #undef 掉 HAVE_SYNC_LINUX_FUTEX / HAVE_SYNC_OPENBSD_FUTEX / HAVE_SYNC_UMTX_OP，
 *   使实现退化为基于 pthread mutex + condvar 的"通用版本"——TSAN 能正确理解后者。
 *   此处只改变编译期选择，运行语义等价。
 */
#if (defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer))
/* TSAN really does not understand what is going on with the low-level
 * futex/umtx calls.  This leads to a whole bunch of warnings, a lot of which
 * also have _extremely_ misleading text - since TSAN does not understand that
 * there is in fact a synchronization primitive involved, it can end up pulling
 * in completely unrelated things.
 *
 * What does work is the "unsupported platform" seqlock implementation based
 * on a pthread mutex + condvar, since TSAN of course suppports these.
 *
 * It may be possible to also fix this with TSAN annotations (__tsan_acquire
 * and __tsan_release), but using those (correctly) is not easy either, and
 * for now just get things rolling.
 */

#ifdef HAVE_SYNC_LINUX_FUTEX
#undef HAVE_SYNC_LINUX_FUTEX
#endif

#ifdef HAVE_SYNC_OPENBSD_FUTEX
#undef HAVE_SYNC_OPENBSD_FUTEX
#endif

#ifdef HAVE_SYNC_UMTX_OP
#undef HAVE_SYNC_UMTX_OP
#endif

#endif /* TSAN */

/*
 * Linux: sys_futex()
 */
#ifdef HAVE_SYNC_LINUX_FUTEX
#include <sys/syscall.h>
#include <linux/futex.h>

/*
 * 【函数】sys_futex(addr1, op, val1, timeout, addr2, val3)
 *   Linux 上对 futex 系统调用的薄封装：参数原样透传给 syscall(SYS_futex, ...)。
 *   返回：系统调用返回值（出错为 -1 并置 errno）。仅在 Linux 编译。
 */
static long sys_futex(void *addr1, int op, int val1,
		      const struct timespec *timeout, void *addr2, int val3)
{
	return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

/*
 * 【宏】wait_once / wait_time / wait_poke（Linux 版本）
 *   wait_once：在 pos 上执行 FUTEX_WAIT，阻塞到该地址的值发生变化；进入内核前会
 *              再次核对期望值 val，避免丢失唤醒。
 *   wait_time：用 FUTEX_WAIT_BITSET 实现带超时等待；time 为 CLOCK_MONOTONIC 绝对
 *              时间点（Linux 直接接受绝对时间）。
 *   wait_poke：用 FUTEX_WAKE 唤醒所有等待者（上限 INT_MAX）。
 *   坑：这里把 &sqlo->pos 强制转成 int * 传给 futex，依赖 pos 是 32 位原子量。
 */
#define wait_once(sqlo, val)	\
	sys_futex((int *)&sqlo->pos, FUTEX_WAIT, (int)val, NULL, NULL, 0)
#define wait_time(sqlo, val, time, reltime)	\
	sys_futex((int *)&sqlo->pos, FUTEX_WAIT_BITSET, (int)val, time, \
		  NULL, ~0U)
#define wait_poke(sqlo)		\
	sys_futex((int *)&sqlo->pos, FUTEX_WAKE, INT_MAX, NULL, NULL, 0)

/*
 * OpenBSD: sys_futex(), almost the same as on Linux
 */
/*
 * 【分段】OpenBSD 实现
 *   把 wait_once / wait_time / wait_poke 映射到 OpenBSD 的 futex()，语义与 Linux
 *   基本一致；TIME_RELATIVE 表明该实现的时间参数使用"相对时间"，因此上方
 *   seqlock_timedwait 里会先把绝对单调时间换算成相对时长。
 */
#elif defined(HAVE_SYNC_OPENBSD_FUTEX)
#include <sys/syscall.h>
#include <sys/futex.h>

#define TIME_RELATIVE		1

#define wait_once(sqlo, val)	\
	futex((int *)&sqlo->pos, FUTEX_WAIT, (int)val, NULL, NULL, 0)
#define wait_time(sqlo, val, time, reltime)	\
	futex((int *)&sqlo->pos, FUTEX_WAIT, (int)val, reltime, NULL, 0)
#define wait_poke(sqlo)		\
	futex((int *)&sqlo->pos, FUTEX_WAKE, INT_MAX, NULL, NULL, 0)

/*
 * FreeBSD: _umtx_op()
 */
/*
 * 【分段】FreeBSD 实现（_umtx_op）
 *   wait_once：用 UMTX_OP_WAIT_UINT 等待 pos 变化。
 *   wait_time：内联函数，构造 struct _umtx_time（_flags = UMTX_ABSTIME，
 *              _clockid = CLOCK_MONOTONIC）后调用 UMTX_OP_WAIT_UINT。
 *   返回：wait_time 返回系统调用结果，非 0 视为超时 / 出错。
 *   wait_poke：用 UMTX_OP_WAKE 唤醒等待者。
 */
#elif defined(HAVE_SYNC_UMTX_OP)
#include <sys/umtx.h>

#define wait_once(sqlo, val)	\
	_umtx_op((void *)&sqlo->pos, UMTX_OP_WAIT_UINT, val, NULL, NULL)
static int wait_time(struct seqlock *sqlo, uint32_t val,
		      const struct timespec *abstime,
		      const struct timespec *reltime)
{
	struct _umtx_time t;
	t._flags = UMTX_ABSTIME;
	t._clockid = CLOCK_MONOTONIC;
	memcpy(&t._timeout, abstime, sizeof(t._timeout));
	return _umtx_op((void *)&sqlo->pos, UMTX_OP_WAIT_UINT, val,
		 (void *)(uintptr_t) sizeof(t), &t);
}
#define wait_poke(sqlo)		\
	_umtx_op((void *)&sqlo->pos, UMTX_OP_WAKE, INT_MAX, NULL, NULL)

/*
 * generic version.  used on NetBSD, Solaris and OSX.  really shitty.
 */
#else

/*
 * 【分段】通用实现（NetBSD / Solaris / macOS 等，最"朴素"）
 *   用 pthread mutex + condvar 模拟等待：
 *     wait_init  在 seqlock_init 时初始化 mutex 与 cond；
 *     wait_prep / wait_done 在等待循环前后加解锁；
 *     wait_once / wait_time 调用 pthread_cond_wait / pthread_cond_timedwait；
 *     wait_poke  广播 cond 唤醒所有等待者。
 *   TIME_ABS_REALTIME 表明该版本的时间参数使用 CLOCK_REALTIME 绝对时间。
 */
#define TIME_ABS_REALTIME	1

#define wait_init(sqlo)		do { \
		pthread_mutex_init(&sqlo->lock, NULL); \
		pthread_cond_init(&sqlo->wake, NULL); \
	} while (0)
#define wait_prep(sqlo)		pthread_mutex_lock(&sqlo->lock)
#define wait_once(sqlo, val)	pthread_cond_wait(&sqlo->wake, &sqlo->lock)
#define wait_time(sqlo, val, time, reltime) \
				pthread_cond_timedwait(&sqlo->wake, \
						       &sqlo->lock, time);
#define wait_done(sqlo)		pthread_mutex_unlock(&sqlo->lock)
#define wait_poke(sqlo)		do { \
		pthread_mutex_lock(&sqlo->lock); \
		pthread_cond_broadcast(&sqlo->wake); \
		pthread_mutex_unlock(&sqlo->lock); \
	} while (0)

#endif

/*
 * 【宏】wait_init / wait_prep / wait_done 的空实现
 *   对使用 futex / umtx 的平台，这三步无需任何操作（同步由内核 futex 完成），
 *   故统一定义为空语句；这样上层等待循环在所有平台上写法一致。
 */
#ifndef wait_init
#define wait_init(sqlo)		/**/
#define wait_prep(sqlo)		/**/
#define wait_done(sqlo)		/**/
#endif /* wait_init */


/*
 * 【函数】seqlock_wait(sqlo, val)
 *   阻塞直到 sqlo 的位置超过 val，或该锁变为非持有状态。
 *   实现：在等待循环里读取 cur；用无符号环绕算术 cal = SEQLOCK_VAL(cur) - val - 1
 *   判断读者是否已领先（cal 落在"半区间"内即领先，可退出）；否则若尚未置 WAITERS，
 *   先用 compare_exchange 补置 WAITERS 位再真正等待，避免"唤醒丢失"。
 *   参数 sqlo：观察的锁；val：调用者自己的序列号。返回：无。
 *   坑：置 WAITERS 与进入等待必须成对，漏置会导致永久阻塞；assert 保证 cal 落在
 *       合法区间，改动这段算术务必谨慎。
 */
void seqlock_wait(struct seqlock *sqlo, seqlock_val_t val)
{
	seqlock_val_t cur, cal;

	seqlock_assert_valid(val);

	wait_prep(sqlo);
	cur = atomic_load_explicit(&sqlo->pos, memory_order_relaxed);

	while (cur & SEQLOCK_HELD) {
		cal = SEQLOCK_VAL(cur) - val - 1;
		assert(cal < 0x40000000 || cal > 0xc0000000);
		if (cal < 0x80000000)
			break;

		if ((cur & SEQLOCK_WAITERS)
		    || atomic_compare_exchange_weak_explicit(
				&sqlo->pos, &cur, cur | SEQLOCK_WAITERS,
				memory_order_relaxed, memory_order_relaxed)) {
			wait_once(sqlo, cur | SEQLOCK_WAITERS);
			cur = atomic_load_explicit(&sqlo->pos,
				memory_order_relaxed);
		}
		/* else: we failed to swap in cur because it just changed */
	}
	wait_done(sqlo);
}

/*
 * 【函数】seqlock_timedwait(sqlo, val, abs_monotime_limit)
 *   seqlock_wait 的带超时版本，limit 为 CLOCK_MONOTONIC 绝对时间点。
 *   为适配不同平台对"时间基准"的要求，函数内用 TIME_ABS_REALTIME / TIME_RELATIVE
 *   等编译期分支把单调时间换算成目标平台需要的形式（如转成 CLOCK_REALTIME 绝对
 *   时间或相对时长），再交给 wait_time。
 *   返回：因条件满足而返回 true；超时返回 false。
 *   坑：纳秒进位 / 借位的换算容易出错；不同平台 time_arg1 / time_arg2 / time_prep
 *       含义不同，修改时需保持三套分支语义一致。真正的等待循环结构与
 *       seqlock_wait 相同，只是 rv 非 0 即判为超时。
 */
bool seqlock_timedwait(struct seqlock *sqlo, seqlock_val_t val,
		       const struct timespec *abs_monotime_limit)
{
/*
 * ABS_REALTIME - used on NetBSD, Solaris and OSX
 */
#ifdef TIME_ABS_REALTIME
#define time_arg1 &abs_rt
#define time_arg2 NULL
#define time_prep
	struct timespec curmono, abs_rt;

	clock_gettime(CLOCK_MONOTONIC, &curmono);
	clock_gettime(CLOCK_REALTIME, &abs_rt);

	abs_rt.tv_nsec += abs_monotime_limit->tv_nsec - curmono.tv_nsec;
	if (abs_rt.tv_nsec < 0) {
		abs_rt.tv_sec--;
		abs_rt.tv_nsec += 1000000000;
	} else if (abs_rt.tv_nsec >= 1000000000) {
		abs_rt.tv_sec++;
		abs_rt.tv_nsec -= 1000000000;
	}
	abs_rt.tv_sec += abs_monotime_limit->tv_sec - curmono.tv_sec;

/*
 * RELATIVE - used on OpenBSD (might get a patch to get absolute monotime)
 */
#elif defined(TIME_RELATIVE)
	struct timespec reltime;

#define time_arg1 abs_monotime_limit
#define time_arg2 &reltime
#define time_prep \
	clock_gettime(CLOCK_MONOTONIC, &reltime);                              \
	reltime.tv_sec = abs_monotime_limit.tv_sec - reltime.tv_sec;           \
	reltime.tv_nsec = abs_monotime_limit.tv_nsec - reltime.tv_nsec;        \
	if (reltime.tv_nsec < 0) {                                             \
		reltime.tv_sec--;                                              \
		reltime.tv_nsec += 1000000000;                                 \
	}
/*
 * FreeBSD & Linux: absolute time re. CLOCK_MONOTONIC
 */
#else
#define time_arg1 abs_monotime_limit
#define time_arg2 NULL
#define time_prep
#endif

	bool ret = true;
	seqlock_val_t cur, cal;

	seqlock_assert_valid(val);

	wait_prep(sqlo);
	cur = atomic_load_explicit(&sqlo->pos, memory_order_relaxed);

	while (cur & SEQLOCK_HELD) {
		cal = SEQLOCK_VAL(cur) - val - 1;
		assert(cal < 0x40000000 || cal > 0xc0000000);
		if (cal < 0x80000000)
			break;

		if ((cur & SEQLOCK_WAITERS)
		    || atomic_compare_exchange_weak_explicit(
				&sqlo->pos, &cur, cur | SEQLOCK_WAITERS,
				memory_order_relaxed, memory_order_relaxed)) {
			int rv;

			time_prep

			rv = wait_time(sqlo, cur | SEQLOCK_WAITERS, time_arg1,
				       time_arg2);
			if (rv) {
				ret = false;
				break;
			}
			cur = atomic_load_explicit(&sqlo->pos,
				memory_order_relaxed);
		}
	}
	wait_done(sqlo);

	return ret;
}

/*
 * 【函数】seqlock_check(sqlo, val)
 *   非阻塞判断：若此刻调用 seqlock_wait 会立刻返回则为 true。
 *   实现：读 cur；若该锁未持有直接返回 true；否则算 cal 并判断是否已领先
 *   （cal < 0x80000000）。
 *   参数 sqlo：锁；val：当前序列号。返回：bool。
 */
bool seqlock_check(struct seqlock *sqlo, seqlock_val_t val)
{
	seqlock_val_t cur;

	seqlock_assert_valid(val);

	cur = atomic_load_explicit(&sqlo->pos, memory_order_relaxed);
	if (!(cur & SEQLOCK_HELD))
		return true;
	cur = SEQLOCK_VAL(cur) - val - 1;
	assert(cur < 0x40000000 || cur > 0xc0000000);
	return cur < 0x80000000;
}

/*
 * 【函数】seqlock_acquire_val(sqlo, val)
 *   原子地把 pos 换成 val（返回旧值）。若旧值带 WAITERS 位，说明有等待者，需
 *   wait_poke 唤醒它们重新评估条件。
 *   参数 sqlo：锁；val：新的序列号。返回：无。
 */
void seqlock_acquire_val(struct seqlock *sqlo, seqlock_val_t val)
{
	seqlock_val_t prev;

	seqlock_assert_valid(val);

	prev = atomic_exchange_explicit(&sqlo->pos, val, memory_order_relaxed);
	if (prev & SEQLOCK_WAITERS)
		wait_poke(sqlo);
}

/*
 * 【函数】seqlock_release(sqlo)
 *   原子地把 pos 置 0（标记为未持有）。若旧值带 WAITERS 位则唤醒等待者。
 *   参数 sqlo：锁。返回：无。
 */
void seqlock_release(struct seqlock *sqlo)
{
	seqlock_val_t prev;

	prev = atomic_exchange_explicit(&sqlo->pos, 0, memory_order_relaxed);
	if (prev & SEQLOCK_WAITERS)
		wait_poke(sqlo);
}

/*
 * 【函数】seqlock_init(sqlo)
 *   初始化：pos 置 0（未持有），并在需要平台初始化 mutex / cond（wait_init）。
 *   参数 sqlo：锁。返回：无。
 *   坑：只能初始化一次，且必须在所有使用之前调用。
 */
void seqlock_init(struct seqlock *sqlo)
{
	sqlo->pos = 0;
	wait_init(sqlo);
}


/*
 * 【函数】seqlock_cur(sqlo)
 *   返回当前位置的"纯序列号"（用 SEQLOCK_VAL 掩掉 WAITERS 位）。
 *   参数 sqlo：锁。返回：seqlock_val_t。
 */
seqlock_val_t seqlock_cur(struct seqlock *sqlo)
{
	return SEQLOCK_VAL(atomic_load_explicit(&sqlo->pos,
						memory_order_relaxed));
}

/*
 * 【函数】seqlock_bump(sqlo)
 *   在 compare_exchange 循环里把 pos 的序列号部分加一个 SEQLOCK_INCR，返回加后的
 *   值；若旧值带 WAITERS 位则唤醒等待者。
 *   参数 sqlo：锁。返回：递增后的序列号。
 *   保证：每次恰好 +SEQLOCK_INCR；返回值只可能来自唯一一次递增。
 *   坑：进入前用 seqlock_assert_valid(cur) 断言当前锁已持有，不可对未持有的计数器
 *       调用本函数。
 */
seqlock_val_t seqlock_bump(struct seqlock *sqlo)
{
	seqlock_val_t val, cur;

	cur = atomic_load_explicit(&sqlo->pos, memory_order_relaxed);
	seqlock_assert_valid(cur);

	do {
		val = SEQLOCK_VAL(cur) + SEQLOCK_INCR;
	} while (!atomic_compare_exchange_weak_explicit(&sqlo->pos, &cur, val,
			memory_order_relaxed, memory_order_relaxed));

	if (cur & SEQLOCK_WAITERS)
		wait_poke(sqlo);
	return val;
}
