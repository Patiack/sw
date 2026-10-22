// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Traffic Control (TC) main library
 * Copyright (C) 2022  Shichu Yang
 */

/*
 * ============================================================================
 * 【文件导读】lib/tc.c —— 流量控制（Traffic Control）库实现
 * ============================================================================
 * 职责：
 *   实现 lib/tc.h 中声明的速率解析函数 tc_getrate，把用户在 VTY 里输入的
 *   人类可读速率字符串（如 "10Mbit"、"100MBps"、"1g"）转换成以比特每秒
 *   (bps) 为单位的整数值。这是 TC 相关命令解析的第一道工序。
 *
 * 核心数据结构：
 *   本文件不定义新结构，只使用 tc.h 里的类型；输入是字符串，输出是
 *   uint64_t 速率值。解析过程只依赖字符指针 endp / p 与若干局部变量。
 *
 * 与其他模块的关系：
 *   - 依赖 lib/tc.h（本模块自身头）与标准库 strtoull / strcmp。
 *   - 供 zebra 以及各协议的 TC 命令解析代码调用，把 rate / ceil 等
 *     配置项从文本转成数值。
 *
 * 阅读顺序：
 *   1. include。
 *   2. tc_getrate 一个函数读到底：先 strtoull 取数字部分，再逐字符解析
 *      单位后缀（k/K、m/M、g/G、t/T、i/I、Bps、bit）。
 *
 * 易踩的坑：
 *   - 单位后缀大小写不敏感（K 与 k 等价），但含义由 i 决定：
 *     带 i 表示 1024 进制，否则 1000 进制。
 *   - 默认单位是 bit；只有显式出现 "Bps" 才按字节处理，字节要除以 8。
 *   - 'i' 必须跟在数量级字母之后才有效，单独出现的 'i' 属非法输入，
 *     函数返回 -1。
 *   - 纯数字输入被视为 bps。
 * ============================================================================
 */

#include "tc.h"

/* tc_getrate：解析速率字符串到 bps。
 * 返回 0 表示成功并把结果写入 *rate；返回 -1 表示格式非法。
 * 流程：strtoull 取数字 -> 循环识别单位后缀 -> 按是否二进制基数换算
 * -> 若为字节单位再除 8。
 * 局部变量含义：
 *   endp        —— strtoull 之后指向数字末尾，即单位串起点。
 *   valid       —— 是否已识别出合法后缀。
 *   bytes       —— 是否按字节（Bps）而非比特计。
 *   binary_base —— 是否使用 1024 进制（出现 i 后缀）。
 *   power       —— 数量级（k=1、m=2、g=3、t=4）。
 */
int tc_getrate(const char *str, uint64_t *rate)
{
	char *endp;
	uint64_t raw = strtoull(str, &endp, 10);

	if (endp == str)
		return -1;

	/* 若数字后面直接是字符串结尾（无任何单位），视为合法的纯数字 bps。 */
	/* if the string only contains a number, it must be valid rate (bps) */
	bool valid = (*endp == '\0');

	const char *p = endp;
	bool bytes = false, binary_base = false;
	int power = 0;

	/* 逐字符解析单位后缀：数量级字母设置 power；'i' 把基数切换为 1024；
	 * "Bps" / "bit" 作为结束后缀；其余字符视为非法并返回 -1。 */
	while (*p) {
		if (strcmp(p, "Bps") == 0) {
			bytes = true;
			valid = true;
			break;
		} else if (strcmp(p, "bit") == 0) {
			valid = true;
			break;
		}
		switch (*p) {
		case 'k':
		case 'K':
			power = 1;
			break;
		case 'm':
		case 'M':
			power = 2;
			break;
		case 'g':
		case 'G':
			power = 3;
			break;
		case 't':
		case 'T':
			power = 4;
			break;
		case 'i':
		case 'I':
			if (power != 0)
				binary_base = true;
			else
				return -1;
			break;
		default:
			return -1;
		}
		p++;
	}

	if (!valid)
		return -1;

	/* 按数量级累乘：二进制基数用 1024，十进制基数用 1000。 */
	for (int i = 0; i < power; i++)
		raw *= binary_base ? 1024ULL : 1000ULL;

	/* 字节单位需换算为比特（除以 8）；否则 raw 本身就是比特速率。 */
	if (bytes)
		*rate = raw;
	else
		*rate = raw / 8ULL;

	return 0;
}
