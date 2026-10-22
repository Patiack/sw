// SPDX-License-Identifier: GPL-2.0-or-later
/* Zebra's client header.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * 【文件导读】zclient.h —— Zebra 客户端(ZAPI)公共接口头文件
 *
 * 本文件定义各路由守护进程(daemon，如 bgpd、ospfd、isisd 等)与 zebra 进程
 * 之间的通信协议 ZAPI 的数据结构与回调约定。zebra 掌管内核路由表 / 接口 /
 * 地址 / 邻居 / MPLS / VRF 等底层资源；其它守护进程通过 zclient 把算好的
 * 路由、标签、nexthop 组等下发(add/del)给 zebra，同时订阅(redistribute /
 * nexthop register) zebra 上报的底层事件。
 *
 * 关键组成：
 *   1) 消息类型枚举 zebra_message_types_t —— ZAPI 的命令字全集，与
 *      zclient.c 里的 command_types 字符串表一一对应，改动务必同步。
 *   2) 报文头 struct zmsghdr —— 固定 10 字节，含 length/marker/version/
 *      vrf_id/command，字节序按网络序编码。
 *   3) 回调声明宏 ZAPI_CALLBACK_ARGS 与 typedef zclient_handler —— 统一
 *      所有 ZAPI 上报处理函数的形参列表。
 *   4) 各类 zapi_xxx 结构体 —— 路由、nexthop、标签、pw、sr policy 等在
 *      线上的编解码载体，收发双方必须逐字段对齐。
 *   5) struct zclient —— 每个守护进程持有的客户端句柄，保存 socket、收发
 *      缓冲、redistribute 位图、回调函数指针与 handler 分发表。
 *
 * 建议阅读顺序：message 类型枚举 -> zmsghdr -> ZAPI_CALLBACK_ARGS ->
 * struct zclient -> 按需读 zapi_route / zapi_nexthop 等具体结构 ->
 * 最后对照 zclient.c 中 zclient_init / zclient_read 及各 *_send 函数。
 *
 * 注意：本文件大量结构体直接映射到线上字节流，字段顺序与宽度即协议，
 * 不能随意调整；新增字段需同步 zebra 侧的编解码实现。
 */

#ifndef _ZEBRA_ZCLIENT_H
#define _ZEBRA_ZCLIENT_H

struct zclient;

/* For struct zapi_route. */
#include "prefix.h"
#include "ipaddr.h"

/* For struct interface and struct connected. */
#include "if.h"

/* For vrf_bitmap_t. */
#include "vrf.h"

/* For union g_addr */
#include "nexthop.h"
/* For resilience */
#include "nexthop_group.h"

/* For union pw_protocol_fields */
#include "pw.h"

#include "mlag.h"
#include "srte.h"
#include "srv6.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zebra types. Used in Zserv message header. */
typedef uint16_t zebra_size_t;

/*
 * 报文尺寸与报文头相关常量：
 *   ZEBRA_MAX_PACKET_SIZ   单个 ZAPI 报文最大长度(16KB)，收发缓冲上限
 *   ZEBRA_SMALL_PACKET_SIZE 小报文阈值(200B)，用于快速路径优化
 *   ZEBRA_HEADER_SIZE      报文头固定长度(10 字节)
 *   ZAPI_TCP_PATHNAME      "@tcp" 特殊套接字名，首字符 @ 表示 Linux 抽象
 *                          套接字命名空间，用于走 TCP 而非 unix socket
 *   ZEBRA_IPSET_NAME_SIZE  ipset 条目名最大长度
 *   ZEBRA_IPTABLES_FORWARD / _DROP  iptables 动作(转发或丢弃)
 *   ZEBRA_FEC_REGISTER_LABEL / _LABEL_INDEX  FEC 注册命令标志
 */
/* Marker value used in new Zserv, in the byte location corresponding
 * the command value in the old zserv header. To allow old and new
 * Zserv headers to be distinguished from each other.
 */
#define ZEBRA_HEADER_MARKER              254

/* For input/output buffer to zebra. */
#define ZEBRA_MAX_PACKET_SIZ          16384U
#define ZEBRA_SMALL_PACKET_SIZE       200U

/* Zebra header size. */
#define ZEBRA_HEADER_SIZE             10

/* special socket path name to use TCP
 * @ is used as first character because that's abstract socket names on Linux
 */
#define ZAPI_TCP_PATHNAME             "@tcp"

/* IPset size name stands for the name of the ipset entry
 * that can be created by using some zapi interfaces
 */
#define ZEBRA_IPSET_NAME_SIZE   32

/* IPTable action is defined by two values: either
 * forward or drop
 */
#define ZEBRA_IPTABLES_FORWARD 0
#define ZEBRA_IPTABLES_DROP    1

/* Zebra FEC register command flags. */
#define ZEBRA_FEC_REGISTER_LABEL          0x1
#define ZEBRA_FEC_REGISTER_LABEL_INDEX    0x2

/* Client capabilities */
/*
 * 客户端能力协商取值：daemon 在握手阶段向 zebra 声明自身支持的扩展能力。
 *   ZEBRA_CLIENT_GR_CAPABILITIES       支持优雅重启(GR)，路由可短暂保留
 *   ZEBRA_CLIENT_ROUTE_UPDATE_COMPLETE 一轮路由更新已完成(用于 GR 收敛判断)
 *   ZEBRA_CLIENT_ROUTE_UPDATE_PENDING  一轮路由更新尚未完成
 *   ZEBRA_CLIENT_GR_DISABLE            明确关闭 GR
 *   ZEBRA_CLIENT_RIB_STALE_TIME        声明 RIB 陈旧(stale)保留时间
 * 这些取值通过 struct zapi_cap 携带在 ZEBRA_CLIENT_CAPABILITIES 报文中。
 */
enum zserv_client_capabilities {
	ZEBRA_CLIENT_GR_CAPABILITIES = 1,
	ZEBRA_CLIENT_ROUTE_UPDATE_COMPLETE = 2,
	ZEBRA_CLIENT_ROUTE_UPDATE_PENDING = 3,
	ZEBRA_CLIENT_GR_DISABLE = 4,
	ZEBRA_CLIENT_RIB_STALE_TIME
};

/* Macro to check if there GR enabled. */
#define ZEBRA_CLIENT_GR_ENABLED(X) (X == ZEBRA_CLIENT_GR_CAPABILITIES)

#define ZEBRA_SR_POLICY_NAME_MAX_LENGTH 100

extern struct sockaddr_storage zclient_addr;
extern socklen_t zclient_addr_len;

/*
 * ZAPI 消息类型全集（命令字）。daemon 与 zebra 之间的每条报文都用这里的
 * 一个取值标识语义，如 ZEBRA_ROUTE_ADD 表示下发路由、ZEBRA_INTERFACE_ADD
 * 表示上报接口新增、ZEBRA_NEXTHOP_UPDATE 表示 nexthop 解析结果回调等。
 * 约定：命令字按方向分为 daemon->zebra(请求/下发) 与 zebra->daemon(上报)，
 * 同一枚举里的编号即 zmsghdr.command 的线上取值，增删必须让 zebra 与各
 * daemon 同步重编译，否则编号错位会导致协议解析错乱。
 * 与 zclient.c 的 command_types 字符串表逐一对应，改动请一并更新。
 */
/* Zebra message types. Please update the corresponding
 * command_types array with any changes!
 */
typedef enum {
	ZEBRA_INTERFACE_ADD,
	ZEBRA_INTERFACE_DELETE,
	ZEBRA_INTERFACE_ADDRESS_ADD,
	ZEBRA_INTERFACE_ADDRESS_DELETE,
	ZEBRA_INTERFACE_UP,
	ZEBRA_INTERFACE_DOWN,
	ZEBRA_INTERFACE_SET_MASTER,
	ZEBRA_INTERFACE_SET_ARP,
	ZEBRA_INTERFACE_SET_PROTODOWN,
	ZEBRA_ROUTE_ADD,
	ZEBRA_ROUTE_DELETE,
	ZEBRA_ROUTE_NOTIFY_OWNER,
	ZEBRA_REDISTRIBUTE_ADD,
	ZEBRA_REDISTRIBUTE_DELETE,
	ZEBRA_REDISTRIBUTE_DEFAULT_ADD,
	ZEBRA_REDISTRIBUTE_DEFAULT_DELETE,
	ZEBRA_ROUTER_ID_ADD,
	ZEBRA_ROUTER_ID_DELETE,
	ZEBRA_ROUTER_ID_UPDATE,
	ZEBRA_HELLO,
	ZEBRA_CAPABILITIES,
	ZEBRA_NEXTHOP_REGISTER,
	ZEBRA_NEXTHOP_UNREGISTER,
	ZEBRA_NEXTHOP_UPDATE,
	ZEBRA_INTERFACE_NBR_ADDRESS_ADD,
	ZEBRA_INTERFACE_NBR_ADDRESS_DELETE,
	ZEBRA_INTERFACE_BFD_DEST_UPDATE,
	ZEBRA_BFD_DEST_REGISTER,
	ZEBRA_BFD_DEST_DEREGISTER,
	ZEBRA_BFD_DEST_UPDATE,
	ZEBRA_BFD_DEST_REPLAY,
	ZEBRA_REDISTRIBUTE_ROUTE_ADD,
	ZEBRA_REDISTRIBUTE_ROUTE_DEL,
	ZEBRA_VRF_ADD,
	ZEBRA_VRF_DELETE,
	ZEBRA_VRF_LABEL,
	ZEBRA_BFD_CLIENT_REGISTER,
	ZEBRA_BFD_CLIENT_DEREGISTER,
	ZEBRA_INTERFACE_ENABLE_RADV,
	ZEBRA_INTERFACE_DISABLE_RADV,
	ZEBRA_NEXTHOP_LOOKUP,
	ZEBRA_INTERFACE_LINK_PARAMS,
	ZEBRA_MPLS_LABELS_ADD,
	ZEBRA_MPLS_LABELS_DELETE,
	ZEBRA_MPLS_LABELS_REPLACE,
	ZEBRA_SR_POLICY_SET,
	ZEBRA_SR_POLICY_DELETE,
	ZEBRA_SR_POLICY_NOTIFY_STATUS,
	ZEBRA_IPMR_ROUTE_STATS,
	ZEBRA_LABEL_MANAGER_CONNECT,
	ZEBRA_LABEL_MANAGER_CONNECT_ASYNC,
	ZEBRA_GET_LABEL_CHUNK,
	ZEBRA_RELEASE_LABEL_CHUNK,
	ZEBRA_FEC_REGISTER,
	ZEBRA_FEC_UNREGISTER,
	ZEBRA_FEC_UPDATE,
	ZEBRA_ADVERTISE_DEFAULT_GW,
	ZEBRA_ADVERTISE_SVI_MACIP,
	ZEBRA_ADVERTISE_SUBNET,
	ZEBRA_ADVERTISE_ALL_VNI,
	ZEBRA_LOCAL_ES_ADD,
	ZEBRA_LOCAL_ES_DEL,
	ZEBRA_REMOTE_ES_VTEP_ADD,
	ZEBRA_REMOTE_ES_VTEP_DEL,
	ZEBRA_LOCAL_ES_EVI_ADD,
	ZEBRA_LOCAL_ES_EVI_DEL,
	ZEBRA_VNI_ADD,
	ZEBRA_VNI_DEL,
	ZEBRA_L3VNI_ADD,
	ZEBRA_L3VNI_DEL,
	ZEBRA_REMOTE_VTEP_ADD,
	ZEBRA_REMOTE_VTEP_DEL,
	ZEBRA_MACIP_ADD,
	ZEBRA_MACIP_DEL,
	ZEBRA_IP_PREFIX_ROUTE_ADD,
	ZEBRA_IP_PREFIX_ROUTE_DEL,
	ZEBRA_REMOTE_MACIP_ADD,
	ZEBRA_REMOTE_MACIP_DEL,
	ZEBRA_DUPLICATE_ADDR_DETECTION,
	ZEBRA_PW_ADD,
	ZEBRA_PW_DELETE,
	ZEBRA_PW_SET,
	ZEBRA_PW_UNSET,
	ZEBRA_PW_STATUS_UPDATE,
	ZEBRA_RULE_ADD,
	ZEBRA_RULE_DELETE,
	ZEBRA_RULE_NOTIFY_OWNER,
	ZEBRA_TABLE_MANAGER_CONNECT,
	ZEBRA_GET_TABLE_CHUNK,
	ZEBRA_RELEASE_TABLE_CHUNK,
	ZEBRA_IPSET_CREATE,
	ZEBRA_IPSET_DESTROY,
	ZEBRA_IPSET_ENTRY_ADD,
	ZEBRA_IPSET_ENTRY_DELETE,
	ZEBRA_IPSET_NOTIFY_OWNER,
	ZEBRA_IPSET_ENTRY_NOTIFY_OWNER,
	ZEBRA_IPTABLE_ADD,
	ZEBRA_IPTABLE_DELETE,
	ZEBRA_IPTABLE_NOTIFY_OWNER,
	ZEBRA_VXLAN_FLOOD_CONTROL,
	ZEBRA_VXLAN_SG_ADD,
	ZEBRA_VXLAN_SG_DEL,
	ZEBRA_VXLAN_SG_REPLAY,
	ZEBRA_MLAG_PROCESS_UP,
	ZEBRA_MLAG_PROCESS_DOWN,
	ZEBRA_MLAG_CLIENT_REGISTER,
	ZEBRA_MLAG_CLIENT_UNREGISTER,
	ZEBRA_MLAG_FORWARD_MSG,
	ZEBRA_NHG_ADD,
	ZEBRA_NHG_DEL,
	ZEBRA_NHG_NOTIFY_OWNER,
	ZEBRA_EVPN_REMOTE_NH_ADD,
	ZEBRA_EVPN_REMOTE_NH_DEL,
	ZEBRA_SRV6_LOCATOR_ADD,
	ZEBRA_SRV6_LOCATOR_DELETE,
	ZEBRA_SRV6_MANAGER_GET_LOCATOR_CHUNK,
	ZEBRA_SRV6_MANAGER_RELEASE_LOCATOR_CHUNK,
	ZEBRA_SRV6_MANAGER_GET_LOCATOR,
	ZEBRA_SRV6_MANAGER_GET_SRV6_SID,
	ZEBRA_SRV6_MANAGER_RELEASE_SRV6_SID,
	ZEBRA_ERROR,
	ZEBRA_CLIENT_CAPABILITIES,
	ZEBRA_OPAQUE_MESSAGE,
	ZEBRA_OPAQUE_REGISTER,
	ZEBRA_OPAQUE_UNREGISTER,
	ZEBRA_NEIGH_DISCOVER,
	ZEBRA_ROUTE_NOTIFY_REQUEST,
	ZEBRA_CLIENT_CLOSE_NOTIFY,
	ZEBRA_NEIGH_ADDED,
	ZEBRA_NEIGH_REMOVED,
	ZEBRA_NEIGH_GET,
	ZEBRA_NEIGH_REGISTER,
	ZEBRA_NEIGH_UNREGISTER,
	ZEBRA_NEIGH_IP_ADD,
	ZEBRA_NEIGH_IP_DEL,
	ZEBRA_CONFIGURE_ARP,
	ZEBRA_GRE_GET,
	ZEBRA_GRE_UPDATE,
	ZEBRA_GRE_SOURCE_SET,
	ZEBRA_TC_QDISC_INSTALL,
	ZEBRA_TC_QDISC_UNINSTALL,
	ZEBRA_TC_CLASS_ADD,
	ZEBRA_TC_CLASS_DELETE,
	ZEBRA_TC_FILTER_ADD,
	ZEBRA_TC_FILTER_DELETE,
	ZEBRA_OPAQUE_NOTIFY,
	ZEBRA_SRV6_SID_NOTIFY,
} zebra_message_types_t;
/* Zebra message types. Please update the corresponding
 * command_types array with any changes!
 */

/*
 * zebra 向客户端回报的错误类型（ZEBRA_ERROR 报文载荷）：
 *   ZEBRA_UNKNOWN_ERROR    未知错误
 *   ZEBRA_NO_VRF           报文头里的 vrf_id 在本端找不到对应 VRF
 *   ZEBRA_INVALID_MSG_TYPE 命令字无对应处理函数
 * 由 zapi_error_decode() 解出，再经 zclient->handle_error 回调交给上层。
 */
enum zebra_error_types {
	ZEBRA_UNKNOWN_ERROR,    /* Error of unknown type */
	ZEBRA_NO_VRF,		/* Vrf in header was not found */
	ZEBRA_INVALID_MSG_TYPE, /* No handler found for msg type */
};

/* 把 zebra 错误类型枚举转成可读字符串，仅用于日志/调试展示。 */
static inline const char *zebra_error_type2str(enum zebra_error_types type)
{
	switch (type) {
	case ZEBRA_UNKNOWN_ERROR:
		return "ZEBRA_UNKNOWN_ERROR";
	case ZEBRA_NO_VRF:
		return "ZEBRA_NO_VRF";
	case ZEBRA_INVALID_MSG_TYPE:
		return "ZEBRA_INVALID_MSG_TYPE";
	}

	return "UNKNOWN";
}

/*
 * 某一(afi, route type)的重分发订阅状态。
 *   enabled   该协议类型是否被订阅(0/1)
 *   instances 已订阅的 instance(实例号)链表；instance 用于区分同协议
 *             多实例，如多个 OSPF 进程。
 */
struct redist_proto {
	uint8_t enabled;
	struct list *instances;
};

/**
 * Redistribute table direct instance data structure: keeps the VRF
 * that subscribed to the table ID.
 *
 * **NOTE**
 * `table_id` is an integer because that is what the netlink interface
 * uses for route attribute RTA_TABLE (32bit int), however the whole
 * zclient API uses `unsigned short` (and CLI commands) so it will be
 * limited to the range 1 to 65535.
 */
/*
 * 「table direct」重分发订阅项：记录哪个 VRF 订阅了某个 table ID。
 *   vrf_id   订阅该 table ID 的 VRF
 *   table_id 内核表号(netlink RTA_TABLE 为 32bit int)
 * 注意：zclient API 与 CLI 多处仍用 unsigned short 传 table_id，
 * 因此实际有效范围被限制在 1..65535。
 */
struct redist_table_direct {
	vrf_id_t vrf_id;
	int table_id;
};

/*
 * zebra 上报给客户端的本地能力快照（ZEBRA_CAPABILITIES 解出）：
 *   ecmp              本机支持的最大 ECMP 条数
 *   mpls_enabled      内核/平台是否启用 MPLS
 *   role              MLAG 角色(primary/secondary/none)
 *   v6_with_v4_nexthop 是否允许 IPv6 路由携带 IPv4 nexthop
 */
struct zclient_capabilities {
	uint32_t ecmp;
	bool mpls_enabled;
	enum mlag_role role;
	bool v6_with_v4_nexthop;
};

/*
 * GR（优雅重启）能力协商报文载荷，随 ZEBRA_CLIENT_CAPABILITIES 传递。
 *   cap               能力类型(见 enum zserv_client_capabilities)
 *   stale_removal_time 陈旧路由的保留/清除时间
 *   afi/safi          该能力作用的地址族与子地址族
 *   vrf_id            该能力作用的 VRF
 */
/* Graceful Restart Capabilities message */
struct zapi_cap {
	enum zserv_client_capabilities cap;
	uint32_t stale_removal_time;
	afi_t afi;
	safi_t safi;
	vrf_id_t vrf_id;
};

/*
 * 所有 ZAPI 上报处理函数的统一形参列表：
 *   cmd    收到的命令字
 *   zclient 触发本次上报的客户端句柄
 *   length 报文长度(zclient_create_header 填充的长度字段)
 *   vrf_id 报文所属 VRF
 * 由 typedef zclient_handler 与各 handler 表使用。
 */
/* clang-format off */
#define ZAPI_CALLBACK_ARGS                                                     \
	int cmd, struct zclient *zclient, uint16_t length, vrf_id_t vrf_id

/* function-type typedef (pointer not included) */
typedef int (zclient_handler)(ZAPI_CALLBACK_ARGS);
/* clang-format on */

struct zapi_route;

/*
 * Zebra 客户端句柄：每个与 zebra 通信的进程持有一个。
 * 主要字段：
 *   master            绑定的 event_loop，用于注册读写/连接事件
 *   privs             切换 socket 权限用的 zebra_privs_t
 *   synchronous       是否同步客户端(收完即处理，无事件循环)
 *   auxiliary         辅助客户端：不跑 lib 层通用接口/VRF 处理，避免重复
 *   bfd_integration   是否经 bfd_protocol_integration_init() 集成 BFD
 *   session_id        会话号，支持一个进程多会话
 *   sock              与 zebra 的套接字
 *   fail              连接失败累计次数(用于重连退避)
 *   ibuf/obuf         收/发消息缓冲 stream
 *   wb               待写入 zebra 的排队数据(buffer)
 *   t_read/t_connect/t_write 读事件、连接事件、写事件
 *   redist_default    本客户端自身的协议类型(lib/zebra.h 的 ZEBRA_ROUTE_*)
 *   instance          本客户端的实例号
 *   mi_redist         多实例重分发状态 [afi][route_type]
 *   redist            重分发订阅位图 [afi][route_type]
 *   default_information 默认路由重分发位图 [afi]
 *   zebra_connected/zebra_capabilities 连接建立/能力上报回调
 *   nexthop_update    nexthop 解析结果回调(带 match 与 nhr)
 *   handle_error      zebra 报错回调
 *   zebra_buffer_write_ready 发送端缓冲可写回调
 *   handlers/n_handlers 命令到处理函数的分发表
 *   n_handlers 为表长度；handlers 为 const 指针数组，下标即命令字。
 */
/* Structure for the zebra client. */
struct zclient {
	/* The thread master we schedule ourselves on */
	struct event_loop *master;

	/* Privileges to change socket values */
	struct zebra_privs_t *privs;

	/* Is this a synchronous client? */
	bool synchronous;

	/* Auxiliary clients don't execute standard library handlers
	 * (which otherwise would duplicate VRF/interface add/delete/etc.
	 */
	bool auxiliary;

	/* BFD enabled with bfd_protocol_integration_init() */
	bool bfd_integration;

	/* Session id (optional) to support clients with multiple sessions */
	uint32_t session_id;

	/* Socket to zebra daemon. */
	int sock;

	/* Connection failure count. */
	int fail;

	/* Input buffer for zebra message. */
	struct stream *ibuf;

	/* Output buffer for zebra message. */
	struct stream *obuf;

	/* Buffer of data waiting to be written to zebra. */
	struct buffer *wb;

	/* Read and connect thread. */
	struct event *t_read;
	struct event *t_connect;

	/* Thread to write buffered data to zebra. */
	struct event *t_write;

	/* Redistribute information. */
	uint8_t redist_default; /* clients protocol */
	unsigned short instance;
	struct redist_proto mi_redist[AFI_MAX][ZEBRA_ROUTE_MAX];
	vrf_bitmap_t redist[AFI_MAX][ZEBRA_ROUTE_MAX];

	/* Redistribute default. */
	vrf_bitmap_t default_information[AFI_MAX];

	/* Pointer to the callback functions. */
	void (*zebra_connected)(struct zclient *);
	void (*zebra_capabilities)(struct zclient_capabilities *cap);

	/*
	 * match -> is the prefix that the calling daemon asked to be matched
	 * against.
	 * nhr->prefix -> is the actual prefix that was matched against in the
	 * rib itself.
	 *
	 * This distinction is made because a LPM can be made if there is a
	 * covering route.  This way the upper level protocol can make a
	 * decision point about whether or not it wants to use the match or not.
	 */
	void (*nexthop_update)(struct vrf *vrf, struct prefix *match,
			       struct zapi_route *nhr);

	int (*handle_error)(enum zebra_error_types error);

	/*
	 * When the zclient attempts to write the stream data to
	 * it's named pipe to/from zebra, we may have a situation
	 * where the other daemon has not fully drained the data
	 * from the socket.  In this case provide a mechanism
	 * where we will *still* buffer the data to be sent
	 * and also provide a callback mechanism to the appropriate
	 * place where we can signal that we're ready to receive
	 * more data.
	 */
	void (*zebra_buffer_write_ready)(void);

	zclient_handler *const *handlers;
	size_t n_handlers;
};

/* lib handlers added in bfd.c */
extern int zclient_bfd_session_replay(ZAPI_CALLBACK_ARGS);
extern int zclient_bfd_session_update(ZAPI_CALLBACK_ARGS);

/*
 * ZAPI 路由的“可选字段有效位”标志：置位表示对应字段在报文中被携带。
 *   NEXTHOP 携带 nexthop、DISTANCE 携带 distance、METRIC 携带 metric、
 *   TAG 携带 tag、MTU 携带 mtu、SRCPFX 携带源前缀、BACKUP_NEXTHOPS 携带
 *   备份 nexthop、NHG 使用 nexthop 组、TABLEID 携带内核表号(仅 daemon
 *   明确告知“表不在该 VRF”且传的是 default vrf 时有效)、SRTE 携带
 *   SR-TE color、OPAQUE 携带不透明附加数据。
 */
/* Zebra API message flag. */
#define ZAPI_MESSAGE_NEXTHOP  0x01
#define ZAPI_MESSAGE_DISTANCE 0x02
#define ZAPI_MESSAGE_METRIC   0x04
#define ZAPI_MESSAGE_TAG      0x08
#define ZAPI_MESSAGE_MTU      0x10
#define ZAPI_MESSAGE_SRCPFX   0x20
/* Backup nexthops are present */
#define ZAPI_MESSAGE_BACKUP_NEXTHOPS 0x40
#define ZAPI_MESSAGE_NHG 0x80
/*
 * This should only be used by a DAEMON that needs to communicate
 * the table being used is not in the VRF.  You must pass the
 * default vrf, else this will be ignored.
 */
#define ZAPI_MESSAGE_TABLEID 0x0100
#define ZAPI_MESSAGE_SRTE 0x0200
#define ZAPI_MESSAGE_OPAQUE 0x0400

#define ZSERV_VERSION 6
/*
 * Zserv 協議报文头（所有 ZAPI 报文都以它开头，按网络序编码，packed）。
 *   length   整条报文长度(含头部)
 *   marker   新版 zserv 固定为 255；旧版此处是 command 字段
 *   version  协议版本，当前 ZSERV_VERSION=6
 *   vrf_id   报文所属 VRF
 *   command  命令字(见 zebra_message_types_t)
 * ZAPI_HEADER_CMD_LOCATION 给出 command 在头中的偏移，供旧/新报文区分。
 */
/* Zserv protocol message header */
struct zmsghdr {
	uint16_t length;
	/* Always set to 255 in new zserv */
	uint8_t marker;
	uint8_t version;
	vrf_id_t vrf_id;
	uint16_t command;
} __attribute__((packed));
#define ZAPI_HEADER_CMD_LOCATION offsetof(struct zmsghdr, command)

/*
 * ZAPI nexthop. Note that these are sorted when associated with ZAPI routes,
 * and that sorting must be aligned with the sorting of nexthops in
 * lib/nexthop.c. Any new fields must be accounted for in zapi_nexthop_cmp().
 */
/*
 * ZAPI nexthop：路由下一跳在 ZAPI 线上的表示。
 * 字段：type(nexthop 类型)、vrf_id、ifindex、flags；联合体 gate/bh_type
 * 二选一(普通网关地址 或 黑洞类型)；label_num/label_type/labels 承载
 * BGP-LU / SR 标签；rmac/weight 用于 EVPN 与加权；backup_num/backup_idx
 * 指向备份 nexthop 下标；srte_color、seg6local_*、seg6_segs 等承载
 * SR-TE 与 SRv6 headend/endpoint 信息。
 * 注意：这些 nexthop 在关联到 ZAPI 路由时会排序，排序规则必须与
 * lib/nexthop.c 保持一致；新增字段要同步更新 zapi_nexthop_cmp()。
 */
struct zapi_nexthop {
	enum nexthop_types_t type;
	vrf_id_t vrf_id;
	ifindex_t ifindex;
	uint8_t flags;
	union {
		union g_addr gate;
		enum blackhole_type bh_type;
	};

	/* MPLS labels for BGP-LU or Segment Routing */
	uint8_t label_num;
	enum lsp_types_t label_type;
	mpls_label_t labels[MPLS_MAX_LABELS];

	struct ethaddr rmac;

	uint64_t weight;

	/* Backup nexthops, for IP-FRR, TI-LFA, etc */
	uint8_t backup_num;
	uint8_t backup_idx[NEXTHOP_MAX_BACKUPS];

	/* SR-TE color. */
	uint32_t srte_color;

	/* SRv6 localsid info for Endpoint-behaviour */
	uint32_t seg6local_action;
	struct seg6local_context seg6local_ctx;

	/* SRv6 Headend-behaviour */
	int seg_num;
	struct in6_addr seg6_segs[SRV6_MAX_SEGS];
	enum srv6_headend_behavior srv6_encap_behavior;
};

/*
 * ZAPI nexthop flags values - we're encoding a single octet
 * initially, so ensure that the on-the-wire encoding continues
 * to match the number of valid flags.
 */

/*
 * ZAPI nexthop 标志位(线上先按单字节编码，故新增标志要确保仍在 8bit 内)：
 *   ONLINK 直连(onlink)、LABEL 带标签、WEIGHT 带权重、HAS_BACKUP 有备份、
 *   SEG6 SRv6 headend 段、SEG6LOCAL SRv6 endpoint 行为、EVPN EVPN nexthop。
 */
#define ZAPI_NEXTHOP_FLAG_ONLINK	0x01
#define ZAPI_NEXTHOP_FLAG_LABEL		0x02
#define ZAPI_NEXTHOP_FLAG_WEIGHT	0x04
#define ZAPI_NEXTHOP_FLAG_HAS_BACKUP	0x08 /* Nexthop has a backup */
#define ZAPI_NEXTHOP_FLAG_SEG6		0x10
#define ZAPI_NEXTHOP_FLAG_SEG6LOCAL	0x20
#define ZAPI_NEXTHOP_FLAG_EVPN		0x40

/*
 * ZAPI Nexthop Group. For use with protocol creation of nexthop groups.
 */
/*
 * ZAPI nexthop 组(NHG)：允许协议侧创建并复用一组 nexthop。
 *   proto            拥有该 NHG 的协议编号
 *   id               NHG 标识；编号空间见 ZEBRA_NHG_PROTO_* 宏
 *   resilience       NHG 弹性(resilience)参数
 *   nexthop_num / nexthops          主 nexthop 数组(上限 MULTIPATH_NUM)
 *   backup_nexthop_num / backup_nexthops 备份 nexthop 数组
 */
struct zapi_nhg {
	uint16_t proto;
	uint32_t id;

	struct nhg_resilience resilience;

	uint16_t nexthop_num;
	struct zapi_nexthop nexthops[MULTIPATH_NUM];

	uint16_t backup_nexthop_num;
	struct zapi_nexthop backup_nexthops[MULTIPATH_NUM];
};

/*
 * Some of these data structures do not map easily to
 * a actual data structure size giving different compilers
 * and systems.  For those data structures we need
 * to use the smallest available stream_getX/putX functions
 * to encode/decode.
 */
/*
 * ZAPI 路由：daemon 下发/接收一条路由的完整表示。
 * 字段要点：
 *   type/instance     路由来源协议类型与实例
 *   flags             路由标志(见下方 ZEBRA_FLAG_*)，新增需同步
 *                     zclient_dump_route_flags()
 *   message           旧的 XXX_MESSAGE 标志(见 ZAPI_MESSAGE_*)，指示哪些
 *                     可选字段有效
 *   safi              子地址族(按 uint8_t 编码)
 *   prefix/src_prefix 目的前缀与源前缀(SR 场景)
 *   nhgid             关联的 nexthop 组 id
 *   distance/metric/tag/mtu 管理距离/度量/路由 tag/MTU
 *   vrf_id/tableid    所属 VRF 与内核表号(SR-TE 另用 srte_color)
 *   其后是 nexthops / backup_nexthops / opaque 等大数组字段。
 * 注意：结构体前半段(到 nexthop_num 之前)会被整体 memset 清零，大数组
 * 字段则手工初始化；因此往前面加字段会被清零，加大字段会拖慢所有人。
 */
struct zapi_route {
	uint8_t type;
	unsigned short instance;

	/* If you add flags, update zclient_dump_route_flags */
	uint32_t flags;
/*
 * 路由标志(ZEBRA_FLAG_*)：zapi_route.flags 的取值，指示路由的特殊处理。
 *   ALLOW_RECURSION      允许 zebra 递归解析该路由的 nexthop
 *   SELFROUTE            启动时读回的上次运行遗留路由
 *   IBGP                 该路由是 IBGP 路由(来自 BGP 下发)
 *   SELECTED             该路由已被选中用于安装 FIB(由 zebra 置位回传)
 *   FIB_OVERRIDE         强制安装(FIB)而压过 SELECTED
 *   EVPN_ROUTE           EVPN 路由，特殊处理
 *   RR_USE_DISTANCE      把 distance 作为选路附加判别(如备份静态路由)
 *   TRAPPED / OFFLOADED / OFFLOAD_FAILED  ASIC 卸载状态(未卸载转 CPU /
 *                        已卸载 / 卸载失败)
 *   OUTOFSYNC            本端与内核路由表可能不同步
 *   TABLEID              该路由绑定 table ID，解绑 VRF 时仍需保留
 *   USE_RECURSIVE_WEIGHT 递归 nexthop 时使用权重
 */
/*
 * Cause Zebra to consider this routes nexthops recursively
 */
#define ZEBRA_FLAG_ALLOW_RECURSION    0x01
/*
 * This is a route that is read in on startup that was left around
 * from a previous run of FRR
 */
#define ZEBRA_FLAG_SELFROUTE          0x02
/*
 * This flag is used to tell Zebra that the BGP route being passed
 * down is a IBGP route
 */
#define ZEBRA_FLAG_IBGP               0x04
/*
 * This is a route that has been selected for FIB installation.
 * This flag is set in zebra and can be passed up to routing daemons
 */
#define ZEBRA_FLAG_SELECTED           0x08
/*
 * This is a route that we are telling Zebra that this route *must*
 * win and will be installed even over ZEBRA_FLAG_SELECTED
 */
#define ZEBRA_FLAG_FIB_OVERRIDE       0x10
/*
 * This flag tells Zebra that the route is a EVPN route and should
 * be treated specially
 */
#define ZEBRA_FLAG_EVPN_ROUTE         0x20
/*
 * This flag tells Zebra that it should treat the distance passed
 * down as an additional discriminator for route selection of the
 * route entry.  This mainly is used for backup static routes.
 */
#define ZEBRA_FLAG_RR_USE_DISTANCE    0x40
/*
 * This flag tells everyone that the route was intentionally
 * not offloaded and the route will be sent to the cpu for
 * forwarding.  This flag makes no sense unless you are in
 * an asic offload situation
 */
#define ZEBRA_FLAG_TRAPPED            0x80
/*
 * This flag tells everyone that the route has been
 * successfully offloaded to an asic for forwarding.
 * This flag makes no sense unless you are in an asic
 * offload situation.
 */
#define ZEBRA_FLAG_OFFLOADED          0x100
/*
 * This flag tells everyone that the route has
 * failed offloading.
 * This flag makes no sense unless you are in an asic
 * offload situation.
 */
#define ZEBRA_FLAG_OFFLOAD_FAILED     0x200

/*
 * This flag lets us know that we think the route entry
 * received has caused us to be out of sync with the
 * kernel (NLM_F_APPEND at the very least )
 */
#define ZEBRA_FLAG_OUTOFSYNC          0x400
/*
 * This flag lets us know that the route entry is
 * associated to the table ID and must remain when the
 * table ID is de-associated from a VRF.
 */
#define ZEBRA_FLAG_TABLEID 0x800

#define ZEBRA_FLAG_USE_RECURSIVE_WEIGHT 0x1000

	/* The older XXX_MESSAGE flags live here */
	uint32_t message;

	/*
	 * This is an enum but we are going to treat it as a uint8_t
	 * for purpose of encoding/decoding
	 */
	safi_t safi;

	struct prefix prefix;
	struct prefix_ipv6 src_prefix;

	uint32_t nhgid;

	uint8_t distance;

	uint32_t metric;

	route_tag_t tag;

	uint32_t mtu;

	vrf_id_t vrf_id;

	uint32_t tableid;

	/* SR-TE color (used for nexthop updates only). */
	uint32_t srte_color;

	/*
	 * Note well: this field marks the end of the part of the struct
	 * that is memset to zero to initialize. We avoid memset of the entire
	 * struct because that can be very expensive if we have a large ecmp
	 * scale. We hand-init the counters associated with the large
	 * embedded fields.
	 * If you add an attribute before this one, it will be memset;
	 * if you add a very large field, everyone will suffer.
	 */

	uint16_t nexthop_num;
	struct zapi_nexthop nexthops[MULTIPATH_NUM];

	/* Support backup routes for IP FRR, TI-LFA, traffic engineering */
	uint16_t backup_nexthop_num;
	struct zapi_nexthop backup_nexthops[MULTIPATH_NUM];

/* 路由携带的不透明附加数据上限(字节)，供上层透传私有信息。 */
#define ZAPI_MESSAGE_OPAQUE_LENGTH 1024
	struct {
		uint16_t length;
		uint8_t data[ZAPI_MESSAGE_OPAQUE_LENGTH];
	} opaque;
};

extern char *zclient_dump_route_flags(uint32_t flags, char *buf, size_t len);

/*
 * ZAPI 标签(labels)：用于 LDP/L3VPN/BGP-LU 等下发标签与转发条目。
 *   message       标志位(见 ZAPI_LABELS_*)
 *   type          标签类型(enum lsp_types_t)
 *   local_label   本地分配的标签
 *   route         关联路由(prefix/type/instance)，用于 FTN 映射
 *   nexthop_num/nexthops           主 nexthop
 *   backup_nexthop_num/backup_nexthops 备份 nexthop
 */
struct zapi_labels {
	uint8_t message;
/* zapi_labels.message 标志：FTN 为转发等价类到 NHLFE 映射，HAS_BACKUPS 表示带备份标签。 */
#define ZAPI_LABELS_FTN           0x01
#define ZAPI_LABELS_HAS_BACKUPS   0x02
	enum lsp_types_t type;
	mpls_label_t local_label;
	struct {
		struct prefix prefix;
		uint8_t type;
		unsigned short instance;
	} route;

	uint16_t nexthop_num;
	struct zapi_nexthop nexthops[MULTIPATH_NUM];

	/* Backup nexthops, if present */
	uint16_t backup_nexthop_num;
	struct zapi_nexthop backup_nexthops[MULTIPATH_NUM];
};

/*
 * SR-TE 隧道(segment list)描述：type 为 LSP 类型，local_label 为本地标签，
 * label_num/labels 给出段列表(最多 MPLS_MAX_LABELS 个标签)。
 * 被 struct zapi_sr_policy 作为 segment_list 内嵌使用。
 */
struct zapi_srte_tunnel {
	enum lsp_types_t type;
	mpls_label_t local_label;
	uint8_t label_num;
	mpls_label_t labels[MPLS_MAX_LABELS];
};

/*
 * SR Policy：以 color + 端点(endpoint)标识一条 SR-TE 策略。
 *   color        策略颜色，与 BGP 扩展团体 SR-TE color 对应
 *   endpoint     策略端点地址(v4/v6)
 *   name         策略名(限 SRTE_POLICY_NAME_MAX_LENGTH)
 *   segment_list 关联的 SR-TE 段列表
 *   status       策略状态(zebra 通知时填写)
 */
struct zapi_sr_policy {
	uint32_t color;
	struct ipaddr endpoint;
	char name[SRTE_POLICY_NAME_MAX_LENGTH];
	struct zapi_srte_tunnel segment_list;
	int status;
};

/*
 * ZAPI 伪线(pseudowire)参数：用于 VPWS/L2VPN。
 *   ifname/ifindex  伪线对应接口名与索引
 *   type/af         伪线类型与地址族
 *   nexthop         对端 nexthop 地址
 *   local_label/remote_label 本地/远端标签
 *   flags           伪线标志(如 ZEBRA_PW_*)
 *   data            协议私有字段(union pw_protocol_fields)
 *   protocol        拥有该伪线的协议
 */
struct zapi_pw {
	char ifname[IFNAMSIZ];
	ifindex_t ifindex;
	int type;
	int af;
	union g_addr nexthop;
	uint32_t local_label;
	uint32_t remote_label;
	uint8_t flags;
	union pw_protocol_fields data;
	uint8_t protocol;
};

/* 伪线状态更新(ZEBRA_PW_STATUS_UPDATE)载荷：接口名/索引 + 状态位。 */
struct zapi_pw_status {
	char ifname[IFNAMSIZ];
	ifindex_t ifindex;
	uint32_t status;
};

/*
 * RLFA(Remote LFA)相关的 IGP 实例数据：vrf_id + protocol，并按协议
 * 联合存放 OSPF/ISIS 的私有标识(如 ISIS 的 area_tag 与 SPF run_id)，
 * 用于把 LDP 侧解析出的标签回填到正确的 IGP 实例。
 */
/* IGP instance data associated to a RLFA. */
struct zapi_rlfa_igp {
	vrf_id_t vrf_id;
	int protocol;
	union {
		struct {
			char area_tag[32];
			struct {
				int tree_id;
				int level;
				unsigned int run_id;
			} spf;
		} isis;
	};
};

/*
 * IGP 向 LDP 发起的 RLFA 注册/注销请求：携带 IGP 实例(igp)、目的前缀
 * (destination)与 PQ 节点地址(pq_address)，请求 LDP 解析到 PQ 节点的标签。
 */
/* IGP -> LDP RLFA (un)registration message. */
struct zapi_rlfa_request {
	/* IGP instance data. */
	struct zapi_rlfa_igp igp;

	/* Destination prefix. */
	struct prefix destination;

	/* PQ node address. */
	struct in_addr pq_address;
};

/*
 * LDP 回给 IGP 的 RLFA 标签更新：在同样的 igp/destination 下给出解析出的
 * PQ 标签(pq_label)以及与各 nexthop(地址+标签)的对应关系，
 * nexthop_num 指明 nexthops 数组有效元素个数。
 */
/* LDP -> IGP RLFA label update. */
struct zapi_rlfa_response {
	/* IGP instance data. */
	struct zapi_rlfa_igp igp;

	/* Destination prefix. */
	struct prefix destination;

	/* Resolved LDP labels. */
	mpls_label_t pq_label;
	uint16_t nexthop_num;
	struct {
		int family;
		union g_addr gate;
		mpls_label_t label;
	} nexthops[MULTIPATH_NUM];
};

/*
 * 路由安装结果通知(zebra 回给下发方)：安装失败/被更优管理距离顶掉/
 * 已安装/已删除/删除失败。用于上层判断路由是否真正落到内核。
 */
enum zapi_route_notify_owner {
	ZAPI_ROUTE_FAIL_INSTALL,
	ZAPI_ROUTE_BETTER_ADMIN_WON,
	ZAPI_ROUTE_INSTALLED,
	ZAPI_ROUTE_REMOVED,
	ZAPI_ROUTE_REMOVE_FAIL,
};

/*
 * nexthop 组安装结果通知：安装失败/已安装/已删除/删除失败。
 */
enum zapi_nhg_notify_owner {
	ZAPI_NHG_FAIL_INSTALL,
	ZAPI_NHG_INSTALLED,
	ZAPI_NHG_REMOVED,
	ZAPI_NHG_REMOVE_FAIL,
};

/*
 * PBR 策略路由(rule)安装结果通知：安装失败/已安装/已删除/删除失败。
 */
enum zapi_rule_notify_owner {
	ZAPI_RULE_FAIL_INSTALL,
	ZAPI_RULE_INSTALLED,
	ZAPI_RULE_REMOVED,
	ZAPI_RULE_FAIL_REMOVE,
};

/*
 * ipset 集合类型：NET_NET(网段对网段)、NET_PORT_NET(网段端口对网段)、
 * NET_PORT(网段端口)、NET(网段)。
 */
enum ipset_type {
	IPSET_NET_NET = 1,
	IPSET_NET_PORT_NET,
	IPSET_NET_PORT,
	IPSET_NET
};

/*
 * ipset 集合创建/删除结果通知：安装失败/已安装/已删除/删除失败。
 */
enum zapi_ipset_notify_owner {
	ZAPI_IPSET_FAIL_INSTALL = 0,
	ZAPI_IPSET_INSTALLED,
	ZAPI_IPSET_REMOVED,
	ZAPI_IPSET_FAIL_REMOVE,
};

/*
 * ipset 条目增删结果通知：安装失败/已安装/已删除/删除失败。
 */
enum zapi_ipset_entry_notify_owner {
	ZAPI_IPSET_ENTRY_FAIL_INSTALL = 0,
	ZAPI_IPSET_ENTRY_INSTALLED,
	ZAPI_IPSET_ENTRY_REMOVED,
	ZAPI_IPSET_ENTRY_FAIL_REMOVE,
};

/*
 * iptable(ipset 规则表)创建/删除结果通知：安装失败/已安装/已删除/删除失败。
 */
enum zapi_iptable_notify_owner {
	ZAPI_IPTABLE_FAIL_INSTALL = 0,
	ZAPI_IPTABLE_INSTALLED,
	ZAPI_IPTABLE_REMOVED,
	ZAPI_IPTABLE_FAIL_REMOVE,
};

/*
 * SRv6 SID 分配/释放结果通知：分配失败/已分配/已释放/释放失败。
 */
enum zapi_srv6_sid_notify {
	ZAPI_SRV6_SID_FAIL_ALLOC = 0,
	ZAPI_SRV6_SID_ALLOCATED,
	ZAPI_SRV6_SID_RELEASED,
	ZAPI_SRV6_SID_FAIL_RELEASE,
};

/*
 * zclient 发送函数的统一返回值：
 *   ZCLIENT_SEND_FAILURE  = -1  I/O 出错
 *   ZCLIENT_SEND_SUCCESS  =  0  数据已成功发出
 *   ZCLIENT_SEND_BUFFERED =  1  对端暂不可写，数据已缓存待发
 * 注意 0 与 1 都表示“成功”，判空/判错只能用与 FAILURE 的比较或专门的
 * 成功判断，误把 BUFFERED 当失败会导致重复发送。
 */
enum zclient_send_status {
	ZCLIENT_SEND_FAILURE = -1,
	ZCLIENT_SEND_SUCCESS = 0,
	ZCLIENT_SEND_BUFFERED = 1
};

/* 把 NHG 通知结果枚举转字符串，仅用于日志/调试。 */
static inline const char *
zapi_nhg_notify_owner2str(enum zapi_nhg_notify_owner note)
{
	switch (note) {
	case ZAPI_NHG_FAIL_INSTALL:
		return "ZAPI_NHG_FAIL_INSTALL";
	case ZAPI_NHG_INSTALLED:
		return "ZAPI_NHG_INSTALLED";
	case ZAPI_NHG_REMOVE_FAIL:
		return "ZAPI_NHG_REMOVE_FAIL";
	case ZAPI_NHG_REMOVED:
		return "ZAPI_NHG_REMOVED";
	}

	return "UNKNOWN";
}

/* 把 PBR rule 通知结果枚举转字符串，仅用于日志/调试。 */
static inline const char *
zapi_rule_notify_owner2str(enum zapi_rule_notify_owner note)
{
	switch (note) {
	case ZAPI_RULE_FAIL_INSTALL:
		return "ZAPI_RULE_FAIL_INSTALL";
	case ZAPI_RULE_INSTALLED:
		return "ZAPI_RULE_INSTALLED";
	case ZAPI_RULE_FAIL_REMOVE:
		return "ZAPI_RULE_FAIL_REMOVE";
	case ZAPI_RULE_REMOVED:
		return "ZAPI_RULE_REMOVED";
	}

	return "UNKNOWN";
}

/* 把 SRv6 SID 通知结果枚举转字符串，仅用于日志/调试。 */
static inline const char *zapi_srv6_sid_notify2str(enum zapi_srv6_sid_notify note)
{
	switch (note) {
	case ZAPI_SRV6_SID_FAIL_ALLOC:
		return "ZAPI_SRV6_SID_FAIL_ALLOC";
	case ZAPI_SRV6_SID_ALLOCATED:
		return "ZAPI_SRV6_SID_ALLOCATED";
	case ZAPI_SRV6_SID_FAIL_RELEASE:
		return "ZAPI_SRV6_SID_FAIL_RELEASE";
	case ZAPI_SRV6_SID_RELEASED:
		return "ZAPI_SRV6_SID_RELEASED";
	}

	return "UNKNOWN";
}

/*
 * Zebra MAC-IP 条目类型标志：
 *   STICKY 粘滞 MAC、GW 网关(SVI)MAC、ROUTER_FLAG 代理 NA 的路由标志、
 *   OVERRIDE_FLAG 覆盖标志、SVI_IP SVI 的 MAC-IP、PROXY_ADVERT 非本地
 *   活跃(代理通告)、SYNC_PATH 同步路径(MLAG 用)。
 * 注意 flags 是 u8，如需用到 0x80 以上要改成 u32，并同步修改
 * zclient_evpn_dump_macip_flags()。
 */
/* Zebra MAC types */
#define ZEBRA_MACIP_TYPE_STICKY                0x01 /* Sticky MAC*/
#define ZEBRA_MACIP_TYPE_GW                    0x02 /* gateway (SVI) mac*/
#define ZEBRA_MACIP_TYPE_ROUTER_FLAG           0x04 /* Router Flag - proxy NA */
#define ZEBRA_MACIP_TYPE_OVERRIDE_FLAG         0x08 /* Override Flag */
#define ZEBRA_MACIP_TYPE_SVI_IP                0x10 /* SVI MAC-IP */
#define ZEBRA_MACIP_TYPE_PROXY_ADVERT          0x20 /* Not locally active */
#define ZEBRA_MACIP_TYPE_SYNC_PATH             0x40 /* sync path */
/* XXX - flags is an u8; that needs to be changed to u32 if you need
 * to allocate past 0x80.  Additionally touch zclient_evpn_dump_macip_flags
 */
#define MACIP_BUF_SIZE 128
extern char *zclient_evpn_dump_macip_flags(uint8_t flags, char *buf,
					   size_t len);

/* Zebra ES VTEP flags (ZEBRA_REMOTE_ES_VTEP_ADD) */
/* ESR has been rxed from the VTEP. Only VTEPs that have advertised the
 * Type-4 route can participate in DF election.
 */
#define ZAPI_ES_VTEP_FLAG_ESR_RXED (1 << 0)

enum zebra_neigh_state { ZEBRA_NEIGH_INACTIVE = 0, ZEBRA_NEIGH_ACTIVE = 1 };

/*
 * 创建 zclient 时的选项：
 *   synchronous 是否同步客户端(收完即处理，不用事件循环)
 *   auxiliary   辅助客户端：不执行 lib 层通用 handler(它们只应在主
 *               zclient 上跑一次)，同步客户端也会置位此标志。
 * 预置实例见 zclient_options_default / _sync / _auxiliary。
 */
struct zclient_options {
	bool synchronous;

	/* auxiliary = don't call common lib/ handlers that manage bits.
	 * Those should only run once, on the "main" zclient, which this is
	 * not.  (This is also set for synchronous clients.)
	 */
	bool auxiliary;
};

extern const struct zclient_options zclient_options_default;
extern const struct zclient_options zclient_options_sync;
extern const struct zclient_options zclient_options_auxiliary;

/* link layer representation for GRE like interfaces
 * ip_in is the underlay IP, ip_out is the tunnel dest
 * index stands for the index of the interface
 * ndm state stands for the NDM value in netlink
 * (see linux/neighbour.h)
 */
/*
 * 邻居(neigh)状态位，与 netlink NUD_* 状态一一对应，用于 GRE 类接口:
 *   INCOMPLETE 地址解析中、REACHABLE 可达、STALE 陈旧、DELAY 延迟、
 *   PROBE 探测中、FAILED 失败、NOARP 无 ARP、PERMANENT 永久、NONE 无。
 */
#define ZEBRA_NEIGH_STATE_INCOMPLETE (0x01)
#define ZEBRA_NEIGH_STATE_REACHABLE (0x02)
#define ZEBRA_NEIGH_STATE_STALE (0x04)
#define ZEBRA_NEIGH_STATE_DELAY (0x08)
#define ZEBRA_NEIGH_STATE_PROBE (0x10)
#define ZEBRA_NEIGH_STATE_FAILED (0x20)
#define ZEBRA_NEIGH_STATE_NOARP (0x40)
#define ZEBRA_NEIGH_STATE_PERMANENT (0x80)
#define ZEBRA_NEIGH_STATE_NONE (0x00)

/*
 * 邻居(neighbour)相关的 IP 信息载荷，用于 GRE 类接口的邻居设置。
 *   cmd       命令字
 *   ip_len    IP 长度
 *   ip_in     底层(underlay)IP
 *   ip_out    隧道目的 IP
 *   index     接口索引
 *   ndm_state netlink 邻居状态(见 linux/neighbour.h 的 NUD_* 与下方
 *             ZEBRA_NEIGH_STATE_* 的对应)
 */
struct zapi_neigh_ip {
	int cmd;
	int ip_len;
	struct ipaddr ip_in;
	struct ipaddr ip_out;
	ifindex_t index;
	uint32_t ndm_state;
};
/* 从字节流解出 struct zapi_neigh_ip；成功返回读取的状态值，失败返回 -1。 */
int zclient_neigh_ip_decode(struct stream *s, struct zapi_neigh_ip *api);
/*
 * 把邻居 IP 信息编码进字节流：cmd 为命令字，in/out 为底层与隧道地址，
 * ifp 提供接口索引，ndm_state 为 netlink 邻居状态，ip_len 为地址长度。
 * 返回编码后的字节数或错误码。
 */
int zclient_neigh_ip_encode(struct stream *s, uint16_t cmd, union sockunion *in,
			    union sockunion *out, struct interface *ifp,
			    int ndm_state, int ip_len);

/*
 * We reserve the top 4 bits for l2-NHG, everything else
 * is for zebra/proto l3-NHG.
 *
 * Each client is going to get it's own nexthop group space
 * and we'll separate them, we'll figure out where to start based upon
 * the route_types.h
 */
/*
 * NHG 编号空间划分：整个空间以 ZEBRA_NHG_PROTO_UPPER 为上界，按路由
 * 协议类型(ZEBRA_ROUTE_MAX)等分(ZEBRA_NHG_PROTO_SPACING)，每个协议拿到
 * 自己的一段，起始从 ZEBRA_NHG_PROTO_LOWER 起(ZEBRA_ROUTE_LOCAL 之后的
 * 第一个协议段)，顶部 4bit 留给 l2-NHG。zclient_get_nhg_start() 据此返回
 * 指定协议可用的起始 id。
 */
#define ZEBRA_NHG_PROTO_UPPER                                                  \
	((uint32_t)250000000) /* Bottom 28 bits then rounded down */
#define ZEBRA_NHG_PROTO_SPACING (ZEBRA_NHG_PROTO_UPPER / ZEBRA_ROUTE_MAX)
#define ZEBRA_NHG_PROTO_LOWER                                                  \
	(ZEBRA_NHG_PROTO_SPACING * (ZEBRA_ROUTE_LOCAL + 1))

extern uint32_t zclient_get_nhg_start(uint32_t proto);

/*
 * zclient 生命周期与基础接口：
 *   zclient_new  按选项 opt 与 handler 表创建并初始化句柄(不建连接)
 *   zclient_init 绑定协议类型、实例与权限，准备收发缓冲
 *   zclient_start 建立到 zebra 的连接并注册事件、发送初始问候
 *   zclient_stop  断开连接并注销事件
 *   zclient_reset 重置状态(如重连前清理缓冲)
 *   zclient_free  释放句柄及其资源
 *   zclient_socket_connect 仅做底层套接字连接
 * 典型顺序：new -> init -> start；daemon 退出时 free。
 */
extern struct zclient *zclient_new(struct event_loop *m,
				   const struct zclient_options *opt,
				   zclient_handler *const *handlers,
				   size_t n_handlers);

extern void zclient_init(struct zclient *zc, int redist_default, unsigned short instance,
			 struct zebra_privs_t *privs);
extern int zclient_start(struct zclient *zclient);
extern void zclient_stop(struct zclient *zclient);
extern void zclient_reset(struct zclient *zclient);
extern void zclient_free(struct zclient *zclient);

extern int zclient_socket_connect(struct zclient *zclient);

/*
 * 重分发 instance 管理：check 查询某 instance 是否存在(返回其链表节点
 * 指针的数据区，不存在返回 NULL)；add/del 增删单个 instance；
 * del_all 清空全部 instance。
 */
extern unsigned short *redist_check_instance(struct redist_proto *red, unsigned short instance);
extern void redist_add_instance(struct redist_proto *red, unsigned short instance);
extern void redist_del_instance(struct redist_proto *red, unsigned short instance);
extern void redist_del_all_instances(struct redist_proto *red);

/*
 * table direct 订阅项管理：lookup 按 table 查找订阅项(返回指针或 NULL)；
 * has_id 判断某 table_id 是否已有订阅；add / del 增删订阅项。
 */
extern struct redist_table_direct *
redist_lookup_table_direct(const struct redist_proto *red, const struct redist_table_direct *table);
extern bool redist_table_direct_has_id(const struct redist_proto *red, int table_id);
extern void redist_add_table_direct(struct redist_proto *red,
				    const struct redist_table_direct *table);
extern void redist_del_table_direct(struct redist_proto *red,
				    const struct redist_table_direct *table);


/*
 * Send to zebra that the specified vrf is using label to resolve
 * itself for L3VPN's.  Repeated calls of this function with
 * different labels will cause an effective update of the
 * label for lookup.  If you pass in MPLS_LABEL_NONE
 * we will cause a delete action and remove this label pop
 * operation.
 *
 * The underlying AF_MPLS doesn't care about afi's
 * but we can make the zebra_vrf keep track of what
 * we have installed and play some special games
 * to get them both installed.
 */
/*
 * 告诉 zebra 某 VRF 用 label 做 L3VPN 自身解析：重复调用不同 label 会
 * 更新查找标签；传 MPLS_LABEL_NONE 表示删除该标签弹出操作。
 * 底层 AF_MPLS 不区分 afi，但会借 zebra_vrf 记录已安装项，以便 v4/v6
 * 都装上。返回发送状态。
 */
extern enum zclient_send_status
zclient_send_vrf_label(struct zclient *zclient, vrf_id_t vrf_id, afi_t afi,
		       mpls_label_t label, enum lsp_types_t ltype);

extern enum zclient_send_status zclient_send_localsid(struct zclient *zclient, uint8_t cmd,
						      const struct in6_addr *sid,
						      uint16_t prefixlen, ifindex_t oif,
						      enum seg6local_action_t action,
						      const struct seg6local_context *context);

/*
 * 注册/注销一批初始请求：连接建立后调用 reg 把本客户端关心的接口、地址、
 * 路由器 id、重分发等事件注册给 zebra；dereg 则反向注销。
 * 另有 router_id_update 用于在指定 afi/vrf 上请求路由器 id 更新，
 * radv_req 控制接口的 RA 通告(enable + 间隔)，protodown 设置接口
 * proto-down 状态。均返回发送状态。
 */
extern void zclient_send_reg_requests(struct zclient *zclient, vrf_id_t vrf_id);
/* 反向操作：注销连接建立时注册的那批初始请求。 */
extern void zclient_send_dereg_requests(struct zclient *zclient, vrf_id_t vrf_id);
/* 在指定 afi/vrf 上请求/推送路由器 id 更新，返回发送状态。 */
extern enum zclient_send_status
zclient_send_router_id_update(struct zclient *zclient,
			      zebra_message_types_t type, afi_t afi,
			      vrf_id_t vrf_id);

/* 控制接口的 RA(路由通告)开关与间隔：enable 非零开启，ra_interval 为发包间隔。 */
extern enum zclient_send_status
zclient_send_interface_radv_req(struct zclient *zclient, vrf_id_t vrf_id,
				struct interface *ifp, int enable,
				uint32_t ra_interval);
/* 设置接口的 proto-down 状态(down 为 true 表示置为 down)，返回发送状态。 */
extern enum zclient_send_status
zclient_send_interface_protodown(struct zclient *zclient, vrf_id_t vrf_id,
				 struct interface *ifp, bool down);

/*
 * 向 zebra 发送一条「重分发」命令(cmd=ADD/DELETE，type=被重分发的路由
 * 协议类型，instance 为实例号，vrf 为目标 VRF)——只发命令，不更新
 * zclient 内部的 redist 订阅状态(状态更新由 zclient_redistribute 负责)。
 * 返回发送状态。
 */
/* Send redistribute command to zebra daemon. Do not update zclient state. */
extern enum zclient_send_status
zebra_redistribute_send(int command, struct zclient *, afi_t, int type,
			unsigned short instance, vrf_id_t vrf_id);

extern enum zclient_send_status
zebra_redistribute_default_send(int command, struct zclient *zclient, afi_t afi,
				vrf_id_t vrf_id);

/*
 * 向 zebra 请求/取消「路由安装结果通知」：set 为 true 表示请求通知，
 * false 表示取消。开启后 zebra 会就路由安装结果回报 ZEBRA_ROUTE_NOTIFY_OWNER。
 */
/* Send route notify request to zebra */
extern int zebra_route_notify_send(int command, struct zclient *zclient,
				   bool set);

/*
 * 带状态感知的重分发：仅当该 (afi,type,instance) 的订阅状态发生变化时，
 * 才更新内部状态并调用 zebra_redistribute_send 通知 zebra(避免重复下发)。
 * 返回 void。
 */
/* If state has changed, update state and call zebra_redistribute_send. */
extern void zclient_redistribute(int command, struct zclient *zclient, afi_t afi, int type,
				 unsigned short instance, vrf_id_t vrf_id);

/* If state has changed, update state and send the command to zebra. */
extern void zclient_redistribute_default(int command, struct zclient *zclient, afi_t afi,
					 vrf_id_t vrf_id);

/*
 * Send the message in zclient->obuf to the zebra daemon (or enqueue it).
 * Returns:
 * -1 on a I/O error
 *  0 data was successfully sent
 *  1 data was buffered for future usage
 */
/*
 * 把 zclient->obuf 中的消息发给 zebra(或排队)：-1 表示 I/O 错误，
 * 0 表示已成功发出，1 表示数据被缓存待发(见 enum zclient_send_status)。
 */
extern enum zclient_send_status zclient_send_message(struct zclient *zclient);

/*
 * 为命令创建报文头并写入 stream s：填入 command、vrf_id、marker、version，
 * 长度字段先占位，待用户 encode 完 payload 后再由调用方回填。
 * 调用后 stream 的写指针位于头部之后，可直接开始编码 payload。
 */
/* create header for command, length to be filled in by user later */
extern void zclient_create_header(struct stream *s, uint16_t command, vrf_id_t vrf_id);
/*
 * Read sizeof(struct zmsghdr) bytes from the provided socket and parse the
 * received data into the specified fields. If this is successful, read the
 * rest of the packet into the provided stream.
 *
 * s
 *    The stream to read into
 *
 * sock
 *    The socket to read from
 *
 * size
 *    Parsed message size will be placed in the pointed-at integer
 *
 * marker
 *    Parsed marker will be placed in the pointed-at byte
 *
 * version
 *    Parsed version will be placed in the pointed-at byte
 *
 * vrf_id
 *    Parsed VRF ID will be placed in the pointed-at vrf_id_t
 *
 * cmd
 *    Parsed command number will be placed in the pointed-at integer
 *
 * Returns:
 *    -1 if:
 *    - insufficient data for header was read
 *    - a version mismatch was detected
 *    - a marker mismatch was detected
 *    - header size field specified more data than could be read
 */
/*
 * 从 socket 读入 sizeof(struct zmsghdr) 字节并解析到各出参，成功后继续把
 * 报文剩余部分读入 stream s。任一情况会返回 -1：头部数据不足、版本不
 * 匹配、marker 不匹配、头部长度字段声称的数据超过可读量。size/marker/
 * version/vrf_id/cmd 分别回填解析结果。
 */
extern int zclient_read_header(struct stream *s, int sock, uint16_t *size,
			       uint8_t *marker, uint8_t *version,
			       vrf_id_t *vrf_id, uint16_t *cmd);
/*
 * Parse header from ZAPI message stream into struct zmsghdr.
 * This function assumes the stream getp points at the first byte of the header.
 * If the function is successful then the stream getp will point to the byte
 * immediately after the last byte of the header.
 *
 * zmsg
 *    The stream containing the header
 *
 * hdr
 *    The header struct to parse into.
 *
 * Returns:
 *    true if parsing succeeded, false otherwise
 */
/*
 * 从已缓冲的 ZAPI 报文中解析头部到 hdr：假定 stream getp 指向头首字节，
 * 成功后 getp 前进到头部之后(即 payload 起点)。解析成功返回 true，
 * 否则 false(stream 位置不变或回退由实现决定)。
 */
extern bool zapi_parse_header(struct stream *zmsg, struct zmsghdr *hdr);

extern enum zclient_send_status zclient_interface_set_arp(struct zclient *client,
							  struct interface *ifp,
							  bool arp_enable);
extern enum zclient_send_status
zclient_interface_set_master(struct zclient *client, struct interface *master,
			     struct interface *slave);
/*
 * 一组「读报文」辅助函数：从已收到的 stream 解出对应对象并落到本进程的
 * 数据结构里(接口/地址/邻居地址/路由器 id/接口 link-params)，返回新建或
 * 更新的对象指针；其中 state_read 读接口状态，address_read 读接口地址，
 * nbr_address_read 读邻居地址，router_id_update_read 读路由器 id 更新，
 * link_params_read/write 读写接口链路参数(changed 回填是否变化)。
 */
extern struct interface *zebra_interface_state_read(struct stream *s, vrf_id_t);
extern struct connected *zebra_interface_address_read(int type, struct stream *s,
						      vrf_id_t vrf_id);
extern struct nbr_connected *
zebra_interface_nbr_address_read(int, struct stream *, vrf_id_t);
extern int zebra_router_id_update_read(struct stream *s, struct prefix *rid);

extern struct interface *zebra_interface_link_params_read(struct stream *s,
							  vrf_id_t vrf_id,
							  bool *changed);
extern size_t zebra_interface_link_params_write(struct stream *s, struct interface *ifp);
extern enum zclient_send_status
zclient_send_get_label_chunk(struct zclient *zclient, uint8_t keep,
			     uint32_t chunk_size, uint32_t base);

/*
 * Label Manager(标签管理器)接口：lm_label_manager_connect 连接标签管理器
 * (async 表示异步连接)；lm_get_label_chunk 申请一段标签(base..end 回填，
 * keep 表示保留已有可用段)；lm_release_label_chunk 释放 [start,end]。
 * 返回 0 成功、非 0 失败。
 */
extern int lm_label_manager_connect(struct zclient *zclient, int async);
extern int lm_get_label_chunk(struct zclient *zclient, uint8_t keep,
			      uint32_t base, uint32_t chunk_size,
			      uint32_t *start, uint32_t *end);
extern int lm_release_label_chunk(struct zclient *zclient, uint32_t start,
				  uint32_t end);
/*
 * Table Manager(表号管理器)接口：tm_table_manager_connect 连接表管理器；
 * tm_get_table_chunk 申请一段表号(chunk_size 大小，[start,end] 回填)；
 * tm_release_table_chunk 释放 [start,end]。返回 0 成功、非 0 失败。
 */
extern int tm_table_manager_connect(struct zclient *zclient);
extern int tm_get_table_chunk(struct zclient *zclient, uint32_t chunk_size,
			      uint32_t *start, uint32_t *end);
extern int tm_release_table_chunk(struct zclient *zclient, uint32_t start,
				  uint32_t end);

/*
 * SRv6 Manager 交互标志：
 *   HAS_SID_VALUE 请求/回带具体 SID 值、HAS_LOCATOR 带 locator 信息、
 *   IS_LOCALONLY 仅本地有效(不对外通告)。
 */
/* Zebra SRv6 Manager flags */
#define ZAPI_SRV6_MANAGER_SID_FLAG_HAS_SID_VALUE 0x01
#define ZAPI_SRV6_MANAGER_SID_FLAG_HAS_LOCATOR	 0x02
#define ZAPI_SRV6_MANAGER_SID_FLAG_IS_LOCALONLY	 0x04

/*
 * SRv6 Manager 接口：向 zebra 的 SRv6 管理器申请/释放 locator chunk 与
 * locator，以及申请/释放 SID。get_sid/release_sid 携带 srv6_sid_ctx、
 * locator 名、sid_func 等。返回 0 成功、非 0 失败；与事件循环配合时结果
 * 经 ZEBRA_SRV6_SID_NOTIFY 异步回报。
 */
extern int srv6_manager_get_locator_chunk(struct zclient *zclient,
					  const char *locator_name);
extern int srv6_manager_release_locator_chunk(struct zclient *zclient,
					      const char *locator_name);
extern int srv6_manager_get_locator(struct zclient *zclient,
				    const char *locator_name);
extern int srv6_manager_get_sid(struct zclient *zclient, const struct srv6_sid_ctx *ctx,
				struct in6_addr *sid_value, const char *locator_name,
				uint32_t *sid_func, bool is_localonly);
extern int srv6_manager_release_sid(struct zclient *zclient, const struct srv6_sid_ctx *ctx,
				    const char *locator_name, bool is_localonly);

/*
 * SR Policy 收发：zebra_send_sr_policy 把策略下发/删除给 zebra(cmd 区分)，
 * zapi_sr_policy_encode/decode 负责线上编解码，
 * zapi_sr_policy_notify_status_decode 解析 zebra 回的策略状态通知。
 */
extern enum zclient_send_status zebra_send_sr_policy(struct zclient *zclient,
						     int cmd,
						     struct zapi_sr_policy *zp);
extern int zapi_sr_policy_encode(struct stream *s, int cmd,
				 struct zapi_sr_policy *zp);
extern int zapi_sr_policy_decode(struct stream *s, struct zapi_sr_policy *zp);
extern int zapi_sr_policy_notify_status_decode(struct stream *s,
					       struct zapi_sr_policy *zp);

/*
 * MPLS 标签收发：zebra_send_mpls_labels 把标签条目(zapi_labels)下发/删除
 * 给 zebra(cmd 区分 ADD/DELETE/REPLACE)；zapi_labels_encode/decode 负责把
 * zapi_labels 编解码到线上字节流。
 */
extern enum zclient_send_status zebra_send_mpls_labels(struct zclient *zclient,
						       int cmd,
						       struct zapi_labels *zl);
extern int zapi_labels_encode(struct stream *s, int cmd,
			      struct zapi_labels *zl);
extern int zapi_labels_decode(struct stream *s, struct zapi_labels *zl);

/*
 * SRv6 locator 及其 chunk 的线上编解码：encode 把结构体写入 stream，
 * decode 从 stream 还原结构体。locator_chunk 表示从 locator 中切分的
 * 一段 SID 前缀空间。
 */
extern int zapi_srv6_locator_encode(struct stream *s,
				    const struct srv6_locator *l);
extern int zapi_srv6_locator_decode(struct stream *s, struct srv6_locator *l);
extern int zapi_srv6_locator_chunk_encode(struct stream *s,
					  const struct srv6_locator_chunk *c);
extern int zapi_srv6_locator_chunk_decode(struct stream *s,
					  struct srv6_locator_chunk *c);

/*
 * 伪线收发：zebra_send_pw 把 zapi_pw(按 command 区分 ADD/DEL/SET/UNSET)下发
 * 给 zebra；zebra_read_pw_status_update 解析 zebra 回传的伪线状态更新到
 * zapi_pw_status。
 */
extern enum zclient_send_status zebra_send_pw(struct zclient *zclient,
					      int command, struct zapi_pw *pw);
extern int zebra_read_pw_status_update(ZAPI_CALLBACK_ARGS,
				       struct zapi_pw_status *pw);

/*
 * 路由下发：zclient_route_send 把一条 zapi_route 按 cmd(ADD/DELETE)发给
 * zebra；zclient_send_rnh 发送 nexthop 注册/注销请求(指定前缀、safi、
 * connected 表示要求直连解析、resolve_via_default 允许经默认路由解析)。
 */
extern enum zclient_send_status zclient_route_send(uint8_t cmd, struct zclient *zclient,
						   struct zapi_route *api);
extern enum zclient_send_status
zclient_send_rnh(struct zclient *zclient, int command, const struct prefix *p,
		 safi_t safi, bool connected, bool resolve_via_default,
		 vrf_id_t vrf_id);
/*
 * 把单个 zapi_nexthop 编码进 stream：api_message 决定哪些可选字段被写入，
 * api_flags 为 ZAPI_NEXTHOP_FLAG_* 标志。返回编码字节数(负数为错误)。
 */
int zapi_nexthop_encode(struct stream *s, const struct zapi_nexthop *api_nh,
			uint32_t api_flags, uint32_t api_message);

/* Init apis for the route and nexthop: these are more efficient than
 * blind memset, so ... use them.
 */
/*
 * 轻量初始化：zapi_route_init 只清零 zapi_route 的“前半段”而非整结构体
 * (避免大 ECMP 场景下整体 memset 的高开销)，大数组字段另行手工处理；
 * zapi_nexthop_init 初始化单个 zapi_nexthop。比盲目 memset 更高效，请优先
 * 使用它们。
 */
void zapi_route_init(struct zapi_route *zr);
void zapi_nexthop_init(struct zapi_nexthop *znh);

/*
 * 路由/nexthop 编解码：encode 把结构体按 cmd 写入 stream，decode 从 stream
 * 还原；zapi_route_decode 只解路由本身(family 相同，字节序与宽度对齐)，
 * zapi_nexthop_decode 依 api_flags/api_message 解出可选字段。
 */
extern int zapi_route_encode(uint8_t cmd, struct stream *s, struct zapi_route *api);
extern int zapi_route_decode(struct stream *s, struct zapi_route *api);
extern int zapi_nexthop_decode(struct stream *s, struct zapi_nexthop *api_nh,
			       uint32_t api_flags, uint32_t api_message);
/*
 * 一组「通知解码」函数：把 zebra 上报的安装结果通知解到出参，成功返回
 * true。nhg_notify 解 NHG 结果(id, note)；route_notify 解路由结果
 * (p/tableid/note/afi/safi)；route_notify_decode_srcdest 额外带源前缀；
 * rule_notify 解 PBR 规则结果；ipset_notify 解 ipset 结果；
 * srv6_sid_notify 解 SRv6 SID 结果(含 SID 值与 locator 名)。
 */
bool zapi_nhg_notify_decode(struct stream *s, uint32_t *id,
			    enum zapi_nhg_notify_owner *note);
bool zapi_route_notify_decode(struct stream *s, struct prefix *p,
			      uint32_t *tableid,
			      enum zapi_route_notify_owner *note,
			      afi_t *afi, safi_t *safi);
bool zapi_route_notify_decode_srcdest(struct stream *s, struct prefix *p, struct prefix *src_p,
				      uint32_t *tableid, enum zapi_route_notify_owner *note,
				      afi_t *afi, safi_t *safi);
bool zapi_rule_notify_decode(struct stream *s, uint32_t *seqno,
			     uint32_t *priority, uint32_t *unique, char *ifname,
			     enum zapi_rule_notify_owner *note);
bool zapi_ipset_notify_decode(struct stream *s,
			      uint32_t *unique,
			     enum zapi_ipset_notify_owner *note);
bool zapi_srv6_sid_notify_decode(struct stream *s, struct srv6_sid_ctx *ctx,
				 struct in6_addr *sid_value, uint32_t *func,
				 uint32_t *wide_func,
				 enum zapi_srv6_sid_notify *note,
				 char **locator_name);

/*
 * nexthop 组消息接口：把 zapi_nhg 按 cmd 下发/删除给 zebra，返回发送状态。
 */
/* Nexthop-group message apis */
extern enum zclient_send_status
zclient_nhg_send(struct zclient *zclient, int cmd, struct zapi_nhg *api_nhg);

#define ZEBRA_IPSET_NAME_SIZE   32

/*
 * 解码 ipset 条目通知(含 ipset 名)与 iptable 通知，回填 unique 与结果枚举，
 * 成功返回 true。
 */
bool zapi_ipset_entry_notify_decode(struct stream *s,
	    uint32_t *unique,
	    char *ipset_name,
	    enum zapi_ipset_entry_notify_owner *note);
bool zapi_iptable_notify_decode(struct stream *s,
		uint32_t *unique,
		enum zapi_iptable_notify_owner *note);

/*
 * 在 ZAPI nexthop 与内部 nexthop 之间转换：
 *   nexthop_from_zapi_nexthop       由 zapi_nexthop 生成(可能新建)内部 nexthop
 *   zapi_nexthop_from_nexthop       由内部 nexthop 填充 zapi_nexthop
 *   zapi_backup_nexthop_from_nexthop 同上，但用于备份 nexthop
 *   zapi_nexthop2str                把 zapi_nexthop 格式化为可读字符串
 */
extern struct nexthop *
nexthop_from_zapi_nexthop(const struct zapi_nexthop *znh);
int zapi_nexthop_from_nexthop(struct zapi_nexthop *znh,
			      const struct nexthop *nh);
int zapi_backup_nexthop_from_nexthop(struct zapi_nexthop *znh,
				     const struct nexthop *nh);
const char *zapi_nexthop2str(const struct zapi_nexthop *znh, char *buf,
			     int bufsize);

/* Decode the zebra error message */
/* 解析 zebra 上报的错误报文，把错误类型写入 error，成功返回 true。 */
extern bool zapi_error_decode(struct stream *s, enum zebra_error_types *error);

/* Encode and decode restart capabilities */
/*
 * 能力协商收发：zclient_capabilities_send 把 zapi_cap 按 cmd 发出(GR 等)，
 * zapi_capabilities_decode 解析报文中携带的能力，返回读取长度或负错误码。
 */
extern enum zclient_send_status
zclient_capabilities_send(uint32_t cmd, struct zclient *zclient,
			  struct zapi_cap *api);
extern int32_t zapi_capabilities_decode(struct stream *s, struct zapi_cap *api);

/*
 * 快捷设置一条黑洞路由：把 nexthop 数置 1，初始化第 0 个 nexthop 为
 * NEXTHOP_TYPE_BLACKHOLE 类型、vrf 为 VRF_DEFAULT、黑洞类型为 bh_type，
 * 并置位 ZAPI_MESSAGE_NEXTHOP 标志，使下发时携带 nexthop 信息。
 */
static inline void zapi_route_set_blackhole(struct zapi_route *api,
					    enum blackhole_type bh_type)
{
	api->nexthop_num = 1;
	zapi_nexthop_init(&api->nexthops[0]);
	api->nexthops[0].type = NEXTHOP_TYPE_BLACKHOLE;
	api->nexthops[0].vrf_id = VRF_DEFAULT;
	api->nexthops[0].bh_type = bh_type;
	SET_FLAG(api->message, ZAPI_MESSAGE_NEXTHOP);
};

/*
 * 让路由改用 nexthop 组：清空松散 nexthop(nexthop_num=0)，把 nhg_id 记入
 * api->nhgid；仅当 id 非 0 时才置位 ZAPI_MESSAGE_NHG 标志，因此传 0 表示
 * 不使用 NHG。
 */
static inline void zapi_route_set_nhg_id(struct zapi_route *api,
					 uint32_t *nhg_id)
{
	api->nexthop_num = 0;
	api->nhgid = *nhg_id;
	if (api->nhgid)
		SET_FLAG(api->message, ZAPI_MESSAGE_NHG);
};

/*
 * MLAG 接口：register 按 bit_map 订阅感兴趣的 MLAG 事件，deregister 注销，
 * data 把一条 MLAG 数据流发给 zebra 转发给对端，返回发送状态。
 */
extern enum zclient_send_status
zclient_send_mlag_register(struct zclient *client, uint32_t bit_map);
extern enum zclient_send_status
zclient_send_mlag_deregister(struct zclient *client);

extern enum zclient_send_status zclient_send_mlag_data(struct zclient *client,
						       struct stream *client_s);

/*
 * Send an OPAQUE message, contents opaque to zebra - but note that
 * the length of the payload is restricted by the zclient's
 * outgoing message buffer.
 * The message header is a message subtype; please use the registry
 * below to avoid sub-type collisions. Clients use the registration
 * apis to manage the specific opaque subtypes they want to receive.
 */
/*
 * 发送一条 OPAQUE 报文：type 为消息子类型(见 zapi_opaque_registry)，data/
 * datasize 为不透明载荷(zebra 不解析，仅按注册关系转发)。载荷长度受
 * zclient 出向缓冲上限约束。返回发送状态。
 */
enum zclient_send_status zclient_send_opaque(struct zclient *zclient,
					     uint32_t type, const uint8_t *data,
					     size_t datasize);

/*
 * 发送单播 OPAQUE 报文：定向到 proto/instance/session_id 指定的目标客户端
 * (对应 ZAPI_OPAQUE_FLAG_UNICAST)。返回发送状态。
 */
enum zclient_send_status
zclient_send_opaque_unicast(struct zclient *zclient, uint32_t type,
			    uint8_t proto, uint16_t instance,
			    uint32_t session_id, const uint8_t *data,
			    size_t datasize);

/* Init functions also provided for clients who want to encode their
 * data inline into the zclient's stream buffer. Please use these instead
 * of hand-encoding the header info, since that may change over time.
 * Note that these will reset the zclient's outbound stream before encoding.
 */
/*
 * 就地初始化 OPAQUE 报文头：重置 zclient 的出向 stream 并写入头部
 * (type 为子类型，flags 为 ZAPI_OPAQUE_FLAG_*)，随后调用方可直接把私有
 * payload 编码进同一 stream。unicast_init 额外带目标客户端三元组。
 * 请用它们而非手工拼头部，格式可能随版本变化。返回发送状态。
 */
enum zclient_send_status zapi_opaque_init(struct zclient *zclient,
					  uint32_t type, uint16_t flags);

enum zclient_send_status
zapi_opaque_unicast_init(struct zclient *zclient, uint32_t type, uint16_t flags,
			 uint8_t proto, uint16_t instance, uint32_t session_id);

/*
 * 解析后的 OPAQUE 报文头信息：
 *   type/len/flags 子类型、本段长度、标志(如 UNICAST)
 *   src_*   发送方客户端三元组(proto/instance/session_id)
 *   dest_*  目的方客户端三元组，仅当 UNICAST 标志置位时有效
 */
/* Struct representing the decoded opaque header info */
struct zapi_opaque_msg {
	uint32_t type; /* Subtype */
	uint16_t len;  /* len after zapi header and this info */
	uint16_t flags;

	/* Sending client info */
	uint8_t src_proto;
	uint16_t src_instance;
	uint32_t src_session_id;

	/* Destination client info - *if* UNICAST flag is set */
	uint8_t dest_proto;
	uint16_t dest_instance;
	uint32_t dest_session_id;
};

/* OPAQUE 报文标志：UNICAST 表示单播给指定目标客户端(携带 dest_* 三元组)。 */
#define ZAPI_OPAQUE_FLAG_UNICAST   0x01

/*
 * OPAQUE 类型注册/注销请求：type 为消息子类型，proto/instance/session_id
 * 标识要注册的客户端会话。
 */
/* Simple struct to convey registration/unreg requests */
struct zapi_opaque_reg_info {
	/* Message subtype */
	uint32_t type;

	/* Client session tuple */
	uint8_t proto;
	uint16_t instance;
	uint32_t session_id;
};

/* Simple struct conveying information about opaque notifications.
 * Daemons can request notifications about the status of registration for
 * opaque message types. For example, a client daemon can request notification
 * when a server registers to receive a certain message code. Or a server can
 * request notification when a subscriber registers for its output.
 */
/*
 * OPAQUE 注册状态通知信息：
 *   request  为 true 表示这是 daemon 发起的需求，false 表示 zebra 的上报
 *   reg      true 表示注册，false 表示注销
 *   msg_type 目标消息码
 *   proto/instance/session_id 客户端会话信息(注册时是被注册者；通知时是
 *            消息服务端/注册者；若无服务端则不存在)
 */
struct zapi_opaque_notif_info {
	bool request;	   /* Request to register, or notification from zebra */
	bool reg;	   /* Register or unregister */
	uint32_t msg_type; /* Target message code */

	/* For notif registration, zapi info for the client.
	 * For notifications, zapi info for the message's server/registrant.
	 * For notification that there is no server/registrant, not present.
	 */
	uint8_t proto;
	uint16_t instance;
	uint32_t session_id;
};

/* The same ZAPI message is used for daemon->zebra requests, and for
 * zebra->daemon notifications.
 * Daemons send 'request' true, and 'reg' true or false.
 * Zebra sends 'request' false, 'reg' set if the notification is a
 * server/receiver registration for the message type, and false if the event
 * is the end of registrations.
 */

/* Decode incoming opaque */
/* 解析收到的 OPAQUE 报文，填充 info(type/src/dest 等)，返回 0 成功。 */
int zclient_opaque_decode(struct stream *msg, struct zapi_opaque_msg *info);

/*
 * 注册/注销某 OPAQUE 子类型的接收：register 表示订阅 type 消息，
 * unregister 表示退订。返回发送状态。另 zapi_opaque_reg_decode 解析
 * zebra 关于注册状态的报文。
 */
enum zclient_send_status zclient_register_opaque(struct zclient *zclient,
						 uint32_t type);
enum zclient_send_status zclient_unregister_opaque(struct zclient *zclient,
						   uint32_t type);
int zapi_opaque_reg_decode(struct stream *msg,
			   struct zapi_opaque_reg_info *info);

/* Opaque notification features */
/*
 * OPAQUE 注册状态通知开关：request_notify 请求在 msgtype 的注册关系变化时
 * 收到通知；drop_notify 取消该通知。返回发送状态。
 */
enum zclient_send_status zclient_opaque_request_notify(struct zclient *zclient,
						       uint32_t msgtype);
enum zclient_send_status zclient_opaque_drop_notify(struct zclient *zclient,
						    uint32_t msgtype);

/* Encode, decode an incoming zapi opaque notification */
/*
 * OPAQUE 注册状态通知的编解码：encode 写入 msg_type、reg(注册/注销)与
 * 客户端三元组；decode 还原到 zapi_opaque_notif_info。返回 0 成功。
 */
int zclient_opaque_notif_encode(struct stream *s, uint32_t msg_type,
				bool reg /* register or unreg*/, uint8_t proto,
				uint16_t instance, uint32_t session_id);
int zclient_opaque_notif_decode(struct stream *s,
				struct zapi_opaque_notif_info *info);

/*
 * Registry of opaque message types. Please do not reuse an in-use
 * type code; some daemons are likely relying on it.
 */
/*
 * OPAQUE 消息子类型注册表：这些编号是全 FRR 共用的，不可复用已占用编号，
 * 否则依赖它的 daemon 会收到错误数据。典型用途：LDP 与 IGP 之间的
 * 链路状态同步(LINK_STATE_*)、LDP-IGP 同步状态(LDP_IGP_SYNC_*)、
 * RLFA 注册与标签分发(LDP_RLFA_*)。
 */
enum zapi_opaque_registry {
	/* Request link-state database dump, at restart for example */
	LINK_STATE_SYNC = 1,
	/* Update containing link-state db info */
	LINK_STATE_UPDATE = 2,
	/* Request LDP-SYNC state from LDP */
	LDP_IGP_SYNC_IF_STATE_REQUEST = 3,
	/* Update containing LDP IGP Sync State info */
	LDP_IGP_SYNC_IF_STATE_UPDATE = 4,
	/* Announce that LDP is up  */
	LDP_IGP_SYNC_ANNOUNCE_UPDATE = 5,
	/* Register RLFA with LDP */
	LDP_RLFA_REGISTER = 7,
	/* Unregister all RLFAs with LDP */
	LDP_RLFA_UNREGISTER_ALL = 8,
	/* Announce LDP labels associated to a previously registered RLFA */
	LDP_RLFA_LABELS = 9,
};

/* Send the hello message.
 * Returns 0 for success or -1 on an I/O error.
 */
/* 发送 HELLO 问候报文(连接建立后握手用)，成功返回 0，I/O 错误返回 -1。 */
extern enum zclient_send_status zclient_send_hello(struct zclient *client);

/*
 * 邻居(neigh)相关：register_neigh 在指定 vrf/afi 上按 reg 订阅或退订邻居
 * 事件；neigh_get 主动请求某接口在 afi 下的邻居信息；
 * send_neigh_discovery_req 针对前缀 p 触发邻居发现。均向 zebra 发请求。
 */
extern void zclient_register_neigh(struct zclient *zclient, vrf_id_t vrf_id,
				   afi_t afi, bool reg);

extern void zclient_neigh_get(struct zclient *zclient, struct interface *ifp, afi_t afi);

extern enum zclient_send_status
zclient_send_neigh_discovery_req(struct zclient *zclient,
				 const struct interface *ifp,
				 const struct prefix *p);

/*
 * 客户端关闭通知(ZEBRA_CLIENT_CLOSE_NOTIFY)载荷：注销客户端的会话三元组
 * proto/instance/session_id。
 */
struct zapi_client_close_info {
	/* Client session tuple */
	uint8_t proto;
	uint16_t instance;
	uint32_t session_id;
};

/* Decode incoming client close notify */
/* 解析客户端关闭通知(ZEBRA_CLIENT_CLOSE_NOTIFY)，填充 info，返回 0 成功。 */
extern int zapi_client_close_notify_decode(struct stream *s,
					   struct zapi_client_close_info *info);

/* 向 zebra 请求为接口 ifp 建立/更新 GRE 隧道信息，返回状态码(0 成功)。 */
extern int zclient_send_zebra_gre_request(struct zclient *client,
					  struct interface *ifp);
#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_ZCLIENT_H */
