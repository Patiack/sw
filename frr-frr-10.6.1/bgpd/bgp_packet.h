// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP packet management header.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_packet.h —— 报文收发层的对外接口（bgp_packet.c 的头文件）
 * ============================================================================
 * 职责与定位：声明“构造并发送/接收并解析 BGP 报文”的全部入口，以及线格式常量。
 *   实现全在 bgp_packet.c。本头文件本身没有逻辑，但集中了几组读代码必须认识的东西：
 *
 *   1) 两个 hook（bgp_packet_dump / bgp_packet_send）：给 BMP 等模块旁路观测报文用。
 *   2) 线格式长度常量（BGP_NLRI_LENGTH / BGP_TOTAL_ATTR_LEN / BGP_UNFEASIBLE_LEN）：
 *      解析 UPDATE 时按这些字段宽度推进指针，写错就会把后续字节读串。
 *   3) ORF 公共部分的位定义（ORF_COMMON_PART_*）：注意这里用 1 个字节同时编码
 *      “动作(ADD/REMOVE)”与“语义(PERMIT/DENY)”——高位是动作、0x20 位是 permit/deny，
 *      所以 REMOVE_ALL(0xC0) 是“两个高位都置”，不是独立枚举。
 *   4) BGP_UPDATE_EOR_PKT：构造并发送 End-of-RIB 的小宏。
 *
 * 阅读提示：本文件里的 *_send 分两类——**直写套接字**的（bgp_notify_send 等紧急报文）
 * 和**入 obuf 队列**的（走 bgp_packet_add，由 IO 线程异步写出）。混淆这两者会
 * 误判“发送是否已完成”。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_PACKET_H
#define _QUAGGA_BGP_PACKET_H

#include "hook.h"

/* 【结构】bgp_enhe_capability —— Extended Next Hop Encoding 能力(码 5)的一项。
 * 含义：“对于 (afi, safi) 的路由，我可以接受用 nh_afi 表示下一跳”。
 * 典型用途：IPv4 单播路由携带 IPv6 下一跳(afi=AFI_IP, safi=SAFI_UNICAST,
 * nh_afi=AFI_IP6)——RFC5549。 */
struct bgp_enhe_capability {
	uint16_t afi;
	uint16_t safi;
	uint16_t nh_afi;
};

DECLARE_HOOK(bgp_packet_dump,
		(struct peer *peer, uint8_t type, bgp_size_t size,
			struct stream *s),
		(peer, type, size, s));

DECLARE_HOOK(bgp_packet_send,
		(struct peer *peer, uint8_t type, bgp_size_t size,
			struct stream *s),
		(peer, type, size, s));

/* UPDATE 报文内部的字段宽度（字节），解析时按此推进指针：
 *   BGP_NLRI_LENGTH    : “撤销路由”段的每条前缀长度字段（1 字节）。
 *   BGP_TOTAL_ATTR_LEN : 路径属性总长度字段（2 字节）。
 *   BGP_UNFEASIBLE_LEN : MP_UNREACH_NLRI 的占位长度（2 字节，本实现恒写 0）。 */
#define BGP_NLRI_LENGTH       1U
#define BGP_TOTAL_ATTR_LEN    2U
#define BGP_UNFEASIBLE_LEN    2U

/* When to refresh */
/* 发 Route Refresh(RFC2918)时“什么时候刷”：
 *   REFRESH_IMMEDIATE : 立即请求对端重发（正常软重置）。
 *   REFRESH_DEFER     : 延迟——等对端的 EoR 或超时后再发，用于
 *                       “避免在收敛过程中反复冲刷”的场景（软重配/GR 配合）。 */
#define REFRESH_IMMEDIATE 1
#define REFRESH_DEFER     2

/* ORF Common part flag */
/* ORF 条目公共部分用【一个字节的两个区域】表达两个维度，不是四个独立枚举：
 *   低 7 位中的 0xC0 = 动作(Add/Remove/Remove-All)，0x20 位 = Permit/Deny。
 * 所以：
 *   ADD(0x00)     + PERMIT(0x00) = 0x00
 *   ADD(0x00)     + DENY(0x20)   = 0x20
 *   REMOVE(0x80)  + PERMIT(0x00) = 0x80
 *   REMOVE_ALL(0xC0)             = 0xC0（动作位两个都置 = 清空全部）
 * 读代码时若把 0xC0 当成“某种单独的 permit 取值”就对不上了。 */
#define ORF_COMMON_PART_ADD        0x00
#define ORF_COMMON_PART_REMOVE     0x80
#define ORF_COMMON_PART_REMOVE_ALL 0xC0
#define ORF_COMMON_PART_PERMIT     0x00
#define ORF_COMMON_PART_DENY       0x20

/* 【宏】BGP_UPDATE_EOR_PKT —— 构造并发送一个 End-of-RIB 报文(RFC4724 §2)。
 * 步骤：bgp_update_packet_eor() 造出 stream（失败返回 NULL，故必须判空），
 * 然后 bgp_packet_add() 压入该 peer 连接的 obuf 队列由 IO 线程写出。
 * EoR 是“该地址族我这边发完了”的标记，对端 GR/收敛判断依赖它。 */
#define BGP_UPDATE_EOR_PKT(_peer, _afi, _safi, _s)                             \
	do {                                                                   \
		_s = bgp_update_packet_eor(_peer, _afi, _safi);                \
		if (_s) {                                                      \
			bgp_packet_add(_peer->connection, _peer, _s);          \
		}                                                              \
	} while (0)

/* Packet send and receive function prototypes. */
/* 【按方向分类】便于建立索引：
 *   构造+发送 : bgp_open_make/bgp_open_send、bgp_keepalive_send、bgp_notify_send*、
 *               bgp_route_refresh_send、bgp_capability_send（动态能力协商）
 *   接收+解析 : bgp_packet 接收总入口是 bgp_process_packet()（由 IO 线程投递触发）；
 *               bgp_capability_receive、bgp_nlri_parse（UPDATE 里的 NLRI 部分）
 *   调度      : bgp_generate_updgrp_packets（把 updgrp 打好的包逐 peer 发出）
 *   特殊      : Hard Reset(RFC8538) 相关的 bgp_notify_*_hard_reset 与
 *               bgp_notify_decapsulate_hard_reset（把标准 NOTIFY 解封装成硬重置）
 * 【注意】bgp_notify_send 系列是**直写套接字**的（用于连接即将关闭前的紧急报文），
 * 而 bgp_generate_updgrp_packets 走的是 obuf 队列，二者路径不同。 */
/* 发送 KEEPALIVE 报文(仅 19 字节报文头)，用于刷新 hold timer、维持会话存活。 */
extern void bgp_keepalive_send(struct peer_connection *connection);
/* 构造本端 OPEN 报文写入新 stream 并返回：携带 version、本端 AS、holdtime、
 * router-id，以及本端能力(capability)。send_holdtime / local_as / id 由调用方给出。 */
extern struct stream *bgp_open_make(struct peer *peer, struct peer_connection *connection,
				    uint16_t send_holdtime, as_t local_as, struct in_addr *id);
/* 发送 OPEN 报文。 */
extern void bgp_open_send(struct peer_connection *connection);
/* 发送 NOTIFY(仅 code 与 sub_code，无附加数据)。通常用于会话即将关闭前的紧急通知。 */
extern void bgp_notify_send(struct peer_connection *connection, uint8_t code,
			    uint8_t sub_code);
/* 发送带附加数据的 NOTIFY(如回显出错的属性或数据，便于诊断)。 */
extern void bgp_notify_send_with_data(struct peer_connection *connection,
				      uint8_t code, uint8_t sub_code,
				      uint8_t *data, size_t datalen);
/* 对 IO 层检测到的非法报文发送 NOTIFY，code / sub_code / data 描述错误原因。 */
void bgp_notify_io_invalid(struct peer *peer, uint8_t code, uint8_t sub_code,
			   uint8_t *data, size_t datalen);
/* 发送 ROUTE-REFRESH：请求对端重新通告某 AFI 与 SAFI 的路由，可携带 ORF 类型、
 * when_to_refresh 与 subtype(如增强刷新 RFC7313)。 */
extern void bgp_route_refresh_send(struct peer *peer, afi_t afi, safi_t safi,
				   uint8_t orf_type, uint8_t when_to_refresh,
				   int remove, uint8_t subtype);
/* 动态能力协商(Dynamic Capability，RFC 9072)：运行中发送携带指定能力的 OPEN，
 * 用于在不中断会话的前提下协商新能力。 */
extern void bgp_capability_send(struct peer *peer, afi_t afi, safi_t safi,
				int capabilty_code, int action);

/* 接收并处理对端的能力 OPEN(含动态能力)，按能力码与 action 更新本端能力状态。 */
extern int bgp_capability_receive(struct peer_connection *connection,
				  struct peer *peer, bgp_size_t length);
/* 解析 UPDATE 报文体中的 NLRI(mp_withdraw 为真表示这是 MP_UNREACH 的撤销部分)，
 * 把解析出的前缀交给路由处理模块。 */
extern int bgp_nlri_parse(struct peer *peer, struct attr *attr,
			  struct bgp_nlri *nlri, bool mp_withdraw);

/* 对已重启完成的 peer 触发其待发更新的发送(GR 恢复收尾)。 */
extern void bgp_update_restarted_peers(struct peer *peer);
/* 检查 update-delay 是否到期，必要时结束收敛期并放行批量更新。 */
extern void bgp_check_update_delay(struct bgp *peer);

/* 在 stream 头部写入 BGP 报文头(全 1 的 marker 与给定 type)，返回该报文起点位置。 */
extern int bgp_packet_set_marker(struct stream *s, uint8_t type);
/* 回填 BGP 报文头里的长度字段(总长等于头加报文体的长度)。 */
extern void bgp_packet_set_size(struct stream *s);

/* 事件驱动：把各 update-group 打好的包逐个 peer 发出(走 obuf 队列)。 */
extern void bgp_generate_updgrp_packets(struct event *event);
/* 收包总入口：由 IO 线程投递事件触发，读取并分派处理一个 BGP 报文。 */
extern void bgp_process_packet(struct event *event);

/* 发送延迟的 EOR(End-of-RIB)标记(对端支持延迟 EOR 时使用)。 */
extern void bgp_send_delayed_eor(struct bgp *bgp);

/* 把收到的 NOTIFY 解封装为 Hard Reset(RFC 8538)：识别其中编码的硬重置信息，
 * 返回对应的 bgp_notify 结构。 */
extern struct bgp_notify
bgp_notify_decapsulate_hard_reset(struct bgp_notify *notify);
/* 判断该 peer 是否已收到过 GR 通知(RFC 8538 风格)，用于重启判定。 */
extern bool bgp_has_graceful_restart_notification(struct peer *peer);
/* 发送 Hard Reset 通知(RFC 8538)：以标准 NOTIFY 加封装数据表达硬重置。 */
extern bool bgp_notify_send_hard_reset(struct peer *peer, uint8_t code,
				       uint8_t subcode);
/* 判断收到的 NOTIFY 是否为 Hard Reset 请求(依据 code 与 subcode)。 */
extern bool bgp_notify_received_hard_reset(struct peer *peer, uint8_t code,
					   uint8_t subcode);

#endif /* _QUAGGA_BGP_PACKET_H */
