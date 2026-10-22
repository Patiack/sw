// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP message debug header.
 * Copyright (C) 1996, 97, 98 Kunihiro Ishiguro
 */

/* ============================================================================
 * 【文件导读】bgp_debug.h —— BGP 调试 / 日志开关体系的对外接口
 * ============================================================================
 * 【这个文件解决什么问题】
 *   为整个 bgpd 提供一套“运行时可开关、可分类、可细化到邻居 / 前缀”的调试
 *   基础设施。业务代码只用一个宏 BGP_DEBUG(大类, 子位) 就能判断“此刻是否该
 *   打印这类调试信息”，无需关心开关是怎么被配置下来的。
 *
 * 【为什么这样设计：两套状态 + 位掩码】
 *   - 两套全局状态(每个大类各一个 unsigned long)：
 *       conf_bgp_debug_* : 配置态，写 running-config(持久化)，在 config 节点设置。
 *       term_bgp_debug_* : 终端态，仅运行期有效、不落盘，在 enable 提示符设置。
 *     因此有 CONF_BGP_DEBUG / BGP_DEBUG 两个判定宏分别读这两套。
 *   - 为什么用位掩码而不是布尔：同一大类下常有多个可独立开关的子项，例如 update
 *     大类下有 in / out / prefix / detail 四个子位。用一个整型存一份掩码即可按位
 *     独立开关，又能一次按位与判断“这类是否开”。布尔无法表达“多个子项各自独立”
 *     的组合，也无法按位组合。
 *
 * 【开关的三个粒度】
 *   1) 按事件类型：每个大类(neighbor_events / update / zebra ...)一个位掩码。
 *   2) 按邻居    ：bgp_debug_*_peers 这些 list，元素是 bgp_debug_filter，
 *                  仅当被打印对象的 host 命中表内条目才输出。
 *   3) 按前缀 / afi-safi：bgp_debug_*_prefixes 这些 list，按前缀(含 EVPN 各
 *                  route-type)或 prefix-list 过滤。
 *   三者是 AND 关系：大类位开、邻居命中、前缀命中，才会真正打印。
 *
 * 【关键数据结构】
 *   struct bgp_debug_filter : 一条过滤条目(邻居 host 或 前缀 p 或 prefix-list)。
 *   bgp_status_msg / bgp_type_str / bgp_notify_* / bgp_global_gr_mode_str：字符串表。
 *
 * 【推荐阅读顺序】
 *   本头文件宏定义(BGP_DEBUG / CONF_BGP_DEBUG / DEBUG_ON 等)
 *   → bgp_debug.c 的 DEFUN 命令族(开关怎么设)
 *   → bgp_config_write_debug(怎么落盘)
 *   → bgp_debug_per_peer / bgp_debug_per_prefix(判定怎么算)。
 * ============================================================================
 */
#ifndef _QUAGGA_BGP_DEBUG_H
#define _QUAGGA_BGP_DEBUG_H

#include "hook.h"
#include "vty.h"

#include "bgp_attr.h"
#include "bgp_updgrp.h"

/*
 * 配置写入钩子(与 bgp_debug.c 中的 DEFINE_HOOK 对应)：其它模块可挂接，
 * 在写 running-config 或 show debugging 时补充自己的 debug 配置行。
 */
DECLARE_HOOK(bgp_hook_config_write_debug, (struct vty *vty, bool running),
	     (vty, running));

/*
 * 报文转储(dump)方向 / 内容位：供 bgp_packet_dump 判断打印范围。
 * DUMP_ON 总开关；DUMP_SEND / DUMP_RECV 选方向；DUMP_WITHDRAW / DUMP_NLRI
 * 在 UPDATE 中额外打印撤消 / 网络前缀；DUMP_DETAIL 打印更详细内容。
 */
/* sort of packet direction */
#define DUMP_ON        1
#define DUMP_SEND      2
#define DUMP_RECV      4

/* for dump_update */
#define DUMP_WITHDRAW  8
#define DUMP_NLRI     16

/* dump detail */
#define DUMP_DETAIL   32

/* RD + Prefix + Path-Id */
#define BGP_PRD_PATH_STRLEN                                                    \
	(PREFIX_STRLEN + RD_ADDRSTRLEN + INET6_ADDRSTRLEN + 34)

/*
 * 旧式全局调试开关(历史遗留)：dump_* 控制报文转储方向；
 * Debug_Event / Debug_Keepalive / Debug_Update / Debug_Radix 为更早的事件类标志。
 * 新代码优先用 BGP_DEBUG 宏，这些仅保留以兼容旧调用点。
 */
extern int dump_open;
extern int dump_update;
extern int dump_keepalive;
extern int dump_notify;

extern int Debug_Event;
extern int Debug_Keepalive;
extern int Debug_Update;
extern int Debug_Radix;

/*
 * 一组调试用标记(NLRI / WITHDRAW / NO_OPT / SEND / RECV / DETAIL)。
 * 疑点：在本头文件对应的 bgp_debug.c 中未查到使用点，可能供其它文件或历史接口
 * 引用，故原样保留。
 */
#define	NLRI	 1
#define	WITHDRAW 2
#define	NO_OPT	 3
#define	SEND	 4
#define	RECV	 5
#define	DETAIL	 6

/*
 * 对外函数原型：初始化(注册命令)、报文转储、以及通用的 debug(option) 判定。
 */
/* Prototypes. */
extern void bgp_debug_init(void);
extern void bgp_packet_dump(struct stream *s);

extern int debug(unsigned int option);

/*
 * ===== 调试状态：两套并行的全局量(每个大类一个位掩码) =====
 * conf_bgp_debug_* 为“配置态”，会被 bgp_config_write_debug 写入配置持久化；
 * 每个变量是一份掩码，具体子位见下方 BGP_DEBUG_ 开头的宏
 * (例如 BGP_DEBUG_UPDATE_IN 对应 update 大类的 IN 子位)。
 */
extern unsigned long conf_bgp_debug_as4;
extern unsigned long conf_bgp_debug_neighbor_events;
extern unsigned long conf_bgp_debug_packet;
extern unsigned long conf_bgp_debug_keepalive;
extern unsigned long conf_bgp_debug_update;
extern unsigned long conf_bgp_debug_bestpath;
extern unsigned long conf_bgp_debug_zebra;
extern unsigned long conf_bgp_debug_nht;
extern unsigned long conf_bgp_debug_update_groups;
extern unsigned long conf_bgp_debug_vpn;
extern unsigned long conf_bgp_debug_flowspec;
extern unsigned long conf_bgp_debug_labelpool;
extern unsigned long conf_bgp_debug_pbr;
extern unsigned long conf_bgp_debug_graceful_restart;
extern unsigned long conf_bgp_debug_evpn_mh;
extern unsigned long conf_bgp_debug_bfd;
extern unsigned long conf_bgp_debug_cond_adv;
extern unsigned long conf_bgp_debug_aggregate;

/*
 * term_bgp_debug_* 为“终端态”，仅运行期有效、不写入配置。
 * 运行期判定(BGP_DEBUG 宏)读的就是这一套：在 enable 提示符下敲 debug 只改 term，
 * 在 config 节点下敲则经 DEBUG_ON 同时改 conf 与 term。
 */
extern unsigned long term_bgp_debug_as4;
extern unsigned long term_bgp_debug_neighbor_events;
extern unsigned long term_bgp_debug_packet;
extern unsigned long term_bgp_debug_keepalive;
extern unsigned long term_bgp_debug_update;
extern unsigned long term_bgp_debug_bestpath;
extern unsigned long term_bgp_debug_zebra;
extern unsigned long term_bgp_debug_nht;
extern unsigned long term_bgp_debug_update_groups;
extern unsigned long term_bgp_debug_vpn;
extern unsigned long term_bgp_debug_flowspec;
extern unsigned long term_bgp_debug_labelpool;
extern unsigned long term_bgp_debug_pbr;
extern unsigned long term_bgp_debug_graceful_restart;
extern unsigned long term_bgp_debug_evpn_mh;
extern unsigned long term_bgp_debug_bfd;
extern unsigned long term_bgp_debug_cond_adv;
extern unsigned long term_bgp_debug_aggregate;

/*
 * 按对象细化的过滤表：当某大类调试开启后，可进一步限定只对表内邻居 / 前缀生效。
 * 表元素为 struct bgp_debug_filter；表为空表示“对该类全部生效”。
 */
extern struct list *bgp_debug_neighbor_events_peers;
extern struct list *bgp_debug_keepalive_peers;
extern struct list *bgp_debug_update_in_peers;
extern struct list *bgp_debug_update_out_peers;
extern struct list *bgp_debug_update_prefixes;
extern struct list *bgp_debug_bestpath_prefixes;
extern struct list *bgp_debug_zebra_prefixes;
extern struct list *bgp_debug_aggregate_prefixes;

/*
 * bgp_debug_filter —— 一条调试过滤条目：
 *   host        : 邻居标识(IP / 接口名 / 主机名)，按邻居过滤时使用；
 *   plist_name  : 引用到的 prefix-list 名称；
 *   plist_v4/v6 : 解析出的 v4 / v6 prefix-list，用于判断前缀是否命中；
 *   p           : 指定的前缀(按前缀过滤时使用，含 EVPN 各 route-type)。
 * host 与 p 互斥：一条条目要么按邻居，要么按前缀。
 */
struct bgp_debug_filter {
	char *host;
	char *plist_name;
	struct prefix_list *plist_v4;
	struct prefix_list *plist_v6;
	struct prefix *p;
};

/*
 * 各大类下的子位定义(位掩码)，命名规律为 BGP_DEBUG_<子项>。
 * 不同大类的子位数值可以重复(都从 0x01 起)，因为它们是各自独立的掩码：
 * 判定时用对应大类的状态量按位与，互不干扰。
 *   例：AS4 与 AS4_SEGMENT 属 as4 大类；UPDATE_IN/OUT/PREFIX/DETAIL 属 update 大类。
 */
#define BGP_DEBUG_AS4                 0x01
#define BGP_DEBUG_AS4_SEGMENT         0x02

#define BGP_DEBUG_BESTPATH            0x01
#define BGP_DEBUG_NEIGHBOR_EVENTS     0x01
#define BGP_DEBUG_NEIGHBOR_EVENTS_DETAIL 0x02
#define BGP_DEBUG_PACKET              0x01
#define BGP_DEBUG_KEEPALIVE           0x01
#define BGP_DEBUG_UPDATE_IN           0x01
#define BGP_DEBUG_UPDATE_OUT          0x02
#define BGP_DEBUG_UPDATE_PREFIX       0x04
#define BGP_DEBUG_UPDATE_DETAIL       0x08
#define BGP_DEBUG_ZEBRA               0x01
#define BGP_DEBUG_NHT                 0x01
#define BGP_DEBUG_UPDATE_GROUPS       0x01
#define BGP_DEBUG_VPN_LEAK_FROM_VRF   0x01
#define BGP_DEBUG_VPN_LEAK_TO_VRF     0x02
#define BGP_DEBUG_VPN_LEAK_RMAP_EVENT 0x04
#define BGP_DEBUG_VPN_LEAK_LABEL      0x08
#define BGP_DEBUG_FLOWSPEC            0x01
#define BGP_DEBUG_LABELPOOL           0x01
#define BGP_DEBUG_PBR                 0x01
#define BGP_DEBUG_PBR_ERROR           0x02
#define BGP_DEBUG_EVPN_MH_ES          0x01
#define BGP_DEBUG_EVPN_MH_RT          0x02

#define BGP_DEBUG_GRACEFUL_RESTART     0x01

#define BGP_DEBUG_BFD_LIB             0x01
#define BGP_DEBUG_COND_ADV 0x01
#define BGP_DEBUG_AGGREGATE	      0x01

/*
 * CONF_DEBUG_ON/OFF：只改“配置态”，用于在 config 节点设置持久化开关。
 * 展开为 conf_bgp_debug_<大类> 对 BGP_DEBUG_<子位> 的置位 / 清位。
 */
#define CONF_DEBUG_ON(a, b)	(conf_bgp_debug_ ## a |= (BGP_DEBUG_ ## b))
#define CONF_DEBUG_OFF(a, b)	(conf_bgp_debug_ ## a &= ~(BGP_DEBUG_ ## b))

/*
 * TERM_DEBUG_ON/OFF：只改“终端态”，用于在 enable 提示符下做临时调试。
 */
#define TERM_DEBUG_ON(a, b)	(term_bgp_debug_ ## a |= (BGP_DEBUG_ ## b))
#define TERM_DEBUG_OFF(a, b)	(term_bgp_debug_ ## a &= ~(BGP_DEBUG_ ## b))

/*
 * DEBUG_ON/OFF：同时改配置态与终端态，把某子位一次性打开 / 关闭。
 * 这是让“在 config 节点配置”与“在 enable 节点立即生效”统一的粘合剂。
 */
#define DEBUG_ON(a, b)                                                         \
	do {                                                                   \
		CONF_DEBUG_ON(a, b);                                           \
		TERM_DEBUG_ON(a, b);                                           \
	} while (0)
#define DEBUG_OFF(a, b)                                                        \
	do {                                                                   \
		CONF_DEBUG_OFF(a, b);                                          \
		TERM_DEBUG_OFF(a, b);                                          \
	} while (0)

/*
 * BGP_DEBUG(a,b)      : 运行期判定——读终端态，看大类 a 的子位 b 是否打开。
 * CONF_BGP_DEBUG(a,b) : 配置判定——读配置态，供写 running-config 时决定是否输出。
 * 两者都套 unlikely()，提示编译器等闲分支(调试默认关闭)。
 */
#define BGP_DEBUG(a, b)	     (unlikely(term_bgp_debug_##a & BGP_DEBUG_##b))
#define CONF_BGP_DEBUG(a, b) (unlikely(conf_bgp_debug_##a & BGP_DEBUG_##b))

/*
 * 字符串表：把状态号、报文类型、GR 模式翻译为人可读文本，供 debug / show 输出。
 */
extern const char *const bgp_type_str[];
extern const char *const bgp_global_gr_mode_str[];

/*
 * 对外判定与打印接口：
 *   bgp_debug_neighbor_events / keepalive / update / bestpath / zebra / aggregate：
 *       结合“大类掩码 + 邻居表 + 前缀表”回答“这次事件该不该打印”。
 *   bgp_debug_rdpfxpath2str : 把 RD / 前缀 / 路径 ID 等拼成展示字符串。
 *   bgp_notify_* : NOTIFICATION 报文的文本化与打印。
 */
extern bool bgp_dump_attr(struct attr *attr, char *buf, size_t size);
extern bool bgp_debug_peer_updout_enabled(char *host);
extern const char *bgp_notify_code_str(char code);
extern const char *bgp_notify_subcode_str(char code, char subcode);
extern void bgp_notify_print(const struct peer *peer, const struct bgp_notify *bgp_notify,
			     const char *direct, bool hard_reset);

extern const struct message bgp_status_msg[];
extern bool bgp_debug_neighbor_events(const struct peer *peer);
extern bool bgp_debug_keepalive(const struct peer *peer);
extern bool bgp_debug_update(const struct peer *peer, const struct prefix *p,
			     struct update_group *updgrp, unsigned int inbound);
extern bool bgp_debug_bestpath(struct bgp_dest *dest);
extern bool bgp_debug_zebra(const struct prefix *p);
extern bool bgp_debug_aggregate(const struct prefix *p);

extern const char *bgp_debug_rdpfxpath2str(
	afi_t afi, safi_t safi, const struct prefix_rd *prd,
	union prefixconstptr pu, mpls_label_t *label, uint8_t num_labels,
	int addpath_valid, uint32_t addpath_id,
	struct bgp_route_evpn *overlay_index, char *str, int size);
const char *bgp_notify_admin_message(char *buf, size_t bufsz, uint8_t *data,
				     size_t datalen);

#endif /* _QUAGGA_BGP_DEBUG_H */
