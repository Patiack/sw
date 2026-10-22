// SPDX-License-Identifier: GPL-2.0-or-later
/* SNMP support
 * Copyright (C) 1999 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

/*
 * 【文件导读】smux.h —— SMUX / AgentX 子代理与 SNMP MIB 注册接口
 *
 * 一、职责
 *   声明 FRR 各守护进程作为 net-snmp 子代理时用到的公共接口与常量：MIB 匹配返回码、
 *   各种 SNMP 语法常量、MIB 注册宏、Trap 发送接口，以及 snmp.c 里那些 OID 转换函数
 *   的原型。
 *
 * 二、核心内容
 *   MATCH_* / SNMP_* / IANAIPROUTEPROTOCOL* / INETADDRESSTYPE* 系列宏：常量。
 *   REGISTER_MIB：把一张 MIB 表注册进 net-snmp 的宏。
 *   struct trap_object / struct index_oid：Trap 时描述待发送对象的辅助结构。
 *   SNMP_LOCAL_VARIABLES 与 SNMP_INTEGER / SNMP_OCTET / SNMP_STRING /
 *     SNMP_IPADDRESS / SNMP_IP6ADDRESS：MIB 处理函数里"回填返回值"的宏。
 *
 * 三、与其他模块的关系
 *   依赖 net-snmp agent 头文件、frrevent.h（事件循环）、hook.h（hook 机制）。
 *   实现在 lib/snmp.c、lib/agentx.c 等；各守护进程（bgpd、ospfd、zebra 等）用这些
 *   宏与函数接入 SNMP。
 *
 * 四、推荐阅读顺序
 *   先看顶部常量宏，再看 REGISTER_MIB 与两个结构体，然后看 SNMP_* 返回值宏（理解
 *   MIB 回调怎么回填），最后看函数声明与末尾的 DECLARE_HOOK。
 *
 * 五、易踩的坑
 *   1) SNMP_STRING 用 var_len 同时承载 MIN 结果与 strlcpy 的长度，注意截断语义。
 *   2) SNMP_IP6ADDRESS 直接返回传入值 V 的地址，V 必须是可长期存活的对象。
 *   3) REGISTER_MIB 靠 sizeof(var) / sizeof(struct vartype) 推断表项个数，var 必须
 *      是真数组而非指针，否则 sizeof 失效。
 *   4) MATCH_SUCCEEDED 是 0、MATCH_FAILED 是 -1，回调返回非 0 即未命中，别写反。
 *   5) trap_object.namelen 为负表示该对象不带索引。
 */

#ifndef _ZEBRA_SNMP_H
#define _ZEBRA_SNMP_H

#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/agent/snmp_vars.h>

#include "frrevent.h"
#include "hook.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Structures here are mostly compatible with UCD SNMP 4.1.1 */
/*
 * 【宏】MATCH_FAILED (-1) / MATCH_SUCCEEDED (0)
 *   net-snmp header 回调的返回码：MATCH_FAILED 表示本次请求不由本处理函数负责，
 *   MATCH_SUCCEEDED 表示命中。注意成功值是 0、失败是 -1。
 */
#define MATCH_FAILED     (-1)
#define MATCH_SUCCEEDED  0

/* SYNTAX TruthValue from SNMPv2-TC. */
/*
 * 【宏】SNMP_TRUE (1) / SNMP_FALSE (2)
 *   SNMPv2-TC 中 TruthValue 语法的取值（注意不是 0 / 1 布尔，TRUE 为 1、FALSE 为 2）。
 */
#define SNMP_TRUE  1
#define SNMP_FALSE 2

/* SYNTAX RowStatus from SNMPv2-TC. */
/*
 * 【宏】SNMP_VALID (1) / SNMP_INVALID (2)
 *   SNMPv2-TC 中 RowStatus 语法的取值，用于表示表项是否有效。
 */
#define SNMP_VALID  1
#define SNMP_INVALID 2

/*
 * 【宏】IN_ADDR_SIZE (4UL) / IN6_ADDR_SIZE (16UL)
 *   IPv4 / IPv6 地址的字节长度常量。
 */
#define IN_ADDR_SIZE  4UL
#define IN6_ADDR_SIZE 16UL

/* IANAipRouteProtocol */
/*
 * 【宏】IANAIPROUTEPROTOCOL系列
 *   IANAipRouteProtocol TC 的枚举常量，用于 ipCidrRouteProto 等 MIB 对象，标记某条
 *   路由的来源协议（other、local、netmgmt、icmp、egp、ospf、bgp 等）。数值需与 RFC
 *   的 IANA 注册一致，勿改。
 */
#define IANAIPROUTEPROTOCOLOTHER 1
#define IANAIPROUTEPROTOCOLLOCAL 2
#define IANAIPROUTEPROTOCOLNETMGMT 3
#define IANAIPROUTEPROTOCOLICMP 4
#define IANAIPROUTEPROTOCOLEGP 5
#define IANAIPROUTEPROTOCOLGGP 6
#define IANAIPROUTEPROTOCOLHELLO 7
#define IANAIPROUTEPROTOCOLRIP 8
#define IANAIPROUTEPROTOCOLISIS 9
#define IANAIPROUTEPROTOCOLESIS 10
#define IANAIPROUTEPROTOCOLCISCOIGRP 11
#define IANAIPROUTEPROTOCOLBBNSPFIGP 12
#define IANAIPROUTEPROTOCOLOSPF 13
#define IANAIPROUTEPROTOCOLBGP 14
#define IANAIPROUTEPROTOCOLIDPR 15
#define IANAIPROUTEPROTOCOLCISCOEIGRP 16
#define IANAIPROUTEPROTOCOLDVMRP 17

/*
 * 【宏】INETADDRESSTYPE系列
 *   InetAddressType TC：UNKNOWN(0) / IPV4(1) / IPV6(2)，用于 InetAddress 系 MIB 对象。
 */
#define INETADDRESSTYPEUNKNOWN 0
#define INETADDRESSTYPEIPV4 1
#define INETADDRESSTYPEIPV6 2

/*
 * 【宏】REGISTER_MIB(descr, var, vartype, theoid)
 *   把一张 MIB 表注册给 net-snmp：转调 smux_register_mib，并自动用 sizeof 推出表项
 *   宽度（sizeof(struct vartype)）、表项个数（sizeof(var) / sizeof(struct vartype)）
 *   以及 OID 长度。
 *   坑：var 必须是真的数组（不能是指针），否则 sizeof(var) 会退化成指针大小，从而算
 *       错表项个数。此处的 #undef 用于覆盖 net-snmp 自带的同名宏。
 */
#undef REGISTER_MIB
#define REGISTER_MIB(descr, var, vartype, theoid)                              \
	smux_register_mib(descr, (struct variable *)var,                       \
			  sizeof(struct vartype),                              \
			  sizeof(var) / sizeof(struct vartype), theoid,        \
			  sizeof(theoid) / sizeof(oid))

/*
 * 【结构体】struct trap_object —— 描述 Trap 中要携带的一个对象
 *   namelen：对象 OID 的长度；为负表示该对象不是"列"（不带索引）。
 *   name   ：对象 OID。
 */
struct trap_object {
	int namelen; /* Negative if the object is not indexed */
	oid name[MAX_OID_LEN];
};

/*
 * 【结构体】struct index_oid —— 描述 Trap 用的索引 OID
 *   indexlen ：索引 OID 的长度。
 *   indexname：索引 OID 数组。
 */
struct index_oid {
	int indexlen;
	oid indexname[MAX_OID_LEN];
};
/* Declare SMUX return value. */
/*
 * 【宏】SNMP_LOCAL_VARIABLES
 *   在每个 MIB 处理函数开头声明一组 static 临时变量（整型、IPv4 地址、octet、字符串
 *   缓冲），供下面的 SNMP_* 宏回填返回值时使用。变量均标了 __attribute__((unused))，
 *   避免未使用告警。
 */
#define SNMP_LOCAL_VARIABLES                                                   \
	static long snmp_int_val __attribute__((unused));                      \
	static struct in_addr snmp_in_addr_val __attribute__((unused));        \
	static uint8_t snmp_octet_val __attribute__((unused));                 \
	static char snmp_string_val[255] __attribute__((unused));
/*
 * 【宏】SNMP_INTEGER(V)
 *   把 *var_len 设为整型大小、snmp_int_val 设为 V，并返回指向它的 uint8_t 指针，供
 *   MIB 回调返回整数值。依赖先展开 SNMP_LOCAL_VARIABLES。
 */
#define SNMP_INTEGER(V)                                                        \
	(*var_len = sizeof(snmp_int_val), snmp_int_val = V,                    \
	 (uint8_t *)&snmp_int_val)

/*
 * 【宏】SNMP_OCTET(V)
 *   回填单个 octet 值 V：设置 *var_len 为 octet 大小并返回指向 snmp_octet_val 的
 *   指针。
 */
#define SNMP_OCTET(V)							\
	(*var_len = sizeof(snmp_octet_val), snmp_octet_val = V,                    \
	 (uint8_t *)&snmp_octet_val)

/*
 * 【宏】SNMP_STRING(V)
 *   回填字符串 V：*var_len 取缓冲大小与 strlen(V)+1 的较小值，用 strlcpy 把 V 拷入
 *   snmp_string_val，返回其指针。
 *   坑：V 超长时会截断，var_len 反映的是截断后的长度。
 */
#define SNMP_STRING(V)                                                         \
	(*var_len = MIN(sizeof(snmp_string_val), strlen(V) + 1),               \
	 strlcpy(snmp_string_val, V, *var_len), (uint8_t *)&snmp_string_val)

/*
 * 【宏】SNMP_IPADDRESS(V)
 *   回填 IPv4 地址 V：*var_len 设为 sizeof(struct in_addr)，把 V 存入
 *   snmp_in_addr_val，返回其指针。
 */
#define SNMP_IPADDRESS(V)                                                      \
	(*var_len = sizeof(struct in_addr), snmp_in_addr_val = V,              \
	 (uint8_t *)&snmp_in_addr_val)

/*
 * 【宏】SNMP_IP6ADDRESS(V)
 *   回填 IPv6 地址 V：*var_len 设为 sizeof(struct in6_addr)，直接返回 V 的地址。
 *   坑：返回的是 V 本身的指针，V 必须在本次应答期间保持有效，不能是已被销毁的临时
 *       栈变量。
 */
#define SNMP_IP6ADDRESS(V) (*var_len = sizeof(struct in6_addr), (uint8_t *)&V)

/*
 * Check to see if snmp is enabled or not
 */
/*
 * 【函数声明】smux_enabled(void)
 *   返回 SNMP / SMUX 是否已启用。供各模块据此决定是否上报 MIB 数据。
 */
extern bool smux_enabled(void);

/*
 * 【函数声明】SMUX / AgentX 生命周期与注册接口
 *   libagentx_init    ：初始化 AgentX 库支持。
 *   smux_init(tm)     ：以给定事件循环 tm 初始化 SMUX 子代理。
 *   smux_terminate    ：关闭并清理 SMUX 子代理。
 *   smux_agentx_enable：启用 AgentX 模式。
 *   smux_register_mib ：把一张 MIB 表注册到 net-snmp（REGISTER_MIB 宏最终调用它）。
 *   参数：descr 描述串；var 变量数组；width 单表项宽度；num 表项个数；name / namelen
 *         为 OID 及其长度。
 */
extern void libagentx_init(void);
extern void smux_init(struct event_loop *tm);
extern void smux_terminate(void);
extern void smux_agentx_enable(void);
extern void smux_register_mib(const char *descr, struct variable *var, size_t width, int num,
			      oid name[], size_t namelen);
/*
 * 【函数声明】smux_header_generic / smux_header_table
 *   两个通用 header 回调，实现在 snmp.c。前者用于标量、后者用于表；返回
 *   MATCH_SUCCEEDED / MATCH_FAILED。参数与副作用详见 snmp.c 中对应实现的注释。
 */
extern int smux_header_generic(struct variable *v, oid name[], size_t *length, int exact,
			       size_t *var_len, WriteMethod **write_method);
extern int smux_header_table(struct variable *v, oid *name, size_t *length, int exact,
			     size_t *var_len, WriteMethod **write_method);

/* For traps, three OID are provided:

 1. The enterprise OID to use (the last argument will be appended to
    it to form the SNMP trap OID)

 2. The base OID for objects to be sent in traps.

 3. The index OID for objects to be sent in traps. This index is used
    to designate a particular instance of a column.

 The provided trap object contains the bindings to be sent with the
 trap. The base OID will be prefixed to the provided OID and, if the
 length is positive, the requested OID is assumed to be a columnar
 object and the index OID will be appended.

 The two first arguments are the MIB registry used to locate the trap
 objects.

 The use of the arguments may differ depending on the implementation
 used.
*/
/*
 * 【函数声明】smux_trap(vp, vp_len, ename, enamelen, name, namelen, iname, inamelen,
 *                       trapobj, trapobjlen, sptrap)
 *   发送一个 SNMP trap。上方的大段英文注释解释了三类 OID 的作用：enterprise OID
 *   （ename，末段会拼成 trap 的 OID）、对象 base OID（name）、以及列对象索引 OID
 *   （iname，长度为正表示对象为列，会追加索引）。
 *   参数：vp / vp_len 用于定位 trap 对象的 MIB 注册表；trapobj / trapobjlen 为要随
 *         trap 发送的对象绑定；sptrap 为具体 trap 类型。返回：无。
 */
extern void smux_trap(struct variable *vp, size_t vp_len, const oid *ename,
		      size_t enamelen, const oid *name, size_t namelen,
		      const oid *iname, size_t inamelen,
		      const struct trap_object *trapobj, size_t trapobjlen,
		      uint8_t sptrap);

/*
 * 【函数声明】smux_trap_multi_index(vp, vp_len, ename, enamelen, name, namelen, iname,
 *                                  index_len, trapobj, trapobjlen, sptrap)
 *   与 smux_trap 类似，但支持"多索引"：索引以 struct index_oid 数组（iname /
 *   index_len）给出，可同时指定多个索引实例。
 *   返回：int（0 成功 / 非 0 失败，沿用 net-snmp 约定）。
 */
extern int smux_trap_multi_index(struct variable *vp, size_t vp_len,
				 const oid *ename, size_t enamelen,
				 const oid *name, size_t namelen,
				 struct index_oid *iname, size_t index_len,
				 const struct trap_object *trapobj,
				 size_t trapobjlen, uint8_t sptrap);

/*
 * 【函数声明】smux_events_update(void)
 *   通知 SMUX 层刷新事件循环相关的注册（例如启用 / 停用后重新挂钩事件）。返回：无。
 */
extern void smux_events_update(void);
/*
 * 【函数声明】OID 与地址 / 数值互转工具（实现在 snmp.c）
 *   oid_compare / oid_copy / oid2in_addr / oid2in6_addr / oid2int /
 *   oid_copy_in_addr / oid_copy_in6_addr / oid_copy_int / oid2string / oid_copy_str。
 *   各函数的行为、参数与陷阱见 snmp.c 中对应实现的注释。
 */
extern int oid_compare(const oid *o1, int o1_len, const oid *o2, int o2_len);
extern void oid2in_addr(oid oid[], int len, struct in_addr *addr);
extern void oid2in6_addr(oid oid[], struct in6_addr *addr);
extern void oid2int(oid oid[], int *dest);
extern void *oid_copy(void *dest, const void *src, size_t size);
extern void oid_copy_in_addr(oid oid[], const struct in_addr *addr);
extern void oid_copy_in6_addr(oid oid[], const struct in6_addr *addr);
extern void oid_copy_int(oid oid[], int *val);
extern void oid2string(oid oid[], int len, char *string);
extern void oid_copy_str(oid oid[], const char *string, int len);

/*
 * 【宏】DECLARE_HOOK(agentx_enabled, (), ())
 *   声明一个名为 agentx_enabled 的 hook（无参数、无额外上下文）：当 AgentX 被启用时
 *   触发，其他模块可注册回调以感知该事件。hook 机制详见 hook.h。
 */
DECLARE_HOOK(agentx_enabled, (), ());

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_SNMP_H */
