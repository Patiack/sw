// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * "Sequence" lock primitive
 *
 * Copyright (C) 2015  David Lamparter <equinox@diac24.net>
 */

/*
 * 【文件导读】seqlock.h —— "序列锁"（sequence lock）原语接口
 *
 * 一、职责
 *   提供一种面向 1:N 场景的轻量同步原语，用于实现 RCU 式的引用回收：有一个全局
 *   递增的"计数器"锁（counter seqlock），多个读者各持自己的 seqlock 记录进度；
 *   清理线程不断推进计数器，等待所有读者越过某序列号后即可安全回收该点以后被
 *   丢弃的引用。
 *
 * 二、核心数据结构
 *   seqlock_ctr_t（_Atomic uint32_t）：锁位置计数器的原子类型。
 *   seqlock_val_t（uint32_t）        ：对外使用的序列号值类型。
 *   struct seqlock                   ：一把锁，含原子计数 pos，以及（非 Linux
 *                                      无 futex 时的备用）pthread mutex + cond。
 *
 * 三、与其他模块的关系
 *   依赖 frratomic.h 的原子操作；Linux 上底层用 futex 实现等待 / 唤醒，非 Linux
 *   平台退化为 pthread mutex/cond。被 RCU 回收机制用作进度跟踪。
 *
 * 四、推荐阅读顺序
 *   先读本文件顶部英文注释里的 1:N 模型与线程时序图，再看类型 typedef、各
 *   SEQLOCK_* 宏的位含义、struct seqlock，最后按 init、acquire(_val)、release、
 *   bump、cur、wait / timedwait / check、held 的顺序读函数声明。
 *
 * 五、易踩的坑
 *   1) 低位有特殊含义：bit0 = SEQLOCK_HELD（已持有），bit1 = SEQLOCK_WAITERS
 *      （有等待者）；计数沿 1、5、9…递增（每次 +SEQLOCK_INCR=4），0 表示未持有。
 *   2) SEQLOCK_WAITERS 只有在 SEQLOCK_HELD 置位时才允许出现——未持有的锁不可能
 *      有等待者；seqlock_assert_valid 用于校验这一点。
 *   3) seqlock_held 用 relaxed 内存序，只适合"粗判"，不构成同步点。
 *   4) 内存序：跨线程发布数据必须配合正确的 acquire / release 语义，切勿把
 *      relaxed 读当成同步屏障。
 *   5) release 之后通常还要对 counter 锁 bump 一次，否则清理进度可能停滞。
 */

#ifndef _SEQLOCK_H
#define _SEQLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "frratomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * this locking primitive is intended to use in a 1:N setup.
 *
 * - one "counter" seqlock issuing increasing numbers
 * - multiple seqlock users hold references on these numbers
 *
 * this is intended for implementing RCU reference-holding.  There is one
 * global counter, with threads locking a seqlock whenever they take a
 * reference.  A seqlock can also be idle/unlocked.
 *
 * The "counter" seqlock will always stay locked;  the RCU cleanup thread
 * continuously counts it up, waiting for threads to release or progress to a
 * sequence number further ahead.  If all threads are > N, references dropped
 * in N can be free'd.
 *
 * generally, the lock function is:
 *
 * Thread-A                  Thread-B
 *
 * seqlock_acquire(a)
 *    | running              seqlock_wait(b)      -- a <= b
 * seqlock_release()            | blocked
 * OR: seqlock_acquire(a')      |                 -- a' > b
 *                           (resumes)
 */

/* use sequentially increasing "ticket numbers".  lowest bit will always
 * be 1 to have a 'cleared' indication (i.e., counts 1,5,9,13,etc. )
 * 2nd lowest bit is used to indicate we have waiters.
 */
/*
 * 【类型】seqlock_ctr_t / seqlock_val_t
 *   seqlock_ctr_t：seqlock 内部计数器的原子类型（_Atomic uint32_t），只能用原子
 *     操作读写，不可当普通整数直接赋值。
 *   seqlock_val_t：对外传递的序列号值类型（普通 uint32_t），供 acquire / wait
 *     等接口的参数使用。
 */
typedef _Atomic uint32_t	seqlock_ctr_t;
typedef uint32_t		seqlock_val_t;
/*
 * 【宏】seqlock_assert_valid(val)
 *   断言 val 已置 SEQLOCK_HELD 位（即该锁处于"已持有"状态）。用于在调试期捕获
 *   "对未持有锁的误用"；release 构建下 assert 可能被裁掉。
 */
#define seqlock_assert_valid(val) assert((val) & SEQLOCK_HELD)

/* NB: SEQLOCK_WAITERS is only allowed if SEQLOCK_HELD is also set; can't
 * have waiters on an unheld seqlock
 */
/*
 * 【宏】SEQLOCK_HELD / SEQLOCK_WAITERS / SEQLOCK_VAL / SEQLOCK_STARTVAL /
 *        SEQLOCK_INCR —— 计数器的位定义与步长
 *   SEQLOCK_HELD    (1U<<0)：bit0，表示该 seqlock 已持有（计数非 0）。
 *   SEQLOCK_WAITERS (1U<<1)：bit1，表示有线程正在等待；仅当 HELD 置位时才允许。
 *   SEQLOCK_VAL(n)         ：清掉 n 的 bit1（WAITERS），得到纯序列号。
 *   SEQLOCK_STARTVAL = 1U  ：初始 / 首个计数值（bit0 置位，即"已持有"）。
 *   SEQLOCK_INCR     = 4U  ：每次 bump 的步长，跳过 bit0 / bit1，使新值仍带 HELD。
 */
#define SEQLOCK_HELD		(1U << 0)
#define SEQLOCK_WAITERS		(1U << 1)
#define SEQLOCK_VAL(n)		((n) & ~SEQLOCK_WAITERS)
#define SEQLOCK_STARTVAL	1U
#define SEQLOCK_INCR		4U

/* TODO: originally, this was using "atomic_fetch_add", which is the reason
 * bit 0 is used to indicate held state.  With SEQLOCK_WAITERS added, there's
 * no fetch_add anymore (cmpxchg loop instead), so we don't need to use bit 0
 * for this anymore & can just special-case the value 0 for it and skip it in
 * counting.
 */

/*
 * 【结构体】struct seqlock —— 一把序列锁实例
 *   pos ：原子计数器（seqlock_ctr_t）。0 表示未持有；非 0 时低位携带 HELD 与
 *         WAITERS 标志。所有读取都必须经原子操作。
 *   lock：备用 pthread 互斥量，仅在无 futex 的平台（非 Linux）用于保护等待逻辑。
 *   wake：备用 pthread 条件变量，配合 lock 实现阻塞 / 唤醒。
 *   坑：pos 才是唯一的同步真相来源；lock / wake 只在无 futex 时被使用，切勿
 *       假设它们在所有平台上都会被初始化或使用。
 */
struct seqlock {
/* always used */
	seqlock_ctr_t pos;
/* used when futexes not available: (i.e. non-linux) */
	pthread_mutex_t lock;
	pthread_cond_t  wake;
};


/* sqlo = 0 - init state: not held */
/*
 * 【函数】seqlock_init(sqlo)
 *   初始化一把 seqlock，将其置为"未持有"状态（pos = 0）。
 *   参数 sqlo：待初始化的锁。返回：无。
 *   坑：必须在任何 acquire / wait 之前调用，且一个锁只能初始化一次。
 */
extern void seqlock_init(struct seqlock *sqlo);


/* basically: "while (sqlo <= val) wait();"
 * returns when sqlo > val || !seqlock_held(sqlo)
 */
/*
 * 【函数】seqlock_wait(sqlo, val)
 *   阻塞等待，语义等价于 "while (sqlo <= val) wait();"。
 *   当 sqlo 的位置推进到大于 val，或该锁变为"非持有"时返回。
 *   参数 sqlo：观察的锁；val：调用者当前的序列号。返回：无。
 *   坑：等待可能被虚假唤醒，底层会重试判断；调用前需确保 val 是有效序列号。
 */
extern void seqlock_wait(struct seqlock *sqlo, seqlock_val_t val);

/* same, but time-limited (limit is an absolute CLOCK_MONOTONIC value) */
/*
 * 【函数】seqlock_timedwait(sqlo, val, abs_monotime_limit)
 *   与 seqlock_wait 相同，但带超时。limit 是绝对时间（CLOCK_MONOTONIC 基准，
 *   注意是"绝对时间点"而非相对时长）。
 *   返回：条件满足而返回 true；超时返回 false。
 *   坑：误传相对时长会立即超时，必须自行换算成绝对时间点。
 */
extern bool seqlock_timedwait(struct seqlock *sqlo, seqlock_val_t val,
			      const struct timespec *abs_monotime_limit);

/* one-shot test, returns true if seqlock_wait would return immediately */
/*
 * 【函数】seqlock_check(sqlo, val)
 *   非阻塞的一次性测试：若此刻调用 seqlock_wait 会立即返回则为 true。
 *   参数 sqlo：观察的锁；val：当前序列号。返回：bool，表示"无需等待"。
 *   用途：进入慢速阻塞路径前做快速判断，省去无谓的锁开销。
 */
extern bool seqlock_check(struct seqlock *sqlo, seqlock_val_t val);

/*
 * 【内联函数】seqlock_held(sqlo)
 *   以 relaxed 内存序原子读取 pos，非 0 即表示该 seqlock 处于"已持有"状态。
 *   参数 sqlo：锁。返回：bool。
 *   坑：relaxed 读只提供"是否持有"的粗判，不建立任何内存同步点；不要据此做
 *       跨线程的数据可见性假设。
 */
static inline bool seqlock_held(struct seqlock *sqlo)
{
	return !!atomic_load_explicit(&sqlo->pos, memory_order_relaxed);
}

/* sqlo - get seqlock position -- for the "counter" seqlock */
/*
 * 【函数】seqlock_cur(sqlo)
 *   读取并返回当前 seqlock 的位置值（主要用于"计数器"锁，供其他锁 acquire 时
 *   参照）。返回：seqlock_val_t。
 *   坑：返回值可能含 WAITERS 位，比较时应用 SEQLOCK_VAL 掩掉或按相等语义处理。
 */
extern seqlock_val_t seqlock_cur(struct seqlock *sqlo);

/* ++sqlo (but atomic & wakes waiters) - returns value that we bumped to.
 *
 * guarantees:
 *  - each seqlock_bump call bumps the position by exactly one SEQLOCK_INCR.
 *    There are no skipped/missed or multiple increments.
 *  - each return value is only returned from one seqlock_bump() call
 */
/*
 * 【函数】seqlock_bump(sqlo)
 *   原子地把 pos 递增一个 SEQLOCK_INCR（并唤醒等待者），返回递增后的新值。
 *   保证：每次调用恰好 +SEQLOCK_INCR，不漏不重；返回值只可能来自唯一一次 bump。
 *   返回：递增后的序列号。
 *   用途：由"计数器"锁的持有者在清理路径里持续推进序列号。
 */
extern seqlock_val_t seqlock_bump(struct seqlock *sqlo);


/* sqlo = val - can be used on held seqlock. */
/*
 * 【函数】seqlock_acquire_val(sqlo, val)
 *   把 sqlo 的位置直接设为 val（可作用于已持有的锁），让该读者声明自己当前
 *   处于序列号 val。返回：无。
 *   坑：val 必须是有效的序列号；设置后该锁即被视为已持有。
 */
extern void seqlock_acquire_val(struct seqlock *sqlo, seqlock_val_t val);

/* sqlo = ref - standard pattern: acquire relative to other seqlock */
/*
 * 【内联函数】seqlock_acquire(sqlo, ref)
 *   标准用法：以另一把锁 ref 的当前位置为基准，把 sqlo 的位置设为 ref 的当前值。
 *   等价于 seqlock_acquire_val(sqlo, seqlock_cur(ref))。
 *   参数 sqlo：要设置的读者锁；ref：参照的（通常是 counter）锁。返回：无。
 */
static inline void seqlock_acquire(struct seqlock *sqlo, struct seqlock *ref)
{
	seqlock_acquire_val(sqlo, seqlock_cur(ref));
}

/* sqlo = 0 - set seqlock position to 0, marking as non-held */
/*
 * 【函数】seqlock_release(sqlo)
 *   把 sqlo 的位置置 0，标记为"未持有"。
 *   返回：无。
 *   坑：源码注释提醒——release 之后通常还需对"计数器"锁做一次 bump（除非仅仅
 *       是读取了一下 RCU 项），否则清理进度可能停滞。
 */
extern void seqlock_release(struct seqlock *sqlo);
/* release should normally be followed by a bump on the "counter", if
 * anything other than reading RCU items was done
 */

#ifdef __cplusplus
}
#endif

#endif /* _SEQLOCK_H */
