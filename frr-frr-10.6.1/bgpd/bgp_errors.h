// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP-specific error messages.
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *               Don Slice
 */

#ifndef __BGP_ERRORS_H__
#define __BGP_ERRORS_H__

#include "lib/ferr.h"

/* ============================================================================
 * 【文件导读】bgp_errors.h —— bgpd 的“结构化错误码”
 * ============================================================================
 * 【它是什么】FRR 有一套 ferr(FRR error) 框架：每条错误不是一段裸字符串，而是
 * 一个**枚举错误码**，附带一段可翻译的模板消息。日志里打印时由框架把码翻成
 * 人类可读文本，因此同一份日志能被工具和本地化统一处理。
 *
 * 【怎么用】代码里写 `flog_err(EC_BGP_ATTR_FLAG, "…%s…", args)`，而不是
 * `zlog_err("…")`。好处有三：错误可被**分类统计**、消息格式集中在 bgp_errors.c
 * 里便于统一措辞、日志有稳定编号便于文档与排障手册引用。
 *
 * 【两条必须记住的规则】
 *   1. 枚举**从 BGP_FERR_START 开始**——ferr 把全局错误号空间按模块切段，各守护
 *      进程用自己的起始值，避免编号冲突。所以这里**不能**写成从 0 开始的普通枚举。
 *   2. **枚举的顺序与 bgp_errors.c 里的消息注册表必须逐条对应**。那张表是按
 *      “第几个枚举 → 第几行消息”的隐式约定登记的，因此在中间插入一个新错误码
 *      会让它**后面所有**错误码的消息全部错位（日志显示成别人的消息）——这是个
 *      非常典型、且不会编译报错的坑。新增错误请**追加到末尾**。
 *
 * 【阅读建议】本文件只是码表；想理解模板消息的写法与注册过程，看 bgp_errors.c 和
 *            lib/ferr.h。想找某个错误在哪儿被抛出，直接搜 `EC_BGP_xxx` 即可。
 * ============================================================================
 */

/* 【枚举】bgpd 的全部错误码。命名前缀 EC_ = Error Code，BGP_ 后用大写下划线描述
 * 场景（如 ATTR_FLAG = UPDATE 属性标志位有问题）。这些名字在日志与代码里出现，
 * 含义可以按其组成部分推断。
 * 按主题大致可分几类：属性/报文解析与校验(ATTR_*、PKT_*、ATTRIBUTE_*)、
 * 邻居与状态机(PEER_*、FSM、CONNECT、SENDQ_*)、更新组与表(UPDGRP_*、TABLE_CHUNK)、
 * EVPN/MACIP/VNI、FlowSpec、SNMP/Dump、能力协商(CAPABILITY_*)。
 * 注意末尾几个(EC_BGP_CONFIG_TIMEOUT、EC_BGP_LABEL_POOL_INSERT_FAIL、
 * EC_BGP_TTL_SECURITY_FAIL)是**后加的**，都追加在最后，见上面第 2 条规则。 */
enum bgp_log_refs {

	EC_BGP_ATTR_FLAG = BGP_FERR_START,
	EC_BGP_ATTR_LEN,
	EC_BGP_ATTR_ORIGIN,
	EC_BGP_ATTR_MAL_AS_PATH,
	EC_BGP_ATTR_FIRST_AS,
	EC_BGP_ATTR_MARTIAN_NH,
	EC_BGP_ATTR_PMSI_TYPE,
	EC_BGP_ATTR_PMSI_LEN,
	EC_BGP_ATTR_NH_SEND_LEN,
	EC_BGP_ATTR_AIGP,
	EC_BGP_PEER_GROUP,
	EC_BGP_PEER_DELETE,
	EC_BGP_TABLE_CHUNK,
	EC_BGP_MACIP_LEN,
	EC_BGP_LM_ERROR,
	EC_BGP_JSON_MEM_ERROR,
	EC_BGP_UPDGRP_ATTR_LEN,
	EC_BGP_UPDGRP_CREATE,
	EC_BGP_UPDATE_SND,
	EC_BGP_PKT_OPEN,
	EC_BGP_SND_FAIL,
	EC_BGP_INVALID_STATUS,
	EC_BGP_UPDATE_RCV,
	EC_BGP_NO_CAP,
	EC_BGP_NOTIFY_RCV,
	EC_BGP_KEEP_RCV,
	EC_BGP_RFSH_RCV,
	EC_BGP_CAP_RCV,
	EC_BGP_NH_UPD,
	EC_BGP_LABEL,
	EC_BGP_MULTIPATH,
	EC_BGP_PKT_PROCESS,
	EC_BGP_CONNECT,
	EC_BGP_FSM,
	EC_BGP_VNI,
	EC_BGP_NO_DFLT,
	EC_BGP_VTEP_INVALID,
	EC_BGP_ES_INVALID,
	EC_BGP_EVPN_ROUTE_DELETE,
	EC_BGP_EVPN_FAIL,
	EC_BGP_EVPN_ROUTE_INVALID,
	EC_BGP_EVPN_ROUTE_CREATE,
	EC_BGP_ES_CREATE,
	EC_BGP_EVPN_AS_MISMATCH,
	EC_BGP_EVPN_INSTANCE_MISMATCH,
	EC_BGP_FLOWSPEC_PACKET,
	EC_BGP_FLOWSPEC_INSTALLATION,
	EC_BGP_ASPATH_FEWER_HOPS,
	EC_BGP_DEFUNCT_SNPA_LEN,
	EC_BGP_MISSING_ATTRIBUTE,
	EC_BGP_ATTRIBUTE_TOO_SMALL,
	EC_BGP_EXT_ATTRIBUTE_TOO_SMALL,
	EC_BGP_ATTRIBUTE_REPEATED,
	EC_BGP_ATTRIBUTE_TOO_LARGE,
	EC_BGP_ATTRIBUTE_PARSE_ERROR,
	EC_BGP_ATTRIBUTE_PARSE_WITHDRAW,
	EC_BGP_ATTRIBUTE_FETCH_ERROR,
	EC_BGP_ATTRIBUTES_MISMATCH,
	EC_BGP_DUMP,
	EC_BGP_UPDATE_PACKET_SHORT,
	EC_BGP_UPDATE_PACKET_LONG,
	EC_BGP_UNRECOGNIZED_CAPABILITY,
	EC_BGP_NO_TCP_MD5,
	EC_BGP_EVPN_PMSI_PRESENT,
	EC_BGP_EVPN_VPN_VNI,
	EC_BGP_EVPN_ESI,
	EC_BGP_INVALID_LABEL_STACK,
	EC_BGP_ZEBRA_SEND,
	EC_BGP_CAPABILITY_INVALID_LENGTH,
	EC_BGP_CAPABILITY_INVALID_DATA,
	EC_BGP_CAPABILITY_VENDOR,
	EC_BGP_CAPABILITY_UNKNOWN,
	EC_BGP_INVALID_NEXTHOP_LENGTH,
	EC_BGP_DOPPELGANGER_CONFIG,
	EC_BGP_ROUTER_ID_SAME,
	EC_BGP_INVALID_BGP_INSTANCE,
	EC_BGP_INVALID_ROUTE,
	EC_BGP_NO_LL_ADDRESS_AVAILABLE,
	EC_BGP_SENDQ_STUCK_WARN,
	EC_BGP_SENDQ_STUCK_PROPER,
	EC_BGP_PATH_WITHOUT_LABEL,
	EC_BGP_ROUTE_REFRESH_INVALID,
	EC_BGP_PER_SRC_NHG,
	EC_BGP_ROUTE_ATTRIBUTE_MALFORMED,
	EC_BGP_CONFIG_CONSTRAINT_VIOLATION,
	EC_BGP_ROUTE_MAP_SCRIPT,
	EC_BGP_VRF_NOT_FOUND,
	EC_BGP_ZEBRA_MSG_DECODE,
	EC_BGP_SRV6_LOCATOR_MISMATCH,
	EC_BGP_INVALID_BGP_INSTANCE_ID,
	EC_BGP_SRV6_SID_MODE_CONFLICT,
	EC_BGP_CONFIG_TIMEOUT,
	EC_BGP_LABEL_POOL_INSERT_FAIL,
	EC_BGP_TTL_SECURITY_FAIL,
};

/* 【函数】把上表的错误码与消息模板注册进 ferr 框架，bgpd 启动时调用一次。
 * 若忘了调用，flog_err(EC_BGP_...) 打出来的就是“未知错误码”而不是文本。 */
extern void bgp_error_init(void);

#endif
