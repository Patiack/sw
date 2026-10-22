// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/*
 * 文件导读：取值 与 设值便捷包装接口（yang_wrappers.h）
 *
 * 本头文件声明一组便捷包装：把 yang.c 中底层 libyang 调用封装成按“YANG 值类型”
 * 分类的简单函数，供各守护进程直接读写配置与状态，无需接触 libyang 的细节。
 * 层次关系：北向回调 与 各守护进程调用本层，本层再调用 yang.c 的通用接口。
 *
 * 每组类型遵循统一的“四件套”命名：
 *   yang_str2 加类型名     字符串转成该类型的值（解析）
 *   yang_data_new_类型名   把值包装成 yang_data
 *   yang_dnode_get_类型名  按 xpath 从数据树取值（路径为可变参数）
 *   yang_get_default_类型名 取该路径 schema 上的默认值
 * 带 PRINTFRR 的函数表示其路径参数受 printf 格式检查。
 * 特例：string 与 binary 另有取到调用方缓冲的 _buf 变体。
 */
#ifndef _FRR_NORTHBOUND_WRAPPERS_H_
#define _FRR_NORTHBOUND_WRAPPERS_H_

#include <libyang/libyang.h>
#include "prefix.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * bool 类型包装四件套：str2 解析、data_new 构造、dnode_get 按 xpath 取值、get_default 取默认值。
 * 后两者路径为可变参数并带 PRINTFRR 格式检查。
 */
/* bool */
extern bool yang_str2bool(const char *value);
extern struct yang_data *yang_data_new_bool(const char *xpath, bool value);
extern bool yang_dnode_get_bool(const struct lyd_node *dnode,
				const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern bool yang_get_default_bool(const char *xpath_fmt, ...) PRINTFRR(1, 2);

/*
 * dec64 类型包装（十进制小数，用 double 承载）：str2 解析、data_new 构造、
 * dnode_get 取值、get_default 取默认值。
 */
/* dec64 */
extern double yang_str2dec64(const char *xpath, const char *value);
extern struct yang_data *yang_data_new_dec64(const char *xpath, double value);
extern double yang_dnode_get_dec64(const struct lyd_node *dnode,
				   const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern double yang_get_default_dec64(const char *xpath_fmt, ...) PRINTFRR(1, 2);

/*
 * enum 类型包装（以 int 承载）：str2 把枚举名解析为取值、data_new 构造、
 * dnode_get 取值、get_default 取默认值。
 */
/* enum */
extern int yang_str2enum(const char *xpath, const char *value);
extern struct yang_data *yang_data_new_enum(const char *xpath, int value);
extern int yang_dnode_get_enum(const struct lyd_node *dnode,
			       const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern int yang_get_default_enum(const char *xpath_fmt, ...) PRINTFRR(1, 2);

/*
 * int8 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* int8 */
extern int8_t yang_str2int8(const char *value);
extern struct yang_data *yang_data_new_int8(const char *xpath, int8_t value);
extern int8_t yang_dnode_get_int8(const struct lyd_node *dnode,
				  const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern int8_t yang_get_default_int8(const char *xpath_fmt, ...) PRINTFRR(1, 2);

/*
 * int16 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* int16 */
extern int16_t yang_str2int16(const char *value);
extern struct yang_data *yang_data_new_int16(const char *xpath, int16_t value);
extern int16_t yang_dnode_get_int16(const struct lyd_node *dnode,
				    const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern int16_t yang_get_default_int16(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * int32 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* int32 */
extern int32_t yang_str2int32(const char *value);
extern struct yang_data *yang_data_new_int32(const char *xpath, int32_t value);
extern int32_t yang_dnode_get_int32(const struct lyd_node *dnode,
				    const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern int32_t yang_get_default_int32(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * int64 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* int64 */
extern int64_t yang_str2int64(const char *value);
extern struct yang_data *yang_data_new_int64(const char *xpath, int64_t value);
extern int64_t yang_dnode_get_int64(const struct lyd_node *dnode,
				    const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern int64_t yang_get_default_int64(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * uint8 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* uint8 */
extern uint8_t yang_str2uint8(const char *value);
extern struct yang_data *yang_data_new_uint8(const char *xpath, uint8_t value);
extern uint8_t yang_dnode_get_uint8(const struct lyd_node *dnode,
				    const char *xpath_fmt, ...) PRINTFRR(2, 3);
extern uint8_t yang_get_default_uint8(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * uint16 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* uint16 */
extern uint16_t yang_str2uint16(const char *value);
extern struct yang_data *yang_data_new_uint16(const char *xpath,
					      uint16_t value);
extern uint16_t yang_dnode_get_uint16(const struct lyd_node *dnode,
				      const char *xpath_fmt, ...)
	PRINTFRR(2, 3);
extern uint16_t yang_get_default_uint16(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * uint32 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* uint32 */
extern uint32_t yang_str2uint32(const char *value);
extern struct yang_data *yang_data_new_uint32(const char *xpath,
					      uint32_t value);
extern uint32_t yang_dnode_get_uint32(const struct lyd_node *dnode,
				      const char *xpath_fmt, ...)
	PRINTFRR(2, 3);
extern uint32_t yang_get_default_uint32(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * uint64 类型包装四件套：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* uint64 */
extern uint64_t yang_str2uint64(const char *value);
extern struct yang_data *yang_data_new_uint64(const char *xpath,
					      uint64_t value);
extern uint64_t yang_dnode_get_uint64(const struct lyd_node *dnode,
				      const char *xpath_fmt, ...)
	PRINTFRR(2, 3);
extern uint64_t yang_get_default_uint64(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);

/*
 * string 类型包装：data_new_string 构造、dnode_get_string 取值（返回库内字符串指针）、
 * dnode_get_string_buf 取到调用方缓冲、get_default_string 与 get_default_string_buf 取默认值。
 * 注意 dnode_get_string 返回的指针归数据树所有，不应释放。
 */
/* string */
extern struct yang_data *yang_data_new_string(const char *xpath,
					      const char *value);
extern const char *yang_dnode_get_string(const struct lyd_node *dnode,
					 const char *xpath_fmt, ...)
	PRINTFRR(2, 3);
extern void yang_dnode_get_string_buf(char *buf, size_t size,
				      const struct lyd_node *dnode,
				      const char *xpath_fmt, ...)
	PRINTFRR(4, 5);
extern const char *yang_get_default_string(const char *xpath_fmt, ...)
	PRINTFRR(1, 2);
extern void yang_get_default_string_buf(char *buf, size_t size,
					const char *xpath_fmt, ...)
	PRINTFRR(3, 4);

/*
 * binary 类型包装：data_new_binary 构造（需给出长度）、
 * dnode_get_binary_buf 取到调用方缓冲并返回实际字节数。
 */
/* binary */
extern struct yang_data *yang_data_new_binary(const char *xpath,
					      const char *value, size_t len);
extern size_t yang_dnode_get_binary_buf(char *buf, size_t size,
					const struct lyd_node *dnode,
					const char *xpath_fmt, ...)
	PRINTFRR(4, 5);

/*
 * empty 类型包装：data_new_empty 构造无值节点、dnode_get_empty 判断该空节点是否存在。
 */
/* empty */
extern struct yang_data *yang_data_new_empty(const char *xpath);
extern bool yang_dnode_get_empty(const struct lyd_node *dnode,
				 const char *xpath_fmt, ...) PRINTFRR(2, 3);

/*
 * ip prefix 类型包装（前缀，含掩码）：str2 解析、data_new 构造、
 * dnode_get 取值到 prefix、get_default 取默认值。
 */
/* ip prefix */
extern void yang_str2prefix(const char *value, union prefixptr prefix);
extern struct yang_data *yang_data_new_prefix(const char *xpath,
					      union prefixconstptr prefix);
extern void yang_dnode_get_prefix(struct prefix *prefix,
				  const struct lyd_node *dnode,
				  const char *xpath_fmt, ...) PRINTFRR(3, 4);
extern void yang_get_default_prefix(union prefixptr var, const char *xpath_fmt,
				    ...) PRINTFRR(2, 3);

/*
 * ipv4 类型包装（单地址）：str2 解析、data_new 构造、
 * dnode_get 取值到 in_addr、get_default 取默认值。
 */
/* ipv4 */
extern void yang_str2ipv4(const char *value, struct in_addr *addr);
extern struct yang_data *yang_data_new_ipv4(const char *xpath,
					    const struct in_addr *addr);
extern void yang_dnode_get_ipv4(struct in_addr *addr,
				const struct lyd_node *dnode,
				const char *xpath_fmt, ...) PRINTFRR(3, 4);
extern void yang_get_default_ipv4(struct in_addr *var, const char *xpath_fmt,
				  ...) PRINTFRR(2, 3);

/*
 * ipv4p 类型包装（ipv4 前缀形式）：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* ipv4p */
extern void yang_str2ipv4p(const char *value, union prefixptr prefix);
extern struct yang_data *yang_data_new_ipv4p(const char *xpath,
					     union prefixconstptr prefix);
extern void yang_dnode_get_ipv4p(union prefixptr prefix,
				 const struct lyd_node *dnode,
				 const char *xpath_fmt, ...) PRINTFRR(3, 4);
extern void yang_get_default_ipv4p(union prefixptr var, const char *xpath_fmt,
				   ...) PRINTFRR(2, 3);

/*
 * ipv6 类型包装（单地址）：str2 解析、data_new 构造、
 * dnode_get 取值到 in6_addr、get_default 取默认值。
 */
/* ipv6 */
extern void yang_str2ipv6(const char *value, struct in6_addr *addr);
extern struct yang_data *yang_data_new_ipv6(const char *xpath,
					    const struct in6_addr *addr);
extern void yang_dnode_get_ipv6(struct in6_addr *addr,
				const struct lyd_node *dnode,
				const char *xpath_fmt, ...) PRINTFRR(3, 4);
extern void yang_get_default_ipv6(struct in6_addr *var, const char *xpath_fmt,
				  ...) PRINTFRR(2, 3);

/*
 * ipv6p 类型包装（ipv6 前缀形式）：str2 解析、data_new 构造、dnode_get 取值、get_default 取默认值。
 */
/* ipv6p */
extern void yang_str2ipv6p(const char *value, union prefixptr prefix);
extern struct yang_data *yang_data_new_ipv6p(const char *xpath,
					     union prefixconstptr prefix);
extern void yang_dnode_get_ipv6p(union prefixptr prefix,
				 const struct lyd_node *dnode,
				 const char *xpath_fmt, ...) PRINTFRR(3, 4);
extern void yang_get_default_ipv6p(union prefixptr var, const char *xpath_fmt,
				   ...) PRINTFRR(2, 3);

/*
 * ip 类型包装（通用 ipaddr，可承载 v4 或 v6）：str2 解析、data_new 构造、
 * dnode_get 取值、get_default 取默认值。
 */
/* ip */
extern void yang_str2ip(const char *value, struct ipaddr *addr);
extern struct yang_data *yang_data_new_ip(const char *xpath,
					  const struct ipaddr *addr);
extern void yang_dnode_get_ip(struct ipaddr *addr, const struct lyd_node *dnode,
			      const char *xpath_fmt, ...) PRINTFRR(3, 4);
extern void yang_get_default_ip(struct ipaddr *var, const char *xpath_fmt, ...)
	PRINTFRR(2, 3);

/*
 * mac 类型包装（以太网地址 ethaddr）：data_new 构造、str2 解析、dnode_get 取值。
 */
/* mac */
extern struct yang_data *yang_data_new_mac(const char *xpath,
					   const struct ethaddr *mac);
extern void yang_str2mac(const char *value, struct ethaddr *mac);
extern void yang_dnode_get_mac(struct ethaddr *mac, const struct lyd_node *dnode,
			       const char *xpath_fmt, ...) PRINTFRR(3, 4);

/*
 * date-and-time 类型包装：data_new_date_and_time 构造（带是否 monotonic 的标志）、
 * dnode_get_date_and_timespec 取 timespec、dnode_get_date_and_time 取 time_t。
 */
/*data-and-time */
extern struct yang_data *yang_data_new_date_and_time(const char *xpath,
						     time_t time,
						     bool is_monotime);
struct timespec yang_dnode_get_date_and_timespec(const struct lyd_node *dnode,
						 const char *xpath_fmt, ...)
	PRINTFRR(2, 3);
time_t yang_dnode_get_date_and_time(const struct lyd_node *dnode,
				    const char *xpath_fmt, ...)
	PRINTFRR(2, 3);

/*
 * rt-types 中的 bandwidth-ieee-float32 包装：
 * dnode_get_bandwidth_ieee_float32 取单精度浮点带宽值。
 */
/* rt-types:bandwidth-ieee-float32 */
extern float yang_dnode_get_bandwidth_ieee_float32(const struct lyd_node *dnode,
						   const char *xpath_fmt, ...)
	PRINTFRR(2, 3);

/*
 * nexthop 类型转字符串：yang_nexthop_type2str 把 nexthop 类型枚举转成名称。
 */
/* nexthop enum2str */
extern const char *yang_nexthop_type2str(uint32_t ntype);

/*
 * AFI 与 SAFI 的 identity 转换：value2identity 把 afi 与 safi 组合成 identity 名，
 * identity2value 则把 identity 名解析回 afi 与 safi 输出参数。
 */
const char *yang_afi_safi_value2identity(afi_t afi, safi_t safi);
void yang_afi_safi_identity2value(const char *key, afi_t *afi, safi_t *safi);
/*
 * yang_dnode_get_identity - 取数据节点的 identity 名字符串。
 */
extern const char *yang_dnode_get_identity(const struct lyd_node *dnode);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_NORTHBOUND_WRAPPERS_H_ */
