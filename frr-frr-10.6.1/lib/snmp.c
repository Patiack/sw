// SPDX-License-Identifier: GPL-2.0-or-later
/* SNMP support
 * Copyright (C) 1999 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

/*
 * 【文件导读】snmp.c —— SNMP / SMUX 场景下的 OID 与地址 / 数值转换工具
 *
 * 一、职责
 *   为 net-snmp 子代理（subagent）提供一组把 OID 与常见数据类型互转的辅助函数，
 *   以及在 MIB 表 / 标量处理中通用的两个 header 匹配函数。OID 在 net-snmp 里是
 *   oid 元素组成的数组（每个元素通常是 32 位整数），本文件负责在这些数组与
 *   IPv4 / IPv6 地址、整数、字符串之间搬移字节。
 *
 * 二、核心数据类型
 *   oid                ：net-snmp 定义的类型，表示 OID 的单个分量。
 *   struct in_addr     ：IPv4 地址。
 *   struct in6_addr    ：IPv6 地址。
 *   本文件没有自定义结构体或全局状态，全部是无状态纯函数。
 *
 * 三、与其他模块的关系
 *   依赖 net-snmp 头文件与 smux.h；被各守护进程的 MIB 处理代码（表 / 标量回调）
 *   调用。smux_header_generic / smux_header_table 是 net-snmp 要求的 header 回调，
 *   返回 MATCH_SUCCEEDED / MATCH_FAILED 决定某个请求是否由本处理函数负责。
 *
 * 四、推荐阅读顺序
 *   先看 oid_compare（比较）与 oid_copy（拷贝）两个基础函数，再按"OID 到本地"
 *   （oid2in_addr、oid2in6_addr、oid2int、oid2string）和"本地到 OID"
 *   （oid_copy_in_addr、oid_copy_in6_addr、oid_copy_int、oid_copy_str）两条线读，
 *   最后看两个 smux_header_* 匹配函数。
 *
 * 五、易踩的坑
 *   1) 这些函数多为"按 len 逐字节拷贝"，调用方必须保证目标缓冲区足够大，函数本身
 *      不做边界检查。
 *   2) 字节序：oid2int / oid_copy_int 内部做网络字节序转换（ntohl / htonl）；而地址
 *      类函数是纯逐字节搬运，不含端序转换，别混用。
 *   3) oid_copy 的 size 参数是"oid 分量个数"而非字节数，函数内会再乘 sizeof(oid)。
 *   4) smux_header_generic 会改写 name / length 以回填完整实例，GETNEXT 依赖这一
 *      副作用；只读不改会破坏 MIB walk。
 */

#include <zebra.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "smux.h"

/*
 * 【函数】oid_compare(o1, o1_len, o2, o2_len)
 *   按字典序比较两个 OID：逐个分量比较，一旦某分量分出大小即返回；前
 *   MIN(o1_len, o2_len) 个分量全等时，较短者视为"更小"。
 *   参数：o1 / o1_len 第一个 OID 及其分量个数；o2 / o2_len 第二个。
 *   返回：o1 < o2 返回 -1，相等返回 0，o1 > o2 返回 1。
 */
int oid_compare(const oid *o1, int o1_len, const oid *o2, int o2_len)
{
	int i;

	for (i = 0; i < MIN(o1_len, o2_len); i++) {
		if (o1[i] < o2[i])
			return -1;
		else if (o1[i] > o2[i])
			return 1;
	}
	if (o1_len < o2_len)
		return -1;
	if (o1_len > o2_len)
		return 1;

	return 0;
}

/*
 * 【函数】oid_copy(dest, src, size)
 *   把 size 个 oid 分量从 src 拷贝到 dest。
 *   参数 size：oid 分量个数（不是字节数）；函数内部会乘 sizeof(oid) 再 memcpy。
 *   返回：dest。坑：dest 必须至少能容纳 size 个 oid；函数不做边界检查。
 */
void *oid_copy(void *dest, const void *src, size_t size)
{
	return memcpy(dest, src, size * sizeof(oid));
}

/*
 * 【函数】oid2in_addr(oid[], len, addr)
 *   把 len 个 OID 分量逐字节写入 IPv4 地址 addr；len == 0 时直接返回不写。
 *   参数 oid：源 OID 数组；len：写入字节数（通常为 4）；addr：目标 IPv4 地址。
 *   返回：无。坑：不检查 len 是否超过 4，len > 4 会越界写 addr。
 */
void oid2in_addr(oid oid[], int len, struct in_addr *addr)
{
	int i;
	uint8_t *pnt;

	if (len == 0)
		return;

	pnt = (uint8_t *)addr;

	for (i = 0; i < len; i++)
		*pnt++ = oid[i];
}

/*
 * 【函数】oid2in6_addr(oid[], addr)
 *   把 sizeof(struct in6_addr) 个 OID 分量逐字节写入 IPv6 地址 addr。
 *   参数 oid：源 OID 数组；addr：目标 IPv6 地址。返回：无。
 *   注意：拷贝长度固定为 16 字节，与来源 OID 的实际长度无关。
 */
void oid2in6_addr(oid oid[], struct in6_addr *addr)
{
	unsigned int i;
	uint8_t *pnt;

	pnt = (uint8_t *)addr;

	for (i = 0; i < sizeof(struct in6_addr); i++)
		*pnt++ = oid[i];
}

/*
 * 【函数】oid2int(oid[], dest)
 *   把 sizeof(int) 个 OID 分量按字节装进一个整数缓冲，再用 ntohl 转成本地字节序，
 *   写回 *dest。
 *   参数 oid：源 OID；dest：输出整数指针。返回：无。
 *   坑：OID 分量被视为网络（大端）字节序；若 OID 不是按大端排列，结果会不符预期。
 */
void oid2int(oid oid[], int *dest)
{
	uint8_t i;
	uint8_t *pnt;
	int network_dest;

	pnt = (uint8_t *)&network_dest;

	for (i = 0; i < sizeof(int); i++)
		*pnt++ = oid[i];
	*dest = ntohl(network_dest);
}

/*
 * 【函数】oid_copy_in_addr(oid[], addr)
 *   把 IPv4 地址 addr 逐字节写入 OID 数组（与 oid2in_addr 方向相反）。
 *   参数 oid：目标 OID 数组；addr：源 IPv4 地址。返回：无。
 *   写入长度固定为 sizeof(struct in_addr)（4）。
 */
void oid_copy_in_addr(oid oid[], const struct in_addr *addr)
{
	int i;
	const uint8_t *pnt;
	int len = sizeof(struct in_addr);

	pnt = (uint8_t *)addr;

	for (i = 0; i < len; i++)
		oid[i] = *pnt++;
}


/*
 * 【函数】oid_copy_in6_addr(oid[], addr)
 *   把 IPv6 地址 addr 逐字节写入 OID 数组。
 *   参数 oid：目标 OID 数组；addr：源 IPv6 地址。返回：无。
 *   写入长度固定为 sizeof(struct in6_addr)（16）。
 */
void oid_copy_in6_addr(oid oid[], const struct in6_addr *addr)
{
	int i;
	const uint8_t *pnt;
	int len = sizeof(struct in6_addr);

	pnt = (uint8_t *)addr;

	for (i = 0; i < len; i++)
		oid[i] = *pnt++;
}

/*
 * 【函数】oid_copy_int(oid[], val)
 *   把整数 *val 先转成网络字节序（htonl），再逐字节写入 OID 数组。
 *   参数 oid：目标 OID 数组；val：源整数指针。返回：无。
 *   写入长度固定为 sizeof(int)。与 oid2int 互为逆操作。
 */
void oid_copy_int(oid oid[], int *val)
{
	uint8_t i;
	const uint8_t *pnt;
	int network_val;

	network_val = htonl(*val);
	pnt = (uint8_t *)&network_val;

	for (i = 0; i < sizeof(int); i++)
		oid[i] = *pnt++;
}

/*
 * 【函数】oid2string(oid[], len, string)
 *   把 len 个 OID 分量逐字节写入字符串缓冲 string；len == 0 时直接返回。
 *   参数 oid：源 OID；len：写入字节数；string：目标缓冲。返回：无。
 *   坑：len 由调用方保证不超过目标缓冲大小；不保证以 0 结尾。
 */
void oid2string(oid oid[], int len, char *string)
{
	int i;
	uint8_t *pnt;

	if (len == 0)
		return;

	pnt = (uint8_t *)string;

	for (i = 0; i < len; i++)
		*pnt++ = (uint8_t)oid[i];
}

/*
 * 【函数】oid_copy_str(oid[], string, len)
 *   把字符串 string 的前 len 个字节逐字节写入 OID 数组（与 oid2string 相反）。
 *   参数 oid：目标 OID；string：源字符串；len：写入字节数。返回：无。
 *   len == 0 时直接返回。
 */
void oid_copy_str(oid oid[], const char *string, int len)
{
	int i;
	const uint8_t *pnt;

	if (len == 0)
		return;

	pnt = (uint8_t *)string;

	for (i = 0; i < len; i++)
		oid[i] = *pnt++;
}

/*
 * 【函数】smux_header_generic(v, name, length, exact, var_len, write_method)
 *   net-snmp 通用 header 回调，用于"标量对象"的 GET / GETNEXT 匹配。
 *   流程：把 v->name 复制成完整实例 fulloid（末尾补 0），与请求 name 比较；按 exact
 *         语义判定命中与否；GETNEXT 时把请求 name 回填为完整实例。
 *   参数：v 变量描述；name / length 请求 OID 及其长度（可能被写回）；exact 非 0 表示
 *         GET（精确匹配），0 表示 GETNEXT；var_len 返回数据长度；write_method 返回
 *         写回调（此处置 0，表示只读）。
 *   返回：MATCH_SUCCEEDED 命中；MATCH_FAILED 不命中。
 *   坑：会对 name / length 产生副作用（GETNEXT 必须回填，否则 walk 出错）；
 *       *var_len 默认填 sizeof(long)。
 */
int smux_header_generic(struct variable *v, oid *name, size_t *length,
			int exact, size_t *var_len, WriteMethod **write_method)
{
	oid fulloid[MAX_OID_LEN];
	int ret;

	oid_copy(fulloid, v->name, v->namelen);
	fulloid[v->namelen] = 0;
	/* Check against full instance. */
	ret = oid_compare(name, *length, fulloid, v->namelen + 1);

	/* Check single instance. */
	if ((exact && (ret != 0)) || (!exact && (ret >= 0)))
		return MATCH_FAILED;

	/* In case of getnext, fill in full instance. */
	memcpy(name, fulloid, (v->namelen + 1) * sizeof(oid));
	*length = v->namelen + 1;

	*write_method = 0;
	*var_len = sizeof(long); /* default to 'long' results */

	return MATCH_SUCCEEDED;
}

/*
 * 【函数】smux_header_table(v, name, length, exact, var_len, write_method)
 *   net-snmp 通用 header 回调，用于"表"对象：若请求 OID 小于本表前缀，则按 exact
 *   与否决定返回失败或把 name 提升到表前缀。
 *   参数含义同 smux_header_generic。
 *   返回：MATCH_SUCCEEDED / MATCH_FAILED。
 *   副作用：可能把 name 改写为 v->name 并同步 *length；write_method 置 0，
 *           var_len 置 sizeof(long)。
 */
int smux_header_table(struct variable *v, oid *name, size_t *length, int exact,
		      size_t *var_len, WriteMethod **write_method)
{
	/* If the requested OID name is less than OID prefix we
	   handle, adjust it to our prefix. */
	if ((oid_compare(name, *length, v->name, v->namelen)) < 0) {
		if (exact)
			return MATCH_FAILED;
		oid_copy(name, v->name, v->namelen);
		*length = v->namelen;
	}

	*write_method = 0;
	*var_len = sizeof(long);

	return MATCH_SUCCEEDED;
}
