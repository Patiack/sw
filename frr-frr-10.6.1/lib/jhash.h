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

#ifndef _QUAGGA_JHASH_H
#define _QUAGGA_JHASH_H

/* ============================================================================
 * 【文件导读】jhash.h —— Jenkins 哈希(俗称 jhash / lookup2)的声明
 * ============================================================================
 * 【来历】对照文件头的英文版权即可看到完整谱系：1996 年 Bob Jenkins 的
 * lookup2.c(公有领域)，2003 年 David S. Miller 改造成适合 Linux 内核的形态，
 * Quagga / FRR 又沿用至今，故保护宏仍叫 _QUAGGA_JHASH_H。
 *
 * 【为什么 FRR 要用它】路由协议里到处需要“把一段不定长数据压成一个 32 位散列
 * 值”：ECMP / LAG 的选路哈希、nexthop 去重、BGP 多路径计算都依赖它。它快、无
 * 表、无状态，而且同一份数据在任何平台都得到同一个值(不像有些库会掺入随机种子)。
 *
 * 【设计要点】核心循环是“一次吃 12 字节(3 个 32 位字)做一轮 mix”，最后对不足
 * 12 字节的尾巴做尾处理并再做一轮 mix；initval 是种子，让同一段数据在不同用途
 * 下得到不同散列。
 *
 * 【易踩的坑】① 散列值**不保证跨版本稳定**，绝不能落盘或跨进程当作唯一标识；
 * ② 它不是加密哈希，不要用于完整性校验或防碰撞场景。
 * ============================================================================
 */
/* 【本文件全部接口一览】五个函数是同一算法的不同入口，按数据形态选择。
 *   jhash(key, length, initval)
 *      最通用：任意字节序列，length 是**字节数**。内部做非对齐安全的读取，
 *      调用方无需保证对齐。
 *   jhash2(k, length, initval)
 *      针对 uint32_t 数组的特化版：k 必须 4 字节对齐，length 是**字的个数**
 *      而不是字节数——这是最容易搞错的一点，误传字节数会读越界。
 *   jhash_3words / jhash_2words / jhash_1word
 *      已知长度恰好为 3 / 2 / 1 个字的极致优化版。注意：它们**省略了通用版本
 *      末尾“c += length 后再 mix 一轮”这一步**(见英文注释的 NOTE)，因此同样的
 *      输入从这两个入口算出的值与 jhash 的结果**不相同**；同一用途内必须统一
 *      使用同一种入口，否则会得到两套散列值。
 * 【共同约定】initval 为种子；不同用途给不同种子可避免“同数据同散列”造成的
 * 意外耦合。返回值为 uint32_t。 */
#ifdef __cplusplus
extern "C" {
#endif

/* The most generic version, hashes an arbitrary sequence
 * of bytes.  No alignment or length assumptions are made about
 * the input key.
 */
extern uint32_t jhash(const void *key, uint32_t length, uint32_t initval);

/* A special optimized version that handles 1 or more of uint32_ts.
 * The length parameter here is the number of uint32_ts in the key.
 */
extern uint32_t jhash2(const uint32_t *k, uint32_t length, uint32_t initval);

/* A special ultra-optimized versions that knows they are hashing exactly
 * 3, 2 or 1 word(s).
 *
 * NOTE: In partilar the "c += length; __jhash_mix(a,b,c);" normally
 *       done at the end is not done here.
 */
extern uint32_t jhash_3words(uint32_t a, uint32_t b, uint32_t c,
			     uint32_t initval);
extern uint32_t jhash_2words(uint32_t a, uint32_t b, uint32_t initval);
extern uint32_t jhash_1word(uint32_t a, uint32_t initval);

#ifdef __cplusplus
}
#endif

#endif /* _QUAGGA_JHASH_H */
