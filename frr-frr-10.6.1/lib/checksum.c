// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Checksum routine for Internet Protocol family headers (C Version).
 *
 * Refer to "Computing the Internet Checksum" by R. Braden, D. Borman and
 * C. Partridge, Computer Communication Review, Vol. 19, No. 2, April 1989,
 * pp. 86-101, for additional details on computing this checksum.
 */

/*
 * 文件导读: checksum.c
 * ----------------------------------------------------------------------------
 * 本文件实现 Internet 协议族首部的校验和计算, 以及 Fletcher 校验和.
 * 作用: 提供 in_cksumv 对任意多段数据做 16 位反码求和, 供 IPv4 / IPv6 与
 *   TCP / UDP 报文构造与校验使用; 另提供 fletcher_checksum 用于 ISO 8473.
 *
 * 核心数据结构: union wordbuf 把两个字节按一个 16 位字处理, 用于跨数据段拼接
 *   最后可能剩下的奇数字节; struct iovec 描述输入数据段.
 *
 * 算法要点: 用 32 位累加器 sum 逐 16 位求和, 进位通过 add_carry 宏即时回卷,
 *   最后把高 16 位折回低 16 位并取反得到结果.
 *
 * 与其他模块关系: 实现 checksum.h 声明的接口; 被 zebra 及各协议模块调用.
 *
 * 阅读顺序: 先看 add_carry 宏, 再读 in_cksumv 的分段处理, 最后看
 *   fletcher_checksum 的取模与回写.
 *
 * 易踩的坑: 输入段长度任意, 奇数字节需借缓存跨段拼接; 计算校验和前校验和字段
 *   通常要先清零; 返回值的字节序处理由调用方负责.
 */

#include <zebra.h>
#include "checksum.h"

/*
 * 端回卷加法: 把 add 加到 dst, 并把产生的进位立刻回卷加到最低位.
 * dst: 累加器(16 / 32 位); add: 被加数.
 * 若 dst 加完后小于 _add, 说明发生了溢出(即产生了进位), 于是 dst 再自增 1.
 */
#define add_carry(dst, add)                                                    \
	do {                                                                   \
		typeof(dst) _add = (add);                                      \
		dst += _add;                                                   \
		if (dst < _add)                                                \
			dst++;                                                 \
	} while (0)

/*
 * 对多段数据计算 Internet 反码求和校验和.
 * iov / iov_len: 数据段数组及段数.
 * 返回: 16 位反码校验和.
 * 实现: 用 32 位 sum 逐段累加, 段内先按 8 字节(两个 32 位字)快进, 再补 16 位,
 *   末尾奇数字节借 wordbuf 保留并与下一段的字节拼成一个字; 最后折叠进位取反.
 * 注意: 段之间长度互不影响, 奇偶由 have_oddbyte 跨段衔接.
 */
uint16_t in_cksumv(const struct iovec *iov, size_t iov_len)
{
	const struct iovec *iov_end;
	uint32_t sum = 0;

	union {
		uint8_t bytes[2];
		uint16_t word;
	} wordbuf;
	bool have_oddbyte = false;

	/*
	 * Our algorithm is simple, using a 32-bit accumulator (sum),
	 * we add sequential 16-bit words to it, and at the end, fold back
	 * all the carry bits from the top 16 bits into the lower 16 bits.
	 */

	for (iov_end = iov + iov_len; iov < iov_end; iov++) {
		const uint8_t *ptr, *end;

		ptr = (const uint8_t *)iov->iov_base;
		end = ptr + iov->iov_len;
		if (ptr == end)
			continue;

		if (have_oddbyte) {
			have_oddbyte = false;
			wordbuf.bytes[1] = *ptr++;

			add_carry(sum, wordbuf.word);
		}

		while (ptr + 8 <= end) {
			add_carry(sum, *(const uint32_t *)(ptr + 0));
			add_carry(sum, *(const uint32_t *)(ptr + 4));
			ptr += 8;
		}

		while (ptr + 2 <= end) {
			add_carry(sum, *(const uint16_t *)ptr);
			ptr += 2;
		}

		if (ptr + 1 <= end) {
			wordbuf.bytes[0] = *ptr++;
			have_oddbyte = true;
		}
	}

	/* mop up an odd byte, if necessary */
	if (have_oddbyte) {
		wordbuf.bytes[1] = 0;
		add_carry(sum, wordbuf.word);
	}

	/*
	 * Add back carry outs from top 16 bits to low 16 bits.
	 */

	sum = (sum >> 16) + (sum & 0xffff); /* add high-16 to low-16 */
	sum += (sum >> 16);		    /* add carry */
	return ~sum;
}

/* Fletcher Checksum -- Refer to RFC1008. */
/*
 * Fletcher 校验和的分块模数. 每处理 MODX 字节做一次取模, 防止累加器溢出;
 * 注释中提到的 5802 也是常用取值.
 */
#define MODX                 4102U   /* 5802 should be fine */

/*
 * 计算或校验 Fletcher 校验和(RFC 1008).
 * buffer: 报文起始; len: 报文长度; offset: 校验和字段的 0 基偏移.
 * 返回: 计算模式返回校验和; 校验模式(offset 为 FLETCHER_CHECKSUM_VALIDATE)
 *       合法时返回 0.
 * 副作用: 计算模式会先清零校验和字段再写回结果.
 * 要点: 用 c0 / c1 两个累加器按 MODX 分块取模 255, 再按公式求 x / y 两个校验字节.
 */
/* To be consistent, offset is 0-based index, rather than the 1-based
   index required in the specification ISO 8473, Annex C.1 */
/* calling with offset == FLETCHER_CHECKSUM_VALIDATE will validate the checksum
   without modifying the buffer; a valid checksum returns 0 */
uint16_t fletcher_checksum(uint8_t *buffer, const size_t len,
			   const uint16_t offset)
{
	uint8_t *p;
	int x, y, c0, c1;
	uint16_t checksum = 0;
	uint16_t *csum;
	size_t partial_len, i, left = len;

	if (offset != FLETCHER_CHECKSUM_VALIDATE)
	/* Zero the csum in the packet. */
	{
		assert(offset
		       < (len - 1)); /* account for two bytes of checksum */
		csum = (uint16_t *)(buffer + offset);
		*(csum) = 0;
	}

	p = buffer;
	c0 = 0;
	c1 = 0;

	while (left != 0) {
		partial_len = MIN(left, MODX);

		for (i = 0; i < partial_len; i++) {
			c0 = c0 + *(p++);
			c1 += c0;
		}

		c0 = c0 % 255;
		c1 = c1 % 255;

		left -= partial_len;
	}

	/* The cast is important, to ensure the mod is taken as a signed value.
	 */
	x = (int)((len - offset - 1) * c0 - c1) % 255;

	if (x <= 0)
		x += 255;
	y = 510 - c0 - x;
	if (y > 255)
		y -= 255;

	if (offset == FLETCHER_CHECKSUM_VALIDATE) {
		checksum = (c1 << 8) + c0;
	} else {
		/*
		 * Now we write this to the packet.
		 * We could skip this step too, since the checksum returned
		 * would
		 * be stored into the checksum field by the caller.
		 */
		buffer[offset] = x;
		buffer[offset + 1] = y;

		/* Take care of the endian issue */
		checksum = htons((x << 8) | (y & 0xFF));
	}

	return checksum;
}
