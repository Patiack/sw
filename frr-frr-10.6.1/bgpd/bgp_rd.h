// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP RD definitions for BGP-based VPNs (IP/EVPN)
 * -- brought over from bgpd/bgp_mplsvpn.h
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */
/* ============================================================================
 * 【文件导读】bgp_rd.h —— RD(Route Distinguisher，路由区分符)
 * ============================================================================
 * 【RD 是什么、为什么需要】VPN 里不同客户（VRF）可能存在**完全相同的前缀**
 * （都可能是 10.0.0.0/24）。为了让它们在同一个 MP-BGP 会话里被区分开，VPN 路由的
 * NLRI 前面拼上一个 8 字节的 **RD**，形成“RD + 前缀”的全局唯一标识。RD 只用于
 * **区分与承载**，本身不表达“谁能收”，决定收发关系的是 **RT(Route Target)**
 * （在 bgp_mplsvpn.h / bgp_ecommunity.h 侧）。二者常被混淆，务必分清：
 *   RD 回答“这是哪条路由”（身份）；RT 回答“这条路由该导入哪些 VRF”（策略）。
 *
 * 【三种 RD 编码格式，这是本文件的核心】8 字节 RD 的前 2 字节是 type：
 *   type 0 (RD_TYPE_AS)  : 2 字节 AS + 4 字节本地编号 —— 常用，但 AS 只有 2 字节，
 *                          4 字节 AS 的运营商放不下，所以才有 type 2。
 *   type 1 (RD_TYPE_IP)  : 4 字节 IPv4 地址 + 2 字节编号 —— 用路由器 loopback
 *                          (router-id) 当前缀，天然避重，是最推荐的写法。
 *   type 2 (RD_TYPE_AS4) : 4 字节 AS + 2 字节本地编号 —— 为 4 字节 AS 而生，
 *                          注意编号字段被**压缩**到 2 字节，可用空间比 type 0 少。
 * 三者长度都是 8 字节，但字段宽度不同，所以解码函数必须按 type 分派——这就是
 * 下面 decode_rd_as / decode_rd_as4 / decode_rd_ip 三个函数并存的原因，**别拿错**。
 * RD_TYPE_UNDEFINED(-1) 是“未设置”的哨兵，不是合法线格式值。
 *
 * 【字符串格式与 %pRD 家族】打印 RD 时要按用户配置的 asnotation 呈现 2 字节 AS
 * 的写法(点分 / 点分加 / 纯数字)，于是有 BGP_RD_AS_FORMAT 这组**带 mode 参数**的
 * 格式化宏——它们最终展开成 printf 的 %pRD 系列扩展（由 lib 里的 printfrr 实现）。
 * _SPACE 版本只是多了左侧宽度填充(%-21)，用于表格对齐。
 *
 * 【阅读建议】先记住三种 type 的字段布局，再看 decode_rd_* 与 str2prefix_rd /
 *            prefix_rd2str 如何互转；form_auto_rd 是配置 `rd auto` 时自动生成 RD。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_RD_H
#define _QUAGGA_BGP_RD_H

#include "asn.h"
#include "prefix.h"

/* RD types */
/* 【RD 类型码】线格式里占**前 2 字节**；-1 是进程内的“未设置”哨兵，不会出现在线上。
 * 0 / 1 / 2 的字段布局差异见文件头导读。 */
#define RD_TYPE_UNDEFINED (-1)
#define RD_TYPE_AS      0
#define RD_TYPE_IP      1
#define RD_TYPE_AS4     2

/* 【VNC 专用类型】仅当编译开启 ENABLE_BGP_VNC（虚拟网络控制的 L2VPN 场景）时存在。
 * 0xff00 明显超出常规 type 取值，属于厂商私有扩展，普通 VPN 里不会遇到。 */
#ifdef ENABLE_BGP_VNC
#define RD_TYPE_VNC_ETH	0xff00  /* VNC L2VPN */
#endif

/* 【长度】RD_BYTES=8 是线格式固定长度；RD_ADDRSTRLEN=28 是打印成字符串的缓冲
 * 上限——最长的写法形如 "65000:4294967295" 或带点分 AS 的形式，28 字节留了余量。
 * 调用 prefix_rd2str 时缓冲至少要给这么大，否则会被截断。 */
#define RD_ADDRSTRLEN  28
#define RD_BYTES  8

/* 【格式化宏】按 asnotation 参数在三种 %pRD 扩展之间选择：DOT=点分(1.10)、
 * DOTPLUS=点分加(1.10+)、其余为纯数字(266)。这些是 printfrr 的自定义转换符，
 * 由 lib 的 printf 扩展实现真正解析——**直接提供给 snprintf 用**，返回的是
 * 格式串而不是结果。SPACE 版本在左对齐宽度上占 21 列，用于表格对齐输出。 */
#define BGP_RD_AS_FORMAT(mode)                                                 \
	((mode == ASNOTATION_DOT)                                              \
		 ? "%pRDD"                                                     \
		 : ((mode == ASNOTATION_DOTPLUS) ? "%pRDE" : "%pRDP"))

#define BGP_RD_AS_FORMAT_SPACE(mode)                                           \
	((mode == ASNOTATION_DOT)                                              \
		 ? "%-21pRDD"                                                  \
		 : ((mode == ASNOTATION_DOTPLUS) ? "%-21pRDE" : "%-21pRDP"))

/* 【结构】rd_as —— type 0 的解码结果：2 字节 AS + 4 字节编号(val)。
 * as_t 是 FRR 的 AS 号类型（4 字节），这里只用低 16 位。type 字段冗余保存是为了
 * 让解码结果自带来源信息，便于回显。 */
struct rd_as {
	uint16_t type;
	as_t as;
	uint32_t val;
};

/* 【结构】rd_ip —— type 1 的解码结果：4 字节 IPv4 + 2 字节编号(val)。
 * 注意与 rd_as 相比 val 从 32 位缩到 16 位，且多了 ip 字段。 */
struct rd_ip {
	uint16_t type;
	uint16_t val;
	struct in_addr ip;
};

/* 【结构】rd_vnc_eth —— VNC 专用的以太网类 RD（编译开关见上）：
 * 2 字节类型 + 1 字节本地 NVE 编号 + 6 字节 MAC。整体也是 8 字节，但布局
 * 与标准三种完全不同——所以解码必须由 decode_rd_vnc_eth 专门处理。 */
#ifdef ENABLE_BGP_VNC
struct rd_vnc_eth {
	uint16_t type;
	uint8_t local_nve_id;
	struct ethaddr macaddr;
};
#endif

/* 【函数】只处理 RD 的**前 2 字节类型码**：
 *   decode_rd_type : 从线格式读出类型（注意字节序转换）。
 *   encode_rd_type : 把类型写回线格式。写的是 2 字节字段，buf 至少 2 字节。
 * 单独提供这两个函数是因为不少调用点只需知道类型而不关心剩余 6 字节。 */
extern uint16_t decode_rd_type(const uint8_t *pnt);
extern void encode_rd_type(uint16_t type, uint8_t *rd_type);

/* 【函数】三个按类型分派的解码函数（**入参 pnt 指向的是 RD 整体还是类型后的
 * 6 字节，读实现确认**，这与调用点的上下文相关，容易错位）：
 *   decode_rd_as  : type 0，2 字节 AS + 4 字节编号 → struct rd_as。
 *   decode_rd_as4 : type 2，4 字节 AS + 2 字节编号 → 复用 struct rd_as（字段宽
 *                   度由函数自己按类型解释，调用方不要拿 rd_as.val 的位宽硬套）。
 *   decode_rd_ip  : type 1，4 字节 IPv4 + 2 字节编号 → struct rd_ip。
 * 三者都**只填充调用方提供的结构体**，不分配内存。 */
extern void decode_rd_as(const uint8_t *pnt, struct rd_as *rd_as);
extern void decode_rd_as4(const uint8_t *pnt, struct rd_as *rd_as);
extern void decode_rd_ip(const uint8_t *pnt, struct rd_ip *rd_ip);
#ifdef ENABLE_BGP_VNC
extern void decode_rd_vnc_eth(const uint8_t *pnt,
			      struct rd_vnc_eth *rd_vnc_eth);
#endif

/* 【函数】字符串与 RD 的互转，以及自动生成：
 *   str2prefix_rd    : 解析配置里写的 RD（如 "65001:100"、"10.0.0.1:5"）填进
 *                      struct prefix_rd。返回 0 表示成功（注意不是返回 bool
 *                      语义，失败时返回非 0），写代码时别把返回 0 当成“没找到”。
 *   prefix_rd2str    : 反向打印，需调用方提供 buf 与 size（至少 RD_ADDRSTRLEN），
 *                      并按 asnotation 决定 2 字节 AS 的显示写法；返回 buf。
 *   form_auto_rd     : 实现配置 `rd auto`——用 router_id 与一个本地编号自动拼出
 *                      type 1 形式的 RD，避免运维手工保证唯一性。
 *                      注意它填的是调用方传入的 prd，不返回新对象。 */
extern int str2prefix_rd(const char *str, struct prefix_rd *prd);
extern char *prefix_rd2str(const struct prefix_rd *prd, char *buf, size_t size,
			   enum asnotation_mode asnotation);
extern void form_auto_rd(struct in_addr router_id, uint16_t rd_id,
			 struct prefix_rd *prd);

#endif /* _QUAGGA_BGP_RD_H */
