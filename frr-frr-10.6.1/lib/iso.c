// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ISO Network functions - iso_net.c
 *
 * Author: Olivier Dugeon <olivier.dugeon@orange.com>
 *
 * Copyright (C) 2023 Orange http://www.orange.com
 */
/*
 * 【文件导读】iso.c —— ISO(OSI / CLNS)地址类标识符的 printfrr 打印扩展
 * =====================================================================
 * 【是什么】本文件为 FRR 的 printfrr 框架注册 4 个“扩展转换符”，使日志 /
 *   show 命令能直接把 ISO(OSI)体系里的各类标识符打印成惯用的点分十六进制，
 *   而无需每个调用点各自手写格式化代码。
 *
 * 【ISO 地址体系速览(本文件涉及的四类)】
 *   - System ID      : 6 字节，形式 0000.0000.0000，唯一标识一个 IS。
 *   - Pseudo Node ID : 7 字节(前 6 字节 System ID 加 1 字节伪节点号)，
 *                      广播网段上的 DIS 常用它。
 *   - LSP 分片标识    : 8 字节(7 字节伪节点 ID 加 1 字节分片号)，
 *                      即 0000.0000.0000.00-00，用于定位一条 LSP 分片。
 *   - Network Address: 变长，由区域地址(area_addr，有效长度 addr_len)与
 *                      System ID 组成，打印为 00.0000.0000.0000 形式。
 *
 * 【注册机制】printfrr_ext_autoreg_p("XX", func) 是自动注册宏：在构造期把
 *   转换符 "%pXX" 与对应打印函数登记进 printfrr 扩展表，运行时按需调用。
 *   本文件注册的 4 个转换符是："SY"、"PN"、"LS"、"IS"。
 *
 * 【核心数据结构】struct iso_address(定义在 iso.h，本文件只读用)：内含
 *   area_addr[] 区域地址字节数组与 addr_len 有效长度两个字段。
 *
 * 【与其他模块关系】被 isisd 等使用 ISO 寻址的模块间接依赖；printfrr 框架
 *   声明在 printfrr.h。本文件不导出任何函数，全部是 static 打印器。
 *
 * 【阅读顺序】SY 打印器 -> PN -> LS -> IS(最后一个最复杂，含小写 l 长格式)。
 *
 * 【易踩的坑】
 *   1) 长格式靠格式串里的小写 "l" 修饰触发(如 %pISl)，见 IS 分支；
 *   2) 空指针一律打印 "(null)" 而非崩溃；
 *   3) bprintfrr 的返回值是“已写出字符数”，IS 里逐步累加后统一返回。
 * =====================================================================
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "compiler.h"

#include <string.h>
#include <ctype.h>
#include <time.h>

#include "printfrr.h"
#include "iso.h"

/* 【注册 + 函数】转换符 "%pSY"：把一个 6 字节 ISO System ID 打印为
 * 0000.0000.0000 形式(每 2 字节一组，组间点分)。
 * 参数 buf : 输出缓冲；ea : printfrr 扩展参数；vptr : 指向 6 字节 System ID。
 * 返回：已写出字符数；vptr 为 NULL 时打印 "(null)" 并返回其字符数。
 * 说明：printfrr_ext_autoreg_p 在构造期完成注册，名字串为 "SY"；
 *   函数本体用 bprintfrr 以 6 个 %02x 拼出点分格式。 */
/**
 * Print ISO System ID as 0000.0000.0000
 *
 * @param	Print buffer
 * @param	Print argument
 * @param	Pointer to the System ID to be printed
 *
 * @return	Number of printed characters
 */
printfrr_ext_autoreg_p("SY", printfrr_iso_sysid);
static ssize_t printfrr_iso_sysid(struct fbuf *buf, struct printfrr_eargs *ea,
				  const void *vptr)
{
	const uint8_t *id = vptr;

	if (!id)
		return bputs(buf, "(null)");

	return bprintfrr(buf, "%02x%02x.%02x%02x.%02x%02x",
			 id[0], id[1], id[2], id[3], id[4], id[5]);
}

/* 【注册 + 函数】转换符 "%pPN"：把 7 字节 ISO 伪节点 ID 打印为
 * 0000.0000.0000.00 形式(在 System ID 之后追加 1 字节伪节点号)。
 * 参数与返回语义同 printfrr_iso_sysid。
 * 说明：比 SY 多打印第 7 个字节 id[6]。 */
/**
 * Print ISO Pseudo Node system ID as 0000.0000.0000.00
 *
 * @param	Print buffer
 * @param	Print argument
 * @param	Pointer to the System ID to be printed
 *
 * @return	Number of printed characters
 */
printfrr_ext_autoreg_p("PN", printfrr_iso_pseudo);
static ssize_t printfrr_iso_pseudo(struct fbuf *buf, struct printfrr_eargs *ea,
				   const void *vptr)
{
	const uint8_t *id = vptr;

	if (!id)
		return bputs(buf, "(null)");

	return bprintfrr(buf, "%02x%02x.%02x%02x.%02x%02x.%02x",
			 id[0], id[1], id[2], id[3], id[4], id[5], id[6]);
}

/* 【注册 + 函数】转换符 "%pLS"：把 8 字节 LSP 分片标识打印为
 * 0000.0000.0000.00-00 形式(7 字节伪节点 ID，短横线后接 1 字节分片号)。
 * 参数与返回语义同 printfrr_iso_sysid。
 * 说明：分片号用 id[7] 并以 "-%02x" 分隔，读法上点分组后跟短横线分片。 */
/**
 * Print ISO LSP Fragment System ID as 0000.0000.0000.00-00
 *
 * @param	Print buffer
 * @param	Print argument
 * @param	Pointer to the System ID to be printed
 *
 * @return	Number of printed characters
 */
printfrr_ext_autoreg_p("LS", printfrr_iso_frag_id);
static ssize_t printfrr_iso_frag_id(struct fbuf *buf, struct printfrr_eargs *ea,
				    const void *vptr)
{
	const uint8_t *id = vptr;

	if (!id)
		return bputs(buf, "(null)");

	return bprintfrr(buf, "%02x%02x.%02x%02x.%02x%02x.%02x-%02x",
			 id[0], id[1], id[2], id[3], id[4], id[5], id[6],
			 id[7]);
}

/* 【注册 + 函数】转换符 "%pIS"：打印一个 ISO 网络地址(struct iso_address)。
 * 参数 buf / ea : 同前；vptr : 指向 struct iso_address。
 * 返回：已写出字符数；ia 为 NULL 时打印 "(null)"。
 * 长格式：若格式串里紧跟小写 'l'(如 "%pISl")，则先把 len 置 7
 *   (ISO System ID 的 6 字节加 1)，即把末尾的 System ID 一并打印出来，
 *   同时把 ea->fmt 前移一位以消费掉这个 'l'。
 * 点分规则：按字节遍历 area_addr[0..len)，偶数下标且未到末尾时输出后加点，
 *   (i & 1) 为真(奇数下标)或到达最后一个字节时不加点——从而形成
 *   00.0000.0000.0000 的“一个点、两个点”交替分隔风格。
 * 【易踩的坑】len = 7 表示“区域 0 字节 + System ID 6 字节”的封顶情形；
 *   'l' 修饰的识别只看 ea->fmt[0]，因此该修饰对本转换符是“就地消费”。 */
/**
 * Print ISO Network address as 00.0000.0000.0000 ... with the System ID
 * as 0000.0000.0000.00 when long 'l' option is added to '%pIS'
 *
 * @param	Print buffer
 * @param	Print argument
 * @param	Pointer to the ISO Network address
 *
 * @return	Number of printed characters
 */
printfrr_ext_autoreg_p("IS", printfrr_iso_addr);
static ssize_t printfrr_iso_addr(struct fbuf *buf, struct printfrr_eargs *ea,
				 const void *vptr)
{
	const struct iso_address *ia = vptr;
	uint8_t len = 0;
	int i = 0;
	ssize_t ret = 0;

	if (ea->fmt[0] == 'l') {
		len = 7; /* ISO SYSTEM ID + 1 */
		ea->fmt++;
	}

	if (!ia)
		return bputs(buf, "(null)");

	len += ia->addr_len;
	while (i < len) {
		/* No dot for odd index and at the end of address */
		if ((i & 1) || (i == (len - 1)))
			ret += bprintfrr(buf, "%02x", ia->area_addr[i]);
		else
			ret += bprintfrr(buf, "%02x.", ia->area_addr[i]);
		i++;
	}

	return ret;
}

