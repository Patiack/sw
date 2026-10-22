// SPDX-License-Identifier: GPL-2.0-or-later
/* Pseudowire definitions
 * Copyright (C) 2016 Volta Networks, Inc.
 */

/*
 * 文件导读（lib/pw.h）
 *
 * 本文件定义伪线（Pseudowire，PW）相关的常量与协议字段结构。伪线用于
 * 在包交换网（PSN，如 MPLS / L2TPv3）之上承载二层业务（如以太网），
 * 是 L2VPN / VPLS 的底层承载，由 BGP 或 LDP 作为信令协议建立。
 *
 * 核心内容：
 *   - L2VPN_NAME_LEN：L2VPN 名称最大长度。
 *   - PW_TYPE_*：伪线类型码，LDP 与 BGP 信令共用同一套值（定义见 RFC 4446）。
 *   - F_PSEUDOWIRE_CWORD：伪线控制字标志。
 *   - PW_*：伪线状态 TLV 的取值与故障位（转发 / 未转发 / 本地 / PSN 故障）。
 *   - union pw_protocol_fields：按信令协议区分的伪线附加信息，目前只有
 *     LDP 分支（包含 LSR ID、PW ID、VPN 名称）。
 *
 * 与其他模块的关系：bgpd 的 EVPN / VPLS 代码、ldpd 以及 zebra 的伪线管理
 * 共享这些取值，以保证两端对类型码与状态位理解一致。
 *
 * 阅读顺序：先看长度与类型码宏，再看标志与状态 TLV，最后看协议字段联合体。
 */

#ifndef _FRR_PW_H
#define _FRR_PW_H

#ifdef __cplusplus
extern "C" {
#endif

/* L2VPN 名称最大长度（含结尾空字符）。 */
/* L2VPN name length. */
#define L2VPN_NAME_LEN		32

/* 伪线类型码：LDP 与 BGP 信令使用同一套取值，编号来自 RFC 4446。
 * TAGGED 表示带 VLAN 标签的以太网，ETHERNET 为原始以太网，WILDCARD 为通配。 */
/* Pseudowire type - LDP and BGP use the same values. */
#define PW_TYPE_ETHERNET_TAGGED	0x0004	/* RFC 4446 */
#define PW_TYPE_ETHERNET	0x0005	/* RFC 4446 */
#define PW_TYPE_WILDCARD	0x7FFF	/* RFC 4863, RFC 6668 */

/* 伪线标志位：CWORD 表示使用控制字（Control Word）。 */
/* Pseudowire flags. */
#define F_PSEUDOWIRE_CWORD	0x01

/* 伪线状态 TLV 取值与故障位：PW_FORWARDING 为转发，其余位表示各类故障。 */
/* Pseudowire status TLV */
#define PW_FORWARDING 0
#define PW_NOT_FORWARDING (1 << 0)
#define PW_LOCAL_RX_FAULT (1 << 1)
#define PW_LOCAL_TX_FAULT (1 << 2)
#define PW_PSN_RX_FAULT (1 << 3)
#define PW_PSN_TX_FAULT (1 << 4)

/*
 * Protocol-specific information about the pseudowire.
 */
/* 按信令协议区分的伪线附加信息联合体；当前仅实现 LDP 分支。
 * LDP 分支含 LSR ID、PW ID 与 VPN 名称。 */
union pw_protocol_fields {
	struct {
		struct in_addr lsr_id;
		uint32_t pwid;
		char vpn_name[L2VPN_NAME_LEN];
	} ldp;
};

#ifdef __cplusplus
}
#endif

#endif /* _FRR_PW_H */
