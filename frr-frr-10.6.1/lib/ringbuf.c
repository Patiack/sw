// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Circular buffer implementation.
 * Copyright (C) 2017 Cumulus Networks
 * Quentin Young
 */
#include <zebra.h>

#include "ringbuf.h"
#include "memory.h"

/*
 * 文件导读（ringbuf.c）：环形缓冲区的实现。
 *
 * 本文件实现 ringbuf.h 声明的接口。核心是 start / end 两个下标在一段固定大小
 * 的 data 区上回绕，并用 empty 布尔量消除“start 等于 end 时无法区分空与满”的
 * 歧义：empty 为真表示空，empty 为假且 start 等于 end 表示满。
 *
 * 读写数据时都把环形区在物理上分成两段：从当前下标到 data 末尾为第一段，回绕
 * 到 data 开头后为第二段，分一到两次 memcpy 完成。所有空间计算都建立在
 * ringbuf_remain 之上，ringbuf_space 是它的补数。
 *
 * 需留意：本模块不做任何字节序转换，也不做线程同步；多线程使用需调用方自行
 * 加锁。这与 buffer 的定位不同（buffer 是带引用计数的共享字节块，多用于共享
 * 只读快照），ringbuf 则是可变的生产 / 消费队列。
 */
DEFINE_MTYPE_STATIC(LIB, RINGBUFFER, "Ring buffer");

/*
 * ringbuf_new：分配控制块与数据区并初始化。
 * 两次 XCALLOC：第一次给控制块，第二次给 size 字节数据区；随后记录 size 并把
 * empty 置真，start 与 end 保持 0。返回控制块指针。易踩的坑：XCALLOC 失败会
 * 直接终止进程，所以不返回空指针。
 */
struct ringbuf *ringbuf_new(size_t size)
{
	struct ringbuf *buf = XCALLOC(MTYPE_RINGBUFFER, sizeof(struct ringbuf));
	buf->data = XCALLOC(MTYPE_RINGBUFFER, size);
	buf->size = size;
	buf->empty = true;
	return buf;
}

/*
 * ringbuf_del：先释放数据区再释放控制块自身。
 * 参数 buf 为待销毁的控制块；不检查空指针，也不清零数据。无返回值。
 */
void ringbuf_del(struct ringbuf *buf)
{
	XFREE(MTYPE_RINGBUFFER, buf->data);
	XFREE(MTYPE_RINGBUFFER, buf);
}

/*
 * ringbuf_remain：计算可读字节数。
 * 以 end 减 start 为基准：若二者相等且非空，说明刚好写满一圈，补一个 size；
 * 若差为负，说明写位置已回绕到读位置之前，同样补一个 size。返回修正后的
 * 字节数；当 start 等于 end 且 empty 为真（空缓冲区）时返回 0。
 */
size_t ringbuf_remain(struct ringbuf *buf)
{
	ssize_t diff = buf->end - buf->start;
	diff += ((diff == 0) && !buf->empty) ? buf->size : 0;
	diff += (diff < 0) ? buf->size : 0;
	return (size_t)diff;
}

/*
 * ringbuf_space：可写字节数等于 size 减去可读字节数。
 * 直接调用 ringbuf_remain 再作减法，返回剩余空位字节数。
 */
size_t ringbuf_space(struct ringbuf *buf)
{
	return buf->size - ringbuf_remain(buf);
}

/*
 * ringbuf_put：写数据到尾端，必要时跨越物理末端回绕。
 * 先取可写空间并把请求长度截到空间大小（copysize 即最终返回值）；若从 end 到
 * data 末尾的连续空间不够，就先写末尾一段（ts 字节）、把 end 归零、源指针前移，
 * 再写剩下的一段。最后推进 end 并更新 empty：仅当本次什么都没写且原本就空时
 * 仍为空，否则按 start 是否追上 end 判定。返回实际写入的 copysize。
 */
size_t ringbuf_put(struct ringbuf *buf, const void *data, size_t size)
{
	const uint8_t *dp = data;
	size_t space = ringbuf_space(buf);
	size_t copysize = MIN(size, space);
	size_t tocopy = copysize;
	if (tocopy >= buf->size - buf->end) {
		size_t ts = buf->size - buf->end;
		memcpy(buf->data + buf->end, dp, ts);
		buf->end = 0;
		tocopy -= ts;
		dp += ts;
	}
	memcpy(buf->data + buf->end, dp, tocopy);
	buf->end += tocopy;
	buf->empty = (buf->start == buf->end) && (buf->empty && !copysize);
	return copysize;
}

/*
 * ringbuf_get：从头端读数据，必要时跨越物理末端回绕。
 * 先取可读字节数并把请求长度截到可读大小（copysize 即最终返回值）；若从 start
 * 到 data 末尾的连续空间不够，就先读末尾一段（ts 字节）、把 start 归零、目的
 * 指针前移，再读剩下的一段。最后推进 start 并更新 empty：当 start 追上 end
 * 且（原本就空 或 本次确实读出过数据）时置为空。返回实际读出的 copysize。
 */
size_t ringbuf_get(struct ringbuf *buf, void *data, size_t size)
{
	uint8_t *dp = data;
	size_t remain = ringbuf_remain(buf);
	size_t copysize = MIN(remain, size);
	size_t tocopy = copysize;
	if (tocopy >= buf->size - buf->start) {
		size_t ts = buf->size - buf->start;
		memcpy(dp, buf->data + buf->start, ts);
		buf->start = 0;
		tocopy -= ts;
		dp += ts;
	}
	memcpy(dp, buf->data + buf->start, tocopy);
	buf->start = buf->start + tocopy;
	buf->empty = (buf->start == buf->end) && (buf->empty || copysize);
	return copysize;
}

/*
 * ringbuf_peek：只读不推进的窥视。
 * 若 offset 已不小于可读字节数则直接返回 0；否则以可读量减去 offset 后再与
 * size 取小得到 copysize。读取起点为 start 加 offset 后对 size 取模，同样在
 * 跨越末端时分两段 memcpy。不修改 start / end / empty。
 */
size_t ringbuf_peek(struct ringbuf *buf, size_t offset, void *data, size_t size)
{
	uint8_t *dp = data;
	size_t remain = ringbuf_remain(buf);
	if (offset >= remain)
		return 0;
	size_t copysize = MAX(MIN(remain - offset, size), (size_t)0);
	size_t tocopy = copysize;
	size_t cstart = (buf->start + offset) % buf->size;
	if (tocopy >= buf->size - cstart) {
		size_t ts = buf->size - cstart;
		memcpy(dp, buf->data + cstart, ts);
		cstart = 0;
		tocopy -= ts;
		dp += ts;
	}
	memcpy(dp, buf->data + cstart, tocopy);
	return copysize;
}

/*
 * ringbuf_copy：借助一块临时缓冲把 from 的数据复制到 to。
 * 先按 to 的可写空间与 size 取小得到待复制量，用 MTYPE_TMP 分配临时缓冲；
 * 通过 ringbuf_peek 从 from 读取（因此不消费 from 的数据），再用 ringbuf_put
 * 写入 to；最后释放临时缓冲，以 put 的返回值为实际复制量返回。
 */
size_t ringbuf_copy(struct ringbuf *to, struct ringbuf *from, size_t size)
{
	size_t tocopy = MIN(ringbuf_space(to), size);
	uint8_t *cbuf = XCALLOC(MTYPE_TMP, tocopy);
	tocopy = ringbuf_peek(from, 0, cbuf, tocopy);
	size_t put = ringbuf_put(to, cbuf, tocopy);
	XFREE(MTYPE_TMP, cbuf);
	return put;
}

/*
 * ringbuf_reset：把 start 与 end 归零并把 empty 置真。
 * 不清零 data，只重置游标语义，等价于丢弃当前全部未读内容。
 */
void ringbuf_reset(struct ringbuf *buf)
{
	buf->start = buf->end = 0;
	buf->empty = true;
}

/*
 * ringbuf_wipe：先用 memset 把 data 全部置零，再调用 ringbuf_reset 重置位置。
 */
void ringbuf_wipe(struct ringbuf *buf)
{
	memset(buf->data, 0x00, buf->size);
	ringbuf_reset(buf);
}
