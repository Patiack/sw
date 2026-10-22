// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Buffering of output and input.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

#include <zebra.h>

#include "memory.h"
#include "buffer.h"
#include "log.h"
#include "network.h"
#include "lib_errors.h"

#include <stddef.h>

/*
 * 文件导读（buffer.c）：面向 socket / 终端 输出的分块缓冲实现。
 *
 * 本文件实现 buffer.h 的接口。数据被切成若干个固定大小的 buffer_data 块，用
 * 单链表（head / tail）串起来；每块用 sp（已冲刷位置）与 cp（已写入位置）两个
 * 下标刻画进度，恒有 0 小于等于 sp 小于等于 cp 小于等于块大小。
 *
 * 写入路径：buffer_put 系列把数据追加到 tail 块，块满就 buffer_add 新块。
 * 冲刷路径：buffer_flush_available 把从 head 起、sp 到 cp 之间的数据用 writev
 * 批量写出，写空的块逐块释放；buffer_flush_all 反复调用它直到写空或阻塞；
 * buffer_flush_window 面向 vty 分屏，按窗口宽高控制一次写出的量并追加 --More--。
 * buffer_write 则先尝试直写描述符，写不完的余量再入队。
 *
 * 与 stream / ringbuf 的边界：本模块只负责“还没发出去的数据”的暂存与冲刷，
 * 不解析字段、不做字节序转换，也不是引用计数的共享对象。多线程使用需外部加锁。
 */
DEFINE_MTYPE_STATIC(LIB, BUFFER, "Buffer");
DEFINE_MTYPE_STATIC(LIB, BUFFER_DATA, "Buffer data");

/* Buffer master. */
/*
 * struct buffer：缓冲区主控结构。
 * head / tail 是 buffer_data 链表的头尾；size 是每个数据块的字节数（创建时
 * 确定后不再改变）。buffer 本身不存放数据，数据都在各个 buffer_data 块里。
 */
struct buffer {
	/* Data list. */
	struct buffer_data *head;
	struct buffer_data *tail;

	/* Size of each buffer_data chunk. */
	size_t size;
};

/* Data container. */
/*
 * struct buffer_data：一个固定大小的数据块。
 * next 串起链表；cp 是已写入位置（下一次追加的偏移）；sp 是已冲刷位置（下一次
 * 写出的偏移）；data 是长度可变数组，实际容量为 buffer->size。尚未写出的有效
 * 数据位于区间 [sp, cp)。恒有 0 小于等于 sp 小于等于 cp 小于等于块大小。
 */
struct buffer_data {
	struct buffer_data *next;

	/* Location to add new data. */
	size_t cp;

	/* Pointer to data not yet flushed. */
	size_t sp;

	/* Actual data stream (variable length). */
	unsigned char data[]; /* real dimension is buffer->size */
};

/*
 * 下面这条不变式是整个模块正确性的基础：对任意 buffer_data 块，都有
 * sp 大于等于 0、cp 大于等于 sp、且 cp 小于等于块大小。写入只推进 cp，冲刷
 * 只推进 sp，二者永不错位；任何代码违反它都意味着缓冲区状态已被破坏。
 */
/* It should always be true that: 0 <= sp <= cp <= size */

/* Default buffer size (used if none specified).  It is rounded up to the
   next page boundary. */
/*
 * BUFFER_SIZE_DEFAULT：未指定块大小时使用的默认块尺寸；buffer_new 会把它按
 * 系统页大小向上取整，避免数据块跨页带来的额外开销。
 */
#define BUFFER_SIZE_DEFAULT		4096

/*
 * BUFFER_DATA_FREE：用一个数据块指针释放其占用的 MTYPE_BUFFER_DATA 内存的
 * 便捷宏，避免各调用点重复书写内存类型名。
 */
#define BUFFER_DATA_FREE(D) XFREE(MTYPE_BUFFER_DATA, (D))

/* Make new buffer. */
/*
 * buffer_new：新建缓冲区并确定每块大小。
 * 参数 size 非零时直接作为块大小；为零时按系统页大小把默认值向上取整，且默认
 * 值用静态变量缓存，只计算一次。返回新建的 buffer。易踩的坑：此时不分配任何
 * 数据块，首个块在首次写入时才由 buffer_add 创建。
 */
struct buffer *buffer_new(size_t size)
{
	struct buffer *b;

	b = XCALLOC(MTYPE_BUFFER, sizeof(struct buffer));

	if (size)
		b->size = size;
	else {
		static size_t default_size;
		if (!default_size) {
			long pgsz = sysconf(_SC_PAGESIZE);
			default_size = ((((BUFFER_SIZE_DEFAULT - 1) / pgsz) + 1)
					* pgsz);
		}
		b->size = default_size;
	}

	return b;
}

/* Free buffer. */
/*
 * buffer_free：先 buffer_reset 释放全部数据块，再释放 buffer 自身。
 * 参数 b 为待销毁的 buffer；不检查空指针。无返回值。
 */
void buffer_free(struct buffer *b)
{
	buffer_reset(b);
	XFREE(MTYPE_BUFFER, b);
}

/* Make string clone. */
/*
 * buffer_getstr：把链表中所有未冲刷数据拼成一个以空字符结尾的字符串。
 * 先遍历统计总长（各块 cp 减 sp 之和），用 MTYPE_TMP 分配 totlen 加 1 字节，
 * 再逐块把区间 [sp, cp) 拷贝过去并在末尾补空字符。返回该字符串；分配失败返回
 * 空指针。易踩的坑：不改变缓冲区状态，返回串需调用方自行释放。
 */
char *buffer_getstr(struct buffer *b)
{
	size_t totlen = 0;
	struct buffer_data *data;
	char *s;
	char *p;

	for (data = b->head; data; data = data->next)
		totlen += data->cp - data->sp;
	if (!(s = XMALLOC(MTYPE_TMP, totlen + 1)))
		return NULL;
	p = s;
	for (data = b->head; data; data = data->next) {
		memcpy(p, data->data + data->sp, data->cp - data->sp);
		p += data->cp - data->sp;
	}
	*p = '\0';
	return s;
}

/* Clear and free all allocated data. */
/*
 * buffer_reset：遍历链表释放所有数据块，并把 head 与 tail 置空。
 * 参数 b 为目标 buffer；每块用 BUFFER_DATA_FREE 释放。无返回值。
 */
void buffer_reset(struct buffer *b)
{
	struct buffer_data *data;
	struct buffer_data *next;

	for (data = b->head; data; data = next) {
		next = data->next;
		BUFFER_DATA_FREE(data);
	}
	b->head = b->tail = NULL;
}

/* Add buffer_data to the end of buffer. */
/*
 * buffer_add：在链表尾部追加一个新的空数据块，块容量为 buffer->size。
 * 参数 b 为目标 buffer；按“结构体固定部分加 data 柔性数组”的总大小分配，把
 * cp 与 sp 归零。若链表非空则接到 tail 之后，否则同时作为 head。返回新块指针。
 */
static struct buffer_data *buffer_add(struct buffer *b)
{
	struct buffer_data *d;

	d = XMALLOC(MTYPE_BUFFER_DATA,
		    offsetof(struct buffer_data, data) + b->size);
	d->cp = d->sp = 0;
	d->next = NULL;

	if (b->tail)
		b->tail->next = d;
	else
		b->head = d;
	b->tail = d;

	return d;
}

/* Write data to buffer. */
/*
 * buffer_put：把数据追加到缓冲区尾部，必要时新建块。
 * 参数 b 为目标 buffer，p 为源数据，size 为长度。循环处理：若当前块不存在或
 * 已满（cp 达到 size）则调用 buffer_add 取新块；每次尽量填满当前块剩余空间后
 * 推进 cp，直到数据写完。无返回值。
 */
void buffer_put(struct buffer *b, const void *p, size_t size)
{
	struct buffer_data *data = b->tail;
	const char *ptr = p;

	/* We use even last one byte of data buffer. */
	while (size) {
		size_t chunk;

		/* If there is no data buffer add it. */
		if (data == NULL || data->cp == b->size)
			data = buffer_add(b);

		chunk = ((size <= (b->size - data->cp)) ? size
							: (b->size - data->cp));
		memcpy((data->data + data->cp), ptr, chunk);
		size -= chunk;
		ptr += chunk;
		data->cp += chunk;
	}
}

/* Insert character into the buffer. */
/*
 * buffer_putc：追加单个字节，等价于以长度 1 调用 buffer_put。
 */
void buffer_putc(struct buffer *b, uint8_t c)
{
	buffer_put(b, &c, 1);
}

/* Put string to the buffer. */
/*
 * buffer_putstr：追加一个以空字符结尾的字符串。
 * 内部按 strlen 求长后调用 buffer_put，因此不会写入结尾的空字符。
 */
void buffer_putstr(struct buffer *b, const char *c)
{
	buffer_put(b, c, strlen(c));
}

/* Expand \n to \r\n */
/*
 * buffer_put_crlf：追加数据，并把每个换行符展开成回车加换行。
 * 参数 b、origp、origsize 同 buffer_put。先用 memchr 找到首个换行，循环把换行
 * 之前的内容写入，若刚好写完一个换行则在其前补写回车、并把源指针越过该换行继续
 * 找下一个。用于终端需要 CRLF 换行的场合。
 */
void buffer_put_crlf(struct buffer *b, const void *origp, size_t origsize)
{
	struct buffer_data *data = b->tail;
	const char *p = origp, *end = p + origsize, *lf;
	size_t size;

	lf = memchr(p, '\n', end - p);

	/* We use even last one byte of data buffer. */
	while (p < end) {
		size_t avail, chunk;

		/* If there is no data buffer add it. */
		if (data == NULL || data->cp == b->size)
			data = buffer_add(b);

		size = (lf ? lf : end) - p;
		avail = b->size - data->cp;

		chunk = (size <= avail) ? size : avail;
		memcpy(data->data + data->cp, p, chunk);

		p += chunk;
		data->cp += chunk;

		if (lf && size <= avail) {
			/* we just copied up to (including) a '\n' */
			if (data->cp == b->size)
				data = buffer_add(b);
			data->data[data->cp++] = '\r';
			if (data->cp == b->size)
				data = buffer_add(b);
			data->data[data->cp++] = '\n';

			p++;
			lf = memchr(p, '\n', end - p);
		}
	}
}

/* Keep flushing data to the fd until the buffer is empty or an error is
   encountered or the operation would block. */
/*
 * buffer_flush_all：持续冲刷直到写空、出错或会阻塞。
 * 参数 b 为源 buffer，fd 为目标描述符。先记住初始 head 与它的 sp，循环调用
 * buffer_flush_available：若一轮下来 head 与 sp 都没变且 errno 不是 EINTR，
 * 说明内核发送缓冲已满，直接返回 PENDING；否则继续。返回最后一次的状态码。
 */
buffer_status_t buffer_flush_all(struct buffer *b, int fd)
{
	buffer_status_t ret;
	struct buffer_data *head;
	size_t head_sp;

	if (!b->head)
		return BUFFER_EMPTY;
	head_sp = (head = b->head)->sp;
	/* Flush all data. */
	while ((ret = buffer_flush_available(b, fd)) == BUFFER_PENDING) {
		if ((b->head == head) && (head_sp == head->sp)
		    && (errno != EINTR))
			/* No data was flushed, so kernel buffer must be full.
			 */
			return ret;
		head_sp = (head = b->head)->sp;
	}

	return ret;
}

/* Flush enough data to fill a terminal window of the given scene (used only
   by vty telnet interface). */
/*
 * buffer_flush_window：按终端窗口尺寸冲刷数据，供 vty 分屏显示。
 * 参数 b 为源 buffer，fd 为描述符，width 与 height 为窗口宽高，erase_flag 表示
 * 是否先覆盖上次的 --More-- 提示，no_more_flag 表示是否禁止追加 --More--。
 * 实现要点：用 iovec 数组组织本次要写出的各块，按字符逐列统计行列以决定写到
 * 哪为止（遇换行或到列宽就减一行）；块数少时用栈上 small_iov，否则用 MTYPE_TMP
 * 分配；最后用 writev 批量写出并逐块释放写空的数据块。返回 BUFFER_ERROR /
 * BUFFER_EMPTY / BUFFER_PENDING。仅供 lib/vty.c 使用。
 */
buffer_status_t buffer_flush_window(struct buffer *b, int fd, int width,
				    int height, int erase_flag,
				    int no_more_flag)
{
	int nbytes;
	int iov_alloc;
	int iov_index;
	struct iovec *iov;
	struct iovec small_iov[3];
	char more[] = " --More-- ";
	char erase[] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
			0x08, 0x08, ' ',  ' ',  ' ',  ' ',  ' ',  ' ',
			' ',  ' ',  ' ',  ' ',  0x08, 0x08, 0x08, 0x08,
			0x08, 0x08, 0x08, 0x08, 0x08, 0x08};
	struct buffer_data *data;
	int column;

	if (!b->head)
		return BUFFER_EMPTY;

	if (height < 1)
		height = 1;
	else if (height >= 2)
		height--;
	if (width < 1)
		width = 1;

	/* For erase and more data add two to b's buffer_data count.*/
	if (b->head->next == NULL) {
		iov_alloc = array_size(small_iov);
		iov = small_iov;
	} else {
		iov_alloc = ((height * (width + 2)) / b->size) + 10;
		iov = XMALLOC(MTYPE_TMP, iov_alloc * sizeof(*iov));
	}
	iov_index = 0;

	/* Previously print out is performed. */
	if (erase_flag) {
		iov[iov_index].iov_base = erase;
		iov[iov_index].iov_len = sizeof(erase);
		iov_index++;
	}

	/* Output data. */
	column = 1; /* Column position of next character displayed. */
	for (data = b->head; data && (height > 0); data = data->next) {
		size_t cp;

		cp = data->sp;
		while ((cp < data->cp) && (height > 0)) {
			/* Calculate lines remaining and column position after
			   displaying
			   this character. */
			if (data->data[cp] == '\r')
				column = 1;
			else if ((data->data[cp] == '\n')
				 || (column == width)) {
				column = 1;
				height--;
			} else
				column++;
			cp++;
		}
		iov[iov_index].iov_base = (char *)(data->data + data->sp);
		iov[iov_index++].iov_len = cp - data->sp;
		data->sp = cp;

		if (iov_index == iov_alloc)
		/* This should not ordinarily happen. */
		{
			iov_alloc *= 2;
			if (iov != small_iov) {
				iov = XREALLOC(MTYPE_TMP, iov,
					       iov_alloc * sizeof(*iov));
			} else {
				/* This should absolutely never occur. */
				flog_err_sys(
					EC_LIB_SYSTEM_CALL,
					"%s: corruption detected: iov_small overflowed; head %p, tail %p, head->next %p",
					__func__, (void *)b->head,
					(void *)b->tail, (void *)b->head->next);
				iov = XMALLOC(MTYPE_TMP,
					      iov_alloc * sizeof(*iov));
				memcpy(iov, small_iov, sizeof(small_iov));
			}
		}
	}

	/* In case of `more' display need. */
	if (b->tail && (b->tail->sp < b->tail->cp) && !no_more_flag) {
		iov[iov_index].iov_base = more;
		iov[iov_index].iov_len = sizeof(more);
		iov_index++;
	}


#ifdef IOV_MAX
	/* IOV_MAX are normally defined in <sys/uio.h> , Posix.1g.
	   example: Solaris2.6 are defined IOV_MAX size at 16.     */
	{
		struct iovec *c_iov = iov;
		nbytes = 0; /* Make sure it's initialized. */

		while (iov_index > 0) {
			int iov_size;

			iov_size =
				((iov_index > IOV_MAX) ? IOV_MAX : iov_index);
			nbytes = writev(fd, c_iov, iov_size);
			if (nbytes < 0) {
				flog_err(EC_LIB_SOCKET,
					 "%s: writev to fd %d failed: %s",
					 __func__, fd, safe_strerror(errno));
				break;
			}

			/* move pointer io-vector */
			c_iov += iov_size;
			iov_index -= iov_size;
		}
	}
#else  /* IOV_MAX */
	nbytes = writev(fd, iov, iov_index);
	if (nbytes < 0)
		flog_err(EC_LIB_SOCKET, "%s: writev to fd %d failed: %s",
			 __func__, fd, safe_strerror(errno));
#endif /* IOV_MAX */

	/* Free printed buffer data. */
	while (b->head && (b->head->sp == b->head->cp)) {
		struct buffer_data *del;
		if (!(b->head = (del = b->head)->next))
			b->tail = NULL;
		BUFFER_DATA_FREE(del);
	}

	if (iov != small_iov)
		XFREE(MTYPE_TMP, iov);

	return (nbytes < 0) ? BUFFER_ERROR
			    : (b->head ? BUFFER_PENDING : BUFFER_EMPTY);
}

/* This function (unlike other buffer_flush* functions above) is designed
to work with non-blocking sockets.  It does not attempt to write out
all of the queued data, just a "big" chunk.  It returns 0 if it was
able to empty out the buffers completely, 1 if more flushing is
required later, or -1 on a fatal write error. */
/*
 * buffer_flush_available：非阻塞场景下尝试写出一大块排队数据，不保证写空。
 * 参数 b 为源 buffer，fd 为目标描述符。先用 iovec 收集至多 MAX_CHUNKS 个块、
 * 总量不超过 MAX_FLUSH；无可写数据时返回 BUFFER_EMPTY；writev 出错时若为可重试
 * 错误返回 PENDING，否则记日志返回 BUFFER_ERROR；写出后按 written 逐块推进 sp
 * 或释放整块。返回 b 是否仍非空对应的 PENDING 或 EMPTY。
 */
buffer_status_t buffer_flush_available(struct buffer *b, int fd)
{

/* These are just reasonable values to make sure a significant amount of
data is written.  There's no need to go crazy and try to write it all
in one shot. */
#ifdef IOV_MAX
#define MAX_CHUNKS ((IOV_MAX >= 16) ? 16 : IOV_MAX)
#else
#define MAX_CHUNKS 16
#endif
#define MAX_FLUSH 131072

	struct buffer_data *d;
	size_t written;
	struct iovec iov[MAX_CHUNKS];
	size_t iovcnt = 0;
	size_t nbyte = 0;

	if (fd < 0)
		return BUFFER_ERROR;

	for (d = b->head; d && (iovcnt < MAX_CHUNKS) && (nbyte < MAX_FLUSH);
	     d = d->next, iovcnt++) {
		iov[iovcnt].iov_base = d->data + d->sp;
		nbyte += (iov[iovcnt].iov_len = d->cp - d->sp);
	}

	if (!nbyte)
		/* No data to flush: should we issue a warning message? */
		return BUFFER_EMPTY;

	/* only place where written should be sign compared */
	if ((ssize_t)(written = writev(fd, iov, iovcnt)) < 0) {
		if (ERRNO_IO_RETRY(errno))
			/* Calling code should try again later. */
			return BUFFER_PENDING;
		flog_err(EC_LIB_SOCKET, "%s: write error on fd %d: %s",
			 __func__, fd, safe_strerror(errno));
		return BUFFER_ERROR;
	}

	/* Free printed buffer data. */
	while (written > 0) {
		if (!(d = b->head)) {
			flog_err(
				EC_LIB_DEVELOPMENT,
				"%s: corruption detected: buffer queue empty, but written is %lu",
				__func__, (unsigned long)written);
			break;
		}
		if (written < d->cp - d->sp) {
			d->sp += written;
			return BUFFER_PENDING;
		}

		written -= (d->cp - d->sp);
		if (!(b->head = d->next))
			b->tail = NULL;
		BUFFER_DATA_FREE(d);
	}

	return b->head ? BUFFER_PENDING : BUFFER_EMPTY;

#undef MAX_CHUNKS
#undef MAX_FLUSH
}

/*
 * buffer_write：先尝试直写描述符，没写尽的余量转入缓冲区排队。
 * 参数 b 为目标 buffer，fd 为描述符，p 为数据，size 为长度。
 * 若缓冲区已有排队数据则本次不直写（nbytes 记 0），以免打乱写出顺序；否则调用
 * write，出错且可重试时按未写处理，不可重试则记日志返回 BUFFER_ERROR。随后把
 * 未写出的部分用 buffer_put 入队，返回缓冲区是否非空对应的状态码。
 */
buffer_status_t buffer_write(struct buffer *b, int fd, const void *p,
			     size_t size)
{
	ssize_t nbytes;

	if (b->head)
		/* Buffer is not empty, so do not attempt to write the new data.
		 */
		nbytes = 0;
	else {
		nbytes = write(fd, p, size);
		if (nbytes < 0) {
			if (ERRNO_IO_RETRY(errno))
				nbytes = 0;
			else {
				flog_err(EC_LIB_SOCKET,
					 "%s: write error on fd %d: %s",
					 __func__, fd, safe_strerror(errno));
				return BUFFER_ERROR;
			}
		}
	}
	/* Add any remaining data to the buffer. */
	{
		size_t written = nbytes;
		if (written < size)
			buffer_put(b, ((const char *)p) + written,
				   size - written);
	}
	return b->head ? BUFFER_PENDING : BUFFER_EMPTY;
}
