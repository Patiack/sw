// SPDX-License-Identifier: BSD-2-Clause
/*-
 * Copyright 2005,2007,2009 Colin Percival
 * All rights reserved.
 *
 * $FreeBSD: src/lib/libmd/sha256.h,v 1.2 2006/01/17 15:35:56 phk Exp $
 */

/*
 * 文件导读: sha256.h
 * ----------------------------------------------------------------------------
 * 本文件声明 FRR 使用的 SHA-256 摘要、HMAC-SHA256 与 PBKDF2-SHA256 接口,
 *   实现源自 Colin Percival(FreeBSD).
 * 作用: 提供 256 位 SHA-2 摘要、基于 SHA-256 的消息认证码(HMAC), 以及基于
 *   HMAC-SHA256 的密钥派生函数 PBKDF2.
 *
 * 核心数据结构:
 *   - SHA256_CTX: SHA-256 上下文, 含 8 个 32 位状态字 state, 2 个 32 位比特
 *     计数 count, 以及 64 字节分块缓冲 buf.
 *   - HMAC_SHA256_CTX: HMAC 上下文, 含内层 ictx 与 外层 octx 两个 SHA256_CTX.
 *
 * 与其他模块关系: 被需要强摘要或密钥派生的模块调用, 实现位于 sha256.c.
 *
 * 阅读顺序: 先看两个上下文结构, 再按 Init / Update / Final 顺序理解 SHA-256,
 *   然后看 HMAC 的两个函数, 最后看 PBKDF2_SHA256.
 *
 * 易踩的坑: digest 缓冲至少 32 字节; Final 会清空上下文; PBKDF2 的 dkLen 上限
 *   为 32 乘上(2 的 32 次方减 1).
 */

#ifndef _SHA256_H_
#define _SHA256_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SHA-256 运算上下文.
 * state: 8 个 32 位中间状态(初始为固定魔数);
 * count: 已处理的比特数(用 2 个 32 位表示 64 位);
 * buf: 待压缩的 64 字节分块缓冲.
 */
typedef struct SHA256Context {
	uint32_t state[8];
	uint32_t count[2];
	unsigned char buf[64];
} SHA256_CTX;

/*
 * HMAC-SHA256 上下文.
 * ictx: 内层 SHA-256 状态(处理 K 异或 ipad 与数据);
 * octx: 外层 SHA-256 状态(处理 K 异或 opad 与内层摘要).
 */
typedef struct HMAC_SHA256Context {
	SHA256_CTX ictx;
	SHA256_CTX octx;
} HMAC_SHA256_CTX;

/* 初始化 SHA-256 上下文: 计数清零并装载 8 个标准初始状态字. */
void SHA256_Init(SHA256_CTX *ctx);
/* 向 SHA-256 上下文追加数据(可多次调用), 满 64 字节即压缩. */
void SHA256_Update(SHA256_CTX *ctx, const void *in, size_t len);
/*
 * 结束 SHA-256 计算并输出摘要.
 * digest: 输出 32 字节; ctx: 上下文.
 * 副作用: 填充后取出摘要并清空上下文.
 */
void SHA256_Final(unsigned char digest[32], SHA256_CTX *ctx);
/*
 * 初始化 HMAC-SHA256.
 * ctx: HMAC 上下文; _K: 密钥; Klen: 密钥长度.
 * 说明: 密钥超过 64 字节时先摘要为 32 字节; 内层与外层分别吸收 K 异或 ipad /
 *   opad 的填充块.
 */
void HMAC__SHA256_Init(HMAC_SHA256_CTX *ctx, const void *_K, size_t Klen);
/* 向 HMAC-SHA256 追加数据(只喂给内层 SHA-256). */
void HMAC__SHA256_Update(HMAC_SHA256_CTX *ctx, const void *in, size_t len);
/*
 * 结束 HMAC-SHA256 并输出认证码.
 * digest: 输出 32 字节; ctx: 上下文.
 * 实现: 先取内层摘要, 再交给外层 SHA-256 完成得到最终结果.
 */
void HMAC__SHA256_Final(unsigned char digest[32], HMAC_SHA256_CTX *ctx);

/*
 * PBKDF2 密钥派生(使用 HMAC-SHA256 作为伪随机函数).
 * passwd / passwdlen: 口令及其长度; salt / saltlen: 盐及其长度; c: 迭代次数;
 * buf: 输出缓冲; dkLen: 期望派生字节数.
 * 说明: dkLen 上限为 32 乘上(2 的 32 次方减 1).
 */
/**
 * PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, c, buf, dkLen):
 * Compute PBKDF2(passwd, salt, c, dkLen) using HMAC-SHA256 as the PRF, and
 * write the output to buf.  The value dkLen must be at most 32 * (2^32 - 1).
 */
void PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen, const uint8_t *salt, size_t saltlen,
		   uint64_t c, uint8_t *buf, size_t dkLen);

#ifdef __cplusplus
}
#endif

#endif /* !_SHA256_H_ */
