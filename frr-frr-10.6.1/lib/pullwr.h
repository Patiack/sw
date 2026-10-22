// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pull-driven write event handler
 * Copyright (C) 2019  David Lamparter
 */

#ifndef _WRITEPOLL_H
#define _WRITEPOLL_H

/*
 * 【文件导读】pullwr.h —— 拉取式(pull-driven)写事件处理器
 *
 * 1. 职责：
 *    提供一个“由空间驱动、而非由数据驱动”的 socket 写接口。传统写法是
 *    上层把要发的数据塞进缓冲区；pullwr 反其道而行：当 socket 可写时，
 *    事件循环回调 fill()，由 fill() 按需“拉取”并喂入数据。
 *
 * 2. 核心概念：
 *    - struct pullwr      : 不透明句柄(定义在 pullwr.c)。
 *    - idle / active 状态 : active 时持续调用 fill() 收集数据；fill() 未再
 *      喂数据则转 idle。
 *    - fill(arg, pullwr)  : 取数据回调；err(arg, pullwr, eof) : 出错 / EOF 回调。
 *    - pullwr_write()     : 主动喂入一段数据。
 *    - pullwr_bump()      : 有新数据时把 idle 的 pullwr 唤醒回 active。
 *
 * 3. 与其他模块的关系：
 *    - frrevent.h : 基于 event_loop 的写事件。
 *    - stream.h   : 提供 stream 便捷喂数封装 pullwr_write_stream。
 *
 * 4. 推荐阅读顺序：
 *    先读本头文件顶部大段英文注释 -> _pullwr_new / pullwr_new 宏 ->
 *    pullwr_write / pullwr_bump -> pullwr_cfg -> pullwr_stats -> pullwr_del。
 *
 * 5. 易踩的坑：
 *    - fill() / err() 在事件循环线程被调用，禁止阻塞。
 *    - fill() 里不要自己循环发送多个“单位”，pullwr 会自行反复调用 fill()。
 *    - err() 被调用后该 pullwr 即视为不可用，须用 pullwr_del 释放；
 *      且可在回调内部直接删除(此时 pullwr 已不再持有引用)。
 *    - pullwr_new 是带类型检查的宏，要求 fill / err 首参与 arg 类型一致。
 *    - pullwr 句柄由调用者管理生命周期，库自身不释放调用者的 arg。
 */

#include <stdbool.h>
#include <stdint.h>

#include "frrevent.h"
#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 不透明前置声明：pullwr 的内部结构定义在 pullwr.c，外部只能持有指针。
 * 这种“抽象句柄”设计让实现可以自由演进而不影响调用者。
 */
struct pullwr;

/* This is a "pull-driven" write event handler.  Instead of having some buffer
 * or being driven by the availability of data, it triggers on the space being
 * available on the socket for data to be written on and then calls fill() to
 * get data to be sent.
 *
 * pullwr_* maintains an "idle" vs. "active" state, going into idle when a
 * fill() call completes without feeing more data into it.  The overall
 * semantics are:
 * - to put data out, call pullwr_write().  This is possible from both inside
 *   fill() callbacks or anywhere else.  Doing so puts the pullwr into
 *   active state.
 * - in active state, the fill() callback will be called and should feed more
 *   data in.  It should NOT loop to push out more than one "unit" of data;
 *   the pullwr code handles this by calling fill() until it has enough data.
 * - if there's nothing more to be sent, fill() returns without doing anything
 *   and pullwr goes into idle state after flushing all buffered data out.
 * - when new data becomes available, pullwr_bump() should be called to put
 *   the pullwr back into active mode so it will collect data from fill(),
 *   or you can directly call pullwr_write().
 * - only calling pullwr_write() from within fill() is the cleanest way of
 *   doing things.
 *
 * When the err() callback is called, the pullwr should be considered unusable
 * and released with pullwr_del().  This can be done from inside the callback,
 * the pullwr code holds no more references on it when calling err().
 */
/*
 * 创建 pullwr 的低层函数(不建议直接调用，请用 pullwr_new 宏)。
 *   tm   : 事件循环句柄。
 *   fd   : 要写入的 socket 文件描述符。
 *   arg  : 不透明用户数据，会作为首参回传给 fill / err。
 *   fill : 取数据回调，签名 fill(arg, pullwr)。
 *   err  : 错误 / EOF 回调，签名 err(arg, pullwr, eof)。
 * 返回新 pullwr 句柄。
 * 副作用：注册可写事件等。内存归调用者，用 pullwr_del 释放。
 * 注意：本函数不做参数类型检查，宏包装版会检查。
 */
extern struct pullwr *_pullwr_new(struct event_loop *tm, int fd, void *arg,
				  void (*fill)(void *, struct pullwr *),
				  void (*err)(void *, struct pullwr *,
					      bool eof));
/*
 * 释放 pullwr 句柄并注销其事件。
 *   pullwr : 目标句柄。
 * 副作用：取消事件、释放内部缓冲与句柄本身；不释放调用者的 arg。
 * 注意：可在 err() 回调内部安全调用(此时库已不再持有对它的引用)。
 */
extern void pullwr_del(struct pullwr *pullwr);

/* type-checking wrapper.  makes sure fill() and err() take a first argument
 * whose type is identical to the type of arg.
 * => use "void fill(struct mystruct *arg, ...)" - no "void *arg"
 */
/*
 * pullwr_new(tm, fd, arg, fill, err) —— _pullwr_new 的类型安全包装宏。
 * 它借助 typeof(arg) 把 fill / err 的首参类型强制为 arg 的实际类型，
 * 从而要求写成 fill(struct mystruct *arg, ...) 而非 void *，编译期即可
 * 捕获回调首参类型不匹配的错误。展开后仍调用 _pullwr_new。
 * 注意：使用 GNU 语句表达式({ ... })，依赖 GCC / Clang 扩展。
 */
#define pullwr_new(tm, fd, arg, fill, err) ({                                  \
	void (*fill_typechk)(typeof(arg), struct pullwr *) = fill;          \
	void (*err_typechk)(typeof(arg), struct pullwr *, bool) = err;      \
	_pullwr_new(tm, fd, arg, (void *)fill_typechk, (void *)err_typechk);   \
})

/* max_spin_usec is the time after which the pullwr event handler will stop
 *   trying to get more data from fill() and yield control back to the
 *   thread_master.  It does reschedule itself to continue later; this is
 *   only to make sure we don't freeze the entire process if we're piping a
 *   lot of data to a local endpoint that reads quickly (i.e. no backpressure)
 *
 *   default: 2500 (2.5 ms)
 *
 * write_threshold is the amount of data buffered from fill() calls at which
 *   the pullwr code starts calling write().  But this is not a "limit".
 *   pullwr will keep poking fill() for more data until
 *   (a) max_spin_usec is reached; fill() will be called again later after
 *       returning to the thread_master to give other events a chance to run
 *   (b) fill() returns without pushing any data onto the pullwr with
 *       pullwr_write(), so fill() will NOT be called again until a call to
 *       pullwr_bump() or pullwr_write() comes in.
 *
 *   default: 16384 (16 kB)
 *
 * passing 0 for either value (or not calling it at all) uses the default.
 */
/*
 * 配置 pullwr 的调度与阈值参数。
 *   pullwr          : 目标句柄。
 *   max_spin_usec   : 单次事件里持续从 fill() 取数据的最大时间(微秒)；
 *                     超过则让出控制权给事件循环、稍后继续，避免冻结进程。
 *   write_threshold : fill() 累积到该字节量后开始调用 write()(非硬上限)。
 * 传 0 或干脆不调用则使用默认值(max_spin_usec=2500us，write_threshold=16KB)。
 * 副作用：仅改内部配置，不立即写数据。
 */
extern void pullwr_cfg(struct pullwr *pullwr, int64_t max_spin_usec,
		       size_t write_threshold);

/*
 * 把处于 idle 的 pullwr 重新置为 active，促使它再次调用 fill() 收集数据。
 *   pullwr : 目标句柄。
 * 副作用：可能注册 / 触发写事件。
 * 使用时机：当外部数据变得可用(而 fill 之前已返回)时调用，或直接改用
 * pullwr_write()。
 */
extern void pullwr_bump(struct pullwr *pullwr);
/*
 * 向 pullwr 喂入一段要发送的数据。
 *   pullwr : 目标句柄。
 *   data   : 数据起始地址。
 *   len    : 数据长度(字节)。
 * 副作用：把数据放入内部发送缓冲并把 pullwr 置为 active；可在 fill() 回调
 * 内部或任何其他事件上下文调用。
 * 注意：实现会拷贝数据，调用返回后原 data 可由调用者处置。
 */
extern void pullwr_write(struct pullwr *pullwr,
		const void *data, size_t len);

/*
 * 便捷内联：把整个 stream 的内容(s->data 起，长度 stream_get_endp)喂给 pullwr。
 *   pullwr : 目标句柄。
 *   s      : 源 stream。
 * 无返回值；等价于 pullwr_write(pullwr, s->data, stream_get_endp(s))。
 * 注意：只发送 stream 的已用区间，不含未写部分。
 */
static inline void pullwr_write_stream(struct pullwr *pullwr,
		struct stream *s)
{
	pullwr_write(pullwr, s->data, stream_get_endp(s));
}

/*
 * 查询 pullwr 的统计信息。
 *   pullwr         : 目标句柄。
 *   total_written  : 出参，累计已写出字节数。
 *   pending        : 出参，仍缓冲待写的字节数。
 *   kernel_pending : 出参，已交给内核但尚未确认发送的字节数。
 * 无返回值；所有出参指针应有效(实现直接写入)。
 */
extern void pullwr_stats(struct pullwr *pullwr, uint64_t *total_written,
			 size_t *pending, size_t *kernel_pending);

#ifdef __cplusplus
}
#endif

#endif /* _WRITEPOLL_H */
