// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Packet interface
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

#ifndef _ZEBRA_STREAM_H
#define _ZEBRA_STREAM_H

/*
 * 文件导读（stream.h）：FRR 的报文游标式读写抽象接口。
 *
 * 一、定位：一个 stream 就是一段被当作网络序报文使用的字节缓冲，同时维护三个
 * 游标：size（分配的总容量）、getp（下一个读取位置）、endp（有效数据结束位置）。
 * 三者恒满足 getp 小于等于 endp 小于等于 size（已有英文横幅对布局有详述）。
 *
 * 二、读写分离：put 系列从 endp 往后写并推进 endp，get 系列从 getp 往后读并推进
 * getp；读与写共用同一段 data，靠两个游标区分，因此一个 stream 既能“填入报文”
 * 也能“从中解析报文”。带 _at 后缀的函数（如 stream_putl_at）允许在指定绝对
 * 偏移处原地改写，常用于先占位、算出长度或校验和后再回填。
 *
 * 三、字节序在哪一层：网络序转换就发生在这一层的 put / get 函数里——put 系列先把
 * 主机序转成网络序再写，get 系列读出来再转回主机序；纯字节的 stream_put /
 * stream_get 与 ptr_get_beXX 内联函数只做搬运。所以调用方拿到的是主机序，通常
 * 不需要自己再转换。
 *
 * 四、边界与复用语义：本层只做游标管理与越界检查，不做引用计数、不做 I/O 缓冲
 * （那是 buffer 的职责）。越界时旧式 stream_getX 会断言终止，而 stream_getX2 与
 * STREAM_GET 系列宏返回布尔失败或跳到 stream_failure 标签，便于出错时安全释放
 * 资源。stream_reset 只把 getp 与 endp 归零，并不清零底层数据，复用前需明确这点。
 *
 * 五、其它：stream_fifo 是带互斥锁与原子计数的线程安全流队列，_safe 后缀的操作
 * 会自动加锁；ptr_get_be32 等内联函数是解析裸缓冲区时用的无状态字节序工具。
 */

#include <pthread.h>

#include "frratomic.h"
#include "mpls.h"
#include "prefix.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A stream is an arbitrary buffer, whose contents generally are assumed to
 * be in network order.
 *
 * A stream has the following attributes associated with it:
 *
 * - size: the allocated, invariant size of the buffer.
 *
 * - getp: the get position marker, denoting the offset in the stream where
 *         the next read (or 'get') will be from. This getp marker is
 *         automatically adjusted when data is read from the stream, the
 *         user may also manipulate this offset as they wish, within limits
 *         (see below)
 *
 * - endp: the end position marker, denoting the offset in the stream where
 *         valid data ends, and if the user attempted to write (or
 *         'put') data where that data would be written (or 'put') to.
 *
 * These attributes are all size_t values.
 *
 * Constraints:
 *
 * 1. getp can never exceed endp
 *
 * - hence if getp is equal to endp, there is no more valid data that can be
 *   gotten from the stream (though, the user may reposition getp to earlier in
 *   the stream, if they wish).
 *
 * 2. endp can never exceed size
 *
 * - hence, if endp is equal to size, then the stream is full, and no more
 *   data can be written to the stream.
 *
 * In other words the following must always be true, and the stream
 * abstraction is allowed internally to assert that the following property
 * holds true for a stream, as and when it wishes:
 *
 *        getp <= endp <= size
 *
 * It is the users responsibility to ensure this property is never violated.
 *
 * A stream therefore can be thought of like this:
 *
 * 	---------------------------------------------------
 * 	|XXXXXXXXXXXXXXXXXXXXXXXX                         |
 * 	---------------------------------------------------
 *               ^               ^                        ^
 *               getp            endp                     size
 *
 * This shows a stream containing data (shown as 'X') up to the endp offset.
 * The stream is empty from endp to size. Without adjusting getp, there are
 * still endp-getp bytes of valid data to be read from the stream.
 *
 * Methods are provided to get and put to/from the stream, as well as
 * retrieve the values of the 3 markers and manipulate the getp marker.
 *
 * Note:
 * At the moment, newly allocated streams are zero filled. Hence, one can
 * use stream_forward_endp() to effectively create arbitrary zero-fill
 * padding. However, note that stream_reset() does *not* zero-out the
 * stream. This property should **not** be relied upon.
 *
 * Best practice is to use stream_put (<stream *>, NULL, <size>) to zero out
 * any part of a stream which isn't otherwise written to.
 */

/* Stream buffer. */
/*
 * struct stream：流控制块。
 * next 供将其链入 stream_fifo 时使用；getp 是下一个读取位置，endp 是有效数据的
 * 结束位置（同时也是下一个写入位置），size 是 data 的容量，allow_expansion 表示
 * 空间不足时是否允许自动扩容，data 指向 size 字节的缓冲区。除 next 外，其余字段
 * 对调用方是私有实现细节，应通过对应函数 / 宏访问。
 */
struct stream {
	struct stream *next;

	/*
	 * Remainder is ***private*** to stream
	 * direct access is frowned upon!
	 * Use the appropriate functions/macros
	 */
	size_t getp;	       /* next get position */
	size_t endp;	       /* last valid data position */
	size_t size;	       /* size of data segment */
	bool allow_expansion;  /* whether stream can be expanded */
	unsigned char *data;   /* data pointer */
};

/* First in first out queue structure. */
/*
 * struct stream_fifo：线程安全的先进先出流队列。
 * mtx 是保护队列操作的互斥锁；count 与 max_count 是原子计数器（当前条数与历史
 * 峰值）；head 与 tail 通过各个 stream 的 next 字段把流串成链表。带 _safe 后缀的
 * 操作会在访问前加锁，非 _safe 版本需要调用方自行保证同步。
 */
struct stream_fifo {
	/* lock for mt-safe operations */
	pthread_mutex_t mtx;

	/* number of streams in this fifo */
	atomic_size_t count;
	atomic_size_t max_count;

	struct stream *head;
	struct stream *tail;
};

/*
 * 一组访问 stream 内部游标的便捷宏：
 * STREAM_SIZE 取缓冲区总容量；
 * STREAM_WRITEABLE 取还可写入的字节数（size 减 endp）；
 * STREAM_READABLE 取还可读取的字节数（endp 减 getp）；
 * STREAM_CONCAT_REMAIN 供 stream_dupcat 使用，估算把两段流拼接后还需要多少空间；
 * STREAM_DATA 取裸数据指针，已标注过时但暂不移除。
 */
/* Utility macros. */
#define STREAM_SIZE(S)  ((S)->size)
/* number of bytes which can still be written */
#define STREAM_WRITEABLE(S) ((S)->size - (S)->endp)
/* number of bytes still to be read */
#define STREAM_READABLE(S) ((S)->endp - (S)->getp)

#define STREAM_CONCAT_REMAIN(S1, S2, size) ((size) - (S1)->endp - (S2)->endp)

/* this macro is deprecated, but not slated for removal anytime soon */
#define STREAM_DATA(S)  ((S)->data)

/* Stream prototypes.
 * For stream_{put,get}S, the S suffix mean:
 *
 * c: character (unsigned byte)
 * w: word (two bytes)
 * l: long (two words)
 * q: quad (four words)
 */
/*
 * stream_new：按给定容量新建一个固定大小的 stream（allow_expansion 为假，
 * 写满即报错）。参数为 data 区的字节数。返回新建的 stream；分配失败会终止进程。
 */
extern struct stream *stream_new(size_t);
/*
 * stream_new_expandable：新建一个可自动扩容的 stream（allow_expansion 为真）。
 * 参数为初始容量。返回新建的 stream。易踩的坑：扩容会重新分配并搬移 data，因此
 * 在这类 stream 上长期持有裸指针（如 stream_pnt 的结果）是不安全的。
 */
extern struct stream *stream_new_expandable(size_t);
/*
 * stream_free：释放 stream 的 data 区与结构体本身。
 * 参数 s 为待销毁的 stream。无返回值，调用后不得再访问 s。
 */
extern void stream_free(struct stream *s);
/* Copy 'src' into 'dest', returns 'dest' */
/*
 * stream_copy：把 src 的容量、游标与数据整体复制到已存在的 dest。
 * 参数 dest 可选，为空时内部新建一个同规模 stream。返回 dest（或新建者）。
 * 易踩的坑：dest 容量不足时会按需扩容或报错，调用方应保证其可用。
 */
extern struct stream *stream_copy(struct stream *dest,
				  const struct stream *src);
/*
 * stream_dup：复制一个 stream，返回全新的副本（等价于 stream_copy 传空 dest）。
 * 参数 s 为源 stream。返回新建的副本，失败会终止进程。
 */
extern struct stream *stream_dup(const struct stream *s);

/*
 * stream_resize_inplace：原地调整 stream 的容量到 newsize。
 * 参数 sptr 是 stream 指针的地址（因为扩容可能替换指针，需要回写）；newsize 为
 * 新容量。返回实际生效的新容量（可能被夹取到不小于当前 endp）。
 * 易踩的坑：传入的是二级指针，扩容后旧的裸 data 指针会失效。
 */
extern size_t stream_resize_inplace(struct stream **sptr, size_t newsize);

/*
 * stream_get_getp：返回当前读取游标 getp 的值（已读取的字节数）。
 * 参数 s 为源 stream。返回值即下一个读取位置。
 */
extern size_t stream_get_getp(const struct stream *s);
/*
 * stream_get_endp：返回有效数据结束位置 endp（通常就是整个报文的长度）。
 * 参数 s 为源 stream。返回 endp 的值。
 */
extern size_t stream_get_endp(const struct stream *s);
/*
 * stream_get_size：返回 stream 分配的总容量 size。
 * 参数 s 为源 stream。返回 data 区字节数。
 */
extern size_t stream_get_size(const struct stream *s);

/**
 * Create a new stream structure; copy offset bytes from s1 to the new
 * stream; copy s2 data to the new stream; copy rest of s1 data to the
 * new stream.
 */
/*
 * stream_dupcat：把 s1 的前 offset 字节、s2 的全部数据、s1 剩余部分依次拼接成
 * 一个新的 stream 并返回。参数 offset 是 s1 的分割点。
 * 返回拼接后的新 stream；新容量按三段之和预留。
 */
extern struct stream *stream_dupcat(const struct stream *s1,
				    const struct stream *s2, size_t offset);

/*
 * stream_set_getp：把读取游标直接设置到绝对位置 pos。
 * 参数 pos 必须在 [0, endp] 之内，否则断言失败（旧式断言约定见横幅）。
 */
extern void stream_set_getp(struct stream *s, size_t pos);
/*
 * stream_set_endp：把有效数据结束位置设置到绝对位置 pos（即人为声明报文长度）。
 * 参数 pos 必须不大于 size 且不小于 getp，否则断言失败。常用于读出长度字段后
 * 据此截断 / 声明报文范围。
 */
extern void stream_set_endp(struct stream *s, size_t pos);
/*
 * stream_forward_getp：把读取游标向前推进 size 字节（跳过一段数据）。
 * 参数 size 为推进量；越界会断言失败。无返回值。
 */
extern void stream_forward_getp(struct stream *s, size_t size);
/*
 * stream_forward_getp2：stream_forward_getp 的安全版本。
 * 越界时返回 false 且不改变游标，否则推进并返回 true。供 STREAM_FORWARD_GETP
 * 宏在失败时跳到 stream_failure 使用。
 */
extern bool stream_forward_getp2(struct stream *s, size_t size);
/*
 * stream_rewind_getp：把读取游标向后退 size 字节（回看已读数据）。
 * 参数 size 为回退量；回退到 0 之前会断言失败。无返回值。
 */
extern void stream_rewind_getp(struct stream *s, size_t size);
/*
 * stream_rewind_getp2：stream_rewind_getp 的安全版本。
 * 越界（回退超过已读量）时返回 false 且不改变游标，否则回退并返回 true。
 */
extern bool stream_rewind_getp2(struct stream *s, size_t size);
/*
 * stream_forward_endp：把写位置 endp 向前推进 size 字节，制造一段保留区。
 * 参数 size 为推进量；越过 size 会断言失败。结合“新建 stream 全零填充”的特性，
 * 可用它预留一段零填充空间。无返回值。
 */
extern void stream_forward_endp(struct stream *s, size_t size);
/*
 * stream_forward_endp2：stream_forward_endp 的安全版本。
 * 越界时返回 false 且不改变 endp，否则推进并返回 true。供 STREAM_FORWARD_ENDP
 * 宏在失败时跳到 stream_failure 使用。
 */
extern bool stream_forward_endp2(struct stream *s, size_t size);

/* steam_put: NULL source zeroes out size_t bytes of stream */
/*
 * stream_put：从 endp 处写入 size 字节原始数据并推进 endp，不做字节序转换。
 * 参数 src 为源数据，为 NULL 时写入 size 个零字节（用于清零）。无返回值。
 * 易踩的坑：空间不足会断言；返回值缺省，需自行确认容量。
 */
extern void stream_put(struct stream *s, const void *src, size_t size);
/*
 * stream_putc：在 endp 处写入一个字节 c 并推进 endp。
 * 返回写入的字节数（1）。不做字节序转换。
 */
extern int stream_putc(struct stream *s, uint8_t c);
/*
 * stream_putc_at：在绝对偏移 putp 处原地改写一个字节，不移动 endp。
 * 返回写入的字节数（1）。用于回填先前预留的位置。
 */
extern int stream_putc_at(struct stream *s, size_t putp, uint8_t c);
/*
 * stream_putw：写入一个 16 位字（2 字节），先转成网络序再写并推进 endp。
 * 返回写入的字节数（2）。
 */
extern int stream_putw(struct stream *s, uint16_t w);
/*
 * stream_putw_at：在绝对偏移 putp 处原地改写一个网络序 16 位字。
 * 返回写入的字节数（2），不移动 endp。用于回填。
 */
extern int stream_putw_at(struct stream *s, size_t putp, uint16_t w);
/*
 * stream_put3：写入一个 24 位（3 字节）大端整数并推进 endp。
 * 返回写入的字节数（3）。用于 BGP 等以 3 字节表示长度的协议字段。
 */
extern int stream_put3(struct stream *s, uint32_t l);
/*
 * stream_put3_at：在绝对偏移 putp 处原地改写一个 24 位大端整数。
 * 返回写入的字节数（3），不移动 endp。
 */
extern int stream_put3_at(struct stream *s, size_t putp, uint32_t l);
/*
 * stream_putl：写入一个 32 位长字（4 字节），先转网络序再写并推进 endp。
 * 返回写入的字节数（4）。
 */
extern int stream_putl(struct stream *s, uint32_t l);
/*
 * stream_putl_at：在绝对偏移 putp 处原地改写一个网络序 32 位长字。
 * 返回写入的字节数（4），不移动 endp。用于回填长度或校验和。
 */
extern int stream_putl_at(struct stream *s, size_t putp, uint32_t l);
/*
 * stream_putq：写入一个 64 位四字（8 字节），先转网络序再写并推进 endp。
 * 返回写入的字节数（8）。
 */
extern int stream_putq(struct stream *s, uint64_t q);
/*
 * stream_putq_at：在绝对偏移 putp 处原地改写一个网络序 64 位四字。
 * 返回写入的字节数（8），不移动 endp。
 */
extern int stream_putq_at(struct stream *s, size_t putp, uint64_t q);
/*
 * stream_put_ipv4：写入一个 32 位 IPv4 地址（以主机序整数给出）并推进 endp。
 * 返回写入的字节数（4）。
 */
extern int stream_put_ipv4(struct stream *s, uint32_t l);
/*
 * stream_put_in_addr：写入一个 in_addr 形式的 IPv4 地址并推进 endp。
 * 参数 addr 为地址结构；返回写入的字节数（4）。
 */
extern int stream_put_in_addr(struct stream *s, const struct in_addr *addr);
/*
 * stream_put_ipaddr：写入一个 IPv4 或 IPv6 地址（按 ipaddr 中的族自动选择）。
 * 参数 ip 为地址结构；成功返回 true。IPv4 写 4 字节，IPv6 写 16 字节，均推进
 * endp。空间不足或族非法时返回 false。
 */
extern bool stream_put_ipaddr(struct stream *s, const struct ipaddr *ip);
/*
 * stream_put_in_addr_at：在绝对偏移 putp 处原地改写一个 in_addr 形式的 IPv4 地址。
 * 返回写入的字节数（4），不移动 endp。
 */
extern int stream_put_in_addr_at(struct stream *s, size_t putp,
				 const struct in_addr *addr);
/*
 * stream_put_in6_addr_at：在绝对偏移 putp 处原地改写一个 16 字节 IPv6 地址。
 * 返回写入的字节数（16），不移动 endp。
 */
extern int stream_put_in6_addr_at(struct stream *s, size_t putp, const struct in6_addr *addr);
/*
 * stream_put_prefix：写入一个前缀，先写 1 字节前缀长度，再写按长度向上取整到
 * 字节的前缀地址字节并推进 endp。参数 p 为前缀结构。返回写入的总字节数。
 */
extern int stream_put_prefix(struct stream *s, const struct prefix *p);
/*
 * stream_get：从 getp 处读取 size 字节原始数据到 dst 并推进 getp，不做字节序转换。
 * 参数 dst 为目的缓冲，s 为源 stream，size 为长度。无返回值；剩余不足会断言。
 */
extern void stream_get(void *dst, struct stream *s, size_t size);
/*
 * stream_get2：stream_get 的安全版本。可读数据不足时返回 false 且不推进 getp，
 * 否则读取并返回 true。供 STREAM_GET 宏在失败时跳到 stream_failure 使用。
 */
extern bool stream_get2(void *data, struct stream *s, size_t size);
/*
 * stream_get_from：从绝对偏移 from 处读取 size 字节到 dst，不改变 getp。
 * 用于回看或随机访问缓冲区中已存在的字节。
 */
extern void stream_get_from(void *dst, struct stream *s, size_t from, size_t size);
/*
 * stream_getc：从 getp 处读取一个字节并推进 getp。返回该字节；不足会断言。
 */
extern uint8_t stream_getc(struct stream *s);
/*
 * stream_getc2：stream_getc 的安全版本。不足时返回 false，否则把字节写入 *byte
 * 并返回 true。供 STREAM_GETC 宏使用。
 */
extern bool stream_getc2(struct stream *s, uint8_t *byte);
/*
 * stream_getc_from：读取绝对偏移 from 处的一个字节，不改变 getp。
 */
extern uint8_t stream_getc_from(struct stream *s, size_t from);
/*
 * stream_getw：读取一个网络序 16 位字并转成主机序，推进 getp。返回该值。
 */
extern uint16_t stream_getw(struct stream *s);
/*
 * stream_getw2：stream_getw 的安全版本。不足时返回 false，否则把值写入 *word
 * 并返回 true。供 STREAM_GETW 宏使用。
 */
extern bool stream_getw2(struct stream *s, uint16_t *word);
/*
 * stream_getw_from：读取绝对偏移 from 处的网络序 16 位字并转主机序，不改变 getp。
 */
extern uint16_t stream_getw_from(struct stream *s, size_t from);
/*
 * stream_get3：读取一个 24 位（3 字节）大端整数，推进 getp。返回主机序值。
 */
extern uint32_t stream_get3(struct stream *s);
/*
 * stream_get3_from：读取绝对偏移 from 处的 24 位大端整数，不改变 getp。
 */
extern uint32_t stream_get3_from(struct stream *s, size_t from);
/*
 * stream_getl：读取一个网络序 32 位长字并转成主机序，推进 getp。返回该值。
 */
extern uint32_t stream_getl(struct stream *s);
/*
 * stream_getl2：stream_getl 的安全版本。不足时返回 false，否则把值写入 *l 并
 * 返回 true。供 STREAM_GETL 宏使用。
 */
extern bool stream_getl2(struct stream *s, uint32_t *l);
/*
 * stream_getl_from：读取绝对偏移 from 处的网络序 32 位长字并转主机序，不改 getp。
 */
extern uint32_t stream_getl_from(struct stream *s, size_t from);
/*
 * stream_getq：读取一个网络序 64 位四字并转成主机序，推进 getp。返回该值。
 */
extern uint64_t stream_getq(struct stream *s);
/*
 * stream_getq_from：读取绝对偏移 from 处的网络序 64 位四字并转主机序，不改 getp。
 */
extern uint64_t stream_getq_from(struct stream *s, size_t from);
/*
 * stream_getq2：stream_getq 的安全版本。不足时返回 false，否则把值写入 *q 并
 * 返回 true。供 STREAM_GETQ 宏使用。
 */
bool stream_getq2(struct stream *s, uint64_t *q);
/*
 * stream_get_ipv4：读取一个 32 位 IPv4 地址（主机序整数），推进 getp。返回该值。
 */
extern uint32_t stream_get_ipv4(struct stream *s);
/*
 * stream_get_ipaddr：读取一个 IPv4 或 IPv6 地址到 ip（按 ip 中预设的族决定读
 * 4 还是 16 字节），推进 getp。成功返回 true，可读不足返回 false。
 * 供 STREAM_GET_IPADDR 宏使用。
 */
extern bool stream_get_ipaddr(struct stream *s, struct ipaddr *ip);

/* IEEE-754 floats */
/*
 * stream_getf：读取一个 32 位 IEEE-754 单精度浮点（按网络序字节拷入），推进
 * getp。返回该浮点值。
 */
extern float stream_getf(struct stream *s);
/*
 * stream_getd：读取一个 64 位 IEEE-754 双精度浮点，推进 getp。返回该值。
 */
extern double stream_getd(struct stream *s);
/*
 * stream_putf：写入一个 32 位 IEEE-754 单精度浮点，推进 endp。返回字节数（4）。
 */
extern int stream_putf(struct stream *s, float f);
/*
 * stream_putd：写入一个 64 位 IEEE-754 双精度浮点，推进 endp。返回字节数（8）。
 */
extern int stream_putd(struct stream *s, double d);

#undef stream_read
#undef stream_write

/* Deprecated: assumes blocking I/O.  Will be removed.
   Use stream_read_try instead.  */
/*
 * stream_read：从描述符阻塞式读取 size 字节追加到 stream 尾部。
 * 参数 fd 为描述符，size 为期望字节数。返回实际读入的字节数；EOF 返回 0，出错
 * 返回 -1。已过时，假定阻塞 I/O，建议改用 stream_read_try。
 */
extern int stream_read(struct stream *s, int fd, size_t size);

/* Read up to size bytes into the stream.
   Return code:
     >0: number of bytes read
     0: end-of-file
     -1: fatal error
     -2: transient error, should retry later (i.e. EAGAIN or EINTR)
   This is suitable for use with non-blocking file descriptors.
 */
/*
 * stream_read_try：面向非阻塞描述符的读取，最多读 size 字节追加到尾部。
 * 返回大于 0 为读到的字节数，0 为 EOF，-1 为致命错误，-2 为可重试的临时错误
 * （如 EAGAIN 或 EINTR）。调用方需根据 -2 稍后重试。
 */
extern ssize_t stream_read_try(struct stream *s, int fd, size_t size);

/*
 * stream_recvmsg：以 recvmsg 方式接收数据并追加到 stream，支持附属控制信息。
 * 参数 fd、msgh、flags 描述接收方式，size 为上限。返回接收字节数或负的错误码。
 */
extern ssize_t stream_recvmsg(struct stream *s, int fd, struct msghdr *msgh, int flags,
			      size_t size);
/*
 * stream_recvfrom：以 recvfrom 方式接收数据并追加到 stream，同时回填来源地址。
 * 参数 len 为上限，flags 为接收标志，from 与 fromlen 为来源地址及其长度。
 * 返回接收字节数或负的错误码。
 */
extern ssize_t stream_recvfrom(struct stream *s, int fd, size_t len, int flags,
			       struct sockaddr *from, socklen_t *fromlen);
/*
 * stream_write：把数据追加到 stream 尾部（与 fd 无关的纯入队，名字沿用历史）。
 * 参数 ptr 为源数据，size 为长度。返回写入的字节数，可能因空间不足而小于 size。
 */
extern size_t stream_write(struct stream *s, const void *ptr, size_t size);

/* reset the stream. See Note above */
/*
 * stream_reset：把 getp 与 endp 归零，使 stream 看起来像空缓冲区以便复用。
 * 参数 s 为目标 stream。易踩的坑：并不清零底层 data（见文件顶部英文说明），
 * 若需要确定性内容应显式写入。
 */
extern void stream_reset(struct stream *s);
/*
 * stream_flush：把 stream 中 [getp, endp) 的数据写出到描述符并推进 getp。
 * 返回写出的字节数，出错返回负值。易踩的坑：部分写时只推进已写出的部分。
 */
extern int stream_flush(struct stream *s, int fd);
/*
 * stream_empty：判断是否已无可读数据。
 * 参数 s 为目标 stream。返回 1 表示 getp 已追上 endp（读空），否则返回 0。
 */
extern int stream_empty(struct stream *s); /* is the stream empty? */

/* debugging */
/*
 * stream_hexdump：把 stream 的当前可读内容以十六进制形式打印到调试日志，供排错。
 * 参数 s 为源 stream。无返回值。
 */
extern void stream_hexdump(const struct stream *s);

/**
 * Reorganize the buffer data so it can fit more. This function is normally
 * called right after stream data is consumed so we can read more data
 * (the functions that consume data start with `stream_get*()` and macros
 * `STREAM_GET*()`).
 *
 * \param s stream pointer.
 */
/*
 * stream_pulldown：把尚未消费的数据前移，使 getp 归零，从而腾出尾部空间继续读入。
 * 参数 s 为目标 stream。常在解析完一段数据后、准备再从描述符读取前调用。
 * 易踩的坑：会搬移 data 内容，之前保存的裸偏移 / 指针需按新布局调整。
 */
extern void stream_pulldown(struct stream *s);

/* deprecated */
/*
 * stream_pnt：返回 stream 内部 data 缓冲区的裸指针（指向头部）。已过时。
 * 参数为源 stream。易踩的坑：可扩容的 stream 扩容后该指针失效。
 */
extern uint8_t *stream_pnt(struct stream *);

/*
 * Operations on struct stream_fifo.
 *
 * Each function has a safe variant, which ensures that the operation performed
 * is atomic with respect to the operations performed by all other safe
 * variants. In other words, the safe variants lock the stream_fifo's mutex
 * before performing their action. These are provided for convenience when
 * using stream_fifo in a multithreaded context, to alleviate the need for the
 * caller to implement their own synchronization around the stream_fifo.
 *
 * The following functions do not have safe variants. The caller must ensure
 * that these operations are performed safely in a multithreaded context:
 * - stream_fifo_new
 * - stream_fifo_free
 */

/*
 * Create a new stream_fifo.
 *
 * Returns:
 *    newly created stream_fifo
 */
/*
 * stream_fifo_new：分配并初始化一个 stream_fifo（含互斥锁与原子计数器）。
 * 无参数；返回新建的 fifo。本函数没有 _safe 变体，多线程环境下创建阶段需调用方
 * 自行保证安全。
 */
extern struct stream_fifo *stream_fifo_new(void);

/*
 * Init or re-init an on-stack fifo. This allows use of a fifo struct without
 * requiring a malloc/free cycle.
 * Note well that the fifo must be de-inited with the 'fifo_deinit' api.
 */
/*
 * stream_fifo_init：就地初始化一个栈上分配的 stream_fifo，省去 malloc / free 往返。
 * 参数 fifo 为调用方提供的存储。注意必须用 stream_fifo_deinit 成对反初始化。
 */
void stream_fifo_init(struct stream_fifo *fifo);

/*
 * Deinit an on-stack fifo.
 */
/*
 * stream_fifo_deinit：反初始化一个由 stream_fifo_init 初始化的栈上 fifo，
 * 释放其内部资源（互斥锁等）但不释放 fifo 本身（它由调用方持有）。
 */
void stream_fifo_deinit(struct stream_fifo *fifo);

/*
 * Push a stream onto a stream_fifo.
 *
 * fifo
 *    the stream_fifo to push onto
 *
 * s
 *    the stream to push onto the stream_fifo
 */
/*
 * stream_fifo_push：把 s 压入 fifo 尾部。
 * stream_fifo_push_safe 是它的加锁版本：在访问队列前后锁定 fifo 的互斥锁，
 * 可与其它 _safe 操作并发安全地配合。非 _safe 版本需调用方自行同步。
 * 参数 fifo 为目标队列，s 为待入队的 stream。无返回值。
 */
extern void stream_fifo_push(struct stream_fifo *fifo, struct stream *s);
extern void stream_fifo_push_safe(struct stream_fifo *fifo, struct stream *s);

/*
 * Pop a stream off a stream_fifo.
 *
 * fifo
 *    the stream_fifo to pop from
 *
 * Returns:
 *    the next stream in the stream_fifo
 */
/*
 * stream_fifo_pop：从 fifo 头部取出并返回一个 stream（出队）。
 * stream_fifo_pop_safe 是它的加锁版本。参数 fifo 为源队列。
 * 返回队首的 stream，队列为空时返回空指针。
 */
extern struct stream *stream_fifo_pop(struct stream_fifo *fifo);
extern struct stream *stream_fifo_pop_safe(struct stream_fifo *fifo);

/*
 * Retrieve the next stream from a stream_fifo without popping it.
 *
 * fifo
 *    the stream_fifo to operate on
 *
 * Returns:
 *    the next stream that would be returned from stream_fifo_pop
 */
/*
 * stream_fifo_head：查看 fifo 头部元素但不将其移出队列。
 * stream_fifo_head_safe 是它的加锁版本。参数 fifo 为源队列。
 * 返回队首的 stream（与下一次 pop 会返回的相同），为空时返回空指针。
 */
extern struct stream *stream_fifo_head(struct stream_fifo *fifo);
extern struct stream *stream_fifo_head_safe(struct stream_fifo *fifo);

/*
 * Remove all streams from a stream_fifo.
 *
 * fifo
 *    the stream_fifo to clean
 */
/*
 * stream_fifo_clean：移除并释放 fifo 中的所有 stream，把队列清空。
 * stream_fifo_clean_safe 是它的加锁版本。参数 fifo 为目标队列。无返回值。
 */
extern void stream_fifo_clean(struct stream_fifo *fifo);
extern void stream_fifo_clean_safe(struct stream_fifo *fifo);

/*
 * Retrieve number of streams on a stream_fifo.
 *
 * fifo
 *    the stream_fifo to retrieve the count for
 *
 * Returns:
 *    the number of streams on the stream_fifo
 */
/*
 * stream_fifo_count_safe：返回 fifo 中当前 stream 的条数（原子读取 count）。
 * 参数 fifo 为目标队列。只有加锁版本，可安全地并发调用。
 */
extern size_t stream_fifo_count_safe(struct stream_fifo *fifo);

/*
 * Free a stream_fifo.
 *
 * Calls stream_fifo_clean, then deinitializes the stream_fifo and frees it.
 *
 * fifo
 *    the stream_fifo to free
 */
/*
 * stream_fifo_free：调用 stream_fifo_clean 清空队列，再反初始化并释放 fifo 本身。
 * 参数 fifo 为待销毁的队列。本函数没有 _safe 变体，销毁阶段需调用方自行保证安全。
 */
extern void stream_fifo_free(struct stream_fifo *fifo);

/* This is here because "<< 24" is particularly problematic in C.
 * This is because the left operand of << is integer-promoted, which means
 * an uint8_t gets converted into a *signed* int.  Shifting into the sign
 * bit of a signed int is theoretically undefined behaviour, so - the left
 * operand needs to be cast to unsigned.
 *
 * This is not a problem for 16- or 8-bit values (they don't reach the sign
 * bit), for 64-bit values (you need to cast them anyway), and neither for
 * encoding (because it's downcasted.)
 */
/*
 * ptr_get_be64：从一个裸指针处解析 64 位大端整数到 *out，返回指向其后 8 字节的
 * 指针，便于链式解析。分两次 memcpy 取回高位与低位 32 位再各自 ntohl 后拼合，
 * 从而避开未对齐访问与有符号移位的问题。用于解析已收到缓冲中的裸报文。
 */
static inline const uint8_t *ptr_get_be64(const uint8_t *ptr, uint64_t *out)
{
	uint32_t tmp1, tmp2;

	memcpy(&tmp1, ptr, sizeof(tmp1));
	memcpy(&tmp2, ptr + sizeof(tmp1), sizeof(tmp1));

	*out = (((uint64_t)ntohl(tmp1)) << 32) | ntohl(tmp2);

	return ptr + 8;
}

/*
 * ptr_get_be32：解析 32 位大端整数到 *out，返回其后 4 字节处的指针。
 * memcpy 取出后再 ntohl，避免未对齐访问。
 */
static inline const uint8_t *ptr_get_be32(const uint8_t *ptr, uint32_t *out)
{
	uint32_t tmp;

	memcpy(&tmp, ptr, sizeof(tmp));
	*out = ntohl(tmp);
	return ptr + 4;
}

/*
 * ptr_get_be16：解析 16 位大端整数到 *out，返回其后 2 字节处的指针。
 * memcpy 取出后再 ntohs。注意其参数与返回值是非 const 的 uint8_t 指针。
 */
static inline uint8_t *ptr_get_be16(uint8_t *ptr, uint16_t *out)
{
	uint16_t tmp;

	memcpy(&tmp, ptr, sizeof(tmp));
	*out = ntohs(tmp);

	return ptr + 2;
}

/*
 * so Normal stream_getX functions assert.  Which is anathema
 * to keeping a daemon up and running when something goes south
 * Provide a stream_getX2 functions that do not assert.
 * In addition provide these macro's that upon failure
 * goto stream_failure.  This is modeled upon some NL_XX
 * macros in the linux kernel.
 *
 * This change allows for proper memory freeing
 * after we've detected an error.
 *
 * In the future we will be removing the assert in
 * the stream functions but we need a transition
 * plan.
 */
/*
 * STREAM_GET 系列宏：基于 stream_getX2 安全族解析报文的推荐写法。每个宏都按
 * do 循环包裹，若对应的安全函数返回 false（数据不足）就跳到调用方提供的
 * stream_failure 标签，从而在单一出口处统一释放内存、避免断言把守护进程打挂。
 * 宏参数 P 是接收结果的目标（左值），S 是 stream，SIZE 是长度，STR 是 stream。
 */
#define STREAM_GETC(S, P)                                                      \
	do {                                                                   \
		uint8_t _pval;                                                 \
		if (!stream_getc2((S), &_pval))                                \
			goto stream_failure;                                   \
		(P) = _pval;                                                   \
	} while (0)

/*
 * STREAM_GETW：读取 16 位字到 P；失败跳到 stream_failure。
 */
#define STREAM_GETW(S, P)                                                      \
	do {                                                                   \
		uint16_t _pval;                                                \
		if (!stream_getw2((S), &_pval))                                \
			goto stream_failure;                                   \
		(P) = _pval;                                                   \
	} while (0)

/*
 * STREAM_GETL：读取 32 位长字到 P；失败跳到 stream_failure。
 */
#define STREAM_GETL(S, P)                                                      \
	do {                                                                   \
		uint32_t _pval;                                                \
		if (!stream_getl2((S), &_pval))                                \
			goto stream_failure;                                   \
		(P) = _pval;                                                   \
	} while (0)

/*
 * STREAM_GETF：读取 32 位浮点（借 union 按位解释）到 P；失败跳到 stream_failure。
 */
#define STREAM_GETF(S, P)                                                      \
	do {                                                                   \
		union {                                                        \
			float r;                                               \
			uint32_t d;                                            \
		} _pval;                                                       \
		if (!stream_getl2((S), &_pval.d))                              \
			goto stream_failure;                                   \
		(P) = _pval.r;                                                 \
	} while (0)

/*
 * STREAM_GETQ：读取 64 位四字到 P；失败跳到 stream_failure。
 */
#define STREAM_GETQ(S, P)                                                      \
	do {                                                                   \
		uint64_t _pval;                                                \
		if (!stream_getq2((S), &_pval))                                \
			goto stream_failure;                                   \
		(P) = _pval;                                                   \
	} while (0)

/*
 * STREAM_GET_IPADDR：读取一个 IP 地址到 P；失败跳到 stream_failure。
 */
#define STREAM_GET_IPADDR(S, P)                                                \
	do {                                                                   \
		if (!stream_get_ipaddr((S), (P)))                              \
			goto stream_failure;                                   \
	} while (0)

/*
 * STREAM_GET：从 STR 读取 SIZE 字节到 P；失败跳到 stream_failure。
 */
#define STREAM_GET(P, STR, SIZE)                                               \
	do {                                                                   \
		if (!stream_get2((P), (STR), (SIZE)))                          \
			goto stream_failure;                                   \
	} while (0)

/*
 * STREAM_FORWARD_GETP：把 STR 的读取游标前移 SIZE 字节；失败跳到 stream_failure。
 */
#define STREAM_FORWARD_GETP(STR, SIZE)                                         \
	do {                                                                   \
		if (!stream_forward_getp2((STR), (SIZE)))                      \
			goto stream_failure;                                   \
	} while (0)

/*
 * STREAM_REWIND_GETP：把 STR 的读取游标后移 SIZE 字节；失败跳到 stream_failure。
 */
#define STREAM_REWIND_GETP(STR, SIZE)                                          \
	do {                                                                   \
		if (!stream_rewind_getp2((STR), (SIZE)))                       \
			goto stream_failure;                                   \
	} while (0)

/*
 * STREAM_FORWARD_ENDP：把 STR 的写位置前移 SIZE 字节；失败跳到 stream_failure。
 */
#define STREAM_FORWARD_ENDP(STR, SIZE)                                         \
	do {                                                                   \
		if (!stream_forward_endp2((STR), (SIZE)))                      \
			goto stream_failure;                                   \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_STREAM_H */
