// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Circular buffer implementation.
 * Copyright (C) 2017 Cumulus Networks
 * Quentin Young
 */
#ifndef _FRR_RINGBUF_H_
#define _FRR_RINGBUF_H_

/*
 * 文件导读（ringbuf.h / ringbuf.c）：FRR 的环形缓冲区接口声明。
 *
 * 一、与 stream / buffer 的边界：
 * - stream 是报文游标：读写位置分离，带越界检查与 endp，用于顺序解析一个报文。
 * - buffer 是带引用计数的共享字节块，用于多处共享同一段不可变字节。
 * - ringbuf 是固定大小的头尾相接字节环，读写各占一个游标，用于生产 / 消费
 *   速率不一致或需要暂存的场景，例如向 socket 写出时的排队。
 * 三者都不做字节序转换；主机序与网络序的换算由调用方通过 stream_put 函数族
 * 显式完成。
 *
 * 二、结构体字段：
 * - size：环形区字节数，创建后不再改变。
 * - start：读位置在 data 中的下标。
 * - end：写位置在 data 中的下标。
 * - empty：区分 start 与 end 相等时是空还是满的布尔量。
 * - data：指向 size 字节的数据区。
 *
 * 三、关键语义：
 * - 因为 start 等于 end 既可能表示空也可能表示满，所以额外用 empty 标记：
 *   empty 为真表示空，empty 为假且 start 等于 end 表示满。
 * - 所有 put / get / peek 都允许部分完成，返回值可能小于请求长度，调用方必须
 *   自行处理剩余数据。
 * - put 写入尾端，get 从头部读出并推进读位置，peek 只读不推进。
 *
 * 四、阅读顺序：先看结构体与 ringbuf_remain / ringbuf_space 理解“满 / 空”的
 * 判定口径，再看 circle 回绕的 put / get / peek 分两段 memcpy 的写法，最后看
 * reset 与 wipe 的差别（是否清零底层数据）。
 */

#include <zebra.h>
#include <stdint.h>

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * struct ringbuf：环形缓冲区控制块。
 * size 是环形区字节数；start 是读位置、end 是写位置，均以取模方式在 data 内
 * 回绕；empty 用于区分 start 等于 end 时究竟是空还是满；data 指向 size 字节
 * 的数据区。注意 start / end 是相对 data 的下标，不是绝对偏移。
 */
struct ringbuf {
	size_t size;
	ssize_t start;
	ssize_t end;
	bool empty;
	uint8_t *data;
};

/*
 * Creates a new ring buffer.
 *
 * @param size	buffer size, in bytes
 * @return the newly created buffer
 */
/*
 * ringbuf_new：创建一个容量为 size 字节的新环形缓冲区。
 * 参数 size 为环形区字节数；内部两次 XCALLOC 分别分配控制块与数据区。
 * 返回新建的控制块指针。易踩的坑：XCALLOC 分配失败时直接终止进程，不返回
 * 空指针，所以调用方无需判空。初始 empty 为真、start 与 end 均为 0。
 */
struct ringbuf *ringbuf_new(size_t size);

/*
 * Deletes a ring buffer and frees all associated resources.
 *
 * @param buf	the ring buffer to destroy
 */
/*
 * ringbuf_del：销毁环形缓冲区并释放全部内存。
 * 参数 buf 为待销毁的控制块；内部先释放 data 再释放控制块自身。无返回值。
 * 易踩的坑：释放后不得再访问 buf，且本函数不对仍在其中的数据做清零。
 */
void ringbuf_del(struct ringbuf *buf);

/*
 * Get amount of data left to read from the buffer.
 *
 * @return number of readable bytes
 */
/*
 * ringbuf_remain：返回当前可读字节数，即已写入但尚未读出的量。
 * 参数 buf 为目标控制块。实现用 end 减 start 估算，再按两种情形修正：二者
 * 相等且非空时补一个 size（正好写满一圈），差为负时也补一个 size（写位置已
 * 回绕到读位置之前）。返回可读字节数。
 */
size_t ringbuf_remain(struct ringbuf *buf);

/*
 * Get amount of space left to write to the buffer
 *
 * @return number of writeable bytes
 */
/*
 * ringbuf_space：返回当前可写字节数，等于 size 减去 ringbuf_remain。
 * 参数 buf 为目标控制块。返回剩余空位字节数。与 ringbuf_remain 互为补数，
 * 二者之和恒为 size。
 */
size_t ringbuf_space(struct ringbuf *buf);


/*
 * Put data into the ring buffer.
 *
 * @param data	the data to put in the buffer
 * @param size	how much of data to put in
 * @return number of bytes written; will be less than size if there was not
 * enough space
 */
/*
 * ringbuf_put：把数据写入环形缓冲区尾端。
 * 参数 data 为源数据，size 为期望写入长度。返回实际写入字节数，可能小于 size
 * （空间不足时按剩余空间截断）。跨越物理末端时分两段 memcpy 完成，并推进 end、
 * 更新 empty。易踩的坑：返回值必须检查，未写完的部分需调用方另行处理。
 */
size_t ringbuf_put(struct ringbuf *buf, const void *data, size_t size);

/*
 * Get data from the ring buffer.
 *
 * @param data	where to put the data
 * @param size	how much of data to get
 * @return number of bytes read into data; will be less than size if there was
 * not enough data to read
 */
/*
 * ringbuf_get：从环形缓冲区头部读出数据，并推进读位置。
 * 参数 data 为目的缓冲，size 为期望读出长度。返回实际读出字节数，可能小于
 * size（数据不足时按剩余数据截断）。跨越物理末端时分两段 memcpy 完成，并推进
 * start、更新 empty。易踩的坑：返回值必须检查，未读到的数据仍留在缓冲区。
 */
size_t ringbuf_get(struct ringbuf *buf, void *data, size_t size);

/*
 * Peek data from the ring buffer.
 *
 * @param offset	where to get the data from, in bytes offset from the
 *			start of the data
 * @param data		where to put the data
 * @param size		how much data to get
 * @return		number of bytes read into data; will be less than size
 *			if there was not enough data to read; will be -1 if the
 *			offset exceeds the amount of data left in the ring
 *			buffer
 */
/*
 * ringbuf_peek：从环形缓冲区中窥视数据，但不移动读位置。
 * 参数 offset 为相对读起点的字节偏移，data 为目的缓冲，size 为期望读出长度。
 * 返回实际读出字节数；当 offset 已达到或超过可读字节数时返回 0。
 * 与 ringbuf_get 的区别是不推进 start，可用于先看一眼再决定是否消费。
 */
size_t ringbuf_peek(struct ringbuf *buf, size_t offset, void *data,
		    size_t size);

/*
 * Copy data from one ringbuf to another.
 *
 * @param to	destination ringbuf
 * @param from	source ringbuf
 * @param size	how much data to copy
 * @return amount of data copied
 */
/*
 * ringbuf_copy：把 from 中的数据复制到 to，最多 size 字节。
 * 参数 to 为目的缓冲区，from 为源缓冲区，size 为复制上限。
 * 返回实际复制进 to 的字节数（受 to 的剩余空间与 from 的可读数据共同限制）。
 * 实现借助一块 MTYPE_TMP 临时缓冲先 peek 再 put，因此不消费 from 的数据。
 */
size_t ringbuf_copy(struct ringbuf *to, struct ringbuf *from, size_t size);

/*
 * Reset buffer. Does not wipe.
 *
 * @param buf
 */
/*
 * ringbuf_reset：重置读 / 写位置，但不清零底层数据。
 * 参数 buf 为目标控制块；把 start 与 end 归零并把 empty 置真。
 * 易踩的坑：底层旧数据仍留在内存里，若涉及敏感内容应改用 ringbuf_wipe。
 */
void ringbuf_reset(struct ringbuf *buf);

/*
 * Reset buffer. Wipes.
 *
 * @param buf
 */
/*
 * ringbuf_wipe：先清零整个数据区，再重置读 / 写位置。
 * 参数 buf 为目标控制块；用于需要抹掉旧数据的场合。内部会把 data 的 size 个
 * 字节全部置零，随后等价于一次 ringbuf_reset。
 */
void ringbuf_wipe(struct ringbuf *buf);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_RINGBUF_H_ */
