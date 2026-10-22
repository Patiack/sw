// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Buffering to output and input.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * 文件导读（buffer.h）：面向 socket / 终端 输出的分块缓冲接口。
 *
 * 一、定位与边界：
 * - 本模块实现的是一个可增长的输出缓冲：数据先攒进若干个固定大小的数据块
 *   （buffer_data），再择机用 writev 一批批写到文件描述符。
 * - 它不是引用计数对象，也不是线程安全的共享快照；每个 buffer 只服务于一个
 *   输出目标（通常是一个 vty 会话或一条 socket）。
 * - 与 stream 的边界：stream 是报文读写游标（带越界检查与 endp），buffer 只
 *   关心“还没发出去的数据”的暂存与冲刷，不做任何字段解析与字节序转换。
 *
 * 二、关键概念：
 * - 每个 buffer_data 有 sp 与 cp 两个下标：sp 是已冲刷位置，cp 是已写入位置，
 *   恒有 0 小于等于 sp 小于等于 cp 小于等于块大小。
 * - buffer_status_t 描述一次冲刷的结果：BUFFER_ERROR 表示 I/O 出错、缓冲区应
 *   被销毁；BUFFER_EMPTY 表示已全部写出；BUFFER_PENDING 表示尚有残留，应在
 *   描述符可写时继续冲刷。
 *
 * 三、调用要点：写入用 buffer_put 系列；冲刷用 buffer_flush_available（非阻塞
 * 场景，只写一大块）或 buffer_flush_all（反复写到空为止）；buffer_flush_window
 * 仅供 vty 终端分屏使用。本文件只放声明，实现见 buffer.c。
 */
#ifndef _ZEBRA_BUFFER_H
#define _ZEBRA_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create a new buffer.  Memory will be allocated in chunks of the given
   size.  If the argument is 0, the library will supply a reasonable
   default size suitable for buffering socket I/O. */
/*
 * buffer_new：新建一个 buffer，按 size 指定每个数据块的字节数。
 * 参数 size 为块大小；为 0 时使用一个按页大小向上取整的默认值（约 4KB）。
 * 返回新建的 buffer 指针。易踩的坑：此刻尚未分配任何数据块，首个数据块在首次
 * 写入时才创建。
 */
extern struct buffer *buffer_new(size_t size);

/* Free all data in the buffer. */
/*
 * buffer_reset：清空缓冲区，释放链表上所有数据块。
 * 参数 b 为目标 buffer；把 head 与 tail 复位为空。无返回值。
 * 易踩的坑：已冲刷位置 sp 之前的空间不会被复用，本函数是把整条链都丢掉重建。
 */
extern void buffer_reset(struct buffer *b);

/* This function first calls buffer_reset to release all buffered data.
   Then it frees the struct buffer itself. */
/*
 * buffer_free：先释放全部数据块，再释放 buffer 结构体本身。
 * 参数 b 为待销毁的 buffer。无返回值，调用后不得再访问 b。
 */
extern void buffer_free(struct buffer *b);

/* Add the given data to the end of the buffer. */
/*
 * buffer_put：把 size 字节数据追加到缓冲区尾部。
 * 参数 b 为目标 buffer，p 为源数据，size 为长度。无返回值。
 * 内部按块填充：当前块写满即追加新块，把数据切成若干块写入。
 */
extern void buffer_put(struct buffer *b, const void *p, size_t size);
/* Add a single character to the end of the buffer. */
/*
 * buffer_putc：向缓冲区追加单个字节。
 * 参数 c 为字符；等价于以 1 字节长度调用 buffer_put。无返回值。
 */
extern void buffer_putc(struct buffer *b, uint8_t c);
/* Add a NUL-terminated string to the end of the buffer. */
/*
 * buffer_putstr：向缓冲区追加一个以空字符结尾的字符串。
 * 参数 str 为字符串；内部用 strlen 求长后调用 buffer_put，不写入结尾的空字符。
 * 无返回值。
 */
extern void buffer_putstr(struct buffer *b, const char *str);
/* Add given data, inline-expanding \n to \r\n */
/*
 * buffer_put_crlf：追加数据，并把其中的换行符就地展开为回车加换行。
 * 参数 b、p、size 同 buffer_put。无返回值。用于终端输出需要 CRLF 换行的场景。
 */
extern void buffer_put_crlf(struct buffer *b, const void *p, size_t size);

/* Combine all accumulated (and unflushed) data inside the buffer into a
   single NUL-terminated string allocated using XMALLOC(MTYPE_TMP).  Note
   that this function does not alter the state of the buffer, so the data
   is still inside waiting to be flushed. */
/*
 * buffer_getstr：把缓冲区中所有尚未冲刷的数据拼接成一个以空字符结尾的字符串。
 * 参数 b 为目标 buffer。返回用 MTYPE_TMP 分配的字符串，失败返回空指针。
 * 易踩的坑：不改变缓冲区状态，数据仍留在原地等待冲刷；返回的字符串需调用方
 * 自行释放。
 */
char *buffer_getstr(struct buffer *b);

/* Returns 1 if there is no pending data in the buffer.  Otherwise returns 0. */
/*
 * buffer_empty：判断缓冲区中是否还有待冲刷数据。
 * 参数 b 为目标 buffer。返回 1 表示没有待发数据，否则返回 0。
 */
int buffer_empty(struct buffer *b);

/*
 * buffer_status_t：一次冲刷操作的结果码。
 * BUFFER_ERROR 为 -1，表示发生 I/O 错误，缓冲区应销毁、描述符应关闭；
 * BUFFER_EMPTY 为 0，表示全部写出、缓冲区已空；
 * BUFFER_PENDING 为 1，表示仍有数据残留，应在描述符可写时再次冲刷。
 */
typedef enum {
	/* An I/O error occurred.  The buffer should be destroyed and the
	   file descriptor should be closed. */
	BUFFER_ERROR = -1,

	/* The data was written successfully, and the buffer is now empty
	   (there is no pending data waiting to be flushed). */
	BUFFER_EMPTY = 0,

	/* There is pending data in the buffer waiting to be flushed.  Please
	   try flushing the buffer when select indicates that the file
	   descriptor
	   is writeable. */
	BUFFER_PENDING = 1
} buffer_status_t;

/* Try to write this data to the file descriptor.  Any data that cannot
   be written immediately is added to the buffer queue. */
/*
 * buffer_write：尝试把数据直接写到描述符，写不完的部分转入缓冲区排队。
 * 参数 b 为目标 buffer，fd 为描述符，p 为数据，size 为长度。
 * 返回冲刷状态：若缓冲区原本非空则本次不直接写，只把数据入队并返回 PENDING；
 * 否则先 write，未写完的余量用 buffer_put 入队。
 */
extern buffer_status_t buffer_write(struct buffer *b, int fd, const void *p,
				    size_t size);

/* This function attempts to flush some (but perhaps not all) of
   the queued data to the given file descriptor. */
/*
 * buffer_flush_available：非阻塞场景下尝试写出一大块排队数据。
 * 参数 b 为源 buffer，fd 为目标描述符。
 * 返回 BUFFER_EMPTY 表示已清空、BUFFER_PENDING 表示需要稍后再冲刷、
 * BUFFER_ERROR 表示致命写错误。易踩的坑：它不保证写空，只写一个较大的块，
 * 因此调用方需在描述符可写时反复调用。
 */
extern buffer_status_t buffer_flush_available(struct buffer *b, int fd);

/* The following 2 functions (buffer_flush_all and buffer_flush_window)
   are for use in lib/vty.c only.  They should not be used elsewhere. */

/* Call buffer_flush_available repeatedly until either all data has been
   flushed, or an I/O error has been encountered, or the operation would
   block. */
/*
 * buffer_flush_all：反复调用 buffer_flush_available 直到写空或出错或会阻塞。
 * 参数 b 为源 buffer，fd 为目标描述符。返回最后一次冲刷的状态码。
 * 用于需要一次把排队数据尽量写尽的阻塞式调用路径。
 */
extern buffer_status_t buffer_flush_all(struct buffer *b, int fd);

/* Attempt to write enough data to the given fd to fill a window of the
   given width and height (and remove the data written from the buffer).

   If !no_more, then a message saying " --More-- " is appended.
   If erase is true, then first overwrite the previous " --More-- " message
   with spaces.

   Any write error (including EAGAIN or EINTR) will cause this function
   to return -1 (because the logic for handling the erase and more features
   is too complicated to retry the write later).
*/
/*
 * buffer_flush_window：按终端窗口宽度与高度冲刷数据，供 vty 分屏使用。
 * 参数 b 为源 buffer，fd 为描述符，width 与 height 为窗口尺寸，erase 表示是否
 * 先用退格与空格覆盖上一次的 --More-- 提示，no_more 表示是否禁止追加 --More--
 * 提示。返回冲刷状态。易踩的坑：任何写错误（含 EAGAIN、EINTR）都直接返回 -1，
 * 因为分屏与擦除逻辑过于复杂不便稍后重试。仅供 lib/vty.c 使用。
 */
extern buffer_status_t buffer_flush_window(struct buffer *b, int fd, int width,
					   int height, int erase, int no_more);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_BUFFER_H */
