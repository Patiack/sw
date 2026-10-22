// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (C) 2004 6WIND
 *                          <Vincent.Jardin@6WIND.com>
 * All rights reserved.
 *
 * This MD5 code is Big endian and Little Endian compatible.
 */

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 */

/*
 * 文件导读: md5.h
 * ----------------------------------------------------------------------------
 * 本文件声明 FRR 使用的 MD5 摘要算法与 HMAC-MD5 接口, 代码源自 6WIND 与
 *   WIDE 项目, 大小端兼容.
 * 作用: 计算 128 位 MD5 摘要, 并实现基于 MD5 的 HMAC(用于 BGP TCP-MD5 等
 *   报文认证场景).
 *
 * 核心数据结构: md5_ctxt 是 MD5 运算上下文, 内含:
 *   - md5_st: 4 个 32 位状态字(可用 4 个 uint32 或 16 个字节两种视角访问);
 *   - md5_count: 已处理的比特数(64 位, 也可用 8 字节数组访问);
 *   - md5_i: 当前 64 字节缓冲 md5_buf 中已填充的字节数;
 *   - md5_buf: 64 字节分块缓冲.
 *   文件内还用一批宏(md5_sta 至 md5_std, md5_st8, md5_n, md5_n8)给联合体成员
 *   起别名, 便于按字或按字节访问.
 *
 * 与其他模块关系: 提供 MD5_CTX / MD5Init / MD5Update / MD5Final 兼容宏供旧代码
 *   使用; 被 bgpd 等需要摘要或 HMAC 认证的模块调用, 实现位于 md5.c.
 *
 * 阅读顺序: 先看 md5_ctxt 与别名宏, 再看 init / loop / pad / result 四步流程,
 *   最后看 hmac_md5.
 *
 * 易踩的坑: MD5 分四步调用(init, loop 可多次, pad, result), 顺序不可乱; digest
 *   缓冲至少 16 字节; HMAC 的 key 超过 64 字节时会先被摘要成 16 字节.
 */

#ifndef _LIBZEBRA_MD5_H_
#define _LIBZEBRA_MD5_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MD5 处理的分块长度(字节): 算法一次压缩 64 字节.
 */
#define MD5_BUFLEN	64

/*
 * MD5 运算上下文.
 * md5_st 联合体同时暴露 4 个 32 位状态字与其 16 字节视图; md5_count 联合体同时
 * 暴露 64 位计数与其 8 字节视图; md5_i 记录缓冲区中已用字节数; md5_buf 为 64 字节
 * 分块缓冲. 结构内的一批 define 为这些联合体成员提供简写别名.
 */
typedef struct {
	union {
		uint32_t md5_state32[4];
		uint8_t md5_state8[16];
	} md5_st;

/*
 * 状态字别名: md5_sta / md5_stb / md5_stc / md5_std 分别引用 4 个 32 位状态字,
 * md5_st8 引用其 16 字节视图.
 */
#define md5_sta		md5_st.md5_state32[0]
#define md5_stb		md5_st.md5_state32[1]
#define md5_stc		md5_st.md5_state32[2]
#define md5_std		md5_st.md5_state32[3]
#define md5_st8		md5_st.md5_state8

	union {
		uint64_t md5_count64;
		uint8_t md5_count8[8];
	} md5_count;
#define md5_n	md5_count.md5_count64
#define md5_n8	md5_count.md5_count8

	uint md5_i;
	uint8_t md5_buf[MD5_BUFLEN];
} md5_ctxt;

/*
 * 初始化 MD5 上下文: 置计数与缓冲偏移为 0, 装载标准初值.
 */
extern void md5_init(md5_ctxt *ctxt);
/*
 * 向 MD5 上下文喂入一段数据(可多次调用).
 * ctxt: 上下文; vinput: 数据; len: 字节数.
 * 副作用: 更新计数与缓冲, 每满 64 字节压缩一次.
 */
extern void md5_loop(md5_ctxt *ctxt, const void *vinput, unsigned int len);
/*
 * 对 MD5 上下文做填充(加 1 位 与 长度), 为取摘要做准备.
 * 副作用: 会改变上下文状态, 应在最后一次 md5_loop 之后调用一次.
 */
extern void md5_pad(md5_ctxt *ctxt);
/*
 * 取出最终的 128 位 MD5 摘要.
 * digest: 输出缓冲, 至少 16 字节; ctxt: 已完成填充的上下文.
 */
extern void md5_result(uint8_t *digest, md5_ctxt *ctxt);

/*
 * 兼容宏: 供使用其它 MD5 实现命名习惯的旧代码直接改用本实现.
 * MD5_CTX 等价 md5_ctxt; MD5Init / MD5Update / MD5Final 分别包装
 * md5_init / md5_loop / (md5_pad 加 md5_result).
 */
/* compatibility */
#define MD5_CTX		md5_ctxt
#define MD5Init(x)	md5_init((x))
#define MD5Update(x, y, z)	md5_loop((x), (y), (z))
#define MD5Final(x, y)                                                         \
	do {                                                                   \
		md5_pad((y));                                                  \
		md5_result((x), (y));                                          \
	} while (0)

/*
 * 计算 HMAC-MD5(见 RFC 2104).
 * text / text_len: 消息及其长度; key / key_len: 密钥及其长度;
 * digest: 输出 16 字节摘要.
 * 说明: 若 key_len 大于 64, 先对 key 做 MD5 得到 16 字节再使用.
 */
/* From RFC 2104 */
void hmac_md5(unsigned char *text, int text_len, unsigned char *key,
	      int key_len, uint8_t *digest);

#ifdef __cplusplus
}
#endif

#endif /* ! _LIBZEBRA_MD5_H_*/
