// SPDX-License-Identifier: GPL-2.0-or-later
/* mlag generic code.
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *                    Donald Sharp
 */
/*
 * ============================================================
 * 【文件导读】mlag.c —— MLAG(跨设备链路聚合)的通用脚手架
 * ============================================================
 *
 * 一、职责
 *   本文件是 Zebra 内部 MLAG 支持的“通用层”：提供角色 / 消息名到
 *   字符串的转换，以及把对端 MLAG 进程发来的字节流解码成 mlag.h
 *   中定义的结构体。它不含协议状态机，只是被上层(zebra 的 MLAG
 *   处理逻辑、pimd 等)复用的解析工具。
 *
 * 二、核心机制
 *   - mlag_role2str / mlag_lib_msgid_to_str：枚举到可打印字符串。
 *   - mlag_lib_decode_*：基于 lib/stream 的 STREAM_GET* 系列宏，
 *     从 stream 中按网络字节序读字段，失败统一 goto stream_failure。
 *   - 各 *_LENGTH 宏给出对应载荷的期望字节数，用于长度校验。
 *
 * 三、与其他模块的关系
 *   - 依赖 lib/stream.h 的 STREAM_GETL(4 字节)/GETW(2 字节)/
 *     GETC(1 字节)/GET(变长)等宏；这些宏自带大端到主机序转换。
 *   - 结构体定义来自 mlag.h，字段顺序即线上顺序，务必一致。
 *
 * 四、阅读顺序
 *   先看两个 *2str，再按解码函数顺序读 hdr -> mroute_add ->
 *   mroute_del -> status -> vxlan_update -> frr_status。
 *
 * 五、易踩的坑
 *   - STREAM_GET* 内部用 goto stream_failure 跳到函数末尾标签，
 *     故每个解码函数必须以 stream_failure 标签收尾。
 *   - LIB_MLAG_HDR_LENGTH 固定为 8(=4+2+2)，对应 struct mlag_msg。
 *   - 长度校验用的 *length 是“去掉头部后”的剩余载荷长度。
 * ============================================================
 */
#include <zebra.h>

#include <mlag.h>

/* 把 enum mlag_role 转成 "NONE"/"PRIMARY"/"SECONDARY" 写入 buf 并返回 buf */
char *mlag_role2str(enum mlag_role role, char *buf, size_t size)
{
	switch (role) {
	case MLAG_ROLE_NONE:
		snprintf(buf, size, "NONE");
		break;
	case MLAG_ROLE_PRIMARY:
		snprintf(buf, size, "PRIMARY");
		break;
	case MLAG_ROLE_SECONDARY:
		snprintf(buf, size, "SECONDARY");
		break;
	}

	return buf;
}

/* 把消息类型编号转成可读名字；未知编号输出 "Unknown <n>" */
char *mlag_lib_msgid_to_str(enum mlag_msg_type msg_type, char *buf, size_t size)
{
	switch (msg_type) {
	case MLAG_REGISTER:
		snprintf(buf, size, "Register");
		break;
	case MLAG_DEREGISTER:
		snprintf(buf, size, "De-Register");
		break;
	case MLAG_MROUTE_ADD:
		snprintf(buf, size, "Mroute add");
		break;
	case MLAG_MROUTE_DEL:
		snprintf(buf, size, "Mroute del");
		break;
	case MLAG_DUMP:
		snprintf(buf, size, "Mlag Replay");
		break;
	case MLAG_MROUTE_ADD_BULK:
		snprintf(buf, size, "Mroute Add Batch");
		break;
	case MLAG_MROUTE_DEL_BULK:
		snprintf(buf, size, "Mroute Del Batch");
		break;
	case MLAG_STATUS_UPDATE:
		snprintf(buf, size, "Mlag Status");
		break;
	case MLAG_VXLAN_UPDATE:
		snprintf(buf, size, "Mlag vxlan update");
		break;
	case MLAG_PEER_FRR_STATUS:
		snprintf(buf, size, "Mlag Peer FRR Status");
		break;
	case MLAG_PIM_CFG_DUMP:
		snprintf(buf, size, "Mlag Pim Configuration Dump");
		break;
	case MLAG_MSG_NONE:
		snprintf(buf, size, "Unknown %d", msg_type);
		break;
	}
	return buf;
}


/*
 * 解码 8 字节通用消息头：msg_type(4) / data_len(2) / msg_cnt(2)。
 * 入参 length 进入时为整包长度，返回时被改写为剩余载荷长度；
 * 出错返回 -1。
 */
int mlag_lib_decode_mlag_hdr(struct stream *s, struct mlag_msg *msg,
			     size_t *length)
{
/* 通用头固定长度：msg_type(4) + data_len(2) + msg_cnt(2) = 8 字节 */
#define LIB_MLAG_HDR_LENGTH 8
	if (s == NULL || msg == NULL)
		return -1;

	*length = stream_get_endp(s);

	if (*length < LIB_MLAG_HDR_LENGTH)
		return -1;

	*length -= LIB_MLAG_HDR_LENGTH;

	STREAM_GETL(s, msg->msg_type);
	STREAM_GETW(s, msg->data_len);
	STREAM_GETW(s, msg->msg_cnt);

	return 0;
stream_failure:
	return -1;
}

/* mroute 新增载荷期望字节数：VRF 名 + 接口名 + 5 个 4 字节字段
 * (source_ip/group_ip/cost_to_rp/owner_id/vrf_id) + 2 个 1 字节标志 */
#define MLAG_MROUTE_ADD_LENGTH                                                 \
	(VRF_NAMSIZ + IFNAMSIZ + 4 + 4 + 4 + 4 + 1 + 1 + 4)

/* 解码 mroute 新增载荷；剩余长度不足 MLAG_MROUTE_ADD_LENGTH 时失败 */
int mlag_lib_decode_mroute_add(struct stream *s, struct mlag_mroute_add *msg,
			       size_t *length)
{
	if (s == NULL || msg == NULL || *length < MLAG_MROUTE_ADD_LENGTH)
		return -1;

	STREAM_GET(msg->vrf_name, s, VRF_NAMSIZ);
	STREAM_GETL(s, msg->source_ip);
	STREAM_GETL(s, msg->group_ip);
	STREAM_GETL(s, msg->cost_to_rp);
	STREAM_GETL(s, msg->owner_id);
	STREAM_GETC(s, msg->am_i_dr);
	STREAM_GETC(s, msg->am_i_dual_active);
	STREAM_GETL(s, msg->vrf_id);
	STREAM_GET(msg->intf_name, s, IFNAMSIZ);

	return 0;
stream_failure:
	return -1;
}

/* mroute 删除载荷期望字节数：VRF 名 + 接口名 + 4 个 4 字节字段
 * (source_ip/group_ip/owner_id/vrf_id) */
#define MLAG_MROUTE_DEL_LENGTH (VRF_NAMSIZ + IFNAMSIZ + 4 + 4 + 4 + 4)

/* 解码 mroute 删除载荷；剩余长度不足 MLAG_MROUTE_DEL_LENGTH 时失败 */
int mlag_lib_decode_mroute_del(struct stream *s, struct mlag_mroute_del *msg,
			       size_t *length)
{
	if (s == NULL || msg == NULL || *length < MLAG_MROUTE_DEL_LENGTH)
		return -1;

	STREAM_GET(msg->vrf_name, s, VRF_NAMSIZ);
	STREAM_GETL(s, msg->source_ip);
	STREAM_GETL(s, msg->group_ip);
	STREAM_GETL(s, msg->owner_id);
	STREAM_GETL(s, msg->vrf_id);
	STREAM_GET(msg->intf_name, s, IFNAMSIZ);

	return 0;
stream_failure:
	return -1;
}

/* 解码 MLAG 状态通告：peerlink 接口名 / 本端角色 / 对端状态 */
int mlag_lib_decode_mlag_status(struct stream *s, struct mlag_status *msg)
{
	if (s == NULL || msg == NULL)
		return -1;

	STREAM_GET(msg->peerlink_rif, s, IFNAMSIZ);
	STREAM_GETL(s, msg->my_role);
	STREAM_GETL(s, msg->peer_state);
	return 0;
stream_failure:
	return -1;
}

/* 解码 VXLAN 更新：任播 IP 与本机 IP */
int mlag_lib_decode_vxlan_update(struct stream *s, struct mlag_vxlan *msg)
{
	if (s == NULL || msg == NULL)
		return -1;

	STREAM_GETL(s, msg->anycast_ip);
	STREAM_GETL(s, msg->local_ip);
	return 0;

stream_failure:
	return -1;
}

/* 解码对端 FRR 的上 / 下线状态 */
int mlag_lib_decode_frr_status(struct stream *s, struct mlag_frr_status *msg)
{
	if (s == NULL || msg == NULL)
		return -1;

	STREAM_GETL(s, msg->frr_state);
	return 0;
stream_failure:
	return -1;
}
