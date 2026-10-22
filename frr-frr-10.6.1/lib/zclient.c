// SPDX-License-Identifier: GPL-2.0-or-later
/* Zebra's client library.
 * Copyright (C) 1999 Kunihiro Ishiguro
 * Copyright (C) 2005 Andrew J. Schorr
 */

/*
 * 【文件导读】zclient.c —— Zebra 客户端库(ZAPI 协议实现)
 *
 * 本文件是各个路由守护进程(daemon)与 zebra 进程通信的公共库实现。它把
 * zclient.h 里定义的 ZAPI 数据结构、命令字、回调约定，落实为：连接管理、
 * 报文收发、编解码、以及各业务(cmd)的 send 辅助函数。任何 daemon 只要
 * 创建一个 struct zclient 并注册 handler 表，就能与 zebra 交换路由/接口/
 * 地址/标签/nexthop 等事件。
 *
 * 本模块在系统中的位置：
 *   daemon 侧：业务逻辑 -> 调用本文件各 zclient_xxx_send() -> 编码进 stream
 *             -> 经 unix socket 发给 zebra。
 *   zebra 侧：zebra 的 zserv 模块做对称的解析与处理，并按需把事件回报，
 *             本进程经 zclient_read() 收到后再分发到 handler 表。
 *
 * 阅读顺序建议：
 *   1) 连接与生命周期：zclient_new / zclient_init / zclient_start /
 *      zclient_stop / zclient_reset（以及事件机 zclient_event）。
 *   2) 收发主循环：zclient_read / zclient_read_header / zclient_send_message
 *      以及 create_header / zapi_parse_header。
 *   3) 命令分发表与 handler：zclient_handle_msg / zclient_handler_lookup。
 *   4) 编解码：zapi_route_encode/decode、zapi_nexthop_encode/decode 等，
 *      它们是理解“协议即结构体”的关键。
 *   5) 各业务 send 函数(接口、地址、标签、nexthop、opaque 等)。
 *
 * 易踩的坑：
 *   - 结构体到 byte 流的编码必须逐字段对齐，新增字段要两侧同步。
 *   - zclient_send_message 返回 0/1 都算成功，只有 -1 是错误。
 *   - 本文件很多“读”函数直接操作各 lib 全局表(interface/connected 等)，
 *     调用需在正确的事件上下文里，避免并发或重入。
 */

#include <zebra.h>

#include "prefix.h"
#include "stream.h"
#include "buffer.h"
#include "network.h"
#include "vrf.h"
#include "vrf_int.h"
#include "if.h"
#include "log.h"
#include "frrevent.h"
#include "zclient.h"
#include "memory.h"
#include "table.h"
#include "nexthop.h"
#include "mpls.h"
#include "sockopt.h"
#include "pbr.h"
#include "tc.h"
#include "nexthop_group.h"
#include "lib_errors.h"
#include "srte.h"
#include "printfrr.h"
#include "srv6.h"

DEFINE_MTYPE_STATIC(LIB, ZCLIENT, "Zclient");
DEFINE_MTYPE_STATIC(LIB, REDIST_INST, "Redistribution instance IDs");
DEFINE_MTYPE_STATIC(LIB, REDIST_TABLE_DIRECT, "Redistribution table direct");

/*
 * zclient 内部使用的事件类型，供 zclient_event() 作为参数分发：
 *   ZCLIENT_SCHEDULE 需要调度一次读写(如发送缓冲待写)
 *   ZCLIENT_READ     套接字可读，需要读取并处理一条 ZAPI 报文
 *   ZCLIENT_CONNECT  需要重新建立到 zebra 的连接
 */
/* Zebra client events. */
enum zclient_event { ZCLIENT_SCHEDULE, ZCLIENT_READ, ZCLIENT_CONNECT };

/* Prototype for event manager. */
static void zclient_event(enum zclient_event, struct zclient *);

static void zebra_interface_if_set_value(struct stream *s,
					 struct interface *ifp);

/*
 * 三种预置 zclient 选项：
 *   default     普通异步客户端(非同步、非辅助)——最常见
 *   sync        同步客户端(收完即处理，不用事件循环)，同时置 auxiliary
 *   auxiliary   辅助客户端：不跑 lib 层通用 handler，避免重复处理
 */
const struct zclient_options zclient_options_default = {
	.synchronous = false,
	.auxiliary = false,
};

const struct zclient_options zclient_options_sync = {
	.synchronous = true,
	.auxiliary = true,
};

const struct zclient_options zclient_options_auxiliary = {
	.synchronous = false,
	.auxiliary = true,
};

/*
 * zebra 服务端地址的全局缓存：zclient_addr 保存 zebra 监听套接字地址
 * (unix 抽象/普通 socket 或 TCP)，zclient_addr_len 为其长度，供各客户端
 * 连接时复用，避免每次重算。
 */
struct sockaddr_storage zclient_addr;
socklen_t zclient_addr_len;

/* This file local debug flag. */
static int zclient_debug;

/*
 * 分配并初始化一个 zclient 句柄(不建连接、不注册事件)。
 *   master      所属事件循环
 *   opt         选项(同步/辅助)，一般传 &zclient_options_default
 *   handlers    命令字到处理函数的表
 *   n_handlers  handler 表长度
 * 收发缓冲大小取 max(ZEBRA_MAX_PACKET_SIZ, sizeof(zapi_route))，以容纳
 * 最大的路由报文。返回新建句柄(调用方需用 zclient_free 释放)。
 */
/* Allocate zclient structure. */
struct zclient *zclient_new(struct event_loop *master,
			    const struct zclient_options *opt,
			    zclient_handler *const *handlers, size_t n_handlers)
{
	struct zclient *zclient;
	size_t stream_size =
		MAX(ZEBRA_MAX_PACKET_SIZ, sizeof(struct zapi_route));

	zclient = XCALLOC(MTYPE_ZCLIENT, sizeof(struct zclient));

	zclient->ibuf = stream_new(stream_size);
	zclient->obuf = stream_new(stream_size);
	zclient->wb = buffer_new(0);
	zclient->master = master;

	zclient->handlers = handlers;
	zclient->n_handlers = n_handlers;

	zclient->synchronous = opt->synchronous;
	zclient->auxiliary = opt->auxiliary;

	return zclient;
}

/*
 * 释放 zclient：先释放收/发缓冲与写缓冲，再释放句柄本身。
 * 注意：仅应在退出时调用——因为大量代码不检查 I/O 错误，过早释放会让
 * 仍在引用该句柄的代码拿到悬空指针。
 */
/* This function is only called when exiting, because
   many parts of the code do not check for I/O errors, so they could
   reference an invalid pointer if the structure was ever freed.

   Free zclient structure. */
void zclient_free(struct zclient *zclient)
{
	if (zclient->ibuf)
		stream_free(zclient->ibuf);
	if (zclient->obuf)
		stream_free(zclient->obuf);
	if (zclient->wb)
		buffer_free(zclient->wb);

	XFREE(MTYPE_ZCLIENT, zclient);
}

/* redist->instances 链表的删除回调：释放一个 instance 节点(其数据区是
 * 一个 unsigned short)。 */
static void redist_free_instance(void *data)
{
	XFREE(MTYPE_REDIST_INST, data);
}

/*
 * 在 red 的 instance 链表中查找给定 instance。
 * 返回指向该 instance 值(链节点数据区)的指针，找不到返回 NULL。
 * 副作用：无。调用方常据返回值是否为 NULL 判断“是否已订阅”。
 */
unsigned short *redist_check_instance(struct redist_proto *red,
				      unsigned short instance)
{
	struct listnode *node;
	unsigned short *id;

	if (!red->instances)
		return NULL;

	for (ALL_LIST_ELEMENTS_RO(red->instances, node, id))
		if (*id == instance)
			return id;

	return NULL;
}

/*
 * 给 red 增加一个 instance 订阅：置 enabled=1；若 instances 链表尚未建立
 * 则创建并设置删除回调；随后分配一个 unsigned short 节点插入链表。
 * 不检查重复(调用方通常先用 redist_check_instance 判重)。
 */
void redist_add_instance(struct redist_proto *red, unsigned short instance)
{
	unsigned short *in;

	red->enabled = 1;

	if (!red->instances) {
		red->instances = list_new();
		red->instances->del = redist_free_instance;
	}

	in = XMALLOC(MTYPE_REDIST_INST, sizeof(unsigned short));
	*in = instance;
	listnode_add(red->instances, in);
}

/*
 * 删除 red 中指定 instance 的订阅：先查节点，不存在直接返回；否则删除
 * 节点并释放；若删空则置 enabled=0 并释放整个 instances 链表。
 */
void redist_del_instance(struct redist_proto *red, unsigned short instance)
{
	unsigned short *id;

	id = redist_check_instance(red, instance);
	if (!id)
		return;

	listnode_delete(red->instances, id);
	red->instances->del(id);
	if (!red->instances->count) {
		red->enabled = 0;
		list_delete(&red->instances);
	}
}

/* redist->instances 链表的删除回调(用于 table direct 场景)：释放一个
 * redist_table_direct 节点。 */
static void redist_free_table_direct(void *data)
{
	XFREE(MTYPE_REDIST_TABLE_DIRECT, data);
}

/*
 * 在 red 的订阅链表中按 (vrf_id, table_id) 精确查找 table direct 项。
 * 返回匹配项指针，找不到返回 NULL。
 * 注意：此函数复用 red->instances 链表，但元素的类型是
 * redist_table_direct(与 instance 场景共用同一链表字段、不同元素类型)。
 */
struct redist_table_direct *redist_lookup_table_direct(const struct redist_proto *red,
						       const struct redist_table_direct *table)
{
	struct redist_table_direct *ntable;
	struct listnode *node;

	if (red->instances == NULL)
		return NULL;

	for (ALL_LIST_ELEMENTS_RO(red->instances, node, ntable)) {
		if (table->vrf_id != ntable->vrf_id)
			continue;
		if (table->table_id != ntable->table_id)
			continue;

		return ntable;
	}

	return NULL;
}

/*
 * 判断 red 中是否已存在订阅了给定 table_id 的 table direct 项。
 * 只比较 table_id(忽略 vrf_id)。存在返回 true，否则 false。
 */
bool redist_table_direct_has_id(const struct redist_proto *red, int table_id)
{
	struct redist_table_direct *table;
	struct listnode *node;

	if (red->instances == NULL)
		return false;

	for (ALL_LIST_ELEMENTS_RO(red->instances, node, table)) {
		if (table->table_id != table_id)
			continue;

		return true;
	}

	return false;
}

/*
 * 增加一个 table direct 订阅：先查重(已存在则直接返回)，必要时创建链表
 * 并设置删除回调，置 enabled=1，再分配并填充一个 redist_table_direct
 * 节点插入链表。
 */
void redist_add_table_direct(struct redist_proto *red, const struct redist_table_direct *table)
{
	struct redist_table_direct *ntable;

	ntable = redist_lookup_table_direct(red, table);
	if (ntable != NULL)
		return;

	if (red->instances == NULL) {
		red->instances = list_new();
		red->instances->del = redist_free_table_direct;
	}

	red->enabled = 1;

	ntable = XCALLOC(MTYPE_REDIST_TABLE_DIRECT, sizeof(*ntable));
	ntable->vrf_id = table->vrf_id;
	ntable->table_id = table->table_id;
	listnode_add(red->instances, ntable);
}

/*
 * 删除一个 table direct 订阅：查找到则从链表删除并释放；删空后置
 * enabled=0 并释放整个 instances 链表。
 */
void redist_del_table_direct(struct redist_proto *red, const struct redist_table_direct *table)
{
	struct redist_table_direct *ntable;

	ntable = redist_lookup_table_direct(red, table);
	if (ntable == NULL)
		return;

	listnode_delete(red->instances, ntable);
	red->instances->del(ntable);
	if (red->instances->count == 0) {
		red->enabled = 0;
		list_delete(&red->instances);
	}
}

/*
 * 清空 red 的全部订阅项(释放 instances 链表)。链表为空则直接返回，
 * 不修改 enabled 标志。
 */
void redist_del_all_instances(struct redist_proto *red)
{
	if (!red->instances)
		return;

	list_delete(&red->instances);
}

/*
 * 停止 zclient 服务：取消读/连接/写三个事件，重置 ibuf/obuf，清空写缓冲
 * wb，关闭套接字(置 sock=-1)，清零连接失败计数 fail；随后清理全部
 * redist 状态——按 [afi][route_type] 释放重分发位图、按默认协议类型删除
 * 本客户端的 mi_redist instance、释放 default_information 位图。
 * 不释放句柄本身(那是 zclient_free 的事)。
 */
/* Stop zebra client services. */
void zclient_stop(struct zclient *zclient)
{
	afi_t afi;
	int i;

	if (zclient_debug)
		zlog_debug("zclient %p stopped", zclient);

	/* Stop threads. */
	event_cancel(&zclient->t_read);
	event_cancel(&zclient->t_connect);
	event_cancel(&zclient->t_write);

	/* Reset streams. */
	stream_reset(zclient->ibuf);
	stream_reset(zclient->obuf);

	/* Empty the write buffer. */
	buffer_reset(zclient->wb);

	/* Close socket. */
	if (zclient->sock >= 0) {
		close(zclient->sock);
		zclient->sock = -1;
	}
	zclient->fail = 0;

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++) {
			vrf_bitmap_free(&zclient->redist[afi][i]);
			zclient->redist[afi][i] = VRF_BITMAP_NULL;
		}
		redist_del_instance(
			&zclient->mi_redist[afi][zclient->redist_default],
			zclient->instance);

		vrf_bitmap_free(&zclient->default_information[afi]);
		zclient->default_information[afi] = VRF_BITMAP_NULL;
	}
}

/*
 * 重置 zclient 以便重连：先调用 zclient_stop 停服并清理 redist，再按
 * [afi] 逐一删除本客户端在默认协议类型下的 mi_redist instance(见下一批
 * 读到的循环体)。用于连接断开后回到干净状态再重新 start。
 */
void zclient_reset(struct zclient *zclient)
{
	afi_t afi;

	zclient_stop(zclient);

	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		redist_del_instance(
			&zclient->mi_redist[afi][zclient->redist_default],
			zclient->instance);

	zclient_init(zclient, zclient->redist_default, zclient->instance,
		     zclient->privs);
}

/**
 * Connect to zebra daemon.
 * @param zclient a pointer to zclient structure
 * @return socket fd just to make sure that connection established
 * @see zclient_init
 * @see zclient_new
 */
/*
 * 建立到 zebra 的套接字连接：按 zclient_addr 的地址族创建流式套接字，
 * 设置 FD_CLOEXEC 与发送缓冲(1MB)，connect 到全局 zclient_addr。
 * 成功时把 fd 存入 zclient->sock 并返回该 fd；失败返回 -1(不修改 sock)。
 * 注意：本函数只负责底层连接，不注册事件、不发 hello。
 */
int zclient_socket_connect(struct zclient *zclient)
{
	int sock;
	int ret;

	/* We should think about IPv6 connection. */
	sock = socket(zclient_addr.ss_family, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	set_cloexec(sock);
	setsockopt_so_sendbuf(sock, 1048576);

	/* Connect to zebra. */
	ret = connect(sock, (struct sockaddr *)&zclient_addr, zclient_addr_len);
	if (ret < 0) {
		if (zclient_debug)
			zlog_debug("%s connect failure: %d(%s)", __func__,
				   errno, safe_strerror(errno));
		close(sock);
		return -1;
	}

	zclient->sock = sock;
	return sock;
}

/*
 * 处理一次发送/连接失败：fail 计数加一，调用 zclient_stop 清理连接与
 * redist 状态，再调度一次 ZCLIENT_CONNECT 事件尝试重连，
 * 返回 ZCLIENT_SEND_FAILURE 供调用方直接透传。
 */
static enum zclient_send_status zclient_failed(struct zclient *zclient)
{
	zclient->fail++;
	zclient_stop(zclient);
	zclient_event(ZCLIENT_CONNECT, zclient);
	return ZCLIENT_SEND_FAILURE;
}

/*
 * 写事件回调：尝试把写缓冲 wb 里排队的数据刷到 socket。
 *   - 出错(BUFFER_ERROR)：调用 zclient_failed 关闭并重连
 *   - 仍有剩余(BUFFER_PENDING)：重新注册写事件等待下次可写
 *   - 刷空(BUFFER_EMPTY)：若上层提供 zebra_buffer_write_ready 回调则调用，
 *     通知它“现在可以继续发了”
 * 事件触发时先把 t_write 清空，避免悬空句柄。
 */
static void zclient_flush_data(struct event *event)
{
	struct zclient *zclient = EVENT_ARG(event);

	zclient->t_write = NULL;
	if (zclient->sock < 0)
		return;
	switch (buffer_flush_available(zclient->wb, zclient->sock)) {
	case BUFFER_ERROR:
		flog_err(
			EC_LIB_ZAPI_SOCKET,
			"%s: buffer_flush_available failed on zclient fd %d, closing",
			__func__, zclient->sock);
		zclient_failed(zclient);
		return;
	case BUFFER_PENDING:
		zclient->t_write = NULL;
		event_add_write(zclient->master, zclient_flush_data, zclient,
				zclient->sock, &zclient->t_write);
		break;
	case BUFFER_EMPTY:
		/* Currently only Sharpd and Bgpd has callbacks defined */
		if (zclient->zebra_buffer_write_ready)
			(*zclient->zebra_buffer_write_ready)();
		break;
	}
}

/*
 * Returns:
 * ZCLIENT_SEND_FAILED   - is a failure
 * ZCLIENT_SEND_SUCCESS  - means we sent data to zebra
 * ZCLIENT_SEND_BUFFERED - means we are buffering
 */
/*
 * 把 zclient->obuf 里的整条消息交给写缓冲并尝试发送。
 * socket 未连接直接返回 ZCLIENT_SEND_FAILURE。
 * BUFFER_ERROR   -> zclient_failed(返回 FAILURE)
 * BUFFER_EMPTY   -> 取消写事件，返回 ZCLIENT_SEND_SUCCESS(已全部发出)
 * BUFFER_PENDING -> 注册写事件 zclient_flush_data 等待续发，返回 BUFFERED
 * 注意：obuf 的内容在调用后由 buffer_write 拷贝进 wb，调用方可复用 obuf。
 */
enum zclient_send_status zclient_send_message(struct zclient *zclient)
{
	if (zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;
	switch (buffer_write(zclient->wb, zclient->sock,
			     STREAM_DATA(zclient->obuf),
			     stream_get_endp(zclient->obuf))) {
	case BUFFER_ERROR:
		flog_err(EC_LIB_ZAPI_SOCKET,
			 "%s: buffer_write failed to zclient fd %d, closing",
			 __func__, zclient->sock);
		return zclient_failed(zclient);
	case BUFFER_EMPTY:
		event_cancel(&zclient->t_write);
		return ZCLIENT_SEND_SUCCESS;
	case BUFFER_PENDING:
		event_add_write(zclient->master, zclient_flush_data, zclient,
				zclient->sock, &zclient->t_write);
		return ZCLIENT_SEND_BUFFERED;
	}

	/* should not get here */
	return ZCLIENT_SEND_SUCCESS;
}

/*
 * If we add more data to this structure please ensure that
 * struct zmsghdr in lib/zclient.h is updated as appropriate.
 */
/*
 * 在 stream 头部写入 ZAPI 报文头：先占位写入 ZEBRA_HEADER_SIZE 作为长度，
 * 再依次写入 marker(ZEBRA_HEADER_MARKER=254)、version(ZSERV_VERSION)、
 * vrf_id、command。调用方随后编码 payload，并用 stream_putw_at(s,0,...)
 * 回填真实长度。编码的字段顺序必须与 zmsghdr 一致。
 */
void zclient_create_header(struct stream *s, uint16_t command, vrf_id_t vrf_id)
{
	/* length placeholder, caller can update */
	stream_putw(s, ZEBRA_HEADER_SIZE);
	stream_putc(s, ZEBRA_HEADER_MARKER);
	stream_putc(s, ZSERV_VERSION);
	stream_putl(s, vrf_id);
	stream_putw(s, command);
}

/*
 * 从 socket 读入并解析 ZAPI 报文头，再读入剩余 payload。
 * 步骤：读 ZEBRA_HEADER_SIZE 字节；解析 size/marker/version/vrf_id/cmd；
 * size 减去头长得到 payload 长度；校验 version 与 marker 匹配；最后按
 * 剩余长度把 payload 读入 stream。
 * 返回 0 成功；-1 表示头部不足/版本或 marker 不匹配/payload 读取不足。
 * 出参 size 为 payload 长度(非报文总长)，易踩坑。
 */
int zclient_read_header(struct stream *s, int sock, uint16_t *size,
			uint8_t *marker, uint8_t *version, vrf_id_t *vrf_id,
			uint16_t *cmd)
{
	if (stream_read(s, sock, ZEBRA_HEADER_SIZE) != ZEBRA_HEADER_SIZE)
		return -1;

	STREAM_GETW(s, *size);
	*size -= ZEBRA_HEADER_SIZE;
	STREAM_GETC(s, *marker);
	STREAM_GETC(s, *version);
	STREAM_GETL(s, *vrf_id);
	STREAM_GETW(s, *cmd);

	if (*version != ZSERV_VERSION || *marker != ZEBRA_HEADER_MARKER) {
		flog_err(
			EC_LIB_ZAPI_MISSMATCH,
			"%s: socket %d version mismatch, marker %d, version %d",
			__func__, sock, *marker, *version);
		return -1;
	}

	if (*size && stream_read(s, sock, *size) != *size)
		return -1;

	return 0;
stream_failure:
	return -1;
}

/*
 * 从已缓冲的报文中解析头部到 hdr(length/marker/version/vrf_id/command)，
 * 成功后 stream 的 getp 前进到 payload 起点。返回 true 成功、false 失败
 * (由 STREAM_GET* 失败跳转到 stream_failure 标签)。
 */
bool zapi_parse_header(struct stream *zmsg, struct zmsghdr *hdr)
{
	STREAM_GETW(zmsg, hdr->length);
	STREAM_GETC(zmsg, hdr->marker);
	STREAM_GETC(zmsg, hdr->version);
	STREAM_GETL(zmsg, hdr->vrf_id);
	STREAM_GETW(zmsg, hdr->command);
	return true;
stream_failure:
	return false;
}

/* Send simple Zebra message. */
/*
 * 发送“纯命令”消息(无 payload)：重置 obuf，写入只含 header 的报文，
 * 再交给 zclient_send_message。用于 HELLO、注册类等无参命令。
 */
static enum zclient_send_status zebra_message_send(struct zclient *zclient,
						   int command, vrf_id_t vrf_id)
{
	struct stream *s;

	/* Get zclient output buffer. */
	s = zclient->obuf;
	stream_reset(s);

	/* Send very simple command only Zebra message. */
	zclient_create_header(s, command, vrf_id);

	return zclient_send_message(zclient);
}

/*
 * 发送 HELLO 问候：仅在设置了 redist_default(即已有协议类型)或为同步
 * 客户端时才发。HELLO 的 vrf_id 恒为 0(VRF_DEFAULT)。payload 依次为
 * redist_default、instance、session_id、以及一个同步标志字节；最后回填
 * 报文长度并发送。未满足条件时直接返回 SUCCESS(表示“无需发送”)。
 */
enum zclient_send_status zclient_send_hello(struct zclient *zclient)
{
	struct stream *s;

	if (zclient->redist_default || zclient->synchronous) {
		s = zclient->obuf;
		stream_reset(s);

		/* The VRF ID in the HELLO message is always 0. */
		zclient_create_header(s, ZEBRA_HELLO, VRF_DEFAULT);
		stream_putc(s, zclient->redist_default);
		stream_putw(s, zclient->instance);
		stream_putl(s, zclient->session_id);
		if (zclient->synchronous)
			stream_putc(s, 1);
		else
			stream_putc(s, 0);

		stream_putw_at(s, 0, stream_get_endp(s));
		return zclient_send_message(zclient);
	}

	return ZCLIENT_SEND_SUCCESS;
}

/*
 * 下发 VRF 标签(ZEBRA_VRF_LABEL)：payload 为 label(32bit)、afi、标签类型
 * ltype。传 MPLS_LABEL_NONE 表示删除该 VRF 的标签弹出项。返回发送状态。
 */
enum zclient_send_status zclient_send_vrf_label(struct zclient *zclient,
						vrf_id_t vrf_id, afi_t afi,
						mpls_label_t label,
						enum lsp_types_t ltype)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_VRF_LABEL, vrf_id);
	stream_putl(s, label);
	stream_putc(s, afi);
	stream_putc(s, ltype);
	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(zclient);
}

/*
 * 为 localsid 选择一个合法的出接口 oif：优先使用 candidate_oif 对应的
 * 接口；但若该接口不存在或正是 loopback，则改用默认 SRv6 接口(sr0，
 * DEFAULT_SRV6_IFNAME)——因为内核不允许把 SRv6 SID 挂在 loopback 上。
 * 返回选中的 interface，仍找不到则返回 NULL。
 */
static struct interface *select_oif_for_localsid(ifindex_t candidate_oif)
{
	struct interface *ifp;

	ifp = if_lookup_by_index(candidate_oif, VRF_DEFAULT);

	/*
	 * If the candidate outgoing interface (oif) is the loopback interface,
	 * select 'sr0' as the oif instead. The Linux kernel does not permit
	 * attaching SRv6 SIDs to the loopback interface, so 'sr0' must be used
	 * in this case.
	 */
	if (!ifp || if_is_loopback_exact(ifp)) {
		ifp = if_lookup_by_name(DEFAULT_SRV6_IFNAME, VRF_DEFAULT);
		if (!ifp)
			return NULL;
	}

	return ifp;
}

/*
 * 下发/删除一个 SRv6 localsid(端点行为 SID)：
 *   cmd       ZEBRA_ROUTE_ADD 安装 或 ZEBRA_ROUTE_DELETE 删除
 *   sid/prefixlen  SID 地址与前缀长度(超过 IPV6_MAX_BITLEN 直接报错)
 *   oif       期望的出接口(会经 select_oif_for_localsid 校正)
 *   action    seg6local 动作，context 为动作上下文
 * 实现上构造一条 ZAPI 路由：prefix 为 SID，type 为自协议，nexthop 类型
 * IFINDEX 且带 SEG6LOCAL 标志。删除时只发 DELETE，添加时置 ALLOW_RECURSION
 * 与 NEXTHOP 消息标志。返回发送状态。
 */
enum zclient_send_status zclient_send_localsid(struct zclient *zclient, uint8_t cmd,
					       const struct in6_addr *sid, uint16_t prefixlen,
					       ifindex_t oif, enum seg6local_action_t action,
					       const struct seg6local_context *context)
{
	struct prefix_ipv6 p = {};
	struct zapi_route api = {};
	struct zapi_nexthop *znh;
	struct interface *ifp;

	if (prefixlen > IPV6_MAX_BITLEN) {
		flog_warn(EC_LIB_DEVELOPMENT, "%s: wrong prefixlen %u", __func__, prefixlen);
		return ZCLIENT_SEND_FAILURE;
	}

	if (zclient_debug)
		zlog_debug("%s:  |- %s SRv6 SID %pI6 behavior %s", __func__,
			   cmd != ZEBRA_ROUTE_ADD ? "Add" : "Delete", sid,
			   seg6local_action2str(action));

	/*
	 * Select a valid outgoing interface (oif) for attaching the SID.
	 * If the provided oif is valid and not the loopback interface, it is used.
	 * Otherwise, fall back to using the 'sr0' interface.
	 */
	ifp = select_oif_for_localsid(oif);
	if (!ifp) {
		zlog_err("Unable to obtain a valid outgoing interface for installing the SID.");
		zlog_err(
			"Please ensure that a dummy interface named 'sr0' exists and is operational.");
		return ZCLIENT_SEND_FAILURE;
	}

	p.family = AF_INET6;
	p.prefixlen = prefixlen;
	p.prefix = *sid;

	api.vrf_id = VRF_DEFAULT;
	api.type = zclient->redist_default;
	api.instance = 0;
	api.safi = SAFI_UNICAST;
	memcpy(&api.prefix, &p, sizeof(p));

	if (cmd == ZEBRA_ROUTE_DELETE)
		return zclient_route_send(ZEBRA_ROUTE_DELETE, zclient, &api);

	SET_FLAG(api.flags, ZEBRA_FLAG_ALLOW_RECURSION);
	SET_FLAG(api.message, ZAPI_MESSAGE_NEXTHOP);

	znh = &api.nexthops[0];

	memset(znh, 0, sizeof(*znh));

	znh->type = NEXTHOP_TYPE_IFINDEX;
	znh->ifindex = ifp->ifindex;
	SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_SEG6LOCAL);
	znh->seg6local_action = action;
	memcpy(&znh->seg6local_ctx, context, sizeof(struct seg6local_context));

	api.nexthop_num = 1;

	return zclient_route_send(ZEBRA_ROUTE_ADD, zclient, &api);
}

/*
 * 把某 afi 下所有 table_direct 订阅项逐条发给 zebra：type 为
 * ADD/DELETE(实际命令)。遍历 mi_redist[afi][ZEBRA_ROUTE_TABLE_DIRECT]
 * 的 instances 链表(元素为 redist_table_direct)，对每项调用
 * zebra_redistribute_send，带上 table_id 与 vrf_id。
 */
static void zclient_send_table_direct(struct zclient *zclient, afi_t afi, int type)
{
	struct redist_table_direct *table;
	struct redist_proto *red = &zclient->mi_redist[afi][ZEBRA_ROUTE_TABLE_DIRECT];
	struct listnode *node;

	for (ALL_LIST_ELEMENTS_RO(red->instances, node, table))
		zebra_redistribute_send(type, zclient, afi, ZEBRA_ROUTE_TABLE_DIRECT,
					table->table_id, table->vrf_id);
}

/* Send register requests to zebra daemon for the information in a VRF. */
/*
 * 连接建立后向 zebra 注册某个 VRF 的信息(未连接则直接返回)：
 *   1) 请求 router-id、请求接口信息；
 *   2) 把本 VRF 记入自协议类型的 redist 位图；
 *   3) 若为标准 VRF，把已使能的多实例重分发(含 table_direct)逐条补发；
 *   4) 对其它协议类型及默认路由重分发按位图重新下发。
 * 用于连接(或重连)后重建订阅。注意这就是“登录”时的批量注册。
 */
void zclient_send_reg_requests(struct zclient *zclient, vrf_id_t vrf_id)
{
	int i;
	afi_t afi;

	/* If not connected to the zebra yet. */
	if (zclient->sock < 0)
		return;

	if (zclient_debug)
		zlog_debug("%s: send register messages for VRF %u", __func__,
			   vrf_id);

	/* We need router-id information. */
	zclient_send_router_id_update(zclient, ZEBRA_ROUTER_ID_ADD, AFI_IP,
				      vrf_id);

	/* We need interface information. */
	zebra_message_send(zclient, ZEBRA_INTERFACE_ADD, vrf_id);

	/* Set unwanted redistribute route. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		vrf_bitmap_set(&zclient->redist[afi][zclient->redist_default],
			       vrf_id);

	/* Flush all redistribute request. */
	if (vrf_id == VRF_DEFAULT) {
		for (afi = AFI_IP; afi < AFI_MAX; afi++) {
			for (i = 0; i < ZEBRA_ROUTE_MAX; i++) {
				if (!zclient->mi_redist[afi][i].enabled)
					continue;

				if (i == ZEBRA_ROUTE_TABLE_DIRECT) {
					zclient_send_table_direct(zclient, afi,
								  ZEBRA_REDISTRIBUTE_ADD);
					continue;
				}

				struct listnode *node;
				unsigned short *id;

				for (ALL_LIST_ELEMENTS_RO(
					     zclient->mi_redist[afi][i]
						     .instances,
					     node, id))
					if (!(i == zclient->redist_default
					      && *id == zclient->instance))
						zebra_redistribute_send(
							ZEBRA_REDISTRIBUTE_ADD,
							zclient, afi, i, *id,
							VRF_DEFAULT);
			}
		}
	}

	/* Resend all redistribute request. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
			if (i != zclient->redist_default &&
			    vrf_bitmap_check(&zclient->redist[afi][i], vrf_id))
				zebra_redistribute_send(ZEBRA_REDISTRIBUTE_ADD,
							zclient, afi, i, 0,
							vrf_id);

		/* If default information is needed. */
		if (vrf_bitmap_check(&zclient->default_information[afi],
				     vrf_id))
			zebra_redistribute_default_send(
				ZEBRA_REDISTRIBUTE_DEFAULT_ADD, zclient, afi,
				vrf_id);
	}
}

/* Send unregister requests to zebra daemon for the information in a VRF. */
/*
 * zclient_send_reg_requests 的逆操作：向 zebra 注销某 VRF 的信息。
 * 结构完全对称——请求删除 router-id、删除接口信息、从 redist 位图移除、
 * 对多实例与默认路由重分发发 DELETE。未连接直接返回。
 */
void zclient_send_dereg_requests(struct zclient *zclient, vrf_id_t vrf_id)
{
	int i;
	afi_t afi;

	/* If not connected to the zebra yet. */
	if (zclient->sock < 0)
		return;

	if (zclient_debug)
		zlog_debug("%s: send deregister messages for VRF %u", __func__,
			   vrf_id);

	/* We need router-id information. */
	zclient_send_router_id_update(zclient, ZEBRA_ROUTER_ID_DELETE, AFI_IP,
				      vrf_id);

	zebra_message_send(zclient, ZEBRA_INTERFACE_DELETE, vrf_id);

	/* Set unwanted redistribute route. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		vrf_bitmap_unset(&zclient->redist[afi][zclient->redist_default],
				 vrf_id);

	/* Flush all redistribute request. */
	if (vrf_id == VRF_DEFAULT) {
		for (afi = AFI_IP; afi < AFI_MAX; afi++) {
			for (i = 0; i < ZEBRA_ROUTE_MAX; i++) {
				if (!zclient->mi_redist[afi][i].enabled)
					continue;

				if (i == ZEBRA_ROUTE_TABLE_DIRECT) {
					zclient_send_table_direct(zclient, afi,
								  ZEBRA_REDISTRIBUTE_DELETE);
					continue;
				}

				struct listnode *node;
				unsigned short *id;

				for (ALL_LIST_ELEMENTS_RO(
					     zclient->mi_redist[afi][i]
						     .instances,
					     node, id))
					if (!(i == zclient->redist_default
					      && *id == zclient->instance))
						zebra_redistribute_send(
							ZEBRA_REDISTRIBUTE_DELETE,
							zclient, afi, i, *id,
							VRF_DEFAULT);
			}
		}
	}

	/* Flush all redistribute request. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
			if (i != zclient->redist_default &&
			    vrf_bitmap_check(&zclient->redist[afi][i], vrf_id))
				zebra_redistribute_send(
					ZEBRA_REDISTRIBUTE_DELETE, zclient, afi,
					i, 0, vrf_id);

		/* If default information is needed. */
		if (vrf_bitmap_check(&zclient->default_information[afi],
				     vrf_id))
			zebra_redistribute_default_send(
				ZEBRA_REDISTRIBUTE_DEFAULT_DELETE, zclient, afi,
				vrf_id);
	}
}

/*
 * 发送 router-id 请求(ADD/DELETE/UPDATE)：payload 仅一个 16bit 的 afi，
 * 回填长度后发送。返回发送状态。
 */
enum zclient_send_status
zclient_send_router_id_update(struct zclient *zclient,
			      zebra_message_types_t type, afi_t afi,
			      vrf_id_t vrf_id)
{
	struct stream *s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, type, vrf_id);
	stream_putw(s, afi);
	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(zclient);
}

/* Send request to zebra daemon to start or stop RA. */
enum zclient_send_status
/*
 * 请求 zebra 在接口 ifp 上开启(enable 非零，ZEBRA_INTERFACE_ENABLE_RADV)
 * 或停止(ZEBRA_INTERFACE_DISABLE_RADV)RA 路由通告，payload 为 ifindex 与
 * ra_interval。未连接返回 FAILURE。返回发送状态。
 */
zclient_send_interface_radv_req(struct zclient *zclient, vrf_id_t vrf_id,
				struct interface *ifp, int enable,
				uint32_t ra_interval)
{
	struct stream *s;

	/* If not connected to the zebra yet. */
	if (zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;

	/* Form and send message. */
	s = zclient->obuf;
	stream_reset(s);

	if (enable)
		zclient_create_header(s, ZEBRA_INTERFACE_ENABLE_RADV, vrf_id);
	else
		zclient_create_header(s, ZEBRA_INTERFACE_DISABLE_RADV, vrf_id);

	stream_putl(s, ifp->ifindex);
	stream_putl(s, ra_interval);

	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

enum zclient_send_status
/*
 * 设置接口的 proto-down 状态(ZEBRA_INTERFACE_SET_PROTODOWN)：payload 为
 * ifindex 与一个字节的 !!down。未连接返回 FAILURE。返回发送状态。
 */
zclient_send_interface_protodown(struct zclient *zclient, vrf_id_t vrf_id,
				 struct interface *ifp, bool down)
{
	struct stream *s;

	if (zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_INTERFACE_SET_PROTODOWN, vrf_id);
	stream_putl(s, ifp->ifindex);
	stream_putc(s, !!down);
	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(zclient);
}

/* Make connection to zebra daemon. */
/*
 * 建立到 zebra 的连接并启动客户端服务。
 * 已连接(sock>=0)或已在重连(t_connect 存在)则直接返回 0。
 * 成功路径：建连接 -> 设非阻塞 -> 清零 fail -> 注册读事件 -> 发 HELLO ->
 * 发 ZEBRA_INTERFACE_ADD 请求初始接口信息 -> 回调 zebra_connected。
 * 失败路径：fail++，调度 ZCLIENT_CONNECT 重连，返回 -1。
 */
int zclient_start(struct zclient *zclient)
{
	if (zclient_debug)
		zlog_info("zclient_start is called");

	/* If already connected to the zebra. */
	if (zclient->sock >= 0)
		return 0;

	/* Check connect thread. */
	if (zclient->t_connect)
		return 0;

	if (zclient_socket_connect(zclient) < 0) {
		if (zclient_debug)
			zlog_debug("zclient connection fail");
		zclient->fail++;
		zclient_event(ZCLIENT_CONNECT, zclient);
		return -1;
	}

	if (set_nonblocking(zclient->sock) < 0)
		flog_err(EC_LIB_ZAPI_SOCKET, "%s: set_nonblocking(%d) failed",
			 __func__, zclient->sock);

	/* Clear fail count. */
	zclient->fail = 0;
	if (zclient_debug)
		zlog_debug("zclient connect success with socket [%d]",
			   zclient->sock);

	/* Create read thread. */
	zclient_event(ZCLIENT_READ, zclient);

	zclient_send_hello(zclient);

	zebra_message_send(zclient, ZEBRA_INTERFACE_ADD, VRF_DEFAULT);

	/* Inform the successful connection. */
	if (zclient->zebra_connected)
		(*zclient->zebra_connected)(zclient);

	return 0;
}

/* Initialize zebra client.  Argument redist_default is unwanted
   redistribute route type. */
/*
 * 初始化 zclient：redist_default 是本客户端自身的协议类型。
 *   sock 置 -1、记录 privs；清空全部 redist 位图；
 *   记录 redist_default/instance；按 afi 把本实例加入 mi_redist，
 *   并初始化 default_information 位图。
 * 注意：redist_default 即“不需要重分发给自己”的类型(如 bgpd 不需要 BGP)。
 * 本函数不建连接，连接由 zclient_start 负责。
 */
void zclient_init(struct zclient *zclient, int redist_default,
		  unsigned short instance, struct zebra_privs_t *privs)
{
	int afi, i;

	/* Set -1 to the default socket value. */
	zclient->sock = -1;
	zclient->privs = privs;

	/* Clear redistribution flags. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
			vrf_bitmap_init(&zclient->redist[afi][i]);

	/* Set unwanted redistribute route.  bgpd does not need BGP route
	   redistribution. */
	zclient->redist_default = redist_default;
	zclient->instance = instance;
	/* Pending: make afi(s) an arg. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		redist_add_instance(&zclient->mi_redist[afi][redist_default],
				    instance);

		/* Set default-information redistribute to zero. */
		vrf_bitmap_init(&zclient->default_information[afi]);
	}

	if (zclient_debug)
		zlog_debug("scheduling zclient connection");

	zclient_event(ZCLIENT_SCHEDULE, zclient);
}

/* This function is a wrapper function for calling zclient_start from
   timer or event thread. */
/*
 * 连接事件回调：从事件参数取 zclient，清空 t_connect 句柄(表示本次连接
 * 尝试结束)，然后调用 zclient_start 真正建立连接。它只是给
 * zclient_start 套一层事件入口，便于从定时器/事件线程触发重连。
 */
static void zclient_connect(struct event *t)
{
	struct zclient *zclient;

	zclient = EVENT_ARG(t);
	zclient->t_connect = NULL;

	if (zclient_debug)
		zlog_debug("zclient_connect is called");

	zclient_start(zclient);
}

/*
 * 发送 nexthop 注册/注销请求(RNH)：command 为
 * ZEBRA_NEXTHOP_REGISTER/UNREGISTER。
 *   p             关注的前缀
 *   safi          子地址族
 *   connected     true 表示要求必须是直连解析(connected route)
 *   resolve_via_def true 表示允许经默认路由解析
 * payload：connected(1B)、resolve_via_def(1B)、safi(2B)、family(2B)、
 * prefixlen(1B) 及前缀字节(按 AF_INET/AF_INET6 分别编码)。返回发送状态。
 */
enum zclient_send_status zclient_send_rnh(struct zclient *zclient, int command,
					  const struct prefix *p, safi_t safi,
					  bool connected, bool resolve_via_def,
					  vrf_id_t vrf_id)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, command, vrf_id);
	stream_putc(s, (connected) ? 1 : 0);
	stream_putc(s, (resolve_via_def) ? 1 : 0);
	stream_putw(s, safi);
	stream_putw(s, PREFIX_FAMILY(p));
	stream_putc(s, p->prefixlen);
	switch (PREFIX_FAMILY(p)) {
	case AF_INET:
		stream_put_in_addr(s, &p->u.prefix4);
		break;
	case AF_INET6:
		stream_put(s, &(p->u.prefix6), 16);
		break;
	default:
		break;
	}
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/*
 * "xdr_encode"-like interface that allows daemon (client) to send
 * a message to zebra server for a route that needs to be
 * added/deleted to the kernel. Info about the route is specified
 * by the caller in a struct zapi_route. zapi_route_encode() then writes
 * the info down the zclient socket using the stream_* functions.
 *
 * The corresponding read ("xdr_decode") function on the server
 * side is zapi_route_decode().
 *
 * If ZAPI_MESSAGE_DISTANCE is set, the distance value is written as a 1
 * byte value.
 *
 * If ZAPI_MESSAGE_METRIC is set, the metric value is written as a 4
 * byte value.
 *
 * If ZAPI_MESSAGE_TAG is set, the tag value is written as a 4 byte value
 *
 * If ZAPI_MESSAGE_MTU is set, the mtu value is written as a 4 byte value
 *
 * XXX: No attention paid to alignment.
 */
enum zclient_send_status
/*
 * 把一条路由下发给 zebra：先 zapi_route_encode 编码进 obuf(失败返回
 * FAILURE)，再 zclient_send_message 发出。cmd 为 ZEBRA_ROUTE_ADD/DELETE。
 * 这是 daemon 通告路由到内核的主入口。
 */
zclient_route_send(uint8_t cmd, struct zclient *zclient, struct zapi_route *api)
{
	if (zapi_route_encode(cmd, zclient->obuf, api) < 0)
		return ZCLIENT_SEND_FAILURE;
	return zclient_send_message(zclient);
}

/*
 * nexthop 排序比较函数之一：比较标签部分。先比 label_num，相同再按
 * 标签字节 memcmp。返回 <0、0、>0(与 qsort 语义一致)。
 */
static int zapi_nexthop_labels_cmp(const struct zapi_nexthop *next1,
				   const struct zapi_nexthop *next2)
{
	if (next1->label_num > next2->label_num)
		return 1;

	if (next1->label_num < next2->label_num)
		return -1;

	return memcmp(next1->labels, next2->labels, next1->label_num);
}

/*
 * nexthop 排序比较函数之一：比较 SRv6 相关字段。依次比较 seg6_segs、
 * srv6_encap_behavior、seg6local_action、seg6local_ctx。返回比较结果。
 */
static int zapi_nexthop_srv6_cmp(const struct zapi_nexthop *next1,
				 const struct zapi_nexthop *next2)
{
	int ret = 0;

	ret = memcmp(&next1->seg6_segs, &next2->seg6_segs,
		     sizeof(struct in6_addr));
	if (ret != 0)
		return ret;

	if (next1->srv6_encap_behavior > next2->srv6_encap_behavior)
		return 1;

	if (next1->srv6_encap_behavior < next2->srv6_encap_behavior)
		return -1;

	if (next1->seg6local_action > next2->seg6local_action)
		return 1;

	if (next1->seg6local_action < next2->seg6local_action)
		return -1;

	return memcmp(&next1->seg6local_ctx, &next2->seg6local_ctx,
		      sizeof(struct seg6local_context));
}

/*
 * nexthop 排序比较函数之一(不含标签)：依次比较 vrf_id、type、weight；
 * 再按 type 分支比较 gate/ifindex/bh_type；再比较 srte_color；最后若有
 * 备份 nexthop(HAS_BACKUP)则比较 backup_num 与 backup_idx。
 * 返回比较结果。排序规则必须与 lib/nexthop.c 保持一致。
 */
static int zapi_nexthop_cmp_no_labels(const struct zapi_nexthop *next1,
				      const struct zapi_nexthop *next2)
{
	int ret = 0;

	if (next1->vrf_id < next2->vrf_id)
		return -1;

	if (next1->vrf_id > next2->vrf_id)
		return 1;

	if (next1->type < next2->type)
		return -1;

	if (next1->type > next2->type)
		return 1;

	if (next1->weight < next2->weight)
		return -1;

	if (next1->weight > next2->weight)
		return 1;

	switch (next1->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV6:
		ret = nexthop_g_addr_cmp(next1->type, &next1->gate,
					 &next2->gate);
		if (ret != 0)
			return ret;
		break;
	case NEXTHOP_TYPE_IPV4_IFINDEX:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		ret = nexthop_g_addr_cmp(next1->type, &next1->gate,
					 &next2->gate);
		if (ret != 0)
			return ret;
		fallthrough;
	case NEXTHOP_TYPE_IFINDEX:
		if (next1->ifindex < next2->ifindex)
			return -1;

		if (next1->ifindex > next2->ifindex)
			return 1;
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		if (next1->bh_type < next2->bh_type)
			return -1;

		if (next1->bh_type > next2->bh_type)
			return 1;
		break;
	}

	if (next1->srte_color < next2->srte_color)
		return -1;
	if (next1->srte_color > next2->srte_color)
		return 1;

	if (CHECK_FLAG(next1->flags, NEXTHOP_FLAG_HAS_BACKUP) ||
	    CHECK_FLAG(next2->flags, NEXTHOP_FLAG_HAS_BACKUP)) {

		if (!CHECK_FLAG(next1->flags, NEXTHOP_FLAG_HAS_BACKUP) &&
		    CHECK_FLAG(next2->flags, NEXTHOP_FLAG_HAS_BACKUP))
			return -1;

		if (CHECK_FLAG(next1->flags, NEXTHOP_FLAG_HAS_BACKUP) &&
		    !CHECK_FLAG(next2->flags, NEXTHOP_FLAG_HAS_BACKUP))
			return 1;

		if (next1->backup_num > 0 || next2->backup_num > 0) {

			if (next1->backup_num < next2->backup_num)
				return -1;

			if (next1->backup_num > next2->backup_num)
				return 1;

			ret = memcmp(next1->backup_idx,
				     next2->backup_idx, next1->backup_num);
			if (ret != 0)
				return ret;
		}
	}

	return 0;
}

/*
 * zapi_nexthop 的完整比较器(供 qsort 使用)：依次调用
 * zapi_nexthop_cmp_no_labels -> zapi_nexthop_labels_cmp ->
 * zapi_nexthop_srv6_cmp，任一步分出胜负即返回。
 */
static int zapi_nexthop_cmp(const void *item1, const void *item2)
{
	int ret = 0;

	const struct zapi_nexthop *next1 = item1;
	const struct zapi_nexthop *next2 = item2;

	ret = zapi_nexthop_cmp_no_labels(next1, next2);
	if (ret != 0)
		return ret;

	ret = zapi_nexthop_labels_cmp(next1, next2);
	if (ret != 0)
		return ret;

	ret = zapi_nexthop_srv6_cmp(next1, next2);

	return ret;
}

/*
 * 对 nexthop 数组就地排序：以 zapi_nexthop_cmp 为比较器，对 nh_grp 的
 * 前 nexthop_num 个元素做 qsort。排序后再编码，保证同一组 nexthop 的
 * 字节流表示稳定一致(便于比较与去重)。
 */
static void zapi_nexthop_group_sort(struct zapi_nexthop *nh_grp,
				    uint16_t nexthop_num)
{
	qsort(nh_grp, nexthop_num, sizeof(struct zapi_nexthop),
	      &zapi_nexthop_cmp);
}

/*
 * Encode a single zapi nexthop
 */
/*
 * 编码单个 zapi_nexthop 进 stream。顺序：vrf_id(4B)、type(1B)、flags(1B)；
 * 依 type 写网关/ifindex/黑洞类型；若 label_num>0 置 LABEL 标志并校验数量，
 * 写 label_num/label_type/labels；weight 非零则置 WEIGHT 标志并写 8B；
 * 依 EVPN 标志写 rmac；依 api_message 的 SRTE 写 color；有备份则写
 * backup_num/backup_idx。返回 0 成功，-1 表示标签数超限等错误。
 * 注意 flags 只按单字节编码，新增标志不能超过 8bit。
 */
int zapi_nexthop_encode(struct stream *s, const struct zapi_nexthop *api_nh,
			uint32_t api_flags, uint32_t api_message)
{
	int i, ret = 0;
	int nh_flags = api_nh->flags;

	stream_putl(s, api_nh->vrf_id);
	stream_putc(s, api_nh->type);

	/* If needed, set 'labelled nexthop' flag */
	if (api_nh->label_num > 0) {
		SET_FLAG(nh_flags, ZAPI_NEXTHOP_FLAG_LABEL);

		/* Validate label count */
		if (api_nh->label_num > MPLS_MAX_LABELS) {
			ret = -1;
			goto done;
		}
	}

	/* If present, set 'weight' flag before encoding flags */
	if (api_nh->weight)
		SET_FLAG(nh_flags, ZAPI_NEXTHOP_FLAG_WEIGHT);

	/* Note that we're only encoding a single octet */
	stream_putc(s, nh_flags);

	switch (api_nh->type) {
	case NEXTHOP_TYPE_BLACKHOLE:
		stream_putc(s, api_nh->bh_type);
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		stream_put_in_addr(s, &api_nh->gate.ipv4);
		stream_putl(s, api_nh->ifindex);
		break;
	case NEXTHOP_TYPE_IFINDEX:
		stream_putl(s, api_nh->ifindex);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		stream_write(s, (uint8_t *)&api_nh->gate.ipv6,
			     16);
		stream_putl(s, api_nh->ifindex);
		break;
	}

	/* We only encode labels if we have >0 - we use
	 * the per-nexthop flag above to signal that the count
	 * is present in the payload.
	 */
	if (api_nh->label_num > 0) {
		stream_putc(s, api_nh->label_num);
		stream_putc(s, api_nh->label_type);
		stream_put(s, &api_nh->labels[0],
			   api_nh->label_num * sizeof(mpls_label_t));
	}

	if (api_nh->weight)
		stream_putq(s, api_nh->weight);

	/* Router MAC for EVPN routes. */
	if (CHECK_FLAG(nh_flags, ZAPI_NEXTHOP_FLAG_EVPN))
		stream_put(s, &(api_nh->rmac),
			   sizeof(struct ethaddr));

	/* Color for Segment Routing TE. */
	if (CHECK_FLAG(api_message, ZAPI_MESSAGE_SRTE))
		stream_putl(s, api_nh->srte_color);

	/* Index of backup nexthop */
	if (CHECK_FLAG(nh_flags, ZAPI_NEXTHOP_FLAG_HAS_BACKUP)) {
		/* Validate backup count */
		if (api_nh->backup_num > NEXTHOP_MAX_BACKUPS) {
			ret = -1;
			goto done;
		}

		stream_putc(s, api_nh->backup_num);
		for (i = 0; i < api_nh->backup_num; i++)
			stream_putc(s, api_nh->backup_idx[i]);
	}

	if (CHECK_FLAG(nh_flags, ZAPI_NEXTHOP_FLAG_SEG6LOCAL)) {
		stream_putl(s, api_nh->seg6local_action);
		stream_write(s, &api_nh->seg6local_ctx,
			     sizeof(struct seg6local_context));
	}

	if (CHECK_FLAG(nh_flags, ZAPI_NEXTHOP_FLAG_SEG6)) {
		stream_putc(s, api_nh->seg_num);
		stream_put(s, &api_nh->seg6_segs[0],
			   api_nh->seg_num * sizeof(struct in6_addr));
		stream_putl(s, api_nh->srv6_encap_behavior);
	}
done:
	return ret;
}

/*
 * 编码 SRv6 locator chunk：locator 名(2B 长度+字节)、prefixlen(2B)、
 * prefix 字节、block/node/function/argument_bits_length 各 1B、flags 1B。
 * 返回 0。
 */
int zapi_srv6_locator_chunk_encode(struct stream *s,
				   const struct srv6_locator_chunk *c)
{
	stream_putw(s, strlen(c->locator_name));
	stream_put(s, c->locator_name, strlen(c->locator_name));
	stream_putw(s, c->prefix.prefixlen);
	stream_put(s, &c->prefix.prefix, sizeof(c->prefix.prefix));
	stream_putc(s, c->block_bits_length);
	stream_putc(s, c->node_bits_length);
	stream_putc(s, c->function_bits_length);
	stream_putc(s, c->argument_bits_length);
	stream_putc(s, c->flags);
	return 0;
}

/*
 * 解码 SRv6 locator chunk(与 chunk_encode 对称)。family 固定 AF_INET6。
 * 若 locator 名长度超过 SRV6_LOCNAME_SIZE 或流数据不足，跳转
 * stream_failure 返回 -1；成功返回 0。
 */
int zapi_srv6_locator_chunk_decode(struct stream *s,
				   struct srv6_locator_chunk *c)
{
	uint16_t len = 0;

	c->prefix.family = AF_INET6;

	STREAM_GETW(s, len);
	if (len > SRV6_LOCNAME_SIZE)
		goto stream_failure;

	STREAM_GET(c->locator_name, s, len);
	STREAM_GETW(s, c->prefix.prefixlen);
	STREAM_GET(&c->prefix.prefix, s, sizeof(c->prefix.prefix));
	STREAM_GETC(s, c->block_bits_length);
	STREAM_GETC(s, c->node_bits_length);
	STREAM_GETC(s, c->function_bits_length);
	STREAM_GETC(s, c->argument_bits_length);
	STREAM_GETC(s, c->flags);
	return 0;

stream_failure:
	return -1;
}

/*
 * 编码 SRv6 locator：locator 名(2B 长度+字节)、prefixlen(2B)、prefix 字节、
 * 各 bits_length 1B、flags 1B。返回 0。
 */
int zapi_srv6_locator_encode(struct stream *s, const struct srv6_locator *l)
{
	stream_putw(s, strlen(l->name));
	stream_put(s, l->name, strlen(l->name));
	stream_putw(s, l->prefix.prefixlen);
	stream_put(s, &l->prefix.prefix, sizeof(l->prefix.prefix));
	stream_putc(s, l->block_bits_length);
	stream_putc(s, l->node_bits_length);
	stream_putc(s, l->function_bits_length);
	stream_putc(s, l->argument_bits_length);
	stream_putc(s, l->flags);
	return 0;
}

/*
 * 解码 SRv6 locator(与 locator_encode 对称)：读名字(校验长度上限)、
 * prefix、各 bits_length 与 flags，family 置 AF_INET6。失败返回 -1，
 * 成功返回 0。
 */
int zapi_srv6_locator_decode(struct stream *s, struct srv6_locator *l)
{
	uint16_t len = 0;

	STREAM_GETW(s, len);
	if (len > SRV6_LOCNAME_SIZE)
		goto stream_failure;

	STREAM_GET(l->name, s, len);
	STREAM_GETW(s, l->prefix.prefixlen);
	STREAM_GET(&l->prefix.prefix, s, sizeof(l->prefix.prefix));
	l->prefix.family = AF_INET6;
	STREAM_GETC(s, l->block_bits_length);
	STREAM_GETC(s, l->node_bits_length);
	STREAM_GETC(s, l->function_bits_length);
	STREAM_GETC(s, l->argument_bits_length);
	STREAM_GETC(s, l->flags);
	return 0;

stream_failure:
	return -1;
}

/*
 * 编码 NHG 报文：仅接受 ZEBRA_NHG_ADD/DEL，且 nexthop 数须 ≤ MULTIPATH_NUM。
 * 写 header(VRF_DEFAULT) -> proto、id -> resilience(buckets/idle_timer/
 * unbalanced_timer)；ADD 时再排序并写主/备份 nexthop 数组。返回 0 成功、
 * -1 非法入参。注意只有 ADD 才携带 nexthop 列表。
 */
static int zapi_nhg_encode(struct stream *s, int cmd, struct zapi_nhg *api_nhg)
{
	int i;

	if (cmd != ZEBRA_NHG_DEL && cmd != ZEBRA_NHG_ADD) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: Specified zapi NHG command (%d) doesn't exist",
			 __func__, cmd);
		return -1;
	}

	if (api_nhg->nexthop_num > MULTIPATH_NUM ||
	    api_nhg->backup_nexthop_num > MULTIPATH_NUM) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: zapi NHG encode with invalid input", __func__);
		return -1;
	}

	stream_reset(s);
	zclient_create_header(s, cmd, VRF_DEFAULT);

	stream_putw(s, api_nhg->proto);
	stream_putl(s, api_nhg->id);

	stream_putw(s, api_nhg->resilience.buckets);
	stream_putl(s, api_nhg->resilience.idle_timer);
	stream_putl(s, api_nhg->resilience.unbalanced_timer);

	if (cmd == ZEBRA_NHG_ADD) {
		/* Nexthops */
		zapi_nexthop_group_sort(api_nhg->nexthops,
					api_nhg->nexthop_num);

		stream_putw(s, api_nhg->nexthop_num);

		for (i = 0; i < api_nhg->nexthop_num; i++)
			zapi_nexthop_encode(s, &api_nhg->nexthops[i], 0, 0);

		/* Backup nexthops */
		stream_putw(s, api_nhg->backup_nexthop_num);

		for (i = 0; i < api_nhg->backup_nexthop_num; i++)
			zapi_nexthop_encode(s, &api_nhg->backup_nexthops[i], 0,
					    0);
	}

	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 下发 NHG：把 api_nhg->proto 设为自协议类型(redist_default)，编码进 obuf
 * (失败返回 -1)，再发送。返回发送状态。
 */
enum zclient_send_status zclient_nhg_send(struct zclient *zclient, int cmd,
					  struct zapi_nhg *api_nhg)
{
	api_nhg->proto = zclient->redist_default;

	if (zapi_nhg_encode(zclient->obuf, cmd, api_nhg))
		return -1;

	return zclient_send_message(zclient);
}

/*
 * Init a zapi_route struct more efficiently than a blind "memset"
 */
/*
 * 高效初始化 zapi_route：只 memset 到 nexthop_num 偏移之前的那段
 * (利用结构体中的“marker”划分)，再手工把 nexthop_num / backup_nexthop_num /
 * opaque.length 置 0，从而避免对大数组(nexthops 等)做整体清零的开销。
 */
void zapi_route_init(struct zapi_route *zr)
{
	size_t offset;

	/* We have a marker field in the zapi_route struct; we'll memset
	 * only part of the struct.
	 */
	offset = offsetof(struct zapi_route, nexthop_num);

	memset(zr, 0, offset);

	/* Init some additional fields but avoid init'ing the large arrays */
	zr->nexthop_num = 0;
	zr->backup_nexthop_num = 0;
	zr->opaque.length = 0;
}

/*
 * Init a zapi nexthop struct
 */
/* 初始化单个 zapi_nexthop：整体清零(其体积不大)。 */
void zapi_nexthop_init(struct zapi_nexthop *znh)
{
	memset(znh, 0, sizeof(struct zapi_nexthop));
}

/*
 * 编码一条路由(zapi_route)进 stream。步骤：
 *   重置 stream 并写 header；校验 type < ZEBRA_ROUTE_MAX；写 type/instance/
 *   flags/message(若 message 含 TABLEID 则同步置 flags 的 TABLEID)；校验并
 *   写 safi；写前缀 family/prefixlen/前缀字节；按 SRCPFX 写源前缀；按 NHG
 *   写 nhgid；按 NEXTHOP 校验数量、排序后逐条 zapi_nexthop_encode；按
 *   BACKUP_NEXTHOPS 写备份 nexthop(不排序，按下标索引)；再按各 message 位
 *   写 distance/metric/tag/mtu 等属性。
 * 返回 0 成功，-1 非法 type/safi/nexthop 数或标签数。调用方随后回填长度。
 * 注意 flags/message 为 32bit，长度字段回填由本函数末段完成。
 */
int zapi_route_encode(uint8_t cmd, struct stream *s, struct zapi_route *api)
{
	struct zapi_nexthop *api_nh;
	int i;
	int psize;

	stream_reset(s);
	zclient_create_header(s, cmd, api->vrf_id);

	if (api->type >= ZEBRA_ROUTE_MAX) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: Specified route type (%u) is not a legal value",
			 __func__, api->type);
		return -1;
	}
	stream_putc(s, api->type);

	stream_putw(s, api->instance);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TABLEID))
		SET_FLAG(api->flags, ZEBRA_FLAG_TABLEID);
	stream_putl(s, api->flags);
	stream_putl(s, api->message);

	if (api->safi < SAFI_UNICAST || api->safi >= SAFI_MAX) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: Specified route SAFI (%u) is not a legal value",
			 __func__, api->safi);
		return -1;
	}
	stream_putc(s, api->safi);

	/* Put prefix information. */
	stream_putc(s, api->prefix.family);
	psize = PSIZE(api->prefix.prefixlen);
	stream_putc(s, api->prefix.prefixlen);
	stream_write(s, &api->prefix.u.prefix, psize);

	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_SRCPFX)) {
		psize = PSIZE(api->src_prefix.prefixlen);
		stream_putc(s, api->src_prefix.prefixlen);
		stream_write(s, (uint8_t *)&api->src_prefix.prefix, psize);
	}

	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_NHG))
		stream_putl(s, api->nhgid);

	/* Nexthops.  */
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_NEXTHOP)) {
		/* limit the number of nexthops if necessary */
		if (api->nexthop_num > MULTIPATH_NUM) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: prefix %pFX: can't encode %u nexthops (maximum is %u)",
				__func__, &api->prefix, api->nexthop_num,
				MULTIPATH_NUM);
			return -1;
		}

		/* We canonicalize the nexthops by sorting them; this allows
		 * zebra to resolve the list of nexthops to a nexthop-group
		 * more efficiently.
		 */
		zapi_nexthop_group_sort(api->nexthops, api->nexthop_num);

		stream_putw(s, api->nexthop_num);

		for (i = 0; i < api->nexthop_num; i++) {
			api_nh = &api->nexthops[i];

			/* MPLS labels for BGP-LU or Segment Routing */
			if (api_nh->label_num > MPLS_MAX_LABELS) {
				flog_err(
					EC_LIB_ZAPI_ENCODE,
					"%s: prefix %pFX: can't encode %u labels (maximum is %u)",
					__func__, &api->prefix,
					api_nh->label_num, MPLS_MAX_LABELS);
				return -1;
			}

			if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TABLEID))
				SET_FLAG(api->flags, ZEBRA_FLAG_TABLEID);
			if (zapi_nexthop_encode(s, api_nh, api->flags,
						api->message)
			    != 0)
				return -1;
		}
	}

	/* Backup nexthops  */
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_BACKUP_NEXTHOPS)) {
		/* limit the number of nexthops if necessary */
		if (api->backup_nexthop_num > MULTIPATH_NUM) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: prefix %pFX: can't encode %u backup nexthops (maximum is %u)",
				__func__, &api->prefix, api->backup_nexthop_num,
				MULTIPATH_NUM);
			return -1;
		}

		/* Note that we do not sort the list of backup nexthops -
		 * this list is treated as an array and indexed by each
		 * primary nexthop that is associated with a backup.
		 */

		stream_putw(s, api->backup_nexthop_num);

		for (i = 0; i < api->backup_nexthop_num; i++) {
			api_nh = &api->backup_nexthops[i];

			/* MPLS labels for BGP-LU or Segment Routing */
			if (api_nh->label_num > MPLS_MAX_LABELS) {
				flog_err(
					EC_LIB_ZAPI_ENCODE,
					"%s: prefix %pFX: backup: can't encode %u labels (maximum is %u)",
					__func__, &api->prefix,
					api_nh->label_num, MPLS_MAX_LABELS);
				return -1;
			}

			if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TABLEID))
				SET_FLAG(api->flags, ZEBRA_FLAG_TABLEID);

			if (zapi_nexthop_encode(s, api_nh, api->flags,
						api->message)
			    != 0)
				return -1;
		}
	}

	/* Attributes. */
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_DISTANCE))
		stream_putc(s, api->distance);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_METRIC))
		stream_putl(s, api->metric);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TAG))
		stream_putl(s, api->tag);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_MTU))
		stream_putl(s, api->mtu);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TABLEID))
		stream_putl(s, api->tableid);

	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_OPAQUE)) {
		if (api->opaque.length > ZAPI_MESSAGE_OPAQUE_LENGTH) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: opaque length %u is greater than allowed value",
				__func__, api->opaque.length);
			return -1;
		}

		stream_putw(s, api->opaque.length);
		stream_write(s, api->opaque.data, api->opaque.length);
	}
	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * Decode a single zapi nexthop object
 */
/*
 * 解码单个 zapi_nexthop(与 encode 对称)。先整体 init；读 vrf_id/type/flags；
 * 依 type 读网关/ifindex；依 flags 各标志位读 label、weight、rmac(依
 * api_message 的 SRTE 读 color)、backup、seg6local、seg6 段。对标签数、
 * 备份数、SRv6 段数做上限校验。返回 0 成功，-1 失败(经 stream_failure 或
 * 显式校验)。
 */
int zapi_nexthop_decode(struct stream *s, struct zapi_nexthop *api_nh,
			uint32_t api_flags, uint32_t api_message)
{
	int i, ret = -1;

	zapi_nexthop_init(api_nh);

	STREAM_GETL(s, api_nh->vrf_id);
	STREAM_GETC(s, api_nh->type);

	/* Note that we're only using a single octet of flags */
	STREAM_GETC(s, api_nh->flags);

	switch (api_nh->type) {
	case NEXTHOP_TYPE_BLACKHOLE:
		STREAM_GETC(s, api_nh->bh_type);
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		STREAM_GET(&api_nh->gate.ipv4.s_addr, s,
			   IPV4_MAX_BYTELEN);
		STREAM_GETL(s, api_nh->ifindex);
		break;
	case NEXTHOP_TYPE_IFINDEX:
		STREAM_GETL(s, api_nh->ifindex);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		STREAM_GET(&api_nh->gate.ipv6, s, 16);
		STREAM_GETL(s, api_nh->ifindex);
		break;
	}

	/* MPLS labels for BGP-LU or Segment Routing */
	if (CHECK_FLAG(api_nh->flags, ZAPI_NEXTHOP_FLAG_LABEL)) {
		STREAM_GETC(s, api_nh->label_num);
		STREAM_GETC(s, api_nh->label_type);
		if (api_nh->label_num > MPLS_MAX_LABELS) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: invalid number of MPLS labels (%u)",
				__func__, api_nh->label_num);
			return -1;
		}

		STREAM_GET(&api_nh->labels[0], s,
			   api_nh->label_num * sizeof(mpls_label_t));
	}

	if (CHECK_FLAG(api_nh->flags, ZAPI_NEXTHOP_FLAG_WEIGHT))
		STREAM_GETQ(s, api_nh->weight);

	/* Router MAC for EVPN routes. */
	if (CHECK_FLAG(api_nh->flags, ZAPI_NEXTHOP_FLAG_EVPN))
		STREAM_GET(&(api_nh->rmac), s,
			   sizeof(struct ethaddr));

	/* Color for Segment Routing TE. */
	if (CHECK_FLAG(api_message, ZAPI_MESSAGE_SRTE))
		STREAM_GETL(s, api_nh->srte_color);

	/* Backup nexthop index */
	if (CHECK_FLAG(api_nh->flags, ZAPI_NEXTHOP_FLAG_HAS_BACKUP)) {
		STREAM_GETC(s, api_nh->backup_num);

		if (api_nh->backup_num > NEXTHOP_MAX_BACKUPS)
			return -1;

		for (i = 0; i < api_nh->backup_num; i++)
			STREAM_GETC(s, api_nh->backup_idx[i]);
	}

	if (CHECK_FLAG(api_nh->flags, ZAPI_NEXTHOP_FLAG_SEG6LOCAL)) {
		STREAM_GETL(s, api_nh->seg6local_action);
		STREAM_GET(&api_nh->seg6local_ctx, s,
			   sizeof(struct seg6local_context));
	}

	if (CHECK_FLAG(api_nh->flags, ZAPI_NEXTHOP_FLAG_SEG6)) {
		STREAM_GETC(s, api_nh->seg_num);
		if (api_nh->seg_num > SRV6_MAX_SEGS) {
			flog_err(EC_LIB_ZAPI_ENCODE,
				 "%s: invalid number of SRv6 Segs (%u)",
				 __func__, api_nh->seg_num);
			return -1;
		}

		STREAM_GET(&api_nh->seg6_segs[0], s,
			   api_nh->seg_num * sizeof(struct in6_addr));

		STREAM_GETL(s, api_nh->srv6_encap_behavior);
	}

	/* Success */
	ret = 0;

stream_failure:

	return ret;
}

/*
 * 解码一条路由(与 zapi_route_encode 对称)。先 zapi_route_init；读
 * type/instance/flags/message/safi 并校验；读前缀 family/prefixlen/字节
 * (按 AF_INET/AF_INET6 校验长度)；按 SRCPFX 读源前缀(要求 v4/v6 语义合理)；
 * 按 NHG 读 nhgid；按 NEXTHOP/BACKUP_NEXTHOPS 读各 nexthop(逐个调
 * zapi_nexthop_decode)；最后按各 message 位读 distance/metric/tag/mtu/
 * tableid/opaque。非法取值返回 -1。返回 0 成功。
 */
int zapi_route_decode(struct stream *s, struct zapi_route *api)
{
	struct zapi_nexthop *api_nh;
	int i;

	zapi_route_init(api);

	/* Type, flags, message. */
	STREAM_GETC(s, api->type);
	if (api->type >= ZEBRA_ROUTE_MAX) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: Specified route type: %d is not a legal value",
			 __func__, api->type);
		return -1;
	}

	STREAM_GETW(s, api->instance);
	STREAM_GETL(s, api->flags);
	STREAM_GETL(s, api->message);
	STREAM_GETC(s, api->safi);
	if (api->safi < SAFI_UNICAST || api->safi >= SAFI_MAX) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: Specified route SAFI (%u) is not a legal value",
			 __func__, api->safi);
		return -1;
	}

	/* Prefix. */
	STREAM_GETC(s, api->prefix.family);
	STREAM_GETC(s, api->prefix.prefixlen);
	switch (api->prefix.family) {
	case AF_INET:
		if (api->prefix.prefixlen > IPV4_MAX_BITLEN) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: V4 prefixlen is %d which should not be more than 32",
				__func__, api->prefix.prefixlen);
			return -1;
		}
		break;
	case AF_INET6:
		if (api->prefix.prefixlen > IPV6_MAX_BITLEN) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: v6 prefixlen is %d which should not be more than 128",
				__func__, api->prefix.prefixlen);
			return -1;
		}
		break;
	default:
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: Specified family %d is not v4 or v6", __func__,
			 api->prefix.family);
		return -1;
	}
	STREAM_GET(&api->prefix.u.prefix, s, PSIZE(api->prefix.prefixlen));

	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_SRCPFX)) {
		api->src_prefix.family = AF_INET6;
		STREAM_GETC(s, api->src_prefix.prefixlen);
		if (api->src_prefix.prefixlen > IPV6_MAX_BITLEN) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: SRC Prefix prefixlen received: %d is too large",
				__func__, api->src_prefix.prefixlen);
			return -1;
		}
		STREAM_GET(&api->src_prefix.prefix, s,
			   PSIZE(api->src_prefix.prefixlen));

		if (api->prefix.family != AF_INET6
		    || api->src_prefix.prefixlen == 0) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: SRC prefix specified in some manner that makes no sense",
				__func__);
			return -1;
		}
	}

	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_NHG))
		STREAM_GETL(s, api->nhgid);

	/* Nexthops. */
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_NEXTHOP)) {
		STREAM_GETW(s, api->nexthop_num);
		if (api->nexthop_num > MULTIPATH_NUM) {
			flog_err(EC_LIB_ZAPI_ENCODE,
				 "%s: invalid number of nexthops (%u)",
				 __func__, api->nexthop_num);
			return -1;
		}

		for (i = 0; i < api->nexthop_num; i++) {
			api_nh = &api->nexthops[i];

			if (zapi_nexthop_decode(s, api_nh, api->flags,
						api->message)
			    != 0)
				return -1;
		}
	}

	/* Backup nexthops. */
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_BACKUP_NEXTHOPS)) {
		STREAM_GETW(s, api->backup_nexthop_num);
		if (api->backup_nexthop_num > MULTIPATH_NUM) {
			flog_err(EC_LIB_ZAPI_ENCODE,
				 "%s: invalid number of backup nexthops (%u)",
				 __func__, api->backup_nexthop_num);
			return -1;
		}

		for (i = 0; i < api->backup_nexthop_num; i++) {
			api_nh = &api->backup_nexthops[i];

			if (zapi_nexthop_decode(s, api_nh, api->flags,
						api->message)
			    != 0)
				return -1;
		}
	}

	/* Attributes. */
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_DISTANCE))
		STREAM_GETC(s, api->distance);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_METRIC))
		STREAM_GETL(s, api->metric);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TAG))
		STREAM_GETL(s, api->tag);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_MTU))
		STREAM_GETL(s, api->mtu);
	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_TABLEID))
		STREAM_GETL(s, api->tableid);

	if (CHECK_FLAG(api->message, ZAPI_MESSAGE_OPAQUE)) {
		STREAM_GETW(s, api->opaque.length);
		if (api->opaque.length > ZAPI_MESSAGE_OPAQUE_LENGTH) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: opaque length %u is greater than allowed value",
				__func__, api->opaque.length);
			return -1;
		}

		STREAM_GET(api->opaque.data, s, api->opaque.length);
	}

	return 0;
stream_failure:
	return -1;
}

/*
 * 编码一个前缀：若 p 为 NULL，则用一个 family 指定、其余清零的“任意前缀”
 * 代替。写入 family(1B)、prefixlen(1B)、前缀按 prefix_blen 计算的长度字节。
 */
static void zapi_encode_prefix(struct stream *s, struct prefix *p,
			       uint8_t family)
{
	struct prefix any;

	if (!p) {
		memset(&any, 0, sizeof(any));
		any.family = family;
		p = &any;
	}

	stream_putc(s, p->family);
	stream_putc(s, p->prefixlen);
	stream_put(s, &p->u.prefix, prefix_blen(p));
}

/*
 * 解码一个前缀(与 zapi_encode_prefix 对称)：读 family/prefixlen 与按
 * prefix_blen 计算的字节。成功返回 true，数据不足跳 stream_failure 返回
 * false。
 */
static bool zapi_decode_prefix(struct stream *s, struct prefix *p)
{
	STREAM_GETC(s, p->family);
	STREAM_GETC(s, p->prefixlen);
	STREAM_GET(&(p->u.prefix), s, prefix_blen(p));
	return true;

stream_failure:
	return false;
}

/*
 * 编码一个 sockunion 地址：写入 family(1B) 与按 family2addrsize 得到的地址
 * 字节。若地址族未知(addrlen 为 0)则 assert 失败。无返回值。
 */
static void zapi_encode_sockunion(struct stream *s, const union sockunion *su)
{
	int family = sockunion_family(su);
	size_t addrlen = family2addrsize(family);

	/*
	 * Must know length to encode
	 */
	assert(addrlen);

	stream_putc(s, (uint8_t)family);

	stream_write(s, sockunion_get_addr(su), addrlen);
}

/*
 * 解码一个 sockunion(与 zapi_encode_sockunion 对称)：先清零 su，读 family
 * 写入 su，再按地址族长度读入地址字节。成功返回 true，数据不足返回 false。
 */
static bool zapi_decode_sockunion(struct stream *s, union sockunion *su)
{
	uint8_t family;
	size_t addrlen;
	uint8_t buf[sizeof(union sockunion)];

	memset(su, 0, sizeof(*su));

	STREAM_GETC(s, family);
	sockunion_family(su) = family;

	addrlen = family2addrsize(family);
	if (!addrlen)
		return false;

	if (addrlen > sizeof(buf))
		return false;

	STREAM_GET(buf, s, addrlen);
	sockunion_set(su, family, buf, addrlen);
	return true;

stream_failure:
	return false;
}

/*
 * Encode filter subsection of pbr_rule
 */
/*
 * 编码 PBR 规则的“过滤器”部分：要求源/目的地址族一致且为 v4 或 v6。
 * 写 filter_bm(4B)，随后按 ll 各标志位选择性写 ip_proto、源/目的前缀、
 * 源/目的端口、dsfield 的 DSCP/ECN、以及 L2 的 pcp/vlan_id/vlan_flags。
 */
static void zapi_pbr_rule_filter_encode(struct stream *s, struct pbr_filter *f)
{
	assert(f->src_ip.family == f->dst_ip.family);
	assert((f->src_ip.family == AF_INET) || (f->src_ip.family == AF_INET6));

	stream_putl(s, f->filter_bm);

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_IP_PROTOCOL))
		stream_putc(s, f->ip_proto);

	/* addresses */
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_SRC_IP))
		zapi_encode_prefix(s, &f->src_ip, f->src_ip.family);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_DST_IP))
		zapi_encode_prefix(s, &f->dst_ip, f->dst_ip.family);

	/* port numbers */
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_SRC_PORT))
		stream_putw(s, f->src_port);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_DST_PORT))
		stream_putw(s, f->dst_port);

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_DSCP))
		stream_putc(s, f->dsfield & PBR_DSFIELD_DSCP);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_ECN))
		stream_putc(s, f->dsfield & PBR_DSFIELD_ECN);

	/* vlan */
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_PCP))
		stream_putc(s, f->pcp);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_VLAN_ID))
		stream_putw(s, f->vlan_id);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_VLAN_FLAGS))
		stream_putw(s, f->vlan_flags);


	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_FWMARK))
		stream_putl(s, f->fwmark);
}

/*
 * 解码 PBR 规则过滤器(与 filter_encode 对称)。DSCP/ECN 分别读入临时变量
 * 再合成到 f->dsfield。成功返回 true，数据不足返回 false。
 */
static bool zapi_pbr_rule_filter_decode(struct stream *s, struct pbr_filter *f)
{
	uint8_t dscp = 0;
	uint8_t ecn = 0;

	STREAM_GETL(s, f->filter_bm);

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_IP_PROTOCOL))
		STREAM_GETC(s, f->ip_proto);

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_SRC_IP))
		if (!zapi_decode_prefix(s, &(f->src_ip)))
			goto stream_failure;
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_DST_IP))
		if (!zapi_decode_prefix(s, &(f->dst_ip)))
			goto stream_failure;

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_SRC_PORT))
		STREAM_GETW(s, f->src_port);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_DST_PORT))
		STREAM_GETW(s, f->dst_port);

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_DSCP))
		STREAM_GETC(s, dscp);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_ECN))
		STREAM_GETC(s, ecn);
	f->dsfield = (dscp & PBR_DSFIELD_DSCP) | (ecn & PBR_DSFIELD_ECN);

	/* vlan */
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_PCP))
		STREAM_GETC(s, f->pcp);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_VLAN_ID))
		STREAM_GETW(s, f->vlan_id);
	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_VLAN_FLAGS))
		STREAM_GETW(s, f->vlan_flags);

	if (CHECK_FLAG(f->filter_bm, PBR_FILTER_FWMARK))
		STREAM_GETL(s, f->fwmark);

	return true;

stream_failure:
	return false;
}

/*
 * 编码 PBR 规则的“动作”部分：写 flags(4B)，随后按标志位选择性写 table/
 * queue_id(L3 源/目的地址走 sockunion 编码，端口、DSCP/ECN)以及 L2 的
 * pcp/vlan_id。
 */
static void zapi_pbr_rule_action_encode(struct stream *s, struct pbr_action *a)
{
	stream_putl(s, a->flags);

	if (CHECK_FLAG(a->flags, PBR_ACTION_TABLE))
		stream_putl(s, a->table);
	if (CHECK_FLAG(a->flags, PBR_ACTION_QUEUE_ID))
		stream_putl(s, a->queue_id);

	/* L3 */
	if (CHECK_FLAG(a->flags, PBR_ACTION_SRC_IP))
		zapi_encode_sockunion(s, &a->src_ip);
	if (CHECK_FLAG(a->flags, PBR_ACTION_DST_IP))
		zapi_encode_sockunion(s, &a->dst_ip);
	if (CHECK_FLAG(a->flags, PBR_ACTION_SRC_PORT))
		stream_putw(s, a->src_port);
	if (CHECK_FLAG(a->flags, PBR_ACTION_DST_PORT))
		stream_putw(s, a->dst_port);

	if (CHECK_FLAG(a->flags, PBR_ACTION_DSCP))
		stream_putc(s, a->dscp & PBR_DSFIELD_DSCP);
	if (CHECK_FLAG(a->flags, PBR_ACTION_ECN))
		stream_putc(s, a->ecn & PBR_DSFIELD_ECN);

	/* L2 */
	if (CHECK_FLAG(a->flags, PBR_ACTION_PCP))
		stream_putc(s, a->pcp);
	if (CHECK_FLAG(a->flags, PBR_ACTION_VLAN_ID))
		stream_putw(s, a->vlan_id);
}

/*
 * 解码 PBR 规则动作(与 action_encode 对称)；DSCP/ECN 读出后与掩码相与。
 * 成功返回 true，数据不足返回 false。
 */
static bool zapi_pbr_rule_action_decode(struct stream *s, struct pbr_action *a)
{
	STREAM_GETL(s, a->flags);

	if (CHECK_FLAG(a->flags, PBR_ACTION_TABLE))
		STREAM_GETL(s, a->table);
	if (CHECK_FLAG(a->flags, PBR_ACTION_QUEUE_ID))
		STREAM_GETL(s, a->queue_id);

	/* L3 */
	if (CHECK_FLAG(a->flags, PBR_ACTION_SRC_IP)) {
		if (!zapi_decode_sockunion(s, &(a->src_ip)))
			goto stream_failure;
	}
	if (CHECK_FLAG(a->flags, PBR_ACTION_DST_IP))
		if (!zapi_decode_sockunion(s, &(a->dst_ip)))
			goto stream_failure;

	if (CHECK_FLAG(a->flags, PBR_ACTION_SRC_PORT))
		STREAM_GETW(s, a->src_port);
	if (CHECK_FLAG(a->flags, PBR_ACTION_DST_PORT))
		STREAM_GETW(s, a->dst_port);

	if (CHECK_FLAG(a->flags, PBR_ACTION_DSCP)) {
		STREAM_GETC(s, a->dscp);
		a->dscp &= PBR_DSFIELD_DSCP;
	}
	if (CHECK_FLAG(a->flags, PBR_ACTION_ECN)) {
		STREAM_GETC(s, a->ecn);
		a->ecn &= PBR_DSFIELD_ECN;
	}

	/* L2 */
	if (CHECK_FLAG(a->flags, PBR_ACTION_PCP))
		STREAM_GETC(s, a->pcp);
	if (CHECK_FLAG(a->flags, PBR_ACTION_VLAN_ID))
		STREAM_GETW(s, a->vlan_id);

	return true;

stream_failure:
	return false;
}

/*
 * 编码一条 PBR 规则：先写固定的记录数 1，再写 family/seq/priority/unique，
 * 接着依次编码 filter 与 action 子段，最后写 ifname(固定 IFNAMSIZ 字节)，
 * 回填长度。返回 0。
 */
int zapi_pbr_rule_encode(struct stream *s, struct pbr_rule *r)
{
	/*
	 * PBR record count is always 1
	 */
	stream_putl(s, 1);

	stream_putc(s, r->family);
	stream_putl(s, r->seq);
	stream_putl(s, r->priority);
	stream_putl(s, r->unique);

	zapi_pbr_rule_filter_encode(s, &(r->filter));
	zapi_pbr_rule_action_encode(s, &(r->action));

	stream_put(s, r->ifname, IFNAMSIZ);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 解码一条 PBR 规则(与 encode 对称)。注意：调用方须已读取开头的 4 字节
 * 规则计数。清零 r 后读 family/seq/priority/unique，再解码 filter/action
 * 子段与 ifname。成功返回 true，失败 false。
 */
bool zapi_pbr_rule_decode(struct stream *s, struct pbr_rule *r)
{
	/* NB caller has already read 4-byte rule count */

	memset(r, 0, sizeof(*r));

	STREAM_GETC(s, r->family);
	STREAM_GETL(s, r->seq);
	STREAM_GETL(s, r->priority);
	STREAM_GETL(s, r->unique);

	if (!zapi_pbr_rule_filter_decode(s, &(r->filter)))
		goto stream_failure;
	if (!zapi_pbr_rule_action_decode(s, &(r->action)))
		goto stream_failure;

	STREAM_GET(r->ifname, s, IFNAMSIZ);
	return true;

stream_failure:
	return false;
}

/*
 * 编码 TC qdisc 报文：header(cmd) + 记录数 1 + ifindex + kind，回填长度。
 * 返回 0。
 */
int zapi_tc_qdisc_encode(uint8_t cmd, struct stream *s, struct tc_qdisc *qdisc)
{
	stream_reset(s);
	zclient_create_header(s, cmd, VRF_DEFAULT);


	stream_putl(s, 1);

	stream_putl(s, qdisc->ifindex);
	stream_putl(s, qdisc->kind);

	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 编码 TC class 报文：header(cmd) + 记录数 1 + ifindex/handle/kind；对
 * HTB 类型再写 rate/ceil；其它类型不写额外字段。回填长度，返回 0。
 */
int zapi_tc_class_encode(uint8_t cmd, struct stream *s, struct tc_class *class)
{
	stream_reset(s);
	zclient_create_header(s, cmd, VRF_DEFAULT);

	stream_putl(s, 1);

	stream_putl(s, class->ifindex);
	stream_putl(s, class->handle);
	stream_putl(s, class->kind);

	switch (class->kind) {
	case TC_QDISC_HTB:
		stream_putq(s, class->u.htb.rate);
		stream_putq(s, class->u.htb.ceil);
		break;
	case TC_QDISC_UNSPEC:
	case TC_QDISC_NOQUEUE:
		/* not implemented */
		break;
	}
	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 编码 TC filter 报文：header(cmd) + 记录数 1 + ifindex/handle/priority/
 * protocol/kind；对 flower 类型再按 filter_bm 选择性写 ip_proto、源/目的
 * 前缀、端口区间、dsfield 与掩码、classid。回填长度，返回 0。
 */
int zapi_tc_filter_encode(uint8_t cmd, struct stream *s,
			  struct tc_filter *filter)
{
	stream_reset(s);
	zclient_create_header(s, cmd, VRF_DEFAULT);

	stream_putl(s, 1);

	stream_putl(s, filter->ifindex);
	stream_putl(s, filter->handle);
	stream_putl(s, filter->priority);
	stream_putl(s, filter->protocol);
	stream_putl(s, filter->kind);

	switch (filter->kind) {
	case TC_FILTER_FLOWER:
		stream_putl(s, filter->u.flower.filter_bm);
		if (filter->u.flower.filter_bm & TC_FLOWER_IP_PROTOCOL)
			stream_putc(s, filter->u.flower.ip_proto);
		if (filter->u.flower.filter_bm & TC_FLOWER_SRC_IP)
			zapi_encode_prefix(s, &filter->u.flower.src_ip,
					   filter->u.flower.src_ip.family);
		if (filter->u.flower.filter_bm & TC_FLOWER_SRC_PORT) {
			stream_putw(s, filter->u.flower.src_port_min);
			stream_putw(s, filter->u.flower.src_port_max);
		}
		if (filter->u.flower.filter_bm & TC_FLOWER_DST_IP)
			zapi_encode_prefix(s, &filter->u.flower.dst_ip,
					   filter->u.flower.dst_ip.family);
		if (filter->u.flower.filter_bm & TC_FLOWER_DST_PORT) {
			stream_putw(s, filter->u.flower.dst_port_min);
			stream_putw(s, filter->u.flower.dst_port_max);
		}
		if (filter->u.flower.filter_bm & TC_FLOWER_DSFIELD) {
			stream_putc(s, filter->u.flower.dsfield);
			stream_putc(s, filter->u.flower.dsfield_mask);
		}
		stream_putl(s, filter->u.flower.classid);
		break;
	case TC_FILTER_UNSPEC:
	case TC_FILTER_BPF:
	case TC_FILTER_FLOW:
	case TC_FILTER_U32:
		/* not implemented */
		break;
	}

	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 解码 NHG 安装结果通知：读 note(结果枚举)与 id，回填出参。成功返回 true，
 * 数据不足返回 false。
 */
bool zapi_nhg_notify_decode(struct stream *s, uint32_t *id,
			    enum zapi_nhg_notify_owner *note)
{
	uint32_t read_id;

	STREAM_GET(note, s, sizeof(*note));
	STREAM_GETL(s, read_id);

	*id = read_id;

	return true;

stream_failure:
	return false;
}

/*
 * 解码路由安装结果通知(不带源前缀)：用一个 dummy 源前缀转调
 * zapi_route_notify_decode_srcdest。返回 true/false。
 */
bool zapi_route_notify_decode(struct stream *s, struct prefix *p,
			      uint32_t *tableid,
			      enum zapi_route_notify_owner *note,
			      afi_t *afi, safi_t *safi)
{
	struct prefix dummy;

	return zapi_route_notify_decode_srcdest(s, p, &dummy, tableid, note, afi, safi);
}

/*
 * 解码路由安装结果通知(带源前缀)：读 note、目的前缀、源前缀、tableid、
 * afi/safi 并回填出参(afi/safi 指针可为 NULL 表示不关心)。成功 true，
 * 失败 false。
 */
bool zapi_route_notify_decode_srcdest(struct stream *s, struct prefix *p, struct prefix *src_p,
				      uint32_t *tableid, enum zapi_route_notify_owner *note,
				      afi_t *afi, safi_t *safi)
{
	uint32_t t;
	afi_t afi_val;
	safi_t safi_val;

	STREAM_GET(note, s, sizeof(*note));

	STREAM_GETC(s, p->family);
	STREAM_GETC(s, p->prefixlen);
	STREAM_GET(&p->u.prefix, s, prefix_blen(p));
	src_p->family = p->family;
	STREAM_GETC(s, src_p->prefixlen);
	STREAM_GET(&src_p->u.prefix, s, prefix_blen(src_p));
	STREAM_GETL(s, t);
	STREAM_GETC(s, afi_val);
	STREAM_GETC(s, safi_val);

	*tableid = t;

	if (afi)
		*afi = afi_val;
	if (safi)
		*safi = safi_val;

	return true;

stream_failure:
	return false;
}

/*
 * 解码 PBR 规则安装结果通知：读 note 与 seqno/priority/unique/ifname 并
 * 回填。成功返回 true，失败 false。
 */
bool zapi_rule_notify_decode(struct stream *s, uint32_t *seqno,
			     uint32_t *priority, uint32_t *unique, char *ifname,
			     enum zapi_rule_notify_owner *note)
{
	uint32_t prio, seq, uni;

	STREAM_GET(note, s, sizeof(*note));

	STREAM_GETL(s, seq);
	STREAM_GETL(s, prio);
	STREAM_GETL(s, uni);
	STREAM_GET(ifname, s, IFNAMSIZ);

	if (zclient_debug)
		zlog_debug("%s: %u %u %u %s", __func__, seq, prio, uni, ifname);
	*seqno = seq;
	*priority = prio;
	*unique = uni;

	return true;

stream_failure:
	return false;
}

bool zapi_ipset_notify_decode(struct stream *s, uint32_t *unique,
			      enum zapi_ipset_notify_owner *note)
{
	uint32_t uni;
	uint16_t notew;

	STREAM_GETW(s, notew);

	STREAM_GETL(s, uni);

	if (zclient_debug)
		zlog_debug("%s: %u", __func__, uni);
	*unique = uni;
	*note = (enum zapi_ipset_notify_owner)notew;
	return true;

stream_failure:
	return false;
}

/*
 * 解码 ipset 条目通知：读结果(按 16bit 读入)与 unique，再读 ipset 名
 * (固定 ZEBRA_IPSET_NAME_SIZE 字节)。回填 unique/note。成功 true，失败
 * false。
 */
bool zapi_ipset_entry_notify_decode(struct stream *s, uint32_t *unique,
				    char *ipset_name,
				    enum zapi_ipset_entry_notify_owner *note)
{
	uint32_t uni;
	uint16_t notew;

	STREAM_GETW(s, notew);

	STREAM_GETL(s, uni);

	STREAM_GET(ipset_name, s, ZEBRA_IPSET_NAME_SIZE);

	if (zclient_debug)
		zlog_debug("%s: %u", __func__, uni);
	*unique = uni;
	*note = (enum zapi_ipset_entry_notify_owner)notew;

	return true;

stream_failure:
	return false;
}

/*
 * 解码 iptable 通知：读结果(16bit)与 unique 并回填。成功 true，失败 false。
 */
bool zapi_iptable_notify_decode(struct stream *s,
		uint32_t *unique,
		enum zapi_iptable_notify_owner *note)
{
	uint32_t uni;
	uint16_t notew;

	STREAM_GETW(s, notew);

	STREAM_GETL(s, uni);

	if (zclient_debug)
		zlog_debug("%s: %u", __func__, uni);
	*unique = uni;
	*note = (enum zapi_iptable_notify_owner)notew;

	return true;

stream_failure:
	return false;
}

/*
 * 解码 SRv6 SID 通知：读 note、sid_ctx、sid_value、func/wide_func(可空)、
 * 以及 locator 名长度与名字。locator 名用函数内 static 缓冲存放并回传
 * 指针(故非线程安全，且下次调用会覆盖)。名字超长返回 false。成功 true。
 */
bool zapi_srv6_sid_notify_decode(struct stream *s, struct srv6_sid_ctx *ctx,
				 struct in6_addr *sid_value, uint32_t *func,
				 uint32_t *wide_func,
				 enum zapi_srv6_sid_notify *note,
				 char **p_locator_name)
{
	uint32_t f, wf;
	uint16_t len;
	static char locator_name[SRV6_LOCNAME_SIZE];

	STREAM_GET(note, s, sizeof(*note));
	STREAM_GET(ctx, s, sizeof(struct srv6_sid_ctx));
	STREAM_GET(sid_value, s, sizeof(struct in6_addr));
	STREAM_GETL(s, f);
	STREAM_GETL(s, wf);

	if (func)
		*func = f;
	if (wide_func)
		*wide_func = wf;

	STREAM_GETW(s, len);
	if (len > SRV6_LOCNAME_SIZE) {
		*p_locator_name = NULL;
		return false;
	}
	if (p_locator_name) {
		if (len == 0)
			*p_locator_name = NULL;
		else {
			memset(locator_name, 0, sizeof(locator_name));
			STREAM_GET(locator_name, s, len);
			*p_locator_name = locator_name;
		}
	}
	return true;

stream_failure:
	return false;
}

/*
 * 由 zapi_nexthop 构造一个内部 nexthop(nexthop_new 分配)：复制 type/vrf/
 * ifindex，按 type 复制网关或黑洞类型(注意只复制有意义字段，保证剩余
 * 字节为 0 以便哈希一致)；复制 color/weight/标签/备份/seg6local/seg6 段。
 * 返回新建的 nexthop(调用方负责释放)。
 */
struct nexthop *nexthop_from_zapi_nexthop(const struct zapi_nexthop *znh)
{
	struct nexthop *n = nexthop_new();

	n->type = znh->type;
	n->vrf_id = znh->vrf_id;
	n->ifindex = znh->ifindex;

	/* only copy values that have meaning - make sure "spare bytes" are
	 * left zeroed for hashing (look at _nexthop_hash_bytes)
	 */
	switch (znh->type) {
	case NEXTHOP_TYPE_BLACKHOLE:
		n->bh_type = znh->bh_type;
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		n->gate.ipv4 = znh->gate.ipv4;
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		n->gate.ipv6 = znh->gate.ipv6;
		break;
	case NEXTHOP_TYPE_IFINDEX:
		/* nothing, ifindex is always copied */
		break;
	}

	n->srte_color = znh->srte_color;
	n->weight = znh->weight;

	/*
	 * This function currently handles labels
	 */
	if (znh->label_num) {
		nexthop_add_labels(n, ZEBRA_LSP_NONE, znh->label_num,
				   znh->labels);
	}

	if (CHECK_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_HAS_BACKUP)) {
		SET_FLAG(n->flags, NEXTHOP_FLAG_HAS_BACKUP);
		n->backup_num = znh->backup_num;
		memcpy(n->backup_idx, znh->backup_idx, n->backup_num);
	}

	if (znh->seg6local_action != ZEBRA_SEG6_LOCAL_ACTION_UNSPEC)
		nexthop_add_srv6_seg6local(n, znh->seg6local_action,
					   &znh->seg6local_ctx);

	if (znh->seg_num && !sid_zero_ipv6(znh->seg6_segs))
		nexthop_add_srv6_seg6(n, &znh->seg6_segs[0], znh->seg_num, znh->srv6_encap_behavior);

	return n;
}

/*
 * Convert nexthop to zapi nexthop
 */
/*
 * 由内部 nexthop 填充 zapi_nexthop(与 nexthop_from_zapi_nexthop 反向)：
 * 先 init，复制 type/vrf/weight/ifindex/gate/color 与 ONLINK/EVPN 标志；
 * 再复制标签(校验数量上限)、备份(校验上限)、以及 SRv6 seg6local/seg6 段。
 * 返回 0 成功，-1 表示标签或备份数超限。
 */
int zapi_nexthop_from_nexthop(struct zapi_nexthop *znh,
			      const struct nexthop *nh)
{
	int i;

	zapi_nexthop_init(znh);

	znh->type = nh->type;
	znh->vrf_id = nh->vrf_id;
	znh->weight = nh->weight;
	znh->ifindex = nh->ifindex;
	znh->gate = nh->gate;
	znh->srte_color = nh->srte_color;

	if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_ONLINK))
		SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_ONLINK);

	if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_EVPN))
		SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_EVPN);

	if (nh->nh_label && (nh->nh_label->num_labels > 0)) {

		/* Validate */
		if (nh->nh_label->num_labels > MPLS_MAX_LABELS)
			return -1;

		for (i = 0; i < nh->nh_label->num_labels; i++)
			znh->labels[i] = nh->nh_label->label[i];

		znh->label_num = i;
		znh->label_type = nh->nh_label_type;
		SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_LABEL);
	}

	if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_HAS_BACKUP)) {
		if (nh->backup_num > NEXTHOP_MAX_BACKUPS)
			return -1;

		SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_HAS_BACKUP);
		znh->backup_num = nh->backup_num;
		memcpy(znh->backup_idx, nh->backup_idx, znh->backup_num);
	}

	if (nh->nh_srv6) {
		if (nh->nh_srv6->seg6local_action !=
		    ZEBRA_SEG6_LOCAL_ACTION_UNSPEC) {
			SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_SEG6LOCAL);
			znh->seg6local_action = nh->nh_srv6->seg6local_action;
			memcpy(&znh->seg6local_ctx,
			       &nh->nh_srv6->seg6local_ctx,
			       sizeof(struct seg6local_context));
		}

		if (nh->nh_srv6->seg6_segs && nh->nh_srv6->seg6_segs->num_segs &&
		    !sid_zero(nh->nh_srv6->seg6_segs)) {
			SET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_SEG6);
			znh->seg_num = nh->nh_srv6->seg6_segs->num_segs;
			for (i = 0; i < nh->nh_srv6->seg6_segs->num_segs; i++)
				memcpy(&znh->seg6_segs[i],
				       &nh->nh_srv6->seg6_segs->seg[i],
				       sizeof(struct in6_addr));
			znh->srv6_encap_behavior = nh->nh_srv6->seg6_segs->encap_behavior;
		}
	}

	return 0;
}

/*
 * Wrapper that converts backup nexthop
 */
/*
 * 把内部 nexthop 转成“备份” zapi_nexthop 的包装：调用
 * zapi_nexthop_from_nexthop，成功后清除 HAS_BACKUP 标志(因为备份 nexthop
 * 自身不应再带备份)。返回转换结果。
 */
int zapi_backup_nexthop_from_nexthop(struct zapi_nexthop *znh,
				     const struct nexthop *nh)
{
	int ret;

	/* Ensure that zapi flags are correct: backups don't have backups */
	ret = zapi_nexthop_from_nexthop(znh, nh);
	if (ret == 0)
		UNSET_FLAG(znh->flags, ZAPI_NEXTHOP_FLAG_HAS_BACKUP);

	return ret;
}

/*
 * Format some info about a zapi nexthop, for debug or logging.
 */
/*
 * 把 zapi_nexthop 格式化为可读字符串写入调用方缓冲 buf(长度 bufsize)，
 * 按 type 输出 "if N"、"addr if N" 或 "blackhole"。返回 buf(便于链式使用)。
 * 仅用于日志/调试，不做严格边界处理。
 */
const char *zapi_nexthop2str(const struct zapi_nexthop *znh, char *buf,
			     int bufsize)
{
	char tmp[INET6_ADDRSTRLEN];

	switch (znh->type) {
	case NEXTHOP_TYPE_IFINDEX:
		snprintf(buf, bufsize, "if %u", znh->ifindex);
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		inet_ntop(AF_INET, &znh->gate.ipv4, tmp, sizeof(tmp));
		snprintf(buf, bufsize, "%s if %u", tmp, znh->ifindex);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		inet_ntop(AF_INET6, &znh->gate.ipv6, tmp, sizeof(tmp));
		snprintf(buf, bufsize, "%s if %u", tmp, znh->ifindex);
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		snprintf(buf, bufsize, "blackhole");
		break;
	default:
		snprintf(buf, bufsize, "unknown");
		break;
	}

	return buf;
}

/*
 * Decode the nexthop-tracking update message
 */
/*
 * 解码 nexthop-tracking(NHT)更新消息：读 message/safi；读“要求匹配的”
 * 前缀到 match；读“实际匹配到的”前缀到 nhr->prefix；按 SRTE 读 color；
 * 读 type/instance/distance/metric 与 nexthop_num，再逐个解码 nexthop 到
 * nhr。成功返回 true，失败 false。match 与 nhr->prefix 的区别是上层做
 * LPM 决策的关键。
 */
static bool zapi_nexthop_update_decode(struct stream *s, struct prefix *match,
				       struct zapi_route *nhr)
{
	uint32_t i;

	zapi_route_init(nhr);

	STREAM_GETL(s, nhr->message);
	STREAM_GETW(s, nhr->safi);
	STREAM_GETW(s, match->family);
	STREAM_GETC(s, match->prefixlen);
	/*
	 * What we got told to match against
	 */
	switch (match->family) {
	case AF_INET:
		STREAM_GET(&match->u.prefix4.s_addr, s, IPV4_MAX_BYTELEN);
		break;
	case AF_INET6:
		STREAM_GET(&match->u.prefix6, s, IPV6_MAX_BYTELEN);
		break;
	}
	/*
	 * What we matched against
	 */
	STREAM_GETW(s, nhr->prefix.family);
	STREAM_GETC(s, nhr->prefix.prefixlen);
	switch (nhr->prefix.family) {
	case AF_INET:
		STREAM_GET(&nhr->prefix.u.prefix4.s_addr, s, IPV4_MAX_BYTELEN);
		break;
	case AF_INET6:
		STREAM_GET(&nhr->prefix.u.prefix6, s, IPV6_MAX_BYTELEN);
		break;
	default:
		break;
	}
	if (CHECK_FLAG(nhr->message, ZAPI_MESSAGE_SRTE))
		STREAM_GETL(s, nhr->srte_color);

	STREAM_GETC(s, nhr->type);
	STREAM_GETW(s, nhr->instance);
	STREAM_GETC(s, nhr->distance);
	STREAM_GETL(s, nhr->metric);
	STREAM_GETW(s, nhr->nexthop_num);

	for (i = 0; i < nhr->nexthop_num; i++) {
		if (zapi_nexthop_decode(s, &(nhr->nexthops[i]), 0, 0) != 0)
			return false;
	}

	return true;
stream_failure:
	return false;
}

/*
 * 解码 zebra 错误报文：清零 error，读入错误类型(大小按枚举)，调试时打印。
 * 成功返回 true，数据不足返回 false。
 */
bool zapi_error_decode(struct stream *s, enum zebra_error_types *error)
{
	memset(error, 0, sizeof(*error));

	STREAM_GET(error, s, sizeof(*error));

	if (zclient_debug)
		zlog_debug("%s: type: %s", __func__,
			   zebra_error_type2str(*error));

	return true;
stream_failure:
	return false;
}

/*
 * send a ZEBRA_REDISTRIBUTE_ADD or ZEBRA_REDISTRIBUTE_DELETE
 * for the route type (ZEBRA_ROUTE_KERNEL etc.). The zebra server will
 * then set/unset redist[type] in the client handle (a struct zserv) for the
 * sending client
 */
enum zclient_send_status
/*
 * 发送 ZEBRA_REDISTRIBUTE_ADD/DELETE：payload 为 afi(1B)、type(1B)、
 * instance(2B)。zebra 收到后会为发送方(struct zserv)设置/清除
 * redist[type]。返回发送状态。
 */
zebra_redistribute_send(int command, struct zclient *zclient, afi_t afi,
			int type, unsigned short instance, vrf_id_t vrf_id)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, command, vrf_id);
	stream_putc(s, afi);
	stream_putc(s, type);
	stream_putw(s, instance);

	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

enum zclient_send_status
/*
 * 发送默认路由重分发(ADD/DELETE)：payload 仅 afi(1B)。返回发送状态。
 */
zebra_redistribute_default_send(int command, struct zclient *zclient, afi_t afi,
				vrf_id_t vrf_id)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, command, vrf_id);
	stream_putc(s, afi);

	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/* Send route notify request to zebra */
/*
 * 请求/取消路由安装结果通知：vrf 恒为 0，payload 为一个 !!set 字节。
 * 返回发送状态。
 */
int zebra_route_notify_send(int command, struct zclient *zclient, bool set)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, command, 0);
	stream_putc(s, !!set);

	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/* Get prefix in ZServ format; family should be filled in on prefix */
/*
 * 从 stream 读取 ZServ 格式前缀：p->family 须已填好，用 prefix_blen 推长度；
 * 读地址字节后再读 1 字节 prefixlen，取 MIN(plen*8, c) 作为最终 prefixlen。
 * 长度为 0 或数据不足返回 -1，成功返回 0。
 */
static int zclient_stream_get_prefix(struct stream *s, struct prefix *p)
{
	size_t plen = prefix_blen(p);
	uint8_t c;
	p->prefixlen = 0;

	if (plen == 0)
		return -1;

	STREAM_GET(&p->u.prefix, s, plen);
	STREAM_GETC(s, c);
	p->prefixlen = MIN(plen * 8, c);

	return 0;
stream_failure:
	return -1;
}

/* Router-id update from zebra daemon. */
/*
 * 读取 zebra 上报的路由器 id 更新：先读 family 到 rid->family，再用
 * zclient_stream_get_prefix 读前缀体。返回 0 成功、-1 失败。
 */
int zebra_router_id_update_read(struct stream *s, struct prefix *rid)
{
	/* Fetch interface address. */
	STREAM_GETC(s, rid->family);

	return zclient_stream_get_prefix(s, rid);

stream_failure:
	return -1;
}

/* Interface addition from zebra daemon. */
/*
 * The format of the message sent with type ZEBRA_INTERFACE_ADD or
 * ZEBRA_INTERFACE_DELETE from zebra to the client is:
 *     0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifname                                                       |
 * |                                                               |
 * |                                                               |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifindex                                                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  status       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  if_flags                                                     |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  metric                                                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  speed                                                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifmtu                                                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifmtu6                                                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  bandwidth                                                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  parent ifindex                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Link Layer Type                                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Harware Address Length                                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Hardware Address      if HW length different from 0          |
 * |   ...                  max INTERFACE_HWADDR_MAX               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Link_params? |  Whether a link-params follows: 1 or 0.
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Link_params    0 or 1 INTERFACE_LINK_PARAMS_SIZE sized       |
 * |   ....          (struct if_link_params).                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

/*
 * 处理 VRF 新增(ZEBRA_VRF_ADD)：读 vrf_data 结构体与 VRF 名(VRF_NAMSIZ
 * 字节，须非空)，按 name 获取/创建 vrf，写入 table_id 与 netns 名，再
 * vrf_enable 使能。名解析失败返回 -1。
 */
static int zclient_vrf_add(ZAPI_CALLBACK_ARGS)
{
	struct vrf *vrf;
	char vrfname_tmp[VRF_NAMSIZ + 1] = {};
	struct vrf_data data;

	STREAM_GET(&data, zclient->ibuf, sizeof(struct vrf_data));
	/* Read interface name. */
	STREAM_GET(vrfname_tmp, zclient->ibuf, VRF_NAMSIZ);

	if (strlen(vrfname_tmp) == 0)
		goto stream_failure;

	/* Lookup/create vrf by name, then vrf_id. */
	vrf = vrf_get(vrf_id, vrfname_tmp);

	/* If there's already a VRF with this name, don't create vrf */
	if (!vrf)
		return 0;

	vrf->data.l.table_id = data.l.table_id;
	memcpy(vrf->data.l.netns_name, data.l.netns_name, NS_NAMSIZ);
	vrf_enable(vrf);

	return 0;
stream_failure:
	return -1;
}

/*
 * 处理 VRF 删除(ZEBRA_VRF_DELETE)：按 vrf_id 查找，找不到(本协议不认识
 * 该 VRF)直接返回 0，否则调用 vrf_delete。
 */
static int zclient_vrf_delete(ZAPI_CALLBACK_ARGS)
{
	struct vrf *vrf;

	/* Lookup vrf by vrf_id. */
	vrf = vrf_lookup_by_id(vrf_id);

	/*
	 * If a routing protocol doesn't know about a
	 * vrf that is about to be deleted.  There is
	 * no point in attempting to delete it.
	 */
	if (!vrf)
		return 0;

	vrf_delete(vrf);
	return 0;
}

/*
 * 处理接口新增(ZEBRA_INTERFACE_ADD)：读接口名，按 vrf_id 找 VRF(不存在
 * 报错返回 -1)，按名获取/创建 interface，再读后续字段填充接口属性
 * (zebra_interface_if_set_value)，最后触发 if_new_via_zapi 回调。
 */
static int zclient_interface_add(ZAPI_CALLBACK_ARGS)
{
	struct interface *ifp;
	char ifname_tmp[IFNAMSIZ + 1] = {};
	struct stream *s = zclient->ibuf;
	struct vrf *vrf;

	/* Read interface name. */
	STREAM_GET(ifname_tmp, s, IFNAMSIZ);

	/* Lookup/create interface by name. */
	vrf = vrf_lookup_by_id(vrf_id);
	if (!vrf) {
		zlog_debug(
			"Rx'd interface add from Zebra, but VRF %u does not exist",
			vrf_id);
		return -1;
	}

	ifp = if_get_by_name(ifname_tmp, vrf_id, vrf->name);

	zebra_interface_if_set_value(s, ifp);

	if_new_via_zapi(ifp);

	return 0;
stream_failure:
	return -1;
}

/*
 * Read interface up/down msg (ZEBRA_INTERFACE_UP/ZEBRA_INTERFACE_DOWN)
 * from zebra server.  The format of this message is the same as
 * that sent for ZEBRA_INTERFACE_ADD/ZEBRA_INTERFACE_DELETE,
 * except that no sockaddr_dl is sent at the tail of the message.
 */
/*
 * 读取接口 up/down 报文(格式与 INTERFACE_ADD/DELETE 相同，但尾部不含
 * sockaddr_dl)：读接口名，按名在 vrf 中查接口，找不到记录错误返回 NULL，
 * 否则用 zebra_interface_if_set_value 更新其属性并返回该接口。
 */
struct interface *zebra_interface_state_read(struct stream *s, vrf_id_t vrf_id)
{
	struct interface *ifp;
	char ifname_tmp[IFNAMSIZ + 1] = {};

	/* Read interface name. */
	STREAM_GET(ifname_tmp, s, IFNAMSIZ);

	/* Lookup this by interface index. */
	ifp = if_lookup_by_name(ifname_tmp, vrf_id);
	if (ifp == NULL) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "INTERFACE_STATE: Cannot find IF %s in VRF %d",
			 ifname_tmp, vrf_id);
		return NULL;
	}

	zebra_interface_if_set_value(s, ifp);

	return ifp;
stream_failure:
	return NULL;
}

/*
 * 处理接口删除(ZEBRA_INTERFACE_DELETE)：经 zebra_interface_state_read 找到
 * 接口(找不到返回 0)，再 if_destroy_via_zapi 销毁。
 */
static int zclient_interface_delete(ZAPI_CALLBACK_ARGS)
{
	struct interface *ifp;
	struct stream *s = zclient->ibuf;

	ifp = zebra_interface_state_read(s, vrf_id);

	if (ifp == NULL)
		return 0;

	if_destroy_via_zapi(ifp);
	return 0;
}

/*
 * 处理接口 up(ZEBRA_INTERFACE_UP)：经 zebra_interface_state_read 找到并更新
 * 接口，再 if_up_via_zapi。找不到返回 0。
 */
static int zclient_interface_up(ZAPI_CALLBACK_ARGS)
{
	struct interface *ifp;
	struct stream *s = zclient->ibuf;

	ifp = zebra_interface_state_read(s, vrf_id);

	if (!ifp)
		return 0;

	if_up_via_zapi(ifp);
	return 0;
}

/*
 * 处理接口 down(ZEBRA_INTERFACE_DOWN)：经 zebra_interface_state_read 找到并
 * 更新接口，再 if_down_via_zapi。找不到返回 0。
 */
static int zclient_interface_down(ZAPI_CALLBACK_ARGS)
{
	struct interface *ifp;
	struct stream *s = zclient->ibuf;

	ifp = zebra_interface_state_read(s, vrf_id);

	if (!ifp)
		return 0;

	if_down_via_zapi(ifp);
	return 0;
}

/*
 * 处理 zebra 错误报文(ZEBRA_ERROR)：解码错误类型，若上层注册了
 * zclient->handle_error 回调则调用它。返回 0。
 */
static int zclient_handle_error(ZAPI_CALLBACK_ARGS)
{
	enum zebra_error_types error;
	struct stream *s = zclient->ibuf;

	zapi_error_decode(s, &error);

	if (zclient->handle_error)
		(*zclient->handle_error)(error);
	return 0;
}

/*
 * 从 stream 解析接口链路参数(link-params)写入 ifp：先读一个启用字节，
 * 未启用则释放已有参数并返回；再读 lp_status/te_metric/max_bw/max_rsv_bw、
 * 带宽类(unrsv_bw，按 bwclassnum 读，超 MAX_CLASS_TYPE 报错)、admin_grp、
 * 扩展管理组、远端 AS/IP、时延与丢包/带宽统计。成功返回 0，失败 -1。
 */
static int link_params_set_value(struct stream *s, struct interface *ifp)
{
	uint8_t link_params_enabled, nb_ext_adm_grp;
	struct if_link_params *iflp;
	uint32_t bwclassnum, bitmap_data;

	iflp = if_link_params_get(ifp);

	if (iflp == NULL)
		iflp = if_link_params_init(ifp);

	STREAM_GETC(s, link_params_enabled);
	if (!link_params_enabled) {
		if_link_params_free(ifp);
		return 0;
	}

	STREAM_GETL(s, iflp->lp_status);
	STREAM_GETL(s, iflp->te_metric);
	STREAM_GETF(s, iflp->max_bw);
	STREAM_GETF(s, iflp->max_rsv_bw);
	STREAM_GETL(s, bwclassnum);
	{
		unsigned int i;
		for (i = 0; i < bwclassnum && i < MAX_CLASS_TYPE; i++)
			STREAM_GETF(s, iflp->unrsv_bw[i]);
		if (i < bwclassnum)
			flog_err(
				EC_LIB_ZAPI_MISSMATCH,
				"%s: received %d > %d (MAX_CLASS_TYPE) bw entries - outdated library?",
				__func__, bwclassnum, MAX_CLASS_TYPE);
	}
	STREAM_GETL(s, iflp->admin_grp);

	/* Extended Administrative Group */
	admin_group_clear(&iflp->ext_admin_grp);
	STREAM_GETC(s, nb_ext_adm_grp);
	for (size_t i = 0; i < nb_ext_adm_grp; i++) {
		STREAM_GETL(s, bitmap_data);
		admin_group_bulk_set(&iflp->ext_admin_grp, bitmap_data, i);
	}

	STREAM_GETL(s, iflp->rmt_as);
	iflp->rmt_ip.s_addr = stream_get_ipv4(s);

	STREAM_GETL(s, iflp->av_delay);
	STREAM_GETL(s, iflp->min_delay);
	STREAM_GETL(s, iflp->max_delay);
	STREAM_GETL(s, iflp->delay_var);

	STREAM_GETF(s, iflp->pkt_loss);
	STREAM_GETF(s, iflp->res_bw);
	STREAM_GETF(s, iflp->ava_bw);
	STREAM_GETF(s, iflp->use_bw);

	return 0;
stream_failure:
	return -1;
}

/*
 * 读取接口链路参数更新：先读 ifindex 定位接口(找不到报错)；保存旧参数
 * 副本用于比较；调 link_params_set_value 解析新值；若 changed 非空则比较
 * 新旧参数算出是否变化；最后清理旧副本。返回接口指针，失败 NULL。
 */
struct interface *zebra_interface_link_params_read(struct stream *s,
						   vrf_id_t vrf_id,
						   bool *changed)
{
	struct if_link_params *iflp;
	struct if_link_params iflp_prev = {0};
	ifindex_t ifindex;
	bool iflp_prev_set = false;

	STREAM_GETL(s, ifindex);

	struct interface *ifp = if_lookup_by_index(ifindex, vrf_id);

	if (ifp == NULL) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: unknown ifindex %u, shouldn't happen", __func__,
			 ifindex);
		return NULL;
	}

	iflp = if_link_params_get(ifp);

	if (iflp) {
		iflp_prev_set = true;
		admin_group_init(&iflp_prev.ext_admin_grp);
		if_link_params_copy(&iflp_prev, iflp);
	}

	/* read the link_params from stream
	 * Free ifp->link_params if the stream has no params
	 * to means that link-params are not enabled on links.
	 */
	if (link_params_set_value(s, ifp) != 0)
		goto stream_failure;

	if (changed != NULL) {
		iflp = if_link_params_get(ifp);

		if (iflp_prev_set && iflp) {
			if (if_link_params_cmp(&iflp_prev, iflp))
				*changed = false;
			else
				*changed = true;
		} else if (!iflp_prev_set && !iflp)
			*changed = false;
		else
			*changed = true;
	}

	if (iflp_prev_set)
		admin_group_term(&iflp_prev.ext_admin_grp);

	return ifp;

stream_failure:
	if (iflp_prev_set)
		admin_group_term(&iflp_prev.ext_admin_grp);
	return NULL;
}

/*
 * 从 stream 读接口的通用字段并写入 ifp：ifindex(触发 if_set_index)、status、
 * flags、ptm、metric/speed/txqlen/mtu/mtu6/bandwidth、link_ifindex、
 * zif_type/ll_type、硬件地址(长度受 INTERFACE_HWADDR_MAX 限制)；若链路参数
 * 状态字节非零再解析 link_params；最后按 oldifindex 触发
 * nexthop_group_interface_state_change。解析失败直接 assert 崩溃(视为协议
 * 严重不一致)。
 */
static void zebra_interface_if_set_value(struct stream *s,
					 struct interface *ifp)
{
	uint8_t link_params_status = 0;
	ifindex_t old_ifindex, new_ifindex;

	old_ifindex = ifp->oldifindex;
	/* Read interface's index. */
	STREAM_GETL(s, new_ifindex);
	if_set_index(ifp, new_ifindex);
	STREAM_GETC(s, ifp->status);

	/* Read interface's value. */
	STREAM_GETQ(s, ifp->flags);
	STREAM_GETC(s, ifp->ptm_enable);
	STREAM_GETC(s, ifp->ptm_status);
	STREAM_GETL(s, ifp->metric);
	STREAM_GETL(s, ifp->speed);
	STREAM_GETL(s, ifp->txqlen);
	STREAM_GETL(s, ifp->mtu);
	STREAM_GETL(s, ifp->mtu6);
	STREAM_GETL(s, ifp->bandwidth);
	STREAM_GETL(s, ifp->link_ifindex);
	STREAM_GETL(s, ifp->zif_type);
	STREAM_GETL(s, ifp->ll_type);
	STREAM_GETL(s, ifp->hw_addr_len);
	if (ifp->hw_addr_len)
		STREAM_GET(ifp->hw_addr, s,
			   MIN(ifp->hw_addr_len, INTERFACE_HWADDR_MAX));

	/* Read Traffic Engineering status */
	link_params_status = stream_getc(s);
	/* Then, Traffic Engineering parameters if any */
	if (link_params_status)
		link_params_set_value(s, ifp);

	nexthop_group_interface_state_change(ifp, old_ifindex);

	return;
stream_failure:
	zlog_err("Could not parse interface values; aborting");
	assert(!"Failed to parse interface values");
}

/*
 * 把接口 ifp 的链路参数编码进 stream(link_params_set_value 的逆)：s/ifp
 * 为空返回 0；先写是否启用(未启用只写一个 false 字节即返回)；否则依次写
 * lp_status/te_metric/max_bw/max_rsv_bw、unrsv_bw、admin_grp、扩展管理组、
 * rmt_as/rmt_ip、时延与统计。返回写入字节数。
 */
size_t zebra_interface_link_params_write(struct stream *s,
					 struct interface *ifp)
{
	size_t w, nb_ext_adm_grp;
	struct if_link_params *iflp;
	int i;
	size_t j;

	if (s == NULL || ifp == NULL)
		return 0;

	iflp = ifp->link_params;
	w = 0;

	/* encode if link_params is enabled */
	if (iflp) {
		w += stream_putc(s, true);
	} else {
		w += stream_putc(s, false);
		return w;
	}

	w += stream_putl(s, iflp->lp_status);

	w += stream_putl(s, iflp->te_metric);
	w += stream_putf(s, iflp->max_bw);
	w += stream_putf(s, iflp->max_rsv_bw);

	w += stream_putl(s, MAX_CLASS_TYPE);
	for (i = 0; i < MAX_CLASS_TYPE; i++)
		w += stream_putf(s, iflp->unrsv_bw[i]);

	w += stream_putl(s, iflp->admin_grp);

	/* Extended Administrative Group */
	nb_ext_adm_grp = admin_group_nb_words(&iflp->ext_admin_grp);
	w += stream_putc(s, nb_ext_adm_grp);
	for (j = 0; j < nb_ext_adm_grp; j++)
		stream_putl(s, admin_group_get_offset(&iflp->ext_admin_grp, j));

	w += stream_putl(s, iflp->rmt_as);
	w += stream_put_in_addr(s, &iflp->rmt_ip);

	w += stream_putl(s, iflp->av_delay);
	w += stream_putl(s, iflp->min_delay);
	w += stream_putl(s, iflp->max_delay);
	w += stream_putl(s, iflp->delay_var);

	w += stream_putf(s, iflp->pkt_loss);
	w += stream_putf(s, iflp->res_bw);
	w += stream_putf(s, iflp->ava_bw);
	w += stream_putf(s, iflp->use_bw);

	return w;
}

/*
 * format of message for address addition is:
 *    0
 *  0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+
 * |   type        |  ZEBRA_INTERFACE_ADDRESS_ADD or
 * +-+-+-+-+-+-+-+-+  ZEBRA_INTERFACE_ADDRES_DELETE
 * |               |
 * +               +
 * |   ifindex     |
 * +               +
 * |               |
 * +               +
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |   ifc_flags   |  flags for connected address
 * +-+-+-+-+-+-+-+-+
 * |  addr_family  |
 * +-+-+-+-+-+-+-+-+
 * |    addr...    |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |    addr_len   |  len of addr. E.g., addr_len = 4 for ipv4 addrs.
 * +-+-+-+-+-+-+-+-+
 * |     daddr..   |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 */

/*
 * 判断一段 n 字节内存是否全部等于字节 c。全部相同返回 1，否则 0。
 * 用于识别“全零”表示的空目的地址。
 */
static int memconstant(const void *s, int c, size_t n)
{
	const uint8_t *p = s;

	while (n-- > 0)
		if (*p++ != c)
			return 0;
	return 1;
}


/*
 * 读取接口地址增删报文：读 ifindex 定位接口(找不到报错)；读 ifc_flags、
 * 地址族与本地地址；读目的地址(全零表示无 peer，转为 NULL)；ADD 时按精确
 * 前缀查找 connected，不存在则新建并设置 flags(含 PEER 标志但无 peer 地址
 * 的告警处理)；DELETE 时按前缀删除 connected。返回 connected 指针，失败
 * NULL。
 */
struct connected *zebra_interface_address_read(int type, struct stream *s,
					       vrf_id_t vrf_id)
{
	ifindex_t ifindex;
	struct interface *ifp;
	struct connected *ifc;
	struct prefix p, d, *dp;
	int plen;
	uint8_t ifc_flags;

	memset(&p, 0, sizeof(p));
	memset(&d, 0, sizeof(d));

	/* Get interface index. */
	STREAM_GETL(s, ifindex);

	/* Lookup index. */
	ifp = if_lookup_by_index(ifindex, vrf_id);
	if (ifp == NULL) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "INTERFACE_ADDRESS_%s: Cannot find IF %u in VRF %d",
			 (type == ZEBRA_INTERFACE_ADDRESS_ADD) ? "ADD" : "DEL",
			 ifindex, vrf_id);
		return NULL;
	}

	/* Fetch flag. */
	STREAM_GETC(s, ifc_flags);

	/* Fetch interface address. */
	STREAM_GETC(s, d.family);
	p.family = d.family;
	plen = prefix_blen(&d);

	if (zclient_stream_get_prefix(s, &p) != 0)
		goto stream_failure;

	/* Fetch destination address. */
	STREAM_GET(&d.u.prefix, s, plen);

	/* N.B. NULL destination pointers are encoded as all zeroes */
	dp = memconstant(&d.u.prefix, 0, plen) ? NULL : &d;

	if (type == ZEBRA_INTERFACE_ADDRESS_ADD) {
		ifc = connected_lookup_prefix_exact(ifp, &p);
		if (!ifc) {
			/* N.B. NULL destination pointers are encoded as all
			 * zeroes */
			ifc = connected_add_by_prefix(ifp, &p, dp);
		}
		if (ifc) {
			ifc->flags = ifc_flags;
			if (ifc->destination)
				ifc->destination->prefixlen =
					ifc->address->prefixlen;
			else if (CHECK_FLAG(ifc->flags, ZEBRA_IFA_PEER)) {
				/* carp interfaces on OpenBSD with 0.0.0.0/0 as
				 * "peer" */
				flog_err(
					EC_LIB_ZAPI_ENCODE,
					"interface %s address %pFX with peer flag set, but no peer address!",
					ifp->name, ifc->address);
				UNSET_FLAG(ifc->flags, ZEBRA_IFA_PEER);
			}
		}
	} else {
		assert(type == ZEBRA_INTERFACE_ADDRESS_DELETE);
		ifc = connected_delete_by_prefix(ifp, &p);
	}

	return ifc;

stream_failure:
	return NULL;
}

/*
 * format of message for neighbor connected address is:
 *    0
 *  0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+
 * |   type        |  ZEBRA_INTERFACE_NBR_ADDRESS_ADD or
 * +-+-+-+-+-+-+-+-+  ZEBRA_INTERFACE_NBR_ADDRES_DELETE
 * |               |
 * +               +
 * |   ifindex     |
 * +               +
 * |               |
 * +               +
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |  addr_family  |
 * +-+-+-+-+-+-+-+-+
 * |    addr...    |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |    addr_len   |  len of addr.
 * +-+-+-+-+-+-+-+-+
 */
/*
 * 读取接口邻居(connected)地址增删报文：读 ifindex 定位接口(找不到报错)，
 * 再读地址族与前缀，按类型新增或删除 nbr_connected。返回指针，失败 NULL。
 */
struct nbr_connected *
zebra_interface_nbr_address_read(int type, struct stream *s, vrf_id_t vrf_id)
{
	unsigned int ifindex;
	struct interface *ifp;
	struct prefix p;
	struct nbr_connected *ifc;

	/* Get interface index. */
	STREAM_GETL(s, ifindex);

	/* Lookup index. */
	ifp = if_lookup_by_index(ifindex, vrf_id);
	if (ifp == NULL) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "INTERFACE_NBR_%s: Cannot find IF %u in VRF %d",
			 (type == ZEBRA_INTERFACE_NBR_ADDRESS_ADD) ? "ADD"
								   : "DELETE",
			 ifindex, vrf_id);
		return NULL;
	}

	STREAM_GETC(s, p.family);
	STREAM_GET(&p.u.prefix, s, prefix_blen(&p));
	STREAM_GETC(s, p.prefixlen);

	if (type == ZEBRA_INTERFACE_NBR_ADDRESS_ADD) {
		/* Currently only supporting P2P links, so any new RA source
		   address is
		   considered as the replacement of the previously learnt
		   Link-Local address. */
		if (!(ifc = listnode_head(ifp->nbr_connected))) {
			ifc = nbr_connected_new();
			ifc->address = prefix_new();
			ifc->ifp = ifp;
			listnode_add(ifp->nbr_connected, ifc);
		}

		prefix_copy(ifc->address, &p);
	} else {
		assert(type == ZEBRA_INTERFACE_NBR_ADDRESS_DELETE);

		ifc = nbr_connected_check(ifp, &p);
		if (ifc)
			listnode_delete(ifp->nbr_connected, ifc);
	}

	return ifc;

stream_failure:
	return NULL;
}

/* filter unwanted messages until the expected one arrives */
/*
 * 同步读取一个响应，丢弃直到收到 expected_cmd 为止(用于 label/table
 * manager 这类“请求-应答”式同步交互)。循环用 poll 等待可读(避免
 * FD_SETSIZE 限制)，把 POLLHUP/POLLERR 也当作可读以便把错误暴露给 read；
 * 用 zclient_read_header 解析。返回 0 成功，-1 失败(无效响应/套接字错误)。
 */
static int zclient_read_sync_response(struct zclient *zclient,
				      uint16_t expected_cmd)
{
	struct stream *s;
	uint16_t size = -1;
	uint8_t marker;
	uint8_t version;
	vrf_id_t vrf_id;
	uint16_t cmd;
	struct pollfd pfd;
	int ret;

	ret = 0;
	cmd = expected_cmd + 1;
	while (ret == 0 && cmd != expected_cmd) {
		s = zclient->ibuf;
		stream_reset(s);

		/* wait until response arrives (poll avoids FD_SETSIZE limit) */
		pfd.fd = zclient->sock;
		pfd.events = POLLIN | POLLHUP | POLLERR;
		if (poll(&pfd, 1, -1) < 0) {
			if (errno == EINTR)
				continue;
			flog_err(EC_LIB_ZAPI_SOCKET, "%s: poll failed: %s",
				 __func__, safe_strerror(errno));
			return -1;
		}
		if (pfd.revents & POLLNVAL) {
			flog_err(EC_LIB_ZAPI_SOCKET, "%s: invalid zclient socket",
				 __func__);
			return -1;
		}
		/*
		 * Treat POLLHUP/POLLERR as readable so read can surface the error
		 * (matches lib/event.c pattern, avoids busy-spin on socket error)
		 */
		if (!(pfd.revents & (POLLIN | POLLHUP | POLLERR)))
			continue;
		/* read response */
		ret = zclient_read_header(s, zclient->sock, &size, &marker,
					  &version, &vrf_id, &cmd);
		if (zclient_debug)
			zlog_debug("%s: Response (%d bytes) received", __func__,
				   size);
	}
	if (ret != 0) {
		flog_err(EC_LIB_ZAPI_ENCODE, "%s: Invalid Sync Message Reply",
			 __func__);
		return -1;
	}

	return 0;
}
/**
 * Connect to label manager in a synchronous way
 *
 * It first writes the request to zclient output buffer and then
 * immediately reads the answer from the input buffer.
 *
 * @param zclient Zclient used to connect to label manager (zebra)
 * @param async Synchronous (0) or asynchronous (1) operation
 * @result Result of response
 */
/*
 * 以同步或异步方式连接 Label Manager：async 为真时命令用
 * ZEBRA_LABEL_MANAGER_CONNECT_ASYNC，否则用 _CONNECT。先发请求(proto=
 * redist_default，instance)，用 writen 直接写 socket(错误则关 socket)；
 * 异步直接返回 0；同步则 zclient_read_sync_response 读回并校验
 * proto/instance 与期望一致，最后返回结果字节(或 -1)。
 */
int lm_label_manager_connect(struct zclient *zclient, int async)
{
	int ret;
	struct stream *s;
	uint8_t result;
	uint16_t cmd = async ? ZEBRA_LABEL_MANAGER_CONNECT_ASYNC :
			       ZEBRA_LABEL_MANAGER_CONNECT;

	if (zclient_debug)
		zlog_debug("Connecting to Label Manager (LM)");

	if (zclient->sock < 0) {
		zlog_debug("%s: invalid zclient socket", __func__);
		return -1;
	}

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, cmd, VRF_DEFAULT);

	/* proto */
	stream_putc(s, zclient->redist_default);
	/* instance */
	stream_putw(s, zclient->instance);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	ret = writen(zclient->sock, s->data, stream_get_endp(s));
	if (ret < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "Can't write to zclient sock");
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (ret == 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "Zclient sock closed");
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (zclient_debug)
		zlog_debug("LM connect request sent (%d bytes)", ret);

	if (async)
		return 0;

	/* read response */
	if (zclient_read_sync_response(zclient, cmd)
	    != 0)
		return -1;

	s = zclient->ibuf;

	/* read instance and proto */
	uint8_t proto;
	uint16_t instance;

	STREAM_GETC(s, proto);
	STREAM_GETW(s, instance);

	/* sanity */
	if (proto != zclient->redist_default)
		flog_err(
			EC_LIB_ZAPI_ENCODE,
			"Wrong proto (%u) in LM connect response. Should be %u",
			proto, zclient->redist_default);
	if (instance != zclient->instance)
		flog_err(
			EC_LIB_ZAPI_ENCODE,
			"Wrong instId (%u) in LM connect response. Should be %u",
			instance, zclient->instance);

	/* result code */
	STREAM_GETC(s, result);
	if (zclient_debug)
		zlog_debug("LM connect-response received, result %u", result);

	return (int)result;

stream_failure:
	return -1;
}

/**
 * Function to request a srv6-locator chunk in an asynchronous way
 *
 * @param zclient Zclient used to connect to table manager (zebra)
 * @param locator_name Name of SRv6-locator
 * @result 0 on success, -1 otherwise
 */
/*
 * 异步向 SRv6 Manager 申请一个 locator chunk：发
 * ZEBRA_SRV6_MANAGER_GET_LOCATOR_CHUNK，payload 为 locator 名(2B 长度+名)。
 * 未连接返回 -1，否则返回发送状态。结果经 ZEBRA_SRV6_SID_NOTIFY 异步回报。
 */
int srv6_manager_get_locator_chunk(struct zclient *zclient,
				   const char *locator_name)
{
	struct stream *s;
	const size_t len = strlen(locator_name);

	if (zclient_debug)
		zlog_debug("Getting SRv6-Locator Chunk %s", locator_name);

	if (zclient->sock < 0)
		return -1;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_SRV6_MANAGER_GET_LOCATOR_CHUNK,
			      VRF_DEFAULT);

	/* locator_name */
	stream_putw(s, len);
	stream_put(s, locator_name, len);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/**
 * Function to release a srv6-locator chunk
 *
 * @param zclient Zclient used to connect to table manager (zebra)
 * @param locator_name Name of SRv6-locator
 * @result 0 on success, -1 otherwise
 */
/*
 * 释放一个 locator chunk：发 ZEBRA_SRV6_MANAGER_RELEASE_LOCATOR_CHUNK，
 * payload 为 locator 名。未连接返回 -1，否则返回发送状态。
 */
int srv6_manager_release_locator_chunk(struct zclient *zclient,
				       const char *locator_name)
{
	struct stream *s;
	const size_t len = strlen(locator_name);

	if (zclient_debug)
		zlog_debug("Releasing SRv6-Locator Chunk %s", locator_name);

	if (zclient->sock < 0)
		return -1;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_SRV6_MANAGER_RELEASE_LOCATOR_CHUNK,
			      VRF_DEFAULT);

	/* locator_name */
	stream_putw(s, len);
	stream_put(s, locator_name, len);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/**
 * Function to request a SRv6 locator in an asynchronous way
 *
 * @param zclient The zclient used to connect to SRv6 Manager (zebra)
 * @param locator_name Name of SRv6 locator
 * @return 0 on success, -1 otherwise
 */
/*
 * 异步请求一个 SRv6 locator：locator_name 为空或未连接返回 -1；否则发
 * ZEBRA_SRV6_MANAGER_GET_LOCATOR(payload 为 locator 名)，返回发送状态。
 */
int srv6_manager_get_locator(struct zclient *zclient, const char *locator_name)
{
	struct stream *s;
	size_t len;

	if (!locator_name)
		return -1;

	if (zclient->sock < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "%s: invalid zclient socket",
			 __func__);
		return -1;
	}

	if (zclient_debug)
		zlog_debug("Getting SRv6 Locator %s", locator_name);

	len = strlen(locator_name);

	/* Send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_SRV6_MANAGER_GET_LOCATOR, VRF_DEFAULT);

	/* Locator name */
	stream_putw(s, len);
	stream_put(s, locator_name, len);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/**
 * Function to request an SRv6 SID in an asynchronous way
 *
 * @param zclient The zclient used to connect to SRv6 manager (zebra)
 * @param ctx Context associated with the SRv6 SID
 * @param sid_value SRv6 SID value for explicit SID allocation
 * @param locator_name Name of the parent locator for dynamic SID allocation
 * @param sid_func SID function assigned by the SRv6 Manager
 * @param is_localonly SID is local-only
 * @result 0 on success, -1 otherwise
 */
/*
 * 异步申请一个 SRv6 SID：发 ZEBRA_SRV6_MANAGER_GET_SRV6_SID。payload：
 * sid_ctx；flags(依 sid_value/locator_name/is_localonly 组合 HAS_SID_VALUE/
 * HAS_LOCATOR/IS_LOCALONLY)；按 flags 选择性携带 SID 值与 locator 名。
 * 未连接返回失败。返回发送状态。
 */
int srv6_manager_get_sid(struct zclient *zclient, const struct srv6_sid_ctx *ctx,
			 struct in6_addr *sid_value, const char *locator_name, uint32_t *sid_func,
			 bool is_localonly)
{
	struct stream *s;
	uint8_t flags = 0;
	size_t len;
	char buf[256];

	if (zclient->sock < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "%s: invalid zclient socket",
			 __func__);
		return ZCLIENT_SEND_FAILURE;
	}

	if (zclient_debug)
		zlog_debug("Getting SRv6 SID: %s",
			   srv6_sid_ctx2str(buf, sizeof(buf), ctx));

	/* send request */
	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_SRV6_MANAGER_GET_SRV6_SID, VRF_DEFAULT);

	/* Context associated with the SRv6 SID */
	stream_put(s, ctx, sizeof(struct srv6_sid_ctx));

	/* Flags */
	if (!sid_zero_ipv6(sid_value))
		SET_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_HAS_SID_VALUE);
	if (locator_name)
		SET_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_HAS_LOCATOR);
	if (is_localonly)
		SET_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_IS_LOCALONLY);
	stream_putc(s, flags);

	/* SRv6 SID value */
	if (CHECK_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_HAS_SID_VALUE))
		stream_put(s, sid_value, sizeof(struct in6_addr));

	/* SRv6 locator */
	if (CHECK_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_HAS_LOCATOR)) {
		len = strlen(locator_name);
		stream_putw(s, len);
		stream_put(s, locator_name, len);
	}

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	/* Send the request to SRv6 Manager */
	return zclient_send_message(zclient);
}

/**
 * Function to release an SRv6 SID
 *
 * @param zclient Zclient used to connect to SRv6 manager (zebra)
 * @param ctx Context associated with the SRv6 SID to be removed
 * @param locator_name Parent locator of the SID
 * @param is_localonly SID is local-only
 * @result 0 on success, -1 otherwise
 */
/*
 * 释放一个 SRv6 SID：发 ZEBRA_SRV6_MANAGER_RELEASE_SRV6_SID。payload 为
 * sid_ctx 与 flags(HAS_LOCATOR/IS_LOCALONLY)，按需携带 locator 名。未连接
 * 返回 -1，否则返回发送状态。
 */
int srv6_manager_release_sid(struct zclient *zclient, const struct srv6_sid_ctx *ctx,
			     const char *locator_name, bool is_localonly)
{
	struct stream *s;
	uint8_t flags = 0;
	size_t len;
	char buf[256];

	if (zclient->sock < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "%s: invalid zclient socket",
			 __func__);
		return -1;
	}

	if (zclient_debug)
		zlog_debug("Releasing SRv6 SID: %s",
			   srv6_sid_ctx2str(buf, sizeof(buf), ctx));

	/* send request */
	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_SRV6_MANAGER_RELEASE_SRV6_SID,
			      VRF_DEFAULT);

	/* Context associated with the SRv6 SID */
	stream_put(s, ctx, sizeof(struct srv6_sid_ctx));

	/* Flags */
	if (locator_name)
		SET_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_HAS_LOCATOR);
	if (is_localonly)
		SET_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_IS_LOCALONLY);
	stream_putc(s, flags);

	/* SRv6 locator */
	if (CHECK_FLAG(flags, ZAPI_SRV6_MANAGER_SID_FLAG_HAS_LOCATOR)) {
		len = strlen(locator_name);
		stream_putw(s, len);
		stream_put(s, locator_name, len);
	}

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	/* Send the SID release message */
	return zclient_send_message(zclient);
}

/*
 * Asynchronous label chunk request
 *
 * @param zclient The zclient used to connect to label manager (zebra)
 * @param keep Avoid garbage collection
 * @param chunk_size Amount of labels requested
 * @param base Base for the label chunk. if MPLS_LABEL_BASE_ANY we do not care
 * @result 0 on success, -1 otherwise
 */
/*
 * 异步申请标签 chunk：发 ZEBRA_GET_LABEL_CHUNK，payload 为 proto/instance/
 * keep/chunk_size/base(base 为 MPLS_LABEL_BASE_ANY 表示不关心起点)。未连接
 * 返回失败。返回发送状态。结果经 ZEBRA_GET_LABEL_CHUNK 异步回报。
 */
enum zclient_send_status zclient_send_get_label_chunk(struct zclient *zclient,
						      uint8_t keep,
						      uint32_t chunk_size,
						      uint32_t base)
{
	struct stream *s;

	if (zclient_debug)
		zlog_debug("Getting Label Chunk");

	if (zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_GET_LABEL_CHUNK, VRF_DEFAULT);
	/* proto */
	stream_putc(s, zclient->redist_default);
	/* instance */
	stream_putw(s, zclient->instance);
	stream_putc(s, keep);
	stream_putl(s, chunk_size);
	stream_putl(s, base);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/**
 * Function to request a label chunk in a synchronous way
 *
 * It first writes the request to zclient output buffer and then
 * immediately reads the answer from the input buffer.
 *
 * @param zclient Zclient used to connect to label manager (zebra)
 * @param keep Avoid garbage collection
 * @param chunk_size Amount of labels requested
 * @param start To write first assigned chunk label to
 * @param end To write last assigned chunk label to
 * @result 0 on success, -1 otherwise
 */
/*
 * 以同步方式申请标签 chunk：发请求后 zclient_read_sync_response 读回，
 * 校验 proto/instance；若响应提前结束(指定 chunk 无法分配)返回 -1；否则
 * 读 keep 与 start/end，校验 keep 一致且 start/end 落在未保留标签范围内，
 * 回填 *start/*end。返回 0 成功，-1 失败。
 */
int lm_get_label_chunk(struct zclient *zclient, uint8_t keep, uint32_t base,
		       uint32_t chunk_size, uint32_t *start, uint32_t *end)
{
	int ret;
	struct stream *s;
	uint8_t response_keep;

	if (zclient_debug)
		zlog_debug("Getting Label Chunk");

	if (!zclient || zclient->sock < 0)
		return -1;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_GET_LABEL_CHUNK, VRF_DEFAULT);
	/* proto */
	stream_putc(s, zclient->redist_default);
	/* instance */
	stream_putw(s, zclient->instance);
	/* keep */
	stream_putc(s, keep);
	/* chunk size */
	stream_putl(s, chunk_size);
	/* requested chunk base */
	stream_putl(s, base);
	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	ret = writen(zclient->sock, s->data, stream_get_endp(s));
	if (ret < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "Can't write to zclient sock");
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (ret == 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "Zclient sock closed");
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (zclient_debug)
		zlog_debug("Label chunk request (%d bytes) sent", ret);

	/* read response */
	if (zclient_read_sync_response(zclient, ZEBRA_GET_LABEL_CHUNK) != 0)
		return -1;

	/* parse response */
	s = zclient->ibuf;

	/* read proto and instance */
	uint8_t proto;
	uint8_t instance;

	STREAM_GETC(s, proto);
	STREAM_GETW(s, instance);

	/* sanities */
	if (proto != zclient->redist_default)
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "Wrong proto (%u) in get chunk response. Should be %u",
			 proto, zclient->redist_default);
	if (instance != zclient->instance)
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "Wrong instId (%u) in get chunk response Should be %u",
			 instance, zclient->instance);

	/* if we requested a specific chunk and it could not be allocated, the
	 * response message will end here
	 */
	if (!STREAM_READABLE(s)) {
		zlog_info("Unable to assign Label Chunk to %s instance %u",
			  zebra_route_string(proto), instance);
		return -1;
	}

	/* keep */
	STREAM_GETC(s, response_keep);
	/* start and end labels */
	STREAM_GETL(s, *start);
	STREAM_GETL(s, *end);

	/* not owning this response */
	if (keep != response_keep) {
		flog_err(
			EC_LIB_ZAPI_ENCODE,
			"Invalid Label chunk: %u - %u, keeps mismatch %u != %u",
			*start, *end, keep, response_keep);
	}
	/* sanity */
	if (*start > *end || *start < MPLS_LABEL_UNRESERVED_MIN
	    || *end > MPLS_LABEL_UNRESERVED_MAX) {
		flog_err(EC_LIB_ZAPI_ENCODE, "Invalid Label chunk: %u - %u",
			 *start, *end);
		return -1;
	}

	if (zclient_debug)
		zlog_debug("Label Chunk assign: %u - %u (%u)", *start, *end,
			   response_keep);

	return 0;

stream_failure:
	return -1;
}

/**
 * Function to release a label chunk
 *
 * @param zclient Zclient used to connect to label manager (zebra)
 * @param start First label of chunk
 * @param end Last label of chunk
 * @result 0 on success, -1 otherwise
 */
/*
 * 释放标签 chunk [start,end]：发 ZEBRA_RELEASE_LABEL_CHUNK(proto/instance/
 * start/end)，用 writen 直接写 socket(错误关 socket 返回 -1)。成功返回 0。
 */
int lm_release_label_chunk(struct zclient *zclient, uint32_t start,
			   uint32_t end)
{
	int ret;
	struct stream *s;

	if (zclient_debug)
		zlog_debug("Releasing Label Chunk %u - %u", start, end);

	if (zclient->sock < 0)
		return -1;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_RELEASE_LABEL_CHUNK, VRF_DEFAULT);

	/* proto */
	stream_putc(s, zclient->redist_default);
	/* instance */
	stream_putw(s, zclient->instance);
	/* start */
	stream_putl(s, start);
	/* end */
	stream_putl(s, end);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	ret = writen(zclient->sock, s->data, stream_get_endp(s));
	if (ret < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "Can't write to zclient sock");
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (ret == 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "Zclient sock connection closed");
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}

	return 0;
}

/**
 * Connect to table manager in a synchronous way
 *
 * It first writes the request to zclient output buffer and then
 * immediately reads the answer from the input buffer.
 *
 * @param zclient Zclient used to connect to table manager (zebra)
 * @result Result of response
 */
/*
 * 以同步方式连接 Table Manager：发 ZEBRA_TABLE_MANAGER_CONNECT(proto/
 * instance)，同步读回结果字节并返回其值(或 -1)。未连接返回失败。
 */
int tm_table_manager_connect(struct zclient *zclient)
{
	int ret;
	struct stream *s;
	uint8_t result;

	if (zclient_debug)
		zlog_debug("Connecting to Table Manager");

	if (zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_TABLE_MANAGER_CONNECT, VRF_DEFAULT);

	/* proto */
	stream_putc(s, zclient->redist_default);
	/* instance */
	stream_putw(s, zclient->instance);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	ret = zclient_send_message(zclient);
	if (ret == ZCLIENT_SEND_FAILURE)
		return -1;

	if (zclient_debug)
		zlog_debug("%s: Table manager connect request sent", __func__);

	/* read response */
	if (zclient_read_sync_response(zclient, ZEBRA_TABLE_MANAGER_CONNECT)
	    != 0)
		return -1;

	/* result */
	s = zclient->ibuf;
	STREAM_GETC(s, result);
	if (zclient_debug)
		zlog_debug(
			"%s: Table Manager connect response received, result %u",
			__func__, result);

	return (int)result;
stream_failure:
	return -1;
}

/**
 * Function to request a table chunk in a synchronous way
 *
 * It first writes the request to zclient output buffer and then
 * immediately reads the answer from the input buffer.
 *
 * @param zclient Zclient used to connect to table manager (zebra)
 * @param chunk_size Amount of table requested
 * @param start to write first assigned chunk table RT ID to
 * @param end To write last assigned chunk table RT ID to
 * @result 0 on success, -1 otherwise
 */
/*
 * 以同步方式申请表号 chunk：发 ZEBRA_GET_TABLE_CHUNK(chunk_size)，同步读回
 * start/end 表号并回填。返回 0 成功，-1 失败。
 */
int tm_get_table_chunk(struct zclient *zclient, uint32_t chunk_size,
		       uint32_t *start, uint32_t *end)
{
	int ret;
	struct stream *s;

	if (zclient_debug)
		zlog_debug("Getting Table Chunk");

	if (zclient->sock < 0)
		return -1;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_GET_TABLE_CHUNK, VRF_DEFAULT);
	/* chunk size */
	stream_putl(s, chunk_size);
	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	ret = writen(zclient->sock, s->data, stream_get_endp(s));
	if (ret < 0) {
		flog_err(EC_LIB_ZAPI_SOCKET, "%s: can't write to zclient->sock",
			 __func__);
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (ret == 0) {
		flog_err(EC_LIB_ZAPI_SOCKET,
			 "%s: zclient->sock connection closed", __func__);
		close(zclient->sock);
		zclient->sock = -1;
		return -1;
	}
	if (zclient_debug)
		zlog_debug("%s: Table chunk request (%d bytes) sent", __func__,
			   ret);

	/* read response */
	if (zclient_read_sync_response(zclient, ZEBRA_GET_TABLE_CHUNK) != 0)
		return -1;

	s = zclient->ibuf;
	/* start and end table IDs */
	STREAM_GETL(s, *start);
	STREAM_GETL(s, *end);

	if (zclient_debug)
		zlog_debug("Table Chunk assign: %u - %u ", *start, *end);

	return 0;
stream_failure:
	return -1;
}

/**
 * Function to release a table chunk
 *
 * @param zclient Zclient used to connect to table manager (zebra)
 * @param start First label of table
 * @param end Last label of chunk
 * @result 0 on success, -1 otherwise
 */
/*
 * 释放表号 chunk [start,end]：发 ZEBRA_RELEASE_TABLE_CHUNK(start/end)。
 * 未连接返回 -1，成功返回 0。不等待响应(由发送路径保证)。
 */
int tm_release_table_chunk(struct zclient *zclient, uint32_t start,
			   uint32_t end)
{
	struct stream *s;

	if (zclient_debug)
		zlog_debug("Releasing Table Chunk");

	if (zclient->sock < 0)
		return -1;

	/* send request */
	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_RELEASE_TABLE_CHUNK, VRF_DEFAULT);

	/* start */
	stream_putl(s, start);
	/* end */
	stream_putl(s, end);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	if (zclient_send_message(zclient) == ZCLIENT_SEND_FAILURE)
		return -1;

	return 0;
}

/*
 * 下发/删除 SR Policy：先 zapi_sr_policy_encode 编码(失败返回 FAILURE)，
 * 再发送。cmd 区分 SET/DELETE。返回发送状态。
 */
enum zclient_send_status zebra_send_sr_policy(struct zclient *zclient, int cmd,
					      struct zapi_sr_policy *zp)
{
	if (zapi_sr_policy_encode(zclient->obuf, cmd, zp) < 0)
		return ZCLIENT_SEND_FAILURE;
	return zclient_send_message(zclient);
}

/*
 * 编码 SR Policy：header(cmd) + color + 端点 + 名字(定长) + 段列表
 * (type/local_label/label_num/labels，label_num 超 MPLS_MAX_LABELS 报错)，
 * 回填长度。返回 0 成功，-1 失败。
 */
int zapi_sr_policy_encode(struct stream *s, int cmd, struct zapi_sr_policy *zp)
{
	struct zapi_srte_tunnel *zt = &zp->segment_list;

	stream_reset(s);

	zclient_create_header(s, cmd, VRF_DEFAULT);
	stream_putl(s, zp->color);
	stream_put_ipaddr(s, &zp->endpoint);
	stream_write(s, &zp->name, SRTE_POLICY_NAME_MAX_LENGTH);

	stream_putc(s, zt->type);
	stream_putl(s, zt->local_label);

	if (zt->label_num > MPLS_MAX_LABELS) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: label %u: can't encode %u labels (maximum is %u)",
			 __func__, zt->local_label, zt->label_num,
			 MPLS_MAX_LABELS);
		return -1;
	}
	stream_putw(s, zt->label_num);

	for (int i = 0; i < zt->label_num; i++)
		stream_putl(s, zt->labels[i]);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 解码 SR Policy(与 encode 对称)：读 color/端点/名字与段列表(type/
 * local_label/label_num/labels，校验 label_num 上限)。返回 0 成功，-1 失败。
 */
int zapi_sr_policy_decode(struct stream *s, struct zapi_sr_policy *zp)
{
	memset(zp, 0, sizeof(*zp));

	struct zapi_srte_tunnel *zt = &zp->segment_list;

	STREAM_GETL(s, zp->color);
	STREAM_GET_IPADDR(s, &zp->endpoint);
	STREAM_GET(&zp->name, s, SRTE_POLICY_NAME_MAX_LENGTH);

	/* segment list of active candidate path */
	STREAM_GETC(s, zt->type);
	STREAM_GETL(s, zt->local_label);
	STREAM_GETW(s, zt->label_num);
	if (zt->label_num > MPLS_MAX_LABELS) {
		flog_err(EC_LIB_ZAPI_ENCODE,
			 "%s: label %u: can't decode %u labels (maximum is %u)",
			 __func__, zt->local_label, zt->label_num,
			 MPLS_MAX_LABELS);
		return -1;
	}
	for (int i = 0; i < zt->label_num; i++)
		STREAM_GETL(s, zt->labels[i]);

	return 0;

stream_failure:
	return -1;
}

/*
 * 解码 SR Policy 状态通知：读 color/端点/名字/status 并回填。返回 0 成功，
 * -1 失败。
 */
int zapi_sr_policy_notify_status_decode(struct stream *s,
					struct zapi_sr_policy *zp)
{
	memset(zp, 0, sizeof(*zp));

	STREAM_GETL(s, zp->color);
	STREAM_GET_IPADDR(s, &zp->endpoint);
	STREAM_GET(&zp->name, s, SRTE_POLICY_NAME_MAX_LENGTH);
	STREAM_GETL(s, zp->status);

	return 0;

stream_failure:
	return -1;
}

/*
 * 下发/删除标签条目：先 zapi_labels_encode 编码(失败返回 FAILURE)，再发送。
 * 返回发送状态。
 */
enum zclient_send_status zebra_send_mpls_labels(struct zclient *zclient,
						int cmd, struct zapi_labels *zl)
{
	if (zapi_labels_encode(zclient->obuf, cmd, zl) < 0)
		return ZCLIENT_SEND_FAILURE;
	return zclient_send_message(zclient);
}

/*
 * 编码标签条目(zapi_labels)：header(cmd) + message + type + local_label；
 * 若 message 含 FTN 则写关联路由(前缀/类型/实例)；校验并写主 nexthop 数
 * 与各 nexthop；若含 HAS_BACKUPS 再写备份 nexthop。回填长度。返回 0 成功，
 * -1 表示 nexthop 数超限或 nexthop 编码失败。
 */
int zapi_labels_encode(struct stream *s, int cmd, struct zapi_labels *zl)
{
	struct zapi_nexthop *znh;

	stream_reset(s);

	zclient_create_header(s, cmd, VRF_DEFAULT);
	stream_putc(s, zl->message);
	stream_putc(s, zl->type);
	stream_putl(s, zl->local_label);

	if (CHECK_FLAG(zl->message, ZAPI_LABELS_FTN)) {
		stream_putw(s, zl->route.prefix.family);
		stream_put_prefix(s, &zl->route.prefix);
		stream_putc(s, zl->route.type);
		stream_putw(s, zl->route.instance);
	}

	if (zl->nexthop_num > MULTIPATH_NUM) {
		flog_err(
			EC_LIB_ZAPI_ENCODE,
			"%s: label %u: can't encode %u nexthops (maximum is %u)",
			__func__, zl->local_label, zl->nexthop_num,
			MULTIPATH_NUM);
		return -1;
	}
	stream_putw(s, zl->nexthop_num);

	for (int i = 0; i < zl->nexthop_num; i++) {
		znh = &zl->nexthops[i];

		if (zapi_nexthop_encode(s, znh, 0, 0) < 0)
			return -1;
	}

	if (CHECK_FLAG(zl->message, ZAPI_LABELS_HAS_BACKUPS)) {

		if (zl->backup_nexthop_num > MULTIPATH_NUM) {
			flog_err(
				EC_LIB_ZAPI_ENCODE,
				"%s: label %u: can't encode %u nexthops (maximum is %u)",
				__func__, zl->local_label, zl->nexthop_num,
				MULTIPATH_NUM);
			return -1;
		}
		stream_putw(s, zl->backup_nexthop_num);

		for (int i = 0; i < zl->backup_nexthop_num; i++) {
			znh = &zl->backup_nexthops[i];

			if (zapi_nexthop_encode(s, znh, 0, 0) < 0)
				return -1;
		}

	}

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return 0;
}

/*
 * 解码标签条目(与 labels_encode 对称)：读 message/type/local_label；含 FTN
 * 时读前缀(校验长度)与路由类型/实例；读 nexthop 数与各 nexthop(超量时告警
 * 并截断到 MULTIPATH_NUM，黑洞 nexthop 不可用则报错)；含 HAS_BACKUPS 时同
 * 样解码备份 nexthop。返回 0 成功，-1 失败。
 */
int zapi_labels_decode(struct stream *s, struct zapi_labels *zl)
{
	struct zapi_nexthop *znh;

	memset(zl, 0, sizeof(*zl));

	/* Get data. */
	STREAM_GETC(s, zl->message);
	STREAM_GETC(s, zl->type);
	STREAM_GETL(s, zl->local_label);

	if (CHECK_FLAG(zl->message, ZAPI_LABELS_FTN)) {
		size_t psize;

		STREAM_GETW(s, zl->route.prefix.family);
		STREAM_GETC(s, zl->route.prefix.prefixlen);

		psize = PSIZE(zl->route.prefix.prefixlen);
		switch (zl->route.prefix.family) {
		case AF_INET:
			if (zl->route.prefix.prefixlen > IPV4_MAX_BITLEN) {
				zlog_debug(
					"%s: Specified prefix length %d is greater than a v4 address can support",
					__func__, zl->route.prefix.prefixlen);
				return -1;
			}
			STREAM_GET(&zl->route.prefix.u.prefix4.s_addr, s,
				   psize);
			break;
		case AF_INET6:
			if (zl->route.prefix.prefixlen > IPV6_MAX_BITLEN) {
				zlog_debug(
					"%s: Specified prefix length %d is greater than a v6 address can support",
					__func__, zl->route.prefix.prefixlen);
				return -1;
			}
			STREAM_GET(&zl->route.prefix.u.prefix6, s, psize);
			break;
		default:
			flog_err(EC_LIB_ZAPI_ENCODE,
				 "%s: Specified family %u is not v4 or v6",
				 __func__, zl->route.prefix.family);
			return -1;
		}

		STREAM_GETC(s, zl->route.type);
		STREAM_GETW(s, zl->route.instance);
	}

	STREAM_GETW(s, zl->nexthop_num);

	if (zl->nexthop_num > MULTIPATH_NUM) {
		flog_warn(
			EC_LIB_ZAPI_ENCODE,
			"%s: Prefix %pFX has %d nexthops, but we can only use the first %d",
			__func__, &zl->route.prefix, zl->nexthop_num,
			MULTIPATH_NUM);
	}

	zl->nexthop_num = MIN(MULTIPATH_NUM, zl->nexthop_num);

	for (int i = 0; i < zl->nexthop_num; i++) {
		znh = &zl->nexthops[i];

		if (zapi_nexthop_decode(s, znh, 0, 0) < 0)
			return -1;

		if (znh->type == NEXTHOP_TYPE_BLACKHOLE) {
			flog_warn(
				EC_LIB_ZAPI_ENCODE,
				"%s: Prefix %pFX has a blackhole nexthop which we cannot use for a label",
				__func__, &zl->route.prefix);
			return -1;
		}
	}

	if (CHECK_FLAG(zl->message, ZAPI_LABELS_HAS_BACKUPS)) {
		STREAM_GETW(s, zl->backup_nexthop_num);

		if (zl->backup_nexthop_num > MULTIPATH_NUM) {
			flog_warn(
				EC_LIB_ZAPI_ENCODE,
				"%s: Prefix %pFX has %d backup nexthops, but we can only use the first %d",
				__func__, &zl->route.prefix,
				zl->backup_nexthop_num,	MULTIPATH_NUM);
		}

		zl->backup_nexthop_num = MIN(MULTIPATH_NUM,
					     zl->backup_nexthop_num);

		for (int i = 0; i < zl->backup_nexthop_num; i++) {
			znh = &zl->backup_nexthops[i];

			if (zapi_nexthop_decode(s, znh, 0, 0) < 0)
				return -1;

			if (znh->type == NEXTHOP_TYPE_BLACKHOLE) {
				flog_warn(
					EC_LIB_ZAPI_ENCODE,
					"%s: Prefix %pFX has a backup blackhole nexthop which we cannot use for a label",
					__func__, &zl->route.prefix);
				return -1;
			}
		}
	}

	return 0;
stream_failure:
	return -1;
}

/*
 * 下发伪线(zapi_pw)：header(command) + ifname(定长) + ifindex + type +
 * af(据 af 写 v4 地址或 16 字节 v6 地址，未知 af 报错) + local/remote
 * label + flags + 协议私有字段(定长)。回填长度并发送。返回发送状态。
 */
enum zclient_send_status zebra_send_pw(struct zclient *zclient, int command,
				       struct zapi_pw *pw)
{
	struct stream *s;

	/* Reset stream. */
	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, command, VRF_DEFAULT);
	stream_write(s, pw->ifname, IFNAMSIZ);
	stream_putl(s, pw->ifindex);

	/* Put type */
	stream_putl(s, pw->type);

	/* Put nexthop */
	stream_putl(s, pw->af);
	switch (pw->af) {
	case AF_INET:
		stream_put_in_addr(s, &pw->nexthop.ipv4);
		break;
	case AF_INET6:
		stream_write(s, (uint8_t *)&pw->nexthop.ipv6, 16);
		break;
	default:
		flog_err(EC_LIB_ZAPI_ENCODE, "%s: unknown af", __func__);
		return ZCLIENT_SEND_FAILURE;
	}

	/* Put labels */
	stream_putl(s, pw->local_label);
	stream_putl(s, pw->remote_label);

	/* Put flags */
	stream_putc(s, pw->flags);

	/* Protocol specific fields */
	stream_write(s, &pw->data, sizeof(union pw_protocol_fields));

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/*
 * Receive PW status update from Zebra and send it to LDE process.
 */
/*
 * 读取 zebra 上报的伪线状态更新：清零 pw，读 ifname(定长)/ifindex/status。
 * 返回 0 成功，-1 失败。
 */
int zebra_read_pw_status_update(ZAPI_CALLBACK_ARGS, struct zapi_pw_status *pw)
{
	struct stream *s;

	memset(pw, 0, sizeof(struct zapi_pw_status));
	s = zclient->ibuf;

	/* Get data. */
	stream_get(pw->ifname, s, IFNAMSIZ);
	STREAM_GETL(s, pw->ifindex);
	STREAM_GETL(s, pw->status);

	return 0;
stream_failure:
	return -1;
}

/*
 * 处理 ZEBRA_CAPABILITIES：读 vrf_backend 并 vrf_configure_backend；读
 * mpls_enabled/ecmp/role/v6_with_v4_nexthop 组成能力快照；若有
 * zebra_capabilities 回调则调用它。返回 0。
 */
static int zclient_capability_decode(ZAPI_CALLBACK_ARGS)
{
	struct zclient_capabilities cap;
	struct stream *s = zclient->ibuf;
	enum vrf_backend_type vrf_backend;
	uint8_t mpls_enabled;

	STREAM_GETC(s, vrf_backend);

	vrf_configure_backend(vrf_backend);

	memset(&cap, 0, sizeof(cap));
	STREAM_GETC(s, mpls_enabled);
	cap.mpls_enabled = !!mpls_enabled;
	STREAM_GETL(s, cap.ecmp);
	STREAM_GETC(s, cap.role);
	STREAM_GETC(s, cap.v6_with_v4_nexthop);

	if (zclient->zebra_capabilities)
		(*zclient->zebra_capabilities)(&cap);

stream_failure:
	return 0;
}

/*
 * 向 zebra 注册 MLAG 客户端：发 ZEBRA_MLAG_CLIENT_REGISTER，payload 为
 * bit_map(感兴趣的事件位)。返回发送状态。
 */
enum zclient_send_status zclient_send_mlag_register(struct zclient *client,
						    uint32_t bit_map)
{
	struct stream *s;

	s = client->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_MLAG_CLIENT_REGISTER, VRF_DEFAULT);
	stream_putl(s, bit_map);

	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(client);
}

/*
 * 向 zebra 注销 MLAG 客户端：发 ZEBRA_MLAG_CLIENT_UNREGISTER（无 payload）。
 * 返回发送状态。
 */
enum zclient_send_status zclient_send_mlag_deregister(struct zclient *client)
{
	return zebra_message_send(client, ZEBRA_MLAG_CLIENT_UNREGISTER,
				  VRF_DEFAULT);
}

/*
 * 把 MLAG 数据转发给 zebra：发 ZEBRA_MLAG_FORWARD_MSG，payload 直接拷贝
 * client_s 的原始数据（endp 字节）。返回发送状态。
 */
enum zclient_send_status zclient_send_mlag_data(struct zclient *client,
						struct stream *client_s)
{
	struct stream *s;

	s = client->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_MLAG_FORWARD_MSG, VRF_DEFAULT);
	stream_put(s, client_s->data, client_s->endp);

	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(client);
}

/*
 * Init/header setup for opaque zapi messages
 */
/*
 * 不透明（opaque）zapi 消息的公共初始化：写 ZEBRA_OPAQUE_MESSAGE 头，
 * 依次放子类型 type、flags、源客户端标识（redist_default / instance /
 * session_id）。返回成功，仅填充 obuf、不发送。
 */
enum zclient_send_status zapi_opaque_init(struct zclient *zclient,
					  uint32_t type, uint16_t flags)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_OPAQUE_MESSAGE, VRF_DEFAULT);

	/* Send sub-type and flags */
	stream_putl(s, type);
	stream_putw(s, flags);

	/* Source daemon identifiers */
	stream_putc(s, zclient->redist_default);
	stream_putw(s, zclient->instance);
	stream_putl(s, zclient->session_id);

	return ZCLIENT_SEND_SUCCESS;
}

/*
 * Init, header setup for opaque unicast messages.
 */
/*
 * 单播 opaque 消息初始化：在 zapi_opaque_init 基础上附加
 * ZAPI_OPAQUE_FLAG_UNICAST 标志，并写入目的客户端标识（proto / instance /
 * session_id）。返回成功，仅填充 obuf、不发送。
 */
enum zclient_send_status
zapi_opaque_unicast_init(struct zclient *zclient, uint32_t type, uint16_t flags,
			 uint8_t proto, uint16_t instance, uint32_t session_id)
{
	struct stream *s;

	s = zclient->obuf;

	/* Common init */
	zapi_opaque_init(zclient, type, flags | ZAPI_OPAQUE_FLAG_UNICAST);

	/* Send destination client info */
	stream_putc(s, proto);
	stream_putw(s, instance);
	stream_putl(s, session_id);

	return ZCLIENT_SEND_SUCCESS;
}

/*
 * Send an OPAQUE message, contents opaque to zebra. The message header
 * is a message subtype.
 */
/*
 * 发送 opaque 消息给 zebra（广播型，内容对 zebra 不透明）。先检查 obuf 是否
 * 容纳 头 + type + datasize，不足返回 FAILURE；再写头、附加 data、回填长度
 * 后发送。返回发送状态。
 */
enum zclient_send_status zclient_send_opaque(struct zclient *zclient,
					     uint32_t type, const uint8_t *data,
					     size_t datasize)
{
	struct stream *s;
	uint16_t flags = 0;

	/* Check buffer size */
	if (STREAM_SIZE(zclient->obuf) <
	    (ZEBRA_HEADER_SIZE + sizeof(type) + datasize))
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;

	zapi_opaque_init(zclient, type, flags);

	/* Send opaque data */
	if (datasize > 0)
		stream_write(s, data, datasize);

	/* Put length into the header at the start of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/*
 * Send an OPAQUE message to a specific zclient. The contents are opaque
 * to zebra.
 */
/*
 * 发送 opaque 单播消息给指定 zclient（dest proto/instance/session_id）。
 * 检查缓冲区是否容纳 zapi_opaque_msg 头 + datasize，写入头与数据、回填长度
 * 后发送。返回发送状态。
 */
enum zclient_send_status
zclient_send_opaque_unicast(struct zclient *zclient, uint32_t type,
			    uint8_t proto, uint16_t instance,
			    uint32_t session_id, const uint8_t *data,
			    size_t datasize)
{
	struct stream *s;
	uint16_t flags = 0;

	/* Check buffer size */
	if (STREAM_SIZE(zclient->obuf) <
	    (ZEBRA_HEADER_SIZE + sizeof(struct zapi_opaque_msg) + datasize))
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;

	/* Common init */
	zapi_opaque_unicast_init(zclient, type, flags, proto, instance,
				 session_id);

	/* Send opaque data */
	if (datasize > 0)
		stream_write(s, data, datasize);

	/* Put length into the header at the start of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/*
 * Decode incoming opaque message into info struct
 */
/*
 * 解析收到的 opaque 消息到 info：先清零，再取 type/flags、源标识，若置了单播
 * 标志再取目的标识，最后把剩余可读字节记为 info->len。成功返回 0，读失败
 * 走 stream_failure 返回 -1。
 */
int zclient_opaque_decode(struct stream *s, struct zapi_opaque_msg *info)
{
	memset(info, 0, sizeof(*info));

	/* Decode subtype and flags */
	STREAM_GETL(s, info->type);
	STREAM_GETW(s, info->flags);

	/* Decode sending daemon info */
	STREAM_GETC(s, info->src_proto);
	STREAM_GETW(s, info->src_instance);
	STREAM_GETL(s, info->src_session_id);

	/* Decode unicast destination info, if present */
	if (CHECK_FLAG(info->flags, ZAPI_OPAQUE_FLAG_UNICAST)) {
		STREAM_GETC(s, info->dest_proto);
		STREAM_GETW(s, info->dest_instance);
		STREAM_GETL(s, info->dest_session_id);
	}

	info->len = STREAM_READABLE(s);

	return 0;

stream_failure:

	return -1;
}

/*
 * Send a registration request for opaque messages with a specified subtype.
 */
/*
 * 向 zebra 注册对某 opaque 子类型的关注：发 ZEBRA_OPAQUE_REGISTER，payload
 * 含子类型及本客户端标识。返回发送状态。
 */
enum zclient_send_status zclient_register_opaque(struct zclient *zclient,
						 uint32_t type)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_OPAQUE_REGISTER, VRF_DEFAULT);

	/* Send sub-type */
	stream_putl(s, type);

	/* Add zclient info */
	stream_putc(s, zclient->redist_default);
	stream_putw(s, zclient->instance);
	stream_putl(s, zclient->session_id);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/*
 * Send an un-registration request for a specified opaque subtype.
 */
/*
 * 取消对某 opaque 子类型的关注：发 ZEBRA_OPAQUE_UNREGISTER，payload 同注册。
 * 返回发送状态。
 */
enum zclient_send_status zclient_unregister_opaque(struct zclient *zclient,
						   uint32_t type)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_OPAQUE_UNREGISTER, VRF_DEFAULT);

	/* Send sub-type */
	stream_putl(s, type);

	/* Add zclient info */
	stream_putc(s, zclient->redist_default);
	stream_putw(s, zclient->instance);
	stream_putl(s, zclient->session_id);

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/* Utility to decode opaque registration info */
/*
 * 解析 opaque 注册/注销信息到 info：依次取 type / proto / instance /
 * session_id。成功返回 0，读失败走 stream_failure 返回 -1。
 */
int zapi_opaque_reg_decode(struct stream *s, struct zapi_opaque_reg_info *info)
{
	STREAM_GETL(s, info->type);
	STREAM_GETC(s, info->proto);
	STREAM_GETW(s, info->instance);
	STREAM_GETL(s, info->session_id);

	return 0;

stream_failure:

	return -1;
}

/* Utility to decode client close notify info */
/*
 * 解析客户端关闭通知到 info：先清零，再取 proto / instance / session_id。
 * 成功返回 0，读失败走 stream_failure 返回 -1。
 */
int zapi_client_close_notify_decode(struct stream *s,
				    struct zapi_client_close_info *info)
{
	memset(info, 0, sizeof(*info));

	STREAM_GETC(s, info->proto);
	STREAM_GETW(s, info->instance);
	STREAM_GETL(s, info->session_id);

	return 0;

stream_failure:

	return -1;
}

/*
 * 处理 ZEBRA_NEXTHOP_UPDATE：按 vrf_id 查 VRF，解析 nexthop 更新到 match /
 * route，再回调 zclient->nexthop_update。VRF 未知告警返回 0；解析失败返回
 * -1，成功返回 0。
 */
static int zclient_nexthop_update(ZAPI_CALLBACK_ARGS)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	struct prefix match;
	struct zapi_route route;

	if (!vrf) {
		zlog_warn("nexthop update for unknown VRF ID %u", vrf_id);
		return 0;
	}

	if (!zapi_nexthop_update_decode(zclient->ibuf, &match, &route)) {
		zlog_err("failed to decode nexthop update");
		return -1;
	}

	if (zclient->nexthop_update)
		zclient->nexthop_update(vrf, &match, &route);

	return 0;
}

/*
 * lib 内置的 ZAPI 命令处理器表，按 command 索引。仅处理各守护进程共用的
 * 消息（能力、错误、VRF 与接口、NHT 预解码、BFD）。auxiliary 类型的
 * zclient 不执行此表，改由协议自己的 handlers 处理。
 */
static zclient_handler *const lib_handlers[] = {
	/* fundamentals */
	[ZEBRA_CAPABILITIES] = zclient_capability_decode,
	[ZEBRA_ERROR] = zclient_handle_error,

	/* VRF & interface code is shared in lib */
	[ZEBRA_VRF_ADD] = zclient_vrf_add,
	[ZEBRA_VRF_DELETE] = zclient_vrf_delete,
	[ZEBRA_INTERFACE_ADD] = zclient_interface_add,
	[ZEBRA_INTERFACE_DELETE] = zclient_interface_delete,
	[ZEBRA_INTERFACE_UP] = zclient_interface_up,
	[ZEBRA_INTERFACE_DOWN] = zclient_interface_down,

	/* NHT pre-decode */
	[ZEBRA_NEXTHOP_UPDATE] = zclient_nexthop_update,

	/* BFD */
	[ZEBRA_BFD_DEST_REPLAY] = zclient_bfd_session_replay,
	[ZEBRA_INTERFACE_BFD_DEST_UPDATE] = zclient_bfd_session_update,
};

/* Zebra client message read function. */
/*
 * zebra 连接可读事件回调：先补全 10 字节 zmsghdr，校验 marker/version 与
 * 长度（过大时自动扩容 ibuf），再补全整包；然后按 command 依次调用
 * lib_handlers 与 zclient->handlers。连接断开或出错走 zclient_failed，
 * 未读满则重新注册读事件等待后续数据。无返回值。
 */
static void zclient_read(struct event *event)
{
	size_t already;
	uint16_t length, command;
	uint8_t marker, version;
	vrf_id_t vrf_id;
	struct zclient *zclient;

	/* Get socket to zebra. */
	zclient = EVENT_ARG(event);
	zclient->t_read = NULL;

	/* Read zebra header (if we don't have it already). */
	already = stream_get_endp(zclient->ibuf);
	if (already < ZEBRA_HEADER_SIZE) {
		ssize_t nbyte;
		if (((nbyte = stream_read_try(zclient->ibuf, zclient->sock,
					      ZEBRA_HEADER_SIZE - already))
		     == 0)
		    || (nbyte == -1)) {
			if (zclient_debug)
				zlog_debug(
					"zclient connection closed socket [%d].",
					zclient->sock);
			zclient_failed(zclient);
			return;
		}
		if (nbyte != (ssize_t)(ZEBRA_HEADER_SIZE - already)) {
			zclient_event(ZCLIENT_READ, zclient);
			return;
		}
		already = ZEBRA_HEADER_SIZE;
	}

	/* Reset to read from the beginning of the incoming packet. */
	stream_set_getp(zclient->ibuf, 0);

	/* Fetch header values. */
	length = stream_getw(zclient->ibuf);
	marker = stream_getc(zclient->ibuf);
	version = stream_getc(zclient->ibuf);
	vrf_id = stream_getl(zclient->ibuf);
	command = stream_getw(zclient->ibuf);

	if (marker != ZEBRA_HEADER_MARKER || version != ZSERV_VERSION) {
		flog_err(
			EC_LIB_ZAPI_MISSMATCH,
			"%s: socket %d version mismatch, marker %d, version %d",
			__func__, zclient->sock, marker, version);
		zclient_failed(zclient);
		return;
	}

	if (length < ZEBRA_HEADER_SIZE) {
		flog_err(EC_LIB_ZAPI_MISSMATCH,
			 "%s: socket %d message length %u is less than %d ",
			 __func__, zclient->sock, length, ZEBRA_HEADER_SIZE);
		zclient_failed(zclient);
		return;
	}

	/* Length check. */
	if (length > STREAM_SIZE(zclient->ibuf)) {
		struct stream *ns;
		flog_err(
			EC_LIB_ZAPI_ENCODE,
			"%s: message size %u exceeds buffer size %lu, expanding...",
			__func__, length,
			(unsigned long)STREAM_SIZE(zclient->ibuf));
		ns = stream_new(length);
		stream_copy(ns, zclient->ibuf);
		stream_free(zclient->ibuf);
		zclient->ibuf = ns;
	}

	/* Read rest of zebra packet. */
	if (already < length) {
		ssize_t nbyte;
		if (((nbyte = stream_read_try(zclient->ibuf, zclient->sock,
					      length - already))
		     == 0)
		    || (nbyte == -1)) {
			if (zclient_debug)
				zlog_debug(
					"zclient connection closed socket [%d].",
					zclient->sock);
			zclient_failed(zclient);
			return;
		}
		if (nbyte != (ssize_t)(length - already)) {
			/* Try again later. */
			zclient_event(ZCLIENT_READ, zclient);
			return;
		}
	}

	length -= ZEBRA_HEADER_SIZE;

	if (zclient_debug)
		zlog_debug("zclient %p command %s VRF %u", zclient,
			   zserv_command_string(command), vrf_id);

	if (!zclient->auxiliary && command < array_size(lib_handlers) &&
	    lib_handlers[command])
		lib_handlers[command](command, zclient, length, vrf_id);
	if (command < zclient->n_handlers && zclient->handlers[command])
		zclient->handlers[command](command, zclient, length, vrf_id);

	if (zclient->sock < 0)
		/* Connection was closed during packet processing. */
		return;

	/* Register read event. */
	stream_reset(zclient->ibuf);
	zclient_event(ZCLIENT_READ, zclient);
}

/*
 * table-direct 重分发的内部实现：此处 instance 语义为 table_id。维护
 * mi_redist[afi][TABLE_DIRECT] 中的表记录（增/删时先判重），仅在状态真正
 * 变化且 socket 有效时向 zebra 发送重分发命令。无返回值。
 */
static void zclient_redistribute_table_direct(struct zclient *zclient, vrf_id_t vrf_id, afi_t afi,
					      int instance, int command)
{
	struct redist_proto *red = &zclient->mi_redist[afi][ZEBRA_ROUTE_TABLE_DIRECT];
	bool has_table;
	struct redist_table_direct table = {
		.vrf_id = vrf_id,
		.table_id = instance,
	};

	has_table = redist_lookup_table_direct(red, &table);

	if (command == ZEBRA_REDISTRIBUTE_ADD) {
		if (has_table)
			return;

		redist_add_table_direct(red, &table);
	} else {
		if (!has_table)
			return;

		redist_del_table_direct(red, &table);
	}

	if (zclient->sock > 0)
		zebra_redistribute_send(command, zclient, afi, ZEBRA_ROUTE_TABLE_DIRECT, instance,
					vrf_id);
}

/*
 * 向 zebra 增/删一条重分发请求（command 为 ADD/DELETE）。type 为
 * TABLE_DIRECT 时交内部函数处理；否则按 instance 是否为 0，分别走
 * mi_redist 实例表或多 VRF 位图记录，去重后发 zebra_redistribute_send。
 * 无返回值；socket 未就绪时仅更新本地记录、不发送。
 */
void zclient_redistribute(int command, struct zclient *zclient, afi_t afi,
			  int type, unsigned short instance, vrf_id_t vrf_id)
{
	/*
	 * When asking for table-direct redistribution the parameter
	 * `instance` has a different meaning: it means table
	 * identification.
	 *
	 * The table identification information is stored in
	 * `zclient->mi_redist` along with the VRF identification
	 * information in a pair (different from the usual single protocol
	 * instance value).
	 */
	if (type == ZEBRA_ROUTE_TABLE_DIRECT) {
		zclient_redistribute_table_direct(zclient, vrf_id, afi, instance, command);
		return;
	}

	if (instance) {
		if (command == ZEBRA_REDISTRIBUTE_ADD) {
			if (redist_check_instance(
				    &zclient->mi_redist[afi][type], instance))
				return;
			redist_add_instance(&zclient->mi_redist[afi][type],
					    instance);
		} else {
			if (!redist_check_instance(
				    &zclient->mi_redist[afi][type], instance))
				return;
			redist_del_instance(&zclient->mi_redist[afi][type],
					    instance);
		}

	} else {
		if (command == ZEBRA_REDISTRIBUTE_ADD) {
			if (vrf_bitmap_check(&zclient->redist[afi][type],
					     vrf_id))
				return;
			vrf_bitmap_set(&zclient->redist[afi][type], vrf_id);
		} else {
			if (!vrf_bitmap_check(&zclient->redist[afi][type],
					      vrf_id))
				return;
			vrf_bitmap_unset(&zclient->redist[afi][type], vrf_id);
		}
	}

	if (zclient->sock > 0)
		zebra_redistribute_send(command, zclient, afi, type, instance,
					vrf_id);
}


/*
 * 增/删默认路由重分发（ADD/DELETE）。按 vrf_id 维护
 * default_information[afi] 位图，去重后发 zebra_redistribute_default_send。
 * 无返回值；socket 未就绪时仅更新位图。
 */
void zclient_redistribute_default(int command, struct zclient *zclient,
				  afi_t afi, vrf_id_t vrf_id)
{

	if (command == ZEBRA_REDISTRIBUTE_DEFAULT_ADD) {
		if (vrf_bitmap_check(&zclient->default_information[afi],
				     vrf_id))
			return;
		vrf_bitmap_set(&zclient->default_information[afi], vrf_id);
	} else {
		if (!vrf_bitmap_check(&zclient->default_information[afi],
				      vrf_id))
			return;
		vrf_bitmap_unset(&zclient->default_information[afi], vrf_id);
	}

	if (zclient->sock > 0)
		zebra_redistribute_default_send(command, zclient, afi, vrf_id);
}

/*
 * zclient 重连节奏参数：快速重连间隔 1 秒，慢速 5 秒，失败次数达到 30 后
 * 切换到慢速重连，避免对 zebra 造成连接风暴。
 */
#define ZCLIENT_QUICK_RECONNECT 1
#define ZCLIENT_SLOW_RECONNECT	5
#define ZCLIENT_SWITCH_TO_SLOW	30

/*
 * 按事件类型驱动 zclient 状态机：SCHEDULE 立即调度连接、CONNECT 按失败
 * 次数选择快/慢间隔的定时连接、READ 注册读事件。无返回值。
 */
static void zclient_event(enum zclient_event event, struct zclient *zclient)
{
	switch (event) {
	case ZCLIENT_SCHEDULE:
		event_add_event(zclient->master, zclient_connect, zclient, 0,
				&zclient->t_connect);
		break;
	case ZCLIENT_CONNECT:
		if (zclient_debug)
			zlog_debug("zclient connect failures: %d schedule interval is now %d",
				   zclient->fail,
				   zclient->fail < ZCLIENT_SWITCH_TO_SLOW ? ZCLIENT_QUICK_RECONNECT
									  : ZCLIENT_SLOW_RECONNECT);
		event_add_timer(zclient->master, zclient_connect, zclient,
				zclient->fail < ZCLIENT_SWITCH_TO_SLOW ? ZCLIENT_QUICK_RECONNECT
								       : ZCLIENT_SLOW_RECONNECT,
				&zclient->t_connect);
		break;
	case ZCLIENT_READ:
		zclient->t_read = NULL;
		event_add_read(zclient->master, zclient_read, zclient,
			       zclient->sock, &zclient->t_read);
		break;
	}
}

/*
 * 请求 zebra 设置接口的 ARP 开关：发 ZEBRA_INTERFACE_SET_ARP，payload 为
 * ifindex + arp_enable。返回发送状态。
 */
enum zclient_send_status zclient_interface_set_arp(struct zclient *client,
						   struct interface *ifp,
						   bool arp_enable)
{
	struct stream *s;

	s = client->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_INTERFACE_SET_ARP, ifp->vrf->vrf_id);

	stream_putl(s, ifp->ifindex);
	stream_putc(s, arp_enable);

	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(client);
}

/*
 * 请求 zebra 把 slave 接口挂到 master 接口（如 bridge）下：发
 * ZEBRA_INTERFACE_SET_MASTER，payload 依次为 master/slave 的 vrf_id 与
 * ifindex。返回发送状态。
 */
enum zclient_send_status zclient_interface_set_master(struct zclient *client,
						      struct interface *master,
						      struct interface *slave)
{
	struct stream *s;

	s = client->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_INTERFACE_SET_MASTER,
			      master->vrf->vrf_id);

	stream_putl(s, master->vrf->vrf_id);
	stream_putl(s, master->ifindex);
	stream_putl(s, slave->vrf->vrf_id);
	stream_putl(s, slave->ifindex);

	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(client);
}

/*
 * Send capabilities message to zebra
 */
/*
 * 向 zebra 发送客户端能力（如 GR、RIB stale 时间、路由更新完成/挂起等）：
 * 按 api->cap 分派不同 payload 布局，写头后发送。zclient 为空返回 FAILURE；
 * 否则返回发送状态。
 */
enum zclient_send_status zclient_capabilities_send(uint32_t cmd,
						   struct zclient *zclient,
						   struct zapi_cap *api)
{

	struct stream *s;

	if (zclient == NULL)
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, cmd, 0);
	stream_putl(s, api->cap);

	switch (api->cap) {
	case ZEBRA_CLIENT_GR_CAPABILITIES:
	case ZEBRA_CLIENT_RIB_STALE_TIME:
		stream_putl(s, api->stale_removal_time);
		stream_putl(s, api->vrf_id);
		break;
	case ZEBRA_CLIENT_ROUTE_UPDATE_COMPLETE:
	case ZEBRA_CLIENT_ROUTE_UPDATE_PENDING:
		stream_putl(s, api->afi);
		stream_putl(s, api->safi);
		stream_putl(s, api->vrf_id);
		break;
	case ZEBRA_CLIENT_GR_DISABLE:
		stream_putl(s, api->vrf_id);
		break;
	}

	/* Put length at the first point of the stream */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/*
 * Process capabilities message from zebra
 */
/*
 * 解析来自 zebra 的能力消息到 api：先清零并把 safi 置默认 UNICAST，再按
 * cap 类型读取对应字段。成功返回 0，读失败走 stream_failure 返回 0
 * （注意此处失败仍返回 0）。
 */
int32_t zapi_capabilities_decode(struct stream *s, struct zapi_cap *api)
{

	memset(api, 0, sizeof(*api));

	api->safi = SAFI_UNICAST;

	STREAM_GETL(s, api->cap);
	switch (api->cap) {
	case ZEBRA_CLIENT_GR_CAPABILITIES:
	case ZEBRA_CLIENT_RIB_STALE_TIME:
		STREAM_GETL(s, api->stale_removal_time);
		STREAM_GETL(s, api->vrf_id);
		break;
	case ZEBRA_CLIENT_ROUTE_UPDATE_COMPLETE:
	case ZEBRA_CLIENT_ROUTE_UPDATE_PENDING:
		STREAM_GETL(s, api->afi);
		STREAM_GETL(s, api->safi);
		STREAM_GETL(s, api->vrf_id);
		break;
	case ZEBRA_CLIENT_GR_DISABLE:
		STREAM_GETL(s, api->vrf_id);
		break;
	}
stream_failure:
	return 0;
}

/*
 * 请求 zebra 对接口 ifp 上的地址 p 发起邻居发现：发 ZEBRA_NEIGH_DISCOVER，
 * payload 为 ifindex + 地址族/前缀长度 + 前缀字节。返回发送状态。
 */
enum zclient_send_status
zclient_send_neigh_discovery_req(struct zclient *zclient,
				 const struct interface *ifp,
				 const struct prefix *p)
{
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_NEIGH_DISCOVER, ifp->vrf->vrf_id);
	stream_putl(s, ifp->ifindex);

	stream_putc(s, p->family);
	stream_putc(s, p->prefixlen);
	stream_put(s, &p->u.prefix, prefix_blen(p));

	stream_putw_at(s, 0, stream_get_endp(s));
	return zclient_send_message(zclient);
}

/*
 * Get a starting nhg point for a routing protocol
 */
/*
 * 返回某路由协议可用的 NHG（nexthop group）ID 起始值：各协议按
 * ZEBRA_NHG_PROTO_SPACING 分段以避免 ID 冲突。proto 越界触发断言。
 * 返回该协议 NHG ID 空间的下界。
 */
uint32_t zclient_get_nhg_start(uint32_t proto)
{
	assert(proto < ZEBRA_ROUTE_MAX);

	return ZEBRA_NHG_PROTO_SPACING * proto;
}

/*
 * 把 ZEBRA_FLAG_* 路由标志解码成可读字符串写入 buf（无标志时输出 "None "）。
 * 用于日志/调试。返回 buf。
 */
char *zclient_dump_route_flags(uint32_t flags, char *buf, size_t len)
{
	if (flags == 0) {
		snprintfrr(buf, len, "None ");
		return buf;
	}

	snprintfrr(buf, len, "%s%s%s%s%s%s%s%s%s%s%s",
		   CHECK_FLAG(flags, ZEBRA_FLAG_ALLOW_RECURSION) ? "Recursion "
								 : "",

		   CHECK_FLAG(flags, ZEBRA_FLAG_SELFROUTE) ? "Self " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_IBGP) ? "iBGP " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_SELECTED) ? "Selected " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_FIB_OVERRIDE) ? "Override " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_EVPN_ROUTE) ? "Evpn " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_RR_USE_DISTANCE) ? "RR Distance "
								 : "",

		   CHECK_FLAG(flags, ZEBRA_FLAG_TRAPPED) ? "Trapped " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_OFFLOADED) ? "Offloaded " : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_OFFLOAD_FAILED)
			   ? "Offload Failed "
			   : "",
		   CHECK_FLAG(flags, ZEBRA_FLAG_OUTOFSYNC) ? "OutOfSync " : "");

	return buf;
}

/*
 * 把 ZEBRA_MACIP_TYPE_* 标志解码成可读字符串写入 buf（无标志时 "None "）。
 * 用于 EVPN MAC/IP 调试。返回 buf。
 */
char *zclient_evpn_dump_macip_flags(uint8_t flags, char *buf, size_t len)
{
	if (flags == 0) {
		snprintfrr(buf, len, "None ");
		return buf;
	}

	snprintfrr(
		buf, len, "%s%s%s%s%s%s%s",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_STICKY) ? "Sticky MAC " : "",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_GW) ? "Gateway MAC " : "",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_ROUTER_FLAG) ? "Router "
								: "",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_OVERRIDE_FLAG) ? "Override "
								  : "",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_SVI_IP) ? "SVI MAC " : "",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_PROXY_ADVERT) ? "Proxy "
								 : "",
		CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_SYNC_PATH) ? "Sync " : "");

	return buf;
}

/*
 * 从流 s 读一个邻居地址到 add：取地址族（仅接受 AF_INET/AF_INET6），按其
 * 长度读入地址并设置 ipa_type。成功返回 0，族非法或读失败返回 -1。
 */
static int zclient_neigh_ip_read_entry(struct stream *s, struct ipaddr *add)
{
	uint8_t family;

	STREAM_GETC(s, family);
	if (family != AF_INET && family != AF_INET6)
		return -1;

	STREAM_GET(&add->ip.addr, s, family2addrsize(family));
	add->ipa_type = family;
	return 0;
 stream_failure:
	return -1;
}

/*
 * 编码邻居 IP 消息：写头(cmd/ifp->vrf)，写入 in 地址，out 非空则写其地址
 * 否则以 AF_UNSPEC 占位，再写 ip_len、ifindex 与 ndm_state（无 out 时用
 * ZEBRA_NEIGH_STATE_FAILED）。返回 ret（当前恒为 0）。
 */
int zclient_neigh_ip_encode(struct stream *s, uint16_t cmd, union sockunion *in,
			    union sockunion *out, struct interface *ifp,
			    int ndm_state, int ip_len)
{
	int ret = 0;

	zclient_create_header(s, cmd, ifp->vrf->vrf_id);
	stream_putc(s, sockunion_family(in));
	stream_write(s, sockunion_get_addr(in), sockunion_get_addrlen(in));
	if (out && sockunion_family(out) != AF_UNSPEC) {
		stream_putc(s, sockunion_family(out));
		stream_write(s, sockunion_get_addr(out),
			     sockunion_get_addrlen(out));
	} else
		stream_putc(s, AF_UNSPEC);
	stream_putl(s, ip_len);
	stream_putl(s, ifp->ifindex);
	if (out)
		stream_putl(s, ndm_state);
	else
		stream_putl(s, ZEBRA_NEIGH_STATE_FAILED);
	return ret;
}

/*
 * 解析邻居 IP 解码入 api：先读 ip_in（必需），再读 ip_out，最后取 ip_len /
 * index / ndm_state。成功返回 0，任一失败返回 -1。
 */
int zclient_neigh_ip_decode(struct stream *s, struct zapi_neigh_ip *api)
{
	int ret;

	ret = zclient_neigh_ip_read_entry(s, &api->ip_in);
	if (ret < 0)
		return -1;
	zclient_neigh_ip_read_entry(s, &api->ip_out);

	STREAM_GETL(s, api->ip_len);
	STREAM_GETL(s, api->index);
	STREAM_GETL(s, api->ndm_state);
	return 0;
 stream_failure:
	return -1;
}

/*
 * 请求 zebra 上报 GRE 信息：发 ZEBRA_GRE_GET（ifp 的 vrf_id + ifindex）。
 * client 为空或 socket 未就绪时打印错误并返回 -1，否则发送后返回 0。
 */
int zclient_send_zebra_gre_request(struct zclient *client,
				   struct interface *ifp)
{
	struct stream *s;

	if (!client || client->sock < 0) {
		zlog_err("%s : zclient not ready", __func__);
		return -1;
	}
	s = client->obuf;
	stream_reset(s);
	zclient_create_header(s, ZEBRA_GRE_GET, ifp->vrf->vrf_id);
	stream_putl(s, ifp->ifindex);
	stream_putw_at(s, 0, stream_get_endp(s));
	zclient_send_message(client);
	return 0;
}


/*
 * Opaque notification features
 */

/*
 * Common encode helper for opaque notifications, both registration
 * and async notification messages.
 */
/*
 * opaque 通知（注册与异步通知）的公共编码：写 ZEBRA_OPAQUE_NOTIFY 头，依次
 * 放 request 标志、reg 标志、msg_type、及 proto/instance/session_id，最后
 * 回填长度。返回 ret（当前恒为 0）。
 */
static int opaque_notif_encode_common(struct stream *s, uint32_t msg_type,
				      bool request, bool reg, uint8_t proto,
				      uint16_t instance, uint32_t session_id)
{
	int ret = 0;
	uint8_t val = 0;

	stream_reset(s);

	zclient_create_header(s, ZEBRA_OPAQUE_NOTIFY, VRF_DEFAULT);

	/* Notification or request */
	if (request)
		val = 1;
	stream_putc(s, val);

	if (reg)
		val = 1;
	else
		val = 0;
	stream_putc(s, val);

	stream_putl(s, msg_type);

	stream_putc(s, proto);
	stream_putw(s, instance);
	stream_putl(s, session_id);

	/* And capture message length */
	stream_putw_at(s, 0, stream_get_endp(s));

	return ret;
}

/*
 * Encode a zapi opaque message type notification into buffer 's'
 */
/*
 * 编码 opaque 消息类型通知到流 s：调用公共编码（request=false，reg 由参数
 * 指定）。返回公共编码的返回值。
 */
int zclient_opaque_notif_encode(struct stream *s, uint32_t msg_type, bool reg,
				uint8_t proto, uint16_t instance,
				uint32_t session_id)
{
	return opaque_notif_encode_common(s, msg_type, false /* !request */,
					  reg, proto, instance, session_id);
}

/*
 * Decode an incoming zapi opaque message type notification
 */
/*
 * 解析收到的 opaque 通知到 info：先清零，再取 request/reg 标志、msg_type 及
 * proto/instance/session_id。成功返回 0，读失败走 stream_failure 返回 -1。
 */
int zclient_opaque_notif_decode(struct stream *s,
				struct zapi_opaque_notif_info *info)
{
	uint8_t val;

	memset(info, 0, sizeof(*info));

	STREAM_GETC(s, val); /* Registration or notification */
	info->request = (val != 0);

	STREAM_GETC(s, val);
	info->reg = (val != 0);

	STREAM_GETL(s, info->msg_type);

	STREAM_GETC(s, info->proto);
	STREAM_GETW(s, info->instance);
	STREAM_GETL(s, info->session_id);

	return 0;

stream_failure:
	return -1;
}

/*
 * Encode and send a zapi opaque message type notification request to zebra
 */
/*
 * 向 zebra 请求某 opaque 消息类型的通知：以 request=true、register=true 编码
 * 并发送。zclient 为空或 socket 未就绪返回 FAILURE，否则返回发送状态。
 */
enum zclient_send_status zclient_opaque_request_notify(struct zclient *zclient,
						       uint32_t msgtype)
{
	struct stream *s;

	if (!zclient || zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;

	opaque_notif_encode_common(s, msgtype, true /* request */,
				   true /* register */, zclient->redist_default,
				   zclient->instance, zclient->session_id);

	return zclient_send_message(zclient);
}

/*
 * Encode and send a request to drop notifications for an opaque message type.
 */
/*
 * 向 zebra 请求停止某 opaque 消息类型的通知：以 request=true、register=false
 * 编码并发送。zclient 为空或 socket 未就绪返回 FAILURE。
 */
enum zclient_send_status zclient_opaque_drop_notify(struct zclient *zclient,
						    uint32_t msgtype)
{
	struct stream *s;

	if (!zclient || zclient->sock < 0)
		return ZCLIENT_SEND_FAILURE;

	s = zclient->obuf;

	opaque_notif_encode_common(s, msgtype, true /* req */,
				   false /* unreg */, zclient->redist_default,
				   zclient->instance, zclient->session_id);

	return zclient_send_message(zclient);
}

/*
 * 向 zebra 注册/注销某 VRF、地址族的邻居（reg=true 发 ZEBRA_NEIGH_REGISTER，
 * 否则发 ZEBRA_NEIGH_UNREGISTER，payload 为 afi）。zclient 为空或 socket
 * 未就绪则直接返回。无返回值。
 */
void zclient_register_neigh(struct zclient *zclient, vrf_id_t vrf_id, afi_t afi,
			    bool reg)
{
	struct stream *s;

	if (!zclient || zclient->sock < 0)
		return;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s,
			      reg ? ZEBRA_NEIGH_REGISTER
				  : ZEBRA_NEIGH_UNREGISTER,
			      vrf_id);
	stream_putw(s, afi);
	stream_putw_at(s, 0, stream_get_endp(s));
	zclient_send_message(zclient);
}

/*
 * 请求 zebra 获取接口 ifp 上某地址族的邻居：发 ZEBRA_NEIGH_GET（ifindex +
 * afi）。zclient 为空或 socket 未就绪时打印错误并返回。无返回值。
 */
void zclient_neigh_get(struct zclient *zclient, struct interface *ifp, afi_t afi)
{
	struct stream *s;

	if (!zclient || zclient->sock < 0) {
		zlog_err("%s : zclient not connected", __func__);
		return;
	}

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, ZEBRA_NEIGH_GET, VRF_DEFAULT);
	stream_putl(s, ifp->ifindex);
	stream_putw(s, afi);

	stream_putw_at(s, 0, stream_get_endp(s));
	zclient_send_message(zclient);
}
