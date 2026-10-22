// SPDX-License-Identifier: NONE
/*
 * This is part of the libb64 project, and has been placed in the public domain.
 * For details, see http://sourceforge.net/projects/libb64
 */

/*
 * 文件导读: base64.h
 * ----------------------------------------------------------------------------
 * 本文件是 FRR 中 Base64 编解码的接口声明, 代码源自 libb64 项目(公有领域).
 * 作用: 把二进制数据与 Base64 文本(A-Z a-z 0-9 加号 与 斜杠)互相转换;
 *       FRR 常用它把二进制字段(密钥片段, 摘要, 证书片段等)编成可打印字符串,
 *       放进配置行或 JSON 输出.
 *
 * 核心数据结构:
 *   - enum base64_encodestep 与 struct base64_encodestate: 编码状态机. 记录当前
 *     停在第几字节(从 step_A 开始)以及跨块遗留的半个字节 result.
 *   - enum base64_decodestep 与 struct base64_decodestate: 解码状态机. 记录当前
 *     停在 step_a 至 step_d 以及跨块遗留的部分字节 plainchar.
 *   状态机的意义: 支持流式(streaming)调用, 允许把一大段数据切成多次喂入,
 *   每次的输入长度不必是 3 字节的整数倍仍能保持一致的结果.
 *
 * 与其他模块关系: 仅依赖 compiler.h 提供的 fallthrough 宏, 不依赖 FRR 网络栈,
 *   可独立编译使用.
 *
 * 阅读顺序: 先读枚举与结构体理解状态含义, 再看 base64.c 中
 *   base64_init_encodestate -> base64_encode_block -> base64_encode_blockend
 *   的典型调用流程; 解码侧类似.
 *
 * 易踩的坑: 编码输出缓冲区需按 4 倍向上取整预留; 解码时字母表外的字符会被跳过;
 *   '=' 填充字符在解码表中映射为 -2, 是正常结束标志而不是错误.
 */

#ifndef _BASE64_H_
#define _BASE64_H_

/*
 * Base64 编码状态机的阶段枚举.
 * step_A 期望读入一个 3 字节组的第 1 字节, step_B 为第 2 字节, step_C 为第 3 字节.
 */
enum base64_encodestep {
	step_A, step_B, step_C
};

/*
 * Base64 编码状态.
 * step:   下一次调用 base64_encode_block 时将从哪个阶段继续.
 * result: 跨调用保留的半个字节(已移位的部分结果), 供下一块拼接.
 */
struct base64_encodestate {
	enum base64_encodestep step;
	char result;
};

/*
 * 初始化编码状态.
 * state_in: 待初始化的状态对象.
 * 阶段置为 step_A(从 3 字节组的第 1 字节开始), 残留结果置 0.
 */
void base64_init_encodestate(struct base64_encodestate *state_in);

/*
 * 把 6 位值映射为 Base64 字符.
 * value_in: 0 至 63 的值.
 * 返回: 对应字母表字符; 若 value_in 大于 63 则返回 '='(填充符).
 */
char base64_encode_value(char value_in);

/*
 * 编码一段明文到 Base64 文本.
 * 参数: plaintext_in 输入明文; length_in 输入字节数; code_out 输出缓冲;
 *       state_in 编码状态(可跨多次调用保持一致).
 * 返回: 本次写入 code_out 的字符个数.
 * 注意: 该函数可能因输入长度不是 3 的倍数而在中途返回, 末尾填充需再调用
 *       base64_encode_blockend.
 */
int base64_encode_block(const char *plaintext_in, int length_in, char *code_out,
			struct base64_encodestate *state_in);

/*
 * 结束编码: 根据状态补足末尾的 '=' 填充字符.
 * 返回本次写入 code_out 的字符个数.
 */
int base64_encode_blockend(char *code_out, struct base64_encodestate *state_in);


/*
 * Base64 解码状态机的阶段枚举, step_a 至 step_d 对应一个 4 字符组的 4 个字符.
 */
enum base64_decodestep {
	step_a, step_b, step_c, step_d
};

/*
 * Base64 解码状态.
 * step:     下一次调用 base64_decode_block 时将从哪个阶段继续.
 * plainchar: 跨调用保留的部分输出字节.
 */
struct base64_decodestate {
	enum base64_decodestep step;
	char plainchar;
};

/*
 * 初始化解码状态.
 * 阶段置为 step_a, 残留输出字节清零.
 */
void base64_init_decodestate(struct base64_decodestate *state_in);

/*
 * 把一个 Base64 字符映射回 0 至 63 的 6 位值, 并识别特殊字符.
 * 返回: 正常字符返回 0 至 63; 表外或需跳过的字符返回 -1;
 *       '=' 填充符返回 -2. 调用方通常以 返回值小于 0 来跳过.
 */
signed char base64_decode_value(signed char value_in);

/*
 * 解码一段 Base64 文本到明文.
 * 参数: code_in 输入文本; length_in 输入字符数; plaintext_out 输出缓冲;
 *       state_in 解码状态.
 * 返回: 本次写入 plaintext_out 的字节个数.
 * 注意: 内部会自行跳过非法字符, 因此输出长度通常小于输入长度的 3 分之 4.
 */
int base64_decode_block(const char *code_in, int length_in, char *plaintext_out,
			struct base64_decodestate *state_in);

#endif /* _BASE64_H_ */
