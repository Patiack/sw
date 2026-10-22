// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP RD definitions for BGP-based VPNs (IP/EVPN)
 * -- brought over from bgpd/bgp_mplsvpn.c
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

/* ============================================================================
 * 【文件导读】bgp_rd.c —— RD(Route Distinguisher) 的编解码与字符串互转
 * ============================================================================
 * 【本文件解决什么问题】VPN 路由的 NLRI 前 8 字节是 RD，用来区分不同 VRF 里
 * 可能相同的业务前缀（详见 bgp_rd.h 的导读）。本文件提供：
 *   - 线格式(8 字节) 与 struct prefix_rd 之间的解码/编码；
 *   - RD 字符串（"65001:100"、"10.0.0.1:5"）与结构体之间的互转；
 *   - `rd auto` 的自动生成；
 *   - 供 printfrr 使用的 %pRDP / %pRDD / %pRDE 打印扩展。
 *
 * 【三种线格式，最易错的地方】前 2 字节是 type：
 *   type 0 (RD_TYPE_AS)  : 2 字节 AS  + 4 字节本地编号
 *   type 1 (RD_TYPE_IP)  : 4 字节 IPv4 + 2 字节本地编号
 *   type 2 (RD_TYPE_AS4) : 4 字节 AS  + 2 字节本地编号（为 4 字节 AS 而设）
 * 三者同为 8 字节但字段宽度不同，所以必须按 type 分派不同的 decode/encode——
 * 这就是 decode_rd_as / decode_rd_as4 / decode_rd_ip 三个函数并存的原因。
 *
 * 【★ 读实现要注意入参 pnt 的位置】decode_rd_as / decode_rd_as4 / decode_rd_ip
 * 收到的 pnt **不包含前 2 字节的 type**：调用方通常先 decode_rd_type 取类型，
 * 再把指针后移 2 字节传进来（即 pnt+2）。而 decode_rd_vnc_eth 传的是 pnt（从类型
 * 字节起），内部按 VNC 布局解释。这个偏移差异是读写本文件最容易错位之处。
 *
 * 【阅读建议】decode_rd_type（定长 2 字节）-> decode_rd_as/as4/ip（按类型解字段）
 *            -> str2prefix_rd（字符串 -> 结构）-> prefix_rd2str（结构 -> 字符串）
 *            -> form_auto_rd（rd auto）-> 末尾的 printfrr 扩展注册。
 * ============================================================================
 */

#include <zebra.h>
#include "command.h"
#include "log.h"
#include "prefix.h"
#include "memory.h"
#include "stream.h"
#include "filter.h"
#include "frrstr.h"

#include "lib/printfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_rd.h"
#include "bgpd/bgp_attr.h"

#ifdef ENABLE_BGP_VNC
#include "bgpd/rfapi/rfapi_backend.h"
#endif

/* 【函数】decode_rd_type —— 从线格式读出 RD 的 2 字节类型码，返回主机序。
 * pnt 指向 RD 起始（含 type 字节）。普通编译下直接读 2 字节大端；VNC 编译下
 * 若高字节已构成 RD_TYPE_VNC_ETH(0xff00)，则**只取高字节**——因为 VNC 的 L2 RD
 * 把 LHI 放在低字节，低字节不属于类型码。 */
uint16_t decode_rd_type(const uint8_t *pnt)
{
	uint16_t v;

	v = ((uint16_t)*pnt++ << 8);
#ifdef ENABLE_BGP_VNC
	/*
	 * VNC L2 stores LHI in lower byte, so omit it
	 */
	if (v != RD_TYPE_VNC_ETH)
		v |= (uint16_t)*pnt;
#else /* duplicate code for clarity */
	v |= (uint16_t)*pnt;
#endif
	return v;
}

/* 【函数】encode_rd_type —— 把主机序的类型码写成 2 字节大端线格式。
 * pnt 至少 2 字节。用 htons 转换后再逐字节写出。 */
void encode_rd_type(uint16_t v, uint8_t *pnt)
{
	uint16_t val = htons(v);

	pnt[0] = (val >> 8) & 0xff;
	pnt[1] = val & 0xff;
}

/* 【函数】decode_rd_as —— 解 type 0：先 2 字节 AS，再 4 字节本地编号(val)。
 * ★ pnt 应指向「AS 字段」起点，即调用方传的是 RD 起始 + 2（跳过 2 字节 type）。
 * 内部把 4 字节编号用 ptr_get_be32 解成主机序放入 rd_as->val。 */
/* type == RD_TYPE_AS */
void decode_rd_as(const uint8_t *pnt, struct rd_as *rd_as)
{
	rd_as->as = (uint16_t)*pnt++ << 8;
	rd_as->as |= (uint16_t)*pnt++;
	ptr_get_be32(pnt, &rd_as->val);
}

/* 【函数】decode_rd_as4 —— 解 type 2：先 4 字节 AS(ptr_get_be32)，再 2 字节编号。
 * ★ 与 decode_rd_as 的字段宽度正好相反：这里 AS 占 4 字节、编号占 2 字节，二者
 *   复用同一个 struct rd_as，请按本函数的语义理解 as/val 的位宽，勿与 type 0 混用。
 * pnt 同样指向「AS 字段」起点（调用方传 RD 起始 + 2）。 */
/* type == RD_TYPE_AS4 */
void decode_rd_as4(const uint8_t *pnt, struct rd_as *rd_as)
{
	pnt = ptr_get_be32(pnt, &rd_as->as);
	rd_as->val = ((uint16_t)*pnt++ << 8);
	rd_as->val |= (uint16_t)*pnt;
}

/* 【函数】decode_rd_ip —— 解 type 1：先 4 字节 IPv4 地址，再 2 字节编号(val)。
 * pnt 指向「IPv4 字段」起点（调用方传 RD 起始 + 2）。 */
/* type == RD_TYPE_IP */
void decode_rd_ip(const uint8_t *pnt, struct rd_ip *rd_ip)
{
	memcpy(&rd_ip->ip, pnt, 4);
	pnt += 4;

	rd_ip->val = ((uint16_t)*pnt++ << 8);
	rd_ip->val |= (uint16_t)*pnt;
}

#ifdef ENABLE_BGP_VNC
/* 【函数】decode_rd_vnc_eth —— 解 VNC 专用 RD：类型用常量写死，第 2 字节是本地
 * NVE 编号(local_nve_id)，其后 6 字节是以太网 MAC。
 * ★ 与上面三个不同，这里 pnt 指向**类型字节**（RD 起始），因为要读 pnt[1]。 */
/* type == RD_TYPE_VNC_ETH */
void decode_rd_vnc_eth(const uint8_t *pnt, struct rd_vnc_eth *rd_vnc_eth)
{
	rd_vnc_eth->type = RD_TYPE_VNC_ETH;
	rd_vnc_eth->local_nve_id = pnt[1];
	memcpy(rd_vnc_eth->macaddr.octet, pnt + 2, ETH_ALEN);
}
#endif

/* 【函数】str2prefix_rd —— 把配置里的 RD 字符串解析进 struct prefix_rd。
 * 支持的写法（按 ':' 切分；出现 2 个 ':' 时第一个 ':' 前可作为**显式 type**）：
 *   "65001:100"      -> 按数值大小自动选 type 0 或 type 2
 *   "10.0.0.1:5"     -> type 1
 *   "0:65001:100"    -> 强制 type 0
 *   "1:10.0.0.1:5"   -> 强制 type 1
 *   "2:65001:100"    -> 强制 type 2
 * 逻辑：先按 ':' 拆分；若存在第二个 ':'，前半段是显式 type，校验必须为 0 / 1 / 2；
 * 中间段作为 AS 或 IP。随后用 asn_str2asn / inet_aton 判断它是 AS 还是 IP：AS 时
 * 若数值 > UINT16_MAX 走 type 2，否则走 type 0；IP 时走 type 1。并在写流之后再校验
 * 显式 type 与实际推断是否一致，不一致则失败。
 * ★ 返回值：1 表示成功，0 表示失败（注意不是 bool 语义，别反用）。
 * 过程在临时 stream 上拼出 8 字节线格式，最后 memcpy 进 prd->val；任何一步失败都
 * goto out，统一释放临时内存并返回 0。
 * 疑点（已逐位推演核对）：两个 ':' 的分支执行 `p = p2 + 1;` 后 p 越过第二个 ':'，
 *   而后续 `all_digit(p + 1)` 与 `atol(p + 1)` 又各多前进一位，等于从值字段的
 *   **第 2 个字符**起取值。例："0:65001:100" 会把 "00" 当编号，得到 100 -> 0；
 *   "1:10.0.0.1:5" 更会取到空串。对照 else 分支（p 停在第一个 ':'，p+1 正好是值）
 *   可见 if 分支偏移多了一位。此处仅标注这一实现差异、不改代码；是否为历史遗留
 *   bug，请以实际需求 / 上游版本为准。 */
int str2prefix_rd(const char *str, struct prefix_rd *prd)
{
	int ret = 0, type = RD_TYPE_UNDEFINED;
	char *p, *p2;
	struct stream *s = NULL;
	char *half = NULL;
	struct in_addr addr;
	as_t as_val;

	prd->family = AF_UNSPEC;
	prd->prefixlen = 64;

	p = strchr(str, ':');
	if (!p)
		goto out;

	/* a second ':' is accepted */
	p2 = strchr(p + 1, ':');
	if (p2) {
		/* type is in first part */
		half = XMALLOC(MTYPE_TMP, (p - str) + 1);
		memcpy(half, str, (p - str));
		half[p - str] = '\0';
		type = atoi(half);
		if (type != RD_TYPE_AS && type != RD_TYPE_IP &&
		    type != RD_TYPE_AS4)
			goto out;
		XFREE(MTYPE_TMP, half);
		half = XMALLOC(MTYPE_TMP, (p2 - p));
		memcpy(half, p + 1, (p2 - p - 1));
		half[p2 - p - 1] = '\0';
		p = p2 + 1;
	} else {
		half = XMALLOC(MTYPE_TMP, (p - str) + 1);
		memcpy(half, str, (p - str));
		half[p - str] = '\0';
	}
	if (!all_digit(p + 1))
		goto out;

	s = stream_new(RD_BYTES);

	/* if it is an AS format or an IP */
	if (asn_str2asn(half, &as_val)) {
		if (as_val > UINT16_MAX) {
			stream_putw(s, RD_TYPE_AS4);
			stream_putl(s, as_val);
			stream_putw(s, atol(p + 1));
			if (type != RD_TYPE_UNDEFINED && type != RD_TYPE_AS4)
				goto out;
		} else {
			stream_putw(s, RD_TYPE_AS);
			stream_putw(s, as_val);
			stream_putl(s, atol(p + 1));
			if (type != RD_TYPE_UNDEFINED && type != RD_TYPE_AS)
				goto out;
		}
	} else if (inet_aton(half, &addr)) {
		stream_putw(s, RD_TYPE_IP);
		stream_put_in_addr(s, &addr);
		stream_putw(s, atol(p + 1));
		if (type != RD_TYPE_UNDEFINED && type != RD_TYPE_IP)
			goto out;
	} else
		goto out;
	memcpy(prd->val, s->data, 8);
	ret = 1;

out:
	if (s)
		stream_free(s);
	XFREE(MTYPE_TMP, half);
	return ret;
}

/* 【函数】prefix_rd2str —— 把 RD 结构按 type 解码并格式化成字符串写入 buf。
 * 先 decode_rd_type 判断类型，再分派 decode_rd_as/as4/ip 得到字段，最后用
 * snprintfrr 组合。type 0/2 用 ASN_FORMAT(asnotation) 打印 AS 号（按用户配置的
 * 点分/点分加/纯数字写法），再拼 ":val"；type 1 用 %pI4:val。VNC 类型（若编译）
 * 单独打印 "LHI:x, mac"。未知类型打印 "Unknown Type: N"。
 * 返回 buf。调用方需保证 size >= RD_ADDRSTRLEN（函数内有 assert 校验）。 */
char *prefix_rd2str(const struct prefix_rd *prd, char *buf, size_t size,
		    enum asnotation_mode asnotation)
{
	const uint8_t *pnt;
	uint16_t type;
	struct rd_as rd_as;
	struct rd_ip rd_ip;
	int len = 0;

	assert(size >= RD_ADDRSTRLEN);

	pnt = prd->val;

	type = decode_rd_type(pnt);

	if (type == RD_TYPE_AS) {
		decode_rd_as(pnt + 2, &rd_as);
		len += snprintfrr(buf + len, size - len, ASN_FORMAT(asnotation),
				  &rd_as.as);
		snprintfrr(buf + len, size - len, ":%u", rd_as.val);
		return buf;
	} else if (type == RD_TYPE_AS4) {
		decode_rd_as4(pnt + 2, &rd_as);
		len += snprintfrr(buf + len, size - len, ASN_FORMAT(asnotation),
				  &rd_as.as);
		snprintfrr(buf + len, size - len, ":%u", rd_as.val);
		return buf;
	} else if (type == RD_TYPE_IP) {
		decode_rd_ip(pnt + 2, &rd_ip);
		snprintfrr(buf, size, "%pI4:%hu", &rd_ip.ip, rd_ip.val);
		return buf;
	}
#ifdef ENABLE_BGP_VNC
	else if (type == RD_TYPE_VNC_ETH) {
		snprintf(buf, size, "LHI:%d, %02x:%02x:%02x:%02x:%02x:%02x",
			 *(pnt + 1), /* LHI */
			 *(pnt + 2), /* MAC[0] */
			 *(pnt + 3), *(pnt + 4), *(pnt + 5), *(pnt + 6),
			 *(pnt + 7));

		return buf;
	}
#endif

	snprintf(buf, size, "Unknown Type: %d", type);
	return buf;
}

/* 【函数】form_auto_rd —— 实现配置 `rd auto`：把 router_id 与本地编号拼成
 * "router_id:rd_id" 再走 str2prefix_rd，得到 type 1 形式的 RD（用路由器地址做
 * 前缀天然去重，避免多台设备手工配重）。填的是调用方传入的 prd，不返回新对象。
 * 注意：这里忽略 str2prefix_rd 的返回值，是按“输入一定合法”的假设写的。 */
void form_auto_rd(struct in_addr router_id,
		  uint16_t rd_id,
		  struct prefix_rd *prd)
{
	char buf[100];

	prd->family = AF_UNSPEC;
	prd->prefixlen = 64;
	snprintfrr(buf, sizeof(buf), "%pI4:%hu", &router_id, rd_id);
	(void)str2prefix_rd(buf, prd);
}

/* 【以下一组】FRR 的 printfrr 扩展：把自定义转换符 %pRDP / %pRDD / %pRDE 关联到
 * 打印函数，使 snprintfrr 能直接输出 RD。printfrr_prd_asnotation 是公共实现，按
 * asnotation 选 AS 显示写法（PLAIN/DOT/DOTPLUS）；其余三个只是用不同 asnotation
 * 调用它，并通过 printfrr_ext_autoreg_p 把符号注册进 lib 的扩展表。
 * ptr 为 NULL 时打印 "(null)"。调用者传的 ptr 应为 struct prefix_rd *。 */
static ssize_t printfrr_prd_asnotation(struct fbuf *buf,
				       struct printfrr_eargs *ea,
				       const void *ptr,
				       enum asnotation_mode asnotation)
{
	char rd_buf[RD_ADDRSTRLEN];

	if (!ptr)
		return bputs(buf, "(null)");

	prefix_rd2str(ptr, rd_buf, sizeof(rd_buf), asnotation);

	return bputs(buf, rd_buf);
}

/* %pRDP：纯数字 AS 写法（如 65001:100）。 */
printfrr_ext_autoreg_p("RDP", printfrr_prd);
static ssize_t printfrr_prd(struct fbuf *buf, struct printfrr_eargs *ea,
			    const void *ptr)
{
	return printfrr_prd_asnotation(buf, ea, ptr, ASNOTATION_PLAIN);
}

/* %pRDD：点分 AS 写法（如 1.10:100）。 */
printfrr_ext_autoreg_p("RDD", printfrr_prd_dot);
static ssize_t printfrr_prd_dot(struct fbuf *buf, struct printfrr_eargs *ea,
				const void *ptr)
{
	return printfrr_prd_asnotation(buf, ea, ptr, ASNOTATION_DOT);
}

/* %pRDE：点分加 AS 写法（如 1.10+:100）。 */
printfrr_ext_autoreg_p("RDE", printfrr_prd_dotplus);
static ssize_t printfrr_prd_dotplus(struct fbuf *buf, struct printfrr_eargs *ea,
				    const void *ptr)
{
	return printfrr_prd_asnotation(buf, ea, ptr, ASNOTATION_DOTPLUS);
}
