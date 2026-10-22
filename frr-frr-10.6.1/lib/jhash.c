/* jhash.h: Jenkins hash support.
 *
 * Copyright (C) 1996 Bob Jenkins (bob_jenkins@burtleburtle.net)
 *
 * http://burtleburtle.net/bob/hash/
 *
 * These are the credits from Bob's sources:
 *
 * lookup2.c, by Bob Jenkins, December 1996, Public Domain.
 * hash(), hash2(), hash3, and mix() are externally useful functions.
 * Routines to test the hash are included if SELF_TEST is defined.
 * You can use this free for any purpose.  It has no warranty.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 *
 * I've modified Bob's hash to be useful in the Linux kernel, and
 * any bugs present are surely my fault.  -DaveM
 */
/*
 * 【文件导读】jhash.c —— Bob Jenkins 哈希(jhash)的可移植实现
 * =====================================================================
 * 【是什么】实现经典的 Jenkins lookup2 哈希(又称 jhash)，把任意字节串或
 *   若干个 32 位字混合成一个 32 位散列值。FRR 大量用它为哈希表(路由 / 邻居
 *   等散列桶)计算键值。算法由 Bob Jenkins 于 1996 年公开(Public Domain)，
 *   David S. Miller 将其移植成 Linux 内核风格，本文件沿用该移植。
 *
 * 【核心思想】维护 a、b、c 三个 32 位累加器，初值取“黄金比”常量
 *   0x9e3779b9 与调用方给的 initval；每处理一段输入就调用 __jhash_mix 做一轮
 *   可逆的加减异或混合，最后返回 c。混合函数的大量移位 / 异或使输入任何
 *   微小变化都会雪崩式扩散到结果的所有位。
 *
 * 【五个入口】
 *   - jhash  : 通用版，输入任意字节流(不对齐、不限长)。
 *   - jhash2 : 优化版，输入是 uint32_t 数组，长度以“字数”计。
 *   - jhash_3words / jhash_2words / jhash_1word : 针对固定 1~3 个字的
 *     “超优化”版本，省去尾部收尾混合。
 *
 * 【核心宏】
 *   - JHASH_GOLDEN_RATIO : 黄金比派生常量 0x9e3779b9，作累加器初值。
 *   - __jhash_mix(a,b,c) : 一轮 32 位混合宏，会就地修改三个入参。
 *   本文件没有结构体、枚举或全局变量，是纯函数式实现。
 *
 * 【与其他模块关系】声明在 jhash.h，被各协议的哈希表实现引用；本文件只依赖
 *   zebra.h。
 *
 * 【阅读顺序】两个宏 -> jhash -> jhash2 -> jhash_3words -> jhash_2words
 *   -> jhash_1word。
 *
 * 【易踩的坑】
 *   1) __jhash_mix 的入参是“按值传入后被就地修改”的宏，勿传入带副作用的
 *      表达式；
 *   2) jhash2 的长度单位是 uint32_t 的**个数**，而 jhash 的单位是**字节数**；
 *   3) 哈希不是密码学安全的，切勿用于安全场景。
 * =====================================================================
 */

#include "zebra.h"
#include "jhash.h"

/* 【宏】JHASH_GOLDEN_RATIO = 0x9e3779b9，取自黄金分割比乘以 2 的 32 次幂的
 *   近似值，是哈希 / 随机数里常用的“无意义但有良好混合性质”的常量。
 *   此处用作 a、b 两个累加器的初值。 */
/* The golden ration: an arbitrary value */
#define JHASH_GOLDEN_RATIO  0x9e3779b9

/* 【宏】__jhash_mix(a, b, c) —— 一轮 32 位混合变换(就地修改 a、b、c)。
 * 参数 a, b, c : 三个 32 位累加器(按值传入，宏体内部重写它们)。
 * 效果：对三者交替做“减法 + 异或 + 移位”，共若干轮(涉及的位移量如
 *   13 与 8、12 与 16、5 与 3、10 与 15)，使任意一位变化都能扩散到整个
 *   32 位空间，形成雪崩效应。
 * 【易踩的坑】因为宏会修改入参，传入的实参应是简单变量 / 临时量，勿用带
 *   副作用的表达式；源码注释亦提示 “Arguments are modified.”。 */
/* NOTE: Arguments are modified. */
#define __jhash_mix(a, b, c)                                                   \
	{                                                                      \
		a -= b;                                                        \
		a -= c;                                                        \
		a ^= (c >> 13);                                                \
		b -= c;                                                        \
		b -= a;                                                        \
		b ^= (a << 8);                                                 \
		c -= a;                                                        \
		c -= b;                                                        \
		c ^= (b >> 13);                                                \
		a -= b;                                                        \
		a -= c;                                                        \
		a ^= (c >> 12);                                                \
		b -= c;                                                        \
		b -= a;                                                        \
		b ^= (a << 16);                                                \
		c -= a;                                                        \
		c -= b;                                                        \
		c ^= (b >> 5);                                                 \
		a -= b;                                                        \
		a -= c;                                                        \
		a ^= (c >> 3);                                                 \
		b -= c;                                                        \
		b -= a;                                                        \
		b ^= (a << 10);                                                \
		c -= a;                                                        \
		c -= b;                                                        \
		c ^= (b >> 15);                                                \
	}

/* 【函数】通用 jhash：散列任意长度的字节序列。
 * 参数 key : 输入字节缓冲；length : 字节数；initval : 初始种子。
 * 返回：32 位散列值。
 * 流程：初值 a = b = JHASH_GOLDEN_RATIO、c = initval；先按每 12 字节一块地
 *   把数据小端拼进 a / b / c 并 __jhash_mix，循环处理完所有整块；再把不足
 *   12 字节的尾巴按长度用 fallthrough 逐字节并入(并先把 length 加到 c)；
 *   最后再做一次 __jhash_mix 并返回 c。
 * 【易踩的坑】不对输入做对齐假设；length 为 0 时也能给出确定结果。 */
/* The most generic version, hashes an arbitrary sequence
 * of bytes.  No alignment or length assumptions are made about
 * the input key.
 */
uint32_t jhash(const void *key, uint32_t length, uint32_t initval)
{
	uint32_t a, b, c, len;
	const uint8_t *k = key;

	len = length;
	a = b = JHASH_GOLDEN_RATIO;
	c = initval;

	while (len >= 12) {
		a += (k[0] + ((uint32_t)k[1] << 8) + ((uint32_t)k[2] << 16)
		      + ((uint32_t)k[3] << 24));
		b += (k[4] + ((uint32_t)k[5] << 8) + ((uint32_t)k[6] << 16)
		      + ((uint32_t)k[7] << 24));
		c += (k[8] + ((uint32_t)k[9] << 8) + ((uint32_t)k[10] << 16)
		      + ((uint32_t)k[11] << 24));

		__jhash_mix(a, b, c);

		k += 12;
		len -= 12;
	}

	c += length;
	switch (len) {
	case 11:
		c += ((uint32_t)k[10] << 24);
		fallthrough;
	case 10:
		c += ((uint32_t)k[9] << 16);
		fallthrough;
	case 9:
		c += ((uint32_t)k[8] << 8);
		fallthrough;
	case 8:
		b += ((uint32_t)k[7] << 24);
		fallthrough;
	case 7:
		b += ((uint32_t)k[6] << 16);
		fallthrough;
	case 6:
		b += ((uint32_t)k[5] << 8);
		fallthrough;
	case 5:
		b += k[4];
		fallthrough;
	case 4:
		a += ((uint32_t)k[3] << 24);
		fallthrough;
	case 3:
		a += ((uint32_t)k[2] << 16);
		fallthrough;
	case 2:
		a += ((uint32_t)k[1] << 8);
		fallthrough;
	case 1:
		a += k[0];
	}

	__jhash_mix(a, b, c);

	return c;
}

/* 【函数】针对 uint32_t 数组的优化版 jhash。
 * 参数 k : uint32_t 数组；length : 数组中**字的个数**(不是字节数)；
 *   initval : 种子。返回：32 位散列值。
 * 流程：每 3 个字一组累加进 a / b / c 并 __jhash_mix；收尾时把 length 乘以 4
 *   (换算成字节数)加到 c；不足 3 个字用 switch + fallthrough 补齐，再做一次
 *   __jhash_mix 后返回 c。
 * 【易踩的坑】千万别把字节长度传进来——那是 jhash 的约定。 */
/* A special optimized version that handles 1 or more of uint32_ts.
 * The length parameter here is the number of uint32_ts in the key.
 */
uint32_t jhash2(const uint32_t *k, uint32_t length, uint32_t initval)
{
	uint32_t a, b, c, len;

	a = b = JHASH_GOLDEN_RATIO;
	c = initval;
	len = length;

	while (len >= 3) {
		a += k[0];
		b += k[1];
		c += k[2];
		__jhash_mix(a, b, c);
		k += 3;
		len -= 3;
	}

	c += length * 4;

	switch (len) {
	case 2:
		b += k[1];
		fallthrough;
	case 1:
		a += k[0];
	}

	__jhash_mix(a, b, c);

	return c;
}


/* 【函数】超优化版：恰好散列 3 个字。
 * 参数 a, b, c : 三个待散列的字；initval : 种子。返回：32 位散列值。
 * 实现：a、b 先加 JHASH_GOLDEN_RATIO，c 加 initval，然后只做一次
 *   __jhash_mix 即返回 c。
 * 【易踩的坑】这里**省略**了通用版结尾的“先 c 加 length 再做 mix”收尾，
 *   专为已知正好 3 个字的场景加速；与其它入口的收尾语义并不完全同构。 */
/* A special ultra-optimized versions that knows they are hashing exactly
 * 3, 2 or 1 word(s).
 *
 * NOTE: In partilar the "c += length; __jhash_mix(a,b,c);" normally
 *       done at the end is not done here.
 */
uint32_t jhash_3words(uint32_t a, uint32_t b, uint32_t c, uint32_t initval)
{
	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += initval;

	__jhash_mix(a, b, c);

	return c;
}

/* 【函数】恰好散列 2 个字：直接转调 jhash_3words，并把第三个字置 0。 */
uint32_t jhash_2words(uint32_t a, uint32_t b, uint32_t initval)
{
	return jhash_3words(a, b, 0, initval);
}

/* 【函数】恰好散列 1 个字：转调 jhash_3words，并把第二、三个字都置 0。 */
uint32_t jhash_1word(uint32_t a, uint32_t initval)
{
	return jhash_3words(a, 0, 0, initval);
}
