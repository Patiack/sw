// SPDX-License-Identifier: GPL-2.0-or-later
/* mlag header.
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *                    Donald Sharp
 */
/*
 * ============================================================
 * 【文件导读】mlag.h —— MLAG(跨设备链路聚合)公共头文件
 * ============================================================
 *
 * 一、职责
 *   定义 Zebra 内部 MLAG 子系统的“数据契约”：协议消息编号、
 *   角色 / 状态枚举、各消息载荷结构体，以及供 zebra、pimd 等
 *   模块解码这些消息的函数原型。本文件只有声明，没有可执行逻辑。
 *
 * 二、核心数据结构
 *   - enum mlag_role / mlag_state / mlag_frr_state / mlag_owner：
 *     描述本端角色、链路运行状态、FRR 侧上 / 下线状态，以及
 *     mroute 条目的归属者(接口 / VXLAN)。
 *   - enum mlag_msg_type：全部 MLAG 消息的类型编号，必须与外部
 *     伙伴进程的 mlag.proto 保持一致(注册依赖此编号)。
 *   - struct mlag_msg：消息通用头部；data[0] 为柔性数组接变长载荷。
 *   - struct mlag_status / mlag_vxlan / mlag_mroute_add /
 *     mlag_mroute_del / mlag_frr_status：各类消息载荷。
 *
 * 三、与其他模块的关系
 *   - mlag.c 实现本文件声明的 mlag_lib_decode_* 解码函数。
 *   - zebra 从 stream 读入对端消息后调用这些函数解析。
 *
 * 四、阅读顺序
 *   先看枚举与结构体(数据模型)，再看文末 extern 函数原型(对外
 *   接口)，最后配合 mlag.c 看解码实现。
 *
 * 五、易踩的坑
 *   - struct mlag_msg 带 __attribute__((packed))，不可加对齐填充。
 *   - data[0] 是零长数组写法，sizeof(struct mlag_msg) 不含载荷。
 *   - 结构体内 IP 均为 uint32_t，隐含主机 / 网络字节序约定，跨机
 *     传递前须确认转换。
 *   - 消息编号有跳号(如 MLAG_PIM_CFG_DUMP=10)，属历史兼容。
 * ============================================================
 */
#ifndef __MLAG_H__
#define __MLAG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 依赖：接口表(if)、VRF 表(vrf)、字节流编解码(stream) */
#include "lib/if.h"
#include "lib/vrf.h"
#include "lib/stream.h"

/* MLAG 消息不带任何载荷(空消息)时的标记值 */
#define MLAG_MSG_NULL_PAYLOAD 0
/* 该消息不采用批量(batch)发送方式 */
#define MLAG_MSG_NO_BATCH 1
/* MLAG 单条消息 / 读写缓冲区的字节上限 */
#define MLAG_BUF_LIMIT 2048

/* MLAG 本端角色：NONE 未参与，PRIMARY 主，SECONDARY 备 */
enum mlag_role {
	MLAG_ROLE_NONE,
	MLAG_ROLE_PRIMARY,
	MLAG_ROLE_SECONDARY
};

/* MLAG 链路运行状态：DOWN 未运行，RUNNING 已建立对端连接 */
enum mlag_state {
	MLAG_STATE_DOWN,
	MLAG_STATE_RUNNING,
};

/* FRR 侧 MLAG 状态：NONE 未知，DOWN 下线，UP 在线 */
enum mlag_frr_state {
	MLAG_FRR_STATE_NONE,
	MLAG_FRR_STATE_DOWN,
	MLAG_FRR_STATE_UP,
};

/* mroute 条目归属者：NONE 无，INTERFACE 接口产生，VXLAN 由 VXLAN 产生 */
enum mlag_owner {
	MLAG_OWNER_NONE,
	MLAG_OWNER_INTERFACE,
	MLAG_OWNER_VXLAN,
};

/*
 * This message definition should match mlag.proto
 * Because message registration is based on this
 */
/* MLAG 协议消息类型编号(见上方说明：须与对端 mlag.proto 一致) */
enum mlag_msg_type {
	MLAG_MSG_NONE = 0,
	MLAG_REGISTER = 1,
	MLAG_DEREGISTER = 2,
	MLAG_STATUS_UPDATE = 3,
	MLAG_MROUTE_ADD = 4,
	MLAG_MROUTE_DEL = 5,
	MLAG_DUMP = 6,
	MLAG_MROUTE_ADD_BULK = 7,
	MLAG_MROUTE_DEL_BULK = 8,
	MLAG_PIM_CFG_DUMP = 10,
	MLAG_VXLAN_UPDATE = 11,
	MLAG_PEER_FRR_STATUS = 12,
};

/* 对端 FRR 的上 / 下线状态消息载荷 */
struct mlag_frr_status {
	enum mlag_frr_state frr_state;
};

/* MLAG 状态通告载荷：peerlink 三层接口名 / 本端角色 / 对端状态 */
struct mlag_status {
	char peerlink_rif[IFNAMSIZ];
	enum mlag_role my_role;
	enum mlag_state peer_state;
};

/* mlag_role2str 输出缓冲区的建议大小 */
#define MLAG_ROLE_STRSIZE 16

/* VXLAN 更新载荷：任播 IP 与本机 IP(网络层地址) */
struct mlag_vxlan {
	uint32_t anycast_ip;
	uint32_t local_ip;
};

/* 组播路由(mroute)新增消息载荷，供对端同步 (S,G) 条目 */
struct mlag_mroute_add {
	char vrf_name[VRF_NAMSIZ];
	uint32_t source_ip;
	uint32_t group_ip;
	uint32_t cost_to_rp;
	enum mlag_owner owner_id;
	bool am_i_dr;
	bool am_i_dual_active;
	vrf_id_t vrf_id;
	char intf_name[IFNAMSIZ];
};

/* 组播路由(mroute)删除消息载荷 */
struct mlag_mroute_del {
	char vrf_name[VRF_NAMSIZ];
	uint32_t source_ip;
	uint32_t group_ip;
	enum mlag_owner owner_id;
	vrf_id_t vrf_id;
	char intf_name[IFNAMSIZ];
};

/* MLAG 消息通用头部；data 为柔性数组，紧跟变长载荷 */
struct mlag_msg {
	enum mlag_msg_type msg_type;
	uint16_t data_len;
	uint16_t msg_cnt;
	uint8_t data[0];
} __attribute__((packed));


/*
 * 以下为本头文件对外导出的声明(实现见 mlag.c)：
 * 一组 mlag_lib_decode_* 负责把 stream 中的字节流解码为上面的结构体。
 */

/* 把 enum mlag_role 转成可打印字符串，写入 buf */
extern char *mlag_role2str(enum mlag_role role, char *buf, size_t size);
extern char *mlag_lib_msgid_to_str(enum mlag_msg_type msg_type, char *buf,
				   size_t size);
/* 解码消息通用头部，返回类型编号 / 载荷长度 / 消息计数 */
extern int mlag_lib_decode_mlag_hdr(struct stream *s, struct mlag_msg *msg,
				    size_t *length);
/* 解码 mroute 新增载荷 */
extern int mlag_lib_decode_mroute_add(struct stream *s,
				      struct mlag_mroute_add *msg,
				      size_t *length);
/* 解码 mroute 删除载荷 */
extern int mlag_lib_decode_mroute_del(struct stream *s,
				      struct mlag_mroute_del *msg,
				      size_t *length);
/* 解码 MLAG 状态通告载荷 */
extern int mlag_lib_decode_mlag_status(struct stream *s,
				       struct mlag_status *msg);
/* 解码 VXLAN 更新载荷 */
extern int mlag_lib_decode_vxlan_update(struct stream *s,
					struct mlag_vxlan *msg);
/* 解码对端 FRR 状态载荷 */
extern int mlag_lib_decode_frr_status(struct stream *s,
				      struct mlag_frr_status *msg);
#ifdef __cplusplus
}
#endif

#endif
