// SPDX-License-Identifier: BSD-2-Clause
/*-
 * Copyright 2005,2007,2009 Colin Percival
 * All rights reserved.
 */

/*
 * 文件导读: sha256.c
 * ----------------------------------------------------------------------------
 * 本文件实现 SHA-256 摘要(RFC 6234)、HMAC-SHA256 与 PBKDF2-SHA256, 源自
 *   Colin Percival 的 FreeBSD 实现.
 * 作用: 提供 256 位 SHA-2 摘要, 以及基于它的消息认证码与密钥派生.
 *
 * 核心实现:
 *   - be32dec / be32enc 及其向量版本 be32dec_vect / be32enc_vect: 大端字节序
 *     与 32 位字之间的转换(平台未提供时自带实现).
 *   - 一批位运算宏(Ch, Maj, SHR, ROTR, S0, S1, s0, s1)与轮宏 RND / RNDr:
 *     描述 SHA-256 的消息扩展与压缩.
 *   - SHA256_Transform: 对 512 位分块做一次压缩, 是算法核心.
 *   - PAD: 填充常量; SHA256_Pad: 填充并写入比特长度.
 *
 * 与其他模块关系: 实现 sha256.h 的接口; 被需要强摘要或密钥派生的模块调用.
 *
 * 阅读顺序: 先看大端转换辅助函数, 再读位运算与轮宏, 然后看 SHA256_Transform,
 *   接着按 Init / Update / Final 理解对外接口, 最后看 HMAC 与 PBKDF2.
 *
 * 易踩的坑: 各处 explicit_bzero 用于擦除栈上敏感中间值; 更新比特计数时需区分
 *   低 32 位的进位; PBKDF2 内层 HMAC 复用 PShctx 以减少重复计算.
 */

#include <zebra.h>
#include "sha256.h"

/*
 * 大端字节序转 32 位字(平台未提供时使用).
 * pp 指向 4 个字节; 返回按大端解析出的 32 位值.
 */
#if !HAVE_DECL_BE32DEC
static inline uint32_t be32dec(const void *pp)
{
	const uint8_t *p = (uint8_t const *)pp;

	return ((uint32_t)(p[3]) + ((uint32_t)(p[2]) << 8)
		+ ((uint32_t)(p[1]) << 16) + ((uint32_t)(p[0]) << 24));
}
#endif

/*
 * 32 位字转大端字节序写出(平台未提供时使用).
 * pp 指向输出 4 字节; x 为待写值.
 */
#if !HAVE_DECL_BE32ENC
static inline void be32enc(void *pp, uint32_t x)
{
	uint8_t *p = (uint8_t *)pp;

	p[3] = x & 0xff;
	p[2] = (x >> 8) & 0xff;
	p[1] = (x >> 16) & 0xff;
	p[0] = (x >> 24) & 0xff;
}
#endif

/*
 * Encode a length len/4 vector of (uint32_t) into a length len vector of
 * (unsigned char) in big-endian form.  Assumes len is a multiple of 4.
 */
/*
 * 把 len 除以 4 个 32 位字按大端写成长度 len 的字节向量.
 * dst: 输出字节; src: 输入字数组; len: 字节数(须为 4 的倍数).
 */
static void be32enc_vect(unsigned char *dst, const uint32_t *src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++)
		be32enc(dst + i * 4, src[i]);
}

/*
 * Decode a big-endian length len vector of (unsigned char) into a length
 * len/4 vector of (uint32_t).  Assumes len is a multiple of 4.
 */
/*
 * 把大端字节向量解析成长度 len 除以 4 的 32 位字数组.
 * dst: 输出字数组; src: 输入字节; len: 字节数(须为 4 的倍数).
 */
static void be32dec_vect(uint32_t *dst, const unsigned char *src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++)
		dst[i] = be32dec(src + i * 4);
}

/* Elementary functions used by SHA256 */
/*
 * SHA-256 基本运算宏.
 * Ch 为选择函数, Maj 为多数函数, SHR 为逻辑右移, ROTR 为循环右移;
 * S0 与 S1 为轮函数所用的 Sigma 变换, s0 与 s1 为消息扩展所用的 sigma 变换.
 */
#define Ch(x, y, z)     ((x & (y ^ z)) ^ z)
#define Maj(x, y, z)    ((x & (y | z)) | (y & z))
#define SHR(x, n)       (x >> n)
#define ROTR(x, n)      ((x >> n) | (x << (32 - n)))
#define S0(x)           (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)           (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x)           (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define s1(x)           (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

/* SHA256 round function */
/*
 * SHA-256 轮函数宏.
 * RND: 单轮运算, 用消息字与轮常数 k 参与, 更新工作变量 d 与 h.
 * RNDr: 对状态数组 S 取旋转索引后的单轮运算, 供 64 轮依次调用.
 */
#define RND(a, b, c, d, e, f, g, h, k)                                         \
	t0 = h + S1(e) + Ch(e, f, g) + k;                                      \
	t1 = S0(a) + Maj(a, b, c);                                             \
	d += t0;                                                               \
	h = t0 + t1;

/* Adjusted round function for rotating state */
#define RNDr(S, W, i, k)                                                       \
	RND(S[(64 - i) % 8], S[(65 - i) % 8], S[(66 - i) % 8],                 \
	    S[(67 - i) % 8], S[(68 - i) % 8], S[(69 - i) % 8],                 \
	    S[(70 - i) % 8], S[(71 - i) % 8], W[i] + k)

/*
 * SHA256 block compression function.  The 256-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
/*
 * SHA-256 压缩函数.
 * state: 8 个 32 位状态字; block: 一个 64 字节分块.
 * 实现: 1) 用 be32dec_vect 与 s0 / s1 生成 64 字消息扩展 W;
 *   2) 把状态复制到工作变量 S; 3) 执行 64 轮 RNDr; 4) 把 S 累加回 state;
 *   5) 用 explicit_bzero 擦除栈上中间量.
 */
static void SHA256_Transform(uint32_t *state, const unsigned char block[64])
{
	uint32_t W[64];
	uint32_t S[8];
	uint32_t t0, t1;
	int i;

	/* 1. Prepare message schedule W. */
	be32dec_vect(W, block, 64);
	for (i = 16; i < 64; i++)
		W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];

	/* 2. Initialize working variables. */
	memcpy(S, state, 32);

	/* 3. Mix. */
	RNDr(S, W, 0, 0x428a2f98);
	RNDr(S, W, 1, 0x71374491);
	RNDr(S, W, 2, 0xb5c0fbcf);
	RNDr(S, W, 3, 0xe9b5dba5);
	RNDr(S, W, 4, 0x3956c25b);
	RNDr(S, W, 5, 0x59f111f1);
	RNDr(S, W, 6, 0x923f82a4);
	RNDr(S, W, 7, 0xab1c5ed5);
	RNDr(S, W, 8, 0xd807aa98);
	RNDr(S, W, 9, 0x12835b01);
	RNDr(S, W, 10, 0x243185be);
	RNDr(S, W, 11, 0x550c7dc3);
	RNDr(S, W, 12, 0x72be5d74);
	RNDr(S, W, 13, 0x80deb1fe);
	RNDr(S, W, 14, 0x9bdc06a7);
	RNDr(S, W, 15, 0xc19bf174);
	RNDr(S, W, 16, 0xe49b69c1);
	RNDr(S, W, 17, 0xefbe4786);
	RNDr(S, W, 18, 0x0fc19dc6);
	RNDr(S, W, 19, 0x240ca1cc);
	RNDr(S, W, 20, 0x2de92c6f);
	RNDr(S, W, 21, 0x4a7484aa);
	RNDr(S, W, 22, 0x5cb0a9dc);
	RNDr(S, W, 23, 0x76f988da);
	RNDr(S, W, 24, 0x983e5152);
	RNDr(S, W, 25, 0xa831c66d);
	RNDr(S, W, 26, 0xb00327c8);
	RNDr(S, W, 27, 0xbf597fc7);
	RNDr(S, W, 28, 0xc6e00bf3);
	RNDr(S, W, 29, 0xd5a79147);
	RNDr(S, W, 30, 0x06ca6351);
	RNDr(S, W, 31, 0x14292967);
	RNDr(S, W, 32, 0x27b70a85);
	RNDr(S, W, 33, 0x2e1b2138);
	RNDr(S, W, 34, 0x4d2c6dfc);
	RNDr(S, W, 35, 0x53380d13);
	RNDr(S, W, 36, 0x650a7354);
	RNDr(S, W, 37, 0x766a0abb);
	RNDr(S, W, 38, 0x81c2c92e);
	RNDr(S, W, 39, 0x92722c85);
	RNDr(S, W, 40, 0xa2bfe8a1);
	RNDr(S, W, 41, 0xa81a664b);
	RNDr(S, W, 42, 0xc24b8b70);
	RNDr(S, W, 43, 0xc76c51a3);
	RNDr(S, W, 44, 0xd192e819);
	RNDr(S, W, 45, 0xd6990624);
	RNDr(S, W, 46, 0xf40e3585);
	RNDr(S, W, 47, 0x106aa070);
	RNDr(S, W, 48, 0x19a4c116);
	RNDr(S, W, 49, 0x1e376c08);
	RNDr(S, W, 50, 0x2748774c);
	RNDr(S, W, 51, 0x34b0bcb5);
	RNDr(S, W, 52, 0x391c0cb3);
	RNDr(S, W, 53, 0x4ed8aa4a);
	RNDr(S, W, 54, 0x5b9cca4f);
	RNDr(S, W, 55, 0x682e6ff3);
	RNDr(S, W, 56, 0x748f82ee);
	RNDr(S, W, 57, 0x78a5636f);
	RNDr(S, W, 58, 0x84c87814);
	RNDr(S, W, 59, 0x8cc70208);
	RNDr(S, W, 60, 0x90befffa);
	RNDr(S, W, 61, 0xa4506ceb);
	RNDr(S, W, 62, 0xbef9a3f7);
	RNDr(S, W, 63, 0xc67178f2);

	/* 4. Mix local working variables into global state */
	for (i = 0; i < 8; i++)
		state[i] += S[i];

	/* Clean the stack. */
	explicit_bzero(W, 256);
	explicit_bzero(S, 32);
	explicit_bzero(&t0, sizeof(t0));
	explicit_bzero(&t1, sizeof(t0));
}

/*
 * 填充常量: 首字节 0x80, 其余全为 0. SHA256_Pad 时按需引用.
 */
static unsigned char PAD[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* Add padding and terminating bit-count. */
/*
 * 填充并写入终止的比特计数.
 * ctx: 上下文.
 * 步骤: 先把当前比特长度按大端编码成 8 字节; 再补 1 至 64 字节使长度满足
 *   56 模 64; 最后追加那 8 字节长度.
 */
static void SHA256_Pad(SHA256_CTX *ctx)
{
	unsigned char len[8];
	uint32_t r, plen;

	/*
	 * Convert length to a vector of bytes -- we do this now rather
	 * than later because the length will change after we pad.
	 */
	be32enc_vect(len, ctx->count, 8);

	/* Add 1--64 bytes so that the resulting length is 56 mod 64 */
	r = (ctx->count[1] >> 3) & 0x3f;
	plen = (r < 56) ? (56 - r) : (120 - r);
	SHA256_Update(ctx, PAD, (size_t)plen);

	/* Add the terminating bit-count */
	SHA256_Update(ctx, len, 8);
}

/* SHA-256 initialization.  Begins a SHA-256 operation. */
/*
 * 初始化 SHA-256 上下文.
 * ctx: 上下文.
 * 比特计数清零, 并装载 8 个标准初始状态字(0x6A09E667 等).
 */
void SHA256_Init(SHA256_CTX *ctx)
{

	/* Zero bits processed so far */
	ctx->count[0] = ctx->count[1] = 0;

	/* Magic initialization constants */
	ctx->state[0] = 0x6A09E667;
	ctx->state[1] = 0xBB67AE85;
	ctx->state[2] = 0x3C6EF372;
	ctx->state[3] = 0xA54FF53A;
	ctx->state[4] = 0x510E527F;
	ctx->state[5] = 0x9B05688C;
	ctx->state[6] = 0x1F83D9AB;
	ctx->state[7] = 0x5BE0CD19;
}

/* Add bytes into the hash */
/*
 * 向 SHA-256 上下文追加数据(可多次调用).
 * ctx: 上下文; in: 数据; len: 字节数.
 * 副作用: 更新 64 位比特计数; 先补齐当前分块, 再对整块循环压缩, 余下拷入缓冲.
 */
void SHA256_Update(SHA256_CTX *ctx, const void *in, size_t len)
{
	uint32_t bitlen[2];
	uint32_t r;
	const unsigned char *src = in;

	/* Number of bytes left in the buffer from previous updates */
	r = (ctx->count[1] >> 3) & 0x3f;

	/* Convert the length into a number of bits */
	bitlen[1] = ((uint32_t)len) << 3;
	bitlen[0] = (uint32_t)(len >> 29);

	/* Update number of bits */
	if ((ctx->count[1] += bitlen[1]) < bitlen[1])
		ctx->count[0]++;
	ctx->count[0] += bitlen[0];

	/* Handle the case where we don't need to perform any transforms */
	if (len < 64 - r) {
		memcpy(&ctx->buf[r], src, len);
		return;
	}

	/* Finish the current block */
	memcpy(&ctx->buf[r], src, 64 - r);
	SHA256_Transform(ctx->state, ctx->buf);
	src += 64 - r;
	len -= 64 - r;

	/* Perform complete blocks */
	while (len >= 64) {
		SHA256_Transform(ctx->state, src);
		src += 64;
		len -= 64;
	}

	/* Copy left over data into buffer */
	memcpy(ctx->buf, src, len);
}

/*
 * SHA-256 finalization.  Pads the input data, exports the hash value,
 * and clears the context state.
 */
/*
 * 结束 SHA-256 计算并输出摘要.
 * digest: 输出 32 字节; ctx: 上下文.
 * 步骤: 填充; 把 8 个状态字按大端写出为 32 字节摘要; 最后清空上下文.
 */
void SHA256_Final(unsigned char digest[32], SHA256_CTX *ctx)
{

	/* Add padding */
	SHA256_Pad(ctx);

	/* Write the hash */
	be32enc_vect(digest, ctx->state, 32);

	/* Clear the context state */
	explicit_bzero((void *)ctx, sizeof(*ctx));
}

/* Initialize an HMAC-SHA256 operation with the given key. */
/*
 * 初始化 HMAC-SHA256.
 * ctx: HMAC 上下文; _K: 密钥; Klen: 密钥长度.
 * 说明: 密钥超过 64 字节时先摘要为 32 字节; 内层吸收 K 异或 0x36 的填充块,
 *   外层吸收 K 异或 0x5c 的填充块; 结束时擦除栈上密钥散列.
 */
void HMAC__SHA256_Init(HMAC_SHA256_CTX *ctx, const void *_K, size_t Klen)
{
	unsigned char pad[64];
	unsigned char khash[32];
	const unsigned char *K = _K;
	size_t i;

	/* If Klen > 64, the key is really SHA256(K). */
	if (Klen > 64) {
		SHA256_Init(&ctx->ictx);
		SHA256_Update(&ctx->ictx, K, Klen);
		SHA256_Final(khash, &ctx->ictx);
		K = khash;
		Klen = 32;
	}

	/* Inner SHA256 operation is SHA256(K xor [block of 0x36] || data). */
	SHA256_Init(&ctx->ictx);
	memset(pad, 0x36, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	SHA256_Update(&ctx->ictx, pad, 64);

	/* Outer SHA256 operation is SHA256(K xor [block of 0x5c] || hash). */
	SHA256_Init(&ctx->octx);
	memset(pad, 0x5c, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	SHA256_Update(&ctx->octx, pad, 64);

	/* Clean the stack. */
	explicit_bzero(khash, 32);
}

/* Add bytes to the HMAC-SHA256 operation. */
/*
 * 向 HMAC-SHA256 追加数据.
 * 只喂给内层 SHA-256(ictx), 外层在 Final 时才吸收内层结果.
 */
void HMAC__SHA256_Update(HMAC_SHA256_CTX *ctx, const void *in, size_t len)
{

	/* Feed data to the inner SHA256 operation. */
	SHA256_Update(&ctx->ictx, in, len);
}

/* Finish an HMAC-SHA256 operation. */
/*
 * 结束 HMAC-SHA256 并输出认证码.
 * digest: 输出 32 字节; ctx: 上下文.
 * 实现: 取内层摘要后交给外层 SHA-256 完成, 结束时擦除栈上内层摘要.
 */
void HMAC__SHA256_Final(unsigned char digest[32], HMAC_SHA256_CTX *ctx)
{
	unsigned char ihash[32];

	/* Finish the inner SHA256 operation. */
	SHA256_Final(ihash, &ctx->ictx);

	/* Feed the inner hash to the outer SHA256 operation. */
	SHA256_Update(&ctx->octx, ihash, 32);

	/* Finish the outer SHA256 operation. */
	SHA256_Final(digest, &ctx->octx);

	/* Clean the stack. */
	explicit_bzero(ihash, 32);
}

/*
 * PBKDF2 密钥派生, 使用 HMAC-SHA256 作为伪随机函数.
 * passwd / passwdlen: 口令及长度; salt / saltlen: 盐及长度; c: 迭代次数;
 * buf: 输出缓冲; dkLen: 期望派生字节数.
 * 说明: 先对 HMAC(P, S) 状态做预计算并复用(PShctx)以减少重复; 每块做 c 次
 *   迭代异或; 结束时擦除 PShctx.
 */
/**
 * PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, c, buf, dkLen):
 * Compute PBKDF2(passwd, salt, c, dkLen) using HMAC-SHA256 as the PRF, and
 * write the output to buf.  The value dkLen must be at most 32 * (2^32 - 1).
 */
void PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen, const uint8_t *salt,
		   size_t saltlen, uint64_t c, uint8_t *buf, size_t dkLen)
{
	HMAC_SHA256_CTX PShctx = {}, hctx;
	size_t i;
	uint8_t ivec[4];
	uint8_t U[32];
	uint8_t T[32];
	uint64_t j;
	int k;
	size_t clen;

	/* Compute HMAC state after processing P and S. */
	HMAC__SHA256_Init(&PShctx, passwd, passwdlen);
	HMAC__SHA256_Update(&PShctx, salt, saltlen);

	/* Iterate through the blocks. */
	for (i = 0; i * 32 < dkLen; i++) {
		/* Generate INT(i + 1). */
		be32enc(ivec, (uint32_t)(i + 1));

		/* Compute U_1 = PRF(P, S || INT(i)). */
		memcpy(&hctx, &PShctx, sizeof(HMAC_SHA256_CTX));
		HMAC__SHA256_Update(&hctx, ivec, 4);
		HMAC__SHA256_Final(U, &hctx);

		/* T_i = U_1 ... */
		memcpy(T, U, 32);

		for (j = 2; j <= c; j++) {
			/* Compute U_j. */
			HMAC__SHA256_Init(&hctx, passwd, passwdlen);
			HMAC__SHA256_Update(&hctx, U, 32);
			HMAC__SHA256_Final(U, &hctx);

			/* ... xor U_j ... */
			for (k = 0; k < 32; k++)
				T[k] ^= U[k];
		}

		/* Copy as many bytes as necessary into buf. */
		clen = dkLen - i * 32;
		if (clen > 32)
			clen = 32;
		memcpy(&buf[i * 32], T, clen);
	}

	/* Clean PShctx, since we never called _Final on it. */
	explicit_bzero(&PShctx, sizeof(HMAC_SHA256_CTX));
}
