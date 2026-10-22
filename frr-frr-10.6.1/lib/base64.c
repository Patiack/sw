// SPDX-License-Identifier: NONE
/*
 * This is part of the libb64 project, and has been placed in the public domain.
 * For details, see http://sourceforge.net/projects/libb64
 */

/*
 * 文件导读: base64.c
 * ----------------------------------------------------------------------------
 * 本文件实现 FRR 使用的 Base64 编解码逻辑, 源自 libb64 项目(公有领域).
 * 作用: 在二进制数据与 Base64 文本之间转换, 供其它模块把二进制内容编码成可
 *       打印字符串(例如放进入配置行或 JSON)以及反向还原.
 *
 * 核心数据结构: 编码侧用 struct base64_encodestate(阶段 与 残留字节 result),
 *   解码侧用 struct base64_decodestate(阶段 与 残留字节 plainchar). 二者都是
 *   状态机, 允许把长数据切成多块分次处理.
 *
 * 与其他模块关系: 仅依赖同目录 base64.h 提供类型声明, compiler.h 提供
 *   fallthrough 宏, 不触及 FRR 其它子系统, 属于自包含工具.
 *
 * 阅读顺序: 先看 ENCODING 字母表与 base64_encode_value 的映射, 再读
 *   base64_encode_block 的 3 字节组状态机; 解码侧先看 base64_decode_value 的
 *   反查表, 再看 base64_decode_block 的 4 字符组状态机.
 *
 * 易踩的坑: 输出缓冲要留足填充位; 解码表把 '=' 记为 -2(正常结束), 非法字符记为
 *   -1(需跳过); switch 内嵌套 while(1) 配合 fallthrough 实现跨状态连续处理,
 *   阅读时注意 fallthrough 的语义.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "base64.h"
#include "compiler.h"

/*
 * Base64 标准字母表(索引 0 至 63 对应的字符).
 * 顺序: 大写字母, 小写字母, 数字, 加号, 斜杠.
 */
static const char *ENCODING =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * 初始化编码状态.
 * state_in: 待初始化的状态对象.
 * 阶段置为 step_A(从 3 字节组的第 1 字节开始), 残留结果置 0.
 */
void base64_init_encodestate(struct base64_encodestate *state_in)
{
	state_in->step = step_A;
	state_in->result = 0;
}

/*
 * 把 6 位值映射为 Base64 字符.
 * value_in: 0 至 63 的值.
 * 返回: 对应字母表字符; 若 value_in 大于 63 则返回 '='(填充符).
 */
char base64_encode_value(char value_in)
{
	if (value_in > 63)
		return '=';
	return ENCODING[(int)value_in];
}

/*
 * 编码一块明文为 Base64.
 * plaintext_in: 输入明文起始地址; length_in: 输入字节数;
 * code_out: 输出缓冲; state_in: 编码状态(支持跨调用续编).
 * 返回: 写入 code_out 的字符数.
 * 副作用: 会更新 state_in 的阶段与残留字节; 若输入耗尽时正好处于组中间,
 *         则保留状态并返回, 由调用方稍后用 base64_encode_blockend 收尾.
 * 实现: switch 内嵌 while(1), 用 fallthrough 在 step_A/B/C 间顺序流转,
 *       每 3 个输入字节产出 4 个输出字符.
 */
int base64_encode_block(const char *plaintext_in, int length_in, char *code_out,
			struct base64_encodestate *state_in)
{
	const char *plainchar = plaintext_in;
	const char *const plaintextend = plaintext_in + length_in;
	char *codechar = code_out;
	char result;
	char fragment;

	result = state_in->result;

	switch (state_in->step) {
		while (1) {
			fallthrough;
			case step_A:
				if (plainchar == plaintextend) {
					state_in->result = result;
					state_in->step = step_A;
					return codechar - code_out;
				}
				fragment = *plainchar++;
				result = (fragment & 0x0fc) >> 2;
				*codechar++ = base64_encode_value(result);
				result = (fragment & 0x003) << 4;
				fallthrough;
			case step_B:
				if (plainchar == plaintextend) {
					state_in->result = result;
					state_in->step = step_B;
					return codechar - code_out;
				}
				fragment = *plainchar++;
				result |= (fragment & 0x0f0) >> 4;
				*codechar++ = base64_encode_value(result);
				result = (fragment & 0x00f) << 2;
				fallthrough;
			case step_C:
				if (plainchar == plaintextend) {
					state_in->result = result;
					state_in->step = step_C;
					return codechar - code_out;
				}
				fragment = *plainchar++;
				result |= (fragment & 0x0c0) >> 6;
				*codechar++ = base64_encode_value(result);
				result  = (fragment & 0x03f) >> 0;
				*codechar++ = base64_encode_value(result);
		}
	}
	/* control should not reach here */
	return codechar - code_out;
}

/*
 * 结束编码并补齐尾部填充.
 * code_out: 输出缓冲; state_in: 编码状态.
 * 返回: 写入的字符数(0 或 3 或 2).
 * 依据残留阶段决定补几个 '=': step_B 补两个, step_C 补一个, step_A 不补.
 */
int base64_encode_blockend(char *code_out, struct base64_encodestate *state_in)
{
	char *codechar = code_out;

	switch (state_in->step) {
	case step_B:
		*codechar++ = base64_encode_value(state_in->result);
		*codechar++ = '=';
		*codechar++ = '=';
		break;
	case step_C:
		*codechar++ = base64_encode_value(state_in->result);
		*codechar++ = '=';
		break;
	case step_A:
		break;
	}

	return codechar - code_out;
}


/*
 * 把 Base64 字符反查为 6 位值.
 * value_in: 输入字符.
 * 返回: 合法字符返回 0 至 63; '='(填充) 返回 -2; 其它非法字符返回 -1.
 * 实现: 用一张以字符减去 43(即 '+' 的 ASCII) 为下标的静态反查表.
 */
signed char base64_decode_value(signed char value_in)
{
	static const signed char decoding[] = {
		62, -1, -1, -1, 63, 52, 53, 54,
		55, 56, 57, 58, 59, 60, 61, -1,
		-1, -1, -2, -1, -1, -1, 0, 1,
		2,  3, 4, 5, 6, 7, 8, 9,
		10, 11, 12, 13, 14, 15, 16, 17,
		18, 19, 20, 21, 22, 23, 24, 25,
		-1, -1, -1, -1, -1, -1, 26, 27,
		28, 29, 30, 31, 32, 33, 34, 35,
		36, 37, 38, 39, 40, 41, 42, 43,
		44, 45, 46, 47, 48, 49, 50, 51
	};
	value_in -= 43;
	if (value_in < 0 || value_in >= 80)
		return -1;
	return decoding[(int)value_in];
}

/*
 * 初始化解码状态.
 * 阶段置为 step_a, 残留输出字节清零.
 */
void base64_init_decodestate(struct base64_decodestate *state_in)
{
	state_in->step = step_a;
	state_in->plainchar = 0;
}

/*
 * 解码一块 Base64 文本为明文.
 * code_in: 输入文本; length_in: 输入字符数; plaintext_out: 输出缓冲;
 * state_in: 解码状态(支持跨调用续解).
 * 返回: 写入 plaintext_out 的字节数.
 * 副作用: 更新 state_in 的阶段与残留字节.
 * 实现: switch 内嵌 while(1), 用 fallthrough 在 step_a 至 step_d 间流转,
 *       每个状态用 do 循环跳过非法字符(-1), 遇到 '='(-2, 小于 0)也一并跳过.
 */
int base64_decode_block(const char *code_in, int length_in, char *plaintext_out,
			struct base64_decodestate *state_in)
{
	const char *codec = code_in;
	char *plainc = plaintext_out;
	signed char fragmt;

	*plainc = state_in->plainchar;

	switch (state_in->step) {
		while (1) {
			fallthrough;
			case step_a:
				do {
					if (codec == code_in+length_in) {
						state_in->step = step_a;
						state_in->plainchar = *plainc;
						return plainc - plaintext_out;
					}
					fragmt = base64_decode_value(*codec++);
				} while (fragmt < 0);
				*plainc = (fragmt & 0x03f) << 2;
				fallthrough;
			case step_b:
				do {
					if (codec == code_in+length_in) {
						state_in->step = step_b;
						state_in->plainchar = *plainc;
						return plainc - plaintext_out;
					}
					fragmt = base64_decode_value(*codec++);
				} while (fragmt < 0);
				*plainc++ |= (fragmt & 0x030) >> 4;
				*plainc = (fragmt & 0x00f) << 4;
				fallthrough;
			case step_c:
				do {
					if (codec == code_in+length_in) {
						state_in->step = step_c;
						state_in->plainchar = *plainc;
						return plainc - plaintext_out;
					}
					fragmt = base64_decode_value(*codec++);
				} while (fragmt < 0);
				*plainc++ |= (fragmt & 0x03c) >> 2;
				*plainc = (fragmt & 0x003) << 6;
				fallthrough;
			case step_d:
				do {
					if (codec == code_in+length_in) {
						state_in->step = step_d;
						state_in->plainchar = *plainc;
						return plainc - plaintext_out;
					}
					fragmt = base64_decode_value(*codec++);
				} while (fragmt < 0);
				*plainc++   |= (fragmt & 0x03f);
		}
	}
	/* control should not reach here */
	return plainc - plaintext_out;
}
