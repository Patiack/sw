// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Packet interface
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

#include <zebra.h>
#include <stddef.h>
#include <pthread.h>

#include "stream.h"
#include "memory.h"
#include "network.h"
#include "prefix.h"
#include "log.h"
#include "frr_pthread.h"
#include "lib_errors.h"

/*
 * MIN_STREAM_EXPANSION_SZ：可扩容 stream 单次扩容的最小步长；小于此值的增长
 * 请求会被抬高到该值，以减少频繁的小幅重分配。
 */
#define MIN_STREAM_EXPANSION_SZ 512

/* Extra size  needed for a stream in bytes, given new write size */
/*
 * STREAM_EXPAND_SIZE：给定一次期望写入 WSZ 字节，算出还需额外扩大的字节数，
 * 即期望写入量减去当前可写量。
 */
#define STREAM_EXPAND_SIZE(S, WSZ) ((WSZ)-STREAM_WRITEABLE(S))

/*
 * 文件导读（stream.c）：stream 报文游标抽象的完整实现。
 *
 * 本文件实现 stream.h 的接口。一个 stream 用 data、size、getp、endp 四个核心
 * 成员表示一段可读可写的网络序字节缓冲，恒满足 getp 小于等于 endp 小于等于
 * size。写入从 endp 开始并推进 endp；读取从 getp 开始并推进 getp。带 _at 后缀的
 * 函数在指定绝对偏移原地改写，常用于回填长度或校验和。
 *
 * 字节序：所有 put / get 的定长数值函数都在这一层完成主机序与网络序的互转
 * （put 先把主机序转成网络序再写，get 读出后转回主机序），调用方拿到的是主机序。
 *
 * 越界策略：本文件用一组宏把关——STREAM_VERIFY_SANE 在每个函数入口校验不变式并
 * 在异常时打印回溯；STREAM_BOUND_WARN 在越界时告警并断言终止进程。相对地，
 * stream_getX2 与 STREAM_GET 系列走“返回失败 / 跳转到 stream_failure”的不中断
 * 路径，供解析不可信报文时安全出错。
 *
 * 复用语义：stream_reset 只把 getp 与 endp 归零，不会清零 data；而本文件新建的
 * stream 用 XMALLOC 分配（不是 XCALLOC），data 内容未定义，需要确定性内容时应
 * 显式写入，或用 stream_forward_endp 预留后再回填。
 *
 * 本文件还实现 stream_fifo（带互斥锁与原子计数的线程安全流队列，_safe 后缀的
 * 操作自动加锁），以及与 socket 收发配合的 stream_read / stream_write 等辅助。
 */
DEFINE_MTYPE_STATIC(LIB, STREAM, "Stream");
DEFINE_MTYPE_STATIC(LIB, STREAM_FIFO, "Stream FIFO");

/*
 * 游标合法性判定宏：
 * GETP_VALID 判定读取位置 G 不超过 endp；
 * PUT_AT_VALID 复用同一判定，用于 put..._at 的绝对写位置；
 * ENDP_VALID 判定位置 E 不超过 size。
 * 断言与越界检查都建立在这三个宏之上。
 */
/* Tests whether a position is valid */
#define GETP_VALID(S, G) ((G) <= (S)->endp)
#define PUT_AT_VALID(S,G) GETP_VALID(S,G)
#define ENDP_VALID(S, E) ((E) <= (S)->size)

/* asserting sanity checks. Following must be true before
 * stream functions are called:
 *
 * Following must always be true of stream elements
 * before and after calls to stream functions:
 *
 * getp <= endp <= size
 *
 * Note that after a stream function is called following may be true:
 * if (getp == endp) then stream is no longer readable
 * if (endp == size) then stream is no longer writeable
 *
 * It is valid to put to anywhere within the size of the stream, but only
 * using stream_put..._at() functions.
 */
/*
 * STREAM_WARN_OFFSETS：打印 stream 的关键字段（指针、size、getp、endp）并输出
 * 一次调用栈回溯，供游标异常时定位问题。仅告警，不终止。
 */
#define STREAM_WARN_OFFSETS(S)                                                 \
	do {                                                                   \
		flog_warn(EC_LIB_STREAM,				       \
			  "&(struct stream): %p, size: %lu, getp: %lu, endp: %lu", \
			  (void *)(S), (unsigned long)(S)->size,	       \
			  (unsigned long)(S)->getp, (unsigned long)(S)->endp); \
		zlog_backtrace(LOG_WARNING);				       \
	} while (0)

/*
 * STREAM_VERIFY_SANE：入口处的自检宏。若 getp 或 endp 越出合法范围，先打印偏移
 * 信息与回溯，再对二者断言；用于在每个 stream 函数开头捕获已被破坏的状态。
 */
#define STREAM_VERIFY_SANE(S)                                                  \
	do {                                                                   \
		if (!(GETP_VALID(S, (S)->getp) && ENDP_VALID(S, (S)->endp))) { \
			STREAM_WARN_OFFSETS(S);                                \
		}                                                              \
		assert(GETP_VALID(S, (S)->getp));                              \
		assert(ENDP_VALID(S, (S)->endp));                              \
	} while (0)

/*
 * STREAM_BOUND_WARN：越界访问的强告警宏：打印告警与回溯后直接 assert(0) 终止
 * 进程。用于旧式 stream_getX / stream_putX 的越界场景。
 */
#define STREAM_BOUND_WARN(S, WHAT)                                             \
	do {                                                                   \
		flog_warn(EC_LIB_STREAM, "%s: Attempt to %s out of bounds",    \
			  __func__, (WHAT));                                   \
		STREAM_WARN_OFFSETS(S);                                        \
		assert(0);                                                     \
	} while (0)

/*
 * STREAM_BOUND_WARN2：与 STREAM_BOUND_WARN 相同但不终止进程的版本，仅告警并
 * 打印回溯后返回，供安全族函数在返回失败前留痕。
 */
#define STREAM_BOUND_WARN2(S, WHAT)                                            \
	do {                                                                   \
		flog_warn(EC_LIB_STREAM, "%s: Attempt to %s out of bounds",    \
			  __func__, (WHAT));                                   \
		STREAM_WARN_OFFSETS(S);                                        \
	} while (0)

/* XXX: Deprecated macro: do not use */
/*
 * CHECK_SIZE：已过时的宏，把请求长度 Z 夹取到剩余可写空间之内，并在需要截断时
 * 告警。新代码不要使用。
 */
#define CHECK_SIZE(S, Z)                                                       \
	do {                                                                   \
		if (((S)->endp + (Z)) > (S)->size) {                           \
			flog_warn(                                             \
				EC_LIB_STREAM,                                 \
				"CHECK_SIZE: truncating requested size %lu",   \
				(unsigned long)(Z));                           \
			STREAM_WARN_OFFSETS(S);                                \
			(Z) = (S)->size - (S)->endp;                           \
		}                                                              \
	} while (0);

/* Make stream buffer. */
/*
 * stream_new：分配一个固定容量（不可扩容）的 stream，游标归零。
 * 参数 size 为 data 区字节数，必须大于 0（否则断言）。返回新建的 stream；
 * XMALLOC 失败会终止进程。易踩的坑：data 用 XMALLOC 分配，内容未清零，不能
 * 假定新流的字节为零，需要时应显式写入。
 */
struct stream *stream_new(size_t size)
{
	struct stream *s;

	assert(size > 0);

	s = XMALLOC(MTYPE_STREAM, sizeof(struct stream));
	s->data = XMALLOC(MTYPE_STREAM, size);

	s->getp = s->endp = 0;
	s->next = NULL;
	s->size = size;
	s->allow_expansion = false;
	return s;
}

/*
 * stream_new_expandable：在 stream_new 的基础上把 allow_expansion 置真，使写入
 * 空间不足时可自动扩容。参数 size 为初始容量。返回新建的 stream。
 * 易踩的坑：扩容会重新分配 data，之前保存的裸指针会失效。
 */
struct stream *stream_new_expandable(size_t size)
{
	struct stream *s = stream_new(size);
	s->allow_expansion = true;
	return s;
}

/* Free it now. */
/*
 * stream_free：释放 stream 的 data 区与结构体本身。
 * 参数 s 可为空（为空时直接返回，不做任何事）。无返回值。
 * 易踩的坑：它假设 s 未被链入其它结构（如 fifo），否则会留下悬空指针。
 */
void stream_free(struct stream *s)
{
	if (!s)
		return;

	XFREE(MTYPE_STREAM, s->data);
	XFREE(MTYPE_STREAM, s);
}

/*
 * stream_copy：把 src 的容量标记、游标与数据整体复制到已存在的 dest。
 * 参数 dest 必须非空且容量不小于 src->endp（否则断言）；src 会先做自检。
 * 复制 allow_expansion、endp、getp 三个字段，并把 [0, endp) 的字节 memcpy 过去。
 * 返回 dest。
 */
struct stream *stream_copy(struct stream *dest, const struct stream *src)
{
	STREAM_VERIFY_SANE(src);

	assert(dest != NULL);
	assert(STREAM_SIZE(dest) >= src->endp);
	dest->allow_expansion = src->allow_expansion;
	dest->endp = src->endp;
	dest->getp = src->getp;

	memcpy(dest->data, src->data, src->endp);

	return dest;
}

/*
 * stream_dup：复制出一个全新的 stream 副本。
 * 参数 s 为源 stream（先自检）。新建一个容量等于 s->endp 的流并沿用其
 * allow_expansion，再 stream_copy 复制内容。返回新副本。
 */
struct stream *stream_dup(const struct stream *s)
{
	struct stream *snew;

	STREAM_VERIFY_SANE(s);

	snew = stream_new(s->endp);
	snew->allow_expansion = s->allow_expansion;
	return (stream_copy(snew, s));
}

/*
 * stream_dupcat：把 s1 的前 offset 字节、s2 全部、s1 剩余部分依次拼接成新流。
 * 参数 offset 为 s1 的分割点，必须不大于 s1->endp，否则打印错误并返回空指针。
 * 新流容量为 s1->endp 加 s2->endp，allow_expansion 取二者的或。返回新流。
 * 易踩的坑：三段各做一次 memcpy，s1 剩余段长度按 endp 减 offset 计算。
 */
struct stream *stream_dupcat(const struct stream *s1, const struct stream *s2,
			     size_t offset)
{
	struct stream *new;

	STREAM_VERIFY_SANE(s1);
	STREAM_VERIFY_SANE(s2);

	if (offset > s1->endp) {
		fprintf(stderr, "Error: Invalid offset %zu, exceeds s1->endp %zu\n", offset,
			s1->endp);
		return NULL;
	}

	if ((new = stream_new(s1->endp + s2->endp)) == NULL)
		return NULL;

	new->allow_expansion = s1->allow_expansion || s2->allow_expansion;
	memcpy(new->data, s1->data, offset);
	memcpy(new->data + offset, s2->data, s2->endp);
	memcpy(new->data + offset + s2->endp, s1->data + offset,
	       (s1->endp - offset));
	new->endp = s1->endp + s2->endp;
	return new;
}

/*
 * stream_resize_inplace：用 XREALLOC 原地把 stream 容量调整为 newsize。
 * 参数 sptr 是 stream 指针的地址（容量变化后需要回写）；newsize 为目标容量。
 * 缩小时会把 endp 夹到不超过 size、getp 夹到不超过 endp。返回生效后的 size。
 * 易踩的坑：传入的是二级指针；扩容后旧 data 指针失效。
 */
size_t stream_resize_inplace(struct stream **sptr, size_t newsize)
{
	struct stream *orig = *sptr;

	STREAM_VERIFY_SANE(orig);

	orig->data = XREALLOC(MTYPE_STREAM, orig->data, newsize);

	orig->size = newsize;

	if (orig->endp > orig->size)
		orig->endp = orig->size;
	if (orig->getp > orig->endp)
		orig->getp = orig->endp;

	STREAM_VERIFY_SANE(orig);

	*sptr = orig;
	return orig->size;
}

/* Helper function to expand stream if needed and allowed */
/*
 * stream_expand：内部辅助，按需扩大 stream 的 data 区。
 * 参数 expand_size 为期望的扩大量；增长策略是：若不超过 MIN_STREAM_EXPANSION_SZ
 * 就至少扩一个最小步长，否则按所需大小扩。用 XREALLOC 重新分配并更新 size。
 * 无返回值，也不改变游标。仅供应扩容的写入路径调用。
 */
static void stream_expand(struct stream *s, size_t expand_size)
{
	size_t new_size;
	size_t actual_expand_size = expand_size;

	/* Growth strategy:
	 * For small expansions (<= min expansion bytes): grow by min size
	 * otherwise grow by needed size
	 */
	if (actual_expand_size <= MIN_STREAM_EXPANSION_SZ) {
		actual_expand_size = MIN_STREAM_EXPANSION_SZ;
	}

	/* Calculate new total size */
	new_size = s->size + actual_expand_size;
	/* Reallocate the data buffer */
	s->data = XREALLOC(MTYPE_STREAM, s->data, new_size);

	/* Update the stream's data size */
	s->size = new_size;
}

/*
 * stream_get_getp：返回当前读取游标 getp（先做自检）。参数 s 为源 stream。
 */
size_t stream_get_getp(const struct stream *s)
{
	STREAM_VERIFY_SANE(s);
	return s->getp;
}

/*
 * stream_get_endp：返回有效数据结束位置 endp（通常即当前报文长度）。先做自检。
 */
size_t stream_get_endp(const struct stream *s)
{
	STREAM_VERIFY_SANE(s);
	return s->endp;
}

/*
 * stream_get_size：返回 stream 分配的总容量 size。先做自检。
 */
size_t stream_get_size(const struct stream *s)
{
	STREAM_VERIFY_SANE(s);
	return s->size;
}

/* Stream structre' stream pointer related functions.  */
/*
 * stream_set_getp：把读取游标设置到绝对位置 pos。
 * 参数 pos 若超过 endp（GETP_VALID 失败）会告警断言，并把 getp 夹到 endp 而不再
 * 越界。无返回值。常用于回定位到报文某字段处重新解析。
 */
void stream_set_getp(struct stream *s, size_t pos)
{
	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, pos)) {
		STREAM_BOUND_WARN(s, "set getp");
		pos = s->endp;
	}

	s->getp = pos;
}

/*
 * stream_set_endp：把有效数据结束位置设置到 pos（即人为声明报文长度）。
 * 参数 pos 必须不超过 size，且不小于当前 getp（否则告警断言并直接返回，不生效）。
 * 成功后再次自检。无返回值。常用在读到长度字段后据此截断报文范围。
 */
void stream_set_endp(struct stream *s, size_t pos)
{
	STREAM_VERIFY_SANE(s);

	if (!ENDP_VALID(s, pos)) {
		STREAM_BOUND_WARN(s, "set endp");
		return;
	}

	/*
	 * Make sure the current read pointer is not beyond the new endp.
	 */
	if (s->getp > pos) {
		STREAM_BOUND_WARN(s, "set endp");
		return;
	}

	s->endp = pos;
	STREAM_VERIFY_SANE(s);
}

/* Forward pointer. */
/*
 * stream_forward_getp：把读取游标向前推进 size 字节（跳过一段数据）。
 * 越界（getp 加 size 超过 endp）时告警断言并放弃推进。无返回值。
 */
void stream_forward_getp(struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, s->getp + size)) {
		STREAM_BOUND_WARN(s, "seek getp");
		return;
	}

	s->getp += size;
}

/*
 * stream_forward_getp2：stream_forward_getp 的安全版本。越界时返回 false 且不改
 * 游标，否则推进并返回 true。供 STREAM_FORWARD_GETP 宏使用。
 */
bool stream_forward_getp2(struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, s->getp + size))
		return false;

	s->getp += size;

	return true;
}

/*
 * stream_rewind_getp：把读取游标向后退 size 字节（回看已读数据）。
 * 若 size 超过已读量（大于 getp）或回退后位置非法则告警断言并放弃。无返回值。
 */
void stream_rewind_getp(struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (size > s->getp || !GETP_VALID(s, s->getp - size)) {
		STREAM_BOUND_WARN(s, "rewind getp");
		return;
	}

	s->getp -= size;
}

/*
 * stream_rewind_getp2：stream_rewind_getp 的安全版本。越界时返回 false 且不改游标，
 * 否则回退并返回 true。供 STREAM_REWIND_GETP 宏使用。
 */
bool stream_rewind_getp2(struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (size > s->getp || !GETP_VALID(s, s->getp - size))
		return false;

	s->getp -= size;

	return true;
}

/*
 * stream_forward_endp：把写位置 endp 向前推进 size 字节，预留一段空间（内容未
 * 定义，需调用方随后写入）。越界（endp 加 size 超过 size）时告警断言并放弃。
 * 无返回值。
 */
void stream_forward_endp(struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (!ENDP_VALID(s, s->endp + size)) {
		STREAM_BOUND_WARN(s, "seek endp");
		return;
	}

	s->endp += size;
}

/*
 * stream_forward_endp2：stream_forward_endp 的安全版本。越界时返回 false 且不改
 * endp，否则推进并返回 true。供 STREAM_FORWARD_ENDP 宏使用。
 */
bool stream_forward_endp2(struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (!ENDP_VALID(s, s->endp + size))
		return false;

	s->endp += size;

	return true;
}

/* Copy from stream to destination. */
/*
 * stream_get2：从 getp 处读取 size 字节原始数据到 dst 的安全版本。
 * 可读数据不足时告警（不终止）并返回 false 且不推进 getp；否则拷贝并推进 getp，
 * 返回 true。供 STREAM_GET 宏使用。
 */
bool stream_get2(void *dst, struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < size) {
		STREAM_BOUND_WARN2(s, "get");
		return false;
	}

	memcpy(dst, s->data + s->getp, size);
	s->getp += size;

	return true;
}

/*
 * stream_get：从 getp 处读取 size 字节原始数据到 dst 并推进 getp，不做字节序转换。
 * 可读数据不足时告警断言并直接返回（不拷贝）。无返回值。
 */
void stream_get(void *dst, struct stream *s, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < size) {
		STREAM_BOUND_WARN(s, "get");
		return;
	}

	memcpy(dst, s->data + s->getp, size);
	s->getp += size;
}

/* Get next character from the stream. */
/*
 * stream_getc2：读取一个字节到 *byte 的安全版本。不足时告警（不终止）并返回
 * false，否则读取并推进 getp，返回 true。供 STREAM_GETC 宏使用。
 */
bool stream_getc2(struct stream *s, uint8_t *byte)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint8_t)) {
		STREAM_BOUND_WARN2(s, "get char");
		return false;
	}
	*byte = s->data[s->getp++];

	return true;
}

/*
 * stream_getc：读取一个字节并推进 getp。返回该字节；不足时告警断言并返回 0。
 */
uint8_t stream_getc(struct stream *s)
{
	uint8_t c;

	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint8_t)) {
		STREAM_BOUND_WARN(s, "get char");
		return 0;
	}
	c = s->data[s->getp++];

	return c;
}

/* Get next character from the stream. */
/*
 * stream_getc_from：读取绝对偏移 from 处的一个字节，不改变 getp。
 * 若 from 加 1 超过 endp 则告警断言并返回 0。
 */
uint8_t stream_getc_from(struct stream *s, size_t from)
{
	uint8_t c;

	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, from + sizeof(uint8_t))) {
		STREAM_BOUND_WARN(s, "get char");
		return 0;
	}

	c = s->data[from];

	return c;
}

/*
 * stream_getw2：读取一个网络序 16 位字并转主机序写入 *word 的安全版本。
 * 不足时告警（不终止）并返回 false，否则逐字节按大端拼装、推进 getp 两次并返回
 * true。供 STREAM_GETW 宏使用。
 */
bool stream_getw2(struct stream *s, uint16_t *word)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint16_t)) {
		STREAM_BOUND_WARN2(s, "get ");
		return false;
	}

	*word = s->data[s->getp++] << 8;
	*word |= s->data[s->getp++];

	return true;
}

/* Get next word from the stream. */
/*
 * stream_getw：读取一个网络序 16 位字（高字节在前）并返回主机序值，推进 getp。
 * 不足时告警断言并返回 0。
 */
uint16_t stream_getw(struct stream *s)
{
	uint16_t w;

	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint16_t)) {
		STREAM_BOUND_WARN(s, "get ");
		return 0;
	}

	w = s->data[s->getp++] << 8;
	w |= s->data[s->getp++];

	return w;
}

/* Get next word from the stream. */
/*
 * stream_getw_from：读取绝对偏移 from 处的网络序 16 位字并返回主机序值，不改 getp。
 * 越界时告警断言并返回 0。
 */
uint16_t stream_getw_from(struct stream *s, size_t from)
{
	uint16_t w;

	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, from + sizeof(uint16_t))) {
		STREAM_BOUND_WARN(s, "get ");
		return 0;
	}

	w = s->data[from++] << 8;
	w |= s->data[from];

	return w;
}

/* Get next 3-byte from the stream. */
/*
 * stream_get3_from：读取绝对偏移 from 处的 24 位大端整数并返回主机序值，不改 getp。
 * 越界时告警断言并返回 0。
 */
uint32_t stream_get3_from(struct stream *s, size_t from)
{
	uint32_t l;

	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, from + 3)) {
		STREAM_BOUND_WARN(s, "get 3byte");
		return 0;
	}

	l = s->data[from++] << 16;
	l |= s->data[from++] << 8;
	l |= s->data[from];

	return l;
}

/*
 * stream_get3：读取一个 24 位（3 字节）大端整数并返回主机序值，推进 getp。
 * 不足时告警断言并返回 0。
 */
uint32_t stream_get3(struct stream *s)
{
	uint32_t l;

	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < 3) {
		STREAM_BOUND_WARN(s, "get 3byte");
		return 0;
	}

	l = s->data[s->getp++] << 16;
	l |= s->data[s->getp++] << 8;
	l |= s->data[s->getp++];

	return l;
}

/* Get next long word from the stream. */
/*
 * stream_getl_from：读取绝对偏移 from 处的网络序 32 位长字并返回主机序值，不改
 * getp。越界时告警断言并返回 0。
 */
uint32_t stream_getl_from(struct stream *s, size_t from)
{
	uint32_t l;

	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, from + sizeof(uint32_t))) {
		STREAM_BOUND_WARN(s, "get long");
		return 0;
	}

	l = (unsigned)(s->data[from++]) << 24;
	l |= s->data[from++] << 16;
	l |= s->data[from++] << 8;
	l |= s->data[from];

	return l;
}

/* Copy from stream at specific location to destination. */
/*
 * stream_get_from：从绝对偏移 from 处读取 size 字节原始数据到 dst，不改变 getp。
 * 若 from 加 size 超过 endp 则告警断言并直接返回。无返回值。
 */
void stream_get_from(void *dst, struct stream *s, size_t from, size_t size)
{
	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, from + size)) {
		STREAM_BOUND_WARN(s, "get from");
		return;
	}

	memcpy(dst, s->data + from, size);
}

/*
 * stream_getl2：读取一个网络序 32 位长字并转主机序写入 *l 的安全版本。
 * 不足时告警（不终止）并返回 false，否则逐字节大端拼装、推进 getp 四次并返回
 * true。供 STREAM_GETL 宏使用。
 */
bool stream_getl2(struct stream *s, uint32_t *l)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint32_t)) {
		STREAM_BOUND_WARN2(s, "get long");
		return false;
	}

	*l = (unsigned int)(s->data[s->getp++]) << 24;
	*l |= s->data[s->getp++] << 16;
	*l |= s->data[s->getp++] << 8;
	*l |= s->data[s->getp++];

	return true;
}

/*
 * stream_getl：读取一个网络序 32 位长字并返回主机序值，推进 getp。
 * 不足时告警断言并返回 0。
 */
uint32_t stream_getl(struct stream *s)
{
	uint32_t l;

	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint32_t)) {
		STREAM_BOUND_WARN(s, "get long");
		return 0;
	}

	l = (unsigned)(s->data[s->getp++]) << 24;
	l |= s->data[s->getp++] << 16;
	l |= s->data[s->getp++] << 8;
	l |= s->data[s->getp++];

	return l;
}

/* Get next quad word from the stream. */
/*
 * stream_getq_from：读取绝对偏移 from 处的网络序 64 位四字并返回主机序值，不改
 * getp。越界时告警断言并返回 0。
 */
uint64_t stream_getq_from(struct stream *s, size_t from)
{
	uint64_t q;

	STREAM_VERIFY_SANE(s);

	if (!GETP_VALID(s, from + sizeof(uint64_t))) {
		STREAM_BOUND_WARN(s, "get quad");
		return 0;
	}

	q = ((uint64_t)s->data[from++]) << 56;
	q |= ((uint64_t)s->data[from++]) << 48;
	q |= ((uint64_t)s->data[from++]) << 40;
	q |= ((uint64_t)s->data[from++]) << 32;
	q |= ((uint64_t)s->data[from++]) << 24;
	q |= ((uint64_t)s->data[from++]) << 16;
	q |= ((uint64_t)s->data[from++]) << 8;
	q |= ((uint64_t)s->data[from++]);

	return q;
}

/*
 * stream_getq：读取一个网络序 64 位四字并返回主机序值，推进 getp。
 * 不足时告警断言并返回 0。
 */
uint64_t stream_getq(struct stream *s)
{
	uint64_t q;

	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint64_t)) {
		STREAM_BOUND_WARN(s, "get quad");
		return 0;
	}

	q = ((uint64_t)s->data[s->getp++]) << 56;
	q |= ((uint64_t)s->data[s->getp++]) << 48;
	q |= ((uint64_t)s->data[s->getp++]) << 40;
	q |= ((uint64_t)s->data[s->getp++]) << 32;
	q |= ((uint64_t)s->data[s->getp++]) << 24;
	q |= ((uint64_t)s->data[s->getp++]) << 16;
	q |= ((uint64_t)s->data[s->getp++]) << 8;
	q |= ((uint64_t)s->data[s->getp++]);

	return q;
}

/*
 * stream_getq2：读取一个网络序 64 位四字并写主机序值到 *q 的安全版本。
 * 不足时告警（不终止）并返回 false，否则拼装、推进 getp 并返回 true。
 * 供 STREAM_GETQ 宏使用。
 */
bool stream_getq2(struct stream *s, uint64_t *q)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint64_t)) {
		STREAM_BOUND_WARN2(s, "get uint64");
		return false;
	}

	*q = ((uint64_t)s->data[s->getp++]) << 56;
	*q |= ((uint64_t)s->data[s->getp++]) << 48;
	*q |= ((uint64_t)s->data[s->getp++]) << 40;
	*q |= ((uint64_t)s->data[s->getp++]) << 32;
	*q |= ((uint64_t)s->data[s->getp++]) << 24;
	*q |= ((uint64_t)s->data[s->getp++]) << 16;
	*q |= ((uint64_t)s->data[s->getp++]) << 8;
	*q |= ((uint64_t)s->data[s->getp++]);

	return true;
}

/* Get next long word from the stream. */
/*
 * stream_get_ipv4：读取 4 字节 IPv4 地址到 32 位整数并推进 getp。
 * 实现按字节原样 memcpy，不做端序转换。不足时告警断言并返回 0。
 * 易踩的坑：得到的整数在内存中的字节布局与线路上一致，是否再转换由调用方决定。
 */
uint32_t stream_get_ipv4(struct stream *s)
{
	uint32_t l;

	STREAM_VERIFY_SANE(s);

	if (STREAM_READABLE(s) < sizeof(uint32_t)) {
		STREAM_BOUND_WARN(s, "get ipv4");
		return 0;
	}

	memcpy(&l, s->data + s->getp, sizeof(uint32_t));
	s->getp += sizeof(uint32_t);

	return l;
}

/*
 * stream_get_ipaddr：读取一个 IP 地址到 ip 并推进 getp。
 * 先读 16 位地址族：IPADDR_V4 读 4 字节、IPADDR_V6 读 16 字节，IPADDR_NONE 记
 * 错误并返回 false。可读不足返回 false，成功返回 true。
 * 供 STREAM_GET_IPADDR 宏使用。
 */
bool stream_get_ipaddr(struct stream *s, struct ipaddr *ip)
{
	uint16_t ipa_len = 0;

	STREAM_VERIFY_SANE(s);

	/* Get address type. */
	if (STREAM_READABLE(s) < sizeof(uint16_t)) {
		STREAM_BOUND_WARN2(s, "get ipaddr");
		return false;
	}
	ip->ipa_type = stream_getw(s);

	/* Get address value. */
	switch (ip->ipa_type) {
	case IPADDR_V4:
		ipa_len = IPV4_MAX_BYTELEN;
		break;
	case IPADDR_V6:
		ipa_len = IPV6_MAX_BYTELEN;
		break;
	case IPADDR_NONE:
		flog_err(EC_LIB_DEVELOPMENT,
			 "%s: unknown ip address-family: %u", __func__,
			 ip->ipa_type);
		return false;
	}
	if (STREAM_READABLE(s) < ipa_len) {
		STREAM_BOUND_WARN2(s, "get ipaddr");
		return false;
	}
	memcpy(&ip->ip, s->data + s->getp, ipa_len);
	s->getp += ipa_len;

	return true;
}

/*
 * stream_getf：读取一个 32 位单精度浮点。实现把读到的 4 字节放进 union 的整数
 * 成员再按 float 解释，从而保持位模式不变。返回该浮点值。
 */
float stream_getf(struct stream *s)
{
	union {
		float r;
		uint32_t d;
	} u;
	u.d = stream_getl(s);
	return u.r;
}

/*
 * stream_getd：读取一个 64 位双精度浮点。实现把读到的 8 字节放进 union 的整数
 * 成员再按 double 解释。返回该值。
 */
double stream_getd(struct stream *s)
{
	union {
		double r;
		uint64_t d;
	} u;
	u.d = stream_getq(s);
	return u.r;
}

/* Copy from source to stream.
 *
 * XXX: This uses CHECK_SIZE and hence has funny semantics -> Size will wrap
 * around. This should be fixed once the stream updates are working.
 *
 * stream_write() is saner
 */
/*
 * stream_put：把 size 字节写入 endp 处并推进 endp，不做字节序转换。
 * 参数 src 为源数据，为 NULL 时写入 size 个零字节（用于清零填充）。空间不足时：
 * 若 allow_expansion 为真则 stream_expand 扩容，否则告警断言并放弃。无返回值。
 * 易踩的坑：函数开头调用的是过时宏 CHECK_SIZE，其“截断 / 环绕”语义较怪。
 */
void stream_put(struct stream *s, const void *src, size_t size)
{

	/* XXX: CHECK_SIZE has strange semantics. It should be deprecated */
	CHECK_SIZE(s, size);

	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < size) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, size));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return;
		}
	}

	if (src)
		memcpy(s->data + s->endp, src, size);
	else
		memset(s->data + s->endp, 0, size);

	s->endp += size;
}

/* Put character to the stream. */
/*
 * stream_putc：在 endp 处写入一个字节并推进 endp，不做字节序转换。
 * 返回写入的字节数（1）。空间不足时按 allow_expansion 决定扩容或告警断言。
 */
int stream_putc(struct stream *s, uint8_t c)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < sizeof(uint8_t)) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, sizeof(uint8_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	s->data[s->endp++] = c;
	return sizeof(uint8_t);
}

/* Put word to the stream. */
/*
 * stream_putw：写入一个 16 位字（高字节在前）并推进 endp，完成主机序到网络序的
 * 转换。返回写入的字节数（2）。空间不足时按 allow_expansion 决定扩容或告警断言。
 */
int stream_putw(struct stream *s, uint16_t w)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < sizeof(uint16_t)) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, sizeof(uint16_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	s->data[s->endp++] = (uint8_t)(w >> 8);
	s->data[s->endp++] = (uint8_t)w;

	return 2;
}

/* Put long word to the stream. */
/*
 * stream_put3：写入一个 24 位（3 字节）大端整数并推进 endp。
 * 返回写入的字节数（3）。空间不足时按 allow_expansion 决定扩容或告警断言。
 */
int stream_put3(struct stream *s, uint32_t l)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < 3) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, 3));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	s->data[s->endp++] = (uint8_t)(l >> 16);
	s->data[s->endp++] = (uint8_t)(l >> 8);
	s->data[s->endp++] = (uint8_t)l;

	return 3;
}

/* Put long word to the stream. */
/*
 * stream_putl：写入一个 32 位长字（4 字节大端）并推进 endp。
 * 返回写入的字节数（4）。空间不足时按 allow_expansion 决定扩容或告警断言。
 */
int stream_putl(struct stream *s, uint32_t l)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < sizeof(uint32_t)) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, sizeof(uint32_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	s->data[s->endp++] = (uint8_t)(l >> 24);
	s->data[s->endp++] = (uint8_t)(l >> 16);
	s->data[s->endp++] = (uint8_t)(l >> 8);
	s->data[s->endp++] = (uint8_t)l;

	return 4;
}

/* Put quad word to the stream. */
/*
 * stream_putq：写入一个 64 位四字（8 字节大端）并推进 endp。
 * 返回写入的字节数（8）。空间不足时按 allow_expansion 决定扩容或告警断言。
 */
int stream_putq(struct stream *s, uint64_t q)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < sizeof(uint64_t)) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, sizeof(uint64_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	s->data[s->endp++] = (uint8_t)(q >> 56);
	s->data[s->endp++] = (uint8_t)(q >> 48);
	s->data[s->endp++] = (uint8_t)(q >> 40);
	s->data[s->endp++] = (uint8_t)(q >> 32);
	s->data[s->endp++] = (uint8_t)(q >> 24);
	s->data[s->endp++] = (uint8_t)(q >> 16);
	s->data[s->endp++] = (uint8_t)(q >> 8);
	s->data[s->endp++] = (uint8_t)q;

	return 8;
}

/*
 * stream_putf：写入一个 32 位单精度浮点。实现把 float 放进 union 的整数成员取出
 * 位模式，再交给 stream_putl 写出。返回写入的字节数（4）。
 */
int stream_putf(struct stream *s, float f)
{
	union {
		float i;
		uint32_t o;
	} u;
	u.i = f;
	return stream_putl(s, u.o);
}

/*
 * stream_putd：写入一个 64 位双精度浮点。实现把 double 放进 union 的整数成员取出
 * 位模式，再交给 stream_putq 写出。返回写入的字节数（8）。
 */
int stream_putd(struct stream *s, double d)
{
	union {
		double i;
		uint64_t o;
	} u;
	u.i = d;
	return stream_putq(s, u.o);
}

/*
 * stream_putc_at：在绝对偏移 putp 处原地改写一个字节，不移动 endp。
 * 越界（PUT_AT_VALID 失败）时告警断言并返回 0。返回写入的字节数（1）。
 */
int stream_putc_at(struct stream *s, size_t putp, uint8_t c)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + sizeof(uint8_t))) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}

	s->data[putp] = c;

	return 1;
}

/*
 * stream_putw_at：在绝对偏移 putp 处原地改写一个 16 位字（高字节在前），不改 endp。
 * 越界时告警断言并返回 0。返回写入的字节数（2）。
 */
int stream_putw_at(struct stream *s, size_t putp, uint16_t w)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + sizeof(uint16_t))) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}

	s->data[putp] = (uint8_t)(w >> 8);
	s->data[putp + 1] = (uint8_t)w;

	return 2;
}

/*
 * stream_put3_at：在绝对偏移 putp 处原地改写一个 24 位大端整数，不改 endp。
 * 越界时告警断言并返回 0。返回写入的字节数（3）。
 */
int stream_put3_at(struct stream *s, size_t putp, uint32_t l)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + 3)) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}
	s->data[putp] = (uint8_t)(l >> 16);
	s->data[putp + 1] = (uint8_t)(l >> 8);
	s->data[putp + 2] = (uint8_t)l;

	return 3;
}

/*
 * stream_putl_at：在绝对偏移 putp 处原地改写一个 32 位长字（大端），不改 endp。
 * 越界时告警断言并返回 0。返回写入的字节数（4）。常用于回填长度或校验和。
 */
int stream_putl_at(struct stream *s, size_t putp, uint32_t l)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + sizeof(uint32_t))) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}
	s->data[putp] = (uint8_t)(l >> 24);
	s->data[putp + 1] = (uint8_t)(l >> 16);
	s->data[putp + 2] = (uint8_t)(l >> 8);
	s->data[putp + 3] = (uint8_t)l;

	return 4;
}

/*
 * stream_putq_at：在绝对偏移 putp 处原地改写一个 64 位四字（大端），不改 endp。
 * 越界时告警断言并返回 0。返回写入的字节数（8）。
 */
int stream_putq_at(struct stream *s, size_t putp, uint64_t q)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + sizeof(uint64_t))) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}
	s->data[putp] = (uint8_t)(q >> 56);
	s->data[putp + 1] = (uint8_t)(q >> 48);
	s->data[putp + 2] = (uint8_t)(q >> 40);
	s->data[putp + 3] = (uint8_t)(q >> 32);
	s->data[putp + 4] = (uint8_t)(q >> 24);
	s->data[putp + 5] = (uint8_t)(q >> 16);
	s->data[putp + 6] = (uint8_t)(q >> 8);
	s->data[putp + 7] = (uint8_t)q;

	return 8;
}

/* Put long word to the stream. */
/*
 * stream_put_ipv4：把 4 字节 IPv4 地址（按内存原样 memcpy）写到 endp 处并推进
 * endp，不做端序转换。成功返回写入的字节数（4）。
 * 易踩的坑：空间不足时（无论是否可扩容）本函数最终都返回 0——可扩容分支在扩容后
 * 也直接 return 0，并不真正写入，因此调用前应确保有 4 字节空间。
 */
int stream_put_ipv4(struct stream *s, uint32_t l)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < sizeof(uint32_t)) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, sizeof(uint32_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}

		return 0;
	}
	memcpy(s->data + s->endp, &l, sizeof(uint32_t));
	s->endp += sizeof(uint32_t);

	return sizeof(uint32_t);
}

/* Put long word to the stream. */
/*
 * stream_put_in_addr：把 in_addr 的 4 字节按内存原样写到 endp 处并推进 endp。
 * 参数 addr 为地址结构。返回写入的字节数（4）。空间不足时按 allow_expansion 决定
 * 扩容或告警断言。
 */
int stream_put_in_addr(struct stream *s, const struct in_addr *addr)
{
	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < sizeof(uint32_t)) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, sizeof(uint32_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	memcpy(s->data + s->endp, addr, sizeof(uint32_t));
	s->endp += sizeof(uint32_t);

	return sizeof(uint32_t);
}

/*
 * stream_put_ipaddr：写入一个 IP 地址。先写 16 位地址族，再按族写地址：IPv4 用
 * stream_put_in_addr 写 4 字节，IPv6 用 stream_write 写 16 字节；IPADDR_NONE 记
 * 错误并返回 false。成功返回 true。IPv6 分支的 16 字节为硬编码常量。
 */
bool stream_put_ipaddr(struct stream *s, const struct ipaddr *ip)
{
	stream_putw(s, ip->ipa_type);

	switch (ip->ipa_type) {
	case IPADDR_V4:
		stream_put_in_addr(s, &ip->ipaddr_v4);
		break;
	case IPADDR_V6:
		stream_write(s, (uint8_t *)&ip->ipaddr_v6, 16);
		break;
	case IPADDR_NONE:
		flog_err(EC_LIB_DEVELOPMENT,
			 "%s: unknown ip address-family: %u", __func__,
			 ip->ipa_type);
		return false;
	}

	return true;
}

/* Put in_addr at location in the stream. */
/*
 * stream_put_in_addr_at：在绝对偏移 putp 处原地改写 4 字节 in_addr，不改 endp。
 * 越界时告警断言并返回 0。返回写入的字节数（4）。
 */
int stream_put_in_addr_at(struct stream *s, size_t putp,
			  const struct in_addr *addr)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + 4)) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}

	memcpy(&s->data[putp], addr, 4);
	return 4;
}

/* Put in6_addr at location in the stream. */
/*
 * stream_put_in6_addr_at：在绝对偏移 putp 处原地改写 16 字节 IPv6 地址，不改 endp。
 * 越界时告警断言并返回 0。返回写入的字节数（16）。
 */
int stream_put_in6_addr_at(struct stream *s, size_t putp,
			   const struct in6_addr *addr)
{
	STREAM_VERIFY_SANE(s);

	if (!PUT_AT_VALID(s, putp + 16)) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}

	memcpy(&s->data[putp], addr, 16);
	return 16;
}

/*
 * stream_put_prefix：写入一个前缀。先写 1 字节前缀长度 prefixlen，再写 psize
 * （由 PSIZE 按长度算出的字节数）个前缀地址字节并推进 endp。
 * 参数 p 为前缀结构。返回写入的字节数（psize）。
 * 易踩的坑：返回值只计前缀地址字节，不含那 1 字节长度。
 */
int stream_put_prefix(struct stream *s, const struct prefix *p)
{
	size_t psize;

	STREAM_VERIFY_SANE(s);

	psize = PSIZE(p->prefixlen);

	if (STREAM_WRITEABLE(s) < (psize + sizeof(uint8_t))) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, psize + sizeof(uint8_t)));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	s->data[s->endp++] = p->prefixlen;
	memcpy(s->data + s->endp, &p->u.prefix, psize);
	s->endp += psize;

	return psize;
}

/* Read size from fd. */
/*
 * stream_read：用 readn 从描述符读取 size 字节追加到 endp 处并推进 endp。
 * 参数 fd 为描述符，size 为字节数。返回读到的字节数（可 0 或负），只有大于 0 时
 * 才推进 endp。已过时，面向阻塞 I/O，建议改用 stream_read_try。
 * 易踩的坑：可写空间不足时告警断言并返回 0，不执行读取。
 */
int stream_read(struct stream *s, int fd, size_t size)
{
	int nbytes;

	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < size) {
		STREAM_BOUND_WARN(s, "put");
		return 0;
	}

	nbytes = readn(fd, s->data + s->endp, size);

	if (nbytes > 0)
		s->endp += nbytes;

	return nbytes;
}

/*
 * stream_read_try：面向非阻塞描述符的读取，最多读 size 字节追加到 endp 处。
 * 返回大于 0 为读到的字节数，0 为 EOF，-1 为致命错误，-2 为可重试的临时错误
 * （EAGAIN 或 EINTR）。空间不足视为致命错误返回 -1。
 */
ssize_t stream_read_try(struct stream *s, int fd, size_t size)
{
	ssize_t nbytes;

	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < size) {
		STREAM_BOUND_WARN(s, "put");
		/* Fatal (not transient) error, since retrying will not help
		   (stream is too small to contain the desired data). */
		return -1;
	}

	nbytes = read(fd, s->data + s->endp, size);
	if (nbytes >= 0) {
		s->endp += nbytes;
		return nbytes;
	}
	/* Error: was it transient (return -2) or fatal (return -1)? */
	if (ERRNO_IO_RETRY(errno))
		return -2;
	flog_err(EC_LIB_SOCKET, "%s: read failed on fd %d: %s", __func__, fd,
		 safe_strerror(errno));
	return -1;
}

/* Read up to size bytes into the stream from the fd, using recvmsgfrom
 * whose arguments match the remaining arguments to this function
 */
/*
 * stream_recvfrom：用 recvfrom 接收数据追加到 endp 处，并可回填来源地址。
 * 参数 fd、size、flags、from、fromlen 对应 recvfrom 的相应参数。返回接收字节数，
 * 0 为 EOF，-1 为致命错误，-2 为可重试的临时错误。空间不足返回 -1。
 */
ssize_t stream_recvfrom(struct stream *s, int fd, size_t size, int flags,
			struct sockaddr *from, socklen_t *fromlen)
{
	ssize_t nbytes;

	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < size) {
		STREAM_BOUND_WARN(s, "put");
		/* Fatal (not transient) error, since retrying will not help
		   (stream is too small to contain the desired data). */
		return -1;
	}

	nbytes = recvfrom(fd, s->data + s->endp, size, flags, from, fromlen);
	if (nbytes >= 0) {
		s->endp += nbytes;
		return nbytes;
	}
	/* Error: was it transient (return -2) or fatal (return -1)? */
	if (ERRNO_IO_RETRY(errno))
		return -2;
	flog_err(EC_LIB_SOCKET, "%s: read failed on fd %d: %s", __func__, fd,
		 safe_strerror(errno));
	return -1;
}

/* Read up to smaller of size or SIZE_REMAIN() bytes to the stream, starting
 * from endp.
 * First iovec will be used to receive the data.
 * Stream need not be empty.
 */
/*
 * stream_recvmsg：用 recvmsg 接收数据，把流当前的写入位置当作首个 iovec 的缓冲区。
 * 参数 msgh 必须至少包含一个 iovec（否则断言失败），size 为期望接收量。返回接收
 * 字节数，读到大于 0 时推进 endp。空间不足返回 -1。
 * 易踩的坑：会覆盖调用方 msgh 第 0 个 iovec 的 iov_base 与 iov_len。
 */
ssize_t stream_recvmsg(struct stream *s, int fd, struct msghdr *msgh, int flags,
		       size_t size)
{
	int nbytes;
	struct iovec *iov;

	STREAM_VERIFY_SANE(s);
	assert(msgh->msg_iovlen > 0);

	if (STREAM_WRITEABLE(s) < size) {
		STREAM_BOUND_WARN(s, "put");
		/* This is a logic error in the calling code: the stream is too
		   small
		   to hold the desired data! */
		return -1;
	}

	iov = &(msgh->msg_iov[0]);
	iov->iov_base = (s->data + s->endp);
	iov->iov_len = size;

	nbytes = recvmsg(fd, msgh, flags);

	if (nbytes > 0)
		s->endp += nbytes;

	return nbytes;
}

/* Write data to buffer. */
/*
 * stream_write：把 ptr 处的 size 字节追加到 endp 处并推进 endp（纯入队到流，不涉及
 * 描述符；名字沿用历史）。返回写入的字节数（size）。空间不足时按 allow_expansion
 * 决定扩容或告警断言。函数开头同样调用过时宏 CHECK_SIZE。
 */
size_t stream_write(struct stream *s, const void *ptr, size_t size)
{

	CHECK_SIZE(s, size);

	STREAM_VERIFY_SANE(s);

	if (STREAM_WRITEABLE(s) < size) {
		if (s->allow_expansion) {
			stream_expand(s, STREAM_EXPAND_SIZE(s, size));
		} else {
			STREAM_BOUND_WARN(s, "put");
			return 0;
		}
	}

	memcpy(s->data + s->endp, ptr, size);
	s->endp += size;

	return size;
}

/* Return current read pointer.
 * DEPRECATED!
 * Use stream_get_pnt_to if you must, but decoding streams properly
 * is preferred
 */
/*
 * stream_pnt：返回指向当前读取位置（data 加 getp）的裸指针。已过时。
 * 易踩的坑：可扩容的 stream 扩容后该指针会失效；解析优先用 stream_get 系列。
 */
uint8_t *stream_pnt(struct stream *s)
{
	STREAM_VERIFY_SANE(s);
	return s->data + s->getp;
}

/* Check does this stream empty? */
/*
 * stream_empty：判断流是否为空。参数 s 为源流。
 * 返回 1 表示 endp 为 0（从未写入任何数据），否则返回 0。
 * 易踩的坑：它看的是 endp 是否为 0，而不是 getp 是否追上 endp；即便数据已全部读空
 * （getp 等于 endp），只要 endp 不为 0 仍返回 0。
 */
int stream_empty(struct stream *s)
{
	STREAM_VERIFY_SANE(s);

	return (s->endp == 0);
}

/* Reset stream. */
/*
 * stream_reset：把 getp 与 endp 归零，使流貌似为空以便复用。不清零 data。
 */
void stream_reset(struct stream *s)
{
	STREAM_VERIFY_SANE(s);

	s->getp = s->endp = 0;
}

/* Write stream contens to the file discriptor. */
/*
 * stream_flush：把 [getp, endp) 之间的数据用一次 write 写到描述符 fd。
 * 返回实际写出的字节数，出错返回负值。
 * 易踩的坑：它不推进 getp，也不处理部分写；调用方需根据返回值自行推进游标。
 */
int stream_flush(struct stream *s, int fd)
{
	int nbytes;

	STREAM_VERIFY_SANE(s);

	nbytes = write(fd, s->data + s->getp, s->endp - s->getp);

	return nbytes;
}

/*
 * stream_hexdump：把流中 [0, endp) 的字节以十六进制形式打印到日志，供排错定位。
 */
void stream_hexdump(const struct stream *s)
{
	zlog_hexdump(s->data, s->endp);
}

/* Stream first in first out queue. */

/*
 * stream_fifo_new：分配一个 stream_fifo 并调用 stream_fifo_init 初始化。
 * 无参数；返回新建的 fifo，XMALLOC 失败会终止进程。无 _safe 变体。
 */
struct stream_fifo *stream_fifo_new(void)
{
	struct stream_fifo *new;

	new = XMALLOC(MTYPE_STREAM_FIFO, sizeof(struct stream_fifo));
	stream_fifo_init(new);
	return new;
}

/*
 * stream_fifo_init：就地初始化 fifo：把结构体内存清零并初始化互斥锁。
 * 参数 fifo 为调用方提供的存储（通常是栈上对象）。需用 stream_fifo_deinit 成对
 * 反初始化。无返回值。
 */
void stream_fifo_init(struct stream_fifo *fifo)
{
	memset(fifo, 0, sizeof(struct stream_fifo));
	pthread_mutex_init(&fifo->mtx, NULL);
}

/* Add new stream to fifo. */
/*
 * stream_fifo_push：把 s 接到队列尾部，把 count 原子加一并按需更新 max_count 峰值。
 * 参数 fifo 为目标队列，s 为待入队的流。无返回值，也不加锁（调用方需自行同步，
 * 或改用 stream_fifo_push_safe）。
 * 易踩的坑：会强行把 s 的 next 置为 NULL，从而复用该字段作链表指针。
 */
void stream_fifo_push(struct stream_fifo *fifo, struct stream *s)
{
	size_t max, curmax;

	if (fifo->tail)
		fifo->tail->next = s;
	else
		fifo->head = s;

	fifo->tail = s;
	fifo->tail->next = NULL;
	max = atomic_fetch_add_explicit(&fifo->count, 1, memory_order_release);
	curmax = atomic_load_explicit(&fifo->max_count, memory_order_relaxed);
	if (max > curmax)
		atomic_store_explicit(&fifo->max_count, max,
				      memory_order_relaxed);
}

/*
 * stream_fifo_push_safe：在持有 fifo 互斥锁的情况下调用 stream_fifo_push，保证与
 * 其它 _safe 操作互斥。
 */
void stream_fifo_push_safe(struct stream_fifo *fifo, struct stream *s)
{
	frr_with_mutex (&fifo->mtx) {
		stream_fifo_push(fifo, s);
	}
}

/* Delete first stream from fifo. */
/*
 * stream_fifo_pop：从队列头部取出一个流并把它摘除，count 原子减一，并清空其 next。
 * 参数 fifo 为源队列。返回取出的流，队列为空时返回空指针。不加锁。
 */
struct stream *stream_fifo_pop(struct stream_fifo *fifo)
{
	struct stream *s;

	s = fifo->head;

	if (s) {
		fifo->head = s->next;

		if (fifo->head == NULL)
			fifo->tail = NULL;

		atomic_fetch_sub_explicit(&fifo->count, 1,
					  memory_order_release);

		/* ensure stream is scrubbed of references to this fifo */
		s->next = NULL;
	}

	return s;
}

/*
 * stream_fifo_pop_safe：加锁版本，在互斥锁保护下调用 stream_fifo_pop 并返回其结果。
 */
struct stream *stream_fifo_pop_safe(struct stream_fifo *fifo)
{
	struct stream *ret;

	frr_with_mutex (&fifo->mtx) {
		ret = stream_fifo_pop(fifo);
	}

	return ret;
}

/*
 * stream_fifo_head：返回队列头部元素但不摘除（不加锁）。为空返回空指针。
 */
struct stream *stream_fifo_head(struct stream_fifo *fifo)
{
	return fifo->head;
}

/*
 * stream_fifo_head_safe：加锁版本，在互斥锁保护下调用 stream_fifo_head。
 */
struct stream *stream_fifo_head_safe(struct stream_fifo *fifo)
{
	struct stream *ret;

	frr_with_mutex (&fifo->mtx) {
		ret = stream_fifo_head(fifo);
	}

	return ret;
}

/*
 * stream_fifo_clean：遍历队列，逐个 stream_free 释放所有流；随后把 head 与 tail
 * 置空，并把 count 原子置 0。参数 fifo 为目标队列。不加锁。
 */
void stream_fifo_clean(struct stream_fifo *fifo)
{
	struct stream *s;
	struct stream *next;

	for (s = fifo->head; s; s = next) {
		next = s->next;
		stream_free(s);
	}
	fifo->head = fifo->tail = NULL;
	atomic_store_explicit(&fifo->count, 0, memory_order_release);
}

/*
 * stream_fifo_clean_safe：加锁版本，在互斥锁保护下调用 stream_fifo_clean。
 */
void stream_fifo_clean_safe(struct stream_fifo *fifo)
{
	frr_with_mutex (&fifo->mtx) {
		stream_fifo_clean(fifo);
	}
}

/*
 * stream_fifo_count_safe：以原子加载返回当前队列条数 count（acquire 语义）。
 */
size_t stream_fifo_count_safe(struct stream_fifo *fifo)
{
	return atomic_load_explicit(&fifo->count, memory_order_acquire);
}

/*
 * stream_fifo_deinit：先 stream_fifo_clean 释放所有流，再销毁互斥锁。
 * 参数 fifo 为待反初始化的队列；不释放 fifo 本身（它由调用方持有）。
 */
void stream_fifo_deinit(struct stream_fifo *fifo)
{
	stream_fifo_clean(fifo);
	pthread_mutex_destroy(&fifo->mtx);
}

/*
 * stream_fifo_free：先 stream_fifo_deinit 反初始化，再释放 fifo 结构体内存。
 */
void stream_fifo_free(struct stream_fifo *fifo)
{
	stream_fifo_deinit(fifo);
	XFREE(MTYPE_STREAM_FIFO, fifo);
}

/*
 * stream_pulldown：把尚未消费的数据（区间 [getp, endp)）memmove 到缓冲区开头，
 * 令 getp 归零、endp 等于剩余长度，从而腾出尾部空间继续读入。
 * 参数 s 为目标流。无返回值。
 * 易踩的坑：若无剩余数据则等价于一次 stream_reset；搬移会改变数据偏移，之前保存
 * 的裸指针或相对偏移需按新布局调整。
 */
void stream_pulldown(struct stream *s)
{
	size_t rlen = STREAM_READABLE(s);

	/* No more data, so just move the pointers. */
	if (rlen == 0) {
		stream_reset(s);
		return;
	}

	/* Move the available data to the beginning. */
	memmove(s->data, &s->data[s->getp], rlen);
	s->getp = 0;
	s->endp = rlen;
}
