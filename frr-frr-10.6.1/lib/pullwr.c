// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pull-driven write event handler
 * Copyright (C) 2019  David Lamparter
 */

/*
 * 【文件导读】pullwr.c —— 拉取式(pull-driven)写事件处理器实现
 *
 * 1. 职责：
 *    实现 pullwr.h。用一个环形缓冲 + 可写事件，把“生产数据”与“写 socket”
 *    解耦：socket 可写时才调用 fill() 拉取数据，写不完则等下一次可写事件。
 *
 * 2. 核心数据结构：
 *    - struct pullwr : fd、事件循环 tm、可写事件 writer(NULL 表示 idle)、
 *      用户回调 fill / err、环形缓冲(buffer/bufsz/valid/pos)、统计与阈值。
 *    - 环形缓冲用 valid(有效字节)、pos(读写位置) 描述，写满可回绕。
 *
 * 3. 与其他模块的关系：
 *    - frrevent.h : event_add_write / event_add_timer / event_cancel。
 *    - monotime.h : monotime / monotime_since 做自旋时间限制。
 *    - memory.h   : XCALLOC / XMALLOC / XFREE 带 MTYPE 记账(PULLWR_HEAD/BUF)。
 *
 * 4. 推荐阅读顺序：
 *    _pullwr_new -> pullwr_write -> pullwr_iov -> pullwr_resize ->
 *    pullwr_run(核心) -> pullwr_bump / pullwr_cfg / pullwr_stats / pullwr_del。
 *
 * 5. 易踩的坑：
 *    - pullwr_run 中 fill() 与 err() 都在事件循环线程执行，禁止阻塞。
 *    - maxspin 限制单次事件里反复 fill() 的时间，防止快速本地端点饿死进程。
 *    - fill() 每次应只喂一个“单位”，若一次没喂数据(valid 未变)会转 idle。
 *    - err() 回调后本对象应立即 pullwr_del，且回调内删除是安全的。
 *    - writev 遇 EAGAIN/EWOULDBLOCK 直接跳出，等下一个可写事件，非错误。
 *    - writer 字段是 idle / active 的唯一判据：非 NULL 即已排事件(active)。
 */
#include "zebra.h"

#include <sys/ioctl.h>

#include "pullwr.h"
#include "memory.h"
#include "monotime.h"

/* defaults */
/*
 * 默认写阈值：缓冲内待写数据达到该字节数(16384，即 16KB)时开始尝试 write。
 * 不是硬上限，仅触发写出的参考点；可用 pullwr_cfg 覆盖。
 */
#define PULLWR_THRESH	16384	/* size at which we start to call write() */
/*
 * 默认最大自旋时间(微秒)：单次 pullwr_run 中持续调用 fill() 收集数据的上限
 * (2500us 即 2.5ms)。超过则让出，稍后再来，避免长时间霸占事件循环。
 * 可用 pullwr_cfg 覆盖。
 */
#define PULLWR_MAXSPIN	2500	/* max µs to spend grabbing more data */

/*
 * pullwr 内部结构(不透明句柄的真实定义)。
 *   fd            : 目标 socket 文件描述符。
 *   tm            : 事件循环句柄。
 *   writer        : 可写 / 定时事件；NULL 表示当前处于 idle(无在途事件)。
 *   arg           : 用户数据，回传给 fill / err。
 *   fill          : 取数据回调 fill(arg, pullwr)。
 *   err           : 错误 / EOF 回调 err(arg, pullwr, eof)。
 *   bufsz         : 环形缓冲总大小。
 *   valid         : 缓冲内有效待写字节数。
 *   pos           : 读 / 写位置指针(相对 buffer 偏移)。
 *   total_written : 累计写出字节数统计。
 *   buffer        : 环形缓冲实体。
 *   thresh        : 写阈值(默认 PULLWR_THRESH)。
 *   maxspin       : 自旋上限微秒(默认 PULLWR_MAXSPIN)。
 * 环形缓冲在扩容时会被“拉直”(重排为线性)，但保持大小时数据仍可能回绕。
 */
struct pullwr {
	int fd;
	struct event_loop *tm;
	/* writer == NULL <=> we're idle */
	struct event *writer;

	void *arg;
	void (*fill)(void *, struct pullwr *);
	void (*err)(void *, struct pullwr *, bool);

	/* ring buffer (although it's "un-ringed" on resizing, it WILL wrap
	 * around if data is trickling in while keeping it at a constant size)
	 */
	size_t bufsz, valid, pos;
	uint64_t total_written;
	char *buffer;

	size_t thresh;		/* PULLWR_THRESH */
	int64_t maxspin;	/* PULLWR_MAXSPIN */
};

/*
 * 两类内存记账类型：
 *   PULLWR_HEAD : pullwr 控制结构体。
 *   PULLWR_BUF  : 内部环形缓冲。
 * 文件内静态，仅供 memory 统计。
 */
DEFINE_MTYPE_STATIC(LIB, PULLWR_HEAD, "pull-driven write controller");
DEFINE_MTYPE_STATIC(LIB, PULLWR_BUF,  "pull-driven write buffer");

/*
 * pullwr_run(可写 / 定时事件回调) 的前向声明，供 pullwr_bump 先引用。
 */
static void pullwr_run(struct event *t);

/*
 * 创建 pullwr 句柄(低层实现，外部请用 pullwr_new 宏)。
 *   tm/fd/arg/fill/err : 见 pullwr.h。
 * 返回新句柄；XCALLOC 失败走 FRR 致命处理。
 * 副作用：分配控制结构并把 thresh / maxspin 置为默认值；
 * 注意此处不注册任何事件，writer 仍为 NULL(初始 idle)。
 * 内存归调用者，用 pullwr_del 释放。
 */
struct pullwr *_pullwr_new(struct event_loop *tm, int fd, void *arg,
			   void (*fill)(void *, struct pullwr *),
			   void (*err)(void *, struct pullwr *, bool))
{
	struct pullwr *pullwr;

	pullwr = XCALLOC(MTYPE_PULLWR_HEAD, sizeof(*pullwr));
	pullwr->fd = fd;
	pullwr->tm = tm;
	pullwr->arg = arg;
	pullwr->fill = fill;
	pullwr->err = err;

	pullwr->thresh = PULLWR_THRESH;
	pullwr->maxspin = PULLWR_MAXSPIN;

	return pullwr;
}

/*
 * 释放 pullwr：取消在途事件、释放缓冲与控制结构。
 *   pullwr : 目标句柄。
 * 副作用：event_cancel 清空 writer；XFREE buffer 与自身。
 * 注意：不释放 pullwr->arg(用户数据)；可在 err() 回调内安全调用。
 */
void pullwr_del(struct pullwr *pullwr)
{
	event_cancel(&pullwr->writer);

	XFREE(MTYPE_PULLWR_BUF, pullwr->buffer);
	XFREE(MTYPE_PULLWR_HEAD, pullwr);
}

/*
 * 覆盖 pullwr 的 maxspin / thresh。
 *   max_spin_usec   : 新的最大自旋微秒数；传 0 则回退默认 PULLWR_MAXSPIN。
 *   write_threshold : 新的写阈值字节数；传 0 则回退默认 PULLWR_THRESH。
 * 副作用：仅改配置字段。注意这里用的是 GNU 的 ?: 省略中间操作数写法。
 */
void pullwr_cfg(struct pullwr *pullwr, int64_t max_spin_usec,
		size_t write_threshold)
{
	pullwr->maxspin = max_spin_usec ?: PULLWR_MAXSPIN;
	pullwr->thresh = write_threshold ?: PULLWR_THRESH;
}

/*
 * 唤醒 pullwr：若当前 idle(writer 为 NULL)则注册一次立即执行的事件。
 *   pullwr : 目标句柄。
 * 副作用：event_add_timer(..., 0, ...) 把 pullwr_run 排队到事件循环。
 * 注意：若已 active(writer 非 NULL)则直接返回，故重复调用是幂等的。
 */
void pullwr_bump(struct pullwr *pullwr)
{
	if (pullwr->writer)
		return;

	event_add_timer(pullwr->tm, pullwr_run, pullwr, 0, &pullwr->writer);
}

/*
 * 把环形缓冲中的有效数据组织成 writev 需要的 iovec 数组。
 *   pullwr : 目标句柄。
 *   iov    : 出参，容量至少 2 个 iovec。
 * 返回 iovec 个数：0 表示无数据；1 表示数据连续；2 表示跨越缓冲回绕点、
 * 需两段拼接。
 * 无副作用；仅计算指针与长度。
 */
static size_t pullwr_iov(struct pullwr *pullwr, struct iovec *iov)
{
	size_t len1;

	if (pullwr->valid == 0)
		return 0;

	if (pullwr->pos + pullwr->valid <= pullwr->bufsz) {
		iov[0].iov_base = pullwr->buffer + pullwr->pos;
		iov[0].iov_len = pullwr->valid;
		return 1;
	}

	len1 = pullwr->bufsz - pullwr->pos;

	iov[0].iov_base = pullwr->buffer + pullwr->pos;
	iov[0].iov_len = len1;
	iov[1].iov_base = pullwr->buffer;
	iov[1].iov_len = pullwr->valid - len1;
	return 2;
}

/*
 * 调整环形缓冲容量。
 *   pullwr : 目标句柄。
 *   need   : 期望再容纳的字节数；>0 表示扩容路径，0 表示缩容路径。
 * 行为：
 *   - need>0 且剩余空间足够 -> 直接返回；否则扩容到 (valid+need)*2 与
 *     thresh*2 的较大者。
 *   - need==0 且缓冲为空 -> 释放缓冲(bufsz=0)。
 *   - need==0 且有数据 -> 剩余空间足够则返回；否则缩容到 valid 与 thresh*2
 *     的较大者(维持两倍阈值，避免频繁抖动)。
 * 副作用：分配新缓冲、经 pullwr_iov 搬迁旧数据、释放旧缓冲并复位 pos=0。
 * 注意：搬迁依赖 iov 顺序正确；缩容时不会丢失有效数据。
 */
static void pullwr_resize(struct pullwr *pullwr, size_t need)
{
	struct iovec iov[2];
	size_t niov, newsize;
	char *newbuf;

	/* the buffer is maintained at pullwr->thresh * 2 since we'll be
	 * trying to fill it as long as it's anywhere below pullwr->thresh.
	 * That means we frequently end up a little short of it and then write
	 * something that goes over the threshold.  So, just use double.
	 */
	if (need) {
		/* resize up */
		if (pullwr->bufsz - pullwr->valid >= need)
			return;

		newsize = MAX((pullwr->valid + need) * 2, pullwr->thresh * 2);
		newbuf = XMALLOC(MTYPE_PULLWR_BUF, newsize);
	} else if (!pullwr->valid) {
		/* resize down, buffer empty */
		newsize = 0;
		newbuf = NULL;
	} else {
		/* resize down */
		if (pullwr->bufsz - pullwr->valid < pullwr->thresh)
			return;
		newsize = MAX(pullwr->valid, pullwr->thresh * 2);
		newbuf = XMALLOC(MTYPE_PULLWR_BUF, newsize);
	}

	niov = pullwr_iov(pullwr, iov);
	if (niov >= 1) {
		memcpy(newbuf, iov[0].iov_base, iov[0].iov_len);
		if (niov >= 2)
			memcpy(newbuf + iov[0].iov_len,
				iov[1].iov_base, iov[1].iov_len);
	}

	XFREE(MTYPE_PULLWR_BUF, pullwr->buffer);
	pullwr->buffer = newbuf;
	pullwr->bufsz = newsize;
	pullwr->pos = 0;
}

/*
 * 向 pullwr 喂入数据(拷贝进内部环形缓冲)。
 *   pullwr : 目标句柄。
 *   data   : 源数据。
 *   len    : 长度。
 * 副作用：先按需 resize，再把数据写入缓冲(必要时分两段处理回绕)，
 * valid 累加 len，最后 pullwr_bump 触发写事件。
 * 注意：写完后原 data 仍归调用者，可自由释放；本函数在事件循环线程调用。
 */
void pullwr_write(struct pullwr *pullwr, const void *data, size_t len)
{
	pullwr_resize(pullwr, len);

	if (pullwr->pos + pullwr->valid > pullwr->bufsz) {
		size_t pos;

		pos = (pullwr->pos + pullwr->valid) % pullwr->bufsz;
		memcpy(pullwr->buffer + pos, data, len);
	} else {
		size_t max1, len1;
		max1 = pullwr->bufsz - (pullwr->pos + pullwr->valid);
		max1 = MIN(max1, len);

		memcpy(pullwr->buffer + pullwr->pos + pullwr->valid,
				data, max1);
		len1 = len - max1;

		if (len1)
			memcpy(pullwr->buffer, (char *)data + max1, len1);

	}
	pullwr->valid += len;

	pullwr_bump(pullwr);
}

/*
 * 核心回调：可写事件或定时事件触发时执行，负责收集数据并写出。
 *   t : 事件对象，EVENT_ARG 取回 pullwr。
 * 流程：
 *   1) 记录起始时间 t0；
 *   2) 内层 while 反复调用 fill() 直至达到 thresh、fill 未再产出数据
 *      (valid 不再变化)或超过 maxspin(置 maxspun)；
 *   3) 若 valid==0 说明本次 fill 没喂任何数据，转 idle：缩容并返回；
 *   4) 用 writev 写出缓冲数据；
 *      - EAGAIN/EWOULDBLOCK : 跳出，等下一次可写事件(非错误)；
 *      - 其它错误          : 调 err(..., false) 后返回；
 *      - 写出 0 字节       : 调 err(..., true)(EOF) 后返回；
 *      - 正常              : 更新 total_written / valid / pos(取模回绕)；
 *   5) do/while 在 valid==0 且未超时(maxspun)时继续重试；
 *   6) 若仍有数据(写不完整，socket 满)，注册可写事件等待；
 *   7) 未超时才缩容缓冲(超时则保留，因为马上还要再跑)。
 * 副作用：调用用户 fill / err，执行 writev，改缓冲与统计，注册事件。
 * 注意：全程事件循环线程；fill / err 禁止阻塞。
 */
static void pullwr_run(struct event *t)
{
	struct pullwr *pullwr = EVENT_ARG(t);
	struct iovec iov[2];
	size_t niov, lastvalid;
	ssize_t nwr;
	struct timeval t0;
	bool maxspun = false;

	monotime(&t0);

	do {
		lastvalid = pullwr->valid - 1;
		while (pullwr->valid < pullwr->thresh
				&& pullwr->valid != lastvalid
				&& !maxspun) {
			lastvalid = pullwr->valid;
			pullwr->fill(pullwr->arg, pullwr);

			/* check after doing at least one fill() call so we
			 * don't spin without making progress on slow boxes
			 */
			if (!maxspun && monotime_since(&t0, NULL)
					>= pullwr->maxspin)
				maxspun = true;
		}

		if (pullwr->valid == 0) {
			/* we made a fill() call above that didn't feed any
			 * data in, and we have nothing more queued, so we go
			 * into idle, i.e. no calling event_add_write()
			 */
			pullwr_resize(pullwr, 0);
			return;
		}

		niov = pullwr_iov(pullwr, iov);
		assert(niov);

		nwr = writev(pullwr->fd, iov, niov);
		if (nwr < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			pullwr->err(pullwr->arg, pullwr, false);
			return;
		}

		if (nwr == 0) {
			pullwr->err(pullwr->arg, pullwr, true);
			return;
		}

		pullwr->total_written += nwr;
		pullwr->valid -= nwr;
		pullwr->pos += nwr;
		pullwr->pos %= pullwr->bufsz;
	} while (pullwr->valid == 0 && !maxspun);
	/* pullwr->valid != 0 implies we did an incomplete write, i.e. socket
	 * is full and we go wait until it's available for writing again.
	 */

	event_add_write(pullwr->tm, pullwr_run, pullwr, pullwr->fd,
			&pullwr->writer);

	/* if we hit the time limit, just keep the buffer, we'll probably need
	 * it anyway & another run is already coming up.
	 */
	if (!maxspun)
		pullwr_resize(pullwr, 0);
}

/*
 * 读取统计：已写出字节、缓冲待写字节、内核待确认字节。
 *   pullwr         : 目标句柄。
 *   total_written  : 出参，累计写出量。
 *   pending        : 出参，缓冲内 valid(用户态待写)。
 *   kernel_pending : 出参，经 ioctl(fd, TIOCOUTQ) 查询内核发送队列长度；
 *                    查询失败则填 0。
 * 无返回值。注意 TIOCOUTQ 是 Linux 扩展，失败时不报错只回退为 0。
 */
void pullwr_stats(struct pullwr *pullwr, uint64_t *total_written,
		  size_t *pending, size_t *kernel_pending)
{
	int tmp;

	*total_written = pullwr->total_written;
	*pending = pullwr->valid;

	if (ioctl(pullwr->fd, TIOCOUTQ, &tmp) != 0)
		tmp = 0;
	*kernel_pending = tmp;
}
